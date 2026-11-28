#ifndef PORT_LABELS_H
#define PORT_LABELS_H

/*
 * labels.h -- the game's text from the asset-pack stack instead of the ROM's language banks (docs/specs/text.md,
 * docs/specs/asset-packs.md). The file labels/text_<lang>.json (base pack ge007.passets, a mod may override it) is read once
 * per language and pack list and answers a string id (bank << 10 | index) with the bytes the engine draws: ASCII, and for
 * Japanese the two-byte glyph codes (the exporter's {j:N} / {jw:N} escapes turned back into code bytes).
 *
 * GE_ROM_LABELS=1 keeps the old path (the ROM's banks) for A/B runs. GE_LABELS_CHECK=1 compares every bank the game loads with
 * the pack and logs the numbers (dev probe). Game thread only.
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LABELS_MAX_IDS (45 * 1024)   /* banks 0..44, 1024 indexes each */

typedef struct Labels {
    unsigned char *arena;   /* decoded strings, NUL terminated; byte 0 unused so offset 0 = no string */
    size_t len, cap;
    uint32_t *off;          /* LABELS_MAX_IDS entries, 0 = no string */
    int count;              /* strings stored */
} Labels;

/* Parses the "strings" object of a ge007-text/1 file. 1 on success (also when it has no strings), 0 on a malformed file. */
int labelsParse(Labels *L, const char *json, size_t len);
const unsigned char *labelsLookup(const Labels *L, unsigned id);   /* NULL when the id has no string */
void labelsFree(Labels *L);

/* The stack-backed reader. The file is the game's language (US: text_en, EU: text_en-GB then text_en, JP: text_ja); it is read
 * on first use and again when Video.AssetPacks changes or labelsInvalidate() is called (a mod switch). */
void labelsSetLanguage(const char *file, const char *fallback);   /* tests: "labels/text_en.json", NULL */
void labelsInvalidate(void);
/* 1 when strings come from the packs: GE_ROM_LABELS unset and the language file loaded. 0 = use the ROM banks. */
int labelsActive(void);
const unsigned char *labelsGet(unsigned id);   /* NULL when the id has no string (or labels are not active) */
/* A visible placeholder for an id the pack lacks but the ROM has; logs the id once. Valid until 8 more calls. */
const unsigned char *labelsMissing(unsigned id);
int labelsCheckEnabled(void);   /* GE_LABELS_CHECK set */

#ifdef __cplusplus
}
#endif
#endif
