#version 460
// GE+ water at half resolution (deko3d only, #79; docs/dev/shaders-sea.md). The level's water draws are replayed here at full
// size with the uber vertex shader, depth test LEQUAL against the scene's depth buffer (writes off): a pixel passes only where
// that water surface is the front-most thing, so nothing drawn in front of the water is ever overwritten and no depth has to be
// sampled. The colour comes from the half-size target the full water.frag drew from the same vertices (waves, reflection,
// fog), read at the same screen position, bilinear. The target is cleared to (0,0,0,0) and written with alpha 1: its alpha is
// the water's coverage, so a silhouette (where a bilinear tap lands on a texel the water did not cover) is a mix toward what
// is below, the cheap body colour of the main pass, and never a dark halo.
layout(location = 17) in vec3 vClip;   // clip x, y, w (the uber vertex shader)

layout(binding = 0) uniform sampler2D uHalf;

layout(std140, binding = 0) uniform Comp {
    vec4 uHV;   // the half-size viewport in the target's memory: x, y from the top, width, height
    vec4 uHS;   // the half-size target's size
};

layout(location = 0) out vec4 outColor;

void main() {
    vec2 ndc = vClip.xy / max(vClip.z, 1e-4);
    // clip y = +1 is the top row of a rendered image (the draws' viewport; see fxaa.frag.glsl's 1 - y)
    vec2 uvn = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    vec2 uv = (uHV.xy + uvn * uHV.zw) / uHS.xy;
    vec4 h = textureLod(uHalf, uv, 0.0);
    float a = h.a;
    if (a < 0.05) discard;
    outColor = vec4(clamp(h.rgb / a, 0.0, 1.0), smoothstep(0.3, 0.85, a));
}
