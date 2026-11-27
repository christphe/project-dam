#include "decor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

extern "C" {
#include "cJSON.h"
}
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "png.h"

static float linearToSrgb(float c)
{
    c = std::min(1.0f, std::max(0.0f, c));
    return c <= 0.0031308f ? 12.92f * c : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

static int wrapToN64(int w)   // glTF sampler wrap (GL enum) -> G_TX: 0 repeat, 1 mirror, 2 clamp
{
    if (w == 33071) return 2;
    if (w == 33648) return 1;
    return 0;
}

static std::string dirOf(const std::string &p)
{
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string(".") : p.substr(0, s);
}

static bool extrasInt(const cgltf_extras &ex, const char *key, int &out)
{
    if (!ex.data) return false;
    cJSON *j = cJSON_Parse(ex.data);
    if (!j) return false;
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    const bool ok = cJSON_IsNumber(v);
    if (ok) out = (int)v->valuedouble;
    cJSON_Delete(j);
    return ok;
}

DecorResult decorLoad(const std::string &gltfPath, int expectVersion, Decor &out, std::string &msg)
{
    out = Decor();
    FILE *probe = fopen(gltfPath.c_str(), "rb");
    if (!probe) { msg = "no glTF export at " + gltfPath; return DECOR_MISSING; }
    fclose(probe);

    cgltf_options opt;
    memset(&opt, 0, sizeof opt);
    cgltf_data *data = nullptr;
    if (cgltf_parse_file(&opt, gltfPath.c_str(), &data) != cgltf_result_success) { msg = "cannot parse " + gltfPath; return DECOR_BAD; }
    struct Guard { cgltf_data *d; ~Guard() { cgltf_free(d); } } guard{ data };

    int ver = 0;
    extrasInt(data->asset.extras, "exporter_version", ver);
    out.version = ver;
    if (ver != expectVersion) {
        msg = "glTF export has exporter_version " + std::to_string(ver) + ", this build writes " + std::to_string(expectVersion);
        return DECOR_STALE;
    }
    if (cgltf_load_buffers(&opt, data, gltfPath.c_str()) != cgltf_result_success) { msg = "cannot read the .bin of " + gltfPath; return DECOR_BAD; }

    const std::string dir = dirOf(gltfPath);
    std::map<const cgltf_image *, int> texOfImage;
    std::map<const cgltf_material *, int> matIndex;

    auto materialFor = [&](const cgltf_material *m) -> int {
        auto it = matIndex.find(m);
        if (it != matIndex.end()) return it->second;
        DecorMaterial dm;
        if (m) {
            dm.alphaMode = m->alpha_mode == cgltf_alpha_mode_blend ? 2 : m->alpha_mode == cgltf_alpha_mode_mask ? 1 : 0;
            dm.doubleSided = m->double_sided != 0;
            if (m->has_pbr_metallic_roughness) {
                const float *f = m->pbr_metallic_roughness.base_color_factor;
                for (int k = 0; k < 3; k++) dm.factor[k] = linearToSrgb(f[k]);
                dm.factor[3] = std::min(1.0f, std::max(0.0f, f[3]));
            }
            if (m->extras.data) {   // extras.n64.othermode = [high word, low word]; the z mode is bits 10..11 of the low one
                if (cJSON *j = cJSON_Parse(m->extras.data)) {
                    const cJSON *om = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(j, "n64"), "othermode");
                    const cJSON *lo = cJSON_IsArray(om) ? cJSON_GetArrayItem(om, 1) : nullptr;
                    if (cJSON_IsNumber(lo)) dm.zmode = (int)((uint32_t)lo->valuedouble & 0xC00u);
                    cJSON_Delete(j);
                }
            }
            const cgltf_texture *t = m->has_pbr_metallic_roughness ? m->pbr_metallic_roughness.base_color_texture.texture : nullptr;
            if (t && t->image && t->image->uri) {
                auto ti = texOfImage.find(t->image);
                if (ti == texOfImage.end()) {
                    DecorTex dt;
                    int w = 0, h = 0;
                    const std::string p = dir + "/" + t->image->uri;
                    if (readPng(p.c_str(), w, h, dt.rgba)) { dt.w = w; dt.h = h; out.tex.push_back(std::move(dt)); ti = texOfImage.emplace(t->image, (int)out.tex.size() - 1).first; }
                    else ti = texOfImage.emplace(t->image, -1).first;   // missing picture: drawn untextured, not fatal
                }
                dm.tex = ti->second;
                if (t->sampler) { dm.cms = wrapToN64((int)t->sampler->wrap_s); dm.cmt = wrapToN64((int)t->sampler->wrap_t); }
            }
        }
        out.materials.push_back(dm);
        matIndex[m] = (int)out.materials.size() - 1;
        return (int)out.materials.size() - 1;
    };

    const float big = 1e30f;
    for (int k = 0; k < 3; k++) { out.mn[k] = big; out.mx[k] = -big; }
    for (cgltf_size ni = 0; ni < data->nodes_count; ni++) {
        const cgltf_node *node = &data->nodes[ni];
        if (!node->mesh) continue;
        if (node->name && strncmp(node->name, "collision", 9) == 0) continue;   // the hidden collision floor: read below
        int room = -1;
        if (!extrasInt(node->extras, "room", room) && node->name && sscanf(node->name, "room_%d", &room) != 1) room = -1;
        float w[16];
        cgltf_node_transform_world(node, w);   // column-major
        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; pi++) {
            const cgltf_primitive &pr = node->mesh->primitives[pi];
            if (pr.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor *apos = nullptr, *auv = nullptr, *acol = nullptr;
            for (cgltf_size ai = 0; ai < pr.attributes_count; ai++) {
                const cgltf_attribute &at = pr.attributes[ai];
                if (at.index != 0) continue;
                if (at.type == cgltf_attribute_type_position) apos = at.data;
                else if (at.type == cgltf_attribute_type_texcoord) auv = at.data;
                else if (at.type == cgltf_attribute_type_color) acol = at.data;
            }
            if (!apos || apos->count == 0) continue;
            DecorPrim dp;
            dp.room = room;
            dp.material = materialFor(pr.material);
            const size_t n = apos->count;
            dp.pos.resize(n * 3);
            if (cgltf_accessor_unpack_floats(apos, dp.pos.data(), n * 3) < n * 3) continue;
            for (size_t i = 0; i < n; i++) {
                float *p = &dp.pos[i * 3];
                const float x = p[0], y = p[1], z = p[2];
                p[0] = w[0] * x + w[4] * y + w[8] * z + w[12];
                p[1] = w[1] * x + w[5] * y + w[9] * z + w[13];
                p[2] = w[2] * x + w[6] * y + w[10] * z + w[14];
                for (int k = 0; k < 3; k++) { out.mn[k] = std::min(out.mn[k], p[k]); out.mx[k] = std::max(out.mx[k], p[k]); }
            }
            if (auv && auv->count == n && dp.material >= 0 && out.materials[(size_t)dp.material].tex >= 0) {
                dp.uv.resize(n * 2);
                if (cgltf_accessor_unpack_floats(auv, dp.uv.data(), n * 2) < n * 2) dp.uv.clear();
            }
            dp.col.assign(n * 4, 1.0f);
            if (acol && acol->count == n) {
                const int nc = (int)cgltf_num_components(acol->type);
                std::vector<float> c(n * (size_t)nc);
                if (cgltf_accessor_unpack_floats(acol, c.data(), c.size()) == c.size())
                    for (size_t i = 0; i < n; i++) {
                        for (int k = 0; k < 3 && k < nc; k++) dp.col[i * 4 + (size_t)k] = linearToSrgb(c[i * (size_t)nc + (size_t)k]);
                        dp.col[i * 4 + 3] = nc >= 4 ? std::min(1.0f, std::max(0.0f, c[i * (size_t)nc + 3])) : 1.0f;
                    }
            }
            if (pr.indices) {
                dp.idx.resize(pr.indices->count);
                if (cgltf_accessor_unpack_indices(pr.indices, dp.idx.data(), sizeof(uint32_t), dp.idx.size()) < dp.idx.size()) continue;
            } else {
                dp.idx.resize(n);
                for (size_t i = 0; i < n; i++) dp.idx[i] = (uint32_t)i;
            }
            {   // vertex colour x baseColorFactor (RGBA)
                const float *f = out.materials[(size_t)dp.material].factor;
                for (size_t i = 0; i < n; i++) for (int k = 0; k < 4; k++) dp.col[i * 4 + (size_t)k] *= f[k];
            }
            dp.idx.resize(dp.idx.size() / 3 * 3);
            bool inRange = true;
            for (uint32_t ix : dp.idx) if (ix >= n) { inRange = false; break; }
            if (!inRange || dp.idx.empty()) continue;
            out.prims.push_back(std::move(dp));
        }
    }
    if (out.prims.empty()) { msg = "the glTF export has no room geometry"; return DECOR_BAD; }
    double levelScale = 0;
    if (data->asset.extras.data) {
        cJSON *j = cJSON_Parse(data->asset.extras.data);
        if (j) {
            const cJSON *l = cJSON_GetObjectItemCaseSensitive(j, "level");
            if (cJSON_IsString(l)) out.level = l->valuestring;
            const cJSON *sc = cJSON_GetObjectItemCaseSensitive(j, "level_scale");
            if (cJSON_IsNumber(sc)) levelScale = sc->valuedouble;
            cJSON_Delete(j);
        }
    }
    // the collision floor: nodes collision_room_<n>, their mesh's one primitive carries extras.tiles (raw s16 points)
    if (levelScale > 0)
        for (cgltf_size ni = 0; ni < data->nodes_count; ni++) {
            const cgltf_node *node = &data->nodes[ni];
            if (!node->mesh || !node->name || strncmp(node->name, "collision_room_", 15) != 0) continue;
            for (cgltf_size pi = 0; pi < node->mesh->primitives_count; pi++) {
                const cgltf_extras &ex = node->mesh->primitives[pi].extras;
                if (!ex.data) continue;
                cJSON *j = cJSON_Parse(ex.data);
                if (!j) continue;
                const cJSON *t;
                cJSON_ArrayForEach(t, cJSON_GetObjectItemCaseSensitive(j, "tiles")) {
                    const cJSON *pts = cJSON_GetObjectItemCaseSensitive(t, "points"), *nbs = cJSON_GetObjectItemCaseSensitive(t, "neighbour");
                    const cJSON *ix = cJSON_GetObjectItemCaseSensitive(t, "index"), *rm = cJSON_GetObjectItemCaseSensitive(t, "room");
                    const cJSON *kd = cJSON_GetObjectItemCaseSensitive(t, "kind");
                    if (!cJSON_IsArray(pts) || !cJSON_IsNumber(ix)) continue;
                    DecorStanTile st;
                    st.index = (int)ix->valuedouble;
                    st.room = cJSON_IsNumber(rm) ? (int)rm->valuedouble : -1;
                    st.kind = cJSON_IsNumber(kd) ? (int)kd->valuedouble : 0;
                    const cJSON *p;
                    cJSON_ArrayForEach(p, pts) {
                        if (!cJSON_IsArray(p) || cJSON_GetArraySize(p) < 3) continue;
                        for (int k = 0; k < 3; k++) st.pt.push_back((float)(cJSON_GetArrayItem(p, k)->valuedouble / levelScale));
                    }
                    const cJSON *n;
                    cJSON_ArrayForEach(n, nbs) st.nb.push_back(cJSON_IsNumber(n) ? (int)n->valuedouble : -1);
                    st.nb.resize(st.pt.size() / 3, -1);
                    if (st.pt.size() >= 9) out.stan.push_back(std::move(st));
                }
                cJSON_Delete(j);
            }
        }
    msg = "glTF decor: " + std::to_string(out.prims.size()) + " primitives, " + std::to_string(out.tex.size()) + " textures";
    if (!out.stan.empty()) msg += ", " + std::to_string(out.stan.size()) + " collision tiles";
    return DECOR_OK;
}
