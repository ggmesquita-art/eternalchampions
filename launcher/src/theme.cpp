#include "theme.h"
#include "json.h"
#include "png_io.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

uint32_t hash2(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float rnd(uint32_t x, uint32_t y, uint32_t seed) { return (hash2(x, y, seed) & 0xffff) / 65535.f; }

// value noise periodico: periodo (px,py) em celulas de lattice
float vnoise(float x, float y, int px, int py, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    auto L = [&](int a, int b) { return rnd(((a % px) + px) % px, ((b % py) + py) % py, seed); };
    float a = L(xi, yi), b = L(xi + 1, yi), c = L(xi, yi + 1), d = L(xi + 1, yi + 1);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

float fbm(float u, float v, int cx, int cy, uint32_t seed) {  // u,v em [0,1) (periodico)
    float s = 0, amp = 0.5f, norm = 0;
    int px = cx, py = cy;
    for (int o = 0; o < 5; ++o) {
        s += amp * vnoise(u * px, v * py, px, py, seed + o);
        norm += amp; amp *= 0.5f; px *= 2; py *= 2;
    }
    return s / norm;
}

Color ramp3(Color a, Color b, Color c, float t) {
    t = std::min(1.f, std::max(0.f, t));
    return t < 0.5f ? lerp(a, b, t * 2) : lerp(b, c, (t - 0.5f) * 2);
}

std::string read_text(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

// extrai "key": "valor" (JSON plano, suficiente para o manifest)
std::string json_str(const std::string& j, const std::string& key) {
    size_t k = j.find("\"" + key + "\"");
    if (k == std::string::npos) return "";
    size_t c = j.find(':', k);
    if (c == std::string::npos) return "";
    size_t a = j.find('"', c);
    if (a == std::string::npos) return "";
    size_t b = j.find('"', a + 1);
    if (b == std::string::npos) return "";
    return j.substr(a + 1, b - a - 1);
}

}  // namespace

bool Theme::init(Render& r, const std::string& assets_dir) {
    fonts.logo = make_system_font(r, "Georgia", 84, true);
    fonts.title = make_system_font(r, "Georgia", 54, true);
    fonts.menu = make_system_font(r, "Segoe UI", 30, true);
    fonts.help = make_system_font(r, "Segoe UI", 20, false);
    if (!fonts.logo || !fonts.title || !fonts.menu || !fonts.help) return false;
    gen_stone(r);
    gen_yinyang(r);
    gen_halo(r);
    if (!assets_dir.empty()) load_art(r, assets_dir);
    return stone_ && yy_ && halo_;
}

void Theme::shutdown() {
    free_art();
    for (SDL_Texture** t : {&stone_, &yy_, &halo_})
        if (*t) { SDL_DestroyTexture(*t); *t = nullptr; }
}

bool Theme::reload_art(Render& r, const std::string& dir) {
    free_art();
    if (!dir.empty()) load_art(r, dir);
    return extracted_art;
}

void Theme::free_art() {
    for (SDL_Texture** t : {&logo_png_, &splash_png_, &header_art_, &yy_art_[0], &yy_art_[1], &stone_art_})
        if (*t) { SDL_DestroyTexture(*t); *t = nullptr; }
    fonts.pixel.reset();
    extracted_art = false;
}

void Theme::load_art(Render& r, const std::string& dir) {
    JVal m;
    if (!json_parse_file(dir + "/manifest.json", m) || (int)m.n("schema") != 1) return;
    const JVal* files = m.get("files");
    if (!files) return;
    auto entry = [&](const char* key) -> const JVal* { return files->get(key); };
    auto png4 = [&](const char* key, std::vector<uint8_t>& px, int& w, int& h) {
        const JVal* e = entry(key);
        if (!e) return false;
        std::string f = e->s("file_4x", e->s("file"));
        return !f.empty() && png_read_rgba(dir + "/" + f, px, w, h);
    };
    auto tex4 = [&](const char* key, int* w, int* h) -> SDL_Texture* {
        std::vector<uint8_t> px; int iw = 0, ih = 0;
        if (!png4(key, px, iw, ih)) return nullptr;
        if (w) *w = iw;
        if (h) *h = ih;
        return r.make_texture(px.data(), iw, ih, true);
    };
    // pedra: bloco A | bloco B lado a lado (periodo de 2 blocos, texture_period_x do manifest)
    {
        std::vector<uint8_t> a, b; int aw = 0, ah = 0, bw = 0, bh = 0;
        if (png4("stone_tile", a, aw, ah)) {
            if (!png4("stone_tile_alt", b, bw, bh) || bw != aw || bh != ah) { b = a; bw = aw; bh = ah; }
            std::vector<uint8_t> comp((size_t)aw * 2 * ah * 4);
            for (int y = 0; y < ah; ++y) {
                memcpy(&comp[(size_t)y * aw * 2 * 4], &a[(size_t)y * aw * 4], (size_t)aw * 4);
                memcpy(&comp[((size_t)y * aw * 2 + aw) * 4], &b[(size_t)y * aw * 4], (size_t)aw * 4);
            }
            stone_art_ = r.make_texture(comp.data(), aw * 2, ah, false);
            stone_art_w_ = aw * 2; stone_art_h_ = ah;
        }
    }
    logo_png_ = tex4("logo", &logo_w_, &logo_h_);
    header_art_ = tex4("header_main_menu", &header_w_, &header_h_);
    yy_art_[0] = tex4("yinyang_00", nullptr, nullptr);
    yy_art_[1] = tex4("yinyang_01", nullptr, nullptr);
    for (SDL_Texture* t : yy_art_) if (t) SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
    {
        std::vector<uint8_t> px; int w = 0, h = 0;
        if (png4("title_splash", px, w, h)) {
            splash_png_ = r.make_texture(px.data(), w, h, true);
            splash_w_ = w; splash_h_ = h;
            auto avg = [&](int y0, int y1) {
                long sr = 0, sg = 0, sb = 0, n = 0;
                for (int y = y0; y < y1; ++y) for (int x = 0; x < 6; ++x) { const uint8_t* p = &px[((size_t)y * w + x) * 4]; sr += p[0]; sg += p[1]; sb += p[2]; ++n; }
                return Color{(uint8_t)(sr / n), (uint8_t)(sg / n), (uint8_t)(sb / n), 255};
            };
            splash_edge_top_ = avg(0, 40);
            splash_edge_bot_ = avg(h - 40, h);
        }
    }
    if (const JVal* fm = entry("font_menu")) {
        std::string f = fm->s("file"), j = fm->s("json");
        if (!f.empty() && !j.empty()) fonts.pixel = load_bitmap_font(r, dir + "/" + f, dir + "/" + j);
    }
    extracted_art = stone_art_ || logo_png_ || header_art_ || yy_art_[0] || splash_png_ || fonts.pixel;
}

void Theme::gen_stone(Render& r) {
    const int W = 512, H = 384, BW = 128, BH = 96;
    std::vector<uint8_t> px((size_t)W * H * 4);
    for (int y = 0; y < H; ++y) {
        int row = y / BH, ly = y % BH;
        for (int x = 0; x < W; ++x) {
            int xo = (x + ((row & 1) ? BW / 2 : 0)) % W;
            int bx = xo / BW, lx = xo % BW;
            float tone = rnd(bx, row, 7) * 0.22f - 0.11f;
            float u = (float)x / W, v = (float)y / H;
            float n = fbm(u, v, 4, 3, 11);
            float warp = fbm(u, v, 8, 6, 23);
            float marble = 0.5f + 0.5f * std::sin(6.2831853f * 2 * u + 6.0f * n + 4.0f * warp);
            float vein = std::pow(1.f - std::fabs(2 * marble - 1), 6.f);   // veios finos claros
            float t = 0.15f + 0.65f * n + tone + 0.22f * vein;
            float fine = rnd(x, y, 3) * 0.05f - 0.025f;
            Color c = ramp3(pal.stone_dark, pal.stone_mid, pal.stone_light, t + fine);
            int de = std::min(std::min(lx, BW - 1 - lx), std::min(ly, BH - 1 - ly));
            if (de < 3) {
                c = lerp(pal.stone_grout, c, 0.0f);
                c = lerp(c, Color::hex(0x0f2540), rnd(x, y, 5) * 0.4f);
            } else if (de < 6) {
                bool light_side = (lx < BW / 2 && lx <= ly * BW / BH) || (ly < BH / 2 && ly < lx * BH / BW);
                bool nearest_top_left = std::min(lx, ly) < std::min(BW - 1 - lx, BH - 1 - ly);
                (void)light_side;
                float k = (6 - de) / 3.f * 0.45f;
                c = nearest_top_left ? lerp(c, pal.stone_light, k) : lerp(c, pal.stone_grout, k * 1.1f);
            }
            uint8_t* d = &px[((size_t)y * W + x) * 4];
            d[0] = c.r; d[1] = c.g; d[2] = c.b; d[3] = 255;
        }
    }
    stone_ = r.make_texture(px.data(), W, H, true);
    stone_w_ = W; stone_h_ = H;
}

void Theme::gen_yinyang(Render& r) {
    const int S = 160, SS = 3;
    std::vector<uint8_t> px((size_t)S * S * 4, 0);
    const float R = S * 0.5f - 1.5f;
    const Color dark = Color::hex(0x14233a), light = Color::hex(0xf4efe0), ring = Color::hex(0xd9a21f);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float ar = 0, ag = 0, ab = 0, aa = 0;
            for (int sy = 0; sy < SS; ++sy)
                for (int sx = 0; sx < SS; ++sx) {
                    float fx = x + (sx + 0.5f) / SS - S * 0.5f, fy = y + (sy + 0.5f) / SS - S * 0.5f;
                    float d = std::sqrt(fx * fx + fy * fy);
                    if (d > R) continue;
                    Color c;
                    if (d > R - 3.5f) c = (d > R - 1.6f) ? dark : ring;
                    else {
                        bool white = fx < 0;
                        float dt = std::sqrt(fx * fx + (fy + R / 2) * (fy + R / 2));
                        float db = std::sqrt(fx * fx + (fy - R / 2) * (fy - R / 2));
                        if (dt < R / 2) white = true;
                        if (db < R / 2) white = false;
                        if (dt < R / 8) white = false;
                        if (db < R / 8) white = true;
                        c = white ? light : dark;
                    }
                    ar += c.r; ag += c.g; ab += c.b; aa += 255;
                }
            int n = SS * SS;
            uint8_t* d = &px[((size_t)y * S + x) * 4];
            if (aa > 0) {
                float k = n * 255.f / aa * 255.f / 255.f;
                (void)k;
                float cnt = aa / 255.f;
                d[0] = (uint8_t)(ar / cnt); d[1] = (uint8_t)(ag / cnt); d[2] = (uint8_t)(ab / cnt);
                d[3] = (uint8_t)(aa / n);
            }
        }
    yy_ = r.make_texture(px.data(), S, S, true);
}

void Theme::gen_halo(Render& r) {
    const int S = 256;
    std::vector<uint8_t> px((size_t)S * S * 4);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            float dx = (x + 0.5f) / S * 2 - 1, dy = (y + 0.5f) / S * 2 - 1;
            float d = std::min(1.f, std::sqrt(dx * dx + dy * dy));
            float a = std::pow(1.f - d, 1.8f);
            uint8_t* p = &px[((size_t)y * S + x) * 4];
            p[0] = pal.dusk_halo.r; p[1] = pal.dusk_halo.g; p[2] = pal.dusk_halo.b; p[3] = (uint8_t)(a * 255);
        }
    halo_ = r.make_texture(px.data(), S, S, true);
}

