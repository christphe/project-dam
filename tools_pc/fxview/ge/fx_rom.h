// fxview: the user's ROM at run time (PC and Switch): the global image table's sprite tables (which textures each effect uses) and the
// guards' weapon models (the muzzle flashes' GUNFIRE nodes). The textures themselves are not decoded from the ROM: fx_tex.h reads them
// from the texture pack (ge_assets). Nothing ROM-derived is written anywhere: the ROM stays where the user keeps it. The table offsets
// are embedded at build time (CMake, fx_romdata.inc), so the Switch build needs no repo files.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

struct ImgEntry { uint32_t index = 0; int w = 0, h = 0, level = 0, format = 0, depth = 0, flagsS = 0, flagsT = 0; };

// The ROM to use: `arg` (--rom), else FXVIEW_ROM / N64_ROM from the environment, else the game's data directory
// (PC: ./data, ../data; Switch: sdmc:/switch/project-dam/data): ge007.ntsc-final.z64, baserom.u.z64, any *.z64. "" = none found.
std::string fxRomFind(const std::string& arg);
bool fxRomOpen(const std::string& path);
bool fxRomLoaded();
const std::string& fxRomNote();   // "ROM ok (...)" or why not
const ImgEntry* fxTableEntry(const char* table, int i);
int fxTableCount(const char* table);

// Texture names and hit types (assets/images.def, embedded): the texture sheet and SMOKE_0 for the explosions. Index = texture number.
enum { FXHIT_DEFAULT = 0, FXHIT_STONE, FXHIT_WOOD, FXHIT_METAL, FXHIT_GLASS, FXHIT_WATER, FXHIT_SNOW, FXHIT_DIRT, FXHIT_MUD, FXHIT_TILE, FXHIT_METALOBJ, FXHIT_CHR, FXHIT_GLASS_XLU };
struct FxTexName { std::string name; int hit = 0; };
const std::vector<FxTexName>& fxTexNames();
const char* fxHitName(int hit);

// The guards' held weapons (Pchr*Z model files in the ROM) carry a GUNFIRE node: the muzzle flash's Offset, Size and texture config.
struct FxGun { std::string name; float off[3] = {}, size[3] = {}; uint32_t tex = 0; int w = 0, h = 0, fs = 0, ft = 0; };
const std::vector<FxGun>& fxGuns();   // scanned on the first call
const std::string& fxGunNote();
