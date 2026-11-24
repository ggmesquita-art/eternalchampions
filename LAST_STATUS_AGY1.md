Conversation Log
verifique o arquivo proposta\_bufix\_2026-11-21.md que resume os quatro problemas que estamos enfrentando. tente resolver na ordem de complexidade (mais facil -> mais difícil) na SUA avaliação, use como guia, mas não se atenha pois pode haver imprecisões no informado.

Investigando os 4 problemas (T1–T4) em paralelo no disassembly, game/ e segagenesisrecomp/ para validar cada diagnóstico antes de ordenar e planejar as correções.

Recebi o relatório de T1 (Santuário / estado 18). Aguardando a conclusão das varreduras paralelas de T2, T3 e T4.

Recebi também o relatório de T3 (save/load V2 — achados críticos no in-place load e na premissa do teste headless). Aguardando T2 e T4.

Recebi também o relatório de T4 (sprites do EC no Round 1 do Trident). Aguardando apenas o relatório final de T2 para consolidar o diagnóstico e a ordem de complexidade dos 4 problemas.

Alerta imediato (concorrência + premissas do doc):

Há outra sessão/agente escrevendo neste mesmo repositório agora: entre 14:20 e 14:45 entraram os commits 0f0a75b (T2, 14:39), 2e016c3 (T3, 14:43), segagenesisrecomp 0ae8f84/231d77a, e scripts novos em tools/scripts/t4\_repro/ + /tmp/opencode/t4\_probe.\* (14:45). Não alterei nenhum arquivo nem rodei build para evitar colisão no working tree e em build/native/.
Três dos quatro diagnósticos em proposta\_bugfix\_2026-11-21.md têm imprecisões críticas — em especial T1 (marcado como RESOLVIDO, mas ainda quebrado), T3 (o staging de 231d77a não resolve a causa-raiz no glue.c) e T4 (a premissa de "assets já na VRAM no round 2" não bate com o disassembly).
Auditoria Real dos 4 Problemas (ordenados do mais fácil → mais difícil)
0. T2 — Training: tela preta ao terminar — ✅ Já corrigido (0f0a75b)
Causa-raiz real (confirmada no disassembly e logs t2nat2/t2int2): não era chunk slicing nem $D7E4, mas vazamento de 4 bytes em A7 no recompilador sobre o padrão link/unlk de Z80\_RingRead/Z80\_RingEpilogue (irq.asm), corrompendo o A6 = $00C00000 restaurado pelo movem.l de Main\_FrameLoop e matando o DMA de CRAM em VBlank\_VdpUpdate ($D6A0).
Status: corrigido em m68k-recomp-core 2666e9f / segagenesisrecomp 0ae8f84.
Pendência menor: o teste antigo de "fluxo original" (t2\_nav5.input) entrava no Estado 17, não no Holo-Trainer original (Estado 14); vale validar o Estado 14 original e decidir se o stall DMA fixo de 3402866 em genesis\_vdp.c deve ser mantido ou revertido.

1. T1 — Santuário (Estado 18): encerra no Round 1 em vez de seguir ao Round 2 — Mais fácil (FALSO "RESOLVIDO")
Por que b428b9c não resolveu: apenas colocou o guard $9CBC!=9 \&\& $9CBE!=9 em $059E6/$059EE (ec\_boss.c). Com isso:
VS 2P Santuário COM EC: P1 vencer R1 ainda não vai ao R2 — cai no ending do boss 1P (8 → 6 \[\~20s estático] → 4 → 10 → jmp $200 reset). Se P2 vence R1, $934E nem é incrementado.
VS 2P Santuário SEM EC: P1 vencer R1 ainda volta ao menu após 1 round; P2 vencer R1 não pontua $934E; R2 encerra em empate 1–1; ignora $936D.
Imprecisões do doc: H2 ($1F19A como "regra de boss" e loc\_01F15C comparando $9CBE) é falsa ($1F19A só grava estatísticas em $92E2; loc\_01F15C ignora d0). Além disso, $936F não é zerado entre VS e 1P Arcade — o discriminador confiável é $9CB0 != 0 || $9424 != 0 (VS/Torneio vs 1P Arcade Boss $9CB0 == 0 \&\& $9424 == 0).
Causa-raiz real (2 pontos acoplados):
Sub\_01EDD6 (obj\_tasks3.asm): para $FD64 == $12, loc\_01EF88 desvia para loc\_01EF9A (que só incrementa $9350 para P1 e nunca $934E para P2) e pula o fluxo normal loc\_01EFE6 ($9350/$934E, $936D via loc\_01F15C, banners $73/$76).
Exit de State18\_Handler (state\_handlers6.asm, $59B6–$59EE): faz regra hardcoded de boss 1P em vez de Stage\_ExitCleanup ($C578, util\_combat\_input.asm).
Fix proposto (100% opt-in --unlock-boss; 1P Arcade intocado):
Quando $FD64 == 18 e modo VS/Torneio ($9CB0 != 0 || $9424 != 0), desviar loc\_01EF88 para o caminho normal loc\_01EFE6 (verificando antes se o banner Obj $73 suporta d0 == 9 sem OOB) e aplicar na saída do Estado 18 a regra exata de Stage\_ExitCleanup ($936D + loc\_01F138 → $9352++; $9CB6 = 18 entre rounds; ao fim do match $9CB2 = dat\_C6BA\[$9CB0]; $9CB6 = 1).
Arquivos afetados: game/ec\_boss.c, game/game.toml (+ regeneração de ec\_dispatch.c/build).
Verificação: input-script VS 2P Santuário (com EC e sem EC) cobrindo P1 vence R1 → R2 → fim → menu VS ($9CB2=2, $9CB6=1) e P2 vence R1 → $934E=1 → R2; regressão 1P Boss autêntico; suíte 17/17.
2. T3 — Savegame V2: 1º load trava (2–3× libera) — Complexidade média (fix 231d77a incompleto)
Por que t3\_load.input não reproduziu e por que o staging (231d77a) não basta:
t3\_save.input e t3\_load.input rodam os mesmos 834 passos até o mesmo Estado 14 antes do LOAD\_STATE — nunca testaram carregar um save de Luta a partir do Title (Estado 7) ou Menu (Estados 1/9/13).
Bug 1 (crítico): glue\_prepare\_inplace\_load() (glue.c) retorna 1 incondicionalmente, tornando código morto todo o ramo else (ec\_save\_resume\_pc → $00255E + glue\_restart\_game\_fiber\_keep\_a7) em main.c. Ao carregar uma Luta a partir de Title/Menu, s\_game\_fiber continua estacionada na pilha C do estado antigo (State7/State9/State13) sobre a RAM/A7 do save, com g\_recomp\_native\_frame apontando para frames pré-load.
Bug 2 (deadlock real): glue\_finish\_inplace\_load() (glue.c) força s\_game\_yielded\_vblank = 1 e limpa o sched restaurado. Se o save tiver SR mascarado (imask >= 6, ex. $2700 — comum em EC), glue\_run\_game\_chunk() retorna imediatamente por s\_game\_yielded\_vblank == 1 e glue\_own\_interrupt(6) só faz latch sem desbloquear → guest travado permanentemente enquanto a UI SDL continua viva.
Bug 3: glue\_reset\_sched\_accounting() (glue.c) não zera g\_split\_sp\_popped, g\_early\_return nem s\_pending\_irq.
Fix proposto:
Só usar in-place quando s\_yield\_site == GLUE\_YIELD\_VBLANK e o estado/loop C ativo for compatível com o save e (g\_cpu.SR \& 0x0700) < 0x0600; fora disso, usar glue\_restart\_game\_fiber\_keep\_a7() com o PC de retomada adequado ao estado restaurado (e recomp\_tail\_frame\_set(NULL) + limpeza completa de g\_split\_sp\_popped/g\_early\_return/s\_pending\_irq).
Arquivos afetados: segagenesisrecomp/runner/glue.c, segagenesisrecomp/runner/main.c, game/ec\_spec.c.
Verificação: repro headless novo carregando save de luta a partir do Title (Estado 7), Menu (Estado 1/9) e frame com SR=$2700 + suíte 17/17.
3. T4 — Sprites dos ECs quebrados só no Round 1 do Trident (Estado 24) — Mais difícil (requer repro + medição antes do fix)
Premissa falsa no doc: na troca de round (Stage\_ExitCleanup → State\_SetState → Sub\_FF08), o jogo libera todos os TileGroups não-pinados e reinicializa AB86/AA52 (($12,a0)=$FFFF, ($A8,a5)=$FFFF), então o Round 2 também re-descomprime e re-enfileira DMAs dos lutadores do zero.
Mecanismo real suspeito (único do Trident + hook do EC):
Os buffers dinâmicos de VRAM dos dois lutadores ($FFFF9794/$FFFF9790, 150 tiles cada) são pinados no boot (Sub\_23F4), então não são zerados entre rounds: no Round 1 contêm lixo/menu; no Round 2 ainda contêm tiles válidos do EC do Round 1 se uma DMA falhar.
O Trident (state\_handlers10.asm) tem a maior pressão de VRAM/DMA do jogo (12 TileAnims todo frame + 5 objetos de cena + TileGroup $0011 — tanto que State28\_Handler libera $0011 só no Estado 24).
O hook 0x043F4Cu (ec\_boss.c) pula o atraso invisível de 10 frames (loc\_043F5E) no Round 1, fazendo ambos os ECs dispararem o burst de DMAs no mesmo Stage\_LoadBg. Se a fila DMA $FFFFFD22 atingir o teto #$7F (127) em loc\_CD14 (util\_combat\_input.asm), as DMAs excedentes são descartadas silenciosamente depois que loc\_EAC6 já gravou ($12,a5) = ($10,a5) — ou há estouro no pool de 148 pieces ($D87A), SAT de 80 sprites ($DF6C) ou Mem\_Alloc ($D4CC).
Próximo passo: reproduzir no cenário exato (mirror EC×EC / formas 1–4 no Estado 24) medindo $FD22, $D87A, $DF6C e $F208 no Round 1 × Round 2 antes de escolher o fix (não aplicar jsr $FCDE cego).
❓ Antes de eu alterar qualquer código, confirme:
A outra sessão/agente que gravou os commits de 14:39/14:43 e rodou t4\_probe às 14:45 já foi encerrada?
Posso iniciar pela ordem T1 → T3 → T4 (começando pela microtarefa T1 em game/ec\_boss.c + game/game.toml)?
sim ja foi encerrada.

Aguardando a conclusão dos testes B1 (VS Santuário com EC) e C (regressão Boss 1P) para analisar os resultados e rodar a suíte do harness.

Aguardando a conclusão da suíte completa do harness (17/17 movies, task-195) para validar regressão zero no jogo base e fechar o ciclo de T1.

Suíte completa do harness (17/17, task-202) iniciada com --report-md /tmp/t1\_test/harness\_report.md; aguardando conclusão.

🎯 Estado Atual
Tarefa Concluída: Correção completa do T1 (fluxo de rounds Best-of-N e saída do Estado 18 / Santuário em modos VS/Torneio off-label sob --unlock-boss, preservando o Boss 1P Arcade e o jogo base).
Item Marcado: N/A — sem task.md (docs/proposta\_bugfix\_2026-11-21.md marcava T1 erroneamente como resolvido; atualização documental ficará para o fechamento da fila).
🛠️ Alterações Realizadas
game/game.toml: adicionados 6 novos sites \[\[widescreen\_site]] (kind = "game\_hook"): 0x0059D6, 0x01EE74, 0x01EF8E, 0x01FD20, 0x01FD96 e 0x05E652 (além dos existentes 0x059E6/0x059EE), totalizando 34 sites gerados em s\_game\_hook\_sites de build/native/generated/ec/ec\_dispatch.c.
game/ec\_boss.c:
is\_cpu\_sanctuary\_boss() passou a exigir ram16(0x9CB0u) == 0u para não tratar P2 como boss da campanha em VS 1P ($9CB0 == 1).
Adicionados is\_vs\_offlabel\_sanctuary() ($FD64 == 0x12 \&\& $9CCC != 2 \&\& ($9CB0 != 0 || $9424 != 0)) e sanctuary\_vs\_exit\_cleanup(), que replica Stage\_ExitCleanup ($C578) nos 3 RTS de saída de State18\_Handler (0x059D6u, 0x059E6u, 0x059EEu) com return 0.
game/ec\_boss.c: hooks 0x01EE74u, 0x01EF8Eu, 0x05E652u, 0x01FD20u e 0x01FD96u limpam SR\_Z quando is\_vs\_offlabel\_sanctuary() é verdadeiro, reativando banner/ícones $72, contagem normal de vitórias P1/P2 ($9350/$934E), intro padrão ($6E) e round timer BCD.
🧪 Validações Executadas
Compilação / Regeneração: PASSOU — cmake --build build/native -j (exit code 0; EternalChampionsRecomp regenerado e linkado sem erros).
Lint / Typecheck standalone: NÃO CONFIGURADO no projeto C/CMake.
Testes direcionados T1 (--unlock-boss, headless): PASSOU (rc=0 em todos):
VS 2P Santuário sem EC (test\_a\_no\_ec, Rax vs Blade): R1 P1 KO (p1w=1, p2w=0, rnd=0) → Round 2 (st=18, rnd=1, tmr=0099, 47 cores no PNG); R2 P2 KO (p1w=1, p2w=1, rnd=1) → Round 3 (st=18, rnd=2, tmr=0098, 46 cores); R3 P1 KO (p1w=2, p2w=1) → retorno ao menu VS (st=1, 21 cores).
VS 2P Santuário com EC (test\_b1\_p1\_ec, P1=EC 9 vs P2=Blade 2): R1 P1 KO (p1w=1, rnd=0) → Round 2 (st=18, rnd=1, tmr=0099, 48 cores); R2 P1 KO (p1w=2) → retorno ao menu VS (st=1, 21 cores).
Regressão Boss 1P Arcade (test\_c\_1p\_boss, --unlock-boss + EC\_DIAG\_BOSS\_SANCTUARY=1, gmode=0, p1=0, p2=9): P1 derrotado no R1 → re-entra no Estado 18 (rnd=1); P1 derrotado no R2 → segue fluxo autêntico de derrota do boss (st=10), sem desvio para menu VS.
Suíte determinística do harness (17/17, sem --unlock-boss): PASSOU — python3 harness/ec\_harness.py --report-md /tmp/t1\_test/harness\_report.md (exit code 0; det=0/65536, png\_eq=True, misses=(0,0,0,0) em 17/17 movies; wram\_diff=0/65536 nas 16 capturas widescreen 16:9; interp\_diff=0/65536 em 15/16 comparados com GPGX/interp e o resíduo já documentado no movie 08\_practice\_xavier\_vs\_midknight\_s19.ecm de 55/65536 com gp\_ok=True).
❓ Próximo Passo Proposto
T3 — Savegame V2 (ECRB2sav): corrigir o in-place load no runtime (glue\_prepare\_inplace\_load / glue\_finish\_inplace\_load em segagenesisrecomp/runner/glue.c, segagenesisrecomp/runner/main.c e retomada em game/ec\_spec.c) para não manter a pilha C de outro estado nem travar em SR mascarado (imask >= 6) ou s\_yield\_site != GLUE\_YIELD\_VBLANK, e validar com loads cross-state (Title/Menu -> Fight).
Aguardando confirmação do usuário para prosseguir.
prossiga
continue

Tracing boot state transitions (State 7 → State 9 → State 1) headlessly to anchor all T3 save-load pre-states.

Executando a matriz de 36 testes com todos os 9 saves reais de usuário (native\_save\_1..9.bin, somente leitura) carregados a partir dos 4 contextos (s7\_title, s9\_init, s1\_menu, in\_fight). Aguardando conclusão da verificação.

Aguardando a conclusão da matriz de 36 testes com todos os 9 saves reais de usuário (native\_save\_1..9.bin, somente leitura) em s7\_title, s9\_init, s1\_menu e in\_fight.

Reexecutando a matriz de 36 testes com os caminhos absolutos dos saves reais (build/native/native\_save\_1..9.bin). Aguardando conclusão.

Escaneando o filme 16\_2p\_vs\_match\_end\_s24\_to\_s1.ecm em modo headless para localizar os frames exatos pré-KO dos Rounds 1 e 2 e rodar o teste de retorno/unwind pós-load. Aguardando conclusão.

Executando os testes de unwind pós-load (Round 1 → Round 2 e fim de partida → Menu State 1) e salvamento/carregamento mid-init com SR=$2700. Aguardando conclusão.

Os testes de unwind (Round-End/Match-End → Menu State 1) e recuperação de saves mid-init/decompression (SR=$2700/$2711 via PC=$00FE74) passaram integralmente. Executando agora a regressão completa dos 17 filmes (harness/ec\_harness.py). Aguardando conclusão.

os finishers estao funcionando. ja testei alguns. falta apenas habilitar no Eternal Champion, pois ele parece ser imune.

Anotado — já localizei a causa exata na disassembly enquanto a suíte de regressão do T3 (harness/ec\_harness.py, 17 filmes) termina de rodar em segundo plano:

