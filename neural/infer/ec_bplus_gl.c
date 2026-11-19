/*
 * ec_bplus_gl.c — Zero-dependency OpenGL 4.3+ Compute Shader backend for Mode B+
 * (ECStudentESPCN4x, P3 / N3), reading versioned ECBP v1 weights.
 *
 * Host-presentation only: never alters guest WRAM, VDP state, snapshots or determinism.
 */

#include "ec_bplus_gl.h"

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
#include <SDL2/SDL_opengl.h>
#include "png_write.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ECBP_MAGIC_0 'E'
#define ECBP_MAGIC_1 'C'
#define ECBP_MAGIC_2 'B'
#define ECBP_MAGIC_3 'P'
#define ECBP_VERSION 1u
#define ECBP_MAX_BLOCKS 16u
#define ECBP_MAX_LAYERS (2u + 2u * ECBP_MAX_BLOCKS)
#define BPLUS_QUERY_SLOTS 8u
#define BPLUS_MAX_SAMPLES 4096u

#ifndef GL_UNPACK_IMAGE_HEIGHT
#define GL_UNPACK_IMAGE_HEIGHT 0x806E
#endif
#ifndef GL_UNPACK_SKIP_IMAGES
#define GL_UNPACK_SKIP_IMAGES 0x806D
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_TEXTURE_BASE_LEVEL
#define GL_TEXTURE_BASE_LEVEL 0x813C
#endif

#define BPLUS_GL_API(X) \
    X(const GLubyte*, GetString, (GLenum)) \
    X(void, GetIntegerv, (GLenum, GLint*)) \
    X(void, GetBooleanv, (GLenum, GLboolean*)) \
    X(GLboolean, IsEnabled, (GLenum)) \
    X(GLenum, GetError, (void)) \
    X(void, Enable, (GLenum)) \
    X(void, Disable, (GLenum)) \
    X(void, Finish, (void)) \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(void, PixelStorei, (GLenum, GLint)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, GenTextures, (GLsizei, GLuint*)) \
    X(void, DeleteTextures, (GLsizei, const GLuint*)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, TexStorage3D, (GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei)) \
    X(void, TexSubImage3D, (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(void, BindImageTexture, (GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum)) \
    X(void, GenBuffers, (GLsizei, GLuint*)) \
    X(void, DeleteBuffers, (GLsizei, const GLuint*)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum)) \
    X(void, BindBufferBase, (GLenum, GLuint, GLuint)) \
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
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, Uniform2i, (GLint, GLint, GLint)) \
    X(void, DispatchCompute, (GLuint, GLuint, GLuint)) \
    X(void, MemoryBarrier, (GLbitfield)) \
    X(void, GenVertexArrays, (GLsizei, GLuint*)) \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))

typedef struct BplusGl {
#define BPLUS_DECL(ret, name, args) ret (APIENTRY *name) args;
    BPLUS_GL_API(BPLUS_DECL)
#undef BPLUS_DECL
    void (APIENTRY *GenQueries)(GLsizei, GLuint*);
    void (APIENTRY *DeleteQueries)(GLsizei, const GLuint*);
    void (APIENTRY *BeginQuery)(GLenum, GLuint);
    void (APIENTRY *EndQuery)(GLenum);
    void (APIENTRY *GetQueryObjectiv)(GLuint, GLenum, GLint*);
    void (APIENTRY *GetQueryObjectui64v)(GLuint, GLenum, GLuint64*);
} BplusGl;

typedef struct LayerDesc {
    unsigned w_offset; /* index in mat4 array */
    unsigned b_offset; /* index in vec4 array */
    unsigned g_in;
    unsigned g_out;
} LayerDesc;

typedef struct EcbpModel {
    unsigned channels;
    unsigned num_blocks;
    unsigned scale;
    float    slope;
    unsigned layer_count;
    LayerDesc layers[ECBP_MAX_LAYERS];
    float   *weights_mat4; /* total_mat4 * 16 floats */
    size_t   total_mat4;
    float   *biases_vec4;  /* total_vec4 * 4 floats */
    size_t   total_vec4;
} EcbpModel;

struct EcBplusGl {
    SDL_Renderer *renderer;
    SDL_GLContext context;
    SDL_Texture  *output;
    BplusGl       gl;
    EcbpModel     model;

    GLuint conv_prog;
    GLuint up_prog;
    GLuint ssbo_w;
    GLuint ssbo_b;
    GLuint vao;

    GLuint tex_in;
    GLuint tex_feat[3];
    GLuint tex_tail;

    GLint u_width, u_height, u_g_in, u_w_offset, u_b_offset, u_act, u_add_res, u_slope;
    GLint u_tex_in, u_tex_res;
    GLint up_u_in, up_u_tail, up_u_in_size, up_u_scale;

    GLuint   queries[BPLUS_QUERY_SLOTS];
    unsigned query_pending[BPLUS_QUERY_SLOTS];
    unsigned query_cursor;
    int      time_elapsed_ok;
    int      sync_timing;

    double  *samples_ms;
    unsigned sample_count;
    double   budget_ms;

    unsigned in_w, in_h, out_w, out_h, max_texture;
    int      failed;

    EcBplusMetrics metrics;
};

typedef struct BplusGlState {
    GLint program, active_tex, vao, unpack_buf, ssbo;
    GLint viewport[4], scissor[4];
    GLint tex2d[2], tex2d_arr[2], unpack[8];
    GLboolean mask[4], enabled[8];
} BplusGlState;

static const GLenum s_switches[8] = {
    GL_BLEND, GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST,
    GL_CULL_FACE, GL_DITHER, GL_FRAMEBUFFER_SRGB, GL_RASTERIZER_DISCARD
};

static const GLenum s_stores[8] = {
    GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_PIXELS,
    GL_UNPACK_SKIP_ROWS, GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST,
    GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_IMAGES
};

