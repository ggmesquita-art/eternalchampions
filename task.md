# `task.md` — Fase 1: Disassembly Reconstruível (`M1`)

> Fase ativa: [docs/phase-1.md](docs/phase-1.md) · Estado detalhado: [docs/PROGRESS.md](docs/PROGRESS.md) · Visão geral: [ROADMAP.md](ROADMAP.md)
> Regra de ciclo: 1 item por iteração → validar com `tools/verify.py` → marcar `- [x]` → aguardar confirmação.

## Concluído anteriormente (`M0` + início da Fase 1)

- [x] Setup `M0`: `split.py`, `verify.py`, `vasm` 1.9d, Ghidra 12.2 + REA, `ares_cli`, `gpgx_runner` + `.ecm`, `reference/roms.toml`
- [x] Boot, IRQ, Z80/som (`$000200–$000A7F`), `main_entry.asm` (`$002230–$0025FD`), `vdp_init.asm` (`$002786–$0027B3`), `state_table.asm` (`$0027B4–$002803`)
- [x] Laço principal e dispatch: `main_frameloop.asm` (`$00FD24–$00FE1D`), `state_dispatch.asm` (`$00FE1E–$00FFAB`)
- [x] Handlers dos estados 0–13 (`$002804–$00397F` em `state_handlers1.asm`, `state1_handler2.asm`, `state_handlers2.asm`, `state_handlers3.asm`)

## Fila ativa — Fase 1 (`M1`)

- [x] Converter handlers dos estados 14–16 (`$003980–$004541`, 3.010 B) em `disasm/code/state_handlers4.asm`
- [x] Converter handler do estado 17 (`$004542–$0056EF`, 4.526 B) em `disasm/code/state_handlers5.asm`
- [x] Converter handlers dos estados 18–19 (`$0056F0–$006273`, 2.948 B) em `disasm/code/state_handlers6.asm`
- [x] Converter handler do estado 20 (`$006274–$00724F`, 4.060 B) em `disasm/code/state_handlers7.asm`
- [x] Converter handlers dos estados 21–22 (`$007250–$0081B5`, 3.942 B) em `disasm/code/state_handlers8.asm`
- [x] Converter handler do estado 23 (`$0081B6–$008D5F`, 2.986 B) em `disasm/code/state_handlers9.asm`
- [x] Converter handlers dos estados 24–25 (`$008D60–$009E53`, 4.340 B) em `disasm/code/state_handlers10.asm`
- [x] Converter handler do estado 26 (`$009E54–$00A253`, 1.024 B)
- [x] Converter handler do estado 27 (`$00A254–$00B56B`, 4.888 B)
- [x] Converter handlers dos estados 28–39 (`$00B56C–$00C09F`, 2.868 B)
- [x] Converter sub-rotinas do laço externo (`$002600–$002785`, 390 B)
- [x] Converter cluster de utilitários (`$00C09E–$00FD23`: input `$C870/$CA80`, alocador `$D4CC`, escalonador `$DB14–$DC4E`, VDP/tilemap)
- [x] Converter cluster de motor (`$00FFAC–$011ACD`) em `engine_ext.asm` e tarefas-núcleo (`$16F32`, `$172E6`, `$1FF00`)
- [x] Converter clusters restantes de tarefas/luta/IA (`$011ACE–$05E8A3` em `obj_tasks1..4`, `menu_system`, `ai_fighters1..3`, `fight_moves1..3`, `combat_core` + `$2FCD1A–$2FF437` em `replay_system`, totalizando `373.204 B` / `364,5 KB` montados e verificados; dados não-código permanecem como blocos `incbin`)
- [x] Mapear estrutura do lutador e estado da luta na RAM em `disasm/symbols/ram_map.toml` (dumps diferenciais GPGX)
- [x] Arbitrar divergência do pad 6 botões (1 press no estado 9) com BlastEm/Exodus *(Resolvido com `BlastEm 1.0.0` via PTY debugger `-g -d`: `$9376=$0101` e acordo 100% entre `BlastEm 1.0.0`, `GPGX`, `ares` e `Native` em `A`, `B1`/`C1→13` e `B2`/`B3`/`C2→1`; a diferença antiga era alinhamento de frame em `State 9` caindo na janela `FCDE(15)` pós-`$9B62`; ver [docs/main_loop.md](docs/main_loop.md))*
- [x] Mapear efeitos de HBlank/raster (VDP Reg 0 / Reg 11 per-line H-scroll) e atualizar `docs/PROGRESS.md`
- [x] Aceitar `gpgx_runner` como referência dinâmica oficial do `M1` e avançar para `M2`–`M6` / Fase 5

