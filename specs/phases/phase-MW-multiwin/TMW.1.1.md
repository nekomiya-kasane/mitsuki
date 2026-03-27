# TMW.1.1 — RenderSurface Interface + NativeWindowHandle

**Phase**: MW (Multi-Window Infrastructure)
**Component**: 1 — RenderSurface
**Dependencies**: Phase 1a IDevice, Phase 2 ISwapchain (absorbed)
**Status**: Complete
**Effort**: S (2-3h)
**Completed**: 2026-03-24

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rhi/RenderSurface.h` | **public** | **H** | `RenderSurface`, `RenderSurfaceDesc`, `RenderSurfaceConfig`, `RenderSurfaceCapabilities`, `PresentMode`, `NativeWindowHandle` |
| `include/miki/rhi/PresentMode.h` | **public** | **M** | `PresentMode` enum (Fifo, Mailbox, Immediate) |

- **Error model**: `RenderSurface::Create()` → `Result<unique_ptr<RenderSurface>>`.
  `Configure()` → `Result<void>`. `AcquireNextImage()` → `Result<TextureHandle>`.
  `Present()` → `Result<void>`. All failures are `ErrorCode`.
- **Thread safety**: NOT thread-safe. All calls on render thread.
- **Invariants**:
  - `RenderSurface` is Pimpl'd (forward-compatible ABI for Phase 15a SDK).
  - `NativeWindowHandle` is a tagged union, not `void*`.
  - API is a strict superset of `ISwapchain` (all ISwapchain methods present).
  - `Configure()` can be called multiple times (reconfiguration = resize + format change).
  - `GetCapabilities()` queries hardware for supported formats/present modes.

### Downstream Consumers

| Consumer | File | Reads |
|----------|------|-------|
| TMW.1.2 | `VulkanRenderSurface.cpp` (**internal**) | Implements `RenderSurface` virtuals |
| TMW.1.3 | `FrameManager.cpp` (**shared**) | `RenderSurface*` replaces `ISwapchain*` |
| TMW.2.1 | `MultiWindowManager.h` (**public**) | Owns `unique_ptr<RenderSurface>` per window |
| Phase 12 | `ViewRenderer.h` (**public**) | Borrows `RenderSurface*` for per-view present |
| Phase 15a | `MikiView.h` (**public**) | Wraps `RenderSurface*` in SDK Pimpl |

### Upstream Contracts

| Source | Provides |
|--------|----------|
| `ISwapchain.h` (Phase 2) | `AcquireNextImage`, `Present`, `Resize`, `GetFormat`, `GetExtent`, `GetCurrentTexture`, `GetSubmitSyncInfo` — all preserved in RenderSurface |
| `RhiDescriptors.h` (Phase 1a) | `SubmitSyncInfo` — reused directly |
| `RhiTypes.h` (Phase 1a) | `TextureHandle`, `Format`, `BackendType` |
| `ISwapchain.h` (Phase 2) | `Extent2D`, `SwapchainDesc` — absorbed into `RenderSurfaceDesc`/`RenderSurfaceConfig` |

### Technical Direction

- **Pimpl pattern**: `RenderSurface` holds `unique_ptr<Impl>`. Backend-specific `Impl`
  subclasses (VulkanImpl, D3D12Impl, etc.) created by factory. This differs from
  ISwapchain's virtual inheritance — Pimpl is preferred for ABI stability (Phase 15a SDK).
  Industry reference: wgpu's `Surface` uses opaque handle + backend dispatch table.
- **NativeWindowHandle tagged union**: Diligent's `NativeWindow` pattern. Type-safe,
  no raw `void*` casts. Each platform variant is a named struct within the union.
  Only `Win32` is implemented now; other variants are forward declarations.
- **Configure vs Resize**: wgpu pattern. `Configure()` replaces `Resize()` — it handles
  format change, present mode change, AND size change in one call. `Resize()` is kept
  as convenience (calls `Configure` with only width/height changed).
- **Capabilities query**: Vulkan `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` +
  `vkGetPhysicalDeviceSurfaceFormatsKHR` + `vkGetPhysicalDeviceSurfacePresentModesKHR`.
  D3D12: DXGI output + format enumeration. WebGPU: `surface.GetCapabilities()`.
  GL: fixed (RGBA8 + Fifo only).

## Acceptance Criteria

| # | Criterion | Verification |
|---|-----------|--------------|
| 1 | `RenderSurface.h` compiles on all 5 backends | Build both presets |
| 2 | `NativeWindowHandle` has Win32 variant with `HWND` | `static_assert(sizeof)` |
| 3 | `PresentMode` enum: Fifo, Mailbox, Immediate | Header exists |
| 4 | `RenderSurfaceDesc` + `RenderSurfaceConfig` + `RenderSurfaceCapabilities` defined | Compile |
| 5 | `RenderSurface` has all ISwapchain methods + `Configure` + `GetCapabilities` | Compile |
| 6 | Pimpl: `RenderSurface` holds `unique_ptr<Impl>`, destructor in .cpp | Compile, no incomplete type |
| 7 | 8 unit tests (type traits, NativeWindowHandle variants, config defaults) | CTest pass |

### Tests (8 new)

| # | Test Name | What It Verifies |
|---|-----------|-----------------|
| 1 | `NativeWindowHandle_Win32` | Win32 variant stores HWND correctly |
| 2 | `NativeWindowHandle_TypeTag` | Type enum discriminates variants |
| 3 | `RenderSurfaceConfig_Defaults` | Default config has sane values (BGRA8, Fifo, 2 images) |
| 4 | `RenderSurfaceCapabilities_Empty` | Default-constructed caps are valid |
| 5 | `PresentMode_Values` | Fifo=0, Mailbox=1, Immediate=2 |
| 6 | `RenderSurfaceDesc_WindowHandle` | Desc holds NativeWindowHandle |
| 7 | `RenderSurface_PimplSize` | `sizeof(RenderSurface) == sizeof(unique_ptr<Impl>)` (Pimpl) |
| 8 | `RenderSurface_MoveOnly` | Move-constructible, not copy-constructible |

## Implementation Steps

### Step 1: NativeWindowHandle + PresentMode

Create `include/miki/rhi/RenderSurface.h` with:
- `NativeWindowHandle` tagged union (Win32 active, others forward-declared)
- `PresentMode` enum
- `RenderSurfaceConfig` struct
- `RenderSurfaceCapabilities` struct
- `RenderSurfaceDesc` struct

### Step 2: RenderSurface class (Pimpl)

In same header:
- Forward-declare `struct Impl;`
- `RenderSurface` class with `unique_ptr<Impl>` member
- Public API: `Create`, `Configure`, `AcquireNextImage`, `Present`, `Resize`,
  `GetFormat`, `GetExtent`, `GetCurrentTexture`, `GetSubmitSyncInfo`,
  `GetConfig`, `GetCapabilities`
- Destructor, move ctor/assign declared (defined in .cpp)
- Delete copy ctor/assign

### Step 3: Stub .cpp

Create `src/miki/rhi/RenderSurface.cpp`:
- Define `RenderSurface::Impl` as empty struct (backends fill in TMW.1.2)
- Destructor, move ops defaulted
- `Create()` returns `NotImplemented` (backends override in TMW.1.2)
- All methods forward to `impl_->` (stub returns error for now)

### Step 4: Tests

Create `tests/unit/test_render_surface.cpp`:
- 8 tests per acceptance criteria
- Type-trait tests: `is_move_constructible`, `!is_copy_constructible`
- Config/Caps default value checks
- NativeWindowHandle variant storage

### Step 5: CMakeLists

- Add `RenderSurface.cpp` to `src/miki/rhi/CMakeLists.txt`
- Add `test_render_surface` to `tests/unit/CMakeLists.txt`

### Step 6: Build verification

- Both debug-vulkan and debug presets: 0 errors
- All existing tests pass (no regression)
- 8 new tests pass

## Implementation Notes

**Contract check: PASS** (13/13 items verified)

### Architecture Improvements (vs original plan)

| # | Change | Rationale |
|---|--------|-----------|
| A1 | `NativeWindowHandle` = `std::variant` (not C union) | C++23 type safety, `std::visit` exhaustiveness, zero overhead (all POD variants) |
| A2 | `RenderSurfaceDesc` excludes `IDevice&` | Regular type (copyable, assignable) |
| A3 | `PresentMode` inlined in `RenderSurface.h` | Only 3 values, not worth separate header |
| A4 | `Extent2D` migrated to `RhiTypes.h` | Universal type, breaks circular dep |
| A5 | `Impl` is virtual base (not plain struct) | 5 backends need internal polymorphism |
| A6 | 12 tests (was 8) | Added TriviallyDestructible, Equality, Extent2D |

### Files Created
- `include/miki/rhi/RenderSurface.h` — public header (all types + Pimpl class)
- `src/miki/rhi/RenderSurface.cpp` — Impl base + forwarding + stub factory
- `tests/unit/test_render_surface.cpp` — 12 tests

### Files Modified
- `include/miki/rhi/RhiTypes.h` — added `Extent2D` (migrated from ISwapchain.h)
- `include/miki/rhi/ISwapchain.h` — removed `Extent2D` definition (now in RhiTypes.h)
- `src/miki/rhi/CMakeLists.txt` — added `RenderSurface.cpp`
- `tests/unit/CMakeLists.txt` — added `test_render_surface` target

### Build Verification
- debug-vulkan: 0 errors, 12/12 new tests pass
- Swapchain tests: 6/6 pass (zero regression)
- Total tests: 3906 registered (was 3894, +12 new)
