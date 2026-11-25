#include "launch.h"
#include "dumps.h"
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// "a b \"c d\"" -> {a, b, c d} (aspas agrupam; fora delas, espacos separam)
static std::vector<std::string> split_args(const std::string& s) {
    std::vector<std::string> r;
    std::string cur;
    bool inq = false, have = false;
    for (char c : s) {
        if (c == '"') { inq = !inq; have = true; }
        else if (!inq && (c == ' ' || c == '\t')) { if (have) { r.push_back(cur); cur.clear(); have = false; } }
        else { cur += c; have = true; }
    }
    if (have) r.push_back(cur);
    return r;
}

bool start_game(App& app, GameProcess& gp, std::string& err) {
    std::error_code ec;
    if (!fs::exists(app.game_exe, ec)) { err = os::game_exe_name() + " nao encontrado em " + path_to_utf8(app.exe_dir); return false; }
    if (app.rom_path.empty() || !fs::exists(utf8_to_path(app.rom_path), ec)) { err = "ROM nao configurada ou inexistente"; return false; }
    app.cfg.save();   // grava mudancas adiadas (Config::set) antes de o jogo ler o ini
    // o launcher generico do runner nunca deve aparecer; se o ini nao puder ser gravado, --no-launcher ja cobre: so registra
    if (!app.cfg.ensure_skip_launcher()) app.log("aviso: nao foi possivel gravar [launcher] skip_launcher no settings.ini (seguindo com --no-launcher)");

    // argv = --no-launcher <rom> + flags de [launcher.args] (+ extras de teste); tudo o mais vai pelo ini
    ecr::SpawnOptions so;
    so.exe = path_to_utf8(app.game_exe);
    so.args = {"--no-launcher", app.rom_path};
    for (const std::string& x : app.cfg.build_args()) so.args.push_back(x);
    for (const std::string& x : split_args(app.extra_game_args)) so.args.push_back(x);
    if (!app.load_slot_arg.empty()) { so.args.push_back("--load-slot"); for (const std::string& x : split_args(app.load_slot_arg)) so.args.push_back(x); }
    so.cwd = path_to_utf8(app.root_dir);   // jogo resolve "assets/..." e "../assets/..." relativo ao cwd
    // stdout/stderr do jogo -> build/logs/game_last.log (para conferir [BPLUS_GL], [SHADER], [EC-AUDIO]...)
    fs::create_directories(app.logs_dir, ec);
    so.log_path = path_to_utf8(app.logs_dir / "game_last.log");
    if (!ecr::spawn(so, gp.proc, &err)) return false;
    std::string cmd = ecr::quote_arg(so.exe);
    for (const std::string& x : so.args) cmd += " " + ecr::quote_arg(x);
    app.log("JOGAR cwd=" + so.cwd + " cmd=" + cmd + " pid=" + std::to_string(gp.proc.pid));
    app.load_slot_arg.clear();
    return true;
}

fs::path find_last_error_log(App& app) {
    std::error_code ec;
    for (fs::path c : {app.logs_dir / "last_error.log", app.exe_dir / "last_error.log", app.root_dir / "last_error.log"})
        if (fs::exists(c, ec)) return c;
    return fs::path();
}
