#include "widgets.h"
#include <algorithm>

void MenuList::set_focus(int i) {
    if (items.empty()) { focus = 0; return; }
    i = std::max(0, std::min((int)items.size() - 1, i));
    focus = i;
    if (!items[focus].enabled) {
        for (int k = 1; k < (int)items.size(); ++k) {
            int a = focus + k, b = focus - k;
            if (a < (int)items.size() && items[a].enabled) { focus = a; break; }
            if (b >= 0 && items[b].enabled) { focus = b; break; }
        }
    }
    if (focus < scroll_) scroll_ = focus;
    if (focus >= scroll_ + visible_rows) scroll_ = focus - visible_rows + 1;
}

void MenuList::move(int dir) {
    int n = (int)items.size();
    if (!n) return;
    int i = focus;
    for (int k = 0; k < n; ++k) {
        i = (i + dir + n) % n;
        if (items[i].enabled) { set_focus(i); return; }
    }
}

int MenuList::update(App& app, const InputState& in) {
    (void)app;
    if (items.empty()) return -1;
    if (in.p(A_UP)) move(-1);
    if (in.p(A_DOWN)) move(1);
    set_focus(focus);
    // mouse: hover (so quando move) e clique
    if (in.mouse_inside && (in.mouse_moved || in.mouse_click)) {
        for (int i = scroll_; i < std::min((int)items.size(), scroll_ + visible_rows); ++i) {
            float iy = item_y(i);
            float x0 = align == Align::Center ? x - width / 2 : x - 20;
            if (in.mouse_x >= x0 && in.mouse_x < x0 + width && in.mouse_y >= iy && in.mouse_y < iy + row_h) {
                if (items[i].enabled) {
                    focus = i;
                    if (in.mouse_click) return i;
                }
                break;
            }
        }
    }
    if (in.p(A_ACCEPT) && items[focus].enabled) return focus;
    return -1;
}

void MenuList::draw(App& app) {
    Theme& th = app.theme;
    const Font& f = *th.fonts.menu;
    float tw = f.line_height() * text_scale;
    int end = std::min((int)items.size(), scroll_ + visible_rows);
    for (int i = scroll_; i < end; ++i) {
        bool foc = (i == focus);
        TextStyle st = th.item_style(foc, items[i].enabled);
        st.scale = text_scale;
        st.align = align;
        float iy = item_y(i) + (row_h - tw) * 0.5f;
        float w = draw_text(app.render, f, x, iy, items[i].label, st);
        if (foc && yinyang_cursor) {
            float sz = std::min(row_h + 6, 40.f);
            float cy = item_y(i) + row_h * 0.5f;
            float lx = align == Align::Center ? x - w / 2 - 30 : x - 34;
            float rx = align == Align::Center ? x + w / 2 + 30 : x + w + 30;
            double ang = app.time * 60.0;
            th.draw_yinyang(app.render, lx, cy, sz, ang);
            th.draw_yinyang(app.render, rx, cy, sz, -ang);
        }
    }
    // indicadores de rolagem
    if (scroll_ > 0) draw_text(app.render, *th.fonts.help, x, y - 22, "\xE2\x96\xB2", [&] { auto s = th.help_style(); s.align = Align::Center; return s; }());
    if (end < (int)items.size()) draw_text(app.render, *th.fonts.help, x, y + visible_rows * row_h, "\xE2\x96\xBC", [&] { auto s = th.help_style(); s.align = Align::Center; return s; }());
}

void draw_option_row(App& app, const Rect& r, const std::string& label, const std::string& value,
                     bool focused, bool enabled, bool modified) {
    Theme& th = app.theme;
    const Font& f = *th.fonts.menu;
    if (focused) {
        app.render.fill(r, Color::hex(0xf9d64a, 40));
        app.render.line_rect(r, Color::hex(0xf9d64a, 150), 1);
    }
    TextStyle st = th.item_style(focused, enabled);
    float ty = r.y + (r.h - f.line_height()) * 0.5f;
    draw_text(app.render, f, r.x + 14, ty, (modified ? "\xE2\x97\x8F " : "") + label, st);
    // pontilhado entre rotulo e valor
    // valor com setas
    TextStyle vs = st; vs.align = Align::Right;
    std::string v = (focused && enabled ? "\xE2\x97\x84  " : "") + value + (focused && enabled ? "  \xE2\x96\xBA" : "");
    draw_text(app.render, f, r.x + r.w - 14, ty, v, vs);
}

void draw_help_bar(App& app, const std::string& text) {
    Render& r = app.render;
    Rect b{0, CANVAS_H - 38, CANVAS_W, 38};
    r.fill(b, Color::hex(0x07111f, 215));
    r.fill({0, b.y, CANVAS_W, 2}, Color::hex(0x6aa9b0, 200));
    TextStyle st = app.theme.help_style();
    st.align = Align::Center;
    draw_text(r, *app.theme.fonts.help, CANVAS_W / 2.f, b.y + 8, text, st);
}

std::string default_hints(App& app, const InputState& in, bool tabs) {
    (void)app;
    std::string s;
    if (in.using_pad) {
        s = "A: confirmar   B: voltar   X: padr\xC3\xA3o   Y: ajuda   Start: JOGAR";
        if (tabs) s = "LB/RB: abas   " + s;
    } else {
        s = "Enter: confirmar   Esc: voltar   X: padr\xC3\xA3o   F1: ajuda   F5: JOGAR";
        if (tabs) s = "Q/E: abas   " + s;
    }
    return s;
}

void draw_header(App& app, const std::string& title, float cx, float y, float scale) {
    Theme& th = app.theme;
    TextStyle st = th.gold_style(scale);
    st.align = Align::Center;
    draw_text(app.render, *th.fonts.title, cx, y, title, st);
}

std::vector<std::string> wrap_text(const Font& f, const std::string& text, float width) {
    std::vector<std::string> lines;
    std::string cur, word;
    auto flush_word = [&]() {
        if (word.empty()) return;
        std::string t = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && f.measure(t) > width) { lines.push_back(cur); cur = word; }
        else cur = t;
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') flush_word();
        else word += c;
    }
    flush_word();
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}
