# Custom co-op missions (issues #100-#104): the mission library.
# Owned by the missions-in-game track (mission lib, Luau runtime).
# Plain host C/C++: its own static target, without the decomp's shadow headers
# (port/shim, include/ have an N64 stdlib.h/math.h) or the forced
# versioninfo.h, like ge_volk. The game links it; so do the tests and the
# editor. Game-side glue that needs game headers goes in port/src/ instead.
add_library(ge_mission STATIC
  "${CMAKE_SOURCE_DIR}/port/mission/mission.c"
  "${CMAKE_SOURCE_DIR}/port/third_party/cjson/cJSON.c"
)
set_property(TARGET ge_mission PROPERTY INCLUDE_DIRECTORIES
  "${CMAKE_SOURCE_DIR}/port/mission"
  "${CMAKE_SOURCE_DIR}/port/levelrec"
  "${CMAKE_SOURCE_DIR}/port/third_party/cjson")
set_property(TARGET ge_mission PROPERTY COMPILE_OPTIONS "")
set(SRC_MISSION "")
