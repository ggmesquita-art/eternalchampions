# DESIGN.md — Eternal Champions: Recompilação Nativa e Remasterização

> Nome provisório do projeto: **EternalRecomp**
> Status: rascunho de design, v0.3 (2026-11-07). Uso privado (§3); upscale neural em tempo real (§11); base do P2 (§11.6)
> Autor: Guilherme Mesquita

---

## 1. Visão geral

Port nativo para PC de *Eternal Champions* (Sega Genesis/Mega Drive, 1993). O caminho é este: disassembly reconstruível, depois recompilação estática do código 68000 para C++, depois um runtime que substitui o hardware do console. Sobre essa base entram melhorias que a emulação não permite: renderização em alta resolução com assets ampliados por IA, widescreen, latência mínima e, numa fase futura, a versão Sega CD (*Challenge from the Dark Side*).

O jogo original continua sendo a fonte de verdade da lógica: física, frame data, hitboxes e IA. Só a apresentação é reescrita.

### 1.1 Objetivos

- **G1**: executável nativo (Windows/Linux/macOS) com comportamento idêntico ao original, quadro a quadro.
- **G2**: renderer moderno com troca de sprites e cenários por versões HD, com fallback para o asset original.
- **G3**: widescreen 16:9 sem quebrar a lógica das arenas.
- **G4**: input nativo, com suporte a 6 botões e sem a latência da emulação.
- **G5**: todo o pipeline reproduzível a partir da ROM local. A ROM e os dados extraídos ficam fora do controle de versão.

### 1.2 Não objetivos

- Não é um emulador genérico de Mega Drive.
- Não altera o balanceamento nem as mecânicas do jogo, salvo modos opcionais separados e claramente marcados.
- Não será publicado nem distribuído. É uso privado (§3).

---

## 2. Princípios de engenharia

1. **O hash é a lei.** Na fase de disassembly, toda mudança precisa remontar um binário idêntico à ROM de referência.
2. **Determinismo é a lei.** Na fase de recompilação, toda mudança precisa passar no harness de replay (RAM idêntica, quadro a quadro, contra o emulador de referência).
3. **Substituição incremental.** Cada camada de hardware emulado só é trocada por uma implementação nativa quando a anterior estiver validada.
4. **Lógica em 320×224, apresentação em qualquer resolução.** A simulação nunca sabe que está em HD.
5. **Fallback sempre.** Um asset HD ausente ou inválido cai no original sem quebrar nada.
6. **LLMs propõem, o verificador decide.** Nenhuma anotação ou transformação gerada por IA entra sem passar pelo hash ou pelo harness.

---

## 3. Escopo de uso

- **Uso privado.** O projeto não será publicado nem distribuído. Por isso, licenças de terceiros não restringem as escolhas técnicas: qualquer emulador, core ou biblioteca pode ser usado, misturado ou modificado livremente.
- Por higiene, a ROM, os dados extraídos e os assets gerados continuam fora do controle de versão. O repositório fica leve, e o pipeline continua reproduzível a partir da ROM.
- A ROM fica em `roms/`, e os dados extraídos em `assets/`, os dois no `.gitignore`. A pasta inteira é ignorada, em vez de extensões: `*.md` é extensão de ROM de Mega Drive, mas também de Markdown.

---

## 4. Escopo de versões

| Versão | Plataforma | Fase | Observação |
|---|---|---|---|
| Cartucho (EUA/JUE, revisão a definir) | Genesis | **Alvo inicial** | Cerca de 24 Mbit (confirmar com o dump). Um único 68000 e um Z80 de som |
| *Challenge from the Dark Side* | Sega CD | Futura | Segundo 68000, streaming de CD, FMV e áudio CD. Reaproveita o runtime |

A revisão exata da ROM de referência (região e revisão) é fixada por hash SHA-1/CRC32 em `reference/roms.toml`.

---

## 5. Arquitetura

