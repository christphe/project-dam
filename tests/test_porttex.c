/* The port's texture registry (port/src/porttex.c): register, find, replace in place, versions, external
 * buffers, release, limits. Pure. */
#include "ge_test.h"
#include "../port/include/porttex.h"

int main(void)
{
    GE_TEST_BEGIN("porttex");
    portTexReset();

    unsigned char px[2 * 2 * 4];
    for (int i = 0; i < 16; i++) px[i] = (unsigned char)i;

    /* register a copy, find it, read it back */
    int a = portTexRegister("impact/stone/0/color", 2, 2, px, PORTTEX_COPY);
    GE_CHECK(a >= 0);
    GE_CHECK_EQ_INT(portTexFind("impact/stone/0/color"), a);
    GE_CHECK_EQ_INT(portTexFind("impact/stone/1/color"), -1);
    const PortTex *t = portTexGet(a);
    GE_CHECK(t != NULL);
    GE_CHECK_EQ_INT(t->w, 2);
    GE_CHECK_EQ_INT(t->h, 2);
    GE_CHECK(t->rgba != px);            /* a copy */
    GE_CHECK_EQ_INT(t->rgba[5], 5);
    px[5] = 99;
    GE_CHECK_EQ_INT(portTexGet(a)->rgba[5], 5);   /* the copy does not follow the caller's buffer */

    /* the same name replaces in place: same id, a new version, the new size */
    const unsigned int v1 = portTexGet(a)->version;
    unsigned char big[4 * 4 * 4] = { 0 };
    GE_CHECK_EQ_INT(portTexRegister("impact/stone/0/color", 4, 4, big, PORTTEX_COPY), a);
    GE_CHECK(portTexGet(a)->version != v1);
    GE_CHECK_EQ_INT(portTexGet(a)->w, 4);

    /* external: the registry reads the caller's buffer; a touch makes a new version */
    int b = portTexRegister("impact/cluster/0/color", 2, 2, px, PORTTEX_EXTERNAL);
    GE_CHECK(b >= 0 && b != a);
    GE_CHECK(portTexGet(b)->rgba == px);
    const unsigned int v2 = portTexGet(b)->version;
    px[0] = 7;
    portTexTouch(b);
    GE_CHECK(portTexGet(b)->version != v2);
    GE_CHECK_EQ_INT(portTexGet(b)->rgba[0], 7);

    /* release frees the name and the id */
    portTexRelease(b);
    GE_CHECK(portTexGet(b) == NULL);
    GE_CHECK_EQ_INT(portTexFind("impact/cluster/0/color"), -1);
    GE_CHECK_EQ_INT(portTexRegister("impact/cluster/0/color", 2, 2, px, PORTTEX_EXTERNAL), b);   /* the free id is reused */

    /* bad arguments */
    GE_CHECK_EQ_INT(portTexRegister(NULL, 2, 2, px, 0), -1);
    GE_CHECK_EQ_INT(portTexRegister("", 2, 2, px, 0), -1);
    GE_CHECK_EQ_INT(portTexRegister("x", 0, 2, px, 0), -1);
    GE_CHECK_EQ_INT(portTexRegister("x", 2, 2, NULL, 0), -1);
    GE_CHECK(portTexGet(-1) == NULL);
    GE_CHECK(portTexGet(PORTTEX_MAX) == NULL);
    portTexTouch(-1);     /* harmless */
    portTexRelease(-1);   /* harmless */

    /* a full registry */
    portTexReset();
    char name[16];
    for (int i = 0; i < PORTTEX_MAX; i++) {
        name[0] = 't'; name[1] = (char)('a' + i / 26 % 26); name[2] = (char)('a' + i % 26); name[3] = (char)('0' + i / 676); name[4] = 0;
        GE_CHECK(portTexRegister(name, 2, 2, px, PORTTEX_EXTERNAL) >= 0);
    }
    GE_CHECK_EQ_INT(portTexRegister("one-more", 2, 2, px, PORTTEX_EXTERNAL), -1);
    portTexReset();

    return GE_TEST_END();
}
