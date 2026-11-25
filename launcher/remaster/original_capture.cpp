// original_capture.cpp - ver original_capture.h
#include "original_capture.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "proc.h"
#include "voices_toml.h"

namespace fs = std::filesystem;

namespace ecr {

namespace {

const double kNtscFps = 59.92275;

struct WavInfo {
    int rate = 0, channels = 0, bits = 0;
    long data_off = 0;
    long long data_bytes = 0;
};

bool read_wav_header(std::FILE* f, WavInfo* w) {
    unsigned char h[12];
    if (std::fread(h, 1, 12, f) != 12 || std::memcmp(h, "RIFF", 4) != 0 || std::memcmp(h + 8, "WAVE", 4) != 0)
        return false;
    std::fseek(f, 0, SEEK_END);
    long long fsz = std::ftell(f);
    std::fseek(f, 12, SEEK_SET);
    bool have_fmt = false;
    for (;;) {
        unsigned char ch[8];
        if (std::fread(ch, 1, 8, f) != 8) return false;
        unsigned long long sz = (unsigned)ch[4] | ((unsigned)ch[5] << 8) | ((unsigned)ch[6] << 16) | ((unsigned)ch[7] << 24);
        long pos = std::ftell(f);
        if (std::memcmp(ch, "fmt ", 4) == 0) {
            unsigned char b[16];
            if (sz < 16 || std::fread(b, 1, 16, f) != 16) return false;
            int fmt = b[0] | (b[1] << 8);
            w->channels = b[2] | (b[3] << 8);
            w->rate = (int)((unsigned)b[4] | ((unsigned)b[5] << 8) | ((unsigned)b[6] << 16) | ((unsigned)b[7] << 24));
            w->bits = b[14] | (b[15] << 8);
            if (fmt != 1 && fmt != 0xFFFE) return false;
            have_fmt = true;
        } else if (std::memcmp(ch, "data", 4) == 0) {
            if (!have_fmt) return false;
            w->data_off = pos;
            long long avail = fsz - pos;
            w->data_bytes = (sz == 0 || (long long)sz > avail) ? avail : (long long)sz;  // cabecalho nao finalizado
            return true;
        }
        std::fseek(f, pos + (long)((sz + 1) & ~1ull), SEEK_SET);
    }
}

}  // namespace

std::vector<float> resample_stereo_to(const std::vector<int16_t>& pcm, int sr, int target) {
    std::vector<float> out;
    const size_t len = pcm.size() / 2;
    if (len == 0) return out;
    if (sr == target) {
        out.resize(pcm.size());
        for (size_t i = 0; i < pcm.size(); ++i) out[i] = (float)pcm[i] / 32768.0f;
        return out;
    }
    const int win = std::max(1, (int)std::nearbyint((double)sr / target));
    const bool boxed = len > (size_t)win;
    const size_t m = boxed ? len + 1 - (size_t)win : len;  // tamanho de `smooth`
    std::vector<double> smooth(m * 2);
    if (boxed) {
        for (int c = 0; c < 2; ++c) {
            // soma acumulada com cs[0] = 0
            std::vector<double> cs(len + 1, 0.0);
            for (size_t i = 0; i < len; ++i) cs[i + 1] = cs[i] + (double)pcm[2 * i + (size_t)c];
            for (size_t i = 0; i < m; ++i) smooth[2 * i + (size_t)c] = (cs[i + (size_t)win] - cs[i]) / win;
        }
    } else {
        for (size_t i = 0; i < len * 2; ++i) smooth[i] = (double)pcm[i];
    }
    const size_t n_out = (size_t)((double)len * (double)target / (double)sr);
    out.resize(n_out * 2);
    const double step = (double)sr / (double)target;
    for (size_t k = 0; k < n_out; ++k) {
        double x = (double)k * step;
        for (int c = 0; c < 2; ++c) {
            double v;
            if (x <= 0.0) v = smooth[(size_t)c];
            else if (x >= (double)(m - 1)) v = smooth[2 * (m - 1) + (size_t)c];
            else {
                size_t i0 = (size_t)x;
                double fr = x - (double)i0;
                v = smooth[2 * i0 + (size_t)c] * (1.0 - fr) + smooth[2 * (i0 + 1) + (size_t)c] * fr;
            }
            v = std::max(-32768.0, std::min(32767.0, std::nearbyint(v)));
            out[2 * k + (size_t)c] = (float)(v / 32768.0);
        }
    }
    return out;
}

bool capture_original(const GameRun& run, int bgm_id, OriginalAudio* out, ProgressFn progress, CancelFn cancel,
                      std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    if (bgm_id < 0 || bgm_id > 16) return fail("bgm_id invalido");
    if (!fs::exists(fs::u8path(run.exe))) return fail("jogo nao encontrado: " + run.exe);
    if (!fs::exists(fs::u8path(run.rom))) return fail("ROM nao encontrada: " + run.rom);

    static std::atomic<int> counter{0};
    std::error_code ec;
    fs::path td = fs::temp_directory_path(ec) / ("ec_orig_" + std::to_string(
                                                     (int)current_pid()
                                                         ) + "_" + std::to_string(counter++));
    fs::create_directories(td, ec);
    struct Cleanup {
        fs::path p;
        ~Cleanup() { std::error_code e; fs::remove_all(p, e); }
    } cleanup{td};

    const std::string script = (td / "orig.input").u8string();
    const std::string wav = (td / "orig.wav").u8string();
    int f_start = 0, f_end = 0;
    if (bgm_id <= 15) {
        if (!build_single_music_script(script, bgm_id, run.frames_per_track, &f_start, &f_end))
            return fail("falha ao gerar o script de entradas");
    } else {
        build_char_select_script(script, (td / "orig.ram").u8string(), run.frames_per_track, &f_start, &f_end);
    }

    ProcessSpec ps;
    ps.exe = run.exe;
    ps.args = {"--max-frames", std::to_string(f_end + 300), "--turbo", "--no-launcher", "--audio-remaster", "off",
               "--wav", wav, "--input-script", script, run.rom};
    ps.cwd = run.work_dir;
    ps.env = {{"SDL_VIDEODRIVER", "dummy"}, {"SDL_AUDIODRIVER", "dummy"}, {"SDL_RENDER_DRIVER", "software"}};
    ps.log_path = (td / "game.log").u8string();
    auto t0 = std::chrono::steady_clock::now();
    if (progress) progress(-1.0f, "Capturando o audio original");
    int rc = run_process(ps, [&]() {
        if (progress) {
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            progress(-1.0f, "Capturando o audio original (" + std::to_string((int)el) + " s)");
        }
        return cancel && cancel();
    }, err);
    if (rc == -2) return fail("cancelado");
    if (rc != 0) return fail(err && !err->empty() ? *err : "o jogo terminou com erro (" + std::to_string(rc) + ")");

    std::FILE* f = fopen_u8(wav, "rb");
    if (!f) return fail("o jogo nao gerou o WAV");
    WavInfo wi;
    if (!read_wav_header(f, &wi) || wi.channels != 2 || wi.bits != 16 || wi.rate <= 0) {
        std::fclose(f);
        return fail("WAV inesperado");
    }
    const long long total_frames = wi.data_bytes / 4;
    long long first = (long long)((double)f_start / kNtscFps * wi.rate);
    long long count = (long long)((double)run.frames_per_track / kNtscFps * wi.rate);
    first = std::min(first, total_frames);
    count = std::min(count, total_frames - first);
    std::vector<int16_t> pcm((size_t)count * 2);
    std::fseek(f, wi.data_off + (long)(first * 4), SEEK_SET);
    size_t got = std::fread(pcm.data(), 4, (size_t)count, f);
    std::fclose(f);
    pcm.resize(got * 2);
    out->sample_rate = 48000;
    out->stereo = resample_stereo_to(pcm, wi.rate, 48000);
    if (progress) progress(1.0f, "pronto");
    return true;
}

}  // namespace ecr
