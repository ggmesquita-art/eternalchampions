/*
 * ec_audio.c — Modulo de som em tempo real em C11 para Eternal Champions.
 *
 * Host-only: opera exclusivamente sobre o observer passivo de registradores
 * YM2612/SN76489 (audio_mixer_set_chip_observer) e o buffer mix_stereo exposto
 * por audio_set_extra_source() (~223.721 Hz antes da ponte DRC). Nunca altera
 * WRAM, 68000, Z80, YM2612 ou PSG.
 *
 * Arquitetura:
 * - Modo OFF: retorno imediato na primeira linha (bit-a-bit identico).
 * - Modo DSP: cadeia de estudio custo-zero sobre o audio do chip (EQ 3 bandas
 *   RBJ em double, abertura estereo mid/side sutil, reverb Schroeder 4 combs +
 *   2 allpass com decimacao limpa em taxas altas, soft-limiter acima de 0.92).
 * - Modos HYBRID / OST: sintese SoundFont2 em tempo real via TinySoundFont
 *   (tsf.h) traduzindo o fluxo de registradores FM/PSG com precisao sub-frame
 *   e perfis .toml por bgm_id, somada antes do reverb de sala custo-zero e
 *   mixada com o chip original (que mantem 100% dos SFX e vozes PCM $2A).
 */

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "ec_audio.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#endif

#define TSF_STATIC
#define TSF_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "third_party/tsf.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#define EC_PI                   3.14159265358979323846
#define EC_AUDIO_PATH_MAX       1024
#define EC_AUDIO_NAME_MAX       128

#define EC_COMB_COUNT           4
#define EC_AP_COUNT             2
#define EC_COMB_MAX_LEN         4096u
#define EC_AP_MAX_LEN           1536u

#define EC_DEFAULT_HYBRID_BLEND 0.65f
#define EC_DEFAULT_OST_BLEND    0.18f
#define EC_DEFAULT_DSP_REVERB   0.18f
#define EC_CHIP_DSP_GAIN        1.75f   /* medido: ver docs/CONFIGURATION_AND_CLI.md (limiter) */
#define EC_SF2_MAKEUP_GAIN      0.25f

#define EC_FADE_IN_SEC          0.010
#define EC_FADE_OUT_SEC         0.400
#define EC_PAUSE_RAMP_SEC       0.005
#define EC_DUCK_RAMP_SEC        0.015

#define EC_SF2_SAMPLE_RATE      48000
#define EC_SF2_MAX_VOICES       96
#define EC_SF2_BUF_MAX          2048u
#define EC_TL_MUTE              100     /* TL >= 100 (75 dB) = canal efetivamente mudo */
#define EC_TL_DB_PER_STEP_LIN   0.018f  /* mesma curva de ec_ym_tl_to_vel */
#define EC_EVENT_CAP            8192u
#define EC_MAX_PROFILES         64u
#define EC_DEFAULT_FRAME_CYCLES 895780u

enum {
    EC_CH_FM0 = 0,
    EC_CH_FM1 = 1,
    EC_CH_FM2 = 2,
    EC_CH_FM3 = 3,
    EC_CH_FM4 = 4,
    EC_CH_FM5 = 5,
    EC_CH_DAC = 6,
    EC_CH_PSG0 = 7,
    EC_CH_PSG1 = 8,
    EC_CH_PSG2 = 9,
    EC_CH_PSG_NOISE = 10,
    EC_CH_COUNT = 11
};

static const char *const k_channel_names[EC_CH_COUNT] = {
    "fm0", "fm1", "fm2", "fm3", "fm4", "fm5",
    "dac", "psg0", "psg1", "psg2", "psg_noise"
};

typedef enum {
    EC_CHAN_MODE_AUTO = 0,
    EC_CHAN_MODE_GM,
    EC_CHAN_MODE_PERCUSSION,
    EC_CHAN_MODE_CHIP,
    EC_CHAN_MODE_OFF
} EcChanMode;

typedef struct {
    EcChanMode mode;
    int        program;
    int        bank;
    float      volume_db;
    float      pan;
    int        mute;
    int        solo;
    int        transpose;
} EcChanProfile;

typedef struct {
    char          name[EC_AUDIO_NAME_MAX];
    int           bgm_id;
    float         master_gain_db;
    float         chip_blend;
    int           has_chip_blend;
    EcChanProfile channels[EC_CH_COUNT];
} EcTrackProfile;

/* ---- Chip register shadow & sub-frame event queue ------------------------ */

typedef struct {
    uint32_t cycle_stamp;
    uint8_t  port;
    uint8_t  value;
} EcChipEvent;

typedef struct {
    uint16_t fnum;
    uint8_t  block;
    uint8_t  msb_latch;
    uint8_t  alg;
    uint8_t  tl[4];
    uint8_t  mul[4];
    uint8_t  ar[4];
    uint8_t  rr[4];
    int      sounding;       /* nota SF2 ativa */
    int      keyed;          /* key-on ativo no hardware (independente de sounding) */
    uint8_t  known;          /* bits EC_KN_*: registradores ja reescritos desde o reset */
    float    ref_gain;       /* ganho linear TL no note-on (referencia p/ fades) */
    float    cur_ratio;      /* razao de volume TL atual aplicada ao canal MIDI */
    int      active_tsf_ch;
    int      active_note;
    int      is_perc;
} EcFmChan;

/* Bits de "registrador conhecido": o shadow so e confiavel depois que o driver
 * reescreve o patch (alg + 4 TL) e a frequencia do canal. */
#define EC_KN_FNUM_LO  0x01u
#define EC_KN_FNUM_HI  0x02u
#define EC_KN_ALG      0x04u
#define EC_KN_TL0      0x08u   /* TL op0..op3 = 0x08,0x10,0x20,0x40 */
#define EC_KN_ALL      0x7Fu

typedef struct {
    uint16_t period;
    uint8_t  vol;
    uint8_t  period_known;   /* bit0 = low nibble escrito, bit1 = high escrito */
    int      sounding;
    int      active_note;
} EcPsgToneChan;

typedef struct {
    uint8_t       fm_addr[2];
    int           dac_enabled;
    EcFmChan      fm[6];

    EcPsgToneChan psg_tone[3];
    uint8_t       psg_noise_ctrl;
    uint8_t       psg_noise_vol;
    int           psg_noise_sounding;
    int           psg_noise_note;
    uint8_t       psg_latch_ch;
    uint8_t       psg_latch_is_vol;
} EcChipShadow;

/* ---- DSP structures ------------------------------------------------------ */

typedef struct {
    double b0, b1, b2;
    double a1, a2;
    double z1[2], z2[2];
} EcBiquad;

typedef struct {
    float    buf[EC_COMB_MAX_LEN];
    uint32_t len;
    uint32_t idx;
    float    filter_store;
} EcComb;

typedef struct {
    float    buf[EC_AP_MAX_LEN];
    uint32_t len;
    uint32_t idx;
} EcAllpass;

typedef struct {
    double    configured_rate;
    EcBiquad  eq_low;
    EcBiquad  eq_mid;
    EcBiquad  eq_high;

    int       rev_decim;
    int       rev_phase;
    float     rev_acc_l;
    float     rev_acc_r;
    float     rev_prev_l;
    float     rev_prev_r;
    float     rev_curr_l;
    float     rev_curr_r;
    EcComb    comb_l[EC_COMB_COUNT];
    EcComb    comb_r[EC_COMB_COUNT];
    EcAllpass ap_l[EC_AP_COUNT];
    EcAllpass ap_r[EC_AP_COUNT];
} EcDspState;

/* ---- Module singleton state ---------------------------------------------- */

typedef struct {
    EcAudioMode           mode;
    float                 bgm_vol;
    float                 sfx_vol;
    float                 chip_blend_user;      /* settings.ini / env: default do usuario */
    int                   has_chip_blend_user;
    float                 chip_blend_cli;       /* --audio-chip-blend: override explicito */
    int                   has_chip_blend_cli;
    float                 dsp_reverb;

    /* TinySoundFont synth + profiles */
    tsf                  *tsf;
    int                   tsf_cur_bank[EC_CH_COUNT];
    int                   tsf_cur_prog[EC_CH_COUNT];
    int                   chan_silenced[EC_CH_COUNT];
    float                 chan_base_vol[EC_CH_COUNT];   /* volume linear do perfil (0 se silenciado) */

    EcTrackProfile        default_profile;
    int                   default_profile_ready;
    EcTrackProfile        profiles[EC_MAX_PROFILES];
    size_t                profile_count;
    const EcTrackProfile *active_profile;

    /* Observer queue (written on emulation thread before audio_flush) */
    uint8_t               obs_addr[2];
    int                   obs_addr_valid[2];   /* 0 apos reset/troca de modo: dados sem endereco sao descartados */
    EcChipEvent           events[EC_EVENT_CAP];
    size_t                ev_count;
    uint32_t              frame_end_cycle;

    /* Chip state & playback envelopes */
    EcChipShadow          chip;
    uint32_t              current_bgm_id;
    int                   has_bgm_id;
    int                   synth_active;
    int                   paused;
    int                   need_resync;   /* re-emitir notes `keyed` no proximo mix (pausa/turbo/perfil) */
    int                   fading_out;
    float                 fade_env;
    float                 pause_env;
    float                 chip_duck;

    /* Studio DSP state */
    EcDspState            dsp;
} EcAudioState;

static EcAudioState s_audio = {
    .mode              = EC_AUDIO_MODE_OFF,
    .bgm_vol           = 1.0f,
    .sfx_vol           = 1.0f,
    .dsp_reverb        = EC_DEFAULT_DSP_REVERB,
    .fade_env          = 0.0f,
    .pause_env         = 1.0f,
    .chip_duck         = 1.0f,
    .synth_active      = 0
};

/* EC_AUDIO_TRACE=1: log de eventos de ciclo de vida (play/pause/resync/state load). */
static int ec_trace_enabled(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char *e = getenv("EC_AUDIO_TRACE");
        cached = (e && *e && *e != '0') ? 1 : 0;
    }
    return cached;
}

/* ---- Portable helpers ---------------------------------------------------- */

