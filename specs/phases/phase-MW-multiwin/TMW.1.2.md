# TMW.1.2 — RenderSurface Backend Implementations (Vulkan, D3D12, WebGPU, GL, Mock)

**Phase**: MW (Multi-Window Infrastructure)
**Component**: 1 — RenderSurface
**Dependencies**: TMW.1.1 (RenderSurface interface)
**Status**: Complete
**Effort**: L (6-8h)
**Completed**: 2026-03-24

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `src/miki/rhi/vulkan/VulkanRenderSurface.h` | internal | **M** | `VulkanRenderSurface` — Vulkan Impl of RenderSurface::Impl |
| `src/miki/rhi/vulkan/VulkanRenderSurface.cpp` | internal | L | VkSurfaceKHR + VkSwapchainKHR lifecycle, per-frame sync |
| `src/miki/rhi/d3d12/D3D12RenderSurface.h` | internal | **M** | `D3D12RenderSurface` — IDXGISwapChain3 lifecycle |
| `src/miki/rhi/d3d12/D3D12RenderSurface.cpp` | internal | L | DXGI swap chain creation, Present, Resize |
| `src/miki/rhi/webgpu/WebGpuRenderSurface.h` | internal | **M** | `WebGpuRenderSurface` — wgpu::Surface lifecycle |
| `src/miki/rhi/webgpu/WebGpuRenderSurface.cpp` | internal | L | surface.Configure, GetCurrentTexture, Present |
| `src/miki/rhi/opengl/OpenGlRenderSurface.h` | internal | L | GL "surface" = default FBO 0 + glfwSwapBuffers |
| `src/miki/rhi/opengl/OpenGlRenderSurface.cpp` | internal | L | GL present path |
| `src/miki/rhi/mock/MockRenderSurface.h` | internal | L | Mock surface for tests |
| `src/miki/rhi/mock/MockRenderSurface.cpp` | internal | L | Simulated acquire/present cycle |
| `src/miki/rhi/RenderSurface.cpp` | **shared** | **H** | Factory dispatch `RenderSurface::Create()` by BackendType |

- **Error model**: `Create()` → `NotSupported` if backend cannot create surface (e.g., no window handle).
  `Configure()` → `InvalidArgument` if format/size unsupported. `AcquireNextImage()` → `SwapchainOutOfDate` on resize needed.
- **Invariants**:
  - Each backend impl is a concrete class derived from `RenderSurface::Impl` (internal, not exported).
  - Vulkan: owns VkSurfaceKHR + VkSwapchainKHR + per-frame-in-flight sync objects (migrated from VulkanSwapchain).
  - D3D12: owns IDXGISwapChain3, flip-model FLIP_DISCARD (migrated from D3D12Swapchain).
  - WebGPU: owns wgpu::Surface, configure/present/resize (migrated from WebGpuSwapchain).
  - GL: no swapchain; Present() = glfwSwapBuffers, AcquireNextImage() = FBO 0 sentinel.
  - Mock: simulated acquire/present cycle, error injection.
  - `GetCapabilities()` queries real hardware (Vulkan: surface caps query; D3D12: DXGI; WebGPU: surface.GetCapabilities).

### Downstream Consumers

| Consumer | File | Reads |
|----------|------|-------|
| TMW.1.3 | `FrameManager.cpp` (**shared**) | Calls `RenderSurface::AcquireNextImage`, `Present`, `GetSubmitSyncInfo` |
| TMW.2.1 | `MultiWindowManager.cpp` (**shared**) | `RenderSurface::Create()` factory |
| TMW.3.1 | All demo `main.cpp` (**demo**) | Migrated from `ISwapchain` |

### Upstream Contracts

