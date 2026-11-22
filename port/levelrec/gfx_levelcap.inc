/* gfx_levelcap.inc -- level export capture (issue #102), included by gfx_pc.cpp
 * right after gfx_meshcache.inc (it reuses mc_fill_seg / mc_emit_vertex, the
 * exact per-vertex float layout of the GE_MESHGPU meshes).
 *
 * What it records. While a driven display list runs (levelcapRunDl), every
 * triangle that reaches gfx_sp_tri1 is taken BEFORE the reject / near clip
 * (so the camera does not matter) and appended to a "run": the resolved
 * combiner shader ids, sampler / depth / blend state and the vertices in the
 * mesh layout, with the position attribute holding (model x, y, z, matrix
 * index). The matrix of a vertex is the modelview on the RSP stack when it
 * was loaded; the drivers set it to model -> world. Nothing is drawn.
 * Textures are copied when fast3d uploads them (gfx_upload_texture), keyed by
 * a hash of the RGBA32 pixels, independent of the LRU texture cache.
 *
 * Off (s_lr_on == false) the cost is one bool test in gfx_sp_vertex,
 * gfx_sp_tri1, the texture upload and the G_DL command. */

#include "levelcap.h"
#include "levelrec.h"
#include <array>
#include <memory>

namespace {

struct LrTex {
    uint64_t hash;
    uint16_t w, h;
    std::vector<uint8_t> rgba;
};

struct LrRun {
    LevelRecRun h;
    int stride;                    /* floats per vertex */
    std::vector<int32_t> slots;    /* global matrix ids, run slot order */
    std::vector<float> verts;
};

struct LrSnap {
    RDP rdp;
    RSP rsp;
    uintptr_t seg[16];
};

std::vector<LrTex> s_lr_tex;
std::unordered_map<uint64_t, int> s_lr_tex_by_hash;
std::unordered_map<uint32_t, int> s_lr_tex_by_id;
std::vector<std::array<float, 16>> s_lr_mtx;
int s_lr_mtx_last = -1;
std::vector<LrRun> s_lr_runs;
std::vector<LevelRecOwner> s_lr_owners;
int s_lr_owner = -1;
std::unique_ptr<LrSnap> s_lr_snap;
unsigned long long s_lr_tris = 0, s_lr_tris_skipped = 0, s_lr_no_tex = 0;

}  // namespace

/* Per loaded vertex: model position, matrix id (declared early, used by gfx_sp_vertex) */
struct LrVtx {
    float ob[3];
    int32_t mtx;
    bool fogz;
};
static LrVtx s_lr_vtx[MAX_VERTICES + 4];

static float s_lr_offset[3];

extern "C" void levelcapSetWorldOffset(float x, float y, float z) {
    s_lr_offset[0] = x;
    s_lr_offset[1] = y;
    s_lr_offset[2] = z;
}

static int lr_cur_mtx(void) {
    std::array<float, 16> m;
    if (rsp.modelview_matrix_stack_size > 0) {
        memcpy(m.data(), rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], sizeof(m));
    } else {
        memset(m.data(), 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }
    m[12] += s_lr_offset[0];   /* row-major [4][4]: the translation row */
    m[13] += s_lr_offset[1];
    m[14] += s_lr_offset[2];
    if (s_lr_mtx_last >= 0 && s_lr_mtx[s_lr_mtx_last] == m) return s_lr_mtx_last;
    for (int i = (int)s_lr_mtx.size() - 1; i >= 0 && i >= (int)s_lr_mtx.size() - 256; i--) {
        if (s_lr_mtx[i] == m) return s_lr_mtx_last = i;
    }
    s_lr_mtx.push_back(m);
    return s_lr_mtx_last = (int)s_lr_mtx.size() - 1;
}

static void lr_note_vertices(size_t n, size_t dest, const Vtx* v) {
    const int mid = lr_cur_mtx();
    const bool fogz = (rsp.geometry_mode & G_FOG) != 0;
    for (size_t i = 0; i < n && dest + i < (size_t)MAX_VERTICES + 4; i++) {
        LrVtx& d = s_lr_vtx[dest + i];
        d.ob[0] = v[i].v.ob[0];
        d.ob[1] = v[i].v.ob[1];
        d.ob[2] = v[i].v.ob[2];
        d.mtx = mid;
        d.fogz = fogz;
    }
}

