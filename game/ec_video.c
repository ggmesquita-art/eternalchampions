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
#include "ec_hd.h"
#include "ec_bplus_gl.h"
#include "ec_bplus_ort.h"
#include "genesis_runtime.h"
#include "genesis_dac.h"
#include "genesis_vdp.h"
#include "video/genesis_machine.h"

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
static EcBplusDeditherMode s_bplus_dedither = EC_BPLUS_DEDITHER_OFF;
static int        s_bplus_dedither_cli_set = 0;
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

/*
 * Snapshot por quadro (tirado na linha 0) de tudo que o renderer widescreen le da WRAM.
 * O 68K roda intercalado com o renderer por scanline e atualiza a WRAM por volta da
 * linha ~35; ler ao vivo causava rasgo de x0 e corrida do c0 (ver docs/widescreen.md GW.3).
 */
#define EC_SNAP_LAYERS   32
#define EC_SNAP_LAYER_SZ 0x3Au

typedef struct {
    uint8_t  raw[EC_SNAP_LAYER_SZ]; /* bytes $00..$39 da camada ($D84E) */
    uint32_t a1;                    /* ($CD1E + slot*8) */
    uint16_t a0;                    /* ($CD1E + slot*8 + 4) */
    uint16_t d5;                    /* ($2,a0) >> 5 */
    uint8_t  slot_ok;               /* slot < 128 */
} EcLayerSnap;

static EcLayerSnap s_snap_layers[EC_SNAP_LAYERS];
static int         s_snap_nlayers = 0;
static int         s_snap_valid = 0;
static int         s_snap_arena = 0;
static int         s_snap_x0 = 0;
static int         s_snap_force_ok = 0; /* ($9B32) != 0 */
static int         s_snap_native_w = 320; /* largura nativa do quadro (H40=320, H32=256) */
static uint16_t    s_snap_fd64 = 0xFFFFu; /* estado ($FD64) do snapshot anterior */

static uint16_t ec_snap_w(const EcLayerSnap *L, unsigned off)
{
    return (uint16_t)(((uint16_t)L->raw[off] << 8) | L->raw[off + 1u]);
}

static uint32_t ec_snap_l(const EcLayerSnap *L, unsigned off)
{
    return ((uint32_t)ec_snap_w(L, off) << 16) | (uint32_t)ec_snap_w(L, off + 2u);
}

static void ec_take_layer_snapshot(void)
{
    s_snap_nlayers = 0;
    uint16_t a4 = ec_ram16(0xD84Eu);
    for (int step = 0; step < EC_SNAP_LAYERS && a4 != 0u; ++step, a4 = ec_ram16(a4)) {
        EcLayerSnap *L = &s_snap_layers[s_snap_nlayers++];
        for (unsigned i = 0; i < EC_SNAP_LAYER_SZ; ++i)
            L->raw[i] = g_ram[((unsigned)a4 + i) & 0xFFFFu];
        uint16_t slot = ec_snap_w(L, 0x2Cu);
        L->slot_ok = (slot < 128u);
        L->a1 = 0u;
        L->a0 = 0u;
        L->d5 = 0u;
        if (L->slot_ok) {
            L->a1 = ec_ram32(0xCD1Eu + (unsigned)slot * 8u);
            L->a0 = ec_ram16(0xCD1Eu + (unsigned)slot * 8u + 4u);
            if (L->a0 != 0u)
                L->d5 = (uint16_t)(ec_ram16((unsigned)L->a0 + 2u) >> 5);
        }
    }
}

