// voices_toml.cpp - ver voices_toml.h
#include "voices_toml.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace ecr {

// ---------------------------------------------------------------------------
// utilidades
// ---------------------------------------------------------------------------

std::FILE* fopen_u8(const std::string& path, const char* mode) {
#ifdef _WIN32
    std::wstring m;
    for (const char* p = mode; *p; ++p) m += (wchar_t)*p;
    return _wfopen(fs::u8path(path).c_str(), m.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

bool read_file(const std::string& path, std::string* out) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

bool write_file_atomic(const std::string& path, const std::string& data, bool make_bak, std::string* err) {
    std::error_code ec;
    fs::path p = fs::u8path(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += fs::u8path(".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "nao foi possivel criar " + tmp.u8string();
            return false;
        }
        f.write(data.data(), (std::streamsize)data.size());
        f.flush();
        if (!f) {
            if (err) *err = "falha ao escrever " + tmp.u8string();
            return false;
        }
    }
    if (make_bak && fs::exists(p, ec)) {
        fs::path bak = p;
        bak += fs::u8path(".bak");
        fs::copy_file(p, bak, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            if (err) *err = "falha no backup: " + ec.message();
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, p, ec);  // substitui o destino (MoveFileEx REPLACE_EXISTING no Windows)
    if (ec) {
        if (err) *err = "falha ao renomear: " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool VoiceEdit::operator==(const VoiceEdit& o) const {
    return name == o.name && mode == o.mode && program == o.program && bank == o.bank &&
           transpose == o.transpose && volume_db == o.volume_db && drum_note == o.drum_note;
}

bool ProfileChannel::operator==(const ProfileChannel& o) const {
    return name == o.name && role == o.role && mode == o.mode && program == o.program && bank == o.bank &&
           transpose == o.transpose && volume_db == o.volume_db && pan == o.pan && mute == o.mute &&
           solo == o.solo && eq_low_db == o.eq_low_db && eq_mid_db == o.eq_mid_db && eq_high_db == o.eq_high_db;
}

namespace {

// UTF-8 -> codepoints (tolerante)
std::vector<uint32_t> utf8_decode(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp = c;
        int extra = 0;
        if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
        ++i;
        for (int k = 0; k < extra && i < n; ++k, ++i) cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
        out.push_back(cp);
    }
    return out;
}

void utf8_append(std::string& s, uint32_t cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F));
    }
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

bool starts_with(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

std::string fmt_fixed(double v, int prec) {
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, prec);
    return std::string(buf, r.ptr);
}

// string basica simples (perfis): so escapa \ e "
std::string quote_basic(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"') o += '\\';
        o += c;
    }
    return o + "\"";
}

// ---------------------------------------------------------------------------
// valores TOML (subconjunto)
// ---------------------------------------------------------------------------

struct TVal {
    enum Kind { None, Int, Float, Str, Bool, Arr, Tbl } k = None;
    long long i = 0;
    double d = 0.0;
    bool b = false;
    std::string s;
    std::vector<TVal> a;
    std::vector<std::pair<std::string, TVal>> t;

    const TVal* get(const char* key) const {
        for (auto& kv : t) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    double num() const { return k == Int ? (double)i : d; }
    bool isnum() const { return k == Int || k == Float; }
};

struct Cur {
    const std::string& s;
    size_t i = 0;
};

void skip_ws(Cur& c, bool newlines) {
    while (c.i < c.s.size()) {
        char ch = c.s[c.i];
        if (ch == ' ' || ch == '\t' || ch == '\r') ++c.i;
        else if (newlines && ch == '\n') ++c.i;
        else if (newlines && ch == '#') { while (c.i < c.s.size() && c.s[c.i] != '\n') ++c.i; }
        else break;
    }
}

bool parse_string(Cur& c, std::string& out) {
    out.clear();
    if (c.i >= c.s.size()) return false;
    char q = c.s[c.i];
    if (q == '\'') {
        ++c.i;
        while (c.i < c.s.size() && c.s[c.i] != '\'') out += c.s[c.i++];
        if (c.i >= c.s.size()) return false;
        ++c.i;
        return true;
    }
    if (q != '"') return false;
    ++c.i;
    auto hex4 = [&](uint32_t& v) {
        v = 0;
        for (int k = 0; k < 4; ++k) {
            if (c.i >= c.s.size()) return false;
            char h = c.s[c.i++];
            v <<= 4;
            if (h >= '0' && h <= '9') v |= (uint32_t)(h - '0');
            else if (h >= 'a' && h <= 'f') v |= (uint32_t)(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= (uint32_t)(h - 'A' + 10);
            else return false;
        }
        return true;
    };
    while (c.i < c.s.size()) {
        char ch = c.s[c.i++];
        if (ch == '"') return true;
        if (ch != '\\') { out += ch; continue; }
        if (c.i >= c.s.size()) return false;
        char e = c.s[c.i++];
        switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case 'u': {
                uint32_t v;
                if (!hex4(v)) return false;
                if (v >= 0xD800 && v < 0xDC00 && c.i + 1 < c.s.size() && c.s[c.i] == '\\' && c.s[c.i + 1] == 'u') {
                    c.i += 2;
                    uint32_t lo;
                    if (!hex4(lo)) return false;
                    v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8_append(out, v);
                break;
            }
            case 'U': {
                uint32_t hi, lo;
                if (!hex4(hi) || !hex4(lo)) return false;
                utf8_append(out, (hi << 16) | lo);
                break;
            }
            default: out += e; break;
        }
    }
    return false;
}

bool parse_value(Cur& c, TVal& v, int depth = 0) {
    if (depth > 8) return false;
    skip_ws(c, true);
    if (c.i >= c.s.size()) return false;
    char ch = c.s[c.i];
    if (ch == '"' || ch == '\'') {
        v.k = TVal::Str;
        return parse_string(c, v.s);
    }
    if (ch == '[') {
        ++c.i;
        v.k = TVal::Arr;
        for (;;) {
            skip_ws(c, true);
            if (c.i >= c.s.size()) return false;
            if (c.s[c.i] == ']') { ++c.i; return true; }
            TVal e;
            if (!parse_value(c, e, depth + 1)) return false;
            v.a.push_back(std::move(e));
            skip_ws(c, true);
            if (c.i < c.s.size() && c.s[c.i] == ',') ++c.i;
        }
    }
    if (ch == '{') {
        ++c.i;
        v.k = TVal::Tbl;
        for (;;) {
            skip_ws(c, true);
            if (c.i >= c.s.size()) return false;
            if (c.s[c.i] == '}') { ++c.i; return true; }
            std::string key;
            if (c.s[c.i] == '"' || c.s[c.i] == '\'') {
                if (!parse_string(c, key)) return false;
            } else {
                while (c.i < c.s.size() && c.s[c.i] != '=' && c.s[c.i] != ' ' && c.s[c.i] != '\t') key += c.s[c.i++];
            }
            skip_ws(c, true);
            if (c.i >= c.s.size() || c.s[c.i] != '=') return false;
            ++c.i;
            TVal e;
            if (!parse_value(c, e, depth + 1)) return false;
            v.t.emplace_back(std::move(key), std::move(e));
            skip_ws(c, true);
            if (c.i < c.s.size() && c.s[c.i] == ',') ++c.i;
        }
    }
    if (starts_with(c.s.substr(c.i, 5), "true")) { c.i += 4; v.k = TVal::Bool; v.b = true; return true; }
    if (starts_with(c.s.substr(c.i, 6), "false")) { c.i += 5; v.k = TVal::Bool; v.b = false; return true; }
    std::string num;
    while (c.i < c.s.size()) {
        char x = c.s[c.i];
        if ((x >= '0' && x <= '9') || x == '+' || x == '-' || x == '.' || x == '_' || x == 'e' || x == 'E') {
            if (x != '_') num += x;
            ++c.i;
        } else break;
    }
    if (num.empty()) return false;
    if (num.find_first_of(".eE") != std::string::npos) {
        v.k = TVal::Float;
        v.d = std::strtod(num.c_str(), nullptr);
    } else {
        v.k = TVal::Int;
        v.i = std::strtoll(num.c_str(), nullptr, 10);
    }
    return true;
}

// ---------------------------------------------------------------------------
// documento: linhas cruas + indice de secoes/chaves
// ---------------------------------------------------------------------------

struct KV {
    std::string key, prefix, valtext, suffix;
    size_t lb = 0, le = 0;  // linhas [lb, le] (inclusive)
    TVal val;
};

struct Sec {
    std::string name;       // "" = preambulo
    size_t begin = 0, end = 0;  // [begin, end)
    std::vector<KV> kvs;
    const KV* find(const char* key) const {
        for (auto& k : kvs) if (k.key == key) return &k;
        return nullptr;
    }
};

// Escaneia a partir de `start`: devolve o fim do valor (antes de comentario) e atualiza o estado.
size_t scan_value(const std::string& s, size_t start, int& depth, char& quote) {
    size_t i = start;
    for (; i < s.size(); ++i) {
        char ch = s[i];
        if (quote) {
            if (quote == '"' && ch == '\\') { ++i; continue; }
            if (ch == quote) quote = 0;
            continue;
        }
        if (ch == '"' || ch == '\'') { quote = ch; continue; }
        if (ch == '[' || ch == '{') ++depth;
        else if (ch == ']' || ch == '}') --depth;
        else if (ch == '#') break;
    }
    return i;
}

struct Doc {
    std::vector<std::string> lines;
    std::vector<Sec> secs;

    void set_text(const std::string& text) {
        lines.clear();
        size_t b = 0;
        for (;;) {
            size_t e = text.find('\n', b);
            if (e == std::string::npos) { lines.push_back(text.substr(b)); break; }
            lines.push_back(text.substr(b, e - b));
            b = e + 1;
        }
        reparse();
    }

    std::string text() const {
        std::string o;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i) o += '\n';
            o += lines[i];
        }
        return o;
    }

    void reparse() {
        secs.clear();
        Sec cur;
        cur.begin = 0;
        size_t n = lines.size(), i = 0;
        while (i < n) {
            std::string t = trim(lines[i]);
            if (i == 0 && t.size() >= 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB &&
                (unsigned char)t[2] == 0xBF)
                t = trim(t.substr(3));
            if (!t.empty() && t[0] == '[' && !starts_with(t, "[[")) {
                cur.end = i;
                secs.push_back(std::move(cur));
                cur = Sec();
                cur.begin = i;
                size_t rb = t.find(']');
                cur.name = trim(t.substr(1, rb == std::string::npos ? std::string::npos : rb - 1));
                ++i;
                continue;
            }
            if (t.empty() || t[0] == '#') { ++i; continue; }
            const std::string& ln = lines[i];
            size_t eq = ln.find('=');
            if (eq == std::string::npos) { ++i; continue; }
            KV kv;
            kv.key = trim(ln.substr(0, eq));
            if (kv.key.size() >= 2 && (kv.key[0] == '"' || kv.key[0] == '\'')) kv.key = kv.key.substr(1, kv.key.size() - 2);
            size_t vs = eq + 1;
            while (vs < ln.size() && (ln[vs] == ' ' || ln[vs] == '\t')) ++vs;
            kv.prefix = ln.substr(0, vs);
            kv.lb = i;
            int depth = 0;
            char quote = 0;
            size_t ve = scan_value(ln, vs, depth, quote);
            std::string val = ln.substr(vs, ve - vs);
            size_t last = i;
            std::string lastline = ln;
            size_t vend_in_last = ve;
            size_t vstart_in_last = vs;
            while ((depth > 0 || quote) && last + 1 < n) {
                ++last;
                lastline = lines[last];
                vstart_in_last = 0;
                vend_in_last = scan_value(lastline, 0, depth, quote);
                val += "\n" + lastline.substr(0, vend_in_last);
            }
            // fim do valor sem espacos a direita (na ultima linha)
            size_t trimmed_end = vend_in_last;
            while (trimmed_end > vstart_in_last &&
                   (lastline[trimmed_end - 1] == ' ' || lastline[trimmed_end - 1] == '\t' || lastline[trimmed_end - 1] == '\r'))
                --trimmed_end;
            kv.suffix = lastline.substr(trimmed_end);
            // valtext: sem espacos a direita
            val = val.substr(0, val.size() - (vend_in_last - trimmed_end));
            kv.valtext = val;
            kv.le = last;
            Cur cu{kv.valtext, 0};
            TVal tv;
            if (parse_value(cu, tv)) kv.val = std::move(tv);
            cur.kvs.push_back(std::move(kv));
            i = last + 1;
        }
        cur.end = n;
        secs.push_back(std::move(cur));
    }

    int find_sec(const std::string& name) const {
        for (size_t i = 1; i < secs.size(); ++i) if (secs[i].name == name) return (int)i;
        return -1;
    }

    // Define o valor (texto TOML) de `key` em secs[si]; cria a chave apos a ultima se faltar.
    bool set_kv(int si, const std::string& key, const std::string& valtext) {
        if (si < 0 || (size_t)si >= secs.size()) return false;
        const Sec& s = secs[(size_t)si];
        for (const KV& kv : s.kvs) {
            if (kv.key != key) continue;
            if (kv.lb != kv.le) return false;
            lines[kv.lb] = kv.prefix + valtext + kv.suffix;
            reparse();
            return true;
        }
        size_t pos = s.kvs.empty() ? s.begin : s.kvs.back().le;
        lines.insert(lines.begin() + (std::ptrdiff_t)pos + 1, key + " = " + valtext);
        reparse();
        return true;
    }
};

bool is_hash8(const std::string& h) {
    if (h.size() != 8) return false;
    for (char c : h) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

std::vector<std::string> split_lines(const std::string& t) {
    std::vector<std::string> out;
    size_t b = 0;
    for (;;) {
        size_t e = t.find('\n', b);
        if (e == std::string::npos) { out.push_back(t.substr(b)); break; }
        out.push_back(t.substr(b, e - b));
        b = e + 1;
    }
    return out;
}

const char* kVoicesHeader =
#include "voices_header.inc"
    ;

int to_int(const TVal* v, int def) { return (v && v->isnum()) ? (int)v->num() : def; }
std::string to_str(const TVal* v, const std::string& def) { return (v && v->k == TVal::Str) ? v->s : def; }
double to_dbl(const TVal* v, double def) { return (v && v->isnum()) ? v->num() : def; }
bool to_bool(const TVal* v, bool def) { return (v && v->k == TVal::Bool) ? v->b : def; }

const TVal* kv_val(const Sec& s, const char* key) {
    const KV* k = s.find(key);
    return k ? &k->val : nullptr;
}

VoiceEdit parse_edit(const Sec& s) {
    VoiceEdit e;
    e.name = to_str(kv_val(s, "name"), "");
    e.mode = to_str(kv_val(s, "mode"), "gm");
    e.program = to_int(kv_val(s, "program"), 0);
    e.bank = to_int(kv_val(s, "bank"), 0);
    e.transpose = std::max(-48, std::min(48, to_int(kv_val(s, "transpose"), 0)));
    e.volume_db = std::max(-60.0, std::min(24.0, to_dbl(kv_val(s, "volume_db"), 0.0)));
    e.drum_note = to_int(kv_val(s, "drum_note"), 0);
    return e;
}

VoiceMeta parse_meta(const Sec& s) {
    VoiceMeta m;
    m.valid = true;
    m.alg = to_int(kv_val(s, "alg"), 0);
    m.fb = to_int(kv_val(s, "fb"), 0);
    m.ams_fms = to_int(kv_val(s, "ams_fms"), 0);
    m.patch = to_str(kv_val(s, "patch"), "");
    if (const TVal* ops = kv_val(s, "ops")) {
        for (const TVal& o : ops->a) {
            FmOp op;
            op.slot = to_str(o.get("slot"), "");
            op.carrier = to_bool(o.get("carrier"), false);
            op.mul = to_int(o.get("mul"), 0);
            op.dt = to_int(o.get("dt"), 0);
            op.ar = to_int(o.get("ar"), 0);
            op.d1r = to_int(o.get("d1r"), 0);
            op.d2r = to_int(o.get("d2r"), 0);
            op.sl = to_int(o.get("sl"), 0);
            op.rr = to_int(o.get("rr"), 0);
            op.tl = to_int(o.get("tl"), -1);
            m.ops.push_back(op);
        }
    }
    if (const TVal* a = kv_val(s, "songs")) for (auto& x : a->a) m.songs.push_back(x.s);
    if (const TVal* a = kv_val(s, "channels")) for (auto& x : a->a) m.channels.push_back(x.s);
    m.notes = to_int(kv_val(s, "notes"), 0);
    m.note_min = to_int(kv_val(s, "note_min"), 0);
    m.note_max = to_int(kv_val(s, "note_max"), 0);
    m.note_median = to_int(kv_val(s, "note_median"), 0);
    m.avg_dur_ms = to_int(kv_val(s, "avg_dur_ms"), 0);
    m.suggestion = to_str(kv_val(s, "suggestion"), "");
    return m;
}

std::string fmt_op(const FmOp& o) {
    std::string r = "{ slot = " + toml_quote(o.slot) + ", carrier = " + (o.carrier ? "true" : "false");
    r += ", mul = " + std::to_string(o.mul) + ", dt = " + std::to_string(o.dt) + ", ar = " + std::to_string(o.ar);
    r += ", d1r = " + std::to_string(o.d1r) + ", d2r = " + std::to_string(o.d2r) + ", sl = " + std::to_string(o.sl);
    r += ", rr = " + std::to_string(o.rr);
    if (!o.carrier && o.tl >= 0) r += ", tl = " + std::to_string(o.tl);
    return r + " }";
}

std::string fmt_str_list(const std::vector<std::string>& v) {
    std::string r = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) r += ", ";
        r += toml_quote(v[i]);
    }
    return r + "]";
}

// Bloco canonico (voice_catalog.write_toml) sem a linha em branco final.
std::vector<std::string> canonical_block(const VoiceEntry& v) {
    std::vector<std::string> L;
    const VoiceEdit& e = v.edit;
    L.push_back("[voices." + v.hash + "]");
    L.push_back("name = " + toml_quote(e.name));
    L.push_back("mode = " + toml_quote(e.mode) + "       # gm | percussion | chip | off");
    L.push_back("program = " + std::to_string(e.program));
    L.push_back("bank = " + std::to_string(e.bank));
    L.push_back("transpose = " + std::to_string(e.transpose));
    L.push_back("volume_db = " + fmt_float1(e.volume_db));
    L.push_back("drum_note = " + std::to_string(e.drum_note));
    L.push_back("[voices." + v.hash + ".meta]");
    const VoiceMeta& m = v.meta;
    L.push_back("alg = " + std::to_string(m.alg));
    L.push_back("fb = " + std::to_string(m.fb));
    L.push_back("ams_fms = " + std::to_string(m.ams_fms));
    L.push_back("patch = " + toml_quote(m.patch));
    L.push_back("ops = [");
    for (const FmOp& o : m.ops) L.push_back("  " + fmt_op(o) + ",");
    L.push_back("]");
    L.push_back("songs = " + fmt_str_list(m.songs));
    L.push_back("channels = " + fmt_str_list(m.channels));
    L.push_back("notes = " + std::to_string(m.notes));
    L.push_back("note_min = " + std::to_string(m.note_min));
    L.push_back("note_max = " + std::to_string(m.note_max));
    L.push_back("note_median = " + std::to_string(m.note_median));
    L.push_back("avg_dur_ms = " + std::to_string(m.avg_dur_ms));
    L.push_back("suggestion = " + toml_quote(m.suggestion));
    return L;
}

}  // namespace

