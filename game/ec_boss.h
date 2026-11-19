#ifndef EC_BOSS_H
#define EC_BOSS_H

#include <stdint.h>

void ec_boss_set_enabled(int enabled);
int  ec_boss_is_enabled(void);
void ec_boss_reset(void);
int  ec_boss_instruction_hook(uint32_t pc);

#endif
