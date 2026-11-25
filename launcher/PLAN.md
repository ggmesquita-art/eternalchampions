# PLAN.md — Launcher do Eternal Champions Recomp ("EC Launcher")

> Frontend próprio do projeto, separado do executável do jogo, para configurar **todas** as opções, escolher a ROM e iniciar a partida. O visual vem das telas do jogo: menu principal sobre pedra marmorizada turquesa e abertura em roxo crepuscular com os lutadores.

---

## 1. Objetivos e não-objetivos

**Objetivos**
1. Expor **100% das opções configuráveis**:
   - todas as chaves do `settings.ini`, tanto do runner quanto do jogo;
   - todas as flags relevantes da CLI;
   - os presets de shader, os modelos B+, o Modo C, o áudio remaster e os controles.
2. Definir a **ROM padrão** (arquivo `rom-<short_name>.cfg`), validando-a pelo SHA-256 da versão US suportada.
3. **Mapear controles** do P1 ao P6, com teclado e SDL GameController (Xbox, PlayStation, 8BitDo…), incluindo:
   - **combinações**: um botão físico aciona vários botões do Mega Drive (ex.: RB = A+B+C);
   - autofire opcional por botão;
   - presets prontos.
4. Identidade visual fiel ao jogo, navegável inteiramente por **controle**, teclado ou mouse.
5. Ter uma única fonte de verdade para as opções: um **esquema** declarativo que gera as telas, a leitura/escrita do ini, os argumentos da CLI e o texto de ajuda.
6. **Estúdio de Remaster integrado (§10):** mapear os 52 instrumentos FM do jogo (`voices.toml`) e os canais PSG/DAC por música. Inclui prévia sintetizada nativa, A/B com o original, solo/mute e "Testar no jogo". Substitui o `tools/audio/mixer_studio.py` para o usuário final; o mixer Python segue como ferramenta de desenvolvimento.

**Não-objetivos (por ora)**
- Redesenhar o menu de runtime (ESC) do runner. Ele continua o genérico do `recomp-ui`; tematizá-lo fica para uma fase futura.
- Distribuir arte da ROM no repositório. Toda arte do jogo é extraída da ROM do próprio usuário (§6).

---

## 2. Arquitetura (decisão proposta)

| Opção | Prós | Contras |
|---|---|---|
| **A. Executável próprio `EternalChampions.exe` (recomendado)** — C++17 + SDL2 + OpenGL 3.3 + Dear ImGui (só para entrada e layout) com renderização própria do tema | Não mexe no `segagenesisrecomp` (repositório compartilhado, com alterações de terceiros pendentes); liberdade visual total; reaproveita as dependências do build (SDL2 e vcpkg); controle de primeira classe via SDL_GameController | Dois executáveis; o tema precisa de widgets próprios por cima do ImGui |
| B. Tematizar o launcher `recomp-ui` do runner | Um executável só | Módulo compartilhado entre jogos, com tema limitado; exige mudar outro repositório |
| C. Web/Tauri ou Python/Qt | Fácil de estilizar | Runtime extra, navegação por controle mais fraca, empacotamento pesado |

**Fluxo com a opção A**
1. `EternalChampions.exe` lê e escreve o `build/settings.ini` (o mesmo arquivo do runner) e o `rom-<short>.cfg`.
2. Ao clicar em **JOGAR**, executa `EternalChampionsRecomp.exe --no-launcher <rom> <flags>`. As flags são só as opções sem chave no ini (§4.3).
3. Espera o jogo encerrar e volta ao menu, mostrando o código de saída. Em caso de falha, oferece "ver log" com `build/logs/` e `last_error.log`.
4. O launcher grava `[launcher] skip_launcher = 1`, para que o launcher genérico do runner nunca apareça por cima.

