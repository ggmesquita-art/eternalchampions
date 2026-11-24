# Arquitetura de Áudio e Síntese SoundFont em Tempo Real — Eternal Champions

Este documento descreve a arquitetura verificada do subsistema de som de *Eternal Champions* (Mega Drive / Genesis), a causa-raiz das distorções de transcrição no decodificador legado, o catálogo de perfis por canal (`assets/audio_remaster/profiles/*.toml`) editável via `tools/audio/mixer_studio.py` e o motor definitivo de **síntese SoundFont em tempo real** no runtime C (`game/ec_audio.{h,c}` + `game/third_party/tsf.h`).

> **Nota arquitetural:** o jogo **não** consome arquivos `.wav` pré-renderizados nem `manifest.toml`. Toda a remasterização musical acontece **ao vivo em tempo real** a partir de `assets/soundfonts/TimGM6mb.sf2` + perfis `.toml` por `bgm_id`, dirigidos diretamente pelas escritas nos registradores dos chips via `audio_mixer_set_chip_observer`. O estúdio `tools/audio/mixer_studio.py` opera **100% sobre os 17 arquivos MIDI em `assets/audio_remaster/midi/*.mid` + perfis `.toml`**, sintetizando cada canal sob demanda **em memória RAM** via `tools/audio/tsf_midi_synth.dll` (`tsf.h`) — sem FluidSynth/WSL e com **zero arquivos `.wav` gravados em disco**.

---

## 1. Subsistema de Som Original (Hardware + Driver Z80)

### 1.1 Chips de Áudio e Canais Físicos

O jogo utiliza os dois chips padrão do Mega Drive (NTSC):

| Chip | Clock NTSC | Canais Físicos | Registradores Principais |
|---|---|---|---|
| **YM2612 (OPN2)** | `7.670.454 Hz` | `fm0`, `fm1`, `fm2` (Part 0) e `fm3`, `fm4`, `fm5` (Part 1) | `0xA4..0xA6` (MSB + Block), `0xA0..0xA2` (LSB), `0xB0..0xB2` (Algoritmo/Feedback), `0x40..0x4E` (Total Level dos 4 operadores, ignorando offset `reg & 3 == 3`), `0x28` (Key On/Off em Part 0: `val & 0x07` seleciona `{0,1,2,4,5,6}` e `val & 0xF0` define a máscara de operadores). |
| **YM2612 DAC** | Compartilha `fm5` | `dac` (PCM 8-bit) | `0x2B` bit 7 habilita o modo DAC no canal 6 (`fm5`); `0x2A` recebe o stream de amostras PCM (percussão sampleada e vozes). |
| **SN76489 (PSG)** | `3.579.545 Hz` | `psg0`, `psg1`, `psg2` (onda quadrada) + `psg_noise` (LFSR) | Protocolo latch/data em porta única: byte com bit 7 = `1` (`1 CC T DDDD`) faz latch do canal `CC` (`0..2` tom, `3` ruído) e tipo `T` (`0` período/modo, `1` atenuação `0..15`); byte com bit 7 = `0` (`0 X DDDDDD`) envia os 6 bits superiores do período de 10 bits para o canal latched. |

### 1.2 Driver Z80 (`$000A80`)

No boot, `Z80_Upload` (`disasm/code/irq.asm:98`) copia `0x1886` bytes a partir de `Z80_Program` (`$000A80`, região `$000A80–$00222F` em `disasm/main.asm:27-28`) para a RAM do Z80 (`$00A00000`) e zera o restante até `$00A02000`. O driver opera com protocolo estilo **GEMS**:

- **Sincronização de quadro:** o 68000 realiza handshake por frame em `Z80_CommFlag` (`$00A01A60`) e lê `Z80_CommAck` (`$00A01A61`), protegendo o barramento com `Z80_BusAcquire`/`Z80_BusRelease` (`$00A11100`) e contador de reentrância `Z80_BusLock` (`$FFFFFD6A`).
- **Ring buffer de comandos:** fila circular de 64 bytes em `$00A01A80` indexada por `Z80_RingPos` (`$00A00036`, máscara `$3F`), escrita via `Z80_RingRead` (`$0007E4`), `Z80_RingWrite` (`$000822`) e `Z80_RingEpilogue` (`$000810`).
- **Tabela de status:** `Z80_SoundStatus` (`$0009FC`, `disasm/code/sound_cmd.asm:155`) lê `$00A01A62[idx]` diretamente na RAM do Z80.

