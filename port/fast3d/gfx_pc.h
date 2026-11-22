#ifndef GFX_PC_H
#define GFX_PC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unordered_map>
#include <list>
#include <cstddef>

#include <PR/gbi.h>

#include "system.h"

#define SCREEN_WIDTH ((int32_t)gfx_current_native_viewport.width)
#define SCREEN_HEIGHT ((int32_t)gfx_current_native_viewport.height)

extern uintptr_t gfxFramebuffer;

struct GfxRenderingAPI;
struct GfxWindowManagerAPI;

struct TextureCacheKey {
    const uint8_t* texture_addr;
    const uint8_t* palette_addrs[2];
    uint8_t fmt, siz;
    uint8_t palette_index;
    // D74: the same source address can be loaded with different sizes
    // (gDPLoadBlock mip chains vs partial loads); include the size so a
    // truncated first import cannot poison later full imports.
    uint32_t size_bytes;
    // D217: for CI (palettized) textures, a hash of the decoded rdp.palette
    // content at import time. GE assembles weapon / character model DLs in
    // scratch arena RAM and reissues gDPLoadTLUT with different palette
    // *content* from a repeated source address, so
    // {texture_addr, palette_addrs, palette_index, size_bytes} alone collides
    // across materials/frames -> a CI tile takes a stale cache HIT decoded
    // against a different palette. Recomputed only in gfx_dp_load_tlut(), so
    // there is no per-texel or per-draw cost. Zero for non-CI textures.
    uint32_t palette_hash;

    /* Spelled out, not "= default": GCC 10 (the arm64 build's compiler) compares
     * an array member of a defaulted operator== by the two objects' addresses,
     * so two distinct keys never matched and every lookup missed (the whole
     * texture cache re-imported and evicted each frame). */
    bool operator==(const TextureCacheKey& o) const noexcept {
        return texture_addr == o.texture_addr && palette_addrs[0] == o.palette_addrs[0] &&
               palette_addrs[1] == o.palette_addrs[1] && fmt == o.fmt && siz == o.siz &&
               palette_index == o.palette_index && size_bytes == o.size_bytes && palette_hash == o.palette_hash;
    }

    struct Hasher {
        size_t operator()(const TextureCacheKey& key) const noexcept {
            uintptr_t addr = (uintptr_t)key.texture_addr;
            return (size_t)(addr ^ (addr >> 5));
        }
    };
};

typedef std::unordered_map<TextureCacheKey, struct TextureCacheValue, TextureCacheKey::Hasher> TextureCacheMap;
typedef std::pair<const TextureCacheKey, struct TextureCacheValue> TextureCacheNode;

struct TextureCacheValue {
    uint32_t texture_id;
    uint8_t cms, cmt;
    bool linear_filter;

    std::list<struct TextureCacheMapIter>::iterator lru_location;
};

struct TextureCacheMapIter {
    TextureCacheMap::iterator it;
};

extern "C" {

#include "gfx_api.h"

}

#endif
