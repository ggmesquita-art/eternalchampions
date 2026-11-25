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

std::string pick_open_file(App& app, const wchar_t* title, const wchar_t* filter, const std::filesystem::path& init) {
    wchar_t file[MAX_PATH * 2] = L"";
    std::wstring dir = init.wstring();
    OPENFILENAMEW o{};
    o.lStructSize = sizeof o; o.hwndOwner = owner_of(app);
    o.lpstrFilter = filter; o.lpstrFile = file; o.nMaxFile = (DWORD)(sizeof file / sizeof file[0]);
    o.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    o.lpstrTitle = title;
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&o)) return "";
    return path_to_utf8(std::filesystem::path(file));
}

std::string pick_save_file(App& app, const wchar_t* title, const wchar_t* filter, const wchar_t* ext) {
    wchar_t file[MAX_PATH * 2] = L"";
    OPENFILENAMEW o{};
    o.lStructSize = sizeof o; o.hwndOwner = owner_of(app);
    o.lpstrFilter = filter; o.lpstrFile = file; o.nMaxFile = (DWORD)(sizeof file / sizeof file[0]);
    o.lpstrTitle = title; o.lpstrDefExt = ext;
    o.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameW(&o)) return "";
    return path_to_utf8(std::filesystem::path(file));
}

std::string pick_folder(App& app, const wchar_t* title) {
    BROWSEINFOW b{};
    b.hwndOwner = owner_of(app); b.lpszTitle = title;
    b.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderW(&b);
    if (!id) return "";
    wchar_t path[MAX_PATH * 2] = L"";
    bool ok = SHGetPathFromIDListW(id, path) != 0;
    CoTaskMemFree(id);
    return ok ? path_to_utf8(std::filesystem::path(path)) : "";
}
