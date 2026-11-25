#include "launch.h"
#include <fstream>
#include <shellapi.h>
#include <sstream>

namespace fs = std::filesystem;

bool start_game(App& app, GameProcess& gp, std::string& err) {
    std::error_code ec;
    if (!fs::exists(app.game_exe, ec)) { err = "EternalChampionsRecomp.exe nao encontrado em " + path_to_utf8(app.exe_dir); return false; }
    if (app.rom_path.empty() || !fs::exists(utf8_to_path(app.rom_path), ec)) { err = "ROM nao configurada ou inexistente"; return false; }
    app.cfg.ensure_skip_launcher();   // o launcher generico do runner nunca deve aparecer
    if (!app.cfg.save()) { err = "nao foi possivel gravar settings.ini"; return false; }

    std::wstring cmd = L"\"" + app.game_exe.wstring() + L"\" --no-launcher \"" + utf8_to_path(app.rom_path).wstring() + L"\"";
    // argv = --no-launcher <rom> + flags de [launcher.args] (+ extras de teste); tudo o mais vai pelo ini
    auto quote = [](const std::string& a) {
        std::wstring w = utf8_to_path(a).wstring();
        return w.find_first_of(L" 	\"") == std::wstring::npos ? w : L"\"" + w + L"\"";
    };
    for (const std::string& a : app.cfg.build_args()) cmd += L" " + quote(a);
    if (!app.extra_game_args.empty()) cmd += L" " + utf8_to_path(app.extra_game_args).wstring();
    if (!app.load_slot_arg.empty()) cmd += L" --load-slot " + utf8_to_path(app.load_slot_arg).wstring();
    std::wstring cwd = app.root_dir.wstring();   // jogo resolve "assets/..." e "../assets/..." relativo ao cwd
    STARTUPINFOW si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    // stdout/stderr do jogo -> build/logs/game_last.log (para conferir [BPLUS_GL], [SHADER], [EC-AUDIO]...)
    fs::create_directories(app.logs_dir, ec);
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE logh = CreateFileW((app.logs_dir / "game_last.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logh != INVALID_HANDLE_VALUE) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = si.hStdError = logh;
        si.hStdInput = nullptr;
    }
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    if (!CreateProcessW(app.game_exe.c_str(), buf.data(), nullptr, nullptr, logh != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW * 0, nullptr, cwd.c_str(), &si, &pi)) {
        if (logh != INVALID_HANDLE_VALUE) CloseHandle(logh);
        err = "CreateProcess falhou (erro " + std::to_string(GetLastError()) + ")";
        return false;
    }
    app.log("JOGAR cwd=" + path_to_utf8(app.root_dir) + " cmd=" + path_to_utf8(fs::path(cmd)) + " pid=" + std::to_string(pi.dwProcessId));
    if (logh != INVALID_HANDLE_VALUE) CloseHandle(logh);
    app.load_slot_arg.clear();
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
