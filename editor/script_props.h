// Reads a mission script's `export type Props = { ... }` with the vendored Luau parser (port/third_party/luau/Ast): the
// field names, their type names (the editor-facing aliases of mission.d.luau: Item, Objective, Door, ...) and whether
// each is optional. Pure CPU, no Analysis needed.
#pragma once

#include <string>
#include <vector>

struct PropField {
    std::string name;
    std::string type;        // the type's name for a plain reference (Item, number, string, boolean, ...); "?" for anything else
    bool optional = false;   // T? or T | nil: may be left out of mission.json
    int line = 0;            // 1-based source line of the field
};

struct PropsSchema {
    bool found = false;      // a `type Props = { ... }` table exists
    std::vector<PropField> fields;
    std::string error;       // a parse error that stopped the search (the first one), empty if none
};

PropsSchema parsePropsSchema(const std::string &source);
