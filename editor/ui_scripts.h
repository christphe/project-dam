// The scripts and zones panels: the Scene tree's Zones / Scripts nodes, the Selection tab's Script and Zone sections (the
// typed property form), the Luau code editor window, picking targets in the 3D view, and the zone / script-target overlay.
// ui_app.h says how the UI files are split; the document side is doc_scripts.cpp.
#pragma once

#include "ui_app.h"

namespace ed {

struct ScriptsUi;                                   // the state (ui_scripts.cpp; holds the code editor widget)
ScriptsUi &scriptsUi(App &a);

void drawSceneZonesAndScripts(App &a, const char *filter);   // the two Scene tree nodes (replaces the read-only lists)
void drawScriptSection(App &a);                     // Selection tab: sel.kind == SCRIPT
void drawZoneSection(App &a);                       // Selection tab: sel.kind == ZONE
void drawScriptEditorWindow(App &a);                // the floating Luau editor
void openScriptEditor(App &a, const std::string &use, int line = 0);
bool scriptPickClick(App &a, const Hit &h);         // a pick in progress (target / property) takes the click
bool scriptPicking(const App &a);
void cancelScriptPick(App &a);
bool zoneToolClick(App &a, const Hit &h);           // the Zone tool: a click on the floor makes a zone
void drawScriptOverlay(App &a, const Overlay &ov);  // zones, the selected script's target and its referenced props
void selectScript(App &a, int index);
void scriptsFrame(App &a);                          // once per frame: caches, the text widget -> document push

} // namespace ed
