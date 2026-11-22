/*
 * missions_ui.c -- the "More..." mission list in the mission select, the briefing text and the objective
 * list of a custom mission (issue #103). Called from src/game/front.c under #ifdef PORT, exactly like the
 * D960 Co-op row calls port/src/coop.c: front.c carries the hooks, the logic and the drawing are here.
 *
 * The list is drawn with the game's own menu font on the mission select's background (the wallet), so it
 * looks like the rest of the front end and needs nothing but the game's pad input.
 */
#include "platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "game/front.h"
#include "game/textrelated.h"
#include "game/language.h"
#include "game/bondview.h"
#include "game/lv.h"
#include "fr.h"
#include "joy.h"
#include "system.h"
#include "mission.h"
#include "mission_lua.h"
#include "missions.h"
#include "missions_priv.h"
#include "assets/obseg/text/LtitleE.h"

Gfx *frontPrintText(Gfx *gdl, s32 *x, s32 *y, char *text, struct fontchar *chars, struct font *font, s32 arg6, s32 vx, s32 vy, s32 a9, s32 aa);
void setTextOrientation(s32 orientation);
int  sub_GAME_7F0AC0E8(u8 *arg);
MissionRuntime *missionsRuntime(void);

/* ---- briefing ------------------------------------------------------------ */

int missionsObjectivesActive(void)
{
    return missionsActive() && selected_stage == missionsStage();
}

const char *missionsBriefingPageText(int page)
{
    static char buf[MISSION_TEXT_LEN + 2048 + 8];
    if (!missionsObjectivesActive()) return NULL;
    if (page == 1) {   /* BRIEFING_OVERVIEW: the mission's name and briefing */
        const char *b = missionsBriefing();
        snprintf(buf, sizeof buf, "%s\n\n%s\n", missionsName(), b[0] ? b : "No briefing.");
    } else {           /* M, Q, Moneypenny: a mission has one briefing page */
        buf[0] = 0;
    }
    return buf;
}

/* ---- objectives ----------------------------------------------------------- */

Gfx *missionsObjectivesDraw(Gfx *gdl, int ytop, int withStatus)
{
    const Mission *m = missionsCurrent();
    MissionRuntime *rt = missionsRuntime();
    char label[8], wrapped[1100];
    s32 lh = 0, lw = 0, lines = 0;
    int i, n, shown = 0;
    DIFFICULTY diff = selected_difficulty;
    if (!m) return gdl;
    n = rt ? missionRtObjectiveCount(rt) : m->nobjectives;
    for (i = 0; i < n; i++) {
        const char *t = rt ? missionRtObjectiveText(rt, i) : m->objectives[i].text;
        int od = rt ? missionRtObjectiveDifficulty(rt, i) : m->objectives[i].difficulty;
        s32 x, y;
        if (od > (int)diff) continue;
        snprintf(label, sizeof label, "a.\n");
        label[0] = (char)('a' + shown);
        textMeasure(&lh, &lw, label, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
        x = 0x37;
        y = lh * lines + ytop;
        gdl = frontPrintText(gdl, &x, &y, label, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        x = 0x4B;
        y = lh * lines + ytop;
        if (withStatus) setTextWordWrap(2);
        textWrap(withStatus ? 0xDC : 0x140, (char *)t, wrapped, ptrFontZurichBoldChars, ptrFontZurichBold);
        if (withStatus) setTextWordWrap(0);
        gdl = frontPrintText(gdl, &x, &y, wrapped, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
        if (withStatus) {
            int done = rt && missionRtObjectiveComplete(rt, i);
            char *st = (char *)langGet(getStringID(LTITLE, done ? TITLE_STR_91_COMPLETED : TITLE_STR_92_FAILED));
            x = 0x136;
            y = lh * lines + ytop;
            gdl = frontPrintText(gdl, &x, &y, st, ptrFontZurichBoldChars, ptrFontZurichBold, done ? 0xFF : 0x780000FF, viGetX(), viGetY(), 0, 0);
        }
        lines += sub_GAME_7F0AC0E8((u8 *)wrapped);
        shown++;
    }
    return gdl;
}
