# TASK.md — Fila Executável Ativa (EternalRecomp v2.0)

> **Roadmap estratégico:** [`ROADMAP.md`](ROADMAP.md) · **Último log de sessão:** [`LAST_STATUS_AGY1.md`](LAST_STATUS_AGY1.md) · **Progresso histórico:** [`docs/PROGRESS.md`](docs/PROGRESS.md).
> **Foco ativo:** Renderização Gráfica (`G`), Suporte a Shaders (`S`) e Remasterização da Trilha Sonora (`A`).
> **Regra de validação:** Toda alteração em C/runtime exige build limpo (`cmake --build build/native -j`) + `python3 harness/ec_harness.py --quick` (e suíte `17/17` no fechamento de cada etapa) com `wram_diff=0/65536`.

---

## 0. Pré-requisito / Checkpoint Imediato (Higiene de Git)

- [ ] **G0.1 — Revisar e commitar o pacote validado do working tree atual** *(aguarda autorização do usuário)*:
  - Submódulo `segagenesisrecomp/`: `runner/main.c` (Hotkey `F12` screenshot em `screenshots/` + `Shift/Ctrl+F12` preservando dump dev) e `runner/glue.c`.
  - Repo principal: Pré-dither GPU (`neural/infer/ec_bplus_gl.{h,c}`), treino/export B+ v2 (`neural/train_espcn.py`, `neural/export_onnx.py`), Modo C (`game/ec_hd.{h,c}`, `game/ec_video.{h,c}`, `game/ec_spec.c`, `game/CMakeLists.txt`), backend CUDA ORT (`neural/infer/ec_bplus_ort.{h,c}` + `ort/`), `ROADMAP.md` e `task.md`.
  - Decidir sobre `docs/ROADMAP.md` (cópia legada v0.1 não rastreada).

---

## 1. Pilar 1 — Aperfeiçoamento da Renderização Gráfica (`Trilha G`)

