# Fase 3 — Validação Determinística (`M3`)

> Extraído de [DESIGN.md](../DESIGN.md). Ver também [ROADMAP.md](../ROADMAP.md).

## 8. Fase 3: Validação determinística

O harness é o componente que torna o projeto viável. Ele precisa existir **antes** de qualquer trabalho no renderer.

- **Formato de movie**: `.ecm` (cabeçalho com o hash da ROM, a seed e o estado inicial, mais um quadro de input por linha, com bitmask de 12 botões).
- **Referência**: Genesis Plus GX via libretro, sem frontend, carregando o mesmo movie.
- **Comparação**: dump da RAM de trabalho de 64 KiB ao fim de cada quadro. O diff reporta o primeiro quadro divergente, os endereços e os nomes do `ram_map.toml`.
- **Suíte de CI**: um conjunto de movies cobrindo cada personagem, cada arena, as finalizações, o modo de treino e a IA em várias dificuldades.
- **Frame data**: o harness exporta tabelas de startup, active e recovery e de hitboxes por golpe para `docs/frame_data/`. Isso serve de documentação e de teste de regressão.

**Critério de saída**: 100% dos movies da suíte idênticos à referência *(revisado abaixo para critério em 2 camadas em função de leitura ao vivo do contador HV)*.

## Revisão do critério de aceite e diagnóstico de paridade (2026-11-19)

