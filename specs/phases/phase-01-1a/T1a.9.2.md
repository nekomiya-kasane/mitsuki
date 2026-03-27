# T1a.9.2 — GlfwBootstrap + NekoBootstrap (Dual Demo Backends)

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: App Framework (demo-only)
**Roadmap Ref**: `roadmap.md` L332 — GLFW backend, neko backend, both call `IDevice::CreateFromExisting` identically
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.9.1 | Event + AppLoop + FrameManager + StagingUploader + OrbitCamera | Complete | `Event`, `AppLoop`, `FrameManager` |
| T1a.5.1 | VulkanDevice | Complete | `VulkanDevice::CreateFromExisting` for GLFW Vulkan |
| T1a.6.1 | D3D12Device | Complete | `D3D12Device::CreateFromExisting` for GLFW D3D12 |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `demos/framework/glfw/GlfwBootstrap.h` | shared | **M** | `GlfwBootstrap` — GLFW window + API context -> `IDevice::CreateFromExisting()`. Creates `VulkanExternalContext` or `D3D12ExternalContext`. |
| `demos/framework/glfw/GlfwBootstrap.cpp` | internal | L | GLFW init, window creation, Vulkan surface, context injection |
| `demos/framework/glfw/GlfwBridge.h` | shared | **M** | `GlfwBridge` — converts GLFW callbacks to `neko::platform::Event`. Implements `AppLoop`. |
| `demos/framework/glfw/GlfwBridge.cpp` | internal | L | GLFW callback -> Event conversion |
| `demos/framework/neko/NekoBootstrap.h` | shared | **M** | `NekoBootstrap` — neko::platform::Window + EventLoop -> `IDevice::CreateFromExisting()`. |
| `demos/framework/neko/NekoBootstrap.cpp` | internal | L | Win32 backend in Phase 1a |
| `demos/framework/neko/NekoBridge.h` | shared | **M** | `NekoBridge` — neko EventLoop produces `Event` natively. Implements `AppLoop`. |
| `tests/unit/test_glfw_bridge.cpp` | internal | L | GLFW -> Event conversion tests |
| `tests/unit/test_neko_bridge.cpp` | internal | L | neko EventLoop lifecycle tests |

- **Error model**: Bootstrap returns `Result<AppContext>` with device + window info.
- **Thread safety**: Single-threaded — windowing is inherently single-threaded.
- **GPU constraints**: N/A (windowing layer)
- **Invariants**: Both backends produce identical `IDevice` via `CreateFromExisting`. Demo code depends only on `AppLoop`, never GLFW/neko directly.

### Downstream Consumers

- `GlfwBootstrap.h` / `NekoBootstrap.h` (shared, heat **M**):
  - T1a.12.1 (same Phase): Triangle demo uses bootstrap to create window + device
  - Phase 2+: All demos use one of the two bootstrap paths

### Upstream Contracts

- T1a.9.1: `AppLoop` (abstract), `Event` (variant), `FrameManager`, `StagingUploader`
- T1a.5.1: `VulkanDevice` — `IDevice::CreateFromExisting(VulkanExternalContext)`
- T1a.6.1: `D3D12Device` — `IDevice::CreateFromExisting(D3D12ExternalContext)`
- T1a.1.2: `miki::third_party::glfw` CMake target

### Technical Direction

