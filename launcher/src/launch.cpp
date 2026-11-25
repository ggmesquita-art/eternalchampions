#include "launch.h"
#include <fstream>
#include <shellapi.h>
#include <sstream>

namespace fs = std::filesystem;

static std::string trim_ws(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

bool ensure_skip_launcher(const fs::path& ini) {
    std::vector<std::string> lines;
    bool crlf = false;
    {
        std::ifstream f(ini, std::ios::binary);
        std::string l;
        while (std::getline(f, l)) {
            if (!l.empty() && l.back() == '\r') { l.pop_back(); crlf = true; }
            lines.push_back(l);
        }
    }
    int sec_line = -1, key_line = -1;
    bool in_launcher = false;
    for (int i = 0; i < (int)lines.size(); ++i) {
        std::string t = trim_ws(lines[i]);
        if (!t.empty() && t[0] == '[') {
            in_launcher = (t == "[launcher]");
            if (in_launcher && sec_line < 0) sec_line = i;
            continue;
        }
        if (in_launcher && key_line < 0) {
            size_t eq = t.find('=');
            if (eq != std::string::npos && t[0] != '#' && t[0] != ';' && trim_ws(t.substr(0, eq)) == "skip_launcher") {
                if (trim_ws(t.substr(eq + 1)) == "1") return true;   // ja esta ok: nao reescreve
                key_line = i;
            }
        }
    }
    if (key_line >= 0) lines[key_line] = "skip_launcher = 1";
    else if (sec_line >= 0) lines.insert(lines.begin() + sec_line + 1, "skip_launcher = 1");
    else { if (!lines.empty() && !trim_ws(lines.back()).empty()) lines.push_back(""); lines.push_back("[launcher]"); lines.push_back("skip_launcher = 1"); }
    std::ofstream o(ini, std::ios::binary | std::ios::trunc);
    if (!o) return false;
    for (auto& l : lines) o << l << (crlf ? "\r\n" : "\n");
    return (bool)o;
}

bool start_game(App& app, GameProcess& gp, std::string& err) {
    std::error_code ec;
    if (!fs::exists(app.game_exe, ec)) { err = "EternalChampionsRecomp.exe nao encontrado em " + path_to_utf8(app.exe_dir); return false; }
    if (app.rom_path.empty() || !fs::exists(utf8_to_path(app.rom_path), ec)) { err = "ROM nao configurada ou inexistente"; return false; }
    if (!ensure_skip_launcher(app.settings_ini)) { err = "nao foi possivel gravar settings.ini"; return false; }

    std::wstring cmd = L"\"" + app.game_exe.wstring() + L"\" --no-launcher \"" + utf8_to_path(app.rom_path).wstring() + L"\"";
    if (!app.extra_game_args.empty()) cmd += L" " + utf8_to_path(app.extra_game_args).wstring();
    std::wstring cwd = app.root_dir.wstring();   // jogo resolve "assets/..." e "../assets/..." relativo ao cwd
    STARTUPINFOW si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    if (!CreateProcessW(app.game_exe.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, cwd.c_str(), &si, &pi)) {
        err = "CreateProcess falhou (erro " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    gp.handle = pi.hProcess;
    gp.pid = pi.dwProcessId;
    return true;
}

void shell_open(const fs::path& p) {
    ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

fs::path find_last_error_log(App& app) {
    std::error_code ec;
    for (fs::path c : {app.logs_dir / "last_error.log", app.exe_dir / "last_error.log", app.root_dir / "last_error.log"})
        if (fs::exists(c, ec)) return c;
    return fs::path();
}
