#pragma once
/* The game's level environment records (src/game/bgfog.c fog_tables / fog_tables2, cut out at build time by sea_env.cmake and compiled
 * with the game's own headers), as plain values. Only the base records (Id < 100: one level, one player, no cinema / alt variant). */
#ifdef __cplusplus
extern "C" {
#endif
struct SeaEnvRec {
    int id;               /* the stage number (LEVELID_*) */
    int clouds;           /* Sky.Clouds */
    int rgb[3];           /* Sky.Red/Green/Blue */
    float cloudRepeat;    /* Sky.CloudRepeat (the cloud height) */
    float cloud[3];       /* Sky.CloudRed/Green/Blue */
    int isWater;          /* Sky.IsWater */
    float waterRepeat;    /* Sky.WaterRepeat (the sea height) */
    int waterImageId;     /* Sky.WaterImageId: skywaterimages[] */
    float farFog;         /* Visibility.FarFog; 3.4e38 for a record without fog (fog_tables2) */
};
int seaEnvCount(void);
int seaEnvGet(int i, struct SeaEnvRec* out);   /* 0..seaEnvCount()-1; false past the end */
#ifdef __cplusplus
}
#endif
