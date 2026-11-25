// Fontes: interface `Font` substituivel + desenho de texto estilizado.
//
// Implementacoes:
//  - SystemFont (make_system_font): fonte do sistema rasterizada via GDI em atlas, em runtime
//    (nenhum arquivo de fonte e distribuido). Fallback padrao.
//  - Bitmap (load_bitmap_font): FONTE PIXEL DO JOGO, extraida da ROM na L5. Hoje e um stub que
//    devolve nullptr; contrato em font.cpp. Quando existir, App/Theme trocam os Font* sem
//    mudar nenhuma tela (as telas so usam a interface Font e draw_text).
#pragma once
#include "render.h"
#include <memory>
#include <string>

struct Glyph {
    int ax = 0, ay = 0, aw = 0, ah = 0;   // retangulo no atlas (px)
    float xoff = 0, yoff = 0;             // deslocamento do desenho em relacao ao pen / topo da linha
    float adv = 0;                        // avanco horizontal
};

class Font {
public:
    virtual ~Font() {}
    virtual float line_height() const = 0;                 // px na escala 1
    virtual const Glyph* glyph(char32_t cp) const = 0;     // nullptr se ausente
    virtual SDL_Texture* texture() const = 0;
    virtual bool smooth() const { return true; }           // false p/ fonte pixel (escala inteira)
    float measure(const std::string& utf8, float scale = 1.f) const;
};

std::unique_ptr<Font> make_system_font(Render& r, const char* face, int px, bool bold);

// Fonte pixel do jogo (bitmap_font.cpp): atlas PNG + font_menu.json do extrator ({"glyphs":{"A":{x,y,w,h}},"advance":8}); compoe acentos PT-BR.
// smooth()==false: use scale inteiro, sem contorno (a fonte ja traz sombra).
std::unique_ptr<Font> load_bitmap_font(Render& r, const std::string& png, const std::string& charmap_json);

enum class Align { Left, Center, Right };

struct TextStyle {
    Color top = Color::hex(0xffffff);
    Color bottom = Color::hex(0xffffff);    // != top => gradiente vertical sobre a linha
    Color outline = Color::hex(0x101018);
    float outline_px = 0;                   // 0 = sem contorno (8 direcoes)
    Color shadow = Color::hex(0x000000, 160);
    float shadow_dx = 0, shadow_dy = 0;     // 0,0 = sem sombra
    float scale = 1.f;
    Align align = Align::Left;
    uint8_t alpha = 255;

    static TextStyle solid(Color c, float outline = 0, Color oc = Color::hex(0x101018)) {
        TextStyle s; s.top = s.bottom = c; s.outline_px = outline; s.outline = oc; return s;
    }
};

// (x,y) = topo da linha; para Center/Right, x e o ponto de ancoragem. Retorna a largura.
float draw_text(Render& r, const Font& f, float x, float y, const std::string& utf8, const TextStyle& st);

// UTF-8 -> codepoints
std::u32string utf8_to_u32(const std::string& s);
