#include "studio.h"
#include <SDL.h>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

Studio::Studio(StudioPaths p) : p_(std::move(p)) { refresh(); }

Studio::~Studio() {
    cancel_jobs();
    close_audio_device();
    if (th_dump_.joinable()) th_dump_.join();
    if (th_load_.joinable()) th_load_.join();
    if (th_orig_.joinable()) th_orig_.join();
    if (th_game_.joinable()) th_game_.detach();   // o jogo segue por conta propria
}

// ---------------------------------------------------------------------------
// estagios e jobs
// ---------------------------------------------------------------------------
void Studio::refresh() {
    std::error_code ec;
    if (stage_ == Stage::Extracting || stage_ == Stage::Loading || stage_ == Stage::Ready) return;
    error_.clear();
    sf2_ok_ = fs::is_regular_file(p_.sf2, ec);
    if (ecr::dumps_complete(p_.cache_dir.u8string())) {
        // dumps ja prontos: nao exigem a ROM; so o SF2
        if (!sf2_ok_) { stage_ = Stage::NoSf2; return; }
        stage_ = Stage::Loading;
        load_done_ = false;
        th_load_ = std::thread([this] { load_songs_thread(); });
        return;
    }
    if (p_.run.rom.empty() || !fs::is_regular_file(fs::u8path(p_.run.rom), ec) || !p_.rom_valid) { stage_ = Stage::NoRom; return; }   // extrair exige ROM valida (SHA US)
    if (!sf2_ok_) { stage_ = Stage::NoSf2; return; }
    stage_ = Stage::NeedDumps;
}

void Studio::load_songs_thread() {
    std::vector<ecr::SongNotes> v;
    bool ok = true;
    for (int i = 0; i < ecr::bgm_count(); ++i) {
        const ecr::BgmInfo& b = ecr::bgm_table()[i];
        ecr::SongNotes s;
        if (!ecr::parse_dump(ecr::dump_path(p_.cache_dir.u8string(), b.id), b.id, b.slug, &s)) { ok = false; continue; }
        v.push_back(std::move(s));
    }
    std::sort(v.begin(), v.end(), [](const ecr::SongNotes& a, const ecr::SongNotes& b) { return a.bgm_id < b.bgm_id; });
    {
        std::lock_guard<std::mutex> l(mu_);
        loaded_ = std::move(v);
        if (!ok && loaded_.empty()) error_ = "Nenhum dump legivel em " + p_.cache_dir.u8string();
    }
    load_done_ = true;
}

void Studio::start_extract() {
    if (stage_ != Stage::NeedDumps && stage_ != Stage::Error) return;
    stage_ = Stage::Extracting;
    cancel_ = false; dump_done_ = false;
    { std::lock_guard<std::mutex> l(mu_); dump_ = JobState{true, 0, "Iniciando…", ""}; }
    if (th_dump_.joinable()) th_dump_.join();
    th_dump_ = std::thread([this] {
        std::string err;
        std::error_code ec;
        fs::create_directories(p_.cache_dir, ec);
        bool ok = ecr::extract_dumps(p_.run, p_.cache_dir.u8string(),
            [this](float f, const std::string& s) { std::lock_guard<std::mutex> l(mu_); dump_.frac = f; dump_.text = s; },
            [this] { return cancel_.load(); }, &err);
        std::lock_guard<std::mutex> l(mu_);
        dump_.running = false;
        dump_.error = ok ? "" : (err.empty() ? "cancelado" : err);
        dump_.frac = ok ? 1.f : dump_.frac;
        dump_done_ = true;
    });
}

void Studio::cancel_jobs() { cancel_ = true; }

JobState Studio::dump_job() const { std::lock_guard<std::mutex> l(mu_); return dump_; }
JobState Studio::orig_job() const { std::lock_guard<std::mutex> l(mu_); return orig_; }

