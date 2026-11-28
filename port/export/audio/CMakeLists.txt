# ge007-audio-export: ROM -> Standard MIDI Files + SoundFont 2 + WAV (clean-room, C99, libc + zlib).
# Stand-alone: cmake -S port/export/audio -B build-audio && cmake --build build-audio
cmake_minimum_required(VERSION 3.16)
project(ge007_audio_export C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(ZLIB REQUIRED)

add_library(ge_audio_export STATIC
  ge_audio_common.c
  ge_audio_seq.c
  ge_audio_bank.c
  ge_audio_sf2.c
  ge_audio_sfx.c
  ge_audio_dirs.c)
target_include_directories(ge_audio_export PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ge_audio_export PUBLIC ZLIB::ZLIB)
if(NOT WIN32)
  target_link_libraries(ge_audio_export PUBLIC m)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ge_audio_export PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_executable(ge007-audio-export ge007-audio-export.c)
target_link_libraries(ge007-audio-export PRIVATE ge_audio_export)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ge007-audio-export PRIVATE -Wall -Wextra)
endif()
