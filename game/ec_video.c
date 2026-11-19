/*
 * ec_video.c — Adaptador GameVideo para Eternal Champions (Fases 4–5: M4/M6/Modo C).
 *
 * - Modo padrão ("off"): Modo A nativo (320x224 sem overhead).
 * - Widescreen ("fit", "16:9", "21:9", "W:H"): centraliza a janela lógica de 320px
 *   e estende os planos A/B nas margens laterais durante os estados de arena ativa (17..27)
 *   usando o VDP HAL vivo (tilemaps na VRAM, VDP Reg 11 per-line H-scroll $8B03,
 *   VSRAM V-scroll e DAC CRAM). Em telas de título/menus/biografias aplica pillarbox.
 * - Modo C (--hd-manifest / EC_HD_MANIFEST): carrega pipeline/manifest.json e mantém
 *   fallback automático para Modo A/B quando não houver asset "approved".
 */

#include "ec_video.h"
#include "ec_bplus_gl.h"
#include "genesis_runtime.h"
#include "genesis_dac.h"
#include "genesis_vdp.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { EC_VIDEO_OFF = 0, EC_VIDEO_FIT = 1, EC_VIDEO_RATIO = 2, EC_VIDEO_STAGE = 3 };

static int    s_mode = EC_VIDEO_OFF;
static double s_ratio = 16.0 / 9.0;
static int    s_requested_width = 320;
static int    s_hd_loaded = 0;
static int    s_hd_total_entries = 0;
static int    s_hd_approved_entries = 0;

static int        s_bplus_cli_set = 0;
static int        s_bplus_enabled = 0;
static int        s_bplus_model_cli_set = 0;
static char       s_bplus_model_path[512] = "assets/models/ec_bplus_espcn4x_weights.bin";
static int        s_bplus_budget_cli_set = 0;
static double     s_bplus_budget_ms = 0.0;
static EcBplusGl *s_bplus_gl = NULL;
static int        s_bplus_checked = 0;

static uint16_t ec_ram16(unsigned a)
{
    a &= 0xFFFFu;
    return (uint16_t)(((uint16_t)g_ram[a] << 8) | g_ram[(a + 1u) & 0xFFFFu]);
}

static uint16_t ec_vram16(const GVDP *v, uint16_t addr)
{
    uint16_t a0 = (uint16_t)(addr & 0xFFFEu);
    return (uint16_t)(((uint16_t)v->vram[a0] << 8) | v->vram[(uint16_t)(a0 + 1u)]);
}

static int ec_plane_tiles(unsigned code2)
{
    switch (code2 & 0x3u) {
    case 1:  return 64;
    case 3:  return 128;
    default: return 32;
    }
}

static void ec_fetch_plane_pixel(const GVDP *v, uint16_t base, int wt, int ht,
                                 int px, int py, int im2,
                                 uint8_t *idx, int *opaque, int *hi)
{
    int shift      = im2 ? 4 : 3;
    int fy_mask    = (1 << shift) - 1;
    int tile_bytes = im2 ? 64 : 32;
    int wpx        = wt * 8;
    int hpx        = ht << shift;

    px &= (wpx - 1);
    py &= (hpx - 1);

    uint16_t nt = (uint16_t)(base + (uint16_t)(((py >> shift) * wt + (px >> 3)) * 2));
    uint16_t e  = ec_vram16(v, nt);
    int ti      = e & 0x07FF;
    int fx      = px & 7;
    int fy      = py & fy_mask;
    if (e & 0x0800u) fx ^= 7;
    if (e & 0x1000u) fy ^= fy_mask;

    uint16_t pa  = (uint16_t)(ti * tile_bytes + fy * 4 + (fx >> 1));
    uint8_t byte = v->vram[pa];
    int nib      = (fx & 1) ? (byte & 0x0F) : (byte >> 4);

    *opaque = (nib != 0);
    *hi     = (e & 0x8000u) != 0;
    *idx    = (uint8_t)(((e >> 13) & 3u) * 16u + (unsigned)nib);
}

