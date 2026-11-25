---
name: ec-disasm-and-hooks
description: Locate 68000 addresses, RAM symbols, and game hooks across disasm/, disasm/symbols/, game/game.toml, and game/ec_*.c without reading large files whole. Use when investigating ROM addresses, adding [[widescreen_site]] game_hooks, or mapping WRAM offsets.
---

# Eternal Champions — Disasm, Símbolos & Hooks

Repo canônico: `C:\Users\Administrator\Projects\eternalchampions` (Windows). Busque sempre nesta pasta; o clone WSL (`/home/mips/eternalchampions`) é somente leitura e só deve ser consultado se um arquivo de referência não existir aqui.

Regras para manter precisão e economizar tokens ao investigar endereços 68k e hooks:

## 0. Grafo primeiro
Com `graphify-out/graph.json` presente, rode `graphify query "<pergunta>"` / `graphify explain "<símbolo>"` antes de grep ou leitura de fonte.

## 1. Nunca leia arquivos grandes inteiros
`disasm/main.asm`, `disasm/symbols/labels.toml`, `disasm/symbols/ram_map.toml`, `game/ec_code_addrs.txt` e `game/ec_discovery.toml` são extensos. **Sempre localize primeiro** e leia apenas o trecho necessário (Read com `offset`/`limit`).

- Ferramenta preferida: **Grep** do agente (ripgrep), ex.: padrão `(?i)30F1E`, `glob` `{disasm/symbols/*.toml,game/game.toml,game/ec_*.c}`.
- Git Bash (no Windows, `rg` não está no PATH; use `grep`):
  ```bash
  grep -n -i "<hex_ou_simbolo>" disasm/symbols/labels.toml disasm/symbols/ram_map.toml game/game.toml game/ec_*.c
  ```
- PowerShell:
  ```powershell
  Select-String -Pattern '<hex_ou_simbolo>' -Path disasm\symbols\labels.toml,disasm\symbols\ram_map.toml,game\game.toml,game\ec_*.c
  ```
- Não use `wsl -e ...` para buscar: os arquivos estão neste repo Windows.

## 2. Onde vive cada informação
- **Labels de código/dados ROM**: `disasm/symbols/labels.toml` (`[[label]]` com `address = 0xXXXXXX`, `name`, `type`, `conf = "confirmed" | "hypothesis"`).
- **Mapa de WRAM (`0xFF0000–0xFFFFFF`)**: `disasm/symbols/ram_map.toml` (`[[ram]]` com `address = 0xFFFFxxxx`, `size`, `type`, `name`). Lembre que dumps de WRAM do GPGX podem ter byte-swap (`ram[addr ^ 1]`).
- **Configuração do recompilador e hooks**: `game/game.toml`
  - `[functions]`: `extra`, `late_extra`, `blacklist`.
  - `[[widescreen_site]]` com `addr = 0xXXXXXX` e `kind = "game_hook"`.
- **Implementação dos hooks em C**: `game/ec_video.c`, `game/ec_boss.c`, `game/ec_spec.c`, `game/ec_hd.c`, `game/ec_audio.c` (e headers `.h` correspondentes).
- Após alterar `game/game.toml` ou hooks: rebuild com `.\build.cmd` (ver skill `ec-build-and-harness`).

## 3. Convenção de busca de endereços
Endereços 68k aparecem em formatos diferentes conforme o arquivo (`0x030F1E`, `0x30F1E`, `$30F1E`, `sub_030F1E`, `030F1E`). Busque pelo sufixo sem zeros à esquerda (ex.: `30F1E`) com `-i` para não perder ocorrências.

## 4. Fins de linha
Arquivos podem ter CRLF no Windows; ao comparar com cópias do WSL use `tr -d '\r'` (Git Bash) antes de `diff`.
