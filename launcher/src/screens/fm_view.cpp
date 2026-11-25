#include "fm_view.h"
#include "../widgets.h"
#include <algorithm>
#include <cmath>

void draw_line(Render& r, float x0, float y0, float x1, float y1, float th, Color c) {
    float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f) return;
    float nx = -dy / len * th * 0.5f, ny = dx / len * th * 0.5f;
    SDL_Vertex v[4] = {{{x0 + nx, y0 + ny}, c.sdl(), {0, 0}}, {{x1 + nx, y1 + ny}, c.sdl(), {0, 0}},
                       {{x1 - nx, y1 - ny}, c.sdl(), {0, 0}}, {{x0 - nx, y0 - ny}, c.sdl(), {0, 0}}};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    r.geometry(nullptr, v, 4, idx, 6);
}

namespace {

// Algoritmos do YM2612 em termos de OP1..OP4 (indice 0..3): arestas (de -> para; para = 4 = saida).
struct Edge { int a, b; };
const std::vector<Edge>& alg_edges(int alg) {
    static const std::vector<Edge> t[8] = {
        {{0, 1}, {1, 2}, {2, 3}, {3, 4}},                    // 0: 1->2->3->4
        {{0, 2}, {1, 2}, {2, 3}, {3, 4}},                    // 1: (1+2)->3->4
        {{0, 3}, {1, 2}, {2, 3}, {3, 4}},                    // 2: (1+(2->3))->4
        {{0, 1}, {1, 3}, {2, 3}, {3, 4}},                    // 3: ((1->2)+3)->4
        {{0, 1}, {1, 4}, {2, 3}, {3, 4}},                    // 4: (1->2)+(3->4)
        {{0, 1}, {0, 2}, {0, 3}, {1, 4}, {2, 4}, {3, 4}},    // 5: 1->(2,3,4)
        {{0, 1}, {1, 4}, {2, 4}, {3, 4}},                    // 6: (1->2)+3+4
        {{0, 4}, {1, 4}, {2, 4}, {3, 4}}};                   // 7: 1+2+3+4
    return t[std::max(0, std::min(7, alg))];
}

// posicoes (coluna, linha) dos 4 operadores no diagrama, por algoritmo
void alg_layout(int alg, float px[4], float py[4]) {
    // colunas 0..3, linhas 0..2
    static const float L[8][4][2] = {
        {{0, 1}, {1, 1}, {2, 1}, {3, 1}}, {{0, 0}, {0, 2}, {1, 1}, {2, 1}}, {{0, 0}, {0, 2}, {1, 2}, {2, 1}},
        {{0, 0}, {1, 0}, {1, 2}, {2, 1}}, {{0, 0}, {1, 0}, {0, 2}, {1, 2}}, {{0, 1}, {1, 0}, {1, 1}, {1, 2}},
        {{0, 0}, {1, 0}, {1, 1}, {1, 2}}, {{0, 0}, {0, 1}, {0, 2}, {0, 3}}};
    for (int i = 0; i < 4; ++i) { px[i] = L[alg][i][0]; py[i] = L[alg][i][1]; }
}

}  // namespace

