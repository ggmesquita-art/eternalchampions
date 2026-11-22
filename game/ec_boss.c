/*
 * ec_boss.c — Opt-in playable Eternal Champion + 4 beast variants (Option A).
 *
 * Activated only when --unlock-boss or EC_UNLOCK_BOSS=1 is passed. When off,
 * ec_boss_instruction_hook() is a single predicted-false branch returning 0,
 * leaving original Genesis gameplay and M3 deterministic parity untouched.
 */

#include "ec_boss.h"
#include "genesis_runtime.h"
#include "genesis_vdp.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
           (ram16(0x9CB0u) == 0u) &&
           (ram16(0x9CCCu) == 0u) &&
           (ram16(0x9422u) == 0u);
}

static uint16_t s_holo_saved_977c = 0u;
static uint16_t s_holo_saved_9384 = 0u;
static uint16_t s_holo_saved_9378 = 0u;
static uint8_t  s_holo_saved_9383 = 0u;

static int is_vs_offlabel_sanctuary(void)
{
    return (ram16(0xFD64u) == 0x0012u) &&
           (ram16(0x9CCCu) != 2u) &&
           (ram16(0x9CB0u) != 0u || ram16(0x9424u) != 0u);
}

typedef struct {
    int      phase;   /* 0=idle, 1=init/dma, 2=fly_to_window, 3=shatter_exit, 4=settle_finish */
    int      timer;
    uint16_t victim;  /* 0xAB86 or 0xAA52 */
    int16_t  start_x;
    int16_t  start_y;
    int16_t  cur_x;
    int16_t  cur_y;
} SancOverkillState;

static SancOverkillState s_sanc_ok = {0};

static int is_vs_offlabel_holo(void)
{
    return (ram16(0xFD64u) == 0x000Eu) &&
           (ram16(0x9CCCu) != 2u) &&
           (ram16(0x9CB0u) != 4u);
}

/* Replicates Stage_ExitCleanup ($C578) for off-label VS/Tournament fights
 * (State 14 Holo-Trainer and State 18 Sanctuary) so Best-of-N rounds and menu
 * return work identically to normal arenas. */
static void offlabel_vs_exit_cleanup(uint16_t arena_state, int undo_round_inc)
{
    static const uint16_t k_win_target[5] = { 1u, 2u, 3u, 6u, 11u };
    static const uint16_t k_max_round[5]  = { 0u, 2u, 4u, 10u, 20u };
    static const uint16_t k_exit_9cb2[4]  = { 0u, 2u, 2u, 1u };

    if (undo_round_inc)
        ram16_write(0x9352u, (uint16_t)(ram16(0x9352u) - 1u));

    g_ram[0xFD69u] &= (uint8_t)~0x80u;

    uint8_t cfg   = g_ram[0x936Du];
    uint16_t p1w  = ram16(0x9350u);
    uint16_t p2w  = ram16(0x934Eu);
    uint16_t tourn = ram16(0x9424u);
    uint16_t gmode = ram16(0x9CB0u);

    int match_over = 0;
    if (cfg == 0u) {
        if (tourn == 0u || p1w == 1u || p2w == 1u) {
            match_over = 1;
        } else {
            ram16_write(0x9352u, 0xFFFFu);
        }
    } else if (cfg <= 4u) {
        uint16_t target = k_win_target[cfg];
        if (p1w == target || p2w == target)
            match_over = 1;
    } else {
        match_over = 1;
    }

    if (!match_over && (gmode == 2u || gmode == 3u) &&
        cfg <= 4u && ram16(0x9352u) == k_max_round[cfg]) {
        match_over = 1;
    }

    if (!match_over) {
        ram16_write(0x9352u, (uint16_t)(ram16(0x9352u) + 1u));
        ram16_write(0x9CB6u, arena_state);
        return;
    }

    if (tourn != 0u && tourn != 3u) {
        ram16_write(0x9CB6u, 0x001Du);
    } else {
        ram16_write(0x9CB2u, (gmode < 4u) ? k_exit_9cb2[gmode] : 2u);
        ram16_write(0x9CB6u, 0x0001u);
    }
}

