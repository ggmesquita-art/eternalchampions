# ROADMAP.md — EternalRecomp

> Roadmap operacional do projeto, derivado do `DESIGN.md` v0.3 (2026-11-07).
> Status: v0.1 (2026-11-18). Os marcos M0–M8/N1–N3 citados aqui são os do DESIGN §16.
> Cada fase lista tarefas concretas, a ferramenta local a usar e o critério de saída.

---

## 0. Fatos verificados (2026-11-18)

### 0.1 Identidade da ROM de referência

Arquivo: `roms/eternal_champions_us.gen` (3.145.728 bytes = 24 Mbit, confirma §4 do DESIGN).

| Campo | Valor |
|---|---|
| SHA-256 | `6d8bdaf8c994d9f718c8f991900c58cdf1af84e030524b57f6dda263cc4ea247` |
| SHA-1 | `5e978217c10b679a42d7d2966a4ccb77e1715962` |
| MD5 | `bde8b2396241aa27463ca875e604b8d3` |
| Header | `SEGA GENESIS (C)SEGA 1993.OCT`, serial **GM MK-1145**, região **U** |

→ **Resolve a questão aberta §18.1 (parcialmente):** a ROM local é a versão US. Falta só confirmar a revisão contra o banco No-Intro (é o primeiro passo da Fase 0) e gravar tudo em `reference/roms.toml`.

### 0.2 Ferramentas presentes na pasta

| Ferramenta | O que é | Papel no projeto |
|---|---|---|
| `segagenesisrecomp/` | **Genesis Recomp** (mstan): framework beta de recompilação estática de Mega Drive/Sega CD. Recompilador 68k→C (`recompiler/src/`: `m68k_decoder.c`, `code_generator.c`, `function_finder.c`…), runner com VDP/áudio/barramento/Z80 compartilhados, **fallback interpretado**, suporte a Sega CD (`feature/segacd-backend`), tools Python (`frame_wram_diff.py`, `genesis_cosim.py`, `divergence_report.py`…), integração Ghidra (`tools/ghidra/SonicAnnotationProject.java`), docs densos (VDP batching, GPU raster, Z80 static recomp, netplay). | **Base principal das Fases 2–3** e modelo de layout `game.toml`/seeds/jumptables. |
| `ares/` | Emulador ciclo-a-ciclo em C++ (`ares/md/`: cpu, vdp, apu, opn2, mcd). | Referência dinâmica da Fase 1 (debugger) e **base do P2** (fallback previsto no DESIGN §11.6). |
| `StreetsOfRageProject/` | Repo do projeto homônimo. ⚠️ **Os 4 submodules (`RageDecompiler`, `MegaDriveEnvironment`, `StreetsOfRageRecompilation`, `MegaDriveEnvironmentSampleGame`) não estão inicializados** (pastas vazias). Precisa de rede: `git submodule update --init --recursive`. | Referência de arquitetura (decompiler próprio + runtime SDL3). Opcional: só se for adotado o caminho de runtime próprio (§8.3). |
| `sonic3air-26.03.28.0-stable/` | Código-fonte do Sonic 3 A.I.R. (Oxygen engine, lemonscript, librmx). | **Modelo do produto final** (§15): lógica traduzida + engine novo + widescreen. Referência de renderer e estrutura de dados de assets. |
| `RGen-1.0.0-compiler-sources/` | Fontes do toolchain WinLibs **GCC 14.2.0 + binutils 2.43.1 + mingw-w64 12.0.0** usado pelo RGen (compilador dos projetos de recomp). | Só para builds Windows com toolchain idêntico ao dos projetos de referência. Baixa prioridade. |
| `rea-agents` (MCP) | Servidor REA 6.9.0 (`reverse-engineer-anything`), 255 ferramentas, Evidence determinístico. | Assistente de análise em todas as fases — ver §7. |
| `.venv/` | Python 3.12.3 virtualenv. | Ferramentas Python do projeto (`tools/`). |

### 0.3 Lacunas de toolchain (instalar na Fase 0)

