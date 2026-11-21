#!/usr/bin/env python3
"""t2_cramscan3.py — full distributions: data-port byte values and code-3
control words, per side."""
import sys
sys.path.insert(0, 'tools/scripts/t2_repro')
from t2_logcmp import parse
from collections import Counter

A = parse('/tmp/opencode/t2nat2/mem_write_log_native.log')   # armed wall 5084
B = parse('/tmp/opencode/t2int2/mem_write_log_native.log')   # armed wall 5062

def analyze(d, label):
    ctrl = []   # (wf, hi_byte_event, lo_byte_event) pairs
    data = Counter()
    data_first = {}
    evs = {}
    for fr, lines in d.items():
        for x in lines:
            p = x.split()
            a = int(p[3], 16)
            v = int(p[4], 16)
            if a in (0xC00000, 0xC00002):
                data[v] += 1
                data_first.setdefault(v, fr)
            else:
                evs.setdefault(fr, []).append((a, v))
    print('=== %s ===' % label)
    print('  data-port byte value distribution (value: count, first log-frame):')
    for v, n in data.most_common(12):
        print('    %02X: %d (first lf=%d)' % (v, n, data_first[v]))
    # control words: pair hi/lo per frame in stream order
    cr = Counter()
    for fr in sorted(evs):
        seq = evs[fr]
        i = 0
        while i < len(seq):
            a, v = seq[i]
            if a & 1 == 0 and i + 1 < len(seq) and seq[i+1][0] == a + 1:
                w = (v << 8) | seq[i+1][1]
                cr[w] += 1
                i += 2
            else:
                cr[v] += 1
                i += 1
    code3 = {w: n for w, n in cr.items() if (w & 0xC000) == 0xC000}
    print('  code-3 (CRAM) control words: %s' % code3)
    # any word with code 3 and DMA bit (bit 5): w & 0xC000 == 0xC000 and w & 0x20
    dma3 = {w: n for w, n in cr.items() if (w & 0xC000) == 0xC000 and (w & 0x20)}
    print('  code-3 with CD5 (DMA): %s' % dma3)
    # count per frame of CRAM-DMA words
    return cr

analyze(A, 'A (recomp)')
print()
analyze(B, 'B (interp)')
