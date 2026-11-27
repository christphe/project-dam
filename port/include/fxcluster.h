/*
 * fxcluster.h -- bullet-hole templates and clusters (shared by the port, port/src/fxparticles.c, and the
 * viewer, tools_pc/fxview). Pure C, header-only, no library calls (GCC builtins: the game's math.h lacks
 * some of libm, docs/dev/findings/2026-11-23-ge-math-h-has-no-log2f.md).
 *
 * A hole template is generated per material and variant: a depth map (0 = the wall's surface, 1 = the
 * deepest) and a pattern (RGBA; A = coverage) with the pit's occlusion baked in but no directional light
 * (the shader lights the relief: port/shaders/fx/impacts.glsl fxClusterPom). The pattern is a grey multiplier around 1
 * (noise, wood fibres, the pit's darkness): the stamp multiplies it by the hole's base colour, the average
 * of the wall's texture under it, a little lighter (the same material, crushed). Metal is the exception: its
 * crown of bare steel keeps its own colour (the paint went, the metal under it is not the paint's colour).
 *
 * A cluster is one texture over a group of holes on one surface. Each hole is stamped into it with "the
 * deepest wins" per texel: where holes overlap, the texel keeps the deeper one's depth and colour, so a new
 * hole never paints its rim over an older hole's pit, and there are no seams. The cluster is drawn as one
 * quad; parallax occlusion runs over its fused depth.
 */
#ifndef FXCLUSTER_H
#define FXCLUSTER_H


#define FXC_TPL 64    /* template size (texels) */
#define FXC_N   128   /* cluster size (texels) */

enum { FXC_STONE, FXC_WOOD, FXC_METAL, FXC_DIRT, FXC_SNOW, FXC_TILE, FXC_GLASS, FXC_MATS };

static float fxc_hash(float x, float y) { float h = __builtin_sinf(x * 127.1f + y * 311.7f) * 43758.5453f; return h - __builtin_floorf(h); }
static float fxc_noise(float x, float y)
{
    const float ix = __builtin_floorf(x), iy = __builtin_floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3.0f - 2.0f * fx); fy = fy * fy * (3.0f - 2.0f * fy);
    const float a = fxc_hash(ix, iy), b = fxc_hash(ix + 1, iy), c = fxc_hash(ix, iy + 1), d = fxc_hash(ix + 1, iy + 1);
    const float ab = a + (b - a) * fx, cd = c + (d - c) * fx;
    return ab + (cd - ab) * fy;
}
static float fxc_fbm(float x, float y)   /* 0..1, mean ~0.47 */
{
    return (0.5f * fxc_noise(x, y) + 0.25f * fxc_noise(x * 2.03f + 1.7f, y * 2.03f) + 0.125f * fxc_noise(x * 4.1f, y * 4.1f + 3.1f) +
            0.0625f * fxc_noise(x * 8.3f + 5.3f, y * 8.3f)) / 0.9375f;
}
static float fxc_ss(float e0, float e1, float x) { float t = (x - e0) / (e1 - e0); t = t < 0 ? 0 : t > 1 ? 1 : t; return t * t * (3 - 2 * t); }
static float fxc_mix(float a, float b, float t) { return a + (b - a) * t; }
static unsigned char fxc_u8(float v) { v = v < 0 ? 0 : v > 1 ? 1 : v; return (unsigned char)(v * 255.0f + 0.5f); }

typedef struct FxcTemplate {
    float depth[FXC_TPL * FXC_TPL];
    unsigned char rgba[FXC_TPL * FXC_TPL * 4];
    float depthWorld;   /* the deepest point, in hole radii (the stamp scales it by the hole's size) */
    int absolute;       /* 1: rgba is the colour itself (metal, glass); 0: a grey multiplier of the hole's base colour */
    int glass;          /* glass: depth -1 marks the hole's empty middle, which stays empty in a cluster; the
                         * cracks elsewhere add up (the larger coverage wins) and may overlap */
} FxcTemplate;

