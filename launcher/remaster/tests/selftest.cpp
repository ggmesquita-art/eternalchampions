// remaster_core_selftest - testes do ec_remaster_core (PASS/FAIL por teste).
//
//   remaster_core_selftest --root <repo> --dumps <dir com bgm_NN.appdump.txt> --ref <dir de referencias Python>
//                          [--tmp <dir>] [--run-game] [--only <substring>]
//
// As referencias Python sao geradas por tests/ref_toml.py, ref_catalog.py e ref_preview.py.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "catalog.h"
#include "dumps.h"
#include "original_capture.h"
#include "preview_synth.h"
#include "voices_toml.h"

namespace fs = std::filesystem;
using namespace ecr;

// ---------------------------------------------------------------------------
// mini JSON (so para os arquivos de referencia)
// ---------------------------------------------------------------------------
struct JV {
    enum K { Null, Num, Str, Bool, Arr, Obj } k = Null;
    double n = 0;
    bool b = false;
    std::string s;
    std::vector<JV> a;
    std::vector<std::pair<std::string, JV>> o;
    const JV* get(const std::string& key) const {
        for (auto& kv : o) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    double num(double d = 0) const { return k == Num ? n : (k == Bool ? (b ? 1 : 0) : d); }
};

struct JP {
    const std::string& s;
    size_t i = 0;
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    bool parse(JV& v) {
        ws();
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '{') {
            ++i;
            v.k = JV::Obj;
            ws();
            if (s[i] == '}') { ++i; return true; }
            for (;;) {
                ws();
                JV key;
                if (!parse(key) || key.k != JV::Str) return false;
                ws();
                if (s[i++] != ':') return false;
                JV val;
                if (!parse(val)) return false;
                v.o.emplace_back(key.s, std::move(val));
                ws();
                if (s[i] == ',') { ++i; continue; }
                if (s[i] == '}') { ++i; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++i;
            v.k = JV::Arr;
            ws();
            if (s[i] == ']') { ++i; return true; }
            for (;;) {
                JV e;
                if (!parse(e)) return false;
                v.a.push_back(std::move(e));
                ws();
                if (s[i] == ',') { ++i; continue; }
                if (s[i] == ']') { ++i; return true; }
                return false;
            }
        }
        if (c == '"') {
            ++i;
            v.k = JV::Str;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\') ++i;  // so ASCII nas referencias
                v.s += s[i++];
            }
            ++i;
            return true;
        }
        if (s.compare(i, 4, "true") == 0) { v.k = JV::Bool; v.b = true; i += 4; return true; }
        if (s.compare(i, 5, "false") == 0) { v.k = JV::Bool; v.b = false; i += 5; return true; }
        if (s.compare(i, 4, "null") == 0) { i += 4; return true; }
        char* end = nullptr;
        v.k = JV::Num;
        v.n = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) return false;
        i = (size_t)(end - s.c_str());
        return true;
    }
};

static bool load_json(const std::string& path, JV* out) {
    std::string txt;
    if (!read_file(path, &txt)) return false;
    JP p{txt};
    return p.parse(*out);
}

// ---------------------------------------------------------------------------
// harness
// ---------------------------------------------------------------------------
struct Ctx {
    std::string root, dumps, ref, tmp, only;
    bool run_game = false;
    int pass = 0, fail = 0, skip = 0;
};
static Ctx g;

static bool selected(const char* name) { return g.only.empty() || std::string(name).find(g.only) != std::string::npos; }

