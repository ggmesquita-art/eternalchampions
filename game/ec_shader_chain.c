/*
 * ec_shader_chain.c — cadeia multi-pass de shaders RetroArch (.glslp + .glsl legado).
 *
 * Host-only: nunca altera estado do jogo. Aplicada no fim de ec_video_present_gpu.
 *
 * Convencoes (iguais as do RetroArch GLSL):
 *   - Texture/TextureSize/InputSize = entrada do passe (passe 0: saida B+/Modo C ou o frame nativo).
 *   - OrigTexture* = frame nativo. PassPrevK* = saida do passe (idx-K); PassPrev{idx+1} = Original.
 *   - Pass{K}Texture* = saida do passe K (1-based: Pass1 = shader0). <alias>Texture* = saida do passe aliased.
 *   - Layout: toda textura tem a linha 0 (topo da imagem) em t=0 e em y=0 do FBO; nenhum flip.
 */

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "ec_shader_chain.h"

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)

#include <ctype.h>
#include <math.h>
#include <png.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#ifndef _MSC_VER
#include <strings.h>
#define _stricmp strcasecmp
#endif

#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif

#define SC_MAX_PASSES   32
#define SC_MAX_PARAMS   512
#define SC_MAX_LUTS     16
#define SC_MAX_OVERRIDE 128
#define SC_PATH         1024
#define SC_RING         4
#define SC_SAMPLES      32768
#define SC_MAX_UNITS    16

enum { SC_SCALE_SOURCE = 0, SC_SCALE_VIEWPORT = 1, SC_SCALE_ABSOLUTE = 2 };
enum { SC_WRAP_BORDER = 0, SC_WRAP_EDGE = 1, SC_WRAP_REPEAT = 2, SC_WRAP_MIRROR = 3 };
enum { SC_B_SRC, SC_B_ORIG, SC_B_PREV, SC_B_PASS, SC_B_ALIAS, SC_B_HIST, SC_B_LUT };

/* ------------------------------------------------------------------------- */
/* Tipos                                                                      */
/* ------------------------------------------------------------------------- */

typedef struct { char name[64]; char desc[96]; float def, minv, maxv, step, value; } ScParam;
typedef struct { char name[64]; char path[SC_PATH]; int linear, mipmap, wrap; } ScLut;
typedef struct { char name[64]; float value; int prio; } ScOverride;

typedef struct { GLint tex, size, input; } ScTexU;
typedef struct { int kind, idx; ScTexU u; } ScBind;

typedef struct {
    char   path[SC_PATH];
    char   alias[64];
    char  *body;            /* fonte ja lido (com #include expandido) */
    int    version;         /* 0 = nao declarado */
    int    linear, wrap, mipmap, float_fb, srgb_fb, frame_count_mod;
    int    type_x, type_y;  /* -1 = nao informado */
    float  scale_x, scale_y;
    int    has_scale_x, has_scale_y;
    /* GL */
    GLuint prog;
    GLint  a_vertex, a_tex, a_color;
    GLint  u_mvp, u_frame_dir, u_frame_count, u_output_size, u_final_vp;
    GLint *param_loc;
    ScBind *binds;
    int    nbinds;
    /* saida */
    GLuint out_tex, out_fbo;
    int    out_w, out_h;
    int    out_fmt;         /* 0 rgba8, 1 rgba16f, 2 srgb8_a8 */
} ScPass;

typedef struct {
    char name[128];
    char dir[SC_PATH];
    char file[SC_PATH];
    int  npass;
    ScPass pass[SC_MAX_PASSES];
    int  nlut;
    ScLut lut[SC_MAX_LUTS];
    GLuint lut_tex[SC_MAX_LUTS];
    int  lut_w[SC_MAX_LUTS], lut_h[SC_MAX_LUTS];
    int  nparam;
    ScParam param[SC_MAX_PARAMS];
    /* extensoes ec_* */
    int    bplus_enable;                 /* -1 ausente */
    char   bplus_model[SC_PATH];
    char   bplus_dedither[16];
    double bplus_intensity;              /* <0 ausente */
    int    has_bplus;
} ScPreset;

typedef struct {
    GLuint id;
    SDL_Texture *sdl;
    int tw, th, iw, ih;
} ScTexDesc;

/* ------------------------------------------------------------------------- */
/* GL                                                                         */
/* ------------------------------------------------------------------------- */

#define SC_GL_API(X) \
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
    X(void, GenerateMipmap, (GLenum)) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void, DeleteShader, (GLuint)) \
    X(GLuint, CreateProgram, (void)) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void, UseProgram, (GLuint)) \
    X(void, DeleteProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*)) \
    X(GLint, GetAttribLocation, (GLuint, const GLchar*)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*)) \
    X(void, GenVertexArrays, (GLsizei, GLuint*)) \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, GenBuffers, (GLsizei, GLuint*)) \
    X(void, DeleteBuffers, (GLsizei, const GLuint*)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, DisableVertexAttribArray, (GLuint)) \
    X(void, GenFramebuffers, (GLsizei, GLuint*)) \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, GenSamplers, (GLsizei, GLuint*)) \
    X(void, DeleteSamplers, (GLsizei, const GLuint*)) \
    X(void, BindSampler, (GLuint, GLuint)) \
    X(void, SamplerParameteri, (GLuint, GLenum, GLint)) \
    X(void, SamplerParameterfv, (GLuint, GLenum, const GLfloat*))

typedef struct {
#define SC_DECL(ret, name, args) ret (APIENTRY *name) args;
    SC_GL_API(SC_DECL)
#undef SC_DECL
    /* opcionais (timer query) */
    void (APIENTRY *GenQueries)(GLsizei, GLuint*);
    void (APIENTRY *DeleteQueries)(GLsizei, const GLuint*);
    void (APIENTRY *BeginQuery)(GLenum, GLuint);
    void (APIENTRY *EndQuery)(GLenum);
    void (APIENTRY *GetQueryObjectiv)(GLuint, GLenum, GLint*);
    void (APIENTRY *GetQueryObjectui64v)(GLuint, GLenum, GLuint64*);
} ScGl;

static const GLenum k_sc_switches[8] = {
    GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST,
    GL_CULL_FACE, GL_DITHER, GL_FRAMEBUFFER_SRGB, GL_RASTERIZER_DISCARD
};
static const GLenum k_sc_stores[6] = {
    GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_PIXELS,
    GL_UNPACK_SKIP_ROWS, GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST
};

typedef struct {
    GLint     program, active_tex, vao, unpack_buf, array_buf, fbo_draw, fbo_read;
    GLint     blend_src, blend_dst;
    GLint     viewport[4], scissor[4];
    GLint     tex2d[4], unpack[6];
    GLboolean mask[4], enabled[8], tex2d_enabled;
} ScGlState;

/* ------------------------------------------------------------------------- */
/* Estado global                                                              */
/* ------------------------------------------------------------------------- */

typedef struct { char key[96]; char val[SC_PATH]; int used; } ScKv;

static char       s_cli_preset[SC_PATH];
static int        s_cli_preset_set;
static char       s_set_preset[SC_PATH];
static char       s_set_params[2048];
static char       s_cli_params[2048];
static int        s_resolved;
static int        s_active;
static int        s_failed;
static ScPreset  *s_pre;
static ScOverride s_ov[SC_MAX_OVERRIDE];
static int        s_nov;

static ScGl          s_gl;
static int           s_gl_ready;
static SDL_GLContext s_ctx;
static SDL_Renderer *s_renderer;
static GLuint        s_vao, s_vbo;
static GLuint        s_samplers[16];
static GLuint        s_orig_tex;
static int           s_orig_w, s_orig_h;
static SDL_Texture  *s_output;
static int           s_out_w, s_out_h;
static uint32_t      s_frames;
static int           s_time_ok;
static int           s_q_created;
static GLuint        s_queries[SC_RING][SC_MAX_PASSES];
static int           s_q_pending[SC_RING];
static float        *s_samples[SC_MAX_PASSES];
static float        *s_total_samples;
static uint32_t      s_nsamples;
static int           s_vp_w, s_vp_h;
static int           s_warned_hist;

/* ------------------------------------------------------------------------- */
/* Utilitarios de texto                                                       */
/* ------------------------------------------------------------------------- */

static char *sc_trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) ++s;
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    return s;
}

static void sc_copy(char *dst, size_t cap, const char *src)
{
    if (!cap) return;
    snprintf(dst, cap, "%s", src ? src : "");
}

static int sc_file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    fclose(fp);
    return 1;
}

static int sc_is_abs(const char *p)
{
    return p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':');
}

static void sc_join(const char *dir, const char *rel, char *out, size_t cap)
{
    if (sc_is_abs(rel) || !dir || !*dir) snprintf(out, cap, "%s", rel);
    else snprintf(out, cap, "%s/%s", dir, rel);
}

static void sc_dirname(const char *path, char *out, size_t cap)
{
    sc_copy(out, cap, path);
    char *s1 = strrchr(out, '/');
    char *s2 = strrchr(out, '\\');
    char *s = (s1 > s2) ? s1 : s2;
    if (s) *s = '\0'; else out[0] = '\0';
}

static char *sc_read_file(const char *path, size_t *out_n)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n < 0 || n > 32 * 1024 * 1024) { fclose(fp); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1u);
    if (!buf) { fclose(fp); return NULL; }
    size_t rd = fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    buf[rd] = '\0';
    if (out_n) *out_n = rd;
    return buf;
}

