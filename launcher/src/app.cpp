#include "app.h"
#include "sha256.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <shlobj.h>
#include <windows.h>

namespace fs = std::filesystem;

std::string path_to_utf8(const fs::path& p) {
    std::wstring w = p.wstring();
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

fs::path utf8_to_path(const std::string& s) {
    if (s.empty()) return fs::path();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return fs::path(w);
}

bool App::init(bool hidden, int win_w, int win_h) {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
    exe_dir = fs::path(std::wstring(buf, n)).parent_path();
    std::error_code ec;
    fs::path parent = exe_dir.parent_path();
    root_dir = fs::exists(parent / "assets", ec) ? parent : exe_dir;
    game_exe = exe_dir / "EternalChampionsRecomp.exe";
    settings_ini = ini_override.empty() ? exe_dir / "settings.ini" : utf8_to_path(ini_override);
    rom_cfg = exe_dir / "rom-EC.cfg";
    recent_file = exe_dir / "launcher_recent.txt";
    assets_dir = exe_dir / "ui_cache";
    {   // arte extraida: %APPDATA%\EternalChampionsRecomp\ui (o extrator recebe --out explicito)
        PWSTR roaming = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)) && roaming) {
            assets_dir = fs::path(roaming) / "EternalChampionsRecomp" / "ui";
            CoTaskMemFree(roaming);
        }
    }
    logs_dir = exe_dir / "logs";

    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) return false;
    if (!render.init(win_w, win_h, hidden, "Eternal Champions")) return false;
    if (!theme.init(render, path_to_utf8(assets_dir))) return false;
    input.init();
    cfg.load(settings_ini);
    load_rom_state();
    maybe_extract_art();
    return true;
}

void App::shutdown() {
    stack_.clear();
    pending_.clear();
    preview.cancel();
    preview.free_textures();
    input.shutdown();
    theme.shutdown();
    theme.fonts = Fonts{};
    render.shutdown();
    SDL_Quit();
}

// ---- pilha de telas ---------------------------------------------------------
void App::push(std::unique_ptr<Screen> s) { pending_.push_back({Pending::Push, std::move(s)}); }
void App::pop() { pending_.push_back({Pending::Pop, nullptr}); }
void App::replace(std::unique_ptr<Screen> s) { pending_.push_back({Pending::Replace, std::move(s)}); }
void App::reset_to(std::unique_ptr<Screen> s) { pending_.push_back({Pending::Reset, std::move(s)}); }

void App::apply_pending() {
    for (auto& p : pending_) {
        switch (p.k) {
        case Pending::Push:
            stack_.push_back(std::move(p.s)); stack_.back()->on_enter(*this); fade = 0.15f; break;
        case Pending::Pop:
            if (!stack_.empty()) stack_.pop_back();
            if (stack_.empty()) quit = true; else stack_.back()->on_resume(*this);
            fade = 0.15f; break;
        case Pending::Replace:
            if (!stack_.empty()) stack_.pop_back();
            stack_.push_back(std::move(p.s)); stack_.back()->on_enter(*this); fade = 0.15f; break;
        case Pending::Reset:
            stack_.clear(); stack_.push_back(std::move(p.s)); stack_.back()->on_enter(*this); fade = 0.15f; break;
        }
    }
    pending_.clear();
}

void App::update(const InputState& in, float dt) {
    poll_art(dt);
    preview.poll(*this, dt);
    if (toast_t_ > 0) toast_t_ -= dt;
    if (fade > 0) fade = std::max(0.f, fade - dt);
    if (!stack_.empty()) stack_.back()->update(*this, in, dt);
    apply_pending();
}

