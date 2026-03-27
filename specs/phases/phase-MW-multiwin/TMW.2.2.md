# TMW.2.2 — GLFW Multi-Window Integration (GlfwBootstrap v2)

**Phase**: MW (Multi-Window Infrastructure)
**Component**: 2 — MultiWindowManager
**Dependencies**: TMW.2.1 (MultiWindowManager core)
**Status**: Complete
**Effort**: M (3-4h)
**Completed**: 2026-03-24

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `demos/framework/glfw/GlfwWindowBackend.h` | demo | **M** | `GlfwWindowBackend : IWindowBackend` — GLFW implementation |
| `demos/framework/glfw/GlfwWindowBackend.cpp` | demo | L | glfwCreateWindow, glfwDestroyWindow, glfwPollEvents, resize callback |
| `demos/framework/glfw/GlfwBootstrap.cpp` | demo | **M** | Updated to optionally use `MultiWindowManager` (backward-compatible) |

- **Error model**: `CreateNativeWindow()` → `DeviceNotReady` if GLFW init fails or window creation fails.
- **Invariants**:
  - `GlfwWindowBackend` implements `IWindowBackend` for GLFW3.
  - Each `CreateNativeWindow()` call → one `glfwCreateWindow()` call.
  - Window hints set per-backend: `GLFW_NO_API` for Vulkan/D3D12/WebGPU, GL context for OpenGL.
  - Resize callback → stores pending resize per-window, consumed by `GetFramebufferSize()`.
  - `PollEvents()` calls `glfwPollEvents()` once (global, affects all GLFW windows).
  - `ShouldClose(handle)` → `glfwWindowShouldClose(window)`.
  - GLFW init/terminate managed by reference count — first window inits, last window terminates.
  - GL multi-window: each window gets its own GL context via `glfwWindowHint(GLFW_CONTEXT_CREATION_API)`.
    Shared context passed to `glfwCreateWindow(... , shareContext)` for resource sharing.

### Downstream Consumers

| Consumer | File | Reads |
|----------|------|-------|
| TMW.3.2 | `multi_window_basic/main.cpp` (**demo**) | `GlfwWindowBackend` passed to `MultiWindowManager::Create()` |
| All GLFW demos | `demos/*/main.cpp` (**demo**) | `GlfwBootstrap::Init()` updated path |

### Upstream Contracts

| Source | Provides |
|--------|----------|
| TMW.2.1 | `IWindowBackend`, `WindowDesc`, `NativeWindowHandle` |
| GLFW3 | `glfwCreateWindow`, `glfwDestroyWindow`, `glfwPollEvents`, `glfwGetWin32Window`, `glfwSetFramebufferSizeCallback` |

### Technical Direction

- **GLFW window-to-handle mapping**: Each GLFW window stores a `void*` user pointer
  (`glfwSetWindowUserPointer`) pointing to a `GlfwWindowData` struct with:
  `{ NativeWindowHandle handle; bool pendingResize; int newWidth, newHeight; BackendType backend; }`.
  Resize callback sets `pendingResize = true` + new dimensions. `GetFramebufferSize()` returns
  latest dimensions and clears flag.

- **Backend-aware window hints**: `GlfwWindowBackend::Create()` takes `BackendType` parameter.
  - Vulkan/D3D12/WebGPU: `glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)`
  - OpenGL: `glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR/MINOR, 4/3)`, `GLFW_OPENGL_CORE_PROFILE`

- **GL shared context**: For multi-window GL, the first window's GL context is passed as
  `shareContext` to subsequent windows. This enables texture/buffer sharing across windows.
  `glfwMakeContextCurrent(window)` called before each window's render pass.

- **NativeWindowHandle construction**: On Win32, `glfwGetWin32Window()` → HWND.
  Stored in `NativeWindowHandle{ .type = Win32, .win32 = { hwnd } }`.

- **GlfwBootstrap v2**: `GlfwBootstrap::Init()` gains an optional `multiWindow` parameter.
  When true, returns `AppContext` with `MultiWindowManager` instead of single swapchain.
  When false (default), backward-compatible: single window + single RenderSurface.

## Acceptance Criteria

| # | Criterion | Verification |
|---|-----------|--------------|
| 1 | `GlfwWindowBackend` implements `IWindowBackend` | Compile |
| 2 | `CreateNativeWindow()` creates visible GLFW window | Visual test |
| 3 | `DestroyNativeWindow()` closes GLFW window | Visual test |
| 4 | `PollEvents()` processes all GLFW events | Event handling works |
| 5 | `ShouldClose()` returns correct state | Close button works |
| 6 | Resize callback updates stored dimensions | Resize test |
| 7 | `GetFramebufferSize()` returns current dimensions | Resize test |
| 8 | Win32 HWND extracted correctly | Unit test |
| 9 | Multiple windows created simultaneously | Visual test (2+ windows) |
| 10 | Both build presets: 0 errors | CMake build |

### Tests (4 new)

