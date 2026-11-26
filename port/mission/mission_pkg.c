/* Mod packages (.pmod = ustar tar): see mission_pkg.h. */
#include "mission_pkg.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define PKG_MKDIR(p) _mkdir(p)
#else
#define PKG_MKDIR(p) mkdir(p, 0755)
#endif

#define BLK 512
#define CACHE_N 4

static int isRegFile(const char *path, long long *size, long long *mtime)
{
    struct stat st;
    if (stat(path, &st) != 0 || (st.st_mode & S_IFMT) != S_IFREG) return 0;
    if (size) *size = (long long)st.st_size;
    if (mtime) *mtime = (long long)st.st_mtime;
    return 1;
}

static int isSep(char c) { return c == '/' || c == '\\'; }

/* ---- the tar index ---------------------------------------------------------------------------------------------- */

typedef struct PkgEntry {
    char *name;
    long long off, size;
} PkgEntry;

typedef struct PkgIndex {
    char path[1024];
    long long fsize, mtime;
    PkgEntry *e;
    int n, cap;
    unsigned stamp;
} PkgIndex;

static PkgIndex s_cache[CACHE_N];
static unsigned s_tick = 0;

static void indexClear(PkgIndex *ix)
{
    for (int i = 0; i < ix->n; i++) free(ix->e[i].name);
    free(ix->e);
    memset(ix, 0, sizeof *ix);
}

static long long octal(const unsigned char *p, int n)
{
    long long v = 0;
    if (p[0] & 0x80) {   /* GNU base-256 */
        v = p[0] & 0x7f;
        for (int i = 1; i < n; i++) v = (v << 8) | p[i];
        return v;
    }
    int i = 0;
    while (i < n && (p[i] == ' ' || p[i] == 0)) i++;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++) v = v * 8 + (p[i] - '0');
    return v;
}

static int zeroBlock(const unsigned char *b)
{
    for (int i = 0; i < BLK; i++) if (b[i]) return 0;
    return 1;
}

static int checksumOk(const unsigned char *b)
{
    long long sum = 0;
    for (int i = 0; i < BLK; i++) sum += (i >= 148 && i < 156) ? ' ' : b[i];
    return sum == octal(b + 148, 8);
}

/* "<len> key=value\n" records of a pax header: the path and size overrides. */
static void paxParse(char *buf, size_t n, char *path, size_t pathn, long long *size)
{
    size_t pos = 0;
    while (pos < n) {
        size_t len = 0, i = pos;
        while (i < n && buf[i] >= '0' && buf[i] <= '9') len = len * 10 + (size_t)(buf[i++] - '0');
        if (!len || pos + len > n || i >= n || buf[i] != ' ') return;
        char *rec = buf + i + 1, *end = buf + pos + len;
        if (end > rec && end[-1] == '\n') end[-1] = 0;
        else if (end < buf + n) *end = 0;
        if (!strncmp(rec, "path=", 5)) snprintf(path, pathn, "%s", rec + 5);
        else if (!strncmp(rec, "size=", 5)) *size = atoll(rec + 5);
        pos += len;
    }
}

static int indexAdd(PkgIndex *ix, const char *name, long long off, long long size)
{
    while (name[0] == '.' && isSep(name[1])) name += 2;
    while (isSep(name[0])) name++;
    if (!*name) return 1;
    if (ix->n == ix->cap) {
        int cap = ix->cap ? ix->cap * 2 : 64;
        PkgEntry *ne = (PkgEntry *)realloc(ix->e, (size_t)cap * sizeof *ne);
        if (!ne) return 0;
        ix->e = ne;
        ix->cap = cap;
    }
    size_t len = strlen(name);
    char *d = (char *)malloc(len + 1);
    if (!d) return 0;
    memcpy(d, name, len + 1);
    for (char *p = d; *p; p++) if (*p == '\\') *p = '/';
    ix->e[ix->n].name = d;
    ix->e[ix->n].off = off;
    ix->e[ix->n].size = size;
    ix->n++;
    return 1;
}

