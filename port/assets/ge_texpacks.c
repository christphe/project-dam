/* Texture packs (.ptexpack): see ge_texpacks.h. */
#include "ge_texpacks.h"
#include "ge_pkg.h"
#include "cJSON.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SUFFIX ".ptexpack"
#define SUFFIX_LEN 9

static int cmpInfo(const void *a, const void *b)
{
    return strcmp(((const TexPackInfo *)a)->file, ((const TexPackInfo *)b)->file);
}

static void copyStr(char *dst, size_t n, const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsString(v) && v->valuestring) snprintf(dst, n, "%s", v->valuestring);
}

int texpackScan(const char *dir, TexPackInfo *out, int max)
{
    DIR *d = dir && dir[0] ? opendir(dir) : NULL;
    struct dirent *e;
    int n = 0, kept = 0;
    if (!d) return 0;
    while ((e = readdir(d)) != NULL) {
        const size_t len = strlen(e->d_name);
        char full[1200];
        struct stat st;
        if (e->d_name[0] == '.' || len <= SUFFIX_LEN || len >= TEXPACK_FILE_LEN) continue;
        if (strcmp(e->d_name + len - SUFFIX_LEN, SUFFIX) != 0) continue;
        snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
        if (stat(full, &st) != 0) continue;
        if ((st.st_mode & S_IFMT) != S_IFREG && (st.st_mode & S_IFMT) != S_IFDIR) continue;
        n++;
        if (kept < max && out) {
            TexPackInfo *p = &out[kept++];
            memset(p, 0, sizeof *p);
            snprintf(p->file, sizeof p->file, "%s", e->d_name);
            snprintf(p->name, sizeof p->name, "%.*s", (int)(len - SUFFIX_LEN), e->d_name);
        }
    }
    closedir(d);
    if (out && kept > 1) qsort(out, (size_t)kept, sizeof *out, cmpInfo);
    for (int i = 0; out && i < kept; i++) {   /* pack.json, when there is one */
        char path[1400];
        uint8_t *buf = NULL;
        size_t blen = 0;
        snprintf(path, sizeof path, "%s/%s/pack.json", dir, out[i].file);
        if (!pkgReadFile(path, &buf, &blen)) continue;
        cJSON *j = cJSON_Parse((const char *)buf);
        if (j) {
            copyStr(out[i].name, sizeof out[i].name, j, "name");
            copyStr(out[i].author, sizeof out[i].author, j, "author");
            copyStr(out[i].description, sizeof out[i].description, j, "description");
            cJSON_Delete(j);
        }
        pkgFree(buf);
    }
    return n;
}

void texpackPicturePath(const char *root, int texnum, char *out, size_t outlen)
{
    snprintf(out, outlen, "%s/textures/%d.png", root, texnum);
}

