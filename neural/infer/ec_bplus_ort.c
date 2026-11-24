/*
 * ec_bplus_ort.c — ONNX Runtime (CUDA) backend for Mode B+ (ECStudentESPCN4x, P3/N3).
 *
 * Loads libonnxruntime via dlopen (no link-time dependency), creates a session
 * with the CUDA Execution Provider, and runs the exported ESPCN model
 * (assets/models/ec_bplus_espcn4x.onnx, opset 17, input "input_rgba" NCHW
 * float32, output 4x NCHW float32). The graph output is the FINAL image
 * (bilinear base + residual composed inside the network, mirroring
 * train_espcn.py's forward), so this backend supports intensity == 1.0
 * exactly; the caller routes other intensities to the GL backend.
 *
 * Host-presentation only: never alters guest WRAM, VDP state, snapshots or
 * determinism.
 */

#include "ec_bplus_ort.h"
#include "ort/onnxruntime_c_api.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dirent.h>
#  include <dlfcn.h>
#endif

/* ---- dynamic-library loading: dlopen (POSIX) vs LoadLibrary (Win32) ----- */

#ifdef _WIN32
static void *ort_dlopen_any(const char *path) { return (void *)LoadLibraryA(path); }
static void *ort_dlsym_any(void *h, const char *name) { return (void *)GetProcAddress((HMODULE)h, name); }
static void  ort_dlclose_any(void *h) { FreeLibrary((HMODULE)h); }
static const char *ort_dlerror_str(void) { return "LoadLibrary/GetProcAddress failed"; }
#else
static void *ort_dlopen_any(const char *path) { return dlopen(path, RTLD_NOW | RTLD_GLOBAL); }
static void *ort_dlsym_any(void *h, const char *name) { return dlsym(h, name); }
static void  ort_dlclose_any(void *h) { dlclose(h); }
static const char *ort_dlerror_str(void) { return dlerror(); }
#endif

#define BPLUS_ORT_MAX_SAMPLES 4096u
#define BPLUS_ORT_WARMUP_FRAMES 10u

struct EcBplusOrt {
    SDL_Renderer *renderer;
    SDL_Texture  *output;          /* ARGB8888 streaming, 4x */
    void         *lib;             /* dlopen handle */
    const OrtApi *api;
    OrtEnv       *env;
    OrtSessionOptions *opts;
    OrtSession   *session;
    OrtMemoryInfo *cpu_mem;
    char         *in_name;         /* OrtAllocator-owned */
    char         *out_name;
    ONNXTensorElementDataType in_type;
    uint32_t     *argb_out;        /* CPU final frame, out_w*out_h */
    int          in_w, in_h, out_w, out_h;
    float        intensity;
    double       budget_ms;
    EcBplusOrtMetrics metrics;
    double       samples[BPLUS_ORT_MAX_SAMPLES];
    unsigned     n_samples;
};

static void ort_record_sample(EcBplusOrt *ctx, double ms)
{
    if (ctx->n_samples < BPLUS_ORT_MAX_SAMPLES)
        ctx->samples[ctx->n_samples++] = ms;
    else
        ctx->samples[BPLUS_ORT_MAX_SAMPLES - 1u] = ms; /* keep newest */
    ctx->metrics.last_gpu_ms = ms;
    ctx->metrics.timed_samples = ctx->n_samples;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void ort_compute_percentiles(EcBplusOrt *ctx)
{
    if (ctx->n_samples == 0u)
        return;
    double sorted[BPLUS_ORT_MAX_SAMPLES];
    memcpy(sorted, ctx->samples, ctx->n_samples * sizeof(double));
    qsort(sorted, ctx->n_samples, sizeof(double), cmp_double);
    ctx->metrics.p50_gpu_ms = sorted[ctx->n_samples / 2u];
    ctx->metrics.p99_gpu_ms = sorted[(ctx->n_samples * 99u) / 100u];
    double sum = 0.0;
    for (unsigned i = 0; i < ctx->n_samples; ++i)
        sum += sorted[i];
    ctx->metrics.mean_gpu_ms = sum / (double)ctx->n_samples;
}

static const char *ort_status_msg(const OrtApi *api, OrtStatus *status)
{
    return status ? api->GetErrorMessage(status) : "unknown ORT error";
}

static void ort_release_status(const OrtApi *api, OrtStatus *status)
{
    if (status)
        api->ReleaseStatus(status);
}

/* Preloads a library by path; best-effort (failures are silent — the
 * dynamic linker may still resolve them via LD_LIBRARY_PATH later). */
static void ort_preload_one(const char *path)
{
    (void)ort_dlopen_any(path);
}

/* The WSL driver (Windows 616.56) calls the host-side ptxjitcompiler; the
 * Ubuntu-packaged one (580.x family) segfaults with it. Pre-loading the
 * driver's own copy makes the driver resolve the right one even without
 * LD_LIBRARY_PATH (mirrors tools/scripts/cuda-env.sh). POSIX/WSL-only: on
 * native Windows the ONNX Runtime GPU bundle resolves its own CUDA DLLs. */
static void ort_preload_driver_ptx(void)
{
#ifndef _WIN32
    DIR *d = opendir("/usr/lib/wsl/drivers");
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "nvmdi.inf_amd64_", 16) != 0)
            continue;
        char path[640];
        snprintf(path, sizeof(path), "/usr/lib/wsl/drivers/%s/libnvidia-ptxjitcompiler.so.1",
                 e->d_name);
        FILE *fp = fopen(path, "rb");
        if (fp) {
            fclose(fp);
            ort_preload_one(path);
            break;
        }
    }
    closedir(d);