static const char s_conv3x3_cs[] =
    "#version 430 core\n"
    "layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
    "layout(std430, binding = 0) readonly buffer WeightsBuf { mat4 w_mat[]; };\n"
    "layout(std430, binding = 1) readonly buffer BiasesBuf  { vec4 b_vec[]; };\n"
    "layout(binding = 0) uniform sampler2DArray u_in;\n"
    "layout(binding = 1) uniform sampler2DArray u_res;\n"
    "layout(rgba16f, binding = 0) writeonly uniform image2DArray u_out;\n"
    "uniform int   u_width;\n"
    "uniform int   u_height;\n"
    "uniform int   u_g_in;\n"
    "uniform int   u_w_offset;\n"
    "uniform int   u_b_offset;\n"
    "uniform int   u_act;\n"
    "uniform int   u_add_res;\n"
    "uniform float u_slope;\n"
    "shared vec4 s_tile[10][10];\n"
    "void main() {\n"
    "    int tid = int(gl_LocalInvocationIndex);\n"
    "    int lx  = int(gl_LocalInvocationID.x);\n"
    "    int ly  = int(gl_LocalInvocationID.y);\n"
    "    int gx  = int(gl_WorkGroupID.x) * 8 + lx;\n"
    "    int gy  = int(gl_WorkGroupID.y) * 8 + ly;\n"
    "    int go  = int(gl_GlobalInvocationID.z);\n"
    "    ivec2 tile_base = ivec2(gl_WorkGroupID.xy) * 8 - ivec2(1, 1);\n"
    "    vec4 acc = b_vec[u_b_offset + go];\n"
    "    for (int gi = 0; gi < u_g_in; ++gi) {\n"
    "        {\n"
    "            int ty = tid / 10;\n"
    "            int tx = tid - ty * 10;\n"
    "            ivec2 p = tile_base + ivec2(tx, ty);\n"
    "            s_tile[ty][tx] = (p.x >= 0 && p.x < u_width && p.y >= 0 && p.y < u_height)\n"
    "                ? texelFetch(u_in, ivec3(p, gi), 0)\n"
    "                : vec4(0.0);\n"
    "        }\n"
    "        if (tid < 36) {\n"
    "            int idx2 = tid + 64;\n"
    "            int ty = idx2 / 10;\n"
    "            int tx = idx2 - ty * 10;\n"
    "            ivec2 p = tile_base + ivec2(tx, ty);\n"
    "            s_tile[ty][tx] = (p.x >= 0 && p.x < u_width && p.y >= 0 && p.y < u_height)\n"
    "                ? texelFetch(u_in, ivec3(p, gi), 0)\n"
    "                : vec4(0.0);\n"
    "        }\n"
    "        barrier();\n"
    "        int wbase = u_w_offset + (go * u_g_in + gi) * 9;\n"
    "        for (int dy = 0; dy < 3; ++dy) {\n"
    "            int row = wbase + dy * 3;\n"
    "            acc += w_mat[row + 0] * s_tile[ly + dy][lx + 0];\n"
    "            acc += w_mat[row + 1] * s_tile[ly + dy][lx + 1];\n"
    "            acc += w_mat[row + 2] * s_tile[ly + dy][lx + 2];\n"
    "        }\n"
    "        barrier();\n"
    "    }\n"
    "    if (gx < u_width && gy < u_height) {\n"
    "        if (u_add_res != 0)\n"
    "            acc += texelFetch(u_res, ivec3(gx, gy, go), 0);\n"
    "        if (u_act != 0)\n"
    "            acc = max(acc, acc * u_slope);\n"
    "        imageStore(u_out, ivec3(gx, gy, go), acc);\n"
    "    }\n"
    "}\n";