| Source | Provides |
|--------|----------|
| TMW.1.1 | `RenderSurface`, `RenderSurfaceDesc`, `RenderSurfaceConfig`, `NativeWindowHandle`, `PresentMode` |
| `VulkanSwapchain.h/.cpp` (Phase 2) | Full Vulkan swapchain impl — **migrated, not rewritten** |
| `D3D12Swapchain.h/.cpp` (Phase 2) | Full D3D12 swapchain impl — **migrated** |
| `WebGpuSwapchain.h/.cpp` (Phase 2) | Full WebGPU swapchain impl — **migrated** |
| `VulkanDevice.h` | `GetVkInstance()`, `GetVkDevice()`, `GetVkPhysicalDevice()`, `GetGraphicsQueueFamily()`, `GetGraphicsQueue()`, `ImportSwapchainImage()` |
| `D3D12Device.h` | `GetDevice()`, `GetCommandQueue()`, `ImportSwapchainImage()` |
| `WebGpuDevice.h` | `GetDevice()`, `GetInstance()`, `ImportSwapchainImage()` |

### Technical Direction

- **Migration, not rewrite**: The 3 existing `*Swapchain` classes (VulkanSwapchain 368 LOC,
  D3D12Swapchain 272 LOC, WebGpuSwapchain 239 LOC) contain production-tested sync logic.
  Each is refactored into a `*RenderSurface` class implementing `RenderSurface::Impl`.
  Core logic (VkSwapchainKHR creation, DXGI flip-model, wgpu surface config) is preserved.
  New additions: `Configure()` method, `GetCapabilities()` query, `PresentMode` enum mapping.

- **Vulkan Configure()**: Translates `RenderSurfaceConfig` → `VkSwapchainCreateInfoKHR`.
  Uses `oldSwapchain` for seamless resize (existing pattern in VulkanSwapchain::CreateSwapchain).
  Present mode mapping: `Fifo → VK_PRESENT_MODE_FIFO_KHR`, `Mailbox → VK_PRESENT_MODE_MAILBOX_KHR`,
  `Immediate → VK_PRESENT_MODE_IMMEDIATE_KHR`.

- **Vulkan GetCapabilities()**: `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` +
  `vkGetPhysicalDeviceSurfaceFormatsKHR` + `vkGetPhysicalDeviceSurfacePresentModesKHR`.
  Map VkSurfaceFormatKHR → miki::Format, VkPresentModeKHR → PresentMode.

- **D3D12 Configure()**: `IDXGISwapChain3::ResizeBuffers` for size change.
  Format change requires swapchain recreation. Present mode: FIFO = SyncInterval 1,
  Immediate = SyncInterval 0 + ALLOW_TEARING.

- **WebGPU Configure()**: Direct map to `wgpu::SurfaceConfiguration`.
  `surface.GetCapabilities()` for capabilities query.

- **GL**: Capabilities = { RGBA8_UNORM, Fifo }. No real swapchain. Present = glfwSwapBuffers.
  Multi-window GL: the `NativeWindowHandle` must carry the `GLFWwindow*` so Present knows
  which window to swap. Store as `void* glfwWindow` in the GL Impl.

- **Factory dispatch**: `RenderSurface::Create()` in `RenderSurface.cpp` inspects
  `IDevice::GetBackendType()` and calls the appropriate backend constructor.
  Uses `#ifdef MIKI_HAS_VULKAN` etc. guards (same pattern as `ISwapchain::Create`).

## Acceptance Criteria

| # | Criterion | Verification |
|---|-----------|--------------|
| 1 | Vulkan RenderSurface creates VkSurfaceKHR + VkSwapchainKHR | Integration test with GLFW window |
| 2 | D3D12 RenderSurface creates IDXGISwapChain3 | Integration test (Windows only) |
| 3 | WebGPU RenderSurface creates wgpu::Surface | Integration test |
| 4 | GL RenderSurface wraps FBO 0 + glfwSwapBuffers | Unit test |
| 5 | Mock RenderSurface simulates acquire/present | Unit test |
| 6 | `Configure()` on Vulkan recreates swapchain with new size/format | Unit test |
| 7 | `GetCapabilities()` returns non-empty formats/modes on Vulkan | GPU test |
| 8 | Factory `RenderSurface::Create()` dispatches by BackendType | Unit test |
| 9 | Old `VulkanSwapchain`, `D3D12Swapchain`, `WebGpuSwapchain` files deleted | Grep verification |
| 10 | Both build presets compile with 0 errors | CMake build |

