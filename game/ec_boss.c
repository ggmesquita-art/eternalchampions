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
#include "genesis_bus.h"   /* GPAD_* */
#include "sim_step.h"      /* genesis_sim_pad: controle selado do quadro */

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

static void holo_ok_reset(int cleanup);
static void ec_form_poll_input(void);
static void ec_form_flash_tick(void);
static void ec_form_reset(void);

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
    int      wall_prio; /* 1 = bloco da parede a esquerda do vitral com prioridade invertida */
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
    s_sanc_ok.wall_prio = 0;
    holo_ok_reset(0);
    ec_form_reset();
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
    if (s_unlock_boss) {
        sanitize_corrupted_fighter_tasks();
        ec_form_poll_input();
        ec_form_flash_tick();
    }
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
    s_sanc_ok.wall_prio = 0;
    holo_ok_reset(0);
    ec_form_reset();
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

/*
 * Sub_0266AE INVERTE (eori #$8000) a prioridade das celulas do plano A no bloco dado.
 * O overkill liga a prioridade da parede a esquerda do vitral (x 0..63, y 80..183 no
 * plano) para a vitima passar por tras dela ao sair; chamar de novo com os mesmos
 * parametros desfaz exatamente. Sem desfazer, o vencedor (sprite de prioridade baixa)
 * que ficasse encostado nessa parede sumia atras dela, sobrando so os pes.
 */
static void sanc_toggle_wall_priority(void)
{
    ram16_write(0x9C44u, 0x0000u);
    ram16_write(0x9C42u, 0x0050u);
    ram16_write(0x9C40u, 0x0007u);
    ram16_write(0x9C3Eu, 0x000Cu);
    ram16_write(0x9C38u, 0x0000u);
    call_68k_sub(0x000266AEu);
    s_sanc_ok.wall_prio ^= 1;
}

/* Overkill interrompido com a parede ainda invertida: desfaz se o cenario ainda e o
 * Santuario (fora dele o plano ja foi recarregado). Preserva D0-D7/A0-A6/SR. */
static void sanc_restore_wall_if_needed(void)
{
    if (!s_sanc_ok.wall_prio)
        return;
    if (ram16(0xFD64u) == 0x0012u) {
        uint32_t sd[8], sa[7];
        for (int i = 0; i < 8; ++i) sd[i] = g_cpu.D[i];
        for (int i = 0; i < 7; ++i) sa[i] = g_cpu.A[i];
        uint16_t ssr = g_cpu.SR;
        g_cpu.A[6] = 0x00C00000u;
        sanc_toggle_wall_priority();
        for (int i = 0; i < 8; ++i) g_cpu.D[i] = sd[i];
        for (int i = 0; i < 7; ++i) g_cpu.A[i] = sa[i];
        g_cpu.SR = ssr;
    }
    s_sanc_ok.wall_prio = 0;
}