static const char s_upscale_vs[] =
    "#version 330 core\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char s_upscale_fs[] =
    "#version 330 core\n"
    "uniform sampler2DArray u_in;\n"
    "uniform sampler2DArray u_tail;\n"
    "uniform ivec2 u_in_size;\n"
    "uniform int   u_scale;\n"
    "out vec4 result;\n"
    "void main() {\n"
    "    ivec2 op = ivec2(gl_FragCoord.xy);\n"
    "    vec2 src = (vec2(op) + 0.5) / float(u_scale) - 0.5;\n"
    "    vec2 sc = clamp(src, vec2(0.0), vec2(u_in_size - 1));\n"
    "    ivec2 p0 = ivec2(floor(sc));\n"
    "    ivec2 p1 = min(p0 + ivec2(1), u_in_size - 1);\n"
    "    vec2 f = sc - vec2(p0);\n"
    "    vec4 c00 = texelFetch(u_in, ivec3(p0.x, p0.y, 0), 0);\n"
    "    vec4 c10 = texelFetch(u_in, ivec3(p1.x, p0.y, 0), 0);\n"
    "    vec4 c01 = texelFetch(u_in, ivec3(p0.x, p1.y, 0), 0);\n"
    "    vec4 c11 = texelFetch(u_in, ivec3(p1.x, p1.y, 0), 0);\n"
    "    vec4 base = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);\n"
    "    ivec2 lp = op / u_scale;\n"
    "    int sx = op.x - lp.x * u_scale;\n"
    "    int sy = op.y - lp.y * u_scale;\n"
    "    int s2 = u_scale * u_scale;\n"
    "    int sub = sy * u_scale + sx;\n"
    "    int c0 = 0 * s2 + sub;\n"
    "    int c1 = 1 * s2 + sub;\n"
    "    int c2 = 2 * s2 + sub;\n"
    "    int c3 = 3 * s2 + sub;\n"
    "    vec4 delta = vec4(\n"
    "        texelFetch(u_tail, ivec3(lp, c0 >> 2), 0)[c0 & 3],\n"
    "        texelFetch(u_tail, ivec3(lp, c1 >> 2), 0)[c1 & 3],\n"
    "        texelFetch(u_tail, ivec3(lp, c2 >> 2), 0)[c2 & 3],\n"
    "        texelFetch(u_tail, ivec3(lp, c3 >> 2), 0)[c3 & 3]\n"
    "    );\n"
    "    result = clamp(base + delta, 0.0, 1.0);\n"
    "}\n";

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float read_le_f32(const uint8_t *p)
{
    uint32_t u = read_le32(p);
    float f = 0.0f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

static void free_ecbp_model(EcbpModel *m)
{
    free(m->weights_mat4);
    free(m->biases_vec4);
    memset(m, 0, sizeof(*m));
}

static int pack_conv_layer(
    EcbpModel *m, unsigned layer_idx,
    const uint8_t *payload, size_t *float_cursor, size_t total_floats,
    unsigned out_ch, unsigned in_ch,
    size_t mat4_cursor, size_t vec4_cursor)
{
    size_t w_floats = (size_t)out_ch * in_ch * 9u;
    size_t b_floats = (size_t)out_ch;
    if (*float_cursor + w_floats + b_floats > total_floats)
        return 0;

    unsigned g_out = out_ch / 4u;
    unsigned g_in = in_ch / 4u;
    const uint8_t *w_raw = payload + (*float_cursor) * 4u;
    const uint8_t *b_raw = payload + (*float_cursor + w_floats) * 4u;

    for (unsigned go = 0; go < g_out; ++go) {
        for (unsigned gi = 0; gi < g_in; ++gi) {
            for (unsigned dy = 0; dy < 3u; ++dy) {
                for (unsigned dx = 0; dx < 3u; ++dx) {
                    size_t mat_idx = mat4_cursor + ((size_t)(go * g_in + gi) * 9u + dy * 3u + dx);
                    float *dst = m->weights_mat4 + mat_idx * 16u;
                    for (unsigned ic_l = 0; ic_l < 4u; ++ic_l) {
                        unsigned ic = gi * 4u + ic_l;
                        for (unsigned oc_l = 0; oc_l < 4u; ++oc_l) {
                            unsigned oc = go * 4u + oc_l;
                            size_t src_idx = (((size_t)oc * in_ch + ic) * 3u + dy) * 3u + dx;
                            /* Column-major mat4: dst[col*4 + row] with col=ic_l, row=oc_l */
                            dst[ic_l * 4u + oc_l] = read_le_f32(w_raw + src_idx * 4u);
                        }
                    }
                }
            }
        }
        float *bdst = m->biases_vec4 + (vec4_cursor + go) * 4u;
        for (unsigned oc_l = 0; oc_l < 4u; ++oc_l) {
            bdst[oc_l] = read_le_f32(b_raw + ((size_t)go * 4u + oc_l) * 4u);
        }
    }

    m->layers[layer_idx].w_offset = (unsigned)mat4_cursor;
    m->layers[layer_idx].b_offset = (unsigned)vec4_cursor;
    m->layers[layer_idx].g_in = g_in;
    m->layers[layer_idx].g_out = g_out;

    *float_cursor += w_floats + b_floats;
    return 1;
}

static int load_ecbp_model(const char *path, EcbpModel *out)
{
    memset(out, 0, sizeof(*out));
    if (!path || !*path) {
        fprintf(stderr, "[BPLUS_GL] empty model path\n");
        return 0;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[BPLUS_GL] failed to open %s\n", path);
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 0; }
    long fsz = ftell(fp);
    if (fsz < 32 || fsz > 64L * 1024L * 1024L || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        fprintf(stderr, "[BPLUS_GL] invalid file size for %s\n", path);
        return 0;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)fsz);
    if (!buf) { fclose(fp); return 0; }
    size_t nr = fread(buf, 1, (size_t)fsz, fp);
    fclose(fp);
    if (nr != (size_t)fsz) { free(buf); return 0; }

    if (buf[0] != ECBP_MAGIC_0 || buf[1] != ECBP_MAGIC_1 ||
        buf[2] != ECBP_MAGIC_2 || buf[3] != ECBP_MAGIC_3) {
        free(buf);
        fprintf(stderr, "[BPLUS_GL] invalid ECBP magic in %s\n", path);
        return 0;
    }

    uint32_t ver = read_le32(buf + 4);
    uint32_t ch = read_le32(buf + 8);
    uint32_t nb = read_le32(buf + 12);
    uint32_t sc = read_le32(buf + 16);
    float slope = read_le_f32(buf + 20);
    uint32_t total_floats = read_le32(buf + 24);
    uint32_t expected_crc = read_le32(buf + 28);

    unsigned out_tail_ch = 4u * sc * sc;
    size_t expected_floats = (size_t)(ch * 4u * 9u + ch) +
        (size_t)nb * 2u * ((size_t)ch * ch * 9u + ch) +
        (size_t)(out_tail_ch * ch * 9u + out_tail_ch);

    if (ver != ECBP_VERSION || ch == 0 || (ch % 4u) != 0 ||
        nb < 1u || nb > ECBP_MAX_BLOCKS || sc < 1u || sc > 4u ||
        (size_t)total_floats != expected_floats ||
        (size_t)fsz != 32u + (size_t)total_floats * 4u) {
        free(buf);
        fprintf(stderr, "[BPLUS_GL] invalid ECBP header metadata in %s\n", path);
        return 0;
    }

    const uint8_t *payload = buf + 32;
    uint32_t actual_crc = png_crc32(payload, (size_t)total_floats * 4u);
    if (actual_crc != expected_crc) {
        free(buf);
        fprintf(stderr, "[BPLUS_GL] ECBP CRC32 mismatch (%08x != %08x)\n", actual_crc, expected_crc);
        return 0;
    }

    unsigned g_c = ch / 4u;
    unsigned g_tail = out_tail_ch / 4u;
    size_t total_mat4 = (size_t)g_c * 1u * 9u +
        (size_t)nb * 2u * ((size_t)g_c * g_c * 9u) +
        (size_t)g_tail * g_c * 9u;
    size_t total_vec4 = (size_t)g_c + (size_t)nb * 2u * g_c + g_tail;

    out->weights_mat4 = (float *)calloc(total_mat4 * 16u, sizeof(float));
    out->biases_vec4 = (float *)calloc(total_vec4 * 4u, sizeof(float));
    if (!out->weights_mat4 || !out->biases_vec4) {
        free(buf);
        free_ecbp_model(out);
        return 0;
    }
    out->channels = ch;
    out->num_blocks = nb;
    out->scale = sc;
    out->slope = slope;
    out->layer_count = 2u + 2u * nb;
    out->total_mat4 = total_mat4;
    out->total_vec4 = total_vec4;

    size_t fcur = 0, mcur = 0, vcur = 0;
    if (!pack_conv_layer(out, 0, payload, &fcur, total_floats, ch, 4u, mcur, vcur)) {
        free(buf); free_ecbp_model(out); return 0;
    }
    mcur += (size_t)g_c * 1u * 9u;
    vcur += g_c;

    for (unsigned b = 0; b < nb; ++b) {
        if (!pack_conv_layer(out, 1u + b * 2u, payload, &fcur, total_floats, ch, ch, mcur, vcur)) {
            free(buf); free_ecbp_model(out); return 0;
        }
        mcur += (size_t)g_c * g_c * 9u;
        vcur += g_c;
        if (!pack_conv_layer(out, 1u + b * 2u + 1u, payload, &fcur, total_floats, ch, ch, mcur, vcur)) {
            free(buf); free_ecbp_model(out); return 0;
        }
        mcur += (size_t)g_c * g_c * 9u;
        vcur += g_c;
    }

    if (!pack_conv_layer(out, 1u + 2u * nb, payload, &fcur, total_floats, out_tail_ch, ch, mcur, vcur)) {
        free(buf); free_ecbp_model(out); return 0;
    }

    free(buf);
    return 1;
}

