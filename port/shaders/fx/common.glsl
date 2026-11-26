// The port's combat effects (port/include/portfx.h), Video.Visuals = Enhanced.
//
// One file per family: bullets.glsl (tracers), bursts.glsl (sparks, blood), impacts.glsl (bullet holes). tools_pc/dkshgen
// gives each effect combiner of fx/combiners.txt its own shader in the packs (deko3d and Vulkan) and pastes this file
// then the effect's own; the uber shader runs no effect. tools_pc/fxview loads the same files (hot reload) to tune them.
// No #include in these files: the tool that assembles them decides the order.
//
// Shared helpers.

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
