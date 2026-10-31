# Armored Core 4 — Native Linux / Steam Deck Port: Dependency & Portability Audit

**Phase 1 deliverable.** This document is the dependency audit required before any major source
change. It records what exists, what is Windows-specific, what is portable, how the recompilers and
the renderer are structured, and the concrete plan (with gates) for Phases 2–7.

- **Audited revision:** `main` @ `219c7bf` (`README: mouse modes`), `VERSION` = `0.6.0`
- **Audited tree:** `omegakatana92/AC4-Port-Steam-Deck_Edition` (fork of `SulphurS16/AC4-Port`)
- **Audit host:** Windows 11 + WSL2 `Ubuntu-22.04` (GCC 11.4.0, CMake 4.4.4, Ninja 1.10.1, 8 cores)
- **Method:** full read of the build system and every subsystem header; per-file symbol inventory of
  Win32 / D3D11 / DXGI / XInput / WASAPI / CNG / COM usage; a real Linux configure run to capture
  the first hard blocker (see §3.2). No source changes were made in Phase 1.

> Nothing in this document claims the game runs on Linux. As of this audit **no Linux build of the
> runtime exists**, and the Direct3D 11 renderer has no Linux equivalent yet. Statements about
> behaviour are statements about the Windows implementation, not about a working Linux port.

---

## 1. Executive summary

| Measure | Value |
| --- | --- |
| Tracked files | 221 |
| C/C++ source (`.cpp`+`.h`) | 173 files, ~36,300 lines |
| Runtime + common only | ~26,600 lines |
| Files that include `<windows.h>` | 40 of 173 (~23 %) |
| Lines in those files (excl. launcher) | ~11,500 of ~26,600 runtime+common lines (~43 %) |
| Direct3D 11 call sites (`ID3D11*`/`D3D11_*`/`DXGI_*`) | ~580, concentrated in `renderer.cpp` (398), `d3d11_device.cpp` (90), `submit.cpp` (79) |
| Files that are **portable with no edits** | 19 runtime/common + all 18 tool/recompiler files + vendored zlib |
| Files that need a **small shim** (≤ ~80 lines each) | 19 |
| Files that need a **Linux implementation behind an existing interface** | 10 |
| Files that are **Windows-only and replaced** | 9 (D3D11 device, Win32 window, Direct2D/DirectWrite overlay, launcher GUI + 5 launcher modules, `.rc` resources) |

**Headline findings**

1. **The recompilers are already portable.** `ac4-prep`, `ac4-recomp`, `ac4-spurecomp`, the PPU
   decoder and the SPU ISA contain **zero** Windows headers and no MSVC-only constructs. They use
   GCC/Clang builtins (`__builtin_cpu_supports`, `__builtin_bswap*`) and SSE/AVX intrinsics, which
   are exactly what Linux GCC/Clang provide. Phase 2 can produce real, tested Linux binaries for
   the whole preparation pipeline.
2. **The build system is the first hard blocker, not the code.** `project(... LANGUAGES C CXX RC)`
   fails immediately on Linux (verified, §3.2). `bcrypt`, `d3d11`, `dxgi`, `d3dcompiler`, `d2d1`,
   `dwrite`, `ole32`, `psapi`, `synchronization`, `hid`, `setupapi`, `comctl32`, `winhttp`,
   `shell32`, `uuid`, `advapi32` are linked unconditionally.
3. **The guest execution core is OS-agnostic in design.** The 4 GiB guest address space at
   `0x0000_2000_0000_0000`, the big-endian memory model, the recompiled function table, the OPD
   dispatch, the 128-byte reservation stripes and the guest setjmp/longjmp bridge all sit on top of
   a small set of primitives (`mem()`, `icall`, `syscall`, `trap`, `timebase`). Porting those
   primitives is a bounded job; the guest semantics do not need to change.
4. **The hard part is the renderer.** `renderer.cpp` (2,886 lines) is a direct D3D11 immediate-mode
   renderer with 398 D3D call sites, plus a D3D11-shaped command recorder (`submit.cpp`) and a
   Direct2D/DirectWrite UI overlay (`overlay.cpp`). There is **no backend abstraction today**. A
   Vulkan backend is a real re-implementation of the draw path, not a recompile.
5. **Shaders are generated HLSL, not hand-written.** `vp_hlsl.cpp` and `fp_hlsl.cpp` emit HLSL from
   RSX microcode; shaders are compiled with `D3DCompile` and cached as DXBC. This is the single most
   important portability asset on the graphics side: **the same generated HLSL can be compiled to
   SPIR-V with DirectX Shader Compiler (DXC) `-spirv`**, so the RSX→HLSL translation layer, its
   cache keys and its validation harness carry over unchanged.
6. **Windows-only UI cannot be carried over verbatim.** The port currently draws its own settings
   screen and system dialogs with Direct2D/DirectWrite and uses *Times New Roman / Bahnschrift /
   Segoe UI*, which cannot be redistributed. The Linux port needs a 2D drawing layer in Vulkan plus
   a redistributable font (OFL/permissive).
7. **The launcher is a Windows GUI program** (`CreateWindowEx`, common controls, `WinHTTP`, registry,
   `ShellExecute`, `SHCreateDirectory`). It must not be "ported line by line"; Linux needs its own
   small setup entry point. Everything the launcher does is already available as CLI logic in
   `ac4-prep` + CMake.
8. **Steam Deck hardware is a good match.** Van Gogh (Zen 2 + RDNA 2) supports x86-64-v3
   (AVX2/FMA/BMI2), which the recompiled code and runtime already require (`-march=haswell`), and
   Vulkan 1.3 is present in SteamOS. 1280×800, 30/60 fps and gamepad-only play are all already
   modelled in the Windows settings layer (`common/settings_file.cpp`).

---

## 2. Repository inventory

```
CMakeLists.txt            single build file, 172 lines, Windows-only assumptions
VERSION                   0.6.0
LICENSE                   MIT (port code only)
THIRD_PARTY.md            zlib (in-tree), llvm-mingw/CMake/Ninja (downloaded), Windows runtime libs
data/                     ac4.ico, shader_manifest.txt, spu_manifest.txt, toolchain.txt
licenses/                 LLVM license, MinGW-w64 runtime notices
third_party/zlib/         zlib 1.3.1, unmodified (portable C)
tools/                    bootstrap.ps1, build.ps1, build-launcher.ps1, env.ps1  (PowerShell only)
src/common/      (16 files)  byte helpers, SHA-1/256 (CNG), file helpers, INI, privacy, settings, SFO
src/prep/         (6 files)  ac4-prep: keyless import of the user's decrypted files, PRX relocation,
                             PARAM.SFO, SPU code-record rebuild
src/ppu/          (4 files)  PPU decode + instruction semantics (header-only, SSE/AVX)
src/recomp/       (7 files)  ac4-recomp: ELF image, control-flow analysis, C++ emitter
src/spu/          (5 files)  SPU ISA, chunk discovery, generated-code interface
src/spurecomp/    (1 file)   ac4-spurecomp: SPU records -> C++
src/runtime/      (105 files) the native host: loader, PPU threads, LV2, HLE, SPU, RSX, GPU, audio,
                             media, input, UI, VFS, logging
src/launcher/     (16 files) Windows GUI launcher (setup, downloads, RPCS3 driving, install)
```

Subsystem sizes (lines, `.cpp` + `.h`):

| Subsystem | Files | Lines | Portability |
| --- | --- | --- | --- |
| `common` | 16 | ~1,150 | 3 Win32 files (crypto, ini, privacy) |
| tools (`prep`,`recomp`,`ppu`,`spu`,`spurecomp`) | 23 | ~4,300 | portable |
| `runtime` | 105 | ~21,600 | 40 Win32/D3D11 files |
| `runtime/gpu` | 21 | ~6,000 | 4 files are D3D11/DXGI |
| `runtime/hle` | 24 | ~5,600 | 8 files touch Win32 |
| `runtime/lv2` | 10 | ~1,900 | 2 files |
| `launcher` | 16 | ~3,400 | Windows GUI (replaced) |

---

## 3. Build system audit

### 3.1 What the current CMake does

Targets, in build order:

| Target | Sources | Links | Notes |
| --- | --- | --- | --- |
| `ac4_zlib` (static) | `third_party/zlib/*.c` | — | portable |
| `ac4_common` (static) | 8 `common/*.cpp` | `ac4_zlib` + **`bcrypt`** | SHA-1/256 via CNG |
| `ac4_ppu` (static) | `ppu/ppu_decode.cpp` | `ac4_common` | portable |
| `ac4_spu` (static) | `spu/spu_isa.cpp` | `ac4_common` | portable |
| `ac4-spurecomp` | `spurecomp/main.cpp` + `utf8.rc` | `ac4_spu` | **RC source** |
| `ac4-recomp` | `recomp/*.cpp` + `utf8.rc` | `ac4_ppu` | **RC source** |
| `ac4-prep` | `prep/*.cpp` + `utf8.rc` | `ac4_common` | **RC source** |
| `ac4` (game) | all `runtime/**/*.cpp` + `utf8.rc` + `game.rc`, plus generated `local/gen/ppu/*.cpp` and `local/gen/spu/*.cpp` | `d3d11 dxgi d3dcompiler d2d1 dwrite ole32 psapi synchronization hid setupapi` | **only built if game-derived code exists** |
| `ac4-launcher` | `launcher/*.cpp` + `launcher.rc` (`WIN32`) | `comctl32 winhttp shell32 ole32 uuid advapi32`, `-municode` | Windows GUI |

