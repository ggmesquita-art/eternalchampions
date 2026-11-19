# docs/main_loop.md — Esqueleto do laço principal (Fase 1, esboço)

> Fonte: pseudocódigo exportado pelo REA/Ghidra 12.2 (`reference/rea/decompile-*`).
> Tudo abaixo é **hipótese** até confirmação no debugger (ares), conforme DESIGN §6.3.

## Entry points

- `Main_Entry` (`$2230`): alvo do `jmp` no fim do boot. Seta a pilha (`0x1000000`),
  inicializa a sombra VDP (`$2786`, tabela de 24 regs em (A6)), zera ~27 KB de
  RAM em `$9378`, checa o contador de boots (`$FEF8`), roda a sequência de modos
  da tabela `$22DE` (07→09→01→FF=reinicia) e faz a checagem de save RAM
  (2 slots, portas `$A10003`/`$A10005`).
- **Cadeia de chamadas do laço (confirmada pelo disasm):**

```
Main_Entry ($2230) ──► Sub_246E (modo 9) ──► init final ($24E0)
   │                                          │ (flag $FD61 bit5 = attract)
   ▼                                          ▼
Main_OuterLoop ($255E) ◄─────────────────── Sub_246E_Attract (demo)
   ├─ Main_InitStructs ($23F4): aloca 2 buffers de $12C0 via Mem_Alloc ($D4CC)
   ├─ jsr $DB14 (input) → FUN_0000fcde (roda 1 quadro) → Main_FrameLoop ($FD24)
   └─ dispatch por $FD61/$9CCA/$FD64 → $DAF8 / $E2B0 / $B56C
```

**A6** = ponteiro global usado como sombra de registros/comandos do VDP
(escritas em (A6) e (A6+4) em `$2786`, `$22A0` e no VBlank via `$D6A0`).

## Validação dinâmica (ares_cli, 2026-11-18)

Driver headless do núcleo do ares (`tools/vendor/ares_cli/`): roda N quadros,
despeja a RAM de trabalho, aceita `--start <quadro>` (aperta Start) e
`--trace` (log de estado por quadro). Primeiro dump (600 quadros) confirmou:

| Hipótese | Resultado |
|---|---|
| `$FEF8` contador de boots | = 1 ✓ |
| `$FEFE` = 4 no fim do boot | = 4 ✓ |
| `$FEFC` = $14 modificado no modo 9 | = $74 ($14\|$30\|$40) ✓ |
| `$FD70` contador do laço principal | = 526 em 600 quadros ✓ |
| `$FD72` contador de VBlank (long) | = 552 em 600 quadros ✓ |
| `$FD76` flag de VBlank | = 0 (o jogo zera a cada quadro — é a flag do §7.3!) ✓ |
| `$FD64` estado do jogo | = 7 (tela de título) ✓ |
| `$FD69` flags de subsistemas | = $7F (todos ligados, bit7 off) ✓ |
| `$FD6A` lock do barramento Z80 | = 0 (protocolo balanceado) ✓ |
| `$9CCA` = $FFFF no boot | = $FFFF ✓ |
| `$9CC0` = $B no boot | = $B ✓ |
| `$C838/$C836` câmera | ativos ($12/$00) ✓ |

Estado dos subsistemas em runtime confirmado: bits 0–6 de `$FD69` ligados →
`DC4E`/`F6A0`/`D7E4`/`E684`/`10AA8` rodando. `$9B32` continua 0 após 600
quadros (o sync de quadro do laço só ativa quando vira ≠ 0 — confirmar quando).

### Mapa de estados (GPGX = referência; ares concorda sem input)

| Momento | Estado |
|---|---|
| boot | 0 → **7 (título)** (GPGX 24 / ares 23) |
| sem input | 7 → 0 → **9 (menu-init)** (686/688; ares 642/644) |
| sem input | 9 → **13** por TIMER de attract (1582; ares 1538) → 7 → 13 (ciclo) |
| Start no título | idem (aceita o Start e vai para 9) |
| Start no estado 9 | **1 press único diverge**: GPGX perde (→13 por timer); ares → **1** (983). **2+ presses: concordam** (9→1 em 1033/1034) |

