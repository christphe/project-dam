// modelview: see mv_anim.h.
#include "mv_anim.h"
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>

namespace {
std::string g_repo;
std::vector<MvAnimInfo> g_list;
std::string g_entries;   // assets/animationtable_entries.c, read on first use (4 MB)

std::string readFile(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb"); if (!f) return "";
    std::string s; char b[65536]; size_t n;
    while ((n = fread(b, 1, sizeof b, f))) s.append(b, n);
    fclose(f); return s;
}

// next token (up to a separator) of C initialiser text
bool token(const std::string& s, size_t& p, std::string& t) {
    while (p < s.size() && (isspace((unsigned char)s[p]) || s[p] == ',' || s[p] == '{')) p++;
    if (p >= s.size() || s[p] == '}') return false;
    size_t q = p; while (q < s.size() && !isspace((unsigned char)s[q]) && s[q] != ',' && s[q] != '}') q++;
    t = s.substr(p, q - p); p = q; return true;
}

uint32_t rb32(const std::vector<uint8_t>& b, size_t o) { return o + 4 <= b.size() ? ((uint32_t)b[o] << 24) | ((uint32_t)b[o + 1] << 16) | ((uint32_t)b[o + 2] << 8) | b[o + 3] : 0; }
uint32_t rb16(const std::vector<uint8_t>& b, size_t o) { return o + 2 <= b.size() ? ((uint32_t)b[o] << 8) | b[o + 1] : 0; }
float rbf(const std::vector<uint8_t>& b, size_t o) { const uint32_t u = rb32(b, o); float f; memcpy(&f, &u, 4); return f; }
const uint32_t kOff = 0xFFFFFFu;

// modelAnimReadBitsAsU16Angle
uint32_t readAngle(const uint8_t* bs, size_t nbytes, int width, uint32_t bitOffset) {
    uint32_t v = 0; int remaining = width; size_t pos = bitOffset / 8; int nb = 8 - (int)(bitOffset % 8);
    while (remaining >= nb) {
        remaining -= nb; const uint32_t mask = (1u << nb) - 1;
        v |= ((uint32_t)((pos < nbytes ? bs[pos] : 0) & mask)) << remaining; v &= 0xFFFF; pos++; nb = 8;
    }
    if (remaining > 0) { v |= (((pos < nbytes ? bs[pos] : 0) >> (nb - remaining)) & ((1u << remaining) - 1)); v &= 0xFFFF; }
    v <<= 16 - width;
    return v & 0xFFFF;
}

// 3x4 row-vector matrices (r0 r1 r2 t): p' = p.x*r0 + p.y*r1 + p.z*r2 + t
struct M34 { float m[12]; };
M34 ident() { M34 a = {}; a.m[0] = a.m[4] = a.m[8] = 1; return a; }
M34 rotXYZ(const float a[3], const float t[3]) {   // matrix_4x4_set_rotation_around_xyz + position
    const float cx = cosf(a[0]), sx = sinf(a[0]), cy = cosf(a[1]), sy = sinf(a[1]), cz = cosf(a[2]), sz = sinf(a[2]);
    M34 r;
    r.m[0] = cy * cz; r.m[1] = cy * sz; r.m[2] = -sy;
    r.m[3] = sx * cz * sy - cx * sz; r.m[4] = sx * sz * sy + cx * cz; r.m[5] = sx * cy;
    r.m[6] = cx * cz * sy + sx * sz; r.m[7] = cx * sz * sy - sx * cz; r.m[8] = cx * cy;
    r.m[9] = t[0]; r.m[10] = t[1]; r.m[11] = t[2];
    return r;
}
M34 mul(const M34& l, const M34& p) {   // local first, then parent
    M34 o;
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) o.m[r * 3 + c] = l.m[r * 3] * p.m[c] + l.m[r * 3 + 1] * p.m[3 + c] + l.m[r * 3 + 2] * p.m[6 + c];
    for (int c = 0; c < 3; c++) o.m[9 + c] = l.m[9] * p.m[c] + l.m[10] * p.m[3 + c] + l.m[11] * p.m[6 + c] + p.m[9 + c];
    return o;
}
M34 halfRot(const M34& a) {   // the same rotation at half the angle (quaternion power 0.5), translation kept
    // row-vector matrix -> column-vector R = transpose
    const float R[3][3] = { { a.m[0], a.m[3], a.m[6] }, { a.m[1], a.m[4], a.m[7] }, { a.m[2], a.m[5], a.m[8] } };
    const float tr = R[0][0] + R[1][1] + R[2][2];
    float w, x, y, z;
    if (tr > 0) { const float s = sqrtf(tr + 1) * 2; w = 0.25f * s; x = (R[2][1] - R[1][2]) / s; y = (R[0][2] - R[2][0]) / s; z = (R[1][0] - R[0][1]) / s; }
    else if (R[0][0] > R[1][1] && R[0][0] > R[2][2]) { const float s = sqrtf(1 + R[0][0] - R[1][1] - R[2][2]) * 2; w = (R[2][1] - R[1][2]) / s; x = 0.25f * s; y = (R[0][1] + R[1][0]) / s; z = (R[0][2] + R[2][0]) / s; }
    else if (R[1][1] > R[2][2]) { const float s = sqrtf(1 + R[1][1] - R[0][0] - R[2][2]) * 2; w = (R[0][2] - R[2][0]) / s; x = (R[0][1] + R[1][0]) / s; y = 0.25f * s; z = (R[1][2] + R[2][1]) / s; }
    else { const float s = sqrtf(1 + R[2][2] - R[0][0] - R[1][1]) * 2; w = (R[1][0] - R[0][1]) / s; x = (R[0][2] + R[2][0]) / s; y = (R[1][2] + R[2][1]) / s; z = 0.25f * s; }
    if (w < 0) { w = -w; x = -x; y = -y; z = -z; }
    const float ang = 2 * acosf(fminf(w, 1.0f)), sn = sqrtf(fmaxf(1 - w * w, 0));
    float hw = 1, hx = 0, hy = 0, hz = 0;
    if (sn > 1e-6f) { const float h = ang * 0.5f * 0.5f, k = sinf(h) / sn; hw = cosf(h); hx = x * k; hy = y * k; hz = z * k; }
    const float C[3][3] = {   // quaternion -> column-vector matrix
        { 1 - 2 * (hy * hy + hz * hz), 2 * (hx * hy - hz * hw), 2 * (hx * hz + hy * hw) },
        { 2 * (hx * hy + hz * hw), 1 - 2 * (hx * hx + hz * hz), 2 * (hy * hz - hx * hw) },
        { 2 * (hx * hz - hy * hw), 2 * (hy * hz + hx * hw), 1 - 2 * (hx * hx + hy * hy) } };
    M34 o;
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) o.m[r * 3 + c] = C[c][r];
    o.m[9] = a.m[9]; o.m[10] = a.m[10]; o.m[11] = a.m[11];
    return o;
}

