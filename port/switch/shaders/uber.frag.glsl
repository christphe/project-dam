#version 460
// D973: deko3d uber-shader, fragment stage: any combiner, chosen by the Comb
// block (struct DkCombinerParams, port/fast3d/gfx_dk_layout.h). Same result as
// the generated per-combiner shader (tools_pc/dkshgen, itself a copy of
// gfx_opengl_gen_shader_src): the single / multiply / mix shortcuts are the
// same (a - b) * c + d, and with color_alpha_same the alpha of the vec4 formula
// is the alpha formula. Used for ids missing from the shipped pack (each one is
// logged once as "DKSH miss"). Sources are picked with a switch per slot, never
// a dynamically indexed local array (Maxwell would spill it to local memory).
layout(location = 1) in vec2 vTexCoord0;
layout(location = 2) in vec2 vTexCoord1;
layout(location = 3) in float vTexClampS0;
layout(location = 4) in float vTexClampT0;
layout(location = 5) in float vTexClampS1;
layout(location = 6) in float vTexClampT1;
layout(location = 7) in vec4 vFog;
layout(location = 16) in float vFogW;
layout(location = 8) in vec4 vGrayscaleColor;
layout(location = 9) in vec4 vInput1;
layout(location = 10) in vec4 vInput2;
layout(location = 11) in vec4 vInput3;
layout(location = 12) in vec4 vInput4;
layout(location = 13) in vec4 vInput5;
layout(location = 14) in vec4 vInput6;
layout(location = 15) in vec4 vInput7;

layout(binding = 0) uniform sampler2D uTex0;
layout(binding = 1) uniform sampler2D uTex1;

layout(std140, binding = 0) uniform Draw {
    int frame_count;
    float noise_scale;
    int three_point_filter0;
    int three_point_filter1;
    int flip_v0;
    int flip_v1;
    int fog_pixel;
    int zpass;      // D987: the depth-only pass of a CF_ZCUT draw
};

layout(std140, binding = 1) uniform Comb {
    uvec4 sel;      // cycle 0 rgb, cycle 0 alpha, cycle 1 rgb, cycle 1 alpha: a | b << 8 | c << 16 | d << 24
    uint flags;     // DK_CF_*
    uint flags2;
};

#define CF_ALPHA     (1u << 0)
#define CF_FOG       (1u << 1)
#define CF_TEXEDGE   (1u << 2)
#define CF_NOISE     (1u << 3)
#define CF_2CYC      (1u << 4)
#define CF_ATHRESH   (1u << 5)
#define CF_INVISIBLE (1u << 6)
#define CF_GRAYSCALE (1u << 7)
#define CF_BLUR      (1u << 8)
#define CF_TEX0      (1u << 9)
#define CF_TEX1      (1u << 10)
#define CF_CLAMP_S0  (1u << 11)
#define CF_CLAMP_T0  (1u << 12)
#define CF_CLAMP_S1  (1u << 13)
#define CF_CLAMP_T1  (1u << 14)
#define CF_A2C       (1u << 15)   // D984: alpha-to-coverage draw
#define CF_ZCUT      (1u << 19)   // D987: blended draw with a depth-only pass

#define WRAP(x, low, high) mod((x)-(low), (high)-(low)) + (low)
#define TEX_OFFSET(tex, uv, texSize, off) texture(tex, uv - (off)/texSize)

layout(location = 0) out vec4 outColor;

// Dynamic lights: same block and lighting as tools_pc/dkshgen (LIGHTS_BLOCK, LIGHTS_FN).
layout(location = 18) in vec3 vEye;
layout(std140, binding = 3) uniform Lights {
    mat4 inv_proj;
    ivec4 lcount;
    vec4 sun_dir;
    vec4 sun_col;
    vec4 lpos[16];
    vec4 lcol[16];
};

vec3 dynLight(vec3 base, vec3 albedo) {
    if (lcount.x == 0 && lcount.y == 0) return base;
    vec3 n = normalize(cross(dFdx(vEye), dFdy(vEye)));
    if (dot(n, vEye) > 0.0) n = -n;
    vec3 add = vec3(0.0);
    for (int i = 0; i < lcount.x; i++) {
        vec3 d = lpos[i].xyz - vEye;
        float d2 = dot(d, d);
        float r2 = lpos[i].w * lpos[i].w;
        if (d2 >= r2) continue;
        float fall = 1.0 - d2 / r2;
        fall *= fall;
        float ndl = clamp(dot(n, d) * inversesqrt(max(d2, 1e-4)) * 0.75 + 0.25, 0.0, 1.0);
        add += lcol[i].rgb * (fall * ndl);
    }
    vec3 c = base + albedo * add;
    if (lcount.y != 0) c += base * sun_col.rgb * (0.4 * max(dot(n, sun_dir.xyz), 0.0));
    return c;
}

