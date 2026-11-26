/*
 * bootscreen.c -- the pre-video progress / message screen. See
 * port/include/bootscreen.h.
 *
 * One tiny software canvas (640x360, 32-bit) drawn with an embedded 8x16 bitmap
 * font (bootfont.h) and shown the platform's own way: an SDL window on PC, the
 * libnx framebuffer (pixel-doubled to 1280x720) on Switch. Nothing here touches
 * the game's renderer; bootscreenClose() releases it all before videoInit().
 */

#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "system.h"
#include "bootscreen.h"
#include "bootfont.h"

#define CW 640
#define CH_BASE 360          /* the layout is designed for 640x360 ... */
#define CH_MAX 480           /* ... and centred in a taller canvas (4:3 windows) */
static int g_ch = CH_BASE;   /* canvas height in use */
static int g_oy;             /* y offset of the designed 640x360 area */
#define CH g_ch

#if defined(PLATFORM_SWITCH)
#define PX(r, g, b) ((unsigned)0xFF000000u | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))
#else
#define PX(r, g, b) ((unsigned)0xFF000000u | ((unsigned)(r) << 16) | ((unsigned)(g) << 8) | (unsigned)(b))
#endif

static unsigned g_px[CW * CH_MAX];

/* ------------------------------------------------------------- canvas ---- */

static int bsLerp(int a, int b, int n, int d) { return d ? a + (b - a) * n / d : a; }

static void bsRect(int x, int y, int w, int h, unsigned c)
{
    int i, j;
    y += g_oy;
    for (j = y < 0 ? 0 : y; j < y + h && j < CH; j++)
        for (i = x < 0 ? 0 : x; i < x + w && i < CW; i++)
            g_px[j * CW + i] = c;
}

/* vertical gradient rectangle, colours given as RGB triplets */
static void bsGrad(int x, int y, int w, int h, const int *top, const int *bot)
{
    int j;
    for (j = 0; j < h; j++)
        bsRect(x, y + j, w, 1, PX(bsLerp(top[0], bot[0], j, h - 1),
                                   bsLerp(top[1], bot[1], j, h - 1),
                                   bsLerp(top[2], bot[2], j, h - 1)));
}

static void bsGlyph(int x, int y, int s, unsigned char ch, unsigned c)
{
    int row, col;
    const unsigned char *g;
    if (ch < 32 || ch > 126) return;
    g = kBootFont[ch - 32];
    for (row = 0; row < BOOTFONT_H; row++)
        for (col = 0; col < BOOTFONT_W; col++)
            if (g[row] & (0x80 >> col))
                bsRect(x + col * s, y + row * s, s, s, c);
}

/* Small text (scale < 3) is monospace. Big text is proportional: each glyph
 * advances by its own ink width plus a fixed gap, so a narrow "1" sits next to
 * its "0" and a wide "%" does not touch the digit before it. */
static void bsInk(unsigned char ch, int *l, int *r)
{
    int row, col, lo = BOOTFONT_W, hi = -1;
    if (ch < 33 || ch > 126) { *l = 0; *r = 3; return; }   /* space: 4 columns */
    for (row = 0; row < BOOTFONT_H; row++)
        for (col = 0; col < BOOTFONT_W; col++)
            if (kBootFont[ch - 32][row] & (0x80 >> col)) {
                if (col < lo) lo = col;
                if (col > hi) hi = col;
            }
    if (hi < 0) { lo = 0; hi = 3; }
    *l = lo; *r = hi;
}

static int bsTextW(const char *t, int s)
{
    int w = 0, l, r;
    if (s < 3) return (int)strlen(t) * BOOTFONT_W * s;
    for (; *t; t++) {
        bsInk((unsigned char)*t, &l, &r);
        w += (r - l + 1) * s + (t[1] ? 2 * s : 0);
    }
    return w;
}

static void bsText(int x, int y, int s, const char *t, unsigned c)
{
    int l, r;
    if (s < 3) {
        for (; *t; t++, x += BOOTFONT_W * s) bsGlyph(x, y, s, (unsigned char)*t, c);
        return;
    }
    for (; *t; t++) {
        bsInk((unsigned char)*t, &l, &r);
        bsGlyph(x - l * s, y, s, (unsigned char)*t, c);
        x += (r - l + 1) * s + 2 * s;
    }
}

/* centred, with a dark drop shadow */
static void bsTextC(int y, int s, const char *t, unsigned c)
{
    int x = (CW - bsTextW(t, s)) / 2;
    bsText(x + s, y + s, s, t, PX(0, 0, 0));
    bsText(x, y, s, t, c);
}