static void sanctuary_vs_exit_cleanup(int undo_round_inc)
{
    offlabel_vs_exit_cleanup(0x0012u, undo_round_inc);
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
    /* As sub-formas (bestas) usam muitos tiles de sprite por linha; em cenas
     * carregadas (névoa do Trident etc.) o limite de hardware de 20
     * sprites/scanline descarta pedaços do corpo. O modo unlock-boss é
     * opt-in e já desvia do hardware — aqui desligamos o limite junto.
     * EC_LIMIT_SPRITES=1 restaura o comportamento autêntico (diagnóstico).
     * (O jogo base sem a flag permanece 100% autêntico.) */
    const char *lim = getenv("EC_LIMIT_SPRITES");
    int limited = (lim && strcmp(lim, "0") != 0 && strcmp(lim, "off") != 0);
    gvdp_set_unlimited_sprites(s_unlock_boss && !limited);
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
    s_sanc_ok.phase = 0;
}

/* ---------------------------------------------------------------------------
 * Cenários off-label no Background do VS (opt-in --unlock-boss)
 * ---------------------------------------------------------------------------
 * O menu grava a seleção do Background em $936F (0=Random, 1..9 = arenas
 * normais) com clamp max=9 lido da definição do item (ROM $2E8AC / $2EC7A) e
 * nomes numa lista de 10 ponteiros em $2EAA2 (apontada pelos LONGs das defs
 * em $2E8B0 / $2EC7E). O commit (loc_02E41A) faz $9CB6 = tabela[$936F]
 * ($2E406, 10 entradas) e o Main_BootRetry vira estado.
 *
 * Com --unlock-boss: clamp 9→11, lista realocada para espaço livre da ROM
 * ($257051, 241 bytes zerados) com 12 entradas + strings "HOLO-TRN" e
 * "SANCTUARY" (as entradas 10/11 originais sobreporiam a string "Random"),
 * commit 10→estado 14 (luta no Holo-Trainer) e 11→estado 18 (santuário).
 * Nada em RAM: o jogo usa $FFCF3C-$FFCF7E (verificado com sentinel — a lista
 * em RAM corrompia sprites do intro e da arena). */
static const char k_offlabel_names[] =
    "HOLO-TRN\0"
    "SANCTUARY\0";

#define OFFLABEL_LIST_ROM   0x257051u  /* espaço livre (241 bytes zerados) */
#define OFFLABEL_NAME0_ROM  (OFFLABEL_LIST_ROM + 48u)
#define OFFLABEL_NAME1_ROM  (OFFLABEL_NAME0_ROM + 9u)

static const uint32_t k_offlabel_strings[10] = {
    0x002EACAu, 0x002EAD4u, 0x002EADEu, 0x002EAE8u, 0x002EAF2u,
    0x002EAFCu, 0x002EB06u, 0x002EB10u, 0x002EB1Au, 0x002EB24u,
};

static void rom_write_u32(uint8_t *rom, uint32_t addr, uint32_t val)
{
    rom[addr + 0u] = (uint8_t)(val >> 24);
    rom[addr + 1u] = (uint8_t)(val >> 16);
    rom[addr + 2u] = (uint8_t)(val >> 8);
    rom[addr + 3u] = (uint8_t)(val);
}

static void offlabel_patch_rom(void)
{
    extern uint8_t g_rom[0x400000];
    /* EC_OFFLABEL_TEST (bits): 1=clamp 9→11, 4=lista realocada para a ROM
     * via def. Default = 5. */
    const char *t = getenv("EC_OFFLABEL_TEST");
    int mode = t ? atoi(t) : 5;
    if (mode & 1) {
        /* clamp max do Background: 9 → 11 (VS 2P: def em $2E8AC; outro menu: $2EC7A) */
        g_rom[0x2E8ACu] = 0x00u; g_rom[0x2E8ADu] = 0x0Bu;
        g_rom[0x2EC7Au] = 0x00u; g_rom[0x2EC7Bu] = 0x0Bu;
    }
    if (!(mode & 4))
        return;
    /* Lista de 12 entradas em espaço livre da ROM + strings novas. */
    for (int i = 0; i < 10; ++i)
        rom_write_u32(g_rom, OFFLABEL_LIST_ROM + 4u * i, k_offlabel_strings[i]);
    rom_write_u32(g_rom, OFFLABEL_LIST_ROM + 40u, OFFLABEL_NAME0_ROM);
    rom_write_u32(g_rom, OFFLABEL_LIST_ROM + 44u, OFFLABEL_NAME1_ROM);
    memcpy(&g_rom[OFFLABEL_NAME0_ROM], &k_offlabel_names[0], 9u);
    memcpy(&g_rom[OFFLABEL_NAME1_ROM], &k_offlabel_names[9], 10u);
    /* Os LONGs das defs do item Background passam a apontar para a lista
     * realocada (o renderer lê via m68k_read32 → g_rom, então os patches
     * são visíveis). */
    rom_write_u32(g_rom, 0x2E8B0u, OFFLABEL_LIST_ROM);
    rom_write_u32(g_rom, 0x2EC7Eu, OFFLABEL_LIST_ROM);
}

