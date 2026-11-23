/* Reading a stage setup for the editor: see mission_setup.h. */
#include "mission_setup.h"
#include "mission_apply.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int rd16u(const uint8_t *p) { return p[0] | (p[1] << 8); }
static int rd16s(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }
static float rdf(const uint8_t *p) { uint32_t u = rd32(p); float f; memcpy(&f, &u, 4); return f; }

/* "0x11 0x72" + a raw deflate stream: the RZ format the game's loader reads (port/src/rzdecomp.c). */
uint8_t *missionRzInflate(const uint8_t *rz, uint32_t n, uint32_t *outn)
{
    z_stream z;
    uint32_t cap = n * 6u + 4096u, got = 0;
    uint8_t *out;
    int r;
    if (n < 4 || rz[0] != 0x11 || rz[1] != 0x72) return NULL;
    out = (uint8_t *)malloc(cap);
    if (!out) return NULL;
    memset(&z, 0, sizeof z);
    if (inflateInit2(&z, -15) != Z_OK) { free(out); return NULL; }
    z.next_in = (Bytef *)(rz + 2);
    z.avail_in = n - 2;
    do {
        if (got == cap) {
            uint8_t *bigger = (uint8_t *)realloc(out, cap * 2u);
            if (!bigger) { inflateEnd(&z); free(out); return NULL; }
            out = bigger; cap *= 2u;
        }
        z.next_out = out + got;
        z.avail_out = cap - got;
        r = inflate(&z, Z_NO_FLUSH);
        got = cap - z.avail_out;
        if (r != Z_OK && r != Z_STREAM_END) { inflateEnd(&z); free(out); return NULL; }
    } while (r != Z_STREAM_END);
    inflateEnd(&z);
    *outn = got;
    return out;
}

/* manifest.csv: "name,offset,size" rows; 1 and the row when `name` is in it. */
static int findRow(const char *manifest, const char *name, uint32_t *off, uint32_t *size)
{
    FILE *f = fopen(manifest, "rb");
    char line[512];
    size_t nl = strlen(name);
    int ok = 0;
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long o, s;
        if (strncmp(line, name, nl) || line[nl] != ',') continue;
        if (sscanf(line + nl + 1, "%lu,%lu", &o, &s) == 2) { *off = (uint32_t)o; *size = (uint32_t)s; ok = 1; }
        break;
    }
    fclose(f);
    return ok;
}

int missionSetupLoadFile(const char *dataDir, const char *name, uint8_t **out, uint32_t *outn, char *err, size_t errlen)
{
    static const char *regions[] = { "ntsc-final", "pal-final", "jpn-final" };
    int anyDir = 0;
    for (size_t i = 0; i < sizeof regions / sizeof *regions; i++) {
        char man[1100], bin[1100];
        uint32_t off, size;
        FILE *f;
        uint8_t *rz;
        snprintf(man, sizeof man, "%s/pccg-%s/manifest.csv", dataDir, regions[i]);
        snprintf(bin, sizeof bin, "%s/pccg-%s/pccg.bin", dataDir, regions[i]);
        f = fopen(man, "rb");
        if (f) { anyDir = 1; fclose(f); }
        if (!findRow(man, name, &off, &size)) continue;
        f = fopen(bin, "rb");
        if (!f) continue;
        rz = (uint8_t *)malloc(size ? size : 1);
        if (!rz || fseek(f, (long)off, SEEK_SET) != 0 || fread(rz, 1, size, f) != size) {
            free(rz); fclose(f);
            snprintf(err, errlen, "cannot read %s", bin);
            return 0;
        }
        fclose(f);
        *out = missionRzInflate(rz, size, outn);
        free(rz);
        if (!*out) { snprintf(err, errlen, "cannot inflate the setup %s from %s", name, bin); return 0; }
        return 1;
    }
    if (anyDir) snprintf(err, errlen, "the converted data in %s has no setup %s", dataDir, name);
    else snprintf(err, errlen, "no converted level data in %s/pccg-<region>/: launch the game once with your ROM (it converts it on first start)", dataDir);
    return 0;
}

static int isObjectType(int t)
{
    switch (t) {
    case 1: case 3: case 4: case 5: case 6: case 7: case 8: case 10: case 11: case 12: case 13: case 17: case 20: case 21:
    case 36: case 39: case 40: case 41: case 42: case 43: case 45: case 47:
        return 1;
    default:
        return 0;
    }
}

