# Guia de Configuração, Linha de Comando (CLI) e Runtime — Eternal Champions (EternalRecomp)

Este documento é a referência oficial, exaustiva e consolidada de configuração, flags de linha de comando, variáveis de ambiente, seções do arquivo `settings.ini` e controles de teclado/gamepad para o executável **EternalRecomp** (*Eternal Champions Native GenesisRecomp*).

---

## 1. Visão Geral dos Binários

O projeto suporta compilação e execução nativa em dois ambientes principais, preservando paridade de estado e determinismo de simulação de hardware:

| Binário | Plataforma Alvo | Toolchain / Backend | Tecnologias Utilizadas |
|---|---|---|---|
| `build/EternalChampionsRecomp.exe` | **Windows Nativo (x64)** | MSVC (Visual Studio 2022) + CMake (Ninja) | SDL2 nativo, Direct3D11/12, OpenGL 4.3+, ONNX Runtime nativo (`onnxruntime.dll`), XInput/DirectInput |
| `build/native/EternalChampionsRecomp` | **Linux / WSL (x64)** | GCC / Clang + CMake (Ninja) | SDL2 (X11 / Wayland), Mesa D3D12/OpenGL, ALSA / PulseAudio |

Ambos os binários operam com recompilação estática 100% C do código Motorola 68000 original do cartucho Sega Genesis/Mega Drive (`eternal_champions_us.gen`), acoplados a módulos nativos de vídeo com suporte widescreen em tempo real (`game/ec_video.c`), motor neural B+ v1/v2/v3 (`neural/infer/`), síntese SoundFont em tempo real (`game/ec_audio.c`) e persistência determinística.

---

## 2. Variáveis de Ambiente

As variáveis de ambiente permitem configurar drivers de baixo nível da biblioteca SDL2, comportamento do runtime base do emulador (*GenesisRecomp*) e módulos especializados de *Eternal Champions*.

### 2.1 Variáveis SDL2 (Vídeo, Renderizador e Áudio)

| Variável | Valores Suportados | Descrição |
|---|---|---|
| `SDL_VIDEODRIVER` | `windows`, `x11`, `wayland`, `dummy` | Define o driver de subsistema de vídeo da SDL2. Utilize `dummy` para execução headless (CI/testes automatizados sem servidor gráfico). |
| `SDL_RENDER_DRIVER` | `direct3d11`, `direct3d12`, `opengl`, `opengles2`, `software` | Força a API gráfica do backend de renderização da SDL2. No Windows nativo, `direct3d11` e `opengl` oferecem a melhor latência e estabilidade. |
| `SDL_AUDIODRIVER` | `wasapi`, `directsound`, `alsa`, `pulseaudio`, `dummy` | Seleciona o driver de áudio host. No Windows moderno, `wasapi` fornece a menor latência de buffer; `dummy` silencia o áudio e elimina bloqueios de dispositivo em testes. |

### 2.2 Variáveis do Runtime Base (*GenesisRecomp*)

| Variável | Valores | Descrição |
|---|---|---|
| `GENESIS_AUDIO_APPLIEDDUMP` | `<caminho.raw>` | Despeja o stream PCM estéreo gerado após todo o processamento e mixagem diretamente no arquivo indicado em tempo real. |
| `GENESIS_YIELD_LOG` | `1` ou `0` | Habilita a escrita de `yield_log_native.log` com uma linha por sincronização de V-Blank (`WaitForVBla`), contendo frame, acumulador de ciclos e rotina de interrupção. |
| `GENESIS_EVIDENCE_FRESH` | `1` ou `0` | Quando ativado (`1`), limpa arquivos de evidência anteriores (`dispatch_misses.toml`, `interior_label_misses.toml`), movendo-os para backup `.prev.toml`. |
| `GENESIS_NO_LAUNCHER` | `1` ou `0` | Força a inicialização direta no jogo, ignorando a interface gráfica de launcher mesmo se configurada para abrir. |
| `GENESIS_INTERNAL_SCALE` | `1` a `64` | Multiplicador de escala de densidade interna de renderização de GPU. |
| `GENESIS_WIDESCREEN` | `1` ou `0` | Habilita viewport widescreen nativo estendido do VDP legado. |
| `GENESIS_WIDESCREEN_COLUMNS` | `<inteiro>` | Número de colunas de 8 pixels adicionadas nas bordas no modo widescreen VDP. |

### 2.3 Variáveis Específicas de Eternal Champions (*EC*)