### 1.3 Interface 68000 → Z80 (`disasm/code/sound_cmd.asm`)

Endereços ROM verificados diretamente em `eternal_champions_us.gen` e `disasm/code/sound_cmd.asm`:

| Endereço ROM | Símbolo | Comando Z80 | Função Observada |
|---|---|---|---|
| `$0008A8` | `Sound_PlayBoot` | `$FF $0B <4×ptr24>` + `$10 <id>` | Verifica flags de ocupação `$FFFF9388` e `$FFFF9386`, carrega banco primário (`$2C4FA0`, `$2C5F35`, `$2C6078`, `$2D1259`) e cai em `Sound_Cmd10` (`$0008FA`) para disparar música ou SFX. |
| `$000920` | `Sound_Cmd0C` | `$0C` | **Pause** do driver de som. Chamado no início de `Main_PauseLoop` (`$002718`, `disasm/code/main_subs.asm:92`). |
| `$00092E` | `Sound_Cmd0D` | `$0D` | **Resume** do driver de som. Chamado em `loc_276E` (`$00276E`, `disasm/code/main_subs.asm:111`) ao retomar o jogo. |
| `$00093C` | `Sound_Cmd16` | `$16` | **Stop / Fade** geral das sequências ativas. Emitido antes das transições de tela/música (confirmado em `snd_ring.txt`). |
| `$0009FC` | `Z80_SoundStatus` | leitura `$A01A62` | Consulta tabela de status de canais/sequências na RAM do Z80. |
| `$000A28` | `Sound_PlaySong2` | `$FF $0B <4×ptr24>` + `$10 <id>` | Carrega banco secundário (`$2F9AA6`, `$2F9F59`, `$2F9F61`, `$2FCD19`) e envia `cmd $10` (`$000A6A`) — usado na música do título e faixas secundárias. |

Além dos comandos diretos, a flag de RAM **`State_ChangedFlag` (`$FFFFFD68`)** assume `$0080` durante `Main_PauseLoop` (`disasm/code/main_subs.asm:101`) e retorna a `$FFFF` em transições de estado (`disasm/code/state_dispatch.asm:53`, `main_subs.asm:104`). O par *(comandos em `sound_cmd.asm` + estado `$FFFFFD64`/`$FFFFFD68`)* fornece pontos determinísticos e não intrusivos para controlar BGM externa em C sem tocar na simulação 68k/Z80.

---

## 2. Causa-Raiz do Falso-Positivo de Percussão em `03_syria_blade`

### 2.1 O Problema na Versão Anterior de `tools/audio/chip_ring_to_midi.py`

A implementação anterior (`tools/audio/chip_ring_to_midi.py:200-224`) continha uma heurística cega pós-passada:

```python
DRUM_MIN_US = 60_000.0
...
if dur < DRUM_MIN_US:
    pnote = 36 if out[j][3] < 40 else (38 if out[j][3] < 60 else 42)
    out[j] = (out[j][0], 1, PERC, pnote, max(1, out[j][4]))
```

Qualquer nota FM (`ch < 6`) com duração inferior a `60 ms` era retirada de seu canal melódico e convertida em bumbo (`36`), caixa (`38`) ou chimbal (`42`) no canal MIDI 10 (`PERC = 9`).

Na faixa **`03_syria_blade`** (captura de `native_save_3`, 22,24 s), os canais `fm0` (122 notas, duração mediana `64 ms`) e `fm3` (137 notas, duração mediana `61 ms`) executam arpejos e ostinatos staccato rápidos justamente em torno do limiar de `60 ms`. O resultado medido:

- **164 notas FM tonais curtas** (distribuídas entre `fm0`, `fm2`, `fm3`, `fm4` e `fm5`) eram desviadas para o canal 10 como percussão aleatória fora da grade rítmica.
- Em contraste, `03_syria_blade` possui **0 eventos DAC** na captura e apenas **42 eventos reais de `psg_noise`**.

Paralelamente, a auditoria do decodificador revelou quatro bugs de registradores na versão anterior que também corrompiam a transcrição:

1. **YM2612 `0x28` (Key On/Off):** tratava os bits `0..2` como bitmask de 3 canais em vez de seletor (`0,1,2,4,5,6`) + máscara de operadores nos bits `4..7`, impedindo a captura correta de `fm3..fm5`.
2. **YM2612 `0xA0..0xA6` (Frequência):** agrupava `(0xA0, 0xA2, 0xA4)` como LSB, ignorando `0xA1` (`fm1` LSB) e engolindo `0xA4` (`fm0` MSB/Block).
3. **YM2612 `0x40..0x4E` (Total Level):** monitorava `0x43/0x47/0x4B` (offsets inválidos no YM2612), deixando a velocity travada.
4. **SN76489 (PSG):** invertia latch/data, ignorava o bit de tipo (`T`), colidia `psg0..psg2` com os canais MIDI `3..5` (mesmos de `fm3..fm5`) e omitia `psg_noise`.

### 2.2 Correção no Novo Pipeline por Canal

O novo `tools/audio/chip_ring_to_midi.py`:

- Decodifica fielmente todos os 11 canais físicos (`fm0..fm5`, `dac`, `psg0..psg2`, `psg_noise`), incluindo `alg` (`0xB0..0xB2`), `TL` dos portadores (`0x40..0x4E`), `MUL` dos operadores (`0x30..0x3E`, bits `0..3`, com `0 -> 0.5x`), bursts PCM em `0x2A`/`0x2B` e protocolo completo latch/data do SN76489.
- Calcula a frequência hardware-exact do YM2612 conforme `ymfm_opn.cpp:320-386`:
  - `f_base = fnum * (YM_CLK / 144.0) / (2 ** (21 - block))` (evitando o erro de `+12` semitons de `2 ** (20 - block)`).
  - Para os portadores ativos de `YM_CARRIER_SLOTS[alg & 7]` (dentro de `+18` de `TL` do portador mais forte e `TL < 96`), toma o menor `mul_factor` (`0.5` se `MUL == 0`, senão `float(MUL)`) e calcula `freq = f_base * mul_factor`.
- Implementa o latch real `0xA4..0xA6 -> 0xA0..0xA2` (escrita em `0xA4..0xA6` apenas armazena `msb_latch`; escrita em `0xA0..0xA2` atualiza atomicamente `fnum` e `block`) e trata atualizações de pitch durante sustain/legato tanto em FM quanto em PSG (`<20 ms` atualiza in-place; `>=20 ms` fecha a nota anterior e inicia a nova).
- **Desativa por padrão** o desvio FM `<60 ms` para percussão (mantido apenas para auditoria/comparação via flag explícita `--legacy-fm-perc`), preservando intactas todas as notas FM tonais curtas e arpejos (ex.: `03_syria_blade`).
- **Detecção física de patch FM metálico/ruído (`_FmChan.is_noise_perc_patch()`):** em vez de heurística por duração, inspeciona o estado de registradores do canal no *key-on* (`alg <= 4`, portador ativo com `mul >= 12` e pelo menos 2 moduladores com `mul >= 10` e modulação intensa `tl <= 15`). Patches metálicos de hi-hat/prato do driver GEMS (como `alg=0, mul=[15,15,15,14], tl=[3,2,5,27]` ou `tl=[3,15,5,23]`) são roteados para o canal `dac` (MIDI ch 9 / Canal 10 GM) conforme a duração do evento (`< 140 ms -> 42` Closed Hi-Hat, `140..349 ms -> 46` Open Hi-Hat, `>= 350 ms -> 49` Crash Cymbal 1), eliminando notas estridentes MIDI `99..121` nos canais `fm0..fm5` sem afetar arpejos tonais.
- Emite MIDI Tipo 1 multi-track determinístico:
  - Track 0: condutor (`set_tempo = 500000`, `PPQN = 480`).
  - Trilhas nomeadas: `"fm0"`..`"fm5"` (MIDI ch `0..5`), `"psg0"`..`"psg2"` (MIDI ch `6..8`), `"dac"` (MIDI ch `9`) e `"psg_noise"` (MIDI ch `9`, em trilha separada de `"dac"`).
