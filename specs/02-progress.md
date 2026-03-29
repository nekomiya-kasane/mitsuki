# RHI Backend Implementation Progress

> Last updated: 2026-03-28

## Vulkan Backend — COMPLETE

- 9 .cpp + 2 .h, 100% spec coverage
- Zero stubs, zero TODO, zero assert(false)

## D3D12 Backend — COMPLETE (with deferred items)

- 8 .cpp + 2 .h, 100% API surface implemented
- CMake: `miki_d3d12.cmake`, default ON on Windows

### Deferred Items

| Item                                                                | Reason                                                                                                                                                                                   | Priority | Resolution Phase                                                      |
| ------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- | --------------------------------------------------------------------- |
| `ExecuteIndirect` (DrawIndirect/DispatchIndirect/MeshTasksIndirect) | Requires pre-created `ID3D12CommandSignature` per draw type. Vulkan has native `vkCmdDrawIndirect`; D3D12 requires explicit signature describing indirect argument layout.               | HIGH     | Phase 3 (RenderGraph) — create command signatures at device init      |
| `CmdBlitTexture`                                                    | D3D12 has no native image blit. Requires fullscreen quad + shader or compute pass.                                                                                                       | MEDIUM   | Phase 4 (Post-processing) — implement blit utility pipeline           |
| `CmdFillBuffer`                                                     | D3D12 has no `CmdFillBuffer`. Use `ClearUnorderedAccessViewUint` on UAV or compute shader fill.                                                                                          | MEDIUM   | Phase 3 — implement via UAV clear path                                |
| `CmdClearColorTexture` / `CmdClearDepthStencil` (standalone)        | Requires creating temporary RTV/DSV descriptors for arbitrary textures. Currently only works within `CmdBeginRendering`.                                                                 | LOW      | Phase 3 — add descriptor cache for on-demand RTV/DSV creation         |
| Pipeline Library split compilation (`LinkGraphicsPipeline`)         | D3D12 Pipeline State Streams sub-object merging via `ID3D12Device2::CreatePipelineState`.                                                                                                | LOW      | Phase 5 (Material System) — when PSO permutation explosion demands it |
| Mesh shader PSO                                                     | Current path uses `CreateGraphicsPipelineState` with VS slot. Proper MS PSO requires `ID3D12Device2::CreatePipelineState` with `CD3DX12_PIPELINE_MESH_STATE_STREAM`.                     | MEDIUM   | Phase 4 (Mesh pipeline) — when mesh shader rendering path is active   |
| Legacy ResourceBarrier fallback                                     | Current implementation uses Enhanced Barriers exclusively (`ID3D12GraphicsCommandList7::Barrier`). Fallback to legacy `ResourceBarrier` for pre-Agility SDK drivers not yet implemented. | LOW      | Only needed for Windows 10 21H2 and earlier                           |
| `CmdSetDepthBias` dynamic                                           | D3D12 depth bias is PSO state. Dynamic depth bias requires `ID3D12GraphicsCommandList9::OMSetDepthBias` (Windows 11 24H2+).                                                              | LOW      | Phase 8 (Shadow mapping) — when dynamic depth bias is needed          |

# 文件 问题 严重度 说明

