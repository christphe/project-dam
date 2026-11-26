/* D973: deko3d shader pack generator (host tool, built by cmake/SwitchPort.cmake).
 *
 *   dkshgen glsl <combiners.txt> <outdir>
 *       writes cc_<id0>_<id1>.vert.glsl / .frag.glsl for every pair of the list
 *       and of fx/combiners.txt next to it (deko3d-flavoured GLSL 460), and
 *       <outdir>/list.txt naming them.
 *
 * Two lists, sorted by what draws: combiners.txt holds the game's combiners
 * (the census, tools_pc/cc_census_merge.py), fx/combiners.txt the port's
 * effects (ids with a PORTFX_* kind in SHADER_OPT_FX_MASK). An effect id gets
 * fx/common.glsl and its family's file (fx/bullets.glsl, bursts.glsl,
 * impacts.glsl) and calls its effect's function: the uber shader is only the
 * fallback for the game's combiners and runs no effect. An id in the wrong
 * list, or an effect kind with no shader here, is an error.
 *   dkshgen pack <outfile> <listfile>
 *       listfile lines: "<id0 hex> <id1 hex> <vert.dksh|-> <frag.dksh|->";
 *       writes the pack described in port/fast3d/gfx_dk_layout.h.
 *
 * The combiner GLSL is a deliberate, separate copy of gfx_opengl.cpp's
 * gfx_opengl_gen_shader_src (the GL backend is not touched: PC goldens depend
 * on it), with the Switch GL defaults baked in: perspective-correct shade (D986) and
 * per-vertex fog (D540/D543), depth clamp (no z hack), three-point filtering
 * always compiled and selected by the Draw block's flags (GL compiles it only
 * while the three-point mode is on; the backend sends the flags as zero
 * otherwise). Varyings and attributes meet by location (DK_ATTR_*). */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <algorithm>

#include "gfx_cc.h"
#include "gfx_dk_layout.h"

static std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
static std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

#define RAND_NOISE "((random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + 1.0) / 2.0)"

/* gfx_opengl.cpp shader_item_to_str, verbatim. "" = no GLSL for that source
 * (GL would fail to compile such a combiner too): the pair is skipped. */
static const char* shader_item_to_str(uint32_t item, bool with_alpha, bool only_alpha, bool inputs_have_alpha,
                                      bool hint_single_element) {
    if (!only_alpha) {
        switch (item) {
            case SHADER_0: return with_alpha ? "vec4(0.0, 0.0, 0.0, 0.0)" : "vec3(0.0, 0.0, 0.0)";
            case SHADER_1: return with_alpha ? "vec4(1.0, 1.0, 1.0, 1.0)" : "vec3(1.0, 1.0, 1.0)";
            case SHADER_INPUT_1: return with_alpha || !inputs_have_alpha ? "cInput1" : "cInput1.rgb";
            case SHADER_INPUT_2: return with_alpha || !inputs_have_alpha ? "cInput2" : "cInput2.rgb";
            case SHADER_INPUT_3: return with_alpha || !inputs_have_alpha ? "cInput3" : "cInput3.rgb";
            case SHADER_INPUT_4: return with_alpha || !inputs_have_alpha ? "cInput4" : "cInput4.rgb";
            case SHADER_TEXEL0: return with_alpha ? "texVal0" : "texVal0.rgb";
            case SHADER_TEXEL0A:
                return hint_single_element ? "texVal0.a"
                                           : (with_alpha ? "vec4(texVal0.a, texVal0.a, texVal0.a, texVal0.a)"
                                                         : "vec3(texVal0.a, texVal0.a, texVal0.a)");
            case SHADER_TEXEL1A:
                return hint_single_element ? "texVal1.a"
                                           : (with_alpha ? "vec4(texVal1.a, texVal1.a, texVal1.a, texVal1.a)"
                                                         : "vec3(texVal1.a, texVal1.a, texVal1.a)");
            case SHADER_TEXEL1: return with_alpha ? "texVal1" : "texVal1.rgb";
            case SHADER_COMBINED: return with_alpha ? "texel" : "texel.rgb";
            case SHADER_NOISE:
                return with_alpha ? "vec4(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")"
                                  : "vec3(" RAND_NOISE ", " RAND_NOISE ", " RAND_NOISE ")";
        }
    } else {
        switch (item) {
            case SHADER_0: return "0.0";
            case SHADER_1: return "1.0";
            case SHADER_INPUT_1: return "cInput1.a";
            case SHADER_INPUT_2: return "cInput2.a";
            case SHADER_INPUT_3: return "cInput3.a";
            case SHADER_INPUT_4: return "cInput4.a";
            case SHADER_TEXEL0: return "texVal0.a";
            case SHADER_TEXEL0A: return "texVal0.a";
            case SHADER_TEXEL1A: return "texVal1.a";
            case SHADER_TEXEL1: return "texVal1.a";
            case SHADER_COMBINED: return "texel.a";
            case SHADER_NOISE: return RAND_NOISE;
        }
    }
    return "";
}

