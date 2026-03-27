# T3b.8.1 — GTAO: Half-Res Compute + Bilateral Upsample

**Phase**: 05-3b
**Component**: 8 — GTAO
**Roadmap Ref**: `rendering-pipeline-architecture.md` S7.3 Pass #15
**Status**: Complete
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.1.2 | Tech Debt B | Not Started | FrameResources |

## Context Anchor

### This Task's Contract

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `Gtao.h` | **public** | **M** | `Gtao` -- half-res GTAO compute, bilateral upsample, AOBuffer output |
| `gtao.slang` | internal | L | GTAO compute (8 directions x 2 horizon steps) |
| `bilateral_upsample.slang` | internal | L | Depth-aware bilateral upsample half-res to full-res |

- Half-res downsample: depth R32F at ceil(w/2) x ceil(h/2)
- GTAO compute: 8 azimuthal directions, 2 horizon search steps per direction. cos-weighted horizon integral.
- Output: AOBuffer R8_UNORM half-res
- Bilateral upsample: depth-aware 4-tap bilateral filter to full-res R8_UNORM
- Dispatch: ceil(w/2/8) x ceil(h/2/8) workgroups, 8x8 threads
- Budget: less than 1ms at 4K (half-res = 1920x1080 effective)
- Async compute candidate: overlap with material resolve on Tier1

### Downstream Consumers

- T3b.10.1: IAOProvider wraps GTAO output
- T3b.16.1: IPipelineFactory::CreateAOPass() uses GTAO for Tier1/2
- Phase 3a DeferredResolve: reads AOBuffer in deferred resolve step 7

### Technical Direction

- **GTAO algorithm**: Jimenez 2016 ground-truth AO. Per-pixel: for each direction, march along view-space horizon, compute max horizon angle. AO = 1 - integral of visible horizon solid angle.
- **Noise**: 4x4 spatial noise texture + per-pixel temporal noise (Halton offset from TAA jitter index). Temporal accumulation via TAA history.
- **Depth linearization**: reconstruct linear depth from Reverse-Z D32F using CameraUBO near/far.
- **Normal reconstruction**: reconstruct view-space normal from depth derivatives (ddx/ddy).

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/Gtao.h` | **public** | **M** | Interface |
| Create | `src/miki/rendergraph/Gtao.cpp` | internal | L | Implementation |
| Create | `shaders/rendergraph/gtao.slang` | internal | L | GTAO compute |
| Create | `shaders/rendergraph/bilateral_upsample.slang` | internal | L | Upsample |
| Create | `tests/unit/test_gtao.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define Gtao interface (Create/Setup/AddToGraph/Execute)
- [x] **Step 2**: Implement half-res depth downsample
- [x] **Step 3**: Implement GTAO compute shader (8 dir x 2 horizon)
- [x] **Step 4**: Implement bilateral upsample
- [x] **Step 5**: Integrate AOBuffer into deferred resolve
      **Files**: `DeferredResolve.h` (**shared** M — add AOBuffer parameter), `DeferredResolve.cpp` (internal L), `deferred_resolve.slang` (internal L)
      Modify `DeferredResolve::Execute` to accept `TextureHandle aoBuffer`. In shader step 7: `lighting *= texture(aoSampler, uv).r`. When aoBuffer is null/invalid, AO = 1.0 (no darkening).
      `[verify: test]`
- [x] **Step 6**: Tests

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(Gtao, CreateReturnsValid)` | Positive | factory success |
| `TEST(Gtao, HalfResOutputDimensions)` | Positive | output is ceil(w/2) x ceil(h/2) |
| `TEST(Gtao, FlatSurface_HighAO)` | Positive | flat plane has AO near 1.0 (unoccluded) |
| `TEST(Gtao, Concavity_LowAO)` | Positive | concave region has AO less than 1.0 |
| `TEST(Gtao, BilateralUpsample_PreservesEdges)` | Positive | depth discontinuity not blurred across |
| `TEST(Gtao, OutputFormat_R8UNORM)` | Positive | AOBuffer is R8_UNORM |
| `TEST(Gtao, DispatchSize_Correct)` | Positive | dispatch matches half-res dimensions |
| `TEST(Gtao, EndToEnd_AOVisible)` | Integration | scene with concavity shows darker AO regions |
| `TEST(Gtao, Create_NullDevice_Error)` | Error | null device returns error |
| `TEST(Gtao, 1x1Viewport)` | Boundary | 1x1 viewport produces valid (not NaN) AO |
| `TEST(Gtao, MaxRadius_Clamped)` | Boundary | extreme radius clamped to screen bounds |
| `TEST(Gtao, MoveSemantics)` | State | move transfers pipeline + AO buffer, source empty |
| `TEST(DeferredResolve, AOMultiplied)` | Positive | with AOBuffer bound, occluded pixels darker than without AO |
| `TEST(DeferredResolve, NullAOBuffer_NoEffect)` | Boundary | null AOBuffer = AO factor 1.0, no darkening |
