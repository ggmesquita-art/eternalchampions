#!/bin/sh
# Executavel do "Eternal Champions Recomp.app" (Contents/MacOS).
# O jogo e o launcher gravam settings.ini, rom-EC.cfg e saves ao lado dos executaveis; dentro do .app
# (em /Applications ou na quarentena do Gatekeeper) isso nao e gravavel. Por isso o pacote em
# Contents/Resources/payload e copiado para ~/Library/Application Support na primeira execucao e a cada
# versao nova; os arquivos do usuario nessa pasta nao sao apagados.
set -e
RES="$(cd "$(dirname "$0")/../Resources/payload" && pwd)"
DATA="$HOME/Library/Application Support/EternalChampionsRecomp"
APP="$DATA/app"
mkdir -p "$APP"
if ! cmp -s "$RES/VERSION" "$APP/VERSION" 2>/dev/null; then
    cp -R "$RES/." "$APP/"
    # o usuario ja autorizou abrir este app; a copia herda a quarentena e os binarios internos seriam
    # bloqueados pelo Gatekeeper, entao ela e removida so da copia
    xattr -dr com.apple.quarantine "$APP" 2>/dev/null || true
fi
cd "$APP"
# argumentos do Finder (-psn_...) sao ignorados
exec "$APP/EternalChampions"
