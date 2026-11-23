// ui_thumb.cpp: the Mission tab's Thumbnail section; see ui_app.h for how the editor UI is split.
//
// thumb.png is a 320x240 colour image, a plain crop of the 3D view. The game turns it into two grey pictures (the
// mission select film strip and the briefing polaroid); port/mission/mission_thumb.c does that conversion for the game
// and for the previews drawn here, so what is shown is what the player sees.
#include "ui_app.h"

#include <filesystem>

#include "mission_thumb.h"
#include "png.h"

namespace ed {

// Follows the thumb.png of the open mission: a new path starts from the file on disk (or nothing), sliders at rest.
void thumbSync(App &a)
{
    const std::string path = a.thumbPath;
    if (a.thumbBaseFor == path) return;
    a.thumbBaseFor = path;
    a.thumbBase.clear();
    a.thumbContrast = 1.0f; a.thumbBright = 0.0f; a.thumbDirty = false;
    a.thumbGen++;
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    if (path.empty() || !readPng(path.c_str(), w, h, px) || w <= 0 || h <= 0) return;
    a.thumbBase.assign((size_t)MTHUMB_W * MTHUMB_H * 4, 255);
    for (int y = 0; y < MTHUMB_H; y++)          // any size: nearest pixel onto the 320x240 master
        for (int x = 0; x < MTHUMB_W; x++) {
            const uint8_t *s = &px[((size_t)(y * h / MTHUMB_H) * (size_t)w + (size_t)(x * w / MTHUMB_W)) * 4];
            memcpy(&a.thumbBase[((size_t)y * MTHUMB_W + x) * 4], s, 4);
        }
}

bool thumbWriteBaked(App &a, const std::string &path, std::string &err)
{
    if (a.thumbBase.size() != (size_t)MTHUMB_W * MTHUMB_H * 4) { err = "no image yet: capture the view first"; return false; }
    std::vector<uint8_t> px = a.thumbBase;
    mthumbAdjustRgba(px.data(), MTHUMB_W * MTHUMB_H, a.thumbContrast, a.thumbBright);
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    if (!writePng(path.c_str(), MTHUMB_W, MTHUMB_H, px.data())) { err = "cannot write " + path; return false; }
    a.thumbDirty = false;
    invalidateThumbs(a);
    return true;
}

static void freePreviews(App &a)
{
    for (uint32_t *t : { &a.pvFull, &a.pvStrip, &a.pvPol })
        if (*t) { editorGlDeleteTexture(*t); *t = 0; }
}

// The three pictures from the base image with contrast and brightness: the colour image, the film strip (68x44 grey, stored
// bottom-up like the game's texture, turned upright here) and the polaroid (decoded from its two 128x64 halves).
static void rebuildPreviews(App &a)
{
    freePreviews(a);
    std::vector<uint8_t> px = a.thumbBase;
    mthumbAdjustRgba(px.data(), MTHUMB_W * MTHUMB_H, a.thumbContrast, a.thumbBright);
    a.pvFull = editorGlUploadTexture(MTHUMB_W, MTHUMB_H, px.data());

    std::vector<uint8_t> grey((size_t)MTHUMB_W * MTHUMB_H);
    mthumbGrey(px.data(), MTHUMB_W, MTHUMB_H, 4, MTHUMB_W, MTHUMB_H, grey.data());

    uint8_t strip[MTHUMB_STRIP_W * MTHUMB_STRIP_H];
    mthumbStrip(grey.data(), strip, MTHUMB_STRIP_W);
    std::vector<uint8_t> srgba((size_t)MTHUMB_STRIP_W * MTHUMB_STRIP_H * 4);
    for (int y = 0; y < MTHUMB_STRIP_H; y++)
        for (int x = 0; x < MTHUMB_STRIP_W; x++) {
            const uint8_t v = strip[(MTHUMB_STRIP_H - 1 - y) * MTHUMB_STRIP_W + x];
            uint8_t *o = &srgba[((size_t)y * MTHUMB_STRIP_W + x) * 4];
            o[0] = o[1] = o[2] = v; o[3] = 255;
        }
    a.pvStrip = editorGlUploadTexture(MTHUMB_STRIP_W, MTHUMB_STRIP_H, srgba.data());

    uint8_t img[MTHUMB_POL_SIZE * MTHUMB_POL_SIZE], h0[MTHUMB_POL_HALF_BYTES], h1[MTHUMB_POL_HALF_BYTES], dec[MTHUMB_POL_SIZE * MTHUMB_POL_SIZE];
    mthumbPolaroidImage(grey.data(), img);
    mthumbPolaroidHalf(img, 0, h0);
    mthumbPolaroidHalf(img, 1, h1);
    mthumbPolaroidDecode(h0, h1, dec);
    std::vector<uint8_t> prgba((size_t)MTHUMB_POL_SIZE * MTHUMB_POL_SIZE * 4);
    for (int i = 0; i < MTHUMB_POL_SIZE * MTHUMB_POL_SIZE; i++) {
        prgba[(size_t)i * 4] = prgba[(size_t)i * 4 + 1] = prgba[(size_t)i * 4 + 2] = (uint8_t)(dec[i] * 17);
        prgba[(size_t)i * 4 + 3] = 255;
    }
    a.pvPol = editorGlUploadTexture(MTHUMB_POL_SIZE, MTHUMB_POL_SIZE, prgba.data());
}

static void picture(uint32_t tex, ImVec2 size)
{
    if (tex) ImGui::Image((ImTextureID)(intptr_t)tex, size);
    else ImGui::Dummy(size);
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(90, 90, 100, 255));
}

