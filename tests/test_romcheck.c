/* The retail ROM check (port/convert/conv_romcheck.c): SHA-1 vectors, byte-swap detection, unknown images. Pure. */
#include "ge_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../port/convert/conv_romcheck.h"

static void hexof(const unsigned char *data, unsigned long len, char out[41])
{
    unsigned char s[20];
    int i;
    cv_sha1(data, len, s);
    for (i = 0; i < 20; i++) sprintf(out + i * 2, "%02x", s[i]);
}

int main(void)
{
    GE_TEST_BEGIN("romcheck");
    char h[41];
    const char *region = NULL;

    hexof((const unsigned char *)"", 0, h);
    GE_CHECK(strcmp(h, "da39a3ee5e6b4b0d3255bfef95601890afd80709") == 0);
    hexof((const unsigned char *)"abc", 3, h);
    GE_CHECK(strcmp(h, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);
    {
        const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        hexof((const unsigned char *)m, (unsigned long)strlen(m), h);
        GE_CHECK(strcmp(h, "84983e441c3bd26ebaae4aa1f95129e5e54670f1") == 0);
    }
    {
        unsigned char *big = (unsigned char *)malloc(1000000);
        GE_CHECK(big != NULL);
        memset(big, 'a', 1000000);
        hexof(big, 1000000, h);
        GE_CHECK(strcmp(h, "34aa973cd4c4daa4f61eeb2bdbad27316534016f") == 0);
        free(big);
    }

    {
        unsigned char v64[64] = { 0x37, 0x80, 0x40, 0x12 };
        unsigned char n64[64] = { 0x40, 0x12, 0x37, 0x80 };
        unsigned char z64[64] = { 0x80, 0x37, 0x12, 0x40 };
        GE_CHECK_EQ_INT(cv_romcheck(v64, 64, &region), CV_ROM_BYTESWAPPED_V64);
        GE_CHECK_EQ_INT(cv_romcheck(n64, 64, &region), CV_ROM_BYTESWAPPED_N64);
        GE_CHECK_EQ_INT(cv_romcheck(z64, 64, &region), CV_ROM_UNKNOWN);
        GE_CHECK_EQ_INT(cv_romcheck(z64, 2, &region), CV_ROM_UNKNOWN);
    }

    GE_CHECK(cv_romcheck_message(CV_ROM_BYTESWAPPED_V64) != NULL);
    GE_CHECK(cv_romcheck_message(CV_ROM_BYTESWAPPED_N64) != NULL);
    GE_CHECK(cv_romcheck_message(CV_ROM_UNKNOWN) != NULL);
    GE_CHECK(strstr(cv_romcheck_message(CV_ROM_BYTESWAPPED_V64), ".z64") != NULL);

    return GE_TEST_END();
}
