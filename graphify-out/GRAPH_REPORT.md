# Graph Report - eternalchampions  (2026-11-25)

## Corpus Check
- 117 files · ~853,844 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 131 file(s) not represented in the graph (top: .asm 48, .toml 24, .ecm 17)
- WARNING: 131 of 255 files seen (51%) have no supported extractor and are missing from this graph entirely. It may look complete while representing only part of the codebase.

## Summary
- 2300 nodes · 4614 edges · 173 communities (128 shown, 45 thin omitted)
- Extraction: 96% EXTRACTED · 4% INFERRED · 0% AMBIGUOUS · INFERRED: 189 edges (avg confidence: 0.88)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `63199d74`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- mixer_studio.py
- ec_audio.c
- ec_hd.c
- ec_bplus_gl.c
- tsf
- tsf.h
- app.js
- PROGRESS.md — Estado do projeto EternalRecomp
- remaster_track.py
- upscaler_lab.py
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
- ec_bplus_ort.c
- ec_boss.c
- tsf_midi_synth.c
- tsf_hydra_phdr
- ec_bplus_ort_create
- sys
- OrtEnvCreationOptions
- extract_all_midis.py
- 5. Arquitetura descoberta do jogo
- argparse
- tsf_hydra
- OrtTensorRTProviderOptions
- OrtErrorCode
- TASK.md — Fila Executável Ativa (EternalRecomp v2.0)
- tsf_hydra_imod
- OrtCUDAProviderOptions
- train_espcn.py
- onnxruntime_ep_c_api.h
- tsf_channel
- 4. Guia Completo do Arquivo `settings.ini`
- OrtMIGraphXProviderOptions
- DESIGN.md — Eternal Champions: Recompilação Nativa e Remasterização
- ec_hd_composite_gpu
- Arquitetura de Áudio e Síntese SoundFont em Tempo Real — Eternal Champions
- docs/main_loop.md — Esqueleto do laço principal (Fase 1, esboço)
- 11. Upscale neural em tempo real (modo B+)
- train_v3
- gen_recomp_config.py
- EcBplusOrt
- time
- EcBplusOrtMetrics
- OrtExternalMemoryDescriptor
- GVDP
- mixer_studio.py
- Resumo do Harness Determinístico & Frame Data (`M3` / `M4`–`M6`)
- 1. Bloqueadores de gameplay
- Tensor
- 11. Ciclo 2026-11-21: Save/Load V2, Cold Reset, EC jogável, cenários off-label e fim de partida
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
- t2_alive.py
- t2_cram3.py
- t2_a6bisect.py
- gen_targets.py
- 9. Fases 2–5: Recompilação Nativa, Harness, Widescreen e Extrator (2026-11-19)
- json
- Investigação: Movelist do Eternal Champion (RE)
- Movelist — Extração dos Movimentos (RE)
- Proposta — Ciclo de bugfix 2ª rodada (2026-11-21)
- GraphOptimizationLevel
- OrtDeviceEpIncompatibilityReason
- OrtLoggingLevel
- Modo C — Planos HD: Flux × AnimeSharp (decisão G3.2)
- 10. Fase 5: Pipeline de assets HD
- package.json
- t2_regdiff.py
- Eternal Champions — Disasm, Símbolos & Hooks
- OrtAllocatorType
- OrtCompiledModelCompatibility
- OrtMemoryInfoDeviceType
- OrtMemType
- OrtSparseFormat
- OrtSparseIndicesFormat
- OrtWeightlessSupport
- OrtProfilingEventCategory
- 12. Ciclo 2026-11-21 (2ª rodada): fluxo do chefe (T1 ✅), training preto (T2) e sprites do Trident (T4)
- 4. Ferramentas dinâmicas
- tsf_channels
- ec_on_post_reset
- tsf_voice_lfo
- TSFOutputMode
- OrtCompileApiFlags
- OrtCustomOpInputOutputCharacteristic
- OrtEpContextDataCallbackSupportFlags
- OrtHardwareDeviceType
- OrtEpDataLayout
- OrtResourceCount
- AGENTS.md
- CLAUDE.md
- Eternal Champions — Build & Harness (Windows nativo)
- tsf_stream_memory
- ec_video_scanline_lazy
- t2_probe.py
- ECStudentESPCN4x
- OrtKernelImpl
- OrtDeviceMemoryType
- OrtLoopKernelHelper
- OrtEp
- OrtNodeComputeInfo
- Shaders and presets
- libretro/README.md
- _load_rgb_u8
- legacy/README.md
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
- `Etapa A5 — Revisão do Pilar 3 + Remaster por Instrumento (proposta 2026-11-25, **aguardando aprovação**)` --references--> `ec_audio_reset()`  [INFERRED]
  task.md → game/ec_audio.c
