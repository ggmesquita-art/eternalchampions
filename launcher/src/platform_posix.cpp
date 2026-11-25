// Dialogos de arquivo/pasta fora do Windows, sempre via processo externo (nunca shell):
//   Linux: zenity, ou kdialog se nao houver zenity.   macOS: osascript (AppleScript, args via `on run argv`).
// Sem nenhum dos programas: devolve "" e registra no log/toast.
#include "platform.h"
#include "proc.h"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <SDL.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

// Roda argv[0] (procurado no PATH) com stdout capturado por pipe; stdin/stderr = /dev/null.
// Retorna false se nao deu para iniciar; *code = exit code.
bool capture_stdout(const std::vector<std::string>& args, std::string* out, int* code) {
    std::string exe = ecr::find_in_path(args[0]);
    if (exe.empty()) return false;
    std::vector<std::string> storage = args;
    std::vector<char*> argv;
    for (auto& s : storage) argv.push_back(&s[0]);
    argv.push_back(nullptr);
    int nullfd = ::open("/dev/null", O_RDWR | O_CLOEXEC);
    int pfd[2];
    ecr::lock_fork();
    if (::pipe(pfd) != 0) { ecr::unlock_fork(); if (nullfd >= 0) ::close(nullfd); return false; }
    ::fcntl(pfd[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(pfd[1], F_SETFD, FD_CLOEXEC);
    pid_t pid = ::fork();
    if (pid != 0) ecr::unlock_fork();
    if (pid < 0) { ::close(pfd[0]); ::close(pfd[1]); if (nullfd >= 0) ::close(nullfd); return false; }
    if (pid == 0) {
        if (nullfd >= 0) { ::dup2(nullfd, 0); ::dup2(nullfd, 2); }
        ::dup2(pfd[1], 1);
        ::execv(exe.c_str(), argv.data());
        _exit(127);
    }
    ::close(pfd[1]);
    if (nullfd >= 0) ::close(nullfd);
    out->clear();
    // O dialogo e modal mas a janela SDL precisa continuar respondendo ao ping do compositor
    // (senao GNOME/KWin oferecem "Forcar encerramento" apos ~5 s): bombeia eventos enquanto espera.
    char buf[4096];
    for (;;) {
        struct pollfd pp = {pfd[0], POLLIN, 0};
        int pr = ::poll(&pp, 1, 50);
        if (pr < 0 && errno != EINTR) break;
        if (pr <= 0) { SDL_PumpEvents(); continue; }
        ssize_t n = ::read(pfd[0], buf, sizeof buf);
        if (n > 0) out->append(buf, (size_t)n);
        else if (n < 0 && errno == EINTR) continue;
        else break;
    }
    ::close(pfd[0]);
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);   // descarta cliques/teclas dados durante o dialogo
    int st = 0;
    while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    *code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    if (*code == 127 && out->empty()) return false;   // execv falhou
    return true;
}

std::vector<std::string> split_patterns(const std::string& p) {
    std::vector<std::string> r;
    size_t pos = 0;
    while (pos <= p.size()) {
        size_t e = p.find(';', pos);
        if (e == std::string::npos) e = p.size();
        std::string t = p.substr(pos, e - pos);
        size_t a = t.find_first_not_of(' '), b = t.find_last_not_of(' ');
        if (a != std::string::npos) r.push_back(t.substr(a, b - a + 1));
        pos = e + 1;
    }
    return r;
}

std::string join(const std::vector<std::string>& v, const char* sep) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) { if (i) s += sep; s += v[i]; }
    return s;
}

// "*.*" nao existe como glob portatil: vira "*". Globs do zenity/kdialog diferenciam maiusculas (o filtro
// do Windows nao): cada "*.ext" ganha tambem a variante "*.EXT".
std::vector<std::string> glob_patterns(const std::string& p) {
    std::vector<std::string> v;
    for (std::string s : split_patterns(p)) {
        if (s == "*.*") s = "*";
        v.push_back(s);
        std::string up = s;
        for (char& c : up) if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (up != s) v.push_back(up);
    }
    return v;
}

std::string trim_nl(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

std::string with_default_ext(std::string p, const std::string& ext) {
    if (p.empty() || ext.empty()) return p;
    size_t slash = p.find_last_of('/');
    size_t dot = p.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) p += "." + ext;
    return p;
}

enum class Kind { Open, Save, Folder };

#ifdef __APPLE__