static void result(const char* name, bool ok, const std::string& detail = "") {
    std::printf("[%s] %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : "  -- ", detail.c_str());
    std::fflush(stdout);
    (ok ? g.pass : g.fail)++;
}
static void skipped(const char* name, const std::string& why) {
    std::printf("[SKIP] %s  -- %s\n", name, why.c_str());
    g.skip++;
}

static std::string P(const std::string& a, const std::string& b) { return (fs::u8path(a) / fs::u8path(b)).u8string(); }

static std::string slurp(const std::string& p) {
    std::string s;
    read_file(p, &s);
    return s;
}

static std::string first_diff(const std::string& a, const std::string& b) {
    size_t n = std::min(a.size(), b.size()), i = 0;
    while (i < n && a[i] == b[i]) ++i;
    size_t ls = 1;
    for (size_t k = 0; k < i; ++k) if (a[k] == '\n') ++ls;
    return "tamanhos " + std::to_string(a.size()) + " vs " + std::to_string(b.size()) + ", 1a diferenca no byte " +
           std::to_string(i) + " (linha " + std::to_string(ls) + ")";
}

static fs::path fresh_tmp(const char* name) {
    fs::path p = fs::u8path(g.tmp) / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

// ---------------------------------------------------------------------------
// testes
// ---------------------------------------------------------------------------

static void test_hash() {
    if (!selected("hash")) return;
    std::string got;
    bool ok = hash_selftest(&got);
    result("hash_fnv1a_contrato (ce6df9ba)", ok, "obtido " + got);
}

static void test_toml_roundtrip() {
    if (!selected("toml")) return;
    std::string base = P(g.root, "assets/audio_remaster");
    {
        VoicesFile v;
        v.load(P(base, "voices.toml"));
        std::string orig = slurp(P(base, "voices.toml"));
        bool ok = v.serialize() == orig && v.voices().size() == 52 && !v.dirty();
        result("toml_voices_carregar_serializar_identico", ok,
               ok ? "52 vozes" : first_diff(v.serialize(), orig) + ", vozes=" + std::to_string(v.voices().size()));
        const VoiceEntry* e = v.find("0047b5c7");
        bool meta_ok = e && e->meta.valid && e->meta.alg == 5 && e->meta.fb == 7 && e->meta.ops.size() == 4 &&
                       e->meta.ops[0].tl == 21 && e->meta.songs.size() == 3 && e->meta.notes == 981 &&
                       e->edit.program == 62 && e->edit.volume_db == 3.0;
        result("toml_voices_meta_lida", meta_ok);
    }
    {
        int n = 0, bad = 0;
        for (auto& de : fs::directory_iterator(fs::u8path(P(base, "profiles")))) {
            if (de.path().extension() != ".toml") continue;
            ProfileFile pf;
            std::string path = de.path().u8string();
            if (!pf.load(path) || pf.serialize() != slurp(path)) ++bad;
            ++n;
        }
        result("toml_perfis_carregar_serializar_identico", bad == 0 && n >= 17, std::to_string(n) + " perfis");
    }
    {   // salvar sem mudanca nao escreve nada
        fs::path d = fresh_tmp("toml_noop");
        fs::copy_file(fs::u8path(P(base, "voices.toml")), d / "voices.toml");
        VoicesFile v;
        v.load((d / "voices.toml").u8string());
        bool s = v.save();
        bool nobak = !fs::exists(d / "voices.toml.bak");
        result("toml_salvar_sem_mudanca_nao_grava", s && nobak && slurp((d / "voices.toml").u8string()) == slurp(P(base, "voices.toml")));
    }
}

static void test_toml_edit() {
    if (!selected("toml")) return;
    std::string base = P(g.root, "assets/audio_remaster");
    {
        fs::path d = fresh_tmp("toml_edit");
        fs::copy_file(fs::u8path(P(base, "voices.toml")), d / "voices.toml");
        std::string path = (d / "voices.toml").u8string();
        VoicesFile v;
        v.load(path);
        VoiceEdit e1 = v.find("0047b5c7")->edit;
        e1.name = "Baixo \xC3\xA9 \"slap\" \xF0\x9F\x8E\xB8";
        e1.mode = "percussion";
        e1.program = 38;
        e1.bank = 128;
        e1.transpose = -3;
        e1.volume_db = -2.25;
        e1.drum_note = 42;
        VoiceEdit e2 = v.find("01a7cbf3")->edit;
        e2.mode = "chip";
        bool ok = v.set_edit("0047b5c7", e1) && v.set_edit("01a7cbf3", e2) && v.dirty() && v.save();
        std::string got = slurp(path), ref = slurp(P(g.ref, "voices_edit_ref.toml"));
        result("toml_voices_edicao_igual_ao_python (write_toml)", ok && got == ref, got == ref ? "" : first_diff(got, ref));
        result("toml_voices_backup_bak", fs::exists(d / "voices.toml.bak") &&
                                             slurp((d / "voices.toml.bak").u8string()) == slurp(P(base, "voices.toml")));
        // reler: valores editados chegam intactos (nome com acento/emoji/aspas)
        VoicesFile r;
        r.load(path);
        const VoiceEntry* re = r.find("0047b5c7");
        result("toml_voices_edicao_reler", re && re->edit.name == e1.name && re->edit.mode == "percussion" &&
                                               re->edit.transpose == -3 && re->edit.drum_note == 42 &&
                                               r.serialize() == got);
        // desfazer: voltar aos valores originais reproduz o arquivo original byte a byte
        VoiceEdit o1, o2;
        {
            VoicesFile orig;
            orig.load(P(base, "voices.toml"));
            o1 = orig.find("0047b5c7")->edit;
            o2 = orig.find("01a7cbf3")->edit;
        }
        r.set_edit("0047b5c7", o1);
        r.set_edit("01a7cbf3", o2);
        result("toml_voices_desfazer_identico", r.serialize() == slurp(P(base, "voices.toml")));
    }
    {
        fs::path d = fresh_tmp("toml_prof");
        std::string src = P(base, "profiles/01_vietnam_midknight.toml");
        std::string path = (d / "01_vietnam_midknight.toml").u8string();
        fs::copy_file(fs::u8path(src), fs::u8path(path));
        ProfileFile p;
        p.load(path);
        ProfileChannel fm2 = *p.find_channel("fm2");
        fm2.program = 48;
        fm2.volume_db = -3.5;
        fm2.mute = true;
        ProfileChannel dac = *p.find_channel("dac");
        dac.mode = "gm";
        dac.bank = 0;
        bool ok = p.set_channel(fm2) && p.set_channel(dac) && p.save();
        std::string got = slurp(path), ref = slurp(P(g.ref, "profile_edit_ref.toml"));
        result("toml_perfil_edicao_igual_ao_python (format_profile_toml)", ok && got == ref,
               got == ref ? "" : first_diff(got, ref));

        // overrides [voices.<hash>]
        ProfileFile q;
        q.load(src);
        VoiceEdit ov;
        ov.mode = "gm";
        ov.program = 38;
        ov.bank = 0;
        ov.transpose = 2;
        ov.volume_db = -1.5;
        bool added = q.set_override("5ec7df75", ov);
        std::string s1 = q.serialize();
        ProfileFile q2;
        q2.load(src);
        VoiceEdit ov2 = ov;
        bool c1 = q.find_override("5ec7df75") && q.find_override("5ec7df75")->edit.program == 38 &&
                  q.find_override("5ec7df75")->edit.transpose == 2;
        ov2.program = 40;
        q.set_override("5ec7df75", ov2);
        bool c2 = q.find_override("5ec7df75")->edit.program == 40;
        q.set_override("1e04081c", ov);
        bool removed = q.remove_override("5ec7df75") && q.remove_override("1e04081c");
        result("toml_perfil_override_criar_editar_remover", added && c1 && c2 && removed && s1.find("[voices.5ec7df75]") != std::string::npos);
        result("toml_perfil_remover_override_restaura_bytes", q.serialize() == slurp(src), q.serialize() == slurp(src) ? "" : first_diff(q.serialize(), slurp(src)));
    }
}

// --- catalogo ---------------------------------------------------------------
static std::vector<SongNotes> g_songs;
static bool load_all_songs(std::string* err) {
    if (!g_songs.empty()) return true;
    for (int i = 0; i < bgm_count(); ++i) {
        SongNotes s;
        std::string path = dump_path(g.dumps, bgm_table()[i].id);
        if (!parse_dump(path, bgm_table()[i].id, bgm_table()[i].slug, &s)) {
            *err = "dump ilegivel: " + path;
            g_songs.clear();
            return false;
        }
        g_songs.push_back(std::move(s));
    }
    return true;
}

static bool near(double a, double b) { return std::fabs(a - b) <= 1e-9 + 1e-12 * std::fabs(b); }

static void test_catalog_equiv() {
    if (!selected("catalog")) return;
    std::string err;
    auto t0 = std::chrono::steady_clock::now();
    if (!load_all_songs(&err)) { result("catalog_parse_17_dumps", false, err); return; }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    JV ref;
    if (!load_json(P(g.ref, "ref_catalog.json"), &ref)) { skipped("catalog_equivalencia_python", "ref_catalog.json ausente"); return; }
    int songs_ok = 0, voices_total = 0, notes_total = 0;
    std::string firstbad;
    for (const SongNotes& s : g_songs) {
        const JV* rs = ref.get(s.slug);
        if (!rs) { if (firstbad.empty()) firstbad = s.slug + ": ausente na referencia"; continue; }
        bool ok = true;
        auto bad = [&](const std::string& m) { if (ok && firstbad.empty()) firstbad = s.slug + ": " + m; ok = false; };
        const JV* rv = rs->get("voices");
        if (rv->o.size() != s.voices.size()) bad("n de vozes " + std::to_string(s.voices.size()) + " vs " + std::to_string(rv->o.size()));
        for (auto& kv : rv->o) {
            const VoiceTrack* v = s.find_voice(kv.first);
            if (!v) { bad("voz ausente " + kv.first); continue; }
            ++voices_total;
            if ((size_t)kv.second.get("n")->num() != v->notes.size()) { bad("n notas " + kv.first); continue; }
            notes_total += (int)v->notes.size();
            const JV* first = kv.second.get("first");
            for (size_t i = 0; i < first->a.size(); ++i) {
                const JV& r = first->a[i];
                const Note& n = v->notes[i];
                if (!near(n.start_s, r.a[0].num()) || !near(n.dur_s, r.a[1].num()) || n.note != (int)r.a[2].num() ||
                    n.vel != (int)r.a[3].num() || (int)n.perc != (int)r.a[4].num() || n.ch != (int)r.a[5].num() ||
                    n.tl != (int)r.a[6].num()) {
                    bad("nota " + std::to_string(i) + " da voz " + kv.first);
                    break;
                }
            }
        }
        const JV* rc = rs->get("channels");
        for (auto& kv : rc->o) {
            size_t rn = (size_t)kv.second.get("n")->num();
            const ChanTrack* c = s.find_channel(kv.first);
            if (rn == 0) { if (c) bad("canal " + kv.first + " nao deveria existir"); continue; }
            if (!c || c->notes.size() != rn) { bad("canal " + kv.first + " n=" + std::to_string(c ? c->notes.size() : 0) + " vs " + std::to_string(rn)); continue; }
            notes_total += (int)rn;
            const JV* first = kv.second.get("first");
            for (size_t i = 0; i < first->a.size(); ++i) {
                const JV& r = first->a[i];
                const Note& n = c->notes[i];
                if (!near(n.start_s, r.a[0].num()) || !near(n.end_s(), r.a[1].num()) || n.note != (int)r.a[2].num() ||
                    n.vel != (int)r.a[3].num()) {
                    bad("nota " + std::to_string(i) + " do canal " + kv.first);
                    break;
                }
            }
        }
        // apos o round-trip MIDI (o que o mixer_studio sintetiza)
        const JV* rq = rs->get("quant");
        for (auto& kv : rq->o) {
            const std::vector<Note>* q = nullptr;
            if (kv.first.rfind("v_", 0) == 0) { if (auto* v = s.find_voice(kv.first.substr(2))) q = &v->q; }
            else if (auto* c = s.find_channel(kv.first)) q = &c->q;
            if (!q || q->size() != (size_t)kv.second.get("n")->num()) { bad("quant n " + kv.first); continue; }
            const JV* first = kv.second.get("first");
            for (size_t i = 0; i < first->a.size(); ++i) {
                const JV& r = first->a[i];
                const Note& n = (*q)[i];
                if (!near(n.start_s, r.a[0].num()) || !near(n.end_s(), r.a[1].num()) || n.note != (int)r.a[2].num() ||
                    n.vel != (int)r.a[3].num()) {
                    bad("quant nota " + std::to_string(i) + " de " + kv.first);
                    break;
                }
            }
        }
        if (ok) ++songs_ok;
    }
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%d/17 musicas, %d vozes, %d notas comparadas, parse total %.0f ms%s%s", songs_ok,
                  voices_total, notes_total, ms, firstbad.empty() ? "" : " | ", firstbad.c_str());
    result("catalog_equivalencia_python (vozes, notas, PSG/ruido/DAC, round-trip MIDI)", songs_ok == 17, buf);
}

static void test_catalog_regen() {
    if (!selected("catalog")) return;
    std::string err;
    if (!load_all_songs(&err)) { result("catalog_regen_toml", false, err); return; }
    std::vector<VoiceEntry> entries = build_voice_entries(g_songs);
    {
        fs::path d = fresh_tmp("catalog_regen");
        VoicesFile v;
        v.load((d / "voices.toml").u8string());  // inexistente => cabecalho canonico
        for (auto& e : entries) v.add_voice(e);
        std::string got = v.serialize(), ref = slurp(P(g.ref, "voices_fresh.toml"));
        result("catalog_regeneracao_do_zero_igual_ao_python",
               got == ref && entries.size() == 52, got == ref ? std::to_string(entries.size()) + " vozes" : first_diff(got, ref));
        // insercao fora de ordem produz o mesmo arquivo
        VoicesFile w;
        w.load((d / "w.toml").u8string());
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) w.add_voice(*it);
        result("catalog_insercao_ordenada", w.serialize() == got, w.serialize() == got ? "" : first_diff(w.serialize(), got));
        v.save();
        VoicesFile r;
        r.load((d / "voices.toml").u8string());
        result("catalog_salvar_novo_arquivo_e_reler", r.voices().size() == 52 && r.serialize() == got);
    }
    {   // com as edicoes reais, meta regerada == arquivo versionado
        VoicesFile real;
        real.load(P(g.root, "assets/audio_remaster/voices.toml"));
        fs::path d = fresh_tmp("catalog_regen2");
        VoicesFile v;
        v.load((d / "voices.toml").u8string());
        for (auto e : entries) {
            if (const VoiceEntry* r = real.find(e.hash)) e.edit = r->edit;
            v.add_voice(e);
        }
        std::string got = v.serialize(), ref = real.serialize();
        result("catalog_regeneracao_com_edicoes_igual_ao_voices.toml_real", got == ref, got == ref ? "byte-identico" : first_diff(got, ref));
    }
}