/* One template: p in -1..1 over the hole's quad. */
static void fxc_template(FxcTemplate *t, int mat, int var)
{
    const float ox = var * 3.7f + mat * 11.3f, oy = var * 5.1f + mat * 7.9f;
    t->depthWorld = mat == FXC_DIRT ? 0.35f : mat == FXC_SNOW ? 0.25f : mat == FXC_METAL ? 0.35f : 0.5f;
    t->absolute = mat == FXC_METAL || mat == FXC_GLASS;
    t->glass = mat == FXC_GLASS;
    if (mat == FXC_GLASS) {   /* an empty round hole, a bright crushed ring, rays of cracks; no relief */
        t->depthWorld = 0.0f;
        for (int y = 0; y < FXC_TPL; y++) {
            for (int x = 0; x < FXC_TPL; x++) {
                const float px = (x + 0.5f) / FXC_TPL * 2.0f - 1.0f, py = (y + 0.5f) / FXC_TPL * 2.0f - 1.0f;
                const float rad = __builtin_sqrtf(px * px + py * py), dl = rad > 1e-4f ? rad : 1e-4f, dx = px / dl, dy = py / dl;
                const int i = y * FXC_TPL + x;
                const float holeR = 0.3f + 0.03f * (fxc_noise(dx * 5.0f + ox, dy * 5.0f + oy) - 0.5f);
                if (rad < holeR) {   /* the hole: empty, and it stays empty whatever is stamped over it */
                    t->depth[i] = -1.0f;
                    __builtin_memset(&t->rgba[i * 4], 0, 4);
                    continue;
                }
                const float ring = 1.0f - fxc_ss(holeR + 0.08f, holeR + 0.26f, rad);
                const float len = 0.6f + 0.4f * fxc_noise(dx * 9.0f + oy, dy * 9.0f + ox);   /* each ray's length */
                const float rays = 0.65f * fxc_noise(dx * 11.0f + ox, dy * 11.0f + oy) + 0.35f * fxc_noise(dx * 23.0f + oy, dy * 23.0f + ox);
                const float ray = fxc_ss(0.42f, 0.62f, rays) * (1.0f - fxc_ss(holeR, holeR + len * (1.0f - holeR), rad));
                const float a = ring > ray ? ring : ray;
                t->depth[i] = 0.0f;
                t->rgba[i * 4 + 0] = fxc_u8(0.95f);
                t->rgba[i * 4 + 1] = fxc_u8(0.97f);
                t->rgba[i * 4 + 2] = fxc_u8(1.0f);
                t->rgba[i * 4 + 3] = fxc_u8(a);
            }
        }
        return;
    }
    for (int y = 0; y < FXC_TPL; y++) {
        for (int x = 0; x < FXC_TPL; x++) {
            float px = (x + 0.5f) / FXC_TPL * 2.0f - 1.0f, py = (y + 0.5f) / FXC_TPL * 2.0f - 1.0f;
            float d = 0.0f, a = 0.0f, r, g, b;
            if (mat == FXC_WOOD) py *= 1.25f;   /* a little long along the grain (x; wood holes are never turned) */
            const float rad = __builtin_sqrtf(px * px + py * py);
            float rim;   /* the ragged outline's radius here */
            if (mat == FXC_WOOD) rim = 0.86f + 0.18f * (fxc_noise(px * 2.0f + ox, py * 9.0f + oy) - 0.5f);
            else if (mat == FXC_METAL) rim = 0.62f + 0.06f * (fxc_noise(px * 6.0f + ox, py * 6.0f + oy) - 0.5f);
            else if (mat == FXC_STONE || mat == FXC_TILE) rim = 0.86f + 0.3f * (fxc_fbm(px * 3.0f + ox, py * 3.0f + oy) - 0.47f) + 0.12f * (fxc_noise(px * 11.0f + ox, py * 11.0f + oy) - 0.5f);
            else if (mat == FXC_DIRT) rim = 0.86f + 0.3f * (fxc_fbm(px * 3.0f + ox, py * 3.0f + oy) - 0.47f) + 0.12f * (fxc_noise(px * 11.0f + ox, py * 11.0f + oy) - 0.5f);
            else rim = 0.9f + 0.1f * (fxc_fbm(px * 2.5f + ox, py * 2.5f + oy) - 0.47f);
            const float c = rad / rim;   /* 0 in the middle, 1 on the rim */
            const float n = fxc_fbm(px * 7.0f + ox, py * 7.0f + oy);
            if (mat == FXC_METAL) {
                /* a small hole, a shallow dent; bright bare steel where the paint went, paint chips around */
                d = c < 0.42f ? 1.0f : 0.25f * (1.0f - fxc_ss(0.42f, 1.0f, c));
                d = fxc_mix(1.0f, d, fxc_ss(0.36f, 0.46f, c));
                r = 0.40f; g = 0.41f; b = 0.44f;   /* dull steel: the relief's light brightens it up to x1.7 */
                const float k = 0.85f + 0.3f * n;
                r *= k; g *= k; b *= k;
                a = 1.0f - fxc_ss(0.9f, 1.03f, c);   /* a soft edge into the wall */
                if (c >= 1.0f && c < 1.35f && fxc_fbm(px * 6.0f + ox, py * 6.0f + oy) > 0.62f) a = 1.0f;   /* a few paint chips */
            } else if (mat == FXC_SNOW) {
                /* a soft crater, no hard rim */
                d = __builtin_powf(1.0f - fxc_ss(0.0f, 1.0f, c), 1.4f);
                r = 0.97f; g = 1.0f; b = 1.04f;   /* a cold shade in the dimple */
                const float k = 0.92f + 0.16f * n;
                r *= k; g *= k; b *= k;
                a = 1.0f - fxc_ss(0.8f, 1.05f, c);
            } else {
                /* stone, tile, dirt, wood: a deep flat pit in a cone; little relief, the striations are in the colour */
                const float pitR = mat == FXC_WOOD ? 0.26f : 0.32f;
                /* the slope is a bevel round the pit only (pitR .. pitR + 0.3); the splash beyond is flat, on the wall: a cone
                 * over the whole splash put the painted streaks below the wall, and the parallax slid them over it */
                float cone = 0.55f * __builtin_powf(1.0f - fxc_ss(pitR, pitR + 0.3f, c), 1.3f);
                /* radial streaks: noise on the direction from the centre (seamless around the circle), two scales */
                const float dl = rad > 1e-4f ? rad : 1e-4f, dx = px / dl, dy = py / dl;
                const float streak = 0.6f * fxc_noise(dx * 7.0f + ox, dy * 7.0f + oy + c * 0.6f) + 0.4f * fxc_noise(dx * 19.0f + oy, dy * 19.0f + ox + c * 1.5f);
                if (mat == FXC_DIRT) {   /* dirt, mud: the earlier stone's low terraces and lumps */
                    cone += 0.012f * __builtin_sinf(c * 22.0f + n * 6.0f) * fxc_ss(pitR + 0.04f, pitR + 0.14f, c) * (1.0f - fxc_ss(0.85f, 1.0f, c));
                    cone += 0.02f * (fxc_fbm(px * 6.0f + ox, py * 6.0f + oy) - 0.47f) * fxc_ss(pitR, pitR + 0.1f, c);
                } else if (mat != FXC_WOOD) {
                    cone += 0.03f * (streak - 0.5f) * fxc_ss(pitR + 0.02f, pitR + 0.12f, c);   /* shallow grooves along the streaks */
                } else {   /* wood: torn fibres */
                    cone += 0.07f * (fxc_noise(px * 2.0f + ox, py * 24.0f + oy) - 0.5f) * fxc_ss(pitR, pitR + 0.1f, c);
                }
                d = fxc_mix(1.0f, cone, fxc_ss(pitR - 0.06f, pitR + 0.04f, c));
                r = g = b = 1.0f;
                if (mat == FXC_WOOD) {   /* fibres along the grain */
                    const float fib = fxc_noise(px * 2.5f + ox, py * 26.0f + oy);
                    r = g = b = fxc_mix(0.85f, 1.12f, fib);
                }
                float k = 0.9f + 0.2f * n;
                if (mat == FXC_DIRT) {
                    k *= 0.93f + 0.14f * (0.5f + 0.5f * __builtin_sinf(c * 24.0f + n * 4.0f));   /* the earlier stone's grain */
                } else if (mat != FXC_WOOD) {
                    k = (0.72f + 0.56f * streak) * (0.95f + 0.1f * n);    /* streaks pointing to the centre */
                    k *= fxc_mix(0.72f, 1.06f, fxc_ss(pitR, 1.0f, c));  /* darker near the hole, lighter towards the rim */
                }
                r *= k; g *= k; b *= k;
                a = 1.0f - fxc_ss(0.9f, 1.03f, c);   /* a soft edge into the wall */
            }
            if (d < 0) d = 0;
            if (d > 1) d = 1;
            /* the pit's occlusion, baked: the deeper, the darker (nearly black at the bottom) */
            float ao = fxc_mix(1.0f, 0.05f, fxc_ss(0.5f, 0.97f, d)) * (1.0f - 0.25f * d);
            if (mat == FXC_SNOW) ao = 1.0f;   /* snow lights itself */
            else if (mat == FXC_DIRT) ao = fxc_mix(1.0f, 0.3f, fxc_ss(0.5f, 0.97f, d));
            const int i = y * FXC_TPL + x;
            t->depth[i] = a > 0.0f ? d : 0.0f;
            /* a multiplier is stored halved (0..2 in 0..255); metal's colour as it is */
            const float sc = t->absolute ? 1.0f : 0.5f;
            t->rgba[i * 4 + 0] = fxc_u8(r * ao * sc);
            t->rgba[i * 4 + 1] = fxc_u8(g * ao * sc);
            t->rgba[i * 4 + 2] = fxc_u8(b * ao * sc);
            t->rgba[i * 4 + 3] = fxc_u8(a);
        }
    }
}