void Studio::poll() {
    if (stage_ == Stage::Extracting && dump_done_) {
        if (th_dump_.joinable()) th_dump_.join();
        dump_done_ = false;
        JobState j = dump_job();
        stage_ = Stage::NeedDumps;
        if (j.error.empty()) refresh();
        else error_ = j.error;
    }
    if (stage_ == Stage::Loading && load_done_) {
        if (th_load_.joinable()) th_load_.join();
        load_done_ = false;
        {
            std::lock_guard<std::mutex> l(mu_);
            songs_ = std::move(loaded_);
        }
        std::string err;
        if (songs_.empty()) { stage_ = Stage::Error; if (error_.empty()) error_ = "dumps ilegiveis"; return; }
        if (!voices_.load(p_.voices.u8string(), &err)) { stage_ = Stage::Error; error_ = "voices.toml: " + err; return; }
        if (!synth_.load_soundfont(p_.sf2.u8string(), &err)) { stage_ = Stage::NoSf2; error_ = "SoundFont: " + err; return; }
        snapshot_baseline();
        stage_ = Stage::Ready;
        select_song(songs_.front().bgm_id);
    }
    if (orig_done_) {
        if (th_orig_.joinable()) th_orig_.join();
        orig_done_ = false;
    }
}

const ecr::SongNotes* Studio::song() const {
    for (auto& s : songs_) if (s.bgm_id == bgm_) return &s;
    return nullptr;
}

void Studio::snapshot_baseline() {
    baseline_.clear();
    for (auto& v : voices_.voices()) baseline_[v.hash] = v.edit;
}

// ---------------------------------------------------------------------------
// musica e mapas
// ---------------------------------------------------------------------------
bool Studio::select_song(int bgm_id) {
    const ecr::SongNotes* sg = nullptr;
    for (auto& s : songs_) if (s.bgm_id == bgm_id) sg = &s;
    if (!sg) return false;
    op_error_.clear();
    // edicoes pendentes do perfil da musica atual: salva antes de trocar (nada se perde ao usar LB/RB)
    if (profile_ok_ && profile_.dirty()) {
        std::string e;
        if (!profile_.save(&e)) { op_error_ = "Nao foi possivel salvar o perfil antes de trocar de musica: " + e; return false; }
    }
    const bool was_playing = playing_;
    play(false);
    bgm_ = bgm_id;
    // perfil da faixa (overrides e canais PSG/DAC). Se <slug>.toml nao existe, default.toml serve de MOLDE somente-leitura:
    // o arquivo da faixa so e criado na primeira edicao por musica (nunca se grava em default.toml)
    std::error_code ec;
    fs::path pp = p_.profiles_dir / (sg->slug + ".toml");
    profile_pending_new_ = false;
    if (!fs::exists(pp, ec)) { profile_target_ = pp; pp = p_.profiles_dir / "default.toml"; profile_pending_new_ = true; }
    profile_ = ecr::ProfileFile();
    std::string err;
    profile_ok_ = profile_.load(pp.u8string(), &err);
    synth_.set_song(*sg, ecr::build_channel_maps(*sg, voices_, profile_));
    undo_.clear(); redo_.clear();
    // original capturado e de outra musica? descarta
    {   // original ja capturado para esta musica? (A/B sem recapturar)
        std::shared_ptr<const ecr::OriginalAudio> o;
        { std::lock_guard<std::mutex> l(mu_); auto it = originals_.find(bgm_id); if (it != originals_.end()) o = it->second; }
        std::atomic_store(&orig_cur_, o);
        orig_ready_ = (o != nullptr);
        if (!o) ab_original_ = false;
    }
    if (was_playing) play(true);
    return true;
}

int Studio::songs_using(const std::string& hash) const {
    const ecr::VoiceEntry* v = voices_.find(hash);
    return v ? (int)v->meta.songs.size() : 0;
}