int texpackTexnumOfName(const char *name)
{
    const char *b = name, *p;
    size_t len;
    int n = 0;
    for (p = name; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    len = strlen(b);
    if (len < 5 || len > 8 || strcmp(b + len - 4, ".png") != 0) return -1;
    for (size_t i = 0; i + 4 < len; i++) {
        if (b[i] < '0' || b[i] > '9') return -1;
        n = n * 10 + (b[i] - '0');
    }
    return n <= TEXPACK_MAX_TEXNUM ? n : -1;
}

/* ---- the index of one pack ---------------------------------------------------------------------------------------- */

typedef struct TexPackEntry { long long off, size; } TexPackEntry;   /* size 0: none */

struct TexPackIndex {
    int tar;                                  /* 1: every picture is a slice of `file`; 0: `file` is the folder */
    char file[1024];
    int count;
    TexPackEntry e[TEXPACK_MAX_TEXNUM + 1];
};

static void addEntry(void *u, const char *name, long long off, long long size)
{
    TexPackIndex *ix = (TexPackIndex *)u;
    int n;
    if (strncmp(name, "textures/", 9) != 0 || strchr(name + 9, '/') || size <= 0) return;
    n = texpackTexnumOfName(name + 9);
    if (n < 0) return;
    if (!ix->e[n].size) ix->count++;
    ix->e[n].off = off;
    ix->e[n].size = size;
}

TexPackIndex *texpackIndexOpen(const char *root)
{
    struct stat st;
    TexPackIndex *ix;
    if (!root || !root[0] || stat(root, &st) != 0 || strlen(root) >= sizeof ix->file) return NULL;
    ix = (TexPackIndex *)calloc(1, sizeof *ix);
    if (!ix) return NULL;
    snprintf(ix->file, sizeof ix->file, "%s", root);
    if ((st.st_mode & S_IFMT) == S_IFREG) {
        ix->tar = 1;
        if (!pkgForEachEntry(root, addEntry, ix)) { free(ix); return NULL; }
    } else if ((st.st_mode & S_IFMT) == S_IFDIR) {
        char dir[1100];
        DIR *d;
        struct dirent *de;
        snprintf(dir, sizeof dir, "%s/textures", root);
        d = opendir(dir);
        while (d && (de = readdir(d)) != NULL) {
            const int n = texpackTexnumOfName(de->d_name);
            char full[1400];
            struct stat fs;
            if (n < 0) continue;
            snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
            if (stat(full, &fs) != 0 || (fs.st_mode & S_IFMT) != S_IFREG || fs.st_size <= 0) continue;
            if (!ix->e[n].size) ix->count++;
            ix->e[n].off = 0;
            ix->e[n].size = (long long)fs.st_size;
        }
        if (d) closedir(d);
    } else {
        free(ix);
        return NULL;
    }
    return ix;
}

void texpackIndexClose(TexPackIndex *ix) { free(ix); }

int texpackIndexCount(const TexPackIndex *ix) { return ix ? ix->count : 0; }

int texpackIndexHas(const TexPackIndex *ix, int texnum)
{
    return ix && texnum >= 0 && texnum <= TEXPACK_MAX_TEXNUM && ix->e[texnum].size > 0;
}

int texpackIndexRead(const TexPackIndex *ix, int texnum, uint8_t **out, size_t *len)
{
    char path[1400];
    FILE *f;
    uint8_t *buf;
    long long size;
    *out = NULL;
    if (len) *len = 0;
    if (!texpackIndexHas(ix, texnum)) return 0;
    size = ix->e[texnum].size;
    if (size > 64ll * 1024 * 1024) return 0;
    if (ix->tar) snprintf(path, sizeof path, "%s", ix->file);
    else texpackPicturePath(ix->file, texnum, path, sizeof path);
    f = fopen(path, "rb");
    if (!f) return 0;
    if (ix->tar && fseek(f, (long)ix->e[texnum].off, SEEK_SET) != 0) { fclose(f); return 0; }
    buf = (uint8_t *)malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return 0; }
    fclose(f);
    *out = buf;
    if (len) *len = (size_t)size;
    return 1;
}


/* ---- textures.json ------------------------------------------------------------------------------------------------- */

static const char *const kSurfaceNames[16] = {
    "default", "stone", "wood", "metal", "glass", "water", "snow", "dirt", "mud", "tile", "metalobj", "chr", "glass_xlu",
    "class13", "class14", "class15"
};

const char *texpackSurfaceName(int n) { return n >= 0 && n < 16 ? kSurfaceNames[n] : NULL; }

int texpackSurfaceFromName(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < 16; i++) if (!strcmp(name, kSurfaceNames[i])) return i;
    return -1;
}

int texpackKeyIsNumber(const char *key)
{
    size_t n = key ? strlen(key) : 0;
    if (n == 0 || n > 9) return 0;
    if (n > 1 && key[0] == '0') return 0;
    for (size_t i = 0; i < n; i++) if (key[i] < '0' || key[i] > '9') return 0;
    return 1;
}

