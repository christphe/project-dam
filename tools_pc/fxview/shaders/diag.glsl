// fxview's diagnostic views of the bullet-hole cluster draw (#141): PORTFX kinds 8..13, GE_TOOLS builds only (tools_pc/fxview/combiners.txt).
// Pasted by tools_pc/dkshgen after fx/common.glsl and fx/impacts.glsl; vc = the vertex colour (the cluster's depth and relief amounts).
// One value per pixel, no light, fog or game blend: probe it to see what the backend feeds the effect shader.

// kind 8: the hole coverage the effect samples (uTex0's alpha at vTexCoord0), as grey. Quad drawn but 0 everywhere = the texture or the uv is wrong.
vec4 fxDiagCoverage(vec4 vc) {
    return vec4(vec3(texture(uTex0, vTexCoord0).a), 1.0);
}

// kind 9: the eye-space position the vertex shader rebuilt from the clip position (inv_proj of the Lights block):
// r = -z / 200, g = x / 400 + 0.5, b = y / 400 + 0.5 (clamped). Near 0 in r = the inverse projection is missing (#141).
vec4 fxDiagEye(vec4 vc) {
    return vec4(clamp(-vEye.z / 200.0, 0.0, 1.0), clamp(vEye.x / 400.0 + 0.5, 0.0, 1.0), clamp(vEye.y / 400.0 + 0.5, 0.0, 1.0), 1.0);
}

// kind 10: the texture coordinates the effect gets (r = u, g = v).
vec4 fxDiagUv(vec4 vc) {
    return vec4(fract(vTexCoord0), 0.0, 1.0);
}

// kind 11: the cluster shader's colour (fxClusterPom) with alpha forced to 1: a transparent or NaN result shows as black / what it computes.
vec4 fxDiagPom(vec4 vc) {
    vec4 r = fxClusterPom(vc);
    return vec4(r.rgb, 1.0);
}

// kind 12: the cluster shader's alpha as grey (0 = it returns nothing to draw).
vec4 fxDiagPomAlpha(vec4 vc) {
    return vec4(vec3(fxClusterPom(vc).a), 1.0);
}

// kind 13: the derivatives the parallax steps start from: r = |dFdx(vEye)|, g = |dFdy(vEye)| (each x 0.5), b = |dFdy(uv)| x 100.
// All zero = the hardware gives no derivatives here (the cluster would take no step).
vec4 fxDiagDeriv(vec4 vc) {
    return vec4(clamp(length(dFdx(vEye)) * 0.5, 0.0, 1.0), clamp(length(dFdy(vEye)) * 0.5, 0.0, 1.0), clamp(length(dFdy(vTexCoord0)) * 100.0, 0.0, 1.0), 1.0);
}