**Organização do código**
```
launcher/
  PLAN.md                 ← este documento
  CMakeLists.txt          ← alvo EternalChampions.exe (opção no CMake raiz: EC_BUILD_LAUNCHER)
  src/main.cpp            ← loop SDL, estados de tela
  src/schema.{h,cpp}      ← esquema declarativo das opções (§4)
  src/ini.{h,cpp}         ← leitor/escritor do settings.ini que preserva comentários, ordem e chaves desconhecidas
  src/theme/*.cpp         ← fundo de pedra, fontes bitmap, molduras, cursor yin-yang, partículas
  src/screens/*.cpp       ← telas (§5)
  src/input_bind.cpp      ← captura de bind ("aperte um botão"), combos, presets
  src/launch.cpp          ← monta o argv, faz o spawn e captura o log
  src/remaster/           ← Estúdio de Remaster (§10)
    voices_toml.cpp       ← lê/grava voices.toml e perfis .toml preservando [*.meta], comentários e ordem
    catalog.cpp           ← port C++ do voice_catalog.py: dumps → vozes/hash FNV-1a → MIDI por voz em memória
    preview_synth.cpp     ← síntese com game/third_party/tsf.h + o mesmo SF2 do jogo (lógica do tsf_midi_synth.c)
    original_capture.cpp  ← captura do original: jogo headless no Music Test com --audio-remaster off --wav (só em RAM)
    patch_view.cpp        ← visualização do patch FM (algoritmo, operadores, envelopes)
  assets/fallback/        ← tema procedural + fontes OFL, usados quando não há arte extraída
tools/launcher/extract_ui_assets.py  ← extrai a arte do jogo da ROM do usuário (§6)
```

---

## 3. Identidade visual (guia de estilo)

Referência: telas do **menu principal** e da **abertura** do jogo, renderizadas com B+ v3.

### 3.1 Paleta
| Token | Uso | Cor de partida (ajustar a partir da extração) |
|---|---|---|
| `stone.light` / `stone.mid` / `stone.dark` | fundo de pedra marmorizada | `#7cc3bd` / `#4f9fa6` / `#2f6f86` |
| `stone.grout` | rejunte dos blocos | `#1d3a5a` |
| `logo.red→orange` | logo e títulos fortes | `#d42a24` → `#f6a43a` |
| `logo.shadow` | sombra deslocada do logo | `#4b1a3f` |
| `title.gold` | cabeçalhos de tela ("MAIN MENU") | `#f9d64a` → `#d9742a`, contorno `#3a1a10` |
| `text.white` | itens de menu | `#ffffff`, contorno 1 px `#101018` |
| `text.select` | item selecionado | `#ffe63a` |
| `text.disabled` | indisponível | `#8aa0a8` |
| `dusk.top→bottom` | fundo da abertura e de modais | `#5a4aa8` → `#a796dc`, com halo radial `#e8d8ff` |
| `accent.kanji` | círculo do emblema 武士道 | `#f3d23a`, kanji `#1f3a2a` |

### 3.2 Tipografia
- **Itens e texto:** a fonte pixel do menu do jogo (8×8), extraída da ROM. O texto é escalado inteiro (×3, ×4…) com contorno e sombra, como em "One Player".
- **Cabeçalhos:** as letras douradas em gradiente do "MAIN MENU" do jogo, quando os glifos existirem na ROM. Para letras ausentes, uma fonte OFL de estilo "western serif" condensada (ex.: *Rye*), com gradiente e contorno no shader.
- **Ajuda e descrições longas:** fonte pixel legível OFL (ex.: *Press Start 2P* ou *Silkscreen*) em tamanho menor.
- Tudo é renderizado num canvas virtual **1280×720** (16:9) e escalado para a janela, com escala inteira opcional para nitidez.

### 3.3 Elementos
- **Fundo:** os blocos de pedra do menu do jogo, extraídos e escalados ×4 com B+ v3 HQ, em mosaico com rejunte. Opcionalmente com parallax lento.
- **Cursor:** um **yin-yang** em cada lado do item focado, girando devagar, como no menu do jogo.
- **Painéis:** caixas com moldura de pedra escura e cantos chanfrados, com o interior em pedra escurecida a 60% para legibilidade.
- **Abertura (splash):** degradê roxo crepuscular com halo, o logo grande e os lutadores. A arte é a tela de título extraída e escalada com B+ v3 HQ. Abaixo, "PRESS START" piscando.
- **Transições:** fade curto (150 ms) e deslize lateral entre abas. Sem animações longas: o launcher deve responder instantaneamente.
- **Sons (opcional):** cursor e confirmação extraídos da ROM no SFX test, ou silêncio por padrão.