// --- previa -----------------------------------------------------------------
static bool read_f32(const std::string& path, std::vector<float>* out) {
    std::string raw;
    if (!read_file(path, &raw)) return false;
    out->resize(raw.size() / 4);
    std::memcpy(out->data(), raw.data(), out->size() * 4);
    return true;
}

static ChannelMap map_from_json(const JV& j) {
    ChannelMap m;
    m.key = j.get("key")->s;
    m.is_voice = j.get("is_voice")->b;
    m.mode = j.get("mode")->s;
    m.bank = (int)j.get("bank")->num();
    m.program = (int)j.get("program")->num();
    m.transpose = (int)j.get("transpose")->num();
    m.drum_note = (int)j.get("drum_note")->num();
    m.volume_db = j.get("volume_db")->num();
    m.pan = j.get("pan")->num();
    m.mute = j.get("mute")->b;
    m.solo = j.get("solo")->b;
    return m;
}

struct Cmp {
    double rms_a_db = 0, rms_b_db = 0, corr = 0, max_abs = 0;
};
static Cmp compare_pcm(const std::vector<float>& a, const std::vector<float>& b) {
    size_t n = std::min(a.size(), b.size());
    double sa = 0, sb = 0, sab = 0, mx = 0;
    for (size_t i = 0; i < n; ++i) {
        sa += (double)a[i] * a[i];
        sb += (double)b[i] * b[i];
        sab += (double)a[i] * b[i];
        mx = std::max(mx, std::fabs((double)a[i] - b[i]));
    }
    Cmp c;
    c.rms_a_db = 10.0 * std::log10(std::max(sa / (double)n, 1e-24));
    c.rms_b_db = 10.0 * std::log10(std::max(sb / (double)n, 1e-24));
    c.corr = (sa > 0 && sb > 0) ? sab / std::sqrt(sa * sb) : (sa == sb ? 1.0 : 0.0);
    c.max_abs = mx;
    return c;
}

