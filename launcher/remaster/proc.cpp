// proc.cpp - ver proc.h.
#include "proc.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <mutex>
#include <set>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace ecr {

std::string quote_arg(const std::string& a) {
    if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos) return a;
    std::string r = "\"";
    size_t bs = 0;
    for (char c : a) {
        if (c == '\\') { ++bs; continue; }
        if (c == '"') { r.append(bs * 2 + 1, '\\'); r += '"'; bs = 0; continue; }
        r.append(bs, '\\');
        bs = 0;
        r += c;
    }
    r.append(bs * 2, '\\');
    return r + "\"";
}

// ===========================================================================
#ifdef _WIN32
// ===========================================================================
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

HANDLE g_job = nullptr;

// CreateProcessW herdando SO `hlog` (stdout/stderr) + um NUL como stdin, via PROC_THREAD_ATTRIBUTE_HANDLE_LIST
// (STARTUPINFOEX). hlog = HANDLE inheritavel ou nullptr/INVALID_HANDLE_VALUE (nada herdado).
bool spawn_process_w(const wchar_t* app, wchar_t* cmdline, const wchar_t* cwd, void* env_block, unsigned long flags,
                     HANDLE hlog, PROCESS_INFORMATION* pi, unsigned long* err) {
    const bool have_log = hlog && hlog != INVALID_HANDLE_VALUE;
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    std::vector<char> attr_buf;
    HANDLE nul = INVALID_HANDLE_VALUE;
    HANDLE list[2] = {nullptr, nullptr};
    bool attr_ok = false;
    if (have_log) {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        SIZE_T sz = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &sz);
        attr_buf.resize(sz);
        LPPROC_THREAD_ATTRIBUTE_LIST al = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
        if (nul != INVALID_HANDLE_VALUE && InitializeProcThreadAttributeList(al, 1, 0, &sz)) {
            list[0] = hlog; list[1] = nul;
            if (UpdateProcThreadAttribute(al, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, list, sizeof(list), nullptr, nullptr)) {
                si.lpAttributeList = al;
                si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
                si.StartupInfo.hStdInput = nul;
                si.StartupInfo.hStdOutput = si.StartupInfo.hStdError = hlog;
                attr_ok = true;
            } else DeleteProcThreadAttributeList(al);
        }
    }
    const bool inherit = have_log && attr_ok;   // sem lista valida nao herda nada (melhor sem log do que vazar handles)
    BOOL ok = CreateProcessW(app, cmdline, nullptr, nullptr, inherit ? TRUE : FALSE,
                             flags | (inherit ? EXTENDED_STARTUPINFO_PRESENT : 0), env_block, cwd, &si.StartupInfo, pi);
    if (!ok && err) *err = GetLastError();
    if (attr_ok) DeleteProcThreadAttributeList(si.lpAttributeList);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    return ok != FALSE;
}

}  // namespace

