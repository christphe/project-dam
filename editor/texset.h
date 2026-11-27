// The texture sources the editor resolves a texture through (docs/specs/texture-packs.md): the open mod (partial, merged field by
// field) over the chosen texture pack (complete). ge007.ptexpack, the game's own pack, is the default and the reference the other
// packs are checked against. Pure CPU, no GL.
#pragma once

#include <memory>
#include <string>
#include <vector>

extern "C" {
#include "texpacks.h"
}

// The sources opened for one (mod, pack) choice. Read-only once open; decorLoad and the model library take a pointer to it.
class TexSet {
public:
    TexSet() = default;
    TexSet(const TexSet &) = delete;
    TexSet &operator=(const TexSet &) = delete;

    // Opens the mod (folder or .pmod, "" none) afresh every call, and the pack when its root changed. `packRoot` is the chosen
    // pack, `baseRoot` ge007.ptexpack (both "" when there is none). A chosen pack that is not complete against the base is refused:
    // the base is used instead and note() says why. Without any base or pack, pack() is null (the glTF's own pictures are used).
    void open(const std::string &modRoot, const std::string &packRoot, const std::string &baseRoot);
    const TexPackSrc *mod() const { return m_mod.get(); }
    const TexPackSrc *pack() const { return m_pack.get(); }
    const TexPackSrc *base() const { return m_base.get(); }
    const std::string &note() const { return m_note; }
    // What the open sources were made from, to see whether another choice needs a reload.
    std::string signature() const { return m_modRoot + "|" + m_packRoot + "|" + m_baseRoot; }
    static std::string signatureOf(const std::string &mod, const std::string &pack, const std::string &base) { return mod + "|" + pack + "|" + base; }

private:
    struct Closer { void operator()(TexPackSrc *s) const { texpackSrcClose(s); } };
    std::unique_ptr<TexPackSrc, Closer> m_mod;
    std::shared_ptr<TexPackSrc> m_pack, m_base;
    std::string m_modRoot, m_packRoot, m_baseRoot, m_note;
    bool m_opened = false;
};

// One entry of the pack list.
struct PackChoice {
    std::string file;         // "ge007.ptexpack", "hd.ptexpack": what the preference holds
    std::string path;         // the pack's root
    std::string name, author, description;
    bool base = false;        // ge007.ptexpack
    int missing = 0;          // textures it lacks against ge007; > 0: not selectable
    std::string reason;       // why it is not selectable ("" when it is)
    bool usable() const { return reason.empty(); }
};

// ge007.ptexpack's root: <texturepacks>/ge007.ptexpack when the game wrote it, else <exportRoot>/ge007.ptexpack; "" when neither exists.
std::string baseTexturePackRoot(const std::string &texturePacksDir, const std::string &exportRoot);

// The packs to choose from: ge007 first (always, even when missing: then not usable), then the other packs of the folder, each checked
// against ge007 (opens every pack: not cheap, call it when the list is shown).
std::vector<PackChoice> listTexturePacks(const std::string &texturePacksDir, const std::string &exportRoot);

// The root a preference value ("" = ge007, else a file name of the folder) stands for.
std::string texturePackPath(const std::string &pref, const std::string &texturePacksDir, const std::string &exportRoot);