### Etapa G1 — Laboratório Visual de Upscalers Teachers *(Próximo passo imediato — sem alterar código do jogo)*
- [ ] **G1.1** — Capturar 4 frames nativos reais completos já com Pre-Dither `both` aplicado (`Vietnam S6`, `Salem S7`, `Sanctuary S2`, `Trident S5`) cobrindo personagens, sombras translúcidas, céu, fogo e HUD.
- [ ] **G1.2** — Processar os 4 frames na RTX 5060 Ti com os upscalers já instalados em `F:\ComfyUI\ComfyUI\models\upscale_models\`:
  - `4x-AnimeSharp.pth`
  - `4x_IllustrationJaNai_V1_DAT2_190k.pth`
  - `4x_foolhardy_Remacri.pth`
  - `4x_NMKD-Siax_200k.pth`
  - `4x-UltraSharpV2.safetensors`
  - `4xNomos8kSCHAT-L.pth`
- [ ] **G1.3** — Montar pranchas comparativas lado a lado com zoom 1:1 (lutadores, rosto/roupas, céu, chão e HUD) em `assets/video_compare/upscalers_teacher/` e comparar contra B+ v1 e B+ v2.
- [ ] **G1.4 [GATE USUÁRIO]** — Escolher visualmente o upscaler teacher vencedor (para tela inteira / sprites e, se diferente, para planos/céus).

### Etapa G2 — Modo B+ v3 Full-Frame Distilled (100% da tela uniforme + 60 fps)
- [ ] **G2.1** — Criar script de coleta de dataset full-frame (`neural/collect_dataset.py`) capturando centenas de frames reais variados (todas as 11 arenas, todos os 10 lutadores incluindo EC, poses em pé/aéreas/golpes/especiais/overkills, menus e HUD) com Pre-Dither `both`.
- [ ] **G2.2** — Gerar targets 4× sobre todo o dataset usando o upscaler teacher aprovado em **G1.4** (sem recortes 32×32 e sem borda `NEAREST`).
- [ ] **G2.3** — Aprimorar `neural/train_espcn.py` com perda perceptual (VGG/LPIPS ou perceptual + L1 + gradiente/SSIM) voltada para suavização real de contornos e redução da pixelação interna.
- [ ] **G2.4** — Treinar e exportar duas variantes lado a lado (sem sobrescrever v1/v2):
  - `assets/models/ec_bplus_v3_fast_weights.bin` (`ch=36..40`, `nb=2..3`, alvo `p50 ≤ 8–10 ms` no WSL D3D12 = 60 fps travados).
  - `assets/models/ec_bplus_v3_hq_weights.bin` (`ch=48..56`, `nb=4..6`, foco qualidade máxima em GPU nativa).
- [ ] **G2.5** — Validar paridade PyTorch ↔ ONNX ↔ GL Compute (`PSNR ≥ 60 dB`), `wram_diff=0/65536`, benchmark 120 frames e prancha comparativa v1 × v2 × v3.

### Etapa G3 — Modo C Híbrido (Planos HD limpos + Fallback B+ v3)
- [ ] **G3.1** — Adicionar controle granular `--hd-mode <off|planes|all>` em `game/ec_hd.{h,c}`, `game/ec_video.{h,c}` e `game/ec_spec.c` (permitindo usar apenas os planos/céus 4× sem o efeito colcha de retalhos de sprites parciais).
- [ ] **G3.2** — Regerar os 27 planos de arena (`assets/hd/planes/`) passando Pre-Dither `both` **antes** do upscaler/Flux (corrigindo especificamente o grão residual do céu do Trident `86cb2ec7b0cbbdd2`).
- [ ] **G3.3** — *(Opcional, se optar por manter sprites no Modo C)* Corrigir `enforce_silhouette()` em `tools/pipeline/comfy_upscale.py` para máscara alfa suave (anti-aliased) em vez de `NEAREST` 4×4.

### Etapa G4 — Otimização GPU (OpenGL Compute & CUDA ORT)
- [ ] **G4.1** — Otimizar `neural/infer/ec_bplus_gl.c` (workgroups, uso consistente de `rgba16f`/FP16, redução de barreiras/passes) para reduzir a latência de B+ v2/v3 no WSL Mesa D3D12 e Windows.
- [ ] **G4.2** — Evoluir `neural/infer/ec_bplus_ort.c` para suportar `--bplus-dedither` e `--bplus-intensity`, e avaliar eliminação do round-trip CPU↔GPU (GL-CUDA interop ou DirectML no Windows).
- [ ] **G4.3** — Habilitar Modo B (`genesis_hd_gpu`) compatível com `--widescreen` quando B+ estiver desligado.

---

## 2. Pilar 2 — Suporte a Shaders Modulares (`Trilha S`)

### Etapa S1 — Engine de Cadeia de Shaders Multi-Pass (OpenGL)
- [ ] **S1.1** — Projetar e implementar `game/ec_shader_chain.{h,c}` com pipeline FBO ping-pong em OpenGL 3.3+ encaixado no fluxo `ec_video_present_gpu()` (`game/ec_video.c`), suportando estágios pré-upscale (nativo), upscale analítico e pós-upscale (saída 4× / viewport).
- [ ] **S1.2** — Implementar parser leve de presets `.ecshader` (subset compatível com `.glslp` do RetroArch: `shaders`, `shaderN`, `filter_linearN`, `scale_typeN`, `scaleN`, `#pragma parameter`, uniformes `Source`, `Original`, `SourceSize`, `OriginalSize`, `OutputSize`, `FrameCount`).
- [ ] **S1.3** — Garantir fallback silencioso em headless/software (`SDL_VIDEODRIVER=dummy`) e `wram_diff=0/65536`.

### Etapa S2 — Biblioteca Curada de Shaders & Presets (`assets/shaders/`)
- [ ] **S2.1** — Portar/implementar shaders base em GLSL:
  - Pré-passe de-dither (`cbod.glsl`, `sgenpt_mix.glsl`)
  - Upscale analítico leve para GPUs sem CUDA (`scalefx.glsl` ou `xbrz_4x.glsl` + `fsr_easu.glsl`)
  - Pós-passe de nitidez sem halo (`cas_rcas.glsl` — AMD FidelityFX CAS/RCAS para 1440p/4K)
  - Pós-passe CRT/Arcade (`crt_subtle.glsl` e `crt_lottes.glsl` com scanlines, máscara de fósforo e bloom/halation configuráveis)
- [ ] **S2.2** — Criar 5 presets prontos em `assets/shaders/presets/`:
  - `remaster-crisp.ecshader` (Pre-Dither → B+ v3 → RCAS)
  - `remaster-crt.ecshader` (Pre-Dither → B+ v3 → CRT sutil)
  - `lite-scalefx.ecshader` (Pre-Dither → ScaleFX/xBRZ → CAS — sem neural/CUDA)
  - `arcade-crt-trinitron.ecshader` (Pre-Dither → CRT-Lottes + ColorLUT Trinitron)
  - `off` (bypass)