| Ferramenta | Uso (DESIGN) | Status |
|---|---|---|
| `vasmm68k_mot` | Montador da Fase 1 (§6.2) | ❌ não instalado |
| Ghidra 12.x | Análise estática (§6.2) **e provider de análise do REA** (§7) | ❌ não instalado |
| Genesis Plus GX (core libretro) | Referência do harness (§8) | ❌ não instalado |
| Exodus / debugger MAME | Active disassembly (§6.2) | ❌ não instalado (opcional; ares cobre parte) |
| Musashi (núcleo 68k) | Fallback (§7.2) — desnecessário se adotarmos o runner do Genesis Recomp (§8.3) | ❌ |

## 0.4 Progresso da Fase 0 (2026-11-18)- [x] `git init` + `.gitignore` (`roms/`, `assets/`, `disasm/data/`, ferramentas externas)
- [x] `reference/roms.toml` com todos os hashes + validação de header (checksum `$3630` bate)
- [x] `tools/split.py` — gera `disasm/main.asm` (64 vetores `dc.l` + `incbin`)
- [x] `tools/verify.py` — monta com `vasmm68k_mot` e compara byte a byte → **idêntico** ✓
- [x] vasm 1.9d compilado em `tools/vendor/vasm/` (espelho GitHub; site oficial fora do ar)
- [x] Ghidra 12.2 + JDK 21.0.13 em `tools/vendor/` (para o REA, §7)
- [x] Config do REA no `opencode.json` atualizada (`GHIDRA_INSTALL_DIR` + `JAVA_HOME` 21) — **exige reiniciar a sessão/opencode para valer**
- [x] **REA operacional na ROM** (após `/restart` do usuário + troca do JDK 21→25 via symlink): `open_binary` raw-binary m68k no Ghidra 12.2 (`68000:BE:32`), **268 funções descobertas** (cluster principal `0xC000`–`0x11000`), `analyze_function`/pseudocódigo/xrefs validados (ex.: `FUN_000006f0` = laço de espera com flag Z80 em `0xA01A60`). Primeiras hipóteses em `disasm/symbols/labels.toml`; Evidence exportado em `reference/rea/`
- [x] Primeiro commit: `ea6b14c` (repo leve, sem as ferramentas externas)
- [ ] Conferência da revisão No-Intro (DAT-o-MATIC bloqueia sem aceitar termos no navegador)
- [ ] M0.5 — teste de boot em ares: build bloqueado sem `libgtk-3-dev`/`libsdl2-dev` (sem sudo). Opções: pedir senha do sudo para `apt install`, ou compilar só o núcleo (`md.cpp` inclui m32x/SH2 → arrastaria qon; adiado)
- [ ] CI de hash (GitHub Actions ou hook local)

**Descobertas da Fase 1 já registradas:** Reset `$000200`, VBlank `$0006B6`, IRQ2 `$0006E2`, demais exceções `$0006DE`; HBlank aponta para `$00FF0000` (desabilitado no boot — anotar no disasm).

**Fase 1 em andamento (2026-11-18, tarde):** regiões `$000200`–`$0002F9` (reset/init de hardware: TMSS, registros VDP, stub do Z80), `$0006B6`–`$00083B` (VBlank, espera de quadro com handshake Z80, aquisição/liberação de barramento, upload do driver de som, ring buffer) **convertidas em asm real** (`disasm/code/boot.asm`, `disasm/code/irq.asm`), com `verify.py` **byte a byte idêntico** ✓. Confirmações: programa do Z80 em `$000A80` (0x1886 bytes → driver de som próprio, §18.2), protocolo busreq/reset em `0xA11100`/`0xA11200`, contadores de quadro em `$FFFFFD72/76` (hipótese).

**Fase 1 (noite):** tela de boot/trava de região (`$2FA–$6B5`), interface de comandos do Z80 (`$83C–$8A7`), entrada do main + laço externo (`$2230–$25FD`) e sombra VDP (`$2786–$27B3`) convertidas — tudo com hash idêntico. Esqueleto do jogo mapeado via REA (267 pseudocódigos exportados): `Main_Entry → Main_OuterLoop → FUN_0000fcde → Main_FrameLoop ($FD24)` com 7 subsistemas por flag, handshake Z80 dentro do laço, alocador em `$D4CC`, câmera/tilemap em `$C836–C83A` (`Map_GetTileAddr $1032A`). Ver `docs/main_loop.md`.

