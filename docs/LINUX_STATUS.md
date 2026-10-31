# Linux port — status

Living document. It records what is **verified** (command + result), what is in progress and what
has not been attempted. It is the companion of [`LINUX_PORT_PLAN.md`](LINUX_PORT_PLAN.md).

Last updated: 2026-10-31 — branch `linux-native-port`.

| Area | State | Evidence |
| --- | --- | --- |
| Phase 1 — audit | **done** | `docs/LINUX_PORT_PLAN.md` (946 lines), `docs/BUILD_LOG.md` entry 1 |
| Phase 2 — Linux build of the tools | **done** | 40/40 tests, GCC 11.4.0 and Clang 14.0.0, Ubuntu 22.04 |
| Phase 2 — Windows build preserved | **verified by cross-compile** | tools + `AC4-Launcher.exe` + tests build with mingw-w64 (`cmake/toolchain-mingw-w64.cmake`) |
| Phase 3 — runtime core (memory, VFS, threads, signals) | not started | — |
| Phase 3 — SDL window/input/pads/audio | not started | — |
| Phase 3 — headless Linux runtime | not started | — |
| Phase 4 — Vulkan backend | not started | — |
| Phase 5 — Steam Deck | not started | no hardware run yet |
| Phase 6 — AppImage | not started | — |
| Phase 7 — release | not started | — |

## What runs on Linux today

Only the preparation and recompilation pipeline:

```
ac4-prep        verifies the player's own decrypted files, relocates firmware modules,
                rebuilds SPU code records
ac4-recomp      PowerPC ELF -> C++ (analyze / disasm / func / fingerprint / emit)
ac4-spurecomp   SPU records -> C++
```

They are the same programs as on Windows, built from the same sources. **No game executable can be
run yet**: the runtime and its renderer do not exist for Linux.

## Not verified / do not claim

- The Linux runtime does not build (it is not part of the build on Linux).
- No frame has been rendered with Vulkan.
- No Steam Deck hardware has run anything from this port.
- No AppImage exists.
- The Windows binaries were compiled, not executed, on the Linux development host.

## Next step (Phase 3)

1. `src/platform/`: time, threads (reserved stacks, names), fixed-address reservation + commit +
   write tracking (`mmap`/`mprotect`/`SIGSEGV`), XDG directories, error strings.
2. Port `runtime/vfs.cpp` to POSIX descriptors behind the existing interface, with tests.
3. Port `runtime/memory.cpp`, `ppu_thread.cpp`, `ppu_runtime.cpp` (signal handler, futex,
   timebase) and `lv2/thread.cpp`.
4. Headless runtime target that loads a **synthetic** ELF produced by `ac4-recomp` and executes it
   (arithmetic, `lwarx/stwcx`, guest `setjmp/longjmp`, thread create/join, `sys_timer_usleep`).
   Gate: exits 0 with expected results, no game data involved.
5. Then the SDL2 window/input/pads/audio layer.
