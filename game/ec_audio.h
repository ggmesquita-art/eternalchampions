#ifndef EC_AUDIO_H
#define EC_AUDIO_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    EC_AUDIO_MODE_OFF    = 0, /* 100% autentico (bypass total, custo zero) */
    EC_AUDIO_MODE_DSP    = 1, /* Chip original (BGM+SFX+DAC) com DSP de estudio custo-zero */
    EC_AUDIO_MODE_HYBRID = 2, /* Sintese SoundFont em tempo real + chip original (com DSP) */
    EC_AUDIO_MODE_OST    = 3  /* Sintese SoundFont em tempo real + chip atenuado (preservando SFX/DAC) */
} EcAudioMode;

/* Ciclo de vida e reset (chamados a partir do runner/spec; nunca tocam em WRAM/Z80). */
void ec_audio_init(void);
void ec_audio_shutdown(void);
void ec_audio_reset(void);            /* reset do console: descarta a musica com rampa */
void ec_audio_on_state_loaded(void);  /* load de savestate: preserva bgm/perfil, shadow vira desconhecido */

/* Selecao de modo ("off", "dsp", "hybrid", "ost", case-insensitive).
 * Retorna 1 se valido, 0 se invalido. */
int ec_audio_set_mode_str(const char *mode_str);
EcAudioMode ec_audio_get_mode(void);

/* Carrega SoundFont2 (.sf2) para sintese em tempo real via TinySoundFont.
 * Retorna 1 em sucesso, 0 em falha. */
int ec_audio_load_soundfont(const char *sf2_path);

/* Carrega perfis .toml por bgm_id (e default.toml fallback) de um diretorio.
 * Retorna quantidade de perfis carregados (>= 0) ou -1 em erro. */
int ec_audio_load_profiles_dir(const char *profiles_dir);

/* Instrumentos FM por hash de patch (docs/audio_voice_hash.md). Carrega voices.toml
 * (campos editaveis de cada [voices.<hash>]); marca o caminho como explicito (o default
 * <profiles_dir>/../voices.toml deixa de ser procurado). Retorna n de vozes ou -1. */
int ec_audio_load_voices(const char *path);

/* Self-test do hash (vetor ce6df9ba). Retorna 1 se OK. EC_AUDIO_SELFTEST=1 imprime no init. */
int ec_audio_selftest(void);

/* Hook da mascara de audicao do chip (ex.: audio_mixer_set_audition). Em `hybrid` canais
 * cujo instrumento esta no SF2 ficam mudos no chip: bits0..5 FM1..6, 6 DAC, 7..9 PSG tom,
 * 10 ruido; enabled=0 restaura o chip completo. */
void ec_audio_set_chip_mask_hook(void (*hook)(int enabled, unsigned mask));

/* Controles de mixagem e DSP. */
void ec_audio_set_bgm_vol(float vol);        /* 0.0 .. 2.0, default 1.0 */
void ec_audio_set_sfx_vol(float vol);        /* 0.0 .. 2.0, default 1.0 (ganho do chip) */
/* chip_blend 0.0 .. 1.0. Precedencia (hybrid/ost): override CLI > perfil da faixa >
 * default do usuario > default do modo (0.65 hybrid, 0.18 ost). */
void ec_audio_set_chip_blend(float blend);           /* default do usuario (settings.ini/env) */
void ec_audio_set_chip_blend_override(float blend);  /* --audio-chip-blend (vence tudo) */
void ec_audio_set_dsp_reverb(float amount);  /* 0.0 .. 1.0, default 0.18 */

/* Eventos passivos de BGM (chamados pelos hooks de sound_cmd sem tocar no Z80). */
void ec_audio_on_bgm_play(uint32_t bgm_id);
void ec_audio_on_bgm_pause(int paused);
void ec_audio_on_bgm_stop(int fade);
uint32_t ec_audio_current_bgm_id(void);

/* Observer passivo de registradores FM/PSG (AudioChipWriteObserver). */
void ec_audio_on_chip_write(uint8_t port, uint8_t value,
                            uint32_t cycle_stamp, uint32_t frame_end_cycle);

/* Callback compativel com AudioExtraSource (audio_set_extra_source). */
void ec_audio_mix_callback(int16_t *mix_stereo, size_t frames, double rate);

#endif /* EC_AUDIO_H */
