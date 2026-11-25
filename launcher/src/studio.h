// Estudio de Remaster (PLAN §10) - modelo sem UI. Usa a biblioteca ec_remaster_core (launcher/remaster):
// dumps -> notas por musica, voices.toml/perfis, sintese da previa (tsf), captura do original.
// A tela (screens/studio_screen.cpp) e o --selftest-studio usam EXATAMENTE este caminho, incluindo o
// callback de audio (Studio::audio_callback).
#pragma once
#include "catalog.h"
#include "dumps.h"
#include "original_capture.h"
#include "preview_synth.h"
#include "voices_toml.h"
#include <atomic>
#include <filesystem>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class App;
struct SDL_AudioDevice;

struct StudioPaths {
    std::filesystem::path root;          // raiz do repo/instalacao (cwd do jogo)
    std::filesystem::path voices;        // voices.toml
    std::filesystem::path profiles_dir;  // profiles/
    std::filesystem::path sf2;           // SoundFont absoluto
    std::filesystem::path cache_dir;     // bgm_NN.appdump.txt (derivados da ROM, fora do git)
    ecr::GameRun run;                    // exe/rom/work_dir do jogo headless
};

enum class VoiceScope { Global, Song };

StudioPaths make_studio_paths(App& app);   // definido em screens/studio_screen.cpp
// Teste sem janela: --selftest-studio --root <repo> --dumps <dir> [--out <dir>]. Retorna 0 se OK.
int studio_selftest(const std::string& root, const std::string& dumps, const std::string& out_dir);

struct JobState {
    bool running = false;
    float frac = -1;
    std::string text;
    std::string error;
};

class Studio {
public:
    enum class Stage { NoRom, NoSf2, NeedDumps, Extracting, Loading, Ready, Error };

    explicit Studio(StudioPaths p);
    ~Studio();

    Stage stage() const { return stage_; }
    const std::string& error() const { return error_; }
    const StudioPaths& paths() const { return p_; }
    void refresh();                                  // reavalia ROM/SF2/dumps; dispara o carregamento
    void poll();                                     // thread principal: junta threads concluidas
    void start_extract();                            // dumps em segundo plano (cancelavel)
    void cancel_jobs();
    JobState dump_job() const;
    JobState orig_job() const;

    // ---- dados
    const std::vector<ecr::SongNotes>& songs() const { return songs_; }
    bool select_song(int bgm_id);                    // carrega perfil, monta mapas, set_song
    int current_bgm() const { return bgm_; }
    const ecr::SongNotes* song() const;
    ecr::VoicesFile& voices() { return voices_; }
    ecr::ProfileFile& profile() { return profile_; }
    bool profile_loaded() const { return profile_ok_; }

    // ---- edicao (desfazer/refazer na sessao)
    ecr::VoiceEdit effective_edit(const std::string& hash) const;     // override da musica (se houver) senao global
    bool has_override(const std::string& hash) const;
    bool voice_dirty(const std::string& hash) const;                  // difere do que estava no disco
    void edit_voice(const std::string& hash, const ecr::VoiceEdit& e, VoiceScope scope);
    void clear_override(const std::string& hash);
    void edit_channel(const ecr::ProfileChannel& c);
    void set_solo(const std::string& key, bool on);
    void set_mute(const std::string& key, bool on);
    ecr::ChannelMap map_of(const std::string& key) const;
    std::vector<ecr::ChannelMap> maps() const { return synth_.maps(); }
    bool dirty() const;
    bool save(std::string* err);                                      // voices.toml + perfil (atomico, .bak)
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    void undo();
    void redo();
    int songs_using(const std::string& hash) const;                   // n de musicas (meta.songs)
    bool saved_edit(const std::string& hash, ecr::VoiceEdit* out) const;   // valor em disco (baseline)
    void debug_set_orig_job(const JobState& j) { std::lock_guard<std::mutex> l(mu_); orig_ = j; }   // so p/ capturas

    // ---- audio
    void set_headless(bool h) { headless_ = h; }     // selftest: sem dispositivo SDL
    bool open_audio_device();                        // SDL; no selftest nao e chamado
    void close_audio_device();
    void play(bool on);
    bool playing() const { return playing_; }
    void set_loop(bool l) { synth_.set_loop(l); }
    void seek_seconds(double s);
    double position_s() const;
    double duration_s() const { return synth_.duration_s(); }
    float level(const std::string& key) const { return synth_.channel_level(key); }
    // A/B: true = original (se ja capturado)
    void set_ab_original(bool o) { ab_original_ = o; }
    bool ab_original() const { return ab_original_; }
    bool original_ready() const { return orig_ready_; }
    void start_capture_original();
    // Mesmo caminho do dispositivo SDL (e do selftest): preenche `frames` quadros estereo float.
    void audio_callback(float* stereo, int frames);
    ecr::PreviewSynth& synth() { return synth_; }
    void inject_original(std::vector<float> stereo);  // teste

    // ---- "Testar no jogo": salva e abre o jogo no Music Test da musica atual (sem bloquear)
    bool test_in_game(std::string* err);
    bool game_running() const { return game_flag_->load(); }

private:
    void load_songs_thread();
    ecr::ChannelMap map_for_voice(const std::string& hash) const;
    void apply_voice_to_synth(const std::string& hash);
    void snapshot_baseline();
    struct UndoItem {
        bool is_override;
        std::string hash;
        bool had_override;
        ecr::VoiceEdit before_global, before_override;
        bool after_had_override = false;
        ecr::VoiceEdit after_global, after_override;
    };
    void apply_undo_item(const UndoItem& u, bool undo);

    StudioPaths p_;
    Stage stage_ = Stage::NoRom;
    std::string error_;

    std::vector<ecr::SongNotes> songs_;
    int bgm_ = -1;
    ecr::VoicesFile voices_;
    ecr::ProfileFile profile_;
    bool profile_ok_ = false;
    std::map<std::string, ecr::VoiceEdit> baseline_;
    std::vector<UndoItem> undo_, redo_;

    ecr::PreviewSynth synth_;
    bool sf2_ok_ = false;

    // threads
    mutable std::mutex mu_;
    JobState dump_, orig_;
    std::thread th_dump_, th_load_, th_orig_;
    std::atomic<bool> cancel_{false}, dump_done_{false}, load_done_{false}, orig_done_{false};
    std::vector<ecr::SongNotes> loaded_;
    ecr::OriginalAudio orig_audio_;
    std::atomic<bool> orig_ready_{false};
    int orig_bgm_ = -1;

    // audio
    bool headless_ = false;
    unsigned dev_id_ = 0;
    std::atomic<bool> playing_{false};
    std::atomic<bool> ab_original_{false};
    float ab_mix_ = 0;
    std::vector<float> scratch_;
    std::vector<float> inj_;       // original injetado (teste)

    std::shared_ptr<std::atomic<bool>> game_flag_ = std::make_shared<std::atomic<bool>>(false);
    std::thread th_game_;
};
