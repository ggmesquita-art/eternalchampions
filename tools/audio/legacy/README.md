# tools/audio/legacy

Scripts das etapas A1/A2 do remaster de áudio (render offline de faixas e
exportação de stems). Dependem da pasta `stems/`, que foi removida, e **não
funcionam mais** sem ela. Mantidos apenas como referência histórica.

O fluxo atual é a síntese em tempo real do motor host (`game/ec_audio.c`) com
perfis em `assets/audio_remaster/profiles/`.

- `remaster_track.py` — A1/A2: remasteriza uma faixa a partir dos stems.
- `export_stems.py` — A1/A2: exporta os stems por canal.