static int ec_strcasecmp(const char *a, const char *b)
{
    if (!a || !b) {
        return (a == b) ? 0 : (a ? 1 : -1);
    }
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        ++a;
        ++b;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static float ec_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int ec_clamp_i(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void ec_normalize_slashes(char *path)
{
    if (!path) {
        return;
    }
    for (char *p = path; *p; ++p) {
        if (*p == '\\') {
            *p = '/';
        }
    }
}

static int ec_join_path(char *out, size_t out_size, const char *base, const char *rel)
{
    if (!out || out_size == 0 || !rel) {
        return 0;
    }
    if (!base || !*base) {
        int n = snprintf(out, out_size, "%s", rel);
        if (n < 0 || (size_t)n >= out_size) {
            return 0;
        }
        ec_normalize_slashes(out);
        return 1;
    }
    size_t blen = strlen(base);
    int has_sep = (blen > 0 && (base[blen - 1] == '/' || base[blen - 1] == '\\'));
    const char *r = rel;
    while (*r == '/' || *r == '\\') {
        ++r;
    }
    int n = has_sep
        ? snprintf(out, out_size, "%s%s", base, r)
        : snprintf(out, out_size, "%s/%s", base, r);
    if (n < 0 || (size_t)n >= out_size) {
        return 0;
    }
    ec_normalize_slashes(out);
    return 1;
}

static int ec_ends_with_ci(const char *str, const char *suffix)
{
    if (!str || !suffix) {
        return 0;
    }
    size_t slen = strlen(str);
    size_t suflen = strlen(suffix);
    if (slen < suflen) {
        return 0;
    }
    return ec_strcasecmp(str + (slen - suflen), suffix) == 0;
}

/* ---- DSP implementation -------------------------------------------------- */

static void ec_biquad_clear(EcBiquad *bq)
{
    bq->z1[0] = bq->z1[1] = 0.0;
    bq->z2[0] = bq->z2[1] = 0.0;
}

static void ec_biquad_set_identity(EcBiquad *bq)
{
    bq->b0 = 1.0; bq->b1 = 0.0; bq->b2 = 0.0;
    bq->a1 = 0.0; bq->a2 = 0.0;
}

static void ec_biquad_design_lowshelf(EcBiquad *bq, double fs, double f0, double db_gain, double slope)
{
    if (fs <= 0.0 || f0 <= 0.0 || f0 >= 0.45 * fs || slope <= 0.0) {
        ec_biquad_set_identity(bq);
        return;
    }
    double A = pow(10.0, db_gain / 40.0);
    double w0 = 2.0 * EC_PI * f0 / fs;
    double cw = cos(w0);
    double sw = sin(w0);
    double disc = (A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0;
    if (disc < 0.0) {
        disc = 0.0;
    }
    double alpha = (sw * 0.5) * sqrt(disc);
    double two_sqrt_a_alpha = 2.0 * sqrt(A) * alpha;

    double b0 = A * ((A + 1.0) - (A - 1.0) * cw + two_sqrt_a_alpha);
    double b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
    double b2 = A * ((A + 1.0) - (A - 1.0) * cw - two_sqrt_a_alpha);
    double a0 = (A + 1.0) + (A - 1.0) * cw + two_sqrt_a_alpha;
    double a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw);
    double a2 = (A + 1.0) + (A - 1.0) * cw - two_sqrt_a_alpha;

    if (fabs(a0) < 1e-12) {
        ec_biquad_set_identity(bq);
        return;
    }
    double inv_a0 = 1.0 / a0;
    bq->b0 = b0 * inv_a0;
    bq->b1 = b1 * inv_a0;
    bq->b2 = b2 * inv_a0;
    bq->a1 = a1 * inv_a0;
    bq->a2 = a2 * inv_a0;
}

static void ec_biquad_design_peaking(EcBiquad *bq, double fs, double f0, double db_gain, double q)
{
    if (fs <= 0.0 || f0 <= 0.0 || f0 >= 0.45 * fs || q <= 0.0) {
        ec_biquad_set_identity(bq);
        return;
    }
    double A = pow(10.0, db_gain / 40.0);
    double w0 = 2.0 * EC_PI * f0 / fs;
    double cw = cos(w0);
    double sw = sin(w0);
    double alpha = sw / (2.0 * q);

    double b0 = 1.0 + alpha * A;
    double b1 = -2.0 * cw;
    double b2 = 1.0 - alpha * A;
    double a0 = 1.0 + alpha / A;
    double a1 = -2.0 * cw;
    double a2 = 1.0 - alpha / A;

    if (fabs(a0) < 1e-12) {
        ec_biquad_set_identity(bq);
        return;
    }
    double inv_a0 = 1.0 / a0;
    bq->b0 = b0 * inv_a0;
    bq->b1 = b1 * inv_a0;
    bq->b2 = b2 * inv_a0;
    bq->a1 = a1 * inv_a0;
    bq->a2 = a2 * inv_a0;
}

static void ec_biquad_design_highshelf(EcBiquad *bq, double fs, double f0, double db_gain, double slope)
{
    if (fs <= 0.0 || f0 <= 0.0 || f0 >= 0.45 * fs || slope <= 0.0) {
        ec_biquad_set_identity(bq);
        return;
    }
    double A = pow(10.0, db_gain / 40.0);
    double w0 = 2.0 * EC_PI * f0 / fs;
    double cw = cos(w0);
    double sw = sin(w0);
    double disc = (A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0;
    if (disc < 0.0) {
        disc = 0.0;
    }
    double alpha = (sw * 0.5) * sqrt(disc);
    double two_sqrt_a_alpha = 2.0 * sqrt(A) * alpha;

    double b0 = A * ((A + 1.0) + (A - 1.0) * cw + two_sqrt_a_alpha);
    double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
    double b2 = A * ((A + 1.0) + (A - 1.0) * cw - two_sqrt_a_alpha);
    double a0 = (A + 1.0) - (A - 1.0) * cw + two_sqrt_a_alpha;
    double a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw);
    double a2 = (A + 1.0) - (A - 1.0) * cw - two_sqrt_a_alpha;

    if (fabs(a0) < 1e-12) {
        ec_biquad_set_identity(bq);
        return;
    }
    double inv_a0 = 1.0 / a0;
    bq->b0 = b0 * inv_a0;
    bq->b1 = b1 * inv_a0;
    bq->b2 = b2 * inv_a0;
    bq->a1 = a1 * inv_a0;
    bq->a2 = a2 * inv_a0;
}

static inline double ec_biquad_tick(EcBiquad *bq, int ch, double x)
{
    double y = bq->b0 * x + bq->z1[ch];
    bq->z1[ch] = bq->b1 * x - bq->a1 * y + bq->z2[ch];
    bq->z2[ch] = bq->b2 * x - bq->a2 * y;
    return y;
}

static void ec_biquad_flush_denormals(EcBiquad *bq)
{
    for (int ch = 0; ch < 2; ++ch) {
        if (!isfinite(bq->z1[ch]) || fabs(bq->z1[ch]) < 1e-20) bq->z1[ch] = 0.0;
        if (!isfinite(bq->z2[ch]) || fabs(bq->z2[ch]) < 1e-20) bq->z2[ch] = 0.0;
    }
}

static uint32_t ec_delay_len(double rev_rate, double delay_ms, uint32_t max_len)
{
    double samples = rev_rate * (delay_ms * 0.001);
    if (samples < 1.0) {
        return 1u;
    }
    if (samples >= (double)(max_len - 1u)) {
        return max_len - 1u;
    }
    return (uint32_t)(samples + 0.5);
}

static void ec_dsp_clear_buffers(EcDspState *dsp)
{
    ec_biquad_clear(&dsp->eq_low);
    ec_biquad_clear(&dsp->eq_mid);
    ec_biquad_clear(&dsp->eq_high);

    dsp->rev_phase  = 0;
    dsp->rev_acc_l  = 0.0f;
    dsp->rev_acc_r  = 0.0f;
    dsp->rev_prev_l = 0.0f;
    dsp->rev_prev_r = 0.0f;
    dsp->rev_curr_l = 0.0f;
    dsp->rev_curr_r = 0.0f;

    for (int i = 0; i < EC_COMB_COUNT; ++i) {
        memset(dsp->comb_l[i].buf, 0, sizeof(dsp->comb_l[i].buf));
        dsp->comb_l[i].idx = 0;
        dsp->comb_l[i].filter_store = 0.0f;
        memset(dsp->comb_r[i].buf, 0, sizeof(dsp->comb_r[i].buf));
        dsp->comb_r[i].idx = 0;
        dsp->comb_r[i].filter_store = 0.0f;
    }
    for (int i = 0; i < EC_AP_COUNT; ++i) {
        memset(dsp->ap_l[i].buf, 0, sizeof(dsp->ap_l[i].buf));
        dsp->ap_l[i].idx = 0;
        memset(dsp->ap_r[i].buf, 0, sizeof(dsp->ap_r[i].buf));
        dsp->ap_r[i].idx = 0;
    }
}

static void ec_dsp_configure(EcDspState *dsp, double rate)
{
    static const double k_comb_ms_l[EC_COMB_COUNT] = { 25.31, 28.93, 32.47, 36.19 };
    static const double k_comb_ms_r[EC_COMB_COUNT] = { 25.87, 29.51, 33.09, 36.73 };
    static const double k_ap_ms_l[EC_AP_COUNT]     = {  5.03, 11.27 };
    static const double k_ap_ms_r[EC_AP_COUNT]     = {  5.41, 11.69 };

    /* 3-band studio EQ tuned for Genesis YM2612 + PSG + DAC */
    ec_biquad_design_lowshelf(&dsp->eq_low,  rate, 110.0,  +1.5, 0.80);
    ec_biquad_design_peaking (&dsp->eq_mid,  rate, 2800.0, -2.0, 1.10);
    ec_biquad_design_highshelf(&dsp->eq_high, rate, 8500.0, +1.0, 0.75);

    /* Keep reverb internal rate <= 64 kHz via clean integer decimation so RAM
     * stays L2-friendly (~152 KiB) and CPU cost is negligible even at 223.7 kHz. */
    int decim = 1;
    if (rate > 64000.0) {
        decim = (int)ceil(rate / 64000.0);
        if (decim < 1) {
            decim = 1;
        }
    }
    double rev_rate = rate / (double)decim;

    dsp->rev_decim = decim;
    for (int i = 0; i < EC_COMB_COUNT; ++i) {
        dsp->comb_l[i].len = ec_delay_len(rev_rate, k_comb_ms_l[i], EC_COMB_MAX_LEN);
        dsp->comb_r[i].len = ec_delay_len(rev_rate, k_comb_ms_r[i], EC_COMB_MAX_LEN);
    }
    for (int i = 0; i < EC_AP_COUNT; ++i) {
        dsp->ap_l[i].len = ec_delay_len(rev_rate, k_ap_ms_l[i], EC_AP_MAX_LEN);
        dsp->ap_r[i].len = ec_delay_len(rev_rate, k_ap_ms_r[i], EC_AP_MAX_LEN);
    }

    ec_dsp_clear_buffers(dsp);
    dsp->configured_rate = rate;
}

static inline float ec_comb_tick(EcComb *c, float in, float fb, float damp1, float damp2)
{
    float out = c->buf[c->idx];
    c->filter_store = (out * damp2) + (c->filter_store * damp1);
    c->buf[c->idx] = in + (c->filter_store * fb);
    uint32_t next = c->idx + 1u;
    c->idx = (next >= c->len) ? 0u : next;
    return out;
}

static inline float ec_allpass_tick(EcAllpass *ap, float in, float fb)
{
    float bufout = ap->buf[ap->idx];
    float v = in + (bufout * fb);
    float out = bufout - (v * fb);
    ap->buf[ap->idx] = v;
    uint32_t next = ap->idx + 1u;
    ap->idx = (next >= ap->len) ? 0u : next;
    return out;
}

static void ec_reverb_tick_stereo(EcDspState *dsp, float in_l, float in_r,
                                  float *out_l, float *out_r)
{
    const float comb_fb = 0.72f;
    const float damp1   = 0.35f;
    const float damp2   = 1.0f - damp1;
    const float ap_fb   = 0.50f;

    /* Slight cross-feed into reverb tank for cohesive studio room glue */
    float feed_l = in_l * 0.75f + in_r * 0.25f;
    float feed_r = in_r * 0.75f + in_l * 0.25f;

    float sum_l = 0.0f;
    float sum_r = 0.0f;
    for (int i = 0; i < EC_COMB_COUNT; ++i) {
        sum_l += ec_comb_tick(&dsp->comb_l[i], feed_l, comb_fb, damp1, damp2);
        sum_r += ec_comb_tick(&dsp->comb_r[i], feed_r, comb_fb, damp1, damp2);
    }
    sum_l *= 0.25f;
    sum_r *= 0.25f;

    for (int i = 0; i < EC_AP_COUNT; ++i) {
        sum_l = ec_allpass_tick(&dsp->ap_l[i], sum_l, ap_fb);
        sum_r = ec_allpass_tick(&dsp->ap_r[i], sum_r, ap_fb);
    }

    *out_l = sum_l;
    *out_r = sum_r;
}

static void ec_dsp_flush_denormals(EcDspState *dsp)
{
    ec_biquad_flush_denormals(&dsp->eq_low);
    ec_biquad_flush_denormals(&dsp->eq_mid);
    ec_biquad_flush_denormals(&dsp->eq_high);

    for (int i = 0; i < EC_COMB_COUNT; ++i) {
        if (!isfinite(dsp->comb_l[i].filter_store) || fabsf(dsp->comb_l[i].filter_store) < 1e-20f)
            dsp->comb_l[i].filter_store = 0.0f;
        if (!isfinite(dsp->comb_r[i].filter_store) || fabsf(dsp->comb_r[i].filter_store) < 1e-20f)
            dsp->comb_r[i].filter_store = 0.0f;
    }
}

/* Transparent up to 0.92, C1-continuous soft-limiting curve asymptotically -> 1.0 */
static inline float ec_soft_limit(float x)
{
    const float thresh = 0.92f;
    const float head   = 0.08f;
    float ax = fabsf(x);
    if (ax <= thresh) {
        return x;
    }
    float over = (ax - thresh) / head;
    float limited = thresh + head * (over / (1.0f + over));
    return (x >= 0.0f) ? limited : -limited;
}

static inline int16_t ec_float_to_s16(float x)
{
    if (!isfinite(x)) {
        return 0;
    }
    float s = ec_soft_limit(x) * 32767.0f;
    if (s >= 32767.0f) return 32767;
    if (s <= -32768.0f) return -32768;
    return (int16_t)(s >= 0.0f ? (s + 0.5f) : (s - 0.5f));
}

/* ---- TOML profile parser ------------------------------------------------- */

static char *ec_trim_inplace(char *str)
{
    while (*str && isspace((unsigned char)*str)) {
        ++str;
    }
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
    return str;
}

static void ec_strip_toml_comment(char *line)
{
    int in_double = 0;
    int in_single = 0;
    for (char *p = line; *p; ++p) {
        if (*p == '\\' && in_double && p[1]) {
            ++p;
            continue;
        }
        if (*p == '"' && !in_single) {
            in_double = !in_double;
        } else if (*p == '\'' && !in_double) {
            in_single = !in_single;
        } else if (*p == '#' && !in_double && !in_single) {
            *p = '\0';
            return;
        }
    }
}

static int ec_parse_toml_string(const char *val, char *out, size_t out_size)
{
    if (!val || !out || out_size == 0) {
        return 0;
    }
    size_t len = strlen(val);
    if (len < 2) {
        return 0;
    }
    char quote = val[0];
    if ((quote != '"' && quote != '\'') || val[len - 1] != quote) {
        return 0;
    }
    size_t oi = 0;
    for (size_t i = 1; i + 1 < len; ++i) {
        char c = val[i];
        if (quote == '"' && c == '\\' && i + 2 < len) {
            char next = val[++i];
            switch (next) {
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case '\\': c = '\\'; break;
                case '"':  c = '"';  break;
                default:   c = next; break;
            }
        }
        if (oi + 1 < out_size) {
            out[oi++] = c;
        }
    }
    out[oi] = '\0';
    return 1;
}

static int ec_parse_i32(const char *val, int *out)
{
    if (!val || !*val || !out) {
        return 0;
    }
    char *end = NULL;
    long v = strtol(val, &end, 10);
    if (end == val || *end != '\0') {
        return 0;
    }
    *out = (int)v;
    return 1;
}

static int ec_parse_float(const char *val, float *out)
{
    if (!val || !*val || !out) {
        return 0;
    }
    char *end = NULL;
    double v = strtod(val, &end);
    if (end == val || *end != '\0' || !isfinite(v)) {
        return 0;
    }
    *out = (float)v;
    return 1;
}

static int ec_parse_bool(const char *val, int *out)
{
    if (!val || !*val || !out) {
        return 0;
    }
    if (ec_strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0) {
        *out = 1;
        return 1;
    }
    if (ec_strcasecmp(val, "false") == 0 || strcmp(val, "0") == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

static void ec_init_builtin_default_profile(EcTrackProfile *p)
{
    static const float k_fm_pans[6]  = { -0.25f, 0.25f, -0.35f, 0.35f, 0.0f, 0.0f };
    static const float k_psg_pans[3] = { -0.45f, 0.0f, 0.45f };

    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "%s", "default");
    p->bgm_id         = -1;
    p->master_gain_db = 0.0f;
    p->chip_blend     = EC_DEFAULT_HYBRID_BLEND;
    p->has_chip_blend = 0;   /* so vale se o .toml definir chip_blend */

    for (int i = 0; i < 6; ++i) {
        p->channels[EC_CH_FM0 + i].mode      = EC_CHAN_MODE_AUTO;
        p->channels[EC_CH_FM0 + i].program   = 0;
        p->channels[EC_CH_FM0 + i].bank      = 0;
        p->channels[EC_CH_FM0 + i].volume_db = 0.0f;
        p->channels[EC_CH_FM0 + i].pan       = k_fm_pans[i];
    }

    p->channels[EC_CH_DAC].mode      = EC_CHAN_MODE_PERCUSSION;
    p->channels[EC_CH_DAC].program   = 0;
    p->channels[EC_CH_DAC].bank      = 128;
    p->channels[EC_CH_DAC].volume_db = -2.0f;
    p->channels[EC_CH_DAC].pan       = 0.0f;

    for (int i = 0; i < 3; ++i) {
        p->channels[EC_CH_PSG0 + i].mode      = EC_CHAN_MODE_GM;
        p->channels[EC_CH_PSG0 + i].program   = 80;
        p->channels[EC_CH_PSG0 + i].bank      = 0;
        p->channels[EC_CH_PSG0 + i].volume_db = -9.0f;
        p->channels[EC_CH_PSG0 + i].pan       = k_psg_pans[i];
    }

    p->channels[EC_CH_PSG_NOISE].mode      = EC_CHAN_MODE_PERCUSSION;
    p->channels[EC_CH_PSG_NOISE].program   = 0;
    p->channels[EC_CH_PSG_NOISE].bank      = 128;
    p->channels[EC_CH_PSG_NOISE].volume_db = -4.0f;
    p->channels[EC_CH_PSG_NOISE].pan       = 0.15f;
}

static void ec_ensure_default_profile(void)
{
    if (!s_audio.default_profile_ready) {
        ec_init_builtin_default_profile(&s_audio.default_profile);
        s_audio.default_profile_ready = 1;
    }
    if (!s_audio.active_profile) {
        s_audio.active_profile = &s_audio.default_profile;
    }
}

static int ec_find_channel_index(const char *name)
{
    if (!name) {
        return -1;
    }
    for (int i = 0; i < EC_CH_COUNT; ++i) {
        if (strcmp(name, k_channel_names[i]) == 0) {
            return i;
        }
    }
    return -1;
}

static int ec_parse_chan_mode(const char *s, EcChanMode *out)
{
    if (!s || !out) {
        return 0;
    }
    if (ec_strcasecmp(s, "auto") == 0) {
        *out = EC_CHAN_MODE_AUTO;
        return 1;
    }
    if (ec_strcasecmp(s, "gm") == 0) {
        *out = EC_CHAN_MODE_GM;
        return 1;
    }
    if (ec_strcasecmp(s, "percussion") == 0) {
        *out = EC_CHAN_MODE_PERCUSSION;
        return 1;
    }
    if (ec_strcasecmp(s, "chip") == 0) {
        *out = EC_CHAN_MODE_CHIP;
        return 1;
    }
    if (ec_strcasecmp(s, "off") == 0) {
        *out = EC_CHAN_MODE_OFF;
        return 1;
    }
    return 0;
}

static int ec_parse_profile_toml(const char *path, const char *filename, EcTrackProfile *out)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }

    ec_init_builtin_default_profile(out);
    if (filename && *filename) {
        snprintf(out->name, sizeof(out->name), "%s", filename);
        char *dot = strrchr(out->name, '.');
        if (dot) {
            *dot = '\0';
        }
    }

    enum { PSEC_NONE = 0, PSEC_TRACK = 1, PSEC_CHANNEL = 2 } section = PSEC_NONE;
    int cur_ch = -1;
    int first_line = 1;

    char line_buf[2048];
    while (fgets(line_buf, (int)sizeof(line_buf), fp) != NULL) {
        char *line = line_buf;
        {
            size_t raw_len = strlen(line_buf);
            if (raw_len > 0 && line_buf[raw_len - 1] != '\n' && !feof(fp)) {
                /* linha maior que o buffer: descarta o resto e ignora a linha inteira */
                int c;
                while ((c = fgetc(fp)) != '\n' && c != EOF) {
                }
                fprintf(stderr, "[EC-AUDIO] aviso: linha muito longa em '%s' ignorada\n", path);
                continue;
            }
        }
        if (first_line) {
            first_line = 0;
            if ((unsigned char)line[0] == 0xEFu &&
                (unsigned char)line[1] == 0xBBu &&
                (unsigned char)line[2] == 0xBFu) {
                line += 3;
            }
        }

        ec_strip_toml_comment(line);
        line = ec_trim_inplace(line);
        if (!*line) {
            continue;
        }

        if (line[0] == '[') {
            cur_ch = -1;
            if (strcmp(line, "[track]") == 0) {
                section = PSEC_TRACK;
            } else if (strncmp(line, "[channels.", 10) == 0) {
                size_t len = strlen(line);
                if (len > 11 && line[len - 1] == ']') {
                    char ch_name[32];
                    size_t nlen = len - 11;
                    if (nlen < sizeof(ch_name)) {
                        memcpy(ch_name, line + 10, nlen);
                        ch_name[nlen] = '\0';
                        cur_ch = ec_find_channel_index(ch_name);
                    }
                }
                section = (cur_ch >= 0) ? PSEC_CHANNEL : PSEC_NONE;
            } else {
                section = PSEC_NONE;
            }
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        char *key = ec_trim_inplace(line);
        char *val = ec_trim_inplace(eq + 1);
        if (!*key || !*val) {
            continue;
        }

        if (section == PSEC_TRACK) {
            if (strcmp(key, "name") == 0) {
                (void)ec_parse_toml_string(val, out->name, sizeof(out->name));
            } else if (strcmp(key, "bgm_id") == 0) {
                int id = -1;
                if (ec_parse_i32(val, &id)) {
                    out->bgm_id = id;
                }
            } else if (strcmp(key, "master_gain_db") == 0) {
                float g = 0.0f;
                if (ec_parse_float(val, &g)) {
                    out->master_gain_db = ec_clampf(g, -24.0f, 18.0f);
                }
            } else if (strcmp(key, "chip_blend") == 0) {
                float cb = 0.0f;
                if (ec_parse_float(val, &cb)) {
                    out->chip_blend = ec_clampf(cb, 0.0f, 1.0f);
                    out->has_chip_blend = 1;
                }
            }
        } else if (section == PSEC_CHANNEL && cur_ch >= 0 && cur_ch < EC_CH_COUNT) {
            EcChanProfile *cp = &out->channels[cur_ch];
            if (strcmp(key, "mode") == 0) {
                char mode_buf[32];
                if (ec_parse_toml_string(val, mode_buf, sizeof(mode_buf))) {
                    (void)ec_parse_chan_mode(mode_buf, &cp->mode);
                }
            } else if (strcmp(key, "program") == 0) {
                int p = 0;
                if (ec_parse_i32(val, &p)) {
                    cp->program = ec_clamp_i(p, 0, 127);
                }
            } else if (strcmp(key, "bank") == 0) {
                int b = 0;
                if (ec_parse_i32(val, &b)) {
                    cp->bank = ec_clamp_i(b, 0, 128);
                }
            } else if (strcmp(key, "volume_db") == 0) {
                float v = 0.0f;
                if (ec_parse_float(val, &v)) {
                    cp->volume_db = ec_clampf(v, -60.0f, 24.0f);
                }
            } else if (strcmp(key, "pan") == 0) {
                float pn = 0.0f;
                if (ec_parse_float(val, &pn)) {
                    cp->pan = ec_clampf(pn, -1.0f, 1.0f);
                }
            } else if (strcmp(key, "mute") == 0) {
                int b = 0;
                if (ec_parse_bool(val, &b)) {
                    cp->mute = b;
                }
            } else if (strcmp(key, "solo") == 0) {
                int b = 0;
                if (ec_parse_bool(val, &b)) {
                    cp->solo = b;
                }
            } else if (strcmp(key, "transpose") == 0) {
                int tr = 0;
                if (ec_parse_i32(val, &tr)) {
                    cp->transpose = ec_clamp_i(tr, -24, 24);
                }
            }
        }
    }

    fclose(fp);
    return 1;
}

/* Retorna 1 se o perfil foi armazenado. Somente `default.toml` define o default;
 * perfis de faixa sem bgm_id valido (>= 0) sao ignorados com aviso. */
static int ec_store_loaded_profile(const EcTrackProfile *prof, const char *filename)
{
    if (ec_strcasecmp(filename, "default.toml") == 0) {
        s_audio.default_profile = *prof;
        s_audio.default_profile.bgm_id = -1;
        s_audio.default_profile_ready = 1;
        return 1;
    }
    if (prof->bgm_id < 0) {
        fprintf(stderr, "[EC-AUDIO] aviso: perfil '%s' sem bgm_id valido (>= 0) ignorado\n", filename);
        return 0;
    }
    for (size_t i = 0; i < s_audio.profile_count; ++i) {
        if (s_audio.profiles[i].bgm_id == prof->bgm_id) {
            s_audio.profiles[i] = *prof;
            return 1;
        }
    }
    if (s_audio.profile_count < EC_MAX_PROFILES) {
        s_audio.profiles[s_audio.profile_count++] = *prof;
        return 1;
    }
    fprintf(stderr, "[EC-AUDIO] aviso: limite de %u perfis atingido; '%s' ignorado\n",
            (unsigned)EC_MAX_PROFILES, filename);
    return 0;
}

static const EcTrackProfile *ec_find_profile_for_bgm(uint32_t bgm_id)
{
    for (size_t i = 0; i < s_audio.profile_count; ++i) {
        if (s_audio.profiles[i].bgm_id == (int)bgm_id) {
            return &s_audio.profiles[i];
        }
    }
    return &s_audio.default_profile;
}

/* ---- TinySoundFont channel & synth helpers ------------------------------- */

static void ec_clear_sounding_flags(void)
{
    for (int i = 0; i < 6; ++i) {
        s_audio.chip.fm[i].sounding = 0;
    }
    for (int i = 0; i < 3; ++i) {
        s_audio.chip.psg_tone[i].sounding = 0;
    }
    s_audio.chip.psg_noise_sounding = 0;
}

static void ec_synth_reset_chip_state(void)
{
    memset(&s_audio.chip, 0, sizeof(s_audio.chip));
    for (int i = 0; i < 6; ++i) {
        for (int op = 0; op < 4; ++op) {
            s_audio.chip.fm[i].tl[op]  = 32u;
            s_audio.chip.fm[i].mul[op] = 1u;
            s_audio.chip.fm[i].ar[op]  = 31u;
            s_audio.chip.fm[i].rr[op]  = 7u;
        }
    }
    for (int i = 0; i < 3; ++i) {
        s_audio.chip.psg_tone[i].vol = 15u;
    }
    s_audio.chip.psg_noise_ctrl = 0x04u;
    s_audio.chip.psg_noise_vol  = 15u;

    s_audio.obs_addr[0] = 0u;
    s_audio.obs_addr[1] = 0u;
    s_audio.obs_addr_valid[0] = 0;   /* latch de endereco desconhecido: dados sem endereco */
    s_audio.obs_addr_valid[1] = 0;   /* (ex.: stream DAC $2A) nao viram eventos */
    s_audio.need_resync = 0;
    s_audio.ev_count = 0u;
    s_audio.frame_end_cycle = EC_DEFAULT_FRAME_CYCLES;
}

static void ec_tsf_set_bank_preset_safe(int ch, int bank, int prog)
{
    if (!s_audio.tsf) {
        return;
    }
    if (s_audio.tsf_cur_bank[ch] == bank && s_audio.tsf_cur_prog[ch] == prog) {
        return;
    }
    if (!tsf_channel_set_bank_preset(s_audio.tsf, ch, bank, prog)) {
        if (bank == 128) {
            (void)tsf_channel_set_bank_preset(s_audio.tsf, ch, 128, 0);
        } else {
            (void)tsf_channel_set_bank_preset(s_audio.tsf, ch, 0, prog);
        }
    }
    s_audio.tsf_cur_bank[ch] = bank;
    s_audio.tsf_cur_prog[ch] = prog;
}

static void ec_apply_active_profile(int resync)
{
    ec_ensure_default_profile();
    const EcTrackProfile *prof = s_audio.active_profile;
    if (!prof) {
        prof = &s_audio.default_profile;
        s_audio.active_profile = prof;
    }

    int any_solo = 0;
    for (int c = 0; c < EC_CH_COUNT; ++c) {
        const EcChanProfile *sc = &prof->channels[c];
        if (sc->solo && !sc->mute &&
            sc->mode != EC_CHAN_MODE_OFF &&
            sc->mode != EC_CHAN_MODE_CHIP) {
            any_solo = 1;
            break;
        }
    }

    if (s_audio.tsf) {
        tsf_note_off_all(s_audio.tsf);
    }
    ec_clear_sounding_flags();

    for (int c = 0; c < EC_CH_COUNT; ++c) {
        const EcChanProfile *cp = &prof->channels[c];
        int silenced = (cp->mute || (any_solo && !cp->solo) ||
                        cp->mode == EC_CHAN_MODE_CHIP ||
                        cp->mode == EC_CHAN_MODE_OFF) ? 1 : 0;
        s_audio.chan_silenced[c] = silenced;
        s_audio.chan_base_vol[c] = silenced
            ? 0.0f
            : powf(10.0f, ec_clampf(cp->volume_db, -60.0f, 24.0f) / 20.0f);

        if (!s_audio.tsf) {
            continue;
        }

        float vol_lin = s_audio.chan_base_vol[c];
        float tsf_pan = ec_clampf(0.5f + 0.5f * cp->pan, 0.0f, 1.0f);

        (void)tsf_channel_set_volume(s_audio.tsf, c, vol_lin);
        (void)tsf_channel_set_pan(s_audio.tsf, c, tsf_pan);

        s_audio.tsf_cur_bank[c] = -1;
        s_audio.tsf_cur_prog[c] = -1;

        if (cp->mode == EC_CHAN_MODE_PERCUSSION) {
            ec_tsf_set_bank_preset_safe(c, 128, cp->program);
        } else if (cp->mode == EC_CHAN_MODE_GM) {
            ec_tsf_set_bank_preset_safe(c, cp->bank, cp->program);
        } else if (cp->mode == EC_CHAN_MODE_AUTO) {
            if (c == EC_CH_DAC || c == EC_CH_PSG_NOISE) {
                ec_tsf_set_bank_preset_safe(c, 128, 0);
            } else if (c >= EC_CH_PSG0 && c <= EC_CH_PSG2) {
                ec_tsf_set_bank_preset_safe(c, 0, 80);
            } else {
                ec_tsf_set_bank_preset_safe(c, 0, 0);
            }
        } else {
            ec_tsf_set_bank_preset_safe(c, 0, 0);
        }
    }

    /* Troca de perfil: notas `keyed` no hardware voltam a soar com o novo perfil
     * (feito no proximo mix, a partir do shadow). */
    if (resync && s_audio.tsf) {
        s_audio.need_resync = 1;
    }
}

/* ---- YM2612 + SN76489 real-time translator ------------------------------- */

static const int k_ym_carrier_slots[8][4] = {
    { 3, -1, -1, -1 }, /* alg 0: OP4 */
    { 3, -1, -1, -1 }, /* alg 1: OP4 */
    { 3, -1, -1, -1 }, /* alg 2: OP4 */
    { 3, -1, -1, -1 }, /* alg 3: OP4 */
    { 2,  3, -1, -1 }, /* alg 4: OP2, OP4 */
    { 1,  2,  3, -1 }, /* alg 5: OP3, OP2, OP4 */
    { 1,  2,  3, -1 }, /* alg 6: OP3, OP2, OP4 */
    { 0,  1,  2,  3  } /* alg 7: all */
};

static const int k_ym_carrier_count[8] = { 1, 1, 1, 1, 2, 3, 3, 4 };

static int ec_ym_is_carrier(uint8_t alg, int op_slot)
{
    int a = (int)(alg & 0x07u);
    int n = k_ym_carrier_count[a];
    for (int i = 0; i < n; ++i) {
        if (k_ym_carrier_slots[a][i] == op_slot) {
            return 1;
        }
    }
    return 0;
}

static void ec_ym_analyze_patch(const EcFmChan *ch,
                                int *out_min_tl,
                                double *out_mul_factor,
                                uint8_t *out_carrier_ar,
                                uint8_t *out_carrier_rr,
                                int *out_is_noise_perc)
{
    int alg = (int)(ch->alg & 0x07u);
    int n_carriers = k_ym_carrier_count[alg];

    int min_tl = 127;
    for (int i = 0; i < n_carriers; ++i) {
        int s = k_ym_carrier_slots[alg][i];
        if ((int)ch->tl[s] < min_tl) {
            min_tl = (int)ch->tl[s];
        }
    }

    int active_slots[4];
    int n_active = 0;
    for (int i = 0; i < n_carriers; ++i) {
        int s = k_ym_carrier_slots[alg][i];
        if ((int)ch->tl[s] <= min_tl + 18 && (int)ch->tl[s] < 96) {
            active_slots[n_active++] = s;
        }
    }
    if (n_active == 0) {
        active_slots[n_active++] = k_ym_carrier_slots[alg][n_carriers - 1];
    }

    double min_mul = 16.0;
    int best_slot = active_slots[0];
    int best_tl = (int)ch->tl[best_slot];
    int has_high_carrier_mul = 0;

    for (int i = 0; i < n_active; ++i) {
        int s = active_slots[i];
        uint8_t m = ch->mul[s] & 0x0Fu;
        double mf = (m == 0u) ? 0.5 : (double)m;
        if (mf < min_mul) {
            min_mul = mf;
        }
        if (m >= 12u) {
            has_high_carrier_mul = 1;
        }
        if ((int)ch->tl[s] < best_tl) {
            best_tl = (int)ch->tl[s];
            best_slot = s;
        }
    }

    int is_perc = 0;
    if (alg <= 4 && has_high_carrier_mul) {
        int intense_mods = 0;
        for (int s = 0; s < 4; ++s) {
            if (!ec_ym_is_carrier(ch->alg, s)) {
                if ((ch->mul[s] & 0x0Fu) >= 10u && ch->tl[s] <= 15u) {
                    ++intense_mods;
                }
            }
        }
        if (intense_mods >= 2) {
            is_perc = 1;
        }
    }

    *out_min_tl = min_tl;
    *out_mul_factor = min_mul;
    *out_carrier_ar = ch->ar[best_slot];
    *out_carrier_rr = ch->rr[best_slot];
    *out_is_noise_perc = is_perc;
}

static int ec_ym_calc_raw_note(const EcFmChan *ch, double mul_factor)
{
    if (ch->fnum == 0u || mul_factor <= 0.0) {
        return -1;
    }
    uint32_t shift = (ch->block <= 7u) ? (21u - (uint32_t)ch->block) : 14u;
    double f_base = (double)ch->fnum * (7670454.0 / 144.0) / (double)(1u << shift);
    double freq = f_base * mul_factor;
    if (freq <= 0.0) {
        return -1;
    }
    return (int)llround(12.0 * log2(freq / 440.0) + 69.0);
}

static float ec_ym_tl_to_vel(int min_tl)
{
    float v = powf(10.0f, -(float)min_tl * EC_TL_DB_PER_STEP_LIN);
    return ec_clampf(v, 0.14f, 1.0f);
}

static int ec_ym_auto_preset(int note, uint8_t carrier_ar)
{
    if (note <= 48) {
        return 33; /* Finger Bass */
    }
    if (carrier_ar <= 18u) {
        return 49; /* Slow Strings */
    }
    if (note >= 67) {
        return 81; /* Saw Lead */
    }
    return 62;     /* Synth Brass 1 */
}

static int ec_psg_calc_raw_note(uint16_t period)
{
    if (period < 2u) {
        return -1;
    }
    double freq = 3579545.0 / (32.0 * (double)period);
    if (freq <= 0.0) {
        return -1;
    }
    return (int)llround(12.0 * log2(freq / 440.0) + 69.0);
}

static float ec_psg_vol_to_vel(uint8_t vol)
{
    if (vol >= 15u) {
        return 0.0f;
    }
    float v = 0.15f + ((float)(15u - vol) / 15.0f) * 0.85f;
    return ec_clampf(v, 0.15f, 1.0f);
}

static int ec_can_emit_notes(int register_only)
{
    return (s_audio.tsf != NULL && !register_only && !s_audio.paused && !s_audio.fading_out);
}

/* Ganho linear associado a uma atenuacao TL (0.75 dB/passo ~ 0.018 decadas/passo,
 * mesma curva de ec_ym_tl_to_vel, mas sem o piso 0.14: usado para fades). */
static float ec_tl_gain(int tl)
{
    if (tl >= EC_TL_MUTE) {
        return 0.0f;
    }
    if (tl < 0) {
        tl = 0;
    }
    return powf(10.0f, -(float)tl * EC_TL_DB_PER_STEP_LIN);
}

static int ec_fm_carrier_min_tl(const EcFmChan *ch)
{
    int alg = (int)(ch->alg & 0x07u);
    int min_tl = 127;
    for (int i = 0; i < k_ym_carrier_count[alg]; ++i) {
        int s = k_ym_carrier_slots[alg][i];
        if ((int)ch->tl[s] < min_tl) {
            min_tl = (int)ch->tl[s];
        }
    }
    return min_tl;
}

/* Volume do canal MIDI = volume do perfil * razao (fade do driver via TL). */
static void ec_set_chan_ratio(int tsf_ch, float ratio)
{
    if (s_audio.tsf) {
        (void)tsf_channel_set_volume(s_audio.tsf, tsf_ch, s_audio.chan_base_vol[tsf_ch] * ratio);
    }
}

/* Inicia a nota SF2 de um canal FM `keyed` a partir do shadow (key-on, retomada
 * de pausa/turbo/troca de perfil, ou TL/fnum que tornam o canal audivel). */
static void ec_fm_start_note(int g, int register_only)
{
    EcFmChan *ch = &s_audio.chip.fm[g];
    if (ch->sounding || !ch->keyed || ch->known != EC_KN_ALL) {
        return;
    }
    if (g == 5 && s_audio.chip.dac_enabled) {
        return;
    }
    if (!ec_can_emit_notes(register_only) || s_audio.chan_silenced[EC_CH_FM0 + g]) {
        return;
    }

    const EcTrackProfile *prof = s_audio.active_profile ? s_audio.active_profile : &s_audio.default_profile;
    const EcChanProfile *cp = &prof->channels[EC_CH_FM0 + g];

    int min_tl = 32;
    double mul_factor = 1.0;
    uint8_t carrier_ar = 31u;
    uint8_t carrier_rr = 7u;
    int is_noise_perc = 0;
    ec_ym_analyze_patch(ch, &min_tl, &mul_factor, &carrier_ar, &carrier_rr, &is_noise_perc);

    if (min_tl >= EC_TL_MUTE) {
        return; /* canal efetivamente mudo (< -75 dB): nao dispara nota no SF2 */
    }
    float vel = ec_ym_tl_to_vel(min_tl);

    if (is_noise_perc && cp->mode == EC_CHAN_MODE_AUTO) {
        const EcChanProfile *dac_cp = &prof->channels[EC_CH_DAC];
        if (!s_audio.chan_silenced[EC_CH_DAC]) {
            int pnote = (carrier_rr < 6u) ? 46 : 42;
            int dac_prog = (dac_cp->mode == EC_CHAN_MODE_PERCUSSION || dac_cp->mode == EC_CHAN_MODE_GM)
                ? dac_cp->program : 0;
            ec_tsf_set_bank_preset_safe(EC_CH_DAC, 128, dac_prog);
            ec_set_chan_ratio(EC_CH_DAC, 1.0f);
            (void)tsf_channel_note_on(s_audio.tsf, EC_CH_DAC, pnote, vel);
            s_audio.synth_active = 1;
            ch->sounding = 1;
            ch->active_tsf_ch = EC_CH_DAC;
            ch->active_note = pnote;
            ch->is_perc = 1;
        }
        return;
    }

    int raw_note = ec_ym_calc_raw_note(ch, mul_factor);
    if (raw_note < 12 || raw_note > 120) {
        return;
    }

    int note = raw_note;
    if (cp->mode == EC_CHAN_MODE_AUTO || cp->mode == EC_CHAN_MODE_GM) {
        note = ec_clamp_i(raw_note + cp->transpose, 0, 127);
    }

    if (cp->mode == EC_CHAN_MODE_AUTO) {
        ec_tsf_set_bank_preset_safe(EC_CH_FM0 + g, 0, ec_ym_auto_preset(note, carrier_ar));
    } else if (cp->mode == EC_CHAN_MODE_GM) {
        ec_tsf_set_bank_preset_safe(EC_CH_FM0 + g, cp->bank, cp->program);
    } else if (cp->mode == EC_CHAN_MODE_PERCUSSION) {
        ec_tsf_set_bank_preset_safe(EC_CH_FM0 + g, 128, cp->program);
    }

    ec_set_chan_ratio(EC_CH_FM0 + g, 1.0f);
    (void)tsf_channel_note_on(s_audio.tsf, EC_CH_FM0 + g, note, vel);
    s_audio.synth_active = 1;
    ch->sounding = 1;
    ch->active_tsf_ch = EC_CH_FM0 + g;
    ch->active_note = note;
    ch->is_perc = 0;
    ch->ref_gain = ec_tl_gain(min_tl);
    ch->cur_ratio = 1.0f;
}

/* Escrita de TL: fades do driver (TL dos carriers) viram volume da voz SF2. */
static void ec_fm_tl_update(int g, int register_only)
{
    EcFmChan *ch = &s_audio.chip.fm[g];
    if (!ch->keyed || !ec_can_emit_notes(register_only)) {
        return;
    }
    if (!ch->sounding) {
        ec_fm_start_note(g, register_only); /* pode ter ficado mudo por TL alto */
        return;
    }
    if (ch->is_perc || ch->ref_gain <= 0.0f) {
        return;
    }
    float ratio = ec_tl_gain(ec_fm_carrier_min_tl(ch)) / ch->ref_gain;
    ratio = ec_clampf(ratio, 0.0f, 4.0f);
    if (fabsf(ratio - ch->cur_ratio) > 0.004f) {
        ec_set_chan_ratio(ch->active_tsf_ch, ratio);
        ch->cur_ratio = ratio;
    }
}

static void ec_process_fm_event(uint8_t port, uint8_t val, int register_only)
{
    if ((port & 0x01u) == 0u) {
        int part = (port & 0x02u) ? 1 : 0;
        s_audio.chip.fm_addr[part] = val;
        return;
    }

    int part = (port & 0x02u) ? 1 : 0;
    uint8_t reg = s_audio.chip.fm_addr[part];

    if (part == 0 && reg == 0x2Bu) {
        s_audio.chip.dac_enabled = (val & 0x80u) ? 1 : 0;
        if (s_audio.chip.dac_enabled && s_audio.chip.fm[5].sounding) {
            if (s_audio.tsf) {
                tsf_channel_note_off(s_audio.tsf,
                                     s_audio.chip.fm[5].active_tsf_ch,
                                     s_audio.chip.fm[5].active_note);
            }
            s_audio.chip.fm[5].sounding = 0;
        }
        return;
    }
    if (part == 0 && reg == 0x2Au) {
        /* PCM stream ($2A): voices/SFX remain exclusively on the chip bus */
        return;
    }

    if (part == 0 && reg == 0x28u) {
        static const int k_keyon_ch[8] = { 0, 1, 2, -1, 3, 4, 5, -1 };
        int g = k_keyon_ch[val & 0x07u];
        if (g < 0) {
            return;
        }

        EcFmChan *ch = &s_audio.chip.fm[g];
        uint8_t slot_mask = (val >> 4) & 0x0Fu;

        if (ch->sounding) {
            if (s_audio.tsf) {
                tsf_channel_note_off(s_audio.tsf, ch->active_tsf_ch, ch->active_note);
            }
            ch->sounding = 0;
        }

        ch->keyed = (slot_mask != 0u) ? 1 : 0;
        if (ch->keyed) {
            ec_fm_start_note(g, register_only);
        }
        return;
    }

    if (reg >= 0xA4u && reg <= 0xA6u) {
        int g = part * 3 + (int)(reg - 0xA4u);
        s_audio.chip.fm[g].msb_latch = val;
        s_audio.chip.fm[g].known |= EC_KN_FNUM_HI;
        return;
    }

    if (reg >= 0xA0u && reg <= 0xA2u) {
        int g = part * 3 + (int)(reg - 0xA0u);
        EcFmChan *ch = &s_audio.chip.fm[g];
        ch->fnum  = (uint16_t)(((uint16_t)(ch->msb_latch & 0x07u) << 8) | (uint16_t)val);
        ch->block = (uint8_t)((ch->msb_latch >> 3) & 0x07u);
        ch->known |= EC_KN_FNUM_LO;

        const EcTrackProfile *prof = s_audio.active_profile ? s_audio.active_profile : &s_audio.default_profile;
        const EcChanProfile *cp = &prof->channels[EC_CH_FM0 + g];

        if (ch->sounding && !ch->is_perc && cp->mode != EC_CHAN_MODE_PERCUSSION &&
            !(g == 5 && s_audio.chip.dac_enabled) &&
            ec_can_emit_notes(register_only) && !s_audio.chan_silenced[EC_CH_FM0 + g]) {
            int min_tl = 32;
            double mul_factor = 1.0;
            uint8_t carrier_ar = 31u;
            uint8_t carrier_rr = 7u;
            int is_noise_perc = 0;
            ec_ym_analyze_patch(ch, &min_tl, &mul_factor, &carrier_ar, &carrier_rr, &is_noise_perc);

            int raw_note = ec_ym_calc_raw_note(ch, mul_factor);
            if (raw_note >= 12 && raw_note <= 120) {
                int new_note = (cp->mode == EC_CHAN_MODE_AUTO || cp->mode == EC_CHAN_MODE_GM)
                    ? ec_clamp_i(raw_note + cp->transpose, 0, 127)
                    : raw_note;

                if (new_note != ch->active_note && min_tl < EC_TL_MUTE) {
                    tsf_channel_note_off(s_audio.tsf, ch->active_tsf_ch, ch->active_note);
                    if (cp->mode == EC_CHAN_MODE_AUTO) {
                        int auto_prog = ec_ym_auto_preset(new_note, carrier_ar);
                        ec_tsf_set_bank_preset_safe(EC_CH_FM0 + g, 0, auto_prog);
                    }
                    float vel = ec_ym_tl_to_vel(min_tl);
                    ec_set_chan_ratio(ch->active_tsf_ch, 1.0f);
                    (void)tsf_channel_note_on(s_audio.tsf, ch->active_tsf_ch, new_note, vel);
                    s_audio.synth_active = 1;
                    ch->active_note = new_note;
                    ch->ref_gain = ec_tl_gain(min_tl);
                    ch->cur_ratio = 1.0f;
                }
            }
        } else if (ch->keyed && !ch->sounding) {
            ec_fm_start_note(g, register_only);
        }
        return;
    }

    if (reg >= 0xB0u && reg <= 0xB2u) {
        int g = part * 3 + (int)(reg - 0xB0u);
        s_audio.chip.fm[g].alg = val & 0x07u;
        s_audio.chip.fm[g].known |= EC_KN_ALG;
        return;
    }

    if ((reg & 0x03u) != 0x03u) {
        int ch_local = (int)(reg & 0x03u);
        int g = part * 3 + ch_local;
        if (reg >= 0x30u && reg <= 0x3Eu) {
            int op_slot = (int)((reg - 0x30u) >> 2);
            s_audio.chip.fm[g].mul[op_slot] = val & 0x0Fu;
        } else if (reg >= 0x40u && reg <= 0x4Eu) {
            int op_slot = (int)((reg - 0x40u) >> 2);
            s_audio.chip.fm[g].tl[op_slot] = val & 0x7Fu;
            s_audio.chip.fm[g].known |= (uint8_t)(EC_KN_TL0 << op_slot);
            ec_fm_tl_update(g, register_only);
        } else if (reg >= 0x50u && reg <= 0x5Eu) {
            int op_slot = (int)((reg - 0x50u) >> 2);
            s_audio.chip.fm[g].ar[op_slot] = val & 0x1Fu;
        } else if (reg >= 0x80u && reg <= 0x8Eu) {
            int op_slot = (int)((reg - 0x80u) >> 2);
            s_audio.chip.fm[g].rr[op_slot] = val & 0x0Fu;
        }
    }
}

/* Dispara nota de tom PSG a partir do shadow (retrigger por volume ou resync). */
static void ec_psg_tone_on(int ch_idx, uint8_t vol, int register_only)
{
    EcPsgToneChan *tch = &s_audio.chip.psg_tone[ch_idx];
    int tsf_ch = EC_CH_PSG0 + ch_idx;

    if (vol >= 15u || tch->period_known != 3u ||
        !ec_can_emit_notes(register_only) || s_audio.chan_silenced[tsf_ch]) {
        return;
    }
    int raw_note = ec_psg_calc_raw_note(tch->period);
    if (raw_note < 12 || raw_note > 120) {
        return;
    }

    const EcTrackProfile *prof = s_audio.active_profile ? s_audio.active_profile : &s_audio.default_profile;
    const EcChanProfile *cp = &prof->channels[tsf_ch];
    int note = (cp->mode == EC_CHAN_MODE_AUTO || cp->mode == EC_CHAN_MODE_GM)
        ? ec_clamp_i(raw_note + cp->transpose, 0, 127)
        : raw_note;

    if (tch->sounding) {
        tsf_channel_note_off(s_audio.tsf, tsf_ch, tch->active_note);
        tch->sounding = 0;
    }

    (void)tsf_channel_note_on(s_audio.tsf, tsf_ch, note, ec_psg_vol_to_vel(vol));
    s_audio.synth_active = 1;
    tch->sounding = 1;
    tch->active_note = note;
}

static void ec_psg_noise_on(uint8_t vol, int register_only)
{
    if (vol >= 15u || !ec_can_emit_notes(register_only) || s_audio.chan_silenced[EC_CH_PSG_NOISE]) {
        return;
    }
    if (s_audio.chip.psg_noise_sounding) {
        tsf_channel_note_off(s_audio.tsf, EC_CH_PSG_NOISE, s_audio.chip.psg_noise_note);
        s_audio.chip.psg_noise_sounding = 0;
    }

    uint8_t rate_bits = s_audio.chip.psg_noise_ctrl & 0x03u;
    int dnote = (rate_bits == 0u) ? 42 : ((rate_bits == 1u) ? 46 : 38);

    (void)tsf_channel_note_on(s_audio.tsf, EC_CH_PSG_NOISE, dnote, ec_psg_vol_to_vel(vol));
    s_audio.synth_active = 1;
    s_audio.chip.psg_noise_sounding = 1;
    s_audio.chip.psg_noise_note = dnote;
}

static void ec_psg_handle_tone_vol(int ch_idx, uint8_t prev_vol, uint8_t new_vol, int register_only)
{
    EcPsgToneChan *tch = &s_audio.chip.psg_tone[ch_idx];
    int tsf_ch = EC_CH_PSG0 + ch_idx;

    if (new_vol >= 15u) {
        if (tch->sounding) {
            if (s_audio.tsf) {
                tsf_channel_note_off(s_audio.tsf, tsf_ch, tch->active_note);
            }
            tch->sounding = 0;
        }
        return;
    }

    int jump_up = (prev_vol >= new_vol) ? ((int)(prev_vol - new_vol) >= 3) : 0;
    int retrigger = (!tch->sounding) || (prev_vol == 15u) || jump_up;
    if (!retrigger) {
        return;
    }
    ec_psg_tone_on(ch_idx, new_vol, register_only);
}

static void ec_psg_handle_noise_vol(uint8_t prev_vol, uint8_t new_vol, int register_only)
{
    if (new_vol >= 15u) {
        if (s_audio.chip.psg_noise_sounding) {
            if (s_audio.tsf) {
                tsf_channel_note_off(s_audio.tsf, EC_CH_PSG_NOISE, s_audio.chip.psg_noise_note);
            }
            s_audio.chip.psg_noise_sounding = 0;
        }
        return;
    }

    int jump_up = (prev_vol >= new_vol) ? ((int)(prev_vol - new_vol) >= 3) : 0;
    if (s_audio.chip.psg_noise_sounding && prev_vol != 15u && !jump_up) {
        return;
    }
    ec_psg_noise_on(new_vol, register_only);
}

/* Re-emite note-on para canais com key-on/volume ativo no hardware (`keyed`) que
 * nao estao soando no SF2, a partir do shadow. Usado apos pausa, turbo e troca de
 * perfil. */
static void ec_resync_keyed(void)
{
    if (!ec_can_emit_notes(0)) {
        return;
    }
    int started = 0;
    for (int g = 0; g < 6; ++g) {
        if (s_audio.chip.fm[g].keyed && !s_audio.chip.fm[g].sounding) {
            ec_fm_start_note(g, 0);
            started += s_audio.chip.fm[g].sounding;
        }
    }
    for (int i = 0; i < 3; ++i) {
        EcPsgToneChan *t = &s_audio.chip.psg_tone[i];
        if (t->vol < 15u && !t->sounding) {
            ec_psg_tone_on(i, t->vol, 0);
            started += t->sounding;
        }
    }
    if (s_audio.chip.psg_noise_vol < 15u && !s_audio.chip.psg_noise_sounding) {
        ec_psg_noise_on(s_audio.chip.psg_noise_vol, 0);
        started += s_audio.chip.psg_noise_sounding;
    }
    if (ec_trace_enabled()) {
        fprintf(stderr, "[EC-AUDIO-TRACE] resync: %d nota(s) re-emitida(s)\n", started);
    }
}

static void ec_process_psg_event(uint8_t val, int register_only)
{
    if (val & 0x80u) {
        uint8_t ch = (uint8_t)((val >> 5) & 0x03u);
        uint8_t is_vol = (uint8_t)((val >> 4) & 0x01u);
        uint8_t data4 = val & 0x0Fu;

        s_audio.chip.psg_latch_ch = ch;
        s_audio.chip.psg_latch_is_vol = is_vol;

        if (is_vol) {
            if (ch < 3u) {
                uint8_t prev = s_audio.chip.psg_tone[ch].vol;
                s_audio.chip.psg_tone[ch].vol = data4;
                ec_psg_handle_tone_vol((int)ch, prev, data4, register_only);
            } else {
                uint8_t prev = s_audio.chip.psg_noise_vol;
                s_audio.chip.psg_noise_vol = data4;
                ec_psg_handle_noise_vol(prev, data4, register_only);
            }
        } else {
            if (ch < 3u) {
                s_audio.chip.psg_tone[ch].period =
                    (uint16_t)((s_audio.chip.psg_tone[ch].period & 0x3F0u) | (uint16_t)data4);
                s_audio.chip.psg_tone[ch].period_known |= 1u;
            } else {
                s_audio.chip.psg_noise_ctrl = val & 0x07u;
            }
        }
        return;
    }

    uint8_t ch = s_audio.chip.psg_latch_ch;
    if (s_audio.chip.psg_latch_is_vol) {
        uint8_t data4 = val & 0x0Fu;
        if (ch < 3u) {
            uint8_t prev = s_audio.chip.psg_tone[ch].vol;
            s_audio.chip.psg_tone[ch].vol = data4;
            ec_psg_handle_tone_vol((int)ch, prev, data4, register_only);
        } else {
            uint8_t prev = s_audio.chip.psg_noise_vol;
            s_audio.chip.psg_noise_vol = data4;
            ec_psg_handle_noise_vol(prev, data4, register_only);
        }
        return;
    }

    if (ch < 3u) {
        EcPsgToneChan *tch = &s_audio.chip.psg_tone[ch];
        tch->period = (uint16_t)((tch->period & 0x00Fu) | ((uint16_t)(val & 0x3Fu) << 4));
        tch->period_known |= 2u;

        if (tch->vol < 15u && tch->period_known == 3u &&
            ec_can_emit_notes(register_only) && !s_audio.chan_silenced[EC_CH_PSG0 + (int)ch]) {
            int raw_note = ec_psg_calc_raw_note(tch->period);
            if (raw_note >= 12 && raw_note <= 120) {
                const EcTrackProfile *prof = s_audio.active_profile ? s_audio.active_profile : &s_audio.default_profile;
                const EcChanProfile *cp = &prof->channels[EC_CH_PSG0 + (int)ch];
                int note = (cp->mode == EC_CHAN_MODE_AUTO || cp->mode == EC_CHAN_MODE_GM)
                    ? ec_clamp_i(raw_note + cp->transpose, 0, 127)
                    : raw_note;

                if (!tch->sounding) {
                    ec_psg_tone_on((int)ch, tch->vol, register_only);
                } else if (cp->mode != EC_CHAN_MODE_PERCUSSION && tch->active_note != note) {
                    tsf_channel_note_off(s_audio.tsf, EC_CH_PSG0 + (int)ch, tch->active_note);
                    (void)tsf_channel_note_on(s_audio.tsf, EC_CH_PSG0 + (int)ch, note,
                                              ec_psg_vol_to_vel(tch->vol));
                    s_audio.synth_active = 1;
                    tch->active_note = note;
                }
            }
        }
    } else {
        s_audio.chip.psg_noise_ctrl = val & 0x07u;
    }
}

static void ec_process_chip_event(const EcChipEvent *ev, int register_only)
{
    if (ev->port == 4u) {
        ec_process_psg_event(ev->value, register_only);
    } else if (ev->port < 4u) {
        ec_process_fm_event(ev->port, ev->value, register_only);
    }
}

/* Precedencia do blend chip/SF2 (hybrid/ost):
 *   1. --audio-chip-blend (override explicito da CLI)
 *   2. chip_blend do perfil da faixa (.toml)
 *   3. chip_blend do settings.ini / EC_AUDIO_* (default do usuario)
 *   4. default do modo (hybrid 0.65, ost 0.18) */
static float ec_effective_chip_blend(void)
{
    if (s_audio.mode == EC_AUDIO_MODE_DSP || s_audio.mode == EC_AUDIO_MODE_OFF || !s_audio.tsf) {
        return 1.0f;
    }
    if (s_audio.has_chip_blend_cli) {
        return s_audio.chip_blend_cli;
    }
    if (s_audio.active_profile && s_audio.active_profile->has_chip_blend) {
        return s_audio.active_profile->chip_blend;
    }
    if (s_audio.has_chip_blend_user) {
        return s_audio.chip_blend_user;
    }
    return (s_audio.mode == EC_AUDIO_MODE_OST) ? EC_DEFAULT_OST_BLEND : EC_DEFAULT_HYBRID_BLEND;
}

/* ---- Public API ---------------------------------------------------------- */

void ec_audio_init(void)
{
    ec_ensure_default_profile();
    ec_dsp_clear_buffers(&s_audio.dsp);
    s_audio.dsp.configured_rate = 0.0;
    ec_synth_reset_chip_state();

    s_audio.synth_active = 0;
    s_audio.paused       = 0;
    s_audio.fading_out   = 0;
    s_audio.fade_env     = 0.0f;
    s_audio.pause_env    = 1.0f;
    s_audio.chip_duck    = 1.0f;

    if (s_audio.tsf) {
        ec_apply_active_profile(0);
    }
}

/* Nao ha hook de saida no GameSpec do runner: o SO libera o SoundFont no exit.
 * Esta funcao existe para testes/hosts que reinicializam o modulo. */
void ec_audio_shutdown(void)
{
    if (s_audio.tsf) {
        tsf_close(s_audio.tsf);
        s_audio.tsf = NULL;
    }
    ec_dsp_clear_buffers(&s_audio.dsp);
    s_audio.dsp.configured_rate = 0.0;
    ec_synth_reset_chip_state();

    s_audio.mode                  = EC_AUDIO_MODE_OFF;
    s_audio.bgm_vol               = 1.0f;
    s_audio.sfx_vol               = 1.0f;
    s_audio.chip_blend_user       = 0.0f;
    s_audio.has_chip_blend_user   = 0;
    s_audio.chip_blend_cli        = 0.0f;
    s_audio.has_chip_blend_cli    = 0;
    s_audio.dsp_reverb            = EC_DEFAULT_DSP_REVERB;
    s_audio.profile_count         = 0u;
    s_audio.default_profile_ready = 0;
    s_audio.active_profile        = NULL;
    s_audio.current_bgm_id        = 0u;
    s_audio.has_bgm_id            = 0;
    s_audio.synth_active          = 0;
    s_audio.paused                = 0;
    s_audio.fading_out            = 0;
    s_audio.fade_env              = 0.0f;
    s_audio.pause_env             = 1.0f;
    s_audio.chip_duck             = 1.0f;
}

/* Reset completo (power-on / reset do console): descarta a musica atual com
 * rampa (fade-out) em vez de corte seco. */
void ec_audio_reset(void)
{
    ec_ensure_default_profile();
    ec_synth_reset_chip_state();   /* shadow -> desconhecido */

    s_audio.current_bgm_id = 0u;
    s_audio.has_bgm_id     = 0;
    s_audio.active_profile = &s_audio.default_profile;
    s_audio.paused         = 0;
    if (s_audio.synth_active && s_audio.fade_env > 0.0f) {
        s_audio.fading_out = 1;    /* rampa EC_FADE_OUT_SEC no mix callback */
    } else {
        s_audio.synth_active = 0;
        s_audio.fading_out   = 0;
        s_audio.fade_env     = 0.0f;
    }

    if (s_audio.tsf) {
        ec_apply_active_profile(0);   /* note_off_all (release natural) + canais */
    }
}

/* Load de savestate/rewind: o estado de hardware e substituido, mas a musica do
 * estado carregado costuma continuar. Preserva bgm_id/perfil/envelopes; o runner
 * nao expoe leitura dos registradores do YM2612/PSG, entao o shadow vira
 * "desconhecido" e as notas so voltam depois que o driver reescrever patch (alg+TL)
 * e frequencia de cada canal (e novo key-on). */
void ec_audio_on_state_loaded(void)
{
    if (ec_trace_enabled()) {
        fprintf(stderr, "[EC-AUDIO-TRACE] state loaded (bgm_id=%u has=%d)\n",
                (unsigned)s_audio.current_bgm_id, s_audio.has_bgm_id);
    }
    ec_ensure_default_profile();
    ec_synth_reset_chip_state();
    if (s_audio.tsf) {
        tsf_note_off_all(s_audio.tsf);   /* release natural, sem corte seco */
    }
    s_audio.paused     = 0;
    s_audio.fading_out = 0;
}

int ec_audio_set_mode_str(const char *mode_str)
{
    if (!mode_str || !*mode_str) {
        return 0;
    }

    EcAudioMode next;
    if (ec_strcasecmp(mode_str, "off") == 0) {
        next = EC_AUDIO_MODE_OFF;
    } else if (ec_strcasecmp(mode_str, "dsp") == 0) {
        next = EC_AUDIO_MODE_DSP;
    } else if (ec_strcasecmp(mode_str, "hybrid") == 0) {
        next = EC_AUDIO_MODE_HYBRID;
    } else if (ec_strcasecmp(mode_str, "ost") == 0) {
        next = EC_AUDIO_MODE_OST;
    } else {
        return 0;
    }

    EcAudioMode prev = s_audio.mode;
    s_audio.mode = next;

    if ((next == EC_AUDIO_MODE_OFF || next == EC_AUDIO_MODE_DSP) &&
        (prev == EC_AUDIO_MODE_HYBRID || prev == EC_AUDIO_MODE_OST)) {
        s_audio.ev_count = 0u;
        if (s_audio.tsf) {
            tsf_note_off_all(s_audio.tsf);
        }
        ec_clear_sounding_flags();
    } else if ((next == EC_AUDIO_MODE_HYBRID || next == EC_AUDIO_MODE_OST) &&
               (prev == EC_AUDIO_MODE_OFF || prev == EC_AUDIO_MODE_DSP)) {
        /* Em off/dsp o observer nao registra nada: shadow desatualizado. */
        ec_synth_reset_chip_state();
        ec_ensure_default_profile();
        s_audio.active_profile = s_audio.has_bgm_id
            ? ec_find_profile_for_bgm(s_audio.current_bgm_id)
            : &s_audio.default_profile;
        if (s_audio.tsf) {
            ec_apply_active_profile(0);
        }
    }
    return 1;
}

EcAudioMode ec_audio_get_mode(void)
{
    return s_audio.mode;
}

int ec_audio_load_soundfont(const char *sf2_path)
{
    if (!sf2_path || !*sf2_path) {
        return 0;
    }

    tsf *loaded = tsf_load_filename(sf2_path);
    if (!loaded) {
        return 0;
    }

    tsf_set_output(loaded, TSF_STEREO_INTERLEAVED, EC_SF2_SAMPLE_RATE, 0.0f);
    if (!tsf_set_max_voices(loaded, EC_SF2_MAX_VOICES)) {
        tsf_close(loaded);
        return 0;
    }

    /* Pre-initialize channels 0..10 to avoid allocations during gameplay */
    for (int ch = 0; ch < EC_CH_COUNT; ++ch) {
        (void)tsf_channel_set_volume(loaded, ch, 1.0f);
        (void)tsf_channel_set_pan(loaded, ch, 0.5f);
        (void)tsf_channel_set_bank_preset(loaded, ch, 0, 0);
    }

    if (s_audio.tsf) {
        tsf_close(s_audio.tsf);
    }
    s_audio.tsf = loaded;
    ec_synth_reset_chip_state();   /* sem tsf o observer nao registrava: shadow desconhecido */

    ec_ensure_default_profile();
    s_audio.active_profile = s_audio.has_bgm_id
        ? ec_find_profile_for_bgm(s_audio.current_bgm_id)
        : &s_audio.default_profile;
    ec_apply_active_profile(0);
    return 1;
}

int ec_audio_load_profiles_dir(const char *profiles_dir)
{
    if (!profiles_dir || !*profiles_dir) {
        return -1;
    }

    ec_init_builtin_default_profile(&s_audio.default_profile);
    s_audio.default_profile_ready = 1;
    s_audio.profile_count = 0u;
    s_audio.active_profile = &s_audio.default_profile;

    int loaded_count = 0;

#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(profiles_dir);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return -1;
    }

    char pattern[EC_AUDIO_PATH_MAX];
    if (!ec_join_path(pattern, sizeof(pattern), profiles_dir, "*.toml")) {
        return -1;
    }

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            return 0;
        }
        return -1;
    }

    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        if (!ec_ends_with_ci(fd.cFileName, ".toml")) {
            continue;
        }
        char full_path[EC_AUDIO_PATH_MAX];
        if (!ec_join_path(full_path, sizeof(full_path), profiles_dir, fd.cFileName)) {
            continue;
        }
        EcTrackProfile prof;
        if (ec_parse_profile_toml(full_path, fd.cFileName, &prof)) {
            if (ec_store_loaded_profile(&prof, fd.cFileName)) {
                ++loaded_count;
            }
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
#else
    DIR *dir = opendir(profiles_dir);
    if (!dir) {
        return -1;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        if (!ec_ends_with_ci(ent->d_name, ".toml")) {
            continue;
        }
        char full_path[EC_AUDIO_PATH_MAX];
        if (!ec_join_path(full_path, sizeof(full_path), profiles_dir, ent->d_name)) {
            continue;
        }
        EcTrackProfile prof;
        if (ec_parse_profile_toml(full_path, ent->d_name, &prof)) {
            if (ec_store_loaded_profile(&prof, ent->d_name)) {
                ++loaded_count;
            }
        }
    }

    closedir(dir);
#endif

    s_audio.active_profile = s_audio.has_bgm_id
        ? ec_find_profile_for_bgm(s_audio.current_bgm_id)
        : &s_audio.default_profile;
    if (s_audio.tsf) {
        ec_apply_active_profile(1);
    }

    return loaded_count;
}