**Fase 1 (madrugada):** camada de comandos do som (`$8A8–$A7F`) convertida. **`tools/vendor/ares_cli/` compilado** — driver headless do núcleo do ares (Mega Drive): roda N quadros e despeja a RAM de trabalho. Primeiro dump (600 quadros) **confirmou 12 hipóteses** (contadores, estado=7 no título, flags de subsistema, lock do Z80 balanceado). Run de 2400 quadros: estado evoluiu 7→13, câmera ativa. Descoberta: **tabela de dispatch de estados em `$27B4`** (22+ handlers espalhados por `$50–$4A9C`) — a região que o Ghidra não mapeou (despacho indireto, risco §17).

**Fase 1 (dia 2):** máquina de estados convertida (`state_table.asm` com 40 handlers, `state_dispatch.asm` com FrameRun/SetState/Dispatch/PostChange/init do sistema e da câmera, `state_handlers1.asm` + `state1_handler2.asm` com o estado 1 completo). **ares_cli com input programável** (`--press quadro:botão`, FightingPad de 6 botões) e `--trace` por quadro. Mapa dinâmico: boot→**7**(título)→Start→**9**(menu)→Start→**1**(setup). Estado 1 não progride no ares — depende da detecção de pad (`$9CB0`), suspeita de diferença no handshake de 6 botões do ares (comparar com Genesis Plus GX). Escritores de `$FD64`: menu→7/13, boot→7/9/1 (via State_SetState), `$2FE59C`→29, restore via `$9B66`.

---

## 1. Decisões estratégicas (gates)

| # | Decisão | Quando | Recomendação |
|---|---|---|---|
| D1 | Revisão da ROM de referência | Fase 0 | Fixar a US local; conferir revisão no No-Intro e gravar em `reference/roms.toml`. |
| D2 | **Adotar o Genesis Recomp como motor das Fases 2–3** em vez de escrever transpilador + runner + fallback próprios (DESIGN §7 descreve a construção própria) | Fim da Fase 1 | **Adotar.** O framework já entrega recompilador, runner, fallback e ferramentas de cosim que o DESIGN prevê construir; o uso privado (§3) permite. O trabalho do projeto vira a *integração do jogo* (game.toml, seeds, spec de vídeo, harness), não a infraestrutura. Fallback: caminho próprio do DESIGN com Musashi. |
| D3 | Runtime próprio (MegaDriveEnvironment do SoR) vs runner do Genesis Recomp | M2 | Runner do Genesis Recomp primeiro (chega ao M2 mais rápido). Reavaliar no M4 se os modos B/C pedirem outra arquitetura (§18.4). |
| D4 | VDP do modo A | M2 | O do runner do Genesis Recomp (mesma base do harness → resposta natural à §18.9). ares fica como segunda opinião de precisão. |
| D5 | Base do P2 (emulador com camadas) | N2 | **ares** (local, é o fallback oficial do DESIGN §11.6). clownmdemu só se for baixado depois e passar no teste empírico. |
| D6 | Formato dos assets HD (§18.5) | M5 | Definir durante o PoC de 1 personagem: começar com PNG por quadro + manifesto; migrar para atlas se a memória pedir. |
| D7 | GPU mínima e tamanho da rede (§18.8, §18.7) | N1 | Medir no P1 antes de fixar. |

---

## 2. Visão geral das fases

```
Fase 0 (setup, M0) ──► Fase 1 (disasm, M1) ──► Fase 2 (recomp, M2) ──► Fase 3 (harness, M3)
                                                                              │
        Trilha neural: N1 (P1 shader) ──► N2 (P2 ares+layers) ──► N3 (P3 runtime)  (paralela, §11.6)
                                                                              ▼
                          Fase 4 (renderer B/C + widescreen, M4–M6) ──► Fase 5 (assets HD, M7) ──► Fase 6 (Sega CD, M8)
```

Caminho crítico: **M1 e M3 decidem o projeto** (DESIGN §16). A trilha neural avança em paralelo desde o dia 1 e não depende do disassembly.

