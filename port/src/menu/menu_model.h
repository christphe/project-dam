#ifndef MENU_MODEL_H
#define MENU_MODEL_H

/* The menu's content: tabs, entries and controls, read from menu.json (see menu.json for the schema), and the
 * rules that decide what is shown. Nothing here knows about ImGui or the game. */
#include <map>
#include <string>
#include <vector>

namespace menu {

/* CT_CHOICE: buttons side by side. CT_SELECT: a list that opens (long lists). CT_SLIDER with "options" is a slider
 * with one notch per option. */
enum CtlType { CT_CHOICE, CT_TOGGLE, CT_SLIDER, CT_ACTION, CT_SELECT, CT_BIND, CT_DIAGRAM };   /* CT_BIND: a key or mouse button for an action, two slots */

/* A choice's option: the id is what the logic uses (expressions, defaults, help); the label is the English
 * source text shown to the player, looked up in the translation table (tr) when drawn. */
struct Option {
    std::string id, label;
    double value = 0;   /* the config value this option stands for (default: its position in the list) */
    std::string show;   /* an expression: the option is offered only while it holds ("" = always) */
};

struct Control {
    std::string id;       /* unique; what expressions call it */
    std::string key;      /* the game's config key this control drives (the adapter finds the row by it) */
    std::string label;
    std::string tag;      /* a small note after the label: "per profile", "restart" */
    std::string disabledTag;   /* a note shown only while the control is greyed out: "(fullscreen)" */
    std::string help;     /* one text for every value ... */
    std::vector<std::string> helpBy;   /* ... or one per option id (choices) / "0","1" (toggles) */
    std::vector<std::string> helpKeys; /* the option id each helpBy[i] belongs to */
    CtlType type = CT_TOGGLE;
    std::vector<Option> options;
    std::string follows;                                                  /* CT_DIAGRAM: the control whose option picks the layout */
    std::map<std::string, std::map<std::string, std::string>> layouts;    /* CT_DIAGRAM: option id -> slot -> text */
    int vmin = 0, vmax = 100, step = 1;
    double per = 1;                    /* config units in one control unit (before "div") */
    int div = 1, decimals = 0;         /* slider text: value / div with this many decimals */
    std::string suffix;
    std::string readout;               /* a note drawn after the value, computed by the game ("hfov") */
    double def[2] = { 0, 0 };          /* default per platform: [0] Switch, [1] PC (choice index, toggle 0/1, slider value) */
    std::string show;     /* visibility expression ("" = always) */
    std::string disabled; /* greyed-out expression */
    int dev = 0;          /* hidden unless dev mode is on: 1 on every platform, 2 on the Switch only, 3 on the PC only */
    bool restart = false;
    std::string widget;   /* a custom widget the UI draws itself ("pad", "keys") */
};

inline bool hasOptions(const Control& c) { return !c.options.empty(); }

struct Item {   /* a group heading or an entry (a topic with a form) */
    bool group = false;
    bool sub = false;
    std::string name;
    std::string show;
    std::vector<Control> controls;
};

struct Tab {
    std::string name;
    std::vector<Item> items;
};

struct Model {
    std::vector<Tab> tabs;
    std::map<std::string, const Control*> byId;   /* rebuilt by loadModel; do not copy a Model around */
};

/* Translations: every text in menu.json is English source text; a table maps it to the player's language
 * (a flat JSON object, English -> translated). A text with no entry is shown as written. Logic never reads
 * these strings: it uses ids. */
bool loadTranslations(const char* json, size_t len, std::string* err);   /* an empty table clears them */
const std::string& tr(const std::string& english);

/* Parses the JSON text. On failure returns false and fills *err; *m is then empty. */
bool loadModel(const char* json, size_t len, Model* m, std::string* err);

/* The values the menu shows. The harness keeps them itself; the game's adapter reads and writes the real settings. */
struct State {
    bool pc = true;
    bool dev = false;
    bool fileSelect = false;           /* the game is on the file select screen (the profile can be chosen) */
    bool profile = false;              /* a save file is selected: the per-profile settings can be read and written */
    bool updateAvailable = false;      /* the update check found a newer release */
    int updateState = 0;               /* 0 idle, 1 checking, 2 up to date, 3 available, 4 failed */
    std::string updateTag, updateNotes, version;   /* the release found, its text, and this build's version */
    bool inStage = false;              /* a mission is running */
    std::map<std::string, double> v;   /* control id -> value */

    void reset(const Model& m);        /* every control to its default for this platform */
    double get(const std::string& id) const;
};

bool evalBool(const std::string& expr, const Model& m, const State& s, std::string* err = nullptr);
bool controlVisible(const Control& c, const Model& m, const State& s);
bool controlDisabled(const Control& c, const Model& m, const State& s);
bool itemVisible(const Item& it, const Model& m, const State& s);   /* an entry shows when it has a visible control */
double controlToConfig(const Control& c, double ctl);                  /* the value the game stores for this control's value */
bool controlFromConfig(const Control& c, double cfg, double* ctl);     /* false: the config value is none of the control's */
bool optionVisible(const Control& c, int i, const Model& m, const State& s);
std::vector<int> visibleOptions(const Control& c, const Model& m, const State& s);   /* indices, in order */
std::string sliderText(const Control& c, double value);
const std::string& helpFor(const Control& c, double value);          /* the description for the current value ("" = none) */

}  // namespace menu

#endif
