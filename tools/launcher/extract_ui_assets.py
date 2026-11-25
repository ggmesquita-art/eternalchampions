#!/usr/bin/env python3
"""extract_ui_assets.py - extrai a arte de UI do jogo (tema do launcher) da ROM do usuario.

Nada daqui e versionado: a saida vai para %APPDATA%\\EternalChampionsRecomp\\ui\\ (ou --out).

Pipeline
  1. Roda o EternalChampionsRecomp.exe headless (SDL dummy) com um --input-script que
     chega a tela de abertura (titulo) e ao menu principal; captura SCREENSHOT nativo
     (320x224) e DUMP_VRAM. O runner NAO expoe CRAM; a paleta e recuperada por votacao:
     cada pixel nao-transparente de plano A / sprites e comparado com a cor que aparece no
     screenshot (modo estatistico por (linha de paleta, indice)).
  2. Roda o jogo 3x em paralelo com janela real + B+ v3 HQ (GL) e grava a saida 4x via
     EC_BPLUS_CAPTURE/_AT nos mesmos frames. Sem GL/falha -> upscale nearest 4x (marcado).
  3. Recorta logo, emblem, stone_tile, yinyang_NN, title_splash, header_main_menu,
     fontes (font_menu, font_gold) e escreve manifest.json.

Uso:  python extract_ui_assets.py [--rom R] [--exe E] [--out DIR] [--no-bplus] [--force]
Codigo de saida: 0 ok / ja atual, 1 erro.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import numpy as np
from PIL import Image

EXTRACTOR_VERSION = 1
ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT / "build" / "EternalChampionsRecomp.exe"
DEFAULT_ROM = ROOT / "roms" / "eternal_champions_us.gen"
DEFAULT_MODEL = ROOT / "assets" / "models" / "ec_bplus_v3_hq_weights.bin"

# ---- cronograma deterministico do jogo (frames do script; ver harness/ec_harness boot) ----
TITLE_FRAME = 1236          # tela de abertura completa, antes do 3o START
MENU_FRAME = 1410           # menu principal estavel (base: recortes + VRAM)
MENU_SCAN = 130             # frames consecutivos do menu varridos (yin-yang)
SCREEN_W, SCREEN_H = 320, 224
SCALE = 4

ASCII_TILE_BASE = 0x600 - 0x1B   # tile = 0x600 + (ascii - 0x1B)   (' ' -> 0x605, '!' -> 0x606)
MISSING_PT = list("ÁÀÂÃÉÊÍÓÔÕÚÜÇáàâãéêíóôõúüç")


def log(msg: str) -> None:
    print(f"[extract_ui] {msg}", flush=True)


class ExtractError(RuntimeError):
    pass


# --------------------------------------------------------------------------- util
def sha256_file(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def default_out() -> Path:
    base = os.environ.get("APPDATA")
    if base:
        return Path(base) / "EternalChampionsRecomp" / "ui"
    return Path.home() / ".local" / "share" / "EternalChampionsRecomp" / "ui"


def fwd(p: Path) -> str:
    return str(p.resolve()).replace("\\", "/")


# --------------------------------------------------------------------------- script / runs
class ScriptBuilder:
    def __init__(self) -> None:
        self.lines: list[str] = []
        self.frame = 1          # frame do script em que a proxima diretiva executa

    def wait(self, n: int) -> None:
        if n > 0:
            self.lines.append(f"WAIT {n}")
            self.frame += n

    def wait_until(self, frame: int) -> None:
        self.wait(frame - self.frame)

    def tap(self, btn: str, hold: int = 15, post: int = 85) -> None:
        self.lines.append(f"HOLD {btn}")
        self.wait(hold)
        self.lines.append("RELEASE")
        self.wait(post)

    def boot_to_title(self) -> None:
        self.wait(800)
        self.tap("START", 15, 85)
        self.tap("START", 15, 320)          # frame 1236 = titulo completo

    def title_to_menu(self) -> None:
        self.wait_until(TITLE_FRAME + 5)
        self.tap("START", 15, 145)           # titulo -> menu principal (estavel em ~1401)

    def write(self, path: Path) -> None:
        self.lines.append("EXIT 0")
        path.write_text("\n".join(self.lines) + "\n", encoding="utf-8")


def native_script(path: Path, shots: Path) -> None:
    sb = ScriptBuilder()
    sb.boot_to_title()
    assert sb.frame == TITLE_FRAME, sb.frame
    sb.lines.append(f"SCREENSHOT {fwd(shots / 'title.png')}")
    sb.title_to_menu()
    sb.wait_until(MENU_FRAME)
    sb.lines.append(f"DUMP_VRAM {fwd(shots / 'menu.bin')}")
    for i in range(MENU_SCAN):
        sb.lines.append(f"SCREENSHOT {fwd(shots / f'menu_{MENU_FRAME + i}.png')}")
        sb.wait(1)
    sb.write(path)


def bplus_script(path: Path, target: int) -> None:
    sb = ScriptBuilder()
    sb.boot_to_title()
    if target > TITLE_FRAME:
        sb.title_to_menu()
    sb.wait_until(target + 3)
    sb.write(path)


def run_native(exe: Path, rom: Path, script: Path, timeout: float) -> None:
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software", SDL_AUDIODRIVER="dummy")
    cmd = [str(exe), "--benchmark", "10000", "--fresh-evidence", "--input-script", str(script), str(rom)]
    try:
        res = subprocess.run(cmd, cwd=str(exe.parent), env=env, capture_output=True, text=True,
                             encoding="utf-8", errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired as e:
        raise ExtractError(f"timeout ({timeout:.0f}s) rodando o jogo para captura nativa") from e
    if res.returncode != 0:
        tail = "\n".join(res.stderr.splitlines()[-8:])
        raise ExtractError(f"o jogo saiu com codigo {res.returncode} na captura nativa:\n{tail}")


def run_bplus_jobs(exe: Path, rom: Path, model: Path, jobs: dict[str, int], work: Path,
                   timeout: float) -> dict[str, Path | None]:
    """jobs: nome -> frame. Executa em paralelo; devolve nome -> png 4x (None se falhou)."""
    procs: dict[str, tuple[subprocess.Popen, Path]] = {}
    for name, frame in jobs.items():
        script = work / f"bp_{name}.txt"
        bplus_script(script, frame)
        out = work / f"bp_{name}.png"
        env = os.environ.copy()
        env.pop("SDL_VIDEODRIVER", None)
        env.pop("SDL_RENDER_DRIVER", None)
        env.update(SDL_AUDIODRIVER="dummy", EC_BPLUS_CAPTURE=str(out), EC_BPLUS_CAPTURE_AT=str(frame))
        # --runtime-uncapped + --measure-runtime: sem vsync/pacer (7 s em vez de 20 s por execucao)
        cmd = [str(exe), "--no-launcher", "--runtime-uncapped", "--measure-runtime", str(frame + 200),
               "--fresh-evidence", "--bplus-model", str(model), "--bplus-backend", "gl",
               "--bplus-dedither", "both", "--input-script", str(script), str(rom)]
        p = subprocess.Popen(cmd, cwd=str(exe.parent), env=env, stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
        procs[name] = (p, out)
    t0 = time.time()
    result: dict[str, Path | None] = {}
    for name, (p, out) in procs.items():
        left = max(1.0, timeout - (time.time() - t0))
        try:
            p.wait(timeout=left)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
            log(f"B+ '{name}': timeout")
        result[name] = out if out.exists() and out.stat().st_size > 0 else None
    return result


# --------------------------------------------------------------------------- VDP model
def decode_tiles(vram: np.ndarray) -> np.ndarray:
    t = vram.reshape(-1, 32)
    return np.stack([t >> 4, t & 15], -1).reshape(-1, 8, 8)          # [2048,8,8] indices


def tile_with_flip(tiles: np.ndarray, idx: int, hf: bool, vf: bool) -> np.ndarray:
    t = tiles[idx & 0x7FF]
    if hf:
        t = t[:, ::-1]
    if vf:
        t = t[::-1, :]
    return t


def render_plane_a(vram: np.ndarray, tiles: np.ndarray):
    """Plano A 64x32 em $0000 (reg2 do runner); retorna (idx[224,320], pal[224,320])."""
    nt = vram[0:0x1000].view(">u2").reshape(32, 64)
    hs = int(vram[0x1000:0x1002].view(">u2")[0])
    hscroll = (-int(hs & 0x3FF)) % 512
    full_i = np.zeros((256, 512), np.uint8)
    full_p = np.zeros((256, 512), np.uint8)
    for r in range(32):
        for c in range(64):
            e = int(nt[r, c])
            t = tile_with_flip(tiles, e & 0x7FF, bool(e & 0x800), bool(e & 0x1000))
            full_i[r * 8:r * 8 + 8, c * 8:c * 8 + 8] = t
            full_p[r * 8:r * 8 + 8, c * 8:c * 8 + 8] = (e >> 13) & 3
    xs = (np.arange(SCREEN_W) - hscroll) % 512
    return full_i[:SCREEN_H][:, xs], full_p[:SCREEN_H][:, xs], nt


def parse_sprites(vram: np.ndarray) -> list[dict]:
    sp = vram[0x1400:0x1400 + 80 * 8].reshape(-1, 8).astype(int)
    out, i, seen = [], 0, set()
    while i not in seen and len(out) < 80:
        seen.add(i)
        s = sp[i]
        link = int(s[3] & 0x7F)
        tl = int((s[4] << 8) | s[5])
        out.append(dict(
            idx=i, y=int(((s[0] << 8) | s[1]) & 0x3FF) - 128, x=int(((s[6] << 8) | s[7]) & 0x1FF) - 128,
            w=int(((s[2] >> 2) & 3) + 1), h=int((s[2] & 3) + 1), tile=tl & 0x7FF,
            pal=(tl >> 13) & 3, pri=tl >> 15, vf=bool(tl & 0x1000), hf=bool(tl & 0x800)))
        i = link
        if i == 0:
            break
    return out


def sprite_pixels(tiles: np.ndarray, s: dict) -> np.ndarray:
    """Indices do sprite [h*8, w*8] (tiles em ordem coluna-maior)."""
    w, h = s["w"], s["h"]
    img = np.zeros((h * 8, w * 8), np.uint8)
    for cx in range(w):
        for cy in range(h):
            img[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8] = tiles[(s["tile"] + cx * h + cy) & 0x7FF]
    if s["hf"]:
        img = img[:, ::-1]
    if s["vf"]:
        img = img[::-1, :]
    return img


def visible_sprites(sprites: list[dict]) -> list[dict]:
    vis = []
    for s in sprites:
        if s["x"] <= -s["w"] * 8 or s["x"] >= SCREEN_W or s["y"] <= -s["h"] * 8 or s["y"] >= SCREEN_H:
            continue
        vis.append(s)
    return vis


def blit_sprite(canvas_idx: np.ndarray, canvas_pal: np.ndarray, s: dict, img: np.ndarray) -> None:
    for yy in range(img.shape[0]):
        y = s["y"] + yy
        if not 0 <= y < SCREEN_H:
            continue
        for xx in range(img.shape[1]):
            x = s["x"] + xx
            if 0 <= x < SCREEN_W and img[yy, xx]:
                canvas_idx[y, x] = img[yy, xx]
                canvas_pal[y, x] = s["pal"]


def vote_palette(screen: np.ndarray, a_idx, a_pal, sprites_vis, tiles):
    """Recupera (pal, idx) -> RGB por voto. Retorna (palette[4,16,3] int, purity[4,16], count)."""
    votes: dict[tuple[int, int], dict[tuple, int]] = {}
    covered = np.zeros((SCREEN_H, SCREEN_W), bool)
    sp_layers = []
    for s in sprites_vis:
        img = sprite_pixels(tiles, s)
        sp_layers.append((s, img))
        for yy in range(img.shape[0]):
            y = s["y"] + yy
            if not 0 <= y < SCREEN_H:
                continue
            for xx in range(img.shape[1]):
                x = s["x"] + xx
                if 0 <= x < SCREEN_W and img[yy, xx]:
                    covered[y, x] = True
    # plano A (so onde nao ha sprite por cima)
    ys, xs = np.nonzero((a_idx != 0) & ~covered)
    for y, x in zip(ys, xs):
        d = votes.setdefault((int(a_pal[y, x]), int(a_idx[y, x])), {})
        c = tuple(screen[y, x])
        d[c] = d.get(c, 0) + 1
    # sprites (ultimo sprite visivel vence; votamos so se pixel nao e sobrescrito - aproximacao por modo)
    for s, img in sp_layers:
        for yy in range(img.shape[0]):
            y = s["y"] + yy
            if not 0 <= y < SCREEN_H:
                continue
            for xx in range(img.shape[1]):
                x = s["x"] + xx
                if 0 <= x < SCREEN_W and img[yy, xx]:
                    d = votes.setdefault((s["pal"], int(img[yy, xx])), {})
                    c = tuple(screen[y, x])
                    d[c] = d.get(c, 0) + 1
    pal = np.zeros((4, 16, 3), np.int32)
    purity = np.zeros((4, 16), np.float32)
    known = np.zeros((4, 16), bool)
    for (p, i), d in votes.items():
        best = max(d.items(), key=lambda kv: kv[1])
        pal[p, i] = best[0]
        purity[p, i] = best[1] / sum(d.values())
        known[p, i] = True
    return pal, purity, known


def to_rgba(idx: np.ndarray, pal_line: np.ndarray | int, palette: np.ndarray) -> np.ndarray:
    """idx[h,w]; pal_line escalar ou [h,w]. Indice 0 -> transparente."""
    if np.isscalar(pal_line):
        rgb = palette[int(pal_line)][idx]
    else:
        rgb = palette[pal_line, idx]
    a = np.where(idx != 0, 255, 0).astype(np.uint8)
    return np.dstack([rgb.astype(np.uint8), a])


def bbox_alpha(rgba: np.ndarray):
    ys, xs = np.nonzero(rgba[..., 3] > 0)
    if len(ys) == 0:
        return None
    return int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1


def crop(a: np.ndarray, box) -> np.ndarray:
    x0, y0, x1, y1 = box
    return a[y0:y1, x0:x1]


def nearest4(a: np.ndarray) -> np.ndarray:
    return np.repeat(np.repeat(a, SCALE, 0), SCALE, 1)


def erode_alpha(mask: np.ndarray, n: int) -> np.ndarray:
    m = mask.copy()
    for _ in range(n):
        p = np.pad(m, 1, constant_values=False)
        m = m & p[:-2, 1:-1] & p[2:, 1:-1] & p[1:-1, :-2] & p[1:-1, 2:]
    return m


def save_png(arr: np.ndarray, path: Path) -> list[int]:
    Image.fromarray(arr).save(path, optimize=True)
    return [int(arr.shape[1]), int(arr.shape[0])]


# --------------------------------------------------------------------------- stone tile
def detect_stone_tile(scr: np.ndarray) -> dict:
    """Detecta o rejunte (linhas escuras) e o periodo do padrao de pedra.

    Rejunte: colunas/linhas ~escuras e regulares -> bloco = intervalo entre rejuntes (80x72).
    Textura: autocorrelacao horizontal na faixa limpa abaixo do texto (periodo real da
    textura, pode ser multiplo do bloco). O bloco devolvido inclui o rejunte direito e o
    superior, logo ladrilha sem emenda."""
    g = scr.astype(np.float32).mean(2)
    strip = g[192:212, :]
    errs = {p: float(np.abs(strip[:, :-p] - strip[:, p:]).mean()) for p in range(32, 161)}
    tex_px = min(errs, key=errs.get)
    colprof = strip.mean(0)
    gcols = [i for i in range(SCREEN_W) if colprof[i] < np.median(colprof) - 25]
    gaps = sorted(b - a for a, b in zip(gcols, gcols[1:]) if b - a > 20)
    bw = gaps[len(gaps) // 2] if gaps else 80
    rows = g[:, 2:30].mean(1)
    grows = [i for i in range(SCREEN_H) if rows[i] < np.median(rows) - 40]
    rgaps = sorted(b - a for a, b in zip(grows, grows[1:]) if b - a > 20)
    bh = rgaps[len(rgaps) // 2] if rgaps else 72
    gy = grows[-1] if grows and grows[-1] + bh <= SCREEN_H else (grows[-2] if len(grows) > 1 else 143)
    gx = gcols[0] if gcols else bw - 1
    # blocos horizontais limpos de texto (texto do menu ocupa x 92..220 em y 142..190)
    cands = [x0 for x0 in range((gx + 1) % bw, SCREEN_W - bw + 1, bw) if x0 + bw <= 92 or x0 >= 220]
    x0 = cands[-1] if cands else (gx + 1) % bw
    # verificacao numerica: MAD do bloco vs vizinhos deslocados, em y limpo (>=195)
    def mad(xa, xb):
        return float(np.abs(g[195:212, xa:xa + bw] - g[195:212, xb:xb + bw]).mean())
    v = {}
    for k in (1, 2):
        xb = x0 - k * bw
        if xb >= 0:
            v[f"shift_{k}_blocks"] = mad(x0, xb)
        xb = x0 + k * bw
        if xb + bw <= SCREEN_W:
            v[f"shift_{k}_blocks_right"] = mad(x0, xb)
    return dict(block_w=bw, block_h=bh, x=x0, y=gy, texture_period_x=tex_px,
                grout_cols=gcols, grout_rows=grows, verify_mad=v)


# --------------------------------------------------------------------------- fonts
def build_font(tiles: np.ndarray, palette: np.ndarray, known: np.ndarray, pal_line: int,
               codes: list[tuple[str, int]], out_png: Path, out_json: Path,
               pad: int = 1, cell: tuple[int, int] = (8, 8), extra: dict | None = None):
    """codes: [(char, tile_index)] glifos 8x8. Atlas 16 colunas, gutter `pad` px."""
    cols = 16
    cw, ch = cell
    sw, sh = cw + 2 * pad, ch + 2 * pad
    rows = (len(codes) + cols - 1) // cols
    atlas = np.zeros((rows * sh, cols * sw, 4), np.uint8)
    glyphs = {}
    ink = {}
    for n, (c, t) in enumerate(codes):
        r, k = divmod(n, cols)
        idx = tiles[t]
        # indices sem voto: cai para a cor mais clara conhecida da linha (evita buraco)
        rgba = to_rgba(idx, pal_line, palette)
        ox, oy = k * sw + pad, r * sh + pad
        atlas[oy:oy + ch, ox:ox + cw] = rgba
        nz = np.nonzero(idx.any(0))[0]
        glyphs[c] = {"x": ox, "y": oy, "w": cw, "h": ch,
                     "ink_x0": int(nz.min()) if len(nz) else 0, "ink_x1": int(nz.max()) + 1 if len(nz) else 0}
        ink[c] = len(nz)
    save_png(atlas, out_png)
    doc = {"image": out_png.name, "atlas_size": [int(atlas.shape[1]), int(atlas.shape[0])],
           "cell": [cw, ch], "gutter": pad, "advance": cw, "line_height": ch,
           "filter": "nearest", "transparent_index0": True, "glyphs": glyphs}
    if extra:
        doc.update(extra)
    out_json.write_text(json.dumps(doc, indent=1, ensure_ascii=False), encoding="utf-8")
    return doc


def render_text(doc: dict, atlas: np.ndarray, text: str, scale: int = 3, bg=(40, 90, 110, 255)):
    adv, ch = doc["advance"], doc["line_height"]
    out = np.zeros((ch, adv * len(text), 4), np.uint8)
    out[:] = bg
    missing = []
    for i, c in enumerate(text):
        if c == " ":
            continue
        g = doc["glyphs"].get(c)
        if g is None:
            missing.append(c)
            continue
        src = atlas[g["y"]:g["y"] + g["h"], g["x"]:g["x"] + g["w"]]
        dst = out[:, i * adv:i * adv + g["w"]]
        a = src[..., 3:4].astype(np.float32) / 255
        dst[:] = (src * a + dst * (1 - a)).astype(np.uint8)
    big = np.repeat(np.repeat(out, scale, 0), scale, 1)
    return big, missing


# --------------------------------------------------------------------------- main
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    ap.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--model", type=Path, default=DEFAULT_MODEL, help="pesos B+ v3 HQ")
    ap.add_argument("--no-bplus", action="store_true", help="pula o B+; usa nearest 4x")
    ap.add_argument("--force", action="store_true", help="reextrai mesmo se o manifest estiver atual")
    ap.add_argument("--timeout", type=float, default=90.0, help="timeout por fase (s)")
    ap.add_argument("--keep-temp", action="store_true")
    a = ap.parse_args()
    out = (a.out or default_out()).resolve()
    t_start = time.time()

    rom, exe = a.rom.resolve(), a.exe.resolve()
    if not rom.is_file():
        log(f"ERRO: ROM nao encontrada: {rom}")
        return 1
    if not exe.is_file():
        log(f"ERRO: executavel nao encontrado: {exe}")
        return 1
    rom_sha = sha256_file(rom)

    mf = out / "manifest.json"
    if mf.is_file() and not a.force:
        try:
            old = json.loads(mf.read_text(encoding="utf-8"))
            ok = (old.get("rom", {}).get("sha256") == rom_sha
                  and old.get("extractor_version") == EXTRACTOR_VERSION
                  and all((out / f["file"]).is_file() for f in _iter_files(old))
                  and (a.no_bplus or old.get("bplus", {}).get("used") or not old.get("bplus", {}).get("requested")))
            if ok:
                log(f"manifest atual (ROM sha {rom_sha[:12]}); nada a fazer. Use --force para reextrair.")
                return 0
        except Exception as e:  # noqa: BLE001
            log(f"manifest antigo ilegivel ({e}); reextraindo")

    work = Path(tempfile.mkdtemp(prefix="ec_ui_"))
    try:
        rc = extract(a, out, work, rom, exe, rom_sha, t_start)
    except ExtractError as e:
        log(f"ERRO: {e}")
        rc = 1
    finally:
        if not a.keep_temp:
            shutil.rmtree(work, ignore_errors=True)
    return rc


def _iter_files(manifest: dict):
    for v in manifest.get("files", {}).values():
        if isinstance(v, dict) and "file" in v:
            yield v


def extract(a, out: Path, work: Path, rom: Path, exe: Path, rom_sha: str, t_start: float) -> int:
    shots = work / "shots"
    shots.mkdir()

    # ---- 1. captura nativa -------------------------------------------------
    log("captura nativa (headless)...")
    nscript = work / "native.txt"
    native_script(nscript, shots)
    run_native(exe, rom, nscript, a.timeout)
    for need in ("title.png", "menu.bin", f"menu_{MENU_FRAME}.png"):
        if not (shots / need).is_file():
            raise ExtractError(f"captura nativa incompleta (falta {need}); a ROM e a US esperada?")
    title = np.asarray(Image.open(shots / "title.png").convert("RGB"))
    menu_frames = {MENU_FRAME + i: np.asarray(Image.open(shots / f"menu_{MENU_FRAME + i}.png").convert("RGB"))
                   for i in range(MENU_SCAN) if (shots / f"menu_{MENU_FRAME + i}.png").is_file()}
    if title.shape[:2] != (SCREEN_H, SCREEN_W):
        raise ExtractError(f"screenshot com tamanho inesperado {title.shape}")
    vram = np.fromfile(shots / "menu.bin", dtype=np.uint8)
    if vram.size != 0x10000:
        raise ExtractError("DUMP_VRAM com tamanho inesperado")
    menu = menu_frames[MENU_FRAME]
    log(f"  nativo ok ({time.time() - t_start:.1f}s)")

    # ---- modelo VDP / paleta ------------------------------------------------
    tiles = decode_tiles(vram)
    a_idx, a_pal, nt = render_plane_a(vram, tiles)
    sprites = parse_sprites(vram)
    vis = visible_sprites(sprites)
    palette, purity, known = vote_palette(menu, a_idx, a_pal, vis, tiles)
    # linha usada no texto branco e nos sprites douradas
    log(f"  paleta recuperada por voto: {int(known.sum())} entradas (pureza media {purity[known].mean():.2f})")

    # ---- yin-yang: estados distintos pelos screenshots consecutivos ------------
    yy_sprites = [s for s in vis if s["w"] == 2 and s["h"] == 2 and s["pal"] == 1 and s["y"] >= 96
                  and s["tile"] == 0x3F2]
    if not yy_sprites:
        yy_sprites = [s for s in vis if s["w"] == 2 and s["h"] == 2 and s["pal"] == 1 and s["y"] >= 96]
    if not yy_sprites:
        raise ExtractError("sprite do yin-yang nao encontrado (VRAM do menu inesperada)")
    yy_r = max(yy_sprites, key=lambda s: s["x"] if not s["hf"] else -1)   # direito, sem flip
    yy_mask = sprite_pixels(tiles, yy_r) != 0
    states, order = [], []                 # (first_frame, crop) ; sequencia por frame
    runs = []                               # (state_idx, first_frame, length)
    for f in sorted(menu_frames):
        c = menu_frames[f][yy_r["y"]:yy_r["y"] + 16, yy_r["x"]:yy_r["x"] + 16]
        key = c[yy_mask].tobytes()
        k = next((i for i, (_, _, kk) in enumerate(states) if kk == key), None)
        if k is None:
            states.append((f, c.copy(), key))
            k = len(states) - 1
        if runs and runs[-1][0] == k:
            runs[-1][2] += 1
        else:
            runs.append([k, f, 1])
    log(f"  yin-yang: {len(states)} estado(s) distinto(s); runs={[(r[0], r[2]) for r in runs]}")
    # frame representativo (meio da 1a run completa de cada estado) p/ B+
    rep_frame = {}
    for k in range(len(states)):
        r = next((r for r in runs if r[0] == k), None)
        rep_frame[k] = r[1] + min(r[2] - 1, 2) if r else states[k][0]
    rep_frame[0] = MENU_FRAME            # estado 0 = frame base do menu
    # estado 0 pode nao ser o do MENU_FRAME se menu_frames[MENU_FRAME] === states[0]; e (primeiro frame)

    # ---- 2. B+ v3 HQ (4x) ---------------------------------------------------
    bp_img: dict[str, np.ndarray | None] = {}
    bplus_info = {"requested": not a.no_bplus, "used": False, "model": None,
                  "dedither": "both", "gl_ok": False}
    if not a.no_bplus:
        if not a.model.is_file():
            log(f"  aviso: modelo B+ ausente ({a.model}); usando nearest 4x")
        else:
            log("B+ v3 HQ 4x (GL, janela real, em paralelo)...")
            jobs = {"title": TITLE_FRAME, "menu": MENU_FRAME}
            for k in range(1, len(states)):
                jobs[f"yy{k}"] = rep_frame[k]
            res = run_bplus_jobs(exe, rom, a.model.resolve(), jobs, work, a.timeout)
            for name, p in res.items():
                if p is None:
                    log(f"  aviso: B+ '{name}' sem captura (GL indisponivel?); nearest para este item")
                    bp_img[name] = None
                else:
                    im = np.asarray(Image.open(p).convert("RGB"))
                    if im.shape[:2] != (SCREEN_H * SCALE, SCREEN_W * SCALE):
                        log(f"  aviso: B+ '{name}' com tamanho {im.shape[:2]}; ignorado")
                        bp_img[name] = None
                    else:
                        bp_img[name] = im
            bplus_info.update(used=any(v is not None for v in bp_img.values()),
                              gl_ok=any(v is not None for v in bp_img.values()),
                              model=a.model.name)
            log(f"  B+ ok ({time.time() - t_start:.1f}s)")

    def hq(name: str, native_rgb: np.ndarray):
        im = bp_img.get(name)
        if im is not None:
            return im, "b+ v3 hq (dedither both)"
        return nearest4(native_rgb), "nearest"

    title4, title_src = hq("title", title)
    menu4, menu_src = hq("menu", menu)

    # ---- 3. recortes ----------------------------------------------------------
    out.mkdir(parents=True, exist_ok=True)
    stage = out.parent / (out.name + ".tmp_new")
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    files: dict[str, dict] = {}

    def put(key: str, native_rgba: np.ndarray, x4_rgba: np.ndarray, src4: str, **extra):
        n = save_png(native_rgba, stage / f"{key}.png")
        h = save_png(x4_rgba, stage / f"{key}_4x.png")
        files[key] = {"file": f"{key}.png", "size": n, "source": "native",
                      "file_4x": f"{key}_4x.png", "size_4x": h, "source_4x": src4, **extra}

    def masked4(box, mask_native: np.ndarray, img4: np.ndarray, erode: int = 1):
        """Recorta img4 em `box` (coords nativas) aplicando mascara nativa (nearest 4x, erodida)."""
        x0, y0, x1, y1 = box
        rgb = img4[y0 * SCALE:y1 * SCALE, x0 * SCALE:x1 * SCALE]
        m = nearest4(mask_native) if erode >= 0 else None
        if erode > 0:
            m = erode_alpha(m, erode)
        return np.dstack([rgb, (m * 255).astype(np.uint8)])

    # title_splash
    put("title_splash", np.dstack([title, np.full(title.shape[:2], 255, np.uint8)]),
        np.dstack([title4, np.full(title4.shape[:2], 255, np.uint8)]), title_src)
    put("menu_screen", np.dstack([menu, np.full(menu.shape[:2], 255, np.uint8)]),
        np.dstack([menu4, np.full(menu4.shape[:2], 255, np.uint8)]), menu_src)

    # logo = plano A linhas 2..7 + sprite do TM
    logo_idx = np.zeros_like(a_idx)
    logo_pal = np.zeros_like(a_pal)
    logo_idx[16:64] = a_idx[16:64]
    logo_pal[16:64] = a_pal[16:64]
    for s in vis:
        if s["y"] < 40 and s["pal"] == 1 and s["w"] <= 2 and s["x"] > 250:
            blit_sprite(logo_idx, logo_pal, s, sprite_pixels(tiles, s))
    logo_rgba_full = to_rgba(logo_idx, logo_pal, palette)
    lb = bbox_alpha(logo_rgba_full)
    logo_mask = logo_rgba_full[..., 3] > 0
    put("logo", crop(logo_rgba_full, lb), masked4(lb, crop(logo_mask, lb), menu4), menu_src, origin=[lb[0], lb[1]])

    # emblem: coluna central do logo sem letras (acha o circulo pelas colunas amarelas/anel)
    emb = detect_emblem(logo_rgba_full, logo_mask)
    em_mask = crop(logo_mask, emb)
    put("emblem", crop(logo_rgba_full, emb), masked4(emb, em_mask, menu4), menu_src, origin=[emb[0], emb[1]])

    # header MAIN MENU (sprites pal0 em y~80)
    hdr_sprites = sorted([s for s in vis if s["pal"] == 0 and 70 <= s["y"] < 100 and s["h"] == 2],
                         key=lambda s: s["x"])
    h_idx = np.zeros_like(a_idx)
    h_pal = np.zeros_like(a_pal)
    for s in hdr_sprites:
        blit_sprite(h_idx, h_pal, s, sprite_pixels(tiles, s))
    h_rgba = to_rgba(h_idx, h_pal, palette)
    hb = bbox_alpha(h_rgba)
    if hb is None:
        raise ExtractError("sprites do cabecalho 'MAIN MENU' nao encontrados")
    put("header_main_menu", crop(h_rgba, hb), masked4(hb, crop(h_rgba[..., 3] > 0, hb), menu4), menu_src,
        origin=[hb[0], hb[1]])

    # stone tile
    st = detect_stone_tile(menu)
    px, qy, sx, sy = st["block_w"], st["block_h"], st["x"], st["y"]
    stone = menu[sy:sy + qy, sx:sx + px]
    stone4 = menu4[sy * SCALE:(sy + qy) * SCALE, sx * SCALE:(sx + px) * SCALE]
    opaque = lambda im: np.dstack([im, np.full(im.shape[:2], 255, np.uint8)])  # noqa: E731
    put("stone_tile", opaque(stone), opaque(stone4), menu_src, period=[px, qy], origin=[sx, sy],
        grout="1 px escuro na ultima coluna (direita) e na primeira linha (topo) do bloco",
        texture_period_x=st["texture_period_x"], detect=st)

    # variante alternativa do bloco (a textura alterna A/B a cada bloco; periodo real = 2 blocos)
    ax = next((x for x in range((sx % px), SCREEN_W - px + 1, px)
               if x != sx and (x + px <= 92 or x >= 220)), -1)
    if ax >= 0:
        alt = menu[sy:sy + qy, ax:ax + px]
        alt4 = menu4[sy * SCALE:(sy + qy) * SCALE, ax * SCALE:(ax + px) * SCALE]
        put("stone_tile_alt", opaque(alt), opaque(alt4), menu_src, period=[px, qy], origin=[ax, sy],
            note="variante B da textura; alternar com stone_tile em colunas pares/impares")

    # yin-yang
    yy_files = []
    for k, (f0, crop16, _) in enumerate(states):
        # nativo: pixels do sprite (indice != 0) na cor do screenshot desse estado
        nat = np.dstack([crop16, (yy_mask * 255).astype(np.uint8)])
        im4 = bp_img.get(f"yy{k}" if k else "menu")
        if im4 is not None:
            frame_img = im4
            # recorta o sprite no frame 4x; frame base ja corresponde a states[0]
            rgb = frame_img[yy_r["y"] * SCALE:(yy_r["y"] + 16) * SCALE, yy_r["x"] * SCALE:(yy_r["x"] + 16) * SCALE]
            h4 = np.dstack([rgb, (erode_alpha(nearest4(yy_mask), 1) * 255).astype(np.uint8)])
            src4 = "b+ v3 hq (dedither both)"
        else:
            h4 = nearest4(nat)
            src4 = "nearest"
        key = f"yinyang_{k:02d}"
        n = save_png(nat, stage / f"{key}.png")
        s4 = save_png(h4, stage / f"{key}_4x.png")
        files[key] = {"file": f"{key}.png", "size": n, "source": "native", "file_4x": f"{key}_4x.png",
                      "size_4x": s4, "source_4x": src4}
        yy_files.append(key)
    cycle = [[r[0], r[2]] for r in runs]
    files["_yinyang_info"] = {"frames": yy_files, "sprite_size": [16, 16],
                              "sequence_observed": cycle, "note":
                              "estados alternam por ciclagem de paleta; 'sequence_observed' = [indice, frames]"}

    # ---- fontes ----------------------------------------------------------------
    # fonte do menu: ASCII 0x21..0x7E contiguo em tile 0x600+(c-0x1B); linha de paleta 1 (branco)
    codes = [(chr(c), ASCII_TILE_BASE + c) for c in range(0x21, 0x7F) if tiles[ASCII_TILE_BASE + c].any()]
    white_line = 1
    used_idx = sorted({int(i) for c, t in codes for i in np.unique(tiles[t]) if i})
    unvoted = [i for i in used_idx if not known[white_line, i]]
    if unvoted:
        log(f"  aviso: indices da fonte sem voto na paleta {white_line}: {unvoted} (ficam brancos)")
        for i in unvoted:
            palette[white_line, i] = (255, 255, 255)
    fm = build_font(tiles, palette, known, white_line, codes, stage / "font_menu.png", stage / "font_menu.json",
                    extra={"palette_line": white_line, "tile_base": f"0x{0x600:03X}", "charset": "ASCII 0x21-0x7E",
                           "space": {"advance": 8},
                           "indices_used": used_idx,
                           "missing_ascii": [chr(c) for c in range(0x21, 0x7F)
                                             if chr(c) not in {g for g, _ in codes}],
                           "missing_non_ascii": MISSING_PT,
                           "outline": "contorno escuro embutido nos glifos; cor branca"})
    files["font_menu"] = {"file": "font_menu.png", "json": "font_menu.json",
                          "size": fm["atlas_size"], "source": "native"}

    # fonte dourada: letras dos sprites do cabecalho (ordem = "MAIN MENU")
    gold = gold_font(hdr_sprites, tiles, palette, stage, "MAIN MENU")
    if gold:
        files["font_gold"] = {"file": "font_gold.png", "json": "font_gold.json", "size": gold, "source": "native"}

    # ---- previews --------------------------------------------------------------
    t3 = np.tile(stone4, (3, 3, 1))
    save_png(t3, stage / "preview_tile_3x3.png")
    pdoc = json.loads((stage / "font_menu.json").read_text(encoding="utf-8"))
    atlas = np.asarray(Image.open(stage / "font_menu.png").convert("RGBA"))
    prev, miss = render_text(pdoc, atlas, "ONE PLAYER  Two Players  JOGAR  ÁUDIO", 3)
    save_png(prev, stage / "preview_font.png")
    log(f"  preview_font: glifos ausentes na frase de teste: {ascii(sorted(set(miss)))}")

    # ---- manifest ---------------------------------------------------------------
    manifest = {
        "schema": 1, "extractor_version": EXTRACTOR_VERSION,
        "created_utc": _dt.datetime.now(_dt.timezone.utc).isoformat(timespec="seconds"),
        "rom": {"sha256": rom_sha, "size": rom.stat().st_size, "name": rom.name},
        "native_resolution": [SCREEN_W, SCREEN_H], "scale_4x": SCALE,
        "capture": {"title_frame": TITLE_FRAME, "menu_frame": MENU_FRAME,
                    "palette": "recuperada por votacao do screenshot (CRAM nao exposta pelo runner)"},
        "bplus": {**bplus_info, "elapsed_s": round(time.time() - t_start, 1)},
        "sources": {"title_4x": title_src, "menu_4x": menu_src},
        "files": files,
        "previews": ["preview_tile_3x3.png", "preview_font.png"],
    }
    (stage / "manifest.json").write_text(json.dumps(manifest, indent=1, ensure_ascii=False, default=_jd),
                                         encoding="utf-8")

    # troca atomica-ish: remove saida antiga (so nossos arquivos) e move os novos
    for p in out.glob("*"):
        if p.is_file():
            p.unlink()
    for p in stage.glob("*"):
        shutil.move(str(p), str(out / p.name))
    shutil.rmtree(stage, ignore_errors=True)
    log(f"concluido em {time.time() - t_start:.1f}s -> {out}")
    return 0


def _jd(o):
    if isinstance(o, (np.integer,)):
        return int(o)
    if isinstance(o, (np.floating,)):
        return float(o)
    if isinstance(o, np.ndarray):
        return o.tolist()
    raise TypeError(type(o))


def detect_emblem(logo_rgba: np.ndarray, mask: np.ndarray):
    """Circulo 武士道: acha o intervalo de colunas centrais de maior 'altura' do anel."""
    h, w = mask.shape
    colh = mask[16:64].sum(0)
    # centro do logo = onde esta o anel; busca janela de largura ~52 px com mais pixels amarelos
    rgb = logo_rgba[..., :3].astype(int)
    yellow = (rgb[..., 0] > 200) & (rgb[..., 1] > 240) & (rgb[..., 2] < 120) & mask
    cols = yellow.sum(0)
    xs = np.nonzero(cols > 0)[0]
    if len(xs) == 0:
        return (110, 10, 170, 64)
    # maior cluster contiguo (permitindo lacunas pequenas) de colunas amarelas
    groups, start, prev = [], xs[0], xs[0]
    for x in xs[1:]:
        if x - prev > 6:
            groups.append((start, prev))
            start = x
        prev = x
    groups.append((start, prev))
    g = max(groups, key=lambda t: cols[t[0]:t[1] + 1].sum())
    x0, x1 = max(0, g[0] - 1), min(w, g[1] + 2)
    sub = mask[:, x0:x1]
    ys = np.nonzero(sub.any(1))[0]
    return (int(x0), int(ys.min()), int(x1), int(ys.max()) + 1)


def gold_font(hdr_sprites: list[dict], tiles: np.ndarray, palette: np.ndarray, stage: Path, text: str):
    if len(hdr_sprites) != len([c for c in text if c != " "]):
        log(f"  aviso: {len(hdr_sprites)} sprites no cabecalho != {len(text.replace(' ', ''))} letras; font_gold ignorada")
        return None
    letters = {}
    for ch, s in zip([c for c in text if c != " "], hdr_sprites):
        if ch in letters:
            continue
        letters[ch] = sprite_pixels(tiles, s), s["pal"]
    pad = 1
    maxw = max(i.shape[1] for i, _ in letters.values())
    maxh = max(i.shape[0] for i, _ in letters.values())
    sw, sh = maxw + 2 * pad, maxh + 2 * pad
    atlas = np.zeros((sh, sw * len(letters), 4), np.uint8)
    glyphs = {}
    for n, (ch, (img, pal)) in enumerate(sorted(letters.items())):
        ox, oy = n * sw + pad, pad
        atlas[oy:oy + img.shape[0], ox:ox + img.shape[1]] = to_rgba(img, pal, palette)
        glyphs[ch] = {"x": ox, "y": oy, "w": int(img.shape[1]), "h": int(img.shape[0])}
    size = save_png(atlas, stage / "font_gold.png")
    allowed = set(glyphs)
    doc = {"image": "font_gold.png", "atlas_size": size, "gutter": pad, "line_height": maxh, "filter": "nearest",
           "glyphs": glyphs, "partial": True,
           "source_text": text,
           "note": "so as letras de 'MAIN MENU'; use o fallback (fonte OFL) para as demais",
           "missing_ascii_letters": [c for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ" if c not in allowed]}
    (stage / "font_gold.json").write_text(json.dumps(doc, indent=1), encoding="utf-8")
    return size


if __name__ == "__main__":
    sys.exit(main())