static void sc_warn_once(const char *fmt, ...)
{
    static char seen[128][160];
    static int nseen;
    char msg[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    for (int i = 0; i < nseen; ++i)
        if (strcmp(seen[i], msg) == 0) return;
    if (nseen < 128) sc_copy(seen[nseen++], sizeof(seen[0]), msg);
    fprintf(stderr, "[SHADER] aviso: %s\n", msg);
}

static int sc_parse_bool(const char *v)
{
    return (!_stricmp(v, "true") || !strcmp(v, "1") || !_stricmp(v, "yes") || !_stricmp(v, "on"));
}

static int sc_parse_float(const char *v, float *out)
{
    char *end = NULL;
    double d = strtod(v, &end);
    if (end == v || *end != '\0' || !isfinite(d)) return 0;
    *out = (float)d;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Configuracao                                                               */
/* ------------------------------------------------------------------------- */

void ec_shader_chain_set_preset_cli(const char *preset)
{
    if (!preset || !*preset) return;
    sc_copy(s_cli_preset, sizeof(s_cli_preset), preset);
    s_cli_preset_set = 1;
    s_resolved = 0;
}

void ec_shader_chain_add_param_cli(const char *kv)
{
    if (!kv || !*kv) return;
    size_t n = strlen(s_cli_params);
    if (n + strlen(kv) + 2 >= sizeof(s_cli_params)) {
        fprintf(stderr, "[SHADER] aviso: --shader-param '%s' ignorado (buffer de parametros cheio)\n", kv);
        return;
    }
    snprintf(s_cli_params + n, sizeof(s_cli_params) - n, "%s%s", n ? "," : "", kv);
    s_resolved = 0;
}

void ec_shader_chain_set_settings(const char *preset, const char *params)
{
    if (preset) sc_copy(s_set_preset, sizeof(s_set_preset), preset);
    if (params) sc_copy(s_set_params, sizeof(s_set_params), params);
    s_resolved = 0;
}

static void sc_add_overrides(const char *list, int prio)
{
    if (!list || !*list) return;
    char buf[2048];
    sc_copy(buf, sizeof(buf), list);
    char *tok = buf;
    while (tok && *tok) {
        char *next = strchr(tok, ',');
        if (next) *next++ = '\0';
        char *eq = strchr(tok, '=');
        if (eq && s_nov >= SC_MAX_OVERRIDE) {
            sc_warn_once("limite de %d overrides de parametros excedido; extras ignorados", SC_MAX_OVERRIDE);
        } else if (eq) {
            *eq = '\0';
            char *k = sc_trim(tok);
            char *v = sc_trim(eq + 1);
            float f;
            if (*k && sc_parse_float(v, &f)) {
                sc_copy(s_ov[s_nov].name, sizeof(s_ov[s_nov].name), k);
                s_ov[s_nov].value = f;
                s_ov[s_nov].prio = prio;
                ++s_nov;
            } else if (*k) {
                fprintf(stderr, "[SHADER] aviso: parametro invalido '%s=%s'\n", k, v);
            }
        }
        tok = next;
    }
}

/* ------------------------------------------------------------------------- */
/* Parse de .glsl / .glslp (somente texto)                                    */
/* ------------------------------------------------------------------------- */

static char *sc_load_glsl(const char *path, int depth)
{
    size_t n = 0;
    char *src = sc_read_file(path, &n);
    if (!src) return NULL;
    if (depth > 4) return src;
    char dir[SC_PATH];
    sc_dirname(path, dir, sizeof(dir));
    size_t cap = n + 1024, len = 0;
    char *out = (char *)malloc(cap);
    if (!out) { free(src); return NULL; }
    out[0] = '\0';
    char *p = src;
    while (*p) {
        char *eol = strchr(p, '\n');
        size_t ll = eol ? (size_t)(eol - p) + 1u : strlen(p);
        const char *q = p;
        while (q < p + ll && (*q == ' ' || *q == '\t')) ++q;
        char *inc = NULL;
        if (strncmp(q, "#include", 8) == 0) {
            const char *a = strchr(q, '"');
            const char *b = a ? strchr(a + 1, '"') : NULL;
            if (a && b) {
                char rel[SC_PATH], full[SC_PATH];
                size_t rl = (size_t)(b - a - 1);
                if (rl >= sizeof(rel)) rl = sizeof(rel) - 1;
                memcpy(rel, a + 1, rl);
                rel[rl] = '\0';
                sc_join(dir, rel, full, sizeof(full));
                inc = sc_load_glsl(full, depth + 1);
                if (!inc) fprintf(stderr, "[SHADER] aviso: #include nao encontrado: %s\n", full);
            }
        }
        const char *chunk = inc ? inc : p;
        size_t cl = inc ? strlen(inc) : ll;
        if (len + cl + 2 > cap) {
            cap = (len + cl + 2) * 2;
            char *nb = (char *)realloc(out, cap);
            if (!nb) { free(out); free(src); free(inc); return NULL; }
            out = nb;
        }
        memcpy(out + len, chunk, cl);
        len += cl;
        if (inc) { out[len++] = '\n'; }
        out[len] = '\0';
        free(inc);
        p += ll;
    }
    free(src);
    return out;
}

/* Remove a linha #version (guardando o numero) e coleta #pragma parameter. */
static void sc_scan_glsl(ScPreset *pre, ScPass *ps)
{
    char *p = ps->body;
    while (p && *p) {
        char *eol = strchr(p, '\n');
        char *q = p;
        while (*q == ' ' || *q == '\t') ++q;
        if (strncmp(q, "#version", 8) == 0 && (q[8] == ' ' || q[8] == '\t')) {
            int ver = atoi(q + 8);
            int es = strstr(q, " es") != NULL && (!eol || strstr(q, " es") < eol);
            if (ps->version == 0 && ver > 0 && !es) ps->version = ver;
            q[0] = '/'; q[1] = '/';  /* comenta a linha original */
        } else if (strncmp(q, "#pragma parameter", 17) == 0) {
            char line[512];
            size_t ll = eol ? (size_t)(eol - q) : strlen(q);
            if (ll >= sizeof(line)) ll = sizeof(line) - 1;
            memcpy(line, q, ll);
            line[ll] = '\0';
            char name[64] = {0}, desc[96] = {0};
            const char *s = line + 17;
            while (*s == ' ' || *s == '\t') ++s;
            int ni = 0;
            while (*s && !isspace((unsigned char)*s) && ni < 63) name[ni++] = *s++;
            while (*s == ' ' || *s == '\t') ++s;
            if (*s == '"') {
                ++s;
                int di = 0;
                while (*s && *s != '"' && di < 95) desc[di++] = *s++;
                if (*s == '"') ++s;
            }
            float v[4] = {0, 0, 0, 0};
            int nf = sscanf(s, "%f %f %f %f", &v[0], &v[1], &v[2], &v[3]);
            if (name[0] && nf >= 3) {
                int dup = 0;
                for (int i = 0; i < pre->nparam; ++i)
                    if (strcmp(pre->param[i].name, name) == 0) { dup = 1; break; }
                if (!dup && pre->nparam < SC_MAX_PARAMS) {
                    ScParam *pp = &pre->param[pre->nparam++];
                    sc_copy(pp->name, sizeof(pp->name), name);
                    sc_copy(pp->desc, sizeof(pp->desc), desc);
                    pp->def = pp->value = v[0];
                    pp->minv = v[1];
                    pp->maxv = v[2];
                    pp->step = nf >= 4 ? v[3] : 0.0f;
                }
            }
        }
        p = eol ? eol + 1 : NULL;
    }
}

static ScKv *sc_kv_find(ScKv *kv, int n, const char *key)
{
    for (int i = 0; i < n; ++i)
        if (strcmp(kv[i].key, key) == 0) { kv[i].used = 1; return &kv[i]; }
    return NULL;
}

static const char *sc_kv_get(ScKv *kv, int n, const char *fmt, int idx)
{
    char key[96];
    snprintf(key, sizeof(key), fmt, idx);
    ScKv *e = sc_kv_find(kv, n, key);
    return e ? e->val : NULL;
}

static int sc_parse_wrap(const char *v)
{
    if (!v) return SC_WRAP_BORDER;
    if (!strcmp(v, "clamp_to_edge")) return SC_WRAP_EDGE;
    if (!strcmp(v, "repeat")) return SC_WRAP_REPEAT;
    if (!strcmp(v, "mirrored_repeat")) return SC_WRAP_MIRROR;
    return SC_WRAP_BORDER;
}

static int sc_parse_scale_type(const char *v)
{
    if (!v) return -1;
    if (!strcmp(v, "viewport")) return SC_SCALE_VIEWPORT;
    if (!strcmp(v, "absolute")) return SC_SCALE_ABSOLUTE;
    return SC_SCALE_SOURCE;
}

static void sc_free_preset(ScPreset *pre)
{
    if (!pre) return;
    for (int i = 0; i < pre->npass; ++i) {
        free(pre->pass[i].body);
        free(pre->pass[i].param_loc);
        free(pre->pass[i].binds);
    }
    free(pre);
}

static ScPreset *sc_parse_preset(const char *path)
{
    size_t n = 0;
    char *text = sc_read_file(path, &n);
    if (!text) {
        fprintf(stderr, "[SHADER] preset nao encontrado: %s\n", path);
        return NULL;
    }
    int cap = 1024, nkv = 0;
    ScKv *kv = (ScKv *)calloc((size_t)cap, sizeof(ScKv));
    ScPreset *pre = (ScPreset *)calloc(1, sizeof(ScPreset));
    if (!kv || !pre) { free(text); free(kv); free(pre); return NULL; }

    sc_copy(pre->file, sizeof(pre->file), path);
    sc_dirname(path, pre->dir, sizeof(pre->dir));
    {
        const char *b = path;
        const char *s1 = strrchr(path, '/'), *s2 = strrchr(path, '\\');
        const char *s = (s1 > s2) ? s1 : s2;
        if (s) b = s + 1;
        sc_copy(pre->name, sizeof(pre->name), b);
        char *dot = strrchr(pre->name, '.');
        if (dot) *dot = '\0';
    }
    pre->bplus_enable = -1;
    pre->bplus_intensity = -1.0;

    char *line = text;
    while (line && *line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';
        char *l = sc_trim(line);
        line = eol ? eol + 1 : NULL;
        if (!*l) continue;
        if (strncmp(l, "#reference", 10) == 0) {
            sc_warn_once("#reference nao suportado em %s (preset ignorado parcialmente)", pre->name);
            continue;
        }
        if (*l == '#' || (l[0] == '/' && l[1] == '/')) continue;
        char *eq = strchr(l, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = sc_trim(l);
        char *v = sc_trim(eq + 1);
        if (*v == '"') {
            ++v;
            char *e = strchr(v, '"');
            if (e) *e = '\0';
        } else {
            char *c = strstr(v, "//");
            if (c) *c = '\0';
            c = strstr(v, " #");
            if (c) *c = '\0';
            v = sc_trim(v);
        }
        if (nkv >= cap) {
            cap *= 2;
            ScKv *nk = (ScKv *)realloc(kv, (size_t)cap * sizeof(ScKv));
            if (!nk) break;
            kv = nk;
        }
        sc_copy(kv[nkv].key, sizeof(kv[nkv].key), k);
        sc_copy(kv[nkv].val, sizeof(kv[nkv].val), v);
        kv[nkv].used = 0;
        ++nkv;
    }

    int ok = 1;
    ScKv *e = sc_kv_find(kv, nkv, "shaders");
    int np = e ? atoi(e->val) : 0;
    if (np < 1 || np > SC_MAX_PASSES) {
        fprintf(stderr, "[SHADER] %s: 'shaders' invalido (%d)\n", path, np);
        ok = 0;
    }
    for (int i = 0; ok && i < np; ++i) {
        ScPass *ps = &pre->pass[i];
        const char *sh = sc_kv_get(kv, nkv, "shader%d", i);
        if (!sh || !*sh) {
            fprintf(stderr, "[SHADER] %s: shader%d ausente\n", path, i);
            ok = 0;
            break;
        }
        sc_join(pre->dir, sh, ps->path, sizeof(ps->path));
        ps->body = sc_load_glsl(ps->path, 0);
        if (!ps->body) {
            fprintf(stderr, "[SHADER] %s: nao foi possivel ler %s\n", path, ps->path);
            ok = 0;
            break;
        }
        pre->npass = i + 1;
        const char *s;
        ps->linear = (s = sc_kv_get(kv, nkv, "filter_linear%d", i)) ? sc_parse_bool(s) : 0;
        ps->wrap = sc_parse_wrap(sc_kv_get(kv, nkv, "wrap_mode%d", i));
        ps->mipmap = (s = sc_kv_get(kv, nkv, "mipmap_input%d", i)) ? sc_parse_bool(s) : 0;
        ps->float_fb = (s = sc_kv_get(kv, nkv, "float_framebuffer%d", i)) ? sc_parse_bool(s) : 0;
        ps->srgb_fb = (s = sc_kv_get(kv, nkv, "srgb_framebuffer%d", i)) ? sc_parse_bool(s) : 0;
        ps->frame_count_mod = (s = sc_kv_get(kv, nkv, "frame_count_mod%d", i)) ? atoi(s) : 0;
        if ((s = sc_kv_get(kv, nkv, "alias%d", i))) sc_copy(ps->alias, sizeof(ps->alias), s);
        ps->type_x = ps->type_y = -1;
        ps->scale_x = ps->scale_y = 1.0f;
        if ((s = sc_kv_get(kv, nkv, "scale_type%d", i))) ps->type_x = ps->type_y = sc_parse_scale_type(s);
        if ((s = sc_kv_get(kv, nkv, "scale_type_x%d", i))) ps->type_x = sc_parse_scale_type(s);
        if ((s = sc_kv_get(kv, nkv, "scale_type_y%d", i))) ps->type_y = sc_parse_scale_type(s);
        float f;
        if ((s = sc_kv_get(kv, nkv, "scale%d", i)) && sc_parse_float(s, &f)) {
            ps->scale_x = ps->scale_y = f;
            ps->has_scale_x = ps->has_scale_y = 1;
        }
        if ((s = sc_kv_get(kv, nkv, "scale_x%d", i)) && sc_parse_float(s, &f)) { ps->scale_x = f; ps->has_scale_x = 1; }
        if ((s = sc_kv_get(kv, nkv, "scale_y%d", i)) && sc_parse_float(s, &f)) { ps->scale_y = f; ps->has_scale_y = 1; }
        sc_scan_glsl(pre, ps);
    }

    /* LUTs */
    if (ok && (e = sc_kv_find(kv, nkv, "textures"))) {
        char list[SC_PATH];
        sc_copy(list, sizeof(list), e->val);
        char *tok = list;
        while (tok && *tok && pre->nlut < SC_MAX_LUTS) {
            char *next = strchr(tok, ';');
            if (next) *next++ = '\0';
            char *nm = sc_trim(tok);
            tok = next;
            if (!*nm) continue;
            ScKv *pe = sc_kv_find(kv, nkv, nm);
            if (!pe) { fprintf(stderr, "[SHADER] %s: textura '%s' sem caminho\n", path, nm); continue; }
            ScLut *lt = &pre->lut[pre->nlut++];
            sc_copy(lt->name, sizeof(lt->name), nm);
            sc_join(pre->dir, pe->val, lt->path, sizeof(lt->path));
            char key[128];
            ScKv *x;
            snprintf(key, sizeof(key), "%s_linear", nm);
            lt->linear = (x = sc_kv_find(kv, nkv, key)) ? sc_parse_bool(x->val) : 1;
            snprintf(key, sizeof(key), "%s_wrap_mode", nm);
            lt->wrap = (x = sc_kv_find(kv, nkv, key)) ? sc_parse_wrap(x->val) : SC_WRAP_BORDER;
            snprintf(key, sizeof(key), "%s_mipmap", nm);
            lt->mipmap = (x = sc_kv_find(kv, nkv, key)) ? sc_parse_bool(x->val) : 0;
        }
    }

    /* extensoes ec_* */
    if (ok) {
        if ((e = sc_kv_find(kv, nkv, "ec_bplus"))) {
            pre->has_bplus = 1;
            if (!e->val[0] || !_stricmp(e->val, "0") || !_stricmp(e->val, "off") ||
                !_stricmp(e->val, "false") || !_stricmp(e->val, "no") ||
                !_stricmp(e->val, "none") || !_stricmp(e->val, "disabled")) {
                pre->bplus_enable = 0;
            } else if (!_stricmp(e->val, "1") || !_stricmp(e->val, "on") || !_stricmp(e->val, "true")) {
                pre->bplus_enable = 1;
            } else {
                char cand[SC_PATH];
                sc_join(pre->dir, e->val, cand, sizeof(cand));
                if (sc_file_exists(cand)) {
                    sc_copy(pre->bplus_model, sizeof(pre->bplus_model), cand);
                    pre->bplus_enable = 1;
                } else if (sc_file_exists(e->val)) {
                    sc_copy(pre->bplus_model, sizeof(pre->bplus_model), e->val);
                    pre->bplus_enable = 1;
                } else {
                    fprintf(stderr, "[SHADER] aviso: ec_bplus='%s' nao existe (relativo a %s); "
                            "B+ do preset desligado\n", e->val, pre->dir);
                    pre->bplus_enable = 0;
                }
            }
        }
        if ((e = sc_kv_find(kv, nkv, "ec_bplus_dedither"))) {
            sc_copy(pre->bplus_dedither, sizeof(pre->bplus_dedither), e->val);
            pre->has_bplus = 1;
        }
        if ((e = sc_kv_find(kv, nkv, "ec_bplus_intensity"))) {
            float f;
            if (sc_parse_float(e->val, &f)) { pre->bplus_intensity = f; pre->has_bplus = 1; }
        }
        /* chaves conhecidas e ignoradas */
        sc_kv_find(kv, nkv, "feedback_pass");
        sc_kv_find(kv, nkv, "parameters");
        sc_kv_find(kv, nkv, "name");
    }

    /* demais chaves: overrides de #pragma parameter ou desconhecidas */
    if (ok) {
        for (int i = 0; i < nkv; ++i) {
            if (kv[i].used) continue;
            int pi = -1;
            for (int j = 0; j < pre->nparam; ++j)
                if (strcmp(pre->param[j].name, kv[i].key) == 0) { pi = j; break; }
            float f;
            if (pi >= 0 && sc_parse_float(kv[i].val, &f))
                pre->param[pi].value = f;
            else
                sc_warn_once("chave desconhecida ignorada em %s: %s", pre->name, kv[i].key);
        }
    }

    free(kv);
    free(text);
    if (!ok) { sc_free_preset(pre); return NULL; }
    return pre;
}

/* ------------------------------------------------------------------------- */
/* Resolucao (preset: CLI > env > settings)                                   */
/* ------------------------------------------------------------------------- */

static int sc_try_path(const char *cand, char *out, size_t cap)
{
    if (sc_file_exists(cand)) { sc_copy(out, cap, cand); return 1; }
    return 0;
}

static int sc_find_preset(const char *name, char *out, size_t cap)
{
    if (sc_try_path(name, out, cap)) return 1;
    char *base = SDL_GetBasePath();
    const char *prefixes[4] = { "", "../../", "../", base ? base : "" };
    char exe_up[SC_PATH] = "";
    if (base) snprintf(exe_up, sizeof(exe_up), "%s../../", base);
    const char *roots[] = { "assets/shaders/presets/", "assets/shaders/libretro/" };
    char cand[SC_PATH * 2];
    int ext = (strlen(name) > 6 && !_stricmp(name + strlen(name) - 6, ".glslp"));
    int found = 0;
    for (int r = 0; r < 2 && !found; ++r) {
        for (int p = 0; p < 5 && !found; ++p) {
            const char *pre = (p < 4) ? prefixes[p] : exe_up;
            snprintf(cand, sizeof(cand), "%s%s%s%s", pre, roots[r], name, ext ? "" : ".glslp");
            if (sc_try_path(cand, out, cap)) found = 1;
        }
    }
    if (base) SDL_free(base);
    return found;
}

static void sc_resolve(void)
{
    s_resolved = 1;
    s_active = 0;
    sc_free_preset(s_pre);
    s_pre = NULL;

    const char *chosen = NULL;
    if (s_cli_preset_set) chosen = s_cli_preset;
    else {
        const char *env = getenv("EC_SHADER_PRESET");
        if (env && *env) chosen = env;
        else if (s_set_preset[0]) chosen = s_set_preset;
    }
    if (!chosen || !*chosen || !_stricmp(chosen, "off") || !strcmp(chosen, "0") || !_stricmp(chosen, "none"))
        return;

    char path[SC_PATH];
    if (!sc_find_preset(chosen, path, sizeof(path))) {
        fprintf(stderr, "[SHADER] preset '%s' nao encontrado (cadeia desativada)\n", chosen);
        return;
    }
    ScPreset *pre = sc_parse_preset(path);
    if (!pre) {
        fprintf(stderr, "[SHADER] falha ao ler preset '%s' (cadeia desativada)\n", path);
        return;
    }

    /* overrides: settings(1) < env(2) < CLI(3), o ultimo vence */
    s_nov = 0;
    sc_add_overrides(s_set_params, 1);
    sc_add_overrides(getenv("EC_SHADER_PARAMS"), 2);
    sc_add_overrides(s_cli_params, 3);
    for (int prio = 1; prio <= 3; ++prio) {
        for (int i = 0; i < s_nov; ++i) {
            if (s_ov[i].prio != prio) continue;
            int found = 0;
            for (int j = 0; j < pre->nparam; ++j)
                if (strcmp(pre->param[j].name, s_ov[i].name) == 0) {
                    pre->param[j].value = s_ov[i].value;
                    found = 1;
                    break;
                }
            if (!found) sc_warn_once("parametro '%s' nao existe no preset %s", s_ov[i].name, pre->name);
        }
    }
    s_pre = pre;
    s_active = 1;
    fprintf(stderr, "[SHADER] preset '%s' (%s): %d passe(s), %d parametro(s), %d LUT(s)\n",
            pre->name, pre->file, pre->npass, pre->nparam, pre->nlut);
}

int ec_shader_chain_active(void)
{
    if (!s_resolved) sc_resolve();
    return s_active && !s_failed;
}

int ec_shader_chain_get_bplus(const char **model, const char **dedither,
                              double *intensity, int *enable)
{
    if (!ec_shader_chain_active() || !s_pre || !s_pre->has_bplus) return 0;
    if (model) *model = s_pre->bplus_model[0] ? s_pre->bplus_model : NULL;
    if (dedither) *dedither = s_pre->bplus_dedither[0] ? s_pre->bplus_dedither : NULL;
    if (intensity) *intensity = s_pre->bplus_intensity;
    if (enable) *enable = s_pre->bplus_enable;
    return 1;
}

/* ------------------------------------------------------------------------- */
/* GL: infraestrutura                                                         */
/* ------------------------------------------------------------------------- */

static int sc_load_gl(ScGl *g)
{
#define SC_LOAD(ret, name, args) \
    g->name = (ret (APIENTRY *) args)SDL_GL_GetProcAddress("gl" #name); \
    if (!g->name) { fprintf(stderr, "[SHADER] simbolo GL ausente: gl%s\n", #name); return 0; }
    SC_GL_API(SC_LOAD)
#undef SC_LOAD
    g->GenQueries = (void (APIENTRY *)(GLsizei, GLuint*))SDL_GL_GetProcAddress("glGenQueries");
    g->DeleteQueries = (void (APIENTRY *)(GLsizei, const GLuint*))SDL_GL_GetProcAddress("glDeleteQueries");
    g->BeginQuery = (void (APIENTRY *)(GLenum, GLuint))SDL_GL_GetProcAddress("glBeginQuery");
    g->EndQuery = (void (APIENTRY *)(GLenum))SDL_GL_GetProcAddress("glEndQuery");
    g->GetQueryObjectiv = (void (APIENTRY *)(GLuint, GLenum, GLint*))SDL_GL_GetProcAddress("glGetQueryObjectiv");
    g->GetQueryObjectui64v = (void (APIENTRY *)(GLuint, GLenum, GLuint64*))SDL_GL_GetProcAddress("glGetQueryObjectui64v");
    return 1;
}

static void sc_state_begin(const ScGl *g, ScGlState *s)
{
    g->GetIntegerv(GL_CURRENT_PROGRAM, &s->program);
    g->GetIntegerv(GL_ACTIVE_TEXTURE, &s->active_tex);
    g->GetIntegerv(GL_VERTEX_ARRAY_BINDING, &s->vao);
    g->GetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &s->unpack_buf);
    g->GetIntegerv(GL_ARRAY_BUFFER_BINDING, &s->array_buf);
    g->GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &s->fbo_draw);
    g->GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &s->fbo_read);
    g->GetIntegerv(GL_BLEND_SRC_RGB, &s->blend_src);
    g->GetIntegerv(GL_BLEND_DST_RGB, &s->blend_dst);
    g->GetIntegerv(GL_VIEWPORT, s->viewport);
    g->GetIntegerv(GL_SCISSOR_BOX, s->scissor);
    g->GetBooleanv(GL_COLOR_WRITEMASK, s->mask);
    s->tex2d_enabled = g->IsEnabled(GL_TEXTURE_2D);
    for (int i = 0; i < 8; ++i) {
        s->enabled[i] = g->IsEnabled(k_sc_switches[i]);
        g->Disable(k_sc_switches[i]);
    }
    for (int i = 0; i < 6; ++i)
        g->GetIntegerv(k_sc_stores[i], &s->unpack[i]);
    for (int i = 0; i < 4; ++i) {
        g->ActiveTexture(GL_TEXTURE0 + (GLenum)i);
        g->GetIntegerv(GL_TEXTURE_BINDING_2D, &s->tex2d[i]);
    }
    g->BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    g->PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (int i = 1; i < 6; ++i)
        g->PixelStorei(k_sc_stores[i], 0);
    g->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    (void)g->GetError();
}

static void sc_state_end(const ScGl *g, const ScGlState *s)
{
    for (int u = 0; u < SC_MAX_UNITS; ++u) {
        g->ActiveTexture(GL_TEXTURE0 + (GLenum)u);
        g->BindSampler((GLuint)u, 0);
        if (u >= 4) g->BindTexture(GL_TEXTURE_2D, 0);
    }
    g->BindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)s->fbo_draw);
    g->BindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)s->fbo_read);
    g->BindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)s->unpack_buf);
    g->BindBuffer(GL_ARRAY_BUFFER, (GLuint)s->array_buf);
    g->UseProgram((GLuint)s->program);
    g->BindVertexArray((GLuint)s->vao);
    for (int i = 0; i < 6; ++i)
        g->PixelStorei(k_sc_stores[i], s->unpack[i]);
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
        if (s->enabled[i]) g->Enable(k_sc_switches[i]);
        else g->Disable(k_sc_switches[i]);
    }
    if (s->tex2d_enabled) g->Enable(GL_TEXTURE_2D);
    else g->Disable(GL_TEXTURE_2D);
}

