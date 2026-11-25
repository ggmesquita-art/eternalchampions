# Widescreen 16:9 / Adaptativo (`M4`–`M6`)

> Implementação em [game/ec_video.h](../game/ec_video.h), [game/ec_video.c](../game/ec_video.c) e [game/ec_spec.c](../game/ec_spec.c). Referência de arquitetura: [docs/phase-4.md](phase-4.md) §9.2.

## 1. Princípio: 320 px na lógica, margem estendida só no renderer

- A lógica do 68000, os limites de câmera (`Stage_ScrollX` `$FFFFCD1A`, `Stage_ScrollY` `$FFFFCD16`) e a geometria de colisão continuam operando exclusivamente sobre a janela nativa de `320x224` (`1:1` com o Mega Drive).
- Quando `--widescreen` é habilitado, o adaptador `g_ec_video` (`GameVideo`) solicita uma largura de saída `width` alinhada em múltiplo par de pixels (ex.: `398x224` em `--widescreen 16:9`) e centraliza a janela lógica de `320 px` (`left = (width - 320) / 2 = 39 px`).
- Verificação empírica em [harness/ec_harness.py](../harness/ec_harness.py): comparar a execução completa de 2.900 quadros em `--widescreen 16:9` (`398x224`) contra a execução padrão (`320x224`) resulta em **`0/65536` bytes divergentes na WRAM**.

## 2. Política por Estado (`$FFFFFD64`)

| Estados (`Main_GameState`, u16 BE em `$FFFFFD64`) | Telas cobertas | Comportamento do `ec_video_scanline_lazy` |
|---|---|---|
| `14`, `15`, `17..27` (`ec_is_arena_state`) | Todas as arenas de luta (`17` Battle Simulator, `18` Eternal Champion Sanctuary, `19..27` arenas dos 9 jogáveis) e modos de treino com cenário ativo (`14` Holo-Trainer, `15` Dexterity Spheres) | **Extensão viva de margens** a partir da VRAM do `GVDP` (Planos A e B), `Vdp_Reg11` per-line H-scroll (`$8B03`), `VSRAM` V-scroll (clampeado nas colunas de borda quando Reg 11 bit 2 ativo), bits de prioridade e `genesis_dac_cram_to_argb` (CRAM com suporte a shadow/highlight). |
| `0..13`, `16`, `28..39` | Logo SEGA, Title (`7`), Menu-init (`9`), Menus (`1`/`13`), Bios, Story, Versus (`16`), Victory/Continue/Credits | **Pillarbox preto puro** (`0xFF000000`) nas margens laterais (`[0..left)` e `[left+320..width)`), preservando a composição original sem revelar lixo fora de tela. |

## 3. Reposicionamento de HUD e Modo C

- **HUD nativo (Modo A/B widescreen):** permanece ancorado na área central de `320 px` (sem distorcer ou alterar a lógica de VRAM de barras de energia e relógio `$FFFF9CB4`). Em uma futura passagem semântica completa (Modo C), elementos de HUD poderão ser ancorados às extremidades do framebuffer ampliado exclusivamente em espaço de tela.
- **Modo C (`EC_HD_MANIFEST`):** `ec_video_load_hd_manifest()` lê [pipeline/manifest.json](../pipeline/manifest.json) em `ec_on_post_reset()` (via variável de ambiente `EC_HD_MANIFEST`), conta apenas entradas com `"status": "approved"` e mantém fallback automático para Modo A/B enquanto `approved == 0`.

## 4. Flags CLI e Resultados Medidos

- Padrão (`video_default = "off"` em [game/ec_spec.c](../game/ec_spec.c)): `320x224` nativo sem overhead de margem.
- Modos opt-in via `--widescreen <modo>`:
  - `off`: `320x224` nativo;
  - `fit` / `adaptive`: adapta à razão de aspecto da janela desenhável (`drawable_w / drawable_h`);
  - `stage` / `16:9`: fixa razão 16:9 (`398x224` em `height=224`);
  - `W:H`: razão arbitrária (ex.: `21:9`), limitada por `GVDP_MAX_WIDTH`.
