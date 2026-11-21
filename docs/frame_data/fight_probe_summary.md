# Resumo do Harness Determinístico & Frame Data (`M3` / `M4`–`M6`)

- **ROM:** `roms/eternal_champions_us.gen` (`SHA-256: 6d8bdaf8c994d9f718c8f991900c58cdf1af84e030524b57f6dda263cc4ea247`)
- **Executável nativo:** `build/native/EternalChampionsRecomp` (`--benchmark`)
- **Oráculo dinâmico:** `tools/vendor/gpgx_runner/gpgx_runner` + `genesis_plus_gx_libretro.so`

## 1. Autodeterminismo Nativo e Misses (Suíte Completa)

| Movie `.ecm` | Quadros | Diff WRAM (bytes) | PNG SHA-256 Run1 == Run2 | `floor_unsafe` | `dispatch_misses` | `interior_label_misses` | `floor_coverage` |
|---|---:|---:|:---:|---:|---:|---:|---:|
| `01_attract_boot.ecm` | 1000 | 0/65536 | SIM (`aac9990bc9be`) | 0 | 0 | 0 | 0 |
| `02_title_to_fight.ecm` | 2900 | 0/65536 | SIM (`9ea36d3b53de`) | 0 | 0 | 0 | 0 |
| `03_practice_rax_vs_blade_s21.ecm` | 3400 | 0/65536 | SIM (`101f4d92d6e3`) | 0 | 0 | 0 | 0 |
| `04_practice_blade_vs_jetta_s22.ecm` | 3400 | 0/65536 | SIM (`0b74d17bab35`) | 0 | 0 | 0 | 0 |
| `05_practice_jetta_vs_slash_s26.ecm` | 3400 | 0/65536 | SIM (`3d899a3e4ded`) | 0 | 0 | 0 | 0 |
| `06_practice_slash_vs_trident_s24.ecm` | 3400 | 0/65536 | SIM (`f7c193a6a83e`) | 0 | 0 | 0 | 0 |
| `07_practice_trident_vs_xavier_s20.ecm` | 3400 | 0/65536 | SIM (`147a446e829c`) | 0 | 0 | 0 | 0 |
| `08_practice_xavier_vs_midknight_s19.ecm` | 3400 | 0/65536 | SIM (`19fd4cafc4a4`) | 0 | 0 | 0 | 0 |
| `09_practice_midknight_vs_larcen_s27.ecm` | 3400 | 0/65536 | SIM (`64e158640bdc`) | 0 | 0 | 0 | 0 |
| `10_practice_larcen_vs_rax_s23.ecm` | 3400 | 0/65536 | SIM (`ee597d5d2035`) | 0 | 0 | 0 | 0 |
| `11_practice_shadow_vs_larcen_s25.ecm` | 3400 | 0/65536 | SIM (`84aaed6cfb1f`) | 0 | 0 | 0 | 0 |
| `12_holo_trainer_s14.ecm` | 2900 | 0/65536 | SIM (`e59a53bdd7b2`) | 0 | 0 | 0 | 0 |
| `13_boss_sanctuary_s18.ecm` | 3000 | 0/65536 | SIM (`c4fb41910bc7`) | 0 | 0 | 0 | 0 |
| `14_2p_vs_rax_vs_blade_s24.ecm` | 3900 | 0/65536 | SIM (`e0d5b409fd03`) | 0 | 0 | 0 | 0 |
| `15_tournament_roundabout_jetta_vs_xavier_s27.ecm` | 3400 | 0/65536 | SIM (`f294fd2534bd`) | 0 | 0 | 0 | 0 |
| `16_2p_vs_match_end_s24_to_s1.ecm` | 7000 | 0/65536 | SIM (`e6862f4a1bea`) | 0 | 0 | 0 | 0 |
| `17_2p_vs_overkill_trident_s24.ecm` | 6900 | 0/65536 | SIM (`eba7a99d3626`) | 0 | 0 | 0 | 0 |

## 2. Paridade da Recompilação Estática (`Recomp` × `GENESIS_FORCE_INTERP=1`)

