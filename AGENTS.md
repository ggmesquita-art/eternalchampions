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

Diretrizes Globais de Desenvolvimento
Regras de execução para qualquer agente/modelo neste ambiente. Em caso de conflito, a regra mais específica prevalece; segurança e veracidade prevalecem sobre velocidade.
0. Escopo de Aplicação
Tarefas de código em projeto (criar/alterar/refatorar código, schemas, configs de build): aplicam-se todas as seções.
Tarefas operacionais ou perguntas (abrir apps, explicar, consultar): execute diretamente; as seções 3.2, 4 e 7.1 não se aplicam.

1. Postura e Comunicação
Comunicação cirúrgica: vá direto à causa-raiz e à solução. Sem introduções genéricas, pedidos de desculpas ou explicações de conceitos básicos.
Desafio de premissa: se o pedido contiver falha arquitetural, anti-pattern, risco de segurança ou gargalo de performance, aponte-o na primeira linha da resposta e proponha a alternativa correta antes de implementar.
Veracidade: nunca afirme ter executado, aberto, testado ou verificado algo que não foi de fato feito. Relate resultados reais, incluindo falhas e etapas não executadas.
2. Zero Suposições e Zero Mocks
Read-before-write: inspecione no workspace as definições reais (assinaturas, contratos de API, schemas, caminhos de arquivos) antes de usá-las ou alterá-las. Nunca presuma assinaturas de métodos de bibliotecas ou módulos internos.
Dependências reais: antes de importar uma biblioteca ou módulo, confirme que ele existe (`package.json`, `pyproject.toml`, `requirements.txt`, etc.).
Proibido dado fictício: não gere arrays hardcoded, objetos falsos, `mockFn()` ou retornos estáticos para simular funcionamento, exceto em arquivos de teste (`*.spec.ts`, `*.test.py`, `test_*.py`) ou quando solicitado pelo usuário.
Proibido atalho para passar em teste: não hardcode valores esperados, não crie casos especiais para inputs de teste e não altere, remova ou pule testes, salvo se o contrato tiver sido revisado no `design.md`. Se um teste parecer errado, explique o motivo e pergunte.
Falha concreta > simulação: se banco de dados, API externa, variável de ambiente ou serviço não estiver configurado/disponível, interrompa a execução e relate o bloqueio.
3. Execução Incremental
3.1 Passo Único
Faça exatamente o que foi pedido. Não adicione features, abstrações, opções de configuração, tratamento de erro para cenários impossíveis nem refatorações "de passagem".
Não crie arquivos desnecessários no projeto (README, docs, `.md` de resumo, scripts auxiliares) sem pedido explícito; prefira editar arquivos existentes. Remova arquivos temporários ao final.
Uma subetapa lógica por iteração (ex.: um tipo/interface, uma migração, uma rota). Não antecipe etapas futuras de `design.md`, `PRD.md` ou do pedido.
Um arquivo/componente por iteração. Se a subetapa exigir inevitavelmente mais de um arquivo ou alterar um contrato de dados, apresente antes um plano curto — arquivos afetados, breaking changes/edge cases, comando de verificação — e aguarde aprovação.
Minimal diff: altere estritamente o necessário. Não reformate código adjacente, não renomeie fora do escopo e não remova comentários ou funcionalidades sem instrução explícita.
3.2 Gestão de Tarefas (`task.md`)
Quando o projeto tiver `design.md`, antes de iniciar uma fase gere/atualize `task.md` com itens `- [ ]` de 15–30 min cada.
Se `design.md` ultrapassar 200 linhas, proponha dividi-lo em `docs/phase-N.md` e carregue apenas a fase ativa.
Ciclo por item: 1) isole o próximo `- [ ]`; 2) implemente apenas ele; 3) valide (seção 4); 4) marque `- [x]`; 5) pare e solicite autorização explícita para prosseguir.
4. Quality Gates (Definition of Done)
Nenhuma tarefa de código está concluída apenas com a escrita. Antes de encerrar o turno, execute:
Lint/formatação: linter configurado no projeto (ex.: `eslint`, `ruff`, `flake8`, `clippy`, `prettier`).
Typecheck/compilação: ex.: `tsc --noEmit`, `mypy`, `npm run typecheck` (se o script existir).
Testes: suíte relevante ao módulo alterado (ex.: `npm test`, `pytest`).
Auto-correção: se algo falhar, leia o traceback completo, isole a linha exata, corrija a causa-raiz e reexecute até exit code 0 — antes de avançar para qualquer outro item.
Regras complementares:
Comandos reais: use apenas comandos configurados no projeto. Se não houver linter, typecheck ou testes, declare isso na resposta em vez de inventar.
Proibido mascarar erros: nada de `@ts-ignore`, `eslint-disable`, `# type: ignore`, `any` ou `try/catch` silencioso. Corrija tipagem e fluxo de dados na origem.
5. Padrões Técnicos (quando aplicáveis à stack do projeto)
Stack de referência: TypeScript (strict mode), Node.js, React/Next.js, Python 3.12+.
Tipagem estrita: sem `any` implícito ou explícito; use `unknown` com type guards ou validação por schema (Zod/Pydantic) nas fronteiras de entrada/saída.
Erros explícitos: funções assíncronas e chamadas externas tratam falhas explicitamente, com erros tipados e mensagens diagnósticas claras.
Estado: prefira funções puras, imutabilidade e operações idempotentes.
6. Contexto e Subagentes
Mantenha no contexto apenas os arquivos da microtarefa atual e suas dependências imediatas (imports/exports, tipos).
Delegue pesquisa pesada, varredura extensa de código ou geração de schemas complexos a subagentes e consuma apenas o relatório final.
Não recomende `/goal` para alterações estruturais de banco, refatorações multi-arquivo ou integrações de APIs críticas; use o modo interativo passo a passo.
7. Formato de Resposta
7.1 Após Alteração de Código

```markdown
### 🎯 Estado Atual
- **Tarefa Concluída:** [microtarefa]
- **Item Marcado:** `- [x] [Nome da tarefa]` no `task.md` (ou "N/A — sem task.md")

### 🛠️ Alterações Realizadas
- `caminho/para/arquivo.ext`: [o que mudou e por quê]

### 🧪 Validações Executadas
- Lint/Typecheck: [PASSOU | FALHOU | NÃO CONFIGURADO] — `[comando executado]`
- Testes: [PASSOU (N testes) | FALHOU | NÃO CONFIGURADO] — `[comando executado]`

### ❓ Próximo Passo Proposto
- [ ] [Próxima microtarefa]
- **Aguardando confirmação do usuário para prosseguir.**
```

7.2 Complementos
Bugs/diagnósticos: antes do template, informe 1) causa-raiz (`arquivo:linha`), 2) correção aplicada, 3) resultado do teste/build.
Revisões/arquitetura: liste trade-offs técnicos (prós e contras) e dê uma recomendação definitiva.
8. Segurança e Operações Irreversíveis
Não execute `git commit`, `git push`, `reset --hard`, `rebase`, force push, `rm -rf`, `DROP`/`TRUNCATE` ou qualquer comando destrutivo sem pedido explícito.
Nunca leia, exiba ou versione segredos (`.env`, credenciais, tokens); referencie-os apenas por nome de variável.

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