### Etapa S3 — Integração CLI, `settings.ini`, Hotkey e Benchmark
- [ ] **S3.1** — Adicionar `--shader-preset <nome|caminho>` e `--shader-param <chave=valor>` em `game/ec_spec.c` e suporte no `settings.ini`.
- [ ] **S3.2** — Adicionar hotkey em runtime (`F9` / `Shift+F9`) em `segagenesisrecomp/runner/main.c` para ciclar presets ao vivo com log/OSD, sem conflitar com `F7/F8` (save/load), `F10` (cold reset) e `F12` (screenshot).
- [ ] **S3.3** — Medir latência por passe (`GL_TIME_ELAPSED`) e gerar prancha comparativa em `1080p`, `1440p` e `4K` em `assets/video_compare/shaders/`.

---

## 3. Pilar 3 — Remasterização da Trilha Sonora (`Trilha A`)

> **Diretriz da Trilha A:** Novo módulo de áudio em tempo real (`game/ec_audio.{h,c}`) com DSP custo-zero aplicado também a SFX/vozes não-MIDI do chip original (sem investimento em remasterizar sons não-MIDI) + **síntese SoundFont2 em tempo real** (`TinySoundFont` em `game/third_party/tsf.h` + `assets/soundfonts/TimGM6mb.sf2`) alimentada por observer passivo de registradores FM/PSG (`audio_mixer_set_chip_observer` em `segagenesisrecomp/runner/audio/mixer.{h,c}`) + perfis `.toml` por `bgm_id` (sem WAVs pré-renderizados no jogo) + ferramenta externa (`tools/audio/mixer_studio.py`) para ajustar mixagem (**volume** e **tipo de instrumento**) de cada canal por música.

### Etapa A1 — Auditoria FM/PSG, Correção de Blade, Stems & Motor de Remasterização (`Task A1-pipeline`)
- [x] **A1.1** — Escrever `docs/audio_remaster_architecture.md` detalhando o driver Z80 (`$000A80`), canais YM2612 (`FM0..FM5` + `DAC $2A`), SN76489 (`PSG0..PSG2` + `PSG_NOISE`), interface `sound_cmd.asm` (`$0008A8–$000A7F`), causa-raiz do bug de percussão em Blade (`chip_ring_to_midi.py:200-224`), schema dos perfis `.toml` e arquitetura de síntese SoundFont em tempo real + DSP custo-zero.
- [x] **A1.2** — Corrigir o mapeamento em `tools/audio/chip_ring_to_midi.py` separando fielmente canais YM2612/PSG/DAC (sem desviar notas FM curtas tonais para bateria, corrigindo `03_syria_blade`) e criar `tools/audio/export_stems.py` para exportar stems MIDI/WAV isolados por canal (`fm0..fm5`, `dac`, `psg0..psg2`, `psg_noise`) e gerar perfis editáveis em `assets/audio_remaster/profiles/<track>.toml`.
- [x] **A1.3** — Implementar `tools/audio/remaster_track.py` lendo perfil `.toml` + SoundFont (`.sf2`) para renderizar stems individuais (preview na ferramenta de mixagem) e validar os perfis `.toml` usados pelo motor em tempo real.

### Etapa A2 — Ferramenta Externa de Mixagem por Instrumento & Catálogo (`Task A2-mixer-tool`)
- [x] **A2.1** — Implementar `tools/audio/mixer_studio.py` (servidor HTTP local Python stdlib + SPA HTML/CSS/JS com Web Audio API, compatível com Windows e WSL):
  - Seletor de música (`assets/audio_remaster/profiles/*.toml`) + alternância A/B (`Original` vs `Remaster`).
  - Controle por canal (`FM0..FM5`, `DAC`, `PSG0..PSG3`): **volume (dB)**, **tipo/instrumento** (patches GM 0–127 categorizados, percussão, `chip`, `off`), **pan**, **mute/solo** com ajuste de volume/mix instantâneo no navegador.
  - Ações: re-renderizar canal ao trocar instrumento e salvar perfil `.toml` em `assets/audio_remaster/profiles/`.
- [x] **A2.2 [GATE USUÁRIO]** — Validar fluxo nas 3 faixas piloto (`Vietnam`, `Salem`, `Syria/Blade` corrigida) e ajustar mixagem via `mixer_studio.py`.
- [x] **A2.3** — Catalogar todos os IDs BGM da ROM em perfis `.toml` dedicados em `assets/audio_remaster/profiles/` (com fallback automático em `default.toml` para faixas ainda sem perfil específico).

