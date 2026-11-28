// modelview: see mv_export.h. glTF through cgltf, characters.json through cJSON (both vendored in port/third_party). The export is the asset
// pack ge007.passets (a tar or a folder): every file is read through ge_pkg (port/assets/ge_pkg.h), cgltf gets the .bin through it too.
#include "mv_export.h"
#include <algorithm>
#include <windows.h>
#include <array>
#include <map>
#include <set>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
extern "C" {
#include "cJSON.h"
#include "ge_tex.h"
#include "ge_png.h"
#include "ge_pkg.h"
}

bool mv_inflate_raw(const uint8_t* src, size_t n, std::vector<uint8_t>& out);   // mv_zlib.cpp

namespace {
std::string g_dir;                       // the characters folder (<pack>/characters, in the asset pack)
std::vector<MvCharEntry> g_chars;
int g_version = -1;
std::vector<MvAnimInfo> g_anims;         // of the body built last
cgltf_data* g_body = nullptr;            // kept for posing
std::vector<int> g_jointNode;            // per mesh joint: index of its node in g_body (-1 = rest)
// animations/<name>.gltf (exporter version 10 on): one animation on the guard rig per file, listed in characters.json;
// the file of the animation shown is loaded when it is first posed
std::vector<MvAnimInfo> g_sharedAnims;
std::vector<std::string> g_sharedFile;   // per entry of g_sharedAnims: its glTF
cgltf_data* g_shared = nullptr;          // the file loaded last
int g_sharedIdx = -1;                    // its index in g_sharedAnims
std::vector<int> g_sharedToBody;         // per node of g_shared: the body node of the same name, -1 none
float g_pov = 1;                         // the body's pov: the shared file's root translation is without it
struct Img { int w = 0, h = 0; std::vector<uint8_t> rgba; bool ok = false; };
std::map<std::string, Img> g_imgs;

// A path with its "a/.." pairs collapsed and backslashes made '/': the glTFs of the pack name their pictures "../../textures/N.png" and the
// package layer takes a path as it is.
std::string normPath(const std::string& in) {
    std::vector<std::string> seg; std::string cur, out;
    const bool abs = !in.empty() && (in[0] == '/' || in[0] == '\\');
    for (size_t i = 0; i <= in.size(); i++) {
        if (i == in.size() || in[i] == '/' || in[i] == '\\') {
            if (cur == "..") { if (!seg.empty() && seg.back() != ".." && seg.back().find(':') == std::string::npos) seg.pop_back(); else seg.push_back(cur); }
            else if (!cur.empty() && cur != ".") seg.push_back(cur);
            cur.clear();
        } else cur += in[i];
    }
    if (abs) out = "/";
    for (size_t i = 0; i < seg.size(); i++) out += (i ? "/" : "") + seg[i];
    return out;
}
bool readBytes(const std::string& p, std::vector<uint8_t>& out) {
    uint8_t* b = nullptr; size_t n = 0;
    if (!pkgReadFile(normPath(p).c_str(), &b, &n)) return false;
    out.assign(b, b + n); pkgFree(b); return true;
}
std::string readText(const std::string& p) {
    uint8_t* b = nullptr; size_t n = 0;
    if (!pkgReadFile(normPath(p).c_str(), &b, &n)) return "";
    std::string s((const char*)b, n); pkgFree(b); return s;
}
bool isFile(const std::string& p) { return pkgExists(normPath(p).c_str()) != 0; }
// cgltf's file reader through the package layer: the .bin of a glTF inside the pack's tar
cgltf_result pkgGltfRead(const cgltf_memory_options*, const cgltf_file_options*, const char* path, cgltf_size* size, void** data) {
    uint8_t* b = nullptr; size_t n = 0;
    if (!pkgReadFile(normPath(path).c_str(), &b, &n)) return cgltf_result_file_not_found;
    *size = n; *data = b; return cgltf_result_success;
}
void pkgGltfRelease(const cgltf_memory_options*, const cgltf_file_options*, void* data) { pkgFree(data); }
std::string dirOf(const std::string& p) { const size_t k = p.find_last_of("/\\"); return k == std::string::npos ? std::string(".") : p.substr(0, k); }
const char* jstr(const cJSON* o, const char* k) { const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k); return cJSON_IsString(v) ? v->valuestring : ""; }
double jnum(const cJSON* o, const char* k, double d) { const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k); return cJSON_IsNumber(v) ? v->valuedouble : d; }
bool jbool(const cJSON* o, const char* k) { const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, k); return cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valuedouble != 0); }

// a node's or primitive's "extras" object, parsed (nullptr when there is none); the caller deletes it
cJSON* extrasOf(const cgltf_extras& e) { return e.data ? cJSON_Parse(e.data) : nullptr; }

// ---- PNG: ge_assets' reader (port/assets/ge_png.h) ----
bool pngDecode(const std::vector<uint8_t>& f, int& w, int& h, std::vector<uint8_t>& rgba) {
    uint8_t* px = nullptr;
    if (!gePngDecode(f.data(), f.size(), &w, &h, &px)) return false;
    rgba.assign(px, px + (size_t)w * h * 4); free(px);
    return true;
}

