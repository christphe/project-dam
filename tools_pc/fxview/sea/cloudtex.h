#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

/* Textures of a big-endian ROM by image number (imagelistCsv = the repo's imagelist.<region>.csv: offset,size per image), decoded by
 * tools_pc/modelview/pc/mv_tex. */
bool romImageRgba(const uint8_t* rom, size_t romSize, const char* imagelistCsv, int texNum, int* w, int* h, std::vector<uint8_t>& rgba, std::string& why);
/* The cloud texture (bg/clouds): 4096 bytes, the I of I4A4 as 0..255 (what skyPlusTexture makes). */
bool cloudTexFromRom(const uint8_t* rom, size_t romSize, const char* imagelistCsv, int texNum, unsigned char* intensity64x64, std::string& why);
/* The level's water texture, skywaterimages[WaterImageId] of the game's global image table. */
bool waterTexFromRom(const uint8_t* rom, size_t romSize, const char* imagelistCsv, int waterImageId, int* w, int* h, std::vector<uint8_t>& rgba, std::string& why);
