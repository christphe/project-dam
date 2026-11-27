/*
 * texpack.c -- texture packs in the game (port/include/texpack.h; the format is port/mission/texpacks.h).
 *
 * Two tables, one lock:
 *  - address -> texture number, filled by texLoad on the game thread (the texture pool is a ring: a reused address is
 *    registered again with its new number). Addresses are taken in the V1 view of DRAM, the KSEG0 mirror folded onto it,
 *    as fast3d sees them either way.
 *  - texture number -> the pack's decoded picture, filled on the render thread the first time fast3d uploads that texture
 *    (one PNG decode, then fast3d's own texture cache keeps it on the GPU). Pictures are kept up to a byte budget and the
 *    oldest dropped past it; a dropped one is decoded again if fast3d ever uploads it again.
 * The pack itself is read through its own index (texpackIndexOpen): never through the package layer's shared cache, which
 * the game thread uses for mods.
 * The mod of the active mission (texpackSetMod) is a second source with the same layout (textures/<n>.png in X.pmod), looked
 * up before the pack. Stopgap until the game draws its levels from the glTF export: the editor already resolves a mod's
 * textures there (editor/decor.cpp), and this mod lookup goes away with the texLoad hook then.
 */
#include "platform.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"
#include "config.h"
#include "portaddr.h"
#include "texpack.h"
#include "texpacks.h"

#define REG_SIZE 16384            /* power of two; cleared when 3/4 full */
#ifdef __SWITCH__
#define CACHE_BUDGET (192u << 20)
#else
#define CACHE_BUDGET (768u << 20)
#endif

#define FMT_IA 3                  /* G_IM_FMT_IA (PR/gbi.h) */
#define FMT_I 4                   /* G_IM_FMT_I */

static char s_cfgPack[TEXPACK_FILE_LEN];   /* Video.TexturePack */
static char s_inUse[TEXPACK_FILE_LEN];
static TexPackIndex *s_ix;
static TexPackIndex *s_modIx;     /* the active mission's mod: its textures/ come first */
static char s_modDir[600];
static unsigned s_gen = 1;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct { uintptr_t addr; int16_t texnum; } RegSlot;
static RegSlot s_reg[REG_SIZE];
static int s_regUsed;

typedef struct {
    uint8_t *rgba, *lum;          /* the picture; the same with alpha = luminance (intensity tiles), made on demand */
    int w, h, opaque, failed;
    unsigned order;               /* when it was decoded: the oldest goes first */
} Pic;
static Pic s_pic[TEXPACK_MAX_TEXNUM + 1];
static size_t s_picBytes;
static unsigned s_picTick;
static unsigned s_picGen;         /* the s_gen the pictures belong to: the render thread drops them when it changes */

PD_CONSTRUCTOR static void texpackConfigInit(void)
{
    configRegisterString("Video.TexturePack", s_cfgPack, sizeof s_cfgPack);
}

static const char *packsDir(void)
{
    static char dir[600];
    snprintf(dir, sizeof dir, "%s", sysResolvePath("$S/../texturepacks"));
    return dir;
}

static uintptr_t normAddr(const void *p)
{
    uintptr_t a = (uintptr_t)p;
    if (a >= (uintptr_t)PORT_DRAM_K0_BASE && a < (uintptr_t)PORT_DRAM_K0_BASE + PORT_DRAM_SIZE)
        a -= (uintptr_t)PORT_DRAM_K0_BASE - (uintptr_t)PORT_DRAM_V1_BASE;
    return a;
}

static void picFree(Pic *p)
{
    s_picBytes -= (size_t)p->w * p->h * 4 * ((p->rgba ? 1 : 0) + (p->lum ? 1 : 0));
    free(p->rgba);
    free(p->lum);
    memset(p, 0, sizeof *p);
}

static void picsClear(void)
{
    for (int i = 0; i <= TEXPACK_MAX_TEXNUM; i++) if (s_pic[i].rgba || s_pic[i].failed) picFree(&s_pic[i]);
    s_picBytes = 0;
}