| Variável | Valores | Descrição |
|---|---|---|
| `EC_UNLOCK_BOSS` | `1` ou `0` | Desbloqueia permanentemente o *Eternal Champion* na tela de seleção de personagens e modos de jogo. |
| `EC_HD_MANIFEST` | `<caminho/manifest.toml>` | Caminho para o manifesto de substituição de assets em alta definição (Modo C). |
| `EC_AUDIO_REMASTER` | `off`, `dsp`, `hybrid`, `ost` | Define o modo inicial do motor de remasterização de áudio: `off` (áudio original puro), `dsp` (apenas efeitos DSP), `hybrid` (síntese SoundFont + chip original), `ost` (síntese SoundFont isolada). |
| `EC_AUDIO_SF2` | `<caminho.sf2>` | Caminho do arquivo SoundFont 2 para síntese instrumental em tempo real (padrão: `assets/soundfonts/TimGM6mb.sf2`). |
| `EC_AUDIO_PROFILES_DIR` | `<diretório>` | Diretório contendo os perfis TOML de instrumentos e parâmetros por canal (padrão: `assets/audio_remaster/profiles`). |
| `EC_NEURAL_BPLUS` | `1` ou `0` | Ativa o pipeline de upscale neural de baixa latência B+. |
| `EC_BPLUS_MODEL` | `<caminho.bin>` | Caminho do arquivo de pesos binários do modelo neural B+. |
| `EC_BPLUS_BACKEND` | `auto`, `cuda`, `gl` | Seleciona o backend de inferência neural: `gl` (OpenGL Compute Shader), `cuda` (CUDA/TensorRT via ONNX Runtime) ou `auto`. |
| `EC_BPLUS_BUDGET_MS` | `<float>` | Orçamento máximo de tempo em milissegundos por quadro alocado para inferência neural. |
| `EC_BPLUS_INTENSITY` | `0.0` a `1.0` | Intensidade da interpolação entre o quadro original em baixa resolução e o quadro aprimorado pelo B+. |
| `EC_BPLUS_DEDITHER` | `off`, `cbod`, `both` | Modo de filtro de desdither pré-inferência (Cross-Bilateral Optimization Dither). |
| `EC_SHADER_PRESET` | `<nome \| caminho.glslp \| off>` | Preset de shader RetroArch (`.glslp`) aplicado no fim da apresentação GPU. Vence `settings.ini`; a flag `--shader-preset` vence o env. Ver seção 3.3.1. |
| `EC_SHADER_PARAMS` | `A=1,B=2` | Sobrescreve `#pragma parameter` do preset (a CLI `--shader-param` vence). |
| `EC_SHADER_CAPTURE` / `EC_SHADER_CAPTURE_AT` | `<arquivo.png>` / `<n>` | Grava a saída final da cadeia de shaders no frame `n` (padrão 1), para depuração. |
| `EC_SHADER_VIEWPORT` | `<L>x<A>` (ex.: `3840x2160`) | Diagnóstico/benchmark: simula um monitor de `L`x`A` pixels para o cálculo do tamanho `viewport` da cadeia de shaders (o SDL redimensiona o resultado para a janela real). |

---

## 3. Catálogo Completo de Argumentos de Linha de Comando (CLI Flags)

A precedência de configuração segue rigorosamente a regra: **Flags de Linha de Comando (CLI) > `settings.ini` > Valores Padrão do Jogo (`GameSpec`)**.

### 3.1 Ajuda e Informações Gerais

| Flag | Argumentos | Descrição |
|---|---|---|
| `-h`, `--help` | Nenhum | Exibe o resumo estruturado de opções do runner e flags específicas do jogo na saída padrão e encerra com código `0`. |

### 3.2 Vídeo, Janela e Widescreen

| Flag | Argumentos | Descrição |
|---|---|---|
| `--widescreen` | `<off \| fit \| stage \| W:H>` | Configura a apresentação widescreen. Ex.: `--widescreen 16:9`, `--widescreen 21:9`, `--widescreen 32:9`, `--widescreen fit` (adaptativo à janela) ou `--widescreen off` (proporção clássica 4:3). |
| `--interlace-display` | `<auto \| bob \| weave>` | Define o método de desentrelacamento em modos de vídeo entrelaçados do VDP: `auto` seleciona o melhor método, `bob` prioriza atualização rápida e `weave` prioriza detalhe estático. |
| `--pacing` | `<sync \| uncapped \| fiber \| accurate>` | Controla o limitador de cadência de quadros do host: `sync` sincroniza na taxa alvo (60 Hz NTSC), `uncapped` desliga espera, `accurate` usa temporizador de alta precisão. |
| `--target-fps` | `<float>` | Força uma taxa de quadros alvo arbitrária (ex.: `--target-fps 59.94` ou `--target-fps 120.0`). |

