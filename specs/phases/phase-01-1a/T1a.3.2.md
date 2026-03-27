# T1a.3.2 — IDevice + ICommandBuffer Interfaces

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: RHI Core — Injection Architecture
**Roadmap Ref**: `roadmap.md` L326, L339-372 — Injection-first `IDevice`, three creation paths, `ICommandBuffer`
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.3.1 | RhiTypes + Handle + ExternalContext + Format | Not Started | `Handle<Tag>`, `ExternalContext`, `DeviceConfig`, `Format`, `PipelineStage`, `TextureHandle`, `BufferHandle` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rhi/IDevice.h` | **public** | **H** | `IDevice` — abstract device interface. Factory methods, resource creation, swapchain import. The single most referenced interface in the entire project. |
| `include/miki/rhi/ICommandBuffer.h` | **public** | **H** | `ICommandBuffer` — abstract command recording interface. Barriers, bind, draw, dispatch, dynamic rendering. |
| `include/miki/rhi/RhiDescriptors.h` | **public** | **M** | `TextureDesc`, `BufferDesc`, `PipelineDesc`, `SamplerDesc`, `RenderingInfo` — creation descriptors |
| `tests/unit/test_idevice.cpp` | internal | L | Interface contract tests (using MockDevice in T1a.8.1) |

- **Error model**: All fallible methods return `Result<T>`. Device creation returns `Result<unique_ptr<IDevice>>`.
- **Thread safety**: `IDevice` is single-owner. Command buffers are not thread-safe — one per thread.
- **GPU constraints**: N/A (pure interface)
- **Invariants**: `IDevice` created via `CreateFromExisting` does NOT own the underlying API device. `CreateOwned` does. `Destroy()` releases miki-internal resources only.

### Downstream Consumers

- `IDevice.h` (**public**, heat **H**):
  - T1a.4.1: `IPipelineFactory` takes `IDevice&` for pipeline creation
  - T1a.5.1: `VulkanDevice` implements `IDevice`
  - T1a.6.1: `D3D12Device` implements `IDevice`
  - T1a.8.1: `MockDevice` implements `IDevice`
  - T1a.7.1: `OffscreenTarget` calls `IDevice::CreateTexture`
  - T1a.9.1: `FrameManager` uses `IDevice` for fence/semaphore
  - Phase 1b: GL/WebGPU devices implement `IDevice`
  - Phase 2+: Every rendering module calls `IDevice` methods
- `ICommandBuffer.h` (**public**, heat **H**):
  - T1a.5.2: `VulkanCommandBuffer` implements `ICommandBuffer`
  - T1a.6.2: `D3D12CommandBuffer` implements `ICommandBuffer`
  - T1a.12.1: Triangle demo records commands via `ICommandBuffer`
  - Phase 2+: All rendering uses `ICommandBuffer`

### Upstream Contracts

- T1a.3.1: `Handle<Tag>` (8B typed handles), `ExternalContext` (variant), `DeviceConfig`, `Format`, `PipelineStage`, `AccessFlags`, `TextureLayout`, `NativeImageHandle`

### Technical Direction

- **Injection-first**: `CreateFromExisting(ExternalContext)` is the primary path. `CreateOwned(DeviceConfig)` is convenience.
- **Shared device isolation**: When `CreateFromExisting` is used, miki creates own VMA allocator, descriptor pool, pipeline cache on the injected device. Does not take ownership.
- **Dynamic rendering**: `ICommandBuffer::BeginRendering(RenderingInfo)` — no `VkRenderPass`. Maps to `VK_KHR_dynamic_rendering` (Vulkan), render pass (D3D12), FBO bind (GL), render pass encoder (WebGPU).
- **Push constants**: `ICommandBuffer::PushConstants(stage, offset, size, data)` — native on Vulkan/D3D12 (root constants), emulated as UBO on GL/WebGPU.
- **Virtual interface**: Pure virtual `IDevice`/`ICommandBuffer`. Backends implement. Pimpl not needed here (virtual dispatch already provides polymorphism).

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rhi/IDevice.h` | **public** | **H** | Core interface — most referenced file in project |
| Create | `include/miki/rhi/ICommandBuffer.h` | **public** | **H** | Command recording interface |
| Create | `include/miki/rhi/RhiDescriptors.h` | **public** | **M** | Resource creation descriptors |
| Create | `tests/unit/test_idevice.cpp` | internal | L | Contract tests |

## Steps

