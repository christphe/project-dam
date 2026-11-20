#ifndef PORT_NXUPDATE_H
#define PORT_NXUPDATE_H

/*
 * Switch only: the network and battery calls behind the in-game update
 * (port/src/updatecheck.c). libcurl + mbedtls over the console's sockets.
 * Every function is blocking and meant for a background thread.
 */
#include <stddef.h>

/* HTTPS GET into buf (NUL-terminated, at most cap-1 bytes). 1 on success. */
int nxHttpGet(const char *url, char *buf, size_t cap);

/* HTTPS download of url to path. *percent is updated while it runs. On
 * success sha256_hex (65 bytes) holds the lowercase hash of what was written.
 * 1 on success; on failure the partial file is removed. */
int nxHttpDownload(const char *url, const char *path, volatile int *percent, char *sha256_hex);

/* On quit: nxNetAbort makes a running transfer stop at its next progress
 * tick; nxNetShutdown (once the threads are joined) releases curl and the
 * sockets we opened. */
void nxNetAbort(void);
void nxNetShutdown(void);

/* 1 if the console reports an internet connection (nifm). No network
 * traffic: a cheap check before any request. */
int nxOnline(void);

/* 1 if it is safe to write to the SD card: charging, or at least min_percent
 * of battery. *percent gets the level (-1 if unknown). */
int nxBatteryOk(int min_percent, int *percent);

#endif
