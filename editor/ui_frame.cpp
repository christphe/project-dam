// ui_frame.cpp: see ui_app.h for how the editor UI is split.
#include "ui_app.h"
#include "ui_scripts.h"
#include <cmath>
#include <filesystem>

namespace ed {

void cameraInput(App &a, bool viewHovered)
{
    ImGuiIO &io = ImGui::GetIO();
    if (a.drag.mode != App::Drag::NONE || a.sunDrag) { a.looking = false; return; }
    if (viewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) a.looking = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) a.looking = false;
    if (a.looking) {
        a.cam.yaw += io.MouseDelta.x * 0.0035f;
        a.cam.pitch -= io.MouseDelta.y * 0.0035f;
        a.cam.pitch = std::max(-1.55f, std::min(1.55f, a.cam.pitch));
    }
    // middle button: pan, the point under the cursor follows the mouse
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) a.panning = false;
    // moves of earlier frames only: the press frame's delta is not part of the drag
    if (a.panning && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
        const float k = 2.0f * tanf(a.cam.fov * 0.5f) * a.panDist / std::max(a.vh, 1.0f);
        const Vec3 f = a.cam.forward(), r = a.cam.right();
        const Vec3 u = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
        for (int i = 0; i < 3; i++) a.cam.pos[i] += (-r[i] * io.MouseDelta.x + u[i] * io.MouseDelta.y) * k;
    }
    if (viewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        Vec3 o, d;
        a.cam.ray(io.MousePos.x - a.vx, io.MousePos.y - a.vy, a.vw, a.vh, o, d);
        Hit h = pickScene(a.doc, o, d);
        if (h.kind != Hit::NONE && h.t > 1.0f) a.panDist = h.t;
        else if (a.doc.hasLevel) {   // nothing under the cursor: the depth of the level's centre
            Vec3 c, f = a.cam.forward();
            for (int i = 0; i < 3; i++) c[i] = (a.doc.level.mn[i] + a.doc.level.mx[i]) * 0.5f - a.cam.pos[i];
            a.panDist = std::max(100.0f, c[0] * f[0] + c[1] * f[1] + c[2] * f[2]);
        } else a.panDist = 1000.0f;
        a.panning = true;
    }
    if (viewHovered && io.MouseWheel != 0) {
        // zoom toward the cursor: along the cursor ray, 15% of the distance to the scene point under it per notch
        // (wheel up = toward, down = away), so the point stays under the cursor and is never reached or passed
        Vec3 o, d;
        a.cam.ray(io.MousePos.x - a.vx, io.MousePos.y - a.vy, a.vw, a.vh, o, d);
        const Hit h = pickScene(a.doc, o, d);
        float step;
        if (h.kind != Hit::NONE && h.t > 1.0f) step = h.t * (1.0f - powf(0.85f, io.MouseWheel));
        else step = a.cam.speed * 0.25f * io.MouseWheel;     // nothing under the cursor: the current step, still along the cursor ray
        const Mat4 vpBefore = mat4Mul(a.cam.proj(a.vw / a.vh), a.cam.view());
        for (int i = 0; i < 3; i++) a.cam.pos[i] += d[i] * step;
        if (a.logCam && h.kind != Hit::NONE) {
            const Vec3 pt = { o[0] + d[0] * h.t, o[1] + d[1] * h.t, o[2] + d[2] * h.t };
            float b[2] = { 0, 0 }, n[2] = { 0, 0 };
            const Mat4 vpAfter = mat4Mul(a.cam.proj(a.vw / a.vh), a.cam.view());
            projectPoint(vpBefore, pt, 0, 0, a.vw, a.vh, b);
            projectPoint(vpAfter, pt, 0, 0, a.vw, a.vh, n);
            fprintf(stderr, "zoom: wheel %.1f, point %.1f %.1f %.1f at distance %.1f -> %.1f; its pixel before %.2f,%.2f after %.2f,%.2f (cursor %.1f,%.1f)\n",
                    io.MouseWheel, pt[0], pt[1], pt[2], h.t, h.t - step, b[0], b[1], n[0], n[1], io.MousePos.x - a.vx, io.MousePos.y - a.vy);
        }
    }
    if (io.WantTextInput) return;
    if (!a.looking) return;     // the movement keys fly only while the right button is held: Ctrl+C / G / X never move the camera
    float s = a.cam.speed * io.DeltaTime * (io.KeyShift ? 4.0f : 1.0f);
    Vec3 f = a.cam.forward(), r = a.cam.right();
    float fw = 0, rt = 0, up = 0;
    if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_Z) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) fw += 1;
    if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) fw -= 1;
    if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) rt += 1;
    if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_Q) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) rt -= 1;
    if (ImGui::IsKeyDown(ImGuiKey_Space)) up += 1;
    if (io.KeyCtrl && !io.KeyShift) up -= 1;
    for (int i = 0; i < 3; i++) a.cam.pos[i] += (f[i] * fw + r[i] * rt) * s;
    a.cam.pos[1] += up * s;
}

