# Build log — Linux port

Every build error encountered during the Linux port, with its cause and the fix that resolved it.
Entries are appended, never rewritten: a later fix that supersedes an earlier one is a new entry.

Format: `## N. <date> — <target> — <platform/toolchain>` then `Command`, `Error`, `Cause`, `Fix`,
`Verified`.

---

## 1. 2026-10-31 — `ac4` (configure) — Ubuntu 22.04 / GCC 11.4.0 / CMake 4.4.4

**Command**

```bash
cmake -S <repo> -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

**Error**

```
-- The C compiler identification is GNU 11.4.0
-- The CXX compiler identification is GNU 11.4.0
CMake Error at CMakeLists.txt:2 (project):
  No CMAKE_RC_COMPILER could be found.

  Tell CMake where to find the compiler by setting either the environment
  variable "RC" or the CMake cache entry CMAKE_RC_COMPILER to the full path
  to the compiler, or to the compiler name if it is in the PATH.

-- Configuring incomplete, errors occurred!
```

**Cause**

`CMakeLists.txt:2` declares `project(ac4 LANGUAGES C CXX RC)`. `RC` is the Windows resource
compiler; it does not exist on Linux, and the target definitions add `src/common/utf8.rc`,
`src/runtime/game.rc` and `src/launcher/launcher.rc` as sources.

**Fix**

Planned (Phase 2): select languages per platform —
`if(WIN32) project(ac4 LANGUAGES C CXX RC) else() project(ac4 LANGUAGES C CXX) endif()` — and add
`.rc` sources only when `WIN32`. No functional change on Windows.

**Verified**

Not yet fixed. This entry is the Phase 1 baseline measurement and the first task of Phase 2.