static uint16_t ec_lookup_layer_tile(uint16_t pl, int line, int sx, int hs, uint16_t cur_e)
{
    for (int li = 0; li < s_snap_nlayers; ++li) {
        const EcLayerSnap *L = &s_snap_layers[li];
        if (ec_snap_w(L, 0x2Eu) != pl)
            continue;
        uint8_t fl = L->raw[0x30u];
        if ((fl & 0x06u) == 0u)
            continue;

        int s1C = (int)(int16_t)ec_snap_w(L, 0x1Cu);
        int hA  = (int)(int16_t)ec_snap_w(L, 0x0Au);
        if (line < s1C || line >= s1C + hA)
            continue;

        int x4  = (int)(int16_t)ec_snap_w(L, 0x04u);
        int eE  = (int)(int16_t)ec_snap_w(L, 0x0Eu);
        int e12 = (int)(int16_t)ec_snap_w(L, 0x12u);
        int s1A = (int)(int16_t)ec_snap_w(L, 0x1Au);
        int c0  = (int)(int16_t)ec_snap_w(L, 0x1Eu);

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

        /*
         * hstream: so o centro de 320px confia na janela c0 / nametable da VRAM. Nas margens
         * a coluna pode ainda nao ter sido gravada pelo vblank (corrida do c0), entao o
         * fallback de metatiles da ROM e sempre usado.
         */
        int is_hstream = ((fl & 0x22u) == 0x02u);
        int margin_px  = (sx < 0 || sx >= s_snap_native_w);
        int out_of_win = (c0 != 0x4FFF && (mt_col < c0 || mt_col > c0 + 10));
        if (cur_e != 0x809Cu && !(is_hstream && (out_of_win || margin_px)))
            return cur_e;

        if (!L->slot_ok)
            return cur_e;

        uint32_t a1 = L->a1;
        if (a1 == 0u || L->a0 == 0u)
            return cur_e;

        uint16_t d5     = L->d5;
        uint16_t mask32 = ec_snap_w(L, 0x32u);
        uint16_t mask34 = ec_snap_w(L, 0x34u);
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
                                 uint8_t *idx, int *opaque, int *hi, int *streamed)
{
    int shift      = im2 ? 4 : 3;
    int fy_mask    = (1 << shift) - 1;
    int tile_bytes = im2 ? 64 : 32;
    int wpx        = wt * 8;
    int hpx        = ht << shift;

    px &= (wpx - 1);
    py &= (hpx - 1);

    uint16_t nt  = (uint16_t)(base + (uint16_t)(((py >> shift) * wt + (px >> 3)) * 2));
    uint16_t raw = ec_vram16(v, nt);
    uint16_t e   = ec_lookup_layer_tile(pl, line, sx, hs, raw);
    if (streamed)
        *streamed = (e != raw);
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
    uint8_t link[GVDP_MAX_WIDTH];
} EcMarginSprites;

static int     s_frame_had_display = 0;
static int     s_last_floor_line = 215;
static uint8_t s_sat_is_hud[80];

typedef struct {
    uint16_t y;
    uint16_t x;
    uint16_t attr;
} EcHudSubSprite;

/*
 * Identifica no início de cada quadro (line == 0) quais entradas da SAT em VRAM
 * pertencem ao HUD: o pool reservado ($D858 -> SAT[0..($DF7A)-2], barras de vida
 * e gemas) e objetos screen-space ($ACBA com ($18,a0) == 6: yin-yangs, timer, textos).
 */
static void ec_refresh_frame_hud_sat(const GVDP *v)
{
    memset(s_sat_is_hud, 0, sizeof(s_sat_is_hud));
    s_last_floor_line = 215;
    if (!v) {
        s_frame_had_display = 0;
        return;
    }
    s_frame_had_display = (v->reg[1] & 0x40u) != 0;

    int reserved = (int)ec_ram16(0xDF7Au) - 1;
    if (reserved > 0) {
        if (reserved > 80)
            reserved = 80;
        for (int i = 0; i < reserved; ++i)
            s_sat_is_hud[i] = 1;
    }

    EcHudSubSprite hud_entries[128];
    int hud_count = 0;

    uint16_t a0 = ec_ram16(0xACBAu + 6u);
    for (int obj_step = 0; obj_step < 64 && a0 != 0u; ++obj_step) {
        if (ec_ram16((unsigned)a0 + 0x18u) == 6u &&
            (ec_ram16((unsigned)a0 + 0x20u) & 0x8080u) == 0u) {
            uint16_t a1  = ec_ram16((unsigned)a0 + 0x0Cu);
            uint16_t cnt = ec_ram16((unsigned)a0 + 0x0Eu);
            if (cnt > 80u)
                cnt = 80u;
            if (a1 != 0u && cnt != 0u) {
                uint16_t d6 = (uint16_t)(ec_ram16((unsigned)a0 + 0x4Au) + 0x80u);
                uint16_t d7 = (uint16_t)(ec_ram16((unsigned)a0 + 0x4Cu) + 0x80u);
                for (uint16_t i = 0; i < cnt && a1 != 0u && hud_count < 128; ++i) {
                    uint16_t rx   = (uint16_t)(ec_ram16((unsigned)a1 + 0u) + d6);
                    uint16_t ry   = (uint16_t)(ec_ram16((unsigned)a1 + 2u) + d7);
                    uint16_t attr = ec_ram16((unsigned)a1 + 6u);
                    hud_entries[hud_count].x    = (uint16_t)(rx & 0x3FFu);
                    hud_entries[hud_count].y    = (uint16_t)(ry & 0x3FFu);
                    hud_entries[hud_count].attr = attr;
                    ++hud_count;
                    a1 = ec_ram16((unsigned)a1 + 8u);
                }
            }
        }
        a0 = ec_ram16((unsigned)a0 + 6u);
    }

    if (hud_count == 0)
        return;

    int h40         = (v->reg[12] & 0x81u) != 0;
    int max_sprites = h40 ? 80 : 64;
    uint16_t sat    = (uint16_t)((v->reg[5] & 0x7Fu) << 9);

    int link = 0;
    for (int n = 0; n < max_sprites; ++n) {
        uint16_t e    = (uint16_t)(sat + link * 8);
        uint16_t w0   = ec_vram16(v, e);
        uint16_t w1   = ec_vram16(v, (uint16_t)(e + 2));
        uint16_t attr = ec_vram16(v, (uint16_t)(e + 4));
        uint16_t w3   = ec_vram16(v, (uint16_t)(e + 6));
        if (w0 == 0 && w1 == 0 && attr == 0 && w3 == 0)
            break;

        int sy = (int)(w0 & 0x3FFu);
        int sx = (int)(w3 & 0x3FFu);
        for (int k = 0; k < hud_count; ++k) {
            int dy   = sy - (int)hud_entries[k].y;
            int dx_s = sx - (int)hud_entries[k].x;
            if ((abs(dy) <= 1 && attr == hud_entries[k].attr) ||
                (dx_s == 0 && dy == 0 &&
                 (attr & 0x07FFu) == (hud_entries[k].attr & 0x07FFu))) {
                s_sat_is_hud[link] = 1;
                break;
            }
        }

        int next = (int)(w1 & 0x7Fu);
        if (next == 0 || next >= max_sprites)
            break;
        link = next;
    }
}

/*
 * Compensa o parallax nos planos A/B quando x0 ancora a cena nas bordas widescreen
 * (dx = x0 - left != 0). Avalia a última camada ativa em $FFFFD84E que escreve H-scroll.
 */
static int ec_plane_edge_comp(uint16_t pl, int line, int dx)
{
    if (dx == 0)
        return 0;

    int comp = 0;
    for (int li = 0; li < s_snap_nlayers; ++li) {
        const EcLayerSnap *L = &s_snap_layers[li];
        if (ec_snap_w(L, 0x2Eu) != pl)
            continue;
        if ((L->raw[0x30u] & 0x08u) == 0u)
            continue;

        int s1C = (int)(int16_t)ec_snap_w(L, 0x1Cu);
        int hA  = (int)(int16_t)ec_snap_w(L, 0x0Au);
        if (line < s1C || line >= s1C + hA)
            continue;

        /* Preserva tabelas raster por scanline ($36,a4). */
        if (ec_snap_l(L, 0x36u) != 0u) {
            comp = 0;
            continue;
        }

        uint8_t hmode = L->raw[0x26u];
        switch (hmode) {
        case 0x00:
        case 0x08:
        case 0x10:
            comp = 0;
            break;
        case 0x04:
            comp = -dx;
            break;
        case 0x14:
            comp = dx;
            break;
        case 0x0C: {
            int fac = (int)(int16_t)ec_snap_w(L, 0x28u);
            int num = -fac * dx;
            comp = (num >= 0) ? ((num + 2048) / 4096) : ((num - 2048) / 4096);
            break;
        }
        default:
            comp = 0;
            break;
        }
    }
    return comp;
}

/*
 * Sub-sprites descartados por Sub_E684 ($E772..$E7B0: x de tela fora de [-32,320]) em
 * objetos fl20&$4000 (pilares, decoracoes). A SAT nunca os contem, entao nas margens
 * widescreen eles sumiam. Hooks passivos em $E684 (zera) e $E7B0 (registra) coletam-nos
 * em s_extra_pend durante a logica do quadro f; na linha 0 do quadro seguinte (quando a
 * SAT correspondente passa a ser exibida) a lista vira s_extra_act. Nao escreve no jogo.
 */
#define EC_EXTRA_MAX 48
typedef struct {
    int16_t  x;     /* x + 128 (como gravado na SAT) */
    int16_t  y;     /* y + 128 */
    uint16_t szw;   /* palavra size/link de origem (byte alto = size) */
    uint16_t attr;
    uint16_t ord;   /* d4: indice da SAT onde entraria (ordem de prioridade) */
} EcExtraSpr;

static EcExtraSpr s_extra_pend[EC_EXTRA_MAX];
static int        s_extra_pend_n = 0;
static EcExtraSpr s_extra_act[EC_EXTRA_MAX];
static int        s_extra_act_n = 0;

void ec_video_reset_ws_state(void)
{
    s_extra_pend_n = 0;
    s_extra_act_n  = 0;
    s_snap_valid   = 0;
    s_snap_fd64    = 0xFFFFu;
}

void ec_video_on_sprite_hook(uint32_t pc)
{
    if (s_mode == EC_VIDEO_OFF)
        return;
    if (pc == 0x00E684u) {
        s_extra_pend_n = 0;
        return;
    }
    if (pc != 0x00E7B0u || s_extra_pend_n >= EC_EXTRA_MAX)
        return;
    uint16_t a0 = (uint16_t)g_cpu.A[0];
    uint16_t a1 = (uint16_t)g_cpu.A[1];
    if (a0 == 0u || ec_ram16((unsigned)a0 + 0x18u) == 6u)
        return;                                   /* HUD screen-space nunca */
    if ((ec_ram16((unsigned)a0 + 0x20u) & 0x4000u) == 0u)
        return;
    uint16_t d0 = (uint16_t)g_cpu.D[0];
    uint16_t d1 = (uint16_t)g_cpu.D[1];
    if ((uint16_t)(d1 - 0x60u) > 0x100u)
        return;                                   /* descartado por Y, nao por X */
    if ((uint16_t)(d0 - 0x60u) <= 0x160u)
        return;
    EcExtraSpr *e = &s_extra_pend[s_extra_pend_n++];
    e->x    = (int16_t)d0;
    e->y    = (int16_t)d1;
    e->szw  = ec_ram16((unsigned)a1 + 0u);
    e->attr = ec_ram16((unsigned)a1 + 2u);
    e->ord  = (uint16_t)g_cpu.D[4];
}

/* Desenha um sprite na linha. margin_only: so pixels fora de [x0, x0+native_w). */
static void ec_draw_sprite_span(const GVDP *v, int line, int y, int sx0, int base_x,
                                uint8_t sz, uint16_t attr, int link, int width,
                                int im2, int sh_mode, int margin_only, int x0, int native_w,
                                uint8_t *drawn, EcMarginSprites *ms)
{
    int cell_h_shift = im2 ? 4 : 3;
    int tile_bytes   = im2 ? 64 : 32;
    int hsz    = ((sz >> 2) & 3) + 1;
    int vsz    = (sz & 3) + 1;
    int height = vsz << cell_h_shift;
    if (!(line >= y && line < y + height))
        return;

    int spr_w = hsz * 8;
    int ox0   = base_x + sx0;
    int ox1   = ox0 + spr_w;
    if (!(ox1 > 0 && ox0 < width))
        return;

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
        if (margin_only && ox >= x0 && ox < x0 + native_w)
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
            ms->idx[ox]  = (uint8_t)(pal * 16 + nib);
            ms->op[ox]   = 1;
            ms->hi[ox]   = (uint8_t)pri;
            ms->link[ox] = (uint8_t)link;
        }
    }
}

