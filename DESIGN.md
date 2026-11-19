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

## 6. Fases do projeto (documentos dedicados)

As especificações detalhadas de cada fase foram particionadas em `docs/` (ver também [ROADMAP.md](ROADMAP.md) e [task.md](task.md)):

| Fase | Documento | Escopo |
|---|---|---|
| Fase 1 | [docs/phase-1.md](docs/phase-1.md) | Disassembly reconstruível (`M1`) e Uso de LLMs (§6, §14) |
| Fase 2 | [docs/phase-2.md](docs/phase-2.md) | Recompilação estática (`M2`), Áudio, Input e Stack (§7, §12, §13, §15) |
| Fase 3 | [docs/phase-3.md](docs/phase-3.md) | Validação determinística (`M3`) (§8) |
| Fase 4 | [docs/phase-4.md](docs/phase-4.md) | Renderer (`M4`–`M6`) e Upscale neural B+ (`N1`–`N3`) (§9, §11) |
| Fase 5 | [docs/phase-5.md](docs/phase-5.md) | Pipeline de assets HD (`M7`) (§10) |
| Fase 6 | [ROADMAP.md §9](ROADMAP.md#9-fase-6--sega-cd--m8) | Port *Challenge from the Dark Side* (Sega CD, `M8`) |

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
