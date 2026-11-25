#include "screens.h"
#include "../widgets.h"

namespace {
class About : public Screen {
public:
    void update(App& app, const InputState& in, float) override {
        if (in.p(A_BACK) || in.p(A_ACCEPT) || in.mouse_right_click) app.pop();
        if (in.p(A_START)) request_play(app);
    }
    void draw(App& app) override {
        Render& r = app.render;
        Theme& th = app.theme;
        th.draw_stone_bg(r, app.time);
        draw_header(app, "SOBRE", CANVAS_W / 2.f, 40);
        Rect p{150, 130, 980, 500};
        th.draw_panel(r, p);
        TextStyle head = th.item_style(true, true);
        TextStyle body = th.help_style();
        float x = p.x + 40, y = p.y + 28;
        draw_text(r, *th.fonts.menu, x, y, "Eternal Champions Recomp \xE2\x80\x94 Launcher", head);
        y += 42;
        const char* lines[] = {
            "Vers\xC3\xA3o " EC_LAUNCHER_VERSION,
            "Port nativo recompilado de Eternal Champions (Mega Drive). Este projeto n\xC3\xA3o distribui a ROM:",
            "voc\xC3\xAA fornece a sua (vers\xC3\xA3o US, validada por SHA-256).",
            "",
            "Licen\xC3\xA7""as de terceiros",
            "\xE2\x80\xA2 SDL2 \xE2\x80\x94 zlib        \xE2\x80\xA2 libpng / zlib \xE2\x80\x94 licen\xC3\xA7""as libpng e zlib",
            "\xE2\x80\xA2 TinySoundFont (tsf) \xE2\x80\x94 MIT        \xE2\x80\xA2 SoundFont GM do jogo: ver assets/soundfonts",
            "\xE2\x80\xA2 Shaders libretro \xE2\x80\x94 licen\xC3\xA7""as indicadas no cabe\xC3\xA7""alho de cada preset",
            "\xE2\x80\xA2 Fontes: as do sistema (Georgia, Segoe UI), rasterizadas em tempo de execu\xC3\xA7\xC3\xA3o;",
            "   nenhuma fonte \xC3\xA9 distribu\xC3\xAD""da. A arte do jogo \xC3\xA9 extra\xC3\xAD""da da sua ROM (L5).",
            "",
            "Eternal Champions \xC2\xA9 SEGA. Projeto de preserva\xC3\xA7\xC3\xA3o sem fins comerciais, n\xC3\xA3o afiliado \xC3\xA0 SEGA.",
        };
        for (const char* l : lines) {
            bool title = std::string(l).rfind("Licen", 0) == 0;
            draw_text(r, *th.fonts.help, x, y, l, title ? head : body);
            y += title ? 36 : 29;
        }
        draw_text(r, *th.fonts.help, x, p.y + p.h - 40, "Tema: " + app.art_status(), TextStyle::solid(Color::hex(0xcfe9e6), 1, th.pal.text_outline));
        draw_help_bar(app, "Esc / B: voltar   Start / F5: JOGAR");
    }
};
}  // namespace

std::unique_ptr<Screen> make_about_screen() { return std::make_unique<About>(); }
