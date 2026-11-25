#include "screens.h"
#include "../sha256.h"
#include "../widgets.h"
#include <SDL_syswm.h>
#include <commdlg.h>
#include <windows.h>

namespace {
enum { ID_BROWSE = 1, ID_RECENT = 2, ID_FORCE = 3, ID_BACK = 4 };

std::string browse_rom(App& app) {
    HWND owner = nullptr;
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (SDL_GetWindowWMInfo(app.render.window, &wm)) owner = wm.info.win.window;
    wchar_t file[MAX_PATH * 2] = L"";
    std::wstring init = (app.root_dir / "roms").wstring();
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"ROMs Mega Drive (*.gen;*.md;*.bin;*.smd)\0*.gen;*.md;*.bin;*.smd\0Todos os arquivos (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)(sizeof file / sizeof file[0]);
    ofn.lpstrInitialDir = init.c_str();
    ofn.lpstrTitle = L"Selecionar a ROM de Eternal Champions (US)";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return "";
    return path_to_utf8(std::filesystem::path(file));
}

class RomScreen : public Screen {
public:
    void on_enter(App& app) override { rebuild(app); }
    void on_resume(App& app) override { rebuild(app); }

    void update(App& app, const InputState& in, float) override {
        if (in.p(A_START)) { request_play(app); return; }
        if (in.p(A_BACK) || in.mouse_right_click) { app.pop(); return; }
        int a = menu_.update(app, in);
        if (a < 0) return;
        const MenuItem& it = menu_.items[a];
        switch (it.id) {
        case ID_BROWSE: {
            std::string p = browse_rom(app);
            if (!p.empty()) choose(app, p);
            break;
        }
        case ID_RECENT: choose(app, paths_[a]); break;
        case ID_FORCE:
            app.set_rom(pending_, true);
            msg_ = "ROM gravada (SHA diferente da vers\xC3\xA3o US \xE2\x80\x94 sem garantias).";
            ok_ = false; pending_.clear();
            rebuild(app);
            break;
        case ID_BACK: app.pop(); break;
        }
    }

    void draw(App& app) override {
        Render& r = app.render;
        Theme& th = app.theme;
        th.draw_stone_bg(r, app.time);
        draw_header(app, "ROM", CANVAS_W / 2.f, 30);
        Rect p{140, 120, 1000, 150};
        th.draw_panel(r, p, 0.7f);
        const Font& hf = *th.fonts.help;
        TextStyle lab = th.help_style(), val = th.item_style(false, true);
        float x = p.x + 28, y = p.y + 18;
        draw_text(r, hf, x, y, "ROM atual:", lab);
        std::string cur = app.rom_path.empty() ? "(nenhuma)" : app.rom_path;
        // encurta o caminho do inicio se for largo
        while (hf.measure(cur) > 760 && cur.size() > 8) cur = "\xE2\x80\xA6" + cur.substr(cur.size() > 40 ? cur.size() - 40 + (cur.size() % 2) : 1);
        draw_text(r, hf, x + 110, y, cur, val);
        y += 32;
        draw_text(r, hf, x, y, "SHA-256:", lab);
        if (!app.rom_sha.empty()) {
            TextStyle ms = TextStyle::solid(app.rom_valid ? th.pal.ok : th.pal.bad, 1, th.pal.text_outline);
            float w = draw_text(r, hf, x + 110, y, app.rom_valid ? "\xE2\x9C\x93" : "\xE2\x9C\x97", ms);
            draw_text(r, hf, x + 110 + w + 10, y, app.rom_sha.substr(0, 24) + "\xE2\x80\xA6", lab);
        } else draw_text(r, hf, x + 110, y, "\xE2\x80\x94", lab);
        y += 32;
        if (!msg_.empty()) {
            TextStyle ms = TextStyle::solid(ok_ ? th.pal.ok : th.pal.text_select, 1, th.pal.text_outline);
            draw_text(r, hf, x, y, msg_, ms);
        } else {
            draw_text(r, hf, x, y, "Esperado (US): " + std::string(EC_US_ROM_SHA256).substr(0, 24) + "\xE2\x80\xA6", lab);
        }
        menu_.draw(app);
        draw_help_bar(app, default_hints(app, app.input.state()));
    }

private:
    void choose(App& app, const std::string& path) {
        std::error_code ec;
        std::string sha = sha256_file_hex(utf8_to_path(path));
        if (sha.empty()) { msg_ = "N\xC3\xA3o foi poss\xC3\xADvel ler o arquivo."; ok_ = false; pending_.clear(); rebuild(app); return; }
        if (sha == EC_US_ROM_SHA256) {
            app.set_rom(path, true);
            msg_ = "ROM v\xC3\xA1lida (Eternal Champions US) \xE2\x80\x94 gravada em rom-EC.cfg."; ok_ = true; pending_.clear();
            app.toast("ROM configurada.");
        } else {
            pending_ = path;
            msg_ = "AVISO: SHA-256 diferente da vers\xC3\xA3o US suportada. N\xC3\xA3o foi gravada.";
            ok_ = false;
        }
        rebuild(app);
    }

    void rebuild(App& app) {
        menu_.items.clear(); paths_.clear();
        menu_.items.push_back({"Selecionar arquivo\xE2\x80\xA6", true, ID_BROWSE}); paths_.push_back("");
        for (auto& rp : app.recents) {
            menu_.items.push_back({"\xE2\x80\xA2 " + path_to_utf8(utf8_to_path(rp).filename()), true, ID_RECENT});
            paths_.push_back(rp);
        }
        for (auto& dp : app.detected) {
            bool dup = false;
            for (auto& rp : app.recents) dup |= (rp == dp);
            if (dup) continue;
            menu_.items.push_back({"\xE2\x80\xA2 " + path_to_utf8(utf8_to_path(dp).filename()) + "  (detectada em roms/)", true, ID_RECENT});
            paths_.push_back(dp);
        }
        if (!pending_.empty()) { menu_.items.push_back({"Usar mesmo assim", true, ID_FORCE}); paths_.push_back(""); }
        menu_.items.push_back({"Voltar", true, ID_BACK}); paths_.push_back("");
        menu_.x = 180; menu_.align = Align::Left; menu_.width = 900;
        menu_.y = 300; menu_.row_h = 36; menu_.visible_rows = 8;
        menu_.set_focus(menu_.focus);
    }

    MenuList menu_;
    std::vector<std::string> paths_;
    std::string msg_, pending_;
    bool ok_ = false;
};
}  // namespace

std::unique_ptr<Screen> make_rom_screen() { return std::make_unique<RomScreen>(); }
