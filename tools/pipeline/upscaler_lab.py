#!/usr/bin/env python
"""Laboratorio Visual de Upscalers Teachers (task.md G1.1-G1.3).

Comando unico (reproduz pre-dither + G1.2 + G1.3, a partir dos frames crus em input/):

    F:\\ComfyUI\\ComfyUI\\.venv\\Scripts\\python.exe tools\\pipeline\\upscaler_lab.py

Opcoes:  --scenes s2_sanctuary,s5_trident   --models 4x-AnimeSharp,...   --force
Captura do frame cru de Trident (Python do Windows, sem GPU; opcional, G1.1):

    python tools\\pipeline\\upscaler_lab.py --capture-trident

Entradas : assets/video_compare/upscalers_teacher/input/<cena>_raw.png (398x224, 16:9, sem dither tratado)
Pre-dither: espelho PyTorch exato (neural/train_espcn.py::pre_dedither_torch, modo `both`) do shader
            s_dedither_cs de neural/infer/ec_bplus_gl.c. Validado contra captura real do GL
            (assets/video_compare/dedither/<cena>/dedither_both_i0.0.png).
Saidas   : input/<cena>_predither.png, out/<cena>/<modelo>.png (1592x896), board_<cena>_full.png,
            board_<cena>_crops.png, report.json (tudo sob assets/video_compare/upscalers_teacher/).
Nao altera codigo do jogo.
"""
from __future__ import annotations

import argparse
import gc
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "assets" / "video_compare" / "upscalers_teacher"
INPUT = LAB / "input"
OUT = LAB / "out"
MODELS_DIR = Path(r"F:\ComfyUI\ComfyUI\models\upscale_models")
ROM_SHA = "6d8bdaf8c994d9f718c8f991900c58cdf1af84e030524b57f6dda263cc4ea247"

SCENES = {
    "s2_sanctuary": "Sanctuary (S2 / estado 18)",
    "s5_trident": "Trident / Atlantida (S5 / estado 24)",
    "s6_vietnam": "Vietnam / Midknight (S6 / estado 19)",
    "s7_salem": "Salem (S7)",
}
# validacao do pre-dither contra o GL real (bilinear 4x i0.0 do dither `both`)
GL_REF = {
    "s2_sanctuary": ROOT / "assets/video_compare/dedither/s2_sanctuary/dedither_both_i0.0.png",
    "s6_vietnam": ROOT / "assets/video_compare/dedither/s6_vietnam/dedither_both_i0.0.png",
    "s7_salem": ROOT / "assets/video_compare/dedither/s7_salem/dedither_both_i0.0.png",
}

REQUIRED = [
    "4x-AnimeSharp.pth",
    "4x_IllustrationJaNai_V1_DAT2_190k.pth",
    "4x_foolhardy_Remacri.pth",
    "4x_NMKD-Siax_200k.pth",
    "4x-UltraSharpV2.safetensors",
    "4xNomos8kSCHAT-L.pth",
]
BONUS = [
    "4x-UltraSharp.pth",
    "4xRealWebPhoto_v4_dat2.safetensors",
    "4x_IllustrationJaNai_V1_ESRGAN_135k.pth",
]
BPLUS = {
    "bplus_v1": ROOT / "assets/models/ec_bplus_espcn4x.pt",
    "bplus_v2": ROOT / "assets/models/ec_bplus_v2_flux.pt",
}

# Recortes 1:1 (x, y, w, h) em coordenadas NATIVAS 398x224; no 4x viram 4x (256x192 px, sem reescala).
CROP_W, CROP_H = 64, 48
REGIONS = {
    "s2_sanctuary": {
        "lutador": (40, 140, "pernas/hakama do lutador direito"),
        "rosto_roupas": (50, 100, "cabeca + torso do lutador"),
        "ceu_fundo": (80, 50, "sem ceu (interior): dragao/parede"),
        "chao": (150, 178, "piso e pedestal"),
        "hud": (175, 0, "timer + inicio da barra"),
    },
    "s5_trident": {
        "lutador": (240, 130, "corpo do Trident"),
        "rosto_roupas": (250, 90, "cabeca/armadura do Trident"),
        "ceu_fundo": (130, 30, "ceu, cupula e cidade"),
        "chao": (150, 175, "agua / reflexo"),
        "hud": (175, 0, "timer + inicio da barra"),
    },
    "s6_vietnam": {
        "lutador": (125, 130, "corpo do lutador esquerdo"),
        "rosto_roupas": (245, 100, "cabeca/camisa do lutador direito"),
        "ceu_fundo": (220, 30, "sol e nuvens"),
        "chao": (80, 175, "galinha + terra/grama"),
        "hud": (175, 0, "timer + inicio da barra"),
    },
    "s7_salem": {
        "lutador": (150, 150, "corpo/hakama do lutador esquerdo"),
        "rosto_roupas": (160, 98, "cabeca + camisa do lutador"),
        "ceu_fundo": (170, 30, "lua e ceu"),
        "chao": (100, 160, "fogueira (translucidez/fogo)"),
        "hud": (175, 0, "timer + inicio da barra"),
    },
}


