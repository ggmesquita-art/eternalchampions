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

### Mapa de estados (trace do ares_cli, 1200–2400 quadros)

| Momento | Estado |
|---|---|
| boot | 0 → **7 (título)** no quadro 23 |
| sem input | 7 → **13** (~attract/demo) |
| Start no quadro 600 | 7 → 0 (transição) → **9 (menu)** no quadro 644 |
| Start no menu (950) | 9 → **1** (setup de jogo: cria objetos, espera Start, config de pads) |

Handlers: `State_HandlerTable[$27B4]` (40 offsets relativos). Estado 7 →
`$2EF2`, estado 9 → `$3256` (menu: `$9CCA==5 → jmp $200` = reset), estado 0 →
`$2804` (`rts`), estado 1 → `$2806` (convertido por completo: loop por quadro,
cria objetos `$B8/$41/$AB`, espera `Start` em `$FD61` bit7, termina em `rts`).
`$FD68` = flag "estado mudou" ($FFFF a cada dispatch).

**Em aberto:** no ares_cli o estado 1 não progride (a transição deve vir de um
subsistema — o array de tarefas de `$D7E4` — que depende de detecção de pad
`$9CB0` ou de timing fino; investigar). A lógica do menu vive nos subsistemas
(`$F32C`/`$F47A` criam os objetos do menu).

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
