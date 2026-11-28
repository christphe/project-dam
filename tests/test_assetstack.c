/* Asset packs (port/assets/ge_assetstack.c, docs/specs/asset-packs.md): the scan of the packs folders, the enabled list,
 * and the resolution of a texture through the mod over the player's ordered, partial packs (folders and tars). */
#include "ge_test.h"
#include "mission_dir.h"
#include "ge_pkg.h"
#include "ge_assetstack.h"

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

#define MAXF 200
static char g_files[MAXF][700];
static char g_dirs[MAXF][700];
static int g_nf, g_nd;

static void mkd(const char *dir)
{
    for (int i = 0; i < g_nd; i++) if (!strcmp(g_dirs[i], dir)) return;
    missionMakeDir(dir);
    snprintf(g_dirs[g_nd++], sizeof g_dirs[0], "%s", dir);
}

/* root/rel with `text` as its content (made with its folders) */
static void put(const char *root, const char *rel, const char *text)
{
    char p[700];
    size_t n = strlen(root);
    FILE *f;
    mkd(root);
    memcpy(p, root, n);
    p[n++] = '/';
    for (const char *s = rel; *s; s++) {
        if (*s == '/') { p[n] = 0; mkd(p); }
        p[n++] = *s;
    }
    p[n] = 0;
    f = fopen(p, "wb");
    if (!f) { printf("  cannot create %s\n", p); return; }
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    snprintf(g_files[g_nf++], sizeof g_files[0], "%s", p);
}

/* the content of texture n through the stack, "" when missing */
static const char *readTex(const AssetStack *s, const TexPackIndex *mod, int n)
{
    static char text[64];
    uint8_t *buf = NULL;
    size_t len = 0;
    text[0] = 0;
    if (assetStackReadTexture(s, mod, n, &buf, &len)) {
        snprintf(text, sizeof text, "%.*s", (int)len, (const char *)buf);
        free(buf);
    }
    return text;
}

