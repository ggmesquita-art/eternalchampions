#!/usr/bin/env python3
"""tools/audio/voice_catalog.py — catálogo global de instrumentos FM (patches YM2612).

Contrato do hash: docs/audio_voice_hash.md (FNV-1a 32 bits sobre 31 bytes do patch).

Fluxo:
  1. lê os dumps GENESIS_AUDIO_APPLIEDDUMP das 17 BGMs (bgm_NN.appdump.txt);
  2. re-executa a transcrição FM do chip_ring_to_midi (mesmas notas, mesma conversão
     fnum/block -> MIDI) mantendo um shadow dos registradores para hashear o patch
     no instante do key-on;
  3. agrega por hash e escreve assets/audio_remaster/voices.toml (preservando as
     edições manuais) e um MIDI por música com uma trilha por voz em
     assets/audio_remaster/midi_voices/<slug>.mid.

Uso:
    python tools/audio/voice_catalog.py --selftest
    python tools/audio/voice_catalog.py [--dumps-dir DIR | --extract] [--no-midi]

Heurística de sugestão de programa GM (apenas PONTO DE PARTIDA, nunca é verdade):
  O hash/metadados vêm do patch; a sugestão usa só: mediana das notas, classe do
  envelope dos carriers (AR/D1R/D2R/SL), algoritmo, feedback e duração. Ordem:
    1. percussão: >50% das notas com a assinatura "ruído FM" do chip_ring_to_midi, ou
       (>=3 notas, mediana <150 ms, mediana de nota <48 e FB>=6)       -> mode=percussion
    2. grave (mediana MIDI <48): ALG>=4 ou FB>=3 -> 38 Synth Bass 1, senão 33 Finger Bass
    3. ALG 7 (aditivo): sustenta -> 16 Drawbar Organ; decai -> 18 Rock Organ
    4. ALG 0..3 com FB>=5: mediana<60 -> 29 Overdriven Gtr, senão 81 Saw Lead
       (FB>=7 e mediana<60 -> 30 Distortion Gtr)
    5. envelope que decai (D2R>=6 ou D1R>=10 com SL>=3): ALG>=4 -> 4 E.Piano 1;
       FB>=3 -> 7 Clavinet; senão 0 Acoustic Grand
    6. sustenta com ataque lento (AR<18): 89 Warm Pad (mediana<60) / 50 Synth Strings 1
    7. sustenta com ataque rápido: mediana<60 -> 62 Synth Brass 1; ALG>=4 -> 80 Square
       Lead; senão 81 Saw Lead
"""

from __future__ import annotations

import argparse
import re
import statistics
import struct
import subprocess
import sys
import tomllib
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from chip_ring_to_midi import (  # noqa: E402
    APP_FM_LINE,
    APP_PSG_LINE,
    FRAME_US,
    M68K_FRAME_CYCLES,
    MASTER_FRAME_CYCLES,
    NoteEvent,
    PERC_MIDI_CH,
    PPQN,
    YM_KEYON_CH_MAP,
    _FmChan,
    _build_track_chunk,
    _meta_set_tempo,
    _meta_track_name,
    _notes_to_track_chunk,
    parse_chip_ring,
    read_midi_notes,
)
from extract_all_midis import BGM_TABLE  # noqa: E402

DEFAULT_DUMPS_DIR = (
    "C:/Users/ADMINI~1/AppData/Local/Temp/claude/3d53f40dad1957f7/"
    "a9072639-33b7-4a0d-8093-635d41a3354c/scratchpad/midi_probe_tmp"
)
DEFAULT_TOML = ROOT / "assets" / "audio_remaster" / "voices.toml"
DEFAULT_MIDI_DIR = ROOT / "assets" / "audio_remaster" / "midi_voices"

# ---------------------------------------------------------------------------
# Hash do contrato (docs/audio_voice_hash.md)
# ---------------------------------------------------------------------------

SLOT_OFF = (0x0, 0x8, 0x4, 0xC)  # S1, S2, S3, S4
# Carriers por ALG, indexados por S1=0..S4=3 (ordem do contrato)
CARRIERS_S = {
    0: (3,), 1: (3,), 2: (3,), 3: (3,),
    4: (1, 3), 5: (1, 2, 3), 6: (1, 2, 3), 7: (0, 1, 2, 3),
}

