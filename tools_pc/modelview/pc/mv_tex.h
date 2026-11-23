// modelview: GE texture decoding (formats, the zlib/paletted and lookup codecs), from the user's ROM at run time.
// Ported from src/game/image.c (texInflateZlib, texInflateNonZlib, texBuildLookup, texInflateHuffman, texInflateRle).
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

// GE texture formats (image.h TEXFORMAT_*)
enum { MVT_RGBA32 = 0, MVT_RGBA16, MVT_RGB24, MVT_RGB15, MVT_IA16, MVT_IA8, MVT_IA4, MVT_I8, MVT_I4, MVT_CI8_RGBA16, MVT_CI4_RGBA16, MVT_CI8_IA16, MVT_CI4_IA16 };

struct MvTexImage { int w = 0, h = 0; std::vector<uint8_t> rgba; std::string note; };   // note = why it is empty / the codec

// Pure per-format decoder: one pixel value of a format -> RGBA8 (CI formats: the 16-bit palette entry).
void mv_tex_value_rgba(int format, uint32_t v, uint8_t out[4]);
// Raw N64 pixel data (rows tight, no padding) -> RGBA8. CI formats take the palette (16-bit entries, RGBA16 or IA16 by format).
bool mv_tex_decode_packed(int format, const uint8_t* data, size_t len, int w, int h, const uint16_t* pal, int npal, std::vector<uint8_t>& rgba);

bool mv_tex_set_imagelist(const std::string& csvPath);          // "offset,size,..." per texture number
bool mv_tex_load(const uint8_t* rom, size_t romSize, int texnum, MvTexImage& out);   // the largest image of the texture
int mv_tex_selftest();                                          // 0 = all formats decode as expected (prints each failure)
bool mv_inflate_raw(const uint8_t* src, size_t n, std::vector<uint8_t>& out);   // mv_zlib.cpp
