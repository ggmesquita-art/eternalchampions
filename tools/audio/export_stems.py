#!/usr/bin/env python3
"""tools/audio/export_stems.py — extrai stems MIDI por canal (fm0..psg_noise) e
gera/atualiza o perfil editável assets/audio_remaster/profiles/<track>.toml.

Aceita como entrada:
  - dump de registradores YM2612/PSG (chip_ring.txt), ou
  - arquivo MIDI multi-canal (.mid), Tipo 0 ou Tipo 1.

Uso:
    python tools/audio/export_stems.py chip_ring.txt --track 03_syria_blade --bgm-id 3
    python tools/audio/export_stems.py musica_luta.mid --track musica_luta --bgm-id 10
    python tools/audio/export_stems.py chip_ring.txt --track 03_syria_blade --keep-existing
"""

from __future__ import annotations

import argparse
from pathlib import Path
import statistics
import sys
import tomllib
from typing import Any

# Permite executar como script direto ou importar via pacote/sys.path
_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from chip_ring_to_midi import (  # noqa: E402
    CHANNEL_ORDER,
    NoteEvent,
    parse_chip_ring,
    read_midi_notes,
    write_per_channel_midis,
)

DEFAULT_SOUNDFONT = "/usr/share/sounds/sf2/TimGM6mb.sf2"
DEFAULT_BASE_DIR = Path("assets/audio_remaster")

# Pan estéreo inicial balanceado por canal físico (graves/bumbo no centro)
_DEFAULT_PAN: dict[str, float] = {
    "fm0": -0.20,
    "fm1": 0.20,
    "fm2": -0.35,
    "fm3": 0.35,
    "fm4": -0.25,
    "fm5": 0.25,
    "dac": 0.0,
    "psg0": -0.45,
    "psg1": 0.0,
    "psg2": 0.45,
    "psg_noise": 0.15,
}


def _classify_channel(
    ch_name: str, events: list[NoteEvent], track_duration_s: float
) -> dict[str, Any]:
    """Heurística inicial de timbre GM, papel musical, pan e ganho por canal (padrão C_hybrid_sf2_balanced)."""
    base: dict[str, Any] = {
        "role": "unused",
        "mode": "off",
        "program": 0,
        "bank": 0,
        "volume_db": 0.0,
        "pan": _DEFAULT_PAN.get(ch_name, 0.0),
        "mute": False,
        "solo": False,
        "eq_low_db": 0.0,
        "eq_mid_db": 0.0,
        "eq_high_db": 0.0,
        "transpose": 0,
    }

    if not events:
        base["mute"] = True
        return base

    if ch_name == "dac":
        base.update(
            {
                "role": "drums",
                "mode": "percussion",
                "program": 0,  # GM Standard Kit
                "bank": 128,
                "volume_db": -3.0,
                "pan": 0.0,
                "eq_low_db": 1.5,
                "eq_high_db": 0.5,
            }
        )
        return base

    if ch_name == "psg_noise":
        base.update(
            {
                "role": "percussion",
                "mode": "percussion",
                "program": 0,  # GM Standard Kit
                "bank": 128,
                "volume_db": -3.0,
                "pan": 0.15,
                "eq_high_db": 1.0,
            }
        )
        return base

    pitches = [e.note for e in events]
    durs = [e.duration_s for e in events]
    med_pitch = float(statistics.median(pitches))
    med_dur = float(statistics.median(durs))
    density = len(events) / max(1.0, track_duration_s)

    if ch_name.startswith("psg"):
        # Canais tonais PSG: sutis em estéreo sem dominar o mix
        if med_pitch < 55.0:
            role = "psg_bass"
        elif density >= 4.0:
            role = "psg_arp"
        else:
            role = "psg_harmony"
        base.update(
            {
                "role": role,
                "mode": "gm",
                "program": 80,  # Lead 1 (Square)
                "bank": 0,
                "volume_db": -10.0,
                "eq_low_db": -2.0,
                "eq_high_db": 0.0,
            }
        )
        return base

    # Canais FM (YM2612 fm0..fm5): SEMPRE tonais por padrão ("gm"), nunca percussão automática
    if med_dur >= 0.25 and med_pitch > 40.0:
        # Notas sustentadas / acordes médios -> Strings
        base.update(
            {
                "role": "strings",
                "mode": "gm",
                "program": 49,  # String Ensemble 2 / Slow Strings
                "bank": 0,
                "volume_db": -3.0,
                "eq_mid_db": -1.0,
                "eq_high_db": 0.5,
            }
        )
    elif med_pitch <= 52.0:
        # Graves -> Electric Bass (Finger) centralizado
        base.update(
            {
                "role": "bass",
                "mode": "gm",
                "program": 33,  # Electric Bass (finger)
                "bank": 0,
                "volume_db": 0.0,
                "pan": 0.0,
                "eq_low_db": 1.5,
                "eq_mid_db": -1.0,
            }
        )
    elif med_pitch >= 68.0:
        # Região aguda -> Lead 2 (Sawtooth)
        base.update(
            {
                "role": "lead",
                "mode": "gm",
                "program": 81,  # Lead 2 (Sawtooth)
                "bank": 0,
                "volume_db": -3.0,
                "eq_mid_db": 0.0,
                "eq_high_db": 0.5,
            }
        )
    else:
        # Rítmicos / região média -> Synth Brass 1
        base.update(
            {
                "role": "brass",
                "mode": "gm",
                "program": 62,  # SynthBrass 1
                "bank": 0,
                "volume_db": -2.5,
                "eq_mid_db": -0.5,
                "eq_high_db": 0.5,
            }
        )
    return base