Por que o Eternal Champion é imune a Overkills/Finishers
Handler de knockdown/KO do EC (loc\_044C70 em fight\_moves1.asm): ao contrário dos 9 lutadores normais (fight\_moves1.asm, loc\_0474FC), o EC não chama a rotina compartilhada de trigger de Overkill ($05E49C → $0269E4 em combat\_core.asm / obj\_tasks4.asm) nem o ajustador de trajetória de Overkill (jsr $000171E2) no golpe fatal.
Handler de derrota tardio (loc\_0444D0 em fight\_moves1.asm): chama $05E49C tarde demais (após o knockdown/reset de estado) e ignora o retorno d1=1 de Overkill, encerrando a corrotina direto com jmp $0000DC10.
(Observação de escopo): $0269E4 possui triggers de Overkill apenas nas 9 arenas padrão ($13–$1B, incluindo Trident $18); Santuário ($12) e Holo-Trainer ($0E) não têm armadilha de Overkill na ROM.
Estado Atual e Próxima Ordem
T3 (Savegame V2): correção aplicada, matriz de 36 testes com os 9 saves reais + testes de unwind (Round 1 → Round 2 → Menu) + saves mid-init SR=$2700 todos aprovados; aguardando a conclusão da regressão 17/17 (ec\_harness.py) que já está em execução para encerrar o T3 com limpeza e relatório completo.
Após fechar o T3, prefere que eu ataque primeiro T4 (sprites do EC quebrados no Round 1 no Trident) ou habilitar Overkills/Finishers no Eternal Champion?

Causa-raiz (T3 — Savegame V2 ECRB2sav):

glue.c (glue\_prepare\_inplace\_load): retornava 1 incondicionalmente, deixando a pilha C pré-load da s\_game\_fiber rodando sobre a RAM/g\_cpu restaurada mesmo quando o jogo estava suspenso em GLUE\_YIELD\_BUDGET (2) ou GLUE\_YIELD\_SPIN (3) no meio da pilha de chamadas — em Eternal Champions, o yield nunca ocorre por GLUE\_YIELD\_VBLANK (1). Além disso, glue\_finish\_inplace\_load forçava s\_game\_yielded\_vblank = 1 mesmo com SR mascarado (imask >= 6), e glue\_reset\_sched\_accounting não zerava estados transientes de scheduler (g\_split\_sp\_popped, g\_early\_return, s\_pending\_irq, s\_state\_parked, s\_spin\_*, s\_z80poll\_*, s\_in\_floor, g\_rte\_pending\_ptr).
ec\_spec.c: não implementava call\_resume; o ec\_save\_resume\_pc fixo em 0x00255E (Main\_OuterLoop) quebrava saves de menu (State 1), saves pausados (Main\_PauseLoop), e vazava frames de Main\_FrameLoop / Frame\_RunN na pilha 68K (A7).
main.c: após fechar o modal de save/load (F8), executava continue e rodava 1 frame extra do estado antigo antes de aplicar s\_pending\_load\_path.
🎯 Estado Atual
Tarefa Concluída: Correção do bug T3 (Savegame V2 — falha/travamento no 1º load) com compatibilidade total para os saves existentes native\_save\_1..9.bin (sem alterar o caminho de rollback genesis\_rb\_load).
Item Marcado: N/A — sem task.md (docs/BUG\_QUEUE.md / docs/proposta\_bugfix\_2026-11-21.md serão atualizados na auditoria final).
🛠️ Alterações Realizadas
glue.c: glue\_prepare\_inplace\_load passou a exigir s\_yield\_site == GLUE\_YIELD\_VBLANK \&\& s\_game\_yielded\_vblank \&\& !g\_game\_spec.call\_resume (roteando EC sempre para fiber-restart limpo) e preserva botões/rebase vivos para a trajetória keep\_a7; glue\_finish\_inplace\_load só mantém s\_game\_yielded\_vblank = 1 se imask < 6; glue\_reset\_sched\_accounting foi movido junto a GlueRbSched (glue.c) zerando todos os campos transientes (g\_split\_sp\_popped, g\_early\_return, s\_pending\_irq, s\_state\_parked, s\_spin\_addr/s\_spin\_count, s\_z80poll\_last\_addr/s\_z80poll\_streak, s\_in\_floor, s\_rte\_real, s\_rte\_dummy, g\_rte\_pending\_ptr).
main.c: drena s\_pending\_load\_path imediatamente ao sair de local\_states\_modal() antes do continue.
ec\_spec.c: adicionados ec\_unwind\_to\_clean\_pc(), ec\_is\_native\_entry(), ec\_release\_z80\_bus\_if\_locked() e ec\_call\_resume() (ligado em .call\_resume), restaurando o bloco movem.l d0-d7/a0-a6 de Main\_FrameLoop e retornando via $00FCEE/$00FD08/$00FE34/$00FE4A (Prioridade 1), VBlank\_WaitFrames $00CB42 (Prioridade 2), init em State\_SetState $00FE74 (Prioridade 3) ou cold boot $002230 (Prioridade 4), alternando entre execução nativa (recomp\_call\_addr) e m68k\_interp\_step nos trechos curtos de retorno interior até o loop principal.
🧪 Validações Executadas
Build / Compilação: PASSOU — cmake --build build/native -j (exit code 0; ec\_spec.c sem warnings; permanece 1 warning pré-existente de declaração implícita de glue\_restart\_game\_fiber\_ex em glue.c:1653). Não há linter/typecheck separado configurado em CMake.
Matriz real de usuário (build/native/native\_save\_1..9.bin, somente leitura × 4 contextos no 1º load: s7\_title, s9\_init, s1\_menu, in\_fight):
Saves 1, 2, 3, 5, 6, 7, 8, 9 (32/32 execuções): PASSOU — 100% responsivos já no 1º load (FD70 avançando normalmente, movimento de P1 imediato em luta, unpause com START no sav6, menu rodando a 60fps em sav1/sav8), misses=(0), zero halts.
native\_save\_4.bin (4/4 funcional): despausou (FD68: 0080→FFFF) e moveu P1 (X: 849→936, d70=45) já no 1º load nos 4 contextos; o script sinalizou \[FLOOR] declined miss $0136FE / interior-label miss: $0136FE, verificado na ROM/RAM do sav4 como bug pré-existente da tabela do EC (id=12, +8C=0x0001B9FE, rom\[0x1B9FE]=0x66, $013566+4\*0x66=$0136FE em func\_013566) independente do save/load.
Unwind Round-End \& Match-End pós-load (16\_2p\_vs\_match\_end\_s24\_to\_s1.ecm): PASSOU — carregamento antes do KO do R1 (s7\_title → R1 KO → R2 st=24 hp=104/104 → R2 KO → State 1 Menu FD70=1708) e antes do KO do R2 (s1\_menu → State 1 Menu FD70=1708), zero misses/halts.
Saves mid-init com IRQ mascarado (SR=$2700 / $2711): PASSOU — boot\_f45 (SR=2700, st=7) e s9\_f910 (SR=2711, st=9) recuperaram via PC=$00FE74 sem deadlock (d70=155 e 126), além de s24\_load\_f2150 (d70=200).
Suíte Determinística Completa: PASSOU (17/17 filmes, widescreen 16/16 com wram\_diff=0/65536) — python3 harness/ec\_harness.py --report-md /tmp/t3\_probe/harness\_report.md (exit code 0; det=0/65536, png\_eq=True, misses=(0,0,0,0) nos 17 filmes; interp\_diff=0/65536 em 15/16 e pré-existente documentado 55/65536 gp\_ok=True no filme 08).
Limpeza / Estado: /tmp/t3\_probe removido; build/native/native\_save\_1..9.bin intactos; alterações apenas em game/ec\_spec.c, game/ec\_boss.c, game/game.toml e segagenesisrecomp/runner/{glue.c,main.c} (sem commit).
❓ Próximo Passo Proposto
Opção A — T4 (Sprites do EC quebrados no Round 1 no Trident, State 24) (próximo item da fila original T1 → T3 → T4)
Opção B — Habilitar Overkills/Finishers no Eternal Champion (permitir que o EC sofra Overkills/Finishers em --unlock-boss, ligando o trigger $05E49C / trajetória $000171E2 nos handlers loc\_044C70/loc\_0444D0 de fight\_moves1.asm)
Aguardando confirmação do usuário para prosseguir (e qual opção priorizar).
prossiga

Iniciando a recompilação de build/native com os novos hooks de T4 (0x05AF92, 0x01354C, 0x01367C) e aguardando o término do build para executar a matriz de verificação.

Todos os testes visuais e de RAM do T4 no Trident (ec\_0\_4, ec\_1\_2, ec\_3\_3, ec\_vs\_blade e leitura de native\_save\_4.bin) passaram sem corrupção de sprite pieces nem misses. Aguardando a conclusão do harness determinístico (ec\_harness.py) para limpar /tmp/t4\_probe/ e encerrar a validação.

o overkill foi habilitado no Eternal Champion?

Não ainda. Acabamos de concluir e validar T4 (sprites quebrados do Eternal Champion no Round 1 da arena do Trident). Habilitar Overkills/Finishers no Eternal Champion (T5) é exatamente o próximo passo da fila, assim que você confirmar.

🔍 Diagnóstico T4 (Causa-Raiz Real)

A hipótese da proposta (corrida de DMA on-demand na VRAM) era falsa — os tiles na VRAM e as 25 entradas da SAT do P2 estavam íntegros. A causa real era corrupção determinística de WRAM no pool de peças de sprite:

Causa-raiz: combat\_core.asm ($05AF8C) na arena do Trident ($FD64 == $18) cria 2 objetos $1B (Sub\_013408, ondas nos pés nos pontos de anexo 2 e 3) para cada lutador. A folha $7C do Eternal Champion ($40 == $000C) só possui 2 pontos de anexo (0, 1), e as duas tabelas de Sub\_013408 (obj\_tasks1.asm, obj\_tasks1.asm, obj\_tasks2.asm) só contêm os 9 lutadores base (3..11), sem checar terminador. Para $000C, o scan varria 318 registros além de $016F74 e gravava ($8C,a5) = $0001B9FE. No início do Round 1 (frame 2798), loc\_013546 lia $66 de $0001B9FE, calculava D0 = $0198 e saltava para $0136FE (meio da tabela $0136E0), cuja word $3700 decodifica como MOVE.W D0, -(A3). Como A3 ainda apontava para $FFFFDADA (último nó montado por Sub\_E884/loc\_EE70), $3700 gravava 0x0198 em $FFFFDAD8 — corrompendo o ponteiro next da peça \[12] do P1 (@FFDAD0), fazendo a SAT saltar para espaços ASCII ($2020) no cabeçalho da ROM e cortar o resto do P1 + todo o P2. Na instrução seguinte ($013700: $000A), o \[FLOOR] rejeitava e colocava $0136FE em blacklist — explicando por que o Round 2 (após reset do pool em Sub\_E584) voltava intacto.
Correção aplicada (opt-in --unlock-boss): pular a criação dos $1B para $40 == $000C em $05AF92, com defesa em profundidade em $01354C e $01367C para saves legados (como native\_save\_4.bin) que já tenham $1B do EC vivos na RAM.
Resultado: build limpo (exit 0), todas as formas do EC (0..4) + luta mista (EC vs Rax) + native\_save\_4.bin (somente leitura) com 0 misses/\[FLOOR] declines, 0 nós corrompidos, 0 entradas SAT corrompidas e sprites verificados visualmente nos PNGs; harness base 17/17 PASS.
🎯 Estado Atual
Tarefa Concluída: T4 — Correção dos sprites corrompidos do Eternal Champion no Round 1 do Trident (Estado 24)
Item Marcado: N/A — sem task.md (T1, T2, T3 e T4 concluídos; /tmp/t4\_probe/ pendente apenas de remoção no próximo turno de execução)
🛠️ Alterações Realizadas
game.toml: adicionados 3 game\_hook (0x05AF92, 0x01354C, 0x01367C).
ec\_boss.c: 0x05AF92 limpa SR\_Z para EC ($40 == $000C) não criar objetos $1B no Trident (preservando os 9 lutadores base); 0x01354C e 0x01367C protegem e retiram com segurança objetos $1B legados com ponteiro fora de $016F74–$01718F.
🧪 Validações Executadas
Lint/Typecheck: NÃO CONFIGURADO (compilação C/C++ via cmake --build build/native -j — PASSOU, exit 0, 0 warnings em ec\_boss.c, hooks confirmados em ec\_part00.c, ec\_part03.c, ec\_part24.c)
Testes:
T4 Trident (--unlock-boss): PASSOU — ec\_0\_4 (Base vs Tiger, R1/R2 stand+air), ec\_1\_2 (Dragon vs Falcon, R1 stand+air), ec\_3\_3 (Shark mirror, R1 stand+air), ec\_vs\_blade (Dragon vs Rax, Rax manteve os 2 $1B de ondas nos pés e EC 0), native\_save\_4.bin (read-only, SHA-256 de native\_save\_1..9.bin inalterados): 0 misses / 0 \[FLOOR] declines / bad\_nodes=\[] / sat\_bad=\[] e sprites confirmados visualmente via PNGs.
Harness determinístico base: PASSOU (17/17 filmes, det=0/65536, png\_eq=True, misses=(0,0,0,0), 16/16 widescreen wram\_diff=0/65536) — python3 harness/ec\_harness.py --report-md /tmp/t4\_probe/harness\_report.md
❓ Próximo Passo Proposto
T5 — Habilitar Overkills/Finishers no Eternal Champion (--unlock-boss) (incluindo limpeza de /tmp/t4\_probe/ e auditoria de todas as 9 arenas + tabelas indexadas por $40 nos scripts de Overkill como $012418 em obj\_tasks1.asm)
Aguardando confirmação do usuário para prosseguir.
prossiga

Recompilando o binário nativo com os hooks de T5 (0x044C70 e as tabelas de Overkill de Trident/Larcen/Xavier) e aguardando a conclusão de task-783.

Executando trident\_ec\_v3 e sondando os índices de seleção das arenas Larcen ($17) e Xavier ($1A) em task-822.

Executando trident\_ec\_v5 (chip prévio + KO na zona \[266..320]) e a sondagem de trajetória na fase Larcen (S=23) em task-865.

Rastreando o frame exato de ativação do 17\_2p\_vs\_overkill\_trident\_s24.ecm e executando larcen\_ec\_v1 (Larcen, Overkill Tipo 2 no Eternal Champion) em task-870.

Executando trident\_ec\_v6 (Tipo 1) e larcen\_ec\_v2 (Tipo 2) com posicionamento ajustado para o KO em x ∈ \[266..320] em task-874.

Substituí o hook de 0x044C70 para 0x044AE2 em game.toml e ec\_boss.c, e estou aguardando o término da recompilação em task-920.

Executando trident\_ec\_v7 (Tipo 1), larcen\_ec\_v3 (Tipo 2) e a sondagem xavier\_probe (Tipo 3) com o hook 0x044AE2 compilado em task-925.

Inspecionei os PNGs parciais (t1v7\_f6900/7200 e t2v3\_f6700/7200, sprites e arenas íntegros) e aguardo a conclusão de task-933 (Tubarão completo até State 1, transições do Overkill de Larcen e sondagens de Xavier e EC vs EC).

Inspecionei os quadros do Overkill de Trident (t1full\_f7400/7700/8000 — captura pelos tentáculos e poça na superfície íntegros, sem glitch visual) e aguardo a conclusão de task-940 (EC vs EC em Trident, transição de $9C7C em Larcen e Xavier Tipo 3 no Eternal Champion).

Executando trident\_ecec\_v3 (EC Form 1 vs EC Form 4 em Trident), xavier\_ec\_v2 (Xavier Tipo 3 no EC com P1.x ∈ \[80..160]) e rastreamento fino de $9C7C em Larcen em task-945.

Executando trident\_ecec\_v4 (EC Form 1 vs EC Form 4 em Trident) e xavier\_ec\_v3 (Xavier Tipo 3 no Eternal Champion) em task-949.

Inspecionei os quadros t1b7\_f6500 e t1b7\_f6800 (EC Form 1 vs EC Form 4 no Overkill de Trident, sprites íntegros) e aguardo a suíte determinística harness/ec\_harness.py (17 filmes) em task-971.

