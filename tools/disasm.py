#!/usr/bin/env python3
"""tools/disasm.py — desassemblador linear de regiões da ROM (68000, capstone).

Complementa o REA/Ghidra: o REA só decodifica endereços já listados;
aqui qualquer região vira listing. Labels vêm de disasm/symbols/labels.toml.

Uso:
    python tools/disasm.py --start 0x6B6 --end 0x78C
    python tools/disasm.py --start 0x6B6 --length 0x100
"""

import argparse
import pathlib
import sys

from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN

try:
    import tomllib
except ImportError:
    import tomli as tomllib


def load_labels(root: pathlib.Path) -> dict[int, str]:
    """labels.toml -> {endereço: nome} (também marca onde não desassemblar)."""
    out: dict[int, str] = {}
    path = root / "disasm" / "symbols" / "labels.toml"
    if not path.is_file():
        return out
    data = tomllib.loads(path.read_text())
    for entry in data.get("label", []):
        out[int(entry["address"])] = entry["name"]
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rom", default="roms/eternal_champions_us.gen")
    ap.add_argument("--start", required=True, help="endereço inicial (0x...)")
    ap.add_argument("--end", default=None, help="endereço final exclusivo (0x...)")
    ap.add_argument("--length", default=None, help="ou: tamanho em bytes (0x...)")
    ap.add_argument("--no-labels", action="store_true",
                    help="não imprime labels (só instruções)")
    args = ap.parse_args()

    root = pathlib.Path(__file__).resolve().parent.parent
    rom = (root / args.rom).read_bytes()
    start = int(args.start, 16)
    if args.end:
        end = int(args.end, 16)
    elif args.length:
        end = start + int(args.length, 16)
    else:
        print("erro: informe --end ou --length", file=sys.stderr)
        return 1
    if end <= start or end > len(rom):
        print(f"erro: faixa inválida {start:#x}..{end:#x} (ROM tem {len(rom)} bytes)",
              file=sys.stderr)
        return 1

    labels = load_labels(root)
    md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN)
    md.detail = False

    code = rom[start:end]
    print(f"; desassembly {start:#06X}..{end:#06X} ({end - start} bytes), capstone")
    for ins in md.disasm(code, start):
        if ins.address in labels:
            print(f"{labels[ins.address]}:")
        print(f"{ins.address:06X}:  {ins.bytes.hex(' '):<16}  {ins.mnemonic:<10} {ins.op_str}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
