#!/bin/bash
# setup-antigravity-orquestrador.sh
# Configura o Antigravity (agy CLI e app) para o padrão
# "Gemini 4 Argon orquestra, Gemini Flash executa, Pro revisa".
# Roda em macOS (bash 3.2), Linux e WSL. Idempotente: pode rodar de novo sem duplicar nada.
#
# Uso:
#   bash setup-antigravity-orquestrador.sh             # global (~/.gemini/config/agents + ~/.gemini/AGENTS.md)
#   bash setup-antigravity-orquestrador.sh --projeto   # só no repositório atual (.agents/agents + .agents/AGENTS.md)
#
# Não toca em ~/.gemini/GEMINI.md, que o Gemini CLI e o Antigravity IDE também usam.

set -euo pipefail

MODO="global"
for arg in "$@"; do
  case "$arg" in
    --projeto) MODO="projeto" ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) echo "Opção desconhecida: $arg (use --help)"; exit 1 ;;
  esac
done

if [[ "$MODO" == "projeto" ]]; then
  AGENTS="./.agents/agents"
  REGRAS="./.agents/AGENTS.md"
else
  AGENTS="$HOME/.gemini/config/agents"
  REGRAS="$HOME/.gemini/AGENTS.md"
fi
mkdir -p "$AGENTS" "$(dirname "$REGRAS")"

backup() { [[ -f "$1" ]] && cp -p "$1" "$1.bak.$(date +%Y%m%d%H%M%S)" || true; }

# ---------------------------------------------------------------------------
# 1. Executor: implementação de tarefas bem definidas (Flash)
#    Só usa nomes de ferramenta documentados; nome errado pode travar o subagente.
# ---------------------------------------------------------------------------
backup "$AGENTS/executor.md"
cat > "$AGENTS/executor.md" <<'EOF'
---
name: executor
description: Implementa tarefas delimitadas e bem especificadas (código, refatoração, testes, transformação de dados). Use quando o orquestrador já decidiu O QUE fazer e só falta fazer.
tools:
  - view_file
  - grep_search
  - replace_file_content
  - run_command
subagent: true
mainAgent: false
model: flash
commandExecutionPolicy: sandbox
---
# Executor

Você recebe do orquestrador uma tarefa já decidida.

Regras:
- Faça só o que foi pedido. Não amplie o escopo nem refatore o que não foi solicitado.
- Escreva apenas nos caminhos indicados na tarefa. Para criar um arquivo novo, use run_command.
- Antes de reportar, rode a verificação que a tarefa pedir (testes, lint, build). Se não houver, diga que não verificou.
- Nunca altere testes, configuração de CI ou o ambiente para fazer uma verificação passar.
- Se a tarefa for ambígua ou exigir decisão de arquitetura, pare e devolva a dúvida em vez de escolher.

Relatório final (curto, sem colar arquivos inteiros):
1. O que mudou (arquivo:linha)
2. Como verificou e o resultado
3. Dúvidas ou riscos não resolvidos
EOF

# ---------------------------------------------------------------------------
# 2. Explorador: leitura e varredura em massa, só leitura (Flash)
# ---------------------------------------------------------------------------
backup "$AGENTS/explorador.md"
cat > "$AGENTS/explorador.md" <<'EOF'
---
name: explorador
description: Varredura somente leitura de código, logs e documentos — localizar arquivos, símbolos, padrões e ocorrências. Devolve conclusões destiladas, não despejos de arquivo.
tools:
  - view_file
  - grep_search
subagent: true
mainAgent: false
model: flash
commandExecutionPolicy: "off"
---
# Explorador

Você localiza e resume; não edita nada.
Devolva: a resposta direta, os caminhos relevantes (arquivo:linha) e trechos de no máximo 10 linhas quando indispensáveis.
Se não encontrar, diga onde procurou.
EOF