ecr::VoiceEdit Studio::effective_edit(const std::string& hash) const {
    if (profile_ok_) {
        if (const ecr::VoiceOverride* o = profile_.find_override(hash)) return o->edit;
    }
    if (const ecr::VoiceEntry* v = voices_.find(hash)) return v->edit;
    return ecr::VoiceEdit();
}

bool Studio::has_override(const std::string& hash) const { return profile_ok_ && profile_.find_override(hash) != nullptr; }

bool Studio::voice_dirty(const std::string& hash) const {
    auto it = baseline_.find(hash);
    const ecr::VoiceEntry* v = voices_.find(hash);
    bool g = v && it != baseline_.end() && v->edit != it->second;
    return g;   // overrides sao detectados pelo perfil (dirty() geral)
}

ecr::ChannelMap Studio::map_of(const std::string& key) const {
    for (auto& m : synth_.maps()) if (m.key == key) return m;
    return ecr::ChannelMap();
}

void Studio::apply_voice_to_synth(const std::string& hash) {
    ecr::ChannelMap m = map_of(hash);
    if (m.key.empty()) return;
    ecr::VoiceEdit e = effective_edit(hash);
    m.mode = e.mode; m.bank = e.bank; m.program = e.program; m.transpose = e.transpose;
    m.drum_note = e.drum_note; m.volume_db = e.volume_db;
    synth_.update_map(m);
}

void Studio::ensure_profile_file() {
    if (!profile_pending_new_) return;
    const ecr::SongNotes* sg = song();
    if (!sg) return;
    profile_.retarget(profile_target_.u8string(), sg->slug, sg->bgm_id);
    profile_pending_new_ = false;
}

void Studio::edit_voice(const std::string& hash, const ecr::VoiceEdit& e, VoiceScope scope) {
    if (scope == VoiceScope::Song) ensure_profile_file();
    UndoItem u;
    u.is_override = scope == VoiceScope::Song;
    u.hash = hash;
    u.had_override = has_override(hash);
    if (const ecr::VoiceEntry* v = voices_.find(hash)) u.before_global = v->edit;
    if (u.had_override) u.before_override = profile_.find_override(hash)->edit;
    if (scope == VoiceScope::Global) {
        voices_.set_edit(hash, e);
        if (profile_ok_ && profile_.find_override(hash)) profile_.remove_override(hash);   // o global passa a valer
    } else if (profile_ok_) {
        profile_.set_override(hash, e);
    }
    u.after_had_override = has_override(hash);
    if (const ecr::VoiceEntry* v = voices_.find(hash)) u.after_global = v->edit;
    if (u.after_had_override) u.after_override = profile_.find_override(hash)->edit;
    undo_.push_back(u); redo_.clear();
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    apply_voice_to_synth(hash);
}

void Studio::clear_override(const std::string& hash) {
    if (!has_override(hash)) return;
    UndoItem u;
    u.is_override = true; u.hash = hash; u.had_override = true;
    u.before_override = profile_.find_override(hash)->edit;
    if (const ecr::VoiceEntry* v = voices_.find(hash)) { u.before_global = v->edit; u.after_global = v->edit; }
    profile_.remove_override(hash);
    u.after_had_override = false;
    undo_.push_back(u); redo_.clear();
    apply_voice_to_synth(hash);
}

void Studio::apply_undo_item(const UndoItem& u, bool undo) {
    const ecr::VoiceEdit& g = undo ? u.before_global : u.after_global;
    bool had = undo ? u.had_override : u.after_had_override;
    const ecr::VoiceEdit& o = undo ? u.before_override : u.after_override;
    voices_.set_edit(u.hash, g);
    if (profile_ok_) {
        if (had) { ensure_profile_file(); profile_.set_override(u.hash, o); } else profile_.remove_override(u.hash);
    }
    apply_voice_to_synth(u.hash);
}

