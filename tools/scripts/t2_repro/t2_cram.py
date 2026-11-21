#!/usr/bin/env python3
"""t2_cram.py — pause both sides near the screenshot point, dump CRAM/VDP."""
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
    for _ in range(400):
        try:
            return socket.create_connection(('127.0.0.1', port), timeout=10)
        except OSError:
            time.sleep(0.25)
    raise RuntimeError('port %d never came up' % port)

def fi(s):
    r = rpc(s, 'frame_info')
    return r.get('current_frame', -1)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--target', type=int, default=6900)
    args = ap.parse_args()
    a = connect(4378); b = connect(4379)
    print('play:', rpc(a, 'owner_review', action='play'), rpc(b, 'owner_review', action='play'))
    da = db = False
    while not (da and db):
        fa, fb = fi(a), fi(b)
        sys.stdout.write('\rfa=%d fb=%d   ' % (fa, fb)); sys.stdout.flush()
        if not da and fa >= args.target:
            rpc(a, 'owner_review', action='pause'); da = True; print('\npaused A at', fa)
        if not db and fb >= args.target:
            rpc(b, 'owner_review', action='pause'); db = True; print('paused B at', fb)
        time.sleep(0.005)
    for sock, name in ((a, 'A rec'), (b, 'B int')):
        r = rpc(sock, 'read_cram', off=0, len=64)
        rv = rpc(sock, 'read_vram', off=0, len=64)
        print(name, 'cram:', r)
        print(name, 'vram[0:64]:', rv)
    a.close(); b.close()

if __name__ == '__main__':
    main()
