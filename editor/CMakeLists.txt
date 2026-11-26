# project-dam-editor: the mission editor (issues #100-#102). Owned by the editor track.
# Not built for the Switch (the root CMakeLists adds this directory only off the Switch).
#
# Three parts:
#  - ge_editor_f3d: fast3d's OpenGL backend (gfx_opengl.cpp + gfx_cc.cpp + glad), compiled
#    exactly as the game compiles it (it includes <PR/gbi.h> and the port headers), plus the
#    symbols it expects from the rest of the game (fast3d_stubs.cpp). Untouched port/fast3d/.
#  - project-dam-editor: the editor itself, plain C++17 with clean include dirs (like ge_volk and
#    ge_mission: no port/shim, include/ or src/ shadow headers, no forced versioninfo.h).
#  - editor/tools/make_test_rec: writes a synthetic level export (no ROM data) for development.
set(ED_DIR "${CMAKE_SOURCE_DIR}/editor")
set(F3D_DIR "${CMAKE_SOURCE_DIR}/port/ge_render")   # the renderer library (issue #142): gfx_opengl.cpp, gfx_cc.cpp, glad and the core
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/port/third_party/imgui")

# ---- fast3d GL backend ---------------------------------------------------------------------
add_library(ge_editor_f3d OBJECT
  "${F3D_DIR}/src/gfx_opengl.cpp"
  "${F3D_DIR}/src/gfx_cc.cpp"
  "${F3D_DIR}/src/ge_core.cpp"   # the renderer's config, callbacks and their defaults (no backend, no window)
  "${F3D_DIR}/include/glad/glad.c"
  "${ED_DIR}/fast3d_stubs.cpp"
  "${CMAKE_SOURCE_DIR}/port/src/gpuprobe.c"   # the first-launch GPU benchmark's pure decision, called by gfx_opengl.cpp
)
target_include_directories(ge_editor_f3d PRIVATE "${F3D_DIR}/include" "${F3D_DIR}/src")
target_link_libraries(ge_editor_f3d PRIVATE ${LIBS})   # SDL2 / GL usage requirements only (an object library is not linked)

# ---- Luau Analysis (the type checker), editor only ------------------------------------------
# port/third_party/luau/Analysis + Config at the same Luau version as the VM / Compiler / Ast that ge_mission builds.
# Its own static library with a clean include path (no include/ or src/ shadow headers), and never linked into the game.
set(ED_LUAU "${CMAKE_SOURCE_DIR}/port/third_party/luau")
file(GLOB ED_LUAU_ANALYSIS_SRC CONFIGURE_DEPENDS "${ED_LUAU}/Analysis/src/*.cpp" "${ED_LUAU}/Config/src/*.cpp")
add_library(ge_luau_analysis STATIC ${ED_LUAU_ANALYSIS_SRC})
set_property(TARGET ge_luau_analysis PROPERTY CXX_STANDARD 17)
set_property(TARGET ge_luau_analysis PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET ge_luau_analysis PROPERTY INCLUDE_DIRECTORIES
  "${ED_LUAU}/Analysis/include" "${ED_LUAU}/Config/include" "${ED_LUAU}/Ast/include" "${ED_LUAU}/Common/include"
  "${ED_LUAU}/VM/include" "${ED_LUAU}/Compiler/include" "${ED_LUAU}/Bytecode/include")
set_property(TARGET ge_luau_analysis PROPERTY COMPILE_OPTIONS "")
set_property(TARGET ge_luau_analysis PROPERTY COMPILE_DEFINITIONS "")
if(WIN32)
  # NativeStackGuard.cpp includes <Windows.h> (capital W): a case-sensitive MinGW cross toolchain only has <windows.h>.
  # A forwarding header keeps the vendored tree unmodified.
  target_include_directories(ge_luau_analysis PRIVATE "${ED_DIR}/third_party/luau_shim")