void draw_fm_patch(App& app, const Rect& a, const ecr::VoiceMeta& m) {
    Render& r = app.render; Theme& th = app.theme;
    TextStyle hs = th.help_style();
    if (!m.valid) { draw_text(r, *th.fonts.help, a.x + 10, a.y + 10, "Sem dados de patch para este item.", hs); return; }
    const Color gold = Color::hex(0xffe63a), box = Color::hex(0x10131a), line = Color::hex(0xdde6ee);
    draw_text(r, *th.fonts.help, a.x + 8, a.y + 2, "ALG " + std::to_string(m.alg) + "  \xC2\xB7  FB " + std::to_string(m.fb), TextStyle::solid(gold, 1, th.pal.text_outline));

    // algoritmo
    {
        const float cw = 54, rh = 30, ox = a.x + 24, oy = a.y + 34;
        float px[4], py[4];
        alg_layout(m.alg, px, py);
        auto C = [&](int i, float& x, float& y) { x = ox + px[i] * cw * 1.25f + 18; y = oy + py[i] * rh + 10; };
        const float outx = ox + 4 * cw * 1.25f + 4;
        for (auto& e : alg_edges(m.alg)) {
            float x0, y0, x1, y1;
            C(e.a, x0, y0);
            if (e.b == 4) { x1 = outx; y1 = oy + 1.5f * rh + 10; } else C(e.b, x1, y1);
            draw_line(r, x0 + 18, y0, x1 - (e.b == 4 ? 0 : 18), y1, 2, line);
        }
        for (int i = 0; i < 4; ++i) {
            float x, y; C(i, x, y);
            bool car = i < (int)m.ops.size() && m.ops[i].carrier;
            r.fill({x - 18, y - 11, 36, 22}, car ? Color::hex(0x2a6a3a) : box);
            r.line_rect({x - 18, y - 11, 36, 22}, car ? Color::hex(0x6fe38a) : line, 2);
            TextStyle ls = TextStyle::solid(Color::hex(0xffffff), 0); ls.align = Align::Center; ls.scale = 0.5f;
            draw_text(r, *th.fonts.menu, x, y - 8, "OP" + std::to_string(i + 1), ls);
        }
        // saida + feedback no OP1
        draw_text(r, *th.fonts.help, outx + 6, oy + 1.5f * rh + 1, "→ saída", hs);
        float fx, fy; C(0, fx, fy);
        draw_line(r, fx - 18, fy, fx - 18 - 8, fy, 2, Color::hex(0xf9d64a));
        draw_text(r, *th.fonts.help, fx - 52, fy - 11, "fb", hs);
    }
    // envelopes
    const float ey = a.y + 150, ew = (a.w - 24) / 2.f, eh = 70;
    for (int i = 0; i < 4 && i < (int)m.ops.size(); ++i) {
        const ecr::FmOp& o = m.ops[i];
        Rect pr{a.x + 8 + (i % 2) * (ew + 8), ey + (i / 2) * (eh + 40), ew, eh};
        r.fill(pr, Color::hex(0x07111f, 230));
        r.line_rect(pr, Color::hex(0x6aa9b0, 140), 1);
        // modelo qualitativo do envelope: A (ar), D1 (d1r) ate SL, D2 (d2r), R (rr)
        float ta = (32 - std::min(31, o.ar)) / 32.f * 0.22f + 0.01f;
        float lvl = 1.f - o.sl / 15.f;
        float td1 = o.d1r == 0 ? 0.35f : (32 - std::min(31, o.d1r)) / 32.f * 0.4f + 0.04f;
        float td2 = 0.3f;
        float tr = (16 - std::min(15, o.rr)) / 16.f * 0.2f + 0.03f;
        float t0 = 0, t1 = ta, t2 = t1 + td1, t3 = t2 + td2, t4 = t3 + tr;
        float k = (pr.w - 8) / t4;
        auto X = [&](float t) { return pr.x + 4 + t * k; };
        auto Y = [&](float v) { return pr.y + pr.h - 6 - v * (pr.h - 12); };
        float l2 = o.d2r ? lvl * 0.55f : lvl;
        Color c = o.carrier ? Color::hex(0x6fe38a) : Color::hex(0xf9d64a);
        draw_line(r, X(t0), Y(0), X(t1), Y(1), 2, c);
        draw_line(r, X(t1), Y(1), X(t2), Y(o.d1r == 0 ? 1 : lvl), 2, c);
        draw_line(r, X(t2), Y(o.d1r == 0 ? 1 : lvl), X(t3), Y(l2), 2, c);
        draw_line(r, X(t3), Y(l2), X(t4), Y(0), 2, c.with_alpha(160));
        TextStyle ps = TextStyle::solid(Color::hex(0xcfe0e8), 0); ps.scale = 0.5f;
        char buf[96];
        snprintf(buf, sizeof buf, "OP%d %s  MUL %d DT %d  TL %s", i + 1, o.carrier ? "(car)" : "(mod)", o.mul, o.dt, o.tl < 0 ? "-" : std::to_string(o.tl).c_str());
        draw_text(r, *th.fonts.menu, pr.x + 4, pr.y + pr.h + 1, buf, ps);
        snprintf(buf, sizeof buf, "AR %d D1R %d D2R %d SL %d RR %d", o.ar, o.d1r, o.d2r, o.sl, o.rr);
        draw_text(r, *th.fonts.menu, pr.x + 4, pr.y + pr.h + 16, buf, ps);
    }
}