static int indexBuild(PkgIndex *ix, FILE *f, long long fsize)
{
    unsigned char h[BLK];
    char longName[4096] = "";
    long long pos = 0, paxSize = -1;
    int haveLong = 0;
    while (pos + BLK <= fsize) {
        long long size, adv;
        int type;
        char name[4096];
        if (fseek(f, (long)pos, SEEK_SET) != 0 || fread(h, 1, BLK, f) != BLK) return 0;
        if (zeroBlock(h)) break;
        if (!checksumOk(h)) return 0;
        size = octal(h + 124, 12);
        type = h[156];
        adv = (size + BLK - 1) / BLK * BLK;
        pos += BLK;
        if (size < 0 || pos + size > fsize) return 0;
        if (type == 'x' || type == 'L') {   /* pax extended header / GNU long name: applies to the next entry */
            if (size > 65536) return 0;
            char *buf = (char *)malloc((size_t)size + 1);
            if (!buf) return 0;
            if (fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); return 0; }
            buf[size] = 0;
            if (type == 'L') { snprintf(longName, sizeof longName, "%s", buf); haveLong = 1; }
            else { paxParse(buf, (size_t)size, longName, sizeof longName, &paxSize); if (longName[0]) haveLong = 1; }
            free(buf);
            pos += adv;
            continue;
        }
        if (type == 'g' || type == 'K') { pos += adv; continue; }
        if (haveLong) snprintf(name, sizeof name, "%s", longName);
        else {
            char nm[101], pre[156];
            memcpy(nm, h, 100); nm[100] = 0;
            memcpy(pre, h + 345, 155); pre[155] = 0;
            if (!memcmp(h + 257, "ustar", 6) && pre[0])   /* POSIX "ustar\0": the GNU "ustar  " format has other fields there */ snprintf(name, sizeof name, "%s/%s", pre, nm);
            else snprintf(name, sizeof name, "%s", nm);
        }
        if (paxSize >= 0) { size = paxSize; adv = (size + BLK - 1) / BLK * BLK; if (pos + size > fsize) return 0; }
        if (type == '0' || type == 0 || type == '7') {
            if (!indexAdd(ix, name, pos, size)) return 0;
        }
        haveLong = 0; longName[0] = 0; paxSize = -1;
        pos += adv;
    }
    return 1;
}

/* The index of package file `pkg`, from the cache when its size and mtime still match; NULL if it is not a tar. */
static PkgIndex *indexGet(const char *pkg)
{
    long long fsize, mtime;
    PkgIndex *slot = NULL;
    FILE *f;
    if (strlen(pkg) >= sizeof s_cache[0].path || !isRegFile(pkg, &fsize, &mtime)) return NULL;
    for (int i = 0; i < CACHE_N; i++) {
        if (s_cache[i].path[0] && !strcmp(s_cache[i].path, pkg)) {
            if (s_cache[i].fsize == fsize && s_cache[i].mtime == mtime) { s_cache[i].stamp = ++s_tick; return &s_cache[i]; }
            indexClear(&s_cache[i]);
        }
    }
    for (int i = 0; i < CACHE_N; i++) {
        if (!s_cache[i].path[0]) { slot = &s_cache[i]; break; }
        if (!slot || s_cache[i].stamp < slot->stamp) slot = &s_cache[i];
    }
    indexClear(slot);
    f = fopen(pkg, "rb");
    if (!f) return NULL;
    snprintf(slot->path, sizeof slot->path, "%s", pkg);
    slot->fsize = fsize;
    slot->mtime = mtime;
    if (!indexBuild(slot, f, fsize)) { fclose(f); indexClear(slot); return NULL; }
    fclose(f);
    slot->stamp = ++s_tick;
    return slot;
}

/* Does `path` go through a regular file named *.pmod? Then pkg = that file and rest = the entry name inside it. */
static int splitPath(const char *path, char *pkg, size_t pkgn, char *rest, size_t restn)
{
    const char *p = path;
    while (*p) {
        const char *q = p;
        while (*q && !isSep(*q)) q++;
        if (q - p > 5 && !strncmp(q - 5, ".pmod", 5)) {
            size_t plen = (size_t)(q - path);
            const char *r = q;
            while (isSep(*r)) r++;
            if (*r && plen < pkgn) {
                memcpy(pkg, path, plen);
                pkg[plen] = 0;
                if (isRegFile(pkg, NULL, NULL)) {
                    size_t k = 0;
                    while (r[0] == '.' && isSep(r[1])) r += 2;
                    for (; *r && k + 1 < restn; r++) rest[k++] = *r == '\\' ? '/' : *r;
                    rest[k] = 0;
                    return 1;
                }
            }
        }
        p = *q ? q + 1 : q;
    }
    return 0;
}