// ---- transforms (column-major 4x4, as glTF) ----
struct M4 { float m[16]; };
M4 ident4() { M4 a = {}; a.m[0] = a.m[5] = a.m[10] = a.m[15] = 1; return a; }
M4 mul4(const M4& a, const M4& b) { M4 o; for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) { float s = 0; for (int k = 0; k < 4; k++) s += a.m[k * 4 + r] * b.m[c * 4 + k]; o.m[c * 4 + r] = s; } return o; }
M4 trs(const float t[3], const float q[4], const float s[3]) {   // q = x, y, z, w
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    M4 o = ident4();
    o.m[0] = (1 - 2 * (y * y + z * z)) * s[0]; o.m[1] = (2 * (x * y + z * w)) * s[0]; o.m[2] = (2 * (x * z - y * w)) * s[0];
    o.m[4] = (2 * (x * y - z * w)) * s[1]; o.m[5] = (1 - 2 * (x * x + z * z)) * s[1]; o.m[6] = (2 * (y * z + x * w)) * s[1];
    o.m[8] = (2 * (x * z + y * w)) * s[2]; o.m[9] = (2 * (y * z - x * w)) * s[2]; o.m[10] = (1 - 2 * (x * x + y * y)) * s[2];
    o.m[12] = t[0]; o.m[13] = t[1]; o.m[14] = t[2];
    return o;
}
void toPose(const M4& a, float* p) {   // MvMesh::pose: row vectors r0 r1 r2 t (p' = p.x*r0 + p.y*r1 + p.z*r2 + t)
    for (int k = 0; k < 3; k++) { p[k] = a.m[k]; p[3 + k] = a.m[4 + k]; p[6 + k] = a.m[8 + k]; p[9 + k] = a.m[12 + k]; }
}
M4 nodeWorld(const cgltf_node* n) { M4 m; cgltf_node_transform_world(n, m.m); return m; }

// one colour per joint, so the limbs read apart and a joint seam shows as a colour change
void jointColour(int j, float* rgb) {
    const float h = fmodf(j * 0.61803f, 1.0f) * 6.0f, c = 0.55f, x = c * (1.0f - fabsf(fmodf(h, 2.0f) - 1.0f));
    rgb[0] = rgb[1] = rgb[2] = 0;
    switch ((int)h) { case 0: rgb[0] = c; rgb[1] = x; break; case 1: rgb[0] = x; rgb[1] = c; break; case 2: rgb[1] = c; rgb[2] = x; break;
                      case 3: rgb[1] = x; rgb[2] = c; break; case 4: rgb[0] = x; rgb[2] = c; break; default: rgb[0] = c; rgb[2] = x; break; }
    for (int k = 0; k < 3; k++) rgb[k] += 0.35f;
}

cgltf_data* loadGltf(const std::string& path, std::string& err) {
    cgltf_options o = {}; cgltf_data* d = nullptr;
    o.file.read = pkgGltfRead; o.file.release = pkgGltfRelease;
    std::vector<uint8_t> json;
    if (!readBytes(path, json)) { err = "cannot read " + path; return nullptr; }
    if (cgltf_parse(&o, json.data(), json.size(), &d) != cgltf_result_success) { err = "cannot parse " + path; return nullptr; }
    if (cgltf_load_buffers(&o, d, normPath(path).c_str()) != cgltf_result_success) { cgltf_free(d); err = "cannot load the buffers of " + path; return nullptr; }
    return d;
}
const cgltf_node* nodeByName(const cgltf_data* d, const std::string& name) {
    for (cgltf_size i = 0; i < d->nodes_count; i++) if (d->nodes[i].name && name == d->nodes[i].name) return &d->nodes[i];
    return nullptr;
}

int wrapOf(int gl) { return gl == 33071 ? 1 : gl == 33648 ? 2 : 0; }
int materialOf(MvMesh& out, const cgltf_material* m, const std::string& gdir) {
    if (!m) return -1;
    MvMaterial mt;
    const cgltf_texture* t = m->has_pbr_metallic_roughness ? m->pbr_metallic_roughness.base_color_texture.texture : nullptr;
    if (!t || !t->image || !t->image->uri) return -1;
    if (cJSON* e = extrasOf(m->extras)) {   // extras.texture: the texture key ("412"); an older export: extras.texnum
        const cJSON* k = cJSON_GetObjectItemCaseSensitive(e, "texture");
        if (cJSON_IsString(k) && k->valuestring) mt.key = k->valuestring;
        else if (cJSON_IsNumber(k)) mt.key = std::to_string((long long)k->valuedouble);
        else if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(e, "texnum"))) mt.key = std::to_string((int)jnum(e, "texnum", -1));
        cJSON_Delete(e);
    }
    mt.image = normPath(gdir + "/" + t->image->uri);
    if (mt.key.empty()) {   // no extras at all: the number in the picture's file name
        const int n = texpackTexnumOfName(t->image->uri);
        if (n >= 0) mt.key = std::to_string(n);
    }
    mt.texnum = texpackKeyIsNumber(mt.key.c_str()) ? atoi(mt.key.c_str()) : -1;
    if (t->sampler) { mt.wrapS = wrapOf(t->sampler->wrap_s); mt.wrapT = wrapOf(t->sampler->wrap_t); }
    for (size_t q = 0; q < out.materials.size(); q++)
        if (out.materials[q].key == mt.key && out.materials[q].image == mt.image && out.materials[q].wrapS == mt.wrapS && out.materials[q].wrapT == mt.wrapT) return (int)q;
    out.materials.push_back(mt); return (int)out.materials.size() - 1;
}

