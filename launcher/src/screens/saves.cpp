// SAVES: grade dos slots 1-9 (native_save_N.bin + .thumb do runner, somente leitura do formato).
// .thumb = "GTHUMB1\0" (8 bytes) + 128*90 pixels uint32 (0xAARRGGBB); .bin = savestate do slot.
#include "../launch.h"
#include "../platform.h"
#include "../widgets.h"
#include "screens.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <windows.h>

namespace fs = std::filesystem;

namespace {
constexpr int TW = 128, TH = 90;
enum { ID_PLAY = 1, ID_EXPORT, ID_IMPORT, ID_DELETE, ID_CANCEL };

struct Slot {
    bool exists = false, has_thumb = false;
    std::string when;
    uintmax_t size = 0;
    SDL_Texture* tex = nullptr;
};

class Saves : public Screen {
public:
    ~Saves() override { free_tex(); }
    void on_enter(App& app) override { refresh(app); }
    void on_resume(App& app) override { refresh(app); }

    void update(App& app, const InputState& in, float) override {
        if (in.p(A_START)) { request_play(app); return; }
        if (menu_open_) { update_menu(app, in); return; }
        if (in.p(A_BACK) || in.mouse_right_click) { app.pop(); return; }
        int c = sel_ % 3, r = sel_ / 3;
        if (in.p(A_LEFT)) c = (c + 2) % 3;
        if (in.p(A_RIGHT)) c = (c + 1) % 3;
        if (in.p(A_UP)) r = (r + 2) % 3;
        if (in.p(A_DOWN)) r = (r + 1) % 3;
        sel_ = r * 3 + c;
        if (in.mouse_inside && (in.mouse_moved || in.mouse_click))
            for (int i = 0; i < 9; ++i) {
                Rect cr = cell(i);
                if (in.mouse_x >= cr.x && in.mouse_x < cr.x + cr.w && in.mouse_y >= cr.y && in.mouse_y < cr.y + cr.h) {
                    sel_ = i;
                    if (in.mouse_click) { open_menu(); return; }
                }
            }
        if (in.p(A_ACCEPT)) open_menu();
    }

    void draw(App& app) override {
        Render& rd = app.render;
        Theme& th = app.theme;
        th.draw_stone_bg(rd, app.time);
        draw_header(app, "SAVES", CANVAS_W / 2.f, 8, 0.8f);
        th.draw_panel(rd, {60, 84, 1160, 540}, 0.72f);
        for (int i = 0; i < 9; ++i) {
            Rect c = cell(i);
            bool foc = i == sel_;
            Slot& s = slots_[i];
            if (foc) { rd.fill(c, Color::hex(0xf9d64a, 36)); rd.line_rect(c, Color::hex(0xf9d64a), 2); }
            else rd.line_rect(c, Color::hex(0x6aa9b0, 90), 1);
            Rect tr{c.x + 8, c.y + 10, 160, 112};
            rd.fill(tr, Color::hex(0x050a12, 230));
            if (s.tex) rd.sprite(s.tex, nullptr, {tr.x, tr.y + (112 - 112) * 0.5f, 160, 112});
            else {
                TextStyle e = th.help_style(); e.align = Align::Center;
                draw_text(rd, *th.fonts.help, tr.x + 80, tr.y + 44, s.exists ? "(sem miniatura)" : "vazio", e);
            }
            rd.line_rect(tr, Color::hex(0x07111f), 2);
            TextStyle t = th.item_style(foc, true); t.scale = 0.8f;
            draw_text(rd, *th.fonts.menu, c.x + 178, c.y + 12, "SLOT " + std::to_string(i + 1), t);
            TextStyle hs = th.help_style();
            if (s.exists) {
                draw_text(rd, *th.fonts.help, c.x + 178, c.y + 52, s.when.substr(0, 10), hs);
                draw_text(rd, *th.fonts.help, c.x + 178, c.y + 76, s.when.substr(11), hs);
                draw_text(rd, *th.fonts.help, c.x + 178, c.y + 100, std::to_string(s.size / 1024) + " KB", hs);
            } else draw_text(rd, *th.fonts.help, c.x + 178, c.y + 52, "—", hs);
        }
        {
            std::string hp = slots_[sel_].exists ? "Enter: ações do slot (jogar a partir dele, exportar, importar, apagar)." : "Slot vazio. Enter: importar um savestate (.bin) para este slot.";
            TextStyle hs = th.help_style(); hs.align = Align::Center;
            draw_text(rd, *th.fonts.help, CANVAS_W / 2.f, 636, hp, hs);
        }
        draw_help_bar(app, default_hints(app, app.input.state()));

        if (menu_open_) {
            rd.fill({0, 0, CANVAS_W, CANVAS_H}, Color::hex(0x040a14, 160));
            Rect p{400, 190, 480, 330};
            th.draw_panel(rd, p, 0.95f);
            TextStyle g = th.gold_style(0.5f); g.align = Align::Center;
            draw_text(rd, *th.fonts.title, CANVAS_W / 2.f, p.y + 14, "SLOT " + std::to_string(sel_ + 1), g);
            menu_.draw(app);
        }
    }

private:
    Rect cell(int i) const { return {80.f + (i % 3) * 380, 98.f + (i / 3) * 178, 366, 132}; }
    fs::path bin(App& a, int n) const { return a.exe_dir / ("native_save_" + std::to_string(n) + ".bin"); }
    fs::path thumb(App& a, int n) const { return a.exe_dir / ("native_save_" + std::to_string(n) + ".bin.thumb"); }

