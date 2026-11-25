#!/usr/bin/env python3
"""tools/audio/mixer_studio.py — Estúdio Web local de mixagem por instrumento
para as músicas remasterizadas de Eternal Champions.

Opera 100% sobre os 17 arquivos MIDI em assets/audio_remaster/midi/*.mid e os
perfis em assets/audio_remaster/profiles/*.toml, sintetizando cada canal sob
demanda em memória RAM via tools/audio/tsf_midi_synth.dll (TinySoundFont tsf.h +
assets/soundfonts/TimGM6mb.sf2) — sem FluidSynth/WSL e sem gravar nenhum .wav
em disco.

Escuta estritamente em 127.0.0.1.

Uso:
    python tools/audio/mixer_studio.py
    python tools/audio/mixer_studio.py --port 8765
    python tools/audio/mixer_studio.py --no-browser --port 0
"""

from __future__ import annotations

import argparse
import atexit
from collections import OrderedDict
import ctypes
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import threading
import time
import tomllib
from typing import Any
from urllib.parse import unquote, urlsplit
import wave
import webbrowser

_THIS_DIR = Path(__file__).resolve().parent
_REPO_ROOT = _THIS_DIR.parents[1]
_UI_DIR = _THIS_DIR / "mixer_ui"

if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from chip_ring_to_midi import (  # noqa: E402
    CHANNEL_ORDER,
    NoteEvent,
    TEMPO_US,
    _decode_vlq,
    read_midi_notes,
)
import voice_catalog  # noqa: E402

VALID_MODES = frozenset({"auto", "gm", "percussion", "chip", "off"})
SYNTH_MODES = frozenset({"auto", "gm", "percussion"})
_MODE_TO_TSF_INT: dict[str, int] = {"gm": 0, "percussion": 1, "auto": 2}
DEFAULT_PORTABLE_SOUNDFONT = "assets/soundfonts/TimGM6mb.sf2"
SYNTH_ENGINE_LABEL = "TinySoundFont (tsf.h)"
DEFAULT_CACHE_CAPACITY = 64
_TRACK_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
_CHANNEL_RE = re.compile(r"^[a-z][a-z0-9_]{0,31}$")
_VOICE_KEY_RE = re.compile(r"^v_[0-9a-f]{8}$")
VOICE_MODES = frozenset({"gm", "percussion", "chip", "off"})
NTSC_FPS = 59.92275
ORIGINAL_FRAMES = 2400  # mesma janela do extract_all_midis (~40 s)

_STATIC_FILES: dict[str, tuple[str, str]] = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/index.html": ("index.html", "text/html; charset=utf-8"),
    "/static/style.css": ("style.css", "text/css; charset=utf-8"),
    "/static/app.js": ("app.js", "application/javascript; charset=utf-8"),
}


class ApiError(Exception):
    def __init__(self, status: int, message: str, code: str = "bad_request") -> None:
        super().__init__(message)
        self.status = status
        self.message = message
        self.code = code


class TsfMidiNote(ctypes.Structure):
    _fields_ = [
        ("start_sec", ctypes.c_double),
        ("end_sec", ctypes.c_double),
        ("note", ctypes.c_int32),
        ("velocity", ctypes.c_int32),
    ]


def _load_tsf_bridge_lib() -> ctypes.CDLL:
    if os.name == "nt":
        candidates = [_THIS_DIR / "tsf_midi_synth.dll"]
    elif sys.platform == "darwin":
        candidates = [_THIS_DIR / "libtsf_midi_synth.dylib", _THIS_DIR / "libtsf_midi_synth.so"]
    else:
        candidates = [_THIS_DIR / "libtsf_midi_synth.so"]

    last_err: Exception | None = None
    for cand in candidates:
        if cand.is_file():
            try:
                lib = ctypes.CDLL(str(cand.resolve()))
                lib.tsf_bridge_open.argtypes = [ctypes.c_char_p]
                lib.tsf_bridge_open.restype = ctypes.c_void_p
                lib.tsf_bridge_close.argtypes = [ctypes.c_void_p]
                lib.tsf_bridge_close.restype = None
                lib.tsf_bridge_render_channel.argtypes = [
                    ctypes.c_void_p,
                    ctypes.POINTER(TsfMidiNote),
                    ctypes.c_int32,
                    ctypes.c_int32,
                    ctypes.c_int32,
                    ctypes.c_int32,
                    ctypes.c_int32,
                    ctypes.c_int32,
                    ctypes.c_int32,
                    ctypes.POINTER(ctypes.c_int16),
                ]
                lib.tsf_bridge_render_channel.restype = ctypes.c_int
                return lib
            except OSError as exc:
                last_err = exc
    raise RuntimeError(
        f"Biblioteca nativa TinySoundFont não encontrada em {_THIS_DIR}: {candidates[0].name}"
        + (f" ({last_err})" if last_err else "")
    )


def _encode_wav_bytes_from_pcm(pcm_bytes: bytes | memoryview, sample_rate: int) -> bytes:
    bio = io.BytesIO()
    with wave.open(bio, "wb") as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(int(sample_rate))
        wf.writeframesraw(pcm_bytes)
    return bio.getvalue()


