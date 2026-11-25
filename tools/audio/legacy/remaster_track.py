#!/usr/bin/env python3
"""tools/audio/remaster_track.py — motor de renderização de stems e mixagem
para remasterização de áudio de Eternal Champions.

Importável como biblioteca Python (por ex. por tools/audio/mixer_studio.py) e
executável via CLI.

Lê:
  - assets/audio_remaster/profiles/<track>.toml
  - assets/audio_remaster/stems/<track>/midi/<channel>.mid

Gera:
  - assets/audio_remaster/stems/<track>/wav/<channel>.wav (48 kHz estéreo 16-bit)
  - assets/audio_remaster/tracks/<track>.wav              (mix final normalizado)
  - opcionalmente atualiza assets/audio_remaster/manifest.toml

Backends de síntese suportados (seleção automática com relatório explícito):
  1. libfluidsynth-ctypes  (Linux/WSL ou DLL nativa, ~0.2-0.5s por stem)
  2. fluidsynth-cli        (binário fluidsynth no PATH)
  3. wsl-libfluidsynth     (ponte transparente do Windows para libfluidsynth no WSL)
  4. python-fallback-synth (sintetizador NumPy leve; também usado no modo "chip")

Uso:
    python tools/audio/remaster_track.py 03_syria_blade
    python tools/audio/remaster_track.py 03_syria_blade --channel fm0
    python tools/audio/remaster_track.py 03_syria_blade --mix-only --update-manifest
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util
from dataclasses import dataclass, field
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import tomllib
from typing import Any
import wave

import numpy as np

_THIS_DIR = Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from chip_ring_to_midi import (  # noqa: E402
    CHANNEL_ORDER,
    NoteEvent,
    PERC_MIDI_CH,
    read_midi_notes,
)
from export_stems import (  # noqa: E402
    DEFAULT_BASE_DIR,
    DEFAULT_SOUNDFONT,
    format_profile_toml,
)

CEILING_DBFS = -1.0
CEILING_LIN = 10.0 ** (CEILING_DBFS / 20.0)  # ~0.89125
SOFT_KNEE_DBFS = -2.5
SOFT_KNEE_LIN = 10.0 ** (SOFT_KNEE_DBFS / 20.0)


@dataclass
class StemRenderResult:
    track: str
    channel: str
    wav_path: Path
    sample_rate: int
    num_frames: int
    duration_s: float
    backend: str
    note_count: int
    peak_dbfs: float
    mode: str


@dataclass
class TrackMixResult:
    track: str
    out_wav: Path
    sample_rate: int
    num_frames: int
    duration_s: float
    peak_dbfs: float
    rms_dbfs: float
    backends_used: list[str] = field(default_factory=list)
    active_channels: list[str] = field(default_factory=list)
    rendered_stems: list[StemRenderResult] = field(default_factory=list)


# ---------------------------------------------------------------------------
# I/O de Profile e Manifest TOML
# ---------------------------------------------------------------------------

def load_profile(profile_path: str | Path) -> dict[str, Any]:
    with open(profile_path, "rb") as f:
        return tomllib.load(f)


def save_profile(profile_path: str | Path, profile: dict[str, Any]) -> None:
    p = Path(profile_path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(format_profile_toml(profile), encoding="utf-8")


def load_manifest(manifest_path: str | Path) -> dict[str, Any]:
    p = Path(manifest_path)
    if not p.exists():
        return {
            "defaults": {
                "tracks_dir": "assets/audio_remaster/tracks",
                "gain_db": 0.0,
            },
            "track": [],
        }
    with open(p, "rb") as f:
        data = tomllib.load(f)
    data.setdefault(
        "defaults",
        {"tracks_dir": "assets/audio_remaster/tracks", "gain_db": 0.0},
    )
    data.setdefault("track", [])
    return data


def _toml_scalar(v: Any) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        s = f"{v:.4f}".rstrip("0").rstrip(".")
        return s if "." in s else f"{s}.0"
    if isinstance(v, str):
        esc = v.replace("\\", "\\\\").replace('"', '\\"')
        return f'"{esc}"'
    raise TypeError(f"Valor TOML inválido: {type(v)!r}")


def format_manifest_toml(manifest: dict[str, Any]) -> str:
    defaults = manifest.get("defaults", {})
    tracks_dir = defaults.get("tracks_dir", "assets/audio_remaster/tracks")
    gain_db = float(defaults.get("gain_db", 0.0))

    lines: list[str] = [
        "[defaults]",
        f"tracks_dir = {_toml_scalar(str(tracks_dir))}",
        f"gain_db = {_toml_scalar(gain_db)}",
    ]
    entries = sorted(manifest.get("track", []), key=lambda t: (int(t.get("id", 0)), str(t.get("name", ""))))
    for tr in entries:
        lines.extend(
            [
                "",
                "[[track]]",
                f"id = {_toml_scalar(int(tr['id']))}",
                f"name = {_toml_scalar(str(tr['name']))}",
                f"file = {_toml_scalar(str(tr['file']))}",
                f"loop_start = {_toml_scalar(int(tr.get('loop_start', 0)))}",
                f"loop_end = {_toml_scalar(int(tr.get('loop_end', 0)))}",
                f"gain_db = {_toml_scalar(float(tr.get('gain_db', 0.0)))}",
            ]
        )
    lines.append("")
    return "\n".join(lines)


def save_manifest(manifest_path: str | Path, manifest: dict[str, Any]) -> None:
    p = Path(manifest_path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(format_manifest_toml(manifest), encoding="utf-8")


def update_manifest_entry(
    manifest_path: str | Path,
    track_id: int,
    name: str,
    filename: str | None = None,
    loop_start: int = 0,
    loop_end: int = 0,
    gain_db: float = 0.0,
    tracks_dir: str | None = None,
) -> dict[str, Any]:
    """Insere ou atualiza uma entrada [[track]] no manifest.toml."""
    manifest = load_manifest(manifest_path)
    if tracks_dir is not None:
        manifest["defaults"]["tracks_dir"] = tracks_dir
    fname = filename or f"{name}.wav"
    new_entry = {
        "id": int(track_id),
        "name": name,
        "file": fname,
        "loop_start": int(loop_start),
        "loop_end": int(loop_end),
        "gain_db": float(gain_db),
    }
    tracks: list[dict[str, Any]] = list(manifest.get("track", []))
    replaced = False
    for idx, cur in enumerate(tracks):
        if cur.get("name") == name or (int(track_id) > 0 and int(cur.get("id", -1)) == int(track_id)):
            tracks[idx] = new_entry
            replaced = True
            break
    if not replaced:
        tracks.append(new_entry)
    manifest["track"] = tracks
    save_manifest(manifest_path, manifest)
    return manifest


# ---------------------------------------------------------------------------
# I/O WAV (PCM 16-bit estéreo) e métricas de áudio
# ---------------------------------------------------------------------------

def write_wav_stereo_16(path: str | Path, stereo: np.ndarray, sample_rate: int) -> None:
    """Grava array float32/float64 shape (N, 2) em [-1, 1] como WAV 16-bit estéreo."""
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    arr = np.asarray(stereo, dtype=np.float32)
    if arr.ndim == 1:
        arr = np.stack([arr, arr], axis=1)
    arr = np.clip(arr, -1.0, 1.0)
    pcm = np.round(arr * 32767.0).astype("<i2")
    fd, tmp_name = tempfile.mkstemp(dir=p.parent, prefix=f".{p.name}.", suffix=".tmp")
    tmp_path = Path(tmp_name)
    try:
        os.close(fd)
        with wave.open(str(tmp_path), "wb") as wf:
            wf.setnchannels(2)
            wf.setsampwidth(2)
            wf.setframerate(int(sample_rate))
            wf.writeframes(pcm.tobytes())
        os.replace(tmp_path, p)
    except BaseException:
        try:
            tmp_path.unlink(missing_ok=True)
        except OSError:
            pass
        raise


def read_wav_stereo_float(path: str | Path) -> tuple[np.ndarray, int]:
    """Lê WAV 16-bit mono/estéreo e retorna (array float32 shape (N, 2), sample_rate)."""
    with wave.open(str(path), "rb") as wf:
        sr = wf.getframerate()
        nch = wf.getnchannels()
        sw = wf.getsampwidth()
        nframes = wf.getnframes()
        raw = wf.readframes(nframes)
    if sw != 2:
        raise ValueError(f"WAV precisa ser 16-bit PCM (recebido {sw*8}-bit): {path}")
    data = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    if nch == 1:
        stereo = np.stack([data, data], axis=1)
    else:
        stereo = data.reshape(-1, nch)[:, :2]
    return stereo, sr


def measure_peak_dbfs(stereo: np.ndarray) -> float:
    if stereo.size == 0:
        return -120.0
    pk = float(np.max(np.abs(stereo)))
    if pk <= 1e-9:
        return -120.0
    return 20.0 * math.log10(pk)


def measure_active_rms_dbfs(stereo: np.ndarray, gate_dbfs: float = -55.0) -> float:
    """Mede RMS em dBFS ignorando trechos de silêncio puro."""
    if stereo.size == 0:
        return -120.0
    power = np.mean(stereo.astype(np.float64) ** 2, axis=1)
    gate_lin = (10.0 ** (gate_dbfs / 20.0)) ** 2
    active = power[power >= gate_lin]
    mean_p = float(np.mean(active)) if active.size >= 256 else float(np.mean(power))
    if mean_p <= 1e-12:
        return -120.0
    return 10.0 * math.log10(mean_p)


# ---------------------------------------------------------------------------
# DSP: EQ 3 bandas (Biquad Audio EQ Cookbook via FFT) + Pan + Limiter suave
# ---------------------------------------------------------------------------

def _biquad_lowshelf(sr: float, f0: float, gain_db: float, q: float = 0.7071) -> tuple[np.ndarray, np.ndarray]:
    a = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * math.pi * f0 / sr
    cw = math.cos(w0)
    sw = math.sin(w0)
    alpha = sw / (2.0 * q)
    two_sq_a = 2.0 * math.sqrt(a) * alpha

    b0 = a * ((a + 1.0) - (a - 1.0) * cw + two_sq_a)
    b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cw)
    b2 = a * ((a + 1.0) - (a - 1.0) * cw - two_sq_a)
    a0 = (a + 1.0) + (a - 1.0) * cw + two_sq_a
    a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cw)
    a2 = (a + 1.0) + (a - 1.0) * cw - two_sq_a
    return np.array([b0 / a0, b1 / a0, b2 / a0]), np.array([1.0, a1 / a0, a2 / a0])


def _biquad_peaking(sr: float, f0: float, gain_db: float, q: float = 1.0) -> tuple[np.ndarray, np.ndarray]:
    a = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * math.pi * f0 / sr
    cw = math.cos(w0)
    sw = math.sin(w0)
    alpha = sw / (2.0 * q)

    b0 = 1.0 + alpha * a
    b1 = -2.0 * cw
    b2 = 1.0 - alpha * a
    a0 = 1.0 + alpha / a
    a1 = -2.0 * cw
    a2 = 1.0 - alpha / a
    return np.array([b0 / a0, b1 / a0, b2 / a0]), np.array([1.0, a1 / a0, a2 / a0])


def _biquad_highshelf(sr: float, f0: float, gain_db: float, q: float = 0.7071) -> tuple[np.ndarray, np.ndarray]:
    a = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * math.pi * f0 / sr
    cw = math.cos(w0)
    sw = math.sin(w0)
    alpha = sw / (2.0 * q)
    two_sq_a = 2.0 * math.sqrt(a) * alpha

    b0 = a * ((a + 1.0) + (a - 1.0) * cw + two_sq_a)
    b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cw)
    b2 = a * ((a + 1.0) + (a - 1.0) * cw - two_sq_a)
    a0 = (a + 1.0) - (a - 1.0) * cw + two_sq_a
    a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cw)
    a2 = (a + 1.0) - (a - 1.0) * cw - two_sq_a
    return np.array([b0 / a0, b1 / a0, b2 / a0]), np.array([1.0, a1 / a0, a2 / a0])


def apply_3band_eq(
    stereo: np.ndarray,
    sample_rate: int,
    low_db: float = 0.0,
    mid_db: float = 0.0,
    high_db: float = 0.0,
) -> np.ndarray:
    """Aplica EQ de 3 bandas (Low Shelf 180 Hz, Mid Peak 1.2 kHz, High Shelf 5 kHz)."""
    if (
        abs(low_db) < 1e-3
        and abs(mid_db) < 1e-3
        and abs(high_db) < 1e-3
    ) or stereo.shape[0] == 0:
        return stereo

    filters: list[tuple[np.ndarray, np.ndarray]] = []
    for fn, freq, gdb, q in (
        (_biquad_lowshelf, 180.0, low_db, 0.7071),
        (_biquad_peaking, 1200.0, mid_db, 1.0),
        (_biquad_highshelf, 5000.0, high_db, 0.7071),
    ):
        if abs(gdb) >= 1e-3:
            filters.append(fn(float(sample_rate), freq, float(gdb), q))

    # Caminho rápido nativo se scipy estiver instalado (ex.: Windows Python)
    try:
        from scipy.signal import lfilter  # type: ignore

        out = stereo.astype(np.float64, copy=True)
        for b, a in filters:
            out[:, 0] = lfilter(b, a, out[:, 0])
            out[:, 1] = lfilter(b, a, out[:, 1])
        return out.astype(np.float32)
    except ImportError:
        pass

    # Fallback pure-NumPy: convolução overlap-add por blocos (n_fft=16384, ~10x mais rápido)
    n = stereo.shape[0]
    ir_len = 2048
    n_fft = 16384
    hop = n_fft - ir_len + 1

    freqs = np.fft.rfftfreq(n_fft, d=1.0 / float(sample_rate))
    w = 2.0 * math.pi * freqs / float(sample_rate)
    z1 = np.exp(-1j * w)
    z2 = z1 * z1
    H = np.ones_like(z1, dtype=np.complex128)
    for b, a in filters:
        H *= (b[0] + b[1] * z1 + b[2] * z2) / (a[0] + a[1] * z1 + a[2] * z2)

    # Trunca a resposta ao impulso para evitar aliasing circular entre blocos
    ir = np.fft.irfft(H, n=n_fft)[:ir_len]
    H_fir = np.fft.rfft(ir, n=n_fft)

    out = np.zeros((n, 2), dtype=np.float32)
    for ch in range(2):
        sig = stereo[:, ch].astype(np.float64)
        acc = np.zeros(n + n_fft, dtype=np.float64)
        for start in range(0, n, hop):
            blk = sig[start : start + hop]
            spec = np.fft.rfft(blk, n=n_fft)
            conv = np.fft.irfft(spec * H_fir, n=n_fft)
            acc[start : start + n_fft] += conv
        out[:, ch] = acc[:n].astype(np.float32)
    return out


def apply_pan_and_gain(stereo: np.ndarray, volume_db: float, pan: float) -> np.ndarray:
    """Aplica ganho em dB e panorama equal-power (-1.0 L .. +1.0 R)."""
    gain = 10.0 ** (float(volume_db) / 20.0)
    pan_c = max(-1.0, min(1.0, float(pan)))
    angle = (pan_c + 1.0) * (math.pi * 0.25)
    # Em pan=0.0: cos(pi/4)*sqrt(2) == 1.0, sin(pi/4)*sqrt(2) == 1.0
    gl = math.cos(angle) * math.sqrt(2.0) * gain
    gr = math.sin(angle) * math.sqrt(2.0) * gain
    out = np.empty_like(stereo, dtype=np.float32)
    out[:, 0] = stereo[:, 0] * gl
    out[:, 1] = stereo[:, 1] * gr
    return out


def apply_master_normalize_and_limit(
    stereo: np.ndarray,
    master_gain_db: float = 0.0,
    target_rms_dbfs: float = -16.0,
    max_makeup_db: float = 18.0,
) -> np.ndarray:
    """Normaliza para target_rms_dbfs e aplica soft-clipping seguro (pico <= -1 dBFS)."""
    if stereo.size == 0:
        return stereo.astype(np.float32)

    x = stereo.astype(np.float64, copy=True)
    # Remove possível DC offset residual
    x -= np.mean(x, axis=0, keepdims=True)

    cur_rms = measure_active_rms_dbfs(x)
    if cur_rms > -90.0:
        delta_db = max(-24.0, min(max_makeup_db, float(target_rms_dbfs) - cur_rms))
        total_db = delta_db + float(master_gain_db)
        x *= 10.0 ** (total_db / 20.0)

    # Soft knee limiter: linear até SOFT_KNEE_LIN (-2.5 dBFS), assintótico até CEILING_LIN (-1.0 dBFS)
    headroom = CEILING_LIN - SOFT_KNEE_LIN
    ax = np.abs(x)
    over = ax > SOFT_KNEE_LIN
    if np.any(over):
        excess = (ax[over] - SOFT_KNEE_LIN) / headroom
        shaped = SOFT_KNEE_LIN + headroom * np.tanh(excess)
        x[over] = np.sign(x[over]) * shaped

    # Garantia absoluta de teto <= -1.0 dBFS
    pk = float(np.max(np.abs(x)))
    if pk > CEILING_LIN:
        x *= CEILING_LIN / pk

    return x.astype(np.float32)


# ---------------------------------------------------------------------------
# Backends de Síntese: libfluidsynth (ctypes), CLI, ponte WSL e fallback Python
# ---------------------------------------------------------------------------

def _load_libfluidsynth() -> ctypes.CDLL | None:
    candidates = [
        "libfluidsynth.so.3",
        "libfluidsynth.so.2",
        "libfluidsynth.so",
        "libfluidsynth.dylib",
        "libfluidsynth-3.dll",
        "fluidsynth.dll",
    ]
    found = ctypes.util.find_library("fluidsynth")
    if found:
        candidates.insert(0, found)
    for name in candidates:
        try:
            lib = ctypes.CDLL(name)
            if hasattr(lib, "new_fluid_synth") and hasattr(lib, "fluid_synth_write_float"):
                return lib
        except OSError:
            continue
    return None


def _render_events_libfluidsynth(
    lib: ctypes.CDLL,
    events: list[NoteEvent],
    soundfont: str,
    sample_rate: int,
    total_frames: int,
    mode: str,
    bank: int,
    program: int,
    transpose: int,
) -> np.ndarray:
    """Renderiza eventos de um canal usando libfluidsynth diretamente via ctypes."""
    lib.new_fluid_settings.restype = ctypes.c_void_p
    lib.delete_fluid_settings.argtypes = [ctypes.c_void_p]
    lib.fluid_settings_setnum.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_double]
    lib.fluid_settings_setint.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.new_fluid_synth.argtypes = [ctypes.c_void_p]
    lib.new_fluid_synth.restype = ctypes.c_void_p
    lib.delete_fluid_synth.argtypes = [ctypes.c_void_p]
    lib.fluid_synth_sfload.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.fluid_synth_sfload.restype = ctypes.c_int
    lib.fluid_synth_program_select.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
    ]
    lib.fluid_synth_noteon.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.fluid_synth_noteoff.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    lib.fluid_synth_cc.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.fluid_synth_set_gain.argtypes = [ctypes.c_void_p, ctypes.c_float]
    lib.fluid_synth_write_float.argtypes = [
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_void_p,
        ctypes.c_int,
        ctypes.c_int,
    ]

    settings = lib.new_fluid_settings()
    lib.fluid_settings_setnum(settings, b"synth.sample-rate", float(sample_rate))
    lib.fluid_settings_setint(settings, b"synth.reverb.active", 1)
    lib.fluid_settings_setnum(settings, b"synth.reverb.room-size", 0.45)
    lib.fluid_settings_setnum(settings, b"synth.reverb.damp", 0.35)
    lib.fluid_settings_setnum(settings, b"synth.reverb.width", 0.6)
    lib.fluid_settings_setnum(settings, b"synth.reverb.level", 0.35)
    lib.fluid_settings_setint(settings, b"synth.chorus.active", 1)
    lib.fluid_settings_setint(settings, b"synth.chorus.nr", 3)
    lib.fluid_settings_setnum(settings, b"synth.chorus.level", 0.4)
    lib.fluid_settings_setnum(settings, b"synth.chorus.speed", 0.3)
    lib.fluid_settings_setnum(settings, b"synth.chorus.depth", 4.0)
    synth = lib.new_fluid_synth(settings)
    try:
        sf_id = lib.fluid_synth_sfload(synth, soundfont.encode("utf-8"), 1)
        if sf_id < 0:
            raise RuntimeError(f"FluidSynth não conseguiu carregar SoundFont: {soundfont}")
        lib.fluid_synth_set_gain(synth, ctypes.c_float(0.65))

        is_perc = mode == "percussion"
        midi_ch = PERC_MIDI_CH if is_perc else 0
        eff_bank = 128 if is_perc else int(bank)
        eff_prog = max(0, min(127, int(program)))
        lib.fluid_synth_program_select(synth, midi_ch, sf_id, eff_bank, eff_prog)
        # Reverb/chorus de estúdio para fundir naturalmente com B_studio_dsp
        lib.fluid_synth_cc(synth, midi_ch, 91, 36 if not is_perc else 18)
        lib.fluid_synth_cc(synth, midi_ch, 93, 22 if not is_perc else 0)
        lib.fluid_synth_cc(synth, midi_ch, 7, 110)

        # Constrói agenda ordenada por amostra: (sample_idx, order, is_on, key, vel)
        schedule: list[tuple[int, int, bool, int, int]] = []
        for ev in events:
            key = ev.note if is_perc else max(0, min(127, ev.note + int(transpose)))
            vel = max(1, min(127, ev.velocity))
            s_on = max(0, min(total_frames - 1, int(round(ev.start_s * sample_rate))))
            s_off = max(s_on + 1, min(total_frames - 1, int(round(ev.end_s * sample_rate))))
            schedule.append((s_on, 1, True, key, vel))
            schedule.append((s_off, 0, False, key, 0))
        schedule.sort(key=lambda x: (x[0], x[1]))

        left = np.zeros(total_frames, dtype=np.float32)
        right = np.zeros(total_frames, dtype=np.float32)
        cursor = 0
        idx = 0
        n_sched = len(schedule)

        def render_span(n_samples: int) -> None:
            nonlocal cursor
            if n_samples <= 0:
                return
            l_ptr = left[cursor:].ctypes.data_as(ctypes.c_void_p)
            r_ptr = right[cursor:].ctypes.data_as(ctypes.c_void_p)
            lib.fluid_synth_write_float(synth, int(n_samples), l_ptr, 0, 1, r_ptr, 0, 1)
            cursor += n_samples

        while idx < n_sched:
            ev_sample = schedule[idx][0]
            if ev_sample > cursor:
                render_span(ev_sample - cursor)
            while idx < n_sched and schedule[idx][0] <= cursor:
                _, _, is_on, key, vel = schedule[idx]
                if is_on:
                    lib.fluid_synth_noteon(synth, midi_ch, key, vel)
                else:
                    lib.fluid_synth_noteoff(synth, midi_ch, key)
                idx += 1

        if cursor < total_frames:
            render_span(total_frames - cursor)

        return np.stack([left, right], axis=1)
    finally:
        lib.delete_fluid_synth(synth)
        lib.delete_fluid_settings(settings)


def _render_events_python_synth(
    events: list[NoteEvent],
    sample_rate: int,
    total_frames: int,
    mode: str,
    channel_name: str,
    program: int,
    transpose: int,
) -> np.ndarray:
    """Sintetizador NumPy leve (fallback e modo 'chip' estilo Mega Drive)."""
    out = np.zeros((total_frames, 2), dtype=np.float32)
    if not events or mode == "off":
        return out

    is_perc = mode == "percussion"
    rng = np.random.default_rng(1993 + sum(ord(c) for c in channel_name))

    for ev in events:
        s_on = max(0, min(total_frames - 1, int(round(ev.start_s * sample_rate))))
        amp = (max(1, min(127, ev.velocity)) / 127.0) * 0.28

        if is_perc:
            # Síntese de percussão: bumbo (35-36), caixa (38/40), chimbal/prato (42/46+)
            if ev.note in (35, 36):
                dur_n = min(total_frames - s_on, int(0.18 * sample_rate))
                t = np.arange(dur_n, dtype=np.float64) / sample_rate
                freq = 50.0 + 110.0 * np.exp(-t * 36.0)
                phase = 2.0 * math.pi * np.cumsum(freq) / sample_rate
                sig = np.sin(phase) * np.exp(-t * 16.0) * (amp * 1.6)
            elif ev.note in (38, 39, 40):
                dur_n = min(total_frames - s_on, int(0.16 * sample_rate))
                t = np.arange(dur_n, dtype=np.float64) / sample_rate
                body = np.sin(2.0 * math.pi * 185.0 * t) * np.exp(-t * 28.0) * 0.55
                noise = rng.standard_normal(dur_n) * np.exp(-t * 22.0) * 0.75
                sig = (body + noise) * (amp * 1.2)
            else:
                dur_s = 0.06 if ev.note == 42 else 0.14
                dur_n = min(total_frames - s_on, int(dur_s * sample_rate))
                t = np.arange(dur_n, dtype=np.float64) / sample_rate
                noise = rng.standard_normal(dur_n)
                # Passa-alta simples por diferença de primeira ordem
                hp = np.empty_like(noise)
                hp[0] = noise[0]
                hp[1:] = noise[1:] - 0.85 * noise[:-1]
                sig = hp * np.exp(-t * (45.0 if ev.note == 42 else 18.0)) * (amp * 0.7)
            s_end = s_on + dur_n
            out[s_on:s_end, 0] += sig.astype(np.float32)
            out[s_on:s_end, 1] += sig.astype(np.float32)
            continue

        key = max(12, min(120, ev.note + int(transpose)))
        freq = 440.0 * (2.0 ** ((key - 69.0) / 12.0))
        rel_s = 0.08 if mode == "chip" else 0.14
        dur_n = min(total_frames - s_on, max(16, int(round((ev.duration_s + rel_s) * sample_rate))))
        t = np.arange(dur_n, dtype=np.float64) / sample_rate
        ph = 2.0 * math.pi * freq * t

        if mode == "chip":
            if channel_name.startswith("psg"):
                # Onda quadrada PSG (3 harmônicos ímpares limitados em banda)
                sig = (
                    np.sin(ph)
                    + (1.0 / 3.0) * np.sin(3.0 * ph)
                    + (1.0 / 5.0) * np.sin(5.0 * ph)
                ) * 0.72
            else:
                # FM 2-operadores estilo YM2612
                mod_idx = 2.2 * np.exp(-t * 6.0) + 0.4
                sig = np.sin(ph + mod_idx * np.sin(2.0 * ph))
        else:
            # Timbre aproximado por família GM
            if 32 <= program <= 39:  # Bass
                sig = 0.65 * np.sin(ph) + 0.35 * np.sin(2.0 * ph) + 0.15 * np.sin(3.0 * ph)
            elif 48 <= program <= 55 or 88 <= program <= 95:  # Strings / Pads
                sig = 0.5 * np.sin(ph) + 0.25 * np.sin(2.0 * ph) + 0.25 * np.sin(ph * 1.003)
            else:  # Lead / Brass
                sig = (
                    0.55 * np.sin(ph)
                    + 0.28 * np.sin(2.0 * ph)
                    + 0.16 * np.sin(3.0 * ph)
                    + 0.08 * np.sin(4.0 * ph)
                )

        # Envelope ADSR simples
        atk_n = min(dur_n, max(1, int(0.008 * sample_rate)))
        rel_n = min(dur_n, max(1, int(rel_s * sample_rate)))
        env = np.ones(dur_n, dtype=np.float64)
        env[:atk_n] = np.linspace(0.0, 1.0, atk_n)
        env[-rel_n:] *= np.linspace(1.0, 0.0, rel_n)
        wave_out = (sig * env * amp).astype(np.float32)

        s_end = s_on + dur_n
        out[s_on:s_end, 0] += wave_out
        out[s_on:s_end, 1] += wave_out

    return out


def _win_to_wsl_path(p: Path) -> str:
    resolved = p.resolve()
    s = str(resolved)
    if len(s) >= 2 and s[1] == ":":
        drive = s[0].lower()
        rest = s[2:].replace("\\", "/")
        return f"/mnt/{drive}{rest}"
    return s.replace("\\", "/")


def _can_use_wsl_fluidsynth() -> tuple[bool, str]:
    """Verifica se o ambiente WSL possui Python + libfluidsynth.so.3."""
    if os.name != "nt" or shutil.which("wsl") is None:
        return False, ""
    for py_bin in ("/home/mips/eternalchampions/.venv/bin/python", "python3"):
        try:
            res = subprocess.run(
                [
                    "wsl",
                    py_bin,
                    "-c",
                    "import ctypes, numpy; ctypes.CDLL('libfluidsynth.so.3'); print('OK')",
                ],
                capture_output=True,
                text=True,
                timeout=10,
            )
            if res.returncode == 0 and "OK" in res.stdout:
                return True, py_bin
        except Exception:
            continue
    return False, ""


def _render_via_wsl_bridge(
    wsl_python: str,
    script_path: Path,
    midi_path: Path,
    wav_out: Path,
    soundfont: str,
    sample_rate: int,
    total_frames: int,
    mode: str,
    bank: int,
    program: int,
    transpose: int,
    channel_name: str,
) -> None:
    """Executa a renderização de um stem via libfluidsynth.so.3 dentro do WSL."""
    sf_path = soundfont
    if not soundfont.startswith("/") and (
        (len(soundfont) >= 2 and soundfont[1] == ":") or Path(soundfont).exists()
    ):
        sf_path = _win_to_wsl_path(Path(soundfont))
    payload = {
        "midi_path": _win_to_wsl_path(midi_path),
        "wav_out": _win_to_wsl_path(wav_out),
        "soundfont": sf_path,
        "sample_rate": int(sample_rate),
        "total_frames": int(total_frames),
        "mode": mode,
        "bank": int(bank),
        "program": int(program),
        "transpose": int(transpose),
        "channel_name": channel_name,
    }
    wsl_script = _win_to_wsl_path(script_path)
    res = subprocess.run(
        ["wsl", wsl_python, wsl_script, "--_wsl-render-stem-json", json.dumps(payload)],
        capture_output=True,
        text=True,
        timeout=120,
    )
    if res.returncode != 0:
        raise RuntimeError(
            f"WSL FluidSynth falhou (rc={res.returncode}): {res.stderr.strip() or res.stdout.strip()}"
        )


def _render_via_fluidsynth_cli(
    fluidsynth_bin: str,
    events: list[NoteEvent],
    soundfont: str,
    sample_rate: int,
    total_frames: int,
    mode: str,
    bank: int,
    program: int,
    transpose: int,
) -> np.ndarray:
    """Fallback usando CLI fluidsynth -ni -F."""
    from chip_ring_to_midi import PPQN, TEMPO_US, _build_track_chunk, _meta_set_tempo
    import struct

    is_perc = mode == "percussion"
    ch = PERC_MIDI_CH if is_perc else 0
    us_per_tick = TEMPO_US / PPQN
    trk_ev: list[tuple[int, bytes]] = [(0, _meta_set_tempo(TEMPO_US))]
    if not is_perc:
        trk_ev.append((0, bytes((0xB0 | ch, 0, bank & 0x7F))))
    trk_ev.append((0, bytes((0xC0 | ch, program & 0x7F))))

    for ev in events:
        key = ev.note if is_perc else max(0, min(127, ev.note + int(transpose)))
        tk_on = int(round((ev.start_s * 1_000_000.0) / us_per_tick))
        tk_off = max(tk_on + 1, int(round((ev.end_s * 1_000_000.0) / us_per_tick)))
        trk_ev.append((tk_on, bytes((0x90 | ch, key, max(1, min(127, ev.velocity))))))
        trk_ev.append((tk_off, bytes((0x80 | ch, key, 0))))

    chunk = _build_track_chunk(trk_ev)
    header = b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQN)

    with tempfile.TemporaryDirectory() as td:
        tmp_mid = Path(td) / "stem.mid"
        tmp_wav = Path(td) / "stem.wav"
        tmp_mid.write_bytes(header + chunk)
        cmd = [
            fluidsynth_bin,
            "-ni",
            "-g",
            "0.65",
            "-r",
            str(sample_rate),
            "-o",
            "synth.reverb.active=1",
            "-o",
            "synth.reverb.room-size=0.45",
            "-o",
            "synth.reverb.level=0.35",
            "-o",
            "synth.chorus.active=1",
            "-o",
            "synth.chorus.level=0.4",
            "-F",
            str(tmp_wav),
            soundfont,
            str(tmp_mid),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        if res.returncode != 0 or not tmp_wav.exists():
            raise RuntimeError(f"fluidsynth CLI falhou: {res.stderr.strip()}")
        stereo, _ = read_wav_stereo_float(tmp_wav)

    if stereo.shape[0] < total_frames:
        pad = np.zeros((total_frames - stereo.shape[0], 2), dtype=np.float32)
        stereo = np.vstack([stereo, pad])
    else:
        stereo = stereo[:total_frames]
    return stereo


def detect_synth_backend(soundfont: str, preferred: str = "auto") -> tuple[str, Any]:
    """Detecta e retorna (backend_name, handle)."""
    if preferred == "python":
        return "python-fallback-synth", None

    if preferred in ("auto", "ctypes"):
        lib = _load_libfluidsynth()
        if lib is not None and Path(soundfont).exists():
            return "libfluidsynth-ctypes", lib
        if preferred == "ctypes":
            raise RuntimeError(
                f"Backend ctypes indisponível (libfluidsynth ou SoundFont '{soundfont}' ausente)"
            )

    if preferred in ("auto", "cli"):
        fs_bin = shutil.which("fluidsynth")
        if fs_bin and Path(soundfont).exists():
            return "fluidsynth-cli", fs_bin
        if preferred == "cli":
            raise RuntimeError("Backend fluidsynth-cli indisponível")

    if preferred in ("auto", "wsl"):
        ok, wsl_py = _can_use_wsl_fluidsynth()
        if ok:
            return "wsl-libfluidsynth", wsl_py
        if preferred == "wsl":
            raise RuntimeError("Backend wsl-libfluidsynth indisponível")

    return "python-fallback-synth", None


# ---------------------------------------------------------------------------
# API principal: render_channel_stem, mix_track, remaster_track
# ---------------------------------------------------------------------------

def _track_total_frames(
    midi_dir: Path, sample_rate: int, tail_s: float = 1.2
) -> int:
    max_end = 0.0
    for ch in CHANNEL_ORDER:
        mp = midi_dir / f"{ch}.mid"
        if mp.exists():
            notes_map = read_midi_notes(mp)
            for evs in notes_map.values():
                for e in evs:
                    if e.end_s > max_end:
                        max_end = e.end_s
    return max(int(sample_rate), int(math.ceil((max_end + tail_s) * sample_rate)))


def render_channel_stem(
    track_name: str,
    channel: str,
    base_dir: str | Path = DEFAULT_BASE_DIR,
    profile: dict[str, Any] | None = None,
    total_frames: int | None = None,
    backend_pref: str = "auto",
    _cached_backend: tuple[str, Any] | None = None,
) -> StemRenderResult:
    """Renderiza um único canal para stems/<track>/wav/<channel>.wav."""
    root = Path(base_dir)
    profile_path = root / "profiles" / f"{track_name}.toml"
    prof = profile if profile is not None else load_profile(profile_path)

    tr_cfg = prof.get("track", {})
    ch_cfg = prof.get("channels", {}).get(channel, {})
    sample_rate = int(tr_cfg.get("sample_rate", 48000))
    soundfont = str(tr_cfg.get("soundfont", DEFAULT_SOUNDFONT))
    mode = str(ch_cfg.get("mode", "off")).lower()
    bank = int(ch_cfg.get("bank", 128 if mode == "percussion" else 0))
    program = int(ch_cfg.get("program", 0))
    transpose = int(ch_cfg.get("transpose", 0))

    midi_dir = root / "stems" / track_name / "midi"
    wav_dir = root / "stems" / track_name / "wav"
    wav_dir.mkdir(parents=True, exist_ok=True)
    midi_path = midi_dir / f"{channel}.mid"
    wav_path = wav_dir / f"{channel}.wav"

    n_frames = total_frames or _track_total_frames(midi_dir, sample_rate)

    events: list[NoteEvent] = []
    if midi_path.exists():
        notes_map = read_midi_notes(midi_path)
        for evs in notes_map.values():
            events.extend(evs)
        events.sort(key=lambda e: (e.start_s, e.note))

    if mode == "off" or not events:
        stereo = np.zeros((n_frames, 2), dtype=np.float32)
        write_wav_stereo_16(wav_path, stereo, sample_rate)
        return StemRenderResult(
            track=track_name,
            channel=channel,
            wav_path=wav_path,
            sample_rate=sample_rate,
            num_frames=n_frames,
            duration_s=n_frames / float(sample_rate),
            backend="silent",
            note_count=len(events),
            peak_dbfs=-120.0,
            mode=mode,
        )

    if mode == "chip":
        stereo = _render_events_python_synth(
            events, sample_rate, n_frames, "chip", channel, program, transpose
        )
        write_wav_stereo_16(wav_path, stereo, sample_rate)
        return StemRenderResult(
            track=track_name,
            channel=channel,
            wav_path=wav_path,
            sample_rate=sample_rate,
            num_frames=n_frames,
            duration_s=n_frames / float(sample_rate),
            backend="python-chip-synth",
            note_count=len(events),
            peak_dbfs=measure_peak_dbfs(stereo),
            mode=mode,
        )

    backend_name, handle = (
        _cached_backend
        if _cached_backend is not None
        else detect_synth_backend(soundfont, preferred=backend_pref)
    )

    if backend_name == "libfluidsynth-ctypes":
        stereo = _render_events_libfluidsynth(
            handle, events, soundfont, sample_rate, n_frames, mode, bank, program, transpose
        )
        write_wav_stereo_16(wav_path, stereo, sample_rate)
    elif backend_name == "fluidsynth-cli":
        stereo = _render_via_fluidsynth_cli(
            handle, events, soundfont, sample_rate, n_frames, mode, bank, program, transpose
        )
        write_wav_stereo_16(wav_path, stereo, sample_rate)
    elif backend_name == "wsl-libfluidsynth":
        _render_via_wsl_bridge(
            handle,
            Path(__file__).resolve(),
            midi_path,
            wav_path,
            soundfont,
            sample_rate,
            n_frames,
            mode,
            bank,
            program,
            transpose,
            channel,
        )
        stereo, _ = read_wav_stereo_float(wav_path)
    else:
        stereo = _render_events_python_synth(
            events, sample_rate, n_frames, mode, channel, program, transpose
        )
        write_wav_stereo_16(wav_path, stereo, sample_rate)

    return StemRenderResult(
        track=track_name,
        channel=channel,
        wav_path=wav_path,
        sample_rate=sample_rate,
        num_frames=stereo.shape[0],
        duration_s=stereo.shape[0] / float(sample_rate),
        backend=backend_name,
        note_count=len(events),
        peak_dbfs=measure_peak_dbfs(stereo),
        mode=mode,
    )


def _resolve_chip_ref_path(
    track_name: str,
    root: Path,
    wav_dir: Path,
    chip_wav: str | Path | None = None,
) -> Path | None:
    """Resolve _chip_ref.wav em ordem de prioridade:
    1. --chip-wav explícito
    2. assets/audio_remaster/stems/<track>/wav/_chip_ref.wav
    3. fallback para assets/audio_prototypes/<track>/B_studio_dsp.wav e A_original.wav
    """
    candidates: list[Path] = []
    if chip_wav is not None and str(chip_wav).strip():
        candidates.append(Path(chip_wav))
    candidates.append(wav_dir / "_chip_ref.wav")
    for proto_base in (root.parent / "audio_prototypes", Path("assets/audio_prototypes")):
        candidates.append(proto_base / track_name / "B_studio_dsp.wav")
        candidates.append(proto_base / track_name / "A_original.wav")
    for cand in candidates:
        if cand.exists() and cand.is_file():
            return cand
    return None


def _resample_stereo_linear(stereo: np.ndarray, src_sr: int, dst_sr: int) -> np.ndarray:
    if src_sr == dst_sr or stereo.shape[0] == 0:
        return stereo
    n_src = stereo.shape[0]
    n_dst = max(1, int(round(n_src * (float(dst_sr) / float(src_sr)))))
    x_src = np.linspace(0.0, 1.0, n_src, endpoint=False)
    x_dst = np.linspace(0.0, 1.0, n_dst, endpoint=False)
    l = np.interp(x_dst, x_src, stereo[:, 0])
    r = np.interp(x_dst, x_src, stereo[:, 1])
    return np.stack([l, r], axis=1).astype(np.float32)


def mix_track(
    track_name: str,
    base_dir: str | Path = DEFAULT_BASE_DIR,
    profile: dict[str, Any] | None = None,
    chip_wav: str | Path | None = None,
    update_manifest: bool = False,
) -> TrackMixResult:
    """Mixa os stems WAV existentes aplicando volume, pan, mute/solo, EQ 3 bandas e masterização."""
    root = Path(base_dir)
    profile_path = root / "profiles" / f"{track_name}.toml"
    prof = profile if profile is not None else load_profile(profile_path)

    tr_cfg = prof.get("track", {})
    channels_cfg: dict[str, dict[str, Any]] = prof.get("channels", {})
    sample_rate = int(tr_cfg.get("sample_rate", 48000))
    master_gain_db = float(tr_cfg.get("master_gain_db", 0.0))
    target_rms_dbfs = float(tr_cfg.get("target_rms_dbfs", -16.5))
    chip_blend = max(0.0, min(1.0, float(tr_cfg.get("chip_blend", 0.0))))

    wav_dir = root / "stems" / track_name / "wav"
    out_wav = root / "tracks" / f"{track_name}.wav"

    any_solo = any(
        bool(cfg.get("solo", False)) and not bool(cfg.get("mute", False))
        for cfg in channels_cfg.values()
    )

    stems_loaded: list[tuple[str, np.ndarray, dict[str, Any]]] = []
    max_len = 0
    for ch in CHANNEL_ORDER:
        cfg = channels_cfg.get(ch)
        if not cfg:
            continue
        if str(cfg.get("mode", "off")).lower() == "off":
            continue
        if bool(cfg.get("mute", False)):
            continue
        if any_solo and not bool(cfg.get("solo", False)):
            continue
        wp = wav_dir / f"{ch}.wav"
        if not wp.exists():
            continue
        audio, sr = read_wav_stereo_float(wp)
        if sr != sample_rate:
            raise ValueError(f"Sample rate incompatível em {wp}: {sr} != {sample_rate}")
        if audio.shape[0] > max_len:
            max_len = audio.shape[0]
        stems_loaded.append((ch, audio, cfg))

    if max_len == 0:
        max_len = sample_rate
    mix = np.zeros((max_len, 2), dtype=np.float64)
    active_channels: list[str] = []

    for ch, audio, cfg in stems_loaded:
        eq_audio = apply_3band_eq(
            audio,
            sample_rate=sample_rate,
            low_db=float(cfg.get("eq_low_db", 0.0)),
            mid_db=float(cfg.get("eq_mid_db", 0.0)),
            high_db=float(cfg.get("eq_high_db", 0.0)),
        )
        panned = apply_pan_and_gain(
            eq_audio,
            volume_db=float(cfg.get("volume_db", 0.0)),
            pan=float(cfg.get("pan", 0.0)),
        )
        mix[: panned.shape[0], :] += panned.astype(np.float64)
        active_channels.append(ch)

    # EQ musical sutil no barramento SF2 para fundir naturalmente com B_studio_dsp
    if active_channels:
        mix = apply_3band_eq(
            mix.astype(np.float32),
            sample_rate=sample_rate,
            low_db=1.0,
            mid_db=-1.2,
            high_db=0.8,
        ).astype(np.float64)

    # Mistura híbrida real com referência do chip original (_chip_ref.wav / B_studio_dsp.wav)
    chip_path = (
        _resolve_chip_ref_path(track_name, root, wav_dir, chip_wav)
        if chip_blend > 0.0
        else None
    )
    if chip_blend > 0.0 and chip_path is not None:
        chip_audio, chip_sr = read_wav_stereo_float(chip_path)
        if chip_sr != sample_rate:
            chip_audio = _resample_stereo_linear(chip_audio, chip_sr, sample_rate)
        n_ref = chip_audio.shape[0]
        if mix.shape[0] >= n_ref:
            sf2_bus = mix[:n_ref]
        else:
            pad = np.zeros((n_ref - mix.shape[0], 2), dtype=np.float64)
            sf2_bus = np.vstack([mix, pad])

        sf2_rms = measure_active_rms_dbfs(sf2_bus)
        chip_rms = measure_active_rms_dbfs(chip_audio)
        if chip_rms > -90.0:
            if sf2_rms > -90.0:
                chip_gain = 10.0 ** ((sf2_rms - chip_rms) / 20.0)
                mix = (1.0 - chip_blend) * sf2_bus + chip_blend * (
                    chip_audio.astype(np.float64) * chip_gain
                )
            else:
                mix = chip_audio.astype(np.float64)
        else:
            mix = sf2_bus

    mastered = apply_master_normalize_and_limit(
        mix,
        master_gain_db=master_gain_db,
        target_rms_dbfs=target_rms_dbfs,
    )
    write_wav_stereo_16(out_wav, mastered, sample_rate)

    if update_manifest:
        manifest_path = root / "manifest.toml"
        update_manifest_entry(
            manifest_path=manifest_path,
            track_id=int(tr_cfg.get("bgm_id", 0)),
            name=track_name,
            filename=f"{track_name}.wav",
            loop_start=int(tr_cfg.get("loop_start", 0)),
            loop_end=int(tr_cfg.get("loop_end", 0)),
            gain_db=0.0,
        )

    return TrackMixResult(
        track=track_name,
        out_wav=out_wav,
        sample_rate=sample_rate,
        num_frames=mastered.shape[0],
        duration_s=mastered.shape[0] / float(sample_rate),
        peak_dbfs=measure_peak_dbfs(mastered),
        rms_dbfs=measure_active_rms_dbfs(mastered),
        active_channels=active_channels,
    )


def remaster_track(
    track_name: str,
    base_dir: str | Path = DEFAULT_BASE_DIR,
    channel: str | None = None,
    mix_only: bool = False,
    backend_pref: str = "auto",
    chip_wav: str | Path | None = None,
    update_manifest: bool = False,
) -> TrackMixResult:
    """Renderiza todos os canais (ou apenas `channel`) e gera o mix final da faixa."""
    root = Path(base_dir)
    profile_path = root / "profiles" / f"{track_name}.toml"
    prof = load_profile(profile_path)
    tr_cfg = prof.get("track", {})
    sample_rate = int(tr_cfg.get("sample_rate", 48000))
    soundfont = str(tr_cfg.get("soundfont", DEFAULT_SOUNDFONT))
    midi_dir = root / "stems" / track_name / "midi"
    total_frames = _track_total_frames(midi_dir, sample_rate)

    rendered: list[StemRenderResult] = []
    backends: list[str] = []

    if not mix_only:
        cached_backend = detect_synth_backend(soundfont, preferred=backend_pref)
        channels_to_do = [channel] if channel else list(CHANNEL_ORDER)
        for ch in channels_to_do:
            if ch not in prof.get("channels", {}):
                continue
            res = render_channel_stem(
                track_name=track_name,
                channel=ch,
                base_dir=root,
                profile=prof,
                total_frames=total_frames,
                backend_pref=backend_pref,
                _cached_backend=cached_backend,
            )
            rendered.append(res)
            if res.backend not in ("silent",) and res.backend not in backends:
                backends.append(res.backend)

    mix_res = mix_track(
        track_name=track_name,
        base_dir=root,
        profile=prof,
        chip_wav=chip_wav,
        update_manifest=update_manifest,
    )
    mix_res.rendered_stems = rendered
    mix_res.backends_used = backends or ["mix-only"]
    return mix_res


# ---------------------------------------------------------------------------
# Entry point auxiliar interno para a ponte WSL -> libfluidsynth
# ---------------------------------------------------------------------------

def _handle_wsl_render_stem_json(raw_json: str) -> int:
    cfg = json.loads(raw_json)
    lib = _load_libfluidsynth()
    if lib is None:
        print("libfluidsynth não encontrada no WSL", file=sys.stderr)
        return 2
    notes_map = read_midi_notes(cfg["midi_path"])
    events = [e for evs in notes_map.values() for e in evs]
    events.sort(key=lambda e: (e.start_s, e.note))
    stereo = _render_events_libfluidsynth(
        lib=lib,
        events=events,
        soundfont=cfg["soundfont"],
        sample_rate=int(cfg["sample_rate"]),
        total_frames=int(cfg["total_frames"]),
        mode=str(cfg["mode"]),
        bank=int(cfg["bank"]),
        program=int(cfg["program"]),
        transpose=int(cfg["transpose"]),
    )
    write_wav_stereo_16(cfg["wav_out"], stereo, int(cfg["sample_rate"]))
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Renderiza stems por canal e mixa faixa remasterizada (48 kHz estéreo)."
    )
    ap.add_argument("track", nargs="?", help="Nome da faixa (ex.: 03_syria_blade)")
    ap.add_argument(
        "--base-dir",
        default=str(DEFAULT_BASE_DIR),
        help="Diretório raiz (default: assets/audio_remaster)",
    )
    ap.add_argument(
        "--channel",
        default=None,
        help="Re-renderiza apenas este canal (ex.: fm0) e remixa",
    )
    ap.add_argument(
        "--mix-only",
        action="store_true",
        help="Pula síntese dos stems e apenas remixa os WAVs existentes",
    )
    ap.add_argument(
        "--backend",
        choices=("auto", "ctypes", "cli", "wsl", "python"),
        default="auto",
        help="Força backend de síntese (default: auto)",
    )
    ap.add_argument(
        "--chip-wav",
        default=None,
        help="WAV de referência do chip original para misturar conforme chip_blend",
    )
    ap.add_argument(
        "--update-manifest",
        action="store_true",
        help="Atualiza assets/audio_remaster/manifest.toml com esta faixa",
    )
    ap.add_argument("--_wsl-render-stem-json", default=None, help=argparse.SUPPRESS)
    args = ap.parse_args(argv)

    if args._wsl_render_stem_json is not None:
        return _handle_wsl_render_stem_json(args._wsl_render_stem_json)

    if not args.track:
        ap.error("Informe o nome da faixa (ex.: 03_syria_blade)")

    res = remaster_track(
        track_name=args.track,
        base_dir=args.base_dir,
        channel=args.channel,
        mix_only=args.mix_only,
        backend_pref=args.backend,
        chip_wav=args.chip_wav,
        update_manifest=args.update_manifest,
    )
    print(
        f"[remaster_track] {res.track} -> {res.out_wav}\n"
        f"  backend={','.join(res.backends_used)} | sr={res.sample_rate} Hz | "
        f"dur={res.duration_s:.2f}s | rms={res.rms_dbfs:.2f} dBFS | peak={res.peak_dbfs:.2f} dBFS\n"
        f"  canais ativos ({len(res.active_channels)}): {', '.join(res.active_channels)}"
    )
    for st in res.rendered_stems:
        if st.note_count > 0:
            print(
                f"    stem {st.channel:9s}: mode={st.mode:10s} notes={st.note_count:4d} "
                f"backend={st.backend:20s} peak={st.peak_dbfs:6.2f} dBFS"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
