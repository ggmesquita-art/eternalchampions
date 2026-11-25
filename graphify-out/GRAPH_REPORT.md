# Graph Report - eternalchampions  (2026-11-25)

## Corpus Check
- 117 files · ~857,092 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 131 file(s) not represented in the graph (top: .asm 48, .toml 24, .ecm 17)
- WARNING: 131 of 255 files seen (51%) have no supported extractor and are missing from this graph entirely. It may look complete while representing only part of the codebase.

## Summary
- 2317 nodes · 4672 edges · 184 communities (137 shown, 47 thin omitted)
- Extraction: 96% EXTRACTED · 4% INFERRED · 0% AMBIGUOUS · INFERRED: 194 edges (avg confidence: 0.88)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `f0b2975a`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- upscaler_lab.py
- ec_audio.c
- ec_hd.c
- ec_bplus_gl.c
- tsf
- tsf.h
- app.js
- PROGRESS.md — Estado do projeto EternalRecomp
- remaster_track.py
- mixer_studio.py
- ec_shader_chain.c
- ONNXTensorElementDataType
- chip_ring_to_midi.py
- tsf_u16
- onnxruntime_c_api.h
- ec_harness.py
- tsf_region
- Cenários off-label (Holo-Trainer + Santuário) — Estado da caçada
- main.c
- tsf_voice
- ec_video.c
- review_viewer.py
- collect_dataset.py
- gen_roster_movies.py
- ROADMAP.md — EternalRecomp
- export_onnx.py
- ec_spec.c
- HeadlessPlatform
- comfy_upscale.py
- extract_assets.py
- ec_boss.c
- tsf_midi_synth.c
- GLuint
- export_stems.py
- sys
- OrtEnvCreationOptions
- extract_all_midis.py
- EcBplusMetrics
- subprocess
- tsf_hydra
- OrtTensorRTProviderOptions
- OrtErrorCode
- TASK.md — Fila Executável Ativa (EternalRecomp v2.0)
- tsf_hydra_imod
- OrtCUDAProviderOptions
- PROGRESS.md
- train_espcn.py
- onnxruntime_ep_c_api.h
- tsf_channel
- 4. Guia Completo do Arquivo `settings.ini`
- OrtMIGraphXProviderOptions
- DESIGN.md — Eternal Champions: Recompilação Nativa e Remasterização
- GLuint
- Arquitetura de Áudio e Síntese SoundFont em Tempo Real — Eternal Champions
- docs/main_loop.md — Esqueleto do laço principal (Fase 1, esboço)
- 11. Upscale neural em tempo real (modo B+)
- train_v3
- gen_recomp_config.py
- t2_regdiff.py
- tsf_load
- EcBplusOrtMetrics
- OrtExternalMemoryDescriptor
- GVDP
- 11. Ciclo 2026-11-21: Save/Load V2, Cold Reset, EC jogável, cenários off-label e fim de partida
- Resumo do Harness Determinístico & Frame Data (`M3` / `M4`–`M6`)
- 1. Bloqueadores de gameplay
- Tensor
- struct
- OrtOpAttrType
- ROADMAP.md — EternalRecomp (v2.0)
- 7. Fase 2: Recompilação estática
- 6. Fase 1: Disassembly reconstruível
- tsf_envelope
- tsf_voice_envelope
- OrtOpenVINOProviderOptions
- OrtExternalMemoryHandle
- ONNXType
- OrtExecutionProviderDevicePolicy
- OrtExternalSemaphoreDescriptor
- OrtLanguageProjection
- voice_catalog.py
- 5. Arquitetura descoberta do jogo
- t2_cram3.py
- t2_a6bisect.py
- gen_targets.py
- ec_parse_profile_toml
- json
- Investigação: Movelist do Eternal Champion (RE)
- Movelist — Extração dos Movimentos (RE)
- Proposta — Ciclo de bugfix 2ª rodada (2026-11-21)
- GraphOptimizationLevel
- OrtDeviceEpIncompatibilityReason
- OrtLoggingLevel
- Modo C — Planos HD: Flux × AnimeSharp (decisão G3.2)
- ECStudentESPCN4x
- package.json
- ec_on_post_reset
- Eternal Champions — Disasm, Símbolos & Hooks
- OrtAllocatorType
- OrtCompiledModelCompatibility
- OrtMemoryInfoDeviceType
- OrtMemType
- OrtSparseFormat
- OrtSparseIndicesFormat
- OrtWeightlessSupport
- OrtProfilingEventCategory
- ec_bplus_ort.c
- sanc_overkill_tick
- tsf_channels
- ec_fm_start_note
- tsf_voice_lfo
- Eternal Champions — Build & Harness (Windows nativo)
- OrtCompileApiFlags
- OrtCustomOpInputOutputCharacteristic
- OrtEpContextDataCallbackSupportFlags
- OrtHardwareDeviceType
- OrtEpDataLayout
- bplus_gl_parity.py
- AGENTS.md
- CLAUDE.md
- tsf_channel_note_off
- _FmChan
- ec_video_scanline_lazy
- tsf_hydra_shdr
- 9. Fases 2–5: Recompilação Nativa, Harness, Widescreen e Extrator (2026-11-19)
- ec_bplus_ort_create
- OrtDeviceMemoryType
- 12. Ciclo 2026-11-21 (2ª rodada): fluxo do chefe (T1 ✅), training preto (T2) e sprites do Trident (T4)
- ec_audio.c
- _load_rgb_u8
- Shaders and presets
- libretro/README.md
- OrtResourceCount
- legacy/README.md
- ec_on_post_reset
- train_v3
- argparse
- t4_gen.py
- ec_hd_begin_frame
- tsf_stream_memory
- OrtEp
- OrtKernelImpl
- OrtLoopKernelHelper
- OrtNodeComputeInfo
- ExecutionMode
- OrtGraphCaptureNodeAssignmentPolicy
- OrtNodeFusionOptions
- OrtResourceCountKind
- setup-antigravity-orquestrador.sh
- setup-opus-orquestrador.sh
- cuda-env.sh
- tools/vendor — ferramentas externas locais
- .agents/AGENTS.md
- .agents/agents/executor.md
- explorador.md
- .agents/agents/revisor.md
- OrtEpFactory
- OrtSyncStreamImpl

## God Nodes (most connected - your core abstractions)
1. `tsf` - 61 edges
2. `MixerApp` - 45 edges
3. `tsf_region` - 32 edges
4. `ONNXTensorElementDataType` - 31 edges
5. `NoteEvent` - 30 edges
6. `ApiError` - 24 edges
7. `ec_video_present_gpu()` - 24 edges
8. `tsf_voice` - 20 edges
9. `tsf_hydra` - 19 edges
10. `OrtTensorRTProviderOptions` - 19 edges

## Surprising Connections (you probably didn't know these)
- `0. Pré-requisito / Checkpoint Imediato (Higiene de Git)` --references--> `to_wsl_path()`  [INFERRED]
  task.md → harness/ec_harness.py