static float ec_set_clamped(const char *name, float v, float lo, float hi)
{
    float c = ec_clampf(v, lo, hi);
    if (c != v) {
        fprintf(stderr, "[EC-AUDIO] aviso: %s=%g fora de [%g, %g]; ajustado para %g\n",
                name, (double)v, (double)lo, (double)hi, (double)c);
    }
    return c;
}

void ec_audio_set_bgm_vol(float vol)
{
    if (!isfinite(vol)) {
        return;
    }
    s_audio.bgm_vol = ec_set_clamped("bgm_vol", vol, 0.0f, 2.0f);
}

void ec_audio_set_sfx_vol(float vol)
{
    if (!isfinite(vol)) {
        return;
    }
    s_audio.sfx_vol = ec_set_clamped("sfx_vol", vol, 0.0f, 2.0f);
}

/* Default do usuario (settings.ini / env): so vale se o perfil nao definir chip_blend. */
void ec_audio_set_chip_blend(float blend)
{
    if (!isfinite(blend)) {
        return;
    }
    s_audio.chip_blend_user     = ec_set_clamped("chip_blend", blend, 0.0f, 1.0f);
    s_audio.has_chip_blend_user = 1;
}

/* Override explicito (--audio-chip-blend): vence perfil e defaults. */
void ec_audio_set_chip_blend_override(float blend)
{
    if (!isfinite(blend)) {
        return;
    }
    s_audio.chip_blend_cli     = ec_set_clamped("--audio-chip-blend", blend, 0.0f, 1.0f);
    s_audio.has_chip_blend_cli = 1;
}

