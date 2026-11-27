#include "texset.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

static std::shared_ptr<TexPackSrc> openShared(const std::string &root)
{
    TexPackSrc *s = root.empty() ? nullptr : texpackSrcOpen(root.c_str());
    return std::shared_ptr<TexPackSrc>(s, [](TexPackSrc *p) { texpackSrcClose(p); });
}

void TexSet::open(const std::string &modRoot, const std::string &packRoot, const std::string &baseRoot)
{
    m_mod.reset(modRoot.empty() ? nullptr : texpackSrcOpen(modRoot.c_str()));
    m_modRoot = modRoot;
    if (m_opened && packRoot == m_packRoot && baseRoot == m_baseRoot) return;   // the pack is read once
    m_opened = true;
    m_packRoot = packRoot;
    m_baseRoot = baseRoot;
    m_note.clear();
    m_base = openShared(baseRoot);
    m_pack.reset();
    if (!packRoot.empty()) {
        if (packRoot == baseRoot) m_pack = m_base;
        else {
            std::shared_ptr<TexPackSrc> p = openShared(packRoot);
            if (!p) m_note = "texture pack " + packRoot + " cannot be read: ge007 is used";
            else if (!m_base) m_note = "texture pack " + packRoot + ": no ge007.ptexpack to check it against, so it is not used";
            else {
                const int missing = texpackMissing(p.get(), m_base.get());
                if (missing > 0) m_note = "texture pack " + packRoot + " is incomplete: " + std::to_string(missing) + " textures missing, ge007 is used";
                else m_pack = p;
            }
        }
    }
    if (!m_pack) m_pack = m_base;   // exactly one pack is always chosen: ge007, when there is one
    if (!m_note.empty()) fprintf(stderr, "[editor] %s\n", m_note.c_str());
}

std::string baseTexturePackRoot(const std::string &texturePacksDir, const std::string &exportRoot)
{
    std::error_code ec;
    if (!texturePacksDir.empty()) {
        const fs::path p = fs::path(texturePacksDir) / "ge007.ptexpack";
        if (fs::exists(p, ec)) return p.generic_string();
    }
    if (!exportRoot.empty()) {
        const fs::path p = fs::path(exportRoot) / "ge007.ptexpack";
        if (fs::exists(p, ec)) return p.generic_string();
    }
    return "";
}

std::string texturePackPath(const std::string &pref, const std::string &texturePacksDir, const std::string &exportRoot)
{
    if (pref.empty() || pref == "ge007.ptexpack") return baseTexturePackRoot(texturePacksDir, exportRoot);
    return texturePacksDir + "/" + pref;
}

std::vector<PackChoice> listTexturePacks(const std::string &texturePacksDir, const std::string &exportRoot)
{
    std::vector<PackChoice> out;
    const std::string baseRoot = baseTexturePackRoot(texturePacksDir, exportRoot);
    std::shared_ptr<TexPackSrc> base = openShared(baseRoot);

    PackChoice b;
    b.file = "ge007.ptexpack";
    b.path = baseRoot;
    b.base = true;
    b.name = "GoldenEye 007";
    if (base) {
        const TexPackMeta *m = texpackSrcMeta(base.get());
        if (m->name[0]) b.name = m->name;
        b.author = m->author;
        b.description = m->description;
        if (!texpackSrcHasTexturesJson(base.get())) b.reason = "no textures.json: export again";
    } else {
        b.reason = "not exported yet";
    }
    out.push_back(b);

    std::vector<TexPackInfo> infos(256);
    const int n = std::min(texpackScan(texturePacksDir.c_str(), infos.data(), (int)infos.size()), (int)infos.size());
    for (int i = 0; i < n; i++) {
        if (std::string(infos[(size_t)i].file) == "ge007.ptexpack") continue;   // the base is listed first
        PackChoice c;
        c.file = infos[(size_t)i].file;
        c.path = texturePacksDir + "/" + c.file;
        c.name = infos[(size_t)i].name;
        c.author = infos[(size_t)i].author;
        c.description = infos[(size_t)i].description;
        std::unique_ptr<TexPackSrc, void (*)(TexPackSrc *)> p(texpackSrcOpen(c.path.c_str()), texpackSrcClose);
        if (!p) c.reason = "cannot be read";
        else if (!base || !texpackSrcHasTexturesJson(base.get())) c.reason = "no ge007 pack to check it against";
        else {
            c.missing = texpackMissing(p.get(), base.get());
            if (c.missing > 0) c.reason = "incomplete: " + std::to_string(c.missing) + " textures missing";
        }
        out.push_back(std::move(c));
    }
    return out;
}
