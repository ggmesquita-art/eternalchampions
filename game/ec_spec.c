/*
 * ec_spec.c — GameSpec de Eternal Champions (SEGA, 1993) USA NTSC.
 *
 * ROM: 3 MB (0x300000 bytes), CRC32 0x48F1A42E, GM MK-1145.
 * Vetores ($000000):
 *   Initial SSP = $01000000
 *   EntryPoint  = $00000200
 *   VBlank IRQ6 = $000006B6
 *   HBlank IRQ4 = NULL (confirmado inativo na Fase 1 §5.7; usa VDP Reg 11 H-scroll)
 */

#include "ec_boss.h"
#include "ec_video.h"
#include "game_spec.h"
#include "genesis_runtime.h"
#include "input_map.h"
#include "video/genesis_machine.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t spec_ram16(unsigned a)
{
    a &= 0xFFFFu;
    return (uint16_t)(((uint16_t)g_ram[a] << 8) | g_ram[(a + 1u) & 0xFFFFu]);
}

static int s_boot_init_done = 0;
static int s_diag_boss_sanctuary = 0;

static void ec_on_post_reset(void)
{
    ec_boss_reset();
    if (s_boot_init_done)
        return;
    s_boot_init_done = 1;

    g_input_map.p[0].pad_type = PAD_6BUTTON;
    g_input_map.p[1].pad_type = PAD_6BUTTON;
    machine_set_pad_type(0, 1);
    machine_set_pad_type(1, 1);

    const char *diag_boss = getenv("EC_DIAG_BOSS_SANCTUARY");
    s_diag_boss_sanctuary = (diag_boss && strcmp(diag_boss, "1") == 0);

    const char *unlock_boss = getenv("EC_UNLOCK_BOSS");
    if (unlock_boss && strcmp(unlock_boss, "1") == 0)
        ec_boss_set_enabled(1);

    const char *manifest = getenv("EC_HD_MANIFEST");
    if (manifest && *manifest)
        (void)ec_video_load_hd_manifest(manifest);
}

static void ec_call_entry_point(void)
{
    ec_on_post_reset();
    recomp_call_addr(0x000200u);
}

static void ec_call_vblank(void)
{
    if (s_diag_boss_sanctuary) {
        uint16_t st = spec_ram16(0xFD64u);
        if (st == 18u) {
            s_diag_boss_sanctuary = 0;
        } else if (st == 1u && g_ram[0x978Au] == 9u) {
            g_ram[0x9782u] = 9u;
            g_ram[0x9CBCu] = 0u;
            g_ram[0x9CBDu] = 9u;
        }
    }
    recomp_call_addr(0x0006B6u);
}

static int ec_handle_arg(const char *key, const char *val)
{
    if (!key)
        return 0;
    if (strcmp(key, "--hd-manifest") == 0 && val && *val) {
        (void)ec_video_load_hd_manifest(val);
        return 2;
    }
    if (strcmp(key, "--neural-bplus") == 0) {
        ec_video_enable_bplus(1);
        return 1;
    }
    if (strcmp(key, "--unlock-boss") == 0) {
        ec_boss_set_enabled(1);
        return 1;
    }
    if (strcmp(key, "--bplus-model") == 0 && val && *val) {
        ec_video_set_bplus_model(val);
        ec_video_enable_bplus(1);
        return 2;
    }
    if (strcmp(key, "--bplus-budget-ms") == 0 && val && *val) {
        char *end = NULL;
        double v = strtod(val, &end);
        if (end != val && *end == '\0' && v >= 0.0) {
            ec_video_set_bplus_budget_ms(v);
            return 2;
        }
    }
    return 0;
}

static void ec_fill_frame_record(uint8_t game_data[256])
{
    uint16_t state   = spec_ram16(0xFD64u); /* State_Current */
    uint16_t timer   = spec_ram16(0x9CB4u); /* Fight_RoundTimer */
    uint16_t p1_hp   = spec_ram16(0xAB86u + 0x68u);
    uint16_t p2_hp   = spec_ram16(0xAA52u + 0x68u);
    uint16_t p1_x    = spec_ram16(0xAB86u + 0x42u);
    uint16_t p1_y    = spec_ram16(0xAB86u + 0x46u);
    uint16_t p2_x    = spec_ram16(0xAA52u + 0x42u);
    uint16_t p2_y    = spec_ram16(0xAA52u + 0x46u);

    uint16_t fields[8] = {state, timer, p1_hp, p2_hp, p1_x, p1_y, p2_x, p2_y};
    for (size_t i = 0; i < 8; ++i) {
        game_data[i * 2 + 0] = (uint8_t)(fields[i] >> 8);
        game_data[i * 2 + 1] = (uint8_t)(fields[i] & 0xFFu);
    }
}

const GameSpec g_game_spec = {
    .video                 = &g_ec_video,
    .video_default           = "off",
    .display_name          = "Eternal Champions",
    .short_name            = "EC",
    .expected_rom_crc32    = 0x48F1A42Eu,
    .expected_rom_size     = 0x300000u,
    .tier3_floor_default   = 1,
    .call_entry_point      = ec_call_entry_point,
    .call_vblank           = ec_call_vblank,
    .call_hblank           = NULL,
    .on_post_reset         = ec_on_post_reset,
    .handle_arg            = ec_handle_arg,
    .arg_usage             = "--hd-manifest <path> | --neural-bplus | --bplus-model <bin> | --bplus-budget-ms <ms> | --unlock-boss",
    .instruction_hook      = ec_boss_instruction_hook,
    .fill_frame_record     = ec_fill_frame_record,
    .frame_record_version  = 1u,
};