static uint32_t ec_render_margin_pixel(const GVDP *v, int line, int native_w, int sx)
{
    int display_on = (v->reg[1] & 0x40u) != 0;
    uint8_t backdrop = (uint8_t)(v->reg[7] & 0x3Fu);
    if (!display_on)
        return genesis_dac_cram_to_argb(v->cram[backdrop], GENESIS_DAC_NORMAL);

    int im2     = (v->reg[12] & 0x06u) == 0x06u;
    int rline   = im2 ? (line >> 1) : line;
    int sh_mode = (v->reg[12] & 0x08u) != 0;

    int wt = ec_plane_tiles(v->reg[16] & 0x03u);
    int ht = ec_plane_tiles((v->reg[16] >> 4) & 0x03u);
    uint16_t base_a = (uint16_t)((v->reg[2] & 0x38u) << 10);
    uint16_t base_b = (uint16_t)((v->reg[4] & 0x07u) << 13);

    uint16_t hbase = (uint16_t)((v->reg[13] & 0x3Fu) << 10);
    int hmode = v->reg[11] & 0x03;
    int hidx  = (hmode == 0) ? 0 : (hmode == 2) ? (rline & ~7) : rline;
    int hs_a  = (int)(ec_vram16(v, (uint16_t)(hbase + hidx * 4 + 0)) & 0x3FFu);
    int hs_b  = (int)(ec_vram16(v, (uint16_t)(hbase + hidx * 4 + 2)) & 0x3FFu);

    int vs_mask = im2 ? 0x7FF : 0x3FF;
    int va = (int)(v->vsram[0] & vs_mask);
    int vb = (int)(v->vsram[1] & vs_mask);
    if (v->reg[11] & 0x04u) {
        int xc  = (sx < 0) ? 0 : (native_w - 1);
        int col = (xc >> 4) * 2;
        va = (int)(v->vsram[col % GVDP_VSRAM_ENTRIES] & vs_mask);
        vb = (int)(v->vsram[(col + 1) % GVDP_VSRAM_ENTRIES] & vs_mask);
    }

    uint8_t idx_a = 0, idx_b = 0;
    int op_a = 0, op_b = 0, hi_a = 0, hi_b = 0;
    ec_fetch_plane_pixel(v, base_a, wt, ht, sx - hs_a, line + va, im2, &idx_a, &op_a, &hi_a);
    ec_fetch_plane_pixel(v, base_b, wt, ht, sx - hs_b, line + vb, im2, &idx_b, &op_b, &hi_b);

    uint8_t cidx = backdrop;
    if (op_a && hi_a)      cidx = idx_a;
    else if (op_b && hi_b) cidx = idx_b;
    else if (op_a)         cidx = idx_a;
    else if (op_b)         cidx = idx_b;

    int level = GENESIS_DAC_NORMAL;
    if (sh_mode && !hi_a && !hi_b)
        level = GENESIS_DAC_SHADOW;

    return genesis_dac_cram_to_argb(v->cram[cidx & 63u], level);
}

static int ec_is_arena_state(void)
{
    uint16_t st = ec_ram16(0xFD64u);
    return (st == 14u || st == 15u || (st >= 17u && st <= 27u));
}

static int ec_video_configure(const char *mode)
{
    if (!mode) return 0;
    if (strcmp(mode, "off") == 0) {
        s_mode = EC_VIDEO_OFF;
    } else if (strcmp(mode, "fit") == 0 || strcmp(mode, "adaptive") == 0) {
        s_mode = EC_VIDEO_FIT;
    } else if (strcmp(mode, "stage") == 0) {
        s_mode  = EC_VIDEO_STAGE;
        s_ratio = 16.0 / 9.0;
    } else {
        char *end = NULL;
        double w = strtod(mode, &end);
        if (end == mode || !end || *end != ':') return 0;
        const char *den = end + 1;
        double h = strtod(den, &end);
        if (end == den || !end || *end != '\0' || !(w > 0.0) || !(h > 0.0) ||
            w / h > (double)INT_MAX / 480.0)
            return 0;
        s_ratio = w / h;
        s_mode  = EC_VIDEO_RATIO;
    }
    return 1;
}

static int ec_video_enabled(void)
{
    return s_mode != EC_VIDEO_OFF;
}

static int ec_video_width(int drawable_w, int drawable_h, int native_w, int native_h)
{
    if (!ec_video_enabled()) return 0;
    double target = (double)native_w;
    if (s_mode == EC_VIDEO_RATIO || s_mode == EC_VIDEO_STAGE) {
        target = (double)native_h * s_ratio;
    } else if (drawable_w > 0 && drawable_h > 0) {
        target = (double)native_h * (double)drawable_w / (double)drawable_h;
    } else {
        target = (double)native_h * (16.0 / 9.0);
    }
    if (target > (double)GVDP_MAX_WIDTH)
        target = (double)GVDP_MAX_WIDTH;
    int px = (int)(target + 0.5);
    if (px < native_w) px = native_w;
    if (px > GVDP_MAX_WIDTH) px = GVDP_MAX_WIDTH;
    /* Alinha em múltiplo de 2 para centralização simétrica dos 320px */
    px &= ~1;
    if (px < native_w) px = native_w;
    s_requested_width = px;
    return s_requested_width;
}

static void ec_video_scanline_lazy(const GVDP *vdp, int line, int native_w,
                                   uint32_t *out, int width,
                                   GameVideoNativeRow native_row, void *native_ctx)
{
    const uint32_t *src = native_row ? native_row(native_ctx) : NULL;
    int left = (width - native_w) / 2;
    if (left < 0) left = 0;

    if (vdp && ec_is_arena_state()) {
        for (int x = 0; x < left; ++x)
            out[x] = ec_render_margin_pixel(vdp, line, native_w, x - left);
        for (int x = left + native_w; x < width; ++x)
            out[x] = ec_render_margin_pixel(vdp, line, native_w, x - left);
    } else {
        for (int x = 0; x < left; ++x)
            out[x] = 0xFF000000u;
        for (int x = left + native_w; x < width; ++x)
            out[x] = 0xFF000000u;
    }

    if (src) {
        int copy_w = (left + native_w <= width) ? native_w : (width - left);
        if (copy_w > 0)
            memcpy(out + left, src, (size_t)copy_w * sizeof(uint32_t));
    }
}

