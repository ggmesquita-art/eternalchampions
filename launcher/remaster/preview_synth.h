// preview_synth.h - sintese da previa (B: REMASTER) com tsf.h e o mesmo SoundFont do jogo.
// Semantica = tools/audio/tsf_midi_synth.c (ganho 0.55 + soft clip) + mixer_studio.py
// (modo gm/percussion/auto, transpose, drum_note); chip/off = mudo.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "catalog.h"
#include "voices_toml.h"

namespace ecr {

constexpr int kPreviewRate = 48000;

// Mapeamento efetivo de um canal/instrumento na previa.
struct ChannelMap {
    std::string key;            // hash (8 hex) de voz FM, ou psg0 psg1 psg2 psg_noise dac
    bool is_voice = false;
    std::string mode = "off";   // gm | percussion | auto | chip | off
    int bank = 0, program = 0, transpose = 0, drum_note = 0;
    double volume_db = 0.0;     // ganho do canal
    double pan = 0.0;           // -1..1 (lei de balanco: centro = ganho 1 nos dois lados)
    bool mute = false, solo = false;
    bool synth() const { return mode == "gm" || mode == "percussion" || mode == "auto"; }
};

// Monta os mapeamentos de uma musica como o jogo/mixer_studio: instrumentos FM de
// voices.toml (+ override do perfil da faixa, se `mode` != auto) e canais PSG/ruido/DAC do perfil.
// Ordem: vozes por (n de notas desc, hash), depois os canais nao-FM presentes na musica.
// Instrumentos ausentes de voices.toml usam a sugestao automatica.
std::vector<ChannelMap> build_channel_maps(const SongNotes& song, const VoicesFile& voices,
                                           const ProfileFile& profile);

class PreviewSynth {
public:
    PreviewSynth();
    ~PreviewSynth();
    PreviewSynth(const PreviewSynth&) = delete;
    PreviewSynth& operator=(const PreviewSynth&) = delete;

    bool load_soundfont(const std::string& sf2_path, std::string* err = nullptr);
    bool has_soundfont() const;

    // Carrega a musica e os mapeamentos (zera a posicao).
    void set_song(const SongNotes& song, const std::vector<ChannelMap>& maps);
    // Altera 1 canal (instantaneo; reconfigura preset/eventos se o mapeamento mudou).
    void update_map(const ChannelMap& m);
    void set_master_gain_db(double db);
    std::vector<ChannelMap> maps() const;

    int64_t total_frames() const;
    double duration_s() const;
    int64_t position_frames() const;
    void seek_frames(int64_t f);
    void set_loop(bool loop);
    bool finished() const;

    // Streaming: preenche `frames` quadros estereo intercalados (float). Zera apos o fim
    // (ou volta ao inicio se loop). Seguro para o callback de audio (mutex curto).
    void render_block(float* stereo, int frames);
    // Render completo (0..fim), independente da posicao de streaming.
    void render_all(std::vector<float>* stereo) const;
    // Pico recente (0..1) do canal, para o VU.
    float channel_level(const std::string& key) const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

}  // namespace ecr
