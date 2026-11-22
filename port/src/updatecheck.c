/*
 * Opt-in update check. See port/include/updatecheck.h.
 *
 * Off by default (Game.CheckUpdates = 0): the ONLY entry point that can
 * reach the network, updateCheckStart(), returns before creating a thread
 * when the setting is 0. When on: one HTTPS GET per launch, on a detached
 * thread, ~5 s timeout, silent on any failure (one log line). Windows uses
 * WinHTTP, LoadLibrary'd at runtime (never linked); elsewhere `curl` is run
 * through popen() (skipped if missing).
 * No telemetry: the request carries only a fixed User-Agent.
 * Checks this fork's releases (christphe/project-dam-nx) against
 * PDAM_VERSION; a release without a PC asset is ignored.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <SDL.h>
#include "sdl_compat.h"   /* SDL_OpenURL on SDL < 2.0.14 */

#include "platform.h"
#include "system.h"
#include "config.h"
#include "versioninfo.h"
#include "updatecheck.h"
#include "updatecheck_parse.h"
#ifdef __SWITCH__
#include "nxupdate.h"
#endif

#define UC_HOST      "api.github.com"
/* /releases/latest deliberately excludes drafts and pre-releases (maintainer
 * decision): a pre-release never prompts anyone to update. */
#define UC_PATH      "/repos/christphe/project-dam-nx/releases/latest"
#define UC_URL       "https://" UC_HOST UC_PATH
#define UC_RELEASES  "https://github.com/christphe/project-dam-nx/releases"
#define UC_UA        "project-dam"
#define UC_BODY_MAX  (256 * 1024)

#define UC_NRO_ASSET "project-dam.nro"

static char s_assetUrl[512];           /* Switch: the NRO asset of the latest release */
static char s_assetSha[65];
static SDL_Thread *s_checkThread;      /* Switch: joined on quit (updateCheckShutdown) */
static SDL_Thread *s_instThread;
enum { UC_IDLE = 0, UC_CHECKING, UC_UPTODATE, UC_AVAILABLE, UC_FAILED };

static int cfgCheckUpdates = 0;        /* Game.CheckUpdates, default OFF */
static SDL_atomic_t s_state;           /* UC_*; s_tag / s_notes are published before it flips to UC_AVAILABLE */
static char s_tag[32];
static char s_notes[4096];             /* the release text, plain ASCII */
static SDL_SpinLock s_lock;
static int s_prevState = UC_IDLE;

PD_CONSTRUCTOR static void updateCheckConfigInit(void)
{
    configRegisterInt("Game.CheckUpdates", &cfgCheckUpdates, 0, 1);
}

#ifdef _WIN32
/* winhttp.dll is loaded at runtime, only here (after the Game.CheckUpdates
 * gate), so the exe's import table never lists it and nothing networking-
 * related is mapped into the process while the setting is off. */