```
                ┌──────────────────────────────────────────┐
  ROM do  ───►  │  tools/  (Python)                         │
  usuário       │   disasm  → labels → recomp → extract     │
                └──────┬───────────────┬───────────────┬────┘
                       │               │               │
                       ▼               ▼               ▼
              disasm/ (asm 68k)  gen/ (C++ gerado)  assets/ (local, ignorado)
              hash = ROM            │                  │  tiles, paletas,
                                    │                  │  quadros de sprite
                                    ▼                  ▼
               ┌─────────────────────────────┐   ┌──────────────────────┐
               │ game/ (C++ gerado + manual) │   │ pipeline HD (ComfyUI) │
               │  rotinas recompiladas       │   │  upscale → manifesto  │
               └──────────────┬──────────────┘   └──────────┬───────────┘
                              │ chamadas de I/O             │
                              ▼                             ▼
               ┌──────────────────────────────────────────────────────┐
               │ runtime/ (C++20 + SDL3)                               │
               │  ┌─────────┐ ┌──────────┐ ┌───────┐ ┌──────────────┐ │
               │  │ CPU ctx │ │ VDP HAL  │ │ Áudio │ │ Input (6 bt) │ │
               │  │ + fallb.│ │ A│B│C    │ │Z80+FM │ │              │ │
               │  └─────────┘ └────┬─────┘ └───────┘ └──────────────┘ │
               │                   ▼                                   │
               │  Renderer (GPU) ← manifesto HD + upscale neural (B+)  │
               └──────────────────────────────────────────────────────┘
                              ▲
               harness/ ──────┘  replay de input + diff de RAM vs. referência
```

### 5.1 Estrutura do repositório

```
/
├── DESIGN.md
├── reference/        # hashes das ROMs, notas de hardware, mapa de RAM
├── disasm/           # disassembly 68k (sintaxe vasm/mot), dividido por módulo
│   ├── main.asm
│   ├── code/         # rotinas
│   ├── data/         # tabelas (incbin para dados extraídos da ROM do usuário)
│   └── symbols/      # labels.toml, ram_map.toml (só metadados)
├── tools/
│   ├── split.py      # extrai binários da ROM do usuário para incbin
│   ├── verify.py     # monta e compara hash
│   ├── recomp/       # transpilador 68k → C++
│   ├── extract/      # tiles, paletas, metasprites, cenários
│   └── llm/          # prompts e scripts de anotação assistida
├── runtime/          # biblioteca: CPU ctx, VDP, áudio, input, renderer
├── game/             # C++ gerado (gen/) + overrides manuais (hand/)
├── harness/          # gravação/replay de input, diff de RAM, CI
├── pipeline/         # workflows de upscale, manifesto de assets
├── neural/           # upscale em tempo real: train/, export/, infer/ (biblioteca isolada)
└── docs/             # formatos de dados descobertos, frame data, IA
```

---

## 6. Fase 1: Disassembly reconstruível

### 6.1 Método

1. `split.py` lê a ROM do usuário e gera `disasm/` com tudo em `dc.w` / `incbin`. O primeiro build já precisa bater o hash.
2. A partir dos pontos de entrada conhecidos (vetor de reset em `0x000004`, autovetores de interrupção, VBlank e HBlank), as regiões viram código de verdade, uma por vez.
3. Cada região convertida passa pelo `verify.py`. Se o hash quebrar, o commit é rejeitado.

### 6.2 Ferramentas

| Uso | Ferramenta |
|---|---|
| Análise estática | Ghidra (68000 nativo) ou IDA |
| Análise dinâmica | Exodus (*active disassembly*: marca código e dados pela execução real), debugger do MAME |
| Montador | `vasmm68k_mot` (open source, multiplataforma) |
| Referência de comportamento | Genesis Plus GX (via libretro) |

### 6.3 Convenções

- Labels: `Modulo_NomeDescritivo` (ex.: `Fight_UpdatePlayerState`, `AI_SelectMove`).
- RAM: `ram_map.toml` com endereço, tamanho, tipo, nome e confiança (`confirmed` / `hypothesis`).
- Tabelas de ponteiros recebem tipo explícito para o recompilador saber que são alvos de salto.
- Comentários de hipótese levam a marca `; ?` até serem confirmados no debugger.