def _toml_val(v: Any) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        s = f"{v:.4f}".rstrip("0").rstrip(".")
        return s if "." in s else f"{s}.0"
    if isinstance(v, str):
        esc = v.replace("\\", "\\\\").replace('"', '\\"')
        return f'"{esc}"'
    raise TypeError(f"Tipo TOML não suportado: {type(v)!r}")


def format_profile_toml(profile: dict[str, Any]) -> str:
    """Serializa perfil no contrato exato de assets/audio_remaster/profiles/<track>.toml."""
    tr = profile["track"]
    lines: list[str] = [
        "# Perfil de remasterização por canal (editável manualmente ou via mixer_studio.py)",
        "[track]",
    ]
    track_keys = (
        "name",
        "bgm_id",
        "soundfont",
        "sample_rate",
        "loop_start",
        "loop_end",
        "master_gain_db",
        "target_rms_dbfs",
        "chip_blend",
    )
    for k in track_keys:
        if k in tr:
            lines.append(f"{k} = {_toml_val(tr[k])}")
    for k, v in tr.items():
        if k not in track_keys:
            lines.append(f"{k} = {_toml_val(v)}")

    ch_keys = (
        "role",
        "mode",
        "program",
        "bank",
        "volume_db",
        "pan",
        "mute",
        "solo",
        "eq_low_db",
        "eq_mid_db",
        "eq_high_db",
        "transpose",
    )
    channels: dict[str, dict[str, Any]] = profile.get("channels", {})
    ordered_chs = list(CHANNEL_ORDER) + [c for c in channels if c not in CHANNEL_ORDER]
    for ch in ordered_chs:
        if ch not in channels:
            continue
        cfg = channels[ch]
        lines.append("")
        lines.append(f"[channels.{ch}]")
        for k in ch_keys:
            if k in cfg:
                lines.append(f"{k} = {_toml_val(cfg[k])}")
        for k, v in cfg.items():
            if k not in ch_keys:
                lines.append(f"{k} = {_toml_val(v)}")

    lines.append("")
    return "\n".join(lines)


def build_default_profile(
    track_name: str,
    notes_by_channel: dict[str, list[NoteEvent]],
    bgm_id: int = 0,
    soundfont: str = DEFAULT_SOUNDFONT,
    sample_rate: int = 48000,
    loop_start: int = 0,
    loop_end: int = 0,
    chip_blend: float = 0.65,
    target_rms_dbfs: float = -16.5,
) -> dict[str, Any]:
    all_ends = [e.end_s for evs in notes_by_channel.values() for e in evs]
    duration_s = max(all_ends) if all_ends else 1.0

    channels: dict[str, dict[str, Any]] = {}
    for ch in CHANNEL_ORDER:
        channels[ch] = _classify_channel(ch, notes_by_channel.get(ch, []), duration_s)

    return {
        "track": {
            "name": track_name,
            "bgm_id": int(bgm_id),
            "soundfont": soundfont,
            "sample_rate": int(sample_rate),
            "loop_start": int(loop_start),
            "loop_end": int(loop_end),
            "master_gain_db": 0.0,
            "target_rms_dbfs": float(target_rms_dbfs),
            "chip_blend": float(chip_blend),
        },
        "channels": channels,
    }


def merge_existing_profile(
    existing: dict[str, Any], generated: dict[str, Any]
) -> dict[str, Any]:
    """Preserva ajustes manuais existentes e preenche apenas chaves/canais faltantes."""
    merged_track = dict(generated["track"])
    merged_track.update(existing.get("track", {}))

    merged_channels: dict[str, dict[str, Any]] = {}
    ex_chs = existing.get("channels", {})
    for ch in CHANNEL_ORDER:
        base = dict(generated["channels"][ch])
        if ch in ex_chs and isinstance(ex_chs[ch], dict):
            base.update(ex_chs[ch])
        merged_channels[ch] = base
    for ch, cfg in ex_chs.items():
        if ch not in merged_channels and isinstance(cfg, dict):
            merged_channels[ch] = dict(cfg)

    return {"track": merged_track, "channels": merged_channels}