- `A5.4 — Mixer Studio por instrumento` --references--> `read_midi_named_tracks()`  [INFERRED]
  docs/audio_remaster_architecture.md → tools/audio/mixer_studio.py
- `0. Pré-requisito / Checkpoint Imediato (Higiene de Git)` --references--> `to_wsl_path()`  [INFERRED]
  task.md → harness/ec_harness.py
- `Etapa G1 — Laboratório Visual de Upscalers Teachers *(Próximo passo imediato — sem alterar código do jogo)*` --references--> `pre_dedither_torch()`  [INFERRED]
  task.md → neural/train_espcn.py
- `Etapa G4 — Otimização GPU (OpenGL Compute & CUDA ORT)` --references--> `ec_video_scanline_lazy()`  [INFERRED]
  task.md → game/ec_video.c

## Import Cycles
- None detected.

## Communities (173 total, 45 thin omitted)

### Community 0 - "mixer_studio.py"
Cohesion: 0.06
Nodes (22): ApiError, _clamp_float(), _clamp_int(), _clean_voice_name(), create_server(), _default_dir(), _ensure_within(), format_profile_toml() (+14 more)

### Community 1 - "ec_audio.c"
Cohesion: 0.06
Nodes (84): 4.2 Fluxo de Duas Camadas em `game/ec_audio.{h,c}`, ec_allpass_tick(), ec_apply_active_profile(), ec_audio_init(), ec_audio_load_profiles_dir(), ec_audio_mix_callback(), ec_audio_on_bgm_pause(), ec_audio_on_bgm_play() (+76 more)

### Community 2 - "ec_hd.c"
Cohesion: 0.11
Nodes (38): cram_to_qrgb9(), derive_asset_root(), ec_hd_approved_entries(), ec_hd_begin_frame(), ec_hd_get_mode(), ec_hd_is_active(), ec_hd_load_manifest(), ec_hd_record_pixel() (+30 more)

### Community 3 - "ec_bplus_gl.c"
Cohesion: 0.08
Nodes (40): collect_query(), compile_shader(), dispatch_conv(), ec_bplus_gl_create(), ec_bplus_gl_destroy(), ec_bplus_gl_metrics(), ec_bplus_gl_process(), ec_bplus_gl_readback() (+32 more)

### Community 4 - "tsf"
Cohesion: 0.09
Nodes (52): ec_audio_load_soundfont(), tsf, tsf_active_voice_count(), tsf_bank_get_presetname(), tsf_bank_note_off(), tsf_bank_note_on(), tsf_channel_applypitch(), tsf_channel_get_pan() (+44 more)

### Community 5 - "tsf.h"
Cohesion: 0.09
Nodes (32): tsf_cents2Hertz(), tsf_channel_get_volume(), tsf_channel_setup_voice(), tsf_decibelsToGain(), tsf_decode_ogg(), tsf_decode_sf3_samples(), tsf_load(), tsf_load_memory() (+24 more)

### Community 6 - "app.js"
Cohesion: 0.13
Nodes (45): anyChannelSoloed(), apiJson(), applyABGains(), applyChipBlendGains(), applyMasterGain(), channelKindClass(), dbToLin(), ensureAudioContext() (+37 more)

