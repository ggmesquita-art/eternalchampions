# ROADMAP.md — EternalRecomp (v2.0)

> **Estado:** Reconstruído em **2026-11-23** a partir do histórico verificado (`docs/PROGRESS.md`, `docs/BUG_QUEUE.md`, commits até `939e69c` / `segagenesisrecomp@89e3bee`, working tree gráfico/áudio atual e `LAST_STATUS_AGY1.md`).
> **Fila executável ativa:** [`task.md`](task.md) (TASK.md) · **Arquitetura base:** [`DESIGN.md`](DESIGN.md) · **Widescreen:** [`docs/widescreen.md`](docs/widescreen.md).
> **Foco estratégico atual:** **(1) Aperfeiçoamento da Renderização Gráfica**, **(2) Suporte a Shaders Modulares** e **(3) Remasterização da Trilha Sonora**, preservando `0/65536` bytes de divergência na WRAM e 100% de fidelidade de gameplay/hitboxes.

---

## 1. Linha de Base Consolidada (O que já está pronto e validado)

Toda a fundação de engenharia reversa, recompilação nativa, determinismo e polimento de gameplay foi concluída:

| Marco | Escopo | Estado | Evidência / Referência |
|---|---|---|---|
| **M0–M1** | Setup, `vasm` 1.9d, Ghidra/REA, disassembly byte-a-byte (`373.204 B` de código 68k) | ✅ Concluído | `tools/verify.py` idêntico à ROM US `GM MK-1145` (CRC32 `0x48F1A42E`) |
| **M2–M3** | Recompilação estática 100% nativa (`EternalChampionsRecomp`) + harness determinístico `.ecm` | ✅ Concluído | `17/17` filmes `.ecm`: `det=0/65536`, `png_eq=True`, `misses=(0,0,0,0)`; arbitragem 4-vias (`BlastEm`/`GPGX`/`ares`/`Native`) |
| **M4–M6** | Renderer `GameVideo`, Widescreen `16:9` (`398×224`) / `21:9` (`512×224`), SAT nas margens, metatiles completos, HUD fixo, fim da linha preta (`y>=216`), compensação de parallax nas bordas e T-Rex ancorado | ✅ Concluído | `game/ec_video.c` (`c4ea32c`, `939e69c`); `wram_diff=0/65536` em todas as arenas |
| **M7 (v1)** | Extrator (`6.135` entradas), pipeline ComfyUI Flux HD (`437/437` aprovados), Modo C (`game/ec_hd.c`) | ✅ Infra pronta *(qualidade visual em revisão)* | Funcional em runtime, porém recortes 32×32 + silhueta `NEAREST` geram inconsistência visual → evoluir na **Trilha G** |
| **N1–N3** | Neural B+ v1 (`82k`, GL Compute + CUDA ORT), `--bplus-intensity`, Pré-dither GPU (`--bplus-dedither off\|cbod\|both`) e B+ v2 Flux HD (`196k`) | ✅ Base operacional *(evolução em curso)* | `neural/infer/ec_bplus_{gl,ort}.c`; Pre-Dither `+0.07 ms` async; B+ v2 `p50=17.86 ms` WSL D3D12 → otimizar e destilar v3 na **Trilha G** |
| **M9 (base)** | Emulação de Timer A do YM2612 (áudio nativo contínuo), `GEN_DEV_TRACE` → VGM/MIDI, protótipo offline A/B/C/D (3 arenas × 4 variantes) | ✅ Base + direção estética aprovada | `assets/audio_prototypes/` (synths aprovados; falta equalização por canal e ajuste de percussão em Blade → **Trilha A**) |
| **M10 & Bugfix** | Xbox 6-button plug-and-play, `--unlock-boss` completo (EC jogável + Overkills no EC + Santuário VS + Overkill do Vitral + Holo-Trainer VS sem flicker P2), Savegame V2 (`ECRB2sav`) 1º load corrigido, Energy Drain Midknight corrigido, Hotkey `F12` screenshot | ✅ Concluído | Commits `0f0a75b`..`939e69c` + `segagenesisrecomp@89e3bee` (+ `F12` e pacote gráfico no working tree atual) |

