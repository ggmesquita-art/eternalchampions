// dumps.cpp - porte de tools/audio/extract_all_midis.py (sem Python).
#include "dumps.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>

#include "proc.h"
#include "voices_toml.h"


namespace fs = std::filesystem;

namespace ecr {

// ---------------------------------------------------------------------------
// script de entradas
// ---------------------------------------------------------------------------

namespace {

struct ScriptBuilder {
    std::vector<std::string> lines;
    int frame = 1;
    void wait(int n) {
        if (n > 0) {
            lines.push_back("WAIT " + std::to_string(n));
            frame += n;
        }
    }
    void tap(const char* btn, int hold = 10, int post = 15) {
        lines.push_back(std::string("HOLD ") + btn);
        wait(hold);
        lines.push_back("RELEASE");
        wait(post);
    }
    void dump_ram(const std::string& path) {
        std::string p = fs::absolute(fs::u8path(path)).u8string();
        std::replace(p.begin(), p.end(), '\\', '/');
        lines.push_back("DUMP_RAM " + p);
    }
    bool write(const std::string& path, bool with_exit = true) {
        if (with_exit) lines.push_back("EXIT 0");
        std::string text;
        for (auto& l : lines) text += l + "\n";
        return write_file_atomic(path, text, false, nullptr);
    }
};

void boot_to_main_menu(ScriptBuilder& sb) {
    sb.wait(800);
    sb.tap("START", 15, 85);
    sb.tap("START", 15, 325);
    sb.tap("START", 15, 145);  // Title -> Main Menu
}

void goto_music_test(ScriptBuilder& sb) {
    boot_to_main_menu(sb);
    for (int i = 0; i < 4; ++i) sb.tap("DOWN", 10, 20);  // Main Menu -> Options
    sb.tap("START", 15, 90);
    for (int i = 0; i < 3; ++i) sb.tap("DOWN", 10, 15);  // Options -> Music Test
    sb.tap("B", 10, 50);                                 // silencia musica residual
}

}  // namespace

std::vector<MusicWindow> build_music_test_script(const std::string& script_path, const std::string& ram_path,
                                                 int track_frames) {
    ScriptBuilder sb;
    goto_music_test(sb);
    std::vector<MusicWindow> w;
    for (int id = 0; id < 16; ++id) {
        if (id > 0) sb.tap("RIGHT", 8, 20);
        int f0 = sb.frame;
        sb.tap("A", 10, track_frames);
        int f1 = sb.frame;
        sb.tap("B", 10, 50);
        w.push_back({id, f0, f1});
    }
    sb.dump_ram(ram_path);
    sb.write(script_path);
    return w;
}

void build_char_select_script(const std::string& script_path, const std::string& ram_path, int track_frames,
                              int* f_start, int* f_end) {
    ScriptBuilder sb;
    boot_to_main_menu(sb);
    sb.tap("START", 15, 145);
    sb.tap("START", 15, 145);
    sb.tap("DOWN", 10, 15);
    sb.tap("START", 15, 120);
    *f_start = sb.frame;
    sb.wait(track_frames);
    *f_end = sb.frame;
    sb.dump_ram(ram_path);
    sb.write(script_path);
}

bool build_single_music_script(const std::string& script_path, int bgm_id, int track_frames, int* f_start,
                               int* f_end) {
    if (bgm_id < 0 || bgm_id > 15) return false;
    ScriptBuilder sb;
    goto_music_test(sb);
    for (int i = 0; i < bgm_id; ++i) sb.tap("RIGHT", 8, 20);
    *f_start = sb.frame;
    sb.tap("A", 10, track_frames);
    *f_end = sb.frame;
    return sb.write(script_path);
}

// ---------------------------------------------------------------------------
// processo headless
// ---------------------------------------------------------------------------

std::string dump_path(const std::string& cache_dir, int id) {
    char b[64];
    std::snprintf(b, sizeof(b), "bgm_%02d.appdump.txt", id);
    return (fs::u8path(cache_dir) / b).u8string();
}

bool dumps_complete(const std::string& cache_dir) {
    std::error_code ec;
    for (int i = 0; i < 17; ++i) {
        auto p = fs::u8path(dump_path(cache_dir, i));
        if (!fs::is_regular_file(p, ec) || fs::file_size(p, ec) == 0) return false;
    }
    return true;
}

long long last_frame_in_file(const std::string& path) {
    std::FILE* f = fopen_u8(path, "rb");
    if (!f) return -1;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    long back = std::min<long>(sz, 16384);
    std::fseek(f, sz - back, SEEK_SET);
    std::string buf((size_t)back, '\0');
    size_t n = std::fread(&buf[0], 1, (size_t)back, f);
    std::fclose(f);
    buf.resize(n);
    // procura "\nf=<digitos>" a partir do fim
    size_t pos = buf.size();
    while (pos > 0) {
        size_t p = buf.rfind("\nf=", pos - 1);
        if (p == std::string::npos) break;
        size_t q = p + 3;
        long long v = 0;
        size_t nd = 0;
        while (q < buf.size() && buf[q] >= '0' && buf[q] <= '9') { v = v * 10 + (buf[q] - '0'); ++q; ++nd; }
        if (nd > 0 && q < buf.size()) return v;  // linha completa o bastante
        if (p == 0) break;
        pos = p;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// extracao
// ---------------------------------------------------------------------------

namespace {

std::vector<std::pair<std::string, std::string>> headless_env(const std::string& dump_file) {
    std::vector<std::pair<std::string, std::string>> e = {
        {"SDL_VIDEODRIVER", "dummy"}, {"SDL_AUDIODRIVER", "dummy"}, {"SDL_RENDER_DRIVER", "software"}};
    if (!dump_file.empty()) e.emplace_back("GENESIS_AUDIO_APPLIEDDUMP", dump_file);
    return e;
}

// Fatia um dump continuo em arquivos por bgm_id (uma leitura sequencial).
bool split_dump(const std::string& dump, std::vector<MusicWindow> wins, const std::string& out_dir,
                std::string* err) {
    std::sort(wins.begin(), wins.end(), [](const MusicWindow& a, const MusicWindow& b) { return a.f_start < b.f_start; });
    std::vector<std::FILE*> out;
    bool ok = true;
    for (auto& w : wins) {
        std::FILE* f = fopen_u8(dump_path(out_dir, w.bgm_id), "wb");
        if (!f) { ok = false; if (err) *err = "nao foi possivel criar " + dump_path(out_dir, w.bgm_id); }
        out.push_back(f);
    }
    if (ok) {
        std::FILE* in = fopen_u8(dump, "rb");
        if (!in) {
            ok = false;
            if (err) *err = "dump ausente: " + dump;
        } else {
            std::vector<char> line(1 << 16);
            size_t idx = 0;
            std::string cur;
            auto handle = [&](const std::string& ln) {
                if (ln.size() < 3 || ln[0] != 'f' || ln[1] != '=') return true;
                size_t i = 2;
                long long fr = 0;
                size_t nd = 0;
                while (i < ln.size() && ln[i] >= '0' && ln[i] <= '9') { fr = fr * 10 + (ln[i] - '0'); ++i; ++nd; }
                if (!nd) return true;
                while (idx < wins.size() && fr >= wins[idx].f_end) ++idx;
                if (idx >= wins.size()) return false;
                if (wins[idx].f_start <= fr && fr < wins[idx].f_end) {
                    std::fwrite(ln.data(), 1, ln.size(), out[idx]);
                    std::fwrite("\r\n", 1, 2, out[idx]);  // como o Python no Windows (modo texto)
                }
                return true;
            };
            size_t n;
            bool more = true;
            while (more && (n = std::fread(line.data(), 1, line.size(), in)) > 0) {
                for (size_t i = 0; i < n && more; ++i) {
                    char c = line[i];
                    if (c == '\n') {
                        if (!cur.empty() && cur.back() == '\r') cur.pop_back();
                        more = handle(cur);
                        cur.clear();
                    } else {
                        cur += c;
                    }
                }
            }
            if (more && !cur.empty()) handle(cur);
            std::fclose(in);
        }
    }
    for (auto* f : out) if (f) std::fclose(f);
    return ok;
}

}  // namespace

bool extract_dumps(const GameRun& run, const std::string& cache_dir, ProgressFn progress, CancelFn cancel,
                   std::string* err) {
    std::error_code ec;
    fs::path cdir = fs::u8path(cache_dir);
    fs::create_directories(cdir, ec);
    fs::path work = cdir / "_work";
    fs::create_directories(work, ec);
    struct Cleanup {
        fs::path p;
        ~Cleanup() { std::error_code e; fs::remove_all(p, e); }
    } cleanup{work};

    auto report = [&](float f, const std::string& s) { if (progress) progress(f, s); };
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    if (!fs::exists(fs::u8path(run.exe))) return fail("jogo nao encontrado: " + run.exe);
    if (!fs::exists(fs::u8path(run.rom))) return fail("ROM nao encontrada: " + run.rom);

    const std::string mt_script = (work / "music_test_0_15.input").u8string();
    const std::string mt_ram = (work / "music_test_0_15.ram").u8string();
    const std::string mt_dump = (work / "music_test_0_15.appdump.txt").u8string();
    const std::string cs_script = (work / "char_select_16.input").u8string();
    const std::string cs_ram = (work / "char_select_16.ram").u8string();
    const std::string cs_dump = (work / "char_select_16.appdump.txt").u8string();

    std::vector<MusicWindow> wins = build_music_test_script(mt_script, mt_ram, run.frames_per_track);
    const int mt_bench = wins.back().f_end + 500;
    int cs_start = 0, cs_end = 0;
    build_char_select_script(cs_script, cs_ram, run.frames_per_track, &cs_start, &cs_end);
    const int cs_bench = cs_end + 500;
    const double total = (double)(mt_bench + cs_bench);

    auto run_stage = [&](const std::string& script, const std::string& dump, int bench, double base,
                         const char* label) -> bool {
        ProcessSpec ps;
        ps.exe = run.exe;
        ps.args = {"--benchmark", std::to_string(bench), "--turbo", "--no-launcher", "--input-script", script, run.rom};
        ps.cwd = run.work_dir;
        ps.env = headless_env(fs::absolute(fs::u8path(dump)).u8string());
        ps.log_path = (work / "game.log").u8string();
        int polls = 0;
        auto poll = [&]() {
            if (cancel && cancel()) return true;
            if ((++polls % 2) == 0) {
                long long f = last_frame_in_file(dump);
                if (f >= 0) report((float)((base + std::min<double>((double)f, bench)) / total), label);
            }
            return false;
        };
        int rc = run_process(ps, poll, err);
        if (rc == -2) { if (err) *err = "cancelado"; return false; }
        if (rc != 0) {
            if (err && err->empty()) *err = std::string("o jogo terminou com erro (") + std::to_string(rc) + ")";
            return false;
        }
        std::error_code e2;
        if (!fs::exists(fs::u8path(dump)) || fs::file_size(fs::u8path(dump), e2) == 0) {
            if (err) *err = "dump de audio vazio ou ausente";
            return false;
        }
        return true;
    };

    report(0.0f, "Music Test (musicas 0..15)");
    if (!run_stage(mt_script, mt_dump, mt_bench, 0.0, "Music Test (musicas 0..15)")) return false;
    {
        std::string ram;
        if (!read_file(mt_ram, &ram) || ram.size() < 0x9B4E) return fail("RAM do Music Test ausente");
        int id = (unsigned char)ram[0x9371];
        int cursor = ((unsigned char)ram[0x9B4C] << 8) | (unsigned char)ram[0x9B4D];
        if (id != 15 || cursor != 3)
            return fail("estado inesperado no Music Test: $9371=" + std::to_string(id) + " cursor=" +
                        std::to_string(cursor));
    }
    if (!split_dump(mt_dump, wins, cache_dir, err)) return false;
    fs::remove(fs::u8path(mt_dump), ec);

    report((float)(mt_bench / total), "Selecao de personagem (musica 16)");
    if (!run_stage(cs_script, cs_dump, cs_bench, (double)mt_bench, "Selecao de personagem (musica 16)")) return false;
    if (!split_dump(cs_dump, {{16, cs_start, cs_end}}, cache_dir, err)) return false;

    if (!dumps_complete(cache_dir)) return fail("extracao incompleta (dumps vazios)");
    report(1.0f, "pronto");
    return true;
}

}  // namespace ecr