std::string toml_quote(const std::string& s) {
    std::string o = "\"";
    char buf[16];
    for (uint32_t cp : utf8_decode(s)) {
        switch (cp) {
            case '"': o += "\\\""; continue;
            case '\\': o += "\\\\"; continue;
            case '\n': o += "\\n"; continue;
            case '\r': o += "\\r"; continue;
            case '\t': o += "\\t"; continue;
            case '\b': o += "\\b"; continue;
            case '\f': o += "\\f"; continue;
            default: break;
        }
        if (cp >= 0x20 && cp < 0x7F) { o += (char)cp; continue; }
        if (cp == 0x7F) { o += (char)cp; continue; }  // json.dumps nao escapa DEL
        if (cp >= 0x10000) {
            uint32_t v = cp - 0x10000;
            std::snprintf(buf, sizeof(buf), "\\u%04x\\u%04x", 0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF));
        } else {
            std::snprintf(buf, sizeof(buf), "\\u%04x", cp);
        }
        o += buf;
    }
    return o + "\"";
}

std::string fmt_float1(double v) {
    std::string s = fmt_fixed(v, 1);
    if (s == "-0.0") return s;  // Python tambem imprime -0.0
    return s;
}

std::string fmt_float_profile(double v) {
    std::string s = fmt_fixed(v, 4);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s.find('.') == std::string::npos) s += ".0";
    return s;
}