1 D3D12CommandBuffer.cpp:303-317 CmdDrawIndirect 是空实现 (no-op) 🔴 高 缺 ID3D12CommandSignature，整个 indirect draw 路径无效
2 D3D12CommandBuffer.cpp:319-330 CmdDrawIndexedIndirect 空实现 🔴 高 同上
3 D3D12CommandBuffer.cpp:332-346 CmdDrawIndexedIndirectCount 空实现 🔴 高 同上
4 D3D12CommandBuffer.cpp:352-356 CmdDrawMeshTasksIndirect 空实现 🔴 高 需要 mesh shader command signature
5 D3D12CommandBuffer.cpp:358-363 CmdDrawMeshTasksIndirectCount 空实现 🔴 高 同上
6 D3D12CommandBuffer.cpp:373-380 CmdDispatchIndirect 空实现 🔴 高 需要 compute command signature
7 D3D12CommandBuffer.cpp:498-503 CmdBlitTexture 空实现 🟡 中 D3D12 无原生 blit，需 compute/fullscreen quad
8 D3D12CommandBuffer.cpp:505-509 CmdFillBuffer 空实现 🟡 中 需要 ClearUnorderedAccessViewUint
9 D3D12CommandBuffer.cpp:511-520 CmdClearColorTexture 空实现 🟡 中 有 RTV 但未使用 ClearRenderTargetView
10 D3D12CommandBuffer.cpp:522-532 CmdClearDepthStencil 空实现 🟡 中 有 DSV 但未使用 ClearDepthStencilView
11 D3D12CommandBuffer.cpp:673-679 CmdSetDepthBias 空实现 🟢 低 D3D12 PSO state，需 ID3D12GraphicsCommandList9
12 D3D12CommandBuffer.cpp:727-730 CmdSetShadingRateImage 空实现 🟡 中 需要从 view 获取 resource 指针
13 D3D12Pipelines.cpp:165-169 Mesh shader PSO 使用 VS slot 占位 🔴 高 需要 Pipeline State Stream API
14 D3D12Pipelines.cpp:468-486 CreatePipelineLibraryPart/LinkGraphicsPipeline 占位 🟡 中 Split compilation 未实现
15 D3D12Pipelines.cpp:392-404 RT pipeline 不存储 StateObject 🔴 高 pso 为 null，无法 dispatch RT
16 D3D12CommandBuffer.cpp:270 CmdBindVertexBuffer StrideInBytes=0 🔴 高 必须从 pipeline 的 vertex input state 获取 stride

WebGPU:

Key Architecture Decisions
Push constants: 256B UBO at @group(0) @binding(0), user bindings shifted to group(N+1)
Command recording: Deferred via WGPUCommandEncoder, finalized at End()
Fence: Emulated via wgpuQueueOnSubmittedWorkDone callback + monotonic serial
Barriers: No-op (Dawn handles transitions implicitly)
Memory: API-managed, no introspection (GetMemoryStats returns zeros)
Bind groups: Immutable — UpdateDescriptorSet recreates the WGPUBindGroup
Surface: Modern configure/unconfigure model (not deprecated WGPUSwapChain)
Known Deferred Items
CmdBlitTexture — requires fullscreen quad shader
CmdFillBuffer(non-zero) — requires compute shader
CmdClearColor/DepthStencil — WebGPU only supports clear via render pass loadOp
CmdExecuteSecondary — render bundles not yet wired
Linux/macOS surface — only Win32 HWND + Emscripten canvas implemented
Dawn prebuilt — needs to be downloaded to third_party/webgpu/dawn/prebuilt/

---

# RHI Architecture Audit (2026-03-29)

> Full audit of Layer 2 RHI + supporting infrastructure.
> Scope: Device, CommandBuffer, IPipelineFactory, Swapchain/Surface, Descriptors, Sync, HandlePool, RenderGraph.

## Fix Plan — Current Phase

Fixes are ordered by dependency chain. H2 must land before H1; H1+H3 before M8.

### Wave 1: Core Ownership & Factory (H1, H2, H3, M2)

**H2 + H1 + M2: OwnedDevice + DeviceFactory**

Goal: `DeviceHandle` becomes a non-owning view (current design preserved). A new `OwnedDevice` class owns the concrete backend device via `unique_ptr<void, Deleter>`. A free function `CreateDevice(DeviceDesc)` replaces per-demo switch-case.

Files to create/modify:

- NEW `include/miki/rhi/DeviceFactory.h` — `OwnedDevice` class + `CreateDevice()` free function
- NEW `src/miki/rhi/DeviceFactory.cpp` — switch on `DeviceDesc::backend`, create concrete device, return `OwnedDevice`
- MODIFY `include/miki/rhi/Device.h` — `DeviceDesc` gains `enableValidation`, `enableGpuCapture` (already has them); remove or keep as-is since factory will consume it
- MODIFY `cmake/targets/miki_rhi.cmake` — add `DeviceFactory.cpp` to sources

Design:

```cpp
class OwnedDevice {
public:
    ~OwnedDevice(); // calls WaitIdle + destroy
    OwnedDevice(OwnedDevice&&) noexcept;
    auto operator=(OwnedDevice&&) noexcept -> OwnedDevice&;
    [[nodiscard]] auto GetHandle() const noexcept -> DeviceHandle; // non-owning view
    [[nodiscard]] auto IsValid() const noexcept -> bool;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

[[nodiscard]] auto CreateDevice(const DeviceDesc& desc) -> core::Result<OwnedDevice>;
```

**H3: CommandList Factory**

Goal: `DeviceBase` gains `AcquireCommandList(QueueType) -> RhiResult<CommandListHandle>` that internally creates the backend command buffer object AND initializes the recordable CommandBuffer, returning a ready-to-use type-erased handle.

Files to modify:

- MODIFY `include/miki/rhi/Device.h` — add `AcquireCommandList` / `ReleaseCommandList` to `DeviceBase`
- MODIFY each backend `XxxDevice.h` — add `AcquireCommandListImpl` / `ReleaseCommandListImpl`
- MODIFY each backend `XxxDevice.cpp` or new `XxxCommandBuffer.cpp` section — implement the factory

Each backend's impl:

- Vulkan: allocate from VkCommandPool, create VulkanCommandBuffer on internal pool, return handle
- D3D12: allocate ID3D12GraphicsCommandList + allocator, create D3D12CommandBuffer, return handle
- OpenGL: create OpenGLCommandBuffer, return handle
- WebGPU: create WebGPUCommandBuffer, return handle

The concrete CommandBuffer objects are stored in a per-device arena (e.g., `std::vector<std::unique_ptr<XxxCommandBuffer>>` or a fixed pool).

### Wave 2: Type Erasure Completeness (H4, M1)

**H4: const Dispatch on CommandListHandle**

Files to modify:

- MODIFY `include/miki/rhi/CommandBuffer.h` — add `const` overload of `Dispatch`, mirroring `DeviceHandle`'s pattern

**M1: IPipelineFactory return FeatureNotSupported**

Files to modify:

- MODIFY `src/miki/rhi/MainPipelineFactory.cpp` — stub methods return `std::unexpected(RhiError::FeatureNotImplemented)` instead of `PipelineHandle{}`
- MODIFY `src/miki/rhi/CompatPipelineFactory.cpp` — same

### Wave 3: RenderSurface Correctness (H5, L4)

**H5: GetCapabilities real query**

Goal: Add `DeviceBase::GetSurfaceCapabilities(NativeWindowHandle)` that queries the backend (e.g., `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`). RenderSurface delegates to this.

Files to modify:

- MODIFY `include/miki/rhi/Device.h` — add `GetSurfaceCapabilities` to `DeviceBase`
- MODIFY each backend — implement `GetSurfaceCapabilitiesImpl`
- MODIFY `src/miki/rhi/RenderSurface.cpp` — `GetCapabilities()` delegates to device

**L4: Format validation in Configure**

Files to modify:

- MODIFY `src/miki/rhi/RenderSurface.cpp` — `ResolveSwapchainDesc` queries `formatSupport` table before overriding format

### Wave 4: HandlePool Hardening (M3, M4)

**M3: Dynamic capacity**

Replace `std::array<Slot, Capacity>` with `std::vector<Slot>` initialized to `Capacity` size. Template `Capacity` becomes default constructor argument. This preserves O(1) access while allowing runtime sizing.

Files to modify:

- MODIFY `include/miki/rhi/Handle.h` — `HandlePool` template loses `Capacity` template param, gains constructor `HandlePool(size_t capacity)`
- MODIFY all backend `XxxDevice.h` — update pool member declarations

**M4: Atomic acquire/release**

Files to modify:

- MODIFY `include/miki/rhi/Handle.h` — `Slot::alive` and `Slot::generation` become `std::atomic<bool>` / `std::atomic<uint16_t>` with `memory_order_acquire` on read, `memory_order_release` on write

### Wave 5: Enum Unification (M5, M6)

**M5: ShaderStage unification**

Files to modify:

- MODIFY `include/miki/shader/ShaderTypes.h` — remove `shader::ShaderStage`, replace with `using ShaderStage = rhi::ShaderStage`
- MODIFY `src/miki/shader/SlangCompiler.cpp` — adapt to unified enum
- MODIFY demo — remove namespace disambiguation

