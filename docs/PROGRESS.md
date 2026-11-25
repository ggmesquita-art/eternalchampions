# PROGRESS.md — Estado do projeto EternalRecomp

> Documento vivo do progresso do projeto (complementa `DESIGN.md` e
> `ROADMAP.md`). Atualizado em 2026-11-21.

## 1. Objetivo e princípios

Descompilar `roms/eternal_champions_us.gen` (Eternal Champions, Sega
Genesis, 24 Mbit, US — SHA-1 `5e978217…`, SHA-256 `6d8bdaf8…`) em
assembly 68k **reconstruível byte a byte**, com recompilação nativa
futura (Fases 2–3, decisão D2: Genesis Recomp).

Princípios (DESIGN §2.1, §6.3):

- **O hash é a lei**: `tools/verify.py` monta `disasm/main.asm` com
  `vasmm68k_mot -no-opt` e compara byte a byte com a ROM. Nada entra sem
  passar por esse gate.
- **Hipóteses marcadas**: `; ?` no asm e `conf = "hypothesis"` no
  `labels.toml`; `confirmed` exige verificação dinâmica (ou leitura
  direta de vetores/tabelas).
- **Determinismo**: comparação dinâmica contra Genesis Plus GX
  (referência oficial do harness, DESIGN §8).

## 2. Infraestrutura

| Peça | O que é |
|---|---|
| `tools/split.py` | Gera `disasm/gen/vectors.asm` (64 vetores) e `disasm/data/rest.bin` |
| `tools/verify.py` | Monta + compara hash (vasm `-no-opt` + sha256) |
| `tools/disasm.py` | Listagem capstone de qualquer região da ROM |
| `tools/analyze_cluster.py` | Analisa pseudocódigo exportado pelo REA |
| `disasm/symbols/labels.toml` | 243 labels (155 confirmed, 88 hypothesis) |
| `disasm/symbols/ram_map.toml` | 72 entradas de RAM (65 confirmed, 7 hypothesis), incluindo lutadores P1/P2 |
| `tools/vendor/vasm/` | vasm 1.9d compilado localmente |
| `tools/vendor/ares_cli/` | Driver headless do ares (input/trace/dump) |
| `tools/vendor/gpgx_runner/` | Loader libretro do GPGX + filmes `.ecm` |
| REA (MCP) | Ghidra 12.2 como provider; 267 pseudocódigos exportados em `reference/rea/` |

## 3. Estado da conversão (Fase 1)

Regiões **convertidas para asm e hash-verificadas** (`364,5 KB` / `373.204` bytes de código/tabelas em `main.asm`; 100% dos blocos de código 68000 alcançáveis):

| Região | Arquivo | Conteúdo |
|---|---|---|
| `$000200–$0002F9` | `code/boot.asm` | Reset, detecção TMSS ("SEGA"), registros VDP, stub do Z80 |
| `$0002FA–$0006B5` | `code/boot2.asm` | Trava de região (letra em `$1F0`), tela de copyright, fonte 1bpp |
| `$0006B6–$00083B` | `code/irq.asm` | VBlank (IRQ6), IRQ2, handshake de quadro com o Z80 (`0xA01A60/61`), lock do barramento (`$A11100`), upload do driver Z80, ring buffer |
| `$00083C–$0008A7` | `code/z80_cmd.asm` | Interface de comandos para o Z80 |
| `$0008A8–$000A7F` | `code/sound_cmd.asm` | Camada de comandos de som |
| `$002230–$0025FD` | `code/main_entry.asm` | Entrada do main, clear de RAM, modos de boot (07→09→01→FF), laço externo |
| `$002600–$002785` | `code/main_subs.asm` | `Main_InitDefaults`, `Main_CheckPause` (Start / 3 socos / 3 chutes), `Main_PauseLoop` |
| `$002786–$0027B3` | `code/vdp_init.asm` | Sombra de registros VDP |
| `$0027B4–$002803` | `code/state_table.asm` | Tabela de dispatch: 40 handlers (offsets relativos) |
| `$002804–$002ADC` | `code/state_handlers1.asm` + `code/state1_handler2.asm` | Estados 0 (nulo) e 1 (init completo + menu principal) |
| `$002ADC–$00313A` | `code/state_handlers2.asm` | Estados 2–7 (seleção de personagem, título) |
| `$00313A–$00397F` | `code/state_handlers3.asm` | Estados 8–13 (menu-init, menu principal) |
| `$003980–$004541` | `code/state_handlers4.asm` | Estados 14–16 (setup/cenas, 3 TileAnims, tela pré-luta) |
| `$004542–$00C09D` | `code/state_handlers5.asm`..`code/state_handlers13.asm` | **Handlers 17–39 completos**: arenas (`17`–`27`), finalização (`28`), cenas (`29`–`32`), `rts` (`33`–`39`) |
| `$00C09E–$00CD73` | `code/util_combat_input.asm` | Setup de arena/saída (`$C0B2/$C288/$C578`), leitura/mapeamento de pads (`$C870/$CA80`), espera VBlank (`$CB42`), RNG (`$CB82`), cópias/VDP Reg 0 (`$CAD2/$CB18`) |
| `$00CD74–$00DB13` | `code/util_alloc_vdp.asm` | Descompressor (`$D084`), `Mem_Alloc/Free` (`$D4CC/$D5A4`), `VBlank_VdpUpdate` (`$D6A0`), paletas (`$D710–$D7A8`), `Sub_StateMachine` (`$D7E4`) |
| `$00DB14–$00E683` | `code/util_sched_obj.asm` | Escalonador cooperativo (`$DB14–$DC4E`), `Obj_Create` (`$DCEA`), alocação/liberação inline de tiles (`$E408/$E5FC/$E61E`), DMA (`$E648`) |
| `$00E684–$00FD23` | `code/util_engine.asm` | Subsistema `$E684`, rotação 2D + `Math_SinCosTable` (`$F00C–$F16B`), fila DMA (`$F2B6`), camadas/Reg 11 (`$F32C–$F47A`), `$F6A0`, `Frame_RunN` (`$FCDE`) |
| `$00FD24–$00FE1D` | `code/main_frameloop.asm` | Laço de 1 quadro: contador `$FD70`, leitura de pads, subsistemas, sync com o Z80 |
| `$00FE1E–$00FFAB` | `code/state_dispatch.asm` | `FrameRun`, `State_SetState`, `State_Dispatch`, `State_PostChange`, `Sub_FF08`, `Camera_Init` |
| `$00FFAC–$011ACD` | `code/engine_ext.asm` | Câmera/mapa (`$FFAC/$1032A`), texto/hex/dec (`$100C2–$10205`), `TileAnim_Register` (`$109EA`), `$10AA8`, stream HV-sync `$10B74` (tabela `$10C6E`), `$11414`, wave scroll (`$11800+`) |
| `$011ACE–$016F31` | `code/obj_tasks1.asm` | Tarefas de objetos e subsistemas do motor (bloco 1) |
| `$016F32–$016F73` | `code/task_title.asm` | Tarefa de confirmação da tela de título |
| `$016F74–$0172E5` | `code/obj_tasks2.asm` | Auxiliares de animação/temporização de objetos (`Anim_SetFrameWait`/`Anim_Wait`) |
| `$0172E6–$0172F9` | `code/pal_shadow.asm` | Cópia de paleta para buffer sombra (`$FFFF9BB0`) |
| `$0172FA–$01FEFF` | `code/obj_tasks3.asm` | Tarefas de objetos, debug `Debug_PlayerEditScreen` (`$182A0`), `Fighter_AITableAlt` (`$1B1C2`) |
| `$01FF00–$01FF4F` | `code/task_input.asm` | Tarefa cooperativa de input/heartbeat (`$FD61` bit4/bit7) |
| `$01FF50–$026E49` | `code/obj_tasks4.asm` | Tarefas de cena e menu `Menu_ButtonConfig` (`$2644C`) |
| `$028B12–$032193` | `code/menu_system.asm` | Sistema de menus, opções, torneio e motor `Menu_MainEngine` (`$312B8`) |
| `$034B0A–$044001` | `code/ai_fighters1.asm`..`code/ai_fighters3.asm` | Lógica de IA de lutadores (`AI_Fighter0..9_Desc`) e 4 formas do chefe (`AI_Boss*`) |
| `$044002–$05778F` | `code/fight_moves1.asm`..`code/fight_moves3.asm` | Máquinas de estado de golpes e física de luta |
| `$057790–$05E8A3` | `code/combat_core.asm` | Núcleo de combate ativo (`Combat_MainLoop`..`Combat_FighterSync`), câmeras de luta |
| `$2FCD1A–$2FF437` | `code/replay_system.asm` | Instant Replay, estatísticas e controle de torneio (`Replay_StateEntry` `$2FDCEE`, estados 29/32) |