void Studio::undo() {
    if (undo_.empty()) return;
    UndoItem u = undo_.back(); undo_.pop_back();
    apply_undo_item(u, true);
    redo_.push_back(u);
}
void Studio::redo() {
    if (redo_.empty()) return;
    UndoItem u = redo_.back(); redo_.pop_back();
    apply_undo_item(u, false);
    undo_.push_back(u);
}

void Studio::edit_channel(const ecr::ProfileChannel& c) {
    ensure_profile_file();
    if (profile_ok_) profile_.set_channel(c);
    ecr::ChannelMap m = map_of(c.name);
    if (m.key.empty()) return;
    m.mode = c.mode; m.bank = c.bank; m.program = c.program; m.transpose = c.transpose; m.volume_db = c.volume_db;
    synth_.update_map(m);
}

void Studio::set_solo(const std::string& key, bool on) {
    ecr::ChannelMap m = map_of(key);
    if (m.key.empty()) return;
    m.solo = on;
    synth_.update_map(m);
}
void Studio::set_mute(const std::string& key, bool on) {
    ecr::ChannelMap m = map_of(key);
    if (m.key.empty()) return;
    m.mute = on;
    synth_.update_map(m);
}

bool Studio::saved_edit(const std::string& hash, ecr::VoiceEdit* out) const {
    auto it = baseline_.find(hash);
    if (it == baseline_.end()) return false;
    if (out) *out = it->second;
    return true;
}

bool Studio::dirty() const { return voices_.dirty() || (profile_ok_ && profile_.dirty()); }

bool Studio::save(std::string* err) {
    bool ok = true;
    if (voices_.dirty()) ok = voices_.save(err) && ok;
    if (profile_ok_ && profile_.dirty()) ok = profile_.save(err) && ok;
    if (ok) snapshot_baseline();
    return ok;
}

// ---------------------------------------------------------------------------
// audio
// ---------------------------------------------------------------------------
static void SDLCALL audio_trampoline(void* ud, Uint8* stream, int len) {
    static_cast<Studio*>(ud)->audio_callback(reinterpret_cast<float*>(stream), len / (int)(2 * sizeof(float)));
}

bool Studio::open_audio_device() {
    if (dev_id_) return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    SDL_AudioSpec want{}, have{};
    want.freq = ecr::kPreviewRate; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 1024;
    want.callback = audio_trampoline; want.userdata = this;
    dev_id_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);   // sem mudancas: a previa e sempre 48 kHz f32 estereo
    if (!dev_id_) return false;
    SDL_PauseAudioDevice(dev_id_, 0);
    return true;
}

void Studio::close_audio_device() {
    if (dev_id_) { SDL_CloseAudioDevice(dev_id_); dev_id_ = 0; }
}

void Studio::play(bool on) {
    if (on && !dev_id_ && !headless_) open_audio_device();
    if (on && synth_.finished()) synth_.seek_frames(0);
    playing_ = on;
}

void Studio::seek_seconds(double s) {
    synth_.seek_frames((int64_t)(std::max(0.0, s) * ecr::kPreviewRate));   // o PreviewSynth tem mutex proprio
}
double Studio::position_s() const { return (double)synth_.position_frames() / ecr::kPreviewRate; }

