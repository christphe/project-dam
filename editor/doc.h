// The editor's document: the loaded level export, the mission being edited, the selection, and
// every action on them. The UI buttons and the MCP tools both call these functions (one
// implementation per action). Pure CPU: no window, no GL (the --mcp-stdio mode links this too).
#pragma once

#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

extern "C" {
#include "mission.h"
}
#include "levelrec.h"

typedef std::array<float, 3> Vec3;

struct LRoom { int index = 0; Vec3 mn{}, mx{}; };
struct LPad { int num = 0; Vec3 pos{}, look{}, up{}; int room = -1; };
struct LProp { int index = 0, type = 0, pad = 0, model = -1, item = -1, ai = -1, owner = -1;
               MissionFieldVals f{};   // every record field of the original (mission_fields.h), from the setup
    int chr = -1;           // a guard's chr number
    int carrier = -1;       // a weapon assigned to a guard: the carrier's chr number (its "pad" field), else -1
};
struct LPoly { int room = -1; std::vector<Vec3> v; };
struct LPortal { int a = -1, b = -1; std::vector<Vec3> v; };
struct LTri { Vec3 a, b, c; int room; int cull; };   // cull as recorded: 0 none, 1 back faces, 2 front faces

struct OwnerInfo {
    Vec3 mn{}, mx{};          // bounds of all its runs, in world space as recorded
    Vec3 anchor{};            // bottom centre of those bounds: where a model sits on a pad
    bool valid = false;
};

// One level export: DIR/level.rec + DIR/level.json.
struct Level {
    std::string dir, name, build;
    std::string dataDir;               // the game's data folder: the setup comes from its pccg sidecar
    bool hasRec = false;
    LevelRec rec{};
    std::vector<LRoom> rooms;
    std::vector<LPad> pads;
    std::vector<LProp> props;
    std::vector<LPoly> stan;
    std::vector<LPortal> portals;
    std::vector<int> ailists;
    std::map<int, int> padIndex;       // pad num -> index in pads
    std::vector<OwnerInfo> owners;     // parallel to rec.owners
    std::vector<LTri> roomTris;        // room geometry for picking
    Vec3 mn{}, mx{};                   // bounds of everything

    Level() = default;
    Level(const Level &) = delete;
    Level &operator=(const Level &) = delete;
    ~Level() { if (hasRec) levelrecFree(&rec); }

    bool load(const std::string &exportDir, std::string &err);
    const LPad *pad(int num) const;
    const LProp *prop(int index) const;
    // The recorded owner of a guard body or an object model (level.json "models"), -1 none. Body and
    // object numbers overlap (Frigate: guard body 17 and the multi-monitor model 17), so the kind matters.
    int ownerForModel(bool guard, int model) const;
    std::map<std::pair<bool, int>, int> modelOwners;   // (guard, model) -> owner
};

// A pad as an action argument: an original pad number, a mission pad id, or a point (a new
// mission pad is created there).
struct PadSpec {
    bool isNew = false;        // true: mission pad `id`
    int num = -1;
    std::string id;
    bool atPoint = false;      // create a pad at `pos` first
    Vec3 pos{};
    int room = -1;
};

// A prop of the level as displayed: the original setup props plus the mission's guard/object ops.
struct Item {
    bool added = false;        // an "add" op of the mission (else an original setup prop)
    bool guard = false;
    int idx = -1;              // original prop index, or the op index in mission.guards/objects
    bool removed = false;
    bool moved = false;
    MissionPadRef pad{};       // where it stands now
    int origPad = -1;          // original pad num (original props)
    int type = 0, model = -1, item = -1, ai = -1, weapon = -1;
    int owner = -1;
    int diffMask = 0;          // the difficulties it is present on (bit n = difficulty n; 0 = all)
    bool hasPos = false;
    Vec3 pos{};
    float yawDeg = 0;          // facing: the look of the pad it stands on (0 = +z)
    // The record fields (missionRecFields(type)): an original prop's own; an added one's own where its op
    // gives them (bit set in `given`), else the template's (the level's first record of the type).
    MissionFieldVals f{};
    uint64_t given = 0;
    // Guards: the weapons they carry (an original's assigned weapon records, or the op's list).
    std::vector<MissionWeapon> weapons;
    int chr = -1;              // an original guard's chr number
    // An original weapon record assigned to a guard: not an item of its own. It is drawn with its carrier
    // (pos / yaw / removed follow the guard), and is not picked, selected, moved or copied by itself.
    bool assigned = false;
    int carrierProp = -1;      // the carrier guard's prop index
};