void ec_audio_set_dsp_reverb(float amount)
{
    if (!isfinite(amount)) {
        return;
    }
    s_audio.dsp_reverb = ec_set_clamped("dsp_reverb", amount, 0.0f, 1.0f);
}

void ec_audio_on_bgm_play(uint32_t bgm_id)
{
    if (ec_trace_enabled()) {
        fprintf(stderr, "[EC-AUDIO-TRACE] bgm_play id=%u\n", (unsigned)bgm_id);
    }
    ec_ensure_default_profile();
    s_audio.current_bgm_id = bgm_id;
    s_audio.has_bgm_id     = 1;
    s_audio.paused         = 0;
    s_audio.fading_out     = 0;
    s_audio.synth_active   = 1;

    s_audio.active_profile = ec_find_profile_for_bgm(bgm_id);
    if (s_audio.tsf &&
        (s_audio.mode == EC_AUDIO_MODE_HYBRID || s_audio.mode == EC_AUDIO_MODE_OST)) {
        /* Musica nova: o driver reescreve os canais; nao ressuscitar notas da anterior. */
        ec_apply_active_profile(0);
    }
}

void ec_audio_on_bgm_pause(int paused)
{
    int next_paused = paused ? 1 : 0;
    if (ec_trace_enabled() && next_paused != s_audio.paused) {
        fprintf(stderr, "[EC-AUDIO-TRACE] pause=%d\n", next_paused);
    }
    if (next_paused && !s_audio.paused && s_audio.tsf) {
        tsf_note_off_all(s_audio.tsf);
        ec_clear_sounding_flags();
    }
    if (!next_paused && s_audio.paused) {
        s_audio.need_resync = 1;   /* canais ainda em key-on voltam a soar */
    }
    s_audio.paused = next_paused;
}

