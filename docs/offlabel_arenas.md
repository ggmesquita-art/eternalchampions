# Cenários off-label (Holo-Trainer + Santuário) — Estado da caçada

## RESULTADO FINAL (2026-11-21) — CONCLUÍDO

A caçada descrita abaixo terminou com sucesso. Resumo do estado final
(detalhes em `PROGRESS.md` §11.2–11.3):

- **Lista de 12 nomes off-label realocada para ROM livre em `$257051`**
  (12 ponteiros + strings `"HOLO-TRN"` `$257081` / `"SANCTUARY"` `$25708A`);
  defs LONG do menu (`$2E8B0`/`$2EC7E`) repontadas. (RAM `$FFCF3C–$FFCF7E`
  era usada pelo jogo como records de sprites → bug global de sprites +
  hang do valor 10; resolvidos com a realocação.)
- **Clamp do Background `9 → 11`** nas defs `$2E8AC`/`$2EC7A`.
- **Commit `$2E42E` (hook):** `$936F=10 → estado 14` (Holo-Trainer) e
  `$936F=11 → estado 18` (santuário do EC). Seleção end-to-end verificada
  headless e visualmente (Holo: 96–98% overlap com o Training Mode
  original; santuário confirmado pelo usuário).
- **Fim de partida:** hooks nos RTS `$59E6`/`$59EE` (return 0) sobrescrevem
  `$9CB6=1` quando `$936F==11` — vencer no santuário off-label volta ao
  menu (antes: finalização/endgame ou travamento). Off-label Holo
  (estado 14) já voltava ao menu.
- **Nota de numeração:** a tabela abaixo hipotetizava "14/17"; o número de
  estado É a arena: 14 = Holo-Trainer (mesmo handler do Training Mode) e
  18 = santuário (câmara do EC). Estado 20 é a arena do Xavier (templo
  escuro), não o santuário.

---

## Objetivo
- Selecionar Holo-Trainer (índice 14) e Santuário do EC (índice 17) no menu de
  Background do VS 2P (opt-in via `--unlock-boss`).

## Fatos estabelecidos (2026-11-21)

### Máquina de estados (state_dispatch.asm / state_table.asm)
- `$FD64` = estado do jogo. `State_SetState` ($FE74): d0 → $FD64 + init + dispatch.
- Tabela de 40 estados em $27B4. **A arena É o número de estado no VS:**
  - 19..27 = as 9 arenas normais (19=Midknight, 20=Xavier, 21=Blade, 22=Jetta,
    23=RAX, 24=Trident, 25=Shadow, 26=Slash, 27=Larcen — confirmar ordem exata)
  - **18 = Santuário** ($56F0 — EC_DIAG_BOSS_SANCTUARY força S18/P2=9)
  - Holo-Trainer: estado a confirmar (Training mode)
- Menu = estado 13; menus internos = sub-estados do 13/1.
- Filme 2P VS (bg_idx=8): state 1 → 0x18 (24=Trident) entre f2440 e f2920,
  com TODAS as 9Bxx = 0 → o VS não grava arena em $9B28/$9B2E no início.
  O índice do menu vira NÚMERO DE ESTADO na transição.

### Pistas do commit (red herring parcial)
- `combat_core.asm` `Combat_CameraTask3` ($5B6B2): consome o índice de arena
  (caminhos lendo $FD58&$F → 9B2E/9B28, $FD5A&$F → 9B2E/9B2A, ou reusa 9B2E).
  Isso é de outro modo (1P/practice?), NÃO do VS 2P.
- Tabela de validade em $5B8FC (`tst.b (a2,d1.w)` após `cmpi.b #$F,d1`).

### Menu de Options (VS)
- Timeline do movie (build_2p_select): 1400 DOWN → 1420 START (Two Players) →
  1560 START (Fight) → 1700 DOWN → 1720 START (Options) → 1880+ taps RIGHT =
  lista do Background (bg_idx=1→Blade … 8→Trident — verificado). Save-state
  em f1850 = Options aberto, cursor no Background.
- Cursor/scroll: $FFFFE65A (dump off 0xE65A) anda ~8px por seleção, sem writer
  direto (acesso indireto). `$FFAB` (off 0xFFAB = campo +$B9 do slot P2 em
  $FFFFAA52) vai 0→12 no 1º tap e fica 12 = suspeito de "tamanho da lista/max".
  `$FF8F` alterna 12/1.
- NÃO existe índice simples 0..13 na RAM durante a seleção (byte/word varreram
  os 64KB). A seleção deriva do scroll (confirma hipótese anterior).
- `$FD58`/`$FD5A` ficam 0 no menu; viram pad-state na luta.

## Caminho mais promissor (próximo passo)
1. Achar a transição menu→estado 19..27: procurar `addi.w #$13` / `addq.w #$13`
   (índice+19) em menu_system.asm / state_handlers*.asm, ou quem chama $FE74
   com d0 calculado.
2. Com o ponto de transição: mapear de onde vem o índice (scroll→índice) e o
   clamp do menu.
3. Patch opt-in: estender o cursor para 14/17 no menu (lista de nomes + clamp)
   OU hook na transição sob `--unlock-boss` (menu chega até 13; com unlock,
   taps extras selecionam 14=Holo-Trainer, 17=Santuário e o commit mapeia
   para os estados corretos: 18=santuário, Holo-Trainer=?).

## Testes prontos
- `/tmp/opencode/commit_trace.input` + `ct_*.bin`: transição state 1→24.
- `/tmp/opencode/bg_diff2.input` + `sel_*.bin`: dumps por seleção (settle +90).
- `/tmp/opencode/bg_base.sav`: Options do VS no frame 1850 (cursor no Background).

## Relacionado
- `docs/main_loop.md`; labels em `disasm/symbols/labels.toml`; RAM em
  `disasm/symbols/ram_map.toml` (614 entradas; $FFFFAA52 = slot P2, $FFFFAB86
  = Fighter_P1 stride 0x134).
