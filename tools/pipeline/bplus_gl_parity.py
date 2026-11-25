"""G2.5: paridade B+ GL Compute (jogo) x PyTorch (.pt) em frames reais de luta.

Roda o executável nativo (janela real, GL 4.3) com um filme .ecm do harness e
captura via EC_BPLUS_CAPTURE{,_IN,_AT} a entrada nativa exata entregue ao GL e a
saída 4x lida de volta (ARGB8888). Reproduz a mesma entrada no PyTorch
(pre-dither espelho `pre_dedither_torch` + modelo FP32), quantiza para 8 bits e
compara. Gate: erro máximo <= 1 LSB (8 bits) — o GL usa fp16 e a saída é
ARGB8888, então flips de arredondamento de 1 nível são esperados e derrubam o
PSNR para ~59-60 dB mesmo com paridade perfeita; o PSNR é reportado à parte.
Frames de entrada vazios (pretos/uniformes) são rejeitados.

Uso (venv com torch+CUDA):
    $env:PYTHONPATH='neural\\.pydeps'
    F:\\ComfyUI\\ComfyUI\\.venv\\Scripts\\python.exe tools\\pipeline\\bplus_gl_parity.py \
        --models ec_bplus_v3_fast,ec_bplus_v3_hq --dedither both,off --at 3000,3390
"""
from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import torch
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "harness"))
sys.path.insert(0, str(ROOT / "neural"))
import ec_harness as H  # noqa: E402
from train_espcn import ECStudentESPCN4x, pre_dedither_torch  # noqa: E402

EXE = ROOT / "build" / "EternalChampionsRecomp.exe"
MODELS = ROOT / "assets" / "models"


def make_script(movie_path: Path, work: Path) -> tuple[Path, int]:
    rom_sha = H.sha256_bytes(H.DEFAULT_ROM.read_bytes())
    movie = H.parse_ecm(movie_path, rom_sha)
    script = work / "parity.input"
    H.write_input_script(movie, script, {}, {}, work / "final.ram", None, work / "final.png")
    return script, int(movie["frames"])


def run_capture(weights: Path, script: Path, dedither: str, at: int, work: Path, ws: str | None) -> tuple[Path, Path, str]:
    tag = f"{weights.stem}_{dedither}_{at}"
    out_png, in_png = work / f"{tag}_gl.png", work / f"{tag}_in.png"
    for p in (out_png, in_png):
        p.unlink(missing_ok=True)
    env = os.environ.copy()
    env.update(EC_BPLUS_CAPTURE=str(out_png), EC_BPLUS_CAPTURE_IN=str(in_png), EC_BPLUS_CAPTURE_AT=str(at))
    cmd = [str(EXE), "--no-launcher", "--input-script", str(script),
           "--bplus-model", str(weights), "--bplus-backend", "gl", "--bplus-dedither", dedither]
    if ws:
        cmd += ["--widescreen", ws]
    cmd.append(str(H.DEFAULT_ROM))
    res = subprocess.run(cmd, cwd=str(ROOT), env=env, capture_output=True, text=True,
                         encoding="utf-8", errors="replace", timeout=900)
    metrics = next((ln for ln in res.stderr.splitlines() if "[BPLUS_GL] frames=" in ln), "")
    if not out_png.exists() or not in_png.exists():
        raise RuntimeError(f"captura ausente ({tag}); exit={res.returncode}\n{res.stderr[-2000:]}")
    return in_png, out_png, metrics


def torch_reference(pt: Path, in_png: Path, dedither: str, dev: torch.device) -> np.ndarray:
    ck = torch.load(pt, map_location="cpu", weights_only=False)
    m = ECStudentESPCN4x(channels=int(ck["channels"]), num_blocks=int(ck["num_blocks"]), scale=4)
    m.load_state_dict(ck["state_dict"])
    m = m.to(dev).eval()
    rgb = np.asarray(Image.open(in_png).convert("RGB"), dtype=np.float32) / 255.0
    x = torch.from_numpy(rgb).permute(2, 0, 1)[None]
    x4 = torch.cat([x, torch.ones_like(x[:, :1])], 1).to(dev)
    if dedither != "off":
        x4 = pre_dedither_torch(x4, dedither)
    with torch.no_grad():
        y = m(x4)[0, :3].clamp(0, 1)
    return (y.permute(1, 2, 0).cpu().numpy() * 255.0 + 0.5).astype(np.uint8)


def metrics(gl: np.ndarray, ref: np.ndarray) -> dict:
    d = gl.astype(np.int16) - ref.astype(np.int16)
    mse = float(np.mean((d.astype(np.float64) / 255.0) ** 2))
    return {
        "psnr_db": round(10 * math.log10(1.0 / mse), 2) if mse > 0 else float("inf"),
        "max_abs_lsb": int(np.abs(d).max()),
        "pct_eq1lsb": round(float(np.mean(np.abs(d).max(axis=2) == 1)) * 100, 3),
        "pct_gt1lsb": round(float(np.mean(np.abs(d).max(axis=2) > 1)) * 100, 4),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--models", default="ec_bplus_v3_fast,ec_bplus_v3_hq,ec_bplus_v3_hq_janai")
    ap.add_argument("--dedither", default="both,off")
    ap.add_argument("--at", default="3000,3390", help="indices de frame B+ a capturar")
    ap.add_argument("--movie", type=Path, default=ROOT / "harness" / "movies" / "06_practice_slash_vs_trident_s24.ecm")
    ap.add_argument("--widescreen", default=None, help="ex.: fit / 16:9 (opcional)")
    ap.add_argument("--out", type=Path, default=ROOT / "assets" / "captures" / "bplus_gl_parity.json")
    ap.add_argument("--max-lsb", type=int, default=1, help="gate: erro maximo em niveis de 8 bits")
    a = ap.parse_args()

    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    work = Path(tempfile.gettempdir()) / "ec_bplus_parity"
    work.mkdir(parents=True, exist_ok=True)
    script, total = make_script(a.movie, work)
    results, ok = [], True
    for name in [s for s in a.models.split(",") if s]:
        pt, wb = MODELS / f"{name}.pt", MODELS / f"{name}_weights.bin"
        for dd in [s for s in a.dedither.split(",") if s]:
            for at in [int(s) for s in a.at.split(",") if s]:
                in_png, gl_png, met = run_capture(wb, script, dd, at, work, a.widescreen)
                inp = np.asarray(Image.open(in_png).convert("RGB"))
                if int(inp.max()) == int(inp.min()):
                    raise RuntimeError(f"frame {at} de entrada vazio/uniforme; escolha outro --at")
                gl = np.asarray(Image.open(gl_png).convert("RGB"))
                ref = torch_reference(pt, in_png, dd, dev)
                if gl.shape != ref.shape:
                    raise RuntimeError(f"shape GL {gl.shape} != ref {ref.shape}")
                r = {"model": name, "dedither": dd, "at": at, "shape": list(gl.shape), **metrics(gl, ref)}
                r["pass"] = r["max_abs_lsb"] <= a.max_lsb
                ok &= r["pass"]
                results.append(r)
                print(json.dumps(r), "|", met.strip())
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps({"movie": a.movie.name, "frames": total, "results": results}, indent=2), encoding="utf-8")
    print(("PARITY OK" if ok else "PARITY FAIL"), "->", a.out)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