Causa-raiz (fight\_moves1.asm:658-659, $044ADE–$044AE2): O Eternal Champion ($40 == $000C) ignorava Sub\_05E49C (combat\_core.asm:6539) porque sua rotina de derrota saltava em tst.w ($68,a5); ble.w loc\_044C70 diretamente para a queda (loc\_044C70) em vez de passar por loc\_0474FC -> $05E49C -> $0269E4 como os lutadores normais; além disso, tentar interceptar 0x044C70 com guarda em ($E6,a5) bit 7 falhava quando golpes pesados com hitstop já haviam setado bit 7 de ($E6,a3) em obj\_tasks3.asm:344 ($01773A). Complementarmente, três rotinas de Overkill em obj\_tasks1.asm (Sub\_015F38, Sub\_01222E/Sub\_01244E, Sub\_012738/Sub\_012C38) consultam tabelas de $40 sem entrada $000C.
Correção aplicada: Substituído o salto $044AE2 (ble.w loc\_044C70) via hook game\_hook (opt-in sob --unlock-boss) chamando Sub\_05E49C com retorno ($8,a4)=$00044C70 apenas quando is\_playable\_ec\_slot(a5) e HP <= 0 (preservando o Boss 1P do Santuário), e adicionados 5 hooks de fallback para índice 8 nas tabelas de Overkill sem $000C.
Resultado: Build limpo (exit=0), Overkills Type 1 (S=24 RAX vs EC e EC Form 1 vs EC Form 4), Type 2 (S=23) e Type 3 (S=26) disparando e encerrando até State=1 com zero misses e sprites íntegros nos PNGs, e suíte base 17/17 PASS.
🎯 Estado Atual
Tarefa Concluída: T5 — Habilitar Overkills / Finishers no Eternal Champion (--unlock-boss)
Item Marcado: N/A — sem task.md (escopo ad-hoc T5 após T1–T4)
🛠️ Alterações Realizadas
game.toml: adicionados 6 pontos \[\[widescreen\_site]] (kind = "game\_hook"): 0x044AE2, 0x015F90, 0x0123C4, 0x01259E, 0x012832 e 0x012C76.
ec\_boss.c: em ec\_boss\_instruction\_hook, implementado case 0x044AE2u (chama recomp\_call\_addr(0x0005E49Cu) com ($8,a4)=0x00044C70 para EC jogável com (int16\_t)ram16(a5+0x68u) <= 0), case 0x015F90u ($000C -> D0=8, SR|=SR\_Z) e case 0x0123C4u / 0x01259Eu / 0x012832u / 0x012C76u ($000C -> 0x0008).
🧪 Validações Executadas
Build / Codegen: PASSOU — cmake --build build/native -j (exit=0); hooks gerados confirmados em ec\_part19.c:14 (0x044AE2u), ec\_part05.c:7660 (0x015F90u), ec\_part16.c:15184 (0x0123C4u), ec\_part18.c:193 (0x01259Eu), ec\_part10.c:1441 (0x012832u) e ec\_part04.c:4665 (0x012C76u, span quebrado automaticamente).
Lint / Typecheck: NÃO CONFIGURADO (compilação C/C++ executada sem warnings em ec\_boss.c).
Overkill Tipo 1 (S=24, Trident):
trident\_ec\_v7\_full (RAX id=5 vs EC Base id=12, --unlock-boss): PASSOU (exit=0) — R1 KO normal ($9C7E=0); R2 Overkill (f=6500..8000: $9C7E=1, $9C7C=1, $9C74=$AA52), saída limpa em f=8400..9200 (S=1, $9C7C=0), 0 misses; PNGs t1v7\_f6900/7200 e t1full\_f7400/7700/8000 inspecionados (tentáculos e tubarão sem corrupção).
trident\_ecec\_v7 (EC Form 1 vs EC Form 4, --unlock-boss): PASSOU (exit=0) — f=5365..6800: $9C7E=1, $9C7C=1, $9C74=$AA52, saída limpa em f=7400..8600 (S=1, $9C7C=0), 0 misses; PNGs t1b7\_f6500/6800 inspecionados sem corrupção.
Overkill Tipo 2 (S=23):
larcen\_ec\_v3 / larcen\_mid (RAX id=5 vs EC Base id=12, --unlock-boss): PASSOU (exit=0) — R1 KO normal ($9C7E=0); R2 Overkill (f=6500..7200: $9C7E=1, $9C7C=2, $9C74=$AA52 -> f=7220: $9C7C=0 -> f=7450..7800: S=1), 0 misses; PNGs t2v3\_f6700/7200 inspecionados sem corrupção.
Overkill Tipo 3 (S=26):
xavier\_ec\_v3 (P1=EC Base id=12 vs P2=RAX id=5, --unlock-boss): PASSOU (exit=0) — f=6200..7500: $9C7E=1, $9C7C=3, $9C74=$AB86, saída limpa em f=8200 (S=1, $9C7C=0), 0 misses; PNGs t3v3\_f6450/7000/7500 inspecionados (T-Rex sem corrupção).
Nota de escopo: nos testes ao vivo (S=24, S=23, S=26), 0x044AE2 e 0x015F90 foram exercitados em runtime; os hooks defensivos 0x0123C4, 0x01259E, 0x012832 e 0x012C76 em obj\_tasks1.asm foram validados no código gerado e no disassembly, sem terem sido atingidos nas arenas específicas executadas nessa matriz (S=23 acionou a torre de laser, S=26 acionou o T-Rex).
Regressão Base (sem --unlock-boss): PASSOU (17/17 filmes + 16 capturas widescreen) — python3 harness/ec\_harness.py (exit=0, det=0/65536, png\_eq=True, misses=(0,0,0,0) em todos os 17 filmes, wram\_diff=0/65536 no 16:9).
Integridade \& Limpeza: diretórios temporários /tmp/t4\_probe e /tmp/t5\_probe removidos; native\_save\_1..9.bin intactos (157432 bytes cada, hashes SHA-256 inalterados).
❓ Próximo Passo Proposto
Auditoria final de T2 (Holo-Trainer) e atualização da documentação (proposta\_bugfix\_2026-11-21.md / filas/progresso do projeto)
Aguardando confirmação do usuário para prosseguir.
primeiro, comit nesse build que está muito bom.

Em ordem de prioridade,