struct Joint { int type, a, b; };
std::map<std::string, std::vector<Joint>> g_skel;

const std::vector<Joint>& skeleton(const std::string& name) {
    auto it = g_skel.find(name); if (it != g_skel.end()) return it->second;
    std::vector<Joint>& out = g_skel[name];
    WIN32_FIND_DATAA fd; const std::string dir = g_repo + "/assets/embedded/skeletons/";
    HANDLE h = FindFirstFileA((dir + "*.inc.c").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::string s = readFile(dir + fd.cFileName);
        size_t p = s.find("JOINTLIST(" + name + ")[]");
        if (p == std::string::npos) continue;
        p = s.find('{', p); if (p == std::string::npos) continue;
        p++;
        for (;;) {   // rows "{a, b, c}," until "};"
            while (p < s.size() && s[p] != '{' && s[p] != '}') p++;
            if (p >= s.size() || s[p] == '}') break;
            p++; std::string t[3]; size_t q = p; bool ok = true;
            for (int k = 0; k < 3 && ok; k++) ok = token(s, q, t[k]);
            if (!ok) break;
            out.push_back({ (int)strtol(t[0].c_str(), 0, 0), (int)strtol(t[1].c_str(), 0, 0), (int)strtol(t[2].c_str(), 0, 0) });
            p = s.find('}', q); if (p == std::string::npos) break;
            p++;
        }
        break;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return out;
}

bool entryBytes(const std::string& entry, std::vector<uint8_t>& out) {
    if (g_entries.empty()) g_entries = readFile(g_repo + "/assets/animationtable_entries.c");
    const std::string key = "u32 ANIM_ENTRY_" + entry + "[]";
    size_t p = g_entries.find(key); if (p == std::string::npos) return false;
    p = g_entries.find('{', p); if (p == std::string::npos) return false;
    p++; out.clear(); std::string t;
    while (token(g_entries, p, t)) { const uint32_t v = (uint32_t)strtoul(t.c_str(), 0, 0); out.push_back((uint8_t)(v >> 24)); out.push_back((uint8_t)(v >> 16)); out.push_back((uint8_t)(v >> 8)); out.push_back((uint8_t)v); }
    return !out.empty();
}
}  // namespace

