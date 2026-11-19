# Fase 4 — Renderer e Upscale Neural (`M4`–`M6`, `N1`–`N3`)

> Extraído de [DESIGN.md](../DESIGN.md). Ver também [ROADMAP.md](../ROADMAP.md).

## 9. Fase 4: Renderer

Três modos de VDP, selecionáveis em runtime, que convivem:

| Modo | O que faz | Uso |
|---|---|---|
| **A: emulado** | VDP fiel, pixel a pixel, em 320×224 | Referência, depuração, fallback |
| **B: estrutural** | A cada quadro, lê o estado do VDP (planos A/B, window, tabela de sprites, paletas, scroll) e monta uma cena para a GPU, tile a tile, em qualquer escala | Escala limpa, shaders, filtros |
| **B+: neural** | Igual ao B, mas cada camada passa por uma rede de super-resolução destilada antes da composição (§11) | Cobre o que ainda não tem asset HD |
| **C: semântico** | Usa o mapa de RAM para saber *qual personagem, qual quadro de animação e qual arena* e desenha o asset HD correspondente no lugar dos tiles | Remasterização |

### 9.1 Modo C: resolução de assets

- Cada quadro de sprite original é identificado pelo **hash do quadro reconstruído** (metasprite inteiro, em pixels indexados) mais o id da paleta.
- O manifesto (`pipeline/manifest.json`) mapeia `hash → { arquivo HD, escala, âncora (x,y), paletas compatíveis }`.
- Posição, espelhamento e prioridade vêm do estado original. O asset HD só substitui a "pele".
- Hash sem entrada → desenha o quadro pelo modo B+ (ou pelo modo B, se o B+ estiver desligado).

### 9.2 Widescreen

- A lógica continua vendo 320 px de largura. O renderer desenha uma margem lateral extra.
- Cenários: as bordas precisam de conteúdo além do que o tilemap original carrega. As opções, por arena: (a) estender o tilemap a partir dos dados de nível, se existirem; (b) arte HD nova pintada para 16:9; (c) pilares ou molduras. A decisão é por arena e fica registrada em `docs/widescreen.md`.
- Limites da arena e câmera: **não mudam**. Widescreen é só área visível adicional, sem gameplay em área nova.
- O HUD é reposicionado no espaço de tela pelo renderer, sem tocar na lógica.

### 9.3 Interpolação de quadros (opcional, fase tardia)

Renderização acima de 60 Hz, interpolando posições entre passos de simulação. Fica desligada por padrão: em jogo de luta, a fidelidade de 60 Hz é preferível.

---

## 11. Upscale neural em tempo real (modo B+)

### 11.1 Objetivo

Cobrir em tempo real tudo o que não tem asset HD aprovado (cenários, efeitos, quadros não catalogados) num estilo coerente com o remaster, em vez de mostrar o pixel cru do modo B. No longo prazo, o B+ é o que permite jogar o remaster inteiro antes de a curadoria terminar.

Redes genéricas (Anime4K, CuNNy, FSRCNNX) foram treinadas com anime ou fotos e deformam pixel art. A premissa aqui é outra: **uma rede pequena treinada só com este jogo**.

### 11.2 Destilação professor → aluno

- **Professor**: o pipeline offline da §10 (assets `approved`). No começo, enquanto ainda há poucos assets curados (bootstrap), o professor é a saída de um modelo de super-resolução pesado rodando offline (modelos de pixel art do OpenModelDB, Real-ESRGAN etc.) sobre as camadas extraídas, revisada por amostragem.
- **Aluno**: uma CNN pequena que processa na resolução nativa e só expande no fim (sub-pixel convolution / pixel shuffle), com fator 4× (320×224 → 1280×896). Arquitetura base no estilo ESPCN/FSRCNN. O tamanho final é definido pelo orçamento da §11.4.
- **Pares de treino**: `(camada original RGBA, alvo HD RGBA)`, separados por tipo de camada (plano, sprite, efeito).
- **Ciclo**: cada asset que passa a `approved` entra no dataset. O retreino é periódico, e a versão do modelo fica registrada em `pipeline/manifest.json`. Quanto mais curadoria, mais o aluno se aproxima do estilo final.
- **Perda**: L1 mais perda perceptual, mais uma penalidade de alfa/silhueta (o contorno ampliado precisa respeitar o original escalado, como na §10.2).