static bool s_bad;

static void item(std::string& s, uint32_t it, bool with_alpha, bool only_alpha, bool inputs_have_alpha, bool hint) {
    const char* p = shader_item_to_str(it, with_alpha, only_alpha, inputs_have_alpha, hint);
    if (p[0] == '\0') s_bad = true;
    s += p;
}

static void append_formula(std::string& s, uint8_t c[2][4], bool do_single, bool do_multiply, bool do_mix,
                           bool with_alpha, bool only_alpha, bool opt_alpha) {
    if (do_single) {
        item(s, c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false);
    } else if (do_multiply) {
        item(s, c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false);
        s += " * ";
        item(s, c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true);
    } else if (do_mix) {
        s += "mix(";
        item(s, c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false);
        s += ", ";
        item(s, c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false);
        s += ", ";
        item(s, c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true);
        s += ")";
    } else {
        s += "(";
        item(s, c[only_alpha][0], with_alpha, only_alpha, opt_alpha, false);
        s += " - ";
        item(s, c[only_alpha][1], with_alpha, only_alpha, opt_alpha, false);
        s += ") * ";
        item(s, c[only_alpha][2], with_alpha, only_alpha, opt_alpha, true);
        s += " + ";
        item(s, c[only_alpha][3], with_alpha, only_alpha, opt_alpha, false);
    }
}

static const char* ST(int j) { return j == 0 ? "S" : "T"; }

/* Dynamic lights (port/include/dynlights.h): struct GfxLights as std140,
 * binding 3 of both stages; the eye-space position rides in varying 18. */
static const char* LIGHTS_BLOCK =
    "layout(std140, binding = 3) uniform Lights {\n"
    "    mat4 inv_proj;\n"      /* eye = inv_proj * clip (row-vector matrix read column-major) */
    "    ivec4 lcount;\n"       /* x point lights, y sun on */
    "    vec4 sun_dir;\n"
    "    vec4 sun_col;\n"
    "    vec4 lpos[16];\n"      /* eye xyz, w radius */
    "    vec4 lcol[16];\n"
    "};\n";
#define DK_VAR_EYE 18

/* Per-pixel lighting of the combined colour, before fog. Flat normals from
 * the eye-space position's derivatives, turned to face the eye. Point lights
 * add the texture colour (or a neutral grey) times the light, so a dark
 * corridor lights up; the sun scales the colour the original lighting gave,
 * so what the level baked as shadow stays dark. */
static const char* LIGHTS_FN =
    "vec3 dynLight(vec3 base, vec3 albedo) {\n"
    "    if (lcount.x == 0 && lcount.y == 0) return base;\n"
    "    vec3 n = normalize(cross(dFdx(vEye), dFdy(vEye)));\n"
    "    if (dot(n, vEye) > 0.0) n = -n;\n"
    "    vec3 add = vec3(0.0);\n"
    "    for (int i = 0; i < lcount.x; i++) {\n"
    "        vec3 d = lpos[i].xyz - vEye;\n"
    "        float d2 = dot(d, d);\n"
    "        float r2 = lpos[i].w * lpos[i].w;\n"
    "        if (d2 >= r2) continue;\n"
    "        float fall = 1.0 - d2 / r2;\n"
    "        fall *= fall;\n"
    "        float ndl = clamp(dot(n, d) * inversesqrt(max(d2, 1e-4)) * 0.75 + 0.25, 0.0, 1.0);\n"
    "        add += lcol[i].rgb * (fall * ndl);\n"
    "    }\n"
    "    vec3 c = base + albedo * add;\n"
    "    if (lcount.y != 0) c += base * sun_col.rgb * (0.4 * max(dot(n, sun_dir.xyz), 0.0));\n"
    "    return c;\n"
    "}\n";

/* Combiner GLSL (deko3d): see gfx_opengl_gen_shader_src. */
/* The effect sources (port/shaders/fx/): common.glsl, then one file per family. */
struct FxSources {
    std::string common, bullets, bursts, impacts;
    std::string diag;   /* a tool's own kinds (8..10: the cluster's diagnostic views, tools_pc/fxview/shaders/diag.glsl); empty in the game's pack */
};

/* An effect kind's family file and its call: "" = no shader for that kind. */
static const std::string& fx_family(const FxSources& fx, unsigned kind) {
    static const std::string none;
    switch (kind) {
        case 2: return fx.bullets;                 /* PORTFX_TRACER: fxTracer */
        case 3: case 4: return fx.bursts;          /* PORTFX_SPARK, PORTFX_BLOOD: fxSpark, fxBlood */
        case 5: case 7: return fx.impacts;         /* PORTFX_IMPACT, PORTFX_CLUSTER: fxImpact, fxClusterPom */
        case 8: case 9: case 10: case 11: case 12: case 13: return fx.diag;   /* tool-only: fxDiagCoverage, Eye, Uv, Pom, PomAlpha, Deriv (the cluster's inputs and steps, not its look) */
    }
    return none;
}