- `Etapa G1 — Laboratório Visual de Upscalers Teachers *(Próximo passo imediato — sem alterar código do jogo)*` --references--> `pre_dedither_torch()`  [INFERRED]
  task.md → neural/train_espcn.py
- `Etapa G4 — Otimização GPU (OpenGL Compute & CUDA ORT)` --references--> `ec_video_scanline_lazy()`  [INFERRED]
  task.md → game/ec_video.c
- `Etapa A5 — Revisão do Pilar 3 + Remaster por Instrumento (aprovada e em execução 2026-11-25)` --references--> `ec_audio_reset()`  [INFERRED]
  task.md → game/ec_audio.c
- `Implementação e verificação (`N1`–`N3` / `P3`, 2026-11-19)` --references--> `ECStudentESPCN4x`  [INFERRED]
  docs/phase-4.md → neural/train_espcn.py

## Import Cycles
- None detected.

## Communities (184 total, 47 thin omitted)

### Community 0 - "upscaler_lab.py"
Cohesion: 0.18
Nodes (14): build_boards(), capture_trident(), font(), load_rgb(), main(), make_predither(), now(), run_spandrel() (+6 more)

### Community 1 - "ec_audio.c"
Cohesion: 0.12
Nodes (21): A5.3 — Motor por instrumento e `hybrid` por substituição de canal, A5.4 — Mixer Studio por instrumento, A5 — remaster por instrumento (voices.toml), ec_allpass_tick(), ec_audio_mix_callback(), ec_audio_selftest(), ec_biquad_clear(), ec_biquad_design_highshelf() (+13 more)

### Community 2 - "ec_hd.c"
Cohesion: 0.11
Nodes (31): cram_to_qrgb9(), derive_asset_root(), ec_hd_approved_entries(), ec_hd_destroy_gpu(), ec_hd_load_manifest(), ec_hd_total_entries(), ensure_plane_cpu_loaded(), ensure_sprite_cpu_loaded() (+23 more)

### Community 3 - "ec_bplus_gl.c"
Cohesion: 0.22
Nodes (18): collect_query(), dispatch_conv(), ec_bplus_gl_destroy(), ec_bplus_gl_metrics(), ec_bplus_gl_process(), ec_bplus_gl_readback(), ec_bplus_gl_set_dedither(), ec_bplus_gl_set_intensity() (+10 more)

### Community 4 - "tsf"
Cohesion: 0.10
Nodes (47): tsf, tsf_active_voice_count(), tsf_bank_get_presetname(), tsf_bank_note_off(), tsf_bank_note_on(), tsf_channel_applypitch(), tsf_channel_get_pan(), tsf_channel_get_pitchrange() (+39 more)

### Community 5 - "tsf.h"
Cohesion: 0.09
Nodes (29): tsf_cents2Hertz(), tsf_channel_get_volume(), tsf_channel_setup_voice(), tsf_channel_sounds_off_all(), tsf_decibelsToGain(), tsf_gainToDecibels(), tsf_note_on(), tsf_reset() (+21 more)

### Community 6 - "app.js"
Cohesion: 0.13
Nodes (45): anyChannelSoloed(), apiJson(), applyABGains(), applyChipBlendGains(), applyMasterGain(), channelKindClass(), dbToLin(), ensureAudioContext() (+37 more)

### Community 7 - "PROGRESS.md — Estado do projeto EternalRecomp"
Cohesion: 0.18
Nodes (11): 1. Objetivo e princípios, 2. Infraestrutura, 3. Estado da conversão (Fase 1), 4.1 ares_cli (análise, base do P2), 4.2 gpgx_runner (referência do harness §8), 4.3 REA + Ghidra, 4. Ferramentas dinâmicas, 6. Investigação e arbitragem da divergência ares × GPGX (BlastEm 1.0.0) (+3 more)

### Community 8 - "remaster_track.py"
Cohesion: 0.08
Nodes (35): apply_3band_eq(), apply_master_normalize_and_limit(), apply_pan_and_gain(), _biquad_highshelf(), _biquad_lowshelf(), _biquad_peaking(), _can_use_wsl_fluidsynth(), detect_synth_backend() (+27 more)

### Community 9 - "mixer_studio.py"
Cohesion: 0.05
Nodes (29): 3.2 Contrato: `assets/audio_remaster/profiles/<track>.toml`, 3.3 Catálogo Completo dos 17 MIDIs (`assets/audio_remaster/midi/`) e 18 Perfis (`profiles/`), 3.4 Mixer Studio (`tools/audio/mixer_studio.py` + `tools/audio/mixer_ui/`), 3. Catálogo de MIDIs (`.mid`), Perfis (`.toml`) e Mixer Studio (`tools/audio/`), ApiError, _clamp_float(), _clamp_int(), _clean_voice_name() (+21 more)

### Community 10 - "ec_shader_chain.c"
Cohesion: 0.07
Nodes (53): ec_shader_chain_active(), ec_shader_chain_destroy(), ec_shader_chain_get_bplus(), ec_shader_chain_process(), ec_shader_chain_set_preset_cli(), ec_shader_chain_set_settings(), sc_add_bind(), sc_add_overrides() (+45 more)

### Community 11 - "ONNXTensorElementDataType"
Cohesion: 0.06
Nodes (36): ONNXTensorElementDataType, ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16, ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL, ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX128, ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX64, ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16 (+28 more)

### Community 12 - "chip_ring_to_midi.py"
Cohesion: 0.12
Nodes (20): _build_track_chunk(), convert_chip_ring_to_midi(), _DacBurst, _encode_vlq(), main(), _meta_set_tempo(), _meta_track_name(), NoteEvent (+12 more)

### Community 13 - "tsf_u16"
Cohesion: 0.07
Nodes (30): tsf_hydra_ibag, instGenNdx, instModNdx, tsf_hydra_igen, genAmount, genOper, tsf_hydra_inst, instBagNdx (+22 more)

### Community 14 - "onnxruntime_c_api.h"
Cohesion: 0.06
Nodes (32): OrtAllocator, version, OrtCompileApi, OrtCustomCreateThreadFn, OrtCustomHandleType, __place_holder, OrtCustomJoinThreadFn, OrtCustomOp (+24 more)

### Community 15 - "ec_harness.py"
Cohesion: 0.20
Nodes (16): diff_wram(), expected_tel_ok(), format_md_report(), inspect_widescreen_margins(), load_ram_map(), main(), mask_to_buttons(), parse_ecm() (+8 more)

### Community 16 - "tsf_region"
Cohesion: 0.06
Nodes (32): tsf_region, ampenv, attenuation, delayModLFO, delayVibLFO, end, freqModLFO, freqVibLFO (+24 more)

### Community 17 - "Cenários off-label (Holo-Trainer + Santuário) — Estado da caçada"
Cohesion: 0.18
Nodes (10): Caminho mais promissor (próximo passo), Cenários off-label (Holo-Trainer + Santuário) — Estado da caçada, Fatos estabelecidos (2026-11-21), Menu de Options (VS), Máquina de estados (state_dispatch.asm / state_table.asm), Objetivo, Pistas do commit (red herring parcial), Relacionado (+2 more)

