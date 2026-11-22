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
#include "ec_bplus_ort.h"
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
static int        s_bplus_onnx_cli_set = 0;
static char       s_bplus_onnx_path[512] = "assets/models/ec_bplus_espcn4x.onnx";
static int        s_bplus_budget_cli_set = 0;
static double     s_bplus_budget_ms = 0.0;
static int        s_bplus_intensity_cli_set = 0;
static double     s_bplus_intensity = 1.0;
enum { EC_BPLUS_BACKEND_AUTO = 0, EC_BPLUS_BACKEND_GL = 1, EC_BPLUS_BACKEND_CUDA = 2 };
static int        s_bplus_backend = EC_BPLUS_BACKEND_AUTO;
static int        s_bplus_backend_cli_set = 0;
static EcBplusGl *s_bplus_gl = NULL;
static EcBplusOrt *s_bplus_ort = NULL;
static int        s_bplus_checked = 0;

static uint16_t ec_ram16(unsigned a)
{
    a &= 0xFFFFu;
    return (uint16_t)(((uint16_t)g_ram[a] << 8) | g_ram[(a + 1u) & 0xFFFFu]);
}

static uint32_t ec_ram32(unsigned a)
{
    return ((uint32_t)ec_ram16(a) << 16) | (uint32_t)ec_ram16(a + 2u);
}

static uint16_t ec_mem16(uint32_t addr)
{
    uint32_t a = addr & 0xFFFFFFu;
    if (a < 0x3FFFE0u)
        return (uint16_t)(((uint16_t)g_rom[a] << 8) | g_rom[a + 1u]);
    if (a >= 0xFF0000u)
        return ec_ram16((unsigned)(a & 0xFFFFu));
    return 0u;
}

