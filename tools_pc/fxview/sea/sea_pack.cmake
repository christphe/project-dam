# The tool pack of the sea tab (issue #145): the viewer variants of the GE+ sky and water shaders (`#ifdef VIEWER` in
# port/shaders/sky and water), compiled into a pack of the game's own format (port/ge_render/src/gfx_dk_layout.h) that holds only
# DK_PACK_SKYPLUS, SKYLUT, WATER (and, for deko3d, WATERBODY). It is built next to the tool and never reaches the game's packs
# (cmake/VulkanShaders.cmake, cmake/SwitchPort.cmake are untouched); the GL backend compiles the variants itself at run time.
#
# Script mode (-P): SEA_KIND vk | dk, SEA_SRC (repo), SEA_OUT (work dir), SEA_PACK (output), SEA_HOST_CXX, and
# SEA_GLSLANG (vk) or SEA_UAM (dk).
file(MAKE_DIRECTORY "${SEA_OUT}")
execute_process(COMMAND "${SEA_HOST_CXX}" -std=c++20 -O2 -I "${SEA_SRC}/port/ge_render/include" -I "${SEA_SRC}/port/ge_render/src"
    "${SEA_SRC}/tools_pc/dkshgen/dkshgen.cpp" "${SEA_SRC}/port/ge_render/src/gfx_cc.cpp" -o "${SEA_OUT}/dkshgen" RESULT_VARIABLE _r)
if(NOT _r EQUAL 0)
  message(FATAL_ERROR "dkshgen: host build failed (${SEA_HOST_CXX})")
endif()

# the Viewer block sits at binding 8 in every variant (the backends bind it there: Vulkan 13 / 12 after the UBO shift, GL and deko3d 8)
set(_hdr "#define VIEWER\nlayout(std140, binding = 8) uniform Viewer { ivec4 uVarv; vec4 uPrv[6]; };\n#define uSeaVar uVarv.x\n#define uSkyVar uVarv.y\n")
file(READ "${SEA_SRC}/port/shaders/sky/common.glsl" _common)

# one variant: the shader file with the defines and the viewer block after its #version line and sky/common.glsl pasted in
function(sea_variant out src defines)
  file(READ "${SEA_SRC}/port/shaders/${src}" _t)
  string(REPLACE "#version 460" "#version 460\n${defines}${_hdr}" _t "${_t}")
  string(REPLACE "#pragma skycommon" "${_common}" _t "${_t}")
  if(SEA_KIND STREQUAL "vk" AND src STREQUAL "water/water.frag.glsl")   # as cmake/VulkanShaders.cmake does for the game's water
    string(REPLACE "binding = 4) uniform sampler2D uWMap" "binding = 9) uniform sampler2D uWMap" _t "${_t}")
    string(REPLACE "binding = 2) uniform Sky" "binding = 4) uniform Sky" _t "${_t}")
  endif()
  file(WRITE "${out}" "${_t}")
endfunction()

set(_pack "")
if(SEA_KIND STREQUAL "vk")
  macro(_sea_compile sub src out)
    execute_process(COMMAND "${SEA_GLSLANG}" -V --target-env vulkan1.3 --sub ${sub} -S frag -o "${out}" "${src}"
      RESULT_VARIABLE _ur OUTPUT_VARIABLE _uo ERROR_VARIABLE _ue)
    if(NOT _ur EQUAL 0)
      message(FATAL_ERROR "glslangValidator failed on ${src}:\n${_uo}${_ue}")
    endif()
  endmacro()
  sea_variant("${SEA_OUT}/skyplus.frag.glsl" "sky/sky.frag.glsl" "")
  sea_variant("${SEA_OUT}/skylut.frag.glsl" "sky/lut.frag.glsl" "")
  sea_variant("${SEA_OUT}/water.frag.glsl" "water/water.frag.glsl" "")
  _sea_compile(5 "${SEA_OUT}/skyplus.frag.glsl" "${SEA_OUT}/skyplus.frag.spv")
  _sea_compile(5 "${SEA_OUT}/skylut.frag.glsl" "${SEA_OUT}/skylut.frag.spv")
  _sea_compile(4 "${SEA_OUT}/water.frag.glsl" "${SEA_OUT}/water.frag.spv")
  set(_ext spv)
else()
  macro(_sea_compile sub src out)   # deko3d: uam, no binding shift
    execute_process(COMMAND "${SEA_UAM}" -s frag -o "${out}" "${src}" RESULT_VARIABLE _ur OUTPUT_VARIABLE _uo ERROR_VARIABLE _ue)
    if(NOT _ur EQUAL 0)
      message(FATAL_ERROR "uam failed on ${src}:\n${_uo}${_ue}")
    endif()
  endmacro()
  sea_variant("${SEA_OUT}/skyplus.frag.glsl" "sky/sky.frag.glsl" "")
  sea_variant("${SEA_OUT}/skylut.frag.glsl" "sky/lut.frag.glsl" "")
  sea_variant("${SEA_OUT}/water.frag.glsl" "water/water.frag.glsl" "#define WATER_FAR\n")   # as cmake/SwitchPort.cmake builds the game's
  sea_variant("${SEA_OUT}/waterbody.frag.glsl" "water/water.frag.glsl" "#define WATER_BODY\n")
  _sea_compile(0 "${SEA_OUT}/skyplus.frag.glsl" "${SEA_OUT}/skyplus.frag.dksh")
  _sea_compile(0 "${SEA_OUT}/skylut.frag.glsl" "${SEA_OUT}/skylut.frag.dksh")
  _sea_compile(0 "${SEA_OUT}/water.frag.glsl" "${SEA_OUT}/water.frag.dksh")
  _sea_compile(0 "${SEA_OUT}/waterbody.frag.glsl" "${SEA_OUT}/waterbody.frag.dksh")
  set(_ext dksh)
endif()
set(_pack "0 4 - ${SEA_OUT}/skyplus.frag.${_ext} -\n0 5 - ${SEA_OUT}/water.frag.${_ext} -\n0 6 - ${SEA_OUT}/skylut.frag.${_ext} -\n")
if(SEA_KIND STREQUAL "dk")
  string(APPEND _pack "0 7 - ${SEA_OUT}/waterbody.frag.${_ext} -\n")
endif()
file(WRITE "${SEA_OUT}/pack.txt" "${_pack}")
execute_process(COMMAND "${SEA_OUT}/dkshgen" pack "${SEA_PACK}" "${SEA_OUT}/pack.txt" RESULT_VARIABLE _r)
if(NOT _r EQUAL 0)
  message(FATAL_ERROR "dkshgen pack failed")
endif()
message(STATUS "sea tab viewer pack (${SEA_KIND}): ${SEA_PACK}")
