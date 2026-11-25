// Dialogos nativos do Windows (comdlg32 / shell32). Titulos e filtros chegam em UTF-8.
#include "platform.h"
#include <SDL_syswm.h>
#include <commdlg.h>
#include <shlobj.h>
#include <windows.h>

static HWND owner_of(App& app) {
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    return SDL_GetWindowWMInfo(app.render.window, &wm) ? wm.info.win.window : nullptr;
}

static std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

// "rotulo\0padroes\0rotulo2\0padroes2\0\0"
static std::wstring build_filter(const std::vector<FileFilter>& filters) {
    std::wstring f;
    for (const FileFilter& ff : filters) {
        f += widen(ff.label); f += L'\0';
        f += widen(ff.patterns); f += L'\0';
    }
    f += L'\0';
    return f;
}

std::string pick_open_file(App& app, const std::string& title, const std::vector<FileFilter>& filters, const std::filesystem::path& init) {
    wchar_t file[MAX_PATH * 2] = L"";
    std::wstring dir = init.wstring();
    std::wstring wt = widen(title), flt = build_filter(filters);
    OPENFILENAMEW o{};
    o.lStructSize = sizeof o; o.hwndOwner = owner_of(app);
    o.lpstrFilter = filters.empty() ? nullptr : flt.c_str();
    o.lpstrFile = file; o.nMaxFile = (DWORD)(sizeof file / sizeof file[0]);
    o.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    o.lpstrTitle = wt.c_str();
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&o)) return "";
    return path_to_utf8(std::filesystem::path(file));
}

std::string pick_save_file(App& app, const std::string& title, const std::vector<FileFilter>& filters, const std::string& ext) {
    wchar_t file[MAX_PATH * 2] = L"";
    std::wstring wt = widen(title), flt = build_filter(filters), we = widen(ext);
    OPENFILENAMEW o{};
    o.lStructSize = sizeof o; o.hwndOwner = owner_of(app);
    o.lpstrFilter = filters.empty() ? nullptr : flt.c_str();
    o.lpstrFile = file; o.nMaxFile = (DWORD)(sizeof file / sizeof file[0]);
    o.lpstrTitle = wt.c_str(); o.lpstrDefExt = we.empty() ? nullptr : we.c_str();
    o.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameW(&o)) return "";
    return path_to_utf8(std::filesystem::path(file));
}

std::string pick_folder(App& app, const std::string& title) {
    std::wstring wt = widen(title);
    BROWSEINFOW b{};
    b.hwndOwner = owner_of(app); b.lpszTitle = wt.c_str();
    b.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderW(&b);
    if (!id) return "";
    wchar_t path[MAX_PATH * 2] = L"";
    bool ok = SHGetPathFromIDListW(id, path) != 0;
    CoTaskMemFree(id);
    return ok ? path_to_utf8(std::filesystem::path(path)) : "";
}
