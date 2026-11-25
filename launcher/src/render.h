// Canvas virtual 1280x720 em render target, escalado para a janela com letterbox.
#pragma once
#include <SDL.h>
#include <cstdint>
#include <string>
#include <vector>

constexpr int CANVAS_W = 1280;
constexpr int CANVAS_H = 720;

struct Color {
    uint8_t r = 255, g = 255, b = 255, a = 255;
    static Color hex(uint32_t rgb, uint8_t a = 255) {
        return Color{(uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb, a};
    }
    Color with_alpha(uint8_t na) const { Color c = *this; c.a = na; return c; }
    SDL_Color sdl() const { return SDL_Color{r, g, b, a}; }
};
Color lerp(Color a, Color b, float t);

struct Rect { float x, y, w, h; };

class Render {
public:
    bool init(int win_w, int win_h, bool hidden, const char* title);
    void shutdown();

    SDL_Window* window = nullptr;
    SDL_Renderer* sdl = nullptr;
    SDL_Texture* canvas = nullptr;
    bool integer_scale = false;

    // Quadro: begin() desenha no canvas; end() escala para a janela (e apresenta se present).
    void begin();
    void end(bool present);

    // Primitivas (coordenadas do canvas)
    void fill(const Rect& r, Color c);
    void fill_gradient_v(const Rect& r, Color top, Color bottom);
    void fill_gradient_h(const Rect& r, Color left, Color right);
    void bevel_frame(const Rect& r, Color light, Color dark, int thick = 2);   // moldura chanfrada (3D)
    void line_rect(const Rect& r, Color c, int thick = 1);
    void sprite(SDL_Texture* t, const SDL_Rect* src, const Rect& dst, double angle = 0, uint8_t alpha = 255,
                Color mod = Color{});
    void tile(SDL_Texture* t, int tw, int th, float ox, float oy, const Rect& area);
    void geometry(SDL_Texture* t, const SDL_Vertex* v, int nv, const int* idx, int ni);
    void set_clip(const Rect* r);

    SDL_Texture* make_texture(const uint8_t* rgba, int w, int h, bool linear = true);
    SDL_Texture* load_png_texture(const std::string& path, int* w = nullptr, int* h = nullptr);

    // Janela -> canvas. Retorna false se fora da area do canvas (letterbox).
    bool window_to_canvas(int wx, int wy, float& cx, float& cy) const;
    bool read_canvas(std::vector<uint8_t>& rgba);  // RGBA do canvas inteiro

private:
    SDL_FRect dest_rect() const;
};
