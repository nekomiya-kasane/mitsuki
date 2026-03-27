# TMW.3.1 — Migrate All Existing Demos from ISwapchain → RenderSurface

**Phase**: MW (Multi-Window Infrastructure)
**Component**: 3 — Demo + Migration
**Dependencies**: TMW.1.3 (FrameManager integration)
**Status**: Complete (absorbed into TMW.1.3)
**Effort**: M (3-4h)
**Completed**: 2026-03-24

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `demos/framework/common/AppContext.h` | demo | **H** | `swapchain` member → `surface` (type change) |
| `demos/framework/glfw/GlfwBootstrap.cpp` | demo | **M** | Creates `RenderSurface` instead of `ISwapchain` |
| `demos/framework/neko/NekoBootstrap.cpp` | demo | L | Offscreen path unchanged, windowed path updated |
| All `demos/*/main.cpp` | demo | **M** | Remove `ISwapchain` references |
| `include/miki/rhi/ISwapchain.h` | **public** | **H** | **DELETED** (migration complete) |
| `src/miki/rhi/ISwapchain.cpp` | **shared** | **M** | **DELETED** (adapter removed) |

- **Error model**: No new errors. Pure mechanical migration.
- **Invariants**:
  - After this task, zero references to `ISwapchain` remain in miki codebase (excluding third_party).
  - `AppContext.swapchain` → `AppContext.surface` (`unique_ptr<RenderSurface>`).
  - `GlfwBootstrap::Init()` creates `RenderSurface::Create()` directly (no adapter).
  - `FrameManager::Create(device, *surface)` called with `RenderSurface&`.
  - All demos compile and run identically to before migration.
  - `ISwapchain.h` and all `*Swapchain.h/.cpp` backend files are deleted.

### Downstream Consumers

| Consumer | File | Reads |
|----------|------|-------|
| TMW.3.2 | `multi_window_basic/main.cpp` (**demo**) | Uses new `AppContext` with `RenderSurface` |
| All future demos | `demos/*/main.cpp` | `AppContext.surface` |
| Phase 12 | All demo infrastructure | Clean RenderSurface-only codebase |

### Upstream Contracts

| Source | Provides |
|--------|----------|
| TMW.1.2 | `RenderSurface::Create()` factory with backend dispatch |
| TMW.1.3 | `FrameManager::Create(IDevice&, RenderSurface&)` |
| TMW.1.3 | `ISwapchain::Create()` adapter (to be removed) |

### Technical Direction

- **Systematic grep-and-replace**: `ISwapchain` appears in ~87 miki source files (excluding
  third_party). Most hits are in demos, tests, and framework files. Strategy:
  1. Replace `#include "miki/rhi/ISwapchain.h"` → `#include "miki/rhi/RenderSurface.h"`
  2. Replace `ISwapchain` type references → `RenderSurface`
  3. Replace `swapchain->` method calls → `surface->` (names identical)
  4. Replace `SwapchainDesc` → `RenderSurfaceDesc` + `RenderSurfaceConfig`
  5. Delete `ISwapchain.h`, `ISwapchain.cpp`, all `*Swapchain.h/.cpp` backend files

- **AppContext change**:
  ```cpp
  // Before:
  std::unique_ptr<miki::rhi::ISwapchain> swapchain;
  // After:
  std::unique_ptr<miki::rhi::RenderSurface> surface;
  ```

- **GlfwBootstrap change**:
  ```cpp
  // Before:
  auto scResult = miki::rhi::ISwapchain::Create(*device, scDesc);
  // After:
  auto surfResult = miki::rhi::RenderSurface::Create(*device, rsDesc);
  ```

- **Demo main.cpp changes**: Minimal — most demos access swapchain only through
  `FrameManager` (BeginFrame/EndFrame). Demos that directly call `swapchain->GetExtent()`
  or `swapchain->GetFormat()` change to `surface->GetExtent()` / `surface->GetFormat()`.

