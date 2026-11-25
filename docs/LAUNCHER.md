# Launcher do Eternal Champions Recomp (`EternalChampions.exe`)

Frontend gráfico do projeto: escolhe a ROM, edita as opções do `settings.ini`, mapeia os controles, abre o Estúdio de Remaster e inicia o jogo. O visual vem das telas do próprio jogo (arte extraída da ROM do usuário). O plano de projeto e o histórico de decisões estão em [`launcher/PLAN.md`](../launcher/PLAN.md); a referência completa de flags e chaves do ini está em [`CONFIGURATION_AND_CLI.md`](CONFIGURATION_AND_CLI.md).

> Windows x64 e Linux x86_64 (verificados); macOS compila pelo mesmo CMake, mas ainda não foi testado. No Windows os diálogos são os nativos (comdlg32); no Linux usam `zenity` ou `kdialog` (sem nenhum dos dois, o launcher avisa e os diálogos ficam indisponíveis); no macOS, `osascript`. Fora do Windows as fontes da interface vêm de TTFs do sistema (DejaVu, Liberation, Noto…; override com `EC_LAUNCHER_FONT=<arquivo.ttf>`). Nos caminhos abaixo, `%APPDATA%\EternalChampionsRecomp` corresponde a `$XDG_CONFIG_HOME/EternalChampionsRecomp` (padrão `~/.config/…`) no Linux e a `~/Library/Application Support/EternalChampionsRecomp` no macOS.

---

## 1. Como executar

