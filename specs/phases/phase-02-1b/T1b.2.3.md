# T1b.2.3 — IDevice::CreateForWindow + GL getProcAddress Auto-Detect

**Phase**: 02-1b (Compat Backends & Full Coverage)
**Component**: OpenGL Tier4 Backend / RHI Core
**Roadmap Ref**: `roadmap.md` L1049 — `IDevice::CreateForWindow(NativeWindowInfo, DeviceConfig)`
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1b.2.1 | OpenGlDevice | Complete | `OpenGlDevice` class, glad loader, GlHandlePool |
| T1a.3.2 | IDevice + ICommandBuffer | Complete | `IDevice` virtual interface, static factory methods |
| T1a.3.1 | RhiTypes | Complete | `BackendType`, `DeviceConfig`, `ExternalContext` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rhi/IDevice.h` | **public** | **H** | `IDevice::CreateForWindow(NativeWindowInfo, DeviceConfig)` static method |
| `include/miki/rhi/RhiTypes.h` | **public** | **H** | `NativeWindowInfo` struct, `OpenGlExternalContext::nativeWindow` field |
| `src/miki/rhi/IDevice.cpp` | internal | M | `CreateForWindow` dispatch logic |
| `src/miki/rhi/opengl/OpenGlDevice.cpp` | internal | M | GL `CreateForWindow` impl + `DetectPlatformLoader()` |
| `tests/unit/test_opengl_device.cpp` | internal | L | New tests for CreateForWindow + auto-detect |

- **Error model**: `std::expected<T, ErrorCode>` — monadic, no exceptions
- **Thread safety**: single-owner. For GL, the calling thread becomes the GL context owner.
- **Invariants**:
  - `CreateForWindow` with `BackendType::OpenGL` + valid native window handle succeeds if GL 4.3+ available.
  - `CreateFromExisting(OpenGlExternalContext{})` (empty, no getProcAddress) succeeds if a GL context is current on the calling thread (auto-detect).
  - Other backends return `NotImplemented` in this phase.

### Design Decisions

**Three sub-features in this task:**

1. **`IDevice::CreateForWindow(NativeWindowInfo, DeviceConfig)`** — new static factory method on `IDevice`.
   - `NativeWindowInfo { void* windowHandle; BackendType backend; }` — lightweight struct.
   - Dispatch in `IDevice.cpp` by `backend` field.
   - GL: create GL context on the provided window via GLFW (`glfwCreateWindow` is not applicable — user already has a window). On Windows, use `wglCreateContext` on the window's HDC.
   - Vulkan/D3D12/WebGPU: return `NotImplemented` (stub).

2. **GL `getProcAddress` auto-detect** — when `OpenGlExternalContext::getProcAddress == nullptr`:
   - On Windows: `GetModuleHandleA("opengl32.dll")` → `GetProcAddress(module, "wglGetProcAddress")`.
   - On Linux: `dlopen("libGL.so")` → `dlsym(lib, "glXGetProcAddress")` or EGL equivalent.
   - Fallback: return `ErrorCode::DeviceNotReady` if detection fails.

3. **`OpenGlExternalContext` expansion** — add optional `nativeWindow` field for future use.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Modify | `include/miki/rhi/RhiTypes.h` | **public** | **H** | Add `NativeWindowInfo`, expand `OpenGlExternalContext` |
| Modify | `include/miki/rhi/IDevice.h` | **public** | **H** | Add `CreateForWindow` declaration |
| Modify | `src/miki/rhi/IDevice.cpp` | internal | M | Add `CreateForWindow` dispatch |
| Modify | `src/miki/rhi/opengl/OpenGlDevice.h` | shared | H | Add `CreateForWindow` static method |
| Modify | `src/miki/rhi/opengl/OpenGlDevice.cpp` | internal | L | Implement `CreateForWindow` + `DetectPlatformLoader()` |
| Modify | `src/miki/rhi/opengl/OpenGlDeviceFactory.h` | internal | L | Add factory free function |
| Modify | `tests/unit/test_opengl_device.cpp` | internal | L | Add tests |

## Steps

- [x] **Step 1**: Add `NativeWindowInfo` to `RhiTypes.h` + `CreateForWindow` to `IDevice.h`
      **Files**: `RhiTypes.h`, `IDevice.h`
      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Add `CreateForWindow` dispatch in `IDevice.cpp` + stubs for all backends
      **Files**: `IDevice.cpp`
      **Acceptance**: compiles, non-GL backends return `NotImplemented`
      `[verify: compile]`

- [x] **Step 3**: Implement `DetectPlatformLoader()` in `OpenGlDevice.cpp`
      **Files**: `OpenGlDevice.cpp`
      Detect `wglGetProcAddress` on Windows, `glXGetProcAddress` on Linux.
      Update `CreateFromExisting` to call auto-detect when `getProcAddress == nullptr`.
      **Acceptance**: `CreateFromExisting(OpenGlExternalContext{})` succeeds when GL context is current
      `[verify: compile]`

- [x] **Step 4**: Implement `OpenGlDevice::CreateForWindow` (Windows: WGL path)
      **Files**: `OpenGlDevice.h`, `OpenGlDevice.cpp`, `OpenGlDeviceFactory.h`
      On Windows: get HDC from HWND via `GetDC()`, create WGL context, make current, glad init.
      **Acceptance**: `CreateForWindow` succeeds with a valid HWND
      `[verify: compile]`

- [x] **Step 5**: Write tests
      **Files**: `test_opengl_device.cpp`
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(OpenGlDevice, CreateForWindow_ReturnsValid)` | Positive | CreateForWindow succeeds with hidden GLFW window's HWND | 4-5 |
| `TEST(OpenGlDevice, CreateForWindow_NullHandle)` | Error | null window handle → InvalidArgument | 4-5 |
| `TEST(OpenGlDevice, CreateForWindow_WrongBackend)` | Error | BackendType::Vulkan → NotImplemented | 2-5 |
| `TEST(OpenGlDevice, CreateFromExisting_AutoDetect)` | Positive | empty OpenGlExternalContext succeeds when GL context is current | 3-5 |
| `TEST(OpenGlDevice, CreateForWindow_BackendTypeIsGL)` | Positive | device reports BackendType::OpenGL | 4-5 |

## Implementation Notes

*(Fill during implementation)*
