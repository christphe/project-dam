#include "menu_model.h"
#include "../defaults.h"
#include "menu_json.h"

namespace menu {

namespace {

/* ---- the expression language: ids, 'strings', numbers, == != && || ! and parentheses ---- */
struct EV {
    bool isStr = false;
    double n = 0;
    std::string s;
    bool truthy() const { return isStr ? !s.empty() : n != 0; }
};

struct Ctx {
    const Model* m;
    const State* st;
};

struct Expr {
    const std::string& src;
    size_t i = 0;
    const Ctx& ctx;
    std::string err;

    Expr(const std::string& s, const Ctx& c) : src(s), ctx(c) {}

    void ws() { while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) i++; }
    bool peek2(const char* a) { ws(); return i + 1 < src.size() && src[i] == a[0] && src[i + 1] == a[1]; }

    EV ident(const std::string& name) {
        EV e;
        if (name == "platform") { e.isStr = true; e.s = ctx.st->pc ? "pc" : "switch"; return e; }
        if (name == "console") { e.n = ctx.st->console ? 1 : 0; return e; }
        if (name == "handheld") { e.n = ctx.st->handheld ? 1 : 0; return e; }
        if (name == "dev") { e.n = ctx.st->dev ? 1 : 0; return e; }
        if (name == "fileselect") { e.n = ctx.st->fileSelect ? 1 : 0; return e; }
        if (name == "profile") { e.n = ctx.st->profile ? 1 : 0; return e; }
        if (name == "update") { e.n = ctx.st->updateAvailable ? 1 : 0; return e; }
        if (name == "stage") { e.n = ctx.st->inStage ? 1 : 0; return e; }
        if (name == "true") { e.n = 1; return e; }
        if (name == "false") { e.n = 0; return e; }
        auto it = ctx.m->byId.find(name);
        if (it == ctx.m->byId.end()) { if (err.empty()) err = "unknown name '" + name + "'"; return e; }
        const Control& c = *it->second;
        const double v = ctx.st->get(name);
        if (hasOptions(c)) {
            e.isStr = true;
            const int k = (int)v;
            if (k >= 0 && k < (int)c.options.size()) e.s = c.options[k].id;   /* expressions compare ids, never labels */
        } else {
            e.n = v;
        }
        return e;
    }
    EV primary() {
        ws();
        EV e;
        if (i >= src.size()) { if (err.empty()) err = "unexpected end"; return e; }
        if (src[i] == '(') {
            i++;
            e = orExpr();
            ws();
            if (i < src.size() && src[i] == ')') i++;
            else if (err.empty()) err = "expected ')'";
            return e;
        }
        if (src[i] == '\'') {
            i++;
            e.isStr = true;
            while (i < src.size() && src[i] != '\'') e.s += src[i++];
            if (i < src.size()) i++;
            return e;
        }
        if ((src[i] >= '0' && src[i] <= '9') || src[i] == '-') {
            bool neg = false;
            if (src[i] == '-') { neg = true; i++; }
            while (i < src.size() && src[i] >= '0' && src[i] <= '9') e.n = e.n * 10 + (src[i++] - '0');
            if (neg) e.n = -e.n;
            return e;
        }
        std::string name;
        while (i < src.size() && ((src[i] >= 'a' && src[i] <= 'z') || (src[i] >= 'A' && src[i] <= 'Z') ||
                                  (src[i] >= '0' && src[i] <= '9') || src[i] == '_'))
            name += src[i++];
        if (name.empty()) { if (err.empty()) err = "unexpected character"; i++; return e; }
        return ident(name);
    }
    EV notExpr() {
        ws();
        if (i < src.size() && src[i] == '!' && !(i + 1 < src.size() && src[i + 1] == '=')) {
            i++;
            EV e;
            e.n = notExpr().truthy() ? 0 : 1;
            return e;
        }
        return cmp();
    }
    EV cmp() {
        EV a = primary();
        ws();
        const bool eq = peek2("==");
        const bool ne = peek2("!=");
        if (!eq && !ne) return a;
        i += 2;
        EV b = primary();
        bool same;
        if (a.isStr || b.isStr) same = a.isStr && b.isStr ? a.s == b.s : false;
        else same = a.n == b.n;
        EV e;
        e.n = (eq ? same : !same) ? 1 : 0;
        return e;
    }
    EV andExpr() {
        EV a = notExpr();
        while (peek2("&&")) {
            i += 2;
            EV b = notExpr();
            a.isStr = false;
            a.n = (a.truthy() && b.truthy()) ? 1 : 0;
        }
        return a;
    }
    EV orExpr() {
        EV a = andExpr();
        while (peek2("||")) {
            i += 2;
            EV b = andExpr();
            a.isStr = false;
            a.n = (a.truthy() || b.truthy()) ? 1 : 0;
        }
        return a;
    }
};

CtlType typeOf(const std::string& s, bool* ok) {
    *ok = true;
    if (s == "choice") return CT_CHOICE;
    if (s == "toggle") return CT_TOGGLE;
    if (s == "slider") return CT_SLIDER;
    if (s == "action") return CT_ACTION;
    if (s == "select") return CT_SELECT;
    if (s == "bind") return CT_BIND;
    if (s == "diagram") return CT_DIAGRAM;
    *ok = false;
    return CT_TOGGLE;
}

/* "default": a value, or {"switch": v, "pc": v}; a string selects a choice by its option text */
bool readDefault(const Json* d, Control* c, std::string* err) {
    auto one = [&](const Json* j, double* out) {
        if (!j) return true;
        if (j->t == Json::Num) { *out = j->n; return true; }
        if (j->t == Json::Bool) { *out = j->b ? 1 : 0; return true; }
        if (j->t == Json::Str) {
            for (size_t k = 0; k < c->options.size(); k++)
                if (c->options[k].id == j->s) { *out = (double)k; return true; }
            *err = "control '" + c->id + "': default '" + j->s + "' is not one of its option ids";
            return false;
        }
        *err = "control '" + c->id + "': bad default";
        return false;
    };
    if (!d) return true;
    if (d->t == Json::Obj) return one(d->get("switch"), &c->def[0]) && one(d->get("pc"), &c->def[1]);
    return one(d, &c->def[0]) && one(d, &c->def[1]);
}

bool readControl(const Json& j, Control* c, std::string* err) {
    c->id = j.str("id");
    c->key = j.str("key");
    if (const Json* l = j.get("label"); l && l->t == Json::Obj) {   /* {"switch": a, "pc": b} */
        c->label = l->str("switch");
        c->labelPc = l->str("pc");
    } else c->label = j.str("label");
    c->tag = j.str("tag");
    c->disabledTag = j.str("disabledTag");
    c->show = j.str("show");
    c->disabled = j.str("disabled");
    if (const Json* d = j.get("dev")) {   /* true / "all": everywhere; "switch" or "pc": that platform only */
        if (d->t == Json::Bool) c->dev = d->b ? 1 : 0;
        else if (d->s == "switch") c->dev = 2;
        else if (d->s == "pc") c->dev = 3;
        else if (d->s == "all") c->dev = 1;
    }
    c->restart = j.flag("restart");
    c->widget = j.str("widget");
    if (c->id.empty()) { *err = "a control has no \"id\" (label '" + c->label + "')"; return false; }
    bool ok;
    c->type = typeOf(j.str("type"), &ok);
    if (!ok) { *err = "control '" + c->id + "': unknown type '" + j.str("type") + "'"; return false; }
    if (const Json* o = j.get("options"))
        for (const Json& x : o->a) {   /* "Text" (id = text) or {"id": "enhanced", "label": "Enhanced"} */
            Option op;
            op.value = (double)c->options.size();
            if (x.t == Json::Str) op.id = op.label = x.s;
            else { op.id = x.str("id"); op.label = x.str("label", op.id); op.value = x.num("value", op.value); op.show = x.str("show"); }
            if (op.id.empty()) { *err = "control '" + c->id + "': an option has no id"; return false; }
            c->options.push_back(op);
        }
    if ((c->type == CT_CHOICE || c->type == CT_SELECT) && c->options.empty()) { *err = "control '" + c->id + "': a choice needs \"options\""; return false; }
    c->vmin = (int)j.num("min", 0);
    c->vmax = (int)j.num("max", 100);
    c->step = (int)j.num("step", 1);
    if (c->step < 1) c->step = 1;
    if (c->type == CT_SLIDER && !c->options.empty()) { c->vmin = 0; c->vmax = (int)c->options.size() - 1; c->step = 1; }   /* a notch per option */
    c->follows = j.str("follows");
    if (const Json* l = j.get("layouts"))
        for (const auto& kv : l->o)
            for (const auto& slot : kv.second.o) c->layouts[kv.first][slot.first] = slot.second.s;
    c->per = j.num("per", 1);
    if (c->per <= 0) c->per = 1;
    c->div = (int)j.num("div", 1);
    if (c->div < 1) c->div = 1;
    c->decimals = (int)j.num("decimals", 0);
    c->suffix = j.str("suffix");
    c->readout = j.str("readout");
    if (const Json* h = j.get("help")) {
        if (h->t == Json::Str) c->help = h->s;
        else if (h->t == Json::Obj)
            for (const auto& kv : h->o) { c->helpKeys.push_back(kv.first); c->helpBy.push_back(kv.second.s); }
    }
    if (!readDefault(j.get("default"), c, err)) return false;
    if (!j.get("default") && !c->key.empty()) {   /* no explicit default: the one in defaults.json */
        for (int pc = 0; pc < 2; pc++) {
            double v;
            if (!defaultsGetFor(pc, c->key.c_str(), &v)) continue;
            if (!c->options.empty()) {
                bool found = false;
                for (size_t k = 0; k < c->options.size(); k++)
                    if (c->options[k].value == v) { c->def[pc] = (double)k; found = true; break; }
                if (!found) { *err = "control '" + c->id + "': defaults.json value for " + c->key + " matches no option"; return false; }
            } else {
                c->def[pc] = v * c->div / c->per;   /* a slider with "div" shows value/div */
            }
        }
    }
    return true;
}

}  // namespace