# ---------------------------------------------------------------------------
# 3. Revisor: segunda leitura independente de diffs (Pro, só leitura)
# ---------------------------------------------------------------------------
backup "$AGENTS/revisor.md"
cat > "$AGENTS/revisor.md" <<'EOF'
---
name: revisor
description: Revisa um diff ou conjunto de mudanças procurando bugs, regressões, falhas de segurança, testes adulterados e desvios da especificação. Somente leitura.
tools:
  - view_file
  - grep_search
  - run_command
subagent: true
mainAgent: false
model: pro
commandExecutionPolicy: sandbox
---
# Revisor

Revise as mudanças indicadas sem editar nada. Pode rodar testes e comandos de leitura.
Verifique também se testes, fixtures ou configuração foram alterados só para fazer a verificação passar.
Liste apenas achados concretos, do mais grave ao menos grave, cada um com arquivo:linha, cenário de falha e correção sugerida.
Não liste preferências de estilo. Se não houver achados, diga isso em uma linha.
EOF

# ---------------------------------------------------------------------------
# 4. Política de orquestração (bloco delimitado, substituível)
# ---------------------------------------------------------------------------
INICIO="<!-- orquestracao-argon:inicio -->"
FIM="<!-- orquestracao-argon:fim -->"
touch "$REGRAS"
backup "$REGRAS"
if grep -q "$INICIO" "$REGRAS"; then
  awk -v i="$INICIO" -v f="$FIM" '
    $0==i {skip=1; next}
    $0==f {skip=0; next}
    !skip' "$REGRAS" > "$REGRAS.tmp" && cat "$REGRAS.tmp" > "$REGRAS" && rm -f "$REGRAS.tmp"
fi
cat >> "$REGRAS" <<EOF
$INICIO
## Orquestração (quando o modelo principal for Gemini 4 Argon ou Pro)
Em tarefas não triviais, atue como orquestrador: entenda o objetivo, decida a arquitetura,
divida o trabalho, integre os resultados e faça a revisão final. Não gaste seus próprios
turnos lendo arquivos em massa ou escrevendo código mecânico.

Roteamento (via invoke_subagent):
- Localizar, varrer, inventariar (código, logs, documentos) → \`explorador\` (Flash)
- Implementar algo já decidido e bem especificado → \`executor\` (Flash)
- Segunda leitura de um diff antes de concluir → \`revisor\` (Pro)
- Arquitetura, ambiguidade, trade-offs, integração e decisão final → você mesmo

Regras:
- Tarefa pequena (poucos minutos, 1–2 arquivos): faça direto; delegar custa mais que fazer.
- Subagentes não herdam esta conversa: cada delegação leva objetivo, arquivos/caminhos permitidos,
  critério de pronto e como verificar.
- Executores que mexem em arquivos sobrepostos rodam em workspace \`branch\` (worktree isolada).
- Subagentes independentes rodam em paralelo.
- Nunca aceite "pronto" sem conferir: rode você mesmo os testes/checagens críticos antes de concluir.
$FIM
EOF

# ---------------------------------------------------------------------------
# 5. Checagens
# ---------------------------------------------------------------------------
echo
echo "Agentes em: $AGENTS"
echo "Política em: $REGRAS"
echo
if command -v agy >/dev/null 2>&1; then
  echo "Antigravity CLI: $(agy --version 2>/dev/null || echo 'versão não detectada') ($(command -v agy))"
  echo "  (agentes customizados em Markdown exigem a versão 1.1.6 ou mais nova)"
else
  echo "Antigravity CLI (agy) não está no PATH. Instale com:"
  echo "  curl -fsSL https://antigravity.google/cli/install.sh | bash"
fi
echo
echo "Próximos passos dentro do agy:"
echo "  /model   → escolha Gemini 4 Argon (fica salvo entre sessões)"
echo "  /agents  → confira executor, explorador e revisor"
echo "  Se algum subagente travar ao ser chamado, confira os nomes em tools: na lista do /agents."
