#!/usr/bin/env python3
"""t2_cramscan.py — scan both write-logs for CRAM commands + data writes.

Finds every CRAM-write command (code 3, word & 0xC000 == 0xC000) and its
following data-port write, per wall frame, on both sides. Identifies where
the interp fills CRAM and what the recomp does instead.
"""
import sys
sys.path.insert(0, 'tools/scripts/t2_repro')
from t2_logcmp import parse

A = parse('/tmp/opencode/t2nat2/mem_write_log_native.log')   # armed wall 5084
B = parse('/tmp/opencode/t2int2/mem_write_log_native.log')   # armed wall 5062
ARM_A, ARM_B = 5084, 5062

def events(d):
    """frame -> ordered byte events on VDP ports."""
    out = {}
    for fr, lines in d.items():
        for x in lines:
            p = x.split()
            a = int(p[3], 16)
            if a in (0xC00000, 0xC00002, 0xC00004, 0xC00005):
                out.setdefault(fr, []).append((a, int(p[4], 16)))
    return out

def words(ev):
    """Reconstruct word writes: pair even+odd byte events of same port."""
    ws = []
    i = 0
    while i < len(ev):
        a, v = ev[i]
        kind = 'C' if a in (0xC00004, 0xC00005) else 'D'
        if i + 1 < len(ev) and (ev[i+1][0] & 1) != (a & 1) and kind == ('C' if ev[i+1][0] in (0xC00004, 0xC00005) else 'D'):
            # pair even byte then odd byte -> word
            w = (v << 8) | ev[i+1][1]
            ws.append((kind, w))
            i += 2
        else:
            ws.append((kind, v))
            i += 1
    return ws

def scan(d, arm, label):
    evs = events(d)
    print('=== %s: CRAM writes (word & 0xC000 == 0xC000) and their data ===' % label)
    n_cram_cmd = 0
    n_data = 0
    hits = []
    for fr in sorted(evs):
        wf = fr + arm
        ws = words(evs[fr])
        for i, (kind, w) in enumerate(ws):
            if kind == 'C' and (w & 0xC000) == 0xC000:
                n_cram_cmd += 1
                # find next data word
                data = None
                for j in range(i+1, len(ws)):
                    if ws[j][0] == 'D':
                        data = ws[j][1]
                        break
                n_data += 1 if data is not None else 0
                hits.append((wf, w, data))
    print('  total CRAM commands: %d, with following data write: %d' % (n_cram_cmd, n_data))
    print('  first 30 (wall, cmd, data):')
    for h in hits[:30]:
        print('    wf=%d cmd=%04X data=%s' % (h[0], h[1], ('%04X' % h[2]) if h[2] is not None else 'None'))
    # nonzero data
    nz = [h for h in hits if h[2]]
    print('  CRAM writes with NONZERO data: %d' % len(nz))
    for h in nz[:20]:
        print('    wf=%d cmd=%04X data=%04X' % h)
    return hits

scan(A, ARM_A, 'A (recomp)')
print()
scan(B, ARM_B, 'B (interp)')
