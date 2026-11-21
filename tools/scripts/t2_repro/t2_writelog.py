#!/usr/bin/env python3
"""t2_writelog.py — arm the mem-write-log on both T2 sides around the
first-divergence window and freeze them for log collection.

Flow per side: play -> poll to arm_frame -> pause -> arm mem_write_log
(ranges, frames=400) -> play -> poll to stop_frame -> pause.
"""
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

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--porta', type=int, default=4378)
    ap.add_argument('--portb', type=int, default=4379)
    ap.add_argument('--arm-frame', type=int, default=5040)
    ap.add_argument('--stop-frame', type=int, default=5360)
    ap.add_argument('--frames', type=int, default=400)
    args = ap.parse_args()

    a = connect(args.porta)
    b = connect(args.portb)
    print('A:', {k: v for k, v in frame_info(a).items() if 'frame' in k})
    print('B:', {k: v for k, v in frame_info(b).items() if 'frame' in k})

    print('play A:', rpc(a, 'owner_review', action='play'))
    print('play B:', rpc(b, 'owner_review', action='play'))

    # ---- phase 1: reach arm frame ----
    done_a = done_b = False
    while not (done_a and done_b):
        fa = frame_info(a).get('current_frame', -1)
        fb = frame_info(b).get('current_frame', -1)
        sys.stdout.write('\rarm: fa=%d fb=%d (target %d)   ' % (fa, fb, args.arm_frame))
        sys.stdout.flush()
        if not done_a and fa >= args.arm_frame:
            rpc(a, 'owner_review', action='pause'); done_a = True
            print('\npaused A at %d' % fa)
        if not done_b and fb >= args.arm_frame:
            rpc(b, 'owner_review', action='pause'); done_b = True
            print('paused B at %d' % fb)
        time.sleep(0.005)

    # ---- arm the write log on both ----
    ranges = [['0xFFAD00', '0xFFAF80'],
              ['0xFF9C80', '0xFF9D00'],
              ['0xFFAC80', '0xFFAD00'],
              ['0xFFFF00', '0xFFFFFF']]
    for sock, name in ((a, 'A'), (b, 'B')):
        # TCP arming uses single addrs (lo==hi); watch region endpoints +
        # a dense net of addrs is not possible, so use the address list
        # variant: watch the first byte of each divergent cluster.
        # Simpler: watch full ranges via the addresses form? The TCP form
        # only sets lo==hi per entry (32 max). We pass range endpoints and
        # rely on lo<=hi filtering — the TCP handler sets lo[i]==hi[i], so
        # pass the endpoints and a few interior addrs instead.
        print('%s arm: %s' % (name, rpc(sock, 'memory_write_log',
                                        action='on',
                                        addrs=[0xFFAD00, 0xFFAD22, 0xFFAD28, 0xFFAD2E,
                                               0xFFAD3F, 0xFFAD47, 0xFFAD4F, 0xFFAD67,
                                               0xFFADC0, 0xFFADC6, 0xFFADD4, 0xFFADD7,
                                               0xFFADE7, 0xFFAE21, 0xFFAE4C, 0xFFAE58,
                                               0xFF9CCF, 0xFFACCB,
                                               0xFFFF88, 0xFFFF90, 0xFFFFA0, 0xFFFFC0],
                                        frames=args.frames)))

    # ---- phase 2: run through the divergence window ----
    print('play A:', rpc(a, 'owner_review', action='play'))
    print('play B:', rpc(b, 'owner_review', action='play'))
    done_a = done_b = False
    while not (done_a and done_b):
        fa = frame_info(a).get('current_frame', -1)
        fb = frame_info(b).get('current_frame', -1)
        sys.stdout.write('\rstop: fa=%d fb=%d (target %d)   ' % (fa, fb, args.stop_frame))
        sys.stdout.flush()
        if not done_a and fa >= args.stop_frame:
            rpc(a, 'owner_review', action='pause'); done_a = True
            print('\npaused A at %d' % fa)
        if not done_b and fb >= args.stop_frame:
            rpc(b, 'owner_review', action='pause'); done_b = True
            print('paused B at %d' % fb)
        time.sleep(0.005)

    print('done. log files: mem_write_log_native.log in each process CWD')
    a.close(); b.close()

if __name__ == '__main__':
    main()