int pkgReadFile(const char *path, uint8_t **out, size_t *len)
{
    char pkg[1024], rest[1024];
    FILE *f;
    long long off, size;
    uint8_t *buf;
    *out = NULL;
    if (len) *len = 0;
    if (splitPath(path, pkg, sizeof pkg, rest, sizeof rest)) {
        PkgIndex *ix = indexGet(pkg);
        int i;
        if (!ix) return 0;
        for (i = 0; i < ix->n; i++) if (!strcmp(ix->e[i].name, rest)) break;
        if (i == ix->n) return 0;
        off = ix->e[i].off;
        size = ix->e[i].size;
        f = fopen(pkg, "rb");
        if (!f) return 0;
        if (fseek(f, (long)off, SEEK_SET) != 0) { fclose(f); return 0; }
    } else {
        if (!isRegFile(path, &size, NULL)) return 0;
        f = fopen(path, "rb");
        if (!f) return 0;
    }
    buf = (uint8_t *)malloc((size_t)size + 1);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return 0; }
    fclose(f);
    buf[size] = 0;
    *out = buf;
    if (len) *len = (size_t)size;
    return 1;
}

int pkgExists(const char *path)
{
    char pkg[1024], rest[1024];
    if (splitPath(path, pkg, sizeof pkg, rest, sizeof rest)) {
        PkgIndex *ix = indexGet(pkg);
        if (!ix) return 0;
        for (int i = 0; i < ix->n; i++) if (!strcmp(ix->e[i].name, rest)) return 1;
        return 0;
    }
    return isRegFile(path, NULL, NULL);
}

void pkgFree(void *p) { free(p); }

/* ---- the writer ------------------------------------------------------------------------------------------------- */

typedef struct NameList {
    char **v;
    int n, cap;
} NameList;

static int cmpStr(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int listAdd(NameList *l, const char *s)
{
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 64;
        char **nv = (char **)realloc(l->v, (size_t)cap * sizeof *nv);
        if (!nv) return 0;
        l->v = nv;
        l->cap = cap;
    }
    size_t len = strlen(s);
    char *d = (char *)malloc(len + 1);
    if (!d) return 0;
    memcpy(d, s, len + 1);
    l->v[l->n++] = d;
    return 1;
}

static void listFree(NameList *l)
{
    for (int i = 0; i < l->n; i++) free(l->v[i]);
    free(l->v);
    memset(l, 0, sizeof *l);
}

/* Collect the regular files below root/rel (rel "" = the root) as relative '/' paths. */
static int collect(const char *root, const char *rel, NameList *out, char *err, size_t errlen)
{
    char dir[2048];
    DIR *d;
    struct dirent *e;
    NameList sub = {0};
    int ok = 1;
    snprintf(dir, sizeof dir, "%s%s%s", root, rel[0] ? "/" : "", rel);
    d = opendir(dir);
    if (!d) { snprintf(err, errlen, "cannot read folder %s", dir); return 0; }
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;                 /* .git, .gitignore, ., .. */
        if (!rel[0] && !strcmp(e->d_name, "dist")) continue;   /* the publish output folder */
        if (!listAdd(&sub, e->d_name)) { ok = 0; break; }
    }
    closedir(d);
    if (!ok) { snprintf(err, errlen, "out of memory"); listFree(&sub); return 0; }
    qsort(sub.v, (size_t)sub.n, sizeof *sub.v, cmpStr);
    for (int i = 0; i < sub.n && ok; i++) {
        char relp[2048], full[2200];
        struct stat st;
        snprintf(relp, sizeof relp, "%s%s%s", rel, rel[0] ? "/" : "", sub.v[i]);
        snprintf(full, sizeof full, "%s/%s", root, relp);
        if (stat(full, &st) != 0) continue;
        if ((st.st_mode & S_IFMT) == S_IFDIR) ok = collect(root, relp, out, err, errlen);
        else if ((st.st_mode & S_IFMT) == S_IFREG) {
            if (!listAdd(out, relp)) { snprintf(err, errlen, "out of memory"); ok = 0; }
        }
    }
    listFree(&sub);
    return ok;
}

static void putOctal(unsigned char *dst, int width, unsigned long long v)
{
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%0*llo", width - 1, v);
    memcpy(dst, tmp, (size_t)width - 1);
    dst[width - 1] = 0;
}

