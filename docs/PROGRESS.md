# PROGRESS.md — Estado do projeto EternalRecomp

> Documento vivo do progresso do projeto (complementa `DESIGN.md` e
> `ROADMAP.md`). Atualizado em 2026-11-19.

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
| `disasm/symbols/labels.toml` | 102 labels (76 confirmed, 28 hypothesis) |
| `tools/vendor/vasm/` | vasm 1.9d compilado localmente |
| `tools/vendor/ares_cli/` | Driver headless do ares (input/trace/dump) |
| `tools/vendor/gpgx_runner/` | Loader libretro do GPGX + filmes `.ecm` |
| REA (MCP) | Ghidra 12.2 como provider; 267 pseudocódigos exportados em `reference/rea/` |

## 3. Estado da conversão (Fase 1)

Regiões **convertidas para asm e hash-verificadas** (~7,6 KB de código):

| Região | Arquivo | Conteúdo |
|---|---|---|
| `$000200–$0002F9` | `code/boot.asm` | Reset, detecção TMSS ("SEGA"), registros VDP, stub do Z80 |
| `$0002FA–$0006B5` | `code/boot2.asm` | Trava de região (letra em `$1F0`), tela de copyright, fonte 1bpp |
| `$0006B6–$00083B` | `code/irq.asm` | VBlank (IRQ6), IRQ2, handshake de quadro com o Z80 (`0xA01A60/61`), lock do barramento (`$A11100`), upload do driver Z80, ring buffer |
| `$00083C–$0008A7` | `code/z80_cmd.asm` | Interface de comandos para o Z80 |
| `$0008A8–$000A7F` | `code/sound_cmd.asm` | Camada de comandos de som |
| `$002230–$0025FD` | `code/main_entry.asm` | Entrada do main, clear de RAM, modos de boot (07→09→01→FF), laço externo |
| `$002786–$0027B3` | `code/vdp_init.asm` | Sombra de registros VDP |
| `$0027B4–$002803` | `code/state_table.asm` | Tabela de dispatch: 40 handlers (offsets relativos) |
| `$002804–$002ADC` | `code/state_handlers1.asm` + `code/state1_handler2.asm` | Estados 0 (nulo) e 1 (init completo) |
| `$002ADC–$00313A` | `code/state_handlers2.asm` | Estados 2–7 (seleção de personagem, título) |
| `$00313A–$00397F` | `code/state_handlers3.asm` | Estados 8–13 (menu-init, menu principal) |
| `$00FD24–$00FE1D` | `code/main_frameloop.asm` | Laço de 1 quadro: contador `$FD70`, leitura de pads, subsistemas, sync com o Z80 |
| `$00FE1E–$00FFAB` | `code/state_dispatch.asm` | FrameRun, State_SetState, State_Dispatch, State_PostChange, init do sistema/câmera |

Regiões **ainda não convertidas** (incbin):

| Região | Tamanho | O que é |
|---|---|---|
| `$000A80–$00222F` | 0x17B0 | Programa do Z80 (driver de som, 0x1886 bytes) + dados — por decisão, fica como bloco |
| `$002600–$002785` | 0x186 | Sub-rotinas do laço externo (Sub_2600/2686/2718) |
| `$003980–$00FD23` | 0xC3A4 (~50 KB) | **Handlers 14–39 + cluster de utilitários** (input, alocador, escalonador, VDP) |
| `$00FFAC–fim` | ~3 MB | Tarefas do escalonador, dados, gráficos, áudio |

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
| 1 | `$2806` | init completo | 14–17 | `$3980`… | não convertidos |
| 2 | `$2ADC` | seleção de personagem | 18 | `$56F0` | não convertido |
| 3 | `$2E24` | vazio (rts) | 19–31 | `$5B06`… | não convertidos |
| 4 | `$2E26` | alias do 10 | 32–39 | `$C08C`… | não convertidos |
| 5 | `$2E2A` | init tilemap/câmera | | | |
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

## 6. Investigação da divergência ares × GPGX

Resumo da investigação (docs em `docs/main_loop.md`):

1. ⚠️ **Bug nosso corrigido**: a 1ª versão do runner usava
   `JOYPAD_START=7` (= RIGHT no libretro) — comparações iniciais
   inválidas. Com START real (id 3), refeito tudo.
2. **Sem input**: máquina de estados **idêntica** nos dois emuladores
   (defasagem ~44 quadros); 9→13 é timer de attract.
3. **Com 2+ presses de Start** no estado 9: **concordam** (9→1 em 1033
   vs 1034).
4. **Com 1 press único**: ares → 9→1 (983); GPGX perde a borda →
   9→13 (attract).
5. No quadro do pressionar, a RAM é **idêntica** nos dois (FD54=$0080,
   FD58=$0080); a diferença está na **oscilação intra-quadro** do
   contador do FightingPad (ares, instantâneo) vs a estabilidade do
   MK-1653-50 (GPGX, latência de 172 ciclos na troca de TH).

**Decisão**: GPGX = referência dinâmica oficial (o `gpgx_runner` já é o
esqueleto do harness §8). Ares fica para o P2/N2 — ou portar o
`gamepad.c` do GPGX para ele.

**Questão em aberto**: qual emulador bate com o hardware real (o jogo
funciona em console real; referendar com BlastEm/Exodus).

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

1. **Pad real**: ares (instantâneo) ou GPGX (latência de 172 ciclos)
   bate com o MK-1653-50 real no polling rápido do Sub_CA80?
   → BlastEm/Exodus como terceiro árbitro
2. Por que a saída A do estado 9 no ares dispara ~16–33 quadros depois
   do pressionar (borda espúria intra-quadro) — confirmar com tracing
   intra-quadro
3. Semântica completa de `$FD61` bit5/bit6 (attract/reset) e do ciclo
   de attract do Sub_246E
4. Revisão No-Intro da ROM (site bloqueia sem aceitar termos)
5. CI de hash (hook local ou GitHub Actions)

## 9. Próximos passos

1. Converter `$3980–$C0A0`: handlers 14–39 (paralelizável com subagentes)
2. Converter `$C0A0–$FD23`: cluster de utilitários (input/alocador/
   escalonador) — com REA
3. Converter `$2600–$2785` e a região de tarefas (`$FFAC+` — `$16000+`)
4. Settle a questão do pad (BlastEm)
5. Fechar o M1 (saída: disassembly completo + labels) e iniciar a
   Fase 2 com o Genesis Recomp