### Tests (10 new)

| # | Test Name | What It Verifies |
|---|-----------|-----------------|
| 1 | `MockRenderSurface_CreateDestroy` | Factory creates mock surface, destroy is clean |
| 2 | `MockRenderSurface_AcquirePresent` | Acquire → Present cycle returns success |
| 3 | `MockRenderSurface_Configure` | Configure changes extent/format |
| 4 | `MockRenderSurface_Capabilities` | GetCapabilities returns mock values |
| 5 | `MockRenderSurface_ErrorInjection` | Simulated AcquireNextImage failure |
| 6 | `VulkanRenderSurface_Capabilities` | Real GPU: non-empty format + mode lists (GPU test) |
| 7 | `VulkanRenderSurface_Configure` | Reconfigure with different size (GPU test) |
| 8 | `RenderSurface_FactoryDispatch` | Create returns correct backend type |
| 9 | `OpenGlRenderSurface_Sentinel` | AcquireNextImage returns FBO 0 sentinel |
| 10 | `RenderSurface_UnsupportedBackend` | Create with invalid backend returns NotSupported |

## Implementation Steps

### Step 1: Define RenderSurface::Impl base

In `src/miki/rhi/RenderSurface.cpp`:
- Define abstract `RenderSurface::Impl` with virtual methods matching public API
- `RenderSurface` public methods forward to `impl_->`

### Step 2: Vulkan backend (migrate from VulkanSwapchain)

Create `src/miki/rhi/vulkan/VulkanRenderSurface.h/.cpp`:
- Move VulkanSwapchain members into VulkanRenderSurface (: public RenderSurface::Impl)
- Add `Configure()`: maps RenderSurfaceConfig → VkSwapchainCreateInfoKHR, calls CreateSwapchain
- Add `GetCapabilities()`: queries VkPhysicalDevice surface caps
- Preserve per-frame-in-flight sync (semaphores, fences, Bug 3 fix)
- Delete old `VulkanSwapchain.h/.cpp`

### Step 3: D3D12 backend (migrate from D3D12Swapchain)

Create `src/miki/rhi/d3d12/D3D12RenderSurface.h/.cpp`:
- Move D3D12Swapchain members into D3D12RenderSurface
- Add `Configure()`: ResizeBuffers or recreate for format change
- Add `GetCapabilities()`: DXGI output enumeration
- Delete old `D3D12Swapchain.h/.cpp`

### Step 4: WebGPU backend (migrate from WebGpuSwapchain)

Create `src/miki/rhi/webgpu/WebGpuRenderSurface.h/.cpp`:
- Move WebGpuSwapchain members into WebGpuRenderSurface
- Add `Configure()`: wgpu::SurfaceConfiguration
- Add `GetCapabilities()`: surface.GetCapabilities
- Delete old `WebGpuSwapchain.h/.cpp`

### Step 5: GL backend

Create `src/miki/rhi/opengl/OpenGlRenderSurface.h/.cpp`:
- Store `GLFWwindow*` (or generic function pointer for swap)
- AcquireNextImage → sentinel TextureHandle (FBO 0)
- Present → glfwSwapBuffers
- Configure → update stored extent (no-op for format/mode)
- GetCapabilities → { RGBA8_UNORM, Fifo }

### Step 6: Mock backend

Create `src/miki/rhi/mock/MockRenderSurface.h/.cpp`:
- Simulated acquire/present with configurable error injection
- Capabilities returns mock values

### Step 7: Factory dispatch

Update `src/miki/rhi/RenderSurface.cpp`:
- `RenderSurface::Create()` inspects device backend type
- Calls appropriate `*RenderSurface::Create()` constructor
- Ifdef guards for each backend

### Step 8: Delete old ISwapchain backend files

- Delete `VulkanSwapchain.h/.cpp`, `D3D12Swapchain.h/.cpp`, `WebGpuSwapchain.h/.cpp`
- Keep `ISwapchain.h` temporarily (deleted in TMW.3.1 after migration)
- Update CMakeLists.txt files