- Resultados medidos na suíte completa (`02`..`13`, `--widescreen 16:9` -> `398x224`, margem de `39` px por lado = `8.736` px/margem):

| Captura | Cenário / Estado | Dimensão | Margem Esq. (não-preta / total) | Margem Dir. (não-preta / total) | WRAM Diff vs `320x224` |
|---|---|---|---:|---:|---:|
| [title_16_9.png](../assets/captures/title_16_9.png) | Pillarbox Title (Estado `7`) | `398x224` | `0/8736` | `0/8736` | `0/65536` |
| [fight_16_9.png](../assets/captures/fight_16_9.png) | Battle Simulator / Training Room (Estado `17`) | `398x224` | `8104/8736` | `8424/8736` | `0/65536` |
| [s14_holo_trainer_16_9.png](../assets/captures/s14_holo_trainer_16_9.png) | Holo-Trainer Dojo (Estado `14`) | `398x224` | `8458/8736` | `8424/8736` | `0/65536` |
| [s18_boss_sanctuary_16_9.png](../assets/captures/s18_boss_sanctuary_16_9.png) | Eternal Champion Sanctuary (Estado `18`) | `398x224` | `8138/8736` | `8277/8736` | `0/65536` |
| [s19_xavier_midknight_16_9.png](../assets/captures/s19_xavier_midknight_16_9.png) | Midknight Arena (Estado `19`) | `398x224` | `8165/8736` | `8083/8736` | `0/65536` |
| [s20_trident_xavier_16_9.png](../assets/captures/s20_trident_xavier_16_9.png) | Xavier Arena (Estado `20`) | `398x224` | `8191/8736` | `8611/8736` | `0/65536` |
| [s21_rax_blade_16_9.png](../assets/captures/s21_rax_blade_16_9.png) | Blade Arena (Estado `21`) | `398x224` | `7397/8736` | `7723/8736` | `0/65536` |
| [s22_blade_jetta_16_9.png](../assets/captures/s22_blade_jetta_16_9.png) | Jetta Arena (Estado `22`) | `398x224` | `8619/8736` | `8619/8736` | `0/65536` |
| [s23_larcen_rax_16_9.png](../assets/captures/s23_larcen_rax_16_9.png) | RAX Arena (Estado `23`) | `398x224` | `6421/8736` | `6074/8736` | `0/65536` |
| [s24_slash_trident_16_9.png](../assets/captures/s24_slash_trident_16_9.png) | Trident Arena (Estado `24`) | `398x224` | `8562/8736` | `8564/8736` | `0/65536` |
| [s25_shadow_larcen_16_9.png](../assets/captures/s25_shadow_larcen_16_9.png) | Shadow Arena (Estado `25`) | `398x224` | `6927/8736` | `6513/8736` | `0/65536` |
| [s26_jetta_slash_16_9.png](../assets/captures/s26_jetta_slash_16_9.png) | Slash Arena (Estado `26`) | `398x224` | `8140/8736` | `8163/8736` | `0/65536` |
| [s27_midknight_larcen_16_9.png](../assets/captures/s27_midknight_larcen_16_9.png) | Larcen Arena (Estado `27`) | `398x224` | `8619/8736` | `8619/8736` | `0/65536` |

## 5. GW.3 — Flicker lateral e sprites descartados nas margens (causa-raiz e correção)