static uint32_t ec_mem32(uint32_t addr)
{
    return ((uint32_t)ec_mem16(addr) << 16) | (uint32_t)ec_mem16(addr + 2u);
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

static uint16_t ec_lookup_layer_tile(uint16_t pl, int line, int sx, int hs, uint16_t cur_e)
{
    uint16_t a4 = ec_ram16(0xD84Eu);
    for (int step = 0; step < 32 && a4 != 0u; ++step, a4 = ec_ram16(a4)) {
        if (ec_ram16((unsigned)a4 + 0x2Eu) != pl)
            continue;
        uint8_t fl = g_ram[((unsigned)a4 + 0x30u) & 0xFFFFu];
        if ((fl & 0x06u) == 0u)
            continue;

        int s1C = (int)(int16_t)ec_ram16((unsigned)a4 + 0x1Cu);
        int hA  = (int)(int16_t)ec_ram16((unsigned)a4 + 0x0Au);
        if (line < s1C || line >= s1C + hA)
            continue;

        int x4  = (int)(int16_t)ec_ram16((unsigned)a4 + 0x04u);
        int eE  = (int)(int16_t)ec_ram16((unsigned)a4 + 0x0Eu);
        int e12 = (int)(int16_t)ec_ram16((unsigned)a4 + 0x12u);
        int s1A = (int)(int16_t)ec_ram16((unsigned)a4 + 0x1Au);
        int c0  = (int)(int16_t)ec_ram16((unsigned)a4 + 0x1Eu);

        int base_x0 = eE - s1A;
        int hs_x0   = ((-hs) - x4) & 0x3FF;
        int diff_x0 = ((hs_x0 - base_x0 + 512) & 0x3FF) - 512;
        int lx      = base_x0 + diff_x0 + sx;
        int ly      = (e12 - s1C) + line;
        if (ly < 0)
            return cur_e;

        int l_tc   = lx >> 3;
        int l_tr   = ly >> 3;
        int mt_col = l_tc >> 2;
        int mt_row = l_tr >> 2;

        int is_hstream = ((fl & 0x22u) == 0x02u);
        int out_of_win = (c0 != 0x4FFF && (mt_col < c0 || mt_col > c0 + 10));
        if (cur_e != 0x809Cu && !(is_hstream && out_of_win))
            return cur_e;

        uint16_t slot = ec_ram16((unsigned)a4 + 0x2Cu);
        if (slot >= 128u)
            return cur_e;

        uint32_t a1 = ec_ram32(0xCD1Eu + (unsigned)slot * 8u);
        uint16_t a0 = ec_ram16(0xCD1Eu + (unsigned)slot * 8u + 4u);
        if (a1 == 0u || a0 == 0u)
            return cur_e;

        uint16_t d5     = (uint16_t)(ec_ram16((unsigned)a0 + 2u) >> 5);
        uint16_t mask32 = ec_ram16((unsigned)a4 + 0x32u);
        uint16_t mask34 = ec_ram16((unsigned)a4 + 0x34u);
        uint16_t mc     = ec_mem16(a1);
        uint16_t mr     = ec_mem16(a1 + 2u);
        uint16_t fb     = ec_mem16(a1 + 4u);
        uint32_t a2     = ec_mem32(a1 + 8u);
        if (a2 == 0u)
            return cur_e;

        uint16_t mc_idx = (uint16_t)mt_col & mask32;
        uint16_t mr_idx = (uint16_t)mt_row & mask34;
        uint16_t mt_idx = fb;
        if (mr_idx <= mr && mc_idx <= mc) {
            uint32_t rptr = ec_mem32(a1 + 12u + (uint32_t)mr_idx * 4u);
            if (rptr != 0u)
                mt_idx = ec_mem16(rptr + (uint32_t)mc_idx * 2u);
        }

        int sub_c = l_tc & 3;
        int sub_r = l_tr & 3;
        int16_t d1_off = (int16_t)(uint16_t)(((mt_idx << 5) + (sub_r << 3) + (sub_c << 1)) & 0xFFFFu);
        uint32_t tile_addr = (uint32_t)((int32_t)a2 + (int32_t)d1_off);
        return (uint16_t)(ec_mem16(tile_addr) + d5);
    }
    return cur_e;
}

static void ec_fetch_plane_pixel(const GVDP *v, uint16_t base, uint16_t pl,
                                 int line, int sx, int hs, int wt, int ht,
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
    e = ec_lookup_layer_tile(pl, line, sx, hs, e);
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

typedef struct {
    uint8_t idx[GVDP_MAX_WIDTH];
    uint8_t op[GVDP_MAX_WIDTH];
    uint8_t hi[GVDP_MAX_WIDTH];
    uint8_t sh[GVDP_MAX_WIDTH];
    uint8_t hl[GVDP_MAX_WIDTH];
} EcMarginSprites;

static void ec_eval_margin_sprites_line(const GVDP *v, int line, int native_w,
                                        int left, int width, EcMarginSprites *ms)
{
    memset(ms, 0, sizeof(*ms));
    if (!(v->reg[1] & 0x40u))
        return;

    int h40          = (v->reg[12] & 0x81u) != 0;
    int im2          = (v->reg[12] & 0x06u) == 0x06u;
    int sh_mode      = (v->reg[12] & 0x08u) != 0;
    int max_sprites  = h40 ? 80 : 64;
    int cell_h_shift = im2 ? 4 : 3;
    int tile_bytes   = im2 ? 64 : 32;
    uint16_t sat     = (uint16_t)((v->reg[5] & 0x7Fu) << 9);

    uint8_t drawn[GVDP_MAX_WIDTH];
    memset(drawn, 0, (size_t)width);

    int link = 0;
    for (int n = 0; n < max_sprites; ++n) {
        uint16_t e    = (uint16_t)(sat + link * 8);
        uint16_t w0   = ec_vram16(v, e);
        uint16_t w1   = ec_vram16(v, (uint16_t)(e + 2));
        uint16_t attr = ec_vram16(v, (uint16_t)(e + 4));
        uint16_t w3   = ec_vram16(v, (uint16_t)(e + 6));
        if (w0 == 0 && w1 == 0 && attr == 0 && w3 == 0)
            break;

        int y      = (int)(w0 & 0x3FFu) - (im2 ? 256 : 128);
        uint8_t sz = (uint8_t)(w1 >> 8);
        int hsz    = ((sz >> 2) & 3) + 1;
        int vsz    = (sz & 3) + 1;
        int height = vsz << cell_h_shift;
        int next   = (int)(w1 & 0x7Fu);

        if (line >= y && line < y + height) {
            /* Sub_E684 grava (screen_x + 128) com sinal 16-bit na SAT (sem cull quando bit6=0). */
            int sx0   = (int)(int16_t)w3 - 128;
            int spr_w = hsz * 8;
            int ox0   = left + sx0;
            int ox1   = ox0 + spr_w;

            if (ox1 > 0 && ox0 < width && (ox0 < left || ox1 > left + native_w)) {
                int tile  = (int)(attr & 0x07FFu);
                int pal   = (int)((attr >> 13) & 0x3u);
                int pri   = (attr & 0x8000u) != 0;
                int hf    = (attr & 0x0800u) != 0;
                int vf    = (attr & 0x1000u) != 0;
                int iy    = vf ? (height - 1 - (line - y)) : (line - y);
                int celly = iy >> cell_h_shift;
                int fy    = iy & ((1 << cell_h_shift) - 1);

                for (int lx = 0; lx < spr_w; ++lx) {
                    int ox = ox0 + lx;
                    if (ox < 0 || ox >= width)
                        continue;
                    if (ox >= left && ox < left + native_w)
                        continue;
                    if (drawn[ox])
                        continue;

                    int ix     = hf ? (spr_w - 1 - lx) : lx;
                    int cellx  = ix >> 3;
                    int fx     = ix & 7;
                    int tileno = (tile + cellx * vsz + celly) & 0x07FF;
                    uint16_t pa  = (uint16_t)(tileno * tile_bytes + fy * 4 + (fx >> 1));
                    uint8_t byte = v->vram[pa];
                    int nib      = (fx & 1) ? (byte & 0x0F) : (byte >> 4);
                    if (nib == 0)
                        continue;

                    drawn[ox] = 1;
                    if (sh_mode && pal == 3 && nib == 14) {
                        ms->hl[ox] = 1;
                        ms->hi[ox] = (uint8_t)pri;
                    } else if (sh_mode && pal == 3 && nib == 15) {
                        ms->sh[ox] = 1;
                        ms->hi[ox] = (uint8_t)pri;
                    } else {
                        ms->idx[ox] = (uint8_t)(pal * 16 + nib);
                        ms->op[ox]  = 1;
                        ms->hi[ox]  = (uint8_t)pri;
                    }
                }
            }
        }

        if (next == 0 || next >= max_sprites)
            break;
        link = next;
    }
}

static uint32_t ec_render_margin_pixel(const GVDP *v, int line, int native_w, int sx,
                                       const EcMarginSprites *ms, int ox)
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
    ec_fetch_plane_pixel(v, base_a, 0u, line, sx, hs_a, wt, ht, sx - hs_a, line + va, im2, &idx_a, &op_a, &hi_a);
    ec_fetch_plane_pixel(v, base_b, 2u, line, sx, hs_b, wt, ht, sx - hs_b, line + vb, im2, &idx_b, &op_b, &hi_b);

    uint8_t s_idx = ms ? ms->idx[ox] : 0;
    int s_op = ms ? (ms->op[ox] != 0) : 0;
    int s_hi = ms ? (ms->hi[ox] != 0) : 0;
    int s_sh = ms ? (ms->sh[ox] != 0) : 0;
    int s_hl = ms ? (ms->hl[ox] != 0) : 0;

    uint8_t cidx = backdrop;
    if      (s_op && s_hi) cidx = s_idx;
    else if (op_a && hi_a) cidx = idx_a;
    else if (op_b && hi_b) cidx = idx_b;
    else if (s_op)         cidx = s_idx;
    else if (op_a)         cidx = idx_a;
    else if (op_b)         cidx = idx_b;

    int level = GENESIS_DAC_NORMAL;
    if (sh_mode) {
        int plane_hi = (op_a && hi_a) || (op_b && hi_b);
        int any_hi   = plane_hi || (s_op && s_hi);
        level = any_hi ? GENESIS_DAC_NORMAL : GENESIS_DAC_SHADOW;
        if (s_hi || !plane_hi) {
            if (s_hl)
                level = (level == GENESIS_DAC_SHADOW) ? GENESIS_DAC_NORMAL : GENESIS_DAC_HIGHLIGHT;
            else if (s_sh)
                level = GENESIS_DAC_SHADOW;
        }
        if (s_op && cidx == 0x3Eu && level == GENESIS_DAC_SHADOW)
            level = GENESIS_DAC_NORMAL;
    }

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

static int s_trex_pan = 0;

static int ec_trex_overkill_state(void)
{
    if (ec_ram16(0xFD64u) != 26u)
        return 0;
    uint16_t a0 = 0xACBAu;
    for (int i = 0; i < 48; ++i) {
        a0 = ec_ram16((unsigned)a0 + 6u);
        if (a0 == 0u)
            break;
        if (ec_ram16((unsigned)a0 + 0x40u) == 0x0017u) {
            uint16_t fl20 = ec_ram16((unsigned)a0 + 0x20u);
            return ((fl20 & 0x8080u) == 0u) ? 2 : 1;
        }
    }
    return 0;
}

static int ec_compute_viewport_x0(int line, int native_w, int width)
{
    int extra = width - native_w;
    if (extra <= 0)
        return 0;
    int left  = extra / 2;
    int right = extra - left;

    int x0 = left;
    int cur_sx = (int)(int16_t)ec_ram16(0xCD14u);
    int min_sx = (int)(int16_t)ec_ram16(0xCCF0u);
    int max_sx = (int)(int16_t)ec_ram16(0xCCECu);
    if (max_sx > min_sx && cur_sx >= min_sx && cur_sx <= max_sx) {
        if (cur_sx - min_sx < left) {
            x0 = cur_sx - min_sx;
            if (x0 < 0) x0 = 0;
        } else if (max_sx - cur_sx < right) {
            x0 = extra - (max_sx - cur_sx);
            if (x0 > extra) x0 = extra;
        }
    }

    int trex_st = ec_trex_overkill_state();
    if (trex_st == 2) {
        s_trex_pan = x0;
        return 0;
    } else if (trex_st == 1) {
        if (line == 0 && s_trex_pan < x0)
            ++s_trex_pan;
        int px0 = x0 - s_trex_pan;
        return (px0 < 0) ? 0 : px0;
    } else {
        s_trex_pan = 0;
    }

    return x0;
}

static void ec_video_scanline_lazy(const GVDP *vdp, int line, int native_w,
                                   uint32_t *out, int width,
                                   GameVideoNativeRow native_row, void *native_ctx)
{
    const uint32_t *src = native_row ? native_row(native_ctx) : NULL;
    int left = (width - native_w) / 2;
    if (left < 0) left = 0;
    int x0 = left;

    if (vdp && ec_is_arena_state() && width > native_w && width <= GVDP_MAX_WIDTH) {
        x0 = ec_compute_viewport_x0(line, native_w, width);
        EcMarginSprites ms;
        ec_eval_margin_sprites_line(vdp, line, native_w, x0, width, &ms);
        for (int x = 0; x < x0; ++x)
            out[x] = ec_render_margin_pixel(vdp, line, native_w, x - x0, &ms, x);
        for (int x = x0 + native_w; x < width; ++x)
            out[x] = ec_render_margin_pixel(vdp, line, native_w, x - x0, &ms, x);
    } else if (width > native_w) {
        for (int x = 0; x < left; ++x)
            out[x] = 0xFF000000u;
        for (int x = left + native_w; x < width; ++x)
            out[x] = 0xFF000000u;
    }

    if (src) {
        int copy_w = (x0 + native_w <= width) ? native_w : (width - x0);
        if (copy_w > 0)
            memcpy(out + x0, src, (size_t)copy_w * sizeof(uint32_t));
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

void ec_video_set_bplus_intensity(double intensity)
{
    if (intensity >= 0.0) {
        s_bplus_intensity = intensity;
        s_bplus_intensity_cli_set = 1;
    }
}

void ec_video_set_bplus_backend(const char *backend)
{
    if (!backend || !*backend)
        return;
    s_bplus_backend_cli_set = 1;
    if (strcmp(backend, "gl") == 0)
        s_bplus_backend = EC_BPLUS_BACKEND_GL;
    else if (strcmp(backend, "cuda") == 0 || strcmp(backend, "ort") == 0 ||
             strcmp(backend, "onnx") == 0)
        s_bplus_backend = EC_BPLUS_BACKEND_CUDA;
    else if (strcmp(backend, "auto") == 0)
        s_bplus_backend = EC_BPLUS_BACKEND_AUTO;
    else
        fprintf(stderr, "[EC_VIDEO] --bplus-backend desconhecido '%s' (auto|cuda|gl)\n",
                backend);
}

static int ec_resolve_bplus_backend(void)
{
    if (s_bplus_backend_cli_set)
        return s_bplus_backend;
    const char *env = getenv("EC_BPLUS_BACKEND");
    if (env && *env) {
        if (strcmp(env, "gl") == 0)
            return EC_BPLUS_BACKEND_GL;
        if (strcmp(env, "cuda") == 0 || strcmp(env, "ort") == 0 || strcmp(env, "onnx") == 0)
            return EC_BPLUS_BACKEND_CUDA;
        if (strcmp(env, "auto") == 0)
            return EC_BPLUS_BACKEND_AUTO;
    }
    return EC_BPLUS_BACKEND_AUTO;
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

static const char *ec_resolve_bplus_onnx_path(void)
{
    if (s_bplus_onnx_cli_set)
        return s_bplus_onnx_path;
    const char *env = getenv("EC_BPLUS_ONNX");
    if (env && *env)
        return env;
    /* Prefere o modelo fp16 quando disponível (paridade < 1/255, ~2x mais
     * rápido no CUDA); cai pro fp32 senão. */
    static const char *fp16_candidates[] = {
        "assets/models/ec_bplus_espcn4x_fp16.onnx",
        "../../assets/models/ec_bplus_espcn4x_fp16.onnx",
    };
    for (size_t i = 0; i < sizeof(fp16_candidates) / sizeof(fp16_candidates[0]); ++i) {
        FILE *fp = fopen(fp16_candidates[i], "rb");
        if (fp) {
            fclose(fp);
            return fp16_candidates[i];
        }
    }
    FILE *fp = fopen(s_bplus_onnx_path, "rb");
    if (fp) {
        fclose(fp);
        return s_bplus_onnx_path;
    }
    static const char k_rel_from_build[] = "../../assets/models/ec_bplus_espcn4x.onnx";
    fp = fopen(k_rel_from_build, "rb");
    if (fp) {
        fclose(fp);
        return k_rel_from_build;
    }
    return s_bplus_onnx_path;
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

static double ec_resolve_bplus_intensity(void)
{
    if (s_bplus_intensity_cli_set)
        return s_bplus_intensity;
    const char *env = getenv("EC_BPLUS_INTENSITY");
    if (env && *env) {
        char *end = NULL;
        double v = strtod(env, &end);
        if (end != env && *end == '\0' && v >= 0.0)
            return v;
    }
    return s_bplus_intensity;
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
        const char *wpath = ec_resolve_bplus_model_path();
        const char *opath = ec_resolve_bplus_onnx_path();
        double budget = ec_resolve_bplus_budget_ms();
        float intensity = (float)ec_resolve_bplus_intensity();
        int backend = ec_resolve_bplus_backend();
        int want_cuda = backend == EC_BPLUS_BACKEND_CUDA || backend == EC_BPLUS_BACKEND_AUTO;
        int want_gl = backend == EC_BPLUS_BACKEND_GL || backend == EC_BPLUS_BACKEND_AUTO;

        if (want_cuda && intensity == 1.0f) {
            s_bplus_ort = ec_bplus_ort_create(renderer, activation, opath, budget);
            if (!s_bplus_ort && backend == EC_BPLUS_BACKEND_CUDA)
                fprintf(stderr, "[EC_VIDEO] Mode B+ CUDA unavailable; retaining Modo A/B fallback\n");
        } else if (want_cuda) {
            if (backend == EC_BPLUS_BACKEND_CUDA)
                fprintf(stderr, "[EC_VIDEO] CUDA backend requires intensity=1.0 (got %.3f); using GL\n",
                        intensity);
        }

        if (!s_bplus_ort && want_gl) {
            s_bplus_gl = ec_bplus_gl_create(renderer, activation, wpath, budget);
            if (!s_bplus_gl)
                fprintf(stderr, "[EC_VIDEO] Mode B+ unavailable; retaining Modo A/B fallback\n");
        }

        if (s_bplus_ort)
            ec_bplus_ort_set_intensity(s_bplus_ort, intensity);
        if (s_bplus_gl)
            ec_bplus_gl_set_intensity(s_bplus_gl, intensity);
    }
    if (s_bplus_ort)
        return ec_bplus_ort_process(s_bplus_ort, argb, width, height, stride_px);
    if (s_bplus_gl)
        return ec_bplus_gl_process(s_bplus_gl, argb, width, height, stride_px);
    return NULL;
}

static void ec_video_present_gpu_destroy(void)
{
    if (s_bplus_ort) {
        EcBplusOrtMetrics m;
        ec_bplus_ort_metrics(s_bplus_ort, &m);
        if (m.frames > 0)
            fprintf(stderr,
                    "[EC_VIDEO] B+ CUDA: %llu frames, %llu fallbacks | last=%.3f ms "
                    "p50=%.3f p99=%.3f mean=%.3f | %ux%u -> %ux%u\n",
                    (unsigned long long)m.frames, (unsigned long long)m.fallback_frames,
                    m.last_gpu_ms, m.p50_gpu_ms, m.p99_gpu_ms, m.mean_gpu_ms,
                    m.input_width, m.input_height, m.output_width, m.output_height);
        ec_bplus_ort_destroy(s_bplus_ort);
        s_bplus_ort = NULL;
    }
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
