#ifndef EC_HD_H
#define EC_HD_H

#include <stdint.h>
#include "genesis_vdp.h"

struct SDL_Renderer;
struct SDL_Texture;

/* Layer ids passed to ec_hd_record_pixel */
enum {
    EC_HD_LAYER_NONE    = 0,
    EC_HD_LAYER_PLANE_B = 1,
    EC_HD_LAYER_PLANE_A = 2,
    EC_HD_LAYER_SPRITE  = 3,
};

/* Parse pipeline/manifest.json and index approved Mode C assets (lazy PNG/GL load).
 * Returns total entry count on success, or -1 on error. */
int ec_hd_load_manifest(const char *path);

/* Granularidade do Modo C (--hd-mode / EC_HD_MODE):
 *   off    = desliga o Modo C mesmo com manifest carregado;
 *   planes = só planos/céus HD 4x (sprites ficam na base B+/nativa, sem colcha de retalhos);
 *   all    = planos + sprites HD (padrão, comportamento histórico). */
enum { EC_HD_MODE_OFF = 0, EC_HD_MODE_PLANES = 1, EC_HD_MODE_ALL = 2 };
/* Aceita "off" | "planes" | "all"; retorna 1 se válido. CLI tem precedência sobre EC_HD_MODE. */
int ec_hd_set_mode_str(const char *mode);
int ec_hd_get_mode(void);
/* True when a manifest is loaded and has at least one approved HD asset
 * and --hd-mode != off. */
int ec_hd_is_active(void);

/* True when ec_hd_begin_frame() armed the current frame for per-pixel recording. */
int ec_hd_frame_active(void);

int ec_hd_total_entries(void);
int ec_hd_approved_entries(void);

/* Evaluate live VDP VRAM/CRAM planes (64x32 tile match) and SAT sprites (SHA-256 +
 * H-flip + CRAM palette check) once at the start of a frame. */
void ec_hd_begin_frame(const GVDP *vdp, uint16_t state_id,
                       int width, int height, int left, int x0,
                       const uint8_t sat_is_hud[80]);

/* Record exact VDP-priority winner and underlying plane coords for one native pixel. */
void ec_hd_record_pixel(int line, int ox,
                        int win_layer,
                        int bg_layer, int bg_px, int bg_py, int bg_streamed,
                        int spr_link, int level);

/* Composite matched 4x HD planes and 4x HD sprites on GPU over base_tex
 * (Mode B+ 4x output, or raw native argb upscaled 4x when base_tex == NULL).
 * Returns borrowed SDL_Texture* (width*4 x height*4 ARGB8888), or base_tex on fallback. */
struct SDL_Texture *ec_hd_composite_gpu(struct SDL_Renderer *renderer,
                                        struct SDL_Texture *activation,
                                        struct SDL_Texture *base_tex,
                                        const uint32_t *argb,
                                        int width, int height, int stride_px,
                                        const GVDP *fallback_vdp,
                                        uint16_t state_id);

void ec_hd_destroy_gpu(void);

#endif /* EC_HD_H */
