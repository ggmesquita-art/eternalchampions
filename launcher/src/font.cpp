#include "font.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <cstdint>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif

std::u32string utf8_to_u32(const std::string& s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        char32_t cp; int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1f; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0f; n = 3; }
        else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; }
        else { cp = '?'; n = 1; }
        for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3f);
        out.push_back(cp);
        i += n;
    }
    return out;
}

float Font::measure(const std::string& utf8, float scale) const {
    float w = 0;
    for (char32_t c : utf8_to_u32(utf8)) {
        const Glyph* g = glyph(c);
        if (!g) g = glyph('?');
        if (g) w += g->adv;
    }
    return w * scale;
}

// ---------------------------------------------------------------------------
// SystemFont (Windows: GDI; demais: stb_truetype sobre TTFs do sistema)
// ---------------------------------------------------------------------------
#ifdef _WIN32
namespace {

class SystemFont : public Font {
public:
    ~SystemFont() override { if (tex_) SDL_DestroyTexture(tex_); }
    float line_height() const override { return lh_; }
    const Glyph* glyph(char32_t cp) const override {
        auto it = glyphs_.find(cp);
        return it == glyphs_.end() ? nullptr : &it->second;
    }
    SDL_Texture* texture() const override { return tex_; }

    bool build(Render& r, const wchar_t* face, int px, bool bold) {
        HDC dc = CreateCompatibleDC(nullptr);
        std::vector<char32_t> cps;
        for (char32_t c = 32; c < 127; ++c) cps.push_back(c);
        for (char32_t c = 160; c < 256; ++c) cps.push_back(c);
        const char32_t extra[] = {0x2026, 0x2022, 0x25C4, 0x25BA, 0x25CF, 0x2713, 0x2717, 0x21BB, 0x2190, 0x2191, 0x2192, 0x2193, 0x2014, 0x2013, 0x25B2, 0x25BC, 0x26A0, 0x2248, 0x25B6, 0x25A0, 0x27F2, 0x2500, 0x25BE, 0x25B8, 0x2264, 0x2265};
        for (char32_t c : extra) cps.push_back(c);

        auto mk = [&](const wchar_t* f) {
            return CreateFontW(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, f);
        };
        HFONT main_font = mk(face);
        HFONT sym_font = mk(L"Segoe UI Symbol");
        HGDIOBJ old = SelectObject(dc, main_font);
        TEXTMETRICW tm; GetTextMetricsW(dc, &tm);
        lh_ = (float)tm.tmHeight;
        const int pad = 3;
        const int cellh = tm.tmHeight;

        // DIB de trabalho
        int maxw = px * 3;
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = maxw; bi.bmiHeader.biHeight = -cellh;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ oldbmp = SelectObject(dc, dib);
        SetBkColor(dc, RGB(0, 0, 0));
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, OPAQUE);

        const int AW = 1024;
        int ah = 256;
        std::vector<uint8_t> atlas((size_t)AW * ah * 4, 0);
        int cx = 0, cy = 0;
        for (char32_t cp : cps) {
            bool sym = cp >= 0x2000;
            SelectObject(dc, sym ? sym_font : main_font);
            wchar_t wc = (wchar_t)cp;
            SIZE sz{}; GetTextExtentPoint32W(dc, &wc, 1, &sz);
            int adv = sz.cx;
            int cw = std::min(adv + pad * 2, maxw);
            RECT rc{0, 0, maxw, cellh};
            HBRUSH br = (HBRUSH)GetStockObject(BLACK_BRUSH);
            FillRect(dc, &rc, br);
            TextOutW(dc, pad, 0, &wc, 1);
            if (cx + cw + 1 > AW) { cx = 0; cy += cellh + 1; }
            while (cy + cellh > ah) { ah *= 2; atlas.resize((size_t)AW * ah * 4, 0); }
            const uint8_t* src = (const uint8_t*)bits;
            for (int y = 0; y < cellh; ++y)
                for (int x = 0; x < cw; ++x) {
                    uint8_t v = src[((size_t)y * maxw + x) * 4 + 1];
                    uint8_t* d = &atlas[((size_t)(cy + y) * AW + cx + x) * 4];
                    d[0] = d[1] = d[2] = 255; d[3] = v;
                }
            Glyph g;
            g.ax = cx; g.ay = cy; g.aw = cw; g.ah = cellh;
            g.xoff = (float)-pad; g.yoff = 0; g.adv = (float)adv;
            glyphs_[cp] = g;
            cx += cw + 1;
        }
        tex_ = r.make_texture(atlas.data(), AW, ah, true);