### Etapa A3 — Síntese SoundFont em Tempo Real no Runner + DSP Custo-Zero (`Tasks A3-engine` & `A3-integration`)
- [x] **A3.1** — Implementar módulo standalone em C11 (`game/ec_audio.{h,c}` + `game/third_party/tsf.h`, zero dependências externas novas no CMake; compatível com MSVC Windows e GCC/Clang WSL):
  - **DSP de estúdio custo-zero** (EQ 3 bandas biquad, abertura estéreo sutil, reverb de sala Schroeder decimado e soft-limiter) aplicado sobre o barramento final (chip original + síntese SF2) — melhora BGM e todos os SFX/vozes não-MIDI sem custo adicional.
  - **Sintetizador SoundFont2 em tempo real (`TinySoundFont` 48 kHz)** com tradutor hardware-exact YM2612 + SN76489 sub-frame via `AudioChipWriteObserver` (`audio_mixer_set_chip_observer` em `mixer.{h,c}`), perfis `.toml` por `bgm_id` (e `default_profile` fallback para savestates diretos), fade-out suave e pause/resume sem cliques, mantendo 100% dos SFX/vozes PCM `$2A` no barramento chip.
  - Modos: `--audio-remaster off|dsp|hybrid|ost` (`off` padrão 100% autêntico).
- [x] **A3.2** — Integrar `game/ec_audio.{h,c}` em `game/game.toml` (hooks passivos `[[widescreen_site]] kind = "game_hook"` retornando `0` em `$0008B8`, `$000920`, `$00092E`, `$00093C`, `$000A28`), `segagenesisrecomp/runner/audio/mixer.{h,c}` (`audio_mixer_set_chip_observer`), `game/ec_spec.c` (`audio_mixer_set_chip_observer()`, `audio_set_extra_source()`, flags CLI `--audio-remaster`, `--audio-sf2`, `--audio-profiles-dir`, `--audio-bgm-vol`, `--audio-sfx-vol`, `--audio-chip-blend`, `--audio-dsp-reverb`, `ec_on_post_reset`) e `game/CMakeLists.txt`.
- [x] **A3.3** — Compilar binário e validar determinismo (`wram_diff=0/65536`, zero cliques/underruns no DRC) + modos `off`/`dsp`/`hybrid`/`ost` com captura `--wav`.

### Etapa A4 — Extração Completa dos 17 MIDIs da ROM & Mixer 100% MIDI via `tsf.h` (Zero WAVs em Disco)
- [x] **A4.1** — Adicionar suporte ao formato `GENESIS_AUDIO_APPLIEDDUMP` (`APP FM` / `APP PSG`) em `tools/audio/chip_ring_to_midi.py` e criar `tools/audio/extract_all_midis.py` para capturar automaticamente os **17 BGMs da ROM (`bgm_id 0..16`)** direto do executável nativo `build/EternalChampionsRecomp.exe` (via Music Test / fluxo de menu, áudio limpo sem SFX/vozes) gerando os 17 arquivos MIDI Tipo 1 multi-canal consolidados em `assets/audio_remaster/midi/<slug>.mid`.
- [x] **A4.2** — Criar e compilar biblioteca nativa leve `tools/audio/tsf_midi_synth.c` → `tools/audio/tsf_midi_synth.dll` (usando o mesmo `game/third_party/tsf.h` + `assets/soundfonts/TimGM6mb.sf2` do jogo) para sintetizar canais MIDI diretamente em memória RAM (suportando `auto`, `gm`, `percussion`, `transpose`, `bank`, `program`) sem depender de FluidSynth/WSL nem gravar WAVs em disco.
- [x] **A4.3** — Migrar `tools/audio/mixer_studio.py` e `tools/audio/mixer_ui/{index.html,style.css,app.js}` para operar 100% sobre `assets/audio_remaster/midi/*.mid` + `assets/audio_remaster/profiles/*.toml` via `tsf_midi_synth.dll` em memória (com download do `.mid` e botão principal **Salvar Perfil `.toml`**), remover completamente a pasta obsoleta `assets/audio_remaster/stems/` (zero `.wav` em disco) e atualizar `docs/audio_remaster_architecture.md`.
- [x] **A4.4** — Revisão independente (`revisor`) + validação fim-a-fim: conferir os 17 `.mid` válidos (notas, canais, zero notas agudas espúrias `>=96`), testar síntese em memória de todos os 17 BGMs via HTTP no `mixer_studio.py`, confirmar `0` WAVs em `assets/audio_remaster/` e manter `wram_diff=0/65536` no executável do jogo.

