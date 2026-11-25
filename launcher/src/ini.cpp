#include "ini.h"
#include "app.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <windows.h>

namespace fs = std::filesystem;

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

void Ini::parse_line(Line& l, const std::string& cur) {
    l.kind = Line::Other;
    std::string t = trim(l.raw);
    if (t.empty() || t[0] == ';' || t[0] == '#') return;
    if (t[0] == '[') {
        size_t e = t.find(']');
        if (e == std::string::npos) return;
        l.kind = Line::Section;
        l.section = trim(t.substr(1, e - 1));
        return;
    }
    size_t eq = l.raw.find('=');
    if (eq == std::string::npos) return;
    l.kind = Line::Kv;
    l.section = cur;
    l.key = trim(l.raw.substr(0, eq));
    size_t b = eq + 1;
    while (b < l.raw.size() && (l.raw[b] == ' ' || l.raw[b] == '\t')) ++b;
    size_t e = l.raw.size();
    if (b < l.raw.size() && l.raw[b] == '"') {
        size_t q = l.raw.find('"', b + 1);
        if (q != std::string::npos) { l.value = l.raw.substr(b + 1, q - b - 1); l.vbeg = b; l.vend = q + 1; return; }
    }
    // comentario final: " ;" ou " #" (mesma regra do jogo)
    size_t c1 = l.raw.find(" ;", b), c2 = l.raw.find(" #", b);
    size_t c = std::min(c1, c2);
    if (c != std::string::npos) e = c;
    while (e > b && (l.raw[e - 1] == ' ' || l.raw[e - 1] == '\t' || l.raw[e - 1] == '\r')) --e;
    l.vbeg = b; l.vend = e;
    l.value = l.raw.substr(b, e - b);
}

bool Ini::load(const fs::path& p) {
    path_ = p;
    lines_.clear();
    crlf_ = false; final_eol_ = true; dirty_ = false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return true;
    std::stringstream ss; ss << f.rdbuf();
    std::string data = ss.str();
    if (data.empty()) return true;
    final_eol_ = data.back() == '\n';
    size_t i = 0;
    std::string cur;
    while (i < data.size()) {
        size_t nl = data.find('\n', i);
        std::string line = data.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        i = nl == std::string::npos ? data.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') { line.pop_back(); crlf_ = true; }
        Line l; l.raw = line;
        parse_line(l, cur);
        if (l.kind == Line::Section) cur = l.section;
        lines_.push_back(std::move(l));
    }
    return true;
}

std::string Ini::serialize() const {
    std::string out;
    const char* eol = crlf_ ? "\r\n" : "\n";
    for (size_t i = 0; i < lines_.size(); ++i) {
        out += lines_[i].raw;
        if (i + 1 < lines_.size() || final_eol_) out += eol;
    }
    return out;
}