Regiões **puramente de dados** (mantidas em `incbin` — `2.772.524` bytes / `2.707,5 KB`):

| Região | Tamanho | O que é |
|---|---|---|
| `$000A80–$00222F` | 0x17B0 (6.064 B) | Programa do Z80 (driver de som) + dados — por decisão, fica como bloco |
| `$026E4A–$028B11` | 0x1CC8 (7.368 B) | Textos comprimidos de finais e créditos |
| `$032194–$034B09` | 0x2976 (10.614 B) | Textos comprimidos das biografias dos lutadores |
| `$05E8A4–$2FCD19` | 0x29E476 (2.745.462 B) | Gráficos, animações, `Obj_TableMaster` (`$06A522`), `Fight_AnimTableMaster` (`$07F65A`), áudio (0 `jsr`/`jmp` 68k) |
| `$2FF438–$2FFFFF` | 0xBC8 (3.016 B) | Tabelas de dados finais / padding |

## 4. Ferramentas dinâmicas

### 4.1 ares_cli (análise, base do P2)

- Compila só o núcleo do ares via CMake wrapper (sem GUI)
- `--press <quadro>:<botão>` (FightingPad de 6 botões; hold de 20 quadros)
- `--trace`: por quadro — `st` (estado), `mode`, `edge` ($FD54),
  `fd61`, `cca` ($9CCA), `cb6` ($9CB6)
- `--dump <quadro>:<arquivo>`: RAM de trabalho em quadro arbitrário
- Patch no ares: FightingPad com máscara 0xFF (assinatura de detecção
  do 6-button; ver `tools/vendor/README.md`)

### 4.2 gpgx_runner (referência do harness §8)

- Loader libretro mínimo do Genesis Plus GX (compilado localmente)
- `start@N` (Start segurado 20 quadros), `dump@N,arquivo` (até 16),
  `--trace` (estado por quadro)
- **Filmes `.ecm`** (DESIGN §8): cabeçalho `rom <sha256>` (verificado,
  recusa ROM errada) + `frames <N>` + 1 máscara de 12 botões por quadro;
  replay byte-igual ao input direto
- Nota: o GPGX retorna a work RAM com **byte-swap por word** (core LSB)

### 4.3 REA + Ghidra

- Raw-binary m68k, entry `$000000`; 268 funções descobertas no cluster
  `$C000–$11000` (o despacho indireto da tabela de estados não é seguido
  pelo Ghidra — os handlers foram convertidos via capstone + análise)
- Pseudocódigo exportado em `reference/rea/decompile-2026-11-18/`

## 5. Arquitetura descoberta do jogo

### 5.1 Fluxo de boot

```
Reset ($200) → TMSS → VDP init → upload do Z80 ($A80) → tela de
copyright + trava de região → jmp $2230 (Main_Entry)
Main_Entry → clear de RAM → checagem de save RAM (Sub_2322) → modos de
boot (tabela $22DE: 07→09→01→FF) → laço externo
```

- Modos de boot = estados iniciais: modo 07 = título, 09 = menu-init,
  01 = setup, FF = reinicia
- `$9CB6` = "próximo modo pedido" (o executor Sub_246E refaz com d0=$9CB6)
- `$9CCA` = contador de passadas do menu (FFFF→5→6…; ==5 no estado 9 →
  `jmp $200` reset)

### 5.2 Máquina de estados (tabela em `$27B4`, 40 handlers)

| Est | End | Papel | Est | End | Papel |
|---|---|---|---|---|---|
| 0 | `$2804` | nulo (rts) | 13 | `$36B2` | **menu principal** |
| 1 | `$2806` | init completo | 14–17 | `$3980`… | setup/cenas (14–16) e arena 17 (`state_handlers4–5`) |
| 2 | `$2ADC` | seleção de personagem | 18 | `$56F0` | arena (`State2_CharTable`) |
| 3 | `$2E24` | vazio (rts) | 19–27 | `$5B06`… | arenas (`State2_CharTable`, `state_handlers6–12`) |
| 4 | `$2E26` | alias do 10 | 28–31 | `$B56C`… | transição (28), `$2FDCEE` (29), cenas/paletas (30–31) |
| 5 | `$2E2A` | init tilemap/câmera | 32–39 | `$C08C`… | `$2FDCEE` (32), `rts` estados vazios (33–39) |
| 6 | `$2EE8` | `$9352=1` → estado 18 | | | |
| 7 | `$2EF2` | **título** (espera `$FD61` bit0) | | | |
| 8 | `$313A` | arte + espera `$FD61` bit7 | | | |
| 9 | `$3256` | **menu-init** (2 saídas) | | | |
| 10 | `$34D8` | init c/ estado 7 temporário, `jmp $200` | | | |
| 11/12 | `$36A8`/`$36AC` | aliases de 1/13 | | | |

Sequência observada (GPGX/ares sem input): `0→7` (título, quadro ~24) →
`0→9` (menu-init, ~686) → `13` (menu, **timer de attract** ~1582) →
`7→13` (ciclo de attract).

### 5.3 Laço principal e subsistemas

- `FrameRun` ($FE1E) → `Main_FrameLoop` ($FD24): contador `$FD70`++,
  input (se estado≠7 e `$FD69` bit7), subsistemas por bits de `$FD69`,
  sync de fim de quadro (h-sync + `Irq_FrameWait`) quando `$9B32`≠0
- Subsistemas (`$FD69`): bit1→`$DC4E` (escalonador), bit3→`$F6A0`,
  bit4→`$D7E4`, bit5→`$E684`, bit6→`$10AA8`, bit7→scroll/input; extra
  `$9CA2`→`$10B74`
- Contadores: `$FD70` = contador do laço (1× por chamada); `$FD72–FD75`
  = contador 32-bit incrementado no VBlank; `$FD76` = flag de VBlank
  (IRQ seta, FrameRun consome — `CB42` espera por ela)

### 5.4 Subsistema de input

```
Sub_CA80 ($CA80) → leitura crua da porta (TH toggling ×6, detecção de
  6 botões pelos bits de contador)
Sub_C870 ($C870) → mapeia (tabelas de identidade $9274/$92B4/$92A4 na
  RAM) → FD58/FD5A (estado), FD54/FD56 (borda = estado & ~anterior),
  FD50/FD52 (anterior)
Tarefa de input ($1FF00, sob o escalonador) → FD61 bit7 (heartbeat) e
  bit4 (borda vista)
```

- `FD61`: bit0 = confirmar (título), bit1 = alternativo, bit4 = borda de
  pad, bit5 = attract, bit6 = pedido de reset, bit7 = heartbeat da tarefa
- Estado 9 sai do wait loop por: (a) borda bit7 (`$0080`) dos pads →
  `$3492` (compartilhada) → modo 01 → **estado 1**; (b) `$FD61` bit7 →
  `$9CCA++` → `$9CB6=2/$D` → **estado 13**

### 5.5 Escalonador cooperativo

Nodos de tarefa (a4 = nodo atual): `0(a4)` = próximo, `2(a4)` = PC de
resumo, `6(a4)` = contador de delay, `C(a4)` = PC alternativo,
`10(a4)` = contador 2. `$DB14` = yield (salva PC), `$DB28` = salva PC
alternativo, `$DB2E` = decrementa/retoma, `$DB4A` = dispatch round-robin,
`$DC10` = volta ao escalonador. Tarefas no cluster `$FFAC+`/`$16000+`
(ex.: input `$1FF00`, título `$16F32`).

### 5.6 Estrutura de lutador e estado da luta (`disasm/symbols/ram_map.toml`)

