// modelview: the user's character models, read from their ROM at run time with the converter's own code
// (port/convert: file table, rz decompression, cv_sd_model_mesh). Nothing ROM-derived is ever written to the repo.
#pragma once
#include "mv_mesh.h"
#include "mv_tex.h"
#include <string>
#include <vector>

struct MvModelEntry { std::string name; bool head; };

bool mv_rom_open(const std::string& path, std::string& err);   // reads the .z64 (big-endian) and picks the region
bool mv_rom_ready();
const std::vector<MvModelEntry>& mv_rom_models();                // the C* character files: bodies and heads
// tier 0 = the near / detailed parts (what the game draws up close), 1 = the far parts (the simplified LOD meshes)
bool mv_rom_load(const std::string& name, int tier, MvMesh& out, std::string& err);

// The decoded RGBA texture (cached) of a model texture number; false = not decodable (img.note says why).
bool mv_rom_texture(int texnum, MvTexImage& out);
// The decompressed N64 image (and switch / texture-config counts) of the model loaded last: the node tree for posing.
const std::vector<uint8_t>& mv_rom_model_image(int* ns, int* nt);
char mv_rom_region_letter();   // 'u', 'e', 'j'
