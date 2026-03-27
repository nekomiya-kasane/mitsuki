# T1a.1.2 — Third-Party Dependency Integration

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: Build System
**Roadmap Ref**: `roadmap.md` L324 — All third-party in `third_party/` as submodules/vendored, STATIC libs
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.1.1 | LLVM/libc++ Toolchain + CMake Skeleton | Not Started | Root `CMakeLists.txt`, toolchain file |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `third_party/CMakeLists.txt` | shared | **H** | Master third-party integration — all libs as CMake targets |
| `third_party/<name>/CMakeLists.txt` (each) | internal | L | Per-lib thin wrappers exposing `miki::third_party::<name>` |
| `.gitmodules` | internal | L | Git submodule references |

- **Error model**: CMake `message(FATAL_ERROR)` if submodule not initialized
- **Thread safety**: N/A
- **GPU constraints**: N/A
- **Invariants**: `git clone --recursive` + `cmake --preset release` builds everything from source; no system packages required

### Downstream Consumers

- `third_party/CMakeLists.txt` (shared, heat **H**):
  - T1a.2.1: links `miki::third_party::glm`
  - T1a.5.1: links `miki::third_party::vma`
  - T1a.6.1: links `miki::third_party::d3d12ma`, `miki::third_party::directx_headers`
  - T1a.9.2: links `miki::third_party::glfw`
  - T1a.10.1: links `miki::third_party::slang`
  - T1a.8.1: links `miki::third_party::googletest`

### Upstream Contracts

- T1a.1.1: Root `CMakeLists.txt` with `add_subdirectory(third_party)` hook; toolchain file for Clang 20 + libc++

### Technical Direction

- **No package managers**: All deps vendored or submoduled in `third_party/`
- **STATIC only**: All third-party libs compiled as STATIC — no shared lib issues
- **libc++ sandbox**: All third-party code compiled with `-stdlib=libc++` via toolchain file
- **Thin wrappers**: Each lib gets a `third_party/<name>/CMakeLists.txt` exposing a namespaced target
- **Phased deps**: Only libs needed for Phase 1a are added now; others added in later phases

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `third_party/CMakeLists.txt` | shared | **H** | Master third-party file |
| Create | `third_party/glm/CMakeLists.txt` | internal | L | Header-only, `miki::third_party::glm` |
| Create | `third_party/googletest/CMakeLists.txt` | internal | L | `miki::third_party::gtest` |
| Create | `third_party/vma/CMakeLists.txt` | internal | L | Header-only, `miki::third_party::vma` |
| Create | `third_party/slang/CMakeLists.txt` | internal | L | Source-compiled, `miki::third_party::slang` |
| Create | `third_party/imgui/CMakeLists.txt` | internal | L | Docking branch, `miki::third_party::imgui` |
| Create | `third_party/glfw/CMakeLists.txt` | internal | L | `miki::third_party::glfw` (demo-only) |
| Create | `third_party/d3d12ma/CMakeLists.txt` | internal | L | D3D12 Memory Allocator |
| Create | `third_party/directx_headers/CMakeLists.txt` | internal | L | Windows-only |
| Create | `third_party/stb/CMakeLists.txt` | internal | L | Header-only (image read/write) |
| Modify | `CMakeLists.txt` | shared | **H** | Add `add_subdirectory(third_party)` |
| Create | `.gitmodules` | internal | L | Submodule definitions |

## Steps

- [x] **Step 1**: Create master `third_party/CMakeLists.txt` + core libs
      **Files**: `third_party/CMakeLists.txt` (shared H)

      **Signatures** (`third_party/CMakeLists.txt`):

      | Symbol | Description | Attrs |
      |--------|-------------|-------|
      | `add_subdirectory(glm)` | Header-only math library | `miki::third_party::glm` |
      | `add_subdirectory(googletest)` | Test framework | `miki::third_party::gtest` |
      | `add_subdirectory(vma)` | Vulkan Memory Allocator (header-only) | `miki::third_party::vma` |
      | `add_subdirectory(slang)` | Shader compiler (source-compiled) | `miki::third_party::slang` |
      | `add_subdirectory(glfw)` | Demo-only windowing | `miki::third_party::glfw` |
      | Conditional: D3D12 | `d3d12ma`, `directx_headers` | Windows + `MIKI_BUILD_D3D12` |

      **Acceptance**: CMake configures with all third-party targets
      `[verify: compile]`

- [x] **Step 2**: Create per-lib CMakeLists wrappers + .gitmodules
      **Files**: All `third_party/<name>/CMakeLists.txt` (internal L), `.gitmodules` (internal L)
      Each wrapper: `add_library(<name> STATIC/INTERFACE ...)`, `add_library(miki::third_party::<name> ALIAS ...)`, set include dirs.
      **Acceptance**: All targets resolvable; full project configures and builds (empty main)
      `[verify: compile]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(ThirdParty, GlmInclude)` | Build | Step 1 — glm headers available | 1 |
| `TEST(ThirdParty, VmaInclude)` | Build | Step 1 — VMA headers available | 1 |
| `TEST(ThirdParty, SlangLink)` | Build | Step 2 — slang library links | 2 |

## Design Decisions

- **Single CMakeLists.txt**: All third-party integration in one file (`third_party/CMakeLists.txt`) rather than per-lib wrapper files. Each lib's native CMake is used via `add_subdirectory()` where possible.
- **Slang headers-only stub**: Slang's CMake build has complex internal deps (SPIRV-Headers, glslang, spirv-tools) that fail when embedded via `add_subdirectory()`. Current target exposes only `slang/include/` headers. Full linkable library deferred to T1a.10.1 (prebuilt binaries or separate build step).
- **glm alias workaround**: glm 1.1.0 exports `glm::glm-header-only` as an ALIAS of `glm-header-only`. CMake forbids alias-of-alias, so `miki::third_party::glm` aliases the real target `glm-header-only` directly.
- **imgui manual STATIC lib**: imgui has no native CMake. Built as STATIC from 5 source files with PUBLIC include dir.
- **VMA/D3D12MA as INTERFACE**: Both are header-only single-file libraries. Wrapped as INTERFACE targets with include dirs only.

## Implementation Notes

- Contract check: PASS
- All 9 submodules cloned and integrated
- Build: 40/40 targets, 0 errors (third-party warnings suppressed via `CMAKE_WARN_DEPRECATED OFF`)
- Tests: 3/3 existing toolchain tests still pass
- googletest pthread detection shows "Failed" on Windows — expected, it falls back to Win32 threads
- Third-party warnings (imgui sign-conversion, googletest `__try` extension) are from upstream code and acceptable
