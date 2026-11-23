/* level.rec (issue #102): the record layout the exporter (port/levelrec/gfx_levelcap.inc)
 * writes and the editor reads, and, when GE_LEVELREC_DIR names an export
 * (`ge007 -level_XX --export-level DIR`), a structural check of that real file.
 * Without the variable the file check is skipped (an export is ROM-derived and
 * never in the tree). */
#include "ge_test.h"
#include "levelrec.h"

#include <stdlib.h>
#include <string.h>

static void test_layout(void)
{
    /* tools_pc/levelrec_check.py unpacks exactly these sizes */
    GE_CHECK_EQ_INT(sizeof(LevelRecHeader), 116);
    GE_CHECK_EQ_INT(sizeof(LevelRecTexture), 12);
    GE_CHECK_EQ_INT(sizeof(LevelRecOwner), 40);
    GE_CHECK_EQ_INT(sizeof(LevelRecRun), 464);
    GE_CHECK_EQ_INT(LEVELREC_MAX_MTX, 32);
    GE_CHECK_EQ_INT(LEVELREC_MAX_INPUTS, 8);
}

static void test_file(void)
{
    const char *dir = getenv("GE_LEVELREC_DIR");
    if (dir == NULL || dir[0] == 0) {
        printf("test_levelrec_file: GE_LEVELREC_DIR not set, real-file check skipped\n");
        return;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/level.rec", dir);
    LevelRec lr;
    GE_CHECK(levelrecLoadFile(&lr, path));
    if (lr.hdr == NULL) return;
    GE_CHECK(lr.hdr->nruns > 0);
    GE_CHECK(lr.hdr->nowners > 0);
    for (uint32_t i = 0; i < lr.hdr->nruns; i++) {
        const LevelRecRun *r = lr.runs[i].r;
        GE_CHECK(r->nverts > 0 && r->nverts % 3 == 0);
        GE_CHECK(r->nfloats % r->nverts == 0);
        const uint32_t stride = r->nverts ? r->nfloats / r->nverts : 0;
        GE_CHECK(stride >= 4);
        for (int t = 0; t < 2; t++) GE_CHECK(r->tex[t] >= -1 && r->tex[t] < (int32_t)lr.hdr->ntextures);
        for (uint32_t v = 0; v < r->nverts && stride >= 4; v++) {
            const float slot = lr.runs[i].verts[(size_t)v * stride + 3];
            if (!(slot >= 0.0f && slot < (float)r->nmtx)) { GE_CHECK(slot >= 0.0f && slot < (float)r->nmtx); break; }
        }
    }
    levelrecFree(&lr);
}

int main(void)
{
    GE_TEST_BEGIN("level.rec layout and export file");
    test_layout();
    test_file();
    return GE_TEST_END();
}
