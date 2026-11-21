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

void ec_boss_vblank(void)
{
    (void)s_unlock_boss;
    /* (hook reservado; sem trabalho por frame no modo atual) */
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
    return 1;
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

    /* Fim de partida no estado 18 (santuário off-label): o handler do boss
     * escreve o próximo estado como se a luta contra o EC tivesse terminado
     * ($59E0 → estado 8 = finalização; $59E8 → estado 10). Em VS off-label
     * substituímos por estado 1 (menu), como nos arenas normais.
     *
     * NOTA: hooks com return 1 "abandonam" a função nativa — em pontos de
     * fall-through (como $59E0) isso desmonta o unwind e trava o jogo. Por
     * isso o hook fica nos RTS ($59E6/$59EE, logo após os writes), com
     * return 0: sobrescreve $9CB6 e deixa o fluxo seguir intacto. */
    case 0x059E6u:
    case 0x059EEu: {
        /* VS off-label do santuário: fim de luta sem o EC no elenco →
         * menu (estado 1). Se há EC na luta (cenário do chefe), o fluxo
         * natural do boss segue: estado 8 (arte/transformação) → 6 → 10. */
        if (g_ram[0x936Fu] == 11u &&
            ram16(0x9CBCu) != 9u && ram16(0x9CBEu) != 9u)
            ram16_write(0x9CB6u, 1u);
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

    default:
        return 0;
    }
}