void Theme::draw_stone_bg(Render& r, float t) {
    if (stone_art_) r.tile(stone_art_, stone_art_w_, stone_art_h_, 0, 0, {0, 0, CANVAS_W, CANVAS_H});
    else r.tile(stone_, stone_w_, stone_h_, -t * 6.f, -t * 3.f, {0, 0, CANVAS_W, CANVAS_H});
    // vinheta para dar foco ao conteudo
    r.fill_gradient_v({0, 0, CANVAS_W, 90}, Color::hex(0x08182a, 150), Color::hex(0x08182a, 0));
    r.fill_gradient_v({0, CANVAS_H - 110, CANVAS_W, 110}, Color::hex(0x08182a, 0), Color::hex(0x08182a, 170));
}

void Theme::draw_dusk_bg(Render& r, float t) {
    r.fill_gradient_v({0, 0, CANVAS_W, CANVAS_H}, pal.dusk_top, pal.dusk_bottom);
    float pulse = 1.f + 0.03f * std::sin(t * 1.2f);
    float hw = 980 * pulse, hh = 760 * pulse;
    r.sprite(halo_, nullptr, {CANVAS_W / 2 - hw / 2, 250 - hh / 2, hw, hh}, 0, 210);
    r.fill_gradient_v({0, CANVAS_H - 160, CANVAS_W, 160}, Color::hex(0x2a1f5e, 0), Color::hex(0x2a1f5e, 190));
}

