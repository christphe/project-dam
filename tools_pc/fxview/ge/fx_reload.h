// fxview, PC only: hot reload of the effect shaders (port/shaders/fx/*.glsl).
//   GL:     the four files are given to ge_render_fx_reload_gl (the backend compiles the effect functions into its effect programs).
//   Vulkan: dkshgen (compiled in, tools_pc/dkshgen/dkshgen.cpp) writes the packs' GLSL, an external glslangValidator (env GLSLANG, else PATH)
//           makes SPIR-V of every effect fragment shader, ge_render_fx_replace_spirv swaps it into the program.
// On an error the previous shader stays and the log (with the file named: "#line 1 N" = common / bullets / bursts / impacts) is the status.
#pragma once
#include <stdint.h>
#include <string>

// dir = the effect folder (port/shaders/fx), work = a scratch folder (with a trailing '/'). Returns false when nothing can be watched.
bool fxReloadInit(const std::string& dir, const std::string& work);
// Checks the files (at most every 0.3 s); reloads when one is newer. force: reload now (GL does it once at start: GL has no effect shaders otherwise).
void fxReloadPoll(bool force);
// A program was just created (Vulkan): give it the reloaded shader if there is one.
void fxReloadApply(uint64_t id0, uint32_t id1);
const std::string& fxReloadStatus();
bool fxReloadFailed();
int fxReloadCount();