# Valor esperado do vetor de teste (ALG=7, FB=0, todo o resto zero).
SELFTEST_EXPECTED = "ce6df9ba"  # FNV-1a dos 31 bytes do vetor (docs/audio_voice_hash.md)


def patch_bytes(regs: list[list[int]], pr: int, ch: int) -> bytes:
    """31 bytes canônicos do patch (contrato). regs[pr][reg] = shadow."""
    r = regs[pr]
    b0 = r[0xB0 + ch]
    alg = b0 & 7
    out = [alg, (b0 >> 3) & 7, r[0xB4 + ch] & 0x37]
    carriers = CARRIERS_S[alg]
    for s in range(4):
        o = SLOT_OFF[s] + ch
        for base in (0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90):
            v = r[base + o] & 0xFF
            if base == 0x40 and s in carriers:
                v = 0xFF
            out.append(v)
    return bytes(out)


def fnv1a32(data: bytes) -> int:
    h = 0x811C9DC5
    for b in data:
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def voice_hash(pb: bytes) -> str:
    return "%08x" % fnv1a32(pb)


def selftest() -> int:
    regs = [[0] * 256, [0] * 256]
    regs[0][0xB0] = 7  # ALG=7, FB=0
    pb = patch_bytes(regs, 0, 0)
    expected_bytes = bytes([7, 0, 0]) + bytes([0x00, 0xFF, 0, 0, 0, 0, 0]) * 4
    assert pb == expected_bytes and len(pb) == 31, "sequência canônica divergente"
    got = voice_hash(pb)
    print(f"selftest: bytes={pb.hex()} hash={got} esperado={SELFTEST_EXPECTED}")
    if got != SELFTEST_EXPECTED:
        print("FALHOU")
        return 1
    print("OK")
    return 0


# ---------------------------------------------------------------------------
# Extração de notas FM com identificação de patch
# ---------------------------------------------------------------------------

@dataclass
class VoiceNote:
    hash: str
    bgm_id: int
    slug: str
    ch: int
    note: int
    start_s: float
    dur_s: float
    tl: int  # TL efetivo dos carriers (min)
    vel: int
    perc: bool


@dataclass
class VoiceInfo:
    pb: bytes
    notes: list[VoiceNote] = field(default_factory=list)


def _load_events(path: Path):
    """Eventos (t_us, kind, port, val) na mesma convenção de tempo do parse_chip_ring."""
    ev: list[tuple[int, int, str, int, int]] = []
    max_cyc = 0
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = APP_FM_LINE.search(line)
            if m:
                cyc = int(m.group(4))
                max_cyc = max(max_cyc, cyc)
                ev.append((int(m.group(1)), cyc, "fm", int(m.group(2)), int(m.group(3), 16)))
                continue
            m = APP_PSG_LINE.search(line)
            if m:
                cyc = int(m.group(3))
                max_cyc = max(max_cyc, cyc)
                ev.append((int(m.group(1)), cyc, "psg", 0, int(m.group(2), 16)))
    fc = MASTER_FRAME_CYCLES if max_cyc > 135_000 else M68K_FRAME_CYCLES
    return [
        (fr * FRAME_US + (min(float(cyc), fc) / fc) * FRAME_US, kind, port, val)
        for fr, cyc, kind, port, val in ev
    ]