- O launcher é o alvo `EternalChampions` do CMake, compilado junto com o jogo por `build.cmd` (Windows) ou `build.sh` (Linux/macOS) (`game/CMakeLists.txt`, opção `EC_BUILD_LAUNCHER`, ligada por padrão). Para desligar: `build.cmd reconfigure -DEC_BUILD_LAUNCHER=OFF` / `./build.sh reconfigure -DEC_BUILD_LAUNCHER=OFF`.
- Saída: `build\EternalChampions.exe`, ao lado de `build\EternalChampionsRecomp.exe` (o jogo). O launcher procura o jogo na própria pasta e trata a pasta-pai como raiz do projeto quando ela tem `assets\` (é daí que vêm `assets\shaders`, `assets\models`, `assets\audio_remaster`, `assets\soundfonts` e `pipeline\`).
- Uso normal: dê dois cliques em `EternalChampions.exe` (ou rode sem argumentos). A janela é 1280×720 virtuais, escalada para a janela real.
- Abertura: "PRESS START" aparece até o primeiro input. Se a ROM já estiver configurada, a abertura passa sozinha em 3 s.

---

## 2. Primeira execução e onde ficam os arquivos

### 2.1 Escolher a ROM

Menu **ROM** › "Selecionar arquivo…" (diálogo do sistema). Também aparecem as ROMs recentes (até 8) e as encontradas em `roms\` (extensões `.gen`, `.md`, `.bin`, `.smd`).

- O launcher calcula o **SHA-256** do arquivo e compara com o da versão US suportada (`6d8bdaf8c994d9f718c8f991900c58cdf1af84e030524b57f6dda263cc4ea247`).
- ROM válida: é gravada em `rom-EC.cfg` (uma linha com o caminho absoluto, o mesmo formato que o runner lê) e a extração da arte começa.
- ROM com SHA diferente: aviso, **não é gravada**; "Usar mesmo assim" grava, sem garantias.
- O rodapé do menu principal mostra a ROM atual e se o SHA confere (✓/✗).
- **JOGAR** sem ROM configurada leva direto à tela ROM.

### 2.2 Arte do jogo (tema)

Com uma ROM válida e sem arte atual, o launcher roda em segundo plano `tools\launcher\extract_ui_assets.py` (progresso no canto da tela; log em `build\logs\extract_ui.log`). O script abre o jogo headless para capturar a abertura e o menu, e recorta logo, pedra, yin-yang, cabeçalho e fontes.

- **Exige Python no PATH** (o `python.exe` real, não o atalho da Microsoft Store; `py -3` também serve) com `numpy` e `Pillow`.
- Saída em `%APPDATA%\EternalChampionsRecomp\ui` (com `manifest.json` que guarda o SHA da ROM e a versão do extrator; se qualquer um mudar, extrai de novo). Nada disso entra no git.
- **Fallback:** sem ROM válida, sem Python, sem o script ou se a extração falhar, o launcher usa o **tema procedural** (pedra gerada, logo e fontes do sistema). A tela **Sobre** informa qual tema está em uso e o motivo.

### 2.3 O que o launcher grava

| Arquivo / pasta | Conteúdo |
|---|---|
| `build\settings.ini` | O mesmo `settings.ini` do jogo (ver §6). O launcher preserva comentários, ordem, BOM UTF-8 e chaves que não conhece; valores iguais ao padrão são removidos do arquivo; valores com ` ;`, ` #` ou espaços nas pontas são gravados entre aspas. Gravação atômica por `settings.ini.launcher.tmp` (o runner usa `settings.ini.tmp`). Se o runner regravar o arquivo enquanto o launcher está aberto, o launcher recarrega e reaplica **só** as chaves que ele mudou. Na primeira gravação da sessão ele guarda `settings.ini.bak` (estado anterior). Falha de gravação aparece como aviso na tela. Também grava `[launcher] skip_launcher = 1` (se não conseguir, JOGAR segue com `--no-launcher`). |
| `build\rom-EC.cfg` | ROM padrão (caminho absoluto). |
| `build\launcher_recent.txt` | ROMs recentes. |
| `build\logs\launcher.log` | Log do launcher (ROM, perfis aplicados, comando de JOGAR). |
| `build\logs\game_last.log` | stdout/stderr da última partida iniciada pelo launcher (`[BPLUS_GL]`, `[SHADER]`, `[EC-AUDIO]`...). |
| `build\logs\extract_ui.log` | Log do extrator de arte. |
| `build\native_save_N.bin` (+ `.thumb`) | Savestates dos slots 1–9 (formato do runner; a tela Saves só lê, copia e apaga). |
| `%APPDATA%\EternalChampionsRecomp\ui\` | Arte extraída da ROM. |
| `%APPDATA%\EternalChampionsRecomp\preview_cache\` | Imagens da pré-visualização do Aprimoramento (uma por combinação de opções + SHA da ROM). |
| `%APPDATA%\EternalChampionsRecomp\remaster_cache\` | Dumps das músicas para o Estúdio de Remaster (derivados da ROM; fora do git). |
| `assets\audio_remaster\voices.toml` e `profiles\*.toml` | Mapeamento de instrumentos editado pelo Estúdio (versionado). |

---

## 3. Telas

Menu principal: **JOGAR · ROM · VÍDEO · APRIMORAMENTO · ÁUDIO · ESTÚDIO DE REMASTER · CONTROLES · JOGO · SAVES · AVANÇADO · SOBRE · SAIR**.

Nas telas de opções cada linha mostra a ajuda na parte de baixo, "•" marca valor diferente do padrão, "↻" marca opção que só vale ao reiniciar o jogo, e linhas que dependem de outra (ex.: modelo B+ sem B+ ligado) ficam apagadas. As alterações são gravadas no `settings.ini` na hora. Páginas com mais de uma aba trocam com LB/RB (Q/E).

### 3.1 Vídeo

| Opção | Chave / flag |
|---|---|
| Escala da janela (1–8) | `[video] window_scale` |
| Tela cheia (janela / sem borda / exclusivo) | `[video] fullscreen` |
| Filtro linear | `[video] linear_filter` |
| Escala inteira | `[video] integer_scale` |
| Escala interna (1x–64x) ↻ | `[video] internal_scale` |
| Taxa de apresentação (off / 120 / 144 / do monitor) | `[video] presentation_rate` (0 / 120 / 144 / -1) |
| Widescreen e proporção (fit, 16:9, 21:9, 32:9) | `[mods.widescreen] enabled`, `aspect` |
| Widescreen por argumento (vence o ini; `stage` e `W:H` livre só editando o ini) | `[launcher.args] widescreen` → `--widescreen` |
| Exibição entrelaçada (padrão do jogo / TV / Raw) | `[launcher.args] interlace` → `--interlace-display tv|raw` (o runner só aceita `tv` e `raw`; `auto`, `bob` e `weave` não existem). Valor inválido no ini aparece como "Padrão". |

### 3.2 Aprimoramento

Cinco abas.

- **Perfis:** aplicam várias opções de uma vez; depois dá para ajustar tudo. O perfil ativo aparece com "✓ ativo"; se as opções não batem com nenhum, a tela mostra "Personalizado". Os custos são **medidos numa RTX 5060 Ti**, em ms por quadro:

| Perfil | O que liga | Custo medido |
|---|---|---|
| Original | tudo desligado (B+, shader, Modo C, remaster de áudio) | ≈ 0 ms |
| Remaster Leve | B+ v3 fast (1,9 ms em 4:3 / 2,9 ms em widescreen) + de-dither `both` + shader `remaster-crisp` (≤ 0,1 ms) + áudio `hybrid` | ≈ 2–3 ms |
| Remaster Máximo | B+ v3 HQ (4,0 ms em 4:3 / 6,3 ms em widescreen) + de-dither `both` + Modo C `planes` (AnimeSharp) + shader `remaster-hq` + áudio `hybrid` | ≈ 4–7 ms |
| Sem neural (GPU fraca) | sem B+, shader `lite-scalefx` (≈ 0,1–0,2 ms), áudio `dsp` | ≈ 0,2 ms |

- **B+ neural:** `[ec.bplus] enabled`, `model` (lista só os pesos presentes em `assets\models`: v3 fast, v3 HQ, v3 HQ JaNai, v2 Flux, v1 ESPCN), `backend` (auto / cuda / gl), `dedither` (off / cbod / both), `intensity` (0–4), `budget_ms` (0–33; 0 = sem limite). Avisos na própria tela: modelos v2/v3 pedem `both`; o backend CUDA não suporta de-dither nem intensidade diferente de 1,0.
- **Modo C:** `[ec.hd] manifest` (Nenhum / AnimeSharp se existir `pipeline\manifest.json` / Flux se existir `pipeline\manifest_flux.json`) e `mode` (off / planes / all).
- **Shader:** `[ec.video] shader_preset` (off + os `.glslp` de `assets\shaders\presets`, ou "Escolher .glslp…" para um preset do RetroArch) e um slider por `#pragma parameter` do preset, gravados em `[ec.video] shader_params` (`NOME=valor,...`). Trocar de preset limpa os parâmetros.
- **Pré-visualizar:** "Gerar pré-visualização" roda o jogo por alguns segundos (≈ 10–25 s; **timeout de 90 s**, e a linha **Cancelar pré-visualização** encerra o jogo antes disso) até a **tela de abertura**, com as opções atuais, e mostra lado a lado o quadro nativo 320×224 e o resultado (rótulo: shader, B+ ou shader + B+). O resultado fica em cache (só vale com `_orig.png`, `_enh.png` e `_kind.txt` completos; as capturas são gravadas em `.tmp` e renomeadas) enquanto as opções, a ROM, o exe do jogo, os pesos do modelo, o `.glslp` e seus shaders não mudarem; se mudarem, o painel avisa "desatualizada". Precisa de ROM válida e de uma janela OpenGL (o jogo abre uma janela rápida). A captura usa `EC_SHADER_CAPTURE` e `EC_BPLUS_CAPTURE` (ver [`CONFIGURATION_AND_CLI.md`](CONFIGURATION_AND_CLI.md) §2.3).