# --------------------------------------------------------------------------- G1.1 capture (windows python)
def capture_trident(frame: int = 2700) -> None:
    sys.path.insert(0, str(ROOT / "harness"))
    import ec_harness as H  # somente leitura

    mv = H.parse_ecm(ROOT / "harness/movies/06_practice_slash_vs_trident_s24.ecm", ROM_SHA)
    tmp = ROOT / "build" / "g1_capture"
    tmp.mkdir(parents=True, exist_ok=True)
    png = tmp / f"f{frame}.png"
    H.write_input_script(mv, tmp / "s.txt", {}, {frame: png}, tmp / "r.bin", None, tmp / "final.png")
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
    r = subprocess.run(
        [str(ROOT / "build/EternalChampionsRecomp.exe"), "--benchmark", "20000", "--fresh-evidence",
         "--input-script", str(tmp / "s.txt"), "--widescreen", "16:9", str(ROOT / "roms/eternal_champions_us.gen")],
        env=env, capture_output=True, text=True, cwd=str(ROOT))
    if r.returncode != 0:
        raise SystemExit(r.stderr[-500:])
    INPUT.mkdir(parents=True, exist_ok=True)
    (INPUT / "s5_trident_raw.png").write_bytes(png.read_bytes())
    print("wrote", INPUT / "s5_trident_raw.png")


# --------------------------------------------------------------------------- helpers
def now() -> float:
    import torch
    if torch.cuda.is_available():
        torch.cuda.synchronize()
    return time.perf_counter()


def load_rgb(path: Path):
    import numpy as np
    from PIL import Image
    return np.array(Image.open(path).convert("RGB"), dtype=np.uint8)


def save_rgb(arr, path: Path) -> None:
    from PIL import Image
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(arr).save(path, optimize=False)


def to_tensor(arr, device):
    import torch
    return torch.from_numpy(arr).float().div(255.0).permute(2, 0, 1).unsqueeze(0).to(device)


def to_u8(t):
    import torch
    return (t.clamp(0, 1).squeeze(0).permute(1, 2, 0).float().cpu().numpy() * 255.0 + 0.5).astype("uint8")


def make_predither(device) -> dict:
    """raw -> predither (both), valida contra GL onde houver referencia."""
    import numpy as np
    import torch
    import torch.nn.functional as F
    sys.path.insert(0, str(ROOT / "neural"))
    from train_espcn import pre_dedither_torch

    info = {}
    for scene in SCENES:
        raw = load_rgb(INPUT / f"{scene}_raw.png")
        x = to_tensor(raw, device)
        x4 = torch.cat([x, torch.ones_like(x[:, :1])], dim=1)
        pd = pre_dedither_torch(x4, "both")[:, :3]
        pd_u8 = to_u8(pd)
        save_rgb(pd_u8, INPUT / f"{scene}_predither.png")
        changed = float((np.abs(pd_u8.astype(int) - raw.astype(int)).max(axis=2) > 0).mean() * 100)
        entry = {"raw": str(INPUT / f"{scene}_raw.png"), "predither": str(INPUT / f"{scene}_predither.png"),
                 "changed_pct_vs_raw": round(changed, 2)}
        if scene in GL_REF and GL_REF[scene].exists():
            up = F.interpolate(pd, scale_factor=4, mode="bilinear", align_corners=False)
            mine = to_u8(up).astype(int)
            gl = load_rgb(GL_REF[scene]).astype(int)
            d = np.abs(mine - gl)
            entry["gl_validation"] = {
                "ref": str(GL_REF[scene]), "max_abs_diff_255": int(d.max()), "mean_abs_diff": round(float(d.mean()), 4),
                "pct_px_diff_gt2": round(float((d.max(axis=2) > 2).mean() * 100), 4),
                "method": "bilinear 4x do predither Python vs dedither_both_i0.0.png (GL, intensidade 0)",
            }
        else:
            entry["gl_validation"] = None
        info[scene] = entry
        print("[predither]", scene, entry["changed_pct_vs_raw"], "% px alterados;", entry["gl_validation"] and entry["gl_validation"]["max_abs_diff_255"])
    return info


