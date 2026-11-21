/*
 * Switch only: the network, hash and battery calls behind the in-game update.
 * See port/include/nxupdate.h and port/src/updatecheck.c.
 *
 * TLS: the portlib libcurl (7.69) is built on mbedtls, which has no system
 * certificate store, so the Mozilla CA bundle of the build image is linked
 * into the NRO (nxcacert.S) and written to data/cacert.pem before the first
 * request. Peer verification stays on.
 */
#ifdef __SWITCH__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <switch.h>
#include <curl/curl.h>
#include <mbedtls/sha256.h>

#include "nxupdate.h"
#include "system.h"

extern const unsigned char nx_cacert_start[];
extern const unsigned char nx_cacert_end[];

#define NX_CA_PATH "data/cacert.pem"

static int s_netUp;
static int s_ownSockets;               /* we opened the sockets (not nxlink stdio) */
static volatile int s_abort;           /* nxNetAbort: the game is quitting */

static int nxNetInit(void)
{
    if (s_netUp) return 1;
#ifndef NX_LINKSTDIO
    /* A test build's nxlink stdio (nxmem.c) already holds the sockets and
     * closes them on exit; a second init fails. */
    if (R_FAILED(socketInitializeDefault())) return 0;
    s_ownSockets = 1;
#endif
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) return 0;
    s_netUp = 1;
    return 1;
}

void nxNetAbort(void) { s_abort = 1; }

void nxNetShutdown(void)
{
    if (s_netUp) curl_global_cleanup();
    if (s_ownSockets) socketExit();
    s_netUp = s_ownSockets = 0;
}

/* Write the embedded CA bundle next to the other data files if it is not
 * already there with the same size. */
static int nxWriteCa(void)
{
    size_t n = (size_t)(nx_cacert_end - nx_cacert_start);
    if (n == 0) return 0;
    FILE *f = fopen(NX_CA_PATH, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long have = ftell(f);
        fclose(f);
        if (have == (long)n) return 1;
    }
    f = fopen(NX_CA_PATH, "wb");
    if (!f) return 0;
    int ok = fwrite(nx_cacert_start, 1, n, f) == n;
    fclose(f);
    return ok;
}

struct Sink {
    char *buf;             /* memory sink, or NULL */
    size_t cap, len;
    FILE *file;            /* file sink, or NULL */
    mbedtls_sha256_context sha;
    int failed;
};

static size_t sinkWrite(char *data, size_t sz, size_t nm, void *user)
{
    struct Sink *s = (struct Sink *)user;
    size_t n = sz * nm;
    if (s->buf) {
        if (s->len + n + 1 > s->cap) { s->failed = 1; return 0; }
        memcpy(s->buf + s->len, data, n);
        s->len += n;
    } else {
        if (fwrite(data, 1, n, s->file) != n) { s->failed = 1; return 0; }
        mbedtls_sha256_update_ret(&s->sha, (const unsigned char *)data, n);
        s->len += n;
    }
    return n;
}

struct Prog { volatile int *percent; };

static int progress(void *user, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    struct Prog *p = (struct Prog *)user;
    (void)ultotal; (void)ulnow;
    if (p->percent && dltotal > 0) *p->percent = (int)(dlnow * 100 / dltotal);
    return s_abort;   /* nonzero: curl stops the transfer (game quitting) */
}

static int nxPerform(const char *url, struct Sink *sink, volatile int *percent, long timeoutSec)
{
    if (!nxNetInit()) { sysLogPrintf(LOG_INFO, "update: network init failed"); return 0; }
    if (!nxWriteCa()) { sysLogPrintf(LOG_INFO, "update: cannot write %s", NX_CA_PATH); return 0; }
    CURL *c = curl_easy_init();
    if (!c) return 0;
    char err[CURL_ERROR_SIZE];
    err[0] = 0;
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
    struct Prog prog = { percent };
    struct curl_slist *hdr = curl_slist_append(NULL, "Accept: application/vnd.github+json");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "project-dam");
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_CAINFO, NX_CA_PATH);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    if (timeoutSec > 0) curl_easy_setopt(c, CURLOPT_TIMEOUT, timeoutSec);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1000L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sinkWrite);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, sink);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &prog);
    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK)
        sysLogPrintf(LOG_INFO, "update: %s: curl %d (%s)", url, (int)rc, err[0] ? err : curl_easy_strerror(rc));
    curl_easy_cleanup(c);
    curl_slist_free_all(hdr);
    return rc == CURLE_OK && !sink->failed;
}

int nxHttpGet(const char *url, char *buf, size_t cap)
{
    struct Sink s;
    memset(&s, 0, sizeof(s));
    s.buf = buf;
    s.cap = cap;
    if (cap) buf[0] = 0;
    if (!nxPerform(url, &s, NULL, 20)) return 0;
    buf[s.len] = 0;
    return s.len > 0;
}

int nxHttpDownload(const char *url, const char *path, volatile int *percent, char *sha256_hex)
{
    struct Sink s;
    memset(&s, 0, sizeof(s));
    s.file = fopen(path, "wb");
    if (!s.file) return 0;
    mbedtls_sha256_init(&s.sha);
    mbedtls_sha256_starts_ret(&s.sha, 0);
    if (percent) *percent = 0;
    int ok = nxPerform(url, &s, percent, 0);
    if (fclose(s.file) != 0) ok = 0;
    unsigned char h[32];
    mbedtls_sha256_finish_ret(&s.sha, h);
    mbedtls_sha256_free(&s.sha);
    if (!ok || s.len == 0) {
        remove(path);
        return 0;
    }
    for (int i = 0; i < 32; i++) snprintf(sha256_hex + i * 2, 3, "%02x", h[i]);
    return 1;
}

int nxOnline(void)
{
    NifmInternetConnectionType type;
    NifmInternetConnectionStatus status = 0;
    u32 strength = 0;
    int online = 0;
    if (R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) {
        /* Fails when the console has no connection at all (airplane mode,
         * no access point). Connected = an access point plus internet. */
        if (R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status)))
            online = status == NifmInternetConnectionStatus_Connected;
        nifmExit();
    }
    return online;
}

int nxBatteryOk(int min_percent, int *percent)
{
    u32 pct = 0;
    PsmChargerType ch = PsmChargerType_Unconnected;
    int known = 0;
    if (R_SUCCEEDED(psmInitialize())) {
        if (R_SUCCEEDED(psmGetBatteryChargePercentage(&pct))) known = 1;
        if (R_FAILED(psmGetChargerType(&ch))) ch = PsmChargerType_Unconnected;
        psmExit();
    }
    if (percent) *percent = known ? (int)pct : -1;
    if (!known) return 0;               /* cannot tell: do not write */
    return ch != PsmChargerType_Unconnected || (int)pct >= min_percent;
}

#endif /* __SWITCH__ */