std::string clean_voice_name(const std::string& utf8) {
    std::vector<uint32_t> cps;
    for (uint32_t cp : utf8_decode(utf8)) {
        if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) continue;
        cps.push_back(cp);
    }
    size_t b = 0, e = cps.size();
    while (b < e && (cps[b] == ' ' || cps[b] == 0xA0)) ++b;
    while (e > b && (cps[e - 1] == ' ' || cps[e - 1] == 0xA0)) --e;
    if (e - b > 48) e = b + 48;
    std::string o;
    for (size_t i = b; i < e; ++i) utf8_append(o, cps[i]);
    return o;
}

// ---------------------------------------------------------------------------
// VoicesFile
// ---------------------------------------------------------------------------

struct VoicesFile::Impl {
    std::string path;
    Doc doc;
    std::vector<VoiceEntry> voices;
    bool dirty = false;
    bool exists = false;
    bool bad = false;        // existe mas nao foi possivel ler: save() bloqueado
    bool bak_done = false;   // .bak (estado ANTERIOR a sessao) so no primeiro save

    void rebuild() {
        voices.clear();
        for (size_t i = 1; i < doc.secs.size(); ++i) {
            const Sec& s = doc.secs[i];
            if (!starts_with(s.name, "voices.")) continue;
            std::string rest = s.name.substr(7);
            if (!is_hash8(rest)) continue;
            VoiceEntry v;
            v.hash = rest;
            v.edit = parse_edit(s);
            int mi = doc.find_sec("voices." + rest + ".meta");
            if (mi >= 0) v.meta = parse_meta(doc.secs[(size_t)mi]);
            voices.push_back(std::move(v));
        }
    }
};

