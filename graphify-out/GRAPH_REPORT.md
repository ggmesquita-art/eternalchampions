# Graph Report - eternalchampions  (2026-11-25)

## Corpus Check
- 145 files · ~879,736 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 131 file(s) not represented in the graph (top: .asm 48, .toml 24, .ecm 17)
- WARNING: 131 of 284 files seen (46%) have no supported extractor and are missing from this graph entirely. It may look complete while representing only part of the codebase.

## Summary
- 2887 nodes · 5933 edges · 195 communities (145 shown, 50 thin omitted)
- Extraction: 93% EXTRACTED · 7% INFERRED · 0% AMBIGUOUS · INFERRED: 406 edges (avg confidence: 0.87)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `22522ef1`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- App
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
- tsf_hydra_shdr
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
- extract_ui_assets.py
- export_stems.py
- sys
- OrtEnvCreationOptions
- extract_all_midis.py
- draw_text
- subprocess
- tsf_hydra
- OrtTensorRTProviderOptions
- OrtErrorCode
- Render
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
- argparse
- PLAN.md — Launcher do Eternal Champions Recomp ("EC Launcher")
- EcBplusOrtMetrics
- OrtExternalMemoryDescriptor
- GVDP
- string
- Resumo do Harness Determinístico & Frame Data (`M3` / `M4`–`M6`)
- 1. Bloqueadores de gameplay
- Tensor
- InputState
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
- Input
- OrtLanguageProjection
- voice_catalog.py
- OrtROCMProviderOptions
- t2_cram3.py
- t2_a6bisect.py
- gen_targets.py
- ec_parse_profile_toml
- src/main.cpp
- Investigação: Movelist do Eternal Champion (RE)
- Movelist — Extração dos Movimentos (RE)
- Proposta — Ciclo de bugfix 2ª rodada (2026-11-21)
- GraphOptimizationLevel
- OrtDeviceEpIncompatibilityReason
- OrtLoggingLevel
- Modo C — Planos HD: Flux × AnimeSharp (decisão G3.2)
- 10. Fase 5: Pipeline de assets HD
- package.json
- ec_on_post_reset
- task.md
- OrtAllocatorType
- OrtCompiledModelCompatibility
- OrtMemoryInfoDeviceType
- OrtMemType
- OrtSparseFormat
- OrtSparseIndicesFormat
- OrtWeightlessSupport
- OrtProfilingEventCategory
- ec_bplus_ort.c
- GameRun
- tsf_channels
- ec_fm_start_note
- tsf_voice_lfo
- font.cpp
- OrtCompileApiFlags
- OrtCustomOpInputOutputCharacteristic
- OrtEpContextDataCallbackSupportFlags
- OrtHardwareDeviceType
- OrtEpDataLayout
- bplus_gl_parity.py
- AGENTS.md
- CLAUDE.md
- Theme
- _FmChan
- Palette
- tsf_hydra_phdr
- SystemFont
- ec_bplus_ort_create
- OrtDeviceMemoryType
- ec_video_scanline_lazy
- ec_handle_arg
- pre_dedither_torch
- Shaders and presets
- libretro/README.md
- OrtResourceCount
- legacy/README.md
- ec_on_post_reset
- EcBplusOrt
- pathlib
- TextStyle
- TASK.md — Fila Executável Ativa (EternalRecomp v2.0)
- tsf_stream_memory
- OrtEp
- OrtKernelImpl
- t2_alive.py
- OrtLoopKernelHelper
- OrtNodeComputeInfo
- ECStudentESPCN4x
- Id
- ec_load_settings
- TSFOutputMode
- Ctx
- ScriptBuilder
- Placeholder
- Kind
- State
- select_v3_frames
- OrtDataTransferImpl
- OrtEpProfilerImpl
- OrtScanKernelHelper
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
- OrtSyncStreamImpl

## God Nodes (most connected - your core abstractions)
1. `App` - 96 edges
2. `tsf` - 61 edges
3. `MixerApp` - 45 edges
4. `Render` - 42 edges
5. `extract()` - 34 edges
6. `Theme` - 32 edges
7. `tsf_region` - 32 edges
8. `ONNXTensorElementDataType` - 31 edges
9. `NoteEvent` - 30 edges
10. `Screen` - 25 edges

## Surprising Connections (you probably didn't know these)
- `Implementação e verificação (`N1`–`N3` / `P3`, 2026-11-19)` --references--> `ECStudentESPCN4x`  [INFERRED]
  docs/phase-4.md → neural/train_espcn.py
- `Execução e estado atual (`M7.1`, 2026-11-19)` --references--> `ECStudentESPCN4x`  [INFERRED]
  docs/phase-5.md → neural/train_espcn.py
- `9.4 Extrator, Pipeline ComfyUI HD (`M7`) e Destilação Neural B+ (`N1`–`N2`)` --references--> `ECStudentESPCN4x`  [INFERRED]
  docs/PROGRESS.md → neural/train_espcn.py
