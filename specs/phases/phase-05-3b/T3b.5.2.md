# T3b.5.2 — VSM Page Table Sampling + PCF in Deferred Resolve + Shadow Parity Test

**Phase**: 05-3b
**Component**: 5 — VSM Shadows (Rendering)
**Status**: Complete
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.5.1 | VSM Shadow Render | Not Started | Rendered physical pages |
| T3b.3.2 | CSM Resolve | Not Started | CSM shadow sampling path |

## Context Anchor

### This Task's Contract

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `VsmShadows.h` | **public** | **M** | Unified VSM facade (page table + pool + render + resolve) |
| `deferred_resolve.slang` (modified) | internal | L | VSM page table lookup, PCF, tier dispatch |

- Page table sample: read R32_UINT page table, extract physical index, sample D32F depth
- PCF: 3x3 Poisson disk kernel
- Tier dispatch: VSM (Tier1) vs CSM (Tier2/3/4) via permutation or runtime branch
- Shadow parity test: VSM vs CSM RMSE less than 0.05

### Downstream Consumers

- T3b.16.1: IPipelineFactory uses VsmShadows for Tier1
- Phase 7a-2: shadow atlas extends VSM for point/spot lights

### Technical Direction

- VsmShadows facade wraps page table + pool + render
- Deferred resolve gains VSM descriptor bindings
- Slope-scaled bias + normal offset for acne prevention

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/VsmShadows.h` | **public** | **M** | Unified facade |
| Create | `src/miki/rendergraph/VsmShadows.cpp` | internal | L | Orchestration |
| Modify | `shaders/rendergraph/deferred_resolve.slang` | internal | L | VSM sampling |
| Modify | `src/miki/rendergraph/DeferredResolve.cpp` | internal | L | Bind VSM resources |
| Create | `tests/unit/test_vsm_shadows.cpp` | internal | L | Tests |
| Create | `tests/integration/test_shadow_parity.cpp` | internal | L | VSM vs CSM parity |

## Steps

- [x] **Step 1**: Define VsmShadows facade interface
- [x] **Step 2**: Implement VSM sampling in deferred_resolve.slang
- [x] **Step 3**: Integrate VSM descriptors into DeferredResolve
- [x] **Step 4**: Shadow parity test (VSM vs CSM)
- [x] **Step 5**: Unit tests

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(VsmShadows, CreateReturnsValid)` | Positive | factory success |
| `TEST(VsmShadows, PageTableSample_ValidPage)` | Positive | valid page returns shadow value |
| `TEST(VsmShadows, PageTableSample_InvalidPage)` | Boundary | invalid page returns lit (no shadow) |
| `TEST(VsmShadows, PCF_SoftEdge)` | Positive | shadow boundary has intermediate values |
| `TEST(VsmShadows, TierDispatch_Tier1UsesVSM)` | Positive | Tier1 takes VSM path |
| `TEST(VsmShadows, TierDispatch_Tier2UsesCSM)` | Positive | Tier2 takes CSM path |
| `TEST(ShadowParity, VsmVsCsm_RMSE)` | Integration | RMSE less than 0.05 |
| `TEST(VsmShadows, EndToEnd_FullPipeline)` | Integration | scene with shadow renders correctly |
| `TEST(VsmShadows, MoveSemantics)` | State | move transfers facade, source empty |
| `TEST(VsmShadows, ObjectAtPageBoundary)` | Boundary | object spanning two pages gets shadow from both |
| `TEST(VsmShadows, NullAtlas_NoShadow)` | Error | null physical page array = fully lit, no crash |
