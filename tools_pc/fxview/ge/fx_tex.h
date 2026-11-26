// fxview: GE texture decoding for every codec the effect sprites use (see fx_tex.cpp); builds on modelview's mv_tex.
#pragma once
#include "mv_tex.h"

bool fx_tex_set_imagelist(const std::string& csvPath);   // also call mv_tex_set_imagelist with the same file
bool fx_tex_decode(const uint8_t* data, size_t size, MvTexImage& out);   // one texture file: the codecs mv_tex lacks
bool fx_tex_load(const uint8_t* rom, size_t romSize, int texnum, MvTexImage& out);   // mv_tex_load, else fx_tex_decode
int fx_tex_selftest();