int texpackKeyValid(const char *key)
{
    size_t n = key ? strlen(key) : 0;
    if (n == 0 || n > 63) return 0;
    for (size_t i = 0; i < n; i++) {
        const char c = key[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/')) return 0;
    }
    return 1;
}

const char *texpackFromName(int from) { return from == TEXPACK_FROM_MOD ? "mod" : from == TEXPACK_FROM_PACK ? "pack" : "none"; }

struct TexPackSrc {
    char root[1024];
    TexPackMeta meta;
    int haveJson;
    TexPackTex *tex;
    int ntex;
    char **files;            /* every file of the source, relative, sorted */
    int nfiles, capfiles;
};

static void addFile(TexPackSrc *s, const char *name)
{
    if (s->nfiles == s->capfiles) {
        const int cap = s->capfiles ? s->capfiles * 2 : 256;
        char **n = (char **)realloc(s->files, sizeof(char *) * (size_t)cap);
        if (!n) return;
        s->files = n;
        s->capfiles = cap;
    }
    if (strncmp(name, "./", 2) == 0) name += 2;
    s->files[s->nfiles] = (char *)malloc(strlen(name) + 1);
    if (s->files[s->nfiles]) strcpy(s->files[s->nfiles++], name);
}

static void tarFileCb(void *u, const char *name, long long off, long long size)
{
    (void)off;
    if (size > 0) addFile((TexPackSrc *)u, name);
}

static void walkDir(TexPackSrc *s, const char *base, const char *rel, int depth)
{
    char dir[1500];
    DIR *d;
    struct dirent *e;
    if (depth > 8 || s->nfiles > 200000) return;
    if (rel[0]) snprintf(dir, sizeof dir, "%s/%s", base, rel);
    else snprintf(dir, sizeof dir, "%s", base);
    d = opendir(dir);
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        char full[1700], sub[700];
        struct stat st;
        if (e->d_name[0] == '.' || strlen(e->d_name) > 200 || strlen(rel) > 400) continue;
        if (rel[0]) snprintf(sub, sizeof sub, "%s/%s", rel, e->d_name);
        else snprintf(sub, sizeof sub, "%s", e->d_name);
        snprintf(full, sizeof full, "%s/%s", base, sub);
        if (stat(full, &st) != 0) continue;
        if ((st.st_mode & S_IFMT) == S_IFDIR) walkDir(s, base, sub, depth + 1);
        else if ((st.st_mode & S_IFMT) == S_IFREG && st.st_size > 0) addFile(s, sub);
    }
    closedir(d);
}

static int cmpStrp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static int cmpTex(const void *a, const void *b) { return strcmp(((const TexPackTex *)a)->key, ((const TexPackTex *)b)->key); }

/* a path inside a source: no drive, no leading slash, no empty or ".." component */
static int relOk(const char *rel)
{
    const char *p = rel;
    if (!rel || !rel[0] || rel[0] == '/' || rel[0] == '\\' || strchr(rel, ':') || strchr(rel, '\\') || strlen(rel) > 400) return 0;
    for (;;) {
        const char *q = strchr(p, '/');
        const size_t n = q ? (size_t)(q - p) : strlen(p);
        if (n == 0 || (n == 2 && p[0] == '.' && p[1] == '.')) return 0;
        if (!q) break;
        p = q + 1;
    }
    return 1;
}