#endif
}

/* CUDA 13 runtime libs from the pip wheels, resolved relative to the
 * onnxruntime lib (<site>/onnxruntime/capi/libonnxruntime.so -> <site>).
 * Pre-loading satisfies the provider's NEEDED entries so the game works
 * without LD_LIBRARY_PATH. */
static void ort_preload_cuda_deps(const char *ort_lib_path)
{
#ifndef _WIN32
    if (!ort_lib_path || !strchr(ort_lib_path, '/'))
        return; /* bare dlopen name: no way to locate the wheel dirs */
    char base[1024];
    snprintf(base, sizeof(base), "%s", ort_lib_path);
    char *slash = strrchr(base, '/');
    if (!slash)
        return;
    *slash = '\0'; /* .../capi */
    slash = strrchr(base, '/');
    if (!slash)
        return;
    *slash = '\0'; /* .../onnxruntime */
    slash = strrchr(base, '/');
    if (!slash)
        return;
    *slash = '\0'; /* <site-packages> */

    static const char *cu13_libs[] = {
        "libcudart.so.13", "libcublas.so.13", "libcublasLt.so.13", "libcurand.so.10",
    };
    char path[1400];
    for (size_t i = 0; i < sizeof(cu13_libs) / sizeof(cu13_libs[0]); ++i) {
        snprintf(path, sizeof(path), "%s/nvidia/cu13/lib/%s", base, cu13_libs[i]);
        ort_preload_one(path);
    }
    snprintf(path, sizeof(path), "%s/nvidia/cudnn/lib/libcudnn.so.9", base);
    ort_preload_one(path);
#endif
}

/* Resolves libonnxruntime.so: EC_ONNXRUNTIME_LIB > system names > project
 * venv-relative candidates (game may run from repo root or from build dirs). */
static void *ort_dlopen_library(void)
{
    const char *env = getenv("EC_ONNXRUNTIME_LIB");
    if (env && *env) {
        void *h = ort_dlopen_any(env);
        if (h) {
#ifndef _WIN32
            ort_preload_driver_ptx();
            ort_preload_cuda_deps(env);
#endif
            return h;
        }
        fprintf(stderr, "[BPLUS_ORT] EC_ONNXRUNTIME_LIB '%s' failed: %s\n",
                env, ort_dlerror_str());
    }
#ifdef _WIN32
    static const char *names[] = {
        "onnxruntime.dll",
        "onnxruntime.dll",
    };
#else
    static const char *names[] = {
        "libonnxruntime.so.1.32.0",
        "libonnxruntime.so.1",
        "libonnxruntime.so",
    };
#endif
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        void *h = ort_dlopen_any(names[i]);
        if (h) {
#ifndef _WIN32
            ort_preload_driver_ptx();
#endif
            return h;
        }
    }
#ifndef _WIN32
    static const char *rel[] = {
        ".venv/lib/python3.12/site-packages/onnxruntime/capi/libonnxruntime.so.1.32.0",
        "../.venv/lib/python3.12/site-packages/onnxruntime/capi/libonnxruntime.so.1.32.0",
        "../../.venv/lib/python3.12/site-packages/onnxruntime/capi/libonnxruntime.so.1.32.0",
        "../../../.venv/lib/python3.12/site-packages/onnxruntime/capi/libonnxruntime.so.1.32.0",
    };
    for (size_t i = 0; i < sizeof(rel) / sizeof(rel[0]); ++i) {
        FILE *fp = fopen(rel[i], "rb");
        if (fp) {
            fclose(fp);
            void *h = ort_dlopen_any(rel[i]);
            if (h) {
                ort_preload_driver_ptx();
                ort_preload_cuda_deps(rel[i]);
                return h;
            }
        }
    }
