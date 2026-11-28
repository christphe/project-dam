/* labels.c -- see port/include/labels.h. Format: docs/specs/text.md ("The export"); decoding of the glyph escapes from the same
 * page (code = (b0 & 0x7F) << 7 | (b1 & 0x7F), wide when code & 0x2000, bit 7 set on both bytes in bank text). */
#include "labels.h"
#include "texpack.h"
#include "ge_pkg.h"
#include "system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef LABELS_PURE_TEST
#include "pc_protos.h"   /* calloc/free/realloc: the N64 stdlib.h stub declares none (D324) */
#endif

static int put(Labels *L, int c)
{
    if (L->len + 1 > L->cap) {
        size_t nc = L->cap ? L->cap * 2 : 65536;
        unsigned char *p = (unsigned char *)realloc(L->arena, nc);
        if (!p) return 0;
        L->arena = p; L->cap = nc;
    }
    L->arena[L->len++] = (unsigned char)c;
    return 1;
}

static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decodes the JSON string body starting after the opening quote; returns the position after the closing quote, NULL on error. */
static const char *decodeText(Labels *L, const char *p, const char *end)
{
    while (p < end && *p != '"') {
        unsigned char c = (unsigned char)*p;
        if (c == '\\') {
            if (++p >= end) return NULL;
            switch (*p) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'u': {
                unsigned v = 0;
                if (end - p < 5) return NULL;
                for (int i = 1; i <= 4; i++) { int h = hexv(p[i]); if (h < 0) return NULL; v = v * 16 + (unsigned)h; }
                p += 4;
                if (v < 0x80) c = (unsigned char)v;
                else if (v < 0x800) { if (!put(L, 0xC0 | (v >> 6))) return NULL; c = (unsigned char)(0x80 | (v & 0x3F)); }
                else { if (!put(L, 0xE0 | (v >> 12)) || !put(L, 0x80 | ((v >> 6) & 0x3F))) return NULL; c = (unsigned char)(0x80 | (v & 0x3F)); }
                break; }
            default: c = (unsigned char)*p; break;   /* \" \\ \/ */
            }
            if (!put(L, c)) return NULL;
            p++;
        } else if (c == '{' && p + 3 < end && (p[1] == 'j' || p[1] == 'x') && (p[2] == ':' || (p[2] == 'w' && p[3] == ':'))) {
            /* {j:N} narrow glyph, {jw:N} wide glyph (decimal), {x:HH} a raw byte */
            int x = p[1] == 'x', wide = p[2] == 'w';
            const char *q = p + (wide ? 4 : 3);
            unsigned n = 0;
            int digits = 0;
            while (q < end && ((!x && *q >= '0' && *q <= '9') || (x && hexv(*q) >= 0))) {
                n = n * (x ? 16u : 10u) + (unsigned)(x ? hexv(*q) : *q - '0'); q++; digits++;
            }
            if (!digits || q >= end || *q != '}' || n > 0x3FFF) return NULL;
            if (x) { if (!put(L, (int)(n & 0xFF))) return NULL; }
            else {
                unsigned code = wide ? (0x2000u | (n & 0x1FFFu)) : n;
                if (!put(L, (int)(0x80u | ((code >> 7) & 0x7Fu))) || !put(L, (int)(0x80u | (code & 0x7Fu)))) return NULL;
            }
            p = q + 1;
        } else {
            if (!put(L, c)) return NULL;
            p++;
        }
    }
    return p < end ? p + 1 : NULL;
}

int labelsParse(Labels *L, const char *json, size_t len)
{
    const char *p, *end = json + len;
    memset(L, 0, sizeof *L);
    L->off = (uint32_t *)calloc(LABELS_MAX_IDS, sizeof(uint32_t));
    if (!L->off || !put(L, 0)) { labelsFree(L); return 0; }
    {   /* the banks list: {"bank":N,...,"slots":M} before the strings object */
        const char *b = json, *so = strstr(json, "\"strings\":{");
        while ((b = strstr(b, "{\"bank\":")) && (!so || b < so)) {
            int bk = atoi(b + 8);
            const char *sl = strstr(b, "\"slots\":"), *e = strchr(b, '}');
            if (sl && e && sl < e && bk >= 0 && bk < 45) L->slots[bk] = (uint16_t)atoi(sl + 8);
            b += 8;
        }
    }
    /* "strings":{ -- the banks list has "strings":N counts, so the object's brace is part of the key */
    p = strstr(json, "\"strings\":{");
    if (!p) return 1;
    p += 11;
    for (;;) {
        unsigned id = 0;
        const char *t;
        size_t start;
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',')) p++;
        if (p >= end || *p == '}') break;
        if (*p != '"') { labelsFree(L); return 0; }
        p++;
        for (int i = 0; i < 4; i++) { int h = p < end ? hexv(*p) : -1; if (h < 0) { labelsFree(L); return 0; } id = id * 16 + (unsigned)h; p++; }
        if (p >= end || *p != '"' || id >= LABELS_MAX_IDS) { labelsFree(L); return 0; }
        t = strstr(p, "\"text\":\"");
        if (!t) { labelsFree(L); return 0; }
        start = L->len;
        p = decodeText(L, t + 8, end);
        if (!p || !put(L, 0)) { labelsFree(L); return 0; }
        L->off[id] = (uint32_t)start;
        L->count++;
        /* an entry also written with "raw" (hex of the bytes) replaces the decoded text */
        if (p + 8 < end && !strncmp(p, ",\"raw\":\"", 8)) {
            const char *q = p + 8;
            L->len = start;
            while (q + 1 < end && hexv(q[0]) >= 0 && hexv(q[1]) >= 0) { if (!put(L, hexv(q[0]) * 16 + hexv(q[1]))) { labelsFree(L); return 0; } q += 2; }
            if (!put(L, 0)) { labelsFree(L); return 0; }
            p = q + 1;
        }
        p = strchr(p, '}');   /* the end of the entry (the text itself is already behind us) */
        if (!p) { labelsFree(L); return 0; }
        p++;
    }
    return 1;
}