### 3.3 Upscale Neural B+ e Modo C (Substituição HD)

| Flag | Argumentos | Descrição |
|---|---|---|
| `--neural-bplus` | Nenhum | Ativa o processamento de upscale neural B+ nos planos de fundo e sprites. |
| `--bplus-model` | `<caminho.bin>` | Especifica o arquivo de modelo neural binário compilado (ex.: `assets/models/ec_bplus_v3_fast_weights.bin`, `ec_bplus_v3_hq_weights.bin`, `ec_bplus_v3_hq_janai_weights.bin`). O backend CUDA usa o ONNX irmão do mesmo modelo (`<base>_fp16.onnx` ou `<base>.onnx`); sem irmão, `auto` cai no GL. Os modelos v2/v3 foram treinados com entrada pré-dither: use `--bplus-dedither both`. |
| `--bplus-backend` | `<auto \| cuda \| gl>` | Força o backend de execução do modelo: `gl` (OpenGL Compute Shaders portável) ou `cuda` (NVIDIA CUDA via DirectML/ORT). |
| `--bplus-budget-ms` | `<float>` | Define o limite máximo de tempo por frame em milissegundos para a inferência antes de degradar suavemente para filtragem bilinear. |
| `--bplus-intensity` | `<float>` | Fator de mistura entre a imagem pura de baixa resolução e a inferência neural (`0.0` = sem efeito, `1.0` = 100% neural). |
| `--bplus-dedither` | `<off \| cbod \| both>` | Ativa o algoritmo de redução de pontilhismo dither do Mega Drive antes da passagem pelo modelo neural. |
| `--hd-manifest` | `<caminho.toml>` | Carrega o arquivo de manifesto de substituição de texturas/sprites vetoriais em alta definição (Modo C). |
| `--hd-mode` | `<off \| planes \| all>` | Granularidade do Modo C: `planes` usa só planos/céus HD 4× (sprites ficam no B+/nativo, sem colcha de retalhos); `all` (padrão) planos + sprites HD; `off` desliga o Modo C. Env: `EC_HD_MODE`. O `pipeline/manifest.json` usa os planos pre-dither + AnimeSharp (`assets/hd/planes_v3/`); os planos Flux antigos ficam em `assets/hd/planes/` (campo `hd_file_flux`): use `--hd-manifest pipeline/manifest_flux.json` ou `tools/pipeline/regen_planes.py --restore-flux`. Ver `docs/modo_c_planos.md`. |

### 3.3.1 Shaders (RetroArch `.glslp`)

| Flag | Argumento | Descrição |
|---|---|---|
| `--shader-preset` | `<off \| nome \| caminho.glslp>` | Ativa uma cadeia multi-pass de shaders no formato **GLSL legado do libretro** (`.glslp` + `.glsl`; não é slang). `off` desliga. Caminho existente é usado direto; senão `nome` é procurado em `assets/shaders/presets/<nome>.glslp` (e `assets/shaders/libretro/`), também relativo ao exe e a `../../`. Env: `EC_SHADER_PRESET`; `settings.ini`: `[ec.video] shader_preset`. |
| `--shader-param` | `<NOME=valor>` | Sobrescreve um `#pragma parameter` do preset (repetível; acumula). Env: `EC_SHADER_PARAMS` (`A=1,B=2`); `settings.ini`: `[ec.video] shader_params`. |

**Precedência:** CLI > env > `settings.ini` > nenhum (vale para o preset e, por parâmetro, para os valores). Parâmetros: default do `#pragma` < linhas `NOME = valor` do preset < `settings.ini` < env < CLI.

```powershell
.\build\EternalChampionsRecomp.exe --no-launcher --shader-preset lite-superxbr
.\build\EternalChampionsRecomp.exe --no-launcher --shader-preset C:/RetroArch/shaders/shaders_glsl/xbr/super-xbr-6p.glslp
# Combinado com B+ (o shader enxerga a saída 4x do B+ como Source e o frame nativo como Original)
.\build\EternalChampionsRecomp.exe --no-launcher --bplus-model assets/models/ec_bplus_v3_fast_weights.bin --bplus-dedither both --shader-preset fsr/fsr
```