bool spawn(const SpawnOptions& o, ChildProcess& out, std::string* err) {
    std::wstring cmd = widen(quote_arg(o.exe));
    for (auto& a : o.args) cmd += L" " + widen(quote_arg(a));

    // ambiente: atual + overrides, ordenado (so quando ha overrides; senao herda direto)
    std::wstring block;
    if (!o.env.empty()) {
        std::vector<std::wstring> env;
        if (LPWCH blk = GetEnvironmentStringsW()) {
            for (LPWCH p = blk; *p;) {
                std::wstring s(p);
                p += s.size() + 1;
                if (!s.empty() && s[0] != L'=') env.push_back(s);
            }
            FreeEnvironmentStringsW(blk);
        }
        for (auto& kv : o.env) {
            std::wstring k = widen(kv.first);
            std::wstring entry = k + L"=" + widen(kv.second);
            bool found = false;
            for (auto& e : env) {
                if (e.size() > k.size() && e[k.size()] == L'=' && _wcsnicmp(e.c_str(), k.c_str(), k.size()) == 0) {
                    e = entry;
                    found = true;
                    break;
                }
            }
            if (!found) env.push_back(entry);
        }
        std::sort(env.begin(), env.end(), [](const std::wstring& a, const std::wstring& b) {
            return _wcsicmp(a.c_str(), b.c_str()) < 0;
        });
        for (auto& e : env) { block += e; block += L'\0'; }
        block += L'\0';
    }

    HANDLE hlog = INVALID_HANDLE_VALUE;
    if (!o.log_path.empty()) {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        hlog = CreateFileW(widen(o.log_path).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    PROCESS_INFORMATION pi{};
    std::wstring cwd = widen(o.cwd);
    std::wstring app;
    if (o.exe.find_first_of("\\/") != std::string::npos) app = widen(o.exe);   // caminho explicito: sem busca no PATH
    unsigned long flags = (o.no_window ? CREATE_NO_WINDOW : 0) | (block.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT);
    unsigned long le = 0;
    bool ok = spawn_process_w(app.empty() ? nullptr : app.c_str(), &cmd[0], cwd.empty() ? nullptr : cwd.c_str(),
                              block.empty() ? nullptr : &block[0], flags, hlog, &pi, &le);
    if (hlog != INVALID_HANDLE_VALUE) CloseHandle(hlog);
    if (!ok) {
        if (err) *err = "CreateProcess falhou (erro " + std::to_string(le) + "): " + o.exe;
        return false;
    }
    CloseHandle(pi.hThread);
    out.handle = pi.hProcess;
    out.pid = pi.dwProcessId;
    return true;
}

bool wait_for(ChildProcess& p, int timeout_ms, int* exit_code) {
    if (!p.valid()) return false;
    HANDLE h = static_cast<HANDLE>(p.handle);
    if (WaitForSingleObject(h, (DWORD)std::max(0, timeout_ms)) != WAIT_OBJECT_0) return false;
    DWORD code = 0;
    GetExitCodeProcess(h, &code);
    if (exit_code) *exit_code = (int)code;
    CloseHandle(h);
    p = ChildProcess{};
    return true;
}

bool try_wait(ChildProcess& p, int* exit_code) { return wait_for(p, 0, exit_code); }

void terminate(ChildProcess& p, int wait_ms) {
    if (!p.valid()) return;
    HANDLE h = static_cast<HANDLE>(p.handle);
    TerminateProcess(h, 1);
    WaitForSingleObject(h, (DWORD)std::max(0, wait_ms));
    CloseHandle(h);
    p = ChildProcess{};
}

void release(ChildProcess& p) {
    if (p.valid() && p.handle) CloseHandle(static_cast<HANDLE>(p.handle));
    p = ChildProcess{};
}

void enable_kill_children_on_exit() {
    // Job Object KILL_ON_JOB_CLOSE: todo filho (jogo, extrator, previa, dumps, captura) morre junto com o launcher.
    if (g_job) return;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li) && AssignProcessToJobObject(job, GetCurrentProcess())) g_job = job;
    else CloseHandle(job);
    // g_job NAO e fechado: com KILL_ON_JOB_CLOSE isso mataria o proprio launcher (membro do job); o SO fecha o handle
    // na saida do processo e encerra qualquer filho remanescente.
}

void kill_all_children() {}

std::string find_in_path(const std::string& name) {
    std::wstring w = widen(name);
    wchar_t buf[MAX_PATH * 2];
    DWORD n = SearchPathW(nullptr, w.c_str(), L".exe", (DWORD)(sizeof buf / sizeof buf[0]), buf, nullptr);
    if (!n || n >= sizeof buf / sizeof buf[0]) return "";
    int m = WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)m, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, (int)n, &s[0], m, nullptr, nullptr);
    return s;
}

unsigned long current_pid() { return GetCurrentProcessId(); }

