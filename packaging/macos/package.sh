#!/usr/bin/env bash
# Gera packaging/out/EternalChampionsRecomp-<versao>-macos-<arch>.zip com "Eternal Champions Recomp.app".
# Rode no Mac, depois do ./build.sh:
#   VERSION=1.0.1 packaging/macos/package.sh
# Precisa so das ferramentas do Xcode Command Line Tools (otool, install_name_tool, codesign, ditto).
#
# As dylibs que nao sao do sistema (SDL2 e libpng do Homebrew) sao copiadas para payload/libs e os
# binarios passam a apontar para @executable_path/libs; tudo e reassinado ad-hoc (obrigatorio em
# Apple Silicon depois do install_name_tool). Compativel com o bash 3.2 do macOS.
set -euo pipefail
shopt -s nullglob
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
VERSION="${VERSION:?defina VERSION, p.ex. VERSION=1.0.1}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
ARCH="$(uname -m)"   # arm64 | x86_64
OUT="$ROOT/packaging/out"
BREW_PREFIX="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

[[ "$(uname -s)" == "Darwin" ]] || { echo "rode no macOS" >&2; exit 1; }
for f in EternalChampions EternalChampionsRecomp; do
    [[ -x "$BUILD_DIR/$f" ]] || { echo "faltando $BUILD_DIR/$f (rode ./build.sh antes)" >&2; exit 1; }
done

APPNAME="Eternal Champions Recomp"
BUNDLE="$WORK/$APPNAME.app"
P="$BUNDLE/Contents/Resources/payload"
mkdir -p "$BUNDLE/Contents/MacOS" "$P/libs" "$P/assets/audio_remaster" "$P/assets/models" \
         "$P/tools/launcher" "$P/roms" "$P/docs" "$P/licenses"

# ---- payload (mesmo conteudo dos pacotes de Linux) -------------------------------------------
cp "$BUILD_DIR/EternalChampions" "$BUILD_DIR/EternalChampionsRecomp" "$P/"
chmod 755 "$P/EternalChampions" "$P/EternalChampionsRecomp"
cp -R "$ROOT/assets/shaders" "$P/assets/shaders"
git -C "$ROOT" ls-files -- 'assets/audio_remaster/*.toml' | while read -r f; do
    rel="${f#assets/audio_remaster/}"
    mkdir -p "$P/assets/audio_remaster/$(dirname "$rel")"
    cp "$ROOT/$f" "$P/assets/audio_remaster/$rel"
done
find "$ROOT/assets/models" -maxdepth 1 -type f \( -name '*.bin' -o -name '*.onnx' \) -exec cp {} "$P/assets/models/" \;
cp -R "$ROOT/assets/soundfonts" "$P/assets/soundfonts"
cp "$ROOT/tools/launcher/extract_ui_assets.py" "$P/tools/launcher/"
cp "$ROOT/README.md" "$ROOT/LICENSE.md" "$ROOT/packaging/THIRD_PARTY_NOTICES.txt" "$P/"
cp "$ROOT/docs/LAUNCHER.md" "$ROOT/docs/CONFIGURATION_AND_CLI.md" "$P/docs/"
for f in LICENSE.md PERMISSIONS.md THIRD-PARTY-LICENSES.md; do
    cp "$ROOT/segagenesisrecomp/$f" "$P/licenses/segagenesisrecomp-$f"
done
printf '%s\n' "$VERSION" > "$P/VERSION"

# ---- dylibs ----------------------------------------------------------------------------------
is_system() { case "$1" in /usr/lib/*|/System/*) return 0 ;; esac; return 1; }

# Caminho real de uma dependencia (absoluta, @rpath/ ou @loader_path/), procurando tambem no Homebrew.
resolve() {
    local dep="$1" from="$2" base cand r
    base="${dep##*/}"
    case "$dep" in
        /*) [[ -f "$dep" ]] && { echo "$dep"; return 0; } ;;
        @loader_path/*) cand="$(dirname "$from")/${dep#@loader_path/}"; [[ -f "$cand" ]] && { echo "$cand"; return 0; } ;;
        @rpath/*)
            for r in $(otool -l "$from" | awk '$1=="cmd" && $2=="LC_RPATH" {getline; getline; print $2}'); do
                r="${r/@loader_path/$(dirname "$from")}"
                cand="$r/${dep#@rpath/}"
                [[ -f "$cand" ]] && { echo "$cand"; return 0; }
            done ;;
    esac
    for cand in "$BREW_PREFIX/lib/$base" /usr/local/lib/"$base"; do
        [[ -f "$cand" ]] && { echo "$cand"; return 0; }
    done
    return 1
}

printf '%s\n' "$P/EternalChampions" "$P/EternalChampionsRecomp" > "$WORK/queue"
: > "$WORK/seen"
i=1
while :; do
    f="$(sed -n "${i}p" "$WORK/queue")"
    [[ -n "$f" ]] || break
    i=$((i + 1))
    self=""
    [[ "$f" == *.dylib ]] && self="$(otool -D "$f" | sed -n 2p)"
    for dep in $(otool -L "$f" | tail -n +2 | awk '{print $1}'); do
        [[ "$dep" == "$self" ]] && continue
        is_system "$dep" && continue
        case "$dep" in @executable_path/*) continue ;; esac
        src="$(resolve "$dep" "$f")" || { echo "dependencia nao encontrada: $dep (de $f)" >&2; exit 1; }
        name="${dep##*/}"
        if ! grep -qxF "$name" "$WORK/seen"; then
            echo "$name" >> "$WORK/seen"
            cp -L "$src" "$P/libs/$name"
            chmod 755 "$P/libs/$name"
            install_name_tool -id "@executable_path/libs/$name" "$P/libs/$name"
            echo "$P/libs/$name" >> "$WORK/queue"
            echo "  + libs/$name  ($src)"
        fi
        install_name_tool -change "$dep" "@executable_path/libs/$name" "$f"
    done
done

for f in "$P"/libs/*.dylib "$P/EternalChampions" "$P/EternalChampionsRecomp"; do
    codesign --force --sign - "$f" 2>/dev/null
done

# nada pode continuar apontando para o Homebrew
if otool -L "$P/EternalChampions" "$P/EternalChampionsRecomp" "$P"/libs/*.dylib | grep -E "$BREW_PREFIX|/usr/local/" ; then
    echo "ainda ha dependencias fora do pacote (acima)" >&2; exit 1
fi

# ---- .app ------------------------------------------------------------------------------------
install -m755 "$HERE/launcher.sh" "$BUNDLE/Contents/MacOS/eternalchampions-recomp"
MINOS="$(otool -l "$P/EternalChampionsRecomp" | awk '$1=="minos" {print $2; exit}')"
sed -e "s|@VERSION@|$VERSION|g" -e "s|@MINOS@|${MINOS:-11.0}|g" "$HERE/Info.plist" > "$BUNDLE/Contents/Info.plist"

mkdir -p "$OUT"
ZIP="$OUT/EternalChampionsRecomp-$VERSION-macos-$ARCH.zip"
rm -f "$ZIP"
ditto -c -k --sequesterRsrc --keepParent "$BUNDLE" "$ZIP"
echo "pacote: $ZIP  (macOS minimo: ${MINOS:-?}, arquitetura: $ARCH)"