**Bug A (flicker lateral com câmera em movimento).** O renderer widescreen roda por scanline intercalado com o 68K (`machine_run_frame_ex`): o PNG do quadro f usa VRAM/SAT do estado f-1, a WRAM na linha 0 ainda é f-1, e por volta da linha ~35 o 68K a atualiza para f. `ec_compute_viewport_x0` ($CD14/$CCF0/$CCEC), `ec_lookup_layer_tile`, `ec_plane_edge_comp` (lista de camadas $D84E, tabela $CD1E) liam a WRAM ao vivo a cada linha, causando (1) rasgo de x0 no meio do quadro (ex.: f3381 linhas 0–35 x0=39, 36+ x0=35) e (2) corrida do `c0`: a coluna nova de metatiles entrava na janela antes do vblank gravar a nametable, e a entrada crua desatualizada da VRAM era usada em vez do fallback de ROM (movie 04 f2230–3399: 299 de 406 shifts com coluna desatualizada, 57 quadros com lixo visível).
Correção (`game/ec_video.c`): na linha 0 `ec_video_scanline_lazy` tira um snapshot por quadro (`s_snap_*`: estado de arena, x0 — preservando `s_trex_pan` —, bytes $00..$39 de cada camada, $CD1E[slot], `($2,a0)>>5`, `$9B32`) e todas as linhas do quadro leem apenas dele. Nas camadas hstream (`(fl & 0x22) == 0x02`), pixels de margem (sx<0 ou sx>=320) usam sempre o fallback de metatiles da ROM, ignorando a janela c0; o centro de 320 px continua idêntico ao 4:3.

**Bug 1 (pilar some na arena Jetta/S22).** Os pilares são sprites (objetos id40=$009F, `fl20 & $4000`, 3 sub-sprites). `Sub_E684` ($E772–$E7B0) descarta da SAT sub-sprites com x de tela fora de [-32,320] (e y fora de [-32,224]); o renderer só lia a SAT, então nada aparecia nas margens.
Correção: hooks passivos (sempre retornam 0, só leem `g_cpu`/`g_ram`) em `$00E684` (zera a lista pendente) e `$00E7B0` (registra {x,y,size,attr,ordem d4} dos sub-sprites descartados por X, com Y válido, objetos `fl20 & $4000`, nunca tipo 6/HUD) — `ec_video_on_sprite_hook`, registrado em `game/game.toml` e `ec_spec.c`. Na linha 0 do quadro seguinte (quando a SAT correspondente passa a ser exibida) a lista pendente vira a ativa; `ec_eval_full_sprites_line` intercala os extras na posição `ord` da SAT (preservando prioridade) e os desenha somente nas margens (`ox < x0 || ox >= x0+320`), com `link=0xFF` (sem pareamento HD) e sem acionar o mascaramento `any_nonzero_x`. Nenhuma escrita no estado do jogo (`wram_diff` = 0/65536).

## 6. Parallax nas bordas: decorações presas a camadas, tremor e objetos cortados pelo jogo (2026-11-25)

Relatos do usuário: meio do tronco da árvore do Trident desalinhando (e, do lado direito, só a faixa de nuvens translúcidas aparecendo onde o tronco deveria estar), cabeças dos leões do RAX sumindo, poste do Larcen se movendo, fundo do Holo-Trainer e bandeiras do Santuário "tremendo" ao andar nas laterais. Todos acontecem só no 16:9, quando `x0` está ancorado na borda (`dx = x0 - left != 0`).

**Decorações presas a camadas (tronco do Trident, poste do Larcen).** Não são tiles de plano: são objetos com `($18,a0) = 8`. Em `Sub_E684` ($E69E..$E6CA) o jogo soma a eles o delta `($16)` da camada `($4E,a0)` (um nó da lista $D84E), então andam na velocidade de parallax da camada. Os planos já recebiam a compensação de borda, mas os sprites eram desenhados em `x0 + sx` sem ela e descolavam do cenário (ex.: tronco do Trident `id $22` preso à camada L2, fac -2560, deslocava até 25 px). Correção: hook passivo em `$00E6CE` (`loc_E6CE`: a0 = objeto, d4 = link da entrada → índice da SAT `d4 - 1`) marca as `($E,a0)` entradas de cada objeto com a camada (ou 0) e zera a seguinte; na linha 0 do quadro seguinte vira `s_sat_layer_act` e `s_sat_xadj[i]` recebe a compensação da camada; `ec_eval_full_sprites_line` e o casamento HD (`ec_hd_begin_frame(..., sat_xadj)`) usam `x0 - s_sat_xadj[link]`. Extras ($E7B0) guardam a camada também.

