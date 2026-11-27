// The static model export (port/export/, issue #119): <root>/models/models.json + <root>/models/<name>/<name>.gltf, one glTF per model
// number in model units. The editor draws the props from it (and lists every model in the Props browser's "All models").
// Pure CPU, no GL. Models are read on first use and kept.
#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "decor.h"

struct ModelEntry {
    int model = -1;
    std::string file, name;      // "PdoorstatgateZ", "doorstatgate"
    std::string gltf;            // path relative to the models folder
    float baseScale = 0.1f;      // model unit -> world unit (before the setup record's extra scale)
    int triangles = 0;
    bool exported = true;        // models.json status
};

class ModelLib {
public:
    // Reads <root>/models/models.json for the first root that has one. Returns false (and keeps nothing) when none has.
    // `texs` (not owned, may be null) is what the models' textures are resolved through: the open mod over the chosen texture pack.
    bool open(const std::vector<std::string> &roots, const TexSet *texs = nullptr);
    bool isOpen() const { return m_open; }
    const std::string &dir() const { return m_dir; }
    const std::vector<ModelEntry> &entries() const { return m_entries; }   // by model number
    const ModelEntry *entry(int model) const;
    // The model's geometry (default scene, model units), or nullptr when it is not in models.json or cannot be read (logged once).
    const Decor *get(int model) const;
    // A model has no glTF: counted/logged once by the caller.
    int loaded() const { return (int)m_cache.size(); }

private:
    bool m_open = false;
    const TexSet *m_texs = nullptr;
    std::string m_dir;
    std::vector<ModelEntry> m_entries;
    std::map<int, int> m_index;                                     // model number -> index in m_entries
    mutable std::map<int, std::unique_ptr<Decor>> m_cache;          // null = failed
};
