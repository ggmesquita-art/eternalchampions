// catalog.cpp - porte de voice_catalog.py + chip_ring_to_midi.parse_chip_ring (lado dumps APP).
#include "catalog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

namespace ecr {

// ---------------------------------------------------------------------------
// hash
// ---------------------------------------------------------------------------

uint32_t fnv1a32(const uint8_t* data, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; ++i) {
        h ^= data[i];
        h *= 0x01000193u;
    }
    return h;
}

std::string hash_to_string(uint32_t h) {
    char b[16];
    std::snprintf(b, sizeof(b), "%08x", h);
    return b;
}

static const int kSlotOff[4] = {0x0, 0x8, 0x4, 0xC};  // S1, S2, S3, S4

static bool is_carrier_s(int alg, int s) {
    switch (alg & 7) {
        case 0: case 1: case 2: case 3: return s == 3;
        case 4: return s == 1 || s == 3;
        case 5: case 6: return s >= 1;
        default: return true;
    }
}

// 31 bytes canonicos (regs[pr][reg] = shadow)
static std::vector<uint8_t> patch_bytes(const uint8_t regs[2][256], int pr, int ch) {
    const uint8_t* r = regs[pr];
    int b0 = r[0xB0 + ch];
    int alg = b0 & 7;
    std::vector<uint8_t> out;
    out.reserve(31);
    out.push_back((uint8_t)alg);
    out.push_back((uint8_t)((b0 >> 3) & 7));
    out.push_back((uint8_t)(r[0xB4 + ch] & 0x37));
    for (int s = 0; s < 4; ++s) {
        int o = kSlotOff[s] + ch;
        static const int bases[7] = {0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90};
        for (int base : bases) {
            int v = r[base + o];
            if (base == 0x40 && is_carrier_s(alg, s)) v = 0xFF;
            out.push_back((uint8_t)v);
        }
    }
    return out;
}

bool hash_selftest(std::string* got) {
    uint8_t regs[2][256];
    std::memset(regs, 0, sizeof(regs));
    regs[0][0xB0] = 7;
    std::vector<uint8_t> pb = patch_bytes(regs, 0, 0);
    std::string h = hash_to_string(fnv1a32(pb.data(), pb.size()));
    if (got) *got = h;
    return pb.size() == 31 && h == "ce6df9ba";
}

// ---------------------------------------------------------------------------
// tabela de musicas
// ---------------------------------------------------------------------------

static const BgmInfo kBgm[17] = {
    {0, "03_syria_blade", "Syria / Blade"},
    {1, "bgm_01_sanctuary", "Sanctuary"},
    {2, "bgm_02_stage_s25", "Stage S25"},
    {3, "bgm_03_stage_s23", "Stage S23"},
    {4, "02_salem_xavier", "Salem / Xavier"},
    {5, "bgm_05_stage_s22", "Stage S22"},
    {6, "bgm_06_stage_s26", "Stage S26"},
    {7, "musica_luta", "Luta"},
    {8, "01_vietnam_midknight", "Vietnam / Midknight"},
    {9, "bgm_09_bank1_09", "Banco 1 - 09"},
    {10, "bgm_10_stage_s27", "Stage S27"},
    {11, "bgm_11_stage_s24", "Stage S24"},
    {12, "musica_titulo", "Titulo"},
    {13, "bgm_13_bank2_01", "Banco 2 - 01"},
    {14, "bgm_14_bank2_02", "Banco 2 - 02"},
    {15, "bgm_15_bank2_03", "Banco 2 - 03"},
    {16, "musica_selecao", "Selecao de personagem"},
};

const BgmInfo* bgm_table() { return kBgm; }
int bgm_count() { return 17; }
const BgmInfo* bgm_by_slug(const std::string& slug) {
    for (auto& b : kBgm) if (slug == b.slug) return &b;
    return nullptr;
}
const BgmInfo* bgm_by_id(int id) {
    for (auto& b : kBgm) if (b.id == id) return &b;
    return nullptr;
}

const VoiceTrack* SongNotes::find_voice(const std::string& h) const {
    for (auto& v : voices) if (v.hash == h) return &v;
    return nullptr;
}
const ChanTrack* SongNotes::find_channel(const std::string& n) const {
    for (auto& c : channels) if (c.name == n) return &c;
    return nullptr;
}

// ---------------------------------------------------------------------------
// constantes do chip_ring_to_midi
// ---------------------------------------------------------------------------