#endif
    return NULL;
}

EcBplusOrt *ec_bplus_ort_create(SDL_Renderer *renderer, SDL_Texture *activation,
                                const char *model_path, double budget_ms)
{
    (void)activation;
    if (!renderer) {
        fprintf(stderr, "[BPLUS_ORT] requires an SDL renderer\n");
        return NULL;
    }
    if (!model_path || !*model_path) {
        fprintf(stderr, "[BPLUS_ORT] no model path\n");
        return NULL;
    }

    FILE *fp = fopen(model_path, "rb");
    if (!fp) {
        fprintf(stderr, "[BPLUS_ORT] model not found: %s\n", model_path);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long mlen = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (mlen <= 0 || mlen > 512L * 1024L * 1024L) {
        fprintf(stderr, "[BPLUS_ORT] implausible model size: %ld\n", mlen);
        fclose(fp);
        return NULL;
    }
    unsigned char *model = (unsigned char *)malloc((size_t)mlen);
    if (!model || fread(model, 1, (size_t)mlen, fp) != (size_t)mlen) {
        fprintf(stderr, "[BPLUS_ORT] failed to read model\n");
        free(model);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    EcBplusOrt *ctx = (EcBplusOrt *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        free(model);
        return NULL;
    }
    ctx->renderer = renderer;
    ctx->budget_ms = budget_ms;
    ctx->intensity = 1.0f;

    ctx->lib = ort_dlopen_library();
    if (!ctx->lib) {
        fprintf(stderr, "[BPLUS_ORT] libonnxruntime not found (set EC_ONNXRUNTIME_LIB); "
                        "retaining GL fallback\n");
        free(model);
        free(ctx);
        return NULL;
    }

    const OrtApiBase *(*get_api_base)(void) =
        (const OrtApiBase *(*)(void))ort_dlsym_any(ctx->lib, "OrtGetApiBase");
    if (!get_api_base) {
        fprintf(stderr, "[BPLUS_ORT] OrtGetApiBase missing: %s\n", ort_dlerror_str());
        goto fail_lib;
    }
    const OrtApiBase *base = get_api_base();
    ctx->api = base ? base->GetApi(ORT_API_VERSION) : NULL;
    if (!ctx->api) {
        fprintf(stderr, "[BPLUS_ORT] ORT API version mismatch\n");
        goto fail_lib;
    }

    OrtStatus *st = ctx->api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "ec_bplus_ort", &ctx->env);
    if (st) {
        fprintf(stderr, "[BPLUS_ORT] CreateEnv: %s\n", ort_status_msg(ctx->api, st));
        ort_release_status(ctx->api, st);
        goto fail_lib;
    }
    st = ctx->api->CreateSessionOptions(&ctx->opts);
    if (st) {
        fprintf(stderr, "[BPLUS_ORT] CreateSessionOptions: %s\n", ort_status_msg(ctx->api, st));
        ort_release_status(ctx->api, st);
        goto fail_env;
    }
    {
        OrtCUDAProviderOptions cu_opts;
        memset(&cu_opts, 0, sizeof(cu_opts));
        cu_opts.device_id = 0;
        cu_opts.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchExhaustive;
        cu_opts.gpu_mem_limit = SIZE_MAX;
        cu_opts.do_copy_in_default_stream = 1;
        st = ctx->api->SessionOptionsAppendExecutionProvider_CUDA(ctx->opts, &cu_opts);
    }
    if (st) {
        fprintf(stderr, "[BPLUS_ORT] CUDA provider unavailable: %s\n"
                        "  (missing CUDA 13 runtime libs or no LD_LIBRARY_PATH — see tools/scripts/cuda-env.sh); "
                        "retaining GL fallback\n", ort_status_msg(ctx->api, st));
        ort_release_status(ctx->api, st);
        goto fail_opts;
    }
    st = ctx->api->CreateSessionFromArray(ctx->env, model, (size_t)mlen,
                                          ctx->opts, &ctx->session);
    if (st) {
        fprintf(stderr, "[BPLUS_ORT] CreateSessionFromArray: %s\n", ort_status_msg(ctx->api, st));
        ort_release_status(ctx->api, st);
        goto fail_opts;
    }
    free(model);
    model = NULL;

    OrtAllocator *alloc = NULL;
    st = ctx->api->GetAllocatorWithDefaultOptions(&alloc);
    if (st || !alloc) {
        fprintf(stderr, "[BPLUS_ORT] allocator unavailable\n");
        ort_release_status(ctx->api, st);
        goto fail_session;
    }
    st = ctx->api->SessionGetInputName(ctx->session, 0, alloc, &ctx->in_name);
    if (st || !ctx->in_name) {
        fprintf(stderr, "[BPLUS_ORT] no input: %s\n", ort_status_msg(ctx->api, st));
        ort_release_status(ctx->api, st);
        goto fail_session;
    }
    st = ctx->api->SessionGetOutputName(ctx->session, 0, alloc, &ctx->out_name);
    if (st || !ctx->out_name) {
        fprintf(stderr, "[BPLUS_ORT] no output: %s\n", ort_status_msg(ctx->api, st));
        ort_release_status(ctx->api, st);
        goto fail_session;
    }
    OrtTypeInfo *ti = NULL;
    st = ctx->api->SessionGetInputTypeInfo(ctx->session, 0, &ti);
    if (st || !ti) {
        fprintf(stderr, "[BPLUS_ORT] no input type info\n");
        ort_release_status(ctx->api, st);
        goto fail_session;
    }
    const OrtTensorTypeAndShapeInfo *tsi = NULL;
    {
        OrtStatus *cst = ctx->api->CastTypeInfoToTensorInfo(ti, &tsi);
        if (cst || !tsi) {
            fprintf(stderr, "[BPLUS_ORT] CastTypeInfoToTensorInfo: %s\n",
                    ort_status_msg(ctx->api, cst));
            ort_release_status(ctx->api, cst);
            ctx->api->ReleaseTypeInfo(ti);
            goto fail_session;
        }
    }
    {
        ONNXTensorElementDataType t = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
        OrtStatus *tst = ctx->api->GetTensorElementType(tsi, &t);
        if (tst) {
            fprintf(stderr, "[BPLUS_ORT] GetTensorElementType: %s\n", ort_status_msg(ctx->api, tst));
            ort_release_status(ctx->api, tst);
            ctx->api->ReleaseTypeInfo(ti);
            goto fail_session;
        }
        ctx->in_type = t;
    }
    ctx->api->ReleaseTypeInfo(ti);
    if (ctx->in_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
        ctx->in_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
        fprintf(stderr, "[BPLUS_ORT] unsupported input type %d (only float32/float16)\n",
                (int)ctx->in_type);
        goto fail_session;
    }

    st = ctx->api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &ctx->cpu_mem);
    if (st) {
        fprintf(stderr, "[BPLUS_ORT] CreateCpuMemoryInfo: %s\n", ort_status_msg(ctx->api, st));
        ort_release_status(ctx->api, st);
        goto fail_session;
    }

    fprintf(stderr, "[BPLUS_ORT] ONNX Runtime CUDA backend ready (model %s, in=%s out=%s, budget=%.2f ms)\n",
            model_path, ctx->in_name, ctx->out_name, budget_ms);
    return ctx;

fail_session:
    ctx->api->ReleaseSession(ctx->session);
    ctx->session = NULL;
fail_opts:
    ctx->api->ReleaseSessionOptions(ctx->opts);
    ctx->opts = NULL;
fail_env:
    ctx->api->ReleaseEnv(ctx->env);
    ctx->env = NULL;
fail_lib:
    ort_dlclose_any(ctx->lib);
    free(model);
    free(ctx);
    return NULL;
}

