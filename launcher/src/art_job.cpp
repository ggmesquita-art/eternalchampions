// Extracao da arte da ROM em segundo plano (tools/launcher/extract_ui_assets.py) e recarga do tema.
#include "app.h"
#include "dumps.h"
#include "json.h"
#include <cstdio>
#include <fstream>

namespace fs = std::filesystem;

static constexpr int kArtExtractorVersion = 1;   // = EXTRACTOR_VERSION do extract_ui_assets.py

bool App::art_manifest_current() const {
    JVal m;
    if (!json_parse_file(path_to_utf8(assets_dir / "manifest.json"), m) || (int)m.n("schema") != 1) return false;
    if ((int)m.n("extractor_version") != kArtExtractorVersion) return false;
    const JVal* rom = m.get("rom");
    return rom && !rom_sha.empty() && rom->s("sha256") == rom_sha;
}

// Interpretador Python: Windows -> python.exe real (nao o stub da Loja) ou "py -3"; POSIX -> python3 / python.
static bool find_python(std::string& exe, std::vector<std::string>& prefix_args) {
    prefix_args.clear();
#ifdef _WIN32
    std::string p = ecr::find_in_path("python.exe");
    // o stub da Microsoft Store (WindowsApps) so abre a loja: nao serve
    if (!p.empty() && p.find("\\WindowsApps\\") == std::string::npos) { exe = p; return true; }
    p = ecr::find_in_path("py.exe");
    if (!p.empty()) { exe = p; prefix_args = {"-3"}; return true; }
    return false;
#else
    for (const char* n : {"python3", "python"}) {
        std::string p = ecr::find_in_path(n);
        if (!p.empty()) { exe = p; return true; }
    }
    return false;
#endif
}

void App::maybe_extract_art() {
    if (shot_mode || art_state == ArtState::Running || !rom_valid || !rom_exists) return;
    if (art_manifest_current()) return;
    std::error_code ec;
    fs::path script = root_dir / "tools" / "launcher" / "extract_ui_assets.py";
    if (!fs::exists(script, ec)) { art_state = ArtState::NoScript; return; }
    ecr::SpawnOptions so;
    if (!find_python(so.exe, so.args)) { art_state = ArtState::NoPython; return; }
    fs::create_directories(logs_dir, ec);
    fs::create_directories(assets_dir, ec);
    for (const std::string& x : {path_to_utf8(script), std::string("--rom"), rom_path, std::string("--exe"), path_to_utf8(game_exe),
                                 std::string("--out"), path_to_utf8(assets_dir)})
        so.args.push_back(x);
    so.cwd = path_to_utf8(root_dir);
    so.log_path = path_to_utf8(logs_dir / "extract_ui.log");
    so.no_window = true;
    std::string err;
    if (!ecr::spawn(so, art_proc_, &err)) { art_state = ArtState::Failed; log("extrator de arte: " + err); return; }
    art_state = ArtState::Running;
    art_progress.clear();
    std::string cmd = ecr::quote_arg(so.exe);
    for (const std::string& x : so.args) cmd += " " + ecr::quote_arg(x);
    log("extrator de arte iniciado: " + cmd);
}

void App::poll_art(float dt) {
    if (art_state != ArtState::Running || !art_proc_.valid()) return;
    art_poll_t_ -= dt;
    if (art_poll_t_ <= 0) {
        art_poll_t_ = 0.5f;
        std::ifstream f(logs_dir / "extract_ui.log", std::ios::binary);
        std::string line, last;
        while (std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            if (!line.empty()) last = line;
        }
        size_t p = last.find("] ");
        art_progress = (last.rfind("[extract_ui]", 0) == 0 && p != std::string::npos) ? last.substr(p + 2) : last;
    }
    int code = 1;
    if (!ecr::try_wait(art_proc_, &code)) return;
    log("extrator de arte terminou: codigo " + std::to_string(code));
    if (code == 0 && theme.reload_art(render, path_to_utf8(assets_dir))) { art_state = ArtState::Done; toast("Arte do jogo carregada."); }
    else { art_state = ArtState::Failed; toast("Falha ao extrair a arte (veja build\\logs\\extract_ui.log)."); }
}

std::string App::art_status() const {
    switch (art_state) {
    case ArtState::Running: return "Extraindo a arte do jogo da sua ROM em segundo plano…";
    case ArtState::NoPython: return "Tema procedural em uso: Python não encontrado no PATH (necessário para extrair a arte da ROM).";
    case ArtState::NoScript: return "Tema procedural em uso: tools/launcher/extract_ui_assets.py não encontrado.";
    case ArtState::Failed: return "Tema procedural em uso: a extração da arte falhou (build\\logs\\extract_ui.log).";
    default: break;
    }
    return theme.has_art() ? "Arte extraída da sua ROM em uso (pedra, logo, cursor e fonte do jogo)."
                           : "Tema procedural em uso (sem ROM válida para extrair a arte).";
}