### 6.4 Prioridades de mapeamento

1. Laço principal, VBlank e leitura de controle.
2. Estrutura de "lutador" na RAM: posição, estado, quadro de animação, vida, hitboxes.
3. Máquina de estados de golpes e tabelas de animação.
4. Formato de metasprites (montagem de um quadro do personagem a partir de vários sprites de hardware).
5. Cenários (tilemaps dos planos A e B, scroll, paletas).
6. IA da CPU.
7. Driver de som (só interface 68k → Z80. O código do Z80 fica emulado).

**Critério de saída**: 100% do código alcançável marcado como código, hash idêntico, mapa de RAM cobrindo a estrutura do lutador e o estado da luta.

---

## 7. Fase 2: Recompilação estática

### 7.1 Modelo de execução

- **Contexto de CPU**: `struct M68kCtx { uint32_t d[8], a[8]; uint32_t pc; Flags ccr; uint16_t sr; ... }`.
- **Flags**: avaliação preguiçosa (guarda a última operação e os operandos, e calcula N/Z/V/C/X só quando alguém lê). Começa com cálculo direto e otimiza depois de validado.
- **Memória**: `read8/16/32` e `write8/16/32` com despacho por faixa. ROM e RAM vão por acesso direto. I/O, VDP, Z80 e controles passam pelo HAL.
- **Rotinas**: cada sub-rotina 68k vira uma função C++ `void sub_XXXXXX(M68kCtx&)`. `JSR`/`BSR` diretos viram chamadas diretas.
- **Saltos indiretos** (`JMP (An)`, `JSR (An,Dn)`, tabelas): despacho por `switch` sobre os endereços conhecidos. Um endereço desconhecido vai para o **interpretador de fallback** e é registrado em `harness/missing_addresses.log`.
- **Ciclo de descoberta**: jogar → registrar endereços faltantes → recompilar → repetir. Um modo especulativo varre a ROM atrás de prólogos plausíveis.

### 7.2 Interpretador de fallback

- Um núcleo 68000 interpretado, embutido (candidato: Musashi, ou o núcleo 68k do emulador base escolhido), que compartilha o mesmo `M68kCtx` e o mesmo barramento.
- Cobre código não descoberto, código em RAM (se houver) e casos patológicos.
- Meta: zero entradas no fallback durante uma partida completa no modo arcade.

### 7.3 Tempo e interrupções

- O jogo original espera o VBlank em laços de espera ativa (polling de uma flag na RAM que o handler do VBlank altera). O recompilador **detecta esses laços** (padrão configurável em `recomp/wait_patterns.toml`) e os substitui por `rt.end_frame()`, que:
  1. apresenta o quadro;
  2. lê o input;
  3. avança o Z80 e o áudio pelo equivalente a um quadro;
  4. chama o handler de VBlank recompilado.
- HBlank: se o jogo usar (efeitos de raster, por exemplo), o VDP HAL modo A emula as linhas. Os modos B e C precisam de tradução específica (ver §9).
- A simulação roda sempre em passos fixos de 60 Hz (NTSC). PAL fica fora do escopo inicial.

### 7.4 Overrides manuais

`game/hand/` substitui rotinas geradas por versões escritas à mão, quando necessário: desempenho, ganchos de renderer, widescreen. Cada override declara o endereço original e tem um teste no harness que prova a equivalência.

**Critério de saída**: o jogo roda do boot aos créditos de forma nativa, com o VDP em modo A, sem fallback.

---

## 8. Fase 3: Validação determinística

O harness é o componente que torna o projeto viável. Ele precisa existir **antes** de qualquer trabalho no renderer.

- **Formato de movie**: `.ecm` (cabeçalho com o hash da ROM, a seed e o estado inicial, mais um quadro de input por linha, com bitmask de 12 botões).
- **Referência**: Genesis Plus GX via libretro, sem frontend, carregando o mesmo movie.
- **Comparação**: dump da RAM de trabalho de 64 KiB ao fim de cada quadro. O diff reporta o primeiro quadro divergente, os endereços e os nomes do `ram_map.toml`.
- **Suíte de CI**: um conjunto de movies cobrindo cada personagem, cada arena, as finalizações, o modo de treino e a IA em várias dificuldades.
- **Frame data**: o harness exporta tabelas de startup, active e recovery e de hitboxes por golpe para `docs/frame_data/`. Isso serve de documentação e de teste de regressão.

