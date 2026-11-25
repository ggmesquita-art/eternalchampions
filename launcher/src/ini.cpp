#include "ini.h"
#include "app.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <windows.h>

namespace fs = std::filesystem;

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// valor que o runner/jogo leem de volta igual: sem aspas internas / quebras de linha
static std::string sanitize_value(std::string v) {
    for (char& c : v) if (c == '"') c = '\'';
    v.erase(std::remove(v.begin(), v.end(), '\r'), v.end());
    v.erase(std::remove(v.begin(), v.end(), '\n'), v.end());
    return v;
}

static bool needs_quote(const std::string& v) {
    if (v.empty()) return false;
    if (v.front() == ' ' || v.front() == '\t' || v.back() == ' ' || v.back() == '\t') return true;
    if (v.find(" ;") != std::string::npos || v.find(" #") != std::string::npos) return true;
    if (v.find('\t') != std::string::npos) return true;
    return false;
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

void Ini::parse_text(const std::string& in) {
    lines_.clear();
    crlf_ = false; final_eol_ = true; bom_ = false;
    std::string data = in;
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
        bom_ = true;
        data.erase(0, 3);
    }
    if (data.empty()) return;
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
}

bool Ini::read_disk(std::string& data) const {
    std::ifstream f(path_, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    data = ss.str();
    return true;
}

void Ini::remember_disk() {
    std::error_code ec;
    disk_exists_ = fs::is_regular_file(path_, ec);
    disk_size_ = disk_exists_ ? fs::file_size(path_, ec) : 0;
    disk_mtime_ = disk_exists_ ? (int64_t)fs::last_write_time(path_, ec).time_since_epoch().count() : 0;
    disk_known_ = true;
}

bool Ini::disk_changed() const {
    if (!disk_known_) return false;
    std::error_code ec;
    bool ex = fs::is_regular_file(path_, ec);
    if (ex != disk_exists_) return true;
    if (!ex) return false;
    return fs::file_size(path_, ec) != disk_size_ || (int64_t)fs::last_write_time(path_, ec).time_since_epoch().count() != disk_mtime_;
}

bool Ini::load(const fs::path& p) {
    path_ = p;
    dirty_ = false; batch_ = 0; error_.clear();
    touched_.clear();
    bak_done_ = false;
    std::string data;
    if (!read_disk(data)) data.clear();
    parse_text(data);
    remember_disk();
    return true;
}

std::string Ini::serialize() const {
    std::string out = bom_ ? "\xEF\xBB\xBF" : "";
    const char* eol = crlf_ ? "\r\n" : "\n";
    for (size_t i = 0; i < lines_.size(); ++i) {
        out += lines_[i].raw;
        if (i + 1 < lines_.size() || final_eol_) out += eol;
    }
    return out;
}

bool Ini::end_batch() {
    if (batch_ > 0) --batch_;
    return batch_ > 0 ? true : save();
}

bool Ini::save() {
    if (!dirty_) return true;
    if (batch_ > 0) return true;
    error_.clear();
    // o arquivo mudou no disco (ex.: o runner regravou)? recarrega e reaplica so as chaves que alteramos
    if (disk_changed()) {
        auto touched = touched_;
        std::string data;
        if (!read_disk(data)) data.clear();
        parse_text(data);
        for (auto& kv : touched) {
            if (kv.second.first) set(kv.first.first, kv.first.second, kv.second.second);
            else remove(kv.first.first, kv.first.second);
        }
    }
    std::error_code ec;
    if (!bak_done_) {   // estado ANTERIOR a sessao
        if (fs::is_regular_file(path_, ec)) {
            fs::path bak = path_; bak += L".bak";
            fs::copy_file(path_, bak, fs::copy_options::overwrite_existing, ec);
            if (ec) { error_ = "backup .bak: " + ec.message(); return false; }
        }
        bak_done_ = true;
    }
    fs::path tmp = path_; tmp += L".launcher.tmp";   // nome proprio: o runner usa settings.ini.tmp
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) { error_ = "nao foi possivel criar " + tmp.filename().string(); return false; }
        std::string s = serialize();
        o.write(s.data(), (std::streamsize)s.size());
        o.flush();
        if (!o) { error_ = "falha ao escrever " + tmp.filename().string(); fs::remove(tmp, ec); return false; }
    }
    if (!MoveFileExW(tmp.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error_ = "MoveFileEx falhou (" + std::to_string(GetLastError()) + ")";
        DeleteFileW(tmp.c_str());
        return false;
    }
    dirty_ = false;
    touched_.clear();
    remember_disk();
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

bool Ini::set(const std::string& section, const std::string& key, const std::string& value_in) {
    const std::string value = sanitize_value(value_in);
    int i = find(section, key);
    if (i >= 0) {
        Line& l = lines_[i];
        if (l.value == value) return false;
        bool quoted = (l.vend > l.vbeg && l.raw[l.vbeg] == '"') || needs_quote(value);
        std::string nv = quoted ? "\"" + value + "\"" : value;
        l.raw = l.raw.substr(0, l.vbeg) + nv + l.raw.substr(l.vend);
        l.vend = l.vbeg + nv.size();
        l.value = value;
        dirty_ = true;
        touched_[{section, key}] = {true, value};
        return true;
    }
    // insere: apos a ultima linha Kv da secao (ou logo apos o cabecalho); cria a secao se nao existir
    int sec = -1, last = -1;
    for (int k = 0; k < (int)lines_.size(); ++k) {
        if (lines_[k].kind == Line::Section && lines_[k].section == section && sec < 0) sec = k;
        if (lines_[k].kind == Line::Kv && lines_[k].section == section) last = k;
    }
    Line nl; nl.raw = key + " = " + (needs_quote(value) ? "\"" + value + "\"" : value);
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
    touched_[{section, key}] = {true, value};
    return true;
}

bool Ini::remove(const std::string& section, const std::string& key) {
    bool any = false;
    for (int i = (int)lines_.size() - 1; i >= 0; --i)
        if (lines_[i].kind == Line::Kv && lines_[i].section == section && lines_[i].key == key) {
            lines_.erase(lines_.begin() + i); any = true;
        }
    if (any) { dirty_ = true; touched_[{section, key}] = {false, ""}; }
    return any;
}

int Ini::remove_variants(const std::string& section, const std::string& key) {
    int n = 0;
    const std::string lk = lower(key);
    for (int i = (int)lines_.size() - 1; i >= 0; --i)
        if (lines_[i].kind == Line::Kv && lines_[i].section == section && lines_[i].key != key && lower(lines_[i].key) == lk) {
            touched_[{section, lines_[i].key}] = {false, ""};
            lines_.erase(lines_.begin() + i); ++n;
        }
    if (n) dirty_ = true;
    return n;
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

static void write_text(const fs::path& p, const std::string& s) {
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o << s;
}

int ini_selftest(const std::string& utf8_path) {
    fs::path src = utf8_to_path(utf8_path);
    std::string report;
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { report += std::string(ok ? "PASS " : "FAIL ") + what + "\n"; if (!ok) ++fails; };

    std::string orig = slurp(src);
    fs::path work = src; work += L".selftest";
    write_text(work, orig);
    std::error_code ec;
    fs::remove(work.string() + ".bak", ec);

    // 1) carregar -> salvar sem mudancas = byte-identico
    Ini a; a.load(work);
    check(a.serialize() == orig, "serialize() sem mudancas == arquivo original (" + std::to_string(orig.size()) + " bytes)");
    check(a.save() && slurp(work) == orig, "save() sem mudancas preserva o arquivo byte a byte");

    // 2) editar 1 valor
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
    if (!first_key.empty() && first_val.find_first_of(" ;#\"") == std::string::npos) {
        Ini b; b.load(work);
        check(!b.set(first_sec, first_key, first_val) && !b.dirty(), "set(" + first_sec + "." + first_key + ") com mesmo valor = no-op");
        b.set(first_sec, first_key, first_val + "9");
        check(b.save(), "save() apos mudar 1 valor");
        std::string after = slurp(work);
        int d = count_diff_lines(orig, after);
        check(d == 1, "mudar 1 valor altera exatamente 1 linha (diff=" + std::to_string(d) + ")");
        check(slurp(work.string() + ".bak") == orig, ".bak = estado anterior a sessao");
        b.set(first_sec, first_key, first_val + "8");
        b.save();
        check(slurp(work.string() + ".bak") == orig, ".bak NAO e sobrescrito no 2o save da sessao");
        check(!fs::exists(work.string() + ".launcher.tmp", ec) && !fs::exists(work.string() + ".tmp", ec), "tmp proprio removido (e settings.ini.tmp do runner intacto)");
        Ini c; c.load(work);
        std::string back; c.get(first_sec, first_key, back);
        check(back == first_val + "8", "valor relido == valor gravado");
        c.set(first_sec, first_key, first_val);
        c.save();
        check(slurp(work) == orig, "restaurar o valor devolve o arquivo byte-identico");
    } else report += "SKIP nenhuma chave simples encontrada para o teste de edicao\n";

    // 3) inserir/remover
    {
        write_text(work, orig);
        Ini d; d.load(work);
        d.set("ec.bplus", "selftest_key", "1");
        d.set("selftest.novasecao", "k", "v");
        d.save();
        std::string after = slurp(work);
        check(after.find("selftest_key = 1") != std::string::npos && after.find("[selftest.novasecao]") != std::string::npos, "chaves/secoes novas inseridas");
        std::istringstream a1(orig), a2(after); std::string x, y; bool ok = true;
        while (std::getline(a1, x)) { bool f = false; while (std::getline(a2, y)) if (y == x) { f = true; break; } if (!f) { ok = false; break; } }
        check(ok, "linhas originais preservadas na mesma ordem");
        d.remove("ec.bplus", "selftest_key");
        d.remove("selftest.novasecao", "k");
        d.save();
        Ini g; g.load(work);
        check(!g.has("selftest.novasecao", "k") && !g.has("ec.bplus", "selftest_key"), "remove() apaga as chaves de teste");
    }

    // 4) aspas: valores com " ;", " #", espacos nas pontas voltam iguais
    {
        write_text(work, "[s]\nk = old\n");
        Ini q; q.load(work);
        q.set("s", "k", "a ;b");
        q.set("s", "n1", " x ");
        q.set("s", "n2", "p #q");
        q.set("s", "n3", "com\"aspas");
        q.save();
        std::string t = slurp(work);
        Ini r; r.load(work);
        std::string v1, v2, v3, v4;
        r.get("s", "k", v1); r.get("s", "n1", v2); r.get("s", "n2", v3); r.get("s", "n3", v4);
        check(t.find("k = \"a ;b\"") != std::string::npos, "valor com ' ;' gravado entre aspas");
        check(v1 == "a ;b" && v2 == " x " && v3 == "p #q", "valores com ' ;', ' #' e espacos nas pontas relidos iguais");
        check(v4 == "com'aspas", "aspas internas sao trocadas por apostrofo (o leitor do jogo nao as representa)");
    }

    // 5) BOM preservado
    {
        write_text(work, "\xEF\xBB\xBF[s]\r\nk = 1\r\n");
        Ini bo; bo.load(work);
        std::string v;
        check(bo.has_bom() && bo.get("s", "k", v) && v == "1", "BOM UTF-8 removido ao ler (chave lida)");
        bo.set("s", "k", "2"); bo.save();
        std::string t = slurp(work);
        check(t.size() > 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB && (unsigned char)t[2] == 0xBF && t.find("k = 2\r\n") != std::string::npos, "BOM e CRLF preservados ao gravar");
    }

    // 6) mudanca concorrente no disco ("runner") entre load e save
    {
        write_text(work, "[video]\nwindow_scale = 2\n[ec.audio]\nremaster = off\n");
        Ini l; l.load(work);
        write_text(work, "[video]\nwindow_scale = 2\nfullscreen = 1\n[ec.audio]\nremaster = off\n[runner_only]\nx = 9\n");   // o "runner" regravou
        l.set("ec.audio", "remaster", "hybrid");
        bool ok = l.save();
        Ini r; r.load(work);
        std::string a1, a2, a3;
        r.get("video", "fullscreen", a1); r.get("runner_only", "x", a2); r.get("ec.audio", "remaster", a3);
        check(ok && a1 == "1" && a2 == "9" && a3 == "hybrid", "mudanca do runner entre load e save preservada + chave do launcher aplicada");
    }

    // 7) lote: varias alteracoes, um save
    {
        write_text(work, "[s]\na = 1\n");
        Ini bt; bt.load(work);
        bt.begin_batch();
        bt.set("s", "a", "2"); bt.save();
        bool mid = slurp(work) == "[s]\na = 1\n";
        bt.set("s", "b", "3");
        bool endok = bt.end_batch();
        check(mid && endok && slurp(work) == "[s]\na = 2\nb = 3\n", "lote: nada grava ate end_batch(), depois um unico save");
        // variantes de caixa
        write_text(work, "[s]\nautofire.x = 5\nautofire.X = 6\n");
        Ini vr; vr.load(work);
        int n = vr.remove_variants("s", "autofire.X");
        vr.save();
        check(n == 1 && slurp(work) == "[s]\nautofire.X = 6\n", "remove_variants remove so a variante de caixa diferente");
    }

    fs::remove(work, ec);
    fs::remove(work.string() + ".bak", ec);
    report += fails ? "RESULT FAIL\n" : "RESULT OK\n";
    fputs(report.c_str(), stdout);
    fs::path rp = src; rp += L".selftest.txt";
    write_text(rp, report);
    return fails ? 1 : 0;
}