### 3.3 Áudio

Três abas.

| Aba | Opção | Chave / flag |
|---|---|---|
| Saída | Volume mestre (0–100), frequência (32000 / 32040 / 44100 / 48000), sem filtro do hardware | `[audio] volume`, `frequency`, `bypass_filter` |
| Remaster | Modo (off / dsp / hybrid / ost) | `[ec.audio] remaster` |
| Remaster | Volume da música, dos efeitos (0–2), reverb DSP (0–1) — só com o remaster ligado | `[ec.audio] bgm_vol`, `sfx_vol`, `dsp_reverb` |
| Remaster | Mistura do chip (só no modo `ost`; vazio = valor do perfil da faixa) | `[launcher.args] audio_chip_blend` → `--audio-chip-blend` |
| Remaster | Recarregar o mapeamento ao vivo (voices.toml e perfis) | `[ec.audio] hot_reload` |
| Arquivos | SoundFont, `voices.toml`, pasta de perfis | `[ec.audio] sf2`, `voices`, `profiles_dir` |

A aba Arquivos mostra o status do SoundFont (✓ encontrado / ✗ ausente; padrão `assets\soundfonts\TimGM6mb.sf2`). Sem ele, `hybrid` e `ost` voltam para o DSP.

### 3.4 Estúdio de Remaster