void ec_bplus_ort_destroy(EcBplusOrt *ctx)
{
    if (!ctx)
        return;
    if (ctx->output)
        SDL_DestroyTexture(ctx->output);
    if (ctx->cpu_mem && ctx->api)
        ctx->api->ReleaseMemoryInfo(ctx->cpu_mem);
    if (ctx->session && ctx->api)
        ctx->api->ReleaseSession(ctx->session);
    if (ctx->opts && ctx->api)
        ctx->api->ReleaseSessionOptions(ctx->opts);
    if (ctx->env && ctx->api)
        ctx->api->ReleaseEnv(ctx->env);
    if (ctx->lib)
        ort_dlclose_any(ctx->lib);
    free(ctx->argb_out);
    free(ctx);
}

void ec_bplus_ort_set_intensity(EcBplusOrt *ctx, float intensity)
{
    if (!ctx)
        return;
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 4.0f) intensity = 4.0f;
    ctx->intensity = intensity;
    if (intensity != 1.0f)
        fprintf(stderr, "[BPLUS_ORT] warning: intensity %.3f != 1.0 is ignored "
                        "(ORT runs the full model output); use the GL backend for composite control\n",
                intensity);
}

/* ---- IEEE 754 half <-> float (software, no libm dependency) ---------------- */

