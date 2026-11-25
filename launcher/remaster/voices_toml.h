// voices_toml.h - leitura/gravacao de assets/audio_remaster/voices.toml e profiles/*.toml
// preservando comentarios, ordem e [voices.<hash>.meta] (edicao "cirurgica" por linha).
// Formato de saida identico ao de tools/audio/voice_catalog.py::write_toml.
#pragma once
#include <cstdio>
#include <string>
#include <vector>

namespace ecr {

// Campos editaveis de um instrumento (o motor do jogo so le estes).
struct VoiceEdit {
    std::string name;
    std::string mode = "gm";   // gm | percussion | chip | off
    int program = 0;           // 0..127
    int bank = 0;              // 0..128
    int transpose = 0;         // -24..24 (UI); o jogo aceita -48..48
    double volume_db = 0.0;    // -60..24
    int drum_note = 0;         // 0..127 (so percussion; 0 = nota da trilha)
    bool operator==(const VoiceEdit& o) const;
    bool operator!=(const VoiceEdit& o) const { return !(*this == o); }
};

struct FmOp {
    std::string slot;          // "S1".."S4"
    bool carrier = false;
    int mul = 0, dt = 0, ar = 0, d1r = 0, d2r = 0, sl = 0, rr = 0;
    int tl = -1;               // so para moduladores (carrier => -1)
};

// Subtabela [voices.<hash>.meta] (somente leitura; regerada pelo catalogo).
struct VoiceMeta {
    bool valid = false;
    int alg = 0, fb = 0, ams_fms = 0;
    std::string patch;         // 31 bytes em hex
    std::vector<FmOp> ops;
    std::vector<std::string> songs;     // slugs, na ordem de BGM_TABLE
    std::vector<std::string> channels;  // "fm0".."fm5"
    int notes = 0, note_min = 0, note_max = 0, note_median = 0, avg_dur_ms = 0;
    std::string suggestion;
};

struct VoiceEntry {
    std::string hash;          // 8 hex minusculos
    VoiceEdit edit;
    VoiceMeta meta;
};

// Escapa como json.dumps(ensure_ascii=True) (formato de string do voice_catalog.py).
std::string toml_quote(const std::string& utf8);
// "%.1f" (voices.toml)
std::string fmt_float1(double v);
// f"{v:.4f}".rstrip("0").rstrip(".") + ".0" se preciso (perfis; mixer_studio._toml_val)
std::string fmt_float_profile(double v);
// Equivalente a mixer_studio._clean_voice_name (imprimiveis, trim, 48 caracteres).
std::string clean_voice_name(const std::string& utf8);

class VoicesFile {
public:
    VoicesFile();
    ~VoicesFile();
    VoicesFile(VoicesFile&&) noexcept;
    VoicesFile& operator=(VoicesFile&&) noexcept;
    VoicesFile(const VoicesFile&) = delete;
    VoicesFile& operator=(const VoicesFile&) = delete;

    // Arquivo ausente => documento novo (cabecalho canonico), save() o cria.
    bool load(const std::string& path, std::string* err = nullptr);
    // Grava atomicamente (tmp + rename) mantendo <path>.bak do conteudo anterior.
    // So grava se houve alteracao (ou o arquivo nao existia).
    bool save(std::string* err = nullptr);
    bool dirty() const;
    const std::string& path() const;
    std::string serialize() const;

    const std::vector<VoiceEntry>& voices() const;       // ordem do arquivo
    const VoiceEntry* find(const std::string& hash) const;
    // Atualiza so os campos que diferem do que esta no arquivo.
    bool set_edit(const std::string& hash, const VoiceEdit& e);
    // Insere um bloco canonico (campos editaveis + meta) na posicao ordenada.
    // Se o hash ja existe, nao faz nada e devolve false.
    bool add_voice(const VoiceEntry& v);

private:
    struct Impl;
    Impl* p_;
};

// Perfis por musica: profiles/<slug>.toml (+ default.toml).
struct ProfileTrack {
    std::string name;
    int bgm_id = -1;
    std::string soundfont = "assets/soundfonts/TimGM6mb.sf2";
    int sample_rate = 48000;
    int loop_start = 0, loop_end = 0;
    double master_gain_db = 0.0, target_rms_dbfs = -16.5, chip_blend = 1.0;
};

struct ProfileChannel {
    std::string name;          // fm0..fm5, dac, psg0..psg2, psg_noise
    std::string role = "auto";
    std::string mode = "auto"; // auto | gm | percussion | chip | off
    int program = 0, bank = 0, transpose = 0;
    double volume_db = 0.0, pan = 0.0;
    bool mute = false, solo = false;
    double eq_low_db = 0.0, eq_mid_db = 0.0, eq_high_db = 0.0;
    bool operator==(const ProfileChannel& o) const;
};

// Override [voices.<hash>] dentro do perfil da faixa (vale so para essa musica).
struct VoiceOverride {
    std::string hash;
    VoiceEdit edit;            // todos os campos gravados (mode != auto no jogo)
};

class ProfileFile {
public:
    ProfileFile();
    ~ProfileFile();
    ProfileFile(ProfileFile&&) noexcept;
    ProfileFile& operator=(ProfileFile&&) noexcept;
    ProfileFile(const ProfileFile&) = delete;
    ProfileFile& operator=(const ProfileFile&) = delete;

    bool load(const std::string& path, std::string* err = nullptr);  // arquivo precisa existir
    bool save(std::string* err = nullptr);                           // atomico + .bak
    bool dirty() const;
    const std::string& path() const;
    std::string serialize() const;

    const ProfileTrack& track() const;
    const std::vector<ProfileChannel>& channels() const;             // ordem do arquivo
    const ProfileChannel* find_channel(const std::string& name) const;
    // Passa a gravar em `new_path` (arquivo novo) e fixa [track] name/bgm_id: usado para criar <slug>.toml a partir de
    // default.toml na primeira edicao por musica (o jogo ignora perfis sem bgm_id valido). Marca como alterado.
    bool retarget(const std::string& new_path, const std::string& name, int bgm_id);
    bool set_channel(const ProfileChannel& c);                       // so campos alterados

    const std::vector<VoiceOverride>& overrides() const;
    const VoiceOverride* find_override(const std::string& hash) const;
    bool set_override(const std::string& hash, const VoiceEdit& e);  // cria ou atualiza
    bool remove_override(const std::string& hash);

private:
    struct Impl;
    Impl* p_;
};

// Escrita atomica de arquivo (tmp na mesma pasta + rename; .bak opcional do anterior).
bool write_file_atomic(const std::string& path, const std::string& data, bool make_bak, std::string* err);
bool read_file(const std::string& path, std::string* out);
// fopen com caminho UTF-8 (Windows: _wfopen).
std::FILE* fopen_u8(const std::string& path, const char* mode);

}  // namespace ecr
