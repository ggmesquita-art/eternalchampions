#!/usr/bin/env bash
# (compativel com o bash 3.2 do macOS: arrays vazios expandidos com ${a[@]+...})
# Build nativo Linux/macOS do EternalChampionsRecomp (+ launcher) em build/.
#   ./build.sh                 -> build incremental em build/
#   ./build.sh reconfigure     -> reconfigura o CMake (apos mudar game/CMakeLists.txt ou opcoes) e builda
#   ./build.sh reconfigure -DEC_GEN_DEV_TRACE=ON   -> argumentos extras vao para o configure
#   ./build.sh --no-package    -> so compila, sem gerar o pacote
# Depois do build gera o pacote da plataforma em packaging/out/ (macOS: .zip com o .app;
# Linux: .tar.gz e, se houver dpkg-deb + fakeroot, .deb). A versao vem do `git describe`
# (tag v1.0.1 -> 1.0.1) ou da variavel VERSION.
# Variaveis: BUILD_DIR (padrao: build), BUILD_TYPE (padrao: Release), JOBS (padrao: n. de CPUs),
#            VERSION, NO_PACKAGE=1.
# Diferente do build.cmd, este script NAO abre o jogo ao terminar.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
cd "$ROOT"

# --no-package pode vir em qualquer posicao; o resto dos argumentos segue como antes
NO_PACKAGE="${NO_PACKAGE:-}"
ARGS=()
for a in "$@"; do
    if [[ "$a" == "--no-package" ]]; then NO_PACKAGE=1; else ARGS+=("$a"); fi
done
set -- ${ARGS[@]+"${ARGS[@]}"}

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

[[ -n "$NO_PACKAGE" ]] && exit 0

# ---- pacote ----------------------------------------------------------------------------------
if [[ -z "${VERSION:-}" ]]; then
    VERSION="$(git -c safe.directory='*' -C "$ROOT" describe --tags --always --dirty 2>/dev/null || true)"
    VERSION="${VERSION#v}"
    # sem tag (so o hash) ou sem git: versao de desenvolvimento
    [[ "$VERSION" =~ ^[0-9] ]] || VERSION="0.0.0-dev${VERSION:+-$VERSION}"
fi
ABS_BUILD_DIR="$BUILD_DIR"
[[ "$ABS_BUILD_DIR" == /* ]] || ABS_BUILD_DIR="$ROOT/$BUILD_DIR"
echo "=== PACKAGE === versao $VERSION"
case "$(uname -s)" in
    Darwin) PKG="$ROOT/packaging/macos/package.sh" ;;
    Linux)  PKG="$ROOT/packaging/linux/package.sh" ;;
    *)      echo "[build.sh] sem pacote para $(uname -s)"; exit 0 ;;
esac
if VERSION="$VERSION" BUILD_DIR="$ABS_BUILD_DIR" bash "$PKG"; then
    echo "=== PACKAGE_OK === packaging/out/"
else
    # o build ja esta pronto; falha no pacote nao invalida os executaveis
    echo "[build.sh] aviso: o empacotamento falhou (o build em $BUILD_DIR/ esta ok). Use --no-package para pular." >&2
fi