- Exporta cada canal para seu próprio stem MIDI, permitindo que o produtor ajuste timbre GM, modo (`gm`/`percussion`/`chip`/`off`), volume, pan, mute/solo e EQ 3 bandas **canal por canal** no perfil `.toml`.

---

## 3. Catálogo de MIDIs (`.mid`), Perfis (`.toml`) e Mixer Studio (`tools/audio/`)

### 3.1 Fluxo de Arquivos e Ferramentas (100% MIDI, Zero WAV em Disco)

```text
assets/soundfonts/TimGM6mb.sf2           (SoundFont portátil único)
assets/audio_remaster/midi/*.mid         (17 MIDIs Tipo 1 multi-canal: bgm_id 0..16)
assets/audio_remaster/profiles/*.toml    (default.toml + IDs BGM 0..16 = 18 perfis)
   ▲
   │ edição direta (volume, pan, EQ, mute/solo, chip_blend, modo auto/gm/percussion/chip/off)
   │
tools/audio/mixer_studio.py + mixer_ui/  (Estúdio Web local em 127.0.0.1)
   │
   └─► síntese sob demanda em RAM (Web Audio API):
       tools/audio/tsf_midi_synth.dll    (ponte C sobre game/third_party/tsf.h; cache LRU em RAM)
```

- **`tools/audio/extract_all_midis.py`:** executa `EternalChampionsRecomp.exe` em modo headless (`GENESIS_AUDIO_APPLIEDDUMP`), navega pelo *Music Test* (`bgm_id 0..15`) e *Character Select* (`bgm_id 16`), fatia as janelas por faixa e converte via `chip_ring_to_midi.py` para os 17 arquivos MIDI Tipo 1 consolidados em `assets/audio_remaster/midi/<slug>.mid`.
- **`tools/audio/tsf_midi_synth.c` → `tools/audio/tsf_midi_synth.dll` (Windows) / `libtsf_midi_synth.so` (Linux):** biblioteca nativa mínima que expõe `tsf_bridge_open`, `tsf_bridge_close` e `tsf_bridge_render_channel` sobre `game/third_party/tsf.h`, replicando exatamente a lógica do jogo (`mode`: `0 = gm`, `1 = percussion`, `2 = auto` com `ec_auto_preset(key, dur_sec)` e soft-clipping `ec_gain_and_soft_clip_s16`).
- O diretório legado `assets/audio_remaster/stems/` foi removido completamente (`0` arquivos `.wav` em `assets/audio_remaster/`). O jogo em C lê exclusivamente `assets/soundfonts/TimGM6mb.sf2` e `assets/audio_remaster/profiles/*.toml`.

### 3.2 Contrato: `assets/audio_remaster/profiles/<track>.toml`

Cada faixa possui um perfil TOML autônomo editável manualmente ou via `tools/audio/mixer_studio.py`:

```toml
[track]
name = "default"                           # ou nome da faixa (ex.: "03_syria_blade")
bgm_id = -1                                # -1 em default.toml; 0..16 para BGMs da ROM
soundfont = "assets/soundfonts/TimGM6mb.sf2"
sample_rate = 48000
loop_start = 0
loop_end = 0
master_gain_db = 0.0
target_rms_dbfs = -16.5
chip_blend = 0.65                          # mistura ao vivo entre chip DSP e SoundFont

[channels.fm0]
role = "auto"
mode = "auto"        # "auto" | "gm" | "percussion" | "chip" | "off"
program = 0          # GM 0..127 (ou kit de bateria se mode = "percussion")
bank = 0             # 0 para melódico/auto, 128 para percussão GM
volume_db = 0.0
pan = -0.25          # -1.0 (L) .. +1.0 (R), lei equal-power
mute = false
solo = false
eq_low_db = 0.0      # Low shelf 180 Hz (Q=0.707)
eq_mid_db = 0.0      # Peaking 1.2 kHz (Q=1.0)
eq_high_db = 0.0     # High shelf 5.0 kHz (Q=0.707)
transpose = 0        # Semitons (ignorado em mode = "percussion")
```

