#include "screens.h"
#include "../widgets.h"

namespace {
class Placeholder : public Screen {
public:
    explicit Placeholder(std::string title) : title_(std::move(title)) {}
    void update(App& app, const InputState& in, float) override {
        if (in.p(A_BACK) || in.p(A_ACCEPT) || in.mouse_click || in.mouse_right_click) app.pop();
        if (in.p(A_START)) request_play(app);
    }
    void draw(App& app) override {
        Render& r = app.render;
        Theme& th = app.theme;
        th.draw_stone_bg(r, app.time);
        draw_header(app, title_, CANVAS_W / 2.f, 70);
        Rect p{240, 190, 800, 300};
        th.draw_panel(r, p);
        th.draw_yinyang(r, CANVAS_W / 2.f, 290, 90, app.time * 40.0);
        TextStyle st = th.item_style(true, true);
        st.align = Align::Center;
        draw_text(r, *th.fonts.menu, CANVAS_W / 2.f, 365, "EM CONSTRU\xC3\x87\xC3\x83O", st);
        TextStyle hs = th.help_style(); hs.align = Align::Center;
        draw_text(r, *th.fonts.help, CANVAS_W / 2.f, 420, "Esta tela chega nas pr\xC3\xB3ximas etapas (L2/L3).", hs);
        draw_help_bar(app, "Esc / B: voltar   Start / F5: JOGAR");
    }
private:
    std::string title_;
};
}  // namespace

std::unique_ptr<Screen> make_placeholder(const std::string& title) { return std::make_unique<Placeholder>(title); }
