// fxview: the game's textures through ge_assets (port/assets/ge_tex.h): the active mod over the chosen texture pack over ge007.ptexpack,
// the same resolution the editor and the game use. No ROM decoding here: ge007.ptexpack is the exporter's picture of every texture of the
// bank (texture number = key); when it is not beside the game, it is written once from the ROM (ex_ensure_base_pack, port/export).
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

struct FxTexImage { int w = 0, h = 0; std::vector<uint8_t> rgba; std::string note; };   // note = why it is empty

// ge007.ptexpack: `arg` (--pack, a folder or .ptexpack), else FXVIEW_PACK, else <texturepacks>/ge007.ptexpack of the game's folder (PC: ./texturepacks,
// ../texturepacks, <ROM's data dir>/../texturepacks; Switch: sdmc:/switch/project-dam/texturepacks). "" = none found.
std::string fxTexFindBase(const std::string& arg, const std::string& romPath);
// Writes ge007.ptexpack from the ROM into packDir when it is missing or out of date (a few seconds, once). 1 current / written, 0 failed; note says why.
bool fxTexEnsureBase(const std::string& romPath, const std::string& packDir, std::string& note);
// Opens the sources: `base` = ge007.ptexpack, `texPack` the chosen pack ("" = ge007; a file name of base's folder or a path), `mod` a mod folder / .pmod ("" none).
bool fxTexOpen(const std::string& base, const std::string& texPack, const std::string& mod);
bool fxTexOpened();
const std::string& fxTexNote();   // "texture pack ... (2698 textures)" or why there is none
// The picture of texture number `texnum` (a texture-bank number), RGBA8. false and out.note when no source has it.
bool fxTexture(int texnum, FxTexImage& out);
int fx_tex_selftest();