**M6: Bitmask enum migration to EnumFlags**

Approach: Add `MIKI_ENABLE_ENUM_FLAGS()` for all bitmask enums in `RhiEnums.h`, or directly use `EnumFlags<E>` as the field type in desc structs. Phase 1: add missing operators (`~`, `|=`, `&=`) via the macro. Phase 2 (optional): migrate desc struct fields to `EnumFlags<E>`.

Files to modify:

- MODIFY `include/miki/rhi/RhiEnums.h` — replace manual `operator|`/`operator&` with `MIKI_ENABLE_ENUM_FLAGS`

### Wave 6: PipelineCache + Mock + Convenience (L1, L2, L3)

**L1: PipelineCache ↔ PipelineCacheHandle**

Files to modify:

- MODIFY `include/miki/rhi/PipelineCache.h` — store `PipelineCacheHandle` internally, expose getter
- MODIFY `src/miki/rhi/PipelineCache.cpp` — `Load()` calls `device.CreatePipelineCache(blob)` to get handle

**L2: MockDevice + MockCommandBuffer**

Files to create:

- NEW `include/miki/rhi/backend/MockDevice.h`
- NEW `include/miki/rhi/backend/MockCommandBuffer.h`
- NEW `src/miki/rhi/mock/MockDevice.cpp`
- NEW `src/miki/rhi/mock/MockCommandBuffer.cpp`

All methods are no-op. HandlePool allocation works normally but native handles are null. This enables headless testing without any GPU.

**L3: DeviceHandle convenience**

Files to modify:

- MODIFY `include/miki/rhi/Device.h` — add `GetBackendName()` and `GetDeviceName()` to `DeviceHandle`

### Wave 7: Demo Refactor (M8)

After H1/H2/H3 land:

- MODIFY `demos/rhi/rhi_triangle_demo.cpp` — replace `DeviceHolder` + `CreateDevice()` + static CommandBuffer with `OwnedDevice` + `AcquireCommandList`

---

## Deferred to Future Phases

The following defects are **not fixed in this phase** because they require Layer 3+ infrastructure or FrameManager implementation.

### H6: RenderSurface sync primitives not created

**Status**: DEFERRED to FrameManager implementation phase
**Reason**: Sync primitive lifecycle (create/destroy per swapchain image) is FrameManager's responsibility. RenderSurface stores the handles but FrameManager populates them at `Create()` time. This is the correct layering — fixing it requires implementing FrameManager first.
**Impact**: Vulkan/D3D12 swapchain acquire/present will crash without valid semaphores. Demo currently bypasses RenderSurface entirely (uses raw device API).

### H7: Layer 3 infrastructure (BindlessTable, StagingRing, MemBudget, RenderGraph)

**Status**: DEFERRED — these are Phase 3/4/5 deliverables per roadmap
**Components missing**:

| Component         | Layer | Roadmap Phase           | Dependency                     |
| ----------------- | ----- | ----------------------- | ------------------------------ |
| BindlessTable     | L3    | Phase 3 (Resource)      | Device + Descriptor system     |
| StagingRing       | L3    | Phase 3 (Resource)      | Device + Buffer + FrameManager |
| ReadbackRing      | L3    | Phase 3 (Resource)      | Device + Buffer + FrameManager |
| MemBudget         | L3    | Phase 3 (Resource)      | Device + VMA/D3D12MA           |
| ResidencyFeedback | L3    | Phase 6 (Streaming)     | MemBudget + Sparse binding     |
| RenderGraph       | L5    | Phase 5 (Core Pipeline) | All of L2 + L3                 |

### M7: FrameManager implementation missing

**Status**: DEFERRED to Phase 3
**Reason**: FrameManager.h is fully designed but no .cpp exists. It depends on:

- OwnedDevice (H1/H2 — this phase fixes)
- RenderSurface sync primitives (H6 — deferred)
- DeferredDestructor (header exists, impl may be partial)
- StagingRing / ReadbackRing (H7 — deferred)

FrameManager implementation is the bridge between Layer 2 (RHI) and Layer 3 (Resource). It should be the first deliverable of Phase 3.