/* A glass template from the game's own glass hole texture (RGBA, w x h, decoded from the user's ROM at run
 * time): the texture as it is, texel for texel (nearest); its transparent middle is the hole, marked empty (depth
 * -1) so that it stays empty in a cluster whatever cracks are stamped over it. */
static void fxc_template_glass_from(FxcTemplate *t, const unsigned char *src, int w, int h)
{
    t->depthWorld = 0.0f;
    t->absolute = 1;
    t->glass = 1;
    for (int y = 0; y < FXC_TPL; y++) {
        for (int x = 0; x < FXC_TPL; x++) {
            /* the game's texels as they are (nearest): no filtering before the GPU's own */
            int sx = (int)((x + 0.5f) / FXC_TPL * w), sy = (int)((y + 0.5f) / FXC_TPL * h);
            sx = sx < 0 ? 0 : sx >= w ? w - 1 : sx;
            sy = sy < 0 ? 0 : sy >= h ? h - 1 : sy;
            float px[4];
            for (int c = 0; c < 4; c++) px[c] = src[(sy * w + sx) * 4 + c];
            const int i = y * FXC_TPL + x;
            const float qx = (x + 0.5f) / FXC_TPL * 2.0f - 1.0f, qy = (y + 0.5f) / FXC_TPL * 2.0f - 1.0f;
            const float rad = __builtin_sqrtf(qx * qx + qy * qy);
            const float lum = 0.30f * px[0] + 0.59f * px[1] + 0.11f * px[2];
            if (rad < 0.45f && (px[3] < 128.0f || lum < 90.0f)) {   /* the hole (transparent or dark): empty, and it stays empty */
                t->depth[i] = -1.0f;
                __builtin_memset(&t->rgba[i * 4], 0, 4);
                continue;
            }
            t->depth[i] = 0.0f;
            for (int c = 0; c < 4; c++) t->rgba[i * 4 + c] = (unsigned char)(px[c] + 0.5f);
        }
    }
}