### Invariantes inegociáveis do projeto
1. **Gameplay e física intocados:** nenhuma melhoria gráfica, shader ou motor de áudio pode alterar a simulação 68k/Z80 — todo avanço exige `wram_diff = 0/65536` e regressão verde em `python3 harness/ec_harness.py`.
2. **Jogo base 100% autêntico por padrão:** todos os recursos de remasterização (widescreen, B+, shaders, Modo C, áudio remasterizado, `--unlock-boss`) são **opt-in** via CLI / `settings.ini`, com fallback automático seguro para modo nativo em ambientes headless/software.
3. **Sem motor paralelo de gameplay:** o visual HD opera estritamente sobre as coordenadas, flips, prioridades e máscaras reais do VDP, sem duplicar lógica de câmera ou hitboxes.

---

## 2. Visão Geral das Frentes Prioritárias (v2.0)

```
 [G0] Checkpoint / Commit da base atual (Pre-Dither + B+ v2 + Modo C + F12)
                      │
      ┌───────────────┼───────────────────────────────┐
      ▼               ▼                               ▼
  TRILHA G        TRILHA S                        TRILHA A
  Renderização    Suporte a Shaders               Remasterização da
  Gráfica HD      Modulares (GPU)                 Trilha Sonora
  ────────────    ────────────────                ─────────────────
  G1 Teacher Lab  S1 Engine Multi-Pass            A1 Auditoria FM/PSG
       │             + Parser Presets                + Fix Blade + Stems
       ▼               │                               │
  G2 B+ v3 Full   S2 Biblioteca Curada                 ▼
     Frame (Fast     (RCAS, CRT, ScaleFX,         A2 Captura Completa
     & HQ, 60fps)    De-Dither, Hotkey)              + Mixagem por Canal
       │               │                               │
       ▼               ▼                               ▼
  G3 Modo C       S3 CLI / settings.ini           A3 Engine de Áudio
     Híbrido         + Bench 1080p/4K                no Runner (DSP +
     (Planos HD)       │                             Híbrido / OST Loop)
       │               │                               │
  G4 Otimização        │                               │
     GL/CUDA           │                               │
      └───────────────┴───────────────┬───────────────┘
                                      ▼
                    TRILHA F — Finalização & Entrega
                    F1 Build Windows Nativo (x64 + revalidação GPU)
                    F2 --help + Documentação CLI / settings.ini
                    F3 Pause → Sair para Menu + Polimento Final
                    F4 Backlog de Baixa Prioridade (Holo Overkill,
                       bugfixes pontuais, Movelist, Sega CD M8)
```

---

## 3. Detalhamento dos 3 Pilares Prioritários

### PILAR 1 — Trilha G: Aperfeiçoamento da Renderização Gráfica

**Diagnóstico atual (por que o visual ainda não está no nível final):**
- O Modo C atual (`437` assets em `assets/hd/`) gera efeito "colcha de retalhos": cobre só parte dos quadros de luta, sprites foram gerados em recortes isolados de `32×32` sem pré-dither e `enforce_silhouette()` aplica máscara `NEAREST` (mantendo escadinha 4×4), enquanto alguns planos/céus ficaram excelentes.
- O B+ v1 (`82k` params) foi destilado de DAT2 antigo sem pré-dither; o B+ v2 (`196k` params, Flux HD) melhorou muito a suavidade mas roda a `p50 = 17.86 ms` no WSL Mesa D3D12 (acima do orçamento de `16.67 ms` para 60 fps) e ainda herdou o dataset de recortes antigos.
- O backend CUDA ORT (`ec_bplus_ort.c`) hoje faz ida-e-volta CPU↔GPU (`p50 = 13.20 ms`) e não suporta `--bplus-dedither` nem `--bplus-intensity != 1.0`.