float random(in vec3 value) {
    float random = dot(sin(value), vec3(12.9898, 78.233, 37.719));
    return fract(sin(random) * 143758.5453);
}

vec4 filter3point(in sampler2D tex, in vec2 texCoord, in vec2 texSize) {
    vec2 offset = fract(texCoord*texSize - vec2(0.5));
    offset -= step(1.0, offset.x + offset.y);
    vec4 c0 = TEX_OFFSET(tex, texCoord, texSize, offset);
    vec4 c1 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x - sign(offset.x), offset.y));
    vec4 c2 = TEX_OFFSET(tex, texCoord, texSize, vec2(offset.x, offset.y - sign(offset.y)));
    return c0 + abs(offset.x)*(c1-c0) + abs(offset.y)*(c2-c0);
}

vec4 sample1(in sampler2D t, in vec2 uv, in vec2 texSize, in int three_point_filter) {
    return three_point_filter != 0 ? filter3point(t, uv, texSize) : texture(t, uv);
}

vec4 hookTexture2D(in sampler2D t, in vec2 uv, in vec2 texSize, in int three_point_filter) {
    if ((flags & CF_BLUR) == 0u) {
        return sample1(t, uv, texSize, three_point_filter);
    }
    vec4 cw = vec4(0.0);
    for (int i = 0; i < 16; ++i) {
        vec2 xy = vec2(float(i & 3), float(i >> 2));
        float w = 0.009947 - length(xy) * 0.001;
        vec2 scaled_uv = uv + (vec2(-1.5) + xy) / texSize;
        vec4 tex = sample1(t, scaled_uv, texSize, three_point_filter);
        cw += vec4(tex.rgb * w, w);
    }
    return vec4(cw.rgb / cw.a, 1.0);
}

// Effect shaders (port/include/portfx.h): the game tags its tracers, impact
// puffs, blood puffs and bullet-hole decals; fast3d puts the PORTFX_* kind in
// Comb.flags2 bits 8..10 (DK_CP_FX_SHIFT, gfx_dk_layout.h), Enhanced only.
// Every effect keeps the game's quad and texture and reshapes its colour and
// alpha. uv = the texture's own 0..1 coordinates.
// FXSHADE BEGIN (tools_pc/fxview reads this block; it uses frame_count)
#define FX_TRACER 2u
#define FX_SPARK  3u
#define FX_BLOOD  4u
#define FX_IMPACT 5u

float fxHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float fxNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(fxHash(i), fxHash(i + vec2(1.0, 0.0)), f.x),
               mix(fxHash(i + vec2(0.0, 1.0)), fxHash(i + vec2(1.0, 1.0)), f.x), f.y);
}

vec4 fxShade(vec4 c, uint kind, vec2 uv) {
    // TRACER, SPARK, BLOOD: the port's particles (port/src/fxparticles.c), drawn untextured; the vertex
    // alpha is 1 on a particle's core and 0 at its rim (times its remaining life), and the shape comes
    // from that ramp here.
    if (kind == FX_TRACER) {
        // a thin white-hot core inside an orange glow
        float core = smoothstep(0.55, 0.95, c.a);
        c.rgb = mix(c.rgb, vec3(1.0, 0.97, 0.88), core);
        c.a = clamp(pow(c.a, 1.6) * 1.3, 0.0, 1.0);
    } else if (kind == FX_SPARK) {
        // a hot streak: white core, the colour at the edge, a slight flicker per frame
        float core = smoothstep(0.6, 1.0, c.a);
        c.rgb = min(mix(c.rgb, vec3(1.0, 0.96, 0.86), core) * 1.15, vec3(1.0));
        float flick = 0.8 + 0.2 * fxHash(gl_FragCoord.xy * 0.01 + vec2(float(frame_count), 3.0));
        c.a = clamp(smoothstep(0.04, 0.5, c.a) * flick, 0.0, 1.0);
    } else if (kind == FX_BLOOD) {
        // a crisp droplet with a wet glint near its middle
        float a = smoothstep(0.28, 0.42, c.a);
        c.rgb = c.rgb + vec3(0.30, 0.10, 0.10) * smoothstep(0.82, 0.98, c.a);
        c.a = a;
    } else if (kind == FX_IMPACT) {
        // a burnt, darker middle and a bit more contrast in the hole
        float r = length(uv - vec2(0.5));
        c.rgb = pow(c.rgb, vec3(1.25)) * mix(0.55, 1.0, smoothstep(0.05, 0.4, r));
    }
    return c;
}
// FXSHADE END