Validado por dumps diferenciais de RAM no `gpgx_runner` durante luta ativa (Estado 17 via `Enter Contest`, frames 2150–2750):

- **Slots de lutador** (stride exacto `$0134` = 308 bytes): `Fighter_P2` em `$FFFFAA52–$AB85` (Slot 0 / CPU em 1P) e `Fighter_P1` em `$FFFFAB86–$ACB9` (Slot 1).
- Campos confirmados em ambos (`base + offset`): `+$16` flags/facing, `+$2A..+$31` screen bounding box `(x1,y1,x2,y2)`, `+$40` ID (`$0003`), `+$42` PosX, `+$46` PosY (chão=`216`), `+$50/$54` VelX/VelY (`16.16`), `+$68` Health (`$0068` = 104 inicial; P1 caiu `$0068→$0066→$0057`), `+$6C` CharId (`$0182/$0183`), `+$7A` `TaskNode`, `+$7C` `OpponentPtr` (simétrico), `+$12A` `InputMode` (`$AB7C/$ACB0`).
- Estado global da luta: `$FFFF9CB4` = timer do round em BCD (`$99→$97→$92→$87`), `$FFFF9774/$9776` = barras de vida espelhadas no HUD para P1/P2.

### 5.7 HBlank / Raster e Scroll (DESIGN §18.3)

- **HBlank IRQ (Autovetor 4 → `$00FF0000`) não é utilizado na luta ativa**: no dump da luta, `$FF0000–$FF001F` permanece zerado, `Vdp_Reg0Shadow` (`$FFFFFEFE`) vale `$0004` (IE1 desligado) e `$FFFFC54C` (gate de H-Int em `$CAE6`) vale `$0000` (nenhum escritor ativo encontrado; `$CAEC` tem 0 callers). `Wram_CopyVerifyStub` (`$0117A6`, que referencia `$00FF0000`) também não possui callers na ROM.
- **Efeitos por linha usam VDP Reg 11 (`$8B03` = H-Scroll per-line)**: os handlers das arenas chamam `$00F462` (`Vdp_Reg11HScrollMode` = `3`), confirmado no dump ativo (`$FFFFCCE0 = 3`, `$FFFFCCDE = 0`), alimentado por rotinas como `Bg_WaveScroll1/2/3` (`$11800`/`$11928`/`$11A82`). O renderer da Fase 4 não precisa de callback HBlank 68k por scanline — precisa apenas emular **H-Scroll per-line do VDP**.

### 5.8 Tabelas mestras, IA dos lutadores e Replay/Estatísticas

- **Tabela mestra de objetos (`$06A522`, `Obj_TableMaster`)**: 233 IDs (deslocamentos em word relativos a `$06A522`, descritores de 34 bytes cada) com ponteiros de tarefa/callback em `+$1A` e `+$1E` lidos por `Obj_Create` (`$DCEA`); descritores diretos em `$06C608/$06C62A/$06C64C` (`Combat_CameraTask1..3` `$05B25E/$05B618/$05B6B2`).
- **IA dos lutadores e chefes**: tabelas `Fighter_AITableMain` (`$00C14C`), `Fighter_AITableAlt` (`$01B1C2`) e `State7_StagePtrTable` (`$0030D6`, 9 formas demo) apontam para descritores de 6 longwords (`AI_Fighter0..9_Desc` `$0354A6..$041C06`, além das 4 formas de Eternal Champion `AI_BossDragon..Tiger_Desc` `$0428E6..$043866`). Os 3 primeiros ponteiros indexam tabelas Close/Mid/Far com contador `dbra` (`n + 1` entradas de 8 bytes, ponteiro de ação em `+4`) e os 3 últimos são callbacks diretos.
- **API completa do escalonador e banco superior (`$2FCD1A–$2FF437`)**: além de `$DB0E`/`$DB14`/`$DB4E`/`$DB8E`/`$DB96`/`$DBCA`/`$DBFE`, o motor de objetos e lutadores também usa `$DBAC`, `$DBF0`, `$DBF6`, `$DBFC` (argumento de código em `d0`) e transições em `replay_system.asm` (`$2FCD1A–$2FF437`, incluindo `Replay_StateEntry` `$2FDCEE` chamado pelos estados 29 e 32, buffer de `$3A00` bytes em `Replay_AllocBuffer` `$2FCD56` e HUD em `Replay_DrawHud` `$2FE2A4`). Varredura estática em toda a ROM de 3 MB confirmou zero instruções `jsr`/`jmp` em `$05E8A4–$2FCD19`.

## 6. Investigação e arbitragem da divergência ares × GPGX (BlastEm 1.0.0)

Resumo da investigação ([docs/main_loop.md](docs/main_loop.md)):

1. ⚠️ **Bug inicial corrigido no runner**: a 1ª versão usava `JOYPAD_START=7` (= RIGHT no libretro) — comparações iniciais inválidas. Com START real (`id 3`), refeito tudo.
2. **Alinhamento errado no teste de 19/nov (frame absoluto vs fase do Estado 9)**:
   - `Start@950` absoluto caía em **`State9 + 306`** no `ares` (`9@644`), já dentro de `State9_WaitLoop` (`$33FE`), saindo em `9→1` (`983`, após `33` quadros de fade em `$3492`/`$D750`).
   - O mesmo `950` caía em **`State9 + 262`** no `GPGX` (`9@688`), durante a espera fixa `jsr $FCDE(15)` logo após `$9B62 != 0` (`948`), consumindo a borda `$FD54=$0080` antes do loop de espera `$33FE`.
3. **Arbitragem final em 4 vias (`BlastEm 1.0.0 -g -d` × `GPGX` × `ares` × `Native`)**:
   - **Sem input:** todos coincidem dentro do Estado `9` (`$9B62 != 0` em `$FD70=182` / `vbl=188..191`, attract `9→13` em `$FD70=814` / `vbl=822..827`).
   - **B1 (`State9 + 262`)** e **C1 (`State9 + 112 / +212`)**: **todos os 4** vão para **`9→13`** (`$9376=$0101` em BlastEm/GPGX/Native).
   - **B2 (`State9 + 306`)**, **B3 (`WaitB62 + 40`)** e **C2 (`WaitB62 + 30 / +100`)**: **todos os 4** vão para **`9→1`** com 1 press único em `33–35` quadros (`vbl=253..272`).

**Conclusão:** não existe divergência de protocolo de 6 botões entre `ares`, `GPGX`, `BlastEm 1.0.0` e `Native`. **GPGX** segue como referência dinâmica oficial do harness (com `ares` / `BlastEm 1.0.0` validados).

## 7. Convenções de conversão (vasm)

- Um `org` por arquivo; labels globais entre includes
- **Todo branch com sufixo explícito** `.b`/`.w` (vasm exige; `-no-opt`
  evita otimizações que quebram o hash)
- RAM absoluta com `.w` e nome comentado; ROM não convertida como
  `jsr ($0000XXXX).l ; ?`
- **Argumentos inline**: alguns jsr são seguidos de `dc.w` (a rotina lê
  os args da pilha e volta depois deles) — `$F47A` (5 words), `$E5FC`
  (até `$FFFF`), `$F42A` (4 words), `$FFAC` (5 words)
- Capstone: PC-relativo = endereço da extension word (validado contra o
  vasm); endereços ímpares no bus do ares leem a word anterior

## 8. Questões em aberto

1. **Pad real (RESOLVIDO em 2026-11-19 via BlastEm 1.0.0):** arbitragem de 4 vias (`BlastEm 1.0.0`, `GPGX`, `ares`, `Native`) provou comportamento 100% idêntico quando alinhados ao Estado `9`/`WaitB62` (`B1`/`C1` → `9→13`; `B2`/`B3`/`C2` → `9→1`); `$9376=$0101` confirmado no BlastEm 1.0.0.
2. **(Resolvido)** O atraso de `~33–35` quadros após Start em `State9_WaitLoop` é o fade de saída de `State9_ExitShared` (`$3492` / `$D750`), observado igualmente nos 4 backends (não uma borda espúria).
3. Semântica completa de `$FD61` bit5/bit6 (attract/reset) e do ciclo
   de attract do Sub_246E
