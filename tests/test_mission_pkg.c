/* The .pmod package layer (port/assets/ge_pkg.c): pkgWriteDir tars a folder (ustar), pkgReadFile / pkgExists read
 * a path inside X.pmod (a tar file) or an unpacked X.pmod/ folder or a plain file the same way. */
#include "ge_test.h"
#include "mission_dir.h"
#include "ge_pkg.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define RMDIR(p) rmdir(p)
#endif

#define MAXF 160
static char g_files[MAXF][700];   /* every file made (for cleanup) */
static char g_dirs[MAXF][700];
static int g_nf, g_nd;

static void mk(const char *dir)
{
    missionMakeDir(dir);
    snprintf(g_dirs[g_nd++], sizeof g_dirs[0], "%s", dir);
}

static void mkp(const char *base, const char *rel)   /* base/rel's folders, one level at a time */
{
    char p[700];
    size_t n = strlen(base);
    memcpy(p, base, n);
    p[n++] = '/';
    for (const char *s = rel; *s; s++) {
        if (*s == '/') { p[n] = 0; mk(p); }
        p[n++] = *s;
    }
}

/* the folders of base/rel, recorded for cleanup (already made by pkgExtract) */
static void mkpRecord(const char *base, const char *rel)
{
    char p[700];
    int seen;
    size_t n = strlen(base);
    memcpy(p, base, n);
    p[n] = 0;
    seen = 0;
    for (int i = 0; i < g_nd; i++) if (!strcmp(g_dirs[i], p)) seen = 1;
    if (!seen) snprintf(g_dirs[g_nd++], sizeof g_dirs[0], "%s", p);
    p[n++] = '/';
    for (const char *s = rel; *s; s++) {
        if (*s == '/') {
            p[n] = 0;
            seen = 0;
            for (int i = 0; i < g_nd; i++) if (!strcmp(g_dirs[i], p)) seen = 1;
            if (!seen) snprintf(g_dirs[g_nd++], sizeof g_dirs[0], "%s", p);
        }
        p[n++] = *s;
    }
}

static int sameBytes(const char *a, const char *b)
{
    uint8_t *x = NULL, *y = NULL;
    size_t nx = 0, ny = 0;
    int same;
    FILE *f;
    /* read both as plain files (a path ending in .pmod is a file here, not an entry) */
    f = fopen(a, "rb"); if (!f) return 0; fseek(f, 0, SEEK_END); nx = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    x = (uint8_t *)malloc(nx + 1); nx = fread(x, 1, nx, f); fclose(f);
    f = fopen(b, "rb"); if (!f) { free(x); return 0; } fseek(f, 0, SEEK_END); ny = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    y = (uint8_t *)malloc(ny + 1); ny = fread(y, 1, ny, f); fclose(f);
    same = nx == ny && memcmp(x, y, nx) == 0;
    free(x); free(y);
    return same;
}

static size_t gen(uint8_t *b, size_t n, unsigned seed)
{
    for (size_t i = 0; i < n; i++) { seed = seed * 1664525u + 1013904223u; b[i] = (uint8_t)(seed >> 24); }
    return n;
}

typedef struct F { const char *rel; size_t len; unsigned seed; int skipped; } F;

static const F kFiles[] = {
    { "campaign.json", 300, 1, 0 },
    { "empty.bin", 0, 2, 0 },
    { "m1/mission.json", 1000, 3, 0 },
    { "m1/thumb.png", 513, 4, 0 },           /* 513: one byte over a block */
    { "m1/scripts/a.luau", 512, 5, 0 },      /* exactly a block */
    { "m1/scripts/b.luau", 1, 6, 0 },
    { "cover.png", 70000, 7, 0 },
    { "aaaaaaaaaaaa/bbbbbbbbbbbb/cccccccccccc/dddddddddddd/eeeeeeeeeeee/ffffffffffff/gggggggggggg/hhhhhhhhhhhh/iiiiiiiiiiii/file.txt", 77, 8, 0 },   /* > 100 chars: the ustar prefix */
    { ".hidden", 5, 9, 1 },
    { ".gitignore", 5, 10, 1 },
    { ".git/config", 5, 11, 1 },
    { "dist/old.pmod", 5, 12, 1 },
};
#define NF ((int)(sizeof kFiles / sizeof *kFiles))

static int writeTree(const char *base)
{
    uint8_t *buf = (uint8_t *)malloc(70001);
    int ok = 1;
    mk(base);
    for (int i = 0; i < NF; i++) {
        char p[700];
        FILE *f;
        snprintf(p, sizeof p, "%s/%s", base, kFiles[i].rel);
        mkp(base, kFiles[i].rel);
        f = fopen(p, "wb");
        if (!f) { printf("  cannot create %s\n", p); ok = 0; continue; }
        gen(buf, kFiles[i].len, kFiles[i].seed);
        fwrite(buf, 1, kFiles[i].len, f);
        fclose(f);
        snprintf(g_files[g_nf++], sizeof g_files[0], "%s", p);
    }
    free(buf);
    return ok;
}