// ===========================================================================
#else   // POSIX
// ===========================================================================
namespace {

std::mutex g_mu;
std::mutex g_fork_mu;   // ver lock_fork() em proc.h
std::set<pid_t> g_tracked;   // filhos com kill_with_launcher ainda nao colhidos

void track(pid_t pid, bool on) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (on) g_tracked.insert(pid); else g_tracked.erase(pid);
}

int decode_status(int st) {
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return -1;
}

bool is_exec_file(const std::string& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec) && access(p.c_str(), X_OK) == 0;
}

}  // namespace

std::string find_in_path(const std::string& name) {
    if (name.empty()) return "";
    if (name.find('/') != std::string::npos) return is_exec_file(name) ? name : "";
    const char* path = std::getenv("PATH");
    std::string pv = path ? path : "/usr/local/bin:/usr/bin:/bin";
    size_t pos = 0;
    while (pos <= pv.size()) {
        size_t e = pv.find(':', pos);
        if (e == std::string::npos) e = pv.size();
        std::string dir = pv.substr(pos, e - pos);
        if (dir.empty()) dir = ".";
        std::string cand = dir + "/" + name;
        if (is_exec_file(cand)) return cand;
        pos = e + 1;
    }
    return "";
}

bool spawn(const SpawnOptions& o, ChildProcess& out, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    // tudo o que aloca memoria acontece ANTES do fork (o filho so usa chamadas async-signal-safe)
    std::string exe_path = find_in_path(o.exe);
    if (exe_path.empty()) return fail("executavel nao encontrado: " + o.exe);
    if (exe_path[0] != '/') {
        std::error_code ec;
        exe_path = fs::absolute(exe_path, ec).string();   // o filho faz chdir(cwd) antes do execve
    }
    std::vector<std::string> argv_s;
    argv_s.push_back(o.exe);
    for (auto& a : o.args) argv_s.push_back(a);
    std::vector<char*> argv;
    for (auto& s : argv_s) argv.push_back(&s[0]);
    argv.push_back(nullptr);

    std::vector<std::string> env_s;
    for (char** e = environ; e && *e; ++e) env_s.push_back(*e);
    for (auto& kv : o.env) {
        std::string entry = kv.first + "=" + kv.second;
        bool found = false;
        for (auto& e : env_s)
            if (e.size() > kv.first.size() && e[kv.first.size()] == '=' && e.compare(0, kv.first.size(), kv.first) == 0) { e = entry; found = true; break; }
        if (!found) env_s.push_back(entry);
    }
    std::vector<char*> envp;
    for (auto& s : env_s) envp.push_back(&s[0]);
    envp.push_back(nullptr);

    int logfd = -1;
    if (!o.log_path.empty()) {
        logfd = ::open(o.log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (logfd < 0) return fail("nao foi possivel criar o log " + o.log_path + ": " + std::strerror(errno));
    }
    int nullfd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    int errpipe[2];
    lock_fork();   // liberado logo apos o fork (no pai); o filho so faz exec/_exit
    if (::pipe(errpipe) != 0) {
        unlock_fork();
        if (logfd >= 0) ::close(logfd);
        if (nullfd >= 0) ::close(nullfd);
        return fail(std::string("pipe falhou: ") + std::strerror(errno));
    }
    ::fcntl(errpipe[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(errpipe[1], F_SETFD, FD_CLOEXEC);
    const char* cwd = o.cwd.empty() ? nullptr : o.cwd.c_str();
    const bool die_with_parent = o.kill_with_launcher;
    const pid_t parent = ::getpid();

    pid_t pid = ::fork();
    if (pid != 0) unlock_fork();
    if (pid < 0) {
        int e = errno;
        if (logfd >= 0) ::close(logfd);
        if (nullfd >= 0) ::close(nullfd);
        ::close(errpipe[0]); ::close(errpipe[1]);
        return fail(std::string("fork falhou: ") + std::strerror(e));
    }
    if (pid == 0) {   // filho
#ifdef __linux__
        if (die_with_parent) {
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            if (::getppid() != parent) _exit(127);   // o pai ja morreu entre o fork e o prctl
        }
#else
        (void)die_with_parent; (void)parent;
#endif
        if (nullfd >= 0) ::dup2(nullfd, 0);
        if (logfd >= 0) { ::dup2(logfd, 1); ::dup2(logfd, 2); }
        if (cwd && ::chdir(cwd) != 0) { int e = errno; ssize_t r = ::write(errpipe[1], &e, sizeof e); (void)r; _exit(127); }
        ::execve(exe_path.c_str(), argv.data(), envp.data());
        int e = errno;
        ssize_t r = ::write(errpipe[1], &e, sizeof e);
        (void)r;
        _exit(127);
    }
    // pai
    ::close(errpipe[1]);
    if (logfd >= 0) ::close(logfd);
    if (nullfd >= 0) ::close(nullfd);
    int child_errno = 0;
    ssize_t n;
    do { n = ::read(errpipe[0], &child_errno, sizeof child_errno); } while (n < 0 && errno == EINTR);
    ::close(errpipe[0]);
    if (n > 0) {   // exec/chdir falhou no filho
        int st = 0;
        ::waitpid(pid, &st, 0);
        return fail("falha ao iniciar " + o.exe + ": " + std::strerror(child_errno));
    }
    out.handle = nullptr;
    out.pid = (unsigned long)pid;
    if (o.kill_with_launcher) track(pid, true);
    return true;
}

bool wait_for(ChildProcess& p, int timeout_ms, int* exit_code) {
    if (!p.valid()) return false;
    const auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        int st = 0;
        pid_t r = ::waitpid((pid_t)p.pid, &st, WNOHANG);
        if (r == (pid_t)p.pid || (r < 0 && errno != EINTR)) {
            if (exit_code) *exit_code = r > 0 ? decode_status(st) : -1;
            track((pid_t)p.pid, false);
            p = ChildProcess{};
            return true;
        }
        if (std::chrono::steady_clock::now() - t0 >= std::chrono::milliseconds(std::max(0, timeout_ms))) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

bool try_wait(ChildProcess& p, int* exit_code) { return wait_for(p, 0, exit_code); }

void release(ChildProcess& p) {
    p = ChildProcess{};   // continua em g_tracked ate kill_all_children(); sem wait ele vira zumbi ate sair
}

void terminate(ChildProcess& p, int wait_ms) {
    if (!p.valid()) return;
    ::kill((pid_t)p.pid, SIGKILL);
    int code = 0;
    if (!wait_for(p, wait_ms, &code)) release(p);
}

void kill_all_children() {
    std::set<pid_t> pids;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        pids.swap(g_tracked);
    }
    for (pid_t pid : pids) {
        ::kill(pid, SIGKILL);
        int st = 0;
        ::waitpid(pid, &st, 0);
    }
}

void enable_kill_children_on_exit() {
    static bool done = false;
    if (done) return;
    done = true;
#ifndef __linux__
    std::atexit([] { kill_all_children(); });   // sem PDEATHSIG (macOS): melhor esforco na saida normal
#endif
}

void lock_fork() { g_fork_mu.lock(); }
void unlock_fork() { g_fork_mu.unlock(); }

unsigned long current_pid() { return (unsigned long)::getpid(); }

#endif

// ===========================================================================

int run_process(const SpawnOptions& spec_in, const std::function<bool()>& poll, std::string* err) {
    SpawnOptions spec = spec_in;
    spec.no_window = true;
    ChildProcess cp;
    if (!spawn(spec, cp, err)) return -1;
    int rc = 0;
    for (;;) {
        if (wait_for(cp, 200, &rc)) return rc;
        if (poll && poll()) {
            terminate(cp, 5000);
            return -2;
        }
    }
}

}  // namespace ecr
