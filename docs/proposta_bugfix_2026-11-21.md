# Proposta — Ciclo de bugfix 2ª rodada (2026-11-21)

> Tasklist dos 4 bugs pendentes da sessão anterior. Complementa
> `docs/BUG_QUEUE.md` (fila viva) e `docs/PROGRESS.md` §11.
> Princípio mantido: jogo base 100% autêntico; todos os desvios são
> opt-in via `--unlock-boss` / `EC_UNLOCK_BOSS=1`.

## Panorama dos fluxos relevantes (fatos verificados até aqui)

- Estados: 13 = menu, 14 = Holo-Trainer/Training, 18 = Santuário do EC,
  19–27 = arenas normais (24 = Trident). `$FD64` = estado atual,
  `$9CB6` = próximo estado pedido (consumido pelo boot loop `$22E6–$231E`
  via `$246E`→`$24E0`→`$FE74`), `$255E` = `Main_OuterLoop`.
- Exit do estado 18 (`state_handlers6.asm` `$59B6–$59F0`):
  `$9350==1 → $9CB6=8` (finalização) · `$9352!=0 → $9CB6=10` ·
  senão `$9352++`, `$9CB6=18` (re-entra no santuário).
- Exit do estado 14 (`state_handlers4.asm` `$3BE8`): grava
  `$9CB2=4` / `$9CB6=1` **incondicionalmente** após o `Main_OuterLoop`.
- Match-end compartilhado (`obj_tasks3.asm` `$1EF88–$1F0A8`): os estados
  **14 e 18** caem no mesmo caminho especial (`cmpi $12/$E` → `$1EF9A`);
  KO faz `$9350++` (`$1EFCA`/`$1F04A`) e `subq.w #1,$9352` (`$1F028`).
  `$9CB0` (0=1P/2=VS/4=training) e `$9424` (tipo de torneio) desviam o
  fluxo. Estado 15 grava `$9352=2` no setup.
- Hooks atuais do EC (`game/ec_boss.c`, 26 sites): `$59E6`/`$59EE`
  sobrescrevem `$9CB6=1` quando `$936F==11` (fim de partida do santuário
  off-label); `$1F19A` é pulado quando `$9CBE>=9` ou `$9CBC>=9`
  (EC no elenco); blobs de forma/animação em `k_form_blob[]` via
  `$043C12`/`$043C76`/`$043C5A`/`$043CDC`.

## Tasklist

### T1 — Cenário do EC: volta ao menu inicial após o 1º round — ✅ RESOLVIDO (2026-11-21)

- **Causa raiz (verificada em dumps):** o jogo grava `$936F=11` durante a
  luta do santuário (índice interno da arena); o guard `$936F==11` dos
  hooks de `$59E6`/`$59EE` (fix do fim de partida off-label) não
  distinguia a luta do chefe e sobrescrevia `$9CB6=8` (estado 8, tela de
  arte/transformação) com `$9CB6=1` (menu) no 1º KO — repro determinística
  em VS 2P santuário com EC (`$9350: 0→1`, `FD64: 18→1`).
- **Fix:** guard `$9CBC!=9 && $9CBE!=9` nos hooks `$59E6`/`$59EE`
  (hijack só no VS off-label sem EC). Fluxo natural validado:
  `18-KO → 8 (arte) → 6 → 4 → 10 (ending) → reset`, com hooks `$321A`/
  `$3662` mapeando START dos pads de luta (`$FD59`/`$FD5B`) para bit7 de
  `$FD61` nas telas de espera dos estados 8/10 (em VS a cadeia de input
  de menu não alimenta `$FD61`).
- **Validação:** cadeia completa headless com input real (sem pokes de
  estado); kosanc (sem EC) segue → menu; suíte `17/17` verde
  (`det=0/65536`, `interp_diff=0/65536`, `png_eq=True`).

- **Sintoma (reportado):** luta com o EC funciona, mas ao vencer o
  primeiro round o jogo volta ao menu principal (antes da tela de
  seleção de 2P), em vez de seguir para o round 2.