### Community 18 - "main.c"
Cohesion: 0.14
Nodes (10): be32(), crash(), environ_cb(), main(), rotr32(), sha256_block(), sha256_final(), sha256_init() (+2 more)

### Community 19 - "tsf_voice"
Cohesion: 0.07
Nodes (28): tsf_voice, ampenv, heldSustain, loopEnd, loopStart, tsf_voice_lowpass, a0, a1 (+20 more)

### Community 20 - "ec_video.c"
Cohesion: 0.10
Nodes (30): ec_boss_set_enabled(), ec_shader_chain_add_param_cli(), ec_handle_arg(), ec_apply_preset_bplus_once(), ec_bplus_is_enabled(), ec_parse_dedither_mode(), ec_resolve_bplus_backend(), ec_resolve_bplus_budget_ms() (+22 more)

### Community 21 - "review_viewer.py"
Cohesion: 0.18
Nodes (13): layer_category(), main(), make_handler(), do_GET(), do_POST(), _send_bytes(), _send_json(), recompute_summary() (+5 more)

### Community 22 - "collect_dataset.py"
Cohesion: 0.30
Nodes (7): assign_split(), capture_movie(), main(), make_predither(), movie_list(), read_state(), select_frames()

### Community 23 - "gen_roster_movies.py"
Cohesion: 0.18
Nodes (15): add_combat_sequence(), add_p2_combat_sequence(), add_tap(), build_2p_header(), build_2p_match_end_movie(), build_2p_overkill_movie(), build_2p_select(), build_2p_vs_movie() (+7 more)

### Community 24 - "ROADMAP.md — EternalRecomp"
Cohesion: 0.08
Nodes (24): 0.1 Identidade da ROM de referência, 0.2 Ferramentas presentes na pasta, 0.3 Lacunas de toolchain (instalar na Fase 0), 0.4 Progresso da Fase 0 (2026-11-18)- [x] `git init` + `.gitignore` (`roms/`, `assets/`, `disasm/data/`, ferramentas externas), 0. Fatos verificados (2026-11-18), 10. Trilha paralela — Upscale neural (N1 → N3), 11.1 Diagnóstico feito, 11.2 Plano de uso do REA no projeto (+16 more)

### Community 25 - "export_onnx.py"
Cohesion: 0.15
Nodes (11): benchmark_ort_latency(), canonical_tensor_specs(), compute_array_metrics(), create_ort_session(), export_onnx_model(), export_weight_pack(), main(), remove_stale_onnx() (+3 more)

### Community 26 - "ec_spec.c"
Cohesion: 0.12
Nodes (13): Revisão do critério de aceite e diagnóstico de paridade (2026-11-19), ec_audio_cli_number(), ec_call_resume(), ec_call_vblank(), ec_fill_frame_record(), ec_ini_clean_value(), ec_ini_trim(), ec_is_native_entry() (+5 more)

### Community 27 - "HeadlessPlatform"
Cohesion: 0.10
Nodes (7): HeadlessPlatform, cartridgePak, fbH, fbW, frames, systemPak, nall::main()

### Community 28 - "comfy_upscale.py"
Cohesion: 0.15
Nodes (12): Etapa G3 — Modo C Híbrido (Planos HD limpos + Fallback B+ v3), bleed_rgb_into_transparent(), enforce_silhouette(), http_json(), layer_category(), main(), save_manifest_atomic(), select_entries() (+4 more)

### Community 29 - "extract_assets.py"
Cohesion: 0.14
Nodes (10): decode_png_rgba(), decode_4bpp_tile(), decode_cram_shadow(), encode_gray_png(), encode_rgba_png(), extract_capture(), find_compatible_palettes(), indexed_to_rgba_rows() (+2 more)

### Community 30 - "ec_boss.c"
Cohesion: 0.22
Nodes (14): ec_boss_apply_rom_patches(), ec_boss_instruction_hook(), ec_boss_reset(), fighter_slot_from_a5(), is_cpu_sanctuary_boss(), is_playable_ec_slot(), is_vs_offlabel_holo(), is_vs_offlabel_sanctuary() (+6 more)

### Community 31 - "tsf_midi_synth.c"
Cohesion: 0.15
Nodes (16): 3.1 Fluxo de Arquivos e Ferramentas (100% MIDI, Zero WAV em Disco), ec_audio_load_soundfont(), tsf_channel_set_bank_preset(), tsf_close(), tsf_copy(), tsf_load_filename(), tsf_set_max_voices(), tsf_set_output() (+8 more)

### Community 32 - "GLuint"
Cohesion: 0.22
Nodes (11): hd_compile_sh(), hd_link_prog(), sc_compile(), compile_shader(), ec_bplus_gl_create(), init_array_texture(), link_compute_prog(), link_upscale_prog() (+3 more)

### Community 33 - "export_stems.py"
Cohesion: 0.18
Nodes (9): _decode_vlq(), read_midi_notes(), build_default_profile(), _classify_channel(), export_stems(), format_profile_toml(), main(), merge_existing_profile() (+1 more)

### Community 34 - "sys"
Cohesion: 0.16
Nodes (6): events(), scan(), words(), key_of(), main(), parse()

### Community 35 - "OrtEnvCreationOptions"
Cohesion: 0.10
Nodes (17): OrtEnvCreationOptions, config_entries, custom_logging_function, custom_logging_param, log_id, logging_severity_level, threading_options, version (+9 more)

### Community 36 - "extract_all_midis.py"
Cohesion: 0.19
Nodes (8): _boot_to_main_menu(), _build_char_select_script(), _build_music_test_script(), main(), _run_headless(), _ScriptBuilder, _split_dump_by_windows(), _summarize_midi()

### Community 37 - "EcBplusMetrics"
Cohesion: 0.14
Nodes (14): EcBplusMetrics, fallback_frames, frames, input_height, input_width, last_gpu_ms, mean_gpu_ms, output_height (+6 more)

### Community 38 - "subprocess"
Cohesion: 0.25
Nodes (4): main(), time_us(), gen(), run()

### Community 39 - "tsf_hydra"
Cohesion: 0.11
Nodes (19): tsf_hydra, ibagNum, ibags, igenNum, igens, imodNum, imods, instNum (+11 more)

### Community 40 - "OrtTensorRTProviderOptions"
Cohesion: 0.11
Nodes (19): OrtTensorRTProviderOptions, device_id, has_user_compute_stream, trt_dla_core, trt_dla_enable, trt_dump_subgraphs, trt_engine_cache_enable, trt_engine_cache_path (+11 more)

### Community 41 - "OrtErrorCode"
Cohesion: 0.11
Nodes (17): OrtErrorCode, ORT_DEVICE_RESET, ORT_ENGINE_ERROR, ORT_EP_FAIL, ORT_FAIL, ORT_INVALID_ARGUMENT, ORT_INVALID_GRAPH, ORT_INVALID_PROTOBUF (+9 more)