VoicesFile::VoicesFile() : p_(new Impl) {}
VoicesFile::~VoicesFile() { delete p_; }
VoicesFile::VoicesFile(VoicesFile&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
VoicesFile& VoicesFile::operator=(VoicesFile&& o) noexcept {
    if (this != &o) { delete p_; p_ = o.p_; o.p_ = nullptr; }
    return *this;
}

bool VoicesFile::load(const std::string& path, std::string* err) {
    p_->path = path;
    p_->bad = false;
    p_->bak_done = false;
    std::string text;
    std::error_code fec;
    const bool existing = fs::exists(fs::u8path(path), fec);
    if (read_file(path, &text)) {
        p_->exists = true;
        p_->doc.set_text(text);
        p_->dirty = false;
    } else if (existing) {   // existe mas nao le (permissao/bloqueio): NAO tratar como arquivo novo
        p_->bad = true;
        p_->exists = true;
        if (err) *err = "nao foi possivel ler " + path;
        return false;
    } else {
        p_->exists = false;
        p_->doc.set_text(kVoicesHeader);  // so cabecalho; save() cria o arquivo
        p_->dirty = true;
    }
    p_->rebuild();
    return true;
}

bool VoicesFile::save(std::string* err) {
    if (p_->bad) { if (err) *err = "voices.toml nao foi lido (erro de leitura): gravacao bloqueada"; return false; }
    if (!p_->dirty && p_->exists) return true;
    if (!write_file_atomic(p_->path, p_->doc.text(), !p_->bak_done, err)) return false;
    p_->bak_done = true;
    p_->dirty = false;
    p_->exists = true;
    return true;
}

bool VoicesFile::dirty() const { return p_->dirty; }
const std::string& VoicesFile::path() const { return p_->path; }
std::string VoicesFile::serialize() const { return p_->doc.text(); }
const std::vector<VoiceEntry>& VoicesFile::voices() const { return p_->voices; }

const VoiceEntry* VoicesFile::find(const std::string& hash) const {
    for (auto& v : p_->voices) if (v.hash == hash) return &v;
    return nullptr;
}

bool VoicesFile::set_edit(const std::string& hash, const VoiceEdit& ne) {
    VoiceEntry* ent = nullptr;
    for (auto& v : p_->voices) if (v.hash == hash) { ent = &v; break; }
    if (!ent) return false;
    const VoiceEdit old = ent->edit;
    if (old == ne) return true;
    int si = p_->doc.find_sec("voices." + hash);
    if (si < 0) return false;
    Doc& d = p_->doc;
    bool ok = true;
    if (ne.name != old.name) ok &= d.set_kv(si, "name", toml_quote(ne.name));
    if (ne.mode != old.mode) ok &= d.set_kv(si, "mode", toml_quote(ne.mode));
    if (ne.program != old.program) ok &= d.set_kv(si, "program", std::to_string(ne.program));
    if (ne.bank != old.bank) ok &= d.set_kv(si, "bank", std::to_string(ne.bank));
    if (ne.transpose != old.transpose) ok &= d.set_kv(si, "transpose", std::to_string(ne.transpose));
    if (ne.volume_db != old.volume_db) ok &= d.set_kv(si, "volume_db", fmt_float1(ne.volume_db));
    if (ne.drum_note != old.drum_note) ok &= d.set_kv(si, "drum_note", std::to_string(ne.drum_note));
    p_->dirty = true;
    p_->rebuild();
    return ok;
}

bool VoicesFile::add_voice(const VoiceEntry& v) {
    if (find(v.hash)) return false;
    Doc& d = p_->doc;
    std::vector<std::string> blk = canonical_block(v);
    // posicao ordenada
    size_t pos = std::string::npos;
    for (size_t i = 1; i < d.secs.size(); ++i) {
        const Sec& s = d.secs[i];
        if (!starts_with(s.name, "voices.")) continue;
        std::string rest = s.name.substr(7);
        if (is_hash8(rest) && rest > v.hash) { pos = s.begin; break; }
    }
    if (pos != std::string::npos) {
        blk.push_back("");
        d.lines.insert(d.lines.begin() + (std::ptrdiff_t)pos, blk.begin(), blk.end());
    } else {
        if (d.lines.empty() || !d.lines.back().empty()) d.lines.push_back("");
        std::vector<std::string> ins;
        // se o ultimo bloco/preambulo nao termina em linha em branco, separa
        ins.push_back("");
        ins.insert(ins.end(), blk.begin(), blk.end());
        d.lines.insert(d.lines.end() - 1, ins.begin(), ins.end());
    }
    d.reparse();
    p_->dirty = true;
    p_->rebuild();
    return true;
}

// ---------------------------------------------------------------------------
// ProfileFile
// ---------------------------------------------------------------------------

struct ProfileFile::Impl {
    std::string path;
    Doc doc;
    ProfileTrack track;
    std::vector<ProfileChannel> channels;
    std::vector<VoiceOverride> overrides;
    bool dirty = false;
    bool bak_done = false;

    void rebuild() {
        channels.clear();
        overrides.clear();
        track = ProfileTrack();
        for (size_t i = 1; i < doc.secs.size(); ++i) {
            const Sec& s = doc.secs[i];
            if (s.name == "track") {
                track.name = to_str(kv_val(s, "name"), "");
                track.bgm_id = to_int(kv_val(s, "bgm_id"), -1);
                track.soundfont = to_str(kv_val(s, "soundfont"), track.soundfont);
                track.sample_rate = to_int(kv_val(s, "sample_rate"), 48000);
                track.loop_start = to_int(kv_val(s, "loop_start"), 0);
                track.loop_end = to_int(kv_val(s, "loop_end"), 0);
                track.master_gain_db = to_dbl(kv_val(s, "master_gain_db"), 0.0);
                track.target_rms_dbfs = to_dbl(kv_val(s, "target_rms_dbfs"), -16.5);
                track.chip_blend = to_dbl(kv_val(s, "chip_blend"), 1.0);
            } else if (starts_with(s.name, "channels.")) {
                ProfileChannel c;
                c.name = s.name.substr(9);
                c.role = to_str(kv_val(s, "role"), "auto");
                c.mode = to_str(kv_val(s, "mode"), "auto");
                c.program = to_int(kv_val(s, "program"), 0);
                c.bank = to_int(kv_val(s, "bank"), 0);
                c.volume_db = std::max(-60.0, std::min(24.0, to_dbl(kv_val(s, "volume_db"), 0.0)));
                c.pan = to_dbl(kv_val(s, "pan"), 0.0);
                c.mute = to_bool(kv_val(s, "mute"), false);
                c.solo = to_bool(kv_val(s, "solo"), false);
                c.eq_low_db = to_dbl(kv_val(s, "eq_low_db"), 0.0);
                c.eq_mid_db = to_dbl(kv_val(s, "eq_mid_db"), 0.0);
                c.eq_high_db = to_dbl(kv_val(s, "eq_high_db"), 0.0);
                c.transpose = std::max(-48, std::min(48, to_int(kv_val(s, "transpose"), 0)));
                channels.push_back(std::move(c));
            } else if (starts_with(s.name, "voices.") && is_hash8(s.name.substr(7))) {
                VoiceOverride o;
                o.hash = s.name.substr(7);
                o.edit = parse_edit(s);
                o.edit.mode = to_str(kv_val(s, "mode"), "auto");  // sem "mode" = sem override
                overrides.push_back(std::move(o));
            }
        }
    }
};

ProfileFile::ProfileFile() : p_(new Impl) {}
ProfileFile::~ProfileFile() { delete p_; }
ProfileFile::ProfileFile(ProfileFile&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
ProfileFile& ProfileFile::operator=(ProfileFile&& o) noexcept {
    if (this != &o) { delete p_; p_ = o.p_; o.p_ = nullptr; }
    return *this;
}

bool ProfileFile::load(const std::string& path, std::string* err) {
    p_->path = path;
    p_->bak_done = false;
    std::string text;
    if (!read_file(path, &text)) {
        if (err) *err = "perfil nao encontrado ou ilegivel: " + path;
        return false;
    }
    p_->doc.set_text(text);
    p_->dirty = false;
    p_->rebuild();
    return true;
}

bool ProfileFile::save(std::string* err) {
    if (!p_->dirty) return true;
    if (!write_file_atomic(p_->path, p_->doc.text(), !p_->bak_done, err)) return false;
    p_->bak_done = true;
    p_->dirty = false;
    return true;
}

bool ProfileFile::retarget(const std::string& new_path, const std::string& name, int bgm_id) {
    int si = p_->doc.find_sec("track");
    if (si < 0) return false;
    bool ok = p_->doc.set_kv(si, "name", toml_quote(name));
    ok &= p_->doc.set_kv(si, "bgm_id", std::to_string(bgm_id));
    p_->path = new_path;
    p_->bak_done = false;
    p_->dirty = true;
    p_->rebuild();
    return ok;
}

bool ProfileFile::dirty() const { return p_->dirty; }
const std::string& ProfileFile::path() const { return p_->path; }
std::string ProfileFile::serialize() const { return p_->doc.text(); }
const ProfileTrack& ProfileFile::track() const { return p_->track; }
const std::vector<ProfileChannel>& ProfileFile::channels() const { return p_->channels; }
const std::vector<VoiceOverride>& ProfileFile::overrides() const { return p_->overrides; }

const ProfileChannel* ProfileFile::find_channel(const std::string& name) const {
    for (auto& c : p_->channels) if (c.name == name) return &c;
    return nullptr;
}

const VoiceOverride* ProfileFile::find_override(const std::string& hash) const {
    for (auto& o : p_->overrides) if (o.hash == hash) return &o;
    return nullptr;
}

bool ProfileFile::set_channel(const ProfileChannel& n) {
    const ProfileChannel* oldp = find_channel(n.name);
    if (!oldp) return false;
    const ProfileChannel o = *oldp;
    if (o == n) return true;
    Doc& d = p_->doc;
    int si = d.find_sec("channels." + n.name);
    if (si < 0) return false;
    bool ok = true;
    if (n.role != o.role) ok &= d.set_kv(si, "role", quote_basic(n.role));
    if (n.mode != o.mode) ok &= d.set_kv(si, "mode", quote_basic(n.mode));
    if (n.program != o.program) ok &= d.set_kv(si, "program", std::to_string(n.program));
    if (n.bank != o.bank) ok &= d.set_kv(si, "bank", std::to_string(n.bank));
    if (n.volume_db != o.volume_db) ok &= d.set_kv(si, "volume_db", fmt_float_profile(n.volume_db));
    if (n.pan != o.pan) ok &= d.set_kv(si, "pan", fmt_float_profile(n.pan));
    if (n.mute != o.mute) ok &= d.set_kv(si, "mute", n.mute ? "true" : "false");
    if (n.solo != o.solo) ok &= d.set_kv(si, "solo", n.solo ? "true" : "false");
    if (n.eq_low_db != o.eq_low_db) ok &= d.set_kv(si, "eq_low_db", fmt_float_profile(n.eq_low_db));
    if (n.eq_mid_db != o.eq_mid_db) ok &= d.set_kv(si, "eq_mid_db", fmt_float_profile(n.eq_mid_db));
    if (n.eq_high_db != o.eq_high_db) ok &= d.set_kv(si, "eq_high_db", fmt_float_profile(n.eq_high_db));
    if (n.transpose != o.transpose) ok &= d.set_kv(si, "transpose", std::to_string(n.transpose));
    p_->dirty = true;
    p_->rebuild();
    return ok;
}

bool ProfileFile::set_override(const std::string& hash, const VoiceEdit& e) {
    if (!is_hash8(hash)) return false;
    Doc& d = p_->doc;
    int si = d.find_sec("voices." + hash);
    if (si < 0) {
        if (d.lines.empty() || !d.lines.back().empty()) d.lines.push_back("");
        std::vector<std::string> ins;
        ins.push_back("");
        ins.push_back("[voices." + hash + "]");
        ins.push_back("mode = " + quote_basic(e.mode));
        ins.push_back("program = " + std::to_string(e.program));
        ins.push_back("bank = " + std::to_string(e.bank));
        ins.push_back("transpose = " + std::to_string(e.transpose));
        ins.push_back("volume_db = " + fmt_float_profile(e.volume_db));
        ins.push_back("drum_note = " + std::to_string(e.drum_note));
        d.lines.insert(d.lines.end() - 1, ins.begin(), ins.end());
        d.reparse();
    } else {
        const VoiceOverride* old = find_override(hash);
        VoiceEdit o = old ? old->edit : VoiceEdit();
        bool ok = true;
        if (e.mode != o.mode) ok &= d.set_kv(si, "mode", quote_basic(e.mode));
        if (e.program != o.program) ok &= d.set_kv(si, "program", std::to_string(e.program));
        if (e.bank != o.bank) ok &= d.set_kv(si, "bank", std::to_string(e.bank));
        if (e.transpose != o.transpose) ok &= d.set_kv(si, "transpose", std::to_string(e.transpose));
        if (e.volume_db != o.volume_db) ok &= d.set_kv(si, "volume_db", fmt_float_profile(e.volume_db));
        if (e.drum_note != o.drum_note) ok &= d.set_kv(si, "drum_note", std::to_string(e.drum_note));
        if (!ok) return false;
    }
    p_->dirty = true;
    p_->rebuild();
    return true;
}

bool ProfileFile::remove_override(const std::string& hash) {
    Doc& d = p_->doc;
    int si = d.find_sec("voices." + hash);
    if (si < 0) return false;
    size_t b = d.secs[(size_t)si].begin, e = d.secs[(size_t)si].end;
    if (e == d.lines.size() && !d.lines.empty() && d.lines.back().empty()) {
        // ultima secao: preserva o marcador de fim de arquivo e remove a linha em branco anterior
        size_t eb = (b > 0 && d.lines[b - 1].empty()) ? b - 1 : b;
        d.lines.erase(d.lines.begin() + (std::ptrdiff_t)eb, d.lines.begin() + (std::ptrdiff_t)(e - 1));
    } else {
        d.lines.erase(d.lines.begin() + (std::ptrdiff_t)b, d.lines.begin() + (std::ptrdiff_t)e);
    }
    d.reparse();
    p_->dirty = true;
    p_->rebuild();
    return true;
}

}  // namespace ecr
