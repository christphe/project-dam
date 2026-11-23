#include "mission_dir.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

int missionListDir(const char *dir, char (*names)[64], int max)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;
    if (!d) return 0;
    while ((e = readdir(d)) != NULL && n < max) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= 64) continue;
        snprintf(names[n++], 64, "%s", e->d_name);
    }
    closedir(d);
    return n;
}

void missionMakeDir(const char *dir)
{
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
}

int missionEepPath(const char *id, char *out, size_t n)
{
    size_t len = id ? strlen(id) : 0;
    if (n) out[0] = 0;
    if (len < 8 || len > 63) return 0;
    for (size_t i = 0; i < len; i++) {
        char c = id[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == '-')) return 0;
    }
    return snprintf(out, n, "$S/mods/%s.eep", id) < (int)n;
}
