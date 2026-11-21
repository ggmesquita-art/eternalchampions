# BUG_QUEUE.md — Fila restante de bugs e pendências

> Documento vivo da fila de bugs do EternalRecomp (complementa
> `PROGRESS.md` §11). Atualizado em 2026-11-21.
> Convenção: `[ ]` aberto · `[~]` em investigação · `[x]` resolvido
> (mantido por rastreabilidade, com a data da resolução).
> Tasklist do ciclo atual (2ª rodada): [docs/proposta_bugfix_2026-11-21.md](proposta_bugfix_2026-11-21.md) (T1–T4).

## 1. Bloqueadores de gameplay

### [~] Sprites do EC falham nos DOIS ECs — só no round 1 do Trident
- **Sintoma:** jogando com o Eternal Champion (modo `--unlock-boss`), os
  sprites dos dois ECs aparecem quebrados/ausentes **apenas no round 1** na
  arena do Trident (estado 24). Nos rounds seguintes os sprites carregam.
- **Condições confirmadas (2026-11-21):** todas as formas da fera
  (base+1–4), mirror EC×EC (os dois ECs), pior em **movimentos aéreos**;
  independente de `--hd-manifest`.
- **Progresso do repro headless (2026-11-21):** infra em
  `tools/scripts/t4_repro/` (`t4_gen.py`, `t4_cursor.py` — variantes do
  movie 17 + probe do id via ASSERT). Descobertas:
  1. Poke direto de `$9CBE`/`$9CBC` (ids) **não funciona**: o setup da
     luta re-deriva os ids da seleção do menu (`jsr $CB82` em `loc_02E4FC`,
     `move.b $9367 → $9CBC`) e o jogo **trava na tela de loading** quando
     os ids do chefe são pokados sem o menu tê-los selecionado.
  2. O char-select do VS (com `--unlock-boss`) responde só a UP/DOWN na
     janela f2080–2200: UP4 volta a Rax (ciclo de 4), UP1–3/5–9 = 0,
     LEFT/RIGHT ignorados — o unlock patcheia a ROM (clamp 9→11, roster
     relocada) e o layout muda. Falta mapear a tabela do roster patchado
     para achar o slot do EC.
- **Próximo passo:** decodificar o roster patchado (`ec_boss.c`
  `apply_rom_patches`) e o handler do select (`menu_system.asm` — bitmap
  `$9B40`, índice `$9B4E`, `$9367`), mapear o cursor ao EC e gerar o
  script mirror completo (mash aéreo + `DUMP_VRAM`; tiles do EC na faixa
  384–605 base `$2000`, SAT em `$1400`).
- **Referência:** `game/ec_boss.c`, arena Trident `$8D60`
  (`state_handlers10.asm`), `menu_system.asm` `$02E190`–`$02E4FC`/`$CB82`.
- **Status:** em investigação.

### [~] 1º load do savegame às vezes trava (carregar 2–3× libera)
- **Sintoma:** ao carregar um save (V2 `ECRB2sav`) pela primeira vez na
  sessão, o jogo pode travar; carregar 2–3 vezes "libera" e aí funciona.
- **Direção do usuário (aplicada em 2026-11-21):** "o state não carrega
  completamente ou sobrescreve irregularmente; o correto é **pausar o
  sistema todo antes de carregar** e aplicar o load".
- **Implementação (commit no segagenesisrecomp):** os loads interativos
  (atalho hold do pad/RB e o modal F7/F8) agora são **staged**: em vez de
  aplicar o swap no meio do loop (o fiber do jogo pode estar yield-ed em
  um site que não é o vblank-wait — BUDGET/SPIN/Z80POLL — e o resume
  in-place sai do ponto errado), o caminho é guardado em
  `s_pending_load_path` e aplicado no **ponto seguro pós-frame** (junto ao
  tick do input script), onde o sistema inteiro está estacionado entre
  frames — o mesmo ponto do `LOAD_STATE` de script, já validado headless.
  Caminho do modal (`local_states.c`) idem via `runner_stage_load_state`.
- **Validação headless (scripted, inalterado):** segue sem hang — 1º e 2º
  load a frio avançam; o estado a +60 frames difere em 73 bytes (classe
  RNG/HV já documentada — antes 0 por coincidência de timing; o fix do
  rts do T2 deslocou o drift). Quick suíte verde.
- **Pendente:** validação interativa com o usuário (o hang não reproduz
  headless). Se persistir, o próximo passo é endurecer
  `glue_prepare_inplace_load` (exigir `s_yield_site == GLUE_YIELD_VBLANK`
  para o in-place; senão, cold-restart `glue_restart_game_fiber_keep_a7`).
- **Referência:** `segagenesisrecomp/runner/main.c`
  (`runner_load_state_file`, `runner_stage_load_state`,
  `s_pending_load_path`), `local_states.c`, `rb_state.c` (blob GRB2 com
  seções exec/cpu/sched/ram/machine/fm/psg/evq/input/game — pendentes do
  recomp como `rte_*`/`split_sp` incluídos na seção `sched`),
  `load_hold.c`.