Handlers: `State_HandlerTable[$27B4]` (40 offsets relativos). Estado 7 →
`$2EF2`, estado 9 → `$3256` (menu: `$9CCA==5 → jmp $200` = reset), estado 0 →
`$2804` (`rts`), estado 1 → `$2806` (convertido por completo: loop por quadro,
cria objetos `$B8/$41/$AB`, espera `Start` em `$FD61` bit7, termina em `rts`).
`$FD68` = flag "estado mudou" ($FFFF a cada dispatch).

**RESOLVIDO (comparação com Genesis Plus GX):** construí o loader libretro
mínimo (`tools/vendor/gpgx_runner/`). ⚠️ **Bug corrigido no caminho**: a
primeira versão do runner usava `RETRO_DEVICE_ID_JOYPAD_START=7` (=RIGHT no
libretro.h do GPGX; o certo é 3) — as comparações iniciais eram inválidas.
Com input correto, a **máquina de estados é idêntica nos dois emuladores
SEM input** (ciclo de attract):

| evento | ares (no-input) | GPGX (no-input) |
|---|---|---|
| 0→7 título | 23 | 24 |
| 7→0→9 menu-init | 642/644 | 686/688 |
| 9→13 (timeout de attract!) | 1538 | 1582 |
| 13→7→13 (ciclo) | 1556/1558 | 1599/1601 |

A transição 9→13 é um **timer de attract** (acontece sem input nenhum).
**Divergência com input no estado 9** (handler $3256 convertido e
verificado): o loop de espera ($33FE) tem DOIS caminhos — (a) `$FD55`/`$FD57`
bit7 (bit `$0080` da BORDA do pad) → saída compartilhada **$3492** (limpa
`$9CCA` etc., rts com `$9CB6`=0 → tabela de modos avança → **estado 1**);
(b) `$FD61` bit7 (heartbeat da tarefa de input) → `$9CCA++` → `$9CB6=2/$D`
→ **estado 13**.

Experimentos iniciais (19/nov — frame absoluto sem alinhamento de Estado 9):

| Cenário | ares | GPGX |
|---|---|---|
| sem input | 9→13 (1538) | 9→13 (1582) |
| **1 press** Start@950 | **9→1 (983)** | **9→13 (1582, press perdido)** |
| **2+ presses** (800+900+1000) | **9→1 (1033)** | **9→1 (1034)** ✓✓ |

**Arbitragem fechada com BlastEm 1.0.0 (`-g -d`), GPGX, ares e Native (2026-11-19):**

Medição com pontos alinhados pela entrada no Estado `9` (`$FD64=9`) e pelo fim de `State9_WaitB62` (`$9B62 != 0` @ `$FD70=182`, `vbl=188..191`) **eliminou a aparente divergência**: os **4 backends concordam 100%** em todos os cenários relativos e todos reportam `0x0101` em `$9376` (6 botões em ambos os ports, exceto `0x0001` no `ares_cli` por configurar Control Pad no Port 2).

| Cenário (alinhado ao Estado 9) | BlastEm 1.0.0 | GPGX | ares | Native (`EternalChampionsRecomp`) |
|---|---|---|---|---|
| **A — Sem input** (`9` / `$9B62!=0` / `9→13`) | `695` (`vbl=2`) / `955` (`vbl=191`) / **`13@1595`** (`vbl=827`) | `688` (`vbl=2`) / `948` (`vbl=191`) / **`13@1582`** (`vbl=825`) | `644` (`vbl=1`) / `905` (`vbl=191`) / **`13@1538`** (`vbl=824`) | `856` (`vbl=2`) / `1113` (`vbl=188`) / **`13@1747`** (`vbl=822`) |
| **B1 — 1 press `State9 + 262`** (equiv. GPGX `@950`, dentro de `FCDE(15)`) | press `@957` (`main=185`, `fd54=$80`) → **`9→13`** (`1592`) | press `@950` (`main=185`) → **`9→13`** (`1582`) | press `@906` (`main=184`) → **`9→13`** (`1538`) | press `@1118` (`main=188`, `fd54=$80`) → **`9→13`** (`1747`) |
| **B2 — 1 press `State9 + 306`** (equiv. ares `@950`, em `State9_WaitLoop`) | press `@1001` (`main=229`) → **`9→1`** (`1036`, `vbl=271`) | press `@994` (`main=229`) → **`9→1`** (`1028`, `vbl=271`) | press `@950` (`main=228`) → **`9→1`** (`983`, `vbl=269`) | press `@1162` (`main=232`) → **`9→1`** (`1197`, `vbl=272`) |
| **B3 — 1 press `WaitB62 + 40`** (em `State9_WaitLoop`) | press `@995` (`main=223`) → **`9→1`** (`1030`, `vbl=265`) | press `@988` (`main=223`) → **`9→1`** (`1022`, `vbl=265`) | press `@945` (`main=223`) → **`9→1`** (`978`, `vbl=264`) | press `@1153` (`main=223`) → **`9→1`** (`1188`, `vbl=263`) |
| **C1 — 2 presses `State9 + 112 / +212`** (`800+900`, antes de `WaitLoop`) | press `@807,907` → **`9→13`** (`1602`) | press `@800,900` → **`9→13`** (`1582`) | press `@756,856` → **`9→13`** (`1538`) | press `@968,1068` → **`9→13`** (`1747`) |
| **C2 — 2 presses `WaitB62 + 30 / +100`** (sai já no 1º press) | press `@985` → **`9→1`** (`<1055`, `vbl<262`) | press `@978` → **`9→1`** (`1012`, `vbl=255`) | press `@935` → **`9→1`** (`968`, `vbl=254`) | press `@1143` → **`9→1`** (`1178`, `vbl=253`) |

