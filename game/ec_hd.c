/*
 * ec_hd.c — Mode C (--hd-manifest) runtime asset loader & 4x OpenGL compositor
 * for Eternal Champions.
 *
 * Host-presentation only: reads live VDP VRAM/CRAM/SAT read-only, never alters
 * guest WRAM, VDP state, snapshots, or M3 determinism.
 *
 * Architecture:
 * - Manifest JSON is indexed once at startup (410 sprites + 27 planes in <20ms).
 * - PNG files (original_png + 4x hd_file) are decoded lazily on first hit via libpng
 *   and cached in VRAM.
 * - Planes (plane_a / plane_b, 512x256 -> 2048x1024): matched per 8x8 tile in
 *   quantized 9-bit Genesis RGB against original_png. Animated tiles, faded palettes,
 *   and widescreen-streamed columns automatically fall back to Mode B+ / native.
 * - Sprites (8x8..32x32 -> 32x32..128x128): matched by exact SHA-256 (matching
 *   tools/extract/extract_assets.py) on both direct and H-flipped buffers, plus live
 *   CRAM palette verification so hit flashes, fades, and mirror-match palettes never
 *   draw wrong colors.
 * - Exact VDP priority, H-scroll, V-scroll, Shadow/Highlight, and sprite-vs-plane
 *   occlusion are preserved via a single per-frame native-res RGBA16UI lookup map.
 */

#include "ec_hd.h"
#include "genesis_dac.h"
#include "genesis_runtime.h"

#include <ctype.h>
#include <limits.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#define EC_HD_HAS_GL 1
#else
#include <SDL2/SDL.h>
#define EC_HD_HAS_GL 0
#endif

#define EC_HD_SCALE        4
#define EC_HD_MAX_STATES   64
#define EC_HD_SPR_HASH_CAP 2048u /* power of 2, > 4x 410 sprites */

/* ---- Minimal self-contained SHA-256 (FIPS 180-4) ------------------------- */

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    size_t   buflen;
} EcSha256;

static const uint32_t k_sha256_k[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static inline uint32_t sha_rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }

static void sha256_transform(EcSha256 *c, const uint8_t blk[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)blk[i * 4 + 0] << 24) | ((uint32_t)blk[i * 4 + 1] << 16) |
               ((uint32_t)blk[i * 4 + 2] << 8)  | (uint32_t)blk[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = sha_rotr(w[i - 15], 7) ^ sha_rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = sha_rotr(w[i - 2], 17) ^ sha_rotr(w[i - 2], 19)  ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = c->state[0], b = c->state[1], cc = c->state[2], d = c->state[3];
    uint32_t e = c->state[4], f = c->state[5], g = c->state[6], h = c->state[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = sha_rotr(e, 6) ^ sha_rotr(e, 11) ^ sha_rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + k_sha256_k[i] + w[i];
        uint32_t S0 = sha_rotr(a, 2) ^ sha_rotr(a, 13) ^ sha_rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d;
    c->state[4] += e; c->state[5] += f; c->state[6] += g;  c->state[7] += h;
}

static void sha256_init(EcSha256 *c)
{
    c->state[0] = 0x6a09e667u; c->state[1] = 0xbb67ae85u;
    c->state[2] = 0x3c6ef372u; c->state[3] = 0xa54ff53au;
    c->state[4] = 0x510e527fu; c->state[5] = 0x9b05688cu;
    c->state[6] = 0x1f83d9abu; c->state[7] = 0x5be0cd19u;
    c->bitlen = 0;
    c->buflen = 0;
}

static void sha256_update(EcSha256 *c, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        c->buf[c->buflen++] = data[i];
        if (c->buflen == 64) {
            sha256_transform(c, c->buf);
            c->bitlen += 512u;
            c->buflen = 0;
        }
    }
}

static void sha256_final(EcSha256 *c, uint8_t out[32])
{
    c->bitlen += (uint64_t)c->buflen * 8u;
    c->buf[c->buflen++] = 0x80u;
    if (c->buflen > 56) {
        while (c->buflen < 64) c->buf[c->buflen++] = 0;
        sha256_transform(c, c->buf);
        c->buflen = 0;
    }
    while (c->buflen < 56) c->buf[c->buflen++] = 0;
    for (int i = 7; i >= 0; --i)
        c->buf[c->buflen++] = (uint8_t)(c->bitlen >> (i * 8));
    sha256_transform(c, c->buf);
    for (int i = 0; i < 8; ++i) {
        out[i * 4 + 0] = (uint8_t)(c->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(c->state[i]);
    }
}

/* ---- Asset catalog data structures --------------------------------------- */

typedef struct {
    uint8_t  key[32];
    char     orig_path[512];
    char     hd_path[512];
    int      width;   /* native pixels */
    int      height;
    int      scale;
    int      pal_id;
    /* Lazy-loaded CPU reference & GPU texture */
    int      tried_load;
    int      loaded;
    uint16_t ref_pal_rgb9[16]; /* bit15=used, bits0..8 = (b3<<6)|(g3<<3)|r3 */
    uint8_t *hd_rgba;          /* temporary until GL upload, then freed */
    int      hd_w, hd_h;
#if EC_HD_HAS_GL
    GLuint   gl_tex;
#endif
} EcHdSpriteAsset;

typedef struct {
    int      present;
    char     orig_path[512];
    char     hd_path[512];
    int      width;
    int      height;
    int      scale;
    int      tried_load;
    int      loaded;
    uint64_t tile_hash[32][64]; /* quantized 9-bit RGB FNV-1a hash per 8x8 tile */
    uint8_t  tile_opaque[32][64];
    uint8_t *hd_rgba;           /* freed after GL upload */
    int      hd_w, hd_h;
#if EC_HD_HAS_GL
    GLuint   gl_tex;
#endif
} EcHdPlaneAsset;

typedef struct {
    int      valid;
    int      asset_idx; /* index into s_sprites[] */
    int      hflip_draw;
    int      ox0, sy0;
    int      spr_w, spr_h;
    int      link;
} EcHdFrameSprite;

/* Global catalog state */
static int             s_manifest_loaded = 0;
static int             s_total_entries = 0;
static int             s_approved_entries = 0;
static char            s_asset_root[512] = "";

static EcHdSpriteAsset *s_sprites = NULL;
static int              s_sprite_count = 0;
static int              s_sprite_cap = 0;
static int              s_spr_ht[EC_HD_SPR_HASH_CAP]; /* -1 = empty, else index in s_sprites */

/* s_planes[state_id][0=plane_b, 1=plane_a] */
static EcHdPlaneAsset   s_planes[EC_HD_MAX_STATES][2];

/* Per-frame capture state (populated in ec_hd_begin_frame + ec_hd_record_pixel) */
static int              s_frame_armed = 0;
static int              s_frame_w = 0;
static int              s_frame_h = 0;
static uint16_t         s_frame_state = 0;
static uint8_t          s_plane_tile_match[2][32][64]; /* [0=B, 1=A] */
static int              s_plane_has_match[2] = {0, 0};
static int              s_sat_match_slot[80]; /* -1 or index in s_frame_sprites */
static EcHdFrameSprite  s_frame_sprites[80];
static int              s_frame_sprite_count = 0;

/* Per-native-pixel lookup uploaded as GL_RGBA16UI: [y][x] * 4 uint16_t */
static uint16_t         s_pixel_map[GVDP_MAX_HEIGHT][GVDP_MAX_WIDTH][4];

/* Runtime metrics */
static uint64_t s_stat_frames = 0;
static uint64_t s_stat_plane_px = 0;
static uint64_t s_stat_total_px = 0;
static uint64_t s_stat_spr_matched = 0;
static uint64_t s_stat_spr_total = 0;

/* ---- Path & PNG helpers -------------------------------------------------- */

static void resolve_asset_path(const char *rel, char *out, size_t out_sz)
{
    if (!rel || !*rel) {
        out[0] = '\0';
        return;
    }
    if (rel[0] == '/' || (isalpha((unsigned char)rel[0]) && rel[1] == ':')) {
        snprintf(out, out_sz, "%s", rel);
        return;
    }
    if (s_asset_root[0]) {
        int n = snprintf(out, out_sz, "%.*s/%s",
                         (int)(out_sz > 2 ? out_sz / 2 : 0), s_asset_root, rel);
        if (n > 0 && (size_t)n < out_sz) {
            FILE *fp = fopen(out, "rb");
            if (fp) {
                fclose(fp);
                return;
            }
        }
    }
    FILE *fp = fopen(rel, "rb");
    if (fp) {
        fclose(fp);
        snprintf(out, out_sz, "%s", rel);
        return;
    }
    snprintf(out, out_sz, "../../%s", rel);
    fp = fopen(out, "rb");
    if (fp) {
        fclose(fp);
        return;
    }
    snprintf(out, out_sz, "%s", rel);
}

static uint8_t *load_png_rgba(const char *path, int *out_w, int *out_h)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;

    uint8_t sig[8];
    if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8) != 0) {
        fclose(fp);
        return NULL;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return NULL;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return NULL;
    }

    uint8_t *pixels = NULL;
    png_bytep *rows = NULL;
    if (setjmp(png_jmpbuf(png))) {
        free(rows);
        free(pixels);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    png_uint_32 w = png_get_image_width(png, info);
    png_uint_32 h = png_get_image_height(png, info);
    int bit_depth = png_get_bit_depth(png, info);
    int color_type = png_get_color_type(png, info);

    if (w == 0 || h == 0 || w > 4096 || h > 4096) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }

    if (bit_depth == 16)
        png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    pixels = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
    rows = (png_bytep *)malloc((size_t)h * sizeof(png_bytep));
    if (!pixels || !rows) {
        free(rows);
        free(pixels);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }
    for (png_uint_32 y = 0; y < h; ++y)
        rows[y] = pixels + (size_t)y * (size_t)w * 4u;

    png_read_image(png, rows);
    png_read_end(png, NULL);

    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    *out_w = (int)w;
    *out_h = (int)h;
    return pixels;
}

static inline uint16_t rgba_to_qrgb9(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (a < 128u)
        return 0u;
    unsigned r3 = (unsigned)genesis_dac_nearest_normal_idx(r);
    unsigned g3 = (unsigned)genesis_dac_nearest_normal_idx(g);
    unsigned b3 = (unsigned)genesis_dac_nearest_normal_idx(b);
    return (uint16_t)(0x8000u | (b3 << 6) | (g3 << 3) | r3);
}

