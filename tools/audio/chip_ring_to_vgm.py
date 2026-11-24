#!/usr/bin/env python3
"""tools/audio/chip_ring_to_vgm.py — converte o dump chip_ring.txt (CHIP-TRACE,
GEN_DEV_TRACE) em um arquivo .vgm reproduzível (VGMPlay, foobar, etc.).

Eixo de tempo: wf (wall frame, 59.94 Hz) + sl (scanline) — independe do
back-end (nativo/oráculo) e basta para a música (tolerância de ±1 linha).

Uso:
    python tools/audio/chip_ring_to_vgm.py chip_ring.txt -o titulo.vgm
"""

from __future__ import annotations

import argparse
import re
import struct

FM_LINE = re.compile(
    r"f=(\d+) sl=(\d+) FM\s+p(\d) \$([0-9A-Fa-f]{2}) "
    r"mc=\d+ wf=(\d+) pcz=\$[0-9A-Fa-f]+"
)
PSG_LINE = re.compile(
    r"f=(\d+) sl=(\d+) PSG \$([0-9A-Fa-f]{2}) "
    r"mc=\d+ wf=(\d+) pcz=\$[0-9A-Fa-f]+"
)

# NTSC Mega Drive: 262 linhas por quadro a ~59.94 Hz
LINE_US = 1_000_000.0 / 59.92275 / 262.0
YM2612_CLK = 7_670_454
PSG_CLK = 3_579_545
VGM_RATE = 44100.0


def time_us(wf: int, sl: int) -> float:
    return wf * 262.0 * LINE_US + sl * LINE_US


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("ring", help="caminho do chip_ring.txt")
    ap.add_argument("-o", "--out", default="capture.vgm")
    ap.add_argument("--start-wf", type=int, default=0, help="descarta eventos antes deste wall frame")
    ap.add_argument("--end-wf", type=int, default=0, help="descarta eventos depois deste wall frame (0 = até o fim)")
    args = ap.parse_args()

    events: list[tuple[int, int, int, int]] = []  # (t_us, port, val, psg?)
    with open(args.ring, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = FM_LINE.search(line)
            if m:
                wf = int(m.group(5)); sl = int(m.group(2))
                events.append((time_us(wf, sl), int(m.group(3)), int(m.group(4), 16), 0))
                continue
            m = PSG_LINE.search(line)
            if m:
                wf = int(m.group(4)); sl = int(m.group(2))
                events.append((time_us(wf, sl), 0, int(m.group(3), 16), 1))
    if not events:
        print("nenhum evento encontrado (o build tem GEN_DEV_TRACE?)")
        return 1
    if args.start_wf:
        cutoff = args.start_wf * 262.0 * LINE_US
        events = [e for e in events if e[0] >= cutoff]
    if args.end_wf:
        cutoff = args.end_wf * 262.0 * LINE_US
        events = [e for e in events if e[0] < cutoff]

    body = bytearray()
    total_samples = 0
    last_t = events[0][0]
    pending_fm: dict[int, int] = {}  # part -> addr aguardando data

    def wait(delta_us: float) -> None:
        nonlocal total_samples
        n = int(round(delta_us * VGM_RATE / 1_000_000.0))
        while n > 0:
            if n > 65535:
                body.append(0x61); body.extend(struct.pack("<H", 65535))
                total_samples += 65535; n -= 65535
            else:
                body.append(0x61); body.extend(struct.pack("<H", n))
                total_samples += n; n = 0

    for t, port, val, is_psg in events:
        wait(t - last_t)
        last_t = t
        if is_psg:
            body.append(0x50); body.append(val)          # SN76489 write
        else:
            part = port & 0x02
            if port & 0x01 == 0:                          # addr write
                pending_fm[part] = val
            else:                                         # data write
                addr = pending_fm.get(part, 0)
                pending_fm.pop(part, None)
                body.append(0x52 if part == 0 else 0x53)
                body.append(addr); body.append(val)

    # Cabeçalho VGM 1.51 (offsets RELATIVOS à posição do próprio campo, como o
    # spec manda; o VGMPlay soma a base na leitura):
    #   0x04 EOF (relativo a 0x04) | 0x0C/0x10 clocks PSG/YM2413
    #   0x2C clock YM2612 (obrigatório p/ o player detectar o chip!)
    #   0x34 data offset (relativo a 0x34: 0x0C = dados em 0x40)
    eof_abs = 0x40 + len(body)
    header = bytearray(0x40)
    header[0:4] = b"Vgm "
    struct.pack_into("<I", header, 0x04, eof_abs - 0x04)   # EOF relativo
    struct.pack_into("<I", header, 0x08, 0x151)           # versão 1.51
    struct.pack_into("<I", header, 0x0C, int(PSG_CLK))    # SN76489
    struct.pack_into("<I", header, 0x10, 0)               # YM2413
    struct.pack_into("<I", header, 0x14, 0)               # GD3 offset
    struct.pack_into("<I", header, 0x18, total_samples)   # total de samples
    struct.pack_into("<I", header, 0x1C, 0)               # loop offset
    struct.pack_into("<I", header, 0x20, 0)               # loop samples
    struct.pack_into("<I", header, 0x2C, int(YM2612_CLK)) # YM2612 (essencial)
    struct.pack_into("<I", header, 0x30, 0)               # YM2151
    struct.pack_into("<I", header, 0x34, 0x0C)            # data offset rel.
    with open(args.out, "wb") as f:
        f.write(header)
        f.write(b"\x00" * (0x40 - len(header)))
        f.write(body)
        f.write(b"\x66")  # end of sound data
    print(f"{args.out}: {len(events)} eventos, {total_samples} samples "
          f"({total_samples / VGM_RATE:.1f} s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
