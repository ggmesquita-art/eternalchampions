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

#endif /* EC_VIDEO_H */