void Theme::draw_yinyang(Render& r, float cx, float cy, float size, double angle, uint8_t alpha) {
    if (yy_art_[0] && yy_art_[1]) {   // arte do jogo: nao gira; alterna os 2 frames a cada 60 quadros
        r.sprite(yy_art_[(int)std::floor(time) & 1], nullptr, {std::floor(cx - size / 2), std::floor(cy - size / 2), size, size}, 0, alpha);
        return;
    }
    r.sprite(yy_, nullptr, {cx - size / 2, cy - size / 2, size, size}, angle, alpha);
}

void Theme::draw_panel(Render& r, const Rect& rc, float darken) {
    r.fill(rc, Color::hex(0x0b1c30, (uint8_t)(255 * darken)));
    r.fill_gradient_v({rc.x + 4, rc.y + 4, rc.w - 8, 26}, Color::hex(0x7cc3bd, 28), Color::hex(0x7cc3bd, 0));
    r.line_rect(rc, Color::hex(0x07111f), 3);
    r.bevel_frame({rc.x + 3, rc.y + 3, rc.w - 6, rc.h - 6}, Color::hex(0x6aa9b0), Color::hex(0x143247), 3);
    r.line_rect({rc.x + 9, rc.y + 9, rc.w - 18, rc.h - 18}, Color::hex(0x07111f, 140), 1);
}

