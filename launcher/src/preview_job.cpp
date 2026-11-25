#include "preview_job.h"
#include "app.h"
#include "dumps.h"
#include <cstdio>
#include <fstream>

namespace fs = std::filesystem;

static fs::path cache_dir(App& app) { return app.assets_dir.parent_path() / "preview_cache"; }

static std::string fwd(const fs::path& p) {
    std::string s = path_to_utf8(p);
    for (char& c : s) if (c == '\\') c = '/';
    return s;
}

PreviewJob::~PreviewJob() {
    cancel_ = true;
    if (th_.joinable()) th_.join();
    free_textures();
}

void PreviewJob::free_textures() {
    if (orig_) { SDL_DestroyTexture(orig_); orig_ = nullptr; }
    if (enh_) { SDL_DestroyTexture(enh_); enh_ = nullptr; }
    shown_key_.clear();
}

std::string PreviewJob::current_key(App& app) const {
    static const char* ids[] = {"ec.bplus.enabled", "ec.bplus.model", "ec.bplus.backend", "ec.bplus.dedither", "ec.bplus.intensity",
                                "ec.bplus.budget_ms", "ec.hd.manifest", "ec.hd.mode", "ec.video.shader_preset", "ec.video.shader_params",
                                "mods.widescreen.enabled", "mods.widescreen.aspect", "video.linear_filter", "video.internal_scale"};
    std::string s;
    for (const char* id : ids) s += std::string(id) + "=" + app.cfg.get(id) + ";";
    s += "rom=" + app.rom_sha;
    uint64_t h = 1469598103934665603ull;   // FNV-1a 64
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    char b[24];
    snprintf(b, sizeof b, "%016llx", (unsigned long long)h);
    return b;
}

bool PreviewJob::cached(App& app) const {
    std::error_code ec;
    return fs::exists(cache_dir(app) / (current_key(app) + "_orig.png"), ec);
}

void PreviewJob::load(App& app, const std::string& key) {
    free_textures();
    fs::path d = cache_dir(app);
    orig_ = app.render.load_png_texture(path_to_utf8(d / (key + "_orig.png")));
    enh_ = app.render.load_png_texture(path_to_utf8(d / (key + "_enh.png")), &enh_w_, &enh_h_);
    if (orig_) SDL_SetTextureScaleMode(orig_, SDL_ScaleModeNearest);
    std::ifstream k(d / (key + "_kind.txt"));
    std::getline(k, enh_kind_);
    shown_key_ = key;
}

void PreviewJob::start(App& app) {
    if (state_ == State::Running) return;
    std::string key = current_key(app);
    std::error_code ec;
    fs::path d = cache_dir(app);
    if (fs::exists(d / (key + "_orig.png"), ec)) {   // cache
        load(app, key);
        state_ = State::Done; msg_ = "Pré-visualização carregada do cache.";
        return;
    }
    if (!app.rom_valid || !app.rom_exists) { state_ = State::Failed; msg_ = "Configure uma ROM válida primeiro."; return; }
    if (!fs::exists(app.game_exe, ec)) { state_ = State::Failed; msg_ = "EternalChampionsRecomp.exe não encontrado."; return; }
    fs::create_directories(d, ec);
    if (th_.joinable()) th_.join();
    state_ = State::Running; msg_ = "Rodando o jogo até a abertura (≈ 10–25 s)…"; elapsed_ = 0;
    done_ = false; cancel_ = false; rc_ = 0; pending_key_ = key;

    // script: boot -> abertura (frame 1236), screenshot nativo, sai
    fs::path script = d / (key + "_script.txt");
    {
        std::ofstream s(script, std::ios::binary);
        s << "WAIT 800\nHOLD START\nWAIT 15\nRELEASE\nWAIT 85\nHOLD START\nWAIT 15\nRELEASE\nWAIT 320\n";
        s << "SCREENSHOT " << fwd(d / (key + "_orig.png")) << "\nWAIT 30\nEXIT 0\n";
    }
    ecr::ProcessSpec ps;
    ps.exe = path_to_utf8(app.game_exe);
    ps.cwd = path_to_utf8(app.root_dir);
    ps.args = {"--no-launcher", "--runtime-uncapped", "--measure-runtime", "1500", "--fresh-evidence", "--input-script", path_to_utf8(script), app.rom_path};
    ps.env = {{"SDL_AUDIODRIVER", "dummy"},
              {"EC_SHADER_CAPTURE", path_to_utf8(d / (key + "_shader.png"))}, {"EC_SHADER_CAPTURE_AT", "1236"},
              {"EC_BPLUS_CAPTURE", path_to_utf8(d / (key + "_bplus.png"))}, {"EC_BPLUS_CAPTURE_AT", "1236"}};
    ps.log_path = path_to_utf8(d / (key + "_game.log"));
    th_ = std::thread([this, ps] {
        std::string err;
        rc_ = ecr::run_process(ps, [this] { return cancel_.load(); }, &err);
        done_ = true;
    });
    app.log("preview: iniciado " + key);
}

void PreviewJob::poll(App& app, float dt) {
    if (state_ != State::Running) return;
    elapsed_ += dt;
    if (!done_) return;
    if (th_.joinable()) th_.join();
    fs::path d = cache_dir(app);
    std::error_code ec;
    const std::string& key = pending_key_;
    fs::path orig = d / (key + "_orig.png"), sh = d / (key + "_shader.png"), bp = d / (key + "_bplus.png"), enh = d / (key + "_enh.png");
    if (!fs::exists(orig, ec)) {
        state_ = State::Failed;
        msg_ = rc_ == -2 ? "Cancelado." : "O jogo não gerou a captura (veja " + path_to_utf8(d / (key + "_game.log")) + ").";
        return;
    }
    std::string kind = "nativa";
    if (fs::exists(sh, ec) && fs::file_size(sh, ec) > 0) { fs::rename(sh, enh, ec); kind = app.cfg.get("ec.bplus.enabled") == "1" ? "shader + B+" : "shader"; }
    else if (fs::exists(bp, ec) && fs::file_size(bp, ec) > 0) { fs::rename(bp, enh, ec); kind = "B+"; }
    else fs::copy_file(orig, enh, fs::copy_options::overwrite_existing, ec);   // sem aprimoramentos ativos
    fs::remove(sh, ec); fs::remove(bp, ec); fs::remove(d / (key + "_script.txt"), ec);
    { std::ofstream k(d / (key + "_kind.txt")); k << kind << "\n"; }
    load(app, key);
    state_ = State::Done;
    msg_ = kind == "nativa" ? "Nenhum aprimoramento ativo: as duas imagens são iguais." : "Pronto.";
}
