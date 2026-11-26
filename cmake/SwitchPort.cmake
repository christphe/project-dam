# D973: script mode. The deko3d shader pack is built by re-running this file
# with -P (see switch_deko3d_shaders below): host dkshgen, uam, pack.
if(DK_PACK_RUN)
  file(MAKE_DIRECTORY "${DK_OUT}/glsl")
  execute_process(COMMAND "${DK_HOST_CXX}" -std=c++20 -O2 -I "${DK_SRC}/port/fast3d"
      "${DK_SRC}/tools_pc/dkshgen/dkshgen.cpp" "${DK_SRC}/port/fast3d/gfx_cc.cpp" -o "${DK_OUT}/dkshgen"
    RESULT_VARIABLE _r)
  if(NOT _r EQUAL 0)
    message(FATAL_ERROR "dkshgen: host build failed (${DK_HOST_CXX})")
  endif()
  file(GLOB _old "${DK_OUT}/glsl/*")
  if(_old)
    file(REMOVE ${_old})
  endif()
  execute_process(COMMAND "${DK_OUT}/dkshgen" glsl "${DK_LIST}" "${DK_OUT}/glsl" RESULT_VARIABLE _r)
  if(NOT _r EQUAL 0)
    message(FATAL_ERROR "dkshgen glsl failed")
  endif()
  set(_pack "")
  macro(_dk_uam stage src out)
    execute_process(COMMAND "${DK_UAM}" -s ${stage} -o "${out}" "${src}" RESULT_VARIABLE _ur OUTPUT_VARIABLE _uo ERROR_VARIABLE _ue)
    if(NOT _ur EQUAL 0)
      message(FATAL_ERROR "uam failed on ${src}:
${_uo}${_ue}")
    endif()
  endmacro()
  _dk_uam(vert "${DK_SRC}/port/shaders/uber.vert.glsl" "${DK_OUT}/glsl/uber.vert.dksh")
  _dk_uam(frag "${DK_SRC}/port/shaders/uber.frag.glsl" "${DK_OUT}/glsl/uber.frag.dksh")
  _dk_uam(vert "${DK_SRC}/port/shaders/fullscreen.vert.glsl" "${DK_OUT}/glsl/fullscreen.vert.dksh")
  _dk_uam(frag "${DK_SRC}/port/shaders/fxaa.frag.glsl" "${DK_OUT}/glsl/fxaa.frag.dksh")
  # GE+ sky and water share skycommon.glsl, pasted at their `#pragma skycommon` line
  file(READ "${DK_SRC}/port/shaders/skycommon.glsl" _skycommon)
  foreach(_sc skyplus water skylut)
    file(READ "${DK_SRC}/port/shaders/${_sc}.frag.glsl" _scsrc)
    if(_sc STREQUAL "water")
      # the full (half-size pass) variant reads the far sea baked for the level's water surfaces (#79); Vulkan / GL never define it
      string(REPLACE "#version 460" "#version 460\n#define WATER_FAR" _scsrc "${_scsrc}")
    endif()
    string(REPLACE "#pragma skycommon" "${_skycommon}" _scsrc "${_scsrc}")
    file(WRITE "${DK_OUT}/glsl/${_sc}.frag.glsl" "${_scsrc}")
    _dk_uam(frag "${DK_OUT}/glsl/${_sc}.frag.glsl" "${DK_OUT}/glsl/${_sc}.frag.dksh")
  endforeach()
  # GE+ water at half resolution (#79): the cheap body variant of water.frag.glsl (#define WATER_BODY after its #version line,
  # the same file, no fork) and the composite that draws the half-size result over it
  file(READ "${DK_SRC}/port/shaders/water.frag.glsl" _wbsrc)
  string(REPLACE "#version 460" "#version 460\n#define WATER_BODY" _wbsrc "${_wbsrc}")
  string(REPLACE "#pragma skycommon" "${_skycommon}" _wbsrc "${_wbsrc}")
  file(WRITE "${DK_OUT}/glsl/waterbody.frag.glsl" "${_wbsrc}")
  _dk_uam(frag "${DK_OUT}/glsl/waterbody.frag.glsl" "${DK_OUT}/glsl/waterbody.frag.dksh")
  _dk_uam(frag "${DK_SRC}/port/shaders/watercomp.frag.glsl" "${DK_OUT}/glsl/watercomp.frag.dksh")
  string(APPEND _pack "0 1 ${DK_OUT}/glsl/uber.vert.dksh ${DK_OUT}/glsl/uber.frag.dksh -
")
  string(APPEND _pack "0 2 ${DK_OUT}/glsl/fullscreen.vert.dksh - -
")
  string(APPEND _pack "0 3 - ${DK_OUT}/glsl/fxaa.frag.dksh -
")
  string(APPEND _pack "0 4 - ${DK_OUT}/glsl/skyplus.frag.dksh -
")
  string(APPEND _pack "0 5 - ${DK_OUT}/glsl/water.frag.dksh -
")
  string(APPEND _pack "0 6 - ${DK_OUT}/glsl/skylut.frag.dksh -
")
  string(APPEND _pack "0 7 - ${DK_OUT}/glsl/waterbody.frag.dksh -
")
  string(APPEND _pack "0 8 - ${DK_OUT}/glsl/watercomp.frag.dksh -
")
  file(STRINGS "${DK_OUT}/glsl/list.txt" _lines)
  set(_n 0)
  foreach(_l IN LISTS _lines)
    string(REPLACE " " ";" _f "${_l}")
    list(GET _f 0 _id0)
    list(GET _f 1 _id1)
    list(GET _f 2 _base)
    _dk_uam(vert "${DK_OUT}/glsl/${_base}.vert.glsl" "${DK_OUT}/glsl/${_base}.vert.dksh")
    _dk_uam(vert "${DK_OUT}/glsl/${_base}.mesh.vert.glsl" "${DK_OUT}/glsl/${_base}.mesh.vert.dksh")
    _dk_uam(frag "${DK_OUT}/glsl/${_base}.frag.glsl" "${DK_OUT}/glsl/${_base}.frag.dksh")
    string(APPEND _pack "${_id0} ${_id1} ${DK_OUT}/glsl/${_base}.vert.dksh ${DK_OUT}/glsl/${_base}.frag.dksh ${DK_OUT}/glsl/${_base}.mesh.vert.dksh
")
    math(EXPR _n "${_n} + 1")
  endforeach()
  file(WRITE "${DK_OUT}/pack.txt" "${_pack}")
  execute_process(COMMAND "${DK_OUT}/dkshgen" pack "${DK_PACK}" "${DK_OUT}/pack.txt" RESULT_VARIABLE _r)
  if(NOT _r EQUAL 0)
    message(FATAL_ERROR "dkshgen pack failed")
  endif()
  message(STATUS "deko3d shader pack: ${_n} combiners compiled by uam")
  return()
endif()

# project-dam: the Nintendo Switch homebrew build (devkitA64 + libnx), kept
# out of CMakeLists.txt as much as possible. CMakeLists.txt includes this file
# right after project() and calls the macros below at marked hooks
# ("project-dam hook"). devkitPro's toolchain file
# (/opt/devkitpro/cmake/Switch.cmake) sets CMAKE_SYSTEM_NAME=NintendoSwitch.
#
# Address model (docs/switch/address-model.md): the N64 window base is chosen at run
# time (g_portAddrBase, a free 4 GiB-aligned slot; nxChooseAddrBase), because the
# main thread's stack can land on the fixed 0x10000000 / 0x70000000 windows.
# port/src/switch/ places the cart, DRAM and stack blocks at base + their N64
# addresses. The ROM labels (romassets_*.s) stay link-time absolutes at their
# N64 values (they are only ever used as N64 cart addresses); the three DRAM
# symbols became macros (portaddr.h PORT_DRAM_SYM).

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
  set(GE_SWITCH ON)
  message(STATUS "project-dam: Nintendo Switch build (devkitA64 ${CMAKE_C_COMPILER_VERSION})")
else()
  set(GE_SWITCH OFF)
endif()

# newlib's headers live in the toolchain's target include dir, not at
# bin/../include like MinGW / a native Linux GCC (hoststring.h & co.).
macro(switch_host_headers)
  if(GE_SWITCH)
    set(_NX_SYSINC "${DEVKITPRO}/devkitA64/aarch64-none-elf/include")
    set(PORT_HOST_STRING_H "${_NX_SYSINC}/string.h")
    set(PORT_HOST_SCHED_H  "${_NX_SYSINC}/sched.h")
    set(PORT_HOST_STDLIB_H "${_NX_SYSINC}/stdlib.h")
  endif()
endmacro()

# Dependencies: SDL2 (static, pulls EGL/glapi/drm_nouveau/libnx), zlib. OpenGL
# entry points are loaded at run time through SDL_GL_GetProcAddress (glad), so
# no GL library is linked directly.
macro(switch_dependencies)
  find_package(SDL2 CONFIG REQUIRED)
  find_package(ZLIB REQUIRED)
  include_directories(SYSTEM "${ZLIB_INCLUDE_DIRS}")
  set(ZLIB_INCLUDE_DIR "${ZLIB_INCLUDE_DIRS}")
  # D973: deko3d (libnx's lib dir), for the deko3d backend (the Switch default).
  list(APPEND LIBS deko3d SDL2::SDL2 ZLIB::ZLIB m)
  # In-game update (port/src/switch/nxupdate.c): libcurl on mbedtls, and the
  # Mozilla CA bundle of the build image linked into the NRO (mbedtls has no
  # system certificate store; libcurl 7.69 cannot take it from memory).
  list(APPEND LIBS curl mbedtls mbedx509 mbedcrypto z)
  set(_ca_bundle "/etc/ssl/certs/ca-certificates.crt")
  if(NOT EXISTS "${_ca_bundle}")
    message(FATAL_ERROR "Switch build: ${_ca_bundle} missing (install ca-certificates in the build image)")
  endif()
  # .tmp + configure_file COPYONLY: file(WRITE) touches the file on every configure, which relinked
  # the NRO on every build.
  file(WRITE "${CMAKE_BINARY_DIR}/nxcacert.S.tmp"
    ".section .rodata
.balign 8
.global nx_cacert_start
.global nx_cacert_end
"
    "nx_cacert_start:
.incbin \"${_ca_bundle}\"
.byte 0
nx_cacert_end:
")
  configure_file("${CMAKE_BINARY_DIR}/nxcacert.S.tmp" "${CMAKE_BINARY_DIR}/nxcacert.S" COPYONLY)
  set(SWITCH_CACERT_SRC "${CMAKE_BINARY_DIR}/nxcacert.S")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_ca_bundle}")
endmacro()

# D973: the deko3d shader pack ("shipped shader cache"): every combiner listed in
# port/shaders/combiners.txt (the game's) and fx/combiners.txt (the port's effects,
# with their code from port/shaders/fx/) plus the uber/FXAA/sky shaders, generated by
# the host tool tools_pc/dkshgen, compiled to DKSH by devkitPro's host uam and
# embedded in the NRO through port/fast3d/gfx_dk_pack.S (.incbin). One fixed GPU,
# so DKSH built here is valid on every console; no shader compiler at run time.
macro(switch_deko3d_shaders target)
  if(GE_SWITCH)
    find_program(DK_UAM uam HINTS "${DEVKITPRO}/tools/bin" REQUIRED)
    find_program(DK_HOST_CXX NAMES c++ g++ HINTS /usr/bin REQUIRED NO_CMAKE_FIND_ROOT_PATH)
    set(_dk_pack "${CMAKE_BINARY_DIR}/ge007_dksh.bin")
    file(GLOB _dk_glsl CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/port/shaders/*.glsl")
    file(GLOB _fx_glsl CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/port/shaders/fx/*.glsl")
    add_custom_command(OUTPUT "${_dk_pack}"
      COMMAND "${CMAKE_COMMAND}" -DDK_PACK_RUN=ON
              "-DDK_SRC=${CMAKE_SOURCE_DIR}" "-DDK_OUT=${CMAKE_BINARY_DIR}/dksh"
              "-DDK_LIST=${CMAKE_SOURCE_DIR}/port/shaders/combiners.txt"
              "-DDK_PACK=${_dk_pack}" "-DDK_UAM=${DK_UAM}" "-DDK_HOST_CXX=${DK_HOST_CXX}"
              -P "${CMAKE_SOURCE_DIR}/cmake/SwitchPort.cmake"
      DEPENDS "${CMAKE_SOURCE_DIR}/port/shaders/combiners.txt" "${CMAKE_SOURCE_DIR}/port/shaders/fx/combiners.txt"
            ${_fx_glsl} ${_dk_glsl}
              "${CMAKE_SOURCE_DIR}/tools_pc/dkshgen/dkshgen.cpp"
              "${CMAKE_SOURCE_DIR}/port/fast3d/gfx_cc.cpp"
              "${CMAKE_SOURCE_DIR}/port/fast3d/gfx_cc.h"
              "${CMAKE_SOURCE_DIR}/port/fast3d/gfx_dk_layout.h"
              "${CMAKE_SOURCE_DIR}/cmake/SwitchPort.cmake"
      COMMENT "deko3d shader pack (dkshgen + uam)"
      VERBATIM)
    set(_dk_s "${CMAKE_SOURCE_DIR}/port/fast3d/gfx_dk_pack.S")
    set_source_files_properties("${_dk_s}" PROPERTIES
      OBJECT_DEPENDS "${_dk_pack}"
      COMPILE_OPTIONS "-Wa,-I${CMAKE_BINARY_DIR}")
    target_sources(${target} PRIVATE "${_dk_s}")
  endif()
endmacro()

# Project Dam's own version (its releases are tagged project-dam-vX.Y.Z); the
# release workflow passes it, local builds say dev-<commit>.
set(PROJECT_DAM_VERSION "" CACHE STRING "Project Dam version shown in the homebrew menu")

# NRO packaging: icon + NACP (title shown in the homebrew menu).
macro(switch_package target)
  if(GE_SWITCH)
    # newlib is linked statically: the game's own strtol (src/str.c, 32-bit
    # libultra flavour, only used for command-line tokens) would collide with
    # libc's. Rename it there; callers get newlib's standard strtol.
    set_source_files_properties("${CMAKE_SOURCE_DIR}/src/str.c"
      PROPERTIES COMPILE_DEFINITIONS "strtol=n64_strtol")
    # Shown in the homebrew menu. Project Dam is a Switch port of the
    # GoldenEye 007 PC port (https://github.com/jkdansereau/goldeneye-pc-port).
    if(NOT PROJECT_DAM_VERSION)
      set(PROJECT_DAM_VERSION "dev-${VERSION_HASH}")
    endif()
    nx_generate_nacp(ge007.nacp
      NAME    "Project Dam"
      AUTHOR  "Switch port of jkdansereau/goldeneye-pc-port"
      VERSION "${PROJECT_DAM_VERSION}")
    set(_ge_nx_icon "${CMAKE_SOURCE_DIR}/port/switch/icon.jpg")
    if(NOT EXISTS "${_ge_nx_icon}")
      set(_ge_nx_icon "${NX_DEFAULT_ICON}")
    endif()
    nx_create_nro(${target}
      NACP ge007.nacp
      ICON "${_ge_nx_icon}")
  endif()
endmacro()