TextStyle Theme::gold_style(float scale) const {
    TextStyle s;
    s.top = pal.gold_top; s.bottom = pal.gold_bottom;
    s.outline = pal.gold_outline; s.outline_px = 3 * scale > 3 ? 3 : 2;
    s.shadow = Color::hex(0x000000, 120); s.shadow_dx = 3; s.shadow_dy = 4;
    s.scale = scale;
    return s;
}

TextStyle Theme::item_style(bool focused, bool enabled) const {
    Color c = !enabled ? pal.text_disabled : (focused ? pal.text_select : pal.text_white);
    TextStyle s = TextStyle::solid(c, 2, pal.text_outline);
    s.shadow = Color::hex(0, 110); s.shadow_dx = 2; s.shadow_dy = 3;
    return s;
}

TextStyle Theme::help_style() const {
    TextStyle s = TextStyle::solid(Color::hex(0xe8f4f2), 1, Color::hex(0x0b1c30));
    return s;
}

float Theme::draw_logo(Render& r, float cx, float y, float scale) {
    if (logo_png_) {
        float w = logo_w_ * scale, h = logo_h_ * scale;
        r.sprite(logo_png_, nullptr, {cx - w / 2, y, w, h});
        return h;
    }
    TextStyle s;
    s.top = pal.logo_red; s.bottom = pal.logo_orange;
    s.outline = Color::hex(0x2a0f1c); s.outline_px = 3;
    s.shadow = pal.logo_shadow; s.shadow_dx = 5 * scale; s.shadow_dy = 6 * scale;
    s.scale = scale; s.align = Align::Center;
    draw_text(r, *fonts.logo, cx, y, "ETERNAL CHAMPIONS", s);
    return fonts.logo->line_height() * scale;
}

void Theme::draw_header_art(Render& r, float cx, float y, float scale) {
    if (!header_art_) return;
    float w = header_w_ * scale, h = header_h_ * scale;
    r.sprite(header_art_, nullptr, {std::floor(cx - w / 2), y, w, h});
}

void Theme::draw_splash_art(Render& r) {
    if (!splash_png_) return;
    // cover 16:9 por altura (a arte e 4:3); nas laterais, o degrade roxo da propria borda da imagem
    float h = CANVAS_H, w = splash_w_ * h / splash_h_;
    float x = (CANVAS_W - w) / 2;
    r.fill_gradient_v({0, 0, CANVAS_W, CANVAS_H}, splash_edge_top_, splash_edge_bot_);
    r.sprite(splash_png_, nullptr, {x, 0, w, h});
    const float f = 70;   // feather das bordas da imagem
    auto strip = [&](float x0, float x1, bool left) {
        Color t0 = splash_edge_top_, b0 = splash_edge_bot_;
        Color tt = left ? t0 : t0.with_alpha(0), tb = left ? b0 : b0.with_alpha(0);
        Color t1 = left ? t0.with_alpha(0) : t0, b1 = left ? b0.with_alpha(0) : b0;
        SDL_Vertex v[4] = {{{x0, 0}, tt.sdl(), {0, 0}}, {{x1, 0}, t1.sdl(), {0, 0}}, {{x1, (float)CANVAS_H}, b1.sdl(), {0, 0}}, {{x0, (float)CANVAS_H}, tb.sdl(), {0, 0}}};
        static const int idx[6] = {0, 1, 2, 0, 2, 3};
        r.geometry(nullptr, v, 4, idx, 6);
    };
    strip(x, x + f, true);
    strip(x + w - f, x + w, false);
}
