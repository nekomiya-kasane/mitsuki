# T3b.16.2 — deferred_pbr Demo Upgrade + Per-Backend Golden Images

**Phase**: 05-3b
**Component**: 16 — 5-Backend Tier Sync + Demo
**Roadmap Ref**: `roadmap.md` Phase 3b — Demo + Tests
**Status**: Complete
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.16.1 | IPipelineFactory impls | Complete | All pass factories |
| T3b.6.1 | Bloom | Complete | `Bloom` pass |
| T3b.7.2 | AutoExposure | Complete | `AutoExposure` |
| T3b.14.1 | VRS | Complete | `VrsImageGenerator` |
| T3b.15.1 | VisualRegression | Complete | Golden image framework |
| T3b.13.1 | Temporal Upscale | Complete | `ITemporalUpscaler` |

## Context Anchor

### This Task's Contract

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `demos/deferred_pbr/main.cpp` | internal | L | Upgraded demo with all Phase 3b features |
| `demos/deferred_pbr/CMakeLists.txt` | internal | L | Build config |
| `tests/golden/deferred_pbr_*.png` | internal | L | Per-backend golden reference images |

- **Demo upgrade**: extend deferred_pbr_basic to deferred_pbr with:
  - VSM shadows (Tier1) / CSM shadows (Tier2/3/4)
  - TAA (Tier1/2) / FXAA (Tier3/4)
  - GTAO (Tier1/2) / SSAO (Tier3/4)
  - VRS (Tier1 only, with debug overlay toggle)
  - Bloom (all tiers)
  - 6 tone mapping modes (ImGui radio buttons)
  - Auto-exposure (toggle on/off via ImGui)
  - Directional light with shadow-casting enabled
- **ImGui controls**: tone map mode selector, bloom intensity slider, AO strength slider, VRS debug overlay toggle, exposure override slider, upscale quality selector
- **Golden images**: capture reference images per backend (vulkan, d3d12, compat, gl, webgpu). Store in `tests/golden/`.
- **CI integration**: visual regression test compares each backend's output against golden reference.

### Downstream Consumers

- Phase 4+: deferred_pbr demo serves as visual regression baseline for all subsequent phases

### Technical Direction

- **Render loop restructure**: use `FrameResources` (T3b.1.2) for clean parameter passing.
- **Pass chain**: DepthPrepass -> GBuffer -> [VSM|CSM] -> DeferredResolve(shadows+AO+IBL) -> Bloom -> ToneMap -> [TAA|FXAA] -> [Upscale] -> Present
- **Conditional RG nodes**: VRS, Bloom disabled when user toggles off in ImGui. Zero cost when disabled.
- **Backend CLI**: `--backend vulkan|d3d12|compat|gl|webgpu` selects backend. `--golden` flag captures golden image. `--compare` flag runs visual regression.
- **Golden image capture**: deterministic render (fixed camera, fixed scene, frame 10 for TAA convergence) -> PNG.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `demos/deferred_pbr/main.cpp` | internal | L | Upgraded demo |
| Create | `demos/deferred_pbr/CMakeLists.txt` | internal | L | Build config |
| Modify | `demos/CMakeLists.txt` | internal | L | Add deferred_pbr subdirectory |
| Create | `tests/integration/test_deferred_pbr.cpp` | internal | L | Visual regression tests |
| Create | `tests/golden/.gitkeep` | internal | L | Golden image directory |

## Steps

- [x] **Step 1**: Create deferred_pbr demo directory and CMake setup
- [x] **Step 2**: Port deferred_pbr_basic loop to FrameResources + new pass chain
- [x] **Step 3**: Integrate all Phase 3b passes (shadows, AO, AA, bloom, tone map, VRS, upscale)
- [x] **Step 4**: Add ImGui controls for all toggles/sliders
- [x] **Step 5**: Implement golden image capture mode (--golden flag)
- [x] **Step 6**: Implement visual regression test (--compare flag)
- [ ] **Step 7**: Generate golden images for all 5 backends
- [ ] **Step 8**: CI integration test

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(DeferredPbr, Vulkan_GoldenImage)` | Integration | Vulkan output matches golden PSNR > 30 |
| `TEST(DeferredPbr, D3D12_GoldenImage)` | Integration | D3D12 output matches golden |
| `TEST(DeferredPbr, Compat_GoldenImage)` | Integration | Compat output matches golden |
| `TEST(DeferredPbr, GL_GoldenImage)` | Integration | GL output matches golden |
| `TEST(DeferredPbr, WebGPU_GoldenImage)` | Integration | WebGPU output matches golden |
| `TEST(DeferredPbr, ShadowsVisible)` | Integration | shadow area darker than lit area |
| `TEST(DeferredPbr, AOVisible)` | Integration | concavity darker with AO |
| `TEST(DeferredPbr, BloomVisible)` | Integration | bright area has bloom halo |
| `TEST(DeferredPbr, ToneMapModeSwitch)` | Integration | all 6 modes produce valid output |
| `TEST(DeferredPbr, VRS_RateImageValid)` | Integration | VRS rate image has expected dimensions |
| `TEST(DeferredPbr, MinimalScene_NoFeatures)` | Boundary | empty scene with all features disabled produces valid output |
| `TEST(DeferredPbr, MissingPass_Graceful)` | Error | skipped pass (e.g., VRS on Tier3) does not crash |
| `TEST(DeferredPbr, FrameIndex0_GoldenCapture)` | State | frame 0 golden capture is deterministic across runs |
