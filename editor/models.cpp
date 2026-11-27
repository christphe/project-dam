#include "models.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

extern "C" {
#include "cJSON.h"
}

static bool readFile(const std::string &path, std::string &out)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[65536];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

bool ModelLib::open(const std::vector<std::string> &roots, const TexSet *texs)
{
    m_open = false;
    m_texs = texs;
    m_entries.clear(); m_index.clear(); m_cache.clear(); m_dir.clear();
    namespace fs = std::filesystem;
    for (const std::string &root : roots) {
        const std::string dir = (fs::path(root) / "models").string();
        std::string text;
        if (!readFile((fs::path(dir) / "models.json").string(), text)) continue;
        cJSON *j = cJSON_Parse(text.c_str());
        if (!j) { printf("[editor] models: %s/models.json is not valid JSON\n", dir.c_str()); continue; }
        const cJSON *arr = cJSON_GetObjectItemCaseSensitive(j, "models"), *e;
        cJSON_ArrayForEach(e, arr) {
            const cJSON *m = cJSON_GetObjectItemCaseSensitive(e, "model"), *g = cJSON_GetObjectItemCaseSensitive(e, "gltf");
            if (!cJSON_IsNumber(m)) continue;
            ModelEntry me;
            me.model = (int)m->valuedouble;
            const cJSON *f = cJSON_GetObjectItemCaseSensitive(e, "file"), *n = cJSON_GetObjectItemCaseSensitive(e, "name");
            const cJSON *bs = cJSON_GetObjectItemCaseSensitive(e, "base_scale"), *tr = cJSON_GetObjectItemCaseSensitive(e, "triangles");
            const cJSON *st = cJSON_GetObjectItemCaseSensitive(e, "status");
            if (cJSON_IsString(f)) me.file = f->valuestring;
            if (cJSON_IsString(n)) me.name = n->valuestring;
            if (cJSON_IsString(g)) me.gltf = g->valuestring;
            if (cJSON_IsNumber(bs) && bs->valuedouble > 0) me.baseScale = (float)bs->valuedouble;
            if (cJSON_IsNumber(tr)) me.triangles = (int)tr->valuedouble;
            me.exported = !cJSON_IsString(st) || strcmp(st->valuestring, "exported") == 0;
            if (me.gltf.empty()) me.exported = false;
            m_index[me.model] = (int)m_entries.size();
            m_entries.push_back(std::move(me));
        }
        cJSON_Delete(j);
        if (m_entries.empty()) continue;
        m_dir = dir;
        m_open = true;
        return true;
    }
    return false;
}

const ModelEntry *ModelLib::entry(int model) const
{
    auto it = m_index.find(model);
    return it == m_index.end() ? nullptr : &m_entries[(size_t)it->second];
}

const Decor *ModelLib::get(int model) const
{
    if (!m_open) return nullptr;
    auto c = m_cache.find(model);
    if (c != m_cache.end()) return c->second.get();
    const ModelEntry *e = entry(model);
    std::unique_ptr<Decor> d;
    if (e && e->exported) {
        d.reset(new Decor());
        std::string msg;
        if (decorLoad(m_dir + "/" + e->gltf, 0, *d, msg, m_texs, true) != DECOR_OK) {
            printf("[editor] models: model %d (%s) cannot be read: %s\n", model, e->file.c_str(), msg.c_str());
            fflush(stdout);
            d.reset();
        }
    }
    const Decor *r = d.get();
    m_cache[model] = std::move(d);
    return r;
}