void texpackSelect(const char *file)
{
    char root[900];
    TexPackIndex *ix = NULL;
    if (!file) file = "";
    snprintf(s_cfgPack, sizeof s_cfgPack, "%s", file);
    if (file[0]) {
        snprintf(root, sizeof root, "%s/%s", packsDir(), file);
        ix = texpackIndexOpen(root);
        if (!ix) sysLogPrintf(LOG_WARNING, "texture pack: cannot read %s (a .ptexpack tar or folder expected)", root);
        else sysLogPrintf(LOG_INFO, "texture pack: %s, %d textures", root, texpackIndexCount(ix));
    } else if (s_ix) {
        sysLogPrintf(LOG_INFO, "texture pack: none");
    }
    pthread_mutex_lock(&s_lock);
    texpackIndexClose(s_ix);
    s_ix = ix;
    snprintf(s_inUse, sizeof s_inUse, "%s", ix ? file : "");
    s_gen++;   /* new cache keys: fast3d uploads every replaced texture again; the render thread drops the old pictures */
    pthread_mutex_unlock(&s_lock);
}

void texpackSetMod(const char *dir)
{
    TexPackIndex *ix = NULL;
    if (!dir) dir = "";
    if (!strcmp(dir, s_modDir)) return;
    if (dir[0]) {
        ix = texpackIndexOpen(dir);
        if (ix && texpackIndexCount(ix) == 0) { texpackIndexClose(ix); ix = NULL; }
        if (ix) sysLogPrintf(LOG_INFO, "texture pack: the mod %s replaces %d textures", dir, texpackIndexCount(ix));
    }
    pthread_mutex_lock(&s_lock);
    texpackIndexClose(s_modIx);
    s_modIx = ix;
    snprintf(s_modDir, sizeof s_modDir, "%s", dir);
    s_gen++;
    pthread_mutex_unlock(&s_lock);
}

/* The source of texture n: the mod's index, then the pack's (NULL: neither has it). Under s_lock. */
static TexPackIndex *sourceOf(int n)
{
    if (texpackIndexHas(s_modIx, n)) return s_modIx;
    if (texpackIndexHas(s_ix, n)) return s_ix;
    return NULL;
}

void texpackInit(void)
{
    char want[TEXPACK_FILE_LEN];
    snprintf(want, sizeof want, "%s", s_cfgPack);
    if (want[0]) texpackSelect(want);
}

const char *texpackSelected(void) { return s_inUse; }

int texpackListPacks(char (*files)[128], char (*names)[64], int max)
{
    TexPackInfo info[64];
    int n = texpackScan(packsDir(), info, 64);
    if (n > 64) n = 64;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) {
        snprintf(files[i], 128, "%s", info[i].file);
        snprintf(names[i], 64, "%s", info[i].name);
    }
    return n;
}

void texpackRegister(const void *data, int texnum)
{
    uintptr_t a = normAddr(data);
    size_t h;
    if (!data || texnum < 0 || texnum > TEXPACK_MAX_TEXNUM) return;
    h = (size_t)((a >> 3) * 2654435761u) & (REG_SIZE - 1);
    pthread_mutex_lock(&s_lock);
    if (s_regUsed >= REG_SIZE * 3 / 4) { memset(s_reg, 0, sizeof s_reg); s_regUsed = 0; }
    while (s_reg[h].addr && s_reg[h].addr != a) h = (h + 1) & (REG_SIZE - 1);
    if (!s_reg[h].addr) { s_reg[h].addr = a; s_regUsed++; }
    s_reg[h].texnum = (int16_t)texnum;
    pthread_mutex_unlock(&s_lock);
}