def extract_voice_notes(
    path: Path, bgm_id: int, slug: str, patches: dict[str, VoiceInfo]
) -> list[VoiceNote]:
    """Espelha a lógica FM de chip_ring_to_midi.parse_chip_ring, anexando o hash do patch."""
    events = _load_events(path)
    if not events:
        return []
    t0 = events[0][0]
    t_last = events[-1][0]
    regs = [[0] * 256, [0] * 256]
    addr = [0, 0]
    fm = [_FmChan() for _ in range(6)]
    dac_enabled = False
    out: list[VoiceNote] = []

    def emit(g, t_on, t_off, note, vel, perc, h, tl):
        if not perc and not (12 <= note <= 120):
            return
        dur_us = max(10_000.0, t_off - t_on)
        vn = VoiceNote(h, bgm_id, slug, g, note, max(0.0, (t_on - t0) / 1e6), dur_us / 1e6, tl, vel, perc)
        out.append(vn)
        patches[h].notes.append(vn)

    def eff_tl(st: _FmChan) -> int:
        # st.tl está em ordem de registrador (OP1,OP3,OP2,OP4) = YM_CARRIER_SLOTS
        from chip_ring_to_midi import YM_CARRIER_SLOTS
        return min(st.tl[i] for i in YM_CARRIER_SLOTS.get(st.alg & 7, (3,)))

    for t, kind, port, val in events:
        if kind != "fm":
            continue
        part = 1 if (port & 0x02) else 0
        if (port & 1) == 0:
            addr[part] = val
            continue
        reg = addr[part]
        regs[part][reg] = val
        if part == 0 and reg == 0x2B:
            dac_enabled = bool(val & 0x80)
        elif part == 0 and reg == 0x28:
            c = val & 7
            g = YM_KEYON_CH_MAP.get(c)
            if g is None or (g == 5 and dac_enabled):
                continue
            st = fm[g]
            if (val >> 4) & 0xF:
                if st.sounding is not None:
                    t_on, n, v, p, h, tl = st.sounding
                    emit(g, t_on, t, n, v, p, h, tl)
                is_perc = st.is_noise_perc_patch()
                note = st.current_midi_note()
                if is_perc or 12 <= note <= 120:
                    pb = patch_bytes(regs, g // 3, g % 3)
                    h = voice_hash(pb)
                    if h not in patches:
                        patches[h] = VoiceInfo(pb)
                    st.sounding = (t, note, st.carrier_velocity(), is_perc, h, eff_tl(st))
                else:
                    st.sounding = None
            elif st.sounding is not None:
                t_on, n, v, p, h, tl = st.sounding
                emit(g, t_on, t, n, v, p, h, tl)
                st.sounding = None
        elif 0xA4 <= reg <= 0xA6:
            fm[part * 3 + reg - 0xA4].msb_latch = val
        elif 0xA0 <= reg <= 0xA2:
            g = part * 3 + reg - 0xA0
            st = fm[g]
            st.fnum = ((st.msb_latch & 7) << 8) | val
            st.block = (st.msb_latch >> 3) & 7
            if st.sounding is not None:
                t_on, old_note, old_vel, old_perc, h, tl = st.sounding
                if not old_perc:
                    new_note = st.current_midi_note()
                    if 12 <= new_note <= 120:
                        if (t - t_on) < 20_000.0:
                            st.sounding = (t_on, new_note, old_vel, False, h, tl)
                        elif abs(new_note - old_note) >= 1:
                            emit(g, t_on, t, old_note, old_vel, False, h, tl)
                            st.sounding = (t, new_note, st.carrier_velocity(), False, h, eff_tl(st))
        elif 0xB0 <= reg <= 0xB2:
            fm[part * 3 + reg - 0xB0].alg = val & 7
        elif 0x30 <= reg <= 0x3E and (reg & 3) != 3:
            fm[part * 3 + (reg & 3)].mul[(reg - 0x30) >> 2] = val & 0x0F
        elif 0x40 <= reg <= 0x4E and (reg & 3) != 3:
            fm[part * 3 + (reg & 3)].tl[(reg - 0x40) >> 2] = val & 0x7F

    t_close = t_last + 250_000.0
    for g, st in enumerate(fm):
        if st.sounding is not None:
            t_on, n, v, p, h, tl = st.sounding
            emit(g, t_on, t_close, n, v, p, h, tl)
            st.sounding = None
    out.sort(key=lambda n: (n.start_s, n.note))
    return out


# ---------------------------------------------------------------------------
# Metadados do patch + heurística
# ---------------------------------------------------------------------------

def decode_patch(pb: bytes) -> dict:
    alg, fb, amsfms = pb[0], pb[1], pb[2]
    carriers = CARRIERS_S[alg]
    ops = []
    for s in range(4):
        dtmul, tl, rsar, amd1r, d2r, slrr, ssg = pb[3 + 7 * s: 10 + 7 * s]
        op = {
            "slot": f"S{s + 1}",
            "carrier": s in carriers,
            "mul": dtmul & 0xF,
            "dt": (dtmul >> 4) & 7,
            "ar": rsar & 0x1F,
            "d1r": amd1r & 0x1F,
            "d2r": d2r & 0x1F,
            "sl": (slrr >> 4) & 0xF,
            "rr": slrr & 0xF,
        }
        if s not in carriers:
            op["tl"] = tl
        ops.append(op)
    return {"alg": alg, "fb": fb, "ams_fms": amsfms, "ops": ops}


def suggest(pb: bytes, notes: list[VoiceNote]) -> tuple[str, int, str]:
    """Retorna (mode, program, rótulo). Ver docstring do módulo."""
    d = decode_patch(pb)
    alg, fb = d["alg"], d["fb"]
    car = [o for o in d["ops"] if o["carrier"]]
    ar = statistics.mean(o["ar"] for o in car)
    d1r = statistics.mean(o["d1r"] for o in car)
    d2r = statistics.mean(o["d2r"] for o in car)
    sl = statistics.mean(o["sl"] for o in car)
    tonal = [n for n in notes if not n.perc] or notes
    med = statistics.median(n.note for n in tonal) if tonal else 60
    med_dur = statistics.median(n.dur_s for n in notes) * 1000.0 if notes else 0.0
    perc_frac = sum(1 for n in notes if n.perc) / len(notes) if notes else 0.0

    if perc_frac > 0.5 or (len(notes) >= 3 and med_dur < 150 and med < 48 and fb >= 6):
        return "percussion", 0, "percussion"
    decays = d2r >= 6 or (d1r >= 10 and sl >= 3)
    if med < 48:
        return "gm", (38 if (alg >= 4 or fb >= 3) else 33), "bass"
    if alg == 7:
        return ("gm", 18, "organ") if decays else ("gm", 16, "organ")
    if alg <= 3 and fb >= 5:
        if fb >= 7 and med < 60:
            return "gm", 30, "dist-guitar"
        return ("gm", 29, "dist-guitar") if med < 60 else ("gm", 81, "lead")
    if decays:
        if alg >= 4:
            return "gm", 4, "plucked"
        return ("gm", 7, "plucked") if fb >= 3 else ("gm", 0, "plucked")
    if ar < 18:
        return ("gm", 89, "pad") if med < 60 else ("gm", 50, "pad")
    if med < 60:
        return "gm", 62, "brass"
    return ("gm", 80, "lead") if alg >= 4 else ("gm", 81, "lead")


# ---------------------------------------------------------------------------
# voices.toml
# ---------------------------------------------------------------------------

EDITABLE_DEFAULTS = {
    "name": "", "mode": "gm", "program": 0, "bank": 0, "transpose": 0, "volume_db": 0.0,
    "drum_note": 0,
}


def suggest_drum_note(notes: "list[VoiceNote]") -> int:
    """Nota GM de percussão sugerida (so p/ mode=percussion): mesma faixa de duração do
    chip_ring_to_midi — <140 ms closed hi-hat (42), <350 ms open hi-hat (46), senão crash (49)."""
    if not notes:
        return 42
    med = statistics.median(n.dur_s for n in notes) * 1000.0
    return 42 if med < 140 else (46 if med < 350 else 49)


def _q(s: str) -> str:
    import json
    return json.dumps(s, ensure_ascii=True)


def _fmt(v) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float):
        return f"{v:.1f}"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, str):
        return _q(v)
    if isinstance(v, (list, tuple)):
        return "[" + ", ".join(_fmt(x) for x in v) + "]"
    if isinstance(v, dict):
        return "{ " + ", ".join(f"{k} = {_fmt(x)}" for k, x in v.items()) + " }"
    raise TypeError(type(v))