### [x] ~~Vencer round 1 no cenário do EC (VS) → volta ao menu inicial~~ — RESOLVIDO (2026-11-21)
- Causa raiz e fix: itens 8–9 da tabela de resolvidos abaixo. O fluxo natural
  do boss (estado 18 → KO → estado 8 arte → estado 6 → estado 4 → estado 10
  ending) agora segue intacto quando há EC no elenco; o hijack para o menu
  ficou restrito ao VS off-label **sem** EC. Hooks `$321A`/`$3662` mapeiam
  START dos pads de luta para `$FD61` bit7 nas telas de arte (VS 2P).
- **Observação VS 2P (a validar visualmente):** a cena do estado 6 em VS é
  uma intermissão estática (~20 s, sem resposta a input) que auto-avança
  para o ending; no modo 1P o fluxo roda como no original (EC controlado
  pela CPU).

### [✅] Training: tela preta ao terminar (após 1 round) — RESOLVIDO (2026-11-21)
- **Sintoma (reportado):** no Training Mode, ao terminar a disputa a tela
  fica **preta** após 1 round. Reproduzido deterministicamente no
  Holo-Trainer off-label (VS bg 10 → estado 14, `ko_holo`): KO → RAM vai
  ao estado 1 (menu) mas o render fica 100% preto; o fluxo original
  (menus → estado 17) renderizava normalmente.
- **Causa raiz (FIXADA no recompilador):** o padrão artesanal de
  link/unlk do ring buffer do Z80 (`Z80_RingRead` `$07E4`/`Z80_RingEpilogue`
  `$0810`: `movea.l (a7)+,a0` + `link a4` + re-push + `rts`, com os pops
  do epílogo na continuação tail-call do caller). O `rts` gerado NÃO
  desempilhava (a convenção deixava o pop para o wrapper do JSR do
  caller) e o wrapper só compensava quando `SP != slot+4` — para esse
  padrão o epílogo restaura `SP == slot+4` **sem consumir o retorno**, o
  wrapper não compensa e **vaza 4 bytes de pilha por comando do ring**.
  No burst de comandos da transição KO→menu (wf 5237, 9+ chamadas de
  `$84C`/`$8FA` em loop), a pilha desce até as slots de registradores
  salvos do frame loop → o `movem` de restauração recarrega **A0=0 e
  A6=0** → o handler de VBlank escreve a tabela de DMA em `$000004` em
  vez de `$C00004` → o upload de CRAM (shadow `$F0B6` → CRAM, todo
  frame) nunca mais acontece → CRAM fica preta (o fade inicial do menu
  zera a CRAM e o fade-in só atualiza a shadow) → tela 100% preta.
  Localizado com: `GENESIS_STRICT_JSR_STACK` (mismatches em cascata
  wf 5237), trace de SP no loop (`RINGSP`), bisect de A6 frame a frame
  via `owner_review step`, e o write-log de RAM/porta VDP.
- **Fix:** `m68k-recomp-core` `2666e9f` — `rts` hardware-fiel
  (`g_cpu.PC = read32(A7); A7 += 4`) + wrapper que só compensa quando
  `SP == slot` (retorno intacto). Regenerado + suíte 17/17 verde
  (`det=0`, `png_eq=True`, `gp_ok=True`). Validação do T2: CRAM 33/64
  com a paleta exata do menu, A6=C00000, screenshots finais 24 cores
  (antes 1 cor/preto). Movie 08 interp residual 4→55 bytes (mesma classe
  RNG/stack; `gameplay_match=True`).
- **Artefatos:** repros em `tools/scripts/t2_repro/` (`t2_holo_shots.input`,
  `t2_dv.input`, probes `t2_probe.py`, `t2_regdiff.py`, `t2_peek.py`,
  `t2_cram.py`/`t2_cram3.py`, `t2_vdpwlog.py`, `t2_logcmp.py`,
  `t2_cramscan*.py`, `t2_writelog.py`, `t2_a6bisect.py`, `t2_alive.py`,
  `t2_pcfreeze.py`); trace env-gated do VDP (`GENESIS_VDP_TRACE=FROM-TO`)
  preservado em `genesis_vdp.c`.
- **Referência:** tasklist T2 em `docs/proposta_bugfix_2026-11-21.md`;
  `irq.asm` (`Z80_RingRead`/`Z80_RingEpilogue` `$07E4`/`$0810`),
  `state_handlers4.asm` `$3BE8` (exit do estado 14),
  `util_alloc_vdp.asm` `$D6A0` (VBlank_VdpUpdate), `$D7E4`
  (Sub_StateMachine/fade).

### [ ] Finalizador da Jetta — bug reportado, a reproduzir
- **Sintoma:** problema com o finalizador (Overkill) da Jetta — detalhes a
  reproduzir com movie de KO contra a Jetta.
- **Referência:** Overkill mapeado em `$5E4B6` → `$269E4` → handler por
  arena, flag `Fight_OverkillActive` `$9C7C` (ver `PROGRESS.md` §10 item 4
  e `docs/phase-5.md`); movie de referência `17_2p_vs_overkill_trident_s24`.
- **Status:** a reproduzir.

