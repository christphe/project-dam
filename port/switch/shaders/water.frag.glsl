#version 460
// GE+ water on the level's own water surfaces (Dam, Caverns, Complex): the
// draws of GE's water combiner (unk_092E50.c MipMap2C_Something*_Setup:
// lerp(TEXEL0, TEXEL1, PRIM_LOD_FRAC) * SHADE, two cycles, opaque), tagged
// SHADER_OPT_WATER by fast3d. Paired with the uber vertex shader (same
// locations; vClip = the clip position). With the water off (uWave4.z == 0:
// no GE+ sky this frame, Visuals = Original) the result is GE's combiner
// exactly, as the uber-shader computes it.
// With the water on, the body colour is the level's own: a patch map baked
// from its water texture (skyplus.c, binding 4) blends a deep colour into a
// light one; until it is ready (uWX2.x) the body is GE's texture x shade as
// before. The sky enters only through the reflection (waterShade).
#ifdef GE_GL
// Desktop GL (gfx_opengl.cpp): the vertex shader is the backend's generated one (plus `out vec3 vClip`), so the inputs
// are declared by name, and the Draw / Comb blocks are constants and defines the backend sets per program:
//   WATER_FOG (the program has fog), WATER_PIXEL_FOG (per-pixel fog), WATER_TPF (three-point filtering),
//   WATER_IVEC (vec3 or vec4: the inputs' type), WATER_SHADEQ / WATER_FOGQ (interpolation qualifiers).
in vec2 vTexCoord0;
in vec2 vTexCoord1;
#ifdef WATER_FOG
WATER_FOGQ in vec4 vFog;
#else
const vec4 vFog = vec4(0.0);
#endif
#define vFogW vFog.a
WATER_SHADEQ in WATER_IVEC vInput1;    /* PRIM_LOD_FRAC */
WATER_SHADEQ in WATER_IVEC vInput2;    /* SHADE */
in vec3 vClip;     /* clip x, y, w */
#define W_IN1 vec4(vInput1.rgb, 1.0)
#define W_IN2 vec4(vInput2.rgb, 1.0)

uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform sampler2D uCloudTex;   /* units 2, 3, 4 */
uniform sampler2D uWaveTex;
uniform sampler2D uWMap;

#ifdef WATER_TPF
uniform int three_point_filter0;
uniform int three_point_filter1;
#else
#define three_point_filter0 0
#define three_point_filter1 0
#endif
#define flip_v0 0
#define flip_v1 0
#ifdef WATER_PIXEL_FOG
#define fog_pixel 1
#else
#define fog_pixel 0
#endif
#ifdef WATER_FOG
const uint flags = 2u;
#else
const uint flags = 0u;
#endif
#define CF_FOG (1u << 1)
#else
layout(location = 1) in vec2 vTexCoord0;
layout(location = 2) in vec2 vTexCoord1;
layout(location = 7) in vec4 vFog;
layout(location = 16) in float vFogW;
layout(location = 9) in vec4 vInput1;    /* PRIM_LOD_FRAC */
layout(location = 10) in vec4 vInput2;   /* SHADE */
layout(location = 17) in vec3 vClip;     /* clip x, y, w */
#define W_IN1 vInput1
#define W_IN2 vInput2

layout(binding = 0) uniform sampler2D uTex0;
layout(binding = 1) uniform sampler2D uTex1;
layout(binding = 2) uniform sampler2D uCloudTex;
layout(binding = 3) uniform sampler2D uWaveTex;   /* the wave heights (skyplus.c's fbm) */
layout(binding = 4) uniform sampler2D uWMap;      /* the level's water patches: R = 0 deep .. 1 light (skyplus.c, baked from its water texture) */

layout(std140, binding = 0) uniform Draw {
    int frame_count;
    float noise_scale;
    int three_point_filter0;
    int three_point_filter1;
    int flip_v0;
    int flip_v1;
    int fog_pixel;
    int zpass;
};

layout(std140, binding = 1) uniform Comb {
    uvec4 sel;
    uint flags;
    uint flags2;
};
#define CF_FOG (1u << 1)
#endif

layout(std140, binding = 2) uniform Sky {
    vec4 uVP;
    vec4 uAsp4;
    vec4 uC0_, uC1_, uC2_, uC3_;
    vec4 uEye4;
    vec4 uSky4;
    vec4 uCloud4;
    vec4 uOfs01;
    vec4 uOfs2X;
    vec4 uSunDir4;
    vec4 uSunCol4;
    vec4 uMoonDir4;
    vec4 uWater4;
    vec4 uWave4;
    vec4 uWX0;   /* deep colour, patch map size in water-texture repeats */
    vec4 uWX1;   /* light colour, reflectLit (0..1: how much the vertex lighting dims the reflection and body) */
    vec4 uWX2;   /* x: the patch map is ready, y: a level without a sky (mirror the fog colour, no glints) */
    vec4 uWX3;   /* x: toneKeep (the reflection pulled into the body's hue), y: reflectGain, z: 1 / the level's scale (bg.c room_data_float2) */
};


