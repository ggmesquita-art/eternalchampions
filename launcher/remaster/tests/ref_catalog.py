#!/usr/bin/env python3
"""Referencia (Python) para o teste de equivalencia do catalog.cpp.

Exporta, por musica: vozes FM (hash, nº de notas, 50 primeiras notas cruas), canais
PSG/ruido/DAC (parse_chip_ring) e as notas apos o round-trip MIDI (quantizacao em ticks,
o que mixer_studio.py realmente sintetiza).

  python ref_catalog.py --dumps-dir DIR --out ref.json
"""
import argparse, json, sys, tempfile
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "audio"))
import voice_catalog as vc                      # noqa: E402
from chip_ring_to_midi import parse_chip_ring    # noqa: E402
from extract_all_midis import BGM_TABLE          # noqa: E402
import mixer_studio as ms                        # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dumps-dir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--n", type=int, default=50)
    a = ap.parse_args()
    dumps = Path(a.dumps_dir)
    patches = {}
    res = {}
    per_song = {}
    for bgm_id, slug in BGM_TABLE:
        p = dumps / f"bgm_{bgm_id:02d}.appdump.txt"
        per_song[slug] = (bgm_id, p, vc.extract_voice_notes(p, bgm_id, slug, patches))
    entries = vc.build_voice_entries(patches)
    with tempfile.TemporaryDirectory() as td:
        for slug, (bgm_id, p, notes) in per_song.items():
            by = defaultdict(list)
            for n in notes:
                by[n.hash].append(n)
            voices = {}
            for h, ns in by.items():
                voices[h] = {"n": len(ns),
                             "first": [[n.start_s, n.dur_s, n.note, n.vel, int(n.perc), n.ch, n.tl] for n in ns[:a.n]]}
            other, _ = parse_chip_ring(p, fm_perc_to_dac=False)
            chans = {}
            for c in ("psg0", "psg1", "psg2", "psg_noise", "dac"):
                evs = other.get(c, [])
                chans[c] = {"n": len(evs),
                            "first": [[e.start_s, e.end_s, e.note, e.velocity] for e in evs[:a.n]]}
            path, _tot = vc.write_voice_midi(p, bgm_id, slug, notes, entries, Path(td))
            q = ms.read_midi_named_tracks(path)
            quant = {}
            for k, evs in q.items():
                if k == "": continue
                quant[k] = {"n": len(evs),
                            "first": [[e.start_s, e.end_s, e.note, e.velocity] for e in evs[:a.n]]}
            res[slug] = {"bgm_id": bgm_id, "voices": voices, "channels": chans, "quant": quant}
    Path(a.out).write_text(json.dumps(res), encoding="utf-8")
    print("ok", len(res), "musicas ->", a.out)


if __name__ == "__main__":
    main()
