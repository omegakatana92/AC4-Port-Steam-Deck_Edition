cmake_minimum_required(VERSION 3.25)

# The resource compiler (RC) exists only on Windows: the .rc files carry the executables' icons,
# version information and the UTF-8 process code page. Linux builds have no .rc sources, so the
# language is declared only there.
if(WIN32)
  project(ac4 LANGUAGES C CXX RC)
else()
  project(ac4 LANGUAGES C CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Windows API level and wide-character Win32 entry points; harmless nowhere else, so gated.
if(WIN32)
  add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX UNICODE _UNICODE)
else()
  # 64-bit file offsets and no implicit declarations in C sources (zlib builds as C11).
  add_compile_definitions(_FILE_OFFSET_BITS=64)
endif()

# The port's version (VERSION; the release's changelog names it), shown by the launcher and logged
# by the game.
file(STRINGS ${CMAKE_SOURCE_DIR}/VERSION AC4_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/VERSION)
# In a generated header, written only when the version changes and included by the game's and the
# launcher's main.cpp alone: a new version rebuilds those two files, not every file (a compile
# definition on every file made each update rebuild the whole game).
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/version/ac4_version.h
     CONTENT "// Generated from VERSION by CMakeLists.txt.\n#pragma once\n#define AC4_VERSION \"@AC4_VERSION@\"\n" @ONLY)
include_directories(${CMAKE_BINARY_DIR}/version)

# Developer diagnostics (control-file commands, profiler, memory search and watchpoints, traces,
# frame reports, A/B switches, test autopilot). Off in player builds: compiled out, and the
# release export (tools\release) removes their code.
option(AC4_DEV "Build with developer diagnostics" OFF)
if(AC4_DEV)
  add_compile_definitions(AC4_DEV=1)
else()
  add_compile_definitions(AC4_DEV=0)
endif()

# SHA-1/SHA-256 through Windows CNG (bcrypt) or the portable implementation in common/sha.cpp.
# Both produce identical digests (tests/ checks the portable one against the NIST vectors); the
# Windows build keeps CNG by default so this port changes nothing there.
if(WIN32)
  option(AC4_USE_BCRYPT "Hash with Windows CNG (bcrypt) instead of the portable SHA implementation" ON)
else()
  set(AC4_USE_BCRYPT OFF)
endif()
if(AC4_USE_BCRYPT)
  add_compile_definitions(AC4_USE_BCRYPT=1)
else()
  add_compile_definitions(AC4_USE_BCRYPT=0)
endif()

# Self-contained executables (no libc++/libunwind DLLs next to them), reproducible: no build
# paths or link timestamps in the binaries.
if(MINGW)
  add_link_options(-static -Wl,--no-insert-timestamp)
endif()
add_compile_options(-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)
# UTF-8 process code page for every executable (paths with any characters).
set(AC4_UTF8_RC "")
set(AC4_RT_RC "")
if(WIN32)
  set(AC4_UTF8_RC ${CMAKE_SOURCE_DIR}/src/common/utf8.rc)
  set(AC4_RT_RC ${CMAKE_SOURCE_DIR}/src/runtime/game.rc)
endif()

