#!/usr/bin/env python
"""G2.1 - coleta de dataset full-frame 398x224 (widescreen 16:9) para o treino do estudante B+.

Reproduzir (venv ComfyUI, GPU p/ pre-dither):
    F:\\ComfyUI\\ComfyUI\\.venv\\Scripts\\python.exe neural\\collect_dataset.py
Opcoes: --movies 3,4  --step 7  --workers 4  --skip-capture  --mad 1.5
Saida: assets/dataset_v3/{raw,predither}/<id>.png + manifest.json (ver contrato em task.md G2).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import random
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
DS = ROOT / "assets" / "dataset_v3"
TMP = ROOT / "build" / "g2_capture"
EXE = ROOT / "build" / "EternalChampionsRecomp.exe"
ROM = ROOT / "roms" / "eternal_champions_us.gen"
MOVIES = ROOT / "harness" / "movies"
MAX_OPS = 4096
TEACHERS = {
    "animesharp": "4x-AnimeSharp.pth",
    "janai_esrgan": "4x_IllustrationJaNai_V1_ESRGAN_135k.pth",
    "janai_dat2": "4x_IllustrationJaNai_V1_DAT2_190k.pth",
    "nomos_schatl": "4xNomos8kSCHAT-L.pth",
}
# cota por movie (apos dedup): menus/titulo menos, boss/overkill/holo mais
QUOTA_DEFAULT = 80
QUOTA = {1: 70, 2: 70, 16: 70, 13: 110, 17: 130, 12: 90}

sys.path.insert(0, str(ROOT / "harness"))
sys.path.insert(0, str(ROOT / "neural"))


def movie_list(sel):
    out = []
    for p in sorted(MOVIES.glob("*.ecm")):
        n = int(p.name[:2])
        if sel is None or n in sel:
            out.append((n, p))
    return out


def capture_movie(n: int, path: Path, step: int, rom_sha: str) -> dict:
    import ec_harness as H
    mv = H.parse_ecm(path, rom_sha)
    total = mv["frames"]
    d = TMP / f"m{n:02d}"
    d.mkdir(parents=True, exist_ok=True)
    while True:
        cands = list(range(60 + (n % step), total, step))
        pngs = {f: d / f"f{f:05d}.png" for f in cands}
        rams = {f: d / f"f{f:05d}.ram" for f in cands}
        sp = d / "script.txt"
        H.write_input_script(mv, sp, rams, pngs, d / "final.ram", None, d / "final.png")
        nops = sum(1 for l in sp.read_text().splitlines() if l.strip() and not l.startswith("#"))
        if nops <= MAX_OPS - 8:
            break
        step += 2
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
    if n == 13:
        env["EC_DIAG_BOSS_SANCTUARY"] = "1"
    t = time.time()
    r = subprocess.run([str(EXE), "--benchmark", str(max(10000, total + 500)), "--fresh-evidence",
                        "--input-script", str(sp), "--widescreen", "16:9", str(ROM)],
                       env=env, capture_output=True, text=True, cwd=str(ROOT))
    ok = [f for f in cands if pngs[f].exists()]
    print(f"[capture] movie {n:02d} rc={r.returncode} ops={nops} step={step} shots={len(ok)}/{len(cands)} {time.time()-t:.0f}s", flush=True)
    return {"n": n, "path": path, "frames": ok, "dir": d}


def read_state(ram: Path):
    try:
        b = ram.read_bytes()
        return (b[0xFD64] << 8) | b[0xFD65]
    except Exception:
        return None


def select_frames(cap: dict, mad_thr: float) -> list:
    n = cap["n"]
    name = cap["path"].stem
    kept, last = [], None
    for f in cap["frames"]:
        a = np.asarray(Image.open(cap["dir"] / f"f{f:05d}.png").convert("RGB"), dtype=np.int16)
        if a.shape[:2] != (224, 398):
            continue
        if a.mean() < 3:  # quase preto (fade)
            continue
        if last is not None and np.abs(a - last).mean() < mad_thr:
            continue
        kept.append(f)
        last = a
    q = QUOTA.get(n, QUOTA_DEFAULT)
    if len(kept) > q:
        idx = np.linspace(0, len(kept) - 1, q).round().astype(int)
        kept = [kept[i] for i in sorted(set(idx.tolist()))]
    m = re.search(r"_s(\d+)", name)
    deduced = int(m.group(1)) if m else None
    out = []
    for f in kept:
        st = read_state(cap["dir"] / f"f{f:05d}.ram")
        out.append({"id": f"m{n:02d}_f{f:05d}", "movie": name, "frame": f,
                    "state": st if st is not None else (deduced if deduced is not None else -1),
                    "w": 398, "h": 224, "_src": str(cap["dir"] / f"f{f:05d}.png")})
    return out


def assign_split(frames: list, seed: int = 1234, frac: float = 0.05) -> None:
    rng = random.Random(seed)
    groups: dict = {}
    for fr in frames:
        groups.setdefault(fr["movie"], []).append(fr)
        fr["split"] = "train"
    for k in sorted(groups):
        g = sorted(groups[k], key=lambda x: x["id"])
        nv = max(1, round(len(g) * frac))
        for fr in rng.sample(g, nv):
            fr["split"] = "val"


def make_predither(frames: list, force: bool) -> None:
    import torch
    from train_espcn import pre_dedither_torch
    dev = "cuda" if torch.cuda.is_available() else "cpu"
    for fr in frames:
        out = DS / "predither" / f"{fr['id']}.png"
        if out.exists() and not force:
            continue
        a = np.asarray(Image.open(DS / "raw" / f"{fr['id']}.png").convert("RGB"), dtype=np.uint8)
        x = torch.from_numpy(a.copy()).float().div(255).permute(2, 0, 1).unsqueeze(0).to(dev)
        x4 = torch.cat([x, torch.ones_like(x[:, :1])], dim=1)
        y = pre_dedither_torch(x4, "both")[:, :3]
        u8 = (y.clamp(0, 1).squeeze(0).permute(1, 2, 0).cpu().numpy() * 255.0 + 0.5).astype(np.uint8)
        Image.fromarray(u8).save(out)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--movies", default=None)
    ap.add_argument("--step", type=int, default=7)
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--mad", type=float, default=1.5)
    ap.add_argument("--skip-capture", action="store_true")
    ap.add_argument("--force-predither", action="store_true")
    a = ap.parse_args()
    sel = {int(x) for x in a.movies.split(",")} if a.movies else None
    rom_sha = hashlib.sha256(ROM.read_bytes()).hexdigest()
    ml = movie_list(sel)
    if a.skip_capture:
        caps = []
        for n, p in ml:
            d = TMP / f"m{n:02d}"
            caps.append({"n": n, "path": p, "dir": d,
                         "frames": sorted(int(x.stem[1:]) for x in d.glob("f*.png") if x.stem != "final")})
    else:
        with ThreadPoolExecutor(a.workers) as ex:
            caps = list(ex.map(lambda t: capture_movie(t[0], t[1], a.step, rom_sha), ml))
    frames: list = []
    for c in caps:
        frames += select_frames(c, a.mad)
    assign_split(frames)
    for sub in ("raw", "predither"):
        (DS / sub).mkdir(parents=True, exist_ok=True)
    for fr in frames:
        Image.open(fr.pop("_src")).convert("RGB").save(DS / "raw" / f"{fr['id']}.png")
    make_predither(frames, a.force_predither)
    prev = {}
    mp = DS / "manifest.json"
    if mp.exists():
        prev = json.loads(mp.read_text(encoding="utf-8")).get("targets_done", {})
    man = {"version": 1, "frames": frames, "teachers": TEACHERS, "targets_done": prev}
    mp.write_text(json.dumps(man, indent=1), encoding="utf-8")
    by_state: dict = {}
    for fr in frames:
        by_state[fr["state"]] = by_state.get(fr["state"], 0) + 1
    print(f"N={len(frames)} val={sum(f['split'] == 'val' for f in frames)}")
    print("por estado:", dict(sorted(by_state.items())))


if __name__ == "__main__":
    main()