Ver §5.

### 3.5 Controles

Ver §4.

### 3.6 Jogo

| Opção | Chave |
|---|---|
| Desbloquear chefe | `[ec.game] unlock_boss` |
| Rewind: ligado, profundidade (10–600), intervalo (1–60) | `[rewind] enabled`, `depth`, `interval` |
| Diagnóstico de replay | `[mods.diagnostics] enabled` |

### 3.7 Saves

Grade 3×3 dos slots 1–9 com miniatura, data/hora e tamanho de `native_save_N.bin`. Enter abre as ações do slot:

- **Jogar a partir deste slot:** inicia o jogo com `--load-slot N` (uso único).
- **Exportar… / Importar…** um savestate `.bin`. A importação valida o arquivo (cabeçalho `ECRB2sav`, mesmo tamanho dos saves existentes e que não seja a ROM), pede confirmação se o slot já estiver ocupado e move o save atual para `native_save_N.bin.bak`; a miniatura antiga é apagada.
- **Apagar** (com confirmação).

### 3.8 Avançado

- **Execução:** cadência de quadros (`--pacing`: padrão / fiber / accurate; o runner só aceita `fiber` e `accurate`, o padrão compilado é `accurate`) e FPS alvo (`--target-fps`: padrão, 30, 50, 75, 90, 120), ambos em `[launcher.args]`; widescreen legado do VDP (`[video] widescreen`, `widescreen_cells`).
- **Desenvolvedor** (oculto até ligar "Mostrar opções de desenvolvedor"): porta de debug TCP (`--port`), descartar evidências (`--fresh-evidence`), log de yields (`--yield-log`) e gravar áudio (`--wav <arquivo>`).
- **Arquivos e padrões:** abrir `settings.ini`, abrir a pasta de logs e "Restaurar" por aba (Vídeo, Aprimoramento, Áudio, Jogo, Avançado) ou **TUDO**, sempre com confirmação. Controles e chaves desconhecidas do ini não são tocados.

### 3.9 Sobre

Versão do launcher (`1.0.0`) e do jogo (`git describe` gerado a cada build; "desconhecida" sem git), aviso de que a ROM não é distribuída, licenças de terceiros (SDL2, libpng/zlib, TinySoundFont, shaders libretro) e o estado do tema (arte extraída ou procedural, §2.2).

---

## 4. Navegação e controles

### 4.1 Navegar no launcher

| Ação | Teclado | Controle | Mouse |
|---|---|---|---|
| Mover | setas ou WASD | D-pad ou analógico esquerdo | passar o mouse; roda = cima/baixo |
| Confirmar | Enter, Espaço ou Z | A | clique esquerdo |
| Voltar | Esc ou Backspace | B | clique direito |
| Trocar de aba | Q / E ou PgUp / PgDn | LB / RB | — |
| Ajuda do item | F1 ou H | Y | — |
| Restaurar padrão | X ou Delete | X | — |
| Ajustar valor | ← → | ← → | clique direito diminui |
| **JOGAR** (em qualquer tela) | F5 | Start | — |
| Salvar (Estúdio) | F2 | Back/Select | — |

### 4.2 Tela Controles

Cinco abas (LB/RB): **Jogador · Botões · Combos · Autofire · Testar**. Ao lado da lista, o desenho de um controle Mega Drive de 6 botões e do controle físico detectado (com a família: Xbox, PlayStation ou Nintendo, e os nomes dos botões dela); o botão focado acende nos dois.

