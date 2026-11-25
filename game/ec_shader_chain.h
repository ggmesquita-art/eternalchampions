#ifndef EC_SHADER_CHAIN_H
#define EC_SHADER_CHAIN_H

/*
 * ec_shader_chain — cadeia de shaders multi-pass compativel com presets RetroArch
 * .glslp + .glsl (GLSL legado do libretro). Extra host-only: so le o frame ja
 * renderizado, nunca toca no estado do jogo.
 *
 * Configuracao (texto apenas, sem GL):  CLI > env > settings.ini > nenhum.
 */

#include <stdint.h>

struct SDL_Renderer;
struct SDL_Texture;

/* --- configuracao (chamado pelo ec_spec.c) --- */
void ec_shader_chain_set_preset_cli(const char *preset);        /* --shader-preset */
void ec_shader_chain_add_param_cli(const char *name_eq_value);  /* --shader-param NOME=valor */
void ec_shader_chain_set_settings(const char *preset, const char *params); /* [ec.video] */

/* Resolve e parseia o preset (uma vez) e diz se ha cadeia ativa. Barato apos a 1a chamada. */
int ec_shader_chain_active(void);

/* Extensoes do preset (ec_bplus*, ignoradas pelo RetroArch). Retorna 1 se o preset tem alguma.
 * model: caminho resolvido (ou NULL); dedither: "off|cbod|both" (ou NULL);
 * intensity: <0 se ausente; enable: -1 ausente, 0/1. */
int ec_shader_chain_get_bplus(const char **model, const char **dedither,
                              double *intensity, int *enable);

/* Executa a cadeia. base_tex = saida B+/Modo C (ou NULL). Retorna a textura SDL final
 * (target) ou NULL se a cadeia estiver desativada/falhou (chamador mantem base_tex). */
struct SDL_Texture *ec_shader_chain_process(struct SDL_Renderer *renderer,
                                            struct SDL_Texture *base_tex,
                                            const uint32_t *argb,
                                            int width, int height, int stride_px);

void ec_shader_chain_destroy(void);

#endif /* EC_SHADER_CHAIN_H */