**Fases da Trilha G:**
1. **G0 — Checkpoint & Higiene do Repositório:** revisar e commitar (após confirmação) o estado validado no working tree (`F12`, Pre-Dither GPU, B+ v2, Modo C infra) para garantir baseline limpo para trabalho paralelo.
2. **G1 — Laboratório Comparativo de Upscalers Teachers (Passo 1 imediato):**
   - Capturar 4 frames completos reais já com Pre-Dither `both` aplicado (`Vietnam S6`, `Salem S7`, `Sanctuary S2`, `Trident S5`).
   - Processar na RTX 5060 Ti contra os upscalers já presentes em `F:\ComfyUI\ComfyUI\models\upscale_models\`:
     `4x-AnimeSharp.pth`, `4x_IllustrationJaNai_V1_DAT2_190k.pth`, `4x_foolhardy_Remacri.pth`, `4x_NMKD-Siax_200k.pth`, `4x-UltraSharpV2.safetensors`, `4xNomos8kSCHAT-L.pth`.
   - Gerar pranchas lado a lado com zoom 1:1 (lutadores, rosto/roupas, céu, chão/sombras, HUD) em `assets/video_compare/upscalers_teacher/`.
   - **Gate Visual:** escolha do estilo vencedor pelo usuário antes de gerar dataset em massa.
3. **G2 — Destilação Modo B+ v3 em Tela Inteira (Uniformidade Total + 60 fps):**
   - Capturar dataset diversificado de centenas de frames reais completos com Pre-Dither `both` cobrindo todas as 11 arenas, os 10 lutadores (incluindo Eternal Champion), golpes/aéreos/especiais/overkills, telas de seleção e HUD.
   - Gerar ground truth 4× com o teacher aprovado em G1 (sem recortes 32×32, sem borda `NEAREST`).
   - Treinar em `neural/train_espcn.py` com função de perda orientada a suavidade/redução perceptual de pixels (L1 + perceptual LPIPS/VGG + gradiente/SSIM):
     - **`ec_bplus_v3_fast`** (`ch=36..40`, `nb=2..3`, meta `≤ 8–10 ms` no WSL D3D12 = **60 fps travados**).
     - **`ec_bplus_v3_hq`** (`ch=48..56`, `nb=4..6`, foco qualidade máxima em GPU nativa Windows / CUDA).
   - Validar paridade PyTorch ↔ GL Compute (`PSNR ≥ 60 dB`), zero WRAM diff e comparativo ao vivo.
4. **G3 — Refinamento do Modo C (`ec_hd.c`) — Modo Híbrido "Planos HD + B+ v3":**
   - Adicionar controles granulares (`--hd-mode off|planes|all`) em `game/ec_hd.c` e `game/ec_spec.c` para permitir usar os planos/céus 4× espetaculares sem depender de sprites parciais.
   - Regerar os 27 planos de arena com Pre-Dither aplicado **antes** do upscaler/Flux (eliminando o grão residual do céu do Trident `86cb2ec7b0cbbdd2`).
   - Opcional (se aprovado): atualizar `enforce_silhouette()` em `tools/pipeline/comfy_upscale.py` para borda alfa suave (anti-aliased) em vez de `NEAREST`.
5. **G4 — Otimização GPU, CUDA Zero-Copy & Modo B Widescreen:**
   - Otimizar shaders de `neural/infer/ec_bplus_gl.c` (tuning de workgroup, empacotamento FP16/`rgba16f` e fusão de passes) para reduzir o p50 do B+ v3 no WSL e no Windows.
   - Evoluir `neural/infer/ec_bplus_ort.c` para evitar cópia CPU↔GPU onde suportado (ou DirectML/Win32 nativo) e alinhar suporte a `--bplus-dedither` + `--bplus-intensity`.
   - Avaliar ativação do Modo B (`genesis_hd_gpu`) em `--widescreen` como alternativa pixel-crisp pura.

---

### PILAR 2 — Trilha S: Implementação de Suporte a Shaders Modulares

**Objetivo:** Permitir cadeia de shaders pós-processamento e upscale analítico em OpenGL (estilo RetroArch `.glslp`/`.slangp` enxuto), atendendo tanto **GPUs sem CUDA / máquinas leves** quanto **usuários de Neural B+ em 1440p/4K** (ex.: RCAS/CAS + CRT sutil no passe final).

**Fases da Trilha S:**
1. **S1 — Engine de Cadeia de Shaders Multi-Pass (`game/ec_shader_chain.{h,c}`):**
   - Pipeline GPU modular encaixado em `ec_video_present_gpu()` (`game/ec_video.c`) com 3 ganchos claros:
     1. **Pre-pass (nativo `320×224` / `398×224`)**: de-dither (`cbod`, `sgenpt-mix`, `mdapt`).
     2. **Upscale pass (alternativo ao Neural B+)**: shaders analíticos 2×/4× (`ScaleFX`, `xBRZ`, `FSR EASU`).
     3. **Post-pass (sobre saída 4× ou nativa)**: `AMD FidelityFX CAS/RCAS`, `CRT` (scanlines, máscara de fósforo, curvatura opcional, halation/bloom sutil) e ColorLUT.
   - Parser de presets em texto simples (`.ecshader` / subset compatível com `.glslp`): número de passes, caminho do `.glsl`, `filter_linear`, `scale_type` (`source`/`viewport`/`absolute`), `scale`, e uniformes padrão (`Source`, `Original`, `SourceSize`, `OriginalSize`, `OutputSize`, `FrameCount`) + parâmetros customizáveis (`#pragma parameter`).
   - Ping-pong de FBOs/texturas GL sem readback para CPU, com fallback limpo se o driver não suportar GL 3.3+ ou em modo headless.