static GLuint sc_compile(const char *label, GLenum type, const char *src)
{
    GLuint sh = s_gl.CreateShader(type);
    if (!sh) return 0;
    s_gl.ShaderSource(sh, 1, &src, NULL);
    s_gl.CompileShader(sh);
    GLint ok = 0;
    s_gl.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        s_gl.GetShaderInfoLog(sh, sizeof(log) - 1, NULL, log);
        fprintf(stderr, "[SHADER] erro de compilacao (%s, %s):\n%s\n", label,
                type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        s_gl.DeleteShader(sh);
        return 0;
    }
    return sh;
}

static char *sc_build_source(const ScPass *ps, const char *stage_define)
{
    size_t need = strlen(ps->body) + 256;
    char *out = (char *)malloc(need);
    if (!out) return NULL;
    snprintf(out, need, "#version %d\n#define %s\n#define PARAMETER_UNIFORM\n#line 1\n%s",
             ps->version > 0 ? ps->version : 130, stage_define, ps->body);
    return out;
}

static void sc_texu(GLuint prog, const char *base, ScTexU *u)
{
    char n[160];
    snprintf(n, sizeof(n), "%sTexture", base);
    u->tex = s_gl.GetUniformLocation(prog, n);
    snprintf(n, sizeof(n), "%sTextureSize", base);
    u->size = s_gl.GetUniformLocation(prog, n);
    snprintf(n, sizeof(n), "%sInputSize", base);
    u->input = s_gl.GetUniformLocation(prog, n);
}

