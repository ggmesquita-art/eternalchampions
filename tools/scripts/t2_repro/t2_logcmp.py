#!/usr/bin/env python3
"""t2_logcmp.py — compare two mem_write_log captures (A=recomp, B=interp).

Finds the first wall frame where the write streams to the watched ranges
differ, printing both sides' lines for a window around it. Then prints the
first differing line pair within that frame.
"""
import sys

def parse(path):
    out = {}
    with open(path) as f:
        for line in f:
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            parts = line.split()
            # frame internal_frame game_mode address value a7 ret0 ret1 ret2 ret3 target_cycle yield
            if len(parts) < 11:
                continue
            fr = int(parts[0])
            out.setdefault(fr, []).append(line)
    return out

def key_of(line):
    p = line.split()
    # (address, value, ret0) — ret chain identifies the caller site
    return (int(p[3], 16), int(p[4], 16), int(p[6], 16), int(p[7], 16))

def main():
    a = parse(sys.argv[1])
    b = parse(sys.argv[2])
    frames = sorted(set(a) | set(b))
    first = None
    for fr in frames:
        la, lb = a.get(fr, []), b.get(fr, [])
        if [key_of(x) for x in la] == [key_of(x) for x in lb]:
            continue
        first = fr
        print('=== first differing frame: %d (A lines=%d B lines=%d) ===' %
              (fr, len(la), len(lb)))
        # show lines that are in one side but not the other (by multiset of keys)
        from collections import Counter
        ca = Counter(key_of(x) for x in la)
        cb = Counter(key_of(x) for x in lb)
        print('--- A-only writes (addr value ret0 ret1):')
        for k, n in (ca - cb).items():
            print('   %06X %02X ret0=%06X ret1=%06X x%d' % (k[0], k[1], k[2], k[3], n))
        print('--- B-only writes (addr value ret0 ret1):')
        for k, n in (cb - ca).items():
            print('   %06X %02X ret0=%06X ret1=%06X x%d' % (k[0], k[1], k[2], k[3], n))
        # print the raw lines around the first difference in stream order
        print('--- A stream:')
        for x in la[:14]:
            print('   ', x)
        print('--- B stream:')
        for x in lb[:14]:
            print('   ', x)
        break
    if first is None:
        print('no divergence found in watched ranges')

if __name__ == '__main__':
    main()
