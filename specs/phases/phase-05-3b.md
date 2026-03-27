# Phase 05: 3b — Shadows, Post-Processing & Visual Regression

**Sequence**: 05 / 27
**Status**: Not Started
**Started**: —
**Completed**: —

## Goal

VSM shadows, TAA + temporal upscale, AO (GTAO/SSAO), VRS, Bloom, tone mapping expansion (6 operators + auto-exposure), visual regression CI, pipeline cache. All plug into the render graph from Phase 3a. RTAO deferred to Phase 7a-2 when BLAS/TLAS is available. SSR/DoF/MotionBlur/CAS/ColorGrading deferred to Phase 7a-2. **Tier-differentiated**: VSM (Tier1), CSM (Tier2/3/4); TAA+FSR (Tier1/2), FXAA (Tier3/4); GTAO (Tier1/2), SSAO (Tier3/4); VRS (Tier1 only); Bloom (all tiers). Resolves Phase 3a tech debt (D2/D4/D5/D6/D8/D10).

## Roadmap Digest

### Key Components (from roadmap table, expanded for volume)

| # | Component | Core deliverable | Test target |
|---|-----------|-----------------|-------------|
| 1 | Phase 3a Tech Debt | Resolve D2/D4/D5/D6/D8/D10; FrameResources struct; per-frame-in-flight transient pools | ~8 |
| 2 | Pipeline Cache | VkPipelineCache / D3D12 PSO cache to disk; warm on first launch | ~5 |
| 3 | CSM Shadows | 2-4 cascade shadow maps; logarithmic split; cascade atlas; PCF; Tier2/3/4 | ~10 |
| 4 | VSM Shadows (Page System) | 16K virtual page table; 128x128 physical pages; page request compute; LRU allocation | ~10 | [DONE] |
| 5 | VSM Shadows (Rendering) | Mesh shader shadow render on dirty pages; page table sample + PCF in deferred resolve; dirty page invalidation | ~8 | |
| 6 | Bloom | Brightness extract; 6-level separable Gaussian downsample/upsample; additive composite; all tiers | ~8 |
| 7 | Tone Mapping (expanded) | AgX, Khronos PBR Neutral, Reinhard Extended, Uncharted 2, Linear; auto-exposure histogram compute; vignette | ~10 |
| 8 | GTAO | Half-res screen-space AO; 8 directions x 2 horizon steps; bilateral upsample; Tier1/2 | ~8 |
| 9 | SSAO Fallback | Full-res SSAO; 8-16 kernel samples; noise tile; Gaussian blur; Tier3/4 | ~6 |
| 10 | RTAO Stub | Interface + config only; actual activation Phase 7a-2; IAOProvider abstraction | ~4 |
| 11 | TAA | Halton 8-sample jitter; history buffer; YCoCg neighborhood clamp; motion rejection; reactive mask | ~10 |
| 12 | FXAA Fallback | FXAA 3.11 quality 29; luma-in-alpha; Tier3/4 | ~5 |
| 13 | Temporal Upscale Interface | TemporalUpscaler abstraction; FSR 3.0 / DLSS 3.5 stubs; quality modes | ~5 |
| 14 | VRS | VrsImageGenerator compute; per-16x16-tile shading rate; CAD-aware override; Tier1 only | ~6 |
| 15 | Visual Regression CI | Headless render → PNG capture → pixel-diff golden images; per-backend | ~6 |
| 16 | 5-Backend Tier Sync + Demo | IPipelineFactory::CreateShadowPass/CreateAOPass/CreateAAPass impl; deferred_pbr demo upgrade; golden images | ~8 |
| 17 | Ground Plane | Shadow-receiving infinite y=0 plane; configurable opacity/fade; VSM/CSM shadow sampling; BackgroundMode extension; deferred from Phase 3a | ~8 |

### Critical Technical Decisions

