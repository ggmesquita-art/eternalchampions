# Hash de patch FM (identidade de instrumento) — contrato A5

Usado por `tools/audio/voice_catalog.py` (extração/catálogo) e por `game/ec_audio.c` (motor em tempo real) para identificar **qual instrumento** um canal YM2612 está tocando no momento do key-on. O canal físico não basta: o driver do jogo reutiliza cada canal FM com vários patches ao longo da música — 86 de 102 canais nos 17 BGMs.

## Entrada

O shadow dos registradores do YM2612 é lido no instante da escrita de key-on (`$28` com bits de operador ≠ 0). A parte é `pr` (0 para FM1–3, 1 para FM4–6) e o canal dentro da parte é `ch` (0–2). Excluem-se o canal 3/7 de `$28` (inválido) e o FM6 quando o DAC está ativo (`$2B` bit 7).

## Sequência canônica de 31 bytes

1. `ALG = reg[pr][$B0+ch] & 7`
2. `FB  = (reg[pr][$B0+ch] >> 3) & 7`
3. `AMSFMS = reg[pr][$B4+ch] & 0x37` (o pan L/R, bits 7–6, fica **fora**)
4. Para cada slot na ordem **S1, S2, S3, S4**, os offsets de registrador são `S1=+0x0`, `S2=+0x8`, `S3=+0x4`, `S4=+0xC` (somados a `ch`). Para cada slot entram 7 bytes, nesta ordem:
   - `$30` DT/MUL
   - `$40` TL — **substituído por `0xFF` se o slot é carrier do algoritmo** (o TL do carrier é volume, não timbre)
   - `$50` RS/AR
   - `$60` AM/D1R
   - `$70` D2R
   - `$80` SL/RR
   - `$90` SSG-EG

Os carriers de cada algoritmo, por índice de slot (S1=0, S2=1, S3=2, S4=3):

| ALG | carriers |
|---|---|
| 0, 1, 2, 3 | S4 |
| 4 | S2, S4 |
| 5, 6 | S2, S3, S4 |
| 7 | S1, S2, S3, S4 |

## Hash

FNV-1a 32 bits sobre os 31 bytes: `h = 0x811C9DC5`; para cada byte, `h ^= b; h *= 0x01000193` (mod 2³²). A representação textual é `%08x`, em minúsculas. Essa é a chave usada em `voices.toml` (`[voices.<hash>]`).

## Vetor de teste

Com todos os registradores zerados exceto `ALG=7, FB=0`, todos os slots são carriers: os bytes ficam `07 00 00` seguidos de 4 × `00 FF 00 00 00 00 00`. Os dois lados (`voice_catalog.py --selftest` e o self-test em C) devem produzir o mesmo valor, registrado em `voice_catalog.py`.

Valor esperado (FNV-1a 32 bits): **`ce6df9ba`** (bytes `07000000ff000000000000ff000000000000ff000000000000ff0000000000`).
