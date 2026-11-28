/* Asset packs (.passets): see ge_assetstack.h. */
#include "ge_assetstack.h"
#include "ge_pkg.h"
#include "cJSON.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *const kSuffix[] = { ".passets", ".ptexpack" };

/* not <ctype.h>: the game links its own isspace (src/str.c), and MinGW's header would define a second one */
static int isSpace(char c) { return c == ' ' || c == 9 || c == 13 || c == 10; }

static int suffixOf(const char *name)
{
    const size_t len = name ? strlen(name) : 0;
    for (int i = 0; i < 2; i++) {
        const size_t sl = strlen(kSuffix[i]);
        if (len > sl && !strcmp(name + len - sl, kSuffix[i])) return i;
    }
    return -1;
}

int assetPackIsFileName(const char *name)
{
    return name && name[0] && name[0] != '.' && strlen(name) < ASSETPACK_FILE_LEN && !strpbrk(name, "/\\;") && suffixOf(name) >= 0;
}

static void copyStr(char *dst, size_t n, const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsString(v) && v->valuestring) snprintf(dst, n, "%s", v->valuestring);
}

static int cmpInfo(const void *a, const void *b)
{
    return strcmp(((const AssetPackInfo *)a)->file, ((const AssetPackInfo *)b)->file);
}

int assetPackScan(const char *const *dirs, int ndirs, AssetPackInfo *out, int max)
{
    int n = 0, kept = 0;
    char seen[256][ASSETPACK_FILE_LEN];
    int nseen = 0;
    for (int k = 0; k < ndirs; k++) {
        const char *dir = dirs[k];
        DIR *d = dir && dir[0] ? opendir(dir) : NULL;
        struct dirent *e;
        if (!d) continue;
        while ((e = readdir(d)) != NULL) {
            char full[1200];
            struct stat st;
            int dup = 0;
            if (!assetPackIsFileName(e->d_name)) continue;
            snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
            if (stat(full, &st) != 0) continue;
            if ((st.st_mode & S_IFMT) != S_IFREG && (st.st_mode & S_IFMT) != S_IFDIR) continue;
            for (int i = 0; i < nseen; i++) if (!strcmp(seen[i], e->d_name)) { dup = 1; break; }
            if (dup) continue;
            if (nseen < 256) snprintf(seen[nseen++], ASSETPACK_FILE_LEN, "%s", e->d_name);
            n++;
            if (out && kept < max) {
                AssetPackInfo *p = &out[kept++];
                const int sfx = suffixOf(e->d_name);
                memset(p, 0, sizeof *p);
                snprintf(p->file, sizeof p->file, "%s", e->d_name);
                snprintf(p->dir, sizeof p->dir, "%s", dir);
                snprintf(p->name, sizeof p->name, "%.*s", (int)(strlen(e->d_name) - strlen(kSuffix[sfx])), e->d_name);
                p->legacy = sfx == 1;
            }
        }
        closedir(d);
    }
    if (out && kept > 1) qsort(out, (size_t)kept, sizeof *out, cmpInfo);
    for (int i = 0; out && i < kept; i++) {   /* pack.json, when there is one */
        char path[1400];
        uint8_t *buf = NULL;
        size_t blen = 0;
        snprintf(path, sizeof path, "%s/%s/pack.json", out[i].dir, out[i].file);
        if (!pkgReadFile(path, &buf, &blen)) continue;
        cJSON *j = cJSON_Parse((const char *)buf);
        if (j) {
            const cJSON *b = cJSON_GetObjectItemCaseSensitive(j, "base");
            copyStr(out[i].name, sizeof out[i].name, j, "name");
            copyStr(out[i].author, sizeof out[i].author, j, "author");
            copyStr(out[i].description, sizeof out[i].description, j, "description");
            out[i].base = cJSON_IsTrue(b);
            cJSON_Delete(j);
        }
        pkgFree(buf);
    }
    return n;
}

int assetPackListFind(char (*files)[ASSETPACK_FILE_LEN], int n, const char *file)
{
    for (int i = 0; i < n; i++) if (!strcmp(files[i], file)) return i;
    return -1;
}

