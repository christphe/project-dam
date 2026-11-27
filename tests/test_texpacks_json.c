/* The texture-pack format with textures.json (port/mission/texpacks.c, docs/specs/texture-packs.md): parsing, the
 * completeness of a pack against the base pack, and the merge of a mod over a pack (field by field, picture only, new key). */
#include "ge_test.h"
#include "mission_dir.h"
#include "ge_pkg.h"
#include "ge_texpacks.h"

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

static const char *kBase =
    "{\"version\":1,\"textures\":{"
    "\"0\":{\"image\":\"textures/0.png\",\"surface\":\"stone\",\"impact_sound\":\"stone\",\"alpha\":\"image\",\"format\":\"rgba16\",\"width\":32,\"height\":32,"
    "\"detail\":{\"s_offset\":1,\"t_offset\":2,\"s_shift\":3,\"t_shift\":4}},"
    "\"1\":{\"image\":\"textures/1.png\",\"surface\":\"wood\",\"impact_sound\":\"wood\",\"alpha\":\"luminance\",\"format\":\"i8\",\"width\":64,\"height\":32},"
    "\"2\":{\"image\":\"textures/2.png\",\"surface\":\"class13\",\"impact_sound\":\"metal\",\"alpha\":\"image\",\"future_field\":[1,2,3]},"
    "\"3\":{\"image\":null,\"surface\":\"glass_xlu\",\"impact_sound\":\"glass_xlu\",\"alpha\":\"image\"}"
    "}}";