- É um extra **host-only** (só lê o frame já renderizado; não altera o estado do jogo). Preset ativo força o renderer SDL `opengl`; sem GL (ex.: `SDL_RENDER_DRIVER=software`) a cadeia se desativa com uma linha de log e a imagem sai sem shader. Erro de compilação/link também desativa a cadeia inteira (log com o arquivo e o info log).
- Suportado: `shaders`, `shaderN`, `filter_linearN`, `scale_typeN`/`_x`/`_y` (`source|viewport|absolute`), `scaleN`/`_x`/`_y`, `float_framebufferN`, `srgb_framebufferN`, `wrap_modeN`, `mipmap_inputN`, `frame_count_modN`, `aliasN`, `textures` + `NOME`, `NOME_linear`, `NOME_wrap_mode`, `NOME_mipmap` (LUT PNG), e `NOME = valor` como override de parâmetro. Uniforms: `Texture`, `TextureSize`, `InputSize`, `OutputSize`, `FrameCount`, `FrameDirection`, `MVPMatrix`, `Orig*`, `PassPrevK*`, `PassK*` (1-based: `Pass1` = saída do `shader0`), `<alias>*`, LUTs, parâmetros. `#include "x"` é expandido.
- Não suportado: `#reference` (avisa e ignora), `feedback_pass` (ignorado), histórico real (`Prev*Texture` é aproximado pelo frame atual, com aviso), slang (`.slangp`), geração de mipmap por LUT além de `NOME_mipmap`.
- O tamanho `viewport` é o retângulo real (pixels) onde o runner desenha o canvas lógico (respeita `integer_scale`). O último passe grava numa textura-alvo SDL desse tamanho (ou do `scale` do preset).
- Extensões opcionais no `.glslp` (ignoradas pelo RetroArch): `ec_bplus = <weights.bin>` (relativo ao preset; ou `on`), `ec_bplus_dedither = off|cbod|both`, `ec_bplus_intensity = <float>`. Ativam o B+ somente se o usuário não o configurou por CLI/env (CLI/env vencem). `ec_bplus` aceita `on`/`1`/`true` (modelo padrão), `off`/`0`/`false`/`no`/`none`/`disabled`/vazio (desligado) ou um caminho (relativo ao `.glslp`, depois como dado); caminho inexistente gera aviso e deixa o B+ do preset desligado (não cai no modelo v1). O modelo vindo do preset conta como modelo explícito (o `.onnx` irmão do backend CUDA é derivado dele). O B+ ligado pelo preset continua ligado mesmo se a cadeia de shaders falhar em runtime (só os shaders são desativados).
- Métricas ao sair: `[SHADER] preset=<nome> passes=N frames=… p50_ms=… p99_ms=…` (GL_TIME_ELAPSED) e uma linha por passe. `EC_SHADER_CAPTURE=<png>` + `EC_SHADER_CAPTURE_AT=<n>` gravam o frame `n`.

### 3.4 Áudio e Remasterização em Tempo Real

