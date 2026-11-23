#include "scene.h"

#include <cstring>

#include "editor_gl.h"
#include "gfx_opengl.h"
#include "gfx_rendering_api.h"

static GfxRenderingAPI *api() { return &gfx_opengl_api; }

bool Scene::init(int w, int h, std::string &err)
{
    GfxRenderingAPI *a = api();
    a->init();
    a->update_framebuffer_parameters(0, (uint32_t)w, (uint32_t)h, 1, false, true, true, false);
    a->set_texture_filter(FILTER_LINEAR);
    a->set_mipmap_filter(MIPMAP_LINEAR);
    m_inited = true;
    (void)err;
    return true;
}

void Scene::setLevel(const Level *lv)
{
    GfxRenderingAPI *a = api();
    for (auto &r : m_runs) if (r.mesh) a->mesh_delete(r.mesh);
    for (uint32_t t : m_tex) a->delete_texture(t);
    m_runs.clear(); m_tex.clear(); m_runsOfOwner.clear();
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
    a->start_frame();
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

    if (showRooms)
        for (uint32_t o = 0; o < rec.hdr->nowners; o++)
            if (rec.owners[o].kind == LEVELREC_OWNER_ROOM)
                for (int run : m_runsOfOwner[o]) drawRun(run, vp, ident);

    if (showProps)
        for (const Item &it : doc.items()) {
            if (it.removed || !it.hasPos || it.owner < 0 || (size_t)it.owner >= m_runsOfOwner.size()) continue;
            const Mat4 place = itemPlacement(doc, it);
            for (int run : m_runsOfOwner[(size_t)it.owner]) drawRun(run, vp, place);
        }
    a->end_frame();
    editorGlScissorOff();
}