vec4 cIn1, cIn2, cIn3, cIn4, cIn5, cIn6, cIn7;
vec4 texVal0, texVal1;
vec4 texel;
float noiseVal;

vec3 srcRgb(uint s) {
    switch (s) {
        case 1u: return cIn1.rgb;
        case 2u: return cIn2.rgb;
        case 3u: return cIn3.rgb;
        case 4u: return cIn4.rgb;
        case 5u: return cIn5.rgb;
        case 6u: return cIn6.rgb;
        case 7u: return cIn7.rgb;
        case 8u: return texVal0.rgb;
        case 9u: return vec3(texVal0.a);
        case 10u: return texVal1.rgb;
        case 11u: return vec3(texVal1.a);
        case 12u: return vec3(1.0);
        case 13u: return texel.rgb;
        case 14u: return vec3(noiseVal);
    }
    return vec3(0.0);   // SHADER_0, LOD_FRAC (GL has no GLSL for it either)
}

float srcA(uint s) {
    switch (s) {
        case 1u: return cIn1.a;
        case 2u: return cIn2.a;
        case 3u: return cIn3.a;
        case 4u: return cIn4.a;
        case 5u: return cIn5.a;
        case 6u: return cIn6.a;
        case 7u: return cIn7.a;
        case 8u: return texVal0.a;
        case 9u: return texVal0.a;
        case 10u: return texVal1.a;
        case 11u: return texVal1.a;
        case 12u: return 1.0;
        case 13u: return texel.a;
        case 14u: return noiseVal;
    }
    return 0.0;
}

vec3 formulaRgb(uint s) {
    return (srcRgb(s & 0xffu) - srcRgb((s >> 8) & 0xffu)) * srcRgb((s >> 16) & 0xffu) + srcRgb(s >> 24);
}

float formulaA(uint s) {
    return (srcA(s & 0xffu) - srcA((s >> 8) & 0xffu)) * srcA((s >> 16) & 0xffu) + srcA(s >> 24);
}

// FXDECAL BEGIN (tools_pc/fxview reads this block; it uses vEye, uTex0, vTexCoord0)
// The port's bullet holes (port/src/fxparticles.c decalDraw): the game's own hole texture (its streaks
// and painted shading), alpha-blended like the game draws it, read through parallax occlusion on a cone:
// seen at an angle, the crater's inside shifts and its near rim hides part of it. The holes are never
// turned (the game turns none: its painted light keeps one direction). No lighting of the relief.
// Groups (PORTFX_DECAL_* = 8 + group): 0 stone/default, 1 wood, 2 metal, 3 dirt/mud, 4 snow, 5 glass, 6 tile.
#define FXD_DEPTH 0.5   // the cone's depth, in quad radii

// The cone: 0 at the rim (quad radius 1), -1 in the middle (with a small flat bottom).
float fxConeH(vec2 p) { return -(1.0 - smoothstep(0.08, 1.0, length(p))); }

// Kept for the core kind (PORTFX_DECAL_CORE, not drawn at the moment): no coverage.
vec4 fxDecalCore(vec4 cc) { return vec4(0.0); }

