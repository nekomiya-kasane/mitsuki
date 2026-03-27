# T3a.1.3 — RenderGraphExecutor: Per-Backend Execution

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 1 — Render Graph
**Roadmap Ref**: `roadmap.md` L1335 — RenderGraphExecutor
**Status**: Done
**Current Step**: 5/5
**Resume Hint**: N/A — complete
**Effort**: XH (4-8h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.1.2-fixup | RenderGraphCompiler (hardened) | Done | `CompiledGraph`, `CompiledPass`, `BarrierCommand` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rendergraph/RenderGraphExecutor.h` | **public** | **H** | `RenderGraphExecutor` — execute compiled graph on device |
| `include/miki/rendergraph/RenderContext.h` | **public** | **H** | `RenderContext` — passed to execute lambdas, wraps cmd buf + device + resolved resources |
| `src/miki/rendergraph/RenderGraphExecutor.cpp` | internal | L | Core executor logic |
| ~~`src/miki/rendergraph/RenderGraphExecutorVulkan.cpp`~~ | — | — | **NOT NEEDED** — see Design Decisions |
| ~~`src/miki/rendergraph/RenderGraphExecutorD3D12.cpp`~~ | — | — | **NOT NEEDED** |
| ~~`src/miki/rendergraph/RenderGraphExecutorGL.cpp`~~ | — | — | **NOT NEEDED** |
| ~~`src/miki/rendergraph/RenderGraphExecutorWebGPU.cpp`~~ | — | — | **NOT NEEDED** |
| `tests/unit/test_render_graph_executor.cpp` | internal | L | Unit tests |

- **Error model**: `Execute()` returns `std::expected<void, ErrorCode>`
- **Thread safety**: Executor is single-threaded per frame (one command buffer)
- **Invariants**: all barriers emitted before each pass; transient resources allocated before first use, freed after last use; execute lambdas called in compiled order

### Downstream Consumers

- `RenderGraphExecutor.h` (**public**, heat **H**):
  - T3a.2.1 (GBuffer) — GBuffer pass execute lambda receives `RenderContext`
  - T3a.3.1 (Deferred) — deferred resolve pass uses `RenderContext`
  - Phase 3b: all post-processing passes
- `RenderContext.h` (**public**, heat **H**):
  - All pass execute lambdas receive `RenderContext&`

### Upstream Contracts
- T3a.1.2: `CompiledGraph`, `CompiledPass`, `BarrierCommand`
- Phase 2: `IDevice`, `ICommandBuffer`, `PipelineBarrierInfo`, `TextureBarrier`

### Technical Direction
- **Backend dispatch**: ~~executor inspects `GetBackendType()`~~ **REVISED**: Barrier translation is fully backend-agnostic. `BarrierCommand` → `PipelineBarrierInfo` conversion is done once; each backend's `ICommandBuffer::PipelineBarrier()` virtual implementation handles native API calls. No per-backend executor files needed.
- **Transient resource allocation**: allocate via `IDevice::CreateTexture`/`CreateBuffer` at first use, destroy at last use. Phase 4 upgrades this to memory aliasing.
- **RenderContext**: wraps `ICommandBuffer&`, `IDevice&`, plus `Resolve(RGHandle) -> TextureHandle/BufferHandle` to map graph handles to physical resources

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/RenderGraphExecutor.h` | **public** | **H** | Executor API |
| Create | `include/miki/rendergraph/RenderContext.h` | **public** | **H** | Context for execute lambdas |
| Create | `src/miki/rendergraph/RenderGraphExecutor.cpp` | internal | L | Core logic |
| — | ~~Per-backend .cpp files~~ | — | — | NOT NEEDED (see Design Decisions) |
| Create | `tests/unit/test_render_graph_executor.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define RenderContext.h (public H)
      **Files**: `RenderContext.h` (**public** H)

      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `RenderContext::GetCommandBuffer` | `() -> ICommandBuffer&` | `[[nodiscard]]` |
      | `RenderContext::GetDevice` | `() -> IDevice&` | `[[nodiscard]]` |
      | `RenderContext::Resolve` | `(RGHandle) -> TextureHandle` | `[[nodiscard]]` — resolve graph handle to physical texture |
      | `RenderContext::ResolveBuffer` | `(RGHandle) -> BufferHandle` | `[[nodiscard]]` — resolve graph handle to physical buffer |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Define RenderGraphExecutor.h (public H)
      **Files**: `RenderGraphExecutor.h` (**public** H)

      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `RenderGraphExecutor::Execute` | `(CompiledGraph&, IDevice&, ICommandBuffer&) -> expected<void, ErrorCode>` | `[[nodiscard]]` static |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 3**: Implement core executor (backend-agnostic)
      **Files**: `RenderGraphExecutor.cpp` (internal L)
      Allocate transient resources, translate BarrierCommand → PipelineBarrierInfo, call execute lambdas in order, destroy transients.
      **Acceptance**: simple 2-pass graph executes on MockDevice
      `[verify: test]`

- [x] **Step 4**: Per-backend files NOT NEEDED
      Architecture decision: `ICommandBuffer::PipelineBarrier()` is a virtual call. Each backend already implements native barrier encoding. The executor only needs to produce `PipelineBarrierInfo` structs — backend dispatch is implicit.

- [x] **Step 5**: Unit + integration tests (13 tests)
      **Files**: `test_render_graph_executor.cpp` (internal L)
      **Acceptance**: all tests pass on both build paths
      `[verify: test]`

- [x] **Step 6**: Architectural refinements (P0-P2 fixes + Improvements A/B/C/D/E)
      **Files**: `RenderGraphExecutor.cpp`, `RenderContext.h`, `RenderGraphBuilder.h`, `CompiledGraph.h`, `test_render_graph_executor.cpp`
      **Changes**:
      - P0a: Barrier srcStage/dstStage OR-merge (not overwrite)
      - P0b: Transient allocation failure cleanup (no leak on partial alloc)
      - P0c: Explicit RGBufferUsage→rhi::BufferUsage mapping + static_assert
      - P1a: Barrier vectors hoisted outside pass loop (reuse via clear)
      - P1b: TODO comment for ResourceLifetime deferred alloc
      - P1c: RenderContext members private + null assert in constructor
      - P2a: 4 tracking vectors → 1 TransientKind enum vector
      - P2b: PassData comment corrected
      - P2c: Test error injection timing fixed
      - P2d: static_assert guards on enum bit layouts
      - Improvement A: barrier stage OR-merge (covered by P0a)
      - Improvement B: BarrierCommand pre-reserved split barrier fields
      - Improvement C: CompiledGraph pre-reserved queueType + RGQueueType enum
      - Improvement D: GL/WebGPU backend barrier skip
      - Improvement E: async compute queue ownership fields pre-reserved
      - 4 new regression tests (17 total)
      **Acceptance**: 17/17 tests pass on both build paths
      `[verify: test]`

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `EmptyGraphNoOp` | Boundary | Empty compiled graph = no commands |
| `SinglePassExecutes` | Positive | Single pass executes, lambda called |
| `TwoPassBarrier` | Positive | Write→Read barrier emitted (PipelineBarrier recorded in MockCmdBuf) |
| `TransientAllocation` | Positive | Transient texture created and destroyed (leak check passes) |
| `ImportedResourcePreserved` | Positive | External resource not destroyed by executor |
| `RenderContextResolve` | Positive | RGHandle → physical handle mapping correct |
| `ExecutionOrder` | Positive | Lambdas called in topological order (A→B→C) |
| `TransientBufferAllocation` | Positive | Transient buffer allocated, resolved, cleaned up |
| `DeviceCreateFailure_Propagated` | Error | Device CreateTexture failure → executor returns error |
| `CulledPassNotExecuted` | Positive | Culled pass lambda not invoked |
| `MultipleTransients` | Positive | Multiple transient textures all cleaned up |
| `NullExecuteLambda` | Boundary | Pass with nullptr execute lambda does not crash |
| `EndToEnd_BuildCompileExecute` | Integration | Full pipeline: build → compile → execute with GBuffer + Lighting |
| `MultiBarrierStageOR` | Regression (P0a) | Multiple barriers OR-merge stages correctly |
| `PartialAllocFailure_NoLeak` | Regression (P0b) | Failed allocation cleans up already-allocated resources |
| `ResolveOutOfBounds_ReturnsEmpty` | Boundary | Out-of-bounds RGHandle resolve returns empty handle |
| `BufferUsageMapping` | Regression (P0c) | Buffer usage flags mapped correctly through executor |

## Design Decisions

### No Per-Backend Executor Files

The original task spec planned separate `.cpp` files for Vulkan, D3D12, GL, WebGPU barrier translation. During implementation, we determined this is unnecessary:

- The executor translates `BarrierCommand` → `rhi::PipelineBarrierInfo` (a backend-agnostic struct).
- `ICommandBuffer::PipelineBarrier(PipelineBarrierInfo)` is a **virtual call** — each backend already implements the native encoding (e.g., `vkCmdPipelineBarrier2`, `ID3D12GraphicsCommandList::ResourceBarrier`, `glMemoryBarrier`).
- Adding per-backend executor files would duplicate backend knowledge that already lives in the command buffer implementations.
- This follows the **Single Responsibility Principle**: the executor owns graph-level orchestration; command buffers own native API translation.

### Transient Resource Strategy

Current implementation: allocate all transients upfront (Phase 1), destroy all after execution (Phase 3). This is simple and correct.

Future optimization (Phase 4): use `ResourceLifetime.firstPass`/`lastPass` for deferred allocation and early destruction, enabling memory aliasing.

### Barrier Batching

All barriers for a single pass are batched into one `PipelineBarrier()` call. The `srcStage`/`dstStage` are **OR-merged** across all `BarrierCommand`s in the pass (P0a fix — previously overwritten by last barrier).

### Backend Barrier Skip (Improvement D)

GL and WebGPU backends have no explicit barrier concept. The executor checks `IDevice::GetBackendType()` and skips barrier emission entirely for these backends, avoiding unnecessary overhead.

## Implementation Notes

- `RenderContext` stores `std::span<const TextureHandle>` and `std::span<const BufferHandle>` — zero-copy views into the executor's resolution tables.
- `ToRhiTextureDesc` casts RG texture usage flags to RHI usage flags (1:1 bit layout, validated by `static_assert`).
- `ToRhiBufferUsage` performs **explicit mapping** from `RGBufferUsage` to `rhi::BufferUsage` because their bit layouts differ (P0c fix).
- `CompiledGraph` and `BarrierCommand` include pre-reserved fields for split barriers (Phase 4-5) and async compute queue ownership (Phase 6-7).
- 17 tests, 0 failures, both build paths (debug-d3d12, debug-vulkan).
