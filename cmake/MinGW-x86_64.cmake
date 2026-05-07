# Cross-compile toolchain: x86_64 MinGW from a Linux host.
#
# Usage:
#   cmake -S . -B build-mingw \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/MinGW-x86_64.cmake \
#       -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-mingw -j
#
# Produces .exe binaries that run under wine on Linux/macOS.

set(CMAKE_SYSTEM_NAME      Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Statically link the C++ runtime + libgcc so the resulting .exe
# doesn't need libstdc++-6.dll / libgcc_s_seh-1.dll alongside it.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