void Studio::audio_callback(float* out, int frames) {
    if (!playing_ || !synth_.has_soundfont()) { std::memset(out, 0, (size_t)frames * 2 * sizeof(float)); return; }
    if ((int)scratch_.size() < frames * 2) scratch_.assign((size_t)frames * 2, 0.f);
    const int64_t pos0 = synth_.position_frames();
    const int64_t total = std::max<int64_t>(1, synth_.total_frames());
    synth_.render_block(scratch_.data(), frames);     // avanca a posicao e alimenta os VUs mesmo em A
    const std::shared_ptr<const ecr::OriginalAudio> oa = std::atomic_load(&orig_cur_);   // troca atomica: sem travar o audio
    const float* orig = nullptr; int64_t ofr = 0;
    if (oa && !oa->stereo.empty()) { orig = oa->stereo.data(); ofr = (int64_t)oa->stereo.size() / 2; }
    const float target = (ab_original_ && orig) ? 1.f : 0.f;
    const float step = 1.f / (0.020f * ecr::kPreviewRate);   // crossfade de 20 ms
    for (int i = 0; i < frames; ++i) {
        ab_mix_ += std::max(-step, std::min(step, target - ab_mix_));
        float a0 = 0, a1 = 0;
        if (orig && ab_mix_ > 0) {
            int64_t p = pos0 + i; if (p >= total) p %= total;
            if (p < ofr) { a0 = orig[2 * p]; a1 = orig[2 * p + 1]; }
        }
        float l = scratch_[2 * i] * (1.f - ab_mix_) + a0 * ab_mix_;
        float r = scratch_[2 * i + 1] * (1.f - ab_mix_) + a1 * ab_mix_;
        if (!std::isfinite(l)) l = 0.f;   // nunca entrega NaN/Inf nem estouro ao dispositivo
        if (!std::isfinite(r)) r = 0.f;
        out[2 * i] = std::max(-1.f, std::min(1.f, l));
        out[2 * i + 1] = std::max(-1.f, std::min(1.f, r));
    }
}

void Studio::inject_original(std::vector<float> stereo) {
    auto o = std::make_shared<ecr::OriginalAudio>();
    o->stereo = std::move(stereo);
    std::atomic_store(&orig_cur_, std::shared_ptr<const ecr::OriginalAudio>(o));
}

std::string Studio::rom_problem() const {
    if (p_.run.rom.empty()) return "Nenhuma ROM configurada.";
    if (!p_.rom_valid) return "A ROM configurada nao confere com o SHA-256 da versao US (escolha a ROM correta no menu ROM).";
    return "";
}

void Studio::start_capture_original() {
    if (orig_job().running || bgm_ < 0) return;
    if (std::string rp = rom_problem(); !rp.empty()) { op_error_ = rp; return; }   // capturar o original exige ROM valida
    if (th_orig_.joinable()) th_orig_.join();
    const int id = bgm_;
    orig_ready_ = false; ab_original_ = false;
    { std::lock_guard<std::mutex> l(mu_); orig_ = JobState{true, -1, "Iniciando o jogo headless…", ""}; }
    orig_done_ = false;
    cancel_ = false;
    th_orig_ = std::thread([this, id] {
        ecr::OriginalAudio out;
        std::string err;
        bool ok = ecr::capture_original(p_.run, id, &out,
            [this](float f, const std::string& s) { std::lock_guard<std::mutex> l(mu_); orig_.frac = f; orig_.text = s; },
            [this] { return cancel_.load(); }, &err);
        {
            std::lock_guard<std::mutex> l(mu_);
            orig_.running = false;
            orig_.error = ok ? "" : (err.empty() ? "cancelado" : err);
            if (ok) orig_.frac = 1;
        }
        if (ok) {
            auto sp = std::make_shared<const ecr::OriginalAudio>(std::move(out));
            { std::lock_guard<std::mutex> l(mu_); originals_[id] = sp; }
            if (bgm_.load() == id) { std::atomic_store(&orig_cur_, sp); orig_ready_ = true; }
        }
        orig_done_ = true;
    });
}