## Fase 2 — Recompilação Estática Nativa (`M2`)

- [x] Gerar `game/ec_code_addrs.txt` (68.700 endereços) e `game/ec_discovery.toml` via [tools/recomp/gen_recomp_config.py](tools/recomp/gen_recomp_config.py) (inclui tabelas `$06A522`, IAs, callbacks de menu, `bra_s`/`bra_w`/`pcrel16`, continuations inline/`YIELD_APIS` e VM `$010BFE`/`$010C6E`)
- [x] Implementar [game/game.toml](game/game.toml), [game/ec_spec.c](game/ec_spec.c) (ROM CRC32 `0x48F1A42E`, 6 botões habilitados em `ec_call_entry_point()`, telemetria, flag diagnóstica opt-in `EC_DIAG_BOSS_SANCTUARY` e carga `EC_HD_MANIFEST`) e [game/CMakeLists.txt](game/CMakeLists.txt)
- [x] Corrigir propagação de desempilhamento de corrotinas (`_entry_split_sp_popped`) em `segagenesisrecomp/external/m68k-recomp-core/profiles/genesis/code_generator.c` e compilar `build/native/EternalChampionsRecomp` (exit `0`)
- [x] Validar boot (`boot_600.ram`/`.vram`/`title.png`: Estado `$FD64=7`, `$FD70=532`, `$FD72=552`) e fluxo completo Título → Menu → Versus → Luta Ativa (Estados `14`, `17`, `18..27`, zero misses)

## Fase 3 — Validação Determinística (`M3`)

- [x] Criar suíte completa `.ecm` em [harness/movies/](harness/movies/) (`01`..`13` via [tools/harness/gen_roster_movies.py](tools/harness/gen_roster_movies.py): todos os 9 jogáveis, Eternal Champion e todas as 11 arenas + Holo-Trainer) com validação SHA-256 da ROM
- [x] Implementar [harness/ec_harness.py](harness/ec_harness.py) (autodeterminismo nativo, tradução `.ecm`→`--input-script`, diff WRAM 64 KiB vs `gpgx_runner` com símbolos de `ram_map.toml`, validação de estado/elenco e relatório [docs/frame_data/fight_probe_summary.md](docs/frame_data/fight_probe_summary.md))
- [x] Validar autodeterminismo nativo `0/65536` bytes + PNG idêntico e `0` misses (`floor_unsafe=0`, `dispatch_misses=0`, `interior_label_misses=0`, `floor_coverage=0`) nos 13 movies
- [x] Paridade determinística em 2 camadas (`M3`) em toda a suíte: **Camada 1** (`Recomp` × `GENESIS_FORCE_INTERP=1`, após corrigir `on_post_reset()` em `main.c`/`ec_spec.c` e unificar ciclos via `m68k_68000_nominal_cycles()` em `m68k_interp.c`): `12/12` (`01`..`12`) com `gameplay_match=True`, `png_eq=True`, `pad=0x0101`, `$FD72` exato e `65.534/65.536` B idênticos (apenas 2 B mortos do cursor de boot `VramStream` `$FFC8B7`/`$FFC8BB`); **Camada 2** (`Native` × `GPGX`): `13/13` autodeterminismo `0/65536` + `(0,0,0,0)` misses e `12/12` acordo de estado `$FD64`/elenco/pad (diff residual `640`–`2.216` B por leitura ao vivo de `$00C00008` em `Math_RandomRange` `$CB82`; `gvdp_read_hv_counter` mantido intocado — ver [docs/phase-3.md](docs/phase-3.md) e [docs/frame_data/fight_probe_summary.md](docs/frame_data/fight_probe_summary.md))

- [x] Corrigir travamento de fim de partida e recompilar o jogo `100%` nativo: `19` tabelas `bra_w` + slots/ponteiros de task isentos da heurística ASCII + callbacks `$36(a0)`/`$968E`/listas `bra.w` de menu em [tools/recomp/gen_recomp_config.py](tools/recomp/gen_recomp_config.py), e cobrança de ciclos de `jsr`/`bsr` antes do callee em `code_generator.c`. A Camada 1 anterior contava com o jogo parcialmente interpretado pelo floor; agora `16/16` `0/65536` B vs `GENESIS_FORCE_INTERP=1` e `17/17` zero misses (ver [docs/phase-3.md](docs/phase-3.md) §5)
- [x] Converter o Duff's device `$05E242` em switch interno (walker do gerador + probe/seeding por mnemônico em `code_generator.c`; `INTERIOR_UNRESOLVED = 0`) e caracterizar o resíduo de temporização de `f1143` (sem efeito em WRAM; correção depende do modelo de yield do engine)