1. **Impossibilidade física de `0/65536` WRAM vs `GPGX` sem falsificar hardware:**
   - `Math_RandomRange` (`$CB82`, [util_combat_input.asm:867-881](../disasm/code/util_combat_input.asm#L867-L881)) lê o contador HV ao vivo `($00C00008).l` duas vezes e desloca o byte baixo (H-counter) para o byte alto antes de `mulu`.
   - Já no quadro `75` (`State7_Handler`, [state_handlers2.asm:263-266](../disasm/code/state_handlers2.asm#L263-L266)), o jogo chama `$CB82(0, 8)` para sortear o lutador da demo de título (`$FFFF9CBE`), alterando a duração da tela de título (`~475`–`833` quadros), e volta a chamar `$CB82` nas decisões da IA em combate.
   - Sondagem pré-RNG (`f=1..75`): `Native` e `GPGX` coincidem integralmente no boot (`0` diffs em `f=1..2`), acumulam apenas `10`–`24` B de pacing da descompressão LZ (`f=10, 30, 70`: pilha `$FFDA..$FFFF`, contador VBlank e scratch `$9CA0`/`$FDF6..$FE01`) e chegam ao quadro `75` com `vbl=26` e LFSR `$FEFA=0x5BDB` idênticos — momento em que `$C00008` sorteia `$9CBE=3` (Jetta) no nativo contra `0` (Shadow) no `GPGX`.
   - Mesmo três emuladores ciclo-precisos (`BlastEm 1.0.0`, `GPGX` e `ares`) divergem entre si no quadro de saída do título (`ares` `644`, `GPGX` `688`, `BlastEm 1.0.0` `695` com TMSS / `~475` pós-boot).

2. **Critério revisado em 2 camadas (`M3`) — 100% aprovado (`harness/ec_harness.py`, exit `0`):**
   - **Camada 1 — Equivalência da recompilação estática (`Recomp` × `GENESIS_FORCE_INTERP=1`, `01`..`12`; `13_boss_sanctuary_s18.ecm` N/A por hook C `EC_DIAG_BOSS_SANCTUARY=1` em `ec_call_vblank()`):**
     - `12/12` `gameplay_match=True`, `12/12` `png_eq=True` (PNG pixel a pixel idêntico), `12/12` `pad_cfg=0x0101/0x0101` e `12/12` `$FD72` VBlank idêntico (`75/75`, `1891/1891`, `2416/2416`, `2410/2410`, `2422/2422`, `2400/2400`, `2403/2403`, `2408/2408`, `2401/2401`, `2419/2419`, `2415/2415`, `1904/1904`).
     - `65.534/65.536` bytes bit a bit idênticos (`2/65536` diff idêntico em todos os 12 filmes): exclusivamente `$FFC8B7` (`0xA0 != 0x4C`) e `$FFC8BB`, LSBs mortos do cursor de retomada do streamer de VRAM (`$FFC8B6` `VramStream_VramCur` + `$FFC8B8` `VramStream_Ptr` em `Sub_10B74`/`Sub_10BFE`, [engine_ext.asm:1065-1156](../disasm/code/engine_ext.asm#L1065-L1156)). Durante o streaming ativo, `loc_10BE2` testa o deadline HV `$C800` (`cmp.w ($00C00008).l,d5`) e salva `d6`/`a0`; ao concluir o stream (`Sub_10C60`), o jogo zera `($FFFF9CA2).w = 0` e retorna sem sobrescrever `$FFC8B6`/`$FFC8B8`, deixando esses 2 bytes mortos e nunca mais lidos no restante da execução.
   - **Camada 2 — Autodeterminismo, higiene de execução e paridade estrutural vs `GPGX` (`01`..`13`):**
     - `13/13` autodeterminismo `0/65536` bytes + PNG SHA-256 idêntico + `(0, 0, 0, 0)` misses (`floor_unsafe`, `dispatch_misses`, `interior_label_misses`, `floor_coverage`).
     - `12/12` (`01`..`12`) acordo exato com `GPGX` em estado final `$FD64`, `pad_cfg=0x0101` e IDs P1/P2 (diff residual de `640`–`2.216/65536` B decorrente do RNG via HV-counter em `$CB82`; `02_title_to_fight.ecm` alinhado em HP/pos/timer até o quadro `2600`).
     - `13/13` capturas widescreen 16:9 (`398x224`) com margens ativas preenchidas (pillarbox `0/8736` no Title `S7`) e `0/65536` diff de WRAM frente a `320x224`.

3. **Dois bugs reais encontrados e corrigidos no framework/runtime:**
   - `segagenesisrecomp/runner/main.c:3227` + [game/ec_spec.c](../game/ec_spec.c): `main.c` nunca invocava `g_game_spec.on_post_reset()` (contrato em `game_spec.h:198-202`), deixando `GENESIS_FORCE_INTERP=1` e o quadro 0 em modo 3 botões (`$9376=0x0000`). Corrigido chamando `on_post_reset()` imediatamente após `glue_init()` e tornando `ec_on_post_reset()` idempotente via `s_boot_init_done`.
   - `segagenesisrecomp/runner/m68k_interp.c` + `segagenesisrecomp/cmake/GenesisRecompRunner.cmake`: `m68k_interp.c` utilizava estimador local defasado (`MN_DBcc=12` vs `10`, escrita `-(An)` `10/6` vs `8/4`, bug de `dst_ea`), provocando drift de 1 VBlank já no quadro 8 (`Boot_RamClear`: `16.384 × dbra`), deslocamento de `158` em `$FD70` (`158 % 4 == 2`) e inversão de `btst.b #$1,($FFFFFD71).w` em `fight_moves1..3.asm`/`combat_core.asm` (`9/12` falhas antes da correção). Unificado para `(int)m68k_68000_nominal_cycles(ins)` (de `common/m68k_timing.c`), o mesmo usado em `code_generator.c:1810`.

4. **Decisão documentada sobre `gvdp_read_hv_counter` (`segagenesisrecomp/runner/video/genesis_vdp.c:463-468`):**
   - Mantido intocado por decisão explícita (`((v->scanline & 0xFF) << 8) | (v->in_hblank ? 0x00 : 0x80)`).
   - **Prós:** preserva estabilidade dos hashes/PNGs e timings dos 13 filmes `.ecm`, evita alterar o VDP compartilhado do `segagenesisrecomp`, e o jogo já possui entropia real via LFSR `$FEFA` + `scanline` (V-counter).
   - **Contras:** a componente H-counter dentro de `$CB82` fica com entropia de 1 bit (`0x00`/`0x80`); pode ser refinada futuramente com `v->line_cycles` caso se deseje, regerando os hashes da suíte.

5. **Revisão de 2026-11-19 (`M7.3`): Camada 1 com o jogo 100% recompilado.**
   - A Camada 1 acima (`65.534/65.536`, `12/12`) era em parte artefato: a task `$24C98` do attract entrava por um dispatch miss no floor Tier-3 e, a partir dali, laço principal, escalonador `$DB4E`, IRQ e menus rodavam **interpretados** (`--exec-coverage-out` sem `GENESIS_FORCE_INTERP`: `4.111` PCs; `1.062` fps contra `1.449` fps agora).
   - Com as sementes faltantes adicionadas em [tools/recomp/gen_recomp_config.py](../tools/recomp/gen_recomp_config.py) (`19` tabelas `bra_w`, slots isentos da heurística ASCII, callbacks `$36(a0)`/`$968E`, listas `bra.w` de menu), o código recompilado passou a divergir do interpretador por **ordem de cobrança de ciclos**: `code_generator.c` cobrava `jsr`/`bsr` (`18`/`20` ciclos) após o retorno do callee, enquanto o 68000 e `m68k_interp.c` cobram antes. Corrigido em `segagenesisrecomp/external/m68k-recomp-core/profiles/genesis/code_generator.c` (cobrança logo após `recomp_push_return`; a contabilidade genérica pós-instrução e o ramo `g_rte_pending` deixam de cobrar `JSR`/`BSR`).
   - Resultado (`harness/ec_harness.py`, exit `0`, `17` movies): **`16/16` com `0/65536` B** contra `GENESIS_FORCE_INTERP=1` (até os `2` B do cursor `VramStream` coincidem), PNG idêntico, `17/17` com `(0,0,0,0)` misses; o fim de partida (filme longo de practice até `f7000`) também fica `0/65536` em todos os checkpoints e volta ao Estado `1`.
   - `$05E242` (Duff's device de `exg` com registradores distintos, embaralhamento de paleta em `$5E20E`): o walker do gerador e o `seed_pc_idx_dispatch_targets`/`probe_pc_idx_targets` do `code_generator.c` passaram a aceitar caudas uniformes por **mnemônico + tamanho 2** (antes: opcode idêntico) → compilado como switch interno de `8` alvos; `INTERIOR_UNRESOLVED = 0` no audit. Chamado por efeitos de golpes especiais (`$1AD6A` objeto `$56`, `$2BB12`), ainda não exercitado por nenhum movie.
   - Resíduo caracterizado (não corrigido): em `f1143` do attract, entre os ciclos `33730` e `106808` do quadro de parede, o acumulador de CPU do recompilado fica `1.200`–`1.210` ciclos atrás dos eventos presos ao raster (escritas de `$FD6A` e IRQ de VBlank `$FD72..$FD75` saem `1.200` ciclos "mais cedo" no acumulador); antes e depois (`f1142`, `f1144`) tudo coincide ciclo a ciclo. Sem efeito em WRAM/PNG em `17/17` movies nem no filme longo de fim de partida. Corrigir exige instrumentar o modelo de yield por spin/poll do Z80 no `glue.c` (build `GEN_DEV_TRACE`/cosim) — fica como pendência do engine.

Ver tabelas completas de execução em [frame_data/fight_probe_summary.md](frame_data/fight_probe_summary.md).
