#!/usr/bin/env python3
"""t2_probe.py — catch the recomp×interp divergence in the T2 (training) flow.

Launches nothing; connects to two already-running debug servers (ports A/B),
polls frame_info until both pass --target, pauses both, then:
  * diffs WRAM for frames [target-lo, target+hi] (first-divergence search)
  * dumps the Tier-1 rdb write logs from both sides to files
"""
import argparse, json, socket, time, sys

def rpc(sock, cmd, **kw):
    sock.sendall((json.dumps({'cmd': cmd, 'id': 1, **kw}) + '\n').encode())
    buf = b''
    while b'\n' not in buf:
        ch = sock.recv(1 << 22)
        if not ch:
            raise RuntimeError(cmd + ': connection closed')
        buf += ch
    return json.loads(buf.split(b'\n', 1)[0].decode())

def connect(port):
    for _ in range(400):
        try:
            s = socket.create_connection(('127.0.0.1', port), timeout=10)
            return s
        except OSError:
            time.sleep(0.25)
    raise RuntimeError('port %d never came up' % port)

def frame_info(s):
    r = rpc(s, 'frame_info')
    if not r.get('ok'):
        raise RuntimeError('frame_info: %r' % r)
    return r

def wram(s, frame):
    r = rpc(s, 'get_frame', frame=frame, include='wram')
    if not r.get('ok'):
        raise RuntimeError('get_frame %d: %s' % (frame, r.get('error')))
    return bytes.fromhex(r['wram']), r

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--porta', type=int, default=4378)
    ap.add_argument('--portb', type=int, default=4379)
    ap.add_argument('--target', type=int, default=5520)
    ap.add_argument('--lo', type=int, default=5100)
    ap.add_argument('--hi', type=int, default=5560)
    ap.add_argument('--rdb-a', default='/tmp/opencode/t2_rdb_a.json')
    ap.add_argument('--rdb-b', default='/tmp/opencode/t2_rdb_b.json')
    ap.add_argument('--no-pause', action='store_true')
    args = ap.parse_args()

    a = connect(args.porta)
    b = connect(args.portb)
    print('A:', {k: v for k, v in frame_info(a).items() if 'frame' in k})
    print('B:', {k: v for k, v in frame_info(b).items() if 'frame' in k})

    if not args.no_pause:
        # GENESIS_OWNER_REVIEW=1 boots paused; start both, then freeze each
        # side individually as it crosses the target.
        print('play A:', rpc(a, 'owner_review', action='play'))
        print('play B:', rpc(b, 'owner_review', action='play'))
        done_a = done_b = False
        while not (done_a and done_b):
            fa = frame_info(a).get('current_frame', -1)
            fb = frame_info(b).get('current_frame', -1)
            sys.stdout.write('\rfa=%d fb=%d (target %d)   ' % (fa, fb, args.target))
            sys.stdout.flush()
            if not done_a and fa >= args.target:
                r = rpc(a, 'owner_review', action='pause')
                done_a = r.get('ok')
                print('\nowner_review pause A at %d -> %r' % (fa, r))
            if not done_b and fb >= args.target:
                r = rpc(b, 'owner_review', action='pause')
                done_b = r.get('ok')
                print('owner_review pause B at %d -> %r' % (fb, r))
            if done_a and done_b:
                break
            time.sleep(0.005)

    print('A:', {k: v for k, v in frame_info(a).items() if 'frame' in k})
    print('B:', {k: v for k, v in frame_info(b).items() if 'frame' in k})

    first = None
    prev = 0
    for wf in range(args.lo, args.hi + 1):
        try:
            wa, ra = wram(a, wf)
            wb, rb = wram(b, wf)
        except RuntimeError as e:
            print('wf %d: %s' % (wf, e))
            continue
        diffs = [(off, wa[off], wb[off]) for off in range(0x10000)
                 if wa[off] != wb[off]]
        jump = '   <<< JUMP' if len(diffs) > prev + 4 else ''
        print('wf %d: %d diffs%s' % (wf, len(diffs), jump))
        if diffs and first is None:
            first = wf
            print('  first divergence at frame %d:' % wf)
            for off, av, bv in diffs[:40]:
                print('   $FF%04X: A=%02X B=%02X' % (off, av, bv))
            ma, mb = ra.get('m68k', {}), rb.get('m68k', {})
            print('  m68k A: PC=0x%06X A7=0x%06X D0=0x%08X SR=0x%04X' %
                  (ma.get('PC', 0), ma.get('A', [0]*8)[7], ma.get('D', [0]*8)[0], ma.get('SR', 0)))
            print('  m68k B: PC=0x%06X A7=0x%06X D0=0x%08X SR=0x%04X' %
                  (mb.get('PC', 0), mb.get('A', [0]*8)[7], mb.get('D', [0]*8)[0], mb.get('SR', 0)))
        prev = len(diffs)

    if first is not None:
        print('RESULT: first divergent frame =', first)

    # dump rdb rings (page through)
    for sock, path in ((a, args.rdb_a), (b, args.rdb_b)):
        out = []
        start = 0
        while True:
            r = rpc(sock, 'rdb_dump', start=start, count=200000)
            if not r.get('ok'):
                print('rdb_dump failed:', r)
                break
            out.extend(r.get('log', []))
            if r.get('done'):
                break
            start += r.get('returned', 0)
        with open(path, 'w') as f:
            json.dump(out, f)
        print('rdb %s: %d entries' % (path, len(out)))

    a.close(); b.close()

if __name__ == '__main__':
    main()
