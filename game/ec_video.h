#ifndef EC_VIDEO_H
#define EC_VIDEO_H

#include "game_video.h"

extern const GameVideo g_ec_video;

int ec_video_load_hd_manifest(const char *path);
void ec_video_enable_bplus(int enabled);
void ec_video_set_bplus_model(const char *path);
void ec_video_set_bplus_budget_ms(double budget_ms);
void ec_video_set_bplus_intensity(double intensity);
void ec_video_set_bplus_backend(const char *backend);
void ec_video_set_bplus_dedither(const char *mode);

/* Hook passivo (so le g_cpu/g_ram) em $E684/$E7B0 de Sub_E684; ver ec_video.c. */
void ec_video_on_sprite_hook(uint32_t pc);
/* Limpa estado host-only do widescreen (extras/snapshot) apos reset ou load de savestate. */
void ec_video_reset_ws_state(void);

#endif /* EC_VIDEO_H */
