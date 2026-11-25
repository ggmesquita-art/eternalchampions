#include "options_screen.h"
#include "../platform.h"
#include "../widgets.h"
#include "screens.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------
Row header_row(const std::string& t) { Row r; r.header = true; r.label = t; return r; }
Row info_row(const std::string& t) { Row r; r.info = true; r.label = t; return r; }
Row action_row(const std::string& label, const std::string& help, std::function<void(App&)> fn, bool rebuild) {
    Row r; r.label = label; r.help = help; r.activate = std::move(fn); r.arrows = false; r.rebuild = rebuild; return r;
}

static std::string fmt_num(double v, int prec) {
    char b[48]; snprintf(b, sizeof b, "%.*f", prec, v); return b;
}

static std::string short_path(const std::string& p) {
    if (p.empty()) return "(padrão)";
    std::string n = path_to_utf8(utf8_to_path(p).filename());
    return n.empty() ? p : n;
}

Row option_row(App& app, const Opt& o) {
    (void)app;
    Row r;
    r.label = o.label; r.help = o.help; r.restart = o.restart;
    const Opt* po = &o;
    r.modified = [po](App& a) { return a.cfg.is_modified(*po); };
    r.reset = [po](App& a) { a.cfg.reset(*po); };
    r.enabled = [po](App& a) { return a.cfg.enabled(*po); };
    switch (o.type) {
    case OType::Bool:
        r.value = [po](App& a) { return a.cfg.get(*po) == "1" ? std::string("Ligado") : std::string("Desligado"); };
        r.adjust = [po](App& a, int) { a.cfg.set(*po, a.cfg.get(*po) == "1" ? "0" : "1"); };
        break;
    case OType::Int:
    case OType::Float:
        r.value = [po](App& a) {
            std::string cv = a.cfg.get(*po);
            if (cv.empty()) return std::string("Padrão");
            double v = atof(cv.c_str());
            return fmt_num(v, po->type == OType::Int ? 0 : po->precision);
        };
        r.adjust = [po](App& a, int dir) {
            std::string cv = a.cfg.get(*po);
            double v = (cv.empty() && !po->empty_start.empty()) ? atof(po->empty_start.c_str()) : atof(cv.c_str()) + dir * po->step;
            v = std::min(po->max, std::max(po->min, v));
            v = std::round(v / po->step) * po->step;
            a.cfg.set(*po, fmt_num(v, po->type == OType::Int ? 0 : std::max(po->precision, 2)));
        };
        break;
    case OType::Enum:
        r.value = [po](App& a) {
            std::string cur = a.cfg.get(*po);
            for (auto& c : a.cfg.choices(*po, a)) if (c.value == cur) return c.label;
            return cur;
        };
        r.adjust = [po](App& a, int dir) {
            auto ch = a.cfg.choices(*po, a);
            if (ch.empty()) return;
            std::string cur = a.cfg.get(*po);
            int i = 0;
            for (int k = 0; k < (int)ch.size(); ++k) if (ch[k].value == cur) i = k;
            i = (i + dir + (int)ch.size()) % (int)ch.size();
            a.cfg.set(*po, ch[i].value);
        };
        r.rebuild = (o.dyn != Dyn::None) || o.id == "ec.bplus.enabled";
        break;
    case OType::Path:
    case OType::Folder:
    case OType::SaveFile:
        r.arrows = false;
        r.value = [po](App& a) { return short_path(a.cfg.get(*po)); };
        r.activate = [po](App& a) {
            std::string p;
            if (po->type == OType::Folder) p = pick_folder(a, L"Escolher pasta");
            else if (po->type == OType::SaveFile) p = pick_save_file(a, L"Gravar como", L"WAV (*.wav)\0*.wav\0Todos (*.*)\0*.*\0", L"wav");
            else p = pick_open_file(a, L"Escolher arquivo", L"Todos os arquivos (*.*)\0*.*\0", a.root_dir);
            if (!p.empty()) a.cfg.set(*po, p);
        };
        r.rebuild = true;
        break;
    }
    return r;
}

