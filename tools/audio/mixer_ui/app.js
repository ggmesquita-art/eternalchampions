(() => {
  "use strict";

  // -------------------------------------------------------------------------
  // General MIDI Level 1 — 128 instrumentos agrupados por categoria oficial
  // -------------------------------------------------------------------------
  const GM_CATEGORIES = [
    { name: "Piano (0–7)", start: 0, items: [
      "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano",
      "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
    ]},
    { name: "Chromatic Percussion (8–15)", start: 8, items: [
      "Celesta", "Glockenspiel", "Music Box", "Vibraphone",
      "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
    ]},
    { name: "Organ (16–23)", start: 16, items: [
      "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
      "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
    ]},
    { name: "Guitar (24–31)", start: 24, items: [
      "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)", "Electric Guitar (jazz)", "Electric Guitar (clean)",
      "Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
    ]},
    { name: "Bass (32–39)", start: 32, items: [
      "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)", "Fretless Bass",
      "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
    ]},
    { name: "Strings (40–47)", start: 40, items: [
      "Violin", "Viola", "Cello", "Contrabass",
      "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
    ]},
    { name: "Ensemble (48–55)", start: 48, items: [
      "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2",
      "Choir Aahs", "Voice Oohs", "Synth Choir", "Orchestra Hit",
    ]},
    { name: "Brass (56–63)", start: 56, items: [
      "Trumpet", "Trombone", "Tuba", "Muted Trumpet",
      "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
    ]},
    { name: "Reed (64–71)", start: 64, items: [
      "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax",
      "Oboe", "English Horn", "Bassoon", "Clarinet",
    ]},
    { name: "Pipe (72–79)", start: 72, items: [
      "Piccolo", "Flute", "Recorder", "Pan Flute",
      "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
    ]},
    { name: "Synth Lead (80–87)", start: 80, items: [
      "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)",
      "Lead 5 (charang)", "Lead 6 (voice)", "Lead 7 (fifths)", "Lead 8 (bass + lead)",
    ]},
    { name: "Synth Pad (88–95)", start: 88, items: [
      "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)",
      "Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",
    ]},
    { name: "Synth Effects (96–103)", start: 96, items: [
      "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)",
      "FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
    ]},
    { name: "Ethnic (104–111)", start: 104, items: [
      "Sitar", "Banjo", "Shamisen", "Koto",
      "Kalimba", "Bagpipe", "Fiddle", "Shanai",
    ]},
    { name: "Percussive (112–119)", start: 112, items: [
      "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock",
      "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
    ]},
    { name: "Sound Effects (120–127)", start: 120, items: [
      "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
      "Telephone Ring", "Helicopter", "Applause", "Gunshot",
    ]},
  ];

  const DRUM_KITS = [
    { program: 0, label: "000 · Standard Kit" },
    { program: 8, label: "008 · Room Kit" },
    { program: 16, label: "016 · Power Kit" },
    { program: 24, label: "024 · Electronic Kit" },
    { program: 25, label: "025 · TR-808 Kit" },
    { program: 32, label: "032 · Jazz Kit" },
    { program: 40, label: "040 · Brush Kit" },
    { program: 48, label: "048 · Orchestra Kit" },
  ];

  const AUTO_MODE_OPT = { value: "auto", label: "Auto (dinâmico em tempo real)" };

  const MODES = [
    { value: "gm", label: "GM (Instrumento MIDI)" },
    { value: "percussion", label: "Percussion (Bateria)" },
    { value: "chip", label: "Chip (Original Sintetizado)" },
    { value: "off", label: "Off (Desligado)" },
  ];

  // -------------------------------------------------------------------------
  // Estado global da aplicação
  // -------------------------------------------------------------------------
  const state = {
    tracks: [],
    currentTrack: null,
    trackData: null,
    profile: null,
    unsaved: false,
    abMode: "remaster", // "remaster" | "original"
    loopEnabled: true,
    dirtyRenderChannels: new Set(),
    renderingChannels: new Set(),
    pendingRerender: new Set(),
    isBusyGlobal: false,
    // Web Audio
    audioCtx: null,
    masterGainNode: null,
    limiterNode: null,
    analyserNode: null,
    abRemasterGain: null,
    abOriginalGain: null,
    sf2BusGain: null,
    chipRefGain: null,
    channelNodes: {},     // ch -> { low, mid, high, panner, gain }
    stemBuffers: {},      // ch -> AudioBuffer
    chipRefBuffer: null,  // AudioBuffer (_chip_ref.wav / B_studio_dsp.wav)
    originalBuffer: null, // AudioBuffer
    activeSources: {},    // ch -> AudioBufferSourceNode
    chipRefSource: null,
    originalSource: null,
    isPlaying: false,
    playbackStartCtxTime: 0,
    playbackOffset: 0,
    duration: 0,
    rafId: null,
    loadToken: 0,
  };

  // -------------------------------------------------------------------------
  // Referências DOM
  // -------------------------------------------------------------------------
  const dom = {
    trackSelect: document.getElementById("trackSelect"),
    btnReloadTracks: document.getElementById("btnReloadTracks"),
    btnRenderAll: document.getElementById("btnRenderAll"),
    btnDownloadMidi: document.getElementById("btnDownloadMidi"),
    btnSave: document.getElementById("btnSave"),
    unsavedDot: document.getElementById("unsavedDot"),
    metaBgmId: document.getElementById("metaBgmId"),
    metaSoundfont: document.getElementById("metaSoundfont"),
    metaSampleRate: document.getElementById("metaSampleRate"),
    metaLoop: document.getElementById("metaLoop"),
    metaStems: document.getElementById("metaStems"),
    metaEngine: document.getElementById("metaEngine"),
    masterGainSlider: document.getElementById("masterGainSlider"),
    masterGainVal: document.getElementById("masterGainVal"),
    chipBlendSlider: document.getElementById("chipBlendSlider"),
    chipBlendVal: document.getElementById("chipBlendVal"),
    chipPresetBtns: Array.from(document.querySelectorAll(".chip-preset-btn")),
    chipRefBadge: document.getElementById("chipRefBadge"),
    autoApplyCheck: document.getElementById("autoApplyCheck"),
    btnPlayPause: document.getElementById("btnPlayPause"),
    btnStop: document.getElementById("btnStop"),
    btnLoop: document.getElementById("btnLoop"),
    timeCurrent: document.getElementById("timeCurrent"),
    timeTotal: document.getElementById("timeTotal"),
    seekBar: document.getElementById("seekBar"),
    btnModeA: document.getElementById("btnModeA"),
    btnModeB: document.getElementById("btnModeB"),
    masterMeterFill: document.getElementById("masterMeterFill"),
    realtimeNoticeBanner: document.getElementById("realtimeNoticeBanner"),
    realtimeNoticeText: document.getElementById("realtimeNoticeText"),
    statusBanner: document.getElementById("statusBanner"),
    statusText: document.getElementById("statusText"),
    btnCloseStatus: document.getElementById("btnCloseStatus"),
    audioStateBadge: document.getElementById("audioStateBadge"),
    soloStatusBadge: document.getElementById("soloStatusBadge"),
    btnClearSolo: document.getElementById("btnClearSolo"),
    btnUnmuteAll: document.getElementById("btnUnmuteAll"),
    btnResetEq: document.getElementById("btnResetEq"),
    mixerGrid: document.getElementById("mixerGrid"),
    exportMetaFooter: document.getElementById("exportMetaFooter"),
    btnGenOriginal: document.getElementById("btnGenOriginal"),
  };

  // -------------------------------------------------------------------------
  // Utilitários
  // -------------------------------------------------------------------------
  function dbToLin(db) {
    return Math.pow(10, Number(db) / 20);
  }

  function fmtDb(v) {
    const n = Number(v) || 0;
    return `${n > 0 ? "+" : ""}${n.toFixed(1)} dB`;
  }

  function fmtPan(v) {
    const n = Number(v) || 0;
    if (Math.abs(n) < 0.02) return "C";
    return n < 0 ? `L${Math.round(Math.abs(n) * 100)}` : `R${Math.round(n * 100)}`;
  }

  function fmtTime(sec) {
    const s = Math.max(0, Number(sec) || 0);
    const m = Math.floor(s / 60);
    const rem = s - m * 60;
    return `${String(m).padStart(2, "0")}:${rem < 10 ? "0" : ""}${rem.toFixed(1)}`;
  }

  function showStatus(msg, kind = "info") {
    dom.statusBanner.className = `banner banner-${kind}`;
    dom.statusText.textContent = msg;
    dom.statusBanner.classList.remove("hidden");
  }

  function hideStatus() {
    dom.statusBanner.classList.add("hidden");
  }

  function setUnsaved(flag) {
    state.unsaved = Boolean(flag);
    dom.unsavedDot.classList.toggle("hidden", !state.unsaved);
  }

  async function apiJson(url, opts = {}) {
    const res = await fetch(url, {
      headers: { "Content-Type": "application/json", ...(opts.headers || {}) },
      ...opts,
    });
    const text = await res.text();
    let data = {};
    try {
      data = text ? JSON.parse(text) : {};
    } catch (_) {
      data = { error: text || `HTTP ${res.status}` };
    }
    if (!res.ok) {
      throw new Error(data.error || `Erro HTTP ${res.status}`);
    }
    return data;
  }

  function getLoopBounds() {
    const tr = state.profile ? state.profile.track : null;
    const dur = state.duration || 0;
    if (!tr || dur <= 0) return { start: 0, end: dur };
    const sr = Number(tr.sample_rate || 48000);
    const ls = Number(tr.loop_start || 0) / sr;
    const le = Number(tr.loop_end || 0) / sr;
    if (le > ls + 0.2 && le <= dur + 0.05) {
      return { start: Math.max(0, ls), end: Math.min(dur, le) };
    }
    return { start: 0, end: dur };
  }

  // -------------------------------------------------------------------------
  // Motor Web Audio API (Preview instantâneo 0 ms)
  // -------------------------------------------------------------------------
  function ensureAudioContext() {
    if (!state.audioCtx) {
      const Ctx = window.AudioContext || window.webkitAudioContext;
      const ctx = new Ctx();
      const sf2BusGain = ctx.createGain();
      const chipRefGain = ctx.createGain();
      const abRemasterGain = ctx.createGain();
      const abOriginalGain = ctx.createGain();
      const masterGain = ctx.createGain();
      const limiter = ctx.createDynamicsCompressor();
      limiter.threshold.value = -2.5;
      limiter.knee.value = 6.0;
      limiter.ratio.value = 12.0;
      limiter.attack.value = 0.003;
      limiter.release.value = 0.12;
      const analyser = ctx.createAnalyser();
      analyser.fftSize = 256;

      sf2BusGain.connect(masterGain);
      masterGain.connect(abRemasterGain);
      chipRefGain.connect(abRemasterGain);
      abRemasterGain.connect(limiter);
      limiter.connect(analyser);
      abOriginalGain.connect(analyser);
      analyser.connect(ctx.destination);

      state.audioCtx = ctx;
      state.sf2BusGain = sf2BusGain;
      state.chipRefGain = chipRefGain;
      state.abRemasterGain = abRemasterGain;
      state.abOriginalGain = abOriginalGain;
      state.masterGainNode = masterGain;
      state.limiterNode = limiter;
      state.analyserNode = analyser;
    }
    if (state.audioCtx.state === "suspended") {
      state.audioCtx.resume();
    }
    applyChipBlendGains(true);
    applyABGains(true);
    applyMasterGain(true);
    return state.audioCtx;
  }

  function ensureChannelNodes(ch) {
    const ctx = ensureAudioContext();
    if (state.channelNodes[ch]) return state.channelNodes[ch];

    const low = ctx.createBiquadFilter();
    low.type = "lowshelf";
    low.frequency.value = 180;

    const mid = ctx.createBiquadFilter();
    mid.type = "peaking";
    mid.frequency.value = 1200;
    mid.Q.value = 1.0;

    const high = ctx.createBiquadFilter();
    high.type = "highshelf";
    high.frequency.value = 5000;

    const panner = ctx.createStereoPanner();
    const gain = ctx.createGain();

    low.connect(mid);
    mid.connect(high);
    high.connect(panner);
    panner.connect(gain);
    gain.connect(state.sf2BusGain);

    state.channelNodes[ch] = { low, mid, high, panner, gain };
    return state.channelNodes[ch];
  }

  function updateChipPresetButtons(blend) {
    const val = Number(blend);
    for (const btn of dom.chipPresetBtns) {
      const target = parseFloat(btn.dataset.blend || "0");
      btn.classList.toggle("active", Math.abs(val - target) < 0.005);
    }
  }

  function updateChipRefStatusUI() {
    if (!dom.chipRefBadge) return;
    if (!state.trackData) {
      dom.chipRefBadge.classList.add("hidden");
      return;
    }
    dom.chipRefBadge.classList.remove("hidden", "is-active", "is-muted", "is-unavailable");
    const hasChipRef = Boolean(state.trackData.has_chip_ref);
    if (!hasChipRef) {
      dom.chipRefBadge.textContent = "Sem Chip DSP";
      dom.chipRefBadge.classList.add("is-unavailable");
      dom.chipRefBadge.title = "Nenhum _chip_ref.wav / B_studio_dsp.wav / A_original.wav encontrado";
    } else if (anyChannelSoloed()) {
      dom.chipRefBadge.textContent = "Chip DSP mudo (Solo)";
      dom.chipRefBadge.classList.add("is-muted");
      dom.chipRefBadge.title = "Camada Chip DSP silenciada automaticamente durante Solo";
    } else {
      dom.chipRefBadge.textContent = state.chipRefBuffer ? "Chip DSP pronto" : "Carregando Chip DSP...";
      dom.chipRefBadge.classList.add("is-active");
      dom.chipRefBadge.title = "Camada de chip com DSP sincronizada ao vivo";
    }
  }

  function applyChipBlendGains(immediate = false) {
    updateChipRefStatusUI();
    if (!state.audioCtx || !state.sf2BusGain || !state.chipRefGain) return;
    const now = state.audioCtx.currentTime;
    const soloActive = anyChannelSoloed();
    const rawBlend =
      state.profile && state.profile.track && state.profile.track.chip_blend !== undefined
        ? Number(state.profile.track.chip_blend)
        : 0.65;
    const blend = Math.max(0.0, Math.min(1.0, Number.isFinite(rawBlend) ? rawBlend : 0.65));

    const chipTarget = state.chipRefBuffer && !soloActive ? blend : 0.0;
    const sf2Target = state.chipRefBuffer && !soloActive ? 1.0 - blend : 1.0;

    if (immediate) {
      state.chipRefGain.gain.setValueAtTime(chipTarget, now);
      state.sf2BusGain.gain.setValueAtTime(sf2Target, now);
    } else {
      state.chipRefGain.gain.setTargetAtTime(chipTarget, now, 0.01);
      state.sf2BusGain.gain.setTargetAtTime(sf2Target, now, 0.01);
    }
  }

  function setChipBlend(val, markUnsaved = true) {
    if (!state.profile) return;
    const clamped = Math.max(0.0, Math.min(1.0, Math.round(Number(val) * 100) / 100));
    state.profile.track.chip_blend = clamped;
    dom.chipBlendSlider.value = String(clamped);
    dom.chipBlendVal.textContent = `${Math.round(clamped * 100)}%`;
    updateChipPresetButtons(clamped);
    if (markUnsaved) setUnsaved(true);
    applyChipBlendGains(false);
  }

  function applyMasterGain(immediate = false) {
    if (!state.audioCtx || !state.masterGainNode || !state.profile) return;
    const val = dbToLin(state.profile.track.master_gain_db || 0);
    const now = state.audioCtx.currentTime;
    if (immediate) {
      state.masterGainNode.gain.setValueAtTime(val, now);
    } else {
      state.masterGainNode.gain.setTargetAtTime(val, now, 0.012);
    }
  }

  function applyABGains(immediate = false) {
    if (!state.audioCtx || !state.abRemasterGain || !state.abOriginalGain) return;
    const now = state.audioCtx.currentTime;
    const rem = state.abMode === "remaster" ? 1.0 : 0.0;
    const orig = state.abMode === "original" ? 1.0 : 0.0;
    if (immediate) {
      state.abRemasterGain.gain.setValueAtTime(rem, now);
      state.abOriginalGain.gain.setValueAtTime(orig, now);
    } else {
      state.abRemasterGain.gain.setTargetAtTime(rem, now, 0.015);
      state.abOriginalGain.gain.setTargetAtTime(orig, now, 0.015);
    }
  }

  function anyChannelSoloed() {
    if (!state.profile) return false;
    return Object.values(state.profile.channels || {}).some(
      (cfg) => Boolean(cfg.solo) && !Boolean(cfg.mute) && cfg.mode !== "off" && cfg.mode !== "chip"
    );
  }

  function updateAllChannelAudioParams(immediate = false) {
    if (!state.profile || !state.audioCtx) return;
    const soloActive = anyChannelSoloed();
    dom.soloStatusBadge.classList.toggle("hidden", !soloActive);
    const now = state.audioCtx.currentTime;

    for (const [ch, cfg] of Object.entries(state.profile.channels || {})) {
      const nodes = state.channelNodes[ch];
      if (!nodes) continue;

      const isSilent =
        cfg.mode === "off" ||
        Boolean(cfg.mute) ||
        (soloActive && !Boolean(cfg.solo));

      const targetGain = isSilent ? 0.0 : dbToLin(cfg.volume_db || 0);
      const pan = Math.max(-1, Math.min(1, Number(cfg.pan || 0)));
      const low = Number(cfg.eq_low_db || 0);
      const mid = Number(cfg.eq_mid_db || 0);
      const high = Number(cfg.eq_high_db || 0);

      if (immediate) {
        nodes.gain.gain.setValueAtTime(targetGain, now);
        nodes.panner.pan.setValueAtTime(pan, now);
        nodes.low.gain.setValueAtTime(low, now);
        nodes.mid.gain.setValueAtTime(mid, now);
        nodes.high.gain.setValueAtTime(high, now);
      } else {
        nodes.gain.gain.setTargetAtTime(targetGain, now, 0.01);
        nodes.panner.pan.setTargetAtTime(pan, now, 0.01);
        nodes.low.gain.setTargetAtTime(low, now, 0.015);
        nodes.mid.gain.setTargetAtTime(mid, now, 0.015);
        nodes.high.gain.setTargetAtTime(high, now, 0.015);
      }
    }
    applyChipBlendGains(immediate);
  }

  function stopAllSources() {
    for (const src of Object.values(state.activeSources)) {
      try {
        src.onended = null;
        src.stop();
        src.disconnect();
      } catch (_) {}
    }
    state.activeSources = {};
    if (state.chipRefSource) {
      try {
        state.chipRefSource.onended = null;
        state.chipRefSource.stop();
        state.chipRefSource.disconnect();
      } catch (_) {}
      state.chipRefSource = null;
    }
    if (state.originalSource) {
      try {
        state.originalSource.onended = null;
        state.originalSource.stop();
        state.originalSource.disconnect();
      } catch (_) {}
      state.originalSource = null;
    }
  }

  function getCurrentPlaybackPosition() {
    if (!state.isPlaying || !state.audioCtx) return state.playbackOffset;
    const elapsed = state.audioCtx.currentTime - state.playbackStartCtxTime;
    const pos = state.playbackOffset + elapsed;
    const bounds = getLoopBounds();
    if (state.loopEnabled && bounds.end > bounds.start) {
      if (pos < bounds.end) return pos;
      const span = bounds.end - bounds.start;
      return bounds.start + ((pos - bounds.start) % span);
    }
    return Math.min(state.duration || 0, Math.max(0, pos));
  }

  function startChannelSource(ch, offset, when) {
    const buf = state.stemBuffers[ch];
    if (!buf || !state.audioCtx) return;
    const nodes = ensureChannelNodes(ch);
    const src = state.audioCtx.createBufferSource();
    src.buffer = buf;
    const bounds = getLoopBounds();
    if (state.loopEnabled && bounds.end > bounds.start) {
      src.loop = true;
      src.loopStart = Math.min(bounds.start, buf.duration);
      src.loopEnd = Math.min(bounds.end, buf.duration);
    } else {
      src.loop = false;
    }
    src.connect(nodes.low);
    const clampedOffset = offset % Math.max(0.001, buf.duration);
    if (src.loop || offset < buf.duration) {
      src.start(when, src.loop ? clampedOffset : offset);
    }
    state.activeSources[ch] = src;
  }

  function startPlayback(fromOffset) {
    if (!state.duration || state.duration <= 0) {
      const hasMidi = Boolean(state.trackData && (state.trackData.has_midi || state.trackData.has_midi_stems));
      showStatus(
        hasMidi
          ? "Nenhum canal ativo carregado em memória RAM. Ajuste os modos ou clique em 'Atualizar Timbres'."
          : "O perfil padrão (default.toml) não possui arquivo .mid próprio — ajuste os canais e salve o .toml, ou selecione um dos 17 BGMs para ouvir ao vivo.",
        "info"
      );
      return;
    }
    const ctx = ensureAudioContext();
    stopAllSources();

    const offset = Math.max(0, Math.min(state.duration - 0.01, fromOffset));
    const when = ctx.currentTime + 0.02;
    const bounds = getLoopBounds();

    for (const ch of Object.keys(state.stemBuffers)) {
      startChannelSource(ch, offset, when);
    }

    if (state.chipRefBuffer) {
      const csrc = ctx.createBufferSource();
      csrc.buffer = state.chipRefBuffer;
      if (state.loopEnabled && bounds.end > bounds.start) {
        csrc.loopStart = Math.min(bounds.start, state.chipRefBuffer.duration);
        csrc.loopEnd = Math.min(bounds.end, state.chipRefBuffer.duration);
        csrc.loop = csrc.loopEnd > csrc.loopStart;
      } else {
        csrc.loop = false;
      }
      csrc.connect(state.chipRefGain);
      const clampedOffset = offset % Math.max(0.001, state.chipRefBuffer.duration);
      if (csrc.loop || offset < state.chipRefBuffer.duration) {
        csrc.start(when, csrc.loop ? clampedOffset : offset);
      }
      state.chipRefSource = csrc;
    }

    if (state.originalBuffer) {
      const osrc = ctx.createBufferSource();
      osrc.buffer = state.originalBuffer;
      if (state.loopEnabled && bounds.end > bounds.start) {
        osrc.loopStart = Math.min(bounds.start, state.originalBuffer.duration);
        osrc.loopEnd = Math.min(bounds.end, state.originalBuffer.duration);
        osrc.loop = osrc.loopEnd > osrc.loopStart;
      } else {
        osrc.loop = false;
      }
      osrc.connect(state.abOriginalGain);
      const clampedOffset = offset % Math.max(0.001, state.originalBuffer.duration);
      if (osrc.loop || offset < state.originalBuffer.duration) {
        osrc.start(when, osrc.loop ? clampedOffset : offset);
      }
      state.originalSource = osrc;
    }

    updateAllChannelAudioParams(true);
    applyChipBlendGains(true);
    applyABGains(true);
    applyMasterGain(true);

    state.playbackOffset = offset;
    state.playbackStartCtxTime = when;
    state.isPlaying = true;
    updateTransportButtons();
    startUiLoop();
  }

  function pausePlayback() {
    if (!state.isPlaying) return;
    state.playbackOffset = getCurrentPlaybackPosition();
    stopAllSources();
    state.isPlaying = false;
    updateTransportButtons();
  }

  function stopPlayback() {
    stopAllSources();
    state.isPlaying = false;
    state.playbackOffset = 0;
    updateTransportButtons();
    updateTimelineUI(0);
    dom.masterMeterFill.style.width = "0%";
  }

  function updateTransportButtons() {
    dom.btnPlayPause.textContent = state.isPlaying ? "❚❚ Pause" : "▶ Play";
    dom.btnPlayPause.classList.toggle("playing", state.isPlaying);
    dom.btnLoop.classList.toggle("active", state.loopEnabled);
  }

  function updateTimelineUI(pos) {
    dom.timeCurrent.textContent = fmtTime(pos);
    dom.timeTotal.textContent = fmtTime(state.duration);
    if (state.duration > 0) {
      dom.seekBar.value = String(Math.round((pos / state.duration) * 1000));
    } else {
      dom.seekBar.value = "0";
    }
  }

  function startUiLoop() {
    if (state.rafId !== null) return;
    const meterBuf = state.analyserNode
      ? new Uint8Array(state.analyserNode.fftSize)
      : null;

    const tick = () => {
      if (!state.isPlaying) {
        state.rafId = null;
        return;
      }
      const pos = getCurrentPlaybackPosition();
      updateTimelineUI(pos);

      if (!state.loopEnabled && pos >= state.duration - 0.02) {
        stopPlayback();
        state.rafId = null;
        return;
      }

      if (state.analyserNode && meterBuf) {
        state.analyserNode.getByteTimeDomainData(meterBuf);
        let peak = 0;
        for (let i = 0; i < meterBuf.length; i++) {
          const v = Math.abs(meterBuf[i] - 128) / 128;
          if (v > peak) peak = v;
        }
        const pct = Math.min(100, Math.round(Math.pow(peak, 0.7) * 100));
        dom.masterMeterFill.style.width = `${pct}%`;
      }
      state.rafId = requestAnimationFrame(tick);
    };
    state.rafId = requestAnimationFrame(tick);
  }

  function padBufferWithSilence(buf, targetDuration) {
    if (!buf || !state.audioCtx || buf.duration >= targetDuration) return buf;
    const targetLength = Math.ceil(targetDuration * buf.sampleRate);
    if (targetLength <= buf.length) return buf;
    const padded = state.audioCtx.createBuffer(
      buf.numberOfChannels,
      targetLength,
      buf.sampleRate
    );
    for (let ch = 0; ch < buf.numberOfChannels; ch++) {
      padded.getChannelData(ch).set(buf.getChannelData(ch));
    }
    return padded;
  }

  async function fetchAndDecodeWav(url) {
    const res = await fetch(url, { cache: "no-store" });
    if (!res.ok) throw new Error(`Falha ao baixar WAV (${res.status})`);
    const arr = await res.arrayBuffer();
    const ctx = ensureAudioContext();
    return await ctx.decodeAudioData(arr);
  }

  async function loadAllAudioBuffers(trackData, token) {
    state.stemBuffers = {};
    state.chipRefBuffer = null;
    state.originalBuffer = null;
    state.duration = Number(trackData.duration_s || 0);
    const hasMidi = Boolean(trackData.has_midi !== undefined ? trackData.has_midi : trackData.has_midi_stems);
    dom.audioStateBadge.textContent = hasMidi
      ? "Sintetizando canais MIDI via TinySoundFont (RAM)..."
      : "Perfil padrão (default.toml) — sem arquivo .mid próprio";
    updateChipRefStatusUI();

    const stems = trackData.stems || {};
    const entries = Object.entries(stems).filter(([, s]) => s.has_wav && (s.audio_url || s.wav_url));
    let loaded = 0;

    await Promise.all(
      entries.map(async ([ch, s]) => {
        try {
          const buf = await fetchAndDecodeWav(s.audio_url || s.wav_url);
          if (token !== state.loadToken) return;
          state.stemBuffers[ch] = buf;
          ensureChannelNodes(ch);
          if (buf.duration > state.duration) state.duration = buf.duration;
          loaded += 1;
          updateStripStemDot(ch, "ready");
        } catch (err) {
          console.warn(`Erro decodificando canal ${ch}:`, err);
          updateStripStemDot(ch, "missing");
        }
      })
    );

    if (token !== state.loadToken) return;

    if (trackData.has_chip_ref && trackData.chip_ref_url) {
      try {
        const cbuf = await fetchAndDecodeWav(trackData.chip_ref_url);
        if (token === state.loadToken) {
          if (cbuf.duration > state.duration) {
            state.duration = cbuf.duration;
            state.chipRefBuffer = cbuf;
          } else if (cbuf.duration < state.duration) {
            state.chipRefBuffer = padBufferWithSilence(cbuf, state.duration);
          } else {
            state.chipRefBuffer = cbuf;
          }
        }
      } catch (err) {
        console.warn("Erro carregando chip_ref.wav:", err);
      }
    }

    if (token !== state.loadToken) return;

    if (trackData.original && trackData.original.available && trackData.original.url) {
      try {
        const obuf = await fetchAndDecodeWav(trackData.original.url);
        if (token === state.loadToken) {
          if (obuf.duration > state.duration) {
            state.duration = obuf.duration;
            state.originalBuffer = obuf;
          } else if (obuf.duration < state.duration) {
            state.originalBuffer = padBufferWithSilence(obuf, state.duration);
          } else {
            state.originalBuffer = obuf;
          }
        }
      } catch (err) {
        console.warn("Erro carregando A_original.wav:", err);
      }
    }

    if (token !== state.loadToken) return;

    // Garante que chipRefBuffer e originalBuffer acompanhem eventual expansão de state.duration
    if (state.chipRefBuffer && state.chipRefBuffer.duration < state.duration) {
      state.chipRefBuffer = padBufferWithSilence(state.chipRefBuffer, state.duration);
    }
    if (state.originalBuffer && state.originalBuffer.duration < state.duration) {
      state.originalBuffer = padBufferWithSilence(state.originalBuffer, state.duration);
    }

    updateAllChannelAudioParams(true);
    updateTimelineUI(0);
    const chipTag = state.chipRefBuffer ? " + Chip DSP" : "";
    dom.audioStateBadge.textContent =
      loaded > 0
        ? `Pronto · ${loaded} canais sintetizados em RAM${chipTag} (${fmtTime(state.duration)})`
        : state.chipRefBuffer
          ? `Pronto · apenas Chip DSP em memória (${fmtTime(state.duration)})`
          : hasMidi
            ? "Nenhum canal ativo com síntese SoundFont — habilite um canal ou clique em Atualizar Timbres"
            : "Perfil padrão (.toml) — sem arquivo .mid próprio";
  }

  // -------------------------------------------------------------------------
  // Renderização da UI do Mixer
  // -------------------------------------------------------------------------
  function escHtml(v) {
    return String(v === undefined || v === null ? "" : v)
      .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
  }

  function channelKindClass(ch) {
    if (ch.startsWith("v_")) return "kind-fm";
    if (ch === "dac") return "kind-dac";
    if (ch === "psg_noise") return "kind-noise";
    if (ch.startsWith("psg")) return "kind-psg";
    return "kind-fm";
  }

  function populateProgramSelect(selectEl, mode, currentProgram) {
    selectEl.innerHTML = "";
    const cur = Number(currentProgram) || 0;
    if (mode === "auto") {
      const opt = document.createElement("option");
      opt.value = String(cur);
      opt.textContent = "Dinâmico: Bass 33 / Strings 49 / Brass 62 / Lead 81";
      opt.selected = true;
      selectEl.appendChild(opt);
      return;
    }
    if (mode === "percussion") {
      const grpKits = document.createElement("optgroup");
      grpKits.label = "GM Drum Kits (Bank 128)";
      let matched = false;
      for (const kit of DRUM_KITS) {
        const opt = document.createElement("option");
        opt.value = String(kit.program);
        opt.textContent = kit.label;
        if (kit.program === cur) {
          opt.selected = true;
          matched = true;
        }
        grpKits.appendChild(opt);
      }
      if (!matched) {
        const opt = document.createElement("option");
        opt.value = String(cur);
        opt.textContent = `${String(cur).padStart(3, "0")} · Custom Drum Kit`;
        opt.selected = true;
        grpKits.appendChild(opt);
      }
      selectEl.appendChild(grpKits);
      return;
    }

    for (const cat of GM_CATEGORIES) {
      const grp = document.createElement("optgroup");
      grp.label = cat.name;
      cat.items.forEach((name, idx) => {
        const prog = cat.start + idx;
        const opt = document.createElement("option");
        opt.value = String(prog);
        opt.textContent = `${String(prog).padStart(3, "0")} · ${name}`;
        if (prog === cur) opt.selected = true;
        grp.appendChild(opt);
      });
      selectEl.appendChild(grp);
    }
  }

  function updateStripStemDot(ch, status) {
    const dot = document.querySelector(`.channel-strip[data-channel="${ch}"] .stem-dot`);
    if (!dot) return;
    dot.classList.remove("ready", "missing", "loading");
    if (status) dot.classList.add(status);
  }

  function refreshStripStateClasses(ch) {
    const strip = document.querySelector(`.channel-strip[data-channel="${ch}"]`);
    if (!strip || !state.profile) return;
    const cfg = state.profile.channels[ch];
    if (!cfg) return;
    strip.classList.toggle("is-off", cfg.mode === "off");
    strip.classList.toggle("is-muted", Boolean(cfg.mute));
    strip.classList.toggle("is-solo", Boolean(cfg.solo));
    strip.classList.toggle("needs-render", state.dirtyRenderChannels.has(ch));
    strip.classList.toggle("is-rendering", state.renderingChannels.has(ch));

    const muteBtn = strip.querySelector(".ms-btn.mute");
    const soloBtn = strip.querySelector(".ms-btn.solo");
    if (muteBtn) muteBtn.classList.toggle("active", Boolean(cfg.mute));
    if (soloBtn) soloBtn.classList.toggle("active", Boolean(cfg.solo));

    const applyBtn = strip.querySelector(".btn-apply-inst");
    if (applyBtn) {
      const rendering = state.renderingChannels.has(ch);
      const dirty = state.dirtyRenderChannels.has(ch);
      applyBtn.disabled = rendering;
      applyBtn.classList.toggle("pending", dirty && !rendering);
      applyBtn.textContent = rendering
        ? "Sintetizando..."
        : dirty
          ? "● Aplicar Timbre"
          : "Aplicar Timbre";
    }
  }

  function renderMixerStrips() {
    dom.mixerGrid.innerHTML = "";
    if (!state.profile || !state.trackData) return;

    const order = state.trackData.channel_order || Object.keys(state.profile.channels || {});
    const stems = state.trackData.stems || {};
    const hasTrackMidi = Boolean(
      state.trackData.has_midi !== undefined ? state.trackData.has_midi : state.trackData.has_midi_stems
    );

    for (const ch of order) {
      const cfg = state.profile.channels[ch];
      if (!cfg) continue;
      const stem = stems[ch] || {};
      const noteCount = Number(stem.note_count || 0);
      const chHasMidi = Boolean(stem.has_midi);

      const strip = document.createElement("article");
      strip.className = "channel-strip" + (cfg.voice ? " voice-strip" : "");
      strip.dataset.channel = ch;
      const globalWarn = Number(cfg.used_in || 0) > 1 ? `<span class="voice-warn">⚠ edição global</span>` : "";
      const voiceBlock = cfg.voice
        ? `
        <div class="voice-box">
          <input type="text" class="strip-input voice-name" maxlength="48" placeholder="nome do instrumento (opcional)" value="${escHtml(cfg.name)}" />
          <div class="voice-note" title="A edição é GLOBAL: vale para todas as músicas que usam este patch (voices.toml).">
            Usado em <strong>${Number(cfg.used_in || 0)}</strong> música(s): ${escHtml((cfg.songs || []).join(", "))}
            ${globalWarn}
          </div>
          <div class="voice-note">Sugestão: <strong>${escHtml(cfg.suggestion || "—")}</strong> · ALG ${Number(cfg.alg || 0)} FB ${Number(cfg.fb || 0)} · nota mediana ${Number(cfg.note_median || 0)}</div>
          <div class="voice-note voice-drum-row hidden">
            Nota de bateria GM (0 = usar a nota da trilha; 42 = closed hi-hat, 38 = snare, 36 = kick):
            <input type="number" class="strip-input voice-drum-note" min="0" max="127" step="1" value="${Number(cfg.drum_note || 0)}" />
          </div>
          <div class="voice-note voice-chip-note hidden">chip = som original do chip, só existe dentro do jogo; mudo neste preview.</div>
        </div>`
        : "";

      const dotClass = stem.has_wav ? "ready" : "";

      const footText = stem.has_wav
        ? `${Number(stem.duration_s || 0).toFixed(1)}s · RAM`
        : chHasMidi
          ? `${cfg.mode}`
          : hasTrackMidi
            ? "sem notas"
            : "Fallback .toml";
      const midiFootText = chHasMidi
        ? `${noteCount} notas`
        : hasTrackMidi
          ? "0 notas"
          : "Tempo real";

      strip.innerHTML = `
        <div class="strip-header">
          <div class="ch-title-wrap">
            <span class="stem-dot ${dotClass}" title="${stem.has_wav ? "Canal sintetizado em RAM via TinySoundFont (tsf.h)" : chHasMidi ? "Canal com notas MIDI (modo sem síntese SoundFont)" : "Sem notas MIDI para este canal"}"></span>
            <span class="ch-tag ${channelKindClass(ch)}">${ch}</span>
            <span class="ch-role">${cfg.role || ""}</span>
          </div>
          <div class="ms-group">
            <button type="button" class="ms-btn mute ${cfg.mute ? "active" : ""}" title="Mute (M)">M</button>
            <button type="button" class="ms-btn solo ${cfg.solo ? "active" : ""}" title="Solo (S)">S</button>
          </div>
        </div>

        ${voiceBlock}

        <div class="field-row">
          <label class="field-label"><span>Tipo / Modo</span></label>
          <select class="strip-select ch-mode"></select>
        </div>

        <div class="field-row">
          <label class="field-label"><span>Instrumento GM (0–127)</span></label>
          <select class="strip-select ch-program"></select>
        </div>

        <div class="field-row">
          <label class="field-label"><span>Transpose (-24..+24)</span></label>
          <div class="transpose-row">
            <input type="number" class="strip-input ch-transpose" min="-24" max="24" step="1" value="${Number(cfg.transpose || 0)}" />
            <button type="button" class="btn-apply-inst ${chHasMidi ? "" : "hidden"}">Aplicar Timbre</button>
          </div>
        </div>

        <div class="field-row">
          <div class="field-label">
            <span>Volume</span>
            <output class="out-vol">${fmtDb(cfg.volume_db)}</output>
          </div>
          <input type="range" class="slider-full ch-volume" min="-36" max="12" step="0.5" value="${Number(cfg.volume_db || 0)}" />
        </div>

        <div class="field-row pan-row">
          <div class="field-label">
            <span>Pan</span>
            <output class="out-pan">${fmtPan(cfg.pan)}</output>
          </div>
          <input type="range" class="slider-full ch-pan" min="-1" max="1" step="0.05" value="${Number(cfg.pan || 0)}" />
        </div>

        <div class="eq-box">
          <div class="eq-title" title="EQ por canal atua apenas no preview do navegador; o jogo aplica EQ de estúdio no barramento master e lê volume, instrumento, pan, mute, solo, transpose, chip_blend e master_gain_db do .toml.">EQ 3 Bandas — Preview (dB)</div>
          <div class="eq-row">
            <span>Low</span>
            <input type="range" class="ch-eq-low" min="-12" max="12" step="0.5" value="${Number(cfg.eq_low_db || 0)}" />
            <output class="out-eq-low">${fmtDb(cfg.eq_low_db)}</output>
          </div>
          <div class="eq-row">
            <span>Mid</span>
            <input type="range" class="ch-eq-mid" min="-12" max="12" step="0.5" value="${Number(cfg.eq_mid_db || 0)}" />
            <output class="out-eq-mid">${fmtDb(cfg.eq_mid_db)}</output>
          </div>
          <div class="eq-row">
            <span>High</span>
            <input type="range" class="ch-eq-high" min="-12" max="12" step="0.5" value="${Number(cfg.eq_high_db || 0)}" />
            <output class="out-eq-high">${fmtDb(cfg.eq_high_db)}</output>
          </div>
        </div>

        <div class="strip-foot">
          <span class="foot-midi">${midiFootText}</span>
          <span class="foot-status">${footText}</span>
        </div>
      `;

      // Preenche modos e instrumentos
      const modeSel = strip.querySelector(".ch-mode");
      const modeOptions = ch.startsWith("fm") || cfg.mode === "auto"
        ? [AUTO_MODE_OPT, ...MODES]
        : MODES;
      for (const m of modeOptions) {
        const opt = document.createElement("option");
        opt.value = m.value;
        opt.textContent = m.label;
        if (m.value === cfg.mode) opt.selected = true;
        modeSel.appendChild(opt);
      }

      const progSel = strip.querySelector(".ch-program");
      const transInput = strip.querySelector(".ch-transpose");
      const applyBtn = strip.querySelector(".btn-apply-inst");

      populateProgramSelect(progSel, cfg.mode, cfg.program);
      const syncInstDisabling = () => {
        const m = cfg.mode;
        progSel.disabled = m === "off" || m === "chip" || m === "auto";
        transInput.disabled = m === "off" || m === "percussion";
      };
      syncInstDisabling();
      const chipNote = strip.querySelector(".voice-chip-note");
      const syncChipNote = () => {
        if (chipNote) chipNote.classList.toggle("hidden", cfg.mode !== "chip");
        const drumRow = strip.querySelector(".voice-drum-row");
        if (drumRow) drumRow.classList.toggle("hidden", cfg.mode !== "percussion");
      };
      syncChipNote();
      const drumInput = strip.querySelector(".voice-drum-note");
      if (drumInput) {
        drumInput.addEventListener("change", () => {
          cfg.drum_note = Math.max(0, Math.min(127, parseInt(drumInput.value, 10) || 0));
          drumInput.value = String(cfg.drum_note);
          onSynthParamChanged();
        });
      }
      const nameInput = strip.querySelector(".voice-name");
      if (nameInput) {
        nameInput.addEventListener("change", () => {
          cfg.name = nameInput.value.trim();
          setUnsaved(true);
        });
      }

      // Eventos que exigem re-render do stem (quando há preview local)
      const onSynthParamChanged = () => {
        setUnsaved(true);
        if (stem.has_midi) {
          state.dirtyRenderChannels.add(ch);
        }
        refreshStripStateClasses(ch);
        updateAllChannelAudioParams();
        if (stem.has_midi && dom.autoApplyCheck.checked) {
          renderSingleChannel(ch);
        }
      };

      modeSel.addEventListener("change", () => {
        const newMode = modeSel.value;
        cfg.mode = newMode;
        cfg.bank = newMode === "percussion" ? 128 : 0;
        populateProgramSelect(progSel, newMode, cfg.program);
        syncInstDisabling();
        syncChipNote();
        onSynthParamChanged();
      });

      progSel.addEventListener("change", () => {
        cfg.program = parseInt(progSel.value, 10) || 0;
        onSynthParamChanged();
      });

      transInput.addEventListener("change", () => {
        const clamped = Math.max(-24, Math.min(24, parseInt(transInput.value, 10) || 0));
        transInput.value = String(clamped);
        cfg.transpose = clamped;
        onSynthParamChanged();
      });

      applyBtn.addEventListener("click", () => {
        renderSingleChannel(ch);
      });

      // Controles instantâneos (0 ms Web Audio)
      const volSlider = strip.querySelector(".ch-volume");
      const outVol = strip.querySelector(".out-vol");
      volSlider.addEventListener("input", () => {
        cfg.volume_db = parseFloat(volSlider.value);
        outVol.textContent = fmtDb(cfg.volume_db);
        setUnsaved(true);
        updateAllChannelAudioParams();
      });
      volSlider.addEventListener("dblclick", () => {
        cfg.volume_db = 0;
        volSlider.value = "0";
        outVol.textContent = fmtDb(0);
        setUnsaved(true);
        updateAllChannelAudioParams();
      });

      const panSlider = strip.querySelector(".ch-pan");
      const outPan = strip.querySelector(".out-pan");
      panSlider.addEventListener("input", () => {
        cfg.pan = parseFloat(panSlider.value);
        outPan.textContent = fmtPan(cfg.pan);
        setUnsaved(true);
        updateAllChannelAudioParams();
      });
      panSlider.addEventListener("dblclick", () => {
        cfg.pan = 0;
        panSlider.value = "0";
        outPan.textContent = fmtPan(0);
        setUnsaved(true);
        updateAllChannelAudioParams();
      });

      const bindEq = (sel, outSel, prop) => {
        const inp = strip.querySelector(sel);
        const out = strip.querySelector(outSel);
        inp.addEventListener("input", () => {
          cfg[prop] = parseFloat(inp.value);
          out.textContent = fmtDb(cfg[prop]);
          setUnsaved(true);
          updateAllChannelAudioParams();
        });
        inp.addEventListener("dblclick", () => {
          cfg[prop] = 0;
          inp.value = "0";
          out.textContent = fmtDb(0);
          setUnsaved(true);
          updateAllChannelAudioParams();
        });
      };
      bindEq(".ch-eq-low", ".out-eq-low", "eq_low_db");
      bindEq(".ch-eq-mid", ".out-eq-mid", "eq_mid_db");
      bindEq(".ch-eq-high", ".out-eq-high", "eq_high_db");

      strip.querySelector(".ms-btn.mute").addEventListener("click", () => {
        cfg.mute = !cfg.mute;
        setUnsaved(true);
        refreshStripStateClasses(ch);
        updateAllChannelAudioParams();
      });

      strip.querySelector(".ms-btn.solo").addEventListener("click", () => {
        cfg.solo = !cfg.solo;
        setUnsaved(true);
        refreshStripStateClasses(ch);
        updateAllChannelAudioParams();
      });

      dom.mixerGrid.appendChild(strip);
      refreshStripStateClasses(ch);
    }
  }

  function updateTrackMetadataUI() {
    if (!state.profile || !state.trackData) return;
    const tr = state.profile.track;
    const bgmId = Number(tr.bgm_id ?? 0);
    dom.metaBgmId.textContent = bgmId < 0 ? "Padrão (-1)" : String(bgmId);
    const sf = String(tr.soundfont || "—");
    dom.metaSoundfont.textContent = sf.split(/[\\/]/).pop() || sf;
    dom.metaSoundfont.title = sf;
    dom.metaSampleRate.textContent = `${tr.sample_rate || 48000} Hz`;
    dom.metaLoop.textContent = `${tr.loop_start || 0} → ${tr.loop_end || 0}`;

    const stems = Object.values(state.trackData.stems || {});
    const wavReady = stems.filter((s) => s.has_wav).length;
    const midiActiveCount = stems.filter((s) => s.has_midi).length;
    const hasMidi = Boolean(
      state.trackData.has_midi !== undefined
        ? state.trackData.has_midi
        : state.trackData.has_midi_stems !== undefined
          ? state.trackData.has_midi_stems
          : midiActiveCount > 0
    );
    const nVoices = Object.values(state.profile.channels || {}).filter((c) => c.voice).length;
    dom.metaStems.textContent = hasMidi
      ? `${wavReady}/${midiActiveCount} ativos${nVoices ? ` · ${nVoices} vozes FM` : ""}`
      : "Perfil padrão";
    if (dom.metaEngine) {
      dom.metaEngine.textContent = state.trackData.synth_engine || "TinySoundFont (tsf.h)";
    }

    dom.btnRenderAll.classList.toggle("hidden", !hasMidi);
    dom.btnRenderAll.disabled = !hasMidi;
    if (dom.btnDownloadMidi) {
      dom.btnDownloadMidi.disabled = !hasMidi || !state.trackData.midi_url;
    }

    dom.masterGainSlider.value = String(Number(tr.master_gain_db || 0));
    dom.masterGainVal.textContent = fmtDb(tr.master_gain_db || 0);
    const chipBlend =
      tr.chip_blend !== undefined && tr.chip_blend !== null
        ? Number(tr.chip_blend)
        : 0.65;
    tr.chip_blend = chipBlend;
    dom.chipBlendSlider.value = String(chipBlend);
    dom.chipBlendVal.textContent = `${Math.round(chipBlend * 100)}%`;
    updateChipPresetButtons(chipBlend);
    updateChipRefStatusUI();

    const origAvail = Boolean(state.trackData.original && state.trackData.original.available);
    dom.btnModeB.disabled = !origAvail;
    dom.btnModeB.title = origAvail
      ? "Ouvir áudio original (Tecla B)"
      : "Original indisponível: sem A_original.wav — use Gerar original (roda o jogo headless, só em RAM)";
    const canGen = !origAvail && Boolean(state.trackData.original && state.trackData.original.can_generate);
    dom.btnGenOriginal.classList.toggle("hidden", !canGen);
    if (!origAvail && state.abMode === "original") {
      setABMode("remaster");
    }

    dom.realtimeNoticeBanner.classList.toggle("hidden", hasMidi);
    dom.exportMetaFooter.textContent =
      "Síntese MIDI em tempo real via TinySoundFont (tsf.h) + TimGM6mb.sf2 — zero arquivos .wav gravados em disco.";
  }

  function setABMode(mode) {
    if (mode === "original" && dom.btnModeB.disabled) return;
    state.abMode = mode;
    dom.btnModeA.classList.toggle("active", mode === "remaster");
    dom.btnModeB.classList.toggle("active", mode === "original");
    applyABGains(false);
  }

  // -------------------------------------------------------------------------
  // Ações de backend (load, render-channel, render-all, save, download midi)
  // -------------------------------------------------------------------------
  async function loadTrackList(keepSelected = true) {
    const prev = keepSelected ? state.currentTrack : null;
    const data = await apiJson("/api/tracks");
    state.tracks = data.tracks || [];
    dom.trackSelect.innerHTML = "";

    if (state.tracks.length === 0) {
      const opt = document.createElement("option");
      opt.value = "";
      opt.textContent = "Nenhum perfil .toml encontrado";
      dom.trackSelect.appendChild(opt);
      showStatus("Nenhum perfil encontrado em assets/audio_remaster/profiles/.", "warn");
      return;
    }

    for (const tr of state.tracks) {
      const opt = document.createElement("option");
      opt.value = tr.name;
      const idLabel =
        tr.is_default || tr.bgm_id < 0 ? "[Padrão]" : `[ID ${tr.bgm_id}]`;
      const midiTag = tr.has_midi ? " [MIDI]" : "";
      opt.textContent = `${idLabel} ${tr.name}${midiTag}`;
      dom.trackSelect.appendChild(opt);
    }

    const target =
      prev && state.tracks.some((t) => t.name === prev)
        ? prev
        : state.tracks[0].name;
    dom.trackSelect.value = target;
    await selectTrack(target);
  }

  async function selectTrack(trackName) {
    if (!trackName) return;
    stopPlayback();
    hideStatus();
    state.currentTrack = trackName;
    state.dirtyRenderChannels.clear();
    state.renderingChannels.clear();
    state.pendingRerender.clear();
    setUnsaved(false);
    const token = ++state.loadToken;

    try {
      const data = await apiJson(`/api/tracks/${encodeURIComponent(trackName)}`);
      if (token !== state.loadToken) return;
      state.trackData = data;
      state.profile = data.profile;
      updateTrackMetadataUI();
      renderMixerStrips();
      await loadAllAudioBuffers(data, token);
    } catch (err) {
      showStatus(`Erro ao carregar faixa '${trackName}': ${err.message}`, "error");
    }
  }

  async function renderSingleChannel(ch) {
    if (!state.currentTrack || !state.profile) return;
    const cfg = state.profile.channels[ch];
    if (!cfg) return;
    if (state.renderingChannels.has(ch)) {
      state.pendingRerender.add(ch);
      return;
    }

    const token = state.loadToken;
    state.pendingRerender.delete(ch);
    state.renderingChannels.add(ch);
    refreshStripStateClasses(ch);
    updateStripStemDot(ch, "loading");

    try {
      const res = await apiJson(
        `/api/tracks/${encodeURIComponent(state.currentTrack)}/render-channel`,
        {
          method: "POST",
          body: JSON.stringify({
            channel: ch,
            mode: cfg.mode,
            program: cfg.program,
            bank: cfg.bank,
            transpose: cfg.transpose,
            drum_note: cfg.drum_note,
            save_profile: false,
          }),
        }
      );
      if (token !== state.loadToken) return;

      // Recarrega apenas o buffer deste canal em RAM mantendo posição/mix dos demais
      const audioUrl = res.audio_url || res.wav_url;
      const newBuf = await fetchAndDecodeWav(audioUrl);
      if (token !== state.loadToken) return;

      state.stemBuffers[ch] = newBuf;
      if (res.backend !== "silent" && newBuf.duration > state.duration) {
        state.duration = newBuf.duration;
        if (state.chipRefBuffer && state.chipRefBuffer.duration < state.duration) {
          state.chipRefBuffer = padBufferWithSilence(state.chipRefBuffer, state.duration);
        }
        if (state.originalBuffer && state.originalBuffer.duration < state.duration) {
          state.originalBuffer = padBufferWithSilence(state.originalBuffer, state.duration);
        }
      }

      if (state.isPlaying && state.audioCtx) {
        const oldSrc = state.activeSources[ch];
        if (oldSrc) {
          try {
            oldSrc.stop();
            oldSrc.disconnect();
          } catch (_) {}
          delete state.activeSources[ch];
        }
        if (res.backend !== "silent") {
          const curPos = getCurrentPlaybackPosition();
          const bounds = getLoopBounds();
          let targetPos = curPos + 0.015;
          if (state.loopEnabled && bounds.end > bounds.start) {
            if (targetPos >= bounds.end) {
              const span = bounds.end - bounds.start;
              targetPos = bounds.start + ((targetPos - bounds.start) % span);
            }
          }
          startChannelSource(ch, targetPos, state.audioCtx.currentTime + 0.015);
        }
        updateAllChannelAudioParams(true);
      }

      if (!state.pendingRerender.has(ch)) {
        state.dirtyRenderChannels.delete(ch);
      }
      updateStripStemDot(ch, res.backend === "silent" ? "" : "ready");
      const foot = document.querySelector(`.channel-strip[data-channel="${ch}"] .foot-status`);
      if (foot) {
        foot.textContent =
          res.backend === "silent"
            ? `${res.mode}`
            : `${Number(res.duration_s || 0).toFixed(1)}s · RAM`;
      }
      showStatus(
        `Canal ${ch} sintetizado em RAM (${res.mode}, prog ${res.program}, ${res.elapsed_ms} ms).`,
        "success"
      );
    } catch (err) {
      if (token !== state.loadToken) return;
      updateStripStemDot(ch, "missing");
      showStatus(`Falha ao sintetizar canal ${ch}: ${err.message}`, "error");
    } finally {
      if (token === state.loadToken) {
        state.renderingChannels.delete(ch);
        const shouldRerender = state.pendingRerender.has(ch);
        if (shouldRerender) {
          state.pendingRerender.delete(ch);
          state.dirtyRenderChannels.add(ch);
        }
        refreshStripStateClasses(ch);
        if (shouldRerender) {
          renderSingleChannel(ch);
        }
      }
    }
  }

  async function renderAllStems() {
    if (!state.currentTrack || !state.profile || state.isBusyGlobal) return;
    let token = state.loadToken;
    state.isBusyGlobal = true;
    dom.btnRenderAll.disabled = true;
    showStatus(`Sintetizando todos os canais de ${state.currentTrack} em memória RAM...`, "info");

    try {
      const wasPlaying = state.isPlaying;
      const curPos = getCurrentPlaybackPosition();
      if (wasPlaying) pausePlayback();

      const res = await apiJson(
        `/api/tracks/${encodeURIComponent(state.currentTrack)}/render-all`,
        {
          method: "POST",
          body: JSON.stringify({ profile: state.profile, save_profile: false }),
        }
      );
      if (token !== state.loadToken) return;

      state.dirtyRenderChannels.clear();
      state.pendingRerender.clear();
      state.trackData.stems = res.stems;
      if (res.has_midi !== undefined) state.trackData.has_midi = res.has_midi;
      state.trackData.has_any_wav = true;
      state.trackData.missing_wav_channels = res.missing_wav_channels || [];
      state.trackData.duration_s = res.duration_s;

      updateTrackMetadataUI();
      renderMixerStrips();
      token = ++state.loadToken;
      await loadAllAudioBuffers(state.trackData, token);
      if (token !== state.loadToken) return;

      showStatus(
        `Timbres sintetizados em RAM via ${res.synth_engine || "TinySoundFont (tsf.h)"} (${res.elapsed_ms} ms).`,
        "success"
      );
      if (wasPlaying) startPlayback(curPos);
    } catch (err) {
      if (token !== state.loadToken) return;
      showStatus(`Erro ao sintetizar canais: ${err.message}`, "error");
    } finally {
      state.isBusyGlobal = false;
      const hasMidi = Boolean(
        state.trackData &&
          (state.trackData.has_midi !== undefined
            ? state.trackData.has_midi
            : state.trackData.has_midi_stems)
      );
      dom.btnRenderAll.disabled = !hasMidi;
    }
  }

  async function saveProfile() {
    if (!state.currentTrack || !state.profile || state.isBusyGlobal) return;
    dom.btnSave.disabled = true;
    try {
      const res = await apiJson(
        `/api/tracks/${encodeURIComponent(state.currentTrack)}/save`,
        {
          method: "POST",
          body: JSON.stringify({ profile: state.profile }),
        }
      );
      state.profile = res.profile;
      setUnsaved(false);
      const nv = (res.voices_saved || []).length;
      showStatus(
        nv > 0
          ? `Salvo: ${nv} voz(es) em ${res.voices_path} (GLOBAL) + canais não-FM em ${res.profile_path}.`
          : `Perfil salvo em ${res.profile_path} — aplicado em tempo real pelo jogo.`,
        "success"
      );
    } catch (err) {
      showStatus(`Erro ao salvar perfil: ${err.message}`, "error");
    } finally {
      dom.btnSave.disabled = false;
    }
  }

  async function generateOriginal() {
    if (!state.currentTrack || state.isBusyGlobal) return;
    const track = state.currentTrack;
    state.isBusyGlobal = true;
    dom.btnGenOriginal.disabled = true;
    showStatus(`Gerando original de ${track} rodando o jogo headless (~1 min, só em RAM)...`, "info");
    try {
      const res = await apiJson(`/api/tracks/${encodeURIComponent(track)}/generate-original`, {
        method: "POST",
        body: "{}",
      });
      if (track !== state.currentTrack) return;
      const data = await apiJson(`/api/tracks/${encodeURIComponent(track)}`);
      state.trackData.original = data.original;
      if (data.original && data.original.available && data.original.url) {
        const obuf = await fetchAndDecodeWav(data.original.url);
        state.originalBuffer = state.duration > obuf.duration ? padBufferWithSilence(obuf, state.duration) : obuf;
      }
      updateTrackMetadataUI();
      showStatus(`Original gerado (${Number(res.duration_s || 0).toFixed(1)} s, ${res.cached ? "cache" : "novo"}). Tecla B alterna.`, "success");
    } catch (err) {
      showStatus(`Falha ao gerar original: ${err.message}`, "error");
    } finally {
      state.isBusyGlobal = false;
      dom.btnGenOriginal.disabled = false;
    }
  }

  function downloadMidi() {
    if (!state.currentTrack || !state.trackData || !state.trackData.midi_url) return;
    const a = document.createElement("a");
    a.href = state.trackData.midi_url;
    a.download = `${state.currentTrack}.mid`;
    document.body.appendChild(a);
    a.click();
    a.remove();
  }

  // -------------------------------------------------------------------------
  // Listeners globais
  // -------------------------------------------------------------------------
  dom.trackSelect.addEventListener("change", () => {
    selectTrack(dom.trackSelect.value);
  });

  dom.btnReloadTracks.addEventListener("click", () => {
    loadTrackList(true);
  });

  dom.btnPlayPause.addEventListener("click", () => {
    if (state.isPlaying) {
      pausePlayback();
    } else {
      startPlayback(state.playbackOffset);
    }
  });

  dom.btnStop.addEventListener("click", () => {
    stopPlayback();
  });

  dom.btnLoop.addEventListener("click", () => {
    state.loopEnabled = !state.loopEnabled;
    updateTransportButtons();
    if (state.isPlaying) {
      startPlayback(getCurrentPlaybackPosition());
    }
  });

  dom.seekBar.addEventListener("input", () => {
    if (state.duration <= 0) return;
    const target = (Number(dom.seekBar.value) / 1000) * state.duration;
    dom.timeCurrent.textContent = fmtTime(target);
  });

  dom.seekBar.addEventListener("change", () => {
    if (state.duration <= 0) return;
    const target = (Number(dom.seekBar.value) / 1000) * state.duration;
    if (state.isPlaying) {
      startPlayback(target);
    } else {
      state.playbackOffset = target;
      updateTimelineUI(target);
    }
  });

  dom.btnModeA.addEventListener("click", () => setABMode("remaster"));
  dom.btnModeB.addEventListener("click", () => setABMode("original"));

  dom.masterGainSlider.addEventListener("input", () => {
    if (!state.profile) return;
    const val = parseFloat(dom.masterGainSlider.value);
    state.profile.track.master_gain_db = val;
    dom.masterGainVal.textContent = fmtDb(val);
    setUnsaved(true);
    applyMasterGain(false);
  });
  dom.masterGainSlider.addEventListener("dblclick", () => {
    if (!state.profile) return;
    state.profile.track.master_gain_db = 0;
    dom.masterGainSlider.value = "0";
    dom.masterGainVal.textContent = fmtDb(0);
    setUnsaved(true);
    applyMasterGain(false);
  });

  dom.chipBlendSlider.addEventListener("input", () => {
    if (!state.profile) return;
    setChipBlend(parseFloat(dom.chipBlendSlider.value), true);
  });
  dom.chipBlendSlider.addEventListener("dblclick", () => {
    if (!state.profile) return;
    setChipBlend(0.65, true);
  });

  for (const btn of dom.chipPresetBtns) {
    btn.addEventListener("click", () => {
      if (!state.profile) return;
      const val = parseFloat(btn.dataset.blend || "0.65");
      setChipBlend(val, true);
    });
  }

  dom.btnSave.addEventListener("click", saveProfile);
  if (dom.btnDownloadMidi) {
    dom.btnDownloadMidi.addEventListener("click", downloadMidi);
  }
  dom.btnRenderAll.addEventListener("click", renderAllStems);
  dom.btnGenOriginal.addEventListener("click", generateOriginal);
  dom.btnCloseStatus.addEventListener("click", hideStatus);

  dom.btnClearSolo.addEventListener("click", () => {
    if (!state.profile) return;
    for (const [ch, cfg] of Object.entries(state.profile.channels || {})) {
      cfg.solo = false;
      refreshStripStateClasses(ch);
    }
    setUnsaved(true);
    updateAllChannelAudioParams();
  });

  dom.btnUnmuteAll.addEventListener("click", () => {
    if (!state.profile) return;
    for (const [ch, cfg] of Object.entries(state.profile.channels || {})) {
      if (cfg.mode !== "off") {
        cfg.mute = false;
        refreshStripStateClasses(ch);
      }
    }
    setUnsaved(true);
    updateAllChannelAudioParams();
  });

  dom.btnResetEq.addEventListener("click", () => {
    if (!state.profile) return;
    for (const cfg of Object.values(state.profile.channels || {})) {
      cfg.eq_low_db = 0;
      cfg.eq_mid_db = 0;
      cfg.eq_high_db = 0;
    }
    setUnsaved(true);
    renderMixerStrips();
    updateAllChannelAudioParams();
  });

  window.addEventListener("keydown", (ev) => {
    const tag = (ev.target && ev.target.tagName) || "";
    const isInput = tag === "INPUT" || tag === "SELECT" || tag === "TEXTAREA";

    if ((ev.ctrlKey || ev.metaKey) && ev.key.toLowerCase() === "s") {
      ev.preventDefault();
      saveProfile();
      return;
    }
    if (isInput) return;

    if (ev.code === "Space") {
      ev.preventDefault();
      if (state.isPlaying) pausePlayback();
      else startPlayback(state.playbackOffset);
    } else if (ev.key.toLowerCase() === "a") {
      setABMode("remaster");
    } else if (ev.key.toLowerCase() === "b") {
      setABMode("original");
    }
  });

  // Inicialização
  loadTrackList(false).catch((err) => {
    showStatus(`Erro inicializando Mixer Studio: ${err.message}`, "error");
  });
})();