static inline uint16_t cram_to_qrgb9(uint16_t cram_word)
{
    unsigned r3 = (cram_word >> 1) & 7u;
    unsigned g3 = (cram_word >> 5) & 7u;
    unsigned b3 = (cram_word >> 9) & 7u;
    return (uint16_t)(0x8000u | (b3 << 6) | (g3 << 3) | r3);
}

static inline uint64_t fnv1a_step(uint64_t h, uint16_t v)
{
    h ^= (uint64_t)(v & 0xFFu);
    h *= 1099511628211ull;
    h ^= (uint64_t)(v >> 8);
    h *= 1099511628211ull;
    return h;
}

/* ---- Manifest JSON parser ------------------------------------------------ */

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex32(const char *s, uint8_t out[32])
{
    if (!s || strlen(s) != 64)
        return 0;
    for (int i = 0; i < 32; ++i) {
        int hi = hex_nibble(s[i * 2]);
        int lo = hex_nibble(s[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return 0;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 1;
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        ++p;
    return p;
}

static const char *parse_json_string(const char *p, char *out, size_t out_sz)
{
    p = skip_ws(p);
    if (*p != '"')
        return NULL;
    ++p;
    size_t n = 0;
    while (*p && *p != '"') {
        char ch = *p++;
        if (ch == '\\' && *p) {
            char esc = *p++;
            switch (esc) {
            case 'n': ch = '\n'; break;
            case 'r': ch = '\r'; break;
            case 't': ch = '\t'; break;
            default:  ch = esc;  break;
            }
        }
        if (out && n + 1 < out_sz)
            out[n++] = ch;
    }
    if (out && out_sz > 0)
        out[n] = '\0';
    if (*p != '"')
        return NULL;
    return p + 1;
}

static const char *skip_json_value(const char *p)
{
    p = skip_ws(p);
    if (*p == '"')
        return parse_json_string(p, NULL, 0);
    if (*p == '{' || *p == '[') {
        char open = *p++;
        char close = (open == '{') ? '}' : ']';
        int depth = 1;
        while (*p && depth > 0) {
            if (*p == '"') {
                p = parse_json_string(p, NULL, 0);
                if (!p) return NULL;
                continue;
            }
            if (*p == open) depth++;
            else if (*p == close) depth--;
            ++p;
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' &&
           *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
        ++p;
    return p;
}

static uint32_t spr_key_hash(const uint8_t key[32])
{
    uint32_t h = ((uint32_t)key[0] << 24) | ((uint32_t)key[1] << 16) |
                 ((uint32_t)key[2] << 8)  | (uint32_t)key[3];
    h ^= ((uint32_t)key[4] << 24) | ((uint32_t)key[5] << 16) |
         ((uint32_t)key[6] << 8)  | (uint32_t)key[7];
    return h;
}

static int find_sprite_asset(const uint8_t key[32])
{
    uint32_t mask = EC_HD_SPR_HASH_CAP - 1u;
    uint32_t slot = spr_key_hash(key) & mask;
    for (uint32_t step = 0; step < EC_HD_SPR_HASH_CAP; ++step) {
        int idx = s_spr_ht[slot];
        if (idx < 0)
            return -1;
        if (memcmp(s_sprites[idx].key, key, 32) == 0)
            return idx;
        slot = (slot + 1u) & mask;
    }
    return -1;
}

static void insert_sprite_asset(const uint8_t key[32], const char *orig_path,
                                const char *hd_path, int w, int h, int scale, int pal_id)
{
    if (s_sprite_count >= (int)(EC_HD_SPR_HASH_CAP / 2u))
        return;
    if (s_sprite_count >= s_sprite_cap) {
        int ncap = s_sprite_cap ? s_sprite_cap * 2 : 512;
        EcHdSpriteAsset *na = (EcHdSpriteAsset *)realloc(s_sprites, (size_t)ncap * sizeof(*na));
        if (!na)
            return;
        s_sprites = na;
        s_sprite_cap = ncap;
    }
    int idx = s_sprite_count++;
    EcHdSpriteAsset *a = &s_sprites[idx];
    memset(a, 0, sizeof(*a));
    memcpy(a->key, key, 32);
    resolve_asset_path(orig_path, a->orig_path, sizeof(a->orig_path));
    resolve_asset_path(hd_path, a->hd_path, sizeof(a->hd_path));
    a->width = w;
    a->height = h;
    a->scale = scale;
    a->pal_id = pal_id;

    uint32_t mask = EC_HD_SPR_HASH_CAP - 1u;
    uint32_t slot = spr_key_hash(key) & mask;
    while (s_spr_ht[slot] >= 0)
        slot = (slot + 1u) & mask;
    s_spr_ht[slot] = idx;
}

static void derive_asset_root(const char *manifest_path)
{
    s_asset_root[0] = '\0';
    if (!manifest_path)
        return;
    /* Strip trailing /pipeline/manifest.json or /manifest.json */
    const char *p = strstr(manifest_path, "/pipeline/");
    if (p && p > manifest_path) {
        size_t len = (size_t)(p - manifest_path);
        if (len >= sizeof(s_asset_root)) len = sizeof(s_asset_root) - 1;
        memcpy(s_asset_root, manifest_path, len);
        s_asset_root[len] = '\0';
        return;
    }
    const char *slash = strrchr(manifest_path, '/');
    if (slash) {
        size_t len = (size_t)(slash - manifest_path);
        if (len >= sizeof(s_asset_root)) len = sizeof(s_asset_root) - 1;
        memcpy(s_asset_root, manifest_path, len);
        s_asset_root[len] = '\0';
        /* If manifest is inside pipeline/, step one directory up */
        size_t sl = strlen(s_asset_root);
        if (sl >= 8 && strcmp(s_asset_root + sl - 8, "pipeline") == 0) {
            if (sl == 8) s_asset_root[0] = '\0';
            else if (s_asset_root[sl - 9] == '/') s_asset_root[sl - 9] = '\0';
        }
    }
}

int ec_hd_load_manifest(const char *path)
{
    if (!path || !*path)
        return -1;
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[EC_HD] manifest não encontrado: %s (fallback Modo A/B ativo)\n", path);
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    long sz = ftell(fp);
    if (sz < 0 || sz > 32L * 1024L * 1024L || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    char *buf = (char *)malloc((size_t)sz + 1u);
    if (!buf) { fclose(fp); return -1; }
    size_t nread = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[nread] = '\0';

    /* Reset previous index */
    ec_hd_destroy_gpu();
    free(s_sprites);
    s_sprites = NULL;
    s_sprite_count = 0;
    s_sprite_cap = 0;
    for (unsigned i = 0; i < EC_HD_SPR_HASH_CAP; ++i)
        s_spr_ht[i] = -1;
    memset(s_planes, 0, sizeof(s_planes));
    s_total_entries = 0;
    s_approved_entries = 0;
    s_manifest_loaded = 0;

    derive_asset_root(path);

    const char *p = strstr(buf, "\"entries\"");
    if (!p) {
        free(buf);
        fprintf(stderr, "[EC_HD] manifest sem bloco \"entries\": %s\n", path);
        return -1;
    }
    p = strchr(p + 9, '{');
    if (!p) {
        free(buf);
        return -1;
    }
    ++p;

    int planes_indexed = 0;
    while (*p) {
        p = skip_ws(p);
        if (*p == '}')
            break;
        if (*p == ',') {
            ++p;
            continue;
        }
        char key_str[128];
        p = parse_json_string(p, key_str, sizeof(key_str));
        if (!p) break;
        p = skip_ws(p);
        if (*p != ':') break;
        ++p;
        p = skip_ws(p);
        if (*p != '{') {
            p = skip_json_value(p);
            if (!p) break;
            continue;
        }
        ++p; /* enter entry object */

        char orig_png[512] = "";
        char hd_file[512] = "";
        char layer[32] = "";
        char status[32] = "";
        int width = 0, height = 0, scale = 4, pal_id = 0, state_id = -1;

        while (*p) {
            p = skip_ws(p);
            if (*p == '}') { ++p; break; }
            if (*p == ',') { ++p; continue; }
            char field[64];
            p = parse_json_string(p, field, sizeof(field));
            if (!p) break;
            p = skip_ws(p);
            if (*p != ':') break;
            ++p;
            p = skip_ws(p);

            if (strcmp(field, "original_png") == 0 && *p == '"') {
                p = parse_json_string(p, orig_png, sizeof(orig_png));
            } else if (strcmp(field, "hd_file") == 0 && *p == '"') {
                p = parse_json_string(p, hd_file, sizeof(hd_file));
            } else if (strcmp(field, "layer") == 0 && *p == '"') {
                p = parse_json_string(p, layer, sizeof(layer));
            } else if (strcmp(field, "status") == 0 && *p == '"') {
                p = parse_json_string(p, status, sizeof(status));
            } else if (strcmp(field, "width") == 0) {
                width = (int)strtol(p, (char **)&p, 10);
            } else if (strcmp(field, "height") == 0) {
                height = (int)strtol(p, (char **)&p, 10);
            } else if (strcmp(field, "scale") == 0) {
                scale = (int)strtol(p, (char **)&p, 10);
            } else if (strcmp(field, "palette_id") == 0) {
                pal_id = (int)strtol(p, (char **)&p, 10);
            } else if (strcmp(field, "state_id") == 0 && (*p == '-' || isdigit((unsigned char)*p))) {
                state_id = (int)strtol(p, (char **)&p, 10);
            } else {
                p = skip_json_value(p);
            }
            if (!p) break;
        }
        if (!p) break;

        s_total_entries++;
        if (strcmp(status, "approved") == 0 && hd_file[0] != '\0' && orig_png[0] != '\0' && scale == EC_HD_SCALE) {
            s_approved_entries++;
            if (strcmp(layer, "sprite") == 0) {
                uint8_t key_bytes[32];
                if (parse_hex32(key_str, key_bytes))
                    insert_sprite_asset(key_bytes, orig_png, hd_file, width, height, scale, pal_id);
            } else if ((strcmp(layer, "plane_b") == 0 || strcmp(layer, "plane_a") == 0) &&
                       state_id >= 0 && state_id < EC_HD_MAX_STATES &&
                       width == 512 && height == 256) {
                int pi = (strcmp(layer, "plane_a") == 0) ? 1 : 0;
                EcHdPlaneAsset *pa = &s_planes[state_id][pi];
                pa->present = 1;
                resolve_asset_path(orig_png, pa->orig_path, sizeof(pa->orig_path));
                resolve_asset_path(hd_file, pa->hd_path, sizeof(pa->hd_path));
                pa->width = width;
                pa->height = height;
                pa->scale = scale;
                planes_indexed++;
            }
        }
    }

    free(buf);
    s_manifest_loaded = 1;
    fprintf(stderr,
            "[EC_HD] manifest '%s': %d entradas (%d approved: %d sprites, %d planes; lazy load)\n",
            path, s_total_entries, s_approved_entries, s_sprite_count, planes_indexed);
    return s_total_entries;
}

int ec_hd_is_active(void) { return s_manifest_loaded && s_approved_entries > 0; }
int ec_hd_frame_active(void) { return s_frame_armed; }
int ec_hd_total_entries(void) { return s_total_entries; }
int ec_hd_approved_entries(void) { return s_approved_entries; }

/* ---- Lazy CPU reference loading ------------------------------------------ */

static int ensure_plane_cpu_loaded(EcHdPlaneAsset *pa)
{
    if (!pa || !pa->present)
        return 0;
    if (pa->loaded)
        return 1;
    if (pa->tried_load)
        return 0;
    pa->tried_load = 1;

    int ow = 0, oh = 0;
    uint8_t *orig = load_png_rgba(pa->orig_path, &ow, &oh);
    if (!orig || ow != 512 || oh != 256) {
        free(orig);
        fprintf(stderr, "[EC_HD] falha ao carregar plane original: %s\n", pa->orig_path);
        return 0;
    }

    /* Precompute quantized 9-bit RGB FNV-1a hash per 8x8 tile */
    for (int ty = 0; ty < 32; ++ty) {
        for (int tx = 0; tx < 64; ++tx) {
            uint64_t h = 14695981039346656037ull;
            uint8_t any_op = 0;
            for (int py = 0; py < 8; ++py) {
                const uint8_t *row = orig + ((size_t)(ty * 8 + py) * 512u + (size_t)(tx * 8)) * 4u;
                for (int px = 0; px < 8; ++px) {
                    uint16_t q = rgba_to_qrgb9(row[px * 4 + 0], row[px * 4 + 1],
                                               row[px * 4 + 2], row[px * 4 + 3]);
                    if (q) any_op = 1;
                    h = fnv1a_step(h, q);
                }
            }
            pa->tile_hash[ty][tx] = h;
            pa->tile_opaque[ty][tx] = any_op;
        }
    }
    free(orig);

    int hw = 0, hh = 0;
    uint8_t *hd = load_png_rgba(pa->hd_path, &hw, &hh);
    if (!hd || hw != 512 * EC_HD_SCALE || hh != 256 * EC_HD_SCALE) {
        free(hd);
        fprintf(stderr, "[EC_HD] falha ao carregar plane HD: %s\n", pa->hd_path);
        return 0;
    }
    pa->hd_rgba = hd;
    pa->hd_w = hw;
    pa->hd_h = hh;
    pa->loaded = 1;
    return 1;
}

static int ensure_sprite_cpu_loaded(EcHdSpriteAsset *sa, const uint8_t *nibble_buf, int hflip_hit)
{
    if (!sa)
        return 0;
    if (sa->loaded)
        return 1;
    if (sa->tried_load)
        return 0;
    sa->tried_load = 1;

    int ow = 0, oh = 0;
    uint8_t *orig = load_png_rgba(sa->orig_path, &ow, &oh);
    if (!orig || ow != sa->width || oh != sa->height) {
        free(orig);
        return 0;
    }

    /* Recover reference 9-bit Genesis RGB palette entries for all non-zero nibbles */
    memset(sa->ref_pal_rgb9, 0, sizeof(sa->ref_pal_rgb9));
    for (int y = 0; y < oh; ++y) {
        for (int x = 0; x < ow; ++x) {
            int nx = hflip_hit ? (ow - 1 - x) : x;
            uint8_t nib = nibble_buf[y * ow + nx];
            if (nib == 0 || nib >= 16)
                continue;
            const uint8_t *px = orig + ((size_t)y * (size_t)ow + (size_t)x) * 4u;
            if (!sa->ref_pal_rgb9[nib])
                sa->ref_pal_rgb9[nib] = rgba_to_qrgb9(px[0], px[1], px[2], px[3]);
        }
    }
    free(orig);

    int hw = 0, hh = 0;
    uint8_t *hd = load_png_rgba(sa->hd_path, &hw, &hh);
    if (!hd || hw != sa->width * EC_HD_SCALE || hh != sa->height * EC_HD_SCALE) {
        free(hd);
        return 0;
    }
    sa->hd_rgba = hd;
    sa->hd_w = hw;
    sa->hd_h = hh;
    sa->loaded = 1;
    return 1;
}

static inline int qrgb9_channel_diff(uint16_t a, uint16_t b)
{
    int dr = (int)(a & 7u) - (int)(b & 7u);
    int dg = (int)((a >> 3) & 7u) - (int)((b >> 3) & 7u);
    int db = (int)((a >> 6) & 7u) - (int)((b >> 6) & 7u);
    if (dr < 0) dr = -dr;
    if (dg < 0) dg = -dg;
    if (db < 0) db = -db;
    int m = dr > dg ? dr : dg;
    return m > db ? m : db;
}

static inline uint16_t hd_ram16(unsigned a)
{
    a &= 0xFFFFu;
    return (uint16_t)(((uint16_t)g_ram[a] << 8) | g_ram[(a + 1u) & 0xFFFFu]);
}

static int sprite_palette_matches_cram(const EcHdSpriteAsset *sa, const GVDP *v, int pal_id)
{
    int base = (pal_id & 3) * 16;
    int close_mismatches = 0;
    for (int nib = 1; nib < 16; ++nib) {
        uint16_t ref = sa->ref_pal_rgb9[nib];
        if (!ref)
            continue;
        uint16_t live = cram_to_qrgb9(v->cram[base + nib]);
        if (live != ref) {
            /* Allow minor 1-step ambient flicker (e.g. Salem firelight), but reject
             * fades, hit-flashes, and mirror-match alternate palettes. */
            if (qrgb9_channel_diff(live, ref) > 1 || ++close_mismatches > 3)
                return 0;
        }
    }
    return 1;
}

/* ---- Per-frame VDP matcher ----------------------------------------------- */

static inline uint16_t hd_vram16(const GVDP *v, uint16_t addr)
{
    uint16_t a0 = (uint16_t)(addr & 0xFFFEu);
    return (uint16_t)(((uint16_t)v->vram[a0] << 8) | v->vram[(uint16_t)(a0 + 1u)]);
}

static void match_live_plane(const GVDP *v, int pi, uint16_t base_addr, EcHdPlaneAsset *pa)
{
    s_plane_has_match[pi] = 0;
    memset(s_plane_tile_match[pi], 0, sizeof(s_plane_tile_match[pi]));
    if (!ensure_plane_cpu_loaded(pa))
        return;

    uint16_t cram_live[64];
    uint16_t cram_shdw[64];
    int luma_live = 0, luma_shdw = 0;
    int any_cram_diff = 0;
    for (int i = 0; i < 64; ++i) {
        if ((i & 15) == 0) {
            cram_live[i] = 0u;
            cram_shdw[i] = 0u;
        } else {
            uint16_t ql = cram_to_qrgb9(v->cram[i]);
            uint16_t qs = cram_to_qrgb9(hd_ram16(0x9BB0u + (unsigned)i * 2u));
            cram_live[i] = ql;
            cram_shdw[i] = qs;
            luma_live += (int)((ql & 7u) + ((ql >> 3) & 7u) + ((ql >> 6) & 7u));
            luma_shdw += (int)((qs & 7u) + ((qs >> 3) & 7u) + ((qs >> 6) & 7u));
            if (ql != qs)
                any_cram_diff = 1;
        }
    }
    /* When live CRAM is not fading/flashing (luma within 80%..120% of RAM shadow $9BB0),
     * allow fallback matching against $9BB0 so ambient CRAM palette cycling (Salem fire,
     * Sanctuary glow) keeps full HD planes while screen fades still drop out cleanly. */
    int allow_shdw = any_cram_diff && luma_shdw > 0 &&
                     luma_live * 100 >= luma_shdw * 80 &&
                     luma_live * 100 <= luma_shdw * 120;

    int matches = 0;
    for (int ty = 0; ty < 32; ++ty) {
        for (int tx = 0; tx < 64; ++tx) {
            if (!pa->tile_opaque[ty][tx])
                continue;
            uint16_t nt = (uint16_t)(base_addr + (uint16_t)((ty * 64 + tx) * 2));
            uint16_t e  = hd_vram16(v, nt);
            int ti      = e & 0x07FF;
            int pal_off = ((e >> 13) & 3) * 16;
            int hf      = (e & 0x0800u) != 0;
            int vf      = (e & 0x1000u) != 0;

            uint64_t hl = 14695981039346656037ull;
            uint64_t hs = 14695981039346656037ull;
            const uint8_t *tptr = v->vram + ((size_t)ti * 32u);
            for (int py = 0; py < 8; ++py) {
                int sy = vf ? (7 - py) : py;
                const uint8_t *row = tptr + (size_t)sy * 4u;
                for (int px = 0; px < 8; ++px) {
                    int sx = hf ? (7 - px) : px;
                    uint8_t byte = row[sx >> 1];
                    int nib = (sx & 1) ? (byte & 0x0F) : (byte >> 4);
                    uint16_t ql = nib ? cram_live[pal_off + nib] : 0u;
                    hl = fnv1a_step(hl, ql);
                    if (allow_shdw) {
                        uint16_t qs = nib ? cram_shdw[pal_off + nib] : 0u;
                        hs = fnv1a_step(hs, qs);
                    }
                }
            }
            if (hl == pa->tile_hash[ty][tx] || (allow_shdw && hs == pa->tile_hash[ty][tx])) {
                s_plane_tile_match[pi][ty][tx] = 1u;
                matches++;
            }
        }
    }
    s_plane_has_match[pi] = (matches > 0);
}

static void match_live_sprites(const GVDP *v, int left, int x0, const uint8_t sat_is_hud[80])
{
    s_frame_sprite_count = 0;
    for (int i = 0; i < 80; ++i)
        s_sat_match_slot[i] = -1;
    if (s_sprite_count <= 0 || !(v->reg[1] & 0x40u))
        return;

    int h40 = (v->reg[12] & 0x81u) != 0;
    int im2 = (v->reg[12] & 0x06u) == 0x06u;
    if (im2)
        return;
    int max_sprites = h40 ? 80 : 64;
    uint16_t sat = (uint16_t)((v->reg[5] & 0x7Fu) << 9);

    uint8_t visited[80];
    memset(visited, 0, sizeof(visited));
    uint8_t buf[32 * 32];
    uint8_t buf_hf[32 * 32];

    int link = 0;
    for (int n = 0; n < max_sprites; ++n) {
        if (link < 0 || link >= max_sprites || visited[link])
            break;
        visited[link] = 1;

        uint16_t e    = (uint16_t)(sat + link * 8);
        uint16_t w0   = hd_vram16(v, e);
        uint16_t w1   = hd_vram16(v, (uint16_t)(e + 2));
        uint16_t attr = hd_vram16(v, (uint16_t)(e + 4));
        uint16_t w3   = hd_vram16(v, (uint16_t)(e + 6));
        if (w0 == 0 && w1 == 0 && attr == 0 && w3 == 0)
            break;

        int next = (int)(w1 & 0x7Fu);
        if ((w0 & 0x3FFu) != 0u) {
            int wt     = ((w1 >> 10) & 3) + 1;
            int ht     = ((w1 >> 8) & 3) + 1;
            int W      = wt * 8;
            int H      = ht * 8;
            int pal_id = (attr >> 13) & 3;
            int hf     = (attr & 0x0800u) != 0;
            int vf     = (attr & 0x1000u) != 0;
            int bt     = attr & 0x07FF;

            memset(buf, 0, (size_t)(W * H));
            memset(buf_hf, 0, (size_t)(W * H));
            int non_empty = 0;
            for (int tx = 0; tx < wt; ++tx) {
                for (int ty = 0; ty < ht; ++ty) {
                    int tid = (bt + tx * ht + ty) & 0x07FF;
                    const uint8_t *tptr = v->vram + ((size_t)tid * 32u);
                    for (int sy = 0; sy < 8; ++sy) {
                        int src_y = ty * 8 + sy;
                        int dy = vf ? (H - 1 - src_y) : src_y;
                        const uint8_t *row = tptr + (size_t)sy * 4u;
                        for (int sx = 0; sx < 8; ++sx) {
                            uint8_t byte = row[sx >> 1];
                            uint8_t nib = (uint8_t)((sx & 1) ? (byte & 0x0F) : (byte >> 4));
                            if (nib == 0)
                                continue;
                            non_empty = 1;
                            int src_x = tx * 8 + sx;
                            int dx = hf ? (W - 1 - src_x) : src_x;
                            buf[dy * W + dx] = nib;
                            buf_hf[dy * W + (W - 1 - dx)] = nib;
                        }
                    }
                }
            }

            if (non_empty) {
                s_stat_spr_total++;
                uint8_t hdr[11];
                memcpy(hdr, "SPRITE", 6);
                hdr[6]  = (uint8_t)((W >> 8) & 0xFF);
                hdr[7]  = (uint8_t)(W & 0xFF);
                hdr[8]  = (uint8_t)((H >> 8) & 0xFF);
                hdr[9]  = (uint8_t)(H & 0xFF);
                hdr[10] = (uint8_t)pal_id;

                uint8_t digest[32];
                EcSha256 sha;
                sha256_init(&sha);
                sha256_update(&sha, hdr, sizeof(hdr));
                sha256_update(&sha, buf, (size_t)(W * H));
                sha256_final(&sha, digest);

                int aidx = find_sprite_asset(digest);
                int hflip_draw = 0;
                if (aidx < 0) {
                    sha256_init(&sha);
                    sha256_update(&sha, hdr, sizeof(hdr));
                    sha256_update(&sha, buf_hf, (size_t)(W * H));
                    sha256_final(&sha, digest);
                    aidx = find_sprite_asset(digest);
                    if (aidx >= 0)
                        hflip_draw = 1;
                }

                if (aidx >= 0) {
                    EcHdSpriteAsset *sa = &s_sprites[aidx];
                    if (ensure_sprite_cpu_loaded(sa, buf, hflip_draw) &&
                        sprite_palette_matches_cram(sa, v, pal_id)) {
                        int sx0 = (int)(int16_t)w3 - 128;
                        int sy0 = (int)(w0 & 0x3FFu) - 128;
                        int base_x = (sat_is_hud && sat_is_hud[link]) ? left : x0;
                        int fslot = s_frame_sprite_count++;
                        EcHdFrameSprite *fs = &s_frame_sprites[fslot];
                        fs->valid      = 1;
                        fs->asset_idx  = aidx;
                        fs->hflip_draw = hflip_draw;
                        fs->ox0        = base_x + sx0;
                        fs->sy0        = sy0;
                        fs->spr_w      = W;
                        fs->spr_h      = H;
                        fs->link       = link;
                        s_sat_match_slot[link] = fslot;
                        s_stat_spr_matched++;
                    }
                }
            }
        }

        if (next == 0 || next >= max_sprites)
            break;
        link = next;
    }
}

void ec_hd_begin_frame(const GVDP *vdp, uint16_t state_id,
                       int width, int height, int left, int x0,
                       const uint8_t sat_is_hud[80])
{
    s_frame_armed = 0;
    if (!ec_hd_is_active() || !vdp || width <= 0 || height <= 0 ||
        width > GVDP_MAX_WIDTH || height > GVDP_MAX_HEIGHT)
        return;

    s_frame_w = width;
    s_frame_h = height;
    s_frame_state = state_id;

    /* Clear lookup map: layer=0, spr_owner=0xFF (no matched HD sprite) */
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            s_pixel_map[y][x][0] = 0u;
            s_pixel_map[y][x][1] = 0u;
            s_pixel_map[y][x][2] = 0u;
            s_pixel_map[y][x][3] = 0x00FFu;
        }
    }

    int wt = ((vdp->reg[16] & 0x03u) == 1u) ? 64 : (((vdp->reg[16] & 0x03u) == 3u) ? 128 : 32);
    int ht = (((vdp->reg[16] >> 4) & 0x03u) == 1u) ? 64 : ((((vdp->reg[16] >> 4) & 0x03u) == 3u) ? 128 : 32);
    int im2 = (vdp->reg[12] & 0x06u) == 0x06u;

    s_plane_has_match[0] = 0;
    s_plane_has_match[1] = 0;
    if (!im2 && wt == 64 && ht == 32 && state_id < EC_HD_MAX_STATES && (vdp->reg[1] & 0x40u)) {
        uint16_t base_a = (uint16_t)((vdp->reg[2] & 0x38u) << 10);
        uint16_t base_b = (uint16_t)((vdp->reg[4] & 0x07u) << 13);
        if (s_planes[state_id][0].present)
            match_live_plane(vdp, 0, base_b, &s_planes[state_id][0]);
        if (s_planes[state_id][1].present)
            match_live_plane(vdp, 1, base_a, &s_planes[state_id][1]);
    }

    match_live_sprites(vdp, left, x0, sat_is_hud);
    s_frame_armed = 1;
}

void ec_hd_record_pixel(int line, int ox,
                        int win_layer,
                        int bg_layer, int bg_px, int bg_py, int bg_streamed,
                        int spr_link, int level)
{
    if (!s_frame_armed || line < 0 || line >= s_frame_h || ox < 0 || ox >= s_frame_w)
        return;

    s_stat_total_px++;
    uint16_t *dst = s_pixel_map[line][ox];

    int spr_matched = (win_layer == EC_HD_LAYER_SPRITE &&
                       spr_link >= 0 && spr_link < 80 &&
                       s_sat_match_slot[spr_link] >= 0);

    /* If an UNMATCHED sprite won at this pixel, leave bg=0 and spr=0xFF so B+/native
     * renders the sprite untouched (never overwrite an unmatched fighter with HD BG). */
    if (win_layer == EC_HD_LAYER_SPRITE && !spr_matched) {
        dst[2] = 0u;
        dst[3] = 0x00FFu;
        return;
    }

    /* Check whether the winning (or underlying, for matched sprites) plane tile matched HD */
    int use_plane = 0;
    if (!bg_streamed && (bg_layer == EC_HD_LAYER_PLANE_B || bg_layer == EC_HD_LAYER_PLANE_A)) {
        int pi = (bg_layer == EC_HD_LAYER_PLANE_A) ? 1 : 0;
        int px = bg_px & 511;
        int py = bg_py & 255;
        if (s_plane_has_match[pi] && s_plane_tile_match[pi][py >> 3][px >> 3]) {
            dst[0] = (uint16_t)px;
            dst[1] = (uint16_t)py;
            dst[2] = (uint16_t)((unsigned)bg_layer | (((unsigned)(level & 3)) << 2));
            use_plane = 1;
            s_stat_plane_px++;
        }
    }
    if (!use_plane)
        dst[2] = 0u;

    if (spr_matched)
        dst[3] = (uint16_t)((unsigned)(spr_link & 0xFF) | (((unsigned)(level & 3)) << 8));
    else
        dst[3] = 0x00FFu;
}

/* Fallback frame evaluator for non-widescreen (4:3) or non-arena states (S7 Title, S9 Menu) */
static void evaluate_4x3_frame(const GVDP *v, uint16_t state_id, int width, int height)
{
    uint8_t no_hud[80];
    memset(no_hud, 0, sizeof(no_hud));
    ec_hd_begin_frame(v, state_id, width, height, 0, 0, no_hud);
    if (!s_frame_armed)
        return;
    if (!s_plane_has_match[0] && !s_plane_has_match[1] && s_frame_sprite_count == 0)
        return;

    int h40     = (v->reg[12] & 0x81u) != 0;
    int im2     = (v->reg[12] & 0x06u) == 0x06u;
    if (im2 || !(v->reg[1] & 0x40u))
        return;
    int sh_mode = (v->reg[12] & 0x08u) != 0;
    int wt      = ((v->reg[16] & 0x03u) == 1u) ? 64 : (((v->reg[16] & 0x03u) == 3u) ? 128 : 32);
    int ht      = (((v->reg[16] >> 4) & 0x03u) == 1u) ? 64 : ((((v->reg[16] >> 4) & 0x03u) == 3u) ? 128 : 32);
    uint16_t base_a = (uint16_t)((v->reg[2] & 0x38u) << 10);
    uint16_t base_b = (uint16_t)((v->reg[4] & 0x07u) << 13);
    uint16_t hbase  = (uint16_t)((v->reg[13] & 0x3Fu) << 10);
    int hmode       = v->reg[11] & 0x03;
    int vmode_2cell = (v->reg[11] & 0x04u) != 0;
    int full_va     = (int)(v->vsram[0] & 0x3FFu);
    int full_vb     = (int)(v->vsram[1] & 0x3FFu);
    int max_sprites = h40 ? 80 : 64;
    uint16_t sat    = (uint16_t)((v->reg[5] & 0x7Fu) << 9);

    for (int line = 0; line < height; ++line) {
        uint8_t s_op[GVDP_MAX_WIDTH], s_hi[GVDP_MAX_WIDTH], s_sh[GVDP_MAX_WIDTH], s_hl[GVDP_MAX_WIDTH];
        uint8_t s_lnk[GVDP_MAX_WIDTH], drawn[GVDP_MAX_WIDTH];
        memset(s_op, 0, (size_t)width);
        memset(s_hi, 0, (size_t)width);
        memset(s_sh, 0, (size_t)width);
        memset(s_hl, 0, (size_t)width);
        memset(drawn, 0, (size_t)width);

        int link = 0, any_nz = 0;
        for (int n = 0; n < max_sprites; ++n) {
            uint16_t e = (uint16_t)(sat + link * 8);
            uint16_t w0 = hd_vram16(v, e);
            uint16_t w1 = hd_vram16(v, (uint16_t)(e + 2));
            uint16_t attr = hd_vram16(v, (uint16_t)(e + 4));
            uint16_t w3 = hd_vram16(v, (uint16_t)(e + 6));
            if (w0 == 0 && w1 == 0 && attr == 0 && w3 == 0) break;
            int y = (int)(w0 & 0x3FFu) - 128;
            int hsz = ((w1 >> 10) & 3) + 1;
            int vsz = ((w1 >> 8) & 3) + 1;
            int spr_h = vsz * 8, spr_w = hsz * 8;
            int next = (int)(w1 & 0x7Fu);
            if (line >= y && line < y + spr_h) {
                if ((w3 & 0x3FFu) == 0u) { if (any_nz) break; } else any_nz = 1;
                int sx0 = (int)(int16_t)w3 - 128;
                int tile = attr & 0x07FF;
                int pal = (attr >> 13) & 3;
                int pri = (attr & 0x8000u) != 0;
                int hf = (attr & 0x0800u) != 0;
                int vf = (attr & 0x1000u) != 0;
                int iy = vf ? (spr_h - 1 - (line - y)) : (line - y);
                int cy = iy >> 3, fy = iy & 7;
                for (int lx = 0; lx < spr_w; ++lx) {
                    int ox = sx0 + lx;
                    if (ox < 0 || ox >= width || drawn[ox]) continue;
                    int ix = hf ? (spr_w - 1 - lx) : lx;
                    int tid = (tile + (ix >> 3) * vsz + cy) & 0x07FF;
                    uint8_t byte = v->vram[(uint16_t)(tid * 32 + fy * 4 + ((ix & 7) >> 1))];
                    int nib = (ix & 1) ? (byte & 0x0F) : (byte >> 4);
                    if (nib == 0) continue;
                    drawn[ox] = 1;
                    if (sh_mode && pal == 3 && nib == 14) { s_hl[ox] = 1; s_hi[ox] = (uint8_t)pri; }
                    else if (sh_mode && pal == 3 && nib == 15) { s_sh[ox] = 1; s_hi[ox] = (uint8_t)pri; }
                    else { s_op[ox] = 1; s_hi[ox] = (uint8_t)pri; s_lnk[ox] = (uint8_t)link; }
                }
            }
            if (next == 0 || next >= max_sprites) break;
            link = next;
        }

        int hidx = (hmode == 0) ? 0 : (hmode == 2) ? (line & ~7) : line;
        int hs_a = (int)(hd_vram16(v, (uint16_t)(hbase + hidx * 4 + 0)) & 0x3FFu);
        int hs_b = (int)(hd_vram16(v, (uint16_t)(hbase + hidx * 4 + 2)) & 0x3FFu);

        for (int ox = 0; ox < width; ++ox) {
            int va = full_va, vb = full_vb;
            if (vmode_2cell) {
                int col = (ox >> 4) * 2;
                va = (int)(v->vsram[col % GVDP_VSRAM_ENTRIES] & 0x3FFu);
                vb = (int)(v->vsram[(col + 1) % GVDP_VSRAM_ENTRIES] & 0x3FFu);
            }
            int px_a = (ox - hs_a) & (wt * 8 - 1), py_a = (line + va) & (ht * 8 - 1);
            int px_b = (ox - hs_b) & (wt * 8 - 1), py_b = (line + vb) & (ht * 8 - 1);

            uint16_t ea = hd_vram16(v, (uint16_t)(base_a + ((py_a >> 3) * wt + (px_a >> 3)) * 2));
            int fxa = (ea & 0x0800u) ? ((px_a & 7) ^ 7) : (px_a & 7);
            int fya = (ea & 0x1000u) ? ((py_a & 7) ^ 7) : (py_a & 7);
            uint8_t ba = v->vram[(uint16_t)((ea & 0x07FF) * 32 + fya * 4 + (fxa >> 1))];
            int op_a = (((fxa & 1) ? (ba & 0x0F) : (ba >> 4)) != 0);
            int hi_a = (ea & 0x8000u) != 0;

            uint16_t eb = hd_vram16(v, (uint16_t)(base_b + ((py_b >> 3) * wt + (px_b >> 3)) * 2));
            int fxb = (eb & 0x0800u) ? ((px_b & 7) ^ 7) : (px_b & 7);
            int fyb = (eb & 0x1000u) ? ((py_b & 7) ^ 7) : (py_b & 7);
            uint8_t bb = v->vram[(uint16_t)((eb & 0x07FF) * 32 + fyb * 4 + (fxb >> 1))];
            int op_b = (((fxb & 1) ? (bb & 0x0F) : (bb >> 4)) != 0);
            int hi_b = (eb & 0x8000u) != 0;

            int win = EC_HD_LAYER_NONE;
            if      (s_op[ox] && s_hi[ox]) win = EC_HD_LAYER_SPRITE;
            else if (op_a && hi_a)         win = EC_HD_LAYER_PLANE_A;
            else if (op_b && hi_b)         win = EC_HD_LAYER_PLANE_B;
            else if (s_op[ox])             win = EC_HD_LAYER_SPRITE;
            else if (op_a)                 win = EC_HD_LAYER_PLANE_A;
            else if (op_b)                 win = EC_HD_LAYER_PLANE_B;

            int bg_layer = EC_HD_LAYER_NONE, bg_px = 0, bg_py = 0;
            if      (op_a && hi_a) { bg_layer = EC_HD_LAYER_PLANE_A; bg_px = px_a; bg_py = py_a; }
            else if (op_b && hi_b) { bg_layer = EC_HD_LAYER_PLANE_B; bg_px = px_b; bg_py = py_b; }
            else if (op_a)         { bg_layer = EC_HD_LAYER_PLANE_A; bg_px = px_a; bg_py = py_a; }
            else if (op_b)         { bg_layer = EC_HD_LAYER_PLANE_B; bg_px = px_b; bg_py = py_b; }

            int level = GENESIS_DAC_NORMAL;
            if (sh_mode) {
                int plane_hi = (op_a && hi_a) || (op_b && hi_b);
                int any_hi = plane_hi || (s_op[ox] && s_hi[ox]);
                level = any_hi ? GENESIS_DAC_NORMAL : GENESIS_DAC_SHADOW;
                if (s_hi[ox] || !plane_hi) {
                    if (s_hl[ox]) level = (level == GENESIS_DAC_SHADOW) ? GENESIS_DAC_NORMAL : GENESIS_DAC_HIGHLIGHT;
                    else if (s_sh[ox]) level = GENESIS_DAC_SHADOW;
                }
            }

            ec_hd_record_pixel(line, ox, win, bg_layer, bg_px, bg_py, 0,
                               s_op[ox] ? (int)s_lnk[ox] : -1, level);
        }
    }
}

/* ---- OpenGL 3.3+ 4x Mode C compositor ------------------------------------ */

#if EC_HD_HAS_GL

#define HD_GL_API(X) \
    X(GLenum, GetError, (void)) \
    X(void, GetIntegerv, (GLenum, GLint*)) \
    X(void, GetBooleanv, (GLenum, GLboolean*)) \
    X(GLboolean, IsEnabled, (GLenum)) \
    X(void, Enable, (GLenum)) \
    X(void, Disable, (GLenum)) \
    X(void, BlendFunc, (GLenum, GLenum)) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(void, PixelStorei, (GLenum, GLint)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, GenTextures, (GLsizei, GLuint*)) \
    X(void, DeleteTextures, (GLsizei, const GLuint*)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void, DeleteShader, (GLuint)) \
    X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, BindFragDataLocation, (GLuint, GLuint, const GLchar*)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void, UseProgram, (GLuint)) \
    X(void, DeleteProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
    X(void, Uniform2i, (GLint, GLint, GLint)) \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, GenVertexArrays, (GLsizei, GLuint*)) \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))

typedef struct {
#define HD_DECL(ret, name, args) ret (APIENTRY *name) args;
    HD_GL_API(HD_DECL)
#undef HD_DECL
} EcHdGlFn;

typedef struct {
    SDL_Renderer *renderer;
    SDL_GLContext context;
    SDL_Texture  *output;
    EcHdGlFn      gl;
    GLuint        vao;
    GLuint        plane_prog;
    GLuint        spr_prog;
    GLuint        tex_map;
    GLuint        tex_raw;
    GLuint        tex_dummy;
    int           in_w, in_h, out_w, out_h;
    int           failed;

    GLint pl_u_base, pl_u_map, pl_u_plane_b, pl_u_plane_a;
    GLint pl_u_base_4x, pl_u_base_uv, pl_u_out_size;

    GLint sp_u_rect, sp_u_out_size, sp_u_spr_size, sp_u_hflip;
    GLint sp_u_tex, sp_u_map, sp_u_link;
} EcHdGpu;

static EcHdGpu *s_gpu = NULL;
static int      s_gpu_checked = 0;

static const GLenum k_hd_switches[8] = {
    GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST,
    GL_CULL_FACE, GL_DITHER, GL_FRAMEBUFFER_SRGB, GL_RASTERIZER_DISCARD
};

static const GLenum k_hd_stores[6] = {
    GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_PIXELS,
    GL_UNPACK_SKIP_ROWS, GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST
};

typedef struct {
    GLint     program, active_tex, vao, unpack_buf;
    GLint     blend_src, blend_dst;
    GLint     viewport[4], scissor[4];
    GLint     tex2d[4], unpack[6];
    GLboolean mask[4], enabled[8];
} EcHdGlState;

static const char s_plane_vs[] =
    "#version 330 core\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char s_plane_fs[] =
    "#version 330 core\n"
    "uniform sampler2D  u_base;\n"
    "uniform usampler2D u_map;\n"
    "uniform sampler2D  u_plane_b;\n"
    "uniform sampler2D  u_plane_a;\n"
    "uniform int   u_base_4x;\n"
    "uniform vec2  u_base_uv;\n"
    "uniform ivec2 u_out_size;\n"
    "out vec4 result;\n"
    "vec3 apply_level(vec3 c, uint lv) {\n"
    "    if (lv == 1u) return c * 0.5;\n"
    "    if (lv == 2u) return 0.5 + 0.5 * c;\n"
    "    return c;\n"
    "}\n"
    "void main() {\n"
    "    ivec2 op = ivec2(gl_FragCoord.xy);\n"
    "    vec2 uv = ((vec2(op) + 0.5) / vec2(u_out_size)) * u_base_uv;\n"
    "    vec4 col = (u_base_4x != 0) ? texelFetch(u_base, op, 0) : texture(u_base, uv);\n"
    "    col.a = 1.0;\n"
    "    ivec2 np = op >> 2;\n"
    "    ivec2 sub = op & 3;\n"
    "    uvec4 m = texelFetch(u_map, np, 0);\n"
    "    uint layer = m.b & 3u;\n"
    "    uint lv    = (m.b >> 2) & 3u;\n"
    "    if (layer == 1u || layer == 2u) {\n"
    "        ivec2 tc = ivec2(int(m.r) * 4 + sub.x, int(m.g) * 4 + sub.y);\n"
    "        vec4 hd = (layer == 1u) ? texelFetch(u_plane_b, tc, 0)\n"
    "                                : texelFetch(u_plane_a, tc, 0);\n"
    "        if (hd.a > 0.01) {\n"
    "            vec3 rgb = apply_level(hd.rgb, lv);\n"
    "            col.rgb = mix(col.rgb, rgb, hd.a);\n"
    "        }\n"
    "    }\n"
    "    result = col;\n"
    "}\n";

static const char s_spr_vs[] =
    "#version 330 core\n"
    "uniform vec4  u_rect;\n"
    "uniform vec2  u_out_size;\n"
    "uniform ivec2 u_spr_size;\n"
    "uniform int   u_hflip;\n"
    "out vec2 v_px;\n"
    "void main() {\n"
    "    const vec2 k[4] = vec2[4](vec2(0,0), vec2(1,0), vec2(0,1), vec2(1,1));\n"
    "    vec2 c = k[gl_VertexID];\n"
    "    vec2 pos = u_rect.xy + c * u_rect.zw;\n"
    "    vec2 ndc = (pos / u_out_size) * 2.0 - 1.0;\n"
    "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "    vec2 uv = vec2(u_hflip != 0 ? (1.0 - c.x) : c.x, c.y);\n"
    "    v_px = uv * vec2(u_spr_size);\n"
    "}\n";

static const char s_spr_fs[] =
    "#version 330 core\n"
    "uniform sampler2D  u_tex;\n"
    "uniform usampler2D u_map;\n"
    "uniform ivec2      u_spr_size;\n"
    "uniform int        u_link;\n"
    "in vec2 v_px;\n"
    "out vec4 result;\n"
    "void main() {\n"
    "    ivec2 op = ivec2(gl_FragCoord.xy);\n"
    "    uvec4 m = texelFetch(u_map, op >> 2, 0);\n"
    "    uint owner = m.a & 0xFFu;\n"
    "    if (owner != uint(u_link))\n"
    "        discard;\n"
    "    ivec2 sp = clamp(ivec2(floor(v_px)), ivec2(0), u_spr_size - 1);\n"
    "    vec4 c = texelFetch(u_tex, sp, 0);\n"
    "    if (c.a <= 0.01)\n"
    "        discard;\n"
    "    uint lv = (m.a >> 8) & 3u;\n"
    "    if (lv == 1u) c.rgb *= 0.5;\n"
    "    else if (lv == 2u) c.rgb = 0.5 + 0.5 * c.rgb;\n"
    "    result = c;\n"
    "}\n";

static int hd_load_gl(EcHdGlFn *g)
{
#define HD_LOAD(ret, name, args) \
    g->name = (ret (APIENTRY *) args)SDL_GL_GetProcAddress("gl" #name); \
    if (!g->name) { fprintf(stderr, "[EC_HD] missing GL symbol: gl%s\n", #name); return 0; }
    HD_GL_API(HD_LOAD)
#undef HD_LOAD
    return 1;
}

static void hd_state_begin(const EcHdGlFn *g, EcHdGlState *s)
{
    g->GetIntegerv(GL_CURRENT_PROGRAM, &s->program);
    g->GetIntegerv(GL_ACTIVE_TEXTURE, &s->active_tex);
    g->GetIntegerv(GL_VERTEX_ARRAY_BINDING, &s->vao);
    g->GetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &s->unpack_buf);
    g->GetIntegerv(GL_BLEND_SRC_RGB, &s->blend_src);
    g->GetIntegerv(GL_BLEND_DST_RGB, &s->blend_dst);
    g->GetIntegerv(GL_VIEWPORT, s->viewport);
    g->GetIntegerv(GL_SCISSOR_BOX, s->scissor);
    g->GetBooleanv(GL_COLOR_WRITEMASK, s->mask);
    for (int i = 0; i < 8; ++i) {
        s->enabled[i] = g->IsEnabled(k_hd_switches[i]);
        g->Disable(k_hd_switches[i]);
    }
    for (int i = 0; i < 6; ++i)
        g->GetIntegerv(k_hd_stores[i], &s->unpack[i]);
    for (int i = 0; i < 4; ++i) {
        g->ActiveTexture(GL_TEXTURE0 + (GLenum)i);
        g->GetIntegerv(GL_TEXTURE_BINDING_2D, &s->tex2d[i]);
    }
    g->BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    g->PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (int i = 1; i < 6; ++i)
        g->PixelStorei(k_hd_stores[i], 0);
    g->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

static void hd_state_end(const EcHdGlFn *g, const EcHdGlState *s)
{
    g->BindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)s->unpack_buf);
    g->UseProgram((GLuint)s->program);
    g->BindVertexArray((GLuint)s->vao);
    for (int i = 0; i < 6; ++i)
        g->PixelStorei(k_hd_stores[i], s->unpack[i]);
    for (int i = 0; i < 4; ++i) {
        g->ActiveTexture(GL_TEXTURE0 + (GLenum)i);
        g->BindTexture(GL_TEXTURE_2D, (GLuint)s->tex2d[i]);
    }
    g->ActiveTexture((GLenum)s->active_tex);
    g->Viewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
    g->Scissor(s->scissor[0], s->scissor[1], s->scissor[2], s->scissor[3]);
    g->ColorMask(s->mask[0], s->mask[1], s->mask[2], s->mask[3]);
    g->BlendFunc((GLenum)s->blend_src, (GLenum)s->blend_dst);
    for (int i = 0; i < 8; ++i) {
        if (s->enabled[i]) g->Enable(k_hd_switches[i]);
        else g->Disable(k_hd_switches[i]);
    }
}

static GLuint hd_compile_sh(const EcHdGlFn *g, GLenum type, const char *src)
{
    GLuint sh = g->CreateShader(type);
    if (!sh) return 0;
    g->ShaderSource(sh, 1, &src, NULL);
    g->CompileShader(sh);
    GLint ok = 0;
    g->GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        g->GetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "[EC_HD] shader compile error: %s\n", log);
        g->DeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint hd_link_prog(const EcHdGlFn *g, const char *vs_s, const char *fs_s)
{
    GLuint vs = hd_compile_sh(g, GL_VERTEX_SHADER, vs_s);
    GLuint fs = hd_compile_sh(g, GL_FRAGMENT_SHADER, fs_s);
    GLuint p = 0;
    if (vs && fs) {
        p = g->CreateProgram();
        if (p) {
            g->AttachShader(p, vs);
            g->AttachShader(p, fs);
            g->BindFragDataLocation(p, 0, "result");
            g->LinkProgram(p);
            GLint ok = 0;
            g->GetProgramiv(p, GL_LINK_STATUS, &ok);
            if (!ok) {
                char log[1024] = {0};
                g->GetProgramInfoLog(p, sizeof(log), NULL, log);
                fprintf(stderr, "[EC_HD] program link error: %s\n", log);
                g->DeleteProgram(p);
                p = 0;
            }
        }
    }
    if (vs) g->DeleteShader(vs);
    if (fs) g->DeleteShader(fs);
    return p;
}

static GLuint hd_upload_rgba_tex(const EcHdGlFn *g, const uint8_t *rgba, int w, int h)
{
    GLuint t = 0;
    g->GenTextures(1, &t);
    g->ActiveTexture(GL_TEXTURE3);
    g->BindTexture(GL_TEXTURE_2D, t);
    g->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return t;
}

static EcHdGpu *hd_gpu_create(SDL_Renderer *renderer, SDL_Texture *activation)
{
    SDL_RendererInfo info = {0};
    if (!renderer || !activation || SDL_GetRendererInfo(renderer, &info) != 0 ||
        !info.name || strcmp(info.name, "opengl") != 0 ||
        !(info.flags & SDL_RENDERER_TARGETTEXTURE)) {
        fprintf(stderr, "[EC_HD] requer SDL opengl renderer com target texture\n");
        return NULL;
    }
    if (SDL_GL_BindTexture(activation, NULL, NULL) != 0)
        return NULL;
    SDL_GL_UnbindTexture(activation);

    EcHdGpu *gpu = (EcHdGpu *)calloc(1, sizeof(*gpu));
    if (!gpu) return NULL;
    gpu->renderer = renderer;
    gpu->context = SDL_GL_GetCurrentContext();
    if (!hd_load_gl(&gpu->gl)) {
        free(gpu);
        return NULL;
    }

    EcHdGlState st;
    hd_state_begin(&gpu->gl, &st);

    gpu->plane_prog = hd_link_prog(&gpu->gl, s_plane_vs, s_plane_fs);
    gpu->spr_prog   = hd_link_prog(&gpu->gl, s_spr_vs, s_spr_fs);
    if (gpu->plane_prog && gpu->spr_prog) {
        gpu->gl.GenVertexArrays(1, &gpu->vao);

        static const uint8_t k_zero[4] = {0, 0, 0, 0};
        gpu->tex_dummy = hd_upload_rgba_tex(&gpu->gl, k_zero, 1, 1);

        gpu->pl_u_base     = gpu->gl.GetUniformLocation(gpu->plane_prog, "u_base");
        gpu->pl_u_map      = gpu->gl.GetUniformLocation(gpu->plane_prog, "u_map");
        gpu->pl_u_plane_b  = gpu->gl.GetUniformLocation(gpu->plane_prog, "u_plane_b");
        gpu->pl_u_plane_a  = gpu->gl.GetUniformLocation(gpu->plane_prog, "u_plane_a");
        gpu->pl_u_base_4x  = gpu->gl.GetUniformLocation(gpu->plane_prog, "u_base_4x");
        gpu->pl_u_base_uv  = gpu->gl.GetUniformLocation(gpu->plane_prog, "u_base_uv");
        gpu->pl_u_out_size = gpu->gl.GetUniformLocation(gpu->plane_prog, "u_out_size");

        gpu->sp_u_rect     = gpu->gl.GetUniformLocation(gpu->spr_prog, "u_rect");
        gpu->sp_u_out_size = gpu->gl.GetUniformLocation(gpu->spr_prog, "u_out_size");
        gpu->sp_u_spr_size = gpu->gl.GetUniformLocation(gpu->spr_prog, "u_spr_size");
        gpu->sp_u_hflip    = gpu->gl.GetUniformLocation(gpu->spr_prog, "u_hflip");
        gpu->sp_u_tex      = gpu->gl.GetUniformLocation(gpu->spr_prog, "u_tex");
        gpu->sp_u_map      = gpu->gl.GetUniformLocation(gpu->spr_prog, "u_map");
        gpu->sp_u_link     = gpu->gl.GetUniformLocation(gpu->spr_prog, "u_link");
    }

    hd_state_end(&gpu->gl, &st);

    if (!gpu->plane_prog || !gpu->spr_prog) {
        free(gpu);
        return NULL;
    }
    fprintf(stderr, "[EC_HD] OpenGL Mode C 4x compositor pronto\n");
    return gpu;
}

static int hd_gpu_ensure_size(EcHdGpu *gpu, int w, int h)
{
    int out_w = w * EC_HD_SCALE;
    int out_h = h * EC_HD_SCALE;
    if (gpu->output && gpu->in_w == w && gpu->in_h == h)
        return 1;

    SDL_Texture *next = SDL_CreateTexture(
        gpu->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, out_w, out_h);
    if (!next)
        return 0;
    SDL_SetTextureBlendMode(next, SDL_BLENDMODE_NONE);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(next, SDL_ScaleModeNearest);
#endif
    if (gpu->output)
        SDL_DestroyTexture(gpu->output);
    gpu->output = next;

    if (gpu->tex_map) gpu->gl.DeleteTextures(1, &gpu->tex_map);
    if (gpu->tex_raw) gpu->gl.DeleteTextures(1, &gpu->tex_raw);

    gpu->gl.GenTextures(1, &gpu->tex_map);
    gpu->gl.ActiveTexture(GL_TEXTURE1);
    gpu->gl.BindTexture(GL_TEXTURE_2D, gpu->tex_map);
    gpu->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gpu->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gpu->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gpu->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gpu->gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16UI, w, h, 0, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, NULL);

    gpu->gl.GenTextures(1, &gpu->tex_raw);
    gpu->gl.ActiveTexture(GL_TEXTURE0);
    gpu->gl.BindTexture(GL_TEXTURE_2D, gpu->tex_raw);
    gpu->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gpu->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gpu->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gpu->gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gpu->gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);

    gpu->in_w = w;
    gpu->in_h = h;
    gpu->out_w = out_w;
    gpu->out_h = out_h;
    return 1;
}

