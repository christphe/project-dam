/* ge_tex.c -- texture resolution: key -> RGBA + entry, mod over pack over ge007.ptexpack (see ge_tex.h). */
#include "ge_tex.h"
#include "ge_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CACHE_BUCKETS 4096
#define MAX_WARN 2
#define WARN_LEN 1400

typedef struct Entry {
    struct Entry *next;
    char key[TEXPACK_KEY_LEN];
    int ok;
    uint8_t *rgba;
    GeTexImage img;                    /* rgba is set on return */
    int nwarn;
    char warn[MAX_WARN][WARN_LEN];
} Entry;

struct GeTexSet {
    TexPackSrc *mod;
    TexPackSrc *pack, *base;           /* pack == base when ge007 is the chosen pack (owned once, by base) */
    char modRoot[1024], packRoot[1024], baseRoot[1024];
    char note[512];
    int opened;
    Entry *buckets[CACHE_BUCKETS];
};

static void cacheClear(GeTexSet *s)
{
    for (int i = 0; i < CACHE_BUCKETS; i++) {
        Entry *e = s->buckets[i];
        while (e) {
            Entry *n = e->next;
            free(e->rgba);
            free(e);
            e = n;
        }
        s->buckets[i] = NULL;
    }
}

GeTexSet *geTexSetNew(void) { return (GeTexSet *)calloc(1, sizeof(GeTexSet)); }

static void closePack(GeTexSet *s)
{
    if (s->pack && s->pack != s->base) texpackSrcClose(s->pack);
    texpackSrcClose(s->base);
    s->pack = s->base = NULL;
}

void geTexSetFree(GeTexSet *s)
{
    if (!s) return;
    cacheClear(s);
    texpackSrcClose(s->mod);
    closePack(s);
    free(s);
}

static void copyStr(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src ? src : "");
}

int geTexSetOpen(GeTexSet *s, const char *modRoot, const char *packRoot, const char *baseRoot)
{
    modRoot = modRoot ? modRoot : "";
    packRoot = packRoot ? packRoot : "";
    baseRoot = baseRoot ? baseRoot : "";
    cacheClear(s);
    texpackSrcClose(s->mod);
    s->mod = modRoot[0] ? texpackSrcOpen(modRoot) : NULL;
    copyStr(s->modRoot, sizeof s->modRoot, modRoot);
    if (s->opened && !strcmp(packRoot, s->packRoot) && !strcmp(baseRoot, s->baseRoot)) return 0;   /* the pack is read once */
    s->opened = 1;
    copyStr(s->packRoot, sizeof s->packRoot, packRoot);
    copyStr(s->baseRoot, sizeof s->baseRoot, baseRoot);
    s->note[0] = 0;
    closePack(s);
    s->base = baseRoot[0] ? texpackSrcOpen(baseRoot) : NULL;
    if (packRoot[0]) {
        if (!strcmp(packRoot, baseRoot)) s->pack = s->base;
        else {
            TexPackSrc *p = texpackSrcOpen(packRoot);
            if (!p) snprintf(s->note, sizeof s->note, "texture pack %s cannot be read: ge007 is used", packRoot);
            else if (!s->base) {
                snprintf(s->note, sizeof s->note, "texture pack %s: no ge007.ptexpack to check it against, so it is not used", packRoot);
                texpackSrcClose(p);
            } else {
                const int missing = texpackMissing(p, s->base);
                if (missing > 0) {
                    snprintf(s->note, sizeof s->note, "texture pack %s is incomplete: %d textures missing, ge007 is used", packRoot, missing);
                    texpackSrcClose(p);
                } else s->pack = p;
            }
        }
    }
    if (!s->pack) s->pack = s->base;   /* exactly one pack is always chosen: ge007, when there is one */
    return 1;
}

const char *geTexSetNote(const GeTexSet *s) { return s->note; }
const TexPackSrc *geTexSetMod(const GeTexSet *s) { return s->mod; }
const TexPackSrc *geTexSetPack(const GeTexSet *s) { return s->pack; }
const TexPackSrc *geTexSetBase(const GeTexSet *s) { return s->base; }

