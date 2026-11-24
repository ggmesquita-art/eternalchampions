#define TSF_STATIC
#define TSF_IMPLEMENTATION

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include "../../game/third_party/tsf.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <windows.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#define TSF_BRIDGE_API __declspec(dllexport)
#else
#include <pthread.h>
#define TSF_BRIDGE_API __attribute__((visibility("default")))
#endif

#define TSF_BRIDGE_RENDER_BLOCK 512
#define TSF_BRIDGE_GAIN         0.55f
#define TSF_BRIDGE_CLIP_THRESH  0.95f

typedef struct {
    double start_sec;
    double end_sec;
    int32_t note;      /* 0..127 */
    int32_t velocity;  /* 1..127 */
} TsfMidiNote;

typedef struct {
    tsf *font;
#if defined(_WIN32)
    CRITICAL_SECTION lock;
#else
    pthread_mutex_t lock;
#endif
} TsfBridgeHandle;

typedef struct {
    int32_t sample_pos;
    uint32_t seq;
    uint8_t is_on;   /* 0 = note_off, 1 = note_on */
    uint8_t key;
    uint8_t vel;
    int16_t preset;
} TsfBridgeEvent;

static int32_t ec_clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static int32_t ec_auto_preset(int32_t key, double dur_sec)
{
    if (key <= 48) {
        return 33; /* Electric Bass Finger */
    }
    if (key >= 67) {
        return 81; /* Lead 2 Sawtooth */
    }
    if (dur_sec >= 0.28) {
        return 49; /* Slow Strings */
    }
    return 62;     /* Synth Brass 1 */
}

static int16_t ec_gain_and_soft_clip_s16(float sample)
{
    float x = sample * TSF_BRIDGE_GAIN;
    if (!isfinite(x)) {
        return 0;
    }
    float ax = fabsf(x);
    if (ax > TSF_BRIDGE_CLIP_THRESH) {
        const float head = 1.0f - TSF_BRIDGE_CLIP_THRESH;
        float over = (ax - TSF_BRIDGE_CLIP_THRESH) / head;
        float limited = TSF_BRIDGE_CLIP_THRESH + head * tanhf(over);
        x = (x >= 0.0f) ? limited : -limited;
    }
    float s = x * 32767.0f;
    if (s >= 32767.0f) {
        return 32767;
    }
    if (s <= -32768.0f) {
        return -32768;
    }
    return (int16_t)(s >= 0.0f ? (s + 0.5f) : (s - 0.5f));
}

static int ec_event_cmp(const void *pa, const void *pb)
{
    const TsfBridgeEvent *a = (const TsfBridgeEvent *)pa;
    const TsfBridgeEvent *b = (const TsfBridgeEvent *)pb;
    if (a->sample_pos < b->sample_pos) {
        return -1;
    }
    if (a->sample_pos > b->sample_pos) {
        return 1;
    }
    if (a->is_on < b->is_on) {
        return -1;
    }
    if (a->is_on > b->is_on) {
        return 1;
    }
    if (a->seq < b->seq) {
        return -1;
    }
    if (a->seq > b->seq) {
        return 1;
    }
    return 0;
}

static void ec_bridge_lock(TsfBridgeHandle *h)
{
#if defined(_WIN32)
    EnterCriticalSection(&h->lock);
#else
    pthread_mutex_lock(&h->lock);
#endif
}

static void ec_bridge_unlock(TsfBridgeHandle *h)
{
#if defined(_WIN32)
    LeaveCriticalSection(&h->lock);
#else
    pthread_mutex_unlock(&h->lock);
#endif
}

TSF_BRIDGE_API void *tsf_bridge_open(const char *sf2_path)
{
    if (!sf2_path || sf2_path[0] == '\0') {
        return NULL;
    }

    tsf *font = tsf_load_filename(sf2_path);
    if (!font) {
        return NULL;
    }
    if (!tsf_set_max_voices(font, 96)) {
        tsf_close(font);
        return NULL;
    }

    TsfBridgeHandle *h = (TsfBridgeHandle *)calloc(1u, sizeof(TsfBridgeHandle));
    if (!h) {
        tsf_close(font);
        return NULL;
    }
    h->font = font;

#if defined(_WIN32)
    InitializeCriticalSection(&h->lock);
#else
    if (pthread_mutex_init(&h->lock, NULL) != 0) {
        tsf_close(font);
        free(h);
        return NULL;
    }
#endif
    return (void *)h;
}