// Appends a primitive. joint < 0: from JOINTS_0 (a skinned body), else every vertex on that joint (a head: its positions
// are already local to the joint it hangs off, jointOff empty). jointOff = per joint the
// bind-space offset subtracted to make the positions joint-local. xf = the node's transform (rigid parts).
int addPrim(MvMesh& out, const cgltf_primitive& p, int joint, const std::vector<std::array<float, 3>>& jointOff, const M4& xf, bool head,
            const std::string& gdir) {
    if (p.type != cgltf_primitive_type_triangles) return 0;
    const cgltf_accessor *ap = nullptr, *at = nullptr, *aj = nullptr;
    for (cgltf_size i = 0; i < p.attributes_count; i++) {
        const cgltf_attribute& a = p.attributes[i];
        if (a.type == cgltf_attribute_type_position) ap = a.data;
        else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) at = a.data;
        else if (a.type == cgltf_attribute_type_joints && a.index == 0) aj = a.data;
    }
    if (!ap) return 0;
    const uint32_t base = (uint32_t)out.verts.size();
    for (cgltf_size i = 0; i < ap->count; i++) {
        MvVertex v; float pos[3] = { 0, 0, 0 }, uv[2] = { 0, 0 };
        cgltf_accessor_read_float(ap, i, pos, 3);
        if (at) cgltf_accessor_read_float(at, i, uv, 2);
        int j = joint;
        if (j < 0) { cgltf_uint jj[4] = { 0, 0, 0, 0 }; if (aj) cgltf_accessor_read_uint(aj, i, jj, 4); j = (int)jj[0]; }
        if (j < 0 || j >= (int)out.joints.size()) j = 0;
        float w[3];
        for (int k = 0; k < 3; k++) w[k] = xf.m[k] * pos[0] + xf.m[4 + k] * pos[1] + xf.m[8 + k] * pos[2] + xf.m[12 + k];
        for (int k = 0; k < 3; k++) v.pos[k] = w[k] - (j < (int)jointOff.size() ? jointOff[(size_t)j][k] : 0.0f);
        v.uv[0] = uv[0]; v.uv[1] = uv[1];
        v.joint = j; v.head = head; v.layer = 0;
        jointColour(j + (head ? 7 : 0), v.col); v.col[3] = 1;
        out.verts.push_back(v);
    }
    const int mat = materialOf(out, p.material, gdir);
    int nt = 0;
    if (p.indices) {
        for (cgltf_size i = 0; i + 2 < p.indices->count; i += 3) {
            for (int k = 0; k < 3; k++) out.idx.push_back(base + (uint32_t)cgltf_accessor_read_index(p.indices, i + k));
            out.tmat.push_back(mat); nt++;
        }
    } else {
        for (cgltf_size i = 0; i + 2 < ap->count; i += 3) { for (int k = 0; k < 3; k++) out.idx.push_back(base + (uint32_t)(i + k)); out.tmat.push_back(mat); nt++; }
    }
    return nt;
}

std::vector<MvAnimInfo> animsOf(const cgltf_data* d) {
    std::vector<MvAnimInfo> out;
    for (cgltf_size a = 0; a < d->animations_count; a++) {
        MvAnimInfo ai; ai.name = d->animations[a].name ? d->animations[a].name : ("animation " + std::to_string(a));
        if (cJSON* e = extrasOf(d->animations[a].extras)) { ai.number = (int)jnum(e, "number", -1); ai.frames = (int)jnum(e, "frames", 0); ai.fps = (float)jnum(e, "fps", 30); ai.loop = jbool(e, "loop"); cJSON_Delete(e); }
        if (!ai.frames && d->animations[a].samplers_count) ai.frames = (int)d->animations[a].samplers[0].input->count;
        out.push_back(ai);
    }
    return out;
}
bool primFar(const cgltf_primitive& p) { cJSON* e = extrasOf(p.extras); const bool f = e && jbool(e, "lod_far"); cJSON_Delete(e); return f; }
bool primSpecs(const cgltf_primitive& p) { cJSON* e = extrasOf(p.extras); const bool f = e && jbool(e, "sunglasses"); cJSON_Delete(e); return f; }
bool anyFar(const cgltf_data* d) {
    for (cgltf_size m = 0; m < d->meshes_count; m++) for (cgltf_size i = 0; i < d->meshes[m].primitives_count; i++) if (primFar(d->meshes[m].primitives[i])) return true;
    return false;
}
}  // namespace

