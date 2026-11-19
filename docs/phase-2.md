# Fase 2 — Recompilação Estática (`M2`)

> Extraído de [DESIGN.md](../DESIGN.md). Ver também [ROADMAP.md](../ROADMAP.md).

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
