// make_test_rec: writes a SYNTHETIC level export (level.rec + level.json, format v1 of
// port/levelrec/levelrec.h) so the editor can be developed and tested without the game or a ROM.
// Nothing here is ROM-derived: three box rooms with procedural textures, some pads, a few guard
// and object props, one model owner per type.
//
// The level setup (pads, props, AI lists) is not in level.json any more: the editor reads it from the
// converted data next to the game (port/mission/mission_setup.h). So this also writes a synthetic setup
// file in the same sidecar format into OUTDIR/data/pccg-ntsc-final/ (manifest.csv + pccg.bin); point the
// editor at it with GE_EDITOR_DATA=OUTDIR/data.
//
// It also writes a synthetic glTF decor into the pack OUTDIR/gltf/ge007.passets/levels/bunker1/ (tools/synth_gltf.h; the editor looks in <export>/gltf/ too), which the
// editor draws instead of level.rec's rooms (GE_EDITOR_DECOR=rec shows level.rec's).
//
//   make_test_rec OUTDIR            (OUTDIR is created by the caller)

#include "export.h"      // EX_EXPORTER_VERSION: the synthetic glTF decor carries the version this build reads
#include "levelrec.h"
#include "synth_gltf.h"

#include <cstdint>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Tex { std::vector<uint8_t> px; uint16_t w, h; uint64_t hash; };

uint64_t fnv(const std::vector<uint8_t> &d)
{
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : d) { h ^= b; h *= 1099511628211ull; }
    return h;
}

Tex makeChecker(int w, int h, int cell, uint8_t a[3], uint8_t b[3])
{
    Tex t; t.w = (uint16_t)w; t.h = (uint16_t)h; t.px.resize((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t *c = (((x / cell) + (y / cell)) & 1) ? b : a;
            uint8_t *p = &t.px[((size_t)y * w + x) * 4];
            p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = 255;
        }
    t.hash = fnv(t.px);
    return t;
}

// Vertex layout of the two programs used (see gfx_opengl.cpp gen_shader_src):
//   textured   id0 0x0108 (TEXEL0 * INPUT_1): pos4, uv2, input1 rgb3  = 9 floats
//   untextured id0 0x1000 (INPUT_1)         : pos4, input1 rgb3       = 7 floats
struct Mesh { std::vector<float> v; int stride; };

void vtx(Mesh &m, float x, float y, float z, float u, float vv, const float c[3])
{
    m.v.push_back(x); m.v.push_back(y); m.v.push_back(z); m.v.push_back(0.0f);   // matrix index 0
    if (m.stride == 9) { m.v.push_back(u); m.v.push_back(vv); }
    m.v.push_back(c[0]); m.v.push_back(c[1]); m.v.push_back(c[2]);
}

// Triangles wound so that the front face (counter-clockwise, as drawn with culling "back") looks
// toward the centre of the box (inward: a room seen from inside) or away from it (a model).
void quad(Mesh &m, const float p[4][3], float uvs, const float c[3], const float ctr[3], bool inward)
{
    float e1[3], e2[3], n[3];
    for (int i = 0; i < 3; i++) { e1[i] = p[1][i] - p[0][i]; e2[i] = p[2][i] - p[0][i]; }
    n[0] = e1[1] * e2[2] - e1[2] * e2[1];
    n[1] = e1[2] * e2[0] - e1[0] * e2[2];
    n[2] = e1[0] * e2[1] - e1[1] * e2[0];
    float d = n[0] * (ctr[0] - p[0][0]) + n[1] * (ctr[1] - p[0][1]) + n[2] * (ctr[2] - p[0][2]);
    bool flip = inward ? d < 0 : d > 0;
    static const int fwd[6] = { 0, 1, 2, 0, 2, 3 };
    static const int bwd[6] = { 0, 3, 2, 0, 2, 1 };
    const int *idx = flip ? bwd : fwd;
    static const float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    for (int i = 0; i < 6; i++) {
        int k = idx[i];
        vtx(m, p[k][0], p[k][1], p[k][2], uv[k][0] * uvs, uv[k][1] * uvs, c);
    }
}

// An axis aligned box [lo, hi] as 6 quads (winding set by the caller).
void box(Mesh &floorCeil, Mesh *walls, const float lo[3], const float hi[3], float uvs, const float c[3], bool single, bool inward)
{
    const float ctr[3] = { (lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f };
    float x0 = lo[0], y0 = lo[1], z0 = lo[2], x1 = hi[0], y1 = hi[1], z1 = hi[2];
    Mesh &w = (single || !walls) ? floorCeil : *walls;
    const float f[4][3] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 } };
    const float t[4][3] = { { x0, y1, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x0, y1, z1 } };
    quad(floorCeil, f, uvs, c, ctr, inward);
    quad(floorCeil, t, uvs, c, ctr, inward);
    const float s0[4][3] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 } };
    const float s1[4][3] = { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } };
    const float s2[4][3] = { { x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 } };
    const float s3[4][3] = { { x1, y0, z0 }, { x1, y0, z1 }, { x1, y1, z1 }, { x1, y1, z0 } };
    quad(w, s0, uvs, c, ctr, inward); quad(w, s1, uvs, c, ctr, inward); quad(w, s2, uvs, c, ctr, inward); quad(w, s3, uvs, c, ctr, inward);
}

