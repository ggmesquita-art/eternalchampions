#ifndef EC_BPLUS_GL_H
#define EC_BPLUS_GL_H

#include <stddef.h>
#include <stdint.h>
#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EcBplusGl EcBplusGl;

typedef struct EcBplusMetrics {
    uint64_t frames;
    uint64_t fallback_frames;
    uint64_t timed_samples;
    double   last_gpu_ms;
    double   p50_gpu_ms;
    double   p99_gpu_ms;
    double   mean_gpu_ms;
    unsigned input_width, input_height;
    unsigned output_width, output_height;
    unsigned scale;
    int      time_elapsed_available;
} EcBplusMetrics;

typedef enum EcBplusDeditherMode {
    EC_BPLUS_DEDITHER_OFF  = 0,
    EC_BPLUS_DEDITHER_CBOD = 1, /* 2x2 checkerboard de-dither (pseudo-transparency & gradients) */
    EC_BPLUS_DEDITHER_BOTH = 2, /* 2x2 checkerboard + vertical-line SGENPT-MIX de-dither */
} EcBplusDeditherMode;

EcBplusGl   *ec_bplus_gl_create(SDL_Renderer *renderer, SDL_Texture *activation,
                                const char *weights_path, double budget_ms);
void         ec_bplus_gl_destroy(EcBplusGl *ctx);
/* Effect intensity: scales the neural residual (delta) added over the
 * bilinear base. 1.0 = exact model output (default); 0.0 = pure bilinear;
 * >1.0 exaggerates the neural detail. Clamped to [0, 4]. */
void         ec_bplus_gl_set_intensity(EcBplusGl *ctx, float intensity);
/* Pre-upscale GPU de-dither pass on the native frame before the neural CNN
 * and bilinear residual base. Default is EC_BPLUS_DEDITHER_OFF. */
void         ec_bplus_gl_set_dedither(EcBplusGl *ctx, EcBplusDeditherMode mode);
/* Runs 4x ECStudentESPCN4x on GPU and returns borrowed SDL_Texture* (ARGB8888 target),
 * or NULL on fallback (invalid state, budget overrun, or unavailable GL 4.3+). */
SDL_Texture *ec_bplus_gl_process(EcBplusGl *ctx, const uint32_t *argb,
                                 int width, int height, int stride_px);
int          ec_bplus_gl_readback(EcBplusGl *ctx, void *argb, size_t pitch_bytes);
void         ec_bplus_gl_metrics(const EcBplusGl *ctx, EcBplusMetrics *out);

#ifdef __cplusplus
}
#endif
#endif /* EC_BPLUS_GL_H */