**Causa-raiz real confirmada:** não há bug nem divergência do handshake de 6 botões entre `ares`, `GPGX`, `BlastEm 1.0.0` e `Native`. A diferença no teste antigo (`Start@950` absoluto) veio de dois fatos somados:
1. O Estado `7` (Title) avança até o Estado `9` em quadros absolutos distintos (`ares 644`, `GPGX 688`, `BlastEm 695` c/ TMSS, `Native 856`), mas **dentro do Estado `9`** todos os 4 executam em fase idêntica (`$9B62 != 0` em `$FD70=182`; attract `9→13` em `$FD70=814`).
2. `State9_Handler` (`$33E6–$33FE`) só entra em `State9_WaitLoop` (`$33FE`) **após** `$9B62 != 0` (`State9 + ~260` quadros) **mais** os `15` quadros de `jsr $FCDE(15)`. Por isso `Start@950` absoluto já encontrava `State9_WaitLoop` no `ares` (`+306`), enquanto no `GPGX` (`+262`) caía **dentro da janela cega do `FCDE(15)`** (borda `$FD54=$0080` consumida antes do `btst #7,($FFFFFD55)`), e a transição `9→1` demora `~33–35` quadros após o press por causa do fade de `State9_ExitShared` (`$3492` / `$D750`), e não por "borda espúria tardia".

**Conclusão:** `gpgx_runner`, `BlastEm 1.0.0`, `ares` e o runtime nativo possuem **comportamento idêntico** no fluxo de 1 press no Estado `9`; **GPGX** permanece como referência automatizada do harness e a questão de arbitragem do `M1` está encerrada.

### Handler do estado 9 (menu-init, $3256 — convertido, verificado)

Fluxo (code/state_handlers3.asm):

1. **`$9CCA==5` → `jmp $200`** (reset suave). `$9CCA=$FFFF` no boot.
2. Desenha a arte do menu (sprites $A9/$AA/$AB/$41 via `jsr $DCEA` com
   `$84(a0)`=índice, objetos $46/$47, args inline para `$F47A`/`$E5FC`).
3. `tst.w $9CCA`; negativo → som ($093C) + `$9CCA=5`. Espera `$9B62≠0`
   (loop $33E6), roda 15 quadros.
4. **Loop de espera** ($33FE): clr `$FD76`, `$FCDE(0)` (roda
   Main_FrameLoop 1x — CB42 só espera VBlank se `$FD76`≠0, então o loop
   roda MUITAS iterações por quadro), testa:
   - `$FD55` bit7 / `$FD57` bit7 (bit `$0080` da borda do pad) → **saída A**
     **$3492** (compartilhada): `$D7A8`, `$D750`, 1 quadro, `$093C`,
     `$9354→$FEFA`, limpa `$9CCC/$9CB2/$9CCA/$9CBE/$9CBC`, rts — `$9CB6`
     fica 0 → tabela de modos avança (09→01) → **estado 1**
   - `$FD61` bit7 (heartbeat) → **saída B**: `$9CCA++`, `$9CCC=2`,
     `$9CB6=2` (ou `=$D` se `$9CCA==6`), `$9388=1`, limpa flags, roda 1
     quadro, rts → **estado 13**