- **VSM over CSM for Tier1**: 16K virtual texture eliminates cascade seams, provides infinite shadow range. CSM is Tier2/3/4 fallback only.
- **VSM page lifecycle**: page request (compute) → LRU alloc (CPU callback or compute) → dirty-only render (mesh shader) → page table sample in deferred resolve. Static scenes = ~0ms shadow cost after first frame.
- **CSM cascade split**: logarithmic split lambda=0.7; 10% border overlap for smooth transition; per-pixel cascade select in deferred resolve.
- **GTAO over SSAO for Tier1/2**: half-res = 4x fewer samples, horizon-based = better quality/perf ratio than kernel SSAO.
- **RTAO stub only**: interface defined here, activation deferred to Phase 7a-2 when BLAS/TLAS is available for picking (shared acceleration structure).
- **Tone mapping before TAA**: TAA operates on LDR post-tone-map output (per architecture §14.1 chain: `ToneMap → TAA → FSR/DLSS`). YCoCg clamp in perceptual LDR space.
- **Bloom all tiers**: compute path (T1/T2), fragment path (T3/T4). 6-level mip chain for physically plausible bloom spread.
- **Auto-exposure**: histogram compute (<0.1ms) → percentile-based EV adjustment → smooth temporal adaptation.
- **Tone mapping expansion**: 6 operators selectable via push constant (zero PSO switch). AgX for superior chroma preservation, Khronos PBR Neutral for physical accuracy.
- **Visual regression**: headless render via OffscreenTarget → PNG → per-pixel RMSE diff. Golden images per-backend stored in CI.
- **Pipeline cache**: `VkPipelineCache` / D3D12 PSO cache serialized to disk. Second-launch compile < 1s.
- **Phase 3a tech debt resolution**: D2 (CompiledGraph mutability), D4 (EnvironmentMap RAII), D5 (WaitIdle per frame → per-frame-in-flight transients), D6 (isCubemap flag), D8 (light SSBO staging), D10 (parameter bloat → FrameResources).

### Performance Targets

| Metric | Target |
|--------|--------|
| VSM dirty page render (static scene) | ~0ms GPU |
| VSM dirty page render (dynamic, 100 pages) | < 2ms GPU |
| CSM 4-cascade render | < 2ms GPU |
| GTAO (half-res @4K) | < 1ms GPU |
| SSAO (full-res @4K) | < 1ms GPU |
| TAA | < 0.5ms GPU |
| Temporal upscale (FSR/DLSS) | < 1ms GPU |
| FXAA 3.11 | < 0.5ms GPU |
| Bloom (T1/T2) | < 0.5ms GPU |
| Bloom (T3/T4) | < 0.8ms GPU |
| Tone mapping + vignette | < 0.2ms GPU |
| Auto-exposure histogram | < 0.1ms GPU |
| VRS image compute | < 0.2ms GPU |

### Downstream Phase Expectations

| Phase | What it expects from this phase |
|-------|-------------------------------|
| Phase 4 | Pipeline cache infrastructure (warm startup) |
| Phase 6a | VSM page logic reusable for virtual texture paging; GTAO/TAA as RG nodes |
| Phase 7a-1 | Shadow infrastructure (VSM/CSM) for HLR shadow tests; TAA reactive mask for edge AA |
| Phase 7a-2 | RTAO stub → activation; VSM shadow atlas for point/spot lights; TAA motion vector consumption |
| Phase 7b | VSM page logic reusable for SDF trim texture virtual paging (GPU Trim Tech Spike) |
| Phase 11 | Visual regression CI infrastructure reusable for debug overlay golden tests |

## Phase Dependencies

| Dependency Phase | What it provides |
|-----------------|-----------------|
| Phase 1a (01-1a) | `IDevice`, `ICommandBuffer`, `Handle<Tag>`, `Format`, `StagingUploader`, `SlangCompiler`, `OrbitCamera` |
| Phase 1b (02-1b) | Compat/GL/WebGPU backends, `OffscreenTarget`, `ReadbackBuffer` |
| Phase 2 (03-2) | `GraphicsPipelineDesc`, `IPipelineFactory`, `DescriptorSetLayout/PipelineLayout/DescriptorSet`, `ForwardPass`, `MaterialRegistry`, `ISwapchain`, `MeshData`, `TimestampQuery`, `CopyTextureToBuffer` |
| Phase 3a (04-3a) | `RenderGraphBuilder/Compiler/Executor/Cache`, `GBuffer`, `DeferredResolve`, `EnvironmentRenderer`, `ToneMapping`, `IUiBridge`, `CameraUBO` (368B), `DrawListBuilder`, `ForwardPass` (RG-native), `DummyTextures` |

---

## Components & Tasks

### Component 1: Phase 3a Tech Debt Resolution

> Resolve deferred tech debt items D2/D4/D5/D6/D8/D10 from Phase 3a code review.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.1.1 | CompiledGraph immutability (D2) + EnvironmentMap RAII guards (D4) + isCubemap flag (D6) | — | M |
| [x] | T3b.1.2 | Per-frame-in-flight transient pools (D5) + FrameResources struct (D10) + light SSBO staging (D8) | T3b.1.1 | H |

