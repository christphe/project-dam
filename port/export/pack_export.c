/* pack_export.c -- the base asset pack ge007.passets (docs/specs/asset-packs.md, "The base pack layout").
 *
 * Every exporter writes into one folder, <pack>.tmp/, laid out as the pack: the textures at its root (pack.json,
 * textures.json, textures/), then levels/, models/, characters/, labels/, music/ and sounds/. Every glTF sits two folders
 * under the root and points at ../../textures/<n>.png. The folder is then packed into an uncompressed ustar tar (ge_pkg,
 * read lazily by the game and the tools) and renamed into place, so a killed run never leaves half a pack.
 * CLEAN ROOM like the rest of port/export/.
 */
#include "export.h"
#include "export_common.h"
#include "ge_pkg.h"
#include "audio/ge_audio_export.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define RMDIR(p) _rmdir(p)
#else
#include <unistd.h>
#define RMDIR(p) rmdir(p)
#endif

/* Removes a file or a whole folder tree; quiet when it does not exist. */
static void rm_tree(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return;
    if ((st.st_mode & S_IFMT) == S_IFDIR) {
        DIR *d = opendir(path);
        struct dirent *e;
        while (d && (e = readdir(d)) != NULL) {
            char sub[1400];
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
            rm_tree(sub);
        }
        if (d) closedir(d);
        RMDIR(path);
    } else {
        remove(path);
    }
}

static int export_audio(const char *rom_path, const char *root, ExLogFn log, void *user)
{
    uint8_t *rom = NULL;
    size_t rom_size = 0;
    const char *region = NULL;
    const CvRegionData *rd = NULL;
    ge_ae_region r;
    const ge_ae_layout *lay;
    int ntracks = 0, errors;
    char err[GE_AE_ERRLEN], music[1300], sounds[1300], rep_path[1400];
    FILE *rep;

    if (ex_open_rom(rom_path, log, user, &rom, &rom_size, &region, &rd)) return 1;
    if (ge_ae_region_from_name(region, &r)) { ex_logf(log, user, "audio: no layout for region %s", region); free(rom); return 1; }
    lay = ge_ae_layout_for(r);
    if (ge_ae_check_rom(rom, rom_size, lay, &ntracks, err)) { ex_logf(log, user, "audio: %s", err); free(rom); return 1; }
    snprintf(music, sizeof music, "%s/music", root);
    snprintf(sounds, sizeof sounds, "%s/sounds", root);
    if (ex_mkdir_p(music) || ex_mkdir_p(sounds)) { ex_logf(log, user, "audio: cannot create %s", sounds); free(rom); return 1; }
    snprintf(rep_path, sizeof rep_path, "%s/report.txt", sounds);
    rep = fopen(rep_path, "w");
    /* music/music/track_NN.mid would repeat the name: the tracks go to <root>/music/ (export_music adds "music") */
    errors = ge_ae_export_dirs(rom, rom_size, lay, ntracks, root, music, sounds, rep);
    if (rep) fclose(rep);
    ex_logf(log, user, "audio: %d tracks, instruments and sound effects, %d error(s) -> %s, %s", ntracks, errors, music, sounds);
    free(rom);
    return errors ? 1 : 0;
}

int ex_export_pack(const char *rom_path, const char *pack_path, int as_folder, ExLogFn log, void *user)
{
    char tmp[1300], tar[1300], dir[1400], err[300] = "";
    int failed = 0;

    snprintf(tmp, sizeof tmp, "%s.tmp", pack_path);
    snprintf(tar, sizeof tar, "%s.tartmp", pack_path);
    rm_tree(tmp);   /* a killed earlier run */
    rm_tree(tar);
    if (ex_export_textures(rom_path, tmp, log, user)) { rm_tree(tmp); return -1; }   /* pack.json + textures */
    snprintf(dir, sizeof dir, "%s/levels", tmp);
    failed += ex_export_levels(rom_path, dir, log, user);
    snprintf(dir, sizeof dir, "%s/models", tmp);
    failed += ex_export_models(rom_path, dir, log, user);
    snprintf(dir, sizeof dir, "%s/characters", tmp);
    failed += ex_export_characters(rom_path, dir, log, user);
    snprintf(dir, sizeof dir, "%s/labels", tmp);
    failed += ex_export_text(rom_path, dir, log, user);
    failed += export_audio(rom_path, tmp, log, user);

    if (as_folder) {
        rm_tree(pack_path);
        if (rename(tmp, pack_path)) { ex_logf(log, user, "asset pack: cannot rename %s to %s", tmp, pack_path); rm_tree(tmp); return -1; }
    } else {
        if (!pkgWriteDir(tmp, tar, err, sizeof err)) {
            ex_logf(log, user, "asset pack: cannot write %s: %s", tar, err);
            rm_tree(tmp); rm_tree(tar);
            return -1;
        }
        rm_tree(tmp);
        rm_tree(pack_path);   /* rename does not replace on Windows */
        if (rename(tar, pack_path)) { ex_logf(log, user, "asset pack: cannot rename %s to %s", tar, pack_path); rm_tree(tar); return -1; }
    }
    ex_logf(log, user, "asset pack: %s written, %d part(s) with failures", pack_path, failed);
    return failed;
}

int ex_pack_version(const char *pack_path)
{
    char path[1300];
    uint8_t *buf = NULL;
    size_t n = 0;
    const char *p;
    int v = -1;
    snprintf(path, sizeof path, "%s/pack.json", pack_path);   /* a folder or a tar: ge_pkg reads through either */
    if (!pkgReadFile(path, &buf, &n)) return -1;
    p = strstr((const char *)buf, "\"exporter_version\"");
    if (p && (p = strchr(p + 18, ':')) != NULL) {
        p++;
        while (*p == ' ' || *p == '\t') p++;
        if (*p >= '0' && *p <= '9') v = atoi(p);
    }
    pkgFree(buf);
    return v;
}

int ex_ensure_base_asset_pack(const char *rom_path, const char *pack_path, const char *old_texture_pack, ExLogFn log, void *user)
{
    if (ex_pack_version(pack_path) == EX_EXPORTER_VERSION) return 1;
    if (ex_export_pack(rom_path, pack_path, 0, log, user) < 0) return -1;
    if (old_texture_pack && old_texture_pack[0]) {
        struct stat st;
        if (stat(old_texture_pack, &st) == 0) {
            rm_tree(old_texture_pack);
            ex_logf(log, user, "asset pack: the older base texture pack %s is removed", old_texture_pack);
        }
    }
    return 0;
}