Semântica dos modos de canal:
- **`"auto"` (padrão recomendado para canais FM mistos):** seleção dinâmica em tempo real por faixa de altura e envelope da nota no YM2612 — notas graves tocam **Electric Bass (Finger) `33`**, sustentações médias tocam **String Ensemble 2 `49`**, riffs médios rítmicos tocam **Synth Brass 1 `62`** e melodias agudas tocam **Lead 2 (Sawtooth) `81`**. Resolve o problema de canais FM que alternam baixo e melodia ao longo da música sem precisar sacrificar nenhum papel musical.
- **`"gm"`:** sintetiza via SoundFont no banco `bank` e programa fixo `program` (`0..127`) com transposição `transpose`.
- **`"percussion"`:** sintetiza no canal de bateria GM (bank `128`, kit `program`).
- **`"chip"`:** mantém a camada original do chip para esse canal.
- **`"off"`:** canal silenciado na camada SoundFont.

### 3.3 Catálogo Completo dos 17 MIDIs (`assets/audio_remaster/midi/`) e 18 Perfis (`profiles/`)

`assets/audio_remaster/midi/` contém **100% das 17 músicas da ROM** (`bgm_id 0..16`) em MIDI Tipo 1 multi-canal, pareadas 1:1 com `assets/audio_remaster/profiles/*.toml` (mais `default.toml` com `bgm_id = -1`):

| `bgm_id` | Arquivo `.mid` | Bytes | Duração | Notas | Canais Ativos no `.mid` |
|---:|---|---:|---:|---:|---|
| `0` | `03_syria_blade.mid` | 14293 | 40.43s | 1712 | 11 (`fm0, fm1, fm2, fm3, fm4, fm5, dac, psg0, psg1, psg2, psg_noise`) |
| `1` | `bgm_01_sanctuary.mid` | 8102 | 40.43s | 906 | 7 (`fm0, fm1, fm2, fm3, fm4, fm5, psg_noise`) |
| `2` | `bgm_02_stage_s25.mid` | 6704 | 40.42s | 725 | 8 (`fm0, fm1, fm2, fm3, fm4, fm5, psg0, psg1`) |
| `3` | `bgm_03_stage_s23.mid` | 9706 | 40.18s | 1115 | 8 (`fm0, fm1, fm2, fm3, fm4, fm5, dac, psg_noise`) |
| `4` | `02_salem_xavier.mid` | 4759 | 40.32s | 513 | 8 (`fm0, fm1, fm2, fm3, fm4, fm5, dac, psg_noise`) |
| `5` | `bgm_05_stage_s22.mid` | 15212 | 40.43s | 1709 | 11 (`fm0, fm1, fm2, fm3, fm4, fm5, dac, psg0, psg1, psg2, psg_noise`) |
| `6` | `bgm_06_stage_s26.mid` | 6018 | 40.17s | 681 | 6 (`fm0, fm1, fm2, fm3, fm4, fm5`) |
| `7` | `musica_luta.mid` | 7351 | 40.42s | 841 | 8 (`fm0, fm1, fm2, fm4, fm5, dac, psg0, psg_noise`) |
| `8` | `01_vietnam_midknight.mid` | 12755 | 40.43s | 1471 | 11 (`fm0, fm1, fm2, fm3, fm4, fm5, dac, psg0, psg1, psg2, psg_noise`) |
| `9` | `bgm_09_bank1_09.mid` | 7577 | 40.15s | 867 | 8 (`fm0, fm1, fm2, fm3, fm4, fm5, dac, psg_noise`) |
| `10` | `bgm_10_stage_s27.mid` | 12376 | 40.43s | 1381 | 10 (`fm0, fm1, fm2, fm3, fm4, fm5, psg0, psg1, psg2, psg_noise`) |
| `11` | `bgm_11_stage_s24.mid` | 10117 | 40.43s | 1191 | 10 (`fm0, fm2, fm3, fm4, fm5, dac, psg0, psg1, psg2, psg_noise`) |
| `12` | `musica_titulo.mid` | 19087 | 40.43s | 2305 | 9 (`fm0, fm1, fm2, fm3, fm4, fm5, psg0, psg1, psg2`) |
| `13` | `bgm_13_bank2_01.mid` | 3228 | 40.02s | 320 | 9 (`fm0, fm1, fm2, fm3, fm4, fm5, psg0, psg1, psg2`) |
| `14` | `bgm_14_bank2_02.mid` | 2933 | 40.17s | 306 | 6 (`fm0, fm1, fm2, fm3, fm4, fm5`) |
| `15` | `bgm_15_bank2_03.mid` | 10107 | 40.43s | 1103 | 10 (`fm0, fm1, fm2, fm3, fm4, fm5, dac, psg0, psg1, psg2`) |
| `16` | `musica_selecao.mid` | 7463 | 40.12s | 853 | 8 (`fm0, fm1, fm2, fm3, fm4, fm5, dac, psg_noise`) |