typedef LPVOID HINT_;
typedef HINT_ (WINAPI *PFN_Open)(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD);
typedef int   (WINAPI *PFN_SetTimeouts)(HINT_, int, int, int, int);
typedef HINT_ (WINAPI *PFN_Connect)(HINT_, LPCWSTR, WORD, DWORD);
typedef HINT_ (WINAPI *PFN_OpenRequest)(HINT_, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR *, DWORD);
typedef int   (WINAPI *PFN_Send)(HINT_, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
typedef int   (WINAPI *PFN_Recv)(HINT_, LPVOID);
typedef int   (WINAPI *PFN_Query)(HINT_, DWORD, LPCWSTR, LPVOID, LPDWORD, LPDWORD);
typedef int   (WINAPI *PFN_Read)(HINT_, LPVOID, DWORD, LPDWORD);
typedef int   (WINAPI *PFN_Close)(HINT_);
#define UC_WH_ACCESS_DEFAULT_PROXY 0
#define UC_WH_FLAG_SECURE          0x00800000
#define UC_WH_QUERY_STATUS_CODE    19
#define UC_WH_QUERY_FLAG_NUMBER    0x20000000
#endif

/* Fetch the response body into buf (NUL-terminated). Returns 1 on success. */
static int fetchLatest(char *buf, size_t cap)
{
    size_t len = 0;
#ifdef _WIN32
    int ok = 0;
    /* System32 only: never a winhttp.dll planted next to the exe (D551 review). */
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif
    HMODULE dll = LoadLibraryExW(L"winhttp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) return 0;
    PFN_Open        pOpen    = (PFN_Open)(void *)GetProcAddress(dll, "WinHttpOpen");
    PFN_SetTimeouts pTimeout = (PFN_SetTimeouts)(void *)GetProcAddress(dll, "WinHttpSetTimeouts");
    PFN_Connect     pConnect = (PFN_Connect)(void *)GetProcAddress(dll, "WinHttpConnect");
    PFN_OpenRequest pOpenReq = (PFN_OpenRequest)(void *)GetProcAddress(dll, "WinHttpOpenRequest");
    PFN_Send        pSend    = (PFN_Send)(void *)GetProcAddress(dll, "WinHttpSendRequest");
    PFN_Recv        pRecv    = (PFN_Recv)(void *)GetProcAddress(dll, "WinHttpReceiveResponse");
    PFN_Query       pQuery   = (PFN_Query)(void *)GetProcAddress(dll, "WinHttpQueryHeaders");
    PFN_Read        pRead    = (PFN_Read)(void *)GetProcAddress(dll, "WinHttpReadData");
    PFN_Close       pClose   = (PFN_Close)(void *)GetProcAddress(dll, "WinHttpCloseHandle");
    if (!pOpen || !pTimeout || !pConnect || !pOpenReq || !pSend || !pRecv ||
        !pQuery || !pRead || !pClose) {
        FreeLibrary(dll);
        return 0;
    }
    HINT_ ses = pOpen(L"" UC_UA, UC_WH_ACCESS_DEFAULT_PROXY, NULL, NULL, 0);
    if (!ses) { FreeLibrary(dll); return 0; }
    /* resolve, connect, send, receive: 5 s each */
    pTimeout(ses, 5000, 5000, 5000, 5000);
    HINT_ con = pConnect(ses, L"" UC_HOST, 443, 0);
    HINT_ req = con ? pOpenReq(con, L"GET", L"" UC_PATH, NULL, NULL, NULL,
                               UC_WH_FLAG_SECURE) : NULL;
    if (req &&
        pSend(req, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L, NULL, 0, 0, 0) &&
        pRecv(req, NULL)) {
        DWORD status = 0, sz = sizeof(status);
        if (pQuery(req, UC_WH_QUERY_STATUS_CODE | UC_WH_QUERY_FLAG_NUMBER, NULL,
                   &status, &sz, NULL) && status == 200) {
            for (;;) {
                DWORD got = 0;
                if (len + 1 >= cap) break;
                if (!pRead(req, buf + len, (DWORD)(cap - 1 - len), &got) || got == 0)
                    break;
                len += got;
            }
            ok = len > 0;
        }
    }
    if (req) pClose(req);
    if (con) pClose(con);
    pClose(ses);
    FreeLibrary(dll);
    buf[len] = 0;
    return ok;
#elif defined(__SWITCH__)
    /* No curl binary or popen on the console: libcurl over the console's sockets. */
    (void)len;
    if (!nxOnline()) {
        sysLogPrintf(LOG_INFO, "update check: console offline, skipped");
        return 0;
    }
    return nxHttpGet(UC_URL, buf, cap);
#else
    FILE *f = popen("curl -q -fsS --proto =https --max-time 5 -A '" UC_UA "' '" UC_URL "' 2>/dev/null", "r");
    if (!f) return 0;
    size_t got;
    while (len + 1 < cap && (got = fread(buf + len, 1, cap - 1 - len, f)) > 0) len += got;
    int full = 0;
    if (len + 1 == cap) {
        /* The body is longer than cap: drain the rest (discarded) so curl
         * can write to EOF and exit cleanly instead of dying on SIGPIPE. */
        full = 1;
        char scratch[4096];
        while (fread(scratch, 1, sizeof(scratch), f) > 0) { }
    }
    int rc = pclose(f);   /* nonzero if curl is missing (127) or failed */
    buf[len] = 0;
    /* A full buffer is success: the tag field lives in the first bytes of
     * the JSON, so a truncated body is still usable. Otherwise curl -f's
     * nonzero rc on an HTTP error (with an empty body) must stand. */
    return len > 0 && (rc == 0 || full);
#endif
}

/* What the last check saw, so the first menu says "update available" at once on the next launch, without the network:
 * the tag, the asset (Switch: its URL and sha256, needed to install), then the release text. */
static const char *seenPath(void) { return sysResolvePath("$S/update.txt"); }

static void seenSave(const char *tag, const char *url, const char *sha, const char *notes)
{
    FILE *f = fopen(seenPath(), "wb");
    if (!f) return;
    fprintf(f, "%s\n%s\n%s\n%s", tag, url, sha, notes);
    fclose(f);
}

static int seenLoad(void)
{
    FILE *f = fopen(seenPath(), "rb");
    if (!f) return 0;
    char tag[32] = "", url[sizeof s_assetUrl] = "", sha[sizeof s_assetSha] = "";
    static char notes[sizeof s_notes];
    size_t n = 0;
    int ok = fgets(tag, sizeof tag, f) && fgets(url, sizeof url, f) && fgets(sha, sizeof sha, f);
    tag[strcspn(tag, "\r\n")] = 0;
    url[strcspn(url, "\r\n")] = 0;
    sha[strcspn(sha, "\r\n")] = 0;
    if (ok) n = fread(notes, 1, sizeof notes - 1, f);
    fclose(f);
    notes[n] = 0;
    if (!ok || !tag[0]) return 0;
    SDL_AtomicLock(&s_lock);
    snprintf(s_tag, sizeof s_tag, "%s", tag);
    snprintf(s_notes, sizeof s_notes, "%s", notes);
    snprintf(s_assetUrl, sizeof s_assetUrl, "%s", url);
    snprintf(s_assetSha, sizeof s_assetSha, "%s", sha);
    SDL_AtomicUnlock(&s_lock);
    return 1;
}

/* the version this build is: a release's number, or "dev" (never offered an update); GE_UPDATE_AS tests the menu */
static const char *runningVersion(void)
{
    const char *e = getenv("GE_UPDATE_AS");
    return (e && e[0]) ? e : PDAM_VERSION;
}

static int updateCheckThread(void *unused)
{
    (void)unused;
    static char body[UC_BODY_MAX];   /* one check at a time (UC_CHECKING guards it) */
    static char notes[sizeof s_notes];
    char tag[32];
    if (fetchLatest(body, UC_BODY_MAX) && extractTag(body, tag, sizeof(tag))) {
        const int testing = getenv("GE_UPDATE_AS") != NULL;   /* the test hook also skips the asset filter */
        int offered = 1;
#ifdef __SWITCH__
        /* The Switch installs the NRO asset, with the sha256 GitHub lists for it. */
        if (!findAsset(body, UC_NRO_ASSET, s_assetUrl, sizeof(s_assetUrl), s_assetSha, sizeof(s_assetSha))) {
            sysLogPrintf(LOG_INFO, "update check: latest %s has no %s asset", tag, UC_NRO_ASSET);
            offered = 0;
        }
#else
        if (!testing && !hasPcAsset(body)) {
            /* The latest release can be Switch-only; nothing to offer here. */
            sysLogPrintf(LOG_INFO, "update check: latest %s has no PC build", tag);
            offered = 0;
        }
#endif
        (void)testing;
        if (offered && versionNewer(tag, runningVersion())) {
            notes[0] = 0;
            extractBody(body, notes, sizeof notes);
            SDL_AtomicLock(&s_lock);
            snprintf(s_tag, sizeof(s_tag), "%s", tag);
            snprintf(s_notes, sizeof(s_notes), "%s", notes);
            SDL_AtomicUnlock(&s_lock);
            seenSave(tag, s_assetUrl, s_assetSha, notes);
            SDL_AtomicSet(&s_state, UC_AVAILABLE);   /* full barrier: s_tag visible first */
            sysLogPrintf(LOG_INFO, "update check: %s is available (running %s); %s", tag, runningVersion(), UC_RELEASES);
        } else {
            remove(seenPath());
            SDL_AtomicSet(&s_state, UC_UPTODATE);
            sysLogPrintf(LOG_INFO, "update check: up to date (latest %s, running %s)", tag, runningVersion());
        }
    } else {
        SDL_AtomicSet(&s_state, s_prevState == UC_AVAILABLE ? UC_AVAILABLE : UC_FAILED);
        sysLogPrintf(LOG_INFO, "update check: no result (offline or unavailable)");
    }
    return 0;
}

/* One check now: the menu's button, or the start-up check when Game.CheckUpdates is on. */
void updateCheckRun(void)
{
    if (SDL_AtomicGet(&s_state) == UC_CHECKING) return;
#ifdef __SWITCH__
    /* The last check's thread is joined before the next starts: libnx keeps a thread's stack mapped until it is joined. */
    if (s_checkThread) { SDL_WaitThread(s_checkThread, NULL); s_checkThread = NULL; }
#endif
    s_prevState = SDL_AtomicGet(&s_state);
    SDL_AtomicSet(&s_state, UC_CHECKING);
    SDL_Thread *t = SDL_CreateThread(updateCheckThread, "ge007-updatecheck", NULL);
    if (!t) {
        SDL_AtomicSet(&s_state, s_prevState);
        sysLogPrintf(LOG_INFO, "update check: could not start thread");
        return;
    }
#ifdef __SWITCH__
    /* Joined by updateCheckShutdown: libnx keeps a thread's stack mapped until
     * it is joined, and the loader aborts (0xd401) on a stack left mapped when
     * the game returns to the launcher (D958). */
    s_checkThread = t;
#else
    SDL_DetachThread(t);
#endif
}

void updateCheckStart(void)
{
    /* a release seen on an earlier launch that this build has not caught up with: offered at once, no network */
    if (seenLoad()) {
        if (versionNewer(s_tag, runningVersion())) SDL_AtomicSet(&s_state, UC_AVAILABLE);
        else remove(seenPath());
    }
    if (!cfgCheckUpdates) return;   /* OFF by default: no network until the player asks (the menu's button) */
    updateCheckRun();
}

const char *updateCheckRunning(void) { return runningVersion(); }

int updateCheckState(void) { return SDL_AtomicGet(&s_state); }

const char *updateCheckAvailable(void)
{
    return SDL_AtomicGet(&s_state) == UC_AVAILABLE ? s_tag : NULL;
}

const char *updateCheckNotes(void)
{
    return SDL_AtomicGet(&s_state) == UC_AVAILABLE ? s_notes : "";
}

void updateCheckOpenReleases(void)
{
    if (SDL_OpenURL(UC_RELEASES) != 0)
        sysLogPrintf(LOG_INFO, "update check: could not open browser: %s", SDL_GetError());
}

#ifdef __SWITCH__
/* ---- Switch: in-game install ------------------------------------------
 * Download project-dam.nro to <nro>.new, check its sha256 against GitHub's
 * digest, then replace the running NRO (loaded in memory, so the file can be
 * replaced; the new version starts on the next launch). FAT32 has no atomic
 * replace-over-existing rename: the old file is removed, then .new renamed. */
enum { INST_IDLE, INST_RUN, INST_DONE, INST_FAIL };
static volatile int s_inst = INST_IDLE;
static volatile int s_instPct;
static char s_instMsg[48];             /* written before s_inst changes */
static char s_instPath[512];

static void instFail(const char *why)
{
    snprintf(s_instMsg, sizeof(s_instMsg), "Update failed: %s", why);
    sysLogPrintf(LOG_INFO, "update: %s", s_instMsg);
    s_inst = INST_FAIL;
}

static int updateInstallThread(void *unused)
{
    (void)unused;
    char tmp[sizeof(s_instPath) + 8], sha[65];
    int pct;
    if (!nxOnline()) { instFail("offline"); return 0; }
    if (!nxBatteryOk(5, &pct)) {
        snprintf(s_instMsg, sizeof(s_instMsg), "Battery too low (%d%%): plug in", pct < 0 ? 0 : pct);
        sysLogPrintf(LOG_INFO, "update: %s", s_instMsg);
        s_inst = INST_FAIL;
        return 0;
    }
    snprintf(tmp, sizeof(tmp), "%s.new", s_instPath);
    if (!nxHttpDownload(s_assetUrl, tmp, &s_instPct, sha)) { instFail("download"); return 0; }
    if (strcmp(sha, s_assetSha) != 0) {
        remove(tmp);
        instFail("hash mismatch");
        return 0;
    }
    if (remove(s_instPath) != 0 || rename(tmp, s_instPath) != 0) {
        /* The new file is intact at <nro>.new; the user can rename it by hand. */
        instFail("replace");
        return 0;
    }
    sysLogPrintf(LOG_INFO, "update: %s replaced with %s (sha256 %s)", s_instPath, s_tag, sha);
    s_inst = INST_DONE;
    return 0;
}

void updateInstall(void)
{
    const char *self = sysSelfPath();
    size_t n = self ? strlen(self) : 0;
    static const char tail[] = "/" UC_NRO_ASSET;
    if (SDL_AtomicGet(&s_state) != UC_AVAILABLE || (s_inst != INST_IDLE && s_inst != INST_FAIL)) return;
    /* Only the installed NRO: a test build run over nxlink has another name. */
    if (n < sizeof(tail) || n >= sizeof(s_instPath) || strcmp(self + n - (sizeof(tail) - 1), tail) != 0) {
        instFail("not the installed NRO");
        return;
    }
    snprintf(s_instPath, sizeof(s_instPath), "%s", self);
    if (s_instThread) { SDL_WaitThread(s_instThread, NULL); s_instThread = NULL; }   /* the failed try */
    s_instPct = 0;
    s_inst = INST_RUN;
    s_instThread = SDL_CreateThread(updateInstallThread, "ge007-update", NULL);   /* joined on quit */
    if (!s_instThread) instFail("thread");
}

void updateCheckShutdown(void)
{
    nxNetAbort();   /* a download in progress stops; its partial file is removed */
    if (s_checkThread) { SDL_WaitThread(s_checkThread, NULL); s_checkThread = NULL; }
    if (s_instThread) { SDL_WaitThread(s_instThread, NULL); s_instThread = NULL; }
    nxNetShutdown();
}

int updateInstallIdle(void) { return s_inst == INST_IDLE || s_inst == INST_FAIL; }

const char *updateInstallStatus(void)
{
    static char text[64];
    switch (s_inst) {
    case INST_RUN:  snprintf(text, sizeof(text), "Downloading %d%%", s_instPct); return text;
    case INST_DONE: return "Updated: restart the game";
    case INST_FAIL: return s_instMsg;
    default:        return "Update available: install";
    }
}
#else
void updateInstall(void) { updateCheckOpenReleases(); }
void updateCheckShutdown(void) { (void)s_checkThread; (void)s_instThread; }   /* PC: threads are detached */
int  updateInstallIdle(void) { return 1; }
const char *updateInstallStatus(void) { return "Update available: open page"; }
#endif
