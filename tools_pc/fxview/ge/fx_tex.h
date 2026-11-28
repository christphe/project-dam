// fxview: the game's textures through ge_assets (port/assets/ge_tex.h): the active mod over the chosen texture pack over ge007.passets,
// the same resolution the editor and the game use. No ROM decoding here: ge007.passets is the exporter's picture of every texture of the
// bank (texture number = key); when it is not beside the game, it is written once from the ROM (ex_ensure_base_asset_pack, port/export).
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

struct FxTexImage { int w = 0, h = 0; std::vector<uint8_t> rgba; std::string note; };   // note = why it is empty

// ge007.passets: `arg` (--pack: the pack itself, a tar or a folder, or a folder holding it), else FXVIEW_PACK, else <assetpacks>/ge007.passets of the game's folder
// (PC: ./assetpacks, ../assetpacks, <ROM's data dir>/../assetpacks; Switch: sdmc:/switch/project-dam/assetpacks); the older ge007.ptexpack still counts. "" = none found.
std::string fxTexFindBase(const std::string& arg, const std::string& romPath);
// Writes ge007.passets from the ROM to the path packDir when it is missing or out of date (a few seconds, once). 1 current / written, 0 failed; note says why.
bool fxTexEnsureBase(const std::string& romPath, const std::string& packDir, std::string& note);
// Opens the sources: `base` = ge007.passets, `texPack` the chosen pack ("" = ge007; a file name of base's folder or a path), `mod` a mod folder / .pmod ("" none).
bool fxTexOpen(const std::string& base, const std::string& texPack, const std::string& mod);
bool fxTexOpened();
const std::string& fxTexNote();   // "texture pack ... (2698 textures)" or why there is none
// The picture of texture number `texnum` (a texture-bank number), RGBA8. false and out.note when no source has it.
bool fxTexture(int texnum, FxTexImage& out);
int fx_tex_selftest();