static GLuint ensure_plane_gl_tex(EcHdGpu *gpu, EcHdPlaneAsset *pa)
{
    if (!pa || !pa->loaded)
        return gpu->tex_dummy;
    if (!pa->gl_tex && pa->hd_rgba) {
        pa->gl_tex = hd_upload_rgba_tex(&gpu->gl, pa->hd_rgba, pa->hd_w, pa->hd_h);
        free(pa->hd_rgba);
        pa->hd_rgba = NULL;
    }
    return pa->gl_tex ? pa->gl_tex : gpu->tex_dummy;
}

static GLuint ensure_sprite_gl_tex(EcHdGpu *gpu, EcHdSpriteAsset *sa)
{
    if (!sa || !sa->loaded)
        return 0;
    if (!sa->gl_tex && sa->hd_rgba) {
        sa->gl_tex = hd_upload_rgba_tex(&gpu->gl, sa->hd_rgba, sa->hd_w, sa->hd_h);
        free(sa->hd_rgba);
        sa->hd_rgba = NULL;
    }
    return sa->gl_tex;
}

static int save_argb_png(const char *path, const uint32_t *argb, int w, int h)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return -1; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_write_struct(&png, NULL); fclose(fp); return -1; }
    uint8_t *row_rgb = (uint8_t *)malloc((size_t)w * 3u);
    if (!row_rgb || setjmp(png_jmpbuf(png))) {
        free(row_rgb);
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return -1;
    }
    png_init_io(png, fp);
    png_set_IHDR(png, info, (png_uint_32)w, (png_uint_32)h, 8,
                 PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < h; ++y) {
        const uint32_t *src = argb + (size_t)y * (size_t)w;
        for (int x = 0; x < w; ++x) {
            uint32_t px = src[x];
            row_rgb[x * 3 + 0] = (uint8_t)((px >> 16) & 0xFFu);
            row_rgb[x * 3 + 1] = (uint8_t)((px >> 8)  & 0xFFu);
            row_rgb[x * 3 + 2] = (uint8_t)(px & 0xFFu);
        }
        png_write_row(png, row_rgb);
    }
    png_write_end(png, NULL);
    free(row_rgb);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return 0;
}