/* ---- resolution ----------------------------------------------------------------------------------------------------- */

static void addWarn(Entry *e, const char *msg)
{
    if (e->nwarn >= MAX_WARN) return;
    copyStr(e->warn[e->nwarn++], WARN_LEN, msg);
}

/* A picture whose alpha is 255 everywhere under alpha "luminance" gets (299 R + 587 G + 114 B) / 1000 as alpha. */
static void applyLuminanceAlpha(uint8_t *rgba, size_t npix)
{
    for (size_t i = 0; i < npix; i++) if (rgba[i * 4 + 3] != 255) return;
    for (size_t i = 0; i < npix; i++) {
        uint8_t *p = rgba + i * 4;
        p[3] = (uint8_t)((299 * p[0] + 587 * p[1] + 114 * p[2]) / 1000);
    }
}

static void loadEntry(GeTexSet *s, const char *key, Entry *e)
{
    const TexPackSrc *mod = s->mod, *pack = s->pack;
    for (int pass = 0; pass < 2; pass++) {   /* the mod's picture first; one that cannot be used gives way to the pack's */
        const TexPackSrc *m = pass == 0 ? mod : NULL;
        TexPackResolved r;
        const TexPackSrc *src;
        char path[sizeof e->img.path], msg[WARN_LEN];
        uint8_t *file = NULL, *px = NULL;
        size_t len = 0;
        int w = 0, h = 0, ok;
        if (pass == 1 && !mod) break;
        if (!texpackResolve(m, pack, key, &r)) continue;
        if (r.modImageMissing) {
            snprintf(msg, sizeof msg, "texture %s: the mod names a picture it does not hold (%s)", key, texpackSrcRoot(mod));
            addWarn(e, msg);
        }
        if (r.imageFrom == TEXPACK_FROM_NONE) continue;
        src = r.imageFrom == TEXPACK_FROM_MOD ? mod : pack;
        snprintf(path, sizeof path, "%s/%s", texpackSrcRoot(src), r.tex.image);
        if (!texpackResolvedRead(m, pack, &r, &file, &len)) { snprintf(msg, sizeof msg, "%s: cannot be read", path); addWarn(e, msg); continue; }
        ok = gePngDecode(file, len, &w, &h, &px);
        free(file);
        if (!ok) { snprintf(msg, sizeof msg, "%s: not readable (an 8-bit PNG, not interlaced, is expected)", path); addWarn(e, msg); continue; }
        if (r.tex.alphaLuminance) applyLuminanceAlpha(px, (size_t)w * (size_t)h);
        e->rgba = px;
        e->img.w = w;
        e->img.h = h;
        e->img.rgba = px;
        e->img.tex = r.tex;
        e->img.from = r.imageFrom;
        e->img.source = r.imageFrom == TEXPACK_FROM_MOD ? GE_TEX_FROM_MOD : (pack && texpackSrcMeta(pack)->base ? GE_TEX_FROM_BASE : GE_TEX_FROM_PACK);
        copyStr(e->img.path, sizeof e->img.path, path);
        e->ok = 1;
        return;
    }
}

static unsigned hashKey(const char *k)
{
    unsigned h = 2166136261u;
    while (*k) h = (h ^ (uint8_t)*k++) * 16777619u;
    return h % CACHE_BUCKETS;
}

int geTexResolve(GeTexSet *s, const char *key, GeTexImage *out, GeTexWarnFn warn, void *user)
{
    Entry *e;
    unsigned b;
    if (!s || !key || !texpackKeyValid(key)) return 0;
    b = hashKey(key);
    for (e = s->buckets[b]; e; e = e->next) if (!strcmp(e->key, key)) break;
    if (!e) {
        e = (Entry *)calloc(1, sizeof *e);
        if (!e) return 0;
        copyStr(e->key, sizeof e->key, key);
        loadEntry(s, key, e);
        e->next = s->buckets[b];
        s->buckets[b] = e;
    }
    if (warn) for (int i = 0; i < e->nwarn; i++) warn(user, e->warn[i]);
    if (!e->ok) return 0;
    if (out) *out = e->img;
    return 1;
}

