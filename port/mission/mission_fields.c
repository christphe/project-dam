/* Setup-record field tables: see mission_fields.h. The offsets are checked against the game
 * structs by the static asserts in port/src/levelexport.c. */
#include "mission_fields.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The header fields every ObjectRecord has (src/bondtypes.h). maxdamage / damage are 16.16 fixed point in
 * the setup file (the game turns them into floats at load): MF_FIXED shows them as the value (1000 = default). */
#define COMMON_FIELDS \
    { "extrascale", 0, 2, MF_UINT, 0 }, \
    { "state", 2, 1, MF_UINT, 0 }, \
    { "flags", 8, 4, MF_UINT, 0 }, \
    { "flags2", 12, 4, MF_UINT, 0 }, \
    { "maxdamage", 128, 4, MF_FIXED, 0 }, \
    { "damage", 132, 4, MF_FIXED, 0 },

static const MissionField kCommonF[] = {
    COMMON_FIELDS
};

static const MissionField kGuardF[] = {
    { "extrascale", 0, 2, MF_UINT, 0 },
    { "state", 2, 1, MF_UINT, 0 },
    { "preset", 12, 2, MF_UINT, 0 },
    { "chrpreset1", 14, 2, MF_UINT, 0 },
    { "health", 16, 2, MF_UINT, 0 },
    { "reaction_time", 18, 2, MF_UINT, 0 },
    { "bitflags", 20, 2, MF_UINT, 0 },
    { "head", 22, 2, MF_SINT, 0 },
};

static const MissionField k_door[] = {
    COMMON_FIELDS
    { "linked_door_offset", 144, 4, MF_SINT, 1 },
    { "max_frac", 148, 4, MF_FIXED, 1 },
    { "perim_frac", 152, 4, MF_FIXED, 1 },
    { "accel", 156, 4, MF_FIXED, 1 },
    { "decel", 160, 4, MF_FIXED, 1 },
    { "max_speed", 164, 4, MF_FIXED, 1 },
    { "door_flags", 168, 2, MF_UINT, 1 },
    { "door_type", 170, 2, MF_UINT, 1 },
    { "keyflags", 172, 4, MF_UINT, 1 },
    { "auto_close_frames", 176, 4, MF_UINT, 1 },
    { "open_sound", 180, 4, MF_UINT, 1 },
};

static const MissionField k_key[] = {
    COMMON_FIELDS
    { "keyflags", 144, 4, MF_UINT, 1 },
};

static const MissionField k_cctv[] = {
    COMMON_FIELDS
    { "lookpad", 144, 4, MF_SINT, 1 },
};

static const MissionField k_magazine[] = {
    COMMON_FIELDS
    { "ammo_type", 144, 4, MF_SINT, 1 },
};

static const MissionField k_collectable[] = {
    COMMON_FIELDS
    { "weaponnum", 144, 1, MF_SINT, 1 },
    { "linked_weapon_type", 145, 1, MF_SINT, 1 },
    { "timer", 146, 2, MF_SINT, 1 },
};

static const MissionField k_monitor[] = {
    COMMON_FIELDS
    { "owner_offset", 272, 4, MF_SINT, 1 },
    { "owner_part", 276, 4, MF_SINT, 1 },
    { "image_num", 280, 4, MF_SINT, 1 },
};

static const MissionField k_multi_monitor[] = {
    COMMON_FIELDS
    { "image_num0", 656, 1, MF_UINT, 1 },
    { "image_num1", 657, 1, MF_UINT, 1 },
    { "image_num2", 658, 1, MF_UINT, 1 },
    { "image_num3", 659, 1, MF_UINT, 1 },
};

static const MissionField k_autogun[] = {
    COMMON_FIELDS
    { "pad_id", 144, 4, MF_SINT, 1 },
    { "rot_related", 148, 4, MF_SINT, 1 },
    { "unk88", 152, 4, MF_SINT, 1 },
    { "unk8c", 156, 4, MF_SINT, 1 },
    { "speed", 180, 4, MF_SINT, 1 },
    { "aimdist", 184, 4, MF_SINT, 1 },
    { "unkac", 188, 4, MF_SINT, 1 },
    { "unkb8", 200, 4, MF_SINT, 1 },
    { "unkbc", 204, 4, MF_SINT, 1 },
    { "unkc0", 208, 4, MF_SINT, 1 },
    { "is_active", 240, 4, MF_SINT, 1 },
};

