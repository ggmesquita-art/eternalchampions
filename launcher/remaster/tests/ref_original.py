#!/usr/bin/env python3
"""Referencia (Python) do "original": MixerApp.generate_original (jogo headless -> RAM, 48 kHz).
  python ref_original.py --bgm 8 --out orig_08.f32
"""
import argparse, io, shutil, sys, tempfile, wave
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "audio"))
import mixer_studio as ms            # noqa: E402
from extract_all_midis import BGM_TABLE  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--bgm", type=int, required=True)
ap.add_argument("--out", required=True)
a = ap.parse_args()
slug = dict(BGM_TABLE)[a.bgm]
src = ROOT / "assets" / "audio_remaster"
with tempfile.TemporaryDirectory() as td:
    base = Path(td) / "ar"
    (base / "profiles").mkdir(parents=True)
    for p in (src / "profiles").glob("*.toml"):
        shutil.copy(p, base / "profiles" / p.name)
    app = ms.MixerApp(base, base / "protos")
    r = app.generate_original(slug)
    wav = app._orig_cache[slug]
    with wave.open(io.BytesIO(wav), "rb") as wf:
        pcm = np.frombuffer(wf.readframes(wf.getnframes()), dtype="<i2").reshape(-1, 2).astype(np.float32) / 32768.0
        print(r, wf.getframerate(), pcm.shape)
    pcm.astype("<f4").tofile(a.out)
    app.close()
