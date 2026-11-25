// CONTROLES (PLAN §7): P1..P6, device/pad_type/deadzone, binds de teclado e controle, combos, autofire,
// presets, pad_hotkeys e "Testar entrada" (mesma resolucao do runner: binds OR combos, deadzone, autofire).
// Formato do ini = segagenesisrecomp/runner/app_config.c / input_map.c:
//   [input.pN] device = 0..3 | pad_type = 0/1 | deadzone = 0..100
//              key.<Btn> = <scancode> | pad.<Btn> = button:N | axis:N:+|- | none
//              combo.K = pad:button:N | pad:axis:N:+|- | key:SC -> a+b+x   (K 1..8)
//              autofire.<Btn> = Hz (0..60; a UI limita a 30)
//   [input]    pad_hotkeys = 0|1
#include "../platform.h"
#include "../widgets.h"
#include "screens.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// ------------------------------------------------------------------ modelo
enum { NB = 12 };   // ordem do GenesisButton do runner
const char* const kBtn[NB] = {"Up", "Down", "Left", "Right", "A", "B", "C", "Start", "X", "Y", "Z", "Mode"};
const char* const kDest[NB] = {"up", "down", "left", "right", "a", "b", "c", "start", "x", "y", "z", "mode"};
const unsigned kBit[NB] = {0x0001, 0x0002, 0x0004, 0x0008, 0x0040, 0x0010, 0x0020, 0x0080, 0x0100, 0x0200, 0x0400, 0x0800};
enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_A, B_B, B_C, B_START, B_X, B_Y, B_Z, B_MODE };

struct Bind { int kind = 0, code = 0, dir = 0; };                 // 0 none, 1 button, 2 axis
struct Combo { int kind = 0, code = 0, dir = 0; unsigned dest = 0; };   // 1 pad button, 2 pad axis, 3 key
struct PCfg {
    int device = 3, pad_type = 0, deadzone = 25;
    int key[NB] = {};
    Bind pad[NB];
    Combo combo[8];
    int af[NB] = {};
};

Bind btn(int b) { return Bind{1, b, 0}; }

