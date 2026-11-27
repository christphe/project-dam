// A SYNTHETIC glTF decor export (no ROM data), in the layout of the static level export (port/export/, issue #150):
// <root>/<level>/<level>.gltf + .bin + textures/1.png. Two rooms with a textured opaque floor, a masked red wall, a blended glass pane
// and a decal. Used by make_test_rec (so the editor can show a glTF decor without a ROM) and by the decor loader's test.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace synth {

inline uint32_t crc32(const uint8_t *p, size_t n, uint32_t c = 0)
{
    c = ~c;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

// An RGBA8 PNG with stored (uncompressed) deflate blocks: no zlib needed to write it.
inline bool writePngStored(const std::string &path, int w, int h, const std::vector<uint8_t> &rgba)
{
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; y++) {
        raw.push_back(0);   // filter: none
        raw.insert(raw.end(), rgba.begin() + (size_t)y * w * 4, rgba.begin() + (size_t)(y + 1) * w * 4);
    }
    std::vector<uint8_t> z = { 0x78, 0x01 };
    for (size_t off = 0; off < raw.size();) {
        const size_t n = std::min<size_t>(65535, raw.size() - off);
        z.push_back(off + n == raw.size() ? 1 : 0);
        z.push_back((uint8_t)(n & 255)); z.push_back((uint8_t)(n >> 8));
        z.push_back((uint8_t)(~n & 255)); z.push_back((uint8_t)((~n >> 8) & 255));
        z.insert(z.end(), raw.begin() + (long)off, raw.begin() + (long)(off + n));
        off += n;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    const uint32_t ad = b << 16 | a;
    for (int s = 24; s >= 0; s -= 8) z.push_back((uint8_t)(ad >> s));
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    auto be32 = [&](uint32_t v) { const uint8_t b4[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v }; fwrite(b4, 1, 4, f); };
    auto chunk = [&](const char *type, const std::vector<uint8_t> &d) {
        be32((uint32_t)d.size());
        std::vector<uint8_t> td(type, type + 4);
        td.insert(td.end(), d.begin(), d.end());
        fwrite(td.data(), 1, td.size(), f);
        be32(crc32(td.data(), td.size()));
    };
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr = { (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                                  (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h, 8, 6, 0, 0, 0 };
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    fclose(f);
    return true;
}

inline float toLinear(float s) { return s <= 0.04045f ? s / 12.92f : powf((s + 0.055f) / 1.055f, 2.4f); }

// One quad lying in the xz plane at height y, corners (x0,z0)-(x1,z1), counter-clockwise seen from +y. `srgb` is the vertex colour as the
// game's byte values would be (0..1), written linear as the exporter does; alpha is kept.
struct Quad { float x0, z0, x1, z1, y; float srgb[3]; float alpha; int material; };

struct Room { int number; float t[3]; std::vector<Quad> quads; };

// Fixed contents (the test checks these numbers): returns false when a file cannot be written.
// materials: 0 textured OPAQUE (texture 1, repeat), 1 untextured MASK double sided, 2 untextured BLEND, 3 untextured OPAQUE decal (zmode 0xC00).
inline bool writeGltf(const std::string &root, const std::string &level, int exporterVersion)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(root) / level;
    std::error_code ec;
    fs::create_directories(dir / "textures", ec);
    std::vector<uint8_t> tex(8 * 8 * 4);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        uint8_t *p = &tex[(size_t)(y * 8 + x) * 4];
        const bool c = ((x / 2) + (y / 2)) & 1;
        p[0] = c ? 200 : 40; p[1] = c ? 190 : 90; p[2] = c ? 60 : 120; p[3] = 255;
    }
    if (!writePngStored((dir / "textures" / "1.png").string(), 8, 8, tex)) return false;

    std::vector<Room> rooms = {
        { 1, { 100.f, 0.f, 0.f }, { { -200, -200, 200, 200, 0, { 1, 1, 1 }, 1, 0 }, { -200, 200, 200, 200, 120, { 0.5f, 0.1f, 0.1f }, 1, 1 },
                                     { -100, -100, 100, 100, 60, { 0.8f, 0.8f, 1 }, 0.5f, 2 } } },
        { 2, { 0.f, 0.f, 300.f }, { { 0, 0, 100, 100, 1, { 0.3f, 0.3f, 0.3f }, 1, 3 } } },
    };

    std::vector<uint8_t> bin;
    std::string views, accs, meshes, nodes;
    int nview = 0, nacc = 0;
    auto addView = [&](const void *d, size_t n) {
        while (bin.size() % 4) bin.push_back(0);
        char b[160];
        snprintf(b, sizeof b, "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}", nview ? "," : "", bin.size(), n);
        views += b;
        bin.insert(bin.end(), (const uint8_t *)d, (const uint8_t *)d + n);
        return nview++;
    };
    auto addAcc = [&](int view, int comp, size_t count, const char *type, bool norm, const char *extra = "") {
        char b[220];
        snprintf(b, sizeof b, "%s{\"bufferView\":%d,\"componentType\":%d,\"count\":%zu,\"type\":\"%s\"%s%s}", nacc ? "," : "", view, comp, count, type,
                 norm ? ",\"normalized\":true" : "", extra);
        accs += b;
        return nacc++;
    };
    for (size_t ri = 0; ri < rooms.size(); ri++) {
        const Room &r = rooms[ri];
        std::string prims;
        for (size_t qi = 0; qi < r.quads.size(); qi++) {
            const Quad &q = r.quads[qi];
            const float pos[12] = { q.x0, q.y, q.z0, q.x0, q.y, q.z1, q.x1, q.y, q.z1, q.x1, q.y, q.z0 };
            // counter-clockwise from +y: (x0,z0) (x0,z1) (x1,z1) (x1,z0) is clockwise in x-right/z-down terms, which is CCW seen from above
            const float uv[8] = { 0, 0, 0, 1, 1, 1, 1, 0 };
            uint16_t col[16];
            for (int v = 0; v < 4; v++) {
                for (int k = 0; k < 3; k++) col[v * 4 + k] = (uint16_t)lroundf(toLinear(q.srgb[k]) * 65535.0f);
                col[v * 4 + 3] = (uint16_t)lroundf(q.alpha * 65535.0f);
            }
            const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
            char mm[96];
            snprintf(mm, sizeof mm, ",\"min\":[%g,%g,%g],\"max\":[%g,%g,%g]", q.x0, q.y, q.z0, q.x1, q.y, q.z1);
            const int ap = addAcc(addView(pos, sizeof pos), 5126, 4, "VEC3", false, mm);
            const int au = q.material == 0 ? addAcc(addView(uv, sizeof uv), 5126, 4, "VEC2", false) : -1;
            const int ac = addAcc(addView(col, sizeof col), 5123, 4, "VEC4", true);
            const int ai = addAcc(addView(idx, sizeof idx), 5123, 6, "SCALAR", false);
            char b[260];
            snprintf(b, sizeof b, "%s{\"attributes\":{\"POSITION\":%d%s%s,\"COLOR_0\":%d},\"indices\":%d,\"material\":%d,\"mode\":4}", qi ? "," : "", ap,
                     au >= 0 ? ",\"TEXCOORD_0\":" : "", au >= 0 ? std::to_string(au).c_str() : "", ac, ai, q.material);
            prims += b;
        }
        char b[400];
        snprintf(b, sizeof b, "%s{\"name\":\"room_%d\",\"primitives\":[%s]}", ri ? "," : "", r.number, prims.c_str());
        meshes += b;
        snprintf(b, sizeof b, "%s{\"name\":\"room_%d\",\"mesh\":%zu,\"translation\":[%g,%g,%g],\"extras\":{\"room\":%d}}", ri ? "," : "", r.number, ri,
                 r.t[0], r.t[1], r.t[2], r.number);
        nodes += b;
    }
    {
        FILE *f = fopen((dir / (level + ".bin")).string().c_str(), "wb");
        if (!f) return false;
        fwrite(bin.data(), 1, bin.size(), f);
        fclose(f);
    }
    auto mat = [](const char *alpha, bool dbl, bool tex, unsigned lo) {
        char b[420];
        snprintf(b, sizeof b,
                 "{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1],\"metallicFactor\":0,\"roughnessFactor\":1%s},\"alphaMode\":\"%s\",\"doubleSided\":%s,"
                 "\"extensions\":{\"KHR_materials_unlit\":{}},\"extras\":{\"n64\":{\"othermode\":[1122304,%u]}}}",
                 tex ? ",\"baseColorTexture\":{\"index\":0}" : "", alpha, dbl ? "true" : "false", lo);
        return std::string(b);
    };
    std::string json;
    char head[300];
    snprintf(head, sizeof head, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"synthetic\",\"extras\":{\"exporter_version\":%d,\"level\":\"%s\"}},", exporterVersion, level.c_str());
    json = head;
    json += "\"extensionsUsed\":[\"KHR_materials_unlit\"],\"scene\":0,\"scenes\":[{\"nodes\":[0,1]}],";
    json += "\"nodes\":[" + nodes + "],\"meshes\":[" + meshes + "],";
    json += "\"materials\":[" + mat("OPAQUE", false, true, 0x0C182000u) + "," + mat("MASK", true, false, 0x0C182000u) + "," +
            mat("BLEND", false, false, 0x0C182800u | 0x4000u) + "," + mat("OPAQUE", false, false, 0x0C182C00u) + "],";
    json += "\"textures\":[{\"sampler\":0,\"source\":0}],\"images\":[{\"uri\":\"textures/1.png\"}],"
            "\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":10497,\"wrapT\":33071}],";
    json += "\"accessors\":[" + accs + "],\"bufferViews\":[" + views + "],\"buffers\":[{\"uri\":\"" + level + ".bin\",\"byteLength\":" + std::to_string(bin.size()) + "}]}";
    FILE *f = fopen((dir / (level + ".gltf")).string().c_str(), "wb");
    if (!f) return false;
    fwrite(json.data(), 1, json.size(), f);
    fclose(f);
    return true;
}

}   // namespace synth