static void maybe_capture_hd(EcHdGpu *gpu)
{
    const char *cap = getenv("EC_HD_CAPTURE");
    if (!cap || !*cap)
        return;
    uint64_t target = 1u;
    const char *at = getenv("EC_HD_CAPTURE_AT");
    if (at && *at) {
        char *end = NULL;
        unsigned long long v = strtoull(at, &end, 10);
        if (!end || *end) return;
        target = (uint64_t)v;
    }
    if (s_stat_frames != target)
        return;

    uint32_t *buf = (uint32_t *)malloc((size_t)gpu->out_w * (size_t)gpu->out_h * sizeof(uint32_t));
    if (!buf)
        return;
    SDL_Texture *old = SDL_GetRenderTarget(gpu->renderer);
    if (SDL_SetRenderTarget(gpu->renderer, gpu->output) == 0 &&
        SDL_RenderReadPixels(gpu->renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                             buf, gpu->out_w * (int)sizeof(uint32_t)) == 0) {
        if (save_argb_png(cap, buf, gpu->out_w, gpu->out_h) == 0) {
            fprintf(stderr, "[EC_HD_CAPTURE] frame=%llu wrote %s (%dx%d)\n",
                    (unsigned long long)s_stat_frames, cap, gpu->out_w, gpu->out_h);
        }
    }
    SDL_SetRenderTarget(gpu->renderer, old);
    free(buf);
}

