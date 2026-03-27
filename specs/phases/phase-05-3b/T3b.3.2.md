# T3b.3.2 — CSM Sampling in Deferred Resolve + Integration Test

**Phase**: 05-3b (Shadows, Post-Processing & Visual Regression)
**Component**: 3 — CSM Shadows
**Roadmap Ref**: `rendering-pipeline-architecture.md` §7.3 Pass #18 step 3, §8 CSM cascade split
**Status**: Complete
**Current Step**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.3.1 | CSM Cascade + Render | Not Started | `CascadeData`, CSM atlas texture |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `deferred_resolve.slang` (modified) | internal | L | CSM shadow sampling: cascade select by depth, PCF 3x3, bias |
| `DeferredResolve.h` (modified) | **shared** | **M** | `DeferredResolve::Execute` accepts `CascadeData` + CSM atlas handle |

- **Cascade selection**: per-pixel world-space depth → find cascade index (first cascade whose splitDistance > pixelDepth)
- **PCF**: 3x3 kernel with Poisson disk offsets for soft shadows. Shadow bias = slope-scaled (dot(N,L)) + constant.
- **Cascade blending**: in overlap region (10%), linear blend between adjacent cascades for seamless transition.

### Downstream Consumers

- `DeferredResolve.h` (**shared** M):
  - T3b.5.2: VSM resolve adds alternative shadow path; shares same deferred resolve integration point
  - T3b.16.2: demo passes CSM data to deferred resolve

### Technical Direction

- **Shadow coordinate**: `worldPos * cascadeVP[i]` → NDC → atlas UV (offset by cascade sub-region)
- **Reverse-Z aware**: shadow compare uses `GreaterEqual` (closer = larger depth in Reverse-Z)
- **Tier dispatch**: Tier2/3/4 always use CSM; Tier1 uses VSM (CSM code still compiled but not executed on Tier1)

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Modify | `include/miki/rendergraph/DeferredResolve.h` | **shared** | **M** | Add CSM parameters to Execute |
| Modify | `src/miki/rendergraph/DeferredResolve.cpp` | internal | L | Bind CSM atlas + CascadeData UBO |
| Modify | `shaders/rendergraph/deferred_resolve.slang` | internal | L | CSM sampling code |
| Create | `tests/unit/test_csm_resolve.cpp` | internal | L | Unit + integration tests |

## Steps

- [x] **Step 1**: Extend `DeferredResolve::Execute` signature to accept `CascadeData` + CSM atlas
      `[verify: compile]`

- [x] **Step 2**: Implement CSM sampling in `deferred_resolve.slang` (cascade select + PCF + bias)
      `[verify: compile]`

- [x] **Step 3**: Integration test — render scene with directional light + CSM, verify shadow visibility
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(CsmResolve, CascadeSelectByDepth)` | Positive | correct cascade index for known depth values | 2 |
| `TEST(CsmResolve, PCF3x3ProducesSoftEdge)` | Positive | shadow boundary has intermediate values (not binary) | 2 |
| `TEST(CsmResolve, CascadeBlendInOverlap)` | Positive | overlap region produces blended shadow | 2 |
| `TEST(CsmResolve, NoBiasAcne)` | Positive | slope-scaled bias eliminates self-shadowing artifacts | 2 |
| `TEST(CsmResolve, EndToEnd_ShadowVisible)` | Integration | render sphere on ground plane, shadow area darker | 3 |
| `TEST(CsmResolve, ExactSplitBoundary)` | Boundary | pixel at exact cascade split distance handled without artifact | 2 |
| `TEST(CsmResolve, MissingAtlas_NoShadow)` | Error | null/invalid atlas texture results in fully lit (no shadow, no crash) | 2 |
| `TEST(CsmResolve, CascadeCountChangeRuntime)` | State | changing cascade count between frames produces correct result | 2 |
