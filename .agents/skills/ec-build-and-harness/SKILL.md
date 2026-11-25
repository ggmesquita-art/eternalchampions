---
name: ec-build-and-harness
description: Build the Eternal Champions native GenesisRecomp binary on Windows (MSVC x64 + Ninja, build.cmd) and run deterministic validation via harness/ec_harness.py. Use whenever compiling C/CMake changes in game/, neural/infer/ or segagenesisrecomp/runner/, or validating determinism, WRAM parity, misses, or widescreen behavior.
---

# Eternal Champions — Build & Harness (Windows nativo)

Repo canônico: `C:\Users\Administrator\Projects\eternalchampions` (Windows). **Todo build e desenvolvimento acontecem aqui.**

> **Clone WSL (`/home/mips/eternalchampions`) é SOMENTE LEITURA.** Pode ser consultado para copiar arquivos de referência que não existem no Windows (binário `gpgx_runner` Linux, core `genesis_plus_gx_libretro.so`, savestates `build/native/native_save_N.bin`), mas **nunca** compile, edite, faça `git` ou rode o harness dentro dele.
> Cópias de referência vão para pastas ignoradas deste repo (`tools/vendor/...`, `build/ref_saves/`) sem sobrescrever arquivos locais.

## 1. Build nativo (`EternalChampionsRecomp.exe`)

- **Script**: `build.cmd` na raiz (chama `vcvars64.bat` do VS 2022 BuildTools, CMake 4.4 + Ninja do WinGet, toolchain vcpkg `x64-windows` para libpng/zlib, SDL2 2.28.5 de `segagenesisrecomp/runner/external/SDL2`).
- **Source CMake**: `game/CMakeLists.txt` (compila `ec_spec.c`, `ec_boss.c`, `ec_video.c`, `ec_hd.c`, `ec_audio.c`, `../neural/infer/ec_bplus_*.c`, fontes geradas de `game/game.toml` e o runner de `segagenesisrecomp/`).
- **Build dir**: `build/` (Ninja, Release).
- **Build incremental**:
  - PowerShell: `.\build.cmd`
  - Git Bash: `cmd.exe //c '.\build.cmd'` (o `.\` é obrigatório)
- **Reconfigurar CMake** (somente ao alterar `game/CMakeLists.txt` ou opções como `-DEC_GEN_DEV_TRACE=ON`):
  - PowerShell: `.\build.cmd reconfigure -DEC_GEN_DEV_TRACE=ON`
- Sucesso = última linha `=== BUILD_OK === build\EternalChampionsRecomp.exe` e exit code 0. O aviso `'vswhere.exe' não é reconhecido` do vcvars é inofensivo.
- Binário: `build/EternalChampionsRecomp.exe` (DLLs `SDL2.dll`, `libpng16.dll`, `libz.dll` ao lado; `onnxruntime.dll` para `--bplus-backend cuda`).
- **Risco conhecido**: o vcpkg está em `%TEMP%\opencode\vcpkg`; se a pasta temporária for limpa, o `reconfigure` falha (o build incremental continua funcionando com as libs já resolvidas no cache).

## 2. Execução manual

```powershell
.\build\EternalChampionsRecomp.exe roms\eternal_champions_us.gen --widescreen fit
```
- `--help` lista todas as flags; guia completo em `docs/CONFIGURATION_AND_CLI.md`.
- Headless (testes/capturas): `$env:SDL_VIDEODRIVER='dummy'; $env:SDL_AUDIODRIVER='dummy'; $env:SDL_RENDER_DRIVER='software'`.
- No Windows o renderer padrão é `direct3d11`; B+/Modo C (GL compute) forçam `opengl` automaticamente.

## 3. Validação determinística (`harness/ec_harness.py`)

Executar após qualquer alteração em `game/`, `game/game.toml`, `neural/infer/` ou no runner. Regra de fechamento: `--quick` a cada iteração; suíte completa `17/17` ao fechar cada etapa, sempre com `wram_diff=0/65536`.

**Windows (etapa `G0.2` do `task.md` — em implantação):**
- Nativo: `build/EternalChampionsRecomp.exe` (Python do Windows, `python`, não `python3`).
- Referência GPGX: o `gpgx_runner` só existe como binário Linux → copiado do clone WSL para `tools/vendor/gpgx_runner/` + core `.so` para `tools/vendor/gpgx/` e executado via `wsl.exe` sobre caminhos `/mnt/c/...` (wrapper no harness). Nada é executado a partir do clone WSL.
- Comando alvo (após G0.2):
  ```powershell
  python harness\ec_harness.py --quick --native build\EternalChampionsRecomp.exe
  python harness\ec_harness.py --native build\EternalChampionsRecomp.exe
  ```
- Até G0.2 concluir, `DEFAULT_NATIVE` do harness ainda aponta para `build/native/EternalChampionsRecomp` (layout WSL) — passe `--native` explicitamente.

O harness verifica automaticamente:
- **Self-determinism** (`0/65536` bytes de WRAM diff + PNG SHA-256 idêntico).
- **State & roster match** vs `MOVIE_SPECS` (filmes em `harness/movies/01..17_*.ecm`).
- **Zero misses** em `build/{floor_unsafe,dispatch_misses,interior_label_misses,floor_coverage}.toml`.
- **Paridade** com `gpgx_runner` / interpretador (`GENESIS_FORCE_INTERP=1`; `--skip-interp` pula) e **widescreen 16:9** (sem afetar WRAM; `inspect_widescreen_margins()`).

Saídas principais:
- Relatório Markdown: `docs/frame_data/fight_probe_summary.md`
- Relatório JSON: `assets/captures/harness_report.json`
- Logs de runtime: `build/logs/` e `last_error.log`

## 4. Python / GPU (treino, upscalers, ferramentas)

- GPU CUDA (RTX 5060 Ti): usar o venv do ComfyUI **sem alterar seus pacotes**: `F:\ComfyUI\ComfyUI\.venv\Scripts\python.exe` (torch `2.13+cu130`, `spandrel 0.4.2`).
- Dependências extras: `pip install --target neural\.pydeps <pkg>` e `$env:PYTHONPATH='neural\.pydeps'` ao rodar.
- O `python` global do Windows (3.14) tem torch **CPU-only** — serve para o harness e scripts sem GPU.
- ComfyUI (API) roda em `http://127.0.0.1:8188`; modelos de upscale em `F:\ComfyUI\ComfyUI\models\upscale_models\`.
