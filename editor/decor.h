// The level decor (rooms) read from the static glTF export (port/export/, issue #150): levels/<level>/<level>.gltf + .bin inside the asset
// pack ge007.passets (a tar or a folder; the path may run through it, it is read with ge_pkg), its pictures textures/N.png in the pack.
// Pure CPU, no GL: Scene uploads it, Level turns its triangles into the picking set. Vertices are in world units
// (the node translation is applied here), colours are the game's own sRGB-space shade values (the glTF holds them linear).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "texset.h"

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
    std::string key;             // the texture key (material extras.texture): the game's texture number in decimal, or a mod's own key; "" when unknown
    int texnum = -1;             // the game's texture number (the key as a number), -1 for a mod's own key / no key
    int origW = 0, origH = 0;    // the ROM picture's size (the entry's width / height), = w, h when not known
    std::string path;            // the file the pixels came from (inside a .passets / .pmod, or the glTF's own picture)
    int source = -1;             // -1 the game's own picture (ge007.passets, or the glTF's own), 0 the open mod's, 1 another texture pack's
    int surface = -1;            // the resolved surface class (texpackSurfaceName), -1 unknown
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
// texs: the sources a texture is resolved through, by its key (material extras.texture; a glTF without it: the number in the image
// file name): the open mod merged over the chosen texture pack (texset.h, docs/specs/texture-packs.md), at any size: the glTF's
// texture coordinates are 0..1 of the picture. The glTF's own image is read only when no source has the key. An unreadable picture
// is skipped and listed in Decor::texWarnings.
// `model`: a static model export (models/<name>/<name>.gltf, issue #119): no version check, only the default scene's nodes, vertices in
// model units with the rest pose of their joints applied (node transforms), no stan, no rooms.
DecorResult decorLoad(const std::string &gltfPath, int expectVersion, Decor &out, std::string &msg,
                      const TexSet *texs = nullptr, bool model = false);

// The texture number of a picture file name "<n>.png" (any folder before it), -1 when it is not one.
int decorTexnumOfFile(const std::string &name);