---

## 3. Fase 0 — Setup e esqueleto → M0

**Critério (M0):** repositório criado, `split.py` e `verify.py` funcionando, CI com hash.

1. `git init` na raiz; `.gitignore` com `roms/` e `assets/` inteiros (DESIGN §3).
2. Criar `reference/roms.toml` com os hashes do §0.1; criar `reference/` com notas de hardware.
3. Verificar a revisão da ROM no banco No-Intro (US, `Eternal Champions (USA)`); registrar revisão/CRC32.
4. Instalar `vasmm68k_mot` (apt ou download do vasm).
5. **M0.5 — Teste de boot em ares:** compilar `ares` (CMake está pronto) e rodar a ROM até a tela de título. Confirma que o dump está íntegro e que ares servirá de referência dinâmica. *(Também mitiga o risco "clownmdemu/ares imprecisa" — é o teste empírico do DESIGN §11.6 feito com o que está local.)*
6. Escrever `tools/split.py` (gera `disasm/` todo em `dc.w`/`incbin` a partir da ROM) e `tools/verify.py` (monta com vasm e compara byte a byte). Primeiro build **já tem que bater o hash** (§6.1).
7. CI mínimo (GitHub Actions local runner): `python tools/verify.py` no push.
8. **REA:** configurar Ghidra 12.x + `GHIDRA_INSTALL_DIR` e validar o fluxo da §7.1.
9. *(Opcional)* Inicializar submodules do StreetsOfRageProject para estudo do RageDecompiler e do MegaDriveEnvironment.

**Saída:** repo verde, ROM íntegra e identificada, ares rodando o jogo.

---

## 4. Fase 1 — Disassembly reconstruível → M1

**Critério (M1):** 100% do código alcançável marcado como código, hash idêntico, mapa de RAM cobrindo a estrutura do lutador e o estado da luta.

Ordem de trabalho (prioridades do DESIGN §6.4):

1. **Pontos de entrada:** vetor de reset `0x000004`, autovetores de interrupção, VBlank/HBlank → converter essas regiões para código real, uma por vez, com `verify.py` a cada passo (§6.1).
2. **Laço principal + leitura de controle** (padrão de polling do VBlank — essencial para o §7.3 depois).
3. **Estrutura do lutador na RAM:** usar **ares (debugger, watchpoints)** + dumps diferenciais para localizar posição, estado, quadro de animação, vida, hitboxes. Gravar em `ram_map.toml` com confiança `confirmed`/`hypothesis` (§6.3). *(REA entra aqui — ver §7.2.)*
4. Máquina de estados de golpes e tabelas de animação; **tabelas de ponteiros tipadas** (§6.3 — mitigação do risco de despacho indireto, DESIGN §17).
5. Formato de metasprites (montagem de um quadro a partir de sprites de hardware).
6. Cenários: tilemaps dos planos A/B, scroll, paletas.
7. IA da CPU.
8. Driver de som: só a interface 68k↔Z80 (§12) — responde a §18.2.

**Ferramentas:** Ghidra (+REA) para estática; ares (ou Exodus, se instalado) para dinâmica; vasm + `verify.py` como gate de hash; LLMs para propor nomes/comentários (`; ?` até confirmação, §14).

**Fase 1 também responde §18.3:** mapear efeitos de HBlank/raster (impacta o renderer depois).

---

## 5. Fase 2 — Recompilação estática → M2

**Critério (M2):** executável nativo chega à tela de título (VDP modo A, fallback permitido).

Caminho recomendado (decisão D2): usar o **Genesis Recomp**.