void App::draw() {
    theme.time = time;
    size_t first = stack_.size();
    while (first > 0) {
        --first;
        if (!stack_[first]->transparent()) break;
    }
    for (size_t i = first; i < stack_.size(); ++i) stack_[i]->draw(*this);
    if (toast_t_ > 0 && !toast_msg_.empty()) {
        const Font& f = *theme.fonts.help;
        float w = f.measure(toast_msg_) + 40;
        Rect r{CANVAS_W / 2 - w / 2, 610, w, 40};
        render.fill(r, Color::hex(0x07111f, 225));
        render.line_rect(r, Color::hex(0xf9d64a), 2);
        TextStyle st = theme.help_style(); st.align = Align::Center;
        draw_text(render, f, CANVAS_W / 2.f, r.y + 8, toast_msg_, st);
    }
    if (art_state == ArtState::Running) {   // indicador discreto de progresso
        const Font& f = *theme.fonts.help;
        std::string msg = "Extraindo a arte do jogo da sua ROM…  " + art_progress.substr(0, 70);
        float w = std::min(1100.f, f.measure(msg) + 80);
        Rect r{20, CANVAS_H - 84, w, 36};
        render.fill(r, Color::hex(0x07111f, 215));
        render.line_rect(r, Color::hex(0x6aa9b0), 1);
        theme.draw_yinyang(render, r.x + 20, r.y + 18, 24, time * 240.0);
        draw_text(render, f, r.x + 42, r.y + 7, msg, theme.help_style());
    }
    if (fade > 0) render.fill({0, 0, CANVAS_W, CANVAS_H}, Color::hex(0x000000, (uint8_t)(255 * fade / 0.15f)));
}

// ---- ROM --------------------------------------------------------------------
static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

void App::scan_roms() {
    detected.clear();
    std::error_code ec;
    fs::path dir = root_dir / "roms";
    if (!fs::is_directory(dir, ec)) return;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::wstring ext = e.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext == L".gen" || ext == L".md" || ext == L".bin" || ext == L".smd")
            detected.push_back(path_to_utf8(fs::absolute(e.path(), ec)));
    }
}

void App::save_recents() {
    std::ofstream f(recent_file, std::ios::binary | std::ios::trunc);
    for (auto& r : recents) f << r << "\n";
}

void App::load_rom_state() {
    rom_path.clear(); rom_sha.clear(); rom_exists = rom_valid = false;
    recents.clear();
    scan_roms();
    {
        std::ifstream f(recent_file, std::ios::binary);
        std::string line;
        while (std::getline(f, line)) { line = trim(line); if (!line.empty() && recents.size() < 8) recents.push_back(line); }
    }
    std::ifstream f(rom_cfg, std::ios::binary);
    std::string line;
    if (f && std::getline(f, line)) {
        line = trim(line);
        if (!line.empty()) set_rom(line, false);
    }
}

bool App::set_rom(const std::string& p, bool write_cfg) {
    rom_path = p;
    std::error_code ec;
    fs::path fp = utf8_to_path(p);
    rom_exists = fs::is_regular_file(fp, ec);
    rom_sha = rom_exists ? sha256_file_hex(fp) : "";
    rom_valid = (rom_sha == EC_US_ROM_SHA256);
    if (write_cfg && rom_exists && !shot_mode) {
        // formato do runner (rom_cfg_write): uma linha com o caminho absoluto
        fs::path abs = fs::absolute(fp, ec);
        std::string a = path_to_utf8(abs.empty() ? fp : abs);
        rom_path = a;
        std::ofstream f(rom_cfg, std::ios::binary | std::ios::trunc);
        f << a << "\n";
        recents.erase(std::remove(recents.begin(), recents.end(), a), recents.end());
        recents.insert(recents.begin(), a);
        if (recents.size() > 8) recents.resize(8);
        save_recents();
        if (!f) return false;
    }
    return rom_exists;
}

std::string App::rom_display_name() const {
    if (rom_path.empty()) return "";
    return path_to_utf8(utf8_to_path(rom_path).filename());
}

void App::log(const std::string& line) {
    std::error_code ec;
    std::filesystem::create_directories(logs_dir, ec);
    std::ofstream f(logs_dir / "launcher.log", std::ios::binary | std::ios::app);
    SYSTEMTIME t; GetLocalTime(&t);
    char ts[40];
    snprintf(ts, sizeof ts, "%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    f << ts << line << "\n";
}