bool loadModel(const char* json, size_t len, Model* m, std::string* err) {
    m->tabs.clear();
    m->byId.clear();
    Json root;
    std::string e;
    if (!parseJson(json, len, &root, &e)) { if (err) *err = "menu.json, " + e; return false; }
    const Json* tabs = root.get("tabs");
    if (!tabs || tabs->t != Json::Arr) { if (err) *err = "menu.json: no \"tabs\" array"; return false; }
    for (const Json& tj : tabs->a) {
        Tab t;
        t.name = tj.str("name");
        const Json* items = tj.get("items");
        if (items)
            for (const Json& ij : items->a) {
                Item it;
                if (const Json* g = ij.get("group")) { it.group = true; it.name = g->s; it.show = ij.str("show"); }
                else {
                    it.name = ij.str("name");
                    it.sub = ij.flag("sub");
                    it.show = ij.str("show");
                    if (const Json* cs = ij.get("controls"))
                        for (const Json& cj : cs->a) {
                            Control c;
                            if (!readControl(cj, &c, &e)) { if (err) *err = "menu.json: " + e; m->tabs.clear(); return false; }
                            it.controls.push_back(std::move(c));
                        }
                }
                t.items.push_back(std::move(it));
            }
        m->tabs.push_back(std::move(t));
    }
    for (const Tab& t : m->tabs)
        for (const Item& it : t.items)
            for (const Control& c : it.controls) {
                if (m->byId.count(c.id)) { if (err) *err = "menu.json: the control id '" + c.id + "' is used twice"; m->tabs.clear(); m->byId.clear(); return false; }
                m->byId[c.id] = &c;
            }
    return true;
}

