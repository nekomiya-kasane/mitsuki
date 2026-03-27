# T1b.3.2 — WebGpuCommandBuffer (Command Encoder, Push Constant UBO)

**Phase**: 02-1b (Compat Backends & Full Coverage)
**Component**: WebGPU Tier3 Backend
**Roadmap Ref**: `roadmap.md` L1147 — `WebGpuCommandBuffer` maps to `wgpu::CommandEncoder`
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1b.3.1 | WebGpuDevice | Complete | `WebGpuDevice`, `WgpuHandlePool`, `wgpu::Device` access |
| T1a.3.2 | ICommandBuffer | Complete | `ICommandBuffer` virtual interface |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `src/miki/rhi/webgpu/WebGpuCommandBuffer.h` | shared | **M** | `WebGpuCommandBuffer` — `ICommandBuffer` impl via `wgpu::CommandEncoder` |
| `src/miki/rhi/webgpu/WebGpuCommandBuffer.cpp` | internal | L | Command encoding + submit impl |
| `tests/unit/test_webgpu_cmdbuf.cpp` | internal | L | Unit tests |

- **Error model**: `std::expected<T, ErrorCode>` — encoder creation can fail
- **Thread safety**: single-owner, same as all ICommandBuffer implementations
- **Invariants**: `Begin()` creates `wgpu::CommandEncoder`. Commands recorded via render/compute pass encoders. `End()` calls `encoder.Finish()` → `wgpu::CommandBuffer`. `Submit()` on device calls `queue.Submit()`. Push constants: `wgpu::Queue::WriteBuffer` into 256B UBO at bind group 0 slot 0.

### Downstream Consumers

- `WebGpuCommandBuffer.h` (shared, heat **M**):
  - T1b.4.1 (same Phase): OffscreenTarget WebGPU tests need command buffer
  - T1b.7.1 (same Phase): triangle demo WebGPU path
  - Phase 2: forward rendering WebGPU path
  - Phase 3a: render graph WebGPU executor — render pass begin/end through command buffer

### Upstream Contracts

- T1a.3.2: `ICommandBuffer` — all virtual methods
  - Source: `include/miki/rhi/ICommandBuffer.h`
- T1b.3.1: `WebGpuDevice` — provides `wgpu::Device`, `WgpuHandlePool`

### Technical Direction

- **Render pass encoder model**: `BeginRendering(RenderingInfo)` creates `wgpu::RenderPassEncoder` from `RenderingInfo`. `EndRendering()` calls `renderPassEncoder.End()`. Draw/bind commands between them go to the render pass encoder.
- **Compute pass encoder**: `Dispatch()` outside a render pass creates a `wgpu::ComputePassEncoder`.
- **Push constant UBO**: `PushConstants()` writes to the pre-created 256B UBO via `queue.WriteBuffer()`. The UBO is bound at bind group 0 slot 0 in every pipeline layout. Slang `[[vk::push_constant]]` → WGSL uniform block at group 0 binding 0.
- **Barrier mapping**: WebGPU handles barriers implicitly through render/compute pass transitions. `PipelineBarrier()` is a no-op on WebGPU (validation layer handles it).
- **Single-queue**: WebGPU is single-queue by design. No explicit queue family management.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `src/miki/rhi/webgpu/WebGpuCommandBuffer.h` | shared | **M** | Command buffer class |
| Create | `src/miki/rhi/webgpu/WebGpuCommandBuffer.cpp` | internal | L | Encoding impl |
| Create | `tests/unit/test_webgpu_cmdbuf.cpp` | internal | L | Unit tests |
| Modify | `tests/unit/CMakeLists.txt` | internal | L | Add test target |

## Steps

- [x] **Step 1**: Define `WebGpuCommandBuffer` class
      **Files**: `WebGpuCommandBuffer.h` (shared M)

      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `WebGpuCommandBuffer` | class implementing `ICommandBuffer` | — |
      | All `ICommandBuffer` virtuals | (see `ICommandBuffer.h`) | override |

      **Acceptance**: compiles
      `[verify: compile]`

- [x] **Step 2**: Implement command encoding
      **Files**: `WebGpuCommandBuffer.cpp` (internal L)
      `Begin()` → create `wgpu::CommandEncoder`. `BeginRendering()` → create `wgpu::RenderPassEncoder`. Draw/bind commands → encoder methods. `End()` → `encoder.Finish()`. Push constants → `queue.WriteBuffer` to UBO.
      **Acceptance**: compiles
      `[verify: compile]`

- [x] **Step 3**: Write unit tests
      **Files**: `test_webgpu_cmdbuf.cpp` (internal L)
      **Acceptance**: all tests pass (Dawn headless) or GTEST_SKIP
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(WebGpuCmdBuf, BeginEndSucceeds)` | Positive | encoder lifecycle | 1-3 |
| `TEST(WebGpuCmdBuf, RecordRenderPass)` | Positive | begin/end rendering + draw | 1-3 |
| `TEST(WebGpuCmdBuf, PushConstantsWriteUBO)` | Positive | push constant → UBO write | 1-3 |
| `TEST(WebGpuCmdBuf, DispatchCompute)` | Positive | compute pass dispatch | 1-3 |
| `TEST(WebGpuCmdBuf, BarrierIsNoOp)` | Boundary | barrier does not crash on WebGPU | 1-3 |
| `TEST(WebGpuCmdBuf, RecordBeforeBeginFails)` | Error | Draw() before Begin() → error | 1-3 |
| `TEST(WebGpuCmdBuf, EncoderStateAfterEnd)` | State | after End(), encoder produces valid CommandBuffer | 1-3 |
| `TEST(WebGpuCmdBuf, EndToEnd_RenderAndSubmit)` | **Integration** | full draw sequence → submit → device tick | 1-3 |

## Design Decisions

*(Fill during implementation)*

## Implementation Notes

*(Fill during implementation)*
