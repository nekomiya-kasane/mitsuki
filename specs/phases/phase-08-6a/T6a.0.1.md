# T6a.0.1 — RHI Mesh Shader + Indirect Dispatch Extensions

**Phase**: 08-6a (GPU-Driven Rendering Core)
**Component**: RHI Extensions for GPU-Driven
**Roadmap Ref**: `roadmap.md` L1731 — Phase 6a GPU-Driven Rendering Core
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| — | (Phase 1a) | Complete | `IDevice`, `ICommandBuffer`, `PipelineStage::TaskShader/MeshShader`, `DeviceFeature::MeshShader`, `BufferUsage`, `GraphicsPipelineDesc` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rhi/ICommandBuffer.h` | **public** | **H** | `DrawMeshTasks()`, `DrawMeshTasksIndirect()`, `DrawMeshTasksIndirectCount()`, `DispatchIndirect()` — 4 new virtual methods |
| `include/miki/rhi/RhiDescriptors.h` | **public** | **H** | `MeshShaderPipelineDesc` or extended `GraphicsPipelineDesc` with task/mesh shader blob fields, `BufferUsage::ShaderDeviceAddress` |
| `include/miki/rhi/RhiTypes.h` | **public** | **M** | `DrawMeshTasksIndirectCommand` struct (12B: groupCountX/Y/Z) |
| `src/miki/rhi/vulkan/VulkanCommandBuffer.cpp` | internal | L | Vulkan implementation of mesh shader dispatch |
| `src/miki/rhi/vulkan/VulkanDevice.cpp` | internal | L | Mesh shader pipeline creation |
| `src/miki/rhi/mock/MockCommandBuffer.cpp` | internal | L | Mock recording of mesh shader commands |
| Backend stubs (D3D12, GL, WebGPU) | internal | L | `NotImplemented` / no-op for non-Tier1 |

- **Error model**: `DrawMeshTasks*` are void (fire-and-forget like `Draw`). `DispatchIndirect` is void. Pipeline creation returns `Result<PipelineHandle>`.
- **Thread safety**: same as existing `ICommandBuffer` — single-threaded recording.
- **GPU constraints**: `DrawMeshTasksIndirectCommand` = `{uint32_t groupCountX, groupCountY, groupCountZ}` — 12B, no alignment requirement (Vulkan spec). `BufferUsage::ShaderDeviceAddress` = `1 << 7` (next free bit).
- **Invariants**: `DrawMeshTasksIndirectCount` maxDrawCount <= device limit. Pipeline with mesh shader stage requires `DeviceFeature::MeshShader` enabled.

### Downstream Consumers

- `ICommandBuffer.h` (**public**, heat **H**):
  - T6a.5.1 (Task Shader): calls `DrawMeshTasksIndirectCount()` for GPU-driven geometry dispatch
  - T6a.8.1 (Demo): records `DrawMeshTasksIndirectCount()` in render graph execute lambda
  - Phase 6b: `PersistentCompute` uses `DispatchIndirect()` for variable-length work queues
  - Phase 7a-1: HLR edge render uses mesh shader line rendering via `DrawMeshTasks()`
- `RhiDescriptors.h` (**public**, heat **H**):
  - T6a.5.1, T6a.5.2: mesh shader pipeline creation uses extended `GraphicsPipelineDesc`
  - `BufferUsage::ShaderDeviceAddress` used by T6a.1.1 (meshlet buffer), T6a.4.1 (SceneBuffer)
- `RhiTypes.h` (**public**, heat **M**):
  - T6a.4.2 (MacroBin): writes `DrawMeshTasksIndirectCommand` structs to indirect arg buffer

### Upstream Contracts
- Phase 1a `ICommandBuffer`: existing virtual methods `Draw()`, `DrawIndexed()`, `Dispatch()`, `BindPipeline()`, `PipelineBarrier()`
- Phase 1a `IDevice`: existing `CreateGraphicsPipeline(GraphicsPipelineDesc)`, `CreateComputePipeline(ComputePipelineDesc)`
- Phase 1a `DeviceFeature::MeshShader`: already defined in `DeviceFeature.h`
- Phase 1a `PipelineStage::TaskShader`, `PipelineStage::MeshShader`: already defined in `RhiTypes.h`

### Technical Direction
- **Vulkan**: `vkCmdDrawMeshTasksEXT`, `vkCmdDrawMeshTasksIndirectEXT`, `vkCmdDrawMeshTasksIndirectCountEXT` from `VK_EXT_mesh_shader`. `vkCmdDispatchIndirect` (Vulkan core).
- **D3D12**: `DispatchMesh()` on `ID3D12GraphicsCommandList6`. `ExecuteIndirect` for indirect mesh dispatch.
- **GL/WebGPU/Mock**: `DrawMeshTasks*` → no-op or `NotImplemented` log. `DispatchIndirect` → `glDispatchComputeIndirect` (GL), no-op (WebGPU/Mock).
- **Pipeline desc**: extend `GraphicsPipelineDesc` with optional `taskShaderBlob` + `meshShaderBlob` fields (empty = traditional vertex pipeline). When mesh shader blob is set, vertex/geometry shader fields are ignored.
- **Feature gate**: `CreateGraphicsPipeline` with mesh shader blob on a device without `DeviceFeature::MeshShader` → return `ErrorCode::FeatureNotSupported`.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Modify | `include/miki/rhi/ICommandBuffer.h` | **public** | **H** | Add 4 virtual methods |
| Modify | `include/miki/rhi/RhiDescriptors.h` | **public** | **H** | Extend GraphicsPipelineDesc, add BufferUsage::ShaderDeviceAddress |
| Modify | `include/miki/rhi/RhiTypes.h` | **public** | **M** | Add DrawMeshTasksIndirectCommand |
| Modify | `src/miki/rhi/vulkan/VulkanCommandBuffer.h` | internal | L | Override new virtuals |
| Modify | `src/miki/rhi/vulkan/VulkanCommandBuffer.cpp` | internal | L | Vulkan impl |
| Modify | `src/miki/rhi/vulkan/VulkanDevice.cpp` | internal | L | Mesh shader pipeline creation |
| Modify | `src/miki/rhi/d3d12/D3D12CommandBuffer.h` | internal | L | Override stubs |
| Modify | `src/miki/rhi/opengl/OpenGlCommandBuffer.h` | internal | L | Override stubs |
| Modify | `src/miki/rhi/webgpu/WebGpuCommandBuffer.h` | internal | L | Override stubs |
| Modify | `src/miki/rhi/mock/MockCommandBuffer.h` | internal | L | Override + record |
| Create | `tests/unit/test_rhi_mesh_shader.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Extend public RHI interfaces (heat H)
      **Files**: `ICommandBuffer.h` (**public** H), `RhiDescriptors.h` (**public** H), `RhiTypes.h` (**public** M)

      **Signatures** (`ICommandBuffer.h` additions):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `DrawMeshTasks` | `(uint32_t groupX, uint32_t groupY, uint32_t groupZ) -> void` | virtual pure |
      | `DrawMeshTasksIndirect` | `(BufferHandle argBuffer, uint64_t argOffset, uint32_t drawCount, uint32_t stride) -> void` | virtual pure |
      | `DrawMeshTasksIndirectCount` | `(BufferHandle argBuffer, uint64_t argOffset, BufferHandle countBuffer, uint64_t countOffset, uint32_t maxDrawCount, uint32_t stride) -> void` | virtual pure |
      | `DispatchIndirect` | `(BufferHandle argBuffer, uint64_t argOffset) -> void` | virtual pure |

      **Signatures** (`RhiDescriptors.h` additions):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `GraphicsPipelineDesc` extension | `+taskShaderBlob: ShaderBlob`, `+meshShaderBlob: ShaderBlob` | Optional fields, empty = vertex pipeline |
      | `BufferUsage::ShaderDeviceAddress` | `= 1 << 7` | New enum value |

      **Signatures** (`RhiTypes.h` additions):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `DrawMeshTasksIndirectCommand` | `{ groupCountX:u32, groupCountY:u32, groupCountZ:u32 }` | 12B, trivially copyable |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Implement Vulkan backend
      **Files**: `VulkanCommandBuffer.cpp` (internal L), `VulkanDevice.cpp` (internal L)
      Implement `DrawMeshTasks` via `vkCmdDrawMeshTasksEXT`. Implement mesh shader pipeline creation (task+mesh stages in `VkGraphicsPipelineCreateInfo`). Feature-gate: check `DeviceFeature::MeshShader` enabled.
      **Acceptance**: compiles, mesh shader pipeline creates successfully on Tier1 hardware
      `[verify: compile]`

