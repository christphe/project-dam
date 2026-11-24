#include "script_check.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>

#include "Luau/BuiltinDefinitions.h"
#include "Luau/Common.h"
#include "Luau/Config.h"
#include "Luau/ConfigResolver.h"
#include "Luau/ExperimentalFlags.h"
#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"
#include "Luau/TypeArena.h"

#include "script_props.h"

extern "C" {
extern const char *const g_missionApiDefs;   // port/mission/mission_lua.h
}

namespace {

struct MemFiles : Luau::FileResolver {
    std::map<std::string, std::string> files;
    std::optional<Luau::SourceCode> readSource(const Luau::ModuleName &name) override
    {
        auto it = files.find(name);
        if (it == files.end()) return std::nullopt;
        return Luau::SourceCode{ it->second, Luau::SourceCode::Module };
    }
};

struct StrictConfig : Luau::ConfigResolver {
    Luau::Config cfg;
    StrictConfig() { cfg.mode = Luau::Mode::Strict; }
    const Luau::Config &getConfig(const Luau::ModuleName &, const Luau::TypeCheckLimits &) const override { return cfg; }
};

struct Checker {
    MemFiles files;
    StrictConfig config;
    std::unique_ptr<Luau::Frontend> fe;
    std::string defsError;
    std::map<std::string, std::vector<ScriptDiag>> cache;

    Checker()
    {
        // The same flag set the Luau CLI runs with (every non-experimental "Luau*" FFlag on).
        for (Luau::FValue<bool> *f = Luau::FValue<bool>::list; f; f = f->next)
            if (strncmp(f->name, "Luau", 4) == 0 && !Luau::isAnalysisFlagExperimental(f->name)) f->value = true;
        Luau::FrontendOptions opts;
        opts.retainFullTypeGraphs = false;
        opts.runLintChecks = false;
        fe = std::make_unique<Luau::Frontend>(Luau::SolverMode::New, &files, &config, opts);
        Luau::registerBuiltinGlobals(*fe, fe->globals);
        Luau::LoadDefinitionFileResult r = fe->loadDefinitionFile(fe->globals, fe->globals.globalScope, g_missionApiDefs, "@mission", false);
        if (!r.success) {
            defsError = "mission.d.luau did not load";
            for (const Luau::ParseError &e : r.parseResult.errors) defsError += "; " + std::to_string(e.getLocation().begin.line + 1) + ": " + e.getMessage();
            if (r.module) for (const Luau::TypeError &e : r.module->errors) defsError += "; " + Luau::toString(e);
        }
        Luau::freeze(fe->globals.globalTypes);
    }
};

// The line (0-based) where the injected declarations go: after the leading comment / blank lines, so a `--!strict` hot comment
// keeps working. -1 = append.
int firstCodeLine(const std::string &s)
{
    int line = 0;
    size_t i = 0;
    bool block = false;
    std::string close;
    while (i <= s.size()) {
        size_t e = s.find('\n', i);
        if (e == std::string::npos) e = s.size();
        std::string l = s.substr(i, e - i);
        if (block) {
            if (l.find(close) != std::string::npos) block = false;
        } else {
            size_t k = l.find_first_not_of(" \t\r");
            if (k != std::string::npos) {
                if (l.compare(k, 2, "--") != 0) return line;
                if (l.compare(k, 3, "--[") == 0) {
                    size_t q = k + 3, eq = 0;
                    while (q < l.size() && l[q] == '=') { q++; eq++; }
                    if (q < l.size() && l[q] == '[') {
                        close = "]" + std::string(eq, '=') + "]";
                        if (l.find(close, q) == std::string::npos) block = true;
                    }
                }
            }
        }
        if (e >= s.size()) break;
        i = e + 1;
        line++;
    }
    return -1;
}

} // namespace

std::vector<ScriptDiag> checkScript(const std::string &source, const std::string &targetKind, std::string *defsError)
{
    static Checker *ck = nullptr;
    if (!ck) ck = new Checker();   // first use loads the builtin and mission definitions (once; a few hundred ms)
    if (defsError) *defsError = ck->defsError;
    const std::string key = targetKind + '\0' + source;
    auto hit = ck->cache.find(key);
    if (hit != ck->cache.end()) return hit->second;
    if (ck->cache.size() > 300) ck->cache.clear();

    // props is typed as the script's Props (or an open table when it has none), target by the attachment; both globals are
    // re-declared as locals on a line of the script itself so every line number stays the script's own.
    const PropsSchema ps = parsePropsSchema(source);
    const char *tclass = "Target";
    if (targetKind == "prop") tclass = "PropTarget";
    else if (targetKind == "pad") tclass = "PadTarget";
    else if (targetKind == "zone") tclass = "ZoneTarget";
    else tclass = "MissionTarget";
    std::string pre = std::string("local props: ") + (ps.found ? "Props" : "{ [string]: any }") + " = (props :: any); local target: " + tclass +
                      " = (target :: any); ";
    std::string checked = source;
    const int fl = firstCodeLine(source);
    if (fl < 0) checked += "\n" + pre;
    else {
        size_t pos = 0;
        for (int k = 0; k < fl; k++) pos = checked.find('\n', pos) + 1;
        checked.insert(pos, pre);
    }

    const std::string mod = "script";
    ck->files.files[mod] = checked;
    ck->fe->markDirty(mod);
    std::vector<ScriptDiag> out;
    try {
        Luau::CheckResult r = ck->fe->check(mod);
        for (const Luau::TypeError &e : r.errors) {
            if (e.moduleName != mod) continue;
            ScriptDiag d;
            d.line = (int)e.location.begin.line + 1; d.col = (int)e.location.begin.column + 1;
            d.endLine = (int)e.location.end.line + 1; d.endCol = (int)e.location.end.column + 1;
            d.text = Luau::toString(e);
            if (fl >= 0) {   // the declarations sit on the first code line: shift its columns back
                const int adj = (int)pre.size();
                if (d.line - 1 == fl) d.col = std::max(1, d.col - adj);
                if (d.endLine - 1 == fl) d.endCol = std::max(d.col, d.endCol - adj);
            }
            out.push_back(d);
        }
    } catch (const std::exception &x) {
        ScriptDiag d; d.text = std::string("type checker failed: ") + x.what();
        out.push_back(d);
    }
    ck->cache[key] = out;
    return out;
}