---

## 4. Esquema de opções (fonte única de verdade)

Cada opção é uma entrada declarativa:
```
{ id, aba, rótulo, ajuda, tipo(bool|int|float|enum|path|text|bind),
  ini{seção,chave} | cli{flag} | env{nome}, default, faixa/valores,
  depende_de, requer_reinício, preview }
```
A partir do esquema, o launcher gera as telas, carrega e salva o ini, monta o argv e valida os valores, sempre com o mesmo clamp do código do jogo/runner. Linhas desconhecidas do ini são **preservadas**.

### 4.1 Runner (`settings.ini`, `segagenesisrecomp/runner/app_config.c`)
| Aba | Seção / chave | Tipo / valores | Default |
|---|---|---|---|
| Vídeo | `[video] window_scale` | int 1..8 | 2 |
| Vídeo | `[video] fullscreen` | 0 janela / 1 borderless / 2 exclusivo | 0 |
| Vídeo | `[video] linear_filter` | bool | 0 |
| Vídeo | `[video] integer_scale` | bool | 0 |
| Vídeo | `[video] internal_scale` | 1, 2, 4, 8, 16, 32, 64 (reinício) | 1 |
| Vídeo | `[video] presentation_rate` | Off, 120, 144, Display (0/120/144/-1) | 0 |
| Vídeo | `[mods.widescreen] enabled` + `aspect` | bool + fit/16:9/21:9/32:9 | 0 / fit |
| Vídeo | `[video] widescreen`, `widescreen_cells` | legado do VDP (área "Avançado") | 0 / 8 |
| Áudio | `[audio] volume` | 0..100 | 100 |
| Áudio | `[audio] frequency` | 32000, 32040, 44100, 48000 | 48000 |
| Áudio | `[audio] bypass_filter` | bool (desligar o passa-baixa do hardware) | 0 |
| Jogo | `[rewind] enabled`, `depth`, `interval` | bool, 10..600, 1..60 | 1, 60, 6 |
| Avançado | `[mods.diagnostics] enabled` | bool | 1 |
| Sistema | `[launcher] skip_launcher` | fixo em 1 pelo EC Launcher | — |
| Controles | `[input.p1..p6] device`, `pad_type`, `deadzone`, `key.<btn>`, `pad.<btn>` | ver §7 | `input_map.c` |

### 4.2 Jogo (`game/ec_spec.c`, seções `[ec.*]`)
| Aba | Chave | Tipo / valores |
|---|---|---|
| Aprimoramento | `[ec.video] shader_preset` | off + lista de `assets/shaders/presets/*.glslp` + "Escolher arquivo .glslp…" (presets do RetroArch) |
| Aprimoramento | `[ec.video] shader_params` | editor gerado lendo os `#pragma parameter` do preset escolhido (slider por parâmetro, com faixa e passo do pragma) |
| Áudio | `[ec.audio] remaster` | off / dsp / hybrid / ost, com descrição de cada modo |
| Áudio | `sf2`, `voices`, `profiles_dir` | caminho (validar que existe) |
| Áudio | `bgm_vol`, `sfx_vol` | 0.0..2.0 |
| Áudio | `dsp_reverb` | 0.0..1.0 |
| Áudio | `chip_blend` (legado) e `chip_blend_ost` | 0.0..1.0 (só em `ost`) |
| Áudio | botão "Abrir Mixer Studio" | `python tools/audio/mixer_studio.py` |

### 4.3 Opções sem chave no ini hoje (pré-requisito L0: criar as chaves)
Recomendação: criar as chaves `[ec.*]` em `ec_spec.c`, para que o jogo funcione igual com ou sem o launcher. O que for do runner e não puder ganhar chave vai como flag no argv.