- **Jogador:** escolhe P1–P6, o dispositivo (nenhum / teclado / controle / ambos → `device` 0–3), o tipo (3 ou 6 botões → `pad_type`) e a zona morta (`deadzone`, 0–100%). Esta aba também tem o preset e o atalho do pad (abaixo).
- **Botões:** Enter e depois aperte uma tecla ou botão/gatilho do controle (**5 s; Esc cancela**). Se o bind já pertence a outro botão, o launcher oferece trocar os dois.
- **Combos:** um botão físico (botão do pad, gatilho/eixo ou tecla) aciona vários botões do Mega Drive, somado aos binds normais. Até 8 por jogador. "+ Novo combo": aperte a origem e marque os destinos; X ou clique direito apaga.
- **Autofire:** repetição por botão de 0 (desligado) a 30 Hz na interface (o runner aceita até 60 por edição manual do ini).
- **Testar:** mostra no diagrama, em tempo real, o que o jogo receberia (binds + combos + zona morta + autofire).

**Presets** (aplicados ao jogador selecionado; reescrevem binds, combos e autofire dele):

| Preset | Resumo |
|---|---|
| Clássico Mega Drive | Padrões do runner; remove combos e autofire. |
| Arcade 6 botões (Xbox) | Socos X, Y, RB; chutes A, B, RT; LB = X+A, LT = Z+C. |
| Stick de luta (8BitDo/Hori) | Primeira fileira (X, Y, LB) = socos; segunda (A, B, RB) = chutes. |
| Teclado | Socos U, I, O; chutes J, K, L; Espaço = X+A. |

**Conflito com os atalhos do pad.** Por padrão o runner usa LB = quick save, RB = quick load e Back = turbo/overlay. Binds ou combos nesses botões conflitam; a tela avisa e, ao aplicar um preset que os usa, oferece desligar os atalhos. A chave é `[input] pad_hotkeys = 0|1` (padrão 1); as teclas F e Tab não mudam.

**Formato no ini** (o mesmo que o runner lê; detalhes em [`CONFIGURATION_AND_CLI.md`](CONFIGURATION_AND_CLI.md) §4.8):

```ini
[input]
pad_hotkeys = 1

[input.p1]
device = 3                         ; 0 nenhum, 1 teclado, 2 controle, 3 ambos
pad_type = 1                       ; 0 = 3 botões, 1 = 6 botões
deadzone = 25
key.a = 29                         ; scancode SDL
pad.a = button:2                   ; button:N | axis:N:+ | axis:N:- | none
combo.1 = pad:button:9 -> x+a      ; origem: pad:button:N | pad:axis:N:+|- | key:SC
autofire.x = 0                     ; Hz
```

---

## 5. Estúdio de Remaster

Edita o mapeamento dos **52 instrumentos FM** do jogo (e dos canais PSG/ruído/DAC por música) para o SoundFont, com prévia sonora. Contrato do hash de instrumento: [`audio_voice_hash.md`](audio_voice_hash.md); arquitetura do motor de áudio: [`audio_remaster_architecture.md`](audio_remaster_architecture.md).

### 5.1 Fluxo

1. **Primeira vez:** exige ROM válida e o SoundFont (ver §3.3). Sem os dumps em cache, a tela oferece "extrair agora": o launcher roda o jogo headless nas 17 músicas, ≈ 40 s cada, **≈ 12 min no total**, em segundo plano, com barra de progresso e cancelamento (Esc). Os dados vão para `%APPDATA%\EternalChampionsRecomp\remaster_cache`.
2. **Editar:** a lista mostra os instrumentos da música atual (LB/RB troca de música) e os canais; o painel à direita edita nome, modo (GM / percussão / chip original / off), programa GM, banco, transposição, volume (dB), nota de bateria, solo e mudo, mostra o patch FM (algoritmo/feedback) e a sugestão automática. **Escopo:** "Global" (vale para todas as músicas que usam o instrumento; a tela avisa em quantas) ou "Só nesta música" (override no perfil da faixa). **BIBLIOTECA** lista todos os instrumentos do jogo, com "Ouvir em contexto".
3. **Ouvir:** a prévia **B (remaster)** é sintetizada em tempo real com TinySoundFont e o SoundFont TimGM6mb (ou o configurado). A **A (original)** é capturada uma vez por música: o jogo roda headless com `--audio-remaster off --wav <tmp>` e o áudio fica só em RAM (≈ 1 min, em segundo plano). A/B toca do mesmo ponto e a troca é instantânea (crossfade de 20 ms).
4. **Salvar:** grava `voices.toml` e o perfil da faixa de forma atômica, preservando comentários, ordem e `[voices.<hash>.meta]`. O `.bak` guarda o estado de **antes da sessão** (criado só no primeiro salvamento de cada arquivo). Trocar de música (LB/RB) salva antes o perfil pendente; se `<slug>.toml` não existe, ele é criado na primeira edição "só nesta música" a partir de `default.toml` (que nunca é alterado). O Estúdio usa `ec.audio.voices` e `ec.audio.profiles_dir` do ini quando definidos (mesma regra do jogo). Extrair dumps, capturar o original e testar no jogo exigem ROM válida. **Desfazer/Refazer** valem na sessão; "Restaurar do disco" reverte o instrumento. Sair com alterações pede confirmação.
5. **TESTAR NO JOGO:** salva e abre o jogo no Music Test, na música atual, com `--audio-remaster hybrid --audio-voices ... --audio-profiles-dir ...`. Com `[ec.audio] hot_reload = 1`, edições seguintes são ouvidas sem reiniciar.