### Community 7 - "PROGRESS.md — Estado do projeto EternalRecomp"
Cohesion: 0.22
Nodes (9): 10.5 Captura e remaster de áudio (2026-11-20, preparação), 10. Estado de Pendências e Bloqueios Reais, 1. Objetivo e princípios, 2. Infraestrutura, 3. Estado da conversão (Fase 1), 6. Investigação e arbitragem da divergência ares × GPGX (BlastEm 1.0.0), 7. Convenções de conversão (vasm), 8. Questões em aberto (+1 more)

### Community 8 - "remaster_track.py"
Cohesion: 0.08
Nodes (35): apply_3band_eq(), apply_master_normalize_and_limit(), apply_pan_and_gain(), _biquad_highshelf(), _biquad_lowshelf(), _biquad_peaking(), _can_use_wsl_fluidsynth(), detect_synth_backend() (+27 more)

### Community 9 - "upscaler_lab.py"
Cohesion: 0.19
Nodes (14): build_boards(), capture_trident(), font(), load_rgb(), main(), make_predither(), now(), run_spandrel() (+6 more)

### Community 10 - "ec_shader_chain.c"
Cohesion: 0.07
Nodes (54): ec_shader_chain_active(), ec_shader_chain_destroy(), ec_shader_chain_get_bplus(), ec_shader_chain_process(), ec_shader_chain_set_preset_cli(), ec_shader_chain_set_settings(), sc_add_bind(), sc_add_overrides() (+46 more)

### Community 11 - "ONNXTensorElementDataType"
Cohesion: 0.06
Nodes (36): ONNXTensorElementDataType, ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16, ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL, ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX128, ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX64, ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16 (+28 more)

### Community 12 - "chip_ring_to_midi.py"
Cohesion: 0.08
Nodes (20): convert_chip_ring_to_midi(), _DacBurst, main(), NoteEvent, parse_chip_ring(), emit_fm_note(), emit_psg_noise(), emit_psg_tone() (+12 more)

### Community 13 - "tsf_u16"
Cohesion: 0.06
Nodes (32): tsf_hydra_ibag, instGenNdx, instModNdx, tsf_hydra_igen, genAmount, genOper, tsf_hydra_inst, instBagNdx (+24 more)

### Community 14 - "onnxruntime_c_api.h"
Cohesion: 0.06
Nodes (32): OrtAllocator, version, OrtCompileApi, OrtCustomCreateThreadFn, OrtCustomHandleType, __place_holder, OrtCustomJoinThreadFn, OrtCustomOp (+24 more)

### Community 15 - "ec_harness.py"
Cohesion: 0.07
Nodes (39): decode_png_rgba(), detect_gpgx_byte_order(), diff_wram(), expected_tel_ok(), format_md_report(), inspect_widescreen_margins(), load_ram_map(), main() (+31 more)

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
Cohesion: 0.12
Nodes (25): ec_shader_chain_add_param_cli(), ec_handle_arg(), ec_apply_preset_bplus_once(), ec_bplus_is_enabled(), ec_parse_dedither_mode(), ec_resolve_bplus_backend(), ec_resolve_bplus_budget_ms(), ec_resolve_bplus_dedither() (+17 more)

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
Cohesion: 0.16
Nodes (11): benchmark_ort_latency(), canonical_tensor_specs(), compute_array_metrics(), create_ort_session(), export_onnx_model(), export_weight_pack(), main(), remove_stale_onnx() (+3 more)

### Community 26 - "ec_spec.c"
Cohesion: 0.12
Nodes (12): ec_audio_cli_number(), ec_call_resume(), ec_fill_frame_record(), ec_ini_clean_value(), ec_ini_trim(), ec_is_native_entry(), ec_on_state_loaded(), ec_parse_double_strict() (+4 more)