| Aba | Opção | Flag atual | Chave nova proposta |
|---|---|---|---|
| Aprimoramento | B+ ligado e modelo | `--neural-bplus`, `--bplus-model` | `[ec.bplus] enabled`, `model` (lista de `assets/models/*_weights.bin` com nome amigável: v3 fast, v3 HQ, v3 HQ JaNai, v2, v1) |
| Aprimoramento | Backend / de-dither / intensidade / budget | `--bplus-backend`, `--bplus-dedither`, `--bplus-intensity`, `--bplus-budget-ms` | `[ec.bplus] backend`, `dedither` (aviso: v2/v3 exigem `both`), `intensity` 0..4, `budget_ms` |
| Aprimoramento | Modo C | `--hd-manifest`, `--hd-mode` | `[ec.hd] manifest` (AnimeSharp padrão / Flux / arquivo), `mode` off/planes/all |
| Jogo | Desbloquear chefe | `--unlock-boss` | `[ec.game] unlock_boss` |
| Vídeo | Widescreen custom | `--widescreen off/fit/stage/W:H` | usar `[mods.widescreen]`; `stage` e `W:H` livres vão por argv |
| Vídeo | Desentrelaçamento | `--interlace-display auto/bob/weave` | argv (runner) |
| Avançado | Pacing / FPS alvo | `--pacing sync/uncapped/fiber/accurate`, `--target-fps` | argv (runner) |
| Avançado | Porta de debug, logs | `--port`, `--fresh-evidence`, `--yield-log` | argv (runner, área "Desenvolvedor", oculta por padrão) |

Flags de teste e automação (`--benchmark`, `--max-frames`, `--input-script`, `--hash-*`, `--snd-dump-*`, `--replay-diagnostic`, `--wav`) **não** entram na interface, exceto `--wav` em "Desenvolvedor › Gravar áudio".

### 4.4 Atalhos de pré-configuração ("Perfis de qualidade")
Botões que preenchem várias opções de uma vez. O usuário pode ajustar tudo depois.

| Perfil | B+ | Shader | Modo C | Áudio |
|---|---|---|---|---|
| **Original** | off | off | off | off |
| **Remaster Leve** | v3 fast, both | `remaster-crisp` | off | hybrid |
| **Remaster Máximo** | v3 HQ, both | `remaster-hq` | planes | hybrid |
| **Sem neural (GPU fraca)** | off | `lite-scalefx` | off | dsp |

Ao lado de cada perfil aparece o custo estimado (ms/frame dos benchmarks do G4/S3).

---

## 5. Telas

Navegação: **D-pad/analógico** move; **A/Enter** confirma; **B/Esc** volta; **LB/RB** (ou Q/E) trocam de aba; **Y** mostra a ajuda; **X** volta a opção ao padrão; **Start** = JOGAR em qualquer tela.

1. **Abertura (splash).** Lutadores, logo e "PRESS START". Some ao primeiro input, ou depois de 3 s se a ROM já estiver configurada.
2. **Menu principal** (pedra turquesa, logo, "MAIN MENU" dourado, yin-yang):
   `JOGAR` · `ROM` · `VÍDEO` · `APRIMORAMENTO` · `ÁUDIO` · `ESTÚDIO DE REMASTER` · `CONTROLES` · `JOGO` · `SAVES` · `AVANÇADO` · `SOBRE` · `SAIR`.
   No rodapé ficam a ROM atual (nome, SHA ✓/✗) e o perfil ativo.
3. **ROM.**
   - Seletor de arquivo nativo (SDL_ShowOpenFileDialog ou equivalente) e lista de ROMs recentes.
   - Validação do SHA-256 contra `6d8bdaf8…ea247` (US). Hash diferente gera um aviso claro.
   - Grava o `rom-<short_name>.cfg`.
4. **VÍDEO.** Janela/tela cheia, escala, filtro, escala inteira, escala interna, taxa de apresentação, widescreen e aspecto, desentrelaçamento.
5. **APRIMORAMENTO.**
   - Perfis de qualidade (§4.4).
   - B+, com modelo, backend, de-dither, intensidade e budget.
   - Modo C, com conjunto de planos (AnimeSharp/Flux) e modo (off/planes/all).
   - Shader: preset, editor de parâmetros e "Abrir .glslp do RetroArch…".
   - **Pré-visualizar:** roda o jogo oculto por alguns frames com as opções atuais e `EC_SHADER_CAPTURE`/`EC_BPLUS_CAPTURE`, e mostra a imagem lado a lado com o original. Requer uma janela GL mínima; se não houver GL, avisa.