#endif /* EC_HD_HAS_GL */

struct SDL_Texture *ec_hd_composite_gpu(struct SDL_Renderer *renderer,
                                        struct SDL_Texture *activation,
                                        struct SDL_Texture *base_tex,
                                        const uint32_t *argb,
                                        int width, int height, int stride_px,
                                        const GVDP *fallback_vdp,
                                        uint16_t state_id)
{
#if EC_HD_HAS_GL
    if (!ec_hd_is_active() || !renderer || !activation || !argb ||
        width <= 0 || height <= 0 || stride_px < width)
        return base_tex;

    /* If scanline_lazy didn't run (e.g. 4:3 mode or non-arena screen S7/S9), evaluate now */
    if (!s_frame_armed && fallback_vdp)
        evaluate_4x3_frame(fallback_vdp, state_id, width, height);

    int armed = s_frame_armed;
    s_frame_armed = 0; /* consume for next frame */

    if (!armed || s_frame_w != width || s_frame_h != height)
        return base_tex;

    /* Fast skip when this frame has zero matched HD planes AND zero matched HD sprites */
    if (!s_plane_has_match[0] && !s_plane_has_match[1] && s_frame_sprite_count == 0) {
        s_stat_frames++;
        return base_tex;
    }

    if (!s_gpu_checked) {
        s_gpu_checked = 1;
        s_gpu = hd_gpu_create(renderer, activation);
    }
    if (!s_gpu || s_gpu->failed)
        return base_tex;
    if (SDL_GL_GetCurrentContext() != s_gpu->context) {
        s_gpu->failed = 1;
        return base_tex;
    }

    SDL_Texture *old_target = SDL_GetRenderTarget(renderer);
    EcHdGlState st;
    hd_state_begin(&s_gpu->gl, &st);

    if (!hd_gpu_ensure_size(s_gpu, width, height)) {
        hd_state_end(&s_gpu->gl, &st);
        return base_tex;
    }

    /* Upload pixel lookup map (RGBA16UI, stride = GVDP_MAX_WIDTH pixels) */
    s_gpu->gl.ActiveTexture(GL_TEXTURE1);
    s_gpu->gl.BindTexture(GL_TEXTURE_2D, s_gpu->tex_map);
    s_gpu->gl.PixelStorei(GL_UNPACK_ROW_LENGTH, GVDP_MAX_WIDTH);
    s_gpu->gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                            GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, &s_pixel_map[0][0][0]);
    s_gpu->gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    GLuint tex_pb = s_gpu->tex_dummy;
    GLuint tex_pa = s_gpu->tex_dummy;
    if (s_frame_state < EC_HD_MAX_STATES) {
        if (s_plane_has_match[0])
            tex_pb = ensure_plane_gl_tex(s_gpu, &s_planes[s_frame_state][0]);
        if (s_plane_has_match[1])
            tex_pa = ensure_plane_gl_tex(s_gpu, &s_planes[s_frame_state][1]);
    }

    /* Upload sprite textures lazily before binding FBO */
    GLuint spr_texes[80];
    for (int i = 0; i < s_frame_sprite_count; ++i)
        spr_texes[i] = ensure_sprite_gl_tex(s_gpu, &s_sprites[s_frame_sprites[i].asset_idx]);

    /* Upload raw native frame if no B+ base_tex is provided */
    if (!base_tex) {
        s_gpu->gl.ActiveTexture(GL_TEXTURE0);
        s_gpu->gl.BindTexture(GL_TEXTURE_2D, s_gpu->tex_raw);
        s_gpu->gl.PixelStorei(GL_UNPACK_ROW_LENGTH, stride_px);
        s_gpu->gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                                GL_BGRA, GL_UNSIGNED_BYTE, argb);
        s_gpu->gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }

    hd_state_end(&s_gpu->gl, &st);

    if (SDL_SetRenderTarget(renderer, s_gpu->output) != 0)
        return base_tex;
    if (SDL_RenderFlush(renderer) != 0) {
        SDL_SetRenderTarget(renderer, old_target);
        return base_tex;
    }

    hd_state_begin(&s_gpu->gl, &st);

    /* Pass 1: Base (B+ 4x or native 4x) + HD Plane A / Plane B replacement */
    float bw = 1.0f, bh = 1.0f;
    int bound_sdl_base = 0;
    s_gpu->gl.ActiveTexture(GL_TEXTURE0);
    if (base_tex && SDL_GL_BindTexture(base_tex, &bw, &bh) == 0) {
        bound_sdl_base = 1;
    } else {
        s_gpu->gl.BindTexture(GL_TEXTURE_2D, s_gpu->tex_raw);
    }
    s_gpu->gl.ActiveTexture(GL_TEXTURE1);
    s_gpu->gl.BindTexture(GL_TEXTURE_2D, s_gpu->tex_map);
    s_gpu->gl.ActiveTexture(GL_TEXTURE2);
    s_gpu->gl.BindTexture(GL_TEXTURE_2D, tex_pb);
    s_gpu->gl.ActiveTexture(GL_TEXTURE3);
    s_gpu->gl.BindTexture(GL_TEXTURE_2D, tex_pa);

    s_gpu->gl.UseProgram(s_gpu->plane_prog);
    s_gpu->gl.BindVertexArray(s_gpu->vao);
    s_gpu->gl.Uniform1i(s_gpu->pl_u_base, 0);
    s_gpu->gl.Uniform1i(s_gpu->pl_u_map, 1);
    s_gpu->gl.Uniform1i(s_gpu->pl_u_plane_b, 2);
    s_gpu->gl.Uniform1i(s_gpu->pl_u_plane_a, 3);
    s_gpu->gl.Uniform1i(s_gpu->pl_u_base_4x, bound_sdl_base ? 1 : 0);
    s_gpu->gl.Uniform2f(s_gpu->pl_u_base_uv, bw, bh);
    s_gpu->gl.Uniform2i(s_gpu->pl_u_out_size, s_gpu->out_w, s_gpu->out_h);
    s_gpu->gl.Viewport(0, 0, s_gpu->out_w, s_gpu->out_h);
    s_gpu->gl.DrawArrays(GL_TRIANGLES, 0, 3);

    if (bound_sdl_base)
        SDL_GL_UnbindTexture(base_tex);

    /* Pass 2: Matched SAT HD Sprites (with exact VDP owner occlusion + 4x smooth alpha) */
    if (s_frame_sprite_count > 0) {
        s_gpu->gl.Enable(GL_BLEND);
        s_gpu->gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        s_gpu->gl.UseProgram(s_gpu->spr_prog);
        s_gpu->gl.ActiveTexture(GL_TEXTURE1);
        s_gpu->gl.BindTexture(GL_TEXTURE_2D, s_gpu->tex_map);
        s_gpu->gl.Uniform1i(s_gpu->sp_u_tex, 0);
        s_gpu->gl.Uniform1i(s_gpu->sp_u_map, 1);
        s_gpu->gl.Uniform2f(s_gpu->sp_u_out_size, (float)s_gpu->out_w, (float)s_gpu->out_h);

        for (int i = 0; i < s_frame_sprite_count; ++i) {
            if (!spr_texes[i])
                continue;
            const EcHdFrameSprite *fs = &s_frame_sprites[i];
            int rw = fs->spr_w * EC_HD_SCALE;
            int rh = fs->spr_h * EC_HD_SCALE;
            s_gpu->gl.ActiveTexture(GL_TEXTURE0);
            s_gpu->gl.BindTexture(GL_TEXTURE_2D, spr_texes[i]);
            s_gpu->gl.Uniform4f(s_gpu->sp_u_rect,
                                (float)(fs->ox0 * EC_HD_SCALE),
                                (float)(fs->sy0 * EC_HD_SCALE),
                                (float)rw, (float)rh);
            s_gpu->gl.Uniform2i(s_gpu->sp_u_spr_size, rw, rh);
            s_gpu->gl.Uniform1i(s_gpu->sp_u_hflip, fs->hflip_draw);
            s_gpu->gl.Uniform1i(s_gpu->sp_u_link, fs->link);
            s_gpu->gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
        s_gpu->gl.Disable(GL_BLEND);
    }

    GLenum err = s_gpu->gl.GetError();
    hd_state_end(&s_gpu->gl, &st);
    if (SDL_SetRenderTarget(renderer, old_target) != 0)
        err = GL_INVALID_OPERATION;

    if (err != GL_NO_ERROR) {
        fprintf(stderr, "[EC_HD] GL error=0x%x; mantendo fallback\n", (unsigned)err);
        s_gpu->failed = 1;
        return base_tex;
    }

    s_stat_frames++;
    maybe_capture_hd(s_gpu);
    return s_gpu->output;
#else
    (void)renderer; (void)activation; (void)argb; (void)width;
    (void)height; (void)stride_px; (void)fallback_vdp; (void)state_id;
    return base_tex;
#endif
}