static void sanc_overkill_tick(void)
{
    if (s_sanc_ok.phase == 0 || ram16(0xFD64u) != 0x0012u ||
        (s_sanc_ok.victim != 0xAB86u && s_sanc_ok.victim != 0xAA52u)) {
        sanc_restore_wall_if_needed();
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

            if (!s_sanc_ok.wall_prio)
                sanc_toggle_wall_priority();

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
            if (s_sanc_ok.wall_prio)
                sanc_toggle_wall_priority(); /* vitima ja oculta: parede volta ao normal */
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

/* ---------------------------------------------------------------------------
 * Overkill do Holo-Trainer (Estado 14 off-label em VS/Torneio, --unlock-boss):
 * o drone flutuante ("moving head", objeto id $7A) persegue a vitima e dispara
 * um raio vertical que a incendeia; ela carboniza e se desfaz em cinzas.
 *
 * - Graficos: raio, clarao no chao e cinzas sao tiles gerados aqui e enviados a
 *   um trecho de VRAM zerado e sem referencia (procurado em $600..$7FF no
 *   disparo; sem espaco, nao ha overkill). No fim o trecho volta a zero. Cores
 *   tiradas da propria linha de paleta do drone (as mais proximas).
 * - Sprites: hook em $E68C (Sub_E684 com d4/a2 ja carregados) grava as entradas
 *   na SAT em RAM antes dos objetos (maior prioridade) e avanca d4/a2.
 * - Queima: interpola a linha de paleta da vitima no buffer $F036 (+ $EF32).
 * Com a fase 0 nenhum estado do jogo e tocado.
 * ------------------------------------------------------------------------- */
#define HOLO_TILE_N   24      /* 0..7 raio, 8..15 clarao, 16..19 monte de po, 20..22 particulas */
#define HOLO_SPR_MAX  64
#define HOLO_PART_MAX 56      /* particulas da desintegracao */

typedef struct {
    int      phase;      /* 0=ocioso 1=preparo 2=drone_aproxima 3=raio 4=carboniza 5=desfaz 6=final */
    int      timer;
    uint16_t victim;
    uint16_t drone;
    uint16_t tile0;      /* primeiro tile do trecho alocado (0 = nenhum) */
    int      vline;      /* linha de paleta da vitima */
    uint16_t vpal[16];   /* cores originais da vitima */
    int16_t  d_x0, d_y0; /* posicao inicial do drone */
    uint8_t  c_white, c_red, c_dark, c_ash, c_ash2;
    int16_t  ash_x, ash_y;   /* posicao (mundo) das cinzas, fixada quando a vitima some */
    int      dline;          /* linha de paleta do drone (cores dos tiles) */
    uint16_t dpal0[16];      /* cores da linha do drone no disparo */
    uint16_t cyc_mask;       /* indices que mudam (ciclo de paleta do Holo): evitados */
    int16_t  dr_x, dr_y;     /* posicao (mundo) do drone/cabeca desenhado pelo overkill */
    uint16_t blk;            /* no do alocador de VRAM (Mem_Alloc) com os tiles do efeito */
    int      head;           /* 1 = armadilha do trilho (grupo $48), 0 = drone */
    uint16_t hbase;          /* tile base do grupo $48 (cabeca da armadilha) */
    int      hgroup_ours;    /* grupo $48 carregado pelo overkill (liberar no fim) */
    int      hremap;         /* tiles da cabeca ja remapeados para a paleta do Holo */
    int      npart;          /* particulas da desintegracao (silhueta da vitima) */
    int32_t  px[HOLO_PART_MAX], py[HOLO_PART_MAX];   /* mundo, ponto fixo 8.8 */
    int16_t  pvx[HOLO_PART_MAX], pvy[HOLO_PART_MAX];
    int16_t  pdelay[HOLO_PART_MAX];
    int16_t  pstate[HOLO_PART_MAX];   /* 0 parada, 1 caindo (quadros), -1 assentada */
    int      nsettled;
    uint32_t rng;
} HoloOkState;

static HoloOkState s_holo_ok = {0};
static int s_holo_dbg = -1;          /* EC_HOLO_OK_TEST (diagnostico), ver holo_debug_frame */
static int s_holo_dbg_count = 0;
static int s_holo_dbg_active = 0;
/* Som do disparo do raio (id do driver; 0 = nenhum). EC_HOLO_OK_SFX=<hex> para testar ids. */
static uint32_t s_holo_sfx_beam = 0u;

static void holo_vram_set_addr(uint16_t addr, int write)
{
    vdp_write_ctrl(0x8F02u);                                  /* auto-incremento 2 */
    vdp_write_ctrl((uint16_t)((write ? 0x4000u : 0x0000u) | (addr & 0x3FFFu)));
    vdp_write_ctrl((uint16_t)((addr >> 14) & 3u));
}

static int holo_tile_is_zero(uint16_t tile)
{
    holo_vram_set_addr((uint16_t)(tile * 32u), 0);
    for (int i = 0; i < 16; ++i)
        if (vdp_read_data() != 0u)
            return 0;
    return 1;
}

/* px: 64 indices de cor (8x8, linha a linha). */
static void holo_tile_write(uint16_t tile, const uint8_t px[64])
{
    holo_vram_set_addr((uint16_t)(tile * 32u), 1);
    for (int row = 0; row < 8; ++row) {
        for (int k = 0; k < 2; ++k) {
            const uint8_t *p = &px[row * 8 + k * 4];
            vdp_write_data((uint16_t)(((p[0] & 15u) << 12) | ((p[1] & 15u) << 8) |
                                      ((p[2] & 15u) << 4) | (p[3] & 15u)));
        }
    }
}

/* Chama uma sub-rotina 68K preservando D0-D7/A0-A6/SR (A6 = porta do VDP). */
static void holo_call(uint32_t addr, uint32_t d0, uint32_t d1, uint32_t a0,
                      uint32_t *out_a0, int *out_carry)
{
    uint32_t sd[8], sa[7];
    for (int i = 0; i < 8; ++i) sd[i] = g_cpu.D[i];
    for (int i = 0; i < 7; ++i) sa[i] = g_cpu.A[i];
    uint16_t ssr = g_cpu.SR;
    g_cpu.A[6] = 0x00C00000u;
    g_cpu.D[0] = d0;
    g_cpu.D[1] = d1;
    g_cpu.A[0] = a0;
    call_68k_sub(addr);
    if (out_a0)
        *out_a0 = g_cpu.A[0];
    if (out_carry)
        *out_carry = (g_cpu.SR & 1u) != 0;
    for (int i = 0; i < 8; ++i) g_cpu.D[i] = sd[i];
    for (int i = 0; i < 7; ++i) g_cpu.A[i] = sa[i];
    g_cpu.SR = ssr;
}

/* Reserva HOLO_TILE_N tiles no alocador de VRAM do jogo (Mem_Alloc $D4CC, como o
 * Tile_AllocGroup faz); devolve o tile inicial ou 0 se nao houver espaco. */
static uint16_t holo_alloc_tiles(void)
{
    uint32_t node = 0;
    int carry = 1;
    holo_call(0x0000D4CCu, (uint32_t)HOLO_TILE_N * 32u, 0x4000u, 0u, &node, &carry);
    if (carry || (node & 0xFFFFu) == 0u)
        return 0u;
    s_holo_ok.blk = (uint16_t)node;
    uint32_t addr = ((uint32_t)ram16(node) << 16) | ram16(node + 2u);
    return (uint16_t)((addr >> 5) & 0x7FFu);
}

static void holo_free_tiles(void)
{
    if (s_holo_ok.blk != 0u)
        holo_call(0x0000D5A4u, 0u, 0u, 0xFFFF0000u | s_holo_ok.blk, NULL, NULL);
    s_holo_ok.blk = 0u;
    s_holo_ok.tile0 = 0u;
}

/* Grupo de tiles $48 = armadilhas da sala de treino (State17_Handler, indice $29 da
 * lista $7F65A). O Holo nao o carrega; Tile_AllocGroup ($E408) aloca e descomprime. */
#define HOLO_TRAP_GID 0x48u
static int holo_load_trap_group(void)
{
    uint16_t base = ram16(0xE210u + HOLO_TRAP_GID * 2u);
    if (base == 0u) {
        holo_call(0x0000E408u, HOLO_TRAP_GID, 0u, 0u, NULL, NULL);
        base = ram16(0xE210u + HOLO_TRAP_GID * 2u);
        if (base == 0u)
            return 0;
        s_holo_ok.hgroup_ours = 1;
    }
    s_holo_ok.hbase = base;
    return 1;
}

static void holo_free_trap_group(void)
{
    if (s_holo_ok.hgroup_ours)
        holo_call(0x0000E340u, HOLO_TRAP_GID, 0u, 0u, NULL, NULL);
    s_holo_ok.hgroup_ours = 0;
}

static void holo_rgb(uint16_t c, int *r, int *g, int *b)
{
    *r = (c >> 1) & 7;
    *g = (c >> 5) & 7;
    *b = (c >> 9) & 7;
}

static uint16_t holo_color(int r, int g, int b)
{
    r = r < 0 ? 0 : (r > 7 ? 7 : r);
    g = g < 0 ? 0 : (g > 7 ? 7 : g);
    b = b < 0 ? 0 : (b > 7 ? 7 : b);
    return (uint16_t)((b << 9) | (g << 5) | (r << 1));
}

static uint8_t holo_nearest(int line, int tr, int tg, int tb)
{
    int best = 1, bd = 1 << 30;
    for (int i = 1; i < 16; ++i) {
        if (s_holo_ok.cyc_mask & (1u << i))
            continue;
        int r, g, b;
        holo_rgb(ram16(0xF036u + (unsigned)(line * 16 + i) * 2u), &r, &g, &b);
        int d = (r - tr) * (r - tr) * 3 + (g - tg) * (g - tg) * 4 + (b - tb) * (b - tb) * 2;
        if (d < bd) { bd = d; best = i; }
    }
    return (uint8_t)best;
}

/* Le/escreve um tile 4bpp como 64 indices. */
static void holo_tile_read(uint16_t tile, uint8_t px[64])
{
    holo_vram_set_addr((uint16_t)(tile * 32u), 0);
    for (int row = 0; row < 8; ++row) {
        for (int k = 0; k < 2; ++k) {
            uint16_t w = vdp_read_data();
            px[row * 8 + k * 4 + 0] = (uint8_t)((w >> 12) & 15u);
            px[row * 8 + k * 4 + 1] = (uint8_t)((w >> 8) & 15u);
            px[row * 8 + k * 4 + 2] = (uint8_t)((w >> 4) & 15u);
            px[row * 8 + k * 4 + 3] = (uint8_t)(w & 15u);
        }
    }
}

/* Cabeca da armadilha: tiles do grupo $48 desenhados para a linha 1 da sala de treino
 * (State17_Palette $4804, cores 16..31); remapeia em lugar para as cores estaveis mais
 * proximas da linha 1 do Holo. Offsets usados: 0..$19 (topo, boca fechada e aberta). */
#define HOLO_HEAD_TILES 0x1Au
static void holo_remap_head(void)
{
    uint8_t map[16];
    map[0] = 0;
    for (int i = 1; i < 16; ++i) {
        uint16_t c = (uint16_t)((g_rom[0x4824u + i * 2u] << 8) | g_rom[0x4825u + i * 2u]);
        int r, g, b;
        holo_rgb(c, &r, &g, &b);
        map[i] = holo_nearest(1, r, g, b);   /* linha 1 (a mesma do drone) */
    }
    uint8_t px[64];
    for (unsigned t = 0; t < HOLO_HEAD_TILES; ++t) {
        holo_tile_read((uint16_t)(s_holo_ok.hbase + t), px);
        for (int k = 0; k < 64; ++k)
            px[k] = map[px[k] & 15u];
        holo_tile_write((uint16_t)(s_holo_ok.hbase + t), px);
    }
    s_holo_ok.hremap = 1;
}

/* Linha de paleta do 1o sub-sprite de um objeto. */
static int holo_obj_line(uint16_t obj)
{
    uint16_t a1 = ram16((uint32_t)obj + 0x0Cu);
    if (a1 == 0u)
        return 1;
    return (ram16((uint32_t)a1 + 6u) >> 13) & 3;
}

static uint16_t holo_find_drone(void)
{
    uint16_t a0 = 0xACBAu;
    for (int i = 0; i < 64; ++i) {
        a0 = ram16((uint32_t)a0 + 6u);
        if (a0 == 0u)
            break;
        if (ram16((uint32_t)a0 + 0x40u) == 0x007Au)
            return a0;
    }
    return 0u;
}

static void holo_build_tiles(int line)
{
    s_holo_ok.c_white = holo_nearest(line, 7, 7, 7);
    s_holo_ok.c_red   = holo_nearest(line, 7, 0, 0);
    s_holo_ok.c_dark  = holo_nearest(line, 4, 0, 0);
    s_holo_ok.c_ash   = holo_nearest(line, 1, 1, 1);
    s_holo_ok.c_ash2  = holo_nearest(line, 4, 4, 4);
    uint8_t W = s_holo_ok.c_white, R = s_holo_ok.c_red, D = s_holo_ok.c_dark;
    uint8_t A = s_holo_ok.c_ash, A2 = s_holo_ok.c_ash2;

    /* Raio com 16 px de largura: perfil por coluna; A = fino, B = largo (alternam = pulso). */
    static const char prof_a[17] = "...DDRRWWRRDD...";
    static const char prof_b[17] = "..DRRRWWWWRRRD..";
    uint8_t px[64];
    for (int v = 0; v < 2; ++v) {
        const char *prof = v ? prof_b : prof_a;
        for (int rowv = 0; rowv < 2; ++rowv) {           /* 2 variacoes de linha (faiscas) */
            for (int half = 0; half < 2; ++half) {
                for (int y = 0; y < 8; ++y) {
                    for (int x = 0; x < 8; ++x) {
                        int col = half * 8 + x;
                        char c = prof[col];
                        /* faiscas irregulares: so o vermelho colado ao nucleo acende */
                        int spark = rowv ? (y == 2 || y == 7) : (y == 4);
                        if (spark && c == 'R' &&
                            ((col > 0 && prof[col - 1] == 'W') || (col < 15 && prof[col + 1] == 'W')))
                            c = 'W';
                        px[y * 8 + x] = (c == 'W') ? W : (c == 'R') ? R : (c == 'D') ? D : 0;
                    }
                }
                /* sprite 2x1: tile = base + v*4 + rowv*2 + metade (esquerda, direita) */
                holo_tile_write((uint16_t)(s_holo_ok.tile0 + v * 4 + rowv * 2 + half), px);
            }
        }
    }

    /* Clarao no chao 32x16 (sprite 4x2, tiles coluna a coluna): elipse branca/vermelha. */
    for (int cx = 0; cx < 4; ++cx) {
        for (int cy = 0; cy < 2; ++cy) {
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    int X = cx * 8 + x, Y = cy * 8 + y;
                    int dx = X * 2 - 31, dy = (Y * 2 - 15) * 2;
                    int d2 = dx * dx + dy * dy;
                    uint8_t c = 0;
                    if (d2 < 120)
                        c = W;
                    else if (d2 < 420)
                        c = R;
                    else if (d2 < 900 && ((X + Y) & 1))
                        c = D;
                    px[y * 8 + x] = c;
                }
            }
            holo_tile_write((uint16_t)(s_holo_ok.tile0 + 8 + cx * 2 + cy), px);
        }
    }

    /* Cinzas 32x8 (sprite 4x1): monte baixo pontilhado. */
    for (int cx = 0; cx < 4; ++cx) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                int X = cx * 8 + x;
                int dx = X * 2 - 31;
                int top = 7 - (6 * (31 * 31 - dx * dx)) / (31 * 31);   /* 1..7 */
                uint8_t c = 0;
                if (y >= top) {
                    int h = (X * 7 + y * 13) % 11;
                    c = (h == 0 && y > top) ? R : (h < 3 ? A2 : A);   /* cinza com brasas */
                }
                px[y * 8 + x] = c;
            }
        }
        holo_tile_write((uint16_t)(s_holo_ok.tile0 + 16 + cx), px);
    }
}

