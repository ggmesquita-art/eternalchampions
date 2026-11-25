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

## Overkill do Holo-Trainer — raio da armadilha do trilho (2026-11-25, `--unlock-boss`)

O Holo-Trainer (Estado `14`) não está na tabela de overkills por arena (`$26A36`: bytes `13 15..1B 14 FF`, sem espaço para mais uma entrada), então o overkill é armado pelo mesmo hook do vitral do Santuário (`$026A16`, em `game/ec_boss.c`) quando `is_vs_offlabel_holo()`.

- **Gatilho:** as mesmas condições do jogo para qualquer overkill (golpe que nocauteia, round decisivo, golpe forte `($128) >= $0B`, atacante com vida) com a vítima no **centro** da arena (`x` entre 400 e 560, `HOLO_ZONE_MIN/MAX`).
- **Vítima parada:** usa `$9C7C = 2`, o caminho do overkill da arena do RAX (`$5E566`). A vítima fica no lugar em vez de ser arremessada (o vitral usa `1`).
- **Armadilha ("moving head"):** as cabeças que correm no trilho do alto são objetos `$CB` criados só na sala de treino (`State17_Handler`, Estado `17`); os gráficos são o grupo de tiles `$48` (índice `$29` da lista `$7F65A`, comprimido na ROM, modo 28). O Holo não os carrega: o overkill chama `Tile_AllocGroup` (`$E408`) para o grupo `$48`, remapeia em lugar os 26 tiles da cabeça da paleta da sala de treino (`State17_Palette` `$4804`, linha 1) para as cores estáveis mais próximas da linha 1 do Holo e a desenha (topo 3×2 alternando, boca 3×2 fechada / 4×2 aberta ao disparar, prioridade alta), deslizando no trilho (`y = $28`) do lado mais distante até ficar sobre a vítima. No fim o grupo é liberado (`$E340`). Se o grupo não puder ser carregado, o drone flutuante (tiles `$E3`–`$E8` do Holo) entra no lugar.
- **Sequência (~7 s, inspirada no overkill do RAX):** a vítima fica tonta e parada (36 quadros), a cabeça desliza pelo trilho (90), dispara o raio vermelho com núcleo branco e clarão no chão (140) enquanto a vítima passa a um brilho ciano pulsante, e então ela **desintegra**: some e vira partículas lidas da própria silhueta (sub-sprites `($C)` + tiles na VRAM, grade 8×8, até 56), que caem em cascata de cima para baixo até formar um monte de pó (120); a cabeça volta pelo trilho (80) e o jogo segue (`$16F32`) para a vitória e o menu. As partículas usam só as vagas livres da SAT (reservando as dos objetos visíveis); se faltar, se revezam entre quadros.
- **Câmera:** `$9C7A`/`$9C76` são os alvos X/Y do overkill (`$1F410`/`$1F4FA`); centraliza na vítima (`x - $96`) e mantém o Y atual.
- **VRAM:** os 24 tiles do raio, clarão, monte de pó e partículas são reservados no alocador do jogo (`Mem_Alloc` `$D4CC`, liberados com `Mem_Free` `$D5A4`). Sem espaço, não há overkill. Os índices de cor que ciclam no Holo são detectados durante a aproximação e evitados.
- **Sprites:** hook em `$00E68C` (`Sub_E684` com `d4`/`a2` carregados) grava as entradas no início da SAT em RAM, à frente dos objetos.
- **Brilho:** interpolação da linha de paleta da vítima para ciano no buffer `$F036` (+ `$EF32`); restaurada no fim do round (`$5818`).
- **Diagnóstico:** `EC_HOLO_OK_TEST=<n>` arma o efeito `n` quadros após entrar no Holo, inclusive no treino, com o P2 como vítima e sem as chamadas de fim de luta (`EC_HOLO_OK_DRONE=1` força o drone); `EC_HOLO_OK_LOG=1` registra o gatilho em stderr; `EC_HOLO_OK_SFX=<hex>` define o som do disparo (padrão: nenhum).
- **Validado:** golpe final real no save do usuário (VS no Holo, adversário com 16 de energia, chute forte no centro): cabeça, raio, queima, cinzas e volta ao menu. Prévia: `assets/video_compare/holo_overkill/preview_overkill_holo.png`.
- **Pendente:** escolher o som do disparo; calibrar a faixa do centro se preciso.