### Community 42 - "TASK.md — Fila Executável Ativa (EternalRecomp v2.0)"
Cohesion: 0.18
Nodes (11): ec_on_state_loaded(), ec_video_reset_ws_state(), 0. Pré-requisito / Checkpoint Imediato (Higiene de Git), 1. Pilar 1 — Aperfeiçoamento da Renderização Gráfica (`Trilha G`), 4. Trilha F — Finalização, Port Windows e Backlog de Baixa Prioridade, 5. Arquivo Histórico — Marcos Concluídos (M0–M10 + Bugfix/Polish), Etapa G1 — Laboratório Visual de Upscalers Teachers *(Próximo passo imediato — sem alterar código do jogo)*, Etapa G2 — Modo B+ v3 Full-Frame Distilled (100% da tela uniforme + 60 fps) (+3 more)

### Community 43 - "tsf_hydra_imod"
Cohesion: 0.12
Nodes (16): tsf_hydra_genamount, range, shortAmount, wordAmount, tsf_hydra_imod, modAmount, modAmtSrcOper, modDestOper (+8 more)

### Community 44 - "OrtCUDAProviderOptions"
Cohesion: 0.07
Nodes (29): OrtCUDAProviderOptions, arena_extend_strategy, cudnn_conv_algo_search, default_memory_arena_cfg, device_id, do_copy_in_default_stream, gpu_mem_limit, has_user_compute_stream (+21 more)

### Community 46 - "train_espcn.py"
Cohesion: 0.27
Nodes (9): alpha_over(), build_composite_eval_batches(), build_eval_batches(), composite_scene_patch(), extract_patch(), load_pair_tensors(), main(), sample_composite_batch() (+1 more)

### Community 48 - "onnxruntime_ep_c_api.h"
Cohesion: 0.13
Nodes (13): CreateEpApiFactoriesFn, OrtDataTransferImpl, ort_version_supported, OrtEpApi, OrtEpProfilerImpl, ort_version_supported, OrtExternalResourceImporterImpl, ort_version_supported (+5 more)

### Community 50 - "tsf_channel"
Cohesion: 0.14
Nodes (14): tsf_channel, bank, gainDB, midiData, midiExpression, midiPan, midiRPN, midiVolume (+6 more)

### Community 51 - "4. Guia Completo do Arquivo `settings.ini`"
Cohesion: 0.06
Nodes (30): 1. Visão Geral dos Binários, 2.1 Variáveis SDL2 (Vídeo, Renderizador e Áudio), 2.2 Variáveis do Runtime Base (*GenesisRecomp*), 2.3 Variáveis Específicas de Eternal Champions (*EC*), 2. Variáveis de Ambiente, 3.1 Ajuda e Informações Gerais, 3.2 Vídeo, Janela e Widescreen, 3.3.1 Shaders (RetroArch `.glslp`) (+22 more)

### Community 52 - "OrtMIGraphXProviderOptions"
Cohesion: 0.14
Nodes (14): OrtMIGraphXProviderOptions, device_id, migraphx_arena_extend_strategy, migraphx_exhaustive_tune, migraphx_fp16_enable, migraphx_fp8_enable, migraphx_int8_calibration_table_name, migraphx_int8_enable (+6 more)

### Community 53 - "DESIGN.md — Eternal Champions: Recompilação Nativa e Remasterização"
Cohesion: 0.15
Nodes (13): 16. Marcos e critérios de aceite, 17. Riscos, 18. Questões em aberto, 1.1 Objetivos, 1.2 Não objetivos, 1. Visão geral, 2. Princípios de engenharia, 3. Escopo de uso (+5 more)

### Community 54 - "GLuint"
Cohesion: 0.25
Nodes (11): ec_hd_composite_gpu(), ensure_plane_gl_tex(), ensure_sprite_gl_tex(), hd_gpu_create(), hd_gpu_ensure_size(), hd_load_gl(), hd_state_begin(), hd_state_end() (+3 more)

### Community 55 - "Arquitetura de Áudio e Síntese SoundFont em Tempo Real — Eternal Champions"
Cohesion: 0.11
Nodes (15): 1.1 Chips de Áudio e Canais Físicos, 1.2 Driver Z80 (`$000A80`), 1.3 Interface 68000 → Z80 (`disasm/code/sound_cmd.asm`), 1. Subsistema de Som Original (Hardware + Driver Z80), 2.1 O Problema na Versão Anterior de `tools/audio/chip_ring_to_midi.py`, 2.2 Correção no Novo Pipeline por Canal, 2. Causa-Raiz do Falso-Positivo de Percussão em `03_syria_blade`, 4.1 Pontos de Integração no Runner (+7 more)

### Community 56 - "docs/main_loop.md — Esqueleto do laço principal (Fase 1, esboço)"
Cohesion: 0.15
Nodes (12): Cadeia de input → menu (decodificada, 2026-11-18), docs/main_loop.md — Esqueleto do laço principal (Fase 1, esboço), Entry points, Flags e estado (RAM $FF0000+, todas hipóteses), Fluxo de um quadro (FUN_0000fd24), Handler do estado 9 (menu-init, $3256 — convertido, verificado), Handler do menu (estado 13, $36B2 — analisado por disassembly, 2026-11-18), Mapa de estados (GPGX = referência; ares concorda sem input) (+4 more)

### Community 57 - "11. Upscale neural em tempo real (modo B+)"
Cohesion: 0.15
Nodes (13): 11.1 Objetivo, 11.2 Destilação professor → aluno, 11.3 Inferência por camada, 11.4 Orçamento de desempenho, 11.5 Runtime de inferência, 11.6 Caminho de implementação (independente da recompilação), 11. Upscale neural em tempo real (modo B+), 9.1 Modo C: resolução de assets (+5 more)

### Community 58 - "train_v3"
Cohesion: 0.32
Nodes (4): amp_ctx(), benchmark_gpu_latency(), make_lpips(), V3Loss

### Community 59 - "gen_recomp_config.py"
Cohesion: 0.23
Nodes (8): 10.5 Captura e remaster de áudio (2026-11-20, preparação), 10. Estado de Pendências e Bloqueios Reais, build_cfg_and_leads(), add_work(), format_hex_array(), in_code_regions(), is_valid_code_start(), main()

### Community 60 - "t2_regdiff.py"
Cohesion: 0.60
Nodes (4): connect(), frame_info(), main(), rpc()

### Community 61 - "tsf_load"
Cohesion: 0.22
Nodes (11): tsf_decode_ogg(), tsf_decode_sf3_samples(), tsf_load(), tsf_load_memory(), tsf_load_presets(), tsf_load_samples(), tsf_region_clear(), tsf_region_envtosecs() (+3 more)

### Community 62 - "EcBplusOrtMetrics"
Cohesion: 0.14
Nodes (14): EcBplusOrtMetrics, fallback_frames, frames, input_height, input_width, last_gpu_ms, mean_gpu_ms, output_height (+6 more)

### Community 63 - "OrtExternalMemoryDescriptor"
Cohesion: 0.17
Nodes (12): OrtExternalMemoryDescriptor, handle_type, native_handle, offset_bytes, size_bytes, version, OrtExternalMemoryHandleType, ORT_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP (+4 more)