/* fx: the effect sources, used when f.fx_kind is set. */
static bool gen_combiner(CCFeatures& f, std::string& vs, std::string& fs, bool mesh = false, const FxSources* fx = nullptr) {
    s_bad = false;
    /* D986: perspective-correct shade and fog on the Switch. The D540
     * screen-affine shade left a dark patch that follows the view on the
     * big Dam ground triangles (not on the N64; gone with GL + GE_SHADEPERSP=1). */
    const char* shadeq = "";
    const char* fogq = shadeq;               /* per-vertex fog rides with the shade (D543) */
    const int nin = f.opt_alpha ? 4 : 3;
    /* Effects (port/shaders/fx/): 2..5 reshape the combined colour (fxTracer, fxSpark, fxBlood, fxImpact); 7 is a
     * bullet-hole cluster (fxClusterPom over the fused colour and depth, uTex0 / uTex1), whose colour replaces the
     * combiner's; it takes its vertex colour from the first input (SHADE). */
    const unsigned fxk = f.fx_kind;
    std::string fx_code;
    if (fxk != 0) {
        const std::string fam = fx ? fx_family(*fx, fxk) : std::string();
        if (fam.empty()) {
            fprintf(stderr, "dkshgen: no effect shader for PORTFX kind %u (port/shaders/fx/)\n", fxk);
            return false;
        }
        if ((fxk == 7u || fxk >= 8u) && f.num_inputs < 1) return false;                     /* the cluster's vertex colour */
        if ((fxk == 5u || fxk == 7u || fxk >= 8u) && !f.used_textures[0]) return false;   /* fx/impacts.glsl reads vTexCoord0 */
        fx_code = fx->common + (fxk >= 8u ? fx->impacts + fam : fam);
    }

    vs = "#version 460\n";
    vs += fmt("layout(location = %d) in vec4 aVtxPos;\n", DK_ATTR_POS);
    for (int i = 0; i < 2; i++) {
        if (!f.used_textures[i]) continue;
        vs += fmt("layout(location = %d) in vec2 aTexCoord%d;\n", DK_ATTR_TC0 + i, i);
        vs += fmt("layout(location = %d) out vec2 vTexCoord%d;\n", DK_ATTR_TC0 + i, i);
        for (int j = 0; j < 2; j++) {
            if (!f.clamp[i][j]) continue;
            vs += fmt("layout(location = %d) in float aTexClamp%s%d;\n", DK_ATTR_CLS0 + 2 * i + j, ST(j), i);
            vs += fmt("layout(location = %d) out float vTexClamp%s%d;\n", DK_ATTR_CLS0 + 2 * i + j, ST(j), i);
        }
    }
    if (f.opt_fog) {
        vs += fmt("layout(location = %d) in vec4 aFog;\n", DK_ATTR_FOG);
        vs += fmt("layout(location = %d) %sout vec4 vFog;\n", DK_ATTR_FOG, fogq);
        vs += fmt("layout(location = %d) out float vFogW;\n", DK_VAR_FOGW);   /* per-pixel fog (fog * w) */
    }
    if (f.opt_grayscale) {
        vs += fmt("layout(location = %d) in vec4 aGrayscaleColor;\n", DK_ATTR_GRAY);
        vs += fmt("layout(location = %d) out vec4 vGrayscaleColor;\n", DK_ATTR_GRAY);
    }
    for (int i = 0; i < f.num_inputs; i++) {
        vs += fmt("layout(location = %d) in vec%d aInput%d;\n", DK_ATTR_IN1 + i, nin, i + 1);
        vs += fmt("layout(location = %d) %sout vec%d vInput%d;\n", DK_ATTR_IN1 + i, shadeq, nin, i + 1);
    }
    if (mesh) {
        vs += "layout(std140, binding = 2) uniform Mesh {\n"
              "    vec4 fog_color;\n"
              "    float fog_mul; float fog_off; int fog_from_z;\n"
              "    vec4 lod_mask[8];\n"
              "    vec4 env_in[8];\n"
              "    vec4 env_mask[8];\n"
              "    mat4 mtx[32];\n"   /* last: see DkMeshUniforms */
              "};\n";
    }
    vs += LIGHTS_BLOCK;
    vs += fmt("layout(location = %d) out vec3 vEye;\n", DK_VAR_EYE);
    vs += "void main() {\n";
    if (mesh) {
        vs += "    vec4 mp = mtx[int(aVtxPos.w + 0.5)] * vec4(aVtxPos.xyz, 1.0);\n"
              "    float lodv = floor((0.7 + clamp(mp.w / 1024.0, 0.0, 1.0) * 0.3) * 255.0) / 255.0;\n";
    }
    for (int i = 0; i < 2; i++) {
        if (!f.used_textures[i]) continue;
        vs += fmt("    vTexCoord%d = aTexCoord%d;\n", i, i);
        for (int j = 0; j < 2; j++) {
            if (f.clamp[i][j]) vs += fmt("    vTexClamp%s%d = aTexClamp%s%d;\n", ST(j), i, ST(j), i);
        }
    }
    if (f.opt_fog) {
        if (mesh) {
            vs += "    float fw = abs(mp.w) < 0.001 ? 0.001 : mp.w;\n"
                  "    vFog = vec4(fog_color.rgb, fog_from_z != 0 ? floor(clamp(mp.z / fw * fog_mul + fog_off, 0.0, 255.0)) / 255.0 : fog_color.a);\n"
                  "    vFogW = fog_from_z != 0 ? (mp.z * fog_mul + mp.w * fog_off) / 255.0 : fog_color.a * mp.w;\n";
        } else {
            vs += "    vFog = aFog;\n"
                  "    vFogW = aFog.a;\n";   /* the CPU sends fog * w when fog_pixel is set */
        }
    }
    if (f.opt_grayscale) vs += "    vGrayscaleColor = aGrayscaleColor;\n";
    for (int i = 0; i < f.num_inputs; i++) {
        if (mesh) {
            const char* sw = nin == 3 ? ".xyz" : "";
            vs += fmt("    vInput%d = mix(mix(aInput%d, vec%d(lodv), lod_mask[%d]%s), env_in[%d]%s, env_mask[%d]%s);\n",
                      i + 1, i + 1, nin, i, sw, i, sw, i, sw);
        }
        else vs += fmt("    vInput%d = aInput%d;\n", i + 1, i + 1);
    }
    vs += mesh ? "    gl_Position = mp;\n" : "    gl_Position = aVtxPos;\n";
    vs += "    vec4 he = inv_proj * gl_Position;\n"
          "    vEye = he.xyz / (abs(he.w) > 1e-6 ? he.w : 1.0);\n";
    vs += "}\n";

    fs = "#version 460\n";
    fs += "#define SAMPLE_TEX(tex, uv) texture(tex, uv)\n";
    fs += "#define WRAP(x, low, high) mod((x)-(low), (high)-(low)) + (low)\n";
    fs += "#define TEX_OFFSET(tex, uv, texSize, off) SAMPLE_TEX(tex, uv - (off)/texSize)\n";
    for (int i = 0; i < 2; i++) {
        if (!f.used_textures[i]) continue;
        fs += fmt("layout(location = %d) in vec2 vTexCoord%d;\n", DK_ATTR_TC0 + i, i);
        for (int j = 0; j < 2; j++) {
            if (f.clamp[i][j]) fs += fmt("layout(location = %d) in float vTexClamp%s%d;\n", DK_ATTR_CLS0 + 2 * i + j, ST(j), i);
        }
    }
    if (f.opt_fog) fs += fmt("layout(location = %d) %sin vec4 vFog;\n", DK_ATTR_FOG, fogq);
    if (f.opt_fog) fs += fmt("layout(location = %d) in float vFogW;\n", DK_VAR_FOGW);
    if (f.opt_grayscale) fs += fmt("layout(location = %d) in vec4 vGrayscaleColor;\n", DK_ATTR_GRAY);
    for (int i = 0; i < f.num_inputs; i++) {
        fs += fmt("layout(location = %d) %sin vec%d vInput%d;\n", DK_ATTR_IN1 + i, shadeq, nin, i + 1);
    }
    const bool fx_impacts = fxk == 5u || fxk == 7u || fxk >= 8u;   /* fx/impacts.glsl: the clusters read uTex0 / uTex1 */
    if (f.used_textures[0] || fx_impacts) fs += "layout(binding = 0) uniform sampler2D uTex0;\n";
    if (f.used_textures[1] || fx_impacts) fs += "layout(binding = 1) uniform sampler2D uTex1;\n";
    fs += "layout(std140, binding = 0) uniform Draw {\n"
          "    int frame_count;\n"
          "    float noise_scale;\n"
          "    int three_point_filter0;\n"
          "    int three_point_filter1;\n"
          "    int flip_v0;\n"
          "    int flip_v1;\n"
          "    int fog_pixel;\n"
          "    int zpass;\n"   /* D987: the depth-only pass of an SHADER_OPT_ZCUT draw */
          "};\n";
    fs += "float random(in vec3 value) {\n"
          "    float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));\n"
          "    return fract(sin(random) * 143758.5453);\n"
          "}\n";
    fs += "vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {\n"
          "    vec2 offset = fract(texCoord*texSize - vec2(0.5));\n"
          "    offset -= step(1.0, offset.x + offset.y);\n"
          "    vec4 c0 = TEX_OFFSET(tex, texCoord, texSize, offset);\n"
          "    vec4 c1 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x - sign(offset.x), offset.y));\n"
          "    vec4 c2 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x, offset.y - sign(offset.y)));\n"
          "    return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);\n"
          "}\n";
    if (f.opt_blur) {
        fs += "vec4 hookTexture2D(in sampler2D t, in vec2 uv, in vec2 texSize, in int three_point_filter) {\n"
              "    vec4 cw = vec4(0.0);\n"
              "    for (int i = 0; i < 16; ++i) {\n"
              "        vec2 xy = vec2(float(i & 3), float(i >> 2));\n"
              "        float w = 0.009947 - length(xy) * 0.001;\n"
              "        vec2 scaled_uv = uv + (vec2(-1.5) + xy) / texSize;\n"
              "        vec4 tex = three_point_filter != 0 ? filter3point(t, scaled_uv, texSize) : SAMPLE_TEX(t, scaled_uv);\n"
              "        cw += vec4(tex.rgb * w, w);\n"
              "    }\n"
              "    return vec4(cw.rgb / cw.a, 1.0);\n"
              "}\n";
    } else {
        fs += "vec4 hookTexture2D(in sampler2D tex, in vec2 uv, in vec2 texSize, in int three_point_filter) {\n"
              "    return three_point_filter != 0 ? filter3point(tex, uv, texSize) : SAMPLE_TEX(tex, uv);\n"
              "}\n";
    }
    fs += "layout(location = 0) out vec4 outColor;\n";
    fs += LIGHTS_BLOCK;
    fs += fmt("layout(location = %d) in vec3 vEye;\n", DK_VAR_EYE);
    fs += LIGHTS_FN;
    fs += fx_code;
    fs += "void main() {\n";
    for (int i = 0; i < f.num_inputs; i++) {
        fs += fmt("    vec%d cInput%d = clamp(vInput%d, 0.0, 1.0);\n", nin, i + 1, i + 1);
    }
    for (int i = 0; i < 2; i++) {
        if (!f.used_textures[i]) continue;
        const bool s = f.clamp[i][0], t = f.clamp[i][1];
        fs += fmt("    vec2 texSize%d = vec2(textureSize(uTex%d, 0));\n", i, i);
        if (!s && !t) {
            fs += fmt("    vec2 vTexCoordAdj%d = vTexCoord%d;\n", i, i);
        } else if (s && t) {
            fs += fmt("    vec2 vTexCoordAdj%d = clamp(vTexCoord%d, 0.5 / texSize%d, vec2(vTexClampS%d, vTexClampT%d));\n",
                      i, i, i, i, i);
        } else if (s) {
            fs += fmt("    vec2 vTexCoordAdj%d = vec2(clamp(vTexCoord%d.s, 0.5 / texSize%d.s, vTexClampS%d), vTexCoord%d.t);\n",
                      i, i, i, i, i);
        } else {
            fs += fmt("    vec2 vTexCoordAdj%d = vec2(vTexCoord%d.s, clamp(vTexCoord%d.t, 0.5 / texSize%d.t, vTexClampT%d));\n",
                      i, i, i, i, i);
        }
        fs += fmt("    if (flip_v%d != 0) vTexCoordAdj%d.t = 1.0 - vTexCoordAdj%d.t;\n", i, i, i);
        fs += fmt("    vec4 texVal%d = hookTexture2D(uTex%d, vTexCoordAdj%d, texSize%d, three_point_filter%d);\n", i, i, i, i, i);
    }
    fs += f.opt_alpha ? "    vec4 texel;\n" : "    vec3 texel;\n";
    for (int c = 0; c < (f.opt_2cyc ? 2 : 1); c++) {
        fs += "    texel = ";
        if (!f.color_alpha_same[c] && f.opt_alpha) {
            fs += "vec4(";
            append_formula(fs, f.c[c], f.do_single[c][0], f.do_multiply[c][0], f.do_mix[c][0], false, false, true);
            fs += ", ";
            append_formula(fs, f.c[c], f.do_single[c][1], f.do_multiply[c][1], f.do_mix[c][1], true, true, true);
            fs += ")";
        } else {
            append_formula(fs, f.c[c], f.do_single[c][0], f.do_multiply[c][0], f.do_mix[c][0], f.opt_alpha, false, f.opt_alpha);
        }
        fs += ";\n";
        if (c == 0) fs += "    texel = WRAP(texel, -1.01, 1.01);\n";
    }
    fs += "    texel = WRAP(texel, -0.51, 1.51);\n";
    fs += "    texel = clamp(texel, 0.0, 1.0);\n";
    fs += f.used_textures[0] ? "    texel.rgb = min(dynLight(texel.rgb, texVal0.rgb), vec3(1.0));\n"
                             : "    texel.rgb = min(dynLight(texel.rgb, vec3(0.6)), vec3(1.0));\n";
    if (f.opt_fog) {
        fs += "    float fogA = fog_pixel != 0 ? clamp(vFogW * gl_FragCoord.w, 0.0, 1.0) : clamp(vFog.a, 0.0, 1.0);\n";
        fs += f.opt_alpha ? "    texel = vec4(mix(texel.rgb, vFog.rgb, fogA), texel.a);\n"
                          : "    texel = mix(texel, vFog.rgb, fogA);\n";
    }
    if (f.opt_texture_edge && f.opt_alpha) {
        if (f.opt_a2c && !f.opt_alpha_threshold) {
            /* D984: alpha-to-coverage cut-out: the edge sharpened to about a pixel */
            fs += "    texel.a = clamp((texel.a - 0.5) / max(fwidth(texel.a), 1.0 / 1024.0) + 0.5, 0.0, 1.0);\n";
        } else if (f.opt_noise) {
            fs += "    if (texel.a < ((random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + 1.0) / 2.0)) discard;\n";
        } else if (f.opt_alpha_threshold) {
            fs += "    if (texel.a < 1.0 / 256.0) discard;\n";
        } else {
            fs += "    if (texel.a < 0.5) discard;\n";
        }
    }
    if (f.opt_alpha && f.opt_noise && !f.opt_texture_edge) {
        fs += "    texel.a *= floor(clamp(random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + texel.a, 0.0, 1.0));\n";
    }
    if (f.opt_grayscale) {
        fs += "    float intensity = (texel.r + texel.g + texel.b) / 3.0;\n"
              "    vec3 new_texel = vGrayscaleColor.rgb * intensity;\n"
              "    texel.rgb = mix(texel.rgb, new_texel, vGrayscaleColor.a);\n";
    }
    if (fxk >= 8u) {   /* tool-only diagnostic views of the cluster draw: one value per pixel, no lighting, fog or blend of the game's */
        static const char* const diag_fn[] = { "fxDiagCoverage", "fxDiagEye", "fxDiagUv", "fxDiagPom", "fxDiagPomAlpha", "fxDiagDeriv" };
        const char* fn = fxk <= 13u ? diag_fn[fxk - 8u] : diag_fn[0];
        fs += nin == 4 ? "    vec4 fxVc = cInput1;\n" : "    vec4 fxVc = vec4(cInput1, 1.0);\n";
        fs += std::string("    outColor = ") + fn + "(fxVc);\n    return;\n";
    } else if (fxk == 7u) {
        /* the cluster's own colour replaces the combiner's: lit and fogged like the game's surfaces */
        fs += nin == 4 ? "    vec4 fxVc = cInput1;\n" : "    vec4 fxVc = vec4(cInput1, 1.0);\n";
        fs += "    vec4 fxD = fxClusterPom(fxVc);\n"
              "    fxD.a *= fxVc.a;\n"   /* the game's fade (the oldest holes of a prop) */
              "    fxD.rgb = min(dynLight(fxD.rgb, fxD.rgb), vec3(1.0));\n";
        if (f.opt_fog) fs += "    fxD.rgb = mix(fxD.rgb, vFog.rgb, fogA);\n";
        fs += "    outColor = fxD;\n"
              "    return;\n";
    } else if (fxk != 0) {
        std::string call;
        switch (fxk) {
            case 2: call = "fxTracer(%s)"; break;
            case 3: call = "fxSpark(%s)"; break;
            case 4: call = "fxBlood(%s)"; break;
            default: call = std::string("fxImpact(%s, ") + (f.used_textures[0] ? "vTexCoord0" : "vec2(0.5)") + ")"; break;
        }
        const size_t at = call.find("%s");
        if (f.opt_alpha) fs += "    texel = " + call.replace(at, 2, "texel") + ";\n";
        else fs += "    texel = " + call.replace(at, 2, "vec4(texel, 1.0)") + ".rgb;\n";
    }
    if (f.opt_alpha) {
        if (f.opt_alpha_threshold) fs += "    if (texel.a < 8.0 / 256.0) discard;\n";
        if (f.opt_invisible) fs += "    texel.a = 0.0;\n";
        if (f.opt_a2c) fs += "    if (texel.a < 1.0 / 256.0) discard;\n";   /* D984: no depth where nothing is drawn */
        if (f.opt_zcut) fs += "    if (zpass != 0 && texel.a < 0.98) discard;\n";   /* D987: depth only where opaque */
        fs += "    outColor = texel;\n";
    } else {
        fs += "    outColor = vec4(texel, 1.0);\n";
    }
    fs += "}\n";
    return !s_bad;
}