vec4 fxDecalPom(uint kind) {
    uint g = kind - 8u;
    // the game's vertex colour by impact type (explosion.c, g_ImpactTypes[].apptype): dark on dirt, light otherwise
    float tint = (g == 3u) ? 0.08 : 0.92;
    vec2 uv = vTexCoord0;
    if (g != 5u) {   // glass is a flat pane: no parallax
        vec2 p0 = uv * 2.0 - 1.0;
        // the tangent frame from the screen derivatives: Tp, Bp = eye-space change per unit of p
        vec3 sx = dFdx(vEye), sy = dFdy(vEye);
        mat2 Ji = inverse(mat2(dFdx(p0), dFdy(p0)));
        vec3 Tp = sx * Ji[0][0] + sy * Ji[0][1];
        vec3 Bp = sx * Ji[1][0] + sy * Ji[1][1];
        vec3 N = normalize(cross(Tp, Bp));
        if (dot(N, vEye) > 0.0) N = -N;
        float LT = length(Tp), LB = length(Bp);
        vec3 V = normalize(-vEye);
        vec3 Vp = vec3(dot(V, Tp) / (LT * LT), dot(V, Bp) / (LB * LB), max(dot(V, N), 0.2) / LT);
        float depth = (g == 3u || g == 4u) ? FXD_DEPTH * 0.6 : FXD_DEPTH;
        float tMax = depth / Vp.z;
        const int STEPS = 12;
        float tPrev = 0.0, tHit = tMax;
        for (int i = 1; i <= STEPS; i++) {
            float t = tMax * float(i) / float(STEPS);
            if (-fxConeH(p0 - Vp.xy * t) * depth <= Vp.z * t) { tHit = t; break; }
            tPrev = t;
        }
        for (int i = 0; i < 4; i++) {
            float t = 0.5 * (tPrev + tHit);
            if (-fxConeH(p0 - Vp.xy * t) * depth <= Vp.z * t) tHit = t; else tPrev = t;
        }
        uv = (p0 - Vp.xy * tHit) * 0.5 + 0.5;
    }
    vec4 t = texture(uTex0, clamp(uv, vec2(0.0), vec2(1.0)));
    if (g == 5u) {   // glass: only the bright cracks; the hole itself is empty (see-through), cracks may overlap
        float lum = dot(t.rgb, vec3(0.299, 0.587, 0.114));
        float r = length(vTexCoord0 * 2.0 - 1.0);
        return vec4(vec3(0.95, 0.97, 1.0), t.a * smoothstep(0.35, 0.65, lum) * smoothstep(0.1, 0.2, r));
    }
    return vec4(t.rgb * tint, t.a * texVal0.a);   // the outline stays the game's (alpha at the quad's own uv)
}

// ---- clusters (prototype): one quad over a group of holes; texture 0 = the fused colour (A = coverage),
// texture 1 = the fused depth (r, 0..1 of the cluster's deepest). The vertex colour's r carries that depth in
// the quad's uv units, x 4 (port/src/fxcluster.c, tools_pc/fxview fx_cluster.h). Parallax occlusion over the
// fused depth (the deepest hole already won per texel), then the relief lit by the sun or a light from above
// and the dynamic lights, as a ratio to the flat wall's light, with the rim's shadow.
#define FXC_STEPS 24
float fxcDepth(vec2 uv) { return textureLod(uTex1, uv, 0.0).r; }
float fxcLight(vec3 n, vec3 L) {
    float s = 0.45 + 0.8 * max(dot(n, L), 0.0);
    for (int i = 0; i < lcount.x; i++) {
        vec3 d = lpos[i].xyz - vEye;
        float d2 = dot(d, d), r2 = lpos[i].w * lpos[i].w;
        if (d2 >= r2) continue;
        float fall = 1.0 - d2 / r2;
        s += dot(lcol[i].rgb, vec3(0.333)) * fall * fall * max(dot(n, d) * inversesqrt(max(d2, 1e-4)), 0.0);
    }
    return s;
}
vec4 fxClusterPom(vec4 vc) {
    vec2 uv0 = vTexCoord0;
    float depthUV = vc.r * 0.25;
    vec3 sx = dFdx(vEye), sy = dFdy(vEye);
    mat2 Ji = inverse(mat2(dFdx(uv0), dFdy(uv0)));
    vec3 Tp = sx * Ji[0][0] + sy * Ji[0][1];
    vec3 Bp = sx * Ji[1][0] + sy * Ji[1][1];
    vec3 N = normalize(cross(Tp, Bp));
    if (dot(N, vEye) > 0.0) N = -N;
    float LT = length(Tp), LB = length(Bp);
    vec3 V = normalize(-vEye);
    vec3 Vp = vec3(dot(V, Tp) / (LT * LT), dot(V, Bp) / (LB * LB), max(dot(V, N), 0.15) / LT);
    float tMax = depthUV / Vp.z, tPrev = 0.0, tHit = tMax;
    for (int i = 1; i <= FXC_STEPS; i++) {
        float t = tMax * float(i) / float(FXC_STEPS);
        if (fxcDepth(uv0 - Vp.xy * t) * depthUV <= Vp.z * t) { tHit = t; break; }
        tPrev = t;
    }
    for (int i = 0; i < 5; i++) {
        float t = 0.5 * (tPrev + tHit);
        if (fxcDepth(uv0 - Vp.xy * t) * depthUV <= Vp.z * t) tHit = t; else tPrev = t;
    }
    vec2 uv = uv0 - Vp.xy * tHit;
    vec4 col = textureLod(uTex0, uv, 0.0);
    // the relief's normal from the depth's slope (uv units), then its light against the flat wall's
    vec2 ts = 1.0 / vec2(textureSize(uTex1, 0));
    float dx = (fxcDepth(uv + vec2(ts.x, 0.0)) - fxcDepth(uv - vec2(ts.x, 0.0))) * depthUV / (2.0 * ts.x);
    float dy = (fxcDepth(uv + vec2(0.0, ts.y)) - fxcDepth(uv - vec2(0.0, ts.y))) * depthUV / (2.0 * ts.y);
    vec3 nt = normalize(vec3(dx, dy, 1.0));
    vec3 Np = normalize(nt.x * Tp / LT + nt.y * Bp / LB + nt.z * N);
    // the sun, else the relief light (dynlights.c addReliefLight: a fixed light from above, 80 degrees up in
    // the world), so a hole keeps one shading as the camera turns; the eye-space fallback only without either
    vec3 L = dot(sun_dir.xyz, sun_dir.xyz) > 0.0 ? normalize(sun_dir.xyz) : normalize(vec3(0.3, 0.9, 0.3));
    vec3 Lp = vec3(dot(L, Tp) / (LT * LT), dot(L, Bp) / (LB * LB), max(dot(L, N), 0.05) / LT);
    float dHit = fxcDepth(uv) * depthUV, lit = 1.0;
    for (int i = 1; i <= 8; i++) {
        float t = (dHit / Lp.z) * float(i) / 8.0;
        if (fxcDepth(uv + Lp.xy * t) * depthUV < dHit - Lp.z * t - depthUV * 0.03) { lit = mix(lit, 0.4, 0.5); }
    }
    float f = clamp(fxcLight(Np, L) * lit / max(fxcLight(N, L), 0.05), 0.3, 1.7);
    f = mix(1.0, f, vc.g);   // the vertex colour's g: how much the relief is lit (0 on snow: it lights itself)
    return vec4(col.rgb * f, col.a);
}
// FXDECAL END

