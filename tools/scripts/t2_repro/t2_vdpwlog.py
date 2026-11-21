#!/usr/bin/env python3
"""t2_vdpwlog.py — pause both at arm frame, arm TCP mem-write-log on the VDP
ports + stream vars, play through the divergence window, pause, done."""
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
    return rpc(s, 'frame_info').get('current_frame', -1)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--arm', type=int, default=5100)
    ap.add_argument('--stop', type=int, default=5400)
    ap.add_argument('--frames', type=int, default=400)
    args = ap.parse_args()
    a = connect(4378); b = connect(4379)
    print('play:', rpc(a, 'owner_review', action='play'), rpc(b, 'owner_review', action='play'))
    da = db = False
    while not (da and db):
        fa, fb = fi(a), fi(b)
        sys.stdout.write('\rarm: fa=%d fb=%d   ' % (fa, fb)); sys.stdout.flush()
        if not da and fa >= args.arm:
            rpc(a, 'owner_review', action='pause'); da = True; print('\npaused A at', fa)
        if not db and fb >= args.arm:
            rpc(b, 'owner_review', action='pause'); db = True; print('paused B at', fb)
        time.sleep(0.005)
    addrs = [0x00C00000, 0x00C00002, 0x00C00004, 0x00C00008,
             0xFFFD8E, 0xFFFD8F, 0xFF9CA2, 0xFF9CA3,
             0xFFFD20, 0xFFFD22, 0xFFFD76, 0xFFFD74,
             0xFFFD64, 0xFF936F, 0xFF9CB6, 0xFFE524,
             0xFFE526, 0xFFE52A, 0xFFACBA, 0xFFE52E]
    for sock, name in ((a, 'A'), (b, 'B')):
        r = rpc(sock, 'memory_write_log', action='on', addrs=addrs, frames=args.frames)
        print(name, 'arm:', r)
    print('play:', rpc(a, 'owner_review', action='play'), rpc(b, 'owner_review', action='play'))
    da = db = False
    while not (da and db):
        fa, fb = fi(a), fi(b)
        sys.stdout.write('\rstop: fa=%d fb=%d   ' % (fa, fb)); sys.stdout.flush()
        if not da and fa >= args.stop:
            rpc(a, 'owner_review', action='pause'); da = True; print('\npaused A at', fa)
        if not db and fb >= args.stop:
            rpc(b, 'owner_review', action='pause'); db = True; print('paused B at', fb)
        time.sleep(0.005)
    print('done; logs at mem_write_log_native.log in each CWD')

if __name__ == '__main__':
    main()