- **Hipóteses (a verificar):**
  - H1: o hook `$59E6`/`$59EE` dispara já no 1º KO — `$9350` vira 1 na
    primeira vitória e a lógica do estado 18 trata 1 vitória como fim
    (regra do boss); no VS off-label o match deveria ser melhor-de-3.
  - H2: a "regra de boss" (fighter id 9 → 1 vitória encerra) vaza para o
    VS 2P por outro ponto além do `$1F19A` já hookado (ex.: `loc_01F15C`
    compara `$9CBE` com `$9350`).
  - H3: o elenco/rounds do VS no santuário não é rearmado entre rounds
    (`$9352` não volta a 2), então o 2º round nunca acontece.
- **Passos:** (1) repro determinístico com movie/input-script + dumps de
  RAM a cada N frames (metodologia dos `ko_*`); (2) watch de writes em
  `$9CB6`/`$9350`/`$9352`/`$FD64` via debug server (frame ring 600);
  (3) mapear `loc_01F15C`/`loc_01F19A` e o caminho `$1F024–$1F0A8`
  round 1 × round 2; (4) comparar com arena normal (estado 24) no mesmo
  momento do fluxo.
- **Fix candidato:** gate do hook por "match realmente acabou" no
  off-label (ex.: só forçar `$9CB6=1` no KO final; nos KOs intermediários
  forçar a re-entrada do estado com round intacto), mantendo o fluxo
  original de rounds.
- **Validação:** movie novo de VS santuário com 2 rounds completos
  (round 1 → round 2 → menu só no final); suíte `17/17` verde;
  confirmação manual.
- **Referências:** `game/ec_boss.c` (hook `$59E6`/`$59EE`/`$1F19A`),
  `disasm/code/state_handlers6.asm` `$59B6–$59F0`,
  `disasm/code/obj_tasks3.asm` `$1EE40–$1F0A8`, `docs/PROGRESS.md` §11.3.

### T2 — Training: tela preta ao terminar (após 1 round) — 🔬 DIAGNOSTICADO (2026-11-21)

- **Sintoma (reportado):** no Training (original e/ou off-label), ao
  terminar a disputa a tela fica preta após o 1º round. Na fila já consta
  que vencer no Training Mode original pode levar à finalização
  (`BUG_QUEUE.md` §1.3).
- **Hipóteses (a verificar):**
  - H1: o caminho de match-end dos estados 14/18 grava `$9CB6=8/10`
    (finalização) antes do exit `$3BE8` do estado 14, e o estado
    8/10 não inicializa o vídeo no contexto do training (preto).
  - H2: o KO no training muda `$FD64` por via direta (sem `$9CB6`),
    pulando o exit normal do handler.
  - H3: diferença de setup entre o training original (`$9CB0=4`,
    `$9422`/`$9424`) e o off-label Holo (já validado headless com
    `ko_holo`: 14 → 1).
- **Passos:** (1) repro do training original até o fim do round com
  dumps; (2) quem escreve `$9CB6`/`$FD64` nos últimos frames; (3) diff
  de fluxo training original × off-label Holo; (4) checar init de vídeo
  dos estados 8/10 no contexto do training.
- **Fix candidato:** hook no exit do training original forçando
  `$9CB6=1` (menu) com guard correto (`$9CB0==4` e/ou arena do training),
  ou correção do match-end para não gravar finalização no training.
- **Validação:** movie de training original completo terminando no
  estado 1 sem tela preta; suíte verde.
- **Referências:** `disasm/code/state_handlers4.asm` `$3BE8`,
  `disasm/code/obj_tasks3.asm` `$1EF88–$1F0A8`,
  `docs/BUG_QUEUE.md` §1.3.

### T3 — Savegame: 1º load às vezes trava (2–3 loads "liberam") — ⏳ não iniciado nesta rodada

- **Sintoma:** o 1º load de um save V2 (`ECRB2sav`) na sessão pode
  travar; repetir o load 2–3× libera. Teoria registrada: save gravado
  incompleto → o jogo não reconstrói o resto no load.