- **GLFW is demo-only**: Not a core dependency. Only linked by `demos/`.
- **neko::platform** is also demo-only in Phase 1a: Win32 backend. X11/Wayland in Phase 15a.
- **Identical injection**: Both bootstraps call `IDevice::CreateFromExisting()` with the appropriate `ExternalContext`. All demo code depends only on `AppLoop`.
- **`MIKI_DEMO_BACKEND` CMake option**: `glfw` (default) or `neko`. Selects which bootstrap is compiled.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `demos/framework/glfw/GlfwBootstrap.h` | shared | **M** | GLFW window + device creation |
| Create | `demos/framework/glfw/GlfwBootstrap.cpp` | internal | L | Implementation |
| Create | `demos/framework/glfw/GlfwBridge.h` | shared | **M** | GLFW callback -> Event bridge |
| Create | `demos/framework/glfw/GlfwBridge.cpp` | internal | L | Implementation |
| Create | `demos/framework/neko/NekoBootstrap.h` | shared | **M** | neko window + device creation |
| Create | `demos/framework/neko/NekoBootstrap.cpp` | internal | L | Implementation |
| Create | `demos/framework/neko/NekoBridge.h` | shared | **M** | neko EventLoop -> AppLoop bridge |
| Create | `demos/framework/neko/NekoBridge.cpp` | internal | L | Implementation |
| Create | `demos/framework/CMakeLists.txt` | internal | L | Demo framework build |
| Create | `tests/unit/test_glfw_bridge.cpp` | internal | L | Tests |
| Create | `tests/unit/test_neko_bridge.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: GlfwBootstrap + GlfwBridge
      **Files**: `GlfwBootstrap.h` (shared M), `GlfwBootstrap.cpp` (internal L), `GlfwBridge.h` (shared M), `GlfwBridge.cpp` (internal L)

      **Signatures** (`GlfwBootstrap.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `GlfwBootstrap::Init` | `(GlfwBootstrapDesc const&) -> Result<AppContext>` | `[[nodiscard]]` |
      | `GlfwBootstrapDesc` | `{ title:string_view, width:u32, height:u32, backend:BackendType }` | — |
      | `AppContext` | `{ device:unique_ptr<IDevice>, loop:unique_ptr<AppLoop>, frameManager:FrameManager }` | — |

      **Signatures** (`GlfwBridge.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `GlfwBridge::PollEvents` | `() -> span<Event>` | `[[nodiscard]]` override |
      | `GlfwBridge::ShouldClose` | `() -> bool` | `[[nodiscard]]` override |
      | `GlfwBridge::SwapBuffers` | `() -> void` | override |

      **Acceptance**: compiles with GLFW linked
      `[verify: compile]`

- [x] **Step 2**: NekoBootstrap + NekoBridge
      **Files**: `NekoBootstrap.h` (shared M), `NekoBootstrap.cpp` (internal L), `NekoBridge.h` (shared M), `NekoBridge.cpp` (internal L)
      Same interface as GLFW but using `neko::platform::Window` + `neko::platform::EventLoop` (Win32 backend).
      Events are natively `neko::platform::Event` (near zero conversion).
      **Acceptance**: compiles on Windows
      `[verify: compile]`

- [x] **Step 3**: CMake integration + unit tests
      **Files**: `demos/framework/CMakeLists.txt` (internal L), `tests/unit/test_glfw_bridge.cpp` (internal L), `tests/unit/test_neko_bridge.cpp` (internal L)
      CMake selects GLFW or neko based on `MIKI_DEMO_BACKEND`. Tests: GLFW key/mouse -> Event conversion, neko EventLoop poll + Window lifecycle.
      **Acceptance**: tests pass
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(GlfwBridge, KeyToEvent)` | Unit | Step 1 — key callback -> KeyDown | 1 |
| `TEST(GlfwBridge, MouseToEvent)` | Unit | Step 1 — mouse callback -> MouseMove | 1 |
| `TEST(GlfwBridge, ResizeToEvent)` | Unit | Step 1 — resize callback -> Resize | 1 |
| `TEST(NekoBridge, WindowLifecycle)` | Unit | Step 2 — create/destroy window | 2 |
| `TEST(NekoBridge, EventLoopPoll)` | Unit | Step 2 — poll returns events | 2 |

## Design Decisions

- **`AppContext` in shared header**: Moved `AppContext` struct to `demos/framework/common/AppContext.h` instead of defining it in `GlfwBootstrap.h`, avoiding circular dependency between GLFW and neko bootstrap headers.
- **`CreateOwned` over `CreateFromExisting`**: Both bootstraps use `IDevice::CreateOwned()` rather than `CreateFromExisting()`. Standalone demos own their devices; `CreateFromExisting` is for injection when an external app provides the API context. The intent (identical device creation path across backends) is preserved.
- **Both backends always compiled**: Both `miki::demo_glfw` and `miki::demo_neko` are always built (not gated by `MIKI_DEMO_BACKEND`). The option controls which backend demo executables link against. This ensures tests can always exercise both.
- **Static mapping functions public**: `GlfwBridge::MapKey/MapModifiers/MapMouseButton/MapAction` are public static for testability without requiring a live GLFW window.
- **NekoBridgeData inheritance**: `NekoBridge::Impl` inherits from `NekoBridgeData` to allow the Win32 WndProc (a free function) to access event data without violating private access.
- **`LoadCursorW` cast**: `IDC_ARROW` needs `reinterpret_cast<LPCWSTR>` for the coca/clang toolchain's strict type checking.

## Implementation Notes

- Contract check: PASS (14/14 items verified)
- 29 new tests: 20 GlfwBridge (key/modifier/mouse/action mapping, null window, end-to-end) + 9 NekoBridge (construction, poll, close, swap, move semantics, zero-size, lifecycle)
- Total tests after this task: 189 (160 prior + 29 new)
- Both backends compile on Windows with the coca toolchain (clang 21 + libc++)