1. Quando a exibição está em wide/ultrawide nas laterais (limite original da tela) há um corte do sprite do personagem. O correto é esse corte não existir, em nenhum nível. Ele precisa colar na area extra renderizada. Tem como resolver isso? prioritariamente para o personagem. Ainda, em certas arenas ela não carrega completamente, como a do Blade (primeira, ventilador), fica como se fosse uma falha colorida dos lados que vai renderizando aos poucos. Gostaria que todas as arenas encaixassem bem, e o limite fosse realmente no canto esquerdo widescreen, se der para a camera dar um scroll para o lado (andar a area limite da tela para a esquerda ou para a direita quando chegar ao fim, para que o *Canto* da tela coincida com o *limite direito ou esquerdo* (eu sei que talvez isso nao exista, nesse caso, como segunda opção talvez nas laterais seja interessante deixar uma barra preta. Ou por fim, apenas encaixar. O que ocorre é que no Overkill do Slash, o dinossauro vai renderizar pela metade provavelmente. Veja o que voce acha e como podemos resolver isso!
2. meus cumprimentos!confirmo tudo funcionante e resolvido, menos a arena de treino, que sai da luta no 1o round e retorna pro menu de 1 player.

2-B) Nesse caso tambem gostaria de saber se há como desativar o efeito holografico do Player 2. Ele fica piscando porque idealmente é uma arena de treino.

3. Outra questão, existe algum Overkill programado para essa arena de treino?
4. e para a arena do Santuário (a outra que foi corrigida)? No caso do santuario tem uma animação já registrada do Eternal Champion atravessando a janela. Talvez conseguimos aproveita-la como efeito para um overkill? colocar a area de ativação na janela que o Eternal Champion escapa, usando o mesmo mecanismo do estagio da Shadow ou do Blade.

Rs... acho que depois de *tudo isso*, ou pelo menos consertando o video, já temos um jogo alto nivel!

Iniciei a investigação em paralelo dos itens 1 (widescreen/corte lateral de sprites, streaming de colunas na arena do Blade e limites de câmera/Overkill do Slash), 2 e 2-B (saída no 1º round e efeito holográfico do P2 na arena de treino off-label) e 3 e 4 (Overkills na arena de treino e reaproveitamento da animação do vitral no Santuário), além de preparar o commit do build atual. Aguardando o retorno dos subagentes exploradores.

Recebi o relatório completo sobre a arena de treino (State 14), o flicker holográfico do P2, a ausência de Overkill em $0E/$11 e a sequência de quebra do vitral no Santuário ($B3/$B4/Sub\_026638, grupo de tiles $0B). Aguardando apenas o retorno do segundo subagente sobre o widescreen/corte lateral de sprites e streaming de colunas nas arenas.

Commits realizados com o build verificado (sem git push):

Submódulo segagenesisrecomp — commit 89e3bee: glue.c, main.c (T3: unwind nested VBlank/call frames on portable savegame V2 load).
Repositório principal — commit d5babef: ec\_boss.c, ec\_spec.c, game.toml (T1/T3/T4/T5: Sanctuary VS rounds, savegame V2 unwind, Trident EC R1 sprites, EC Overkills).
Diagnóstico Técnico, Trade-offs e Viabilidade (Itens 1–4)

1. Widescreen / Ultrawide: corte de sprites, falha colorida lateral e T-Rex do Slash
Corte de sprites nas bordas de 320px (personagens e T-Rex):
Em ec\_video.c, ec\_video\_scanline\_lazy copia os 320px centrais e chama ec\_render\_margin\_pixel para \[0..left) e \[left+320..width). Porém ec\_render\_margin\_pixel amostra apenas Plane A e Plane B — sprites da SAT nunca são desenhados nas margens.
A ROM já escreve peças que extrapolam 320px na SAT: em Sub\_E684 (util\_engine.asm), objetos com bit 6 de ($20,a0) ligado sofrem cull apenas em screen\_x ∈ \[-32..+320] ($00E772), enquanto objetos com bit 6 = 0 — caso do T-Rex $17 de Sub\_012738 (obj\_tasks1.asm), que nasce em Stage\_ScrollX\_Render - 65 ($01280A) — vão direto para a SAT via loc\_E74A ($00E74A). Em ambos os casos, o corte na vertical de 320px acontece no compositor de margens, além de loc\_E74A precisar evitar que peças em screen\_x <= -128 gravem x = 0 na SAT (sentinela de sprite-mask do VDP em genesis\_vdp.c:773-777).
Falha colorida lateral no início da luta (Blade S=21/$15, Larcen S=27/$1B):
Sub\_F16C (util\_engine.asm) usa nametable de 64×32 tiles (512×256 px) inicializada com tile vazio $809C (exibe CRAM\[0], p.ex. $064E colorido em State21\_Palette, state\_handlers8.asm).
Em Sub\_F6A0 (util\_engine.asm), só camadas com bit 5 ($20) em ($30,a4) preenchem todas as 16 colunas (512px) em loc\_F7FE. Nas camadas #$1E/#$0E de Blade (state\_handlers8.asm:9-36) e Larcen (state\_handlers12.asm:9-43), bit 5 = 0 e bit 1 = 1: loc\_F9F2 inicializa só 11 colunas (352px), deixando as margens em $809C até você caminhar e loc\_FB62 ($00FB62) ir preenchendo coluna por coluna.
Trade-offs de câmera/bordas:
Opção A (Recomendada): (1) Compor sprites da SAT nas margens \[0..left) e \[left+320..width) em ec\_video.c (e alargar o cull de loc\_E772 + proteger x <= -128 em loc\_E74A); (2) pré-preencher todas as 16 colunas na inicialização / amostrar o mapa de metatiles em RAM além de 352px; (3) no limite físico do estágio (\[$FFFFCCF0, $FFFFCCEC + 320]), fazer clamp visual da janela / aplicar barra preta apenas quando a margem ultrawide ultrapassar a largura real do cenário (ou centralizar o excesso).
Prós: zera corte de sprites e faixas coloridas desde o frame 1 em todas as arenas, corrige o T-Rex do Slash, preserva 0/65536 de diff de WRAM na suíte determinística e mantém todas as zonas de Overkill intactas.
Contras: exige tratar camadas sem streaming / wrap-around de 512px no renderizador ou via g\_ws\_margin, e caso a arte original do T-Rex tenha traseira cortada no pixel -65, só saberemos após remover o corte em x = left (se tiver borda aberta, recuar o spawn em g\_ws\_margin no $01280A).
Opção B: Alterar Combat\_CameraTask1 ($05B25E, combat\_core.asm) na ROM para mover os limites de scroll/personagens na direção da margem.
Contras: altera o tamanho jogável da arena (canto deixa de coincidir), quebra determinismo 0/65536 de WRAM, desloca zonas de Overkill e expõe wrap-around em nametables de 512px em fases sem streaming (bit 1 = 0).
2 e 2-B) Arena de Treino (State 14, $FD64 == $0E): saída no 1º round + flicker do P2
Por que sai no 1º round para o menu 1P (State14\_Handler, state\_handlers4.asm + Sub\_01EDD6, obj\_tasks3.asm):
Ao contrário de State17\_Handler (jmp $00C578), o final de State14\_Handler em $003BD6–$003BE8 nunca chama Stage\_ExitCleanup ($00C578): ele seta ($FFFF9CB2).w = 4 (submenu 1P/Training) e ($FFFF9CB6).w = 1 (volta ao menu após 1 luta).
Em Sub\_01EDD6, existem 3 desvios específicos de $0E: $01EE64 (cmpi.w #$E pula banner/estrelas), $01EF90 (loc\_01EFCA só incrementa $9350 do P1 e ignora P2 $934E) e $01F160 (loc\_01F15C retorna round decisivo imediato ignorando $936D Best-of-N).
Na entrada ($003980–$0039C0), força ($FFFF936E).w = 2 (timer ∞) e spawna os objetos de treino $78/$7A em $003B4E–$003B66.
Como desligar o efeito holográfico (flicker 30Hz) do P2:
Em Sub\_01F5EE (obj\_tasks3.asm), $01F6B8 pisca o bit 7 de ($FFFFAA72).w (P2 + $20) durante a materialização e $01F6D4 executa bset.b #$5,($FFFFAA72).w ($01F6E2 no projétil filho), o que faz Sub\_E684 ($00E6DE, util\_engine.asm) pular o desenho do P2 em quadros pares.
Em VS/Torneio off-label ($FD64 == $0E), pular o spawn do objeto $78 em $003B4E (ou retornar logo em $01F5EE) remove de uma só vez o flicker holográfico do P2, o blink inicial, o placar de treino e o timer de 300 frames, mantendo o modo 1P Training 100% intacto.
2. Overkill programado na Arena de Treino ($0E / $11)?
Não existe nenhum. A tabela de despacho $026A36 em Sub\_0269E4 (obj\_tasks4.asm: dc.w $1315,$1617,$1819,$1A1B,$14FF) cobre apenas as 9 arenas regulares $13..$1B (19..27; $14 hex é o estado 20). Qualquer outro estado atinge a sentinela $FF e sai sem Overkill.
3. Reaproveitar a quebra do vitral no Santuário (State 18, $12) como Overkill
É viável. A animação original do vitral fica na task do objeto $B3 (Sub\_025494, obj\_tasks4.asm, ramo loc\_025782 em $025782–$025BE8):
Toca o SFX de quebra $2F ($02596E), chama Sub\_026638 ($026638) 3 vezes com o mapa $000309DE e o grupo de tiles $0B ($FFFFE210\[$B]) para desenhar o buraco no Plane A (x=$30..$80, y=$58..$90), spawna o objeto $B4 (Sub\_025E92, estilhaços + SFX $4E) em (x=$64, y=$78) e chama Sub\_0266AE em (x=0, y=$50, w=7, h=$C).
Requisitos técnicos para funcionar com qualquer lutador em VS ($12):
Em State18\_Handler (state\_handlers6.asm, $005816), o grupo $0B só é carregado na VRAM quando $FD64 == 6; é necessário liberar $005816 também para $FD64 == $12 sob --unlock-boss para o vitral quebrado não corromper tiles.
Em Sub\_0269E4, adicionar gatilho de zona para $FD64 == $12 perto da janela à esquerda (($FFFF9C7A) = 0 para pan à esquerda), usando uma task própria para arremessar a vítima (a5, com seus sprites próprios) até ($64, $78) e então acionar Sub\_026638 + $B4 + Sub\_0266AE (pois $B3 hardcoda gráficos do EC em $25460 e $ABA6).
Plano Proposto (por Prioridade)
Microtarefa 1A (Próxima): Corrigir o corte lateral de sprites (lutadores + T-Rex do Slash) em wide/ultrawide adicionando composição de SAT nas margens em ec\_video.c + ajuste de cull/wrap em Sub\_E684 (util\_engine.asm) via hooks de widescreen.
Microtarefa 1B: Eliminar as faixas coloridas laterais no início de arenas como Blade (S=21) e Larcen (S=27) pré-populando/amostrando as colunas laterais + tratando os limites físicos de borda nas laterais ultrawide.
Microtarefa 2 (Itens 2 e 2-B): Corrigir a Arena de Treino (State 14 $0E) em VS off-label (Stage\_ExitCleanup Best-of-N, desvios de $01EDD6, timer opcional e remoção do flicker holográfico do P2 em $003B4E/$01F6D4).
Microtarefa 3 (Item 4): Implementar o Overkill customizado da quebra do vitral no Santuário (State 18 $12).

Aguardando confirmação do usuário para iniciar a Microtarefa 1A.

inicie a implementação

Compilei ec\_video.c com a composição de sprites da SAT nas margens laterais e aguardo os testes s21 (16:9/21:9) e do T-Rex de Slash (S=26) em task-1055.

🎯 Estado Atual
Tarefa Concluída: Microtarefa 1A — Composição de sprites da SAT nas margens laterais wide/ultrawide em ec\_video.c
Item Marcado: N/A — sem task.md
🛠️ Alterações Realizadas
ec\_video.c: implementado o avaliador por scanline ec\_eval\_margin\_sprites\_line (EcMarginSprites), percorrendo a SAT da VRAM (VDP Reg 5, modo H40/IM2, coordenada X 16-bit com sinal gravada por Sub\_E684 em util\_engine.asm) e desenhando peças que cruzam \[0..left) e \[left+native\_w..width) com suporte a operadores de Shadow/Highlight (pal == 3, níbeis 14/15).
ec\_video.c: atualizado ec\_render\_margin\_pixel e ec\_video\_scanline\_lazy para compor sprites de baixa/alta prioridade junto de Plane A e Plane B com a ordem exata do VDP (S-hi > A-hi > B-hi > S-lo > A-lo > B-lo > backdrop) e regras de Shadow/Highlight.
🧪 Validações Executadas
Build: PASSOU — cmake --build build/native -j (exit=0, zero warnings em ec\_video.c).
Lint / Typecheck: NÃO CONFIGURADO.
Teste Wide/Ultrawide Arena Blade (S=21, 03\_practice\_rax\_vs\_blade\_s21.ecm, 3500 quadros): PASSOU (exit=0)
--widescreen 16:9 (398×224): wram\_diff\_vs\_4\_3=0/65536 contra assets/captures/s21\_rax\_blade.ram; inspeção visual do PNG (s21\_16\_9.png) confirmou o corpo/cabeça completos de RAX (sx = -34..30) e sua sombra colados na borda esquerda sem o corte na linha 320px, além do sprite na margem direita (sx = 327..351).
--widescreen 21:9 (512×224): wram\_diff\_vs\_4\_3=0/65536; inspeção visual (s21\_21\_9.png) confirmou a mesma continuidade dos sprites nas laterais ultrawide (mantendo apenas a faixa colorida de fundo próxima ao ventilador, que será tratada na Microtarefa 1B).
Teste Overkill T-Rex do Slash (S=26, --unlock-boss --widescreen 16:9 vs 4:3, frames 6450, 6650, 7000, 7250, 7500): PASSOU (exit=0)
wram\_diff=0/65536 em todos os 5 checkpoints (S=26, $9C7C=3, $9C7E=1).
Inspeção visual (trex\_16\_9\_f7000/7250/7500.png): o compositor desenha agora todos os tiles da SAT na margem esquerda sem corte no renderizador; isso também comprovou que a arte original do T-Rex termina em uma borda vertical reta atrás da cabeça (foi desenhada sem pescoço/tronco porque nascia oculta fora da janela 320px em Stage\_ScrollX\_Render - $41, $01280A em obj\_tasks1.asm) — podendo receber pillarbox/recorte automático só na janela desse Overkill na Microtarefa 1B.
Limpeza: diretório temporário /tmp/ws1a\_probe removido.
❓ Próximo Passo Proposto
Microtarefa 1B: Eliminar as faixas coloridas laterais no início das arenas (ex.: Blade S=21 / Larcen S=27) pré-populando as 16 colunas de metatiles / lendo do mapa em RAM + tratar limites físicos do cenário nas bordas e recorte/ancoragem do T-Rex no Overkill S=26.
Aguardando confirmação do usuário para prosseguir.
a solucao mais obvia do dinossauro do Slash seria renderizar no canto da tela esquerdo em vez de tentar inventar um corpo inteiro ou outra coisa... a nao ser que vc ache viavel. anote, a micro-observação kkk...

prossiga para a proxima tarefa

Executando a sonda de verificação visual para a Microtarefa 1B (probe1b.py em /tmp/ws1b\_probe/ para os estados salvos, Blade S21 e Larcen S27 em 16:9 e 21:9) e aguardando a conclusão.

Executando a captura e verificação do Overkill do T-Rex do Slash (State 26) em 4:3, 16:9 e 21:9 (task-1304) e aguardando o término.

Executando a suíte completa de regressão em ec\_harness.py (17 filmes + verificações widescreen, task-1318) e aguardando o término.

🎯 Estado Atual
Tarefa Concluída: Microtarefa 1B — Fallback do mapa de metatiles ($FFFFD84E / $FFFFCD1E) para eliminar faixas laterais descarregadas nas arenas (Blade S21, Larcen S27, etc.) + ancoragem da viewport widescreen/ultrawide nas bordas físicas das arenas e alinhamento esquerdo automático do T-Rex no Overkill do Slash (S26).
Item Marcado: N/A — sem task.md (Item 1 do backlog concluído: Microtarefas 1A + 1B verificadas).
🛠️ Alterações Realizadas
ec\_video.c: adicionados os leitores somente leitura ec\_ram32, ec\_mem16, ec\_mem32 e a função ec\_lookup\_layer\_tile, integrada em ec\_fetch\_plane\_pixel, para resolver diretamente do mapa de metatiles descompactado em RAM/ROM ($FFFFCD1E + slot \* 8) qualquer célula ainda em $809C ou coluna de streaming horizontal (fl \& 0x22 == 0x02) fora da janela ativa de 11 metatiles na VRAM (\[c0 .. c0 + 10]).
ec\_video.c: adicionadas ec\_trex\_overkill\_state e ec\_compute\_viewport\_x0 em ec\_video\_scanline\_lazy para:
Ancorar x0 nas bordas reais do monitor quando Stage\_ScrollX ($CD14) chega aos limites físicos da arena (\[$CCF0, $CCEC]): o canto esquerdo encosta em x = 0 sem ler coordenadas negativas e o canto direito encosta em x = width.
Em State 26, detectar o objeto do T-Rex ($0017 em $FFFFACBA) e deslocar x0 para 0 antes/durante sua entrada em cena, garantindo que o dinossauro saia colado na borda esquerda real em 16:9 e 21:9 sem corte traseiro flutuante.
🧪 Validações Executadas
Compilação: PASSOU — cmake --build build/native -j (exit code 0, sem warnings).
Inspeção visual (16:9 398x224 e 21:9 512x224): PASSOU —
s21\_rax\_blade\_16\_9.png (e capturas no início do round em CD14=324 e canto esquerdo CD14=0): zero faixas salmão/roxas laterais, canto esquerdo e sprite de RAX encostados em x = 0 e extensão contínua à direita sem duplicação do ventilador.
Larcen S27 (16:9 e 21:9) e s25\_shadow\_larcen\_16\_9.png (CD14=616): bordas e prédios/carro esquerdo preenchidos sem artefatos.
Overkill do Slash (State 26, f=7000..7500 em 16:9 e 21:9): T-Rex emergindo colado na borda esquerda real (x = 0), com wram\_diff=0/65536.
Lint/Typecheck: NÃO CONFIGURADO (projeto C/CMake sem alvo de linter separado; compilado com flags do build).
Testes completos do harness: PASSOU (17/17 filmes + 16/16 capturas widescreen 16:9 com wram\_diff=0/65536 e margins\_nz > 0 em todas as arenas; 1 divergência pré-existente em modo interp isolado no filme 08 interp\_diff=55/65536 / determinismo nativo 0/65536 em todos os 17/17) — python3 harness/ec\_harness.py --report-md /tmp/ws1b\_probe/harness\_report.md (exit code 0).
❓ Próximo Passo Proposto
Microtarefa 2 (Itens 2 e 2-B): Investigar e corrigir a Arena de Treino (State 14, $FD64 == 0x0E) para não sair da luta no 1º round rumo ao menu 1P em modo VS off-label, e desativar o efeito holográfico de piscar (30 Hz) no Player 2.
Aguardando confirmação do usuário para prosseguir.
prossiga

Compilando o executável nativo (cmake --build build/native -j) com os novos endereços de hook em game.toml e a lógica corrigida em ec\_boss.c. Aguardando a conclusão do build para executar a suíte de verificação da Microtarefa 2.

🎯 Estado Atual
Tarefa Concluída: Microtarefa 2 — Arena de Treino (State 14, $FD64 == 0x000E) multi-round em VS/Practice off-label (Item 2), desativação do flicker holográfico 30 Hz do Player 2 (Item 2-B) e diagnóstico de Overkill na arena de treino (Item 3).
Item Marcado: N/A — sem task.md
🛠️ Alterações Realizadas
game.toml: substituídos os hooks provisórios da Microtarefa 2 pelos 11 endereços reais no fluxo State 14 / Sub\_01EDD6 / Sub\_01F5EE / Sub\_01FAE6 (0x003986, 0x0039BA, 0x003BE8, 0x01EE6A, 0x01EF98, 0x01F166, 0x05AF4E, 0x01F5EE, 0x01FAE6, 0x01F6D6, 0x01F6E6).
ec\_boss.c e ec\_boss.c:
Generalização de saída (offlabel\_vs\_exit\_cleanup): unificada a saída de arenas off-label (Stage\_ExitCleanup $00C578) para suportar State 14 e State 18, retornando ao menu VS 2P após o fim do Best-of-N e avançando rounds ($9352) corretamente sem retornar ao menu 1P após o Round 1.
Preservação de opções VS em State14\_Handler (0x003986 / 0x0039BA): salva e restaura $936E (via $9B44), $977C, $9384, $9378 e $9383 em is\_vs\_offlabel\_holo() sem retorno prematuro de função.
Neutralização dos objetos exclusivos de treino 1P (0x01F5EE = $78, 0x01FAE6 = $7A): em VS/Practice off-label, a primeira execução da task oculta o objeto a5 (+$20 |= 0x80), marca deleção (+$74 |= 0x04) e encerra via recomp\_call\_addr(0x0000DC10u) (Task\_Exit), removendo placar e drone sem afetar State14\_Handler.
Fluxo normal de rounds/vitórias (0x01EE6A, 0x01EF98, 0x01F166, 0x05AF4E, 0x003BE8): evita os atalhos de saída única do modo 1P Holo-Trainer.
Item 2-B — Remoção do flicker holográfico 30 Hz do P2 (0x01F6D6, 0x01F6E6): limpa o bit 5 (0x20) de $FFFFAA72 e de objetos filhos ($FFFFAB5E + 0x20) logo após Sub\_01F5EE setá-lo em 0x01F6D0 / 0x01F6E0 (ativo sob --unlock-boss).
📌 Resposta ao Item 3 (Overkill na Arena de Treino)
Não há nenhum Overkill programado na ROM original para a Arena de Treino (State 14 = $0E, nem para o Battle Simulator State 17 = $11).
Em obj\_tasks4.asm, o dispatcher de Overkills loc\_026A16 percorre a tabela de bytes $026A36 ($13,$15,$16,$17,$18,$19,$1A,$1B,$14,$FF), que cobre apenas as 9 arenas principais (Midknight, Blade, Jetta, RAX, Trident, Shadow, Slash, Larcen, Xavier). Ao atingir $FF (o que ocorre em State 14, State 17 e State 18), loc\_026A30 retorna com carry = 0 (move.w #$0,ccr; rts).
🧪 Validações Executadas
Lint/Typecheck: NÃO CONFIGURADO — projeto C recompilado compilado diretamente com CMake/compilador nativo.
Build nativo: PASSOU — cmake --build build/native -j (exit code 0, regeneração completa de código e linkagem de EternalChampionsRecomp).
Suíte da Microtarefa 2 (m2\_verify.py): PASSOU (3/3 cenários) —
Caso A (VS 2P off-label State 14, Best-of-3 completo): R1 (st=14, p1w=0, p2w=0) → P1 vence → R2 (st=14, p1w=1, p2w=0, rnd=1) → P2 vence → R3 (st=14, p1w=1, p2w=1, rnd=2) → P1 vence → retorna ao Menu VS 2P (st=1, p1w=2, p2w=1, aa72=0000), sem retornar ao menu 1P; capturas visuais R1 par/ímpar, R2, R3 e tela final confirmadas.
Caso B (12\_holo\_trainer\_s14.ecm com --unlock-boss): AA72 manteve bit 5 zerado (aa72=0000 flicker\_bit=0) nos frames 2650, 2651, 2899 e 2900; capturas nos frames 2650 e 2651 confirmaram P2 sólido sem flicker e placar/drone intactos.
Caso C (12\_holo\_trainer\_s14.ecm sem --unlock-boss): PASSOU (wram\_diff=0/65536) contra s14\_holo\_trainer.ram, onde o bit 5 original permanece ativo (AA72 = 0x20).
Regressão rápida (python3 harness/ec\_harness.py --quick --report-md /tmp/m2\_holo\_probe/quick\_report.md): PASSOU (01\_attract\_boot.ecm e 02\_title\_to\_fight.ecm com det=0/65536, interp\_diff=0/65536, png\_eq=True, e WS 16:9 com wram\_diff=0/65536).
Observação sobre working tree: fight\_probe\_summary.md já continha atualização da execução anterior da Microtarefa 1B e não foi alterado nesta etapa; o diretório temporário /tmp/m2\_holo\_probe foi removido ao final.
❓ Próximo Passo Proposto
Microtarefa 3 (Item 4): investigar e implementar o Overkill customizado do Santuário (State 18, $FD64 == 0x12) reaproveitando a animação do Eternal Champion atravessando o vitral na área de ativação correspondente.
Aguardando confirmação do usuário para prosseguir.
antes:

anote mais um bug grave: eternal champion vs Midnight, quando midnight executa o golpe de drenagem de energia, os sprites bugam.

Savestate 4 e 9 == resultado
Savestate 6 == logo antes, basta fazer FRENTE(ou trás)+X+Y+Z tudo ao mesmo tempo encostado do adversario.

ao recarregar o savestate, observa-se que o midnight está travado com sprites estranhos, provavelmente alguma falha no carregamento dos sprites ou estouro dos mesmos.

anote e dê prioridade para esse, é bug grave.

depois pode seguir com o overkill

Iniciando a compilação do build nativo com 0x04EDD4 adicionado à lista \[functions] extra em game.toml e o saneador de tasks corrompidas em ec\_boss.c; aguardando a conclusão do build para executar a bateria de testes nos savestates 4, 6 e 9.

contine
contine

Compilando o build atualizado com a invalidação do cache de DMA de sprites ($12, a5) e executando a suíte de verificação completa nos savestates 4, 6 e 9 (scratch/m25\_verify.py). Aguardando a conclusão da execução.

Diagnóstico do Bug (Energy Drain do Midknight vs Eternal Champion)
Causa-raiz (loc\_04ED2A / loc\_04EDD4 no recompilador e loc\_05D9CA nos savestates 4 e 9):
No disparo de golpes especiais em fight\_moves2.asm, loc\_04ED2A desempilha o endereço de retorno do chamador em $04ED50: movea.l (a7)+, a0 (\_sp\_popped += 4 em func\_04ED2A).
Diferente dos outros 8 alvos de golpes dessa rotina (que já eram funções separadas e recebiam g\_split\_sp\_popped += \_sp\_popped antes do RECOMP\_TAIL\_NATIVE), o Energy Drain (0x04EDD4) era emitido inline (label\_04EDD4:) dentro de func\_04ED2A (ec\_part17.c antes da divisão).
Ao executar $04EDE0: jsr ($0000DB0E).l (Sub\_DB0E → loc\_05DE00 → Task\_Dispatch), o escalonador avançava até o 5º nó de task do Midknight ($E72C, que mantém 0x0005D8B2 — atualizador de sprites/colisão) deixando a4 = $E72C e sinalizando g\_rte\_pending = 1.
Ao retornar em func\_04ED2A, o bloco if (g\_rte\_pending) decrementava 1 → 0 sem somar \_sp\_popped / 4 (pois verificava apenas \_entry\_split\_sp\_popped e g\_split\_sp\_popped), devolvendo o controle para após bsr.w loc\_04ED2A com g\_rte\_pending == 0 e a4 == $E72C. O Task\_Yield seguinte sobrescrevia ($2, $E72C) com $04FF7C (save 9) e depois $04E5EE (save 4), destruindo loc\_05D8AC, deixando Midknight travado em 0x04EF16 e com a VRAM de sprites embaralhada e travada em cache (($12, a5) == ($10, a5)).
Correção aplicada:
Adicionado 0x04EDD4 em \[functions] extra no game.toml, separando func\_04EDD4 (ec\_part17.c) para transferir g\_split\_sp\_popped += \_sp\_popped no tail-call de $04ED9E e propagar o unwind de volta ao caller sem corromper $E72C.
Adicionada sanitize\_corrupted\_fighter\_tasks() em ec\_boss.c, chamada em ec\_boss\_vblank() e ec\_boss\_rb\_state\_load() (ec\_boss.c), que recupera automaticamente savestates legados corrompidos (native\_save\_4.bin e native\_save\_9.bin): restaura o 5º nó (t4) para 0x0005D8B2, invalida ($12, a5) = 0xFFFF nos dois lutadores para forçar re-DMA limpo na VRAM e desbloqueia t1 preso em 0x04EF16 com t2 já encerrado (0x00DC10/0x00DC14) via bit 7 de ($A8, a5).
Observação de build/validação: os savestates originais build/native/native\_save\_1..9.bin permaneceram intactos (todos os testes usaram cópias em /tmp/m25\_probe).
🎯 Estado Atual
Tarefa Concluída: Microtarefa 2.5 — Correção do Energy Drain do Midknight vs Eternal Champion ($04EDD4) + recuperação automática dos savestates legados 4 e 9.
Item Marcado: N/A — sem task.md.
🛠️ Alterações Realizadas
game.toml: inclusão de 0x04EDD4 na lista \[functions] extra.
ec\_boss.c: implementação de sanitize\_corrupted\_fighter\_tasks() e chamada em ec\_boss\_vblank() e ec\_boss\_rb\_state\_load().
🧪 Validações Executadas
Lint/Typecheck: NÃO CONFIGURADO no projeto (CMake C/C++ sem alvo separado de linter).
Build nativo: PASSOU (cmake --build build/native -j, exit code 0, func\_04EDD4 confirmada em ec\_part17.c).
Testes direcionados nos 3 savestates (m25\_verify.py, --unlock-boss em --headless com cópias dos saves + inspeção visual dos PNGs): PASSOU:
Save 6 (native\_save\_6.bin): aproximação até o Eternal Champion (x=(523,558)) + RIGHT + X + Y + Z disparou o Energy Drain (04EEBC -> 04EF16 -> 04E6CA), reduziu HP do P2 (68 -> 57), manteve E72C intacto em 05D8B2 o tempo todo e retornou ao controle livre (c1\_attack: pulo/ataque em y=138, sprites limpos em c1\_draining.png, c1\_after.png, c1\_attack.png).
Save 9 (native\_save\_9.bin): c2\_loaded restaurou imediatamente E72C para 05D8B2 com re-DMA limpo em c2\_loaded.png, concluiu o dreno até o fim (P2 hp=57, x=783, c2\_after com P1 em 04E6CA) e permitiu andar/atacar (c2\_walk: x=508, c2\_attack: 04E5EE) com sprites limpos.
Save 4 (native\_save\_4.bin): c3\_loaded destravou E6F6 (04EF16 -> 04E6CA) e E72C (04E5EE -> 05D8B2), já exibindo Midknight limpo em c3\_loaded.png, e permitiu andar (c3\_walk: x=556), pular e atacar (c3\_attack: y=101, 04FF7C).
Suíte de regressão: PASSOU (python3 harness/ec\_harness.py --quick, exit code 0: 01\_attract\_boot.ecm e 02\_title\_to\_fight.ecm com det=0/65536, interp\_diff=0/65536, png\_eq=True, \[WS 16:9] wram\_diff=0/65536).
❓ Próximo Passo Proposto
Microtarefa 3 (Item 4): implementar o Overkill customizado da arena do Santuário (State 18, $FD64 == $0012) aproveitando a janela/vitral pela qual o Eternal Champion escapa, sob --unlock-boss.
Aguardando confirmação do usuário para prosseguir.
compact
prossiga

Executei a implementação da Microtarefa 3 (Overkill do vitral 1 do Santuário em game/game.toml e game/ec\_boss.c) e concluí a compilação do binário nativo EternalChampionsRecomp. A bateria de testes determinísticos e de captura visual em m3\_verify.py está em execução em segundo plano (task-2105); aguardando o término para inspecionar as capturas e os dumps de RAM.

Corrigi a flag --widescreen 16:9 no script de verificação m3\_verify.py e iniciei a execução completa (task-2147); aguardando o término para inspecionar as capturas em 16:9 e rodar a suíte de regressão.

Inspecionei visualmente as capturas em widescreen 16:9 (c2b\_ec\_winner\_169\_t015..t200.png) confirmando voo, oclusão atrás do pilar esquerdo, estilhaços do obj $B4, abertura do vitral revelando o Plano B e extensão widescreen sem cortes. A suíte de regressão python3 harness/ec\_harness.py --quick está em execução (task-2156); aguardando o resultado para limpeza e relatório final.

🎯 Estado Atual
Tarefa Concluída: Microtarefa 3 (Item 4) — Overkill customizado no vitral 1 do Santuário (Estado 18, $FD64 == 0x0012) em modo VS/Torneio off-label com --unlock-boss.
Item Marcado: N/A — sem task.md.
🛠️ Alterações Realizadas
game/game.toml: adicionados 3 \[\[widescreen\_site]] (kind = "game\_hook"):
0x005818 (bne.b loc\_5822 em State18\_Handler): força o carregamento do grupo de tiles $B (Tile\_AllocGroup $00E408, antes restrito ao estado $06) e reinicia o estado do Overkill quando is\_vs\_offlabel\_sanctuary() está ativo.
0x026A16 (loop de estados em Sub\_0269E4): ativa o Overkill no vitral 1 no round decisivo sob golpe pesado ($128(a5) >= $B) quando a vítima está no chão e na zona esquerda (x ∈ \[$40..$F0] ou \[$28..$88] conforme o facing), inicializando câmera ($9C7A = 0x0004, $9C76 = 0x0017, $9C7C = 1, $9C7E = 1) e spawnando obj $75 (Sub\_01F3F2).
0x01F4FA (loc\_01F4FA dentro de Sub\_01F3F2): dirige o tick por frame da máquina de estados no contexto normal da CPU 68k.
game/ec\_boss.c, game/ec\_boss.c, game/ec\_boss.c, game/ec\_boss.c:
Adicionados SancOverkillState, helpers call\_68k\_sub(), call\_68k\_sfx(), call\_68k\_pal(), sanc\_apply\_window\_hole() e a máquina de 4 fases sanc\_overkill\_tick() (estritamente protegida por s\_unlock\_boss \&\& is\_vs\_offlabel\_sanctuary(), preservando EC\_BOSS\_RB\_STATE\_LEN == 7u e o fluxo do Boss 1P):
Fase 1: congela timer ($9CB4 = $FFFF), limpa VRAM de golpes especiais ($05B01A), garante grupo $B ($00E408), agenda DMA da folha $18 ($026954) e toca SFX $6A.
Fase 2: após o knockback inicial, interpola o arco da vítima até (0x0064, 0x00A4) aguardando fim do DMA ($9CA2 == 0) e pan da câmera ($CD1A <= 8).
Fase 3: toca SFX $2F, abre o buraco no Plano A com Sub\_026638 ($309DE, grupo $B) revelando o Plano B, spawna estilhaços obj $B4 ($00DCEA, paleta 0 do Santuário \~0x6000), eleva prioridade do pilar esquerdo com Sub\_0266AE para ocluir a vítima atrás da coluna, faz flash das cores $0B/$0D ($0EEE → $0AA4/$0EC8), treme a câmera e oculta a vítima ao sair da moldura.
Fase 4: aguarda os estilhaços encerrarem (52 frames), ajusta $CD1A = 0x0004 e chama Task\_TitleConfirm ($016F32) para pose de vitória e retorno ao menu VS.
🧪 Validações Executadas
Compilação: PASSOU (exit 0) — cmake --build build/native -j$(nproc) (\[100%] Built target EternalChampionsRecomp, sites 0x005818u, 0x01F4FAu e 0x026A16u confirmados em ec\_dispatch.c, ec\_part03.c, ec\_part28.c e ec\_part29.c).
Testes funcionais/visuais (scratch/m3\_verify.py com --unlock-boss): PASSOU (ALL\_SANCTUARY\_OVERKILL\_TESTS\_PASSED):
c1\_normal (Shadow vs Blade na zona esquerda, golpe forte str=0xB): $E210\[B]=0x009D, $9C7C=1, $9C7E=1, vítima oculta em (48, 151), $9C7C volta a 0, retorno limpo para st=1.
c2a\_ec\_victim (Shadow vs Eternal Champion id=12 como vítima na zona esquerda, str=0xB): Overkill concluído e retorno limpo para st=1.
c2b\_ec\_winner\_43 e c2b\_ec\_winner\_169 (Eternal Champion vs Blade em 4:3 e --widescreen 16:9): ambos completaram o Overkill, pose de vitória e retorno a st=1; capturas PNG inspecionadas confirmaram estilhaços obj $B4 na paleta 0, oclusão atrás do pilar esquerdo, abertura limpa revelando o Plano B e ausência de artefatos na extensão 16:9.
c3\_outside (Shadow vs Blade no centro em x=0x01E0, str=0xB): $9C7E=0 (não disparou falso positivo fora do vitral 1), KO normal e retorno limpo para st=1.
Lint/Typecheck: NÃO CONFIGURADO — projeto em C/Make sem linter separado configurado além dos warnings de compilação.
Testes de regressão (sem --unlock-boss): PASSOU (2/2 filmes + \[WS 16:9]) — python3 harness/ec\_harness.py --quick:
01\_attract\_boot.ecm: det=0/65536, interp\_diff=0/65536, png\_eq=True, misses=(0,0,0,0)
02\_title\_to\_fight.ecm: det=0/65536, interp\_diff=0/65536, png\_eq=True, misses=(0,0,0,0)
\[WS 16:9]: wram\_diff=0/65536 em título e luta.
Limpeza e integridade: /tmp/sanc\_view e /tmp/m3\_probe removidos; build/native/native\_save\_1..9.bin intactos; sem git commit.
❓ Próximo Passo Proposto
Todos os itens planejados desta sequência (1A/1B widescreen, 2/2-B Holo-Trainer, 2.5 Midknight Drain e 3 Overkill do Santuário) foram implementados e validados localmente.
Sugestão: testar interativamente no jogo (--unlock-boss --widescreen 16:9) e confirmar se deseja ajustes visuais finos ou se posso realizar o git commit das alterações em game/ec\_video.c, game/ec\_boss.c e game/game.toml.
Aguardando confirmação do usuário para prosseguir.

(Notificação tardia da tarefa de diagnóstico pré-compactação task-1647, já superada pelas etapas concluídas acima.)

Permaneço aguardando sua confirmação para eventuais ajustes ou git commit.

1. Verifique se é possível fazer o seguinte.. Fixar a barra de energia dos personagens no topo para ela nao se mover junto quando eles andam para os cantos do cenário. Outra coisa, existe uma linha no chão que é claramente um "frame" provavelmente para limitar a parte inferior. Se tiver como remover.

1B) Prioridade tambem: a documentação está completa? Especialmente com todos parametros do settings.ini e da linha de comando? a linha de comando nao aceita --help para listar os comandos. E nao temos menu in-game, mas tudo bem.

2. A partir daqui gostaria de revisar a implementação da renderização do audio. Se possível modificar o engine de audio para algo mais sofisticado, permitindo um som remasterizado. Poderia manter a camada de som original e uma outra por cima dando mais abertura, som estero mais imponente, implesmentacao de percussoes que praticamente nao existe nesse jogo, e outros elementos tornando a trilha sonora mais rica. O que você sugere? Ou seria o caso de extrair+remasterizar+reinserir em uma nova engine?  (grill-me)
3. Revisar a implementação dos engines de renderização de vídeo, pois a imagem melhora bastante com o render neural, mas ainda percebe-se o pixelamento de laguns personagens, talvez seria possível transformar isso em transparencia ou suavizar. O que vc sugere? Mais um upscale criativo/regenerativo mais extenso? Um filter? Um shader? embaixo do shader neural? em cima? Outra solução? Até onde conseguiremos ir com esse upscale? Podiamos fazer uma comparação de todos os renders lado a lado, certo? em diversas resoluções? Quem sabe implermentar shaders do  retrocade?  (grill-me)

3B) Sendo um pouco mais "ousado", existe a possibilidade de criar outro engine gráfico em cima, algo como um renderizador de sprites ocm maior resolução mantendo todos outros aspectos e proporções da tela? Aí substituiria completamente os sprites menores, através de um upscale criativo/regenerativo mais extenso.