| Flag | Argumentos | Descrição |
|---|---|---|
| `--audio-remaster` | `<off \| dsp \| hybrid \| ost>` | Seleciona a estratégia de áudio: `off` (YM2612 + PSG puros), `dsp` (apenas processamento de estúdio EQ/reverb), `hybrid` (instrumentos acústicos SoundFont substituindo canais FM melódicos mantendo DAC/FM de percussão), `ost` (trilha sonora sintetizada em tempo real). |
| `--audio-sf2` | `<caminho.sf2>` | Aponta para o arquivo SoundFont (`.sf2`) usado no banco de amostras melódicas. |
| `--audio-profiles-dir` | `<diretório>` | Diretório contendo os perfis de canais instrumentais em formato TOML. |
| `--audio-bgm-vol` | `<float>` | Ajusta o volume relativo da música de fundo (BGM), ex.: `1.0` (padrão), `0.8`, `1.5`. |
| `--audio-sfx-vol` | `<float>` | Ganho (`0.0` a `2.0`, padrão `1.0`) aplicado a **todo o barramento do chip** (YM2612 + PSG + DAC, ou seja, BGM do chip, SFX e vozes) nos modos `dsp`/`hybrid`/`ost`; não atua só nos SFX nem na síntese SoundFont (essa usa `--audio-bgm-vol`). Valores fora da faixa são ajustados com aviso `[EC-AUDIO]`; valor não numérico é ignorado com aviso `valor invalido`. |
| `--audio-chip-blend` | `<float>` | **Override explícito** (`0.0` a `1.0`) da presença do chip original em `hybrid`/`ost`; vence o `chip_blend` do perfil da faixa, o do `settings.ini` e os defaults do modo. Precedência completa: `--audio-chip-blend` > `chip_blend` do perfil `.toml` da faixa > `chip_blend` de `[ec.audio]` no `settings.ini` (default do usuário) > default do modo (`hybrid` 0.65, `ost` 0.18). Sem efeito em `off`/`dsp`. |
| `--audio-manifest` | `<arquivo>` | **Obsoleto**: ignorado com aviso (o módulo usa síntese em tempo real: `--audio-sf2` + `--audio-profiles-dir`). |
| `--audio-dsp-reverb` | `<float>` | Quantidade de reverberação de estúdio Schroeder injetada no barramento mestre (`0.0` a `1.0`). |
| `--wav` | `<caminho.wav>` | Grava a sessão em WAV estéreo 16-bit na **taxa interna do mixer (223 721 Hz)** (mesma taxa do `[audio] DRC bridge: 223721 Hz internal`); reamostre para 44,1/48 kHz para players comuns. |
| `--psg-vol-div` | `<inteiro>` | Divisor de ganho de atenuação do canal PSG (ajuste fino de balanço entre YM2612 e PSG). |
| `--snd-dump-frame` | `<inteiro>` | Despeja o estado completo dos buffers de som no frame indicado (para diagnóstico determinístico). |
| `--snd-dump-vint` | `<inteiro>` | Despeja o estado completo do som quando o contador lógico V-Int atingir o valor especificado. |

### 3.5 Execução, Benchmark e Determinismo

| Flag | Argumentos | Descrição |
|---|---|---|
| `--unlock-boss` | Nenhum | Desbloqueia o *Eternal Champion* para seleção no modo de torneio e versus. |
| `--turbo` | Nenhum | Roda na velocidade máxima possível do processador, desativando a espera de sincronização e o áudio SDL. |
| `--launcher` | Nenhum | Força a exibição do Launcher gráfico antes do início da partida. |
| `--no-launcher` | Nenhum | Pula qualquer tela de launcher e inicializa o jogo instantaneamente. |
| `--max-frames` | `<inteiro>` | Executa exatamente o número especificado de quadros e fecha o emulador com retorno de sucesso. |
| `--benchmark` | `<inteiro>` | Executa benchmark headless por N frames com taxa destravada, medindo FPS e tempo médio de quadro. |
| `--measure-runtime` | `<inteiro>` | Mede o tempo de execução real de N quadros com pipeline completo (vídeo + áudio). |
| `--runtime-uncapped` | Nenhum | Usado junto com `--measure-runtime` para manter todas as rotinas ativas sem limitação de 60 FPS. |
| `--input-script` | `<caminho.input>` | Executa um roteiro de inputs gravados para regressão determinística e testes automáticos. |
| `--fresh-evidence` | Nenhum | Inicia arquivos de evidências de despacho de memória e rotinas vazios na sessão atual. |
| `--framelog` | `<caminho.log>` | Grava um arquivo de log contendo os microssegundos gastos em cada quadro. |
| `--port` | `<inteiro>` | Define a porta TCP de escuta do servidor embutido de depuração e inspeção de memória. |
| `--yield-log` | Nenhum | Cria o arquivo `yield_log_native.log` com métricas das chamadas de V-Blank. |
| `--hash-frames` | `<inteiro>` | Emite hash FNV-1a de 64-bit do buffer de vídeo a cada N frames para verificação de paridade de imagem. |
| `--hash-on-mode` | Nenhum | Emite o hash do buffer gráfico em cada mudança de modo de jogo (`Game_Mode`). |
| `--replay-diagnostic` | `<caminho>` | Executa e valida um arquivo de replay de diagnóstico previamente capturado. |

---

## 4. Guia Completo do Arquivo `settings.ini`

O arquivo `settings.ini` localiza-se no mesmo diretório do executável (`build/settings.ini`). Ele mantém as preferências do usuário salvas pelo launcher gráfico ou editadas manualmente.

### 4.1 Seção `[video]`

```ini
[video]
window_scale = 3
fullscreen = 0
linear_filter = 0
internal_scale = 1
integer_scale = 1
widescreen = 1
widescreen_cells = 10
presentation_rate = 0
```

