# T1b.3.1 — WebGpuDevice (Dawn Native, CreateFromExisting + CreateOwned)

**Phase**: 02-1b (Compat Backends & Full Coverage)
**Component**: WebGPU Tier3 Backend
**Roadmap Ref**: `roadmap.md` L1147 — `WebGpuDevice` via Dawn native C++
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.3.2 | IDevice + ICommandBuffer | Complete | `IDevice` virtual interface |
| T1a.3.3 | GpuCapabilityProfile | Complete | `CapabilityTier::Tier3_WebGPU`, `GpuCapabilityProfile` |
| T1a.3.1 | RhiTypes | Complete | `BackendType::WebGPU`, `WebGpuExternalContext`, `Handle<Tag>` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `src/miki/rhi/webgpu/WebGpuDevice.h` | shared | **H** | `WebGpuDevice` — `IDevice` impl via Dawn. Used by T1b.3.2, T1b.4.1. |
| `src/miki/rhi/webgpu/WebGpuDevice.cpp` | internal | L | Device creation, resource CRUD via wgpu API |
| `src/miki/rhi/webgpu/WgpuHandlePool.h` | shared | **M** | Handle<Tag> → wgpu object mapping |
| `src/miki/rhi/webgpu/CMakeLists.txt` | internal | L | `miki_rhi_webgpu` STATIC library |
| `tests/unit/test_webgpu_device.cpp` | internal | L | Unit tests |

- **Error model**: `std::expected<T, ErrorCode>` — Dawn error callbacks mapped to ErrorCode
- **Thread safety**: single-owner. Dawn device operations must happen on the thread that created it.
- **Invariants**: `CreateFromExisting(WebGpuExternalContext)` succeeds if `device != nullptr`. `CreateOwned` creates Dawn headless instance (null adapter) for CI.

### Downstream Consumers

- `WebGpuDevice.h` (shared, heat **H**):
  - T1b.3.2 (same Phase): `WebGpuCommandBuffer` needs wgpu::Device for encoder creation
  - T1b.4.1 (same Phase): OffscreenTarget WebGPU needs `CreateTexture()` for offscreen canvas
  - Phase 2: `StagingUploader` WebGPU path uses `wgpu::Queue::WriteBuffer`
  - Phase 3a: render graph WebGPU executor uses render pass encoders

### Upstream Contracts

- T1a.3.2: `IDevice` virtual interface (all methods)
  - Source: `include/miki/rhi/IDevice.h`
- T1a.3.1: `WebGpuExternalContext { void* device; }`
  - Source: `include/miki/rhi/RhiTypes.h`

### Technical Direction

- **Dawn native C++**: link against Dawn's native C++ API (`dawn/native/DawnNative.h`). NOT the JS-style WebGPU C API for the initial implementation. Dawn provides both; native C++ is cleaner for desktop.
- **Headless creation**: `CreateOwned` creates `wgpu::Instance` → request null adapter → create device. This works without a window or GPU for basic CI validation.
- **External surface**: `WebGpuExternalContext` extended to optionally include `WGPUSurface` for swapchain. Phase 1b focuses on headless; surface support is stretch.
- **Push constant → UBO**: WebGPU has no push constants. Create a persistent 256B UBO at bind group 0, slot 0. `PushConstants()` does `wgpu::Queue::WriteBuffer` on this UBO.
- **Texture format subset**: WebGPU lacks BC compression on some platforms. Fallback to uncompressed. `SupportsFormat()` queries Dawn adapter capabilities.
- **No Emscripten**: WASM build is a stretch goal, not Phase 1b requirement.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `src/miki/rhi/webgpu/WebGpuDevice.h` | shared | **H** | Device class |
| Create | `src/miki/rhi/webgpu/WebGpuDevice.cpp` | internal | L | IDevice impl via Dawn |
| Create | `src/miki/rhi/webgpu/WgpuHandlePool.h` | shared | **M** | Handle mapping |
| Create | `src/miki/rhi/webgpu/WgpuHandlePool.cpp` | internal | L | Pool impl |
| Create | `src/miki/rhi/webgpu/CMakeLists.txt` | internal | L | Static lib |
| Create | `tests/unit/test_webgpu_device.cpp` | internal | L | Unit tests |
| Modify | `tests/unit/CMakeLists.txt` | internal | L | Add test target |
| Modify | `src/CMakeLists.txt` | internal | L | Add webgpu subdirectory |

## Steps

- [x] **Step 1**: Dawn dependency integration (CMake)
      **Files**: `CMakeLists.txt` (internal L)
      Add Dawn as third-party dependency (prebuilt or FetchContent). Create `miki_rhi_webgpu` target.
      **Acceptance**: Dawn headers found, library links
      `[verify: compile]`

- [x] **Step 2**: Create `WebGpuDevice.h` + `WgpuHandlePool.h`
      **Files**: `WebGpuDevice.h` (shared H), `WgpuHandlePool.h` (shared M)

      **Signatures** (`WebGpuDevice.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `WebGpuDevice::CreateFromExisting` | `(WebGpuExternalContext) -> Result<unique_ptr<IDevice>>` | `[[nodiscard]]` static |
      | `WebGpuDevice::CreateOwned` | `(DeviceConfig) -> Result<unique_ptr<IDevice>>` | `[[nodiscard]]` static |
      | All `IDevice` virtuals | (see `IDevice.h`) | override |

      **Acceptance**: compiles
      `[verify: compile]`

- [x] **Step 3**: Implement `WebGpuDevice.cpp`
      **Files**: `WebGpuDevice.cpp` (internal L), `WgpuHandlePool.cpp` (internal L)
      Dawn instance/adapter/device creation. Resource CRUD via wgpu API.
      Push constant UBO creation (256B, bind group 0 slot 0).
      **Acceptance**: device creates successfully (headless Dawn)
      `[verify: compile]`

- [x] **Step 4**: Write unit tests
      **Files**: `test_webgpu_device.cpp` (internal L)
      Tests use `CreateOwned` (Dawn headless) or `GTEST_SKIP` if Dawn unavailable.
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(WebGpuDevice, CreateOwnedReturnsValid)` | Positive | headless Dawn device creation | 3-4 |
| `TEST(WebGpuDevice, GetBackendTypeReturnsWebGPU)` | Positive | correct backend type | 3-4 |
| `TEST(WebGpuDevice, CapabilityTierIsTier3)` | Positive | reports Tier3_WebGPU | 3-4 |
| `TEST(WebGpuDevice, CreateTextureReturnsValid)` | Positive | texture creation via Dawn | 3-4 |
| `TEST(WebGpuDevice, CreateBufferReturnsValid)` | Positive | buffer creation via Dawn | 3-4 |
| `TEST(WebGpuDevice, DestroyTextureReleasesResource)` | State | destroy calls wgpu release | 3-4 |
| `TEST(WebGpuDevice, DestroyInvalidHandleSafe)` | Boundary | double-destroy safe | 3-4 |
| `TEST(WebGpuDevice, WaitIdleCompletes)` | Boundary | device tick + drain completes | 3-4 |
| `TEST(WebGpuDevice, PushConstantUBOCreated)` | State | 256B UBO exists after device creation | 3-4 |
| `TEST(WebGpuDevice, CreateFromExisting_NullDevice)` | Error | null device pointer → error | 3-4 |
| `TEST(WebGpuDevice, EndToEnd_CreateAndDestroy)` | **Integration** | full lifecycle | 1-4 |

## Design Decisions

*(Fill during implementation)*

## Implementation Notes

*(Fill during implementation)*