/* Backdrop: deep blue-black to warm dark gradient, faint scanlines, a gold rule
 * top and bottom. */
static void bsBackdrop(void)
{
    static const int top[3] = { 6, 9, 20 }, bot[3] = { 26, 17, 8 };
    int y, x;
    g_oy = 0;
    bsGrad(0, 0, CW, CH, top, bot);
    for (y = 0; y < CH; y += 2)
        for (x = 0; x < CW; x++) {
            unsigned c = g_px[y * CW + x];
            g_px[y * CW + x] = PX(((c >> 16) & 0xFF) * 3 / 4, ((c >> 8) & 0xFF) * 3 / 4, (c & 0xFF) * 3 / 4);
        }
    bsRect(0, 14, CW, 2, PX(190, 150, 40));
    bsRect(0, CH - 16, CW, 2, PX(190, 150, 40));
    g_oy = (CH - CH_BASE) / 2;   /* everything after this is the centred layout */
}

static void bsTitle(int y)
{
    /* "PROJECT" pale gold, "DAM" bigger and bright gold on the same baseline */
    const int sa = 4, sb = 7, gap = 24;
    const char *a = "PROJECT";
    const char *b = "DAM";
    int wa = bsTextW(a, sa), wb = bsTextW(b, sb);
    int x = (CW - wa - gap - wb) / 2;
    int ya = y + (sb - sa) * 12;     /* baseline is row 12 of the 16-row cell */
    bsText(x + 3, ya + 3, sa, a, PX(0, 0, 0));
    bsText(x, ya, sa, a, PX(232, 214, 160));
    bsText(x + wa + gap + 4, y + 4, sb, b, PX(0, 0, 0));
    bsText(x + wa + gap, y, sb, b, PX(255, 196, 40));
}

static void bsBar(float frac)
{
    static const int hiT[3] = { 255, 232, 130 }, hiB[3] = { 214, 150, 20 };
    const int x = 70, y = 190, w = 500, h = 28;
    int fw, t;

    bsRect(x - 4, y - 4, w + 8, h + 8, PX(190, 150, 40));     /* frame */
    bsRect(x - 2, y - 2, w + 4, h + 4, PX(8, 8, 12));
    bsRect(x, y, w, h, PX(22, 22, 30));                         /* trough */
    fw = (int)((float)w * frac);
    if (fw > 0) {
        bsGrad(x, y, fw, h, hiT, hiB);
        bsRect(x, y + 3, fw, 2, PX(255, 250, 220));             /* gloss */
        if (fw > 6) bsRect(x + fw - 3, y, 3, h, PX(255, 255, 240)); /* bright leading edge */
    }
    for (t = 1; t < 10; t++)                                    /* 10% notches */
        bsRect(x + w * t / 10, y + h - 6, 2, 6, PX(8, 8, 12));
}

static const char *g_footer = "First launch only - please do not close the game";

static void bsDrawProgress(const char *label, float frac)
{
    char pct[8];
    bsBackdrop();
    bsTitle(26);
    bsTextC(142, 2, "BUILDING THE GAME DATA FROM YOUR ROM", PX(210, 210, 220));
    bsBar(frac);
    snprintf(pct, sizeof(pct), "%d%%", (int)(frac * 100.f));
    bsTextC(232, 4, pct, PX(255, 196, 40));
    if (label && label[0]) {
        char up[64];
        int i;
        for (i = 0; label[i] && i < 63; i++)
            up[i] = (label[i] >= 'a' && label[i] <= 'z') ? (char)(label[i] - 32) : label[i];
        up[i] = 0;
        bsTextC(286, 2, up, PX(150, 150, 165));
    }
    bsTextC(CH_BASE - 46, 1, g_footer, PX(120, 120, 135));
}

/* word-wrapped body text (honours '\n'); returns the next free y */
static int bsWrap(int x, int y, int cols, const char *t, unsigned c)
{
    char line[128];
    int n = 0;
    while (*t) {
        int wl = 0;
        if (*t == '\n') {                       /* paragraph break */
            line[n] = 0; bsText(x, y, 1, line, c); y += 20; n = 0; t++;
            continue;
        }
        if (*t == ' ') { if (n) line[n++] = ' '; t++; continue; }
        while (t[wl] && t[wl] != ' ' && t[wl] != '\n') wl++;
        if (n && n + wl > cols) {                /* word does not fit: new line */
            while (n && line[n - 1] == ' ') n--;
            line[n] = 0; bsText(x, y, 1, line, c); y += 20; n = 0;
        }
        while (wl-- > 0 && n < (int)sizeof(line) - 1) line[n++] = *t++;
    }
    if (n) { line[n] = 0; bsText(x, y, 1, line, c); y += 20; }
    return y;
}