### Community 27 - "HeadlessPlatform"
Cohesion: 0.10
Nodes (7): HeadlessPlatform, cartridgePak, fbH, fbW, frames, systemPak, nall::main()

### Community 28 - "comfy_upscale.py"
Cohesion: 0.14
Nodes (12): Etapa G3 — Modo C Híbrido (Planos HD limpos + Fallback B+ v3), bleed_rgb_into_transparent(), enforce_silhouette(), http_json(), layer_category(), main(), save_manifest_atomic(), select_entries() (+4 more)

### Community 29 - "ec_bplus_ort.c"
Cohesion: 0.18
Nodes (16): ec_bplus_ort_process(), float_to_half(), half_to_float(), ort_dlerror_str(), ort_dlopen_any(), ort_dlopen_library(), ort_download_rgba(), ort_download_rgba16() (+8 more)

### Community 30 - "ec_boss.c"
Cohesion: 0.18
Nodes (24): 11.4 EC jogável e sprites ilimitados, call_68k_pal(), call_68k_sfx(), call_68k_sub(), ec_boss_apply_rom_patches(), ec_boss_instruction_hook(), ec_boss_rb_state_load(), ec_boss_vblank() (+16 more)

### Community 31 - "tsf_midi_synth.c"
Cohesion: 0.16
Nodes (13): 3.1 Fluxo de Arquivos e Ferramentas (100% MIDI, Zero WAV em Disco), tsf_close(), tsf_copy(), tsf_load_filename(), tsf_set_max_voices(), ec_auto_preset(), ec_bridge_lock(), ec_bridge_unlock() (+5 more)

### Community 32 - "tsf_hydra_phdr"
Cohesion: 0.15
Nodes (11): tsf_hydra_phdr, bank, genre, library, morphology, preset, presetBagNdx, presetName (+3 more)

### Community 33 - "ec_bplus_ort_create"
Cohesion: 0.22
Nodes (7): ec_bplus_ort_create(), ort_dlsym_any(), ort_release_status(), ort_status_msg(), OrtApi, ORT_ALL_ARGS_NONNULL, OrtApiBase

### Community 34 - "sys"
Cohesion: 0.16
Nodes (6): events(), scan(), words(), key_of(), main(), parse()

### Community 35 - "OrtEnvCreationOptions"
Cohesion: 0.10
Nodes (17): OrtEnvCreationOptions, config_entries, custom_logging_function, custom_logging_param, log_id, logging_severity_level, threading_options, version (+9 more)

### Community 36 - "extract_all_midis.py"
Cohesion: 0.19
Nodes (8): _boot_to_main_menu(), _build_char_select_script(), _build_music_test_script(), main(), _run_headless(), _ScriptBuilder, _split_dump_by_windows(), _summarize_midi()

### Community 37 - "5. Arquitetura descoberta do jogo"
Cohesion: 0.22
Nodes (9): 5.1 Fluxo de boot, 5.2 Máquina de estados (tabela em `$27B4`, 40 handlers), 5.3 Laço principal e subsistemas, 5.4 Subsistema de input, 5.5 Escalonador cooperativo, 5.6 Estrutura de lutador e estado da luta (`disasm/symbols/ram_map.toml`), 5.7 HBlank / Raster e Scroll (DESIGN §18.3), 5.8 Tabelas mestras, IA dos lutadores e Replay/Estatísticas (+1 more)

### Community 38 - "argparse"
Cohesion: 0.08
Nodes (15): main(), load_labels(), main(), main(), make_script(), metrics(), run_capture(), main() (+7 more)

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
Cohesion: 0.17
Nodes (12): 0. Pré-requisito / Checkpoint Imediato (Higiene de Git), 1. Pilar 1 — Aperfeiçoamento da Renderização Gráfica (`Trilha G`), 2. Pilar 2 — Suporte a Shaders Modulares (`Trilha S`) — *próxima fase, após Pilar 1*, 4. Trilha F — Finalização, Port Windows e Backlog de Baixa Prioridade, 5. Arquivo Histórico — Marcos Concluídos (M0–M10 + Bugfix/Polish), Etapa G1 — Laboratório Visual de Upscalers Teachers *(Próximo passo imediato — sem alterar código do jogo)*, Etapa G2 — Modo B+ v3 Full-Frame Distilled (100% da tela uniforme + 60 fps), Etapa G4 — Otimização GPU (OpenGL Compute & CUDA ORT) (+4 more)