static int load_bplus_gl(BplusGl *g)
{
#define BPLUS_LOAD(ret, name, args) \
    g->name = (ret (APIENTRY *) args)SDL_GL_GetProcAddress("gl" #name); \
    if (!g->name) { fprintf(stderr, "[BPLUS_GL] missing GL symbol: gl%s\n", #name); return 0; }
    BPLUS_GL_API(BPLUS_LOAD)
#undef BPLUS_LOAD
    g->GenQueries = (void (APIENTRY *)(GLsizei, GLuint*))SDL_GL_GetProcAddress("glGenQueries");
    g->DeleteQueries = (void (APIENTRY *)(GLsizei, const GLuint*))SDL_GL_GetProcAddress("glDeleteQueries");
    g->BeginQuery = (void (APIENTRY *)(GLenum, GLuint))SDL_GL_GetProcAddress("glBeginQuery");
    g->EndQuery = (void (APIENTRY *)(GLenum))SDL_GL_GetProcAddress("glEndQuery");
    g->GetQueryObjectiv = (void (APIENTRY *)(GLuint, GLenum, GLint*))SDL_GL_GetProcAddress("glGetQueryObjectiv");
    g->GetQueryObjectui64v = (void (APIENTRY *)(GLuint, GLenum, GLuint64*))SDL_GL_GetProcAddress("glGetQueryObjectui64v");
    return 1;
}

static void state_begin(const BplusGl *g, BplusGlState *s)
{
    g->GetIntegerv(GL_CURRENT_PROGRAM, &s->program);
    g->GetIntegerv(GL_ACTIVE_TEXTURE, &s->active_tex);
    g->GetIntegerv(GL_VERTEX_ARRAY_BINDING, &s->vao);
    g->GetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &s->unpack_buf);
    g->GetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &s->ssbo);
    g->GetIntegerv(GL_VIEWPORT, s->viewport);
    g->GetIntegerv(GL_SCISSOR_BOX, s->scissor);
    g->GetBooleanv(GL_COLOR_WRITEMASK, s->mask);
    for (unsigned i = 0; i < 8u; ++i) {
        s->enabled[i] = g->IsEnabled(s_switches[i]);
        g->Disable(s_switches[i]);
    }
    for (unsigned i = 0; i < 8u; ++i)
        g->GetIntegerv(s_stores[i], &s->unpack[i]);
    for (unsigned i = 0; i < 2u; ++i) {
        g->ActiveTexture(GL_TEXTURE0 + i);
        g->GetIntegerv(GL_TEXTURE_BINDING_2D, &s->tex2d[i]);
        g->GetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &s->tex2d_arr[i]);
    }
    g->BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    g->PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (unsigned i = 1u; i < 8u; ++i)
        g->PixelStorei(s_stores[i], 0);
    g->ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

static void state_end(const BplusGl *g, const BplusGlState *s)
{
    g->BindImageTexture(0, 0, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
    g->BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    g->BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
    g->BindBuffer(GL_SHADER_STORAGE_BUFFER, (GLuint)s->ssbo);
    g->BindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)s->unpack_buf);
    g->UseProgram((GLuint)s->program);
    g->BindVertexArray((GLuint)s->vao);
    for (unsigned i = 0; i < 8u; ++i)
        g->PixelStorei(s_stores[i], s->unpack[i]);
    for (unsigned i = 0; i < 2u; ++i) {
        g->ActiveTexture(GL_TEXTURE0 + i);
        g->BindTexture(GL_TEXTURE_2D, (GLuint)s->tex2d[i]);
        g->BindTexture(GL_TEXTURE_2D_ARRAY, (GLuint)s->tex2d_arr[i]);
    }
    g->ActiveTexture((GLenum)s->active_tex);
    g->Viewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
    g->Scissor(s->scissor[0], s->scissor[1], s->scissor[2], s->scissor[3]);
    g->ColorMask(s->mask[0], s->mask[1], s->mask[2], s->mask[3]);
    for (unsigned i = 0; i < 8u; ++i) {
        if (s->enabled[i]) g->Enable(s_switches[i]);
        else g->Disable(s_switches[i]);
    }
}

static GLuint compile_shader(const BplusGl *g, GLenum type, const char *src)
{
    GLuint sh = g->CreateShader(type);
    if (!sh) return 0;
    g->ShaderSource(sh, 1, &src, NULL);
    g->CompileShader(sh);
    GLint ok = 0;
    g->GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        g->GetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "[BPLUS_GL] shader compile failed: %s\n", log);
        g->DeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint link_compute_prog(const BplusGl *g, const char *src)
{
    GLuint cs = compile_shader(g, GL_COMPUTE_SHADER, src);
    if (!cs) return 0;
    GLuint p = g->CreateProgram();
    if (p) {
        g->AttachShader(p, cs);
        g->LinkProgram(p);
        GLint ok = 0;
        g->GetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048] = {0};
            g->GetProgramInfoLog(p, sizeof(log), NULL, log);
            fprintf(stderr, "[BPLUS_GL] compute link failed: %s\n", log);
            g->DeleteProgram(p);
            p = 0;
        }
    }
    g->DeleteShader(cs);
    return p;
}

static GLuint link_upscale_prog(const BplusGl *g, const char *vs_src, const char *fs_src)
{
    GLuint vs = compile_shader(g, GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(g, GL_FRAGMENT_SHADER, fs_src);
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
                char log[2048] = {0};
                g->GetProgramInfoLog(p, sizeof(log), NULL, log);
                fprintf(stderr, "[BPLUS_GL] upscale link failed: %s\n", log);
                g->DeleteProgram(p);
                p = 0;
            }
        }
    }
    if (vs) g->DeleteShader(vs);
    if (fs) g->DeleteShader(fs);
    return p;
}

