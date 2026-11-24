# Movelist — Extração dos Movimentos (RE)

Extração sequencial dos golpes de todos os personagens direto da ROM
(`roms/eternal_champions_us.gen`, US 24 Mbit, SHA-256 `6d8bdaf8…`),
validada dinamicamente no harness.

## Metodologia

1. **Estático** (fonte primária): `disasm/code/*.asm` (regiões convertidas,
   hash-verificadas por `verify.py`) + pseudocódigos Ghidra em
   `reference/rea/decompile-2026-11-18/functions/*.c` + `labels.toml`.
2. **Dinâmico** (validação): debug server TCP (`--port 4378`): input override
   + `read_ram` — injeta a sequência candidata e lê o ID do golpe em RAM.
3. **Registro**: um arquivo por personagem em `docs/movements/`.

## Pontos de entrada (ROM)

| Endereço | Papel |
|---|---|
| `$044002–$04AFFF` | máquinas de estado de golpes (fight_moves1) |
| `~$05A000` | tabelas de registros de golpe (`dc.w`, IDs `69xx/7Axx/73xx` + física) |
| `$05A07E` | handler de golpe: ID → `($A8,a5)` |
| `$05B6B2` | decodificador de input (FD5A/5B → atual/borda, tabela `$05B8FC`) |
| `$05DFCC` | `move.w #$D,($FFFF9B22)` — espelho de golpe em RAM fixa (a investigar) |
| `$D7E4` | StateMachine (StateTaskArray `$FFFFF136`, 15 entradas) |
| `$968E` → `$3533E`/`$3547C` | tabelas de ponteiros por personagem |

## Formato bruto (por arquivo de personagem)

```
# docs/movements/<personagem>.md
## Moves
| ID | Entrada | Golpe | Notas |
```

Flags de anomalia: `⚠️ suspeito` · `🔒 desativado` · `🤫 secreto` · `👤 não mapeado`

## Progresso

- [x] Estrutura e pontos de entrada
- [ ] A1: matcher de sequência de input
- [ ] A2: tabelas por personagem
- [ ] A3: decodificação das tabelas
- [ ] B1: probe dinâmico (debug server)
- [ ] B2: validação por personagem
- [ ] B3: auditoria de anomalias/segredos/desativados