### Community 64 - "GVDP"
Cohesion: 0.27
Nodes (13): ec_hd_frame_active(), ec_hd_record_pixel(), evaluate_4x3_frame(), hd_vram16(), ec_draw_sprite_span(), ec_eval_full_sprites_line(), ec_fetch_plane_pixel(), ec_fetch_window_pixel() (+5 more)

### Community 66 - "11. Ciclo 2026-11-21: Save/Load V2, Cold Reset, EC jogável, cenários off-label e fim de partida"
Cohesion: 0.20
Nodes (10): 11.1 Save/load V2 (`ECRB2sav`) e reset frio (F10), 11.2 Cenários off-label no VS 2P (Holo-Trainer + Santuário) — CONCLUÍDO, 11.3 Fim de partida nos estados off-label — CONCLUÍDO, 11.4 EC jogável e sprites ilimitados, 11.5 Arquitetura da máquina de estados mapeada (neste ciclo), 11.6 Limpeza e metodologia, 11. Ciclo 2026-11-21: Save/Load V2, Cold Reset, EC jogável, cenários off-label e fim de partida, ec_boss_rb_state_load() (+2 more)

### Community 67 - "Resumo do Harness Determinístico & Frame Data (`M3` / `M4`–`M6`)"
Cohesion: 0.22
Nodes (9): `01_attract_boot.ecm` (1000 quadros) — `byte-swapped-16bit (desfeito via i^1)`, `02_title_to_fight.ecm` (2900 quadros) — `byte-swapped-16bit (desfeito via i^1)`, 1. Autodeterminismo Nativo e Misses (Suíte Completa), 2. Paridade da Recompilação Estática (`Recomp` × `GENESIS_FORCE_INTERP=1`), 3. Cobertura do Elenco e Arenas + Paridade Native vs GPGX, 4. Progressão de Luta Ativa Observada (`02_title_to_fight.ecm`), 5. Validação Widescreen 16:9 (`game/ec_video.c` — Todas as Arenas), Checkpoints Detalhados — Boot (`01`) e Luta Principal (`02`) (+1 more)

### Community 68 - "1. Bloqueadores de gameplay"
Cohesion: 0.20
Nodes (10): 1. Bloqueadores de gameplay, [~] 1º load do savegame às vezes trava (carregar 2–3× libera), 2. Bugs já resolvidos (rastreabilidade), 3. Limitações conhecidas (não são bugs do jogo), 4. Pendências de escopo (não-bugs), BUG_QUEUE.md — Fila restante de bugs e pendências, [ ] Finalizador da Jetta — bug reportado, a reproduzir, [~] Sprites do EC falham nos DOIS ECs — só no round 1 do Trident (+2 more)

### Community 70 - "Tensor"
Cohesion: 0.27
Nodes (4): compute_ssim(), compute_ssim_tensor(), sample_v3_batch(), sobel_edges()

### Community 71 - "struct"
Cohesion: 0.33
Nodes (8): detect_gpgx_byte_order(), read_be_s16(), read_be_s32(), read_be_u16(), read_be_u32(), read_telemetry(), fighter(), unswap_16bit_words()

### Community 72 - "OrtOpAttrType"
Cohesion: 0.20
Nodes (10): OrtOpAttrType, ORT_OP_ATTR_FLOAT, ORT_OP_ATTR_FLOATS, ORT_OP_ATTR_GRAPH, ORT_OP_ATTR_INT, ORT_OP_ATTR_INTS, ORT_OP_ATTR_STRING, ORT_OP_ATTR_STRINGS (+2 more)

### Community 73 - "ROADMAP.md — EternalRecomp (v2.0)"
Cohesion: 0.20
Nodes (10): 1. Linha de Base Consolidada (O que já está pronto e validado), 2. Visão Geral das Frentes Prioritárias (v2.0), 3. Detalhamento dos 3 Pilares Prioritários, 4. Trilha F — Finalização, Port Windows e Backlog de Baixa Prioridade, 5. Plano de Orquestração Paralela (Orca DAG), Invariantes inegociáveis do projeto, PILAR 1 — Trilha G: Aperfeiçoamento da Renderização Gráfica, PILAR 2 — Trilha S: Implementação de Suporte a Shaders Modulares (+2 more)

### Community 74 - "7. Fase 2: Recompilação estática"
Cohesion: 0.22
Nodes (9): 12. Áudio, 13. Input, 15. Stack e dependências, 7.1 Modelo de execução, 7.2 Interpretador de fallback, 7.3 Tempo e interrupções, 7.4 Overrides manuais, 7. Fase 2: Recompilação estática (+1 more)

### Community 75 - "6. Fase 1: Disassembly reconstruível"
Cohesion: 0.29
Nodes (7): 14. Uso de LLMs, 6.1 Método, 6.2 Ferramentas, 6.3 Convenções, 6.4 Prioridades de mapeamento, 6. Fase 1: Disassembly reconstruível, Fase 1 — Disassembly Reconstruível (`M1`)

### Community 76 - "tsf_envelope"
Cohesion: 0.22
Nodes (9): tsf_envelope, attack, decay, delay, hold, keynumToDecay, keynumToHold, release (+1 more)

### Community 77 - "tsf_voice_envelope"
Cohesion: 0.22
Nodes (9): tsf_voice_envelope, isAmpEnv, level, midiVelocity, parameters, samplesUntilNextSegment, segment, segmentIsExponential (+1 more)

### Community 78 - "OrtOpenVINOProviderOptions"
Cohesion: 0.22
Nodes (9): OrtOpenVINOProviderOptions, cache_dir, context, device_id, device_type, enable_dynamic_shapes, enable_npu_fast_compile, enable_opencl_throttling (+1 more)

### Community 79 - "OrtExternalMemoryHandle"
Cohesion: 0.22
Nodes (8): OrtExternalMemoryHandle, descriptor, ep_device, version, OrtExternalSemaphoreHandle, descriptor, ep_device, version

### Community 80 - "ONNXType"
Cohesion: 0.25
Nodes (8): ONNXType, ONNX_TYPE_MAP, ONNX_TYPE_OPAQUE, ONNX_TYPE_OPTIONAL, ONNX_TYPE_SEQUENCE, ONNX_TYPE_SPARSETENSOR, ONNX_TYPE_TENSOR, ONNX_TYPE_UNKNOWN

### Community 81 - "OrtExecutionProviderDevicePolicy"
Cohesion: 0.25
Nodes (8): OrtExecutionProviderDevicePolicy, OrtExecutionProviderDevicePolicy_DEFAULT, OrtExecutionProviderDevicePolicy_MAX_EFFICIENCY, OrtExecutionProviderDevicePolicy_MAX_PERFORMANCE, OrtExecutionProviderDevicePolicy_MIN_OVERALL_POWER, OrtExecutionProviderDevicePolicy_PREFER_CPU, OrtExecutionProviderDevicePolicy_PREFER_GPU, OrtExecutionProviderDevicePolicy_PREFER_NPU

