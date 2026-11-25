#!/usr/bin/env python
"""G2.2 - alvos 4x (1592x896) dos 4 teachers sobre assets/dataset_v3/predither.

    F:\\ComfyUI\\ComfyUI\\.venv\\Scripts\\python.exe neural\\gen_targets.py            # todos, na ordem
    ... gen_targets.py --teachers animesharp,janai_esrgan
    ... gen_targets.py --check                                                     # valida contagens/tamanhos/NaN/preto
Retomavel (pula PNGs existentes). 1 modelo por vez; VRAM liberada entre modelos.
Reutiliza run_spandrel/ (fp16 + fallback tiled) de tools/pipeline/upscaler_lab.py.
"""
from __future__ import annotations

import argparse
import gc
import json
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
DS = ROOT / "assets" / "dataset_v3"
ORDER = ["animesharp", "janai_esrgan", "janai_dat2", "nomos_schatl"]
sys.path.insert(0, str(ROOT / "tools" / "pipeline"))


def load_manifest() -> dict:
    return json.loads((DS / "manifest.json").read_text(encoding="utf-8"))


def save_manifest(man: dict) -> None:
    (DS / "manifest.json").write_text(json.dumps(man, indent=1), encoding="utf-8")


def gen(teachers: list) -> None:
    import torch
    from spandrel import ModelLoader
    import upscaler_lab as L

    dev = torch.device("cuda")
    man = load_manifest()
    ids = [f["id"] for f in man["frames"]]
    for t in teachers:
        out_dir = DS / "targets" / t
        out_dir.mkdir(parents=True, exist_ok=True)
        todo = [i for i in ids if not (out_dir / f"{i}.png").exists()]
        t0 = time.time()
        if todo:
            desc = ModelLoader().load_from_file(str(L.MODELS_DIR / man["teachers"][t]))
            assert desc.scale == 4
            desc.to(dev).eval()
            half = bool(desc.supports_half)
            if half:
                desc.half()
            try:
                L.run_spandrel(desc, torch.rand(1, 3, 64, 64, device=dev), half)
            except Exception:  # noqa: BLE001
                pass
            tiled = 0
            for k, i in enumerate(todo):
                a = np.asarray(Image.open(DS / "predither" / f"{i}.png").convert("RGB"), dtype=np.uint8).copy()
                x = torch.from_numpy(a).float().div(255).permute(2, 0, 1).unsqueeze(0).to(dev)
                y, mode = L.run_spandrel(desc, x, half)
                tiled += mode != "full"
                u8 = (y.clamp(0, 1).squeeze(0).permute(1, 2, 0).cpu().numpy() * 255.0 + 0.5).astype(np.uint8)
                tmp = out_dir / f"{i}.tmp.png"
                Image.fromarray(u8).save(tmp, format="PNG")
                tmp.replace(out_dir / f"{i}.png")
                if (k + 1) % 50 == 0 or k + 1 == len(todo):
                    print(f"[{t}] {k + 1}/{len(todo)} {time.time() - t0:.0f}s tiled={tiled}", flush=True)
                    man = load_manifest()
                    man["targets_done"][t] = sum((out_dir / f"{j}.png").exists() for j in ids)
                    save_manifest(man)
            del desc
            gc.collect()
            torch.cuda.empty_cache()
        man = load_manifest()
        man["targets_done"][t] = sum((out_dir / f"{j}.png").exists() for j in ids)
        man.setdefault("targets_seconds", {})
        if todo:
            man["targets_seconds"][t] = round(man["targets_seconds"].get(t, 0) + time.time() - t0, 1)
        save_manifest(man)
        print(f"[{t}] DONE {man['targets_done'][t]}/{len(ids)} sessao={time.time() - t0:.0f}s", flush=True)


def check(teachers: list) -> int:
    man = load_manifest()
    ids = [f["id"] for f in man["frames"]]
    bad = 0
    for sub, size in (("raw", (398, 224)), ("predither", (398, 224))):
        for i in ids:
            p = DS / sub / f"{i}.png"
            if not p.exists() or Image.open(p).size != size:
                bad += 1
                print("BAD", sub, i)
    print(f"raw/predither: {len(ids)} frames, ruins={bad}")
    for t in teachers:
        n = nb = 0
        for i in ids:
            p = DS / "targets" / t / f"{i}.png"
            if not p.exists():
                nb += 1
                continue
            im = Image.open(p)
            if im.size != (1592, 896) or im.mode != "RGB":
                nb += 1
                print("BAD size/mode", t, i, im.size, im.mode)
                continue
            a = np.asarray(im)
            if a.max() < 8 or a.std() < 1.0:
                nb += 1
                print("BAD blank", t, i)
                continue
            n += 1
        print(f"{t}: ok={n}/{len(ids)} ruins={nb}")
        bad += nb
    print("CHECK", "OK" if bad == 0 else f"FALHOU ({bad})")
    return 1 if bad else 0


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--teachers", default=",".join(ORDER))
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    ts = [t for t in a.teachers.split(",") if t]
    if a.check:
        sys.exit(check(ts))
    gen(ts)


if __name__ == "__main__":
    main()
