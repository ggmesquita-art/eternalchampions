#include "theme.h"
#include <algorithm>
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
    fonts.logo = make_system_font(r, L"Georgia", 84, true);
    fonts.title = make_system_font(r, L"Georgia", 54, true);
    fonts.menu = make_system_font(r, L"Segoe UI", 30, true);
    fonts.help = make_system_font(r, L"Segoe UI", 20, false);
    if (!fonts.logo || !fonts.title || !fonts.menu || !fonts.help) return false;
    gen_stone(r);
    gen_yinyang(r);
    gen_halo(r);
    if (!assets_dir.empty()) load_manifest(r, assets_dir);
    return stone_ && yy_ && halo_;
}

void Theme::shutdown() {
    for (SDL_Texture** t : {&stone_, &yy_, &halo_, &logo_png_, &splash_png_})
        if (*t) { SDL_DestroyTexture(*t); *t = nullptr; }
}

void Theme::load_manifest(Render& r, const std::string& dir) {
    std::string j = read_text(dir + "/manifest.json");
    if (j.empty()) return;
    auto tex = [&](const char* key, int* w = nullptr, int* h = nullptr) -> SDL_Texture* {
        std::string f = json_str(j, key);
        return f.empty() ? nullptr : r.load_png_texture(dir + "/" + f, w, h);
    };
    if (SDL_Texture* t = tex("stone_tile", &stone_w_, &stone_h_)) { SDL_DestroyTexture(stone_); stone_ = t; extracted_art = true; }
    if (SDL_Texture* t = tex("yinyang")) { SDL_DestroyTexture(yy_); yy_ = t; extracted_art = true; }
    if (SDL_Texture* t = tex("logo", &logo_w_, &logo_h_)) { logo_png_ = t; extracted_art = true; }
    if (SDL_Texture* t = tex("splash_bg")) { splash_png_ = t; extracted_art = true; }
    std::string fp = json_str(j, "font_png"), fc = json_str(j, "font_charmap");
    if (!fp.empty() && !fc.empty()) {
        if (auto bf = load_bitmap_font(r, dir + "/" + fp, dir + "/" + fc)) {
            fonts.menu = std::move(bf);  // L5: fonte pixel substitui a do sistema
            extracted_art = true;
        }
    }
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
    r.tile(stone_, stone_w_, stone_h_, -t * 6.f, -t * 3.f, {0, 0, CANVAS_W, CANVAS_H});
    // vinheta para dar foco ao conteudo
    r.fill_gradient_v({0, 0, CANVAS_W, 90}, Color::hex(0x08182a, 150), Color::hex(0x08182a, 0));
    r.fill_gradient_v({0, CANVAS_H - 110, CANVAS_W, 110}, Color::hex(0x08182a, 0), Color::hex(0x08182a, 170));
}

void Theme::draw_dusk_bg(Render& r, float t) {
    if (splash_png_) { r.sprite(splash_png_, nullptr, {0, 0, CANVAS_W, CANVAS_H}); return; }
    r.fill_gradient_v({0, 0, CANVAS_W, CANVAS_H}, pal.dusk_top, pal.dusk_bottom);
    float pulse = 1.f + 0.03f * std::sin(t * 1.2f);
    float hw = 980 * pulse, hh = 760 * pulse;
    r.sprite(halo_, nullptr, {CANVAS_W / 2 - hw / 2, 250 - hh / 2, hw, hh}, 0, 210);
    r.fill_gradient_v({0, CANVAS_H - 160, CANVAS_W, 160}, Color::hex(0x2a1f5e, 0), Color::hex(0x2a1f5e, 190));
}

void Theme::draw_yinyang(Render& r, float cx, float cy, float size, double angle, uint8_t alpha) {
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
