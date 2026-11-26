/* The game's environment tables (generated sea_env_tables.inc) read as plain values; compiled with the game's headers and defines. */
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "bgfog.h"
#include "sea_env.h"

#include "sea_env_tables.inc"

#define NREC1 ((int)(sizeof s_fog_tables / sizeof *s_fog_tables))
#define NREC2 ((int)(sizeof s_fog_tables2 / sizeof *s_fog_tables2))

static int base(unsigned id) { return id != 0 && id < 100; }

int seaEnvGet(int want, struct SeaEnvRec* o) {
    int n = 0, i, j;
    for (i = 0; i < NREC1; i++) {
        const EnvironmentRecord* r = &s_fog_tables[i];
        if (!base(r->Id)) continue;
        if (n++ != want) continue;
        o->id = (int)r->Id; o->clouds = r->Sky.Clouds; o->rgb[0] = r->Sky.Red; o->rgb[1] = r->Sky.Green; o->rgb[2] = r->Sky.Blue;
        o->cloudRepeat = r->Sky.CloudRepeat; o->cloud[0] = r->Sky.CloudRed; o->cloud[1] = r->Sky.CloudGreen; o->cloud[2] = r->Sky.CloudBlue;
        o->isWater = r->Sky.IsWater; o->waterRepeat = r->Sky.WaterRepeat; o->waterImageId = r->Sky.WaterImageId;
        o->farFog = (float)r->Visibility.FarFog;
        return 1;
    }
    for (i = 0; i < NREC2; i++) {
        const EnvironmentFoglessRecord* r = &s_fog_tables2[i];
        int dup = 0;
        if (!base(r->Id)) continue;
        for (j = 0; j < NREC1; j++) if ((int)s_fog_tables[j].Id == (int)r->Id) dup = 1;
        if (dup) continue;
        if (n++ != want) continue;
        o->id = (int)r->Id; o->clouds = r->Clouds; o->rgb[0] = r->Red; o->rgb[1] = r->Green; o->rgb[2] = r->Blue;
        o->cloudRepeat = r->CloudRepeat; o->cloud[0] = r->CloudRed; o->cloud[1] = r->CloudGreen; o->cloud[2] = r->CloudBlue;
        o->isWater = r->IsWater; o->waterRepeat = r->WaterRepeat; o->waterImageId = r->WaterImageId;
        o->farFog = 3.4e38f;
        return 1;
    }
    return 0;
}

int seaEnvCount(void) {
    struct SeaEnvRec r;
    int n = 0;
    while (seaEnvGet(n, &r)) n++;
    return n;
}
