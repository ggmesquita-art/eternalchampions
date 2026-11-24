---
name: ec-disasm-and-hooks
description: Locate 68000 addresses, RAM symbols, and game hooks across disasm/, disasm/symbols/, game/game.toml, and game/ec_*.c without reading large files whole. Use when investigating ROM addresses, adding [[widescreen_site]] game_hooks, or mapping WRAM offsets.
---

# Eternal Champions — Disasm, Símbolos & Hooks

Regras para manter precisão e economizar tokens ao investigar endereços 68k e hooks:

## 1. Nunca leia arquivos grandes inteiros
`disasm/main.asm`, `disasm/symbols/labels.toml`, `disasm/symbols/ram_map.toml`, `game/ec_code_addrs.txt` e `game/ec_discovery.toml` são extensos. **Sempre localize primeiro com `grep -n` via WSL** e leia apenas o trecho necessário.

```bash
wsl -e bash -lc 'cd /home/mips/eternalchampions && grep -n -i "<hex_ou_simbolo>" disasm/symbols/labels.toml disasm/symbols/ram_map.toml game/game.toml game/ec_*.c'
```

## 2. Onde vive cada informação
- **Labels de código/dados ROM**: `disasm/symbols/labels.toml` (`[[label]]` com `address = 0xXXXXXX`, `name`, `type`, `conf = "confirmed" | "hypothesis"`).
- **Mapa de WRAM (`0xFF0000–0xFFFFFF`)**: `disasm/symbols/ram_map.toml` (`[[ram]]` com `address = 0xFFFFxxxx`, `size`, `type`, `name`). Lembre que dumps de WRAM do GPGX podem ter byte-swap (`ram[addr ^ 1]`).
- **Configuração do recompilador e hooks**: `game/game.toml`
  - `[functions]`: `extra`, `late_extra`, `blacklist`.
  - `[[widescreen_site]]` com `addr = 0xXXXXXX` e `kind = "game_hook"`.
- **Implementação dos hooks em C**: `game/ec_video.c`, `game/ec_boss.c`, `game/ec_spec.c` (e headers `.h` correspondentes).

## 3. Convenção de busca de endereços
Endereços 68k aparecem em formatos diferentes conforme o arquivo (`0x030F1E`, `0x30F1E`, `$30F1E`, `sub_030F1E`, `030F1E`). Busque pelo sufixo sem zeros à esquerda (ex.: `30F1E`) com `-i` para não perder ocorrências.