### 5.2 Atalhos

| Ação | Teclado | Controle |
|---|---|---|
| Play / pausa | Espaço | botão PLAY da barra |
| Solo / mudo do instrumento | S / M | campos Solo e Mudo |
| Alternar A/B | B | botão A/B da barra |
| Loop | L | botão LOOP |
| Voltar ao início | Home | — |
| Música anterior / próxima | — | LB / RB (ou Q/E, PgUp/PgDn) |
| Desfazer / refazer | Ctrl+Z / Ctrl+Y | botões DESFAZER / REFAZER |
| Salvar | F2 | Back/Select |
| Testar no jogo | F5 | Start |
| Restaurar | X ou Delete | X |
| Ajuda do Estúdio | F1 ou H | Y |

### 5.3 Limitações

- A prévia B **não reproduz o chip nem a equalização do jogo**: é só o SoundFont. Instrumentos em modo `chip` ficam mudos na prévia (eles soam no jogo).
- A captura do original cobre **≈ 40 s por música** (`frames_per_track = 2400`); trechos mais longos não estão no A/B.
- O jogo aberto por "Testar no jogo" é um processo à parte; fechá-lo é por conta do usuário.

---

## 6. Como o launcher inicia o jogo

**JOGAR** (ou Start/F5) executa, com o diretório de trabalho na raiz do projeto:

```
EternalChampionsRecomp.exe --no-launcher "<rom>" [flags de [launcher.args]]
```

- `--no-launcher` impede que o launcher genérico do runner apareça por cima. O launcher também grava `[launcher] skip_launcher = 1` no ini.
- Só vão como argumento as opções que **não têm chave no ini**, guardadas na seção `[launcher.args]` do `settings.ini` e que o launcher traduz para flags: `--interlace-display`, `--widescreen`, `--pacing`, `--target-fps`, `--audio-chip-blend`, `--port`, `--fresh-evidence`, `--yield-log`, `--wav` (só as que estão fora do padrão). Tudo o mais o jogo lê do `settings.ini`, que é **o mesmo arquivo** que o launcher edita: o que você muda no launcher vale para o jogo, com ou sem o launcher, e a CLI do jogo continua vencendo o ini.
- Slot de Saves: acrescenta `--load-slot N` uma única vez.
- Durante a partida o launcher fica minimizado e "JOGANDO" mostra o yin-yang girando. Ao terminar, a janela volta, o `settings.ini` é relido (o runner pode ter regravado) e aparece o código de saída. Se foi diferente de 0 (ou o jogo não abriu), há atalhos para a pasta de logs e para `last_error.log`.
- stdout/stderr do jogo vão para `build\logs\game_last.log`.

---

## 7. Flags de teste e diagnóstico do launcher

Servem para automação e capturas; não aparecem na interface. Lista conforme `launcher/src/main.cpp`.