void ec_boss_apply_rom_patches(void)
{
    fprintf(stderr, "[OFFLABEL-DBG] apply_rom_patches: unlock=%d\n", s_unlock_boss);
    if (s_unlock_boss)
        offlabel_patch_rom();
}

/* Recupera cadeias de tasks de lutador já corrompidas em saves legados
 * (ex.: native_save_4.bin e native_save_9.bin do Energy Drain do Midknight):
 * o 5º nó (t4, next == 0) deve sempre executar loc_05D8AC ($05D8B2, sprites/
 * colisão), e se t1 ficou preso em $04EF16 esperando bit 7 de ($A8, a5) com
 * a animação t2 já finalizada ($00DC10/$00DC14), libera o bit 7 para retomar. */
static void sanitize_corrupted_fighter_tasks(void)
{
    uint16_t st = ram16(0xFD64u);
    if (st != 14u && !(st >= 17u && st <= 27u))
        return;

    static const uint16_t k_slots[2] = { 0xAB86u, 0xAA52u };
    for (int s = 0; s < 2; ++s) {
        uint16_t a5 = k_slots[s];
        uint16_t obj_id = ram16((uint32_t)a5 + 0x40u);
        if (obj_id < 0x0002u || obj_id > 0x000Cu)
            continue;

        uint16_t nodes[5];
        uint16_t cur = ram16((uint32_t)a5 + 0x7Au);
        int ok = 1;
        for (int i = 0; i < 5; ++i) {
            if (cur < 0xE000u || cur > 0xEFF0u) {
                ok = 0;
                break;
            }
            nodes[i] = cur;
            cur = ram16(cur);
        }
        if (!ok || cur != 0u)
            continue;

        uint16_t t1 = nodes[1];
        uint16_t t2 = nodes[2];
        uint16_t t4 = nodes[4];
        uint32_t pc4 = ((uint32_t)ram16((uint32_t)t4 + 2u) << 16) |
                       (uint32_t)ram16((uint32_t)t4 + 4u);
        if (pc4 != 0x0005D8ACu && pc4 != 0x0005D8B2u) {
            ram16_write((uint32_t)t4 + 2u, 0x0005u);
            ram16_write((uint32_t)t4 + 4u, 0xD8B2u);
            ram16_write((uint32_t)t4 + 6u, 0u);
            ram16_write(0xAB86u + 0x12u, 0xFFFFu);
            ram16_write(0xAA52u + 0x12u, 0xFFFFu);

            uint32_t pc1 = ((uint32_t)ram16((uint32_t)t1 + 2u) << 16) |
                           (uint32_t)ram16((uint32_t)t1 + 4u);
            uint32_t pc2 = ((uint32_t)ram16((uint32_t)t2 + 2u) << 16) |
                           (uint32_t)ram16((uint32_t)t2 + 4u);
            if (pc1 == 0x0004EF16u &&
                (pc2 == 0x0000DC10u || pc2 == 0x0000DC14u)) {
                g_ram[((uint32_t)a5 + 0xA8u) & 0xFFFFu] |= 0x80u;
                ram16_write((uint32_t)t1 + 6u, 0u);
            }
        }
    }
}

void ec_boss_vblank(void)
{
    if (s_unlock_boss)
        sanitize_corrupted_fighter_tasks();
}

/* Host-side adapter state que um tick lê/escreve (formas do EC): precisa
 * round-trip pelo save/load, senão um save como Dragon volta como Base e o
 * patch de paleta/anim é aplicado com a forma errada (sprites quebrados). */
#define EC_BOSS_RB_STATE_LEN 7u
size_t ec_boss_rb_state_save(void *dst, size_t cap)
{
    if (!dst)
        return EC_BOSS_RB_STATE_LEN;
    if (cap < EC_BOSS_RB_STATE_LEN)
        return 0u;
    uint8_t *p = (uint8_t *)dst;
    p[0] = 1u; /* versão */
    p[1] = s_ec_form[0];
    p[2] = s_ec_form[1];
    p[3] = s_wrap_to_9[0];
    p[4] = s_wrap_to_9[1];
    p[5] = s_prev_ud[0];
    p[6] = s_prev_ud[1];
    return EC_BOSS_RB_STATE_LEN;
}