void main() {
    const bool A = (flags & CF_ALPHA) != 0u;
    cIn1 = clamp(vInput1, 0.0, 1.0);
    cIn2 = clamp(vInput2, 0.0, 1.0);
    cIn3 = clamp(vInput3, 0.0, 1.0);
    cIn4 = clamp(vInput4, 0.0, 1.0);
    cIn5 = clamp(vInput5, 0.0, 1.0);
    cIn6 = clamp(vInput6, 0.0, 1.0);
    cIn7 = clamp(vInput7, 0.0, 1.0);
    noiseVal = (random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + 1.0) / 2.0;

    texVal0 = vec4(0.0);
    texVal1 = vec4(0.0);
    if ((flags & CF_TEX0) != 0u) {
        vec2 texSize0 = vec2(textureSize(uTex0, 0));
        vec2 uv = vTexCoord0;
        if ((flags & CF_CLAMP_S0) != 0u) uv.s = clamp(uv.s, 0.5 / texSize0.s, vTexClampS0);
        if ((flags & CF_CLAMP_T0) != 0u) uv.t = clamp(uv.t, 0.5 / texSize0.t, vTexClampT0);
        if (flip_v0 != 0) uv.t = 1.0 - uv.t;
        texVal0 = hookTexture2D(uTex0, uv, texSize0, three_point_filter0);
    }
    if ((flags & CF_TEX1) != 0u) {
        vec2 texSize1 = vec2(textureSize(uTex1, 0));
        vec2 uv = vTexCoord1;
        if ((flags & CF_CLAMP_S1) != 0u) uv.s = clamp(uv.s, 0.5 / texSize1.s, vTexClampS1);
        if ((flags & CF_CLAMP_T1) != 0u) uv.t = clamp(uv.t, 0.5 / texSize1.t, vTexClampT1);
        if (flip_v1 != 0) uv.t = 1.0 - uv.t;
        texVal1 = hookTexture2D(uTex1, uv, texSize1, three_point_filter1);
    }

    texel = vec4(0.0);
    texel = vec4(formulaRgb(sel.x), A ? formulaA(sel.y) : 1.0);
    texel = WRAP(texel, -1.01, 1.01);
    if ((flags & CF_2CYC) != 0u) {
        texel = vec4(formulaRgb(sel.z), A ? formulaA(sel.w) : 1.0);
    }
    texel = WRAP(texel, -0.51, 1.51);
    texel = clamp(texel, 0.0, 1.0);
    vec4 fxRaw = texel;   // a bullet hole's vertex colour: uv and code (fxDecalBlend)
    if (((flags2 >> 8) & 15u) == 15u) texel = fxDecalCore(texel);   // a hole's core: before the lights and the fog
    texel.rgb = min(dynLight(texel.rgb, (flags & CF_TEX0) != 0u ? texVal0.rgb : vec3(0.6)), vec3(1.0));
    if ((flags & CF_FOG) != 0u) {
        float fogA = fog_pixel != 0 ? clamp(vFogW * gl_FragCoord.w, 0.0, 1.0) : clamp(vFog.a, 0.0, 1.0);
        texel.rgb = mix(texel.rgb, vFog.rgb, fogA);
    }
    if (A && (flags & CF_A2C) != 0u && (flags & CF_TEXEDGE) == 0u) {
        /* A2C on a blended 1-bit cut-out: the magnified bilinear alpha ramp would
         * become a wide dithered band; sharpen it to ~a pixel, leave flat alpha. */
        float w = fwidth(texel.a);
        if (w > 1.0 / 256.0) texel.a = clamp((texel.a - 0.5) / w + 0.5, 0.0, 1.0);
    }
    if (A && (flags & CF_TEXEDGE) != 0u) {
        if ((flags & CF_A2C) != 0u && (flags & CF_ATHRESH) == 0u) {
            texel.a = clamp((texel.a - 0.5) / max(fwidth(texel.a), 1.0 / 1024.0) + 0.5, 0.0, 1.0);
        } else if ((flags & CF_NOISE) != 0u) {
            if (texel.a < noiseVal) discard;
        } else if ((flags & CF_ATHRESH) != 0u) {
            if (texel.a < 1.0 / 256.0) discard;
        } else {
            if (texel.a < 0.5) discard;
        }
    }
    if (A && (flags & CF_NOISE) != 0u && (flags & CF_TEXEDGE) == 0u) {
        texel.a *= floor(clamp(random(vec3(floor(gl_FragCoord.xy * noise_scale), float(frame_count))) + texel.a, 0.0, 1.0));
    }
    if ((flags & CF_GRAYSCALE) != 0u) {
        float intensity = (texel.r + texel.g + texel.b) / 3.0;
        vec3 new_texel = vGrayscaleColor.rgb * intensity;
        texel.rgb = mix(texel.rgb, new_texel, vGrayscaleColor.a);
    }
    {
        uint fxk = (flags2 >> 8) & 15u;
        if (fxk == 7u) {   // a bullet-hole cluster (port/src/fxclusters.c): parallax occlusion over its fused depth
            vec4 d = fxClusterPom(cIn1);   // the combiner's first input is SHADE (fxclusters.c): the vertex colour
            d.a *= cIn1.a;                 // the game's fade (a prop's oldest holes)
            d.rgb = min(dynLight(d.rgb, d.rgb), vec3(1.0));
            if ((flags & CF_FOG) != 0u) {
                float fogA = fog_pixel != 0 ? clamp(vFogW * gl_FragCoord.w, 0.0, 1.0) : clamp(vFog.a, 0.0, 1.0);
                d.rgb = mix(d.rgb, vFog.rgb, fogA);
            }
            outColor = d;
            return;
        }
        if (fxk >= 8u && fxk != 15u) {   // the port's bullet holes: the game's texture through parallax occlusion
            vec4 d = fxDecalPom(fxk);
            d.rgb = min(dynLight(d.rgb, d.rgb), vec3(1.0));
            if ((flags & CF_FOG) != 0u) {
                float fogA = fog_pixel != 0 ? clamp(vFogW * gl_FragCoord.w, 0.0, 1.0) : clamp(vFog.a, 0.0, 1.0);
                d.rgb = mix(d.rgb, vFog.rgb, fogA);
            }
            outColor = d;
            return;
        }
        if (fxk != 0u) texel = fxShade(texel, fxk, vTexCoord0);
    }
    if (A) {
        if ((flags & CF_ATHRESH) != 0u && texel.a < 8.0 / 256.0) discard;
        if ((flags & CF_INVISIBLE) != 0u) texel.a = 0.0;
        if ((flags & CF_A2C) != 0u && texel.a < 1.0 / 256.0) discard;
        if ((flags & CF_ZCUT) != 0u && zpass != 0 && texel.a < 0.98) discard;
        outColor = texel;
    } else {
        outColor = vec4(texel.rgb, 1.0);
    }
}