        SelectObject(dc, oldbmp); SelectObject(dc, old);
        DeleteObject(dib); DeleteObject(main_font); DeleteObject(sym_font);
        DeleteDC(dc);
        return tex_ != nullptr;
    }

private:
    SDL_Texture* tex_ = nullptr;
    float lh_ = 16;
    std::unordered_map<char32_t, Glyph> glyphs_;
};

}  // namespace

std::unique_ptr<Font> make_system_font(Render& r, const char* face, int px, bool bold) {
    std::wstring wface;
    for (const char* c = face; *c; ++c) wface += (wchar_t)(unsigned char)*c;   // nomes de familia ASCII
    auto f = std::make_unique<SystemFont>();
    if (!f->build(r, wface.c_str(), px, bold)) return nullptr;
    return f;
}

#else   // !_WIN32: TTF do sistema rasterizado com stb_truetype (public domain, third_party/)

namespace {

bool file_exists(const std::string& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec);
}

struct Ttf {
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool load(const std::string& path) {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n <= 0) { std::fclose(f); return false; }
        data.resize((size_t)n);
        size_t got = std::fread(data.data(), 1, data.size(), f);
        std::fclose(f);
        if (got != data.size()) return false;
        int off = stbtt_GetFontOffsetForIndex(data.data(), 0);   // .ttc: primeira fonte da colecao
        return off >= 0 && stbtt_InitFont(&info, data.data(), off) != 0;
    }
};

const char* const kFontDirs[] = {
    "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/truetype/liberation", "/usr/share/fonts/truetype/liberation2",
    "/usr/share/fonts/truetype/noto", "/usr/share/fonts/truetype/ubuntu", "/usr/share/fonts/truetype/freefont",
    "/usr/share/fonts/dejavu", "/usr/share/fonts/TTF", "/usr/share/fonts/liberation", "/usr/share/fonts/liberation-fonts",
    "/usr/share/fonts/noto", "/usr/share/fonts/opentype/noto", "/usr/share/fonts/google-noto", "/usr/share/fonts/truetype",
    "/usr/local/share/fonts",
    "/System/Library/Fonts", "/System/Library/Fonts/Supplemental", "/Library/Fonts",
};

// Nomes de arquivo por estilo; a ordem e a preferencia. Windows usa "Georgia" (serifada) e "Segoe UI" (sem serifa).
std::vector<std::string> font_names(bool serif, bool bold) {
    if (serif) {
        if (bold) return {"Georgia Bold.ttf", "DejaVuSerif-Bold.ttf", "LiberationSerif-Bold.ttf", "NotoSerif-Bold.ttf", "FreeSerifBold.ttf", "Times New Roman Bold.ttf"};
        return {"Georgia.ttf", "DejaVuSerif.ttf", "LiberationSerif-Regular.ttf", "NotoSerif-Regular.ttf", "FreeSerif.ttf", "Times New Roman.ttf"};
    }
    if (bold) return {"DejaVuSans-Bold.ttf", "LiberationSans-Bold.ttf", "NotoSans-Bold.ttf", "Ubuntu-B.ttf", "FreeSansBold.ttf", "Arial Bold.ttf"};
    return {"DejaVuSans.ttf", "LiberationSans-Regular.ttf", "NotoSans-Regular.ttf", "Ubuntu-R.ttf", "FreeSans.ttf", "Arial.ttf"};
}

std::string find_font_file(bool serif, bool bold) {
    if (const char* env = std::getenv("EC_LAUNCHER_FONT"))
        if (*env && file_exists(env)) return env;
    for (const std::string& n : font_names(serif, bold))
        for (const char* d : kFontDirs) {
            std::string p = std::string(d) + "/" + n;
            if (file_exists(p)) return p;
        }
    if (bold || serif) return find_font_file(false, false);   // estilo indisponivel: cai no sans regular
    // ultimo recurso: qualquer .ttf achado sob as pastas de fontes
    for (const char* d : {"/usr/share/fonts", "/usr/local/share/fonts", "/System/Library/Fonts", "/Library/Fonts"}) {
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(d, std::filesystem::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec) && it->path().extension() == ".ttf") return it->path().string();
    }
    return "";
}

