// script_check_cli: type-checks Luau mission scripts against the script API (mission.d.luau), as the editor does
// (script_check.cpp), from the command line: the campaign (#165) is checked this way in CI and before a game run.
//
//   script_check_cli [--kind mission|prop|pad|zone] [--errors-only] [--ignore-require] FILE.luau...
//
// Each diagnostic prints as "file:line:col: error|warning: text". --ignore-require drops the diagnostics about
// require()d modules (the checker sees one script at a time: a mod's modules are not resolved). Exit status 1 when
// an error is left.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "script_check.h"

int main(int argc, char **argv)
{
    std::string kind = "mission";
    bool errorsOnly = false, ignoreRequire = false;
    int files = 0, errors = 0, warnings = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--kind") && i + 1 < argc) { kind = argv[++i]; continue; }
        if (!strcmp(argv[i], "--errors-only")) { errorsOnly = true; continue; }
        if (!strcmp(argv[i], "--ignore-require")) { ignoreRequire = true; continue; }
        std::ifstream in(argv[i], std::ios::binary);
        if (!in) { fprintf(stderr, "%s: cannot read\n", argv[i]); errors++; continue; }
        std::stringstream ss;
        ss << in.rdbuf();
        std::string defsError;
        std::vector<ScriptDiag> diags = checkScript(ss.str(), kind, &defsError);
        if (!defsError.empty()) { fprintf(stderr, "the API definitions do not load: %s\n", defsError.c_str()); return 2; }
        files++;
        for (const ScriptDiag &d : diags) {
            if (errorsOnly && d.sev < 2) continue;
            if (ignoreRequire && (d.text.find("require") != std::string::npos || d.text.find("Unknown require") != std::string::npos))
                continue;
            printf("%s:%d:%d: %s: %s\n", argv[i], d.line, d.col, d.sev >= 2 ? "error" : "warning", d.text.c_str());
            if (d.sev >= 2) errors++; else warnings++;
        }
    }
    printf("%d file(s), %d error(s), %d warning(s)\n", files, errors, warnings);
    return errors ? 1 : 0;
}