void ec_hd_destroy_gpu(void)
{
#if EC_HD_HAS_GL
    if (s_gpu) {
        int has_ctx = (s_gpu->gl.GetError && SDL_GL_GetCurrentContext() == s_gpu->context);
        if (has_ctx) {
            for (int i = 0; i < s_sprite_count; ++i) {
                if (s_sprites[i].gl_tex) {
                    s_gpu->gl.DeleteTextures(1, &s_sprites[i].gl_tex);
                    s_sprites[i].gl_tex = 0;
                }
            }
            for (int st = 0; st < EC_HD_MAX_STATES; ++st) {
                for (int pi = 0; pi < 2; ++pi) {
                    if (s_planes[st][pi].gl_tex) {
                        s_gpu->gl.DeleteTextures(1, &s_planes[st][pi].gl_tex);
                        s_planes[st][pi].gl_tex = 0;
                    }
                }
            }
            if (s_gpu->tex_map) s_gpu->gl.DeleteTextures(1, &s_gpu->tex_map);
            if (s_gpu->tex_raw) s_gpu->gl.DeleteTextures(1, &s_gpu->tex_raw);
            if (s_gpu->tex_dummy) s_gpu->gl.DeleteTextures(1, &s_gpu->tex_dummy);
            if (s_gpu->vao) s_gpu->gl.DeleteVertexArrays(1, &s_gpu->vao);
            if (s_gpu->plane_prog) s_gpu->gl.DeleteProgram(s_gpu->plane_prog);
            if (s_gpu->spr_prog) s_gpu->gl.DeleteProgram(s_gpu->spr_prog);
        }
        if (s_gpu->output)
            SDL_DestroyTexture(s_gpu->output);
        if (s_stat_frames > 0) {
            double plane_pct = s_stat_total_px
                ? (100.0 * (double)s_stat_plane_px / (double)s_stat_total_px)
                : 0.0;
            double spr_pct = s_stat_spr_total
                ? (100.0 * (double)s_stat_spr_matched / (double)s_stat_spr_total)
                : 0.0;
            fprintf(stderr,
                    "[EC_HD] frames=%llu | plane_hd=%.1f%% px | sprite_hd=%llu/%llu (%.1f%%)\n",
                    (unsigned long long)s_stat_frames, plane_pct,
                    (unsigned long long)s_stat_spr_matched,
                    (unsigned long long)s_stat_spr_total, spr_pct);
        }
        free(s_gpu);
        s_gpu = NULL;
    }
    s_gpu_checked = 0;
#endif
    for (int i = 0; i < s_sprite_count; ++i) {
        free(s_sprites[i].hd_rgba);
        s_sprites[i].hd_rgba = NULL;
        s_sprites[i].loaded = 0;
        s_sprites[i].tried_load = 0;
    }
    for (int st = 0; st < EC_HD_MAX_STATES; ++st) {
        for (int pi = 0; pi < 2; ++pi) {
            free(s_planes[st][pi].hd_rgba);
            s_planes[st][pi].hd_rgba = NULL;
            s_planes[st][pi].loaded = 0;
            s_planes[st][pi].tried_load = 0;
        }
    }
    s_stat_frames = 0;
    s_stat_plane_px = 0;
    s_stat_total_px = 0;
    s_stat_spr_matched = 0;
    s_stat_spr_total = 0;
}