static int holo_rand(int n)
{
    s_holo_ok.rng = s_holo_ok.rng * 1103515245u + 12345u;
    return (int)((s_holo_ok.rng >> 16) % (uint32_t)(n > 0 ? n : 1));
}

/*
 * Po da desintegracao na linha de paleta da vitima (ja em brilho ciano): indice mais
 * claro = brilho, intermediario = ciano, mais escuro = sombra. Monte 32x8 (16..19) e
 * 3 particulas 8x8 (20 densa, 21 media, 22 rala).
 */
static void holo_build_dust(void)
{
    int idx[15], lum[16];
    for (int i = 1; i < 16; ++i) {
        int r, g, b;
        holo_rgb(s_holo_ok.vpal[i], &r, &g, &b);
        lum[i] = r * 3 + g * 4 + b * 2;
        idx[i - 1] = i;
    }
    for (int a = 0; a < 15; ++a)                     /* ordena por luminosidade */
        for (int b = a + 1; b < 15; ++b)
            if (lum[idx[b]] < lum[idx[a]]) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
    uint8_t lo = (uint8_t)idx[2], mid = (uint8_t)idx[8], hi = (uint8_t)idx[14];
    uint8_t px[64];
    for (int cx = 0; cx < 4; ++cx) {
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                int X = cx * 8 + x;
                int dx = X * 2 - 31;
                int top = 7 - (5 * (31 * 31 - dx * dx)) / (31 * 31);   /* 2..7 */
                uint8_t c = 0;
                if (y >= top) {
                    int h = (X * 7 + y * 13) % 9;
                    c = (h == 0) ? hi : ((h < 4) ? mid : lo);
                    if (y == top && (X & 1))
                        c = 0;                                       /* borda granulada */
                }
                px[y * 8 + x] = c;
            }
        }
        holo_tile_write((uint16_t)(s_holo_ok.tile0 + 16 + cx), px);
    }
    static const char *pat[3] = {
        ".M.H..M." "M..M.H.." "..H..M.M" ".M..M..." "H..M..H." "..M.H..M" ".H...M.." "M..H..M.",
        "....M..." ".H...... " "......M." "..M....." "....H..." ".M...... " "......H." "........",
        "........" "...H...." "........" "......M." "........" ".M......" "........" "....H..." };
    for (int v = 0; v < 3; ++v) {
        const char *q = pat[v];
        int k = 0;
        for (int i = 0; i < 64 && q[k]; ++k) {
            char c = q[k];
            if (c == ' ')
                continue;
            px[i++] = (c == 'H') ? hi : (c == 'M') ? mid : 0;
        }
        holo_tile_write((uint16_t)(s_holo_ok.tile0 + 20 + v), px);
    }
}

/* Vitima em brilho ciano (como no overkill do RAX): sobe ate t = tmax e depois pulsa. */
static void holo_victim_pal(int t, int tmax)
{
    int line = s_holo_ok.vline;
    int k = (t * 10) / (tmax > 0 ? tmax : 1);
    if (k > 10)
        k = 10;
    int pulse = (t > tmax) ? (((t >> 2) & 1) ? 1 : 0) : 0;
    for (int i = 1; i < 16; ++i) {
        int r, g, b;
        holo_rgb(s_holo_ok.vpal[i], &r, &g, &b);
        int lum = (r * 3 + g * 4 + b * 2) / 9;                 /* 0..7 */
        int hr = lum / 2 + pulse, hg = 3 + (lum * 4) / 7 + pulse, hb = 7;
        int nr = r + ((hr - r) * k) / 10;
        int ng = g + ((hg - g) * k) / 10;
        int nb = b + ((hb - b) * k) / 10;
        ram16_write(0xF036u + (unsigned)(line * 16 + i) * 2u, holo_color(nr, ng, nb));
    }
    ram16_write(0xEF32u, 0xFFFFu);
}

/*
 * Desintegracao: le a silhueta atual da vitima (lista de sub-sprites ($C) + tiles na
 * VRAM, respeitando os espelhamentos) numa grade de 8x8 e cria uma particula em cada
 * celula bem coberta. As de cima comecam a cair primeiro.
 */
