# fxview's sky / sea mode (issue #145): the pieces fxview's own target cannot build itself, the game's environment records
# (fxview_sea_env) and the tool pack of the viewer variants (fxview_sea_pack). sea_tab.cpp is compiled by fxview's target.
# Included by the root CMakeLists.txt inside the GE_TOOLS block, before tools_pc/fxview.

# The game's level environment records: cut out of src/game/bgfog.c at build time (sea_env.cmake) and compiled with the game's own
# headers and defines (the directory's, so no override of the include paths here).
set(_sea_env_inc "${CMAKE_CURRENT_BINARY_DIR}/sea_env_tables.inc")
add_custom_command(OUTPUT "${_sea_env_inc}"
  COMMAND "${CMAKE_COMMAND}" "-DSEA_SRC=${CMAKE_SOURCE_DIR}" "-DSEA_OUT=${_sea_env_inc}" -P "${CMAKE_CURRENT_SOURCE_DIR}/sea_env.cmake"
  DEPENDS "${CMAKE_SOURCE_DIR}/src/game/bgfog.c" "${CMAKE_CURRENT_SOURCE_DIR}/sea_env.cmake" COMMENT "sea tab: environment records from bgfog.c" VERBATIM)
add_library(fxview_sea_env STATIC sea_env.c "${_sea_env_inc}")
target_include_directories(fxview_sea_env PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src/game")
if(GE_SWITCH)
  set_property(TARGET fxview_sea_env PROPERTY POSITION_INDEPENDENT_CODE OFF)
  target_compile_options(fxview_sea_env PRIVATE -fPIE)
endif()

# The tool pack of the viewer variants (sea_pack.cmake): SPIR-V for the Vulkan backend, DKSH for deko3d. GL needs none.
set(_sea_kind "")
if(GE_SWITCH)
  find_program(SEA_UAM uam HINTS "${DEVKITPRO}/tools/bin" REQUIRED)
  set(_sea_kind dk)
elseif(GE_VULKAN)
  find_program(SEA_GLSLANG glslangValidator REQUIRED)
  set(_sea_kind vk)
endif()
if(_sea_kind)
  find_program(SEA_HOST_CXX NAMES c++ g++ HINTS /usr/bin REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  set(_sea_pack "${CMAKE_CURRENT_BINARY_DIR}/ge007_sea.bin")
  file(GLOB_RECURSE _sea_glsl CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/port/shaders/sky/*.glsl" "${CMAKE_SOURCE_DIR}/port/shaders/water/*.glsl")
  add_custom_command(OUTPUT "${_sea_pack}"
    COMMAND "${CMAKE_COMMAND}" -DSEA_KIND=${_sea_kind} "-DSEA_SRC=${CMAKE_SOURCE_DIR}" "-DSEA_OUT=${CMAKE_CURRENT_BINARY_DIR}/pack"
            "-DSEA_PACK=${_sea_pack}" "-DSEA_HOST_CXX=${SEA_HOST_CXX}" "-DSEA_GLSLANG=${SEA_GLSLANG}" "-DSEA_UAM=${SEA_UAM}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/sea_pack.cmake"
    DEPENDS ${_sea_glsl} "${CMAKE_CURRENT_SOURCE_DIR}/sea_pack.cmake" "${CMAKE_SOURCE_DIR}/tools_pc/dkshgen/dkshgen.cpp"
            "${CMAKE_SOURCE_DIR}/port/ge_render/src/gfx_cc.cpp" "${CMAKE_SOURCE_DIR}/port/ge_render/src/gfx_dk_layout.h"
            ${SEA_UAM} ${SEA_GLSLANG}   # a new compiler rebuilds the pack
    COMMENT "sea tab viewer pack (${_sea_kind})" VERBATIM)
  set(_sea_s "${CMAKE_CURRENT_SOURCE_DIR}/sea_pack.S")
  add_library(fxview_sea_pack STATIC "${_sea_s}")
  set_property(TARGET fxview_sea_pack PROPERTY INCLUDE_DIRECTORIES "")
  set_property(TARGET fxview_sea_pack PROPERTY COMPILE_OPTIONS "-Wa,-I${CMAKE_CURRENT_BINARY_DIR}")
  set_source_files_properties("${_sea_s}" PROPERTIES OBJECT_DEPENDS "${_sea_pack}")
  if(GE_SWITCH)
    set_property(TARGET fxview_sea_pack PROPERTY POSITION_INDEPENDENT_CODE OFF)
    target_compile_options(fxview_sea_pack PRIVATE -fPIE)
  endif()
endif()