- `Etapa G1 — Laboratório Visual de Upscalers Teachers *(Próximo passo imediato — sem alterar código do jogo)*` --references--> `pre_dedither_torch()`  [INFERRED]
  task.md → neural/train_espcn.py
- `Etapa A5 — Revisão do Pilar 3 + Remaster por Instrumento (aprovada e em execução 2026-11-25)` --references--> `ec_audio_reset()`  [INFERRED]
  task.md → game/ec_audio.c

## Import Cycles
- None detected.

## Communities (195 total, 50 thin omitted)

### Community 0 - "App"
Cohesion: 0.04
Nodes (56): App, assets_dir, autoplay, detected, exe_dir, extra_game_args, fade, game_exe (+48 more)

### Community 1 - "ec_audio.c"
Cohesion: 0.10
Nodes (23): A5.3 — Motor por instrumento e `hybrid` por substituição de canal, ec_allpass_tick(), ec_audio_mix_callback(), ec_audio_selftest(), ec_biquad_clear(), ec_biquad_design_highshelf(), ec_biquad_design_lowshelf(), ec_biquad_design_peaking() (+15 more)

### Community 2 - "ec_hd.c"
Cohesion: 0.12
Nodes (35): cram_to_qrgb9(), derive_asset_root(), ec_hd_begin_frame(), ec_hd_get_mode(), ec_hd_is_active(), ec_hd_load_manifest(), ec_hd_record_pixel(), ec_hd_set_mode_str() (+27 more)

### Community 3 - "ec_bplus_gl.c"
Cohesion: 0.08
Nodes (41): sc_compile(), collect_query(), compile_shader(), dispatch_conv(), ec_bplus_gl_create(), ec_bplus_gl_destroy(), ec_bplus_gl_metrics(), ec_bplus_gl_process() (+33 more)

### Community 4 - "tsf"
Cohesion: 0.09
Nodes (48): ec_audio_load_soundfont(), tsf, tsf_active_voice_count(), tsf_bank_get_presetname(), tsf_bank_note_off(), tsf_channel_applypitch(), tsf_channel_get_pan(), tsf_channel_get_pitchrange() (+40 more)

### Community 5 - "tsf.h"
Cohesion: 0.10
Nodes (31): tsf_bank_note_on(), tsf_cents2Hertz(), tsf_channel_get_volume(), tsf_channel_setup_voice(), tsf_channel_sounds_off_all(), tsf_decibelsToGain(), tsf_load_presets(), tsf_note_on() (+23 more)

### Community 6 - "app.js"
Cohesion: 0.13
Nodes (45): anyChannelSoloed(), apiJson(), applyABGains(), applyChipBlendGains(), applyMasterGain(), channelKindClass(), dbToLin(), ensureAudioContext() (+37 more)

### Community 7 - "PROGRESS.md — Estado do projeto EternalRecomp"
Cohesion: 0.05
Nodes (39): 10.5 Captura e remaster de áudio (2026-11-20, preparação), 10. Estado de Pendências e Bloqueios Reais, 11.1 Save/load V2 (`ECRB2sav`) e reset frio (F10), 11.2 Cenários off-label no VS 2P (Holo-Trainer + Santuário) — CONCLUÍDO, 11.3 Fim de partida nos estados off-label — CONCLUÍDO, 11.5 Arquitetura da máquina de estados mapeada (neste ciclo), 11.6 Limpeza e metodologia, 11. Ciclo 2026-11-21: Save/Load V2, Cold Reset, EC jogável, cenários off-label e fim de partida (+31 more)

### Community 8 - "remaster_track.py"
Cohesion: 0.08
Nodes (34): apply_3band_eq(), apply_master_normalize_and_limit(), apply_pan_and_gain(), _biquad_highshelf(), _biquad_lowshelf(), _biquad_peaking(), _can_use_wsl_fluidsynth(), detect_synth_backend() (+26 more)

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
Cohesion: 0.10
Nodes (21): _build_track_chunk(), convert_chip_ring_to_midi(), _DacBurst, _encode_vlq(), main(), _meta_set_tempo(), _meta_track_name(), NoteEvent (+13 more)

### Community 13 - "tsf_hydra_shdr"
Cohesion: 0.09
Nodes (20): tsf_decode_ogg(), tsf_decode_sf3_samples(), tsf_hydra_shdr, end, endLoop, originalPitch, pitchCorrection, sampleLink (+12 more)

### Community 14 - "onnxruntime_c_api.h"
Cohesion: 0.06
Nodes (35): ExecutionMode, ORT_PARALLEL, ORT_SEQUENTIAL, OrtAllocator, version, OrtCompileApi, OrtCustomCreateThreadFn, OrtCustomHandleType (+27 more)

### Community 15 - "ec_harness.py"
Cohesion: 0.14
Nodes (25): decode_png_rgba(), detect_gpgx_byte_order(), diff_wram(), expected_tel_ok(), format_md_report(), inspect_widescreen_margins(), load_ram_map(), main() (+17 more)

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
Cohesion: 0.13
Nodes (24): ec_apply_preset_bplus_once(), ec_bplus_is_enabled(), ec_lookup_layer_tile(), ec_mem16(), ec_mem32(), ec_parse_dedither_mode(), ec_plane_edge_comp(), ec_resolve_bplus_backend() (+16 more)

