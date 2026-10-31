# Building on Linux

Status: **the preparation and recompilation tools build and are tested on Linux. The game runtime
does not build on Linux yet** — that is Phase 3/4 of [`docs/LINUX_PORT_PLAN.md`](LINUX_PORT_PLAN.md).
Nothing in this document claims the game is playable.

## What builds today

| Target | Purpose | Linux |
| --- | --- | --- |
| `ac4-prep` | verifies the player's decrypted files and prepares them | yes |
| `ac4-recomp` | PowerPC → C++ static recompiler | yes |
| `ac4-spurecomp` | SPU records → C++ | yes |
| `ac4-setup` | Linux preparation/launch helper | not yet (Phase 3) |
| `ac4` | the game runtime | not yet (Phase 3/4) |
| `ac4-launcher` | Windows GUI launcher | Windows only (not built on Linux) |

## Requirements

- 64-bit x86 Linux (Ubuntu 22.04+, SteamOS 3.x, Fedora, Arch)
- CMake ≥ 3.25
- Ninja (recommended) or Make
- GCC ≥ 11 or Clang ≥ 14, with x86-64-v3 (AVX2/FMA/BMI2) on the machine that later runs the game
- No network downloads: the Linux build uses the system toolchain

`tools/bootstrap.sh` checks these and prints the install command for the detected distribution.

## Build

```sh
tools/bootstrap.sh          # checks the toolchain (add --install to run the package command)
tools/build.sh              # configure + build the tools
tools/build.sh --tests      # ... and the test suite
ctest --test-dir local/build/release --output-on-failure
```

Build directories default to `local/build/<config>` (already in `.gitignore`); override with
`AC4_BUILD_DIR=/path`. Compiler selection:

```sh
AC4_CC=clang AC4_CXX=clang++ tools/build.sh
```

`tools/check.sh` builds and tests with every compiler it finds — that is what CI runs.

## Tests

The suite uses **synthetic fixtures only** (no game data, no game-derived code):

- SHA-1/SHA-256 against the NIST vectors, plus the file-hashing path across read buffers
- INI editing, atomic save, the shared settings schema, the legacy-layout migration
- PARAM.SFO build/parse round-trip
- PPU and SPU instruction decoding, SPU chunk discovery and hashing
- PRX relocation to a fixed base (synthetic PRX)
- A synthetic PowerPC64 ELF through `ac4-recomp`: analysis, emission, and the real tool binary

Measured on Ubuntu 22.04 (WSL2), 2026-10-31:

```
GCC 11.4.0   : 39 tests, 0 failed
Clang 14.0.0 : 39 tests, 0 failed
```

## Configuration options

| Option | Default | Meaning |
| --- | --- | --- |
| `AC4_BUILD_TESTS` | OFF | builds and registers `ac4-tests` |
| `AC4_BUILD_RUNTIME` | ON (Windows), OFF (Linux) | the game runtime ; Linux support is in progress |
| `AC4_USE_BCRYPT` | ON (Windows), OFF (else) | Windows CNG hashing instead of `common/sha.cpp` |
| `AC4_DEV` | OFF | developer diagnostics in the runtime |
| `AC4_GEN_DIR` | `local/gen/ppu` | recompiled PowerPC code (game-derived, never committed) |
| `AC4_SPU_GEN_DIR` | `local/gen/spu` | recompiled SPU code (game-derived, never committed) |

## Checking the Windows build from Linux

The Windows branch must keep working. A cross build compiles the tools, the launcher and the tests
(it needs `g++-mingw-w64-x86-64-posix`; the `-posix` variants are required because Debian's default
mingw-w64 uses the win32 threading model, which has no `<mutex>`/`<thread>`):

```sh
cmake -S . -B build-win -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake \
  -DCMAKE_BUILD_TYPE=Release -DAC4_BUILD_TESTS=ON -DAC4_BUILD_RUNTIME=OFF
cmake --build build-win
```

This is a compile check, not a substitute for the pinned llvm-mingw release build
(`tools/build.ps1` on Windows) and not a test run.

## SteamOS notes

- SteamOS 3 has a read-only root filesystem: build in Desktop Mode's user space, or in a container.
  `local/build` (inside the project) needs no system changes.
- The Deck's APU (Zen 2 + RDNA 2) satisfies x86-64-v3, which `AC4_CPU_FLAGS` (`-march=haswell`)
  requires.
- Packaging into an AppImage is Phase 6; the intended filename is
  `ArmoredCore4-SteamDeck-x86_64.AppImage`.

## Troubleshooting

- `No CMAKE_RC_COMPILER could be found` — you are on a revision before the platform split. Update
  to `linux-native-port` or newer.
- `undefined reference to std::filesystem::...` — the C++ compiler was invoked as the linker
  (CMake given `clang` as `CMAKE_CXX_COMPILER`). Use `clang++`.
- `error: ‘mutex’ in namespace ‘std’ does not name a type` in a mingw cross build — use the
  `-posix` compiler variants (see above).
- `this CPU lacks avx2 ...` from `ac4-prep cpu` — the generated game code needs x86-64-v3.