// The right panel: three tabs, each drawn by its own file (Selection, Mission, Mod).
void drawRightPanel(App &a, float x, float y, float w, float h)
{
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("Properties", nullptr, kPanel);
    if (ImGui::BeginTabBar("rtabs")) {
        static const char *names[3] = { "Selection", "Mission", "Mod" };
        for (int i = 0; i < 3; i++) {
            if (ImGui::BeginTabItem(names[i], nullptr, a.forceTab == i ? ImGuiTabItemFlags_SetSelected : 0)) {
                if (i == 0) drawSelectionTab(a);
                else if (i == 1) drawMissionTab(a);
                else drawModTab(a);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    a.forceTab = -1;
    ImGui::End();
}

void frameUi(App &a, int winW, int winH, bool &quit)
{
    ImGuiIO &io = ImGui::GetIO();
    const float W = (float)winW, H = (float)winH;
    const float menuH = ImGui::GetFrameHeight(), toolH = 40, leftW = 310, rightW = 330;
    const float statusH = a.showLog ? 130.0f : 26.0f;
    const float top = menuH + toolH;
    a.vx = leftW; a.vy = top; a.vw = std::max(W - leftW - rightW, 1.0f); a.vh = std::max(H - top - statusH, 1.0f);
    a.items = a.doc.items();
    a.frameNo++;
    {   // the thumbnail goes next to the open mission: in its mod's folder, else the mission file's own folder
        std::string t = a.doc.defaultThumbPath();
        if (t.empty() && a.doc.hasMission && !a.doc.missionPath.empty()) {
            const size_t s = a.doc.missionPath.find_last_of("/\\");
            t = (s == std::string::npos ? std::string() : a.doc.missionPath.substr(0, s + 1)) + "thumb.png";
        }
        if (!t.empty() && t != a.lastAutoThumb) { a.lastAutoThumb = t; snprintf(a.thumbPath, sizeof a.thumbPath, "%s", t.c_str()); }
    }
    thumbSync(a);

    drawMenuBar(a);
    drawToolbar(a, menuH, toolH);
    drawScenePanel(a, 0, top, leftW, H - top);
    drawRightPanel(a, W - rightW, top, rightW, H - top - statusH);
    drawStatusBar(a, leftW, H - statusH, W - leftW, statusH);
    drawSettingsWindow(a);
    drawPropsWindow(a);
    scriptsFrame(a);
    drawScriptEditorWindow(a);
    drawModals(a);

    const ImVec2 mp = io.MousePos;
    const bool inView = mp.x >= a.vx && mp.x < a.vx + a.vw && mp.y >= a.vy && mp.y < a.vy + a.vh;
    const bool viewHovered = inView && !io.WantCaptureMouse;
    cameraInput(a, viewHovered);

    Overlay ov;
    ov.dl = ImGui::GetBackgroundDrawList();
    ov.vp = mat4Mul(a.cam.proj(a.vw / a.vh), a.cam.view());
    ov.x = a.vx; ov.y = a.vy; ov.w = a.vw; ov.h = a.vh;

    // transform gizmo of the selection (pads of the level are fixed: no gizmo)
    const TransformInfo ti = a.doc.getTransform(a.doc.sel);
    GizmoGeo gg;
    if (ti.valid && ti.movable) gg = gizmoGeo(a, ov, ti);
    a.gizOk = gg.ok; a.gizC = gg.c;
    for (int i = 0; i < 3; i++) a.gizTip[i] = gg.tip[i];
    Vec3 mOrg, mDir;
    a.cam.ray(mp.x - a.vx, mp.y - a.vy, a.vw, a.vh, mOrg, mDir);

    if (a.sunDrag) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { std::string e; a.doc.restoreSnapshot(a.sunSnap, e); a.sunDrag = false; }
        else {
            sunUpdate(a, mOrg, mDir);
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) { a.doc.commit(a.sunSnap); a.sunDrag = false; }
        }
    } else if (a.drag.mode != App::Drag::NONE) {
        if (!io.WantTextInput && a.drag.mode == App::Drag::MOVE) {
            int k = ImGui::IsKeyPressed(ImGuiKey_X, false) ? 0 : ImGui::IsKeyPressed(ImGuiKey_Y, false) ? 1 : ImGui::IsKeyPressed(ImGuiKey_Z, false) ? 2 : -1;
            if (k >= 0) { a.drag.axis = a.drag.axis == k ? -1 : k; dragRebase(a, mOrg, mDir); }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            dragEnd(a, false);
            a.say("move cancelled");
        } else {
            dragUpdate(a, mOrg, mDir);
            if (!a.drag.keymove && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                fabsf(mp.x - a.drag.pressAt.x) < 4 && fabsf(mp.y - a.drag.pressAt.y) < 4) {
                a.drag.keymove = true;   // a click on a handle (no drag): it follows the mouse until the next click
            } else {
                const bool confirm = a.drag.keymove ? ImGui::IsMouseClicked(ImGuiMouseButton_Left) : ImGui::IsMouseReleased(ImGuiMouseButton_Left);
                if (confirm) dragEnd(a, true);
            }
        }
    } else {
        if (viewHovered && ti.valid && ti.movable && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
            dragBegin(a, 4, mOrg, mDir);
            a.drag.keymove = true;
        } else if (viewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && sunHit(a, ov, mp)) {
            a.sunDrag = true;
            a.sunSnap = a.doc.snapshot();
        } else if (viewHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            int hit = gg.ok ? gizmoHit(gg, mp) : 0;
            if (hit) { dragBegin(a, hit, mOrg, mDir); a.drag.pressAt = mp; }
            else { a.lmbDown = true; a.lmbX = mp.x; a.lmbY = mp.y; }
        }
        // click = press and release in the view without dragging
        if (a.lmbDown && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            a.lmbDown = false;
            if (inView && fabsf(mp.x - a.lmbX) < 4 && fabsf(mp.y - a.lmbY) < 4)
                applyClick(a, pickScene(a.doc, mOrg, mDir));
        }
    }

    // shortcuts
    if (!io.WantTextInput) {
        if (viewHovered && io.KeyCtrl && !io.KeyShift && !a.looking && a.drag.mode == App::Drag::NONE) {
            // Ctrl+C copies the selected guard / object, Ctrl+V pastes a new one under the cursor, Ctrl+D duplicates
            // it in place and starts a move that follows the mouse until the next click
            if (ImGui::IsKeyPressed(ImGuiKey_C, false)) copySelected(a);
            if (ImGui::IsKeyPressed(ImGuiKey_V, false)) pasteAtMouse(a, mOrg, mDir);
            if (ImGui::IsKeyPressed(ImGuiKey_D, false)) duplicateSelected(a, mOrg, mDir);
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { std::string e; a.doc.undo(e); }
        if (!a.modalOpen) {
            if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) doSave(a);
            if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false) && fileDialogAvailable()) doSaveAs(a);
            if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false) && fileDialogAvailable()) guarded(a, [&a] { doOpenDialog(a); });
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && (a.doc.sel.kind == Selection::ITEM || a.doc.sel.kind == Selection::ZONE)) deleteSelected(a);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) a.tool = App::T_SELECT;
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !io.KeyCtrl) {
            if (const Item *it = selectedItem(a)) { if (it->hasPos) focusOn(a, it->pos); }
            else if (a.doc.sel.kind == Selection::PAD) { if (const LPad *p = a.doc.level.pad(a.doc.sel.num)) focusOn(a, p->pos); }
        }
    }
    (void)quit;

    ov.dl->PushClipRect(ImVec2(a.vx, a.vy), ImVec2(a.vx + a.vw, a.vy + a.vh), true);
    drawOverlay(a, ov);
    drawScriptOverlay(a, ov);
    drawSun(a, ov);
    {   // the gizmo follows the selection live while it is dragged
        TransformInfo t2 = a.doc.getTransform(a.doc.sel);
        if (t2.valid && t2.movable) drawGizmo(a, ov, gizmoGeo(a, ov, t2));
    }
    ov.dl->PopClipRect();
}

} // namespace ed