def build_voice_entries(patches: dict[str, VoiceInfo]) -> dict[str, dict]:
    entries: dict[str, dict] = {}
    for h, info in patches.items():
        notes = info.notes
        tonal = [n for n in notes if not n.perc] or notes
        d = decode_patch(info.pb)
        mode, prog, label = suggest(info.pb, notes)
        meta = {
            "alg": d["alg"],
            "fb": d["fb"],
            "ams_fms": d["ams_fms"],
            "patch": info.pb.hex(),
            "ops": d["ops"],
            "songs": sorted({n.slug for n in notes}, key=lambda s: [x[1] for x in BGM_TABLE].index(s)),
            "channels": [f"fm{c}" for c in sorted({n.ch for n in notes})],
            "notes": len(notes),
            "note_min": min(n.note for n in tonal),
            "note_max": max(n.note for n in tonal),
            "note_median": int(statistics.median(n.note for n in tonal)),
            "avg_dur_ms": int(round(1000.0 * statistics.mean(n.dur_s for n in notes))),
            "suggestion": label,
        }
        entries[h] = {
            "edit": {
                **EDITABLE_DEFAULTS,
                "mode": mode,
                "program": prog,
                "drum_note": suggest_drum_note(notes) if mode == "percussion" else 0,
            },
            "meta": meta,
        }
    return entries


