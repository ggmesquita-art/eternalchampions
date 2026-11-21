#!/usr/bin/env python3
"""t2_peek.py — query both paused debug servers for key RAM words per frame."""
import argparse, json, socket

def rpc(sock, cmd, **kw):
    sock.sendall((json.dumps({'cmd': cmd, 'id': 1, **kw}) + '\n').encode())
    buf = b''
    while b'\n' not in buf:
        ch = sock.recv(1 << 22)
        if not ch:
            raise RuntimeError('closed')
        buf += ch
    return json.loads(buf.split(b'\n', 1)[0].decode())

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--porta', type=int, default=4378)
    ap.add_argument('--portb', type=int, default=4379)
    ap.add_argument('--lo', type=int, default=5220)
    ap.add_argument('--hi', type=int, default=5245)
    args = ap.parse_args()
    a = socket.create_connection(('127.0.0.1', args.porta), timeout=30)
    b = socket.create_connection(('127.0.0.1', args.portb), timeout=30)
    addrs = [('FEFA', 0xFEFA), ('FD70', 0xFD70), ('FD72', 0xFD72),
             ('FD74', 0xFD74), ('FD76', 0xFD76), ('9CCC', 0x9CCC),
             ('9CCF', 0x9CCF), ('AD28', 0xAD28), ('AD2E', 0xAD2E),
             ('E524', 0xE524), ('E526', 0xE526), ('E52A', 0xE52A),
             ('ACBA', 0xACBA), ('C8E8', 0xC8E8), ('E528', 0xE528)]
    def word(ram, off):
        return (ram[off] << 8) | ram[off + 1]
    hdr = 'frame ' + ' '.join('%8s' % n for n, _ in addrs)
    for wf in range(args.lo, args.hi + 1):
        ra = rpc(a, 'get_frame', frame=wf, include='wram')
        rb = rpc(b, 'get_frame', frame=wf, include='wram')
        if not ra.get('ok') or not rb.get('ok'):
            print(wf, 'MISS'); continue
        wa = bytes.fromhex(ra['wram']); wb = bytes.fromhex(rb['wram'])
        va = [word(wa, off) for _, off in addrs]
        vb = [word(wb, off) for _, off in addrs]
        mark = '   <<<' if va != vb else ''
        if wf == args.lo:
            print(hdr)
        print('%5d A: %s' % (wf, ' '.join('%8X' % x for x in va)))
        if mark:
            print('%5d B: %s%s' % (wf, ' '.join('%8X' % x for x in vb), mark))
    a.close(); b.close()

if __name__ == '__main__':
    main()
