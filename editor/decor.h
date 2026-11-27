// The level decor (rooms) read from the static glTF export (port/export/, issue #150): <level>.gltf + .bin + textures/N.png.
// Pure CPU, no GL: Scene uploads it, Level turns its triangles into the picking set. Vertices are in world units
// (the node translation is applied here), colours are the game's own sRGB-space shade values (the glTF holds them linear).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct DecorMaterial {
    int tex = -1;                // index into Decor::tex, -1 untextured
    int alphaMode = 0;           // 0 OPAQUE, 1 MASK (alpha test 0.5), 2 BLEND
    bool doubleSided = false;    // no culling; else back faces are culled (counter-clockwise = front)
    int cms = 0, cmt = 0;        // N64 wrap (G_TX_*): 0 repeat, 1 mirror, 2 clamp
    float factor[4] = { 1, 1, 1, 1 };   // pbrMetallicRoughness.baseColorFactor (alpha = the real opacity of a translucent material); rgb in gamma space
    int zmode = 0;               // the RDP z mode from extras.n64.othermode (0x000 opaque, 0x400 inter, 0x800 translucent, 0xC00 decal: depth offset)
};

struct DecorTex {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;   // top-down RGBA8
    int texnum = -1;             // the game's texture number (the image file is textures/<texnum>.png), -1 when the name is not one
    int origW = 0, origH = 0;    // the exported picture's size (= w, h unless the mod replaces it)
    std::string path;            // the file the pixels came from (a replacement, maybe inside a .ptexpack / .pmod, or the export's picture)
    int source = -1;             // the index in decorLoad's texRoots the pixels came from, -1 the export's own picture
};

struct DecorPrim {
    int room = -1;               // node extras.room
    int material = 0;            // index into Decor::materials
    std::vector<float> pos;      // xyz per vertex, world
    std::vector<float> uv;       // uv per vertex, empty when untextured
    std::vector<float> col;      // rgba per vertex, 0..1, gamma (sRGB) space
    std::vector<uint32_t> idx;   // triangle list
};

// One collision floor tile ("stan", docs/specs/stan.md) from the glTF's hidden `collision` node.
struct DecorStanTile {
    int index = 0;               // level-wide tile index (the neighbour values refer to it)
    int room = -1;
    int kind = 0;                // 0 normal floor, others special (crouch, ladder, ...)
    std::vector<float> pt;       // xyz per point, world units (raw s16 point / level_scale)
    std::vector<int> nb;         // per edge i (point i -> i+1): the neighbour tile index, -1 = no neighbour (a wall)
};

struct Decor {
    std::vector<DecorStanTile> stan;   // empty when the glTF has no collision node
    std::vector<DecorMaterial> materials;
    std::vector<DecorTex> tex;
    std::vector<DecorPrim> prims;
    std::vector<std::string> texWarnings;   // a replacement that could not be read (the next source, or the exported picture, is used)
    std::string level;
    int version = 0;
    float mn[3] = { 0, 0, 0 }, mx[3] = { 0, 0, 0 };
};

enum DecorResult { DECOR_OK, DECOR_MISSING, DECOR_STALE, DECOR_BAD };

// Loads <path>.gltf with its .bin and PNGs. DECOR_MISSING: no such file; DECOR_STALE: asset.extras.exporter_version differs from
// `expectVersion` (nothing else is read); DECOR_BAD: unreadable (msg says why). msg is also set for the other results.
// texRoots: replacement sources, first wins (the open mod's folder, then the chosen texture pack "<texturepacks>/X.ptexpack");
// the picture textures/<n>.png is read from <root>/textures/<n>.png when it is there (a .pmod / .ptexpack tar or a folder,
// texpacks.h), at any size: the glTF's texture coordinates are 0..1 of the picture. An unreadable one is skipped and listed in
// Decor::texWarnings.
// `model`: a static model export (models/<name>/<name>.gltf, issue #119): no version check, only the default scene's nodes, vertices in
// model units with the rest pose of their joints applied (node transforms), no stan, no rooms.
DecorResult decorLoad(const std::string &gltfPath, int expectVersion, Decor &out, std::string &msg,
                      const std::vector<std::string> &texRoots = {}, bool model = false);

// The texture number of a picture file name "<n>.png" (any folder before it), -1 when it is not one.
int decorTexnumOfFile(const std::string &name);