### Component 2: Pipeline Cache

> VkPipelineCache / D3D12 PSO cache persistent to disk. Warm on first launch.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.2.1 | PipelineCache — serialize/deserialize VkPipelineCache + D3D12 PSO cache; mock pass-through | — | M |

### Component 3: CSM Shadows

> Cascaded Shadow Maps for Tier2/3/4. 2-4 cascades, logarithmic split, PCF.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.3.1 | CSM cascade computation (split, frustum, atlas layout) + shadow render pass (vertex shader, depth-only) | T3b.1.2 | H |
| [x] | T3b.3.2 | CSM sampling in deferred resolve (cascade select, PCF, bias) + integration test | T3b.3.1 | M |

### Component 4: VSM Shadows — Page System

> Virtual Shadow Map page table, page request compute, LRU physical page allocation.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.4.1 | VSM page table data structures + page request compute shader | T3b.1.2 | H |
| [x] | T3b.4.2 | Physical page pool + LRU allocator + dirty page tracking | T3b.4.1 | H |

### Component 5: VSM Shadows — Rendering

> Mesh shader shadow render on dirty pages; page table sampling + PCF in deferred resolve.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.5.1 | VSM shadow render pass (dirty pages only, depth-only, mesh shader T1 / vertex T2 fallback) | T3b.4.2 | H |
| [x] | T3b.5.2 | VSM page table sampling + PCF in deferred resolve + shadow parity test | T3b.5.1, T3b.3.2 | H |

### Component 6: Bloom

> Brightness extract → 6-level separable Gaussian → additive composite. All tiers.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.6.1 | Bloom compute (T1/T2) + fragment fallback (T3/T4) — extract, downsample/upsample chain, composite | T3b.1.2 | H |

### Component 7: Tone Mapping (expanded)

> 5 new operators + auto-exposure histogram + vignette.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.7.1 | Tone mapping operator expansion (AgX, Khronos PBR Neutral, Reinhard Extended, Uncharted 2, Linear) + vignette + CA | — | M |
| [x] | T3b.7.2 | Auto-exposure histogram compute + temporal adaptation + EV push constant | T3b.7.1 | M |

### Component 8: GTAO

> Ground-Truth Ambient Occlusion. Half-res compute, bilateral upsample. Tier1/2.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.8.1 | GTAO compute shader (half-res, 8 dir x 2 horizon) + bilateral upsample + AOBuffer output | T3b.1.2 | H |

### Component 9: SSAO Fallback

> Screen-space AO for Tier3/4. Full-res fragment, 8-16 samples, Gaussian blur.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.9.1 | SSAO fragment shader (8-16 samples, noise tile, 2-pass Gaussian blur) | T3b.1.2 | M |

### Component 10: RTAO Stub

> Interface + config definition. Actual activation in Phase 7a-2.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.10.1 | IAOProvider interface + RTAOConfig + RTAO stub (returns GTAO output) | T3b.8.1 | S |

### Component 11: TAA

> Temporal Anti-Aliasing. Halton jitter, history buffer, YCoCg clamp, motion rejection, reactive mask.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.11.1 | TAA compute — Halton jitter integration into CameraUBO + projection matrix offset | T3b.1.2 | M |
| [x] | T3b.11.2 | TAA history buffer + YCoCg neighborhood clamp + motion rejection + reactive mask + output | T3b.11.1, T3b.7.1 | H |

### Component 12: FXAA Fallback

> FXAA 3.11 for Tier3/4. Fragment shader, luma-in-alpha.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.12.1 | FXAA 3.11 (quality 29) fragment shader + luma-in-alpha prep | T3b.7.1 | M |

### Component 13: Temporal Upscale Interface

> TemporalUpscaler abstraction. FSR 3.0 / DLSS 3.5 stubs. Quality modes.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.13.1 | ITemporalUpscaler interface + NativeUpscaler (TAA passthrough) + quality mode enum | T3b.11.2 | M |

### Component 14: VRS

> Variable Rate Shading image generator. Tier1 only.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.14.1 | VrsImageGenerator compute (luminance gradient + edge detect → rate image) + CAD-aware override | T3b.1.2 | M |

### Component 15: Visual Regression CI

