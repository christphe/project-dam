// What a hit throws out (port/include/fxburst.h), pasted by tools_pc/dkshgen after fx/common.glsl. Drawn untextured
// (port/src/fxparticles.c): the vertex alpha is 1 on a particle's core and 0 at its rim (times its remaining life), and
// the shape comes from that ramp here. Needs frame_count (the Draw block).

// Kind 3, PORTFX_SPARK (bullet impact puffs): a hot streak, white core, the colour at the edge, a slight flicker per frame.
vec4 fxSpark(vec4 c) {
    float core = smoothstep(0.6, 1.0, c.a);
    c.rgb = min(mix(c.rgb, vec3(1.0, 0.96, 0.86), core) * 1.15, vec3(1.0));
    float flick = 0.8 + 0.2 * fxHash(gl_FragCoord.xy * 0.01 + vec2(float(frame_count), 3.0));
    c.a = clamp(smoothstep(0.04, 0.5, c.a) * flick, 0.0, 1.0);
    return c;
}

// Kind 4, PORTFX_BLOOD (the Game.BloodOnHit body puff): a crisp droplet with a wet glint near its middle.
vec4 fxBlood(vec4 c) {
    float a = smoothstep(0.28, 0.42, c.a);
    c.rgb = c.rgb + vec3(0.30, 0.10, 0.10) * smoothstep(0.82, 0.98, c.a);
    c.a = a;
    return c;
}
