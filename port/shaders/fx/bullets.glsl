// Tracers (kind 2, PORTFX_TRACER: BeamRecord tracers of the player, guards and autoguns), pasted by tools_pc/dkshgen
// after fx/common.glsl. Drawn untextured (port/src/fxparticles.c): the vertex alpha is 1 on the core and 0 at the rim
// (times the remaining life), and the shape comes from that ramp here.

// A thin white-hot core inside an orange glow.
vec4 fxTracer(vec4 c) {
    float core = smoothstep(0.55, 0.95, c.a);
    c.rgb = mix(c.rgb, vec3(1.0, 0.97, 0.88), core);
    c.a = clamp(pow(c.a, 1.6) * 1.3, 0.0, 1.0);
    return c;
}