### Community 21 - "review_viewer.py"
Cohesion: 0.09
Nodes (27): layer_category(), main(), make_handler(), do_GET(), do_POST(), _send_bytes(), _send_json(), recompute_summary() (+19 more)

### Community 22 - "collect_dataset.py"
Cohesion: 0.31
Nodes (7): assign_split(), capture_movie(), main(), make_predither(), movie_list(), read_state(), select_frames()

### Community 23 - "gen_roster_movies.py"
Cohesion: 0.18
Nodes (15): add_combat_sequence(), add_p2_combat_sequence(), add_tap(), build_2p_header(), build_2p_match_end_movie(), build_2p_overkill_movie(), build_2p_select(), build_2p_vs_movie() (+7 more)

### Community 24 - "ROADMAP.md — EternalRecomp"
Cohesion: 0.08
Nodes (24): 0.1 Identidade da ROM de referência, 0.2 Ferramentas presentes na pasta, 0.3 Lacunas de toolchain (instalar na Fase 0), 0.4 Progresso da Fase 0 (2026-11-18)- [x] `git init` + `.gitignore` (`roms/`, `assets/`, `disasm/data/`, ferramentas externas), 0. Fatos verificados (2026-11-18), 10. Trilha paralela — Upscale neural (N1 → N3), 11.1 Diagnóstico feito, 11.2 Plano de uso do REA no projeto (+16 more)

### Community 25 - "export_onnx.py"
Cohesion: 0.16
Nodes (11): benchmark_ort_latency(), canonical_tensor_specs(), compute_array_metrics(), create_ort_session(), export_onnx_model(), export_weight_pack(), main(), remove_stale_onnx() (+3 more)

### Community 26 - "ec_spec.c"
Cohesion: 0.11
Nodes (12): Revisão do critério de aceite e diagnóstico de paridade (2026-11-19), ec_call_resume(), ec_call_vblank(), ec_fill_frame_record(), ec_ini_clean_value(), ec_ini_trim(), ec_is_native_entry(), ec_on_state_loaded() (+4 more)

### Community 27 - "HeadlessPlatform"
Cohesion: 0.10
Nodes (7): HeadlessPlatform, cartridgePak, fbH, fbW, frames, systemPak, nall::main()

### Community 28 - "comfy_upscale.py"
Cohesion: 0.14
Nodes (12): Etapa G3 — Modo C Híbrido (Planos HD limpos + Fallback B+ v3), bleed_rgb_into_transparent(), enforce_silhouette(), http_json(), layer_category(), main(), save_manifest_atomic(), select_entries() (+4 more)

### Community 29 - "extract_assets.py"
Cohesion: 0.17
Nodes (9): decode_4bpp_tile(), decode_cram_shadow(), encode_gray_png(), encode_rgba_png(), extract_capture(), find_compatible_palettes(), indexed_to_rgba_rows(), inspect_obj_table_master() (+1 more)

### Community 30 - "ec_boss.c"
Cohesion: 0.15
Nodes (25): 11.4 EC jogável e sprites ilimitados, call_68k_pal(), call_68k_sfx(), call_68k_sub(), ec_boss_apply_rom_patches(), ec_boss_instruction_hook(), ec_boss_rb_state_load(), ec_boss_reset() (+17 more)

### Community 31 - "tsf_midi_synth.c"
Cohesion: 0.20
Nodes (12): 3.1 Fluxo de Arquivos e Ferramentas (100% MIDI, Zero WAV em Disco), tsf_close(), tsf_copy(), tsf_set_max_voices(), ec_auto_preset(), ec_bridge_lock(), ec_bridge_unlock(), ec_clamp_i32() (+4 more)

### Community 32 - "extract_ui_assets.py"
Cohesion: 0.11
Nodes (35): bbox_alpha(), blit_sprite(), bplus_script(), build_font(), crop(), decode_tiles(), default_out(), detect_emblem() (+27 more)

### Community 33 - "export_stems.py"
Cohesion: 0.18
Nodes (9): _decode_vlq(), read_midi_notes(), build_default_profile(), _classify_channel(), export_stems(), format_profile_toml(), main(), merge_existing_profile() (+1 more)

### Community 34 - "sys"
Cohesion: 0.15
Nodes (6): events(), scan(), words(), key_of(), main(), parse()

### Community 35 - "OrtEnvCreationOptions"
Cohesion: 0.10
Nodes (17): OrtEnvCreationOptions, config_entries, custom_logging_function, custom_logging_param, log_id, logging_severity_level, threading_options, version (+9 more)

### Community 36 - "extract_all_midis.py"
Cohesion: 0.20
Nodes (8): _boot_to_main_menu(), _build_char_select_script(), _build_music_test_script(), main(), _run_headless(), _ScriptBuilder, _split_dump_by_windows(), _summarize_midi()