static void lr_note_upload(const uint8_t* rgba, uint32_t w, uint32_t h) {
    TextureCacheNode* n = rendering_state.textures[s_lr_unit];
    if (n == nullptr || w == 0 || h == 0 || w > 0xffff || h > 0xffff) return;
    uint64_t hash = 1469598103934665603ULL;
    const size_t bytes = (size_t)w * h * 4;
    for (size_t i = 0; i < bytes; i++) hash = (hash ^ rgba[i]) * 1099511628211ULL;
    hash = (hash ^ w) * 1099511628211ULL;
    hash = (hash ^ h) * 1099511628211ULL;
    auto it = s_lr_tex_by_hash.find(hash);
    int idx;
    if (it == s_lr_tex_by_hash.end()) {
        idx = (int)s_lr_tex.size();
        s_lr_tex.push_back({ hash, (uint16_t)w, (uint16_t)h, std::vector<uint8_t>(rgba, rgba + bytes) });
        s_lr_tex_by_hash[hash] = idx;
    } else {
        idx = it->second;
    }
    s_lr_tex_by_id[n->second.texture_id] = idx;
}

static int lr_tex_index(int unit) {
    const TextureCacheNode* n = rendering_state.textures[unit];
    if (n == nullptr) return -1;
    auto it = s_lr_tex_by_id.find(n->second.texture_id);
    if (it == s_lr_tex_by_id.end()) { s_lr_no_tex++; return -1; }
    return it->second;
}

/* gfx_sp_tri1 with capture on (non-rect triangles only). */
static void lr_capture_tri(uint8_t i1, uint8_t i2, uint8_t i3) {
    const uint32_t gm = rsp.geometry_mode & G_CULL_BOTH;
    const uint8_t cull = gm == G_CULL_BOTH ? 3 : gm == G_CULL_BACK ? 1 : gm == G_CULL_FRONT ? 2 : 0;
    const uint8_t idx[3] = { i1, i2, i3 };
    if (cull == 3 || s_lr_owner < 0 || i1 >= MAX_VERTICES || i2 >= MAX_VERTICES || i3 >= MAX_VERTICES) {
        s_lr_tris_skipped++;
        return;
    }
    TriState ts;
    gfx_tri_state(ts);   /* binds the textures (imports them: gfx_upload_texture) and the program */
    McSeg sg;
    mc_fill_seg(sg, ts);

    LevelRecRun h;
    memset(&h, 0, sizeof(h));
    h.owner = (uint32_t)s_lr_owner;
    h.shader_id0 = ts.comb->shader_id0;
    h.shader_id1 = ts.comb->shader_id1 | (ts.tm * SHADER_OPT_TEXEL0_CLAMP_S);
    for (int t = 0; t < 2; t++) {
        h.tex[t] = -1;
        if (!ts.comb->used_textures[t]) continue;
        h.tex[t] = lr_tex_index(t);
        const TextureCacheNode* n = rendering_state.textures[t];
        if (n != nullptr) {
            h.linear[t] = n->second.linear_filter;
            h.cms[t] = n->second.cms;
            h.cmt[t] = n->second.cmt;
        }
        h.mipmaps[t] = rdp.tex_lod;
    }
    const uint8_t dm = rendering_state.depth_mode;
    h.depth_test = dm & 1;
    h.depth_update = (dm >> 1) & 1;
    h.depth_compare = (dm >> 2) & 1;
    h.depth_source_prim = (dm >> 3) & 1;
    h.zmode = (uint16_t)((dm & 0x30) << 6);
    h.use_alpha = ts.blend;
    h.modulate = ts.use_modulate;
    h.cull = cull;
    h.fog_from_z = ts.use_fog && s_lr_vtx[i1].fogz;
    h.fog_color[0] = rdp.fog_color.r / 255.0f;
    h.fog_color[1] = rdp.fog_color.g / 255.0f;
    h.fog_color[2] = rdp.fog_color.b / 255.0f;
    h.fog_color[3] = rdp.fog_color.a / 255.0f;
    h.fog_mul = rsp.fog_mul;
    h.fog_off = rsp.fog_offset;

    /* the three corners in the mesh layout; the floats that depend on w (the LOD
     * fraction) are found by emitting at two depths, as mc_bake_tri does */
    float vb[3][64];
    int stride = 0;
    for (int c = 0; c < 3; c++) {
        const LrVtx& src = s_lr_vtx[idx[c]];
        struct LoadedVertex lv = rsp.loaded_vertices[idx[c]];
        const int n = (int)(mc_emit_vertex(sg, &lv, vb[c]) - vb[c]);
        if (n > 64 || (c > 0 && n != stride)) { s_lr_tris_skipped++; return; }
        stride = n;
        float a[64], b[64];
        struct LoadedVertex lw = lv;
        lw.w = 1.0f;
        mc_emit_vertex(sg, &lw, a);
        lw.w = 3000.0f;
        mc_emit_vertex(sg, &lw, b);
        const int base = mc_input_base(sg);
        const int ncomp = ts.use_alpha ? 4 : 3;
        for (int i = 4; i < n; i++) {
            if (a[i] != b[i] && i >= base && (i - base) / ncomp < GFX_MESH_MAX_INPUTS) {
                h.lod_mask[(i - base) / ncomp][(i - base) % ncomp] = 1.0f;
            }
        }
        vb[c][0] = src.ob[0];
        vb[c][1] = src.ob[1];
        vb[c][2] = src.ob[2];
    }

    /* the run to append to: same owner and state, and room for the matrices */
    LrRun* run = s_lr_runs.empty() ? nullptr : &s_lr_runs.back();
    auto fits = [&](const LrRun* r) {
        if (r == nullptr || r->stride != stride) return false;
        if (memcmp(&r->h, &h, offsetof(LevelRecRun, nverts)) != 0) return false;
        size_t extra = 0;
        for (int c = 0; c < 3; c++) {
            const int32_t m = s_lr_vtx[idx[c]].mtx;
            bool have = false;
            for (int32_t s : r->slots) have |= (s == m);
            for (int k = 0; k < c; k++) have |= (s_lr_vtx[idx[k]].mtx == m);
            if (!have) extra++;
        }
        return r->slots.size() + extra <= (size_t)LEVELREC_MAX_MTX;
    };
    if (!fits(run)) {
        s_lr_runs.emplace_back();
        run = &s_lr_runs.back();
        run->h = h;
        run->stride = stride;
    }
    for (int c = 0; c < 3; c++) {
        const int32_t m = s_lr_vtx[idx[c]].mtx;
        int slot = -1;
        for (size_t k = 0; k < run->slots.size(); k++) {
            if (run->slots[k] == m) { slot = (int)k; break; }
        }
        if (slot < 0) {
            slot = (int)run->slots.size();
            run->slots.push_back(m);
        }
        vb[c][3] = (float)slot;
        run->verts.insert(run->verts.end(), vb[c], vb[c] + stride);
        run->h.nverts++;
    }
    s_lr_tris++;
}