void thumbSection(App &a)
{
    if (!ImGui::CollapsingHeader("Thumbnail", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::BeginDisabled(!a.doc.captureFn);
    if (ImGui::Button("Capture the view")) doCapture(a);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("a plain 4:3 crop");
    ImGui::TextDisabled("%s", shortPath(a.thumbPath, 44).c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", a.thumbPath);

    if (a.thumbBase.empty()) { ImGui::TextWrapped("No thumbnail yet. Fly to a good view and capture it: the game shows it in the mission select and the briefing."); return; }

    bool changed = false;
    ImGui::SetNextItemWidth(-110);
    changed |= ImGui::SliderFloat("Contrast", &a.thumbContrast, 0.5f, 2.0f, "%.2f");
    ImGui::SetNextItemWidth(-110);
    changed |= ImGui::SliderFloat("Brightness", &a.thumbBright, -80.0f, 80.0f, "%.0f");
    if (changed) a.thumbDirty = true;
    if (ImGui::SmallButton("Reset")) { a.thumbContrast = 1.0f; a.thumbBright = 0.0f; a.thumbDirty = true; }
    ImGui::SameLine();
    ImGui::BeginDisabled(!a.thumbDirty);
    if (ImGui::SmallButton("Write thumb.png now")) {
        std::string e;
        if (thumbWriteBaked(a, a.thumbPath, e)) a.say("thumbnail written: %s", a.thumbPath);
        else a.say("capture: %s", e.c_str());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", a.thumbDirty ? "baked on save" : "");

    char key[96];
    snprintf(key, sizeof key, "%d|%s|%.3f|%.1f", a.thumbGen, a.thumbBaseFor.c_str(), a.thumbContrast, a.thumbBright);
    if (a.pvKey != key || !a.pvFull) { rebuildPreviews(a); a.pvKey = key; }

    ImGui::Spacing();
    ImGui::TextUnformatted("What the player sees");
    picture(a.pvFull, ImVec2(160, 120));
    ImGui::SameLine();
    picture(a.pvPol, ImVec2(128, 128));
    ImGui::TextDisabled("thumb.png           briefing polaroid");
    picture(a.pvStrip, ImVec2(MTHUMB_STRIP_W * 2, MTHUMB_STRIP_H * 2));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextDisabled("mission select");
    ImGui::TextDisabled("film strip, 68x44");
    ImGui::TextDisabled("grey (I8)");
    ImGui::EndGroup();
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("The polaroid has 16 greys (I4) in two 128x64 halves, as the game draws it.");
    ImGui::PopTextWrapPos();
}

} // namespace ed
