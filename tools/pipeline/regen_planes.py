"""G3.2: regera os planos HD (Modo C) com Pre-Dither `both` + upscaler teacher (spandrel).

Para cada plano aprovado em pipeline/manifest.json:
  original 512x256 RGBA -> bleed RGB nos transparentes -> pre_dedither_torch("both")
  -> teacher 4x (padrão AnimeSharp, o mesmo do B+ v3) -> alpha 4x NEAREST (máscara exata
  do plano) -> assets/hd/planes_v3/<hash>_4x.png.
Atualiza pipeline/manifest.json in-place (padrão desde a decisão do usuário G3.2: planos
AnimeSharp são o padrão do Modo C). O caminho Flux anterior fica em `hd_file_flux` (os PNGs
em assets/hd/planes/ não são apagados) — use --restore-flux para voltar. Com --out-manifest
diferente, escreve um manifest paralelo para A/B via --hd-manifest.

Uso (venv ComfyUI):
    $env:PYTHONPATH='neural\\.pydeps'
    F:\\ComfyUI\\ComfyUI\\.venv\\Scripts\\python.exe tools\\pipeline\\regen_planes.py [--states 24,22] [--teacher animesharp]
"""
from __future__ import annotations

import argparse
import copy
import json
import sys
import time
from pathlib import Path

import numpy as np
import torch
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ROOT / "neural"))
import upscaler_lab as L  # noqa: E402  (MODELS_DIR, run_spandrel)
from comfy_upscale import bleed_rgb_into_transparent  # noqa: E402
from train_espcn import pre_dedither_torch  # noqa: E402

TEACHERS = {
    "animesharp": "4x-AnimeSharp.pth",
    "janai_dat2": "4x_IllustrationJaNai_V1_DAT2_190k.pth",
}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", type=Path, default=ROOT / "pipeline" / "manifest.json")
    ap.add_argument("--out-manifest", type=Path, default=None, help="padrao: in-place (--manifest)")
    ap.add_argument("--restore-flux", action="store_true", help="so volta hd_file <- hd_file_flux e sai")
    ap.add_argument("--out-dir", type=Path, default=ROOT / "assets" / "hd" / "planes_v3")
    ap.add_argument("--teacher", default="animesharp", choices=sorted(TEACHERS))
    ap.add_argument("--model-file", default=None, help="sobrescreve o arquivo do teacher em MODELS_DIR")
    ap.add_argument("--dedither", default="both", choices=("off", "cbod", "both"))
    ap.add_argument("--states", default="", help="lista de state_id (vazio = todos)")
    a = ap.parse_args()
    out_path = a.out_manifest or a.manifest

    def save(m: dict) -> None:
        out_path.write_text(json.dumps(m, indent=2, ensure_ascii=True) + "\n", encoding="utf-8", newline="\n")

    def is_plane(e: dict) -> bool:
        return (e.get("status") == "approved" and e.get("layer") in ("plane_a", "plane_b")
                and "/planes/" in str(e.get("original_png", "")).replace("\\", "/"))

    if a.restore_flux:
        m = json.loads(a.manifest.read_text(encoding="utf-8"))
        k = 0
        for e in m["entries"].values():
            if is_plane(e) and e.get("hd_file_flux"):
                e["hd_file"] = e["hd_file_flux"]
                e["workflow"], e["workflow_version"], e["model"] = "remaster_flux_usdu", "1.0", "flux1-dev-fp8.safetensors"
                k += 1
        save(m)
        print(f"{k} planos restaurados para Flux -> {out_path}")
        return 0

    from spandrel import ModelLoader

    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    mfile = L.MODELS_DIR / (a.model_file or TEACHERS[a.teacher])
    desc = ModelLoader().load_from_file(str(mfile))
    assert desc.scale == 4
    desc.to(dev).eval()
    half = dev.type == "cuda" and bool(desc.supports_half)
    if half:
        desc.half()

    man = json.loads(a.manifest.read_text(encoding="utf-8"))
    out_man = copy.deepcopy(man)
    want = {int(s) for s in a.states.split(",") if s.strip()}
    a.out_dir.mkdir(parents=True, exist_ok=True)
    n = 0
    t0 = time.time()
    for key, e in man["entries"].items():
        hd = e.get("hd_file") or ""
        if not is_plane(e) or not hd:
            continue
        if want and int(e.get("state_id", -1)) not in want:
            continue
        src = Image.open(ROOT / e["original_png"]).convert("RGBA")
        alpha = np.asarray(src)[:, :, 3]
        rgb = np.asarray(bleed_rgb_into_transparent(src, iterations=8), dtype=np.float32) / 255.0
        x = torch.from_numpy(rgb).permute(2, 0, 1)[None].to(dev)
        x4 = torch.cat([x, torch.ones_like(x[:, :1])], 1)
        if a.dedither != "off":
            x4 = pre_dedither_torch(x4, a.dedither)
        with torch.no_grad():
            y, mode = L.run_spandrel(desc, x4[:, :3].contiguous(), half)
        y = (y[0].float().clamp(0, 1).permute(1, 2, 0).cpu().numpy() * 255 + 0.5).astype(np.uint8)
        a4 = np.repeat(np.repeat(alpha, 4, 0), 4, 1)
        out = np.dstack([y, a4])
        dst = a.out_dir / Path(hd).name
        Image.fromarray(out, "RGBA").save(dst, optimize=True)
        ne = out_man["entries"][key]
        if "/planes/" in hd.replace("\\", "/") and not ne.get("hd_file_flux"):
            ne["hd_file_flux"] = hd
        ne["hd_file"] = dst.relative_to(ROOT).as_posix()
        ne["workflow"] = f"predither_{a.dedither}+{a.teacher}"
        ne["workflow_version"] = "g3.2"
        ne["model"] = mfile.name
        n += 1
        print(f"[{n}] s{e['state_id']} {e['layer']} {e['original_png']} -> {ne['hd_file']} ({mode})", flush=True)
    save(out_man)
    print(f"{n} planos em {time.time() - t0:.1f}s -> {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
