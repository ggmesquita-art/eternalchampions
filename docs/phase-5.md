# Fase 5 — Pipeline de Assets HD (`M7`)

> Extraído de [DESIGN.md](../DESIGN.md). Ver também [ROADMAP.md](../ROADMAP.md).

## 10. Fase 5: Pipeline de assets HD

### 10.1 Extração

1. `extract/` roda o jogo pelo harness e registra todo quadro de sprite que aparece: metasprite montado, paleta e contexto (personagem e estado).
2. Deduplicação por hash. O catálogo fica em `assets/catalog/` (local, ignorado pelo git).
3. Cenários: tilemaps completos dos planos A e B por arena, com as camadas de parallax separadas.

### 10.2 Upscale

- **Problema central: consistência temporal.** Quadros de animação ampliados um a um geram detalhes divergentes, e a animação "ferve" na tela. Mitigações, em ordem de preferência:
  1. Personagem a personagem: uma folha de referência HD aprovada à mão (rosto, roupa, materiais), condicionando todo o resto via img2img/IP-Adapter/ControlNet com seed fixa.
  2. Keyframes curados à mão, com os intermediários gerados a partir deles.
  3. Modelos de vídeo, tratando cada animação como clipe.
- **Paletas**: fazer o upscale sobre uma imagem de índices (ou de luminância), não sobre as cores finais. Assim, as trocas de paleta (skins, flashes de dano, efeitos) continuam funcionando. Guardar mapas de região por índice.
- **Silhueta**: a máscara de alfa ampliada precisa respeitar a silhueta original escalada (tolerância configurável), para os hitboxes visuais não mentirem.
- **Ferramenta**: workflows de ComfyUI versionados em `pipeline/workflows/`. Cada asset gerado registra o workflow, o modelo, a seed e a versão no manifesto, para ser reproduzível.

### 10.3 Revisão

- Visualizador lado a lado (original × HD) por animação, tocando em loop.
- Status por asset: `generated` → `reviewed` → `approved`. Só `approved` entra no manifesto padrão.

---

## Execução e estado atual (`M7.1`, 2026-11-19)

- **Geração HD via ComfyUI (`0.45.4`, `RTX 5060 Ti` em `127.0.0.1:8188`, [tools/pipeline/comfy_upscale.py](../tools/pipeline/comfy_upscale.py)):**
  - **`437/437` assets HD 4× gerados** (`100%` dos planos e sprites catalogados em [pipeline/manifest.json](../pipeline/manifest.json): **`27/27` planos** + **`410/410` sprites**, workflow `upscale_4x_pixelart` / `4x_IllustrationJaNai_V1_DAT2_190k.pth`, seed `19931211`).
  - Tolerância de silhueta verificada em todos os `437` assets (`min_iou=0,972973`; planos `min_iou=0,9820`, lote inicial de `80` sprites `min_iou=0,9867`, lote restante de `330` sprites `min_iou=0,9730`).
- **Curadoria e Aprovação Manual Concluída (`M7.2`, 2026-11-19):**
  - Todos os **`437/437` assets (`27/27` planos + `410/410` sprites)** aprovados manualmente pelo usuário (`approved_count=437`, `generated_count=0`, `rejected_count=0` em [pipeline/manifest.json](../pipeline/manifest.json)).
  - Carga em runtime validada via `EC_HD_MANIFEST=pipeline/manifest.json` e `--hd-manifest pipeline/manifest.json` (`--benchmark 120`: `[EC_VIDEO] HD manifest 'pipeline/manifest.json': 6135 entradas (437 approved, fallback=Modo A/B)`, exit `0`).
  - Retreino completo do modelo B+ (`ECStudentESPCN4x`, `60` épocas × `48` steps) nos `437` pares aprovados, re-export `ONNX` (`sha256=61a6d163c868...`) / `ECBP` v1 (`crc32=0x6c2bd111`) e paridade OpenGL 4.6 validadas (ver [phase-4.md](phase-4.md)).