int ec_boss_rb_state_load(const void *src, size_t len)
{
    if (!src || len < EC_BOSS_RB_STATE_LEN)
        return 0;
    const uint8_t *p = (const uint8_t *)src;
    if (p[0] != 1u)
        return 0;
    s_ec_form[0] = p[1];
    s_ec_form[1] = p[2];
    s_wrap_to_9[0] = p[3];
    s_wrap_to_9[1] = p[4];
    s_prev_ud[0] = p[5];
    s_prev_ud[1] = p[6];
    s_sanc_ok.phase = 0;
    if (s_unlock_boss)
        sanitize_corrupted_fighter_tasks();
    return 1;
}

static void call_68k_sub(uint32_t addr)
{
    int saved_split = g_split_sp_popped;
    g_split_sp_popped = 0;
    g_cpu.A[7] -= 4u;
    m68k_write32(g_cpu.A[7], 0x00000000u);
    recomp_call_addr(addr);
    g_split_sp_popped = saved_split;
}

static void call_68k_sfx(uint32_t sfx_id)
{
    g_cpu.A[7] -= 4u;
    m68k_write32(g_cpu.A[7], sfx_id);
    call_68k_sub(0x000008B0u);
    g_cpu.A[7] += 4u;
}

static void call_68k_pal(uint16_t idx, uint16_t color)
{
    g_cpu.D[0] = idx;
    g_cpu.D[1] = idx;
    g_cpu.D[2] = color;
    call_68k_sub(0x0000D6F6u);
}

static void sanc_apply_window_hole(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    ram16_write(0x9C38u, 0x0000u);
    ram16_write(0x9C44u, x);
    ram16_write(0x9C42u, y);
    ram16_write(0x9C40u, w);
    ram16_write(0x9C3Eu, h);
    ram16_write(0x9C3Au, 0x4000u);
    ram16_write(0x9C34u, 0x0003u);
    ram16_write(0x9C36u, 0x09DEu);
    ram16_write(0x9C3Cu, 0x000Bu);
    call_68k_sub(0x00026638u);
}

