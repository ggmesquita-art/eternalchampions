#ifndef EC_BOSS_H
#define EC_BOSS_H

#include <stddef.h>
#include <stdint.h>

void ec_boss_set_enabled(int enabled);
int  ec_boss_is_enabled(void);
void ec_boss_reset(void);
void ec_boss_apply_rom_patches(void);
void ec_boss_vblank(void);
int  ec_boss_instruction_hook(uint32_t pc);
size_t ec_boss_rb_state_save(void *dst, size_t cap);
int ec_boss_rb_state_load(const void *src, size_t len);

#endif
