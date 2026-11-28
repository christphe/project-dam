/*
 * missions_ui.c -- the briefing text, the objective list and the pause-menu objectives of a custom mission (issue #103).
 * Called from src/game/front.c and options.c under #ifdef PORT, like the D960 Co-op row calls port/src/coop.c: the
 * game files carry the hooks, the logic and the drawing are here.
 */
#include "platform.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <ultra64.h>
#include "boss.h"
#include "game/front.h"
#include "game/textrelated.h"
#include "game/language.h"
#include "game/bondview.h"
#include "game/lv.h"
#include "game/objective_status.h"
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
extern s32 get_difficulty_for_objective(s32 objectiveIndex);
extern u8 *get_text_for_objective(int objectiveIndex);

/* "keep_level_objectives": the level keeps its briefing, objectives and exit; the mission's objectives follow the level's. */
int missionsKeepLevelObjectives(void)
{
    const Mission *m = missionsCurrent();
    return m && m->keep_level_objectives;
}

/* A mission string may be a reference to the game's own text, "text:XXXX" (4 hex digits, a langGet id): its name,
 * description, briefing and objective texts. Resolved here, wherever the mission UI shows one, in the language played;
 * anything else (or a text the loaded banks do not have) is shown as written. One of 4 rotating buffers per call. */
static const char *tx(const char *s)
{
    static char ring[4][MISSION_TEXT_LEN * 4 + 8];
    static int next;
    char *out;
    char *end = NULL;
    long id;
    const char *t;
    size_t n = 0;
    if (!s || strncmp(s, "text:", 5) != 0 || strlen(s) != 9) return s;
    id = strtol(s + 5, &end, 16);
    if (!end || *end != 0 || id < 0x400 || id > 0xB3FF) return s;
    t = (const char *)langGet((s32)id);
    if (!t || !t[0]) return s;
    out = ring[next++ & 3];
    while (t[n] && n + 1 < sizeof ring[0]) { out[n] = t[n]; n++; }
    while (n > 0 && out[n - 1] == '\n') n--;   /* the game's lines end in a line break; the caller lays text out */
    out[n] = 0;
    return out;
}

/* ---- briefing ------------------------------------------------------------ */

int missionsHeaderLines(char *l1, char *l2, int size)
{
    const char *mod = modsModName();
    int slot = modsActiveSlot();
    if (!missionsObjectivesActive()) return 0;
    if (mod[0]) {
        snprintf(l1, size, "Mission %d: %s\n", slot >= 0 ? slot + 1 : 1, mod);
        snprintf(l2, size, "%s\n", tx(missionsName()));
    } else {
        snprintf(l1, size, "%s\n", tx(missionsName()));
        snprintf(l2, size, "\n");
    }
    return 1;
}

int missionsObjectivesActive(void)
{
    return missionsActive() && selected_stage == missionsStage();
}

const char *missionsBriefingPageText(int page)
{
    static char buf[MISSION_TEXT_LEN + 2048 + 8];
    if (!missionsObjectivesActive() || missionsKeepLevelObjectives()) return NULL;   /* kept: the level's own pages */
    if (page == 1) {   /* BRIEFING_OVERVIEW: the mission's name and briefing */
        const char *b = tx(missionsBriefing());
        snprintf(buf, sizeof buf, "%s\n\n%s\n", tx(missionsName()), b[0] ? b : "No briefing.");
    } else {           /* M, Q, Moneypenny: a mission has one briefing page */
        buf[0] = 0;
    }
    return buf;
}

/* ---- the pause menu (options.c) ------------------------------------------ */

int missionsPauseActive(void)
{
    return missionsActive() && missionsRuntime() && (int)bossGetStageNum() == missionsStage();
}

/* With keep_level_objectives the pause list is the level's objectives (the game's own text, difficulty and status) and
 * then the mission's: index < levelCount() is the level's, above it the mission's. */
static int levelCount(void) { return missionsKeepLevelObjectives() ? (int)objectiveGetCount() : 0; }

int missionsPauseObjCount(void) { return levelCount() + missionRtObjectiveCount(missionsRuntime()); }

int missionsPauseObjDifficulty(int i)
{
    const int lc = levelCount();
    return i < lc ? (int)get_difficulty_for_objective(i) : missionRtObjectiveDifficulty(missionsRuntime(), i - lc);
}

int missionsPauseObjStatus(int i)   /* OBJECTIVESTATUS */
{
    const int lc = levelCount();
    return i < lc ? (int)get_status_of_objective(i) : missionRtObjectiveStatus(missionsRuntime(), i - lc);
}

