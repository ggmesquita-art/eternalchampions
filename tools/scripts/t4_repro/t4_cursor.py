#!/usr/bin/env python3
"""t4_cursor.py — map the VS char-select cursor: run direction variants,
read the picked fighter id from the ASSERT failure message.

The VS flow (movie 17): P1 cursor moves f1880-2048, P1 START f2060, P2
cursor f2370+, P1 START again f2390, P2 START f2440 — the picks land in
$9CBE/$9CBC around f2430-2470. We override only the P1 cursor moves and
probe $9CBE at f2550.
"""
import re, subprocess, sys, os
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent.parent / "harness"))
from ec_harness import parse_ecm, write_input_script

ROM_SHA = '6d8bdaf8c994d9f718c8f991900c58cdf1af84e030524b57f6dda263cc4ea247'
BASE = (Path(__file__).resolve().parent.parent.parent.parent / "harness/movies/17_2p_vs_overkill_trident_s24.ecm")
EXE = 'build/native/EternalChampionsRecomp'
ROM = 'roms/eternal_champions_us.gen'
B = {'UP': 16, 'DOWN': 32, 'LEFT': 64, 'RIGHT': 128, 'A': 2, 'B': 1, 'C': 256, 'START': 8}

def gen(moves, out, stop=2600):
    m = parse_ecm(BASE, ROM_SHA)
    masks = list(m['masks'][:stop])
    for f in range(1879, 2080):
        masks[f] = 0
    f = 2080
    for name, reps in moves:
        for i in range(reps):
            for k in range(8):
                masks[f + i * 20 + k] = B[name]
        f += reps * 20
    for k in range(10):
        masks[f + k] = B['START']
    f += 20
    masks[f] = 0
    m['masks'] = masks; m['frames'] = stop
    write_input_script(m, Path(out), {}, {}, Path('/tmp/opencode/t4c_last.ram'), None,
                       Path('/tmp/opencode/t4c_last.png'))
    lines = open(out).read().splitlines()
    while lines and 'EXIT' in lines[-1]:
        lines.pop()
    lines.append('ASSERT_RAM16 0xFF9CBE 0xFFFF')
    lines.append('EXIT 0')
    open(out, 'w').write('\n'.join(lines) + '\n')

def run(moves):
    gen(moves, '/tmp/opencode/t4c.input')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SDL_RENDER_DRIVER='software')
    r = subprocess.run(['env', *('%s=%s' % kv for kv in env.items()), f'./{EXE}',
                        '--benchmark', '3000', '--fresh-evidence',
                        '--input-script', '/tmp/opencode/t4c.input', ROM],
                       capture_output=True, text=True, timeout=600)
    out = (r.stdout or '') + (r.stderr or '')
    m = re.search(r'ASSERT_RAM16 \$FF9CBE expected \$FFFF got \$([0-9A-F]{4})', out)
    if m:
        return 'picked id=%d' % int(m.group(1), 16)
    return '(no assert; rc=%d)' % r.returncode

if __name__ == '__main__':
    variants = [
        [('RIGHT', 0)], [('RIGHT', 1)], [('RIGHT', 2)], [('RIGHT', 3)],
        [('RIGHT', 4)], [('RIGHT', 5)], [('RIGHT', 6)], [('RIGHT', 7)],
        [('RIGHT', 8)], [('UP', 1)], [('DOWN', 1)], [('LEFT', 1)],
    ]
    for v in variants:
        label = '+'.join('%s%d' % t for t in v)
        print('%-14s -> %s' % (label, run(v)), flush=True)