void ec_audio_on_bgm_stop(int fade)
{
    if (s_audio.tsf) {
        tsf_note_off_all(s_audio.tsf);
    }
    ec_clear_sounding_flags();

    if (fade && s_audio.synth_active && s_audio.fade_env > 0.0f) {
        s_audio.fading_out = 1;
        s_audio.paused     = 0;
    } else {
        s_audio.synth_active   = 0;
        s_audio.fading_out     = 0;
        s_audio.paused         = 0;
        s_audio.fade_env       = 0.0f;
        s_audio.current_bgm_id = 0u;
        s_audio.has_bgm_id     = 0;
    }
}

uint32_t ec_audio_current_bgm_id(void)
{
    return s_audio.has_bgm_id ? s_audio.current_bgm_id : 0u;
}

void ec_audio_on_chip_write(uint8_t port, uint8_t value,
                            uint32_t cycle_stamp, uint32_t frame_end_cycle)
{
    if (s_audio.mode == EC_AUDIO_MODE_OFF ||
        s_audio.mode == EC_AUDIO_MODE_DSP ||
        !s_audio.tsf) {
        return;
    }

    if (port == 0xFFu) {
        if (s_audio.ev_count > 0u) {
            for (size_t i = 0u; i < s_audio.ev_count; ++i) {
                ec_process_chip_event(&s_audio.events[i], 1);
            }
            tsf_note_off_all(s_audio.tsf);
            ec_clear_sounding_flags();
            s_audio.ev_count = 0u;
            s_audio.need_resync = 1;   /* turbo: re-emitir `keyed` no proximo mix */
        }
        s_audio.frame_end_cycle = frame_end_cycle;
        return;
    }

    if (port == 0u) {
        s_audio.obs_addr[0] = value;
        s_audio.obs_addr_valid[0] = 1;
        if (value == 0x2Au) {
            /* Discard $2A address writes so DAC PCM bursts do not consume event slots */
            return;
        }
    } else if (port == 2u) {
        s_audio.obs_addr[1] = value;
        s_audio.obs_addr_valid[1] = 1;
    } else if (port == 1u && (!s_audio.obs_addr_valid[0] || s_audio.obs_addr[0] == 0x2Au)) {
        /* Discard DAC PCM stream bytes (and data with unknown address latch after
         * reset/state load) so voices/SFX never become register events */
        return;
    } else if (port == 3u && !s_audio.obs_addr_valid[1]) {
        return;
    }

    if (s_audio.ev_count >= EC_EVENT_CAP) {
        for (size_t i = 0u; i < s_audio.ev_count; ++i) {
            ec_process_chip_event(&s_audio.events[i], 1);
        }
        s_audio.ev_count = 0u;
        s_audio.need_resync = 1;
    }

    {
        EcChipEvent *ev = &s_audio.events[s_audio.ev_count++];
        ev->cycle_stamp = cycle_stamp;
        ev->port        = port;
        ev->value       = value;
    }
    s_audio.frame_end_cycle = frame_end_cycle;
}

