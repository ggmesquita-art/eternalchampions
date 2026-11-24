---
name: ec-build-and-harness
description: Build the Eternal Champions native GenesisRecomp binary in WSL and run deterministic validation via harness/ec_harness.py. Use whenever compiling C/CMake changes in game/ or validating determinism, WRAM parity, misses, or widescreen behavior.
---

# Eternal Champions — Build & Harness (WSL)

Always execute build and test commands inside WSL (repo root: `/home/mips/eternalchampions`), never directly in Windows PowerShell.

## 1. Build nativo (`EternalChampionsRecomp`)

- **Source CMake**: `game/CMakeLists.txt` (compila `ec_spec.c`, `ec_boss.c`, `ec_video.c`, `../neural/infer/ec_bplus_*.c`, fontes geradas de `game/game.toml` e `segagenesisrecomp` runner).
- **Build dir**: `build/native/` (já configurado com CMake/Make).
- **Recompilação incremental**:
  ```bash
  wsl -e bash -lc 'cd /home/mips/eternalchampions && cmake --build build/native -j$(nproc)'
  ```
- **Reconfigurar CMake** (somente se alterar `game/CMakeLists.txt` ou opções como `-DEC_GEN_DEV_TRACE=ON`):
  ```bash
  wsl -e bash -lc 'cd /home/mips/eternalchampions && cmake -S game -B build/native && cmake --build build/native -j$(nproc)'
  ```
- Binário gerado: `build/native/EternalChampionsRecomp`.

## 2. Validação determinística (`harness/ec_harness.py`)

Execute sempre após alterações em `game/` ou `game/game.toml`:
```bash
wsl -e bash -lc 'cd /home/mips/eternalchampions && python3 harness/ec_harness.py'
```

O harness verifica automaticamente:
- **Self-determinism** (`0/65536` bytes de WRAM diff + PNG SHA-256 idêntico).
- **State & roster match** vs `MOVIE_SPECS`.
- **Zero misses** em `build/native/{floor_unsafe,dispatch_misses,interior_label_misses,floor_coverage}.toml`.
- **Paridade** com `gpgx_runner` / interpretador e **widescreen 16:9** (sem afetar WRAM).

Saídas principais:
- Relatório Markdown: `docs/frame_data/fight_probe_summary.md`
- Relatório JSON: `assets/captures/harness_report.json`
- Logs de runtime: `build/native/logs/` e `last_error.log`