static void test_preview() {
    if (!selected("preview")) return;
    std::string err;
    if (!load_all_songs(&err)) { result("preview_carregar_dumps", false, err); return; }
    PreviewSynth ps;
    if (!ps.load_soundfont(P(g.root, "assets/soundfonts/TimGM6mb.sf2"), &err)) { result("preview_soundfont", false, err); return; }
    VoicesFile voices;
    voices.load(P(g.root, "assets/audio_remaster/voices.toml"));
    const char* slugs[2] = {"01_vietnam_midknight", "musica_luta"};
    const char* cases[3] = {"A", "B", "C"};
    for (const char* slug : slugs) {
        const SongNotes* song = nullptr;
        for (auto& s : g_songs) if (s.slug == slug) song = &s;
        ProfileFile prof;
        prof.load(P(g.root, std::string("assets/audio_remaster/profiles/") + slug + ".toml"));
        for (const char* cs : cases) {
            std::string base = P(g.ref, std::string("preview_") + slug + "_" + cs);
            JV j;
            std::vector<float> refpcm;
            std::string nm = std::string("preview_") + slug + "_" + cs;
            if (!load_json(base + ".json", &j) || !read_f32(base + ".f32", &refpcm)) { skipped(nm.c_str(), "referencia ausente"); continue; }
            std::vector<ChannelMap> maps;
            for (auto& m : j.get("maps")->a) maps.push_back(map_from_json(m));
            if (std::string(cs) == "A") {   // o construtor de mapeamentos == MixerApp._voice_profile
                std::vector<ChannelMap> mine = build_channel_maps(*song, voices, prof);
                int diffs = 0;
                for (const ChannelMap& m : mine) {
                    const ChannelMap* r = nullptr;
                    for (auto& x : maps) if (x.key == m.key) r = &x;
                    if (!r || r->mode != m.mode || r->bank != m.bank || r->program != m.program ||
                        r->transpose != m.transpose || r->drum_note != m.drum_note ||
                        std::fabs(r->volume_db - m.volume_db) > 1e-9 || std::fabs(r->pan - m.pan) > 1e-9 ||
                        r->mute != m.mute || r->solo != m.solo || r->is_voice != m.is_voice)
                        ++diffs;
                }
                result((nm + "_mapeamento_igual_ao_mixer_studio").c_str(), diffs == 0 && !mine.empty(),
                       std::to_string(mine.size()) + " canais, " + std::to_string(diffs) + " diferencas");
            }
            ps.set_song(*song, maps);
            std::vector<float> mine;
            auto t0 = std::chrono::steady_clock::now();
            ps.render_all(&mine);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            Cmp c = compare_pcm(mine, refpcm);
            double ddb = c.rms_a_db - c.rms_b_db;
            char buf[256];
            std::snprintf(buf, sizeof(buf), "RMS C++ %.2f dB / Python %.2f dB (delta %+.3f dB), correlacao %.6f, max|diff| %.5f, quadros %zu/%zu, render %.0f ms",
                          c.rms_a_db, c.rms_b_db, ddb, c.corr, c.max_abs, mine.size() / 2, refpcm.size() / 2, ms);
            bool ok = mine.size() == refpcm.size() && std::fabs(ddb) <= 0.5 && c.corr > 0.99;
            result(nm.c_str(), ok, buf);
            if (std::string(cs) == "A" && std::string(slug) == slugs[0]) {
                // streaming por blocos == render completo
                ps.seek_frames(0);
                std::vector<float> st(mine.size(), 0.0f);
                std::vector<float> blk(1024 * 2);
                size_t frames = mine.size() / 2, done = 0;
                auto s0 = std::chrono::steady_clock::now();
                while (done < frames) {
                    size_t n = std::min<size_t>(1024, frames - done);
                    ps.render_block(blk.data(), (int)n);
                    std::memcpy(&st[done * 2], blk.data(), n * 2 * sizeof(float));
                    done += n;
                }
                double sms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s0).count();
                Cmp sc = compare_pcm(st, mine);
                std::snprintf(buf, sizeof(buf), "delta RMS %+.3f dB, correlacao %.6f, tempo %.0f ms para %.1f s de audio (%.1fx tempo real)",
                              sc.rms_a_db - sc.rms_b_db, sc.corr, sms, (double)frames / kPreviewRate, ((double)frames / kPreviewRate * 1000.0) / sms);
                result("preview_streaming_blocos_1024_igual_ao_render_completo", std::fabs(sc.rms_a_db - sc.rms_b_db) < 0.1 && sc.corr > 0.999, buf);
                // mute instantaneo: silenciar todas as vozes FM reduz muito o nivel
                std::vector<ChannelMap> mm = ps.maps();
                for (auto& m : mm) if (m.is_voice) { m.mute = true; ps.update_map(m); }
                ps.seek_frames(0);
                std::vector<float> muted(frames * 2, 0.0f);
                done = 0;
                while (done < frames) {
                    size_t n = std::min<size_t>(1024, frames - done);
                    ps.render_block(blk.data(), (int)n);
                    std::memcpy(&muted[done * 2], blk.data(), n * 2 * sizeof(float));
                    done += n;
                }
                Cmp mc = compare_pcm(muted, mine);
                std::snprintf(buf, sizeof(buf), "RMS com vozes FM mudas %.2f dB (cheio %.2f dB)", mc.rms_a_db, mc.rms_b_db);
                result("preview_mute_instantaneo_vozes_FM", mc.rms_a_db < mc.rms_b_db - 3.0, buf);
                // fim e loop
                ps.seek_frames((int64_t)frames - 100);
                ps.set_loop(false);
                ps.render_block(blk.data(), 1024);
                bool fin = ps.finished();
                ps.seek_frames((int64_t)frames - 100);
                ps.set_loop(true);
                ps.render_block(blk.data(), 1024);
                result("preview_fim_e_loop", fin && !ps.finished() && ps.position_frames() > 0 && ps.position_frames() < 1024);
            }
        }
    }
}