### Community 43 - "tsf_hydra_imod"
Cohesion: 0.12
Nodes (16): tsf_hydra_genamount, range, shortAmount, wordAmount, tsf_hydra_imod, modAmount, modAmtSrcOper, modDestOper (+8 more)

### Community 44 - "OrtCUDAProviderOptions"
Cohesion: 0.07
Nodes (29): OrtCUDAProviderOptions, arena_extend_strategy, cudnn_conv_algo_search, default_memory_arena_cfg, device_id, do_copy_in_default_stream, gpu_mem_limit, has_user_compute_stream (+21 more)

### Community 46 - "train_espcn.py"
Cohesion: 0.23
Nodes (10): alpha_over(), build_composite_eval_batches(), build_eval_batches(), composite_scene_patch(), extract_patch(), load_pair_tensors(), main(), sample_composite_batch() (+2 more)

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

### Community 54 - "ec_hd_composite_gpu"
Cohesion: 0.25
Nodes (13): ec_hd_composite_gpu(), ensure_plane_gl_tex(), ensure_sprite_gl_tex(), hd_compile_sh(), hd_gpu_create(), hd_gpu_ensure_size(), hd_link_prog(), hd_load_gl() (+5 more)

### Community 55 - "Arquitetura de Áudio e Síntese SoundFont em Tempo Real — Eternal Champions"
Cohesion: 0.08
Nodes (21): 1.1 Chips de Áudio e Canais Físicos, 1.2 Driver Z80 (`$000A80`), 1.3 Interface 68000 → Z80 (`disasm/code/sound_cmd.asm`), 1. Subsistema de Som Original (Hardware + Driver Z80), 2.1 O Problema na Versão Anterior de `tools/audio/chip_ring_to_midi.py`, 2.2 Correção no Novo Pipeline por Canal, 2. Causa-Raiz do Falso-Positivo de Percussão em `03_syria_blade`, 3.2 Contrato: `assets/audio_remaster/profiles/<track>.toml` (+13 more)

### Community 56 - "docs/main_loop.md — Esqueleto do laço principal (Fase 1, esboço)"
Cohesion: 0.15
Nodes (12): Cadeia de input → menu (decodificada, 2026-11-18), docs/main_loop.md — Esqueleto do laço principal (Fase 1, esboço), Entry points, Flags e estado (RAM $FF0000+, todas hipóteses), Fluxo de um quadro (FUN_0000fd24), Handler do estado 9 (menu-init, $3256 — convertido, verificado), Handler do menu (estado 13, $36B2 — analisado por disassembly, 2026-11-18), Mapa de estados (GPGX = referência; ares concorda sem input) (+4 more)

### Community 57 - "11. Upscale neural em tempo real (modo B+)"
Cohesion: 0.15
Nodes (13): 11.1 Objetivo, 11.2 Destilação professor → aluno, 11.3 Inferência por camada, 11.4 Orçamento de desempenho, 11.5 Runtime de inferência, 11.6 Caminho de implementação (independente da recompilação), 11. Upscale neural em tempo real (modo B+), 9.1 Modo C: resolução de assets (+5 more)

### Community 58 - "train_v3"
Cohesion: 0.17
Nodes (6): amp_ctx(), benchmark_gpu_latency(), make_lpips(), select_v3_frames(), train_v3(), V3Loss

### Community 59 - "gen_recomp_config.py"
Cohesion: 0.29
Nodes (6): build_cfg_and_leads(), add_work(), format_hex_array(), in_code_regions(), is_valid_code_start(), main()

