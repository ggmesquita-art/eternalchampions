#include "screens.h"
#include "../widgets.h"

namespace {
class Confirm : public Screen {
public:
    Confirm(std::string title, std::string msg, std::string label, std::function<void(App&)> cb)
        : title_(std::move(title)), msg_(std::move(msg)), cb_(std::move(cb)) {
        menu_.items = {{"Cancelar", true, 0}, {std::move(label), true, 1}};
        menu_.row_h = 44; menu_.set_focus(0);
    }
    bool transparent() const override { return true; }
    void update(App& app, const InputState& in, float) override {
        if (in.p(A_BACK) || in.mouse_right_click) { app.pop(); return; }
        // opcoes lado a lado: esquerda/direita tambem movem o foco
        if (in.p(A_LEFT)) menu_.set_focus(0);
        if (in.p(A_RIGHT)) menu_.set_focus(1);
        int a = menu_.update(app, in);
        if (a < 0) return;
        app.pop();
        if (menu_.items[a].id == 1 && cb_) cb_(app);
    }
    void draw(App& app) override {
        Render& r = app.render;
        Theme& th = app.theme;
        r.fill({0, 0, CANVAS_W, CANVAS_H}, Color::hex(0x040a14, 170));
        Rect p{320, 200, 640, 300};
        th.draw_panel(r, p, 0.92f);
        TextStyle g = th.gold_style(0.55f); g.align = Align::Center;
        draw_text(r, *th.fonts.title, CANVAS_W / 2.f, p.y + 18, title_, g);
        TextStyle body = th.help_style(); body.align = Align::Center;
        float y = p.y + 92;
        for (auto& l : wrap_text(*th.fonts.help, msg_, p.w - 80)) { draw_text(r, *th.fonts.help, CANVAS_W / 2.f, y, l, body); y += 28; }
        menu_.x = CANVAS_W / 2.f; menu_.width = 360;
        menu_.y = p.y + p.h - 112;
        menu_.draw(app);
    }
private:
    std::string title_, msg_;
    std::function<void(App&)> cb_;
    MenuList menu_;
};
}  // namespace

std::unique_ptr<Screen> make_confirm(const std::string& t, const std::string& m, const std::string& l, std::function<void(App&)> cb) {
    return std::make_unique<Confirm>(t, m, l, std::move(cb));
}