2. **S2 — Biblioteca Curada de Shaders e Presets (`assets/shaders/`):**
   - `remaster-crisp.ecshader`: Pre-Dither (`both`) → B+ v3 → **RCAS/CAS** (nitidez cinematográfica para 1440p/4K sem halos).
   - `remaster-crt.ecshader`: Pre-Dither (`both`) → B+ v3 → **CRT sutil** (scanlines finas + máscara leve + bloom discreto).
   - `lite-scalefx.ecshader`: Pre-Dither (`both`) → **ScaleFX / xBRZ** → **CAS** (alternativa leve de alta qualidade para GPUs sem CUDA/neural).
   - `arcade-crt-trinitron.ecshader`: Pre-Dither (`both`) → **CRT-Lottes / CRT-Geom** + ColorLUT Trinitron (fidelidade arcade 1993).
   - `off`: bypass total.
3. **S3 — Integração CLI, `settings.ini`, Hotkey em Runtime e Validação:**
   - Flags `--shader-preset <nome|caminho>` e `--shader-param <chave=valor>` em `game/ec_spec.c` + persistência em `settings.ini`.
   - Atalho em runtime (ex.: `F9` / `Shift+F9`) para alternar presets ao vivo com notificação discreta no OSD/stderr (sem quebrar `F12` screenshot nem F7/F8 save/load).
   - Medição de latência por passe via `GL_TIME_ELAPSED` e prancha comparativa em `1080p`, `1440p` e `4K` (`assets/video_compare/shaders/`).

---

### PILAR 3 — Trilha A: Remasterização da Trilha Sonora

**Situação atual:**
- Protótipo offline de 3 arenas (`01_vietnam_midknight`, `02_salem_xavier`, `03_syria_blade`) validado em `assets/audio_prototypes/` (48 kHz estéreo, -16.5 dBFS RMS).
- **Feedback do usuário:** a pegada de synths híbridos (`C`/`D`) está aprovada; é necessário:
  1. Explicar detalhadamente como o mapeamento de canais/elementos funciona.
  2. Permitir **equalização e balanceamento individual por instrumento/elemento**.
  3. Corrigir o falso-positivo de percussão em **Syria/Blade** (um elemento tonal/rítmico foi classificado como bateria fora do ritmo).