### Community 82 - "OrtExternalSemaphoreDescriptor"
Cohesion: 0.25
Nodes (8): OrtExternalSemaphoreDescriptor, native_handle, type, version, OrtExternalSemaphoreType, ORT_EXTERNAL_SEMAPHORE_D3D12_FENCE, ORT_EXTERNAL_SEMAPHORE_VK_TIMELINE_SEMAPHORE_OPAQUE_FD, ORT_EXTERNAL_SEMAPHORE_VK_TIMELINE_SEMAPHORE_WIN32

### Community 83 - "OrtLanguageProjection"
Cohesion: 0.25
Nodes (8): OrtLanguageProjection, ORT_PROJECTION_C, ORT_PROJECTION_CPLUSPLUS, ORT_PROJECTION_CSHARP, ORT_PROJECTION_JAVA, ORT_PROJECTION_NODEJS, ORT_PROJECTION_PYTHON, ORT_PROJECTION_WINML

### Community 84 - "voice_catalog.py"
Cohesion: 0.13
Nodes (19): build_voice_entries(), decode_patch(), extract_voice_notes(), emit(), _fmt(), fnv1a32(), _load_events(), main() (+11 more)

### Community 85 - "5. Arquitetura descoberta do jogo"
Cohesion: 0.22
Nodes (9): 5.1 Fluxo de boot, 5.2 Máquina de estados (tabela em `$27B4`, 40 handlers), 5.3 Laço principal e subsistemas, 5.4 Subsistema de input, 5.5 Escalonador cooperativo, 5.6 Estrutura de lutador e estado da luta (`disasm/symbols/ram_map.toml`), 5.7 HBlank / Raster e Scroll (DESIGN §18.3), 5.8 Tabelas mestras, IA dos lutadores e Replay/Estatísticas (+1 more)

### Community 86 - "t2_cram3.py"
Cohesion: 0.54
Nodes (6): connect(), dump(), fi(), main(), pause_at(), rpc()

### Community 87 - "t2_a6bisect.py"
Cohesion: 0.57
Nodes (5): a6(), connect(), fi(), main(), rpc()

### Community 88 - "gen_targets.py"
Cohesion: 0.52
Nodes (5): check(), gen(), load_manifest(), main(), save_manifest()

### Community 89 - "ec_parse_profile_toml"
Cohesion: 0.15
Nodes (21): ec_audio_load_profiles_dir(), ec_clamp_i(), ec_ends_with_ci(), ec_find_channel_index(), ec_find_profile_for_bgm(), ec_find_voice(), ec_init_builtin_default_profile(), ec_load_voices_file() (+13 more)

### Community 91 - "json"
Cohesion: 0.11
Nodes (29): connect(), fi(), main(), snap(), rd(), rpc(), connect(), fi() (+21 more)

### Community 92 - "Investigação: Movelist do Eternal Champion (RE)"
Cohesion: 0.33
Nodes (5): Fatos estabelecidos, Investigação: Movelist do Eternal Champion (RE), Perguntas abertas, Plano, Pontos de entrada identificados

### Community 93 - "Movelist — Extração dos Movimentos (RE)"
Cohesion: 0.33
Nodes (5): Formato bruto (por arquivo de personagem), Metodologia, Movelist — Extração dos Movimentos (RE), Pontos de entrada (ROM), Progresso

### Community 94 - "Proposta — Ciclo de bugfix 2ª rodada (2026-11-21)"
Cohesion: 0.22
Nodes (9): Critérios de aceite gerais, Ordem sugerida e dependências, Panorama dos fluxos relevantes (fatos verificados até aqui), Proposta — Ciclo de bugfix 2ª rodada (2026-11-21), T1 — Cenário do EC: volta ao menu inicial após o 1º round — ✅ RESOLVIDO (2026-11-21), T2 — Training: tela preta ao terminar (após 1 round) — ✅ RESOLVIDO (2026-11-21), T3 — Savegame: 1º load às vezes trava (2–3 loads "liberam") — 🔧 FIX APLICADO (2026-11-21), T4 — Sprites do EC quebrados só no round 1 do Trident — 🔬 OBSERVAÇÃO PARCIAL (2026-11-21) (+1 more)

### Community 95 - "GraphOptimizationLevel"
Cohesion: 0.33
Nodes (6): GraphOptimizationLevel, ORT_DISABLE_ALL, ORT_ENABLE_ALL, ORT_ENABLE_BASIC, ORT_ENABLE_EXTENDED, ORT_ENABLE_LAYOUT

### Community 96 - "OrtDeviceEpIncompatibilityReason"
Cohesion: 0.33
Nodes (6): OrtDeviceEpIncompatibilityReason, OrtDeviceEpIncompatibility_DEVICE_INCOMPATIBLE, OrtDeviceEpIncompatibility_DRIVER_INCOMPATIBLE, OrtDeviceEpIncompatibility_MISSING_DEPENDENCY, OrtDeviceEpIncompatibility_NONE, OrtDeviceEpIncompatibility_UNKNOWN

### Community 97 - "OrtLoggingLevel"
Cohesion: 0.33
Nodes (6): OrtLoggingLevel, ORT_LOGGING_LEVEL_ERROR, ORT_LOGGING_LEVEL_FATAL, ORT_LOGGING_LEVEL_INFO, ORT_LOGGING_LEVEL_VERBOSE, ORT_LOGGING_LEVEL_WARNING

### Community 98 - "Modo C — Planos HD: Flux × AnimeSharp (decisão G3.2)"
Cohesion: 0.29
Nodes (6): 1. Os dois conjuntos de planos, 2. Por que não o Flux, 3. Como usar cada conjunto, 4. Correções no compositor (`game/ec_hd.c`) feitas junto, 5. Preservação dos arquivos, Modo C — Planos HD: Flux × AnimeSharp (decisão G3.2)

### Community 99 - "ECStudentESPCN4x"
Cohesion: 0.14
Nodes (9): 10.1 Extração, 10.2 Upscale, 10.3 Revisão, 10. Fase 5: Pipeline de assets HD, Execução e estado atual (`M7.1`, 2026-11-19), Fase 5 — Pipeline de Assets HD (`M7`), Modos extras (`M7.3`, 2026-11-19), ECStudentESPCN4x (+1 more)

### Community 100 - "package.json"
Cohesion: 0.40
Nodes (5): allowScripts, node@26.13.1, dependencies, node@^26.13.1, node

### Community 101 - "ec_on_post_reset"
Cohesion: 0.25
Nodes (16): 4.2 Fluxo de Duas Camadas em `game/ec_audio.{h,c}`, ec_apply_active_profile(), ec_apply_chip_mask(), ec_audio_init(), ec_audio_on_bgm_pause(), ec_audio_on_bgm_play(), ec_audio_on_bgm_stop(), ec_audio_on_state_loaded() (+8 more)

### Community 102 - "Eternal Champions — Disasm, Símbolos & Hooks"
Cohesion: 0.29
Nodes (6): 0. Grafo primeiro, 1. Nunca leia arquivos grandes inteiros, 2. Onde vive cada informação, 3. Convenção de busca de endereços, 4. Fins de linha, Eternal Champions — Disasm, Símbolos & Hooks