bool mv_anim_init(const std::string& repo) {
    g_repo = repo; g_list.clear();
    const std::string s = readFile(repo + "/assets/animationtable_data.c");
    if (s.empty()) return false;
    size_t p = 0;
    for (;;) {
        p = s.find("u32 ANIM_DATA_", p); if (p == std::string::npos) break;
        const size_t ns = p + 14, ne = s.find('[', ns); if (ne == std::string::npos) break;
        MvAnimInfo a; a.name = s.substr(ns, ne - ns);
        size_t q = s.find('{', ne); if (q == std::string::npos) break;
        std::string t[4]; int n = 0;
        while (n < 4 && token(s, q, t[n])) n++;
        if (n == 4 && t[0].compare(0, 15, "PTR_ANIM_ENTRY_") == 0) {
            a.entry = t[0].substr(15);
            const uint32_t w1 = (uint32_t)strtoul(t[1].c_str(), 0, 0), w3 = (uint32_t)strtoul(t[3].c_str(), 0, 0);
            a.frames = (int)(w1 >> 16); a.width = (int)((w1 >> 8) & 0xFF); a.frameBits = (int)(w3 & 0xFFFF);
            g_list.push_back(a);
        }
        p = ne;
    }
    return !g_list.empty();
}
const std::vector<MvAnimInfo>& mv_anim_list() { return g_list; }
int mv_anim_find(const std::string& name) { for (size_t i = 0; i < g_list.size(); i++) if (g_list[i].name == name) return (int)i; return -1; }

std::string mv_anim_skeleton_of(const std::string& model) {
    std::string n = model; if (n.size() > 2 && n[0] == 'C') n = n.substr(1); if (!n.empty() && n.back() == 'Z') n.pop_back();
    const std::string s = readFile(g_repo + "/assets/obseg/chr/" + n + "/modelFileHeader.inc.c");
    const size_t p = s.find("SKELETON("); if (p == std::string::npos) return "";
    const size_t e = s.find(')', p); return e == std::string::npos ? "" : s.substr(p + 9, e - p - 9);
}

namespace {
struct Walk {
    const std::vector<uint8_t>& img; const std::vector<Joint>* joints; const uint8_t* frame; size_t frameLen; int width; int njoints;
    std::vector<float>& pose; int visits = 0; int nOp2 = 0, nRot = 0;
    void set(int id, const M34& m) { if (id >= 0 && id < njoints) memcpy(&pose[12 * (size_t)id], m.m, sizeof m.m); }
    void chain(uint32_t o, const M34& parent, int depth) {
        while (o && o + 0x18 <= img.size() && depth < 64 && visits++ < 4000) {
            const uint32_t opw = rb16(img, o), op = opw & 0xFF, data = rb32(img, o + 4) & kOff;
            M34 childM = parent;
            if (op == 1 && data + 4 <= img.size()) { M34 id = ident(); set((int16_t)rb16(img, data + 2), id); childM = id; }   // the root header: the model's own space
            else if (op == 2 && data + 0x14 <= img.size()) {
                const float org[3] = { rbf(img, data), rbf(img, data + 4), rbf(img, data + 8) };
                const int jn = (int)rb16(img, data + 0xC);
                float rot[3] = { 0, 0, 0 };
                if (frame && joints && jn < (int)joints->size()) {
                    uint32_t off = (uint32_t)(*joints)[(size_t)jn].a * (uint32_t)width;
                    for (int k = 0; k < 3; k++, off += (uint32_t)width) rot[k] = readAngle(frame, frameLen, width, off) * 6.2831855f / 65535.0f;
                }
                nOp2++; if (rot[0] != 0 || rot[1] != 0 || rot[2] != 0) nRot++;
                const M34 local = rotXYZ(rot, org), world = mul(local, parent);
                set((int16_t)rb16(img, data + 0xE), world);
                if (opw & 0x100) set((int16_t)rb16(img, data + 0x10), mul(halfRot(local), parent));
                childM = world;
            }
            uint32_t ch = rb32(img, o + 0x14) & kOff;
            if (op == 8 && data + 12 <= img.size()) ch = rb32(img, data + 8) & kOff;
            else if (op == 18) ch = rb32(img, data) & kOff;
            if (ch) chain(ch, childM, depth + 1);
            o = rb32(img, o + 0xC) & kOff;   // next sibling
        }
    }
};
}  // namespace