1. Estudar o fluxo do `sonicthehedgehog/` como exemplo mínimo de integração: `game.toml`, `sonic1.disasm_seeds.toml`, `sonic1.disasm_jumptables.toml`, `sonic1.disasm_subs.toml`, `sonic1_spec.c`, `sonic1_video.c`.
2. Compilar o recompilador (`cmake -S recompiler -B build/recompiler && cmake --build ...` → `GenesisRecomp.exe`).
3. Criar o projeto `eternalchampions/` (repo próprio ou pasta no nosso): `game.toml` com identidade da ROM (hashes do §0.1), seeds = código mapeado na Fase 1, jumptables tipadas.
4. Gerar o C (`GenesisRecomp.exe rom --game game.toml --output-dir generated`), ligar ao runner, escrever o adaptador de boot (verificação de ROM, frame execution, integração de título).
5. **Laços de espera de VBlank:** registrar no `game.toml`/spec os padrões de polling descobertos na Fase 1 para virarem `end_frame()` (§7.3).
6. Manter o **fallback interpretado** ligado; jogar e alimentar o ciclo de descoberta de endereços (o framework já traz isso).
7. Validar contra ares: boot → título → seleção → primeira luta.

**Saída:** jogo nativo até a tela de título, fallback permitido. (M2 não exige a luta completa — o M3 exige.)

---

## 6. Fase 3 — Harness e determinismo → M3

**Critério (M3):** suíte de movies 100% idêntica à referência, zero fallback no modo arcade.

1. Instalar Genesis Plus GX (core libretro) — referência do DESIGN §8.
2. Formato `.ecm` (cabeçalho: hash da ROM + seed + estado inicial; 1 linha de input por quadro, bitmask de 12 botões).
3. Gravação/replay de input no nosso runner; amostragem no `end_frame()` (§13).
4. Diff de RAM de 64 KiB ao fim de cada quadro vs referência, com nomes do `ram_map.toml`. *(Os `tools/*.py` do Genesis Recomp — `frame_wram_diff.py`, `divergence_report.py`, `genesis_cosim.py` — servem de base ou de referência de design.)*
5. Suíte de CI: cada personagem × cada arena, finalizações, treino, IA em dificuldades variadas.
6. **Ciclo de descoberta:** zerar `missing_addresses.log` no modo arcade (§7.1) — jogar, recompilar, repetir.
7. Exportar frame data (startup/active/recovery/hitboxes) para `docs/frame_data/` — documentação + teste de regressão.

**Saída:** determinismo total + base pronta para o renderer (e para rollback/netplay no futuro, §18.6).

---

## 7. Fase 4 — Renderer → M4 → M6

**Critérios:** M4 = renderer B com escala arbitrária e shaders, paridade visual com o modo A. M6 = todas as arenas em 16:9.

1. Modo A (emulado) como referência — já existe do M2.
2. Modo B: ler estado do VDP (planos A/B, window, sprites, paletas, scroll) e montar a cena na GPU (§9). Referência: docs do Genesis Recomp (`ENGINE_VDP_BATCHING.md`, `ENGINE_GPU_RASTER.md`, `ENGINE_RASTER_FUSION.md`) e o renderer do S3AIR (Oxygen) como modelo.
3. Modo C: manifesto `hash → asset HD` (§9.1) — protótipo com 1 personagem e 1 arena (M5).
4. Widescreen por arena (M6): decisões em `docs/widescreen.md` (§9.2); referência: `WIDESCREEN_ISSUES.md` do Genesis Recomp e as soluções do S3AIR.
5. Interpolação de quadros (§9.3): só no final, desligada por padrão.

---

## 8. Fase 5 — Pipeline de assets HD → M7

**Critério (M7):** elenco e arenas completos, assets `approved`.

1. Extrator de quadros de sprite (metasprite + paleta + contexto), deduplicação por hash (§10.1) — **usa o P2/emulador com camadas da trilha neural**, como o DESIGN prevê.
2. Upscale com consistência temporal: folha de referência HD por personagem → img2img/IP-Adapter/ControlNet com seed fixa (§10.2).
3. Upscale sobre índices/luminância, não sobre cor final (paletas dinâmicas); mapas de região por índice.
4. Workflows ComfyUI versionados em `pipeline/workflows/`; manifesto com workflow/modelo/seed/versão.
5. Visualizador lado a lado; status `generated → reviewed → approved`.

---

## 9. Fase 6 — Sega CD → M8

**Critério (M8):** port de *Challenge from the Dark Side* reaproveitando o runtime.

O Genesis Recomp já tem pipeline Sega CD (`feature/segacd-backend`, `docs/SEGA_CD_SETUP.md`, `SEGA_CD_RECOMP_HANDOFF.md`): segundo 68000, Z80, streaming de CD. Só entra depois do M7; sem tarefas agora.

