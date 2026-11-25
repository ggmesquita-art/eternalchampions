# Roadmap — APIs gráficas modernas e geração de quadros

**Estado:** planejado, **adiado** (2026-11-25). Nada daqui foi iniciado.
**Objetivo:** mais fluidez visual a 120/144 Hz sem mudar a lógica do jogo, que continua a ~59,94 ticks/s. A emulação, o áudio, a entrada e os saves seguem idênticos.

O trabalho tem três frentes, na ordem pedida:

1. **Trilha V:** suporte a Direct3D 11, Direct3D 12 e Vulkan.
2. **Trilha X:** geração de quadros externa (Lossless Scaling, NVIDIA Smooth Motion, Optiscaler e FG nativo FSR 3 / DLSS).
3. **Trilha I:** interpolação nativa a partir do estado do jogo.

A ordem de execução recomendada é outra e está no §5.

---

## 0. Ponto de partida (medido no código em 2026-11-25)

| Fato | Onde |
|---|---|
| O runner pede `direct3d11` ao SDL por padrão, mas o caminho de GPU troca para `opengl` | `segagenesisrecomp/runner/main.c:3273` e `segagenesisrecomp/runner/video/genesis_hd_gpu.c:21` |
| Todo aprimoramento de GPU do jogo exige o renderizador SDL `opengl` com *target texture*: **cadeia de shaders** (GLSL, presets `.glslp`) | `game/ec_shader_chain.c:1201` (1.827 linhas) |
| **B+ neural** em compute shader GL 4.3; backend alternativo ONNX Runtime/CUDA | `neural/infer/ec_bplus_gl.c:880`, `neural/infer/ec_bplus_ort.c` |
| **Modo C** (planos HD) em GL | `game/ec_hd.c:1657` |
| A apresentação de GPU do jogo passa pelo gancho `present_gpu`, que devolve uma `SDL_Texture` | `game/ec_video.c:1258`, contrato em `segagenesisrecomp/runner/game_video.h:84-90` |
| O runner já tem um **contrato de interpolação**: `presentation_configure`, `presentation_capture`, `interpolate(phase, …)`, `presentation_reset` e `presentation_state_save/load`. O EC **não implementa** nenhum deles | `game_video.h:63-81` |
| O modo de alta taxa só liga com `presentation_rate ≠ 0`, `target_fps` entre 40 e 70, altura 224 **e** apresentação nativa (`frame_texture == texture`). Ou seja: **desliga com B+/shader/Modo C** | `main.c:4544-4546` |
| Os presets atuais são GLSL (`assets/shaders/presets/*.glslp`: lite-scalefx, lite-superxbr, remaster-crisp, remaster-hq, remaster-smooth) | |
| Custo do B+ v3 HQ medido em RTX 5060 Ti: ~6,4 ms por quadro a 2048×896 | `build/logs/game_last.log` |
| `--target-fps` muda o ritmo dos **ticks** do jogo, não gera quadros | `main.c` (frame pacer) |

Consequências:
- Nenhuma ferramenta que dependa de DX11/12/Vulkan enxerga o jogo hoje. Isso vale para o Smooth Motion e o Optiscaler.
- Sem B+/shaders, o renderizador já é D3D11. Com eles, é OpenGL.

## 1. Regras que valem para todas as fases

- **Só no lado do host.** Nada altera o estado canônico, o RNG, o tempo do guest, o áudio ou o netplay. Os portões de cada entrega são:
  - `harness/ec_harness.py --quick` com `det=0/65536`, misses `(0,0,0,0)` e WS `wram_diff=0/65536`;
  - depois do harness, `git checkout -- docs/frame_data/fight_probe_summary.md`.
