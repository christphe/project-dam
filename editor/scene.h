// Replays a level export through fast3d's OpenGL backend (gfx_opengl_api): the rooms and the
// models of the props look as they do in the game.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "view.h"

struct ShaderProgram;

class Scene {
public:
    // A GL 3.3+ context must be current. Initialises the fast3d backend.
    bool init(int w, int h, std::string &err);
    // Uploads textures, meshes and shader programs of a level (nullptr unloads).
    void setLevel(const Level *lv);
    // Draws the window background and the level into the viewport rectangle (x, y from the top).
    void render(const Doc &doc, const Camera &cam, int winW, int winH, int vx, int vy, int vw, int vh, bool showRooms,
                bool showProps);
    // Whole-frame offscreen rendering (editor_gl.h); the caller copies the frame to the window after its GUI.
    void setOffscreen(bool on) { m_offscreen = on; }
    bool offscreen() const { return m_offscreen; }
    // One owner's recorded model alone, in a size x size picture (RGBA, top-down): the Props browser's thumbnails. Draws into the
    // window frame like render(), so call it between two frames, never inside one.
    bool renderOwner(int owner, int winW, int winH, int size, std::vector<uint8_t> &rgba);
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

    const Level *m_level = nullptr;
    std::vector<uint32_t> m_tex;
    std::vector<GpuRun> m_runs;
    std::vector<std::vector<int>> m_runsOfOwner;
    ShaderProgram *m_cur = nullptr;
    bool m_inited = false;
    int m_drawn = 0;
};
