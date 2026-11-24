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
import sys
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

from chip_ring_to_midi import CHANNEL_ORDER, NoteEvent, read_midi_notes  # noqa: E402

VALID_MODES = frozenset({"auto", "gm", "percussion", "chip", "off"})
SYNTH_MODES = frozenset({"auto", "gm", "percussion"})
_MODE_TO_TSF_INT: dict[str, int] = {"gm": 0, "percussion": 1, "auto": 2}
DEFAULT_PORTABLE_SOUNDFONT = "assets/soundfonts/TimGM6mb.sf2"
SYNTH_ENGINE_LABEL = "TinySoundFont (tsf.h)"
DEFAULT_CACHE_CAPACITY = 64
_TRACK_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
_CHANNEL_RE = re.compile(r"^[a-z][a-z0-9_]{0,31}$")

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
        midi_info = self._load_track_midi(track_name)
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

        cache_key = (
            track_name,
            channel,
            mode,
            bank,
            program,
            transpose,
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
                has_original = self._original_wav_path(tname) is not None
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
        ordered = list(CHANNEL_ORDER) + [c for c in chs_cfg if c not in CHANNEL_ORDER]
        sample_rate = int(profile.get("track", {}).get("sample_rate", 48000))

        midi_info = self._load_track_midi(track_name)
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
        stems, missing_wav, max_dur, has_midi = self._build_stems_info(track_name, profile)

        orig_p = self._original_wav_path(track_name)
        if orig_p is not None:
            ometa = _probe_wav_header(orig_p)
            original_info = {
                "available": True,
                "url": f"/api/tracks/{track_name}/original.wav?v={ometa['mtime_ns']}",
                "path": str(orig_p),
                "duration_s": ometa["duration_s"],
                "sample_rate": ometa["sample_rate"],
            }
        else:
            original_info = {"available": False, "url": None, "path": None, "duration_s": 0.0}

        chip_ref_p = self._chip_ref_wav_path(track_name)
        has_chip_ref = chip_ref_p is not None
        chip_ref_url = (
            f"/api/tracks/{track_name}/chip-ref.wav" if has_chip_ref else None
        )

        ordered_chs = list(CHANNEL_ORDER) + [
            c for c in profile.get("channels", {}) if c not in CHANNEL_ORDER
        ]
        return {
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

    def save_track_profile(self, track_name: str, payload: dict[str, Any]) -> dict[str, Any]:
        p = self._require_profile_path(track_name)
        with self._track_lock(track_name):
            existing = load_profile(p)
            merged = self._merge_and_validate_profile(existing, payload, track_name)
            save_profile(p, merged)
            self._clear_preview_overrides(track_name)
            self._invalidate_wav_cache(track_name)
        return {
            "ok": True,
            "track": track_name,
            "profile_path": str(p),
            "profile": merged,
        }

    def render_channel(self, track_name: str, payload: dict[str, Any]) -> dict[str, Any]:
        p = self._require_profile_path(track_name)
        if not isinstance(payload, dict):
            raise ApiError(400, "Payload JSON inválido.", "invalid_payload")
        channel = validate_channel_name(str(payload.get("channel", "")))

        with self._track_lock(track_name):
            existing = load_profile(p)
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

            tr_cfg = dict(merged.get("track", {}))
            channels_cfg = merged.get("channels", {})
            save_it = bool(payload.get("save_profile", False)) if isinstance(payload, dict) else False
            if save_it:
                save_profile(p, merged)
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

            for ch in CHANNEL_ORDER:
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

            if len(parts) == 3 and parts[0] == "api" and parts[1] == "tracks":
                track = validate_track_name(parts[2])
                self._send_json(200, self.app.get_track(track))
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