**Critério de saída**: 100% dos movies da suíte idênticos à referência.

---

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

## 12. Áudio

- Z80, YM2612 e SN76489 continuam **emulados** no runtime: o custo é desprezível e a fidelidade é máxima.
- O 68k conversa com o Z80 pelo HAL, como no hardware.
- Futuro opcional: interceptar comandos de música e efeitos para tocar faixas remasterizadas (áudio CD do Sega CD, por exemplo), com fallback para a síntese FM.

---

## 13. Input

- SDL3 game controller, com mapeamento para 6 botões (3 socos e 3 chutes) mais Start e Mode.
- O input é amostrado em `rt.end_frame()`, imediatamente antes do handler do VBlank. A latência fica igual à do hardware original, sem a camada do emulador. Run-ahead não é necessário.
- Teclado e arcade stick configuráveis. O replay de movies usa o mesmo caminho do input real.

---

## 14. Uso de LLMs

| Tarefa | Verificador |
|---|---|
| Propor nomes de função e comentários a partir do asm e dos traces | Revisão humana mais hash inalterado |
| Hipóteses de estrutura de RAM a partir de dumps diferenciais | Confirmação no debugger e status `confirmed` |
| Gerar código do transpilador para instruções e modos de endereçamento | Testes unitários por instrução contra o interpretador |
| Escrever overrides manuais | Teste de equivalência no harness |
| Documentar formatos (metasprites, tilemaps, frame data) | Extrator funcional e round-trip |

Regras:

- Agentes trabalham em branches. O merge só acontece com CI verde (hash e harness).
- Nada gerado por LLM vai para `ram_map.toml` como `confirmed` sem verificação dinâmica.
- Prompts e contexto ficam em `tools/llm/`, versionados.
- Modelos locais servem para anotação em massa. Modelos de fronteira, para raciocínio sobre rotinas complexas (IA, máquina de estados).

---

## 15. Stack e dependências

| Camada | Escolha | Observação |
|---|---|---|
| Linguagem do runtime | C++20 | |
| Build | CMake | Presets por plataforma |
| Plataforma | SDL3 | Vídeo, áudio, input |
| GPU | SDL_GPU (ou Vulkan/Metal/D3D12 via abstração) | Modos B e C |
| Ferramentas | Python 3.12+ | |
| Montador | vasm (m68k, sintaxe mot) | |
| Interpretador de fallback | Musashi, ou o núcleo 68k do emulador base | |
| Referência | Genesis Plus GX (libretro) | Harness. Candidato também a VDP do modo A |
| Áudio FM/PSG | Núcleos do emulador base (YM2612/SN76489) | |
| Treino da rede | PyTorch | Exporta para ONNX |
| Inferência | ONNX Runtime (TensorRT, DirectML, CoreML) ou compute shaders FP16 | §11.5 |
| Base do protótipo P2 | clownmdemu (fallback: ares) | §11.6 |

Projetos de referência para estudo:

- **Streets of Rage Project** (RuiNelson): recompilação de Mega Drive para C++ com runtime SDL3. Mesma arquitetura, ciclo de descoberta de endereços e uso de LLMs.
- **Sonic 3 A.I.R.**: modelo do produto final (lógica traduzida, engine novo, widescreen).
- **N64Recomp**: técnicas de recompilação estática e overrides.

---

## 16. Marcos e critérios de aceite