4. Revisão No-Intro da ROM (site bloqueia sem aceitar termos)
5. CI de hash (hook local ou GitHub Actions)

## 9. Fases 2–5: Recompilação Nativa, Harness, Widescreen e Extrator (2026-11-19)

### 9.1 Recompilação estática (`M2`) — `build/native/EternalChampionsRecomp`

- **Gerador de configuração** (`tools/recomp/gen_recomp_config.py`): exporta `68.700` inícios de instrução 68000 em `game/ec_code_addrs.txt` e `game/ec_discovery.toml` (`extra=315`, `late_extra=7395`, `inline_conts=209`, `yield_conts=5672`, 15+ tabelas `abs`/`bra_s`/`bra_w`/`pcrel16` incluindo VM HV-sync `$010BFE`/`$010C6E`, `YIELD_APIS` com `$DB0E`/`$DB2E`/`$17192`/`$17196`/`$171AE` e 5 regiões de dados protegidas).
- **Correções estruturais de runtime/codegen:**
  - Propagação de desempilhamento de corrotinas (`_entry_split_sp_popped`) adicionada em `segagenesisrecomp/external/m68k-recomp-core/profiles/genesis/code_generator.c` (`MN_RTS`, `pops_return_unconditionally` e retorno de `MN_JSR`), evitando queda prematura de retorno após `$00DB0E`/`$00DB2E`/`$00DB4A`.
  - Chamada explícita de `ec_on_post_reset()` garantida na primeira entrada de `ec_call_entry_point()` em `game/ec_spec.c` (habilita `PAD_6BUTTON` nas duas portas — corrigindo `$FF9376/$FF9377` de `00/00` para `01/01` idêntico ao GPGX — e carrega `EC_HD_MANIFEST`) e corrigida no próprio runner em `segagenesisrecomp/runner/main.c:3227` (`on_post_reset()` chamado após `glue_init()`, com `s_boot_init_done` tornando `ec_on_post_reset()` idempotente em `game/ec_spec.c`).
  - Ciclo de instruções do interpretador unificado em `segagenesisrecomp/runner/m68k_interp.c` + `segagenesisrecomp/cmake/GenesisRecompRunner.cmake`: removido estimador local defasado (`MN_DBcc=12` vs `10`, escrita `-(An)` `10/6` vs `8/4`, bug de `dst_ea`) e ligado `common/m68k_timing.c` para usar `(int)m68k_68000_nominal_cycles(ins)` em `interp_account_cycles()`, igual ao `code_generator.c:1810` (eliminou drift de 1 VBlank no quadro 8 e inversão de `btst.b #$1,($FFFFFD71).w`).
- **Artefato gerado:** `build/native/EternalChampionsRecomp` (`17.103.584` B, build Release com `GENESIS_NATIVE_IPO=OFF`, zero erros). Smoke de 600 quadros headless (`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software`) alcança Estado `7` (`$FD70=532`, `$FD72=552`), grava `boot_600.{ram,vram}` e `title.png` (`320x224`).

### 9.2 Harness determinístico `.ecm` (`M3`) — `harness/ec_harness.py`

- **Autodeterminismo nativo (Camada 2, 13/13 filmes):** `0/65536` bytes divergentes em WRAM e PNG SHA-256 idênticos (`aac9990bc9be` em `01_attract_boot.ecm`, `f88e69dfec7d` em `02_title_to_fight.ecm`) com **zero** misses de execução (`floor_unsafe=0`, `dispatch_misses=0`, `interior_label_misses=0`, `floor_coverage=0`) nos 13 filmes.
- **Equivalência da recompilação estática (Camada 1 — `Recomp` × `GENESIS_FORCE_INTERP=1`, `01`..`12`):** `12/12` com **`gameplay_match=True`**, **`png_eq=True`** (PNG pixel a pixel idêntico), **`pad=0x0101/0x0101`**, contador VBlank **`$FD72` exato em 12/12** (`75`, `1891`, `2416`, `2410`, `2422`, `2400`, `2403`, `2408`, `2401`, `2419`, `2415`, `1904`) e **`65.534/65.536` bytes idênticos** em todos os 12 filmes (`13_boss_sanctuary_s18.ecm` N/A por hook C `EC_DIAG_BOSS_SANCTUARY=1`). Os únicos `2/65536` B divergentes em todos os 12 filmes são `$FFC8B7` (`0xA0 != 0x4C`) e `$FFC8BB`, LSBs mortos do cursor de boot de `Sub_10B74`/`Sub_10BFE` (`$FFC8B6` `VramStream_VramCur` + `$FFC8B8` `VramStream_Ptr`), deixados após o `move.w #$0, ($FFFF9CA2).w` de `Sub_10C60` em `disasm/code/engine_ext.asm:1065-1156`.
- **Comparação com `gpgx_runner` (Camada 2, `byte-swapped-16bit` desfeito via `i^1`):**
  - Sondagem pré-RNG (`f=1..75`) mostrou `0` diffs em `f=1..2`, apenas `10`–`24` B de pacing LZ em `f=10, 30, 70` e `vbl=26` / `$FEFA=0x5BDB` idênticos em `f=75`, quando `Math_RandomRange` (`$CB82`) lê `$00C00008` (HV-counter) e diverge no sorteio de `$9CBE` (`3` Jetta vs `0` Shadow), causando também defasagem entre `BlastEm 1.0.0`, `GPGX` e `ares`.
  - `01_attract_boot.ecm` (1.000 q): ambos atingem Estado `9` (`menu-init`); diff final `729/65536` bytes.
  - `02_title_to_fight.ecm` (2.900 q): ambos percorrem Estado `7 → 9 → 1 → 16 → 17` com telemetria P1/P2 (HP, coordenadas `102 (273,216) / 104 (232,216)` e timer `$0092`) idêntica até o quadro `2600` (`diff=599/65536` em `2600`; `2195/65536` em `2900`, `P1 HP=78` vs `76` no GPGX e timer BCD `$0082` idêntico). Em `01`..`12`, acordo exato em `$FD64`, `pad_cfg=0x0101` e IDs P1/P2 (diff residual `640`–`2.216/65536` B por leitura HV em `$CB82`). Relatório completo em [docs/frame_data/fight_probe_summary.md](frame_data/fight_probe_summary.md) e decisão/trade-offs sobre manter `gvdp_read_hv_counter` (`genesis_vdp.c:467`) intocado em [docs/phase-3.md](phase-3.md).

### 9.3 Renderer `GameVideo`, Widescreen 16:9 e Modo C (`M4`–`M6`) — `docs/widescreen.md`

- `game/ec_video.c` implementa Modo A padrão (`320x224`) e `--widescreen` opt-in estendendo Planos A/B com `GVDP` VRAM, `Vdp_Reg11` per-line H-scroll (`$8B03`), `VSRAM` V-scroll e CRAM nos estados de arena e treino (`14`, `15` e `17..27`), mantendo pillarbox preto puro fora de arena.
- Medição completa em `--widescreen 16:9` (`398x224`) nas 12 capturas de arena (`S14`, `S17`, `S18`–`S27`) + Title (`S7` pillarbox): margens ativas preenchidas em todas (`6.074`–`8.619` de `8.736` px por lado) e **`0/65536` bytes divergentes na WRAM** frente a `320x224`.

### 9.4 Extrator, Pipeline ComfyUI HD (`M7`) e Destilação Neural B+ (`N1`–`N2`)