unsigned labelsBankSlots(const Labels *L, int bank)
{
    return (L && bank >= 0 && bank < 45 && L->slots[bank]) ? L->slots[bank] : 1024;
}

const unsigned char *labelsLookup(const Labels *L, unsigned id)
{
    if (!L || !L->off || id >= LABELS_MAX_IDS || !L->off[id]) return NULL;
    return L->arena + L->off[id];
}

void labelsFree(Labels *L)
{
    free(L->arena); free(L->off);
    memset(L, 0, sizeof *L);
}

/* ---- the stack-backed reader ---- */

static Labels g_labels;
static int g_loaded, g_ok;                 /* g_loaded: tried for g_packs; g_ok: the file is in g_labels */
static char g_packs[512];
static const char *g_file, *g_fallback;
static int g_romFlag = -1, g_checkFlag = -1;

static void defaultLanguage(void)
{
    if (g_file) return;
#if defined(LANG_JP)
    g_file = "labels/text_ja.json";
#elif defined(LANG_EU)
    g_file = "labels/text_en-GB.json"; g_fallback = "labels/text_en.json";
#else
    g_file = "labels/text_en.json";
#endif
}

void labelsSetLanguage(const char *file, const char *fallback) { g_file = file; g_fallback = fallback; labelsInvalidate(); }

void labelsInvalidate(void)
{
    labelsFree(&g_labels);   /* a string handed out earlier is valid only until the next load: callers ask again per draw */
    g_loaded = g_ok = 0;
}

static int romFlag(void)
{
    if (g_romFlag < 0) g_romFlag = getenv("GE_ROM_LABELS") != NULL;
    return g_romFlag;
}

int labelsCheckEnabled(void)
{
    if (g_checkFlag < 0) g_checkFlag = getenv("GE_LABELS_CHECK") != NULL;
    return g_checkFlag;
}

static void load(void)
{
    unsigned char *buf = NULL;
    unsigned long n = 0;
    const char *file;
    g_loaded = 1;
    g_ok = 0;
    defaultLanguage();
    file = g_file;
    if (!assetRead(file, &buf, &n, NULL) && g_fallback) { file = g_fallback; assetRead(file, &buf, &n, NULL); }
    if (!buf) {
        sysLogPrintf(LOG_WARNING, "labels: %s is in no asset pack (base pack not written yet?): using the ROM's language banks", g_file);
        return;
    }
    if (labelsParse(&g_labels, (const char *)buf, n)) {
        g_ok = 1;
        sysLogPrintf(LOG_NOTE, "labels: %s: %d strings from the asset packs", file, g_labels.count);
    } else {
        sysLogPrintf(LOG_WARNING, "labels: %s is malformed: using the ROM's language banks", file);
    }
    pkgFree(buf);
}

int labelsActive(void)
{
    const char *packs;
    if (romFlag()) return 0;
    packs = texpackPacks();
    if (!packs) packs = "";
    if (g_loaded && strcmp(packs, g_packs)) labelsInvalidate();
    if (!g_loaded) { snprintf(g_packs, sizeof g_packs, "%s", packs); load(); }
    return g_ok;
}

unsigned labelsGetBankSlots(int bank) { return labelsActive() ? labelsBankSlots(&g_labels, bank) : 1024; }

const unsigned char *labelsGet(unsigned id)
{
    return labelsActive() ? labelsLookup(&g_labels, id) : NULL;
}

const unsigned char *labelsMissing(unsigned id)
{
    static unsigned char ring[8][24];
    static int n, logged;
    static unsigned seen[32];
    unsigned char *b = ring[n++ & 7];
    int known = 0;
    for (int i = 0; i < logged && i < 32; i++) if (seen[i] == id) known = 1;
    if (!known) {
        if (logged < 32) seen[logged] = id;
        if (logged++ < 32) sysLogPrintf(LOG_WARNING, "labels: string 0x%04X (bank %u index %u) is in no asset pack", id, id >> 10, id & 0x3FF);
    }
    snprintf((char *)b, sizeof ring[0], "[0x%04X]", id & 0xFFFF);
    return b;
}
