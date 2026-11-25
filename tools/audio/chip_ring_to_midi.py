#!/usr/bin/env python3
"""tools/audio/chip_ring_to_midi.py — transcrição fiel por canal do fluxo de
registradores YM2612/SN76489 (chip_ring.txt) para MIDI Tipo 1 multi-track.

Mapeia cada canal físico do Mega Drive para uma trilha MIDI nomeada e
determinística, preservando timing exato, pitch, velocity e duração:

  fm0..fm5   -> canais MIDI 0..5  (trilhas "fm0".."fm5")
  psg0..psg2 -> canais MIDI 6..8  (trilhas "psg0".."psg2")
  dac        -> canal MIDI 9      (trilha "dac", percussão GM)
  psg_noise  -> canal MIDI 9      (trilha "psg_noise", percussão GM)

Por padrão, notas FM curtas (<60 ms) NÃO são desviadas para percussão no canal
10 (correção do falso-positivo em arpejos/leads rápidos como em 03_syria_blade).
Use --legacy-fm-perc apenas para reproduzir/comparar o comportamento antigo.

Uso:
    python tools/audio/chip_ring_to_midi.py chip_ring.txt out.mid
    python tools/audio/chip_ring_to_midi.py chip_ring.txt -o out.mid \
        [--start-wf N] [--end-wf N] [--per-channel-dir DIR] [--legacy-fm-perc]
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import math
import os
from pathlib import Path
import re
import struct
from typing import Iterable

FM_LINE = re.compile(
    r"f=\d+ sl=(\d+) FM\s+p(\d) \$([0-9A-Fa-f]{2}) mc=\d+ wf=(\d+)"
)
PSG_LINE = re.compile(
    r"f=\d+ sl=(\d+) PSG \$([0-9A-Fa-f]{2}) mc=\d+ wf=(\d+)"
)
APP_FM_LINE = re.compile(
    r"f=(\d+)\s+APP\s+FM\s+p(\d)\s+\$([0-9A-Fa-f]{2})\s+cyc=(\d+)"
)
APP_PSG_LINE = re.compile(
    r"f=(\d+)\s+APP\s+PSG\s+\$([0-9A-Fa-f]{2})\s+cyc=(\d+)"
)

YM_CLK = 7_670_454.0
PSG_CLK = 3_579_545.0
NTSC_FPS = 59.92275
SCANLINES_PER_FRAME = 262.0
LINE_US = 1_000_000.0 / NTSC_FPS / SCANLINES_PER_FRAME
FRAME_US = 1_000_000.0 / NTSC_FPS
M68K_FRAME_CYCLES = 127_841.0
MASTER_FRAME_CYCLES = 895_780.0
DAC_VOICE_MAX_US = 500_000.0

PPQN = 480
TEMPO_US = 500_000  # 120 BPM de referência
US_PER_TICK = TEMPO_US / PPQN

PERC_MIDI_CH = 9  # Canal 10 GM (0-indexed = 9)
LEGACY_DRUM_MIN_US = 60_000.0

CHANNEL_ORDER: tuple[str, ...] = (
    "fm0",
    "fm1",
    "fm2",
    "fm3",
    "fm4",
    "fm5",
    "dac",
    "psg0",
    "psg1",
    "psg2",
    "psg_noise",
)

CHANNEL_MIDI_MAP: dict[str, int] = {
    "fm0": 0,
    "fm1": 1,
    "fm2": 2,
    "fm3": 3,
    "fm4": 4,
    "fm5": 5,
    "psg0": 6,
    "psg1": 7,
    "psg2": 8,
    "dac": PERC_MIDI_CH,
    "psg_noise": PERC_MIDI_CH,
}

# Mapeamento reverso usado ao importar MIDIs legados de trilha única
MIDI_CH_FALLBACK_NAME: dict[int, str] = {
    0: "fm0",
    1: "fm1",
    2: "fm2",
    3: "fm3",
    4: "fm4",
    5: "fm5",
    6: "psg0",
    7: "psg1",
    8: "psg2",
    9: "dac",
    10: "psg_noise",
}

# YM2612 Key-On/Off (reg 0x28): bits 0..2 selecionam canal físico
YM_KEYON_CH_MAP: dict[int, int] = {
    0: 0,
    1: 1,
    2: 2,
    4: 3,
    5: 4,
    6: 5,
}

# Operadores portadores por algoritmo YM2612 (slots na ordem de reg 0x40: 0=OP1, 1=OP3, 2=OP2, 3=OP4)
YM_CARRIER_SLOTS: dict[int, tuple[int, ...]] = {
    0: (3,),
    1: (3,),
    2: (3,),
    3: (3,),
    4: (2, 3),
    5: (1, 2, 3),
    6: (1, 2, 3),
    7: (0, 1, 2, 3),
}


@dataclass(slots=True)
class NoteEvent:
    channel: str
    start_s: float
    end_s: float
    note: int
    velocity: int
    midi_channel: int = 0

    @property
    def duration_s(self) -> float:
        return max(0.0, self.end_s - self.start_s)


def time_us(wf: int, sl: int) -> float:
    return wf * SCANLINES_PER_FRAME * LINE_US + sl * LINE_US


def mul_to_factor(mul: int) -> float:
    return 0.5 if (mul & 0x0F) == 0 else float(mul & 0x0F)


def fnum_to_midi(block: int, fnum: int, mul_factor: float = 1.0) -> int:
    if fnum <= 0 or mul_factor <= 0.0:
        return 0
    f_base = fnum * (YM_CLK / 144.0) / (2 ** (21 - block))
    freq = f_base * mul_factor
    if freq <= 0.0:
        return 0
    return max(0, min(127, int(round(12.0 * math.log2(freq / 440.0) + 69.0))))


def psg_period_to_midi(period: int) -> int:
    if period <= 0:
        return 0
    freq = PSG_CLK / (32.0 * period)
    if freq <= 0.0:
        return 0
    return max(0, min(127, int(round(12.0 * math.log2(freq / 440.0) + 69.0))))


class _FmChan:
    __slots__ = ("fnum", "block", "msb_latch", "alg", "tl", "mul", "sounding")

    def __init__(self) -> None:
        self.fnum = 0
        self.block = 0
        self.msb_latch = 0
        self.alg = 0
        self.tl = [32, 32, 32, 32]  # OP1, OP3, OP2, OP4
        self.mul = [1, 1, 1, 1]   # OP1, OP3, OP2, OP4 (reg 0x30..0x3E bits 0..3)
        self.sounding: tuple[float, int, int, bool] | None = None  # (t_on_us, note, vel, is_perc)

    def _active_carriers(self) -> list[int]:
        carriers = YM_CARRIER_SLOTS.get(self.alg & 0x07, (3,))
        min_tl = min(self.tl[i] for i in carriers)
        active = [
            i for i in carriers if self.tl[i] <= min_tl + 18 and self.tl[i] < 96
        ]
        if not active:
            active = [carriers[-1]]
        return active

    def carrier_mul_factor(self) -> float:
        return min(mul_to_factor(self.mul[i]) for i in self._active_carriers())

    def is_noise_perc_patch(self) -> bool:
        """Detecta pela física do patch FM (sem usar duração) percussão metálica/ruído FM."""
        alg = self.alg & 0x07
        if alg > 4:
            return False
        carriers = YM_CARRIER_SLOTS.get(alg, (3,))
        min_tl = min(self.tl[i] for i in carriers)
        active = [
            i for i in carriers if self.tl[i] <= min_tl + 18 and self.tl[i] < 96
        ]
        if not active or not any((self.mul[i] & 0x0F) >= 12 for i in active):
            return False
        modulators = [i for i in range(4) if i not in carriers]
        intense_mods = sum(
            1
            for i in modulators
            if (self.mul[i] & 0x0F) >= 10 and self.tl[i] <= 15
        )
        return intense_mods >= 2

    def current_midi_note(self) -> int:
        return fnum_to_midi(self.block, self.fnum, self.carrier_mul_factor())

    def carrier_velocity(self) -> int:
        carriers = YM_CARRIER_SLOTS.get(self.alg & 0x07, (3,))
        eff_tl = min(self.tl[i] for i in carriers)
        # YM2612 TL: 0 = máximo (0 dB), 127 = silêncio (~-95 dB).
        # Faixa musical típica fica em 0..55; curva suave preserva dinâmica.
        norm = max(0.0, min(1.0, 1.0 - (eff_tl / 96.0)))
        return max(16, min(127, int(round(20 + norm * 107))))


class _PsgToneChan:
    __slots__ = ("period", "vol", "sounding")

    def __init__(self) -> None:
        self.period = 0
        self.vol = 15  # 15 = mudo
        self.sounding: tuple[float, int, int] | None = None

    def velocity(self) -> int:
        if self.vol >= 15:
            return 0
        return max(16, min(127, int(round(18 + ((15 - self.vol) / 15.0) * 109))))


class _DacBurst:
    __slots__ = ("start_us", "last_us", "count", "peak_dev", "zcr", "prev_sign")

    def __init__(self, t_us: float, sample: int) -> None:
        self.start_us = t_us
        self.last_us = t_us
        self.count = 1
        dev = abs(sample - 128)
        self.peak_dev = dev
        self.zcr = 0
        self.prev_sign = 1 if sample >= 128 else -1

    def feed(self, t_us: float, sample: int) -> None:
        self.last_us = t_us
        self.count += 1
        dev = abs(sample - 128)
        if dev > self.peak_dev:
            self.peak_dev = dev
        sign = 1 if sample >= 128 else -1
        if sign != self.prev_sign:
            self.zcr += 1
            self.prev_sign = sign

    def is_voice_stream(self) -> bool:
        return (self.last_us - self.start_us) > DAC_VOICE_MAX_US

    def to_note_event(self, t0_us: float) -> NoteEvent:
        dur_us = max(40_000.0, min(350_000.0, (self.last_us - self.start_us) + 20_000.0))
        zcr_ratio = self.zcr / max(1, self.count)
        # Classificação acústica simples do burst PCM (sem tocar canais FM!)
        if zcr_ratio < 0.09:
            drum_note = 36  # Bass Drum 1 (Kick)
        elif zcr_ratio < 0.22:
            drum_note = 38  # Acoustic Snare
        elif dur_us < 100_000.0:
            drum_note = 42  # Closed Hi-Hat
        else:
            drum_note = 41  # Low Floor Tom / percussão grave
        vel = max(36, min(127, 45 + int(self.peak_dev * 0.65)))
        start_s = max(0.0, (self.start_us - t0_us) / 1_000_000.0)
        end_s = start_s + dur_us / 1_000_000.0
        return NoteEvent(
            channel="dac",
            start_s=start_s,
            end_s=end_s,
            note=drum_note,
            velocity=vel,
            midi_channel=PERC_MIDI_CH,
        )


def _psg_noise_drum_note(noise_reg: int) -> int:
    fb = (noise_reg >> 2) & 0x01  # 1 = ruído branco, 0 = periódico
    nf = noise_reg & 0x03         # 0 = agudo, 1 = médio, 2 = grave, 3 = tone2
    if fb == 0:
        return 39  # Hand Clap / percussão metálica periódica
    if nf == 0:
        return 42  # Closed Hi-Hat
    if nf == 1:
        return 46  # Open Hi-Hat
    return 38      # Acoustic Snare


def parse_chip_ring(
    ring_path: str | os.PathLike[str],
    start_wf: int = 0,
    end_wf: int = 0,
    legacy_fm_perc: bool = False,
    frame_start: int = 0,
    frame_end: int = 0,
    fm_perc_to_dac: bool = True,
) -> tuple[dict[str, list[NoteEvent]], dict[str, int]]:
    """Decodifica chip_ring.txt ou GENESIS_AUDIO_APPLIEDDUMP para eventos de nota por canal físico.

    Retorna (notes_by_channel, stats).
    """
    raw_events: list[tuple[float, str, int, int]] = []
    app_events: list[tuple[int, int, str, int, int]] = []
    max_app_cyc = 0

    with open(ring_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = FM_LINE.search(line)
            if m:
                raw_events.append(
                    (
                        time_us(int(m.group(4)), int(m.group(1))),
                        "fm",
                        int(m.group(2)),
                        int(m.group(3), 16),
                    )
                )
                continue
            m = PSG_LINE.search(line)
            if m:
                raw_events.append(
                    (
                        time_us(int(m.group(3)), int(m.group(1))),
                        "psg",
                        0,
                        int(m.group(2), 16),
                    )
                )
                continue
            m = APP_FM_LINE.search(line)
            if m:
                cyc = int(m.group(4))
                if cyc > max_app_cyc:
                    max_app_cyc = cyc
                app_events.append(
                    (
                        int(m.group(1)),
                        cyc,
                        "fm",
                        int(m.group(2)),
                        int(m.group(3), 16),
                    )
                )
                continue
            m = APP_PSG_LINE.search(line)
            if m:
                cyc = int(m.group(3))
                if cyc > max_app_cyc:
                    max_app_cyc = cyc
                app_events.append(
                    (
                        int(m.group(1)),
                        cyc,
                        "psg",
                        0,
                        int(m.group(2), 16),
                    )
                )

    eff_start = frame_start if frame_start > 0 else start_wf
    eff_end = frame_end if frame_end > 0 else end_wf

    if app_events and not raw_events:
        frame_cycles = MASTER_FRAME_CYCLES if max_app_cyc > 135_000 else M68K_FRAME_CYCLES
        for frame_idx, cyc, kind, port, val in app_events:
            if eff_start > 0 and frame_idx < eff_start:
                continue
            if eff_end > 0 and frame_idx >= eff_end:
                continue
            t_us = frame_idx * FRAME_US + (min(float(cyc), frame_cycles) / frame_cycles) * FRAME_US
            raw_events.append((t_us, kind, port, val))
    elif raw_events:
        if eff_start > 0:
            cut_start = eff_start * SCANLINES_PER_FRAME * LINE_US
            raw_events = [e for e in raw_events if e[0] >= cut_start]
        if eff_end > 0:
            cut_end = eff_end * SCANLINES_PER_FRAME * LINE_US
            raw_events = [e for e in raw_events if e[0] < cut_end]

    if not raw_events:
        return {ch: [] for ch in CHANNEL_ORDER}, {"raw_events": 0}

    t0_us = raw_events[0][0]
    t_last_us = raw_events[-1][0]

    fm = [_FmChan() for _ in range(6)]
    psg_tone = [_PsgToneChan() for _ in range(3)]
    psg_noise_reg = 0x04  # branco, taxa 0
    psg_noise_vol = 15
    psg_noise_on: tuple[float, int, int] | None = None  # (t_on_us, note, vel)
    psg_latch_ch = 0
    psg_latch_is_vol = False

    pending_fm_addr = [0, 0]
    dac_enabled = False
    active_dac: _DacBurst | None = None

    notes: dict[str, list[NoteEvent]] = {ch: [] for ch in CHANNEL_ORDER}
    legacy_redirected = 0
    fm_noise_perc_redirected = 0

    def emit_fm_note(
        ch_idx: int,
        t_on: float,
        t_off: float,
        note: int,
        vel: int,
        is_perc: bool = False,
    ) -> None:
        nonlocal legacy_redirected, fm_noise_perc_redirected
        dur_us = max(10_000.0, t_off - t_on)
        start_s = max(0.0, (t_on - t0_us) / 1_000_000.0)
        end_s = start_s + dur_us / 1_000_000.0
        if is_perc and not fm_perc_to_dac:
            return  # caminho por voz (voice_catalog): a nota FM fica na trilha da voz, nao em "dac"
        if is_perc:
            dur_ms = dur_us / 1000.0
            if dur_ms < 140.0:
                pnote = 42  # Closed Hi-Hat
            elif dur_ms < 350.0:
                pnote = 46  # Open Hi-Hat
            else:
                pnote = 49  # Crash Cymbal 1
            notes["dac"].append(
                NoteEvent(
                    channel="dac",
                    start_s=start_s,
                    end_s=end_s,
                    note=pnote,
                    velocity=vel,
                    midi_channel=PERC_MIDI_CH,
                )
            )
            fm_noise_perc_redirected += 1
            return
        if not (12 <= note <= 120):
            return
        if legacy_fm_perc and dur_us < LEGACY_DRUM_MIN_US:
            pnote = 36 if note < 40 else (38 if note < 60 else 42)
            notes["dac"].append(
                NoteEvent(
                    channel="dac",
                    start_s=start_s,
                    end_s=end_s,
                    note=pnote,
                    velocity=vel,
                    midi_channel=PERC_MIDI_CH,
                )
            )
            legacy_redirected += 1
            return
        ch_name = f"fm{ch_idx}"
        notes[ch_name].append(
            NoteEvent(
                channel=ch_name,
                start_s=start_s,
                end_s=end_s,
                note=note,
                velocity=vel,
                midi_channel=CHANNEL_MIDI_MAP[ch_name],
            )
        )

    def emit_psg_tone(ch_idx: int, t_on: float, t_off: float, note: int, vel: int) -> None:
        if not (12 <= note <= 120):
            return
        dur_us = max(10_000.0, t_off - t_on)
        start_s = max(0.0, (t_on - t0_us) / 1_000_000.0)
        ch_name = f"psg{ch_idx}"
        notes[ch_name].append(
            NoteEvent(
                channel=ch_name,
                start_s=start_s,
                end_s=start_s + dur_us / 1_000_000.0,
                note=note,
                velocity=vel,
                midi_channel=CHANNEL_MIDI_MAP[ch_name],
            )
        )

    def emit_psg_noise(t_on: float, t_off: float, note: int, vel: int) -> None:
        dur_us = max(25_000.0, min(400_000.0, t_off - t_on))
        start_s = max(0.0, (t_on - t0_us) / 1_000_000.0)
        notes["psg_noise"].append(
            NoteEvent(
                channel="psg_noise",
                start_s=start_s,
                end_s=start_s + dur_us / 1_000_000.0,
                note=note,
                velocity=vel,
                midi_channel=PERC_MIDI_CH,
            )
        )

    for t, kind, port, val in raw_events:
        if kind == "fm":
            part = 1 if (port & 0x02) else 0
            if (port & 0x01) == 0:
                pending_fm_addr[part] = val
                continue
            reg = pending_fm_addr[part]

            if part == 0 and reg == 0x2B:
                dac_enabled = bool(val & 0x80)
                if not dac_enabled and active_dac is not None:
                    if not active_dac.is_voice_stream():
                        notes["dac"].append(active_dac.to_note_event(t0_us))
                    active_dac = None
            elif part == 0 and reg == 0x2A:
                if active_dac is None:
                    active_dac = _DacBurst(t, val)
                elif (t - active_dac.last_us) > 18_000.0:
                    if not active_dac.is_voice_stream():
                        notes["dac"].append(active_dac.to_note_event(t0_us))
                    active_dac = _DacBurst(t, val)
                else:
                    active_dac.feed(t, val)
            elif part == 0 and reg == 0x28:
                ch_sel = val & 0x07
                g = YM_KEYON_CH_MAP.get(ch_sel)
                if g is None:
                    continue
                if g == 5 and dac_enabled:
                    continue
                op_mask = (val >> 4) & 0x0F
                ch_st = fm[g]
                if op_mask != 0:
                    if ch_st.sounding is not None:
                        t_on, old_note, old_vel, old_is_perc = ch_st.sounding
                        emit_fm_note(g, t_on, t, old_note, old_vel, old_is_perc)
                    is_perc = ch_st.is_noise_perc_patch()
                    note = ch_st.current_midi_note()
                    if is_perc or (12 <= note <= 120):
                        ch_st.sounding = (t, note, ch_st.carrier_velocity(), is_perc)
                    else:
                        ch_st.sounding = None
                else:
                    if ch_st.sounding is not None:
                        t_on, old_note, old_vel, old_is_perc = ch_st.sounding
                        emit_fm_note(g, t_on, t, old_note, old_vel, old_is_perc)
                        ch_st.sounding = None
            elif 0xA4 <= reg <= 0xA6:
                g = part * 3 + (reg - 0xA4)
                fm[g].msb_latch = val
            elif 0xA0 <= reg <= 0xA2:
                g = part * 3 + (reg - 0xA0)
                ch_st = fm[g]
                ch_st.fnum = ((ch_st.msb_latch & 0x07) << 8) | val
                ch_st.block = (ch_st.msb_latch >> 3) & 0x07
                if ch_st.sounding is not None:
                    t_on, old_note, old_vel, old_is_perc = ch_st.sounding
                    if not old_is_perc:
                        new_note = ch_st.current_midi_note()
                        if 12 <= new_note <= 120:
                            if (t - t_on) < 20_000.0:
                                ch_st.sounding = (t_on, new_note, old_vel, False)
                            elif abs(new_note - old_note) >= 1:
                                emit_fm_note(g, t_on, t, old_note, old_vel, False)
                                ch_st.sounding = (t, new_note, ch_st.carrier_velocity(), False)
            elif 0xB0 <= reg <= 0xB2:
                g = part * 3 + (reg - 0xB0)
                fm[g].alg = val & 0x07
            elif 0x30 <= reg <= 0x3E and (reg & 0x03) != 0x03:
                ch_local = reg & 0x03
                op_slot = (reg - 0x30) >> 2
                g = part * 3 + ch_local
                fm[g].mul[op_slot] = val & 0x0F
            elif 0x40 <= reg <= 0x4E and (reg & 0x03) != 0x03:
                ch_local = reg & 0x03
                op_slot = (reg - 0x40) >> 2
                g = part * 3 + ch_local
                fm[g].tl[op_slot] = val & 0x7F
        else:
            # SN76489 PSG
            if val & 0x80:
                psg_latch_ch = (val >> 5) & 0x03
                psg_latch_is_vol = bool(val & 0x10)
                data4 = val & 0x0F
                if psg_latch_is_vol:
                    if psg_latch_ch < 3:
                        tch = psg_tone[psg_latch_ch]
                        prev_vol = tch.vol
                        tch.vol = data4
                        if data4 == 15:
                            if tch.sounding is not None:
                                t_on, old_n, old_v = tch.sounding
                                emit_psg_tone(psg_latch_ch, t_on, t, old_n, old_v)
                                tch.sounding = None
                        else:
                            note = psg_period_to_midi(tch.period)
                            vel = tch.velocity()
                            if tch.sounding is None:
                                if 12 <= note <= 120:
                                    tch.sounding = (t, note, vel)
                            elif prev_vol - data4 >= 4 and (t - tch.sounding[0]) >= 35_000.0:
                                # Re-ataque perceptível no envelope de volume PSG
                                t_on, old_n, old_v = tch.sounding
                                emit_psg_tone(psg_latch_ch, t_on, t, old_n, old_v)
                                if 12 <= note <= 120:
                                    tch.sounding = (t, note, vel)
                                else:
                                    tch.sounding = None
                    else:
                        prev_vol = psg_noise_vol
                        psg_noise_vol = data4
                        if data4 == 15:
                            if psg_noise_on is not None:
                                t_on, old_n, old_v = psg_noise_on
                                emit_psg_noise(t_on, t, old_n, old_v)
                                psg_noise_on = None
                        else:
                            vel = max(20, min(127, int(round(20 + ((15 - data4) / 15.0) * 107))))
                            dnote = _psg_noise_drum_note(psg_noise_reg)
                            if psg_noise_on is None:
                                psg_noise_on = (t, dnote, vel)
                            elif prev_vol - data4 >= 3 and (t - psg_noise_on[0]) >= 30_000.0:
                                t_on, old_n, old_v = psg_noise_on
                                emit_psg_noise(t_on, t, old_n, old_v)
                                psg_noise_on = (t, dnote, vel)
                else:
                    if psg_latch_ch < 3:
                        tch = psg_tone[psg_latch_ch]
                        tch.period = (tch.period & 0x3F0) | data4
                    else:
                        psg_noise_reg = val & 0x07
            else:
                # Data byte: continua canal/tipo latched
                if psg_latch_is_vol:
                    data4 = val & 0x0F
                    if psg_latch_ch < 3:
                        tch = psg_tone[psg_latch_ch]
                        tch.vol = data4
                        if data4 == 15 and tch.sounding is not None:
                            t_on, old_n, old_v = tch.sounding
                            emit_psg_tone(psg_latch_ch, t_on, t, old_n, old_v)
                            tch.sounding = None
                    else:
                        psg_noise_vol = data4
                        if data4 == 15 and psg_noise_on is not None:
                            t_on, old_n, old_v = psg_noise_on
                            emit_psg_noise(t_on, t, old_n, old_v)
                            psg_noise_on = None
                elif psg_latch_ch < 3:
                    tch = psg_tone[psg_latch_ch]
                    tch.period = (tch.period & 0x00F) | ((val & 0x3F) << 4)
                    if tch.vol < 15:
                        note = psg_period_to_midi(tch.period)
                        vel = tch.velocity()
                        if 12 <= note <= 120:
                            if tch.sounding is None:
                                tch.sounding = (t, note, vel)
                            elif tch.sounding[1] != note:
                                t_on, old_n, old_v = tch.sounding
                                if (t - t_on) < 20_000.0:
                                    tch.sounding = (t_on, note, old_v)
                                else:
                                    emit_psg_tone(psg_latch_ch, t_on, t, old_n, old_v)
                                    tch.sounding = (t, note, vel)

    # Fecha notas ainda abertas no final do dump
    t_close_us = t_last_us + 250_000.0
    for g, ch_st in enumerate(fm):
        if ch_st.sounding is not None:
            t_on, old_note, old_vel, old_is_perc = ch_st.sounding
            emit_fm_note(g, t_on, t_close_us, old_note, old_vel, old_is_perc)
            ch_st.sounding = None
    for i, tch in enumerate(psg_tone):
        if tch.sounding is not None:
            t_on, old_n, old_v = tch.sounding
            emit_psg_tone(i, t_on, t_close_us, old_n, old_v)
            tch.sounding = None
    if psg_noise_on is not None:
        t_on, old_n, old_v = psg_noise_on
        emit_psg_noise(t_on, t_close_us, old_n, old_v)
    if active_dac is not None and not active_dac.is_voice_stream():
        notes["dac"].append(active_dac.to_note_event(t0_us))

    for ch in CHANNEL_ORDER:
        notes[ch].sort(key=lambda n: (n.start_s, n.note))

    stats = {
        "raw_events": len(raw_events),
        "total_notes": sum(len(v) for v in notes.values()),
        "legacy_fm_perc_redirected": legacy_redirected,
        "fm_noise_perc_redirected": fm_noise_perc_redirected,
        "duration_ms": int(round((t_last_us - t0_us) / 1000.0)),
    }
    return notes, stats


# ---------------------------------------------------------------------------
# Leitor/Escritor SMF (Standard MIDI File Type 0/1) puro em Python
# Funciona identicamente no Windows e WSL sem exigir pacotes externos.
# ---------------------------------------------------------------------------

def _encode_vlq(value: int) -> bytes:
    value = max(0, int(value))
    out = [value & 0x7F]
    value >>= 7
    while value > 0:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.reverse()
    return bytes(out)


def _decode_vlq(data: bytes, pos: int) -> tuple[int, int]:
    val = 0
    while pos < len(data):
        b = data[pos]
        pos += 1
        val = (val << 7) | (b & 0x7F)
        if not (b & 0x80):
            break
    return val, pos


def _build_track_chunk(events: list[tuple[int, bytes]]) -> bytes:
    """Serializa lista de (abs_tick, raw_msg_bytes) em chunk MTrk."""
    events_sorted = sorted(events, key=lambda x: x[0])
    body = bytearray()
    last_tick = 0
    for tick, payload in events_sorted:
        delta = max(0, tick - last_tick)
        last_tick = tick
        body.extend(_encode_vlq(delta))
        body.extend(payload)
    # End of Track meta event
    body.extend(b"\x00\xFF\x2F\x00")
    return b"MTrk" + struct.pack(">I", len(body)) + bytes(body)


def _meta_track_name(name: str) -> bytes:
    raw = name.encode("utf-8")
    return b"\xFF\x03" + _encode_vlq(len(raw)) + raw


def _meta_set_tempo(tempo_us: int = TEMPO_US) -> bytes:
    return b"\xFF\x51\x03" + struct.pack(">I", tempo_us)[1:]


def _notes_to_track_chunk(
    track_name: str,
    note_events: Iterable[NoteEvent],
    midi_channel: int,
    ppqn: int = PPQN,
    include_tempo: bool = False,
) -> bytes:
    us_per_tick = TEMPO_US / ppqn
    events: list[tuple[int, int, bytes]] = []  # (tick, order, payload)
    events.append((0, 0, _meta_track_name(track_name)))
    if include_tempo:
        events.append((0, 1, _meta_set_tempo(TEMPO_US)))

    ch = midi_channel & 0x0F
    for ev in note_events:
        tk_on = int(round((ev.start_s * 1_000_000.0) / us_per_tick))
        tk_off = max(tk_on + 1, int(round((ev.end_s * 1_000_000.0) / us_per_tick)))
        note = max(0, min(127, ev.note))
        vel = max(1, min(127, ev.velocity))
        # note_off antes de note_on no mesmo tick (order=2 vs 3)
        events.append((tk_on, 3, bytes((0x90 | ch, note, vel))))
        events.append((tk_off, 2, bytes((0x80 | ch, note, 0))))

    events.sort(key=lambda x: (x[0], x[1]))
    return _build_track_chunk([(tk, payload) for tk, _, payload in events])


def write_multitrack_midi(
    notes_by_channel: dict[str, list[NoteEvent]],
    out_path: str | os.PathLike[str],
    ppqn: int = PPQN,
    include_empty: bool = False,
) -> int:
    """Grava MIDI Tipo 1 multi-track (Track 0 = tempo, demais = 1 por canal)."""
    chunks: list[bytes] = []
    conductor = _build_track_chunk(
        [
            (0, _meta_track_name("YM2612+PSG")),
            (0, _meta_set_tempo(TEMPO_US)),
        ]
    )
    chunks.append(conductor)

    total_notes = 0
    for ch_name in CHANNEL_ORDER:
        evs = notes_by_channel.get(ch_name, [])
        if not evs and not include_empty:
            continue
        midi_ch = CHANNEL_MIDI_MAP[ch_name]
        chunks.append(_notes_to_track_chunk(ch_name, evs, midi_ch, ppqn=ppqn))
        total_notes += len(evs)

    header = b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), ppqn)
    out = Path(out_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    with open(out, "wb") as f:
        f.write(header)
        for chunk in chunks:
            f.write(chunk)
    return total_notes


def write_per_channel_midis(
    notes_by_channel: dict[str, list[NoteEvent]],
    out_dir: str | os.PathLike[str],
    ppqn: int = PPQN,
    include_empty: bool = False,
) -> dict[str, Path]:
    """Exporta um arquivo <canal>.mid individual para cada canal."""
    dst = Path(out_dir)
    dst.mkdir(parents=True, exist_ok=True)
    written: dict[str, Path] = {}
    for ch_name in CHANNEL_ORDER:
        evs = notes_by_channel.get(ch_name, [])
        if not evs and not include_empty:
            continue
        midi_ch = CHANNEL_MIDI_MAP[ch_name]
        chunk = _notes_to_track_chunk(
            ch_name, evs, midi_ch, ppqn=ppqn, include_tempo=True
        )
        header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, ppqn)
        path = dst / f"{ch_name}.mid"
        with open(path, "wb") as f:
            f.write(header)
            f.write(chunk)
        written[ch_name] = path
    return written


def read_midi_notes(
    midi_path: str | os.PathLike[str],
) -> dict[str, list[NoteEvent]]:
    """Lê um arquivo MIDI (Tipo 0 ou Tipo 1) e separa notas por canal físico.

    Se o MIDI possuir trilhas nomeadas com nomes canônicos (fm0..psg_noise),
    usa o nome da trilha; caso contrário, separa pelo canal MIDI (0..10).
    """
    data = Path(midi_path).read_bytes()
    if len(data) < 14 or data[0:4] != b"MThd":
        raise ValueError(f"Arquivo MIDI inválido: {midi_path}")

    hdr_len = struct.unpack_from(">I", data, 4)[0]
    _fmt, n_tracks, division = struct.unpack_from(">HHH", data, 8)
    if division & 0x8000:
        raise ValueError("MIDI SMPTE timecode não suportado")
    ppqn = max(1, division)

    pos = 8 + hdr_len
    tempo_changes: list[tuple[int, int]] = [(0, TEMPO_US)]
    parsed_tracks: list[tuple[str, list[tuple[int, str, int, int, int]]]] = []

    for _ in range(n_tracks):
        if pos + 8 > len(data) or data[pos : pos + 4] != b"MTrk":
            break
        trk_len = struct.unpack_from(">I", data, pos + 4)[0]
        trk_end = min(len(data), pos + 8 + trk_len)
        p = pos + 8
        pos = trk_end

        tick = 0
        running_status = 0
        trk_name = ""
        trk_msgs: list[tuple[int, str, int, int, int]] = []

        while p < trk_end:
            delta, p = _decode_vlq(data, p)
            tick += delta
            if p >= trk_end:
                break
            status = data[p]
            if status < 0x80:
                status = running_status
            else:
                p += 1
                if status < 0xF0:
                    running_status = status

            if status == 0xFF:
                if p >= trk_end:
                    break
                meta_type = data[p]
                p += 1
                mlen, p = _decode_vlq(data, p)
                mdata = data[p : p + mlen]
                p += mlen
                if meta_type == 0x03:
                    trk_name = mdata.decode("utf-8", errors="replace").strip()
                elif meta_type == 0x51 and len(mdata) == 3:
                    tempo = (mdata[0] << 16) | (mdata[1] << 8) | mdata[2]
                    tempo_changes.append((tick, max(1, tempo)))
                elif meta_type == 0x2F:
                    break
            elif status in (0xF0, 0xF7):
                slen, p = _decode_vlq(data, p)
                p += slen
            else:
                msg_type = status & 0xF0
                ch = status & 0x0F
                if msg_type in (0xC0, 0xD0):
                    p += 1
                elif msg_type in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                    if p + 1 >= trk_end:
                        break
                    d1 = data[p]
                    d2 = data[p + 1]
                    p += 2
                    if msg_type == 0x90 and d2 > 0:
                        trk_msgs.append((tick, "on", ch, d1, d2))
                    elif msg_type == 0x80 or (msg_type == 0x90 and d2 == 0):
                        trk_msgs.append((tick, "off", ch, d1, 0))
                else:
                    break
        parsed_tracks.append((trk_name, trk_msgs))

    # Constrói mapa de tempo (tick -> segundos)
    tempo_changes.sort(key=lambda x: x[0])
    tempo_map: list[tuple[int, float, int]] = []  # (tick, sec_at_tick, tempo_us)
    cur_tick = 0
    cur_sec = 0.0
    cur_tempo = TEMPO_US
    for tk, tmp in tempo_changes:
        if tk > cur_tick:
            cur_sec += ((tk - cur_tick) / ppqn) * (cur_tempo / 1_000_000.0)
            cur_tick = tk
        cur_tempo = tmp
        tempo_map.append((cur_tick, cur_sec, cur_tempo))

    def tick_to_sec(tk: int) -> float:
        base_tk, base_sec, base_tmp = tempo_map[0]
        for t_k, t_s, t_u in tempo_map:
            if t_k <= tk:
                base_tk, base_sec, base_tmp = t_k, t_s, t_u
            else:
                break
        return base_sec + ((tk - base_tk) / ppqn) * (base_tmp / 1_000_000.0)

    notes_by_channel: dict[str, list[NoteEvent]] = {ch: [] for ch in CHANNEL_ORDER}
    canonical_set = set(CHANNEL_ORDER)

    for trk_name, trk_msgs in parsed_tracks:
        norm_name = trk_name.lower().strip()
        use_track_name = norm_name in canonical_set
        active: dict[tuple[str, int], tuple[float, int, int]] = {}
        last_sec = 0.0

        for tk, kind, ch, note, vel in trk_msgs:
            sec = tick_to_sec(tk)
            if sec > last_sec:
                last_sec = sec
            ch_name = norm_name if use_track_name else MIDI_CH_FALLBACK_NAME.get(ch, f"fm{ch % 6}")
            key = (ch_name, note)
            if kind == "on":
                if key in active:
                    t_on, old_v, old_mc = active.pop(key)
                    notes_by_channel[ch_name].append(
                        NoteEvent(ch_name, t_on, max(t_on + 0.01, sec), note, old_v, old_mc)
                    )
                active[key] = (sec, vel, ch)
            else:
                if key in active:
                    t_on, old_v, old_mc = active.pop(key)
                    notes_by_channel[ch_name].append(
                        NoteEvent(ch_name, t_on, max(t_on + 0.01, sec), note, old_v, old_mc)
                    )

        for (ch_name, note), (t_on, old_v, old_mc) in active.items():
            notes_by_channel[ch_name].append(
                NoteEvent(ch_name, t_on, max(t_on + 0.1, last_sec + 0.1), note, old_v, old_mc)
            )

    for ch in CHANNEL_ORDER:
        notes_by_channel[ch].sort(key=lambda n: (n.start_s, n.note))
    return notes_by_channel


def convert_chip_ring_to_midi(
    ring_path: str | os.PathLike[str],
    out_path: str | os.PathLike[str],
    start_wf: int = 0,
    end_wf: int = 0,
    per_channel_dir: str | os.PathLike[str] | None = None,
    legacy_fm_perc: bool = False,
    frame_start: int = 0,
    frame_end: int = 0,
) -> tuple[dict[str, list[NoteEvent]], dict[str, int]]:
    notes_by_channel, stats = parse_chip_ring(
        ring_path,
        start_wf=start_wf,
        end_wf=end_wf,
        legacy_fm_perc=legacy_fm_perc,
        frame_start=frame_start,
        frame_end=frame_end,
    )
    write_multitrack_midi(notes_by_channel, out_path)
    if per_channel_dir:
        write_per_channel_midis(notes_by_channel, per_channel_dir)
    return notes_by_channel, stats


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Converte chip_ring.txt (YM2612/SN76489) em MIDI Tipo 1 por canal."
    )
    ap.add_argument("ring", help="Caminho do chip_ring.txt")
    ap.add_argument("out_pos", nargs="?", default=None, help="Arquivo .mid de saída (posicional)")
    ap.add_argument("-o", "--out", default=None, help="Arquivo .mid de saída")
    ap.add_argument("--start-wf", type=int, default=0, help="Frame inicial (wf)")
    ap.add_argument("--end-wf", type=int, default=0, help="Frame final (wf, 0 = até o fim)")
    ap.add_argument("--frame-start", type=int, default=0, help="Frame inicial (f= / wf=)")
    ap.add_argument("--frame-end", type=int, default=0, help="Frame final (f= / wf=, 0 = até o fim)")
    ap.add_argument(
        "--per-channel-dir",
        default=None,
        help="Diretório opcional para exportar <canal>.mid individuais",
    )
    ap.add_argument(
        "--legacy-fm-perc",
        action="store_true",
        help="Reativa a heurística legada que desvia notas FM <60 ms para percussão ch 10 (default: OFF)",
    )
    args = ap.parse_args(argv)

    out_path = args.out or args.out_pos or "transcricao.mid"
    notes_by_channel, stats = convert_chip_ring_to_midi(
        args.ring,
        out_path,
        start_wf=args.start_wf,
        end_wf=args.end_wf,
        per_channel_dir=args.per_channel_dir,
        legacy_fm_perc=args.legacy_fm_perc,
        frame_start=args.frame_start,
        frame_end=args.frame_end,
    )
    if stats.get("raw_events", 0) == 0:
        print("nenhum evento encontrado")
        return 1

    active_summary = ", ".join(
        f"{ch}={len(notes_by_channel[ch])}"
        for ch in CHANNEL_ORDER
        if notes_by_channel[ch]
    )
    print(
        f"{out_path}: {stats['total_notes']} notas "
        f"({stats['duration_ms'] / 1000.0:.2f}s) | {active_summary}"
    )
    if args.legacy_fm_perc:
        print(
            f"  [legacy-fm-perc] notas FM <60ms redirecionadas para ch 10: "
            f"{stats['legacy_fm_perc_redirected']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
