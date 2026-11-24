#ifndef EC_BPLUS_ORT_H
#define EC_BPLUS_ORT_H

/*
 * ec_bplus_ort.h — ONNX Runtime (CUDA Execution Provider) backend for Mode B+.
 *
 * dlopen-based: no link-time dependency on onnxruntime. ec_bplus_ort_create()
 * returns NULL when the runtime/library/model/CUDA stack is unavailable, and
 * the caller keeps the OpenGL compute backend (ec_bplus_gl) as fallback.
 *
 * Intensity: this backend runs the exported model, whose output is the FINAL
 * image (bilinear base + residual already composed inside the graph).
 * Therefore only intensity == 1.0 is supported here; the caller routes any
 * other intensity to the GL backend, which performs the exact composite.
 *
 * Host-presentation only: never alters guest WRAM, VDP state, snapshots or
 * determinism.
 */

#include <stddef.h>
#include <stdint.h>
#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EcBplusOrt EcBplusOrt;

typedef struct EcBplusOrtMetrics {
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
} EcBplusOrtMetrics;

EcBplusOrt  *ec_bplus_ort_create(SDL_Renderer *renderer, SDL_Texture *activation,
                                 const char *model_path, double budget_ms);
void         ec_bplus_ort_destroy(EcBplusOrt *ctx);
/* Stored for interface parity; only 1.0 is honored (see header note). */
void         ec_bplus_ort_set_intensity(EcBplusOrt *ctx, float intensity);
/* Runs ECStudentESPCN4x on CUDA (via ONNX Runtime) and returns a borrowed
 * ARGB8888 streaming SDL_Texture at 4x, or NULL on fallback (invalid state,
 * budget overrun, or runtime failure). */
SDL_Texture *ec_bplus_ort_process(EcBplusOrt *ctx, const uint32_t *argb,
                                  int width, int height, int stride_px);
/* Copies the last composed frame (already on CPU) into *argb. */
int          ec_bplus_ort_readback(EcBplusOrt *ctx, void *argb, size_t pitch_bytes);
void         ec_bplus_ort_metrics(const EcBplusOrt *ctx, EcBplusOrtMetrics *out);

#ifdef __cplusplus
}
#endif
#endif /* EC_BPLUS_ORT_H */