int main(void)
{
    char tmp[500], base[600], full[600], lack[600], nopic[600], mod[600], tar[600], err[300] = "";
    const char *t = getenv("TEMP");
    TexPackSrc *b, *f, *l, *np, *m, *tp;
    TexPackResolved r;
    uint8_t *buf = NULL;
    size_t len = 0;

    GE_TEST_BEGIN("texpacks_json");
    if (!t || !*t) t = getenv("TMPDIR");
    if (!t || !*t) t = "/tmp";
    snprintf(tmp, sizeof tmp, "%s/ge_texpacks_json_test", t);
    snprintf(base, sizeof base, "%s/ge007.ptexpack", tmp);
    snprintf(full, sizeof full, "%s/full.ptexpack", tmp);
    snprintf(lack, sizeof lack, "%s/lack.ptexpack", tmp);
    snprintf(nopic, sizeof nopic, "%s/nopic.ptexpack", tmp);
    snprintf(mod, sizeof mod, "%s/m.pmod", tmp);
    snprintf(tar, sizeof tar, "%s/tar.ptexpack", tmp);
    mkd(tmp);

    /* the base pack: 4 keys, key 3 has no picture */
    put(base, "pack.json", "{\"name\":\"GoldenEye 007\",\"author\":\"Rare\",\"base\":true,\"region\":\"ntsc-final\",\"exporter_version\":11}");
    put(base, "textures.json", kBase);
    put(base, "textures/0.png", "base0"); put(base, "textures/1.png", "base1"); put(base, "textures/2.png", "base2");
    /* a complete pack of its own: other pictures, key 1's surface changed, key 3 got a picture, a custom image path */
    put(full, "pack.json", "{\"name\":\"Full\",\"author\":\"tests\",\"description\":\"d\"}");
    put(full, "textures.json",
        "{\"version\":1,\"textures\":{\"0\":{\"image\":\"hd/zero.png\",\"surface\":\"stone\"},\"1\":{\"surface\":\"metal\"},\"2\":{\"surface\":\"dirt\"},\"3\":{\"image\":\"textures/3.png\",\"surface\":\"glass\"}}}");
    put(full, "hd/zero.png", "full0"); put(full, "textures/1.png", "full1"); put(full, "textures/2.png", "full2"); put(full, "textures/3.png", "full3");
    /* lacking key 2 (the entry) and key 1 (the picture file) */
    put(lack, "textures.json", "{\"version\":1,\"textures\":{\"0\":{\"surface\":\"stone\"},\"1\":{\"surface\":\"wood\"},\"3\":{\"image\":null,\"surface\":\"glass\"}}}");
    put(lack, "textures/0.png", "lack0");
    /* no textures.json at all */
    put(nopic, "textures/0.png", "x");
    /* a mod */
    put(mod, "campaign.json", "{}");
    put(mod, "textures.json",
        "{\"version\":1,\"textures\":{"
        "\"0\":{\"surface\":\"metal\",\"impact_sound\":\"tile\"},"
        "\"2\":{\"image\":\"art/two.png\"},"
        "\"mymod/rusty\":{\"image\":\"textures/mymod/rusty.png\",\"surface\":\"metalobj\"},"
        "\"mymod/nopic\":{\"surface\":\"wood\"},"
        "\"mymod/nosurf\":{\"image\":\"textures/mymod/nosurf.png\"},"
        "\"Bad Key\":{\"surface\":\"wood\"}"
        "}}");
    put(mod, "textures/1.png", "mod1");                     /* picture only */
    put(mod, "art/two.png", "mod2");
    put(mod, "textures/mymod/rusty.png", "rusty");
    put(mod, "textures/mymod/nosurf.png", "nosurf");

    b = texpackSrcOpen(base); f = texpackSrcOpen(full); l = texpackSrcOpen(lack); np = texpackSrcOpen(nopic); m = texpackSrcOpen(mod);
    GE_CHECK(b && f && l && np && m);
    if (!(b && f && l && np && m)) return GE_TEST_END();

    /* parsing */
    GE_CHECK_EQ_INT(texpackSrcCount(b), 4);
    GE_CHECK(texpackSrcMeta(b)->base && texpackSrcMeta(b)->exporterVersion == 11);
    GE_CHECK_STR(texpackSrcMeta(b)->name, "GoldenEye 007");
    GE_CHECK_STR(texpackSrcMeta(b)->region, "ntsc-final");
    GE_CHECK(!texpackSrcMeta(f)->base && texpackSrcMeta(f)->exporterVersion == 0);
    {
        const TexPackTex *e = texpackSrcFind(b, "0");
        GE_CHECK(e != NULL);
        if (e) {
            GE_CHECK(e->surface == 1 && e->impactSound == 1 && e->alphaLuminance == 0 && e->width == 32 && e->height == 32);
            GE_CHECK(e->detail[0] == 1 && e->detail[1] == 2 && e->detail[2] == 3 && e->detail[3] == 4);
            GE_CHECK_STR(e->format, "rgba16");
            GE_CHECK_STR(e->image, "textures/0.png");
        }
        e = texpackSrcFind(b, "1");
        GE_CHECK(e && e->alphaLuminance == 1 && !(e->fields & TEXPACK_F_DETAIL));
        e = texpackSrcFind(b, "2");
        GE_CHECK(e && e->surface == 13 && e->impactSound == 3);   /* class13, and an unknown field ignored */
        e = texpackSrcFind(b, "3");
        GE_CHECK(e && e->imageNull && (e->fields & TEXPACK_F_IMAGE) && e->surface == 12);
        GE_CHECK(texpackSrcFind(b, "4") == NULL);
        GE_CHECK(texpackSrcAt(b, 0) && !strcmp(texpackSrcAt(b, 0)->key, "0") && !strcmp(texpackSrcAt(b, 3)->key, "3"));
    }
    GE_CHECK_EQ_INT(texpackSrcCount(m), 5);                  /* "Bad Key" is skipped */
    GE_CHECK(texpackSrcFind(m, "Bad Key") == NULL);
    GE_CHECK(!texpackSrcHasTexturesJson(np) && texpackSrcHasTexturesJson(l));
    GE_CHECK(texpackSrcHasFile(b, "textures/0.png") && !texpackSrcHasFile(b, "textures/3.png"));
    GE_CHECK(!texpackSrcHasFile(b, "../x") && !texpackSrcHasFile(b, "/pack.json") && !texpackSrcHasFile(b, "textures//0.png"));
    GE_CHECK(texpackSrcReadFile(b, "textures/1.png", &buf, &len) && len == 5 && !memcmp(buf, "base1", 5));
    free(buf);

    /* names and keys */
    GE_CHECK(!strcmp(texpackSurfaceName(12), "glass_xlu") && !strcmp(texpackSurfaceName(15), "class15") && !texpackSurfaceName(16));
    GE_CHECK(texpackSurfaceFromName("metalobj") == 10 && texpackSurfaceFromName("chr") == 11 && texpackSurfaceFromName("lava") == -1);
    GE_CHECK(texpackKeyIsNumber("412") && texpackKeyIsNumber("0") && !texpackKeyIsNumber("012") && !texpackKeyIsNumber("a1") && !texpackKeyIsNumber(""));
    GE_CHECK(texpackKeyValid("mymod/rusty_door-2.x") && !texpackKeyValid("Mod") && !texpackKeyValid("") && !texpackKeyValid("a b"));

    /* completeness against the base */
    GE_CHECK_EQ_INT(texpackMissing(b, b), 0);
    GE_CHECK_EQ_INT(texpackMissing(f, b), 0);   /* a complete pack, key 3 may have a picture the base lacks */
    GE_CHECK_EQ_INT(texpackMissing(l, b), 2);   /* key 2 has no entry, key 1's picture is absent */
    GE_CHECK_EQ_INT(texpackMissing(np, b), 4);
    GE_CHECK_EQ_INT(texpackMissing(NULL, b), 4);

    /* resolving: a pack alone */
    GE_CHECK(texpackResolve(NULL, f, "0", &r));
    GE_CHECK(r.imageFrom == TEXPACK_FROM_PACK && !strcmp(r.tex.image, "hd/zero.png") && r.inPack && r.modFields == 0);
    GE_CHECK(texpackResolvedRead(NULL, f, &r, &buf, &len) && len == 5 && !memcmp(buf, "full0", 5));
    free(buf);
    GE_CHECK(texpackResolve(NULL, f, "1", &r) && r.tex.surface == 3 && !strcmp(r.tex.image, "textures/1.png"));   /* default image path */
    GE_CHECK(texpackResolve(NULL, b, "3", &r) && r.imageFrom == TEXPACK_FROM_NONE && r.tex.imageNull);
    GE_CHECK(!texpackResolve(NULL, b, "9", &r));
    GE_CHECK(!texpackResolve(NULL, b, "Bad", &r));

    /* a mod over the base: fields only */
    GE_CHECK(texpackResolve(m, b, "0", &r));
    GE_CHECK(r.tex.surface == 3 && r.tex.impactSound == 9);                                 /* the mod's */
    GE_CHECK(r.tex.width == 32 && r.tex.detail[3] == 4 && !strcmp(r.tex.format, "rgba16"));  /* the pack's */
    GE_CHECK(r.imageFrom == TEXPACK_FROM_PACK && !strcmp(r.tex.image, "textures/0.png"));
    GE_CHECK(r.modFields == (TEXPACK_F_SURFACE | TEXPACK_F_IMPACT));
    GE_CHECK(texpackResolvedRead(m, b, &r, &buf, &len) && !memcmp(buf, "base0", 5));
    free(buf);
    /* picture only (no entry in the mod) */
    GE_CHECK(texpackResolve(m, b, "1", &r));
    GE_CHECK(r.imageFrom == TEXPACK_FROM_MOD && r.tex.surface == 2 && r.tex.alphaLuminance == 1 && r.modFields == TEXPACK_F_IMAGE);
    GE_CHECK(texpackResolvedRead(m, b, &r, &buf, &len) && !memcmp(buf, "mod1", 4));
    free(buf);
    /* an entry with its own image path, resolved in the mod */
    GE_CHECK(texpackResolve(m, b, "2", &r));
    GE_CHECK(r.imageFrom == TEXPACK_FROM_MOD && !strcmp(r.tex.image, "art/two.png") && r.tex.surface == 13);
    GE_CHECK(texpackResolvedRead(m, b, &r, &buf, &len) && !memcmp(buf, "mod2", 4));
    free(buf);
    /* the same mod over another pack: the surface from the mod, the picture from that pack */
    GE_CHECK(texpackResolve(m, f, "0", &r) && r.tex.surface == 3 && !strcmp(r.tex.image, "hd/zero.png") && r.imageFrom == TEXPACK_FROM_PACK);
    /* a texture the mod does not touch */
    GE_CHECK(texpackResolve(m, b, "3", &r) && r.modFields == 0 && r.imageFrom == TEXPACK_FROM_NONE);
    /* new keys */
    GE_CHECK(texpackResolve(m, b, "mymod/rusty", &r));
    GE_CHECK(!r.inPack && r.tex.surface == 10 && r.tex.impactSound == 10 && r.imageFrom == TEXPACK_FROM_MOD);   /* impact defaults to the surface */
    GE_CHECK(texpackResolvedRead(m, b, &r, &buf, &len) && !memcmp(buf, "rusty", 5));
    free(buf);
    GE_CHECK(!texpackResolve(m, b, "mymod/nopic", &r));      /* no picture */
    GE_CHECK(!texpackResolve(m, b, "mymod/nosurf", &r));     /* no surface */
    GE_CHECK(!texpackResolve(m, b, "mymod/none", &r));

    /* a pack as a tar */
    GE_CHECK_EQ_INT(pkgWriteDir(full, tar, err, sizeof err), 1);
    tp = texpackSrcOpen(tar);
    GE_CHECK(tp != NULL);
    if (tp) {
        GE_CHECK_EQ_INT(texpackSrcCount(tp), 4);
        GE_CHECK_STR(texpackSrcMeta(tp)->name, "Full");
        GE_CHECK_EQ_INT(texpackMissing(tp, b), 0);
        GE_CHECK(texpackResolve(m, tp, "1", &r) && r.imageFrom == TEXPACK_FROM_MOD);
        GE_CHECK(texpackResolve(NULL, tp, "0", &r) && texpackResolvedRead(NULL, tp, &r, &buf, &len) && !memcmp(buf, "full0", 5));
        free(buf);
        texpackSrcClose(tp);
    }
    GE_CHECK(texpackSrcOpen("/no/such/pack") == NULL);

    texpackSrcClose(b); texpackSrcClose(f); texpackSrcClose(l); texpackSrcClose(np); texpackSrcClose(m);
    remove(tar);
    for (int i = 0; i < g_nf; i++) remove(g_files[i]);
    for (int i = g_nd - 1; i >= 0; i--) RMDIR(g_dirs[i]);
    RMDIR(tmp);
    return GE_TEST_END();
}
