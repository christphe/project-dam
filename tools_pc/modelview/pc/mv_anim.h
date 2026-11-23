// modelview: pose a character like the game. Skeleton and animation data come from the repo's decomp assets
// (assets/embedded/skeletons, assets/obseg/chr/*/modelFileHeader.inc.c, assets/animationtable_*.c), read at run
// time; the node tree and joint origins come from the model image read out of the user's ROM.
// Ported from src/game/model.c: process_02_position, modelBuildGroupMatrices, sub_GAME_7F06DEC0, modelAnimReadBitsAsU16Angle.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

struct MvAnimInfo { std::string name; std::string entry; int frames = 0; int width = 0; int frameBits = 0; };

bool mv_anim_init(const std::string& repoRoot);                 // reads the animation table headers; false = assets not found
const std::vector<MvAnimInfo>& mv_anim_list();
int mv_anim_find(const std::string& name);                      // index or -1
// The skeleton ("guard", ...) a character file uses, "" if unknown.
std::string mv_anim_skeleton_of(const std::string& modelName);
// The pose matrices (12 floats per matrix id, see MvMesh::pose) of a model image at one animation frame.
// anim < 0 = no animation (joints at rest). Returns false (note says why) when the skeleton does not match the animation set.
bool mv_anim_pose(const std::vector<uint8_t>& image, int ns, int nt, const std::string& modelName, int anim, int frame, int njoints,
                  std::vector<float>& pose, std::string& note);
int mv_anim_selftest();                                         // bit reader and matrix checks, no ROM needed