static const MissionField k_ammo[] = {
    COMMON_FIELDS
    { "slot0_model", 144, 2, MF_UINT, 1 },
    { "slot0_qty", 146, 2, MF_UINT, 1 },
    { "slot1_model", 148, 2, MF_UINT, 1 },
    { "slot1_qty", 150, 2, MF_UINT, 1 },
    { "slot2_model", 152, 2, MF_UINT, 1 },
    { "slot2_qty", 154, 2, MF_UINT, 1 },
    { "slot3_model", 156, 2, MF_UINT, 1 },
    { "slot3_qty", 158, 2, MF_UINT, 1 },
    { "slot4_model", 160, 2, MF_UINT, 1 },
    { "slot4_qty", 162, 2, MF_UINT, 1 },
    { "slot5_model", 164, 2, MF_UINT, 1 },
    { "slot5_qty", 166, 2, MF_UINT, 1 },
    { "slot6_model", 168, 2, MF_UINT, 1 },
    { "slot6_qty", 170, 2, MF_UINT, 1 },
    { "slot7_model", 172, 2, MF_UINT, 1 },
    { "slot7_qty", 174, 2, MF_UINT, 1 },
    { "slot8_model", 176, 2, MF_UINT, 1 },
    { "slot8_qty", 178, 2, MF_UINT, 1 },
    { "slot9_model", 180, 2, MF_UINT, 1 },
    { "slot9_qty", 182, 2, MF_UINT, 1 },
    { "slot10_model", 184, 2, MF_UINT, 1 },
    { "slot10_qty", 186, 2, MF_UINT, 1 },
    { "slot11_model", 188, 2, MF_UINT, 1 },
    { "slot11_qty", 190, 2, MF_UINT, 1 },
    { "slot12_model", 192, 2, MF_UINT, 1 },
    { "slot12_qty", 194, 2, MF_UINT, 1 },
};

static const MissionField k_armour[] = {
    COMMON_FIELDS
    { "initial_amount", 144, 4, MF_FIXED, 1 },
};

static const MissionField k_vehicle[] = {
    COMMON_FIELDS
    { "ai_offset", 152, 2, MF_UINT, 1 },
    { "ai_return_list", 154, 2, MF_SINT, 1 },
};

static const MissionField k_aircraft[] = {
    COMMON_FIELDS
    { "ai_offset", 152, 2, MF_UINT, 1 },
    { "ai_return_list", 154, 2, MF_SINT, 1 },
};

static const MissionField k_tank[] = {
    COMMON_FIELDS
    { "unka4", 184, 4, MF_SINT, 1 },
    { "unka8", 188, 4, MF_SINT, 1 },
    { "unkac", 192, 4, MF_SINT, 1 },
    { "unkb0", 196, 4, MF_SINT, 1 },
    { "unkb4", 200, 4, MF_SINT, 1 },
    { "unkb8", 204, 4, MF_SINT, 1 },
    { "unkbc", 208, 4, MF_SINT, 1 },
    { "unkc0", 212, 4, MF_SINT, 1 },
};

static const MissionField k_tinted_glass[] = {
    COMMON_FIELDS
    { "tint_dist", 144, 4, MF_SINT, 1 },
    { "cull_dist", 148, 4, MF_SINT, 1 },
    { "unk90", 160, 4, MF_SINT, 1 },
};


typedef struct RecDef { int type; const char *group; const MissionField *fields; int nfields; } RecDef;

static const RecDef kRecs[] = {
    { 9, NULL, kGuardF, (int)(sizeof kGuardF / sizeof *kGuardF) },
    { 1, "door", k_door, (int)(sizeof k_door / sizeof *k_door) },
    { 3, NULL, kCommonF, (int)(sizeof kCommonF / sizeof *kCommonF) },
    { 4, "key", k_key, (int)(sizeof k_key / sizeof *k_key) },
    { 5, NULL, kCommonF, (int)(sizeof kCommonF / sizeof *kCommonF) },
    { 6, "cctv", k_cctv, (int)(sizeof k_cctv / sizeof *k_cctv) },
    { 7, "magazine", k_magazine, (int)(sizeof k_magazine / sizeof *k_magazine) },
    { 8, "collectable", k_collectable, (int)(sizeof k_collectable / sizeof *k_collectable) },
    { 10, "monitor", k_monitor, (int)(sizeof k_monitor / sizeof *k_monitor) },
    { 11, "multi_monitor", k_multi_monitor, (int)(sizeof k_multi_monitor / sizeof *k_multi_monitor) },
    { 12, NULL, kCommonF, (int)(sizeof kCommonF / sizeof *kCommonF) },
    { 13, "autogun", k_autogun, (int)(sizeof k_autogun / sizeof *k_autogun) },
    { 17, NULL, kCommonF, (int)(sizeof kCommonF / sizeof *kCommonF) },
    { 20, "ammo", k_ammo, (int)(sizeof k_ammo / sizeof *k_ammo) },
    { 21, "armour", k_armour, (int)(sizeof k_armour / sizeof *k_armour) },
    { 36, NULL, kCommonF, (int)(sizeof kCommonF / sizeof *kCommonF) },
    { 39, "vehicle", k_vehicle, (int)(sizeof k_vehicle / sizeof *k_vehicle) },
    { 40, "aircraft", k_aircraft, (int)(sizeof k_aircraft / sizeof *k_aircraft) },
    { 42, NULL, kCommonF, (int)(sizeof kCommonF / sizeof *kCommonF) },
    { 43, NULL, kCommonF, (int)(sizeof kCommonF / sizeof *kCommonF) },
    { 45, "tank", k_tank, (int)(sizeof k_tank / sizeof *k_tank) },
    { 47, "tinted_glass", k_tinted_glass, (int)(sizeof k_tinted_glass / sizeof *k_tinted_glass) },
};