// ---------------------------------------------------------------------------
// testar no jogo
// ---------------------------------------------------------------------------
bool Studio::test_in_game(std::string* err) {
    if (game_running()) { if (err) *err = "o jogo ja esta aberto"; return false; }
    if (bgm_ < 0) return false;
    if (std::string rp = rom_problem(); !rp.empty()) { if (err) *err = rp; return false; }   // testar no jogo exige ROM valida
    if (!save(err)) return false;   // o jogo le os arquivos salvos (e recarrega ao vivo, hot_reload)
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / "ec_studio_test";
    fs::create_directories(tmp, ec);
    std::string script = (tmp / "music_test.script").u8string();
    int fs_ = 0, fe = 0;
    const int frames = 60 * 900;   // ate 15 min tocando
    if (bgm_ <= 15) {
        if (!ecr::build_single_music_script(script, bgm_, frames, &fs_, &fe)) { if (err) *err = "script do Music Test"; return false; }
    } else {
        ecr::build_char_select_script(script, (tmp / "test.ram").u8string(), frames, &fs_, &fe);
    }
    ecr::ProcessSpec ps;
    ps.exe = p_.run.exe; ps.cwd = p_.run.work_dir;
    ps.args = {"--no-launcher", "--audio-remaster", "hybrid", "--audio-voices", p_.voices.u8string(),
               "--audio-profiles-dir", p_.profiles_dir.u8string(), "--input-script", script, p_.run.rom};
    ps.log_path = (tmp / "game.log").u8string();
    game_flag_->store(true);
    if (th_game_.joinable()) th_game_.join();
    auto flag = game_flag_;
    th_game_ = std::thread([flag, ps] {
        std::string e;
        ecr::run_process(ps, nullptr, &e);
        flag->store(false);
    });
    return true;
}

// ---------------------------------------------------------------------------
// --selftest-studio: caminho real da tela, sem janela
// ---------------------------------------------------------------------------
namespace {
double rms_db(const std::vector<float>& v) {
    if (v.empty()) return -200;
    double s = 0;
    for (float x : v) s += (double)x * x;
    s = std::sqrt(s / (double)v.size());
    return s > 1e-9 ? 20.0 * std::log10(s) : -200.0;
}
std::vector<float> render_seconds(Studio& st, double sec) {
    std::vector<float> all, blk(1024 * 2);
    int frames = (int)(sec * ecr::kPreviewRate);
    for (int done = 0; done < frames; done += 1024) {
        st.audio_callback(blk.data(), 1024);   // o MESMO callback do dispositivo SDL
        all.insert(all.end(), blk.begin(), blk.end());
    }
    return all;
}
uint64_t file_sig(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    uint64_t h = 1469598103934665603ull;
    char c;
    while (f.get(c)) { h ^= (unsigned char)c; h *= 1099511628211ull; }
    return h;
}
}  // namespace