4. Compilação para Windows, já temos tudo que precisamos?
5. acho que seria interessante ter um botão para captura de screenshots
6. Seria possível implementar uma opção no PAUSE para sair da luta e retornar ao menu anterior? (/grill-me)
volte para a pergunta 3 por favor

Auditoria confirmada — você tinha razão em desconfiar:

Status real de cada modo de renderização hoje no código
Modo	Flag / Config atual	Estado real em runtime
Modo A (Nativo)	padrão (320×224) + --widescreen 16:9 (398×224) + linear\_filter=0/1 + GENESIS\_SCREEN={raw,crt,trinitron,composite,linear}	Ativo e funcional. Apenas pixel nativo + LUT de cor (sem scanlines/de-dither).
Modo B (genesis\_hd\_gpu)	internal\_scale (1..64) em settings.ini ou GENESIS\_INTERNAL\_SCALE	Ativo no runner (main.c), mas faz só escala inteira limpa de tiles na GPU e fica desligado quando --neural-bplus está ativo.
Modo B+ (Neural 4×)	--neural-bplus, --bplus-backend <auto|cuda|gl>, --bplus-intensity <0..4>, --bplus-budget-ms	Ativo e funcional, mas com 2 desvios em relação ao planejado em docs/phase-4.md: (1) roda em full-frame já composto (não por camada separada — main.c passa raster = NULL), e (2) não tem pré-pass de de-dithering antes da rede (além de usar só 82k params e cair de CUDA → GL quando intensity != 1.0).
Modo C (Sprites/Planos HD)	--hd-manifest pipeline/manifest.json	NÃO ESTÁ RENDERIZANDO EM RUNTIME. Os 437 PNGs 4× aprovados existem em assets/hd/ (e experimentos em assets/hd\_remaster\_flux/, assets/hd\_remaster\_90scartoon/), mas ec\_video\_load\_hd\_manifest apenas lê o JSON, conta "original\_png" / "approved", imprime o log e libera o buffer. Nenhum PNG é carregado nem desenhado na tela — os 437 assets hoje só serviram offline para treinar o modelo B+.
Ordem registrada (conforme você definiu)
Amostras offline de áudio (WAVs A/B de 2–3 arenas para você ouvir).
Vídeo (comparativo dos modos atuais + correção/evolução do render neural / HD upscaler).
Decisão final sobre implementar o áudio no runner (e demais itens do backlog).

Voltando à Pergunta 3 (Vídeo) com esse diagnóstico real na mesa:

1 question

Para o Item 3 / 3B (Vídeo), como você quer conduzir o comparativo e a correção/evolução do renderizador?

Gerar o comparativo dos melhores modos possiveis VERSUS original (sem nada = software sem upscale sem filtro) + verificar o status, se é possível melhorar o que já foi feito + 2) acho que shaders "modulares" sempre é uma boa implementar, mas nao precisa ser agora mas se tiver um efeito muito bom pode ser... aí acho que vai depender do que vamos precisar de verdade (eu nao queria usar algo que já temos, queria algo novo, mas shaders talvez seja o ideal pra quem nao vai conseguir usar o neural pq eh pesado e depende de cuda) + 3) se o HD nao está renderizando como deveria por falha do outro agente que codou, por favor verifique para habilitarmos esse recurso.

por fim, acredito que a ordem de prioridade: 1) verificar o render do upscale de texturas HD + 2) verificar a rotina do driver de grafico neural upscale e verificar parametros de ajuste pra aumentar a depixelação e creative generation + 3) implementar as transparências (acho que isso vai dar um refinamento muito grande)

criar outro motor em cima do jogo acho arriscado por enquanto, vamos devagar. porque vai influenciar hitbox etc... o jogo já é meio precário nisso. (write-in)

Excelente — Item 3 / 3B (Vídeo) completamente alinhado (e ótima decisão sobre não construir um motor paralelo de gameplay para não mexer em hitboxes; o Modo C/HD entra estritamente como camada visual mantendo posições, flips e prioridades originais do VDP).

Falta apenas fechar o /grill-me do Item 6 (Opção de sair da luta no PAUSE).

Pela inspeção em main\_subs.asm (Main\_PauseLoop), a saída limpa para o menu é 100% viável e segura via hook nativo (sem patch na ROM): o jogo já congela $FD68=$80, lê os controles a cada quadro e o retorno via State\_SetState(1) (Sub\_FF08) reseta VRAM, objetos e heaps automaticamente (só precisamos despausar/parar o Z80 via $00092E/$00093C, fazer fade out e gravar $9CB6=1 + o submenu em $9CB2).

Para fechar o comportamento do Item 6 (Pause):

1 question

Como você prefere a interface e o destino ao sair da luta pelo PAUSE (Item 6)?

O que for causar menos risco de problemas. acho que seria o menu, talvez. mas isso deixa como finalização, baixa prioridade. (write-in)

Todas as decisões de design estão alinhadas. Segue o plano consolidado na ordem que definimos (seguindo 1 microtarefa por vez, com validação e confirmação entre etapas):

Roadmap Consolidado

Etapa 1 — Polimento de Tela, CLI/Docs e Screenshot (Itens 1, 1B e 5)

1A — HUD fixo + fim da linha preta no chão: em ec\_video.c, fixar sprites de HUD (($18,a0)==6) no topo em widescreen e ignorar o blanking de DMA ($D8 / scanlines 216..223) em arenas ativas (0 diff na WRAM).
1B — --help + documentação completa: implementar --help/-h em main.c (runner + g\_game\_spec.arg\_usage) e consolidar a documentação de todas as flags de CLI e chaves de settings.ini (app\_config.c).
1C — Hotkey de screenshot: adicionar atalho dedicado (ex.: F6 / PrintScreen) gravando PNG com timestamp em screenshots/.

Etapa 2 — Amostras Offline de Áudio (Item 2 — fase de audição)

Gerar via script Python WAVs comparativos A/B de 2–3 arenas (Original YM2612+PSG × DSP de estúdio × DSP + camada SoundFont/percussão via FluidSynth) para você ouvir e avaliar antes de qualquer alteração no runner C.

Etapa 3 — Vídeo: Comparativo, Modo C (HD), Neural B+ e Transparências (Itens 3 e 3B)

3A — Comparativo lado a lado: modos/parâmetros atuais × original puro (software, sem filtro/upscale) em múltiplas resoluções (720p/1080p/1440p/4K) + visualização dos PNGs HD de assets/hd\*.
3B — Habilitar renderização real das texturas/sprites HD (--hd-manifest / Modo C): substituir apenas a camada visual com os assets aprovados mantendo 100% das posições, flips, prioridades e hitboxes originais (fallback automático para B+/nativo; sem motor paralelo de gameplay).
3C — Revisão do Neural B+ (ec\_bplus\_ort / ec\_bplus\_gl): corrigir suporte/parâmetros para máxima depixelação e geração criativa (ex.: intensity no CUDA, capacidade/modelo e avaliação por camada).
3D — Transparências reais (De-dither): pré-pass (SGENPT-MIX/CBOD) embaixo do upscale convertendo dithering xadrez/barras em transparência suave. (Shaders modulares estilo RetroArch ficam como opção leve posterior para GPUs sem CUDA.)

Etapa 4 — Implementação definitiva do Áudio no Runner (decidida após sua audição da Etapa 2 e a conclusão da Etapa 3).

Etapa 5 — Compilação nativa para Windows (Item 4)

Guardar m/pthread/dl com if(UNIX) em game/CMakeLists.txt e portar ec\_bplus\_ort.c para Win32 (LoadLibraryA/onnxruntime.dll + fallback GL).

Etapa 6 — Finalização / Baixa prioridade (Item 6 — PAUSE → Sair para o menu)

Mini-menu visual seguro (RESUME / EXIT TO MENU) no Main\_PauseLoop via hook nativo sem alterar ROM.

Nota rápida: as alterações já validadas do ciclo anterior (Widescreen + T-Rex, Holo-Trainer, Midknight Drain e Overkill do Santuário) continuam sem commit no working tree.

Quer começar pela Etapa 1A (HUD fixo no topo + remover a linha preta no chão), prefere fazer commit do ciclo anterior antes, ou começar por outra etapa (ex.: Etapa 2 ou Etapa 3A)?

outra coisa, é possível dar uma reduzida leve no parametro de parallax da tela profunda? ela se mexe demais demais, e da um efeito negativo de profundidade, em vez de ser bacana, parece que o fundo se mexe sozinho, em vez de dar perspectiva. kkkk (por causa da area final do nosso cenario que fizemos o patch que ficou excelente, só esse detalhe que acabou ficando estranho, porque o jogo obviamente nao foi calculado para esse tamanho de cenario ultra wide screen!!!!) pode colocar isso tambem no fluxo onde melhor encaixar e nao for comprometer, porque podemos travar nisso simplesmente. Vamos ser realistas sobre o que é possível e o que não é, embora nesse momento eu acredito que você consegue kkkk

