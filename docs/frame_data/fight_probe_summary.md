# Resumo do Harness Determinístico & Frame Data (`M3` / `M4`–`M6`)

- **ROM:** `roms/eternal_champions_us.gen` (`SHA-256: 6d8bdaf8c994d9f718c8f991900c58cdf1af84e030524b57f6dda263cc4ea247`)
- **Executável nativo:** `build/native/EternalChampionsRecomp` (`--benchmark`)
- **Oráculo dinâmico:** `tools/vendor/gpgx_runner/gpgx_runner` + `genesis_plus_gx_libretro.so`

## 1. Autodeterminismo Nativo e Misses (Suíte Completa)

| Movie `.ecm` | Quadros | Diff WRAM (bytes) | PNG SHA-256 Run1 == Run2 | `floor_unsafe` | `dispatch_misses` | `interior_label_misses` | `floor_coverage` |
|---|---:|---:|:---:|---:|---:|---:|---:|
| `01_attract_boot.ecm` | 1000 | 0/65536 | SIM (`aac9990bc9be`) | 0 | 0 | 0 | 0 |
| `02_title_to_fight.ecm` | 2900 | 0/65536 | SIM (`9ea36d3b53de`) | 0 | 0 | 0 | 0 |

## 2. Paridade da Recompilação Estática (`Recomp` × `GENESIS_FORCE_INTERP=1`)

| Movie `.ecm` | WRAM Diff | PNG Igual | Pad (`$9376` Recomp/Interp) | Lógica/Combate | VBlank (`$FD72` R/I) | Resumo do diff |
|---|---:|:---:|:---:|:---:|:---:|---|
| `01_attract_boot.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 75/75 | Idêntico (`0/65536`, PNG igual) |
| `02_title_to_fight.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 1891/1891 | Idêntico (`0/65536`, PNG igual) |

## 3. Cobertura do Elenco e Arenas + Paridade Native vs GPGX

> **Nota técnica (entropia HV-counter em `Math_RandomRange` `$CB82`):** o jogo lê o contador HV ao vivo (`$00C00008`) em `$CB82` (já no quadro `75` do Title para escolher `$9CBE` e durante decisões da IA), gerando sorteios e duração de frames distintos até entre emuladores ciclo-precisos (`BlastEm 1.0.0`, `GPGX` e `ares`). Mantivemos `gvdp_read_hv_counter` (`genesis_vdp.c:467`) intocado para preservar a estabilidade dos hashes e scripts da suíte de `.ecm`.

| Movie `.ecm` | Arena / Cenário | P1 (Nat / Esperado) | P2 (Nat / Esperado) | Native `$FD64` | GPGX `$FD64` | Diff WRAM final | HP Final (P1/P2) |
|---|---|---|---|---:|:---:|---:|---:|
| `01_attract_boot.ecm` | Attract / Menu Init (S9) | — | — | 9 | 9 | 729/65536 | — |
| `02_title_to_fight.ecm` | Battle Simulator / Training Room (S17) | Shadow (0) | Shadow (0) | 17 | 17 | 2194/65536 | 78/104 |

### Checkpoints Detalhados — Boot (`01`) e Luta Principal (`02`)

#### `01_attract_boot.ecm` (1000 quadros) — `byte-swapped-16bit (desfeito via i^1)`

| Quadro | Native `$FD64` | GPGX `$FD64` | Native `$FD72` | GPGX `$FD72` | Bytes divergentes | Primeiro diff |
|---:|---:|---:|---:|---:|---:|---|
| 600 | 7 | 7 | 551 | 551 | 1157/65536 | `0xFF9690` ($FF9690 (unmapped): nat=0xBD vs gpgx=0x68) |
| 1000 | 9 | 9 | 75 | 242 | 729/65536 | `0xFF9B5F` ($FF9B5F (unmapped): nat=0x00 vs gpgx=0x02) |

#### `02_title_to_fight.ecm` (2900 quadros) — `byte-swapped-16bit (desfeito via i^1)`

