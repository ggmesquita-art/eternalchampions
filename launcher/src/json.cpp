#include "json.h"
#include "app.h"
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {
struct P {
    const std::string& t;
    size_t i = 0;
    explicit P(const std::string& s) : t(s) {}
    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i; }
    static void put_utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    }
    bool str(std::string& o) {
        if (i >= t.size() || t[i] != '"') return false;
        ++i;
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c != '\\') { o += c; continue; }
            if (i >= t.size()) return false;
            char e = t[i++];
            switch (e) {
            case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break;
            case 'b': o += '\b'; break; case 'f': o += '\f'; break;
            case 'u': {
                if (i + 4 > t.size()) return false;
                unsigned cp = (unsigned)strtoul(t.substr(i, 4).c_str(), nullptr, 16); i += 4;
                put_utf8(o, cp); break;
            }
            default: o += e;
            }
        }
        if (i >= t.size()) return false;
        ++i;
        return true;
    }
    bool val(JVal& v, int depth) {
        if (depth > 32) return false;
        ws();
        if (i >= t.size()) return false;
        char c = t[i];
        if (c == '{') {
            ++i; v.type = JVal::Obj; ws();
            if (i < t.size() && t[i] == '}') { ++i; return true; }
            while (true) {
                ws(); std::string k;
                if (!str(k)) return false;
                ws(); if (i >= t.size() || t[i] != ':') return false; ++i;
                if (!val(v.obj[k], depth + 1)) return false;
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == '}') { ++i; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++i; v.type = JVal::Arr; ws();
            if (i < t.size() && t[i] == ']') { ++i; return true; }
            while (true) {
                v.arr.emplace_back();
                if (!val(v.arr.back(), depth + 1)) return false;
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == ']') { ++i; return true; }
                return false;
            }
        }
        if (c == '"') { v.type = JVal::Str; return str(v.str); }
        if (t.compare(i, 4, "true") == 0) { v.type = JVal::Bool; v.b = true; i += 4; return true; }
        if (t.compare(i, 5, "false") == 0) { v.type = JVal::Bool; v.b = false; i += 5; return true; }
        if (t.compare(i, 4, "null") == 0) { v.type = JVal::Null; i += 4; return true; }
        char* e = nullptr;
        double d = strtod(t.c_str() + i, &e);
        if (e == t.c_str() + i) return false;
        v.type = JVal::Num; v.num = d; i = (size_t)(e - t.c_str());
        return true;
    }
};
}  // namespace

bool json_parse(const std::string& text, JVal& out) {
    P p(text);
    out = JVal{};
    return p.val(out, 0);
}

bool json_parse_file(const std::string& utf8_path, JVal& out) {
    std::ifstream f(utf8_to_path(utf8_path), std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    return json_parse(ss.str(), out);
}