static int writeHeader(FILE *f, const char *name, long long size, char *err, size_t errlen)
{
    unsigned char h[BLK];
    size_t len = strlen(name);
    const char *tail = name;
    char prefix[156] = "";
    unsigned sum = 0;
    memset(h, 0, sizeof h);
    if (len > 100) {   /* split at a '/': prefix up to 155 bytes, name up to 100 */
        size_t i = len - 2 < 155 ? len - 2 : 155;
        int found = 0;
        for (; i > 0; i--) {
            if (name[i] == '/' && len - i - 1 <= 100) { found = 1; break; }
        }
        if (!found) { snprintf(err, errlen, "path too long for a package: %s", name); return 0; }
        memcpy(prefix, name, i);
        prefix[i] = 0;
        tail = name + i + 1;
    }
    memcpy(h, tail, strlen(tail));
    putOctal(h + 100, 8, 0644);
    putOctal(h + 108, 8, 0);
    putOctal(h + 116, 8, 0);
    putOctal(h + 124, 12, (unsigned long long)size);
    putOctal(h + 136, 12, 0);
    memset(h + 148, ' ', 8);
    h[156] = '0';
    memcpy(h + 257, "ustar", 6);   /* magic + NUL */
    memcpy(h + 263, "00", 2);
    memcpy(h + 345, prefix, strlen(prefix));
    for (int i = 0; i < BLK; i++) sum += h[i];
    putOctal(h + 148, 7, sum);
    h[155] = ' ';
    if (fwrite(h, 1, BLK, f) != BLK) { snprintf(err, errlen, "write failed"); return 0; }
    return 1;
}

int pkgWriteDirSized(const char *srcDir, const char *outFile, uint64_t *outBytes, char *err, size_t errlen)
{
    NameList files = {0};
    char tmp[1100] = "", full[2300];
    FILE *o = NULL;
    uint64_t total = 0;
    int ok = 0;
    char lerr[256] = "";
    static const unsigned char zero[BLK];
    if (outBytes) *outBytes = 0;
    if (err && errlen) err[0] = 0;
    if (!collect(srcDir, "", &files, lerr, sizeof lerr)) goto fail;
    if (!files.n) { snprintf(lerr, sizeof lerr, "no files in %s", srcDir); goto fail; }
    qsort(files.v, (size_t)files.n, sizeof *files.v, cmpStr);
    snprintf(tmp, sizeof tmp, "%s.tmp", outFile);
    o = fopen(tmp, "wb");
    if (!o) { snprintf(lerr, sizeof lerr, "cannot write %s", tmp); goto fail; }
    for (int i = 0; i < files.n; i++) {
        struct stat st;
        FILE *in;
        unsigned char buf[8192];
        long long left;
        snprintf(full, sizeof full, "%s/%s", srcDir, files.v[i]);
        if (stat(full, &st) != 0) { snprintf(lerr, sizeof lerr, "cannot read %s", full); goto fail; }
        left = (long long)st.st_size;
        if (!writeHeader(o, files.v[i], left, lerr, sizeof lerr)) goto fail;
        in = fopen(full, "rb");
        if (!in) { snprintf(lerr, sizeof lerr, "cannot read %s", full); goto fail; }
        total += BLK;
        while (left > 0) {
            size_t want = left > (long long)sizeof buf ? sizeof buf : (size_t)left;
            if (fread(buf, 1, want, in) != want || fwrite(buf, 1, want, o) != want) {
                fclose(in);
                snprintf(lerr, sizeof lerr, "read or write failed at %s", full);
                goto fail;
            }
            left -= (long long)want;
            total += want;
        }
        fclose(in);
        {
            size_t pad = (size_t)((BLK - (st.st_size % BLK)) % BLK);
            if (pad && fwrite(zero, 1, pad, o) != pad) { snprintf(lerr, sizeof lerr, "write failed"); goto fail; }
            total += pad;
        }
    }
    if (fwrite(zero, 1, BLK, o) != BLK || fwrite(zero, 1, BLK, o) != BLK) { snprintf(lerr, sizeof lerr, "write failed"); goto fail; }
    total += 2 * BLK;
    if (fclose(o) != 0) { o = NULL; snprintf(lerr, sizeof lerr, "write failed: %s", tmp); goto fail; }
    o = NULL;
    {
        struct stat st;
        if (stat(outFile, &st) == 0) {
            if ((st.st_mode & S_IFMT) == S_IFDIR) { snprintf(lerr, sizeof lerr, "%s is a folder, not a package file", outFile); goto fail; }
            if (remove(outFile) != 0) { snprintf(lerr, sizeof lerr, "cannot replace %s (in use?)", outFile); goto fail; }
        }
    }
    if (rename(tmp, outFile) != 0) { snprintf(lerr, sizeof lerr, "cannot rename %s to %s", tmp, outFile); goto fail; }
    /* drop its cached index: a rewrite of the same size within the same second (mtime has 1 s steps) would keep it */
    for (int i = 0; i < CACHE_N; i++) if (s_cache[i].path[0] && !strcmp(s_cache[i].path, outFile)) indexClear(&s_cache[i]);
    if (outBytes) *outBytes = total;
    ok = 1;
    goto done;
fail:
    if (o) { fclose(o); remove(tmp); }
    else if (files.n && tmp[0]) remove(tmp);
    if (err && errlen) snprintf(err, errlen, "%s", lerr);
done:
    listFree(&files);
    return ok;
}

