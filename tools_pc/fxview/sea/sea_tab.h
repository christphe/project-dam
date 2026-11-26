#pragma once
/* The sea and sky tab of fxview (issue #145), a self-contained module: the GE+ sky and sea of the Frigate capture
 * (data/sea_dump.bin, git-ignored, written by the game's GE_SEADUMP=1) and the level-water surface mode, drawn through
 * ge_render's own sky and water passes (GeRenderPasses sky_stage / sky_pass, PortSkyPlus; the water through draw_triangles
 * with the water combiner), on every backend. The viewer-only test variants (`#ifdef VIEWER` in port/shaders/sky and water) are
 * switched on with ge_render_viewer_enable; the load mode repeats the passes 1-64 times and times them with the GPU timer.
 *
 * The host owns the window, the ImGui frame and the camera input:
 *   seaTabParseArg   in the command-line loop (before ge_render_open)
 *   seaTabInit       after ge_render_open (loads the capture, builds the textures, enables the viewer variants)
 *   seaTabFrame      every frame between ge_render_begin_frame and ge_render_end_frame, with the window framebuffer bound and
 *                    cleared (depth too): draws the sky, the sea or the water surfaces over the viewport
 *   seaTabUi         ImGui widgets only (inside a window of the host's), no backend
 *   seaTabShutdown
 * The camera lives in a SeaTabCam the host keeps; seaTabLook / seaTabMove turn input into it (the capture's heading is yaw 0). */

#include <stddef.h>

struct SeaTabCam {
    float eye[3];
    float yaw, pitch;   /* radians, relative to the capture's view */
};

/* A command-line argument at argv[*i]: true when the tab took it (and any value after it; *i is advanced past them).
 *   --rom FILE  the user's ROM (else N64_ROM)     --data DIR  the game's data dir ($S: water/<level>.ini)   --imagelist FILE
 *   --ini FILE   settings to load at start (the same file the UI saves)
 *   --mode sea|water|sky   --level frigate|dam|caverns|complex|surface|streets|runway|temple|cradle   --sea N  --sky N  --tex N   the variants and the cloud texture
 *   --wtex FILE  the level's water texture (PNG or TGA)   --scene 0|1   the water mode's test scene
 *   --cam x,y,z,yaw,pitch   --view overview|pier|boxes|shore|pool|far   --time T (fixed time)   --sway 0|1
 *   --rep N      load mode, 1..64                     --scale 0|1|2   sky and sea pass size: full, half, quarter
 *   --set "Name=value"   any setting of the table by its name (repeatable). */
bool seaTabParseArg(int argc, char** argv, int* i);
/* viewerPack: the tool pack (Vulkan SPIR-V, deko3d DKSH; built by this directory's CMake, see sea_pack.h), NULL = GL / none. */
bool seaTabInit(const void* viewerPack, size_t viewerPackSize);
void seaTabInitCam(SeaTabCam* cam);                       /* the capture's view (or --cam / --view) */
void seaTabLook(SeaTabCam* cam, float dx, float dy);      /* turn right by dx and up by -dy (radians) */
void seaTabMove(SeaTabCam* cam, float fwd, float right, float up);   /* world units */
void seaTabFrame(float dt, SeaTabCam* cam, const int vp[4]);   /* vp: x, y (bottom-up), w, h of the viewport in pixels */
void seaTabUi(void);
const char* seaTabStatus(void);   /* one line: mode, variants, GPU time (a window title or a log line) */
int seaTabRep(void);              /* load mode: passes per frame, 1..64 */
void seaTabSetRep(int n);
void seaTabCycleScale(void);      /* sky and sea pass size: full, half, quarter */
void seaTabToggleSway(void);
void seaTabToggleFreeze(void);
void seaTabSaveIni(const char* path);
void seaTabLoadIni(const char* path);
void seaTabShutdown(void);