typedef struct FxcHole {
    float x, y;     /* centre, in the cluster's plane (world units) */
    float hw, hh;   /* half extents (world units) */
    int mat, var;
    float base[3];  /* the hole's colour: the wall texture's average under it, a little lighter (0..1) */
} FxcHole;

typedef struct FxcCluster {
    float x0, y0, size;   /* the square it covers (world units) */
    float depthMax;       /* world units of a depth of 1.0 in `depth` */
    float depth[FXC_N * FXC_N];   /* world units */
    unsigned char rgba[FXC_N * FXC_N * 4];
} FxcCluster;

static void fxc_cluster_clear(FxcCluster *c, float x0, float y0, float size)
{
    c->x0 = x0; c->y0 = y0; c->size = size; c->depthMax = 0.0f;
    __builtin_memset(c->depth, 0, sizeof c->depth);
    __builtin_memset(c->rgba, 0, sizeof c->rgba);
}

/* Stamp one hole: each covered texel takes the hole's depth and colour where it is deeper than what is
 * there (or where nothing is there yet). */
static void fxc_stamp(FxcCluster *c, const FxcTemplate *t, const FxcHole *h)
{
    const float scale = t->depthWorld * (h->hw > h->hh ? h->hw : h->hh);
    if (scale > c->depthMax) c->depthMax = scale;
    const float tx0 = (h->x - h->hw - c->x0) / c->size * FXC_N, tx1 = (h->x + h->hw - c->x0) / c->size * FXC_N;
    const float ty0 = (h->y - h->hh - c->y0) / c->size * FXC_N, ty1 = (h->y + h->hh - c->y0) / c->size * FXC_N;
    for (int y = (int)__builtin_floorf(ty0); y <= (int)__builtin_ceilf(ty1); y++) {
        if (y < 0 || y >= FXC_N) continue;
        for (int x = (int)__builtin_floorf(tx0); x <= (int)__builtin_ceilf(tx1); x++) {
            if (x < 0 || x >= FXC_N) continue;
            const float wx = c->x0 + (x + 0.5f) / FXC_N * c->size, wy = c->y0 + (y + 0.5f) / FXC_N * c->size;
            const float u = ((wx - h->x) / h->hw * 0.5f + 0.5f) * FXC_TPL - 0.5f, v = ((wy - h->y) / h->hh * 0.5f + 0.5f) * FXC_TPL - 0.5f;
            const int iu = (int)__builtin_floorf(u + 0.5f), iv = (int)__builtin_floorf(v + 0.5f);
            if (iu < 0 || iv < 0 || iu >= FXC_TPL || iv >= FXC_TPL) continue;
            const int ti = iv * FXC_TPL + iu, ci = y * FXC_N + x;
            /* bilinear: the template's colour, coverage and depth between its texels (a smooth outline) */
            float bl[5] = { 0, 0, 0, 0, 0 };
            {
                const int u0 = (int)__builtin_floorf(u), v0 = (int)__builtin_floorf(v);
                const float fu = u - u0, fv = v - v0;
                for (int k = 0; k < 4; k++) {
                    int su = u0 + (k & 1), sv = v0 + (k >> 1);
                    su = su < 0 ? 0 : su >= FXC_TPL ? FXC_TPL - 1 : su;
                    sv = sv < 0 ? 0 : sv >= FXC_TPL ? FXC_TPL - 1 : sv;
                    const float wk = ((k & 1) ? fu : 1.0f - fu) * ((k >> 1) ? fv : 1.0f - fv);
                    const int si = sv * FXC_TPL + su;
                    for (int q = 0; q < 4; q++) bl[q] += t->rgba[si * 4 + q] * wk;
                    bl[4] += (t->depth[si] > 0.0f ? t->depth[si] : 0.0f) * wk;
                }
            }
            if (t->glass) {   /* glass: a hole stays empty; the cracks elsewhere add up */
                if (t->depth[ti] < 0.0f) {
                    c->depth[ci] = -1.0f;
                    __builtin_memset(&c->rgba[ci * 4], 0, 4);
                } else if (c->depth[ci] >= 0.0f && t->rgba[ti * 4 + 3] > c->rgba[ci * 4 + 3]) {
                    __builtin_memcpy(&c->rgba[ci * 4], &t->rgba[ti * 4], 4);
                }
                continue;
            }
            const unsigned char a = (unsigned char)(bl[3] + 0.5f);
            if (a == 0) continue;
            const float d = bl[4] * scale;
            if (c->depth[ci] < 0.0f) continue;   /* a glass hole (not mixed with other materials, kept safe) */
            if (d > c->depth[ci] || c->rgba[ci * 4 + 3] == 0) {
                c->depth[ci] = d;
                for (int k = 0; k < 3; k++) {
                    const float tc = bl[k] / 255.0f;
                    c->rgba[ci * 4 + k] = fxc_u8(t->absolute ? tc : tc * 2.0f * h->base[k]);
                }
                c->rgba[ci * 4 + 3] = a;
            }
        }
    }
}

