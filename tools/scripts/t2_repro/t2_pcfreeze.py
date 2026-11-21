#!/usr/bin/env python3
"""t2_pcfreeze.py — pause A at ~5250, then sample PC/CRAM over time.

If the 68K is frozen in a wait loop, PC stops moving while frames advance.
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

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=4378)
    ap.add_argument('--target', type=int, default=5250)
    ap.add_argument('--samples', type=int, default=6)
    args = ap.parse_args()
    s = connect(args.port)
    rpc(s, 'owner_review', action='play')
    while fi(s) < args.target:
        time.sleep(0.004)
    rpc(s, 'owner_review', action='pause')
    print('paused at', fi(s))
    # sample PC/CRAM/RAM with the game paused (frames don't advance while paused;
    # so play 5 frames between samples)
    regs = rpc(s, 'get_registers')
    print('regs:', json.dumps(regs)[:300])
    cr = rpc(s, 'read_cram', off=0, len=64)
    h = cr.get('hex', '')
    nz = sum(1 for i in range(0, len(h), 4) if h[i:i+4] != '0000')
    print('CRAM nonzero=%d/64' % nz)
    vb = rpc(s, 'vblank_info')
    print('vblank:', json.dumps(vb)[:300])
    for i in range(args.samples):
        rpc(s, 'owner_review', action='play')
        time.sleep(0.12)
        rpc(s, 'owner_review', action='pause')
        regs = rpc(s, 'get_registers')
        pc = None
        for k in ('pc', 'PC'):
            if k in regs:
                pc = regs[k]
        cr = rpc(s, 'read_cram', off=0, len=64)
        h = cr.get('hex', '')
        nz = sum(1 for i in range(0, len(h), 4) if h[i:i+4] != '0000')
        print('sample %d: frame=%d PC=%s CRAM_nz=%d' % (i, fi(s), pc, nz))
    s.close()

if __name__ == '__main__':
    main()