- **Suíte completa `01`..`13` (`tools/harness/gen_roster_movies.py` + `harness/ec_harness.py`):** cobre todos os **9 lutadores jogáveis** (Shadow, RAX, Blade, Jetta, Slash, Trident, Xavier, Midknight, Larcen), **Eternal Champion** (Estado `18`, opt-in `EC_DIAG_BOSS_SANCTUARY=1`) e todas as **11 arenas + Holo-Trainer** (`Estados 14, 17, 18..27`) com `det=0/65536`, `png_eq=True`, `(0,0,0,0)` misses em todos os 13 filmes e acordo de estado/lutadores vs `gpgx_runner` em `01`..`12`.
- **Extrator (`tools/extract/extract_assets.py`):** catálogo deduplicado por SHA-256 em `assets/catalog/` com **`6.135` entradas** (`410` sprites, `5.698` tiles, `27` planos) e mapas de índice em `assets/catalog/indices/`.
- **Pipeline ComfyUI HD (`pipeline/workflows/`, `tools/pipeline/comfy_upscale.py`, `tools/pipeline/review_viewer.py`):** workflows versionados (`upscale_4x_pixelart.json` via `4x_IllustrationJaNai_V1_DAT2_190k.pth` e `remaster_sdxl_consistent.json` via `90sCartoonXL_v09Alpha.SDXL.safetensors` + ControlNet Canny + IP-Adapter Plus ViT-H) executados contra ComfyUI `v0.45.4` (`RTX 5060 Ti`): **`437/437` assets HD 4× gerados e APROVADOS (`M7.1`–`M7.2`)** — **`27/27` planos** + **`410/410` sprites** (`100%` dos planos e sprites catalogados, `min_iou=0,972973`, **`approved_count=437`**, `generated_count=0`, `rejected_count=0`; os `5.698` tiles `8×8` ficam em `extracted` sem upscale isolado para evitar costuras nas bordas). Galeria atualizada em `assets/review/index.html` com suporte a curadoria interativa + player em loop via `tools/pipeline/review_viewer.py --serve`.
- **Destilação B+ (`neural/train_espcn.py` — `N1`), Export ONNX (`neural/export_onnx.py` — `N2`) & Retreino nos `437` pares aprovados (`M7.2`):**
  - `ECStudentESPCN4x` (`82.344` parâmetros): inicialmente treinado no bootstrap de `107` pares (`PSNR=23,273 dB`, `SSIM=0,8869`, `AlphaIoU=0,9996`), e **retreinado em `60` épocas × `48` steps sobre todos os `437` pares aprovados** (`371` treino / `66` validação = `80` crops): **`PSNR=22,45 dB`**, **`SSIM=0,8805`**, **`AlphaIoU=0,9971`**, checkpoint em `assets/models/ec_bplus_espcn4x.pt` e latência PyTorch CUDA `320×224 → 1280×896` **`fp16 p50=1,1871 ms / p99=1,4109 ms`** (dentro da meta `≤ 2,0 ms` de `§11.4`; `fp32 p50=2,097 ms`).
  - Export ONNX (`N2`) reexecutado sobre o modelo final de `437` pares via [neural/export_onnx.py](neural/export_onnx.py): `assets/models/ec_bplus_espcn4x.onnx` (`opset 17`, `332.523` B, `18` nós, arquivo único sem `.onnx.data`, `sha256=61a6d163c868...`) + pack `ECBP` v1 em `assets/models/ec_bplus_espcn4x_weights.bin` (`329.408` B, `crc32=0x6c2bd111`, round-trip `max_diff=0.0`). Paridade PyTorch FP32 ↔ ONNX Runtime validada com `det_max_err=0.0` em `320×224` (`max_abs_err=7.15e-07`, `PSNR=144,266 dB`), widescreen `398×224 → 1592×896` (`max_abs_err=7.75e-07`, `PSNR=144,247 dB`) e captura real `assets/captures/fight_320.png` → `assets/captures/fight_bplus_onnx_1280.png` (`max_abs_err=5.07e-07`, `PSNR=149,215 dB`, `SSIM=1.0`; vs PyTorch CUDA FP16: `max_abs_err=8.80e-04`, `PSNR=77,242 dB`). Metadados gravados em `pipeline/manifest.json` preservando `approved_count=437`.
  - Diagnóstico de backends e integração `P3` (`N3` — [neural/infer/ec_bplus_gl.h](../neural/infer/ec_bplus_gl.h), [neural/infer/ec_bplus_gl.c](../neural/infer/ec_bplus_gl.c), [game/ec_video.c](../game/ec_video.c), [game/ec_spec.c](../game/ec_spec.c), [segagenesisrecomp/runner/game_video.h](../segagenesisrecomp/runner/game_video.h), [segagenesisrecomp/runner/main.c](../segagenesisrecomp/runner/main.c)): `onnxruntime-gpu 1.32.0` do `.venv` exige CUDA `13.*` (`libcublasLt.so.13`/`libcudart.so.13`, incompatível com CUDA `12.6`/`12.8` do ambiente, sem `onnxruntime_c_api.h`). Optou-se pelo backend sem dependências (`§11.5`) via **OpenGL 4.3+ Compute Shaders + PixelShuffle 4×** carregando `ec_bplus_espcn4x_weights.bin` (`EC_NEURAL_BPLUS=1` / `--neural-bplus`, opt-in). Revalidado com o modelo final de `437` pares na **`RTX 5060 Ti`** (`OpenGL 4.6`, `GALLIUM_DRIVER=d3d12`):
    - **Paridade GPU OpenGL vs ONNX:** `320×224 → 1280×896` (quadro `600`): `max_abs=1/255`, **`PSNR=78,522 dB`**, **`SSIM=0,999999`**; widescreen `16:9` `398×224 → 1592×896` (Arena S17 quadro `2400`): `max_abs=1/255`, **`PSNR=62,523 dB`**, **`SSIM=0,999881`**.
    - **Neutralidade WRAM:** `0/65536` bytes divergentes entre B+ ON e OFF; fallbacks headless (`--benchmark`), guarda `llvmpipe` e orçamento (`--bplus-budget-ms 0.001`: `10` quadros + `15` fallbacks) aprovados com `exit 0`.
    - **Latência WSL2 Mesa D3D12:** async `GL_TIME_ELAPSED` `320×224` `p50=5,6767 ms / p99=9,7828 ms` (`sync=1`: `p50=7,0952 ms`, widescreen `p50=8,1396 ms`; referência PyTorch CUDA FP16 nativa `p50=1,1871 ms / p99=1,4109 ms` — medição OpenGL `≤ 2 ms` a revalidar em Windows nativo conforme `ROADMAP.md §11.5`). Ver detalhes em [docs/phase-4.md](phase-4.md).
- **Teste headless de `EC_HD_MANIFEST=pipeline/manifest.json` e `--hd-manifest pipeline/manifest.json` (`--benchmark 120`):** carga confirmada com **`6135 entradas (437 approved, fallback=Modo A/B)`**, exit `0` em ambos.

### 9.5 Backend CUDA/ONNX Runtime do Modo B+ (`P3` alternativo, 2026-11-20)

- **Habilitação do CUDA 13 no WSL2 (sem sudo):** `onnxruntime-gpu 1.32.0` exige CUDA `13.*`; o ambiente tinha `12.6`. Resolvido com wheels pip **dentro do `.venv`** — `nvidia-cuda-runtime 13.4.92` (fixado: o driver `616.56` suporta até CUDA `13.4` UMD; `13.5.37` segfaulta), `nvidia-cublas 13.8.2.12`, `nvidia-curand 10.4.5.25`, `nvidia-cudnn-cu13 9.28.1.4`. **Correção de segfault no WSL2:** o `libnvidia-ptxjitcompiler.so.1` do Ubuntu (família `580.178.04`) é incompatível com o driver Windows `616.56` — `tools/scripts/cuda-env.sh` pré-carrega a cópia do próprio driver (`/usr/lib/wsl/drivers/nvmdi.inf_amd64_*/`) antes das libs dos wheels, no `LD_LIBRARY_PATH`.
- **Backend `neural/infer/ec_bplus_ort.{h,c}`:** ONNX Runtime via `dlopen` (sem dependência de link; headers C API v1.32 vendored em `neural/infer/ort/`), CUDAExecutionProvider, suporte a modelos fp32 e fp16 (auto-detectado pelo tipo do input). Seleção por `--bplus-backend auto|cuda|gl` (ou `EC_BPLUS_BACKEND`): `auto` tenta CUDA e cai pro OpenGL (`ec_bplus_gl`) e depois Modo A/B; `cuda` sem ambiente CUDA falha limpo com mensagem (sem crash). Como o grafo ONNX exporta a imagem final (base+residual compostos na rede), o backend CUDA honra `intensity == 1.0` (default); intensidade ≠ 1.0 é roteada ao GL.
- **Modelo fp16:** `assets/models/ec_bplus_espcn4x_fp16.onnx` (onnxconverter-common, IO fp16) com paridade `max_abs_err=1,29e-3` vs fp32 na GPU; preferido automaticamente quando presente. Download half→u8 via lookup table de 64 KB.
- **Resultados in-game (600 quadros, 0 fallbacks, WSL2):** `320×224 → 1280×896` **`p50=8,85 ms / p99=14,76 ms`** wall-clock (inclui round-trip CPU→GPU inerente ao WSL2; referências: GL async `p50=5,68 ms`, PyTorch CUDA fp16 `p50=1,19 ms`). Métricas impressas na saída (`[EC_VIDEO] B+ CUDA: N frames, ... p50/p99/mean`). Latências a revalidar em Windows nativo conforme `ROADMAP.md §11.5`.