### Community 37 - "draw_text"
Cohesion: 0.15
Nodes (16): App::draw(), draw_text(), Color, a, b, g, r, Rect (+8 more)

### Community 38 - "subprocess"
Cohesion: 0.18
Nodes (6): main(), time_us(), main(), parse(), gen(), run()

### Community 39 - "tsf_hydra"
Cohesion: 0.11
Nodes (19): tsf_hydra, ibagNum, ibags, igenNum, igens, imodNum, imods, instNum (+11 more)

### Community 40 - "OrtTensorRTProviderOptions"
Cohesion: 0.11
Nodes (19): OrtTensorRTProviderOptions, device_id, has_user_compute_stream, trt_dla_core, trt_dla_enable, trt_dump_subgraphs, trt_engine_cache_enable, trt_engine_cache_path (+11 more)

### Community 41 - "OrtErrorCode"
Cohesion: 0.11
Nodes (17): OrtErrorCode, ORT_DEVICE_RESET, ORT_ENGINE_ERROR, ORT_EP_FAIL, ORT_FAIL, ORT_INVALID_ARGUMENT, ORT_INVALID_GRAPH, ORT_INVALID_PROTOBUF (+9 more)

### Community 42 - "Render"
Cohesion: 0.10
Nodes (13): lerp(), Render, canvas, integer_scale, sdl, window, fbm(), hash2() (+5 more)

### Community 43 - "tsf_hydra_imod"
Cohesion: 0.07
Nodes (28): tsf_hydra_genamount, range, shortAmount, wordAmount, tsf_hydra_ibag, instGenNdx, instModNdx, tsf_hydra_igen (+20 more)

### Community 44 - "OrtCUDAProviderOptions"
Cohesion: 0.12
Nodes (16): OrtCUDAProviderOptions, arena_extend_strategy, cudnn_conv_algo_search, default_memory_arena_cfg, device_id, do_copy_in_default_stream, gpu_mem_limit, has_user_compute_stream (+8 more)

### Community 46 - "train_espcn.py"
Cohesion: 0.23
Nodes (10): alpha_over(), build_composite_eval_batches(), build_eval_batches(), composite_scene_patch(), extract_patch(), load_pair_tensors(), main(), sample_composite_batch() (+2 more)

### Community 48 - "onnxruntime_ep_c_api.h"
Cohesion: 0.18
Nodes (9): CreateEpApiFactoriesFn, OrtEpApi, OrtEpFactory, ort_version_supported, OrtExternalResourceImporterImpl, ort_version_supported, OrtSyncNotificationImpl, ort_version_supported (+1 more)

### Community 50 - "tsf_channel"
Cohesion: 0.14
Nodes (14): tsf_channel, bank, gainDB, midiData, midiExpression, midiPan, midiRPN, midiVolume (+6 more)

### Community 51 - "4. Guia Completo do Arquivo `settings.ini`"
Cohesion: 0.06
Nodes (31): 1. Visão Geral dos Binários, 2.1 Variáveis SDL2 (Vídeo, Renderizador e Áudio), 2.2 Variáveis do Runtime Base (*GenesisRecomp*), 2.3 Variáveis Específicas de Eternal Champions (*EC*), 2. Variáveis de Ambiente, 3.1 Ajuda e Informações Gerais, 3.2 Vídeo, Janela e Widescreen, 3.3.1 Shaders (RetroArch `.glslp`) (+23 more)

### Community 52 - "OrtMIGraphXProviderOptions"
Cohesion: 0.14
Nodes (14): OrtMIGraphXProviderOptions, device_id, migraphx_arena_extend_strategy, migraphx_exhaustive_tune, migraphx_fp16_enable, migraphx_fp8_enable, migraphx_int8_calibration_table_name, migraphx_int8_enable (+6 more)

### Community 53 - "DESIGN.md — Eternal Champions: Recompilação Nativa e Remasterização"
Cohesion: 0.15
Nodes (13): 16. Marcos e critérios de aceite, 17. Riscos, 18. Questões em aberto, 1.1 Objetivos, 1.2 Não objetivos, 1. Visão geral, 2. Princípios de engenharia, 3. Escopo de uso (+5 more)

### Community 54 - "GLuint"
Cohesion: 0.22
Nodes (13): ec_hd_composite_gpu(), ensure_plane_gl_tex(), ensure_sprite_gl_tex(), hd_compile_sh(), hd_gpu_create(), hd_gpu_ensure_size(), hd_link_prog(), hd_load_gl() (+5 more)

### Community 55 - "Arquitetura de Áudio e Síntese SoundFont em Tempo Real — Eternal Champions"
Cohesion: 0.10
Nodes (17): 1.1 Chips de Áudio e Canais Físicos, 1.2 Driver Z80 (`$000A80`), 1.3 Interface 68000 → Z80 (`disasm/code/sound_cmd.asm`), 1. Subsistema de Som Original (Hardware + Driver Z80), 2.1 O Problema na Versão Anterior de `tools/audio/chip_ring_to_midi.py`, 2.2 Correção no Novo Pipeline por Canal, 2. Causa-Raiz do Falso-Positivo de Percussão em `03_syria_blade`, 4.1 Pontos de Integração no Runner (+9 more)