static void sc_add_bind(ScPass *ps, int kind, int idx, const ScTexU *u)
{
    if (u->tex < 0 && u->size < 0 && u->input < 0 && kind != SC_B_SRC) return;
    ScBind *nb = (ScBind *)realloc(ps->binds, (size_t)(ps->nbinds + 1) * sizeof(ScBind));
    if (!nb) return;
    ps->binds = nb;
    ps->binds[ps->nbinds].kind = kind;
    ps->binds[ps->nbinds].idx = idx;
    ps->binds[ps->nbinds].u = *u;
    ++ps->nbinds;
}

static int sc_link_pass(ScPreset *pre, int i)
{
    ScPass *ps = &pre->pass[i];
    char label[SC_PATH];
    snprintf(label, sizeof(label), "%s", ps->path);
    char *vs_s = sc_build_source(ps, "VERTEX");
    char *fs_s = sc_build_source(ps, "FRAGMENT");
    if (!vs_s || !fs_s) { free(vs_s); free(fs_s); return 0; }
    GLuint vs = sc_compile(label, GL_VERTEX_SHADER, vs_s);
    GLuint fs = vs ? sc_compile(label, GL_FRAGMENT_SHADER, fs_s) : 0;
    free(vs_s);
    free(fs_s);
    if (!vs || !fs) {
        if (vs) s_gl.DeleteShader(vs);
        if (fs) s_gl.DeleteShader(fs);
        return 0;
    }
    GLuint p = s_gl.CreateProgram();
    s_gl.AttachShader(p, vs);
    s_gl.AttachShader(p, fs);
    s_gl.LinkProgram(p);
    s_gl.DeleteShader(vs);
    s_gl.DeleteShader(fs);
    GLint ok = 0;
    s_gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096] = {0};
        s_gl.GetProgramInfoLog(p, sizeof(log) - 1, NULL, log);
        fprintf(stderr, "[SHADER] erro de link (%s):\n%s\n", label, log);
        s_gl.DeleteProgram(p);
        return 0;
    }
    ps->prog = p;
    ps->a_vertex = s_gl.GetAttribLocation(p, "VertexCoord");
    ps->a_tex = s_gl.GetAttribLocation(p, "TexCoord");
    ps->a_color = s_gl.GetAttribLocation(p, "COLOR");
    ps->u_mvp = s_gl.GetUniformLocation(p, "MVPMatrix");
    ps->u_frame_dir = s_gl.GetUniformLocation(p, "FrameDirection");
    ps->u_frame_count = s_gl.GetUniformLocation(p, "FrameCount");
    ps->u_output_size = s_gl.GetUniformLocation(p, "OutputSize");
    ps->u_final_vp = s_gl.GetUniformLocation(p, "FinalViewportSize");

    ps->param_loc = (GLint *)malloc((size_t)(pre->nparam > 0 ? pre->nparam : 1) * sizeof(GLint));
    for (int k = 0; k < pre->nparam; ++k)
        ps->param_loc[k] = s_gl.GetUniformLocation(p, pre->param[k].name);

    ScTexU u;
    sc_texu(p, "", &u);               sc_add_bind(ps, SC_B_SRC, 0, &u);
    sc_texu(p, "Orig", &u);           sc_add_bind(ps, SC_B_ORIG, 0, &u);
    for (int k = 1; k <= i + 1; ++k) {
        char b[32];
        snprintf(b, sizeof(b), "PassPrev%d", k);
        sc_texu(p, b, &u);
        sc_add_bind(ps, SC_B_PREV, k, &u);
    }
    for (int k = 1; k <= i; ++k) {
        char b[32];
        snprintf(b, sizeof(b), "Pass%d", k);
        sc_texu(p, b, &u);
        sc_add_bind(ps, SC_B_PASS, k, &u);
    }
    for (int j = 0; j < i; ++j) {
        if (!pre->pass[j].alias[0]) continue;
        sc_texu(p, pre->pass[j].alias, &u);
        sc_add_bind(ps, SC_B_ALIAS, j, &u);
    }
    sc_texu(p, "Prev", &u);           sc_add_bind(ps, SC_B_HIST, 0, &u);
    for (int k = 1; k <= 6; ++k) {
        char b[32];
        snprintf(b, sizeof(b), "Prev%d", k);
        sc_texu(p, b, &u);
        sc_add_bind(ps, SC_B_HIST, k, &u);
    }
    for (int l = 0; l < pre->nlut; ++l) {
        char n[160];
        u.tex = s_gl.GetUniformLocation(p, pre->lut[l].name);
        snprintf(n, sizeof(n), "%sSize", pre->lut[l].name);
        u.size = s_gl.GetUniformLocation(p, n);
        snprintf(n, sizeof(n), "%sTextureSize", pre->lut[l].name);
        if (u.size < 0) u.size = s_gl.GetUniformLocation(p, n);
        snprintf(n, sizeof(n), "%sInputSize", pre->lut[l].name);
        u.input = s_gl.GetUniformLocation(p, n);
        sc_add_bind(ps, SC_B_LUT, l, &u);
    }
    return 1;
}