static void holo_sample_victim(void)
{
    enum { GW = 20, GH = 26 };
    static uint16_t cov[GH][GW];
    memset(cov, 0, sizeof(cov));
    uint16_t v = s_holo_ok.victim;
    int bx = (int16_t)ram16((uint32_t)v + 0x4Au), by = (int16_t)ram16((uint32_t)v + 0x4Cu);
    int gx0 = bx - GW * 4, gy0 = by - GH * 8 + 8;
    uint16_t a1 = ram16((uint32_t)v + 0x0Cu);
    unsigned cnt = ram16((uint32_t)v + 0x0Eu);
    uint8_t tp[64];
    for (unsigned n = 0; n < cnt && n < 32u && a1 != 0u; ++n) {
        int sx = bx + (int16_t)ram16((uint32_t)a1 + 0u);
        int sy = by + (int16_t)ram16((uint32_t)a1 + 2u);
        int sz = (ram16((uint32_t)a1 + 4u) >> 8) & 0x0F;
        uint16_t at = ram16((uint32_t)a1 + 6u);
        int hs = ((sz >> 2) & 3) + 1, vs = (sz & 3) + 1;
        int hf = (at & 0x0800u) != 0, vf = (at & 0x1000u) != 0;
        for (int c = 0; c < hs; ++c) {
            for (int r = 0; r < vs; ++r) {
                holo_tile_read((uint16_t)(((at & 0x7FFu) + (unsigned)(c * vs + r)) & 0x7FFu), tp);
                int cc = hf ? (hs - 1 - c) : c, rr = vf ? (vs - 1 - r) : r;
                for (int y = 0; y < 8; ++y) {
                    for (int x = 0; x < 8; ++x) {
                        if (!tp[y * 8 + x])
                            continue;
                        int X = sx + cc * 8 + (hf ? 7 - x : x) - gx0;
                        int Y = sy + rr * 8 + (vf ? 7 - y : y) - gy0;
                        if (X >= 0 && Y >= 0 && X < GW * 8 && Y < GH * 8)
                            cov[Y >> 3][X >> 3]++;
                    }
                }
            }
        }
        a1 = ram16((uint32_t)a1 + 8u);
    }
    int ncell = 0;
    for (int y = 0; y < GH; ++y)
        for (int x = 0; x < GW; ++x)
            if (cov[y][x] >= 12)
                ++ncell;
    int step = (ncell + HOLO_PART_MAX - 1) / HOLO_PART_MAX;
    if (step < 1)
        step = 1;
    int k = 0, np = 0, top_row = -1;
    for (int y = 0; y < GH && np < HOLO_PART_MAX; ++y) {
        for (int x = 0; x < GW && np < HOLO_PART_MAX; ++x) {
            if (cov[y][x] < 12 || (k++ % step) != 0)
                continue;
            if (top_row < 0)
                top_row = y;
            s_holo_ok.px[np] = (int32_t)((gx0 + x * 8 + 4) << 8);
            s_holo_ok.py[np] = (int32_t)((gy0 + y * 8 + 4) << 8);
            s_holo_ok.pvx[np] = (int16_t)(holo_rand(97) - 48);       /* deriva lateral */
            s_holo_ok.pvy[np] = (int16_t)(-holo_rand(64));           /* pequeno salto */
            s_holo_ok.pdelay[np] = (int16_t)((y - top_row) * 3 + holo_rand(14));
            s_holo_ok.pstate[np] = 0;
            ++np;
        }
    }
    s_holo_ok.npart = np;
    s_holo_ok.nsettled = 0;
    if (getenv("EC_HOLO_OK_LOG"))
        fprintf(stderr, "[HOLO-OK] silhueta: subsprites=%u celulas=%d particulas=%d x=%d y=%d\n",
                cnt, ncell, np, bx, by);
}

static void holo_particles_step(void)
{
    int floor_y = (int16_t)ram16((uint32_t)s_holo_ok.victim + 0x4Cu);
    for (int i = 0; i < s_holo_ok.npart; ++i) {
        if (s_holo_ok.pstate[i] < 0)
            continue;
        if (s_holo_ok.pdelay[i] > 0) {
            --s_holo_ok.pdelay[i];
            continue;
        }
        ++s_holo_ok.pstate[i];
        s_holo_ok.pvy[i] = (int16_t)(s_holo_ok.pvy[i] + 22);         /* gravidade */
        s_holo_ok.px[i] += s_holo_ok.pvx[i];
        s_holo_ok.py[i] += s_holo_ok.pvy[i];
        if ((s_holo_ok.py[i] >> 8) >= floor_y - 2) {
            s_holo_ok.pstate[i] = -1;
            ++s_holo_ok.nsettled;
        }
    }
}

static void holo_restore_victim_pal(void)
{
    for (int i = 1; i < 16; ++i)
        ram16_write(0xF036u + (unsigned)(s_holo_ok.vline * 16 + i) * 2u, s_holo_ok.vpal[i]);
    ram16_write(0xEF32u, 0xFFFFu);
}

/* cleanup: devolve VRAM/paleta (ainda na arena). Sem cleanup: so esquece (load/troca). */
static void holo_ok_reset(int cleanup)
{
    if (cleanup && s_holo_ok.phase != 0 && ram16(0xFD64u) == 0x000Eu) {
        holo_free_tiles();
        holo_free_trap_group();
        if (s_holo_ok.phase >= 2) {
            holo_restore_victim_pal();
            g_ram[((uint32_t)s_holo_ok.victim + 0x20u) & 0xFFFFu] &= (uint8_t)~0x80u;
        }
    }
    memset(&s_holo_ok, 0, sizeof(s_holo_ok));
}

/*
 * Drone desenhado pelo proprio overkill: no VS off-label o objeto $7A (criado so pelo
 * modo treino, State14_Handler) nao existe, mas os tiles dele ($E3..$E8, linha de
 * paleta 1) continuam na VRAM. Quadros do sprite copiados do objeto original:
 * corpo 2x2 em (-8,0) e, quadro sim quadro nao (de 2 em 2), 3 jatos 1x1 embaixo.
 */
#define HOLO_DRONE_ATTR   0x2000u   /* linha de paleta 1, prioridade baixa (como o original) */
#define HOLO_DRONE_BODY   0x00E3u
#define HOLO_DRONE_LINE   1
#define HOLO_RAIL_Y       0x28      /* y (mundo) do trilho das armadilhas ($CB em State17_Handler) */
/* Tempos (quadros a 60 Hz): vitima tonta, cabeca desliza, raio (brilho ciano), particulas, saida. */
#define HOLO_T_WAIT       36
#define HOLO_T_SLIDE      90
#define HOLO_T_BEAM       140
#define HOLO_T_GLOW       70
#define HOLO_T_DISINT     120
#define HOLO_T_EXIT       80
#define HOLO_ZONE_MIN     400       /* faixa de x (mundo) da vitima que arma o overkill */
#define HOLO_ZONE_MAX     560