- **OpenGL continua sendo o caminho de referência** até que cada backend novo alcance paridade visual nas capturas do §6.
- **Fallback sempre.** Se o backend pedido falhar, o jogo volta ao anterior com uma linha no log, nunca com tela preta.
- **Latência é métrica de primeira classe**, porque é um jogo de luta. Toda forma de geração de quadros mostra o custo em ms no log e na documentação, e vem desligada por padrão.
- **Runner = motor compartilhado** (`segagenesisrecomp/`). As mudanças nele ficam em commits próprios e não tocam nas alterações pendentes de terceiros (`tools/rb_*.sh`, `tools/rb_statics_resolve.py` e os submódulos).
- **Opções novas:** toda opção entra no `settings.ini`, no esquema do launcher (`launcher/src/schema.cpp`) e em `docs/CONFIGURATION_AND_CLI.md`.

---

## 2. Trilha V — Direct3D 11, Direct3D 12 e Vulkan

Estratégia em duas camadas:
- **V1–V3 (interop):** o jogo **apresenta** por DXGI/Vulkan, mas continua **renderizando** em GL. É barato e já destrava o Smooth Motion e a apresentação *flip-model*.
- **V4–V7 (nativo):** cada módulo de GPU é portado para rodar sem GL. Fica para quando a camada de interop não bastar: FG nativo em DX12/Vulkan, ou máquinas sem GL bom.

### V0 — Camada de apresentação abstrata (pré-requisito)
- Criar `segagenesisrecomp/runner/video/present_backend.{h,c}` com uma interface mínima: `init(window, api)`, `acquire_target(w, h)`, `submit(texture, src_rect, dst_rect)`, `present(vsync, tearing)`, `resize`, `shutdown`, `query_caps`.
- O backend `sdl_renderer` empacota o caminho atual sem mudar comportamento.
- Seleção por `[video] backend = auto|opengl|d3d11|d3d12|vulkan` e `--video-backend`.
- **Pronto quando:**
  - o backend `sdl_renderer` é bit-idêntico ao atual nas capturas;
  - o harness passa limpo;
  - o log mostra `[VIDEO] backend=…`.

### V1 — Apresentação D3D11 com interop GL
- Criar a swapchain DXGI *flip-model* (`DXGI_SWAP_EFFECT_FLIP_DISCARD`) com `ALLOW_TEARING` quando suportado, para VRR.
- Ligar a textura final do GL a uma textura D3D11 compartilhada via `WGL_NV_DX_interop2`, com `wglDXLockObjectsNV`/`Unlock` por quadro.
- Toda a cadeia (Modo C → B+ → shaders) segue em GL e renderiza direto na textura compartilhada.
- O `present_gpu` do EC (`game/ec_video.c:1258`) passa a entregar o alvo ao `present_backend` em vez de devolver uma `SDL_Texture`.
- **Riscos:** o interop exige driver NVIDIA/AMD com a extensão (Intel varia), e o contexto GL é criado pelo SDL. Fallback: copiar via PBO para uma textura D3D11 (mais lento, mas funciona).
- **Pronto quando:**
  - o jogo, com B+ HQ e shader, roda com apresentação D3D11;
  - a diferença para a captura OpenGL é ≤ 1 LSB por canal;
  - o custo extra é ≤ 0,3 ms por quadro;
  - a apresentação aparece como DX11 *flip* no overlay da NVIDIA ou no PresentMon.

### V2 — Apresentação D3D12 com interop
- D3D12 + DXGI *flip-model*. Interop GL→D3D12 por recurso compartilhado: o D3D11 do V1 compartilha um handle NT com o D3D12 (`ID3D11Device5::CreateFence` + `OpenSharedHandle`), com fences para sincronizar.
- Base necessária para o FG nativo FSR 3 / DLSS (X4), que exige DX12 ou Vulkan.
- **Pronto quando:** os mesmos critérios do V1, mais sincronização por fence sem *stalls* (CPU ociosa ≤ 0,2 ms por quadro esperando).