static int lookupLocked(uintptr_t a)
{
    size_t h = (size_t)((a >> 3) * 2654435761u) & (REG_SIZE - 1);
    while (s_reg[h].addr) {
        if (s_reg[h].addr == a) return s_reg[h].texnum;
        h = (h + 1) & (REG_SIZE - 1);
    }
    return -1;
}

unsigned texpackSalt(const void *addr, int *texnum)
{
    unsigned salt = 0;
    *texnum = -1;
    if (!s_ix && !s_modIx) return 0;   /* no pack, no mod textures: one unlocked test per import */
    pthread_mutex_lock(&s_lock);
    if (s_picGen != s_gen) { picsClear(); s_picGen = s_gen; }
    {
        const int n = lookupLocked(normAddr(addr));
        if (n >= 0 && sourceOf(n) && !s_pic[n].failed) {
            *texnum = n;
            salt = ((0x5EA70000u ^ (s_gen * 2654435761u)) + (unsigned)n * 40503u) | 1u;
        }
    }
    pthread_mutex_unlock(&s_lock);
    return salt;
}

static void evictFor(size_t need)
{
    while (s_picBytes + need > CACHE_BUDGET) {
        int old = -1;
        for (int i = 0; i <= TEXPACK_MAX_TEXNUM; i++)
            if (s_pic[i].rgba && (old < 0 || s_pic[i].order < s_pic[old].order)) old = i;
        if (old < 0) return;
        picFree(&s_pic[old]);
    }
}

const uint8_t *texpackPixels(int texnum, int fmt, int maxSize, int *w, int *h)
{
    const uint8_t *out = NULL;
    Pic *p;
    if (texnum < 0 || texnum > TEXPACK_MAX_TEXNUM) return NULL;
    pthread_mutex_lock(&s_lock);
    if (s_picGen != s_gen) { picsClear(); s_picGen = s_gen; }
    p = &s_pic[texnum];
    if (!p->rgba && !p->failed && sourceOf(texnum)) {
        uint8_t *file = NULL, *rgba = NULL;
        size_t len = 0;
        int pw = 0, ph = 0;
        if (texpackIndexRead(sourceOf(texnum), texnum, &file, &len) && texpackDecodePng(file, len, &pw, &ph, &rgba)) {
            if (pw > maxSize || ph > maxSize) {
                sysLogPrintf(LOG_WARNING, "texture pack: textures/%d.png is %dx%d, over this GPU's %d: the game's own is drawn", texnum, pw, ph, maxSize);
                free(rgba);
                p->failed = 1;
            } else {
                const size_t n = (size_t)pw * ph;
                evictFor(n * 4);
                p->rgba = rgba; p->w = pw; p->h = ph; p->order = ++s_picTick;
                p->opaque = 1;
                for (size_t i = 0; i < n; i++) if (rgba[i * 4 + 3] != 255) { p->opaque = 0; break; }
                s_picBytes += n * 4;
            }
        } else {
            sysLogPrintf(LOG_WARNING, "texture pack: textures/%d.png is not readable (an 8-bit PNG, not interlaced, is expected): the game's own is drawn", texnum);
            p->failed = 1;
        }
        free(file);
    }
    if (p->rgba) {
        out = p->rgba;
        if ((fmt == FMT_I || fmt == FMT_IA) && p->opaque) {   /* the RDP reads an intensity texel's alpha from its value */
            if (!p->lum && (p->lum = (uint8_t *)malloc((size_t)p->w * p->h * 4)) != NULL) {
                for (size_t i = 0; i < (size_t)p->w * p->h; i++) {
                    const uint8_t *s = p->rgba + i * 4;
                    uint8_t *d = p->lum + i * 4;
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
                    d[3] = (uint8_t)((s[0] * 299 + s[1] * 587 + s[2] * 114) / 1000);
                }
                s_picBytes += (size_t)p->w * p->h * 4;
            }
            if (p->lum) out = p->lum;
        }
        *w = p->w; *h = p->h;
    }
    pthread_mutex_unlock(&s_lock);
    return out;
}
