/* ge007_export_cli.c -- ge007-export: ROM -> the base asset pack ge007.passets (textures, levels, models, characters, labels,
 * music, sounds). */
#include "export.h"

#include <stdio.h>
#include <string.h>

static void say(void *user, const char *line)
{
    (void)user;
    printf("%s\n", line);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int as_folder = argc == 4 && !strcmp(argv[3], "--folder");
    char pack[1100];
    int failed;
    if (argc != 3 && !as_folder) {
        fprintf(stderr, "usage: ge007-export <rom.z64> <out_dir> [--folder]\n"
                        "  Writes <out_dir>/ge007.passets, the base asset pack (docs/specs/asset-packs.md): an uncompressed\n"
                        "  tar holding the textures, levels/, models/, characters/, labels/, music/ and sounds/.\n"
                        "  --folder writes it as a folder <out_dir>/ge007.passets/ instead. Exit code = parts with failures.\n");
        return 255;
    }
    snprintf(pack, sizeof pack, "%s/ge007.passets", argv[2]);
    failed = ex_export_pack(argv[1], pack, as_folder, say, NULL);
    if (failed < 0) return 254;
    return failed > 254 ? 254 : failed;
}