/* every non-skipped file under `base` reads back as generated; in a package (packed) every skipped one is absent */
static void checkTree(const char *base, int packed)
{
    uint8_t *want = (uint8_t *)malloc(70001);
    for (int i = 0; i < NF; i++) {
        char p[700];
        uint8_t *got = NULL;
        size_t n = 0;
        snprintf(p, sizeof p, "%s/%s", base, kFiles[i].rel);
        if (kFiles[i].skipped) {
            if (packed) GE_CHECK_EQ_INT(pkgExists(p), 0);
            continue;
        }
        GE_CHECK_EQ_INT(pkgExists(p), 1);
        GE_CHECK_EQ_INT(pkgReadFile(p, &got, &n), 1);
        if (!got) continue;
        GE_CHECK_EQ_INT(n, kFiles[i].len);
        gen(want, kFiles[i].len, kFiles[i].seed);
        GE_CHECK(memcmp(got, want, kFiles[i].len) == 0);
        GE_CHECK_EQ_INT(got[n], 0);   /* the extra terminator */
        pkgFree(got);
    }
    free(want);
}

int main(void)
{
    char tmp[500], src[600], pkg[600], dirform[600], p[800], err[300] = "";
    const char *t = getenv("TEMP");
    uint8_t *got = NULL;
    size_t n = 0;
    uint64_t bytes = 0;

    GE_TEST_BEGIN("mission_pkg");
    if (!t || !*t) t = getenv("TMPDIR");
    if (!t || !*t) t = "/tmp";
    snprintf(tmp, sizeof tmp, "%s/ge_pmod_pkg_test", t);
    snprintf(src, sizeof src, "%s/src", tmp);
    snprintf(pkg, sizeof pkg, "%s/x.pmod", tmp);
    snprintf(dirform, sizeof dirform, "%s/y.pmod", tmp);
    mk(tmp);
    GE_CHECK_EQ_INT(writeTree(src), 1);
    GE_CHECK_EQ_INT(writeTree(dirform), 1);   /* the unpacked form */

    GE_CHECK_EQ_INT(pkgWriteDirSized(src, pkg, &bytes, err, sizeof err), 1);
    if (err[0]) printf("  pkgWriteDir: %s\n", err);
    GE_CHECK(bytes > 0 && bytes % 512 == 0);
    snprintf(p, sizeof p, "%s.tmp", pkg);
    GE_CHECK_EQ_INT(pkgExists(p), 0);         /* the temp file is renamed away */

    checkTree(pkg, 1);        /* inside the tar */
    checkTree(dirform, 0);    /* an unpacked X.pmod/ folder */
    checkTree(src, 0);        /* a plain path */

    /* missing entries fail, in the tar and out of it */
    snprintf(p, sizeof p, "%s/nope.json", pkg);
    GE_CHECK_EQ_INT(pkgExists(p), 0);
    GE_CHECK_EQ_INT(pkgReadFile(p, &got, &n), 0);
    GE_CHECK(got == NULL);
    snprintf(p, sizeof p, "%s/m1", pkg);       /* a folder is not a file */
    GE_CHECK_EQ_INT(pkgExists(p), 0);
    snprintf(p, sizeof p, "%s/nope.json", src);
    GE_CHECK_EQ_INT(pkgReadFile(p, &got, &n), 0);
    snprintf(p, sizeof p, "%s/nope.pmod/campaign.json", tmp);
    GE_CHECK_EQ_INT(pkgExists(p), 0);

    /* both separators, and a "./" lead, name the same entry */
    snprintf(p, sizeof p, "%s\\m1\\scripts\\b.luau", pkg);
    GE_CHECK_EQ_INT(pkgReadFile(p, &got, &n), 1);
    GE_CHECK_EQ_INT(n, 1);
    pkgFree(got);

    /* the tar is a real ustar: block 1 has the magic, names are relative with '/' */
    {
        FILE *f = fopen(pkg, "rb");
        unsigned char h[512];
        GE_CHECK(f != NULL);
        if (f) {
            GE_CHECK_EQ_INT(fread(h, 1, 512, f), 512);
            GE_CHECK(memcmp(h + 257, "ustar", 5) == 0);
            GE_CHECK_EQ_INT(h[156], '0');                                  /* a regular file */
            GE_CHECK(memcmp(h + 345, "aaaaaaaaaaaa/bbbbbbbbbbbb", 25) == 0);   /* the long path went to the prefix field (sorted first) */
            fclose(f);
        }
    }

    /* rewriting the package replaces it, and the reader sees the new content (the cache is keyed by size and mtime) */
    {
        FILE *f;
        snprintf(p, sizeof p, "%s/campaign.json", src);
        f = fopen(p, "wb");
        if (f) { fputs("{\"changed\": true, \"padding\": \"............................................................\"}\n", f); fclose(f); }
        GE_CHECK_EQ_INT(pkgWriteDir(src, pkg, err, sizeof err), 1);
        snprintf(p, sizeof p, "%s/campaign.json", pkg);
        GE_CHECK_EQ_INT(pkgReadFile(p, &got, &n), 1);
        GE_CHECK(got && strstr((const char *)got, "changed") != NULL);
        pkgFree(got);
        snprintf(p, sizeof p, "%s/m1/mission.json", pkg);
        GE_CHECK_EQ_INT(pkgReadFile(p, &got, &n), 1);
        GE_CHECK_EQ_INT(n, 1000);
        pkgFree(got);
    }

    /* pkgExtract: the package unpacks into a new folder, every packed file byte for byte (the skipped ones absent) */
    {
        char ex[650], f2[700];
        snprintf(ex, sizeof ex, "%s/ex.pmod", tmp);
        err[0] = 0;
        GE_CHECK_EQ_INT(pkgExtract(pkg, ex, err, sizeof err), 1);
        if (err[0]) printf("  pkgExtract: %s\n", err);
        snprintf(p, sizeof p, "%s/campaign.json", ex);
        GE_CHECK_EQ_INT(pkgReadFile(p, &got, &n), 1);
        GE_CHECK(got && strstr((const char *)got, "changed") != NULL);
        pkgFree(got);
        for (int i = 0; i < NF; i++) {
            snprintf(f2, sizeof f2, "%s/%s", ex, kFiles[i].rel);
            if (strcmp(kFiles[i].rel, "campaign.json")) {
                uint8_t *want = (uint8_t *)malloc(kFiles[i].len + 1);
                got = NULL;
                if (kFiles[i].skipped) GE_CHECK_EQ_INT(pkgExists(f2), 0);
                else GE_CHECK_EQ_INT(pkgReadFile(f2, &got, &n), 1);
                if (got) {
                    GE_CHECK_EQ_INT(n, kFiles[i].len);
                    gen(want, kFiles[i].len, kFiles[i].seed);
                    GE_CHECK(memcmp(got, want, kFiles[i].len) == 0);
                    pkgFree(got);
                }
                free(want);
            }
            if (!kFiles[i].skipped) { snprintf(g_files[g_nf++], sizeof g_files[0], "%s", f2); mkpRecord(ex, kFiles[i].rel); }
        }
        /* repacking the unpacked folder gives the same bytes as the package */
        snprintf(f2, sizeof f2, "%s/re.pmod", tmp);
        GE_CHECK_EQ_INT(pkgWriteDir(ex, f2, err, sizeof err), 1);
        GE_CHECK_EQ_INT(sameBytes(pkg, f2), 1);
        remove(f2);
        /* an existing destination is refused */
        err[0] = 0;
        GE_CHECK_EQ_INT(pkgExtract(pkg, ex, err, sizeof err), 0);
        GE_CHECK(err[0] != 0);
        /* a file that is not a tar is refused, nothing made */
        snprintf(f2, sizeof f2, "%s/ex2.pmod", tmp);
        snprintf(p, sizeof p, "%s/m1/mission.json", src);
        GE_CHECK_EQ_INT(pkgExtract(p, f2, err, sizeof err), 0);
        GE_CHECK_EQ_INT(pkgExists(f2), 0);
        /* an entry with ".." is refused before anything is written */
        {
            char evil[650];
            FILE *f;
            unsigned char h[512];
            unsigned sum = 0;
            snprintf(evil, sizeof evil, "%s/evil.pmod", tmp);
            memset(h, 0, sizeof h);
            memcpy(h, "../escape.txt", 13);
            memcpy(h + 100, "0000644", 7);
            memcpy(h + 124, "00000000001", 11);
            memset(h + 148, ' ', 8);
            h[156] = '0';
            memcpy(h + 257, "ustar", 6);
            memcpy(h + 263, "00", 2);
            for (int i = 0; i < 512; i++) sum += h[i];
            snprintf((char *)h + 148, 8, "%06o", sum);
            f = fopen(evil, "wb");
            if (f) {
                unsigned char z[1536];
                memset(z, 0, sizeof z);
                z[0] = 'x';
                fwrite(h, 1, 512, f);
                fwrite(z, 1, sizeof z, f);
                fclose(f);
            }
            err[0] = 0;
            GE_CHECK_EQ_INT(pkgExtract(evil, f2, err, sizeof err), 0);
            GE_CHECK(strstr(err, "outside") != NULL);
            GE_CHECK_EQ_INT(pkgExists(f2), 0);
            snprintf(p, sizeof p, "%s/escape.txt", tmp);
            GE_CHECK_EQ_INT(pkgExists(p), 0);
            remove(evil);
        }
    }

    /* a source folder that is not there: refused with a message, nothing written */
    snprintf(p, sizeof p, "%s/no_such_folder", tmp);
    snprintf(src, sizeof src, "%s/z.pmod", tmp);
    err[0] = 0;
    GE_CHECK_EQ_INT(pkgWriteDir(p, src, err, sizeof err), 0);
    GE_CHECK(err[0] != 0);
    GE_CHECK_EQ_INT(pkgExists(src), 0);

    /* cleanup: files, then the folders in reverse order of creation */
    remove(pkg);
    for (int i = 0; i < g_nf; i++) remove(g_files[i]);
    for (int i = g_nd - 1; i >= 0; i--) RMDIR(g_dirs[i]);

    return GE_TEST_END();
}
