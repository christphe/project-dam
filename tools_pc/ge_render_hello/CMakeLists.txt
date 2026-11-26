# ge_render_hello: the minimal host of the renderer library (issue #143). Off by default: -DGE_TOOLS=ON.
# PC: build-*/ge_render_hello[.exe] (the Windows build's directory has the DLLs next to it). Switch: ge_render_hello.nro.
add_executable(ge_render_hello main.cpp)
target_compile_definitions(ge_render_hello PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(ge_render_hello ge_render)
if(GE_SWITCH)
  nx_generate_nacp(ge_render_hello.nacp NAME "ge_render_hello" AUTHOR "Project Dam" VERSION "0")
  nx_create_nro(ge_render_hello NACP ge_render_hello.nacp ICON "${NX_DEFAULT_ICON}")
endif()
