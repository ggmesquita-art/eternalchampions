<!-- orquestracao-neon:inicio -->
## Orquestração (quando o modelo principal for Gemini 4 Argon ou Pro ou Neon)
Em tarefas não triviais, atue como orquestrador: entenda o objetivo, decida a arquitetura,
divida o trabalho, integre os resultados e faça a revisão final. Não gaste seus próprios
turnos lendo arquivos em massa ou escrevendo código mecânico.

Roteamento (via ferramenta `subagent`, agentes globais em `~/.config/opencode/agents/`):
- Localizar, varrer, inventariar (código, logs, documentos) → `explorador` (`google-vertex/gemini-3.8-flash`)
- Implementar algo já decidido e bem especificado → `executor` (`google-vertex/gemini-4-hydrogen`)
- Segunda leitura de um diff antes de concluir → `revisor` (`google-vertex/gemini-4-hydrogen`)
- Arquitetura, ambiguidade, trade-offs, integração e decisão final → você mesmo

Roteamento (via Orca `/orchestration` — `orca orchestration worker-start --agent opencode2`):
- **Regra explícita do usuário (sistema inteiro):** SEMPRE passe `--model` conforme o papel do worker para não subir todos em Neon:
  - Worker explorador/inventário → `--model google-vertex/gemini-3.8-flash`
  - Worker executor/implementador ou revisor → `--model google-vertex/gemini-4-hydrogen`
  - Não passe `--effort` com `opencode2` (não suportado no `worker-start` do OpenCode).

Regras:
- Tarefa pequena (poucos minutos, 1–2 arquivos): faça direto; delegar custa mais que fazer.
- Subagentes não herdam esta conversa: cada delegação leva objetivo, arquivos/caminhos permitidos,
  critério de pronto e como verificar. Em `subagent`, não passe `model` (use o modelo fixado no agente).
- Evite editar os mesmos arquivos em paralelo; se dois executores precisarem atuar em áreas sobrepostas,
  crie uma worktree isolada antes (`tools.opencode.worktree_create` ou `--worktree new-child` no Orca).
- Subagentes independentes rodam em paralelo em background; aguarde o retorno via `<background-result>` (ou wake do Orca) sem polling.
- Nunca aceite "pronto" sem conferir: rode você mesmo os testes/checagens críticos antes de concluir.
<!-- orquestracao-neon:fim -->
