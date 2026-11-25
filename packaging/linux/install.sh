#!/bin/sh
# Instala o Eternal Champions Recomp para o usuario atual (sem root).
#   ./install.sh                 instala em ~/.local/share/eternalchampions-recomp
#   ./install.sh --rom ARQUIVO   tambem copia a ROM (US) e ja deixa configurada
#   ./install.sh --uninstall     remove os arquivos instalados (mantem saves/configuracoes em app/)
# Dependencias: SDL2 e libpng (Debian/Ubuntu: sudo apt install libsdl2-2.0-0 libpng16-16t64 zenity).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}/eternalchampions-recomp"
BIN="$HOME/.local/bin"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ROM_SHA256=6d8bdaf8c994d9f718c8f991900c58cdf1af84e030524b57f6dda263cc4ea247

if [ "$1" = "--uninstall" ]; then
    rm -rf "$DATA/dist"
    rm -f "$BIN/eternalchampions-recomp" "$APPS/eternalchampions-recomp.desktop"
    echo "Removido. Saves e configuracoes continuam em $DATA/app (apague a pasta se quiser)."
    exit 0
fi

ROM=""
if [ "$1" = "--rom" ]; then ROM="$2"; fi

mkdir -p "$DATA/dist" "$BIN" "$APPS"
cp -R "$HERE/payload/." "$DATA/dist/"
sed "s|@PAYLOAD@|$DATA/dist|" "$HERE/eternalchampions-recomp" > "$BIN/eternalchampions-recomp"
chmod +x "$BIN/eternalchampions-recomp"
sed "s|@EXEC@|$BIN/eternalchampions-recomp|" "$HERE/eternalchampions-recomp.desktop" > "$APPS/eternalchampions-recomp.desktop"

if [ -n "$ROM" ]; then
    [ -f "$ROM" ] || { echo "ROM nao encontrada: $ROM" >&2; exit 1; }
    SUM="$(sha256sum "$ROM" | cut -d' ' -f1)"
    [ "$SUM" = "$ROM_SHA256" ] || echo "Aviso: o SHA-256 da ROM nao confere com a versao US suportada." >&2
    mkdir -p "$DATA/app/roms"
    cp "$ROM" "$DATA/app/roms/eternal_champions_us.gen"
    printf '%s\n' "$DATA/app/roms/eternal_champions_us.gen" > "$DATA/app/rom-EC.cfg"
fi

echo "Instalado. Abra \"Eternal Champions Recomp\" no menu de aplicativos ou rode: $BIN/eternalchampions-recomp"
for lib in libSDL2-2.0.so.0 libpng16.so.16; do
    ldconfig -p 2>/dev/null | grep -q "$lib" || echo "Aviso: $lib nao encontrada; instale SDL2 e libpng pelo gerenciador de pacotes." >&2
done