### Community 56 - "docs/main_loop.md — Esqueleto do laço principal (Fase 1, esboço)"
Cohesion: 0.15
Nodes (12): Cadeia de input → menu (decodificada, 2026-11-18), docs/main_loop.md — Esqueleto do laço principal (Fase 1, esboço), Entry points, Flags e estado (RAM $FF0000+, todas hipóteses), Fluxo de um quadro (FUN_0000fd24), Handler do estado 9 (menu-init, $3256 — convertido, verificado), Handler do menu (estado 13, $36B2 — analisado por disassembly, 2026-11-18), Mapa de estados (GPGX = referência; ares concorda sem input) (+4 more)

### Community 57 - "11. Upscale neural em tempo real (modo B+)"
Cohesion: 0.15
Nodes (13): 11.1 Objetivo, 11.2 Destilação professor → aluno, 11.3 Inferência por camada, 11.4 Orçamento de desempenho, 11.5 Runtime de inferência, 11.6 Caminho de implementação (independente da recompilação), 11. Upscale neural em tempo real (modo B+), 9.1 Modo C: resolução de assets (+5 more)

### Community 58 - "train_v3"
Cohesion: 0.19
Nodes (7): amp_ctx(), benchmark_gpu_latency(), _load_rgb_u8(), load_v3_pairs(), make_lpips(), train_v3(), V3Loss

### Community 59 - "gen_recomp_config.py"
Cohesion: 0.16
Nodes (8): load_labels(), main(), build_cfg_and_leads(), add_work(), format_hex_array(), in_code_regions(), is_valid_code_start(), main()

### Community 60 - "argparse"
Cohesion: 0.10
Nodes (30): main(), main(), region_list(), connect(), fi(), main(), rpc(), connect() (+22 more)

### Community 61 - "PLAN.md — Launcher do Eternal Champions Recomp ("EC Launcher")"
Cohesion: 0.07
Nodes (26): 10.1 Conceito, 10.2 Layout, 10.3 Áudio da prévia, 10.4 Primeira execução e dados, 10.5 Pré-requisitos e riscos, 10. Estúdio de Remaster (integrado), 1. Objetivos e não-objetivos, 2. Arquitetura (decisão proposta) (+18 more)

### Community 62 - "EcBplusOrtMetrics"
Cohesion: 0.14
Nodes (14): EcBplusOrtMetrics, fallback_frames, frames, input_height, input_width, last_gpu_ms, mean_gpu_ms, output_height (+6 more)

### Community 63 - "OrtExternalMemoryDescriptor"
Cohesion: 0.17
Nodes (12): OrtExternalMemoryDescriptor, handle_type, native_handle, offset_bytes, size_bytes, version, OrtExternalMemoryHandleType, ORT_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP (+4 more)

### Community 64 - "GVDP"
Cohesion: 0.29
Nodes (12): ec_hd_frame_active(), ec_draw_sprite_span(), ec_eval_full_sprites_line(), ec_fetch_plane_pixel(), ec_fetch_window_pixel(), ec_plane_tiles(), ec_refresh_frame_hud_sat(), ec_render_ws_scanline() (+4 more)

### Community 66 - "string"
Cohesion: 0.10
Nodes (5): open_utf8(), png_read_rgba(), png_write_rgba(), rotr(), sha256_hex()

### Community 67 - "Resumo do Harness Determinístico & Frame Data (`M3` / `M4`–`M6`)"
Cohesion: 0.20
Nodes (9): `01_attract_boot.ecm` (1000 quadros) — `byte-swapped-16bit (desfeito via i^1)`, `02_title_to_fight.ecm` (2900 quadros) — `byte-swapped-16bit (desfeito via i^1)`, 1. Autodeterminismo Nativo e Misses (Suíte Completa), 2. Paridade da Recompilação Estática (`Recomp` × `GENESIS_FORCE_INTERP=1`), 3. Cobertura do Elenco e Arenas + Paridade Native vs GPGX, 4. Progressão de Luta Ativa Observada (`02_title_to_fight.ecm`), 5. Validação Widescreen 16:9 (`game/ec_video.c` — Todas as Arenas), Checkpoints Detalhados — Boot (`01`) e Luta Principal (`02`) (+1 more)

### Community 68 - "1. Bloqueadores de gameplay"
Cohesion: 0.20
Nodes (10): 1. Bloqueadores de gameplay, [~] 1º load do savegame às vezes trava (carregar 2–3× libera), 2. Bugs já resolvidos (rastreabilidade), 3. Limitações conhecidas (não são bugs do jogo), 4. Pendências de escopo (não-bugs), BUG_QUEUE.md — Fila restante de bugs e pendências, [ ] Finalizador da Jetta — bug reportado, a reproduzir, [~] Sprites do EC falham nos DOIS ECs — só no round 1 do Trident (+2 more)