- **Nota arquitetural sobre tiles `8×8` isolados (`5.698` entradas):** os `5.698` tiles `8×8` do catálogo permanecem com status `extracted` intencionalmente; ampliá-los isoladamente criaria costuras de fronteira, enquanto os **`27/27` planos completos** já cobrem as camadas de arena sem costura.
- **Galeria interativa e player em loop ([tools/pipeline/review_viewer.py](../tools/pipeline/review_viewer.py)):**
  - Estático em [assets/review/index.html](../assets/review/index.html) (`437` pares).
  - Modo servidor local interativo: `.venv/bin/python tools/pipeline/review_viewer.py --serve --host 127.0.0.1 --port 8765` com player de loop lado a lado (`4/6/8/12` FPS), filtros por categoria/contexto/status/IoU máximo, botões `Reviewed`/`Approve`/`Reject`/`Reset`, lotes com confirmação explícita e persistência atômica em `pipeline/manifest.json` (com trava contra path traversal verificada em smoke test).

## Modos extras (`M7.3`, 2026-11-19)

- **Formato `.ecm` com pad 2:** cada linha aceita uma 2ª coluna hex opcional (`<p1> [<p2>]`); filmes antigos (só P1) continuam válidos e byte a byte iguais. Suporte em [tools/vendor/gpgx_runner/main.c](../tools/vendor/gpgx_runner/main.c) (porta 1 do libretro), [harness/ec_harness.py](../harness/ec_harness.py) (`PLAYER 1/2` no `--input-script`, telemetria `game_mode`/`two_player`/`tournament_type`, `expected_tel`) e [tools/harness/gen_roster_movies.py](../tools/harness/gen_roster_movies.py).
- **Novos movies** (Main Menu: One Player / Two Players / Tournament Mode / Information / Options):
  - `14_2p_vs_rax_vs_blade_s24.ecm` — Two Players → Fight → Options (Background = Trident, para não depender do RNG) → Fight; RAX (pad 1) × Blade (pad 2) no Estado `24`, os dois pads atacando. Nativo = interpretador `0/65536`, GPGX no mesmo estado/HP (diff residual `368` B).
  - `15_tournament_roundabout_jetta_vs_xavier_s27.ecm` — Tournament Mode → Round-A-Bout → Enter Tournament; Jetta × Xavier, `Tournament_Type` (`$FFFF9424`) `= 3`. A arena é sorteada (Round-A-Bout não tem Background) pelo RNG HV de `$CB82`: nativo Estado `27`, GPGX Estado `19` — o harness exige só estado de luta no GPGX (`gpgx_any_arena`).
  - `16_2p_vs_match_end_s24_to_s1.ecm` — 2P VS em que o P1 vence os 2 rounds contra P2 parado: KO no round final → `PLAYER 1 RAX WINS COMPETITION` → Estado `1` (HP `104/0`) nos três executores. É o filme de regressão do travamento de fim de partida corrigido nesta fase.
- **Overkill (mapeado e confirmado):** não é modo de menu (não há string na ROM; o "Sudden Death" é exclusivo da versão Sega CD). Regra no código: na resolução do round (`combat_core` `$5E4B6`), o perdedor passa por `jsr $269E4` → exige HP do vencedor `≠ 0`, força do golpe do perdedor `$128 ≥ $B`, round decisivo (`$1F15C`: vitórias+1 = rounds da Match Condition `$936D`, ou Estado `14`), arena na tabela `$26A36` (Estados `19..27`) e salta pela tabela `bra.w` `$26A40` para o handler da arena, que testa o X do perdedor no quadro do KO — Trident `$26D74`: `[$10A,$140]` se `$E6` bit 3 setado (perdedor virado para a esquerda), senão `[$309,$33F]`. Aceito → `Fight_OverkillActive` (`$FFFF9C7C`) `= 1`, objetos `$37`/`$75` (tentáculos puxam o perdedor para a água, `~f6385→f7500`) e volta ao Estado `1` em `~f7700`.
  - `17_2p_vs_overkill_trident_s24.ecm` — mesma partida do `16` até o P2 ficar com `6` HP na parede (confirm `+3485`); os dois pads andam para a esquerda `325` quadros (P2 em `X≈300`) e o P1 aplica `C` (`$128=$B`). Janela válida medida: `310..330` quadros (`300`/`340` falham). Termina em `f6900` durante a animação: Estado `24`, `$9C7C=1`, HP `104/0` no nativo, no interpretador (`0/65536` B) e no GPGX.

