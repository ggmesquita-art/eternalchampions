/*
 * ec_boss.c — Opt-in playable Eternal Champion + 4 beast variants (Option A).
 *
 * Activated only when --unlock-boss or EC_UNLOCK_BOSS=1 is passed. When off,
 * ec_boss_instruction_hook() is a single predicted-false branch returning 0,
 * leaving original Genesis gameplay and M3 deterministic parity untouched.
 */

#include "ec_boss.h"
#include "genesis_runtime.h"

#include <stdint.h>

static int s_unlock_boss = 0;
static uint8_t s_ec_form[2] = {0u, 0u};   /* 0=Base, 1=Dragon, 2=Falcon, 3=Shark, 4=Tiger */
static uint8_t s_wrap_to_9[2] = {0u, 0u};
static uint8_t s_prev_ud[2] = {0u, 0u};

static uint16_t ram16(uint32_t addr)
{
    uint32_t a = addr & 0xFFFFu;
    return (uint16_t)(((uint16_t)g_ram[a] << 8) | g_ram[(a + 1u) & 0xFFFFu]);
}

static void ram16_write(uint32_t addr, uint16_t val)
{
    uint32_t a = addr & 0xFFFFu;
    g_ram[a] = (uint8_t)(val >> 8);
    g_ram[(a + 1u) & 0xFFFFu] = (uint8_t)(val & 0xFFu);
}

static void write_pal16(uint16_t dst, const uint16_t pal[16])
{
    for (uint32_t i = 0; i < 16u; ++i)
        ram16_write((uint32_t)dst + i * 2u, pal[i]);
}

static int fighter_slot_from_a5(uint16_t a5)
{
    return (a5 == 0xAA52u) ? 1 : 0;
}

static int select_slot_from_a5(uint16_t a5)
{
    return (g_ram[(uint32_t)(a5 + 0x6Du) & 0xFFFFu] & 0x01u) ? 1 : 0;
}

static int is_cpu_sanctuary_boss(uint16_t a5)
{
    return (a5 == 0xAA52u) &&
           (ram16(0xFD64u) == 0x0012u) &&
           (ram16(0x9CCCu) == 0u) &&
           (ram16(0x9422u) == 0u);
}

static int is_playable_ec_slot(uint16_t a5)
{
    return ((a5 == 0xAB86u) || (a5 == 0xAA52u)) && !is_cpu_sanctuary_boss(a5);
}

/* Move/animation blobs in US NTSC ROM for forms 0..4. */
static const uint32_t k_form_blob[5] = {
    0x05E8A4u, /* Base */
    0x05EFC4u, /* Dragon */
    0x05F57Au, /* Falcon */
    0x05FB7Cu, /* Shark */
    0x060066u, /* Tiger */
};

/* Glowing palette-cycle table pointers (slots P1/P2 x forms 0..4). */
static const uint32_t k_form_pal1[2][5] = {
    { 0x043CFAu, 0x043D30u, 0x043D66u, 0x043D9Cu, 0x043DD2u },
    { 0x043D12u, 0x043D48u, 0x043D7Eu, 0x043DB4u, 0x043DEAu },
};

static const uint32_t k_form_pal2[2][5] = {
    { 0x043E08u, 0x043E3Eu, 0x043E74u, 0x043EAAu, 0x043EE0u },
    { 0x043E20u, 0x043E56u, 0x043E8Cu, 0x043EC2u, 0x043EF8u },
};

