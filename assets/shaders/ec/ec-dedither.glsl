/*
    ec-dedither - Eternal Champions project shader
    Faithful fragment-shader port of the CBOD / SGENPT de-dither used by the
    neural stage (compute shader s_dedither_cs in neural/infer/ec_bplus_gl.c,
    mirrored by pre_dedither_torch in neural/train_espcn.py).
    Native resolution pass (scale 1.0, filter_linear = false).
    Neighbours are fetched at texel centres and clamped to InputSize.
    Same license as the project.
*/

#pragma parameter EC_DEDITHER_MODE "De-dither (0=off 1=CBOD 2=CBOD+SGENPT)" 2.0 0.0 2.0 1.0

#if defined(VERTEX)

#if __VERSION__ >= 130
#define COMPAT_VARYING out
#define COMPAT_ATTRIBUTE in
#else
#define COMPAT_VARYING varying
#define COMPAT_ATTRIBUTE attribute
#endif

#ifdef GL_ES
#define COMPAT_PRECISION mediump
#else
#define COMPAT_PRECISION
#endif

COMPAT_ATTRIBUTE vec4 VertexCoord;
COMPAT_ATTRIBUTE vec4 COLOR;
COMPAT_ATTRIBUTE vec4 TexCoord;
COMPAT_VARYING vec4 COL0;
COMPAT_VARYING vec4 TEX0;

uniform mat4 MVPMatrix;
uniform COMPAT_PRECISION int FrameDirection;
uniform COMPAT_PRECISION int FrameCount;
uniform COMPAT_PRECISION vec2 OutputSize;
uniform COMPAT_PRECISION vec2 TextureSize;
uniform COMPAT_PRECISION vec2 InputSize;

void main()
{
    gl_Position = MVPMatrix * VertexCoord;
    COL0 = COLOR;
    TEX0.xy = TexCoord.xy;
}

#elif defined(FRAGMENT)

#if __VERSION__ >= 130
#define COMPAT_VARYING in
#define COMPAT_TEXTURE texture
out vec4 FragColor;
#else
#define COMPAT_VARYING varying
#define FragColor gl_FragColor
#define COMPAT_TEXTURE texture2D
#endif

#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#define COMPAT_PRECISION mediump
#else
#define COMPAT_PRECISION
#endif

uniform COMPAT_PRECISION int FrameDirection;
uniform COMPAT_PRECISION int FrameCount;
uniform COMPAT_PRECISION vec2 OutputSize;
uniform COMPAT_PRECISION vec2 TextureSize;
uniform COMPAT_PRECISION vec2 InputSize;
uniform sampler2D Texture;
COMPAT_VARYING vec4 TEX0;

#define vTexCoord TEX0.xy

#ifdef PARAMETER_UNIFORM
uniform COMPAT_PRECISION float EC_DEDITHER_MODE;
#else
#define EC_DEDITHER_MODE 2.0
#endif

// integer texel position of the current fragment (origin = top-left of the image)
vec2 g_base;

vec3 fetch_clamped(float dx, float dy)
{
    vec2 p = clamp(g_base + vec2(dx, dy), vec2(0.0), InputSize - vec2(1.0));
    return COMPAT_TEXTURE(Texture, (p + vec2(0.5)) / TextureSize).rgb;
}

float cdist(vec3 a, vec3 b)
{
    vec3 d = abs(a - b);
    return max(d.r, max(d.g, d.b));
}
bool ceq(vec3 a, vec3 b)  { return cdist(a, b) <= 0.035; }
bool cneq(vec3 a, vec3 b) { return cdist(a, b) >  0.045; }