static void parseEntry(TexPackTex *t, const cJSON *o)
{
    const cJSON *v;
    t->surface = t->impactSound = TEXPACK_SURFACE_NONE;
    if (!cJSON_IsObject(o)) return;
    v = cJSON_GetObjectItemCaseSensitive(o, "image");
    if (cJSON_IsNull(v)) {
        t->fields |= TEXPACK_F_IMAGE;
        t->imageNull = 1;
    } else if (cJSON_IsString(v) && v->valuestring && v->valuestring[0] && strlen(v->valuestring) < sizeof t->image) {
        t->fields |= TEXPACK_F_IMAGE;
        snprintf(t->image, sizeof t->image, "%s", v->valuestring);
    }
    v = cJSON_GetObjectItemCaseSensitive(o, "surface");
    if (cJSON_IsString(v) && texpackSurfaceFromName(v->valuestring) >= 0) {
        t->fields |= TEXPACK_F_SURFACE;
        t->surface = texpackSurfaceFromName(v->valuestring);
    }
    v = cJSON_GetObjectItemCaseSensitive(o, "impact_sound");
    if (cJSON_IsString(v) && texpackSurfaceFromName(v->valuestring) >= 0) {
        t->fields |= TEXPACK_F_IMPACT;
        t->impactSound = texpackSurfaceFromName(v->valuestring);
    }
    v = cJSON_GetObjectItemCaseSensitive(o, "alpha");
    if (cJSON_IsString(v) && v->valuestring && (!strcmp(v->valuestring, "image") || !strcmp(v->valuestring, "luminance"))) {
        t->fields |= TEXPACK_F_ALPHA;
        t->alphaLuminance = v->valuestring[0] == 'l';
    }
    v = cJSON_GetObjectItemCaseSensitive(o, "format");
    if (cJSON_IsString(v) && v->valuestring) {
        t->fields |= TEXPACK_F_FORMAT;
        snprintf(t->format, sizeof t->format, "%s", v->valuestring);
    }
    v = cJSON_GetObjectItemCaseSensitive(o, "width");
    if (cJSON_IsNumber(v)) { t->fields |= TEXPACK_F_WIDTH; t->width = (int)v->valuedouble; }
    v = cJSON_GetObjectItemCaseSensitive(o, "height");
    if (cJSON_IsNumber(v)) { t->fields |= TEXPACK_F_HEIGHT; t->height = (int)v->valuedouble; }
    {
        const cJSON *tw = cJSON_GetObjectItemCaseSensitive(o, "tile_width"), *th = cJSON_GetObjectItemCaseSensitive(o, "tile_height");
        if (cJSON_IsNumber(tw) && cJSON_IsNumber(th)) { t->fields |= TEXPACK_F_TILE; t->tileWidth = (int)tw->valuedouble; t->tileHeight = (int)th->valuedouble; }
    }
    v = cJSON_GetObjectItemCaseSensitive(o, "detail");
    if (cJSON_IsObject(v)) {
        static const char *const n[4] = { "s_offset", "t_offset", "s_shift", "t_shift" };
        t->fields |= TEXPACK_F_DETAIL;
        for (int i = 0; i < 4; i++) {
            const cJSON *d = cJSON_GetObjectItemCaseSensitive(v, n[i]);
            if (cJSON_IsNumber(d)) t->detail[i] = (int)d->valuedouble;
        }
    }
    v = cJSON_GetObjectItemCaseSensitive(o, "normal");
    if (cJSON_IsString(v) && v->valuestring && strlen(v->valuestring) < sizeof t->normal) {
        t->fields |= TEXPACK_F_NORMAL;
        snprintf(t->normal, sizeof t->normal, "%s", v->valuestring);
    }
}

static int readJson(const TexPackSrc *s, const char *rel, cJSON **out)
{
    char path[1300];
    uint8_t *buf = NULL;
    size_t len = 0;
    *out = NULL;
    snprintf(path, sizeof path, "%s/%s", s->root, rel);
    if (!pkgReadFile(path, &buf, &len)) return 0;
    *out = cJSON_Parse((const char *)buf);
    pkgFree(buf);
    return 1;
}

TexPackSrc *texpackSrcOpen(const char *root)
{
    struct stat st;
    TexPackSrc *s;
    cJSON *j;
    if (!root || !root[0] || strlen(root) >= sizeof s->root || stat(root, &st) != 0) return NULL;
    s = (TexPackSrc *)calloc(1, sizeof *s);
    if (!s) return NULL;
    snprintf(s->root, sizeof s->root, "%s", root);
    if ((st.st_mode & S_IFMT) == S_IFREG) {
        if (!pkgForEachEntry(root, tarFileCb, s)) { texpackSrcClose(s); return NULL; }
    } else if ((st.st_mode & S_IFMT) == S_IFDIR) {
        walkDir(s, root, "", 0);
    } else {
        free(s);
        return NULL;
    }
    if (s->nfiles > 1) qsort(s->files, (size_t)s->nfiles, sizeof(char *), cmpStrp);

    if (readJson(s, "pack.json", &j) && j) {
        const cJSON *v;
        s->meta.hasPackJson = 1;
        copyStr(s->meta.name, sizeof s->meta.name, j, "name");
        copyStr(s->meta.author, sizeof s->meta.author, j, "author");
        copyStr(s->meta.description, sizeof s->meta.description, j, "description");
        copyStr(s->meta.region, sizeof s->meta.region, j, "region");
        v = cJSON_GetObjectItemCaseSensitive(j, "base");
        s->meta.base = cJSON_IsTrue(v);
        v = cJSON_GetObjectItemCaseSensitive(j, "exporter_version");
        s->meta.exporterVersion = cJSON_IsNumber(v) ? (int)v->valuedouble : 0;
        cJSON_Delete(j);
    }
    if (readJson(s, "textures.json", &j) && j) {
        const cJSON *tex = cJSON_GetObjectItemCaseSensitive(j, "textures"), *e;
        if (cJSON_IsObject(tex)) {
            const int n = cJSON_GetArraySize(tex);
            int k = 0;
            s->haveJson = 1;
            s->tex = (TexPackTex *)calloc((size_t)(n ? n : 1), sizeof(TexPackTex));
            if (!s->tex) { cJSON_Delete(j); texpackSrcClose(s); return NULL; }
            cJSON_ArrayForEach(e, tex) {
                if (!e->string || !texpackKeyValid(e->string)) continue;
                snprintf(s->tex[k].key, sizeof s->tex[k].key, "%s", e->string);
                parseEntry(&s->tex[k], e);
                k++;
            }
            s->ntex = k;
            if (k > 1) qsort(s->tex, (size_t)k, sizeof(TexPackTex), cmpTex);
        }
        cJSON_Delete(j);
    }
    return s;
}