### 3.4 Mixer Studio (`tools/audio/mixer_studio.py` + `tools/audio/mixer_ui/`)

- **Integração nativa via `ctypes` (`TsfBridge`):** carrega `tools/audio/tsf_midi_synth.dll` (ou `libtsf_midi_synth.so` no Linux) e abre `assets/soundfonts/TimGM6mb.sf2` uma única vez no ciclo de vida de `MixerApp` (fechando em `close()` / `atexit`).
- **Leitura direta de `.mid` + sincronia 0 ms:** lê cada trilha por `Meta 0x03 Track Name` via `chip_ring_to_midi.read_midi_notes()` (mantendo os 11 canais separados, incluindo `dac` e `psg_noise` no canal MIDI 9) e calcula `track_duration = max(1.0, max(note.end_sec) + 0.5)` para sintetizar todos os canais da música com o mesmo `total_frames`.
- **Cache LRU em RAM (zero WAV em disco):** armazena até 64 buffers WAV (`io.BytesIO` + `wave.open`) indexados por `(track, channel, mode, bank, program, transpose, sample_rate, soundfont, midi_mtime_ns)` e serve `GET /api/tracks/<track>/stems/<channel>.wav` diretamente da memória com suporte a `Range` (`206` / `200`).
- **Rotas principais:**
  - `GET /api/tracks` e `GET /api/tracks/<track>`: listam os 18 perfis (`default` com `has_midi = false` e todos os 17 BGMs `0..16` com `has_midi = true`, `midi_url`, `synth_engine = "TinySoundFont (tsf.h)"` e `note_count` real por canal).
  - `GET /api/tracks/<track>/midi`: serve o arquivo `.mid` com `Content-Type: audio/midi` e proteção contra path traversal.
  - `POST /api/tracks/<track>/render-channel` e `POST /api/tracks/<track>/render-all`: invalidam o cache do canal/faixa e sintetizam imediatamente em RAM via `tsf_midi_synth.dll`, retornando `audio_url` para recarregamento instantâneo na Web Audio API.
  - `POST /api/tracks/<track>/save`: persiste as alterações diretamente em `assets/audio_remaster/profiles/<track>.toml`.
- **UI (`mixer_ui/`):** todos os 17 BGMs tocam ao vivo imediatamente ao clicar em Play, com botões **Salvar Perfil (.toml)**, **Baixar MIDI (.mid)** e **Atualizar Timbres** (síntese em RAM), e `Auto-aplicar instrumento` ativo por padrão para trocar timbres (inclusive `auto`) em ~20–60 ms sem gravar nada em disco.

---

## 4. Arquitetura de Runtime em Tempo Real (`game/ec_audio.{h,c}` + `tsf.h`)