layout(location = 0) out vec4 outColor;

#ifdef WATER_FAR
/* deko3d's half-size water pass only (cmake/SwitchPort.cmake defines WATER_FAR for that pack entry; Vulkan and GL never do):
 * beyond the near distance the waves come from the far sea baked by skyplus.frag.glsl (pass 4, the same panorama as Frigate's
 * sea, baked for a fixed eye height over a fake plane; uWater4.w is that plane). */
layout(binding = 5) uniform sampler2D uSeaPano;   /* rgb = the reflection + glints weighted, a = the reflection's weight (body 0) */
#define uBakeX uC1_.w   /* where (and how high over the fake plane) the panorama was baked from; > 0 once it is settled */
#define uBakeZ uC2_.w
#define uBakeH uC3_.w
#define WATER_NEAR_Z0 600.0    /* the live water gives way to the baked one between these depths along the view */
#define WATER_NEAR_Z1 6000.0
#define WATER_PANO_MAX 0.8     /* skyplus.frag.glsl panoMax */
#endif

#pragma skycommon

#define TEX_OFFSET(tex, uv, texSize, off) texture(tex, uv - (off)/texSize)
vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {
    vec2 offset = fract(texCoord*texSize - vec2(0.5));
    offset -= step(1.0, offset.x + offset.y);
    vec4 c0 = TEX_OFFSET(tex, texCoord, texSize, offset);
    vec4 c1 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x - sign(offset.x), offset.y));
    vec4 c2 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x, offset.y - sign(offset.y)));
    return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);
}

/* the tone rules on a shaded colour w (waterShade's result, or the baked one): reflectLit scales only the reflection's weight
 * (0 = the lighting does not dim it, 1 = GE's rule); toneKeep pulls the reflection's brightness into the body's hue so the sky's
 * blue does not repaint the water (the original's tone stays); reflectGain is its weight */
vec3 waterTone(vec3 w, vec3 body, vec3 ge, float lit, bool mapOn) {
    if (mapOn) {
        const vec3 LUM = vec3(0.299, 0.587, 0.114);
        vec3 wt = body * (dot(w, LUM) / max(dot(body, LUM), 1e-3));
        w = mix(w, wt, clamp(uWX3.x, 0.0, 1.0));
        return mix(body, w, clamp(mix(1.0, lit, uWX1.w) * uWX3.y, 0.0, 1.0));
    }
    return mix(ge, w, lit);
}

