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
- **Pista:** carregamento/timing — os assets do boss são carregados sob
  demanda e no round 1 do Trident o carregamento perde a corrida contra o
  primeiro frame da luta.
- **Observação 2026-11-21 (nova):** capturas de VRAM/SAT no início do
  round 1 (f2950+) do cenário VS Trident com EC (P2=9, forma base): os
  tiles do EC (faixa 384–605) já estão carregados e idênticos ao estado
  "assentado" desde o primeiro frame — a corrida **não reproduz** no modo
  headless com a forma base. SAT idêntico entre round 1 e round 2 (a única
  diferença entre rounds é a cinemática de KO). A reproduzir com as
  variáveis do usuário: forma da fera (1–4), mirror match EC×EC, modo
  interativo (timing real) ou assets HD (`--hd-manifest`).
- **Referência:** `game/ec_boss.c`, arena Trident `$8D60`
  (`state_handlers10.asm`); repros em `tools/scripts/t4_*` (`/tmp/opencode`).
- **Status:** em investigação.

### [~] 1º load do savegame às vezes trava (carregar 2–3× libera)
- **Sintoma:** ao carregar um save (V2 `ECRB2sav`) pela primeira vez na
  sessão, o jogo pode travar; carregar 2–3 vezes "libera" e aí funciona.
- **Teoria do usuário:** o save é gravado incompleto, faltando informações;
  ao carregar, o jogo não sabe como reconstruir o restante. Deveria haver um
  ciclo de carregamento das funções do controle, recarregando sprites,
  cenário e música do save.
- **Estado técnico:** o blob V2 é um snapshot de rollback completo e
  validado byte a byte (round-trip com resíduo de 84 bytes de ruído HV,
  classe documentada e aceita). O load é in-place com re-entrada em
  `dispatch_main_loop_pc = 0x255E` e A7 preservado. Suspeitas a investigar:
  (a) pending exec/handlers no 1º load a frio; (b) estado do áudio/Z80 não
  restaurado na primeira passada; (c) ordem de `cancel_load_hold` ×
  `audio_discard_playback` no caminho de 1º load.
- **Referência:** `segagenesisrecomp/runner/main.c`
  (`runner_save_state_file`/`runner_load_state_file`, magics
  `GROWNS3`/`ECRB2sav`).
- **Status:** a investigar.

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

### [~] Training: tela preta ao terminar (após 1 round)
- **Sintoma (reportado):** no Training Mode, ao terminar a disputa a tela
  fica **preta** após 1 round. **Reproduzido deterministicamente** no
  Holo-Trainer off-label (VS bg 10 → estado 14, `ko_holo`): KO → RAM vai
  ao estado 1 (menu) mas o render fica 100% preto; o fluxo original
  (menus → estado 17) renderiza normalmente.
- **Diagnóstico (2026-11-21, 3ª rodada — causa raiz em 2 camadas):**
  1. **Tabela de ciclos do interpretador (FIXADO):** o interp
     (`GENESIS_FORCE_INTERP=1`) cobrava `m68k_68000_nominal_cycles`
     (estimativa PRM) por instrução, enquanto o recomp usa a tabela
     **medida** emitida pelo recompilador (`<game>_cycles.c`, gerada mas
     **nunca compilada** — faltava em `_sources` do
     `GenesisRecompGenerated.cmake`; `game_cycles.c` também não linkava).
     O drift de `g_audio_cycle_counter` dessincronizava o scheduler de
     tasks do menu (~85 frames após entrar no estado 1 o VINT caía em
     pontos diferentes do stream de dispatch — task da cascata de layout
     disparava 2 wall frames depois; 5235 no interp × 5237 no recomp).
     Fix: tabela medida habilitada no shipping (o gate `GENESIS_COSIM` foi
     removido — a mudança era a "validated pacing change" já prevista no
     comentário do código). Validado: contador da cascata (`$9CCF`) em
     sincronia no fim do fluxo; suíte base segue verde.
  2. **Upload de CRAM do menu (diagnosticado, pendente):** mesmo com as
     tasks em sincronia, o CRAM do menu fica **todo zerado** no recomp
     (paleta nunca sobe → tela preta). Escrita a porta de dados do VDP
     medida nos dois lados (write-log TCP): o handler do estado 1 faz
     `0x8134` (display off) + comando CRAM `0xC000` + dados + `0x8174`
     em ambos; o **fade de paleta** (Pal_FadeOut → registro tipo 3 em
     `$F136` → `Sub_StateMachine` `$D7E4`, subsistema bit4 do frame loop)
     escreve a CRAM cor a cor (~1 byte/frame) só no interp (f5133–5157);
     no recomp o fade nunca escreve. O burst de tiles (DMA via fila VDP
     `$F620`/drain `$CD74`) acontece nos dois lados mas **25 frames
     deslocado** (5167 no recomp × 5192 no interp) — o wall-frame slicing
     (chunk por scanline) dos dois backends ainda difere. Conclusão: a
     dessincronia restante é da mesma classe de engine (posição do
     wall-frame/scanline no momento das escritas de VDP), mais profunda
     que a tabela de ciclos.
- **Próximo passo (2a camada):** comparar o slicing/chunk do interp com o
  do recomp (o interp drena o orçamento por instrução via
  `glue_check_vblank`/safety-net de 2 frames; o recomp por acesso de bus)
  e alinhar o cadence, ou contornar com hook opt-in (ex.: forçar o fade
  do menu a não depender do slicing). Artefatos: `t2_vdpwlog.py`,
  `t2_logcmp.py`, logs em `/tmp/opencode/t2nat2/`/`t2int2/`.
- **Artefatos:** repros em `tools/scripts/t2_repro/`
  (`t2_holo_shots.input`, `t2_dv.input`, `t2_win_to_1.input`,
  `t2_nav5.input` + probes `t2_probe.py`, `t2_regdiff.py`, `t2_peek.py`,
  `t2_cram.py`, `t2_vdpwlog.py`, `t2_logcmp.py`, `t2_writelog.py`).
- **Referência:** tasklist T2 em `docs/proposta_bugfix_2026-11-21.md`;
  `state_handlers4.asm` `$3BE8` (exit do estado 14), `$C578`/`$C6A4`
  (cadeia de decisão pós-partida), `util_alloc_vdp.asm` `$D6A0`
  (VBlank_VdpUpdate) e `$D7E4` (Sub_StateMachine/fade),
  `irq.asm` `$6B6` (handler de VBlank).

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