static void init_array_texture(const BplusGl *g, GLuint *tex, GLenum fmt,
                               unsigned w, unsigned h, unsigned layers)
{
    if (*tex) g->DeleteTextures(1, tex);
    *tex = 0;
    g->GenTextures(1, tex);
    g->ActiveTexture(GL_TEXTURE0);
    g->BindTexture(GL_TEXTURE_2D_ARRAY, *tex);
    g->TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g->TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g->TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g->TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g->TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
    g->TexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
    g->TexStorage3D(GL_TEXTURE_2D_ARRAY, 1, fmt, (GLsizei)w, (GLsizei)h, (GLsizei)layers);
}

static int ensure_size(EcBplusGl *ctx, unsigned w, unsigned h)
{
    unsigned scale = ctx->model.scale;
    unsigned out_w = w * scale;
    unsigned out_h = h * scale;
    if (ctx->output && ctx->in_w == w && ctx->in_h == h)
        return 1;
    if (w == 0 || h == 0 || out_w > ctx->max_texture || out_h > ctx->max_texture)
        return 0;

    SDL_Texture *next = SDL_CreateTexture(
        ctx->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
        (int)out_w, (int)out_h);
    if (!next)
        return 0;
    SDL_SetTextureBlendMode(next, SDL_BLENDMODE_NONE);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(next, SDL_ScaleModeNearest);
#endif
    if (ctx->output) SDL_DestroyTexture(ctx->output);
    ctx->output = next;

    unsigned g_c = ctx->model.channels / 4u;
    unsigned g_tail = (4u * scale * scale) / 4u;
    init_array_texture(&ctx->gl, &ctx->tex_in, GL_RGBA8, w, h, 1u);
    for (unsigned i = 0; i < 3u; ++i)
        init_array_texture(&ctx->gl, &ctx->tex_feat[i], GL_RGBA16F, w, h, g_c);
    init_array_texture(&ctx->gl, &ctx->tex_tail, GL_RGBA16F, w, h, g_tail);

    ctx->in_w = w;
    ctx->in_h = h;
    ctx->out_w = out_w;
    ctx->out_h = out_h;
    ctx->metrics.input_width = w;
    ctx->metrics.input_height = h;
    ctx->metrics.output_width = out_w;
    ctx->metrics.output_height = out_h;
    ctx->metrics.scale = scale;
    return 1;
}

static void record_sample_ms(EcBplusGl *ctx, double ms)
{
    ctx->metrics.last_gpu_ms = ms;
    ctx->metrics.timed_samples++;
    if (ctx->samples_ms && ctx->sample_count < BPLUS_MAX_SAMPLES)
        ctx->samples_ms[ctx->sample_count++] = ms;
}

static void collect_query(EcBplusGl *ctx, unsigned slot)
{
    if (!ctx->query_pending[slot]) return;
    GLint ready = 0;
    ctx->gl.GetQueryObjectiv(ctx->queries[slot], GL_QUERY_RESULT_AVAILABLE, &ready);
    if (!ready) return;
    GLuint64 ns = 0;
    ctx->gl.GetQueryObjectui64v(ctx->queries[slot], GL_QUERY_RESULT, &ns);
    ctx->query_pending[slot] = 0;
    if (ns > 0)
        record_sample_ms(ctx, (double)ns / 1e6);
}

static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static void finalize_stats(EcBplusGl *ctx, EcBplusMetrics *out)
{
    if (!ctx->samples_ms || ctx->sample_count == 0) {
        out->p50_gpu_ms = 0.0;
        out->p99_gpu_ms = 0.0;
        out->mean_gpu_ms = 0.0;
        return;
    }
    double *sorted = (double *)malloc(sizeof(double) * ctx->sample_count);
    if (!sorted) return;
    memcpy(sorted, ctx->samples_ms, sizeof(double) * ctx->sample_count);
    qsort(sorted, ctx->sample_count, sizeof(double), cmp_double);

    unsigned n = ctx->sample_count;
    out->p50_gpu_ms = (n & 1u) ? sorted[n / 2u] : 0.5 * (sorted[n / 2u - 1u] + sorted[n / 2u]);
    unsigned p99_idx = (unsigned)(0.99 * (double)(n - 1u));
    if (p99_idx >= n) p99_idx = n - 1u;
    out->p99_gpu_ms = sorted[p99_idx];
    double sum = 0.0;
    for (unsigned i = 0; i < n; ++i) sum += sorted[i];
    out->mean_gpu_ms = sum / (double)n;
    free(sorted);
}