| # | Marco | Critério |
|---|---|---|
| M0 | Esqueleto | Repositório, `split.py` e `verify.py` funcionando, CI com hash |
| M1 | Disassembly | Todo o código alcançável marcado, hash idêntico, mapa de RAM do lutador |
| M2 | Boot nativo | Executável recompilado chega à tela de título (VDP modo A, fallback permitido) |
| M3 | Determinismo | Suíte de movies 100% idêntica à referência, zero fallback no modo arcade |
| M4 | Renderer B | Escala arbitrária, shaders, paridade visual com o modo A |
| M5 | Prova de conceito HD | 1 personagem com todas as animações e 1 arena no modo C, revisados |
| M6 | Widescreen | Todas as arenas em 16:9 com solução definida |
| M7 | Remaster completo | Elenco e arenas completos, assets `approved` |
| M8 | Sega CD | Port do *Challenge from the Dark Side* reaproveitando o runtime |

M1 e M3 decidem se o projeto se sustenta. Os outros dependem deles.

**Trilha paralela: upscale neural (§11)**

| # | Marco | Critério |
|---|---|---|
| N1 | Protótipo em shader (P1) | Rede de bootstrap rodando no RetroArch a 60 fps, com comparação visual contra o integer scaling e contra o Anime4K |
| N2 | Emulador com camadas (P2) | Camadas separadas exportadas, inferência por camada com cache, orçamento ≤ 2 ms no p99, extrator de dataset funcionando |
| N3 | B+ no runtime (P3) | Modo B+ integrado ao renderer (depende de M4), retreino com assets `approved` |

---

## 17. Riscos

| Risco | Impacto | Mitigação |
|---|---|---|
| Despacho indireto intenso (golpes e IA) | Fallback frequente, recompilação incompleta | Tipar as tabelas de ponteiros no disassembly e manter o ciclo de descoberta no harness |
| Dependência de timing fino do VDP (raster, DMA durante a exibição) | Divergência visual entre os modos A e B/C | Mapear os efeitos de raster na Fase 1 e criar caminhos específicos no renderer |
| Inconsistência temporal no upscale | Animações "fervendo" | Folha de referência por personagem, seed fixa, revisão manual (§10.2) |
| Paletas dinâmicas | Assets HD com a cor errada | Upscale sobre índices e mapas de região |
| Escopo (Sega CD, widescreen, remaster) | Projeto nunca fecha | Marcos sequenciais. Cada marco precisa ser útil sozinho |
| clownmdemu imprecisa para o Eternal Champions | Protótipo P2 com glitches ou travando | Teste empírico antes de começar (§11.6); fallback para o ares |
| Rede do B+ inventando detalhes que contradizem os assets curados | Estilo incoerente entre personagens HD e cenário | Treinar o aluno com os `approved`, priorizando-os no dataset; retreino periódico |
| Rede pequena demais para a qualidade desejada | B+ pouco melhor que o modo B | Medir no N1 e no N2 antes de investir; cache agressivo para liberar orçamento para uma rede maior |
| HUD e texto deformados pela rede | Legibilidade | A camada window/HUD fica fora do B+ (escala inteira ou arte HD dedicada) |
| Shadow/highlight e efeitos de raster | Composição HD errada | Aplicar como operação pós-composição; mapear os casos na Fase 1 |

---

## 18. Questões em aberto

1. Qual revisão regional da ROM usar como referência? Ela precisa ser fixada antes do M0.
2. Qual driver de som o jogo usa? Isso define a interface 68k↔Z80 a documentar.
3. O jogo usa efeitos de HBlank ou raster? Se usar, em quais arenas?
4. Reaproveitar o `MegaDriveEnvironment` do Streets of Rage Project ou escrever um runtime próprio? Depende do encaixe com os modos B/C.
5. Qual o formato final dos assets HD? PNG por quadro, atlas por personagem ou textura indexada mais LUT?
6. Que modos opcionais oferecer (treino com hitboxes visíveis, netplay por rollback)? O determinismo do M3 deixa o rollback quase de graça, então vale avaliar depois do M3.
7. Para o B+: um modelo único, com embedding do tipo de camada, ou modelos separados para planos e sprites?
8. Qual a GPU mínima suportada? Ela define o tamanho máximo da rede (§11.4).
9. Qual VDP o runtime vai usar no modo A (M2): clownmdemu, ares ou Genesis Plus GX? Usar o mesmo da referência do harness faria o modo A sair idêntico, pixel a pixel, ao que o harness compara.
