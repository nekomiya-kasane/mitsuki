# T3b.5.1 — VSM Shadow Render Pass (Dirty Pages Only)

**Phase**: 05-3b
**Component**: 5 — VSM Shadows (Rendering)
**Roadmap Ref**: `rendering-pipeline-architecture.md` S7.3 Pass #12
**Status**: Complete
**Current Step**: —
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.4.2 | VSM Page Pool | Not Started | `VsmPagePool`, dirty page list, physical page array |

## Context Anchor

### This Task's Contract

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `VsmShadowRender.h` | **shared** | **M** | Render dirty pages, depth-only, per-page viewport |
| `vsm_shadow_depth.slang` | internal | L | Depth-only shader for VSM page rendering |

- Render: iterate dirty page list; per page set viewport/scissor to physical page region; draw shadow casters.
- PSO: depth-only, front-face cull, depthBias=(2,1.5), depthClamp=true, GreaterEqual (Reverse-Z).
- Static scene: no dirty pages = ~0ms shadow cost.

### Downstream Consumers

- T3b.5.2: rendered pages sampled in deferred resolve
- T3b.16.1: IPipelineFactory::CreateShadowPass() uses VSM for Tier1

### Technical Direction

- Per-page rendering with CPU frustum-cull per page bounds
- Batch adjacent pages where possible
- Budget: less than 2ms for 100 dirty pages

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/VsmShadowRender.h` | **shared** | **M** | Interface |
| Create | `src/miki/rendergraph/VsmShadowRender.cpp` | internal | L | Render logic |
| Create | `shaders/rendergraph/vsm_shadow_depth.slang` | internal | L | Depth shader |
| Create | `tests/unit/test_vsm_shadow_render.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define VsmShadowRender interface
- [x] **Step 2**: Implement per-page depth render with viewport/scissor
- [x] **Step 3**: Write vsm_shadow_depth.slang
- [x] **Step 4**: Tests

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(VsmShadowRender, CreateReturnsValid)` | Positive | factory success |
| `TEST(VsmShadowRender, NoDirtyPages_NoRender)` | Boundary | zero dispatch when no dirty pages |
| `TEST(VsmShadowRender, DirtyPage_RendersDepth)` | Positive | dirty page has non-zero depth |
| `TEST(VsmShadowRender, ViewportMatchesPhysicalPage)` | Positive | viewport matches page region |
| `TEST(VsmShadowRender, DepthBiasApplied)` | Positive | bias params set in PSO |
| `TEST(VsmShadowRender, FrustumCullPerPage)` | Positive | objects outside page not rendered |
| `TEST(VsmShadowRender, MultipleDirtyPages_AllRendered)` | Positive | N dirty pages all get rendered |
| `TEST(VsmShadowRender, EndToEnd_RenderAndReadback)` | Integration | render + readback non-zero |
| `TEST(VsmShadowRender, InvalidPageIndex_Skip)` | Error | out-of-range page index skipped, no crash |
| `TEST(VsmShadowRender, MoveSemantics)` | State | move transfers pipeline, source empty |
| `TEST(VsmShadowRender, MaxDirtyPages_AllRendered)` | Boundary | pool-size dirty pages all rendered correctly |