/* Elemental portrait palettes (Genesis 0BGR) for Character Select / VS portrait. */
static const uint16_t k_portrait_pal[5][16] = {
    { /* 0: Base (original Sub_030AF4[9]) */
      0x0466u, 0x0424u, 0x0844u, 0x0886u, 0x08C8u, 0x0E6Cu, 0x06EEu, 0x0200u,
      0x0E84u, 0x00E0u, 0x08ACu, 0x088Au, 0x0AE0u, 0x0CEEu, 0x0444u, 0x0EEEu },
    { /* 1: Dragon (fire orange/gold) */
      0x0246u, 0x0024u, 0x0048u, 0x006Cu, 0x028Eu, 0x00CEu, 0x00ECu, 0x0002u,
      0x004Eu, 0x00AEu, 0x026Eu, 0x028Cu, 0x00CEu, 0x08EEu, 0x0444u, 0x0EEEu },
    { /* 2: Falcon (emerald green) */
      0x0262u, 0x0040u, 0x0062u, 0x0284u, 0x04C4u, 0x06E6u, 0x0AEAu, 0x0020u,
      0x00A2u, 0x00E4u, 0x04A4u, 0x0282u, 0x06E8u, 0x0CEEu, 0x0444u, 0x0EEEu },
    { /* 3: Shark (electric cyan/aqua) */
      0x0642u, 0x0420u, 0x0840u, 0x0A80u, 0x0CC2u, 0x0EC4u, 0x0EE8u, 0x0200u,
      0x0E80u, 0x0EC0u, 0x0CA4u, 0x0A84u, 0x0EE6u, 0x0EEEu, 0x0444u, 0x0EEEu },
    { /* 4: Tiger (crimson red/amber) */
      0x0226u, 0x0004u, 0x0008u, 0x002Cu, 0x002Eu, 0x006Eu, 0x00AEu, 0x0002u,
      0x000Eu, 0x008Eu, 0x024Cu, 0x004Au, 0x00AEu, 0x04EEu, 0x0444u, 0x0EEEu },
};

/* Dark steel/bronze armor shift (indices 2..8) for P2 in same-form mirror matches. */
static const uint16_t k_p2_mirror_armor[7] = {
    0x0022u, 0x0224u, 0x0446u, 0x068Au, 0x046Cu, 0x068Eu, 0x08CEu
};

void ec_boss_set_enabled(int enabled)
{
    s_unlock_boss = enabled ? 1 : 0;
}

int ec_boss_is_enabled(void)
{
    return s_unlock_boss;
}

void ec_boss_reset(void)
{
    s_ec_form[0] = 0u;
    s_ec_form[1] = 0u;
    s_wrap_to_9[0] = 0u;
    s_wrap_to_9[1] = 0u;
    s_prev_ud[0] = 0u;
    s_prev_ud[1] = 0u;
}

