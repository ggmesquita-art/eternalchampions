#include "preview_job.h"
#include "app.h"
#include "dumps.h"
#include "os.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

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

static int64_t mtime_of(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return 0;
    return (int64_t)fs::last_write_time(p, ec).time_since_epoch().count();
}

// arquivos externos que afetam a imagem: exe do jogo, pesos B+ (.bin/.onnx), preset .glslp e seus shaders, manifesto do Modo C
static std::string deps_signature(App& app) {
    std::ostringstream o;
    o << "exe=" << mtime_of(app.game_exe) << ";";
    std::string model = app.cfg.get("ec.bplus.model");
    static const struct { const char* n; const char* f; } mm[] = {{"v3_fast", "ec_bplus_v3_fast_weights.bin"}, {"v3_hq", "ec_bplus_v3_hq_weights.bin"},
        {"v3_hq_janai", "ec_bplus_v3_hq_janai_weights.bin"}, {"v2", "ec_bplus_v2_flux_weights.bin"}, {"v1", "ec_bplus_espcn4x_weights.bin"}};
    fs::path mp = utf8_to_path(model);
    for (auto& e : mm) if (model == e.n) mp = app.root_dir / "assets" / "models" / e.f;
    if (!mp.is_absolute()) mp = app.root_dir / mp;
    o << "model=" << mtime_of(mp) << ";";
    { std::string onnx = path_to_utf8(mp); size_t k = onnx.rfind("_weights.bin"); if (k != std::string::npos) o << "onnx=" << mtime_of(utf8_to_path(onnx.substr(0, k) + ".onnx")) << ";"; }
    std::string preset = app.cfg.get("ec.video.shader_preset");
    if (!preset.empty() && preset != "off") {
        fs::path pp = utf8_to_path(preset);
        if (!(pp.has_extension() && fs::exists(pp))) pp = app.root_dir / "assets" / "shaders" / "presets" / utf8_to_path(preset + ".glslp");
        o << "glslp=" << mtime_of(pp) << ";";
        std::ifstream f(pp);
        std::string line;
        while (std::getline(f, line)) {
            size_t eq = line.find('=');
            if (eq == std::string::npos || line.rfind("shader", 0) != 0 || line.size() < 7 || !isdigit((unsigned char)line[6])) continue;
            std::string v = line.substr(eq + 1);
            v.erase(std::remove(v.begin(), v.end(), '"'), v.end());
            size_t a = v.find_first_not_of(" \t\r"), b = v.find_last_not_of(" \t\r");
            if (a == std::string::npos) continue;
            o << "sh=" << mtime_of(pp.parent_path() / utf8_to_path(v.substr(a, b - a + 1))) << ";";
        }
    }
    std::string hd = app.cfg.get("ec.hd.manifest");
    if (!hd.empty()) {
        fs::path hp = hd == "animesharp" ? app.root_dir / "pipeline" / "manifest.json" : hd == "flux" ? app.root_dir / "pipeline" / "manifest_flux.json" : utf8_to_path(hd);
        o << "hd=" << mtime_of(hp) << ";";
    }
    return o.str();
}

std::string PreviewJob::current_key(App& app) const {
    static const char* ids[] = {"ec.bplus.enabled", "ec.bplus.model", "ec.bplus.backend", "ec.bplus.dedither", "ec.bplus.intensity",
                                "ec.bplus.budget_ms", "ec.hd.manifest", "ec.hd.mode", "ec.video.shader_preset", "ec.video.shader_params",
                                "mods.widescreen.enabled", "mods.widescreen.aspect", "video.linear_filter", "video.internal_scale"};
    std::string s;
    for (const char* id : ids) s += std::string(id) + "=" + app.cfg.get(id) + ";";
    s += "args.widescreen=" + app.cfg.get("args.widescreen") + ";args.interlace=" + app.cfg.get("args.interlace") + ";";
    s += deps_signature(app);
    s += "rom=" + app.rom_sha;
    uint64_t h = 1469598103934665603ull;   // FNV-1a 64
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    char b[24];
    snprintf(b, sizeof b, "%016llx", (unsigned long long)h);
    return b;
}

// cache valido = _orig.png, _enh.png e _kind.txt (o _kind.txt e escrito por ultimo)
static bool cache_complete(const fs::path& d, const std::string& key) {
    std::error_code ec;
    return fs::exists(d / (key + "_orig.png"), ec) && fs::exists(d / (key + "_enh.png"), ec) && fs::exists(d / (key + "_kind.txt"), ec);
}