static const RecDef *recdef(int type)
{
    for (size_t i = 0; i < sizeof kRecs / sizeof *kRecs; i++)
        if (kRecs[i].type == type) return &kRecs[i];
    return NULL;
}

int missionRecFields(int type, const MissionField **out)
{
    const RecDef *d = recdef(type);
    if (out) *out = d ? d->fields : NULL;
    return d ? d->nfields : 0;
}

const char *missionRecGroup(int type)
{
    const RecDef *d = recdef(type);
    return d ? d->group : NULL;
}

double missionFieldGet(const MissionField *f, const uint8_t *rec)
{
    const uint8_t *p = rec + f->off;
    uint32_t u = 0;
    for (int i = 0; i < f->size; i++) u |= (uint32_t)p[i] << (8 * i);
    if (f->kind == MF_FLOAT) { float v; memcpy(&v, &u, 4); return v; }
    if (f->kind == MF_FIXED) return (int32_t)u / 65536.0;
    if (f->kind == MF_SINT) {
        if (f->size == 1) return (int8_t)u;
        if (f->size == 2) return (int16_t)u;
        return (int32_t)u;
    }
    return u;
}

void missionFieldSet(const MissionField *f, uint8_t *rec, double v)
{
    uint8_t *p = rec + f->off;
    uint32_t u;
    if (f->kind == MF_FLOAT) { float x = (float)v; memcpy(&u, &x, 4); }
    else if (f->kind == MF_FIXED) u = (uint32_t)(int32_t)llround(v * 65536.0);
    else if (v < 0) u = (uint32_t)(int64_t)v;                       /* two's complement, truncated to the size */
    else u = v >= 4294967295.0 ? 0xFFFFFFFFu : (uint32_t)v;
    for (int i = 0; i < f->size; i++) p[i] = (uint8_t)(u >> (8 * i));
}

void missionFloatText(char *buf, size_t n, float v)
{
    if (v != v || v > 3.4e38f || v < -3.4e38f) { snprintf(buf, n, "0"); return; }
    for (int p = 1; p <= 9; p++) {
        snprintf(buf, n, "%.*g", p, (double)v);
        if (strtof(buf, NULL) == v) return;
    }
}

static void catf(char *buf, size_t n, size_t *used, const char *k, const char *num, int comma)
{
    int r;
    if (*used >= n) return;
    r = snprintf(buf + *used, n - *used, "%s\"%s\":%s", comma ? "," : "", k, num);
    if (r > 0) *used += (size_t)r;
}

size_t missionRecJson(char *buf, size_t n, int type, const uint8_t *rec)
{
    const MissionField *f;
    int nf = missionRecFields(type, &f);
    const char *group = missionRecGroup(type);
    size_t used = 0;
    int ngrouped = 0;
    char num[40];
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < nf; i++) {
            if (f[i].grouped != pass) continue;
            if (f[i].kind == MF_FLOAT) missionFloatText(num, sizeof num, (float)missionFieldGet(&f[i], rec));
            else if (f[i].kind == MF_FIXED) {
                const double d = missionFieldGet(&f[i], rec);
                snprintf(num, sizeof num, "%.15g", d);
                if (strtod(num, NULL) != d) snprintf(num, sizeof num, "%.17g", d);
            }
            else snprintf(num, sizeof num, "%.0f", missionFieldGet(&f[i], rec));
            if (pass == 1 && !ngrouped) {
                if (used < n) { int r = snprintf(buf + used, n - used, ",\"%s\":{", group); if (r > 0) used += (size_t)r; }
            }
            catf(buf, n, &used, f[i].name, num, pass == 0 || ngrouped);
            if (pass == 1) ngrouped++;
        }
        if (pass == 1 && ngrouped && used < n) { buf[used++] = '}'; }
    }
    if (n) buf[used < n ? used : n - 1] = 0;
    return used;
}