struct ItemRef {
    bool valid = false;
    bool added = false;
    bool guard = false;
    int idx = -1;
    bool operator==(const ItemRef &o) const { return valid == o.valid && added == o.added && guard == o.guard && idx == o.idx; }
};

// Snapping of free positions: to a grid (step in level units, 0 = off), to the floor (the stan tile
// under the point) and to existing pads.
struct SnapOpts {
    float grid = 0;
    bool floor = false, pads = false;
};

// What can be moved: a mission pad, or a guard/object (an original prop gets a mission pad of its
// own at the new spot). Original pads are fixed level data (movable = false).
struct TransformInfo {
    bool valid = false, movable = false;
    Vec3 pos{};
    float yawDeg = 0;          // facing, from the pad's look vector (0 = +z)
};

// A copied guard or object: all its fields and its facing. In-app clipboard, shared with the MCP duplicate.
struct ClipItem {
    bool valid = false, guard = false;
    int type = 0, model = -1, item = -1, ai = -1;
    std::vector<MissionWeapon> weapons;   // a guard's carried weapons
    MissionFieldVals f{};      // every field of the record, set
    float yawDeg = 0;
};

struct Selection {
    enum Kind { NONE, ROOM, PAD, MPAD, ITEM } kind = NONE;
    int num = -1;              // ROOM: index; PAD: pad num
    std::string id;            // MPAD
    ItemRef item;              // ITEM
};

// One finding of the mission check: severity 1 warning / 2 error, the text, and what to select to look at it.
struct Issue {
    int sev = 2;
    std::string text;
    bool hasSel = false;
    Selection sel;
};

// A model the open level uses (its setup props): the Props browser lists these.
struct ModelUse {
    bool guard = false;          // a guard body (else an object model)
    int model = -1;
    int owner = -1;              // the recorded renderer owner, -1 when the export has no drawing of it
    int count = 0;               // how many setup props use it
    int type = 0;                // the setup prop type of the first user (objects)
    int item = -1;               // that user's item id, if any
};

struct Doc {
    Level level;
    bool hasLevel = false;
    Mission mission;
    bool hasMission = false;
    std::string missionPath;
    bool dirty = false;
    bool emptyPending = false;   // a new "emptied" mission waits for its level to load (openLevel)
    int levelGen = 0;          // counts openLevel calls: the GUI reloads its GPU copy when it changes
    Selection sel;
    std::string gameExe;       // GE_EDITOR_GAME or the sibling build dir's ge007
    std::string lastPlay;      // command line of the last test_run
    std::vector<std::string> undoStack;   // mission JSON before each action

    Doc() { missionInit(&mission); mission.env.mood = MISSION_MOOD_KEEP; campaignInit(&camp); }
    ~Doc() { missionFree(&mission); campaignFree(&camp); }
    Doc(const Doc &) = delete;
    Doc &operator=(const Doc &) = delete;