static bool read_list(const char* path, std::vector<std::pair<uint64_t, uint32_t>>& out) {
    FILE* f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "dkshgen: cannot open %s\n", path);
        return false;
    }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
        unsigned long long a = 0;
        unsigned b = 0;
        if (sscanf(p, "%llx %x", &a, &b) != 2) {
            fprintf(stderr, "dkshgen: bad line in %s: %s", path, line);
            fclose(f);
            return false;
        }
        if (a == 0 && b < DK_PACK_NAMED_MAX) {
            fprintf(stderr, "dkshgen: combiner id 0x0 0x%x collides with the named entries\n", b);
            fclose(f);
            return false;
        }
        out.push_back(std::make_pair((uint64_t)a, (uint32_t)b));
    }
    fclose(f);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return true;
}

static bool write_file(const std::string& path, const std::string& s) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        fprintf(stderr, "dkshgen: cannot write %s\n", path.c_str());
        return false;
    }
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
    return true;
}

static bool read_text(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

static int cmd_glsl(const char* list, const char* outdir, const char* tool_list = nullptr) {
    std::vector<std::pair<uint64_t, uint32_t>> ids, fx_ids, tool_ids;
    if (!read_list(list, ids)) return 1;
    std::string dir = list;
    const size_t cut = dir.find_last_of("/\\");
    dir = cut == std::string::npos ? std::string(".") : dir.substr(0, cut);
    const std::string fx_dir = dir + "/fx", fx_list = fx_dir + "/combiners.txt";
    FxSources fx;
    if (!read_list(fx_list.c_str(), fx_ids)) return 1;
    if (!fx_ids.empty()) {
        const std::pair<const char*, std::string*> files[4] = {
            { "common.glsl", &fx.common }, { "bullets.glsl", &fx.bullets }, { "bursts.glsl", &fx.bursts }, { "impacts.glsl", &fx.impacts } };
        for (const auto& fl : files) {
            const std::string path = fx_dir + "/" + fl.first;
            if (!read_text(path, *fl.second)) {
                fprintf(stderr, "dkshgen: cannot read %s\n", path.c_str());
                return 1;
            }
            *fl.second = "// ---- " + std::string("fx/") + fl.first + "\n" + *fl.second;
        }
    }
    if (tool_list) {   /* a GE_TOOLS build: the tool's combiners (kinds 8..13) and their GLSL, next to the list */
        if (!read_list(tool_list, tool_ids)) return 1;
        std::string td = tool_list;
        const size_t tc = td.find_last_of("/\\");
        td = tc == std::string::npos ? std::string(".") : td.substr(0, tc);
        if (!read_text(td + "/shaders/diag.glsl", fx.diag)) {
            fprintf(stderr, "dkshgen: cannot read %s/shaders/diag.glsl\n", td.c_str());
            return 1;
        }
        fx.diag = "// ---- fxview diag.glsl\n" + fx.diag;
        fx_ids.insert(fx_ids.end(), tool_ids.begin(), tool_ids.end());
    }
    int misfiled = 0;
    for (auto& id : ids) {
        if (id.second & SHADER_OPT_FX_MASK) {
            fprintf(stderr, "dkshgen: 0x%016llx 0x%08x is an effect (PORTFX kind %u): it belongs in %s\n",
                    (unsigned long long)id.first, id.second, (id.second & SHADER_OPT_FX_MASK) >> SHADER_OPT_FX_SHIFT, fx_list.c_str());
            misfiled++;
        }
    }
    for (auto& id : fx_ids) {
        if ((id.second & SHADER_OPT_FX_MASK) == 0) {
            fprintf(stderr, "dkshgen: 0x%016llx 0x%08x carries no effect kind: it belongs in %s\n",
                    (unsigned long long)id.first, id.second, list);
            misfiled++;
        }
    }
    if (misfiled) return 1;
    const size_t n_game = ids.size();
    ids.insert(ids.end(), fx_ids.begin(), fx_ids.end());
    std::string names;
    int skipped = 0;
    for (auto& id : ids) {
        CCFeatures f;
        memset(&f, 0, sizeof f);
        gfx_cc_get_features(id.first, id.second, &f);
        std::string vs, fs, mesh_vs;
        const bool is_fx = (id.second & SHADER_OPT_FX_MASK) != 0;
        if (!gen_combiner(f, vs, fs, false, &fx) || !gen_combiner(f, mesh_vs, fs, true, &fx)) {
            if (is_fx) {   /* an effect is never left to the uber shader */
                fprintf(stderr, "dkshgen: effect 0x%016llx 0x%08x cannot be generated\n", (unsigned long long)id.first, id.second);
                return 1;
            }
            fprintf(stderr, "dkshgen: 0x%016llx 0x%08x uses a source GL has no GLSL for; left to the uber-shader\n",
                    (unsigned long long)id.first, id.second);
            skipped++;
            continue;
        }
        const std::string base = fmt("cc_%016llx_%08x", (unsigned long long)id.first, id.second);
        if (!write_file(std::string(outdir) + "/" + base + ".vert.glsl", vs)) return 1;
        if (!write_file(std::string(outdir) + "/" + base + ".mesh.vert.glsl", mesh_vs)) return 1;
        if (!write_file(std::string(outdir) + "/" + base + ".frag.glsl", fs)) return 1;
        names += fmt("%016llx %08x %s\n", (unsigned long long)id.first, id.second, base.c_str());
    }
    if (!write_file(std::string(outdir) + "/list.txt", names)) return 1;
    printf("dkshgen: %d combiners written (%d effects), %d skipped\n", (int)(ids.size() - skipped), (int)(ids.size() - n_game), skipped);
    return 0;
}

static bool read_blob(const char* path, std::vector<uint8_t>& out) {
    out.clear();
    if (strcmp(path, "-") == 0) return true;
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "dkshgen: cannot open %s\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize((size_t)n);
    if (n > 0 && fread(out.data(), 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return false;
    }
    fclose(f);
    return true;
}

static int cmd_pack(const char* outfile, const char* listfile) {
    FILE* lf = fopen(listfile, "r");
    if (!lf) {
        fprintf(stderr, "dkshgen: cannot open %s\n", listfile);
        return 1;
    }
    struct Item { uint64_t id0; uint32_t id1; std::vector<uint8_t> v, f, mv; };
    std::vector<Item> items;
    char line[2048];
    while (fgets(line, sizeof line, lf)) {
        unsigned long long a;
        unsigned b;
        char vp[900], fp[900], mp[900];
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%llx %x %899s %899s %899s", &a, &b, vp, fp, mp) != 5) {
            fprintf(stderr, "dkshgen: bad pack line: %s", line);
            fclose(lf);
            return 1;
        }
        Item it;
        it.id0 = a;
        it.id1 = b;
        if (!read_blob(vp, it.v) || !read_blob(fp, it.f) || !read_blob(mp, it.mv)) {
            fclose(lf);
            return 1;
        }
        items.push_back(std::move(it));
    }
    fclose(lf);
    std::sort(items.begin(), items.end(), [](const Item& x, const Item& y) {
        return x.id0 != y.id0 ? x.id0 < y.id0 : x.id1 < y.id1;
    });
    for (size_t i = 1; i < items.size(); i++) {
        if (items[i].id0 == items[i - 1].id0 && items[i].id1 == items[i - 1].id1) {
            fprintf(stderr, "dkshgen: duplicate pack id 0x%016llx 0x%08x\n", (unsigned long long)items[i].id0, items[i].id1);
            return 1;
        }
    }
    std::vector<uint8_t> out(8 + 8 + items.size() * sizeof(DkPackEntry), 0);
    memcpy(out.data(), DK_PACK_MAGIC, 8);
    const uint32_t count = (uint32_t)items.size();
    memcpy(out.data() + 8, &count, 4);
    auto put = [&](const std::vector<uint8_t>& b, uint32_t& off, uint32_t& len) {
        while (out.size() % 256) out.push_back(0);   /* DKSH code is copied to an aligned code block anyway */
        off = (uint32_t)out.size();
        len = (uint32_t)b.size();
        out.insert(out.end(), b.begin(), b.end());
    };
    for (size_t i = 0; i < items.size(); i++) {
        DkPackEntry e;
        memset(&e, 0, sizeof e);
        e.id0 = items[i].id0;
        e.id1 = items[i].id1;
        if (!items[i].v.empty()) put(items[i].v, e.vert_off, e.vert_len);
        if (!items[i].f.empty()) put(items[i].f, e.frag_off, e.frag_len);
        if (!items[i].mv.empty()) put(items[i].mv, e.mesh_vert_off, e.mesh_vert_len);
        memcpy(out.data() + 16 + i * sizeof(DkPackEntry), &e, sizeof e);
    }
    FILE* of = fopen(outfile, "wb");
    if (!of) {
        fprintf(stderr, "dkshgen: cannot write %s\n", outfile);
        return 1;
    }
    fwrite(out.data(), 1, out.size(), of);
    fclose(of);
    printf("dkshgen: pack %s: %u entries, %u bytes\n", outfile, count, (unsigned)out.size());
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 4 && strcmp(argv[1], "glsl") == 0) return cmd_glsl(argv[2], argv[3]);
    if (argc == 5 && strcmp(argv[1], "glsl") == 0) return cmd_glsl(argv[2], argv[3], argv[4]);
    if (argc == 4 && strcmp(argv[1], "pack") == 0) return cmd_pack(argv[2], argv[3]);
    fprintf(stderr, "usage: dkshgen glsl <combiners.txt> <outdir>\n       dkshgen pack <outfile> <listfile>\n");
    return 2;
}
