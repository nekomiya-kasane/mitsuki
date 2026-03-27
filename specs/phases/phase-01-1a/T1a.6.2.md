# T1a.6.2 — D3D12CommandBuffer (Command List, Barriers, Root Constants)

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: D3D12 Backend
**Roadmap Ref**: `roadmap.md` L329 — `D3D12CommandBuffer` maps to `ID3D12GraphicsCommandList7`, root constants, barriers
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.6.1 | D3D12Device | Complete | `D3D12Device`, `D3D12HandlePool` — native handle access |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `src/miki/rhi/d3d12/D3D12CommandBuffer.h` | shared | **M** | `D3D12CommandBuffer` — `ICommandBuffer` impl. Command list, barriers, root constants. |
| `src/miki/rhi/d3d12/D3D12CommandBuffer.cpp` | internal | L | Implementation mapping ICommandBuffer to D3D12 API |
| `tests/unit/test_d3d12_cmdbuf.cpp` | internal | L | Command recording tests |

- **Error model**: `Result<void>` for Begin/End. Other commands void (errors at ExecuteCommandLists).
- **Thread safety**: Not thread-safe — one command buffer per thread.
- **GPU constraints**: Maps to `ID3D12GraphicsCommandList7`. Root constants = 32 DWORD (128B). Enhanced barriers when available.
- **Invariants**: Command allocator reset per frame. Pipeline state set before draw. Root signature bound before root constants.

### Downstream Consumers

- `D3D12CommandBuffer.h` (shared, heat **M**):
  - T1a.12.1 (same Phase): Triangle demo records draw commands
  - Phase 2+: All D3D12 rendering uses this

### Upstream Contracts

- T1a.6.1: `D3D12Device` — `ID3D12Device*`, `D3D12HandlePool`, descriptor heaps
- T1a.3.2: `ICommandBuffer` interface, `RenderingInfo`, `PipelineBarrierInfo`

### Technical Direction

- **Command list**: `ID3D12GraphicsCommandList7` for draw/dispatch. `ID3D12CommandAllocator` reset per frame.
- **Dynamic rendering equivalent**: D3D12 uses render targets directly (`OMSetRenderTargets`). `BeginRendering` maps to `OMSetRenderTargets` + clear. `EndRendering` is a no-op (D3D12 has no explicit render pass end).
- **Root constants**: `SetGraphicsRoot32BitConstants` / `SetComputeRoot32BitConstants`. 32 DWORD = 128B max.
- **Barriers**: `ResourceBarrier` with `D3D12_RESOURCE_BARRIER_TYPE_TRANSITION`. Enhanced barriers (`D3D12_BARRIER_TYPE_TEXTURE`/`BUFFER`) when available.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `src/miki/rhi/d3d12/D3D12CommandBuffer.h` | shared | **M** | ICommandBuffer impl |
| Create | `src/miki/rhi/d3d12/D3D12CommandBuffer.cpp` | internal | L | D3D12 API mapping |
| Create | `tests/unit/test_d3d12_cmdbuf.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: D3D12CommandBuffer class + Begin/End + render targets
      **Files**: `D3D12CommandBuffer.h` (shared M), `D3D12CommandBuffer.cpp` (internal L)
      Command allocator + command list creation. Begin (reset allocator + list), End, BeginRendering (OMSetRenderTargets + clear), EndRendering.
      **Acceptance**: compiles on Windows with `MIKI_BUILD_D3D12=ON`
      `[verify: compile]`

- [x] **Step 2**: Draw commands + barriers + root constants
      **Files**: `D3D12CommandBuffer.cpp` (extend)
      BindPipeline (SetPipelineState), BindVertexBuffer (IASetVertexBuffers), BindIndexBuffer (IASetIndexBuffer), SetViewport (RSSetViewports), SetScissor (RSSetScissorRects), Draw (DrawInstanced), DrawIndexed (DrawIndexedInstanced), Dispatch, PushConstants (SetGraphicsRoot32BitConstants), PipelineBarrier (ResourceBarrier), copy commands.
      **Acceptance**: all ICommandBuffer methods implemented
      `[verify: compile]`

- [x] **Step 3**: Unit tests
      **Files**: `tests/unit/test_d3d12_cmdbuf.cpp` (internal L)
      Cover: Begin/End lifecycle, draw recording, root constant size, barrier types. GTEST_SKIP on non-Windows/no-GPU.
      **Acceptance**: tests pass
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(D3D12CmdBuf, CreateCommandBufferSucceeds)` | Unit | Step 1 | 1 |
| `TEST(D3D12CmdBuf, BeginEndLifecycle)` | Unit | Step 1 | 1 |
| `TEST(D3D12CmdBuf, RenderTargetSet)` | Unit | Step 1 | 1 |
| `TEST(D3D12CmdBuf, DrawCommand)` | Unit | Step 2 | 2 |
| `TEST(D3D12CmdBuf, RootConstants128B)` | Unit | Step 2 | 2 |
| `TEST(D3D12CmdBuf, PipelineBarrierTransition)` | Unit | Step 2 | 2 |
| `TEST(D3D12CmdBuf, DispatchCompute)` | Unit | Step 2 | 2 |
| `TEST(D3D12CmdBuf, CopyBufferRecording)` | Unit | Step 2 | 2 |
| `TEST(D3D12CmdBuf, ReRecordAfterEnd)` | Unit | Step 1+2 | 3 |

## Design Decisions

- **CreateCommandList1 (ID3D12Device4)**: Used to create command list in closed state, avoiding the open->close->reset cycle from `CreateCommandList`. Fallback to `CreateCommandList`+`Close` for older runtimes.
- **rootSignatureBound_ guard**: `PushConstants` is a no-op when no root signature is bound. D3D12 runtime faults (access violation) if `SetGraphicsRoot32BitConstants` is called without a root signature. Guard prevents crash during recording without a pipeline.
- **BeginRendering placeholder**: `OMSetRenderTargets` called with zero RTVs since descriptor heap management (TextureHandle -> RTV) is deferred to T1a.7.1.
- **Barrier model**: Uses legacy `D3D12_RESOURCE_BARRIER_TYPE_TRANSITION`. Enhanced barriers deferred to Phase 2+.
- **D3D12Utils extensions**: Added `ToD3D12ResourceState`, `ToD3D12IndexFormat`, `ToD3D12AccessState` conversion functions.

## Implementation Notes

- `D3D12Device::CreateCommandBuffer()` and `Submit()` now use real `D3D12CommandBuffer` (no longer stubs).
- `D3D12Device::Submit()` signals fence after `ExecuteCommandLists` for synchronous submit.
- `test_d3d12_device.cpp` updated: `StubMethodsReturnNotImplemented` now expects `CreateCommandBuffer` to succeed.
- Test guard uses `#ifndef _WIN32` (not `MIKI_BUILD_D3D12`) since the define is `PRIVATE` to `miki_rhi` and not propagated to test binaries.
- 9 tests total, all passing. CopyBufferToTexture footprint calculation uses simplified 4-byte-per-pixel fallback.
- `BindVertexBuffer` stride is 0 (placeholder) — requires pipeline vertex input layout info (Phase 2+).