extern "C" void levelcapArm(int on) {
    s_lr_armed = on != 0;
}

extern "C" int levelcapIsOn(void) {
    return s_lr_on ? 1 : 0;
}

static void lr_restore(void) {
    if (!s_lr_snap) return;
    gfx_flush();
    rdp = s_lr_snap->rdp;
    rsp = s_lr_snap->rsp;
    memcpy(segmentPointers, s_lr_snap->seg, sizeof(segmentPointers));
    rdp.textures_changed[0] = rdp.textures_changed[1] = true;   /* rebind what the list needs */
}

extern "C" void levelcapBegin(void) {
    mc_on();   /* fills the s_mc_lut255 table mc_emit_vertex reads */
    gfx_flush();
    s_lr_snap.reset(new LrSnap);
    s_lr_snap->rdp = rdp;
    s_lr_snap->rsp = rsp;
    memcpy(s_lr_snap->seg, segmentPointers, sizeof(segmentPointers));
    s_lr_tex.clear();
    s_lr_tex_by_hash.clear();
    s_lr_tex_by_id.clear();
    s_lr_mtx.clear();
    s_lr_mtx_last = -1;
    s_lr_runs.clear();
    s_lr_owners.clear();
    s_lr_tris = s_lr_tris_skipped = s_lr_no_tex = 0;
    gfx_texture_cache_clear();   /* every texture the lists use is uploaded again, under capture */
}

extern "C" void levelcapEnd(void) {
    sysLogPrintf(LOG_NOTE, "level export: %llu triangles recorded, %llu skipped (culled both sides / out of range), %llu textures not in the table",
                 s_lr_tris, s_lr_tris_skipped, s_lr_no_tex);
    s_lr_on = false;
    lr_restore();
    s_lr_snap.reset();
}