---

## 4. Trilha F — Finalização, Port Windows e Backlog de Baixa Prioridade

- [x] **F1 — Build Windows Nativo (x64):**
  - Portar `neural/infer/ec_bplus_ort.c` (`dlopen` → `LoadLibraryA` / `onnxruntime.dll`).
  - Validar `game/CMakeLists.txt` (SDL2, libpng, OpenGL, decoder de áudio) em compilação Windows nativa.
  - Revalidar latência real de B+ v3 + Shaders em GPU nativa Windows (fora do overhead WSL Mesa D3D12).
- [x] **F2 — CLI `--help` & Documentação Consolidada:**
  - Implementar `--help` / `-h` em `segagenesisrecomp/runner/main.c` listando flags do runner + `g_game_spec.arg_usage`.
  - Consolidar guia completo de flags CLI e todas as chaves de `settings.ini` em `docs/CONFIGURATION_AND_CLI.md`.
- [ ] **F3 — PAUSE → Sair para o Menu:**
  - Implementar mini-menu visual seguro (`RESUME / EXIT TO MENU`) no `Main_PauseLoop` via hook nativo sem patch na ROM.
- [ ] **F4 — Itens de Baixa Prioridade & Bugfixes Pontuais:**
  - [ ] Corrigir recarga de sprite pós-explosão no Overkill do Santuário quando o vencedor (ex.: Eternal Champion) está muito próximo do vitral no disparo.
  - [ ] Prototipar Overkill customizado da Arena de Treino (State 14 / Holo-Trainer: robô da projeção / moving heads no centro).
  - [ ] Validar fluxo 1P Holo-Trainer original (Estado 14 sem `--unlock-boss`) e revisar manutenção/reversão do stall DMA de `3402866`.
  - [ ] Extração completa de movelists em `docs/movements/`.
  - [ ] Expansão Sega CD (*Challenge from the Dark Side*, `M8` — adiada até conclusão do cartucho Genesis).

---

## 5. Arquivo Histórico — Marcos Concluídos (M0–M10 + Bugfix/Polish)

- [x] **M0–M1:** Setup, `vasm` 1.9d, Ghidra/REA, disassembly reconstruível 100% byte-a-byte (`373.204 B`), `ram_map.toml`, arbitragem 4-vias (`BlastEm`/`GPGX`/`ares`/`Native`), mapeamento de VDP Reg 11 H-Scroll.
- [x] **M2–M3:** Recompilação estática 100% nativa (`build/native/EternalChampionsRecomp`), suíte `17/17` `.ecm` (`det=0/65536`, `png_eq=True`, `misses=(0,0,0,0)`), correção do fim de partida e Duff's device `$05E242`.
- [x] **M4–M6:** `ec_video.c` Modo A + Widescreen `16:9`/`21:9` (`0/65536` WRAM diff), SAT nas margens (`Microtarefa 1A`), fallback de metatiles + ancoragem de bordas + T-Rex esquerdo (`Microtarefa 1B`), HUD fixo no topo + remoção da linha preta inferior + compensação de parallax nas bordas (`Etapa 1A`, commit `939e69c`).
- [x] **M7 / N1–N3 (v1–v2):** Extrator `6.135` entradas, `437/437` assets Flux HD aprovados, B+ v1 (`82k` GL Compute + CUDA ORT) + `--bplus-intensity`, Pré-dither GPU (`--bplus-dedither off|cbod|both`), B+ v2 Flux HD (`196k`) e infraestrutura Modo C (`ec_hd.c`).
- [x] **M9 (base):** Timer A do YM2612 em `genesis_bus.c`, `GEN_DEV_TRACE` → VGM/MIDI, protótipo offline de 12 WAVs em `assets/audio_prototypes/` (direção de synths aprovada).
- [x] **M10 & Bugfix (`0f0a75b`..`c4ea32c` + `89e3bee`):** Xbox 6-button, T1 (Santuário VS Best-of-N), T2 (tela preta Training / `m68k-recomp-core 2666e9f`), T3 (Savegame V2 1º load), T4 (Trident R1 EC sprites), T5 (Overkills no EC), Holo-Trainer VS Best-of-N + sem flicker P2, Energy Drain Midknight (`0x04EDD4` + sanitização saves 4/9), Overkill customizado do Vitral no Santuário, Hotkey `F12` screenshot.