static GLuint sc_sampler(int linear, int wrap, int mip)
{
    int idx = (linear ? 1 : 0) | (mip ? 2 : 0) | (wrap << 2);
    if (!s_samplers[idx]) {
        GLuint s = 0;
        s_gl.GenSamplers(1, &s);
        GLint mag = linear ? GL_LINEAR : GL_NEAREST;
        GLint min = mip ? (linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST) : mag;
        GLint w = GL_CLAMP_TO_BORDER;
        if (wrap == SC_WRAP_EDGE) w = GL_CLAMP_TO_EDGE;
        else if (wrap == SC_WRAP_REPEAT) w = GL_REPEAT;
        else if (wrap == SC_WRAP_MIRROR) w = GL_MIRRORED_REPEAT;
        static const GLfloat border[4] = {0, 0, 0, 0};
        s_gl.SamplerParameteri(s, GL_TEXTURE_MIN_FILTER, min);
        s_gl.SamplerParameteri(s, GL_TEXTURE_MAG_FILTER, mag);
        s_gl.SamplerParameteri(s, GL_TEXTURE_WRAP_S, w);
        s_gl.SamplerParameteri(s, GL_TEXTURE_WRAP_T, w);
        s_gl.SamplerParameterfv(s, GL_TEXTURE_BORDER_COLOR, border);
        s_samplers[idx] = s;
    }
    return s_samplers[idx];
}

