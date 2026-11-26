#!/usr/bin/env bash
# Gera os pacotes Linux a partir de um build pronto:
#   packaging/out/EternalChampionsRecomp-<versao>-linux-x86_64.tar.gz   (install.sh, por usuario)
#   packaging/out/eternalchampions-recomp_<versao>_amd64.deb             (Debian/Ubuntu)
# Uso: VERSION=1.0.0 BUILD_DIR=build packaging/linux/package.sh
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
VERSION="${VERSION:?defina VERSION, p.ex. VERSION=1.0.0}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
OUT="$ROOT/packaging/out"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
git_() { git -c safe.directory='*' -C "$ROOT" "$@"; }

for f in EternalChampions EternalChampionsRecomp; do
    [[ -x "$BUILD_DIR/$f" ]] || { echo "faltando $BUILD_DIR/$f (rode ./build.sh antes)" >&2; exit 1; }
done

# ---- payload comum ---------------------------------------------------------------------------
P="$WORK/payload"
mkdir -p "$P/assets/audio_remaster" "$P/tools/launcher" "$P/roms" "$P/docs" "$P/licenses"
install -m755 "$BUILD_DIR/EternalChampions" "$BUILD_DIR/EternalChampionsRecomp" "$P/"
strip "$P/EternalChampions" "$P/EternalChampionsRecomp"
cp -R "$ROOT/assets/shaders" "$P/assets/shaders"
git_ ls-files -- 'assets/audio_remaster/*.toml' | while read -r f; do
    rel="${f#assets/audio_remaster/}"
    mkdir -p "$P/assets/audio_remaster/$(dirname "$rel")"
    cp "$ROOT/$f" "$P/assets/audio_remaster/$rel"
done
cp "$ROOT/tools/launcher/extract_ui_assets.py" "$P/tools/launcher/"
# Modo C (HD): manifest AnimeSharp + arte aprovada e originais versionados (o Flux nao e distribuido)
mkdir -p "$P/pipeline"
cp "$ROOT/pipeline/manifest.json" "$P/pipeline/"
git -c safe.directory='*' -C "$ROOT" ls-files -z -- assets/hd assets/catalog | (cd "$ROOT" && tar --null -T - -cf -) | tar -C "$P" -xf -
# modelos do upscale B+ (so os de runtime: .bin/.onnx; sem checkpoints .pt) e SoundFont
mkdir -p "$P/assets/models"
find "$ROOT/assets/models" -maxdepth 1 -type f \( -name '*.bin' -o -name '*.onnx' \) -exec cp {} "$P/assets/models/" \;
cp -R "$ROOT/assets/soundfonts" "$P/assets/soundfonts"
cp "$ROOT/README.md" "$ROOT/LICENSE.md" "$ROOT/packaging/THIRD_PARTY_NOTICES.txt" "$P/"
cp "$ROOT/docs/LAUNCHER.md" "$ROOT/docs/CONFIGURATION_AND_CLI.md" "$P/docs/"
for f in LICENSE.md PERMISSIONS.md THIRD-PARTY-LICENSES.md; do
    cp "$ROOT/segagenesisrecomp/$f" "$P/licenses/segagenesisrecomp-$f"
done
printf '%s\n' "$VERSION" > "$P/VERSION"
# arquivos vindos de /mnt/c (WSL) chegam com modo 777: normaliza
chmod -R u=rwX,go=rX "$P"
chmod 755 "$P/EternalChampions" "$P/EternalChampionsRecomp"
mkdir -p "$OUT"

# ---- .tar.gz ---------------------------------------------------------------------------------
T="$WORK/EternalChampionsRecomp-$VERSION-linux-x86_64"
mkdir -p "$T"
cp -R "$P" "$T/payload"
install -m755 "$HERE/install.sh" "$T/install.sh"
install -m755 "$HERE/eternalchampions-recomp" "$T/eternalchampions-recomp"
install -m644 "$HERE/eternalchampions-recomp.desktop" "$T/eternalchampions-recomp.desktop"
tar -C "$WORK" --owner=0 --group=0 -czf "$OUT/$(basename "$T").tar.gz" "$(basename "$T")"

# ---- .deb ------------------------------------------------------------------------------------
if ! command -v dpkg-deb >/dev/null 2>&1 || ! command -v fakeroot >/dev/null 2>&1; then
    echo "dpkg-deb/fakeroot ausentes: .deb nao gerado (so o .tar.gz)"
    ls -la "$OUT"/*"$VERSION"*
    exit 0
fi
D="$WORK/deb"
mkdir -p "$D/DEBIAN" "$D/opt" "$D/usr/bin" "$D/usr/share/applications" "$D/usr/share/doc/eternalchampions-recomp"
cp -R "$P" "$D/opt/eternalchampions-recomp"
sed "s|@PAYLOAD@|/opt/eternalchampions-recomp|" "$HERE/eternalchampions-recomp" > "$D/usr/bin/eternalchampions-recomp"
chmod 755 "$D/usr/bin/eternalchampions-recomp"
sed "s|@EXEC@|eternalchampions-recomp|" "$HERE/eternalchampions-recomp.desktop" > "$D/usr/share/applications/eternalchampions-recomp.desktop"
cp "$ROOT/packaging/THIRD_PARTY_NOTICES.txt" "$D/usr/share/doc/eternalchampions-recomp/copyright"
SIZE_KB="$(du -sk "$D" | cut -f1)"
cat > "$D/DEBIAN/control" <<EOF
Package: eternalchampions-recomp
Version: $VERSION
Architecture: amd64
Maintainer: ggmesquita-art <ggmesquita-art@users.noreply.github.com>
Installed-Size: $SIZE_KB
Depends: libc6 (>= 2.39), libstdc++6, libgcc-s1, libsdl2-2.0-0, libpng16-16t64 | libpng16-16, zlib1g
Recommends: zenity | kdialog, fonts-dejavu-core
Suggests: python3, python3-numpy, python3-pil
Section: games
Priority: optional
Homepage: https://github.com/ggmesquita-art/eternalchampions
Description: Eternal Champions (Mega Drive) recompilado para PC nativo
 Recompilacao estatica de Eternal Champions com widescreen, upscale neural,
 shaders, remaster de audio e launcher grafico. Requer um dump proprio da
 ROM US do jogo (nao incluida).
EOF
find "$D" -type d -exec chmod 755 {} +
fakeroot dpkg-deb --build -Zxz "$D" "$OUT/eternalchampions-recomp_${VERSION}_amd64.deb" >/dev/null

ls -la "$OUT"/*"$VERSION"*