> Headless render → PNG capture → pixel-diff against golden images.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.15.1 | Visual regression framework (headless render, PNG capture, RMSE diff, CI integration) | T3b.1.2 | M |

### Component 16: 5-Backend Tier Sync + Demo

> IPipelineFactory pass implementations + deferred_pbr demo upgrade + golden images.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.16.1 | IPipelineFactory::CreateShadowPass / CreateAOPass / CreateAAPass — tier-appropriate implementations | T3b.5.2, T3b.8.1, T3b.9.1, T3b.11.2, T3b.12.1 | H |
| [x] | T3b.16.2 | deferred_pbr demo upgrade (VSM/CSM + TAA/FXAA + GTAO/SSAO + VRS + Bloom + 6 tone map modes + ground plane) + per-backend golden images | T3b.16.1, T3b.6.1, T3b.7.2, T3b.14.1, T3b.15.1, T3b.13.1, T3b.17.1 | H |

### Component 17: Ground Plane (deferred from Phase 3a)

> Shadow-receiving infinite y=0 plane with configurable opacity/fade. Requires VSM/CSM shadow data.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3b.17.1 | Ground shadow plane (fullscreen fragment, VSM/CSM shadow sample, BackgroundMode extension, distance fade) | T3b.5.2, T3b.3.2 | M |

---

## Demo Plan

### deferred_pbr_basic (Phase 3b intermediate demo enhancement)

> **Already implemented** (pre-L2): ImGui post-processing panel added to existing `deferred_pbr_basic` demo.
> Shows: 6 tone mapping modes (ImGui combo), exposure slider (-5~+5 EV), vignette (strength + falloff), chromatic aberration strength.
> All controlled via `ToneMappingSettings` struct → `BuildFrameGraph` → `ToneMapping::Execute` push constants.

### deferred_pbr (final Phase 3b demo, upgraded from deferred_pbr_basic)

- **Name**: `demos/deferred_pbr/`
- **Shows**: 49 PBR spheres, VSM/CSM shadows (directional light), TAA/FXAA, GTAO/SSAO, VRS (Tier1), bloom, tone mapping (all 6 modes toggle via ImGui), auto-exposure, ground shadow plane
- **Requires Tasks**: All T3b.*.* tasks
- **CLI**: `--backend vulkan|compat|d3d12|gl|webgpu`
- **Acceptance**:
  - [ ] VSM shadows visible on Vulkan/D3D12 (Tier1)
  - [ ] CSM shadows visible on Compat/GL/WebGPU (Tier2/3/4)
  - [ ] TAA active on Tier1/2, FXAA on Tier3/4
  - [ ] GTAO active on Tier1/2, SSAO on Tier3/4
  - [ ] Bloom visible across all tiers
  - [ ] All 6 tone mapping modes switchable via ImGui
  - [ ] Auto-exposure adapts to scene brightness
  - [ ] VRS active on Tier1 (shading rate debug overlay in ImGui)
  - [ ] Ground shadow plane visible when BackgroundMode == GroundPlane
  - [ ] Golden image parity: per-backend PSNR > 30 dB vs reference (Vulkan Tier1); RMSE < 0.02

## Test Summary

| Category    | Target | Actual | Notes |
|-------------|--------|--------|-------|
| Unit        | ~171   |        | T3b.1.1(10), T3b.1.2(10), T3b.2.1(9), T3b.3.1(13), T3b.3.2(8), T3b.4.1(13), T3b.4.2(14), T3b.5.1(11), T3b.5.2(11), T3b.6.1(12), T3b.7.1(15), T3b.7.2(11), T3b.8.1(14), T3b.9.1(10), T3b.10.1(7), T3b.11.1(10), T3b.11.2(13), T3b.12.1(8), T3b.13.1(8), T3b.14.1(10), T3b.15.1(9), T3b.16.1(11), T3b.16.2(13), T3b.17.1(13) |
| Integration | ~30    |        | EndToEnd tests embedded in unit test files + deferred_pbr golden images (5 backends) + shadow parity + AO/AA parity + ground plane |
| Shader      | ~11    |        | VSM page table lookup, CSM cascade select, GTAO horizon, SSAO kernel, TAA YCoCg, FXAA luma, Bloom Gaussian, ToneMap operators, AutoExposure histogram, VRS rate, Ground plane shadow |
| **Total**   | **~246** |      | Roadmap original: ~80. Expanded 3.1x due to: (a) component splits 10->17, (b) tests-as-contracts audit, (c) CA + AO integration + ground plane additions |

