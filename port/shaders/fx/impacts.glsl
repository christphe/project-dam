// The port's bullet holes (Video.Visuals = Enhanced), pasted by tools_pc/dkshgen after fx/common.glsl.
// Needs from the host shader: the Lights block (lcount, lpos, lcol, sun_dir), vEye, uTex0, uTex1.

// The game's own holes on props (kind 5, PORTFX_IMPACT; the level's holes are clusters, below): a burnt,
// darker middle and a bit more contrast in the hole. uv = the hole texture's own 0..1 coordinates.
vec4 fxImpact(vec4 c, vec2 uv) {
    float r = length(uv - vec2(0.5));
    c.rgb = pow(c.rgb, vec3(1.25)) * mix(0.55, 1.0, smoothstep(0.05, 0.4, r));
    return c;
}

// A bullet-hole cluster (kind 7, port/src/fxclusters.c): one quad over a group of holes; texture 0 = the fused colour (A = coverage),
// texture 1 = the fused depth (r, 0..1 of the cluster's deepest). The vertex colour's r carries that depth in
// the quad's uv units, x 4 (port/src/fxcluster.c, tools_pc/fxview fx_cluster.h). Parallax occlusion over the
// fused depth (the deepest hole already won per texel), then the relief lit by the sun or a light from above
// and the dynamic lights, as a ratio to the flat wall's light, with the rim's shadow.
// FX_LOWCOST (deko3d: cmake/SwitchPort.cmake defines it for the shaders uam compiles): fewer march, refine and shadow
// steps. One full-height pass of the full shader costs ~5 ms of GPU at 720p on the Switch (fxview --bench).
#ifdef FX_LOWCOST
#define FXC_STEPS 10
#define FXC_REFINE 3
#define FXC_SHADOW 3
#else
#define FXC_STEPS 24
#define FXC_REFINE 5
#define FXC_SHADOW 8
#endif
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
    vec3 sx = dFdx(vEye), sy = dFdy(vEye);   // derivatives first, in uniform control flow (the early out below diverges)
    vec2 ux = dFdx(uv0), uy = dFdy(uv0);
    // Flat wall here (depth 0): the ray meets the surface at t = 0, so uv = uv0. With no coverage there either, the result
    // is transparent: most of a cluster's quad is wall between the holes, and this skips its whole march and lighting.
    if (fxcDepth(uv0) <= 0.0 && textureLod(uTex0, uv0, 0.0).a <= 0.0) return vec4(0.0);
    mat2 Ji = inverse(mat2(ux, uy));
    vec3 Tp = sx * Ji[0][0] + sy * Ji[0][1];
    vec3 Bp = sx * Ji[1][0] + sy * Ji[1][1];
    vec3 N = normalize(cross(Tp, Bp));
    if (dot(N, vEye) > 0.0) N = -N;
    float LT = length(Tp), LB = length(Bp);
    vec3 V = normalize(-vEye);
    vec3 Vp = vec3(dot(V, Tp) / (LT * LT), dot(V, Bp) / (LB * LB), max(dot(V, N), 0.15) / LT);
    float tMax = depthUV / Vp.z, tPrev = 0.0, tHit = tMax;
    // LOD: the largest parallax shift (uv) against one screen pixel's footprint (uv). Below a pixel the march cannot
    // show (a far hole, or one seen straight on): it is drawn at uv0, with no march or refine.
    float pix = max(length(ux), length(uy));
    // A pixel on the wall itself (depth ~0 at uv0) meets the surface at t = 0: the march below starts at t > 0 and would
    // carry a pixel next to a hole into it, so the hole slid over the wall towards the eye (seen "under the rim").
    bool lod = length(Vp.xy) * tMax < pix || fxcDepth(uv0) <= 0.01;
    if (lod) tHit = 0.0;
    for (int i = 1; i <= (lod ? 0 : FXC_STEPS); i++) {
        float t = tMax * float(i) / float(FXC_STEPS);
        if (fxcDepth(uv0 - Vp.xy * t) * depthUV <= Vp.z * t) { tHit = t; break; }
        tPrev = t;
    }
    for (int i = 0; i < (lod ? 0 : FXC_REFINE); i++) {
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
    bool lodShadow = length(Lp.xy) * dHit / Lp.z < pix;   // the rim shadow's own LOD: its shift along the light, below a pixel
    for (int i = 1; i <= (lodShadow ? 0 : FXC_SHADOW); i++) {
        float t = (dHit / Lp.z) * float(i) / float(FXC_SHADOW);
        if (fxcDepth(uv + Lp.xy * t) * depthUV < dHit - Lp.z * t - depthUV * 0.03) { lit = mix(lit, 0.4, 0.5); }
    }
    float f = clamp(fxcLight(Np, L) * lit / max(fxcLight(N, L), 0.05), 0.3, 1.7);
    f = mix(1.0, f, vc.g);   // the vertex colour's g: how much the relief is lit (0 on snow: it lights itself)
    return vec4(col.rgb * f, col.a);
}