### Handler do menu (estado 13, $36B2 — analisado por disassembly, 2026-11-18)

Estrutura decodificada (ainda não convertido para asm — aguarda a vez):

1. **Init pesado**: `jsr $F42A/$F47A` com **argumentos inline** (dc.w após o
   jsr — convenção "args após a chamada"), cria objetos do fundo do menu,
   `move.b #$1E,($30,a0)` / `move.w #$51,($2C,a0)` (campo/índice de sprite),
   `jsr $F32C/$F446/$E408`, som.
2. **Se estado 12** → pula direto para o fim do init. Senão: seta estado 7
   (título), limpa `$9B30/$9B2E/$9B22/$AC96/$9352`, `jsr $C288`, configura
   `$AC2A/$ABC8/$9C70/$AAF6/$AA94`, `jsr $05D9F6`, **seta estado 13**.
3. **Loop de espera do menu** (`$382A`): `jsr $FCDE` (roda 0 quadros),
   `clr.w $FD76`, sai quando: `$FD61` bit7 OU `$FD59` bit7 OU `$FD5B` bit7.
   - `$FD61` bit7 = "iniciar luta" (setado pelas tarefas de input do
     escalonador cooperativo, `$1FF26`)
   - `$FD59`/`$FD5B` bit7 = bits altos do PadA_State/PadB_State (palavra em
     FD58/FD5A) — setados indiretamente (tabelas de mapeamento do Sub_C870
     em $9274/$92B4/$92A4 ou tarefas do escalonador; não há writer direto
     na ROM baixa)
4. **Saída**: `jsr $D7A8`, roda 1 quadro (`$FE1E`), `jsr $093C` (som), seta
   `$9CB2=6` e `$9CB6=1` → volta ao executor de modos de boot (retry).
5. **Sub-rotinas auxiliares**: `$3896` = espera com `$9CCA=1`/`$9CB6=9`;
   `$38D6` = limpa `$9CBE/$9CBC/$9CCC/$9CB2/$9CCA/$9388`, seta `$9CB6=1`,
   roda 1 quadro. Depois de `$391E`: **tabela de dados** até o estado 14
   ($3980).

**Hipótese sobre a divergência**: ares lê o pad sem a latência do
MK-1653-50 → a detecção de 6 botões do Sub_CA80 (bits de contador) decide
diferente → os bits altos (FD59/FD5B bit7) ficam setados com 1 Start → o
loop do menu sai imediatamente com o modo errado. A confirmar quando a
cadeia de input do escalonador ($1FF26, tarefas $16000–$26000) for
convertida.

### Cadeia de input → menu (decodificada, 2026-11-18)

```
Sub_CA80 ($CA80)          lê a porta crua (TH toggling, detecção de 6 botões)
  → Sub_C870 ($C870)      mapeia p/ formato do jogo (tabelas $9274/$92B4/$92A4)
    → FD58/FD5A (estado), FD54/FD56 (bordas), FD50/FD52 (anterior)
      → Tarefa de input ($1FF00, sob o escalonador cooperativo):
          jsr $DB28 (entra no escalonador)
          btst #7, FD55 / FD57   ; bit 7 ($0080) das BORDAS (botão recém-pressionado)
          → seta $FD61 bit7 ("confirmar/lutar") ou bits 7+4 ("cancelar")
          → bset #2,($74,a5); jmp $DC10 (cede a vez)
        → loop do menu ($382A) vê $FD61 bit7 → sai → $9CB6=1 → boot mode retry
```

O menu também sai com `$FD59`/`$FD5B` bit7 (bits 15 do ESTADO dos pads) —
caminho alternativo provavelmente usado quando o escalonador não roda
(ex.: modo de boot).

## Fluxo de um quadro (FUN_0000fd24)

