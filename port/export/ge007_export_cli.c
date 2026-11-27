/* ge007_export_cli.c -- ge007-export: ROM -> the base texture pack, every level's decor mesh and every object model as glTF 2.0. */
#include "export.h"

#include <stdio.h>

static void say(void *user, const char *line)
{
    (void)user;
    printf("%s\n", line);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: ge007-export <rom.z64> <out_dir>\n"
                        "  Writes <out_dir>/ge007.ptexpack/ (the game's base texture pack), then\n"
                        "  <out_dir>/<level>/<level>.gltf + .bin + report.txt for every level, models/, text/, characters/.\n"
                        "  The glTF files point into the pack. Exit code = number of failures.\n");
        return 255;
    }
    char mdir[1100];
    int failed;
    snprintf(mdir, sizeof mdir, "%s/ge007.ptexpack", argv[2]);
    failed = ex_export_textures(argv[1], mdir, say, NULL);
    failed += ex_export_levels(argv[1], argv[2], say, NULL);
    snprintf(mdir, sizeof mdir, "%s/models", argv[2]);
    failed += ex_export_models(argv[1], mdir, say, NULL);
    snprintf(mdir, sizeof mdir, "%s/text", argv[2]);
    failed += ex_export_text(argv[1], mdir, say, NULL);
    snprintf(mdir, sizeof mdir, "%s/characters", argv[2]);
    failed += ex_export_characters(argv[1], mdir, say, NULL);
    return failed > 254 ? 254 : failed;
}
