/*
 * portevents.c -- the port's game event bus (port/include/portevents.h).
 * Pure: no SDL, no game state (tests/test_portevents.c links it alone).
 */
#include "portevents.h"

#include <stddef.h>
#ifndef NULL   /* the port's stddef shim (port/shim) does not define it */
#define NULL ((void *)0)
#endif

typedef struct Sub {
    PortEventFn fn;
    void *ctx;
    int type, phase, priority;
    unsigned seq;     /* subscription order, the tie-break between equal priorities */
} Sub;

static Sub s_subs[PEV_MAX_SUBSCRIBERS];
static int s_count[PEV_COUNT];
static unsigned s_seq;

int portEventSubscribe(int type, int phase, int priority, PortEventFn fn, void *ctx)
{
    if (type < 0 || type >= PEV_COUNT || phase < 0 || phase >= PEV_PHASES || fn == NULL) {
        return -1;
    }
    for (int i = 0; i < PEV_MAX_SUBSCRIBERS; i++) {
        if (s_subs[i].fn == NULL) {
            s_subs[i].fn = fn;
            s_subs[i].ctx = ctx;
            s_subs[i].type = type;
            s_subs[i].phase = phase;
            s_subs[i].priority = priority;
            s_subs[i].seq = ++s_seq;
            s_count[type]++;
            return i;
        }
    }
    return -1;
}

void portEventUnsubscribe(int handle)
{
    if (handle < 0 || handle >= PEV_MAX_SUBSCRIBERS || s_subs[handle].fn == NULL) {
        return;
    }
    s_count[s_subs[handle].type]--;
    s_subs[handle].fn = NULL;
}

int portEventCount(int type)
{
    return (type >= 0 && type < PEV_COUNT) ? s_count[type] : 0;
}

void portEventReset(void)
{
    for (int i = 0; i < PEV_MAX_SUBSCRIBERS; i++) {
        s_subs[i].fn = NULL;
    }
    for (int t = 0; t < PEV_COUNT; t++) {
        s_count[t] = 0;
    }
    s_seq = 0;
}

int portEventEmit(int type, int phase, void *data)
{
    if (type < 0 || type >= PEV_COUNT || s_count[type] == 0) {
        return PEV_CONTINUE;
    }
    /* Walk the matching subscribers in (priority, seq) order without sorting the table: at most 32
     * entries, so pick the next one each round. */
    int lastPrio = 0;
    unsigned lastSeq = 0;
    int first = 1;
    for (;;) {
        int best = -1;
        for (int i = 0; i < PEV_MAX_SUBSCRIBERS; i++) {
            const Sub *s = &s_subs[i];
            if (s->fn == NULL || s->type != type || s->phase != phase) continue;
            if (!first && (s->priority < lastPrio || (s->priority == lastPrio && s->seq <= lastSeq))) continue;
            if (best < 0 || s->priority < s_subs[best].priority ||
                (s->priority == s_subs[best].priority && s->seq < s_subs[best].seq)) {
                best = i;
            }
        }
        if (best < 0) {
            return PEV_CONTINUE;
        }
        first = 0;
        lastPrio = s_subs[best].priority;
        lastSeq = s_subs[best].seq;
        /* the callback may unsubscribe itself: read what we need first */
        PortEventFn fn = s_subs[best].fn;
        void *ctx = s_subs[best].ctx;
        if (fn(type, phase, data, ctx) == PEV_HANDLED && phase == PEV_BEFORE) {
            return PEV_HANDLED;
        }
    }
}