### 9.6 Remaster generativo Flux dos 437 assets (`M7.4`, 2026-11-20) — CONCLUÍDO

- **Workflow `pipeline/workflows/remaster_flux_usdu.json`** (Flux.1 Dev fp8 + Ultimate SD Upscale: pré-upscale GAN `4x-UltraSharp` → difusão tiled 1024 com `denoise=0.30`, `steps=20`, `seam_fix=Half Tile` + imposição de silhueta pelo pipeline). Node `ComfyUI_UltimateSDUpscale` instalado no ComfyUI 0.45.4 (Windows, alcançável do WSL via `127.0.0.1:8188`).
- **Pilotos aprovados**: 6 arenas + 6 sprites comparados em 4 vias (original × pixel-art × 90sCartoonXL × Flux) — Flux vencedor (`assets/review/piloto_comparacao_4vias.png` e `piloto_sprites_4vias.png`); silhueta preservada (`min_iou ≥ 0,995`).
- **Rodada completa: `437/437` assets (`410` sprites + `27` planos) remasterizados em ~5h40** na RTX 5060 Ti, `min_iou=0,9730`, zero falhas, status `approved` → **todos os assets Flux estão vivos no Modo C**. Os `5.698` tiles `8×8` seguem `extracted` sem upscale isolado (decisão M7.1 mantida — evitar costuras).
- Kit portátil do workflow em `~/EternalChampions_Flux_Remaster_kit/` (workflow + README com receita completa + folhas de comparação).

## 10. Estado de Pendências e Bloqueios Reais

1. **BlastEm (`1.0.0`, `/usr/local/bin/blastem`) — Arbitragem `M1` CONCLUÍDA**: controlado via PTY debugger (`-g -d`), detecta `6-button gamepad` nos dois ports (`$9376=$0101`) e concorda em 100% dos cenários (`A`, `B1`, `B2`, `B3`, `C1`, `C2`) com `GPGX`, `ares` e `Native`. O `gpgx_runner` segue como referência automatizada em lote do harness.
2. **Paridade determinística `M3` CONCLUÍDA (critério de 2 camadas)**: **Camada 1** (`Recomp` × `GENESIS_FORCE_INTERP=1`): `16/16` (`01`..`12`, `14`..`17`) com `gameplay_match=True`, `png_eq=True`, `pad=0x0101` e **`65.536/65.536` B idênticos** com o jogo `100%` recompilado (o resultado anterior, `65.534/65.536` em `12/12`, contava com partes do jogo interpretadas pelo floor — ver item 5). **Camada 2** (`Native` × `GPGX`): `13/13` autodeterminismo `0/65536` + `0` misses e `12/12` acordo em `$FD64`/`pad`/elenco P1-P2 (diff residual `640`–`2.216` B por leitura ao vivo do contador HV em `$CB82`); `gvdp_read_hv_counter` (`genesis_vdp.c:467`) mantido intocado por decisão documentada em [docs/phase-3.md](phase-3.md).
3. **Export ONNX (`N2`) e Runtime B+ `P3` (`N3`) — CONCLUÍDOS**: `ec_bplus_espcn4x.onnx` (`332.523` B, `opset 17`) e `ec_bplus_espcn4x_weights.bin` (`329.408` B, `ECBP` v1) validados via `neural/export_onnx.py` (`det=0.0`, `PSNR≥144,48 dB`), e backend OpenGL 4.3+ Compute Shaders integrado em `neural/infer/ec_bplus_gl.{h,c}` + `game/ec_video.c` + `segagenesisrecomp/runner/{game_video.h,main.c}` (`PSNR=78,603 dB` / `SSIM=0,999998` em `1280×896`, `PSNR=62,973 dB` em widescreen `1592×896`, `0/65536` WRAM diff, `p50=5,3423 ms` no WSL2 Mesa D3D12 — revalidar `≤ 2 ms` OpenGL em Windows nativo conforme `ROADMAP.md §11.5`). **Backend alternativo CUDA/ONNX Runtime adicionado em 2026-11-20** (`--bplus-backend auto|cuda|gl`, ver §9.5).
4. **Curadoria HD (`M7.2`) e modos extras (`M7.3`) — CONCLUÍDOS**: todos os **`437/437` assets (`27/27` planos + `410/410` sprites, `min_iou=0,972973`)** foram revisados/aprovados (`approved_count=437`, carga `EC_HD_MANIFEST` e retreino `ECStudentESPCN4x` + re-export `ONNX`/`ECBP` `crc32=0x6c2bd111` validados; `5.698` tiles `8×8` mantidos em `extracted` sem upscale isolado). `M7.3`: `.ecm` ganhou 2ª coluna opcional (pad 2) em `gpgx_runner`/`ec_harness.py`/`gen_roster_movies.py` e a suíte passou a `17` movies — `14_2p_vs_rax_vs_blade_s24` (2P VS, Background fixo via Options), `15_tournament_roundabout_jetta_vs_xavier_s27` (Round-A-Bout, `Tournament_Type=$9424=3`; arena sorteada pelo RNG HV, no GPGX basta estado de luta) e `16_2p_vs_match_end_s24_to_s1` (KO no round final → `WINS COMPETITION` → Estado `1`). Overkill mapeado (`$5E4B6` → `$269E4` → handler por arena, flag `Fight_OverkillActive` `$9C7C`) e confirmado pelo movie `17_2p_vs_overkill_trident_s24` (Estado `24`, `$9C7C=1`, HP `104/0` em nativo, interpretador e GPGX); ver [docs/phase-5.md](phase-5.md).
5. **Correção da recompilação (2026-11-19, descoberta no `M7.3`)**: o nativo **travava em todo fim de partida** (Estado `21`/`24` congelado após `WINS COMPETITION`; GPGX e interpretador voltam ao Estado `1`). Causas e correções: (a) `19` tabelas `jmp (pc,Dn.w)` + `bra.w` sem cobertura (ex.: slot 8 `$024CEC` de `Sub_024C98`) → declaradas em `BRA_JUMP_TABLES`; (b) heurística ASCII de `is_valid_code_start` descartava slots `bra` e alvos de task reais (`$E8AE`, `$C5D0`, `$05E2FE`) → slots de tabela e ponteiros de `jsr` a APIs de task isentos; (c) callbacks `$36(a0)` de objetos (`$2479E`…), ponteiros em `$968E` (`$3533E`/`$3547C`) e listas `bra.w` dos descritores de menu (`$2DA54`, `$2D918`, `$2DBE8`…) viraram sementes; (d) o gerador cobrava os ciclos de `jsr`/`bsr` **após** o retorno do callee (interpretador e 68000 cobram antes) → `segagenesisrecomp/external/m68k-recomp-core/profiles/genesis/code_generator.c` agora cobra antes da chamada. **Achado importante:** a antiga "Camada 1 `12/12`" era em parte artefato — a task `$24C98` do attract entrava pelo floor Tier-3 e o laço principal, escalonador, IRQ e menus rodavam **interpretados** (`4.111` PCs, `1.062` fps). Agora o jogo roda `100%` recompilado (`1.449` fps, `--exec-coverage-out` vazio) com `0/65536` B contra `GENESIS_FORCE_INTERP=1` em `16/16` movies e `0` misses em `17/17`. O Duff's device `$05E242` virou switch interno (`INTERIOR_UNRESOLVED = 0`), mas não é exercitado pela suíte; resíduo de temporização caracterizado em `f1143` (sem efeito em WRAM) documentado em [docs/phase-3.md](phase-3.md) §5.
6. **Expansão Sega CD (`M8`) — ADIADA por decisão de escopo**: `roms/` já contém `Eternal Champions - Challenge from the Dark Side (U).chd` e o pack completo de BIOS Sega CD (US/EU/JP, incluindo `us_scd1_9210.bin`, `eu_mcd1_9210.bin` e `jp_mcd1_9112.bin`); retomada apenas após concluir os marcos do cartucho Genesis (`M4`–`M7`, `N2`–`N3`).

