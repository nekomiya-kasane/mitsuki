# T3b.1.2 — Phase 3a Tech Debt B: Per-Frame-In-Flight Transients + FrameResources + Light SSBO Staging

**Phase**: 05-3b (Shadows, Post-Processing & Visual Regression)
**Component**: 1 — Phase 3a Tech Debt Resolution
**Roadmap Ref**: `phase-04-3a.md` D5/D8/D10
**Status**: Complete
**Current Step**: —
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.1.1 | Tech Debt A | Complete | `TransientResourceSet`, `TextureDesc::isCubemap` |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `FrameResources.h` | **shared** | **M** | `FrameResources` struct — per-frame GPU resource bundle (device, cmdBuf, frameIndex, transientPool, swapchainImage, dimensions) |
| `DeferredResolve.cpp` | internal | L | Light SSBO via staging upload instead of CpuToGpu persistent map |
| `deferred_pbr_basic/main.cpp` | internal | L | Per-frame-in-flight transient resource pools; uses FrameResources |

- **Error model**: `FrameResources` is a POD aggregate — no error paths
- **Thread safety**: `FrameResources` is per-frame, single-owner
- **D5 resolution**: replace `WaitIdle` every frame with per-frame-in-flight transient descriptor pools (2 slots, fence-guarded reuse)
- **D8 resolution**: `DeferredResolve` light SSBO created as `GpuOnly` + staging upload per dirty frame (not CpuToGpu persistent)
- **D10 resolution**: `FrameResources` struct consolidates 10+ loose parameters into single aggregate

### Downstream Consumers

- `FrameResources.h` (**shared** M):
  - T3b.3.1–T3b.16.2 (all Phase 3b GPU pass tasks): receive `FrameResources` instead of loose params
  - Phase 3b demo: `RunInteractive`/`RunOffscreen` pass `FrameResources` to all passes

### Upstream Contracts

- T3b.1.1: `TransientResourceSet` from `Execute()` — transients allocated per-frame slot
- Phase 3a: `ISwapchain::AcquireNextImage()`, `FrameManager`, `RenderGraphExecutor::Execute()`

### Technical Direction

- **Per-frame-in-flight pools**: `kMaxFramesInFlight = 2` descriptor pools + transient texture/buffer arrays. Frame N reuses slot `N % 2` after fence wait confirms GPU completion.
- **Staging upload for lights**: create light SSBO as `GpuOnly`. Each frame, if dirty, copy via `StagingUploader::Upload()`. Eliminates persistent CPU→GPU mapping overhead.
- **FrameResources aggregate**: single struct passed to all `Execute()` lambdas, replacing 10+ individual parameters.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/FrameResources.h` | **shared** | **M** | Per-frame resource bundle |
| Modify | `src/miki/rendergraph/DeferredResolve.cpp` | internal | L | Light SSBO staging |
| Modify | `demos/deferred_pbr_basic/main.cpp` | internal | L | Per-frame-in-flight + FrameResources |
| Modify | `tests/unit/test_deferred_resolve.cpp` | internal | L | Updated test |
| Create | `tests/unit/test_frame_resources.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define `FrameResources` struct
      **Files**: `FrameResources.h` (**shared** M)

      **Signatures**:

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `FrameResources` | `{ IDevice* device; ICommandBuffer* cmdBuf; uint32_t frameIndex; uint32_t width; uint32_t height; TextureHandle swapchainImage; TransientResourceSet transients; }` | POD aggregate |

      `[verify: compile]`

- [x] **Step 2**: Implement per-frame-in-flight transient pools in demo
      **Files**: `deferred_pbr_basic/main.cpp` (internal L)
      Replace `WaitIdle` per frame with fence-guarded descriptor pool rotation (2 slots). Allocate transient descriptors from current frame slot.
      `[verify: compile]`

- [x] **Step 3**: Light SSBO staging upload
      **Files**: `DeferredResolve.cpp` (internal L)
      Change light SSBO from `CpuToGpu` to `GpuOnly`. Upload dirty light data via `StagingUploader`.
      `[verify: test]`

- [x] **Step 4**: Refactor demo to use `FrameResources`
      **Files**: `deferred_pbr_basic/main.cpp` (internal L)
      Replace loose parameters in `RunOffscreen`/`RunInteractive` with `FrameResources` aggregate.
      `[verify: compile]`

- [x] **Step 5**: Tests
      **Files**: `test_frame_resources.cpp`, `test_deferred_resolve.cpp` (internal L)
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(FrameResources, DefaultConstruction)` | Positive | all fields zero/null | 1 |
| `TEST(FrameResources, AggregateInit)` | Positive | brace-init with all fields | 1 |
| `TEST(DeferredResolve, LightSsboIsGpuOnly)` | State | buffer usage is GpuOnly not CpuToGpu | 3 |
| `TEST(DeferredResolve, LightSsboDirtyUpload)` | Positive | staging upload on dirty | 3 |
| `TEST(DeferredResolve, LightSsboCleanSkipsUpload)` | Boundary | no upload when not dirty | 3 |
| `TEST(DeferredPbrBasic, NoWaitIdlePerFrame)` | Integration | demo runs without WaitIdle in steady state | 2,4 |
| `TEST(FrameResources, FrameIndexWraps)` | Boundary | frameIndex wraps at kMaxFramesInFlight correctly | 2 |
| `TEST(DeferredResolve, StagingUploadFailure_Graceful)` | Error | staging upload failure does not crash, previous data retained | 3 |
| `TEST(FrameResources, DescriptorPoolRotation_2Frames)` | State | 2 consecutive frames use different descriptor pool slots | 2 |
| `TEST(FrameResources, MoveSemantics)` | State | move transfers all fields, source zeroed | 1 |
