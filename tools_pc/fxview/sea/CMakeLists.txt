# The sea and sky tab of fxview (issue #145): fxview_sea (the tab, a static library the fxview host links; its ImGui widgets need no
# backend) and seatab_host (a small ge_render host that runs the tab on its own, with a fly camera). Included by the root
# CMakeLists.txt inside the GE_TOOLS block.
set(GE_IMGUI_DIR "${CMAKE_SOURCE_DIR}/port/third_party/imgui")

# Compiled like ge_imgui: the repo's include/ holds N64 SDK headers named like system ones (D66), so the directory's include paths
# and the game's force-included header stay out; the library needs ImGui, ge_render's public headers and its own directory.
get_directory_property(_sea_opts COMPILE_OPTIONS)
list(FILTER _sea_opts EXCLUDE REGEX "^(-include|versioninfo[.]h)$")
set(MV_DIR "${CMAKE_SOURCE_DIR}/tools_pc/modelview/pc")   # the shared texture decoder (GE's image.c codecs)
add_library(fxview_sea STATIC sea_tab.cpp cloudtex.cpp "${MV_DIR}/mv_tex.cpp" "${MV_DIR}/mv_zlib.cpp" "${CMAKE_SOURCE_DIR}/port/src/skyplus_core.c")
set_property(TARGET fxview_sea PROPERTY INCLUDE_DIRECTORIES "${GE_IMGUI_DIR}" "${GE_RENDER_DIR}/include" "${CMAKE_SOURCE_DIR}/port/include" "${CMAKE_CURRENT_SOURCE_DIR}" "${MV_DIR}")
set_property(TARGET fxview_sea PROPERTY COMPILE_OPTIONS ${_sea_opts})
set_property(TARGET fxview_sea PROPERTY COMPILE_DEFINITIONS "")
if(GE_SWITCH)
  set_property(TARGET fxview_sea PROPERTY POSITION_INDEPENDENT_CODE OFF)
  target_compile_options(fxview_sea PRIVATE -fPIE)
endif()
target_include_directories(fxview_sea INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
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
target_link_libraries(fxview_sea PUBLIC ge_imgui fxview_sea_env)

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
  target_link_libraries(fxview_sea INTERFACE fxview_sea_pack)
endif()

add_executable(seatab_host seatab_host.cpp)
target_compile_definitions(seatab_host PRIVATE SDL_MAIN_HANDLED)
if(_sea_kind)
  target_compile_definitions(seatab_host PRIVATE SEA_HAVE_PACK=1)
endif()
target_link_libraries(seatab_host fxview_sea ge_render)
if(GE_SWITCH)
  nx_generate_nacp(seatab_host.nacp NAME "seatab_host" AUTHOR "Project Dam" VERSION "0")
  nx_create_nro(seatab_host NACP seatab_host.nacp ICON "${NX_DEFAULT_ICON}")
endif()
