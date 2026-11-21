#!/usr/bin/env python3
"""t2_regdiff.py — play both sides to the target frame, pause, then compare
m68k register snapshots per frame across the ring window."""
import argparse, json, socket, sys, time

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
            return socket.create_connection(('127.0.0.1', port), timeout=10)
        except OSError:
            time.sleep(0.25)
    raise RuntimeError('port %d never came up' % port)

def frame_info(s):
    r = rpc(s, 'frame_info')
    if not r.get('ok'):
        raise RuntimeError('frame_info: %r' % r)
    return r

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--porta', type=int, default=4378)
    ap.add_argument('--portb', type=int, default=4379)
    ap.add_argument('--target', type=int, default=5260)
    ap.add_argument('--lo', type=int, default=5220)
    ap.add_argument('--hi', type=int, default=5260)
    args = ap.parse_args()

    a = connect(args.porta)
    b = connect(args.portb)
    print('play A:', rpc(a, 'owner_review', action='play'))
    print('play B:', rpc(b, 'owner_review', action='play'))
    done_a = done_b = False
    while not (done_a and done_b):
        fa = frame_info(a).get('current_frame', -1)
        fb = frame_info(b).get('current_frame', -1)
        sys.stdout.write('\rfa=%d fb=%d (target %d)   ' % (fa, fb, args.target))
        sys.stdout.flush()
        if not done_a and fa >= args.target:
            rpc(a, 'owner_review', action='pause'); done_a = True
            print('\npaused A at %d' % fa)
        if not done_b and fb >= args.target:
            rpc(b, 'owner_review', action='pause'); done_b = True
            print('paused B at %d' % fb)
        time.sleep(0.005)

    for wf in range(args.lo, args.hi + 1):
        ra = rpc(a, 'get_frame', frame=wf)
        rb = rpc(b, 'get_frame', frame=wf)
        if not ra.get('ok') or not rb.get('ok'):
            print('wf %d: A=%s B=%s' % (wf, ra.get('error'), rb.get('error')))
            continue
        ma, mb = ra['m68k'], rb['m68k']
        keys = ['PC', 'SR', 'USP'] + ['D%d' % i for i in range(8)] + ['A%d' % i for i in range(8)]
        diff = [(k, ma[k], mb[k]) for k in keys if ma.get(k) != mb.get(k)]
        tag = '  <<< REGS DIVERGE' if diff else ''
        print('wf %d: A PC=%06X A7=%06X SR=%04X | B PC=%06X A7=%06X SR=%04X%s' %
              (wf, ma.get('PC', 0) & 0xFFFFFF, ma['A'][7] & 0xFFFFFF, ma.get('SR', 0),
               mb.get('PC', 0) & 0xFFFFFF, mb['A'][7] & 0xFFFFFF, mb.get('SR', 0), tag))
        if diff:
            for k, va, vb in diff[:12]:
                print('    %s: A=%08X B=%08X' % (k, va, vb))
    a.close(); b.close()

if __name__ == '__main__':
    main()
