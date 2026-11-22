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
#include "m68k_interp.h"
#include "video/genesis_machine.h"

extern int      game_dispatch_table_size(void);
extern uint32_t game_dispatch_table_addr(int i);

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

static uint32_t spec_ram32(unsigned a)
{
    a &= 0xFFFFu;
    return ((uint32_t)g_ram[a] << 24) |
           ((uint32_t)g_ram[(a + 1u) & 0xFFFFu] << 16) |
           ((uint32_t)g_ram[(a + 2u) & 0xFFFFu] << 8) |
           (uint32_t)g_ram[(a + 3u) & 0xFFFFu];
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

    /* Patches de ROM opt-in (clamp do Background + lista de nomes off-label). */
    ec_boss_apply_rom_patches();

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
    ec_boss_vblank();
    if (s_diag_boss_sanctuary) {
        uint16_t st = spec_ram16(0xFD64u);
        if (st == 18u) {
            s_diag_boss_sanctuary = 0;
        } else if (st == 1u && g_ram[0x978Au] == 9u) {
            static int fired = 0;
            static uint32_t first_frame = 0;
            uint32_t fr = (uint32_t)((uint32_t)g_ram[0xFD72u] << 24 |
                                     (uint32_t)g_ram[0xFD73u] << 16 |
                                     (uint32_t)g_ram[0xFD74u] << 8 |
                                     (uint32_t)g_ram[0xFD75u]);
            if (fired == 0) first_frame = fr;
            fired++;
            if (fired == 1)
                fprintf(stderr, "[BOSS-HOOK] first fire frame=$%08X st=%d 978A=%02X\n", first_frame, (int)st, g_ram[0x978Au]);
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
    if (strcmp(key, "--bplus-intensity") == 0 && val && *val) {
        char *end = NULL;
        double v = strtod(val, &end);
        if (end != val && *end == '\0' && v >= 0.0) {
            ec_video_set_bplus_intensity(v);
            return 2;
        }
    }
    if (strcmp(key, "--bplus-backend") == 0 && val && *val) {
        ec_video_set_bplus_backend(val);
        return 2;
    }
    return 0;
}

static uint32_t ec_save_resume_pc(uint8_t game_mode)
{
    (void)game_mode;
    return 0x00255Eu; /* Main_OuterLoop */
}

static int ec_is_native_entry(uint32_t addr)
{
    int lo = 0;
    int hi = game_dispatch_table_size();
    addr &= 0xFFFFFFu;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        uint32_t entry = game_dispatch_table_addr(mid);
        if (entry < addr)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < game_dispatch_table_size() && game_dispatch_table_addr(lo) == addr;
}

static void ec_release_z80_bus_if_locked(void)
{
    if (spec_ram16(0xFD6Au) != 0u || g_machine.bus.z80_busreq != 0u) {
        g_ram[0xFD6Au] = 0u;
        g_ram[0xFD6Bu] = 0u;
        m68k_write16(0x00A11100u, 0u);
    }
}

static uint32_t ec_unwind_to_clean_pc(void)
{
    uint32_t sp = g_cpu.A[7] & 0xFFFFFFu;
    if (sp >= 0xFF0000u && sp < 0x1000000u && !(sp & 1u)) {
        /* Priority 1: saved inside Main_FrameLoop ($00FD24); unwind movem + return to caller tail. */
        static const uint32_t k_ml_rets[] = {
            0x00FCEEu, /* Frame_RunN after bsr Main_FrameLoop */
            0x00FD08u, /* Frame_WaitKey after bsr Main_FrameLoop */
            0x00FE34u, /* FrameRun loop after bsr Main_FrameLoop */
            0x00FE4Au, /* FrameRun tail after bsr Main_FrameLoop */
        };
        for (uint32_t a = sp; a + 4u <= 0x1000000u; a += 2u) {
            uint32_t ret = spec_ram32(a) & 0xFFFFFFu;
            int matched = 0;
            for (size_t i = 0; i < sizeof(k_ml_rets) / sizeof(k_ml_rets[0]); ++i) {
                if (ret == k_ml_rets[i]) {
                    matched = 1;
                    break;
                }
            }
            if (matched && a >= 0xFF003Cu && spec_ram32(a - 4u) == 0x00C00000u) {
                for (unsigned i = 0; i < 8u; ++i)
                    g_cpu.D[i] = spec_ram32(a - 60u + i * 4u);
                for (unsigned i = 0; i < 7u; ++i)
                    g_cpu.A[i] = spec_ram32(a - 28u + i * 4u);
                g_cpu.A[7] = a + 4u;
                g_cpu.SR = (uint16_t)((g_cpu.SR & ~0x0700u) | 0x2000u);
                ec_release_z80_bus_if_locked();
                return ret;
            }
        }

        /* Priority 2: saved inside VBlank_WaitFrames ($00CB42). */
        if (sp + 4u <= 0x1000000u) {
            static const uint32_t k_vbl_rets[] = {
                0x00FCEAu, /* Frame_RunN after jsr VBlank_WaitFrames */
                0x00FD04u, /* Frame_WaitKey after jsr VBlank_WaitFrames */
                0x00FE2Eu, /* FrameRun loop after jsr VBlank_WaitFrames */
                0x00FE44u, /* FrameRun after jsr VBlank_WaitFrames */
                0x00FE52u, /* FrameRun tail after jsr VBlank_WaitFrames */
            };
            uint32_t top = spec_ram32(sp) & 0xFFFFFFu;
            for (size_t i = 0; i < sizeof(k_vbl_rets) / sizeof(k_vbl_rets[0]); ++i) {
                if (top == k_vbl_rets[i]) {
                    g_ram[0xFD76u] = 0u;
                    g_ram[0xFD77u] = 0u;
                    g_cpu.A[7] = sp + 4u;
                    g_cpu.SR = (uint16_t)((g_cpu.SR & ~0x0700u) | 0x2000u);
                    ec_release_z80_bus_if_locked();
                    return top;
                }
            }
        }

        /* Priority 3: saved mid-state-init/decompression under State_SetState ($00FE74). */
        for (uint32_t a = sp; a + 8u <= 0x1000000u; a += 2u) {
            if ((spec_ram32(a) & 0xFFFFFFu) == 0x0024ECu &&
                (spec_ram32(a + 4u) & 0xFFFFFFu) == 0x002306u) {
                g_cpu.A[7] = a;
                g_cpu.A[6] = 0x00C00000u;
                g_cpu.D[0] = spec_ram16(0xFD64u);
                g_cpu.SR = 0x2000u;
                ec_release_z80_bus_if_locked();
                return 0x00FE74u; /* State_SetState */
            }
        }
    }

    /* Priority 4: cold boot fallback (pre-Sub_246E early boot snapshot). */
    g_cpu.A[7] = 0x01000000u;
    g_cpu.A[6] = 0x00C00000u;
    g_cpu.SR = 0x2700u;
    ec_release_z80_bus_if_locked();
    return 0x002230u; /* Main_Entry */
}

static void ec_call_resume(uint32_t resume_pc)
{
    (void)resume_pc;
    g_cpu.PC = ec_unwind_to_clean_pc() & 0xFFFFFFu;
    g_rte_pending = 0;
    g_split_sp_popped = 0;
    fprintf(stderr, "[EC-RESUME] unwound -> PC=$%06X A7=$%08X A6=$%08X SR=$%04X st=%u\n",
            g_cpu.PC, g_cpu.A[7], g_cpu.A[6], g_cpu.SR, (unsigned)spec_ram16(0xFD64u));

    for (;;) {
        uint32_t pc = g_cpu.PC & 0xFFFFFFu;
        if (ec_is_native_entry(pc)) {
            uint32_t pre_sp = g_cpu.A[7];
            g_rte_pending = 0;
            g_split_sp_popped = 0;
            recomp_call_addr(pc);
            g_rte_pending = 0;
            g_split_sp_popped = 0;
            if (g_cpu.A[7] == pre_sp) {
                uint32_t sp = g_cpu.A[7] & 0xFFFFFFu;
                if (sp >= 0xFF0000u && sp + 4u <= 0x1000000u) {
                    g_cpu.PC = m68k_read32(g_cpu.A[7]) & 0xFFFFFFu;
                    g_cpu.A[7] += 4u;
                } else {
                    break;
                }
            }
            continue;
        }
        M68kiStatus st = m68k_interp_step();
        if (st != M68KI_OK) {
            fprintf(stderr, "[EC-RESUME] interp halt st=%d PC=$%06X op=$%04X A7=$%08X\n",
                    (int)st, g_m68ki_bad_pc, g_m68ki_bad_op, g_cpu.A[7]);
            break;
        }
    }
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
    .dispatch_main_loop_pc = 0x000255Eu, /* Main_OuterLoop: re-enter when the
                                            loop unwinds (file-load resumes
                                            lack the boot chain's tail
                                            frames; the win-sequence rts must
                                            land somewhere that loops) */
    .save_resume_pc        = ec_save_resume_pc,
    .call_resume           = ec_call_resume,
    .call_vblank           = ec_call_vblank,
    .call_hblank           = NULL,
    .on_post_reset         = ec_on_post_reset,
    .handle_arg            = ec_handle_arg,
    .arg_usage             = "--hd-manifest <path> | --neural-bplus | --bplus-model <bin> | --bplus-backend <auto|cuda|gl> | --bplus-budget-ms <ms> | --bplus-intensity <f> | --unlock-boss",
    .instruction_hook      = ec_boss_instruction_hook,
    .rb_state_save         = ec_boss_rb_state_save,
    .rb_state_load         = ec_boss_rb_state_load,
    .fill_frame_record     = ec_fill_frame_record,
    .frame_record_version  = 1u,
};