def merge_existing(entries: dict[str, dict], toml_path: Path) -> tuple[int, int]:
    """Preserva campos editáveis (e voz inteira, se ausente nos dumps atuais)."""
    if not toml_path.exists():
        return 0, 0
    with open(toml_path, "rb") as f:
        old = tomllib.load(f).get("voices", {})
    preserved = kept = 0
    for h, ov in old.items():
        if h in entries:
            for k in EDITABLE_DEFAULTS:
                if k in ov:
                    entries[h]["edit"][k] = ov[k]
                    preserved += 1
        else:
            edit = {k: ov.get(k, EDITABLE_DEFAULTS[k]) for k in EDITABLE_DEFAULTS}
            entries[h] = {"edit": edit, "meta": ov.get("meta", {})}
            kept += 1
    return preserved, kept


HEADER = """\
# voices.toml — catálogo de instrumentos FM do Eternal Champions (A5).
# Gerado por tools/audio/voice_catalog.py; hash = contrato em docs/audio_voice_hash.md.
#
# Campos EDITÁVEIS (o motor só lê estes; reexecutar o script os preserva):
#   name       rótulo livre
#   mode       gm | percussion | chip | off
#   program    programa GM 0-127 (sugestão automática = ponto de partida, não verdade)
#   bank       banco do SoundFont
#   transpose  semitons somados à nota
#   volume_db  ganho em dB
#   drum_note  so p/ mode=percussion: nota GM fixa (ex. 42 = closed hi-hat; 0 = usar a nota da
#              propria trilha). Em percussao GM o program e ignorado: a nota escolhe o instrumento.
# A subtabela [voices.<hash>.meta] é REGERADA a cada execução (não edite).
# Vozes ausentes dos dumps atuais são mantidas como estão.
"""


def write_toml(entries: dict[str, dict], path: Path) -> None:
    lines = [HEADER]
    for h in sorted(entries):
        e, m = entries[h]["edit"], entries[h]["meta"]
        lines.append(f"[voices.{h}]")
        lines.append(f"name = {_fmt(e['name'])}")
        lines.append(f"mode = {_fmt(e['mode'])}       # gm | percussion | chip | off")
        lines.append(f"program = {_fmt(int(e['program']))}")
        lines.append(f"bank = {_fmt(int(e['bank']))}")
        lines.append(f"transpose = {_fmt(int(e['transpose']))}")
        lines.append(f"volume_db = {_fmt(float(e['volume_db']))}")
        lines.append(f"drum_note = {_fmt(int(e['drum_note']))}")
        lines.append(f"[voices.{h}.meta]")
        for k, v in m.items():
            if k == "ops":
                lines.append("ops = [")
                for op in v:
                    lines.append(f"  {_fmt(op)},")
                lines.append("]")
            else:
                lines.append(f"{k} = {_fmt(v)}")
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(lines)
    # newline fixo para idempotência byte a byte no Windows
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


# ---------------------------------------------------------------------------
# MIDI por instrumento
# ---------------------------------------------------------------------------

VOICE_MIDI_CHANNELS = [0, 1, 2, 3, 4, 5, 10, 11, 12, 13, 14, 15]


def _velocity(vn: VoiceNote) -> int:
    return vn.vel  # carrier_velocity() do chip_ring_to_midi: derivada do TL dos carriers


def write_voice_midi(
    dump: Path, bgm_id: int, slug: str, notes: list[VoiceNote],
    entries: dict[str, dict], out_dir: Path,
) -> tuple[Path, int]:
    # PSG + ruído + DAC real reaproveitando o parser existente (FM nunca vai para "dac")
    other, _ = parse_chip_ring(dump, fm_perc_to_dac=False)  # "dac" = so DAC ($2A/$2B)
    by_voice: dict[str, list[NoteEvent]] = defaultdict(list)
    for n in notes:
        by_voice[n.hash].append(
            NoteEvent(f"v_{n.hash}", n.start_s, n.start_s + n.dur_s, n.note, _velocity(n))
        )
    order = sorted(by_voice, key=lambda h: (by_voice[h][0].start_s, h))
    chunks = [
        _build_track_chunk([(0, _meta_track_name("YM2612+PSG voices")), (0, _meta_set_tempo())])
    ]
    total = 0
    for i, h in enumerate(order):
        name = entries[h]["edit"]["name"]
        tname = f"v_{h}" + (f" {name}" if name else "")
        ch = VOICE_MIDI_CHANNELS[i % len(VOICE_MIDI_CHANNELS)]
        chunks.append(_notes_to_track_chunk(tname, by_voice[h], ch))
        total += len(by_voice[h])
    from chip_ring_to_midi import CHANNEL_MIDI_MAP
    for cname in ("psg0", "psg1", "psg2", "psg_noise", "dac"):
        evs = other.get(cname, [])
        if evs:
            chunks.append(_notes_to_track_chunk(cname, evs, CHANNEL_MIDI_MAP[cname]))
            total += len(evs)
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"{slug}.mid"
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), PPQN))
        for c in chunks:
            f.write(c)
    return path, total


