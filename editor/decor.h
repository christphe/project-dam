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

struct DecorTex { int w = 0, h = 0; std::vector<uint8_t> rgba; };   // top-down RGBA8

struct DecorPrim {
    int room = -1;               // node extras.room
    int material = 0;            // index into Decor::materials
    std::vector<float> pos;      // xyz per vertex, world
    std::vector<float> uv;       // uv per vertex, empty when untextured
    std::vector<float> col;      // rgba per vertex, 0..1, gamma (sRGB) space
    std::vector<uint32_t> idx;   // triangle list
};

struct Decor {
    std::vector<DecorMaterial> materials;
    std::vector<DecorTex> tex;
    std::vector<DecorPrim> prims;
    std::string level;
    int version = 0;
    float mn[3] = { 0, 0, 0 }, mx[3] = { 0, 0, 0 };
};

enum DecorResult { DECOR_OK, DECOR_MISSING, DECOR_STALE, DECOR_BAD };

// Loads <path>.gltf with its .bin and PNGs. DECOR_MISSING: no such file; DECOR_STALE: asset.extras.exporter_version differs from
// `expectVersion` (nothing else is read); DECOR_BAD: unreadable (msg says why). msg is also set for the other results.
DecorResult decorLoad(const std::string &gltfPath, int expectVersion, Decor &out, std::string &msg);