namespace {

const double YM_CLK = 7670454.0;
const double PSG_CLK = 3579545.0;
const double NTSC_FPS = 59.92275;
const double FRAME_US = 1000000.0 / NTSC_FPS;
const double M68K_FRAME_CYCLES = 127841.0;
const double MASTER_FRAME_CYCLES = 895780.0;
const double DAC_VOICE_MAX_US = 500000.0;
const int PPQN = 480;
const double TEMPO_US = 500000.0;

double py_round(double x) { return std::nearbyint(x); }  // banker's, como Python round()

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// carriers por slot de registrador (OP1, OP3, OP2, OP4)
bool is_carrier_reg(int alg, int i) {
    switch (alg & 7) {
        case 0: case 1: case 2: case 3: return i == 3;
        case 4: return i == 2 || i == 3;
        case 5: case 6: return i >= 1;
        default: return true;
    }
}

double mul_to_factor(int mul) { return (mul & 0x0F) == 0 ? 0.5 : (double)(mul & 0x0F); }

int fnum_to_midi(int block, int fnum, double mulf) {
    if (fnum <= 0 || mulf <= 0.0) return 0;
    double f_base = fnum * (YM_CLK / 144.0) / std::ldexp(1.0, 21 - block);
    double freq = f_base * mulf;
    if (freq <= 0.0) return 0;
    double v = py_round(12.0 * std::log2(freq / 440.0) + 69.0);
    return (int)std::max(0.0, std::min(127.0, v));
}

int psg_period_to_midi(int period) {
    if (period <= 0) return 0;
    double freq = PSG_CLK / (32.0 * period);
    if (freq <= 0.0) return 0;
    double v = py_round(12.0 * std::log2(freq / 440.0) + 69.0);
    return (int)std::max(0.0, std::min(127.0, v));
}

struct Sounding {
    bool on = false;
    double t_on = 0.0;
    int note = 0, vel = 0;
    bool perc = false;
    uint32_t hash = 0;
    int tl = 0;
};

struct FmChan {
    int fnum = 0, block = 0, msb = 0, alg = 0;
    int tl[4] = {32, 32, 32, 32};
    int mul[4] = {1, 1, 1, 1};
    Sounding s;