static void ec_eval_full_sprites_line(const GVDP *v, int line, int left, int x0, int native_w,
                                      int width, int force_display, EcMarginSprites *ms)
{
    memset(ms, 0, sizeof(*ms));
    if (!force_display && !(v->reg[1] & 0x40u))
        return;

    int h40          = (v->reg[12] & 0x81u) != 0;
    int im2          = (v->reg[12] & 0x06u) == 0x06u;
    int sh_mode      = (v->reg[12] & 0x08u) != 0;
    int max_sprites  = h40 ? 80 : 64;
    int cell_h_shift = im2 ? 4 : 3;
    uint16_t sat     = (uint16_t)((v->reg[5] & 0x7Fu) << 9);

    uint8_t drawn[GVDP_MAX_WIDTH];
    memset(drawn, 0, (size_t)width);

    int ex = 0;                       /* proximo extra a intercalar (ordenados por ord) */
    int link = 0;
    int any_nonzero_x = 0;
    for (int n = 0; n < max_sprites; ++n) {
        uint16_t e    = (uint16_t)(sat + link * 8);
        uint16_t w0   = ec_vram16(v, e);
        uint16_t w1   = ec_vram16(v, (uint16_t)(e + 2));
        uint16_t attr = ec_vram16(v, (uint16_t)(e + 4));
        uint16_t w3   = ec_vram16(v, (uint16_t)(e + 6));
        if (w0 == 0 && w1 == 0 && attr == 0 && w3 == 0)
            break;

        /* Extras descartados que Sub_E684 teria colocado antes deste indice da SAT. */
        for (; ex < s_extra_act_n && s_extra_act[ex].ord <= (uint16_t)link; ++ex) {
            const EcExtraSpr *x = &s_extra_act[ex];
            ec_draw_sprite_span(v, line, (int)(x->y & 0x3FF) - (im2 ? 256 : 128),
                                (int)x->x - 128, x0, (uint8_t)(x->szw >> 8), x->attr, 0xFF,
                                width, im2, sh_mode, 1, x0, native_w, drawn, ms);
        }

        int y      = (int)(w0 & 0x3FFu) - (im2 ? 256 : 128);
        uint8_t sz = (uint8_t)(w1 >> 8);
        int vsz    = (sz & 3) + 1;
        int height = vsz << cell_h_shift;
        int next   = (int)(w1 & 0x7Fu);

        if (line >= y && line < y + height) {
            if ((w3 & 0x3FFu) == 0u) {
                if (any_nonzero_x)
                    break;
            } else {
                any_nonzero_x = 1;
            }

            /* Sub_E684 grava (screen_x + 128) com sinal 16-bit na SAT; HUD usa âncora fixa `left`. */
            int sx0    = (int)(int16_t)w3 - 128;
            int base_x = s_sat_is_hud[link] ? left : x0;
            ec_draw_sprite_span(v, line, y, sx0, base_x, sz, attr, link, width, im2, sh_mode,
                                0, x0, native_w, drawn, ms);
        }

        if (next == 0 || next >= max_sprites)
            break;
        link = next;
    }

    for (; ex < s_extra_act_n; ++ex) {
        const EcExtraSpr *x = &s_extra_act[ex];
        ec_draw_sprite_span(v, line, (int)(x->y & 0x3FF) - (im2 ? 256 : 128),
                            (int)x->x - 128, x0, (uint8_t)(x->szw >> 8), x->attr, 0xFF,
                            width, im2, sh_mode, 1, x0, native_w, drawn, ms);
    }
}

