#!/usr/bin/env python3
"""Referencia (Python) para preview_synth.cpp: renderiza com o mixer_studio.py real
(MixerApp._synthesize_channel_wav -> tsf_midi_synth.dll) e mixa os stems (ganho do canal,
balanco, mute/solo), exportando PCM float32 estereo 48 kHz + o mapeamento usado.

  python ref_preview.py --dumps-dir DIR --out-dir DIR
"""
import argparse, copy, io, json, shutil, sys, tempfile, wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "audio"))
import voice_catalog as vc            # noqa: E402
import mixer_studio as ms             # noqa: E402
from extract_all_midis import BGM_TABLE   # noqa: E402

SONGS = ["01_vietnam_midknight", "musica_luta"]


def export_map(key, cfg):
    return {
        "key": key, "is_voice": bool(cfg.get("voice", False)),
        "mode": cfg["mode"], "bank": int(cfg.get("bank", 0)), "program": int(cfg.get("program", 0)),
        "transpose": int(cfg.get("transpose", 0)), "drum_note": int(cfg.get("drum_note", 0)),
        "volume_db": float(cfg.get("volume_db", 0.0)), "pan": float(cfg.get("pan", 0.0)),
        "mute": bool(cfg.get("mute", False)), "solo": bool(cfg.get("solo", False)),
    }


def render_mix(app, slug, chans, tr_cfg):
    any_solo = any(c.get("solo") and c["mode"] in ("gm", "percussion", "auto") for c in chans.values())
    mix = None
    for ch, cfg in chans.items():
        mode = cfg["mode"]
        synth = mode in ("gm", "percussion", "auto")
        if not synth or cfg.get("mute") or (any_solo and not cfg.get("solo")):
            # ainda renderiza p/ descobrir o total de frames
            pass
        wav_bytes, meta, _ = app._synthesize_channel_wav(slug, ch, cfg, tr_cfg)
        if meta["backend"] == "silent":
            continue
        if not synth or cfg.get("mute") or (any_solo and not cfg.get("solo")):
            continue
        with wave.open(io.BytesIO(wav_bytes), "rb") as wf:
            pcm = np.frombuffer(wf.readframes(wf.getnframes()), dtype="<i2").reshape(-1, 2).astype(np.float64) / 32768.0
        g = 10.0 ** (float(cfg.get("volume_db", 0.0)) / 20.0)
        pan = max(-1.0, min(1.0, float(cfg.get("pan", 0.0))))
        pcm = pcm * np.array([g * (1.0 - max(0.0, pan)), g * (1.0 - max(0.0, -pan))])
        mix = pcm if mix is None else mix + pcm
    return mix


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dumps-dir", required=True)
    ap.add_argument("--out-dir", required=True)
    a = ap.parse_args()
    out = Path(a.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    src = ROOT / "assets" / "audio_remaster"
    with tempfile.TemporaryDirectory() as td:
        base = Path(td) / "ar"
        (base / "profiles").mkdir(parents=True)
        shutil.copy(src / "voices.toml", base / "voices.toml")
        for p in (src / "profiles").glob("*.toml"):
            shutil.copy(p, base / "profiles" / p.name)
        # MIDIs por voz a partir dos dumps (como voice_catalog.main)
        patches = {}
        per_song = {}
        for bgm_id, slug in BGM_TABLE:
            p = Path(a.dumps_dir) / f"bgm_{bgm_id:02d}.appdump.txt"
            per_song[slug] = (bgm_id, p, vc.extract_voice_notes(p, bgm_id, slug, patches))
        entries = vc.build_voice_entries(patches)
        for slug, (bgm_id, p, notes) in per_song.items():
            if slug in SONGS:
                vc.write_voice_midi(p, bgm_id, slug, notes, entries, base / "midi_voices")
        app = ms.MixerApp(base, base / "protos")
        for slug in SONGS:
            prof = ms.load_profile(base / "profiles" / f"{slug}.toml")
            vprof = app._voice_profile(slug, prof)
            tr_cfg = dict(vprof["track"])
            A = {k: dict(v) for k, v in vprof["channels"].items()}
            keys = list(A.keys())
            vkeys = [k for k in keys if k.startswith("v_")]
            B = copy.deepcopy(A)
            B[vkeys[0]].update(mode="gm", program=25, transpose=5, volume_db=-3.0, bank=0)
            B[vkeys[1]].update(mode="percussion", drum_note=38, bank=128, program=0)
            B[vkeys[2]].update(mode="chip")
            if len(vkeys) > 3:
                B[vkeys[3]].update(mode="gm", program=80, transpose=-12, volume_db=2.0, bank=0)
            if "dac" in B:
                B["dac"]["mute"] = True
            C = copy.deepcopy(B)
            C[vkeys[0]]["solo"] = True
            psgs = [k for k in C if k.startswith("psg") and k != "psg_noise"]
            if psgs:
                C[psgs[0]]["solo"] = True
            for name, chans in (("A", A), ("B", B), ("C", C)):
                mix = render_mix(app, slug, chans, tr_cfg)
                base_name = f"preview_{slug}_{name}"
                mix.astype("<f4").tofile(out / f"{base_name}.f32")
                maps = [export_map(k[2:] if k.startswith("v_") else k, dict(v, voice=k.startswith("v_")))
                        for k, v in chans.items()]
                (out / f"{base_name}.json").write_text(json.dumps(
                    {"slug": slug, "case": name, "frames": int(mix.shape[0]), "maps": maps}), encoding="utf-8")
                rms = float(np.sqrt(np.mean(mix ** 2)))
                print(f"{base_name}: frames={mix.shape[0]} rms={20*np.log10(max(rms,1e-12)):.2f} dBFS")
        app.close()


if __name__ == "__main__":
    main()
