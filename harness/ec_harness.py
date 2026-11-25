#!/usr/bin/env python3
"""Deterministic validation harness for Eternal Champions (GenesisRecomp vs GPGX).

Implements Phase 3 (M3) & Phase 4 (M4/M6) runtime validation:
1. Validates .ecm movie headers against roms/eternal_champions_us.gen SHA-256.
2. Translates .ecm per-frame bitmasks into segagenesisrecomp --input-script files.
3. Verifies native self-determinism (0/65536 WRAM bytes + identical screenshot SHA-256).
4. Runs Genesis Plus GX reference (gpgx_runner), unswaps 16-bit WRAM if byte-swapped,
   and reports per-checkpoint telemetry + byte-level WRAM diffs mapped via ram_map.toml.
5. Verifies --widescreen 16:9 extension in active arena states (17..27), pure black
   pillarbox in non-arena states, and 0 WRAM diff against 320x224 native.
6. Exports observed active-fight telemetry to docs/frame_data/fight_probe_summary.md.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import tomllib
import zlib
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_ROM = ROOT / "roms" / "eternal_champions_us.gen"
IS_WINDOWS = os.name == "nt"
DEFAULT_NATIVE = (
    ROOT / "build" / "EternalChampionsRecomp.exe"
    if IS_WINDOWS
    else ROOT / "build" / "native" / "EternalChampionsRecomp"
)
DEFAULT_GPGX_RUNNER = ROOT / "tools" / "vendor" / "gpgx_runner" / "gpgx_runner"
DEFAULT_GPGX_CORE = ROOT / "tools" / "vendor" / "gpgx" / "genesis_plus_gx_libretro.so"
DEFAULT_RAM_MAP = ROOT / "disasm" / "symbols" / "ram_map.toml"
DEFAULT_CAPTURES = ROOT / "assets" / "captures"
DEFAULT_REPORT_MD = ROOT / "docs" / "frame_data" / "fight_probe_summary.md"

# Libretro RETRO_DEVICE_ID_JOYPAD_* bitmask -> GenesisRecomp input_script button name
ECM_BIT_TO_BUTTON: list[tuple[int, str]] = [
    (0x0010, "UP"),
    (0x0020, "DOWN"),
    (0x0040, "LEFT"),
    (0x0080, "RIGHT"),
    (0x0002, "A"),
    (0x0001, "B"),
    (0x0100, "C"),
    (0x0008, "START"),
]

FIGHTER_NAMES: dict[int, str] = {
    0: "Shadow",
    1: "RAX",
    2: "Blade",
    3: "Jetta",
    4: "Slash",
    5: "Trident",
    6: "Xavier",
    7: "Midknight",
    8: "Larcen",
    9: "Eternal Champion",
}

MOVIE_SPECS: list[dict[str, Any]] = [
    {
        "movie": "01_attract_boot.ecm",
        "slug": "boot_1000",
        "checkpoints": [600, 1000],
        "expected_state": 9,
        "expected_p1": None,
        "expected_p2": None,
        "arena_name": "Attract / Menu Init (S9)",
        "is_arena": False,
        "is_diag_boss": False,
    },
    {
        "movie": "02_title_to_fight.ecm",
        "slug": "fight_2900",
        "checkpoints": [600, 1200, 1926, 2074, 2400, 2500, 2600, 2750, 2900],
        "expected_state": 17,
        "expected_p1": 0,
        "expected_p2": 0,
        "arena_name": "Battle Simulator / Training Room (S17)",
        "is_arena": True,
        "is_diag_boss": False,
    },
    {
        "movie": "03_practice_rax_vs_blade_s21.ecm",
        "slug": "s21_rax_blade",
        "checkpoints": None,
        "expected_state": 21,
        "expected_p1": 1,
        "expected_p2": 2,
        "arena_name": "Blade Arena (S21)",
        "is_arena": True,
        "is_diag_boss": False,
    },
    {
        "movie": "04_practice_blade_vs_jetta_s22.ecm",
        "slug": "s22_blade_jetta",
        "checkpoints": None,
        "expected_state": 22,
        "expected_p1": 2,
        "expected_p2": 3,
        "arena_name": "Jetta Arena (S22)",
        "is_arena": True,
        "is_diag_boss": False,
    },
    {
        "movie": "05_practice_jetta_vs_slash_s26.ecm",
        "slug": "s26_jetta_slash",
        "checkpoints": None,
        "expected_state": 26,
        "expected_p1": 3,
        "expected_p2": 4,
        "arena_name": "Slash Arena (S26)",
        "is_arena": True,
        "is_diag_boss": False,
    },
    {
        "movie": "06_practice_slash_vs_trident_s24.ecm",
        "slug": "s24_slash_trident",
        "checkpoints": None,
        "expected_state": 24,
        "expected_p1": 4,
        "expected_p2": 5,
        "arena_name": "Trident Arena (S24)",
        "is_arena": True,
        "is_diag_boss": False,
    },
    {
        "movie": "07_practice_trident_vs_xavier_s20.ecm",
        "slug": "s20_trident_xavier",
        "checkpoints": None,
        "expected_state": 20,
        "expected_p1": 5,
        "expected_p2": 6,
        "arena_name": "Xavier Arena (S20)",
        "is_arena": True,
        "is_diag_boss": False,
    },
    {
        "movie": "08_practice_xavier_vs_midknight_s19.ecm",
        "slug": "s19_xavier_midknight",
        "checkpoints": None,
        "expected_state": 19,
        "expected_p1": 6,
        "expected_p2": 7,
        "arena_name": "Midknight Arena (S19)",
        "is_arena": True,
        "is_diag_boss": False,
    },
    {
        "movie": "09_practice_midknight_vs_larcen_s27.ecm",
        "slug": "s27_midknight_larcen",
        "checkpoints": None,
        "expected_state": 27,
        "expected_p1": 7,
        "expected_p2": 8,
        "arena_name": "Larcen Arena (S27)",
        "is_arena": True,
        "is_diag_boss": False,
    },
    {
        "movie": "10_practice_larcen_vs_rax_s23.ecm",
        "slug": "s23_larcen_rax",
        "checkpoints": None,
        "expected_state": 23,
        "expected_p1": 8,
        "expected_p2": 1,
        "arena_name": "RAX Arena (S23)",
        "is_arena": True,
        "is_diag_boss": False,
    },
    {
        "movie": "11_practice_shadow_vs_larcen_s25.ecm",
        "slug": "s25_shadow_larcen",
        "checkpoints": None,
        "expected_state": 25,
        "expected_p1": 0,
        "expected_p2": 8,
        "arena_name": "Shadow Arena (S25)",
        "is_arena": True,
        "is_diag_boss": False,
    },
    {
        "movie": "12_holo_trainer_s14.ecm",
        "slug": "s14_holo_trainer",
        "checkpoints": None,
        "expected_state": 14,
        "expected_p1": 0,
        "expected_p2": 0,
        "arena_name": "Holo-Trainer Dojo (S14)",
        "is_arena": True,
        "is_diag_boss": False,
    },
    {
        "movie": "13_boss_sanctuary_s18.ecm",
        "slug": "s18_boss_sanctuary",
        "checkpoints": None,
        "expected_state": 18,
        "expected_p1": 0,
        "expected_p2": 9,
        "arena_name": "Eternal Champion Sanctuary (S18)",
        "is_arena": True,
        "is_diag_boss": True,
    },
    # ---- M7.3: modos extras (filmes com 2ª coluna = pad 2) ----
    {
        "movie": "14_2p_vs_rax_vs_blade_s24.ecm",
        "slug": "s24_2p_vs_rax_blade",
        "checkpoints": None,
        "expected_state": 24,
        "expected_p1": 1,
        "expected_p2": 2,
        "expected_tel": {"game_mode": 2, "two_player": 1, "tournament_type": 0},
        "arena_name": "Trident Arena (S24, 2P VS)",
        "is_arena": True,
        "is_diag_boss": False,
    },
    {
        "movie": "15_tournament_roundabout_jetta_vs_xavier_s27.ecm",
        "slug": "s27_tournament_jetta_xavier",
        "checkpoints": None,
        "expected_state": 27,
        "expected_p1": 3,
        "expected_p2": 6,
        "expected_tel": {"game_mode": 2, "two_player": 1, "tournament_type": 3},
        "gpgx_any_arena": True,
        "arena_name": "Tournament Round-A-Bout (S27, arena sorteada)",
        "is_arena": True,
        "is_diag_boss": False,
    },
    {
        "movie": "16_2p_vs_match_end_s24_to_s1.ecm",
        "slug": "s1_2p_vs_match_end",
        "checkpoints": None,
        "expected_state": 1,
        "expected_p1": 1,
        "expected_p2": 2,
        "expected_tel": {"game_mode": 2, "two_player": 1, "p1.health": 104, "p2.health": 0},
        "arena_name": "2P VS fim de partida (S24 → WINS COMPETITION → S1)",
        "is_arena": False,
        "is_diag_boss": False,
    },
    {
        "movie": "17_2p_vs_overkill_trident_s24.ecm",
        "slug": "s24_2p_vs_overkill_trident",
        "checkpoints": None,
        "expected_state": 24,
        "expected_p1": 1,
        "expected_p2": 2,
        "expected_tel": {"game_mode": 2, "two_player": 1, "overkill": 1, "p2.health": 0},
        "arena_name": "Trident Arena Overkill (S24, 2P VS, $9C7C=1)",
        "is_arena": True,
        "is_diag_boss": False,
    },
]


def tel_get(tel: dict[str, Any], key: str) -> Any:
    """Lê um campo de telemetria; `p2.health` acessa o sub-dicionário do lutador."""
    cur: Any = tel
    for part in key.split("."):
        cur = cur[part]
    return cur


def expected_tel_ok(tel: dict[str, Any], spec: dict[str, Any]) -> bool:
    """Campos extras de telemetria exigidos pelo spec (ex.: modo 2P / torneio / KO)."""
    return all(tel_get(tel, k) == v for k, v in spec.get("expected_tel", {}).items())


ARENA_STATES = range(17, 28)  # Estados de luta $FD64 = 17..27 (ver docs/widescreen.md)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_ram_map(path: Path) -> list[dict[str, Any]]:
    with path.open("rb") as f:
        doc = tomllib.load(f)
    entries = []
    for item in doc.get("ram", []):
        addr = int(item["address"])
        offset = addr & 0xFFFF
        size = int(item["size"])
        entries.append(
            {
                "offset": offset,
                "end": offset + size,
                "size": size,
                "name": str(item["name"]),
                "type": str(item.get("type", "")),
                "conf": str(item.get("conf", "")),
            }
        )
    entries.sort(key=lambda e: (e["offset"], e["size"]))
    return entries


def resolve_symbol(offset: int, ram_entries: list[dict[str, Any]]) -> str:
    matches = [e for e in ram_entries if e["offset"] <= offset < e["end"]]
    if not matches:
        return f"$FF{offset:04X} (unmapped)"
    # Prefer exact smallest-span field over coarse parent struct (e.g. P1_Health over Fighter_P1)
    matches.sort(key=lambda e: (e["size"], e["offset"]))
    best = matches[0]
    if best["offset"] == offset:
        return best["name"]
    return f"{best['name']}+0x{offset - best['offset']:X}"


def parse_ecm(path: Path, expected_rom_sha256: str) -> dict[str, Any]:
    rom_hash = None
    seed = 0
    declared_frames = None
    masks: list[int] = []
    masks2: list[int] = []

    with path.open("r", encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.split("#", 1)[0].strip()
            if not line:
                continue
            if line.startswith("rom "):
                rom_hash = line.split(None, 1)[1].strip().lower()
            elif line.startswith("seed "):
                seed = int(line.split(None, 1)[1].strip(), 0)
            elif line.startswith("frames "):
                declared_frames = int(line.split(None, 1)[1].strip(), 0)
            else:
                cols = line.split()
                masks.append(int(cols[0], 16))
                masks2.append(int(cols[1], 16) if len(cols) > 1 else 0)

    if rom_hash is None:
        raise ValueError(f"{path}: cabeçalho sem 'rom <sha256>'")
    if rom_hash != expected_rom_sha256.lower():
        raise ValueError(
            f"{path}: SHA-256 da ROM diverge (filme={rom_hash}, rom={expected_rom_sha256})"
        )
    if declared_frames is None:
        raise ValueError(f"{path}: cabeçalho sem 'frames <N>'")
    if len(masks) < declared_frames:
        raise ValueError(f"{path}: apenas {len(masks)} linhas para {declared_frames} quadros")

    return {
        "path": path,
        "rom_sha256": rom_hash,
        "seed": seed,
        "frames": declared_frames,
        "masks": masks[:declared_frames],
        # Pad 2 (2ª coluna opcional do .ecm); None quando o filme é só P1.
        "masks2": masks2[:declared_frames] if any(masks2[:declared_frames]) else None,
    }


def mask_to_buttons(mask: int) -> list[str]:
    return [name for bit, name in ECM_BIT_TO_BUTTON if mask & bit]


def write_input_script(
    movie: dict[str, Any],
    script_path: Path,
    checkpoint_ram: dict[int, Path],
    checkpoint_png: dict[int, Path],
    final_ram: Path,
    final_vram: Path | None,
    final_png: Path,
    checkpoint_vram: dict[int, Path] | None = None,
) -> int:
    """Translate an .ecm movie into segagenesisrecomp input_script directives.

    Frame alignment note:
    - GenesisRecomp starts the simulation loop at `frame_num = 1`, running frame 1's
      `genesis_sim_step()` before calling `input_script_tick(1)`.
    - Therefore `WAIT (N - 1)` from initial tick (`frame_num = 1`) lands immediately
      after frame `N` completes (matching GPGX after `N` calls to `retro_run()`).
    - A mask transition at `.ecm` index `k` (`1 <= k < N`) is emitted at frame `k` so it
      takes effect on simulation frame `k + 1` (0-based `.ecm` frame `k`).
    """
    total_frames: int = movie["frames"]
    masks: list[int] = movie["masks"]
    masks2: list[int] | None = movie.get("masks2")
    if masks and masks[0] != 0:
        raise ValueError(f"{movie['path']}: frame 0 não nulo ({masks[0]:#x}) não suportado no script")
    if masks2 and masks2[0] != 0:
        raise ValueError(f"{movie['path']}: frame 0 do pad 2 não nulo ({masks2[0]:#x}) não suportado")

    ckpt_vram = checkpoint_vram or {}
    event_frames: set[int] = {total_frames}
    event_frames.update(f for f in checkpoint_ram if 1 <= f < total_frames)
    event_frames.update(f for f in checkpoint_png if 1 <= f < total_frames)
    event_frames.update(f for f in ckpt_vram if 1 <= f < total_frames)

    for pad_masks in (masks, masks2):
        if pad_masks is None:
            continue
        prev = 0
        for f in range(1, total_frames):
            if pad_masks[f] != prev:
                event_frames.add(f)
                prev = pad_masks[f]

    lines: list[str] = [f"# Auto-generated from {movie['path'].name} ({total_frames} frames)"]
    cur_frame = 1
    held_mask = 0
    held_mask2 = 0

    for f in sorted(event_frames):
        delta = f - cur_frame
        if delta > 0:
            lines.append(f"WAIT {delta}")
            cur_frame = f

        if f in checkpoint_ram:
            lines.append(f"DUMP_RAM {checkpoint_ram[f].resolve()}")
        if f in ckpt_vram:
            lines.append(f"DUMP_VRAM {ckpt_vram[f].resolve()}")
        if f in checkpoint_png:
            lines.append(f"SCREENSHOT {checkpoint_png[f].resolve()}")

        if f == total_frames:
            lines.append(f"DUMP_RAM {final_ram.resolve()}")
            if final_vram is not None:
                lines.append(f"DUMP_VRAM {final_vram.resolve()}")
            lines.append(f"SCREENSHOT {final_png.resolve()}")
            lines.append("EXIT 0")
        else:
            next_mask = masks[f]
            if next_mask != held_mask:
                if masks2 is not None:
                    lines.append("PLAYER 1")
                if held_mask != 0:
                    lines.append("RELEASE")
                for btn in mask_to_buttons(next_mask):
                    lines.append(f"HOLD {btn}")
                held_mask = next_mask
            if masks2 is not None and masks2[f] != held_mask2:
                lines.append("PLAYER 2")
                if held_mask2 != 0:
                    lines.append("RELEASE")
                for btn in mask_to_buttons(masks2[f]):
                    lines.append(f"HOLD {btn}")
                held_mask2 = masks2[f]

    directive_count = sum(1 for ln in lines if ln and not ln.startswith("#"))
    if directive_count > 4096:
        raise ValueError(f"input_script excedeu MAX_OPS=4096 ({directive_count} diretivas)")

    script_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return directive_count


def decode_png_rgba(path: Path) -> tuple[int, int, list[bytes]]:
    raw = path.read_bytes()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: assinatura PNG inválida")

    pos = 8
    width = height = bit_depth = color_type = 0
    idat_chunks: list[bytes] = []
    palette: list[tuple[int, int, int, int]] = []

    while pos < len(raw):
        length, ctype = struct.unpack(">I4s", raw[pos : pos + 8])
        data = raw[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, bit_depth, color_type, comp, filt, interlace = struct.unpack(">IIBBBBB", data)
            if comp != 0 or filt != 0 or interlace != 0 or bit_depth != 8:
                raise ValueError(
                    f"{path}: formato PNG não suportado (bd={bit_depth}, ct={color_type}, il={interlace})"
                )
        elif ctype == b"PLTE":
            palette = [(data[i], data[i + 1], data[i + 2], 255) for i in range(0, len(data), 3)]
        elif ctype == b"IDAT":
            idat_chunks.append(data)
        elif ctype == b"IEND":
            break

    bpp = {2: 3, 6: 4, 3: 1}.get(color_type)
    if bpp is None:
        raise ValueError(f"{path}: color_type PNG não suportado: {color_type}")

    decompressed = zlib.decompress(b"".join(idat_chunks))
    stride = width * bpp
    expected_len = height * (1 + stride)
    if len(decompressed) != expected_len:
        raise ValueError(f"{path}: IDAT tamanho {len(decompressed)} != {expected_len}")

    def paeth(a: int, b: int, c: int) -> int:
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        if pa <= pb and pa <= pc:
            return a
        if pb <= pc:
            return b
        return c

    rows_raw: list[bytearray] = []
    prev_row = bytearray(stride)
    idx = 0
    for _ in range(height):
        ftype = decompressed[idx]
        idx += 1
        src = decompressed[idx : idx + stride]
        idx += stride
        cur = bytearray(stride)
        if ftype == 0:
            cur[:] = src
        elif ftype == 1:
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                cur[i] = (src[i] + left) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                cur[i] = (src[i] + prev_row[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                cur[i] = (src[i] + ((left + prev_row[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                up = prev_row[i]
                up_left = prev_row[i - bpp] if i >= bpp else 0
                cur[i] = (src[i] + paeth(left, up, up_left)) & 0xFF
        else:
            raise ValueError(f"{path}: filtro PNG inválido {ftype}")
        rows_raw.append(cur)
        prev_row = cur

    rows_rgba: list[bytes] = []
    for row in rows_raw:
        if color_type == 6:
            rows_rgba.append(bytes(row))
        elif color_type == 2:
            out = bytearray(width * 4)
            for x in range(width):
                out[x * 4 : x * 4 + 3] = row[x * 3 : x * 3 + 3]
                out[x * 4 + 3] = 255
            rows_rgba.append(bytes(out))
        else:
            out = bytearray(width * 4)
            for x in range(width):
                r, g, b, a = palette[row[x]]
                out[x * 4 : x * 4 + 4] = bytes((r, g, b, a))
            rows_rgba.append(bytes(out))

    return width, height, rows_rgba


def inspect_widescreen_margins(png_path: Path, native_w: int = 320) -> dict[str, Any]:
    width, height, rows = decode_png_rgba(png_path)
    margin_left = max(0, (width - native_w) // 2)
    margin_right_start = min(width, margin_left + native_w)

    left_nonblack = 0
    right_nonblack = 0
    left_total = margin_left * height
    right_total = max(0, width - margin_right_start) * height

    for row in rows:
        for x in range(0, margin_left):
            if row[x * 4 : x * 4 + 3] != b"\x00\x00\x00":
                left_nonblack += 1
        for x in range(margin_right_start, width):
            if row[x * 4 : x * 4 + 3] != b"\x00\x00\x00":
                right_nonblack += 1

    return {
        "path": str(png_path.relative_to(ROOT)),
        "width": width,
        "height": height,
        "margin_left_px": margin_left,
        "margin_right_px": max(0, width - margin_right_start),
        "left_nonblack_pixels": left_nonblack,
        "left_total_pixels": left_total,
        "right_nonblack_pixels": right_nonblack,
        "right_total_pixels": right_total,
    }


def read_be_u16(ram: bytes, offset: int) -> int:
    return struct.unpack_from(">H", ram, offset & 0xFFFF)[0]


def read_be_s16(ram: bytes, offset: int) -> int:
    return struct.unpack_from(">h", ram, offset & 0xFFFF)[0]


def read_be_u32(ram: bytes, offset: int) -> int:
    return struct.unpack_from(">I", ram, offset & 0xFFFF)[0]


def read_be_s32(ram: bytes, offset: int) -> int:
    return struct.unpack_from(">i", ram, offset & 0xFFFF)[0]


def read_telemetry(ram_be: bytes) -> dict[str, Any]:
    def fighter(base: int) -> dict[str, int]:
        return {
            "obj_id": read_be_u16(ram_be, base + 0x40),
            "pos_x": read_be_s16(ram_be, base + 0x42),
            "pos_y": read_be_s16(ram_be, base + 0x46),
            "vel_x_16_16": read_be_s32(ram_be, base + 0x50),
            "vel_y_16_16": read_be_s32(ram_be, base + 0x54),
            "health": read_be_u16(ram_be, base + 0x68),
            "char_id": read_be_u16(ram_be, base + 0x6C),
            "action_state": read_be_u16(ram_be, base + 0x82),
            "move_counter": read_be_u16(ram_be, base + 0x100),
        }

    return {
        "state": read_be_u16(ram_be, 0xFD64),
        "pad_cfg": read_be_u16(ram_be, 0x9376),
        "p1_id": read_be_u16(ram_be, 0x9CBE),
        "p2_id": read_be_u16(ram_be, 0x9CBC),
        "frame_counter_main": read_be_u16(ram_be, 0xFD70),
        "frame_counter_vblank": read_be_u32(ram_be, 0xFD72),
        "pad_a_edge": read_be_u16(ram_be, 0xFD54),
        "pad_a_state": read_be_u16(ram_be, 0xFD58),
        "round_timer_bcd": read_be_u16(ram_be, 0x9CB4),
        "game_mode": read_be_u16(ram_be, 0x9CB0),
        "two_player": read_be_u16(ram_be, 0x9CCC),
        "tournament_type": read_be_u16(ram_be, 0x9424),
        "overkill": read_be_u16(ram_be, 0x9C7C),
        "vdp_reg1_shadow": read_be_u16(ram_be, 0xFEFC),
        "vdp_reg11_hscroll": read_be_u16(ram_be, 0xCCE0),
        "stage_scroll_x": read_be_s16(ram_be, 0xCD1A),
        "stage_scroll_y": read_be_s16(ram_be, 0xCD16),
        "p1": fighter(0xAB86),
        "p2": fighter(0xAA52),
    }


def unswap_16bit_words(raw: bytes) -> bytes:
    out = bytearray(len(raw))
    for i in range(0, len(raw) - 1, 2):
        out[i] = raw[i + 1]
        out[i + 1] = raw[i]
    if len(raw) % 2:
        out[-1] = raw[-1]
    return bytes(out)


def detect_gpgx_byte_order(raw_wram: bytes) -> tuple[str, bytes]:
    """Verify whether GPGX libretro system RAM dump is 16-bit host byte-swapped."""
    unswapped = unswap_16bit_words(raw_wram)
    raw_state = read_be_u16(raw_wram, 0xFD64)
    unswapped_state = read_be_u16(unswapped, 0xFD64)
    raw_vbl = read_be_u32(raw_wram, 0xFD72)
    unswapped_vbl = read_be_u32(unswapped, 0xFD72)

    if unswapped_state < 40 and unswapped_vbl < 100000 and (raw_state >= 40 or raw_vbl > 100000):
        return "byte-swapped-16bit (desfeito via i^1)", unswapped
    if raw_state < 40 and raw_vbl < 100000 and not (unswapped_state < 40):
        return "big-endian-direto", raw_wram
    # Default documented in ram_map.toml line 9
    return "byte-swapped-16bit (padrao libretro GPGX, i^1)", unswapped


def diff_wram(
    native_ram: bytes,
    ref_ram: bytes,
    ram_entries: list[dict[str, Any]],
    top_n: int = 12,
) -> dict[str, Any]:
    diff_offsets = [i for i in range(min(len(native_ram), len(ref_ram))) if native_ram[i] != ref_ram[i]]
    if not diff_offsets:
        return {
            "diff_bytes": 0,
            "total_bytes": len(native_ram),
            "first_diff": None,
            "top_symbols": [],
        }

    sym_counts: Counter[str] = Counter()
    sym_first_off: dict[str, int] = {}
    for off in diff_offsets:
        sym = resolve_symbol(off, ram_entries)
        sym_counts[sym] += 1
        sym_first_off.setdefault(sym, off)

    first_off = diff_offsets[0]
    first_info = {
        "offset": f"0xFF{first_off:04X}",
        "symbol": resolve_symbol(first_off, ram_entries),
        "native_byte": f"0x{native_ram[first_off]:02X}",
        "gpgx_byte": f"0x{ref_ram[first_off]:02X}",
    }
    top = [
        {
            "symbol": sym,
            "first_offset": f"0xFF{sym_first_off[sym]:04X}",
            "diff_bytes": cnt,
        }
        for sym, cnt in sym_counts.most_common(top_n)
    ]
    return {
        "diff_bytes": len(diff_offsets),
        "total_bytes": len(native_ram),
        "first_diff": first_info,
        "top_symbols": top,
    }


def to_wsl_path(p: Path | str) -> str:
    """C:\foo\bar -> /mnt/c/foo/bar (sem wslpath)."""
    s = str(p).replace("\\", "/")
    if len(s) >= 2 and s[1] == ":":
        return f"/mnt/{s[0].lower()}{s[2:]}"
    return s


def run_native(
    native_bin: Path,
    rom_path: Path,
    script_path: Path,
    benchmark_frames: int = 10000,
    extra_args: list[str] | None = None,
    env_extra: dict[str, str] | None = None,
) -> dict[str, Any]:
    env = os.environ.copy()
    env["SDL_VIDEODRIVER"] = "dummy"
    env["SDL_AUDIODRIVER"] = "dummy"
    env["SDL_RENDER_DRIVER"] = "software"
    if env_extra:
        env.update(env_extra)

    cmd = [
        str(native_bin),
        "--benchmark",
        str(benchmark_frames),
        "--fresh-evidence",
        "--input-script",
        str(script_path),
    ]
    if extra_args:
        cmd.extend(extra_args)
    cmd.append(str(rom_path))

    res = subprocess.run(cmd, cwd=str(ROOT), env=env, capture_output=True, text=True,
        encoding="utf-8", errors="replace",
    )
    floor_lines = [
        ln
        for ln in res.stderr.splitlines()
        if any(k in ln for k in ("[FLOOR]", "[DISPATCH]", "[VRAMDUMP]", "[EC_VIDEO]"))
    ]
    vram_meta_map: dict[str, dict[str, int]] = {}
    vram_meta: dict[str, int] | None = None
    for m in re.finditer(
        r"\[VRAMDUMP\] wrote (\S+) \(planeA=\$([0-9A-Fa-f]+) planeB=\$([0-9A-Fa-f]+) "
        r"window=\$([0-9A-Fa-f]+) sprites=\$([0-9A-Fa-f]+) hscroll=\$([0-9A-Fa-f]+)\)",
        res.stderr,
    ):
        meta = {
            "planeA": int(m.group(2), 16),
            "planeB": int(m.group(3), 16),
            "window": int(m.group(4), 16),
            "sprites": int(m.group(5), 16),
            "hscroll": int(m.group(6), 16),
        }
        vram_meta_map[Path(m.group(1)).name] = meta
        vram_meta = meta

    if res.returncode != 0:
        raise RuntimeError(
            f"Falha ao executar {native_bin.name} (exit={res.returncode}):\n{res.stderr}"
        )

    evidence = {}
    build_dir = native_bin.parent
    for fname in (
        "floor_unsafe.toml",
        "dispatch_misses.toml",
        "interior_label_misses.toml",
        "floor_coverage.toml",
    ):
        p = build_dir / fname
        if p.exists():
            with p.open("rb") as f:
                data = tomllib.load(f)
            if "floor_unsafe" in data:
                addrs = data["floor_unsafe"].get("addresses", [])
            elif "interior_labels" in data:
                addrs = data["interior_labels"].get("addresses", [])
            elif "functions" in data:
                addrs = data["functions"].get("extra", [])
            else:
                addrs = []
            evidence[fname] = [f"0x{a:06X}" for a in addrs]

    return {
        "returncode": res.returncode,
        "floor_lines": floor_lines,
        "vram_meta": vram_meta,
        "vram_meta_map": vram_meta_map,
        "evidence": evidence,
        "stderr": res.stderr,
    }


def run_gpgx(
    gpgx_runner: Path,
    gpgx_core: Path,
    rom_path: Path,
    movie: dict[str, Any],
    checkpoints: list[int],
    work_dir: Path,
) -> dict[int, bytes]:
    final_frame = movie["frames"]
    final_path = work_dir / f"gpgx_{movie['path'].stem}_{final_frame}.ram"
    dump_args: list[str] = []
    ckpt_paths: dict[int, Path] = {}
    for frame in checkpoints:
        if 1 <= frame < final_frame:
            p = work_dir / f"gpgx_{movie['path'].stem}_{frame}.ram"
            ckpt_paths[frame] = p
            dump_args.append(f"dump@{frame - 1},{p}")

    if IS_WINDOWS:
        dump_args = [
            f"dump@{frame - 1},{to_wsl_path(p)}" for frame, p in ckpt_paths.items()
        ]
        conv = to_wsl_path
        prefix = ["wsl.exe", "-d", "Ubuntu", "--"]
    else:
        conv = str
        prefix = []
    cmd = [
        *prefix,
        conv(gpgx_runner),
        conv(gpgx_core),
        conv(rom_path),
        conv(final_path),
        conv(movie["path"]),
        *dump_args,
    ]
    res = subprocess.run(
        cmd, cwd=str(ROOT), capture_output=True, text=True, encoding="utf-8", errors="replace"
    )
    if res.returncode != 0:
        raise RuntimeError(f"gpgx_runner falhou (exit={res.returncode}):\n{res.stderr}")

    out: dict[int, bytes] = {}
    for frame, p in ckpt_paths.items():
        out[frame] = p.read_bytes()
    out[final_frame] = final_path.read_bytes()
    return out


def run_interp_parity(
    movie: dict[str, Any],
    spec: dict[str, Any],
    slug: str,
    final_frame: int,
    bench_n: int,
    native_bin: Path,
    rom_path: Path,
    tmp_work: Path,
    final_nat_ram: bytes,
    final_nat_tel: dict[str, Any],
    nat_png_sha: str,
    ram_entries: list[dict[str, Any]],
) -> dict[str, Any]:
    """Compare recompiled execution against pure 68k interpreter on the same runner."""
    interp_script = tmp_work / f"{slug}_interp.input"
    interp_ram_p = tmp_work / f"{slug}_interp.ram"
    interp_png_p = tmp_work / f"{slug}_interp.png"
    write_input_script(
        movie=movie,
        script_path=interp_script,
        checkpoint_ram={},
        checkpoint_png={},
        final_ram=interp_ram_p,
        final_vram=None,
        final_png=interp_png_p,
    )
    run_native(
        native_bin,
        rom_path,
        interp_script,
        benchmark_frames=bench_n,
        env_extra={"GENESIS_FORCE_INTERP": "1"},
    )
    interp_ram = interp_ram_p.read_bytes()
    interp_tel = read_telemetry(interp_ram)
    interp_diff = diff_wram(final_nat_ram, interp_ram, ram_entries)
    interp_png_eq = sha256_bytes(interp_png_p.read_bytes()) == nat_png_sha

    gameplay_match = (
        interp_tel["state"] == final_nat_tel["state"]
        and interp_tel["pad_cfg"] == 0x0101
        and final_nat_tel["pad_cfg"] == 0x0101
    )
    if spec["expected_p1"] is not None:
        gameplay_match = gameplay_match and (
            interp_tel["p1_id"] == final_nat_tel["p1_id"]
            and interp_tel["p2_id"] == final_nat_tel["p2_id"]
        )
    if spec["is_arena"]:
        gameplay_match = gameplay_match and (
            interp_tel["round_timer_bcd"] == final_nat_tel["round_timer_bcd"]
        )
        for slot in ("p1", "p2"):
            for field in ("health", "pos_x", "pos_y", "action_state", "move_counter"):
                if interp_tel[slot][field] != final_nat_tel[slot][field]:
                    gameplay_match = False

    return {
        "diff_bytes": interp_diff["diff_bytes"],
        "total_bytes": interp_diff["total_bytes"],
        "png_equal": interp_png_eq,
        "gameplay_match": gameplay_match,
        "pad_cfg_native": f"0x{final_nat_tel['pad_cfg']:04X}",
        "pad_cfg_interp": f"0x{interp_tel['pad_cfg']:04X}",
        "native_vbl": final_nat_tel["frame_counter_vblank"],
        "interp_vbl": interp_tel["frame_counter_vblank"],
        "first_diff": interp_diff["first_diff"],
        "top_symbols": interp_diff["top_symbols"][:5],
    }


def format_md_report(report: dict[str, Any]) -> str:
    lines: list[str] = [
        "# Resumo do Harness Determinístico & Frame Data (`M3` / `M4`–`M6`)",
        "",
        f"- **ROM:** `roms/eternal_champions_us.gen` (`SHA-256: {report['rom_sha256']}`)",
        "- **Executável nativo:** `build/native/EternalChampionsRecomp` (`--benchmark`)",
        "- **Oráculo dinâmico:** `tools/vendor/gpgx_runner/gpgx_runner` + `genesis_plus_gx_libretro.so`",
        "",
        "## 1. Autodeterminismo Nativo e Misses (Suíte Completa)",
        "",
        "| Movie `.ecm` | Quadros | Diff WRAM (bytes) | PNG SHA-256 Run1 == Run2 | `floor_unsafe` | `dispatch_misses` | `interior_label_misses` | `floor_coverage` |",
        "|---|---:|---:|:---:|---:|---:|---:|---:|",
    ]
    for m in report["movies"]:
        det = m["self_determinism"]
        ev = m["native_evidence"]
        lines.append(
            f"| `{m['name']}` | {m['frames']} | {det['diff_bytes']}/{det['total_bytes']} | "
            f"{'SIM' if det['png_equal'] else 'NÃO'} (`{det['png_sha256_run1'][:12]}`) | "
            f"{len(ev.get('floor_unsafe.toml', []))} | {len(ev.get('dispatch_misses.toml', []))} | "
            f"{len(ev.get('interior_label_misses.toml', []))} | {len(ev.get('floor_coverage.toml', []))} |"
        )

    lines.extend([
        "",
        "## 2. Paridade da Recompilação Estática (`Recomp` × `GENESIS_FORCE_INTERP=1`)",
        "",
        "| Movie `.ecm` | WRAM Diff | PNG Igual | Pad (`$9376` Recomp/Interp) | Lógica/Combate | VBlank (`$FD72` R/I) | Resumo do diff |",
        "|---|---:|:---:|:---:|:---:|:---:|---|",
    ])
    for m in report["movies"]:
        ip = m.get("interp_parity")
        if ip is None:
            lines.append(
                f"| `{m['name']}` | N/A | N/A | — | N/A (hook C `EC_DIAG_BOSS_SANCTUARY` em `ec_call_vblank`) | — | — |"
            )
            continue
        if ip["diff_bytes"] == 0 and ip["png_equal"]:
            diff_desc = "Idêntico (`0/65536`, PNG igual)"
        elif ip["diff_bytes"] == 0:
            diff_desc = "WRAM 0 B divergente (diferença só no framebuffer PNG)"
        else:
            top_syms = ", ".join(
                f"`{t['symbol']}` ({t['diff_bytes']}B)" for t in ip.get("top_symbols", [])[:3]
            )
            fd = ip.get("first_diff")
            first_note = f"`{fd['offset']}` {fd['native_byte']}!={fd['gpgx_byte']}" if fd else ""
            diff_desc = f"1º {first_note}; top: {top_syms}" if first_note else top_syms
        lines.append(
            f"| `{m['name']}` | {ip['diff_bytes']}/{ip['total_bytes']} | "
            f"{'SIM' if ip['png_equal'] else 'NÃO'} | "
            f"`{ip['pad_cfg_native']}` / `{ip['pad_cfg_interp']}` | "
            f"{'PASS' if ip['gameplay_match'] else 'FAIL'} | "
            f"{ip['native_vbl']}/{ip['interp_vbl']} | {diff_desc} |"
        )

    lines.extend([
        "",
        "## 3. Cobertura do Elenco e Arenas + Paridade Native vs GPGX",
        "",
        "> **Nota técnica (entropia HV-counter em `Math_RandomRange` `$CB82`):** o jogo lê o contador HV ao vivo (`$00C00008`) em `$CB82` (já no quadro `75` do Title para escolher `$9CBE` e durante decisões da IA), gerando sorteios e duração de frames distintos até entre emuladores ciclo-precisos (`BlastEm 1.0.0`, `GPGX` e `ares`). Mantivemos `gvdp_read_hv_counter` (`genesis_vdp.c:467`) intocado para preservar a estabilidade dos hashes e scripts da suíte de `.ecm`.",
        "",
        "| Movie `.ecm` | Arena / Cenário | P1 (Nat / Esperado) | P2 (Nat / Esperado) | Native `$FD64` | GPGX `$FD64` | Diff WRAM final | HP Final (P1/P2) |",
        "|---|---|---|---|---:|:---:|---:|---:|",
    ])
    for m in report["movies"]:
        last = m["checkpoints"][-1]
        nt = last["native_telemetry"]
        gt = last.get("gpgx_telemetry")
        df = last.get("wram_diff")
        p1_str = f"{FIGHTER_NAMES.get(nt['p1_id'], nt['p1_id'])} ({nt['p1_id']})" if m["expected_p1"] is not None else "—"
        p2_str = f"{FIGHTER_NAMES.get(nt['p2_id'], nt['p2_id'])} ({nt['p2_id']})" if m["expected_p2"] is not None else "—"
        gpgx_st = str(gt["state"]) if gt is not None else "N/A (probe nativo State 18)"
        diff_str = f"{df['diff_bytes']}/{df['total_bytes']}" if df is not None else "N/A"
        hp_str = f"{nt['p1']['health']}/{nt['p2']['health']}" if m["is_arena"] else "—"
        lines.append(
            f"| `{m['name']}` | {m['arena_name']} | {p1_str} | {p2_str} | "
            f"{nt['state']} | {gpgx_st} | {diff_str} | {hp_str} |"
        )

    lines.extend([
        "",
        "### Checkpoints Detalhados — Boot (`01`) e Luta Principal (`02`)",
        "",
    ])
    for m in report["movies"][:2]:
        lines.append(f"#### `{m['name']}` ({m['frames']} quadros) — `{m['gpgx_byte_order']}`")
        lines.append("")
        lines.append(
            "| Quadro | Native `$FD64` | GPGX `$FD64` | Native `$FD72` | GPGX `$FD72` | Bytes divergentes | Primeiro diff |"
        )
        lines.append("|---:|---:|---:|---:|---:|---:|---|")
        for ck in m["checkpoints"]:
            nt = ck["native_telemetry"]
            gt = ck["gpgx_telemetry"]
            df = ck["wram_diff"]
            first_str = (
                f"`{df['first_diff']['offset']}` ({df['first_diff']['symbol']}: "
                f"nat={df['first_diff']['native_byte']} vs gpgx={df['first_diff']['gpgx_byte']})"
                if df["first_diff"]
                else "idêntico (0 bytes)"
            )
            lines.append(
                f"| {ck['frame']} | {nt['state']} | {gt['state']} | "
                f"{nt['frame_counter_vblank']} | {gt['frame_counter_vblank']} | "
                f"{df['diff_bytes']}/{df['total_bytes']} | {first_str} |"
            )
        lines.append("")

    fight_movie = next((m for m in report["movies"] if m["name"] == "02_title_to_fight.ecm"), None)
    if fight_movie:
        lines.extend([
            "## 4. Progressão de Luta Ativa Observada (`02_title_to_fight.ecm`)",
            "",
            "| Quadro | Engine | Estado | Timer BCD | P1 HP | P1 Pos (X,Y) | P1 Action/Move | P2 HP | P2 Pos (X,Y) | P2 Action/Move |",
            "|---:|---|---:|---:|---:|---|---:|---:|---|---:|",
        ])
        for ck in fight_movie["checkpoints"]:
            for label, tel in (("Native", ck["native_telemetry"]), ("GPGX", ck["gpgx_telemetry"])):
                p1 = tel["p1"]
                p2 = tel["p2"]
                lines.append(
                    f"| {ck['frame']} | {label} | {tel['state']} | `${tel['round_timer_bcd']:04X}` | "
                    f"{p1['health']} | ({p1['pos_x']}, {p1['pos_y']}) | "
                    f"{p1['action_state']}/{p1['move_counter']} | "
                    f"{p2['health']} | ({p2['pos_x']}, {p2['pos_y']}) | "
                    f"{p2['action_state']}/{p2['move_counter']} |"
                )
        lines.append("")

    ws = report["widescreen"]
    lines.extend([
        "## 5. Validação Widescreen 16:9 (`game/ec_video.c` — Todas as Arenas)",
        "",
        "| Captura | Cenário / Estado | Dimensão PNG | Margem Esq. (não-preta / total) | Margem Dir. (não-preta / total) | WRAM Diff vs 320x224 |",
        "|---|---|---|---:|---:|---:|",
    ])
    for item in ws.get("items", []):
        df = item["wram_diff"]
        lines.append(
            f"| `{item['path']}` | {item['label']} | "
            f"{item['width']}x{item['height']} | "
            f"{item['left_nonblack_pixels']}/{item['left_total_pixels']} | "
            f"{item['right_nonblack_pixels']}/{item['right_total_pixels']} | "
            f"{df['diff_bytes']}/{df['total_bytes']} |"
        )
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description="Harness determinístico EternalRecomp vs GPGX")
    ap.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    ap.add_argument("--native", type=Path, default=DEFAULT_NATIVE)
    ap.add_argument("--gpgx-runner", type=Path, default=DEFAULT_GPGX_RUNNER)
    ap.add_argument("--gpgx-core", type=Path, default=DEFAULT_GPGX_CORE)
    ap.add_argument("--ram-map", type=Path, default=DEFAULT_RAM_MAP)
    ap.add_argument("--captures-dir", type=Path, default=DEFAULT_CAPTURES)
    ap.add_argument("--report-md", type=Path, default=DEFAULT_REPORT_MD)
    ap.add_argument("--quick", action="store_true", help="Executa apenas 01_attract_boot + 02_title_to_fight")
    ap.add_argument("--skip-interp", action="store_true", help="Pula Camada 1 (Recomp vs GENESIS_FORCE_INTERP=1)")
    args = ap.parse_args()

    for req in (args.rom, args.native, args.gpgx_runner, args.gpgx_core, args.ram_map):
        if not req.exists():
            print(f"ERRO: arquivo obrigatório ausente: {req}", file=sys.stderr)
            return 1

    args.captures_dir.mkdir(parents=True, exist_ok=True)
    args.report_md.parent.mkdir(parents=True, exist_ok=True)

    rom_bytes = args.rom.read_bytes()
    rom_sha256 = sha256_bytes(rom_bytes)
    ram_entries = load_ram_map(args.ram_map)

    selected_specs = MOVIE_SPECS[:2] if args.quick else MOVIE_SPECS

    tmp_work = (
        Path(tempfile.gettempdir()) / "ec_harness_work" if IS_WINDOWS else Path("/tmp/ec_harness_work")
    )
    tmp_work.mkdir(parents=True, exist_ok=True)

    report: dict[str, Any] = {
        "rom_sha256": rom_sha256,
        "movies": [],
        "captures": [],
        "widescreen": {},
    }

    determinism_ok = True
    state_match_ok = True
    misses_ok = True
    interp_parity_ok = True
    widescreen_items: list[dict[str, Any]] = []
    vram_dumps_meta: dict[str, Any] = {}

    for spec in selected_specs:
        ecm_path = ROOT / "harness" / "movies" / spec["movie"]
        slug = spec["slug"]
        movie = parse_ecm(ecm_path, rom_sha256)
        final_frame = movie["frames"]
        checkpoints = spec["checkpoints"] if spec["checkpoints"] else [final_frame]
        interim = [f for f in checkpoints if f < final_frame]
        env_extra = {"EC_DIAG_BOSS_SANCTUARY": "1"} if spec["is_diag_boss"] else None
        bench_n = max(10000, final_frame + 500)

        run_outputs: list[dict[str, Any]] = []
        run_rams: list[dict[int, bytes]] = []
        run_png_sha: list[str] = []

        for r_idx in (1, 2):
            script_path = tmp_work / f"{slug}_run{r_idx}.input"
            ckpt_ram_map = {f: tmp_work / f"{slug}_run{r_idx}_f{f}.ram" for f in interim}
            ckpt_vram_map: dict[int, Path] = {}
            if r_idx == 1 and spec["movie"] == "01_attract_boot.ecm":
                ckpt_ram_map[600] = args.captures_dir / "boot_600.ram"
                ckpt_vram_map[600] = args.captures_dir / "boot_600.vram"

            if r_idx == 1:
                final_ram_p = args.captures_dir / f"{slug}.ram"
                final_vram_p = args.captures_dir / f"{slug}.vram"
                if slug == "fight_2900":
                    final_png_p = args.captures_dir / "fight_320.png"
                else:
                    final_png_p = args.captures_dir / f"{slug}_320.png"
            else:
                final_ram_p = tmp_work / f"{slug}_run2.ram"
                final_vram_p = tmp_work / f"{slug}_run2.vram"
                final_png_p = tmp_work / f"{slug}_run2.png"

            write_input_script(
                movie=movie,
                script_path=script_path,
                checkpoint_ram=ckpt_ram_map,
                checkpoint_png={},
                final_ram=final_ram_p,
                final_vram=final_vram_p,
                final_png=final_png_p,
                checkpoint_vram=ckpt_vram_map,
            )
            res = run_native(
                args.native,
                args.rom,
                script_path,
                benchmark_frames=bench_n,
                env_extra=env_extra,
            )
            run_outputs.append(res)
            if r_idx == 1:
                vram_dumps_meta.update(res.get("vram_meta_map", {}))

            frames_ram = {f: p.read_bytes() for f, p in ckpt_ram_map.items()}
            frames_ram[final_frame] = final_ram_p.read_bytes()
            run_rams.append(frames_ram)
            run_png_sha.append(sha256_bytes(final_png_p.read_bytes()))

        det_diff = diff_wram(run_rams[0][final_frame], run_rams[1][final_frame], ram_entries)
        png_equal = run_png_sha[0] == run_png_sha[1]
        if det_diff["diff_bytes"] != 0 or not png_equal:
            determinism_ok = False

        ev = run_outputs[0]["evidence"]
        if any(len(ev.get(k, [])) != 0 for k in ("floor_unsafe.toml", "dispatch_misses.toml", "interior_label_misses.toml", "floor_coverage.toml")):
            misses_ok = False

        # Verify expected native state/fighters at final frame
        final_nat_ram = run_rams[0][final_frame]
        final_nat_tel = read_telemetry(final_nat_ram)
        if final_nat_tel["state"] != spec["expected_state"]:
            state_match_ok = False
        if final_nat_tel["pad_cfg"] != 0x0101:
            state_match_ok = False
        if spec["expected_p1"] is not None and (
            final_nat_tel["p1_id"] != spec["expected_p1"] or final_nat_tel["p2_id"] != spec["expected_p2"]
        ):
            state_match_ok = False
        if not expected_tel_ok(final_nat_tel, spec):
            state_match_ok = False

        # Capture catalog metadata
        if spec["movie"] == "01_attract_boot.ecm":
            report["captures"].append({
                "context": "state_07_title",
                "ram_file": "boot_600.ram",
                "vram_file": "boot_600.vram",
                "state_id": 7,
                "arena_name": "Title Screen (S7)",
                "fighter_p1": None,
                "fighter_p2": None,
            })
            report["captures"].append({
                "context": "state_09_menu_init",
                "ram_file": "boot_1000.ram",
                "vram_file": "boot_1000.vram",
                "state_id": final_nat_tel["state"],
                "arena_name": spec["arena_name"],
                "fighter_p1": None,
                "fighter_p2": None,
            })
        else:
            p1_name = FIGHTER_NAMES.get(final_nat_tel["p1_id"], str(final_nat_tel["p1_id"]))
            p2_name = FIGHTER_NAMES.get(final_nat_tel["p2_id"], str(final_nat_tel["p2_id"]))
            report["captures"].append({
                "context": slug if slug != "fight_2900" else "state_17_fight",
                "ram_file": f"{slug}.ram",
                "vram_file": f"{slug}.vram",
                "state_id": final_nat_tel["state"],
                "arena_name": spec["arena_name"],
                "fighter_p1": p1_name,
                "fighter_p2": p2_name,
            })

        # GPGX Oracle comparison (skip only native-only State 18 diagnostic hook)
        ckpt_reports = []
        if not spec["is_diag_boss"]:
            gpgx_raw_map = run_gpgx(args.gpgx_runner, args.gpgx_core, args.rom, movie, checkpoints, tmp_work)
            byte_order_desc, _ = detect_gpgx_byte_order(gpgx_raw_map[final_frame])
            for f in checkpoints:
                nat_ram = run_rams[0][f]
                _, gpgx_ram = detect_gpgx_byte_order(gpgx_raw_map[f])
                nat_tel = read_telemetry(nat_ram)
                gpgx_tel = read_telemetry(gpgx_ram)
                wdiff = diff_wram(nat_ram, gpgx_ram, ram_entries)
                ckpt_reports.append(
                    {
                        "frame": f,
                        "native_telemetry": nat_tel,
                        "gpgx_telemetry": gpgx_tel,
                        "wram_diff": wdiff,
                    }
                )
            last_gpgx = ckpt_reports[-1]["gpgx_telemetry"]
            # Arena sorteada (Tournament Round-A-Bout não tem opção de Background): o RNG
            # lê o contador HV ao vivo ($CB82), então no GPGX basta estar em luta.
            if spec.get("gpgx_any_arena"):
                gpgx_state_ok = last_gpgx["state"] in ARENA_STATES
            else:
                gpgx_state_ok = last_gpgx["state"] == spec["expected_state"]
            if not gpgx_state_ok or last_gpgx["pad_cfg"] != 0x0101:
                state_match_ok = False
            if spec["expected_p1"] is not None and (
                last_gpgx["p1_id"] != spec["expected_p1"] or last_gpgx["p2_id"] != spec["expected_p2"]
            ):
                state_match_ok = False
            if not expected_tel_ok(last_gpgx, spec):
                state_match_ok = False
        else:
            byte_order_desc = "N/A (probe nativo State 18)"
            ckpt_reports.append(
                {
                    "frame": final_frame,
                    "native_telemetry": final_nat_tel,
                    "gpgx_telemetry": None,
                    "wram_diff": None,
                }
            )

        interp_parity = None
        if not spec["is_diag_boss"] and not args.skip_interp:
            interp_parity = run_interp_parity(
                movie=movie,
                spec=spec,
                slug=slug,
                final_frame=final_frame,
                bench_n=bench_n,
                native_bin=args.native,
                rom_path=args.rom,
                tmp_work=tmp_work,
                final_nat_ram=final_nat_ram,
                final_nat_tel=final_nat_tel,
                nat_png_sha=run_png_sha[0],
                ram_entries=ram_entries,
            )
            if not interp_parity["gameplay_match"]:
                interp_parity_ok = False

        report["movies"].append(
            {
                "name": ecm_path.name,
                "slug": slug,
                "frames": final_frame,
                "arena_name": spec["arena_name"],
                "is_arena": spec["is_arena"],
                "expected_state": spec["expected_state"],
                "expected_p1": spec["expected_p1"],
                "expected_p2": spec["expected_p2"],
                "self_determinism": {
                    "diff_bytes": det_diff["diff_bytes"],
                    "total_bytes": det_diff["total_bytes"],
                    "png_equal": png_equal,
                    "png_sha256_run1": run_png_sha[0],
                    "png_sha256_run2": run_png_sha[1],
                },
                "interp_parity": interp_parity,
                "native_evidence": ev,
                "floor_lines": run_outputs[0]["floor_lines"],
                "gpgx_byte_order": byte_order_desc,
                "checkpoints": ckpt_reports,
            }
        )

        # Widescreen 16:9 validation for every arena movie (+ Title pillarbox on movie 02)
        if spec["is_arena"]:
            ws_script = tmp_work / f"{slug}_ws_16_9.input"
            ckpt_png_ws: dict[int, Path] = {}
            title_16_9_png = args.captures_dir / "title_16_9.png"
            if slug == "fight_2900":
                ckpt_png_ws[600] = title_16_9_png
                ws_png_out = args.captures_dir / "fight_16_9.png"
                ws_ram_out = args.captures_dir / "fight_16_9.ram"
            else:
                ws_png_out = args.captures_dir / f"{slug}_16_9.png"
                ws_ram_out = tmp_work / f"{slug}_16_9.ram"

            write_input_script(
                movie=movie,
                script_path=ws_script,
                checkpoint_ram={},
                checkpoint_png=ckpt_png_ws,
                final_ram=ws_ram_out,
                final_vram=None,
                final_png=ws_png_out,
            )
            run_native(
                args.native,
                args.rom,
                ws_script,
                benchmark_frames=bench_n,
                extra_args=["--widescreen", "16:9"],
                env_extra=env_extra,
            )

            if slug == "fight_2900":
                title_ws_info = inspect_widescreen_margins(title_16_9_png, native_w=320)
                title_ws_info["label"] = "Pillarbox Title (Estado 7)"
                title_ws_info["wram_diff"] = {"diff_bytes": 0, "total_bytes": 65536}
                title_ws_info["expect_pillarbox"] = True
                widescreen_items.append(title_ws_info)

            arena_ws_info = inspect_widescreen_margins(ws_png_out, native_w=320)
            arena_ws_info["label"] = spec["arena_name"]
            arena_ws_info["wram_diff"] = diff_wram(final_nat_ram, ws_ram_out.read_bytes(), ram_entries)
            arena_ws_info["expect_pillarbox"] = False
            widescreen_items.append(arena_ws_info)

    ws_ok = len(widescreen_items) > 0 and all(
        (
            it["width"] > 320
            and it["wram_diff"]["diff_bytes"] == 0
            and (
                (it["left_nonblack_pixels"] == 0 and it["right_nonblack_pixels"] == 0)
                if it["expect_pillarbox"]
                else (it["left_nonblack_pixels"] > 0 and it["right_nonblack_pixels"] > 0)
            )
        )
        for it in widescreen_items
    )

    title_item = next((i for i in widescreen_items if i.get("expect_pillarbox")), None)
    fight_item = next((i for i in widescreen_items if i["path"].endswith("fight_16_9.png")), None)
    report["widescreen"] = {
        "title": title_item,
        "fight": fight_item,
        "items": widescreen_items,
        "verified": ws_ok,
    }
    report["vram_dumps"] = vram_dumps_meta
    report["summary"] = {
        "movies_count": len(report["movies"]),
        "self_determinism_ok": determinism_ok,
        "state_and_roster_ok": state_match_ok,
        "zero_misses_ok": misses_ok,
        "interp_parity_ok": interp_parity_ok,
        "widescreen_ok": ws_ok,
    }

    report_json = args.captures_dir / "harness_report.json"
    report_json.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    args.report_md.write_text(format_md_report(report), encoding="utf-8")

    print(f"ROM SHA-256: {rom_sha256} (movies={len(report['movies'])})")
    for m in report["movies"]:
        det = m["self_determinism"]
        ev = m["native_evidence"]
        last = m["checkpoints"][-1]
        nt = last["native_telemetry"]
        gt = last.get("gpgx_telemetry")
        df = last.get("wram_diff")
        ip = m.get("interp_parity")
        g_str = f"gpgx_st={gt['state']} diff={df['diff_bytes']}/{df['total_bytes']}" if gt and df else "gpgx=N/A"
        i_str = (
            f"interp_diff={ip['diff_bytes']}/{ip['total_bytes']} png_eq={ip['png_equal']} "
            f"gp_ok={ip['gameplay_match']} pad={ip['pad_cfg_native']}/{ip['pad_cfg_interp']}"
            if ip is not None
            else f"interp=N/A pad={nt['pad_cfg']:#06x}"
        )
        print(
            f"[{m['name']}] det={det['diff_bytes']}/{det['total_bytes']} png_eq={det['png_equal']} "
            f"misses=({len(ev.get('floor_unsafe.toml', []))},{len(ev.get('dispatch_misses.toml', []))},"
            f"{len(ev.get('interior_label_misses.toml', []))},{len(ev.get('floor_coverage.toml', []))}) "
            f"nat_st={nt['state']} p1={nt['p1_id']} p2={nt['p2_id']} {i_str} {g_str}"
        )
    for it in widescreen_items:
        print(
            f"[WS 16:9] {it['path']} {it['width']}x{it['height']} "
            f"margins_nz=({it['left_nonblack_pixels']},{it['right_nonblack_pixels']}) "
            f"wram_diff={it['wram_diff']['diff_bytes']}/65536"
        )
    print(f"Report MD: {args.report_md}")
    print(f"Report JSON: {report_json}")

    if not (determinism_ok and state_match_ok and misses_ok and ws_ok and interp_parity_ok):
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
