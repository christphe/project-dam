// Test of the glTF decor loader (editor/decor.cpp) on a synthetic glTF written here (tools/synth_gltf.h): no ROM data.
//   test_decor <scratch dir>
#include "decor.h"
#include "export.h"
#include "synth_gltf.h"

#include <cmath>
#include <cstdio>
#include <filesystem>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
static bool near(float a, float b, float e) { return fabsf(a - b) <= e; }

int main(int argc, char **argv)
{
    const std::string dir = argc > 1 ? argv[1] : "test_decor_tmp";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    Decor d;
    std::string msg;

    CHECK(decorLoad(dir + "/nothing/nothing.gltf", EX_EXPORTER_VERSION, d, msg) == DECOR_MISSING);

    CHECK(synth::writeGltf(dir, "stale", EX_EXPORTER_VERSION + 1));
    CHECK(decorLoad(dir + "/stale/stale.gltf", EX_EXPORTER_VERSION, d, msg) == DECOR_STALE);
    CHECK(d.version == EX_EXPORTER_VERSION + 1);
    CHECK(d.prims.empty());   // a stale export is not read further

    CHECK(synth::writeGltf(dir, "lvl", EX_EXPORTER_VERSION));
    CHECK(decorLoad(dir + "/lvl/lvl.gltf", EX_EXPORTER_VERSION, d, msg) == DECOR_OK);
    CHECK(d.level == "lvl");
    CHECK(d.prims.size() == 4);
    CHECK(d.materials.size() == 4);
    CHECK(d.tex.size() == 1 && d.tex[0].w == 8 && d.tex[0].h == 8 && d.tex[0].rgba.size() == 8 * 8 * 4);
    if (d.tex.size() == 1) CHECK(d.tex[0].rgba[0] == 40 && d.tex[0].rgba[1] == 90 && d.tex[0].rgba[2] == 120 && d.tex[0].rgba[3] == 255);

    if (d.prims.size() == 4) {
        // textured opaque floor of room 1: the node translation (100, 0, 0) is applied, uv and colour kept
        const DecorPrim &floor = d.prims[0];
        CHECK(floor.room == 1);
        CHECK(floor.pos.size() == 12 && floor.idx.size() == 6 && floor.uv.size() == 8 && floor.col.size() == 16);
        CHECK(near(floor.pos[0], -100, 1e-4f) && near(floor.pos[1], 0, 1e-4f) && near(floor.pos[2], -200, 1e-4f));
        CHECK(near(floor.pos[6], 300, 1e-4f) && near(floor.pos[8], 200, 1e-4f));
        CHECK(near(floor.col[0], 1.0f, 0.002f) && near(floor.col[3], 1.0f, 0.002f));
        const DecorMaterial &m0 = d.materials[(size_t)floor.material];
        CHECK(m0.tex == 0 && m0.alphaMode == 0 && !m0.doubleSided && m0.cms == 0 && m0.cmt == 2 && m0.zmode == 0);

        // red wall: MASK, double sided, untextured; the linear colour comes back as the sRGB value the game's byte had
        const DecorPrim &wall = d.prims[1];
        const DecorMaterial &m1 = d.materials[(size_t)wall.material];
        CHECK(m1.alphaMode == 1 && m1.doubleSided && m1.tex < 0 && wall.uv.empty());
        CHECK(near(wall.col[0], 0.5f, 0.003f) && near(wall.col[1], 0.1f, 0.003f) && near(wall.col[2], 0.1f, 0.003f));

        // glass: BLEND, alpha kept as is, translucent z mode
        const DecorPrim &glass = d.prims[2];
        const DecorMaterial &m2 = d.materials[(size_t)glass.material];
        CHECK(m2.alphaMode == 2 && !m2.doubleSided && m2.zmode == 0x800);
        CHECK(near(glass.col[3], 0.5f, 0.001f));
        CHECK(near(glass.col[0], 0.8f, 0.003f) && near(glass.col[2], 1.0f, 0.003f));

        // decal of room 2: translation (0, 0, 300), z mode DEC
        const DecorPrim &decal = d.prims[3];
        CHECK(decal.room == 2);
        CHECK(near(decal.pos[2], 300, 1e-4f) && near(decal.pos[8], 400, 1e-4f) && near(decal.pos[1], 1, 1e-4f));
        CHECK(d.materials[(size_t)decal.material].zmode == 0xC00);
    }
    CHECK(near(d.mn[0], -100, 1e-3f) && near(d.mx[0], 300, 1e-3f) && near(d.mx[2], 400, 1e-3f));

    std::filesystem::remove_all(dir, ec);
    if (g_fail) { fprintf(stderr, "%d check(s) failed\n", g_fail); return 1; }
    printf("test_decor: ok\n");
    return 0;
}
