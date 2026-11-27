# Static level export (issue #150): ROM -> level decor mesh + textures -> glTF.
# Library linked into the game (`ge007 --export-levels DIR`) + the standalone
# `ge007-export` CLI (off Switch).
#
# CLEAN ROOM: these sources are written from docs/specs/ only. Like
# port/convert/ this directory scope is freestanding (libc + zlib + libm and
# the converter library), so none of the game's directory-wide settings leak
# in: the decomp include path, the forced `-include versioninfo.h`, the region
# -D macros and the game-specific linker flags.
set_directory_properties(PROPERTIES
  COMPILE_OPTIONS "" COMPILE_DEFINITIONS "" INCLUDE_DIRECTORIES "")
set(CMAKE_C_FLAGS "")
set(CMAKE_EXE_LINKER_FLAGS "")

find_package(ZLIB REQUIRED)

file(GLOB EXPORT_SRC "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
list(FILTER EXPORT_SRC EXCLUDE REGEX "ge007_export_cli[.]c$")
list(FILTER EXPORT_SRC EXCLUDE REGEX "zz_[^/]*[.]c$")
add_library(ge007_export STATIC ${EXPORT_SRC})
target_include_directories(ge007_export PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(ge007_export PRIVATE "${ZLIB_INCLUDE_DIR}")
target_compile_options(ge007_export PRIVATE -O2 -Wall -Wextra)
target_link_libraries(ge007_export PUBLIC ge007_convert m)

if(NOT GE_SWITCH)
  add_executable(ge007-export ge007_export_cli.c)
  target_compile_options(ge007-export PRIVATE -O2 -Wall -Wextra)
  target_link_libraries(ge007-export ge007_export ge007_convert ${ZLIB_LIBRARIES} m)

  # The texture/PNG test is built here, in this freestanding scope (the tests/ scope has the
  # decomp include path, whose zlib.h shadows the real one); tests/CMakeLists.txt registers it.
  # Always built off Switch: this directory is configured before the GE007_BUILD_TESTS option exists.
  add_executable(test_export_tex "${CMAKE_SOURCE_DIR}/tests/test_export_tex.c")
  target_compile_options(test_export_tex PRIVATE -O2 -Wall -Wextra)
  target_link_libraries(test_export_tex ge007_export ge007_convert ${ZLIB_LIBRARIES} m)
  set_target_properties(test_export_tex PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
endif()