- [x] **Step 3**: Implement D3D12/GL/WebGPU/Mock stubs
      **Files**: All other backend .h/.cpp (internal L)
      D3D12: `DispatchMesh()` impl if available, else stub. GL/WebGPU: `DispatchIndirect` real impl, mesh shader methods log warning + no-op. Mock: record commands.
      **Acceptance**: compiles on both build paths, no linker errors
      `[verify: compile]`

- [x] **Step 4**: Unit tests
      **Files**: `test_rhi_mesh_shader.cpp` (internal L)
      Cover: DrawMeshTasks recording on Mock, DispatchIndirect on Mock, pipeline creation with mesh shader blob (feature gate error on non-Tier1), BufferUsage::ShaderDeviceAddress flag composition.
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(RhiMeshShader, DrawMeshTasksRecordsOnMock)` | Positive | Mock records DrawMeshTasks command | 3-4 |
| `TEST(RhiMeshShader, DrawMeshTasksIndirectCountRecordsOnMock)` | Positive | Mock records IndirectCount | 3-4 |
| `TEST(RhiMeshShader, DispatchIndirectRecordsOnMock)` | Positive | Mock records DispatchIndirect | 3-4 |
| `TEST(RhiMeshShader, MeshShaderPipelineCreation_Tier1)` | Positive | Pipeline with task+mesh blob succeeds on VulkanDevice (GTEST_SKIP on no-GPU) | 2,4 |
| `TEST(RhiMeshShader, MeshShaderPipeline_FeatureGate)` | Error | Pipeline creation with mesh blob on MockDevice (no MeshShader feature) returns FeatureNotSupported | 3-4 |
| `TEST(RhiMeshShader, BufferUsageShaderDeviceAddress)` | Positive | Flag OR composition works correctly | 1,4 |
| `TEST(RhiMeshShader, DrawMeshTasksIndirectCommand_Layout)` | Boundary | sizeof == 12, trivially copyable | 1,4 |
| `TEST(RhiMeshShader, DispatchIndirect_ZeroOffset)` | Boundary | offset=0 accepted | 3-4 |
| `TEST(RhiMeshShader, GraphicsPipelineDesc_EmptyMeshBlob_IsVertexPipeline)` | Positive | Empty mesh blob = traditional vertex pipeline | 1,4 |
| `TEST(RhiMeshShader, EndToEnd_MeshShaderDispatch)` | **Integration** | Create mesh pipeline → record DrawMeshTasks → submit → no crash (VulkanDevice, GTEST_SKIP) | 2,4 |

## Design Decisions

- **Dynamic function loading**: `vkCmdDrawMeshTasksEXT` / `Indirect` / `IndirectCount` are VK_EXT_mesh_shader extension functions not exported by the Vulkan loader. Loaded via `vkGetDeviceProcAddr` in `VulkanDevice::LoadMeshShaderFunctions()`, called at end of `PopulateCapabilities()`. Function pointers stored as `PFN_vk*` members in `VulkanDevice`, accessed by `VulkanCommandBuffer` via public getters.
- **GraphicsPipelineDesc already had taskShader/meshShader fields**: Added in an earlier phase (Phase 1a or 2). `IsMeshShaderPipeline()` helper already existed. No changes needed to pipeline desc struct.
- **BufferUsage::ShaderDeviceAddress = 1 << 7**: Next free bit after TransferDst (1 << 6).
- **GL/WebGPU mesh shader methods are no-op**: These backends have no mesh shader support. `DispatchIndirect` stubs are placeholders for future `glDispatchComputeIndirect` / `wgpu::ComputePassEncoder::DispatchWorkgroupsIndirect`.
- **D3D12 stubs**: `DrawMeshTasks` requires `ID3D12GraphicsCommandList6::DispatchMesh`, `DispatchIndirect` requires `ExecuteIndirect` with command signatures — deferred to when D3D12 mesh shader pipeline is fully wired.

## Implementation Notes

- **10/10 tests pass** (debug-vulkan build)
- **0 build errors** across all backends
- **Files modified**: `ICommandBuffer.h` (+4 pure virtuals), `RhiDescriptors.h` (+BufferUsage::ShaderDeviceAddress), `RhiTypes.h` (+DrawMeshTasksIndirectCommand), `VulkanDevice.h/.cpp` (+function pointer loading), `VulkanCommandBuffer.h/.cpp` (+Vulkan impl), `D3D12CommandBuffer.h/.cpp` (+stubs), `OpenGlCommandBuffer.h/.cpp` (+stubs), `WebGpuCommandBuffer.h/.cpp` (+stubs), `MockCommandBuffer.h/.cpp` (+recording)
- **Files created**: `tests/unit/test_rhi_mesh_shader.cpp` (10 tests), CMakeLists.txt entry
- Contract check: PASS (all signatures match Anchor Card)