class TsfBridge:
    """Wrapper thread-safe sobre tools/audio/tsf_midi_synth.{dll,so}."""

    def __init__(self) -> None:
        self._lib = _load_tsf_bridge_lib()
        self._lock = threading.Lock()
        self._cond = threading.Condition(self._lock)
        self._handles: dict[str, int] = {}
        self._active_renders = 0
        self._closed = False

    def _get_handle_locked(self, sf2_path: str) -> int:
        if self._closed:
            raise RuntimeError("TsfBridge closed")
        resolved = str(Path(sf2_path).resolve()) if Path(sf2_path).exists() else sf2_path
        handle = self._handles.get(resolved)
        if handle:
            return handle
        if not Path(resolved).is_file():
            raise RuntimeError(f"SoundFont não encontrado: {resolved}")
        ptr = self._lib.tsf_bridge_open(resolved.encode("utf-8"))
        if not ptr:
            raise RuntimeError(f"Falha ao abrir SoundFont via TinySoundFont: {resolved}")
        self._handles[resolved] = int(ptr)
        return int(ptr)

    def get_handle(self, sf2_path: str) -> int:
        with self._cond:
            return self._get_handle_locked(sf2_path)

    def render_channel_wav_bytes(
        self,
        sf2_path: str,
        notes: list[NoteEvent],
        mode: str,
        bank: int,
        program: int,
        transpose: int,
        sample_rate: int,
        total_frames: int,
    ) -> tuple[bytes, str, int]:
        """Sintetiza um canal inteiro em memória RAM e retorna (wav_bytes, backend, out_frames)."""
        mode_norm = mode.lower()
        if mode_norm not in _MODE_TO_TSF_INT or not notes:
            silent_frames = max(1, min(int(total_frames), max(1, int(sample_rate) // 10)))
            wav_bytes = _encode_wav_bytes_from_pcm(b"\x00" * (silent_frames * 4), sample_rate)
            return wav_bytes, "silent", silent_frames

        with self._cond:
            if self._closed:
                raise RuntimeError("TsfBridge closed")
            handle = self._get_handle_locked(sf2_path)
            self._active_renders += 1

        try:
            n_notes = len(notes)
            note_arr = (TsfMidiNote * n_notes)(
                *[
                    TsfMidiNote(
                        float(ev.start_sec if hasattr(ev, "start_sec") else ev.start_s),
                        float(ev.end_sec if hasattr(ev, "end_sec") else ev.end_s),
                        int(ev.note),
                        int(ev.velocity),
                    )
                    for ev in notes
                ]
            )
            out_frames = max(1, int(total_frames))
            pcm_buf = (ctypes.c_int16 * (out_frames * 2))()
            rc = self._lib.tsf_bridge_render_channel(
                ctypes.c_void_p(handle),
                note_arr,
                ctypes.c_int32(n_notes),
                ctypes.c_int32(_MODE_TO_TSF_INT[mode_norm]),
                ctypes.c_int32(int(bank)),
                ctypes.c_int32(int(program)),
                ctypes.c_int32(int(transpose)),
                ctypes.c_int32(int(sample_rate)),
                ctypes.c_int32(out_frames),
                pcm_buf,
            )
            if rc < 0:
                raise RuntimeError(f"tsf_bridge_render_channel falhou (rc={rc}).")

            wav_bytes = _encode_wav_bytes_from_pcm(memoryview(pcm_buf), sample_rate)
            return wav_bytes, "tsf_midi_synth", out_frames
        finally:
            with self._cond:
                self._active_renders -= 1
                if self._active_renders == 0:
                    self._cond.notify_all()

    def close(self) -> None:
        with self._cond:
            if self._closed:
                return
            self._closed = True
            while self._active_renders > 0:
                self._cond.wait()
            for ptr in self._handles.values():
                if ptr:
                    try:
                        self._lib.tsf_bridge_close(ctypes.c_void_p(ptr))
                    except Exception:
                        pass
            self._handles.clear()


def _toml_val(v: Any) -> str:
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
    raise TypeError(f"Tipo TOML não suportado: {type(v)!r}")


def load_profile(profile_path: str | Path) -> dict[str, Any]:
    with open(profile_path, "rb") as f:
        return tomllib.load(f)


def format_profile_toml(profile: dict[str, Any]) -> str:
    """Serializa perfil no contrato exato de assets/audio_remaster/profiles/<track>.toml."""
    tr = profile["track"]
    lines: list[str] = [
        "# Perfil de remasterização por canal (editável manualmente ou via mixer_studio.py)",
        "[track]",
    ]
    track_keys = (
        "name",
        "bgm_id",
        "soundfont",
        "sample_rate",
        "loop_start",
        "loop_end",
        "master_gain_db",
        "target_rms_dbfs",
        "chip_blend",
    )
    for k in track_keys:
        if k in tr:
            lines.append(f"{k} = {_toml_val(tr[k])}")
    for k, v in tr.items():
        if k not in track_keys:
            lines.append(f"{k} = {_toml_val(v)}")

    ch_keys = (
        "role",
        "mode",
        "program",
        "bank",
        "volume_db",
        "pan",
        "mute",
        "solo",
        "eq_low_db",
        "eq_mid_db",
        "eq_high_db",
        "transpose",
    )
    channels: dict[str, dict[str, Any]] = profile.get("channels", {})
    ordered_chs = list(CHANNEL_ORDER) + [c for c in channels if c not in CHANNEL_ORDER]
    for ch in ordered_chs:
        if ch not in channels:
            continue
        cfg = channels[ch]
        lines.extend(["", f"[channels.{ch}]"])
        for k in ch_keys:
            if k in cfg:
                lines.append(f"{k} = {_toml_val(cfg[k])}")
        for k, v in cfg.items():
            if k not in ch_keys:
                lines.append(f"{k} = {_toml_val(v)}")
    lines.append("")
    return "\n".join(lines)


def save_profile(profile_path: str | Path, profile: dict[str, Any]) -> None:
    p = Path(profile_path)
    p.parent.mkdir(parents=True, exist_ok=True)
    content = format_profile_toml(profile)
    tmp_path = p.parent / f".{p.name}.{os.getpid()}.{threading.get_ident()}.tmp"
    try:
        with open(tmp_path, "w", encoding="utf-8", newline="\n") as f:
            f.write(content)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp_path, p)
    finally:
        if tmp_path.exists():
            try:
                tmp_path.unlink()
            except OSError:
                pass


def _default_dir(rel: str) -> Path:
    cwd_candidate = Path(rel)
    if cwd_candidate.exists():
        return cwd_candidate.resolve()
    return (_REPO_ROOT / rel).resolve()


def _resolve_soundfont_path(raw_sf: str) -> str:
    """Resolve SoundFont portátil no repositório."""
    sf = (raw_sf or "").strip() or DEFAULT_PORTABLE_SOUNDFONT
    cand = Path(sf)
    if cand.is_file():
        return str(cand.resolve()) if not cand.is_absolute() else str(cand)
    repo_cand = _REPO_ROOT / sf
    if repo_cand.is_file():
        return str(repo_cand.resolve())
    repo_default = _REPO_ROOT / DEFAULT_PORTABLE_SOUNDFONT
    if repo_default.is_file():
        return str(repo_default.resolve())
    return str(repo_default)


def _ensure_within(path: Path, root: Path) -> Path:
    resolved_root = root.resolve()
    resolved_path = path.resolve()
    if not resolved_path.is_relative_to(resolved_root):
        raise ApiError(400, "Caminho inválido fora do diretório permitido.", "path_traversal")
    return resolved_path


def validate_track_name(raw: str) -> str:
    if not raw or "\x00" in raw or "/" in raw or "\\" in raw or ".." in raw:
        raise ApiError(400, "Nome de faixa inválido.", "invalid_track")
    if not _TRACK_RE.fullmatch(raw) or Path(raw).name != raw:
        raise ApiError(400, f"Nome de faixa inválido: {raw!r}", "invalid_track")
    return raw


def validate_channel_name(raw: str) -> str:
    if not raw or "\x00" in raw or "/" in raw or "\\" in raw or ".." in raw:
        raise ApiError(400, "Nome de canal inválido.", "invalid_channel")
    if not _CHANNEL_RE.fullmatch(raw) or Path(raw).name != raw:
        raise ApiError(400, f"Nome de canal inválido: {raw!r}", "invalid_channel")
    return raw


def _clamp_float(val: Any, lo: float, hi: float, field: str) -> float:
    try:
        num = float(val)
    except (TypeError, ValueError) as exc:
        raise ApiError(400, f"Valor numérico inválido em '{field}'.", "invalid_field") from exc
    if num != num or num in (float("inf"), float("-inf")):
        raise ApiError(400, f"Valor não finito em '{field}'.", "invalid_field")
    return max(lo, min(hi, num))


def _clamp_int(val: Any, lo: int, hi: int, field: str) -> int:
    try:
        if isinstance(val, bool):
            raise ValueError("bool")
        num = float(val)
        if not math.isfinite(num):
            raise ValueError("non-finite")
        rounded = int(round(num))
    except (TypeError, ValueError, OverflowError) as exc:
        raise ApiError(400, f"Valor inteiro inválido em '{field}'.", "invalid_field") from exc
    return max(lo, min(hi, rounded))


def _parse_bool(val: Any, field: str) -> bool:
    if isinstance(val, bool):
        return val
    if isinstance(val, int) and val in (0, 1):
        return bool(val)
    raise ApiError(400, f"Valor booleano inválido em '{field}'.", "invalid_field")


def _probe_wav_header(wav_path: Path) -> dict[str, Any]:
    stat = wav_path.stat()
    info: dict[str, Any] = {
        "size": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
        "sample_rate": 0,
        "num_frames": 0,
        "duration_s": 0.0,
        "channels": 0,
    }
    try:
        with wave.open(str(wav_path), "rb") as wf:
            sr = wf.getframerate()
            nf = wf.getnframes()
            info["sample_rate"] = sr
            info["num_frames"] = nf
            info["channels"] = wf.getnchannels()
            info["duration_s"] = round(nf / float(sr), 4) if sr > 0 else 0.0
    except Exception:
        pass
    return info


def read_midi_named_tracks(
    midi_path: str | os.PathLike[str],
) -> dict[str, list[NoteEvent]]:
    """Lê um MIDI Tipo 1 e devolve as notas por NOME de trilha (1o token do nome).

    Serve aos MIDIs por voz (`v_<hash> [nome]`, `psg0`, `dac`...) — o read_midi_notes()
    legado só entende os nomes canônicos de canal físico.
    """
    data = Path(midi_path).read_bytes()
    if len(data) < 14 or data[0:4] != b"MThd":
        raise ValueError(f"Arquivo MIDI inválido: {midi_path}")
    hdr_len = struct.unpack_from(">I", data, 4)[0]
    _fmt, n_tracks, division = struct.unpack_from(">HHH", data, 8)
    ppqn = max(1, division & 0x7FFF)
    tempo_us = TEMPO_US
    pos = 8 + hdr_len
    out: dict[str, list[NoteEvent]] = {}
    raw_tracks: list[tuple[str, list[tuple[int, str, int, int, int]]]] = []
    for _ in range(n_tracks):
        if pos + 8 > len(data) or data[pos : pos + 4] != b"MTrk":
            break
        end = min(len(data), pos + 8 + struct.unpack_from(">I", data, pos + 4)[0])
        p = pos + 8
        pos = end
        tick = 0
        running = 0
        name = ""
        msgs: list[tuple[int, str, int, int, int]] = []
        while p < end:
            delta, p = _decode_vlq(data, p)
            tick += delta
            if p >= end:
                break
            status = data[p]
            if status < 0x80:
                status = running
            else:
                p += 1
                if status < 0xF0:
                    running = status
            if status == 0xFF:
                mtype = data[p]
                p += 1
                mlen, p = _decode_vlq(data, p)
                mdata = data[p : p + mlen]
                p += mlen
                if mtype == 0x03:
                    name = mdata.decode("utf-8", errors="replace").strip()
                elif mtype == 0x51 and len(mdata) == 3:
                    tempo_us = (mdata[0] << 16) | (mdata[1] << 8) | mdata[2]
                elif mtype == 0x2F:
                    break
            elif status in (0xF0, 0xF7):
                slen, p = _decode_vlq(data, p)
                p += slen
            else:
                kind = status & 0xF0
                ch = status & 0x0F
                if kind in (0xC0, 0xD0):
                    p += 1
                else:
                    d1, d2 = data[p], data[p + 1]
                    p += 2
                    if kind == 0x90 and d2 > 0:
                        msgs.append((tick, "on", ch, d1, d2))
                    elif kind in (0x80, 0x90):
                        msgs.append((tick, "off", ch, d1, 0))
        raw_tracks.append((name, msgs))

    sec_per_tick = (tempo_us / 1_000_000.0) / ppqn
    for name, msgs in raw_tracks:
        key = (name.split() or [""])[0]
        if not key or not msgs:
            continue
        active: dict[tuple[int, int], tuple[int, int]] = {}
        evs = out.setdefault(key, [])
        for tick, kind, ch, note, vel in msgs:
            k = (ch, note)
            if kind == "on":
                if k in active:
                    t0, v0 = active.pop(k)
                    evs.append(NoteEvent(key, t0 * sec_per_tick, max(t0 + 1, tick) * sec_per_tick, note, v0, ch))
                active[k] = (tick, vel)
            elif k in active:
                t0, v0 = active.pop(k)
                evs.append(NoteEvent(key, t0 * sec_per_tick, max(t0 + 1, tick) * sec_per_tick, note, v0, ch))
        for (ch, note), (t0, v0) in active.items():
            evs.append(NoteEvent(key, t0 * sec_per_tick, t0 * sec_per_tick + 0.1, note, v0, ch))
        evs.sort(key=lambda n: (n.start_s, n.note))
    return out


def _resample_pcm16_stereo(pcm: bytes, sr: int, target: int) -> tuple[bytes, int]:
    """O --wav do jogo sai em taxa nativa alta (ex.: 223721 Hz); leva a 48 kHz (média + interp).
    Sem numpy mantém a taxa original (o navegador reamostra no decodeAudioData)."""
    if sr == target:
        return pcm, sr
    try:
        import numpy as np
    except ImportError:
        return pcm, sr
    a = np.frombuffer(pcm, dtype="<i2").reshape(-1, 2).astype(np.float64)
    win = max(1, int(round(sr / target)))
    cs = np.vstack([np.zeros((1, 2)), np.cumsum(a, axis=0)])
    smooth = (cs[win:] - cs[:-win]) / win if len(a) > win else a
    n_out = int(len(a) * target / sr)
    src_pos = np.arange(n_out) * (sr / target)
    idx = np.arange(len(smooth))
    out = np.stack([np.interp(src_pos, idx, smooth[:, c]) for c in range(2)], axis=1)
    return np.clip(np.rint(out), -32768, 32767).astype("<i2").tobytes(), target


def _clean_voice_name(raw: Any) -> str:
    s = "".join(c for c in str(raw) if c.isprintable())
    return s.strip()[:48]


class MixerApp:
    """Camada de estado e serviço do Mixer Studio (100% MIDI + síntese RAM via tsf.h)."""

    def __init__(
        self,
        base_dir: Path,
        prototypes_dir: Path,
        backend_pref: str = "tsf",
        cache_capacity: int = DEFAULT_CACHE_CAPACITY,
    ) -> None:
        self.base_dir = base_dir.resolve()
        self.prototypes_dir = prototypes_dir.resolve()
        self.backend_pref = backend_pref
        self._cache_capacity = max(1, int(cache_capacity))
        self._locks_guard = threading.Lock()
        self._track_locks: dict[str, threading.Lock] = {}

        self._midi_cache_lock = threading.Lock()
        self._midi_cache: dict[str, tuple[int, dict[str, list[NoteEvent]], float]] = {}

        self._wav_cache_lock = threading.Lock()
        self._wav_cache: OrderedDict[tuple[Any, ...], tuple[bytes, dict[str, Any]]] = OrderedDict()
        self._preview_overrides: dict[tuple[str, str], dict[str, Any]] = {}

        # A5.4: catálogo de vozes, MIDIs por voz e "original" gerado headless
        self._voices_lock = threading.RLock()
        self._voice_midi_cache: dict[str, tuple[int, dict[str, list[NoteEvent]], float]] = {}
        self._orig_cache: dict[str, bytes] = {}
        self._orig_locks: dict[str, threading.Lock] = {}

        self._tsf_bridge = TsfBridge()
        default_sf = _resolve_soundfont_path(DEFAULT_PORTABLE_SOUNDFONT)
        if Path(default_sf).is_file():
            self._tsf_bridge.get_handle(default_sf)
        atexit.register(self.close)

    def close(self) -> None:
        try:
            self._tsf_bridge.close()
        except Exception:
            pass
        with self._wav_cache_lock:
            self._wav_cache.clear()
            self._preview_overrides.clear()
        with self._midi_cache_lock:
            self._midi_cache.clear()

    @property
    def profiles_dir(self) -> Path:
        return self.base_dir / "profiles"

    @property
    def midi_dir(self) -> Path:
        return self.base_dir / "midi"

    def _track_lock(self, track_name: str) -> threading.Lock:
        with self._locks_guard:
            lock = self._track_locks.get(track_name)
            if lock is None:
                lock = threading.Lock()
                self._track_locks[track_name] = lock
            return lock

    def _profile_path(self, track_name: str) -> Path:
        t = validate_track_name(track_name)
        return _ensure_within(self.profiles_dir / f"{t}.toml", self.profiles_dir)

    def _midi_path(self, track_name: str) -> Path:
        t = validate_track_name(track_name)
        return _ensure_within(self.midi_dir / f"{t}.mid", self.midi_dir)

    def _require_profile_path(self, track_name: str) -> Path:
        p = self._profile_path(track_name)
        if not p.is_file():
            raise ApiError(404, f"Perfil não encontrado: {track_name}", "track_not_found")
        return p

    def _original_wav_path(self, track_name: str) -> Path | None:
        t = validate_track_name(track_name)
        if self.prototypes_dir.is_dir():
            cand = _ensure_within(
                self.prototypes_dir / t / "A_original.wav", self.prototypes_dir
            )
            if cand.is_file():
                return cand
        return None

    def _chip_ref_wav_path(self, track_name: str) -> Path | None:
        t = validate_track_name(track_name)
        if self.prototypes_dir.is_dir():
            for fname in ("B_studio_dsp.wav", "A_original.wav"):
                cand = _ensure_within(
                    self.prototypes_dir / t / fname, self.prototypes_dir
                )
                if cand.is_file():
                    return cand
        return None

    def _load_track_midi(
        self, track_name: str
    ) -> tuple[int, dict[str, list[NoteEvent]], float] | None:
        midi_path = self._midi_path(track_name)
        if not midi_path.is_file():
            return None
        mtime_ns = midi_path.stat().st_mtime_ns
        with self._midi_cache_lock:
            cached = self._midi_cache.get(track_name)
            if cached is not None and cached[0] == mtime_ns:
                return cached

        notes_by_ch = read_midi_notes(midi_path)
        max_end = max(
            (n.end_s for ch in CHANNEL_ORDER for n in notes_by_ch.get(ch, [])),
            default=0.0,
        )
        track_duration_s = max(1.0, max_end + 0.5)
        entry = (mtime_ns, notes_by_ch, track_duration_s)
        with self._midi_cache_lock:
            self._midi_cache[track_name] = entry
        return entry

    # ------------------------------------------------------------------
    # A5.4 — modo por instrumento (voices.toml + midi_voices/<slug>.mid)
    # ------------------------------------------------------------------
    @property
    def voices_path(self) -> Path:
        return self.base_dir / "voices.toml"

    @property
    def voice_midi_dir(self) -> Path:
        return self.base_dir / "midi_voices"

    def _load_voices(self) -> dict[str, dict[str, Any]]:
        """hash -> {"edit": {...campos editáveis...}, "meta": {...}} (formato do voice_catalog)."""
        with self._voices_lock:
            if not self.voices_path.is_file():
                return {}
            with open(self.voices_path, "rb") as f:
                raw = tomllib.load(f).get("voices", {})
        out: dict[str, dict[str, Any]] = {}
        for h, v in raw.items():
            if not isinstance(v, dict):
                continue
            edit = {k: v.get(k, d) for k, d in voice_catalog.EDITABLE_DEFAULTS.items()}
            out[h] = {"edit": edit, "meta": dict(v.get("meta", {}))}
        return out

    def _load_voice_midi(
        self, track_name: str
    ) -> tuple[int, dict[str, list[NoteEvent]], float] | None:
        t = validate_track_name(track_name)
        path = _ensure_within(self.voice_midi_dir / f"{t}.mid", self.voice_midi_dir)
        if not path.is_file():
            return None
        mtime_ns = path.stat().st_mtime_ns
        with self._midi_cache_lock:
            cached = self._voice_midi_cache.get(t)
            if cached is not None and cached[0] == mtime_ns:
                return cached
        notes = read_midi_named_tracks(path)
        max_end = max((n.end_s for evs in notes.values() for n in evs), default=0.0)
        entry = (mtime_ns, notes, max(1.0, max_end + 0.5))
        with self._midi_cache_lock:
            self._voice_midi_cache[t] = entry
        return entry

    def _voice_mode(self, track_name: str) -> bool:
        info = self._load_voice_midi(track_name)
        if info is None or not any(_VOICE_KEY_RE.match(k) for k in info[1]):
            return False
        return self.voices_path.is_file()

    def _midi_for(self, track_name: str):
        if self._voice_mode(track_name):
            return self._load_voice_midi(track_name)
        return self._load_track_midi(track_name)

    def _voice_cfgs(self, track_name: str) -> "OrderedDict[str, dict[str, Any]]":
        info = self._load_voice_midi(track_name)
        cfgs: OrderedDict[str, dict[str, Any]] = OrderedDict()
        if info is None:
            return cfgs
        notes = info[1]
        voices = self._load_voices()
        keys = [k for k in notes if _VOICE_KEY_RE.match(k)]
        keys.sort(key=lambda k: (-len(notes[k]), k))
        for k in keys:
            v = voices.get(k[2:])
            if v is None:
                continue
            e, m = v["edit"], v["meta"]
            mode = str(e.get("mode", "gm")).lower()
            if mode not in VOICE_MODES:
                mode = "gm"
            songs = [str(x) for x in m.get("songs", [])]
            cfgs[k] = {
                "voice": True,
                "hash": k[2:],
                "role": str(m.get("suggestion", "")),
                "suggestion": str(m.get("suggestion", "")),
                "name": str(e.get("name", "")),
                "mode": mode,
                "program": int(e.get("program", 0)),
                "bank": 128 if mode == "percussion" else int(e.get("bank", 0)),
                "transpose": int(e.get("transpose", 0)),
                "volume_db": float(e.get("volume_db", 0.0)),
                "drum_note": int(e.get("drum_note", 0)),
                "pan": 0.0,
                "mute": False,
                "solo": False,
                "eq_low_db": 0.0,
                "eq_mid_db": 0.0,
                "eq_high_db": 0.0,
                "songs": songs,
                "used_in": len(songs),
                "alg": int(m.get("alg", 0)),
                "fb": int(m.get("fb", 0)),
                "note_median": int(m.get("note_median", 0)),
            }
        return cfgs

    @staticmethod
    def _apply_voice_patch(cfg: dict[str, Any], patch: dict[str, Any]) -> dict[str, Any]:
        """Aplica (validando) os campos editáveis de uma voz. mute/solo/pan/eq são só de preview."""
        if "name" in patch:
            cfg["name"] = _clean_voice_name(patch["name"])
        if "mode" in patch:
            mode = str(patch["mode"]).strip().lower()
            if mode not in VOICE_MODES:
                raise ApiError(
                    400,
                    f"Modo inválido para voz: {mode!r} (esperado: {sorted(VOICE_MODES)})",
                    "invalid_mode",
                )
            cfg["mode"] = mode
            if "bank" not in patch:
                cfg["bank"] = 128 if mode == "percussion" else 0
        if "program" in patch:
            cfg["program"] = _clamp_int(patch["program"], 0, 127, "program")
        if "bank" in patch:
            cfg["bank"] = _clamp_int(patch["bank"], 0, 128, "bank")
        if "transpose" in patch:
            cfg["transpose"] = _clamp_int(patch["transpose"], -24, 24, "transpose")
        if "volume_db" in patch:
            cfg["volume_db"] = round(_clamp_float(patch["volume_db"], -60.0, 24.0, "volume_db"), 3)
        if "drum_note" in patch:
            cfg["drum_note"] = _clamp_int(patch["drum_note"], 0, 127, "drum_note")
        return cfg

    def _voice_profile(
        self,
        track_name: str,
        profile: dict[str, Any],
        patches: dict[str, dict[str, Any]] | None = None,
    ) -> dict[str, Any]:
        """Perfil "virtual": vozes FM (de voices.toml) + canais não-FM do perfil da música."""
        cfgs = self._voice_cfgs(track_name)
        for k, pt in (patches or {}).items():
            if k in cfgs and isinstance(pt, dict):
                self._apply_voice_patch(cfgs[k], pt)
        channels: OrderedDict[str, dict[str, Any]] = OrderedDict(cfgs)
        for ch in CHANNEL_ORDER:
            if not ch.startswith("fm") and ch in profile.get("channels", {}):
                channels[ch] = dict(profile["channels"][ch])
        return {"track": dict(profile.get("track", {})), "channels": channels}

    def _save_voice_edits(self, patches: dict[str, dict[str, Any]]) -> None:
        """Grava edições GLOBAIS em voices.toml via writer do voice_catalog (meta preservado)."""
        if not patches:
            return
        with self._voices_lock:
            entries = self._load_voices()
            for k, pt in patches.items():
                if not _VOICE_KEY_RE.match(k) or k[2:] not in entries or not isinstance(pt, dict):
                    raise ApiError(404, f"Voz desconhecida: {k}", "voice_not_found")
                entry = entries[k[2:]]
                new_edit = self._apply_voice_patch(dict(entry["edit"]), pt)
                entry["edit"] = {kk: new_edit[kk] for kk in voice_catalog.EDITABLE_DEFAULTS}
            voice_catalog.write_toml(entries, self.voices_path)
        with self._wav_cache_lock:
            for k in list(self._preview_overrides):
                if k[1] in patches:
                    del self._preview_overrides[k]

    def list_voices(self, track_name: str | None = None) -> dict[str, Any]:
        if track_name is not None:
            cfgs = self._voice_cfgs(track_name)
            info = self._load_voice_midi(track_name)
            notes = info[1] if info else {}
            return {
                "track": track_name,
                "voices": [
                    {**cfg, "key": k, "song_notes": len(notes.get(k, []))}
                    for k, cfg in cfgs.items()
                ],
            }
        voices = self._load_voices()
        return {
            "voices": [
                {
                    "hash": h,
                    **v["edit"],
                    "suggestion": v["meta"].get("suggestion", ""),
                    "songs": v["meta"].get("songs", []),
                    "notes": v["meta"].get("notes", 0),
                }
                for h, v in sorted(voices.items())
            ]
        }

    # ---- "original" sem WAV em disco: jogo headless -> RAM -------------------
    def _orig_lock(self, track_name: str) -> threading.Lock:
        with self._locks_guard:
            return self._orig_locks.setdefault(track_name, threading.Lock())

    def _game_paths(self) -> tuple[Path, Path]:
        return (
            _REPO_ROOT / "build" / "EternalChampionsRecomp.exe",
            _REPO_ROOT / "roms" / "eternal_champions_us.gen",
        )

    def can_generate_original(self) -> bool:
        exe, rom = self._game_paths()
        return exe.is_file() and rom.is_file()

    def generate_original(self, track_name: str) -> dict[str, Any]:
        """Roda o jogo headless (--audio-remaster off --wav tmp) no Music Test, recorta a
        janela da música, guarda o WAV só em RAM e apaga o temporário."""
        import extract_all_midis as eam

        t = validate_track_name(track_name)
        p = self._require_profile_path(t)
        bgm_id = int(load_profile(p).get("track", {}).get("bgm_id", -1))
        if not 0 <= bgm_id <= 16:
            raise ApiError(400, "Faixa sem bgm_id válido (0..16) para gerar o original.", "no_bgm_id")
        if not self.can_generate_original():
            raise ApiError(404, "build/EternalChampionsRecomp.exe ou ROM ausentes.", "game_missing")
        exe, rom = self._game_paths()

        with self._orig_lock(t):
            if t in self._orig_cache:
                return {"ok": True, "cached": True, "bgm_id": bgm_id}
            t0 = time.perf_counter()
            with tempfile.TemporaryDirectory(prefix="ec_orig_") as td:
                tdp = Path(td)
                script = tdp / "orig.input"
                wav_tmp = tdp / "orig.wav"
                if bgm_id <= 15:
                    sb = eam._ScriptBuilder()
                    eam._boot_to_main_menu(sb)
                    for _ in range(4):
                        sb.tap("DOWN", hold=10, post=20)
                    sb.tap("START", hold=15, post=90)
                    for _ in range(3):
                        sb.tap("DOWN", hold=10, post=15)
                    sb.tap("B", hold=10, post=50)
                    for _ in range(bgm_id):
                        sb.tap("RIGHT", hold=8, post=20)
                    f_start = sb.frame
                    sb.tap("A", hold=10, post=ORIGINAL_FRAMES)
                    f_end = sb.frame
                    sb.write(script)
                else:
                    f_start, f_end = eam._build_char_select_script(
                        script, tdp / "orig.ram", ORIGINAL_FRAMES
                    )
                env = os.environ.copy()
                env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
                cmd = [
                    str(exe), "--max-frames", str(f_end + 300), "--turbo", "--no-launcher",
                    "--audio-remaster", "off", "--wav", str(wav_tmp),
                    "--input-script", str(script), str(rom),
                ]
                res = subprocess.run(
                    cmd, cwd=str(_REPO_ROOT), env=env, capture_output=True, text=True, timeout=600
                )
                if res.returncode != 0 or not wav_tmp.is_file():
                    raise ApiError(
                        500,
                        f"Falha ao gerar original (exit={res.returncode}): {res.stderr[-400:]}",
                        "original_failed",
                    )
                with wave.open(str(wav_tmp), "rb") as wf:
                    sr, nch, sw = wf.getframerate(), wf.getnchannels(), wf.getsampwidth()
                    if nch != 2 or sw != 2:
                        raise ApiError(500, f"WAV inesperado ({nch}ch/{sw * 8}bit).", "original_failed")
                    first = int(f_start / NTSC_FPS * sr)
                    count = int(ORIGINAL_FRAMES / NTSC_FPS * sr)
                    wf.setpos(min(first, wf.getnframes()))
                    pcm = wf.readframes(count)
            # tempdir (e o WAV completo) já apagados; só o recorte fica em RAM
            pcm, sr = _resample_pcm16_stereo(pcm, sr, 48000)
            self._orig_cache[t] = _encode_wav_bytes_from_pcm(pcm, sr)
            return {
                "ok": True,
                "cached": False,
                "bgm_id": bgm_id,
                "duration_s": round(len(pcm) / 4 / sr, 3),
                "elapsed_ms": round((time.perf_counter() - t0) * 1000.0, 1),
            }

    def _invalidate_wav_cache(self, track_name: str, channel: str | None = None) -> None:
        with self._wav_cache_lock:
            keys_to_del = [
                k
                for k in self._wav_cache
                if k[0] == track_name and (channel is None or k[1] == channel)
            ]
            for k in keys_to_del:
                del self._wav_cache[k]

    def _clear_preview_overrides(self, track_name: str, channel: str | None = None) -> None:
        with self._wav_cache_lock:
            keys = [
                k
                for k in self._preview_overrides
                if k[0] == track_name and (channel is None or k[1] == channel)
            ]
            for k in keys:
                del self._preview_overrides[k]

    def _synthesize_channel_wav(
        self,
        track_name: str,
        channel: str,
        ch_cfg: dict[str, Any],
        tr_cfg: dict[str, Any],
    ) -> tuple[bytes, dict[str, Any], float]:
        midi_info = self._midi_for(track_name)
        if midi_info is None:
            raise ApiError(
                404,
                f"Arquivo MIDI não encontrado para '{track_name}'.",
                "midi_not_found",
            )
        midi_mtime_ns, notes_by_ch, track_duration_s = midi_info
        sample_rate = int(tr_cfg.get("sample_rate", 48000))
        total_frames = max(sample_rate, int(round(track_duration_s * sample_rate)))
        soundfont = _resolve_soundfont_path(
            str(tr_cfg.get("soundfont", DEFAULT_PORTABLE_SOUNDFONT))
        )
        mode = str(ch_cfg.get("mode", "off")).lower()
        bank = int(ch_cfg.get("bank", 128 if mode == "percussion" else 0))
        program = int(ch_cfg.get("program", 0))
        transpose = int(ch_cfg.get("transpose", 0))
        notes = notes_by_ch.get(channel, [])
        # voz em percussao com drum_note fixo (>0): toda nota vira essa nota GM de bateria
        drum_note = int(ch_cfg.get("drum_note", 0)) if (
            mode == "percussion" and _VOICE_KEY_RE.match(channel)
        ) else 0
        if drum_note > 0:
            notes = [
                NoteEvent(n.channel, n.start_s, n.end_s, drum_note, n.velocity, n.midi_channel)
                for n in notes
            ]

        cache_key = (
            track_name,
            channel,
            mode,
            bank,
            program,
            transpose,
            drum_note,
            sample_rate,
            soundfont,
            midi_mtime_ns,
        )

        with self._wav_cache_lock:
            cached = self._wav_cache.get(cache_key)
            if cached is not None:
                self._wav_cache.move_to_end(cache_key)
                return cached[0], cached[1], 0.0

        t0 = time.perf_counter()
        wav_bytes, backend_used, out_frames = self._tsf_bridge.render_channel_wav_bytes(
            sf2_path=soundfont,
            notes=notes,
            mode=mode,
            bank=bank,
            program=program,
            transpose=transpose,
            sample_rate=sample_rate,
            total_frames=total_frames,
        )
        elapsed_ms = round((time.perf_counter() - t0) * 1000.0, 2)
        meta: dict[str, Any] = {
            "duration_s": round(out_frames / float(sample_rate), 4),
            "sample_rate": sample_rate,
            "num_frames": out_frames,
            "note_count": len(notes),
            "mode": mode,
            "backend": backend_used,
            "size": len(wav_bytes),
            "version_ts": time.time_ns(),
        }

        with self._wav_cache_lock:
            self._wav_cache[cache_key] = (wav_bytes, meta)
            self._wav_cache.move_to_end(cache_key)
            while len(self._wav_cache) > self._cache_capacity:
                self._wav_cache.popitem(last=False)

        return wav_bytes, meta, elapsed_ms

    def get_stem_wav_bytes(self, track_name: str, channel: str) -> bytes:
        t = validate_track_name(track_name)
        ch = validate_channel_name(channel)
        p = self._require_profile_path(t)
        with self._track_lock(t):
            prof = load_profile(p)
            tr_cfg = dict(prof.get("track", {}))
            if _VOICE_KEY_RE.match(ch):
                vcfg = self._voice_cfgs(t).get(ch)
                if vcfg is None:
                    raise ApiError(404, f"Voz desconhecida nesta música: {ch}", "channel_not_found")
                ch_cfg = dict(vcfg)
            else:
                ch_cfg = dict(prof.get("channels", {}).get(ch, {}))

            with self._wav_cache_lock:
                override = self._preview_overrides.get((t, ch))
                if override is not None:
                    ch_cfg.update(override["ch"])
                    if override.get("soundfont"):
                        tr_cfg["soundfont"] = override["soundfont"]
                    if override.get("sample_rate"):
                        tr_cfg["sample_rate"] = override["sample_rate"]

        wav_bytes, _meta, _elapsed = self._synthesize_channel_wav(t, ch, ch_cfg, tr_cfg)
        return wav_bytes

    def get_midi_bytes(self, track_name: str) -> bytes:
        midi_path = self._midi_path(track_name)
        if not midi_path.is_file():
            raise ApiError(
                404,
                f"Arquivo MIDI não encontrado para '{track_name}'.",
                "midi_not_found",
            )
        return midi_path.read_bytes()

    def list_tracks(self) -> dict[str, Any]:
        items: list[dict[str, Any]] = []
        if self.profiles_dir.is_dir():
            for p in sorted(self.profiles_dir.glob("*.toml")):
                tname = p.stem
                try:
                    validate_track_name(tname)
                    with self._track_lock(tname):
                        prof = load_profile(p)
                except Exception:
                    continue
                tr = prof.get("track", {})
                chs = prof.get("channels", {})
                midi_info = self._load_track_midi(tname)
                has_midi = midi_info is not None
                if midi_info is not None:
                    _, notes_by_ch, dur_s = midi_info
                    active_midi_chs = sum(1 for ch in CHANNEL_ORDER if notes_by_ch.get(ch))
                    total_notes = sum(len(notes_by_ch.get(ch, [])) for ch in CHANNEL_ORDER)
                else:
                    active_midi_chs = 0
                    total_notes = 0
                    dur_s = 0.0

                active_chs = [
                    ch
                    for ch in CHANNEL_ORDER
                    if str(chs.get(ch, {}).get("mode", "off")).lower() != "off"
                ]
                has_original = (
                    self._original_wav_path(tname) is not None or tname in self._orig_cache
                )
                voice_mode = self._voice_mode(tname)
                voice_count = (
                    len(self._voice_cfgs(tname)) if voice_mode else 0
                )
                has_chip_ref = self._chip_ref_wav_path(tname) is not None
                bgm_id = int(tr.get("bgm_id", 0))
                items.append(
                    {
                        "name": tname,
                        "bgm_id": bgm_id,
                        "is_default": tname == "default" or bgm_id < 0,
                        "soundfont": str(tr.get("soundfont", "")),
                        "sample_rate": int(tr.get("sample_rate", 48000)),
                        "loop_start": int(tr.get("loop_start", 0)),
                        "loop_end": int(tr.get("loop_end", 0)),
                        "master_gain_db": float(tr.get("master_gain_db", 0.0)),
                        "target_rms_dbfs": float(tr.get("target_rms_dbfs", -16.0)),
                        "chip_blend": float(tr.get("chip_blend", 0.65)),
                        "active_channel_count": len(active_chs),
                        "has_midi": has_midi,
                        "midi_url": f"/api/tracks/{tname}/midi" if has_midi else None,
                        "has_preview_stems": has_midi,
                        "has_midi_stems": has_midi,
                        "synth_engine": SYNTH_ENGINE_LABEL,
                        "active_midi_channels": active_midi_chs,
                        "total_notes": total_notes,
                        "duration_s": round(dur_s, 4),
                        "has_chip_ref": has_chip_ref,
                        "has_original_wav": has_original,
                        "voice_mode": voice_mode,
                        "voice_count": voice_count,
                    }
                )
        items.sort(
            key=lambda x: (
                0 if (x["name"] == "default" or x["bgm_id"] < 0) else 1,
                x["bgm_id"],
                x["name"],
            )
        )
        return {
            "tracks": items,
            "base_dir": str(self.base_dir),
            "synth_engine": SYNTH_ENGINE_LABEL,
        }

    def _build_stems_info(
        self,
        track_name: str,
        profile: dict[str, Any],
        version_map: dict[str, int] | None = None,
    ) -> tuple[dict[str, Any], list[str], float, bool]:
        chs_cfg = profile.get("channels", {})
        if self._voice_mode(track_name):
            ordered = list(chs_cfg)
        else:
            ordered = list(CHANNEL_ORDER) + [c for c in chs_cfg if c not in CHANNEL_ORDER]
        sample_rate = int(profile.get("track", {}).get("sample_rate", 48000))

        midi_info = self._midi_for(track_name)
        has_track_midi = midi_info is not None
        if midi_info is not None:
            midi_mtime_ns, notes_by_ch, track_duration_s = midi_info
        else:
            midi_mtime_ns, notes_by_ch, track_duration_s = 0, {}, 0.0

        stems: dict[str, Any] = {}
        for ch in ordered:
            notes = notes_by_ch.get(ch, [])
            note_count = len(notes)
            ch_has_midi = has_track_midi and note_count > 0
            mode = str(chs_cfg.get(ch, {}).get("mode", "off")).lower()
            can_synth = ch_has_midi and mode in SYNTH_MODES
            ver = (version_map or {}).get(ch, midi_mtime_ns)
            wav_url = (
                f"/api/tracks/{track_name}/stems/{ch}.wav?v={ver}"
                if can_synth
                else None
            )
            stems[ch] = {
                "channel": ch,
                "has_midi": ch_has_midi,
                "has_wav": can_synth,
                "note_count": note_count,
                "mode": mode,
                "wav_url": wav_url,
                "audio_url": wav_url,
                "duration_s": round(track_duration_s, 4) if can_synth else 0.0,
                "sample_rate": sample_rate,
                "mtime_ns": ver,
            }
        return stems, [], (round(track_duration_s, 4) if has_track_midi else 0.0), has_track_midi

    def get_track(self, track_name: str) -> dict[str, Any]:
        p = self._require_profile_path(track_name)
        with self._track_lock(track_name):
            self._clear_preview_overrides(track_name)
            profile = load_profile(p)
        voice_mode = self._voice_mode(track_name)
        if voice_mode:
            profile = self._voice_profile(track_name, profile)
        stems, missing_wav, max_dur, has_midi = self._build_stems_info(track_name, profile)

        orig_p = self._original_wav_path(track_name)
        if orig_p is None and track_name in self._orig_cache:
            original_info = {
                "available": True,
                "url": f"/api/tracks/{track_name}/original.wav?v={len(self._orig_cache[track_name])}",
                "path": None,
                "source": "headless-ram",
                "duration_s": ORIGINAL_FRAMES / NTSC_FPS,
                "can_generate": False,
            }
        elif orig_p is not None:
            ometa = _probe_wav_header(orig_p)
            original_info = {
                "available": True,
                "url": f"/api/tracks/{track_name}/original.wav?v={ometa['mtime_ns']}",
                "path": str(orig_p),
                "duration_s": ometa["duration_s"],
                "sample_rate": ometa["sample_rate"],
            }
        else:
            original_info = {
                "available": False,
                "url": None,
                "path": None,
                "duration_s": 0.0,
                "can_generate": self.can_generate_original()
                and 0 <= int(profile.get("track", {}).get("bgm_id", -1)) <= 16,
            }

        chip_ref_p = self._chip_ref_wav_path(track_name)
        has_chip_ref = chip_ref_p is not None
        chip_ref_url = (
            f"/api/tracks/{track_name}/chip-ref.wav" if has_chip_ref else None
        )

        if voice_mode:
            ordered_chs = list(profile.get("channels", {}))
        else:
            ordered_chs = list(CHANNEL_ORDER) + [
                c for c in profile.get("channels", {}) if c not in CHANNEL_ORDER
            ]
        return {
            "voice_mode": voice_mode,
            "track_name": track_name,
            "profile_path": str(p),
            "profile": profile,
            "channel_order": ordered_chs,
            "stems": stems,
            "has_midi": has_midi,
            "midi_url": f"/api/tracks/{track_name}/midi" if has_midi else None,
            "has_preview_stems": has_midi,
            "has_midi_stems": has_midi,
            "has_any_wav": any(s["has_wav"] for s in stems.values()),
            "synth_engine": SYNTH_ENGINE_LABEL,
            "missing_wav_channels": missing_wav,
            "duration_s": max_dur,
            "original": original_info,
            "has_chip_ref": has_chip_ref,
            "chip_ref_url": chip_ref_url,
        }

    def _merge_and_validate_profile(
        self,
        existing: dict[str, Any],
        incoming: dict[str, Any],
        track_name: str,
    ) -> dict[str, Any]:
        if not isinstance(incoming, dict):
            raise ApiError(400, "Payload de perfil deve ser um objeto JSON.", "invalid_payload")

        if "profile" in incoming and isinstance(incoming["profile"], dict):
            src = incoming["profile"]
        else:
            src = incoming

        ex_track = dict(existing.get("track", {}))
        in_track = src.get("track")
        if in_track is not None:
            if not isinstance(in_track, dict):
                raise ApiError(400, "Campo 'track' deve ser um objeto.", "invalid_field")
            if "bgm_id" in in_track:
                ex_track["bgm_id"] = _clamp_int(in_track["bgm_id"], -1, 99999, "track.bgm_id")
            if "soundfont" in in_track:
                sf = str(in_track["soundfont"]).strip()
                if not sf or "\x00" in sf:
                    raise ApiError(400, "Campo 'track.soundfont' inválido.", "invalid_field")
                ex_track["soundfont"] = sf
            if "sample_rate" in in_track:
                ex_track["sample_rate"] = _clamp_int(
                    in_track["sample_rate"], 8000, 192000, "track.sample_rate"
                )
            if "loop_start" in in_track:
                ex_track["loop_start"] = _clamp_int(
                    in_track["loop_start"], 0, 2_000_000_000, "track.loop_start"
                )
            if "loop_end" in in_track:
                ex_track["loop_end"] = _clamp_int(
                    in_track["loop_end"], 0, 2_000_000_000, "track.loop_end"
                )
            if "master_gain_db" in in_track:
                ex_track["master_gain_db"] = round(
                    _clamp_float(in_track["master_gain_db"], -24.0, 18.0, "track.master_gain_db"),
                    3,
                )
            if "target_rms_dbfs" in in_track:
                ex_track["target_rms_dbfs"] = round(
                    _clamp_float(in_track["target_rms_dbfs"], -36.0, -3.0, "track.target_rms_dbfs"),
                    3,
                )
            if "chip_blend" in in_track:
                ex_track["chip_blend"] = round(
                    _clamp_float(in_track["chip_blend"], 0.0, 1.0, "track.chip_blend"),
                    4,
                )
        ex_track["name"] = track_name

        ex_channels: dict[str, dict[str, Any]] = {
            k: dict(v) for k, v in existing.get("channels", {}).items() if isinstance(v, dict)
        }
        in_channels = src.get("channels")
        if in_channels is not None:
            if not isinstance(in_channels, dict):
                raise ApiError(400, "Campo 'channels' deve ser um objeto.", "invalid_field")
            for raw_ch, ch_patch in in_channels.items():
                if _VOICE_KEY_RE.match(str(raw_ch)):
                    continue  # voz FM: vive em voices.toml (ver _extract_voice_patches)
                ch = validate_channel_name(raw_ch)
                if not isinstance(ch_patch, dict):
                    raise ApiError(
                        400, f"Canal '{ch}' deve ser um objeto JSON.", "invalid_field"
                    )
                cur = dict(ex_channels.get(ch, {}))
                if "role" in ch_patch:
                    cur["role"] = str(ch_patch["role"])[:48]
                if "mode" in ch_patch:
                    mode = str(ch_patch["mode"]).strip().lower()
                    if mode not in VALID_MODES:
                        raise ApiError(
                            400,
                            f"Modo inválido em '{ch}': {mode!r} (esperado: {sorted(VALID_MODES)})",
                            "invalid_mode",
                        )
                    cur["mode"] = mode
                if "program" in ch_patch:
                    cur["program"] = _clamp_int(ch_patch["program"], 0, 127, f"{ch}.program")
                if "bank" in ch_patch:
                    cur["bank"] = _clamp_int(ch_patch["bank"], 0, 128, f"{ch}.bank")
                else:
                    eff_mode = str(cur.get("mode", "off")).lower()
                    if eff_mode == "percussion":
                        cur["bank"] = 128
                    elif eff_mode in ("auto", "gm", "chip", "off") and cur.get("bank") == 128:
                        cur["bank"] = 0
                if "volume_db" in ch_patch:
                    cur["volume_db"] = round(
                        _clamp_float(ch_patch["volume_db"], -60.0, 24.0, f"{ch}.volume_db"),
                        3,
                    )
                if "pan" in ch_patch:
                    cur["pan"] = round(
                        _clamp_float(ch_patch["pan"], -1.0, 1.0, f"{ch}.pan"),
                        4,
                    )
                if "mute" in ch_patch:
                    cur["mute"] = _parse_bool(ch_patch["mute"], f"{ch}.mute")
                if "solo" in ch_patch:
                    cur["solo"] = _parse_bool(ch_patch["solo"], f"{ch}.solo")
                if "eq_low_db" in ch_patch:
                    cur["eq_low_db"] = round(
                        _clamp_float(ch_patch["eq_low_db"], -24.0, 24.0, f"{ch}.eq_low_db"),
                        3,
                    )
                if "eq_mid_db" in ch_patch:
                    cur["eq_mid_db"] = round(
                        _clamp_float(ch_patch["eq_mid_db"], -24.0, 24.0, f"{ch}.eq_mid_db"),
                        3,
                    )
                if "eq_high_db" in ch_patch:
                    cur["eq_high_db"] = round(
                        _clamp_float(ch_patch["eq_high_db"], -24.0, 24.0, f"{ch}.eq_high_db"),
                        3,
                    )
                if "transpose" in ch_patch:
                    cur["transpose"] = _clamp_int(ch_patch["transpose"], -24, 24, f"{ch}.transpose")
                ex_channels[ch] = cur

        return {"track": ex_track, "channels": ex_channels}

    @staticmethod
    def _extract_voice_patches(payload: Any) -> dict[str, dict[str, Any]]:
        """Entradas `v_<hash>` de payload[.profile].channels (edições de voz FM)."""
        if not isinstance(payload, dict):
            return {}
        src = payload.get("profile") if isinstance(payload.get("profile"), dict) else payload
        chans = src.get("channels") if isinstance(src, dict) else None
        if not isinstance(chans, dict):
            return {}
        return {
            k: v for k, v in chans.items()
            if _VOICE_KEY_RE.match(str(k)) and isinstance(v, dict)
        }

    def save_track_profile(self, track_name: str, payload: dict[str, Any]) -> dict[str, Any]:
        p = self._require_profile_path(track_name)
        with self._track_lock(track_name):
            existing = load_profile(p)
            merged = self._merge_and_validate_profile(existing, payload, track_name)
            voice_patches = self._extract_voice_patches(payload)
            if voice_patches and not self._voice_mode(track_name):
                raise ApiError(400, "Esta música não está em modo por instrumento.", "not_voice_mode")
            save_profile(p, merged)
            self._save_voice_edits(voice_patches)
            self._clear_preview_overrides(track_name)
            self._invalidate_wav_cache(track_name)
            out_profile = (
                self._voice_profile(track_name, merged) if self._voice_mode(track_name) else merged
            )
        return {
            "ok": True,
            "track": track_name,
            "profile_path": str(p),
            "voices_path": str(self.voices_path) if voice_patches else None,
            "voices_saved": sorted(voice_patches),
            "profile": out_profile,
        }

    def _render_voice_channel(
        self, track_name: str, channel: str, existing: dict[str, Any], payload: dict[str, Any]
    ) -> dict[str, Any]:
        vcfg = self._voice_cfgs(track_name).get(channel)
        if vcfg is None:
            raise ApiError(404, f"Voz desconhecida nesta música: {channel}", "channel_not_found")
        vpatch = {
            k: payload[k]
            for k in ("mode", "program", "bank", "transpose", "name", "drum_note")
            if k in payload
        }
        ch_cfg = self._apply_voice_patch(dict(vcfg), vpatch)
        tr_cfg = dict(existing.get("track", {}))
        if "soundfont" in payload:
            tr_cfg["soundfont"] = str(payload["soundfont"])
        if bool(payload.get("save_profile", False)):
            self._save_voice_edits({channel: {**vpatch, "bank": ch_cfg["bank"]}})
        else:
            with self._wav_cache_lock:
                self._preview_overrides[(track_name, channel)] = {
                    "ch": {
                        k: ch_cfg[k]
                        for k in ("mode", "program", "bank", "transpose", "drum_note")
                    },
                    "soundfont": tr_cfg.get("soundfont"),
                    "sample_rate": tr_cfg.get("sample_rate"),
                }
        self._invalidate_wav_cache(track_name, channel)
        _wav, meta, elapsed_ms = self._synthesize_channel_wav(
            track_name=track_name, channel=channel, ch_cfg=ch_cfg, tr_cfg=tr_cfg
        )
        audio_url = f"/api/tracks/{track_name}/stems/{channel}.wav?v={meta['version_ts']}"
        return {
            "ok": True,
            "track": track_name,
            "channel": channel,
            "mode": meta["mode"],
            "program": int(ch_cfg["program"]),
            "bank": int(ch_cfg["bank"]),
            "transpose": int(ch_cfg["transpose"]),
            "backend": meta["backend"],
            "synth_engine": SYNTH_ENGINE_LABEL,
            "note_count": meta["note_count"],
            "duration_s": meta["duration_s"],
            "sample_rate": meta["sample_rate"],
            "elapsed_ms": elapsed_ms,
            "audio_url": audio_url,
            "wav_url": audio_url,
            "mtime_ns": meta["version_ts"],
        }

    def render_channel(self, track_name: str, payload: dict[str, Any]) -> dict[str, Any]:
        p = self._require_profile_path(track_name)
        if not isinstance(payload, dict):
            raise ApiError(400, "Payload JSON inválido.", "invalid_payload")
        channel = validate_channel_name(str(payload.get("channel", "")))

        with self._track_lock(track_name):
            existing = load_profile(p)
            if _VOICE_KEY_RE.match(channel):
                return self._render_voice_channel(track_name, channel, existing, payload)
            if channel not in existing.get("channels", {}) and channel not in CHANNEL_ORDER:
                raise ApiError(404, f"Canal desconhecido: {channel}", "channel_not_found")

            patch_ch: dict[str, Any] = {}
            for key in ("mode", "program", "bank", "transpose", "role"):
                if key in payload:
                    patch_ch[key] = payload[key]
            if "mode" in patch_ch and "bank" not in patch_ch:
                m = str(patch_ch["mode"]).strip().lower()
                patch_ch["bank"] = 128 if m == "percussion" else 0

            patch: dict[str, Any] = {"channels": {channel: patch_ch}}
            if "soundfont" in payload:
                patch["track"] = {"soundfont": payload["soundfont"]}
            if "profile" in payload and isinstance(payload["profile"], dict):
                merged = self._merge_and_validate_profile(existing, payload["profile"], track_name)
                merged = self._merge_and_validate_profile(merged, patch, track_name)
            else:
                merged = self._merge_and_validate_profile(existing, patch, track_name)

            ch_cfg = dict(merged["channels"].get(channel, {}))
            tr_cfg = dict(merged.get("track", {}))

            if bool(payload.get("save_profile", False)):
                save_profile(p, merged)
                self._clear_preview_overrides(track_name, channel)
            else:
                with self._wav_cache_lock:
                    self._preview_overrides[(track_name, channel)] = {
                        "ch": {
                            k: ch_cfg[k]
                            for k in ("mode", "program", "bank", "transpose")
                            if k in ch_cfg
                        },
                        "soundfont": tr_cfg.get("soundfont"),
                        "sample_rate": tr_cfg.get("sample_rate"),
                    }

            self._invalidate_wav_cache(track_name, channel)
            _wav_bytes, meta, elapsed_ms = self._synthesize_channel_wav(
                track_name=track_name,
                channel=channel,
                ch_cfg=ch_cfg,
                tr_cfg=tr_cfg,
            )

            audio_url = f"/api/tracks/{track_name}/stems/{channel}.wav?v={meta['version_ts']}"
            return {
                "ok": True,
                "track": track_name,
                "channel": channel,
                "mode": meta["mode"],
                "program": int(ch_cfg.get("program", 0)),
                "bank": int(ch_cfg.get("bank", 0)),
                "transpose": int(ch_cfg.get("transpose", 0)),
                "backend": meta["backend"],
                "synth_engine": SYNTH_ENGINE_LABEL,
                "note_count": meta["note_count"],
                "duration_s": meta["duration_s"],
                "sample_rate": meta["sample_rate"],
                "elapsed_ms": elapsed_ms,
                "audio_url": audio_url,
                "wav_url": audio_url,
                "mtime_ns": meta["version_ts"],
            }

    def render_all_stems(self, track_name: str, payload: dict[str, Any]) -> dict[str, Any]:
        p = self._require_profile_path(track_name)
        with self._track_lock(track_name):
            existing = load_profile(p)
            if isinstance(payload, dict) and (
                "profile" in payload or "channels" in payload or "track" in payload
            ):
                merged = self._merge_and_validate_profile(existing, payload, track_name)
            else:
                merged = existing
            voice_mode = self._voice_mode(track_name)
            voice_patches = self._extract_voice_patches(payload) if voice_mode else {}
            merged_raw = merged
            if voice_mode:
                merged = self._voice_profile(track_name, merged_raw, voice_patches)

            tr_cfg = dict(merged.get("track", {}))
            channels_cfg = merged.get("channels", {})
            save_it = bool(payload.get("save_profile", False)) if isinstance(payload, dict) else False
            if save_it:
                save_profile(p, merged_raw)
                self._save_voice_edits(voice_patches)
                self._clear_preview_overrides(track_name)
            else:
                with self._wav_cache_lock:
                    for ch, ch_cfg in channels_cfg.items():
                        self._preview_overrides[(track_name, ch)] = {
                            "ch": {
                                k: ch_cfg[k]
                                for k in ("mode", "program", "bank", "transpose")
                                if k in ch_cfg
                            },
                            "soundfont": tr_cfg.get("soundfont"),
                            "sample_rate": tr_cfg.get("sample_rate"),
                        }

            self._invalidate_wav_cache(track_name)
            t0 = time.perf_counter()
            rendered: list[dict[str, Any]] = []
            backends_used: list[str] = []
            version_map: dict[str, int] = {}

            for ch in (list(channels_cfg) if voice_mode else CHANNEL_ORDER):
                if ch not in channels_cfg:
                    continue
                ch_cfg = channels_cfg[ch]
                _wav_bytes, meta, _ch_ms = self._synthesize_channel_wav(
                    track_name=track_name,
                    channel=ch,
                    ch_cfg=ch_cfg,
                    tr_cfg=tr_cfg,
                )
                version_map[ch] = meta["version_ts"]
                if meta["backend"] != "silent" and meta["backend"] not in backends_used:
                    backends_used.append(meta["backend"])
                rendered.append(
                    {
                        "channel": ch,
                        "mode": meta["mode"],
                        "backend": meta["backend"],
                        "note_count": meta["note_count"],
                        "duration_s": meta["duration_s"],
                    }
                )

            elapsed_ms = round((time.perf_counter() - t0) * 1000.0, 2)
            stems, missing_wav, max_dur, has_midi = self._build_stems_info(
                track_name, merged, version_map=version_map
            )

        return {
            "ok": True,
            "track": track_name,
            "elapsed_ms": elapsed_ms,
            "synth_engine": SYNTH_ENGINE_LABEL,
            "backends_used": backends_used or ["silent"],
            "rendered": rendered,
            "stems": stems,
            "has_midi": has_midi,
            "missing_wav_channels": missing_wav,
            "duration_s": max_dur,
        }


class MixerRequestHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    app: MixerApp

    def log_message(self, format: str, *args: Any) -> None:  # noqa: A002
        if getattr(self.server, "quiet", False):
            return
        sys.stderr.write(f"[mixer_studio] {self.address_string()} - {format % args}\n")

    def _send_bytes(
        self,
        status: int,
        data: bytes,
        content_type: str,
        extra_headers: dict[str, str] | None = None,
        head_only: bool = False,
        support_range: bool = False,
    ) -> None:
        if support_range and status == 200:
            range_hdr = self.headers.get("Range")
            if range_hdr and range_hdr.startswith("bytes="):
                total_size = len(data)
                spec = range_hdr[6:].split(",", 1)[0].strip()
                m = re.fullmatch(r"(\d*)-(\d*)", spec)
                if not m or (m.group(1) == "" and m.group(2) == ""):
                    self._send_bytes(
                        416,
                        b"",
                        content_type,
                        {"Content-Range": f"bytes */{total_size}", "Accept-Ranges": "bytes"},
                        head_only=head_only,
                    )
                    return
                if m.group(1) == "":
                    suffix = int(m.group(2))
                    start = max(0, total_size - suffix)
                    end = total_size - 1
                else:
                    start = int(m.group(1))
                    end = int(m.group(2)) if m.group(2) else total_size - 1
                if start >= total_size or start > end:
                    self._send_bytes(
                        416,
                        b"",
                        content_type,
                        {"Content-Range": f"bytes */{total_size}", "Accept-Ranges": "bytes"},
                        head_only=head_only,
                    )
                    return
                end = min(end, total_size - 1)
                slice_data = data[start : end + 1]
                hdrs = {
                    "Accept-Ranges": "bytes",
                    "Content-Range": f"bytes {start}-{end}/{total_size}",
                }
                if extra_headers:
                    hdrs.update(extra_headers)
                self._send_bytes(206, slice_data, content_type, hdrs, head_only=head_only)
                return

        try:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            if support_range and (not extra_headers or "Accept-Ranges" not in extra_headers):
                self.send_header("Accept-Ranges", "bytes")
            if extra_headers:
                for k, v in extra_headers.items():
                    self.send_header(k, v)
            self.end_headers()
            if not head_only and data:
                self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def _send_json(self, status: int, obj: Any) -> None:
        raw = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self._send_bytes(status, raw, "application/json; charset=utf-8")

    def _send_file(self, path: Path, content_type: str, head_only: bool = False) -> None:
        if not path.is_file():
            self._send_json(404, {"error": "Arquivo não encontrado.", "code": "not_found"})
            return
        file_size = path.stat().st_size
        range_hdr = self.headers.get("Range")
        if range_hdr and range_hdr.startswith("bytes="):
            spec = range_hdr[6:].split(",", 1)[0].strip()
            m = re.fullmatch(r"(\d*)-(\d*)", spec)
            if not m or (m.group(1) == "" and m.group(2) == ""):
                self._send_bytes(
                    416,
                    b"",
                    content_type,
                    {"Content-Range": f"bytes */{file_size}"},
                    head_only=head_only,
                )
                return
            if m.group(1) == "":
                suffix = int(m.group(2))
                start = max(0, file_size - suffix)
                end = file_size - 1
            else:
                start = int(m.group(1))
                end = int(m.group(2)) if m.group(2) else file_size - 1
            if start >= file_size or start > end:
                self._send_bytes(
                    416,
                    b"",
                    content_type,
                    {"Content-Range": f"bytes */{file_size}"},
                    head_only=head_only,
                )
                return
            end = min(end, file_size - 1)
            length = end - start + 1
            try:
                self.send_response(206)
                self.send_header("Content-Type", content_type)
                self.send_header("Accept-Ranges", "bytes")
                self.send_header("Content-Range", f"bytes {start}-{end}/{file_size}")
                self.send_header("Content-Length", str(length))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                if not head_only:
                    with open(path, "rb") as f:
                        f.seek(start)
                        remaining = length
                        while remaining > 0:
                            chunk = f.read(min(65536, remaining))
                            if not chunk:
                                break
                            self.wfile.write(chunk)
                            remaining -= len(chunk)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                pass
            return

        try:
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Content-Length", str(file_size))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            if not head_only:
                with open(path, "rb") as f:
                    while True:
                        chunk = f.read(65536)
                        if not chunk:
                            break
                        self.wfile.write(chunk)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def _read_json_body(self) -> dict[str, Any]:
        raw_len = self.headers.get("Content-Length")
        if raw_len is None:
            return {}
        try:
            length = int(raw_len)
        except ValueError as exc:
            raise ApiError(400, "Content-Length inválido.", "invalid_header") from exc
        if length < 0 or length > 2 * 1024 * 1024:
            raise ApiError(413, "Corpo JSON excede o limite permitido.", "payload_too_large")
        if length == 0:
            return {}
        body = self.rfile.read(length)
        try:
            parsed = json.loads(body.decode("utf-8"))
        except Exception as exc:
            raise ApiError(400, f"JSON inválido: {exc}", "invalid_json") from exc
        if not isinstance(parsed, dict):
            raise ApiError(400, "Corpo JSON deve ser um objeto.", "invalid_json")
        return parsed

    def _raw_path_segments(self) -> tuple[str, list[str]]:
        raw_path = urlsplit(self.path).path
        lower = raw_path.lower()
        if (
            ".." in raw_path
            or "%2e" in lower
            or "%5c" in lower
            or "\\" in raw_path
            or "%00" in lower
            or "\x00" in raw_path
        ):
            raise ApiError(400, "Caminho inválido (path traversal bloqueado).", "path_traversal")
        raw_parts = [seg for seg in raw_path.split("/") if seg != ""]
        parts: list[str] = []
        for seg in raw_parts:
            decoded = unquote(seg)
            if "/" in decoded or "\\" in decoded or ".." in decoded or "\x00" in decoded:
                raise ApiError(400, "Segmento de caminho inválido.", "path_traversal")
            parts.append(decoded)
        return raw_path, parts

    def _dispatch_get(self, head_only: bool = False) -> None:
        try:
            raw_path, parts = self._raw_path_segments()

            if raw_path in _STATIC_FILES:
                fname, ctype = _STATIC_FILES[raw_path]
                file_path = _ensure_within(_UI_DIR / fname, _UI_DIR)
                if not file_path.is_file():
                    raise ApiError(500, f"Arquivo de UI ausente: {fname}", "ui_missing")
                self._send_file(file_path, ctype, head_only=head_only)
                return

            if parts == ["api", "tracks"]:
                self._send_json(200, self.app.list_tracks())
                return

            if parts == ["api", "voices"]:
                self._send_json(200, self.app.list_voices())
                return

            if len(parts) == 3 and parts[0] == "api" and parts[1] == "tracks":
                track = validate_track_name(parts[2])
                self._send_json(200, self.app.get_track(track))
                return

            if (
                len(parts) == 4
                and parts[0] == "api"
                and parts[1] == "tracks"
                and parts[3] == "voices"
            ):
                track = validate_track_name(parts[2])
                self.app._require_profile_path(track)
                self._send_json(200, self.app.list_voices(track))
                return

            if (
                len(parts) == 4
                and parts[0] == "api"
                and parts[1] == "tracks"
                and parts[3] == "midi"
            ):
                track = validate_track_name(parts[2])
                midi_bytes = self.app.get_midi_bytes(track)
                self._send_bytes(
                    200,
                    midi_bytes,
                    "audio/midi",
                    {"Content-Disposition": f'attachment; filename="{track}.mid"'},
                    head_only=head_only,
                    support_range=True,
                )
                return

            if (
                len(parts) == 5
                and parts[0] == "api"
                and parts[1] == "tracks"
                and parts[3] == "stems"
            ):
                track = validate_track_name(parts[2])
                ch_seg = parts[4]
                if ch_seg.endswith(".wav"):
                    ch_seg = ch_seg[:-4]
                channel = validate_channel_name(ch_seg)
                wav_bytes = self.app.get_stem_wav_bytes(track, channel)
                self._send_bytes(
                    200,
                    wav_bytes,
                    "audio/wav",
                    head_only=head_only,
                    support_range=True,
                )
                return

            if (
                len(parts) == 4
                and parts[0] == "api"
                and parts[1] == "tracks"
                and parts[3] == "original.wav"
            ):
                track = validate_track_name(parts[2])
                orig = self.app._original_wav_path(track)
                if orig is None and track in self.app._orig_cache:
                    self._send_bytes(
                        200, self.app._orig_cache[track], "audio/wav",
                        head_only=head_only, support_range=True,
                    )
                    return
                if orig is None:
                    raise ApiError(404, "Áudio original (A_original.wav) não encontrado.", "not_found")
                self._send_file(orig, "audio/wav", head_only=head_only)
                return

            if (
                len(parts) == 4
                and parts[0] == "api"
                and parts[1] == "tracks"
                and parts[3] == "chip-ref.wav"
            ):
                track = validate_track_name(parts[2])
                chip_ref = self.app._chip_ref_wav_path(track)
                if chip_ref is None:
                    raise ApiError(
                        404,
                        "Áudio de referência do chip não encontrado.",
                        "not_found",
                    )
                self._send_file(chip_ref, "audio/wav", head_only=head_only)
                return

            raise ApiError(404, f"Rota não encontrada: {raw_path}", "not_found")
        except ApiError as err:
            self._send_json(err.status, {"error": err.message, "code": err.code})
        except Exception as exc:
            self._send_json(
                HTTPStatus.INTERNAL_SERVER_ERROR,
                {"error": str(exc), "code": "internal_error"},
            )

    def do_HEAD(self) -> None:  # noqa: N802
        self._dispatch_get(head_only=True)

    def do_GET(self) -> None:  # noqa: N802
        self._dispatch_get(head_only=False)

    def do_POST(self) -> None:  # noqa: N802
        try:
            raw_path, parts = self._raw_path_segments()
            if len(parts) == 4 and parts[0] == "api" and parts[1] == "tracks":
                track = validate_track_name(parts[2])
                action = parts[3]
                payload = self._read_json_body()

                if action == "save":
                    self._send_json(200, self.app.save_track_profile(track, payload))
                    return
                if action == "render-channel":
                    self._send_json(200, self.app.render_channel(track, payload))
                    return
                if action == "render-all":
                    self._send_json(200, self.app.render_all_stems(track, payload))
                    return
                if action == "generate-original":
                    self._send_json(200, self.app.generate_original(track))
                    return

            raise ApiError(404, f"Endpoint POST não encontrado: {raw_path}", "not_found")
        except ApiError as err:
            self._send_json(err.status, {"error": err.message, "code": err.code})
        except Exception as exc:
            self._send_json(
                HTTPStatus.INTERNAL_SERVER_ERROR,
                {"error": str(exc), "code": "internal_error"},
            )


def create_server(
    port: int = 8765,
    base_dir: str | Path | None = None,
    prototypes_dir: str | Path | None = None,
    backend_pref: str = "tsf",
    quiet: bool = False,
) -> ThreadingHTTPServer:
    resolved_base = Path(base_dir).resolve() if base_dir else _default_dir("assets/audio_remaster")
    resolved_proto = (
        Path(prototypes_dir).resolve()
        if prototypes_dir
        else _default_dir("assets/audio_prototypes")
    )
    app = MixerApp(
        base_dir=resolved_base,
        prototypes_dir=resolved_proto,
        backend_pref=backend_pref,
    )

    class _BoundHandler(MixerRequestHandler):
        pass

    _BoundHandler.app = app
    ThreadingHTTPServer.allow_reuse_address = True
    server = ThreadingHTTPServer(("127.0.0.1", int(port)), _BoundHandler)
    server.daemon_threads = True
    server.quiet = quiet  # type: ignore[attr-defined]
    orig_close = server.server_close

    def _close_with_app() -> None:
        try:
            app.close()
        finally:
            orig_close()

    server.server_close = _close_with_app  # type: ignore[method-assign]
    return server


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Eternal Champions — Mixer Studio Web local (100% MIDI + TinySoundFont RAM)."
    )
    ap.add_argument(
        "--port",
        type=int,
        default=8765,
        help="Porta TCP em 127.0.0.1 (default: 8765; use 0 para porta efêmera)",
    )
    ap.add_argument(
        "--no-browser",
        action="store_true",
        help="Não abre o navegador automaticamente",
    )
    ap.add_argument(
        "--base-dir",
        default=None,
        help="Diretório raiz de remasterização (default: assets/audio_remaster)",
    )
    ap.add_argument(
        "--prototypes-dir",
        default=None,
        help="Diretório de protótipos/A_original.wav (default: assets/audio_prototypes)",
    )
    ap.add_argument(
        "--backend",
        default="tsf",
        help="Motor de síntese (TinySoundFont nativo em RAM; mantido por compatibilidade)",
    )
    ap.add_argument(
        "--quiet",
        action="store_true",
        help="Suprime logs de requisição HTTP no stderr",
    )
    args = ap.parse_args(argv)

    server = create_server(
        port=args.port,
        base_dir=args.base_dir,
        prototypes_dir=args.prototypes_dir,
        backend_pref=args.backend,
        quiet=args.quiet,
    )
    actual_port = server.server_address[1]
    url = f"http://127.0.0.1:{actual_port}"
    print(f"[mixer_studio] Servidor pronto em {url}", flush=True)
    print(
        f"[mixer_studio] Motor: {SYNTH_ENGINE_LABEL} | Base: {server.RequestHandlerClass.app.base_dir} | Ctrl+C para sair",  # type: ignore[attr-defined]
        flush=True,
    )

    if not args.no_browser:
        threading.Timer(0.35, lambda: webbrowser.open(url)).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[mixer_studio] Encerrando servidor...", flush=True)
    finally:
        server.shutdown()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