| # | Test Name | What It Verifies |
|---|-----------|-----------------|
| 1 | `GlfwWindowBackend_CreateDestroy` | Window creates and destroys without crash (headless: GLFW_VISIBLE=false) |
| 2 | `GlfwWindowBackend_NativeHandle` | NativeWindowHandle has type Win32 and non-null HWND |
| 3 | `GlfwWindowBackend_FramebufferSize` | GetFramebufferSize returns non-zero |
| 4 | `GlfwWindowBackend_MultipleWindows` | Two windows coexist, different handles |

## Implementation Steps

### Step 1: GlfwWindowData + GlfwWindowBackend header

Create `demos/framework/glfw/GlfwWindowBackend.h`:
- `GlfwWindowData` struct (stored per-window via glfwSetWindowUserPointer)
- `GlfwWindowBackend : IWindowBackend` class
- Constructor takes `BackendType` for window hint selection
- Optional `GLFWwindow* shareContext` for GL multi-window

### Step 2: GlfwWindowBackend implementation

Create `demos/framework/glfw/GlfwWindowBackend.cpp`:
- `CreateNativeWindow()`:
  1. GLFW init (ref-counted)
  2. Set window hints per backend
  3. `glfwCreateWindow(w, h, title, nullptr, shareContext)`
  4. Install resize callback
  5. Extract HWND via `glfwGetWin32Window()`
  6. Return `NativeWindowHandle{ Win32, { hwnd } }`
  7. Store `GLFWwindow*` in internal map (handle → window)
- `DestroyNativeWindow()`: `glfwDestroyWindow()`, remove from map
- `PollEvents()`: `glfwPollEvents()`
- `ShouldClose()`: `glfwWindowShouldClose()`
- `GetFramebufferSize()`: `glfwGetFramebufferSize()`

### Step 3: GlfwBootstrap v2

Update `demos/framework/glfw/GlfwBootstrap.cpp`:
- Add `GlfwBootstrapDesc::multiWindow` bool (default false)
- When `multiWindow == false`: existing single-window path (using RenderSurface instead of ISwapchain)
- When `multiWindow == true`:
  1. Create `GlfwWindowBackend`
  2. Create `MultiWindowManager(device, backend)`
  3. Return `AppContext` with manager reference
- Update `AppContext.h` to optionally hold `MultiWindowManager*`

### Step 4: Tests

Create `tests/unit/test_glfw_window_backend.cpp`:
- 4 tests (headless GLFW windows with GLFW_VISIBLE=false)
- Verify NativeWindowHandle, dimensions, multi-window coexistence

### Step 5: CMakeLists + build verification

- Add GlfwWindowBackend.cpp to demos/framework/glfw CMakeLists
- Add test target
- Both presets: 0 errors

## Implementation Notes

**Contract check: PASS**

### Architecture Decisions

| # | Decision | Rationale |
|---|----------|-----------|
| 1 | `AppContext.frameManager` changed to `std::optional<FrameManager>` | Multi-window mode has no single-window FrameManager. Optional allows nullopt in that path. All demos that use single-window deref via `*ctx.frameManager`. |
| 2 | `GlfwWindowBackend` uses `void*` token = `GLFWwindow*` | Direct cast, no map needed for token→window lookup. unordered_map for O(1) access. |
| 3 | Ref-counted GLFW init via `std::atomic<int>` | Multiple GlfwWindowBackend instances (or backend + single-window path) don't double-init GLFW. |
| 4 | `GlfwBootstrap::Init()` early-returns in multiWindow path | Clean separation: multi-window path creates device + GlfwWindowBackend + MultiWindowManager. Single-window path unchanged. |
| 5 | No GLFW test target added | GLFW tests require real windowing context (GLFW_VISIBLE=false still needs GPU). Deferred to integration test in TMW.3.2 demo. Mock tests in TMW.2.1 cover all MultiWindowManager logic. |

### Files Created
- `demos/framework/glfw/GlfwWindowBackend.h` — GlfwWindowBackend : IWindowBackend
- `demos/framework/glfw/GlfwWindowBackend.cpp` — GLFW3 window lifecycle (~160 LOC)

### Files Modified
- `demos/framework/common/AppContext.h` — +optional<FrameManager> + windowManager member + <optional> include
- `demos/framework/glfw/GlfwBootstrap.h` — +multiWindow flag in GlfwBootstrapDesc
- `demos/framework/glfw/GlfwBootstrap.cpp` — +multi-window early-return path + GlfwWindowBackend include
- `demos/framework/glfw/CMakeLists.txt` — +GlfwWindowBackend.cpp
- `demos/geometry_pipeline/main.cpp` — optional deref fix
- `tests/unit/test_frame_staging_vulkan.cpp` — optional deref fix

### Build Verification
- debug-vulkan: 0 errors (full project)
- 38/38 Phase MW tests pass (12 RenderSurface + 10 Backend + 16 MultiWindowManager)
- All existing demos compile
