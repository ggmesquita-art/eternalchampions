#!/usr/bin/env python3
"""tools/split.py — extrai da ROM do usuário os arquivos gerados do disasm.

Gera:
  disasm/gen/vectors.asm — tabela de vetores ($000000–$0001FF) como 64 dc.l
  disasm/data/rest.bin  — resto da ROM ($000200 em diante)

NÃO toca em disasm/main.asm (escrito à mão) nem nos includes de disasm/code/.
O layout de regiões convertidas x incbin vive no main.asm.

Uso:
    python tools/split.py [--rom roms/eternal_champions_us.gen]
"""

import argparse
import hashlib
import pathlib
import sys

VECTOR_NAMES = {
    0x000: "Vector_SSP",         # stack pointer inicial
    0x004: "Vector_Reset",       # PC inicial
    0x008: "Vector_BusError",
    0x00C: "Vector_AddressError",
    0x010: "Vector_IllegalInstr",
    0x014: "Vector_ZeroDivide",
    0x018: "Vector_CHK",
    0x01C: "Vector_TRAPV",
    0x020: "Vector_PrivViolation",
    0x024: "Vector_Trace",
    0x028: "Vector_Line1010",
    0x02C: "Vector_Line1111",
    0x060: "Vector_SpuriousInt",
    0x064: "Vector_IRQ1",
    0x068: "Vector_IRQ2",
    0x06C: "Vector_IRQ3",
    0x070: "Vector_HBlank",      # IRQ4 — aponta para $FF0000 (desabilitado)
    0x074: "Vector_IRQ5",
    0x078: "Vector_VBlank",      # IRQ6
    0x07C: "Vector_IRQ7",
}
VECTOR_NAMES.update({0x080 + 4 * i: f"Vector_Trap{i}" for i in range(16)})


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rom", default="roms/eternal_champions_us.gen")
    ap.add_argument("--out", default="disasm")
    args = ap.parse_args()

    root = pathlib.Path(__file__).resolve().parent.parent
    rom_path = (root / args.rom).resolve()
    out_dir = (root / args.out).resolve()

    if not rom_path.is_file():
        print(f"erro: ROM não encontrada: {rom_path}", file=sys.stderr)
        return 1

    rom = rom_path.read_bytes()
    if len(rom) < 0x200:
        print("erro: ROM menor que a tabela de vetores", file=sys.stderr)
        return 1

    gen_dir = out_dir / "gen"
    data_dir = out_dir / "data"
    gen_dir.mkdir(parents=True, exist_ok=True)
    data_dir.mkdir(parents=True, exist_ok=True)

    lines = []
    lines.append("; disasm/gen/vectors.asm — GERADO por tools/split.py. Não edite.")
    lines.append(f"; ROM de origem: {rom_path.name}")
    lines.append(f"; sha256: {hashlib.sha256(rom).hexdigest()}")
    lines.append("")
    lines.append("\torg\t$000000")
    lines.append("; ---- Tabela de vetores de exceção (64 x 4 bytes) ----")
    for off in range(0, 0x200, 4):
        val = int.from_bytes(rom[off:off + 4], "big")
        name = VECTOR_NAMES.get(off, f"Vector_{off:03X}")
        lines.append(f"{name}:\tdc.l\t${val:08X}\t; ${off:06X}")

    (gen_dir / "vectors.asm").write_text("\n".join(lines) + "\n")
    (data_dir / "rest.bin").write_bytes(rom[0x200:])

    print(f"ok: {gen_dir / 'vectors.asm'}")
    print(f"ok: {data_dir / 'rest.bin'} ({len(rom) - 0x200} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
