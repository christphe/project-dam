#ifndef MENU_JSON_H
#define MENU_JSON_H

/* A small JSON reader for the menu's data file (menu.json). No dependency, no libc number parsing (the game
 * defines some libc names of its own, so this code stays on plain C++). Strict enough to catch typos in the
 * file, with a line number in the message. */
#include <stddef.h>
#include <string>
#include <utility>
#include <vector>

namespace menu {

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    const Json* get(const char* key) const {   /* object member, or null */
        if (t != Obj) return nullptr;
        for (const auto& kv : o)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    std::string str(const char* key, const std::string& dflt = std::string()) const {
        const Json* j = get(key);
        return j && j->t == Str ? j->s : dflt;
    }
    double num(const char* key, double dflt = 0) const {
        const Json* j = get(key);
        return j && j->t == Num ? j->n : dflt;
    }
    bool flag(const char* key, bool dflt = false) const {
        const Json* j = get(key);
        return j && j->t == Bool ? j->b : dflt;
    }
};

/* Parses text[0, len). On failure returns false and fills *err ("line 12: expected ':'"). */
bool parseJson(const char* text, size_t len, Json* out, std::string* err);

}  // namespace menu

#endif