endif()
target_compile_options(ge_luau_analysis PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include climits>")
target_link_libraries(ge_luau_analysis PUBLIC ge_mission)   # Ast / Common / VM / Compiler objects live there

# ---- the editor ----------------------------------------------------------------------------
file(GLOB ED_SRC CONFIGURE_DEPENDS "${ED_DIR}/*.cpp")
list(FILTER ED_SRC EXCLUDE REGEX "/fast3d_stubs[.]cpp$")
file(GLOB ED_IMGUI CONFIGURE_DEPENDS "${IMGUI_DIR}/*.cpp")
set(ED_TEXTEDIT "${ED_DIR}/third_party/ImGuiColorTextEdit/TextEditor.cpp")   # ImGuiColorTextEdit (the Luau code editor widget)
set(ED_BACKENDS
  "${ED_DIR}/third_party/imgui_backends/imgui_impl_sdl2.cpp"
  "${ED_DIR}/third_party/imgui_backends/imgui_impl_opengl3.cpp")
# The decomp's include/math.h is not on this target's include path, but keep the game's
# trick for the vendored core anyway: harmless with the system headers.
set_source_files_properties(${ED_IMGUI} PROPERTIES COMPILE_OPTIONS "-include;cmath")

add_executable(project-dam-editor ${ED_SRC} ${ED_IMGUI} ${ED_BACKENDS} ${ED_TEXTEDIT} $<TARGET_OBJECTS:ge_editor_f3d>)
set_property(TARGET project-dam-editor PROPERTY CXX_STANDARD 17)
# Clean include dirs: SDL2 only comes from the root's SYSTEM include (re-added below).
set_property(TARGET project-dam-editor PROPERTY INCLUDE_DIRECTORIES
  "${ED_DIR}"
  "${ED_DIR}/third_party"
  "${ED_DIR}/third_party/imgui_backends"
  "${ED_DIR}/third_party/ImGuiColorTextEdit"
  "${ED_LUAU}/Analysis/include" "${ED_LUAU}/Config/include" "${ED_LUAU}/Ast/include" "${ED_LUAU}/Common/include"
  "${IMGUI_DIR}"
  "${F3D_DIR}/include"
  "${CMAKE_SOURCE_DIR}/port/mission"
  "${CMAKE_SOURCE_DIR}/port/levelrec"
  "${CMAKE_SOURCE_DIR}/port/third_party/cjson")
if(SDL2_INCLUDE_DIR)
  target_include_directories(project-dam-editor SYSTEM PRIVATE "${SDL2_INCLUDE_DIR}")
endif()
set_property(TARGET project-dam-editor PROPERTY COMPILE_OPTIONS "")
set_property(TARGET project-dam-editor PROPERTY COMPILE_DEFINITIONS "")
if(WIN32)
  target_link_libraries(project-dam-editor PRIVATE ws2_32)
endif()
target_link_libraries(project-dam-editor PRIVATE ge_luau_analysis ge_mission ${LIBS})
if(WIN32)
  target_link_libraries(project-dam-editor PRIVATE comdlg32)   # the Open / Save file dialogs (filedialog.cpp)
  # Explorer/taskbar icon (tools_pc/make_icon.py); the window icon is editor_icon.h. Stays a console program (--help,
  # --mcp-stdio), so no WIN32_EXECUTABLE.
  target_sources(project-dam-editor PRIVATE "${ED_DIR}/icon/project-dam-editor.rc")
endif()
# Next to the other binaries; the Windows bundler leaves SDL2.dll in the same directory.
set_target_properties(project-dam-editor PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set_target_properties(project-dam-editor PROPERTIES OUTPUT_NAME "project-dam-editor")

# ---- synthetic level export ----------------------------------------------------------------
add_executable(make_test_rec "${ED_DIR}/tools/make_test_rec.cpp")
set_property(TARGET make_test_rec PROPERTY CXX_STANDARD 17)
set_property(TARGET make_test_rec PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set_property(TARGET make_test_rec PROPERTY INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/port/levelrec")
set_property(TARGET make_test_rec PROPERTY COMPILE_OPTIONS "")
set_property(TARGET make_test_rec PROPERTY COMPILE_DEFINITIONS "")
if(NOT WIN32)
  target_link_libraries(make_test_rec PRIVATE m)
elseif(MINGW)
  # The Windows bundle ships no libstdc++-6.dll: a PATH one from another toolchain fails to load (0xC0000139).
  target_link_options(make_test_rec PRIVATE -static-libstdc++ -static-libgcc)
endif()
