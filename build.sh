#!/usr/bin/env bash
# (compativel com o bash 3.2 do macOS: arrays vazios expandidos com ${a[@]+...})
# Build nativo Linux/macOS do EternalChampionsRecomp (+ launcher) em build/.
#   ./build.sh                 -> build incremental em build/
#   ./build.sh reconfigure     -> reconfigura o CMake (apos mudar game/CMakeLists.txt ou opcoes) e builda
#   ./build.sh reconfigure -DEC_GEN_DEV_TRACE=ON   -> argumentos extras vao para o configure
# Variaveis: BUILD_DIR (padrao: build), BUILD_TYPE (padrao: Release), JOBS (padrao: n. de CPUs).
# Diferente do build.cmd, este script NAO abre o jogo ao terminar.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
cd "$ROOT"

if [[ ! -f roms/eternal_champions_us.gen ]]; then
    echo "[build.sh] ROM ausente: roms/eternal_champions_us.gen (veja README.md, secao ROM)" >&2
    exit 1
fi
if [[ ! -f segagenesisrecomp/cmake/GenesisRecompGenerated.cmake \
   || ! -f segagenesisrecomp/external/m68k-recomp-core/common/m68k_timing.c \
   || ! -f segagenesisrecomp/external/rbengine/CMakeLists.txt ]]; then
    echo "[build.sh] segagenesisrecomp/ ou os submodulos dele estao ausentes." >&2
    echo "           Rode: git submodule update --init --recursive   (veja README.md, secao Dependencias)" >&2
    exit 1
fi

if [[ -z "${JOBS:-}" ]]; then
    JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
fi

GEN_ARGS=()
command -v ninja >/dev/null 2>&1 && GEN_ARGS=(-G Ninja)

# Homebrew (macOS): SDL2/libpng ficam fora dos caminhos padrao do CMake em Apple Silicon.
PREFIX_ARGS=()
if [[ "$(uname -s)" == "Darwin" ]] && command -v brew >/dev/null 2>&1; then
    PREFIX_ARGS=(-DCMAKE_PREFIX_PATH="$(brew --prefix)")
fi

DO_CONFIGURE=""
[[ -f "$BUILD_DIR/CMakeCache.txt" ]] || DO_CONFIGURE=1
if [[ "${1:-}" == "reconfigure" ]]; then DO_CONFIGURE=1; shift; fi

if [[ -n "$DO_CONFIGURE" ]]; then
    echo "=== CONFIGURE ==="
    cmake -S game -B "$BUILD_DIR" ${GEN_ARGS[@]+"${GEN_ARGS[@]}"} -DCMAKE_BUILD_TYPE="$BUILD_TYPE" ${PREFIX_ARGS[@]+"${PREFIX_ARGS[@]}"} "$@"
fi
echo "=== BUILD ==="
cmake --build "$BUILD_DIR" -j "$JOBS"
echo "=== BUILD_OK === $BUILD_DIR/EternalChampionsRecomp  $BUILD_DIR/EternalChampions"