int missionSetupDecode(const uint8_t *in, uint32_t n, float scale, SetupData *d, char *err, size_t errlen)
{
    uint32_t H[10];
    uint32_t o;
    int cap;
    memset(d, 0, sizeof *d);
    if (n < MISSION_SETUP_HDR_SIZE) { snprintf(err, errlen, "setup too small (%u)", n); return 0; }
    for (int i = 0; i < 10; i++) {
        H[i] = rd32(in + 8 * i);
        if (H[i] >= n) { snprintf(err, errlen, "setup header field %d offset %#x past the file", i, H[i]); return 0; }
    }
    if (!H[3] || !H[6]) { snprintf(err, errlen, "setup has no pads or no propDefs"); return 0; }

    /* pads (record 56 bytes, the plink slot at 0x28 is null at the end), then bound pads (80 bytes) */
    for (int bound = 0; bound < 2; bound++) {
        const uint32_t base = H[bound ? 7 : 6], rs = bound ? 80 : MISSION_SETUP_PAD_SIZE;
        if (!base) continue;
        for (int i = 0; base + (uint32_t)(i + 1) * rs <= n && rd32(in + base + (uint32_t)i * rs + 0x28); i++) {
            const uint8_t *p = in + base + (uint32_t)i * rs;
            SetupPad *np = (SetupPad *)realloc(d->pads, sizeof(SetupPad) * (size_t)(d->npads + 1));
            SetupPad *s;
            if (!np) { missionSetupFree(d); snprintf(err, errlen, "out of memory"); return 0; }
            d->pads = np;
            s = &d->pads[d->npads++];
            memset(s, 0, sizeof *s);
            s->num = bound ? 10000 + i : i;
            s->bound = bound;
            for (int a = 0; a < 3; a++) { s->pos[a] = rdf(p + 4 * a) * scale; s->up[a] = rdf(p + 12 + 4 * a); s->look[a] = rdf(p + 24 + 4 * a); }
            if (bound) {            /* struct bbox: xmin xmax ymin ymax zmin zmax */
                s->bbox[0] = rdf(p + 0x38) * scale; s->bbox[3] = rdf(p + 0x3c) * scale;
                s->bbox[1] = rdf(p + 0x40) * scale; s->bbox[4] = rdf(p + 0x44) * scale;
                s->bbox[2] = rdf(p + 0x48) * scale; s->bbox[5] = rdf(p + 0x4c) * scale;
            }
        }
    }

    if (H[5]) {                     /* AI lists: 16 bytes {pointer slot, id} until a null pointer */
        for (uint32_t q = H[5]; q + 16 <= n && rd32(in + q); q += 16) {
            int *na = (int *)realloc(d->ailists, sizeof(int) * (size_t)(d->nailists + 1));
            if (!na) { missionSetupFree(d); snprintf(err, errlen, "out of memory"); return 0; }
            d->ailists = na;
            d->ailists[d->nailists++] = (int)rd32(in + q + 8);
        }
    }

    cap = 0;
    for (o = H[3]; ; ) {
        const uint8_t *p = in + o;
        int t, idx = d->nprops;
        SetupProp *s;
        if (o + 4 > n) { snprintf(err, errlen, "propDefs run past the file"); missionSetupFree(d); return 0; }
        t = p[3];
        if (t < 1 || t > 48 || !missionPropdefSize(t) || o + missionPropdefSize(t) > n) {
            snprintf(err, errlen, "propDefs: bad record type %d at %#x", t, o); missionSetupFree(d); return 0;
        }
        if (t == MISSION_PROPDEF_END) break;
        if (idx == cap) {
            SetupProp *np;
            cap = cap ? cap * 2 : 256;
            np = (SetupProp *)realloc(d->props, sizeof(SetupProp) * (size_t)cap);
            if (!np) { missionSetupFree(d); snprintf(err, errlen, "out of memory"); return 0; }
            d->props = np;
        }
        s = &d->props[d->nprops++];
        memset(s, 0, sizeof *s);
        s->index = idx; s->type = t; s->pad = s->model = s->item = s->ai = s->chr = s->carrier = -1;
        if (t == MISSION_REC_GUARD) {
            s->pad = rd16u(p + 6); s->model = rd16u(p + 8); s->ai = rd16u(p + 10); s->chr = rd16u(p + 4);
        } else if (isObjectType(t)) {
            s->model = rd16s(p + 4); s->pad = rd16s(p + 6);
            if (t == 8) s->item = (int8_t)p[144];
            if (t == 4) s->item = (int)rd32(p + 144);
            if (rd32(p + 8) & 0x4000u) s->carrier = s->pad;     /* PROPFLAG_ASSIGNEDTOCHR: the pad field is the carrier's chr */
        }
        if (t == MISSION_REC_GUARD || isObjectType(t)) {
            const MissionField *f;
            const int nf = missionRecFields(t, &f);
            for (int i = 0; i < nf && i < MISSION_MAX_FIELDS; i++) {
                s->f.set |= (uint64_t)1 << i;
                s->f.v[i] = missionFieldGet(&f[i], p);
            }
        }
        o += missionPropdefSize(t);
    }
    return 1;
}

void missionSetupFree(SetupData *d)
{
    free(d->pads); free(d->props); free(d->ailists);
    memset(d, 0, sizeof *d);
}