void main()
{
    g_base = floor(vTexCoord * TextureSize);
    vec4 c4 = COMPAT_TEXTURE(Texture, (g_base + vec2(0.5)) / TextureSize);

    if (EC_DEDITHER_MODE < 0.5) { FragColor = c4; return; }

    vec3 C  = c4.rgb;
    vec3 L  = fetch_clamped(-1.0,  0.0);
    vec3 R  = fetch_clamped( 1.0,  0.0);
    vec3 U  = fetch_clamped( 0.0, -1.0);
    vec3 D  = fetch_clamped( 0.0,  1.0);
    vec3 UL = fetch_clamped(-1.0, -1.0);
    vec3 UR = fetch_clamped( 1.0, -1.0);
    vec3 DL = fetch_clamped(-1.0,  1.0);
    vec3 DR = fetch_clamped( 1.0,  1.0);

    vec3 out_rgb = C;
    bool blended = false;

    // 1. 2x2 checkerboard (CBOD)
    bool q_ul = ceq(C, UL) && ceq(L, U) && cneq(C, L);
    bool q_ur = ceq(C, UR) && ceq(R, U) && cneq(C, R);
    bool q_dl = ceq(C, DL) && ceq(L, D) && cneq(C, L);
    bool q_dr = ceq(C, DR) && ceq(R, D) && cneq(C, R);
    int q_cnt = int(q_ul) + int(q_ur) + int(q_dl) + int(q_dr);
    if (q_cnt >= 2) {
        vec3 acc = vec3(0.0);
        if (q_ul) acc += 0.5 * (L + U);
        if (q_ur) acc += 0.5 * (R + U);
        if (q_dl) acc += 0.5 * (L + D);
        if (q_dr) acc += 0.5 * (R + D);
        out_rgb = 0.5 * C + 0.5 * (acc / float(q_cnt));
        blended = true;
    }

    // 2. textured-background checkerboard
    if (!blended) {
        bool eq_ul = ceq(C, UL);
        bool eq_ur = ceq(C, UR);
        bool eq_dl = ceq(C, DL);
        bool eq_dr = ceq(C, DR);
        bool adj_diag = (eq_ul && eq_ur) || (eq_dl && eq_dr) ||
                        (eq_ul && eq_dl) || (eq_ur && eq_dr);
        if (adj_diag && cneq(C, L) && cneq(C, R) && cneq(C, U) && cneq(C, D)) {
            vec3 acc = vec3(0.0);
            float wsum = 0.0;
            if (eq_ul && eq_ur) { acc += U; wsum += 1.0; }
            if (eq_dl && eq_dr) { acc += D; wsum += 1.0; }
            if (eq_ul && eq_dl) { acc += L; wsum += 1.0; }
            if (eq_ur && eq_dr) { acc += R; wsum += 1.0; }
            out_rgb = 0.5 * C + 0.5 * (acc / wsum);
            blended = true;
        } else if (cneq(C, L) && ceq(L, R) && ceq(U, D) && ceq(L, U)) {
            int d_not_b = int(cneq(UL, L)) + int(cneq(UR, L)) +
                          int(cneq(DL, L)) + int(cneq(DR, L));
            if (d_not_b >= 2) {
                vec3 B = 0.25 * (L + R + U + D);
                out_rgb = 0.5 * C + 0.5 * B;
                blended = true;
            }
        }
    }

    // 3. vertical-line pseudo-transparency (SGENPT-MIX VL), mode >= 2
    if (!blended && EC_DEDITHER_MODE > 1.5) {
        if (ceq(L, R) && cneq(C, L)) {
            bool vc = ceq(U, C) || ceq(D, C);
            bool vl = ceq(UL, L) || ceq(DL, L);
            bool vr = ceq(UR, R) || ceq(DR, R);
            if (vc && vl && vr) {
                vec3 L2 = fetch_clamped(-2.0, 0.0);
                vec3 R2 = fetch_clamped( 2.0, 0.0);
                if (ceq(C, L2) || ceq(C, R2)) {
                    out_rgb = 0.5 * C + 0.25 * (L + R);
                }
            }
        }
    }

    FragColor = vec4(out_rgb, c4.a);
}
#endif
