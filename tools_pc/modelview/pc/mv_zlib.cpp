// modelview: inflate behind the zlib API subset of shim/zlib.h (RFC 1951; the decoder is seaview's, copied).
#include "shim/zlib.h"
#include <stdint.h>
#include <string.h>
#include <vector>

namespace {
struct Bits { const uint8_t* p; size_t n, pos = 0; uint32_t buf = 0; int cnt = 0;
    int get(int k) { while (cnt < k) { if (pos >= n) return -1; buf |= (uint32_t)p[pos++] << cnt; cnt += 8; } int v = buf & ((1u << k) - 1); buf >>= k; cnt -= k; return v; } };
struct Huff { uint16_t count[16], sym[288]; };
void huffBuild(Huff& h, const uint8_t* len, int n) {
    memset(h.count, 0, sizeof h.count); for (int i = 0; i < n; i++) h.count[len[i]]++; h.count[0] = 0;
    uint16_t offs[16]; offs[1] = 0; for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h.count[i];
    for (int i = 0; i < n; i++) if (len[i]) h.sym[offs[len[i]]++] = (uint16_t)i;
}
int huffDecode(Bits& b, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int l = 1; l < 16; l++) { int bit = b.get(1); if (bit < 0) return -1; code |= bit; int c = h.count[l];
        if (code - c < first) { return h.sym[index + (code - first)]; } index += c; first += c; first <<= 1; code <<= 1; }
    return -1;
}
bool inflateRaw(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    static const uint16_t lb[] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
    static const uint16_t le[] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
    static const uint16_t db[] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
    static const uint16_t de[] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    Bits b{ src, n }; int last;
    do {
        last = b.get(1); int type = b.get(2); if (last < 0 || type < 0) return false;
        if (type == 0) {
            b.buf = 0; b.cnt = 0; if (b.pos + 4 > n) return false;
            size_t len = src[b.pos] | src[b.pos + 1] << 8; b.pos += 4; if (b.pos + len > n) return false;
            out.insert(out.end(), src + b.pos, src + b.pos + len); b.pos += len;
        } else if (type == 1 || type == 2) {
            Huff lh, dh; uint8_t lens[320];
            if (type == 1) { int i = 0; for (; i < 144; i++) lens[i] = 8; for (; i < 256; i++) lens[i] = 9; for (; i < 280; i++) lens[i] = 7; for (; i < 288; i++) lens[i] = 8; huffBuild(lh, lens, 288); for (i = 0; i < 30; i++) lens[i] = 5; huffBuild(dh, lens, 30); }
            else {
                int nl = b.get(5) + 257, nd = b.get(5) + 1, nc = b.get(4) + 4; if (nl < 257 || nd < 1 || nc < 4) return false;
                static const uint8_t ord[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
                uint8_t cl[19] = {}; for (int i = 0; i < nc; i++) { int v = b.get(3); if (v < 0) return false; cl[ord[i]] = (uint8_t)v; }
                Huff ch; huffBuild(ch, cl, 19); int i = 0;
                while (i < nl + nd) {
                    int s = huffDecode(b, ch); if (s < 0) return false;
                    if (s < 16) lens[i++] = (uint8_t)s;
                    else { int rep, v = 0; if (s == 16) { if (!i) return false; v = lens[i - 1]; rep = 3 + b.get(2); } else if (s == 17) rep = 3 + b.get(3); else rep = 11 + b.get(7);
                        if (i + rep > nl + nd) { return false; } while (rep--) lens[i++] = (uint8_t)v; }
                }
                huffBuild(lh, lens, nl); huffBuild(dh, lens + nl, nd);
            }
            for (;;) {
                int s = huffDecode(b, lh); if (s < 0) return false;
                if (s < 256) out.push_back((uint8_t)s); else if (s == 256) break;
                else { s -= 257; if (s >= 29) return false; int len = lb[s] + b.get(le[s]); int ds = huffDecode(b, dh); if (ds < 0 || ds >= 30) return false;
                    size_t dist = db[ds] + b.get(de[ds]); if (dist > out.size()) return false; for (int k = 0; k < len; k++) out.push_back(out[out.size() - dist]); }
            }
        } else return false;
    } while (!last);
    return true;
}
}  // namespace
bool mv_inflate_raw(const uint8_t* src, size_t n, std::vector<uint8_t>& out) { return inflateRaw(src, n, out); }
namespace {
struct St { std::vector<uint8_t> data; size_t pos = 0; bool done = false; };
}  // namespace

extern "C" {
int inflateInit2(z_stream* z, int) { z->state = new St; return Z_OK; }
int inflate(z_stream* z, int) {   // the whole stream on the first call, then handed out in the caller's chunks
    St* s = (St*)z->state;
    if (!s->done) { if (!inflateRaw(z->next_in, z->avail_in, s->data)) return -3; s->done = true; }
    const size_t n = s->data.size() - s->pos < z->avail_out ? s->data.size() - s->pos : z->avail_out;
    memcpy(z->next_out, s->data.data() + s->pos, n);
    s->pos += n; z->avail_out -= (uInt)n;
    return s->pos == s->data.size() ? Z_STREAM_END : Z_OK;
}
int inflateEnd(z_stream* z) { delete (St*)z->state; z->state = nullptr; return Z_OK; }
int deflateInit2(z_stream*, int, int, int, int, int) { return -2; }
int deflate(z_stream*, int) { return -2; }
int deflateEnd(z_stream*) { return -2; }
}
