/* Texture packs (.ptexpack): see texpacks.h. */
#include "texpacks.h"
#include "mission_pkg.h"
#include "cJSON.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

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

/* ---- PNG ---------------------------------------------------------------------------------------------------------- */

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static int iabs(int v) { return v < 0 ? -v : v; }

int texpackDecodePng(const uint8_t *file, size_t len, int *outW, int *outH, uint8_t **outRgba)
{
    uint8_t *idat = NULL, *raw = NULL, *pix = NULL, *rgba = NULL;
    uint8_t plte[256 * 3], trns[256];
    uint32_t w = 0, h = 0, pos = 8, idatn = 0, ch, stride, nplte = 0, ntrns = 0;
    int ctype = -1, ok = 0;
    z_stream z;
    *outRgba = NULL;
    *outW = *outH = 0;
    memset(trns, 255, sizeof trns);
    if (!file || len < 33 || len > 0x7FFFFFFF || memcmp(file, "\x89PNG\r\n\x1a\n", 8) != 0) return 0;
    idat = (uint8_t *)malloc(len);
    if (!idat) return 0;
    while ((size_t)pos + 12 <= len) {
        const uint32_t clen = be32(file + pos);
        const uint8_t *type = file + pos + 4, *d = file + pos + 8;
        if ((size_t)pos + 12 + clen > len) goto done;
        if (!memcmp(type, "IHDR", 4) && clen >= 13) {
            w = be32(d); h = be32(d + 4);
            if (d[8] != 8 || d[12] != 0) goto done;   /* 8-bit, not interlaced */
            ctype = d[9];
        } else if (!memcmp(type, "PLTE", 4) && clen <= sizeof plte) {
            memcpy(plte, d, clen); nplte = clen / 3;
        } else if (!memcmp(type, "tRNS", 4) && clen <= sizeof trns) {
            memcpy(trns, d, clen); ntrns = clen;
        } else if (!memcmp(type, "IDAT", 4)) {
            memcpy(idat + idatn, d, clen); idatn += clen;
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        pos += 12 + clen;
    }
    (void)ntrns;
    ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!ch || !w || !h || w > 8192 || h > 8192 || !idatn || (ctype == 3 && !nplte)) goto done;
    stride = w * ch;
    raw = (uint8_t *)malloc((size_t)(stride + 1) * h);
    pix = (uint8_t *)malloc((size_t)stride * h);
    rgba = (uint8_t *)malloc((size_t)w * h * 4);
    if (!raw || !pix || !rgba) goto done;
    memset(&z, 0, sizeof z);
    if (inflateInit(&z) != Z_OK) goto done;
    z.next_in = idat; z.avail_in = idatn;
    z.next_out = raw; z.avail_out = (uInt)((size_t)(stride + 1) * h);
    if (inflate(&z, Z_FINISH) != Z_STREAM_END || z.avail_out != 0) { inflateEnd(&z); goto done; }
    inflateEnd(&z);
    for (uint32_t y = 0; y < h; y++) {   /* unfilter */
        const uint8_t *src = raw + (size_t)y * (stride + 1) + 1;
        uint8_t *dst = pix + (size_t)y * stride, *up = y ? dst - stride : NULL;
        const int ft = raw[(size_t)y * (stride + 1)];
        for (uint32_t x = 0; x < stride; x++) {
            const int a = x >= ch ? dst[x - ch] : 0, b = up ? up[x] : 0, c = (up && x >= ch) ? up[x - ch] : 0;
            int v = src[x];
            switch (ft) {
            case 0: break;
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: { const int p = a + b - c, pa = iabs(p - a), pb = iabs(p - b), pc = iabs(p - c);
                      v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); break; }
            default: goto done;
            }
            dst[x] = (uint8_t)v;
        }
    }
    for (size_t i = 0; i < (size_t)w * h; i++) {
        const uint8_t *s = pix + i * ch;
        uint8_t *o = rgba + i * 4;
        switch (ctype) {
        case 0: o[0] = o[1] = o[2] = s[0]; o[3] = 255; break;
        case 4: o[0] = o[1] = o[2] = s[0]; o[3] = s[1]; break;
        case 2: o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = 255; break;
        case 6: memcpy(o, s, 4); break;
        case 3:
            if (s[0] >= nplte) goto done;
            o[0] = plte[s[0] * 3]; o[1] = plte[s[0] * 3 + 1]; o[2] = plte[s[0] * 3 + 2]; o[3] = trns[s[0]];
            break;
        }
    }
    *outW = (int)w; *outH = (int)h; *outRgba = rgba; rgba = NULL;
    ok = 1;
done:
    free(idat); free(raw); free(pix); free(rgba);
    return ok;
}