const char *missionsPauseObjText(int i)
{
    static char buf[MISSION_TEXT_LEN * 2];
    size_t n;
    const int lc = levelCount();
    if (i < lc) {   /* the game's own text, as WATCH_OBJ_TEXT gives it without a mission */
        const char *t = (const char *)get_text_for_objective(i);
        textWrap(0x73, (char *)(t ? t : ""), buf, ptrFontBankGothicChars, ptrFontBankGothic);
        n = strlen(buf);
        if (n == 0 || buf[n - 1] != '\n') {
            if (n + 2 < sizeof buf) { buf[n] = '\n'; buf[n + 1] = 0; }
        }
        return buf;
    }
    textWrap(0x73, (char *)tx(missionRtObjectiveText(missionsRuntime(), i - lc)), buf, ptrFontBankGothicChars, ptrFontBankGothic);
    n = strlen(buf);
    if (n == 0 || buf[n - 1] != '\n') {
        if (n + 2 < sizeof buf) { buf[n] = '\n'; buf[n + 1] = 0; }
    }
    return buf;
}

const char *missionsPausePageText(int page)
{
    static char buf[MISSION_TEXT_LEN + 2048 + 8];
    if (page == 0 || missionsKeepLevelObjectives()) return NULL;
    if (page == 1) {
        const char *b = tx(missionsBriefing());
        snprintf(buf, sizeof buf, "%s\n\n%s\n", tx(missionsName()), b[0] ? b : "No briefing.");
    } else {
        buf[0] = 0;
    }
    return buf;
}

/* ---- objectives ----------------------------------------------------------- */

/* One objective line: the letter, the wrapped text and (withStatus) its status; `lines` is the running line count. */
static Gfx *drawObjective(Gfx *gdl, int ytop, int withStatus, int letter, s32 *lines, const char *t, int done)
{
    char label[8], wrapped[1100];
    s32 lh = 0, lw = 0, x, y;
    snprintf(label, sizeof label, "a.\n");
    label[0] = (char)('a' + letter);
    textMeasure(&lh, &lw, label, ptrFontZurichBoldChars, ptrFontZurichBold, 0);
    x = 0x37;
    y = lh * *lines + ytop;
    gdl = frontPrintText(gdl, &x, &y, label, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    x = 0x4B;
    y = lh * *lines + ytop;
    if (withStatus) setTextWordWrap(2);
    textWrap(withStatus ? 0xDC : 0x140, (char *)t, wrapped, ptrFontZurichBoldChars, ptrFontZurichBold);
    if (withStatus) setTextWordWrap(0);
    /* the game's own strings end in '\n' and sub_GAME_7F0AC0E8 counts newlines for the line advance;
     * a mod's text has none, so every objective would advance by 0 lines and overprint the last */
    {
        size_t wn = strlen(wrapped);
        if (wn == 0 || wrapped[wn - 1] != '\n') { if (wn + 2 < sizeof wrapped) { wrapped[wn] = '\n'; wrapped[wn + 1] = 0; } }
    }
    gdl = frontPrintText(gdl, &x, &y, wrapped, ptrFontZurichBoldChars, ptrFontZurichBold, 0xFF, viGetX(), viGetY(), 0, 0);
    if (withStatus) {
        s32 slot = done ? TITLE_STR_91_COMPLETED : TITLE_STR_92_FAILED;   /* not inside getStringID: the macro is unparenthesised */
        char *st = (char *)langGet(getStringID(LTITLE, slot));
        x = 0x136;
        y = lh * *lines + ytop;
        gdl = frontPrintText(gdl, &x, &y, st, ptrFontZurichBoldChars, ptrFontZurichBold, done ? 0xFF : 0x780000FF, viGetX(), viGetY(), 0, 0);
    }
    *lines += sub_GAME_7F0AC0E8((u8 *)wrapped);
    return gdl;
}

Gfx *missionsObjectivesDraw(Gfx *gdl, int ytop, int withStatus)
{
    const Mission *m = missionsCurrent();
    MissionRuntime *rt = missionsRuntime();
    s32 lines = 0;
    int i, n, shown = 0;
    DIFFICULTY diff = selected_difficulty;
    if (!m) return gdl;
    if (missionsKeepLevelObjectives() && ptrbriefingdata) {   /* the level's own first (front.c print_objectives_and_status_to_menu) */
        for (i = 0; i < OBJECTIVES_MAX; i++) {
            if (ptrbriefingdata->objective[i].textid == 0 || diff < ptrbriefingdata->objective[i].enabled_difficulty) continue;
            gdl = drawObjective(gdl, ytop, withStatus, shown, &lines, (const char *)langGet(ptrbriefingdata->objective[i].textid),
                                get_status_of_objective(i) == 1);
            shown++;
        }
    }
    n = rt ? missionRtObjectiveCount(rt) : m->nobjectives;
    for (i = 0; i < n; i++) {
        const char *t = tx(rt ? missionRtObjectiveText(rt, i) : m->objectives[i].text);
        int od = rt ? missionRtObjectiveDifficulty(rt, i) : m->objectives[i].difficulty;
        if (od > (int)diff) continue;
        gdl = drawObjective(gdl, ytop, withStatus, shown, &lines, t, rt && missionRtObjectiveComplete(rt, i));
        shown++;
    }
    return gdl;
}
