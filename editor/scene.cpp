#include "scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "editor_gl.h"
#include "ge_render.h"

// gfx_cc.h needs C++20 (the editor is C++17): the few combiner constants the decor uses, as there (SHADER_* / SHADER_OPT_*)
enum { CC_INPUT_1 = 1, CC_TEXEL0 = 8 };
enum { CC_OPT_ALPHA = 1 << 0, CC_OPT_TEXTURE_EDGE = 1 << 2, CC_OPT_ZCUT = 1 << 14 };

static GfxRenderingAPI *api() { return ge_render_api(); }

bool Scene::init(int w, int h, std::string &err)
{
    GfxRenderingAPI *a = api();   // ge_render_open has initialised the backend
    resize(w, h);
    a->set_texture_filter(FILTER_LINEAR);
    a->set_mipmap_filter(MIPMAP_LINEAR);
    m_inited = true;
    (void)err;
    return true;
}

void Scene::resize(int w, int h)
{
    api()->update_framebuffer_parameters(0, (uint32_t)w, (uint32_t)h, 1, false, true, true, false);
    api()->on_resize();
}

void Scene::setLevel(const Level *lv)
{
    GfxRenderingAPI *a = api();
    for (auto &r : m_runs) if (r.mesh) a->mesh_delete(r.mesh);
    for (uint32_t t : m_tex) a->delete_texture(t);
    for (auto &d : m_decor) if (d.mesh) a->mesh_delete(d.mesh);
    for (uint32_t t : m_decorTex) a->delete_texture(t);
    m_runs.clear(); m_tex.clear(); m_runsOfOwner.clear();
    m_decor.clear(); m_decorTex.clear(); m_useDecor = false;
    m_cur = nullptr;
    m_level = lv;
    if (!lv || !lv->hasRec) return;
    const LevelRec &rec = lv->rec;

    for (uint32_t i = 0; i < rec.hdr->ntextures; i++) {
        uint32_t id = a->new_texture();
        a->select_texture(0, id, true);
        a->upload_texture(rec.tex[i].rgba, rec.tex[i].t->w, rec.tex[i].t->h, true);
        m_tex.push_back(id);
    }
    m_runsOfOwner.assign(rec.hdr->nowners, {});
    m_runs.resize(rec.hdr->nruns);
    for (uint32_t i = 0; i < rec.hdr->nruns; i++) {
        const LevelRecRun *r = rec.runs[i].r;
        GpuRun &g = m_runs[i];
        m_runsOfOwner[r->owner].push_back((int)i);
        if (r->nverts == 0 || r->nmtx == 0 || r->nfloats == 0) continue;
        ShaderProgram *prg = a->lookup_shader(r->shader_id0, r->shader_id1);
        if (!prg) prg = a->create_and_load_new_shader(r->shader_id0, r->shader_id1);
        uint8_t nin; bool used[2];
        a->shader_get_info(prg, &nin, used);
        g.prg = prg;
        for (int t = 0; t < 2; t++) {
            g.tex[t] = (used[t] && r->tex[t] >= 0 && (uint32_t)r->tex[t] < rec.hdr->ntextures) ? r->tex[t] : -1;
            if (used[t] && g.tex[t] < 0) g.prg = nullptr;   // needs a texture it does not have
        }
        if (!g.prg) continue;
        g.mesh = a->mesh_create(rec.runs[i].verts, r->nfloats);
        g.usable = true;
    }

    // The decor from the glTF export. Its combiner is TEXEL0 * SHADE (colour and alpha), SHADE alone when untextured: the same
    // cycle-1 word is written for colour and alpha (gfx_cc.h: a, b, c, d nibbles per formula, colour in bits 0..15, alpha in 16..31).
    // OPAQUE draws no alpha at all (3-float inputs), MASK discards below 0.5 (SHADER_OPT_TEXTURE_EDGE), BLEND blends.
    if (!lv->hasDecor) return;
    m_useDecor = true;
    const Decor &dc = lv->decor;
    for (const DecorTex &t : dc.tex) {
        uint32_t id = a->new_texture();
        a->select_texture(0, id, true);
        a->upload_texture(t.rgba.data(), (uint32_t)t.w, (uint32_t)t.h, true);
        m_decorTex.push_back(id);
    }
    for (const DecorPrim &p : dc.prims) {
        const DecorMaterial &m = dc.materials[(size_t)p.material];
        const bool textured = m.tex >= 0 && !p.uv.empty();
        const bool alpha = m.alphaMode != 0;
        const uint64_t w = textured ? ((uint64_t)CC_TEXEL0 | ((uint64_t)CC_INPUT_1 << 8)) : ((uint64_t)CC_INPUT_1 << 12);
        const uint64_t id0 = w | (w << 16);
        // A blended surface that writes no depth also writes it where it is opaque (SHADER_OPT_ZCUT, the backend's second depth-only
        // pass), as the game's own draws do (gfx_pc.cpp, D987), except a decal; so its solid part hides what is drawn behind it later.
        const uint32_t id1 = alpha ? (CC_OPT_ALPHA | (m.alphaMode == 1 ? CC_OPT_TEXTURE_EDGE : 0) |
                                      (m.alphaMode == 2 && m.zmode != 0xC00 ? CC_OPT_ZCUT : 0)) : 0;
        ShaderProgram *prg = a->lookup_shader(id0, id1);
        if (!prg) prg = a->create_and_load_new_shader(id0, id1);
        uint8_t nin; bool used[2];
        a->shader_get_info(prg, &nin, used);
        if (nin != 1 || used[0] != textured || used[1]) continue;
        const size_t ncomp = alpha ? 4 : 3, stride = 4 + (textured ? 2 : 0) + ncomp;
        std::vector<float> vb(p.idx.size() * stride);
        for (size_t i = 0; i < p.idx.size(); i++) {
            const size_t v = p.idx[i];
            float *o = &vb[i * stride];
            o[0] = p.pos[v * 3]; o[1] = p.pos[v * 3 + 1]; o[2] = p.pos[v * 3 + 2]; o[3] = 0;   // matrix 0: the view-projection
            size_t k = 4;
            if (textured) { o[k++] = p.uv[v * 2]; o[k++] = p.uv[v * 2 + 1]; }
            for (size_t c = 0; c < ncomp; c++) o[k++] = p.col[v * 4 + c];
        }
        DecorDraw d;
        d.mesh = a->mesh_create(vb.data(), vb.size());
        d.prg = prg;
        d.tex = textured ? m.tex : -1;
        d.cms = m.cms; d.cmt = m.cmt; d.zmode = m.zmode;
        d.nverts = (uint32_t)p.idx.size();
        d.doubleSided = m.doubleSided;
        d.blend = m.alphaMode == 2;
        m_decor.push_back(d);
    }
}