- **`window_scale`**: Fator de multiplicação inteira da janela (ex.: `1` para 320×224, `2` para 640×448, `3` para 960×672, `4` para 1280×896).
- **`fullscreen`**: Modo de tela cheia:
  - `0`: Modo Janela convencional.
  - `1`: Modo Janela Sem Bordas (*Borderless Desktop*).
  - `2`: Modo Exclusivo de Tela Cheia (*Exclusive Fullscreen*).
- **`linear_filter`**:
  - `0`: Interpolação *Nearest Neighbor* (pixels perfeitamente nítidos, estilo retro pixel art).
  - `1`: Interpolação Bilinear (suavização linear entre pixels).
- **`internal_scale`**: Fator de resolução interna de GPU para texturas e assets HD (padrão `1`, suporta até `64`).
- **`integer_scale`**:
  - `1`: Força a renderização em múltiplos inteiros de pixel, evitando distorções irregulares em monitores modernos.
  - `0`: Permite esticar livremente preenchendo a janela.
- **`widescreen`**: Ativação do modo widescreen padrão VDP (`1` = ligado, `0` = desligado).
- **`widescreen_cells`**: Quantidade de colunas extras de 8px estendidas à esquerda e à direita (padrão `10` para formato 16:9).
- **`presentation_rate`**: Taxa de atualização forçada na apresentação gráfica (`0` = automático/monitor, `120`, `144` ou `-1` para taxa nativa do display).

### 4.2 Seção `[mods.widescreen]`

```ini
[mods.widescreen]
enabled = 1
aspect = 16:9
```

- **`enabled`**: Habilita o renderizador customizado de cena widescreen de *Eternal Champions* com reposicionamento dinâmico de HUD e correção de metatiles (`1` = sim, `0` = não).
- **`aspect`**: Proporção de aspecto desejada:
  - `adaptive` ou `fit`: Adapta-se automaticamente à proporção da janela do usuário.
  - `16:9`: Formato widescreen tradicional de televisores e monitores padrão (1920×1080, 2560×1440, etc.).
  - `21:9`: Formato ultrawide para monitores panorâmicos (2560×1080, 3440×1440, etc.).
  - `32:9`: Formato super ultrawide.

### 4.3 Seção `[audio]`

```ini
[audio]
volume = 100
frequency = 48000
bypass_filter = 0
```

- **`volume`**: Volume geral de áudio de `0` (mudo) a `100` (máximo).
- **`frequency`**: Frequência de amostragem solicitada ao dispositivo de áudio host em Hertz (`32000`, `32040`, `44100` ou `48000`).
- **`bypass_filter`**:
  - `0`: Aplica a curva de filtragem passa-baixa analógica do console original (som mais encorpado e aveludado).
  - `1`: Ignora os filtros, gerando áudio digital cru com agudos cristalinos.

### 4.4 Seção `[ec.audio]`

Configurações do módulo de síntese instrumental e remasterização de *Eternal Champions*:

```ini
[ec.audio]
remaster = hybrid
sf2 = assets/soundfonts/TimGM6mb.sf2
profiles_dir = assets/audio_remaster/profiles
bgm_vol = 1.0
sfx_vol = 1.0
chip_blend = 0.3
dsp_reverb = 0.35
```

- **`remaster`**: Modo de remasterização em tempo real:
  - `off`: Somente hardware emulado original (chips YM2612 + PSG).
  - `dsp`: Chips originais enriquecidos com equalização de 3 bandas, alargador estéreo sutil e reverberador Schroeder.
  - `hybrid`: Substituição melódica ao vivo via SoundFont 2 combinada com percussão e ruídos do chip original.
  - `ost`: Reprodução 100% sintetizada SoundFont com perfis de estúdio.
- **`sf2`**: Caminho relativo ou absoluto para o arquivo de banco SoundFont 2.
- **`profiles_dir`**: Diretório com os mapeamentos TOML de instrumentos para cada uma das faixas do jogo.
- **`bgm_vol`**: Ganho de volume para a trilha sonora (0.0 a 2.0).
- **`sfx_vol`**: Ganho de todo o barramento do chip (BGM do chip + SFX + vozes DAC) nos modos `dsp`/`hybrid`/`ost` (0.0 a 2.0). Ver `--audio-sfx-vol`.
- **`chip_blend`**: Default do usuário (`0.0` a `1.0`) para a presença do chip em `hybrid`/`ost`. Só é usado quando o perfil da faixa **não** define `chip_blend`; a CLI `--audio-chip-blend` vence ambos (ver precedência em `--audio-chip-blend`).
- Valores de `[ec.audio]` aceitam aspas externas (`sf2 = "caminho com espaço.sf2"`) e comentário final (` ;` ou ` #`).
- **`dsp_reverb`**: Intensidade do efeito de reverberação de sala aplicado à saída final (`0.0` a `1.0`).