static void dispatch_conv(const EcBplusGl *ctx, unsigned layer_idx,
                          GLuint in_tex, GLuint out_tex, GLuint res_tex,
                          int act, int add_res)
{
    const LayerDesc *L = &ctx->model.layers[layer_idx];
    ctx->gl.ActiveTexture(GL_TEXTURE0);
    ctx->gl.BindTexture(GL_TEXTURE_2D_ARRAY, in_tex);
    ctx->gl.ActiveTexture(GL_TEXTURE1);
    ctx->gl.BindTexture(GL_TEXTURE_2D_ARRAY, res_tex ? res_tex : in_tex);
    ctx->gl.BindImageTexture(0, out_tex, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

    ctx->gl.Uniform1i(ctx->u_tex_in, 0);
    ctx->gl.Uniform1i(ctx->u_tex_res, 1);
    ctx->gl.Uniform1i(ctx->u_width, (GLint)ctx->in_w);
    ctx->gl.Uniform1i(ctx->u_height, (GLint)ctx->in_h);
    ctx->gl.Uniform1i(ctx->u_g_in, (GLint)L->g_in);
    ctx->gl.Uniform1i(ctx->u_w_offset, (GLint)L->w_offset);
    ctx->gl.Uniform1i(ctx->u_b_offset, (GLint)L->b_offset);
    ctx->gl.Uniform1i(ctx->u_act, act);
    ctx->gl.Uniform1i(ctx->u_add_res, add_res);
    ctx->gl.Uniform1f(ctx->u_slope, ctx->model.slope);

    GLuint wg_x = (GLuint)((ctx->in_w + 7u) / 8u);
    GLuint wg_y = (GLuint)((ctx->in_h + 7u) / 8u);
    ctx->gl.DispatchCompute(wg_x, wg_y, (GLuint)L->g_out);
    ctx->gl.MemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

EcBplusGl *ec_bplus_gl_create(SDL_Renderer *renderer, SDL_Texture *activation,
                              const char *weights_path, double budget_ms)
{
    SDL_RendererInfo info = {0};
    if (!renderer || !activation || SDL_GetRendererInfo(renderer, &info) != 0 ||
        !info.name || strcmp(info.name, "opengl") != 0 ||
        !(info.flags & SDL_RENDERER_TARGETTEXTURE)) {
        fprintf(stderr, "[BPLUS_GL] requires SDL opengl renderer with target texture support\n");
        return NULL;
    }

    if (SDL_GL_BindTexture(activation, NULL, NULL) != 0)
        return NULL;
    SDL_GL_UnbindTexture(activation);

    EcBplusGl *ctx = (EcBplusGl *)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->renderer = renderer;
    ctx->context = SDL_GL_GetCurrentContext();
    ctx->budget_ms = budget_ms;

    if (!load_bplus_gl(&ctx->gl)) {
        free(ctx);
        return NULL;
    }

    const char *ver_str = (const char *)ctx->gl.GetString(GL_VERSION);
    const char *gl_ren = (const char *)ctx->gl.GetString(GL_RENDERER);
    int major = 0, minor = 0;
    if (!ver_str || sscanf(ver_str, "%d.%d", &major, &minor) != 2 ||
        major < 4 || (major == 4 && minor < 3)) {
        fprintf(stderr, "[BPLUS_GL] OpenGL 4.3+ required (got %s); retaining fallback\n",
                ver_str ? ver_str : "unknown");
        free(ctx);
        return NULL;
    }

    const char *allow_sw = getenv("EC_BPLUS_ALLOW_SOFTWARE");
    if (gl_ren && (strstr(gl_ren, "llvmpipe") || strstr(gl_ren, "softpipe")) &&
        (!allow_sw || strcmp(allow_sw, "1") != 0)) {
        fprintf(stderr,
                "[BPLUS_GL] software OpenGL (%s) detected; set GALLIUM_DRIVER=d3d12 for GPU "
                "or EC_BPLUS_ALLOW_SOFTWARE=1 — retaining native fallback\n",
                gl_ren);
        free(ctx);
        return NULL;
    }

    GLint max_tex = 0;
    ctx->gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
    if (max_tex < 2048) {
        fprintf(stderr, "[BPLUS_GL] GL_MAX_TEXTURE_SIZE=%d too small\n", max_tex);
        free(ctx);
        return NULL;
    }
    ctx->max_texture = (unsigned)max_tex;

    if (!load_ecbp_model(weights_path, &ctx->model)) {
        free(ctx);
        return NULL;
    }

    BplusGlState st;
    state_begin(&ctx->gl, &st);

    ctx->conv_prog = link_compute_prog(&ctx->gl, s_conv3x3_cs);
    ctx->up_prog = link_upscale_prog(&ctx->gl, s_upscale_vs, s_upscale_fs);
    if (ctx->conv_prog && ctx->up_prog) {
        ctx->gl.GenBuffers(1, &ctx->ssbo_w);
        ctx->gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, ctx->ssbo_w);
        ctx->gl.BufferData(GL_SHADER_STORAGE_BUFFER,
                           (GLsizeiptr)(ctx->model.total_mat4 * 16u * sizeof(float)),
                           ctx->model.weights_mat4, GL_STATIC_DRAW);

        ctx->gl.GenBuffers(1, &ctx->ssbo_b);
        ctx->gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, ctx->ssbo_b);
        ctx->gl.BufferData(GL_SHADER_STORAGE_BUFFER,
                           (GLsizeiptr)(ctx->model.total_vec4 * 4u * sizeof(float)),
                           ctx->model.biases_vec4, GL_STATIC_DRAW);
        ctx->gl.BindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        ctx->gl.GenVertexArrays(1, &ctx->vao);

        ctx->u_width = ctx->gl.GetUniformLocation(ctx->conv_prog, "u_width");
        ctx->u_height = ctx->gl.GetUniformLocation(ctx->conv_prog, "u_height");
        ctx->u_g_in = ctx->gl.GetUniformLocation(ctx->conv_prog, "u_g_in");
        ctx->u_w_offset = ctx->gl.GetUniformLocation(ctx->conv_prog, "u_w_offset");
        ctx->u_b_offset = ctx->gl.GetUniformLocation(ctx->conv_prog, "u_b_offset");
        ctx->u_act = ctx->gl.GetUniformLocation(ctx->conv_prog, "u_act");
        ctx->u_add_res = ctx->gl.GetUniformLocation(ctx->conv_prog, "u_add_res");
        ctx->u_slope = ctx->gl.GetUniformLocation(ctx->conv_prog, "u_slope");
        ctx->u_tex_in = ctx->gl.GetUniformLocation(ctx->conv_prog, "u_in");
        ctx->u_tex_res = ctx->gl.GetUniformLocation(ctx->conv_prog, "u_res");

        ctx->up_u_in = ctx->gl.GetUniformLocation(ctx->up_prog, "u_in");
        ctx->up_u_tail = ctx->gl.GetUniformLocation(ctx->up_prog, "u_tail");
        ctx->up_u_in_size = ctx->gl.GetUniformLocation(ctx->up_prog, "u_in_size");
        ctx->up_u_scale = ctx->gl.GetUniformLocation(ctx->up_prog, "u_scale");
    }

    state_end(&ctx->gl, &st);

    if (!ctx->conv_prog || !ctx->up_prog || !ctx->ssbo_w || !ctx->ssbo_b) {
        ec_bplus_gl_destroy(ctx);
        return NULL;
    }

    ctx->time_elapsed_ok = ctx->gl.GenQueries && ctx->gl.DeleteQueries &&
        ctx->gl.BeginQuery && ctx->gl.EndQuery &&
        ctx->gl.GetQueryObjectiv && ctx->gl.GetQueryObjectui64v;
    if (ctx->time_elapsed_ok)
        ctx->gl.GenQueries(BPLUS_QUERY_SLOTS, ctx->queries);
    ctx->metrics.time_elapsed_available = ctx->time_elapsed_ok;
    ctx->metrics.scale = ctx->model.scale;

    const char *sync_env = getenv("EC_BPLUS_SYNC_TIMING");
    ctx->sync_timing = (sync_env && strcmp(sync_env, "1") == 0);
    ctx->samples_ms = (double *)calloc(BPLUS_MAX_SAMPLES, sizeof(double));

    fprintf(stderr,
            "[BPLUS_GL] renderer=%s | GL=%s | ch=%u blocks=%u scale=%ux | budget=%.2f ms sync=%d\n",
            gl_ren ? gl_ren : "unknown", ver_str,
            ctx->model.channels, ctx->model.num_blocks, ctx->model.scale,
            ctx->budget_ms, ctx->sync_timing);
    return ctx;
}

