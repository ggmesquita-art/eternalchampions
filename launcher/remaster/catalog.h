// catalog.h - catalogo de instrumentos FM (hash do patch) e notas por musica, a partir dos
// dumps GENESIS_AUDIO_APPLIEDDUMP. Porte de tools/audio/voice_catalog.py +
// chip_ring_to_midi.parse_chip_ring (mesma logica, mesmos numeros).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "voices_toml.h"

namespace ecr {

// --- hash do contrato (docs/audio_voice_hash.md) ---
uint32_t fnv1a32(const uint8_t* data, size_t n);
std::string hash_to_string(uint32_t h);              // "%08x"
// Vetor de teste (ALG=7, FB=0, resto zero) => "ce6df9ba". Devolve true se bate.
bool hash_selftest(std::string* got_hash);

// --- tabela das 17 musicas ---
struct BgmInfo {
    int id;                 // bgm_id (0..16)
    const char* slug;       // nome do perfil / MIDI
    const char* title;      // nome amigavel (UI)
};
const BgmInfo* bgm_table();   // 17 entradas
int bgm_count();
const BgmInfo* bgm_by_slug(const std::string& slug);
const BgmInfo* bgm_by_id(int id);

// --- notas ---
struct Note {
    double start_s = 0.0;   // segundos desde o 1o evento do dump
    double dur_s = 0.0;
    int note = 0;           // MIDI 0..127
    int vel = 0;            // 1..127
    bool perc = false;      // percussao FM ("ruido FM")
    int ch = 0;             // canal FM fisico (0..5) quando de voz FM
    int tl = 0;             // TL efetivo dos carriers no key-on
    double end_s() const { return start_s + dur_s; }
};

// Instrumento FM numa musica.
struct VoiceTrack {
    std::string hash;
    std::vector<Note> notes;      // cruas (ordenadas por start, note)
    std::vector<Note> q;          // apos round-trip MIDI (ticks de 1/480 de 120 BPM): o que se sintetiza
    std::vector<uint8_t> patch;   // 31 bytes canonicos do contrato
};

// PSG0..2, psg_noise, dac ("dac" = so DAC $2A/$2B, nunca FM)
struct ChanTrack {
    std::string name;
    std::vector<Note> notes;
    std::vector<Note> q;
};

struct SongNotes {
    int bgm_id = -1;
    std::string slug;
    std::vector<VoiceTrack> voices;   // ordenadas por (1o start, hash) = ordem das trilhas do MIDI
    std::vector<ChanTrack> channels;  // so as nao vazias, ordem psg0 psg1 psg2 psg_noise dac
    double duration_s = 0.0;          // max fim das notas quantizadas + 0.5 (min 1.0), como mixer_studio
    const VoiceTrack* find_voice(const std::string& hash) const;
    const ChanTrack* find_channel(const std::string& name) const;
};

// Le um dump bgm_NN.appdump.txt e produz as notas. Devolve false se ilegivel/sem eventos.
// `patches` acumula hash -> bytes do patch (para o catalogo global).
bool parse_dump(const std::string& path, int bgm_id, const std::string& slug, SongNotes* out,
                std::map<std::string, std::vector<uint8_t>>* patches = nullptr);

// --- metadados e heuristica (decode_patch / suggest / suggest_drum_note) ---
struct Suggestion {
    std::string mode;       // "gm" | "percussion"
    int program = 0;
    std::string label;      // "bass", "brass", "pad"...
};
void decode_patch(const std::vector<uint8_t>& pb, VoiceMeta* m);   // alg/fb/ams_fms/ops/patch
Suggestion suggest(const std::vector<uint8_t>& pb, const std::vector<Note>& notes);
int suggest_drum_note(const std::vector<Note>& notes);

// Catalogo global: agrega as musicas e gera as entradas (equivale a build_voice_entries).
std::vector<VoiceEntry> build_voice_entries(const std::vector<SongNotes>& songs);
// Nome GM do programa (0..127) e da peca de bateria (27..87)
const char* gm_program_name(int program);
const char* gm_drum_name(int note);
const char* gm_program_category(int program);

}  // namespace ecr
