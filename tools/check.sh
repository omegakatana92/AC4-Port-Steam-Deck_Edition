#!/bin/sh
# Builds the tools and runs the tests with every compiler available (GCC and Clang), each in its
# own build directory. This is what CI runs on Linux.
#   tools/check.sh [build-root]
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$root/tools/env.sh"
build_root=${1:-"$root/local/build"}
failed=0
ran=0

for cc in gcc clang; do
  case "$cc" in
    gcc) cxx=g++ ;;
    clang) cxx=clang++ ;;
    *) cxx=$cc ;;
  esac
  command -v "$cc" >/dev/null 2>&1 || { echo "skip: $cc not installed"; continue; }
  ran=$((ran + 1))
  dir="$build_root/$cc"
  echo "== $cc =="
  if AC4_BUILD_DIR="$dir" AC4_CC="$cc" AC4_CXX="$cxx" sh "$root/tools/build.sh" --clean --tests; then
    (cd "$dir" && ctest --output-on-failure)
  else
    echo "FAILED: build with $cc"
    failed=$((failed + 1))
  fi
done

if [ "$ran" = 0 ]; then
  echo "no compiler found (install gcc or clang)" >&2
  exit 1
fi
if [ "$failed" != 0 ]; then
  echo "$failed compiler(s) failed" >&2
  exit 1
fi
echo "all compilers passed"