static void bsDrawMessage(const char *title, const char *text, const char *footer)
{
    bsBackdrop();
    bsTextC(46, 3, title ? title : "PROJECT DAM", PX(255, 96, 72));
    bsRect(70, 104, 500, 2, PX(190, 150, 40));
    bsWrap(70, 124, 62, text ? text : "", PX(225, 225, 235));
    if (footer) bsTextC(CH_BASE - 52, 2, footer, PX(255, 196, 40));
}

/* ----------------------------------------------------------- platforms ---- */

#if defined(PLATFORM_SWITCH)

#include <switch.h>

static Framebuffer g_fb;
static int g_open;

static int bsOpen(void)
{
    if (g_open) return 1;
    if (R_FAILED(framebufferCreate(&g_fb, nwindowGetDefault(), 1280, 720, PIXEL_FORMAT_RGBA_8888, 2)))
        return 0;
    framebufferMakeLinear(&g_fb);
    g_open = 1;
    return 1;
}

static void bsPresent(void)
{
    u32 stride, x, y;
    u32 *fb = (u32 *)framebufferBegin(&g_fb, &stride);
    stride /= sizeof(u32);
    for (y = 0; y < CH; y++) {
        u32 *d0 = fb + (y * 2) * stride, *d1 = d0 + stride;
        const unsigned *s = g_px + y * CW;
        for (x = 0; x < CW; x++) {
            d0[x * 2] = d0[x * 2 + 1] = d1[x * 2] = d1[x * 2 + 1] = s[x];
        }
    }
    framebufferEnd(&g_fb);
}

void bootscreenProgress(const char *label, float frac)
{
    static int lastPct = -1;
    int pct;
    if (frac < 0.f) frac = 0.f;
    if (frac > 1.f) frac = 1.f;
    pct = (int)(frac * 100.f);
    if (pct == lastPct && g_open) return;
    lastPct = pct;
    if (!bsOpen()) return;
    bsDrawProgress(label, frac);
    bsPresent();
}

void bootscreenMessage(const char *title, const char *text)
{
    PadState pad;

    sysLogPrintf(LOG_ERROR, "%s: %s", title ? title : "Project Dam", text ? text : "");
    if (bsOpen()) {
        bsDrawMessage(title, text, "Press A to quit");
        bsPresent();
        padConfigureInput(8, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);
        while (appletMainLoop()) {
            padUpdate(&pad);
            if (padGetButtonsDown(&pad) & (HidNpadButton_A | HidNpadButton_Plus))
                break;
            bsPresent();   /* keep presenting: a frame must be queued for the screen to stay up */
        }
    }
    bootscreenClose();
}

void bootscreenDone(void)
{
    PadState pad;
    if (!g_open) return;
    g_footer = "";
    bsDrawProgress("", 1.f);
    bsTextC(CH_BASE - 66, 2, "Press A to start", PX(255, 196, 40));
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & (HidNpadButton_A | HidNpadButton_Plus))
            break;
        bsPresent();
    }
}

void bootscreenClose(void)
{
    if (!g_open) return;
    framebufferClose(&g_fb);
    g_open = 0;
}

#else /* PC */

#include <SDL.h>

/* video.c */
extern void videoBootWindowGeometry(int *w, int *h, int *x, int *y, int *fullscreen);

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex;
static int g_sdlUp;
static int g_failed;   /* no display: log-only from here on */