static void ec_fetch_window_pixel(const GVDP *v, uint16_t base_w, int win_wt, int ht,
                                  int px, int py, int im2,
                                  uint8_t *idx, int *opaque, int *hi)
{
    int shift      = im2 ? 4 : 3;
    int fy_mask    = (1 << shift) - 1;
    int tile_bytes = im2 ? 64 : 32;
    int wpx        = win_wt * 8;
    int hpx        = ht << shift;

    px &= (wpx - 1);
    py &= (hpx - 1);

    uint16_t nt = (uint16_t)(base_w + (uint16_t)(((py >> shift) * win_wt + (px >> 3)) * 2));
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

static void ec_render_ws_scanline_impl(const GVDP *v, int out_line, int bg_line, int native_w, int left, int x0,
                                       int width, int force_display, const EcMarginSprites *ms,
                                       uint32_t *out, int *out_any_plane_opaque)
{
    *out_any_plane_opaque = 0;
    int display_on = force_display || ((v->reg[1] & 0x40u) != 0);
    uint8_t backdrop = (uint8_t)(v->reg[7] & 0x3Fu);
    if (!display_on) {
        uint32_t bg = genesis_dac_cram_to_argb(v->cram[backdrop], GENESIS_DAC_NORMAL);
        for (int ox = 0; ox < width; ++ox)
            out[ox] = bg;
        return;
    }

    int h40     = (v->reg[12] & 0x81u) != 0;
    int im2     = (v->reg[12] & 0x06u) == 0x06u;
    int rline   = im2 ? (bg_line >> 1) : bg_line;
    int sh_mode = (v->reg[12] & 0x08u) != 0;

    int wt          = ec_plane_tiles(v->reg[16] & 0x03u);
    int ht          = ec_plane_tiles((v->reg[16] >> 4) & 0x03u);
    int win_wt      = h40 ? 64 : 32;
    uint16_t base_a = (uint16_t)((v->reg[2] & 0x38u) << 10);
    uint16_t base_b = (uint16_t)((v->reg[4] & 0x07u) << 13);
    uint16_t base_w = (uint16_t)((v->reg[3] & (h40 ? 0x3Eu : 0x3Fu)) << 10);

    uint16_t hbase = (uint16_t)((v->reg[13] & 0x3Fu) << 10);
    int hmode      = v->reg[11] & 0x03;
    int hidx       = (hmode == 0) ? 0 : (hmode == 2) ? (rline & ~7) : rline;
    int hs_a       = (int)(ec_vram16(v, (uint16_t)(hbase + hidx * 4 + 0)) & 0x3FFu);
    int hs_b       = (int)(ec_vram16(v, (uint16_t)(hbase + hidx * 4 + 2)) & 0x3FFu);
    int comp_a     = ec_plane_edge_comp(0u, bg_line, x0 - left);
    int hs_a_adj   = (hs_a - comp_a) & 0x3FF;
    int comp_b     = ec_plane_edge_comp(2u, bg_line, x0 - left);
    int hs_b_adj   = (hs_b - comp_b) & 0x3FF;

    int vs_mask     = im2 ? 0x7FF : 0x3FF;
    int vmode_2cell = (v->reg[11] & 0x04u) != 0;
    int full_va     = (int)(v->vsram[0] & vs_mask);
    int full_vb     = (int)(v->vsram[1] & vs_mask);

    unsigned wx        = v->reg[17];
    unsigned wy        = v->reg[18];
    int window_x       = (int)(wx & 0x1Fu) * 16;
    int window_y       = (int)(wy & 0x1Fu) * 8;
    int window_h_right = (wx & 0x80u) != 0;
    int window_v_in    = (wy & 0x80u) ? (rline >= window_y) : (rline < window_y);
    int hd_rec         = ec_hd_frame_active();

    int any_opaque = 0;
    for (int ox = 0; ox < width; ++ox) {
        int sx = ox - x0;
        int va = full_va;
        int vb = full_vb;
        if (vmode_2cell) {
            int xc  = (sx < 0) ? 0 : (sx >= native_w ? native_w - 1 : sx);
            int col = (xc >> 4) * 2;
            va = (int)(v->vsram[col % GVDP_VSRAM_ENTRIES] & vs_mask);
            vb = (int)(v->vsram[(col + 1) % GVDP_VSRAM_ENTRIES] & vs_mask);
        }

        uint8_t idx_a = 0, idx_b = 0;
        int op_a = 0, op_b = 0, hi_a = 0, hi_b = 0;
        int st_a = 0, st_b = 0, is_win = 0;
        int px_a = sx - hs_a_adj, py_a = bg_line + va;
        int px_b = sx - hs_b_adj, py_b = bg_line + vb;
        int window_h_in = window_h_right ? (sx >= window_x) : (sx < window_x);
        if (sx >= 0 && sx < native_w && (window_h_in || window_v_in)) {
            is_win = 1;
            ec_fetch_window_pixel(v, base_w, win_wt, ht, sx, bg_line, im2, &idx_a, &op_a, &hi_a);
        } else {
            ec_fetch_plane_pixel(v, base_a, 0u, bg_line, sx, hs_a_adj, wt, ht, px_a, py_a, im2, &idx_a, &op_a, &hi_a, &st_a);
        }
        ec_fetch_plane_pixel(v, base_b, 2u, bg_line, sx, hs_b_adj, wt, ht, px_b, py_b, im2, &idx_b, &op_b, &hi_b, &st_b);
        if (op_a || op_b)
            any_opaque = 1;

        uint8_t s_idx = ms ? ms->idx[ox] : 0;
        int s_op = ms ? (ms->op[ox] != 0) : 0;
        int s_hi = ms ? (ms->hi[ox] != 0) : 0;
        int s_sh = ms ? (ms->sh[ox] != 0) : 0;
        int s_hl = ms ? (ms->hl[ox] != 0) : 0;

        uint8_t cidx = backdrop;
        int win_layer = EC_HD_LAYER_NONE;
        if      (s_op && s_hi) { cidx = s_idx; win_layer = EC_HD_LAYER_SPRITE; }
        else if (op_a && hi_a) { cidx = idx_a; win_layer = is_win ? EC_HD_LAYER_NONE : EC_HD_LAYER_PLANE_A; }
        else if (op_b && hi_b) { cidx = idx_b; win_layer = EC_HD_LAYER_PLANE_B; }
        else if (s_op)         { cidx = s_idx; win_layer = EC_HD_LAYER_SPRITE; }
        else if (op_a)         { cidx = idx_a; win_layer = is_win ? EC_HD_LAYER_NONE : EC_HD_LAYER_PLANE_A; }
        else if (op_b)         { cidx = idx_b; win_layer = EC_HD_LAYER_PLANE_B; }

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

        if (hd_rec) {
            int bg_layer = EC_HD_LAYER_NONE, bg_px = 0, bg_py = 0, bg_st = 0;
            if      (op_a && hi_a && !is_win) { bg_layer = EC_HD_LAYER_PLANE_A; bg_px = px_a; bg_py = py_a; bg_st = st_a; }
            else if (op_b && hi_b)            { bg_layer = EC_HD_LAYER_PLANE_B; bg_px = px_b; bg_py = py_b; bg_st = st_b; }
            else if (op_a && !is_win)         { bg_layer = EC_HD_LAYER_PLANE_A; bg_px = px_a; bg_py = py_a; bg_st = st_a; }
            else if (op_b)                    { bg_layer = EC_HD_LAYER_PLANE_B; bg_px = px_b; bg_py = py_b; bg_st = st_b; }
            int spr_lnk = (ms && s_op) ? (int)ms->link[ox] : -1;
            ec_hd_record_pixel(out_line, ox, win_layer, bg_layer, bg_px, bg_py, bg_st, spr_lnk, level);
        }

        out[ox] = genesis_dac_cram_to_argb(v->cram[cidx & 63u], level);
    }
    *out_any_plane_opaque = any_opaque;
}

static void ec_render_ws_scanline(const GVDP *v, int line, int native_w, int left, int x0,
                                  int width, int force_display, const EcMarginSprites *ms,
                                  uint32_t *out)
{
    int any_plane_opaque = 0;
    ec_render_ws_scanline_impl(v, line, line, native_w, left, x0, width, force_display, ms, out, &any_plane_opaque);
    if (line >= 200 && any_plane_opaque)
        s_last_floor_line = line;
    if (line >= 216 && force_display && !any_plane_opaque) {
        int src_line = 2 * s_last_floor_line - line;
        if (src_line < 200)
            src_line = 200;
        if (src_line > s_last_floor_line)
            src_line = s_last_floor_line;
        int dummy = 0;
        ec_render_ws_scanline_impl(v, line, src_line, native_w, left, x0, width, force_display, ms, out, &dummy);
    }
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
    int left = (width - native_w) / 2;
    if (left < 0) left = 0;

    if (line == 0) {
        /* Snapshot por quadro: tudo que o renderer le da WRAM (ver docs/widescreen.md GW.3). */
        s_snap_arena = ec_is_arena_state();
        s_snap_valid = 0;
        if (vdp && s_snap_arena && width > native_w && width <= GVDP_MAX_WIDTH) {
            uint16_t fd64 = ec_ram16(0xFD64u);
            ec_take_layer_snapshot();
            s_snap_native_w = native_w;
            s_snap_x0       = ec_compute_viewport_x0(0, native_w, width);
            s_snap_force_ok = (ec_ram16(0x9B32u) != 0u);
            if (fd64 == s_snap_fd64) {
                memcpy(s_extra_act, s_extra_pend, (size_t)s_extra_pend_n * sizeof(EcExtraSpr));
                s_extra_act_n = s_extra_pend_n;
            } else {
                /* Entrada/troca de arena: a lista pendente pode ser de outro estado. */
                s_extra_act_n = 0;
            }
            s_snap_fd64  = fd64;
            s_snap_valid = 1;
        } else {
            /* Fora de arena widescreen: nada de extras residuais no proximo quadro valido. */
            s_extra_pend_n = 0;
            s_extra_act_n  = 0;
            s_snap_fd64    = 0xFFFFu;
        }
    }

    if (vdp && s_snap_valid && s_snap_arena && width > native_w && width <= GVDP_MAX_WIDTH) {
        if (line == 0)
            ec_refresh_frame_hud_sat(vdp);
        int x0 = s_snap_x0;
        if (line == 0 && ec_hd_is_active())
            ec_hd_begin_frame(vdp, ec_ram16(0xFD64u), width, 224, left, x0, s_sat_is_hud);
        int force_display = (line >= 216 && s_frame_had_display && s_snap_force_ok) ? 1 : 0;
        EcMarginSprites ms;
        ec_eval_full_sprites_line(vdp, line, left, x0, native_w, width, force_display, &ms);
        ec_render_ws_scanline(vdp, line, native_w, left, x0, width, force_display, &ms, out);
        return;
    }

    const uint32_t *src = native_row ? native_row(native_ctx) : NULL;
    if (width > native_w) {
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
    int total = ec_hd_load_manifest(path);
    if (total < 0)
        return -1;
    s_hd_loaded = 1;
    s_hd_total_entries = ec_hd_total_entries();
    s_hd_approved_entries = ec_hd_approved_entries();
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

static int ec_parse_dedither_mode(const char *mode, EcBplusDeditherMode *out)
{
    if (!mode || !*mode || !out)
        return 0;
    if (strcmp(mode, "off") == 0 || strcmp(mode, "0") == 0 || strcmp(mode, "none") == 0) {
        *out = EC_BPLUS_DEDITHER_OFF;
        return 1;
    }
    if (strcmp(mode, "cbod") == 0 || strcmp(mode, "cb") == 0 ||
        strcmp(mode, "checkerboard") == 0 || strcmp(mode, "1") == 0) {
        *out = EC_BPLUS_DEDITHER_CBOD;
        return 1;
    }
    if (strcmp(mode, "both") == 0 || strcmp(mode, "sgenpt") == 0 ||
        strcmp(mode, "on") == 0 || strcmp(mode, "2") == 0) {
        *out = EC_BPLUS_DEDITHER_BOTH;
        return 1;
    }
    return 0;
}

void ec_video_set_bplus_dedither(const char *mode)
{
    EcBplusDeditherMode parsed = EC_BPLUS_DEDITHER_OFF;
    if (ec_parse_dedither_mode(mode, &parsed)) {
        s_bplus_dedither = parsed;
        s_bplus_dedither_cli_set = 1;
    } else {
        fprintf(stderr, "[EC_VIDEO] --bplus-dedither desconhecido '%s' (off|cbod|both)\n",
                mode ? mode : "");
    }
}

static EcBplusDeditherMode ec_resolve_bplus_dedither(void)
{
    if (s_bplus_dedither_cli_set)
        return s_bplus_dedither;
    const char *env = getenv("EC_BPLUS_DEDITHER");
    EcBplusDeditherMode parsed = EC_BPLUS_DEDITHER_OFF;
    if (env && *env && ec_parse_dedither_mode(env, &parsed))
        return parsed;
    return s_bplus_dedither;
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

static int ec_bplus_is_enabled(void)
{
    if (s_bplus_cli_set)
        return s_bplus_enabled;
    const char *env = getenv("EC_NEURAL_BPLUS");
    return (env && (strcmp(env, "1") == 0 || strcmp(env, "on") == 0));
}

static int ec_video_present_gpu_enabled(void)
{
    return ec_bplus_is_enabled() || ec_hd_is_active();
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
    /* Modelo escolhido explicitamente (--bplus-model / EC_BPLUS_MODEL): o ONNX
     * do backend CUDA tem que ser o irmão do mesmo modelo
     * (<base>_weights.bin -> <base>_fp16.onnx | <base>.onnx). Sem irmão, retorna
     * o caminho derivado inexistente: o ORT falha e `auto` cai no GL com os
     * pesos certos, em vez de rodar o v1 silenciosamente. */
    const char *model_env = getenv("EC_BPLUS_MODEL");
    const char *model = s_bplus_model_cli_set ? s_bplus_model_path
                      : (model_env && *model_env ? model_env : NULL);
    if (model) {
        static char derived[512];
        static char derived_fp16[512];
        size_t n = strlen(model);
        static const char k_suffix[] = "_weights.bin";
        size_t ks = sizeof(k_suffix) - 1u;
        if (n >= ks && strcmp(model + n - ks, k_suffix) == 0)
            n -= ks;
        else if (n >= 4u && strcmp(model + n - 4u, ".bin") == 0)
            n -= 4u;
        if (n > sizeof(derived) - 16u)
            n = sizeof(derived) - 16u;
        snprintf(derived_fp16, sizeof(derived_fp16), "%.*s_fp16.onnx", (int)n, model);
        snprintf(derived, sizeof(derived), "%.*s.onnx", (int)n, model);
        FILE *fp = fopen(derived_fp16, "rb");
        if (fp) {
            fclose(fp);
            return derived_fp16;
        }
        return derived;
    }
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

    struct SDL_Texture *base_tex = NULL;
    if (ec_bplus_is_enabled()) {
        if (!s_bplus_checked) {
            s_bplus_checked = 1;
            const char *wpath = ec_resolve_bplus_model_path();
            const char *opath = ec_resolve_bplus_onnx_path();
            double budget = ec_resolve_bplus_budget_ms();
            float intensity = (float)ec_resolve_bplus_intensity();
            EcBplusDeditherMode dedither = ec_resolve_bplus_dedither();
            int backend = ec_resolve_bplus_backend();
            /* G4.2: o caminho ORT/CUDA faz ida-e-volta CPU<->GPU por frame (~15 ms no
             * v3 fast vs ~1,9 ms do GL otimizado), entao `auto` prefere GL e so cai
             * no CUDA se o GL 4.3 nao existir. `cuda` explicito continua disponivel. */
            int ort_ok = (intensity == 1.0f && dedither == EC_BPLUS_DEDITHER_OFF);
            int want_gl = 0;

            if (backend == EC_BPLUS_BACKEND_CUDA) {
                if (ort_ok) {
                    s_bplus_ort = ec_bplus_ort_create(renderer, activation, opath, budget);
                    if (!s_bplus_ort)
                        fprintf(stderr, "[EC_VIDEO] Mode B+ CUDA unavailable; retaining Modo A/B fallback\n");
                } else {
                    want_gl = 1;
                    if (dedither != EC_BPLUS_DEDITHER_OFF)
                        fprintf(stderr, "[EC_VIDEO] CUDA backend does not support --bplus-dedither; using GL\n");
                    else
                        fprintf(stderr, "[EC_VIDEO] CUDA backend requires intensity=1.0 (got %.3f); using GL\n",
                                intensity);
                }
            } else {
                want_gl = 1;
            }

            if (!s_bplus_ort && want_gl)
                s_bplus_gl = ec_bplus_gl_create(renderer, activation, wpath, budget);
            if (!s_bplus_ort && !s_bplus_gl && backend == EC_BPLUS_BACKEND_AUTO && ort_ok) {
                fprintf(stderr, "[EC_VIDEO] B+ GL indisponivel; tentando CUDA/ORT\n");
                s_bplus_ort = ec_bplus_ort_create(renderer, activation, opath, budget);
            }
            if (!s_bplus_ort && !s_bplus_gl && want_gl)
                fprintf(stderr, "[EC_VIDEO] Mode B+ unavailable; retaining Modo A/B fallback\n");

            if (s_bplus_ort)
                ec_bplus_ort_set_intensity(s_bplus_ort, intensity);
            if (s_bplus_gl) {
                ec_bplus_gl_set_intensity(s_bplus_gl, intensity);
                ec_bplus_gl_set_dedither(s_bplus_gl, dedither);
            }
        }
        if (s_bplus_ort)
            base_tex = ec_bplus_ort_process(s_bplus_ort, argb, width, height, stride_px);
        else if (s_bplus_gl)
            base_tex = ec_bplus_gl_process(s_bplus_gl, argb, width, height, stride_px);
    }

    if (ec_hd_is_active()) {
        return ec_hd_composite_gpu(renderer, activation, base_tex,
                                   argb, width, height, stride_px,
                                   &g_machine.vdp, ec_ram16(0xFD64u));
    }
    return base_tex;
}

static void ec_video_present_gpu_destroy(void)
{
    ec_hd_destroy_gpu();
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
