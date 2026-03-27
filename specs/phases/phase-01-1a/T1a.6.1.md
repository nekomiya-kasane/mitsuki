# T1a.6.1 — D3D12Device (CreateFromExisting + CreateOwned + D3D12MA)

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: D3D12 Backend
**Roadmap Ref**: `roadmap.md` L329 — `D3D12Device` wrapping injected `ID3D12Device`, D3D12MA, mesh shader, DXR 1.1
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.3.2 | IDevice + ICommandBuffer | Not Started | `IDevice` interface, `Result<T>`, handle types |
| T1a.3.3 | GpuCapabilityProfile | Not Started | `GpuCapabilityProfile`, `CapabilityTier` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `src/miki/rhi/d3d12/D3D12Device.h` | shared | **M** | `D3D12Device` — `IDevice` implementation for D3D12. CreateFromExisting + CreateOwned. D3D12MA. |
| `src/miki/rhi/d3d12/D3D12Device.cpp` | internal | L | Implementation: device init, D3D12MA, resource creation, descriptor heaps |
| `src/miki/rhi/d3d12/D3D12HandlePool.h` | shared | **M** | `D3D12HandlePool` — maps `Handle<Tag>` to `ID3D12Resource*` etc. |
| `tests/unit/test_d3d12_device.cpp` | internal | L | D3D12Device tests (GTEST_SKIP on non-Windows/no-GPU) |

- **Error model**: `Result<T>`. HRESULT mapped to `ErrorCode`.
- **Thread safety**: `D3D12Device` single-owner. D3D12MA is thread-safe internally.
- **GPU constraints**: Own `ID3D12CommandAllocator` per frame. Shader-visible CBV/SRV/UAV heap (bindless). Sampler heap.
- **Invariants**: `CreateFromExisting` does NOT own `ID3D12Device`. `CreateOwned` creates and owns device. Conditional build: `MIKI_BUILD_D3D12=ON` (Windows-only).

### Downstream Consumers

- `D3D12Device.h` (shared, heat **M**):
  - T1a.6.2 (same Phase): `D3D12CommandBuffer` uses device for native handle access
  - T1a.7.1 (same Phase): `OffscreenTarget` D3D12 path
  - T1a.9.2 (same Phase): `GlfwBootstrap` creates D3D12 device on Windows

### Upstream Contracts

- T1a.3.2: `IDevice` interface, `TextureDesc`, `BufferDesc`, `NativeImageHandle`
- T1a.3.3: `GpuCapabilityProfile` — populated from `D3D12_FEATURE_DATA_D3D12_OPTIONS7`

### Technical Direction

