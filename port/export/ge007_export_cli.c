/* ge007_export_cli.c -- ge007-export: ROM -> every level's decor mesh as glTF 2.0. */
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
                        "  Writes <out_dir>/<level>/<level>.gltf + .bin + textures/<n>.png + report.txt\n"
                        "  for every level. Exit code = number of levels that failed.\n");
        return 255;
    }
    int failed = ex_export_levels(argv[1], argv[2], say, NULL);
    return failed > 254 ? 254 : failed;
}