```
se (FFFF9B32 == 0):            ; primeira execução
    FUN_f2b6(); FUN_e648()     ; ? init de subsistemas (áudio/estado?)
FUN_cd74()                     ; ? update por quadro (input? seleção?)
VDP_CTRL = $8174               ; reg1=$74: DMA + M4 + IE1(VInt)
FFFFFD70 += 1                  ; contador de quadros do laço principal
se (FFFFFD64 == 7) || !(FD69 & $80):
    FD58 = FD5A = FD54 = FD56 = 0    ; ? zera scroll/posição
senão:
    FUN_c870()                 ; ? update de scroll (tela de título?)
se (FD69 & $02): FUN_dc4e()
se (FD69 & $08): FUN_f6a0()    ; 246 linhas de pseudo — ? update grande
se (FD69 & $20): FUN_e684()    ; 127 linhas — ?
se (FD69 & $10): FUN_d7e4()    ; 256 linhas — máquina de estados (luta?)
se (FD69 & $40): FUN_10aa8()
se (FFFF9CA2 != 0): FUN_10b74()
se (FFFF9B32 != 0):            ; pós-init: sincronização de quadro
    espera H-counter < $D7     ; fim da área ativa (0xC00008 = HV counter)
    espera H-counter >= $D8    ; linha 216+
    VDP_CTRL = $8134           ; reg1=$34: VInt desligado
    (A6+2) = $C0000000         ; ? comando p/ o VBlank via ponteiro global A6
    (A6+0) = 0
    FUN_000006f0()             ; Irq_FrameWait — handshake 68k↔Z80
    FUN_f2b6(); FUN_e648()     ; ? de novo (áudio?)
    FUN_00000726()             ; Irq_SignalNoWait
    VDP_CTRL = $8174           ; religa VInt
loop
```

**Implicações para o DESIGN §7.3:** o padrão de espera ativa não é um simples
polling de flag na RAM — é um polling do **HV counter do VDP** (0xC00008)
dentro do laço principal, mais um handshake com o Z80 (`Irq_FrameWait`).
O recompilador precisa detectar este padrão duplo (wait_patterns.toml).

## Flags e estado (RAM $FF0000+, todas hipóteses)

| Endereço | Nome proposto | Evidência |
|---|---|---|
| `$FFFFFD69` | `Main_SubsysFlags` | bitmask que liga cada subsistema por quadro |
| `$FFFFFD70` | `Main_FrameCounter` | incrementado 1×/quadro no laço principal |
| `$FFFFFD72/76` | `Irq_FrameCounterL/W` | incrementados no VBlank |
| `$FFFFFD64` | `Main_GameState` | comparado com 7 no laço |
| `$FFFFFD54–5B` | `Main_Scroll?` | zerados quando estado==7 |
| `$FFFF9B32` | `Boot_InitDone` | gate da primeira execução |
| `$FFFF9CA2` | `Main_Flag9CA2` | habilita FUN_10b74 |
| `$FFFF9B4C/9B30/9B2E` | `?` | muito referenciados (estado de jogo) |
| `$FFFFC838/C836/C83A` | `?` | os mais referenciados do cluster (46/41/28 acessos) |
| `$FFFFF136` | `?` | array de 15 estruturas processadas por FUN_d7e4 |

## Subsistemas (hipóteses)

| Função | Tamanho | Chamada por | Suspeita |
|---|---|---|---|
| `FUN_0000d7e4` | 256 l. | flag $10 | máquina de estados sobre 15 entradas em $FFFFF136 — candidata à lógica de luta |
| `FUN_0000f6a0` | 246 l. | flag $08 | ? |
| `FUN_0000e684` | 127 l. | flag $20 | ? |
| `FUN_0000dc4e` | 52 l. | flag $02 | ? |
| `FUN_0000c870` | 99 l. | quando estado≠7 | ? scroll/anim de título |
| `FUN_0000cd74` | ? | sempre | ? |
| `FUN_0000f2b6`/`e648` | ? | init e pós-espera | ? áudio/estado |
| `FUN_0000d6a0` | 18 l. | VBlank | escreve 4 words de $D6CE no buffer (A6+4) — update de VDP no VBlank |

## Utilitários mais chamados (prováveis libs)

`1032a` (13×), `c764` (12×), `c746` (10×), `dcea` (9×), `df9c` (8×), `dcd6` (8×),
`dd4c` (7×), `de72` (7×), `dcf6` (7×), `ef56` (7×) — candidatos a matemática
16/32 bits, RNG, cópia de memória, etc. A identificar.

## Próximos passos

1. Mapear `$2230` → como chega em `$FD24` (confirmar o caller).
2. Identificar os utilitários (comparar com padrões conhecidos de 68k).
3. Confirmar no debugger do ares (quando buildado): semântica de $FD69, $FD64,
   $FFFFF136 e dos subsistemas.
4. Converter a região em asm real (regiões $C288–$10B74), com verify.py.
