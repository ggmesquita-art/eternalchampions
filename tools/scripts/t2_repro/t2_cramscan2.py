#!/usr/bin/env python3
"""t2_cramscan2.py — raw byte events on the VDP data port, both sides.

The mem-write-log records per-BYTE accesses; a word write to $C00000 logs
only the hi byte (C00000), the lo byte (C00001) is not in the watch list.
So scan raw events: any nonzero hi byte on C00000/C00002 is a palette write.
"""
import sys
sys.path.insert(0, 'tools/scripts/t2_repro')
from t2_logcmp import parse

A = parse('/tmp/opencode/t2nat2/mem_write_log_native.log')   # armed wall 5084
B = parse('/tmp/opencode/t2int2/mem_write_log_native.log')   # armed wall 5062
ARM_A, ARM_B = 5084, 5062

def data_events(d, arm, label):
    print('=== %s: raw data-port (C00000/C00002) byte events ===' % label)
    total = 0
    nz = []
    first = []
    for fr in sorted(d):
        wf = fr + arm
        for x in d[fr]:
            p = x.split()
            a = int(p[3], 16)
            if a in (0xC00000, 0xC00002):
                v = int(p[4], 16)
                total += 1
                if len(first) < 8:
                    first.append((wf, '%04X' % a, '%02X' % v))
                if v:
                    nz.append((wf, '%04X' % a, '%02X' % v))
    print('  total data byte events: %d, nonzero: %d' % (total, len(nz)))
    print('  first 8 events:', first)
    print('  first 25 NONZERO (wf, addr, value):')
    for h in nz[:25]:
        print('    wf=%d %s=%s' % h)
    return nz

na = data_events(A, ARM_A, 'A (recomp)')
print()
nb = data_events(B, ARM_B, 'B (interp)')
