/*
 * missions.c -- custom co-op missions in the game (issues #101, #103, #104): the active
 * mission, the command line, the missions directory, and the rebuilt stage setup.
 *
 * A mission is a delta over one solo level (port/mission/mission.h). When one is chosen
 * (--mission, or "More..." in the mission select) the level's setup row of the pccg
 * sidecar is replaced by a rebuilt copy (port/mission/mission_apply.c) that pccg.c serves
 * to the unmodified loader (src/game/prop.c proplvreset2). Everything that is not a
 * mission behaves exactly as before: with no mission active nothing here changes a byte.
 */
#include "platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "system.h"
#include "fs.h"
#include "mission.h"
#include "mission_apply.h"
#include "missions.h"
#include "missions_priv.h"
#include "realzlib.h"   /* host zlib, as rzdecomp.c */

extern char *setup_text_pointers[];   /* src/game/chraidata.c: stage number -> "Usetup<name>Z" */
int  pccgRowData(const char *name, const uint8_t **data, uint32_t *size);
int  pccgServeRow(const char *name, const uint8_t *data, uint32_t size);
void pccgRestoreRow(const char *name);

static Mission s_m;
static int  s_active = 0;
static int  s_stage = -1;
static int  s_spawn = -1;
static int  s_argsDone = 0;
static int  s_patched = 0;
static char s_dir[512] = "";           /* the active mission's folder */
static char s_modDir[512] = "";        /* its mod folder (own scripts), "" for --mission outside a mod */
static int  s_slot = -1;               /* its place in the mod's mission select */
static char s_served[64] = "";         /* the setup row currently replaced */


int         missionsActive(void) { return s_active; }
const char *missionsName(void) { return s_active ? s_m.name : ""; }
const char *missionsBriefing(void) { return s_active ? s_m.briefing : ""; }
int         missionsStage(void) { return s_active ? s_stage : -1; }
const Mission *missionsCurrent(void) { return s_active ? &s_m : NULL; }
const char *missionsDir(void) { return s_dir; }
const char *missionsModDir(void) { return s_modDir; }
int         modsActiveSlot(void) { return s_active ? s_slot : -1; }
int         missionsSpawnPad(void) { return s_spawn; }

/* ---- the setup row ------------------------------------------------------ */

/* The RZ stream the loader reads is "0x11 0x72" + a raw deflate stream; a stored (uncompressed)
 * deflate stream is as valid and needs no compressor. */
static uint8_t *rzStore(const uint8_t *raw, uint32_t n, uint32_t *outn)
{
    uint32_t blocks = n ? (n + 65534u) / 65535u : 1u, o = 2, pos = 0;
    uint8_t *out = (uint8_t *)malloc(2u + blocks * 5u + n);
    if (!out) return NULL;
    out[0] = 0x11; out[1] = 0x72;
    do {
        uint32_t len = n - pos > 65535u ? 65535u : n - pos;
        out[o++] = (pos + len >= n) ? 1 : 0;                 /* BFINAL, BTYPE 00 */
        out[o++] = (uint8_t)len; out[o++] = (uint8_t)(len >> 8);
        out[o++] = (uint8_t)~len; out[o++] = (uint8_t)(~len >> 8);
        memcpy(out + o, raw + pos, len);
        o += len; pos += len;
    } while (pos < n);
    *outn = o;
    return out;
}

static uint8_t *rzInflate(const uint8_t *rz, uint32_t n, uint32_t *outn)
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

static void logApplied(const char *log)
{
    char line[256];
    const char *p = log;
    while (*p) {
        size_t n = strcspn(p, "\n");
        snprintf(line, sizeof line, "missions: %.*s", (int)(n > 230 ? 230 : n), p);
        sysLogPrintf(LOG_INFO, "%s", line);
        p += n + (p[n] ? 1 : 0);
    }
}

/* src/game/bg.c: levelinfotable[STAGES_MAX] (struct levelentry, bg.h). The setup's pad positions are stored unscaled and
 * the loader multiplies them by room_data_float2 = 1 / levelscale; mission.json positions are world units, so the new
 * pads are divided by that factor when written (the bg is not loaded yet when the setup row is served). */
struct LevelEntryMirror { s32 levelID; void *bg, *stan; f32 levelscale, visibility, unknown; };
extern struct LevelEntryMirror levelinfotable[];
#define LEVELINFO_COUNT 38

static float levelPosScale(int stage)
{
    for (int i = 0; i < LEVELINFO_COUNT; i++)
        if (levelinfotable[i].levelID == stage && levelinfotable[i].levelscale > 0.0f)
            return 1.0f / levelinfotable[i].levelscale;
    return 1.0f;
}