| Quadro | Native `$FD64` | GPGX `$FD64` | Native `$FD72` | GPGX `$FD72` | Bytes divergentes | Primeiro diff |
|---:|---:|---:|---:|---:|---:|---|
| 600 | 7 | 7 | 551 | 551 | 1157/65536 | `0xFF9690` ($FF9690 (unmapped): nat=0xBD vs gpgx=0x68) |
| 1200 | 9 | 9 | 275 | 442 | 411/65536 | `0xFF9B78` ($FF9B78 (unmapped): nat=0xB4 vs gpgx=0x00) |
| 1926 | 16 | 1 | 971 | 1139 | 693/65536 | `0xFF9B7E` ($FF9B7E (unmapped): nat=0xB1 vs gpgx=0xAD) |
| 2074 | 16 | 16 | 1118 | 1286 | 543/65536 | `0xFF9B7E` ($FF9B7E (unmapped): nat=0xB1 vs gpgx=0xAD) |
| 2400 | 17 | 17 | 1391 | 1558 | 826/65536 | `0xFF9695` ($FF9695 (unmapped): nat=0x04 vs gpgx=0x00) |
| 2500 | 17 | 17 | 1491 | 1658 | 672/65536 | `0xFF9B2F` ($FF9B2F (unmapped): nat=0x04 vs gpgx=0x00) |
| 2600 | 17 | 17 | 1591 | 1758 | 599/65536 | `0xFF9B7E` ($FF9B7E (unmapped): nat=0xB1 vs gpgx=0xAD) |
| 2750 | 17 | 17 | 1741 | 1908 | 1554/65536 | `0xFF9695` ($FF9695 (unmapped): nat=0x04 vs gpgx=0x05) |
| 2900 | 17 | 17 | 1891 | 2058 | 2194/65536 | `0xFF9695` ($FF9695 (unmapped): nat=0x04 vs gpgx=0x05) |

## 4. Progressão de Luta Ativa Observada (`02_title_to_fight.ecm`)

| Quadro | Engine | Estado | Timer BCD | P1 HP | P1 Pos (X,Y) | P1 Action/Move | P2 HP | P2 Pos (X,Y) | P2 Action/Move |
|---:|---|---:|---:|---:|---|---:|---:|---|---:|
| 600 | Native | 7 | `$FFFF` | 104 | (56, 49) | 0/0 | 0 | (152, 120) | 0/0 |
| 600 | GPGX | 7 | `$FFFF` | 104 | (64, 160) | 0/0 | 0 | (152, 120) | 0/0 |
| 1200 | Native | 9 | `$0000` | 0 | (0, 0) | 0/0 | 0 | (0, 0) | 0/0 |
| 1200 | GPGX | 9 | `$0000` | 0 | (0, 0) | 0/0 | 0 | (0, 0) | 0/0 |
| 1926 | Native | 16 | `$0000` | 0 | (0, 0) | 0/0 | 0 | (0, 0) | 0/0 |
| 1926 | GPGX | 1 | `$0000` | 0 | (0, 0) | 0/0 | 0 | (0, 0) | 0/0 |
| 2074 | Native | 16 | `$0000` | 0 | (0, 0) | 0/0 | 0 | (0, 0) | 0/0 |
| 2074 | GPGX | 16 | `$0000` | 0 | (0, 0) | 0/0 | 0 | (0, 0) | 0/0 |
| 2400 | Native | 17 | `$0098` | 104 | (395, 216) | 0/0 | 104 | (560, 216) | 0/0 |
| 2400 | GPGX | 17 | `$0098` | 104 | (395, 216) | 0/0 | 104 | (560, 216) | 0/0 |
| 2500 | Native | 17 | `$0095` | 102 | (257, 216) | 0/0 | 104 | (404, 216) | 0/4 |
| 2500 | GPGX | 17 | `$0095` | 102 | (257, 216) | 0/0 | 104 | (404, 216) | 0/4 |
| 2600 | Native | 17 | `$0092` | 102 | (273, 216) | 0/0 | 104 | (232, 216) | 0/0 |
| 2600 | GPGX | 17 | `$0092` | 102 | (273, 216) | 0/0 | 104 | (232, 216) | 0/0 |
| 2750 | Native | 17 | `$0087` | 91 | (475, 216) | 0/0 | 104 | (341, 217) | 0/1 |
| 2750 | GPGX | 17 | `$0087` | 87 | (319, 216) | 0/0 | 104 | (232, 216) | 0/5 |
| 2900 | Native | 17 | `$0082` | 78 | (615, 216) | 0/0 | 104 | (434, 216) | 0/0 |
| 2900 | GPGX | 17 | `$0082` | 76 | (437, 151) | 0/0 | 104 | (297, 216) | 0/4 |

## 5. Validação Widescreen 16:9 (`game/ec_video.c` — Todas as Arenas)

| Captura | Cenário / Estado | Dimensão PNG | Margem Esq. (não-preta / total) | Margem Dir. (não-preta / total) | WRAM Diff vs 320x224 |
|---|---|---|---:|---:|---:|
| `assets/captures/title_16_9.png` | Pillarbox Title (Estado 7) | 398x224 | 0/8736 | 0/8736 | 0/65536 |
| `assets/captures/fight_16_9.png` | Battle Simulator / Training Room (S17) | 398x224 | 8593/8736 | 8461/8736 | 0/65536 |