### Community 70 - "Tensor"
Cohesion: 0.31
Nodes (5): compute_ssim(), compute_ssim_tensor(), eval_v3(), rgb_to_rgba(), sobel_edges()

### Community 71 - "InputState"
Cohesion: 0.06
Nodes (31): InputState, any_input, held, mouse_click, mouse_inside, mouse_moved, mouse_right_click, mouse_x (+23 more)

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
Cohesion: 0.25
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
Cohesion: 0.12
Nodes (16): OrtExternalSemaphoreDescriptor, native_handle, type, version, OrtExternalSemaphoreType, ORT_EXTERNAL_SEMAPHORE_D3D12_FENCE, ORT_EXTERNAL_SEMAPHORE_VK_TIMELINE_SEMAPHORE_OPAQUE_FD, ORT_EXTERNAL_SEMAPHORE_VK_TIMELINE_SEMAPHORE_WIN32 (+8 more)

### Community 80 - "ONNXType"
Cohesion: 0.25
Nodes (8): ONNXType, ONNX_TYPE_MAP, ONNX_TYPE_OPAQUE, ONNX_TYPE_OPTIONAL, ONNX_TYPE_SEQUENCE, ONNX_TYPE_SPARSETENSOR, ONNX_TYPE_TENSOR, ONNX_TYPE_UNKNOWN

### Community 81 - "OrtExecutionProviderDevicePolicy"
Cohesion: 0.25
Nodes (8): OrtExecutionProviderDevicePolicy, OrtExecutionProviderDevicePolicy_DEFAULT, OrtExecutionProviderDevicePolicy_MAX_EFFICIENCY, OrtExecutionProviderDevicePolicy_MAX_PERFORMANCE, OrtExecutionProviderDevicePolicy_MIN_OVERALL_POWER, OrtExecutionProviderDevicePolicy_PREFER_CPU, OrtExecutionProviderDevicePolicy_PREFER_GPU, OrtExecutionProviderDevicePolicy_PREFER_NPU

### Community 82 - "Input"
Cohesion: 0.07
Nodes (20): App::shutdown(), Action, A_ACCEPT, A_BACK, A_COUNT, A_DOWN, A_HELP, A_LEFT (+12 more)

### Community 83 - "OrtLanguageProjection"
Cohesion: 0.25
Nodes (8): OrtLanguageProjection, ORT_PROJECTION_C, ORT_PROJECTION_CPLUSPLUS, ORT_PROJECTION_CSHARP, ORT_PROJECTION_JAVA, ORT_PROJECTION_NODEJS, ORT_PROJECTION_PYTHON, ORT_PROJECTION_WINML

### Community 84 - "voice_catalog.py"
Cohesion: 0.13
Nodes (19): build_voice_entries(), decode_patch(), extract_voice_notes(), emit(), _fmt(), fnv1a32(), _load_events(), main() (+11 more)

### Community 85 - "OrtROCMProviderOptions"
Cohesion: 0.15
Nodes (13): OrtROCMProviderOptions, arena_extend_strategy, default_memory_arena_cfg, device_id, do_copy_in_default_stream, enable_hip_graph, gpu_mem_limit, has_user_compute_stream (+5 more)

### Community 86 - "t2_cram3.py"
Cohesion: 0.54
Nodes (6): connect(), dump(), fi(), main(), pause_at(), rpc()

### Community 87 - "t2_a6bisect.py"
Cohesion: 0.57
Nodes (5): a6(), connect(), fi(), main(), rpc()

### Community 88 - "gen_targets.py"
Cohesion: 0.43
Nodes (5): check(), gen(), load_manifest(), main(), save_manifest()

### Community 89 - "ec_parse_profile_toml"
Cohesion: 0.16
Nodes (25): ec_audio_load_profiles_dir(), ec_audio_load_voices(), ec_ends_with_ci(), ec_file_mtime(), ec_find_channel_index(), ec_find_profile_for_bgm(), ec_find_voice(), ec_hot_reload_poll() (+17 more)

### Community 91 - "src/main.cpp"
Cohesion: 0.12
Nodes (15): Args, autoplay, frames, game_args, h, screen, shot, w (+7 more)

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

### Community 99 - "10. Fase 5: Pipeline de assets HD"
Cohesion: 0.29
Nodes (7): 10.1 Extração, 10.2 Upscale, 10.3 Revisão, 10. Fase 5: Pipeline de assets HD, Execução e estado atual (`M7.1`, 2026-11-19), Fase 5 — Pipeline de Assets HD (`M7`), Modos extras (`M7.3`, 2026-11-19)

### Community 100 - "package.json"
Cohesion: 0.40
Nodes (5): allowScripts, node@26.13.1, dependencies, node@^26.13.1, node

### Community 101 - "ec_on_post_reset"
Cohesion: 0.24
Nodes (18): 4.2 Fluxo de Duas Camadas em `game/ec_audio.{h,c}`, ec_apply_active_profile(), ec_apply_chip_mask(), ec_audio_init(), ec_audio_on_bgm_pause(), ec_audio_on_bgm_play(), ec_audio_on_bgm_stop(), ec_audio_on_chip_write() (+10 more)