static uint16_t float_to_half(float f)
{
    uint32_t x;
    memcpy(&x, &f, sizeof(x));
    uint32_t sign = (x >> 16) & 0x8000u;
    uint32_t abs = x & 0x7FFFFFFFu;
    if (abs >= 0x7F800000u) { /* inf / nan */
        uint32_t nan_tail = (abs > 0x7F800000u) ? 0x200u : 0u;
        return (uint16_t)(sign | 0x7C00u | nan_tail);
    }
    int exp = (int)((x >> 23) & 0xFFu) - 127 + 15;
    if (exp >= 0x1F)
        return (uint16_t)(sign | 0x7C00u); /* overflow -> inf */
    if (exp <= 0) {
        if (exp < -10)
            return (uint16_t)sign; /* underflow -> 0 */
        abs = (abs | 0x800000u) >> (uint32_t)(1 - exp);
        uint32_t m = abs >> 13;
        m += ((abs >> 12) & 1u) & (uint32_t)((abs & 0x1FFFu) != 0u || (m & 1u));
        return (uint16_t)(sign | m);
    }
    uint32_t m = x >> 13;
    m += (x >> 12) & 1u;
    return (uint16_t)(sign | ((uint32_t)exp << 10) | (m & 0x3FFu));
}

static float half_to_float(uint16_t h)
{
    uint32_t sign = ((uint32_t)h & 0x8000u) << 16;
    uint32_t exp = ((uint32_t)h >> 10) & 0x1Fu;
    uint32_t man = (uint32_t)h & 0x3FFu;
    uint32_t out;
    if (exp == 0u) {
        if (man == 0u) {
            out = sign;
        } else {
            uint32_t e = 127u - 15u + 1u;
            while (!(man & 0x400u)) {
                man <<= 1;
                --e;
            }
            man &= 0x3FFu;
            out = sign | (e << 23) | (man << 13);
        }
    } else if (exp == 0x1Fu) {
        out = sign | 0x7F800000u | (man << 13);
    } else {
        out = sign | ((exp - 15u + 127u) << 23) | (man << 13);
    }
    float f;
    memcpy(&f, &out, sizeof(f));
    return f;
}

static void ort_upload_rgba16(const uint32_t *argb, int width, int height,
                              int stride_px, uint16_t *planar)
{
    const size_t n = (size_t)width * (size_t)height;
    uint16_t *r = planar;
    uint16_t *g = planar + n;
    uint16_t *b = planar + 2u * n;
    uint16_t *a = planar + 3u * n;
    const float inv255 = 1.0f / 255.0f;
    for (int y = 0; y < height; ++y) {
        const uint32_t *row = argb + (size_t)y * (size_t)stride_px;
        size_t o = (size_t)y * (size_t)width;
        for (int x = 0; x < width; ++x) {
            uint32_t px = row[x];
            r[o] = float_to_half((float)((px >> 16) & 0xFFu) * inv255);
            g[o] = float_to_half((float)((px >> 8) & 0xFFu) * inv255);
            b[o] = float_to_half((float)(px & 0xFFu) * inv255);
            a[o] = float_to_half((float)(px >> 24) * inv255);
            ++o;
        }
    }
}

static void ort_download_rgba16(const uint16_t *restrict planar, int width,
                                int height, uint32_t *restrict argb)
{
    /* half -> u8 via lookup table (branchless; 64 KB, fica quente em L2). */
    static uint8_t s_lut[65536];
    static int s_lut_ready = 0;
    if (!s_lut_ready) {
        for (uint32_t h = 0; h < 65536u; ++h) {
            uint32_t v = (uint32_t)(half_to_float((uint16_t)h) * 255.0f + 0.5f);
            s_lut[h] = (uint8_t)(v > 255u ? 255u : v);
        }
        s_lut_ready = 1;
    }
    const size_t n = (size_t)width * (size_t)height;
    const uint16_t *r = planar;
    const uint16_t *g = planar + n;
    const uint16_t *b = planar + 2u * n;
    const uint16_t *a = planar + 3u * n;
    for (int y = 0; y < height; ++y) {
        size_t o = (size_t)y * (size_t)width;
        for (int x = 0; x < width; ++x) {
            uint32_t rr = s_lut[r[o]];
            uint32_t gg = s_lut[g[o]];
            uint32_t bb = s_lut[b[o]];
            uint32_t aa = s_lut[a[o]];
            argb[o] = (aa << 24) | (rr << 16) | (gg << 8) | bb;
            ++o;
        }
    }
}