Global settings that are Windows-only or Windows-specific:

```cmake
project(ac4 LANGUAGES C CXX RC)                                    # line 2  – no RC on Linux
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX UNICODE _UNICODE)  # line 13
if(MINGW) add_link_options(-static -Wl,--no-insert-timestamp) endif()                 # 38-40
set(AC4_UTF8_RC ${CMAKE_SOURCE_DIR}/src/common/utf8.rc)                               # 43
set(AC4_CPU_FLAGS -march=haswell)                                                     # 92
set_target_properties(ac4 PROPERTIES WIN32_EXECUTABLE ON)                             # 122
```

Positive findings:

- **Compilers:** the code is written for Clang (`-ffile-prefix-map`, `-Wall -Wextra`, GCC builtins).
  Nothing requires MSVC; nothing is MinGW-exclusive except the link flags and `.rc` files.
- `CMAKE_EXPORT_COMPILE_COMMANDS` is already on, so `clangd` works for Linux development out of the box.
- Generated-code directories are configurable (`AC4_GEN_DIR`, `AC4_SPU_GEN_DIR`) and the runtime is
  built **only** when generated PPU code exists. That is exactly the property needed for a lawful
  distribution: no game-derived code in the repository, buildable only from the user's own data.
- `ac4-recomp`/`ac4-spurecomp` output is byte-stable and written only on change, which makes the
  Linux and Windows pipelines interchangeable.

### 3.2 Verified first blocker (evidence)

Configure with the stock tree on Ubuntu 22.04 / GCC 11.4:

```
$ cmake -S <repo> -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
-- The C compiler identification is GNU 11.4.0
-- The CXX compiler identification is GNU 11.4.0
CMake Error at CMakeLists.txt:2 (project):
  No CMAKE_RC_COMPILER could be found.
```

Recorded in `docs/BUILD_LOG.md` (entry 1). Everything after this point is masked by it; the audit
predicts the following next failures, in order:

1. `LANGUAGES C CXX RC` and every `.rc` source.
2. `add_compile_definitions(_WIN32_WINNT …)` is harmless on Linux but meaningless; must be gated.
3. `target_link_libraries(ac4_common PUBLIC … bcrypt)` → no such library.
4. `ac4` link line: `d3d11 dxgi d3dcompiler d2d1 dwrite psapi synchronization hid setupapi`.
5. `ac4-launcher`: `WIN32` executable type, `-municode`, `comctl32`, `winhttp`, `.rc`.
6. Compile errors in the 40 `<windows.h>` translation units, starting at `main.cpp`.

### 3.3 Proposed CMake shape (Phase 2)

Keep one tree, two platforms; do not fork the build file.

```
option(AC4_PLATFORM_WINDOWS "" ${WIN32})   # derived, not user-facing
if(WIN32)        → project(LANGUAGES C CXX RC); Windows sources; bcrypt, d3d11, …
elseif(UNIX)     → project(LANGUAGES C CXX); no .rc; find_package(SDL2, Vulkan, Threads)
```

New Linux targets and libraries:

| Target | Purpose |
| --- | --- |
| `ac4-platform` (static) | the only place with OS APIs: paths, threads, time, memory reservation, dynamic loading, error strings, hashing |
| `ac4-render-core` (static) | RSX state tracking, textures, vertex conversion, shader cache, **backend-agnostic** |
| `ac4-render-vulkan` (static) | Vulkan device/swapchain, pipelines, command recording, 2D overlay |
| `ac4-render-d3d11` (static) | existing Windows path, unchanged behaviour |
| `ac4` | runtime, links the platform-appropriate backend |
| `ac4-setup` | Linux preparation/launch CLI (replaces the Windows launcher GUI) |
| `ac4-tests` | opt-in (`-DAC4_BUILD_TESTS=ON`), synthetic fixtures only, no game data |

---

## 4. Platform-independent core (usable on Linux today)

Verified by inspection: no `<windows.h>`, no Win32 symbol, no MSVC extension.

| Area | Files |
| --- | --- |
| Byte order / formatting | `common/bytes.{h,cpp}` |
| INI model (pure text) | `common/ini.{h,cpp}` (only `save()` uses `MoveFileExW`) |
| Settings schema (pure text) | `common/settings_file.{h,cpp}` |
| PARAM.SFO | `common/sfo.{h,cpp}`, `prep/sfo.cpp` |
| Known-input hashes (data only) | `common/known_inputs.{h,cpp}` |
| PPU decoder | `ppu/ppu_decode.cpp`, `ppu/ppu_insn.h` |
| PPU instruction semantics | `ppu/ppu_context.h`, `ppu/ppu_recomp.h` (SSE/AVX intrinsics; haswell-class) |
| SPU ISA + chunk discovery | `spu/spu_isa.{h,cpp}`, `spu/spu_ops.h`, `spu/spu_gen.h`, `spu/spu_gen_empty.cpp` |
| Recompiler | `recomp/{image,analysis,emit}.cpp`, `recomp/main.cpp` |
| SPU recompiler | `spurecomp/main.cpp` |
| Preparation | `prep/{import,prx,spu_records}.cpp`, `prep/main.cpp`, `prep/formats.h` |
| Guest helpers | `runtime/guest.h`, `runtime/guest_printf.cpp`, `runtime/guest_setjmp.cpp` |
| ELF loader (guest image) | `runtime/loader.{h,cpp}` |
| LV2 sync objects | `runtime/lv2/sync.cpp` (pure C++17 synchronisation) |
| Reservation stripes | `runtime/reservation.h` (only `_mm_pause`) |
| MPEG-2 decoder | `runtime/media/mpeg2_video.cpp` |
| PNG writer | `runtime/gpu/png.cpp` (zlib) |
| Shader text generation | `runtime/gpu/vp_hlsl.cpp`, `fp_hlsl.cpp` (emit HLSL strings) |
| Game hooks | most of `runtime/game/*` (hard-coded guest addresses, no OS calls) |

Portability caveats that are *not* Windows headers but still need attention:

- **`common/file.cpp`** normalises paths by lower-casing and converting `/`→`\`, and compares with
  `wstring`. On Linux `/` is the separator and case matters. The protected-root logic must switch to
  `generic_string()` and case-sensitive comparison, or it will silently mis-compare on Linux.
- **`config.h`/`main.cpp`** build user-visible paths with `\` in comments and text only; the actual
  code uses `std::filesystem`, except for the save-data path handed to the guest (`/dev_hdd0/...`,
  which is already POSIX-shaped and therefore *easier* on Linux).
- **`size_t` printf formats** use `%zu`, fine; `%llx` casts are explicit, fine.
- **`strprintf`** writes `n+1` bytes into an `std::string` of size `n`; legal (the terminator), but
  worth a comment or an `s.resize(n+1)` if any sanitizer complains.
- **`-march=haswell`** is applied to generated code and the runtime. Steam Deck (Zen 2) satisfies
  x86-64-v3. Keep it, but expose `AC4_CPU_FLAGS` as a cache variable so ARM64/older x86 hosts can be
  diagnosed rather than mysteriously crashing.

---

## 5. Windows-only inventory

### 5.1 Per-file classification

**A — portable as-is** (see §4). **B — small shim** (one or two functions). **C — needs a Linux
implementation behind the existing interface**. **D — Windows-only, excluded/replaced on Linux.**

#### common

| File | Lines | Class | Windows dependency → replacement |
| --- | --- | --- | --- |
| `crypto.cpp` | 48 | C | CNG `BCryptHash*` → in-tree SHA-1/SHA-256 (≈150 lines, no new dependency, keeps MIT) *or* OpenSSL. Recommendation: in-tree, because the hashes only verify the user's own files and adding libcrypto bloats the AppImage |
| `ini.cpp` | 174 | B | `MoveFileExW(REPLACE_EXISTING)` → `rename(2)` (atomic on POSIX within a filesystem) |
| `privacy.cpp` | 72 | C | `GetEnvironmentVariableW`/`USERPROFILE`/`USERNAME` → `getenv("HOME")`, `getenv("USER")`; redaction token `%USERPROFILE%` → `~` (keep the token name for log compatibility) |
| `bytes.cpp`, `sfo.cpp`, `known_inputs.cpp`, `settings_file.cpp` | ~530 | A | — |
| `utf8.rc`, `utf8.manifest` | 4 | D | dropped on Linux |

#### runtime — core

| File | Lines | Class | Windows dependency → replacement |
| --- | --- | --- | --- |
| `main.cpp` | 341 | C | `SetProcessAffinityMask`, `SetPriorityClass`, `GetProcessMemoryInfo`, `GetSystemTimes`, `GetProcessTimes`, `ExitProcess`, `gmtime_s` → `sched_setaffinity`, `setpriority`, `/proc/self/stat(m)`, `/proc/stat`, `_exit`, `gmtime_r` |
| `log.cpp` | 76 | B | `GetCurrentThreadId`→`gettid()`, `TerminateProcess`→`_exit(3)` |
| `memory.cpp` | 230 | C | `VirtualAlloc(RESERVE/COMMIT)`, `VirtualFree(DECOMMIT)`, **`MEM_WRITE_WATCH` + `GetWriteWatch`** → `mmap(MAP_FIXED_NOREPLACE|MAP_NORESERVE)`, `mprotect`, `madvise`; write tracking → per-range `mprotect(PROT_READ)` + `SIGSEGV` dirty-page collection (or `/proc/self/pagemap` soft-dirty). **This is the highest-risk core item** (see §9.3) |
| `ppu_thread.cpp` | 274 | C | `CreateThread`+`STACK_SIZE_PARAM_IS_A_RESERVATION`, `DWORD WINAPI`, `SetThreadDescription`, `ExitProcess` → `pthread_create` with `pthread_attr_setstacksize`, `pthread_setname_np` (16-char limit), `_exit` |
| `ppu_runtime.cpp` | 449 | C | `AddVectoredExceptionHandler`+`PEXCEPTION_POINTERS` → `sigaction(SIGSEGV/SIGILL/SIGFPE/SIGBUS)` + `ucontext_t` (x86-64 `REG_RIP`); `WaitOnAddress`/`WakeByAddressAll` → `futex(FUTEX_WAIT_PRIVATE/FUTEX_WAKE_PRIVATE)`; `QueryPerformanceCounter` → `clock_gettime(CLOCK_MONOTONIC)`; `GetProcessAffinityMask`/`GetSystemInfo` → `sched_getaffinity` |
| `vfs.cpp` | 407 | C | `CreateFileW/ReadFile/WriteFile/SetFilePointerEx/GetFileAttributesExW/MoveFileExW/RemoveDirectoryW/DeleteFileW/GetFileInformationByHandle/SetFileInformationByHandle` → `open/pread/pwrite/lseek/fstat/stat/mkdir/rmdir/unlink/rename/utimensat/opendir/readdir`. The guest ABI (CELL_E* codes, fd table, mount resolution) is unchanged; `_stricmp` → `strcasecmp` (guest paths are case-sensitive on the console but the current code matches mounts case-insensitively — keep that behaviour) |
| `reservation.h` | 60 | A | `_mm_pause` only |
| `loader.cpp` | 247 | A | — |
| `bindings.cpp` | 421 | B | `windows.h` only for `VK_*` virtual-key constants → local `platform/keys.h` with the same numeric values (keeps `settings.ini` bindings compatible across OSes) |
| `settings.cpp` | 163 | B | monitor device names (`\\.\DISPLAY2`) → SDL display names; the INI string stays opaque |
| `input.cpp` | 103 | C | `RegisterRawInputDevices`/`GetRawInputData`/`GetSystemMetrics`/`MOUSE_MOVE_ABSOLUTE` → SDL relative mouse mode + `SDL_MOUSEMOTION`/`SDL_MOUSEWHEEL` (capture the same "raw-ish" deltas; see §9.4) |
| `gamepads.{h,cpp}` | 368 | C | XInput (`LoadLibraryW("xinput1_4.dll")`) + HID (`hidsdi.h`, `hidpi.h`, `setupapi.h`, one thread per pad, manual DS4/DualSense report parsing) → **SDL2 GameController API**, which already parses DS4/DualSense/Switch/Steam and gives the XInput layout the rest of the code expects. Keep `XINPUT_GAMEPAD` as the internal struct to avoid touching `sys_io.cpp`, `bindings.cpp`, `overlay.cpp` |
| `sysdialog.cpp` | 456 | B | `MultiByteToWideChar` (2 calls) → UTF-8↔UTF-16 helper; logic is platform-neutral |
| `audio/audio_out.cpp` | 81 | C | WASAPI shared mode (48 kHz float stereo, `AUTOCONVERTPCM`) → SDL2 audio device (`AUDIO_F32SYS`, 48 kHz, 2 ch, same non-blocking "drop if full" contract) |
| `ui/overlay.cpp` | 1,267 | D | Direct2D 1.1 + DirectWrite + three Windows font families → Vulkan 2D layer + redistributable font (§9.5) |
| `config.h`, `profiler.h`, `sysutil.h` | 96 | A/B | `profiler.h` is already a no-op stub in player builds; keep it |

#### runtime — HLE (LV2 user library)

| File | Lines | Class | Note |
| --- | --- | --- | --- |
| `cellAudio.cpp` | 419 | B | Audio output contract only (`audio_out.h`); `SetThreadDescription` is cosmetic |
| `cellGcmSys.cpp` | 257 | B | `windows.h` for thread/time types only; RSX state is pure memory |
| `cellL10n.cpp` | 149 | B | `WideCharToMultiByte`/`MultiByteToWideChar` with codepages → iconv-free UTF-8/UTF-16/UTF-32 + Latin-1 tables (`cellL10n` only needs UTF-8/16/32 and a few single-byte pages) |
| `cellRtc.cpp` | 112 | B | `GetTimeZoneInformation`/`GetSystemTimePreciseAsFileTime`/`FILETIME` → `localtime_r`, `clock_gettime(CLOCK_REALTIME)`, `tm_gmtoff` |
| `cellSysutil.cpp` | 268 | B | timezone of `CELL_SYSUTIL_SYSTEMPARAM_ID_TIMEZONE` |
| `save_data.cpp` | 755 | C | `WIN32_FILE_ATTRIBUTE_DATA`/`GetFileAttributesExW` for mtimes → `stat`; everything else already goes through `vfs.h`; save layout (`SAVEDATA.BIN`, PARAM.SFO, `PARAM.PFD`) is host-agnostic |
| `sysPrxForUser.cpp` | 477 | B | `_snprintf`, `SwitchToThread` |
| `sys_io.cpp` | 312 | B | pad data via `pads::get` and `input_*`; no direct OS calls |
| `sceNp.cpp` | 438 | A | offline stub (network disabled by design) |
| `cellHttp*`, `cellNetCtl`, `cellSail`, `cellPamf`, `cellVdec`, `cellResc`, `cellSpurs`, `cellAtrac`, `cellSysmodule`, `cellSysutilAvcExt`, `sys_fs`, `sys_net`, `game_data` | ~1,900 | A | no OS dependency (all through `vfs`, `memory`, `hle`) |

#### runtime — LV2 kernel

| File | Lines | Class | Note |
| --- | --- | --- | --- |
| `thread.cpp` | 139 | C | `QueryPerformanceCounter`, `CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)`, `Sleep`, `SwitchToThread`, `GetTimeZoneInformation`, `GetSystemTimePreciseAsFileTime` → `clock_gettime`, `clock_nanosleep(TIMER_ABSTIME)`, `sched_yield`, `localtime_r` |
| `spu.cpp` | 594 | B | `CreateThread`/`WaitForSingleObject` for SPU run threads |
| `sync.cpp`, `memory.cpp`, `process.cpp`, `event_flag.cpp`, `lv2.cpp` | ~900 | A | — |

#### runtime — GPU

| File | Lines | Class | Note |
| --- | --- | --- | --- |
| `renderer.cpp` | 2,886 | C+ | RSX method execution, surface cache, texture cache, vertex decoding, draw batching, present, occlusion queries, captures. **Backend-agnostic logic ≈ 60 %; D3D11-specific ≈ 40 %** (398 call sites) |
| `d3d11_device.cpp` | 468 | D | device, swap chain (`DXGI_SWAP_EFFECT_FLIP_DISCARD`, `B8G8R8A8_UNORM`, tearing flag), exclusive fullscreen, blit shaders, captures, DXGI frame statistics |
| `submit.cpp` | 660 | C | record/replay of immediate-context calls on a submission thread. On Linux this becomes a thin wrapper over a Vulkan command buffer; the *design* (record on FIFO thread, execute on submit thread) survives |
| `display.cpp` | 292 | C | `IDXGIOutput::WaitForVBlank`, `EnumDisplaySettingsW`, `DXGI_FRAME_STATISTICS` → time-based vblank + `VK_KHR_present_wait`/`VK_GOOGLE_display_timing` where available, `VK_KHR_display`/SDL display modes |
| `window.cpp` | 623 | D | Win32 window, raw input, cursor confinement, monitor enumeration, DPI awareness → SDL2 window (see §9.4) |
| `rsx_texture.cpp` | 443 | B | `DXGI_FORMAT` enum in `TexData` → introduce a backend-neutral `TexFormat` and map to `VkFormat`/`DXGI_FORMAT`; the RSX decode and byte swapping are pure code |
| `shader_warm.cpp` | 152 | B | compiles HLSL without a device via `compile_hlsl`; on Linux the same loop runs DXC |
| `vp_hlsl.cpp`, `fp_hlsl.cpp` | 937 | A | emit HLSL text (also see §8.2 for the SM6 tweaks) |
| `png.cpp` | 45 | A | zlib |

#### runtime — other

| File | Lines | Class | Note |
| --- | --- | --- | --- |
| `rsx/rsx_core.cpp` | 500 | B | FIFO thread with `Sleep`, `SetThreadDescription`, `GetCurrentThreadId` |
| `spu/spu_thread.cpp` | 164 | B | `SwitchToThread` |
| `spu/spu_channels.cpp`, `spu_interp.cpp`, `spu_image.cpp` | ~1,120 | A | — |
| `game/input_hooks.cpp` | 730 | B | `QueryPerformanceCounter` (5×) → monotonic clock; the rest is guest-address logic |
| `game/menu_mouse.cpp` | 647 | B | `GetCursorPos`/`ScreenToClient`/`SetFocus` → window-relative cursor position from SDL |
| `game/frame_hooks.cpp`, `frame_policy.cpp`, `native_hooks.cpp`, `compat_patches.cpp`, `lua_hooks.cpp`, `system_menu.cpp` | ~520 | A/B | `frame_hooks` uses only `std::mutex`/`std::atomic` |
| `gamepads.h` | 24 | C | exposes `XINPUT_STATE`; keep the struct, drop `<windows.h>`/`<xinput.h>` |

#### launcher

All 16 files are Windows GUI/COM/WinHTTP/registry code (3,400 lines). **Class D.** None of it is
needed to build or run the game; the Windows release uses it to download a toolchain and drive
RPCS3. The Linux equivalent is a small CLI (`ac4-setup`) plus documentation, and optionally an
SDL2 UI later. Do not port `main.cpp`'s window procedure.

### 5.2 Windows API → Linux mapping (consolidated)

| Windows facility | Used in | Linux replacement | Cost |
| --- | --- | --- | --- |
| `CreateThread`, `WaitForSingleObject`, `SetThreadDescription` | ppu_thread, lv2/spu, cellAudio | `pthread_create` (detached or joined), `pthread_join`, `pthread_setname_np` | S |
| `CreateWaitableTimerExW`, `Sleep`, `SwitchToThread` | lv2/thread, rsx_core, spu_thread | `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)`, `sched_yield` | S |
| `WaitOnAddress`/`WakeByAddressAll` | ppu_runtime (guest atomics) | `syscall(SYS_futex, FUTEX_WAIT_PRIVATE/FUTEX_WAKE_PRIVATE)` | S |
| `SRWLOCK`/`CRITICAL_SECTION` | none directly (std:: primitives used) | n/a | — |
| `VirtualAlloc`/`VirtualFree` fixed-base 4 GiB | memory.cpp | `mmap(MAP_FIXED_NOREPLACE|MAP_NORESERVE)`, `mprotect`, `madvise` | M |
| `MEM_WRITE_WATCH`/`GetWriteWatch` | memory.cpp, renderer texture cache | `mprotect`+`SIGSEGV` dirty tracking, or pagemap soft-dirty | **L** |
| Vectored exception handler | ppu_runtime | `sigaction` + `ucontext_t` | M |
| `QueryPerformanceCounter/Frequency` | 6 files | `clock_gettime(CLOCK_MONOTONIC)`, `rdtsc` for phase ticks | S |
| `GetTickCount64`, `GetSystemTimes`, `GetProcessTimes`, `GetProcessMemoryInfo` | main.cpp, ppu_runtime | `clock_gettime`, `/proc/stat`, `/proc/self/stat`, `/proc/self/statm` | S |
| `SetPriorityClass`, `SetProcessAffinityMask`, `GetProcessAffinityMask` | main.cpp, ppu_runtime | `setpriority`, `sched_setaffinity`, `sched_getaffinity` | S |
| `GetLastError`, `FormatMessage` | ini, vfs, launcher | `errno`, `strerror_r` | S |
| `CreateFileW` family (17 functions) | vfs, save_data, ini | POSIX (`open`, `pread`, `pwrite`, `fstat`, `rename`, …) | M |
| `SHGetKnownFolderPath(FOLDERID_SavedGames)` | launcher only | XDG: `$XDG_DATA_HOME/ac4` (default `~/.local/share/ac4`) | S |
| `GetEnvironmentVariableW`, `USERPROFILE` | privacy | `getenv("HOME"/"USER")` | S |
| `WideCharToMultiByte`/`MultiByteToWideChar` | cellL10n, sysdialog, private | `std::wstring_convert`-style UTF helpers (own, ~60 lines) | S |
| `GetTimeZoneInformation`/`FILETIME` | lv2/thread, cellRtc, cellSysutil | `localtime_r`, `tm_gmtoff`, `clock_gettime(CLOCK_REALTIME)` | S |
| `CreateWindowExW`/`RegisterClassExW`/WndProc/raw input/`ClipCursor` | window.cpp, d3d11_device, launcher | SDL2 window + `SDL_SetRelativeMouseMode` | M |
| XInput + HID controller parsing | gamepads | SDL2 GameController (+ rumble) | M |
| WASAPI | audio_out | SDL2 audio (or PipeWire directly later) | S |
| Direct3D 11 / DXGI | renderer, submit, device, display | **Vulkan 1.3** | **XL** |
| Direct2D/DirectWrite | overlay | Vulkan 2D + stb_truetype + bundled OFL font | L |
| CNG (`bcrypt`) | crypto | in-tree SHA-1/256 | S |
| `bcrypt` `RtlCaptureContext`-adjacent code | none | n/a | — |
| Winsock | none (`sceNp` is an offline stub) | n/a | — |
| COM (`CoInitialize`, `CoCreateInstance`) | audio_out, launcher | n/a on Linux | — |

Size key: S ≤ 1 day, M ≈ 1–3 days, L ≈ 1–2 weeks, XL ≈ several weeks.

---

## 6. PPU recompiler audit

**Architecture** (unchanged by the port):

1. `Image` (`recomp/image.cpp`) parses the user's decrypted `eboot.elf` and the relocated
   firmware modules: LOAD segments, sections, OPD table, import stubs (`0x39800000`/`658c` pattern),
   exports, module info, TLS/process parameters.
2. `Analysis` (`recomp/analysis.cpp`) does reachability from OPD entries, follows `b`/`bc`,
   resolves indirect jumps through switch tables, and records per-function instruction sets,
   labels and external calls.
3. `Emitter` (`recomp/emit.cpp`, 813 lines) writes one C++ function per guest function, using the
   header-only semantics in `ppu/ppu_recomp.h` (SSE4.1/AVX2 intrinsics, byte-reversed VMX register
   file, per-instruction CR/XER updates).
4. `main.cpp` provides `analyze`, `disasm`, `func`, `fingerprint`, `emit` and accepts `--patches`
   (immediate rewrites) and `--hooks` (guest address → hook name).

**Portability:** no Windows dependency. Uses `-march=haswell`, `__builtin_bswap32`,
`__builtin_cpu_supports` (in `ac4-prep cpu`), `_setjmp` only inside generated *source text*
(`setjmp(...)`, valid on glibc). Determinism: analysis and emission are deterministic; the emitter
writes only changed files, so a Linux and a Windows build of the same input produce identical
`local/gen/ppu/*.cpp` (worth asserting in CI, see §14).

**Guest→host contract that must be preserved exactly:**

- `PPU_MEM_BASE = 0x0000_2000_0000_0000` — 4 GiB, reserved once; `mem()` is a constant expression.
- 32 GPRs, 32 FPRs, 32 VMX regs byte-reversed, CR as 32 bytes, XER bits, `res_line[128]`.
- `icall`, `syscall`, `trap`, `bad_switch`, `timebase`, `read_spr`, `write_spr` — implemented by the
  runtime; these are the only entry points the generated code needs from the OS layer.
- `lwarx/ldarx` + `stwcx/stdcx` reservation semantics across PPU and SPU threads (128-byte granule,
  striped locks, sequence-number futex wake).
- Guest `setjmp`/`longjmp` bridging uses a thread-local deque of live `jmp_buf`s and host `longjmp`.

**Linux-specific risks:** `jmp_buf` layout differs between glibc and MSVCRT/llvm-mingw — harmless,
because these buffers are host-only and never shared with the guest. The `__try/__except`-free design
means no SEH dependency in generated code.

---

## 7. SPU recompiler audit

- `spu/spu_isa.cpp` decodes SPU instructions and discovers chunks from entry points (`discover_chunk`,
  `chunk_hash`, `ls_word`), portable.
- `spu/spu_ops.h` (554 lines) implements SPU semantics with scalar helpers and `std::atomic`.
- `ac4-spurecomp` reads `*.spurec` records (positions only, produced by `ac4-prep spu-records` from
  the manifest and the user's files), deduplicates chunks by hash, and emits translation units with
  a validation table (`g_chunks`).
- The runtime (`spu/spu_channels.cpp`, `spu_interp.cpp`, `spu_thread.cpp`, `lv2/spu.cpp`) runs the
  compiled chunk if its local-store bytes match, else falls back to the interpreter.
- **No Windows dependency** except thread creation in `lv2/spu.cpp` and `SwitchToThread`/`Sleep` in
  `spu_thread.cpp`.

Consequence: the entire SPU pipeline (manifest → records → C++ → runtime) is portable and testable
on Linux without the game, using synthetic `.spurec` files.

---

## 8. Graphics audit (Direct3D 11 / HLSL) and the Vulkan strategy

### 8.1 What the current renderer is

```
guest RSX FIFO  ──►  rsx_core.cpp (FIFO thread)  ──►  renderer.cpp (renderer_method)
                                                        │  surfaces keyed by (location, offset)
                                                        │  texture cache keyed by descriptor+hash
                                                        │  vertex decoding into ByteAddressBuffers
                                                        │  pipelines cached by (VS,PS,blend,depth,raster,MRT,RT formats)
                                                        ▼
                                              submit.cpp Recorder  ──►  "rsx-submit" thread
                                                        │   (record/replay of ID3D11DeviceContext)
                                                        ▼
                                              d3d11_device.cpp (device, swap chain, present, captures)
```

- **Present path:** render the guest display buffer into a surface, blit to a `B8G8R8A8_UNORM`
  swap chain (`FLIP_DISCARD`, 2 buffers, tearing when allowed), then `overlay.cpp` draws the port's
  own UI on top with Direct2D.
- **Shaders:** `vp_to_hlsl` and `fp_to_hlsl` generate HLSL from RSX microcode; `compile_hlsl`
  (`d3d11_device.cpp`) compiles `vs_5_0`/`ps_5_0` with `D3DCompile`, caches DXBC by SHA-256 of source
  + entry + profile + flags. `shader_warm.cpp` precompiles the manifest without a device.
- **State objects:** blend/depth/raster/sampler states are cached by descriptor
  (`blend_state`, `depth_state`, `raster_state`, `sampler_state`).
- **Occlusion:** D3D11 queries (`zpass_begin/end/poll`) drive conditional draws and `ZPASS` reports.
- **Captures:** staging textures + readback (`read_back_last_frame`, `begin_capture`,
  `poll_captures`).
- **Texture write tracking:** the renderer asks `vm::take_written_pages` to know which guest texture
  granules changed, in addition to content hashing.

### 8.2 Why the HLSL is reusable (the key finding)

The generated shaders use only constructs that DXC accepts for SPIR-V:

| Construct used | DXC SPIR-V support |
| --- | --- |
| `float4 PSMain(VOut i) : SV_Target0` | yes (`-T ps_6_0 -spirv`) |
| `ByteAddressBuffer vb0..3 : register(t0..3)` + `Load`/`Load2` | yes (→ `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` SRV) |
| `cbuffer X : register(bN)` | yes (std140/std430 layout is DXC-managed) |
| `Texture2D`/`Texture3D`/`TextureCube` + `SampleLevel`/`SampleCmpLevelZero` | yes |
| `clip()` (pixel discard) | yes |
| `SV_VertexID`, `SV_Position`, `SV_IsFrontFace` | yes |
| `static const float4` tables, loops with constant bounds | yes |
| `Texture2D<uint2>` (stencil fetch), integer math | yes |

Work items for SM6/SPIR-V:

1. Change compile targets from `vs_5_0`/`ps_5_0` to `vs_6_0`/`ps_6_0` with `-spirv`. DXC requires
   explicit `register()` bindings in SM6 for resources — they already exist. `SV_Target0` → keep.
2. Replace `#define`-based entry selection with explicit `-E` entry points (already the case).
3. Move the shader cache from DXBC to SPIR-V, keyed by the same inputs **plus** the DXC version and
   the `-spirv` target, so Windows and Linux caches never collide.
4. Add a shader-test harness: compile every manifest entry on both platforms and diff the
   reflection (inputs, resources, cbuffer sizes). This is a real, runnable validation that does not
   need the game to be playable.

### 8.3 D3D11 → Vulkan mapping

| D3D11 concept | Vulkan equivalent | Notes |
| --- | --- | --- |
| `ID3D11Device` + immediate context | `VkDevice` + one primary `VkCommandBuffer` per frame in flight | single queue family (graphics) is enough |
| Swap chain (`FLIP_DISCARD`, 2 buffers) | `VkSwapchainKHR` (`MAILBOX` if available, else `FIFO`), `B8G8R8A8_UNORM` | present mode = `MAILBOX` gives the tearing-free equivalent of `ALLOW_TEARING`; `IMMEDIATE` for the "clock pacing + tearing" mode |
| `ID3D11Texture2D` (+SRV/RTV/DSV) | `VkImage` + view(s) | surfaces keyed identically; add `VK_IMAGE_USAGE_*` from the binding role |
| `ID3D11Buffer` (vertex/index/constant) | `VkBuffer` (+ `VkBufferView` for byte-address SRVs) | uniform buffers need `minUniformBufferOffsetAlignment` handling |
| `ID3D11ShaderResourceView` of a buffer | storage buffer (SSBO) or uniform texel buffer | `ByteAddressBuffer` → SSBO in DXC |
| `ID3D11SamplerState` | `VkSampler` | 1:1 (filter, wrap, border color, LOD bias, compare op) |
| `ID3D11BlendState` / `DepthStencilState` / `RasterizerState` | baked into `VkPipeline` (dynamic state for viewport/scissor/stencil ref) | pipeline cache key = current D3D state key; add `VK_EXT_extended_dynamic_state` use |
| `IASetPrimitiveTopology`, `IASetInputLayout` | `VkPipelineInputAssemblyState`, empty vertex input (attributes come from SSBO + `SV_VertexID`) | the port already decodes vertices in the shader |
| `Draw/DrawIndexed` | `vkCmdDraw/vkCmdDrawIndexed` | base-vertex needs `VK_KHR_draw_indirect_count`-free path or index buffer offset |
| `ClearRenderTargetView`/`ClearDepthStencilView` | `vkCmdClearAttachments` inside a render pass, or `vkCmdClearColorImage` outside | prefer `loadOp=CLEAR` when starting a pass |
| `CopyResource` | `vkCmdCopyImage`/`vkCmdCopyBuffer` | |
| `UpdateSubresource` | staging buffer + `vkCmdCopyBufferToImage` / `vkCmdUpdateBuffer` | recorded-then-executed model already batches this |
| `Map(WRITE_DISCARD)` | persistently mapped ring buffer per frame | removes the recorder's copy step entirely |
| Occlusion queries (`zpass_*`) | `VkQueryPool` (`OCCLUSION`) + `vkCmdBeginConditionalRenderingEXT` | needs `VK_EXT_conditional_rendering` (present on RADV/Steam Deck) |
| `IDXGIOutput::WaitForVBlank` | `VK_KHR_present_wait` / `VK_GOOGLE_display_timing`, else monotonic clock | keep the existing `display.cpp` pacing model |
| Exclusive fullscreen | `VK_EXT_full_screen_exclusive` or borderless only | **recommendation: borderless-only on Linux** (Wayland/Gamescope); document it |
| Frame statistics | `VK_EXT_present_timing`/present wait | optional diagnostics |
| Readback captures | copy image → host-visible buffer + fence | same contract |
| Direct2D/DirectWrite overlay | Vulkan 2D: textured quads + font atlas | see §9.5 |

Feature level: the port needs nothing exotic — Vulkan 1.1 core plus optional
`VK_EXT_conditional_rendering`. SteamOS RADV supports all of it. A 1.1 baseline keeps older Mesa
working.

### 8.4 Recommended incremental strategy

**Do not rewrite the renderer.** Split it, then replace one backend at a time, behind a
compile-time-selected interface, validating each step:

1. **Extract `render-core`** from `renderer.cpp`: remove all `ID3D11*` types from the RSX state
   tracker (method execution, surface registry, texture decode, vertex conversion, pipeline keys,
   draw batching). Keep the D3D11 code as `render-d3d11` behind small interfaces:
   `create_texture`, `create_buffer`, `map/unmap`, `create_pipeline`, `begin_pass/end_pass`,
   `draw`, `clear`, `copy`, `present`, `readback`.
   *Gate:* Windows binary byte-for-byte behaviour unchanged on the same trace (logs + captures).
2. **Implement `render-vulkan` for the minimum viable frame**: device + swapchain + a fullscreen
   textured quad (the existing blit path) + `vkCmdClear`. This proves window/present/resize/scale on
   Steam Deck with **no game data** (a synthetic test pattern).
   *Gate:* `ac4 --test-present` shows a moving gradient at 1280×800, 60 fps, on Steam Deck.
3. **Port simple draws**: vertex streams (SSBO), constant buffers, one texture format (RGBA8), no
   blend, no depth. Validate against a synthetic geometry scene.
   *Gate:* triangle/cube test with correct colors and orientation; compare against a D3D11 capture.
4. **Port the state set**: depth/stencil, blend, rasterizer, samplers, MRT. Use the in-game menus as
   the first real target (2D-ish, no depth complexity).
   *Gate:* the game's title screen and main menu render correctly from the user's own data.
5. **Port the texture formats**: BC1–BC3 (already optional), 16-bit, depth textures, cube maps,
   swizzled DXT. Validate with `rsx_texture`'s fast-path test (`ac4-textest` mentioned in the code;
   it is not present in this tree — recreate it as a test target).
6. **Port occlusion queries and conditional rendering**; then framebuffer feedback copies
   (`feedback_copy`, `depth_as_color`).
7. **Port the overlay** (§9.5), then the movie path (`cellVdec` writes into guest memory; the renderer
   presents it as a texture — already backend-neutral).
8. **Performance pass** on Steam Deck: pipeline cache, descriptor sets, render-pass merging,
   `MAILBOX`, and the frame policy (30/60).

Explicitly **out of scope for the first release**: exclusive fullscreen, HDR, MSAA (the RSX path does
not use it today), and any renderer feature the Windows port does not implement.

### 8.5 Graphics risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| HLSL→SPIR-V semantic drift (precision, `mad`, integer division) | visual differences | DXC emits SPIR-V from the same front end; add `-fspv-target-env=vulkan1.1`, compare captures |
| No backend abstraction today | large first step | step 1 above is mechanical and testable on Windows first |
| `submit.cpp` recorder is D3D-shaped | rework | replace the recorder with a real `VkCommandBuffer`; the "one worker thread" design is kept |
| Texture write-tracking on Linux | stale textures or huge hashing cost | implement `mprotect` dirty tracking before step 3, measure with the existing counters |
| Steam Deck Gamescope/Wayland windowing | fullscreen/scale bugs | SDL2 abstracts it; test both Gaming Mode (gamescope) and Desktop Mode (KWin) |
| Shader compile time on Deck (weak CPU) | stutter | keep `--warm-shaders`; ship a *user-generated* pipeline cache, never a prebuilt one containing game-derived shaders |

---

## 9. Non-graphics runtime subsystems

### 9.1 Files, saves, configuration

- Guest paths (`/dev_bdvd`, `/app_home`, `/dev_hdd0`, `/dev_hdd1`, `/dev_flash`) are already
  POSIX-shaped; `vfs.cpp` maps them to host directories. The Linux port keeps the exact mount
  table and swaps the syscall layer.
- **Host layout (proposal, XDG-compliant and Steam-friendly):**

  ```
  $XDG_DATA_HOME/AC4-Port/          (default ~/.local/share/AC4-Port)
    game/BLUS30027/                 prepared eboot.elf + lle/*.elf
    game/dev_flash/sys/external/flashATRAC.pic
    hdd0/  hdd1/
    spu_cache/  shader_cache/  runs/
    config/settings.ini  config/disc_root.txt
  $XDG_DATA_HOME/AC4-Port/savedata/  (or --user DIR/savedata)
  ```

  Saves remain the console's `SAVEDATA.BIN` + `PARAM.SFO` + `PARAM.PFD` layout, so saves are
  interchangeable with the Windows port and with RPCS3.
- `common/settings_file.cpp` is already portable and is the single schema for launcher + in-game
  settings. No format change is needed for Linux.

### 9.2 Threading and synchronisation

- All guest-visible scheduling semantics live in `ppu_thread.cpp` (priority hand-off on start,
  `note_block`, join/detach, TLS recycling) and are built on `std::mutex`/`std::condition_variable`.
  Only thread *creation* and *naming* are Windows-specific.
- `lv2/sync.cpp` implements semaphores, mutexes, condition variables, event queues, event flags,
  barriers with pure C++ — no change.
- `reservation.h` uses 1024 striped spinlocks and futex-like wakeups; only the wakeup primitive
  changes.
- **Priority:** LV2 priorities are emulated by the runtime, not by the OS. Do **not** try to map them
  to `pthread_setschedparam` (requires privileges and behaves differently on SteamOS). Keep the
  current user-space model.

### 9.3 Guest memory and write tracking (highest-risk core item)

Windows: `VirtualAlloc(MEM_RESERVE|MEM_WRITE_WATCH)` + `GetWriteWatch` gives page-granular write
tracking for free. Linux has no direct equivalent:

| Option | Availability | Verdict |
| --- | --- | --- |
| `userfaultfd` write-protect mode | usually `vm.unprivileged_userfaultfd=0`; needs root/CAP_SYS_PTRACE | reject for a consumer AppImage |
| `/proc/self/pagemap` soft-dirty bit | unprivileged for own process; requires clearing via `/proc/self/clear_refs` | viable, but the interface is "clear all, then read all" — O(4 GiB/4 KiB) scans unless ranges are narrow |
| `mprotect(PROT_READ)` + `SIGSEGV` handler | always available | **recommended**: handler sets the dirty bit, makes the page writable again, returns; `take_written_pages` answers from a bitmap and re-protects the range at a frame boundary |

Implementation notes: the fault handler must distinguish *tracking* faults (address inside the guest
reservation, page currently read-only *because of tracking*) from real guest faults, or it will turn
genuine crashes into silent corruption. Keep a separate per-page state byte
(`committed`, `tracked`, `writable`). Cost model: one fault per page per re-protect cycle — the same
order as `GetWriteWatch`'s per-page bookkeeping, and the existing renderer already falls back to
hashing when tracking is unavailable (`g_write_watch = false`), which is a safe degradation path if
tuning is needed.

### 9.4 Window, input, gamepads

- **SDL2** for window, Vulkan surface, keyboard, mouse, gamepads, and audio. It is packaged on
  SteamOS, is permissively licensed (zlib), and is the only dependency that touches all four.
- **Keyboard:** keep the runtime's `u32 vk` interface (Windows virtual-key numbers) and translate
  `SDL_Scancode`/`SDL_Keycode` → VK with a static table. This keeps `settings.ini` bindings portable
  and avoids touching `bindings.cpp` semantics.
- **Mouse:** `SDL_SetRelativeMouseMode(SDL_TRUE)` provides raw, unaccelerated deltas on Linux
  (evdev/libinput). The absolute-position branch in `input.cpp` (remote desktop/tablets) becomes
  `SDL_MOUSEMOTION` with `xrel/yrel` when relative mode is off. Mouse capture, `F1` release and the
  in-game cursor all map 1:1.
- **Gamepads:** `SDL_GameController` covers Xbox, DualShock 4, DualSense, Switch Pro **and the Steam
  Deck's built-in controls** (in Gaming Mode, Steam Input presents them as an XInput device; in
  Desktop Mode SDL reads them through hidraw). Rumble via `SDL_GameControllerRumble`. Keep the
  `XINPUT_STATE` internal representation so `sys_io.cpp`/`bindings.cpp`/menus do not change.
- **Steam Deck specifics:** 1280×800 → `window = 1280x800`, `mode = fullscreen` (borderless) by
  default when a 1280×800 display is detected; 30/60 fps already selectable; touchpad → mouse is
  handled by Steam Input in Gaming Mode.

### 9.5 The port's own UI (PC SETTINGS + system dialogs)

`ui/overlay.cpp` draws the settings screen and all system dialogs (save list, confirmations, OSK)
with Direct2D/DirectWrite, using **Times New Roman, Bahnschrift, Segoe UI** and a linear-gradient
brush. Replacement plan:

1. Add a minimal 2D layer on top of the Vulkan backend: solid rects, linear gradients, clipped
   textured quads, and text via a glyph atlas.
2. Text: `stb_truetype` (public domain / MIT) + a bundled font with a redistributable license.
   **Do not bundle Microsoft fonts.** Recommended: *DejaVu Serif* (Bitstream Vera license, serif to
   match the game's menu style) and *DejaVu Sans* for labels; both permit redistribution with the
   license notice. Add them to `THIRD_PARTY.md` and `licenses/`.
3. Keep `overlay.h`'s public API (`screen_up`, `is_open`, `on_key`, `render(...)`) and change only
   the `render` signature to take the backend's frame object. `sysdialog.cpp` needs no change.
4. `input.cpp`'s `GetKeyState` (for shift-state display) → tracked key state from SDL events.

### 9.6 Audio

WASAPI shared mode with OS-side SRC is replaced by an SDL2 audio device at 48 kHz float stereo.
The contract (`out_init`, `out_available`, `out_queued_frames`, `out_write` non-blocking, drop when
full) stays identical, so `hle/cellAudio.cpp` (its mixer thread, period scheduling, port timestamps)
needs no logic change. Latency target: 1024-frame buffer (≈21 ms) — the Windows port uses a 100 ms
device buffer and drops on overflow; keep the same tolerant policy for the first milestone, then
tune.

### 9.7 Media playback

`cellVdec` + `media/mpeg2_video.cpp` are portable; the movie path only needs the renderer to present
decoded frames, which the Vulkan blit path provides. Audio for movies comes through `cellAudio`, so
A/V sync depends on the same 60 Hz pacing rule already implemented (`display::movie_started`).

---

## 10. Launcher, packaging, distribution

- **Windows launcher** (3,400 lines, COM/WinHTTP/registry/common controls) is not ported. Linux gets
  `ac4-setup`, a CLI with the same steps:
  `ac4-setup verify-disc`, `ac4-setup prepare`, `ac4-setup build`, `ac4-setup play`.
  Everything it needs already exists: `ac4-prep import`, `ac4-prep spu-records`, `ac4-recomp emit`,
  `ac4-spurecomp`, CMake/Ninja.
- **Preparation workflow (lawful, user-side):** the user supplies their own disc, their own
  decrypted `EBOOT.elf`, system modules and `flashATRAC.pic` (decrypted with their own copy of
  RPCS3), plus `PS3UPDAT.PUP` from Sony for the firmware modules. The port decrypts nothing and
  ships nothing game-derived. This is exactly the current model; the Linux README must restate it.
- **AppImage:** built in a container (Ubuntu 22.04 base ⇒ glibc 2.35, SteamOS 3.x compatible),
  `-static-libstdc++ -static-libgcc`, bundle SDL2 + libdxcompiler (if dynamically linked) with
  `linuxdeploy`, **do not bundle** the Vulkan loader or Mesa (use the system's; Deck has RADV).
  AppRun sets `XDG_DATA_HOME` fallbacks and forwards argv. Desktop entry with `StartupWMClass`.
  Filename: `ArmoredCore4-SteamDeck-x86_64.AppImage`.
- **What the AppImage may contain:** the port's own binaries, SDL2, DXC, fonts, licenses. It must
  **not** contain `eboot.elf`, `lle/*.elf`, `local/gen/**`, `flashATRAC.pic`, shader caches derived
  from the game, or any key.

---

## 11. Steam Deck integration checklist (Phase 5)

| Item | Current state | Plan |
| --- | --- | --- |
| 1280×800 | settings support any size; `window_sizes()` lacks 1280×800 | add `{1280, 800}` and an auto-preset for Deck displays |
| Fullscreen | borderless/exclusive implemented with DXGI | borderless via SDL; exclusive dropped on Linux |
| 30 / 60 fps | `fps_choices()` already has 30/60/120/144/display/uncapped | unchanged; default 60, `frame_policy` keeps menus/movies at 60 |
| Analog sticks/triggers | XInput layout + analog L2/R2 travel | SDL GameController axes, same layout |
| Steam Deck controls | Steam Input in Gaming Mode | works via SDL; verify both modes |
| Audio | WASAPI | SDL2/PipeWire |
| Save persistence | `savedata/` | XDG path; AppImage keeps data outside the mount |
| Gaming Mode launch | n/a | AppImage added to Steam; test gamescope fullscreen and focus |

---

## 12. Legal / licensing constraints

- The port's code is **MIT**; it incorporates code from the same author's ACFA port under MIT.
  Keep `LICENSE` and `THIRD_PARTY.md` intact; add new third-party entries (SDL2 zlib, DXC
  Apache-2.0 WITH LLVM-exception, stb_truetype public domain/MIT, DejaVu fonts Bitstream Vera).
- Never commit or distribute: game data, `eboot.elf`, relocated firmware modules, `flashATRAC.pic`,
  SPU records, generated `local/gen/**`, shader caches containing game-derived shaders, keys,
  or `PS3UPDAT.PUP`.
- New code must remain MIT-compatible. Prefer permissive dependencies only.
- Do not relicense upstream code. Attribution to `SulphurS16/AC4-Port` stays in the README and
  release notes.

---

## 13. Phased execution plan with gates

Each phase ends with a written milestone report (changed files, commands, results, blockers) and a
commit series on branch `linux-native-port`. **A gate that is not met stops the phase.**

### Phase 2 — Linux build infrastructure

1. CMake platform split (`WIN32` vs `UNIX`), no `.rc` on Linux, `ac4-platform` library.
2. Portable replacements for `common`: in-tree SHA-1/256, POSIX INI save, POSIX path normalisation,
   `getenv`-based privacy redaction.
3. Build `ac4_zlib`, `ac4_common`, `ac4_ppu`, `ac4_spu`, `ac4-prep`, `ac4-recomp`,
   `ac4-spurecomp` on GCC **and** Clang.
4. `tools/build.sh`, `tools/env.sh`, `tools/bootstrap.sh`; keep the PowerShell scripts working.
5. Tests with **synthetic fixtures only** (see §14) and `ctest` wiring.
6. CI workflow (Linux, GCC + Clang) that builds tools and runs tests.

**Gate:** all five tools build and pass their tests on Linux; the Windows build still configures and
produces the same binaries' targets. No claim about the game.

### Phase 3 — Native runtime

Order (each step compiles and has a smoke test):
`platform` (paths, time, threads, errors) → `memory` (mmap + dirty tracking) → `vfs` (POSIX) →
`ppu_thread`/`lv2` threads → `ppu_runtime` (signals, futex, timebase) → `log`/`privacy` →
`input`/`window`/`gamepads` (SDL2) → `audio` (SDL2) → `sysdialog`/`overlay` (after the Vulkan 2D
layer) → `main`.

**Gate:** the runtime starts on Linux, reserves the guest address space, loads a **synthetic** PS3
ELF, runs recompiled test code (including `lwarx/stwcx`, setjmp/longjmp, thread create/join,
`sys_timer_usleep`), and exits cleanly. Then, with the user's own prepared data, reaches the loader
and the first RSX method with a headless renderer stub.

### Phase 4 — Graphics

Steps 1–8 of §8.4.

**Gate (incremental):** each step has a reproducible test — test pattern, synthetic geometry, then
in-game menus, then a mission. Capture comparisons are stored as artifacts. Anything incomplete is
listed in `docs/LINUX_STATUS.md`.

### Phase 5 — Steam Deck

**Gate:** 1280×800 borderless, 60 fps cap, controller-only navigation through the menus, audio,
saves persisting across restarts, and a 30 fps mode — verified on real hardware with logs attached.

### Phase 6 — AppImage

**Gate:** the AppImage launches in Desktop Mode and in Gaming Mode from Steam, on a clean Deck
(nothing installed besides SteamOS), with the user's data directory outside the AppImage.

### Phase 7 — Release

**Gate:** only artifacts that passed Phase 6 are published; release notes list known limitations,
attribution, licenses, and the user-side preparation steps.

---

## 14. Test and verification strategy (no fabricated results)

The project cannot ship or test game data. Everything below uses synthetic inputs and is therefore
fully lawful and CI-friendly.

1. **Recompiler round-trip (PPU).** Assemble a small PowerPC64 ELF with `llvm-mc`/`clang
   -target powerpc64` containing: arithmetic, CR/XER updates, a switch table, an indirect call, a
   `lwarx/stwcx` pair, a `sc`, and a VMX kernel. Run `ac4-recomp analyze|emit`, compile the output
   with host GCC/Clang, call the emitted functions from a harness, and assert results against the
   expected values. This exercises decoder, analysis, emitter and `ppu_recomp.h` semantics.
2. **Emulator cross-check.** Run the same synthetic binary in a reference PowerPC interpreter
   (a ~200-line test-only interpreter is enough) and compare register/CR results instruction by
   instruction.
3. **SPU round-trip.** Hand-build local-store images + `.spurec` records for a handful of instructions,
   run `ac4-spurecomp`, link the generated chunks, execute them on a mock `spu::Thread`, and compare
   against `spu_interp`.
4. **VFS/threading/memory tests.** Create/read/write/rename/truncate in a temp dir; mount resolution
   including `..` escapes; 4 GiB reservation at `PPU_MEM_BASE`; dirty-page tracking correctness;
   thread create/join/detach; futex wake latency.
5. **Shader tests.** Compile every manifest entry with DXC→SPIR-V (no game data needed if the
   manifest entries are skipped when sources are absent — the test asserts the *toolchain* works and
   the generated HLSL parses).
6. **Graphics smoke tests.** Test-pattern present, synthetic geometry, deterministic readback with
   tolerance.
7. **Determinism check.** `ac4-recomp emit` on the same synthetic image produces identical output on
   Windows and Linux (hash the files).
8. **Honesty rule:** a phase is "verified" only when a command and its output are recorded in
   `docs/BUILD_LOG.md` or the milestone report. Toolchain success ≠ playability.

---

## 15. Open decisions for the maintainer

1. **Hashing:** in-tree SHA-1/SHA-256 (no dependency, recommended) vs OpenSSL (smaller code, larger
   AppImage).
2. **Graphics backend:** Vulkan 1.1 baseline (recommended for Mesa compatibility) vs 1.3.
3. **Fullscreen on Linux:** borderless-only (recommended) vs `VK_EXT_full_screen_exclusive`.
4. **Windowing:** SDL2 (recommended, single dependency for window/input/pads/audio) vs GLFW +
   libevdev + PipeWire.
5. **Overlay font:** DejaVu (recommended) vs Noto (OFL) vs bundling nothing and requiring a system
   font.
6. **Launcher:** CLI first (recommended), optional SDL2+ImGui GUI later.
7. **Distribution model:** AppImage containing *binaries only* (recommended; user runs a prepare step
   once) vs AppImage that builds the game on first run (needs a compiler in the image; rejected).

---

## Appendix A — Windows API symbols by file (audit extract)

Counts are occurrences; generated from the tree at `219c7bf`. Only files with at least one hit.

```
src/common/crypto.cpp          HANDLE(3) wchar_t(1)
src/common/file.cpp            std::mutex(1) wchar_t(1)
src/common/ini.cpp             GetLastError(1) DWORD(1) MoveFileExW(1)
src/common/privacy.cpp         GetEnvironmentVariableW(1) WideCharToMultiByte(2) DWORD(2) wchar_t(2)
src/launcher/download.cpp      GetLastError(2) DWORD(6) std::atomic(1) _wfopen(1) wchar_t(2)
src/launcher/install.cpp       SHGetKnownFolderPath(1) GetEnvironmentVariableW(1) CoCreateInstance(2)
                               RegOpenKeyExW(2) DWORD(13) LPWSTR(1) wchar_t(20)
src/launcher/main.cpp          CreateFileW(1) CreateMutexW(1) CreateProcessW(1) ShellExecuteW(2)
                               CreateWindowExW(2) RegisterClassExW(1) GetMessageW(1) DispatchMessageW(1)
                               CoInitialize(2) CoCreateInstance(1) EnumDisplaySettingsW(2) SetWindowPos(4)
                               AdjustWindowRectEx(2) LoadCursorW(3) LoadIconW(1) MessageBoxW(11)
                               EnumWindows(1) SetForegroundWindow(3) wWinMain(1) wchar_t(20)
src/launcher/process.cpp       CreateFileW(1) ReadFile(1) CreateProcessW(1) MultiByteToWideChar(2)
                               WideCharToMultiByte(2) HANDLE(5) DWORD(2)
src/launcher/rpcs3.cpp         WaitForSingleObject(2) CreateProcessW(1) TerminateProcess(1) EnumWindows(1)
src/launcher/setup.cpp         _wfopen(1) GetDiskFreeSpaceExW(1)
src/launcher/source.cpp        CreateProcessW(1) TerminateProcess(1) wchar_t(2)
src/runtime/audio/audio_out.cpp CoInitialize(1) CoCreateInstance(1)
src/runtime/game/input_hooks.cpp QueryPerformanceCounter(5) QueryPerformanceFrequency(5)
src/runtime/game/menu_mouse.cpp GetCursorPos(1) ScreenToClient(1) SetFocus(4)
src/runtime/gamepads.cpp       CreateFileW(3) ReadFile(1) WaitForMultipleObjects(1) CreateEventW(2)
                               LoadLibraryW(2) GetProcAddress(2) XInputGetState(1) HANDLE(10) DWORD(11)
                               GetOverlappedResult(2) CancelIoEx(1) WINAPI(2)
src/runtime/gpu/d3d11_device.cpp EnumDisplaySettingsW(1) GetForegroundWindow(1) WideCharToMultiByte(1)
src/runtime/gpu/display.cpp    EnumDisplaySettingsW(1) QueryPerformanceFrequency(1)
src/runtime/gpu/renderer.cpp   QueryPerformanceCounter(2) QueryPerformanceFrequency(1)
src/runtime/gpu/submit.cpp     QueryPerformanceCounter(2) QueryPerformanceFrequency(1) HANDLE(2)
                               SetThreadDescription(1) GetCurrentThreadId(2)
src/runtime/gpu/window.cpp     CreateWindowExW(1) RegisterClassExW(1) ShowWindow(3) PeekMessageW(1)
                               DispatchMessageW(1) GetRawInputData(1) RegisterRawInputDevices(2)
                               GetModuleHandleW(2) MonitorFromWindow(6) GetMonitorInfoW(6) SetWindowPos(5)
                               AdjustWindowRectEx(1) LoadCursorW(4) LoadIconW(1) SetCursor(4)
                               ShowCursor(2) ClipCursor(2) GetCursorPos(2) ScreenToClient(1)
                               ClientToScreen(1) CreateDIBSection(1)
src/runtime/hle/cellAudio.cpp  SetThreadDescription(1)
src/runtime/hle/cellL10n.cpp   MultiByteToWideChar(1) WideCharToMultiByte(1) wchar_t(3)
src/runtime/hle/cellRtc.cpp    GetSystemTimePreciseAsFileTime(1) GetTimeZoneInformation(1) DWORD(1)
src/runtime/hle/cellSysutil.cpp GetTimeZoneInformation(1)
src/runtime/hle/save_data.cpp  GetFileAttributesExW(1)
src/runtime/hle/sysPrxForUser.cpp SwitchToThread(1)
src/runtime/input.cpp          GetSystemMetrics(2)
src/runtime/log.cpp            GetCurrentThreadId(1) TerminateProcess(1)
src/runtime/lv2/spu.cpp        CreateThread(1) WaitForSingleObject(1) SetThreadDescription(1) WINAPI(1)
src/runtime/lv2/thread.cpp     WaitForSingleObject(1) QueryPerformanceCounter(2) QueryPerformanceFrequency(1)
                               SwitchToThread(2) SetWaitableTimer(1) Sleep(1) GetSystemTimePreciseAsFileTime(1)
src/runtime/main.cpp           SetPriorityClass(1) GetSystemTimes(1) GetProcessTimes(1)
                               GetProcessMemoryInfo(1) ExitProcess(1) gmtime_s(1) SetProcessAffinityMask(1)
src/runtime/memory.cpp         VirtualAlloc(3) VirtualFree(2) GetWriteWatch(1) GetLastError(2)
src/runtime/ppu_runtime.cpp    GetTickCount64(1) GetProcessAffinityMask(1) GetSystemInfo(1)
                               AddVectoredExceptionHandler(1) WaitOnAddress(1) WakeByAddressAll(1)
                               SwitchToThread(1) QueryPerformanceCounter(2) QueryPerformanceFrequency(1)
                               VirtualAlloc(1) CALLBACK(1)
src/runtime/ppu_thread.cpp     CreateThread(1) SetThreadDescription(1) ExitProcess(1) WINAPI(1) DWORD(2)
src/runtime/rsx/rsx_core.cpp   Sleep(3) SetThreadDescription(2) GetCurrentThreadId(1)
src/runtime/spu/spu_thread.cpp SwitchToThread(1) Sleep(1)
src/runtime/sysdialog.cpp      MultiByteToWideChar(2)
src/runtime/ui/overlay.cpp     Direct2D 1.1, DirectWrite, GetKeyState, 12 sized DWrite text formats
src/runtime/vfs.cpp            CreateFileW(1) ReadFile(3) WriteFile(3) SetFilePointerEx(6)
                               GetFileInformationByHandle(1) GetFileAttributesExW(1) DeleteFileW(1)
                               RemoveDirectoryW(1) MoveFileExW(1) SetFileInformationByHandle(1)
                               GetLastError(9) _stricmp(1) DWORD(18) HANDLE(6)
```

## Appendix B — Verified blockers to date

| # | Blocker | Evidence | Status |
| --- | --- | --- | --- |
| 1 | `project(LANGUAGES C CXX RC)` | CMake configure fails: `No CMAKE_RC_COMPILER could be found` | open, Phase 2 |
| 2 | `.rc` sources in every tool target | same failure | open, Phase 2 |
| 3 | `bcrypt` link in `ac4_common` | not yet reached (masked by #1) | open, Phase 2 |
| 4 | D3D11/DXGI/D2D/DWrite link lines | not yet reached | open, Phase 4 |
| 5 | 40 `<windows.h>` translation units | not yet reached | open, Phase 3 |

`docs/BUILD_LOG.md` records each error and its fix as the port proceeds.