### 10.5 Captura e remaster de áudio (2026-11-20, preparação)

- **Captura do fluxo FM/PSG (`GEN_DEV_TRACE`):** opção CMake `EC_GEN_DEV_TRACE` em `game/CMakeLists.txt` (default OFF); com ela, `F12` (ou `--snd-dump-frame N`) grava `chip_ring.txt` (fluxo de registradores YM2612/SN76489 com timestamps `wf`/scanline) + `snd_ring.txt` (ciclo de comandos 68k↔Z80) + `z80_ram.bin`. Anéis ampliados para `1<<21` (chip, ~2 min) e `1<<19` (snd) em `chip_trace.c`/`genesis_machine.c` — um `F12` captura logo → título → seleção → luta numa sessão.
- **Conversor `tools/audio/chip_ring_to_vgm.py`:** chip_ring → VGM 1.51 com offsets relativos corretos e clock do YM2612 em `0x2C` (sem ele, players como o VGMPlay ignoram o FM e dessincronizam — corrigido empiricamente contra o código-fonte do VGMPlay legado). Recorte por faixa via `--start-wf`/`--end-wf`.
- **Validadas de ouvido (3 faixas, VGMPlay):** `musica_titulo.vgm` (16,7 s), `musica_selecao.vgm` (40,4 s), `musica_luta.vgm` (23,8 s) — capturadas da ROM do usuário (mesmo princípio dos assets HD: tudo derivado da ROM própria, sem material de terceiros). Referência auditiva externa apenas por streaming (Spotify), sem extração (DRM/licença).
- **Próximos passos do áudio:** render HQ (WAV via VGMPlay) para A/B contra o som em tempo real; cadeia de FX in-game (stereo widen/EQ/reverb leve) no drain; e VGM → MIDI (VGMTrans) → Dexed para o remaster criativo.

## 11. Ciclo 2026-11-21: Save/Load V2, Cold Reset, EC jogável, cenários off-label e fim de partida

> Fila de bugs e pendências: [docs/BUG_QUEUE.md](BUG_QUEUE.md).
> Princípio mantido: jogo base 100% autêntico; todos os desvios são
> opt-in via `--unlock-boss` (ou `EC_UNLOCK_BOSS=1`).

### 11.1 Save/load V2 (`ECRB2sav`) e reset frio (F10)

- **Problema:** o formato antigo de save (seções exec/input) não era
  portável e o load não restaurava o estado por completo (input fantasma
  após carregar).
- **Solução — formato V2 `ECRB2sav`** (`segagenesisrecomp/runner/main.c`):
  magic própria de 8 bytes + `size_t` + **blob portátil do rollback**
  (`genesis_rb_save_portable`), a mesma maquinaria validada do rewind, mas
  portável entre processos. Sem seções exec (que carregam o PC do guest) e
  input.
- **Load in-place:** o blob é restaurado no estado corrente sem
  reinicializar a máquina; A7 preservado; re-entrada pela
  `dispatch_main_loop_pc = 0x255E` (`Main_OuterLoop`). Estado de input é
  rearmado para eliminar o **input fantasma** (bug resolvido).
- **F10 = cold reset** (`runner_cold_reset`): RAM zerada + `machine_init` +
  SRAM de bateria preservado + boot do vetor de reset — comportamento de
  console desligar/ligar. Diretiva `COLD_RESET` disponível no input-script
  (testes headless reproduzem o F10).

### 11.2 Cenários off-label no VS 2P (Holo-Trainer + Santuário) — CONCLUÍDO

Ver [docs/offlabel_arenas.md](offlabel_arenas.md). Resumo do estado final:

- **Lista de nomes off-label realocada para ROM livre em `$257051`**
  (241 bytes zerados, maior janela de zeros do cartucho): 12 ponteiros +
  strings `"HOLO-TRN"` em `$257081` e `"SANCTUARY"` em `$25708A`. As
  definições LONG do menu (`$2E8B0`/`$2EC7E`) foram repontadas para lá.
  **Por quê:** a primeira versão usava RAM `$FFCF3C–$FFCF7E`, que o jogo
  usa como records de sprites durante a intro/luta — sentinel confirmou o
  conflito, que causava o **bug global de sprites** (intro + cenários) e o
  **hang do valor 10** (a 10ª entrada caía sobre a string "Random", bytes
  lidos como ponteiro inválido). Ambos resolvidos com a realocação.
- **Clamp do Background:** `9 → 11` nas defs `$2E8AC`/`$2EC7A`
  (`EC_OFFLABEL_TEST` bit 1; lista realocada = bit 4; default 5).
- **Commit da seleção (`$2E42E`, hook return 1 — site é entrada via bsr):**
  `$936F=10 → estado 14` (Holo-Trainer) e `$936F=11 → estado 18`
  (câmara/santuário do EC). Verificado headless: navegação sem travar,
  ponteiros válidos lidos da ROM, commit dispara e a luta roda viva
  (~1000 objetos ativos nos estados 14/18).
- **Validação visual (por pixel/VRAM):** Holo-Trainer off-label com
  **96–98% de overlap** de pixels com o Training Mode original (estado 14).
  Santuário (estado 18) confirmado pelo usuário ("arena certinha").

### 11.3 Fim de partida nos estados off-label — CONCLUÍDO

- **Sintoma:** vencer no VS off-label (santuário) jogava para a
  finalização/endgame em vez do menu — e uma primeira tentativa de fix
  travava o cenário.
- **Fluxo original do estado 18** (`$59B6–$59F0`): `$9350==1 → $59E0` grava
  `$9CB6=8` (finalização); `$9352!=0 → $59E8` grava `$9CB6=10`; também
  `$59D0→18` e `$59D8→4`. O unwind segue: `$59E0` → `$24EC` (mesmo frame) →
  ~17 frames → `$FE74` (State_SetState).
- **Armadilhas encontradas:**
  1. Hook com `return 1` nos writes `$59E0`/`$59E8` **trava**: são pontos de
     fall-through; `return 1` abandona a função nativa e quebra o unwind.
  2. Patch direto na ROM não funciona em **imediatos de instrução** — o
     código gerado inlinea o imediato como constante C.
- **Solução final:** hooks nos **RTS `$59E6`/`$59EE`** (logo após os
  writes) com `return 0`: sobrescrevem `$9CB6=1` (menu) quando
  `$936F==11` e deixam o fluxo/unwind intactos. `game/game.toml` exige
  `cmake --build build/native --target genesisrecomp_generate_ec` após
  editar.
- **Validação:** headless `ko_sanc` — `[OFFLABEL] fim de partida: estado 18
  → menu` e dumps mostram `f5100: state=0001` (antes: estado 8/10 ou
  travado). Usuário confirmou: "fechou o cenário do chefão".
- **Off-label Holo-Trainer (bg 10 → estado 14):** já retornava ao menu
  (`ko_holo`: estado 14 → 1). **Fica na fila:** vencer no Training Mode
  **original** pode levar à finalização (ver `BUG_QUEUE.md`).

### 11.4 EC jogável e sprites ilimitados

- Hooks do EC jogável concentrados em `game/ec_boss.c`/`ec_boss.h`
  (`ec_boss_instruction_hook` + `ec_boss_vblank` + `ec_boss_apply_rom_patches`),
  registrados em `game/game.toml` (26 sites `game_hook`).
- **Sprites ilimitados** ligados ao `--unlock-boss`;
  `EC_LIMIT_SPRITES=1` restaura o limite original.
- **Bug restante na fila:** sprites do EC falham nos DOIS ECs só no
  round 1 no Trident (pista: carregamento/timing).
