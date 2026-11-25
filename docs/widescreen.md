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