void ec_bplus_gl_destroy(EcBplusGl *ctx)
{
    if (!ctx) return;

    if (ctx->gl.GetError && SDL_GL_GetCurrentContext() == ctx->context) {
        if (ctx->time_elapsed_ok) {
            for (unsigned i = 0; i < BPLUS_QUERY_SLOTS; ++i)
                collect_query(ctx, i);
            ctx->gl.DeleteQueries(BPLUS_QUERY_SLOTS, ctx->queries);
        }
        if (ctx->tex_in) ctx->gl.DeleteTextures(1, &ctx->tex_in);
        for (unsigned i = 0; i < 3u; ++i)
            if (ctx->tex_feat[i]) ctx->gl.DeleteTextures(1, &ctx->tex_feat[i]);
        if (ctx->tex_tail) ctx->gl.DeleteTextures(1, &ctx->tex_tail);
        if (ctx->ssbo_w) ctx->gl.DeleteBuffers(1, &ctx->ssbo_w);
        if (ctx->ssbo_b) ctx->gl.DeleteBuffers(1, &ctx->ssbo_b);
        if (ctx->vao) ctx->gl.DeleteVertexArrays(1, &ctx->vao);
        if (ctx->conv_prog) ctx->gl.DeleteProgram(ctx->conv_prog);
        if (ctx->up_prog) ctx->gl.DeleteProgram(ctx->up_prog);
    }

    EcBplusMetrics m = ctx->metrics;
    finalize_stats(ctx, &m);

    const char *timings_path = getenv("EC_BPLUS_TIMINGS");
    if (timings_path && *timings_path && ctx->samples_ms) {
        FILE *fp = fopen(timings_path, "wb");
        if (fp) {
            fprintf(fp, "sample_idx,gpu_ms\n");
            for (unsigned i = 0; i < ctx->sample_count; ++i)
                fprintf(fp, "%u,%.6f\n", i, ctx->samples_ms[i]);
            fclose(fp);
        }
    }

    fprintf(stderr,
            "[BPLUS_GL] frames=%llu fallback=%llu samples=%u p50_ms=%.4f p99_ms=%.4f mean_ms=%.4f output=%ux%u\n",
            (unsigned long long)m.frames, (unsigned long long)m.fallback_frames,
            ctx->sample_count, m.p50_gpu_ms, m.p99_gpu_ms, m.mean_gpu_ms,
            ctx->out_w, ctx->out_h);

    if (ctx->output) SDL_DestroyTexture(ctx->output);
    free(ctx->samples_ms);
    free_ecbp_model(&ctx->model);
    free(ctx);
}

int ec_bplus_gl_readback(EcBplusGl *ctx, void *argb, size_t pitch_bytes)
{
    if (!ctx || !ctx->output || !argb ||
        pitch_bytes < (size_t)ctx->out_w * 4u || pitch_bytes > INT_MAX)
        return 0;
    SDL_Texture *old = SDL_GetRenderTarget(ctx->renderer);
    if (SDL_SetRenderTarget(ctx->renderer, ctx->output) != 0)
        return 0;
    int ok = (SDL_RenderReadPixels(ctx->renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                   argb, (int)pitch_bytes) == 0);
    if (SDL_SetRenderTarget(ctx->renderer, old) != 0)
        ok = 0;
    return ok;
}