- **Troca de forma na luta (2026-11-25):** com o EC jogável, `MODE` + `X`
  (Tubarão) / `Y` (Dragão) / `B` (Tigre) / `A` (Fênix) troca a forma no meio da
  luta; o botão da forma atual volta à forma neutra (Base). Reaproveita as partes síncronas da transformação original do chefe
  (`fight_moves1.asm` `$451CE..$453E0`): blob em `($108)` + paleta `$172AE`,
  ciclos de brilho `$D782`, cadeia de golpes preenchida em lugar `$1822A`,
  tabela `$5E180`/`$44160`, IA da forma e objeto `$36`; som original `$1D` e
  clarão só na paleta do lutador (o clarão do cenário do Santuário, `$1DFA6`,
  fica de fora). Pedido lido do controle selado (`genesis_sim_pad`) em
  `ec_boss_vblank` e aplicado no laço principal do EC (`$044274`, após o
  `jsr $DB14` que marca a retomada da tarefa), ou seja, só em neutro. Exige
  controle de 6 botões (`pad_type = 1`). Diagnóstico: `EC_FORM_TEST`, `EC_FORM_LOG`.

### 11.5 Arquitetura da máquina de estados mapeada (neste ciclo)

- `$2230` = `Main_Entry` (soft restart) · boot loop `$22E6–$231E` (consome
  `$9CB6`) · `$246E`→`$24E0`→`$FE74` = único chamador de `State_SetState`
  (`$FE74`: `d0 → $FD64` + init + dispatch) · `$255E` = `Main_OuterLoop` ·
  `State_Dispatch` `$FE8C` · exit do estado 18 em `$59B6–$59F0` ·
  match-end em `$1EE40+` (`$9350++` em `$1EFD8`) · `$9CCC`=1 = VS 2P ·
  `$9CB0` = 0/2/4 = 1P/VS/training · `$9CB6` = "próximo estado pedido".

### 11.6 Limpeza e metodologia

- **Limpeza:** removidos RENDER-DBG (`$31A34`/`$31A40`), arena-checks
  mortos (`$47958`/`$47960`/`$47B92`/`$47B9A`), `EC_OFFLABEL_TRACE`, trap
  NULL-READ do runner e hooks de debug (`$2306`/`$FE74`/`$24EC`/`$2230`).
- **Metodologia:** harness determinístico (movies `.ecm` → input-scripts,
  `gpgx_runner`, `verify.py`) + análise de screenshots por pixel/VRAM
  (modelo sem visão de imagem) + debug server TCP 4378
  (`get_registers`/`get_frame`/`frame_info`/`read_memory`, frame ring de
  600). Movies de seleção `v3_{holo,sanc}.input` e de KO
  `ko_{holo,sanc}.input` com dumps de RAM a cada 100 frames em
  `/tmp/opencode/`.

## 12. Ciclo 2026-11-21 (2ª rodada): fluxo do chefe (T1 ✅), training preto (T2) e sprites do Trident (T4)

> Tasklist: [docs/proposta_bugfix_2026-11-21.md](proposta_bugfix_2026-11-21.md) (T1–T4). Fila: [docs/BUG_QUEUE.md](BUG_QUEUE.md).

### 12.1 T1 — Cenário do EC voltava ao menu após o 1º round — RESOLVIDO

- **Causa raiz (verificada em dumps):** o jogo grava `$936F=11` durante a
  luta do santuário (índice interno da arena); o guard `$936F==11` dos
  hooks de `$59E6`/`$59EE` (fix do fim de partida off-label do ciclo
  anterior) não distinguia a luta do chefe e sobrescrevia `$9CB6=8`
  (estado 8 = tela de arte/transformação) com `$9CB6=1` (menu) no 1º KO.
  Repro determinística: VS 2P santuário com EC → KO → `$9350: 0→1`,
  `FD64: 18→1` (antes: deveria ser `18→8`).
- **Fix (`game/ec_boss.c` + `game/game.toml`):** guard `$9CBC!=9 &&
  $9CBE!=9` nos hooks `$59E6`/`$59EE` (hijack restrito ao VS off-label sem
  EC). Fluxo natural do boss validado: `18-KO → 8 (arte) → [START] → 6 →
  4 → 10 (ending) → reset`. Hooks novos em `$321A`/`$3662` (antes do
  `btst.b #7,$FD61` dos wait loops dos estados 8/10) mapeiam START dos
  pads de luta (`$FD59`/`$FD5B`) para bit7 de `$FD61` — em VS 2P a cadeia
  de input de menu não alimenta esse flag.
- **Validação:** cadeia completa headless com input real; kosanc (sem EC)
  segue → menu; suíte `17/17` verde (`det=0/65536`, `interp_diff=0/65536`,
  `png_eq=True`).

### 12.2 T2 — Training (Holo off-label) termina em tela preta — ✅ RESOLVIDO (2026-11-21)

- **Causa raiz:** padrão artesanal link/unlk do ring buffer do Z80
  (`Z80_RingRead` `$07E4`: `movea.l (a7)+,a0` + `link a4` + re-push +
  `rts`; pops do epílogo na continuação tail-call do caller `$0810`). O
  `rts` gerado não desempilhava e o wrapper do JSR não compensava quando
  `SP == slot+4` → **vazamento de 4 bytes de pilha por comando do ring**.
  No burst de comandos da transição KO→menu (wf 5237), a pilha desce até
  as slots salvas do frame loop → `movem` restaura **A0/A6 = 0** → o
  VBlank escreve a tabela de DMA em `$000004` (não `$C00004`) → o DMA de
  CRAM (shadow `$F0B6` → CRAM) morre → CRAM preta → tela preta.
- **Fix:** `m68k-recomp-core` `2666e9f` (rts hardware-fiel +
  wrapper compensa só com o slot intacto); regenerado + suíte 17/17
  verde. Validação: CRAM 33/64 com a paleta do menu, A6=C00000,
  screenshots 24 cores. Movie 08 interp residual 4→55 bytes (mesma
  classe; `gameplay_match=True`). Instrumentação: `GENESIS_STRICT_JSR_STACK`
  (mismatch em cascata wf 5237), trace de SP do ring, bisect de A6 via
  `owner_review step`, trace env-gated `GENESIS_VDP_TRACE=FROM-TO`.

### 12.3 T4 — Sprites do EC no round 1 do Trident — REPRO EM CONSTRUÇÃO

- Condições do usuário confirmadas: todas as formas (base+1–4), mirror
  EC×EC, pior em movimentos aéreos, independente de HD.
- Infra `tools/scripts/t4_repro/` (t4_gen/t4_cursor): poke de `$9CBE`/`$9CBC`
  descartado (menu re-deriva os ids via `$CB82`/`$9367` e trava na loading);
  mapeamento do cursor do select com `--unlock-boss` iniciado (UP4 cicla a
  Rax; roster patchado clamp 9→11 ainda por decodificar).

### 12.4 T3 — 1º load do savegame — STAGING INTERATIVO IMPLEMENTADO (2026-11-21)

- Repro headless: `t3_save.input` grava na luta (momento do KO); sessão
  nova com `t3_load.input` faz o 1º load a frio e o 2º load, com dumps a
  +60/+180 frames de cada.
- Resultado (scripted): o jogo avança após ambos os loads; o estado a
  +60 frames difere em **73 bytes** entre o 1º e o 2º load (classe
  RNG/HV documentada §3.1 — o fix do rts do T2 deslocou o drift; antes
  era 0 por coincidência de timing). Nada disso é o hang.
- **Direção do usuário aplicada:** os loads interativos (hold do pad e
  modal F7/F8) agora são **staged** e aplicados no ponto seguro pós-frame
  (junto ao tick do script) — "pausar o sistema todo antes de aplicar o
  load". O swap nunca mais acontece no meio do loop com o fiber em um
  yield site arbitrário. `runner_stage_load_state` +
  `s_pending_load_path` em `main.c`; modal via `local_states.c`.
- Pendente: validação interativa com o usuário. Plano B documentado:
  endurecer `glue_prepare_inplace_load` para exigir
  `s_yield_site == GLUE_YIELD_VBLANK` (senão, cold-restart).
