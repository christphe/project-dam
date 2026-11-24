/* The port event bus (port/src/portevents.c): order, priorities, filters, replace, unsubscribe. Pure. */
#include "ge_test.h"
#include "../port/include/portevents.h"

static char s_trace[64];
static int s_n;

static void note(char c) { if (s_n < 63) { s_trace[s_n++] = c; s_trace[s_n] = 0; } }
static void trace_reset(void) { s_n = 0; s_trace[0] = 0; }

static int subA(int t, int p, void *d, void *c) { (void)t; (void)p; (void)d; (void)c; note('A'); return PEV_CONTINUE; }
static int subB(int t, int p, void *d, void *c) { (void)t; (void)p; (void)d; (void)c; note('B'); return PEV_CONTINUE; }
static int subC(int t, int p, void *d, void *c) { (void)t; (void)p; (void)d; (void)c; note('C'); return PEV_CONTINUE; }
static int subHandle(int t, int p, void *d, void *c) { (void)t; (void)p; (void)d; (void)c; note('H'); return PEV_HANDLED; }
static int subDouble(int t, int p, void *d, void *c) { (void)t; (void)p; (void)c; ((PortEvPuff *)d)->size *= 2.0f; return PEV_CONTINUE; }
static int subAddOne(int t, int p, void *d, void *c) { (void)t; (void)p; (void)c; ((PortEvPuff *)d)->size += 1.0f; return PEV_CONTINUE; }
static int s_selfHandle = -1;
static int subSelfDrop(int t, int p, void *d, void *c) { (void)t; (void)p; (void)d; (void)c; note('S'); portEventUnsubscribe(s_selfHandle); return PEV_CONTINUE; }
static int subCtx(int t, int p, void *d, void *c) { (void)t; (void)p; (void)d; (*(int *)c)++; return PEV_CONTINUE; }

int main(void)
{
    GE_TEST_BEGIN("portevents");

    /* no subscriber: nothing runs, the game's action is not replaced */
    portEventReset();
    GE_CHECK_EQ_INT(portEventCount(PEV_IMPACT), 0);
    GE_CHECK_EQ_INT(portEventEmit(PEV_IMPACT, PEV_BEFORE, NULL), PEV_CONTINUE);

    /* order: priority first, then subscription order */
    portEventReset(); trace_reset();
    portEventSubscribe(PEV_PUFF, PEV_AFTER, 10, subB, NULL);
    portEventSubscribe(PEV_PUFF, PEV_AFTER, 0, subA, NULL);
    portEventSubscribe(PEV_PUFF, PEV_AFTER, 10, subC, NULL);
    portEventEmit(PEV_PUFF, PEV_AFTER, NULL);
    GE_CHECK_STR(s_trace, "ABC");
    GE_CHECK_EQ_INT(portEventCount(PEV_PUFF), 3);

    /* phases are separate chains */
    trace_reset();
    portEventEmit(PEV_PUFF, PEV_BEFORE, NULL);
    GE_CHECK_STR(s_trace, "");

    /* types are separate chains */
    trace_reset();
    portEventEmit(PEV_SHOT, PEV_AFTER, NULL);
    GE_CHECK_STR(s_trace, "");

    /* replace: a BEFORE subscriber that handles stops the chain and reports it */
    portEventReset(); trace_reset();
    portEventSubscribe(PEV_RENDER_FX, PEV_BEFORE, 0, subA, NULL);
    portEventSubscribe(PEV_RENDER_FX, PEV_BEFORE, 5, subHandle, NULL);
    portEventSubscribe(PEV_RENDER_FX, PEV_BEFORE, 9, subC, NULL);
    GE_CHECK_EQ_INT(portEventEmit(PEV_RENDER_FX, PEV_BEFORE, NULL), PEV_HANDLED);
    GE_CHECK_STR(s_trace, "AH");

    /* HANDLED in AFTER does not stop the chain (nothing left to replace) */
    portEventReset(); trace_reset();
    portEventSubscribe(PEV_RENDER_FX, PEV_AFTER, 0, subHandle, NULL);
    portEventSubscribe(PEV_RENDER_FX, PEV_AFTER, 1, subB, NULL);
    GE_CHECK_EQ_INT(portEventEmit(PEV_RENDER_FX, PEV_AFTER, NULL), PEV_CONTINUE);
    GE_CHECK_STR(s_trace, "HB");

    /* filter: subscribers change the payload in order */
    portEventReset();
    {
        PortEvPuff p = { { 0, 0, 0 }, 1, 3.0f, 0 };
        portEventSubscribe(PEV_PUFF, PEV_BEFORE, 1, subAddOne, NULL);
        portEventSubscribe(PEV_PUFF, PEV_BEFORE, 0, subDouble, NULL);
        portEventEmit(PEV_PUFF, PEV_BEFORE, &p);
        GE_CHECK(p.size == 7.0f);   /* (3 * 2) + 1 */
    }

    /* unsubscribe, including from inside the callback */
    portEventReset(); trace_reset();
    {
        int h = portEventSubscribe(PEV_HIT_CHR, PEV_AFTER, 0, subA, NULL);
        s_selfHandle = portEventSubscribe(PEV_HIT_CHR, PEV_AFTER, 1, subSelfDrop, NULL);
        portEventSubscribe(PEV_HIT_CHR, PEV_AFTER, 2, subC, NULL);
        portEventEmit(PEV_HIT_CHR, PEV_AFTER, NULL);
        GE_CHECK_STR(s_trace, "ASC");
        GE_CHECK_EQ_INT(portEventCount(PEV_HIT_CHR), 2);
        trace_reset();
        portEventUnsubscribe(h);
        portEventUnsubscribe(h);   /* twice is harmless */
        portEventEmit(PEV_HIT_CHR, PEV_AFTER, NULL);
        GE_CHECK_STR(s_trace, "C");
        GE_CHECK_EQ_INT(portEventCount(PEV_HIT_CHR), 1);
    }

    /* context pointer reaches the subscriber */
    portEventReset();
    {
        int calls = 0;
        portEventSubscribe(PEV_SHOT, PEV_AFTER, 0, subCtx, &calls);
        portEventEmit(PEV_SHOT, PEV_AFTER, NULL);
        portEventEmit(PEV_SHOT, PEV_AFTER, NULL);
        GE_CHECK_EQ_INT(calls, 2);
    }

    /* bad arguments and a full table */
    portEventReset();
    GE_CHECK_EQ_INT(portEventSubscribe(PEV_COUNT, PEV_AFTER, 0, subA, NULL), -1);
    GE_CHECK_EQ_INT(portEventSubscribe(PEV_SHOT, PEV_PHASES, 0, subA, NULL), -1);
    GE_CHECK_EQ_INT(portEventSubscribe(PEV_SHOT, PEV_AFTER, 0, NULL, NULL), -1);
    for (int i = 0; i < PEV_MAX_SUBSCRIBERS; i++) {
        GE_CHECK(portEventSubscribe(PEV_SHOT, PEV_AFTER, 0, subA, NULL) >= 0);
    }
    GE_CHECK_EQ_INT(portEventSubscribe(PEV_SHOT, PEV_AFTER, 0, subA, NULL), -1);
    portEventReset();

    return GE_TEST_END();
}
