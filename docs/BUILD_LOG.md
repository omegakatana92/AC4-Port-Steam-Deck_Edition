# Build log â€” Linux port

Every build error encountered during the Linux port, with its cause and the fix that resolved it.
Entries are appended, never rewritten: a later fix that supersedes an earlier one is a new entry.

Format: `## N. <date> â€” <target> â€” <platform/toolchain>` then `Command`, `Error`, `Cause`, `Fix`,
`Verified`.

---

## 1. 2026-10-31 â€” `ac4` (configure) â€” Ubuntu 22.04 / GCC 11.4.0 / CMake 4.4.4

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

Planned (Phase 2): select languages per platform â€”
`if(WIN32) project(ac4 LANGUAGES C CXX RC) else() project(ac4 LANGUAGES C CXX) endif()` â€” and add
`.rc` sources only when `WIN32`. No functional change on Windows.

**Verified**

Not yet fixed. This entry is the Phase 1 baseline measurement and the first task of Phase 2.

---

## 2. 2026-10-31 — `ac4` (configure) — fixed — resolves entry 1

**Cause:** `project(ac4 LANGUAGES C CXX RC)` and `.rc` sources in every tool target.

**Fix:** `CMakeLists.txt` declares `RC` only for `WIN32`:

```cmake
if(WIN32)
  project(ac4 LANGUAGES C CXX RC)
else()
  project(ac4 LANGUAGES C CXX)
endif()
```

and `AC4_UTF8_RC` / `AC4_RT_RC` expand to nothing on Unix, so the `.rc` sources disappear with the
language. The Windows definitions (`_WIN32_WINNT`, `WINVER`, `NOMINMAX`, `UNICODE`, `_UNICODE`) are
gated behind `WIN32` too.

**Verified:** configure succeeds on Ubuntu 22.04 with GCC 11.4.0 and with Clang 14.0.0, and the
same tree still configures for Windows (mingw cross build, entry 6).

---

## 3. 2026-10-31 — `ac4_common` (link) — fixed — would have been the next failure

**Error (expected):**

```
/usr/bin/ld: cannot find -lbcrypt
```

`CMakeLists.txt` linked `bcrypt` unconditionally and `src/common/crypto.cpp` used Windows CNG.

**Fix:** added a portable SHA-1/SHA-256 (`src/common/sha.{h,cpp}`) and made the backend a build
option:

```cmake
option(AC4_USE_BCRYPT "..." ON)   # ON on Windows, forced OFF elsewhere
```

`common/crypto.cpp` keeps the CNG path when `_WIN32 && AC4_USE_BCRYPT`, and otherwise calls
`ac4::sha::`. Both paths return the same digests; the portable one is checked against the NIST
vectors in `tests/test_sha.cpp`.

**Verified:** GCC and Clang builds link without bcrypt; 6 hash tests pass (empty, "abc", the
448-bit and 896-bit NIST messages, one million 'a', and a 2.5 MB file through the streaming path).

---

## 4. 2026-10-31 — `ac4-tests` (SHA) — fixed — found by the test suite, not the compiler

**Error:**

```
FAIL sha1_nist_vectors
  CHECK_EQ failed: sha1_hex(kMsg448) == "84983e44..."
FAIL sha256_nist_vectors
  CHECK_EQ failed: sha256_hex(kMsg448) == "248d6a61..."
```

**Cause:** the padding helper returned both blocks at the same address when the message length left
no room for the 64-bit length, so the "length" block hashed the message block again. Short messages
(one block) and long messages whose tail happened to leave room were unaffected, which is why only
the 56-byte message failed.

**Fix:** `pad_block` writes the second block at `buf + 64` inside a 128-byte buffer and emits a
distinct pointer for it (`src/common/sha.cpp`).

**Verified:** all SHA-1/SHA-256 vectors pass, including the streaming path with every chunk size
around the block boundary.

---

## 5. 2026-10-31 — `ac4_common` (GCC 11) — fixed

**Warning:**

```
src/common/sfo.cpp: In function 'std::vector<unsigned char> ac4::sfo::build(...)':
  warning: 'void* __builtin_memcpy(...)' reading 1 or more bytes from a region of size 0
  [-Wstringop-overread]
```

**Cause:** GCC 11 mis-analyses `out.insert(out.end(), range.begin(), range.end())` after
`std::memcpy` writes into a perfectly sized 20-byte vector (a known false positive for vector
self-insert-like patterns).

**Fix:** `sfo::build` allocates the final size once and copies header, index, keys and data with
explicit `std::memcpy` calls (`src/common/sfo.cpp`). Same bytes, no warning, no reliance on
optimiser behaviour.

**Verified:** `sfo_build_parse_roundtrip` compares `build(parse(build(m)))` with `build(m)`
byte-for-byte; GCC 11 build is warning-free.

---

## 6. 2026-10-31 — Windows cross build (mingw-w64) — fixed

**Error:**

```
src/common/file.cpp:16:6: error: 'mutex' in namespace 'std' does not name a type
src/common/crypto.cpp:36:27: error: 'BCryptHash' was not declared in this scope
```

**Cause:** two independent issues.

1. Debian/Ubuntu's default `x86_64-w64-mingw32-g++` is built with the **win32 threading model**,
   which does not provide `<mutex>`/`<thread>`. The port uses both extensively.
2. `BCryptHash` (the one-shot CNG call) is newer than some Windows SDK headers.

**Fix:**

1. `cmake/toolchain-mingw-w64.cmake` (CI-only) selects the `-posix` compiler variants
   (`x86_64-w64-mingw32-g++-posix`).
2. `crypto.cpp` uses `BCryptCreateHash`/`BCryptHashData`/`BCryptFinishHash`, available since Vista
   and identical in result (`BCryptHash` is a wrapper over exactly this sequence).

**Verified:** the Windows branch builds the tools, the launcher (`AC4-Launcher.exe`) and the test
executable with mingw-w64 GCC 10 from Linux. This is a compile check only; the release still uses
the pinned llvm-mingw toolchain from `data/toolchain.txt`, and the Windows tests are not executed
here.

---

## 7. 2026-10-31 — `ac4-tests` (link, Clang) — fixed

**Error:**

```
/usr/bin/ld: libac4_common.a(file.cpp.o): undefined reference to
  std::filesystem::absolute(...), std::runtime_error::runtime_error(...), std::_V2::system_category()
FAILED: ac4-spurecomp
```

**Cause:** `tools/check.sh` set `CMAKE_CXX_COMPILER=clang` instead of `clang++`, so CMake linked
C++ targets with the C driver and libstdc++ was never added to the link line.

**Fix:** `tools/check.sh` maps `gcc ? g++` and `clang ? clang++`.

**Verified:** `tools/check.sh` builds and tests with GCC 11.4.0 and Clang 14.0.0; 39 tests pass on
both.

---

## Phase 2 result

```
$ tools/check.sh
== gcc ==
100% tests passed out of 1
== clang ==
100% tests passed out of 1
all compilers passed
```

Built targets on Linux: `ac4_zlib`, `ac4_common`, `ac4_ppu`, `ac4_spu`, `ac4_recomp`, `ac4_prep`,
`ac4-spurecomp`, `ac4-recomp`, `ac4-prep`, `ac4-tests`. Not built: `ac4` (runtime, Phase 3/4) and
`ac4-launcher` (Windows only).