static void ort_upload_rgba(const uint32_t *argb, int width, int height,
                            int stride_px, float *planar)
{
    /* ARGB8888 (little-endian B,G,R,A bytes) -> planar float RGBA in [0,1], NCHW.
     * Branch-free, strength-reduced: processes 4 pixels per inner iteration. */
    const size_t n = (size_t)width * (size_t)height;
    float *r = planar;
    float *g = planar + n;
    float *b = planar + 2u * n;
    float *a = planar + 3u * n;
    const float inv255 = 1.0f / 255.0f;
    const int w4 = width & ~3;
    for (int y = 0; y < height; ++y) {
        const uint32_t *row = argb + (size_t)y * (size_t)stride_px;
        size_t o = (size_t)y * (size_t)width;
        int x = 0;
        for (; x < w4; x += 4) {
            uint32_t p0 = row[x], p1 = row[x + 1], p2 = row[x + 2], p3 = row[x + 3];
            r[o] = (float)((p0 >> 16) & 0xFFu) * inv255;
            g[o] = (float)((p0 >> 8) & 0xFFu) * inv255;
            b[o] = (float)(p0 & 0xFFu) * inv255;
            a[o] = (float)(p0 >> 24) * inv255; ++o;
            r[o] = (float)((p1 >> 16) & 0xFFu) * inv255;
            g[o] = (float)((p1 >> 8) & 0xFFu) * inv255;
            b[o] = (float)(p1 & 0xFFu) * inv255;
            a[o] = (float)(p1 >> 24) * inv255; ++o;
            r[o] = (float)((p2 >> 16) & 0xFFu) * inv255;
            g[o] = (float)((p2 >> 8) & 0xFFu) * inv255;
            b[o] = (float)(p2 & 0xFFu) * inv255;
            a[o] = (float)(p2 >> 24) * inv255; ++o;
            r[o] = (float)((p3 >> 16) & 0xFFu) * inv255;
            g[o] = (float)((p3 >> 8) & 0xFFu) * inv255;
            b[o] = (float)(p3 & 0xFFu) * inv255;
            a[o] = (float)(p3 >> 24) * inv255; ++o;
        }
        for (; x < width; ++x) {
            uint32_t px = row[x];
            r[o] = (float)((px >> 16) & 0xFFu) * inv255;
            g[o] = (float)((px >> 8) & 0xFFu) * inv255;
            b[o] = (float)(px & 0xFFu) * inv255;
            a[o] = (float)(px >> 24) * inv255;
            ++o;
        }
    }
}

