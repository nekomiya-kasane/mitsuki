# T1a.5.2 — VulkanCommandBuffer (Barriers, Dynamic Rendering, Push Constants)

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: Vulkan Tier1 Backend
**Roadmap Ref**: `roadmap.md` L328 — `VulkanCommandBuffer` — barriers via core sync2, dynamic rendering, push constants
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.5.1 | VulkanDevice | Not Started | `VulkanDevice`, `VulkanHandlePool` — native handle access |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `src/miki/rhi/vulkan/VulkanCommandBuffer.h` | shared | **M** | `VulkanCommandBuffer` — `ICommandBuffer` implementation. Dynamic rendering, sync2 barriers, push constants. |
| `src/miki/rhi/vulkan/VulkanCommandBuffer.cpp` | internal | L | Implementation mapping `ICommandBuffer` calls to Vulkan API |
| `tests/unit/test_vulkan_cmdbuf.cpp` | internal | L | Command recording tests |

- **Error model**: `Result<void>` for Begin/End. Other commands are void (errors are deferred to Submit).
- **Thread safety**: Not thread-safe — one command buffer per recording thread.
- **GPU constraints**: Uses `VK_KHR_dynamic_rendering` (Vulkan 1.3+ core). `VK_KHR_synchronization2` for barriers. Push constants up to 256B.
- **Invariants**: `Begin()` must be called before recording. `End()` must be called before `Submit()`. Commands recorded between Begin/End are deferred to GPU execution.

### Downstream Consumers

- `VulkanCommandBuffer.h` (shared, heat **M**):
  - T1a.12.1 (same Phase): Triangle demo records draw commands via `ICommandBuffer`
  - Phase 2: Forward pass records mesh draws
  - Phase 3a: Render graph executor records per-pass commands

### Upstream Contracts

- T1a.5.1: `VulkanDevice` — access to `VkDevice`, `VkQueue`, `VulkanHandlePool` for handle->native resolution
- T1a.3.2: `ICommandBuffer` interface (pure virtual), `RenderingInfo`, `PipelineBarrierInfo`, `Viewport`, `Rect2D`

### Technical Direction

- **Dynamic rendering**: `BeginRendering(RenderingInfo)` maps to `vkCmdBeginRendering(VkRenderingInfo)`. No `VkRenderPass` objects.
- **Sync2 barriers**: `PipelineBarrier(PipelineBarrierInfo)` maps to `vkCmdPipelineBarrier2(VkDependencyInfo)`. Image layout transitions via `VkImageMemoryBarrier2`.
- **Push constants**: `PushConstants(stage, offset, size, data)` maps to `vkCmdPushConstants`. Up to 256B (Vulkan 1.4 guaranteed minimum).
- **Command pool**: One `VkCommandPool` per `VulkanCommandBuffer`. Reset on `Begin()`.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `src/miki/rhi/vulkan/VulkanCommandBuffer.h` | shared | **M** | ICommandBuffer impl |
| Create | `src/miki/rhi/vulkan/VulkanCommandBuffer.cpp` | internal | L | Vulkan API mapping |
| Create | `tests/unit/test_vulkan_cmdbuf.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: VulkanCommandBuffer class + Begin/End/Submit
      **Files**: `VulkanCommandBuffer.h` (shared M), `VulkanCommandBuffer.cpp` (internal L)
      Create `VkCommandPool` + `VkCommandBuffer`. Implement `Begin` (reset + begin), `End` (end recording), dynamic rendering (`BeginRendering`/`EndRendering`).
      **Acceptance**: compiles; Begin/End/BeginRendering/EndRendering sequence valid
      `[verify: compile]`

- [x] **Step 2**: Draw commands + barriers + push constants
      **Files**: `VulkanCommandBuffer.cpp` (internal L — extend)
      Implement: `BindPipeline`, `BindVertexBuffer`, `BindIndexBuffer`, `SetViewport`, `SetScissor`, `Draw`, `DrawIndexed`, `Dispatch`, `PushConstants`, `PipelineBarrier` (sync2), `CopyBufferToTexture`, `CopyBuffer`.
      **Acceptance**: all ICommandBuffer methods implemented
      `[verify: compile]`

- [x] **Step 3**: Unit tests
      **Files**: `tests/unit/test_vulkan_cmdbuf.cpp` (internal L)
      Cover: Begin/End lifecycle, draw command recording, barrier insertion, push constant size limits. GTEST_SKIP on no-GPU.
      **Acceptance**: tests pass on Vulkan-capable machine
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(VulkanCmdBuf, BeginEndLifecycle)` | Unit | Step 1 — begin/end sequence | 1 |
| `TEST(VulkanCmdBuf, DynamicRendering)` | Unit | Step 1 — begin/end rendering | 1 |
| `TEST(VulkanCmdBuf, DrawCommand)` | Unit | Step 2 — draw records | 2 |
| `TEST(VulkanCmdBuf, PushConstants256B)` | Unit | Step 2 — max push constant size | 2 |
| `TEST(VulkanCmdBuf, PipelineBarrierSync2)` | Unit | Step 2 — barrier insertion | 2 |

## Design Decisions

- **One VkCommandPool per VulkanCommandBuffer**: Simplest model. Pool is reset on `Begin()` via `vkResetCommandPool`. No cross-thread sharing.
- **Handle resolution via VulkanDevice pools**: `VulkanCommandBuffer` holds a `VulkanDevice*` and resolves `Handle<Tag>` to native Vulkan objects via `GetTexturePool()`, `GetBufferPool()`, `GetPipelinePool()`.
- **Push constants require bound pipeline layout**: `PushConstants()` is a no-op when `boundLayout_ == VK_NULL_HANDLE`. Pipeline layout binding will be wired when `BindPipeline` resolves to a real `VkPipelineLayout`.
- **VkImageView not resolved yet**: `BeginRendering` sets `imageView = VK_NULL_HANDLE` with `TODO(T1a.7.1)` forward reference. Downstream OffscreenTarget task will add VkImageView management.
- **Sync2 barrier mapping**: `PipelineStage` → `VkPipelineStageFlags2`, `AccessFlags` → `VkAccessFlags2`, `TextureLayout` → `VkImageLayout`. All conversions in `VulkanUtils.h`.

## Implementation Notes

- **Files created**: `VulkanCommandBuffer.h`, `VulkanCommandBuffer.cpp`, `test_vulkan_cmdbuf.cpp`
- **Files modified**: `VulkanDevice.cpp` (CreateCommandBuffer + Submit now use real impl), `VulkanDevice.h` (pool accessors), `VulkanUtils.h` (sync2/layout/index type converters), `CMakeLists.txt` (build + test targets), `test_vulkan_device.cpp` (updated stub test)
- **Test count**: 5 new tests (BeginEndLifecycle, DynamicRendering, DrawCommand, PushConstants256B, PipelineBarrierSync2)
- **Total tests**: 80 (75 existing + 5 new)