6. **ÁUDIO.** Volume mestre, frequência, bypass do filtro, modo de remaster (com explicação), volumes BGM/SFX, reverb, `chip_blend_ost`, caminhos SF2/voices/perfis, status do SoundFont (encontrado ✓ / ausente ✗) e o atalho **"ESTÚDIO DE REMASTER"** (§10).
7. **CONTROLES** (§7).
8. **JOGO.** Desbloquear chefe, rewind (ligado, profundidade, intervalo) e diagnóstico.
9. **SAVES.**
   - Grade dos slots 1–9 com a miniatura (`native_save_N.bin.thumb`), data e hora.
   - Ações: apagar, exportar, importar e "Iniciar carregando o slot N", que exige um suporte novo de `--load-slot N` no runner. Se não for aprovado, fica só a gestão de arquivos.
10. **AVANÇADO.**
    - Pacing, FPS alvo, porta de debug, "Gravar áudio (WAV)" e logs.
    - "Abrir settings.ini".
    - "Restaurar padrões" (por aba ou geral, sempre com confirmação).
11. **SOBRE.** Créditos, versões do jogo/engine/commit (o runner já imprime `[evidence] build exe=… git …`), licenças (shaders libretro, fontes OFL, TinySoundFont) e links da documentação.
12. **ESTÚDIO DE REMASTER** (§10). Mapear os instrumentos FM do jogo por música, com prévia sintetizada, A/B com o original e "Testar no jogo".

Cada opção mostra a **ajuda** na barra inferior, gerada do esquema. Opções que exigem reinício do jogo ganham um selo "↻". Valores fora do padrão ganham um marcador "●".

---

## 6. Arte extraída da ROM (sem distribuir conteúdo protegido)

`tools/launcher/extract_ui_assets.py`, executado na primeira vez em que se escolhe uma ROM válida, com barra de progresso no launcher:
1. Roda `EternalChampionsRecomp.exe` headless com um `--input-script` que navega até a **tela de título** e o **menu principal**. Usa `EC_BPLUS_CAPTURE` com B+ v3 HQ, além de `--hd-manifest` se houver, e grava os frames 4×.
2. Recorta os elementos: logo, emblema 武士道, blocos de pedra (um bloco que se repete), yin-yang (frames da rotação), letras douradas disponíveis e a fonte branca do menu. Os glifos da fonte vêm dos tiles da VRAM, com dump via `DUMP_VRAM` do input script e paleta da CRAM.
3. Salva em `%APPDATA%/EternalChampionsRecomp/ui/` (ou `build/ui_cache/`), **fora do git**, com um `manifest.json` que inclui o SHA da ROM. Se a ROM mudar, a extração é refeita.
4. **Fallback:** sem ROM ou sem extração, o launcher usa o tema procedural (pedra gerada por ruído com a paleta §3.1) e as fontes OFL de `launcher/assets/fallback/`.

---

## 7. Controles e combinações

### 7.1 O que existe hoje no runner
- Os 12 botões do Mega Drive (UP/DOWN/LEFT/RIGHT, A/B/C, START, X/Y/Z, MODE) e até 6 jogadores.
- `device`: 1 teclado, 2 pad, 3 ambos. `pad_type`: 0 = 3 botões, 1 = 6 botões. `deadzone` 0..100.
- Bind de teclado: `key.<btn> = <scancode>`. Bind de pad: `pad.<btn> = button:N | axis:N:+ | axis:N:- | none`. O formato correto é **`button:N`**; a documentação atual diz `btn:N`, o que está errado.
- Hoje cada botão físico corresponde a um único botão do Mega Drive. **Não há combos nem autofire.** O turbo é de emulação (Tab / Back).

### 7.2 Novidade necessária no runner (pré-requisito L0)
Mudança em `segagenesisrecomp/runner/input_map.{c,h}` e `gamepad.c`. É o repositório compartilhado: coordenar e commitar separadamente.

