/* port/src/labels.c: parse a small ge007-text/1 fixture, look ids up, a missing id, the Japanese glyph escapes, the stack-backed
 * reader with the asset stack stubbed. Pure, no core. */
#include "ge_test.h"
#include "labels.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ---- stubs for what labels.c links against ---- */
static const char *g_fixture;
static int g_reads;
static const char *g_packs = "";
int assetRead(const char *rel, unsigned char **out, unsigned long *len, int *from)
{
    (void)from;
    g_reads++;
    if (!g_fixture || strcmp(rel, "labels/text_en.json")) return 0;
    *len = (unsigned long)strlen(g_fixture);
    *out = (unsigned char *)malloc(*len + 1);
    memcpy(*out, g_fixture, *len + 1);
    return 1;
}
const char *texpackPacks(void) { return g_packs; }
void pkgFree(void *p) { free(p); }
void sysLogPrintf(int level, const char *fmt, ...) { (void)level; (void)fmt; }

static const char kEn[] =
    "{\n\"format\":\"ge007-text/1\",\n\"banks\":[\n{\"bank\":38,\"name\":\"LGUN\",\"slots\":224,\"strings\":2}\n],\n\"strings\":{\n"
    "\"9840\":{\"bank\":\"LGUN\",\"index\":64,\"text\":\"Hunting Knife\\n\"},\n"
    "\"986B\":{\"bank\":\"LGUN\",\"index\":107,\"text\":\"Say \\\"hi\\\" {x} \\\\ \\u0001\"},\n"
    "\"0800\":{\"bank\":\"LARCH\",\"index\":0,\"text\":\"{j:350}{jw:5}A{x:E9}\\n\"}\n}\n}\n";

int main(void)
{
    Labels L;
    const unsigned char *s;
    GE_TEST_BEGIN("labels");

    GE_CHECK(labelsParse(&L, kEn, strlen(kEn)));
    GE_CHECK_EQ_INT(L.count, 3);
    s = labelsLookup(&L, 0x9840);
    GE_CHECK(s && !strcmp((const char *)s, "Hunting Knife\n"));
    s = labelsLookup(&L, 0x986B);
    GE_CHECK(s && !strcmp((const char *)s, "Say \"hi\" {x} \\ \x01"));
    /* narrow 350 = 0x15E -> 0x82 0xDE; wide 5 = 0x2005 -> 0xC0 0x85; then ASCII, a raw byte, newline */
    s = labelsLookup(&L, 0x0800);
    GE_CHECK(s && s[0] == 0x82 && s[1] == 0xDE && s[2] == 0xC0 && s[3] == 0x85 && s[4] == 'A' && s[5] == 0xE9 && s[6] == '\n' && s[7] == 0);
    GE_CHECK(labelsLookup(&L, 0x9841) == NULL);        /* an id nobody provides */
    GE_CHECK(labelsLookup(&L, 0xFFFFF) == NULL);       /* out of range */
    labelsFree(&L);

    GE_CHECK(!labelsParse(&L, "{\"strings\":{\"98\":", 17));   /* malformed */
    GE_CHECK(labelsParse(&L, "{\"strings\":{}}", 14));
    GE_CHECK_EQ_INT(L.count, 0);
    labelsFree(&L);

    /* the stack-backed reader */
    labelsSetLanguage("labels/text_en.json", NULL);
    g_fixture = NULL;
    GE_CHECK(!labelsActive());                          /* no pack has the file: the ROM path */
    GE_CHECK(labelsGet(0x9840) == NULL);
    g_fixture = kEn;
    g_packs = "ge007.passets";                          /* the pack list changed: read again */
    GE_CHECK(labelsActive());
    s = labelsGet(0x9840);
    GE_CHECK(s && !strcmp((const char *)s, "Hunting Knife\n"));
    GE_CHECK(labelsGet(0x9841) == NULL);
    s = labelsMissing(0x9841);
    GE_CHECK(s && !strcmp((const char *)s, "[0x9841]"));
    { int r = g_reads; labelsGet(0x9840); labelsGet(0x986B); GE_CHECK_EQ_INT(g_reads, r); }   /* read once */

    return GE_TEST_END();
}
