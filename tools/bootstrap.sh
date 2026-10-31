#!/bin/sh
# Checks (and reports how to install) the tools needed to build the port on GNU/Linux.
#   tools/bootstrap.sh [--install]
#
# Unlike the Windows bootstrap (tools/bootstrap.ps1), nothing is downloaded from the network and
# nothing is installed system-wide: on Linux the distribution's toolchain is used, which keeps the
# build reproducible and the Steam Deck untouched. --install prints the exact package command for
# the detected distribution and asks before running it.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
install=0
[ "${1:-}" = "--install" ] && install=1

missing=""
need() {
  command -v "$1" >/dev/null 2>&1 || missing="$missing $1"
}
need cmake
need git
need "$(command -v clang >/dev/null 2>&1 && echo clang || echo cc)"

# Ninja is preferred but optional (Makefiles work).
have_ninja=0
command -v ninja >/dev/null 2>&1 && have_ninja=1

cmake_version=$(cmake --version 2>/dev/null | head -1 | sed 's/[^0-9.]*\([0-9][0-9.]*\).*/\1/')
case "$cmake_version" in
  3.2[5-9]*|3.[3-9][0-9]*|4.*) ;;
  *) missing="$missing cmake>=3.25" ;;
esac

cxx=$(command -v clang++ >/dev/null 2>&1 && echo clang++ || echo c++)
echo "toolchain: $($cxx --version | head -1)"
echo "cmake:     $cmake_version"
if [ "$have_ninja" = 1 ]; then
  echo "ninja:     $(ninja --version)"
else
  echo "ninja:     not found (Makefiles will be used)"
fi

if [ -n "$missing" ]; then
  echo
  echo "missing:$missing"
  pkg=""
  if [ -r /etc/os-release ]; then
    . /etc/os-release
    case "${ID:-}${ID_LIKE:-}" in
      *debian*|*ubuntu*) pkg="sudo apt install build-essential cmake ninja-build git clang" ;;
      *fedora*|*rhel*)   pkg="sudo dnf install gcc-c++ cmake ninja-build git clang" ;;
      *arch*)            pkg="sudo pacman -S --needed base-devel cmake ninja git clang" ;;
      *steamos*|*arch*)  pkg="sudo pacman -S --needed base-devel cmake ninja git clang" ;;
    esac
  fi
  if [ -n "$pkg" ]; then
    echo "install with: $pkg"
    if [ "$install" = 1 ]; then
      echo "running: $pkg"
      # shellcheck disable=SC2086
      $pkg
    fi
  fi
  exit 1
fi

echo
echo "ready: tools/build.sh --tests"