```ini
[input.p1]
; combo: botão físico → vários botões do Mega Drive (OR com os binds normais)
combo.1 = pad:button:10 -> a+b+c        ; RB = 3 chutes
combo.2 = pad:axis:5:+  -> x+y+z        ; RT = 3 socos
combo.3 = key:44 -> x+a                 ; tecla Espaço = X+A
; autofire opcional por botão (Hz), 0 = desligado
autofire.x = 0
```
- Até 8 combos por jogador. A origem é qualquer bind (tecla, botão ou eixo/gatilho com o limiar da deadzone).
- Os combos são resolvidos no mesmo ponto em que `input_map` monta a máscara do pad, uma vez por frame, de forma **determinística** (o input já faz parte do estado gravado em replays e netplay).
- Gatilhos LT/RT do Xbox entram como `axis:4:+` e `axis:5:+`, com limiar.

### 7.3 Tela de Controles
- Seletor de jogador (P1–P6) e de dispositivo, com o nome do controle detectado (SDL_GameControllerName) e um ícone por família (Xbox, PlayStation, Nintendo, genérico).
- **Dois diagramas lado a lado:** o controle Mega Drive de 6 botões e o controle físico detectado. Ao focar um botão do Mega Drive, o botão físico correspondente acende, e vice-versa.
- **Bind:** "Aperte o botão para *Y*…" com timeout de 5 s; **Esc/Back cancela**. Conflitos são detectados e o launcher oferece trocar os dois binds.
- **Combos:** lista com "+ Novo combo": aperte o botão físico e depois marque os botões do Mega Drive (A B C X Y Z START MODE).
- **Testar entrada:** mostra em tempo real os botões do Mega Drive acionados, com combos e deadzone aplicados.
- **Presets** (por família de controle):

| Preset | Socos (X/Y/Z) | Chutes (A/B/C) | Extras |
|---|---|---|---|
| **Arcade 6 botões (Xbox)** | X, Y, RB | A, B, RT | LB = X+A, LT = Z+C |
| **Clássico Mega Drive** | LB, Y, RB | X, A, B | igual aos defaults do runner |
| **Stick de luta (8BitDo/Hori)** | 1ª fileira | 2ª fileira | — |
| **Teclado** | U, I, O | J, K, L | Espaço = X+A |

Os atalhos de pad do runner (LB = quicksave, RB = quickload, Back = turbo/overlay) **conflitam** com esses presets. O launcher mostra o aviso e oferece desativá-los, o que exige a chave nova `[input] pad_hotkeys = 0` no runner (incluída na L0).

---

## 8. Fases de implementação

| Fase | Entrega | Critério de pronto |
|---|---|---|
| **L0 — Pré-requisitos** | Chaves `[ec.bplus]`, `[ec.hd]` e `[ec.game]` em `ec_spec.c`; `short_name` do EC conhecido; correção da doc (`button:N`, p1..p6, turbo = Tab, `chip_blend`); runner com combos, autofire e `pad_hotkeys` (repositório separado); `--load-slot N` (opcional) | Harness `--quick` com det=0 e `wram_diff=0`; jogo lê as chaves novas sem o launcher |
| **L1 — Esqueleto** | `launcher/` no CMake, janela SDL/GL, loop de telas, input unificado (pad/teclado/mouse), tema fallback | Abre, navega e sai pelo controle |
| **L2 — Esquema e ini** | `schema` com todas as opções da §4; ini que preserva comentários e chaves desconhecidas; montagem do argv | Teste: carregar → salvar sem mudanças dá **diff zero**; cada opção lida e escrita confere com o código |
| **L3 — Telas** | Menu, ROM, Vídeo, Aprimoramento, Áudio, Jogo, Saves, Avançado, Sobre; perfis de qualidade; ajuda | Todas as opções da §4 editáveis; JOGAR inicia o jogo com as flags certas (verificado pelo log `[evidence]`) |
| **L4 — Controles** | Tela de binds, combos, testar entrada, presets | Combos funcionando no jogo; replay determinístico |
| **L5 — Arte extraída** | `extract_ui_assets.py`; tema final com pedra, logo, yin-yang e fontes do jogo | Visual fiel às referências; fallback funcional sem ROM |
| **L6 — Pré-visualização** | Captura com as opções atuais (B+, shader, Modo C) lado a lado | Imagem coerente com o jogo |
| **L6b — Estúdio de Remaster** (§10) | Leitor e gravador do `voices.toml` e dos perfis; catálogo C++ (port do `voice_catalog.py`, com o mesmo hash e o vetor `ce6df9ba`); prévia com tsf; A/B com o original; "Testar no jogo"; hot-reload no jogo (L0) | O `voices.toml` gravado pelo launcher é idêntico ao do `voice_catalog.py` para a mesma edição; a prévia bate com o render do `mixer_studio.py` (mesmo RMS e hash do PCM); "Testar no jogo" abre a música certa com o mapeamento novo |
| **L7 — QA e docs** | Teste em 1080p, 1440p e 4K, com Xbox, PS, teclado e mouse; doc `docs/LAUNCHER.md` | Revisão independente, checklist de acessibilidade (contraste, tamanho de fonte) |