static void ort_download_rgba(const float *restrict planar, int width, int height,
                              uint32_t *restrict argb)
{
    /* Planar float RGBA in [0,1] -> ARGB8888, branch-free clamp via min.
     * 4 pixels per inner iteration. */
    const size_t n = (size_t)width * (size_t)height;
    const float *r = planar;
    const float *g = planar + n;
    const float *b = planar + 2u * n;
    const float *a = planar + 3u * n;
    const int w4 = width & ~3;
    for (int y = 0; y < height; ++y) {
        size_t o = (size_t)y * (size_t)width;
        int x = 0;
        for (; x < w4; x += 4) {
            /* per-pixel: min(round(f*255), 255) */
            uint32_t rr0 = (uint32_t)(r[o] * 255.0f + 0.5f);
            uint32_t gg0 = (uint32_t)(g[o] * 255.0f + 0.5f);
            uint32_t bb0 = (uint32_t)(b[o] * 255.0f + 0.5f);
            uint32_t aa0 = (uint32_t)(a[o] * 255.0f + 0.5f);
            rr0 = rr0 > 255u ? 255u : rr0; gg0 = gg0 > 255u ? 255u : gg0;
            bb0 = bb0 > 255u ? 255u : bb0; aa0 = aa0 > 255u ? 255u : aa0;
            argb[o++] = (aa0 << 24) | (rr0 << 16) | (gg0 << 8) | bb0;
            uint32_t rr1 = (uint32_t)(r[o] * 255.0f + 0.5f);
            uint32_t gg1 = (uint32_t)(g[o] * 255.0f + 0.5f);
            uint32_t bb1 = (uint32_t)(b[o] * 255.0f + 0.5f);
            uint32_t aa1 = (uint32_t)(a[o] * 255.0f + 0.5f);
            rr1 = rr1 > 255u ? 255u : rr1; gg1 = gg1 > 255u ? 255u : gg1;
            bb1 = bb1 > 255u ? 255u : bb1; aa1 = aa1 > 255u ? 255u : aa1;
            argb[o++] = (aa1 << 24) | (rr1 << 16) | (gg1 << 8) | bb1;
            uint32_t rr2 = (uint32_t)(r[o] * 255.0f + 0.5f);
            uint32_t gg2 = (uint32_t)(g[o] * 255.0f + 0.5f);
            uint32_t bb2 = (uint32_t)(b[o] * 255.0f + 0.5f);
            uint32_t aa2 = (uint32_t)(a[o] * 255.0f + 0.5f);
            rr2 = rr2 > 255u ? 255u : rr2; gg2 = gg2 > 255u ? 255u : gg2;
            bb2 = bb2 > 255u ? 255u : bb2; aa2 = aa2 > 255u ? 255u : aa2;
            argb[o++] = (aa2 << 24) | (rr2 << 16) | (gg2 << 8) | bb2;
            uint32_t rr3 = (uint32_t)(r[o] * 255.0f + 0.5f);
            uint32_t gg3 = (uint32_t)(g[o] * 255.0f + 0.5f);
            uint32_t bb3 = (uint32_t)(b[o] * 255.0f + 0.5f);
            uint32_t aa3 = (uint32_t)(a[o] * 255.0f + 0.5f);
            rr3 = rr3 > 255u ? 255u : rr3; gg3 = gg3 > 255u ? 255u : gg3;
            bb3 = bb3 > 255u ? 255u : bb3; aa3 = aa3 > 255u ? 255u : aa3;
            argb[o++] = (aa3 << 24) | (rr3 << 16) | (gg3 << 8) | bb3;
        }
        for (; x < width; ++x) {
            uint32_t rr = (uint32_t)(r[o] * 255.0f + 0.5f);
            uint32_t gg = (uint32_t)(g[o] * 255.0f + 0.5f);
            uint32_t bb = (uint32_t)(b[o] * 255.0f + 0.5f);
            uint32_t aa = (uint32_t)(a[o] * 255.0f + 0.5f);
            rr = rr > 255u ? 255u : rr; gg = gg > 255u ? 255u : gg;
            bb = bb > 255u ? 255u : bb; aa = aa > 255u ? 255u : aa;
            argb[o++] = (aa << 24) | (rr << 16) | (gg << 8) | bb;
        }
    }
}