/* PNG (LUTs) */
static uint8_t *sc_load_png(const char *path, int *out_w, int *out_h)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    uint8_t sig[8];
    if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8) != 0) { fclose(fp); return NULL; }
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return NULL; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return NULL; }
    uint8_t *volatile pixels = NULL;   /* volatile: modificados apos o setjmp */
    png_bytep *volatile rows = NULL;
    if (setjmp(png_jmpbuf(png))) {
        free(rows); free(pixels);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }
    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);
    png_uint_32 w = png_get_image_width(png, info), h = png_get_image_height(png, info);
    int bit_depth = png_get_bit_depth(png, info), ct = png_get_color_type(png, info);
    if (w == 0 || h == 0 || w > 16384 || h > 16384) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }
    if (bit_depth == 16) png_set_strip_16(png);
    if (ct == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (ct == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (ct == PNG_COLOR_TYPE_RGB || ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    png_read_update_info(png, info);
    pixels = (uint8_t *)malloc((size_t)w * h * 4u);
    rows = (png_bytep *)malloc((size_t)h * sizeof(png_bytep));
    if (!pixels || !rows) {
        free(rows); free(pixels);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return NULL;
    }
    for (png_uint_32 y = 0; y < h; ++y) rows[y] = pixels + (size_t)y * w * 4u;
    png_read_image(png, rows);
    png_read_end(png, NULL);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    *out_w = (int)w;
    *out_h = (int)h;
    return pixels;
}

static int sc_save_argb_png(const char *path, const uint32_t *argb, int w, int h)
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
    png_set_IHDR(png, info, (png_uint_32)w, (png_uint_32)h, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < h; ++y) {
        const uint32_t *src = argb + (size_t)y * (size_t)w;
        for (int x = 0; x < w; ++x) {
            uint32_t px = src[x];
            row_rgb[x * 3 + 0] = (uint8_t)((px >> 16) & 0xFFu);
            row_rgb[x * 3 + 1] = (uint8_t)((px >> 8) & 0xFFu);
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

static void sc_release_gl(int have_ctx);

/* Cria tudo que nao depende do tamanho. Chamado dentro de state_begin. */
static int sc_init_resources(SDL_Renderer *renderer)
{
    ScPreset *pre = s_pre;
    SDL_RendererInfo info;
    memset(&info, 0, sizeof(info));
    if (!renderer || SDL_GetRendererInfo(renderer, &info) != 0 || !info.name ||
        strcmp(info.name, "opengl") != 0 || !(info.flags & SDL_RENDERER_TARGETTEXTURE)) {
        fprintf(stderr, "[SHADER] sem renderer OpenGL com target texture (renderer=%s); cadeia desativada\n",
                info.name ? info.name : "?");
        return 0;
    }
    s_renderer = renderer;
    s_ctx = SDL_GL_GetCurrentContext();
    if (!s_ctx) {
        fprintf(stderr, "[SHADER] sem contexto GL atual; cadeia desativada\n");
        return 0;
    }
    if (!sc_load_gl(&s_gl)) {
        fprintf(stderr, "[SHADER] GL incompleto (precisa GL 3.3+); cadeia desativada\n");
        return 0;
    }
    return pre != NULL;
}

static int sc_init_gl_objects(void)
{
    ScPreset *pre = s_pre;
    ScGlState st;
    sc_state_begin(&s_gl, &st);
    int ok = 1;
    s_gl.GenVertexArrays(1, &s_vao);
    s_gl.GenBuffers(1, &s_vbo);
    for (int i = 0; ok && i < pre->npass; ++i)
        ok = sc_link_pass(pre, i);
    for (int l = 0; ok && l < pre->nlut; ++l) {
        int w = 0, h = 0;
        uint8_t *px = sc_load_png(pre->lut[l].path, &w, &h);
        if (!px) {
            fprintf(stderr, "[SHADER] LUT '%s': nao foi possivel ler %s\n", pre->lut[l].name, pre->lut[l].path);
            ok = 0;
            break;
        }
        GLuint t = 0;
        s_gl.GenTextures(1, &t);
        s_gl.ActiveTexture(GL_TEXTURE3);
        s_gl.BindTexture(GL_TEXTURE_2D, t);
        s_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        s_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        s_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        if (pre->lut[l].mipmap) s_gl.GenerateMipmap(GL_TEXTURE_2D);
        free(px);
        pre->lut_tex[l] = t;
        pre->lut_w[l] = w;
        pre->lut_h[l] = h;
    }
    if (ok && s_gl.GenQueries && s_gl.BeginQuery && s_gl.EndQuery &&
        s_gl.GetQueryObjectiv && s_gl.GetQueryObjectui64v && s_gl.DeleteQueries) {
        for (int r = 0; r < SC_RING; ++r)
            s_gl.GenQueries(pre->npass, s_queries[r]);
        s_time_ok = 1;
        s_q_created = 1;
    }
    for (int i = 0; i < pre->npass; ++i)
        s_samples[i] = (float *)calloc(SC_SAMPLES, sizeof(float));
    s_total_samples = (float *)calloc(SC_SAMPLES, sizeof(float));
    (void)s_gl.GetError();
    sc_state_end(&s_gl, &st);
    return ok;
}

static void sc_release_gl(int have_ctx)
{
    ScPreset *pre = s_pre;
    if (have_ctx && s_gl.GetError && pre) {
        for (int i = 0; i < pre->npass; ++i) {
            ScPass *ps = &pre->pass[i];
            if (ps->prog) s_gl.DeleteProgram(ps->prog);
            if (ps->out_fbo) s_gl.DeleteFramebuffers(1, &ps->out_fbo);
            if (ps->out_tex) s_gl.DeleteTextures(1, &ps->out_tex);
            ps->prog = 0; ps->out_fbo = 0; ps->out_tex = 0;
        }
        for (int l = 0; l < pre->nlut; ++l)
            if (pre->lut_tex[l]) { s_gl.DeleteTextures(1, &pre->lut_tex[l]); pre->lut_tex[l] = 0; }
        for (int i = 0; i < 16; ++i)
            if (s_samplers[i]) { s_gl.DeleteSamplers(1, &s_samplers[i]); s_samplers[i] = 0; }
        if (s_orig_tex) s_gl.DeleteTextures(1, &s_orig_tex);
        if (s_vbo) s_gl.DeleteBuffers(1, &s_vbo);
        if (s_vao) s_gl.DeleteVertexArrays(1, &s_vao);
        if (s_q_created)
            for (int r = 0; r < SC_RING; ++r)
                s_gl.DeleteQueries(pre->npass, s_queries[r]);
    }
    s_orig_tex = 0; s_vbo = 0; s_vao = 0; s_orig_w = s_orig_h = 0;
    s_time_ok = 0;
    s_q_created = 0;
    memset(s_samplers, 0, sizeof(s_samplers));
    memset(s_queries, 0, sizeof(s_queries));
    memset(s_q_pending, 0, sizeof(s_q_pending));
}

/* ------------------------------------------------------------------------- */
/* Metricas                                                                   */
/* ------------------------------------------------------------------------- */

static int sc_cmp_f(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static void sc_percentiles(const float *src, uint32_t n, double *p50, double *p99)
{
    *p50 = *p99 = 0.0;
    if (!src || n == 0) return;
    if (n > SC_SAMPLES) n = SC_SAMPLES;
    float *tmp = (float *)malloc(n * sizeof(float));
    if (!tmp) return;
    memcpy(tmp, src, n * sizeof(float));
    qsort(tmp, n, sizeof(float), sc_cmp_f);
    *p50 = tmp[n / 2];
    *p99 = tmp[(uint32_t)((double)(n - 1) * 0.99)];
    free(tmp);
}

static void sc_read_slot(int slot, int blocking)
{
    ScPreset *pre = s_pre;
    if (!s_time_ok || !s_q_pending[slot]) return;
    if (!blocking) {
        GLint ready = 0;
        s_gl.GetQueryObjectiv(s_queries[slot][pre->npass - 1], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) return;
    }
    double total = 0.0;
    uint32_t at = s_nsamples % SC_SAMPLES;
    for (int i = 0; i < pre->npass; ++i) {
        GLuint64 ns = 0;
        s_gl.GetQueryObjectui64v(s_queries[slot][i], GL_QUERY_RESULT, &ns);
        float ms = (float)((double)ns / 1.0e6);
        s_samples[i][at] = ms;
        total += ms;
    }
    s_total_samples[at] = (float)total;
    ++s_nsamples;
    s_q_pending[slot] = 0;
}

/* ------------------------------------------------------------------------- */
/* Execucao por frame                                                         */
/* ------------------------------------------------------------------------- */

static int sc_scale_dim(int type, float scale, int prev, int vp)
{
    double v;
    if (type == SC_SCALE_VIEWPORT) v = (double)scale * (double)vp;
    else if (type == SC_SCALE_ABSOLUTE) v = (double)scale;
    else v = (double)scale * (double)prev;
    if (!(v > 0.0)) v = 1.0;
    if (v > 16384.0) v = 16384.0;
    int r = (int)(v + 0.5);
    if (r < 1) r = 1;
    if (r > 16384) r = 16384;
    return r;
}

static void sc_compute_sizes(int src_w, int src_h, int vp_w, int vp_h)
{
    ScPreset *pre = s_pre;
    int pw = src_w, ph = src_h;
    for (int i = 0; i < pre->npass; ++i) {
        ScPass *ps = &pre->pass[i];
        int last = (i == pre->npass - 1);
        int tx = ps->type_x, ty = ps->type_y;
        float sx = ps->scale_x, sy = ps->scale_y;
        if (tx < 0) { tx = (last && !ps->has_scale_x) ? SC_SCALE_VIEWPORT : SC_SCALE_SOURCE; }
        if (ty < 0) { ty = (last && !ps->has_scale_y) ? SC_SCALE_VIEWPORT : SC_SCALE_SOURCE; }
        ps->out_w = sc_scale_dim(tx, sx, pw, vp_w);
        ps->out_h = sc_scale_dim(ty, sy, ph, vp_h);
        pw = ps->out_w;
        ph = ps->out_h;
    }
}

static int sc_fmt_of(const ScPass *ps)
{
    if (ps->float_fb) return 1;
    if (ps->srgb_fb) return 2;
    return 0;
}

static int sc_ensure_pass_fbo(ScPass *ps, int w, int h, int fmt, int *cur_fmt, int *cur_w, int *cur_h)
{
    if (ps->out_tex && *cur_w == w && *cur_h == h && *cur_fmt == fmt) return 1;
    if (!ps->out_tex) s_gl.GenTextures(1, &ps->out_tex);
    if (!ps->out_fbo) s_gl.GenFramebuffers(1, &ps->out_fbo);
    s_gl.ActiveTexture(GL_TEXTURE3);
    s_gl.BindTexture(GL_TEXTURE_2D, ps->out_tex);
    s_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    s_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    s_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    s_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (fmt == 1)
        s_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    else if (fmt == 2)
        s_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    else
        s_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    s_gl.BindFramebuffer(GL_FRAMEBUFFER, ps->out_fbo);
    s_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ps->out_tex, 0);
    GLenum fs = s_gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fs != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[SHADER] FBO incompleto (0x%x) %dx%d fmt=%d\n", (unsigned)fs, w, h, fmt);
        return 0;
    }
    *cur_w = w; *cur_h = h; *cur_fmt = fmt;
    return 1;
}

static void sc_desc_of_pass(const ScPass *ps, ScTexDesc *d)
{
    d->id = ps->out_tex;
    d->sdl = NULL;
    d->tw = d->iw = ps->out_w;
    d->th = d->ih = ps->out_h;
}

/* Desenha o passe i. fbo_mode: 1 = FBO proprio do passe, 0 = mantem o alvo atual (SDL target). */
static void sc_timing_off(void)
{
    if (s_time_ok)
        fprintf(stderr, "[SHADER] aviso: GL_TIME_ELAPSED falhou; metricas de tempo desligadas (cadeia continua)\n");
    s_time_ok = 0;
}

static int sc_draw_pass(int i, int fbo_mode, const ScTexDesc *src, const ScTexDesc *orig)
{
    ScPreset *pre = s_pre;
    ScPass *ps = &pre->pass[i];
    int ow = ps->out_w, oh = ps->out_h;

    if (fbo_mode) {
        s_gl.BindFramebuffer(GL_FRAMEBUFFER, ps->out_fbo);
        if (ps->srgb_fb) s_gl.Enable(GL_FRAMEBUFFER_SRGB);
        else s_gl.Disable(GL_FRAMEBUFFER_SRGB);
    } else {
        s_gl.Disable(GL_FRAMEBUFFER_SRGB);
    }
    s_gl.Viewport(0, 0, ow, oh);
    s_gl.UseProgram(ps->prog);

    /* geometria */
    float cu = (float)src->iw / (float)src->tw;
    float cv = (float)src->ih / (float)src->th;
    float bw = 1.0f, bh = 1.0f;
    SDL_Texture *bound_sdl[SC_MAX_UNITS];
    int nbound_sdl = 0;

    int unit = 0;
    for (int b = 0; b < ps->nbinds; ++b) {
        const ScBind *bd = &ps->binds[b];
        ScTexDesc d;
        int have = 0;
        switch (bd->kind) {
        case SC_B_SRC:  d = *src; have = 1; break;
        case SC_B_ORIG: d = *orig; have = 1; break;
        case SC_B_HIST: d = *orig; have = 1; break;
        case SC_B_PREV: {
            int j = i - bd->idx;
            if (j >= 0) { sc_desc_of_pass(&pre->pass[j], &d); have = 1; }
            else if (j == -1) { d = *orig; have = 1; }
            break;
        }
        case SC_B_PASS: {
            int j = bd->idx - 1;
            if (j >= 0 && j < i) { sc_desc_of_pass(&pre->pass[j], &d); have = 1; }
            break;
        }
        case SC_B_ALIAS: sc_desc_of_pass(&pre->pass[bd->idx], &d); have = 1; break;
        case SC_B_LUT:
            d.id = pre->lut_tex[bd->idx]; d.sdl = NULL;
            d.tw = d.iw = pre->lut_w[bd->idx];
            d.th = d.ih = pre->lut_h[bd->idx];
            have = 1;
            break;
        default: break;
        }
        if (!have) continue;
        if (bd->kind == SC_B_HIST && !s_warned_hist) {
            s_warned_hist = 1;
            fprintf(stderr, "[SHADER] aviso: historico (Prev*Texture) aproximado pelo frame atual\n");
        }
        if (bd->u.tex >= 0) {
            if (unit >= SC_MAX_UNITS) { sc_warn_once("unidades de textura esgotadas em %s", ps->path); continue; }
            int linear = ps->linear, wrap = ps->wrap, mip = 0;
            if (bd->kind == SC_B_LUT) {
                linear = pre->lut[bd->idx].linear; wrap = pre->lut[bd->idx].wrap;
                mip = pre->lut[bd->idx].mipmap;
            } else if (bd->kind == SC_B_SRC) {
                mip = ps->mipmap;
            }
            s_gl.ActiveTexture(GL_TEXTURE0 + (GLenum)unit);
            if (d.sdl) {
                float tw = 1.0f, th = 1.0f;
                if (SDL_GL_BindTexture(d.sdl, &tw, &th) != 0) return 0;
                bound_sdl[nbound_sdl++] = d.sdl;
                if (bd->kind == SC_B_SRC) { bw = tw; bh = th; }
            } else {
                s_gl.BindTexture(GL_TEXTURE_2D, d.id);
            }
            if (mip && bd->kind != SC_B_LUT) s_gl.GenerateMipmap(GL_TEXTURE_2D);
            s_gl.BindSampler((GLuint)unit, sc_sampler(linear, wrap, mip));
            s_gl.Uniform1i(bd->u.tex, unit);
            ++unit;
        }
        if (bd->u.size >= 0) s_gl.Uniform2f(bd->u.size, (float)d.tw, (float)d.th);
        if (bd->u.input >= 0) s_gl.Uniform2f(bd->u.input, (float)d.iw, (float)d.ih);
    }
    cu *= bw;
    cv *= bh;

    static const float mvp[16] = { 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1 };
    if (ps->u_mvp >= 0) s_gl.UniformMatrix4fv(ps->u_mvp, 1, GL_FALSE, mvp);
    if (ps->u_frame_dir >= 0) s_gl.Uniform1i(ps->u_frame_dir, 1);
    if (ps->u_frame_count >= 0) {
        uint32_t fc = s_frames;
        if (ps->frame_count_mod > 0) fc %= (uint32_t)ps->frame_count_mod;
        s_gl.Uniform1i(ps->u_frame_count, (GLint)fc);
    }
    if (ps->u_output_size >= 0) s_gl.Uniform2f(ps->u_output_size, (float)ow, (float)oh);
    if (ps->u_final_vp >= 0) s_gl.Uniform2f(ps->u_final_vp, (float)s_vp_w, (float)s_vp_h);
    for (int k = 0; k < pre->nparam; ++k)
        if (ps->param_loc[k] >= 0) s_gl.Uniform1f(ps->param_loc[k], pre->param[k].value);

    const float verts[4][12] = {
        { 0, 0, 0, 1,  0,  0, 0, 1,  1, 1, 1, 1 },
        { 1, 0, 0, 1,  cu, 0, 0, 1,  1, 1, 1, 1 },
        { 0, 1, 0, 1,  0,  cv, 0, 1, 1, 1, 1, 1 },
        { 1, 1, 0, 1,  cu, cv, 0, 1, 1, 1, 1, 1 },
    };
    s_gl.BindVertexArray(s_vao);
    s_gl.BindBuffer(GL_ARRAY_BUFFER, s_vbo);
    s_gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(verts), verts, GL_STREAM_DRAW);
    for (GLuint a = 0; a < 8; ++a) s_gl.DisableVertexAttribArray(a);
    const GLsizei stride = (GLsizei)(12 * sizeof(float));
    if (ps->a_vertex >= 0) {
        s_gl.EnableVertexAttribArray((GLuint)ps->a_vertex);
        s_gl.VertexAttribPointer((GLuint)ps->a_vertex, 4, GL_FLOAT, GL_FALSE, stride, (const void *)0);
    }
    if (ps->a_tex >= 0) {
        s_gl.EnableVertexAttribArray((GLuint)ps->a_tex);
        s_gl.VertexAttribPointer((GLuint)ps->a_tex, 4, GL_FLOAT, GL_FALSE, stride, (const void *)(4 * sizeof(float)));
    }
    if (ps->a_color >= 0) {
        s_gl.EnableVertexAttribArray((GLuint)ps->a_color);
        s_gl.VertexAttribPointer((GLuint)ps->a_color, 4, GL_FLOAT, GL_FALSE, stride, (const void *)(8 * sizeof(float)));
    }

    (void)s_gl.GetError();  /* erros de uniform/tipo nao sao fatais */
    int slot = (int)(s_frames % SC_RING);
    int timed = s_time_ok && s_nsamples < 0xFFFFFFF0u;
    if (timed) {
        s_gl.BeginQuery(GL_TIME_ELAPSED, s_queries[slot][i]);
        if (s_gl.GetError() != GL_NO_ERROR) { sc_timing_off(); timed = 0; }
    }
    s_gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (timed) {
        s_gl.EndQuery(GL_TIME_ELAPSED);
        /* falha so na query: desliga apenas o timing (o erro nao e do desenho) */
        if (s_gl.GetError() != GL_NO_ERROR) { sc_timing_off(); timed = 0; }
    }
    GLenum err = s_gl.GetError();

    for (int k = 0; k < nbound_sdl; ++k) SDL_GL_UnbindTexture(bound_sdl[k]);
    for (GLuint a = 0; a < 8; ++a) s_gl.DisableVertexAttribArray(a);
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "[SHADER] erro GL 0x%x no passe %d (%s)\n", (unsigned)err, i, ps->path);
        return 0;
    }
    return 1;
}