void texpackSrcClose(TexPackSrc *s)
{
    if (!s) return;
    for (int i = 0; i < s->nfiles; i++) free(s->files[i]);
    free(s->files);
    free(s->tex);
    free(s);
}

const char *texpackSrcRoot(const TexPackSrc *s) { return s ? s->root : ""; }
const TexPackMeta *texpackSrcMeta(const TexPackSrc *s) { static const TexPackMeta none; return s ? &s->meta : &none; }
int texpackSrcHasTexturesJson(const TexPackSrc *s) { return s && s->haveJson; }
int texpackSrcCount(const TexPackSrc *s) { return s ? s->ntex : 0; }
const TexPackTex *texpackSrcAt(const TexPackSrc *s, int i) { return s && i >= 0 && i < s->ntex ? &s->tex[i] : NULL; }

const TexPackTex *texpackSrcFind(const TexPackSrc *s, const char *key)
{
    int lo = 0, hi;
    if (!s || !key) return NULL;
    hi = s->ntex - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2, c = strcmp(key, s->tex[mid].key);
        if (c == 0) return &s->tex[mid];
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return NULL;
}

int texpackSrcHasFile(const TexPackSrc *s, const char *rel)
{
    int lo = 0, hi;
    if (!s || !relOk(rel)) return 0;
    hi = s->nfiles - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2, c = strcmp(rel, s->files[mid]);
        if (c == 0) return 1;
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return 0;
}

int texpackSrcReadFile(const TexPackSrc *s, const char *rel, uint8_t **out, size_t *len)
{
    char path[1700];
    size_t n = 0;
    *out = NULL;
    if (len) *len = 0;
    if (!texpackSrcHasFile(s, rel)) return 0;
    snprintf(path, sizeof path, "%s/%s", s->root, rel);
    if (!pkgReadFile(path, out, &n)) return 0;
    if (len) *len = n;
    return 1;
}

/* the picture path an entry names: its image, or the default textures/<key>.png; NULL when "image": null */
static const char *entryImage(const TexPackTex *t, char *buf, size_t n)
{
    if (t->fields & TEXPACK_F_IMAGE) return t->imageNull ? NULL : t->image;
    snprintf(buf, n, "textures/%s.png", t->key);
    return buf;
}

int texpackMissing(const TexPackSrc *pack, const TexPackSrc *base)
{
    int missing = 0;
    for (int i = 0; i < texpackSrcCount(base); i++) {
        const TexPackTex *b = texpackSrcAt(base, i), *p;
        char bb[128], pb[128];
        p = texpackSrcFind(pack, b->key);
        if (!p) { missing++; continue; }
        if (entryImage(b, bb, sizeof bb)) {
            const char *pi = entryImage(p, pb, sizeof pb);
            if (!pi || !texpackSrcHasFile(pack, pi)) missing++;
        }
    }
    return missing;
}

