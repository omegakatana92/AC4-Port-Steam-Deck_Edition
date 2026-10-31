# Cross-compilation to Windows for verifying that the Windows build stays healthy on a Linux
# development machine (the release itself is built by the project's pinned llvm-mingw toolchain).
#
#   cmake -S . -B <dir> -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake \
#         -DAC4_BUILD_TESTS=ON -DAC4_BUILD_RUNTIME=OFF
#
# Only used for CI/portability checks; it is not the supported way to build the Windows release.
#
# The -posix compiler variants are required: Debian's default mingw-w64 is built with the win32
# threading model, which has no <mutex>/<thread> (the port uses both everywhere).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_AR x86_64-w64-mingw32-ar)
set(CMAKE_RANLIB x86_64-w64-mingw32-ranlib)
set(CMAKE_STRIP x86_64-w64-mingw32-strip)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
