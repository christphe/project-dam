// modelview: see mv_rom.h.
#include "mv_rom.h"
#include "mv_tex.h"
#include <map>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "convert.h"
#include "conv_subdiv.h"
}

static std::vector<uint8_t> g_rom;
static const CvRegionData* g_rd = nullptr;
static std::vector<MvModelEntry> g_models;
static std::vector<uint8_t> g_img;   // the decompressed N64 image of the model loaded last
static int g_ns = 0, g_nt = 0;
static std::map<int, MvTexImage> g_texCache;
static uint32_t rb32(const std::vector<uint8_t>& b, size_t o) { return o + 4 <= b.size() ? ((uint32_t)b[o] << 24) | ((uint32_t)b[o + 1] << 16) | ((uint32_t)b[o + 2] << 8) | b[o + 3] : 0; }
static uint32_t rb16(const std::vector<uint8_t>& b, size_t o) { return o + 2 <= b.size() ? ((uint32_t)b[o] << 8) | b[o + 1] : 0; }

bool mv_rom_texture(int texnum, MvTexImage& out) {
    auto it = g_texCache.find(texnum);
    if (it == g_texCache.end()) { MvTexImage t; mv_tex_load(g_rom.data(), g_rom.size(), texnum, t); it = g_texCache.emplace(texnum, std::move(t)).first; }
    out = it->second; return it->second.w > 0;
}
const std::vector<uint8_t>& mv_rom_model_image(int* ns, int* nt) { if (ns) *ns = g_ns; if (nt) *nt = g_nt; return g_img; }
char mv_rom_region_letter() { return g_rd ? g_rd->letter[0] : 'u'; }

// the converter's candidates for a table name: name, name[1:], name[:-1], name[1:-1] (conv_d43.c)
static bool candidate(const char* name, int k, char* out, size_t cap) {
    const size_t len = strlen(name);
    const char* s = name; size_t n = len;
    if (k == 1 || k == 3) { s++; n--; }
    if (k == 2 || k == 3) n--;
    if ((k == 3 && len < 2) || ((k == 1 || k == 2) && len < 1) || n >= cap) return false;
    memcpy(out, s, n); out[n] = 0; return true;
}

bool mv_rom_ready() { return g_rd != nullptr; }
const std::vector<MvModelEntry>& mv_rom_models() { return g_models; }

bool mv_rom_open(const std::string& path, std::string& err) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { err = "cannot open the ROM"; return false; }
    fseek(f, 0, SEEK_END); const long sz = ftell(f); fseek(f, 0, SEEK_SET);
    g_rom.resize((size_t)sz);
    if (fread(g_rom.data(), 1, (size_t)sz, f) != (size_t)sz) { fclose(f); err = "short read"; return false; }
    fclose(f);
    g_rd = nullptr;
    if (sz > 0x40) switch (g_rom[0x3E]) { case 'E': g_rd = &cv_regions[0]; break; case 'P': g_rd = &cv_regions[1]; break; case 'J': g_rd = &cv_regions[2]; break; }
    if (!g_rd) { err = "unrecognised ROM (a big-endian .z64 is expected)"; return false; }
    g_models.clear();
    for (int i = 0; i < cv_table_count; i++) {
        const char* n = cv_table_names[i]; const size_t len = strlen(n);
        if (len < 3 || n[0] != 'C' || n[len - 1] != 'Z' || strstr(n, "_stan")) continue;
        g_models.push_back({ n, strncmp(n, "Chead", 5) == 0 });
    }
    return true;
}

