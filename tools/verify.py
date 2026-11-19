#!/usr/bin/env python3
"""tools/verify.py — monta disasm/main.asm com vasmm68k_mot e compara com a ROM.

"O hash é a lei" (DESIGN §2.1): o binário montado precisa ser byte a byte
idêntico à ROM de referência. Sai com 0 se bate, 1 se difere.

Uso:
    python tools/verify.py [--rom roms/eternal_champions_us.gen]
                           [--vasm tools/vendor/vasm/vasmm68k_mot]
"""

import argparse
import pathlib
import subprocess
import sys
import tempfile


def find_vasm(root: pathlib.Path) -> pathlib.Path:
    candidates = [
        root / "tools/vendor/vasm/vasmm68k_mot",
        pathlib.Path("/usr/local/bin/vasmm68k_mot"),
        pathlib.Path("/usr/bin/vasmm68k_mot"),
    ]
    for c in candidates:
        if c.is_file():
            return c
    return pathlib.Path("vasmm68k_mot")  # espera estar no PATH


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rom", default="roms/eternal_champions_us.gen")
    ap.add_argument("--vasm", default=None)
    ap.add_argument("--keep", action="store_true",
                    help="mantém o .bin montado (despeja em disasm/build)")
    args = ap.parse_args()

    root = pathlib.Path(__file__).resolve().parent.parent
    rom_path = (root / args.rom).resolve()
    main_asm = root / "disasm" / "main.asm"
    vasm = pathlib.Path(args.vasm) if args.vasm else find_vasm(root)

    if not rom_path.is_file():
        print(f"erro: ROM não encontrada: {rom_path}", file=sys.stderr)
        return 1
    if not main_asm.is_file():
        print(f"erro: {main_asm} não existe — rode tools/split.py primeiro",
              file=sys.stderr)
        return 1
    if not vasm.is_file():
        print(f"erro: vasmm68k_mot não encontrado (procurado em: {vasm})",
              file=sys.stderr)
        return 1

    rom = rom_path.read_bytes()
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tf:
        out_bin = pathlib.Path(tf.name)

    # incbin em main.asm é relativo ao diretório do fonte: rodar com cwd=disasm/
    # -no-opt: o hash é a lei (DESIGN §2.1) — proíbe o vasm de "otimizar"
    # instruções (ex.: asl #1 → add), o que mudaria os bytes.
    proc = subprocess.run(
        [str(vasm), "-no-opt", "-Fbin", "-o", str(out_bin), "main.asm"],
        cwd=root / "disasm",
        capture_output=True, text=True,
    )
    if proc.returncode != 0:
        print("erro: vasm falhou:", file=sys.stderr)
        print(proc.stderr, file=sys.stderr)
        out_bin.unlink(missing_ok=True)
        return 1

    built = out_bin.read_bytes()
    if args.keep:
        keep = root / "disasm" / "build"
        keep.mkdir(exist_ok=True)
        (keep / "main.bin").write_bytes(built)
        print(f"binário montado: {keep / 'main.bin'}")
    else:
        out_bin.unlink(missing_ok=True)

    if built == rom:
        print(f"OK: binário montado é idêntico à ROM ({len(rom)} bytes)")
        return 0

    n = min(len(built), len(rom))
    first = next((i for i in range(n) if built[i] != rom[i]), n)
    print(f"FALHA: primeira divergência no offset ${first:06X}", file=sys.stderr)
    print(f"  tamanho montado={len(built)}  ROM={len(rom)}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