def export_stems(
    input_path: str | Path,
    track_name: str,
    base_dir: str | Path = DEFAULT_BASE_DIR,
    bgm_id: int = 0,
    soundfont: str = DEFAULT_SOUNDFONT,
    sample_rate: int = 48000,
    start_wf: int = 0,
    end_wf: int = 0,
    keep_existing: bool = False,
    legacy_fm_perc: bool = False,
    include_empty_midi: bool = False,
) -> dict[str, Any]:
    """Extrai stems MIDI por canal e grava o perfil TOML da faixa."""
    inp = Path(input_path)
    if not inp.exists():
        raise FileNotFoundError(f"Entrada não encontrada: {inp}")

    if inp.suffix.lower() in (".mid", ".midi"):
        notes_by_channel = read_midi_notes(inp)
        source_kind = "midi"
    else:
        notes_by_channel, _stats = parse_chip_ring(
            inp,
            start_wf=start_wf,
            end_wf=end_wf,
            legacy_fm_perc=legacy_fm_perc,
        )
        source_kind = "chip_ring"

    root = Path(base_dir)
    midi_dir = root / "stems" / track_name / "midi"
    profile_path = root / "profiles" / f"{track_name}.toml"
    profile_path.parent.mkdir(parents=True, exist_ok=True)

    written_midis = write_per_channel_midis(
        notes_by_channel, midi_dir, include_empty=include_empty_midi
    )

    generated = build_default_profile(
        track_name=track_name,
        notes_by_channel=notes_by_channel,
        bgm_id=bgm_id,
        soundfont=soundfont,
        sample_rate=sample_rate,
    )
    if keep_existing and profile_path.exists():
        with open(profile_path, "rb") as f:
            existing = tomllib.load(f)
        profile = merge_existing_profile(existing, generated)
    else:
        profile = generated

    profile_path.write_text(format_profile_toml(profile), encoding="utf-8")

    return {
        "track": track_name,
        "source": str(inp),
        "source_kind": source_kind,
        "midi_dir": str(midi_dir),
        "profile_path": str(profile_path),
        "channels_exported": sorted(written_midis.keys(), key=lambda c: CHANNEL_ORDER.index(c)),
        "note_counts": {ch: len(notes_by_channel[ch]) for ch in CHANNEL_ORDER},
    }


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Extrai stems MIDI por canal e gera perfil TOML de remasterização."
    )
    ap.add_argument("input", help="Arquivo chip_ring.txt ou .mid multi-canal")
    ap.add_argument(
        "--track",
        required=True,
        help="Nome canônico da faixa (ex.: 03_syria_blade)",
    )
    ap.add_argument(
        "--base-dir",
        default=str(DEFAULT_BASE_DIR),
        help="Diretório raiz de remasterização (default: assets/audio_remaster)",
    )
    ap.add_argument("--bgm-id", type=int, default=0, help="ID BGM no jogo")
    ap.add_argument(
        "--soundfont",
        default=DEFAULT_SOUNDFONT,
        help="Caminho do SoundFont (.sf2)",
    )
    ap.add_argument("--sample-rate", type=int, default=48000, help="Sample rate alvo (Hz)")
    ap.add_argument("--start-wf", type=int, default=0, help="Frame inicial (para chip_ring.txt)")
    ap.add_argument("--end-wf", type=int, default=0, help="Frame final (para chip_ring.txt)")
    ap.add_argument(
        "--keep-existing",
        action="store_true",
        help="Preserva ajustes manuais se profiles/<track>.toml já existir",
    )
    ap.add_argument(
        "--legacy-fm-perc",
        action="store_true",
        help="Habilita heurística legada de FM <60ms -> percussão (default: OFF)",
    )
    args = ap.parse_args(argv)

    res = export_stems(
        input_path=args.input,
        track_name=args.track,
        base_dir=args.base_dir,
        bgm_id=args.bgm_id,
        soundfont=args.soundfont,
        sample_rate=args.sample_rate,
        start_wf=args.start_wf,
        end_wf=args.end_wf,
        keep_existing=args.keep_existing,
        legacy_fm_perc=args.legacy_fm_perc,
    )
    counts = ", ".join(
        f"{ch}={res['note_counts'][ch]}"
        for ch in res["channels_exported"]
    )
    print(
        f"[export_stems] {res['track']} ({res['source_kind']}) -> "
        f"{len(res['channels_exported'])} canais ({counts})"
    )
    print(f"  MIDI stems: {res['midi_dir']}")
    print(f"  Profile:    {res['profile_path']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