# ---------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Catálogo global de patches FM (voices.toml + MIDI por voz).")
    ap.add_argument("--selftest", action="store_true", help="verifica o vetor de teste do hash")
    ap.add_argument("--dumps-dir", default=DEFAULT_DUMPS_DIR, help="dir com bgm_NN.appdump.txt")
    ap.add_argument("--extract", action="store_true",
                    help="gera os dumps via extract_all_midis.py --keep-temp --temp-dir <dumps-dir>")
    ap.add_argument("--toml", default=str(DEFAULT_TOML), help="voices.toml de saída")
    ap.add_argument("--midi-dir", default=str(DEFAULT_MIDI_DIR), help="saída dos MIDIs por voz")
    ap.add_argument("--no-midi", action="store_true", help="não gera os MIDIs por voz")
    ap.add_argument("--top", type=int, default=15, help="linhas do top de vozes no relatório")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    dumps = Path(args.dumps_dir)
    if args.extract:
        dumps.mkdir(parents=True, exist_ok=True)
        cmd = [sys.executable, str(Path(__file__).with_name("extract_all_midis.py")),
               "--keep-temp", "--temp-dir", str(dumps), "--out-dir", str(dumps / "midi_tmp")]
        print("[extract]", " ".join(cmd))
        subprocess.run(cmd, check=True, cwd=str(ROOT))

    patches: dict[str, VoiceInfo] = {}
    per_song: dict[str, list[VoiceNote]] = {}
    dump_by_slug: dict[str, tuple[int, Path]] = {}
    for bgm_id, slug in BGM_TABLE:
        p = dumps / f"bgm_{bgm_id:02d}.appdump.txt"
        if not p.exists():
            print(f"aviso: dump ausente {p}", file=sys.stderr)
            continue
        per_song[slug] = extract_voice_notes(p, bgm_id, slug, patches)
        dump_by_slug[slug] = (bgm_id, p)
    if not patches:
        print("nenhum patch encontrado (dumps ausentes?)")
        return 1

    entries = build_voice_entries(patches)
    toml_path = Path(args.toml)
    preserved, kept = merge_existing(entries, toml_path)
    write_toml(entries, toml_path)

    if not args.no_midi:
        for slug, (bgm_id, p) in dump_by_slug.items():
            write_voice_midi(p, bgm_id, slug, per_song[slug], entries, Path(args.midi_dir))

    # Relatório
    n_notes = sum(len(v) for v in per_song.values())
    print(f"vozes unicas: {len(patches)} | notas FM: {n_notes} | {toml_path}")
    print(f"campos editaveis preservados: {preserved} | vozes antigas mantidas: {kept}")
    print("\npor musica:")
    for slug, notes in per_song.items():
        hs = {n.hash for n in notes}
        print(f"  {slug:<24} vozes={len(hs):>3} notas={len(notes):>5}")
    print("\nvozes compartilhadas (>=2 musicas):")
    shared = [(h, e["meta"]["songs"]) for h, e in entries.items() if len(e["meta"].get("songs", [])) >= 2]
    print(f"  total: {len(shared)}")
    for h, songs in sorted(shared, key=lambda x: -len(x[1])):
        print(f"  {h} em {len(songs)} musicas: {', '.join(songs)}")
    print(f"\ntop {args.top} vozes por notas:")
    ranked = sorted(entries.items(), key=lambda kv: -kv[1]["meta"].get("notes", 0))[: args.top]
    for h, e in ranked:
        m = e["meta"]
        print(f"  {h} notas={m['notes']:>4} alg={m['alg']} fb={m['fb']} "
              f"mediana={m['note_median']:>3} sug={m['suggestion']}/{e['edit']['program']} "
              f"musicas={len(m['songs'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
