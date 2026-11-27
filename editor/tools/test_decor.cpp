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

    // texture sources (texpacks.h, texset.h): the game's pack ge007.ptexpack (a folder), a mod folder and a complete hd.ptexpack tar,
    // each with a picture of texture "1" (the glTF's image is textures/1.png): the mod over the chosen pack, any size
    {
        auto solid = [](int w, int h, uint8_t r, uint8_t g, uint8_t b) {
            std::vector<uint8_t> px((size_t)w * h * 4);
            for (size_t i = 0; i < px.size(); i += 4) { px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = 255; }
            return px;
        };
        auto put = [](const std::string &path, const char *text) {
            FILE *f = fopen(path.c_str(), "wb");
            CHECK(f != nullptr);
            if (f) { fputs(text, f); fclose(f); }
        };
        const std::string base = dir + "/gebase/ge007.ptexpack", mod = dir + "/mymod", packSrc = dir + "/packsrc", packs = dir + "/texturepacks";
        const std::string pack = packs + "/hd.ptexpack", lack = dir + "/lack.ptexpack";
        for (const std::string &p : { base + "/textures", mod + "/textures", packSrc + "/textures", lack, packs }) std::filesystem::create_directories(p, ec);
        const char *oneJson = "{\"version\":1,\"textures\":{\"1\":{\"image\":\"textures/1.png\",\"surface\":\"stone\",\"impact_sound\":\"stone\",\"alpha\":\"image\",\"width\":8,\"height\":8}}}";
        put(base + "/textures.json", oneJson);
        put(base + "/pack.json", "{\"name\":\"GoldenEye 007\",\"base\":true}");
        CHECK(synth::writePngStored(base + "/textures/1.png", 8, 8, solid(8, 8, 1, 2, 3)));
        CHECK(synth::writePngStored(mod + "/textures/1.png", 16, 32, solid(16, 32, 200, 10, 10)));
        CHECK(synth::writePngStored(packSrc + "/textures/1.png", 64, 64, solid(64, 64, 10, 200, 10)));
        put(packSrc + "/textures.json", "{\"version\":1,\"textures\":{\"1\":{\"surface\":\"wood\",\"width\":8,\"height\":8}}}");
        put(packSrc + "/pack.json", "{\"name\": \"HD test\", \"author\": \"tests\"}");
        put(lack + "/textures.json", "{\"version\":1,\"textures\":{}}");
        char err[256] = "";
        CHECK(pkgWriteDir(packSrc.c_str(), pack.c_str(), err, sizeof err));

        TexPackInfo info[4];
        CHECK(texpackScan(packs.c_str(), info, 4) == 1);
        CHECK(!strcmp(info[0].file, "hd.ptexpack") && !strcmp(info[0].name, "HD test") && !strcmp(info[0].author, "tests"));

        const std::vector<PackChoice> list = listTexturePacks(packs, dir + "/gebase");   // ge007 from the export root, then hd
        CHECK(list.size() == 2 && list[0].base && list[0].usable() && list[0].name == "GoldenEye 007" && list[1].file == "hd.ptexpack" && list[1].usable());
        CHECK(listTexturePacks(packs, "").size() == 2 && !listTexturePacks(packs, "")[0].usable() && !listTexturePacks(packs, "")[1].usable());
        CHECK(baseTexturePackRoot(packs, dir + "/gebase") == base && baseTexturePackRoot(packs, "").empty());

        TexSet ts;
        ts.open(mod, pack, base);
        CHECK(decorLoad(dir + "/lvl/lvl.gltf", EX_EXPORTER_VERSION, d, msg, &ts) == DECOR_OK);
        CHECK(d.tex.size() == 1);
        if (d.tex.size() == 1) {   // the mod's picture: its own size, the ROM size kept beside it
            CHECK(d.tex[0].source == 0 && d.tex[0].w == 16 && d.tex[0].h == 32 && d.tex[0].origW == 8 && d.tex[0].origH == 8 && d.tex[0].key == "1");
            CHECK(d.tex[0].rgba.size() == 16 * 32 * 4 && d.tex[0].rgba[0] == 200 && d.tex[0].rgba[1] == 10);
            CHECK(d.tex[0].surface == 2);   // the pack's wood: the mod gave no surface
        }
        ts.open("", pack, base);   // the pack alone, from inside the tar
        CHECK(decorLoad(dir + "/lvl/lvl.gltf", EX_EXPORTER_VERSION, d, msg, &ts) == DECOR_OK);
        if (d.tex.size() == 1) CHECK(d.tex[0].source == 1 && d.tex[0].w == 64 && d.tex[0].rgba[1] == 200);
        ts.open("", base, base);   // ge007 chosen: the game's own picture
        CHECK(decorLoad(dir + "/lvl/lvl.gltf", EX_EXPORTER_VERSION, d, msg, &ts) == DECOR_OK);
        if (d.tex.size() == 1) CHECK(d.tex[0].source == -1 && d.tex[0].w == 8 && d.tex[0].rgba[0] == 1 && d.tex[0].surface == 1 && d.texWarnings.empty());
        ts.open("", lack, base);   // an incomplete pack is refused: ge007 is used
        CHECK(!ts.note().empty() && ts.pack() == ts.base());
        ts.open("", pack, "");     // no ge007 to check a pack against: no pack
        CHECK(!ts.note().empty() && ts.pack() == nullptr);

        // a mod's textures.json alone: the surface changes, the picture stays the pack's
        const std::string mod2 = dir + "/mymod2";
        std::filesystem::create_directories(mod2, ec);
        put(mod2 + "/textures.json", "{\"version\":1,\"textures\":{\"1\":{\"surface\":\"metal\"}}}");
        ts.open(mod2, base, base);
        CHECK(decorLoad(dir + "/lvl/lvl.gltf", EX_EXPORTER_VERSION, d, msg, &ts) == DECOR_OK);
        if (d.tex.size() == 1) CHECK(d.tex[0].source == -1 && d.tex[0].w == 8 && d.tex[0].surface == 3);

        // alpha "luminance": an opaque picture draws its luminance as alpha
        const std::string mod3 = dir + "/mymod3";
        std::filesystem::create_directories(mod3 + "/textures", ec);
        put(mod3 + "/textures.json", "{\"version\":1,\"textures\":{\"1\":{\"alpha\":\"luminance\"}}}");
        CHECK(synth::writePngStored(mod3 + "/textures/1.png", 4, 4, solid(4, 4, 100, 200, 50)));
        ts.open(mod3, base, base);
        CHECK(decorLoad(dir + "/lvl/lvl.gltf", EX_EXPORTER_VERSION, d, msg, &ts) == DECOR_OK);
        if (d.tex.size() == 1) CHECK(d.tex[0].source == 0 && d.tex[0].rgba[3] == (299 * 100 + 587 * 200 + 114 * 50) / 1000);

        {   // an unreadable mod picture: skipped with a warning, the pack's is used
            put(mod + "/textures/1.png", "not a png");
        }
        ts.open(mod, pack, base);
        CHECK(decorLoad(dir + "/lvl/lvl.gltf", EX_EXPORTER_VERSION, d, msg, &ts) == DECOR_OK);
        if (d.tex.size() == 1) CHECK(d.tex[0].source == 1 && d.tex[0].w == 64);
        CHECK(d.texWarnings.size() == 1);

        // a key no source has: the glTF's own picture, one warning
        put(base + "/textures.json", "{\"version\":1,\"textures\":{\"7\":{\"image\":null,\"surface\":\"stone\"}}}");
        ts.open("", "", "");   // (the base was cached empty above: a fresh set)
        TexSet fresh;
        fresh.open("", base, base);
        CHECK(decorLoad(dir + "/lvl/lvl.gltf", EX_EXPORTER_VERSION, d, msg, &fresh) == DECOR_OK);
        if (d.tex.size() == 1) CHECK(d.tex[0].source == -1 && d.tex[0].w == 8);
        CHECK(d.texWarnings.size() == 1);
        CHECK(decorLoad(dir + "/lvl/lvl.gltf", EX_EXPORTER_VERSION, d, msg) == DECOR_OK);   // no sources at all: the glTF's own, no warning
        if (d.tex.size() == 1) CHECK(d.tex[0].source == -1 && d.tex[0].w == 8 && d.texWarnings.empty());
        CHECK(texpackTexnumOfName("textures/4095.png") == 4095 && texpackTexnumOfName("4096.png") == -1 && texpackTexnumOfName("a1.png") == -1);
    }

    std::filesystem::remove_all(dir, ec);
    if (g_fail) { fprintf(stderr, "%d check(s) failed\n", g_fail); return 1; }
    printf("test_decor: ok\n");
    return 0;
}