TSF_BRIDGE_API void tsf_bridge_close(void *handle)
{
    TsfBridgeHandle *h = (TsfBridgeHandle *)handle;
    if (!h) {
        return;
    }
    ec_bridge_lock(h);
    if (h->font) {
        tsf_close(h->font);
        h->font = NULL;
    }
    ec_bridge_unlock(h);
#if defined(_WIN32)
    DeleteCriticalSection(&h->lock);
#else
    pthread_mutex_destroy(&h->lock);
#endif
    free(h);
}

TSF_BRIDGE_API int tsf_bridge_render_channel(
    void *handle,
    const TsfMidiNote *notes,
    int32_t note_count,
    int32_t mode,
    int32_t bank,
    int32_t program,
    int32_t transpose,
    int32_t sample_rate,
    int32_t total_frames,
    int16_t *out_stereo_s16)
{
    TsfBridgeHandle *h = (TsfBridgeHandle *)handle;
    if (!h || !h->font || !out_stereo_s16) {
        return -1;
    }
    if (sample_rate <= 0 || total_frames <= 0) {
        return -1;
    }
    if ((size_t)total_frames > SIZE_MAX / (2u * sizeof(int16_t))) {
        return -1;
    }
    if (note_count < 0 || (note_count > 0 && !notes)) {
        return -1;
    }
    if (mode < 0 || mode > 2) {
        return -1;
    }

    if (note_count == 0) {
        memset(out_stereo_s16, 0, (size_t)total_frames * 2u * sizeof(int16_t));
        return (int)total_frames;
    }

    if ((size_t)note_count > SIZE_MAX / (2u * sizeof(TsfBridgeEvent))) {
        return -1;
    }

    TsfBridgeEvent *events = (TsfBridgeEvent *)malloc((size_t)note_count * 2u * sizeof(TsfBridgeEvent));
    if (!events) {
        return -1;
    }

    const double max_sec = (double)total_frames / (double)sample_rate;
    const double clamp_hi = max_sec + 1.0;

    size_t ev_count = 0;
    uint32_t seq = 0;
    for (int32_t i = 0; i < note_count; ++i) {
        double start_sec = notes[i].start_sec;
        double end_sec = notes[i].end_sec;
        if (!isfinite(start_sec) || !isfinite(end_sec)) {
            continue;
        }
        if (end_sec <= start_sec) {
            continue;
        }
        double dur_sec = end_sec - start_sec;

        if (start_sec < 0.0) {
            start_sec = 0.0;
        } else if (start_sec > clamp_hi) {
            start_sec = clamp_hi;
        }
        if (end_sec < 0.0) {
            end_sec = 0.0;
        } else if (end_sec > clamp_hi) {
            end_sec = clamp_hi;
        }

        int32_t key = notes[i].note + ((mode == 1) ? 0 : transpose);
        key = ec_clamp_i32(key, 0, 127);
        int32_t vel = ec_clamp_i32(notes[i].velocity, 1, 127);

        int64_t on_pos = (int64_t)llround(start_sec * (double)sample_rate);
        int64_t off_pos = (int64_t)llround(end_sec * (double)sample_rate);
        if (off_pos <= 0 || on_pos >= (int64_t)total_frames) {
            continue;
        }
        if (on_pos < 0) {
            on_pos = 0;
        }
        if (off_pos <= on_pos) {
            off_pos = on_pos + 1;
        }

        int16_t preset = (mode == 2)
            ? (int16_t)ec_auto_preset(key, dur_sec)
            : (int16_t)ec_clamp_i32(program, 0, 127);

        events[ev_count].sample_pos = (int32_t)on_pos;
        events[ev_count].seq = seq++;
        events[ev_count].is_on = 1u;
        events[ev_count].key = (uint8_t)key;
        events[ev_count].vel = (uint8_t)vel;
        events[ev_count].preset = preset;
        ev_count++;

        if (off_pos < (int64_t)total_frames) {
            events[ev_count].sample_pos = (int32_t)off_pos;
            events[ev_count].seq = seq++;
            events[ev_count].is_on = 0u;
            events[ev_count].key = (uint8_t)key;
            events[ev_count].vel = 0u;
            events[ev_count].preset = preset;
            ev_count++;
        }
    }

    if (ev_count > 1u) {
        qsort(events, ev_count, sizeof(TsfBridgeEvent), ec_event_cmp);
    }

    ec_bridge_lock(h);
    tsf *inst = tsf_copy(h->font);
    ec_bridge_unlock(h);

    if (!inst) {
        free(events);
        return -1;
    }

    tsf_set_output(inst, TSF_STEREO_INTERLEAVED, sample_rate, 0.0f);
    if (!tsf_set_max_voices(inst, 64)) {
        ec_bridge_lock(h);
        tsf_close(inst);
        ec_bridge_unlock(h);
        free(events);
        return -1;
    }

    int32_t clamped_bank = ec_clamp_i32(bank, 0, 128);
    int32_t clamped_prog = ec_clamp_i32(program, 0, 127);
    int32_t cur_auto_preset = -1;

    if (mode == 1) {
        if (!tsf_channel_set_bank_preset(inst, 0, 128, clamped_prog)) {
            (void)tsf_channel_set_bank_preset(inst, 0, 128, 0);
        }
    } else if (mode == 0) {
        if (!tsf_channel_set_bank_preset(inst, 0, clamped_bank, clamped_prog)) {
            (void)tsf_channel_set_bank_preset(inst, 0, 0, clamped_prog);
        }
    } else {
        (void)tsf_channel_set_bank_preset(inst, 0, 0, 33);
        cur_auto_preset = 33;
    }

    float tmp_buf[TSF_BRIDGE_RENDER_BLOCK * 2];
    int32_t frame_pos = 0;
    size_t ev_idx = 0;

    while (frame_pos < total_frames) {
        while (ev_idx < ev_count && events[ev_idx].sample_pos <= frame_pos) {
            const TsfBridgeEvent *ev = &events[ev_idx];
            if (ev->is_on == 0u) {
                tsf_channel_note_off(inst, 0, (int)ev->key);
            } else {
                if (mode == 2 && cur_auto_preset != (int32_t)ev->preset) {
                    if (!tsf_channel_set_bank_preset(inst, 0, 0, (int)ev->preset)) {
                        (void)tsf_channel_set_bank_preset(inst, 0, 0, 0);
                    }
                    cur_auto_preset = (int32_t)ev->preset;
                }
                float vel_f = (float)ev->vel / 127.0f;
                (void)tsf_channel_note_on(inst, 0, (int)ev->key, vel_f);
            }
            ev_idx++;
        }

        int32_t next_pos = (ev_idx < ev_count) ? events[ev_idx].sample_pos : total_frames;
        if (next_pos > total_frames) {
            next_pos = total_frames;
        }
        int32_t remain = next_pos - frame_pos;
        while (remain > 0) {
            int32_t chunk = (remain > TSF_BRIDGE_RENDER_BLOCK) ? TSF_BRIDGE_RENDER_BLOCK : remain;
            tsf_render_float(inst, tmp_buf, (int)chunk, 0);

            int16_t *dst = out_stereo_s16 + ((size_t)frame_pos * 2u);
            const size_t n_samples = (size_t)chunk * 2u;
            for (size_t k = 0; k < n_samples; ++k) {
                dst[k] = ec_gain_and_soft_clip_s16(tmp_buf[k]);
            }

            frame_pos += chunk;
            remain -= chunk;
        }
    }

    ec_bridge_lock(h);
    tsf_close(inst);
    ec_bridge_unlock(h);
    free(events);

    return (int)total_frames;
}