void Scene::drawDecor(bool blendPass, const Mat4 &vp)
{
    GfxRenderingAPI *a = api();
    static GfxMeshDraw d;   // large: keep it off the stack
    for (const DecorDraw &g : m_decor) {
        if (g.blend != blendPass) continue;
        if (g.prg != m_cur) {
            if (m_cur) a->unload_shader(m_cur);
            a->load_shader(g.prg);
            m_cur = g.prg;
        }
        if (g.tex >= 0) {
            a->select_texture(0, m_decorTex[(size_t)g.tex], true);
            a->set_sampler_parameters(0, true, (uint32_t)g.cms, (uint32_t)g.cmt, true);
        }
        a->set_depth_mode(true, !g.blend, true, false, (uint16_t)g.zmode);   // blended: tested, never written; a decal gets the depth offset
        a->set_use_alpha(g.blend, 0);
        memset(&d, 0, sizeof d);
        d.nverts = g.nverts;
        d.nmtx = 1;
        d.cull = g.doubleSided ? 0 : 1;   // glTF: counter-clockwise is the front, as the game's
        d.noise_scale = 1.0f;
        memcpy(d.mtx[0], vp.m, sizeof vp.m);
        a->mesh_draw(g.mesh, &d);
        m_drawn++;
    }
}

void Scene::drawRun(int runIndex, const Mat4 &vp, const Mat4 &placement)
{
    GfxRenderingAPI *a = api();
    const GpuRun &g = m_runs[(size_t)runIndex];
    if (!g.usable) return;
    const LevelRec &rec = m_level->rec;
    const LevelRecRun *r = rec.runs[runIndex].r;

    if (g.prg != m_cur) {
        if (m_cur) a->unload_shader(m_cur);
        a->load_shader(g.prg);
        m_cur = g.prg;
    }
    for (int t = 0; t < 2; t++) {
        if (g.tex[t] < 0) continue;
        a->select_texture(t, m_tex[(size_t)g.tex[t]], r->linear[t] != 0);
        a->set_sampler_parameters(t, r->linear[t] != 0, r->cms[t], r->cmt[t], r->mipmaps[t] != 0);
    }
    a->set_depth_mode(r->depth_test != 0, r->depth_update != 0, r->depth_compare != 0, r->depth_source_prim != 0, r->zmode);
    a->set_use_alpha(r->use_alpha != 0, r->modulate != 0);

    static GfxMeshDraw d;   // large: keep it off the stack
    memset(&d, 0, sizeof d);
    d.byte_offset = 0;
    d.nverts = r->nverts;
    d.nmtx = r->nmtx;
    d.cull = r->cull;
    d.invert_y = false;
    d.fog_from_z = false;      // no fog in the editor: the whole level is readable
    d.noise_scale = 1.0f;
    d.tpf0 = r->tpf0 != 0; d.tpf1 = r->tpf1 != 0;
    memcpy(d.lod_mask, r->lod_mask, sizeof d.lod_mask);
    memcpy(d.env_in, r->env_in, sizeof d.env_in);
    memcpy(d.env_mask, r->env_mask, sizeof d.env_mask);
    const Mat4 vpp = mat4Mul(vp, placement);
    for (uint32_t i = 0; i < r->nmtx; i++) {
        Mat4 w; memcpy(w.m, rec.runs[runIndex].mtx + i * 16, sizeof w.m);
        Mat4 c = mat4Mul(vpp, w);
        memcpy(d.mtx[i], c.m, sizeof c.m);
    }
    a->mesh_draw(g.mesh, &d);
    m_drawn++;
}