/* Rebuild the active mission's setup row and have pccg serve it. */
static void buildOverride(void)
{
    const char *name;
    const uint8_t *rz;
    uint32_t rzn = 0, rawn = 0, outn = 0;
    uint8_t *raw, *store;
    MissionApplyResult res;
    char err[160] = "";

    if (!s_active || !s_patched || s_served[0]) return;
    if (s_stage < 0 || s_stage > 90 || !(name = setup_text_pointers[s_stage])) {
        sysLogPrintf(LOG_WARNING, "missions: level %s has no setup file", s_m.level);
        return;
    }
    if (!pccgRowData(name, &rz, &rzn)) {
        sysLogPrintf(LOG_WARNING, "missions: setup row %s is not in the pccg sidecar", name);
        return;
    }
    raw = rzInflate(rz, rzn, &rawn);
    if (!raw) { sysLogPrintf(LOG_WARNING, "missions: cannot inflate %s", name); return; }
    if (!missionApplySetup(&s_m, raw, rawn, s_spawn, levelPosScale(s_stage), &res, err, sizeof err)) {
        sysLogPrintf(LOG_WARNING, "missions: cannot apply the mission to %s: %s", name, err);
        free(raw);
        return;
    }
    free(raw);
    logApplied(res.log);
    sysLogPrintf(LOG_INFO, "missions: %s: %d guard(s) moved, %d added, %d removed; %d object(s) moved, %d added, %d removed; %d pad(s) added; %d op(s) skipped",
                 s_m.name, res.guards_moved, res.guards_added, res.guards_removed, res.objects_moved, res.objects_added,
                 res.objects_removed, res.pads_added, res.skipped);
    store = rzStore(res.data, res.size, &outn);
    missionApplyFree(&res);
    if (!store) return;
    if (pccgServeRow(name, store, outn)) {
        snprintf(s_served, sizeof s_served, "%s", name);
        sysLogPrintf(LOG_INFO, "missions: serving the rebuilt %s (%u bytes) for \"%s\"", name, outn, s_m.name);
    } else {
        sysLogPrintf(LOG_WARNING, "missions: could not serve the rebuilt %s", name);
    }
    free(store);
}

void missionsOnPatchTable(void)
{
    s_patched = 1;
    missionsEnsureArgs();
    buildOverride();
}

/* ---- activation ---------------------------------------------------------- */

static void dirOf(const char *file, char *out, size_t n)
{
    const char *a = strrchr(file, '/'), *b = strrchr(file, '\\');
    const char *s = a > b ? a : b;
    size_t len = s ? (size_t)(s - file) : 0;
    if (!s) { snprintf(out, n, "."); return; }
    if (len >= n) len = n - 1;
    memcpy(out, file, len);
    out[len] = 0;
}

void missionsDeactivate(void)
{
    modsUnbind();
    if (s_served[0]) {
        pccgRestoreRow(s_served);
        s_served[0] = 0;
    }
    if (s_active) {
        sysLogPrintf(LOG_INFO, "missions: \"%s\" deactivated", s_m.name);
        missionsHostReset();
        missionFree(&s_m);
        missionInit(&s_m);
    }
    s_slot = -1;
    s_active = 0;
    s_stage = -1;
}

int missionsActivateFile(const char *path, int spawn_pad, const char *mod_dir, int slot)
{
    char err[200] = "";
    Mission m;
    missionInit(&m);
    if (!missionLoadFile(path, &m, err, sizeof err) || !missionValidate(&m, err, sizeof err)) {
        sysLogPrintf(LOG_WARNING, "missions: %s: %s", path, err);
        missionFree(&m);
        return 0;
    }
    missionsDeactivate();
    s_m = m;
    s_spawn = spawn_pad;
    s_stage = missionLevelNumber(m.level);
    dirOf(path, s_dir, sizeof s_dir);
    snprintf(s_modDir, sizeof s_modDir, "%s", mod_dir ? mod_dir : "");
    s_slot = slot;
    s_active = 1;
    sysLogPrintf(LOG_INFO, "missions: \"%s\" (%s, stage %d) from %s", s_m.name, s_m.level, s_stage, path);
    buildOverride();
    return 1;
}

/* "-level_NN" of the command line, -1 if none. */
static int bootLevel(void)
{
    const char *t = strstr(sysGetTokenString(), "-level_");
    if (!t || t[7] < '0' || t[7] > '9' || t[8] < '0' || t[8] > '9') return -1;
    return (t[7] - '0') * 10 + (t[8] - '0');
}

void missionsEnsureArgs(void)
{
    const char *f, *sp;
    if (s_argsDone) return;
    s_argsDone = 1;
    sp = sysArgGetString("--spawn");
    f = sysArgGetString("--mission");
    if (!f) return;
    if (!missionsActivateFile(f, sp ? atoi(sp) : -1, NULL, -1)) return;
    {
        int boot = bootLevel();
        if (boot < 0)
            sysLogPrintf(LOG_WARNING, "missions: no -level_%02d on the command line: the mission starts only if that level is chosen from the menu", s_stage);
        else if (boot != s_stage)
            sysLogPrintf(LOG_WARNING, "missions: the mission is for level %d but -level_%02d boots: it will not apply", s_stage, boot);
    }
}

