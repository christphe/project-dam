# ge-editor: the mission editor (issues #100-#102). Owned by the editor track.
# Not built for the Switch (the root CMakeLists adds this directory only off the Switch).
#
# Three parts:
#  - ge_editor_f3d: fast3d's OpenGL backend (gfx_opengl.cpp + gfx_cc.cpp + glad), compiled
#    exactly as the game compiles it (it includes <PR/gbi.h> and the port headers), plus the
#    symbols it expects from the rest of the game (fast3d_stubs.cpp). Untouched port/fast3d/.
#  - ge-editor: the editor itself, plain C++17 with clean include dirs (like ge_volk and
#    ge_mission: no port/shim, include/ or src/ shadow headers, no forced versioninfo.h).
#  - editor/tools/make_test_rec: writes a synthetic level export (no ROM data) for development.
set(ED_DIR "${CMAKE_SOURCE_DIR}/editor")
set(F3D_DIR "${CMAKE_SOURCE_DIR}/port/fast3d")
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/port/third_party/imgui")

# ---- fast3d GL backend ---------------------------------------------------------------------
add_library(ge_editor_f3d OBJECT
  "${F3D_DIR}/gfx_opengl.cpp"
  "${F3D_DIR}/gfx_cc.cpp"
  "${F3D_DIR}/glad/glad.c"
  "${ED_DIR}/fast3d_stubs.cpp"
)
target_link_libraries(ge_editor_f3d PRIVATE ${LIBS})   # SDL2 / GL usage requirements only (an object library is not linked)

# ---- the editor ----------------------------------------------------------------------------
file(GLOB ED_SRC CONFIGURE_DEPENDS "${ED_DIR}/*.cpp")
list(FILTER ED_SRC EXCLUDE REGEX "/fast3d_stubs[.]cpp$")
file(GLOB ED_IMGUI CONFIGURE_DEPENDS "${IMGUI_DIR}/*.cpp")
set(ED_BACKENDS
  "${ED_DIR}/third_party/imgui_backends/imgui_impl_sdl2.cpp"
  "${ED_DIR}/third_party/imgui_backends/imgui_impl_opengl3.cpp")
# The decomp's include/math.h is not on this target's include path, but keep the game's
# trick for the vendored core anyway: harmless with the system headers.
set_source_files_properties(${ED_IMGUI} PROPERTIES COMPILE_OPTIONS "-include;cmath")

add_executable(ge-editor ${ED_SRC} ${ED_IMGUI} ${ED_BACKENDS} $<TARGET_OBJECTS:ge_editor_f3d>)
set_property(TARGET ge-editor PROPERTY CXX_STANDARD 17)
# Clean include dirs: SDL2 only comes from the root's SYSTEM include (re-added below).
set_property(TARGET ge-editor PROPERTY INCLUDE_DIRECTORIES
  "${ED_DIR}"
  "${ED_DIR}/third_party"
  "${ED_DIR}/third_party/imgui_backends"
  "${IMGUI_DIR}"
  "${F3D_DIR}"
  "${CMAKE_SOURCE_DIR}/port/mission"
  "${CMAKE_SOURCE_DIR}/port/levelrec"
  "${CMAKE_SOURCE_DIR}/port/third_party/cjson")
if(SDL2_INCLUDE_DIR)
  target_include_directories(ge-editor SYSTEM PRIVATE "${SDL2_INCLUDE_DIR}")
endif()
set_property(TARGET ge-editor PROPERTY COMPILE_OPTIONS "")
set_property(TARGET ge-editor PROPERTY COMPILE_DEFINITIONS "")
if(WIN32)
  target_link_libraries(ge-editor PRIVATE ws2_32)
endif()
target_link_libraries(ge-editor PRIVATE ge_mission ${LIBS})
# Next to the other binaries; the Windows bundler leaves SDL2.dll in the same directory.
set_target_properties(ge-editor PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set_target_properties(ge-editor PROPERTIES OUTPUT_NAME "ge-editor")

# ---- synthetic level export ----------------------------------------------------------------
add_executable(make_test_rec "${ED_DIR}/tools/make_test_rec.cpp")
set_property(TARGET make_test_rec PROPERTY CXX_STANDARD 17)
set_property(TARGET make_test_rec PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set_property(TARGET make_test_rec PROPERTY INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/port/levelrec")
set_property(TARGET make_test_rec PROPERTY COMPILE_OPTIONS "")
set_property(TARGET make_test_rec PROPERTY COMPILE_DEFINITIONS "")
if(NOT WIN32)
  target_link_libraries(make_test_rec PRIVATE m)
endif()
