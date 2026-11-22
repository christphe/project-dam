# project-dam: cross-compilation to Linux arm64 for PortMaster handhelds,
# with Debian's aarch64-linux-gnu toolchain and arm64 multiarch libraries
# (Dockerfile.arm64).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_ASM_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH /opt/sdl2 /usr/aarch64-linux-gnu /usr/lib/aarch64-linux-gnu)
set(CMAKE_PREFIX_PATH /opt/sdl2)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
# Any ARMv8-A core; tuned for the Cortex-A53 most handhelds use.
set(CMAKE_C_FLAGS_INIT "-march=armv8-a -mtune=cortex-a53")
set(CMAKE_CXX_FLAGS_INIT "-march=armv8-a -mtune=cortex-a53")
# No build-tree rpath: the binary loads the firmware's libSDL2 by soname, never
# the import library in /opt/sdl2 (Dockerfile.arm64). The static C++ runtime is
# set in CMakeLists.txt (EXTRA_LIBRARIES, GE_ARM64).
set(CMAKE_SKIP_RPATH ON CACHE BOOL "no build-tree rpath (arm64 handhelds)" FORCE)