### Community 103 - "OrtAllocatorType"
Cohesion: 0.40
Nodes (5): OrtAllocatorType, OrtArenaAllocator, OrtDeviceAllocator, OrtInvalidAllocator, OrtReadOnlyAllocator

### Community 104 - "OrtCompiledModelCompatibility"
Cohesion: 0.40
Nodes (5): OrtCompiledModelCompatibility, OrtCompiledModelCompatibility_EP_NOT_APPLICABLE, OrtCompiledModelCompatibility_EP_SUPPORTED_OPTIMAL, OrtCompiledModelCompatibility_EP_SUPPORTED_PREFER_RECOMPILATION, OrtCompiledModelCompatibility_EP_UNSUPPORTED

### Community 105 - "OrtMemoryInfoDeviceType"
Cohesion: 0.40
Nodes (5): OrtMemoryInfoDeviceType, OrtMemoryInfoDeviceType_CPU, OrtMemoryInfoDeviceType_FPGA, OrtMemoryInfoDeviceType_GPU, OrtMemoryInfoDeviceType_NPU

### Community 106 - "OrtMemType"
Cohesion: 0.40
Nodes (5): OrtMemType, OrtMemTypeCPU, OrtMemTypeCPUInput, OrtMemTypeCPUOutput, OrtMemTypeDefault

### Community 107 - "OrtSparseFormat"
Cohesion: 0.40
Nodes (5): OrtSparseFormat, ORT_SPARSE_BLOCK_SPARSE, ORT_SPARSE_COO, ORT_SPARSE_CSRC, ORT_SPARSE_UNDEFINED

### Community 108 - "OrtSparseIndicesFormat"
Cohesion: 0.40
Nodes (5): OrtSparseIndicesFormat, ORT_SPARSE_BLOCK_SPARSE_INDICES, ORT_SPARSE_COO_INDICES, ORT_SPARSE_CSR_INNER_INDICES, ORT_SPARSE_CSR_OUTER_INDICES

### Community 109 - "OrtWeightlessSupport"
Cohesion: 0.40
Nodes (5): OrtWeightlessSupport, OrtWeightlessSupport_ALL, OrtWeightlessSupport_ALL_OR_EXTERNAL_ONLY, OrtWeightlessSupport_EXTERNAL_ONLY, OrtWeightlessSupport_NONE

### Community 110 - "OrtProfilingEventCategory"
Cohesion: 0.40
Nodes (5): OrtProfilingEventCategory, OrtProfilingEventCategory_API, OrtProfilingEventCategory_KERNEL, OrtProfilingEventCategory_NODE, OrtProfilingEventCategory_SESSION

### Community 111 - "ec_bplus_ort.c"
Cohesion: 0.12
Nodes (26): ec_video_present_gpu_destroy(), ec_bplus_ort_create(), ec_bplus_ort_destroy(), ec_bplus_ort_metrics(), ec_bplus_ort_process(), ec_bplus_ort_readback(), ec_bplus_ort_set_intensity(), EcBplusOrt (+18 more)

### Community 112 - "sanc_overkill_tick"
Cohesion: 0.48
Nodes (7): call_68k_pal(), call_68k_sfx(), call_68k_sub(), ram16_write(), sanc_apply_window_hole(), sanc_overkill_tick(), write_pal16()

### Community 113 - "tsf_channels"
Cohesion: 0.50
Nodes (4): tsf_channels, activeChannel, channelNum, channels

### Community 114 - "ec_fm_start_note"
Cohesion: 0.31
Nodes (13): ec_clampf(), ec_fm_carrier_min_tl(), ec_fm_set_vol(), ec_fm_start_note(), ec_fm_tl_update(), ec_process_fm_event(), ec_tl_gain(), ec_tsf_set_bank_preset_safe() (+5 more)

### Community 115 - "tsf_voice_lfo"
Cohesion: 0.50
Nodes (4): tsf_voice_lfo, delta, level, samplesUntil

### Community 116 - "Eternal Champions — Build & Harness (Windows nativo)"
Cohesion: 0.33
Nodes (5): 1. Build nativo (`EternalChampionsRecomp.exe`), 2. Execução manual, 3. Validação determinística (`harness/ec_harness.py`), 4. Python / GPU (treino, upscalers, ferramentas), Eternal Champions — Build & Harness (Windows nativo)

### Community 117 - "OrtCompileApiFlags"
Cohesion: 0.50
Nodes (4): OrtCompileApiFlags, OrtCompileApiFlags_ERROR_IF_NO_NODES_COMPILED, OrtCompileApiFlags_ERROR_IF_OUTPUT_FILE_EXISTS, OrtCompileApiFlags_NONE

### Community 118 - "OrtCustomOpInputOutputCharacteristic"
Cohesion: 0.50
Nodes (4): OrtCustomOpInputOutputCharacteristic, INPUT_OUTPUT_OPTIONAL, INPUT_OUTPUT_REQUIRED, INPUT_OUTPUT_VARIADIC

### Community 119 - "OrtEpContextDataCallbackSupportFlags"
Cohesion: 0.50
Nodes (4): OrtEpContextDataCallbackSupportFlags, OrtEpContextDataCallbackSupportFlags_NONE, OrtEpContextDataCallbackSupportFlags_READ, OrtEpContextDataCallbackSupportFlags_WRITE

### Community 120 - "OrtHardwareDeviceType"
Cohesion: 0.50
Nodes (4): OrtHardwareDeviceType, OrtHardwareDeviceType_CPU, OrtHardwareDeviceType_GPU, OrtHardwareDeviceType_NPU

### Community 121 - "OrtEpDataLayout"
Cohesion: 0.50
Nodes (4): OrtEpDataLayout, OrtEpDataLayout_Default, OrtEpDataLayout_NCHW, OrtEpDataLayout_NHWC

### Community 123 - "bplus_gl_parity.py"
Cohesion: 0.22
Nodes (6): pre_dedither_torch(), main(), make_script(), metrics(), run_capture(), torch_reference()

### Community 126 - "tsf_channel_note_off"
Cohesion: 0.29
Nodes (11): ec_audio_on_chip_write(), ec_can_emit_notes(), ec_process_chip_event(), ec_process_psg_event(), ec_psg_calc_raw_note(), ec_psg_handle_noise_vol(), ec_psg_handle_tone_vol(), ec_psg_noise_on() (+3 more)

### Community 127 - "_FmChan"
Cohesion: 0.32
Nodes (3): _FmChan, fnum_to_midi(), mul_to_factor()

### Community 128 - "ec_video_scanline_lazy"
Cohesion: 0.16
Nodes (20): 1. Princípio: 320 px na lógica, margem estendida só no renderer, 2. Política por Estado (`$FFFFFD64`), 3. Reposicionamento de HUD e Modo C, 4. Flags CLI e Resultados Medidos, 5. GW.3 — Flicker lateral e sprites descartados nas margens (causa-raiz e correção), Widescreen 16:9 / Adaptativo (`M4`–`M6`), ec_compute_viewport_x0(), ec_is_arena_state() (+12 more)

