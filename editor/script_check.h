// Type-checks a mission script with Luau Analysis (port/third_party/luau/Analysis, the editor only) against the script API
// definitions (mission.d.luau, embedded as g_missionApiDefs). `props` is typed as the script's own `Props`, `target` by the
// kind of the attachment. Results are cached by (kind, source): asking again for the same text costs nothing.
#pragma once

#include <string>
#include <vector>

struct ScriptDiag {
    int line = 1, col = 1;           // 1-based start
    int endLine = 1, endCol = 1;     // 1-based, end exclusive
    int sev = 2;                     // 1 warning, 2 error
    std::string text;
};

// targetKind: "prop" | "pad" | "zone" | "mission" (anything else = mission).
// `defsOk` (when given) is false and `defsError` says why when the API definitions could not be loaded.
std::vector<ScriptDiag> checkScript(const std::string &source, const std::string &targetKind, std::string *defsError = nullptr);