### V3 — Apresentação Vulkan com interop
- Instância e dispositivo Vulkan com swapchain `VK_PRESENT_MODE_FIFO_KHR` ou `MAILBOX_KHR`.
- Interop GL↔VK por memória externa (`GL_EXT_memory_object_win32` + `VK_KHR_external_memory_win32`) e semáforos externos (`GL_EXT_semaphore_win32` + `VK_KHR_external_semaphore_win32`).
- O SDL2 não tem renderizador Vulkan: o backend cria a superfície por `SDL_Vulkan_CreateSurface`, e a janela precisa de `SDL_WINDOW_VULKAN` quando o backend for Vulkan.
- **Pronto quando:** os mesmos critérios do V1, com o *validation layer* limpo numa sessão de 5 minutos.

### V4 — Cadeia de shaders multi-API (substitui `ec_shader_chain.c`)
- **Decisão recomendada: integrar a [librashader](https://github.com/SnowflakePowered/librashader)** (API em C; *runtimes* GL, D3D11, D3D12 e Vulkan; formato *slang* `.slangp`), em vez de manter 4 implementações próprias.
- Converter os 5 presets de `.glslp` para `.slangp` (as versões *slang* do repositório libretro/slang-shaders cobrem scalefx, super-xBR e FSR). Os **parâmetros** dos presets e a chave `ec.video.shader_params` seguem compatíveis por nome.
- O launcher passa a listar `.slangp`; o `.glslp` fica só no backend GL durante a transição.
- **Licenças:** a librashader usa MPL-2.0 (ou GPLv3); os shaders têm cada um a sua, indicada no cabeçalho. Registrar em `THIRD-PARTY-LICENSES.md`.
- **Pronto quando:**
  - cada preset, em cada API, tem PSNR ≥ 45 dB contra a referência GL no quadro de abertura (frame 1236) e numa luta;
  - o p99 por quadro fica ≤ o da implementação GL.

### V5 — B+ neural sem GL
- **D3D11/D3D12:** portar os compute shaders GLSL 4.3 (`ec_bplus_gl.c`) para HLSL cs_5_0/cs_6_0 com os mesmos pesos `.bin`. Os kernels são convoluções pequenas, então o port é direto.
- **Vulkan:** mesmo GLSL compilado para SPIR-V (glslang em build time).
- **Alternativa a avaliar:** ONNX Runtime com DirectML (D3D12), reaproveitando os `.onnx` já exportados (`assets/models/*.onnx`). Compará-la com o HLSL próprio em custo e paridade.
- **Pronto quando:**
  - a saída tem diferença máxima ≤ 1/255 contra o B+ GL;
  - o custo do v3 HQ fica ≤ 6,4 ms (a referência GL);
  - os modos `dedither`/`intensity` são equivalentes.

### V6 — Modo C sem GL
- Portar a composição de planos HD (`game/ec_hd.c`) para D3D11/12/Vulkan. As texturas dos manifestos (AnimeSharp/Flux) são as mesmas; só muda o upload e o *blend*.
- **Pronto quando:** as capturas `modo_c` de `docs/modo_c_planos.md` são idênticas a ≤ 1 LSB.

### V7 — Backends nativos completos e limpeza
- Com o V4–V6, cada backend renderiza sem GL e o interop do V1–V3 vira fallback.
- O launcher ganha a opção "API gráfica" (Auto/OpenGL/D3D11/D3D12/Vulkan) na tela de Vídeo, com teste de disponibilidade.
- **Pronto quando:**
  - matriz {4 APIs} × {nativo, B+, shader, Modo C, widescreen} sem regressão visual;
  - harness limpo;
  - `docs/LAUNCHER.md` e `docs/CONFIGURATION_AND_CLI.md` atualizados.

---

## 3. Trilha X — Geração de quadros externa e por API

### X1 — Lossless Scaling (LSFG): **sem dependências, pode vir primeiro**
O Lossless Scaling captura a janela (WGC) e funciona com o jogo como está, inclusive em OpenGL e com B+ e shaders. "Implementar" aqui é deixar o jogo pronto para ele e documentar:
- **Launcher:** um perfil "Compatível com Lossless Scaling" que aplica:
  - `video.fullscreen = 2` (janela sem borda);
  - `presentation_rate = 0`, para não somar interpolação nativa com LSFG;
  - vsync como recomendado;
  - o overlay do runner desligado.
  Esse perfil também mostra o atalho padrão do LS e o aviso de latência.
- **Documentação:** um guia em `docs/LAUNCHER.md` com configuração recomendada:
  - LSFG 3.x em modo *Fixed* 2×;
  - escala Off quando o B+ ou shader já fazem o upscale;
  - captura WGC;
  - G-Sync ligado.
  O guia também lista os limites conhecidos: borrão em sprites que piscam a 30 Hz, nas trocas de cena e no HUD.
- **Medição:** registrar a latência *click-to-photon* com e sem LS, no mínimo pelo PresentMon e, idealmente, com câmera a 240 fps. O resultado entra no documento.
- **Pronto quando:** o perfil existe no launcher, o guia está publicado e a latência foi medida.

### X2 — NVIDIA Smooth Motion: **depende do V1 (D3D11) ou do V2 (D3D12)**
- É geração de quadros no driver para GPUs RTX 50 (a RTX 5060 Ti é elegível), ligada no NVIDIA App por jogo. O jogo só precisa apresentar por DX11/DX12; o suporte a Vulkan depende da versão do driver.
- Não há API pública para ligá-la pelo código. O launcher só detecta a GPU e orienta: link e passo a passo no NVIDIA App, perfil do `EternalChampionsRecomp.exe`.
- **Testar:** se ela engata com o interop do V1 (o *present* é DX11 *flip*), o efeito em sprites que piscam e a latência medida.
- **Pronto quando:** está confirmado engatando com B+ HQ via V1, latência medida e guia escrito.

### X3 — Optiscaler: **só depois do X4**
- O Optiscaler intercepta chamadas de **upscaler** (DLSS, FSR, XeSS) em jogos DX11/12/Vulkan e pode ligar FG em cima delas. Este jogo não chama nenhum upscaler, então hoje ele **não tem o que interceptar**. Por isso a pasta `Optiscaler_*` no repositório não serve no estado atual e não deve ir para o git.
- Passa a fazer sentido quando o X4 existir: com o jogo chamando a API do FSR 3.1, o Optiscaler pode trocar o upscaler ou o FG (por DLSS ou XeSS). Ele entra como opção **do usuário**: não distribuir, e checar a licença antes de qualquer menção no launcher.
- **Pronto quando:** o Optiscaler instalado pelo usuário engata sobre o X4 em DX12, e o guia está escrito.

### X4 — FG nativo por API: FSR 3.1 (FidelityFX SDK) e, opcionalmente, DLSS Frame Generation (Streamline)
- **Depende do V2 (D3D12) ou do V3 (Vulkan)**, e do **I1–I3** para vetores de movimento exatos.
- FSR 3.1 FG (MIT, DX12/Vulkan, qualquer GPU) recebe:
  - a cor final;
  - **vetores de movimento** e profundidade (aqui, uma profundidade sintética por camada: plano B < plano A < sprites);
  - uma imagem *hudless* opcional;
  - a câmera, e o sinal de *reset* nas trocas de cena.
- Os vetores vêm do estado do jogo (I2/I3), não de fluxo óptico. É o que dá qualidade em pixel art.
- DLSS-G (Streamline, só RTX 40/50) só entra depois que o FSR 3.1 estiver estável. Checar a licença de redistribuição do SDK.
- **Pronto quando:**
  - o FG 2× funciona com B+ HQ;
  - a latência medida é ≤ a do LSFG;
  - sem artefatos nas trocas de cena (o *reset* dispara);
  - harness limpo.

---

## 4. Trilha I — Interpolação nativa (contrato do runner)

Implementa no EC o contrato que o runner já tem: `presentation_capture` + `interpolate(phase)`. O runner chama `interpolate` várias vezes por tick (por exemplo 2,4 vezes a 144 Hz), com `phase` de 0 (tick anterior) a 1 (tick atual). **Isso adiciona 1 tick (~16,7 ms) de latência visual.**

### I0 — Liberar alta taxa com GPU (runner + EC)
- Remover a exigência `frame_texture == texture` (`main.c:4546`): quando há `present_gpu`, o quadro interpolado passa pela cadeia de GPU a cada apresentação.
- **Decisão de custo**, a medir antes de escolher:
  - **(a) Re-render e re-aprimorar** cada quadro intermediário. Simples e correto. Custa o B+ e o shader × (taxa do monitor ÷ 60): o B+ HQ a 144 Hz chega a ~15 ms em cada 16,7 ms, inviável. Serve para v3 fast e shaders leves.
  - **(b) Aprimorar uma vez por tick e deslocar o resultado aprimorado** por camada (planos e sprites em alvos separados), com os mesmos vetores do I2/I3. É mais barato, mas exige separar camadas na cadeia, e oclusões abrem buracos.
  - **Recomendação:** (a) primeiro, como base correta; (b) só se o B+ HQ for requisito.
- **Pronto quando:** `presentation_rate = 144` com shader leve mostra `interpolated>0` no log, o p99 de apresentação fica abaixo de 6,9 ms e o harness está limpo.

### I1 — Captura por tick
- `presentation_capture` copia por tick: hscroll por linha dos planos A e B, vscroll (global e por coluna), a tabela de sprites (SAT: x, y, tamanho, link, tile, paleta, prioridade), o estado da janela, a paleta e os registradores de modo relevantes.
- Usar *double buffer*: tick anterior e tick atual.
- `presentation_reset` em load, rewind e troca de modo; `presentation_state_save/load` para o rewind.
- **Pronto quando:** a captura é idêntica ao estado da VDP (teste com um dump de VRAM e SAT em 3 cenas) e o custo é ≤ 0,1 ms por tick.

### I2 — Câmera e planos
- Interpolar hscroll e vscroll entre os ticks com **aritmética modular** (planos de 256, 512 ou 1024 px fazem a volta).
- Detectar descontinuidade: |Δ| acima de um limite (por exemplo 32 px por tick) ou mudança de base de plano, paleta ou modo da VDP leva a `phase = 1`, sem interpolar.
- Re-renderizar o quadro nativo no `phase` com o mesmo rasterizador do runner.
- **Atenção:** o widescreen (Modo A) e o scroll de chão por linha das arenas (`floor_coverage.toml`) precisam interpolar **linha a linha**, não só o valor global.
- **Pronto quando:** o *pan* da câmera nas arenas fica contínuo a 144 Hz (trace de apresentação sem saltos) e não há artefato nas trocas de tela (round, vitória, menus).

### I3 — Sprites (lutadores, projéteis, efeitos)
- **Identidade entre ticks:** casar sprites por (slot da SAT, tile base, tamanho e paleta) e agrupar *metasprites* pela cadeia de link e pela proximidade. Sem par confiável, mostrar o tick atual (`phase = 1`) para aquele grupo.
- **Animação:** o quadro do desenho não é interpolado. O grupo usa a arte do tick anterior até `phase < 0,5` e a do atual depois, com a posição interpolada o tempo todo.
- **Casos especiais:**
  - *hit-stop* (posição parada) não precisa de nada;
  - teleportes e trocas de personagem disparam o limite de descontinuidade;
  - o tremor de tela intencional é detectado por oscilação e não é suavizado;
  - sprites que piscam a 30 Hz nunca são interpolados em alfa.
- **Pronto quando:** em 20 lutas (as rotas da `docs/movements`) não há duplicação nem "fantasma" nas capturas por fase, e a revisão visual está aprovada.

### I4 — Vetores de movimento e profundidade sintética (base do X4)
- Gerar a partir do I2/I3 um alvo RG16F com o deslocamento por pixel, uma profundidade sintética por camada e o sinal de *reset* da cena. É exatamente o que o FSR 3.1 FG e o DLSS-G pedem.
- **Pronto quando:** o alvo é exportável numa captura de depuração e coerente com as diferenças entre ticks (erro médio < 0,5 px nas cenas de teste).

### I5 — Produto
- No launcher, a opção "Suavização de movimento" (Desligada / 120 / 144 / monitor) mapeia para `presentation_rate`, com aviso de +1 tick de latência e sugestão de desligar no modo competitivo.
- Desligada automaticamente no netplay (o runner já faz isso), no turbo e no `--runtime-uncapped`.
- **Pronto quando:** guia em `docs/LAUNCHER.md`, latência medida, harness limpo e captura de alta taxa no trace (`presentation_trace`).

---

## 5. Ordem de execução recomendada

| # | Fase | Por quê | Depende de |
|---|---|---|---|
| 1 | **X1** Lossless Scaling | ganho imediato, sem código no motor | nada |
| 2 | **V0** camada de apresentação | base para todo o resto | nada |
| 3 | **V1** D3D11 por interop | destrava Smooth Motion e *flip-model* | V0 |
| 4 | **X2** Smooth Motion | ganho sem código de FG | V1 |
| 5 | **I0 → I1 → I2** câmera | primeira interpolação nativa, risco baixo | I0 (com V0 recomendado) |
| 6 | **I3** sprites | interpolação nativa completa | I2 |
| 7 | **V2 / V3** D3D12 e Vulkan por interop | APIs exigidas pelo FG nativo | V0 |
| 8 | **I4** vetores de movimento | insumo do FG nativo | I3 |
| 9 | **X4** FSR 3.1 FG (depois DLSS-G) | melhor qualidade de FG com latência controlada | V2 ou V3, I4 |
| 10 | **X3** Optiscaler | opção do usuário sobre o X4 | X4 |
| 11 | **V4–V7** ports nativos | só se o interop limitar (desempenho, Intel/AMD, Linux) | V1–V3 |

Tamanho relativo, sem datas: X1 **P**, V0 **M**, V1 **M**, X2 **P**, I0–I2 **M**, I3 **G**, V2 **M**, V3 **M**, I4 **M**, X4 **G**, X3 **P**, V4 **G**, V5 **M**, V6 **M**, V7 **M**.

## 6. Validação comum a todas as fases

- **Determinismo:** `ec_harness.py --quick` com `det=0/65536`, misses `(0,0,0,0)` e `wram_diff=0/65536`. Toda geração de quadros fica fora do estado canônico.
- **Imagem:** capturas no frame 1236 (abertura, mesmo roteiro da prévia do launcher) e num quadro de luta fixo, por API e por modo (nativo, B+ v3 fast/HQ, cada preset, Modo C, widescreen 16:9 e 21:9).
- **Ritmo de apresentação:** `presentation_trace` mais PresentMon, com p50/p99 do intervalo entre apresentações, quadros perdidos e `interpolated` por segundo.
- **Latência:** PresentMon (*PC latency* quando disponível) e, para decisões finais, câmera de 240 fps (botão → reação na tela), com e sem cada forma de FG.
- **Custo:** ms por quadro de cada etapa, já registrados no log (`[SHADER]`, `[BPLUS_GL]`), mais o da etapa nova.

## 7. Fora de escopo

- Geração de quadros por fluxo óptico próprio, sem vetores do jogo: a qualidade em pixel art não compensa o custo.
- Mudar a taxa lógica do jogo (`--target-fps`) como "fluidez": isso altera a velocidade do jogo.
- Distribuir Lossless Scaling, Optiscaler ou SDKs proprietários junto com o projeto.