### Community 102 - "task.md"
Cohesion: 0.13
Nodes (11): 1. Build nativo (`EternalChampionsRecomp.exe`), 2. Execução manual, 3. Validação determinística (`harness/ec_harness.py`), 4. Python / GPU (treino, upscalers, ferramentas), Eternal Champions — Build & Harness (Windows nativo), 0. Grafo primeiro, 1. Nunca leia arquivos grandes inteiros, 2. Onde vive cada informação (+3 more)

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
Cohesion: 0.18
Nodes (16): ec_bplus_ort_process(), float_to_half(), half_to_float(), ort_dlerror_str(), ort_dlopen_any(), ort_dlopen_library(), ort_download_rgba(), ort_download_rgba16() (+8 more)

### Community 112 - "GameRun"
Cohesion: 0.12
Nodes (13): ensure_skip_launcher(), find_last_error_log(), GameProcess, handle, pid, shell_open(), start_game(), trim_ws() (+5 more)

### Community 113 - "tsf_channels"
Cohesion: 0.50
Nodes (4): tsf_channels, activeChannel, channelNum, channels

### Community 114 - "ec_fm_start_note"
Cohesion: 0.18
Nodes (27): ec_audio_fm_patch_hash(), ec_can_emit_notes(), ec_clamp_i(), ec_clampf(), ec_fm_carrier_min_tl(), ec_fm_patch_hash(), ec_fm_set_vol(), ec_fm_start_note() (+19 more)

### Community 115 - "tsf_voice_lfo"
Cohesion: 0.50
Nodes (4): tsf_voice_lfo, delta, level, samplesUntil

### Community 116 - "font.cpp"
Cohesion: 0.12
Nodes (7): Batch, idx, v, Font, load_bitmap_font(), make_system_font(), utf8_to_u32()

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
Cohesion: 0.46
Nodes (5): main(), make_script(), metrics(), run_capture(), torch_reference()

### Community 126 - "Theme"
Cohesion: 0.10
Nodes (18): Fonts, help, logo, menu, title, Theme, extracted_art, fonts (+10 more)

### Community 127 - "_FmChan"
Cohesion: 0.32
Nodes (3): _FmChan, fnum_to_midi(), mul_to_factor()

### Community 128 - "Palette"
Cohesion: 0.09
Nodes (22): Palette, bad, dusk_bottom, dusk_halo, dusk_top, gold_bottom, gold_outline, gold_top (+14 more)

### Community 129 - "tsf_hydra_phdr"
Cohesion: 0.11
Nodes (17): tsf_hydra_inst, instBagNdx, instName, tsf_hydra_phdr, bank, genre, library, morphology (+9 more)

### Community 135 - "SystemFont"
Cohesion: 0.12
Nodes (12): Glyph, adv, ah, aw, ax, ay, xoff, yoff (+4 more)

### Community 136 - "ec_bplus_ort_create"
Cohesion: 0.22
Nodes (7): ec_bplus_ort_create(), ort_dlsym_any(), ort_release_status(), ort_status_msg(), OrtApi, ORT_ALL_ARGS_NONNULL, OrtApiBase

### Community 137 - "OrtDeviceMemoryType"
Cohesion: 0.67
Nodes (3): OrtDeviceMemoryType, OrtDeviceMemoryType_DEFAULT, OrtDeviceMemoryType_HOST_ACCESSIBLE

### Community 138 - "ec_video_scanline_lazy"
Cohesion: 0.22
Nodes (14): 1. Princípio: 320 px na lógica, margem estendida só no renderer, 2. Política por Estado (`$FFFFFD64`), 3. Reposicionamento de HUD e Modo C, 4. Flags CLI e Resultados Medidos, 5. GW.3 — Flicker lateral e sprites descartados nas margens (causa-raiz e correção), Widescreen 16:9 / Adaptativo (`M4`–`M6`), ec_compute_viewport_x0(), ec_is_arena_state() (+6 more)

### Community 139 - "ec_handle_arg"
Cohesion: 0.18
Nodes (17): ec_boss_set_enabled(), ec_hd_approved_entries(), ec_hd_total_entries(), ec_shader_chain_add_param_cli(), ec_audio_cli_number(), ec_env_set(), ec_handle_arg(), ec_ini_enhance_key() (+9 more)

### Community 145 - "OrtResourceCount"
Cohesion: 0.33
Nodes (6): FromTotalBytes(), None(), OrtResourceCount, kind, reserved, value

### Community 154 - "ec_on_post_reset"
Cohesion: 0.18
Nodes (11): 9.1 Recompilação estática (`M2`) — `build/native/EternalChampionsRecomp`, ec_audio_get_mode(), ec_audio_set_chip_mask_hook(), ec_call_entry_point(), ec_on_post_reset(), 3. Pilar 3 — Remasterização da Trilha Sonora (`Trilha A`), Etapa A1 — Auditoria FM/PSG, Correção de Blade, Stems & Motor de Remasterização (`Task A1-pipeline`), Etapa A2 — Ferramenta Externa de Mixagem por Instrumento & Catálogo (`Task A2-mixer-tool`) (+3 more)