bool mv_export_open(const std::string& dirIn, std::string& err) {
    g_chars.clear(); g_dir.clear(); g_version = -1;
    std::string dir = dirIn;
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    // --export takes the folder holding ge007.passets, or the pack itself (a tar or a folder); the characters are in its characters/
    const bool isPack = dir.size() > 8 && dir.compare(dir.size() - 8, 8, ".passets") == 0;
    for (const std::string& c : { isPack ? dir + "/characters" : dir + "/ge007.passets/characters", dir + "/characters", dir })
        if (isFile(c + "/characters.json")) { dir = c; break; }
    const std::string text = readText(dir + "/characters.json");
    if (text.empty()) { err = "no characters/characters.json in the asset pack " + dirIn + " (--export takes the folder holding ge007.passets, or the pack)"; return false; }
    cJSON* root = cJSON_Parse(text.c_str());
    if (!root) { err = dir + "/characters.json is not valid JSON"; return false; }
    g_version = (int)jnum(root, "exporter_version", -1);
    if (g_shared) { cgltf_free(g_shared); g_shared = nullptr; }
    g_sharedIdx = -1; g_sharedAnims.clear(); g_sharedFile.clear();
    if (*jstr(root, "animations_dir")) {
        const cJSON* a;
        cJSON_ArrayForEach(a, cJSON_GetObjectItemCaseSensitive(root, "animations")) {
            if (!*jstr(a, "gltf")) continue;
            MvAnimInfo ai; ai.name = jstr(a, "name"); ai.number = (int)jnum(a, "number", -1); ai.frames = (int)jnum(a, "frames", 0);
            ai.fps = (float)jnum(a, "fps", 30); ai.loop = jbool(a, "loop");
            g_sharedAnims.push_back(ai); g_sharedFile.push_back(dir + "/" + jstr(a, "gltf"));
        }
    }
    const cJSON* list = cJSON_GetObjectItemCaseSensitive(root, "characters");
    const cJSON* c;
    cJSON_ArrayForEach(c, list) {
        MvCharEntry e;
        e.id = (int)jnum(c, "id", -1); e.file = jstr(c, "file"); e.name = jstr(c, "name"); e.label = jstr(c, "label"); e.kind = jstr(c, "kind");
        e.gltf = jstr(c, "gltf"); e.bodyScale = (float)jnum(c, "body_scale", 1); e.modelScale = (float)jnum(c, "model_scale", 0.1); e.pov = (float)jnum(c, "pov", 1);
        e.isMale = jbool(c, "is_male"); e.hasHead = jbool(c, "has_head"); e.attachJoint = jstr(c, "attach_joint");
        for (char& ch : e.label) if (ch == '_') ch = ' ';
        g_chars.push_back(e);
    }
    cJSON_Delete(root);
    if (g_chars.empty()) { err = dir + "/characters.json lists no characters"; return false; }
    g_dir = dir;
    return true;
}
bool mv_export_ready() { return !g_dir.empty(); }
const std::string& mv_export_dir() { return g_dir; }
int mv_export_version() { return g_version; }
const std::vector<MvCharEntry>& mv_export_chars() { return g_chars; }
int mv_export_find(const std::string& s) {
    for (size_t i = 0; i < g_chars.size(); i++) if (g_chars[i].file == s || g_chars[i].name == s) return (int)i;
    return -1;
}
const std::vector<MvAnimInfo>& mv_export_anims() { return g_anims; }