---

## 10. Trilha paralela — Upscale neural (N1 → N3)

Independente da recompilação (§11.6). **Começa já.**

- **N1 — P1 em shader:** rede de bootstrap (ESPCN/FSRCNN pequena) compilada para shaders multi-pass no RetroArch; comparar com integer scaling e Anime4K. Fixa arquitetura, tamanho, GPU mínima (D7) e orçamento. Ferramentas: `neural/train` (PyTorch) → pesos embutidos em shader.
- **N2 — P2 em ares:** fork do `ares/md/vdp` para exportar camadas separadas (planos A/B, sprites por prioridade, window) + frontend próprio com inferência por camada e cache (§11.3); orçamento ≤ 2 ms p99 (§11.4); **extrator de dataset da §10.1 funcionando** (alimenta a Fase 5 antes do M3!). O ponto de corte em ares: o `DAC` calcula pixels de A, B e sprites separados antes da composição (DESIGN §11.6).
- **N3 — P3 no runtime:** integrar o B+ ao renderer (após M4); retreino com assets `approved`.

---

## 11. Onde o rea-agents entra (estado verificado em 2026-11-18)

### 11.1 Diagnóstico feito

| Checagem | Resultado |
|---|---|
| `binary_session` | Servidor `rea-agents` 6.9.0 ativo; **nenhum provider de análise nativo configurado** (Ghidra ❌ sem `GHIDRA_INSTALL_DIR`; Hopper ❌; IDA ❌; Kuna ❌). |
| `open_binary` na ROM | ❌ `unsupported binary format` — o inventário de artefatos do REA só reconhece ELF/PE/Mach-O/APK etc.; **ROM crua de Mega Drive não abre sem um provider como Ghidra**. |
| `inspect_byte_entropy` na ROM | ✅ Funcionou: entropia média 6,25 (código/gráficos sem regiões comprimidas), SHA-256 confirmado. Útil para perfil de regiões código/dados. |
| Ferramentas sem provider | ✅ Disponíveis já: entropia, inventário de arquivos, `capture_process_scenario` (rodar comandos com Evidence), comparações de artefatos. |

### 11.2 Plano de uso do REA no projeto

1. **T0 (Fase 0):** instalar **Ghidra 12.x** (≥12.1) e definir `GHIDRA_INSTALL_DIR`. Depois, tentar `open_binary` da ROM como **raw binary com processador 68000** (Ghidra suporta; a aceitação pelo REA é empírica). 
   - Se abrir: a Fase 1 ganha `list_procedures`, `analyze_function`, `procedure_pseudo_code`, `xrefs`, `list_strings`, `search_bytes`, `read_bytes`, `set_address_name`/`set_comment` (anotações que viram labels/`; ?` no disasm), `binary_overview`, `decompile_all`.
   - Se não abrir: usar o caminho próprio do framework (`tools/ghidra/SonicAnnotationProject.java`, que o Genesis Recomp já traz) fora do REA, e usar o REA nos **artefatos derivados**.
2. **Durante a Fase 1:** fluxo assistido — REA propõe funções/limites/xrefs → anotação vira `disasm/` → **`verify.py` (hash) decide**, conforme §2.6 e §14.
3. **Fases 2–4:** REA nos **binários ELF/PE do projeto** (recompilador, runner, jogo recompilado): `inspect_binary_layout`, `inspect_native_exports`, `inspect_binary_hardening`, unwind/crash (`inspect_elf_*`), comparação de builds (`match_native_functions`, `compare_functions`) — útil para depurar o gerado e medir otimizações.
4. **Trilha neural:** `inspect_model_artifact` (ONNX) para auditar o modelo exportado; comparação de saídas (`compare_artifacts`) nos testes de regressão visual.
5. **Prática:** exportar `export_evidence_bundle` para `reference/rea/` ao fim de cada sessão grande (rastro reproduzível, sem binários da ROM no git — apenas metadados/evidence).

---

## 11.5 Ambiente de desenvolvimento (WSL2, verificado)