### Community 60 - "EcBplusOrt"
Cohesion: 0.28
Nodes (9): ec_hd_destroy_gpu(), ec_video_present_gpu_destroy(), ec_bplus_ort_destroy(), ec_bplus_ort_metrics(), ec_bplus_ort_readback(), ec_bplus_ort_set_intensity(), EcBplusOrt, ort_compute_percentiles() (+1 more)

### Community 61 - "time"
Cohesion: 0.31
Nodes (8): connect(), fi(), main(), rpc(), connect(), frame_info(), main(), rpc()

### Community 62 - "EcBplusOrtMetrics"
Cohesion: 0.14
Nodes (14): EcBplusOrtMetrics, fallback_frames, frames, input_height, input_width, last_gpu_ms, mean_gpu_ms, output_height (+6 more)

### Community 63 - "OrtExternalMemoryDescriptor"
Cohesion: 0.17
Nodes (12): OrtExternalMemoryDescriptor, handle_type, native_handle, offset_bytes, size_bytes, version, OrtExternalMemoryHandleType, ORT_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP (+4 more)

### Community 64 - "GVDP"
Cohesion: 0.29
Nodes (12): ec_hd_frame_active(), ec_draw_sprite_span(), ec_eval_full_sprites_line(), ec_fetch_plane_pixel(), ec_fetch_window_pixel(), ec_plane_tiles(), ec_refresh_frame_hud_sat(), ec_render_ws_scanline() (+4 more)

### Community 66 - "mixer_studio.py"
Cohesion: 0.13
Nodes (5): _decode_vlq(), read_midi_notes(), _encode_wav_bytes_from_pcm(), main(), read_midi_named_tracks()

### Community 67 - "Resumo do Harness Determinístico & Frame Data (`M3` / `M4`–`M6`)"
Cohesion: 0.22
Nodes (9): `01_attract_boot.ecm` (1000 quadros) — `byte-swapped-16bit (desfeito via i^1)`, `02_title_to_fight.ecm` (2900 quadros) — `byte-swapped-16bit (desfeito via i^1)`, 1. Autodeterminismo Nativo e Misses (Suíte Completa), 2. Paridade da Recompilação Estática (`Recomp` × `GENESIS_FORCE_INTERP=1`), 3. Cobertura do Elenco e Arenas + Paridade Native vs GPGX, 4. Progressão de Luta Ativa Observada (`02_title_to_fight.ecm`), 5. Validação Widescreen 16:9 (`game/ec_video.c` — Todas as Arenas), Checkpoints Detalhados — Boot (`01`) e Luta Principal (`02`) (+1 more)

### Community 68 - "1. Bloqueadores de gameplay"
Cohesion: 0.20
Nodes (10): 1. Bloqueadores de gameplay, [~] 1º load do savegame às vezes trava (carregar 2–3× libera), 2. Bugs já resolvidos (rastreabilidade), 3. Limitações conhecidas (não são bugs do jogo), 4. Pendências de escopo (não-bugs), BUG_QUEUE.md — Fila restante de bugs e pendências, [ ] Finalizador da Jetta — bug reportado, a reproduzir, [~] Sprites do EC falham nos DOIS ECs — só no round 1 do Trident (+2 more)

### Community 70 - "Tensor"
Cohesion: 0.31
Nodes (5): compute_ssim(), compute_ssim_tensor(), eval_v3(), rgb_to_rgba(), sobel_edges()