### 11.3 Inferência por camada

É a vantagem estrutural sobre upscalers de janela, como o Magpie. Eles veem o quadro já composto e erram nas bordas sprite/fundo. Aqui, cada camada é tratada isoladamente.

Por quadro:

1. O VDP HAL entrega as camadas separadas: plano B, plano A, sprites (com as duas prioridades separadas) e window. Todas em RGBA, com alfa = cor de índice 0.
2. Os sprites com asset HD (modo C) saem da camada de sprites e são desenhados direto.
3. A rede é aplicada às camadas restantes.
4. A composição por prioridade acontece na GPU, já em HD. O modo shadow/highlight do VDP, se for usado, entra depois da composição, como operação de cor.

**Cache.** O cenário quase não muda, e por isso o custo real fica muito abaixo do pior caso:

- Os planos são inferidos sobre o bitmap do tilemap (região visível mais uma margem) e ficam em cache enquanto VRAM, tilemap e paletas daquela região não mudarem. O scroll, inclusive por linha, é aplicado depois, no espaço HD (deslocamento × 4).
- Os sprites ficam em cache pelo hash do metasprite mais a paleta, igual ao modo C.
- Uma troca de paleta (ciclos de cor em fogo ou água, por exemplo) invalida só as regiões afetadas.

### 11.4 Orçamento de desempenho

Estimativa para uma camada de 320×224 (~72 mil pixels), rede processando na resolução nativa:

| Rede | GFLOP/quadro por camada | Pior caso (5 camadas sujas) a 60 fps |
|---|---|---|
| ~100 mil parâmetros | ~14 | ~4,3 TFLOPS |
| ~1 milhão de parâmetros | ~140 | ~43 TFLOPS (exige tensor cores e FP16, ou cache agressivo) |

- Meta: **≤ 2 ms por quadro** de inferência no pior caso, na GPU mínima suportada (a definir).
- No caso típico (cenário em cache, 2 lutadores e efeitos), a inferência cai para uma fração disso.
- Medição: o harness roda as movies da suíte com o B+ ligado e reporta p50/p99 de tempo por quadro.

### 11.5 Runtime de inferência

- **Treino**: PyTorch. **Exportação**: ONNX.
- **Backend principal**: ONNX Runtime, com o execution provider de cada plataforma (TensorRT/CUDA, DirectML, CoreML).
- **Backend alternativo, sem dependências**: compilar os pesos para compute shaders em FP16, como fazem o CuNNy e o Anime4K. Ele serve também para o protótipo P1.
- **Interop**: as texturas do renderer vão direto para a inferência, sem cópia nem readback para a CPU.
- **Determinismo**: a mesma entrada gera sempre a mesma saída (sem amostragem). Os testes de regressão visual usam tolerância de PSNR/SSIM contra a saída de referência do modelo versionado.
- O B+ nunca afeta a simulação: se a inferência estourar o orçamento, o quadro sai no modo B.

### 11.6 Caminho de implementação (independente da recompilação)

O B+ não precisa esperar o runtime. Ele avança em paralelo, em três etapas, e o módulo `neural/infer` é uma biblioteca isolada, com interface estável, usada pelas etapas P2 e P3:

| Etapa | Base | O que valida | Limite |
|---|---|---|---|
| **P1: Shader** | Shader slang multi-pass no RetroArch, com os pesos embutidos | Arquitetura e tamanho da rede, qualidade visual, custo | Só vê o quadro composto; número de passes limitado |
| **P2: Emulador com camadas** | Fork de um emulador open source com o VDP modificado para exportar as camadas, e frontend próprio | Inferência por camada, cache, orçamento; **extração do dataset da §10.1 antes de a recompilação existir** | Não é o produto final |
| **P3: Runtime** | Renderer do runtime (§9) | Integração final do modo B+ | Depende de M2 e M4 |

Candidatos para o P2, analisados no código-fonte em 2026-11:

| Emulador | Onde as camadas se separam | Precisão | Embutir num frontend C++ |
|---|---|---|---|
| **clownmdemu** (C89) | Planos desenhados em buffers de linha e combinados por tabela de blit. O callback por linha já entrega os índices de paleta. Widescreen já implementado no core | Por linha. Exceções do 68k incompletas (segundo o próprio TODO) | Muito fácil: o core foi feito para ser biblioteca |
| **ares** (C++) | `DAC::pixel()`: os pixels de A, B e sprite são calculados separados, com índice e prioridade, antes da composição | Ciclo a ciclo | Médio: depende do `nall` |
| **jgenesis** (Rust) | `merge_layers()` com buffers `plane_a_pixels`/`plane_b_pixels` mais o pixel de sprite. Renderer já em GPU (wgpu) | Alta | Médio: fronteira Rust/C++ |
| **Genesis Plus GX** (C) | Planos em `linebuf`, mas os sprites são compostos durante o desenho, em muitas variantes otimizadas | Por linha, compatibilidade altíssima | Médio. Já é a referência do harness |
| **BlastEm** (C) | `tmp_buf_a/b` mais `layer_debug_buf`, num `vdp.c` monolítico | Ciclo a ciclo | Difícil. É a melhor ferramenta de debug para a Fase 1 |

Recomendação: **clownmdemu para o P2**, desde que passe no teste empírico com a ROM (boot, luta completa e troca de arena, comparados com o Genesis Plus GX). Se não passar, o fallback é o **ares**. A escolha do VDP do runtime (modo A) é uma decisão separada, tomada no M2, só por precisão.

O P2 cumpre um papel duplo: é o protótipo do B+ e é a ferramenta de extração de camadas e quadros para o pipeline de assets. Por isso vale começar o projeto por ele, em paralelo com a Fase 1.

---

## Implementação e verificação (`N1`–`N3` / `P3`, 2026-11-19)

Pipeline neural B+ completo implementado e integrado ao runtime nativo sem dependências externas em C/OpenGL (`§11.5`):

1. **Treino (`N1` — [neural/train_espcn.py](../neural/train_espcn.py)):** `ECStudentESPCN4x` (`40` canais, `2` blocos residuais, escala `4×`, `82.344` parâmetros) destilado em PyTorch CUDA sobre os `107` pares ComfyUI (`PSNR=23,273 dB`, `SSIM=0,8869`, `AlphaIoU=0,9996`, checkpoint `assets/models/ec_bplus_espcn4x.pt`).
2. **Export ONNX + Pack Binário (`N2` — [neural/export_onnx.py](../neural/export_onnx.py)):**
   - `assets/models/ec_bplus_espcn4x.onnx` (`opset 17`, `332.523` B, `18` nós, arquivo único sem `.onnx.data`, eixos espaciais dinâmicos) + pack `ECBP` v1 `assets/models/ec_bplus_espcn4x_weights.bin` (`329.408` B, `crc32=0x4728339b`, round-trip `max_diff=0.0`).
   - Paridade PyTorch FP32 ↔ ONNX Runtime verificada com `det_max_err=0.0` em `320×224` (`PSNR=144,493 dB`), widescreen `398×224 → 1592×896` (`PSNR=144,484 dB`) e [fight_320.png](../assets/captures/fight_320.png) → `fight_bplus_onnx_1280.png` (`PSNR=149,397 dB`, `SSIM=0,9999997`).
   - `onnxruntime-gpu 1.32.0` do `.venv` exige CUDA `13.*` (`libcublasLt.so.13`/`libcudart.so.13`, incompatível com o host CUDA `12.6`/`.venv` CUDA `12.8`, além da ausência de headers `onnxruntime_c_api.h`); por isso o runtime C utiliza Compute Shaders OpenGL 4.3+ (`§11.5`).
