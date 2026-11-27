# ge_assets (issues #171, #172): what the game and its tools read from the export, in one static library. Pure C, no GL, no
# game headers: the package reader (.pmod / .ptexpack tars and folders), the one PNG reader and writer, the texture-pack format
# (textures.json), the texture resolution (mod over pack over ge007.ptexpack) and the asset-pack stack (.passets: the mod over
# the player's ordered packs, ge_assetstack.c). Plain host C, its own target without the
# decomp's shadow headers (port/shim, include/ have an N64 stdlib.h/math.h) or the forced versioninfo.h, like ge_mission, which
# links it. Builds for the Switch, Windows and Linux. cJSON (vendored, MIT) lives here: textures.json is parsed with it.
add_library(ge_assets STATIC
  "${CMAKE_SOURCE_DIR}/port/assets/ge_pkg.c"
  "${CMAKE_SOURCE_DIR}/port/assets/ge_png.c"
  "${CMAKE_SOURCE_DIR}/port/assets/ge_texpacks.c"
  "${CMAKE_SOURCE_DIR}/port/assets/ge_tex.c"
  "${CMAKE_SOURCE_DIR}/port/assets/ge_assetstack.c"
  "${CMAKE_SOURCE_DIR}/port/third_party/cjson/cJSON.c"
)
set_property(TARGET ge_assets PROPERTY INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/port/third_party/cjson")
set_property(TARGET ge_assets PROPERTY COMPILE_OPTIONS "")
target_include_directories(ge_assets PUBLIC "${CMAKE_SOURCE_DIR}/port/assets")
if(ZLIB_INCLUDE_DIR)
  target_include_directories(ge_assets SYSTEM PRIVATE "${ZLIB_INCLUDE_DIR}")
endif()
target_link_libraries(ge_assets PUBLIC ${ZLIB_LIBRARY})