int main(void)
{
    char tmp[500], packs[600], legacy[600], hd[600], src[600], partial[700], old[700], mod[600], err[300] = "";
    const char *t = getenv("TEMP");
    char files[ASSETPACK_MAX][ASSETPACK_FILE_LEN];
    AssetPackInfo info[8];
    TexPackIndex *m;

    GE_TEST_BEGIN("assetstack");
    if (!t || !*t) t = getenv("TMPDIR");
    if (!t || !*t) t = "/tmp";
    snprintf(tmp, sizeof tmp, "%s/ge_assetstack_test", t);
    snprintf(packs, sizeof packs, "%s/assetpacks", tmp);
    snprintf(legacy, sizeof legacy, "%s/texturepacks", tmp);
    snprintf(hd, sizeof hd, "%s/hd.passets", packs);             /* a folder: textures 1 and 2, with pack.json */
    snprintf(src, sizeof src, "%s/srcfaces", tmp);               /* the source of a tar pack: textures 2 and 3 */
    snprintf(partial, sizeof partial, "%s/faces.passets", packs);
    snprintf(old, sizeof old, "%s/old.ptexpack", legacy);        /* the older name, in the older folder: texture 4 */
    snprintf(mod, sizeof mod, "%s/m.pmod", tmp);                 /* the mod: texture 3 */

    mkd(tmp); mkd(packs); mkd(legacy);
    put(hd, "pack.json", "{\"name\":\"HD\",\"author\":\"someone\",\"description\":\"sharper\",\"base\":false}");
    put(hd, "textures/1.png", "hd1");
    put(hd, "textures/2.png", "hd2");
    put(src, "pack.json", "{\"name\":\"Faces\"}");
    put(src, "textures/2.png", "faces2");
    put(src, "textures/3.png", "faces3");
    mkd(packs);
    GE_CHECK(pkgWriteDir(src, partial, err, sizeof err));
    put(old, "textures/4.png", "old4");
    put(legacy, "hd.passets/textures/1.png", "shadowed");        /* same file name in a later folder: skipped */
    put(packs, "notes.txt", "not a pack");
    put(mod, "textures/3.png", "mod3");

    /* the scan: both folders, both names, sorted, a name seen earlier skipped, pack.json read from folders and tars */
    {
        const char *dirs[2] = { packs, legacy };
        const int n = assetPackScan(dirs, 2, info, 8);
        GE_CHECK_EQ_INT(n, 3);
        GE_CHECK_STR(info[0].file, "faces.passets");
        GE_CHECK_STR(info[0].name, "Faces");
        GE_CHECK(!info[0].legacy);
        GE_CHECK_STR(info[1].file, "hd.passets");
        GE_CHECK_STR(info[1].name, "HD");
        GE_CHECK_STR(info[1].author, "someone");
        GE_CHECK_STR(info[1].dir, packs);
        GE_CHECK_STR(info[2].file, "old.ptexpack");
        GE_CHECK_STR(info[2].name, "old");
        GE_CHECK(info[2].legacy);
        GE_CHECK_EQ_INT(assetPackScan(dirs, 2, NULL, 0), 3);
    }

    /* the enabled list */
    {
        char text[ASSETPACK_LIST_LEN];
        int n = assetPackListParse(" hd.passets ; faces.passets;;hd.passets;bad name.txt;old.ptexpack;a/b.passets", files, ASSETPACK_MAX);
        GE_CHECK_EQ_INT(n, 3);
        GE_CHECK_STR(files[0], "hd.passets");
        GE_CHECK_STR(files[1], "faces.passets");
        GE_CHECK_STR(files[2], "old.ptexpack");
        assetPackListFormat(files, n, text, sizeof text);
        GE_CHECK_STR(text, "hd.passets;faces.passets;old.ptexpack");
        GE_CHECK(assetPackListMove(files, n, 2, 0));
        assetPackListFormat(files, n, text, sizeof text);
        GE_CHECK_STR(text, "old.ptexpack;hd.passets;faces.passets");
        GE_CHECK(!assetPackListMove(files, n, 1, 1) && !assetPackListMove(files, n, 0, 3));
        GE_CHECK_EQ_INT(assetPackListFind(files, n, "faces.passets"), 2);
        GE_CHECK_EQ_INT(assetPackListFind(files, n, "none.passets"), -1);
        GE_CHECK_EQ_INT(assetPackListParse("", files, ASSETPACK_MAX), 0);
        GE_CHECK_EQ_INT(assetPackListParse(NULL, files, ASSETPACK_MAX), 0);
        assetPackListFormat(files, 0, text, sizeof text);
        GE_CHECK_STR(text, "");
        GE_CHECK(assetPackIsFileName("x.passets") && assetPackIsFileName("x.ptexpack"));
        GE_CHECK(!assetPackIsFileName(".passets") && !assetPackIsFileName("x.pmod") && !assetPackIsFileName("a;b.passets"));
    }

    /* the whole list: disabled packs keep their place, written with a "-" */
    {
        char text[ASSETPACK_LIST_LEN], en[ASSETPACK_MAX][ASSETPACK_FILE_LEN];
        int on[ASSETPACK_MAX];
        int n = assetPackEntriesParse(" hd.passets ; -faces.passets;;-hd.passets;-bad name.txt;old.ptexpack;-a/b.passets;-x.passets", files, on, ASSETPACK_MAX);
        GE_CHECK_EQ_INT(n, 4);
        GE_CHECK_STR(files[1], "faces.passets");
        GE_CHECK(on[0] && !on[1] && on[2] && !on[3]);
        assetPackEntriesFormat(files, on, n, text, sizeof text);
        GE_CHECK_STR(text, "hd.passets;-faces.passets;old.ptexpack;-x.passets");
        GE_CHECK(assetPackEntriesMove(files, on, n, 3, 1));   /* a disabled pack moves with its flag */
        assetPackEntriesFormat(files, on, n, text, sizeof text);
        GE_CHECK_STR(text, "hd.passets;-x.passets;-faces.passets;old.ptexpack");
        GE_CHECK(assetPackEntriesMove(files, on, n, 0, 3));
        assetPackEntriesFormat(files, on, n, text, sizeof text);
        GE_CHECK_STR(text, "-x.passets;-faces.passets;old.ptexpack;hd.passets");
        GE_CHECK(!assetPackEntriesMove(files, on, n, 2, 2) && !assetPackEntriesMove(files, on, n, 0, 4));
        GE_CHECK_EQ_INT(assetPackEntriesEnabled(files, on, n, en, ASSETPACK_MAX), 2);
        GE_CHECK_STR(en[0], "old.ptexpack");
        GE_CHECK_STR(en[1], "hd.passets");
        /* the older form (no "-") is all enabled; the enabled-only reader skips the disabled ones */
        n = assetPackEntriesParse("a.passets;b.passets", files, on, ASSETPACK_MAX);
        GE_CHECK(n == 2 && on[0] && on[1]);
        n = assetPackListParse("a.passets;-b.passets;c.passets", files, ASSETPACK_MAX);
        GE_CHECK_EQ_INT(n, 2);
        GE_CHECK_STR(files[1], "c.passets");
        GE_CHECK(!assetPackIsFileName("-a.passets"));
    }

    /* the resolution: the mod first, then the packs in order, every pack partial */
    m = texpackIndexOpen(mod);
    GE_CHECK(m != NULL);
    {
        const char *roots[3] = { hd, partial, old };
        AssetStack *s = assetStackOpen(roots, 3);
        GE_CHECK(s != NULL);
        GE_CHECK_EQ_INT(assetStackCount(s), 3);
        GE_CHECK(assetStackOpened(s, 0) && assetStackOpened(s, 1) && assetStackOpened(s, 2));
        GE_CHECK_EQ_INT(assetStackTextureCount(s, 1), 2);
        GE_CHECK_EQ_INT(assetStackTextureFrom(s, m, 1), 0);
        GE_CHECK_EQ_INT(assetStackTextureFrom(s, m, 2), 0);                    /* hd is above faces */
        GE_CHECK_EQ_INT(assetStackTextureFrom(s, m, 3), ASSET_FROM_MOD);       /* the mod always wins */
        GE_CHECK_EQ_INT(assetStackTextureFrom(s, NULL, 3), 1);
        GE_CHECK_EQ_INT(assetStackTextureFrom(s, m, 4), 2);
        GE_CHECK_EQ_INT(assetStackTextureFrom(s, m, 5), ASSET_FROM_NONE);
        GE_CHECK_STR(readTex(s, m, 1), "hd1");
        GE_CHECK_STR(readTex(s, m, 2), "hd2");
        GE_CHECK_STR(readTex(s, m, 3), "mod3");
        GE_CHECK_STR(readTex(s, NULL, 3), "faces3");                          /* read from the tar */
        GE_CHECK_STR(readTex(s, m, 4), "old4");
        GE_CHECK_STR(readTex(s, m, 5), "");
        assetStackClose(s);
    }
    {
        const char *roots[3] = { partial, "/no/such/pack.passets", hd };      /* reordered; an unreadable pack is skipped */
        AssetStack *s = assetStackOpen(roots, 3);
        GE_CHECK(!assetStackOpened(s, 1));
        GE_CHECK_STR(assetStackRoot(s, 1), "/no/such/pack.passets");
        GE_CHECK_EQ_INT(assetStackTextureFrom(s, NULL, 2), 0);                 /* faces is above hd now */
        GE_CHECK_STR(readTex(s, NULL, 2), "faces2");
        GE_CHECK_STR(readTex(s, NULL, 1), "hd1");
        GE_CHECK_EQ_INT(assetStackTextureFrom(s, NULL, 4), ASSET_FROM_NONE);  /* old is not enabled */
        assetStackClose(s);
    }
    {
        AssetStack *s = assetStackOpen(NULL, 0);                                /* no pack enabled: only the mod */
        GE_CHECK_EQ_INT(assetStackCount(s), 0);
        GE_CHECK_EQ_INT(assetStackTextureFrom(s, m, 3), ASSET_FROM_MOD);
        GE_CHECK_EQ_INT(assetStackTextureFrom(s, m, 1), ASSET_FROM_NONE);
        assetStackClose(s);
    }
    texpackIndexClose(m);

    remove(partial);
    for (int i = 0; i < g_nf; i++) remove(g_files[i]);
    for (int i = g_nd - 1; i >= 0; i--) RMDIR(g_dirs[i]);
    RMDIR(tmp);
    return GE_TEST_END();
}