100% possível — e você matou a causa na mosca! O que você sentiu não é impressão sua: existe uma causa matemática exata para esse "efeito negativo de profundidade / fundo se mexendo sozinho" nas bordas do widescreen:

Causa-raiz exata
No 4:3 original, quando a câmera ($CD14) anda +Δx, o primeiro plano (Plano A) move -1,0 × Δx e o fundo distante (Plano B, calculado em util\_engine.asm loc\_F592 usando o fator r = ($28,a4) / 4096) move mais devagar (-r × Δx, ex.: -0,5 × Δx) → sensação correta de profundidade.
No nosso widescreen com ancoragem de borda (ec\_video.c), perto dos cantos da arena x0 desliza junto com $CD14 (dx = x0 - left) para travar o primeiro plano na borda da tela (0,0× movimento visual).
Como o jogo 68K continua aplicando r × Δx no Plano B para uma câmera 4:3, sobra um erro de (1 - r) × dx na tela: o primeiro plano fica completamente parado, mas o céu/montanhas continuam andando (e na direção invertida!), dando exatamente a sensação de "o fundo está se mexendo sozinho".
Encaixe no fluxo + realismo (o que dá e os limites)
Onde encaixa melhor: direto na Etapa 1A, junto com HUD fixo no topo e remoção da linha preta no chão ($D8 / y=216..223) — porque os 3 exigem exatamente a mesma coisa em ec\_video.c: renderizar a scanline widescreen inteira (0..width-1) diretamente do VDP em arenas ativas (em vez de dar memcpy na faixa central de 320 px já deslocada), 100% na apresentação, 0 diff na WRAM e 4:3 intocado.
O que é realista e seguro fazer:
Compensação exata de borda (+(1 - r) × dx): lendo o modo ($26,a4) e o fator ($28,a4) de cada camada do Plano B na lista $D84E (que ec\_video.c já percorre!), o fundo distante para junto com a câmera visual nas bordas (inclusive camadas estáticas r=0 e nuvens auto-scroll modo 16 mantêm só a deriva natural delas), eliminando 100% do parallax invertido sem costura.
Redução leve do parallax no meio da tela (se ainda achar rápido): também é possível aplicar um fator suave sobre o deslocamento do Plano B, mas sendo realista: em arenas com perspectiva de chão por scanline (($36,a4) != 0 em loc\_F94A, onde o chão acompanha os pés dos lutadores linha a linha), reduzir demais o scroll geral quebraria o alinhamento visual do chão com os personagens. Por isso, o caminho seguro é:
Aplicar a compensação exata de borda nas camadas de fundo distante (Plano B acima do piso / sem raster de chão), e
Validar visualmente primeiro — muito provavelmente só corrigir essa inversão nas bordas já resolve 90–100% do incômodo sem arriscar quebrar perspectiva de piso.

Etapa 1A atualizada (tudo em ec\_video.c, 1 arquivo, 0 WRAM diff):

HUD fixo no topo em widescreen (($18,a0)==6 ancorado em left).
Remoção da linha preta horizontal no chão (y >= 216, blanking $D8).
Compensação exata do parallax do Plano B nas bordas widescreen (eliminando o "fundo se mexendo sozinho").

Quer que eu faça o commit do ciclo anterior antes, ou já execute a Etapa 1A?

pode começar pela sua sugestão que é um trabalho pequeno e ja manda o comit. Outra coisa que ficou pendente, é o Overkill da tela de treinamento. Pode ser que fique sem, mas tem aquele robozinho da projeção e também os moving heads (hazards) que poderiam ser aproveitados exatamente no meio, soltar um raio, nao sei, e queimar a pessoa... ou eletrocutar igual na tela da Shadow.. enfim, vc pode me sugerir algo possivel tambem, pra gente nao estragar o jogo. Pensei tambem no movimento do robozinho do cenario do RAX que desintegra o adversario, só que aí seria o robozinho da projeção, pra deixar a cara dessa tela. Não tem muito mais o que possamos fazer. Tambem coloque como baixa prioridade, o finisher do cenario final ficou sensacional. Ah notei que as vezes quando explode a tela e o adversario está muito em cima, no caso eu estava com o eternal champion, ele falha o sprite (provavelmente nao recarrega depois). Não sei se dá pra consertar, mas se der é um bugfix pequeno, pode colocar tambem junto dessa listinha final de baixa prioridade.

Compilação concluída e suíte harness/ec\_harness.py --quick em execução em segundo plano; aguardando a conclusão para seguir para a inspeção visual e programática em /tmp/e1a\_verify/.

Rodando a verificação programática e geração de capturas em 16:9 para os slots de teste em /tmp/e1a\_verify/; aguardando a conclusão do comando.

Build concluído sem erros e harness/ec\_harness.py --quick em execução em segundo plano; aguardando a conclusão para rodar a verificação visual e de parallax em /tmp/e1a\_verify/.

🎯 Estado Atual
Commit do ciclo anterior: c4ea32c (feat: widescreen edge/T-Rex anchoring, Holo-Trainer VS flow, Midknight drain fix, and Sanctuary Window Overkill — 4 arquivos comitados).
Tarefa Concluída: Etapa 1A (HUD fixo no topo em widescreen, remoção da linha preta inferior do chão e compensação de parallax nas bordas).
Item Marcado: N/A — sem task.md ativo para esta fase.
🛠️ Alterações Realizadas
game/ec\_video.c:
HUD fixo no topo em widescreen (ec\_refresh\_frame\_hud\_sat, ec\_eval\_full\_sprites\_line): marca tanto o pool reservado na SAT ($D858 → SAT\[0..($DF7A)-2], barras de vida e gemas de round) quanto os objetos screen-space em $ACBA com ($18,a0) == 6 (yin-yangs, timer e avisos), ancorando-os em left = (width - native\_w)/2 enquanto o viewport x0 desliza até os cantos da arena.
Remoção da linha preta horizontal no chão (ec\_render\_ws\_scanline, ec\_render\_ws\_scanline\_impl): ignora o blanking de DMA na linha 216 ($D8 quando $9B32 != 0) se o quadro iniciou com display ativo (s\_frame\_had\_display), e quando linhas >= 216 expõem tiles totalmente transparentes no fim da nametable (ex.: tiles 0x4DB no Santuário e 0x485 no Vietnã com $CD16 = 23 a partir de y = 217), preenche a faixa de planos com espelhamento contínuo do último piso opaco (s\_last\_floor\_line), mantendo a camada real de sprites intacta na scanline original.
Compensação exata de parallax nas bordas (ec\_plane\_edge\_comp): percorre a cadeia $FFFFD84E e compensa dx = x0 - left nos Planos A e B usando a última camada ativa aplicável (com sinal real de ($28,a4)/4096 para camadas hm=0x0C, modo 0x04/0x14 e preservação de tabelas raster $36,a4), eliminando o movimento invertido do fundo distante ao encostar nas laterais da arena.
game/ec\_video.c: ec\_video\_scanline\_lazy passa a renderizar a linha widescreen completa nas arenas ativas (14, 15, 17..27) mantendo o pillarbox intacto nos menus e deixando o modo 4:3 intocado.
docs/frame\_data/fight\_probe\_summary.md: atualizado automaticamente pelo ec\_harness.py --quick.
🧪 Validações Executadas
Lint/Typecheck: NÃO CONFIGURADO no projeto; compilação C11 sem erros ou novos warnings.
Compilação: PASSOU — cmake --build build/native -j$(nproc) (\[100%] Built target EternalChampionsRecomp, exit 0).
Paridade e Widescreen: PASSOU — python3 harness/ec\_harness.py --quick:
01\_attract\_boot.ecm: det=0/65536, interp\_diff=0/65536, png\_eq=True, misses=(0,0,0,0)
02\_title\_to\_fight.ecm: det=0/65536, interp\_diff=0/65536, png\_eq=True, misses=(0,0,0,0)
\[WS 16:9]: title\_16\_9.png 398x224 margins\_nz=(0,0) wram\_diff=0/65536; fight\_16\_9.png 398x224 margins\_nz=(8593,8461) wram\_diff=0/65536.
Inspeção visual/pixel 16:9 em cenários reais (removido /tmp/e1a\_verify/ após checagem): PASSOU —
native\_save\_1 (Options): margins\_nz=(0,0) (pillarbox preservado).
native\_save\_2 (Santuário, x0=0), native\_save\_6 (Vietnã) e native\_save\_7 (Salem): bottom\_black\_rows=\[] nos 3 estágios; HUD completo (yin-yangs, barras de vida, timer 99 e gemas) alinhado no centro superior mesmo em x0=0.
Caminhada nas bordas (s2 esquerda x0: 3→33 e s6 direita x0: 56→78): 0 px de deriva relativa entre Plano A e Plano B e HUD estável sem sobreposição nem costuras.

Anotados no backlog de baixa prioridade: (1) Overkill da tela de treino (Holo-Trainer: robô da projeção / moving heads desintegrando ou eletrocutando no centro), e (2) bugfix pontual de recarga de sprite após explosão no Santuário quando o vencedor (ex.: Eternal Champion) está muito próximo no disparo.

❓ Próximo Passo Proposto
Etapa 1B: implementar --help / -h no runner (segagenesisrecomp/runner/main.c) exibindo as flags do runner + g\_game\_spec.arg\_usage e consolidar a documentação completa de CLI e settings.ini (ou fazer commit da Etapa 1A antes, se preferir).
Aguardando confirmação do usuário para prosseguir.
faz o commit, deixa essa parte de documentação pra finaleira tambem

Commit da Etapa 1A realizado: 939e69c (game/ec\_video.c e docs/frame\_data/fight\_probe\_summary.md, 2 files changed, 325 insertions(+), 78 deletions(-)).

Confirmado também: Etapa 1B (--help e documentação completa de CLI / settings.ini) movida para o backlog final, junto com os itens de baixa prioridade (PAUSE → sair para o menu, Overkill do Holo-Trainer e recarga de sprite pós-explosão no Santuário).

❓ Próximo Passo Proposto

Escolha por onde quer seguir agora:

Etapa 1C (rápida): adicionar hotkey dedicada para captura de screenshots (PNG com timestamp em screenshots/) em segagenesisrecomp/runner/main.c.
Ou pular direto para a Etapa 2: gerar as amostras offline de áudio (WAVs A/B de 2–3 arenas: Original YM2612+PSG × DSP de estúdio × DSP + SoundFont/percussão via FluidSynth) para você ouvir antes de avançarmos no vídeo.

Aguardando confirmação para prosseguir.

pode fazer as duas. hotkey = F12

Edições da hotkey F12 aplicadas em segagenesisrecomp/runner/main.c; aguardando a conclusão da captura de áudio/eventos (task-2437) antes de recompilar o executável nativo e gerar os protótipos WAV.

🎯 Estado Atual
Tarefas Concluídas: Etapa 1C (F12 screenshot hotkey) e Etapa 2 (protótipo offline A/B de áudio remasterizado: 3 trilhas × 4 variantes em assets/audio\_prototypes/).
Item Marcado: N/A — sem task.md ativo.
🛠️ Alterações Realizadas

segagenesisrecomp/runner/main.c:

Hotkey F12 (L17-L26, L1787-L1844, L3741, L3834-L3842, L4530-L4556): cria o diretório screenshots/ (via \_mkdir/mkdir), salva capturas em ec\_YYYYMMDD\_HHMMSS.png (com sufixo \_01..\_99 em colisões) tentando SDL\_RenderReadPixels no backbuffer apresentado e com fallback em s\_framebuf. Shift+F12 / Ctrl+F12 preservam o dump de diagnóstico (chip\_ring.txt / snd\_ring.txt).

Amostras offline de áudio (48000 Hz, 16-bit estéreo, 22.20 s, loudness equalizado em -16.50 dBFS RMS): geradas via libfluidsynth.so.3 (/usr/share/sounds/sf2/TimGM6mb.sf2) guiado pelos registradores YM2612/PSG sincronizados + ffmpeg:

1. 01\_vietnam\_midknight (native\_save\_6, eventos: bass=69, pad=95, lead=98, psg\_tone=496, fm\_perc=37, psg\_perc=41):

A\_original.wav — peak -2.24 dBFS, largura estéreo 0.015 (chip puro reamostrado)
B\_studio\_dsp.wav — peak -3.40 dBFS, largura 0.194 (DSP puro: EQ, controle de médios FM, reflexões L/R + reverb de sala sem SoundFont)
C\_hybrid\_sf2\_balanced.wav — peak -2.01 dBFS, largura 0.664 (híbrido \~65% B + SF2 Finger Bass / Strings / Synth Brass / Standard Kit)
D\_hybrid\_sf2\_cinematic.wav — peak -1.71 dBFS, largura 1.001 (híbrido arrojado \~36% B + SF2 Synth Bass / Slow Strings / Brass Section / Power Kit)

2. 02\_salem\_xavier (native\_save\_7, eventos: bass=0, pad=130, lead=61, fm\_perc=77, psg\_perc=9):

A\_original.wav — peak -3.26 dBFS, largura 0.026
B\_studio\_dsp.wav — peak -3.46 dBFS, largura 0.217
C\_hybrid\_sf2\_balanced.wav — peak -1.55 dBFS, largura 0.687
D\_hybrid\_sf2\_cinematic.wav — peak -1.06 dBFS, largura 1.031

3. 03\_syria\_blade (native\_save\_3, eventos: bass=101, pad=25, lead=252, psg\_tone=456, fm\_perc=163, psg\_perc=42):

A\_original.wav — peak -1.35 dBFS, largura 0.020
B\_studio\_dsp.wav — peak -2.36 dBFS, largura 0.215
C\_hybrid\_sf2\_balanced.wav — peak -2.17 dBFS, largura 0.518
D\_hybrid\_sf2\_cinematic.wav — peak -1.44 dBFS, largura 0.475
🧪 Validações Executadas
Lint/Typecheck: NÃO CONFIGURADO no projeto.
Compilação: PASSOU — cmake --build build/native -j$(nproc) (\[100%] Built target EternalChampionsRecomp, exit 0; apenas nota -Wformat-truncation pré-existente no inline de exe\_relative em main.c:207).
Paridade determinística: PASSOU — python3 harness/ec\_harness.py --quick (det=0/65536, interp\_diff=0/65536, png\_eq=True, misses=(0,0,0,0), \[WS 16:9] wram\_diff=0/65536).
Rota SCREENSHOT headless pós-build: PASSOU (rc=0, PNG de 215.347 bytes gravado; note que o disparo interativo por tecla SDLK\_F12 em janela gráfica SDL não foi testado interativamente).
Prototipagem WAV: PASSOU — todos os 12/12 WAVs validados (48000 Hz, 2 ch, 16-bit, -16.50 dBFS RMS, zero clipping) e diretório temporário /tmp/audio\_proto removido.
❓ Próximo Passo Proposto

Enquanto você ouve as amostras A/B/C/D (observando principalmente equilíbrio entre chip original, abertura estéreo, punch dos graves/percussão e fidelidade dos timbres), podemos iniciar a parte de vídeo:

Etapa 3A: auditar e gerar o comparativo visual lado a lado dos modos de renderização existentes (Original puro sem filtro × ColorLUT × Modo B / internal\_scale × Modo B+ com diferentes intensidades/backends) + inspeção dos assets HD já existentes em assets/hd\*, para identificar exatamente o que habilitar/corrigir no Modo C (HD), no Modo B+ e nas transparências (de-dither).

Aguardando confirmação do usuário para prosseguir.

ja aprovei essa pegada de synths, mas preciso equalizar individualmente, e no caso da blade, ele escolheu percussao em algum elemento que nao é pra ser, pq ta fora do ritmo, mas aí a gente analisa elemento por elemento, vc me explica como funciona detalhadamente. mas é por aí sim! fechou. agora vamos pro driver de video

Capturando as 3 cenas reais de luta (s6\_vietnam, s7\_salem, s2\_sanctuary), executando o benchmark de 120 frames em GPU (Modo B+ GL e CUDA ORT) e gerando as pranchas comparativas em assets/video\_compare/. Aguardando a conclusão da tarefa em segundo plano.

Auditoria da Etapa 3A concluída sem alterar código do repositório nem savestates (git status -s --untracked-files=no limpo; build/native/native\_save\_1..9.bin intactos). Abaixo estão os achados reais do código/GPU, as comparações visuais geradas e a recomendação técnica antes de iniciarmos a Etapa 3B.

1. Diagnóstico Real dos Drivers de Vídeo Atuais

Por que personagens continuam pixelados e o dithering do Mega Drive aparece mesmo com --neural-bplus:

Student leve demais (ECStudentESPCN4x): apenas 6 camadas conv (40 canais, 2 blocos residuais, 82.344 parâmetros em neural/train\_espcn.py). O campo receptivo efetivo é pequeno (\~13×13 px nativos), insuficiente para regenerar texturas internas ou dissolver dithering em áreas largas — ele suaviza bem os contornos/diagonais externos, mas mantém os blocos internos e o quadriculado (2×2) do fundo e das sombras.
Teacher antigo vs. Flux atual: os checkpoints em assets/models/ (2026-11-19) foram destilados de 4x\_IllustrationJaNai\_V1\_DAT2\_190k.pth (pixel-art/DAT2), antes da geração dos 437 assets Flux HD em assets/hd/ (2026-11-20, silhouette\_iou 0.978..0.999).
Pós-processamento opaco em frame inteiro: segagenesisrecomp/runner/main.c passa apenas present\_src composto (raster = NULL) para ec\_video\_present\_gpu() em game/ec\_video.c. A rede não separa sprite de cenário nem tem pré-passe de de-dither.
--bplus-intensity (1.5 / 2.0) não aumenta detalhe criativo: no shader ec\_bplus\_gl.c, apenas multiplica base + delta \* u\_intensity. Acima de 1.0, cria halos/overshoot escuros e claros sem resolver a pixelação interna.

