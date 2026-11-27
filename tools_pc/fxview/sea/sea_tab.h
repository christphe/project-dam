#pragma once
/* fxview's sky / sea mode (issue #145): the GE+ sky, sea and level water surfaces, drawn through ge_render's own sky and water
 * passes on every backend, from the user's ROM (the ROM fxview opened: fx_rom) and the game's code (port/src/skyplus_core.c,
 * the environment records of src/game/bgfog.c). The viewer-only test variants (`#ifdef VIEWER` in port/shaders/sky and water)
 * come from a tool pack (ge_render_viewer_enable); the load mode repeats the passes 1-64 times and times them on the GPU.
 *
 * fxview hosts it (ge/fxview.cpp, --mode sea|water|sky):
 *   seaTabSetting    the mode's settings, before seaTabInit: fxview.ini's sea.<key> lines first, then the command line's --<key> value
 *   seaTabInit       after ge_render_open and fxRomOpen (textures from the ROM, the viewer variants, the level's defaults, the settings)
 *   seaTabFrame      every frame between ge_render_begin_frame and ge_render_end_frame, with the window framebuffer bound and
 *                    cleared (depth too): draws the sky, the sea or the water surfaces over the viewport
 *   seaTabUi         ImGui widgets only (inside fxview's panel)
 *   seaTabWriteSettings   its sea.<key> lines, appended to fxview.ini
 *   seaTabShutdown
 * The camera lives in a SeaTabCam fxview keeps; seaTabLook / seaTabMove turn fxview's input into it. */
#include <stddef.h>
#include <stdio.h>
struct SeaTabCam {
    float eye[3];
    float yaw, pitch;   /* radians, relative to the level's base view */
};
/* One setting (key without the leading --): true when the mode took it. fxview routes a key it also has (mode, view, scene) here
 * when the value is the mode's: mode sea|water|sky, view overview|pier|boxes|shore|pool|far, scene 0|1.
 *   mode sea|water|sky   level <name or stage number>   sea N  sky N  tex N (variants, cloud texture)   wtex FILE (a water texture file)
 *   scene 0|1 (the water mode's test scene)   view <name>   cam x,y,z,yaw,pitch   time T (fixed)   sway 0|1   rep N (load, 1..64)
 *   scale 0|1|2 (sky/sea pass size)   preset 0|1|2 (water surface preset)   data DIR (the game's data dir: water/<level>.ini)
 *   set "Name=value", or <Name> directly: a value of the settings table by its panel name. */
bool seaTabSetting(const char* key, const char* value);
/* viewerPack: the tool pack (Vulkan SPIR-V, deko3d DKSH; sea_pack.cmake), NULL = GL / none. romPath: the ROM fxview opened (its
 * directory is the game's data dir unless `data` was set). */
bool seaTabInit(const void* viewerPack, size_t viewerPackSize, const char* romPath);
void seaTabInitCam(SeaTabCam* cam);                       /* the level's view (or cam / view) */
void seaTabLook(SeaTabCam* cam, float dx, float dy);      /* turn right by dx and up by -dy (radians) */
void seaTabMove(SeaTabCam* cam, float fwd, float right, float up);   /* world units */
void seaTabFrame(float dt, SeaTabCam* cam, const int vp[4]);   /* vp: x, y (bottom-up), w, h of the viewport in pixels */
void seaTabUi(void);
const char* seaTabStatus(void);   /* one line: mode, variants, GPU time (the panel and the log) */
int seaTabRep(void);              /* load mode: passes per frame, 1..64 */
void seaTabSetRep(int n);
void seaTabCycleScale(void);      /* sky and sea pass size: full, half, quarter */
void seaTabToggleSway(void);
void seaTabToggleFreeze(void);
void seaTabWriteSettings(FILE* f);   /* sea.<key>=value lines */
void seaTabShutdown(void);