# --------------------------------------------------------------------------- upscaling
def sr_tiled(model, x, tile=128, overlap=16, scale=4):
    import torch
    _, c, h, w = x.shape
    out = torch.zeros((1, c, h * scale, w * scale), dtype=x.dtype, device=x.device)
    wsum = torch.zeros_like(out[:, :1])
    step = tile - 2 * overlap
    ys = list(range(0, max(h - tile, 0) + 1, step))
    xs = list(range(0, max(w - tile, 0) + 1, step))
    if ys[-1] + tile < h:
        ys.append(h - tile)
    if xs[-1] + tile < w:
        xs.append(w - tile)
    for y0 in ys:
        for x0 in xs:
            patch = x[:, :, y0:y0 + tile, x0:x0 + tile]
            o = model(patch)
            out[:, :, y0 * scale:(y0 + patch.shape[2]) * scale, x0 * scale:(x0 + patch.shape[3]) * scale] += o
            wsum[:, :, y0 * scale:(y0 + patch.shape[2]) * scale, x0 * scale:(x0 + patch.shape[3]) * scale] += 1
    return out / wsum


def run_spandrel(desc, x, use_half: bool):
    """Retorna (saida float32 [0,1], modo) tentando inteiro e depois tiled."""
    import torch
    import torch.nn.functional as F
    req = desc.size_requirements
    mult = max(int(getattr(req, "multiple_of", 1) or 1), 1)
    mn = int(getattr(req, "minimum", 0) or 0)
    _, _, h, w = x.shape
    ph = (-h) % mult
    pw = (-w) % mult
    if h + ph < mn:
        ph = mn - h
    if w + pw < mn:
        pw = mn - w
    xin = F.pad(x, (0, pw, 0, ph), mode="replicate") if (ph or pw) else x
    if use_half:
        xin = xin.half()
    mode = "full"
    try:
        with torch.inference_mode():
            y = desc(xin)
    except torch.OutOfMemoryError:
        torch.cuda.empty_cache()
        mode = "tiled128"
        with torch.inference_mode():
            y = sr_tiled(desc, xin, tile=max(128 // mult * mult, mult * 4))
    y = y[:, :, : h * 4, : w * 4].float()
    return y, mode


def upscale_all(device, scenes, model_files, force) -> dict:
    import numpy as np
    import torch
    import torch.nn.functional as F
    from PIL import Image
    from spandrel import ModelLoader

    sys.path.insert(0, str(ROOT / "neural"))
    from train_espcn import ECStudentESPCN4x

    results: dict = {s: {} for s in scenes}
    pd_t = {s: to_tensor(load_rgb(INPUT / f"{s}_predither.png"), device) for s in scenes}

    def record(scene, name, arr, secs, **extra):
        p = OUT / scene / f"{name}.png"
        save_rgb(arr, p)
        results[scene][name] = {"path": str(p), "seconds": round(secs, 4), "size": [arr.shape[1], arr.shape[0]], **extra}

    # baselines
    for s in scenes:
        raw_pd = load_rgb(INPUT / f"{s}_predither.png")
        im = Image.fromarray(raw_pd)
        t = time.perf_counter()
        record(s, "nearest_4x", np.array(im.resize((im.width * 4, im.height * 4), Image.NEAREST)), time.perf_counter() - t, kind="baseline")
        t = time.perf_counter()
        record(s, "bicubic_4x", np.array(im.resize((im.width * 4, im.height * 4), Image.BICUBIC)), time.perf_counter() - t, kind="baseline")

    # B+ v1 / v2 (intensidade 1.0 = forward puro do estudante; entrada = predither)
    for name, p in BPLUS.items():
        try:
            ck = torch.load(p, map_location="cpu", weights_only=False)
            net = ECStudentESPCN4x(ck["channels"], ck["num_blocks"], ck["scale"])
            sd = ck.get("state_dict") or ck.get("model_state_dict")
            net.load_state_dict(sd)
            net.to(device).eval()
            for s in scenes:
                x4 = torch.cat([pd_t[s], torch.ones_like(pd_t[s][:, :1])], dim=1)
                with torch.inference_mode():
                    net(x4)
                t = now()
                with torch.inference_mode():
                    y = net(x4)[:, :3]
                dt = now() - t
                record(s, name, to_u8(y), dt, kind="bplus", source=str(p), note="torch forward (arquitetura de neural/train_espcn.py), nao o GL")
            del net
            torch.cuda.empty_cache()
        except Exception as e:  # noqa: BLE001
            for s in scenes:
                results[s][name] = {"error": repr(e)}
            print("[FAIL]", name, e)

    # teachers via spandrel
    for mf in model_files:
        mname = Path(mf).stem
        path = MODELS_DIR / mf
        t_load = time.perf_counter()
        desc = None
        try:
            if not path.exists():
                raise FileNotFoundError(path)
            desc = ModelLoader().load_from_file(str(path))
            arch = type(desc.model).__name__
            if desc.scale != 4:
                raise RuntimeError(f"scale {desc.scale} != 4")
            desc.to(device).eval()
            half = bool(desc.supports_half)
            if half:
                desc.half()
            load_s = time.perf_counter() - t_load
            # warmup (cudnn/kernels) fora da medicao
            try:
                run_spandrel(desc, torch.rand(1, 3, 64, 64, device=device), half)
            except Exception:  # noqa: BLE001
                pass
            for s in scenes:
                outp = OUT / s / f"{mname}.png"
                if outp.exists() and not force:
                    print("[skip existing]", s, mname)
                    # mantem o registro de tempo previo, se houver
                    prev = {}
                    rp = LAB / "report.json"
                    if rp.exists():
                        prev = json.loads(rp.read_text(encoding="utf-8")).get("results", {}).get(s, {}).get(mname, {})
                    results[s][mname] = prev or {"path": str(outp), "seconds": None, "note": "reaproveitado"}
                    continue
                torch.cuda.reset_peak_memory_stats()
                mode = "full"
                prec = "fp16" if half else "fp32"
                t = now()
                y, mode = run_spandrel(desc, pd_t[s], half)
                dt = now() - t
                if not torch.isfinite(y).all() and half:
                    desc.float()
                    t = now()
                    y, mode = run_spandrel(desc, pd_t[s], False)
                    dt = now() - t
                    prec = "fp32 (fp16 deu NaN/Inf)"
                    desc.half()
                record(s, mname, to_u8(y), dt, kind="teacher", arch=arch, precision=prec, mode=mode,
                       load_seconds=round(load_s, 3), peak_vram_mb=round(torch.cuda.max_memory_allocated() / 2**20))
                print(f"[ok] {s} {mname} {dt:.3f}s {prec} {mode}")
        except Exception as e:  # noqa: BLE001
            msg = repr(e)[:400]
            print("[FAIL]", mname, msg)
            for s in scenes:
                results[s][mname] = {"error": msg, "kind": "teacher"}
        finally:
            del desc
            gc.collect()
            torch.cuda.empty_cache()
    return results


# --------------------------------------------------------------------------- boards
def font(size):
    from PIL import ImageFont
    for f in ("C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/arial.ttf"):
        if Path(f).exists():
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


def label(draw, xy, text, fnt, fill=(255, 255, 255), bg=(0, 0, 0)):
    l, t, r, b = draw.textbbox(xy, text, font=fnt)
    draw.rectangle((l - 3, t - 2, r + 3, b + 2), fill=bg)
    draw.text(xy, text, font=fnt, fill=fill)


def build_boards(scene, results, order) -> dict:
    from PIL import Image, ImageDraw
    names = [n for n in order if "path" in results.get(scene, {}).get(n, {}) and Path(results[scene][n]["path"]).exists()]
    imgs = {n: Image.open(results[scene][n]["path"]).convert("RGB") for n in names}
    f_l, f_s = font(22), font(18)

    # full board
    cols = 4
    tw, th = 598, 336
    rows = (len(names) + cols - 1) // cols
    top = 40
    board = Image.new("RGB", (cols * tw, top + rows * (th + 30)), (24, 24, 24))
    d = ImageDraw.Draw(board)
    d.text((8, 8), f"{SCENES[scene]} - saidas 4x reduzidas para {tw}x{th} (original 1592x896)", font=f_l, fill=(255, 255, 255))
    for i, n in enumerate(names):
        x, y = (i % cols) * tw, top + (i // cols) * (th + 30)
        board.paste(imgs[n].resize((tw, th), Image.LANCZOS), (x, y + 30))
        sec = results[scene][n].get("seconds")
        d.text((x + 6, y + 4), f"{n}" + (f"  {sec*1000:.0f} ms" if sec is not None else ""), font=f_s, fill=(255, 220, 120))
    full_p = LAB / f"board_{scene}_full.png"
    board.save(full_p)

    # crops 1:1
    regs = REGIONS[scene]
    cw, ch = CROP_W * 4, CROP_H * 4
    lab_w, top = 150, 36
    cb = Image.new("RGB", (lab_w + len(names) * (cw + 4), top + len(regs) * (ch + 4)), (24, 24, 24))
    d = ImageDraw.Draw(cb)
    for j, n in enumerate(names):
        x = lab_w + j * (cw + 4)
        d.text((x + 2, 8), n[:30], font=font(14), fill=(255, 220, 120))
    crop_info = {}
    for i, (rname, (nx, ny, desc)) in enumerate(regs.items()):
        y = top + i * (ch + 4)
        d.text((4, y + 4), rname, font=f_s, fill=(160, 255, 160))
        d.text((4, y + 28), f"nat({nx},{ny})", font=font(14), fill=(200, 200, 200))
        d.text((4, y + 46), f"4x({nx*4},{ny*4})", font=font(14), fill=(200, 200, 200))
        d.text((4, y + 64), f"{cw}x{ch}px 1:1", font=font(14), fill=(200, 200, 200))
        crop_info[rname] = {"native_xywh": [nx, ny, CROP_W, CROP_H], "output_xywh": [nx * 4, ny * 4, cw, ch], "descricao": desc}
        for j, n in enumerate(names):
            x = lab_w + j * (cw + 4)
            cb.paste(imgs[n].crop((nx * 4, ny * 4, nx * 4 + cw, ny * 4 + ch)), (x, y))
    crops_p = LAB / f"board_{scene}_crops.png"
    cb.save(crops_p)
    return {"full": str(full_p), "crops": str(crops_p), "full_mb": round(full_p.stat().st_size / 2**20, 2),
            "crops_mb": round(crops_p.stat().st_size / 2**20, 2), "regions": crop_info, "columns": names}


# --------------------------------------------------------------------------- main
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--capture-trident", action="store_true", help="captura o frame cru de Trident (python do Windows)")
    ap.add_argument("--scenes", default=",".join(SCENES))
    ap.add_argument("--models", default="", help="lista (stems) separada por virgula; vazio = obrigatorios + bonus")
    ap.add_argument("--force", action="store_true", help="refaz saidas ja existentes")
    args = ap.parse_args()
    if args.capture_trident:
        capture_trident()
        return 0

    import torch
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    scenes = [s for s in args.scenes.split(",") if s]
    files = REQUIRED + BONUS
    if args.models:
        want = set(args.models.split(","))
        files = [f for f in files if Path(f).stem in want]

    pd_info = make_predither(device)
    results = upscale_all(device, scenes, files, args.force)

    order = ["nearest_4x", "bicubic_4x", "bplus_v1", "bplus_v2"] + [Path(f).stem for f in files]
    boards = {s: build_boards(s, results, order) for s in scenes}

    report = {
        "gpu": torch.cuda.get_device_name(0) if torch.cuda.is_available() else None,
        "torch": torch.__version__,
        "scenes": {s: {"title": SCENES[s], **pd_info[s]} for s in scenes},
        "models": {"required": REQUIRED, "bonus": BONUS, "baselines": ["nearest_4x", "bicubic_4x"], "bplus": {k: str(v) for k, v in BPLUS.items()}},
        "results": results,
        "boards": boards,
        "failures": {s: {m: r["error"] for m, r in results[s].items() if "error" in r} for s in scenes},
    }
    (LAB / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print("report:", LAB / "report.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