**Ganho e limiter (modo `dsp`)**: o chip recebe ganho fixo `EC_CHIP_DSP_GAIN = 1.75` (+4,9 dB) antes do soft-limiter (joelho em 0,92). Medido nos filmes de regressão 02, 06, 13 e 17 (`--wav` em `off` vs `dsp`): pico do chip 0,326 -> 0,606 em `dsp`, **0 %** das amostras acima de 0,80 (limiter nunca atua; alvo < 0,5 %), RMS +6 dB sobre `off`. Valor mantido em 1,75.

**Variáveis de depuração**: `EC_AUDIO_TRACE=1` registra em stderr eventos de ciclo de vida do motor (`bgm_play`, pausa, resync de notas, state load).

**Estado do motor após pausa/turbo/state load**: o motor mantém por canal o estado `keyed` (key-on no hardware); ao fim da pausa, do turbo e na troca/recarga de perfil as notas `keyed` voltam a soar a partir do shadow. Após carregar um savestate o shadow de registradores é tratado como *desconhecido* (o runner não expõe leitura do YM2612/PSG): o SoundFont só volta a soar depois que o driver reescreve patch (algoritmo + 4 TL) e frequência do canal; `bgm_id`/perfil ativo são preservados. Bytes de streaming DAC (`$2A`) e dados sem endereço conhecido nunca viram eventos. TL dos carriers vira volume da voz SF2 (fades do driver audíveis); canais com TL >= 100 (< -75 dB) não disparam nota.

### 4.4.1 Seção `[ec.video]`

```ini
[ec.video]
shader_preset = remaster-crisp
shader_params = hardScan=-10,maskDark=0.4
```

Equivalem a `--shader-preset` / `--shader-param` (aspas externas e comentário final ` ;`/` #` são removidos); a CLI e as variáveis `EC_SHADER_PRESET` / `EC_SHADER_PARAMS` têm precedência.

### 4.5 Seção `[launcher]`

```ini
[launcher]
skip_launcher = 1
```

- **`skip_launcher`**: Quando `1`, o jogo inicia diretamente sem exibir o menu gráfico de configuração a cada boot.

### 4.6 Seção `[rewind]`

```ini
[rewind]
enabled = 1
depth = 120
interval = 5
```

- **`enabled`**: Ativa o suporte ao sistema de rebobinamento local contínuo de estados (`1` = ativado, `0` = desativado).
- **`depth`**: Número de posições de memória salvas no histórico circular (ex.: `120` estados).
- **`interval`**: Intervalo em frames de jogo entre cada captura de estado automático (ex.: `5` frames = 12 capturas por segundo).

### 4.7 Seção `[mods.diagnostics]`

```ini
[mods.diagnostics]
enabled = 1
```

- **`enabled`**: Registra dados de telemetria e diagnóstico determinístico da partida em `logs/EC-feedback.replay`.

### 4.8 Seções `[input.p1]` e `[input.p2]`

Mapeamento de teclado e gamepads para o Jogador 1 e Jogador 2:

```ini
[input.p1]
device = -1
pad_type = 1
deadzone = 15
key.up = 82
key.down = 81
key.left = 80
key.right = 79
key.a = 4
key.b = 22
key.c = 7
key.x = 20
key.y = 26
key.z = 8
key.start = 40
key.mode = 42

pad.up = btn:11
pad.down = btn:12
pad.left = btn:13
pad.right = btn:14
pad.a = btn:0
pad.b = btn:1
pad.c = btn:5
pad.x = btn:2
pad.y = btn:3
pad.z = btn:4
pad.start = btn:6
pad.mode = btn:7
```

- **`device`**: `-1` para teclado; `>= 0` para o índice de joystick/gamepad detectado na SDL2.
- **`pad_type`**: `0` para controle padrão de 3 botões (A, B, C); `1` para controle arcade de 6 botões (A, B, C, X, Y, Z).
- **`deadzone`**: Porcentagem de zona morta para analógicos (0 a 100%).
- **`key.*`**: Código Scancode SDL2 da tecla mapeada para a respectiva ação.
- **`pad.*`**: Botão ou eixo do controle (ex.: `btn:0`, `axis:0:+`, `axis:1:-`).

---

## 5. Tabela de Atalhos de Teclado (Hotkeys) em Tempo Real