## Implementation Order (Layers)

| Layer | Tasks (parallel within layer) | Depends on Layer |
|-------|-------------------------------|------------------|
| L0 | T3b.1.1 (Tech Debt A), T3b.2.1 (Pipeline Cache), T3b.7.1 (ToneMap operators) | — (Phase 3a) |
| L1 | T3b.1.2 (Tech Debt B: FrameResources), T3b.7.2 (AutoExposure) | L0 |
| L2 | T3b.3.1 (CSM), T3b.4.1 (VSM Page Table), T3b.6.1 (Bloom), T3b.8.1 (GTAO), T3b.9.1 (SSAO), T3b.11.1 (TAA jitter), T3b.14.1 (VRS), T3b.15.1 (VisRegression) | L1 |
| L3 | T3b.3.2 (CSM resolve), T3b.4.2 (VSM LRU), T3b.11.2 (TAA history), T3b.12.1 (FXAA), T3b.10.1 (RTAO stub) | L2 |
| L4 | T3b.5.1 (VSM render), T3b.13.1 (Temporal Upscale) | L3 |
| L5 | T3b.5.2 (VSM resolve + parity), T3b.17.1 (Ground Plane) | L4 |
| L6 | T3b.16.1 (IPipelineFactory impls) | L5 |
| L7 | T3b.16.2 (Demo + golden images) | L6 |

**Critical path**: L0 → L1 → L2 → L3 → L4 → L5 → L6 → L7 = **8 layers** (unchanged — Ground Plane slots into L5 parallel with VSM resolve)

**Parallel opportunity**: L2 has 8 independent tasks — assignable to multiple developers. CSM and VSM are on separate tracks until L5 (shadow parity test). GTAO/SSAO, TAA/FXAA, Bloom, VRS, VisRegression are all independent in L2.

**Rationale for layer assignments**:
- **L0**: Tech debt A + pipeline cache + tone map operators have no inter-dependency and no dependency on FrameResources.
- **L1**: FrameResources (D5/D10) restructures the demo loop — prerequisite for all GPU pass tasks that need per-frame-in-flight resources.
- **L2**: All new GPU passes (CSM, VSM, Bloom, GTAO, SSAO, TAA jitter, VRS) can start once FrameResources is stable. High parallelism.
- **L3**: Passes that depend on L2 outputs (CSM resolve needs CSM, VSM LRU needs page table, TAA needs jitter, FXAA/RTAO stub need AO).
- **L4-L5**: VSM render + resolve is the last shadow component (most complex). Temporal upscale needs TAA.
- **L6-L7**: Integration (factory impls) and demo (needs everything).

---

## Forward Design Notes

| Future Phase | What this phase prepares | Interface / extension point |
|-------------|------------------------|---------------------------|
| Phase 4 | Pipeline cache infrastructure | `PipelineCache::Load/Save` reusable for descriptor buffer cache |
| Phase 6a | VSM page table structure | Reusable for virtual texture paging in GPU-driven pipeline |
| Phase 7a-2 | RTAO stub | `IAOProvider` interface → plug in RT-based AO with zero API change |
| Phase 7a-2 | Shadow Atlas | VSM page allocation logic reusable for point/spot shadow atlas tile management |
| Phase 7b | VSM page logic | Reusable for SDF trim texture virtual paging (GPU Trim Tech Spike) |
| Phase 11 | Visual regression CI | Golden image framework reusable for all subsequent phase visual tests |
| Phase 13 | Temporal upscale | `ITemporalUpscaler` interface → plug in FSR/DLSS SDK when available |

---

## Cross-Component Contracts

