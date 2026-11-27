// modelview: the characters of the static export (ge007-export / ge007 --export-levels, docs/specs/characters.md):
// characters/characters.json + one glTF per body and per head. The export is made from the user's own ROM and lives
// outside the repo (or in the git-ignored tools_pc/modelview/data/); nothing here reads the ROM.
#pragma once
#include "mv_mesh.h"
#include <string>
#include <vector>

struct MvCharEntry {
    int id = -1;
    std::string file, name, label, kind, gltf;   // "ColiveguardZ", "oliveguard", "Russian Soldier", "body" | "head" | "hand", relative path
    float bodyScale = 1, modelScale = 0.1f, pov = 1;
    bool isMale = false, hasHead = false;
    std::string attachJoint;                     // heads: the body node the head hangs off ("j3_neck")
    bool exported() const { return !gltf.empty(); }
};

struct MvAnimInfo { std::string name; int number = -1; int frames = 0; float fps = 30; bool loop = false; };

// dir = the characters folder (holds characters.json) or the export folder above it
bool mv_export_open(const std::string& dir, std::string& err);
bool mv_export_ready();
const std::string& mv_export_dir();
int mv_export_version();                          // asset.extras / characters.json exporter_version, -1 unknown
const std::vector<MvCharEntry>& mv_export_chars();
int mv_export_find(const std::string& fileOrName);   // by file ("CgreyguardZ") or name ("greyguard"); -1 none

// What to put together, like the game's makeonebody: a body (or -1), a head (or -1; ignored when the body has its own
// head), the level of detail and the head's switches.
struct MvCharSpec {
    int body = -1, head = -1;
    int tier = 0;             // 0 near (the default scene), 1 far (the far level-of-detail parts only)
    bool sunglasses = false;  // the head's part 0 (the game hides it unless the guard wears them)
    bool hair = true;         // the head's part 1
};
struct MvCharInfo {           // what the build found, for the panel
    std::string note;         // why the head was not attached, ...
    int bodyTris = 0, headTris = 0, specsTris = 0, hairTris = 0;
    bool hasSpecs = false, hasHair = false;
};
bool mv_export_build(const MvCharSpec& spec, MvMesh& out, MvCharInfo& info, std::string& err);

// The animations of the body built last (the export's default selection unless it was made with
// GE007_EXPORT_ALL_ANIMATIONS=1); empty for a head on its own.
const std::vector<MvAnimInfo>& mv_export_anims();
// The pose (12 floats per mesh joint, see MvMesh::pose) of the body built last at a frame of animation `anim`
// (index into mv_export_anims()); the root's travel in x/z is left out (the model stays in place).
bool mv_export_pose(int anim, float frame, int njoints, std::vector<float>& pose);

// A texture image (RGBA8) by the path a material carries; cached; false = missing or not a PNG.
bool mv_export_image(const std::string& path, int& w, int& h, std::vector<uint8_t>& rgba);

// ---- texture sources (docs/specs/texture-packs.md): the active mod (partial, merged field by field) over the chosen texture
// pack (complete). ge007.ptexpack, the game's own pack, is the default: <game>/texturepacks/ge007.ptexpack when the game wrote
// it, else the export's own copy beside characters/. A pack that lacks textures of ge007 is refused (never mixed).
struct MvPackChoice { std::string file, path, name, reason; bool base = false; bool usable() const { return reason.empty(); } };
struct MvModChoice { std::string file, path, name; };
void mv_tex_init(const std::string& gameDir);          // the game folder (texturepacks/, mods/); "" = none known
const std::string& mv_tex_game();
std::vector<MvPackChoice> mv_tex_packs();              // ge007 first, then the folder's packs; not selectable ones carry a reason (opens every pack: not cheap)
std::vector<MvModChoice> mv_tex_mods();                // <game>/mods/*.pmod
// mod: "" none, else a .pmod / folder path or the name of one in <game>/mods; pack: "" = ge007, else a path or the name of one
// in <game>/texturepacks. false (and mv_tex_note() says why) when one was refused: ge007 is used, or no mod.
bool mv_tex_use(const std::string& mod, const std::string& pack);
const std::string& mv_tex_note();
const std::string& mv_tex_mod();                       // what is in use ("" none / ge007)
const std::string& mv_tex_pack();
int mv_tex_gen();                                      // changes whenever mv_tex_use ran: cached GL textures are stale
// The picture of a material: through the sources by its key, else the glTF's own image. false = missing.
bool mv_tex_image(const MvMaterial& mt, int& w, int& h, std::vector<uint8_t>& rgba);

int mv_export_selftest();   // PNG decoding and the pose maths, no export needed
