#include "screens.h"
#include "../widgets.h"
#include <cmath>

namespace {
class Splash : public Screen {
public:
    void update(App& app, const InputState& in, float dt) override {
        t_ += dt;
        bool go = in.any_input || in.mouse_click || in.p(A_START);
        if (!app.shot_mode && app.rom_exists && t_ > 3.f) go = true;   // ROM ja configurada: some sozinha em 3 s
        if (go && t_ > 0.15f) app.replace(make_main_menu());
    }
    void draw(App& app) override {
        Render& r = app.render;
        Theme& th = app.theme;
        th.draw_dusk_bg(r, app.time);
        // emblema yin-yang grande, translucido, girando devagar atras do logo
        th.draw_yinyang(r, CANVAS_W / 2.f, 330, 520, app.time * 6.0, 46);
        float h = th.draw_logo(r, CANVAS_W / 2.f, 150, 1.0f);
        TextStyle sub = TextStyle::solid(Color::hex(0xf3ecff), 2, Color::hex(0x2a1f5e));
        sub.align = Align::Center;
        draw_text(r, *th.fonts.menu, CANVAS_W / 2.f, 150 + h + 6, "RECOMP  LAUNCHER", sub);
        // PRESS START piscando
        float blink = 0.5f + 0.5f * std::sin(app.time * 5.f);
        TextStyle ps = th.gold_style(0.8f);
        ps.alpha = (uint8_t)(70 + 185 * blink);
        ps.align = Align::Center;
        draw_text(r, *th.fonts.title, CANVAS_W / 2.f, 520, "PRESS START", ps);
        TextStyle hs = TextStyle::solid(Color::hex(0xf3ecff), 1, Color::hex(0x2a1f5e));
        hs.align = Align::Center;
        draw_text(r, *th.fonts.help, CANVAS_W / 2.f, CANVAS_H - 40, "v" EC_LAUNCHER_VERSION, hs);
    }
private:
    float t_ = 0;
};
}  // namespace

std::unique_ptr<Screen> make_splash() { return std::make_unique<Splash>(); }