### Community 129 - "tsf_hydra_shdr"
Cohesion: 0.12
Nodes (14): tsf_hydra_shdr, end, endLoop, originalPitch, pitchCorrection, sampleLink, sampleName, sampleRate (+6 more)

### Community 135 - "9. Fases 2–5: Recompilação Nativa, Harness, Widescreen e Extrator (2026-11-19)"
Cohesion: 0.33
Nodes (6): 9.2 Harness determinístico `.ecm` (`M3`) — `harness/ec_harness.py`, 9.3 Renderer `GameVideo`, Widescreen 16:9 e Modo C (`M4`–`M6`) — `docs/widescreen.md`, 9.4 Extrator, Pipeline ComfyUI HD (`M7`) e Destilação Neural B+ (`N1`–`N2`), 9.5 Backend CUDA/ONNX Runtime do Modo B+ (`P3` alternativo, 2026-11-20), 9.6 Remaster generativo Flux dos 437 assets (`M7.4`, 2026-11-20) — CONCLUÍDO, 9. Fases 2–5: Recompilação Nativa, Harness, Widescreen e Extrator (2026-11-19)

### Community 136 - "ec_bplus_ort_create"
Cohesion: 0.33
Nodes (5): ort_release_status(), ort_status_msg(), OrtApi, ORT_ALL_ARGS_NONNULL, OrtApiBase

### Community 137 - "OrtDeviceMemoryType"
Cohesion: 0.67
Nodes (3): OrtDeviceMemoryType, OrtDeviceMemoryType_DEFAULT, OrtDeviceMemoryType_HOST_ACCESSIBLE

### Community 138 - "12. Ciclo 2026-11-21 (2ª rodada): fluxo do chefe (T1 ✅), training preto (T2) e sprites do Trident (T4)"
Cohesion: 0.40
Nodes (5): 12.1 T1 — Cenário do EC voltava ao menu após o 1º round — RESOLVIDO, 12.2 T2 — Training (Holo off-label) termina em tela preta — ✅ RESOLVIDO (2026-11-21), 12.3 T4 — Sprites do EC no round 1 do Trident — REPRO EM CONSTRUÇÃO, 12.4 T3 — 1º load do savegame — STAGING INTERATIVO IMPLEMENTADO (2026-11-21), 12. Ciclo 2026-11-21 (2ª rodada): fluxo do chefe (T1 ✅), training preto (T2) e sprites do Trident (T4)

### Community 139 - "ec_audio.c"
Cohesion: 0.23
Nodes (14): ec_audio_fm_patch_hash(), ec_audio_set_bgm_vol(), ec_audio_set_chip_blend(), ec_audio_set_chip_blend_override(), ec_audio_set_dsp_reverb(), ec_audio_set_sfx_vol(), ec_float_to_s16(), ec_fm_patch_hash() (+6 more)

### Community 145 - "OrtResourceCount"
Cohesion: 0.33
Nodes (6): FromTotalBytes(), None(), OrtResourceCount, kind, reserved, value

### Community 154 - "ec_on_post_reset"
Cohesion: 0.17
Nodes (12): 9.1 Recompilação estática (`M2`) — `build/native/EternalChampionsRecomp`, ec_audio_get_mode(), ec_audio_load_voices(), ec_audio_set_chip_mask_hook(), ec_call_entry_point(), ec_on_post_reset(), 3. Pilar 3 — Remasterização da Trilha Sonora (`Trilha A`), Etapa A1 — Auditoria FM/PSG, Correção de Blade, Stems & Motor de Remasterização (`Task A1-pipeline`) (+4 more)

### Community 155 - "train_v3"
Cohesion: 0.21
Nodes (5): eval_v3(), rgb_to_rgba(), run_infer(), select_v3_frames(), train_v3()

### Community 156 - "argparse"
Cohesion: 0.10
Nodes (10): main(), load_labels(), main(), main(), parse(), main(), region_list(), main() (+2 more)

### Community 158 - "ec_hd_begin_frame"
Cohesion: 0.50
Nodes (5): ec_hd_begin_frame(), ec_hd_get_mode(), ec_hd_is_active(), ec_hd_set_mode_str(), hd_parse_mode()

### Community 159 - "tsf_stream_memory"
Cohesion: 0.50
Nodes (4): tsf_stream_memory, buffer, pos, total

### Community 161 - "OrtKernelImpl"
Cohesion: 0.67
Nodes (3): OrtKernelImpl, flags, ort_version_supported

### Community 265 - "ExecutionMode"
Cohesion: 0.67
Nodes (3): ExecutionMode, ORT_PARALLEL, ORT_SEQUENTIAL

### Community 267 - "OrtGraphCaptureNodeAssignmentPolicy"
Cohesion: 0.67
Nodes (3): OrtGraphCaptureNodeAssignmentPolicy, OrtGraphCaptureNodeAssignmentPolicy_ALL_NODES_ON_EP, OrtGraphCaptureNodeAssignmentPolicy_ALLOW_CPU_FOR_SHAPES

### Community 269 - "OrtNodeFusionOptions"
Cohesion: 0.67
Nodes (3): OrtNodeFusionOptions, drop_constant_initializers, ort_version_supported

### Community 270 - "OrtResourceCountKind"
Cohesion: 0.67
Nodes (3): OrtResourceCountKind, OrtResourceCountKind_None, OrtResourceCountKind_TotalBytes

## Knowledge Gaps
- **488 isolated node(s):** `OrtArenaAllocator`, `OrtDeviceAllocator`, `OrtInvalidAllocator`, `OrtReadOnlyAllocator`, `OrtCompiledModelCompatibility_EP_NOT_APPLICABLE` (+483 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 1015 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **47 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `tsf_region` connect `tsf_region` to `tsf.h`?**
  _High betweenness centrality (0.030) - this node is a cross-community bridge._
- **Are the 2 inferred relationships involving `MixerApp` (e.g. with `3.4 Mixer Studio (`tools/audio/mixer_studio.py` + `tools/audio/mixer_ui/`)` and `NoteEvent`) actually correct?**
  _`MixerApp` has 2 INFERRED edges - model-reasoned connections that need verification._
- **What connects `OrtArenaAllocator`, `OrtDeviceAllocator`, `OrtInvalidAllocator` to the rest of the system?**
  _488 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `ec_audio.c` be split into smaller, more focused modules?**
  _Cohesion score 0.12 - nodes in this community are weakly interconnected._
- **Why does `ONNXTensorElementDataType` connect `ONNXTensorElementDataType` to `onnxruntime_c_api.h`?**
  _High betweenness centrality (0.029) - this node is a cross-community bridge._
- **Should `ec_hd.c` be split into smaller, more focused modules?**
  _Cohesion score 0.112375533428165 - nodes in this community are weakly interconnected._
- **Why does `EcBplusMetrics` connect `EcBplusMetrics` to `ec_bplus_gl.h`, `ec_bplus_gl.c`?**
  _High betweenness centrality (0.023) - this node is a cross-community bridge._