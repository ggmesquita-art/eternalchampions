#include "render.h"
#include "png_io.h"
#include <algorithm>
#include <cmath>

Color lerp(Color a, Color b, float t) {
    t = std::min(1.f, std::max(0.f, t));
    auto m = [&](uint8_t x, uint8_t y) { return (uint8_t)(x + (y - x) * t + 0.5f); };
    return Color{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}

bool Render::init(int ww, int wh, bool hidden, const char* title) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    Uint32 flags = SDL_WINDOW_RESIZABLE | (hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, flags);
    if (!window) return false;
    Uint32 rf = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE | (hidden ? 0 : SDL_RENDERER_PRESENTVSYNC);
    sdl = SDL_CreateRenderer(window, -1, rf);
    if (!sdl) sdl = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
    if (!sdl) return false;
    canvas = SDL_CreateTexture(sdl, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, CANVAS_W, CANVAS_H);
    if (!canvas) return false;
    SDL_SetTextureBlendMode(canvas, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(canvas, SDL_ScaleModeLinear);
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    return true;
}

void Render::shutdown() {
    if (canvas) SDL_DestroyTexture(canvas);
    if (sdl) SDL_DestroyRenderer(sdl);
    if (window) SDL_DestroyWindow(window);
    canvas = nullptr; sdl = nullptr; window = nullptr;
}

void Render::begin() {
    SDL_SetRenderTarget(sdl, canvas);
    SDL_SetRenderDrawBlendMode(sdl, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl, 0, 0, 0, 255);
    SDL_RenderClear(sdl);
}

SDL_FRect Render::dest_rect() const {
    int ww = 0, wh = 0;
    SDL_GetRendererOutputSize(sdl, &ww, &wh);
    float s = std::min((float)ww / CANVAS_W, (float)wh / CANVAS_H);
    if (integer_scale && s >= 1.f) s = std::floor(s);
    float w = CANVAS_W * s, h = CANVAS_H * s;
    return SDL_FRect{(ww - w) * 0.5f, (wh - h) * 0.5f, w, h};
}

void Render::end(bool present) {
    SDL_SetRenderTarget(sdl, nullptr);
    if (!present) return;
    SDL_SetRenderDrawColor(sdl, 0, 0, 0, 255);
    SDL_RenderClear(sdl);
    SDL_FRect d = dest_rect();
    SDL_RenderCopyF(sdl, canvas, nullptr, &d);
    SDL_RenderPresent(sdl);
}

bool Render::window_to_canvas(int wx, int wy, float& cx, float& cy) const {
    int ww = 0, wh = 0, ow = 0, oh = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_GetRendererOutputSize(sdl, &ow, &oh);
    float fx = ww ? (float)ow / ww : 1.f, fy = wh ? (float)oh / wh : 1.f;
    SDL_FRect d = dest_rect();
    cx = ((wx * fx) - d.x) / d.w * CANVAS_W;
    cy = ((wy * fy) - d.y) / d.h * CANVAS_H;
    return cx >= 0 && cy >= 0 && cx < CANVAS_W && cy < CANVAS_H;
}

void Render::fill(const Rect& r, Color c) {
    SDL_SetRenderDrawColor(sdl, c.r, c.g, c.b, c.a);
    SDL_FRect f{r.x, r.y, r.w, r.h};
    SDL_RenderFillRectF(sdl, &f);
}

void Render::geometry(SDL_Texture* t, const SDL_Vertex* v, int nv, const int* idx, int ni) {
    SDL_RenderGeometry(sdl, t, v, nv, idx, ni);
}

void Render::fill_gradient_v(const Rect& r, Color top, Color bottom) {
    SDL_Vertex v[4] = {
        {{r.x, r.y}, top.sdl(), {0, 0}},
        {{r.x + r.w, r.y}, top.sdl(), {0, 0}},
        {{r.x + r.w, r.y + r.h}, bottom.sdl(), {0, 0}},
        {{r.x, r.y + r.h}, bottom.sdl(), {0, 0}}};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    geometry(nullptr, v, 4, idx, 6);
}

void Render::fill_gradient_h(const Rect& r, Color l, Color rr) {
    SDL_Vertex v[4] = {
        {{r.x, r.y}, l.sdl(), {0, 0}},
        {{r.x + r.w, r.y}, rr.sdl(), {0, 0}},
        {{r.x + r.w, r.y + r.h}, rr.sdl(), {0, 0}},
        {{r.x, r.y + r.h}, l.sdl(), {0, 0}}};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    geometry(nullptr, v, 4, idx, 6);
}

void Render::line_rect(const Rect& r, Color c, int t) {
    fill({r.x, r.y, r.w, (float)t}, c);
    fill({r.x, r.y + r.h - t, r.w, (float)t}, c);
    fill({r.x, r.y + t, (float)t, r.h - 2.f * t}, c);
    fill({r.x + r.w - t, r.y + t, (float)t, r.h - 2.f * t}, c);
}

void Render::bevel_frame(const Rect& r, Color light, Color dark, int t) {
    // luz em cima/esquerda, sombra em baixo/direita, com cantos chanfrados (45 graus)
    fill({r.x + t, r.y, r.w - 2.f * t, (float)t}, light);
    fill({r.x, r.y + t, (float)t, r.h - 2.f * t}, light);
    fill({r.x + t, r.y + r.h - t, r.w - 2.f * t, (float)t}, dark);
    fill({r.x + r.w - t, r.y + t, (float)t, r.h - 2.f * t}, dark);
    Color mid = lerp(light, dark, 0.5f);
    SDL_Vertex a[3] = {{{r.x, r.y + t}, light.sdl(), {0, 0}}, {{r.x + t, r.y}, light.sdl(), {0, 0}},
                       {{r.x + t, r.y + t}, mid.sdl(), {0, 0}}};
    SDL_Vertex b[3] = {{{r.x + r.w - t, r.y + r.h}, dark.sdl(), {0, 0}}, {{r.x + r.w, r.y + r.h - t}, dark.sdl(), {0, 0}},
                       {{r.x + r.w - t, r.y + r.h - t}, mid.sdl(), {0, 0}}};
    static const int idx[3] = {0, 1, 2};
    geometry(nullptr, a, 3, idx, 3);
    geometry(nullptr, b, 3, idx, 3);
}

void Render::sprite(SDL_Texture* t, const SDL_Rect* src, const Rect& dst, double angle, uint8_t alpha, Color mod) {
    if (!t) return;
    SDL_SetTextureAlphaMod(t, alpha);
    SDL_SetTextureColorMod(t, mod.r, mod.g, mod.b);
    SDL_FRect d{dst.x, dst.y, dst.w, dst.h};
    SDL_RenderCopyExF(sdl, t, src, &d, angle, nullptr, SDL_FLIP_NONE);
}

void Render::tile(SDL_Texture* t, int tw, int th, float ox, float oy, const Rect& area) {
    if (!t || tw <= 0 || th <= 0) return;
    set_clip(&area);
    float sx = std::fmod(ox, (float)tw); if (sx > 0) sx -= tw;
    float sy = std::fmod(oy, (float)th); if (sy > 0) sy -= th;
    for (float y = area.y + sy; y < area.y + area.h; y += th)
        for (float x = area.x + sx; x < area.x + area.w; x += tw) {
            SDL_FRect d{x, y, (float)tw, (float)th};
            SDL_RenderCopyF(sdl, t, nullptr, &d);
        }
    set_clip(nullptr);
}

void Render::set_clip(const Rect* r) {
    if (!r) { SDL_RenderSetClipRect(sdl, nullptr); return; }
    SDL_Rect c{(int)r->x, (int)r->y, (int)r->w, (int)r->h};
    SDL_RenderSetClipRect(sdl, &c);
}

SDL_Texture* Render::make_texture(const uint8_t* rgba, int w, int h, bool linear) {
    SDL_Texture* t = SDL_CreateTexture(sdl, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, rgba, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    return t;
}

SDL_Texture* Render::load_png_texture(const std::string& path, int* w, int* h) {
    std::vector<uint8_t> px; int iw = 0, ih = 0;
    if (!png_read_rgba(path, px, iw, ih)) return nullptr;
    if (w) *w = iw;
    if (h) *h = ih;
    return make_texture(px.data(), iw, ih, true);
}

bool Render::read_canvas(std::vector<uint8_t>& rgba) {
    rgba.assign((size_t)CANVAS_W * CANVAS_H * 4, 0);
    SDL_SetRenderTarget(sdl, canvas);
    int rc = SDL_RenderReadPixels(sdl, nullptr, SDL_PIXELFORMAT_ABGR8888, rgba.data(), CANVAS_W * 4);
    SDL_SetRenderTarget(sdl, nullptr);
    for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
    return rc == 0;
}
