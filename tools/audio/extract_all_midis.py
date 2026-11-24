#!/usr/bin/env python3
"""tools/audio/extract_all_midis.py — Captura todas as 17 músicas da ROM (bgm_id 0..16)
via EternalChampionsRecomp.exe headless (GENESIS_AUDIO_APPLIEDDUMP) e gera os 17 arquivos
MIDI Tipo 1 multi-canal consolidados em assets/audio_remaster/midi/<slug>.mid.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from chip_ring_to_midi import (  # noqa: E402
    CHANNEL_ORDER,
    convert_chip_ring_to_midi,
    read_midi_notes,
)

BGM_TABLE: list[tuple[int, str]] = [
    (0, "03_syria_blade"),
    (1, "bgm_01_sanctuary"),
    (2, "bgm_02_stage_s25"),
    (3, "bgm_03_stage_s23"),
    (4, "02_salem_xavier"),
    (5, "bgm_05_stage_s22"),
    (6, "bgm_06_stage_s26"),
    (7, "musica_luta"),
    (8, "01_vietnam_midknight"),
    (9, "bgm_09_bank1_09"),
    (10, "bgm_10_stage_s27"),
    (11, "bgm_11_stage_s24"),
    (12, "musica_titulo"),
    (13, "bgm_13_bank2_01"),
    (14, "bgm_14_bank2_02"),
    (15, "bgm_15_bank2_03"),
    (16, "musica_selecao"),
]

FRAME_PREFIX_RE = re.compile(r"^f=(\d+)\b")


class _ScriptBuilder:
    def __init__(self) -> None:
        self.lines: list[str] = []
        self.frame = 1

    def wait(self, n: int) -> None:
        if n > 0:
            self.lines.append(f"WAIT {n}")
            self.frame += n

    def tap(self, btn: str, hold: int = 10, post: int = 15) -> None:
        self.lines.append(f"HOLD {btn}")
        self.wait(hold)
        self.lines.append("RELEASE")
        self.wait(post)

    def dump_ram(self, path: Path) -> None:
        p_str = str(path.resolve()).replace("\\", "/")
        self.lines.append(f"DUMP_RAM {p_str}")

    def write(self, path: Path) -> None:
        self.lines.append("EXIT 0")
        path.write_text("\n".join(self.lines) + "\n", encoding="utf-8")


def _boot_to_main_menu(sb: _ScriptBuilder) -> None:
    sb.wait(800)
    sb.tap("START", hold=15, post=85)
    sb.tap("START", hold=15, post=325)
    sb.tap("START", hold=15, post=145)  # Title -> Main Menu


def _build_music_test_script(
    script_path: Path,
    ram_path: Path,
    track_frames: int,
) -> list[tuple[int, int, int]]:
    """Navega até Options -> Music Test e captura bgm_id 0..15 em sequência limpa."""
    sb = _ScriptBuilder()
    _boot_to_main_menu(sb)

    # Main Menu -> Options (item 4)
    for _ in range(4):
        sb.tap("DOWN", hold=10, post=20)
    sb.tap("START", hold=15, post=90)

    # Options -> Music Test (row 3)
    for _ in range(3):
        sb.tap("DOWN", hold=10, post=15)

    # Silencia qualquer música residual antes de iniciar a bateria
    sb.tap("B", hold=10, post=50)

    windows: list[tuple[int, int, int]] = []
    for bgm_id in range(16):
        if bgm_id > 0:
            sb.tap("RIGHT", hold=8, post=20)
        f_start = sb.frame
        sb.tap("A", hold=10, post=track_frames)
        f_end = sb.frame
        sb.tap("B", hold=10, post=50)
        windows.append((bgm_id, f_start, f_end))

    sb.dump_ram(ram_path)
    sb.write(script_path)
    return windows


def _build_char_select_script(
    script_path: Path,
    ram_path: Path,
    track_frames: int,
) -> tuple[int, int]:
    """Navega até Character Select e grava bgm_id 16 (musica_selecao) sem SFX."""
    sb = _ScriptBuilder()
    _boot_to_main_menu(sb)

    # Main Menu -> One Player -> Fight -> Enter Contest -> Character Select
    sb.tap("START", hold=15, post=145)
    sb.tap("START", hold=15, post=145)
    sb.tap("DOWN", hold=10, post=15)
    sb.tap("START", hold=15, post=120)

    # Aguarda tela estabilizar sem nenhum input para captura 100% limpa
    f_start = sb.frame
    sb.wait(track_frames)
    f_end = sb.frame

    sb.dump_ram(ram_path)
    sb.write(script_path)
    return f_start, f_end


def _run_headless(
    exe_path: Path,
    rom_path: Path,
    script_path: Path,
    dump_path: Path,
    bench_limit: int,
) -> None:
    env = os.environ.copy()
    env["SDL_VIDEODRIVER"] = "dummy"
    env["SDL_AUDIODRIVER"] = "dummy"
    env["SDL_RENDER_DRIVER"] = "software"
    env["GENESIS_AUDIO_APPLIEDDUMP"] = str(dump_path.resolve())

    cmd = [
        str(exe_path),
        "--benchmark",
        str(bench_limit),
        "--turbo",
        "--no-launcher",
        "--input-script",
        str(script_path),
        str(rom_path),
    ]
    res = subprocess.run(
        cmd,
        cwd=str(ROOT),
        env=env,
        capture_output=True,
        text=True,
        timeout=600,
    )
    if res.returncode != 0:
        raise RuntimeError(
            f"Falha ao executar {exe_path.name} (exit={res.returncode}):\n{res.stderr[-2000:]}"
        )
    if not dump_path.exists() or dump_path.stat().st_size == 0:
        raise RuntimeError(f"Dump de áudio vazio ou ausente: {dump_path}")


def _split_dump_by_windows(
    dump_path: Path,
    windows: list[tuple[int, int, int]],
    out_dir: Path,
) -> dict[int, Path]:
    """Fatia um dump contínuo em arquivos por bgm_id em uma única leitura sequencial."""
    sorted_wins = sorted(windows, key=lambda w: w[1])
    handles: dict[int, object] = {}
    paths: dict[int, Path] = {}
    for bgm_id, _, _ in sorted_wins:
        p = out_dir / f"bgm_{bgm_id:02d}.appdump.txt"
        paths[bgm_id] = p
        handles[bgm_id] = open(p, "w", encoding="utf-8")

    try:
        idx = 0
        n_wins = len(sorted_wins)
        with open(dump_path, "r", encoding="utf-8", errors="replace") as src:
            for line in src:
                m = FRAME_PREFIX_RE.match(line)
                if not m:
                    continue
                f = int(m.group(1))
                while idx < n_wins and f >= sorted_wins[idx][2]:
                    idx += 1
                if idx >= n_wins:
                    break
                bgm_id, f_start, f_end = sorted_wins[idx]
                if f_start <= f < f_end:
                    handles[bgm_id].write(line)
    finally:
        for fh in handles.values():
            fh.close()

    return paths


def _summarize_midi(midi_path: Path) -> dict[str, object]:
    notes_by_ch = read_midi_notes(midi_path)
    all_notes = [ev for ch in CHANNEL_ORDER for ev in notes_by_ch[ch]]
    duration_s = max((ev.end_s for ev in all_notes), default=0.0)
    active_channels = [ch for ch in CHANNEL_ORDER if notes_by_ch[ch]]
    high_fm = sum(
        1
        for ch in ("fm0", "fm1", "fm2", "fm3", "fm4", "fm5")
        for ev in notes_by_ch[ch]
        if ev.note >= 96
    )
    return {
        "bytes": midi_path.stat().st_size,
        "duration_s": duration_s,
        "total_notes": len(all_notes),
        "active_channels": active_channels,
        "high_fm": high_fm,
    }


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Extrai todas as 17 músicas da ROM (bgm_id 0..16) para MIDI Tipo 1."
    )
    ap.add_argument(
        "--exe",
        default=str(ROOT / "build" / "EternalChampionsRecomp.exe"),
        help="Caminho do EternalChampionsRecomp.exe",
    )
    ap.add_argument(
        "--rom",
        default=str(ROOT / "roms" / "eternal_champions_us.gen"),
        help="Caminho da ROM",
    )
    ap.add_argument(
        "--out-dir",
        default=str(ROOT / "assets" / "audio_remaster" / "midi"),
        help="Diretório de saída dos arquivos .mid",
    )
    ap.add_argument(
        "--frames",
        type=int,
        default=2400,
        help="Quadros gravados por música (~60 fps, default: 2400 = ~40s)",
    )
    ap.add_argument(
        "--temp-dir",
        default=str(Path(tempfile.gettempdir()) / "opencode" / "midi_extract"),
        help="Diretório temporário para dumps brutos",
    )
    ap.add_argument(
        "--keep-temp",
        action="store_true",
        help="Mantém os dumps temporários ao final",
    )
    args = ap.parse_args(argv)

    exe_path = Path(args.exe)
    rom_path = Path(args.rom)
    out_dir = Path(args.out_dir)
    temp_dir = Path(args.temp_dir)
    if not exe_path.exists():
        raise FileNotFoundError(f"Executável não encontrado: {exe_path}")
    if not rom_path.exists():
        raise FileNotFoundError(f"ROM não encontrada: {rom_path}")

    out_dir.mkdir(parents=True, exist_ok=True)
    temp_dir.mkdir(parents=True, exist_ok=True)

    slug_by_id = dict(BGM_TABLE)
    slice_paths: dict[int, Path] = {}

    try:
        # 1) Music Test (bgm_id 0..15)
        mt_script = temp_dir / "music_test_0_15.input"
        mt_ram = temp_dir / "music_test_0_15.ram"
        mt_dump = temp_dir / "music_test_0_15.appdump.txt"
        mt_windows = _build_music_test_script(mt_script, mt_ram, args.frames)
        mt_bench = mt_windows[-1][2] + 500
        print(
            f"[extract] Capturando bgm_id 0..15 via Music Test ({args.frames} quadros/faixa, ~{mt_bench} frames)..."
        )
        t0 = time.time()
        _run_headless(exe_path, rom_path, mt_script, mt_dump, mt_bench)
        ram_bytes = mt_ram.read_bytes()
        final_mt_id = ram_bytes[0x9371]
        final_cursor = struct.unpack_from(">H", ram_bytes, 0x9B4C)[0]
        print(
            f"[extract] Music Test concluído em {time.time() - t0:.1f}s "
            f"(RAM $9371={final_mt_id}, cursor={final_cursor})."
        )
        if final_mt_id != 15 or final_cursor != 3:
            raise RuntimeError(
                f"Estado inesperado no Music Test: $9371={final_mt_id} (esperado 15), cursor={final_cursor} (esperado 3)"
            )
        slice_paths.update(_split_dump_by_windows(mt_dump, mt_windows, temp_dir))
        if not args.keep_temp and mt_dump.exists():
            mt_dump.unlink()

        # 2) Character Select (bgm_id 16)
        cs_script = temp_dir / "char_select_16.input"
        cs_ram = temp_dir / "char_select_16.ram"
        cs_dump = temp_dir / "char_select_16.appdump.txt"
        cs_start, cs_end = _build_char_select_script(cs_script, cs_ram, args.frames)
        cs_bench = cs_end + 500
        print(
            f"[extract] Capturando bgm_id 16 (musica_selecao) via Character Select ({args.frames} quadros)..."
        )
        t1 = time.time()
        _run_headless(exe_path, rom_path, cs_script, cs_dump, cs_bench)
        print(f"[extract] Character Select concluído em {time.time() - t1:.1f}s.")
        slice_paths.update(_split_dump_by_windows(cs_dump, [(16, cs_start, cs_end)], temp_dir))
        if not args.keep_temp and cs_dump.exists():
            cs_dump.unlink()

        # 3) Conversão para MIDI Tipo 1 + validação
        print("\n=== Tabela de MIDIs extraídos ===")
        print(
            f"{'ID':>2}  {'Slug':<24} {'Bytes':>7} {'Dur(s)':>7} {'Notas':>6} {'HighFM':>6}  {'Canais ativos'}"
        )
        failures: list[str] = []
        for bgm_id, slug in BGM_TABLE:
            mid_path = out_dir / f"{slug}.mid"
            slice_p = slice_paths[bgm_id]
            _notes, _stats = convert_chip_ring_to_midi(slice_p, mid_path)
            info = _summarize_midi(mid_path)
            ch_str = ", ".join(str(c) for c in info["active_channels"])  # type: ignore[iterable]
            print(
                f"{bgm_id:2d}  {slug:<24} {info['bytes']:>7} {info['duration_s']:>7.2f} "
                f"{info['total_notes']:>6} {info['high_fm']:>6}  {ch_str}"
            )
            if info["total_notes"] <= 0:  # type: ignore[operator]
                failures.append(f"{slug}: 0 notas")
            if info["duration_s"] < 10.0:  # type: ignore[operator]
                failures.append(f"{slug}: duração < 10s ({info['duration_s']:.2f}s)")
            if info["high_fm"] > 0:  # type: ignore[operator]
                failures.append(f"{slug}: high_fm(>=96)={info['high_fm']}")

        if failures:
            print("\nAVISOS/EXCEÇÕES DETECTADAS:")
            for msg in failures:
                print(f"  - {msg}")
        else:
            print("\nTodos os 17 MIDIs validados com sucesso (notas > 0, dur >= 10s, high_fm == 0).")

        return 0
    finally:
        if not args.keep_temp and temp_dir.exists():
            shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