/* ---- the packs of a folder ------------------------------------------------------------------------------------------ */

static int existsPath(const char *p)
{
    struct stat st;
    return p && p[0] && stat(p, &st) == 0;
}

static void slashes(char *p)
{
    for (; *p; p++) if (*p == '\\') *p = '/';
}

void geTexBaseRoot(const char *packsDir, const char *exportRoot, char *out, size_t outlen)
{
    char p[1100];
    out[0] = 0;
    if (packsDir && packsDir[0]) {
        snprintf(p, sizeof p, "%s/ge007.ptexpack", packsDir);
        if (existsPath(p)) { snprintf(out, outlen, "%s", p); slashes(out); return; }
    }
    if (exportRoot && exportRoot[0]) {
        snprintf(p, sizeof p, "%s/ge007.ptexpack", exportRoot);
        if (existsPath(p)) { snprintf(out, outlen, "%s", p); slashes(out); return; }
    }
}

void geTexPackPath(const char *pref, const char *packsDir, const char *exportRoot, char *out, size_t outlen)
{
    if (!pref || !pref[0] || !strcmp(pref, "ge007.ptexpack")) geTexBaseRoot(packsDir, exportRoot, out, outlen);
    else snprintf(out, outlen, "%s/%s", packsDir ? packsDir : "", pref);
}

int geTexPackList(const char *packsDir, const char *exportRoot, GeTexPackChoice *out, int max)
{
    char baseRoot[1024];
    TexPackSrc *base;
    GeTexPackChoice b;
    TexPackInfo *infos;
    int n = 0, total = 0;
    geTexBaseRoot(packsDir, exportRoot, baseRoot, sizeof baseRoot);
    base = baseRoot[0] ? texpackSrcOpen(baseRoot) : NULL;

    memset(&b, 0, sizeof b);
    copyStr(b.file, sizeof b.file, "ge007.ptexpack");
    copyStr(b.path, sizeof b.path, baseRoot);
    b.base = 1;
    copyStr(b.name, sizeof b.name, "GoldenEye 007");
    if (base) {
        const TexPackMeta *m = texpackSrcMeta(base);
        if (m->name[0]) copyStr(b.name, sizeof b.name, m->name);
        copyStr(b.author, sizeof b.author, m->author);
        copyStr(b.description, sizeof b.description, m->description);
        if (!texpackSrcHasTexturesJson(base)) copyStr(b.reason, sizeof b.reason, "no textures.json: export again");
    } else {
        copyStr(b.reason, sizeof b.reason, "not exported yet");
    }
    if (total < max && out) out[total] = b;
    total++;

    infos = (TexPackInfo *)calloc(256, sizeof *infos);
    if (infos && packsDir) n = texpackScan(packsDir, infos, 256);
    if (n > 256) n = 256;
    for (int i = 0; i < n; i++) {
        GeTexPackChoice c;
        TexPackSrc *p;
        if (!strcmp(infos[i].file, "ge007.ptexpack")) continue;   /* the base is listed first */
        memset(&c, 0, sizeof c);
        copyStr(c.file, sizeof c.file, infos[i].file);
        snprintf(c.path, sizeof c.path, "%s/%s", packsDir, c.file);
        copyStr(c.name, sizeof c.name, infos[i].name);
        copyStr(c.author, sizeof c.author, infos[i].author);
        copyStr(c.description, sizeof c.description, infos[i].description);
        p = texpackSrcOpen(c.path);
        if (!p) copyStr(c.reason, sizeof c.reason, "cannot be read");
        else if (!base || !texpackSrcHasTexturesJson(base)) copyStr(c.reason, sizeof c.reason, "no ge007 pack to check it against");
        else {
            c.missing = texpackMissing(p, base);
            if (c.missing > 0) snprintf(c.reason, sizeof c.reason, "incomplete: %d textures missing", c.missing);
        }
        texpackSrcClose(p);
        if (total < max && out) out[total] = c;
        total++;
    }
    free(infos);
    texpackSrcClose(base);
    return total;
}
