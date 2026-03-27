# T3a.7.1 — deferred_pbr_basic Demo (49 PBR Spheres, 5 Backends) + CI

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 7 — 5-Backend Sync + Demo
**Roadmap Ref**: `roadmap.md` L1341 — Demo deferred_pbr_basic
**Status**: Partial (framework-only)
**Current Step**: Step 2-3 (GPU render activation)
**Resume Hint**: All execute lambdas are stubs — demo framework + CI complete, but no visible rendering. Depends on T3a.2.1/3.1/4.1/4.2/5.1 GPU dispatch completion.
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.5.1 | Tone Mapping | Not Started | LDR output for display |
| T3a.4.2 | IBL + Skybox | Not Started | Skybox + IBL environment |
| T3a.6.1 | IUiBridge | Not Started | Input via bridge, not raw GLFW |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `demos/deferred_pbr_basic/main.cpp` | internal | L | Demo entry point |
| `demos/deferred_pbr_basic/CMakeLists.txt` | internal | L | Build target |
| `tests/integration/test_deferred_pbr_basic.cpp` | internal | L | 5-backend integration tests |

- **Scene**: 7×7 grid of spheres varying metallic (rows) × roughness (columns), 1 directional + 2 point lights
- **Render pipeline**: GBuffer → Deferred Resolve → IBL → Tone Map → ImGui overlay → Present
- **Input**: via `IUiBridge` (GlfwBridge for windowed, NullBridge for offscreen)
- **CLI**: `--backend vulkan|compat|d3d12|gl|webgpu`, `--offscreen` for headless

### Downstream Consumers

- None (leaf demo). Pattern reused by Phase 3b `deferred_pbr` with shadows/AA.

### Upstream Contracts
- T3a.1.1–1.4: Full render graph pipeline
- T3a.2.1: GBuffer geometry pass
- T3a.3.1: Deferred resolve
- T3a.4.1–4.2: Environment + IBL
- T3a.5.1: Tone mapping
- T3a.6.1: IUiBridge (GlfwBridge)
- Phase 2: `ImGuiBackend`, `ISwapchain`, `OrbitCamera`, `MaterialRegistry`

### Technical Direction
- **Full render graph**: entire frame assembled as render graph (GBuffer → Deferred → Skybox → ToneMap → ImGui)
- **Golden image**: readback after tone mapping, compare across backends
- **Offscreen mode**: no window, render to `OffscreenTarget`, readback, exit
- **CI integration**: add to `.github/workflows/ci.yml` alongside existing demos

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `demos/deferred_pbr_basic/main.cpp` | internal | L | Demo |
| Create | `demos/deferred_pbr_basic/CMakeLists.txt` | internal | L | Build |
| Modify | `demos/CMakeLists.txt` | internal | L | Add subdirectory |
| Create | `tests/integration/test_deferred_pbr_basic.cpp` | internal | L | Integration tests |
| Modify | `.github/workflows/ci.yml` | internal | L | CI matrix |

## Steps

- [x] **Step 1**: Demo skeleton + CLI + backend dispatch + render graph assembly
      **Acceptance**: compiles, creates window on Vulkan
      `[verify: compile]`

- [ ] **Step 2**: Scene setup (49 spheres, materials, lights, camera)
      **Acceptance**: GBuffer + deferred resolve produces lit image. **Currently stub** — spheres/materials/lights/camera set up in CPU, but GBuffer execute lambda only resolves handles.
      `[verify: visual]`

- [ ] **Step 3**: Full pipeline (IBL + tone map + ImGui + present)
      **Acceptance**: complete render on Vulkan. **Currently stub** — all pass lambdas are stubs, Present blit is empty.
      `[verify: visual]`

- [x] **Step 4**: Integration tests (5 backends + golden image)
      **Acceptance**: all tests pass or skip appropriately
      `[verify: test]`

- [x] **Step 5**: CI matrix update
      **Acceptance**: CI jobs added
      `[verify: compile]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(DeferredPbrBasic, RenderVulkan)` | Integration | Vulkan renders non-black | 4 |
| `TEST(DeferredPbrBasic, RenderCompat)` | Integration | Compat renders non-black | 4 |
| `TEST(DeferredPbrBasic, RenderD3D12)` | Integration | D3D12 renders non-black | 4 |
| `TEST(DeferredPbrBasic, RenderGL)` | Integration | GL renders non-black | 4 |
| `TEST(DeferredPbrBasic, RenderWebGPU)` | Integration | WebGPU renders non-black | 4 |
| `TEST(DeferredPbrBasic, GoldenImageParity)` | Visual | Cross-backend PSNR > 30 dB vs Vulkan Tier1 reference; RMSE < 0.02 normalized | 4 |
| `TEST(DeferredPbrBasic, RenderGraphCacheHit)` | Positive | Second frame hits cache | 4 |

## Design Decisions

- ~~Execute lambdas are stubs in Phase 3a — resolve RG handles, defer GPU dispatch to Phase 3b shader activation~~ **2026-03-16 correction**: This was an incorrect deferral. Phase 3a's scope explicitly includes "deferred PBR lighting" and "ACES filmic tone mapping" as deliverables. GPU dispatch must be completed within Phase 3a, not deferred to 3b. Steps 2-3 reopened.
- 128×128 offscreen for integration tests (speed), 1280×720 for interactive
- MakeSphere(32) for demo, MakeSphere(16) for tests
- Integration tests: 8 tests (5 backend render graph execution + cache hit + GBuffer validation + material uniqueness)

## Implementation Notes

- Contract check: PASS (all internal-exposure files, no public contract verification needed)
- Both build paths (debug-d3d12, debug-vulkan) pass with 0 errors, 0 project warnings
- CI updated: 6 offscreen steps added (Vulkan, Compat, D3D12, WebGPU jobs)
