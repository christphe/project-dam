// Textures of the user's ROM for the sea tab (the cloud texture bg/clouds, the level's water texture), decoded by the shared texture
// decoder of the tools (tools_pc/modelview/pc/mv_tex: the game's image.c codecs, including compression method 8, the cloud's).
#include "cloudtex.h"

#include <stdio.h>
#include <string.h>

#include "mv_tex.h"

bool romImageRgba(const uint8_t* rom, size_t romSize, const char* imagelistCsv, int texNum, int* w, int* h, std::vector<uint8_t>& rgba, std::string& why) {
    if (!mv_tex_set_imagelist(imagelistCsv)) { why = "no image list"; return false; }
    MvTexImage im;
    if (!mv_tex_load(rom, romSize, texNum, im)) { why = im.note; return false; }
    *w = im.w; *h = im.h; rgba.swap(im.rgba);
    return true;
}

bool cloudTexFromRom(const uint8_t* rom, size_t romSize, const char* imagelistCsv, int texNum, unsigned char* intensity64x64, std::string& why) {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    if (!romImageRgba(rom, romSize, imagelistCsv, texNum, &w, &h, px, why)) return false;
    if (w != 64 || h != 64) { why = "not a 64x64 image"; return false; }
    for (int i = 0; i < 4096; i++) intensity64x64[i] = px[(size_t)i * 4];   // the I of I4A4 as 0..255
    return true;
}

// The level's water texture the way the game picks it (src/game/sky.c: skywaterimages[env->WaterImageId], an entry of the global image
// table, Globalimagetable at skyWaterTable): entry = { u32 index (image number), w, h, level, format, depth, flagsS, flagsT, pad }, big endian.
bool waterTexFromRom(const uint8_t* rom, size_t romSize, const char* imagelistCsv, int waterImageId, int* w, int* h, std::vector<uint8_t>& rgba, std::string& why) {
    // Globalimagetable in the ROM (scripts/filelist.<region>.csv); skywaterimages sits 0xFB4 into it (src/game/image_bank.c)
    const char reg = rom[0x3E] == 'E' ? 'u' : rom[0x3E] == 'P' ? 'e' : rom[0x3E] == 'J' ? 'j' : 'u';
    const size_t tableRom = reg == 'u' ? 2740576 : reg == 'e' ? 2662608 : 2743936;
    const size_t o = tableRom + 0xFB4 + 12 * (size_t)(waterImageId < 0 ? 0 : waterImageId);
    if (o + 12 > romSize) { why = "outside the ROM"; return false; }
    const uint32_t index = ((uint32_t)rom[o] << 24) | ((uint32_t)rom[o + 1] << 16) | ((uint32_t)rom[o + 2] << 8) | rom[o + 3];
    if (rom[o + 4] < 8 || rom[o + 5] < 8) { why = "no plausible entry (this ROM's table differs)"; return false; }
    char b[64];
    snprintf(b, sizeof b, "image %u", index);
    why = b;
    return romImageRgba(rom, romSize, imagelistCsv, (int)index, w, h, rgba, why) ? (why = b, true) : false;
}
