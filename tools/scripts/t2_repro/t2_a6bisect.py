#!/usr/bin/env python3
"""t2_a6bisect.py — step the recomp 2 frames at a time and watch A6.

Finds the wall-frame window where the VDP base register A6 stops being
0xC00000 (the T2 black-screen clobber). Uses owner_review + run_frames.
"""
import argparse, json, socket, time

def rpc(sock, cmd, **kw):
    sock.sendall((json.dumps({'cmd': cmd, 'id': 1, **kw}) + '\n').encode())
    buf = b''
    while b'\n' not in buf:
        ch = sock.recv(1 << 22)
        if not ch:
            raise RuntimeError('closed')
        buf += ch
    return json.loads(buf.split(b'\n', 1)[0].decode())

def connect(port):
    for _ in range(600):
        try:
            return socket.create_connection(('127.0.0.1', port), timeout=10)
        except OSError:
            time.sleep(0.25)
    raise RuntimeError('no port')

def fi(s):
    return rpc(s, 'frame_info').get('current_frame', -1)

def a6(s):
    return rpc(s, 'get_registers').get('A6')

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=4378)
    ap.add_argument('--start', type=int, default=5200)
    ap.add_argument('--step', type=int, default=2)
    ap.add_argument('--maxf', type=int, default=5320)
    args = ap.parse_args()
    s = connect(args.port)
    rpc(s, 'owner_review', action='pause')
    rpc(s, 'owner_review', action='play')
    while fi(s) < args.start:
        time.sleep(0.001)
    rpc(s, 'owner_review', action='pause')
    prev = a6(s)
    print('start: frame=%d A6=%06X' % (fi(s), prev))
    while True:
        f0 = fi(s)
        if f0 >= args.maxf:
            print('reached maxf, A6 still %06X' % prev)
            break
        rpc(s, 'owner_review', action='step', frames=args.step)
        # step completes synchronously while paused; frame advances
        t0 = time.time()
        while fi(s) < f0 + args.step:
            if time.time() - t0 > 30:
                print('timeout stepping'); return
            time.sleep(0.01)
        rpc(s, 'owner_review', action='pause')
        cur = a6(s)
        f1 = fi(s)
        if cur != prev:
            print('A6 CHANGED: frame %d -> %d: %06X -> %06X' % (f0, f1, prev, cur))
            # read all regs for context
            regs = rpc(s, 'get_registers')
            for k in ('A0','A1','A2','A3','A4','A5','A6','A7','PC','SR'):
                if k in regs:
                    print('  %s=%s' % (k, regs[k]))
            break
        prev = cur
        print('frame=%d A6=%06X' % (f1, cur))
    s.close()

if __name__ == '__main__':
    main()