static void sc_compute_viewport(SDL_Renderer *renderer, int *vw, int *vh)
{
    int ow = 0, oh = 0, lw = 0, lh = 0;
    SDL_GetRendererOutputSize(renderer, &ow, &oh);
    SDL_RenderGetLogicalSize(renderer, &lw, &lh);
    /* Diagnóstico/benchmark (S3.3): EC_SHADER_VIEWPORT=LxA simula um monitor de
     * LxA pixels (ex.: 3840x2160) — o fit do canvas lógico é calculado sobre ele e o
     * SDL redimensiona o resultado para a janela real. */
    {
        const char *ov = getenv("EC_SHADER_VIEWPORT");
        int fw = 0, fh = 0;
        if (ov && sscanf(ov, "%dx%d", &fw, &fh) == 2 && fw > 0 && fh > 0) {
            ow = fw;
            oh = fh;
        }
    }
    if (ow < 1) ow = 1;
    if (oh < 1) oh = 1;
    if (lw > 0 && lh > 0) {
        double s = (double)ow / lw;
        double s2 = (double)oh / lh;
        if (s2 < s) s = s2;
        if (SDL_RenderGetIntegerScale(renderer) && s >= 1.0) s = floor(s);
        *vw = (int)(lw * s + 0.5);
        *vh = (int)(lh * s + 0.5);
    } else {
        *vw = ow;
        *vh = oh;
    }
    if (*vw < 1) *vw = 1;
    if (*vh < 1) *vh = 1;
    if (*vw > 8192) *vw = 8192;
    if (*vh > 8192) *vh = 8192;
}