- **D3D12MA**: AMD D3D12 Memory Allocator, source-compiled from `third_party/d3d12ma/`
- **Descriptor heaps**: Shader-visible CBV/SRV/UAV heap for bindless. Separate sampler heap. Miki owns its heaps (isolation from host).
- **Push constants -> root constants**: 32 DWORD = 128B root constants. Vulkan's 256B push constants reduced to 128B on D3D12.
- **Mesh shader**: `D3D12_FEATURE_D3D12_OPTIONS7` `MeshShaderTier` detection. Maps to `Tier1_Full`.
- **Enhanced barriers**: Use `ID3D12GraphicsCommandList::ResourceBarrier` with enhanced barriers when available.
- **Timeline fence**: `ID3D12Fence` for GPU-CPU synchronization.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `src/miki/rhi/d3d12/D3D12Device.h` | shared | **M** | IDevice impl for D3D12 |
| Create | `src/miki/rhi/d3d12/D3D12Device.cpp` | internal | L | Implementation |
| Create | `src/miki/rhi/d3d12/D3D12HandlePool.h` | shared | **M** | Handle mapping |
| Create | `src/miki/rhi/d3d12/D3D12Utils.h` | internal | L | HRESULT mapping, format conversion |
| Create | `tests/unit/test_d3d12_device.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: D3D12Device class + CreateFromExisting + CreateOwned + D3D12MA
      **Files**: `D3D12Device.h` (shared M), `D3D12Device.cpp` (internal L)
      Implement `IDevice`. D3D12MA init. Feature detection (`D3D12_FEATURE_DATA_D3D12_OPTIONS7`). Populate `GpuCapabilityProfile`. Descriptor heap creation.
      **Acceptance**: compiles on Windows with `MIKI_BUILD_D3D12=ON`
      `[verify: compile]`

- [x] **Step 2**: D3D12HandlePool + resource creation
      **Files**: `D3D12HandlePool.h` (shared M), `D3D12Device.cpp` (extend)
      Implement `CreateTexture`, `CreateBuffer`, `DestroyTexture`, `DestroyBuffer`, `ImportSwapchainImage` (from `ID3D12Resource*`).
      **Acceptance**: resource lifecycle correct
      `[verify: compile]`

- [x] **Step 3**: Unit tests
      **Files**: `tests/unit/test_d3d12_device.cpp` (internal L)
      Cover: CreateOwned, capability profile, resource lifecycle, handle generation. GTEST_SKIP on non-Windows/no-GPU.
      **Acceptance**: tests pass on Windows D3D12-capable machine
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(D3D12Device, CreateOwnedSucceeds)` | Unit | Step 1 | 1 |
| `TEST(D3D12Device, CapabilityTier1Full)` | Unit | Step 1 | 1 |
| `TEST(D3D12Device, CreateTextureLifecycle)` | Unit | Step 2 | 2 |
| `TEST(D3D12Device, ImportSwapchainImage)` | Unit | Step 2 | 2 |

## Design Decisions

- **D3D12MA as STATIC library**: D3D12MA is NOT header-only (unlike VMA). Changed `third_party/CMakeLists.txt` from INTERFACE to STATIC, compiling `D3D12MemAlloc.cpp`. `D3D12MA_USING_DIRECTX_HEADERS` set as PUBLIC define.
- **ComPtr for COM**: Use `Microsoft::WRL::ComPtr` for COM ref-counting. Required adding COCA sysroot `winrt` include path for `wrl/client.h`.
- **CreateFromExisting AddRef**: When wrapping external device, we `AddRef` on the injected `ID3D12Device` / `IDXGIFactory6` / `ID3D12CommandQueue` so our `ComPtr` destructor doesn't prematurely release them.
- **Fence-based WaitIdle**: `ID3D12Fence` + `WaitForSingleObject` for GPU-CPU sync. Fence value monotonically incremented.
- **D3D12DeviceFactory.h**: Dispatch bridge pattern matching `VulkanDeviceFactory.h` — breaks circular dependency between `miki::rhi` and `miki::rhi_d3d12`.
- **Handle pool void* allocation**: `D3D12HandlePool::Entry::allocation` stored as `void*` to avoid D3D12MA include in the header. Cast to `D3D12MA::Allocation*` at use site.
- **Root constants = 128B**: `maxPushConstantSize` set to 128 (32 DWORDs), matching D3D12 root constant limits.

## Implementation Notes

- **Files created**: `D3D12Device.h`, `D3D12Device.cpp`, `D3D12HandlePool.h`, `D3D12Utils.h`, `D3D12DeviceFactory.h`, `d3d12/CMakeLists.txt`, `test_d3d12_device.cpp`
- **Build**: 0 compilation errors on COCA toolchain (Clang 21.1.8 C++23). D3D12MA generates warnings (third-party, suppressed).
- **Tests**: 10/10 passed — CreateOwned, CreateFromExisting null, CapabilityProfile, TextureLifecycle, BufferLifecycle, HandleGeneration, ImportSwapchainNull, StubMethods, WaitIdle, ZeroSizeBuffer.
- **Both build paths verified**: `debug-d3d12` (D3D12 only) and `debug-vulkan` (Vulkan only) both compile with 0 errors.