bool mv_export_build(const MvCharSpec& spec, MvMesh& out, MvCharInfo& info, std::string& err) {
    info = MvCharInfo(); out = MvMesh();
    const MvCharEntry* body = spec.body >= 0 && spec.body < (int)g_chars.size() ? &g_chars[(size_t)spec.body] : nullptr;
    const MvCharEntry* head = spec.head >= 0 && spec.head < (int)g_chars.size() ? &g_chars[(size_t)spec.head] : nullptr;
    if (body && body->kind != "body") { err = body->file + " is not a body"; return false; }
    if (head && head->kind != "head") { err = head->file + " is not a head"; return false; }
    if (!body && !head) { err = "nothing chosen"; return false; }
    if ((body && !body->exported()) || (head && !head->exported())) { err = "not in the export"; return false; }
    if (body && body->hasHead && head) { info.note = body->label + " has its own head: no head file is attached"; head = nullptr; }

    if (g_body) { cgltf_free(g_body); g_body = nullptr; }
    g_anims.clear(); g_jointNode.clear();
    std::vector<std::array<float, 3>> joff;
    int attach = 0;   // the mesh joint a head hangs off

    if (body) {
        const std::string path = g_dir + "/" + body->gltf, gdir = dirOf(path);
        cgltf_data* d = loadGltf(path, err); if (!d) return false;
        if (!d->skins_count) { cgltf_free(d); err = body->file + ": no skin"; return false; }
        const cgltf_skin& sk = d->skins[0];
        out.joints.resize(sk.joints_count); joff.resize(sk.joints_count); g_jointNode.resize(sk.joints_count, -1);
        for (cgltf_size j = 0; j < sk.joints_count; j++) {
            float ibm[16]; for (int k = 0; k < 16; k++) ibm[k] = k % 5 ? 0.0f : 1.0f;
            if (sk.inverse_bind_matrices) cgltf_accessor_read_float(sk.inverse_bind_matrices, j, ibm, 16);
            for (int k = 0; k < 3; k++) { joff[j][(size_t)k] = -ibm[12 + k]; out.joints[j].offset[k] = joff[j][(size_t)k]; }
            out.joints[j].parent = -1;
            g_jointNode[j] = (int)(sk.joints[j] - d->nodes);
        }
        // has_head bodies carry the head themselves: the neck joint and everything below it is the head region
        std::vector<char> headJoint(sk.joints_count, 0);
        if (body->hasHead)
            if (const cgltf_node* neck = nodeByName(d, "j3_neck"))
                for (cgltf_size j = 0; j < sk.joints_count; j++)
                    for (const cgltf_node* n = sk.joints[j]; n; n = n->parent) if (n == neck) { headJoint[j] = 1; break; }
        const std::string an = head && !head->attachJoint.empty() ? head->attachJoint : std::string("j3_neck");
        for (cgltf_size j = 0; j < sk.joints_count; j++) if (sk.joints[j]->name && an == sk.joints[j]->name) attach = (int)j;
        const bool lodSplit = anyFar(d);
        for (cgltf_size i = 0; i < d->nodes_count; i++) {
            const cgltf_node& n = d->nodes[i];
            if (!n.mesh) continue;
            for (cgltf_size pi = 0; pi < n.mesh->primitives_count; pi++) {
                const cgltf_primitive& p = n.mesh->primitives[pi];
                if (lodSplit && primFar(p) != (spec.tier == 1)) continue;
                const size_t t0 = out.triCount();
                info.bodyTris += addPrim(out, p, n.skin ? -1 : attach, joff, n.skin ? ident4() : nodeWorld(&n), false, gdir);
                if (body->hasHead)
                    for (size_t t = t0; t < out.triCount(); t++) for (int k = 0; k < 3; k++) { MvVertex& v = out.verts[out.idx[3 * t + k]]; if (headJoint[(size_t)v.joint]) v.head = true; }
            }
        }
        // the animations: the shared file's, retargeted by node name, else the body's own
        g_pov = body->pov;
        g_sharedToBody.clear(); g_sharedIdx = -1;   // the node map is made for this body when an animation is posed
        if (g_shared) { cgltf_free(g_shared); g_shared = nullptr; }
        g_anims = g_sharedAnims.empty() ? animsOf(d) : g_sharedAnims;
        g_body = d;
        out.scale = 0.01f * body->bodyScale;
    } else {
        out.joints.resize(1); out.joints[0].parent = -1; joff.assign(1, { 0, 0, 0 });
        out.scale = 0.01f;
    }

    if (head) {
        const std::string path = g_dir + "/" + head->gltf, gdir = dirOf(path);
        cgltf_data* d = loadGltf(path, err); if (!d) return false;
        // the switches above each display list: sunglasses and hair follow the options, the others their default
        const bool lodSplit = anyFar(d);
        for (cgltf_size i = 0; i < d->nodes_count; i++) {
            const cgltf_node& n = d->nodes[i];
            if (!n.mesh) continue;
            bool specs = false, hair = false, hidden = false;
            for (const cgltf_node* a = n.parent; a; a = a->parent) {
                const std::string nm = a->name ? a->name : "";
                if (nm == "switch_specs") specs = true;
                else if (nm == "switch_hair") hair = true;
                else if (cJSON* e = extrasOf(a->extras)) { if (jbool(e, "switch") && !strcmp(jstr(e, "default"), "hidden")) hidden = true; cJSON_Delete(e); }
            }
            if (specs) info.hasSpecs = true;
            if (hair) info.hasHair = true;
            if (hidden || (specs && !spec.sunglasses) || (hair && !spec.hair)) continue;
            for (cgltf_size pi = 0; pi < n.mesh->primitives_count; pi++) {
                const cgltf_primitive& p = n.mesh->primitives[pi];
                if (lodSplit && primFar(p) != (spec.tier == 1)) continue;
                const bool sp = specs || primSpecs(p);
                if (sp && !spec.sunglasses) continue;
                const int nt = addPrim(out, p, body ? attach : 0, {}, nodeWorld(&n), true, gdir);
                info.headTris += nt; if (sp) info.specsTris += nt; if (hair) info.hairTris += nt;
            }
        }
        cgltf_free(d);
    }
    out.name = body ? (head ? body->file + " + " + head->file : body->file) : head->file;
    out.headOnly = true;
    if (out.idx.empty()) { err = "no triangles in this level of detail"; return false; }
    return true;
}