/* Hook em $E68C: injeta os sprites do overkill no inicio da SAT em RAM. */
static void holo_inject_sprites(void)
{
    int ph = s_holo_ok.phase;
    if (ph < 2 || s_holo_ok.tile0 == 0u || ram16(0xFD64u) != 0x000Eu)
        return;
    uint16_t d4  = (uint16_t)g_cpu.D[4];
    uint32_t a2  = g_cpu.A[2] & 0xFFFFu;
    /* Reserva as vagas da SAT dos objetos visiveis do jogo (Sub_E684 para em 80): os
     * sprites do overkill nunca podem cortar os lutadores. */
    int reserve = 0;
    {
        uint16_t o = 0xACBAu;
        for (int i = 0; i < 64; ++i) {
            o = ram16((uint32_t)o + 6u);
            if (o == 0u)
                break;
            if ((ram16((uint32_t)o + 0x20u) & 0x8080u) == 0u)
                reserve += ram16((uint32_t)o + 0x0Eu);
        }
    }
    int      cap = 0x50 - (int)d4 - reserve;
    if (cap > HOLO_SPR_MAX)
        cap = HOLO_SPR_MAX;
    if (cap <= 0)
        return;

    int camx = (int16_t)ram16(0xCD14u), camy = (int16_t)ram16(0xCD12u);
    uint16_t v = s_holo_ok.victim;
    int vx = (int16_t)ram16((uint32_t)v + 0x4Au) - camx;
    int vy = (int16_t)ram16((uint32_t)v + 0x4Cu) - camy;           /* pes */
    int dx = s_holo_ok.dr_x - camx, dy = s_holo_ok.dr_y - camy;    /* drone (tela) */
    uint16_t pal_bits = (uint16_t)(HOLO_DRONE_LINE << 13);
    uint16_t base = s_holo_ok.tile0;
    int n = 0;

#define HOLO_PUT(Y, SZ, ATTR, X) do { \
        ram16_write(a2 + 0u, (uint16_t)(((Y) + 128) & 0x3FF)); \
        ram16_write(a2 + 2u, (uint16_t)(((SZ) << 8) | (uint16_t)(d4 + n))); \
        ram16_write(a2 + 4u, (uint16_t)(ATTR)); \
        ram16_write(a2 + 6u, (uint16_t)(((X) + 128) & 0x1FF)); \
        a2 += 8u; ++n; } while (0)

    int beam_top = dy + 20;
    if (s_holo_ok.head) {
        /* cabeca da armadilha no trilho (quadros copiados do objeto $CB da sala de treino):
         * topo 3x2 em (-12,-15) alternando $4C8/$4CE; boca 3x2 fechada (-12,1) ou 4x2
         * aberta (-13,1) enquanto dispara. Prioridade alta e linha 1, como o original. */
        if (s_holo_ok.hremap && dx > -40 && dx < 360 && dy > -40 && dy < 240 && n + 2 <= cap) {
            uint16_t hb = s_holo_ok.hbase;
            int fire = (ph == 3);
            HOLO_PUT(dy - 15, 0x09, 0xA000u | (uint16_t)(hb + (((s_holo_ok.timer >> 2) & 1) ? 6u : 0u)), dx - 12);
            if (fire)
                HOLO_PUT(dy + 1, 0x0D, 0xA000u | (uint16_t)(hb + 0x12u), dx - 13);
            else
                HOLO_PUT(dy + 1, 0x09, 0xA000u | (uint16_t)(hb + 0x0Cu), dx - 12);
        }
        beam_top = dy + 17;
    } else if (dx > -32 && dx < 352 && dy > -40 && dy < 240 && n + 4 <= cap) {
        /* drone (reserva, quando o grupo das armadilhas nao pode ser carregado) */
        HOLO_PUT(dy, 0x05, HOLO_DRONE_ATTR | HOLO_DRONE_BODY, dx - 8);
        if ((s_holo_ok.timer >> 1) & 1) {
            HOLO_PUT(dy + 14, 0x00, HOLO_DRONE_ATTR | 0x00E8u, dx + 3);
            HOLO_PUT(dy + 14, 0x00, HOLO_DRONE_ATTR | 0x00E8u, dx - 10);
            HOLO_PUT(dy + 15, 0x00, HOLO_DRONE_ATTR | 0x00E7u, dx - 4);
        }
    }
    if (ph == 3) {
        int top = beam_top;
        int bot = vy - 4;
        int var = (s_holo_ok.timer >> 1) & 1;
        if (n < cap)
            HOLO_PUT(vy - 12, 0x0D, 0x8000u | pal_bits | (uint16_t)(base + 8), vx - 16);   /* clarao */
        for (int y = top; y < bot && n < cap; y += 8) {
            int rowv = ((y - top) >> 3) & 1;
            HOLO_PUT(y, 0x04, 0x8000u | pal_bits | (uint16_t)(base + var * 4 + rowv * 2), vx - 8);
        }
    }
    if (ph >= 4) {
        /* prioridade alta, como os sprites dos lutadores (senao o cenario fica na frente) */
        uint16_t vpal_bits = (uint16_t)(0x8000u | ((s_holo_ok.vline & 3) << 13));
        /* monte de po: aparece quando um quarto das particulas assentou */
        if (n < cap && s_holo_ok.npart > 0 && s_holo_ok.nsettled * 4 >= s_holo_ok.npart) {
            int ax = s_holo_ok.ash_x - camx, ay = s_holo_ok.ash_y - camy;
            HOLO_PUT(ay - 7, 0x0C, vpal_bits | (uint16_t)(base + 16), ax - 16);
        }
        /* particulas (a vitima ja escondida): densas paradas, ralas ao cair; se faltar
         * vaga na SAT, desenha uma a cada 'pstep' (alternando a cada quadro) */
        int alive = 0;
        for (int i = 0; i < s_holo_ok.npart; ++i)
            alive += (s_holo_ok.pstate[i] >= 0);
        int room = cap - n, pstep = 1;
        while (room > 0 && alive > room * pstep)
            ++pstep;
        for (int i = (s_holo_ok.timer % pstep); i < s_holo_ok.npart && n < cap; i += pstep) {
            int st = s_holo_ok.pstate[i];
            if (st < 0)
                continue;
            int var = (st == 0) ? 0 : (st < 18 ? 1 : 2);
            if (st == 0 && ((s_holo_ok.timer + i) & 7) == 0)
                var = 1;                                             /* cintila antes de cair */
            HOLO_PUT((int)(s_holo_ok.py[i] >> 8) - camy - 4, 0x00,
                     vpal_bits | (uint16_t)(base + 20 + var), (int)(s_holo_ok.px[i] >> 8) - camx - 4);
        }
    }
#undef HOLO_PUT
    if (n == 0)
        return;
    g_cpu.A[2] = (g_cpu.A[2] & 0xFFFF0000u) | (a2 & 0xFFFFu);
    g_cpu.D[4] = (g_cpu.D[4] & 0xFFFF0000u) | (uint16_t)(d4 + n);
}

/* Prepara paletas/tiles e a posicao de entrada do drone (fora da tela, no alto). */
static void holo_ok_prepare(uint16_t v)
{
    s_holo_ok.vline = holo_obj_line(v);
    for (int i = 0; i < 16; ++i)
        s_holo_ok.vpal[i] = ram16(0xF036u + (unsigned)(s_holo_ok.vline * 16 + i) * 2u);
    s_holo_ok.dline = HOLO_DRONE_LINE;
    for (int i = 0; i < 16; ++i)
        s_holo_ok.dpal0[i] = ram16(0xF036u + (unsigned)(s_holo_ok.dline * 16 + i) * 2u);
    holo_build_tiles(s_holo_ok.dline);
    int camx = (int16_t)ram16(0xCD14u), camy = (int16_t)ram16(0xCD12u);
    int vx = (int16_t)ram16((uint32_t)v + 0x4Au);
    /* entra pelo lado mais distante da vitima: a cabeca desliza no trilho do alto
     * (y = $28, o mesmo do objeto $CB); o drone vem voando de cima */
    int from_left = (vx - camx) > 160;
    s_holo_ok.d_x0 = (int16_t)(from_left ? camx - 32 : camx + 352);
    s_holo_ok.d_y0 = (int16_t)(s_holo_ok.head ? HOLO_RAIL_Y : camy - 24);
    s_holo_ok.dr_x = s_holo_ok.d_x0;
    s_holo_ok.dr_y = s_holo_ok.d_y0;
}

