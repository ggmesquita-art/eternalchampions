R"HDR(# voices.toml — catálogo de instrumentos FM do Eternal Champions (A5).
# Gerado por tools/audio/voice_catalog.py; hash = contrato em docs/audio_voice_hash.md.
#
# Campos EDITÁVEIS (o motor só lê estes; reexecutar o script os preserva):
#   name       rótulo livre
#   mode       gm | percussion | chip | off
#   program    programa GM 0-127 (sugestão automática = ponto de partida, não verdade)
#   bank       banco do SoundFont
#   transpose  semitons somados à nota
#   volume_db  ganho em dB
#   drum_note  so p/ mode=percussion: nota GM fixa (ex. 42 = closed hi-hat; 0 = usar a nota da
#              propria trilha). Em percussao GM o program e ignorado: a nota escolhe o instrumento.
# A subtabela [voices.<hash>.meta] é REGERADA a cada execução (não edite).
# Vozes ausentes dos dumps atuais são mantidas como estão.
)HDR"