**Tremor de ±1 px.** A compensação era `round(-fac*dx/4096)`, enquanto o jogo usa `H-scroll = -(cam + eE)`, com `eE` 16.16 em `($0E,a4)` (`floor` de `cam*fac/4096` no modo $0C). Os dois arredondamentos divergiam em quadros alternados. Agora `ec_layer_edge_comp` calcula `floor(eE - fac*dx/4096)` (o deslocamento da camada na câmera virtual `cam - dx`, constante na zona de borda) menos o deslocamento **realmente** gravado na linha (`-hs - cam`, com `cam = -($1A,a4)`). Medir o valor real era necessário no Santuário: lá a camada das bandeiras (L1 do plano B, fac 1024) é gravada com o `eE` do quadro anterior quantizado em 2 px (`-hs - cam` = 0, 2, 4…). Com `dx = 0` nada muda (centro idêntico ao 4:3); ao entrar na zona há um ajuste único de até 1–2 px. Para sprites presos a camada (sem H-scroll de linha) vale a fórmula `floor`.

**Objetos cortados pelo próprio jogo (cabeça dos leões do RAX).** `loc_DF18` (util_sched_obj.asm) liga o bit 7 de `($21,a0)` (objeto oculto) quando o x de tela sai de `[-64, 384)`. No 16:9 parte do objeto ainda cai na margem: o corpo do leão é cenário e continua visível, a cabeça (`id $0F`, 4 sub-sprites) some. Correção: no mesmo hook `$E6CE`, `ec_collect_xculled_object` registra como extras (desenhados só nas margens) os sub-sprites de objetos com `fl20 & $8080 == $0080` cujo único motivo de corte é o X (Y dentro da faixa, mesmos filtros de quadro alternado de `Sub_E684`), recalculando a posição como em $E722..$E76A.

Validação: Trident (tronco alinhado e parado de câmera 0 a 40), Larcen (poste inteiro), RAX (cabeça visível no quadro em que o jogo a cortava), Holo-Trainer e Santuário (linhas de fundo com deslocamento 0 entre quadros de mesma paridade na zona de borda). Harness: `det=0/65536`, misses 0, `wram_diff=0/65536`.

**Névoa na frente do tronco do Trident (mudança deliberada em relação ao original).** O meio do tronco é um sprite de prioridade alta (objeto `$22`, `attr $847F`) e, no jogo original, fica na frente da névoa, que está pintada em pontilhado no próprio plano B (índices 20/21/22/23/25 da linha 1; o céu usa 17..19) — comparação pixel a pixel com o 4:3 em `assets/video_compare/widescreen/trident_tronco_nevoa_16x9_vs_4x3.png`. A pedido do usuário, no renderer widescreen o plano B vence esse sprite onde tem um ponto de névoa, então a névoa passa translúcida na frente do tronco. O hook `$E6CE` guarda também o id do objeto de cada entrada da SAT (`s_sat_objid_act`) para isso. Só apresentação: o 4:3 nativo segue idêntico ao original.

**Fora do widescreen — Overkill do vitral do Santuário (`game/ec_boss.c`).** O vencedor encostado na parede à esquerda do vitral sumia, ficando só os pés (também no 4:3). Causa: na fase 3 o overkill chama `Sub_0266AE` (x 0, y $50, 8×13 células), que **inverte** (`eori #$8000`) a prioridade dessas células do plano A para a vítima passar por trás da parede, e nunca desfazia; o sprite do vencedor (prioridade baixa) ficava atrás. Correção: `sanc_toggle_wall_priority()` desfaz a inversão assim que a vítima é ocultada (fim da fase 3) ou se o overkill for interrompido ainda no Santuário. Saves gravados com a parede já invertida (ex.: depois do overkill) continuam com o defeito: é preciso refazer o overkill.
