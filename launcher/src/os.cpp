#include "os.h"
#include "proc.h"

#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#else
#include <cstdlib>
#include <limits.h>
#include <cerrno>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace fs = std::filesystem;

namespace os {

namespace {
const char* kAppDirName = "EternalChampionsRecomp";
}

std::string game_exe_name() {
#ifdef _WIN32
    return std::string(kAppDirName) + ".exe";
#else
    return kAppDirName;
#endif
}

unsigned long long tick_ms() {
    return (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

#ifdef _WIN32

fs::path exe_path() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
    return fs::path(std::wstring(buf, n));
}

fs::path user_data_dir() {
    fs::path r;
    PWSTR roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)) && roaming) {
        r = fs::path(roaming) / kAppDirName;
        CoTaskMemFree(roaming);
    }
    return r;
}

void shell_open(const fs::path& p) {
    ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::FILE* fopen_utf8(const std::string& path, const char* mode) {
    std::wstring wm;
    for (const char* c = mode; *c; ++c) wm += (wchar_t)*c;
    int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], n);
    FILE* f = nullptr;
    _wfopen_s(&f, w.c_str(), wm.c_str());
    return f;
}

bool replace_file(const fs::path& from, const fs::path& to, std::string* err) {
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (err) *err = "MoveFileEx falhou (" + std::to_string(GetLastError()) + ")";
        DeleteFileW(from.c_str());
        return false;
    }
    return true;
}

bool file_mtime_local(const fs::path& p, std::tm* out) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa)) return false;
    FILETIME lf; SYSTEMTIME st;
    FileTimeToLocalFileTime(&fa.ftLastWriteTime, &lf);
    FileTimeToSystemTime(&lf, &st);
    *out = std::tm{};
    out->tm_mday = st.wDay; out->tm_mon = st.wMonth - 1; out->tm_year = st.wYear - 1900;
    out->tm_hour = st.wHour; out->tm_min = st.wMinute; out->tm_sec = st.wSecond;
    return true;
}

#else   // POSIX

fs::path exe_path() {
#ifdef __APPLE__
    char raw[PATH_MAX * 2];
    uint32_t sz = (uint32_t)sizeof raw;
    if (_NSGetExecutablePath(raw, &sz) != 0) return fs::path();
    char real[PATH_MAX];
    if (realpath(raw, real)) return fs::path(real);
    return fs::path(raw);
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return fs::path();
    buf[n] = 0;
    return fs::path(buf);
#endif
}

fs::path user_data_dir() {
#ifdef __APPLE__
    const char* home = std::getenv("HOME");
    if (!home || !*home) return fs::path();
    return fs::path(home) / "Library" / "Application Support" / kAppDirName;
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg && xdg[0] == '/') return fs::path(xdg) / kAppDirName;
    const char* home = std::getenv("HOME");
    if (!home || !*home) return fs::path();
    return fs::path(home) / ".config" / kAppDirName;
#endif
}

void shell_open(const fs::path& p) {
    if (p.empty()) return;
    ecr::SpawnOptions o;
#ifdef __APPLE__
    o.exe = "open";
#else
    o.exe = "xdg-open";
#endif
    o.args = {p.string()};
    o.log_path = "/dev/null";
    o.kill_with_launcher = false;   // o visualizador pode sobreviver ao launcher
    ecr::ChildProcess cp;
    std::string err;
    if (!ecr::spawn(o, cp, &err)) {
        std::fprintf(stderr, "shell_open: %s\n", err.c_str());
        return;
    }
    // colhe o zumbi sem prazo (xdg-open costuma virar o proprio visualizador) e sem tocar nos globais de
    // proc.cpp, que podem ja ter sido destruidos se o launcher estiver saindo
    const pid_t pid = (pid_t)cp.pid;
    ecr::release(cp);
    std::thread([pid]() { int st = 0; while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {} }).detach();
}

std::FILE* fopen_utf8(const std::string& path, const char* mode) { return std::fopen(path.c_str(), mode); }

bool replace_file(const fs::path& from, const fs::path& to, std::string* err) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) {
        if (err) *err = "rename falhou (" + ec.message() + ")";
        fs::remove(from, ec);
        return false;
    }
    return true;
}

bool file_mtime_local(const fs::path& p, std::tm* out) {
    struct stat sb;
    if (::stat(p.c_str(), &sb) != 0) return false;
    time_t t = sb.st_mtime;
    return localtime_r(&t, out) != nullptr;
}

#endif

}  // namespace os
