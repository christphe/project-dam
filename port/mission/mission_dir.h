#ifndef PORT_MISSION_DIR_H
#define PORT_MISSION_DIR_H
/* A directory's entry names, in the clean ge_mission library (devkitA64's dirent.h pulls <stdbool.h>, which the
 * game's own `bool` in src/bondtypes.h cannot share a translation unit with). */
#ifdef __cplusplus
extern "C" {
#endif
/* Up to `max` names (not "." / ".." / hidden) of `dir` into names[i][64]; returns the count, 0 if the dir is missing. */
int missionListDir(const char *dir, char (*names)[64], int max);
/* mkdir, best effort (an existing dir is fine). */
void missionMakeDir(const char *dir);
#ifdef __cplusplus
}
#endif
#endif