void Scene::render(const Doc &doc, const Camera &cam, int winW, int winH, int vx, int vy, int vw, int vh, bool showRooms,
                   bool showProps)
{
    GfxRenderingAPI *a = api();
    m_drawn = 0;
    m_cur = nullptr;   // the GUI and the clear load programs of their own between two renders
    a->start_draw_to_framebuffer(0, 1.0f);
    if (m_offscreen) editorOffscreenBind(winW, winH);   // see editor_gl.h
    editorGlClear(winW, winH, 0.10f, 0.11f, 0.13f);
    if (vw <= 0 || vh <= 0) return;
    const int gx = vx, gy = winH - (vy + vh);    // GL origin is bottom left
    editorGlScissor(gx, gy, vw, vh);
    a->set_viewport(gx, gy, vw, vh);
    a->set_scissor(gx, gy, vw, vh);
    if (!m_level || !m_level->hasRec) return;

    const Mat4 vp = mat4Mul(cam.proj((float)vw / (float)vh), cam.view());
    const Mat4 ident = mat4Identity();
    const LevelRec &rec = m_level->rec;

    if (showRooms) {
        if (m_useDecor) drawDecor(false, vp);   // the glTF decor replaces level.rec's room runs
        else
            for (uint32_t o = 0; o < rec.hdr->nowners; o++)
                if (rec.owners[o].kind == LEVELREC_OWNER_ROOM)
                    for (int run : m_runsOfOwner[o]) drawRun(run, vp, ident);
    }

    if (showProps)
        for (const Item &it : doc.items()) {
            if (it.removed || !it.hasPos || it.owner < 0 || (size_t)it.owner >= m_runsOfOwner.size()) continue;
            const Mat4 place = itemPlacement(doc, it);
            for (int run : m_runsOfOwner[(size_t)it.owner]) drawRun(run, vp, place);
        }
    if (showRooms && m_useDecor) drawDecor(true, vp);   // translucent decor last: tested against props, not written
    if (m_cur) { a->unload_shader(m_cur); m_cur = nullptr; }
    editorGlScissorOff();
}

bool Scene::renderOwner(int owner, int winW, int winH, int size, std::vector<uint8_t> &rgba)
{
    if (!m_level || !m_level->hasRec || owner < 0 || (size_t)owner >= m_runsOfOwner.size() || (size_t)owner >= m_level->owners.size()) return false;
    const OwnerInfo &oi = m_level->owners[(size_t)owner];
    if (!oi.valid || m_runsOfOwner[(size_t)owner].empty() || size > winW || size > winH) return false;
    GfxRenderingAPI *a = api();
    m_drawn = 0;
    m_cur = nullptr;
    ge_render_begin_frame();   // between two frames of the main loop: its own
    a->start_draw_to_framebuffer(0, 1.0f);
    if (m_offscreen) editorOffscreenBind(winW, winH);
    editorGlClear(winW, winH, 0.16f, 0.17f, 0.20f);
    editorGlScissor(0, 0, size, size);            // the bottom left corner of the frame; the next frame draws over it
    a->set_viewport(0, 0, size, size);
    a->set_scissor(0, 0, size, size);
    // a model records facing +z: look at it from the front, a little from the side and above
    Camera cam;
    const Vec3 c = { (oi.mn[0] + oi.mx[0]) * 0.5f, (oi.mn[1] + oi.mx[1]) * 0.5f, (oi.mn[2] + oi.mx[2]) * 0.5f };
    const float dx = oi.mx[0] - oi.mn[0], dy = oi.mx[1] - oi.mn[1], dz = oi.mx[2] - oi.mn[2];
    const float radius = std::max(5.0f, 0.5f * sqrtf(dx * dx + dy * dy + dz * dz));
    cam.yaw = -2.1f; cam.pitch = -0.35f;
    const float dist = radius / sinf(cam.fov * 0.5f) * 1.05f;
    cam.lookAtPoint(c, dist);
    cam.znear = std::max(1.0f, dist - radius * 1.5f);
    cam.zfar = dist + radius * 3.0f;
    const Mat4 vp = mat4Mul(cam.proj(1.0f), cam.view());
    for (int run : m_runsOfOwner[(size_t)owner]) drawRun(run, vp, mat4Identity());
    if (m_cur) { a->unload_shader(m_cur); m_cur = nullptr; }
    ge_render_end_frame();
    editorGlScissorOff();
    return editorGlReadFrame(m_offscreen, winH, 0, winH - size, size, size, rgba);
}
