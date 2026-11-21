#!/usr/bin/env python3
"""t2_cram3.py — pause both sides at target frames, dump CRAM + VRAM head.

Two stops: T1 (post-fade) and T2 (end). Dumps CRAM as hex, counts nonzero
colors, and dumps the first 64 bytes of VRAM for context.
"""
import argparse, json, socket, sys, time

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
    raise RuntimeError('port %d never came up' % port)

def fi(s):
    return rpc(s, 'frame_info').get('current_frame', -1)

def pause_at(s, target, label):
    while True:
        f = fi(s)
        if f >= target:
            rpc(s, 'owner_review', action='pause')
            print('  %s paused at %d (target %d)' % (label, f, target))
            return f
        time.sleep(0.004)

def dump(s, name, stop):
    cr = rpc(s, 'read_cram', off=0, len=64)
    h = cr.get('hex', '')
    colors = [h[i:i+4] for i in range(0, len(h), 4)]
    nz = sum(1 for c in colors if c != '0000')
    vr = rpc(s, 'read_vram', off=0, len=64)
    print('  [%s@%d] CRAM nonzero=%d/64  first8=%s' %
          (name, stop, nz, ' '.join(colors[:8])))
    print('  [%s@%d] VRAM[0:64]=%s' % (name, stop, vr.get('hex', '')[:128]))
    return nz

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--t1', type=int, default=5250)
    ap.add_argument('--t2', type=int, default=6900)
    args = ap.parse_args()
    a = connect(4378); b = connect(4379)
    print('play:', rpc(a, 'owner_review', action='play').get('ok'),
          rpc(b, 'owner_review', action='play').get('ok'))
    fa = pause_at(a, args.t1, 'A rec')
    fb = pause_at(b, args.t1, 'B int')
    dump(a, 'A rec', fa); dump(b, 'B int', fb)
    rpc(a, 'owner_review', action='play'); rpc(b, 'owner_review', action='play')
    fa = pause_at(a, args.t2, 'A rec')
    fb = pause_at(b, args.t2, 'B int')
    dump(a, 'A rec', fa); dump(b, 'B int', fb)
    a.close(); b.close()

if __name__ == '__main__':
    main()
