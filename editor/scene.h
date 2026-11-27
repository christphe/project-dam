// Replays a level export through ge_render's draw API (the OpenGL or the Vulkan backend): the rooms and the
// models of the props look as they do in the game.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "view.h"

struct ShaderProgram;

class Scene {
public:
    // ge_render_open has run. Sets the window framebuffer's size.
    bool init(int w, int h, std::string &err);
    // The drawable size changed.
    void resize(int w, int h);
    // Uploads textures, meshes and shader programs of a level (nullptr unloads).
    void setLevel(const Level *lv);
    // Draws the window background and the level into the viewport rectangle (x, y from the top), inside a ge_render frame
    // the caller began (ge_render_begin_frame ... ge_render_end_frame, the GUI draws after it).
    void render(const Doc &doc, const Camera &cam, int winW, int winH, int vx, int vy, int vw, int vh, bool showRooms,
                bool showProps);
    // Whole-frame offscreen rendering (editor_gl.h); the caller copies the frame to the window after its GUI.
    void setOffscreen(bool on) { m_offscreen = on; }
    bool offscreen() const { return m_offscreen; }
    // One owner's recorded model alone, in a size x size picture (RGBA, top-down): the Props browser's thumbnails. Draws into the
    // window frame like render(), so call it between two frames, never inside one.
    bool renderOwner(int owner, int winW, int winH, int size, std::vector<uint8_t> &rgba);
    // The same for one glTF model (the static model export) in model units, alone: the Props browser's thumbnails of the models.
    bool renderModel(int model, int winW, int winH, int size, std::vector<uint8_t> &rgba);
    int runCount() const { return (int)m_runs.size(); }
    int drawn() const { return m_drawn; }

private:
    bool m_offscreen = true;
    struct GpuRun {
        uint32_t mesh = 0;
        ShaderProgram *prg = nullptr;
        int tex[2] = { -1, -1 };
        bool usable = false;
    };
    void drawRun(int runIndex, const Mat4 &vp, const Mat4 &placement);
    // The glTF decor (Level::decor): one mesh per primitive, drawn as "texture x shade colour" (see scene.cpp).
    struct DecorDraw {
        uint32_t mesh = 0;
        ShaderProgram *prg = nullptr;
        int tex = -1;                // index into m_decorTex
        int cms = 0, cmt = 0, zmode = 0;
        uint32_t nverts = 0;
        bool doubleSided = false, blend = false;
    };
    void buildDraws(const Decor &dc, std::vector<uint32_t> &texIds, std::vector<DecorDraw> &out);
    void drawDraws(const std::vector<DecorDraw> &draws, const std::vector<uint32_t> &texIds, bool blendPass, const Mat4 &mtx);
    void drawDecor(bool blendPass, const Mat4 &vp) { drawDraws(m_decor, m_decorTex, blendPass, vp); }
    bool m_useDecor = false;
    // The props' glTF models (Level::models), uploaded on first use: the same draws as the decor, in model units.
    struct ModelGpu { std::vector<uint32_t> tex; std::vector<DecorDraw> draws; };
    const ModelGpu *modelGpu(int model);
    void clearModels();
    std::map<int, ModelGpu> m_models;
    bool m_propsLogged = false;
    struct PropInst { const ModelGpu *g; Mat4 m; };
    std::vector<PropInst> m_propModels;   // this frame's glTF props: their translucent draws come after the decor's
    std::vector<uint32_t> m_decorTex;
    std::vector<DecorDraw> m_decor;

    const Level *m_level = nullptr;
    std::vector<uint32_t> m_tex;
    std::vector<GpuRun> m_runs;
    std::vector<std::vector<int>> m_runsOfOwner;
    ShaderProgram *m_cur = nullptr;
    bool m_inited = false;
    int m_drawn = 0;
};