Estado real de cada modo e backend medido hoje na RTX 5060 Ti (WSL GALLIUM\_DRIVER=d3d12):

Modo A (Nativo / ColorLUT): 398×224 em 16:9 (GENESIS\_SCREEN=trinitron|crt aplica apenas LUT de cor por pixel; ColorLUT compõe antes do Modo B+ sem custo de GPU).
Modo B (genesis\_hd\_gpu, GENESIS\_INTERNAL\_SCALE=4): funciona em 4:3 (1280×896 confirmado no log \[HD\_GPU]), mas é desativado em --widescreen por !s\_video\_frame.owner em main.c, além de fazer apenas rasterização inteira dos tiles sem IA.
Modo B+ OpenGL Compute (ec\_bplus\_gl.c): 398×224 -> 1592×896, paridade medida contra PyTorch FP32 max\_abs\_rgb\_u8 = 1 (PSNR = 62.41 dB). Benchmark real de 120+ frames em s6\_vietnam:
Async (GL\_TIME\_ELAPSED): p50 = 6.69 ms, p99 = 10.84 ms, mean = 8.91 ms (0 fallbacks).
Sync (EC\_BPLUS\_SYNC\_TIMING=1, pós-warmup após frame 10): p50 = 8.23 ms, mean = 9.21 ms, max = 12.58 ms.
Modo B+ CUDA ORT (ec\_bplus\_ort.c): 398×224 -> 1592×896 com ec\_bplus\_espcn4x\_fp16.onnx:
Benchmark real de 127 frames: p50 = 13.20 ms, p99 = 18.14 ms, mean = 18.62 ms (0 fallbacks; ainda ignora --bplus-intensity != 1.0, caindo para GL). Hoje é mais lento que GL Compute porque faz upload/download CPU↔GPU por frame (CreateTensorWithDataAsOrtValue na CPU + conversão RGBA↔planar NCHW + SDL\_UpdateTexture), enquanto o GL Compute roda 100% na VRAM.
Modo C (--hd-manifest): apenas stub em ec\_video\_load\_hd\_manifest — só conta strings no JSON e não carrega/desenha nenhum PNG HD em runtime.
2. Pranchas Visuais Geradas (Clique para Abrir e Inspecionar)

Todas geradas a partir de execuções reais de EternalChampionsRecomp (capturas individuais também disponíveis em assets/video\_compare/frames/):

Modos atuais lado a lado + zoom 2× na área crítica (Original Nearest, Trinitron LUT, Bilinear i=0.0, Neural B+ i=1.0, 1.5, 2.0):
Vietnam / Midknight (native\_save\_6.bin): compare\_modes\_s6\_vietnam.png
Salem / Xavier (native\_save\_7.bin): compare\_modes\_s7\_salem.png
Sanctuary / Eternal Champion (native\_save\_2.bin): compare\_modes\_s2\_sanctuary.png
Resoluções (720p, 1080p, 1440p, 4K) — Original Nearest vs B+ 1.0 vs B+ 1.5:
compare\_resolutions\_s6.png
Achado visual: até 1080p (1.21× sobre 1592×896), o 4× acompanha a tela; em 1440p (1.61×) e 4K (2.41×), o bilinear secundário do SDL amacia sem acrescentar detalhe e o quadriculado de dithering continua nítido.
Comparação dos assets offline já existentes vs Student B+ (Original 4× | Neural B+ 82k | DAT2 | Flux HD | 90s Cartoon):
Sprites: compare\_hd\_styles\_sprites.png
Planos de cenário: compare\_hd\_styles\_planes.png
Achado visual: a diferença entre o B+ 82k atual e o Flux HD (assets/hd/) é enorme — o Flux remove praticamente todo o dithering nos cenários (Holo-Trainer, Jetta, Xavier, Midknight) e nas vestes/pele dos sprites, mantendo a silhueta (IoU \~0.98), enquanto o DAT2 cria rachaduras em áreas ruidosas (d17fea01717e00f0).
3. Respostas Diretas às Perguntas 3 e 3B (Trade-offs e Recomendação)
Sobre criar "outro engine gráfico em cima" vs. Modo C:
Contra uma engine gráfica paralela/desacoplada: duplicaria o estado de câmera, prioridades VDP, flips, scroll linha-a-linha (HScroll), Shadow/Highlight (sh\_mode) e arriscaria dessincronizar visual vs. hitbox/hurtbox real.
A favor do Modo C sobre o VDP original (Recomendação definitiva): substituir apenas as texturas visuais (sprites e planes por hash canônico) exatamente nas mesmas coordenadas/flips/prioridades que o VDP já calcula em ec\_video.c, com fallback instantâneo para Modo B+ / De-dither quando um frame/sprite novo não estiver no catálogo HD. Zero risco de quebrar hitboxes, física ou paridade (0/65536 WRAM intacto).
Onde entram filtros/shaders (De-dither, B+ mais forte, RetroArch/CRT):
EMBAIXO do neural/upscale (pré-passe, no 398×224 nativo): De-dither (SGENPT-MIX / CBOD) — detecta pares verticais e quadriculados 2×2 de dithering do Mega Drive antes de ampliar 4× e converte em mistura/transparência suave real (ex.: sombras translúcidas no chão, fogueira de Salem, degradê de céu). Se rodar depois do B+, o padrão já foi alargado para blocos 4×4 e não é mais removível com limpeza.
No próprio neural (Modo B+): retreinar um Student mais capaz no CUDA (--backend gl compute ou CUDA GPU-resident sem ida e volta pela CPU, ou --scale 4 + pós-sharpen leve tipo CAS em 4K) usando o Flux HD / 90s Cartoon como teacher + saída do pré-passe de de-dither, dando um salto gigantesco mesmo nos sprites ainda não cobertos pelo Modo C. (Nota: os 437 assets aprovados atuais cobrem todos os 410 sprites e 27 planos extraídos até agora; quadros inéditos de luta usam o Modo B+ aprimorado e poderão ser extraídos/gerados em lote depois se você quiser cobrir 100% dos golpes.)
ACIMA do B+ / Modo C (último passe de apresentação): shaders cosméticos opcionais estilo RetroArch (CAS/RCAS para nitidez em 1440p/4K, ou scanlines/máscara CRT leves em GL pós-upscale). FX pesada antes do neural estraga a entrada da rede.
Próximo Passo Proposto — Etapa 3B

Seguindo o plano aprovado, o próximo passo é Etapa 3B (Modo C runtime via --hd-manifest), ou, caso prefira ver resultado imediato em 100% dos frames de luta mesmo sem expandir os 410 sprites ainda, podemos iniciar pela Etapa 3D (De-dither real em shader/pixel pipeline) / 3C (Upgrade do Student B+ com Flux HD + suporte CUDA sem ida-e-volta CPU).

Por favor, confirme após olhar as pranchas acima:

Qual estilo visual você prefere como alvo para Modo C e para destilar o novo Modo B+: Flux HD (assets/hd/) ou 90s Cartoon (assets/hd\_remaster\_90scartoon/)?
Posso prosseguir com a Etapa 3B (implementar o carregador/renderizador real de sprites e planos HD do --hd-manifest mantendo fallback automático para B+)?





Recuperei o estado exato da sessão anterior (Resolução de Problemas da Proposta), encerrada às 05:50 (pouco antes da atualização de graphify às 06:01; nenhum graphify-out/ chegou a ser criado neste repositório).

Onde paramos

A Etapa 3A (Auditoria dos Drivers de Vídeo) foi concluída sem alterações no repositório nem nos savestates (build/native/native\_save\_1..9.bin intactos), e o fluxo pausou aguardando sua decisão sobre o próximo passo (Etapa 3B / 3C / 3D).

Resumo do diagnóstico concluído:

Modo B+ atual (ECStudentESPCN4x, 82k params): destilado do teacher antigo (DAT2), roda em frame inteiro composto sem de-dither prévio — suaviza contornos externos, mas mantém pixelação interna e quadriculado 2×2 de dithering. --bplus-intensity > 1.0 só amplifica o residual (base + delta \* intensity), gerando halos.

Benchmarks reais (RTX 5060 Ti, WSL GALLIUM\_DRIVER=d3d12, 120+ frames 398×224 → 1592×896):

B+ GL Compute: p50 = 6.69 ms (async) / 8.23 ms (sync pós-warmup), PSNR = 62.41 dB vs PyTorch FP32 (max\_abs = 1), 0 fallbacks.

B+ CUDA ORT (FP16): p50 = 13.20 ms (mais lento que GL hoje pelo ida-e-volta CPU↔GPU por frame).

Modo B (GENESIS\_INTERNAL\_SCALE=4): funciona em 4:3, mas fica desativado em --widescreen.

Modo C (--hd-manifest): atualmente é apenas stub em game/ec\_video.c (não carrega/desenha PNGs HD em runtime).

Pranchas comparativas já prontas em assets/video\_compare/:

Modos atuais lado a lado + zoom 2×:

compare\_modes\_s6\_vietnam.png

compare\_modes\_s7\_salem.png

compare\_modes\_s2\_sanctuary.png

Resoluções (720p / 1080p / 1440p / 4K): compare\_resolutions\_s6.png

Estilos HD offline (Original 4× | B+ 82k | DAT2 | Flux HD | 90s Cartoon):

Sprites: compare\_hd\_styles\_sprites.png

Planos: compare\_hd\_styles\_planes.png

Decisões pendentes para eu continuar agora

Estilo alvo para o Modo C e para destilar o novo Modo B+: Flux HD (assets/hd/) ou 90s Cartoon (assets/hd\_remaster\_90scartoon/)?

Ordem de ataque:

Etapa 3B: implementar o carregador/renderizador real do Modo C (--hd-manifest) sobre as coordenadas/prioridades do VDP (com fallback automático para B+), ou

Etapa 3D → 3C primeiro: pré-passe de De-dither (SGENPT-MIX/CBOD) no 398×224 nativo + upgrade do Student B+ destilado do novo teacher (efeito imediato em 100% dos quadros de luta)?

(Se também quiser gerar o grafo do repositório com /graphify antes de mexer no vídeo, basta avisar.)

acho que só vendo para saber qual o melhor. impossivel saber antes. o pre dither é para aplicar as transparencias certo? acho que seria bom que tivesse. e depois aplicar as texturas novas. é isso?

Sim — no caminho neural (Modo B+), é exatamente essa a ordem, com um detalhe técnico importante para as texturas HD (Modo C):

1. Como o pré-dither vira transparência real

No Mega Drive o VDP não tem canal alpha (só pixel 100% opaco ou 100% transparente). Para simular sombras translúcidas, fogo, neblina e degradês, o jogo alterna pixels em colunas (A-B-A-B) ou quadriculado 2×2 (checkerboard), contando com o borramento da TV CRT.

O pré-dither (SGENPT-MIX / CBOD) roda no quadro nativo (398×224) antes do upscale, detecta esses padrões alternados e mistura as cores vizinhas — convertendo o quadriculado em transparência/degradê suave de verdade sem borrar o restante da imagem (textos/contornos rígidos ficam preservados por limiar).

2. Ajuste na ordem entre Pré-dither e Texturas Novas

No caminho neural (Modo B+ — vale para 100% dos frames): VDP nativo (398×224) → Pré-dither (transparências reais) → Upscale Neural (Modo B+ 4×) → (opcional: nitidez leve/CAS ou CRT no final).

No caminho de texturas prontas (Modo C, --hd-manifest): As texturas em assets/hd/ já estão em 4× e o Flux já dissolveu o dithering delas. Por isso, o lookup do Modo C identifica o sprite/plano pelos tiles/hash originais do VDP (antes do de-dither, senão o hash não casa com pipeline/manifest.json) e aplica a textura 4× direto na composição final — enquanto qualquer sprite/frame sem textura HD cai automaticamente no fluxo Pré-dither → Modo B+.

Plano sugerido para você ver rodando ao vivo (incremental)

Etapa 1 — Pré-dither em GPU (SGENPT-MIX / CBOD) antes do Modo B+:

Arquivos afetados: neural/infer/ec\_bplus\_gl.c, neural/infer/ec\_bplus\_gl.h, game/ec\_video.c, game/ec\_video.h, game/ec\_spec.c.

Contrato / edge cases: opt-in via --bplus-dedither <off|cbod|both> (ou EC\_BPLUS\_DEDITHER), padrão seguro mantendo o modo sem --neural-bplus e WRAM 0/65536 intactos; preservando HUD/texto nítido via detecção estrita de padrão 2×2 / listra vertical.

