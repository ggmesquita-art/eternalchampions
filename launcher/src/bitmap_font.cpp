// Fonte pixel do jogo (extraida da ROM pelo tools/launcher/extract_ui_assets.py): font_menu.png + font_menu.json.
// O JSON traz glyphs {"A": {x,y,w,h,ink_x0,ink_x1}} em celulas 8x8 (ASCII 0x21..0x7E, SEM acentos). Os acentos PT-BR sao
// compostos aqui: letra base + diacritico em pixel art no mesmo estilo (branco com sombra preta), em celulas 8x14
// (4 linhas acima da celula original e 2 abaixo, para a cedilha).
#include "font.h"
#include "json.h"
#include "png_io.h"
#include <cstring>
#include <unordered_map>

namespace {

class BitmapFont : public Font {
public:
    ~BitmapFont() override { if (tex_) SDL_DestroyTexture(tex_); }
    float line_height() const override { return lh_; }
    const Glyph* glyph(char32_t cp) const override {
        if (cp == 0x2026) cp = '.';                       // reticencias -> ponto
        else if (cp == 0x2022 || cp == 0x25CF) cp = '*';  // marcadores
        else if (cp == 0x2014 || cp == 0x2013) cp = '-';
        else if (cp == 0xA0) cp = ' ';
        auto it = glyphs_.find(cp);
        return it == glyphs_.end() ? nullptr : &it->second;
    }
    SDL_Texture* texture() const override { return tex_; }
    bool smooth() const override { return false; }

    bool build(Render& r, const std::string& png, const std::string& json) {
        std::vector<uint8_t> src; int sw = 0, sh = 0;
        if (!png_read_rgba(png, src, sw, sh)) return false;
        JVal j;
        if (!json_parse_file(json, j)) return false;
        const JVal* gl = j.get("glyphs");
        if (!gl || gl->type != JVal::Obj) return false;
        const int adv = (int)j.n("advance", 8);
        lh_ = (float)j.n("line_height", 8);
        const int cw = 8, ch = 8;

        const int ccw = 8, cch = 14, pitchx = 10, pitchy = 15, per_row = 15;
        struct Acc { char32_t cp; char base; int accent; };   // accent: 0 agudo 1 grave 2 circunflexo 3 til 4 trema 5 cedilha
        const Acc accs[] = {
            {0xC1, 'A', 0}, {0xC0, 'A', 1}, {0xC2, 'A', 2}, {0xC3, 'A', 3}, {0xC9, 'E', 0}, {0xCA, 'E', 2}, {0xCD, 'I', 0},
            {0xD3, 'O', 0}, {0xD4, 'O', 2}, {0xD5, 'O', 3}, {0xDA, 'U', 0}, {0xDC, 'U', 4}, {0xC7, 'C', 5},
            {0xE1, 'a', 0}, {0xE0, 'a', 1}, {0xE2, 'a', 2}, {0xE3, 'a', 3}, {0xE9, 'e', 0}, {0xEA, 'e', 2}, {0xED, 'i', 0},
            {0xF3, 'o', 0}, {0xF4, 'o', 2}, {0xF5, 'o', 3}, {0xFA, 'u', 0}, {0xFC, 'u', 4}, {0xE7, 'c', 5}};
        const int nacc = (int)(sizeof accs / sizeof accs[0]);
        const int rows = (nacc + per_row - 1) / per_row;
        const int aw = std::max(sw, per_row * pitchx + 1), ah = sh + rows * pitchy + 1;
        std::vector<uint8_t> atlas((size_t)aw * ah * 4, 0);
        for (int y = 0; y < sh; ++y) memcpy(&atlas[(size_t)y * aw * 4], &src[(size_t)y * sw * 4], (size_t)sw * 4);

        for (auto& kv : gl->obj) {
            const JVal& g = kv.second;
            std::u32string cps = utf8_to_u32(kv.first);
            if (cps.size() != 1) continue;
            Glyph o;
            o.ax = (int)g.n("x"); o.ay = (int)g.n("y"); o.aw = (int)g.n("w", cw); o.ah = (int)g.n("h", ch);
            o.adv = (float)adv;
            glyphs_[cps[0]] = o;
        }
        {   // espaco
            Glyph sp; sp.adv = (float)adv;
            glyphs_[U' '] = sp;
        }

        // pixels de uma celula original -> buffer
        auto px = [&](int x, int y) -> uint8_t* { return &atlas[((size_t)y * aw + x) * 4]; };
        // mascaras 8x4 (linhas 0..3 da area de acento; # = branco)
        static const char* const masks[6][4] = {
            {"........", "....##..", "...##...", "........"},   // agudo
            {"........", "..##....", "...##...", "........"},   // grave
            {"........", "...##...", "..#..#..", "........"},   // circunflexo
            {"........", "..##.#..", ".#.##...", "........"},   // til
            {"........", "..#..#..", "..#..#..", "........"},   // trema
            {"........", "........", "........", "........"}};  // cedilha (desenhada abaixo)
        int n = 0;
        for (const Acc& a : accs) {
            auto it = glyphs_.find((char32_t)a.base);
            if (it == glyphs_.end()) { ++n; continue; }
            const Glyph& b = it->second;
            int cx = 1 + (n % per_row) * pitchx, cy = sh + 1 + (n / per_row) * pitchy;
            // base na linha 4..11 da celula composta (o "i" perde o ponto: linhas 0..2 da base)
            bool lower = a.base >= 'a' && a.base <= 'z';
            for (int y = 0; y < ch; ++y)
                for (int x = 0; x < cw; ++x) {
                    if (a.base == 'i' && y <= 2) continue;
                    memcpy(px(cx + x, cy + 4 + y), px(b.ax + x, b.ay + y), 4);
                }
            int shift = lower ? 1 : 0;
            // sombra preta primeiro, depois o branco
            for (int pass = 0; pass < 2; ++pass)
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 8; ++x) {
                        if (masks[a.accent][y][x] != '#') continue;
                        int dx = pass == 0 ? 1 : 0, dy = pass == 0 ? 1 : 0;
                        int tx = cx + x + dx, ty = cy + y + shift + dy;
                        if (tx >= cx + ccw || ty >= cy + cch) continue;
                        uint8_t* d = px(tx, ty);
                        if (pass == 0 && d[3]) continue;
                        if (pass == 0) { d[0] = d[1] = d[2] = 0; d[3] = 255; }
                        else { d[0] = d[1] = d[2] = 255; d[3] = 255; }
                    }
            if (a.accent == 5) {   // cedilha: cauda sob a letra
                const char* tail[2] = {"...#....", "..##...."};
                for (int y = 0; y < 2; ++y)
                    for (int x = 0; x < 8; ++x)
                        if (tail[y][x] == '#') { uint8_t* d = px(cx + x, cy + 12 + y); d[0] = d[1] = d[2] = 255; d[3] = 255; }
            }
            Glyph o;
            o.ax = cx; o.ay = cy; o.aw = ccw; o.ah = cch; o.xoff = 0; o.yoff = -4; o.adv = (float)adv;
            glyphs_[a.cp] = o;
            ++n;
        }
        tex_ = r.make_texture(atlas.data(), aw, ah, false);
        return tex_ != nullptr;
    }

private:
    SDL_Texture* tex_ = nullptr;
    float lh_ = 8;
    std::unordered_map<char32_t, Glyph> glyphs_;
};

}  // namespace

std::unique_ptr<Font> load_bitmap_font(Render& r, const std::string& png, const std::string& charmap_json) {
    auto f = std::make_unique<BitmapFont>();
    if (!f->build(r, png, charmap_json)) return nullptr;
    return f;
}