void reset_options(App& app, const std::string& tab) {
    for (const Opt& o : schema())
        if (tab.empty() || o.tab == tab) app.cfg.reset(o);
    app.cfg.ensure_skip_launcher();
    app.cfg.save();
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------
namespace {
class OptionsScreen : public Screen {
public:
    OptionsScreen(std::string title, PageBuilder b) : title_(std::move(title)), build_(std::move(b)) {}

    void on_enter(App& app) override {
        rebuild(app);
        if (app.shot_mode) {   // captura: --tab N / --focus N
            page_ = std::min(app.shot_page, (int)pages_.size() - 1);
            focus_ = -1; scroll_ = 0; move(+1);
            for (int k = 0; k < app.shot_focus; ++k) move(+1);
        }
    }
    void on_resume(App& app) override { rebuild(app); }

    void update(App& app, const InputState& in, float) override {
        if (in.p(A_START)) { request_play(app); return; }
        if (in.p(A_BACK) || in.mouse_right_click) {
            if (in.mouse_right_click && hover_ >= 0 && cur().rows[hover_].adjust) { do_adjust(app, hover_, -1); return; }
            app.cfg.save();
            app.pop();
            return;
        }
        if (pages_.size() > 1) {
            if (in.p(A_TABPREV)) switch_page(app, -1);
            if (in.p(A_TABNEXT)) switch_page(app, +1);
        }
        if (in.p(A_HELP)) show_help_ = !show_help_;
        Page& pg = cur();
        if (in.p(A_UP)) move(-1);
        if (in.p(A_DOWN)) move(+1);

        // mouse
        hover_ = -1;
        if (in.mouse_inside) {
            for (int i = scroll_; i < std::min((int)pg.rows.size(), scroll_ + visible_); ++i) {
                float y = row_y(i);
                if (in.mouse_x >= panel_.x && in.mouse_x < panel_.x + panel_.w && in.mouse_y >= y && in.mouse_y < y + row_h_ && selectable(app, pg.rows[i])) {
                    hover_ = i;
                    if (in.mouse_moved || in.mouse_click) focus_ = i;
                    if (in.mouse_click) { do_activate(app, i); return; }
                }
            }
        }
        if (focus_ < 0 || focus_ >= (int)pg.rows.size() || !selectable(app, pg.rows[focus_])) move(+1);
        if (pg.rows.empty() || focus_ < 0 || focus_ >= (int)pg.rows.size()) return;   // pagina sem linha selecionavel
        Row& r = pg.rows[focus_];
        if (in.p(A_LEFT) && r.adjust && enabled(app, r)) do_adjust(app, focus_, -1);
        else if (in.p(A_RIGHT) && r.adjust && enabled(app, r)) do_adjust(app, focus_, +1);
        else if (in.p(A_ACCEPT) && enabled(app, r)) do_activate(app, focus_);
        else if (in.p(A_RESET) && r.reset && enabled(app, r)) { r.reset(app); if (r.rebuild) rebuild(app); }
    }

    void draw(App& app) override {
        Render& rd = app.render;
        Theme& th = app.theme;
        th.draw_stone_bg(rd, app.time);
        draw_header(app, title_, CANVAS_W / 2.f, 8, 0.8f);
        // abas
        if (pages_.size() > 1) {
            float total = 0;
            std::vector<float> w;
            for (auto& p : pages_) { w.push_back(th.fonts.menu->measure(p.tab) + 44); total += w.back(); }
            float x = CANVAS_W / 2.f - total / 2;
            for (size_t i = 0; i < pages_.size(); ++i) {
                bool on = (int)i == page_;
                if (on) { rd.fill({x + 4, 80, w[i] - 8, 34}, Color::hex(0x07111f, 200)); rd.line_rect({x + 4, 80, w[i] - 8, 34}, Color::hex(0xf9d64a), 2); }
                TextStyle st = th.item_style(on, true); st.align = Align::Center; st.scale = 0.8f;
                draw_text(rd, *th.fonts.menu, x + w[i] / 2, 81, pages_[i].tab, st);
                x += w[i];
            }
            TextStyle ar = th.item_style(true, true); ar.align = Align::Center; ar.scale = 0.8f;
            draw_text(rd, *th.fonts.menu, CANVAS_W / 2.f - total / 2 - 22, 81, "◄", ar);
            draw_text(rd, *th.fonts.menu, CANVAS_W / 2.f + total / 2 + 22, 81, "►", ar);
        }
        th.draw_panel(rd, panel_, 0.72f);
        Page& pg = cur();
        const Font& mf = *th.fonts.menu;
        int end = std::min((int)pg.rows.size(), scroll_ + visible_);
        for (int i = scroll_; i < end; ++i) {
            Row& r = pg.rows[i];
            float y = row_y(i);
            Rect rr{panel_.x + 18, y, panel_.w - 36, row_h_};
            if (r.header) {
                TextStyle g = th.gold_style(0.4f);
                draw_text(rd, *th.fonts.help, rr.x + 8, y + 8, r.label, [&] { TextStyle s = TextStyle::solid(th.pal.gold_top, 1, th.pal.gold_outline); return s; }());
                rd.fill({rr.x + 8, y + row_h_ - 6, rr.w - 16, 2}, Color::hex(0xf9d64a, 90));
                (void)g;
                continue;
            }
            if (r.info) {
                draw_text(rd, *th.fonts.help, rr.x + 8, y + 8, r.label, th.help_style());
                continue;
            }
            bool foc = (i == focus_);
            bool en = enabled(app, r);
            bool mod = r.modified && r.modified(app);
            std::string val = r.value ? r.value(app) : "";
            // linha
            if (foc) { rd.fill(rr, Color::hex(0xf9d64a, 38)); rd.line_rect(rr, Color::hex(0xf9d64a, 150), 1); }
            TextStyle st = th.item_style(foc, en);
            st.scale = 0.78f;
            float ty = y + (row_h_ - mf.line_height() * st.scale) * 0.5f;
            float lw = draw_text(rd, mf, rr.x + 34, ty, r.label, st);
            TextStyle bad = TextStyle::solid(Color::hex(0xf9d64a), 1, th.pal.text_outline); bad.scale = 0.78f;
            if (mod) draw_text(rd, mf, rr.x + 12, ty, "•", bad);
            if (r.restart) draw_text(rd, mf, rr.x + 34 + lw + 10, ty, "↻", bad);
            TextStyle vs = st; vs.align = Align::Right;
            if (en && r.tint.a) { vs.top = vs.bottom = Color{r.tint.r, r.tint.g, r.tint.b, 255}; }
            std::string txt = val;
            if (foc && en && r.adjust && r.arrows) txt = "◄  " + val + "  ►";
            draw_text(rd, mf, rr.x + rr.w - 14, ty, txt, vs);
        }
        if (pg.extra) pg.extra(app, Rect{panel_.x + 18, panel_.y + 12 + (float)pg.rows.size() * row_h_ + 6, panel_.w - 36, panel_.h - 24 - (float)pg.rows.size() * row_h_});
        // rolagem
        if (scroll_ > 0) draw_text(rd, *th.fonts.help, panel_.x + panel_.w - 30, panel_.y + 4, "▲", th.help_style());
        if (end < (int)pg.rows.size()) draw_text(rd, *th.fonts.help, panel_.x + panel_.w - 30, panel_.y + panel_.h - 28, "▼", th.help_style());

        // ajuda do item focado (2 linhas) + barra de atalhos
        std::string help = (focus_ >= 0 && focus_ < (int)pg.rows.size()) ? pg.rows[focus_].help : "";
        if (!help.empty()) {
            auto lines = wrap_text(*th.fonts.help, help, CANVAS_W - 120);
            TextStyle hs = th.help_style(); hs.align = Align::Center;
            for (size_t k = 0; k < lines.size() && k < 2; ++k)
                draw_text(rd, *th.fonts.help, CANVAS_W / 2.f, panel_.y + panel_.h + 6 + k * 24.f, lines[k], hs);
        }
        draw_help_bar(app, default_hints(app, app.input.state(), pages_.size() > 1));
        if (show_help_ && !help.empty()) {
            rd.fill({0, 0, CANVAS_W, CANVAS_H}, Color::hex(0x040a14, 150));
            Rect hp{260, 220, 760, 240};
            th.draw_panel(rd, hp, 0.95f);
            TextStyle g = th.gold_style(0.5f); g.align = Align::Center;
            draw_text(rd, *th.fonts.title, CANVAS_W / 2.f, hp.y + 14, "AJUDA", g);
            float y = hp.y + 90;
            TextStyle hs = th.help_style(); hs.align = Align::Center;
            for (auto& l : wrap_text(*th.fonts.help, help, hp.w - 70)) { draw_text(rd, *th.fonts.help, CANVAS_W / 2.f, y, l, hs); y += 28; }
        }
    }

private:
    Page& cur() { return pages_[page_]; }
    float row_y(int i) const { return panel_.y + 12 + (i - scroll_) * row_h_; }
    bool enabled(App& a, const Row& r) { return !r.enabled || r.enabled(a); }
    bool selectable(App& a, const Row& r) { (void)a; return !r.header && !r.info; }

    void move(int dir) {
        Page& pg = cur();
        int n = (int)pg.rows.size();
        if (!n) return;
        int i = focus_;
        for (int k = 0; k < n; ++k) {
            i = (i + dir + n) % n;
            if (!pg.rows[i].header && !pg.rows[i].info) { focus_ = i; break; }
        }
        if (focus_ < scroll_) scroll_ = focus_;
        if (focus_ >= scroll_ + visible_) scroll_ = focus_ - visible_ + 1;
        // mostra o cabecalho logo acima, se houver
        if (scroll_ > 0 && focus_ == scroll_ && pg.rows[scroll_ - 1].header) --scroll_;
        if (focus_ < scroll_) scroll_ = focus_;
    }
    void switch_page(App& app, int d) {
        saved_focus_[page_] = focus_;
        page_ = (page_ + d + (int)pages_.size()) % (int)pages_.size();
        focus_ = saved_focus_[page_]; scroll_ = 0;
        Page& pg = cur();
        // foco salvo -1 = aba nunca visitada: rows[-1] era lido aqui (e depois chamado em update), derrubando o launcher
        if (focus_ < 0 || focus_ >= (int)pg.rows.size() || pg.rows[focus_].header || pg.rows[focus_].info) { focus_ = -1; move(+1); }
        (void)app;
        if (focus_ >= scroll_ + visible_) scroll_ = focus_ - visible_ + 1;
        app.fade = 0.08f;
    }
    void do_adjust(App& app, int i, int dir) {
        Row& r = cur().rows[i];
        bool rb = r.rebuild;
        r.adjust(app, dir);
        if (rb) rebuild(app);
    }
    void do_activate(App& app, int i) {
        Row& r = cur().rows[i];
        if (!enabled(app, r)) return;
        bool rb = r.rebuild;
        if (r.activate) r.activate(app);
        else if (r.adjust) r.adjust(app, +1);
        if (rb) rebuild(app);
    }
    void rebuild(App& app) {
        int keep_page = page_, keep_focus = focus_;
        pages_ = build_(app);
        if (pages_.empty()) pages_.push_back({"", {}});
        page_ = std::min(keep_page, (int)pages_.size() - 1);
        saved_focus_.resize(pages_.size(), -1);
        focus_ = keep_focus;
        Page& pg = cur();
        if (focus_ < 0 || focus_ >= (int)pg.rows.size() || pg.rows[focus_].header || pg.rows[focus_].info) { focus_ = -1; move(+1); }
        if (focus_ >= scroll_ + visible_) scroll_ = focus_ - visible_ + 1;
        if (scroll_ > std::max(0, (int)pg.rows.size() - 1)) scroll_ = 0;
    }

    std::string title_;
    PageBuilder build_;
    std::vector<Page> pages_;
    std::vector<int> saved_focus_;
    int page_ = 0, focus_ = -1, scroll_ = 0, hover_ = -1;
    bool show_help_ = false;
    const Rect panel_{90, 122, 1100, 500};
    const float row_h_ = 40;
    const int visible_ = 12;
};
}  // namespace

std::unique_ptr<Screen> make_options_screen(const std::string& title, PageBuilder b) {
    return std::make_unique<OptionsScreen>(title, std::move(b));
}