## Fase 4 — Renderer `GameVideo`, Widescreen e Base Modo C (`M4`–`M6`)

- [x] Implementar [game/ec_video.h](game/ec_video.h) e [game/ec_video.c](game/ec_video.c) (Modo A padrão `320x224`, `--widescreen` opt-in com extensão viva de Planos A/B + Reg 11 H-scroll + VSRAM + CRAM nos estados `14, 15, 17..27`, pillarbox fora de arena e loader `EC_HD_MANIFEST` com fallback seguro)
- [x] Validar `--widescreen 16:9` via `ec_harness.py` em todas as 12 capturas de arena (`S14, S17, S18..S27` em `398x224`, margens ativas preenchidas, Title `S7` em pillarbox `0/8736` e `0/65536` bytes divergentes na WRAM vs `320x224`)
- [x] Documentar política por estado e tabela completa em [docs/widescreen.md](docs/widescreen.md)
- [x] Protótipo neural B+ (`N1`): destilação PyTorch CUDA em [neural/train_espcn.py](neural/train_espcn.py) (`ECStudentESPCN4x` `82.344` params, `PSNR=23,273 dB`, `SSIM=0,8869`, `AlphaIoU=0,9996`, latência `fp16 320x224` `p50=1,1924 ms / p99=1,7751 ms`, checkpoint em `assets/models/ec_bplus_espcn4x.pt`)
- [x] Export ONNX (`N2`) e integração em runtime `P3` (`N3`) *(**CONCLUÍDOS**: [neural/export_onnx.py](neural/export_onnx.py) → `ec_bplus_espcn4x.onnx` `opset 17` + pack `ECBP` v1 `ec_bplus_espcn4x_weights.bin` `329.408` B `det=0.0` `PSNR≥144,48 dB`; runtime C sem dependências via OpenGL 4.3+ Compute Shaders em [neural/infer/ec_bplus_gl.c](neural/infer/ec_bplus_gl.c) + [game/ec_video.c](game/ec_video.c) com `--neural-bplus`/`EC_NEURAL_BPLUS=1`, paridade `PSNR=78,603 dB` em `1280×896` e `62,973 dB` em widescreen `1592×896`, `0/65536` WRAM diff, `p50=5,34 ms / p99=6,73 ms` no WSL2 Mesa D3D12 e fallbacks verificados — ver [docs/phase-4.md](docs/phase-4.md))*

## Fase 5 — Pipeline de Assets HD (`M7`) e Fase 6 — Sega CD (`M8`)

- [x] Expandir extrator [tools/extract/extract_assets.py](tools/extract/extract_assets.py) (CRAM `$FFFF9BB0`, SAT, tiles/planos A e B deduplicados por SHA-256 + mapas de índice em `assets/catalog/indices/` + `--preserve-hd`) para todas as capturas (`6.135` entradas: `410` sprites, `5.698` tiles, `27` planos)
- [x] Validar carga em runtime com `EC_HD_MANIFEST=pipeline/manifest.json` (`120` quadros com `--benchmark 120`: `6135 entradas (0 approved, fallback=Modo A/B)`, exit `0`)
- [x] Pipeline ComfyUI HD (`pipeline/workflows/` + [tools/pipeline/comfy_upscale.py](tools/pipeline/comfy_upscale.py) + [tools/pipeline/review_viewer.py](tools/pipeline/review_viewer.py)): `107` assets 4× gerados via ComfyUI `127.0.0.1:8188` (`27/27` planos + `80` sprites representativos do elenco, `min_iou >= 0.9820`) e galeria lado a lado em `assets/review/index.html`
- [x] Curadoria e aprovação manual (`status="approved"`) dos `437` assets + retreino B+ + modos extras (`2P VS`, `Overkill`, `Tournament`) *(**`M7` CONCLUÍDO**: `437/437` aprovados + retreino B+ (`M7.2`); **`M7.3`**: `.ecm` com 2ª coluna (pad 2) e movies `14_2p_vs_rax_vs_blade_s24`, `15_tournament_roundabout_jetta_vs_xavier_s27` (`$9424=3`, arena sorteada) e `16_2p_vs_match_end_s24_to_s1` (KO final → `WINS COMPETITION` → Estado `1`); Overkill mapeado e confirmado em `17_2p_vs_overkill_trident_s24` (`$9C7C=1`); suíte `17/17` verde — ver [docs/phase-5.md](docs/phase-5.md))*
- [ ] Expansão Sega CD / *Eternal Champions: Challenge from the Dark Side* (`M8`) *(ADIADA por decisão de escopo até concluir o cartucho Genesis; dump `.chd` e pack completo de BIOS US/EU/JP já estão em `roms/`)*