Durante a execução da partida, as seguintes teclas de atalho estão ativas em nível global no emulador:

| Tecla / Combinação | Função no Emulador | Descrição Detalhada |
|---|---|---|
| **`F1` a **`F9`** (Segurar 1.5s) | **Carregar Quickstate** | Segurar a tecla por 1.5 segundo carrega o estado salvo do slot correspondente (`native_save_<slot>.bin`). |
| **`Shift + F1` a **`F9`** | **Salvar Quickstate** | Salva instantaneamente o estado atual do jogo no slot especificado. |
| **`F5`** ou **`Tab`** | **Modo Turbo** | Ativa a velocidade máxima sem limite de FPS e suspende o áudio enquanto mantido pressionado. |
| **`F7`** | **Gerenciador de Saves** | Abre a interface visual de gerenciamento e seleção de slots de Savestates. |
| **`F8`** | **Histórico de Rebobinamento** | Abre o visualizador gráfico de snapshots da linha do tempo para voltar no tempo. |
| **`F10`** | **Cold Reset** | Reinicia completamente a máquina virtual e o cartucho (a memória SRAM de bateria é preservada). |
| **`F11`** / **`Alt+Enter`** | **Alternar Tela Cheia** | Alterna entre o modo janela e o modo tela cheia configurado no `settings.ini`. |
| **`F12`** | **Captura de Tela (PNG)** | Tira um print de alta qualidade e salva em `screenshots/screenshot_YYYYMMDD_HHMMSS.png`. |
| **`Shift + F12`** / **`Ctrl + F12`** | **Dump de Áudio Dev** | Despeja imediatamente os buffers de rastreamento dos chips para `chip_ring.txt` e `snd_ring.txt`. |
| **`Backspace`** | **Rebobinar / Botão Mode** | Rebobina a ação nos últimos quadros ou atua como o botão *Mode* do gamepad de 6 botões. |
| **`Esc`** | **Menu de Pausa / Sair** | Abre o menu de configurações do runtime UI ou fecha o jogo se o launcher estiver desativado. |

---

## 6. Exemplos Práticos de Linha de Comando para Cenários Reais

Abaixo estão comandos prontos para os casos de uso mais comuns em ambientes de desenvolvimento e gameplay:

### Cenário 1: Experiência Moderna Completa (Widescreen 16:9 + Áudio Híbrido Remasterizado)
Ideal para jogar em telas 1080p/4K modernas, com proporção 16:9 perfeita, instrumentos SoundFont em tempo real e chefe desbloqueado:

```powershell
.\build\EternalChampionsRecomp.exe --widescreen 16:9 --audio-remaster hybrid --audio-sf2 assets/soundfonts/TimGM6mb.sf2 --audio-dsp-reverb 0.35 --unlock-boss --no-launcher
```

### Cenário 2: Monitor Ultrawide (21:9) com Upscale Neural B+ e Desdither GPU
Aproveitamento total da largura de monitores ultrawide (2560×1080 ou 3440×1440), com remoção de dither quadriculado e upscale com redes neurais:

```powershell
.\build\EternalChampionsRecomp.exe --widescreen 21:9 --neural-bplus --bplus-backend gl --bplus-dedither cbod --bplus-intensity 0.85 --no-launcher
```

### Cenário 3: Purismo Retro (4:3 Clássico com Filtro de Hardware e Áudio FM Original)
Para entusiastas do visual clássico do Mega Drive em monitores CRT ou com pixels nítidos sem qualquer modificação moderna:

```powershell
.\build\EternalChampionsRecomp.exe --widescreen off --audio-remaster off --interlace-display weave --no-launcher
```

### Cenário 4: Benchmark Automatizado Headless para CI/CD
Execução de teste de regressão e benchmark de performance por 1.000 quadros sem criar janela gráfica nem abrir dispositivo de áudio:

```powershell
$env:SDL_VIDEODRIVER = "dummy"
$env:SDL_AUDIODRIVER = "dummy"
.\build\EternalChampionsRecomp.exe --benchmark 1000 --fresh-evidence --no-launcher
```

### Cenário 5: Gravação de Trilha Sonora Direta para WAV
Gera a gravação de áudio em tempo real de uma luta ou sessão completa para um arquivo WAV estéreo em velocidade acelerada:

```powershell
.\build\EternalChampionsRecomp.exe --audio-remaster hybrid --wav trilha_gravada.wav --max-frames 3600 --turbo --no-launcher
```