PCfg defaults(int p) {
    PCfg c;
    auto set = [&](int b, int sc, Bind pb) { c.key[b] = sc; c.pad[b] = pb; };
    c.pad_type = (p < 2) ? 1 : 0;   // o jogo forca 6 botoes em P1/P2 (ec_on_post_reset)
    if (p == 0) {
        c.device = 3;
        set(B_UP, SDL_SCANCODE_UP, btn(SDL_CONTROLLER_BUTTON_DPAD_UP)); set(B_DOWN, SDL_SCANCODE_DOWN, btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
        set(B_LEFT, SDL_SCANCODE_LEFT, btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)); set(B_RIGHT, SDL_SCANCODE_RIGHT, btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
        set(B_A, SDL_SCANCODE_Z, btn(SDL_CONTROLLER_BUTTON_X)); set(B_B, SDL_SCANCODE_X, btn(SDL_CONTROLLER_BUTTON_A));
        set(B_C, SDL_SCANCODE_C, btn(SDL_CONTROLLER_BUTTON_B)); set(B_START, SDL_SCANCODE_RETURN, btn(SDL_CONTROLLER_BUTTON_START));
        set(B_X, SDL_SCANCODE_A, btn(SDL_CONTROLLER_BUTTON_Y)); set(B_Y, SDL_SCANCODE_S, btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        set(B_Z, SDL_SCANCODE_D, btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)); set(B_MODE, SDL_SCANCODE_BACKSPACE, btn(SDL_CONTROLLER_BUTTON_BACK));
    } else if (p == 1) {
        c.device = 0;
        set(B_UP, SDL_SCANCODE_I, btn(SDL_CONTROLLER_BUTTON_DPAD_UP)); set(B_DOWN, SDL_SCANCODE_K, btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
        set(B_LEFT, SDL_SCANCODE_J, btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)); set(B_RIGHT, SDL_SCANCODE_L, btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
        set(B_A, SDL_SCANCODE_N, btn(SDL_CONTROLLER_BUTTON_X)); set(B_B, SDL_SCANCODE_M, btn(SDL_CONTROLLER_BUTTON_A));
        set(B_C, SDL_SCANCODE_COMMA, btn(SDL_CONTROLLER_BUTTON_B)); set(B_START, SDL_SCANCODE_RSHIFT, btn(SDL_CONTROLLER_BUTTON_START));
        set(B_X, SDL_SCANCODE_U, btn(SDL_CONTROLLER_BUTTON_Y)); set(B_Y, SDL_SCANCODE_O, btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
        set(B_Z, SDL_SCANCODE_P, btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)); set(B_MODE, SDL_SCANCODE_SLASH, btn(SDL_CONTROLLER_BUTTON_BACK));
    } else {
        c = defaults(0);
        c.device = 2;
        memset(c.key, 0, sizeof c.key);
    }
    return c;
}

std::string sec(int p) { return "input.p" + std::to_string(p + 1); }

std::string fmt_bind(const Bind& b) {
    char buf[40];
    if (b.kind == 1) snprintf(buf, sizeof buf, "button:%d", b.code);
    else if (b.kind == 2) snprintf(buf, sizeof buf, "axis:%d:%c", b.code, b.dir < 0 ? '-' : '+');
    else snprintf(buf, sizeof buf, "none");
    return buf;
}
Bind parse_bind(const std::string& v) {
    Bind b;
    if (v.rfind("button:", 0) == 0) { b.kind = 1; b.code = atoi(v.c_str() + 7); }
    else if (v.rfind("axis:", 0) == 0) {
        b.kind = 2; b.code = atoi(v.c_str() + 5);
        size_t c = v.rfind(':');
        b.dir = (c != std::string::npos && c + 1 < v.size() && v[c + 1] == '-') ? -1 : 1;
    }
    return b;
}
std::string fmt_combo(const Combo& c) {
    char src[48];
    if (c.kind == 1) snprintf(src, sizeof src, "pad:button:%d", c.code);
    else if (c.kind == 2) snprintf(src, sizeof src, "pad:axis:%d:%c", c.code, c.dir < 0 ? '-' : '+');
    else snprintf(src, sizeof src, "key:%d", c.code);
    std::string d;
    for (int i = 0; i < NB; ++i) if (c.dest & kBit[i]) { if (!d.empty()) d += "+"; d += kDest[i]; }
    return std::string(src) + " -> " + d;
}
bool parse_combo(const std::string& val, Combo& out) {
    std::string s = val;
    size_t sc = s.find(';'); if (sc != std::string::npos) s = s.substr(0, sc);
    size_t ar = s.find("->");
    if (ar == std::string::npos) return false;
    std::string src, dst = s.substr(ar + 2);
    for (char ch : s.substr(0, ar)) if (ch != ' ' && ch != '\t') src += ch;
    Combo c;
    if (src.rfind("pad:button:", 0) == 0) { c.kind = 1; c.code = atoi(src.c_str() + 11); }
    else if (src.rfind("pad:axis:", 0) == 0) {
        c.kind = 2; c.code = atoi(src.c_str() + 9);
        size_t k = src.rfind(':');
        c.dir = (k != std::string::npos && k + 1 < src.size() && src[k + 1] == '-') ? -1 : 1;
    } else if (src.rfind("key:", 0) == 0) { c.kind = 3; c.code = atoi(src.c_str() + 4); }
    else return false;
    std::string tok;
    for (size_t i = 0; i <= dst.size(); ++i) {
        if (i == dst.size() || dst[i] == '+') {
            std::string t; for (char ch : tok) if (ch != ' ') t += (char)tolower((unsigned char)ch);
            for (int b = 0; b < NB; ++b) if (t == kDest[b]) c.dest |= kBit[b];
            tok.clear();
        } else tok += dst[i];
    }
    if (!c.dest) return false;
    out = c;
    return true;
}

bool ini_get_ci(App& a, const std::string& s, const std::string& name, std::string& out) {
    if (a.cfg.ini().get(s, name, out)) return true;
    std::string low; for (char c : name) low += (char)tolower((unsigned char)c);
    return a.cfg.ini().get(s, low, out);
}

PCfg load_pcfg(App& a, int p) {
    PCfg c = defaults(p);
    std::string s = sec(p), v;
    Ini& ini = a.cfg.ini();
    if (ini.get(s, "device", v)) c.device = std::min(3, std::max(0, atoi(v.c_str())));
    if (ini.get(s, "pad_type", v)) c.pad_type = atoi(v.c_str()) ? 1 : 0;
    if (ini.get(s, "deadzone", v)) c.deadzone = std::min(100, std::max(0, atoi(v.c_str())));
    for (int b = 0; b < NB; ++b) {
        if (ini.get(s, std::string("key.") + kBtn[b], v)) c.key[b] = atoi(v.c_str());
        if (ini.get(s, std::string("pad.") + kBtn[b], v)) c.pad[b] = parse_bind(v);
        if (ini_get_ci(a, s, std::string("autofire.") + kBtn[b], v)) c.af[b] = std::min(60, std::max(0, atoi(v.c_str())));
    }
    for (int k = 0; k < 8; ++k)
        if (ini.get(s, "combo." + std::to_string(k + 1), v)) parse_combo(v, c.combo[k]);
    return c;
}

void put(App& a, int p, const std::string& key, const std::string& val) { a.cfg.ini().set(sec(p), key, val); a.cfg.ini().save(); }
void del(App& a, int p, const std::string& key) { a.cfg.ini().remove(sec(p), key); a.cfg.ini().save(); }
void put_key(App& a, int p, int b, int sc) { put(a, p, std::string("key.") + kBtn[b], std::to_string(sc)); }
void put_pad(App& a, int p, int b, const Bind& bd) { put(a, p, std::string("pad.") + kBtn[b], fmt_bind(bd)); }
void put_combo(App& a, int p, int k, const Combo& c) {
    if (!c.dest) del(a, p, "combo." + std::to_string(k + 1)); else put(a, p, "combo." + std::to_string(k + 1), fmt_combo(c));
}
bool hotkeys_on(App& a) { std::string v; return !a.cfg.ini().get("input", "pad_hotkeys", v) || atoi(v.c_str()) != 0; }
void set_hotkeys(App& a, bool on) { a.cfg.ini().set("input", "pad_hotkeys", on ? "1" : "0"); a.cfg.ini().save(); }

// ------------------------------------------------------------------ nomes
const char* kXboxBtn[] = {"A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3", "LB", "RB", "D-pad ↑", "D-pad ↓", "D-pad ←", "D-pad →"};
const char* kPsBtn[] = {"✕", "○", "□", "△", "Share", "PS", "Options", "L3", "R3", "L1", "R1", "D-pad ↑", "D-pad ↓", "D-pad ←", "D-pad →"};
const char* kNinBtn[] = {"B", "A", "Y", "X", "-", "Home", "+", "L3", "R3", "L", "R", "D-pad ↑", "D-pad ↓", "D-pad ←", "D-pad →"};
const char* kFamily[] = {"Xbox", "PlayStation", "Nintendo"};

std::string pad_btn_name(int fam, int code) {
    const char* const* t = fam == 1 ? kPsBtn : fam == 2 ? kNinBtn : kXboxBtn;
    return (code >= 0 && code < 15) ? t[code] : "botão " + std::to_string(code);
}
std::string axis_name(int fam, int code, int dir) {
    bool ps = fam == 1;
    switch (code) {
    case 0: return std::string("Analógico E ") + (dir < 0 ? "←" : "→");
    case 1: return std::string("Analógico E ") + (dir < 0 ? "↑" : "↓");
    case 2: return std::string("Analógico D ") + (dir < 0 ? "←" : "→");
    case 3: return std::string("Analógico D ") + (dir < 0 ? "↑" : "↓");
    case 4: return ps ? "L2" : (fam == 2 ? "ZL" : "LT");
    case 5: return ps ? "R2" : (fam == 2 ? "ZR" : "RT");
    }
    return "eixo " + std::to_string(code);
}
std::string bind_name(int fam, const Bind& b) {
    if (b.kind == 1) return pad_btn_name(fam, b.code);
    if (b.kind == 2) return axis_name(fam, b.code, b.dir);
    return "—";
}
std::string key_name(int sc) {
    if (sc <= 0) return "—";
    const char* n = SDL_GetScancodeName((SDL_Scancode)sc);
    return (n && *n) ? n : "tecla " + std::to_string(sc);
}
std::string combo_src_name(int fam, const Combo& c) {
    if (c.kind == 1) return pad_btn_name(fam, c.code);
    if (c.kind == 2) return axis_name(fam, c.code, c.dir);
    return key_name(c.code);
}
std::string dest_name(unsigned d) {
    std::string r;
    for (int i = 0; i < NB; ++i) if (d & kBit[i]) { if (!r.empty()) r += "+"; std::string n = kBtn[i]; r += n; }
    return r;
}

// ------------------------------------------------------------------ presets (§7.3)
struct Preset { const char* id; const char* label; const char* help; };
const Preset kPresets[] = {
    {"classico", "Clássico Mega Drive", "Os padrões do runner: A/B/C nos botões X/A/B do controle, X no Y, Y/Z em LB/RB. Remove combos e autofire."},
    {"arcade", "Arcade 6 botões (Xbox)", "Socos X, Y, RB; chutes A, B, RT; LB = X+A e LT = Z+C. Usa LB/RB (atalhos de quick save/load do runner)."},
    {"stick", "Stick de luta (8BitDo/Hori)", "Primeira fileira (X, Y, LB) = socos; segunda (A, B, RB) = chutes. Usa LB/RB."},
    {"teclado", "Teclado", "Socos U, I, O; chutes J, K, L; Espaço = X+A. Setas, Enter e Backspace como no padrão."},
};

bool uses_hotkey_buttons(const PCfg& c) {
    auto hot = [](int code) { return code == SDL_CONTROLLER_BUTTON_LEFTSHOULDER || code == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER || code == SDL_CONTROLLER_BUTTON_BACK; };
    for (int b = 0; b < NB; ++b) if (c.pad[b].kind == 1 && hot(c.pad[b].code)) return true;
    for (auto& k : c.combo) if (k.kind == 1 && hot(k.code)) return true;
    return false;
}

void apply_preset_cfg(App& a, int p, const std::string& id) {
    PCfg c = defaults(p == 0 || p == 1 ? 0 : p);
    PCfg base = defaults(p);
    c.device = base.device;
    for (int b = 0; b < NB; ++b) if (p != 0) c.key[b] = base.key[b];
    Combo extra[8];
    if (id == "arcade" || id == "stick") {
        c.pad_type = 1;
        c.pad[B_X] = btn(SDL_CONTROLLER_BUTTON_X);
        c.pad[B_Y] = btn(SDL_CONTROLLER_BUTTON_Y);
        if (id == "arcade") {
            c.pad[B_Z] = btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
            c.pad[B_A] = btn(SDL_CONTROLLER_BUTTON_A);
            c.pad[B_B] = btn(SDL_CONTROLLER_BUTTON_B);
            c.pad[B_C] = Bind{2, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1};
            extra[0] = Combo{1, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0, kBit[B_X] | kBit[B_A]};
            extra[1] = Combo{2, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1, kBit[B_Z] | kBit[B_C]};
        } else {
            c.pad[B_Z] = btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
            c.pad[B_A] = btn(SDL_CONTROLLER_BUTTON_A);
            c.pad[B_B] = btn(SDL_CONTROLLER_BUTTON_B);
            c.pad[B_C] = btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        }
    } else if (id == "teclado") {
        c.pad_type = 1;
        c.key[B_X] = SDL_SCANCODE_U; c.key[B_Y] = SDL_SCANCODE_I; c.key[B_Z] = SDL_SCANCODE_O;
        c.key[B_A] = SDL_SCANCODE_J; c.key[B_B] = SDL_SCANCODE_K; c.key[B_C] = SDL_SCANCODE_L;
        extra[0] = Combo{3, SDL_SCANCODE_SPACE, 0, kBit[B_X] | kBit[B_A]};
    } else {   // classico
        c.pad_type = base.pad_type;
    }
    c.deadzone = 25;
    put(a, p, "pad_type", std::to_string(c.pad_type));
    put(a, p, "deadzone", std::to_string(c.deadzone));
    for (int b = 0; b < NB; ++b) { put_key(a, p, b, c.key[b]); put_pad(a, p, b, c.pad[b]); del(a, p, std::string("autofire.") + kBtn[b]); }
    for (int k = 0; k < 8; ++k) put_combo(a, p, k, extra[k]);
}

// ------------------------------------------------------------------ desenho
void fill_circle(Render& r, float cx, float cy, float rad, Color c, int seg = 28) {
    std::vector<SDL_Vertex> v; std::vector<int> idx;
    v.push_back({{cx, cy}, c.sdl(), {0, 0}});
    for (int i = 0; i <= seg; ++i) {
        float a = i * 6.2831853f / seg;
        v.push_back({{cx + std::cos(a) * rad, cy + std::sin(a) * rad}, c.sdl(), {0, 0}});
        if (i) { idx.push_back(0); idx.push_back(i); idx.push_back(i + 1); }
    }
    r.geometry(nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}
void ring(Render& r, float cx, float cy, float rad, float w, Color c) { fill_circle(r, cx, cy, rad + w, c); }

void round_rect(Render& r, Rect b, float rad, Color c) {
    r.fill({b.x + rad, b.y, b.w - 2 * rad, b.h}, c);
    r.fill({b.x, b.y + rad, b.w, b.h - 2 * rad}, c);
    fill_circle(r, b.x + rad, b.y + rad, rad, c); fill_circle(r, b.x + b.w - rad, b.y + rad, rad, c);
    fill_circle(r, b.x + rad, b.y + b.h - rad, rad, c); fill_circle(r, b.x + b.w - rad, b.y + b.h - rad, rad, c);
}

struct Lit { bool on[NB] = {}; };

const Color kGold = Color::hex(0xffe63a), kBodyD = Color::hex(0x1b1f27), kBodyM = Color::hex(0x333a46), kBtnD = Color::hex(0x10131a);

void draw_label(App& app, float x, float y, const std::string& s, Color c, float scale = 0.55f) {
    TextStyle st = TextStyle::solid(c, 0); st.align = Align::Center; st.scale = scale;
    draw_text(app.render, *app.theme.fonts.menu, x, y - app.theme.fonts.menu->line_height() * scale * 0.5f, s, st);
}

// Controle Mega Drive de 6 botoes. lit[]: botoes realcados.
void draw_md_pad(App& app, float cx, float cy, const Lit& lit, int focus_btn) {
    Render& r = app.render;
    round_rect(r, {cx - 210, cy - 66, 420, 132}, 38, Color::hex(0x0a0c10));
    round_rect(r, {cx - 206, cy - 62, 412, 124}, 36, kBodyM);
    round_rect(r, {cx - 206, cy - 62, 412, 58}, 30, Color::hex(0x414a58));
    auto spot = [&](int b, float x, float y, float rad, const char* lab) {
        bool on = lit.on[b], foc = focus_btn == b;
        if (foc) ring(r, x, y, rad, 4, kGold);
        fill_circle(r, x, y, rad, on ? kGold : kBtnD);
        draw_label(app, x, y, lab, on ? Color::hex(0x101018) : Color::hex(0xdde6ee));
    };
    // D-pad
    float dx = cx - 130, dy = cy;
    auto arm = [&](int b, Rect rc) {
        if (focus_btn == b) r.fill({rc.x - 3, rc.y - 3, rc.w + 6, rc.h + 6}, kGold);
        r.fill(rc, lit.on[b] ? kGold : kBtnD);
    };
    r.fill({dx - 12, dy - 12, 24, 24}, kBtnD);
    arm(B_UP, {dx - 12, dy - 44, 24, 32}); arm(B_DOWN, {dx - 12, dy + 12, 24, 32});
    arm(B_LEFT, {dx - 44, dy - 12, 32, 24}); arm(B_RIGHT, {dx + 12, dy - 12, 32, 24});
    // A B C (fileira de baixo) / X Y Z (cima)
    spot(B_A, cx + 20, cy + 30, 19, "A"); spot(B_B, cx + 74, cy + 22, 19, "B"); spot(B_C, cx + 128, cy + 14, 19, "C");
    spot(B_X, cx + 28, cy - 18, 15, "X"); spot(B_Y, cx + 77, cy - 26, 15, "Y"); spot(B_Z, cx + 126, cy - 34, 15, "Z");
    // Start / Mode
    for (int b : {B_START, B_MODE}) {
        float x = cx - 38, y = b == B_START ? cy + 36 : cy - 40;
        bool on = lit.on[b], foc = focus_btn == b;
        if (foc) round_rect(r, {x - 34, y - 11, 68, 22}, 11, kGold);
        round_rect(r, {x - 30, y - 8, 60, 16}, 8, on ? kGold : kBtnD);
        draw_label(app, x, y, b == B_START ? "START" : "MODE", on ? Color::hex(0x101018) : Color::hex(0xdde6ee), 0.4f);
    }
}

// Posicao de um controle fisico no desenho do pad (cx,cy = centro). Retorna false se desconhecido.
struct PadSpot { float x, y, r; };
bool pad_spot(float cx, float cy, int kind, int code, int dir, PadSpot& s) {
    if (kind == 1) {
        switch (code) {
        case SDL_CONTROLLER_BUTTON_A: s = {cx + 118, cy + 2, 14}; return true;
        case SDL_CONTROLLER_BUTTON_B: s = {cx + 146, cy - 18, 14}; return true;
        case SDL_CONTROLLER_BUTTON_X: s = {cx + 90, cy - 18, 14}; return true;
        case SDL_CONTROLLER_BUTTON_Y: s = {cx + 118, cy - 38, 14}; return true;
        case SDL_CONTROLLER_BUTTON_BACK: s = {cx - 26, cy - 22, 10}; return true;
        case SDL_CONTROLLER_BUTTON_GUIDE: s = {cx, cy - 36, 10}; return true;
        case SDL_CONTROLLER_BUTTON_START: s = {cx + 26, cy - 22, 10}; return true;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK: s = {cx - 118, cy - 18, 26}; return true;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK: s = {cx + 56, cy + 34, 24}; return true;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: s = {cx - 128, cy - 64, 15}; return true;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: s = {cx + 128, cy - 64, 15}; return true;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: s = {cx - 56, cy + 12, 11}; return true;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: s = {cx - 56, cy + 56, 11}; return true;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: s = {cx - 78, cy + 34, 11}; return true;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: s = {cx - 34, cy + 34, 11}; return true;
        }
    } else if (kind == 2) {
        switch (code) {
        case SDL_CONTROLLER_AXIS_LEFTX: s = {cx - 118 + dir * 18, cy - 18, 12}; return true;
        case SDL_CONTROLLER_AXIS_LEFTY: s = {cx - 118, cy - 18 + dir * 18, 12}; return true;
        case SDL_CONTROLLER_AXIS_RIGHTX: s = {cx + 56 + dir * 16, cy + 34, 12}; return true;
        case SDL_CONTROLLER_AXIS_RIGHTY: s = {cx + 56, cy + 34 + dir * 16, 12}; return true;
        case SDL_CONTROLLER_AXIS_TRIGGERLEFT: s = {cx - 128, cy - 87, 13}; return true;
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: s = {cx + 128, cy - 87, 13}; return true;
        }
    }
    return false;
}

// Controle fisico estilo Xbox/PS. `marks`: controles a realcar (cruzado com o bind focado / ao vivo).
void draw_phys_pad(App& app, float cx, float cy, int fam, const std::vector<std::array<int, 3>>& marks, bool have_pad) {
    Render& r = app.render;
    Color body = have_pad ? kBodyM : Color::hex(0x262b33);
    // gatilhos e ombros
    for (int side : {-1, 1}) {
        round_rect(r, {cx + side * 128 - 22, cy - 98, 44, 22}, 8, kBodyD);                 // LT/RT
        round_rect(r, {cx + side * 128 - 32, cy - 74, 64, 20}, 8, Color::hex(0x20252d));   // LB/RB
    }
    round_rect(r, {cx - 168, cy - 54, 336, 130}, 52, Color::hex(0x0a0c10));
    round_rect(r, {cx - 164, cy - 50, 328, 122}, 50, body);
    // analogico esquerdo / direito
    fill_circle(r, cx - 118, cy - 18, 24, Color::hex(0x0a0c10)); fill_circle(r, cx - 118, cy - 18, 17, kBtnD);
    fill_circle(r, cx + 56, cy + 34, 24, Color::hex(0x0a0c10)); fill_circle(r, cx + 56, cy + 34, 17, kBtnD);
    // dpad
    r.fill({cx - 66, cy + 24, 20, 20}, kBtnD);
    r.fill({cx - 66, cy + 6, 20, 56}, kBtnD); r.fill({cx - 84, cy + 24, 56, 20}, kBtnD);
    // faces
    struct F { int code; float x, y; } faces[] = {{0, cx + 118, cy + 2}, {1, cx + 146, cy - 18}, {2, cx + 90, cy - 18}, {3, cx + 118, cy - 38}};
    Color fc[] = {Color::hex(0x3fae4a), Color::hex(0xd0453a), Color::hex(0x3c7ad0), Color::hex(0xd6b73a)};
    for (auto& f : faces) {
        fill_circle(r, f.x, f.y, 14, fam == 1 ? Color::hex(0x20252d) : fc[f.code]);
        draw_label(app, f.x, f.y, pad_btn_name(fam, f.code), Color::hex(0xffffff), 0.45f);
    }
    for (int code : {SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE, SDL_CONTROLLER_BUTTON_START}) {
        PadSpot s; pad_spot(cx, cy, 1, code, 0, s);
        fill_circle(r, s.x, s.y, 8, kBtnD);
    }
    draw_label(app, cx - 128, cy - 64, fam == 1 ? "L1" : (fam == 2 ? "L" : "LB"), Color::hex(0xdde6ee), 0.4f);
    draw_label(app, cx + 128, cy - 64, fam == 1 ? "R1" : (fam == 2 ? "R" : "RB"), Color::hex(0xdde6ee), 0.4f);
    draw_label(app, cx - 128, cy - 87, fam == 1 ? "L2" : (fam == 2 ? "ZL" : "LT"), Color::hex(0xdde6ee), 0.4f);
    draw_label(app, cx + 128, cy - 87, fam == 1 ? "R2" : (fam == 2 ? "ZR" : "RT"), Color::hex(0xdde6ee), 0.4f);
    for (auto& m : marks) {
        PadSpot s;
        if (!pad_spot(cx, cy, m[0], m[1], m[2], s)) continue;
        ring(app.render, s.x, s.y, s.r, 3, kGold);
        fill_circle(app.render, s.x, s.y, s.r, kGold.with_alpha(110));
    }
}

// ------------------------------------------------------------------ tela
enum Page { P_PLAYER, P_BINDS, P_COMBOS, P_AUTOFIRE, P_TEST, P_COUNT };
const char* kPageName[P_COUNT] = {"Jogador", "Botões", "Combos", "Autofire", "Testar"};

struct CRow {
    std::string label, value, help;
    int id = 0;
    bool header = false, info = false, arrows = false;
    Color tint{255, 255, 255, 0};
    bool modified = false, dim = false;
};

enum class Mode { Normal, Capture, DestEdit };
enum class CapTarget { Bind, ComboNew, ComboOrigin };

class Controls : public Screen {
public:
    void on_enter(App& app) override {
        if (app.shot_mode) {
            page_ = std::min(app.shot_page, (int)P_COUNT - 1);
            focus_ = app.shot_focus;
            if (app.shot_state == 1) begin_capture(app, CapTarget::Bind, B_Y);
            if (app.shot_state == 2) { dest_k_ = 0; dest_sel_ = 0; dest_tmp_ = load_pcfg(app, player_).combo[0]; if (!dest_tmp_.dest) dest_tmp_ = Combo{1, 10, 0, kBit[B_A]}; mode_ = Mode::DestEdit; }
        }
    }
    void on_resume(App&) override {}

    void update(App& app, const InputState& in, float dt) override {
        t_ += dt;
        pc_ = load_pcfg(app, player_);
        fam_ = app.input.pad_family(player_);
        app.input.capture_enabled = (mode_ == Mode::Capture);
        if (mode_ == Mode::Capture) { update_capture(app, in, dt); return; }
        if (mode_ == Mode::DestEdit) { update_dest(app, in); return; }
        if (in.p(A_START)) { request_play(app); return; }
        if (in.p(A_BACK) || in.mouse_right_click) {
            if (in.mouse_right_click && page_ == P_COMBOS && sel_row() && sel_row()->id >= 0 && sel_row()->id < 8) { del_combo(app, sel_row()->id); return; }
            app.cfg.save(); app.pop(); return;
        }
        if (in.p(A_TABPREV)) { page_ = (page_ + P_COUNT - 1) % P_COUNT; focus_ = 0; app.fade = 0.08f; }
        if (in.p(A_TABNEXT)) { page_ = (page_ + 1) % P_COUNT; focus_ = 0; app.fade = 0.08f; }
        build_rows(app);
        if (page_ == P_TEST) { frame_++; return; }
        if (in.p(A_UP)) move(-1);
        if (in.p(A_DOWN)) move(+1);
        if (in.mouse_inside && (in.mouse_moved || in.mouse_click))
            for (int i = scroll_; i < std::min((int)rows_.size(), scroll_ + kVis); ++i) {
                float y = list_.y + 12 + (i - scroll_) * kRowH;
                if (in.mouse_x >= list_.x && in.mouse_x < list_.x + list_.w && in.mouse_y >= y && in.mouse_y < y + kRowH && !rows_[i].header && !rows_[i].info) {
                    focus_ = i;
                    if (in.mouse_click) { activate(app); return; }
                }
            }
        if (focus_ >= (int)rows_.size() || rows_.empty() || rows_[focus_].header || rows_[focus_].info) { focus_ = std::max(0, std::min(focus_, (int)rows_.size() - 1)); move(+1); }
        if (rows_.empty()) return;
        if (in.p(A_LEFT)) adjust(app, -1);
        else if (in.p(A_RIGHT)) adjust(app, +1);
        else if (in.p(A_ACCEPT)) activate(app);
        else if (in.p(A_RESET)) reset(app);
    }

    void draw(App& app) override {
        Render& rd = app.render;
        Theme& th = app.theme;
        th.draw_stone_bg(rd, app.time);
        draw_header(app, "CONTROLES", CANVAS_W / 2.f, 4, 0.7f);
        // abas
        {
            float total = 0; float w[P_COUNT];
            for (int i = 0; i < P_COUNT; ++i) { w[i] = th.fonts.menu->measure(kPageName[i]) * 0.75f + 40; total += w[i]; }
            float x = CANVAS_W / 2.f - total / 2;
            for (int i = 0; i < P_COUNT; ++i) {
                bool on = i == page_;
                if (on) { rd.fill({x + 3, 66, w[i] - 6, 30}, Color::hex(0x07111f, 200)); rd.line_rect({x + 3, 66, w[i] - 6, 30}, kGold, 2); }
                TextStyle st = th.item_style(on, true); st.align = Align::Center; st.scale = 0.75f;
                draw_text(rd, *th.fonts.menu, x + w[i] / 2, 67, kPageName[i], st);
                x += w[i];
            }
        }
        // diagramas lado a lado
        Rect pl{40, 102, 600, 216}, pr{660, 102, 580, 216};
        th.draw_panel(rd, pl, 0.72f); th.draw_panel(rd, pr, 0.72f);
        Lit lit; int focus_btn = -1;
        std::vector<std::array<int, 3>> marks;
        const bool have_pad = app.input.pad(player_) != nullptr;
        if (page_ == P_TEST) {
            unsigned m = test_mask(app);
            for (int b = 0; b < NB; ++b) lit.on[b] = (m & kBit[b]) != 0;
            live_marks(app, marks);
        } else if (page_ == P_BINDS && focus_ < (int)rows_.size() && rows_[focus_].id >= 0 && rows_[focus_].id < NB) {
            focus_btn = rows_[focus_].id;
            const Bind& b = pc_.pad[focus_btn];
            if (b.kind) marks.push_back({b.kind, b.code, b.dir});
        } else if (page_ == P_AUTOFIRE && focus_ < (int)rows_.size() && rows_[focus_].id >= 0 && rows_[focus_].id < NB) {
            focus_btn = rows_[focus_].id;
            const Bind& b = pc_.pad[focus_btn];
            if (b.kind) marks.push_back({b.kind, b.code, b.dir});
        } else if (page_ == P_COMBOS && focus_ < (int)rows_.size() && rows_[focus_].id >= 0 && rows_[focus_].id < 8) {
            const Combo& c = pc_.combo[rows_[focus_].id];
            for (int b = 0; b < NB; ++b) lit.on[b] = (c.dest & kBit[b]) != 0;
            if (c.kind == 1 || c.kind == 2) marks.push_back({c.kind, c.code, c.dir});
        }
        // se um bind fisico do controle esta focado, o botao Mega Drive correspondente tambem acende (realce cruzado)
        draw_md_pad(app, pl.x + pl.w / 2, pl.y + 122, lit, focus_btn);
        draw_phys_pad(app, pr.x + pr.w / 2, pr.y + 132, fam_, marks, have_pad);
        {
            TextStyle cap = th.help_style(); cap.align = Align::Center;
            draw_text(rd, *th.fonts.help, pl.x + pl.w / 2, pl.y + 2, "Mega Drive (" + std::string(pc_.pad_type ? "6" : "3") + " botões) \xE2\x80\x94 P" + std::to_string(player_ + 1), cap);
            std::string nm = have_pad ? std::string(app.input.pad_name(player_)) : "nenhum controle detectado";
            if (have_pad) nm += std::string(" (") + kFamily[fam_] + ")";
            if (nm.size() > 52) nm = nm.substr(0, 52) + "…";
            draw_text(rd, *th.fonts.help, pr.x + pr.w / 2, pr.y + 2, nm, cap);
        }
        // lista
        th.draw_panel(rd, list_, 0.72f);
        if (page_ == P_TEST) draw_test(app); else draw_rows(app);
        // ajuda
        {
            std::string help;
            if (page_ != P_TEST && focus_ < (int)rows_.size()) help = rows_[focus_].help;
            if (page_ == P_TEST) help = "Aperte botões, teclas ou gatilhos: o resultado (binds + combos + deadzone + autofire) acende no Mega Drive.";
            TextStyle hs = th.help_style(); hs.align = Align::Center;
            auto lines = wrap_text(*th.fonts.help, help, CANVAS_W - 120);
            for (size_t k = 0; k < lines.size() && k < 2; ++k) draw_text(rd, *th.fonts.help, CANVAS_W / 2.f, list_.y + list_.h + 4 + k * 22.f, lines[k], hs);
        }
        draw_help_bar(app, default_hints(app, app.input.state(), true));
        if (mode_ == Mode::Capture) draw_capture(app);
        if (mode_ == Mode::DestEdit) draw_dest(app);
    }

private:
    static constexpr int kVis = 6;
    static constexpr float kRowH = 40;
    const Rect list_{40, 326, 1200, 264};

    // ------- linhas
    void build_rows(App& app) {
        rows_.clear();
        auto hdr = [&](const std::string& s) { CRow r; r.header = true; r.label = s; rows_.push_back(r); };
        auto info = [&](const std::string& s, Color tint = Color{255, 255, 255, 0}) { CRow r; r.info = true; r.label = s; r.tint = tint; rows_.push_back(r); };
        static const char* devn[] = {"Nenhum", "Teclado", "Controle", "Teclado + controle"};
        if (page_ == P_PLAYER) {
            CRow r;
            r.id = 0; r.label = "Jogador"; r.value = "P" + std::to_string(player_ + 1); r.arrows = true; r.help = "Escolhe qual jogador (P1 a P6) está sendo configurado."; rows_.push_back(r);
            PCfg d = defaults(player_);
            r = CRow(); r.id = 1; r.label = "Dispositivo"; r.value = devn[pc_.device]; r.arrows = true; r.modified = pc_.device != d.device;
            r.help = "Quem controla este jogador: nenhum, teclado, controle ou ambos (as entradas se somam). O P2 vem desligado."; rows_.push_back(r);
            r = CRow(); r.id = 2; r.label = "Tipo de controle"; r.value = pc_.pad_type ? "6 botões" : "3 botões"; r.arrows = true; r.modified = pc_.pad_type != d.pad_type;
            r.help = "Com 3 botões os botões X, Y, Z e Mode do controle são ignorados. O Eternal Champions usa 6 botões (3 socos + 3 chutes); o jogo força 6 botões em P1 e P2."; rows_.push_back(r);
            r = CRow(); r.id = 3; r.label = "Zona morta (deadzone)"; r.value = std::to_string(pc_.deadzone) + "%"; r.arrows = true; r.modified = pc_.deadzone != 25;
            r.help = "Quanto o analógico precisa se mover antes de contar como apertado (0 a 100%)."; rows_.push_back(r);
            hdr("Presets");
            r = CRow(); r.id = 4; r.label = "Preset"; r.value = kPresets[preset_].label; r.arrows = true; r.help = kPresets[preset_].help + std::string(" Enter aplica ao P") + std::to_string(player_ + 1) + "."; rows_.push_back(r);
            r = CRow(); r.id = 5; r.label = "Aplicar o preset acima"; r.value = "Enter"; r.help = "Reescreve os binds, combos e autofire deste jogador conforme o preset."; rows_.push_back(r);
            hdr("Atalhos do controle");
            r = CRow(); r.id = 6; r.label = "Atalhos do pad (quick save/load, turbo)"; r.value = hotkeys_on(app) ? "Ligados" : "Desligados"; r.arrows = true; r.modified = !hotkeys_on(app);
            r.help = "[input] pad_hotkeys: LB = quick save, RB = quick load, Back = turbo/overlay. Desligue para usar LB/RB/Back nos botões do jogo."; rows_.push_back(r);
            bool conflict = hotkeys_on(app) && uses_hotkey_buttons(pc_);
            info(conflict ? "⚠ Há binds/combos em LB, RB ou Back, que são atalhos do runner. Desligue os atalhos do pad para evitar conflito." : "Sem conflito entre os binds e os atalhos do pad.", conflict ? Color::hex(0xffb347) : Color{255, 255, 255, 0});
        } else if (page_ == P_BINDS) {
            for (int b = 0; b < NB; ++b) {
                CRow r; r.id = b; r.label = kBtn[b];
                PCfg d = defaults(player_);
                r.value = "Teclado: " + key_name(pc_.key[b]) + "   Controle: " + bind_name(fam_, pc_.pad[b]);
                r.modified = pc_.key[b] != d.key[b] || fmt_bind(pc_.pad[b]) != fmt_bind(d.pad[b]);
                r.dim = !pc_.pad_type && (b == B_X || b == B_Y || b == B_Z || b == B_MODE);
                r.help = std::string("Enter e depois aperte uma tecla ou botão do controle (5 s; Esc cancela). X restaura o padrão.") + (r.dim ? " Inerte em modo de 3 botões." : "");
                rows_.push_back(r);
            }
        } else if (page_ == P_COMBOS) {
            info("Um botão físico aciona vários botões do Mega Drive ao mesmo tempo (somado aos binds normais). Até 8 combos por jogador.");
            int used = 0;
            for (int k = 0; k < 8; ++k) {
                if (!pc_.combo[k].dest) continue;
                ++used;
                CRow r; r.id = k; r.label = "Combo " + std::to_string(k + 1);
                r.value = combo_src_name(fam_, pc_.combo[k]) + "  →  " + dest_name(pc_.combo[k].dest);
                r.modified = true;
                r.help = "Enter edita os destinos; X ou botão direito apaga o combo.";
                rows_.push_back(r);
            }
            CRow n; n.id = 100; n.label = "+ Novo combo"; n.value = used >= 8 ? "(limite de 8)" : "Enter"; n.dim = used >= 8;
            n.help = "Aperte o botão, tecla ou gatilho de origem e depois marque os botões do Mega Drive que ele aciona."; rows_.push_back(n);
        } else if (page_ == P_AUTOFIRE) {
            info("Repete o botão enquanto segurado (0 = desligado). Só botões de ação.");
            for (int b : {B_A, B_B, B_C, B_X, B_Y, B_Z, B_START}) {
                CRow r; r.id = b; r.label = kBtn[b]; r.arrows = true;
                r.value = pc_.af[b] ? std::to_string(pc_.af[b]) + " Hz" : "Desligado"; r.modified = pc_.af[b] != 0;
                r.help = "Frequência de repetição de " + std::string(kBtn[b]) + " (0 a 30 Hz)."; rows_.push_back(r);
            }
        }
    }
    CRow* sel_row() { return focus_ >= 0 && focus_ < (int)rows_.size() ? &rows_[focus_] : nullptr; }
    void move(int dir) {
        int n = (int)rows_.size(); if (!n) return;
        int i = std::max(0, std::min(n - 1, focus_));
        for (int k = 0; k < n; ++k) {
            i = (i + dir + n) % n;
            if (!rows_[i].header && !rows_[i].info) { focus_ = i; break; }
        }
        if (focus_ < scroll_) scroll_ = focus_;
        if (focus_ >= scroll_ + kVis) scroll_ = focus_ - kVis + 1;
        if (scroll_ > 0 && focus_ == scroll_ && rows_[scroll_ - 1].header) --scroll_;
    }

    void draw_rows(App& app) {
        Render& rd = app.render; Theme& th = app.theme;
        const Font& mf = *th.fonts.menu;
        scroll_ = std::max(0, std::min(scroll_, std::max(0, (int)rows_.size() - kVis)));
        if (focus_ < scroll_) scroll_ = std::max(0, focus_);
        if (focus_ >= scroll_ + kVis) scroll_ = focus_ - kVis + 1;
        int end = std::min((int)rows_.size(), scroll_ + kVis);
        for (int i = scroll_; i < end; ++i) {
            CRow& r = rows_[i];
            float y = list_.y + 12 + (i - scroll_) * kRowH;
            Rect rr{list_.x + 18, y, list_.w - 36, kRowH};
            if (r.header) {
                draw_text(rd, *th.fonts.help, rr.x + 8, y + 8, r.label, TextStyle::solid(th.pal.gold_top, 1, th.pal.gold_outline));
                rd.fill({rr.x + 8, y + kRowH - 6, rr.w - 16, 2}, Color::hex(0xf9d64a, 90));
                continue;
            }
            if (r.info) {
                TextStyle hs = r.tint.a ? TextStyle::solid(Color{r.tint.r, r.tint.g, r.tint.b, 255}, 1, th.pal.text_outline) : th.help_style();
                auto lines = wrap_text(*th.fonts.help, r.label, rr.w - 20);
                draw_text(rd, *th.fonts.help, rr.x + 8, y + 8, lines.empty() ? "" : lines[0], hs);
                continue;
            }
            bool foc = i == focus_;
            if (foc) { rd.fill(rr, Color::hex(0xf9d64a, 38)); rd.line_rect(rr, Color::hex(0xf9d64a, 150), 1); }
            TextStyle st = th.item_style(foc, !r.dim); st.scale = 0.78f;
            float ty = y + (kRowH - mf.line_height() * st.scale) * 0.5f;
            if (r.modified) draw_text(rd, mf, rr.x + 12, ty, "•", TextStyle::solid(kGold, 1, th.pal.text_outline));
            draw_text(rd, mf, rr.x + 34, ty, r.label, st);
            TextStyle vs = st; vs.align = Align::Right;
            std::string v = r.value;
            if (foc && r.arrows) v = "◄  " + v + "  ►";
            draw_text(rd, mf, rr.x + rr.w - 14, ty, v, vs);
        }
        if (scroll_ > 0) draw_text(rd, *th.fonts.help, list_.x + list_.w - 30, list_.y + 2, "▲", th.help_style());
        if (end < (int)rows_.size()) draw_text(rd, *th.fonts.help, list_.x + list_.w - 30, list_.y + list_.h - 24, "▼", th.help_style());
    }

    // ------- acoes
    void adjust(App& app, int dir) {
        CRow* r = sel_row(); if (!r) return;
        if (page_ == P_PLAYER) {
            switch (r->id) {
            case 0: player_ = (player_ + dir + 6) % 6; break;
            case 1: put(app, player_, "device", std::to_string((pc_.device + dir + 4) % 4)); break;
            case 2: put(app, player_, "pad_type", pc_.pad_type ? "0" : "1"); break;
            case 3: put(app, player_, "deadzone", std::to_string(std::min(100, std::max(0, pc_.deadzone + dir * 5)))); break;
            case 4: preset_ = (preset_ + dir + 4) % 4; break;
            case 6: set_hotkeys(app, !hotkeys_on(app)); break;
            }
        } else if (page_ == P_AUTOFIRE && r->id < NB) {
            int v = std::min(30, std::max(0, pc_.af[r->id] + dir));
            if (v == 0) del(app, player_, std::string("autofire.") + kBtn[r->id]); else put(app, player_, std::string("autofire.") + kBtn[r->id], std::to_string(v));
        }
    }
    void activate(App& app) {
        CRow* r = sel_row(); if (!r) return;
        if (page_ == P_PLAYER) {
            if (r->id == 5) {
                apply_preset_cfg(app, player_, kPresets[preset_].id);
                app.log(std::string("controles: preset ") + kPresets[preset_].id + " aplicado ao P" + std::to_string(player_ + 1));
                app.toast(std::string("Preset \"") + kPresets[preset_].label + "\" aplicado ao P" + std::to_string(player_ + 1) + ".");
                PCfg c = load_pcfg(app, player_);
                if (hotkeys_on(app) && uses_hotkey_buttons(c))
                    app.push(make_confirm("CONFLITO COM ATALHOS", "Este preset usa LB, RB ou Back, que no runner são quick save, quick load e turbo. Desativar os atalhos do pad (pad_hotkeys = 0)?", "Desativar", [](App& a) { set_hotkeys(a, false); a.toast("Atalhos do pad desativados."); }));
            } else adjust(app, +1);
        } else if (page_ == P_BINDS) begin_capture(app, CapTarget::Bind, r->id);
        else if (page_ == P_COMBOS) {
            if (r->id == 100) { if (!r->dim) begin_capture(app, CapTarget::ComboNew, -1); }
            else { dest_k_ = r->id; dest_tmp_ = pc_.combo[r->id]; dest_sel_ = 0; mode_ = Mode::DestEdit; }
        } else if (page_ == P_AUTOFIRE) adjust(app, +1);
    }
    void reset(App& app) {
        CRow* r = sel_row(); if (!r) return;
        if (page_ == P_BINDS && r->id < NB) {
            PCfg d = defaults(player_); int b = r->id;
            put_key(app, player_, b, d.key[b]); put_pad(app, player_, b, d.pad[b]);
        } else if (page_ == P_COMBOS && r->id < 8) del_combo(app, r->id);
        else if (page_ == P_AUTOFIRE && r->id < NB) del(app, player_, std::string("autofire.") + kBtn[r->id]);
        else if (page_ == P_PLAYER) {
            PCfg d = defaults(player_);
            if (r->id == 1) put(app, player_, "device", std::to_string(d.device));
            if (r->id == 2) put(app, player_, "pad_type", std::to_string(d.pad_type));
            if (r->id == 3) put(app, player_, "deadzone", "25");
            if (r->id == 6) set_hotkeys(app, true);
        }
    }
    void del_combo(App& app, int k) { del(app, player_, "combo." + std::to_string(k + 1)); }

    // ------- captura de bind
    void begin_capture(App& app, CapTarget t, int btn_idx) {
        cap_target_ = t; cap_btn_ = btn_idx; cap_t_ = 5.f; mode_ = Mode::Capture;
        app.input.capture_enabled = true;
    }
    void update_capture(App& app, const InputState& in, float dt) {
        if (app.shot_mode) return;
        cap_t_ -= dt;
        if (cap_t_ <= 0 || in.mouse_right_click) { mode_ = Mode::Normal; app.input.capture_enabled = false; app.toast("Captura cancelada."); return; }
        const Capture& c = in.cap;
        if (!c.kind) return;
        if (c.kind == 1 && c.code == SDL_SCANCODE_ESCAPE) { mode_ = Mode::Normal; app.input.capture_enabled = false; return; }
        mode_ = Mode::Normal; app.input.capture_enabled = false;
        if (cap_target_ == CapTarget::Bind) apply_bind(app, c);
        else {
            Combo cb; cb.kind = c.kind == 1 ? 3 : (c.kind == 2 ? 1 : 2); cb.code = c.code; cb.dir = c.dir;
            if (cap_target_ == CapTarget::ComboNew) {
                int slot = -1;
                for (int k = 0; k < 8 && slot < 0; ++k) if (!pc_.combo[k].dest) slot = k;
                if (slot < 0) { app.toast("Limite de 8 combos."); return; }
                dest_k_ = slot;
            }
            cb.dest = 0; dest_tmp_ = cb; dest_new_ = true; dest_sel_ = 0; mode_ = Mode::DestEdit;
        }
    }
    void apply_bind(App& app, const Capture& c) {
        int b = cap_btn_, p = player_;
        bool is_key = c.kind == 1;
        Bind nb; if (!is_key) { nb.kind = c.kind == 2 ? 1 : 2; nb.code = c.code; nb.dir = c.dir; }
        // conflito?
        int other = -1;
        for (int o = 0; o < NB && other < 0; ++o) {
            if (o == b) continue;
            if (is_key ? (pc_.key[o] == c.code) : (pc_.pad[o].kind == nb.kind && pc_.pad[o].code == nb.code && pc_.pad[o].dir == nb.dir)) other = o;
        }
        auto write = [is_key, nb, c](App& a, int pl, int bt) { if (is_key) put_key(a, pl, bt, c.code); else put_pad(a, pl, bt, nb); };
        if (other < 0) { write(app, p, b); app.toast(is_key ? "Tecla: " + key_name(c.code) : "Controle: " + bind_name(fam_, nb)); return; }
        std::string what = is_key ? key_name(c.code) : bind_name(fam_, nb);
        int oldk = pc_.key[b]; Bind oldp = pc_.pad[b];
        app.push(make_confirm("BIND EM CONFLITO", "\"" + what + "\" já é usado por " + kBtn[other] + ". Trocar os dois (" + kBtn[b] + " ↔ " + kBtn[other] + ")?", "Trocar",
                              [=](App& a) {
                                  write(a, p, b);
                                  if (is_key) put_key(a, p, other, oldk); else put_pad(a, p, other, oldp);
                              }));
    }
    void draw_capture(App& app) {
        Render& rd = app.render; Theme& th = app.theme;
        rd.fill({0, 0, CANVAS_W, CANVAS_H}, Color::hex(0x040a14, 170));
        Rect p{320, 220, 640, 250};
        th.draw_panel(rd, p, 0.95f);
        std::string what = cap_target_ == CapTarget::Bind ? std::string("Aperte o botão para ") + kBtn[cap_btn_] + "…" : "Aperte o botão, tecla ou gatilho de origem…";
        TextStyle g = th.gold_style(0.45f); g.align = Align::Center;
        draw_text(rd, *th.fonts.title, CANVAS_W / 2.f, p.y + 20, "CAPTURAR", g);
        TextStyle it = th.item_style(true, true); it.align = Align::Center;
        draw_text(rd, *th.fonts.menu, CANVAS_W / 2.f, p.y + 100, what, it);
        float f = std::max(0.f, std::min(1.f, cap_t_ / 5.f));
        rd.fill({p.x + 60, p.y + 168, p.w - 120, 12}, Color::hex(0x07111f));
        rd.fill({p.x + 60, p.y + 168, (p.w - 120) * f, 12}, kGold);
        TextStyle hs = th.help_style(); hs.align = Align::Center;
        draw_text(rd, *th.fonts.help, CANVAS_W / 2.f, p.y + 196, "Teclado, botão ou gatilho do controle  \xE2\x80\x94  Esc cancela (ou espere 5 s)", hs);
        th.draw_yinyang(rd, CANVAS_W / 2.f - 250, p.y + 105, 40, app.time * 90.0);
        th.draw_yinyang(rd, CANVAS_W / 2.f + 250, p.y + 105, 40, -app.time * 90.0);
    }

    // ------- editor de destinos do combo
    void update_dest(App& app, const InputState& in) {
        const int items = 8 + 2;   // A B C X Y Z Start Mode + Concluir + Apagar
        if (in.p(A_UP)) dest_sel_ = (dest_sel_ + items - 1) % items;
        if (in.p(A_DOWN)) dest_sel_ = (dest_sel_ + 1) % items;
        static const int dbtn[8] = {B_A, B_B, B_C, B_X, B_Y, B_Z, B_START, B_MODE};
        auto finish = [&]() {
            mode_ = Mode::Normal;
            if (dest_tmp_.dest) { put_combo(app, player_, dest_k_, dest_tmp_); app.log("controles: " + fmt_combo(dest_tmp_)); }
            else if (!dest_new_) del_combo(app, dest_k_);
            dest_new_ = false;
        };
        if (in.p(A_BACK) || in.mouse_right_click) { finish(); return; }
        if (in.p(A_START)) { finish(); return; }
        if (in.p(A_ACCEPT) || in.p(A_LEFT) || in.p(A_RIGHT)) {
            if (dest_sel_ < 8) dest_tmp_.dest ^= kBit[dbtn[dest_sel_]];
            else if (dest_sel_ == 8) finish();
            else { dest_tmp_.dest = 0; finish(); }
        }
    }
    void draw_dest(App& app) {
        Render& rd = app.render; Theme& th = app.theme;
        rd.fill({0, 0, CANVAS_W, CANVAS_H}, Color::hex(0x040a14, 170));
        Rect p{360, 110, 560, 500};
        th.draw_panel(rd, p, 0.95f);
        TextStyle g = th.gold_style(0.45f); g.align = Align::Center;
        draw_text(rd, *th.fonts.title, CANVAS_W / 2.f, p.y + 12, "COMBO " + std::to_string(dest_k_ + 1), g);
        TextStyle hs = th.help_style(); hs.align = Align::Center;
        draw_text(rd, *th.fonts.help, CANVAS_W / 2.f, p.y + 80, "Origem: " + combo_src_name(fam_, dest_tmp_), hs);
        static const int dbtn[8] = {B_A, B_B, B_C, B_X, B_Y, B_Z, B_START, B_MODE};
        const Font& mf = *th.fonts.menu;
        for (int i = 0; i < 10; ++i) {
            float y = p.y + 114 + i * 37;
            bool foc = i == dest_sel_;
            if (foc) rd.fill({p.x + 30, y, p.w - 60, 35}, Color::hex(0xf9d64a, 40));
            TextStyle st = th.item_style(foc, true); st.scale = 0.78f;
            if (i < 8) {
                bool on = (dest_tmp_.dest & kBit[dbtn[i]]) != 0;
                rd.fill({p.x + 48, y + 6, 22, 22}, Color::hex(0x07111f)); rd.line_rect({p.x + 48, y + 6, 22, 22}, Color::hex(0xdde6ee), 2);
                if (on) rd.fill({p.x + 53, y + 11, 12, 12}, kGold);
                draw_text(rd, mf, p.x + 86, y + 3, kBtn[dbtn[i]], st);
            } else {
                st.align = Align::Center;
                draw_text(rd, mf, CANVAS_W / 2.f, y + 3, i == 8 ? "Concluir" : "Apagar combo", st);
            }
        }
    }

    // ------- teste ao vivo (mesma resolucao do runner)
    unsigned test_mask(App& app) {
        if (app.shot_mode) return kBit[B_A] | kBit[B_X] | kBit[B_Y];   // captura de tela: exemplo
        const PCfg& c = pc_;
        unsigned m = 0;
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        if (c.device & 1) for (int b = 0; b < NB; ++b) { int sc = c.key[b]; if (sc > 0 && sc < SDL_NUM_SCANCODES && ks[sc]) m |= kBit[b]; }
        SDL_GameController* pad = app.input.pad(player_);
        int dz = std::min(100, std::max(0, c.deadzone)) * 32767 / 100;
        auto held = [&](int kind, int code, int dir) {
            if (!pad) return false;
            if (kind == 1) return SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)code) != 0;
            int v = SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis)code);
            return dir < 0 ? v < -dz : v > dz;
        };
        if (c.device & 2) {
            for (int b = 0; b < NB; ++b) {
                if (!c.pad_type && b >= B_X) continue;   // 3 botoes: X/Y/Z/Mode inertes
                if (c.pad[b].kind && held(c.pad[b].kind, c.pad[b].code, c.pad[b].dir)) m |= kBit[b];
            }
            if (pad) {
                int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX), ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
                if (lx < -dz) m |= kBit[B_LEFT]; if (lx > dz) m |= kBit[B_RIGHT];
                if (ly < -dz) m |= kBit[B_UP]; if (ly > dz) m |= kBit[B_DOWN];
            }
        }
        for (int k = 0; k < 8; ++k) {
            const Combo& cb = c.combo[k];
            if (!cb.dest) continue;
            bool h = false;
            if (cb.kind == 3) h = (c.device & 1) && cb.code > 0 && cb.code < SDL_NUM_SCANCODES && ks[cb.code];
            else h = (c.device & 2) && held(cb.kind == 1 ? 1 : 2, cb.code, cb.dir);
            if (h) m |= cb.dest;
        }
        for (int b = 0; b < NB; ++b) {
            int hz = c.af[b];
            if (hz <= 0 || !(m & kBit[b])) continue;
            unsigned half = (unsigned)((30 + hz / 2) / hz);
            if (half < 1) half = 1;
            if ((frame_ / half) & 1u) m &= ~kBit[b];
        }
        return m;
    }
    void live_marks(App& app, std::vector<std::array<int, 3>>& marks) {
        if (app.shot_mode) { marks.push_back({1, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0}); return; }
        SDL_GameController* pad = app.input.pad(player_);
        if (!pad) return;
        for (int b = 0; b < 15; ++b) if (SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)b)) marks.push_back({1, b, 0});
        for (int a = 0; a < 6; ++a) { int v = SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis)a); if (v > 16000) marks.push_back({2, a, 1}); if (v < -16000) marks.push_back({2, a, -1}); }
    }
    void draw_test(App& app) {
        Render& rd = app.render; Theme& th = app.theme;
        unsigned m = test_mask(app);
        TextStyle hs = th.help_style();
        float x = list_.x + 30, y = list_.y + 18;
        draw_text(rd, *th.fonts.help, x, y, std::string("P") + std::to_string(player_ + 1) + "  \xE2\x80\x94  dispositivo: " + std::vector<std::string>{"nenhum", "teclado", "controle", "teclado + controle"}[pc_.device] +
                  "  \xE2\x80\x94  " + (pc_.pad_type ? "6 botões" : "3 botões") + "  \xE2\x80\x94  deadzone " + std::to_string(pc_.deadzone) + "%" + (app.shot_mode ? "   (exemplo)" : ""), hs);
        y += 38;
        draw_text(rd, *th.fonts.help, x, y, "Botões do Mega Drive acionados:", hs);
        y += 34;
        float bx = x;
        for (int b = 0; b < NB; ++b) {
            bool on = (m & kBit[b]) != 0;
            float w = th.fonts.menu->measure(kBtn[b]) * 0.7f + 30;
            Rect rc{bx, y, w, 40};
            rd.fill(rc, on ? kGold : Color::hex(0x07111f, 220));
            rd.line_rect(rc, on ? Color::hex(0xffffff) : Color::hex(0x6aa9b0, 120), 2);
            TextStyle st = TextStyle::solid(on ? Color::hex(0x101018) : Color::hex(0xcfe0e8), 0); st.align = Align::Center; st.scale = 0.7f;
            draw_text(rd, *th.fonts.menu, bx + w / 2, y + 7, kBtn[b], st);
            bx += w + 8;
        }
        y += 56;
        std::string combos;
        for (auto& cb : pc_.combo) if (cb.dest) combos += (combos.empty() ? "" : "   ") + combo_src_name(fam_, cb) + "→" + dest_name(cb.dest);
        draw_text(rd, *th.fonts.help, x, y, "Combos ativos no perfil: " + (combos.empty() ? std::string("nenhum") : combos), hs);
    }

    int player_ = 0, page_ = 0, focus_ = 0, scroll_ = 0, preset_ = 0, fam_ = 0;
    PCfg pc_;
    std::vector<CRow> rows_;
    Mode mode_ = Mode::Normal;
    CapTarget cap_target_ = CapTarget::Bind;
    int cap_btn_ = 0;
    float cap_t_ = 5.f, t_ = 0;
    int dest_k_ = 0, dest_sel_ = 0;
    Combo dest_tmp_;
    bool dest_new_ = false;
    unsigned frame_ = 0;
};
}  // namespace

std::unique_ptr<Screen> make_controls_screen() { return std::make_unique<Controls>(); }
void apply_controls_preset(App& app, int player, const std::string& id) { apply_preset_cfg(app, player, id); }
