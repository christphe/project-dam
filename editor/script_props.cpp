#include "script_props.h"

#include "Luau/Allocator.h"
#include "Luau/Ast.h"
#include "Luau/ParseOptions.h"
#include "Luau/Parser.h"

PropsSchema parsePropsSchema(const std::string &source)
{
    PropsSchema out;
    Luau::Allocator alloc;
    Luau::AstNameTable names(alloc);
    Luau::ParseOptions opts;
    Luau::ParseResult pr = Luau::Parser::parse(source.data(), source.size(), names, alloc, opts);
    if (!pr.errors.empty()) out.error = pr.errors[0].getMessage();
    if (!pr.root) return out;
    for (Luau::AstStat *st : pr.root->body) {
        Luau::AstStatTypeAlias *al = st->as<Luau::AstStatTypeAlias>();
        if (!al || std::string(al->name.value) != "Props") continue;
        Luau::AstTypeTable *tt = al->type ? al->type->as<Luau::AstTypeTable>() : nullptr;
        if (!tt) continue;
        out.found = true;
        for (const Luau::AstTableProp &p : tt->props) {
            PropField f;
            f.name = p.name.value;
            f.line = p.location.begin.line + 1;
            f.type = "?";
            Luau::AstType *t = p.type;
            if (Luau::AstTypeUnion *u = t ? t->as<Luau::AstTypeUnion>() : nullptr) {
                Luau::AstType *rest = nullptr;
                int others = 0;
                for (Luau::AstType *m : u->types) {
                    Luau::AstTypeReference *r = m->as<Luau::AstTypeReference>();
                    if (m->is<Luau::AstTypeOptional>() || (r && !r->prefix && std::string(r->name.value) == "nil")) f.optional = true;
                    else { rest = m; others++; }
                }
                t = others == 1 ? rest : nullptr;
            }
            if (t) {
                if (Luau::AstTypeReference *r = t->as<Luau::AstTypeReference>()) { if (!r->prefix) f.type = r->name.value; }
            }
            out.fields.push_back(f);
        }
        break;
    }
    return out;
}