    int active_mask(int& min_tl) const {
        min_tl = 1000;
        for (int i = 0; i < 4; ++i) if (is_carrier_reg(alg, i)) min_tl = std::min(min_tl, tl[i]);
        int mask = 0, last = 0;
        for (int i = 0; i < 4; ++i) {
            if (!is_carrier_reg(alg, i)) continue;
            last = i;
            if (tl[i] <= min_tl + 18 && tl[i] < 96) mask |= 1 << i;
        }
        if (!mask) mask = 1 << last;
        return mask;
    }
    double carrier_mul_factor() const {
        int mt;
        int m = active_mask(mt);
        double f = 1e9;
        for (int i = 0; i < 4; ++i) if (m & (1 << i)) f = std::min(f, mul_to_factor(mul[i]));
        return f;
    }
    bool is_noise_perc_patch() const {
        int a = alg & 7;
        if (a > 4) return false;
        int mt;
        int m = active_mask(mt);
        bool any = false;
        for (int i = 0; i < 4; ++i) if ((m & (1 << i)) && (mul[i] & 0x0F) >= 12) any = true;
        if (!any) return false;
        int intense = 0;
        for (int i = 0; i < 4; ++i) {
            if (is_carrier_reg(a, i)) continue;
            if ((mul[i] & 0x0F) >= 10 && tl[i] <= 15) ++intense;
        }
        return intense >= 2;
    }
    int current_midi_note() const { return fnum_to_midi(block, fnum, carrier_mul_factor()); }
    int eff_tl() const {
        int mt;
        active_mask(mt);
        return mt;
    }
    int carrier_velocity() const {
        double norm = std::max(0.0, std::min(1.0, 1.0 - (eff_tl() / 96.0)));
        return (int)std::max(16.0, std::min(127.0, py_round(20.0 + norm * 107.0)));
    }
};

struct PsgTone {
    int period = 0, vol = 15;
    bool on = false;
    double t_on = 0.0;
    int note = 0, vel = 0;
    int velocity() const {
        if (vol >= 15) return 0;
        return (int)std::max(16.0, std::min(127.0, py_round(18.0 + ((15 - vol) / 15.0) * 109.0)));
    }
};

struct DacBurst {
    double start_us, last_us;
    int count = 1;
    int peak_dev;
    int zcr = 0;
    int prev_sign;
    DacBurst(double t, int sample) : start_us(t), last_us(t) {
        peak_dev = std::abs(sample - 128);
        prev_sign = sample >= 128 ? 1 : -1;
    }
    void feed(double t, int sample) {
        last_us = t;
        ++count;
        int dev = std::abs(sample - 128);
        if (dev > peak_dev) peak_dev = dev;
        int sign = sample >= 128 ? 1 : -1;
        if (sign != prev_sign) { ++zcr; prev_sign = sign; }
    }
    bool is_voice_stream() const { return (last_us - start_us) > DAC_VOICE_MAX_US; }
    Note to_note(double t0_us) const {
        double dur_us = std::max(40000.0, std::min(350000.0, (last_us - start_us) + 20000.0));
        double zr = (double)zcr / (double)std::max(1, count);
        int dn;
        if (zr < 0.09) dn = 36;
        else if (zr < 0.22) dn = 38;
        else if (dur_us < 100000.0) dn = 42;
        else dn = 41;
        int vel = (int)std::max(36.0, std::min(127.0, 45.0 + (double)(int)(peak_dev * 0.65)));
        Note n;
        n.start_s = std::max(0.0, (start_us - t0_us) / 1000000.0);
        n.dur_s = dur_us / 1000000.0;
        n.note = dn;
        n.vel = vel;
        return n;
    }
};

int psg_noise_drum_note(int reg) {
    int fb = (reg >> 2) & 1, nf = reg & 3;
    if (fb == 0) return 39;
    if (nf == 0) return 42;
    if (nf == 1) return 46;
    return 38;
}

struct RawEv {
    int fr, cyc;
    bool fm;
    int port, val;
};

bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v'; }

// "f=N APP FM pP $VV cyc=C" / "f=N APP PSG $VV cyc=C" (ancorado no inicio da linha)
bool parse_app_line(const char* p, const char* e, RawEv& ev) {
    auto digits = [&](long long& v) {
        if (p >= e || *p < '0' || *p > '9') return false;
        v = 0;
        while (p < e && *p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
        return true;
    };
    auto ws1 = [&]() {
        if (p >= e || !is_ws(*p)) return false;
        while (p < e && is_ws(*p)) ++p;
        return true;
    };
    auto lit = [&](const char* s) {
        size_t n = std::strlen(s);
        if ((size_t)(e - p) < n || std::memcmp(p, s, n) != 0) return false;
        p += n;
        return true;
    };
    auto hex2 = [&](int& v) {
        if (e - p < 2) return false;
        v = 0;
        for (int k = 0; k < 2; ++k) {
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return false;
        }
        return true;
    };
    long long f, cyc, port = 0;
    int val;
    if (!lit("f=") || !digits(f) || !ws1() || !lit("APP") || !ws1()) return false;
    if (lit("FM")) {
        if (!ws1() || !lit("p") || !digits(port) || port > 9) return false;
        if (!ws1() || !lit("$") || !hex2(val) || !ws1() || !lit("cyc=") || !digits(cyc)) return false;
        ev = {(int)f, (int)cyc, true, (int)port, val};
        return true;
    }
    if (lit("PSG")) {
        if (!ws1() || !lit("$") || !hex2(val) || !ws1() || !lit("cyc=") || !digits(cyc)) return false;
        ev = {(int)f, (int)cyc, false, 0, val};
        return true;
    }
    return false;
}

// round-trip por MIDI (write_voice_midi -> read_midi_named_tracks) de UMA trilha
std::vector<Note> midi_roundtrip(const std::vector<Note>& notes) {
    struct E { long long tick; int order; int note, vel; };
    const double us_per_tick = TEMPO_US / PPQN;
    std::vector<E> ev;
    ev.reserve(notes.size() * 2);
    for (const Note& n : notes) {
        long long on = (long long)py_round((n.start_s * 1000000.0) / us_per_tick);
        long long off = std::max(on + 1, (long long)py_round((n.end_s() * 1000000.0) / us_per_tick));
        int note = clampi(n.note, 0, 127);
        int vel = clampi(n.vel, 1, 127);
        ev.push_back({on, 3, note, vel});
        ev.push_back({off, 2, note, 0});
    }
    std::stable_sort(ev.begin(), ev.end(), [](const E& a, const E& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });
    const double spt = (TEMPO_US / 1000000.0) / PPQN;
    struct Act { long long tick; int vel; };
    Act active[128];
    bool has[128] = {false};
    std::vector<Note> out;
    auto push = [&](long long t0, long long t1, int note, int vel) {
        Note q;
        q.start_s = (double)t0 * spt;
        double end = (double)std::max(t0 + 1, t1) * spt;
        q.dur_s = end - q.start_s;
        q.note = note;
        q.vel = vel;
        out.push_back(q);
    };
    for (const E& e : ev) {
        if (e.order == 3) {
            if (has[e.note]) push(active[e.note].tick, e.tick, e.note, active[e.note].vel);
            active[e.note] = {e.tick, e.vel};
            has[e.note] = true;
        } else if (has[e.note]) {
            push(active[e.note].tick, e.tick, e.note, active[e.note].vel);
            has[e.note] = false;
        }
    }
    for (int n = 0; n < 128; ++n) {
        if (!has[n]) continue;
        Note q;
        q.start_s = (double)active[n].tick * spt;
        q.dur_s = 0.1;
        q.note = n;
        q.vel = active[n].vel;
        out.push_back(q);
    }
    std::stable_sort(out.begin(), out.end(), [](const Note& a, const Note& b) {
        return a.start_s != b.start_s ? a.start_s < b.start_s : a.note < b.note;
    });
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// parse_dump
// ---------------------------------------------------------------------------

bool parse_dump(const std::string& path, int bgm_id, const std::string& slug, SongNotes* out,
                std::map<std::string, std::vector<uint8_t>>* patches) {
    FILE* f = fopen_u8(path, "rb");
    if (!f) return false;
    std::string data;
    {
        char buf[1 << 16];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
    }
    std::fclose(f);

    std::vector<RawEv> raw;
    raw.reserve(data.size() / 32);
    int max_cyc = 0;
    {
        const char* p = data.data();
        const char* end = p + data.size();
        while (p < end) {
            const char* nl = (const char*)std::memchr(p, '\n', (size_t)(end - p));
            const char* le = nl ? nl : end;
            RawEv ev;
            if (parse_app_line(p, le, ev)) {
                if (ev.cyc > max_cyc) max_cyc = ev.cyc;
                raw.push_back(ev);
            }
            p = nl ? nl + 1 : end;
        }
    }
    if (raw.empty()) return false;
    const double fc = max_cyc > 135000 ? MASTER_FRAME_CYCLES : M68K_FRAME_CYCLES;
    auto tof = [&](const RawEv& e) {
        return e.fr * FRAME_US + (std::min((double)e.cyc, fc) / fc) * FRAME_US;
    };
    const double t0 = tof(raw.front());
    const double t_last = tof(raw.back());

    uint8_t regs[2][256];
    std::memset(regs, 0, sizeof(regs));
    int addr[2] = {0, 0};
    FmChan fm[6];
    bool dac_enabled = false;
    std::vector<Note> fm_notes;
    std::map<uint32_t, std::vector<uint8_t>> pmap;

    // PSG / ruido / DAC
    PsgTone psg[3];
    std::vector<Note> psg_notes[3], noise_notes, dac_notes;
    int noise_reg = 0x04, noise_vol = 15;
    bool noise_on = false;
    double noise_t = 0.0;
    int noise_note = 0, noise_vel = 0;
    int latch_ch = 0;
    bool latch_vol = false;
    bool dac_active = false;
    DacBurst dac_burst(0.0, 0);

    auto emit_fm = [&](int g, double t_on, double t_off, int note, int vel, bool perc, uint32_t h, int tl) {
        if (!perc && !(12 <= note && note <= 120)) return;
        double dur_us = std::max(10000.0, t_off - t_on);
        Note n;
        n.start_s = std::max(0.0, (t_on - t0) / 1e6);
        n.dur_s = dur_us / 1e6;
        n.note = note;
        n.vel = vel;
        n.perc = perc;
        n.ch = g;
        n.tl = tl;
        fm_notes.push_back(n);
        // hash associado: guardado em paralelo
        fm_notes.back().tl = tl;
        (void)h;
    };
    std::vector<uint32_t> fm_hash;  // paralelo a fm_notes
    auto emit_fm2 = [&](int g, double t_on, double t_off, int note, int vel, bool perc, uint32_t h, int tl) {
        size_t before = fm_notes.size();
        emit_fm(g, t_on, t_off, note, vel, perc, h, tl);
        if (fm_notes.size() != before) fm_hash.push_back(h);
    };
    auto emit_psg_tone = [&](int ch, double t_on, double t_off, int note, int vel) {
        if (!(12 <= note && note <= 120)) return;
        double dur_us = std::max(10000.0, t_off - t_on);
        Note n;
        n.start_s = std::max(0.0, (t_on - t0) / 1e6);
        n.dur_s = dur_us / 1e6;
        n.note = note;
        n.vel = vel;
        psg_notes[ch].push_back(n);
    };
    auto emit_noise = [&](double t_on, double t_off, int note, int vel) {
        double dur_us = std::max(25000.0, std::min(400000.0, t_off - t_on));
        Note n;
        n.start_s = std::max(0.0, (t_on - t0) / 1e6);
        n.dur_s = dur_us / 1e6;
        n.note = note;
        n.vel = vel;
        noise_notes.push_back(n);
    };

    for (const RawEv& re : raw) {
        const double t = tof(re);
        const int val = re.val;
        if (re.fm) {
            const int port = re.port;
            const int part = (port & 2) ? 1 : 0;
            if ((port & 1) == 0) { addr[part] = val; continue; }
            const int reg = addr[part];
            regs[part][reg] = (uint8_t)val;

            // --- DAC (parse_chip_ring) ---
            if (part == 0 && reg == 0x2B) {
                dac_enabled = (val & 0x80) != 0;
                if (!dac_enabled && dac_active) {
                    if (!dac_burst.is_voice_stream()) dac_notes.push_back(dac_burst.to_note(t0));
                    dac_active = false;
                }
            } else if (part == 0 && reg == 0x2A) {
                if (!dac_active) {
                    dac_burst = DacBurst(t, val);
                    dac_active = true;
                } else if ((t - dac_burst.last_us) > 18000.0) {
                    if (!dac_burst.is_voice_stream()) dac_notes.push_back(dac_burst.to_note(t0));
                    dac_burst = DacBurst(t, val);
                } else {
                    dac_burst.feed(t, val);
                }
            }

            // --- FM por voz (voice_catalog.extract_voice_notes) ---
            if (part == 0 && reg == 0x28) {
                int c = val & 7;
                int g;
                switch (c) {
                    case 0: g = 0; break; case 1: g = 1; break; case 2: g = 2; break;
                    case 4: g = 3; break; case 5: g = 4; break; case 6: g = 5; break;
                    default: g = -1;
                }
                if (g < 0 || (g == 5 && dac_enabled)) continue;
                FmChan& st = fm[g];
                if ((val >> 4) & 0xF) {
                    if (st.s.on) {
                        emit_fm2(g, st.s.t_on, t, st.s.note, st.s.vel, st.s.perc, st.s.hash, st.s.tl);
                    }
                    bool is_perc = st.is_noise_perc_patch();
                    int note = st.current_midi_note();
                    if (is_perc || (12 <= note && note <= 120)) {
                        std::vector<uint8_t> pb = patch_bytes(regs, g / 3, g % 3);
                        uint32_t h = fnv1a32(pb.data(), pb.size());
                        if (!pmap.count(h)) pmap[h] = pb;
                        st.s = {true, t, note, st.carrier_velocity(), is_perc, h, st.eff_tl()};
                    } else {
                        st.s.on = false;
                    }
                } else if (st.s.on) {
                    emit_fm2(g, st.s.t_on, t, st.s.note, st.s.vel, st.s.perc, st.s.hash, st.s.tl);
                    st.s.on = false;
                }
            } else if (reg >= 0xA4 && reg <= 0xA6) {
                fm[part * 3 + reg - 0xA4].msb = val;
            } else if (reg >= 0xA0 && reg <= 0xA2) {
                int g = part * 3 + reg - 0xA0;
                FmChan& st = fm[g];
                st.fnum = ((st.msb & 7) << 8) | val;
                st.block = (st.msb >> 3) & 7;
                if (st.s.on) {
                    double t_on = st.s.t_on;
                    int old_note = st.s.note, old_vel = st.s.vel;
                    uint32_t h = st.s.hash;
                    int tl = st.s.tl;
                    if (!st.s.perc) {
                        int new_note = st.current_midi_note();
                        if (12 <= new_note && new_note <= 120) {
                            if ((t - t_on) < 20000.0) {
                                st.s = {true, t_on, new_note, old_vel, false, h, tl};
                            } else if (std::abs(new_note - old_note) >= 1) {
                                emit_fm2(g, t_on, t, old_note, old_vel, false, h, tl);
                                st.s = {true, t, new_note, st.carrier_velocity(), false, h, st.eff_tl()};
                            }
                        }
                    }
                }
            } else if (reg >= 0xB0 && reg <= 0xB2) {
                fm[part * 3 + reg - 0xB0].alg = val & 7;
            } else if (reg >= 0x30 && reg <= 0x3E && (reg & 3) != 3) {
                fm[part * 3 + (reg & 3)].mul[(reg - 0x30) >> 2] = val & 0x0F;
            } else if (reg >= 0x40 && reg <= 0x4E && (reg & 3) != 3) {
                fm[part * 3 + (reg & 3)].tl[(reg - 0x40) >> 2] = val & 0x7F;
            }
        } else {
            // --- SN76489 ---
            if (val & 0x80) {
                latch_ch = (val >> 5) & 3;
                latch_vol = (val & 0x10) != 0;
                int data4 = val & 0x0F;
                if (latch_vol) {
                    if (latch_ch < 3) {
                        PsgTone& tc = psg[latch_ch];
                        int prev_vol = tc.vol;
                        tc.vol = data4;
                        if (data4 == 15) {
                            if (tc.on) {
                                emit_psg_tone(latch_ch, tc.t_on, t, tc.note, tc.vel);
                                tc.on = false;
                            }
                        } else {
                            int note = psg_period_to_midi(tc.period);
                            int vel = tc.velocity();
                            if (!tc.on) {
                                if (12 <= note && note <= 120) { tc.on = true; tc.t_on = t; tc.note = note; tc.vel = vel; }
                            } else if (prev_vol - data4 >= 4 && (t - tc.t_on) >= 35000.0) {
                                emit_psg_tone(latch_ch, tc.t_on, t, tc.note, tc.vel);
                                if (12 <= note && note <= 120) { tc.on = true; tc.t_on = t; tc.note = note; tc.vel = vel; }
                                else tc.on = false;
                            }
                        }
                    } else {
                        int prev_vol = noise_vol;
                        noise_vol = data4;
                        if (data4 == 15) {
                            if (noise_on) { emit_noise(noise_t, t, noise_note, noise_vel); noise_on = false; }
                        } else {
                            int vel = (int)std::max(20.0, std::min(127.0, py_round(20.0 + ((15 - data4) / 15.0) * 107.0)));
                            int dnote = psg_noise_drum_note(noise_reg);
                            if (!noise_on) {
                                noise_on = true; noise_t = t; noise_note = dnote; noise_vel = vel;
                            } else if (prev_vol - data4 >= 3 && (t - noise_t) >= 30000.0) {
                                emit_noise(noise_t, t, noise_note, noise_vel);
                                noise_t = t; noise_note = dnote; noise_vel = vel;
                            }
                        }
                    }
                } else {
                    if (latch_ch < 3) {
                        PsgTone& tc = psg[latch_ch];
                        tc.period = (tc.period & 0x3F0) | data4;
                    } else {
                        noise_reg = val & 0x07;
                    }
                }
            } else {
                if (latch_vol) {
                    int data4 = val & 0x0F;
                    if (latch_ch < 3) {
                        PsgTone& tc = psg[latch_ch];
                        tc.vol = data4;
                        if (data4 == 15 && tc.on) {
                            emit_psg_tone(latch_ch, tc.t_on, t, tc.note, tc.vel);
                            tc.on = false;
                        }
                    } else {
                        noise_vol = data4;
                        if (data4 == 15 && noise_on) {
                            emit_noise(noise_t, t, noise_note, noise_vel);
                            noise_on = false;
                        }
                    }
                } else if (latch_ch < 3) {
                    PsgTone& tc = psg[latch_ch];
                    tc.period = (tc.period & 0x00F) | ((val & 0x3F) << 4);
                    if (tc.vol < 15) {
                        int note = psg_period_to_midi(tc.period);
                        int vel = tc.velocity();
                        if (12 <= note && note <= 120) {
                            if (!tc.on) {
                                tc.on = true; tc.t_on = t; tc.note = note; tc.vel = vel;
                            } else if (tc.note != note) {
                                double t_on = tc.t_on;
                                int old_n = tc.note, old_v = tc.vel;
                                if ((t - t_on) < 20000.0) {
                                    tc.note = note;  // (t_on, note, old_v)
                                } else {
                                    emit_psg_tone(latch_ch, t_on, t, old_n, old_v);
                                    tc.t_on = t; tc.note = note; tc.vel = vel;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // fecha o que ficou aberto
    const double t_close = t_last + 250000.0;
    for (int g = 0; g < 6; ++g) {
        if (fm[g].s.on) {
            emit_fm2(g, fm[g].s.t_on, t_close, fm[g].s.note, fm[g].s.vel, fm[g].s.perc, fm[g].s.hash, fm[g].s.tl);
            fm[g].s.on = false;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (psg[i].on) { emit_psg_tone(i, psg[i].t_on, t_close, psg[i].note, psg[i].vel); psg[i].on = false; }
    }
    if (noise_on) emit_noise(noise_t, t_close, noise_note, noise_vel);
    if (dac_active && !dac_burst.is_voice_stream()) dac_notes.push_back(dac_burst.to_note(t0));

    auto sort_notes = [](std::vector<Note>& v) {
        std::stable_sort(v.begin(), v.end(), [](const Note& a, const Note& b) {
            return a.start_s != b.start_s ? a.start_s < b.start_s : a.note < b.note;
        });
    };

    // ordena as notas FM (global) mantendo o hash junto
    std::vector<size_t> idx(fm_notes.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        const Note &x = fm_notes[a], &y = fm_notes[b];
        return x.start_s != y.start_s ? x.start_s < y.start_s : x.note < y.note;
    });

    SongNotes sn;
    sn.bgm_id = bgm_id;
    sn.slug = slug;
    std::map<uint32_t, size_t> pos;
    for (size_t i : idx) {
        uint32_t h = fm_hash[i];
        auto it = pos.find(h);
        if (it == pos.end()) {
            VoiceTrack vt;
            vt.hash = hash_to_string(h);
            vt.patch = pmap[h];
            sn.voices.push_back(std::move(vt));
            it = pos.emplace(h, sn.voices.size() - 1).first;
        }
        sn.voices[it->second].notes.push_back(fm_notes[i]);
    }
    // ordem das trilhas do MIDI: (1o start, hash)
    std::stable_sort(sn.voices.begin(), sn.voices.end(), [](const VoiceTrack& a, const VoiceTrack& b) {
        double sa = a.notes.front().start_s, sb = b.notes.front().start_s;
        return sa != sb ? sa < sb : a.hash < b.hash;
    });
    double max_end = 0.0;
    for (VoiceTrack& v : sn.voices) {
        v.q = midi_roundtrip(v.notes);
        for (auto& n : v.q) max_end = std::max(max_end, n.end_s());
        if (patches) (*patches)[v.hash] = v.patch;
    }
    const char* names[5] = {"psg0", "psg1", "psg2", "psg_noise", "dac"};
    std::vector<Note>* srcs[5] = {&psg_notes[0], &psg_notes[1], &psg_notes[2], &noise_notes, &dac_notes};
    for (int i = 0; i < 5; ++i) {
        sort_notes(*srcs[i]);
        if (srcs[i]->empty()) continue;
        ChanTrack ct;
        ct.name = names[i];
        ct.notes = *srcs[i];
        ct.q = midi_roundtrip(ct.notes);
        for (auto& n : ct.q) max_end = std::max(max_end, n.end_s());
        sn.channels.push_back(std::move(ct));
    }
    sn.duration_s = std::max(1.0, max_end + 0.5);
    *out = std::move(sn);
    return true;
}

// ---------------------------------------------------------------------------
// metadados / heuristica
// ---------------------------------------------------------------------------

void decode_patch(const std::vector<uint8_t>& pb, VoiceMeta* m) {
    *m = VoiceMeta();
    if (pb.size() < 31) return;
    m->valid = true;
    m->alg = pb[0];
    m->fb = pb[1];
    m->ams_fms = pb[2];
    static const char* hexd = "0123456789abcdef";
    for (uint8_t b : pb) { m->patch += hexd[b >> 4]; m->patch += hexd[b & 15]; }
    for (int s = 0; s < 4; ++s) {
        const uint8_t* q = &pb[(size_t)(3 + 7 * s)];
        FmOp op;
        op.slot = "S" + std::to_string(s + 1);
        op.carrier = is_carrier_s(pb[0], s);
        op.mul = q[0] & 0xF;
        op.dt = (q[0] >> 4) & 7;
        op.ar = q[2] & 0x1F;
        op.d1r = q[3] & 0x1F;
        op.d2r = q[4] & 0x1F;
        op.sl = (q[5] >> 4) & 0xF;
        op.rr = q[5] & 0xF;
        op.tl = op.carrier ? -1 : q[1];
        m->ops.push_back(op);
    }
}

static double median_of(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return (n & 1) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

Suggestion suggest(const std::vector<uint8_t>& pb, const std::vector<Note>& notes) {
    VoiceMeta d;
    decode_patch(pb, &d);
    int alg = d.alg, fb = d.fb;
    double ar = 0, d1r = 0, d2r = 0, sl = 0;
    int nc = 0;
    for (auto& o : d.ops) {
        if (!o.carrier) continue;
        ar += o.ar; d1r += o.d1r; d2r += o.d2r; sl += o.sl;
        ++nc;
    }
    ar /= nc; d1r /= nc; d2r /= nc; sl /= nc;
    std::vector<double> tonal, durs;
    size_t nperc = 0;
    for (auto& n : notes) {
        if (!n.perc) tonal.push_back(n.note); else ++nperc;
        durs.push_back(n.dur_s);
    }
    if (tonal.empty()) for (auto& n : notes) tonal.push_back(n.note);
    double med = tonal.empty() ? 60.0 : median_of(tonal);
    double med_dur = notes.empty() ? 0.0 : median_of(durs) * 1000.0;
    double perc_frac = notes.empty() ? 0.0 : (double)nperc / (double)notes.size();

    if (perc_frac > 0.5 || (notes.size() >= 3 && med_dur < 150 && med < 48 && fb >= 6))
        return {"percussion", 0, "percussion"};
    bool decays = d2r >= 6 || (d1r >= 10 && sl >= 3);
    if (med < 48) return {"gm", (alg >= 4 || fb >= 3) ? 38 : 33, "bass"};
    if (alg == 7) return {"gm", decays ? 18 : 16, "organ"};
    if (alg <= 3 && fb >= 5) {
        if (fb >= 7 && med < 60) return {"gm", 30, "dist-guitar"};
        return med < 60 ? Suggestion{"gm", 29, "dist-guitar"} : Suggestion{"gm", 81, "lead"};
    }
    if (decays) {
        if (alg >= 4) return {"gm", 4, "plucked"};
        return fb >= 3 ? Suggestion{"gm", 7, "plucked"} : Suggestion{"gm", 0, "plucked"};
    }
    if (ar < 18) return med < 60 ? Suggestion{"gm", 89, "pad"} : Suggestion{"gm", 50, "pad"};
    if (med < 60) return {"gm", 62, "brass"};
    return alg >= 4 ? Suggestion{"gm", 80, "lead"} : Suggestion{"gm", 81, "lead"};
}

int suggest_drum_note(const std::vector<Note>& notes) {
    if (notes.empty()) return 42;
    std::vector<double> d;
    for (auto& n : notes) d.push_back(n.dur_s);
    double med = median_of(d) * 1000.0;
    return med < 140 ? 42 : (med < 350 ? 46 : 49);
}

std::vector<VoiceEntry> build_voice_entries(const std::vector<SongNotes>& songs) {
    struct Agg {
        std::vector<uint8_t> pb;
        std::vector<Note> notes;
        std::set<int> song_idx;
        std::set<int> chans;
    };
    std::map<std::string, Agg> agg;
    for (size_t si = 0; si < songs.size(); ++si) {
        int tidx = 0;
        for (int k = 0; k < bgm_count(); ++k) if (songs[si].slug == kBgm[k].slug) tidx = k;
        for (const VoiceTrack& v : songs[si].voices) {
            Agg& a = agg[v.hash];
            a.pb = v.patch;
            for (const Note& n : v.notes) { a.notes.push_back(n); a.chans.insert(n.ch); }
            a.song_idx.insert(tidx);
        }
    }
    std::vector<VoiceEntry> out;
    for (auto& kv : agg) {  // std::map = ordenado por hash (como sorted(entries))
        const Agg& a = kv.second;
        VoiceEntry e;
        e.hash = kv.first;
        Suggestion sg = suggest(a.pb, a.notes);
        decode_patch(a.pb, &e.meta);
        std::vector<double> tonal;
        double sum_dur = 0;
        for (auto& n : a.notes) {
            if (!n.perc) tonal.push_back(n.note);
            sum_dur += n.dur_s;
        }
        if (tonal.empty()) for (auto& n : a.notes) tonal.push_back(n.note);
        VoiceMeta& m = e.meta;
        for (int i : a.song_idx) m.songs.push_back(kBgm[i].slug);
        for (int c : a.chans) m.channels.push_back("fm" + std::to_string(c));
        m.notes = (int)a.notes.size();
        m.note_min = (int)*std::min_element(tonal.begin(), tonal.end());
        m.note_max = (int)*std::max_element(tonal.begin(), tonal.end());
        m.note_median = (int)median_of(tonal);
        m.avg_dur_ms = (int)py_round(1000.0 * (sum_dur / (double)a.notes.size()));
        m.suggestion = sg.label;
        e.edit.mode = sg.mode;
        e.edit.program = sg.program;
        e.edit.drum_note = sg.mode == "percussion" ? suggest_drum_note(a.notes) : 0;
        out.push_back(std::move(e));
    }
    return out;
}

// ---------------------------------------------------------------------------
// nomes GM
// ---------------------------------------------------------------------------

static const char* kGmNames[128] = {
    "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano",
    "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
    "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
    "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica",
    "Tango Accordion",
    "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)", "Electric Guitar (jazz)", "Electric Guitar (clean)",
    "Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
    "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)", "Fretless Bass", "Slap Bass 1",
    "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
    "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
    "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs", "Voice Oohs",
    "Synth Voice", "Orchestra Hit",
    "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1",
    "Synth Brass 2",
    "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
    "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
    "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)", "Lead 5 (charang)",
    "Lead 6 (voice)", "Lead 7 (fifths)", "Lead 8 (bass + lead)",
    "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)", "Pad 5 (bowed)", "Pad 6 (metallic)",
    "Pad 7 (halo)", "Pad 8 (sweep)",
    "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)", "FX 5 (brightness)",
    "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)",
    "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bag pipe", "Fiddle", "Shanai",
    "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum",
    "Reverse Cymbal",
    "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause",
    "Gunshot"};

const char* gm_program_name(int p) { return (p >= 0 && p < 128) ? kGmNames[p] : "?"; }

const char* gm_program_category(int p) {
    static const char* cats[16] = {"Piano", "Percussao cromatica", "Orgao", "Guitarra", "Baixo", "Cordas",
                                   "Conjunto", "Metais", "Palheta", "Flauta", "Lead", "Pad", "Efeito sintetico",
                                   "Etnico", "Percussivo", "Efeitos sonoros"};
    return (p >= 0 && p < 128) ? cats[p / 8] : "?";
}

const char* gm_drum_name(int n) {
    static const char* names[] = {
        "Acoustic Bass Drum", "Bass Drum 1", "Side Stick", "Acoustic Snare", "Hand Clap", "Electric Snare",
        "Low Floor Tom", "Closed Hi-Hat", "High Floor Tom", "Pedal Hi-Hat", "Low Tom", "Open Hi-Hat",
        "Low-Mid Tom", "Hi-Mid Tom", "Crash Cymbal 1", "High Tom", "Ride Cymbal 1", "Chinese Cymbal",
        "Ride Bell", "Tambourine", "Splash Cymbal", "Cowbell", "Crash Cymbal 2", "Vibraslap", "Ride Cymbal 2",
        "Hi Bongo", "Low Bongo", "Mute Hi Conga", "Open Hi Conga", "Low Conga", "High Timbale", "Low Timbale",
        "High Agogo", "Low Agogo", "Cabasa", "Maracas", "Short Whistle", "Long Whistle", "Short Guiro",
        "Long Guiro", "Claves", "Hi Wood Block", "Low Wood Block", "Mute Cuica", "Open Cuica", "Mute Triangle",
        "Open Triangle"};
    if (n < 35 || n > 81) return "";
    return names[n - 35];
}

}  // namespace ecr