**Fases da Trilha A:**
1. **A1 — Auditoria Elemento-a-Elemento do Driver Z80 & Correção do Mapeamento (Blade + Pilotos):**
   - Documentar em `docs/audio_remaster_architecture.md` a anatomia real do driver Z80 (`$000A80`) e dos canais YM2612 (6 canais FM + DAC `$2A`) + SN76489 (3 tons + ruído), incluindo a separação entre BGM e SFX em `disasm/code/sound_cmd.asm` (`$0008A8–$000A7F`).
   - Auditar canal por canal `03_syria_blade` (e `vietnam`/`salem`) no `chip_ring`, identificando exatamente qual canal/patch foi confundido com percussão em Blade e corrigindo o classificador em `tools/audio/`.
   - Exportar **stems isolados** por canal/papel (ex.: `bass`, `pad`, `lead`, `arp`, `perc_fm`, `perc_dac`, `psg_tone`, `psg_noise`) + gerar arquivo de receita editável por faixa (`assets/audio_remaster/profiles/<track>.toml`) com ganho, EQ (low/mid/high), pan, patch SoundFont/synth e regra de percussão independentes.
2. **A2 — Pipeline de Produção de Todas as Faixas & Revisão A/B:**
   - Catalogar a tabela completa de IDs musicais da ROM (Título, Seleção/Menu, 9 Arenas principais, Santuário/Boss, Holo-Trainer, Overkill, Vitória/Finais/Créditos) via `snd_ring.txt` + `chip_ring_to_vgm.py`.
   - Criar ferramenta determinística `tools/audio/remaster_track.py` (lendo o perfil `.toml` da faixa + SoundFont(s) de alta qualidade / synths dedicados + cadeia DSP de masterização) para iterar rapidamente a equalização individual com o usuário.
   - Validar de ouvido com o usuário primeiro as 3 faixas piloto refinadas, depois produzir o pacote completo de faixas remasterizadas com **pontos exatos de loop** (intro + loop sem clique).
3. **A3 — Engine de Áudio Remasterizado em Tempo Real no Runner (`game/ec_audio.{h,c}` + `segagenesisrecomp/runner/audio/`):**
   - Implementar interceptação passiva dos comandos BGM no barramento 68k→Z80 (`sound_cmd.asm` — leitura pura, zero alteração de estado/timing do Z80 → `0/65536` WRAM diff).
   - Implementar dois módulos combináveis na saída do host (antes/integrado ao `recomp_audio_drc.h`):
     - **DSP de Estúdio em tempo real (`--audio-dsp`):** EQ musical, controle de médios FM, abertura estéreo controlada e reverb de sala leve aplicados ao YM2612+PSG (beneficia inclusive SFX e modo autêntico).
     - **Player Híbrido / OST Sincronizado (`--audio-remaster off|dsp|hybrid|ost`):** reproduz a faixa remasterizada 48 kHz (OGG/FLAC/WAV com loop sample-accurate, pausa sincronizada com `$FD68`, fade-out sincronizado com comandos Z80) em camada paralela (`hybrid` somando sobre o chip original) ou substituição limpa da BGM preservando 100% dos SFX e vozes DAC originais (via mute seletivo de canais musicais ou ducking limpo).
   - Controles via CLI e `settings.ini` (`audio_mode`, `audio_bgm_vol`, `audio_sfx_vol`, `audio_chip_blend`, `audio_dsp_reverb`) + regressão determinística 17/17.

---

## 4. Trilha F — Finalização, Port Windows e Backlog de Baixa Prioridade