void State::reset(const Model& m) {
    v.clear();
    for (const Tab& t : m.tabs)
        for (const Item& it : t.items)
            for (const Control& c : it.controls) v[c.id] = c.def[pc ? 1 : 0];
}

double State::get(const std::string& id) const {
    auto it = v.find(id);
    return it == v.end() ? 0 : it->second;
}

namespace {
std::map<std::string, std::string> g_tr;
}

bool loadTranslations(const char* json, size_t len, std::string* err) {
    g_tr.clear();
    if (!json || len == 0) return true;
    Json root;
    std::string e;
    if (!parseJson(json, len, &root, &e)) { if (err) *err = "translation file, " + e; return false; }
    for (const auto& kv : root.o)
        if (kv.second.t == Json::Str) g_tr[kv.first] = kv.second.s;
    return true;
}

const std::string& tr(const std::string& english) {
    if (g_tr.empty()) return english;
    auto it = g_tr.find(english);
    return it == g_tr.end() ? english : it->second;
}

bool evalBool(const std::string& expr, const Model& m, const State& s, std::string* err) {
    if (expr.empty()) return true;
    Ctx ctx{ &m, &s };
    Expr ex(expr, ctx);
    const bool r = ex.orExpr().truthy();
    if (err) *err = ex.err;
    return r;
}