/* ---- Real-time AudioExtraSource callback --------------------------------- */

void ec_audio_mix_callback(int16_t *mix_stereo, size_t frames, double rate)
{
    if (s_audio.mode == EC_AUDIO_MODE_OFF) {
        return;
    }
    if (!mix_stereo || frames == 0 || !isfinite(rate) || rate <= 0.0) {
        return;
    }

    EcDspState *dsp = &s_audio.dsp;
    if (fabs(rate - dsp->configured_rate) > 1.0) {
        ec_dsp_configure(dsp, rate);
    }

    const int   use_reverb   = (s_audio.dsp_reverb > 0.0001f);
    const float rev_wet      = s_audio.dsp_reverb * 0.45f;
    const int   rev_decim    = dsp->rev_decim;
    const float inv_decim    = 1.0f / (float)rev_decim;
    const float stereo_w     = 1.12f;
    const float sfx_vol      = s_audio.sfx_vol;
    const float bgm_vol      = s_audio.bgm_vol;
    const float blend_min    = ec_effective_chip_blend();

    const float fade_in_inc  = ec_clampf((float)(1.0 / (EC_FADE_IN_SEC  * rate)), 0.0f, 1.0f);
    const float fade_out_dec = ec_clampf((float)(1.0 / (EC_FADE_OUT_SEC * rate)), 0.0f, 1.0f);
    const float pause_inc    = ec_clampf((float)(1.0 / (EC_PAUSE_RAMP_SEC * rate)), 0.0f, 1.0f);
    const float duck_inc     = ec_clampf((float)(1.0 / (EC_DUCK_RAMP_SEC * rate)), 0.0f, 1.0f);

    const int use_sf2 = (s_audio.mode == EC_AUDIO_MODE_HYBRID ||
                         s_audio.mode == EC_AUDIO_MODE_OST) &&
                        (s_audio.tsf != NULL);

    float sf2_buf[EC_SF2_BUF_MAX * 2u];
    size_t sf2_frames = 0u;
    float sf2_master_gain = 1.0f;

    if (use_sf2) {
        ec_ensure_default_profile();
        const EcTrackProfile *prof = s_audio.active_profile ? s_audio.active_profile : &s_audio.default_profile;
        sf2_master_gain = powf(10.0f, prof->master_gain_db / 20.0f);

        if (s_audio.need_resync && ec_can_emit_notes(0)) {
            s_audio.need_resync = 0;
            ec_resync_keyed();
        }

        long long raw_sf2 = llround((double)frames * ((double)EC_SF2_SAMPLE_RATE / rate));
        if (raw_sf2 < 1) {
            raw_sf2 = 1;
        } else if (raw_sf2 > (long long)EC_SF2_BUF_MAX) {
            raw_sf2 = (long long)EC_SF2_BUF_MAX;
        }
        sf2_frames = (size_t)raw_sf2;

        uint32_t frame_cycles = s_audio.frame_end_cycle ? s_audio.frame_end_cycle : EC_DEFAULT_FRAME_CYCLES;
        size_t sf2_pos = 0u;

        for (size_t ei = 0u; ei < s_audio.ev_count; ++ei) {
            const EcChipEvent *ev = &s_audio.events[ei];
            double cyc_frac = (double)ev->cycle_stamp / (double)frame_cycles;
            if (cyc_frac < 0.0) cyc_frac = 0.0;
            if (cyc_frac > 1.0) cyc_frac = 1.0;

            long long tgt_ll = llround(cyc_frac * (double)sf2_frames);
            size_t target = (tgt_ll < 0) ? 0u : (size_t)tgt_ll;
            if (target < sf2_pos) target = sf2_pos;
            if (target > sf2_frames) target = sf2_frames;

            if (target > sf2_pos) {
                tsf_render_float(s_audio.tsf, sf2_buf + sf2_pos * 2u, (int)(target - sf2_pos), 0);
                sf2_pos = target;
            }
            ec_process_chip_event(ev, 0);
        }

        if (sf2_pos < sf2_frames) {
            tsf_render_float(s_audio.tsf, sf2_buf + sf2_pos * 2u, (int)(sf2_frames - sf2_pos), 0);
        }
        s_audio.ev_count = 0u;
    }

    const double sf2_step = use_sf2 ? ((double)sf2_frames / (double)frames) : 0.0;
    const float sf2_weight = 1.0f - blend_min;
    const float sf2_scale = EC_SF2_MAKEUP_GAIN * sf2_weight * sf2_master_gain * bgm_vol;
    const float mix_denom = sqrtf(blend_min * blend_min + sf2_weight * sf2_weight);
    const float eq_power_target = (mix_denom > 0.001f) ? (1.0f / mix_denom) : 1.0f;

    for (size_t i = 0; i < frames; ++i) {
        /* 1. Chip input [-1.0, 1.0] */
        double cl = (double)mix_stereo[i * 2u + 0u] * (1.0 / 32768.0);
        double cr = (double)mix_stereo[i * 2u + 1u] * (1.0 / 32768.0);

        /* 2. Studio 3-band EQ (double precision for stability at 223.7 kHz) */
        cl = ec_biquad_tick(&dsp->eq_low,  0, cl);
        cr = ec_biquad_tick(&dsp->eq_low,  1, cr);
        cl = ec_biquad_tick(&dsp->eq_mid,  0, cl);
        cr = ec_biquad_tick(&dsp->eq_mid,  1, cr);
        cl = ec_biquad_tick(&dsp->eq_high, 0, cl);
        cr = ec_biquad_tick(&dsp->eq_high, 1, cr);

        /* 3. Subtle Mid/Side stereo widening */
        float fl = (float)cl;
        float fr = (float)cr;
        float mid  = 0.5f * (fl + fr);
        float side = 0.5f * (fl - fr) * stereo_w;
        float chip_l = mid + side;
        float chip_r = mid - side;

        /* 4. Real-time SF2 stream + envelopes */
        float sf2_l = 0.0f;
        float sf2_r = 0.0f;
        float sf2_presence = 0.0f;

        if (use_sf2) {
            if (s_audio.fading_out) {
                s_audio.fade_env -= fade_out_dec;
                if (s_audio.fade_env <= 0.0f) {
                    s_audio.fade_env       = 0.0f;
                    s_audio.fading_out     = 0;
                    s_audio.synth_active   = 0;
                    s_audio.current_bgm_id = 0u;
                    s_audio.has_bgm_id     = 0;
                }
            } else if (s_audio.synth_active && s_audio.fade_env < 1.0f) {
                s_audio.fade_env += fade_in_inc;
                if (s_audio.fade_env > 1.0f) {
                    s_audio.fade_env = 1.0f;
                }
            }

            if (s_audio.paused) {
                if (s_audio.pause_env > 0.0f) {
                    s_audio.pause_env -= pause_inc;
                    if (s_audio.pause_env < 0.0f) {
                        s_audio.pause_env = 0.0f;
                    }
                }
            } else if (s_audio.pause_env < 1.0f) {
                s_audio.pause_env += pause_inc;
                if (s_audio.pause_env > 1.0f) {
                    s_audio.pause_env = 1.0f;
                }
            }

            if (s_audio.synth_active || s_audio.fading_out) {
                sf2_presence = s_audio.fade_env * s_audio.pause_env;
                if (sf2_presence > 0.0f) {
                    double spos = (double)i * sf2_step;
                    size_t idx0 = (size_t)spos;
                    if (idx0 >= sf2_frames) {
                        idx0 = sf2_frames - 1u;
                    }
                    size_t idx1 = (idx0 + 1u < sf2_frames) ? (idx0 + 1u) : idx0;
                    float frac = (float)(spos - (double)idx0);

                    float l0 = sf2_buf[idx0 * 2u + 0u];
                    float r0 = sf2_buf[idx0 * 2u + 1u];
                    float l1 = sf2_buf[idx1 * 2u + 0u];
                    float r1 = sf2_buf[idx1 * 2u + 1u];

                    float amp = sf2_presence * sf2_scale;
                    sf2_l = (l0 + (l1 - l0) * frac) * amp;
                    sf2_r = (r0 + (r1 - r0) * frac) * amp;
                }
            }
        }

        if (!isfinite(sf2_l)) sf2_l = 0.0f;
        if (!isfinite(sf2_r)) sf2_r = 0.0f;

        /* 5. Smooth chip ducking envelope (1.0 when SF2 inactive -> blend_min when active) */
        float target_duck = 1.0f - (1.0f - blend_min) * sf2_presence;
        if (s_audio.chip_duck < target_duck) {
            s_audio.chip_duck += duck_inc;
            if (s_audio.chip_duck > target_duck) {
                s_audio.chip_duck = target_duck;
            }
        } else if (s_audio.chip_duck > target_duck) {
            s_audio.chip_duck -= duck_inc;
            if (s_audio.chip_duck < target_duck) {
                s_audio.chip_duck = target_duck;
            }
        }

        float chip_gain = s_audio.chip_duck * sfx_vol;
        float eq_power = 1.0f + sf2_presence * (eq_power_target - 1.0f);
        float out_l = chip_l * (chip_gain * EC_CHIP_DSP_GAIN * eq_power) + sf2_l * eq_power;
        float out_r = chip_r * (chip_gain * EC_CHIP_DSP_GAIN * eq_power) + sf2_r * eq_power;

        /* 6. Ultra-light Schroeder room reverb (applied to combined chip + SF2 bus) */
        const float dry_l = out_l;
        const float dry_r = out_r;
        if (use_reverb) {
            if (rev_decim == 1) {
                float wl = 0.0f, wr = 0.0f;
                ec_reverb_tick_stereo(dsp, out_l, out_r, &wl, &wr);
                out_l += wl * rev_wet;
                out_r += wr * rev_wet;
            } else {
                dsp->rev_acc_l += out_l;
                dsp->rev_acc_r += out_r;
                if (++dsp->rev_phase >= rev_decim) {
                    dsp->rev_phase = 0;
                    dsp->rev_prev_l = dsp->rev_curr_l;
                    dsp->rev_prev_r = dsp->rev_curr_r;
                    ec_reverb_tick_stereo(dsp,
                                          dsp->rev_acc_l * inv_decim,
                                          dsp->rev_acc_r * inv_decim,
                                          &dsp->rev_curr_l,
                                          &dsp->rev_curr_r);
                    dsp->rev_acc_l = 0.0f;
                    dsp->rev_acc_r = 0.0f;
                }
                float t = (float)dsp->rev_phase * inv_decim;
                float wl = dsp->rev_prev_l + (dsp->rev_curr_l - dsp->rev_prev_l) * t;
                float wr = dsp->rev_prev_r + (dsp->rev_curr_r - dsp->rev_prev_r) * t;
                out_l += wl * rev_wet;
                out_r += wr * rev_wet;
            }
        }

        /* Saida nao finita (NaN/Inf do SF2/reverb/EQ): limpa estado e usa o sinal seco */
        if (!isfinite(out_l) || !isfinite(out_r)) {
            ec_dsp_clear_buffers(dsp);
            out_l = isfinite(dry_l) ? dry_l : 0.0f;
            out_r = isfinite(dry_r) ? dry_r : 0.0f;
        }

        /* 7. Soft-limiter + S16 clamp */
        mix_stereo[i * 2u + 0u] = ec_float_to_s16(out_l);
        mix_stereo[i * 2u + 1u] = ec_float_to_s16(out_r);
    }

    ec_dsp_flush_denormals(dsp);
}
