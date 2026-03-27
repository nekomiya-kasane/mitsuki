# T3b.14.1 — VRS Image Generator Compute + CAD-Aware Override

**Phase**: 05-3b
**Component**: 14 — VRS
**Roadmap Ref**: `rendering-pipeline-architecture.md` S18.0 Pass #59
**Status**: Complete
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.1.2 | Tech Debt B | Not Started | FrameResources |

## Context Anchor

### This Task's Contract

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `VrsImageGenerator.h` | **public** | **M** | `VrsImageGenerator` -- per-16x16-tile shading rate from luminance gradient |
| `vrs_image.slang` | internal | L | Compute shader: luminance gradient + edge detect -> rate image |

- Per-16x16-tile analysis: compute luminance gradient magnitude across tile
- Rate assignment: edges/high-gradient = 1x1, smooth = 2x2, background = 4x4
- CAD-aware override: force 1x1 on tiles containing selected entities, annotations, gizmo overlay
- Output: VRS rate image R8_UINT, ceil(w/16) x ceil(h/16)
- `VK_KHR_fragment_shading_rate` (Vulkan), `D3D12_VARIABLE_SHADING_RATE_TIER_2` (D3D12)
- Tier1 only; Tier2/3/4 skip entirely (conditional RG node)
- Budget: less than 0.2ms

### Downstream Consumers

- T3b.16.2: demo applies VRS rate image, debug overlay shows shading rates
- Phase 6a: VRS remains active in GPU-driven pipeline

### Technical Direction

- **Gradient analysis**: per tile, compute max luminance difference between adjacent pixels (horizontal + vertical). High gradient = fine shading rate.
- **Selection override**: read `GpuInstance.selectionMask` from VisBuffer (future) or a selection stencil texture. Any selected pixel in tile forces 1x1.
- **Rate image format**: R8_UINT where value encodes `{0=1x1, 1=1x2, 2=2x1, 3=2x2, 4=2x4, 5=4x2, 6=4x4}`.
- **Feature detection**: check `VK_KHR_fragment_shading_rate` support; if unavailable, VrsImageGenerator is a no-op.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/VrsImageGenerator.h` | **public** | **M** | Interface |
| Create | `src/miki/rendergraph/VrsImageGenerator.cpp` | internal | L | Implementation |
| Create | `shaders/rendergraph/vrs_image.slang` | internal | L | Compute shader |
| Create | `tests/unit/test_vrs_image.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define VrsImageGenerator interface (Create/Compute/GetRateImage)
- [x] **Step 2**: Implement luminance gradient compute shader
- [x] **Step 3**: Implement CAD-aware override logic
- [x] **Step 4**: Feature detection + no-op fallback
- [x] **Step 5**: Tests

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(VrsImageGenerator, CreateReturnsValid)` | Positive | factory success |
| `TEST(VrsImageGenerator, UniformScene_CoarseRate)` | Positive | smooth scene gets 2x2 or 4x4 |
| `TEST(VrsImageGenerator, HighContrast_FineRate)` | Positive | edge tiles get 1x1 |
| `TEST(VrsImageGenerator, SelectedTile_Force1x1)` | Positive | selection override works |
| `TEST(VrsImageGenerator, OutputDimensions)` | Positive | ceil(w/16) x ceil(h/16) |
| `TEST(VrsImageGenerator, NoVRS_NoOp)` | Boundary | unsupported hardware = no-op |
| `TEST(VrsImageGenerator, Create_NullDevice_Error)` | Error | null device returns error |
| `TEST(VrsImageGenerator, MoveSemantics)` | State | move transfers rate image, source empty |
| `TEST(VrsImageGenerator, EndToEnd_WithRendering)` | Integration | generate rate image + apply to rendering pass |
| `TEST(VrsImageGenerator, ZeroTiles)` | Boundary | 1x1 viewport produces single tile |
