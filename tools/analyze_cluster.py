#!/usr/bin/env python3
"""tools/analyze_cluster.py — análise local do pseudocódigo exportado pelo REA.

Uso: python tools/analyze_cluster.py [--dir reference/rea/decompile-2026-11-18]
     [--start 0xC000 --end 0x11000]

Extrai de cada função: chamadas (FUN_*), acessos a hardware (DAT_00a1*,
DAT_00c0*, ...), tamanho, e monta o grafo de chamadas do cluster.
"""

import argparse
import pathlib
import re
import sys
from collections import Counter, defaultdict

CALL_RE = re.compile(r"\b(FUN_[0-9a-f]{8}|DAT_[0-9a-f]{8})\b")
HW_RE = re.compile(r"\bDAT_(00[a-f0-9]{6})\b", re.I)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dir", default="reference/rea/decompile-2026-11-18")
    ap.add_argument("--start", default="0xC000")
    ap.add_argument("--end", default="0x11000")
    ap.add_argument("--top", type=int, default=25, help="N no ranking")
    args = ap.parse_args()

    root = pathlib.Path(__file__).resolve().parent.parent
    fdir = (root / args.dir / "functions").resolve()
    lo, hi = int(args.start, 16), int(args.end, 16)

    funcs: dict[str, dict] = {}
    for f in sorted(fdir.glob("*.c")):
        addr = int(f.stem, 16)
        if not (lo <= addr < hi):
            continue
        src = f.read_text(errors="replace")
        syms = CALL_RE.findall(src)
        funcs[f.stem] = {
            "lines": len(src.splitlines()),
            "bytes": len(src),
            "calls": [s for s in syms if s.startswith("FUN_")],
            "data": [s for s in syms if s.startswith("DAT_")],
            "hw": sorted(set(HW_RE.findall(src))),
        }

    print(f"funções no cluster {args.start}–{args.end}: {len(funcs)}")
    print()

    # ranking: chamadas recebidas (utilitários)
    indeg: Counter = Counter()
    for f in funcs.values():
        indeg.update(c.lower() for c in f["calls"])

    print(f"--- TOP {args.top} mais chamadas (prováveis utilitários) ---")
    for name, n in indeg.most_common(args.top):
        a = name.split("_")[1]
        lines = funcs.get(a, {}).get("lines", "?")
        print(f"  {name}: {n} chamadas, {lines} linhas de pseudo")

    print()
    print(f"--- TOP {args.top} maiores funções ---")
    for name, info in sorted(funcs.items(), key=lambda kv: -kv[1]["lines"])[:args.top]:
        print(f"  FUN_{name}: {info['lines']} linhas, "
              f"{len(info['calls'])} chamadas, {len(set(info['hw']))} endereços HW")

    print()
    print("--- funções que tocam hardware (VDP/Z80/IO) ---")
    hw: Counter = Counter()
    for name, info in funcs.items():
        for a in info["hw"]:
            hw[a] += 1
    for a, n in hw.most_common(20):
        print(f"  DAT_{a}: {n} funções")
    return 0


if __name__ == "__main__":
    sys.exit(main())