static int bsOpen(void)
{
    if (g_win) return 1;
    if (g_failed) return 0;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        g_failed = 1;
        sysLogPrintf(LOG_WARNING, "bootscreen: no video (%s); progress goes to the log", SDL_GetError());
        return 0;
    }
    g_sdlUp = 1;
    {
        /* same window as the game's (ge_window_sdl.cpp): the ini's size/position,
         * else a 4:3 window filling ~85% of the desktop; fullscreen if it will be */
        int w, h, x, y, fs;
        Uint32 fl = SDL_WINDOW_SHOWN;
        SDL_DisplayMode dm;
        videoBootWindowGeometry(&w, &h, &x, &y, &fs);
        if (w <= 0 || h <= 0) {
            w = 640; h = 480;
            if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
                int maxw = dm.w * 85 / 100, maxh = dm.h * 85 / 100;
                if (maxw * 3 > maxh * 4) { h = maxh; w = maxh * 4 / 3; }
                else                     { w = maxw; h = maxw * 3 / 4; }
            }
            if (w < 640 || h < 480) { w = 640; h = 480; }
        }
        if (fs) fl |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   /* crisp pixels when scaled up */
        g_win = SDL_CreateWindow("Project Dam",
                                 x >= 0 && y >= 0 ? x : SDL_WINDOWPOS_CENTERED,
                                 x >= 0 && y >= 0 ? y : SDL_WINDOWPOS_CENTERED, w, h, fl);
    }
    if (g_win) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (g_win) {   /* canvas height follows the window's aspect (16:9 .. 4:3), no big bars */
        int ww, wh;
        SDL_GetWindowSize(g_win, &ww, &wh);
        g_ch = ww > 0 ? CW * wh / ww : CH_BASE;
        if (g_ch < CH_BASE) g_ch = CH_BASE;
        if (g_ch > CH_MAX) g_ch = CH_MAX;
    }
    if (g_ren) SDL_RenderSetLogicalSize(g_ren, CW, CH);
    if (g_ren) g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CW, CH);
    if (!g_tex) {
        sysLogPrintf(LOG_WARNING, "bootscreen: window failed (%s)", SDL_GetError());
        g_failed = 1;
        bootscreenClose();
        return 0;
    }
    return 1;
}

void bootscreenProgress(const char *label, float frac)
{
    static int lastPct = -1;
    static Uint32 lastTick;
    int pct;
    SDL_Event ev;

    if (frac < 0.f) frac = 0.f;
    if (frac > 1.f) frac = 1.f;
    pct = (int)(frac * 100.f);
    if (pct == lastPct && g_win) return;
    if (!bsOpen()) {
        if (pct != lastPct) sysLogPrintf(LOG_INFO, "romconvert: %d%% %s", pct, label ? label : "");
        lastPct = pct;
        return;
    }
    /* the converter calls this per file; redraw at most ~30 times a second */
    if (pct != 100 && SDL_GetTicks() - lastTick < 33) return;
    lastTick = SDL_GetTicks();
    lastPct = pct;

    bsDrawProgress(label, frac);
    SDL_UpdateTexture(g_tex, NULL, g_px, CW * (int)sizeof(unsigned));
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);

    /* keep the window responsive ("not responding" ghosting) */
    while (SDL_PollEvent(&ev)) { /* no input handled: it is a few seconds */ }
}

/* shows g_px and waits for a key / click / window close (input from the first
 * 800 ms is ignored: a key typed during the conversion must not skip the screen) */
static void bsWaitKey(void)
{
    SDL_Event ev;
    Uint32 t0 = SDL_GetTicks();
    int wait = 1;
    SDL_UpdateTexture(g_tex, NULL, g_px, CW * (int)sizeof(unsigned));
    while (wait) {
        SDL_RenderClear(g_ren);
        SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
        SDL_RenderPresent(g_ren);
        if (SDL_WaitEventTimeout(&ev, 100) &&
            (ev.type == SDL_QUIT ||
             (SDL_GetTicks() - t0 > 800 && (ev.type == SDL_KEYDOWN || ev.type == SDL_MOUSEBUTTONDOWN)))) {
            sysLogPrintf(LOG_INFO, "bootscreen: dismissed (SDL event 0x%x after %u ms)",
                         (unsigned)ev.type, (unsigned)(SDL_GetTicks() - t0));
            wait = 0;
        }
    }
}

void bootscreenDone(void)
{
    if (!g_win) return;
    g_footer = "";
    bsDrawProgress("", 1.f);
    bsTextC(CH_BASE - 66, 2, "Press any key to start", PX(255, 196, 40));
    bsWaitKey();
}

void bootscreenMessage(const char *title, const char *text)
{
    sysLogPrintf(LOG_ERROR, "%s: %s", title ? title : "Project Dam", text ? text : "");
    if (bsOpen()) {
        bsDrawMessage(title, text, "Press any key to quit");
        bsWaitKey();
        bootscreenClose();
        return;
    }
    /* no window possible: a native box (works without one); the log has it anyway */
    if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title ? title : "Project Dam",
                                 text ? text : "", NULL) != 0)
        sysLogPrintf(LOG_WARNING, "bootscreen: message box failed (%s)", SDL_GetError());
}

void bootscreenClose(void)
{
    if (g_tex) { SDL_DestroyTexture(g_tex); g_tex = NULL; }
    if (g_ren) { SDL_DestroyRenderer(g_ren); g_ren = NULL; }
    if (g_win) { SDL_DestroyWindow(g_win); g_win = NULL; }
    if (g_sdlUp) { SDL_QuitSubSystem(SDL_INIT_VIDEO); g_sdlUp = 0; }
}

#endif