static void sanc_overkill_tick(void)
{
    if (s_sanc_ok.phase == 0 || ram16(0xFD64u) != 0x0012u ||
        (s_sanc_ok.victim != 0xAB86u && s_sanc_ok.victim != 0xAA52u)) {
        s_sanc_ok.phase = 0;
        return;
    }

    uint32_t sd[8], sa[7];
    for (int i = 0; i < 8; ++i) sd[i] = g_cpu.D[i];
    for (int i = 0; i < 7; ++i) sa[i] = g_cpu.A[i];
    uint16_t ssr = g_cpu.SR;
    g_cpu.A[6] = 0x00C00000u;

    uint16_t v = s_sanc_ok.victim;

    switch (s_sanc_ok.phase) {
    case 1:
        ram16_write(0x9CB4u, 0xFFFFu);
        g_ram[0xFD69u] &= (uint8_t)~0x80u;
        call_68k_sub(0x0005B01Au);
        if (ram16(0xE210u + 0x0Bu * 2u) == 0u) {
            g_cpu.D[0] = 0x000Bu;
            call_68k_sub(0x0000E408u);
        }
        g_cpu.D[0] = 0x0018u;
        call_68k_sub(0x00026954u);
        call_68k_sfx(0x6Au);
        s_sanc_ok.phase = 2;
        s_sanc_ok.timer = 0;
        break;

    case 2:
        ++s_sanc_ok.timer;
        if (s_sanc_ok.timer == 10) {
            g_ram[((uint32_t)v + 0xE7u) & 0xFFFFu] |= 0x02u;
            ram16_write((uint32_t)v + 0xF4u, 0x1000u);
            s_sanc_ok.start_x = (int16_t)ram16((uint32_t)v + 0x42u);
            s_sanc_ok.start_y = (int16_t)ram16((uint32_t)v + 0x46u);
        } else if (s_sanc_ok.timer > 10 && s_sanc_ok.timer <= 28) {
            int step = s_sanc_ok.timer - 10;
            int16_t nx = (int16_t)(s_sanc_ok.start_x + ((0x0064 - (int)s_sanc_ok.start_x) * step) / 18);
            int16_t ny = (int16_t)(s_sanc_ok.start_y + ((0x00A4 - (int)s_sanc_ok.start_y) * step) / 18
                                   - (24 * step * (18 - step)) / 81);
            ram16_write((uint32_t)v + 0x42u, (uint16_t)nx);
            ram16_write((uint32_t)v + 0xA4u, (uint16_t)nx);
            ram16_write((uint32_t)v + 0x102u, (uint16_t)nx);
            ram16_write((uint32_t)v + 0x46u, (uint16_t)ny);
            ram16_write((uint32_t)v + 0xA6u, (uint16_t)ny);
            ram16_write((uint32_t)v + 0x104u, (uint16_t)ny);
        }
        if ((s_sanc_ok.timer >= 28 && ram16(0x9CA2u) == 0u && ram16(0xCD1Au) <= 0x0008u) ||
            s_sanc_ok.timer >= 120) {
            s_sanc_ok.phase = 3;
            s_sanc_ok.timer = 0;
            s_sanc_ok.cur_x = 0x0064;
            s_sanc_ok.cur_y = 0x00A4;
        }
        break;

    case 3:
        if (s_sanc_ok.timer == 0) {
            call_68k_sfx(0x2Fu);
            sanc_apply_window_hole(0x0030u, 0x0068u, 0x000Bu, 0x0005u);
            sanc_apply_window_hole(0x0050u, 0x0058u, 0x0005u, 0x0001u);
            sanc_apply_window_hole(0x0080u, 0x0060u, 0x0000u, 0x0000u);

            g_cpu.D[0] = 0x00B4u;
            g_cpu.D[1] = 0x0064u;
            g_cpu.D[2] = 0x0078u;
            call_68k_sub(0x0000DCEAu);
            if (!(g_cpu.SR & SR_C) && (uint16_t)g_cpu.A[0] != 0u) {
                uint16_t b4 = (uint16_t)g_cpu.A[0];
                ram16_write((uint32_t)b4 + 0x16u, (uint16_t)(ram16((uint32_t)b4 + 0x16u) & (uint16_t)~0x6000u));
            }

            ram16_write(0x9C44u, 0x0000u);
            ram16_write(0x9C42u, 0x0050u);
            ram16_write(0x9C40u, 0x0007u);
            ram16_write(0x9C3Eu, 0x000Cu);
            ram16_write(0x9C38u, 0x0000u);
            call_68k_sub(0x000266AEu);

            g_ram[((uint32_t)v + 0x16u) & 0xFFFFu] &= (uint8_t)~0x80u;
            call_68k_pal(0x0Bu, 0x0EEEu);
            call_68k_pal(0x0Du, 0x0EEEu);
        } else {
            s_sanc_ok.cur_x = (int16_t)(s_sanc_ok.cur_x - 4);
            s_sanc_ok.cur_y = (int16_t)(s_sanc_ok.cur_y - 1);
            ram16_write((uint32_t)v + 0x42u, (uint16_t)s_sanc_ok.cur_x);
            ram16_write((uint32_t)v + 0xA4u, (uint16_t)s_sanc_ok.cur_x);
            ram16_write((uint32_t)v + 0x102u, (uint16_t)s_sanc_ok.cur_x);
            ram16_write((uint32_t)v + 0x46u, (uint16_t)s_sanc_ok.cur_y);
            ram16_write((uint32_t)v + 0xA6u, (uint16_t)s_sanc_ok.cur_y);
            ram16_write((uint32_t)v + 0x104u, (uint16_t)s_sanc_ok.cur_y);

            if (s_sanc_ok.timer <= 8)
                ram16_write(0xCD16u, (uint16_t)(0x0017 + ((s_sanc_ok.timer & 1) ? -3 : 3)));
            else
                ram16_write(0xCD16u, 0x0017u);

            if (s_sanc_ok.timer == 3) {
                call_68k_pal(0x0Bu, 0x0EEAu);
                call_68k_pal(0x0Du, 0x0EEEu);
            } else if (s_sanc_ok.timer == 6) {
                call_68k_pal(0x0Bu, 0x0CC8u);
                call_68k_pal(0x0Du, 0x0ECAu);
            } else if (s_sanc_ok.timer == 9) {
                call_68k_pal(0x0Bu, 0x0AA4u);
                call_68k_pal(0x0Du, 0x0EC8u);
            }
        }
        ++s_sanc_ok.timer;
        if (s_sanc_ok.timer >= 14 || s_sanc_ok.cur_x <= 0x0018) {
            g_ram[((uint32_t)v + 0x20u) & 0xFFFFu] |= 0x80u;
            ram16_write(0xCD16u, 0x0017u);
            call_68k_pal(0x0Bu, 0x0AA4u);
            call_68k_pal(0x0Du, 0x0EC8u);
            s_sanc_ok.phase = 4;
            s_sanc_ok.timer = 0;
        }
        break;

    case 4:
        ++s_sanc_ok.timer;
        if (s_sanc_ok.timer >= 52) {
            ram16_write(0xCD1Au, 0x0004u);
            ram16_write(0xCD16u, 0x0017u);
            call_68k_sub(0x00016F32u);
            s_sanc_ok.phase = 0;
        }
        break;

    default:
        s_sanc_ok.phase = 0;
        break;
    }

    for (int i = 0; i < 8; ++i) g_cpu.D[i] = sd[i];
    for (int i = 0; i < 7; ++i) g_cpu.A[i] = sa[i];
    g_cpu.SR = ssr;
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

    /* ---- Cenários off-label no Background do VS (opt-in) ---- */
    case 0x02E42Eu: {
        /* commit: $9CB6 = tabela[$936F] (move.w $2e406(pc,d0.w),$9cb6.w).
         * Seleções 10/11 (novas) cairiam fora da tabela de 10 entradas e
         * leriam bytes de código; gravamos o estado e substituímos a
         * instrução (return 1 = pula o move — o rts em $2E434 segue). */
        uint8_t sel = g_ram[0x936Fu];
        if (sel == 10u) {
            ram16_write(0x9CB6u, 14u);   /* estado 14 = luta no Holo-Trainer */
            fprintf(stderr, "[OFFLABEL] commit $936F=10 → estado 14 (Holo-Trainer)\n");
            return 1;
        }
        if (sel == 11u) {
            /* estado 18 = câmara/santuário do EC (o intro do boss usa este
             * handler; em VS ele roda a luta com o fundo da câmara). */
            ram16_write(0x9CB6u, 18u);
            fprintf(stderr, "[OFFLABEL] commit $936F=11 → estado 18 (santuário)\n");
            return 1;
        }
        return 0;
    }

    /* VS/Torneio off-label no Santuário (Estado 18): quando não é o Boss 1P
     * ($9CB0 != 0 || $9424 != 0), desvia dos atalhos exclusivos de boss em
     * Sub_01EDD6 ($1EE74/$1EF8E), intro freeze ($5E652) e timer ($1FD20/$1FD96)
     * para usar o fluxo normal de rounds (banner, contador P1/P2, relógio). */
    case 0x01EE74u:
    case 0x01EF8Eu:
    case 0x05E652u:
    case 0x01FD20u:
    case 0x01FD96u: {
        if (is_vs_offlabel_sanctuary())
            g_cpu.SR &= (uint16_t)~SR_Z;
        return 0;
    }

    /* Fim de partida no estado 18 (santuário off-label): nos 3 RTS de saída
     * do handler ($59D6/$59E6/$59EE), aplica Stage_ExitCleanup ($C578) se for
     * VS/Torneio/Practice; no Boss 1P ($9CB0==0 && $9424==0) segue intacto. */
    case 0x059D6u:
    case 0x059E6u:
    case 0x059EEu: {
        if (is_vs_offlabel_sanctuary())
            sanctuary_vs_exit_cleanup(pc == 0x059D6u);
        return 0;
    }

    /* Telas de arte do fluxo do chefe (estado 8, arte/transformação em
     * $320C; estado 10, tela de ending em $3654) esperam bit7 de $FD61,
     * que só a cadeia de input de menu seta (modo 1P). Em VS 2P o START
     * chega só nos pads de luta ($FD59/$FD5B) — mapeamos aqui, antes do
     * btst.b #7,$FD61 do wait loop (return 0 = instrução segue). */
    case 0x0321Au:
    case 0x03662u: {
        if ((g_ram[0xFD59u] & 0x80u) || (g_ram[0xFD5Bu] & 0x80u))
            g_ram[0xFD61u] |= 0x80u;
        return 0;
    }

    /* Trident (Estado 24): pula a criação dos 2 objetos $1B (Sub_013408, ondas
     * nos pés anexos 2/3) para o Eternal Champion ($40 == $000C). A folha $7C
     * só tem 2 pontos de anexo (0/1) e o EC não está em $016F74/$0136E0 —
     * criar $1B deixava ($8C,a5)=$0001B9FE, que saltava para $0136FE
     * (MOVE.W D0,-(A3)) e corrompia o pool de peças de sprite no Round 1. */
    case 0x05AF92u: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        if (ram16((uint32_t)a5 + 0x40u) == 0x000Cu)
            g_cpu.SR &= (uint16_t)~SR_Z;
        return 0;
    }

    /* Defesa em profundidade + compatibilidade com saves legados (ex.: sav4)
     * que já têm objetos $1B do EC vivos na RAM com ($8C,a5) fora de
     * $016F74–$01718F: aponta para a linha segura do Trident ($016FEE, índice
     * 26 = $00 → loc_013F32), oculta e marca deleção ($74 bit 2). */
    case 0x01354Cu: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        uint32_t tbl = ((uint32_t)ram16((uint32_t)a5 + 0x8Cu) << 16) |
                       (uint32_t)ram16((uint32_t)a5 + 0x8Eu);
        if (tbl < 0x016F74u || tbl >= 0x017190u) {
            ram16_write((uint32_t)a5 + 0x8Cu, 0x0001u);
            ram16_write((uint32_t)a5 + 0x8Eu, 0x6FEEu);
            g_cpu.D[1] = (g_cpu.D[1] & 0xFFFF0000u) | 0x001Au;
            g_ram[((uint32_t)a5 + 0x20u) & 0xFFFFu] |= 0x80u;
            g_ram[((uint32_t)a5 + 0x74u) & 0xFFFFu] |= 0x04u;
        }
        return 0;
    }

    case 0x01367Cu: {
        if ((g_cpu.D[0] & 0xFFFFu) == 0x000Cu)
            g_cpu.D[0] = (g_cpu.D[0] & 0xFFFF0000u) | 0x000Au;
        return 0;
    }

    /* T5 — Overkills / Finishers no Eternal Champion ($40 == $000C):
     * Substitui `ble.w loc_044C70` ($044AE2, logo após `tst.w ($68,a5)`) por
     * chamada a Sub_05E49C com ($8,a4)=$044C70 — espelho exato de loc_0474FC
     * nos lutadores normais. Evita depender de ($E6,a5) bit 7, que o hitstop
     * de golpes pesados ($01773A) já seta antes da entrada em loc_044C70. */
    case 0x044AE2u: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        if (is_playable_ec_slot(a5) &&
            (int16_t)ram16((uint32_t)a5 + 0x68u) <= 0) {
            uint16_t a4 = (uint16_t)g_cpu.A[4];
            ram16_write((uint32_t)a4 + 8u, 0x0004u);
            ram16_write((uint32_t)a4 + 10u, 0x4C70u);
            g_cpu.A[0] = 0x0005E49Cu;
            recomp_call_addr(0x0005E49Cu);
            return 1;
        }
        return 0;
    }

    /* T5 — Tabelas de lookup por $40 sem $000C nos scripts de Overkill:
     * Trident Tubarão ($015F38), Larcen Carro ($01222E / $01244E),
     * Xavier Bola de Fogo & Fumaça ($012738 / $012C38). Mapeia para índice 8. */
    case 0x015F90u: {
        uint16_t a0 = (uint16_t)g_cpu.A[0];
        if (ram16((uint32_t)a0 + 0x40u) == 0x000Cu) {
            g_cpu.D[0] = (g_cpu.D[0] & 0xFFFF0000u) | 0x0008u;
            g_cpu.SR |= SR_Z;
        }
        return 0;
    }

    case 0x0123C4u: {
        if ((g_cpu.D[1] & 0xFFFFu) == 0x000Cu)
            g_cpu.D[1] = (g_cpu.D[1] & 0xFFFF0000u) | 0x0008u;
        return 0;
    }

    case 0x01259Eu:
    case 0x012832u:
    case 0x012C76u: {
        if ((g_cpu.D[0] & 0xFFFFu) == 0x000Cu)
            g_cpu.D[0] = (g_cpu.D[0] & 0xFFFF0000u) | 0x0008u;
        return 0;
    }

    /* VS/Practice/Torneio off-label no Holo-Trainer (Estado 14): preserva
     * opções de partida do VS, neutraliza objetos exclusivos de treino 1P
     * ($78/$7A) na entrada de suas tasks, usa fluxo normal de rounds/vitórias
     * em Sub_01EDD6/loc_01F15C e executa Stage_ExitCleanup na saída ($003BE8). */
    case 0x003986u: {
        if (is_vs_offlabel_holo()) {
            s_holo_saved_977c = ram16(0x977Cu);
            s_holo_saved_9384 = ram16(0x9384u);
            s_holo_saved_9378 = ram16(0x9378u);
            s_holo_saved_9383 = g_ram[0x9383u];
        }
        return 0;
    }

    case 0x0039BAu: {
        if (is_vs_offlabel_holo()) {
            g_ram[0x936Eu] = g_ram[0x9B44u];
            ram16_write(0x977Cu, s_holo_saved_977c);
            ram16_write(0x9384u, s_holo_saved_9384);
            ram16_write(0x9378u, s_holo_saved_9378);
            g_ram[0x9383u] = s_holo_saved_9383;
        }
        return 0;
    }

    case 0x01F5EEu:
    case 0x01FAE6u: {
        if (is_vs_offlabel_holo()) {
            uint16_t a5 = (uint16_t)g_cpu.A[5];
            g_ram[((uint32_t)a5 + 0x20u) & 0xFFFFu] |= 0x80u;
            g_ram[((uint32_t)a5 + 0x74u) & 0xFFFFu] |= 0x04u;
            recomp_call_addr(0x0000DC10u);
            return 1;
        }
        return 0;
    }

    case 0x01EE6Au:
    case 0x01EF98u:
    case 0x01F166u:
    case 0x05AF4Eu: {
        if (is_vs_offlabel_holo())
            g_cpu.SR &= (uint16_t)~SR_Z;
        return 0;
    }

    case 0x003BE8u: {
        if (is_vs_offlabel_holo())
            offlabel_vs_exit_cleanup(0x000Eu, 0);
        return 0;
    }

    /* Item 2-B: desativa o flicker holográfico de 30 Hz do Player 2 (bit 5 de
     * $FFFFAA72 e de objetos filhos em $FFFFAB5E) logo após $01F6D0/$01F6E0. */
    case 0x01F6D6u:
        g_ram[0xAA72u] &= (uint8_t)~0x20u;
        return 0;

    case 0x01F6E6u: {
        uint16_t child = ram16(0xAB5Eu);
        if (child != 0u)
            g_ram[((uint32_t)child + 0x20u) & 0xFFFFu] &= (uint8_t)~0x20u;
        return 0;
    }

    /* Item 4 (Microtarefa 3): Overkill customizado no vitral 1 do Santuário
     * (Estado 18 off-label em VS/Torneio). */
    case 0x005818u:
        s_sanc_ok.phase = 0;
        if (is_vs_offlabel_sanctuary())
            g_cpu.SR |= (uint16_t)SR_Z;
        return 0;

    case 0x026A16u: {
        if (!is_vs_offlabel_sanctuary() || s_sanc_ok.phase != 0)
            return 0;
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        if (a5 != 0xAB86u && a5 != 0xAA52u)
            return 0;
        if (g_ram[((uint32_t)a5 + 0xDDu) & 0xFFFFu] & 0x40u)
            return 0;
        uint16_t vx = ram16((uint32_t)a5 + 0x42u);
        int facing_left = (g_ram[((uint32_t)a5 + 0xE6u) & 0xFFFFu] & 0x08u) != 0;
        int in_zone = (!facing_left && vx >= 0x0040u && vx <= 0x00F0u) ||
                      (facing_left && vx >= 0x0028u && vx <= 0x0088u);
        if (!in_zone)
            return 0;

        ram16_write(0x9C74u, a5);
        ram16_write(0x9C7Cu, 1u);
        ram16_write(0x9C7Au, 0x0004u);
        ram16_write(0x9C76u, 0x0017u);
        ram16_write(0x9C7Eu, 1u);

        uint32_t sd[5] = { g_cpu.D[0], g_cpu.D[1], g_cpu.D[2], g_cpu.D[3], g_cpu.D[4] };
        uint32_t sa[4] = { g_cpu.A[0], g_cpu.A[1], g_cpu.A[5], g_cpu.A[6] };
        g_cpu.A[6] = 0x00C00000u;
        g_cpu.D[0] = 0x0075u;
        g_cpu.D[1] = 0u;
        g_cpu.D[2] = 0u;
        call_68k_sub(0x0000DCEAu);
        for (int i = 0; i < 5; ++i) g_cpu.D[i] = sd[i];
        g_cpu.A[0] = sa[0];
        g_cpu.A[1] = sa[1];
        g_cpu.A[5] = sa[2];
        g_cpu.A[6] = sa[3];

        s_sanc_ok.phase = 1;
        s_sanc_ok.timer = 0;
        s_sanc_ok.victim = a5;

        g_cpu.SR = (uint16_t)((g_cpu.SR & 0xFF00u) | SR_C);
        g_cpu.PC = m68k_read32(g_cpu.A[7]);
        g_cpu.A[7] += 4u;
        return 1;
    }

    case 0x01F4FAu:
        if (s_sanc_ok.phase != 0)
            sanc_overkill_tick();
        return 0;

    default:
        return 0;
    }
}
