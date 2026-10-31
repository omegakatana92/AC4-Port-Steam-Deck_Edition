#!/bin/sh
# Configure (first time) and build with the system toolchain.
#   tools/build.sh [-t <target>] [-c Release|RelWithDebInfo|Debug] [--clean] [--tests]
#
# The Windows equivalent is tools/build.ps1. Both configure the same tree; no source differs
# between the platforms except the platform layers themselves.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$root/tools/env.sh"

target=""
clean=0
tests=0
while [ $# -gt 0 ]; do
  case "$1" in
    -t|--target) target=${2:?missing target}; shift 2 ;;
    -c|--config) AC4_BUILD_TYPE=${2:?missing config}; shift 2 ;;
    --clean) clean=1; shift ;;
    --tests) tests=1; shift ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

bdir="$AC4_BUILD_DIR"
if [ "$clean" = 1 ] && [ -d "$bdir" ]; then rm -rf "$bdir"; fi

configure() {
  # Positional parameters avoid word splitting for paths with spaces.
  set -- -S "$root" -B "$bdir" "-DCMAKE_BUILD_TYPE=$AC4_BUILD_TYPE"
  if [ "$AC4_GENERATOR" = Ninja ]; then set -- "$@" -G Ninja; fi
  if [ -n "${AC4_CC:-}" ]; then
    set -- "$@" "-DCMAKE_C_COMPILER=$AC4_CC" "-DCMAKE_CXX_COMPILER=$AC4_CXX"
  fi
  if [ "$tests" = 1 ]; then set -- "$@" -DAC4_BUILD_TESTS=ON; fi
  cmake "$@"
}

if [ ! -f "$bdir/build.ninja" ] && [ ! -f "$bdir/Makefile" ]; then
  configure
elif [ "$tests" = 1 ] && ! grep -q 'AC4_BUILD_TESTS:BOOL=ON' "$bdir/CMakeCache.txt"; then
  configure
fi

if [ -n "$target" ]; then
  cmake --build "$bdir" --target "$target"
else
  cmake --build "$bdir"
fi