bool mv_rom_load(const std::string& name, int tier, MvMesh& out, std::string& err) {
    if (!g_rd) { err = "no ROM"; return false; }
    char key[160]; const CvFileRow* row = nullptr;
    for (int k = 0; k < 4 && !row; k++) if (candidate(name.c_str(), k, key, sizeof key)) row = cv_find_row(g_rd, key);
    if (!row) { err = "no file-list row"; return false; }
    for (int k = 0; k < 4; k++) if (candidate(name.c_str(), k, key, sizeof key) && cv_find_row(g_rd, key)) break;
    if ((size_t)row->off + row->size > g_rom.size()) { err = "file past the end of the ROM"; return false; }
    CvBuf dec = {};
    if (cv_rz_inflate(g_rom.data() + row->off, row->size, &dec)) { cv_buf_free(&dec); err = "decompression failed"; return false; }
    int hk = -1;
    for (int k = 0; k < 4 && hk < 0; k++) {
        char cand[160]; if (!candidate(key, k, cand, sizeof cand)) continue;
        for (int i = 0; i < cv_nsnt_count; i++) if (!strcmp(cv_nsnt[i].name, cand)) { hk = i; break; }
    }
    if (hk < 0) { cv_buf_free(&dec); err = "no header data"; return false; }
    CvSdModelMesh mm;
    const int r = cv_sd_model_mesh(dec.p, (uint32_t)dec.len, cv_nsnt[hk].ns, cv_nsnt[hk].nt, tier, &mm);
    g_img.assign(dec.p, dec.p + dec.len); g_ns = cv_nsnt[hk].ns; g_nt = cv_nsnt[hk].nt;
    cv_buf_free(&dec);
    if (r <= 0) { err = r < 0 ? "mesh extraction failed" : "no mesh in this tier"; return false; }
    // joints: one per matrix id (rest offset = the converter's sum of group origins), no hierarchy needed
    int nj = 1;
    for (int i = 0; i < mm.mesh.nv_in; i++) if (mm.mesh.v[i].mtx + 1 > nj) nj = mm.mesh.v[i].mtx + 1;
    out = MvMesh(); out.name = name; out.scale = 0.01f;
    out.joints.resize((size_t)nj);
    for (int j = 0; j < nj; j++) { out.joints[(size_t)j].parent = -1; for (int k = 0; k < 3; k++) out.joints[(size_t)j].offset[k] = j < 1024 && mm.joint_ok[j] ? mm.joint_off[j][k] : 0.0f; }
    out.verts.resize((size_t)mm.mesh.nv_in);
    for (int i = 0; i < mm.mesh.nv_in; i++) {
        const CvSdVert& s = mm.mesh.v[i]; MvVertex& d = out.verts[(size_t)i];
        for (int k = 0; k < 3; k++) d.pos[k] = (float)s.pos[k];
        d.uv[0] = s.st[0] / 1024.0f; d.uv[1] = s.st[1] / 1024.0f;
        d.joint = s.mtx < 0 ? 0 : s.mtx; d.layer = s.layer;
        // a colour per matrix: limbs read apart, and a joint seam shows as a colour change
        const float h = fmodf(d.joint * 0.61803f, 1.0f) * 6.0f, c = 0.55f, x = c * (1.0f - fabsf(fmodf(h, 2.0f) - 1.0f));
        float rgb[3] = { 0, 0, 0 };
        switch ((int)h) { case 0: rgb[0] = c; rgb[1] = x; break; case 1: rgb[0] = x; rgb[1] = c; break; case 2: rgb[1] = c; rgb[2] = x; break;
                          case 3: rgb[1] = x; rgb[2] = c; break; case 4: rgb[0] = x; rgb[2] = c; break; default: rgb[0] = c; rgb[2] = x; break; }
        for (int k = 0; k < 3; k++) d.col[k] = rgb[k] + 0.35f;
        d.col[3] = 1;
    }
    // the head region, as the converter flags it (conv_subdiv.c, cv_subdiv_model: not exposed there, mirrored here):
    // a Chead* file is all head; a body whose switch table has no head attachment (Switches[4] == NULL) has the head
    // in the body: the matrices of the op-2 group with JointID 3 (SKEL_NECK) and of every op-2 group below it.
    {
        const bool headFile = name.compare(0, 5, "Chead") == 0;
        std::vector<char> hmat(1024, 0);
        if (!headFile && g_ns > 4 && (rb32(g_img, 16) & 0xFFFFFF) == 0) {
            struct N { uint32_t o, data; int op; }; std::vector<N> nodes; std::vector<uint32_t> st; std::map<uint32_t, int> seen;
            st.push_back(4u * (uint32_t)g_ns + 12u * (uint32_t)g_nt);
            while (!st.empty() && nodes.size() < 4096) {
                const uint32_t o = st.back(); st.pop_back();
                if (!o || o + 0x18 > g_img.size() || seen.count(o)) continue;
                seen[o] = 1;
                const int op = (int)(rb16(g_img, o) & 0xFF); const uint32_t data = rb32(g_img, o + 4) & 0xFFFFFF;
                if (data + 4 > g_img.size()) continue;
                nodes.push_back({ o, data, op });
                uint32_t ch = rb32(g_img, o + 0x14) & 0xFFFFFF;
                if (op == 8 && data + 12 <= g_img.size()) ch = rb32(g_img, data + 8) & 0xFFFFFF; else if (op == 18) ch = rb32(g_img, data) & 0xFFFFFF;
                if (ch) st.push_back(ch);
                if (rb32(g_img, o + 0xC) & 0xFFFFFF) st.push_back(rb32(g_img, o + 0xC) & 0xFFFFFF);
            }
            auto mark = [&](const N& n) { for (int q = 0; q < 3; q++) { const int id = (int16_t)rb16(g_img, n.data + 0xE + 2 * (uint32_t)q); if (id >= 0 && id < 1024) hmat[(size_t)id] = 1; } };
            for (const N& a : nodes) {
                if (a.op != 2 || a.data + 0x14 > g_img.size() || rb16(g_img, a.data + 0xC) != 3) continue;
                mark(a);
                for (const N& j : nodes) {   // joints below it
                    if (j.op != 2 || j.data + 0x14 > g_img.size()) continue;
                    uint32_t p = rb32(g_img, j.o + 8) & 0xFFFFFF; int depth = 0;
                    while (p && depth++ < 64) { if (p == a.o) { mark(j); break; } p = p < g_img.size() ? rb32(g_img, p + 8) & 0xFFFFFF : 0; }
                }
            }
        }
        for (size_t i = 0; i < out.verts.size(); i++) { const int m = out.verts[i].joint; out.verts[i].head = headFile || (m >= 0 && m < 1024 && hmat[(size_t)m]); }
        out.headOnly = true;
    }
    out.idx.resize((size_t)mm.mesh.nt * 3);
    for (int i = 0; i < mm.mesh.nt * 3; i++) out.idx[(size_t)i] = (uint32_t)mm.mesh.tri[i];
    // materials: one per distinct (texture, wrap S, wrap T) of the DL texture markers (texLoadFromGdl: id = w1 & 0xfff, smode/tmode = w0 bits 22..23 / 20..21)
    {
        std::vector<int> markerMat((size_t)mm.nmarker, -1);
        for (int k = 0; k < mm.nmarker; k++) {
            MvMaterial m; m.texnum = (int)(mm.marker[k][1] & 0xFFF);
            const int sm = (int)((mm.marker[k][0] >> 22) & 3), tm = (int)((mm.marker[k][0] >> 20) & 3);
            m.wrapS = sm == 1 ? 1 : sm == 2 ? 2 : 0; m.wrapT = tm == 1 ? 1 : tm == 2 ? 2 : 0;
            for (size_t q = 0; q < out.materials.size(); q++)
                if (out.materials[q].texnum == m.texnum && out.materials[q].wrapS == m.wrapS && out.materials[q].wrapT == m.wrapT) markerMat[(size_t)k] = (int)q;
            if (markerMat[(size_t)k] < 0) { out.materials.push_back(m); markerMat[(size_t)k] = (int)out.materials.size() - 1; }
        }
        out.tmat.resize((size_t)mm.mesh.nt);
        for (int t = 0; t < mm.mesh.nt; t++) out.tmat[(size_t)t] = mm.tri_marker[t] >= 0 ? markerMat[(size_t)mm.tri_marker[t]] : -1;
    }
    cv_sd_model_mesh_free(&mm);
    return true;
}