std::string find_symbol_font() {
    for (const char* n : {"DejaVuSans.ttf", "NotoSansSymbols2-Regular.ttf", "NotoSansSymbols-Regular.ttf", "Apple Symbols.ttf", "Arial Unicode.ttf", "FreeSans.ttf"})
        for (const char* d : kFontDirs) {
            std::string p = std::string(d) + "/" + n;
            if (file_exists(p)) return p;
        }
    return "";
}

class SystemFont : public Font {
public:
    ~SystemFont() override { if (tex_) SDL_DestroyTexture(tex_); }
    float line_height() const override { return lh_; }
    const Glyph* glyph(char32_t cp) const override {
        auto it = glyphs_.find(cp);
        return it == glyphs_.end() ? nullptr : &it->second;
    }
    SDL_Texture* texture() const override { return tex_; }

    bool build(Render& r, const char* face, int px, bool bold) {
        const bool serif = face && (!std::strcmp(face, "Georgia") || !std::strcmp(face, "Times New Roman"));
        const std::string main_path = find_font_file(serif, bold);
        Ttf main_font;
        if (main_path.empty() || !main_font.load(main_path)) {
            std::fprintf(stderr, "launcher: nenhuma fonte TTF do sistema encontrada (instale fonts-dejavu-core ou fonts-liberation; ou defina EC_LAUNCHER_FONT=<arquivo.ttf>)\n");
            return false;
        }
        Ttf sym_font;
        const std::string sym_path = find_symbol_font();
        const bool have_sym = !sym_path.empty() && sym_path != main_path && sym_font.load(sym_path);

        std::vector<char32_t> cps;
        for (char32_t c = 32; c < 127; ++c) cps.push_back(c);
        for (char32_t c = 160; c < 256; ++c) cps.push_back(c);
        const char32_t extra[] = {0x2026, 0x2022, 0x25C4, 0x25BA, 0x25CF, 0x2713, 0x2717, 0x21BB, 0x2190, 0x2191, 0x2192, 0x2193, 0x2014, 0x2013, 0x25B2, 0x25BC, 0x26A0, 0x2248, 0x25B6, 0x25A0, 0x27F2, 0x2500, 0x25BE, 0x25B8, 0x2264, 0x2265};
        for (char32_t c : extra) cps.push_back(c);

        const float msc = stbtt_ScaleForMappingEmToPixels(&main_font.info, (float)px);
        int asc = 0, desc = 0, gap = 0;
        stbtt_GetFontVMetrics(&main_font.info, &asc, &desc, &gap);
        const int ascent = (int)std::ceil(asc * msc);
        const int cellh = std::max(1, ascent + (int)std::ceil(-desc * msc));
        lh_ = (float)cellh;
        const int pad = 3;
        const int maxw = px * 3;

        const int AW = 1024;
        int ah = 256;
        std::vector<uint8_t> atlas((size_t)AW * ah * 4, 0);
        int cx = 0, cy = 0;
        std::vector<unsigned char> gbuf;
        for (char32_t cp : cps) {
            Ttf* f = &main_font;
            if (!stbtt_FindGlyphIndex(&f->info, (int)cp)) {
                if (have_sym && stbtt_FindGlyphIndex(&sym_font.info, (int)cp)) f = &sym_font;
                else if (cp != 32 && cp != 0xA0) continue;   // sem glifo: o draw_text cai no '?'
            }
            const float sc = stbtt_ScaleForMappingEmToPixels(&f->info, (float)px);
            int adv_u = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&f->info, (int)cp, &adv_u, &lsb);
            const int adv = (int)std::lround(adv_u * sc);
            const int cw = std::min(adv + pad * 2, maxw);
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            stbtt_GetCodepointBitmapBox(&f->info, (int)cp, sc, sc, &x0, &y0, &x1, &y1);
            const int gw = x1 - x0, gh = y1 - y0;
            if (cx + cw + 1 > AW) { cx = 0; cy += cellh + 1; }
            while (cy + cellh > ah) { ah *= 2; atlas.resize((size_t)AW * ah * 4, 0); }
            if (gw > 0 && gh > 0) {
                gbuf.assign((size_t)gw * gh, 0);
                stbtt_MakeCodepointBitmap(&f->info, gbuf.data(), gw, gh, gw, sc, sc, (int)cp);
                for (int y = 0; y < gh; ++y) {
                    const int ty = ascent + y0 + y;
                    if (ty < 0 || ty >= cellh) continue;
                    for (int x = 0; x < gw; ++x) {
                        const int tx = pad + x0 + x;
                        if (tx < 0 || tx >= cw) continue;
                        uint8_t* d = &atlas[((size_t)(cy + ty) * AW + cx + tx) * 4];
                        d[0] = d[1] = d[2] = 255; d[3] = gbuf[(size_t)y * gw + x];
                    }
                }
            }
            Glyph g;
            g.ax = cx; g.ay = cy; g.aw = cw; g.ah = cellh;
            g.xoff = (float)-pad; g.yoff = 0; g.adv = (float)adv;
            glyphs_[cp] = g;
            cx += cw + 1;
        }
        tex_ = r.make_texture(atlas.data(), AW, ah, true);
        return tex_ != nullptr;
    }