static void holo_overkill_tick(void)
{
    if (s_holo_ok.phase == 0)
        return;
    if (ram16(0xFD64u) != 0x000Eu ||
        (s_holo_ok.victim != 0xAB86u && s_holo_ok.victim != 0xAA52u)) {
        memset(&s_holo_ok, 0, sizeof(s_holo_ok));   /* saiu da arena: VRAM/paleta recarregadas */
        return;
    }

    uint32_t sd[8], sa[7];
    for (int i = 0; i < 8; ++i) sd[i] = g_cpu.D[i];
    for (int i = 0; i < 7; ++i) sa[i] = g_cpu.A[i];
    uint16_t ssr = g_cpu.SR;
    g_cpu.A[6] = 0x00C00000u;

    uint16_t v = s_holo_ok.victim;
    ++s_holo_ok.timer;

    /* No modo de teste (treino) o drone original existe: fica escondido durante o efeito. */
    if (s_holo_ok.drone)
        g_ram[((uint32_t)s_holo_ok.drone + 0x20u) & 0xFFFFu] |= 0x80u;

    /* Trajetoria da cabeca (ou do drone), coordenadas de mundo. */
    if (s_holo_ok.phase >= 2) {
        int tx = s_holo_ok.ash_x ? s_holo_ok.ash_x : (int16_t)ram16((uint32_t)v + 0x4Au);
        int ty = s_holo_ok.head ? HOLO_RAIL_Y : (int16_t)ram16((uint32_t)v + 0x4Cu) - 150;
        int nx = tx, ny = ty;
        if (s_holo_ok.phase == 2) {
            /* vitima tonta por HOLO_T_WAIT quadros; depois a cabeca desliza */
            int k = s_holo_ok.timer - HOLO_T_WAIT, kmax = HOLO_T_SLIDE;
            if (k < 0)
                k = 0;
            if (k > kmax)
                k = kmax;
            int e = (k * k * (3 * kmax - 2 * k)) / (kmax * kmax);   /* smoothstep * kmax */
            nx = s_holo_ok.d_x0 + ((tx - s_holo_ok.d_x0) * e) / kmax;
            ny = s_holo_ok.d_y0 + ((ty - s_holo_ok.d_y0) * e) / kmax;
        } else if (s_holo_ok.phase == 3) {
            nx += ((s_holo_ok.timer & 3) == 0) ? 1 : (((s_holo_ok.timer & 3) == 2) ? -1 : 0);
        } else if (s_holo_ok.phase >= 5) {
            /* missao cumprida: volta pelo trilho (o drone sobe) */
            int t = (s_holo_ok.phase == 5) ? s_holo_ok.timer : HOLO_T_EXIT + s_holo_ok.timer;
            int up = (t > 16) ? ((t - 16) * (t - 16)) / 10 : 0;
            if (s_holo_ok.head)
                nx = tx + ((s_holo_ok.d_x0 < tx) ? -up : up);
            else
                ny = ty - up;
        }
        if (s_holo_ok.phase < 5 && !s_holo_ok.head)
            ny += (s_holo_ok.timer >> 3) & 1;                       /* leve flutuacao */
        s_holo_ok.dr_x = (int16_t)nx;
        s_holo_ok.dr_y = (int16_t)ny;
    }

    switch (s_holo_ok.phase) {
    case 1:
        ram16_write(0x9CB4u, 0xFFFFu);
        g_ram[0xFD69u] &= (uint8_t)~0x80u;
        call_68k_sub(0x0005B01Au);
        g_cpu.D[0] = 0x0018u;
        call_68k_sub(0x00026954u);
        call_68k_sfx(0x6Au);
        holo_ok_prepare(v);
        s_holo_ok.phase = 2;
        s_holo_ok.timer = 0;
        break;
    case 2:
        /* enquanto a vitima esta tonta, descobre quais cores da linha 1 ciclam */
        for (int i = 1; i < 16; ++i)
            if (ram16(0xF036u + (unsigned)(s_holo_ok.dline * 16 + i) * 2u) != s_holo_ok.dpal0[i])
                s_holo_ok.cyc_mask |= (uint16_t)(1u << i);
        if (s_holo_ok.head && !s_holo_ok.hremap && s_holo_ok.timer >= HOLO_T_WAIT - 4) {
            holo_remap_head();
            if (s_holo_ok.cyc_mask)
                holo_build_tiles(s_holo_ok.dline);   /* refaz so com cores estaveis */
        }
        if (s_holo_ok.timer >= HOLO_T_WAIT + HOLO_T_SLIDE + 6) {
            if (!s_holo_ok.head && s_holo_ok.cyc_mask)
                holo_build_tiles(s_holo_ok.dline);
            s_holo_ok.phase = 3;
            s_holo_ok.timer = 0;
            if (!s_holo_dbg_active && s_holo_sfx_beam)
                call_68k_sfx(s_holo_sfx_beam);
        }
        break;
    case 3:
        holo_victim_pal(s_holo_ok.timer, HOLO_T_GLOW);
        if (s_holo_ok.timer >= HOLO_T_BEAM) {
            /* desintegra: a vitima some e vira particulas da propria silhueta */
            s_holo_ok.ash_x = (int16_t)ram16((uint32_t)v + 0x4Au);
            s_holo_ok.ash_y = (int16_t)ram16((uint32_t)v + 0x4Cu);
            s_holo_ok.rng = (uint32_t)ram16(0xFD70u) * 2654435761u + 1u;
            holo_build_dust();
            holo_sample_victim();
            g_ram[((uint32_t)v + 0x20u) & 0xFFFFu] |= 0x80u;
            s_holo_ok.phase = 4;
            s_holo_ok.timer = 0;
        }
        break;
    case 4:
        holo_victim_pal(HOLO_T_GLOW + s_holo_ok.timer, HOLO_T_GLOW);
        g_ram[((uint32_t)v + 0x20u) & 0xFFFFu] |= 0x80u;
        holo_particles_step();
        if (s_holo_ok.timer >= HOLO_T_DISINT) {
            s_holo_ok.phase = 5;
            s_holo_ok.timer = 0;
        }
        break;
    case 5:
        g_ram[((uint32_t)v + 0x20u) & 0xFFFFu] |= 0x80u;
        holo_particles_step();
        if (s_holo_ok.timer >= HOLO_T_EXIT) {
            s_holo_ok.phase = 6;
            s_holo_ok.timer = 0;
            if (!s_holo_dbg_active)
                call_68k_sub(0x00016F32u);
        }
        break;
    default:
        break;   /* 6: o monte de po fica ate o fim do round ($5818) ou saida da arena */
    }

    for (int i = 0; i < 8; ++i) g_cpu.D[i] = sd[i];
    for (int i = 0; i < 7; ++i) g_cpu.A[i] = sa[i];
    g_cpu.SR = ssr;
}

/* Tiles do drone presentes na VRAM? (o efeito depende deles) */
static int holo_drone_tiles_loaded(void)
{
    return !holo_tile_is_zero(HOLO_DRONE_BODY) && !holo_tile_is_zero(0x00E8u);
}

/*
 * Diagnostico: EC_HOLO_OK_TEST=<n> arma o overkill no Holo-Trainer (inclusive no modo
 * treino) n quadros depois de entrar na arena, com o P2 ($AA52) como vitima, e roda a
 * animacao a partir deste hook (sem as chamadas de fim de luta $5B01A/$26954/$16F32).
 * So para ver o efeito; nao usar em partida normal.
 */
static void holo_debug_frame(void)
{
    if (s_holo_dbg < 0) {
        const char *e = getenv("EC_HOLO_OK_TEST");
        s_holo_dbg = (e && *e) ? atoi(e) : 0;
        const char *f = getenv("EC_HOLO_OK_SFX");
        if (f && *f)
            s_holo_sfx_beam = (uint32_t)strtoul(f, NULL, 16);
    }
    if (s_holo_dbg <= 0 || ram16(0xFD64u) != 0x000Eu)
        return;
    if (!s_holo_dbg_active) {
        if (++s_holo_dbg_count < s_holo_dbg || s_holo_ok.phase != 0)
            return;
        memset(&s_holo_ok, 0, sizeof(s_holo_ok));
        uint16_t t0 = holo_alloc_tiles();
        if (t0)
            s_holo_ok.head = holo_load_trap_group();
        if (getenv("EC_HOLO_OK_DRONE"))
            s_holo_ok.head = 0;                      /* forca o drone (comparacao) */
        fprintf(stderr, "[HOLO-OK] teste: tiles=%03X cabeca=%d base=%03X\n", t0, s_holo_ok.head, s_holo_ok.hbase);
        if (!t0)
            return;
        s_holo_ok.tile0  = t0;
        s_holo_ok.drone  = holo_find_drone();
        s_holo_ok.victim = 0xAA52u;
        holo_ok_prepare(0xAA52u);
        s_holo_ok.phase = 2;
        s_holo_dbg_active = 1;
        return;
    }
    if (s_holo_ok.phase == 0)
        return;
    holo_overkill_tick();
}

