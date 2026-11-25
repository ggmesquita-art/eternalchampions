// Extracao da arte da ROM em segundo plano (tools/launcher/extract_ui_assets.py) e recarga do tema.
#include "app.h"
#include "dumps.h"
#include "json.h"
#include <cstdio>
#include <fstream>
#include <windows.h>

namespace fs = std::filesystem;

static constexpr int kArtExtractorVersion = 1;   // = EXTRACTOR_VERSION do extract_ui_assets.py

bool App::art_manifest_current() const {
    JVal m;
    if (!json_parse_file(path_to_utf8(assets_dir / "manifest.json"), m) || (int)m.n("schema") != 1) return false;
    if ((int)m.n("extractor_version") != kArtExtractorVersion) return false;
    const JVal* rom = m.get("rom");
    return rom && !rom_sha.empty() && rom->s("sha256") == rom_sha;
}

static bool find_python(std::wstring& exe, std::wstring& prefix_args) {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = (DWORD)(sizeof buf / sizeof buf[0]);
    if (SearchPathW(nullptr, L"python.exe", nullptr, n, buf, nullptr)) {
        std::wstring p = buf;
        // o stub da Microsoft Store (WindowsApps) so abre a loja: nao serve
        if (p.find(L"\\WindowsApps\\") == std::wstring::npos) { exe = p; prefix_args.clear(); return true; }
    }
    if (SearchPathW(nullptr, L"py.exe", nullptr, n, buf, nullptr)) { exe = buf; prefix_args = L" -3"; return true; }
    return false;
}

void App::maybe_extract_art() {
    if (shot_mode || art_state == ArtState::Running || !rom_valid || !rom_exists) return;
    if (art_manifest_current()) return;
    std::error_code ec;
    fs::path script = root_dir / "tools" / "launcher" / "extract_ui_assets.py";
    if (!fs::exists(script, ec)) { art_state = ArtState::NoScript; return; }
    std::wstring py, pre;
    if (!find_python(py, pre)) { art_state = ArtState::NoPython; return; }
    fs::create_directories(logs_dir, ec);
    fs::create_directories(assets_dir, ec);
    std::wstring cmd = L"\"" + py + L"\"" + pre + L" \"" + script.wstring() + L"\" --rom \"" + utf8_to_path(rom_path).wstring() +
                       L"\" --exe \"" + game_exe.wstring() + L"\" --out \"" + assets_dir.wstring() + L"\"";
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE logh = CreateFileW((logs_dir / "extract_ui.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    std::wstring cwd = root_dir.wstring();
    unsigned long le = 0;
    bool ok = ecr::spawn_process_w(nullptr, buf.data(), cwd.c_str(), nullptr, CREATE_NO_WINDOW, logh, &pi, &le);
    if (logh != INVALID_HANDLE_VALUE) CloseHandle(logh);
    if (!ok) { art_state = ArtState::Failed; log("extrator de arte: CreateProcess falhou"); return; }
    CloseHandle(pi.hThread);
    art_proc_ = pi.hProcess;
    art_state = ArtState::Running;
    art_progress.clear();
    log("extrator de arte iniciado: " + path_to_utf8(fs::path(cmd)));
}

void App::poll_art(float dt) {
    if (art_state != ArtState::Running || !art_proc_) return;
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
    if (WaitForSingleObject((HANDLE)art_proc_, 0) != WAIT_OBJECT_0) return;
    DWORD code = 1;
    GetExitCodeProcess((HANDLE)art_proc_, &code);
    CloseHandle((HANDLE)art_proc_);
    art_proc_ = nullptr;
    log("extrator de arte terminou: codigo " + std::to_string((int)code));
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