bool PreviewJob::cached(App& app) const { return cache_complete(cache_dir(app), current_key(app)); }

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
    if (cache_complete(d, key)) {   // cache
        load(app, key);
        state_ = State::Done; msg_ = "Pré-visualização carregada do cache.";
        return;
    }
    if (!app.ini_override.empty()) { state_ = State::Failed; msg_ = "Indisponível com --ini / cópia de teste: o jogo lê o settings.ini real (use --real-ini)."; return; }
    if (!app.rom_valid || !app.rom_exists) { state_ = State::Failed; msg_ = "Configure uma ROM válida primeiro."; return; }
    if (!fs::exists(app.game_exe, ec)) { state_ = State::Failed; msg_ = os::game_exe_name() + " não encontrado."; return; }
    app.cfg.save();   // o jogo da previa le o settings.ini: grava mudancas adiadas antes
    fs::create_directories(d, ec);
    if (th_.joinable()) th_.join();
    state_ = State::Running; msg_ = "Rodando o jogo até a abertura (≈ 10–25 s)…"; elapsed_ = 0;
    done_ = false; cancel_ = false; rc_ = 0; pending_key_ = key;

    // script: boot -> abertura (frame 1236), screenshot nativo, sai
    fs::path script = d / (key + "_script.txt");
    {
        std::ofstream s(script, std::ios::binary);
        s << "WAIT 800\nHOLD START\nWAIT 15\nRELEASE\nWAIT 85\nHOLD START\nWAIT 15\nRELEASE\nWAIT 320\n";
        s << "SCREENSHOT " << fwd(d / (key + "_orig.png.tmp")) << "\nWAIT 30\nEXIT 0\n";
    }
    ecr::ProcessSpec ps;
    ps.exe = path_to_utf8(app.game_exe);
    ps.cwd = path_to_utf8(app.root_dir);
    ps.args = {"--no-launcher", "--runtime-uncapped", "--measure-runtime", "1500", "--input-script", path_to_utf8(script), app.rom_path};
    {   // opcoes de [launcher.args] que mudam a imagem
        std::string ws = app.cfg.get("args.widescreen"), il = app.cfg.get("args.interlace");
        if (!ws.empty()) { ps.args.insert(ps.args.begin() + 1, "--widescreen"); ps.args.insert(ps.args.begin() + 2, ws); }
        if (!il.empty()) { ps.args.insert(ps.args.begin() + 1, "--interlace-display"); ps.args.insert(ps.args.begin() + 2, il); }
    }
    // capturas gravadas em .tmp e renomeadas so quando o jogo terminou (sem PNG pela metade no cache)
    ps.env = {{"SDL_AUDIODRIVER", "dummy"},
              {"EC_SHADER_CAPTURE", path_to_utf8(d / (key + "_shader.png.tmp"))}, {"EC_SHADER_CAPTURE_AT", "1236"},
              {"EC_BPLUS_CAPTURE", path_to_utf8(d / (key + "_bplus.png.tmp"))}, {"EC_BPLUS_CAPTURE_AT", "1236"}};
    ps.log_path = path_to_utf8(d / (key + "_game.log"));
    timed_out_ = false;
    th_ = std::thread([this, ps] {
        std::string err;
        const auto t0 = std::chrono::steady_clock::now();
        rc_ = ecr::run_process(ps, [this, t0] {
            if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(90)) { timed_out_ = true; return true; }   // timeout 90 s: TerminateProcess
            return cancel_.load();
        }, &err);
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
    auto promote = [&](const fs::path& final_p) {   // .tmp -> final (so se o jogo terminou bem)
        fs::path t = final_p; t += ".tmp";
        if (fs::exists(t, ec)) { fs::rename(t, final_p, ec); }
    };
    if (timed_out_ || rc_ == -2) {
        for (const char* n : {"_orig.png", "_shader.png", "_bplus.png"}) { fs::remove(d / (key + n + std::string(".tmp")), ec); }
        state_ = State::Failed;
        msg_ = timed_out_ ? "Tempo esgotado (90 s): o jogo foi encerrado." : "Cancelado.";
        return;
    }
    promote(orig); promote(sh); promote(bp);
    if (!fs::exists(orig, ec)) {
        state_ = State::Failed;
        msg_ = "O jogo não gerou a captura (veja " + path_to_utf8(d / (key + "_game.log")) + ").";
        return;
    }
    std::string kind = "nativa";
    if (fs::exists(sh, ec) && fs::file_size(sh, ec) > 0) { fs::rename(sh, enh, ec); kind = app.cfg.get("ec.bplus.enabled") == "1" ? "shader + B+" : "shader"; }
    else if (fs::exists(bp, ec) && fs::file_size(bp, ec) > 0) { fs::rename(bp, enh, ec); kind = "B+"; }
    else fs::copy_file(orig, enh, fs::copy_options::overwrite_existing, ec);   // sem aprimoramentos ativos
    fs::remove(sh, ec); fs::remove(bp, ec); fs::remove(d / (key + "_script.txt"), ec);
    { std::ofstream k(d / (key + "_kind.txt")); k << kind << "\n"; }   // por ultimo: marca o cache como completo
    load(app, key);
    state_ = State::Done;
    msg_ = kind == "nativa" ? "Nenhum aprimoramento ativo: as duas imagens são iguais." : "Pronto.";
}