---

## 9. Decisões (usuário: "prossiga", 2026-11-25, seguindo as recomendações)
1. **Arquitetura A**: executável próprio, `EternalChampions.exe`.
2. **Runner muda** (`segagenesisrecomp`): combos, autofire, `pad_hotkeys` e `--load-slot`. Os commits nesse repositório são separados e só tocam os arquivos desta frente; as alterações de terceiros pendentes lá ficam intactas.
3. **Arte extraída da ROM** na primeira execução, com o tema procedural como fallback.
4. **Fontes OFL** de fallback: *Rye* nos títulos e *Press Start 2P* / *Silkscreen* no texto.
5. **Sons de interface:** nenhum por padrão. A extração da ROM pode entrar depois.
6. **Catálogo do Estúdio portado para C++**, sem dependência de Python.
7. **Hot-reload** do `voices.toml` e dos perfis no jogo, já na L0.

---

## 10. Estúdio de Remaster (integrado)

Leva para o launcher o fluxo da etapa A5 (remaster por instrumento), com a identidade visual do jogo e navegação por controle.

### 10.1 Conceito
O driver de som reutiliza cada canal FM com vários instrumentos. Por isso o remaster mapeia **instrumentos (patches FM)**, não canais: são 52 no jogo todo, e 16 aparecem em mais de uma música. Uma edição num instrumento vale para **todas as músicas** que o usam (o `voices.toml` global). Overrides por música ficam em `[voices.<hash>]` dentro do perfil da faixa. PSG, ruído e DAC continuam por canal, no perfil da música. O contrato do hash está em `docs/audio_voice_hash.md`.

### 10.2 Layout
```
┌ ESTÚDIO DE REMASTER ─────────────────────────────── [Música: ◄ Vietnam / Midknight ►] ┐
│ ▶ PLAY  ■ STOP  ⟲ LOOP   [A: ORIGINAL | B: REMASTER]   ⏱ 00:12 / 00:40   ▁▃▅▇▅▃ (VU)    │
├─ INSTRUMENTOS DESTA MÚSICA (por nº de notas) ─────────┬─ INSTRUMENTO SELECIONADO ────────┤
│ ◉ 5ec7df75  "Baixo slap"    GM 38  ●  S  M  ▮▮▮▯      │ Nome: [Baixo slap         ]      │
│   1e04081c  "Brass"         GM 62     S  M  ▮▮▯▯      │ Modo: ◄ GM ►  (GM/Percussão/      │
│   b65d91f5  "Hi-hat FM"     PERC 42   S  M  ▮▯▯▯      │        Chip original/Off)        │
│   c778a64f  —               CHIP      S  M  ▮▮▯▯      │ Programa: ◄ 38 Synth Bass 1 ►    │
│ ── canais ──                                          │ Banco 0 · Transp. +0 · Vol -2 dB │
│   PSG 0  GM 80   S M · PSG ruído PERC · DAC (chip)    │ Nota de bateria: (só percussão)  │
│                                                       │ ⚠ edição GLOBAL: usado em 10      │
│                                                       │   músicas (lista)                 │
│                                                       │ [Patch FM] ALG 5 · FB 7 · ops ▤   │
│                                                       │ Notas 36–89 · mediana 57 · 253 ms │
│                                                       │ Sugestão: brass (62) [aplicar]    │
└─ Y ajuda · X padrão · LB/RB música · Start: TESTAR NO JOGO · Select: SALVAR ─────────────┘
```
- **Lista de instrumentos** da música, ordenada por presença. Cada linha tem o hash, o nome editável, o mapeamento atual, solo (S), mute (M), um medidor de atividade em tempo real durante o play e "●" quando difere do valor salvo.
- **Painel do instrumento:**
  - modo `gm` / `percussion` / `chip` / `off`;
  - programa GM com busca por categoria (Piano, Baixo, Cordas, Brass, Lead, Pad…);
  - banco, transposição, volume e `drum_note` (com nome GM da peça: 42 closed hi-hat…);
  - escopo da edição: **global** (padrão) ou "só nesta música" (grava override no perfil da faixa).