std::string run_dialog(App& app, Kind kind, const std::string& title, const std::vector<FileFilter>& filters,
                       const std::string& dir, const std::string& def_ext) {
    // Todos os argumentos de usuario vao por argv ("_" na frente: o osascript nao os confunde com opcoes).
    static const char* kScript[] = {
        "on arg(x)",
        "if (count of x) > 1 then return text 2 thru -1 of x",
        "return \"\"",
        "end arg",
        "on run argv",
        "set mode to my arg(item 1 of argv)",
        "set t to my arg(item 2 of argv)",
        "set d to my arg(item 3 of argv)",
        "set exts to {}",
        "repeat with i from 4 to count of argv",
        "set end of exts to my arg(item i of argv)",
        "end repeat",
        "try",
        "if mode is \"folder\" then",
        "if d is \"\" then",
        "set r to choose folder with prompt t",
        "else",
        "set r to choose folder with prompt t default location (POSIX file d as alias)",
        "end if",
        "else if mode is \"save\" then",
        "if d is \"\" then",
        "set r to choose file name with prompt t",
        "else",
        "set r to choose file name with prompt t default location (POSIX file d as alias)",
        "end if",
        "else",
        "if d is \"\" then",
        "if (count of exts) is 0 then",
        "set r to choose file with prompt t",
        "else",
        "set r to choose file with prompt t of type exts",
        "end if",
        "else",
        "if (count of exts) is 0 then",
        "set r to choose file with prompt t default location (POSIX file d as alias)",
        "else",
        "set r to choose file with prompt t of type exts default location (POSIX file d as alias)",
        "end if",
        "end if",
        "end if",
        "return POSIX path of r",
        "on error",
        "return \"\"",
        "end try",
        "end run",
    };
    std::vector<std::string> a = {"osascript"};
    for (const char* l : kScript) { a.push_back("-e"); a.push_back(l); }
    a.push_back(std::string("_") + (kind == Kind::Folder ? "folder" : kind == Kind::Save ? "save" : "open"));
    a.push_back("_" + title);
    std::error_code dec;
    a.push_back("_" + (!dir.empty() && std::filesystem::is_directory(dir, dec) ? dir : std::string()));
    if (kind == Kind::Open && !filters.empty()) {   // macOS nao tem lista de filtros: usa so o primeiro
        for (const std::string& p : split_patterns(filters[0].patterns))
            if (p.rfind("*.", 0) == 0 && p.find_first_of("*?", 2) == std::string::npos) a.push_back("_" + p.substr(2));
    }
    std::string out; int code = 0;
    if (!capture_stdout(a, &out, &code)) { app.log("osascript nao pode ser executado"); app.toast("osascript indisponivel."); return ""; }
    if (code != 0) return "";
    std::string p = trim_nl(out);
    if (kind == Kind::Save) p = with_default_ext(p, def_ext);
    if (kind == Kind::Folder && p.size() > 1 && p.back() == '/') p.pop_back();   // POSIX path of <pasta> termina em /
    return p;
}

#else   // Linux

std::string run_dialog(App& app, Kind kind, const std::string& title, const std::vector<FileFilter>& filters,
                       const std::string& dir, const std::string& def_ext) {
    std::vector<std::string> a;
    if (!ecr::find_in_path("zenity").empty()) {
        a = {"zenity", "--file-selection", "--title=" + title};
        if (kind == Kind::Save) { a.push_back("--save"); a.push_back("--confirm-overwrite"); }
        if (kind == Kind::Folder) a.push_back("--directory");
        if (!dir.empty()) a.push_back("--filename=" + dir + (dir.back() == '/' ? "" : "/"));
        if (kind != Kind::Folder)
            for (const FileFilter& f : filters)
                a.push_back("--file-filter=" + f.label + " | " + join(glob_patterns(f.patterns), " "));
    } else if (!ecr::find_in_path("kdialog").empty()) {
        std::string start = dir.empty() ? "." : dir;
        if (kind == Kind::Folder) a = {"kdialog", "--getexistingdirectory", start};
        else {
            std::string flt;
            for (const FileFilter& f : filters) {
                if (!flt.empty()) flt += "\n";
                flt += join(glob_patterns(f.patterns), " ") + "|" + f.label;
            }
            a = {"kdialog", kind == Kind::Save ? "--getsavefilename" : "--getopenfilename", start};
            if (!flt.empty()) a.push_back(flt);
        }
        a.push_back("--title");
        a.push_back(title);
    } else {
        app.log("dialogo de arquivo indisponivel: instale zenity ou kdialog");
        app.toast("Instale zenity ou kdialog para escolher arquivos.", 5.f);
        return "";
    }
    std::string out; int code = 0;
    if (!capture_stdout(a, &out, &code)) { app.log("nao foi possivel executar " + a[0]); app.toast("Falha ao abrir o seletor (" + a[0] + ")."); return ""; }
    if (code != 0) return "";   // cancelado
    std::string p = trim_nl(out);
    if (kind == Kind::Save) p = with_default_ext(p, def_ext);
    return p;
}

#endif

}  // namespace

std::string pick_open_file(App& app, const std::string& title, const std::vector<FileFilter>& filters, const std::filesystem::path& init) {
    return run_dialog(app, Kind::Open, title, filters, path_to_utf8(init), "");
}

std::string pick_save_file(App& app, const std::string& title, const std::vector<FileFilter>& filters, const std::string& default_ext) {
    return run_dialog(app, Kind::Save, title, filters, "", default_ext);
}

std::string pick_folder(App& app, const std::string& title) {
    return run_dialog(app, Kind::Folder, title, {}, "", "");
}