- **Test migration**: `test_swapchain.cpp` → `test_render_surface_integration.cpp`.
  Test names updated. Test logic unchanged.

## Acceptance Criteria

| # | Criterion | Verification |
|---|-----------|--------------|
| 1 | Zero `ISwapchain` references in miki source (excl. third_party) | `grep -r ISwapchain include/ src/ demos/ tests/` returns 0 |
| 2 | `ISwapchain.h` deleted | File does not exist |
| 3 | `VulkanSwapchain.h/.cpp` deleted | Files do not exist |
| 4 | `D3D12Swapchain.h/.cpp` deleted | Files do not exist |
| 5 | `WebGpuSwapchain.h/.cpp` deleted | Files do not exist |
| 6 | `AppContext.h` has `RenderSurface` member | Code inspection |
| 7 | All existing demos compile | Both build presets: 0 errors |
| 8 | All existing tests pass | CTest: 0 regression |
| 9 | `geometry_pipeline` demo runs correctly | Visual verification |
| 10 | `deferred_pbr_basic` demo runs correctly | Visual verification |

### Tests (0 new — existing tests migrated)

No new tests. Existing swapchain tests are renamed and migrated to use `RenderSurface`.

## Implementation Steps

### Step 1: Identify all ISwapchain usage sites

```bash
grep -rn "ISwapchain" include/ src/ demos/ tests/ --include="*.h" --include="*.cpp"
```
Expected: ~25 miki-owned files (excluding third_party).

### Step 2: Update AppContext.h

- Replace `ISwapchain` include with `RenderSurface` include
- Change member type and name: `swapchain` → `surface`
- Update doc comments

### Step 3: Update GlfwBootstrap.cpp

- Replace `ISwapchain::Create()` with `RenderSurface::Create()`
- Build `RenderSurfaceDesc` from HWND (instead of `SwapchainDesc`)
- Create `FrameManager` with `RenderSurface&` (no adapter)

### Step 4: Update NekoBootstrap.cpp

- Offscreen path: unchanged
- Windowed path (if any): same as GlfwBootstrap

### Step 5: Update all demo main.cpp files

For each demo that references `swapchain`:
- Replace includes
- Replace `ctx.swapchain->` with `ctx.surface->`
- Replace types

Known demos: geometry_pipeline, deferred_pbr_basic, triangle, forward_cubes,
bindless_scene, triangle_web, forward_cubes_web, slang-playground.

### Step 6: Update test files

- `test_swapchain.cpp` → rename to `test_render_surface_integration.cpp`
- Update test names and includes
- `test_frame_staging.cpp` — update if it references ISwapchain

### Step 7: Delete old files

- `include/miki/rhi/ISwapchain.h`
- `src/miki/rhi/ISwapchain.cpp`
- `src/miki/rhi/vulkan/VulkanSwapchain.h`
- `src/miki/rhi/vulkan/VulkanSwapchain.cpp`
- `src/miki/rhi/d3d12/D3D12Swapchain.h`
- `src/miki/rhi/d3d12/D3D12Swapchain.cpp`
- `src/miki/rhi/webgpu/WebGpuSwapchain.h`
- `src/miki/rhi/webgpu/WebGpuSwapchain.cpp`

### Step 8: Update CMakeLists.txt files

- Remove old swapchain source files from CMakeLists
- Ensure RenderSurface source files already added (TMW.1.2)
- Rename test target if needed

### Step 9: Build + test verification

- Both build presets: 0 errors
- CTest: all tests pass, 0 regression
- Grep verification: zero ISwapchain hits
- Visual: at least 2 demos run correctly

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Missed ISwapchain reference (compile error) | Low | Low | Systematic grep before build |
| Demo runtime regression (sync bug) | Low | Medium | RenderSurface backend code is migrated, not rewritten |
| Third-party code uses ISwapchain | None | None | Third-party has its own swapchain abstractions |