bool mv_anim_pose(const std::vector<uint8_t>& image, int ns, int nt, const std::string& model, int anim, int frame, int nj, std::vector<float>& pose, std::string& note) {
    pose.assign(12 * (size_t)nj, 0);
    for (int i = 0; i < nj; i++) pose[12 * (size_t)i] = pose[12 * (size_t)i + 4] = pose[12 * (size_t)i + 8] = 1;
    const std::vector<Joint>* joints = nullptr; std::vector<uint8_t> entry; const uint8_t* fp = nullptr; size_t flen = 0; int width = 0;
    note.clear();
    if (anim >= 0 && anim < (int)g_list.size()) {
        const std::string sk = mv_anim_skeleton_of(model);
        if (sk != "guard") { note = "animations are for the guard skeleton; this model uses " + (sk.empty() ? std::string("an unknown one") : sk); }
        else {
            const MvAnimInfo& a = g_list[(size_t)anim];
            joints = &skeleton(sk);
            if (joints->empty()) note = "skeleton data not found";
            else if (!entryBytes(a.entry, entry)) { note = "animation frames not found"; joints = nullptr; }
            else {
                const int fb = a.frameBits >> 3, f = frame < 0 ? 0 : frame >= a.frames ? a.frames - 1 : frame;
                if ((size_t)(f + 1) * (size_t)fb > entry.size()) { note = "frame outside the data"; joints = nullptr; }
                else { fp = &entry[(size_t)f * (size_t)fb]; flen = (size_t)fb; width = a.width; }
            }
        }
    }
    const uint32_t R0 = 4u * (uint32_t)ns + 12u * (uint32_t)nt;
    Walk w{ image, joints, fp, flen, width, nj, pose };
    w.chain(R0, ident(), 0);
    if (getenv("MV_ANIMDBG")) fprintf(stderr, "anim dbg: anim %d frame %d visits %d op2 %d rotated %d fp %p width %d\n", anim, frame, w.visits, w.nOp2, w.nRot, (const void*)fp, width);
    return fp != nullptr || anim < 0;
}

int mv_anim_selftest() {
    int fail = 0;
    // bit reader: 12 bit values packed MSB first: 0xABC 0x123 0x456 -> bytes AB C1 23 45 6x
    const uint8_t bs[5] = { 0xAB, 0xC1, 0x23, 0x45, 0x60 };
    if (readAngle(bs, 5, 12, 0) != 0xABC0 || readAngle(bs, 5, 12, 12) != 0x1230 || readAngle(bs, 5, 12, 24) != 0x4560) { fail++; fprintf(stderr, "selftest FAIL anim bit reader\n"); }
    // rotation about z by 90 degrees maps +x to +y (row vector p' = p*R)
    const float a[3] = { 0, 0, 1.5707964f }, t[3] = { 1, 2, 3 };
    const M34 r = rotXYZ(a, t);
    const float px = 1 * r.m[0] + r.m[9], py = 1 * r.m[1] + r.m[10];
    if (fabsf(px - 1) > 1e-5f || fabsf(py - 3) > 1e-5f) { fail++; fprintf(stderr, "selftest FAIL rotXYZ (%g %g)\n", px, py); }
    // half of a 90 degree rotation about z is 45 degrees
    const M34 h = halfRot(r);
    if (fabsf(h.m[0] - 0.70710678f) > 1e-4f || fabsf(h.m[1] - 0.70710678f) > 1e-4f) { fail++; fprintf(stderr, "selftest FAIL halfRot %g %g\n", h.m[0], h.m[1]); }
    // composition: child offset (1,0,0) under a parent rotated 90 degrees about z lands at (0,1,0)
    const M34 child = rotXYZ(t, t); (void)child;
    const float z0[3] = { 0, 0, 0 }, o1[3] = { 1, 0, 0 }, o0[3] = { 0, 0, 0 };
    const M34 par = rotXYZ(a, o0), loc = rotXYZ(z0, o1), wld = mul(loc, par);
    if (fabsf(wld.m[9]) > 1e-5f || fabsf(wld.m[10] - 1) > 1e-5f) { fail++; fprintf(stderr, "selftest FAIL mul %g %g\n", wld.m[9], wld.m[10]); }
    fprintf(stderr, fail ? "selftest anim: %d failures\n" : "selftest: animation maths ok\n", fail);
    return fail ? 1 : 0;
}