- **Host:** WSL2, kernel `6.18.58.1-microsoft-standard-WSL2`, 15 GiB RAM visíveis.
- **GPU:** NVIDIA **RTX 5060 Ti** (driver 616.56) com `nvidia-smi` funcional no WSL → **CUDA disponível** para PyTorch/ONNX Runtime. Atenção: Blackwell (sm_120) exige torch ≥ 2.8 com wheels cu128+.
- **Gráficos:** `/dev/dxg` + Mesa (dzn) — SDL3/renderer funcionam via WSLg, mas Vulkan/OpenGL são traduzidos sobre DX12. Medições de performance (orçamento de 2 ms do N2, 60 fps) precisam ser revalidadas em build Windows nativo.
- **Interop Windows:** `cmd.exe`/`powershell.exe` acessíveis → ferramentas Windows (Exodus, RetroArch, release `GenesisRecomp.exe`) podem ser chamadas do WSL. Preferir mesmo assim builds Linux nativos do que compila de fonte.
- **Limitações conhecidas:** gamepads/arcade sticks não passam por padrão (usar `usbipd-win`); áudio via WSLg tem latência maior; **não colocar repo/builds em `/mnt/c`** (9p é lento) — manter em `/home/...` como está.

## 12. Primeiras ações (esta semana)

1. `git init` + `.gitignore` + `reference/roms.toml` (hashes do §0.1). *(~1h)*
2. Instalar `vasm`; escrever `tools/split.py` + `tools/verify.py`; primeiro build com hash idêntico. *(~1 dia)*
3. Compilar `ares` e rodar a ROM até a tela de título (M0.5). *(~2h)*
4. Instalar Ghidra 12.x + configurar REA; testar `open_binary` da ROM (§11.2.1). *(~2h, incerto)*
5. Compilar o recompilador do Genesis Recomp e rodar o exemplo Sonic 1 de ponta a ponta (só para dominar o fluxo). *(~2h)*
6. Decidir D2 (adotar framework vs construir próprio) — recomendado: adotar.

---

## 13. Ordem de execução e dependências

```
M0 ──► M1 ──────────────► M2 ──► M3 ──► M4 ──► M5 ──► M6 ──► M7 ──► M8
        │                 │      │      │
        └─ (REA+Ghidra, ares, LLMs)
        N1 ──────────────► N2 ──────────────────────► N3 (após M4)
              (N2 alimenta o dataset da Fase 5 antes mesmo do M3)
```

- **M1 e M3 são os gates críticos** (§16). N1/N2 correm em paralelo e são independentes.
- O P2/N2 é **também** a ferramenta de extração da Fase 5 — por isso começar cedo.

## 14. Riscos-chave com mitigação no roadmap

| Risco (DESIGN §17) | Mitigação no roadmap |
|---|---|
| Despacho indireto intenso | Tabelas de ponteiros tipadas na Fase 1 (tarefa 4.4) + ciclo de descoberta do framework (Fase 3.6) |
| Timing fino do VDP | Mapear raster/HBlank já na Fase 1 (§4.8) |
| Emulador impreciso para o jogo (P2) | Teste empírico em ares logo no M0.5 |
| Framework não encaixar no jogo (D2/D3) | Fallback: caminho próprio do DESIGN (Musashi + runtime SDL3, com referência no SoR) |
| Escopo infinito | Marcos sequenciais com critério de saída mensurável (§16) |

## 15. Questões abertas do DESIGN — status

| # | Questão | Status no roadmap |
|---|---|---|
| 1 | Revisão da ROM | Resolvida em 0.1 + verificação No-Intro na Fase 0.3 |
| 2 | Driver de som | Fase 1, tarefa 4.8 |
| 3 | HBlank/raster | Fase 1, tarefa 4.8 |
| 4 | Runtime próprio vs reaproveitar | D3, decidir no M2 |
| 5 | Formato dos assets HD | D6, decidir no M5 |
| 6 | Modos opcionais (rollback etc.) | Avaliar após M3 (Fase 3.7) |
| 7 | Modelo único vs por camada | D7, medir no N1 |
| 8 | GPU mínima | D7, medir no N1 |
| 9 | VDP do modo A | D4 — o do runner (mesma base do harness) |
