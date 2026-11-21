#!/usr/bin/env python3
"""t2_alive.py — play A to a target frame, then sample RAM counters across
small frame steps to test whether the game is still executing."""
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

def rd(s, off, n=2):
    r = rpc(s, 'read_ram', off=off, len=n)
    h = r.get('hex', '')
    return int(h, 16) if h else 0

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=4378)
    ap.add_argument('--target', type=int, default=5300)
    args = ap.parse_args()
    s = connect(args.port)
    rpc(s, 'owner_review', action='play')
    t0 = time.time()
    while fi(s) < args.target:
        if time.time() - t0 > 300:
            print('timeout waiting'); return
        time.sleep(0.004)
    rpc(s, 'owner_review', action='pause')
    f0 = fi(s)
    def snap(tag):
        fd74 = rd(s, 0xFFFD74); fd70 = rd(s, 0xFFFD70); fd64 = rd(s, 0xFFFD64, 1)
        pc = rpc(s, 'get_registers').get('PC')
        cr = rpc(s, 'read_cram', off=0, len=64).get('hex', '')
        nz = sum(1 for i in range(0, len(cr), 4) if cr[i:i+4] != '0000')
        print('%s: frame=%d FD64=%02X FD74=%04X FD70=%04X PC=%s CRAM_nz=%d' %
              (tag, fi(s), fd64, fd74, fd70, pc, nz))
    snap('T0')
    for i in range(4):
        rpc(s, 'owner_review', action='play')
        time.sleep(0.4)
        rpc(s, 'owner_review', action='pause')
        snap('T%d' % (i+1))
    s.close()

if __name__ == '__main__':
    main()