    void free_tex() { for (auto& s : slots_) if (s.tex) { SDL_DestroyTexture(s.tex); s.tex = nullptr; } }

    void refresh(App& app) {
        free_tex();
        std::error_code ec;
        for (int i = 0; i < 9; ++i) {
            Slot& s = slots_[i];
            s = Slot{};
            fs::path b = bin(app, i + 1);
            s.exists = fs::is_regular_file(b, ec);
            if (!s.exists) continue;
            s.size = fs::file_size(b, ec);
            // data/hora local da ultima gravacao
            WIN32_FILE_ATTRIBUTE_DATA fa;
            if (GetFileAttributesExW(b.c_str(), GetFileExInfoStandard, &fa)) {
                FILETIME lf; SYSTEMTIME st;
                FileTimeToLocalFileTime(&fa.ftLastWriteTime, &lf);
                FileTimeToSystemTime(&lf, &st);
                char buf[40]; snprintf(buf, sizeof buf, "%02d/%02d/%04d %02d:%02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond);
                s.when = buf;
            }
            std::ifstream f(thumb(app, i + 1), std::ios::binary);
            char magic[8];
            std::vector<uint32_t> px((size_t)TW * TH);
            if (f && f.read(magic, 8) && !memcmp(magic, "GTHUMB1", 7) && f.read((char*)px.data(), (std::streamsize)px.size() * 4)) {
                s.has_thumb = true;
                SDL_Texture* t = SDL_CreateTexture(app.render.sdl, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, TW, TH);
                if (t) {
                    SDL_UpdateTexture(t, nullptr, px.data(), TW * 4);
                    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
                    s.tex = t;
                }
            }
        }
    }

    void open_menu() {
        menu_.items.clear();
        bool ex = slots_[sel_].exists;
        menu_.items.push_back({"Jogar a partir deste slot", ex, ID_PLAY});
        menu_.items.push_back({"Exportar…", ex, ID_EXPORT});
        menu_.items.push_back({"Importar…", true, ID_IMPORT});
        menu_.items.push_back({"Apagar", ex, ID_DELETE});
        menu_.items.push_back({"Cancelar", true, ID_CANCEL});
        menu_.x = CANVAS_W / 2.f; menu_.y = 262; menu_.row_h = 44; menu_.width = 420;
        menu_.focus = 0; menu_.set_focus(ex ? 0 : 2);
        menu_open_ = true;
    }

    void update_menu(App& app, const InputState& in) {
        if (in.p(A_BACK) || in.mouse_right_click) { menu_open_ = false; return; }
        int a = menu_.update(app, in);
        if (a < 0) return;
        int slot = sel_ + 1;
        switch (menu_.items[a].id) {
        case ID_PLAY:
            menu_open_ = false;
            app.load_slot_arg = std::to_string(slot);
            request_play(app);
            break;
        case ID_EXPORT: {
            menu_open_ = false;
            std::string p = pick_save_file(app, L"Exportar savestate", L"Savestate (*.bin)\0*.bin\0Todos (*.*)\0*.*\0", L"bin");
            if (!p.empty()) {
                std::error_code ec;
                fs::copy_file(bin(app, slot), utf8_to_path(p), fs::copy_options::overwrite_existing, ec);
                app.toast(ec ? "Falha ao exportar." : "Slot " + std::to_string(slot) + " exportado.");
            }
            break;
        }
        case ID_IMPORT: {
            menu_open_ = false;
            std::string p = pick_open_file(app, L"Importar savestate", L"Savestate (*.bin)\0*.bin\0Todos (*.*)\0*.*\0", app.exe_dir);
            if (!p.empty()) {
                std::error_code ec;
                fs::copy_file(utf8_to_path(p), bin(app, slot), fs::copy_options::overwrite_existing, ec);
                fs::remove(thumb(app, slot), ec);   // miniatura antiga nao vale para o novo estado
                app.toast(ec ? "Falha ao importar." : "Importado para o slot " + std::to_string(slot) + ".");
                refresh(app);
            }
            break;
        }
        case ID_DELETE: {
            menu_open_ = false;
            app.push(make_confirm("APAGAR SLOT " + std::to_string(slot), "Apagar o savestate do slot " + std::to_string(slot) + "? Isso não pode ser desfeito.", "Apagar", [slot, this](App& ap) {
                std::error_code ec;
                fs::remove(bin(ap, slot), ec);
                fs::remove(thumb(ap, slot), ec);
                ap.toast("Slot " + std::to_string(slot) + " apagado.");
                refresh(ap);
            }));
            break;
        }
        default: menu_open_ = false; break;
        }
    }

    Slot slots_[9];
    int sel_ = 0;
    bool menu_open_ = false;
    MenuList menu_;
};
}  // namespace

std::unique_ptr<Screen> make_saves_screen() { return std::make_unique<Saves>(); }