- **Hipóteses (da fila):** (a) pending exec/handlers no 1º load a frio;
  (b) estado de áudio/Z80 não restaurado na 1ª passada; (c) ordem
  `cancel_load_hold` × `audio_discard_playback` no caminho do 1º load.
- **Passos:** (1) repro headless com input-script (diretivas de save/load)
  gravando o estado pós-load de cada tentativa; (2) diff 1º × 2º load
  (PC, regs, Z80 RAM, estado do áudio, `$FD64`); (3) auditar
  `glue_prepare_inplace_load`/`glue_finish_inplace_load` e a restauração
  do Z80/áudio na 1ª passada; (4) instrumentar o caminho
  `runner_load_state_file` (`segagenesisrecomp/runner/main.c`).
- **Fix candidato:** rearmar áudio/Z80 antes do 1º load (mesma ordem do
  caminho que funciona) e/ou drenar pending exec/handlers.
- **Validação:** N loads a frio consecutivos headless (todos sem travar)
  + teste manual.
- **Referências:** `segagenesisrecomp/runner/main.c`
  (`runner_save_state_file`/`runner_load_state_file`), `docs/BUG_QUEUE.md`
  §1.2, `docs/PROGRESS.md` §11.1.

### T4 — Sprites do EC quebrados só no round 1 do Trident — 🔬 OBSERVAÇÃO PARCIAL (2026-11-21)

- **Sintoma:** com EC no elenco (VS, arena Trident = estado 24), os
  sprites dos ECs aparecem quebrados/ausentes **só no round 1**; no
  round 2 carregam. (Fila: `BUG_QUEUE.md` §1.1.)
- **Pista registrada:** carregamento sob demanda perde a corrida contra
  o 1º frame do round 1; no round 2 os assets já estão na VRAM desde o
  round 1 — **o que muda entre os rounds é a chave da observação**.
- **Passos:** (1) dumps de VRAM/SAT/índices de tiles no início do round 1
  × início do round 2 (mesmo frame relativo); (2) diff do conjunto de
  assets carregados (quais tiles/planos existem no round 2 e faltam no
  round 1); (3) rastrear a task de carga do estado 24 (quem faz DMA de
  tiles/VRAM e quando); (4) revisar a ordem de aplicação dos hooks
  `$043C12`/`$043C76`/`$043C5A`/`$043CDC` (blobs de forma) no 1º frame
  do round 1.
- **Fix candidato:** stall/garantia de carga antes do 1º frame do round 1
  quando há EC (ex.: `jsr $FCDE` extra no handler do estado 24, padrão
  já usado pelos handlers originais), ou pré-carga do blob correto.
- **Validação:** capturas round 1 × round 2 com sprites íntegros;
  suíte `17/17` verde; autodeterminismo M3 preservado (o fix não pode
  tocar o jogo base sem `--unlock-boss`).
- **Referências:** `game/ec_boss.c`/`ec_boss.h`,
  `disasm/code/state_handlers10.asm` (`$8D60`, estado 24),
  `docs/BUG_QUEUE.md` §1.1.

## Ordem sugerida e dependências

1. **T1** (bloqueador de gameplay, provavelmente o hook mais simples) —
   e **T2** compartilham o match-end `$1EF88–$1F0A8` e os exits 14/18:
   investigar juntos, fixar separadamente.
2. **T4** (sprites) — toca os mesmos hooks do EC que T1; risco de
   interação, fazer após T1 estabilizado.
3. **T3** (savegame) — independente do resto; pode andar em paralelo.

## Critérios de aceite gerais

- Suíte do harness `17/17` verde (regressão M3/2 camadas).
- Jogo base sem `--unlock-boss` byte-identical ao comportamento atual
  (todos os desvios seguem opt-in).
- Cada fix validado com movie/input-script headless + confirmação manual
  no build nativo.
- `docs/BUG_QUEUE.md` e `docs/PROGRESS.md` atualizados com causa raiz,
  fix e data.