int ec_boss_instruction_hook(uint32_t pc)
{
    if (!s_unlock_boss)
        return 0;

    switch (pc) {
    case 0x030F1Eu:
    case 0x030F48u: {
        int slot = (pc == 0x030F48u) ? 1 : 0;
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        uint8_t pad = g_ram[slot ? 0xFD5Bu : 0xFD59u];
        if (ram16((uint32_t)a5 + 0x84u) == 9u) {
            uint8_t ud = (uint8_t)(pad & 0x03u);
            uint8_t edge = (uint8_t)(ud & (uint8_t)~s_prev_ud[slot]);
            s_prev_ud[slot] = ud;
            if (edge & 0x02u)
                s_ec_form[slot] = (uint8_t)((s_ec_form[slot] + 1u) % 5u);
            else if (edge & 0x01u)
                s_ec_form[slot] = (uint8_t)((s_ec_form[slot] + 4u) % 5u);
            if (edge & 0x03u) {
                write_pal16(slot ? 0xF076u : 0xF096u, k_portrait_pal[s_ec_form[slot]]);
                ram16_write(0xEF32u, 0xFFFFu);
            }
        } else {
            s_prev_ud[slot] = 0u;
        }
        return 0;
    }

    case 0x030F7Eu: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        ram16_write((uint32_t)a5 + 0x84u, 9u);
        return 0;
    }

    case 0x030F90u: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        int slot = select_slot_from_a5(a5);
        uint16_t cur = ram16((uint32_t)a5 + 0x84u);
        if (cur == 8u) {
            s_wrap_to_9[slot] = 1u;
        } else if (cur == 9u) {
            ram16_write((uint32_t)a5 + 0x84u, 8u);
            s_wrap_to_9[slot] = 0u;
        } else {
            s_wrap_to_9[slot] = 0u;
        }
        return 0;
    }

    case 0x030F9Eu: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        int slot = select_slot_from_a5(a5);
        if (s_wrap_to_9[slot]) {
            ram16_write((uint32_t)a5 + 0x84u, 9u);
            s_wrap_to_9[slot] = 0u;
        }
        return 0;
    }

    case 0x03104Eu: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        int slot = select_slot_from_a5(a5);
        if (ram16((uint32_t)a5 + 0x80u) == 0u)
            ram16_write((uint32_t)a5 + 0x46u, 0x0054u);
        if (ram16((uint32_t)a5 + 0x84u) == 9u && ram16((uint32_t)a5 + 0x88u) != 0u) {
            uint8_t f = (slot == 1 && ram16(0x9CCCu) == 0u &&
                         ram16(0x9422u) == 0u && ram16((uint32_t)a5 + 0x80u) != 0u)
                            ? 0u
                            : s_ec_form[slot];
            write_pal16(slot ? 0xF076u : 0xF096u, k_portrait_pal[f]);
            ram16_write(0xEF32u, 0xFFFFu);
        }
        return 0;
    }

    case 0x02E270u:
    case 0x02E574u: {
        if (ram16(0x9CBCu) == 9u) {
            uint8_t f2 = (ram16(0x9CCCu) == 0u && ram16(0x9422u) == 0u) ? 0u : s_ec_form[1];
            write_pal16(0x9BF0u, k_portrait_pal[f2]);
        }
        if (ram16(0x9CBEu) == 9u)
            write_pal16(0x9C10u, k_portrait_pal[s_ec_form[0]]);
        return 0;
    }

    case 0x02E2DAu: {
        uint16_t p1obj = ram16(0x9B80u);
        if (ram16((uint32_t)p1obj + 0x84u) == 9u && (g_cpu.D[5] & 0xFFFFu) == 8u)
            g_cpu.D[5] = (g_cpu.D[5] & 0xFFFF0000u) | 9u;
        return 0;
    }

    case 0x043C12u:
    case 0x043C76u: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        if (is_playable_ec_slot(a5)) {
            int slot = fighter_slot_from_a5(a5);
            uint8_t f = s_ec_form[slot];
            ram16_write((uint32_t)a5 + 0x82u, f);
            g_cpu.A[0] = k_form_blob[f];
        }
        return 0;
    }

    case 0x043C5Au:
    case 0x043CDCu: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        if (is_playable_ec_slot(a5)) {
            int slot = fighter_slot_from_a5(a5);
            g_cpu.A[0] = k_form_blob[s_ec_form[slot]];
        }
        return 0;
    }

    case 0x043F4Cu: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        if (is_playable_ec_slot(a5))
            g_cpu.SR &= (uint16_t)~SR_Z;
        return 0;
    }

    case 0x044204u:
    case 0x044218u:
    case 0x044246u:
    case 0x04425Au: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        if (is_playable_ec_slot(a5)) {
            int slot = fighter_slot_from_a5(a5);
            int pal2 = (pc == 0x044218u || pc == 0x04425Au) ? 1 : 0;
            g_cpu.A[0] = pal2 ? k_form_pal2[slot][s_ec_form[slot]]
                              : k_form_pal1[slot][s_ec_form[slot]];
        }
        return 0;
    }

    case 0x01816Au: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        if (a5 == 0xAA52u && is_playable_ec_slot(a5) &&
            ram16(0x9CBEu) == 9u && ram16(0x9CBCu) == 9u &&
            s_ec_form[0] == s_ec_form[1]) {
            for (uint32_t i = 2u; i <= 8u; ++i)
                ram16_write(0x9BF0u + i * 2u, k_p2_mirror_armor[i - 2u]);
        }
        return 0;
    }

    case 0x0444D0u: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        if (is_playable_ec_slot(a5))
            ram16_write((uint32_t)a5 + 0x82u, 5u);
        return 0;
    }

    case 0x04453Cu: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        if (is_playable_ec_slot(a5))
            g_cpu.SR |= (uint16_t)SR_Z;
        return 0;
    }

    case 0x01F19Au:
        if (ram16(0x9CBEu) >= 9u || ram16(0x9CBCu) >= 9u)
            return 1;
        return 0;

    case 0x01F7A4u:
        if ((g_cpu.D[0] & 0xFFFFu) >= 9u)
            g_cpu.D[0] &= 0xFFFF0000u;
        return 0;

    default:
        return 0;
    }
}