### 4.1 Pontos de Integração no Runner

O runner (`segagenesisrecomp/runner/audio.h`) fornece dois ganchos não invasivos:

1. **Observador de eventos de chip (`audio_mixer_set_chip_observer`):** notifica em tempo real cada evento decodificado de nota/key-on/key-off/pitch/volume nos 11 canais físicos (`fm0..fm5`, `dac`, `psg0..psg2`, `psg_noise`), sem interferir no estado da CPU 68000, Z80, YM2612 ou SN76489 (paridade WRAM 0/65536 preservada).
2. **Fonte extra de mixagem (`AudioExtraSource` / `audio_set_extra_source`):**
   ```c
   typedef void (*AudioExtraSource)(int16_t *mix_stereo, size_t frames, double rate);
   void audio_set_extra_source(AudioExtraSource source);
   ```
   `audio_flush()` mistura FM + PSG na taxa interna de saída e invoca o callback registrado **uma vez por quadro emulado na thread principal**, in-place em `mix_stereo`, antes da captura WAV, do controle de volume mestre e da entrega à SDL (zero locks ou alocações no callback de áudio da SDL).

### 4.2 Fluxo de Duas Camadas em `game/ec_audio.{h,c}`

1. **Camada de Síntese SoundFont em Tempo Real (`TinySoundFont` — `game/third_party/tsf.h`):**
   - Carrega `assets/soundfonts/TimGM6mb.sf2` uma única vez na inicialização e indexa todos os perfis em `assets/audio_remaster/profiles/*.toml` (com fallback automático para `default.toml` quando o `bgm_id` ativo não possuir perfil específico).
   - Ao receber eventos passivos de BGM (`ec_audio_on_bgm_play`, `ec_audio_on_bgm_pause`, `ec_audio_on_bgm_stop` nos hooks de `sound_cmd.asm`: `$0008A8`, `$000A28`, `$000920`, `$00092E`, `$00093C` e flag `$FFFFFD68`), seleciona o perfil correspondente ao `bgm_id` ativo sem I/O bloqueante por frame.
   - Converte os eventos ao vivo de `audio_mixer_set_chip_observer` em chamadas `tsf_note_on` / `tsf_note_off` / pitch / pan / ganho por canal, aplicando:
     - Resolução dinâmica `mode = "auto"` (Bass `33` / Strings `49` / Brass `62` / Lead `81`) conforme altura e envelope em tempo real.
     - `program`, `bank`, `volume_db`, `pan`, `mute`, `solo`, EQ e `transpose` definidos no `.toml` do `bgm_id` ativo.
     - Sincronia exata com a música real do jogo (loops, variações de tempo, pausas e transições seguem o driver Z80 frame a frame, sem risco de dessincronia).

2. **Camada DSP Custo-Zero para Chip, SFX e Vozes (`mix_stereo`):**
   Mesmo em modo `dsp` puro ou durante vozes/SFX não-BGM, uma cadeia DSP de complexidade **O(1) por amostra** e **memória estática fixa** processa o áudio original do chip antes da soma com o barramento SoundFont (`chip_blend`):
   - **EQ de Estúdio em 3 Biquads DF2T (precisão `double`):** low shelf (`110 Hz`, `+1,5 dB`), dip de presença (`2,8 kHz`, `-2,0 dB`, `Q = 1,1`) e high shelf (`8,5 kHz`, `+1,0 dB`) para domar a aspereza FM/PSG mantendo clareza.
   - **Alargamento Estéreo Mid/Side (`1.12x`):** adiciona espacialidade controlada a SFX originalmente mono sem borrar transientes.
   - **Reverb Schroeder Compacto (4 combs + 2 allpass, decimado acima de `64 kHz`):** cola os efeitos sonoros e vozes PCM originais do Mega Drive no mesmo espaço acústico dos instrumentos SoundFont.
   - **Ducking / Blend Suave (`chip_blend`) + Soft-Limiter de Saída:** preserva 100% dos golpes, gritos e vozes do chip com teto livre de clipping.