int studio_selftest(const std::string& root_s, const std::string& dumps_s, const std::string& out_s) {
    std::string report;
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { report += std::string(ok ? "PASS " : "FAIL ") + what + "\n"; if (!ok) ++fails; };
    fs::path root = root_s.empty() ? fs::current_path() : fs::u8path(root_s);
    const bool own_out = out_s.empty();   // diretorio criado por nos (so este e apagado no fim)
    fs::path out = own_out ? fs::temp_directory_path() / ("ec_studio_selftest_" + std::to_string((unsigned long long)GetTickCount64())) : fs::u8path(out_s);
    std::error_code ec;
    if (!own_out && fs::exists(out, ec) && !fs::is_empty(out, ec)) {   // nunca remove_all num --out arbitrario
        fprintf(stderr, "--out deve ser um diretorio novo ou vazio: %s\n", out.string().c_str());
        return 2;
    }
    fs::create_directories(out / "profiles", ec);
    const fs::path real_voices = root / "assets" / "audio_remaster" / "voices.toml";
    const fs::path real_prof = root / "assets" / "audio_remaster" / "profiles";
    const uint64_t sig_before = file_sig(real_voices);
    fs::copy_file(real_voices, out / "voices.toml", fs::copy_options::overwrite_existing, ec);
    for (auto& e : fs::directory_iterator(real_prof, ec)) fs::copy_file(e.path(), out / "profiles" / e.path().filename(), fs::copy_options::overwrite_existing, ec);

    StudioPaths p;
    p.root = root; p.voices = out / "voices.toml"; p.profiles_dir = out / "profiles";
    p.sf2 = root / "assets" / "soundfonts" / "TimGM6mb.sf2";
    p.cache_dir = fs::u8path(dumps_s);
    Studio st(p);
    st.set_headless(true);
    for (int i = 0; i < 2400 && st.stage() != Studio::Stage::Ready && st.stage() != Studio::Stage::Error; ++i) { st.poll(); SDL_Delay(25); }
    check(st.stage() == Studio::Stage::Ready, "estagio Ready (dumps + voices.toml + SoundFont carregados)");
    if (st.stage() == Studio::Stage::Ready) {
        check((int)st.songs().size() == ecr::bgm_count(), "17 musicas parseadas dos dumps");
        check(st.select_song(8), "select_song(8) = Vietnam / Midknight");
        auto maps = st.maps();
        int nv = 0, nc = 0;
        for (auto& m : maps) (m.is_voice ? nv : nc)++;
        check(nv > 0, "mapas: " + std::to_string(nv) + " instrumentos FM + " + std::to_string(nc) + " canais");
        st.play(true);
        auto a = render_seconds(st, 2.0);
        double r1 = rms_db(a);
        char b[64]; snprintf(b, sizeof b, "RMS da previa (2 s) = %.1f dBFS (> -40)", r1);
        check(r1 > -40, b);

        // mudo em todas as vozes FM -> queda
        for (auto& m : maps) if (m.is_voice) st.set_mute(m.key, true);
        st.seek_seconds(0);
        auto a2 = render_seconds(st, 2.0);
        double r2 = rms_db(a2);
        snprintf(b, sizeof b, "mudo em todas as vozes FM: %.1f -> %.1f dBFS (queda >= 6 dB)", r1, r2);
        check(r2 < r1 - 6, b);
        for (auto& m : maps) if (m.is_voice) st.set_mute(m.key, false);

        // solo numa voz
        std::string first;
        for (auto& m : maps) if (m.is_voice) { first = m.key; break; }
        st.set_solo(first, true);
        st.seek_seconds(0);
        double r3 = rms_db(render_seconds(st, 2.0));
        snprintf(b, sizeof b, "solo na voz %s: %.1f dBFS (< pleno %.1f)", first.c_str(), r3, r1);
        check(r3 < r1, b);
        st.set_solo(first, false);

        // volume_db absurdo (400) -> saida finita e dentro de [-1, 1]
        {
            ecr::VoiceEdit big = st.effective_edit(first);
            big.volume_db = 400.0;
            big.transpose = 500;
            st.edit_voice(first, big, VoiceScope::Global);
            st.seek_seconds(0);
            auto cl = render_seconds(st, 1.0);
            bool finite = true; float mx = 0;
            for (float x : cl) { if (!std::isfinite(x)) finite = false; mx = std::max(mx, std::fabs(x)); }
            snprintf(b, sizeof b, "volume_db=400: saida finita e |x| <= 1 (pico %.3f)", mx);
            check(finite && mx <= 1.0f, b);
            // o ganho efetivo foi limitado a +24 dB: comparavel ao teto, nao 400 dB
            st.undo();
        }

        // A/B com um "original" sintetico (seno 440 Hz, -20 dBFS)
        {
            std::vector<float> sine((size_t)ecr::kPreviewRate * 4 * 2);
            for (size_t i = 0; i < sine.size() / 2; ++i) { float v = 0.1f * (float)std::sin(2 * 3.14159265 * 440 * (double)i / ecr::kPreviewRate); sine[2 * i] = sine[2 * i + 1] = v; }
            st.inject_original(sine);
            st.set_ab_original(true);
            st.seek_seconds(0);
            auto c = render_seconds(st, 0.5);
            std::vector<float> tail(c.begin() + (c.size() / 2), c.end());   // depois do crossfade de 20 ms
            double r4 = rms_db(tail);
            snprintf(b, sizeof b, "A (original injetado) = %.1f dBFS (esperado -23.0 +-1)", r4);
            check(std::fabs(r4 + 23.0) < 1.0, b);
            st.set_ab_original(false);
        }

        // edicao global -> salvar numa COPIA -> reler; .bak criado
        ecr::VoiceEdit e0 = st.effective_edit(first);
        ecr::VoiceEdit e1 = e0; e1.program = (e0.program + 1) % 128; e1.name = "selftest";
        st.edit_voice(first, e1, VoiceScope::Global);
        check(st.dirty() && st.can_undo(), "edit_voice marca sujo e empilha desfazer");
        std::string err;
        check(st.save(&err), "save() na copia: " + err);
        ecr::VoicesFile re;
        re.load((out / "voices.toml").u8string(), &err);
        const ecr::VoiceEntry* ve = re.find(first);
        check(ve && ve->edit.program == e1.program && ve->edit.name == "selftest", "voices.toml (copia) relido com a edicao");
        check(fs::exists(out / "voices.toml.bak", ec), "backup .bak criado");
        st.undo();
        check(st.effective_edit(first).program == e0.program, "desfazer restaura o valor");
        st.redo();
        check(st.effective_edit(first).program == e1.program, "refazer reaplica");
        // override por musica
        ecr::VoiceEdit e2 = e1; e2.volume_db = -6.0;
        st.edit_voice(first, e2, VoiceScope::Song);
        check(st.has_override(first), "override da musica criado");
        check(st.save(&err), "save() com override: " + err);
        ecr::ProfileFile pf;
        const ecr::BgmInfo* bi = ecr::bgm_by_id(8);
        pf.load((out / "profiles" / (std::string(bi->slug) + ".toml")).u8string(), &err);
        const ecr::VoiceOverride* ov = pf.find_override(first);
        check(ov && std::fabs(ov->edit.volume_db + 6.0) < 0.01, "perfil da faixa (copia) com [voices.<hash>] relido");

        // trocar de musica salva o perfil pendente; perfil inexistente nasce de default.toml SEM tocar nele
        {
            const ecr::BgmInfo* b4 = ecr::bgm_by_id(4);   // 02_salem_xavier
            fs::path np = out / "profiles" / (std::string(b4->slug) + ".toml");
            fs::remove(np, ec);
            const uint64_t def_before = file_sig(out / "profiles" / "default.toml");
            check(st.select_song(4), "select_song(4) com perfil inexistente (default.toml como molde)");
            std::string first4;
            for (auto& m : st.maps()) if (m.is_voice) { first4 = m.key; break; }
            ecr::VoiceEdit e4 = st.effective_edit(first4); e4.volume_db = -3.0;
            st.edit_voice(first4, e4, VoiceScope::Song);
            check(!fs::exists(np, ec) && st.dirty(), "edicao por musica pendente; <slug>.toml ainda nao gravado");
            check(st.select_song(8), "trocar de musica com perfil sujo");
            ecr::ProfileFile np4;
            std::string e5;
            bool rd = np4.load(np.u8string(), &e5);
            const ecr::VoiceOverride* o4 = rd ? np4.find_override(first4) : nullptr;
            check(rd && np4.track().bgm_id == 4 && o4 && std::fabs(o4->edit.volume_db + 3.0) < 0.01, "<slug>.toml criado na troca, com bgm_id=4 e o override");
            check(file_sig(out / "profiles" / "default.toml") == def_before, "default.toml NAO foi alterado");
        }
    }
    // o arquivo real nao foi tocado
    check(file_sig(real_voices) == sig_before, "voices.toml REAL intacto (assinatura igual)");
    report += fails ? "RESULT FAIL\n" : "RESULT OK\n";
    fputs(report.c_str(), stdout);
    std::ofstream(out / "selftest_studio.txt", std::ios::binary) << report;
    if (own_out) { st.close_audio_device(); std::error_code e2; fs::remove_all(out, e2); }   // so o que criamos
    return fails ? 1 : 0;
}