Itens deliberadamente mantidos no final do ciclo para não atrasar os 3 pilares principais:

| ID | Item | Escopo técnico | Prioridade |
|---|---|---|---|
| **F1** | **Compilação Nativa Windows (x64)** | Portar `ec_bplus_ort.c` (`dlopen` → `LoadLibraryA` / `onnxruntime.dll`), validar `libpng`/SDL2/OpenGL/FluidSynth ou decoder OGG/WAV no Windows (MSVC/MinGW), revalidar latência real de B+ v3 e Shaders fora do WSL Mesa D3D12 (meta `≤ 4 ms`). | Média (após G2/S2) |
| **F2** | **CLI `--help` & Documentação Completa** | Implementar `--help`/`-h` em `segagenesisrecomp/runner/main.c` exibindo runner + `g_game_spec.arg_usage`; consolidar referência completa de CLI e `settings.ini`. | Baixa (finalização) |
| **F3** | **PAUSE → Sair para o Menu** | Mini-menu visual seguro (`RESUME / EXIT TO MENU`) no `Main_PauseLoop` via hook nativo chamando fade + `State_SetState(1)` sem patch de ROM. | Baixa (finalização) |
| **F4.1** | **Overkill da Arena de Treino (State 14)** | Protótipo temático opcional sob `--unlock-boss` (robô de projeção / moving heads desintegrando ou eletrocutando no centro da arena). | Baixa |
| **F4.2** | **Bugfixes Pontuais Restantes** | (a) Recarga de sprite pós-explosão no Santuário quando vencedor está muito próximo do vitral; (b) reproduzir eventual bug no Overkill da Jetta (`docs/BUG_QUEUE.md`); (c) auditar Estado 14 1P original e stall DMA de `3402866`. | Baixa |
| **F4.3** | **Extras de Longo Prazo** | Extração completa de movelists (`docs/movements/`), menu in-game amigável (controles/shaders/áudio) e Expansão Sega CD (*Challenge from the Dark Side*, `M8`). | Futuro |

---

## 5. Plano de Orquestração Paralela (Orca DAG)

As 3 trilhas principais têm fronteiras de arquivo bem isoladas na maior parte do trabalho, permitindo execução em paralelo via workers Orca com pontos de sincronização explícitos:

| Worker / Frente | Escopo Exclusivo (sem conflito) | Ponto de Convergência (Merge) |
|---|---|---|
| **Worker G (Gráficos ML/Assets)** | `neural/train_espcn.py`, `neural/export_onnx.py`, `tools/pipeline/`, `assets/video_compare/`, `assets/models/` | `game/ec_video.c`, `game/ec_hd.c`, `game/ec_spec.c` (apenas após G1/G2 aprovados) |
| **Worker S (Shaders GPU)** | `assets/shaders/`, `game/ec_shader_chain.{h,c}` (novos arquivos) | Integração curta em `game/ec_video.c`, `game/ec_spec.c`, `game/CMakeLists.txt`, `segagenesisrecomp/runner/main.c` |
| **Worker A (Áudio & Remaster)** | `tools/audio/`, `docs/audio_remaster_architecture.md`, `assets/audio_remaster/`, `game/ec_audio.{h,c}` (novos arquivos) | Gancho de leitura Z80 em `game/ec_spec.c` / `segagenesisrecomp/runner/main.c` + `game/CMakeLists.txt` |

**Gates obrigatórios antes de qualquer merge na branch principal:**
1. **Gate Visual/Auditivo:** aprovação do usuário nas pranchas (`G1`, `G2`, `S3`) e amostras WAV (`A1`, `A2`).
2. **Gate de Build:** `cmake --build build/native -j` sem erros nem warnings novos.
3. **Gate de Determinismo:** `python3 harness/ec_harness.py` (ou `--quick` durante iterações e suíte `17/17` no fechamento de cada trilha) com `det=0/65536`, `wram_diff=0/65536` e `misses=(0,0,0,0)`.
