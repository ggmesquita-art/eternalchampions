<!-- orquestracao-opus:inicio -->
## Orquestração (quando a sessão principal for opus)
Em tarefas não triviais, atue como orquestrador: entenda o objetivo, decida a arquitetura,
divida o trabalho, integre os resultados e faça a revisão final. Não gaste seus próprios
turnos lendo arquivos em massa ou escrevendo código mecânico.

Roteamento:
- Localizar, varrer, inventariar (código, logs, documentos) → subagente `Explore` (Haiku)
- Implementar algo já decidido e bem especificado → subagente `executor` (Sonnet)
- Segunda leitura de um diff antes de concluir → subagente `revisor` (Sonnet)
- Arquitetura, ambiguidade, trade-offs, integração e decisão final → você mesmo

Regras:
- Tarefa pequena (poucos minutos, 1–2 arquivos): faça direto; delegar custa mais que fazer.
- Cada delegação leva: objetivo, arquivos/caminhos permitidos, critério de pronto e como verificar.
- Subagentes independentes rodam em paralelo.
- Nunca aceite "pronto" sem conferir: rode você mesmo os testes/checagens críticos antes de concluir.
<!-- orquestracao-opus:fim -->

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

When the user types `/graphify`, use the installed graphify skill or instructions before doing anything else.

Rules:

- When graphify-out/graph.json exists, run `graphify query "<question>"` before your first grep, search or source-file read for a codebase question, and put this rule in every subagent prompt that explores code. Read raw files only for lines the graph or an earlier read already located. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. When the answer is under a Markdown heading, `graphify section "<heading>"` prints that section's text, so the .md need not be opened. These return a scoped subgraph, usually much smaller than GRAPH\_[REPORT.md](http://REPORT.md) or raw grep output.

- Dirty graphify-out/ files are expected after hooks or incremental updates; dirty graph files are not a reason to skip graphify. Only skip graphify if the task is about stale or incorrect graph output, or the user explicitly says not to use it.

- If graphify-out/wiki/[index.md](http://index.md) exists, use it for broad navigation instead of raw source browsing.

- Read graphify-out/GRAPH\_[REPORT.md](http://REPORT.md) only for broad architecture review or when query/path/explain do not surface enough context.

- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).

- A committed graphify-out/graph.json is rewritten whole on every build, so a large diff to it in a pull request is expected. Do not flag it as churn or suggest skipping the graph rebuild or its hooks.