SDL_Texture *ec_bplus_ort_process(EcBplusOrt *ctx, const uint32_t *argb,
                                  int width, int height, int stride_px)
{
    if (!ctx || !ctx->session || !argb || width <= 0 || height <= 0)
        return NULL;

    Uint64 t0 = SDL_GetPerformanceCounter();
    int out_w = width * 4, out_h = height * 4;
    size_t in_n = (size_t)width * (size_t)height;
    size_t out_n = (size_t)out_w * (size_t)out_h;

    if (!ctx->output || ctx->in_w != width || ctx->in_h != height) {
        if (ctx->output)
            SDL_DestroyTexture(ctx->output);
        ctx->output = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING, out_w, out_h);
        uint32_t *next = (uint32_t *)realloc(ctx->argb_out, out_n * sizeof(uint32_t));
        if (!ctx->output || !next) {
            free(next);
            if (ctx->output) {
                SDL_DestroyTexture(ctx->output);
                ctx->output = NULL;
            }
            fprintf(stderr, "[BPLUS_ORT] allocation failure at %dx%d\n", width, height);
            ctx->metrics.fallback_frames++;
            return NULL;
        }
        ctx->argb_out = next;
        ctx->in_w = width;
        ctx->in_h = height;
        ctx->out_w = out_w;
        ctx->out_h = out_h;
        ctx->metrics.input_width = (unsigned)width;
        ctx->metrics.input_height = (unsigned)height;
        ctx->metrics.output_width = (unsigned)out_w;
        ctx->metrics.output_height = (unsigned)out_h;
        ctx->metrics.scale = 4u;
    }

    const int is_fp16 = ctx->in_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16;
    const size_t elem_size = is_fp16 ? sizeof(uint16_t) : sizeof(float);
    void *in_planar = malloc(in_n * 4u * elem_size);
    if (!in_planar) {
        ctx->metrics.fallback_frames++;
        return NULL;
    }

    if (is_fp16)
        ort_upload_rgba16(argb, width, height, stride_px, (uint16_t *)in_planar);
    else
        ort_upload_rgba(argb, width, height, stride_px, (float *)in_planar);

    const int64_t in_shape[4] = {1, 4, height, width};
    const int64_t out_shape[4] = {1, 4, out_h, out_w};
    (void)out_shape;
    OrtValue *in_val = NULL, *out_val = NULL;
    OrtStatus *st = ctx->api->CreateTensorWithDataAsOrtValue(
        ctx->cpu_mem, in_planar, in_n * 4u * elem_size,
        in_shape, 4, ctx->in_type, &in_val);
    if (!st)
        st = ctx->api->Run(ctx->session, NULL, (const char *const *)&ctx->in_name,
                           (const OrtValue *const *)&in_val, 1,
                           (const char *const *)&ctx->out_name, 1, &out_val);
    if (st) {
        fprintf(stderr, "[BPLUS_ORT] run failed: %s\n", ort_status_msg(ctx->api, st));
        ort_release_status(ctx->api, st);
        if (in_val) ctx->api->ReleaseValue(in_val);
        free(in_planar);
        ctx->metrics.fallback_frames++;
        return NULL;
    }
    void *out_data = NULL;
    st = ctx->api->GetTensorMutableData(out_val, &out_data);
    if (!st && out_data) {
        if (is_fp16)
            ort_download_rgba16((const uint16_t *)out_data, out_w, out_h, ctx->argb_out);
        else
            ort_download_rgba((const float *)out_data, out_w, out_h, ctx->argb_out);
    } else {
        fprintf(stderr, "[BPLUS_ORT] output copy failed: %s\n", ort_status_msg(ctx->api, st));
        ort_release_status(ctx->api, st);
        ctx->api->ReleaseValue(in_val);
        ctx->api->ReleaseValue(out_val);
        free(in_planar);
        ctx->metrics.fallback_frames++;
        return NULL;
    }

    ctx->api->ReleaseValue(in_val);
    ctx->api->ReleaseValue(out_val);
    free(in_planar);

    if (SDL_UpdateTexture(ctx->output, NULL, ctx->argb_out,
                          out_w * (int)sizeof(uint32_t)) != 0) {
        fprintf(stderr, "[BPLUS_ORT] UpdateTexture failed: %s\n", SDL_GetError());
        ctx->metrics.fallback_frames++;
        return NULL;
    }

    Uint64 t1 = SDL_GetPerformanceCounter();
    double ms = (double)(t1 - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    ort_record_sample(ctx, ms);

    ctx->metrics.frames++;
    if (ctx->budget_ms > 0.0 && ctx->metrics.frames >= BPLUS_ORT_WARMUP_FRAMES &&
        ctx->metrics.last_gpu_ms > ctx->budget_ms) {
        fprintf(stderr, "[BPLUS_ORT] budget overrun: %.2f ms > %.2f ms; "
                        "retaining fallback for this frame\n", ms, ctx->budget_ms);
        ctx->metrics.fallback_frames++;
        return NULL;
    }
    return ctx->output;
}

int ec_bplus_ort_readback(EcBplusOrt *ctx, void *argb, size_t pitch_bytes)
{
    if (!ctx || !argb || !ctx->argb_out || ctx->out_w <= 0 || ctx->out_h <= 0)
        return 0;
    if (pitch_bytes < (size_t)ctx->out_w * 4u || pitch_bytes > (size_t)INT_MAX)
        return 0;
    const size_t row = (size_t)ctx->out_w * sizeof(uint32_t);
    if (pitch_bytes == row) {
        memcpy(argb, ctx->argb_out, row * (size_t)ctx->out_h);
    } else {
        uint8_t *dst = (uint8_t *)argb;
        const uint8_t *src = (const uint8_t *)ctx->argb_out;
        for (int y = 0; y < ctx->out_h; ++y) {
            memcpy(dst, src, row);
            dst += pitch_bytes;
            src += row;
        }
    }
    return 1;
}

void ec_bplus_ort_metrics(const EcBplusOrt *ctx, EcBplusOrtMetrics *out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!ctx)
        return;
    *out = ctx->metrics;
    out->time_elapsed_available = 1;
    ort_compute_percentiles((EcBplusOrt *)ctx);
    out->p50_gpu_ms = ctx->metrics.p50_gpu_ms;
    out->p99_gpu_ms = ctx->metrics.p99_gpu_ms;
    out->mean_gpu_ms = ctx->metrics.mean_gpu_ms;
}