| Movie `.ecm` | WRAM Diff | PNG Igual | Pad (`$9376` Recomp/Interp) | Lógica/Combate | VBlank (`$FD72` R/I) | Resumo do diff |
|---|---:|:---:|:---:|:---:|:---:|---|
| `01_attract_boot.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 75/75 | Idêntico (`0/65536`, PNG igual) |
| `02_title_to_fight.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 1891/1891 | Idêntico (`0/65536`, PNG igual) |
| `03_practice_rax_vs_blade_s21.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 2416/2416 | Idêntico (`0/65536`, PNG igual) |
| `04_practice_blade_vs_jetta_s22.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 2410/2410 | Idêntico (`0/65536`, PNG igual) |
| `05_practice_jetta_vs_slash_s26.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 2422/2422 | Idêntico (`0/65536`, PNG igual) |
| `06_practice_slash_vs_trident_s24.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 2400/2400 | Idêntico (`0/65536`, PNG igual) |
| `07_practice_trident_vs_xavier_s20.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 2403/2403 | Idêntico (`0/65536`, PNG igual) |
| `08_practice_xavier_vs_midknight_s19.ecm` | 4/65536 | SIM | `0x0101` / `0x0101` | PASS | 2408/2408 | 1º `0xFFAAFB` 0x00!=0x0D; top: `Fighter_P2+0xA9` (1B), `$FFE897 (unmapped)` (1B), `$FFE898 (unmapped)` (1B) |
| `09_practice_midknight_vs_larcen_s27.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 2401/2401 | Idêntico (`0/65536`, PNG igual) |
| `10_practice_larcen_vs_rax_s23.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 2419/2419 | Idêntico (`0/65536`, PNG igual) |
| `11_practice_shadow_vs_larcen_s25.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 2415/2415 | Idêntico (`0/65536`, PNG igual) |
| `12_holo_trainer_s14.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 1904/1904 | Idêntico (`0/65536`, PNG igual) |
| `13_boss_sanctuary_s18.ecm` | N/A | N/A | — | N/A (hook C `EC_DIAG_BOSS_SANCTUARY` em `ec_call_vblank`) | — | — |
| `14_2p_vs_rax_vs_blade_s24.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 2900/2900 | Idêntico (`0/65536`, PNG igual) |
| `15_tournament_roundabout_jetta_vs_xavier_s27.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 2401/2401 | Idêntico (`0/65536`, PNG igual) |
| `16_2p_vs_match_end_s24_to_s1.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 5925/5925 | Idêntico (`0/65536`, PNG igual) |
| `17_2p_vs_overkill_trident_s24.ecm` | 0/65536 | SIM | `0x0101` / `0x0101` | PASS | 5855/5855 | Idêntico (`0/65536`, PNG igual) |

## 3. Cobertura do Elenco e Arenas + Paridade Native vs GPGX

> **Nota técnica (entropia HV-counter em `Math_RandomRange` `$CB82`):** o jogo lê o contador HV ao vivo (`$00C00008`) em `$CB82` (já no quadro `75` do Title para escolher `$9CBE` e durante decisões da IA), gerando sorteios e duração de frames distintos até entre emuladores ciclo-precisos (`BlastEm 1.0.0`, `GPGX` e `ares`). Mantivemos `gvdp_read_hv_counter` (`genesis_vdp.c:467`) intocado para preservar a estabilidade dos hashes e scripts da suíte de `.ecm`.