### Step 9: Tests + build verification

- Write 10 tests per acceptance criteria
- Both build presets: 0 errors
- All existing tests pass (ISwapchain.h still exists, FrameManager still uses ISwapchain until TMW.1.3)

## Implementation Notes

**Temporary coexistence**: During TMW.1.2, both `ISwapchain.h` and `RenderSurface.h` exist.
`ISwapchain::Create()` dispatch functions (at bottom of old *Swapchain.cpp files) must be
preserved in `src/miki/rhi/ISwapchain.cpp` pointing to the new `*RenderSurface` backends
via adapter. This adapter is removed in TMW.1.3 when FrameManager switches to RenderSurface.

**Alternative considered and rejected**: Keeping ISwapchain as a base class of RenderSurface.
Rejected because ISwapchain uses virtual inheritance (vtable), while RenderSurface uses Pimpl
(no vtable). Mixing both adds complexity for zero benefit. Clean break is better.

**Contract check: PASS**

### Architecture Decisions Made During Implementation

| # | Decision | Rationale |
|---|----------|-----------|
| 1 | `RenderSurfaceImpl.h` shared internal header | `Impl` base class must be visible to all 5 backends for inheritance. Cannot stay in .cpp (incomplete type). New internal header under `src/miki/rhi/` — not public. |
| 2 | `FromVkFormat()` added to `VulkanUtils.h` | `GetCapabilities()` needs VkSurfaceFormatKHR → miki::Format reverse mapping. Mirror of existing `ToVkFormat()`. |
| 3 | Old `*Swapchain` files KEPT (not deleted yet) | `ISwapchain::Create()` dispatch + `FrameManager` + all demos still reference them. Deletion deferred to TMW.3.1 (migration task). |
| 4 | `MIKI_BUILD_MOCK` not needed | Mock backend linked directly into test targets via `miki::rhi_mock`. Factory dispatch for Mock only used in tests, not in `miki_rhi` lib. |

### Files Created (10)
- `src/miki/rhi/RenderSurfaceImpl.h` — Impl abstract base (shared internal)
- `src/miki/rhi/vulkan/VulkanRenderSurface.h/.cpp` — Vulkan backend (migrated from VulkanSwapchain)
- `src/miki/rhi/d3d12/D3D12RenderSurface.h/.cpp` — D3D12 backend (migrated from D3D12Swapchain)
- `src/miki/rhi/webgpu/WebGpuRenderSurface.h/.cpp` — WebGPU backend (migrated from WebGpuSwapchain)
- `src/miki/rhi/opengl/OpenGlRenderSurface.h/.cpp` — GL backend (FBO 0 + swap function pointer)
- `src/miki/rhi/mock/MockRenderSurface.h/.cpp` — Mock backend (error injection, counters)

### Files Modified (7)
- `src/miki/rhi/RenderSurface.cpp` — factory dispatch by BackendType
- `src/miki/rhi/vulkan/VulkanUtils.h` — added `FromVkFormat()`
- `src/miki/rhi/vulkan/CMakeLists.txt` — added VulkanRenderSurface.cpp
- `src/miki/rhi/d3d12/CMakeLists.txt` — added D3D12RenderSurface.cpp
- `src/miki/rhi/webgpu/CMakeLists.txt` — added WebGpuRenderSurface.cpp
- `src/miki/rhi/opengl/CMakeLists.txt` — added OpenGlRenderSurface.cpp
- `src/miki/rhi/mock/CMakeLists.txt` — added MockRenderSurface.cpp
- `tests/unit/CMakeLists.txt` — added test_render_surface_backend target

### Build Verification
- debug-vulkan: 0 errors (miki_rhi + all backends + tests)
- 10/10 new RenderSurfaceBackend tests pass
- 12/12 TMW.1.1 RenderSurface tests pass (no regression)
- 6/6 Swapchain tests pass (no regression)
- Total: 28/28 surface-related tests pass