void main() {
    vec2 uv0 = vTexCoord0, uv1 = vTexCoord1;
    if (flip_v0 != 0) uv0.t = 1.0 - uv0.t;
    if (flip_v1 != 0) uv1.t = 1.0 - uv1.t;
    vec4 t0 = three_point_filter0 != 0 ? filter3point(uTex0, uv0, vec2(textureSize(uTex0, 0))) : texture(uTex0, uv0);
    vec4 t1 = three_point_filter1 != 0 ? filter3point(uTex1, uv1, vec2(textureSize(uTex1, 0))) : texture(uTex1, uv1);
    vec4 lod = clamp(W_IN1, 0.0, 1.0);
    vec4 shade = clamp(W_IN2, 0.0, 1.0);
    /* the patch map: the draw's own texcoord scaled down = enlarged, large soft patches (read here: uniform control flow) */
    float wpatch = texture(uWMap, uv0 / max(uWX0.w, 0.5)).r;
    /* GE's combiner (uber.frag: the same formula, wraps and clamp) */
    vec3 ge = (t1.rgb - t0.rgb) * lod.rgb + t0.rgb;
    ge = mod(ge + 1.01, 2.02) - 1.01;
    ge = ge * shade.rgb;
    ge = clamp(mod(ge + 0.51, 2.02) - 0.51, 0.0, 1.0);
    vec3 c = ge;

#ifdef WATER_BODY
    /* deko3d's half-resolution water (#79): the main pass draws only the body colour (GE's draw: no waves, no reflection); the
     * same draws are replayed at half size by the full variant of this shader (no define) and drawn over this colour by
     * watercomp.frag.glsl. Where that fails (a silhouette, the pass off) this is what shows. cmake/SwitchPort.cmake adds the
     * define after the #version line of the pack entry DK_PACK_WATERBODY. */
    if (uWave4.z > 0.5) {
        bool mapOn = uWX2.x > 0.5;
        if (mapOn) c = clamp(mix(uWX0.rgb, uWX1.rgb, wpatch) * shade.rgb, 0.0, 1.0) * uSea;
    }
#else
    /* the pixel's world ray from its screen position (the sky's corner rays),
     * computed in uniform control flow (the texture reads need its derivatives) */
    float wz = max(vClip.z, 1e-4);
    vec2 ndc = vClip.xy / wz;
    if (uAsp4.z > 0.5) ndc.y = -ndc.y;
    float xg = (ndc.x - uAsp.y) / uAsp.x;
    vec3 d = skyRay(xg * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    /* d is on the image plane (1 along the view axis), w = the view depth. w comes out of GE's matrices in the level's
     * scaled units, the eye and the rays in the player's: x room_data_float2 (1 / the level's scale) puts them together.
     * Without it the pattern moved ~0.22x as far as the scenery on Dam: the waves slid with the camera when strafing */
    float wscale = uWX3.z > 0.0 ? uWX3.z : 1.0;
    vec3 p = uEye + d * (wz * wscale);
    gDpdx = dFdx(p.xz);
    gDpdy = dFdy(p.xz);
    if (uWave4.z > 0.5 && vClip.z > 0.0) {
        float dist = length(d) * vClip.z * wscale;
        vec3 nd = normalize(d);
        /* GE's rule: the reflection and glints are dimmed by the vertex colour,
         * so dark water stays dark (Complex's pits are lit black) */
        float lit = clamp(max(max(shade.r, shade.g), shade.b) * 1.4, 0.0, 1.0);
        bool mapOn = uWX2.x > 0.5;
        /* until the map is ready: GE's texture x shade, x the level's tint (no sea on these levels). With it: the patch colour
         * takes the texture's place in GE's combiner (x SHADE, no other factor), so the body keeps the original's tone */
        vec3 body = mapOn ? clamp(mix(uWX0.rgb, uWX1.rgb, wpatch) * shade.rgb, 0.0, 1.0) * uSea : ge * uSea;
        if (uWX2.y > 0.5) {   /* no sky: the cave mirrors its fog colour (the horizon), no sun glints */
            gLite = true;
            gGlint = 0.0;
        }
#ifdef WATER_FAR
        /* the live waves (the moving set only: the far set is the baked sea) up to NEAR_Z1 of depth along the view, the baked far
         * sea from NEAR_Z0 on, blended in between; no bake yet (uBakeH = 0): the live waves everywhere, both sets, as before. The
         * body (the patch map) stays the level's own everywhere: the panorama gives only the reflection's weight and colour, so
         * the tone is the near water's across the blend */
        bool farOn = uBakeH > 0.0;
        float kf = farOn ? smoothstep(WATER_NEAR_Z0, WATER_NEAR_Z1, wz * wscale) : 0.0;   /* wz * wscale: the depth in the eye's units */
        vec3 liveC = c, farC = c;
        if (kf < 1.0) {
            gSeaMode = farOn ? 1 : 0;
            vec3 n = waterNormal(p.xz, dist);
            liveC = waterTone(waterShade(nd, n, body, dist), body, ge, lit, mapOn);
        }
        if (kf > 0.0) {
            /* the panorama was baked from another eye over a fake plane (uBake*): the point under this pixel is looked up as the
             * baked eye saw it, as the far sea does (skyplus.frag.glsl); the point is on this water's own surface, the plane is
             * the fake one: a fixed height, which shows nowhere far away */
            vec3 rd = normalize(vec3(p.x - uBakeX, -uBakeH, p.z - uBakeZ));
            float pu = atan(rd.x, rd.z) / 6.2831853 + 0.5, pv = min(sqrt(max(-rd.y, 0.0)) / WATER_PANO_MAX, 1.0);
            vec4 pn = textureLod(uSeaPano, vec2(pu, 1.0 - pv), 0.0);   /* a render target is stored top row first */
            /* the shade waterShade gives, linear in the body: body * (1 - reflection weight) + the reflection and glints */
            farC = waterTone(body * (1.0 - pn.a) + pn.rgb, body, ge, lit, mapOn);
        }
        c = mix(liveC, farC, kf);
#else
        vec3 n = waterNormal(p.xz, dist);
        c = waterTone(waterShade(nd, n, body, dist), body, ge, lit, mapOn);   /* the Fresnel weight of the reflection, as it is */
#endif
    }
#endif
    if ((flags & CF_FOG) != 0u) {
        float fogA = fog_pixel != 0 ? clamp(vFogW * gl_FragCoord.w, 0.0, 1.0) : clamp(vFog.a, 0.0, 1.0);
        c = mix(c, vFog.rgb, fogA);
    }
    outColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
