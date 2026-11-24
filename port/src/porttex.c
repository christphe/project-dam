/*
 * porttex.c -- the port's own textures, by name (port/include/porttex.h).
 * Pure: no SDL, no game state (tests/test_porttex.c links it alone).
 */
#include "porttex.h"

#include <stdlib.h>
#include <string.h>

typedef struct Entry {
    PortTex t;
    unsigned char *owned;   /* PORTTEX_COPY: the registry's copy */
    int used;
} Entry;

static Entry s_tex[PORTTEX_MAX];
static unsigned int s_version;   /* one counter for all: a replaced texture never reuses a version */

static int nameOk(const char *name)
{
    return name != NULL && name[0] != '\0' && strlen(name) < PORTTEX_NAME_MAX;
}

int portTexFind(const char *name)
{
    if (!nameOk(name)) return -1;
    for (int i = 0; i < PORTTEX_MAX; i++) {
        if (s_tex[i].used && strcmp(s_tex[i].t.name, name) == 0) return i;
    }
    return -1;
}

int portTexRegister(const char *name, int w, int h, const unsigned char *rgba, int flags)
{
    if (!nameOk(name) || w <= 0 || h <= 0 || rgba == NULL) return -1;
    int id = portTexFind(name);
    if (id < 0) {
        for (int i = 0; i < PORTTEX_MAX && id < 0; i++) {
            if (!s_tex[i].used) id = i;
        }
        if (id < 0) return -1;
    }
    Entry *e = &s_tex[id];
    unsigned char *copy = NULL;
    if (!(flags & PORTTEX_EXTERNAL)) {
        const size_t n = (size_t)w * (size_t)h * 4;
        copy = (unsigned char *)malloc(n);
        if (copy == NULL) return -1;
        memcpy(copy, rgba, n);
    }
    free(e->owned);
    e->owned = copy;
    e->used = 1;
    strcpy(e->t.name, name);
    e->t.w = w;
    e->t.h = h;
    e->t.rgba = copy != NULL ? copy : rgba;
    e->t.flags = flags;
    e->t.version = ++s_version;
    return id;
}

const PortTex *portTexGet(int id)
{
    if (id < 0 || id >= PORTTEX_MAX || !s_tex[id].used) return NULL;
    return &s_tex[id].t;
}

void portTexTouch(int id)
{
    if (id >= 0 && id < PORTTEX_MAX && s_tex[id].used) s_tex[id].t.version = ++s_version;
}

void portTexRelease(int id)
{
    if (id < 0 || id >= PORTTEX_MAX || !s_tex[id].used) return;
    free(s_tex[id].owned);
    memset(&s_tex[id], 0, sizeof s_tex[id]);
}

void portTexReset(void)
{
    for (int i = 0; i < PORTTEX_MAX; i++) portTexRelease(i);
}