int ec_video_load_hd_manifest(const char *path)
{
    if (!path || !*path) return -1;
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[EC_VIDEO] HD manifest não encontrado: %s (fallback Modo A/B ativo)\n", path);
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long sz = ftell(fp);
    if (sz < 0 || sz > 16L * 1024L * 1024L || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    char *buf = (char *)malloc((size_t)sz + 1u);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    size_t nread = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[nread] = '\0';

    int total = 0;
    int approved = 0;
    const char *p = buf;
    while ((p = strstr(p, "\"original_png\"")) != NULL) {
        total++;
        p += 14;
    }
    p = buf;
    while ((p = strstr(p, "\"status\"")) != NULL) {
        const char *q = p + 8;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
        if (*q == ':') {
            q++;
            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
            if (strncmp(q, "\"approved\"", 10) == 0)
                approved++;
        }
        p = q;
    }
    free(buf);

    s_hd_loaded = 1;
    s_hd_total_entries = total;
    s_hd_approved_entries = approved;
    fprintf(stderr, "[EC_VIDEO] HD manifest '%s': %d entradas (%d approved, fallback=Modo A/B)\n",
            path, total, approved);
    return total;
}

void ec_video_enable_bplus(int enabled)
{
    s_bplus_cli_set = 1;
    s_bplus_enabled = enabled ? 1 : 0;
}

void ec_video_set_bplus_model(const char *path)
{
    if (!path || !*path)
        return;
    snprintf(s_bplus_model_path, sizeof(s_bplus_model_path), "%s", path);
    s_bplus_model_cli_set = 1;
}

void ec_video_set_bplus_budget_ms(double budget_ms)
{
    if (budget_ms >= 0.0) {
        s_bplus_budget_ms = budget_ms;
        s_bplus_budget_cli_set = 1;
    }
}

static int ec_video_present_gpu_enabled(void)
{
    if (s_bplus_cli_set)
        return s_bplus_enabled;
    const char *env = getenv("EC_NEURAL_BPLUS");
    return (env && (strcmp(env, "1") == 0 || strcmp(env, "on") == 0));
}

static const char *ec_resolve_bplus_model_path(void)
{
    if (s_bplus_model_cli_set)
        return s_bplus_model_path;
    const char *env = getenv("EC_BPLUS_MODEL");
    if (env && *env)
        return env;
    FILE *fp = fopen(s_bplus_model_path, "rb");
    if (fp) {
        fclose(fp);
        return s_bplus_model_path;
    }
    static const char k_rel_from_build[] = "../../assets/models/ec_bplus_espcn4x_weights.bin";
    fp = fopen(k_rel_from_build, "rb");
    if (fp) {
        fclose(fp);
        return k_rel_from_build;
    }
    return s_bplus_model_path;
}

static double ec_resolve_bplus_budget_ms(void)
{
    if (s_bplus_budget_cli_set)
        return s_bplus_budget_ms;
    const char *env = getenv("EC_BPLUS_BUDGET_MS");
    if (env && *env) {
        char *end = NULL;
        double v = strtod(env, &end);
        if (end != env && *end == '\0' && v >= 0.0)
            return v;
    }
    return s_bplus_budget_ms;
}

static struct SDL_Texture *ec_video_present_gpu(
    struct SDL_Renderer *renderer,
    struct SDL_Texture *activation,
    const uint32_t *argb,
    int width, int height, int stride_px,
    const struct GVDPRasterFrame *raster)
{
    (void)raster;
    if (!ec_video_present_gpu_enabled())
        return NULL;
    if (!s_bplus_checked) {
        s_bplus_checked = 1;
        const char *mpath = ec_resolve_bplus_model_path();
        double budget = ec_resolve_bplus_budget_ms();
        s_bplus_gl = ec_bplus_gl_create(renderer, activation, mpath, budget);
        if (!s_bplus_gl)
            fprintf(stderr, "[EC_VIDEO] Mode B+ unavailable; retaining Modo A/B fallback\n");
    }
    if (!s_bplus_gl)
        return NULL;
    return ec_bplus_gl_process(s_bplus_gl, argb, width, height, stride_px);
}

static void ec_video_present_gpu_destroy(void)
{
    if (s_bplus_gl) {
        ec_bplus_gl_destroy(s_bplus_gl);
        s_bplus_gl = NULL;
    }
    s_bplus_checked = 0;
}

const GameVideo g_ec_video = {
    .configure           = ec_video_configure,
    .enabled             = ec_video_enabled,
    .width               = ec_video_width,
    .scanline_lazy       = ec_video_scanline_lazy,
    .present_gpu_enabled = ec_video_present_gpu_enabled,
    .present_gpu         = ec_video_present_gpu,
    .present_gpu_destroy = ec_video_present_gpu_destroy,
};