bool mv_export_pose(int anim, float frame, int nj, std::vector<float>& pose) {
    pose.assign(12 * (size_t)nj, 0);
    for (int i = 0; i < nj; i++) pose[12 * (size_t)i] = pose[12 * (size_t)i + 4] = pose[12 * (size_t)i + 8] = 1;
    if (!g_body || anim < 0 || anim >= (int)g_anims.size()) return false;
    const cgltf_data* d = g_body;
    // every node's local transform: its own TRS, the animated channels at this frame over it
    std::vector<float> T(d->nodes_count * 3), R(d->nodes_count * 4), S(d->nodes_count * 3);
    for (cgltf_size i = 0; i < d->nodes_count; i++) {
        const cgltf_node& n = d->nodes[i];
        for (int k = 0; k < 3; k++) { T[i * 3 + k] = n.has_translation ? n.translation[k] : 0; S[i * 3 + k] = n.has_scale ? n.scale[k] : 1; }
        for (int k = 0; k < 4; k++) R[i * 4 + k] = n.has_rotation ? n.rotation[k] : (k == 3 ? 1.0f : 0.0f);
    }
    const bool shared = !g_sharedAnims.empty();
    if (shared && g_sharedIdx != anim) {   // load the animation's own file, map its nodes onto this body's by name
        if (g_shared) { cgltf_free(g_shared); g_shared = nullptr; }
        std::string e2;
        g_shared = loadGltf(g_sharedFile[(size_t)anim], e2);
        if (!g_shared) { fprintf(stderr, "animation: %s\n", e2.c_str()); g_sharedIdx = -1; return false; }
        g_sharedIdx = anim;
        g_sharedToBody.assign(g_shared->nodes_count, -1);
        for (cgltf_size i = 0; i < g_shared->nodes_count; i++) {
            const cgltf_node* n = g_shared->nodes[i].name ? nodeByName(d, g_shared->nodes[i].name) : nullptr;
            if (n) g_sharedToBody[i] = (int)(n - d->nodes);
        }
    }
    const cgltf_data* src = shared ? g_shared : d;   // where the animation lives
    const int ai = shared ? 0 : anim;
    if (!src || ai >= (int)src->animations_count) return false;
    const cgltf_animation& a = src->animations[ai];
    const float t = frame / (g_anims[(size_t)anim].fps > 0 ? g_anims[(size_t)anim].fps : 30.0f);
    for (cgltf_size c = 0; c < a.channels_count; c++) {
        const cgltf_animation_channel& ch = a.channels[c];
        if (!ch.target_node || !ch.sampler) continue;
        const cgltf_accessor *in = ch.sampler->input, *outp = ch.sampler->output;
        const cgltf_size n = in->count; if (!n) continue;
        cgltf_size k0 = 0; float f = 0;
        float tk0 = 0, tk1 = 0;
        for (cgltf_size k = 0; k + 1 < n; k++) {
            cgltf_accessor_read_float(in, k, &tk0, 1); cgltf_accessor_read_float(in, k + 1, &tk1, 1);
            if (t >= tk0 && t <= tk1) { k0 = k; f = tk1 > tk0 ? (t - tk0) / (tk1 - tk0) : 0; break; }
            if (k + 2 == n) { k0 = n - 1; f = 0; }
        }
        const cgltf_size k1 = k0 + 1 < n ? k0 + 1 : k0;
        int bn = (int)(ch.target_node - src->nodes);
        if (shared) bn = bn < (int)g_sharedToBody.size() ? g_sharedToBody[(size_t)bn] : -1;
        if (bn < 0) continue;   // a node this body does not have
        const size_t ni = (size_t)bn;
        const bool isRoot = !d->nodes[ni].parent;
        if (ch.target_path == cgltf_animation_path_type_rotation) {
            float q0[4], q1[4]; cgltf_accessor_read_float(outp, k0, q0, 4); cgltf_accessor_read_float(outp, k1, q1, 4);
            float dt = q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3], sg = dt < 0 ? -1.0f : 1.0f, l = 0;
            float q[4]; for (int k = 0; k < 4; k++) { q[k] = q0[k] * (1 - f) + q1[k] * sg * f; l += q[k] * q[k]; }   // nlerp
            l = l > 0 ? 1.0f / sqrtf(l) : 1.0f; for (int k = 0; k < 4; k++) R[ni * 4 + k] = q[k] * l;
        } else if (ch.target_path == cgltf_animation_path_type_translation) {
            float v0[3], v1[3]; cgltf_accessor_read_float(outp, k0, v0, 3); cgltf_accessor_read_float(outp, k1, v1, 3);
            const float sc = shared ? g_pov : 1.0f;   // the shared file's root motion is without the body's pov
            for (int k = 0; k < 3; k++) T[ni * 3 + k] = (v0[k] * (1 - f) + v1[k] * f) * sc;
            if (isRoot) { T[ni * 3] = 0; T[ni * 3 + 2] = 0; }   // the root's travel: stay in place
        }
    }
    // world = parent world x local, parents first (resolved recursively with a memo)
    std::vector<M4> W(d->nodes_count); std::vector<char> done(d->nodes_count, 0);
    struct Rec { const cgltf_data* d; std::vector<float>&T, &R, &S; std::vector<M4>& W; std::vector<char>& done;
        const M4& get(size_t i) {
            if (done[i]) return W[i];
            const M4 l = trs(&T[i * 3], &R[i * 4], &S[i * 3]);
            const cgltf_node* p = d->nodes[i].parent;
            W[i] = p ? mul4(get((size_t)(p - d->nodes)), l) : l; done[i] = 1; return W[i];
        } } rec{ d, T, R, S, W, done };
    for (int j = 0; j < nj && j < (int)g_jointNode.size(); j++) if (g_jointNode[(size_t)j] >= 0) toPose(rec.get((size_t)g_jointNode[(size_t)j]), &pose[12 * (size_t)j]);
    return true;
}

bool mv_export_image(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba) {
    auto it = g_imgs.find(path);
    if (it == g_imgs.end()) {
        Img im; std::vector<uint8_t> b;
        if (readBytes(path, b)) im.ok = pngDecode(b, im.w, im.h, im.rgba);
        it = g_imgs.emplace(path, std::move(im)).first;
    }
    w = it->second.w; h = it->second.h; rgba = it->second.rgba;
    return it->second.ok;
}


// ---- texture sources (docs/specs/texture-packs.md): the active mod over the chosen pack ----------------------------------
// A material is drawn from the texture its key names (material extras.texture): the mod's entry merged field by field over
// the chosen pack's, the pack being ge007.passets (the game's own) or a complete other pack: ge_assets' GeTexSet (port/assets/ge_tex.h).
// The glTF's own image URI is used only when no source has the key (an export without ge007.passets beside it).
namespace {
std::string g_game;                       // the game folder (holds assetpacks/ and mods/), "" unknown
std::string g_modRoot, g_packRoot, g_baseRoot, g_texNote;
GeTexSet* g_texSet = nullptr;
std::set<std::string> g_texWarned;        // the warnings printed since the sources were opened (a missing picture is asked for every frame)
int g_texGen = 0;
std::string slash(std::string p) { for (char& c : p) if (c == '\\') c = '/'; return p; }
bool exists(const std::string& p) { WIN32_FILE_ATTRIBUTE_DATA a; return GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &a) != 0; }
std::string exportRoot() {   // the asset pack above characters/ (a tar or a folder): the export's own ge007.passets
    std::string d = slash(g_dir);
    while (!d.empty() && d.back() == '/') d.pop_back();
    const size_t k = d.find_last_of('/');
    return k == std::string::npos ? "" : d.substr(0, k);
}
std::string packsDir() { return g_game.empty() ? "" : g_game + "/assetpacks"; }
std::string modsDir() { return g_game.empty() ? "" : g_game + "/mods"; }
std::string findBase() { char b[1024]; geTexBaseRoot(packsDir().c_str(), exportRoot().c_str(), b, sizeof b); return b; }
}  // namespace

