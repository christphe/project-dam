/*
 * ge007-audio-export <rom.z64> <ntsc-final|pal-final|jpn-final> <out_dir>
 *
 * Writes out_dir/music/track_NN.mid, out_dir/instruments.sf2, out_dir/sfx/sfx_NNNN.wav
 * (N = effect number), out_dir/sfx/sfx.json and out_dir/report.txt. Exit status 0 = no errors.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define MKDIR(p) mkdir((p), 0777)
#endif

#include "ge_audio_export.h"


static int g_errors;

static int make_dir(const char *path)
{
    if (MKDIR(path) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *p;
    long n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return NULL;
    }
    p = (uint8_t *)malloc((size_t)n ? (size_t)n : 1);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return p;
}

int main(int argc, char **argv)
{
    ge_ae_region region;
    const ge_ae_layout *lay;
    uint8_t *rom;
    size_t size = 0;
    int ntracks = 0;
    char err[GE_AE_ERRLEN], path[1024];
    FILE *rep;

    if (argc != 4) {
        fprintf(stderr, "usage: %s <rom.z64> <ntsc-final|pal-final|jpn-final> <out_dir>\n", argv[0]);
        return 2;
    }
    if (ge_ae_region_from_name(argv[2], &region)) {
        fprintf(stderr, "unknown region '%s' (ntsc-final, pal-final or jpn-final)\n", argv[2]);
        return 2;
    }
    lay = ge_ae_layout_for(region);
    rom = read_file(argv[1], &size);
    if (!rom) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    if (ge_ae_check_rom(rom, size, lay, &ntracks, err)) {
        fprintf(stderr, "%s: %s: %s\n", argv[1], lay->name, err);
        free(rom);
        return 1;
    }
    if (make_dir(argv[3])) {
        fprintf(stderr, "cannot create %s: %s\n", argv[3], strerror(errno));
        free(rom);
        return 1;
    }
    snprintf(path, sizeof path, "%s/report.txt", argv[3]);
    rep = fopen(path, "w");
    if (!rep) {
        fprintf(stderr, "cannot write %s\n", path);
        free(rom);
        return 1;
    }
    fprintf(rep, "ge007-audio-export region=%s rom_bytes=%zu\n\n", lay->name, size);

    g_errors = ge_ae_export_dirs(rom, size, lay, ntracks, argv[3], argv[3], argv[3], rep);

    fprintf(rep, "total errors: %d\n", g_errors);
    fclose(rep);
    free(rom);
    printf("%s: %s, %d error(s); report: %s/report.txt\n", argv[1], lay->name, g_errors, argv[3]);
    return g_errors ? 1 : 0;
}
