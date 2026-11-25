#!/usr/bin/env python3
"""Referencias (Python) para voices_toml.cpp: o que voice_catalog.write_toml e
mixer_studio.format_profile_toml produziriam para as mesmas edicoes.

  python ref_toml.py --dumps-dir DIR --out-dir DIR
"""
import argparse, shutil, sys, tempfile, tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "audio"))
import voice_catalog as vc   # noqa: E402
import mixer_studio as ms    # noqa: E402

# Edicoes (as mesmas estao codificadas em tests/selftest.cpp)
VOICE_EDITS = {
    "0047b5c7": dict(name='Baixo \u00e9 "slap" \U0001F3B8', mode="percussion", program=38, bank=128,
                     transpose=-3, volume_db=-2.25, drum_note=42),
    "01a7cbf3": dict(mode="chip"),
}
PROFILE = "01_vietnam_midknight"
PROFILE_EDITS = {"fm2": dict(program=48, volume_db=-3.5, mute=True), "dac": dict(mode="gm", bank=0)}


def load_voices(path):
    with open(path, "rb") as f:
        raw = tomllib.load(f).get("voices", {})
    out = {}
    for h, v in raw.items():
        edit = {k: v.get(k, d) for k, d in vc.EDITABLE_DEFAULTS.items()}
        out[h] = {"edit": edit, "meta": dict(v.get("meta", {}))}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dumps-dir", required=True)
    ap.add_argument("--out-dir", required=True)
    a = ap.parse_args()
    out = Path(a.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    base = ROOT / "assets" / "audio_remaster"

    ent = load_voices(base / "voices.toml")
    for h, e in VOICE_EDITS.items():
        ent[h]["edit"].update(e)
    vc.write_toml(ent, out / "voices_edit_ref.toml")

    prof = ms.load_profile(base / "profiles" / f"{PROFILE}.toml")
    for ch, e in PROFILE_EDITS.items():
        prof["channels"][ch].update(e)
    (out / "profile_edit_ref.toml").write_text(ms.format_profile_toml(prof), encoding="utf-8", newline="\n")

    # regeneracao do zero (sem preservar edicoes): voice_catalog.main
    fresh = out / "voices_fresh.toml"
    if fresh.exists():
        fresh.unlink()
    vc.main(["--dumps-dir", a.dumps_dir, "--toml", str(fresh), "--no-midi", "--top", "0"])
    print("ok ->", out)


if __name__ == "__main__":
    main()