    // ---- files ----
    bool openLevel(const std::string &dir, std::string &err);
    std::string gameDataDir() const;               // <game exe dir>/data, or GE_EDITOR_DATA
    // Opens a solo level by name from the export cache next to the game (exportcache.h), running the
    // game's export first when it is missing or when reexport is set. Blocks until the game is done.
    bool openLevelByName(const std::string &level, bool reexport, std::string &err);
    // The open mission's level when the loaded export is another one (or none), else "".
    std::string missingLevel() const;
    // emptied: also writes a remove op for every original guard, the weapons they carry and every pickup object (see emptyLevel).
    bool newMission(const std::string &name, const std::string &levelName, std::string &err, bool emptied = false);
    // Marks removed (ops, one undo step): every original guard and the weapons they carry, and every pickup object (keys,
    // magazines, weapons, ammo, body armour). Doors, glass, scenery and the other objects are kept. Counts are returned.
    bool emptyLevel(int &guards, int &objects, std::string &err);
    bool emptyOrDefer(std::string &err);   // emptyLevel now, or when the mission's level has loaded
    // Moves the open mission to another solo level (one undo step; pads and items keep their positions).
    bool setMissionLevel(const std::string &levelName, std::string &err);
    // Where a new mission is saved unless it joins a mod: <game data dir>/missions/<slug of the name>/mission.json
    // (a free folder name), never the export cache.
    std::string defaultMissionPath(const std::string &name) const;
    bool openMission(const std::string &path, std::string &err);
    bool saveMission(const std::string &path, std::string &err);   // path "" = missionPath

    // ---- queries ----
    std::vector<Item> items() const;               // original props with the delta applied, then the added ones
    bool padPos(const MissionPadRef &r, Vec3 &out) const;
    int roomAt(const Vec3 &p) const;               // room whose bounds contain p, -1 none
    std::vector<std::string> validate() const;     // the texts of checkIssues(); empty = ok
    // missionValidate + checks against the level: format, ids, pads, ops, the walkable floor under every pad in use,
    // scripts whose target / key / objective is gone. Each finding names what to select.
    std::vector<Issue> checkIssues() const;
    // A stan (walkable floor) tile within `tol` units of p on the ground plane and within `tolY` in height. True when the
    // level has no stan data (nothing to say).
    bool floorUnder(const Vec3 &p, float tol = 35.0f, float tolY = 250.0f) const;
    std::vector<ModelUse> levelModels() const;     // the models of the level's own setup props, one entry each
    std::string describe(const std::string &detail) const;   // JSON text for level_describe

    // ---- actions (each pushes an undo step on success) ----
    bool resolvePad(const PadSpec &s, MissionPadRef &out, std::string &err);   // creates the pad at a point
    bool addGuard(const PadSpec &pad, int model, int ai, int weapon, ItemRef &out, std::string &err);
    bool addObject(const PadSpec &pad, int type, int model, int item, ItemRef &out, std::string &err);
    bool moveItem(const ItemRef &it, const PadSpec &pad, std::string &err);
    bool removeItem(const ItemRef &it, std::string &err);
    bool createPad(const Vec3 &pos, int room, std::string &id, std::string &err);
    bool undo(std::string &err);
    // Edits the fields of an item the mission added (guard: model/ai/weapon; object: type/model/item).
    bool setAddedFields(const ItemRef &it, int model, int a, int b, std::string &err);
    // One record field (index into missionRecFields) of an added item; undoable. Original props are read-only.
    bool setItemField(const ItemRef &it, int field, double value, std::string &err);
    // Copy / paste / duplicate: a copy is always a new "add" op, the original is untouched. Paste puts it at
    // the pad (a point makes a mission pad there, with the copied facing); duplicate stands it on the item's
    // own pad unless `pad` is given.
    bool copyItem(const ItemRef &it, ClipItem &out, std::string &err) const;
    bool pasteItem(const ClipItem &c, const PadSpec &pad, ItemRef &out, std::string &err);
    bool duplicateItem(const ItemRef &it, const PadSpec *pad, ItemRef &out, std::string &err);
    ClipItem clip;             // the app's clipboard (Ctrl+C)
    bool itemFromProp(int prop, ItemRef &out, std::string &err) const;   // original prop index -> ItemRef