| Movie `.ecm` | Arena / Cenário | P1 (Nat / Esperado) | P2 (Nat / Esperado) | Native `$FD64` | GPGX `$FD64` | Diff WRAM final | HP Final (P1/P2) |
|---|---|---|---|---:|:---:|---:|---:|
| `01_attract_boot.ecm` | Attract / Menu Init (S9) | — | — | 9 | 9 | 729/65536 | — |
| `02_title_to_fight.ecm` | Battle Simulator / Training Room (S17) | Shadow (0) | Shadow (0) | 17 | 17 | 2194/65536 | 78/104 |
| `03_practice_rax_vs_blade_s21.ecm` | Blade Arena (S21) | RAX (1) | Blade (2) | 21 | 21 | 1944/65536 | 0/104 |
| `04_practice_blade_vs_jetta_s22.ecm` | Jetta Arena (S22) | Blade (2) | Jetta (3) | 22 | 22 | 1851/65536 | 70/96 |
| `05_practice_jetta_vs_slash_s26.ecm` | Slash Arena (S26) | Jetta (3) | Slash (4) | 26 | 26 | 1445/65536 | 49/92 |
| `06_practice_slash_vs_trident_s24.ecm` | Trident Arena (S24) | Slash (4) | Trident (5) | 24 | 24 | 2218/65536 | 81/82 |
| `07_practice_trident_vs_xavier_s20.ecm` | Xavier Arena (S20) | Trident (5) | Xavier (6) | 20 | 20 | 636/65536 | 80/104 |
| `08_practice_xavier_vs_midknight_s19.ecm` | Midknight Arena (S19) | Xavier (6) | Midknight (7) | 19 | 19 | 2213/65536 | 84/73 |
| `09_practice_midknight_vs_larcen_s27.ecm` | Larcen Arena (S27) | Midknight (7) | Larcen (8) | 27 | 27 | 1872/65536 | 67/95 |
| `10_practice_larcen_vs_rax_s23.ecm` | RAX Arena (S23) | Larcen (8) | RAX (1) | 23 | 23 | 1997/65536 | 18/85 |
| `11_practice_shadow_vs_larcen_s25.ecm` | Shadow Arena (S25) | Shadow (0) | Larcen (8) | 25 | 25 | 1143/65536 | 56/104 |
| `12_holo_trainer_s14.ecm` | Holo-Trainer Dojo (S14) | Shadow (0) | Shadow (0) | 14 | 14 | 2055/65536 | 54/96 |
| `13_boss_sanctuary_s18.ecm` | Eternal Champion Sanctuary (S18) | Shadow (0) | Eternal Champion (9) | 18 | N/A (probe nativo State 18) | N/A | 86/104 |
| `14_2p_vs_rax_vs_blade_s24.ecm` | Trident Arena (S24, 2P VS) | RAX (1) | Blade (2) | 24 | 24 | 368/65536 | 81/95 |
| `15_tournament_roundabout_jetta_vs_xavier_s27.ecm` | Tournament Round-A-Bout (S27, arena sorteada) | Jetta (3) | Xavier (6) | 27 | 19 | 13841/65536 | 104/84 |
| `16_2p_vs_match_end_s24_to_s1.ecm` | 2P VS fim de partida (S24 → WINS COMPETITION → S1) | RAX (1) | Blade (2) | 1 | 1 | 711/65536 | — |
| `17_2p_vs_overkill_trident_s24.ecm` | Trident Arena Overkill (S24, 2P VS, $9C7C=1) | RAX (1) | Blade (2) | 24 | 24 | 1410/65536 | 104/0 |

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
| `assets/captures/fight_16_9.png` | Battle Simulator / Training Room (S17) | 398x224 | 8517/8736 | 8430/8736 | 0/65536 |
| `assets/captures/s21_rax_blade_16_9.png` | Blade Arena (S21) | 398x224 | 7529/8736 | 7434/8736 | 0/65536 |
| `assets/captures/s22_blade_jetta_16_9.png` | Jetta Arena (S22) | 398x224 | 8619/8736 | 8619/8736 | 0/65536 |
| `assets/captures/s26_jetta_slash_16_9.png` | Slash Arena (S26) | 398x224 | 8092/8736 | 7913/8736 | 0/65536 |
| `assets/captures/s24_slash_trident_16_9.png` | Trident Arena (S24) | 398x224 | 8561/8736 | 8559/8736 | 0/65536 |
| `assets/captures/s20_trident_xavier_16_9.png` | Xavier Arena (S20) | 398x224 | 8113/8736 | 8533/8736 | 0/65536 |
| `assets/captures/s19_xavier_midknight_16_9.png` | Midknight Arena (S19) | 398x224 | 7982/8736 | 8401/8736 | 0/65536 |
| `assets/captures/s27_midknight_larcen_16_9.png` | Larcen Arena (S27) | 398x224 | 8619/8736 | 8619/8736 | 0/65536 |
| `assets/captures/s23_larcen_rax_16_9.png` | RAX Arena (S23) | 398x224 | 6257/8736 | 5937/8736 | 0/65536 |
| `assets/captures/s25_shadow_larcen_16_9.png` | Shadow Arena (S25) | 398x224 | 7195/8736 | 6212/8736 | 0/65536 |
| `assets/captures/s14_holo_trainer_16_9.png` | Holo-Trainer Dojo (S14) | 398x224 | 8524/8736 | 8463/8736 | 0/65536 |
| `assets/captures/s18_boss_sanctuary_16_9.png` | Eternal Champion Sanctuary (S18) | 398x224 | 8138/8736 | 8277/8736 | 0/65536 |
| `assets/captures/s24_2p_vs_rax_blade_16_9.png` | Trident Arena (S24, 2P VS) | 398x224 | 8557/8736 | 8553/8736 | 0/65536 |
| `assets/captures/s27_tournament_jetta_xavier_16_9.png` | Tournament Round-A-Bout (S27, arena sorteada) | 398x224 | 8424/8736 | 7994/8736 | 0/65536 |
| `assets/captures/s24_2p_vs_overkill_trident_16_9.png` | Trident Arena Overkill (S24, 2P VS, $9C7C=1) | 398x224 | 8560/8736 | 8560/8736 | 0/65536 |
