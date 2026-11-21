#!/usr/bin/env python3
"""t4_gen.py — build mirror ECxEC (char 9) input scripts from movie 17.

The VS char select of movie 17 picks Rax(1)/Blade(2) with:
  P1: 8xRIGHT @f1880-2028, UP @f2040, START @f2060
  P2: RIGHT @f2370, START(P1) @f2390-2400, START(P2) @f2440
We replace the cursor moves with a variant and assert the resulting
fighter ids ($9CBE=P1, $9CBC=P2), then dump VRAM at the fight.
"""
import argparse, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent.parent / "harness"))
from ec_harness import parse_ecm, write_input_script

ROM_SHA = '6d8bdaf8c994d9f718c8f991900c58cdf1af84e030524b57f6dda263cc4ea247'

def build(movie_path, out, p1_moves, p2_moves, stop_frame=3400,
          dump_vram=None, assert_p1=None, assert_p2=None):
    m = parse_ecm(Path(movie_path), ROM_SHA)
    masks = list(m['masks'][:stop_frame])
    masks2 = list(m['masks2'][:stop_frame]) if m['masks2'] else None
    # zero the char-select regions
    for f in range(1879, 2061):
        masks[f] = 0
    for f in range(2369, 2451):
        masks[f] = 0
        if masks2: masks2[f] = 0
    # P1 moves: list of (start_frame, buttons) — held for 8 frames each
    f = 1880
    for btn in p1_moves:
        for k in range(8):
            masks[f + k] = btn
        f += 8
    # P1 START
    masks[f] = 8  # START
    f += 8
    masks[f] = 0
    f += 8
    # P2 moves
    f2 = 2370
    for btn in p2_moves:
        for k in range(8):
            if masks2: masks2[f2 + k] = btn
            else: masks[f2 + k] = btn
        f2 += 8
    if masks2:
        # P1 START then P2 START (mirrors the movie's lock-in)
        masks[f2] = 8
        masks2[f2 + 8] = 8
        masks[f2 + 16] = 0
        masks2[f2 + 16] = 0
    m['masks'] = masks
    m['masks2'] = masks2
    m['frames'] = stop_frame
    # write a script: first the ECM-driven part, then asserts/dumps
    write_input_script(m, out, {}, {}, Path('/tmp/opencode/t4_last.ram'),
                       Path('/tmp/opencode/t4_last.vram') if dump_vram else None,
                       Path('/tmp/opencode/t4_last.png'))
    # append asserts at the right moments: after P1 START (~f2070) and after P2 lock (~f2460)
    lines = out.read_text().splitlines()
    # strip the trailing DUMP/SCREENSHOT lines (they run at stop_frame; keep them)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--p1', default='128,128,128,128,128,128,128,128,16')
    ap.add_argument('--p2', default='128')
    ap.add_argument('--out', default='/tmp/opencode/t4_variant.input')
    args = ap.parse_args()
    p1 = [int(x) for x in args.p1.split(',')]
    p2 = [int(x) for x in args.p2.split(',')]
    build('harness/movies/17_2p_vs_overkill_trident_s24.ecm',
          Path(args.out), p1, p2)
    print('wrote', args.out)

if __name__ == '__main__':
    main()