    // ---- transforms ----
    TransformInfo getTransform(const Selection &s) const;
    // Sets the position and/or yaw (degrees) of a mission pad or of an item. An item that stands on
    // a pad of its own edits that pad; otherwise it gets a new mission pad at the spot.
    // commitUndo=false is for live drags: the caller takes snapshot() first and commit()s it.
    bool setTransform(const Selection &s, const Vec3 *pos, const float *yawDeg, bool commitUndo, std::string &err);
    void snapPoint(Vec3 &p, const SnapOpts &o, const std::string &ignoreMPad = "") const;
    std::string snapshot() const;                  // mission JSON, for undo steps taken by the caller
    void commit(const std::string &snap);          // push a snapshot taken before an edit as one undo step
    bool restoreSnapshot(const std::string &snap, std::string &err);   // cancel a drag

    // Set by the GUI: renders the view to a 320x240 PNG (a plain crop, no overlay). Empty when headless.
    std::function<bool(const std::string &path, std::string &err)> captureFn;

    // ---- mod (campaign): mods/<mod>/campaign.json + <mission>/mission.json + thumb.png ----
    Campaign camp;
    bool hasMod = false;
    bool modDirty = false;
    std::string modDir;
    bool openMod(const std::string &dir, std::string &err);          // creates the folder and an empty campaign when new
    std::string modNotice;                                           // set by saveMod when it had to make an id
    bool modIdOk() const;
    bool saveMod(std::string &err);                                  // campaign.json (not required to be valid yet)
    std::vector<std::string> validateMod() const;
    // New mission folder (unique, from `folder` or the name), mission.json written, appended to the
    // campaign, campaign saved, the mission opened.
    bool modAddMission(const std::string &name, const std::string &level, const std::string &folder, std::string &outFolder, std::string &err, bool emptied = false);
    std::string modMissionFolder(const std::string &name, const std::string &folder = "") const;   // the free folder name a new mission would get
    bool modAddOpenMission(std::string &outFolder, std::string &err);   // the open mission joins the mod (saved into it)
    bool modRemoveMission(int index, std::string &err);              // leaves the folder on disk
    bool modMoveMission(int index, int newIndex, std::string &err);
    bool modOpenMission(int index, std::string &err);
    std::string modMissionPath(int index) const;                     // <mod>/<folder>/mission.json
    std::string defaultThumbPath() const;                            // next to the open mission when it is in the mod

    // The guard's AI list (original guard: a move op that keeps its pad and carries "ai"; added guard: its "ai"). One undo step.
    bool setGuardAi(const ItemRef &it, int ai, std::string &err);

    // The difficulties (mask: bit 0 Agent .. 3 007; 0 or 15 = all) a guard / object is present on. An original prop gets a move op
    // without a pad (a presence edit). One undo step.
    bool setPresence(const ItemRef &it, int mask, std::string &err);

    // ---- objectives (undo covers them) ----
    bool addObjective(const std::string &text, int difficulty, std::string &err);
    bool removeObjective(int index, std::string &err);
    bool moveObjective(int index, int newIndex, std::string &err);

    // ---- environment (mission.env; undo covers it) ----
    bool setEnv(const MissionEnv &e, bool commitUndo);

    // ---- play ----
    // Saves the mission, then starts the game on its level with --mission/--spawn (launch.cpp).
    bool testRun(const std::string &spawn, bool coop, std::string &err);   // coop: two players

private:
    bool applyMove(const ItemRef &it, const MissionPadRef &ref);
    int padRefCount(const char *id) const;
    std::string createPadRaw(const Vec3 &pos, int room);
    bool itemInRange(const ItemRef &it) const;
    bool isAssignedWeapon(const ItemRef &it) const;
    std::string newPadId() const;
    bool findOp(bool guard, int prop, int &opIndex) const;
};

// Small helpers shared by the UI and MCP code.
bool readFile(const std::string &path, std::string &out);
bool padSpecFromJson(const struct cJSON *args, PadSpec &out, std::string &err);   // "pad": n | "id", or "pos": [x,y,z]

// UUID v4 from a real random source (std::random_device); and the 8-4-4-4-12 hex check.
std::string generateUuid();
bool uuidValid(const char *s);