struct Run { LevelRecRun r; std::vector<float> mtx, verts; };

Run makeRun(uint32_t owner, bool textured, int tex, const Mesh &m)
{
    Run run; memset(&run.r, 0, sizeof run.r);
    LevelRecRun &r = run.r;
    r.owner = owner;
    r.shader_id0 = textured ? 0x0108ull : 0x1000ull;
    r.shader_id1 = 0;
    r.tex[0] = textured ? tex : -1; r.tex[1] = -1;
    r.linear[0] = 1; r.mipmaps[0] = textured ? 1 : 0;
    r.depth_test = 1; r.depth_update = 1; r.depth_compare = 1; r.depth_source_prim = 0;
    r.zmode = 0;   // ZMODE_OPA
    r.use_alpha = 0; r.modulate = 0;
    r.cull = 1; r.fog_from_z = 0;   // back faces culled: a room seen from outside is open
    r.nverts = (uint32_t)(m.v.size() / (size_t)m.stride);
    r.nmtx = 1;
    r.nfloats = (uint32_t)m.v.size();
    run.mtx = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    run.verts = m.v;
    return run;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: make_test_rec OUTDIR\n"); return 2; }
    std::string dir = argv[1];

    uint8_t ca[3] = { 170, 170, 160 }, cb[3] = { 110, 110, 105 };
    uint8_t wa[3] = { 160, 90, 70 }, wb[3] = { 120, 60, 45 };
    uint8_t ga[3] = { 90, 150, 100 }, gb[3] = { 60, 110, 70 };
    std::vector<Tex> texs = { makeChecker(32, 32, 8, ca, cb), makeChecker(32, 32, 4, wa, wb), makeChecker(32, 32, 8, ga, gb) };

    // Three rooms in an L: A [0..1000], B [1000..2000] along x, C above B along z.
    struct RoomDef { int index; float lo[3], hi[3]; } rooms[3] = {
        { 1, { 0, 0, 0 },       { 1000, 300, 800 } },
        { 2, { 1000, 0, 0 },    { 2000, 300, 800 } },
        { 3, { 1000, 0, 800 },  { 2000, 300, 1600 } },
    };
    const float white[3] = { 1, 1, 1 };

    // Owners: rooms first, then the model owners (guard model 3, crate model 120).
    std::vector<LevelRecOwner> owners;
    for (auto &r : rooms) {
        LevelRecOwner o; memset(&o, 0, sizeof o);
        o.kind = LEVELREC_OWNER_ROOM; o.id = r.index; snprintf(o.name, sizeof o.name, "room%d", r.index);
        owners.push_back(o);
    }
    { LevelRecOwner o; memset(&o, 0, sizeof o); o.kind = LEVELREC_OWNER_MODEL; o.id = 3;   snprintf(o.name, sizeof o.name, "guard"); owners.push_back(o); }
    { LevelRecOwner o; memset(&o, 0, sizeof o); o.kind = LEVELREC_OWNER_MODEL; o.id = 120; snprintf(o.name, sizeof o.name, "crate"); owners.push_back(o); }
    const uint32_t ownerGuard = 3, ownerCrate = 4;

    std::vector<Run> runs;
    for (int i = 0; i < 3; i++) {
        Mesh fc; fc.stride = 9; Mesh w; w.stride = 9;
        box(fc, &w, rooms[i].lo, rooms[i].hi, 4.0f, white, false, true);
        runs.push_back(makeRun((uint32_t)i, true, i == 2 ? 2 : 0, fc));
        runs.push_back(makeRun((uint32_t)i, true, 1, w));
    }
    {   // guard: a tall coloured box with a head; model local, feet at y 0, centred on x/z
        Mesh m; m.stride = 7; Mesh dummy; dummy.stride = 7;
        const float blue[3] = { 0.25f, 0.35f, 0.8f }, skin[3] = { 0.9f, 0.7f, 0.55f };
        const float b0[3] = { -25, 0, -15 }, b1[3] = { 25, 120, 15 };
        box(m, nullptr, b0, b1, 1.0f, blue, true, false);
        const float h0[3] = { -12, 120, -12 }, h1[3] = { 12, 150, 12 };
        box(m, nullptr, h0, h1, 1.0f, skin, true, false);
        runs.push_back(makeRun(ownerGuard, false, -1, m));
    }
    {   // crate
        Mesh m; m.stride = 7;
        const float brown[3] = { 0.6f, 0.4f, 0.2f };
        const float c0[3] = { -30, 0, -30 }, c1[3] = { 30, 60, 30 };
        box(m, nullptr, c0, c1, 1.0f, brown, true, false);
        runs.push_back(makeRun(ownerCrate, false, -1, m));
    }

    // ---- level.rec ----
    std::string recPath = dir + "/level.rec";
    FILE *f = fopen(recPath.c_str(), "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", recPath.c_str()); return 1; }
    LevelRecHeader h; memset(&h, 0, sizeof h);
    h.magic = LEVELREC_MAGIC; h.version = LEVELREC_VERSION;
    snprintf(h.build, sizeof h.build, "synthetic-test");
    snprintf(h.level, sizeof h.level, "bunker1");
    h.ntextures = (uint32_t)texs.size(); h.nowners = (uint32_t)owners.size(); h.nruns = (uint32_t)runs.size();
    bool ok = levelrecWrite(f, &h, sizeof h);
    for (auto &t : texs) {
        LevelRecTexture lt; lt.hash = t.hash; lt.w = t.w; lt.h = t.h;
        ok = ok && levelrecWrite(f, &lt, sizeof lt) && levelrecWrite(f, t.px.data(), t.px.size());
    }
    ok = ok && levelrecWrite(f, owners.data(), owners.size() * sizeof(LevelRecOwner));
    for (auto &r : runs)
        ok = ok && levelrecWrite(f, &r.r, sizeof r.r) && levelrecWrite(f, r.mtx.data(), r.mtx.size() * 4)
                && levelrecWrite(f, r.verts.data(), r.verts.size() * 4);
    fclose(f);
    if (!ok) { fprintf(stderr, "write failed\n"); return 1; }

    // ---- level.json ----
    std::string jsonPath = dir + "/level.json";
    f = fopen(jsonPath.c_str(), "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", jsonPath.c_str()); return 1; }
    fprintf(f, "{ \"version\": 1, \"level\": \"bunker1\", \"build\": \"synthetic-test\", \"setup\": \"UsetupsynthZ\", \"scale\": 1.0,\n  \"rooms\": [\n");
    for (int i = 0; i < 3; i++)
        fprintf(f, "    { \"index\": %d, \"min\": [%g,%g,%g], \"max\": [%g,%g,%g] }%s\n", rooms[i].index,
                rooms[i].lo[0], rooms[i].lo[1], rooms[i].lo[2], rooms[i].hi[0], rooms[i].hi[1], rooms[i].hi[2], i < 2 ? "," : "");
    fprintf(f, "  ],\n  \"stan\": [\n");
    for (int i = 0; i < 3; i++)
        fprintf(f, "    { \"room\": %d, \"v\": [[%g,0,%g],[%g,0,%g],[%g,0,%g],[%g,0,%g]] }%s\n", rooms[i].index,
                rooms[i].lo[0], rooms[i].lo[2], rooms[i].hi[0], rooms[i].lo[2], rooms[i].hi[0], rooms[i].hi[2], rooms[i].lo[0], rooms[i].hi[2], i < 2 ? "," : "");
    fprintf(f, "  ],\n  \"portals\": [\n"
               "    { \"rooms\": [1, 2], \"v\": [[1000,0,300],[1000,0,500],[1000,220,500],[1000,220,300]] },\n"
               "    { \"rooms\": [2, 3], \"v\": [[1400,0,800],[1600,0,800],[1600,220,800],[1400,220,800]] }\n  ],\n");
    struct PadDef { float p[3]; } pads[] = {     // pad numbers are the indices 0..6
        { { 200, 0, 200 } }, { { 700, 0, 600 } }, { { 500, 0, 400 } },
        { { 1300, 0, 200 } }, { { 1800, 0, 600 } },
        { { 1200, 0, 1200 } }, { { 1750, 0, 1500 } },
    };
    const int npads = (int)(sizeof pads / sizeof pads[0]);
    // which recorded model draws which model id: guard model 3, crate model 120
    fprintf(f, "  \"models\": [\n    { \"guard\": true, \"model\": 3, \"owner\": %u },\n    { \"guard\": false, \"model\": 120, \"owner\": %u }\n  ]\n}\n",
            ownerGuard, ownerCrate);
    fclose(f);

    // ---- the synthetic setup row (PC layout, see port/mission/mission_apply.h) ----
    {
        std::vector<uint8_t> in(0x50, 0);
        auto w32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; i++) in[o + i] = (uint8_t)(v >> (8 * i)); };
        auto wf = [&](size_t o, float v) { uint32_t u; memcpy(&u, &v, 4); w32(o, u); };
        auto w16 = [&](size_t o, unsigned v) { in[o] = (uint8_t)v; in[o + 1] = (uint8_t)(v >> 8); };
        auto grow = [&](size_t n) { size_t o = in.size(); in.resize(o + n, 0); return o; };
        const size_t ailOff = grow(4 * 16);                       // 3 lists + terminator
        const uint32_t ail[3] = { 1025, 1026, 1027 };
        for (int i = 0; i < 3; i++) { w32(ailOff + 16 * i, 1); w32(ailOff + 16 * i + 8, ail[i]); }
        const size_t padOff = grow((size_t)(npads + 1) * 56);
        for (int i = 0; i < npads; i++) {
            const size_t o = padOff + (size_t)i * 56;
            for (int a = 0; a < 3; a++) wf(o + 4 * a, pads[i].p[a]);
            wf(o + 16, 1.0f); wf(o + 24 + 8, 1.0f);              // up +y, look +z
            w32(o + 0x28, 1);                                      // a non-null tile name slot
        }
        const size_t propOff = in.size();
        struct P { int type; int pad; int model; int ai; int item; } props[] = {
            { 9, 1, 3, 1025, -1 }, { 9, 3, 3, 1026, -1 }, { 9, 5, 3, 1027, -1 },
            { 3, 2, 120, -1, -1 }, { 3, 4, 120, -1, -1 }, { 4, 6, 999, -1, 33 },
        };
        for (const P &p : props) {
            const size_t sz = p.type == 9 ? 32 : p.type == 4 ? 152 : 144;
            const size_t o = grow(sz);
            in[o + 3] = (uint8_t)p.type;
            if (p.type == 9) { w16(o + 6, (unsigned)p.pad); w16(o + 8, (unsigned)p.model); w16(o + 10, (unsigned)p.ai); w16(o + 16, 100); }
            else { w16(o, 0x100); w16(o + 4, (unsigned)p.model); w16(o + 6, (unsigned)p.pad); w32(o + 8, 1); wf(o + 132, 0); w32(o + 132, 1000u << 16); }
            if (p.type == 4) w32(o + 144, (uint32_t)p.item);
        }
        const size_t endOff = grow(4);
        in[endOff + 3] = 48;
        grow(8);
        w32(8 * 3, (uint32_t)propOff); w32(8 * 5, (uint32_t)ailOff); w32(8 * 6, (uint32_t)padOff);
        // RZ: 0x11 0x72 + a stored deflate block
        std::vector<uint8_t> rz = { 0x11, 0x72, 1, (uint8_t)in.size(), (uint8_t)(in.size() >> 8), (uint8_t)~in.size(), (uint8_t)(~in.size() >> 8) };
        rz.insert(rz.end(), in.begin(), in.end());
        const std::string sdir = dir + "/data/pccg-ntsc-final";
        std::filesystem::create_directories(sdir);
        FILE *b = fopen((sdir + "/pccg.bin").c_str(), "wb");
        FILE *m = fopen((sdir + "/manifest.csv").c_str(), "wb");
        if (!b || !m) { fprintf(stderr, "cannot write %s\n", sdir.c_str()); return 1; }
        fwrite(rz.data(), 1, rz.size(), b);
        fprintf(m, "name,offset,size\nUsetupsynthZ,0,%zu\n", rz.size());
        fclose(b); fclose(m);
        printf("wrote the synthetic setup %s (set GE_EDITOR_DATA=%s/data)\n", sdir.c_str(), dir.c_str());
    }
    printf("wrote %s and %s (%zu runs, %zu textures)\n", recPath.c_str(), jsonPath.c_str(), runs.size(), texs.size());
    if (!synth::writeGltf(dir + "/gltf", "bunker1", EX_EXPORTER_VERSION)) { fprintf(stderr, "cannot write the synthetic glTF under %s/gltf\n", dir.c_str()); return 1; }
    printf("wrote the synthetic glTF decor %s/gltf/bunker1/bunker1.gltf\n", dir.c_str());
    return 0;
}