/* Gatilho (de $26A16): 1 se o overkill do Holo foi armado para a vitima a5. */
static int holo_overkill_try_trigger(uint16_t a5)
{
    if (getenv("EC_HOLO_OK_LOG"))
        fprintf(stderr, "[HOLO-OK] gatilho: st=%u vs=%d fase=%d a5=%04X x=%d dd=%02X\n", ram16(0xFD64u),
                is_vs_offlabel_holo(), s_holo_ok.phase, a5, (int16_t)ram16((uint32_t)a5 + 0x42u),
                g_ram[((uint32_t)a5 + 0xDDu) & 0xFFFFu]);
    if (!is_vs_offlabel_holo() || s_holo_ok.phase != 0)
        return 0;
    if (a5 != 0xAB86u && a5 != 0xAA52u)
        return 0;
    if (g_ram[((uint32_t)a5 + 0xDDu) & 0xFFFFu] & 0x40u)
        return 0;
    /* So no centro da arena (mundo 0..960, centro 480). */
    int vx = (int)(int16_t)ram16((uint32_t)a5 + 0x42u);
    if (vx < HOLO_ZONE_MIN || vx > HOLO_ZONE_MAX)
        return 0;
    memset(&s_holo_ok, 0, sizeof(s_holo_ok));
    uint16_t t0 = holo_alloc_tiles();
    if (t0 == 0u) {
        memset(&s_holo_ok, 0, sizeof(s_holo_ok));
        return 0;                                    /* sem VRAM livre: sem overkill */
    }
    s_holo_ok.head = holo_load_trap_group();
    if (!s_holo_ok.head && !holo_drone_tiles_loaded()) {
        holo_free_tiles();
        memset(&s_holo_ok, 0, sizeof(s_holo_ok));
        return 0;
    }
    if (getenv("EC_HOLO_OK_LOG"))
        fprintf(stderr, "[HOLO-OK] armado: tiles=%03X cabeca=%d base=%03X\n", t0, s_holo_ok.head, s_holo_ok.hbase);
    s_holo_ok.tile0  = t0;
    s_holo_ok.drone  = holo_find_drone();            /* so existe no modo treino */
    s_holo_ok.victim = a5;
    s_holo_ok.phase  = 1;
    return 1;
}

/* ---------------------------------------------------------------------------
 * Troca de forma do Eternal Champion jogavel durante a luta (--unlock-boss):
 * segurando MODE (controle de 6 botoes) + X = Tubarao, Y = Dragao, B = Tigre,
 * A = Fenix (forma "Falcon" da ROM). MODE + o botao da forma atual volta a
 * forma neutra (Base).
 *
 * Reaproveita as partes sincronas da transformacao original do chefe
 * (fight_moves1.asm $451CE..$453E0): blob da forma em ($108) + paleta ($172AE),
 * ciclos de brilho ($D782 -> ($120)/($122)), cadeia de golpes preenchida em
 * lugar ($1822A), tabela de golpes ($5E180 + $44160), IA da forma e o objeto
 * $36. Ficam de fora as esperas da tarefa e o clarao do cenario do Santuario
 * ($1DFA6), que estragaria a paleta de fundo das outras arenas: no lugar, o som
 * original da transformacao ($1D) e um clarao branco so na paleta do lutador.
 * O pedido e lido do controle selado do quadro (genesis_sim_pad, deterministico)
 * e aplicado no laco principal do EC ($044274), ou seja, so em neutro.
 * ------------------------------------------------------------------------- */
static const uint32_t k_form_ai[5] = { 0x041C06u, 0x0428E6u, 0x042F42u, 0x043556u, 0x043866u };

static uint16_t s_form_prev_pad[2];
static uint8_t  s_form_req[2];        /* forma + 1 pedida (0 = nenhuma) */
static int      s_form_flash[2];      /* quadros restantes do clarao */
static int      s_form_flash_line[2];
static uint16_t s_form_flash_pal[2][16];
#define EC_FORM_FLASH_FRAMES 18

static uint32_t ram32(uint32_t addr)
{
    return ((uint32_t)ram16(addr) << 16) | ram16(addr + 2u);
}

static void ram32_write(uint32_t addr, uint32_t val)
{
    ram16_write(addr, (uint16_t)(val >> 16));
    ram16_write(addr + 2u, (uint16_t)val);
}

static int ec_form_in_fight(void)
{
    uint16_t st = ram16(0xFD64u);
    return (st == 14u || st == 18u || (st >= 17u && st <= 27u)) && ram16(0x9C7Cu) == 0u;
}

/* Chamado a cada quadro (ec_boss_vblank): le MODE + botao e marca o pedido. */
static void ec_form_poll_input(void)
{
    static int dbg = -1;
    static int dbg_n = 0, dbg_i = 0, dbg_count = 0;
    static int dbg_form[16], dbg_frame[16];
    if (dbg < 0) {
        /* EC_FORM_TEST=<forma>@<quadro>[,<forma>@<quadro>...]: pede formas (0..4) para
         * o P1 nos quadros dados (diagnostico, sem precisar de controle de 6 botoes) */
        const char *e = getenv("EC_FORM_TEST");
        dbg = 0;
        while (e && *e && dbg_n < 16) {
            dbg_form[dbg_n] = atoi(e);
            const char *at = strchr(e, '@');
            dbg_frame[dbg_n] = at ? atoi(at + 1) : 60;
            ++dbg_n;
            dbg = 1;
            e = strchr(e, ',');
            if (e)
                ++e;
        }
    }
    if (dbg > 0)
        ++dbg_count;
    for (int slot = 0; slot < 2; ++slot) {
        uint16_t a5  = slot ? 0xAA52u : 0xAB86u;
        uint16_t pad = genesis_sim_pad(slot);
        uint16_t edge = (uint16_t)(pad & (uint16_t)~s_form_prev_pad[slot]);
        s_form_prev_pad[slot] = pad;
        if (!ec_form_in_fight() || ram16(slot ? 0x9CBCu : 0x9CBEu) != 9u || !is_playable_ec_slot(a5))
            continue;
        int f = -1;
        if (pad & GPAD_MODE) {
            if (edge & GPAD_X)      f = 3;   /* Tubarao */
            else if (edge & GPAD_Y) f = 1;   /* Dragao  */
            else if (edge & GPAD_B) f = 4;   /* Tigre   */
            else if (edge & GPAD_A) f = 2;   /* Fenix   */
        }
        if (dbg > 0 && slot == 0 && dbg_i < dbg_n && dbg_count >= dbg_frame[dbg_i]) {
            if (dbg_form[dbg_i] >= 0 && dbg_form[dbg_i] <= 4)
                f = dbg_form[dbg_i];
            ++dbg_i;
        }
        if (f >= 0) {
            s_form_req[slot] = (uint8_t)(f + 1);
            if (getenv("EC_FORM_LOG"))
                fprintf(stderr, "[EC-FORM] pedido slot=%d forma=%d\n", slot, f);
        }
    }
}

/* Clarao: a paleta do lutador sai do branco para as cores da nova forma. */
static void ec_form_flash_tick(void)
{
    for (int slot = 0; slot < 2; ++slot) {
        if (s_form_flash[slot] <= 0)
            continue;
        int k = s_form_flash[slot]--;            /* EC_FORM_FLASH_FRAMES..1 */
        int line = s_form_flash_line[slot];
        for (int i = 1; i < 16; ++i) {
            uint16_t c = s_form_flash_pal[slot][i];
            int r = (c >> 1) & 7, g = (c >> 5) & 7, b = (c >> 9) & 7;
            int t = (k - 1) * 8 / EC_FORM_FLASH_FRAMES;   /* 7..0 */
            r += ((7 - r) * t) / 7; g += ((7 - g) * t) / 7; b += ((7 - b) * t) / 7;
            ram16_write(0xF036u + (unsigned)(line * 16 + i) * 2u,
                        (uint16_t)((b << 9) | (g << 5) | (r << 1)));
        }
        ram16_write(0xEF32u, 0xFFFFu);
    }
}

static void ec_form_reset(void)
{
    memset(s_form_req, 0, sizeof(s_form_req));
    memset(s_form_flash, 0, sizeof(s_form_flash));
}

