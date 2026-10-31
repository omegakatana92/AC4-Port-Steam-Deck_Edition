#!/bin/sh
# Puts the project-local toolchain first on PATH (on Linux, only if tools/bootstrap.sh unpacked
# one; the system compiler is used otherwise).
#   . tools/env.sh
#
# GNU/Linux and SteamOS: nothing is installed system-wide by this project.
set -u

# The repository root: from AC4_ROOT, else from this file's location, else the current directory
# (when the file is sourced from an interactive shell, $0 does not name it).
if [ -n "${AC4_ROOT:-}" ] && [ -f "$AC4_ROOT/CMakeLists.txt" ]; then
  root="$AC4_ROOT"
else
  d=$(dirname -- "$0" 2>/dev/null || echo .)
  if [ -f "$d/../CMakeLists.txt" ]; then
    root=$(CDPATH= cd -- "$d/.." && pwd)
  else
    root=$(pwd)
  fi
fi
AC4_ROOT="$root"
export AC4_ROOT

if [ -d "$root/local/tools/bin" ]; then
  PATH="$root/local/tools/bin:$PATH"
  export PATH
fi

if [ -z "${AC4_CC:-}" ]; then
  if command -v clang >/dev/null 2>&1; then
    AC4_CC=clang
    AC4_CXX=clang++
  else
    AC4_CC=cc
    AC4_CXX=c++
  fi
fi

AC4_BUILD_DIR="${AC4_BUILD_DIR:-$root/local/build/release}"
AC4_BUILD_TYPE="${AC4_BUILD_TYPE:-Release}"
AC4_GENERATOR="${AC4_GENERATOR:-Ninja}"

export AC4_CC AC4_CXX AC4_BUILD_DIR AC4_BUILD_TYPE AC4_GENERATOR
