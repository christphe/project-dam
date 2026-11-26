/* ge_render debug drawing (ge_render.h): lines and points in flat colour, as screen-space quads drawn through the
 * backend's own draw_triangles with the "shade only" combiner. No backend code of its own: it runs on the GL, the
 * Vulkan and the deko3d backend alike. */

#include <cmath>   /* not <math.h>: the game's include/math.h shadows it and declares sqrtf with C++ linkage */
#include <string.h>
#include <vector>

#include "ge_internal.h"
#include "gfx_cc.h"

namespace {

struct Vert { float x, y, z, w, r, g, b, a; };   /* the "shade only" program's layout: position, input 1 (rgba) */

float s_m[16];
int s_vpw = 1, s_vph = 1;
bool s_depth = true;
bool s_open = false;
std::vector<Vert> s_v;   /* three vertices per triangle */

void clip_of(const float p[3], float out[4]) {
    for (int i = 0; i < 4; i++) {
        out[i] = p[0] * s_m[0 * 4 + i] + p[1] * s_m[1 * 4 + i] + p[2] * s_m[2 * 4 + i] + s_m[3 * 4 + i];
    }
}

void quad(const float c[4][4], const float rgba[4]) {
    static const int idx[6] = { 0, 1, 2, 2, 1, 3 };
    for (int i = 0; i < 6; i++) {
        const float* p = c[idx[i]];
        s_v.push_back({ p[0], p[1], p[2], p[3], rgba[0], rgba[1], rgba[2], rgba[3] });
    }
}

void flush(void) {
    if (s_v.empty()) return;
    struct GfxRenderingAPI* api = ge_render_api();
    const uint64_t id0 = ge_render_cc_id_shade();
    const uint32_t id1 = SHADER_OPT_ALPHA;
    struct ShaderProgram* prg = api->lookup_shader(id0, id1);
    if (!prg) prg = api->create_and_load_new_shader(id0, id1);
    api->load_shader(prg);
    api->set_depth_mode(s_depth, false, s_depth, false, 0);
    api->set_use_alpha(true, 0);
    const size_t per_batch = 255 * 3;   /* the translator's batch size, which every backend's vertex ring is sized for */
    for (size_t at = 0; at < s_v.size(); at += per_batch) {
        const size_t n = s_v.size() - at < per_batch ? s_v.size() - at : per_batch;
        api->draw_triangles(&s_v[at].x, n * (sizeof(Vert) / sizeof(float)), n / 3);
    }
    s_v.clear();
}

}   // namespace

extern "C" {

void ge_debug_begin(const float clip_from_world[16], int vp_width, int vp_height, bool depth_test) {
    memcpy(s_m, clip_from_world, sizeof s_m);
    s_vpw = vp_width > 0 ? vp_width : 1;
    s_vph = vp_height > 0 ? vp_height : 1;
    s_depth = depth_test;
    s_v.clear();
    s_open = true;
}

void ge_debug_line(const float a[3], const float b[3], const float rgba[4], float width_px) {
    if (!s_open) return;
    float ca[4], cb[4];
    clip_of(a, ca);
    clip_of(b, cb);
    const float eps = 1e-4f;
    if (ca[3] < eps && cb[3] < eps) return;   /* behind the eye */
    if (ca[3] < eps || cb[3] < eps) {          /* clip the end behind the eye to the w = eps plane */
        float* in = ca[3] >= eps ? ca : cb;
        float* out = ca[3] >= eps ? cb : ca;
        const float t = (in[3] - eps) / (in[3] - out[3]);
        for (int i = 0; i < 4; i++) out[i] = in[i] + (out[i] - in[i]) * t;
    }
    /* direction in pixels, its normal, then the corners offset by half the width (as NDC offsets, scaled by w) */
    const float pax = ca[0] / ca[3] * 0.5f * s_vpw, pay = ca[1] / ca[3] * 0.5f * s_vph;
    const float pbx = cb[0] / cb[3] * 0.5f * s_vpw, pby = cb[1] / cb[3] * 0.5f * s_vph;
    float dx = pbx - pax, dy = pby - pay;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f) { dx = 1.0f; dy = 0.0f; } else { dx /= len; dy /= len; }
    const float h = width_px * 0.5f;
    const float nx = -dy * h * 2.0f / s_vpw, ny = dx * h * 2.0f / s_vph;   /* NDC offset of the half width */
    float c[4][4] = {
        { ca[0] + nx * ca[3], ca[1] + ny * ca[3], ca[2], ca[3] },
        { ca[0] - nx * ca[3], ca[1] - ny * ca[3], ca[2], ca[3] },
        { cb[0] + nx * cb[3], cb[1] + ny * cb[3], cb[2], cb[3] },
        { cb[0] - nx * cb[3], cb[1] - ny * cb[3], cb[2], cb[3] },
    };
    quad(c, rgba);
}

void ge_debug_point(const float p[3], const float rgba[4], float size_px) {
    if (!s_open) return;
    float cp[4];
    clip_of(p, cp);
    if (cp[3] < 1e-4f) return;
    const float hx = size_px * 0.5f * 2.0f / s_vpw, hy = size_px * 0.5f * 2.0f / s_vph;
    float c[4][4] = {
        { cp[0] - hx * cp[3], cp[1] - hy * cp[3], cp[2], cp[3] },
        { cp[0] + hx * cp[3], cp[1] - hy * cp[3], cp[2], cp[3] },
        { cp[0] - hx * cp[3], cp[1] + hy * cp[3], cp[2], cp[3] },
        { cp[0] + hx * cp[3], cp[1] + hy * cp[3], cp[2], cp[3] },
    };
    quad(c, rgba);
}

void ge_debug_end(void) {
    if (!s_open) return;
    flush();
    s_open = false;
}

}
