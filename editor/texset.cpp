#include "texset.h"

#include <algorithm>
#include <cstdio>

void TexSet::open(const std::string &modRoot, const std::string &packRoot, const std::string &baseRoot)
{
    m_modRoot = modRoot;
    if (geTexSetOpen(m_set, modRoot.c_str(), packRoot.c_str(), baseRoot.c_str())) {   // the pack was read (again)
        m_packRoot = packRoot;
        m_baseRoot = baseRoot;
        m_note = geTexSetNote(m_set);
        if (!m_note.empty()) fprintf(stderr, "[editor] %s\n", m_note.c_str());
    }
}

std::string baseTexturePackRoot(const std::string &texturePacksDir, const std::string &exportRoot)
{
    char root[1024];
    geTexBaseRoot(texturePacksDir.c_str(), exportRoot.c_str(), root, sizeof root);
    return root;
}

std::string texturePackPath(const std::string &pref, const std::string &texturePacksDir, const std::string &exportRoot)
{
    char root[1100];
    geTexPackPath(pref.c_str(), texturePacksDir.c_str(), exportRoot.c_str(), root, sizeof root);
    return root;
}

std::vector<PackChoice> listTexturePacks(const std::string &texturePacksDir, const std::string &exportRoot)
{
    std::vector<GeTexPackChoice> raw(257);
    const int n = std::min(geTexPackList(texturePacksDir.c_str(), exportRoot.c_str(), raw.data(), (int)raw.size()), (int)raw.size());
    std::vector<PackChoice> out;
    for (int i = 0; i < n; i++) {
        const GeTexPackChoice &c = raw[(size_t)i];
        PackChoice p;
        p.file = c.file;
        p.path = c.path;
        p.name = c.name;
        p.author = c.author;
        p.description = c.description;
        p.base = c.base != 0;
        p.missing = c.missing;
        p.reason = c.reason;
        out.push_back(std::move(p));
    }
    return out;
}