3. **Runtime C `P3` (`N3` — [neural/infer/ec_bplus_gl.h](../neural/infer/ec_bplus_gl.h), [neural/infer/ec_bplus_gl.c](../neural/infer/ec_bplus_gl.c), [game/ec_video.c](../game/ec_video.c), [game/ec_spec.c](../game/ec_spec.c)):**
   - **Flags opt-in:** `--neural-bplus`, `--bplus-model <bin>`, `--bplus-budget-ms <ms>` e variáveis `EC_NEURAL_BPLUS=1`, `EC_BPLUS_MODEL`, `EC_BPLUS_BUDGET_MS`, `EC_BPLUS_SYNC_TIMING=1`, `EC_BPLUS_TIMINGS`, `EC_BPLUS_CAPTURE` / `EC_BPLUS_CAPTURE_IN` / `EC_BPLUS_CAPTURE_AT` (desligado por padrão; `GALLIUM_DRIVER=d3d12` ativa a GPU RTX no WSL2).
   - **Paridade GPU OpenGL 4.6 vs ONNX Runtime (`NVIDIA GeForce RTX 5060 Ti` via `GALLIUM_DRIVER=d3d12`):**
     - `320×224 → 1280×896` (Title, quadro `600`): `max_abs=1/255` (`0,0039`), **`PSNR=78,603 dB`**, **`SSIM=0,999998`**.
     - Widescreen `16:9` `398×224 → 1592×896` (Arena Estado `17`, quadro `2400`): `max_abs=1/255` (`0,0039`), **`PSNR=62,973 dB`**, **`SSIM=0,999878`**.
   - **Neutralidade na simulação:** `0/65536` bytes divergentes na WRAM entre B+ ligado e desligado.
   - **Latência medida na RTX 5060 Ti:**
     - WSL2 Mesa D3D12 OpenGL 4.6 (`GALLIUM_DRIVER=d3d12`): async `GL_TIME_ELAPSED` `320×224` `p50=5,3423 ms / p99=6,7295 ms` (`200` amostras, dentro do quadro de `16,67 ms` a 60 fps); `sync=1` `320×224` `p50=6,7007 ms / p99=7,9590 ms` (`600` amostras); widescreen `398×224` `sync=1` `p50=7,8803 ms / p99=11,7582 ms` (`2500` amostras).
     - Referência PyTorch CUDA FP16 nativa na mesma GPU: `p50=1,1924 ms / p99=1,7751 ms` (meta `≤ 2,0 ms` de `§11.4`). Conforme [ROADMAP.md §11.5](../ROADMAP.md#L249), o benchmark estrito OpenGL `≤ 2,0 ms` deve ser revalidado em build Windows nativo sem a camada de tradução Mesa D3D12 do WSL2.
   - **Fallbacks validados (`exit 0`):** `--benchmark 120` headless (`software`), guarda contra OpenGL em software (`llvmpipe`, sem `GALLIUM_DRIVER=d3d12`) e guarda de orçamento (`--bplus-budget-ms 0.001`: `frames=10`, `fallback=15`).
4. **Retreino nos `437` assets aprovados (`M7.2`, 2026-11-19):**
   - **Dataset completo (`371` treino / `66` validação = `80` crops, `60` épocas × `48` steps):** **`PSNR=22,45 dB`**, **`SSIM=0,8805`**, **`AlphaIoU=0,9971`**, latência PyTorch CUDA `fp16 320×224` **`p50=1,1871 ms / p99=1,4109 ms / mean=1,1965 ms`** (`fp32 p50=2,097 ms / p99=2,55 ms`).
   - **Re-export (`N2`):** `ec_bplus_espcn4x.onnx` (`332.523` B, `opset 17`, `sha256=61a6d163c868...`) + `ec_bplus_espcn4x_weights.bin` (`329.408` B, `crc32=0x6c2bd111`, round-trip `max_diff=0.0`); paridade ONNX `det_max_err=0.0` em `320×224` (`PSNR=144,266 dB`), widescreen `398×224` (`PSNR=144,247 dB`) e `fight_320.png` (`PSNR=149,215 dB`, `SSIM=1.0`).
   - **Paridade GPU OpenGL 4.6 (`RTX 5060 Ti`, `GALLIUM_DRIVER=d3d12`) com pesos retreinados:** `320×224 → 1280×896` **`PSNR=78,522 dB / SSIM=0,999999`** (async `p50=5,6767 ms / p99=9,7828 ms`; `sync=1` `p50=7,0952 ms`), widescreen `398×224 → 1592×896` **`PSNR=62,523 dB / SSIM=0,999881`** (`sync=1` `p50=8,1396 ms`), WRAM B+ ON vs OFF **`0/65536`**, fallbacks exit `0`.