static void ec_form_apply(uint16_t a5, int slot, int f)
{
    uint32_t sd[8], sa[7];
    for (int i = 0; i < 8; ++i) sd[i] = g_cpu.D[i];
    for (int i = 0; i < 7; ++i) sa[i] = g_cpu.A[i];
    uint16_t ssr = g_cpu.SR;
    g_cpu.A[5] = 0x00FF0000u | a5;
    g_cpu.A[6] = 0x00C00000u;

    int side = (g_ram[((uint32_t)a5 + 0x6Du) & 0xFFFFu] & 0x01u) ? 1 : 0;
    uint32_t blob = k_form_blob[f];

    /* desliga os ciclos de brilho da forma anterior */
    uint16_t c = ram16((uint32_t)a5 + 0x120u);
    if (c)
        ram16_write(c, 0u);
    c = ram16((uint32_t)a5 + 0x122u);
    if (c)
        ram16_write(c, 0u);

    ram32_write((uint32_t)a5 + 0x108u, blob);
    g_cpu.A[0] = blob;
    call_68k_sub(0x000172AEu);                               /* paleta da forma */

    g_cpu.A[0] = k_form_pal1[side][f];
    g_cpu.D[0] = 3u;
    call_68k_sub(0x0000D782u);
    ram16_write((uint32_t)a5 + 0x120u, (uint16_t)g_cpu.A[0]);
    g_cpu.A[0] = k_form_pal2[side][f];
    g_cpu.D[0] = 3u;
    call_68k_sub(0x0000D782u);
    ram16_write((uint32_t)a5 + 0x122u, (uint16_t)g_cpu.A[0]);

    uint32_t head = ram32(side ? 0x9920u : 0x991Cu);
    if (head != 0u) {
        g_cpu.A[0] = blob;
        g_cpu.A[3] = head;
        call_68k_sub(0x0001822Au);                           /* golpes da forma */
        g_cpu.A[1] = side ? 0xFFFF994Cu : 0xFFFF9A30u;
        g_cpu.D[0] = head;
        call_68k_sub(0x0005E180u);                           /* tabela de golpes */
        g_cpu.A[0] = ram32((uint32_t)a5 + 0x98u);
        call_68k_sub(0x00044160u);
    }

    ram32_write(side ? 0xAB64u : 0xAC98u, k_form_ai[f]);

    /* objeto $36 da forma, recriado como na transformacao original (se existir) */
    uint16_t optr = side ? 0x9678u : 0x967Au;
    uint16_t old  = ram16(optr);
    if (old != 0u) {
        g_ram[((uint32_t)old + 0x74u) & 0xFFFFu] |= 0x04u;
        g_ram[((uint32_t)old + 0x74u) & 0xFFFFu] &= (uint8_t)~0x80u;
        g_cpu.D[0] = 0x36u;
        g_cpu.D[1] = 0u;
        g_cpu.D[2] = 0u;
        call_68k_sub(0x0000DCEAu);
        if (!(g_cpu.SR & SR_C) && (uint16_t)g_cpu.A[0] != 0u) {
            uint16_t no = (uint16_t)g_cpu.A[0];
            ram16_write(optr, no);
            ram16_write((uint32_t)no + 0x7Cu, side ? 0xAA52u : 0xAB86u);
            ram16_write((uint32_t)no + 0x7Eu, side ? 0xAB86u : 0xAA52u);
        }
    }

    /* volta ao estado de pe com as animacoes da nova forma (como o final original) */
    ram16_write((uint32_t)a5 + 0x82u, (uint16_t)f);
    ram16_write((uint32_t)a5 + 0xDCu, 0u);
    ram16_write((uint32_t)a5 + 0x100u, 0u);
    ram16_write((uint32_t)a5 + 0xA8u, 0x8000u);
    g_ram[((uint32_t)a5 + 0xE7u) & 0xFFFFu] |= 0x01u;

    /* som original da transformacao */
    g_cpu.A[7] -= 4u;
    m68k_write32(g_cpu.A[7], 0x1Du);
    call_68k_sub(0x000008A8u);
    g_cpu.A[7] += 4u;

    s_ec_form[slot] = (uint8_t)f;
    s_form_flash_line[slot] = side ? 2 : 3;
    for (int i = 0; i < 16; ++i)
        s_form_flash_pal[slot][i] = ram16(0xF036u + (unsigned)(s_form_flash_line[slot] * 16 + i) * 2u);
    s_form_flash[slot] = EC_FORM_FLASH_FRAMES;

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

    /* Laco principal do EC jogavel ($044274, logo apos o jsr $DB14 que marca a retomada da tarefa; neutro): aplica a troca de forma pedida
     * com MODE + X/Y/A/B (ver ec_form_apply). */
    case 0x044274u: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        if (!is_playable_ec_slot(a5))
            return 0;
        int slot = fighter_slot_from_a5(a5);
        if (ram16(slot ? 0x9CBCu : 0x9CBEu) != 9u)           /* personagem 9 = EC */
            return 0;
        if (!s_form_req[slot])
            return 0;
        int f = s_form_req[slot] - 1;
        s_form_req[slot] = 0;
        if (getenv("EC_FORM_LOG"))
            fprintf(stderr, "[EC-FORM] aplica slot=%d forma=%d atual=%u luta=%d hp=%d\n", slot, f,
                    ram16((uint32_t)a5 + 0x82u), ec_form_in_fight(), (int16_t)ram16((uint32_t)a5 + 0x68u));
        /* MODE + o botao da forma atual: volta a forma neutra (Base) */
        if (f == (int)ram16((uint32_t)a5 + 0x82u))
            f = 0;
        if (f != (int)ram16((uint32_t)a5 + 0x82u) && ec_form_in_fight() &&
            (int16_t)ram16((uint32_t)a5 + 0x68u) > 0)
            ec_form_apply(a5, slot, f);
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
        sanc_restore_wall_if_needed(); /* KO novo no meio da fase 3 */
        holo_ok_reset(1);
        s_sanc_ok.phase = 0;
        if (is_vs_offlabel_sanctuary())
            g_cpu.SR |= (uint16_t)SR_Z;
        return 0;

    case 0x026A16u: {
        uint16_t a5 = (uint16_t)g_cpu.A[5];
        int holo = holo_overkill_try_trigger(a5);
        if (!holo && (!is_vs_offlabel_sanctuary() || s_sanc_ok.phase != 0))
            return 0;
        if (!holo) {
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
        }

        ram16_write(0x9C74u, a5);
        /* 1 = vitima arremessada (vitral); 2 = vitima fica no lugar (caminho do RAX) */
        ram16_write(0x9C7Cu, holo ? 2u : 1u);
        ram16_write(0x9C7Au, 0x0004u);
        ram16_write(0x9C76u, 0x0017u);
        ram16_write(0x9C7Eu, 1u);
        if (holo) {
            /* $9C7A/$9C76 = alvo X/Y da camera no overkill ($1F410/$1F4FA): centraliza
             * na vitima (como o caminho generico, x - $96) e mantem o Y atual. */
            int cx = (int)(int16_t)ram16((uint32_t)a5 + 0x42u) - 0x96;
            if (cx < 0) cx = 0;
            if (cx > 0x278) cx = 0x278;
            ram16_write(0x9C7Au, (uint16_t)cx);
            ram16_write(0x9C76u, ram16(0xCD16u));
        }

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

        if (!holo) {
            s_sanc_ok.phase = 1;
            s_sanc_ok.timer = 0;
            s_sanc_ok.victim = a5;
        }

        g_cpu.SR = (uint16_t)((g_cpu.SR & 0xFF00u) | SR_C);
        g_cpu.PC = m68k_read32(g_cpu.A[7]);
        g_cpu.A[7] += 4u;
        return 1;
    }

    case 0x01F4FAu:
        if (s_sanc_ok.phase != 0)
            sanc_overkill_tick();
        if (s_holo_ok.phase != 0)
            holo_overkill_tick();
        return 0;

    /* Overkill do Holo-Trainer: sprites do raio/cinzas no inicio da SAT ($E68C:
     * Sub_E684 com d4 = ($DF7A) e a2 = ($DF7C) ja carregados). */
    case 0x00E68Cu:
        holo_debug_frame();
        if (s_holo_ok.phase != 0)
            holo_inject_sprites();
        return 0;

    default:
        return 0;
    }
}