# ---- third party ----
file(GLOB ZLIB_SRC ${CMAKE_SOURCE_DIR}/third_party/zlib/*.c)
add_library(ac4_zlib STATIC ${ZLIB_SRC})
target_include_directories(ac4_zlib PUBLIC ${CMAKE_SOURCE_DIR}/third_party/zlib)
target_compile_options(ac4_zlib PRIVATE -w)

# ---- common ----
add_library(ac4_common STATIC
  src/common/bytes.cpp
  src/common/crypto.cpp
  src/common/known_inputs.cpp
  src/common/file.cpp
  src/common/ini.cpp
  src/common/privacy.cpp
  src/common/settings_file.cpp
  src/common/sha.cpp
  src/common/sfo.cpp)
target_include_directories(ac4_common PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ac4_common PUBLIC ac4_zlib)
if(WIN32 AND AC4_USE_BCRYPT)
  target_link_libraries(ac4_common PUBLIC bcrypt)
endif()
if(NOT WIN32)
  # std::mutex / std::thread in the tools: pthreads on glibc older than 2.34.
  find_package(Threads REQUIRED)
  target_link_libraries(ac4_common PUBLIC Threads::Threads)
endif()
target_compile_options(ac4_common PRIVATE -Wall -Wextra)

# ---- PPU decoding (shared by recompiler and runtime) ----
add_library(ac4_ppu STATIC src/ppu/ppu_decode.cpp)
target_link_libraries(ac4_ppu PUBLIC ac4_common)
target_compile_options(ac4_ppu PRIVATE -Wall -Wextra)

# ---- SPU ISA, chunk discovery (shared by the SPU recompiler and the runtime) ----
add_library(ac4_spu STATIC src/spu/spu_isa.cpp)
target_link_libraries(ac4_spu PUBLIC ac4_common)
target_compile_options(ac4_spu PRIVATE -Wall -Wextra)

add_executable(ac4-spurecomp src/spurecomp/main.cpp ${AC4_UTF8_RC})
target_link_libraries(ac4-spurecomp PRIVATE ac4_spu)
target_compile_options(ac4-spurecomp PRIVATE -Wall -Wextra)

# ---- recompiler ----
add_library(ac4_recomp STATIC
  src/recomp/image.cpp
  src/recomp/analysis.cpp
  src/recomp/emit.cpp)
target_link_libraries(ac4_recomp PUBLIC ac4_ppu)
target_compile_options(ac4_recomp PRIVATE -Wall -Wextra)

add_executable(ac4-recomp src/recomp/main.cpp ${AC4_UTF8_RC})
target_link_libraries(ac4-recomp PRIVATE ac4_recomp)
target_compile_options(ac4-recomp PRIVATE -Wall -Wextra)

# ---- generated PPU code (game-derived, from local/gen/ppu; never distributed) ----
set(AC4_GEN_DIR "${CMAKE_SOURCE_DIR}/local/gen/ppu" CACHE PATH "Directory with ac4-recomp output")
file(GLOB AC4_GEN_SRC CONFIGURE_DEPENDS "${AC4_GEN_DIR}/*.cpp")
set(AC4_CPU_FLAGS -march=haswell)  # AVX2 + FMA + BMI2 (x86-64-v3)

# The runtime needs the platform backends (Direct3D 11 on Windows, Vulkan on Linux). The Linux
# runtime is Phase 3 of docs/LINUX_PORT_PLAN.md; until it exists this option is off there, so the
# preparation and recompilation tools build and are tested on their own.
if(WIN32)
  option(AC4_BUILD_RUNTIME "Build the game runtime" ON)
else()
  option(AC4_BUILD_RUNTIME "Build the game runtime (Linux backend: see docs/LINUX_PORT_PLAN.md)" OFF)
endif()

if(AC4_GEN_SRC AND AC4_BUILD_RUNTIME)
  add_library(ac4_ppu_gen STATIC ${AC4_GEN_SRC})
  target_include_directories(ac4_ppu_gen PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_compile_options(ac4_ppu_gen PRIVATE -O2 ${AC4_CPU_FLAGS} -fno-strict-aliasing -w)

  file(GLOB AC4_RT_SRC CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/runtime/*.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/hle/*.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/lv2/*.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/rsx/*.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/gpu/*.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/audio/*.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/media/*.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/game/*.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/spu/*.cpp
    ${CMAKE_SOURCE_DIR}/src/runtime/ui/*.cpp)
  # Recompiled SPU code (game-derived, from local/gen/spu; never distributed).
  set(AC4_SPU_GEN_DIR "${CMAKE_SOURCE_DIR}/local/gen/spu" CACHE PATH "Directory with ac4-spurecomp output")
  file(GLOB AC4_SPU_GEN_SRC CONFIGURE_DEPENDS "${AC4_SPU_GEN_DIR}/*.cpp")
  if(NOT AC4_SPU_GEN_SRC)
    set(AC4_SPU_GEN_SRC ${CMAKE_SOURCE_DIR}/src/spu/spu_gen_empty.cpp)
  endif()
  add_library(ac4_spu_gen STATIC ${AC4_SPU_GEN_SRC})
  target_include_directories(ac4_spu_gen PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_compile_options(ac4_spu_gen PRIVATE -O2 ${AC4_CPU_FLAGS} -fno-strict-aliasing -w)

  add_executable(ac4 ${AC4_RT_SRC} ${AC4_UTF8_RC} ${AC4_RT_RC})
  if(WIN32 AND NOT AC4_DEV)  # players' builds open no console window (the run log holds every message)
    set_target_properties(ac4 PROPERTIES WIN32_EXECUTABLE ON)
  endif()
  target_include_directories(ac4 PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_compile_options(ac4 PRIVATE ${AC4_CPU_FLAGS} -fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter -g)
  # Whole-archive: the function table references everything anyway; keeps HLE registrars alive.
  target_link_libraries(ac4 PRIVATE ac4_ppu_gen ac4_spu_gen ac4_spu ac4_ppu ac4_common)
  if(WIN32)
    target_link_libraries(ac4 PRIVATE d3d11 dxgi d3dcompiler d2d1 dwrite ole32 psapi synchronization hid setupapi)
  else()
    find_package(SDL2 QUIET)
    find_package(Vulkan QUIET)
    if(TARGET SDL2::SDL2)
      target_link_libraries(ac4 PRIVATE SDL2::SDL2)
    endif()
    if(Vulkan_FOUND)
      target_link_libraries(ac4 PRIVATE Vulkan::Vulkan)
    endif()
  endif()
elseif(AC4_GEN_SRC)
  message(STATUS "ac4: generated code found but AC4_BUILD_RUNTIME=OFF; building tools only")
endif()

# ---- preparation tool ----
add_library(ac4_prep STATIC
  src/prep/import.cpp
  src/prep/prx.cpp
  src/prep/sfo.cpp
  src/prep/spu_records.cpp)
target_link_libraries(ac4_prep PUBLIC ac4_common)
target_compile_options(ac4_prep PRIVATE -Wall -Wextra)

add_executable(ac4-prep src/prep/main.cpp ${AC4_UTF8_RC})
target_link_libraries(ac4-prep PRIVATE ac4_prep)
target_compile_options(ac4-prep PRIVATE -Wall -Wextra)

# ---- launcher (players' setup and start; no game data or game-derived code) ----
# Windows GUI program. Linux uses ac4-setup (docs/LINUX_PORT_PLAN.md §10).
if(WIN32)
  add_executable(ac4-launcher WIN32
    src/launcher/main.cpp
    src/launcher/setup.cpp
    src/launcher/inputs.cpp
    src/launcher/process.cpp
    src/launcher/rpcs3.cpp
    src/launcher/download.cpp
    src/launcher/source.cpp
    src/launcher/install.cpp
    src/launcher/launcher.rc)
  # The launcher's file version (its properties): the numbers of VERSION, for its resource file only.
  string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" ac4_version_numbers "${AC4_VERSION}")
  set_source_files_properties(src/launcher/launcher.rc PROPERTIES COMPILE_DEFINITIONS
    "AC4_VERSION_MAJOR=${CMAKE_MATCH_1};AC4_VERSION_MINOR=${CMAKE_MATCH_2};AC4_VERSION_PATCH=${CMAKE_MATCH_3}")
  # A release launcher carries the port's sources (a zip of the exported tree, made by
  # tools/release/export.py) and unpacks them into its install folder (src/launcher/source.h).
  set(AC4_SOURCE_ZIP "" CACHE FILEPATH "Zip of the port's sources to embed in the launcher (releases)")
  if(AC4_SOURCE_ZIP)
    file(TO_CMAKE_PATH "${AC4_SOURCE_ZIP}" ac4_source_zip)
    file(WRITE ${CMAKE_BINARY_DIR}/launcher_source.rc "2 RCDATA \"${ac4_source_zip}\"\n")
    target_sources(ac4-launcher PRIVATE ${CMAKE_BINARY_DIR}/launcher_source.rc)
    set_source_files_properties(${CMAKE_BINARY_DIR}/launcher_source.rc PROPERTIES OBJECT_DEPENDS "${ac4_source_zip}")
  endif()
  target_include_directories(ac4-launcher PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(ac4-launcher PRIVATE ac4_common comctl32 winhttp shell32 ole32 uuid advapi32)
  target_link_options(ac4-launcher PRIVATE -municode)
  target_compile_options(ac4-launcher PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
  set_target_properties(ac4-launcher PROPERTIES OUTPUT_NAME AC4-Launcher)
endif()

# ---- media decoder test tool (development: checks the MPEG-2 decoder on the user's movies) ----
# (target not present in this tree)

# ---- tests (synthetic fixtures only: no game data, no game-derived code) ----
option(AC4_BUILD_TESTS "Build the port's tests" OFF)
if(AC4_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