const std::string& controlLabel(const Control& c, const State& s) {
    return (s.pc && !c.labelPc.empty()) ? c.labelPc : c.label;
}

bool controlVisible(const Control& c, const Model& m, const State& s) {
    if (c.dev && !s.dev && (c.dev == 1 || (c.dev == 2 && !s.pc) || (c.dev == 3 && s.pc))) return false;
    return evalBool(c.show, m, s);
}

bool controlDisabled(const Control& c, const Model& m, const State& s) {
    return !c.disabled.empty() && evalBool(c.disabled, m, s);
}

bool itemVisible(const Item& it, const Model& m, const State& s) {
    if (it.group) return evalBool(it.show, m, s);
    if (!evalBool(it.show, m, s)) return false;
    for (const Control& c : it.controls)
        if (controlVisible(c, m, s)) return true;
    return false;
}

double controlToConfig(const Control& c, double ctl) {
    if (!c.options.empty()) {
        const int k = (int)ctl;
        return (k >= 0 && k < (int)c.options.size()) ? c.options[k].value : 0.0;
    }
    if (c.type == CT_TOGGLE) return ctl != 0 ? 1.0 : 0.0;
    return ctl * c.per / (double)c.div;
}

bool controlFromConfig(const Control& c, double cfg, double* ctl) {
    if (!c.options.empty()) {
        for (size_t k = 0; k < c.options.size(); k++) {
            const double d = c.options[k].value - cfg;
            if (d < 0.000001 && d > -0.000001) { *ctl = (double)k; return true; }
        }
        return false;
    }
    if (c.type == CT_TOGGLE) { *ctl = cfg != 0 ? 1.0 : 0.0; return true; }
    const double x = cfg * (double)c.div / c.per;
    *ctl = (double)(long)(x < 0 ? x - 0.5 : x + 0.5);
    return true;
}

bool optionVisible(const Control& c, int i, const Model& m, const State& s) {
    if (i < 0 || i >= (int)c.options.size()) return false;
    return c.options[i].show.empty() || evalBool(c.options[i].show, m, s);
}

std::vector<int> visibleOptions(const Control& c, const Model& m, const State& s) {
    std::vector<int> r;
    for (int i = 0; i < (int)c.options.size(); i++)
        if (optionVisible(c, i, m, s)) r.push_back(i);
    return r;
}

std::string sliderText(const Control& c, double value) {
    const int v = (int)value;
    if (!c.options.empty()) return (v >= 0 && v < (int)c.options.size()) ? tr(c.options[v].label) : std::string();
    std::string out;
    if (c.div <= 1) out = std::to_string(v);
    else {
        const int whole = v / c.div;
        out = std::to_string(whole);
        if (c.decimals > 0) {
            int rest = (v % c.div);
            if (rest < 0) rest = -rest;
            int scale = 1;
            for (int d = 0; d < c.decimals; d++) scale *= 10;
            const int frac = rest * scale / c.div;
            std::string f = std::to_string(frac);
            while ((int)f.size() < c.decimals) f = "0" + f;
            out += "." + f;
        }
    }
    return out + c.suffix;
}

const std::string& helpFor(const Control& c, double value) {
    static const std::string none;
    if (!c.helpBy.empty()) {
        std::string key;
        if (hasOptions(c)) {
            const int k = (int)value;
            if (k >= 0 && k < (int)c.options.size()) key = c.options[k].id;
        } else {
            key = value != 0 ? "1" : "0";
        }
        for (size_t i = 0; i < c.helpKeys.size(); i++)
            if (c.helpKeys[i] == key) return c.helpBy[i];
        return none;
    }
    return c.help;
}

}  // namespace menu
