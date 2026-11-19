#!/usr/bin/env python3
"""Phase 5 Asset Extractor for Eternal Champions (Genesis VRAM/CRAM -> Catalog + Manifest).

Pure Python stdlib implementation:
- Decodes Sega Genesis CRAM shadow palettes ($FFFF9BB0..$FFFF9C2F) using DAC levels.
- Walks VDP Sprite Attribute Table (SAT) linked list in column-major Genesis order.
- Extracts Plane A & Plane B 8x8 tiles and full 512x256 layer PNGs.
- Deduplicates sprites, tiles, and planes by SHA-256 into assets/catalog/ (gitignored).
- Writes pipeline/manifest.json with status="extracted" (Mode C fallback-safe).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import zlib
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_ROM = ROOT / "roms" / "eternal_champions_us.gen"
DEFAULT_CAPTURES = ROOT / "assets" / "captures"
DEFAULT_CATALOG = ROOT / "assets" / "catalog"
DEFAULT_MANIFEST = ROOT / "pipeline" / "manifest.json"

# Official Sega Genesis VDP DAC levels (3-bit channel -> 8-bit luminance)
GENESIS_DAC = (0, 52, 87, 116, 144, 172, 206, 255)

OBJ_TABLE_MASTER_ADDR = 0x06A522
OBJ_TABLE_MASTER_COUNT = 233
PALETTE_SHADOW_OFFSET = 0x9BB0


def encode_rgba_png(width: int, height: int, rgba_rows: list[bytes]) -> bytes:
    """Encode 8-bit RGBA image as PNG using only stdlib struct + zlib."""
    def chunk(ctype: bytes, payload: bytes) -> bytes:
        crc = zlib.crc32(ctype + payload) & 0xFFFFFFFF
        return struct.pack(">I", len(payload)) + ctype + payload + struct.pack(">I", crc)

    raw_scanlines = bytearray()
    row_len = width * 4
    for row in rgba_rows:
        if len(row) != row_len:
            raise ValueError(f"RGBA scanline length {len(row)} != {row_len}")
        raw_scanlines.append(0)  # Filter type 0 (None)
        raw_scanlines.extend(row)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    idat = zlib.compress(bytes(raw_scanlines), level=6)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


def encode_gray_png(width: int, height: int, rows: list[bytes]) -> bytes:
    """Encode 8-bit grayscale (color_type=0) PNG for palette index maps (values 0..15)."""
    def chunk(ctype: bytes, payload: bytes) -> bytes:
        crc = zlib.crc32(ctype + payload) & 0xFFFFFFFF
        return struct.pack(">I", len(payload)) + ctype + payload + struct.pack(">I", crc)

    raw_scanlines = bytearray()
    for row in rows:
        if len(row) != width:
            raise ValueError(f"Gray scanline length {len(row)} != {width}")
        raw_scanlines.append(0)
        raw_scanlines.extend(row)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    idat = zlib.compress(bytes(raw_scanlines), level=6)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


def decode_cram_shadow(ram: bytes) -> list[list[tuple[int, int, int, int]]]:
    """Decode 4 palettes x 16 RGBA entries from RAM shadow $FFFF9BB0..$FFFF9C2F."""
    palettes: list[list[tuple[int, int, int, int]]] = []
    for p in range(4):
        pal: list[tuple[int, int, int, int]] = []
        for c in range(16):
            idx = p * 16 + c
            word = struct.unpack_from(">H", ram, PALETTE_SHADOW_OFFSET + idx * 2)[0]
            r = GENESIS_DAC[(word >> 1) & 0x7]
            g = GENESIS_DAC[(word >> 5) & 0x7]
            b = GENESIS_DAC[(word >> 9) & 0x7]
            a = 0 if c == 0 else 255
            pal.append((r, g, b, a))
        palettes.append(pal)
    return palettes


def decode_4bpp_tile(vram: bytes, tile_id: int, hf: bool, vf: bool) -> bytearray:
    """Decode an 8x8 4bpp Genesis VRAM tile (32 bytes) into 64 palette nibble indices."""
    base = (tile_id & 0x7FF) * 32
    out = bytearray(64)
    if base + 32 > len(vram):
        return out
    for sy in range(8):
        dy = (7 - sy) if vf else sy
        row_off = base + sy * 4
        for sx in range(8):
            byte = vram[row_off + (sx >> 1)]
            nib = (byte & 0x0F) if (sx & 1) else (byte >> 4)
            dx = (7 - sx) if hf else sx
            out[dy * 8 + dx] = nib
    return out


def indexed_to_rgba_rows(
    width: int,
    height: int,
    indexed: bytes | bytearray,
    palette: list[tuple[int, int, int, int]],
) -> list[bytes]:
    rows: list[bytes] = []
    for y in range(height):
        row = bytearray(width * 4)
        base = y * width
        for x in range(width):
            r, g, b, a = palette[indexed[base + x] & 0x0F]
            row[x * 4 : x * 4 + 4] = bytes((r, g, b, a))
        rows.append(bytes(row))
    return rows


def find_compatible_palettes(palettes: list[list[tuple[int, int, int, int]]], pal_id: int) -> list[int]:
    compat = [pal_id]
    target = palettes[pal_id]
    for other_id, other in enumerate(palettes):
        if other_id != pal_id and other == target:
            compat.append(other_id)
    return sorted(compat)


def extract_capture(
    cap_meta: dict[str, Any],
    ram_path: Path,
    vram_path: Path,
    vram_meta: dict[str, int],
    catalog_dir: Path,
    entries: dict[str, dict[str, Any]],
) -> dict[str, int]:
    ram = ram_path.read_bytes()
    vram = vram_path.read_bytes()
    state = struct.unpack_from(">H", ram, 0xFD64)[0]
    context = str(cap_meta.get("context") or f"state_{state:02d}_{ram_path.stem}")
    state_id = int(cap_meta.get("state_id", state))
    arena_name = cap_meta.get("arena_name")
    fighter_p1 = cap_meta.get("fighter_p1")
    fighter_p2 = cap_meta.get("fighter_p2")
    palettes = decode_cram_shadow(ram)

    sat_base = vram_meta.get("sprites", 0x1400)
    plane_a_base = vram_meta.get("planeA", 0x0000)
    plane_b_base = vram_meta.get("planeB", 0x2000)

    counts = {"sprites": 0, "tiles": 0, "planes": 0}

    # 1. Walk SAT linked list (up to 80 entries, column-major sprite tiles)
    visited: set[int] = set()
    link = 0
    for _ in range(80):
        if link in visited or link >= 80:
            break
        visited.add(link)
        off = (sat_base + link * 8) & 0xFFFF
        if off + 8 > len(vram):
            break
        w0, w1, w2, w3 = struct.unpack_from(">4H", vram, off)
        w_tiles = ((w1 >> 10) & 0x3) + 1
        h_tiles = ((w1 >> 8) & 0x3) + 1
        next_link = w1 & 0x7F
        pal_id = (w2 >> 13) & 0x3
        vf = bool(w2 & 0x1000)
        hf = bool(w2 & 0x0800)
        base_tile = w2 & 0x7FF

        width = w_tiles * 8
        height = h_tiles * 8
        buf = bytearray(width * height)
        non_empty = False

        # Assemble canonical sprite buffer with hf/vf applied across full sprite box
        for tx in range(w_tiles):
            for ty in range(h_tiles):
                tile_id = (base_tile + tx * h_tiles + ty) & 0x7FF
                tile_px = decode_4bpp_tile(vram, tile_id, hf=False, vf=False)
                for sy in range(8):
                    src_y = ty * 8 + sy
                    dst_y = (height - 1 - src_y) if vf else src_y
                    for sx in range(8):
                        nib = tile_px[sy * 8 + sx]
                        if nib == 0:
                            continue
                        non_empty = True
                        src_x = tx * 8 + sx
                        dst_x = (width - 1 - src_x) if hf else src_x
                        buf[dst_y * width + dst_x] = nib

        # Off-screen / unlinked check: still extract if within valid SAT and non-empty
        if non_empty and (w0 & 0x3FF) != 0:
            key = hashlib.sha256(
                b"SPRITE" + struct.pack(">HHB", width, height, pal_id) + bytes(buf)
            ).hexdigest()
            if key not in entries:
                png_rel = Path("assets/catalog/sprites") / f"{key[:16]}.png"
                png_abs = ROOT / png_rel
                idx_rel = Path("assets/catalog/indices") / f"{key[:16]}_idx.png"
                idx_abs = ROOT / idx_rel
                rows = indexed_to_rgba_rows(width, height, buf, palettes[pal_id])
                png_abs.write_bytes(encode_rgba_png(width, height, rows))
                idx_rows = [bytes(buf[y * width : (y + 1) * width]) for y in range(height)]
                idx_abs.write_bytes(encode_gray_png(width, height, idx_rows))
                entries[key] = {
                    "original_png": str(png_rel).replace("\\", "/"),
                    "index_map": str(idx_rel).replace("\\", "/"),
                    "hd_file": None,
                    "scale": 4,
                    "width": width,
                    "height": height,
                    "anchor": [width // 2, height // 2],
                    "palette_id": pal_id,
                    "compatible_palettes": find_compatible_palettes(palettes, pal_id),
                    "layer": "sprite",
                    "context": context,
                    "state_id": state_id,
                    "arena_name": arena_name,
                    "fighter_p1": fighter_p1,
                    "fighter_p2": fighter_p2,
                    "status": "extracted",
                }
                counts["sprites"] += 1

        if next_link == 0:
            break
        link = next_link

    # 2. Extract Plane A & Plane B 64x32 tilemaps and unique 8x8 tiles
    plane_w_tiles = 64
    plane_h_tiles = 32
    plane_w_px = plane_w_tiles * 8
    plane_h_px = plane_h_tiles * 8

    for layer_name, base_addr in (("plane_a", plane_a_base), ("plane_b", plane_b_base)):
        layer_rgba = [bytearray(plane_w_px * 4) for _ in range(plane_h_px)]
        non_empty_plane = False

        for ty in range(plane_h_tiles):
            for tx in range(plane_w_tiles):
                nt_off = (base_addr + (ty * plane_w_tiles + tx) * 2) & 0xFFFF
                if nt_off + 2 > len(vram):
                    continue
                e = struct.unpack_from(">H", vram, nt_off)[0]
                pal_id = (e >> 13) & 0x3
                vf = bool(e & 0x1000)
                hf = bool(e & 0x0800)
                tile_id = e & 0x7FF

                # Rendered tile (with hf/vf) for plane sheet
                rendered = decode_4bpp_tile(vram, tile_id, hf=hf, vf=vf)
                if any(rendered):
                    non_empty_plane = True
                    pal = palettes[pal_id]
                    for py in range(8):
                        row = layer_rgba[ty * 8 + py]
                        x0 = tx * 8
                        for px in range(8):
                            rgba = pal[rendered[py * 8 + px]]
                            pos = (x0 + px) * 4
                            row[pos : pos + 4] = bytes(rgba)

                    # Canonical unflipped tile for tile deduplication catalog
                    canonical = decode_4bpp_tile(vram, tile_id, hf=False, vf=False)
                    tkey = hashlib.sha256(
                        b"TILE" + struct.pack(">B", pal_id) + bytes(canonical)
                    ).hexdigest()
                    if tkey not in entries:
                        tpng_rel = Path("assets/catalog/tiles") / f"{tkey[:16]}.png"
                        tpng_abs = ROOT / tpng_rel
                        trows = indexed_to_rgba_rows(8, 8, canonical, pal)
                        tpng_abs.write_bytes(encode_rgba_png(8, 8, trows))
                        entries[tkey] = {
                            "original_png": str(tpng_rel).replace("\\", "/"),
                            "hd_file": None,
                            "scale": 4,
                            "width": 8,
                            "height": 8,
                            "anchor": [0, 0],
                            "palette_id": pal_id,
                            "compatible_palettes": find_compatible_palettes(palettes, pal_id),
                            "layer": layer_name,
                            "context": context,
                            "state_id": state_id,
                            "arena_name": arena_name,
                            "fighter_p1": fighter_p1,
                            "fighter_p2": fighter_p2,
                            "status": "extracted",
                        }
                        counts["tiles"] += 1

        if non_empty_plane:
            plane_rows = [bytes(r) for r in layer_rgba]
            pkey = hashlib.sha256(
                b"PLANE" + layer_name.encode("ascii") + b"".join(plane_rows)
            ).hexdigest()
            if pkey not in entries:
                ppng_rel = Path("assets/catalog/planes") / f"{context}_{layer_name}.png"
                ppng_abs = ROOT / ppng_rel
                ppng_abs.write_bytes(encode_rgba_png(plane_w_px, plane_h_px, plane_rows))
                entries[pkey] = {
                    "original_png": str(ppng_rel).replace("\\", "/"),
                    "hd_file": None,
                    "scale": 4,
                    "width": plane_w_px,
                    "height": plane_h_px,
                    "anchor": [0, 0],
                    "palette_id": 0,
                    "compatible_palettes": [0, 1, 2, 3],
                    "layer": layer_name,
                    "context": context,
                    "state_id": state_id,
                    "arena_name": arena_name,
                    "fighter_p1": fighter_p1,
                    "fighter_p2": fighter_p2,
                    "status": "extracted",
                }
                counts["planes"] += 1

    return counts


def inspect_obj_table_master(rom: bytes) -> dict[str, Any]:
    ptrs = [
        struct.unpack_from(">I", rom, OBJ_TABLE_MASTER_ADDR + i * 4)[0]
        for i in range(OBJ_TABLE_MASTER_COUNT)
    ]
    return {
        "rom_addr": f"0x{OBJ_TABLE_MASTER_ADDR:06X}",
        "entry_count": OBJ_TABLE_MASTER_COUNT,
        "unique_handlers": len(set(ptrs)),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Extrator de assets VRAM/CRAM -> pipeline/manifest.json")
    ap.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    ap.add_argument("--captures-dir", type=Path, default=DEFAULT_CAPTURES)
    ap.add_argument("--catalog-dir", type=Path, default=DEFAULT_CATALOG)
    ap.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    ap.add_argument("--preserve-hd", action="store_true", help="Preserva hd_file/status/neural_model existentes")
    args = ap.parse_args()

    if not args.rom.exists():
        print(f"ERRO: ROM não encontrada: {args.rom}", file=sys.stderr)
        return 1

    for sub in ("sprites", "tiles", "planes", "indices"):
        (args.catalog_dir / sub).mkdir(parents=True, exist_ok=True)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)

    prev_entries: dict[str, dict[str, Any]] = {}
    prev_neural_model: Any = None
    if args.preserve_hd and args.manifest.exists():
        prev_doc = json.loads(args.manifest.read_text(encoding="utf-8"))
        prev_entries = prev_doc.get("entries", {})
        prev_neural_model = prev_doc.get("neural_model")

    rom_bytes = args.rom.read_bytes()
    rom_sha256 = hashlib.sha256(rom_bytes).hexdigest()

    vram_meta_map: dict[str, dict[str, int]] = {}
    captures_meta: list[dict[str, Any]] = []
    harness_report_path = args.captures_dir / "harness_report.json"
    if harness_report_path.exists():
        hdata = json.loads(harness_report_path.read_text(encoding="utf-8"))
        vram_meta_map = hdata.get("vram_dumps", {})
        captures_meta = hdata.get("captures", [])

    available: list[tuple[dict[str, Any], Path, Path]] = []
    if captures_meta:
        for cap in captures_meta:
            r = args.captures_dir / cap["ram_file"]
            v = args.captures_dir / cap["vram_file"]
            if r.exists() and v.exists():
                available.append((cap, r, v))
    else:
        for r in sorted(args.captures_dir.glob("*.ram")):
            if r.stem.endswith("_16_9"):
                continue
            v = r.with_suffix(".vram")
            if v.exists():
                available.append(({"context": r.stem}, r, v))

    if not available:
        print(
            f"ERRO: nenhuma captura RAM/VRAM encontrada em {args.captures_dir} "
            "(execute harness/ec_harness.py primeiro).",
            file=sys.stderr,
        )
        return 1

    entries: dict[str, dict[str, Any]] = {}
    for cap_meta, ram_p, vram_p in available:
        meta = vram_meta_map.get(vram_p.name, {"planeA": 0x0000, "planeB": 0x2000, "sprites": 0x1400})
        extract_capture(cap_meta, ram_p, vram_p, meta, args.catalog_dir, entries)

    if args.preserve_hd and prev_entries:
        preserve_keys = ("hd_file", "status", "workflow", "workflow_version", "model", "seed", "silhouette_iou", "reference_png")
        for key, item in entries.items():
            old = prev_entries.get(key)
            if old and (old.get("hd_file") or old.get("status") != "extracted"):
                for pk in preserve_keys:
                    if pk in old:
                        item[pk] = old[pk]

    sprite_entries = sum(1 for e in entries.values() if e["layer"] == "sprite")
    tile_entries = sum(
        1
        for e in entries.values()
        if e["layer"] in ("plane_a", "plane_b") and e["width"] == 8 and e["height"] == 8
    )
    plane_entries = sum(
        1
        for e in entries.values()
        if e["layer"] in ("plane_a", "plane_b") and (e["width"], e["height"]) != (8, 8)
    )
    extracted_count = sum(1 for e in entries.values() if e.get("status") == "extracted")
    generated_count = sum(1 for e in entries.values() if e.get("status") == "generated")
    reviewed_count = sum(1 for e in entries.values() if e.get("status") == "reviewed")
    approved_count = sum(1 for e in entries.values() if e.get("status") == "approved")

    manifest = {
        "schema_version": 1,
        "rom_sha256": rom_sha256,
        "neural_model": prev_neural_model,
        "obj_table_master": inspect_obj_table_master(rom_bytes),
        "summary": {
            "total_entries": len(entries),
            "extracted_count": extracted_count,
            "generated_count": generated_count,
            "reviewed_count": reviewed_count,
            "approved_count": approved_count,
            "sprite_entries": sprite_entries,
            "tile_entries": tile_entries,
            "plane_entries": plane_entries,
        },
        "entries": entries,
    }

    args.manifest.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(
        f"Manifest gerado em {args.manifest}: {len(entries)} entradas "
        f"(sprites={sprite_entries}, tiles={tile_entries}, planes={plane_entries}, "
        f"generated={generated_count}, approved={approved_count})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