### Community 71 - "11. Ciclo 2026-11-21: Save/Load V2, Cold Reset, EC jogável, cenários off-label e fim de partida"
Cohesion: 0.33
Nodes (6): 11.1 Save/load V2 (`ECRB2sav`) e reset frio (F10), 11.2 Cenários off-label no VS 2P (Holo-Trainer + Santuário) — CONCLUÍDO, 11.3 Fim de partida nos estados off-label — CONCLUÍDO, 11.5 Arquitetura da máquina de estados mapeada (neste ciclo), 11.6 Limpeza e metodologia, 11. Ciclo 2026-11-21: Save/Load V2, Cold Reset, EC jogável, cenários off-label e fim de partida

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
Cohesion: 0.09
Nodes (29): _build_track_chunk(), _encode_vlq(), _FmChan, fnum_to_midi(), _meta_set_tempo(), _meta_track_name(), mul_to_factor(), _notes_to_track_chunk() (+21 more)

### Community 85 - "t2_alive.py"
Cohesion: 0.54
Nodes (6): connect(), fi(), main(), snap(), rd(), rpc()

### Community 86 - "t2_cram3.py"
Cohesion: 0.54
Nodes (6): connect(), dump(), fi(), main(), pause_at(), rpc()

### Community 87 - "t2_a6bisect.py"
Cohesion: 0.57
Nodes (5): a6(), connect(), fi(), main(), rpc()

### Community 88 - "gen_targets.py"
Cohesion: 0.43
Nodes (5): check(), gen(), load_manifest(), main(), save_manifest()

### Community 89 - "9. Fases 2–5: Recompilação Nativa, Harness, Widescreen e Extrator (2026-11-19)"
Cohesion: 0.33
Nodes (6): 9.2 Harness determinístico `.ecm` (`M3`) — `harness/ec_harness.py`, 9.3 Renderer `GameVideo`, Widescreen 16:9 e Modo C (`M4`–`M6`) — `docs/widescreen.md`, 9.4 Extrator, Pipeline ComfyUI HD (`M7`) e Destilação Neural B+ (`N1`–`N2`), 9.5 Backend CUDA/ONNX Runtime do Modo B+ (`P3` alternativo, 2026-11-20), 9.6 Remaster generativo Flux dos 437 assets (`M7.4`, 2026-11-20) — CONCLUÍDO, 9. Fases 2–5: Recompilação Nativa, Harness, Widescreen e Extrator (2026-11-19)

### Community 91 - "json"
Cohesion: 0.21
Nodes (10): connect(), fi(), main(), rpc(), main(), rpc(), connect(), fi() (+2 more)

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

### Community 101 - "t2_regdiff.py"
Cohesion: 0.60
Nodes (4): connect(), frame_info(), main(), rpc()

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

### Community 111 - "12. Ciclo 2026-11-21 (2ª rodada): fluxo do chefe (T1 ✅), training preto (T2) e sprites do Trident (T4)"
Cohesion: 0.40
Nodes (5): 12.1 T1 — Cenário do EC voltava ao menu após o 1º round — RESOLVIDO, 12.2 T2 — Training (Holo off-label) termina em tela preta — ✅ RESOLVIDO (2026-11-21), 12.3 T4 — Sprites do EC no round 1 do Trident — REPRO EM CONSTRUÇÃO, 12.4 T3 — 1º load do savegame — STAGING INTERATIVO IMPLEMENTADO (2026-11-21), 12. Ciclo 2026-11-21 (2ª rodada): fluxo do chefe (T1 ✅), training preto (T2) e sprites do Trident (T4)

### Community 112 - "4. Ferramentas dinâmicas"
Cohesion: 0.50
Nodes (4): 4.1 ares_cli (análise, base do P2), 4.2 gpgx_runner (referência do harness §8), 4.3 REA + Ghidra, 4. Ferramentas dinâmicas

### Community 113 - "tsf_channels"
Cohesion: 0.50
Nodes (4): tsf_channels, activeChannel, channelNum, channels

### Community 114 - "ec_on_post_reset"
Cohesion: 0.12
Nodes (16): 8. Fase 3: Validação determinística, Fase 3 — Validação Determinística (`M3`), Revisão do critério de aceite e diagnóstico de paridade (2026-11-19), 9.1 Recompilação estática (`M2`) — `build/native/EternalChampionsRecomp`, ec_audio_get_mode(), ec_boss_reset(), ec_boss_set_enabled(), ec_call_entry_point() (+8 more)

