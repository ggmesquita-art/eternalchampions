#!/usr/bin/env python3
"""Validate assets/shaders/presets/*.glslp: every shaderN / textures path must exist."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PRESETS = ROOT / "assets" / "shaders" / "presets"


def parse(path):
    kv = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        kv[k.strip()] = v.split("#")[0].strip().strip('"')
    return kv


def main():
    bad = 0
    files = sorted(PRESETS.glob("*.glslp"))
    if not files:
        print("no presets found")
        return 1
    for p in files:
        kv = parse(p)
        n = int(kv.get("shaders", "0"))
        refs = []
        for i in range(n):
            if f"shader{i}" not in kv:
                print(f"{p.name}: missing shader{i}")
                bad += 1
            else:
                refs.append((f"shader{i}", kv[f"shader{i}"]))
        for name in kv.get("textures", "").split(";"):
            name = name.strip()
            if name and name in kv:
                refs.append((name, kv[name]))
        if "ec_bplus" in kv:
            refs.append(("ec_bplus", kv["ec_bplus"]))
        for key, rel in refs:
            ok = (p.parent / rel).is_file()
            if not ok and key == "ec_bplus":
                print(f"{p.name}: WARN {key} -> {rel} not found (models are not versioned)")
                continue
            if not ok:
                print(f"{p.name}: MISSING {key} -> {rel}")
                bad += 1
        print(f"{p.name}: {n} passes, {len(refs)} refs checked")
    print("FAIL" if bad else "OK")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