| Provider Task | File (Exposure) | Consumer Task | Agreed Signature |
|--------------|-----------------|---------------|------------------|
| T3b.1.2 | `FrameResources.h` (**shared** M) | T3b.3.1+, T3b.16.2 | `FrameResources { device, cmdBuf, frameIndex, transientPool, swapchainImage, ... }` |
| T3b.3.1 | `CsmShadows.h` (**public** M) | T3b.3.2, T3b.5.2, Phase 7a-2 | `CsmShadows::Create/Setup/AddToGraph/Execute`, `CascadeData` |
| T3b.4.1 | `VsmPageTable.h` (**public** H) | T3b.4.2, T3b.5.1, T3b.5.2, Phase 6a, Phase 7b | `VsmPageTable::Create`, page request compute, virtual→physical mapping |
| T3b.4.2 | `VsmPagePool.h` (**shared** M) | T3b.5.1, T3b.5.2 | `VsmPagePool::Allocate/Evict/GetDirtyPages` |
| T3b.5.2 | `VsmShadows.h` (**public** M) | T3b.16.1, Phase 7a-2 | `VsmShadows::Create/Setup/AddToGraph/Execute`, shadow resolve integration |
| T3b.6.1 | `Bloom.h` (**public** M) | T3b.16.2 | `Bloom::Create/Setup/AddToGraph/Execute`, threshold/intensity/radius params |
| T3b.7.1 | `ToneMapping.h` (**public** M, modified) | T3b.7.2, T3b.11.2, T3b.16.2 | `ToneMappingMode` expanded enum (6 modes), `ToneMapping::Execute` updated |
| T3b.7.2 | `AutoExposure.h` (**public** M) | T3b.16.2, Phase 7a-2 | `AutoExposure::Create/Compute`, histogram → EV |
| T3b.8.1 | `Gtao.h` (**public** M) | T3b.10.1, T3b.16.1 | `Gtao::Create/Setup/AddToGraph/Execute → AOBuffer R8_UNORM` |
| T3b.9.1 | `Ssao.h` (**public** M) | T3b.10.1, T3b.16.1 | `Ssao::Create/Setup/AddToGraph/Execute → AOBuffer R8_UNORM` |
| T3b.10.1 | `IAOProvider.h` (**public** H) | T3b.16.1, Phase 7a-2 (RTAO activation) | `IAOProvider::GetAOBuffer()`, `AOMode { GTAO, SSAO, RTAO }` |
| T3b.11.2 | `Taa.h` (**public** M) | T3b.13.1, T3b.16.1 | `Taa::Create/Setup/AddToGraph/Execute`, history buffer, reactive mask input |
| T3b.12.1 | `Fxaa.h` (**public** M) | T3b.16.1 | `Fxaa::Create/Setup/AddToGraph/Execute` |
| T3b.13.1 | `ITemporalUpscaler.h` (**public** H) | T3b.16.2, Phase 13 | `ITemporalUpscaler::Upscale()`, `UpscaleQuality { UltraQuality, Quality, Balanced, Performance }` |
| T3b.14.1 | `VrsImageGenerator.h` (**public** M) | T3b.16.2, Phase 6a | `VrsImageGenerator::Create/Compute → VRS rate image R8_UINT` |
| T3b.15.1 | `VisualRegression.h` (**shared** M) | T3b.16.2, Phase 11 | `CaptureGoldenImage()`, `CompareGoldenImage() → DiffResult { psnr, rmse, diffImage }` |
| T3b.17.1 | `GroundPlane.h` (**public** M) | T3b.16.2, Phase 7a-1, Phase 9 | `GroundPlane::Create/Setup/AddToGraph/Execute`, `GroundPlaneConfig`, `BackgroundMode::GroundPlane` |

---

## Known Phase 3a Tech Debt (resolved in this phase)

| # | Finding | Severity | Resolution Task |
|---|---------|----------|----------------|
| D2 | `CompiledGraph` stores runtime mutable data (transient handles) | Medium | T3b.1.1 |
| D4 | `EnvironmentMap::CreatePreset` error path — fragile manual chain-destroy | Medium | T3b.1.1 |
| D5 | Interactive path calls `WaitIdle` every frame — disables frame pipelining | Medium | T3b.1.2 |
| D6 | Cubemap detection uses `arrayLayers==6` heuristic | Medium | T3b.1.1 |
| D8 | Light SSBO uses `CpuToGpu` persistent map instead of staging upload | Low | T3b.1.2 |
| D10 | `RunOffscreen`/`RunInteractive` parameter count bloat | Low | T3b.1.2 |

---

## Completion Summary

*(Filled on phase completion)*

- **Date**: —
- **Tests**: — pass / — total
- **Known limitations**: RTAO stub only (activation Phase 7a-2); FSR/DLSS stubs (SDK integration Phase 13); SSR/DoF/MotionBlur/CAS/ColorGrading deferred to Phase 7a-2
- **Design decisions**: —
- **Next phase**: Phase 06-4 (Resource Management & Bindless)

### Locked API Surface

*(Filled on phase completion)*

| File | Exposure | Key Exports | Consumed by Phase(s) |
|------|----------|-------------|---------------------|