private:
    SDL_Texture* tex_ = nullptr;
    float lh_ = 16;
    std::unordered_map<char32_t, Glyph> glyphs_;
};

}  // namespace

std::unique_ptr<Font> make_system_font(Render& r, const char* face, int px, bool bold) {
    auto f = std::make_unique<SystemFont>();
    if (!f->build(r, face, px, bold)) return nullptr;
    return f;
}

#endif

// ---------------------------------------------------------------------------
// draw_text
// ---------------------------------------------------------------------------
namespace {
struct Batch {
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
};
}  // namespace

float draw_text(Render& r, const Font& f, float x, float y, const std::string& utf8, const TextStyle& st) {
    std::u32string s = utf8_to_u32(utf8);
    const float sc = st.scale;
    float width = f.measure(utf8, sc);
    if (st.align == Align::Center) x -= width * 0.5f;
    else if (st.align == Align::Right) x -= width;
    x = std::floor(x); y = std::floor(y);

    SDL_Texture* tex = f.texture();
    int tw = 0, th = 0;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    const float lh = f.line_height() * sc;

    auto pass = [&](float ox, float oy, Color top, Color bot) {
        Batch b;
        b.v.reserve(s.size() * 4); b.idx.reserve(s.size() * 6);
        float pen = x;
        for (char32_t c : s) {
            const Glyph* g = f.glyph(c);
            if (!g) g = f.glyph('?');
            if (!g) continue;
            if (g->aw > 0) {
                float x0 = pen + g->xoff * sc + ox, y0 = y + g->yoff * sc + oy;
                float x1 = x0 + g->aw * sc, y1 = y0 + g->ah * sc;
                float u0 = (float)g->ax / tw, v0 = (float)g->ay / th;
                float u1 = (float)(g->ax + g->aw) / tw, v1 = (float)(g->ay + g->ah) / th;
                Color ct = lerp(top, bot, (y0 - oy - y) / lh);
                Color cb = lerp(top, bot, (y1 - oy - y) / lh);
                ct.a = (uint8_t)(ct.a * st.alpha / 255); cb.a = (uint8_t)(cb.a * st.alpha / 255);
                int base = (int)b.v.size();
                b.v.push_back({{x0, y0}, ct.sdl(), {u0, v0}});
                b.v.push_back({{x1, y0}, ct.sdl(), {u1, v0}});
                b.v.push_back({{x1, y1}, cb.sdl(), {u1, v1}});
                b.v.push_back({{x0, y1}, cb.sdl(), {u0, v1}});
                int q[6] = {0, 1, 2, 0, 2, 3};
                for (int k : q) b.idx.push_back(base + k);
            }
            pen += g->adv * sc;
        }
        if (!b.v.empty()) r.geometry(tex, b.v.data(), (int)b.v.size(), b.idx.data(), (int)b.idx.size());
    };

    if (st.shadow_dx != 0 || st.shadow_dy != 0) {
        Color sh = st.shadow;
        if (st.outline_px > 0) {
            for (int k = 0; k < 8; ++k) {
                float a = k * 3.14159265f / 4;
                pass(st.shadow_dx + std::round(std::cos(a) * st.outline_px), st.shadow_dy + std::round(std::sin(a) * st.outline_px), sh, sh);
            }
        }
        pass(st.shadow_dx, st.shadow_dy, sh, sh);
    }
    if (st.outline_px > 0) {
        for (int k = 0; k < 8; ++k) {
            float a = k * 3.14159265f / 4;
            pass(std::round(std::cos(a) * st.outline_px), std::round(std::sin(a) * st.outline_px), st.outline, st.outline);
        }
    }
    pass(0, 0, st.top, st.bottom);
    return width;
}
