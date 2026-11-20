/*
 * Parsing helpers for the opt-in update check (see port/src/updatecheck.c).
 * Header-only (static inline) so tools_pc/updatecheck_test.c can exercise
 * them offline without pulling in SDL or the rest of the port.
 */
#ifndef UPDATECHECK_PARSE_H
#define UPDATECHECK_PARSE_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Parse "vMAJOR.MINOR.PATCH" (missing parts = 0, suffix ignored). Project Dam
 * release tags are "project-dam-vMAJOR.MINOR.PATCH"; the prefix is skipped. */
static inline int parseVer(const char *s, int v[3])
{
    v[0] = v[1] = v[2] = 0;
    if (strncmp(s, "project-dam-", 12) == 0) s += 12;
    if (*s == 'v' || *s == 'V') s++;
    if (*s < '0' || *s > '9') return 0;
    for (int i = 0; i < 3; i++) {
        if (*s < '0' || *s > '9') break;
        v[i] = (int)strtol(s, (char **)&s, 10);
        if (*s != '.') break;
        s++;
    }
    return 1;
}

static inline int versionNewer(const char *tag, const char *running)
{
    int a[3], b[3];
    if (!parseVer(tag, a) || !parseVer(running, b)) return 0;
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return 0;
}

/* Minimal scan for "tag_name": "<value>". */
static inline int extractTag(const char *body, char *out, size_t n)
{
    const char *p = strstr(body, "\"tag_name\"");
    if (!p) return 0;
    p += 10;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    /* Tag characters only (no control/ANSI bytes reach the log). */
    while (*p && *p != '"' && i + 1 < n) {
        char c = *p++;
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              c == '.' || c == '_' || c == '-')) return 0;
        out[i++] = c;
    }
    if (*p != '"' || i == 0) return 0;
    out[i] = 0;
    return 1;
}

/* True if the release JSON lists a PC build: some "browser_download_url"
 * value whose file name carries "-windows" or "-linux". The latest release
 * may be Switch-only; the PC build must not announce it. */
static inline int hasPcAsset(const char *body)
{
    static const char key[] = "\"browser_download_url\"";
    const char *p = body;
    while ((p = strstr(p, key)) != NULL) {
        p += sizeof(key) - 1;
        while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p != '"') continue;
        p++;
        const char *end = strchr(p, '"');
        if (!end) return 0;
        const char *slash = p;
        for (const char *q = p; q < end; q++) if (*q == '/') slash = q;
        for (const char *q = slash; q + 1 < end; q++) {
            if (strncmp(q, "-windows", 8) == 0 && q + 8 <= end) return 1;
            if (strncmp(q, "-linux", 6) == 0 && q + 6 <= end) return 1;
        }
        p = end;
    }
    return 0;
}

static inline const char *jsonSkipWs(const char *q)
{
    while (*q == ' ' || (*q >= 9 && *q <= 13)) q++;
    return q;
}

/* Copy the JSON string value of the next "key": "value" at or after p into out
 * (no escapes: GitHub's asset fields have none). Returns the position just
 * past the value, or NULL. */
static inline const char *jsonFindKey(const char *p, const char *key, char *out, size_t n)
{
    size_t kl = strlen(key);
    while ((p = strchr(p, '"')) != NULL) {
        if (strncmp(p + 1, key, kl) == 0 && p[1 + kl] == '"') {
            const char *q = jsonSkipWs(p + 2 + kl);
            if (*q == ':') {
                q = jsonSkipWs(q + 1);
                if (*q == '"') {
                    size_t i = 0;
                    q++;
                    while (*q && *q != '"' && *q != 92 && i + 1 < n) out[i++] = *q++;
                    if (*q != '"') return NULL;
                    out[i] = 0;
                    return q + 1;
                }
            }
        }
        p++;
    }
    return NULL;
}

/* Find the release asset called `name`: its download URL and its sha256
 * (GitHub's "digest": "sha256:<64 hex>"). Returns 1 only if both are there. */
static inline int findAsset(const char *body, const char *name, char *url, size_t urlN, char *sha, size_t shaN)
{
    char val[256], dig[96];
    const char *p = body;
    while ((p = jsonFindKey(p, "name", val, sizeof(val))) != NULL) {
        if (strcmp(val, name) != 0) continue;
        const char *d = jsonFindKey(p, "digest", dig, sizeof(dig));
        if (!d || strncmp(dig, "sha256:", 7) != 0 || strlen(dig + 7) != 64 || shaN < 65) return 0;
        if (!jsonFindKey(d, "browser_download_url", url, urlN)) return 0;
        for (int i = 0; i < 64; i++) {
            char c = dig[7 + i];
            if (c >= 'A' && c <= 'F') c = (char)(c + 32);
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
            sha[i] = c;
        }
        sha[64] = 0;
        return 1;
    }
    return 0;
}

#endif