### Community 115 - "tsf_voice_lfo"
Cohesion: 0.50
Nodes (4): tsf_voice_lfo, delta, level, samplesUntil

### Community 116 - "TSFOutputMode"
Cohesion: 0.50
Nodes (4): TSFOutputMode, TSF_MONO, TSF_STEREO_INTERLEAVED, TSF_STEREO_UNWEAVED

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

### Community 123 - "OrtResourceCount"
Cohesion: 0.33
Nodes (6): FromTotalBytes(), None(), OrtResourceCount, kind, reserved, value

### Community 126 - "Eternal Champions — Build & Harness (Windows nativo)"
Cohesion: 0.33
Nodes (5): 1. Build nativo (`EternalChampionsRecomp.exe`), 2. Execução manual, 3. Validação determinística (`harness/ec_harness.py`), 4. Python / GPU (treino, upscalers, ferramentas), Eternal Champions — Build & Harness (Windows nativo)

### Community 127 - "tsf_stream_memory"
Cohesion: 0.50
Nodes (4): tsf_stream_memory, buffer, pos, total

### Community 128 - "ec_video_scanline_lazy"
Cohesion: 0.16
Nodes (20): 1. Princípio: 320 px na lógica, margem estendida só no renderer, 2. Política por Estado (`$FFFFFD64`), 3. Reposicionamento de HUD e Modo C, 4. Flags CLI e Resultados Medidos, 5. GW.3 — Flicker lateral e sprites descartados nas margens (causa-raiz e correção), Widescreen 16:9 / Adaptativo (`M4`–`M6`), ec_compute_viewport_x0(), ec_is_arena_state() (+12 more)

### Community 129 - "t2_probe.py"
Cohesion: 0.57
Nodes (5): connect(), frame_info(), main(), rpc(), wram()

### Community 135 - "ECStudentESPCN4x"
Cohesion: 0.13
Nodes (5): ECStudentESPCN4x, pre_dedither_torch(), ResBlock, run_infer(), torch_reference()

### Community 136 - "OrtKernelImpl"
Cohesion: 0.67
Nodes (3): OrtKernelImpl, flags, ort_version_supported

### Community 137 - "OrtDeviceMemoryType"
Cohesion: 0.67
Nodes (3): OrtDeviceMemoryType, OrtDeviceMemoryType_DEFAULT, OrtDeviceMemoryType_HOST_ACCESSIBLE

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
- **45 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `tsf_region` connect `tsf_region` to `tsf.h`?**
  _High betweenness centrality (0.048) - this node is a cross-community bridge._
- **Are the 2 inferred relationships involving `MixerApp` (e.g. with `3.4 Mixer Studio (`tools/audio/mixer_studio.py` + `tools/audio/mixer_ui/`)` and `NoteEvent`) actually correct?**
  _`MixerApp` has 2 INFERRED edges - model-reasoned connections that need verification._
- **What connects `OrtArenaAllocator`, `OrtDeviceAllocator`, `OrtInvalidAllocator` to the rest of the system?**
  _488 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `mixer_studio.py` be split into smaller, more focused modules?**
  _Cohesion score 0.06080370292446876 - nodes in this community are weakly interconnected._
- **Why does `MixerApp` connect `mixer_studio.py` to `mixer_studio.py`, `chip_ring_to_midi.py`, `Arquitetura de Áudio e Síntese SoundFont em Tempo Real — Eternal Champions`?**
  _High betweenness centrality (0.029) - this node is a cross-community bridge._
- **Should `ec_audio.c` be split into smaller, more focused modules?**
  _Cohesion score 0.06016929764355983 - nodes in this community are weakly interconnected._
- **Why does `tsf_hydra` connect `tsf_hydra` to `tsf.h`?**
  _High betweenness centrality (0.025) - this node is a cross-community bridge._