- [x] **Step 1**: Define `IDevice` interface + resource creation descriptors
      **Files**: `IDevice.h` (**public** H), `RhiDescriptors.h` (**public** M)

      **Signatures** (`IDevice.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `IDevice::CreateFromExisting` | `static (ExternalContext) -> Result<unique_ptr<IDevice>>` | `[[nodiscard]]` |
      | `IDevice::CreateOwned` | `static (DeviceConfig) -> Result<unique_ptr<IDevice>>` | `[[nodiscard]]` |
      | `IDevice::CreateTexture` | `(TextureDesc const&) -> Result<TextureHandle>` | `[[nodiscard]]` virtual pure |
      | `IDevice::CreateBuffer` | `(BufferDesc const&) -> Result<BufferHandle>` | `[[nodiscard]]` virtual pure |
      | `IDevice::CreateGraphicsPipeline` | `(GraphicsPipelineDesc const&) -> Result<PipelineHandle>` | `[[nodiscard]]` virtual pure |
      | `IDevice::CreateComputePipeline` | `(ComputePipelineDesc const&) -> Result<PipelineHandle>` | `[[nodiscard]]` virtual pure |
      | `IDevice::CreateSampler` | `(SamplerDesc const&) -> Result<SamplerHandle>` | `[[nodiscard]]` virtual pure |
      | `IDevice::DestroyTexture` | `(TextureHandle) -> void` | virtual pure |
      | `IDevice::DestroyBuffer` | `(BufferHandle) -> void` | virtual pure |
      | `IDevice::DestroyPipeline` | `(PipelineHandle) -> void` | virtual pure |
      | `IDevice::ImportSwapchainImage` | `(NativeImageHandle) -> Result<TextureHandle>` | `[[nodiscard]]` virtual pure |
      | `IDevice::CreateCommandBuffer` | `() -> Result<unique_ptr<ICommandBuffer>>` | `[[nodiscard]]` virtual pure |
      | `IDevice::Submit` | `(ICommandBuffer&) -> Result<void>` | virtual pure |
      | `IDevice::WaitIdle` | `() -> void` | virtual pure |
      | `IDevice::GetCapabilities` | `() const noexcept -> GpuCapabilityProfile const&` | `[[nodiscard]]` virtual pure |
      | `IDevice::GetBackendType` | `() const noexcept -> BackendType` | `[[nodiscard]]` virtual pure |
      | `~IDevice` | | virtual default |

      **Signatures** (`RhiDescriptors.h`):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `TextureDesc` | `{ width:u32, height:u32, depth:u32=1, mipLevels:u32=1, arrayLayers:u32=1, format:Format, usage:TextureUsage, samples:u32=1 }` | — |
      | `BufferDesc` | `{ size:u64, usage:BufferUsage, memoryType:MemoryType }` | — |
      | `TextureUsage` | `enum class : u32 { Sampled=1<<0, Storage=1<<1, ColorAttachment=1<<2, DepthStencilAttachment=1<<3, TransferSrc=1<<4, TransferDst=1<<5 }` | Bitmask |
      | `BufferUsage` | `enum class : u32 { Vertex=1<<0, Index=1<<1, Uniform=1<<2, Storage=1<<3, Indirect=1<<4, TransferSrc=1<<5, TransferDst=1<<6 }` | Bitmask |
      | `MemoryType` | `enum class : u8 { GpuOnly, CpuToGpu, GpuToCpu }` | — |
      | `SamplerDesc` | `{ magFilter:Filter, minFilter:Filter, mipMapMode:MipMapMode, addressModeU/V/W:AddressMode, maxAnisotropy:f32, minLod:f32, maxLod:f32 }` | — |
      | `RenderingInfo` | `{ colorAttachments:span<RenderingAttachment>, depthAttachment:optional<RenderingAttachment>, renderArea:Rect2D }` | — |
      | `RenderingAttachment` | `{ texture:TextureHandle, loadOp:LoadOp, storeOp:StoreOp, clearValue:ClearValue }` | — |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | Namespace | `miki::rhi` |
      | No API includes | No `<vulkan/vulkan.h>`, `<d3d12.h>` etc. in public headers |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Define `ICommandBuffer` interface
      **Files**: `ICommandBuffer.h` (**public** H)

      **Signatures** (`ICommandBuffer.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `ICommandBuffer::Begin` | `() -> Result<void>` | virtual pure |
      | `ICommandBuffer::End` | `() -> Result<void>` | virtual pure |
      | `ICommandBuffer::BeginRendering` | `(RenderingInfo const&) -> void` | virtual pure |
      | `ICommandBuffer::EndRendering` | `() -> void` | virtual pure |
      | `ICommandBuffer::BindPipeline` | `(PipelineHandle) -> void` | virtual pure |
      | `ICommandBuffer::BindVertexBuffer` | `(u32 binding, BufferHandle, u64 offset) -> void` | virtual pure |
      | `ICommandBuffer::BindIndexBuffer` | `(BufferHandle, u64 offset, IndexType) -> void` | virtual pure |
      | `ICommandBuffer::PushConstants` | `(PipelineStage, u32 offset, u32 size, void const*) -> void` | virtual pure |
      | `ICommandBuffer::SetViewport` | `(Viewport const&) -> void` | virtual pure |
      | `ICommandBuffer::SetScissor` | `(Rect2D const&) -> void` | virtual pure |
      | `ICommandBuffer::Draw` | `(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) -> void` | virtual pure |
      | `ICommandBuffer::DrawIndexed` | `(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) -> void` | virtual pure |
      | `ICommandBuffer::Dispatch` | `(u32 groupX, u32 groupY, u32 groupZ) -> void` | virtual pure |
      | `ICommandBuffer::PipelineBarrier` | `(PipelineBarrierInfo const&) -> void` | virtual pure |
      | `ICommandBuffer::CopyBufferToTexture` | `(BufferHandle, TextureHandle, BufferTextureCopyInfo const&) -> void` | virtual pure |
      | `ICommandBuffer::CopyBuffer` | `(BufferHandle src, BufferHandle dst, BufferCopyInfo const&) -> void` | virtual pure |
      | `~ICommandBuffer` | | virtual default |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | Namespace | `miki::rhi` |
      | Viewport | `{ x:f32, y:f32, width:f32, height:f32, minDepth:f32, maxDepth:f32 }` |
      | Rect2D | `{ offset:int2, extent:uint2 }` |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 3**: Unit tests for interface contracts
      **Files**: `tests/unit/test_idevice.cpp` (internal L)
      Test: `IDevice` static factory returns correct backend type; `ICommandBuffer` records commands in order (validated via MockDevice in T1a.8.1 — initially test compilation only, functional tests added after MockDevice).
      **Acceptance**: tests compile and non-mock tests pass
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(IDevice, InterfaceCompiles)` | Build | Step 1 — interface definition valid | 1 |
| `TEST(RhiDescriptors, TextureDescDefault)` | Unit | Step 1 — default construction | 1 |
| `TEST(RhiDescriptors, BufferUsageBitmask)` | Unit | Step 1 — bitwise OR | 1 |
| `TEST(ICommandBuffer, InterfaceCompiles)` | Build | Step 2 — interface definition valid | 2 |

## Design Decisions

- **STATIC library**: `miki::rhi` converted from INTERFACE to STATIC since `IDevice.cpp` provides factory method stubs. Backends will add their `.cpp` files later.
- **Factory stubs return NotImplemented**: `CreateFromExisting`/`CreateOwned` return `ErrorCode::NotImplemented` until backends (T1a.5.1/6.1/8.1) are implemented. This allows compile + link verification now.
- **Forward-declared GpuCapabilityProfile**: Referenced by `IDevice::GetCapabilities` return type. Full definition deferred to T1a.3.3.
- **Added int2 to Types.h**: Needed by `Rect2D.offset`. Follows same pattern as existing `uint2`.
- **Extra descriptor types**: Added `PipelineBarrierInfo`, `TextureBarrier`, `BufferBarrier`, `BufferTextureCopyInfo`, `BufferCopyInfo`, `GraphicsPipelineDesc`, `ComputePipelineDesc`, `ShaderModuleDesc` — all needed by `ICommandBuffer` methods.
- **ClearValue as variant**: `std::variant<ClearColor, ClearDepthStencil>` — type-safe, avoids union UB.
- **Bitmask operators**: Provided `operator|` and `operator&` for `TextureUsage` and `BufferUsage` — enables `usage = Sampled | ColorAttachment` syntax.

## Implementation Notes

- Contract check: PASS (46/46 items)
- Build: 0 errors, miki_rhi.lib + 3 test executables
- Tests: 47/47 pass (11 IDevice + 16 RhiTypes + 17 Foundation + 3 Toolchain)
- No TODO/STUB/FIXME in task files (IDevice.cpp comment says "stubs" descriptively, not as a marker)
