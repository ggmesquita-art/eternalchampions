// Tema visual (PLAN §3): paleta, fundo de pedra procedural, yin-yang, paineis, splash.
//
// Arte extraida da ROM (L5): Theme::init/reload_art leem `assets_dir/manifest.json` (schema 1 do
// tools/launcher/extract_ui_assets.py): files.<chave>.file_4x (stone_tile, stone_tile_alt, logo,
// header_main_menu, title_splash, yinyang_00/01) e font_menu {file,json}. Qualquer item ausente/ilegivel
// mantem o procedural correspondente; has_art() diz se ha arte carregada.
#pragma once
#include "font.h"
#include "render.h"
#include <memory>
#include <string>

struct Palette {
    Color stone_light = Color::hex(0x7cc3bd), stone_mid = Color::hex(0x4f9fa6), stone_dark = Color::hex(0x2f6f86);
    Color stone_grout = Color::hex(0x1d3a5a);
    Color logo_red = Color::hex(0xd42a24), logo_orange = Color::hex(0xf6a43a), logo_shadow = Color::hex(0x4b1a3f);
    Color gold_top = Color::hex(0xf9d64a), gold_bottom = Color::hex(0xd9742a), gold_outline = Color::hex(0x3a1a10);
    Color text_white = Color::hex(0xffffff), text_outline = Color::hex(0x101018);
    Color text_select = Color::hex(0xffe63a), text_disabled = Color::hex(0x8aa0a8);
    Color dusk_top = Color::hex(0x5a4aa8), dusk_bottom = Color::hex(0xa796dc), dusk_halo = Color::hex(0xe8d8ff);
    Color kanji_circle = Color::hex(0xf3d23a), kanji_ink = Color::hex(0x1f3a2a);
    Color ok = Color::hex(0x6fe38a), bad = Color::hex(0xff6a5a);
};

struct Fonts {
    std::unique_ptr<Font> logo;    // logo/titulos grandes
    std::unique_ptr<Font> title;   // cabecalho ("MAIN MENU")
    std::unique_ptr<Font> menu;    // itens
    std::unique_ptr<Font> help;    // ajuda/rodape
    std::unique_ptr<Font> pixel;   // fonte pixel do jogo (arte extraida) - null no tema procedural
};

class Theme {
public:
    ~Theme() { shutdown(); }
    bool init(Render& r, const std::string& assets_dir);
    bool reload_art(Render& r, const std::string& assets_dir);   // descarta e recarrega a arte extraida
    bool has_art() const { return extracted_art; }
    bool has_header_art() const { return header_art_ != nullptr; }
    bool has_splash_art() const { return splash_png_ != nullptr; }
    float time = 0;                      // segundos (animacao do cursor: troca de frame a cada 60 quadros)
    void shutdown();

    Palette pal;
    Fonts fonts;
    bool extracted_art = false;

    // Fundos
    void draw_stone_bg(Render& r, float t);                       // pedra marmorizada em mosaico (parallax lento)
    void draw_dusk_bg(Render& r, float t);                        // degrade roxo + halo (splash/modais)
    // Elementos
    void draw_yinyang(Render& r, float cx, float cy, float size, double angle_deg, uint8_t alpha = 255);
    void draw_panel(Render& r, const Rect& rc, float darken = 0.6f);  // moldura de pedra escura
    float draw_logo(Render& r, float cx, float y, float scale = 1.f);  // logo provisorio (texto) ou PNG; retorna altura
    void draw_header_art(Render& r, float cx, float y, float scale);   // "MAIN MENU" original (so com arte)
    void draw_splash_art(Render& r);                                   // title_splash em cover 16:9 com laterais roxas
    TextStyle gold_style(float scale = 1.f) const;
    TextStyle item_style(bool focused, bool enabled) const;
    TextStyle help_style() const;

private:
    void gen_stone(Render& r);
    void gen_yinyang(Render& r);
    void gen_halo(Render& r);
    void load_art(Render& r, const std::string& dir);
    void free_art();

    SDL_Texture* stone_ = nullptr; int stone_w_ = 0, stone_h_ = 0;
    SDL_Texture* yy_ = nullptr;
    SDL_Texture* halo_ = nullptr;
    SDL_Texture* logo_png_ = nullptr; int logo_w_ = 0, logo_h_ = 0;
    SDL_Texture* splash_png_ = nullptr; int splash_w_ = 0, splash_h_ = 0; Color splash_edge_top_, splash_edge_bot_;
    SDL_Texture* header_art_ = nullptr; int header_w_ = 0, header_h_ = 0;
    SDL_Texture* yy_art_[2] = {nullptr, nullptr};
    SDL_Texture* stone_art_ = nullptr; int stone_art_w_ = 0, stone_art_h_ = 0;
};