void ec_bplus_gl_metrics(const EcBplusGl *ctx, EcBplusMetrics *out)
{
    if (!out) return;
    if (!ctx) {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = ctx->metrics;
    finalize_stats((EcBplusGl *)ctx, out);
}

static void maybe_capture_diagnostic(EcBplusGl *ctx, const uint32_t *argb,
                                     int width, int height, int stride_px)
{
    const char *cap_out = getenv("EC_BPLUS_CAPTURE");
    if (!cap_out || !*cap_out) return;

    uint64_t target_frame = 1u;
    const char *at = getenv("EC_BPLUS_CAPTURE_AT");
    if (at && *at) {
        char *end = NULL;
        unsigned long long v = strtoull(at, &end, 10);
        if (!end || *end) return;
        target_frame = (uint64_t)v;
    }
    if (ctx->metrics.frames != target_frame)
        return;

    const char *cap_in = getenv("EC_BPLUS_CAPTURE_IN");
    if (cap_in && *cap_in)
        (void)png_write_argb(cap_in, argb, width, height, stride_px);

    uint32_t *gpu = (uint32_t *)malloc((size_t)ctx->out_w * ctx->out_h * sizeof(uint32_t));
    if (gpu) {
        if (ec_bplus_gl_readback(ctx, gpu, (size_t)ctx->out_w * sizeof(uint32_t))) {
            if (png_write_argb(cap_out, gpu, (int)ctx->out_w, (int)ctx->out_h, (int)ctx->out_w) == 0) {
                fprintf(stderr, "[BPLUS_CAPTURE] frame=%llu wrote %s (%ux%u)\n",
                        (unsigned long long)ctx->metrics.frames, cap_out, ctx->out_w, ctx->out_h);
            }
        }
        free(gpu);
    }
}

SDL_Texture *ec_bplus_gl_process(EcBplusGl *ctx, const uint32_t *argb,
                                 int width, int height, int stride_px)
{
    if (!ctx || ctx->failed || !argb || width <= 0 || height <= 0 || stride_px < width)
        return NULL;
    if (SDL_GL_GetCurrentContext() != ctx->context) {
        ctx->failed = 1;
        fprintf(stderr, "[BPLUS_GL] context changed; retaining native fallback\n");
        return NULL;
    }

    if (ctx->budget_ms > 0.0 && ctx->metrics.frames >= 10u &&
        ctx->metrics.last_gpu_ms > ctx->budget_ms) {
        ctx->metrics.fallback_frames++;
        return NULL;
    }

    if (!ensure_size(ctx, (unsigned)width, (unsigned)height)) {
        ctx->metrics.fallback_frames++;
        return NULL;
    }

    unsigned slot = ctx->query_cursor++ % BPLUS_QUERY_SLOTS;
    int use_query = !ctx->sync_timing && ctx->time_elapsed_ok;
    if (use_query) {
        collect_query(ctx, slot);
        if (ctx->query_pending[slot])
            use_query = 0;
    }

    SDL_Texture *old_target = SDL_GetRenderTarget(ctx->renderer);
    if (SDL_SetRenderTarget(ctx->renderer, ctx->output) != 0) {
        ctx->metrics.fallback_frames++;
        return NULL;
    }
    if (SDL_RenderFlush(ctx->renderer) != 0) {
        SDL_SetRenderTarget(ctx->renderer, old_target);
        ctx->metrics.fallback_frames++;
        return NULL;
    }

    BplusGlState st;
    state_begin(&ctx->gl, &st);

    Uint64 t0 = 0;
    if (ctx->sync_timing) {
        ctx->gl.Finish();
        t0 = SDL_GetPerformanceCounter();
    } else if (use_query) {
        ctx->gl.BeginQuery(GL_TIME_ELAPSED, ctx->queries[slot]);
    }

    /* Upload native ARGB8888 (little-endian B,G,R,A bytes -> GL_BGRA) into layer 0 */
    ctx->gl.ActiveTexture(GL_TEXTURE0);
    ctx->gl.BindTexture(GL_TEXTURE_2D_ARRAY, ctx->tex_in);
    ctx->gl.PixelStorei(GL_UNPACK_ROW_LENGTH, stride_px);
    ctx->gl.TexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0,
                           width, height, 1, GL_BGRA, GL_UNSIGNED_BYTE, argb);
    ctx->gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    /* Compute passes: head -> N residual blocks -> tail */
    ctx->gl.UseProgram(ctx->conv_prog);
    ctx->gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ctx->ssbo_w);
    ctx->gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ctx->ssbo_b);

    dispatch_conv(ctx, 0u, ctx->tex_in, ctx->tex_feat[0], 0u, 1, 0);

    unsigned res_idx = 0u;
    for (unsigned b = 0u; b < ctx->model.num_blocks; ++b) {
        unsigned tmp_idx = (res_idx + 1u) % 3u;
        unsigned nxt_idx = (res_idx + 2u) % 3u;
        dispatch_conv(ctx, 1u + b * 2u, ctx->tex_feat[res_idx], ctx->tex_feat[tmp_idx], 0u, 1, 0);
        dispatch_conv(ctx, 1u + b * 2u + 1u, ctx->tex_feat[tmp_idx], ctx->tex_feat[nxt_idx],
                      ctx->tex_feat[res_idx], 0, 1);
        res_idx = nxt_idx;
    }

    dispatch_conv(ctx, 1u + 2u * ctx->model.num_blocks, ctx->tex_feat[res_idx], ctx->tex_tail, 0u, 0, 0);

    /* Final fullscreen PixelShuffle + exact PyTorch bilinear residual composite */
    ctx->gl.UseProgram(ctx->up_prog);
    ctx->gl.BindVertexArray(ctx->vao);
    ctx->gl.ActiveTexture(GL_TEXTURE0);
    ctx->gl.BindTexture(GL_TEXTURE_2D_ARRAY, ctx->tex_in);
    ctx->gl.ActiveTexture(GL_TEXTURE1);
    ctx->gl.BindTexture(GL_TEXTURE_2D_ARRAY, ctx->tex_tail);
    ctx->gl.Uniform1i(ctx->up_u_in, 0);
    ctx->gl.Uniform1i(ctx->up_u_tail, 1);
    ctx->gl.Uniform2i(ctx->up_u_in_size, width, height);
    ctx->gl.Uniform1i(ctx->up_u_scale, (GLint)ctx->model.scale);
    ctx->gl.Viewport(0, 0, (GLsizei)ctx->out_w, (GLsizei)ctx->out_h);
    ctx->gl.DrawArrays(GL_TRIANGLES, 0, 3);

    GLenum err = ctx->gl.GetError();

    if (ctx->sync_timing) {
        ctx->gl.Finish();
        Uint64 t1 = SDL_GetPerformanceCounter();
        double ms = (double)(t1 - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
        record_sample_ms(ctx, ms);
    } else if (use_query) {
        ctx->gl.EndQuery(GL_TIME_ELAPSED);
        ctx->query_pending[slot] = 1u;
    }

    state_end(&ctx->gl, &st);
    if (SDL_SetRenderTarget(ctx->renderer, old_target) != 0)
        err = GL_INVALID_OPERATION;

    if (err != GL_NO_ERROR) {
        fprintf(stderr, "[BPLUS_GL] GL error=0x%x; retaining native fallback\n", (unsigned)err);
        ctx->failed = 1;
        ctx->metrics.fallback_frames++;
        return NULL;
    }

    ctx->metrics.frames++;
    maybe_capture_diagnostic(ctx, argb, width, height, stride_px);
    return ctx->output;
}

#else
struct EcBplusGl { int unused; };
EcBplusGl *ec_bplus_gl_create(SDL_Renderer *r, SDL_Texture *t, const char *p, double b)
{ (void)r; (void)t; (void)p; (void)b; return NULL; }
void ec_bplus_gl_destroy(EcBplusGl *ctx) { (void)ctx; }
SDL_Texture *ec_bplus_gl_process(EcBplusGl *ctx, const uint32_t *a, int w, int h, int s)
{ (void)ctx; (void)a; (void)w; (void)h; (void)s; return NULL; }
int ec_bplus_gl_readback(EcBplusGl *ctx, void *a, size_t p)
{ (void)ctx; (void)a; (void)p; return 0; }
void ec_bplus_gl_metrics(const EcBplusGl *ctx, EcBplusMetrics *out)
{ (void)ctx; if (out) memset(out, 0, sizeof(*out)); }
#endif