static void sc_capture(SDL_Renderer *renderer)
{
    const char *cap = getenv("EC_SHADER_CAPTURE");
    if (!cap || !*cap) return;
    uint32_t target = 1u;
    const char *at = getenv("EC_SHADER_CAPTURE_AT");
    if (at && *at) {
        char *end = NULL;
        unsigned long v = strtoul(at, &end, 10);
        if (!end || *end) return;
        target = (uint32_t)v;
    }
    if (s_frames != target) return;
    uint32_t *buf = (uint32_t *)malloc((size_t)s_out_w * (size_t)s_out_h * sizeof(uint32_t));
    if (!buf) return;
    SDL_Texture *old = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, s_output) == 0 &&
        SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, buf,
                             s_out_w * (int)sizeof(uint32_t)) == 0) {
        if (sc_save_argb_png(cap, buf, s_out_w, s_out_h) == 0)
            fprintf(stderr, "[EC_SHADER_CAPTURE] frame=%u wrote %s (%dx%d)\n",
                    (unsigned)s_frames, cap, s_out_w, s_out_h);
    }
    SDL_SetRenderTarget(renderer, old);
    free(buf);
}

static void sc_disable(const char *why)
{
    if (!s_failed) fprintf(stderr, "[SHADER] cadeia desativada: %s\n", why);
    s_failed = 1;
}

SDL_Texture *ec_shader_chain_process(SDL_Renderer *renderer, SDL_Texture *base_tex,
                                     const uint32_t *argb, int width, int height, int stride_px)
{
    if (!ec_shader_chain_active() || !renderer || !argb || width <= 0 || height <= 0 || stride_px < width)
        return NULL;
    ScPreset *pre = s_pre;

    if (!s_gl_ready) {
        s_gl_ready = 1;  /* tenta uma unica vez */
        if (!sc_init_resources(renderer)) { s_failed = 1; return NULL; }  /* ja logou o motivo */
        if (!sc_init_gl_objects()) {
            sc_release_gl(1);
            sc_disable("falha ao compilar/ligar os shaders");
            return NULL;
        }
        fprintf(stderr, "[SHADER] cadeia pronta: preset=%s passes=%d\n", pre->name, pre->npass);
    }
    if (s_failed) return NULL;
    if (SDL_GL_GetCurrentContext() != s_ctx) { sc_disable("contexto GL mudou"); return NULL; }

    SDL_RenderFlush(renderer);

    int src_w = width, src_h = height;
    if (base_tex) {
        int tw = 0, th = 0;
        if (SDL_QueryTexture(base_tex, NULL, NULL, &tw, &th) == 0 && tw > 0 && th > 0) { src_w = tw; src_h = th; }
        else base_tex = NULL;
    }
    int vw, vh;
    sc_compute_viewport(renderer, &vw, &vh);
    s_vp_w = vw;
    s_vp_h = vh;
    sc_compute_sizes(src_w, src_h, vw, vh);
    int lw = pre->pass[pre->npass - 1].out_w, lh = pre->pass[pre->npass - 1].out_h;

    if (!s_output || s_out_w != lw || s_out_h != lh) {
        if (s_output) SDL_DestroyTexture(s_output);
        s_output = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, lw, lh);
        s_out_w = lw;
        s_out_h = lh;
        if (!s_output) { sc_disable("SDL_CreateTexture(target) falhou"); return NULL; }
    }

    SDL_Texture *old_target = SDL_GetRenderTarget(renderer);
    ScGlState st;
    sc_state_begin(&s_gl, &st);

    /* ring de queries: coleta o slot antigo antes de reutiliza-lo */
    int slot = (int)(s_frames % SC_RING);
    sc_read_slot(slot, 0);
    s_q_pending[slot] = 0;

    /* Original */
    s_gl.ActiveTexture(GL_TEXTURE3);
    if (!s_orig_tex) s_gl.GenTextures(1, &s_orig_tex);
    s_gl.BindTexture(GL_TEXTURE_2D, s_orig_tex);
    if (s_orig_w != width || s_orig_h != height) {
        s_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        s_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        s_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        s_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        s_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
        s_orig_w = width;
        s_orig_h = height;
    }
    s_gl.PixelStorei(GL_UNPACK_ROW_LENGTH, stride_px);
    s_gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, argb);
    s_gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    ScTexDesc orig;
    orig.id = s_orig_tex; orig.sdl = NULL;
    orig.tw = orig.iw = width;
    orig.th = orig.ih = height;
    ScTexDesc src = orig;
    if (base_tex) {
        src.id = 0; src.sdl = base_tex;
        src.tw = src.iw = src_w;
        src.th = src.ih = src_h;
    }

    int ok = 1;
    int nlast = pre->npass - 1;
    for (int i = 0; ok && i < nlast; ++i) {
        ScPass *ps = &pre->pass[i];
        int fmt = sc_fmt_of(ps);
        /* out_w/out_h ja calculados; guardamos o tamanho anterior no proprio FBO via tex query */
        static int cw[SC_MAX_PASSES], ch[SC_MAX_PASSES], cf[SC_MAX_PASSES];
        ok = sc_ensure_pass_fbo(ps, ps->out_w, ps->out_h, fmt, &cf[i], &cw[i], &ch[i]);
        if (!ok) break;
        ScTexDesc in;
        if (i == 0) in = src;
        else sc_desc_of_pass(&pre->pass[i - 1], &in);
        ok = sc_draw_pass(i, 1, &in, &orig);
    }
    sc_state_end(&s_gl, &st);
    if (!ok) { sc_disable("falha em passe intermediario"); return NULL; }

    if (SDL_SetRenderTarget(renderer, s_output) != 0) { sc_disable("SDL_SetRenderTarget falhou"); return NULL; }
    if (SDL_RenderFlush(renderer) != 0) {
        SDL_SetRenderTarget(renderer, old_target);
        sc_disable("SDL_RenderFlush falhou");
        return NULL;
    }
    sc_state_begin(&s_gl, &st);
    {
        ScTexDesc in;
        if (nlast == 0) in = src;
        else sc_desc_of_pass(&pre->pass[nlast - 1], &in);
        ok = sc_draw_pass(nlast, 0, &in, &orig);
    }
    sc_state_end(&s_gl, &st);
    SDL_SetRenderTarget(renderer, old_target);
    if (!ok) { sc_disable("falha no ultimo passe"); return NULL; }

    s_q_pending[slot] = s_time_ok ? 1 : 0;
    ++s_frames;
    sc_capture(renderer);
    return s_output;
}

void ec_shader_chain_destroy(void)
{
    if (s_gl_ready && s_pre) {
        int have_ctx = (s_ctx && s_gl.GetError && SDL_GL_GetCurrentContext() == s_ctx);
        if (have_ctx && s_time_ok) {
            for (int r = 0; r < SC_RING; ++r) {
                int slot = (int)(((s_frames % SC_RING) + (uint32_t)r) % SC_RING);
                sc_read_slot(slot, 1);
            }
        }
        if (s_frames > 0 && s_nsamples > 0) {
            double p50, p99;
            uint32_t n = s_nsamples < SC_SAMPLES ? s_nsamples : SC_SAMPLES;
            sc_percentiles(s_total_samples, n, &p50, &p99);
            fprintf(stderr, "[SHADER] preset=%s passes=%d frames=%u p50_ms=%.3f p99_ms=%.3f\n",
                    s_pre->name, s_pre->npass, (unsigned)s_frames, p50, p99);
            for (int i = 0; i < s_pre->npass; ++i) {
                double a, b;
                sc_percentiles(s_samples[i], n, &a, &b);
                fprintf(stderr, "[SHADER]   pass %d %dx%d p50_ms=%.3f p99_ms=%.3f (%s)\n", i,
                        s_pre->pass[i].out_w, s_pre->pass[i].out_h, a, b, s_pre->pass[i].path);
            }
        } else if (s_frames > 0) {
            fprintf(stderr, "[SHADER] preset=%s passes=%d frames=%u (sem GL_TIME_ELAPSED)\n",
                    s_pre->name, s_pre->npass, (unsigned)s_frames);
        }
        sc_release_gl(have_ctx);
    }
    if (s_output) { SDL_DestroyTexture(s_output); s_output = NULL; }
    for (int i = 0; i < SC_MAX_PASSES; ++i) { free(s_samples[i]); s_samples[i] = NULL; }
    free(s_total_samples);
    s_total_samples = NULL;
    s_nsamples = 0;
    s_frames = 0;
    s_out_w = s_out_h = 0;
    s_gl_ready = 0;
    s_failed = 0;
    s_warned_hist = 0;
    sc_free_preset(s_pre);
    s_pre = NULL;
    s_resolved = 0;  /* proxima chamada reparseia (preset/params continuam configurados) */
    s_active = 0;
}

#else /* plataformas sem GL de desktop: stubs */

void ec_shader_chain_set_preset_cli(const char *preset) { (void)preset; }
void ec_shader_chain_add_param_cli(const char *kv) { (void)kv; }
void ec_shader_chain_set_settings(const char *preset, const char *params) { (void)preset; (void)params; }
int ec_shader_chain_active(void) { return 0; }
int ec_shader_chain_get_bplus(const char **model, const char **dedither, double *intensity, int *enable)
{
    (void)model; (void)dedither; (void)intensity; (void)enable;
    return 0;
}
struct SDL_Texture *ec_shader_chain_process(struct SDL_Renderer *renderer, struct SDL_Texture *base_tex,
                                            const uint32_t *argb, int width, int height, int stride_px)
{
    (void)renderer; (void)base_tex; (void)argb; (void)width; (void)height; (void)stride_px;
    return NULL;
}
void ec_shader_chain_destroy(void) {}

#endif /* desktop GL */