// --- jogo headless (opcional) ----------------------------------------------
static void test_game() {
    if (!selected("game")) return;
    if (!g.run_game) { skipped("game_dumps_e_original", "use --run-game (leva alguns minutos)"); return; }
    GameRun run;
    run.exe = P(g.root, "build/EternalChampionsRecomp.exe");
    run.rom = P(g.root, "roms/eternal_champions_us.gen");
    run.work_dir = g.root;
    {
        fs::path cache = fresh_tmp("game_dumps");
        std::string stage;
        float last = -1;
        auto t0 = std::chrono::steady_clock::now();
        bool cancelled_ok = true;
        std::string err;
        bool ok = extract_dumps(run, (cache / "c").u8string(), [&](float f, const std::string& s) {
            if (s != stage || f - last > 0.1f) { std::printf("    progresso %3.0f%% %s\n", f * 100, s.c_str()); stage = s; last = f; std::fflush(stdout); }
        }, nullptr, &err);
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        (void)cancelled_ok;
        if (!ok) { result("game_extrair_17_dumps", false, err); }
        else {
            // compara com os dumps de referencia: mesmo catalogo de vozes e mesmas notas
            int same = 0, bytes_same = 0;
            std::string bad;
            for (int i = 0; i < bgm_count(); ++i) {
                SongNotes a, b;
                std::string pa = dump_path((cache / "c").u8string(), i), pb = dump_path(g.dumps, i);
                if (!parse_dump(pa, i, bgm_table()[i].slug, &a) || !parse_dump(pb, i, bgm_table()[i].slug, &b)) { bad = "parse " + std::to_string(i); continue; }
                bool eq = a.voices.size() == b.voices.size();
                for (size_t k = 0; eq && k < a.voices.size(); ++k)
                    eq = a.voices[k].hash == b.voices[k].hash && a.voices[k].notes.size() == b.voices[k].notes.size();
                if (eq) ++same; else if (bad.empty()) bad = "musica " + std::to_string(i) + " difere";
                if (slurp(pa) == slurp(pb)) ++bytes_same;
            }
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%.0f s; %d/17 com as mesmas vozes/notas da referencia, %d/17 byte-identicos %s", secs, same, bytes_same, bad.c_str());
            result("game_extrair_17_dumps", same == 17, buf);
        }
        // cancelamento
        std::string e2;
        auto t1 = std::chrono::steady_clock::now();
        bool c = extract_dumps(run, (cache / "cancel").u8string(), nullptr,
                               [&]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count() > 3.0; }, &e2);
        double cs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        result("game_extrair_cancelamento", !c && e2 == "cancelado" && cs < 15.0, "cancelou em " + std::to_string(cs) + " s");
    }
    {
        OriginalAudio oa;
        std::string err;
        auto t0 = std::chrono::steady_clock::now();
        bool ok = capture_original(run, 8, &oa, nullptr, nullptr, &err);
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (!ok) { result("game_original_capture_bgm8", false, err); return; }
        double ss = 0;
        for (float x : oa.stereo) ss += (double)x * x;
        double rms = 10.0 * std::log10(std::max(ss / (double)oa.stereo.size(), 1e-24));
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%.1f s de audio a %d Hz, RMS %.1f dBFS, %.0f s de captura", oa.duration_s(), oa.sample_rate, rms, secs);
        bool ok2 = oa.sample_rate == 48000 && std::fabs(oa.duration_s() - 2400.0 / 59.92275) < 0.5 && rms > -50.0;
        // referencia opcional gerada pelo mixer_studio (ref_original.py)
        std::vector<float> refpcm;
        std::string extra;
        if (read_f32(P(g.ref, "orig_08.f32"), &refpcm)) {
            Cmp c = compare_pcm(oa.stereo, refpcm);
            std::snprintf(buf + std::strlen(buf), sizeof(buf) - std::strlen(buf), " | vs Python: delta RMS %+.3f dB, correlacao %.6f, quadros %zu/%zu",
                          c.rms_a_db - c.rms_b_db, c.corr, oa.stereo.size() / 2, refpcm.size() / 2);
            ok2 = ok2 && c.corr > 0.999;
        }
        result("game_original_capture_bgm8", ok2, buf);
        std::error_code ec;
        bool clean = true;
        for (auto& de : fs::directory_iterator(fs::temp_directory_path(ec), ec)) {
            if (de.path().filename().string().rfind("ec_orig_", 0) == 0) clean = false;
        }
        result("game_original_temporario_apagado", clean);
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--root") g.root = next();
        else if (a == "--dumps") g.dumps = next();
        else if (a == "--ref") g.ref = next();
        else if (a == "--tmp") g.tmp = next();
        else if (a == "--only") g.only = next();
        else if (a == "--run-game") g.run_game = true;
        else { std::printf("argumento desconhecido: %s\n", a.c_str()); return 2; }
    }
    if (g.root.empty()) {
        fs::path p = fs::absolute(fs::u8path(argv[0])).parent_path();
        for (int i = 0; i < 6 && !p.empty(); ++i, p = p.parent_path())
            if (fs::exists(p / "assets" / "audio_remaster" / "voices.toml")) { g.root = p.u8string(); break; }
    }
    if (g.root.empty() || g.dumps.empty() || g.ref.empty()) {
        std::printf("uso: remaster_core_selftest --root <repo> --dumps <dir> --ref <dir> [--tmp <dir>] [--run-game] [--only <txt>]\n");
        return 2;
    }
    if (g.tmp.empty()) g.tmp = (fs::temp_directory_path() / "ec_remaster_selftest").u8string();
    std::error_code ec;
    fs::create_directories(fs::u8path(g.tmp), ec);

    std::printf("== remaster_core_selftest ==\nroot=%s\ndumps=%s\nref=%s\n", g.root.c_str(), g.dumps.c_str(), g.ref.c_str());
    test_hash();
    test_toml_roundtrip();
    test_toml_edit();
    test_catalog_equiv();
    test_catalog_regen();
    test_preview();
    test_game();
    std::printf("== %d PASS, %d FAIL, %d SKIP ==\n%s\n", g.pass, g.fail, g.skip, g.fail ? "FAIL" : "PASS");
    return g.fail ? 1 : 0;
}