| Flag | Efeito |
|---|---|
| `--shot <arquivo.png>` | Renderiza a tela escolhida, sem interação, e grava o canvas 1280×720 em PNG (janela oculta). Não grava `rom-EC.cfg`. |
| `--screen <nome>` | Tela da captura: `splash`, `main` (padrão), `rom`, `about`, `placeholder`, `fonttest`, `studio`, `controls`, `video`, `enh`, `audio`, `game`, `saves`, `adv`, `confirm`. |
| `--frames <N>` | Quadros simulados a 60 fps antes de capturar (padrão 30). |
| `--size <LxA>` | Tamanho da janela de captura (padrão 1280x720). |
| `--tab <N>` / `--focus <N>` | Aba e linha focada nas telas de opções, Controles e Estúdio (só em `--shot`). |
| `--state <N>` | Estado da captura: em Controles, 1 = captura de bind aberta e 2 = editor de combo; no Estúdio, estados de teste (editor, patch FM, captura do original, extração, barra). |
| `--dev` | Mostra as opções de desenvolvedor do Avançado. |
| `--ini <arquivo>` | Usa outro `settings.ini` (não toca o real). |
| `--real-ini` | Opt-in explícito: deixa `--apply-profile` / `--apply-controls-preset` / `--run-preview` gravarem no `settings.ini` **real**. Sem `--ini` nem `--real-ini`, essas flags operam numa cópia temporária que é apagada ao sair. |
| `--apply-profile <id>` | Aplica um perfil de Aprimoramento: `original`, `remaster_leve`, `remaster_maximo`, `sem_neural`. |
| `--apply-controls-preset <id>` | Aplica um preset de controles ao P1: `classico`, `arcade`, `stick`, `teclado`. |
| `--run-preview` | (com `--shot`) roda de verdade a pré-visualização do Aprimoramento e espera; imprime `preview: OK/FALHOU`. |
| `--autoplay` | Pula a abertura e clica em JOGAR ao abrir; ao fim da partida grava `launcher_autoplay.txt` (`exit_code=N`) ao lado do exe e sai. |
| `--game-args "<args>"` | Argumentos extras repassados ao jogo (ex.: `--max-frames 120`). |
| `--selftest-ini <settings.ini>` | Testa o leitor/gravador do ini (carregar→salvar sem mudanças = idêntico, edição preserva o resto, aspas, BOM, mudança concorrente do runner, `.bak` só na primeira gravação, lote) em uma cópia `.selftest`; imprime PASS/FAIL e `RESULT OK/FAIL`. |
| `--selftest-studio` | Teste do núcleo do Estúdio sem janela (dumps + voices.toml + SoundFont, prévia com energia, A/B, salvar numa cópia com `.bak`, `voices.toml` real intacto). Inclui clamp de áudio (`volume_db` absurdo), troca de música com perfil pendente e perfil novo a partir de `default.toml`. Usa `--root`, `--dumps`, `--out` (diretório **novo ou vazio**; nunca é apagado). |
| `--root <pasta>` / `--dumps <pasta>` / `--voices <arquivo>` / `--out <pasta>` | Raiz do projeto, dumps prontos, cópia do `voices.toml` e pasta de saída para os testes e capturas do Estúdio. |

Exemplos:

```
build\EternalChampions.exe --shot out\enh.png --screen enh --tab 0 --ini out\settings_teste.ini --apply-profile remaster_maximo
build\EternalChampions.exe --selftest-ini build\settings.ini
build\EternalChampions.exe --autoplay --game-args "--max-frames 120"
```

---

## 8. Limitações conhecidas e pendências

- macOS não testado (código escrito, nunca compilado num Mac). No macOS, se o launcher morrer por crash ou SIGKILL, processos filhos (jogo, extração) não são encerrados junto.
- **QA em andamento (fase L7):** revisão independente, teste em 1080p/1440p/4K e checklist de acessibilidade ainda não concluídos.
- A navegação **por controle físico** (Xbox, PlayStation etc.) não foi testada de ponta a ponta; o caminho por teclado e mouse é o mais exercitado.
- O preset **Stick de luta** é aproximado (mapeia os botões do SDL, não o layout físico de cada stick); os rótulos de **Nintendo** também são aproximados.
- A pré-visualização do Aprimoramento mostra só a tela de abertura, não uma luta.
- O Estúdio não reproduz chip/EQ na prévia e captura ≈ 40 s por música (§5.3).
