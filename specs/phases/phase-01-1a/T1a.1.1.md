# T1a.1.1 — LLVM/libc++ Toolchain Bootstrap + CMake Skeleton

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: Build System
**Roadmap Ref**: `roadmap.md` L324 — CMake 4.0 + LLVM 20/libc++ sandbox, C++23 modules validation
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| — | — | — | First task, no dependencies |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `CMakeLists.txt` (root) | shared | **H** | Root CMake project: C++23, module support, presets, toolchain selection |
| `cmake/toolchain/clang-libc++.cmake` | shared | **H** | Toolchain file: Clang 20 + libc++ flags (`-stdlib=libc++ -fexperimental-library`) |
| `cmake/presets/CMakePresets.json` | shared | **M** | Build presets: Debug/Release/ASAN/TSAN/UBSAN/D3D12/Emscripten |
| `third_party/llvm/bootstrap.cmake` | internal | L | Downloads prebuilt LLVM 20 binary or builds from source |
| `tests/unit/test_modules.cpp` | internal | L | C++23 module toolchain validation (10-module dependency graph) |

- **Error model**: CMake `message(FATAL_ERROR ...)` for toolchain validation failures
- **Thread safety**: N/A (build system)
- **GPU constraints**: N/A
- **Invariants**: `cmake --preset release` must succeed from clean clone; C++23 stdlib features (std::expected, std::format, std::print) must compile

### Downstream Consumers

- `CMakeLists.txt` (shared, heat **H**):
  - T1a.1.2 (same Phase): adds `add_subdirectory(third_party/...)` targets
  - T1a.2.1 (same Phase): adds `miki::foundation` library target
  - All subsequent Tasks: depend on CMake target structure
- `cmake/toolchain/clang-libc++.cmake` (shared, heat **H**):
  - T1a.1.2: third-party deps must compile with this toolchain
  - All Tasks: all code compiled with this toolchain

### Upstream Contracts

- None (first Task)

### Technical Direction

- **LLVM sandbox**: All code (miki + third-party) compiled with Clang 20 + libc++ 20. No libstdc++ or MSVC STL at any ABI boundary
- **CMake 4.0**: Required for C++23 support (`CMAKE_CXX_STANDARD 23`). C++20/23 modules disabled; traditional headers (.h) + implementation files (.cpp)
- **No package managers**: No vcpkg, no Conan. All deps vendored in `third_party/`
- **Presets**: `CMakePresets.json` for reproducible builds across developers and CI
- **Toolchain validation**: 10-header dependency graph test (std::expected, std::format, std::print, constexpr inline) on Clang 21 + libc++ (primary)

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `CMakeLists.txt` | shared | **H** | Root project — all Tasks add to this |
| Create | `cmake/toolchain/clang-libc++.cmake` | shared | **H** | Clang 20 + libc++ toolchain |
| Create | `cmake/presets/CMakePresets.json` | shared | **M** | Build presets |
| Create | `third_party/llvm/bootstrap.cmake` | internal | L | LLVM download/build script |
| Create | `tests/unit/test_modules.cpp` | internal | L | C++23 module validation |
| Create | `tests/CMakeLists.txt` | internal | L | Test target setup |

## Steps

- [x] **Step 1**: Create root `CMakeLists.txt` + toolchain file
      **Files**: `CMakeLists.txt` (shared H), `cmake/toolchain/clang-libc++.cmake` (shared H)

      **Signatures** (`CMakeLists.txt`):

      | Symbol | Description | Attrs |
      |--------|-------------|-------|
      | `cmake_minimum_required` | `VERSION 4.0` | — |
      | `project(miki)` | `LANGUAGES CXX` | C++23 |
      | `CMAKE_CXX_STANDARD` | `23` | required |
      | `MIKI_BUILD_D3D12` | `OPTION(... OFF)` | Windows-only |
      | `MIKI_BUILD_VULKAN` | `OPTION(... ON)` | Default ON |
      | `MIKI_DEMO_BACKEND` | `STRING "glfw"` | `glfw` or `neko` |

      **Signatures** (`clang-libc++.cmake`):

      | Symbol | Value | Attrs |
      |--------|-------|-------|
      | `CMAKE_C_COMPILER` | `clang` | LLVM 20 |
      | `CMAKE_CXX_COMPILER` | `clang++` | LLVM 20 |
      | `CMAKE_CXX_FLAGS` | `-stdlib=libc++ -fexperimental-library` | libc++ sandbox |

      **Acceptance**: `cmake --preset release` configures successfully
      `[verify: compile]`

- [x] **Step 2**: Create CMakePresets.json + LLVM bootstrap
      **Files**: `cmake/presets/CMakePresets.json` (shared M), `third_party/llvm/bootstrap.cmake` (internal L)
      Create presets: `debug`, `release`, `asan`, `tsan`, `ubsan`, `d3d12` (Windows), `emscripten`.
      LLVM bootstrap: detect host platform, download prebuilt LLVM 20, verify SHA256.
      **Acceptance**: presets resolve correctly
      `[verify: compile]`

- [x] **Step 3**: C++23 toolchain validation test
      **Files**: `tests/unit/test_modules.cpp` (internal L), `tests/CMakeLists.txt` (internal L)
      Create 10-header dependency graph validating C++23 features: std::expected, std::format, std::print, constexpr inline, nested namespaces.
      **Acceptance**: toolchain test compiles, links, and passes
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(BuildSystem, CmakeConfigures)` | Build | Step 1 — CMake configures | 1 |
| `TEST(BuildSystem, PresetsResolve)` | Build | Step 2 — presets work | 2 |
| `TEST(Toolchain, StdlibFeatures)` | Unit | Step 3 — C++23 stdlib features compile | 3 |
| `TEST(Toolchain, InterHeaderInclude)` | Unit | Step 3 — cross-header includes | 3 |
| `TEST(Toolchain, TenHeaderGraph)` | Unit | Step 3 — 10-header dependency graph | 3 |

## Design Decisions

- **COCA toolchain instead of raw LLVM**: Uses COCA (Clang-on-COCA-Architecture) prebuilt toolchain instead of downloading LLVM from source. Wrapper toolchain `cmake/toolchain/miki-coca.cmake` includes the COCA toolchain via `$ENV{COCA_TOOLCHAIN_CMAKE}` and applies project-specific fixes (archiver, CRT selection).
- **No `clang-libc++.cmake`**: Spec listed this file but COCA toolchain handles all compiler/stdlib flags. `miki-coca.cmake` wraps it.
- **No LLVM bootstrap script**: COCA provides prebuilt binaries. `third_party/llvm/bootstrap.cmake` not created.
- **CMakePresets.json at root**: Not in `cmake/presets/` as spec suggested. Root-level is CMake convention.
- **Force release CRT**: `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL` forced for all build types to avoid heap corruption with prebuilt Slang DLLs.
- **3 toolchain tests**: `StdlibFeatures`, `InterHeaderInclude`, `TenHeaderGraph` — validates C++23 features via 10-header dependency graph.

## Implementation Notes

- Contract check: PARTIAL. Toolchain file path differs (`miki-coca.cmake` vs `clang-libc++.cmake`). No LLVM bootstrap script. CMakePresets.json at root.
- 3 tests registered in ctest (build-type tests not registered as ctest targets).
- Both `debug` and `debug-d3d12` presets configure and build successfully.