Verificação: build em build/native, python3 harness/ec\_[harness.py](http://harness.py) --quick, benchmark de latência GPU e prancha visual Antes vs Com Pré-dither em Vietnam/Salem/Sanctuary (além de você testar jogando ao vivo).

Etapa 2 — Retreinar Modo B+ v2 (Flux HD + entrada já com pré-dither):

Treina na sua RTX 5060 Ti (\~1–2 min) sobre os 437 pares Flux HD existentes em assets/hd/, gerando um .bin separado (sem sobrescrever o atual) para você comparar ao vivo trocando apenas --bplus-model.

Etapa 3 — Modo C (--hd-manifest) com texturas novas 4×:

Carregamento e composição real dos planos/sprites 4× aprovados direto sobre as camadas do VDP, com fallback para Pré-dither + B+ v2.

Posso iniciar pela Etapa 1 (Pré-dither em GPU antes do Modo B+) agora?

pode completar essa etapa grafica.

Build concluído (EternalChampionsRecomp compilado sem novos warnings); aguardando o término do harness/ec\_[harness.py](http://harness.py) --quick em segundo plano para rodar em seguida a captura/benchmark GPU e a inspeção visual do pré-dither.

ec\_[harness.py](http://harness.py) --quick passou (0/65536 WRAM diff, png\_eq=True, zero misses). Executando agora as capturas reais em GPU (off vs cbod vs both), benchmark de 120 frames e geração da prancha comparativa (task-86) em segundo plano.

🎯 Estado Atual

Tarefa Concluída: Etapa 1 — Pré-dither em GPU (SGENPT-MIX / CBOD) antes do Modo B+ (GL Compute), convertendo quadriculados 2×2 e listras verticais de pseudo-transparência do Mega Drive em transparências/degradês suaves no frame nativo (398×224) antes das convoluções neurais e da base bilinear 4×.

Item Marcado: N/A — sem item correspondente em [task.md](http://task.md) (etapa do ciclo gráfico aprovado).

🛠️ Alterações Realizadas

neural/infer/ec\_bplus\_gl.h: adicionado EcBplusDeditherMode (OFF, CBOD, BOTH) e ec\_bplus\_gl\_set\_dedither().

neural/infer/ec\_bplus\_gl.c: adicionado compute shader GLSL 4.30 s\_dedither\_cs (detecção por quadrantes 2×2, malha sobre fundo texturizado para sombras no chão sem afetar contornos diagonais 45° nem pontos isolados, e listras verticais SGENPT-MIX com confirmação período-2 preservando HUD/texto), textura intermediária tex\_dedither (GL\_RGBA16F), e encadeamento antes da camada 0 da CNN e da base bilinear em up\_prog. Com dedither=off, zero passos extras (bit-idêntico).

game/ec\_video.h e game/ec\_video.c: parser/configuração --bplus-dedither e env EC\_BPLUS\_DEDITHER (off|cbod|both), roteando automaticamente para GL Compute quando ativo.

game/ec\_spec.c: suporte à flag CLI --bplus-dedither <off|cbod|both> e atualização de .arg\_usage.

🧪 Validações Executadas

Compilação: PASSOU (zero warnings novos nos arquivos alterados) — cmake --build build/native -j$(nproc)

Determinismo / Regressão: PASSOU — python3 harness/ec\_[harness.py](http://harness.py) --quick (01\_attract\_boot.ecm e 02\_title\_to\_fight.ecm com det=0/65536, png\_eq=True, misses=(0,0,0,0), interp\_diff=0/65536; WS 16:9 com wram\_diff=0/65536).

Paridade dedither=off: PASSOU — max\_abs\_diff = 0 nas 3 cenas (s6\_vietnam, s7\_salem, s2\_sanctuary) contra a captura base anterior.

Benchmark GPU (127 frames 398×224 → 1592×896, RTX 5060 Ti / WSL D3D12, 0 fallbacks):

Async (GL\_TIME\_ELAPSED): off = 6.60 ms → cbod = 6.62 ms (+0.02 ms) → both = 6.67 ms (+0.07 ms).

Sync (EC\_BPLUS\_SYNC\_TIMING=1): off = 7.89 ms → both = 8.21 ms (+0.32 ms).

Inspeção Visual: prancha comparativa gerada e verificada em compare\_dedither.png — sombras no chão de Vietnam, túnica/céu de Salem e mármore/janelas do Sanctuary ficam lisos e translúcidos, mantendo barras de vida, timer e contornos nítidos.

Para testar ao vivo agora (WSL):

bash

GALLIUM\_DRIVER=d3d12 ./build/native/EternalChampionsRecomp roms/eternal\_champions\_us.gen --no-launcher --widescreen 16:9 --neural-bplus --bplus-dedither both --unlock-boss

❓ Próximo Passo Proposto

Etapa 2: Retreinar Modo B+ v2 (Student mais capaz sobre os 437 pares Flux HD de assets/hd/ com entrada já passada pelo mesmo algoritmo de pré-dither), exportando um .bin separado para comparar ao vivo via --bplus-model sem sobrescrever o modelo atual.

Aguardando confirmação do usuário para prosseguir (ou, se preferir pular direto para a Etapa 3 — Modo C --hd-manifest ou fazer ajustes no pré-dither após testar ao vivo, só avisar).

prossiga

Treinamento do Modo B+ v2 (ch=48, 4 blocos residuais, \~196k parâmetros) iniciado na RTX 5060 Ti (task-112) sobre cenas opacas compostas (plane\_b + plane\_a + sprites dos 437 pares Flux HD) com o mesmo pré-dither da GPU, seguido da exportação para assets/models/ec\_bplus\_v2\_flux\_weights.bin (sem alterar o modelo v1 atual). Aguardando conclusão em segundo plano.

Treinamento concluído (PSNR = 22.35 dB em cenas opacas compostas) e exportação ONNX + ECBP v1 (assets/models/ec\_bplus\_v2\_flux\_weights.bin) validada com paridade 146.25 dB (det\_max\_err = 0.0). Executando agora as capturas ao vivo em GPU, benchmark de 120 frames e geração da prancha comparativa v1 vs v2 (task-124) em segundo plano.

Atenção de latência no WSL: o Modo B+ v2 (ch=48, 4 blocos, 195.760 parâmetros) atingiu p50 = 17.86 ms (async) / 19.03 ms (sync) no OpenGL do WSL Mesa D3D12 (398×224 → 1592×896) — \~1,2 ms acima dos 16.67 ms de 60 fps dentro do WSL (no PyTorch CUDA nativo roda a 2.22 ms FP16 / 3.38 ms FP32, e em OpenGL nativo no Windows terá folga ampla). Caso queira 60 fps travados também dentro do WSL, podemos treinar uma variante compacta v2\_fast (ch=40, nb=2 ou 3, \~6.8–10 ms no WSL) com o mesmo pipeline Flux HD + pré-dither.

🎯 Estado Atual

Tarefa Concluída: Etapa 2 — Retreinamento e exportação do Modo B+ v2 (ec\_bplus\_v2\_flux) sobre os 437 pares Flux HD de assets/hd/, com composição de cenas opacas (plane\_b + plane\_a + 0..4 sprites, alpha=1.0, idêntico ao frame de runtime) e espelho exato em PyTorch GPU do pré-dither both (CBOD + SGENPT-MIX VL). Modelo v1 (ec\_bplus\_espcn4x.\*) e pipeline/manifest.json preservados intactos.

Item Marcado: N/A — sem item correspondente em [task.md](http://task.md).

🛠️ Alterações Realizadas

neural/train\_[espcn.py](http://espcn.py): adicionados parâmetros --channels, --num-blocks, --dedither, --dedither-prob, --composite-scenes, --no-update-manifest, função pre\_dedither\_torch() (espelho exato de s\_dedither\_cs), compositor realista de cenas opacas (plane\_b + plane\_a + sprites com flip horizontal) e perda L1 + Sobel + SSIM; corrigido .resolve().relative\_to(ROOT) para caminhos relativos.

neural/export\_[onnx.py](http://onnx.py): adicionada flag --no-update-manifest e corrigido .resolve().relative\_to(ROOT) nos caminhos de saída.

Artefatos gerados (lado a lado com v1):

assets/models/ec\_bplus\_v2\_[flux.pt](http://flux.pt) (772 KB)

assets/models/ec\_bplus\_v2\_flux.onnx (770 KB)

assets/models/ec\_bplus\_v2\_flux\_weights.bin (765 KB, ECBP v1, 195.760 floats, CRC32=0x405d79ce, round-trip 0.0).

🧪 Validações Executadas

Lint/Typecheck: NÃO CONFIGURADO (Python/C sem linter configurado no repositório; scripts executaram sem erros de sintaxe/tipo; código C não foi alterado nesta etapa).

Treino \& Validação (RTX 5060 Ti): PASSOU — 120 épocas × 40 passos (batch=24, patch=64), PSNR = 22.35 dB / SSIM = 0.7825 em cenas opacas compostas inéditas (AlphaIoU = 1.0000).

Paridade PyTorch ↔ ONNX: PASSOU — python3 neural/export\_[onnx.py](http://onnx.py) ... (max\_abs\_err = 6.56e-7, PSNR = 146.25 dB, SSIM = 1.0, det\_max\_err = 0.0).

Paridade PyTorch ↔ GL Compute (com pré-dither both em s6\_vietnam, 1592×896): PASSOU — max\_abs\_rgb\_u8 = 1, mean\_abs\_rgb\_u8 = 0.0565, PSNR = 60.61 dB.

Benchmark GPU ao vivo (127 frames 398×224 → 1592×896, WSL Mesa D3D12, 0 fallbacks):

v1 (82k) + Pre-Dither Both: p50 = 6.84 ms (async)

v2 (196k Flux HD) + Pre-Dither Both: p50 = 17.86 ms (async) / 19.03 ms (sync)

Inspeção Visual: prancha comparativa lado a lado (v1 off | v1 + Pre-Dither | v2 Flux HD + Pre-Dither) gerada e verificada em compare\_bplus\_v2.png — salto nítido na suavidade de mármore/estátua do Sanctuary, madeira/túnica/fogo de Salem e grama/rosto/roupas de Vietnam sem dithering residual nem escadas duras.

Para testar ao vivo agora (WSL):

bash

GALLIUM\_DRIVER=d3d12 ./build/native/EternalChampionsRecomp roms/eternal\_champions\_us.gen --no-launcher --widescreen 16:9 --neural-bplus --bplus-dedither both --bplus-model assets/models/ec\_bplus\_v2\_flux\_weights.bin --unlock-boss

❓ Próximo Passo Proposto

Etapa 3 — Modo C (--hd-manifest): implementar o carregador e compositor real das texturas 4× aprovadas de pipeline/manifest.json sobre as camadas do VDP (com fallback automático para Pré-dither + B+ v2) — ou, se preferir primeiro uma variante v2\_fast (ch=40, nb=2, 6.8 ms = 60 fps travados no WSL) com o mesmo treino Flux HD, posso gerá-la antes.

Aguardando confirmação do usuário para prosseguir.

prossiga

Corrigido conflito de símbolo entre png\_write.h e <png.h> (libpng 1.6.43) em game/ec\_hd.c. Build nativo, validação harness/ec\_[harness.py](http://harness.py) --quick e capturas/benchmark do Modo C em GPU rodando em segundo plano (task-184). Aguardando conclusão.

Adicionado suporte ao ciclo de paleta ambiente em CRAM (fogueira de Salem / brilho do Sanctuary) em game/ec\_hd.c mantendo fallback em fades/flashes e paletas alternadas. Build concluído com zero warnings; executando ec\_[harness.py](http://harness.py) --quick e revalidação do Modo C (task-230) em segundo plano.

🎯 Estado Atual

Tarefa Concluída: Etapa 3 — Modo C (--hd-manifest), concluindo o pipeline gráfico completo (Pre-Dither → Mode B+ → Modo C 4× Flux HD).

Item Marcado: N/A — sem [task.md](http://task.md).

🛠️ Alterações Realizadas

ec\_hd.h: API pública do compositor HD Modo C (ec\_hd\_load\_manifest, ec\_hd\_is\_active, ec\_hd\_frame\_active, ec\_hd\_begin\_frame, ec\_hd\_record\_pixel, ec\_hd\_composite\_gpu, ec\_hd\_destroy\_gpu).

ec\_hd.c:

Parser streaming de pipeline/manifest.json (indexa os 6.135 registros / 437 aprovados: 410 sprites + 27 planos em <20 ms) + carga lazy via libpng com cache de texturas VRAM.

Matching determinístico em 9-bit Genesis RGB quantizado (qrgb9, imune à diferença de DAC BlastEm vs extrator):

Planos (A/B): hash FNV-1a por tile 8×8 + fallback de paleta shadow $FFFF9BB0 condicionado à luminância (80%–120%), mantendo compatibilidade com ciclos de paleta ambiente (fogueira de Salem, brilho de Sanctuary) sem falso-positivo em fades/flashes de tela.

Sprites: SHA-256 FIPS 180-4 idêntico ao extrator, com busca direta e hflip\_draw (espelhamento horizontal) + validação de CRAM (tolera até 3 índices com flicker ±1, rejeita fades/flashes e paletas alternadas de mirror match).

Compositor OpenGL 3.3+ em 2 passos sobre o framebuffer 4× (base\_tex vindo do B+ ou nativo 4×): Passo 1 desenha base + planos HD respeitando H-scroll, V-scroll e Shadow/Highlight do VDP; Passo 2 desenha sprites HD com verificação u\_map de dono do pixel para oclusão exata sprite×plano e sprite×sprite.

ec\_video.c: integração no rasterizador widescreen (ec\_fetch\_plane\_pixel marca colunas streamed para fallback automático ao B+, EcMarginSprites registra link, ec\_render\_ws\_scanline\_impl alimenta ec\_hd\_record\_pixel, ec\_video\_scanline\_lazy inicia o frame na linha 0 e ec\_video\_present\_gpu\* compõe o Modo C sobre o passe GPU).

CMakeLists.txt: adicionado find\_package(PNG REQUIRED), ec\_hd.c e link com PNG::PNG (libpng 1.6.43 + zlib 1.3).

🧪 Validações Executadas

Compilação: PASSOU (zero warnings novos em ec\_hd.c, ec\_video.c, ec\_spec.c e ec\_bplus\_gl.c) — cmake --build build/native -j$(nproc)

Lint/Typecheck: NÃO CONFIGURADO para C no projeto (validado com compilação limpa via GCC/CMake).

Testes de Regressão e Determinismo:

Harness rápido: PASSOU (01\_attract\_boot.ecm e 02\_title\_to\_fight.ecm: det=0/65536, png\_eq=True, misses=(0,0,0,0), interp\_diff=0/65536; WS 16:9 wram\_diff=0/65536) — python3 harness/ec\_[harness.py](http://harness.py) --quick

Paridade de WRAM com Modo C + B+ + Pre-Dither: PASSOU (diff\_bytes = 0 / 65536 vs nativo 16:9).

Cobertura HD real em jogo e inspeção visual (compare\_mode\_c.png):

Vietnam (s6\_vietnam): plane\_hd = 83.7%, sprite\_hd = 751/1189 (63.2%) (em 127 frames: 62.6%), 72.57% dos pixels aprimorados sobre B+ v2.

Salem (s7\_salem): plane\_hd = 66.2%, sprite\_hd = 410/1102 (37.2%) (tiles animados da fogueira fazem fallback suave ao B+ v2).

Sanctuary (s2\_sanctuary): plane\_hd = 55.8%, sprite\_hd = 461/953 (48.4%).

Trident (s5\_trident): plane\_hd = 80.7%, sprite\_hd = 288/1051 (27.4%). Nota visual: o PNG HD do céu de Trident (86cb2ec7b0cbbdd2\_4x.png) herdou um leve grão do dithering original do céu, enquanto o B+ v2 + Pre-Dither o alisava completamente.

Benchmark (127 frames, s6\_vietnam, 1592×896, WSL Mesa D3D12, 0 fallbacks):

Mode C solo: 2913.6 ms wall total (inclui inicialização + decodificação PNG lazy).

Mode C + B+ v1 + Pre-Dither Both: B+ p50 = 7.23 ms (p99 = 14.36 ms) — 60 fps estáveis no WSL.

Mode C + B+ v2 + Pre-Dither Both: B+ p50 = 18.46 ms (p99 = 22.49 ms) — \~1.8 ms acima de 16.67 ms no WSL Mesa D3D12 (rápido em GPU nativa Windows).

Comandos prontos para testar ao vivo no WSL

60 fps travados no WSL (Modo C + Pre-Dither + B+ v1):

bash

GALLIUM\_DRIVER=d3d12 ./build/native/EternalChampionsRecomp roms/eternal\_champions\_us.gen --no-launcher --widescreen 16:9 --hd-manifest pipeline/manifest.json --neural-bplus --bplus-dedither both --unlock-boss

Qualidade máxima nos frames sem match HD (Modo C + Pre-Dither + B+ v2 Flux HD):

bash

GALLIUM\_DRIVER=d3d12 ./build/native/EternalChampionsRecomp roms/eternal\_champions\_us.gen --no-launcher --widescreen 16:9 --hd-manifest pipeline/manifest.json --neural-bplus --bplus-dedither both --bplus-model assets/models/ec\_bplus\_v2\_flux\_weights.bin --unlock-boss

❓ Próximo Passo Proposto

Escolha o próximo passo desejado:

Opção A — B+ v2\_fast (ch=40, nb=2): treinar variante rápida do Flux HD + pré-dither mirando \~7 ms (60 fps travados também no WSL com qualidade Flux nos fallbacks).

Opção B — Commit das 3 etapas gráficas: revisar git diff e commitar Etapas 1, 2 e 3 (somente com sua autorização explícita).

Opção C — Próxima frente do projeto: ex. expandir catálogo de sprites/planos HD do Modo C, equalização/áudio remasterizado ou build Windows nativo.

Aguardando confirmação do usuário para prosseguir.

sendo bem sincero... o hd manifest nao ficou bom. sera que poderiamos testar outro assets de upscale de texturas? o problema foi que algumas ficaram boas outras nao. acho que faltou alguma suavidade e reduzir o tamanho dos pixels, teve algumas coisas que nem mudou. mas teve coisas que ficou incrivel por ex o ceu

Você acertou em cheio na leitura visual — existem 3 causas técnicas concretas para exatamente o que você notou:

Por que "teve coisas que nem mudaram" (efeito colcha de retalhos):

O manifest.json tem apenas 27 planos e 410 peças de sprite aprovadas (média de \~30 peças por arena). Em luta real, 40%–75% dos frames de animação dos personagens, HUD, barras e efeitos não têm PNG no manifest e caem no fallback.

Por que o céu/cenário ficou incrível em alguns trechos, mas os personagens/detalhes ficaram irregulares e ainda pixelados:

Os sprites em assets/hd/sprites/ foram gerados peça por peça em recortes soltos de 32×32 (uma perna, um braço) sem pré-dither — o Flux perdeu contexto e gerou granulação/inconsistência entre peças vizinhas (visível em piloto\_sprites\_4vias.png).

Além disso, em comfy\_[upscale.py](http://upscale.py), enforce\_silhouette() recorta o alfa com NEAREST, travando a borda externa de todos os sprites na escadinha quadrada 4×4 original.

E os planos de assets/hd/planes/ foram gerados antes do Pre-Dither existir — por isso o céu de Trident no Modo C voltou a ter grão, enquanto no Pre-Dither + B+ ficou liso.

Por que o fallback atual (B+ v1/v2) ainda parece pixelado:

Ele foi treinado sobre esses mesmos 437 recortes antigos (assets/hd) com L1 tradicional, que tende a manter a geometria em bloco em vez de suavizar curvas e reduzir visualmente o tamanho dos pixels.

O que já temos pronto na sua máquina para testar agora

Na sua F:\\ComfyUI\\ComfyUI\\models\\upscale\_models\\ (com RTX 5060 Ti 16 GB) você já tem modelos de upscale 4× muito melhores para suavidade e eliminação de pixels:

4x-AnimeSharp.pth — contornos muito lisos, elimina bem o serrilhado/pixelização.

4x\_IllustrationJaNai\_V1\_DAT2\_190k.pth — Transformer (DAT2) focado em arte/ilustração 2D (suave sem borrar detalhes).

4x\_foolhardy\_Remacri.pth — clássico de pacotes HD de jogos 2D/3D (texturas orgânicas e suaves).

4x\_NMKD-Siax\_200k.pth — muito consistente e suave em gradientes e sprites.

4x-UltraSharpV2.safetensors e 4xNomos8kSCHAT-L.pth — alta definição com reconstrução de bordas.

(Você também já tem assets/hd\_pixelart\_backup/ com os 437 arquivos gerados por 4x\_IllustrationJaNai\_V1\_DAT2, mas com o mesmo limite de 437 peças + borda NEAREST.)

Proposta (em 2 passos — ver primeiro, gerar depois)

Passo 1 — Teste visual comparativo rápido (sem alterar o jogo ainda):

Capturar 4 frames inteiros reais do jogo já com Pre-Dither aplicado (Vietnam, Salem, Sanctuary e Trident — pegando lutadores, céu, chão e HUD).

Rodar esses 4 frames lado a lado nos melhores upscalers já instalados em F:\\ComfyUI\\ComfyUI\\models\\upscale\_models\\ (AnimeSharp, IllustrationJaNai DAT2, Remacri, NMKD-Siax, UltraSharpV2, e opcionalmente Nomos8kSCHAT-L) direto na RTX 5060 Ti e montar uma folha comparativa com zoom 1:1 nos personagens e no céu.

Passo 2 — Aplicar o estilo que você aprovar no olho em 100% da tela:

Caminho principal (recomendado — zero coisa sem mudar): capturar centenas de frames inteiros reais de todas as arenas/lutadores (com Pre-Dither), passar pelo upscaler vencedor e treinar um Mode B+ v3 destilado em tela inteira (com perda perceptual + suavização de contornos). Assim 100% do jogo muda junto em tempo real (todos os golpes, personagens, HUD, menus, céu e cenários), sem recortes 32×32 e sem borda NEAREST.

Opcional no Modo C (--hd-manifest): regerar apenas os planos/cenários (céus e fundos) com Pre-Dither + upscaler vencedor (ou Flux) e desativar sprites soltos no Modo C, ou manter só o B+ v3 para uniformidade total.

Quer que eu já rode o Passo 1 e gere a folha comparativa com esses upscalers sobre os frames reais com Pre-Dither?



