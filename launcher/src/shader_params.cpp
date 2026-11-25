#include "shader_params.h"
#include "app.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}
static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

static void scan(const fs::path& file, int depth, std::vector<ShaderParam>& out, std::set<std::string>& seen_files) {
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(file, ec);
    if (depth > 4 || !seen_files.insert(path_to_utf8(canon)).second) return;
    std::istringstream is(slurp(file));
    std::string line;
    while (std::getline(is, line)) {
        std::string t = trim(line);
        if (t.rfind("#include", 0) == 0) {
            size_t a = t.find('"'), b = t.rfind('"');
            if (a != std::string::npos && b > a) scan(file.parent_path() / utf8_to_path(t.substr(a + 1, b - a - 1)), depth + 1, out, seen_files);
        } else if (t.rfind("#pragma parameter", 0) == 0) {
            const char* s = t.c_str() + 17;
            while (*s == ' ' || *s == '\t') ++s;
            std::string name;
            while (*s && *s != ' ' && *s != '\t') name += *s++;
            while (*s == ' ' || *s == '\t') ++s;
            std::string desc;
            if (*s == '"') { ++s; while (*s && *s != '"') desc += *s++; if (*s == '"') ++s; }
            float v[4] = {0, 0, 0, 0};
            int n = sscanf(s, "%f %f %f %f", &v[0], &v[1], &v[2], &v[3]);
            if (name.empty() || n < 3) continue;
            bool dup = false;
            for (auto& p : out) dup |= p.name == name;
            if (dup) continue;
            ShaderParam p; p.name = name; p.desc = desc.empty() ? name : desc;
            p.def = v[0]; p.min = v[1]; p.max = v[2];
            p.step = (n >= 4 && v[3] > 0) ? v[3] : (v[2] - v[1]) / 20.0;
            out.push_back(p);
        }
    }
}

bool load_shader_params(const fs::path& root, const std::string& preset, std::vector<ShaderParam>& out) {
    out.clear();
    if (preset.empty() || preset == "off" || preset == "none" || preset == "0") return false;
    std::error_code ec;
    fs::path p = utf8_to_path(preset);
    if (!(p.has_extension() && fs::exists(p, ec))) {
        p = root / "assets" / "shaders" / "presets" / utf8_to_path(preset + (preset.size() > 6 && preset.substr(preset.size() - 6) == ".glslp" ? "" : ".glslp"));
        if (!fs::exists(p, ec)) return false;
    }
    std::istringstream is(slurp(p));
    std::string line;
    std::vector<fs::path> shaders;
    std::map<std::string, double> overrides;
    while (std::getline(is, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(t.substr(0, eq)), v = unquote(t.substr(eq + 1));
        if (k.rfind("shader", 0) == 0 && k.size() > 6 && isdigit((unsigned char)k[6])) {
            shaders.push_back(p.parent_path() / utf8_to_path(v));
        } else {
            char* e = nullptr; double d = strtod(v.c_str(), &e);
            if (e != v.c_str() && *e == 0) overrides[k] = d;
        }
    }
    std::set<std::string> seen;
    for (auto& s : shaders) scan(s, 0, out, seen);
    for (auto& prm : out) {
        auto it = overrides.find(prm.name);
        if (it != overrides.end()) prm.def = it->second;
    }
    return true;
}

std::map<std::string, double> parse_param_string(const std::string& s) {
    std::map<std::string, double> m;
    std::istringstream is(s);
    std::string tok;
    while (std::getline(is, tok, ',')) {
        size_t eq = tok.find('=');
        if (eq == std::string::npos) continue;
        char* e = nullptr; std::string v = trim(tok.substr(eq + 1));
        double d = strtod(v.c_str(), &e);
        if (e != v.c_str()) m[trim(tok.substr(0, eq))] = d;
    }
    return m;
}

std::string format_param_string(const std::map<std::string, double>& m) {
    std::string r;
    for (auto& kv : m) {
        char b[64]; snprintf(b, sizeof b, "%g", kv.second);
        if (!r.empty()) r += ",";
        r += kv.first + "=" + b;
    }
    return r;
}
