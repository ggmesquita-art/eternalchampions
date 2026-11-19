# Fase 1 — Disassembly Reconstruível (`M1`)

> Extraído de [DESIGN.md](../DESIGN.md). Progresso ativo em [task.md](../task.md) e [PROGRESS.md](PROGRESS.md).

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