## 2. Bugs já resolvidos (rastreabilidade)

| # | Bug | Causa raiz | Resolução | Data |
|---|---|---|---|---|
| 1 | Save/load travava / corrompia | Formato antigo com seções exec/input não-portáveis | **Save/load V2** `ECRB2sav`: blob portátil do rollback sem exec/input, load in-place, A7 preservado, re-entrada `0x255E` | 2026-11-21 |
| 2 | Input fantasma após load | Estado de input (pads/bordas) reutilizado do frame anterior ao load | Restauração limpa do estado de input no load in-place | 2026-11-21 |
| 3 | Reset não zerava tudo | Reset morno reutilizava RAM | **F10 = cold reset**: RAM zerada + `machine_init` + SRAM preservado + boot do vetor de reset; diretiva `COLD_RESET` no input-script | 2026-11-21 |
| 4 | Hang do valor 10 no menu de Background | A 10ª entrada da lista off-label caía sobre a string "Random" (bytes lidos como ponteiro inválido) | Lista realocada para ROM livre em `$257051` (12 entradas + strings "HOLO-TRN"/"SANCTUARY") | 2026-11-21 |
| 5 | Bug global de sprites (intro + cenários) | Lista off-label em RAM `$FFCF3C–$FFCF7E` pisava em records de sprites do jogo (sentinel confirmou conflito) | Lista movida para a ROM (defs `$2E8B0`/`$2EC7E` repontadas) | 2026-11-21 |
| 6 | Fim de partida do santuário off-label → endgame/travamento | Handler do estado 18 grava `$9CB6=8/10` (finalização do boss) ao fim da luta VS | Hooks nos RTS `$59E6`/`$59EE` (return 0) sobrescrevem `$9CB6=1` quando `$936F==11` | 2026-11-21 |
| 7 | Hook return 1 em pontos de fall-through travava | `return 1` abandona a função nativa e quebra o unwind | Hooks reposicionados em RTS com `return 0` | 2026-11-21 |
| 8 | Cenário do EC (VS/1P com EC no elenco) → menu após o 1º round | O jogo grava `$936F=11` durante a luta do santuário; o guard `$936F==11` dos hooks de `$59E6`/`$59EE` não distinguia a luta do chefe e hijackava a saída natural (`$9CB6=8` → estado 8, arte/transformação) para o menu | Guard ampliado: hijack só quando **não há EC no elenco** (`$9CBC!=9 && $9CBE!=9`); fluxo do boss (18→8→6→4→10, tela de ending) segue intacto | 2026-11-21 |
| 9 | Telas de arte do fluxo do boss (estados 8/10) não avançavam em VS 2P | Os wait loops esperam bit7 de `$FD61`, que só a cadeia de input de menu seta (modo 1P); em VS o START chega só nos pads de luta `$FD59`/`$FD5B` | Hooks em `$321A`/`$3662` (antes do `btst.b #7,$FD61`) mapeiam START dos pads de luta → `$FD61` bit7 | 2026-11-21 |

## 3. Limitações conhecidas (não são bugs do jogo)

1. **Ruído HV no round-trip de RAM do save/load:** 84 bytes divergentes por
   leitura ao vivo de `$00C00008` (HV-counter) em `Math_RandomRange`
   (`$CB82`) — classe documentada e aceita (ver `docs/phase-3.md`).
2. **Camada 1 do movie 08:** `4/65536` bytes divergentes (Recomp ×
   `GENESIS_FORCE_INTERP=1`) só nos últimos 100 quadros do fim de luta —
   com o Z80 vivo, o handshake CommFlag/Ack no VINT depende do interleave
   68k×Z80; o handler interpretado roda atômico. Gameplay inalterado.
   *Opcional futuro: budget-yield no handler interpretado.*
3. **Movies `.ecm` de KO/me dessincronizam no build atual** quando a
   sequência depende de sorteio por HV-counter (RNG aleatório no jogo
   original). Os movies `ko_sanc`/`ko_holo`/`v3_*` foram construídos como
   input-scripts com dumps a cada 100 frames para contornar isso.
4. **Latência B+ OpenGL (`p50≈5,3–8,1 ms` no WSL2 Mesa D3D12)** a revalidar
   em Windows nativo (meta `≤ 2,0 ms` de `ROADMAP.md §11.5`; referência
   PyTorch CUDA nativa `p50=1,19 ms`).

## 4. Pendências de escopo (não-bugs)

- [ ] **Movelist:** extração sequencial dos golpes começando pelo EC
  (formato bruto markdown em `docs/movements/`, ver `README.md` local).
- [ ] **Áudio:** extração das músicas/efeitos (trilhas do driver Z80
  YM2612/SN76489 + amostras PCM DAC) para remasterização — captura
  (`GEN_DEV_TRACE`) e conversor `chip_ring_to_vgm.py` prontos
  (`PROGRESS.md` §10.5).
- [ ] **Expansão Sega CD (`M8`)** — adiada por decisão de escopo; `.chd` e
  BIOS já em `roms/`.
- [ ] **Latência/qualidade B+ em Windows nativo** — revalidação.