## Fase 7 — Áudio (`M9`)

- [x] Corrigir silêncio do recomp nativo: **Timer A do YM2612 não era emulado** no barramento ([segagenesisrecomp/runner/video/genesis_bus.c](segagenesisrecomp/runner/video/genesis_bus.c)/[genesis_bus.h](segagenesisrecomp/runner/video/genesis_bus.h)) — só Timer B (RKA). O driver Z80 do Eternal Champions programa `$24/$25` (TA=1019, TAL=5) e escreve `$27=$15` (reset flag A + habilita flag A + load/run A), depois gira em `bit 0,(hl)` no status `$4000` aguardando o overflow do Timer A — flag que nunca era setada, então o Z80 parava em `$0287` e o som morria após o frame 245. Emulação adicionada (período = `(1024−TA)×1008` ciclos master, 1 decremento por amostra FM — mesma cadência do oracle GPGX `INTERNAL_TIMER_A`), com load/run em `$27` bit 0 e flag gate em bit 2; `rb_state.c` atualizado (asserts de tamanho 24688/90600). **Validado**: WAV bit-identical entre runs (`md5` igual), fluxo de 48 s boot→título→menu→luta com áudio contínuo (DAC PCM + música, RMS 460–2150, pico 10.688), suíte completa `17/17` verde (`det=0/65536`, `gp_ok=True`, PNG iguais, Camada 2 vs GPGX inalterada na classe de ruído HV `$C00008`). **Nota honesta**: Camada 1 (Recomp × `GENESIS_FORCE_INTERP=1`) do movie `08` passou de `0/65536` para `4/65536` (estável, só nos últimos 100 quadros do fim de luta; `$FFAAFB`+`$FFE897-99`): com o Z80 vivo, o handshake CommFlag/Ack dentro do VINT agora depende do interleave 68k×Z80 — o handler interpretado roda atômico (`m68k_interp_run_handler` não faz budget-yield) enquanto o recompilado intercala com o Z80 por acesso de barramento; retries diferentes → fase de raster diferente → leituras HV divergentes no RNG do fim de luta. Era mascarado pelo Z80 congelado. Gameplay inalterado (PASS/png_eq/HP iguais). *Opcional futuro: budget-yield no handler interpretado para zerar esses 4 bytes.*
- [ ] Extrair músicas e efeitos sonoros (trilhas do driver Z80 via YM2612/SN76489 + amostras PCM DAC) para remasterização — ainda não feito

## Fase 8 — Controles e Apresentação (`M10`)

- [x] Mapeamento padrão do **controle XBOX** ([segagenesisrecomp/runner/input_map.c](segagenesisrecomp/runner/input_map.c)): layout posicional estilo Sega — Genesis A/B/C (os três botões de ataque do Eternal Champions) em face X/A/B, Genesis X em Y, Y/Z nos ombros LB/RB, Mode em Back, Start em Start (P1 e P2); antes era o mapeamento "ajustado para PlayStation". Funciona plug-and-play (defaults compilados; sem `settings.ini` para sobrescrever)
- [x] Intensidade do efeito **B+** (upscaler neural ESPCN 4× via Compute Shaders OpenGL): novo `--bplus-intensity <f>` / `EC_BPLUS_INTENSITY` ([neural/infer/ec_bplus_gl.c](neural/infer/ec_bplus_gl.c) + [game/ec_video.c](game/ec_video.c) + [game/ec_spec.c](game/ec_spec.c)) — escala o resíduo neural sobre a base bilinear (`result = clamp(base + delta × intensity)`): `0.0` = bilinear puro, `1.0` = saída exata do modelo (padrão), `>1` amplifica o detalhe (ex.: `1.5`–`2.0` para efeito mais forte); clamp `[0,4]`. **Validado** no WSL2 (`GALLIUM_DRIVER=d3d12`, GL 4.6 Mesa, `fallback=0`, capturas com md5 distintos entre `0.0` e `3.0`) e `ec_harness --quick` verde
- [ ] *Futuro:* camada de mapeamento de controles amigável (ou menu in-game) em vez de editar `settings.ini`/defaults