int texpackResolve(const TexPackSrc *mod, const TexPackSrc *pack, const char *key, TexPackResolved *out)
{
    const TexPackTex *m, *p;
    char defp[128], tmp[128];
    const char *pi;
    int modPic;
    memset(out, 0, sizeof *out);
    out->imageFrom = TEXPACK_FROM_NONE;
    if (!key || !texpackKeyValid(key)) return 0;
    m = texpackSrcFind(mod, key);
    p = texpackSrcFind(pack, key);
    snprintf(defp, sizeof defp, "textures/%s.png", key);
    modPic = mod && texpackSrcHasFile(mod, defp);
    if (!m && !p && !modPic) return 0;

    if (p) {
        out->tex = *p;
        out->inPack = 1;
    } else {
        snprintf(out->tex.key, sizeof out->tex.key, "%s", key);
        out->tex.surface = out->tex.impactSound = TEXPACK_SURFACE_NONE;
    }
    out->tex.image[0] = 0;
    out->tex.imageNull = 1;
    pi = p ? entryImage(p, tmp, sizeof tmp) : NULL;
    if (pi) {
        snprintf(out->tex.image, sizeof out->tex.image, "%s", pi);
        out->tex.imageNull = 0;
        out->imageFrom = TEXPACK_FROM_PACK;
    }
    if (m) {   /* field by field */
        const unsigned f = m->fields;
        if (f & TEXPACK_F_SURFACE) { out->tex.surface = m->surface; out->modFields |= TEXPACK_F_SURFACE; }
        if (f & TEXPACK_F_IMPACT) { out->tex.impactSound = m->impactSound; out->modFields |= TEXPACK_F_IMPACT; }
        if (f & TEXPACK_F_ALPHA) { out->tex.alphaLuminance = m->alphaLuminance; out->modFields |= TEXPACK_F_ALPHA; }
        if (f & TEXPACK_F_FORMAT) { snprintf(out->tex.format, sizeof out->tex.format, "%s", m->format); out->modFields |= TEXPACK_F_FORMAT; }
        if (f & TEXPACK_F_WIDTH) { out->tex.width = m->width; out->modFields |= TEXPACK_F_WIDTH; }
        if (f & TEXPACK_F_HEIGHT) { out->tex.height = m->height; out->modFields |= TEXPACK_F_HEIGHT; }
        if (f & TEXPACK_F_TILE) { out->tex.tileWidth = m->tileWidth; out->tex.tileHeight = m->tileHeight; out->modFields |= TEXPACK_F_TILE; }
        if (f & TEXPACK_F_DETAIL) { memcpy(out->tex.detail, m->detail, sizeof out->tex.detail); out->modFields |= TEXPACK_F_DETAIL; }
        if (f & TEXPACK_F_NORMAL) { snprintf(out->tex.normal, sizeof out->tex.normal, "%s", m->normal); out->modFields |= TEXPACK_F_NORMAL; }
    }
    {   /* the mod's picture: its image path, else the default textures/<key>.png when the mod has it */
        const int modNull = m && (m->fields & TEXPACK_F_IMAGE) && m->imageNull;
        const char *mi = NULL;
        if (modNull) {   /* a mod cannot remove a picture (texture-packs.md): "image": null is ignored */
            if (modPic) mi = defp;
        } else if (m && (m->fields & TEXPACK_F_IMAGE)) {
            mi = m->image;
        } else if (modPic) {
            mi = defp;
        }
        if (mi) {
            if (texpackSrcHasFile(mod, mi)) {
                snprintf(out->tex.image, sizeof out->tex.image, "%s", mi);
                out->tex.imageNull = 0;
                out->imageFrom = TEXPACK_FROM_MOD;
                out->modFields |= TEXPACK_F_IMAGE;
            } else {
                out->modImageMissing = 1;   /* named by the mod, absent: the pack's picture stays */
            }
        }
    }
    out->tex.fields |= TEXPACK_F_IMAGE;
    if (!p) {   /* a new texture: a picture and a surface are the minimum */
        if (out->imageFrom == TEXPACK_FROM_NONE || out->tex.surface == TEXPACK_SURFACE_NONE) return 0;
        if (out->tex.impactSound == TEXPACK_SURFACE_NONE) out->tex.impactSound = out->tex.surface;
    }
    return 1;
}

int texpackResolvedRead(const TexPackSrc *mod, const TexPackSrc *pack, const TexPackResolved *r, uint8_t **out, size_t *len)
{
    *out = NULL;
    if (len) *len = 0;
    if (r->imageFrom == TEXPACK_FROM_MOD) return texpackSrcReadFile(mod, r->tex.image, out, len);
    if (r->imageFrom == TEXPACK_FROM_PACK) return texpackSrcReadFile(pack, r->tex.image, out, len);
    return 0;
}