void mv_tex_init(const std::string& gameDir) { g_game = slash(gameDir); while (!g_game.empty() && g_game.back() == '/') g_game.pop_back(); }
const std::string& mv_tex_game() { return g_game; }
const std::string& mv_tex_note() { return g_texNote; }
int mv_tex_gen() { return g_texGen; }
const std::string& mv_tex_mod() { return g_modRoot; }
const std::string& mv_tex_pack() { return g_packRoot; }

std::vector<MvPackChoice> mv_tex_packs() {
    std::vector<GeTexPackChoice> raw(257);
    const int n = std::min(geTexPackList(packsDir().c_str(), exportRoot().c_str(), raw.data(), (int)raw.size()), (int)raw.size());
    std::vector<MvPackChoice> out;
    for (int i = 0; i < n; i++) {
        MvPackChoice c; c.file = raw[(size_t)i].file; c.path = raw[(size_t)i].path; c.name = raw[(size_t)i].name; c.reason = raw[(size_t)i].reason; c.base = raw[(size_t)i].base != 0;
        out.push_back(c);
    }
    return out;
}

std::vector<MvModChoice> mv_tex_mods() {
    std::vector<MvModChoice> out;
    if (modsDir().empty()) return out;
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((modsDir() + "/*.pmod").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        MvModChoice m; m.file = fd.cFileName; m.path = modsDir() + "/" + m.file; m.name = m.file.substr(0, m.file.size() - 5);
        out.push_back(m);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}

bool mv_tex_use(const std::string& modIn, const std::string& packIn) {
    bool ok = true;
    g_texNote.clear();
    // a name is looked up in the game's folders, a path (it exists as is) is taken as it is
    auto resolve = [&](const std::string& v, const std::string& dir, const char* suffix) -> std::string {
        if (v.empty()) return "";
        if (exists(v)) return slash(v);
        if (!dir.empty()) {
            const std::string a = dir + "/" + v, b = dir + "/" + v + suffix;
            if (exists(a)) return a;
            if (exists(b)) return b;
        }
        return slash(v);
    };
    const std::string mod = resolve(modIn, modsDir(), ".pmod");
    const std::string pack = packIn.empty() || packIn == "ge007" || packIn == "ge007.passets" || packIn == "ge007.ptexpack" ? "" : resolve(packIn, packsDir(), ".passets");
    const std::string base = findBase();
    if (!g_texSet) g_texSet = geTexSetNew();
    g_texGen++;
    g_texWarned.clear();
    g_baseRoot = base;
    geTexSetOpen(g_texSet, mod.c_str(), pack.c_str(), base.c_str());   // the mod afresh, the cache dropped
    if (!mod.empty() && !geTexSetMod(g_texSet)) { g_texNote = "mod " + mod + " cannot be read: no mod"; ok = false; }
    g_modRoot = geTexSetMod(g_texSet) ? mod : "";
    g_packRoot = geTexSetPack(g_texSet) && geTexSetPack(g_texSet) != geTexSetBase(g_texSet) ? pack : "";   // "" = ge007 is the chosen pack
    if (geTexSetNote(g_texSet)[0]) { g_texNote = geTexSetNote(g_texSet); ok = false; }
    if (!g_texNote.empty()) fprintf(stderr, "textures: %s\n", g_texNote.c_str());
    return ok;
}

// One material's picture: through the sources by its key, else the glTF's own image. 0 = missing.
bool mv_tex_image(const MvMaterial& mt, int& w, int& h, std::vector<uint8_t>& rgba) {
    if (!mt.key.empty() && g_texSet && geTexSetPack(g_texSet)) {
        GeTexImage im;
        auto warn = [](void*, const char* msg) { fprintf(stderr, "textures: %s\n", msg); };
        if (geTexResolve(g_texSet, mt.key.c_str(), &im, warn, nullptr)) {
            w = im.w; h = im.h; rgba.assign(im.rgba, im.rgba + (size_t)im.w * im.h * 4);
            return true;
        }
    }
    return mv_export_image(mt.image, w, h, rgba);
}

#include "mv_png.h"
int mv_export_selftest() {
    int fail = 0;
    {   // PNG: the viewer's own writer (RGB, stored deflate) read back
        const int W = 5, H = 3; std::vector<uint8_t> src((size_t)W * H * 4);
        for (size_t i = 0; i < src.size(); i++) src[i] = (uint8_t)(i * 37 + 11);
        for (int i = 0; i < W * H; i++) src[(size_t)i * 4 + 3] = 255;
        char tmp[MAX_PATH]; GetTempPathA(sizeof tmp, tmp); const std::string p = std::string(tmp) + "mv_selftest.png";
        int w = 0, h = 0; std::vector<uint8_t> got;
        if (!mvpng::write(p, W, H, src.data()) || !mv_export_image(p, w, h, got) || w != W || h != H || got != src) { fail++; fprintf(stderr, "selftest FAIL png round trip\n"); }
        remove(p.c_str());
    }
    {   // PNG filters: a hand-made 2x2 RGBA, row 0 filter 1 (sub), row 1 filter 4 (paeth)
        const uint8_t raw[] = { 1, 10, 20, 30, 255, 5, 5, 5, 0,   4, 1, 1, 1, 0, 2, 2, 2, 0 };
        std::vector<uint8_t> z = { 0x78, 0x01, 1, (uint8_t)sizeof raw, 0, (uint8_t)~sizeof raw, 0xFF };
        z.insert(z.end(), raw, raw + sizeof raw); z.insert(z.end(), { 0, 0, 0, 0 });
        std::vector<uint8_t> f = { 137, 80, 78, 71, 13, 10, 26, 10 };
        auto chunk = [&](const char* t, const std::vector<uint8_t>& d) { const uint32_t n = (uint32_t)d.size(); f.insert(f.end(), { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n }); f.insert(f.end(), t, t + 4); f.insert(f.end(), d.begin(), d.end()); f.insert(f.end(), { 0, 0, 0, 0 }); };
        chunk("IHDR", { 0, 0, 0, 2, 0, 0, 0, 2, 8, 6, 0, 0, 0 }); chunk("IDAT", z); chunk("IEND", {});
        int w, h; std::vector<uint8_t> px;
        const uint8_t want[] = { 10, 20, 30, 255, 15, 25, 35, 255,   11, 21, 31, 255, 17, 27, 37, 255 };
        if (!pngDecode(f, w, h, px) || w != 2 || h != 2 || memcmp(px.data(), want, sizeof want)) { fail++; fprintf(stderr, "selftest FAIL png filters\n"); }
    }
    {   // a 90 degree turn about z maps +x to +y; the pose rows are the matrix columns
        const float t[3] = { 1, 2, 3 }, s[3] = { 1, 1, 1 }, q[4] = { 0, 0, 0.70710678f, 0.70710678f };
        float p[12]; toPose(trs(t, q, s), p);
        const float x = 1 * p[0] + p[9], y = 1 * p[1] + p[10];
        if (fabsf(x - 1) > 1e-5f || fabsf(y - 3) > 1e-5f) { fail++; fprintf(stderr, "selftest FAIL trs (%g %g)\n", x, y); }
    }
    {   // the texture sources: ge007.passets as the default, a mod over it, a pack that is incomplete
        char tmp[MAX_PATH]; GetTempPathA(sizeof tmp, tmp);
        const std::string T = slash(std::string(tmp)) + "mv_texsel", P = T + "/assetpacks", B = P + "/ge007.passets", M = T + "/mods/m.pmod", L = P + "/lack.passets";
        std::vector<std::string> files, dirs;
        auto mk = [&](const std::string& d) { if (CreateDirectoryA(d.c_str(), nullptr)) dirs.push_back(d); };
        auto txt = [&](const std::string& p, const char* s) { FILE* f = fopen(p.c_str(), "wb"); if (f) { fputs(s, f); fclose(f); files.push_back(p); } };
        auto png = [&](const std::string& p, uint8_t r, uint8_t g, uint8_t b) { uint8_t px[2 * 2 * 4]; for (int i = 0; i < 4; i++) { px[i * 4] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255; } if (mvpng::write(p, 2, 2, px)) files.push_back(p); };
        for (const std::string& d : { T, P, B, B + "/textures", T + "/mods", M, M + "/textures", L }) mk(d);
        txt(B + "/textures.json", "{\"version\":1,\"textures\":{\"5\":{\"image\":\"textures/5.png\",\"surface\":\"stone\",\"alpha\":\"image\"},\"6\":{\"image\":\"textures/6.png\",\"surface\":\"wood\"}}}");
        png(B + "/textures/5.png", 255, 0, 0); png(B + "/textures/6.png", 0, 0, 255);
        txt(L + "/textures.json", "{\"version\":1,\"textures\":{\"5\":{\"surface\":\"stone\"}}}");
        png(M + "/textures/5.png", 0, 255, 0);
        mv_tex_init(T);
        MvMaterial m5; m5.key = "5"; MvMaterial m6; m6.key = "6"; int w = 0, h = 0; std::vector<uint8_t> px;
        const std::vector<MvPackChoice> packs = mv_tex_packs();
        if (packs.size() != 2 || !packs[0].base || !packs[0].usable() || packs[1].usable()) { fail++; fprintf(stderr, "selftest FAIL pack list\n"); }
        if (!mv_tex_use("", "") || !mv_tex_image(m5, w, h, px) || w != 2 || px[0] != 255 || px[1] != 0) { fail++; fprintf(stderr, "selftest FAIL ge007 picture\n"); }
        if (!mv_tex_use("m", "") || !mv_tex_image(m5, w, h, px) || px[0] != 0 || px[1] != 255 || !mv_tex_image(m6, w, h, px) || px[2] != 255) { fail++; fprintf(stderr, "selftest FAIL mod over pack\n"); }
        if (mv_tex_use("", "lack") || mv_tex_note().empty() || !mv_tex_pack().empty()) { fail++; fprintf(stderr, "selftest FAIL incomplete pack\n"); }
        mv_tex_use("", "");
        for (const std::string& f : files) remove(f.c_str());
        for (size_t i = dirs.size(); i-- > 0;) RemoveDirectoryA(dirs[i].c_str());
    }
    fprintf(stderr, fail ? "selftest export: %d failures\n" : "selftest: PNG decoding and pose maths ok\n", fail);
    return fail ? 1 : 0;
}
