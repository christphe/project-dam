// Test of the glTF decor loader (editor/decor.cpp) on a synthetic glTF written here (tools/synth_gltf.h): no ROM data.
//   test_decor <scratch dir>
#include "decor.h"
#include "export.h"
#include "synth_gltf.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

extern "C" {
#include "mission_pkg.h"
#include "texpacks.h"
}

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
    if (d.tex.size() == 1) CHECK(d.tex[0].texnum == 1 && d.tex[0].source == -1 && d.tex[0].origW == 8);

    // texture sources (texpacks.h): a mod folder and a .ptexpack tar, each with textures/1.png; the first listed wins, any size
    {
        auto solid = [](int w, int h, uint8_t r, uint8_t g, uint8_t b) {
            std::vector<uint8_t> px((size_t)w * h * 4);
            for (size_t i = 0; i < px.size(); i += 4) { px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = 255; }
            return px;
        };
        const std::string mod = dir + "/mymod", packSrc = dir + "/packsrc", packs = dir + "/texturepacks", pack = packs + "/hd.ptexpack";
        std::filesystem::create_directories(mod + "/textures", ec);
        std::filesystem::create_directories(packSrc + "/textures", ec);
        std::filesystem::create_directories(packs, ec);
        CHECK(synth::writePngStored(mod + "/textures/1.png", 16, 32, solid(16, 32, 200, 10, 10)));
        CHECK(synth::writePngStored(packSrc + "/textures/1.png", 64, 64, solid(64, 64, 10, 200, 10)));
        {
            FILE *f = fopen((packSrc + "/pack.json").c_str(), "wb");
            CHECK(f != nullptr);
            if (f) { fputs("{\"name\": \"HD test\", \"author\": \"tests\"}", f); fclose(f); }
        }
        char err[256] = "";
        CHECK(pkgWriteDir(packSrc.c_str(), pack.c_str(), err, sizeof err));

        TexPackInfo info[4];
        CHECK(texpackScan(packs.c_str(), info, 4) == 1);
        CHECK(!strcmp(info[0].file, "hd.ptexpack") && !strcmp(info[0].name, "HD test") && !strcmp(info[0].author, "tests"));

        CHECK(decorLoad(dir + "/lvl/lvl.gltf", EX_EXPORTER_VERSION, d, msg, { mod, pack }) == DECOR_OK);
        CHECK(d.tex.size() == 1);
        if (d.tex.size() == 1) {   // the mod's picture: its own size, the export's size kept beside it
            CHECK(d.tex[0].source == 0 && d.tex[0].w == 16 && d.tex[0].h == 32 && d.tex[0].origW == 8 && d.tex[0].origH == 8);
            CHECK(d.tex[0].rgba.size() == 16 * 32 * 4 && d.tex[0].rgba[0] == 200 && d.tex[0].rgba[1] == 10);
        }
        CHECK(decorLoad(dir + "/lvl/lvl.gltf", EX_EXPORTER_VERSION, d, msg, { pack }) == DECOR_OK);   // the pack alone, from inside the tar
        if (d.tex.size() == 1) CHECK(d.tex[0].source == 0 && d.tex[0].w == 64 && d.tex[0].rgba[1] == 200);

        {   // an unreadable replacement: skipped with a warning, the next source is used
            FILE *f = fopen((mod + "/textures/1.png").c_str(), "wb");
            if (f) { fputs("not a png", f); fclose(f); }
        }
        CHECK(decorLoad(dir + "/lvl/lvl.gltf", EX_EXPORTER_VERSION, d, msg, { mod, pack }) == DECOR_OK);
        if (d.tex.size() == 1) CHECK(d.tex[0].source == 1 && d.tex[0].w == 64);
        CHECK(d.texWarnings.size() == 1);
        CHECK(decorLoad(dir + "/lvl/lvl.gltf", EX_EXPORTER_VERSION, d, msg, { mod }) == DECOR_OK);   // nothing readable: the export's
        if (d.tex.size() == 1) CHECK(d.tex[0].source == -1 && d.tex[0].w == 8);
        CHECK(texpackTexnumOfName("textures/4095.png") == 4095 && texpackTexnumOfName("4096.png") == -1 && texpackTexnumOfName("a1.png") == -1);
    }

    std::filesystem::remove_all(dir, ec);
    if (g_fail) { fprintf(stderr, "%d check(s) failed\n", g_fail); return 1; }
    printf("test_decor: ok\n");
    return 0;
}