### Community 155 - "EcBplusOrt"
Cohesion: 0.28
Nodes (9): ec_hd_destroy_gpu(), ec_video_present_gpu_destroy(), ec_bplus_ort_destroy(), ec_bplus_ort_metrics(), ec_bplus_ort_readback(), ec_bplus_ort_set_intensity(), EcBplusOrt, ort_compute_percentiles() (+1 more)

### Community 156 - "pathlib"
Cohesion: 0.20
Nodes (5): build(), main(), main(), find_vasm(), main()

### Community 157 - "TextStyle"
Cohesion: 0.13
Nodes (15): Align, Center, Left, Right, TextStyle, align, alpha, bottom (+7 more)

### Community 158 - "TASK.md — Fila Executável Ativa (EternalRecomp v2.0)"
Cohesion: 0.17
Nodes (12): 0. Pré-requisito / Checkpoint Imediato (Higiene de Git), 1. Pilar 1 — Aperfeiçoamento da Renderização Gráfica (`Trilha G`), 2. Pilar 2 — Suporte a Shaders Modulares (`Trilha S`) — *próxima fase, após Pilar 1*, 4. Trilha F — Finalização, Port Windows e Backlog de Baixa Prioridade, 5. Arquivo Histórico — Marcos Concluídos (M0–M10 + Bugfix/Polish), Etapa G1 — Laboratório Visual de Upscalers Teachers *(Próximo passo imediato — sem alterar código do jogo)*, Etapa G2 — Modo B+ v3 Full-Frame Distilled (100% da tela uniforme + 60 fps), Etapa G4 — Otimização GPU (OpenGL Compute & CUDA ORT) (+4 more)

### Community 159 - "tsf_stream_memory"
Cohesion: 0.50
Nodes (4): tsf_stream_memory, buffer, pos, total

### Community 161 - "OrtKernelImpl"
Cohesion: 0.67
Nodes (3): OrtKernelImpl, flags, ort_version_supported

### Community 162 - "t2_alive.py"
Cohesion: 0.54
Nodes (6): connect(), fi(), main(), snap(), rd(), rpc()

### Community 166 - "Id"
Cohesion: 0.15
Nodes (13): Id, ID_ABOUT, ID_ADV, ID_AUDIO, ID_CONTROLS, ID_ENH, ID_GAME, ID_PLAY (+5 more)

### Community 167 - "ec_load_settings"
Cohesion: 0.36
Nodes (8): ec_audio_set_bgm_vol(), ec_audio_set_chip_blend(), ec_audio_set_chip_blend_override(), ec_audio_set_dsp_reverb(), ec_audio_set_hot_reload(), ec_audio_set_sfx_vol(), ec_set_clamped(), ec_load_settings()

### Community 168 - "TSFOutputMode"
Cohesion: 0.50
Nodes (4): TSFOutputMode, TSF_MONO, TSF_STEREO_INTERLEAVED, TSF_STEREO_UNWEAVED

### Community 169 - "Ctx"
Cohesion: 0.32
Nodes (5): Ctx, buf, buflen, h, total

### Community 171 - "Placeholder"
Cohesion: 0.40
Nodes (3): make_placeholder(), Placeholder, title_

### Community 172 - "Kind"
Cohesion: 0.40
Nodes (5): Kind, Pop, Push, Replace, Reset

### Community 173 - "State"
Cohesion: 0.50
Nodes (4): State, Done, Failed, Running

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
- **678 isolated node(s):** `render`, `theme`, `input`, `exe_dir`, `root_dir` (+673 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 1297 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **50 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `App` connect `App` to `string`, `draw_text`, `InputState`, `Render`, `GameRun`, `Input`, `src/main.cpp`, `Theme`?**
  _High betweenness centrality (0.053) - this node is a cross-community bridge._
- **Are the 2 inferred relationships involving `MixerApp` (e.g. with `3.4 Mixer Studio (`tools/audio/mixer_studio.py` + `tools/audio/mixer_ui/`)` and `NoteEvent`) actually correct?**
  _`MixerApp` has 2 INFERRED edges - model-reasoned connections that need verification._
- **What connects `render`, `theme`, `input` to the rest of the system?**
  _678 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `App` be split into smaller, more focused modules?**
  _Cohesion score 0.03515151515151515 - nodes in this community are weakly interconnected._
- **Why does `Palette` connect `Palette` to `string`, `draw_text`, `Theme`?**
  _High betweenness centrality (0.034) - this node is a cross-community bridge._
- **Should `ec_audio.c` be split into smaller, more focused modules?**
  _Cohesion score 0.10317460317460317 - nodes in this community are weakly interconnected._
- **Why does `MixerApp` connect `mixer_studio.py` to `chip_ring_to_midi.py`?**
  _High betweenness centrality (0.028) - this node is a cross-community bridge._