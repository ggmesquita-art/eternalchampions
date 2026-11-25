#include "screens.h"
#include "../launch.h"
#include "../widgets.h"

void request_play(App& app) {
    if (app.game_running) return;
    if (app.rom_path.empty() || !app.rom_exists) {
        app.toast("Nenhuma ROM configurada \xE2\x80\x94 escolha a ROM primeiro.");
        app.push(make_rom_screen());
        return;
    }
    app.push(make_game_run());
}

namespace {
enum Id { ID_PLAY, ID_ROM, ID_VIDEO, ID_ENH, ID_AUDIO, ID_STUDIO, ID_CONTROLS, ID_GAME, ID_SAVES, ID_ADV, ID_ABOUT, ID_QUIT };

class MainMenu : public Screen {
public:
    void on_enter(App&) override {
        const struct { const char* label; int id; } its[] = {
            {"JOGAR", ID_PLAY}, {"ROM", ID_ROM}, {"VÍDEO", ID_VIDEO}, {"APRIMORAMENTO", ID_ENH},
            {"ÁUDIO", ID_AUDIO}, {"ESTÚDIO DE REMASTER", ID_STUDIO}, {"CONTROLES", ID_CONTROLS},
            {"JOGO", ID_GAME}, {"SAVES", ID_SAVES}, {"AVANÇADO", ID_ADV}, {"SOBRE", ID_ABOUT}, {"SAIR", ID_QUIT}};
        for (auto& i : its) menu_.items.push_back({i.label, true, i.id});
        menu_.y = 160; menu_.row_h = 35; menu_.x = CANVAS_W / 2.f;
        menu_.set_focus(0);
    }
    void update(App& app, const InputState& in, float) override {
        if (in.p(A_START)) { request_play(app); return; }
        int a = menu_.update(app, in);
        if (a < 0) return;
        const MenuItem& it = menu_.items[a];
        switch (it.id) {
        case ID_PLAY: request_play(app); break;
        case ID_ROM: app.push(make_rom_screen()); break;
        case ID_ABOUT: app.push(make_about_screen()); break;
        case ID_QUIT: app.quit = true; break;
        default: app.push(make_placeholder(it.label)); break;
        }
    }
    void draw(App& app) override {
        Render& r = app.render;
        Theme& th = app.theme;
        th.draw_stone_bg(r, app.time);
        th.draw_logo(r, CANVAS_W / 2.f, 4, 0.72f);
        draw_header(app, "MAIN MENU", CANVAS_W / 2.f, 76, 0.85f);
        menu_.draw(app);
        // rodape: ROM atual + SHA
        Rect p{250, 590, 780, 80};
        th.draw_panel(r, p, 0.7f);
        TextStyle lab = th.help_style();
        TextStyle val = th.item_style(false, true);
        const Font& hf = *th.fonts.help;
        float x = p.x + 24, y = p.y + 12;
        draw_text(r, hf, x, y, "ROM:", lab);
        std::string name = app.rom_path.empty() ? "(nenhuma \xE2\x80\x94 escolha em ROM)" : app.rom_display_name();
        draw_text(r, hf, x + 56, y, name, val);
        y += 30;
        draw_text(r, hf, x, y, "SHA-256:", lab);
        float tx = x + 96;
        if (app.rom_path.empty() || !app.rom_exists) {
            TextStyle bad = TextStyle::solid(th.pal.bad, 1, th.pal.text_outline);
            draw_text(r, hf, tx, y, app.rom_path.empty() ? "\xE2\x80\x94" : "\xE2\x9C\x97  arquivo n\xC3\xA3o encontrado", bad);
        } else {
            bool ok = app.rom_valid;
            TextStyle ms = TextStyle::solid(ok ? th.pal.ok : th.pal.bad, 1, th.pal.text_outline);
            float w = draw_text(r, hf, tx, y, ok ? "\xE2\x9C\x93" : "\xE2\x9C\x97", ms);
            draw_text(r, hf, tx + w + 10, y, ok ? "confere (Eternal Champions US)" : "diferente da vers\xC3\xA3o US \xE2\x80\x94 pode n\xC3\xA3o funcionar", ms);
        }
        draw_help_bar(app, default_hints(app, app.input.state()));
    }
private:
    MenuList menu_;
};
}  // namespace

std::unique_ptr<Screen> make_main_menu() { return std::make_unique<MainMenu>(); }
