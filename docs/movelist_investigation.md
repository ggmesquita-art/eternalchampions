# Investigação: Movelist do Eternal Champion (RE)

**Objetivo:** extrair a lista completa de golpes do Eternal Champion (chefe) —
sequência de input → golpe (e, se possível, dados de frame/dano) — direto do
código da ROM, validada dinamicamente no harness.

**Estado:** em andamento (2026-11-20)

## Pontos de entrada identificados

| Endereço | O que é | Evidência |
|---|---|---|
| `$044002–$04AFFF` | `fight_moves1.asm` — máquinas de estado de golpes + física | disasm/code/fight_moves1.asm |
| `$05A07E` | handler de golpe: guarda o ID do golpe em `($A8,a5)` (objeto do lutador) | combat_core.asm |
| `~$05A000` | tabelas `dc.w` de definição de golpes (IDs `69xx`/`7Axx`/`73xx` + física) | combat_core.asm |
| `$05B6B2` (`Combat_CameraTask3`) | decodificador de input de luta: FD5A/FD5B/FD53/FD52 → botões atuais `($8D,a5)`, bordas `($8B,a5)`, com tabela de validação em `$05B8FC` | combat_core.asm |
| `$05E08A` | leitura de FD54/FD56 (edge de direção) na máquina de estado | combat_core.asm |
| `$D7E4` (`Sub_StateMachine`) | processa `StateTaskArray` em `$FFFFF136` (15 entradas) | util_alloc_vdp.asm |
| `$968E` → `$3533E`/`$3547C` | tabelas de ponteiros por personagem | docs/PROGRESS.md |

## Fatos estabelecidos

- Input de luta: `$FFFFFD5A` (estado), `$FFFFFD5B` (P2?), `$FFFFFD52/53` — decomposto em
  atual/borda e validado por tabela em `$05B8FC`.
- O ID do golpe corrente fica em `+$A8` do objeto do lutador (a5).
- O Eternal Champion tem **5 formas** trocadas por UP/DOWN no retrato
  (`game/ec_boss.c`, pc `$030F48`, paletas `$F096`/`$F076`) — a terceira (fogo)
  está com bug de renderização (reportado pelo usuário).

## Plano

1. **Estático**: decodificar o matcher de sequências (quem lê as bordas de
   direção + botões e casa com as tabelas por personagem) — via
   `disasm/code/*.asm` + pseudocódigos em `reference/rea/decompile-2026-11-18/`.
2. **Dinâmico**: com o EC selecionado, injetar sequências candidatas
   (`--input-script`) e ler o ID do golpe (`+$A8` do objeto / mirrors em
   `$FFFF9Cxx`) + `StateTaskArray $FFFFF136` — confirmação empírica de cada
   sequência → golpe.
3. **Produto**: movelist do EC (entrada → golpe, formas 0–4), depois estender
   aos demais lutadores.

## Perguntas abertas

- ID/slot do EC no elenco de luta (base de dados do personagem — P1 `$AB86`?).
- Onde o matcher de sequência usa a tabela por personagem (offset via `$968E`?).
- Se as tabelas de golpe carregam frame data (startup/active/recovery) junto.