bool Ini::save() {
    if (!dirty_) return true;
    fs::path tmp = path_; tmp += L".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) return false;
        std::string s = serialize();
        o.write(s.data(), (std::streamsize)s.size());
        if (!o) return false;
    }
    if (!MoveFileExW(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    dirty_ = false;
    return true;
}

int Ini::find(const std::string& section, const std::string& key) const {
    int found = -1;
    for (int i = 0; i < (int)lines_.size(); ++i)
        if (lines_[i].kind == Line::Kv && lines_[i].section == section && lines_[i].key == key) found = i;  // a ultima vence (como o runner)
    return found;
}

bool Ini::has(const std::string& s, const std::string& k) const { return find(s, k) >= 0; }

bool Ini::get(const std::string& s, const std::string& k, std::string& out) const {
    int i = find(s, k);
    if (i < 0) return false;
    out = lines_[i].value;
    return true;
}

bool Ini::set(const std::string& section, const std::string& key, const std::string& value) {
    int i = find(section, key);
    if (i >= 0) {
        Line& l = lines_[i];
        if (l.value == value) return false;
        bool quoted = l.vend > l.vbeg && l.raw[l.vbeg] == '"';
        std::string nv = quoted ? "\"" + value + "\"" : value;
        l.raw = l.raw.substr(0, l.vbeg) + nv + l.raw.substr(l.vend);
        l.vend = l.vbeg + nv.size();
        l.value = value;
        dirty_ = true;
        return true;
    }
    // insere: apos a ultima linha Kv da secao (ou logo apos o cabecalho); cria a secao se nao existir
    int sec = -1, last = -1;
    for (int k = 0; k < (int)lines_.size(); ++k) {
        if (lines_[k].kind == Line::Section && lines_[k].section == section && sec < 0) sec = k;
        if (lines_[k].kind == Line::Kv && lines_[k].section == section) last = k;
    }
    Line nl; nl.raw = key + " = " + value;
    parse_line(nl, section);
    if (sec < 0) {
        if (!lines_.empty() && !trim(lines_.back().raw).empty()) { Line b; lines_.push_back(b); }
        Line h; h.raw = "[" + section + "]"; h.kind = Line::Section; h.section = section;
        lines_.push_back(h);
        lines_.push_back(nl);
        final_eol_ = true;
    } else {
        int at = (last >= 0 ? last : sec) + 1;
        lines_.insert(lines_.begin() + at, nl);
    }
    dirty_ = true;
    return true;
}

bool Ini::remove(const std::string& section, const std::string& key) {
    bool any = false;
    for (int i = (int)lines_.size() - 1; i >= 0; --i)
        if (lines_[i].kind == Line::Kv && lines_[i].section == section && lines_[i].key == key) {
            lines_.erase(lines_.begin() + i); any = true;
        }
    if (any) dirty_ = true;
    return any;
}

// ---------------------------------------------------------------------------
static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

static int count_diff_lines(const std::string& a, const std::string& b) {
    auto split = [](const std::string& s) {
        std::vector<std::string> v; size_t i = 0;
        while (i <= s.size()) { size_t n = s.find('\n', i); if (n == std::string::npos) { v.push_back(s.substr(i)); break; } v.push_back(s.substr(i, n - i)); i = n + 1; }
        return v;
    };
    auto va = split(a), vb = split(b);
    if (va.size() != vb.size()) return -1;
    int d = 0;
    for (size_t i = 0; i < va.size(); ++i) d += va[i] != vb[i];
    return d;
}

int ini_selftest(const std::string& utf8_path) {
    fs::path src = utf8_to_path(utf8_path);
    std::string report;
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { report += std::string(ok ? "PASS " : "FAIL ") + what + "\n"; if (!ok) ++fails; };

    std::string orig = slurp(src);
    fs::path work = src; work += L".selftest";
    { std::ofstream o(work, std::ios::binary | std::ios::trunc); o << orig; }

    // 1) carregar -> salvar sem mudancas = byte-identico (mesmo forcando a gravacao)
    Ini a; a.load(work);
    check(a.serialize() == orig, "serialize() sem mudancas == arquivo original (" + std::to_string(orig.size()) + " bytes)");
    check(a.save() && slurp(work) == orig, "save() sem mudancas preserva o arquivo byte a byte");

    // 2) set com o mesmo valor nao altera nada
    std::string first_sec, first_key, first_val;
    {
        Ini probe; probe.load(work);
        std::istringstream is(orig); std::string l, sec;
        while (std::getline(is, l)) {
            std::string t = trim(l);
            if (!t.empty() && t[0] == '[') { sec = trim(t.substr(1, t.find(']') - 1)); continue; }
            size_t eq = t.find('=');
            if (!t.empty() && t[0] != '#' && t[0] != ';' && eq != std::string::npos && !sec.empty()) {
                first_sec = sec; first_key = trim(t.substr(0, eq));
                probe.get(sec, first_key, first_val);
                break;
            }
        }
    }
    if (!first_key.empty()) {
        Ini b; b.load(work);
        check(!b.set(first_sec, first_key, first_val) && !b.dirty(), "set(" + first_sec + "." + first_key + ") com mesmo valor = no-op");
        b.set(first_sec, first_key, first_val + "9");
        check(b.save(), "save() apos mudar 1 valor");
        std::string after = slurp(work);
        int d = count_diff_lines(orig, after);
        check(d == 1, "mudar 1 valor altera exatamente 1 linha (diff=" + std::to_string(d) + ")");
        Ini c; c.load(work);
        std::string back; c.get(first_sec, first_key, back);
        check(back == first_val + "9", "valor relido == valor gravado");
        c.set(first_sec, first_key, first_val);
        c.save();
        check(slurp(work) == orig, "restaurar o valor devolve o arquivo byte-identico");
    } else report += "SKIP nenhuma chave encontrada para o teste de edicao\n";

    // 3) inserir chave nova em secao existente/nova toca so linhas novas; remover desfaz
    {
        Ini d; d.load(work);
        d.set("ec.bplus", "selftest_key", "1");
        d.set("selftest.novasecao", "k", "v");
        d.save();
        std::string after = slurp(work);
        check(after.find("selftest_key = 1") != std::string::npos && after.find("[selftest.novasecao]") != std::string::npos, "chaves/secoes novas inseridas");
        // as linhas originais continuam, na ordem, como subsequencia
        std::istringstream a1(orig), a2(after); std::string x, y; bool ok = true;
        while (std::getline(a1, x)) { bool f = false; while (std::getline(a2, y)) if (y == x) { f = true; break; } if (!f) { ok = false; break; } }
        check(ok, "linhas originais preservadas na mesma ordem");
        Ini e; e.load(work);
        e.remove("ec.bplus", "selftest_key");
        e.remove("selftest.novasecao", "k");
        e.save();
        std::string final = slurp(work);
        // sobra apenas o cabecalho/linha em branco da secao de teste; removemos para comparar
        Ini g; g.load(work);
        check(!g.has("selftest.novasecao", "k") && !g.has("ec.bplus", "selftest_key"), "remove() apaga as chaves de teste");
    }
    std::error_code ec;
    fs::remove(work, ec);
    report += fails ? "RESULT FAIL\n" : "RESULT OK\n";
    fputs(report.c_str(), stdout);
    fs::path rp = src; rp += L".selftest.txt";
    std::ofstream(rp, std::ios::binary) << report;
    return fails ? 1 : 0;
}
