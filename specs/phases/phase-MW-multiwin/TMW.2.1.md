# TMW.2.1 — MultiWindowManager Core (create/destroy/enumerate)

**Phase**: MW (Multi-Window Infrastructure)
**Component**: 2 — MultiWindowManager
**Dependencies**: TMW.1.3 (FrameManager + RenderSurface integration)
**Status**: Complete
**Effort**: M (4-5h)
**Completed**: 2026-03-24

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rhi/MultiWindowManager.h` | **public** | **H** | `MultiWindowManager`, `WindowDesc`, `WindowHandle`, `WindowInfo` |
| `src/miki/rhi/MultiWindowManager.cpp` | **shared** | **M** | Window lifecycle, per-window resource management |

- **Error model**: `CreateWindow()` → `Result<WindowHandle>`. `DestroyWindow()` → `Result<void>`.
  `Create()` → `Result<MultiWindowManager>`. Errors: `InvalidArgument` (zero size), `DeviceLost`
  (surface creation failure), `ResourceExhausted` (max windows reached).
- **Thread safety**: NOT thread-safe. Single render-thread orchestration.
  Phase 13 may add thread-safe wrapper.
- **Invariants**:
  - `MultiWindowManager` owns all `RenderSurface` and `FrameManager` instances.
  - `IDevice` is borrowed (not owned). Device must outlive manager.
  - `WindowHandle.id` is stable, monotonically increasing, never reused (generation-free).
  - `DestroyWindow()` performs per-surface `WaitIdle` before resource cleanup (no global stall).
  - Max 8 simultaneous windows (configurable). Beyond limit → `ResourceExhausted`.
  - `GetActiveWindows()` returns only non-minimized, non-zero-size windows.
  - `ShouldClose()` returns true when ALL windows are closed (not just one).

### Downstream Consumers

| Consumer | File | Reads |
|----------|------|-------|
| TMW.2.2 | `GlfwMultiWindow.cpp` (**shared**) | GLFW-specific window creation hooks |
| TMW.3.2 | `multi_window_basic/main.cpp` (**demo**) | Full API: Create/Destroy/GetSurface/GetFrameManager/PollEvents |
| Phase 12 | `ViewRenderer.h` (**public**) | `GetRenderSurface(handle)`, extends with `GetViewRenderer(handle)` |
| Phase 15a | `MikiView.h` (**public**) | Wraps MultiWindowManager for SDK |

### Upstream Contracts

| Source | Provides |
|--------|----------|
| TMW.1.1 | `RenderSurface`, `RenderSurfaceDesc`, `RenderSurfaceConfig`, `NativeWindowHandle` |
| TMW.1.3 | `FrameManager::Create(IDevice&, RenderSurface&)` |
| `IDevice.h` (Phase 1a) | `GetBackendType()`, `WaitIdle()` |

### Technical Direction

- **Filament-inspired ownership**: Filament `Engine` owns `SwapChain` objects; user creates
  them via `Engine::createSwapChain()`. Similarly, `MultiWindowManager` owns all per-window
  resources. User gets back a `WindowHandle` (lightweight ID), accesses resources via
  `GetRenderSurface(handle)`.

- **Platform-agnostic core**: `MultiWindowManager` does NOT create OS windows directly.
  It accepts a `WindowCreateCallback` (or uses a platform-specific `IWindowBackend`
  interface) that creates the actual OS window and returns a `NativeWindowHandle`.
  TMW.2.2 provides the GLFW implementation of this callback.
  This keeps the core portable (no GLFW dependency in `miki::rhi`).

- **IWindowBackend interface**:
  ```cpp
  class IWindowBackend {
  public:
      virtual auto CreateNativeWindow(const WindowDesc& desc) -> Result<NativeWindowHandle> = 0;
      virtual auto DestroyNativeWindow(NativeWindowHandle handle) -> void = 0;
      virtual auto PollEvents() -> void = 0;
      virtual auto ShouldClose(NativeWindowHandle handle) -> bool = 0;
      virtual auto GetFramebufferSize(NativeWindowHandle handle) -> Extent2D = 0;
      virtual ~IWindowBackend() = default;
  };
  ```
  `MultiWindowManager::Create(IDevice&, unique_ptr<IWindowBackend>)`.

- **Internal storage**: `std::vector<WindowSlot>` where each slot holds:
  ```cpp
  struct WindowSlot {
      uint32_t id;
      NativeWindowHandle nativeWindow;
      unique_ptr<RenderSurface> surface;
      FrameManager frameManager;
      bool alive = true;
      bool minimized = false;
  };
  ```
  Destroy marks `alive = false` and performs cleanup. No slot reuse (IDs monotonic).

- **Per-window render loop pattern** (established for demos):
  ```cpp
  for (auto handle : manager.GetActiveWindows()) {
      auto* fm = manager.GetFrameManager(handle);
      auto ctx = fm->BeginFrame();
      // ... record commands ...
      fm->EndFrame(cmd);
  }
  manager.PollEvents();
  ```

## Acceptance Criteria

| # | Criterion | Verification |
|---|-----------|--------------|
| 1 | `MultiWindowManager.h` compiles on all backends | Build |
| 2 | `IWindowBackend` interface defined | Header exists |
| 3 | `Create()` with mock backend succeeds | Unit test |
| 4 | `CreateWindow()` returns valid handle | Unit test |
| 5 | `DestroyWindow()` cleans up resources | Unit test |
| 6 | `GetRenderSurface(handle)` returns non-null | Unit test |
| 7 | `GetFrameManager(handle)` returns non-null | Unit test |
| 8 | `GetActiveWindows()` skips destroyed windows | Unit test |
| 9 | `ShouldClose()` returns true only when all closed | Unit test |
| 10 | Max window limit enforced | Unit test |
| 11 | Both build presets: 0 errors | CMake build |
| 12 | 12 new tests pass | CTest |

### Tests (12 new)

| # | Test Name | What It Verifies |
|---|-----------|-----------------|
| 1 | `MultiWindowManager_Create` | Factory creates manager with mock backend |
| 2 | `MultiWindowManager_CreateWindow` | CreateWindow returns valid handle |
| 3 | `MultiWindowManager_DestroyWindow` | DestroyWindow removes from active list |
| 4 | `MultiWindowManager_GetSurface` | GetRenderSurface returns non-null for valid handle |
| 5 | `MultiWindowManager_GetFrameManager` | GetFrameManager returns non-null for valid handle |
| 6 | `MultiWindowManager_InvalidHandle` | GetRenderSurface with invalid handle returns nullptr |
| 7 | `MultiWindowManager_ActiveWindows` | GetActiveWindows excludes destroyed windows |
| 8 | `MultiWindowManager_ShouldClose_AllOpen` | ShouldClose returns false when windows exist |
| 9 | `MultiWindowManager_ShouldClose_AllClosed` | ShouldClose returns true when all destroyed |
| 10 | `MultiWindowManager_MaxWindows` | Creating 9th window returns ResourceExhausted |
| 11 | `MultiWindowManager_HandleMonotonic` | Second window gets higher ID than first |
| 12 | `MultiWindowManager_DoubleDestroy` | Destroying same handle twice returns InvalidArgument |

## Implementation Steps

### Step 1: IWindowBackend interface

Create `include/miki/rhi/MultiWindowManager.h`:
- `IWindowBackend` abstract base class
- `WindowDesc` struct (title, width, height, surfaceConfig)
- `WindowHandle` struct (uint32_t id)
- `WindowInfo` struct (handle, extent, alive, minimized)

### Step 2: MultiWindowManager class

In same header:
- `Create(IDevice&, unique_ptr<IWindowBackend>)` factory
- `CreateWindow`, `DestroyWindow`
- `GetRenderSurface`, `GetFrameManager`, `GetNativeWindow`
- `GetWindowCount`, `GetAllWindows`, `GetActiveWindows`
- `PollEvents`, `ShouldClose`

### Step 3: Implementation

Create `src/miki/rhi/MultiWindowManager.cpp`:
- Internal `WindowSlot` vector
- `CreateWindow`: backend.CreateNativeWindow → RenderSurface::Create → FrameManager::Create
- `DestroyWindow`: FrameManager.WaitAll → destroy surface → backend.DestroyNativeWindow
- `GetActiveWindows`: filter alive && !minimized && extent > 0
- `PollEvents`: delegates to backend.PollEvents()
- `ShouldClose`: check backend.ShouldClose for all alive windows, remove closed ones

### Step 4: MockWindowBackend for tests

Create `src/miki/rhi/mock/MockWindowBackend.h`:
- Returns dummy `NativeWindowHandle` (Win32 type with fake HWND)
- `ShouldClose` controllable via `SetShouldClose(handle, bool)`

### Step 5: Tests

Create `tests/unit/test_multi_window_manager.cpp`:
- 12 tests per acceptance criteria
- All use MockWindowBackend + MockRenderSurface (from TMW.1.2)

### Step 6: CMakeLists + build verification

## Implementation Notes

**Contract check: PASS**

### Architecture Decisions

| # | Decision | Rationale |
|---|----------|-----------|
| 1 | `IWindowBackend` uses `void* nativeToken` (not `NativeWindowHandle`) for window ID | NativeWindowHandle is `std::variant` (value type, not hashable). Token = backend-internal opaque ID (e.g. GLFWwindow*). |
| 2 | `WindowSlot::frameManager` is `std::optional<FrameManager>` | FrameManager has private ctor (Pimpl). Cannot default-construct. Optional defers construction to CreateWindow(). |
| 3 | `MIKI_BUILD_MOCK` added to `miki_rhi` CMakeLists | RenderSurface::Create() factory needs mock dispatch path for tests. Same pattern as MIKI_BUILD_VULKAN etc. |
| 4 | `ErrorCode::ResourceExhausted` added (0x0008) | Max 8 windows limit needs a specific error. No existing code covers this. |
| 5 | `ProcessWindowEvents()` auto-destroys closed windows + detects resize | Automatic close handling avoids manual ShouldClose polling per-window in demo code. |
| 6 | `IsMinimized()` added to IWindowBackend | GetActiveWindows() needs to skip minimized windows to avoid zero-size framebuffer issues. |

### Files Created
- `include/miki/rhi/MultiWindowManager.h` — public header (IWindowBackend + WindowDesc/Handle/Info + MultiWindowManager)
- `src/miki/rhi/MultiWindowManager.cpp` — Pimpl impl (~290 LOC)
- `src/miki/rhi/mock/MockWindowBackend.h` — test-only backend (controllable state)
- `tests/unit/test_multi_window_manager.cpp` — 16 tests

### Files Modified
- `include/miki/core/ErrorCode.h` — added ResourceExhausted
- `src/miki/rhi/CMakeLists.txt` — added MultiWindowManager.cpp + MIKI_BUILD_MOCK linkage
- `tests/unit/CMakeLists.txt` — added test_multi_window_manager target

### Build Verification
- debug-vulkan: 0 errors
- 16/16 MultiWindowManager tests pass
- All previous tests unaffected