extern "C" void levelcapRunDl(void* dl, int owner) {
    if (!s_lr_snap || dl == nullptr) return;
    lr_restore();
    s_lr_owner = owner;
    s_lr_on = true;
    s_lr_mtx_last = -1;
    const auto interp = s_interp_mode;   /* D578 hooks would rewrite the matrices */
    s_interp_mode = INTERP_OFF;
    gfx_run_dl((Gfx*)dl);
    gfx_flush();
    s_interp_mode = interp;
    s_lr_on = false;
    s_lr_owner = -1;
}

extern "C" int levelcapAddOwner(int kind, int id, const char* name) {
    LevelRecOwner o;
    memset(&o, 0, sizeof(o));
    o.kind = (uint32_t)kind;
    o.id = id;
    snprintf(o.name, sizeof(o.name), "%s", name ? name : "");
    s_lr_owners.push_back(o);
    return (int)s_lr_owners.size() - 1;
}

extern "C" void levelcapCounts(int* ntex, int* nowners, int* nruns, unsigned long long* ntris) {
    if (ntex) *ntex = (int)s_lr_tex.size();
    if (nowners) *nowners = (int)s_lr_owners.size();
    if (nruns) *nruns = (int)s_lr_runs.size();
    if (ntris) *ntris = s_lr_tris;
}

extern "C" int levelcapOwnerBounds(int owner, float mn[3], float mx[3]) {
    int any = 0;
    for (const LrRun& r : s_lr_runs) {
        if ((int)r.h.owner != owner) continue;
        for (uint32_t v = 0; v < r.h.nverts; v++) {
            const float* p = &r.verts[(size_t)v * r.stride];
            const std::array<float, 16>& m = s_lr_mtx[r.slots[(size_t)p[3]]];
            for (int k = 0; k < 3; k++) {
                const float w = p[0] * m[k] + p[1] * m[4 + k] + p[2] * m[8 + k] + m[12 + k];
                if (!any || w < mn[k]) mn[k] = w;
                if (!any || w > mx[k]) mx[k] = w;
            }
            any = 1;
        }
    }
    return any;
}

extern "C" int levelcapWriteRec(const char* path, const char* build, const char* level) {
    FILE* f = fopen(path, "wb");
    if (f == nullptr) return 1;
    bool ok = true;
    LevelRecHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = LEVELREC_MAGIC;
    hdr.version = LEVELREC_VERSION;
    snprintf(hdr.build, sizeof(hdr.build), "%s", build ? build : "");
    snprintf(hdr.level, sizeof(hdr.level), "%s", level ? level : "");
    hdr.ntextures = (uint32_t)s_lr_tex.size();
    hdr.nowners = (uint32_t)s_lr_owners.size();
    hdr.nruns = (uint32_t)s_lr_runs.size();
    ok = ok && levelrecWrite(f, &hdr, sizeof(hdr));
    for (const LrTex& t : s_lr_tex) {
        LevelRecTexture lt = { t.hash, t.w, t.h };
        ok = ok && levelrecWrite(f, &lt, sizeof(lt)) && levelrecWrite(f, t.rgba.data(), t.rgba.size());
    }
    if (!s_lr_owners.empty()) ok = ok && levelrecWrite(f, s_lr_owners.data(), s_lr_owners.size() * sizeof(LevelRecOwner));
    for (LrRun& r : s_lr_runs) {
        r.h.nmtx = (uint32_t)r.slots.size();
        r.h.nfloats = (uint32_t)r.verts.size();
        ok = ok && levelrecWrite(f, &r.h, sizeof(r.h));
        for (int32_t s : r.slots) ok = ok && levelrecWrite(f, s_lr_mtx[(size_t)s].data(), 16 * sizeof(float));
        ok = ok && levelrecWrite(f, r.verts.data(), r.verts.size() * sizeof(float));
    }
    ok = (fclose(f) == 0) && ok;
    return ok ? 0 : 2;
}

/* The G_DL hook: called for every display-list call while armed. */
static inline void lr_dl_hook(const void* dl) {
    if (levelExportOnDl(dl)) {
        /* the driver ran: it disarmed itself and restored the state */
    }
}