int pkgWriteDir(const char *srcDir, const char *outFile, char *err, size_t errlen)
{
    return pkgWriteDirSized(srcDir, outFile, NULL, err, errlen);
}

/* ---- the extractor ---------------------------------------------------------------------------------------------- */

/* A tar entry name that stays inside the destination: relative, no drive, no "." or ".." component, no empty one. */
static int safeName(const char *name)
{
    const char *p = name;
    if (!*name || isSep(name[0]) || strchr(name, ':')) return 0;
    while (*p) {
        const char *q = p;
        while (*q && !isSep(*q)) q++;
        if (q == p) return 0;
        if ((q - p == 1 && p[0] == '.') || (q - p == 2 && p[0] == '.' && p[1] == '.')) return 0;
        p = *q ? q + 1 : q;
    }
    return 1;
}

int pkgExtract(const char *pkgFile, const char *destDir, char *err, size_t errlen)
{
    PkgIndex *ix;
    FILE *in = NULL;
    struct stat st;
    char lerr[300] = "";
    int ok = 0;
    if (err && errlen) err[0] = 0;
    if (stat(destDir, &st) == 0) { snprintf(lerr, sizeof lerr, "%s already exists", destDir); goto out; }
    ix = indexGet(pkgFile);
    if (!ix) { snprintf(lerr, sizeof lerr, "%s is not a package (a tar file)", pkgFile); goto out; }
    for (int i = 0; i < ix->n; i++)
        if (!safeName(ix->e[i].name)) { snprintf(lerr, sizeof lerr, "refused: entry \"%s\" would land outside the folder", ix->e[i].name); goto out; }
    if (PKG_MKDIR(destDir) != 0) { snprintf(lerr, sizeof lerr, "cannot create %s", destDir); goto out; }
    in = fopen(pkgFile, "rb");
    if (!in) { snprintf(lerr, sizeof lerr, "cannot read %s", pkgFile); goto out; }
    for (int i = 0; i < ix->n; i++) {
        char full[2200];
        unsigned char buf[8192];
        long long left = ix->e[i].size;
        FILE *o;
        size_t base = strlen(destDir) + 1;
        if (snprintf(full, sizeof full, "%s/%s", destDir, ix->e[i].name) >= (int)sizeof full) { snprintf(lerr, sizeof lerr, "path too long: %s", ix->e[i].name); goto out; }
        for (char *c = full + base; *c; c++) {   /* the entry's folders, one level at a time */
            if (*c != '/') continue;
            *c = 0;
            if (stat(full, &st) != 0 && PKG_MKDIR(full) != 0) { snprintf(lerr, sizeof lerr, "cannot create %s", full); goto out; }
            *c = '/';
        }
        if (fseek(in, (long)ix->e[i].off, SEEK_SET) != 0) { snprintf(lerr, sizeof lerr, "cannot read %s", pkgFile); goto out; }
        o = fopen(full, "wb");
        if (!o) { snprintf(lerr, sizeof lerr, "cannot write %s", full); goto out; }
        while (left > 0) {
            size_t want = left > (long long)sizeof buf ? sizeof buf : (size_t)left;
            if (fread(buf, 1, want, in) != want || fwrite(buf, 1, want, o) != want) {
                fclose(o);
                snprintf(lerr, sizeof lerr, "read or write failed at %s", full);
                goto out;
            }
            left -= (long long)want;
        }
        if (fclose(o) != 0) { snprintf(lerr, sizeof lerr, "write failed: %s", full); goto out; }
    }
    ok = 1;
out:
    if (in) fclose(in);
    if (!ok && err && errlen) snprintf(err, errlen, "%s", lerr);
    return ok;
}