/* The cluster at the wall's own texel density: an area average down to R x R (R <= FXC_N), so the holes
 * are as soft as the texture they pierce once the GPU filters them like the wall (a sharp hole on a blurry
 * N64 wall gives the effect away). The colour is averaged weighted by coverage (no dark fringe at the edges);
 * the depth is in 0..1 of depthMax (a glass hole's -1 counts as 0). */
static void fxc_cluster_resample(const FxcCluster *c, int R, unsigned char *rgba, float *depth01)
{
    for (int y = 0; y < R; y++) {
        for (int x = 0; x < R; x++) {
            const int sx0 = x * FXC_N / R, sx1 = (x + 1) * FXC_N / R, sy0 = y * FXC_N / R, sy1 = (y + 1) * FXC_N / R;
            float col[3] = { 0, 0, 0 }, aw = 0, d = 0;
            int cnt = 0;
            for (int sy = sy0; sy < sy1; sy++) {
                for (int sx = sx0; sx < sx1; sx++) {
                    const int i = sy * FXC_N + sx;
                    const float a = c->rgba[i * 4 + 3] / 255.0f;
                    for (int k = 0; k < 3; k++) col[k] += c->rgba[i * 4 + k] * a;
                    aw += a;
                    d += c->depth[i] > 0.0f ? c->depth[i] : 0.0f;
                    cnt++;
                }
            }
            const int o = y * R + x;
            for (int k = 0; k < 3; k++) rgba[o * 4 + k] = (unsigned char)(aw > 0.0f ? col[k] / aw + 0.5f : 0.0f);
            rgba[o * 4 + 3] = (unsigned char)(cnt ? aw / cnt * 255.0f + 0.5f : 0.0f);
            depth01[o] = cnt && c->depthMax > 0.0f ? d / cnt / c->depthMax : 0.0f;
        }
    }
}

#endif