int assetPackListParse(const char *list, char (*files)[ASSETPACK_FILE_LEN], int max)
{
    int n = 0;
    const char *p = list ? list : "";
    while (*p && n < max) {
        const char *end = strchr(p, ';');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        const char *s = p;
        char name[ASSETPACK_FILE_LEN];
        while (len && isSpace(*s)) { s++; len--; }
        while (len && isSpace(s[len - 1])) len--;
        if (len && len < ASSETPACK_FILE_LEN) {
            memcpy(name, s, len);
            name[len] = 0;
            if (assetPackIsFileName(name) && assetPackListFind(files, n, name) < 0) snprintf(files[n++], ASSETPACK_FILE_LEN, "%s", name);
        }
        if (!end) break;
        p = end + 1;
    }
    return n;
}

void assetPackListFormat(char (*files)[ASSETPACK_FILE_LEN], int n, char *out, size_t outlen)
{
    size_t used = 0;
    if (!out || !outlen) return;
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        const int w = snprintf(out + used, outlen - used, "%s%s", i ? ";" : "", files[i]);
        if (w < 0 || (size_t)w >= outlen - used) { out[used] = 0; return; }   /* never a cut name */
        used += (size_t)w;
    }
}

int assetPackListMove(char (*files)[ASSETPACK_FILE_LEN], int n, int from, int to)
{
    char tmp[ASSETPACK_FILE_LEN];
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return 0;
    memcpy(tmp, files[from], sizeof tmp);
    if (from < to) memmove(files[from], files[from + 1], (size_t)(to - from) * ASSETPACK_FILE_LEN);
    else memmove(files[to + 1], files[to], (size_t)(from - to) * ASSETPACK_FILE_LEN);
    memcpy(files[to], tmp, sizeof tmp);
    return 1;
}

struct AssetStack {
    int n;
    char root[ASSETPACK_MAX][1024];
    TexPackIndex *tex[ASSETPACK_MAX];
};

AssetStack *assetStackOpen(const char *const *packRoots, int n)
{
    AssetStack *s = (AssetStack *)calloc(1, sizeof *s);
    if (!s) return NULL;
    if (n > ASSETPACK_MAX) n = ASSETPACK_MAX;
    for (int i = 0; i < n; i++) {
        snprintf(s->root[i], sizeof s->root[i], "%s", packRoots[i] ? packRoots[i] : "");
        s->tex[i] = texpackIndexOpen(s->root[i]);
    }
    s->n = n < 0 ? 0 : n;
    return s;
}

void assetStackClose(AssetStack *s)
{
    if (!s) return;
    for (int i = 0; i < s->n; i++) texpackIndexClose(s->tex[i]);
    free(s);
}

int assetStackCount(const AssetStack *s) { return s ? s->n : 0; }
int assetStackOpened(const AssetStack *s, int i) { return s && i >= 0 && i < s->n && s->tex[i] != NULL; }
const char *assetStackRoot(const AssetStack *s, int i) { return s && i >= 0 && i < s->n ? s->root[i] : ""; }
int assetStackTextureCount(const AssetStack *s, int i) { return s && i >= 0 && i < s->n ? texpackIndexCount(s->tex[i]) : 0; }

int assetStackTextureFrom(const AssetStack *s, const TexPackIndex *mod, int texnum)
{
    if (texpackIndexHas(mod, texnum)) return ASSET_FROM_MOD;
    for (int i = 0; s && i < s->n; i++) if (texpackIndexHas(s->tex[i], texnum)) return i;
    return ASSET_FROM_NONE;
}

int assetStackReadTexture(const AssetStack *s, const TexPackIndex *mod, int texnum, uint8_t **out, size_t *len)
{
    const int from = assetStackTextureFrom(s, mod, texnum);
    if (from == ASSET_FROM_NONE) return 0;
    return texpackIndexRead(from == ASSET_FROM_MOD ? mod : s->tex[from], texnum, out, len);
}