- **Visualização do patch FM:** diagrama do algoritmo (operadores e conexões), envelopes ADSR por operador desenhados a partir de AR/D1R/D2R/SL/RR, multiplicadores e feedback. Ajuda a reconhecer o instrumento original.
- **Biblioteca:** aba com os **52 instrumentos do jogo inteiro**, filtrável ("não mapeados", "usados em mais músicas", por sugestão). Cada um tem o "ouvir em contexto", que toca o trecho de uma música onde ele aparece.

### 10.3 Áudio da prévia
- **Remaster (B):** síntese nativa com `tsf.h` e o **mesmo SF2 do jogo**, a partir do MIDI por instrumento gerado em memória (port C++ do `voice_catalog.py`, a partir dos dumps `GENESIS_AUDIO_APPLIEDDUMP`). Solo, mute e volume são instantâneos.
- **Original (A):** o jogo roda headless no Music Test com `--audio-remaster off --wav <tmp>`. O WAV é carregado e reamostrado em RAM, e o temporário é apagado. Fica em cache por sessão.
- **A/B sincronizados:** os dois tocam do mesmo ponto; a troca A↔B é instantânea, com crossfade de 20 ms.
- **"Chip" na prévia:** um instrumento em `chip` fica mudo na prévia B, com o aviso de que ele soa no jogo. Opcional futuro: renderizar o FM original desse instrumento isolado, usando a máscara de áudio do chip numa captura headless por canal.
- **TESTAR NO JOGO:** salva as edições e abre o jogo no **Music Test** já na música selecionada, com `--audio-remaster hybrid`. Com o hot-reload (decisão 7), as edições seguintes são ouvidas sem reiniciar o jogo.

### 10.4 Primeira execução e dados
- Na primeira entrada no estúdio, sem dumps em cache, o launcher roda a extração (equivalente ao `extract_all_midis.py --keep-temp`) com barra de progresso: cerca de 17 músicas × 40 s de jogo headless. Os dumps e MIDIs ficam no cache do usuário, **fora do git** (são derivados da ROM). O `voices.toml` e os perfis ficam em `assets/audio_remaster/`, versionados.
- **Salvar:** grava só os campos editáveis, preservando `[voices.<hash>.meta]`, comentários e ordem. A gravação é atômica (escreve num temporário e depois renomeia) e mantém um backup `.bak`.
- **Desfazer/refazer** na sessão (Ctrl+Z / Y com LB pressionado). "Restaurar sugestão" por instrumento e "Restaurar tudo".

### 10.5 Pré-requisitos e riscos
- Lembrete da decisão 6 (catálogo em C++): o hash precisa bater com o contrato, e o vetor `ce6df9ba` vira teste do launcher.
- O **hot-reload** é opcional, mas faz muita diferença no fluxo. No `game/ec_audio.c`, ele verifica a data de modificação do `voices.toml` e dos perfis a cada N frames, fora do drain, e reaplica o mapeamento no próximo key-on. Não afeta o determinismo, porque é só host.
- A captura do original leva cerca de 1 min por música; ela é feita em segundo plano, com indicador, sem bloquear a tela.
- Sem a ROM configurada, o estúdio fica desabilitado, com uma explicação.
