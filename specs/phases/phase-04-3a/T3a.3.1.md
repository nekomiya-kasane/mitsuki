# T3a.3.1 — Deferred PBR Resolve (Compute/Fullscreen-Tri, Multiple Lights)

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 3 — PBR Deferred Lighting
**Roadmap Ref**: `roadmap.md` L1337 — PBR Lighting
**Status**: Partial (wiring-only)
**Current Step**: Step 3 (GPU dispatch)
**Resume Hint**: Execute() is stub — needs compute pipeline creation, GBuffer SRV binding, light SSBO upload, compute dispatch (Tier1/2) or fullscreen-tri draw (Tier3/4)
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.1.1 | RenderGraphBuilder | Not Started | `RenderGraphBuilder::AddComputePass()` / `AddGraphicsPass()` for resolve pass declaration |
| T3a.2.1 | GBuffer | Not Started | `GBufferLayout` — albedo, normal, depth, motion |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rendergraph/DeferredResolve.h` | **shared** | **M** | `DeferredResolve` — lighting pass node |
| `shaders/rendergraph/deferred_resolve.slang` | internal | L | Compute (Tier1/2) + fullscreen-tri fragment (Tier3/4) |
| `src/miki/rendergraph/DeferredResolve.cpp` | internal | L | Implementation |
| `tests/unit/test_deferred_resolve.cpp` | internal | L | Unit tests |

- **Error model**: infallible setup; execution via `RenderContext`
- **Tier differentiation**: Tier1/2 (Vulkan/D3D12/Compat) use compute shader dispatch; Tier3/4 (WebGPU/GL) use fullscreen triangle fragment shader (no compute for lighting on WebGPU)
- **Light model**: `LightBuffer` UBO upgraded from Phase 2's MAX_LIGHTS=4 to SSBO for Tier1/2 (unbounded), UBO cap 16 for Tier3/4
- **IBL integration model**: IBL terms (specular pre-filtered env lookup + BRDF LUT + diffuse irradiance SH) are computed **inside** the deferred resolve pass, not as a separate pass. `DeferredResolve::Execute()` accepts `EnvironmentRenderer const*` (nullable — if null, IBL contribution is zero). This avoids an extra read-back of the HDR buffer and ensures a single lighting pass. T3a.4.2 provides the `EnvironmentRenderer` instance; T3a.3.1 consumes it.

### Downstream Consumers

- `DeferredResolve.h` (**shared**, heat **M**):
  - T3a.4.2 (IBL) — deferred resolve adds IBL terms
  - T3a.5.1 (ToneMap) — reads HDR output
  - Phase 3b: TAA reads HDR output

### Upstream Contracts
- T3a.2.1: `GBufferLayout` — texture handles for GBuffer reads
- T3a.1.1: `RenderGraphBuilder::AddComputePass()` / `AddGraphicsPass()`
- Phase 2: `StandardPBR` (Cook-Torrance GGX), `KullaContyLut`

### Technical Direction
- **Cook-Torrance GGX**: reuse Phase 2 BRDF from `StandardPBR` Slang module
- **GBuffer decode**: inverse octahedral for normals, unpack metallic from alpha
- **HDR output**: RGBA16F render target
- **Light SSBO**: `StructuredBuffer<LightData>` with light count push constant
- **Fullscreen-tri path**: single triangle covering viewport, sample GBuffer in fragment shader
- **Multi-scattering energy compensation**: Phase 2 established `KullaContyLut` (E_lut + E_avg). The deferred resolve shader must apply Kulla-Conty multi-scattering compensation to both direct and IBL specular terms: `Fms = F_avg * Ems / (1 - F_avg * (1 - Ems))` where `Ems = 1 - E(NdotV, roughness)` (Fdez-Aguera 2019, JCGT 8.1.3). This prevents energy loss on rough metallic surfaces — critical for CAD materials (brushed aluminum, steel). The `KullaContyLut` textures are passed alongside `EnvironmentRenderer` to `Execute()`.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/DeferredResolve.h` | **shared** | **M** | Deferred resolve API |
| Create | `shaders/rendergraph/deferred_resolve.slang` | internal | L | Compute + fragment shaders |
| Create | `src/miki/rendergraph/DeferredResolve.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_deferred_resolve.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define DeferredResolve.h (shared M)
      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `DeferredResolve::Setup` | `(RenderGraphBuilder&, GBufferLayout, uint32_t w, uint32_t h) -> RGHandle` | `[[nodiscard]]` static — returns HDR output handle |
      | `DeferredResolve::Execute` | `(RenderContext&, GBufferLayout, RGHandle hdrOutput, span<const LightData> lights, EnvironmentRenderer const* env = nullptr)` | static — IBL terms computed inside if env != nullptr |

      **Acceptance**: compiles
      `[verify: compile]`

- [x] **Step 2**: Deferred resolve Slang shaders (compute + fullscreen-tri)
      **Acceptance**: shaders compile
      `[verify: compile]`

- [ ] **Step 3**: Implement resolve pass with tier dispatch
      **Acceptance**: deferred resolve produces lit HDR output on Vulkan. **Currently stub** — Execute() only resolves handles, no GPU commands issued.
      `[verify: test]`

- [x] **Step 4**: Unit tests
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(DeferredResolve, SetupReturnsHDR)` | Positive | HDR output handle valid | 1-3 |
| `TEST(DeferredResolve, SingleDirectionalLight)` | Positive | Lit output non-black | 3-4 |
| `TEST(DeferredResolve, MultiplePointLights)` | Positive | 4 point lights produce distinct illumination | 3-4 |
| `TEST(DeferredResolve, NoLights_AmbientOnly)` | Boundary | Zero lights → ambient contribution only | 3-4 |
| `TEST(DeferredResolve, ComputePath_Vulkan)` | Positive | Compute dispatch on Tier1 | 3-4 |
| `TEST(DeferredResolve, FullscreenTriPath_GL)` | Positive | Fragment shader path on GL | 3-4 |
| `TEST(DeferredResolve, EnergyConservation)` | Positive | BRDF output energy ≤ 1.0 | 3-4 |
| `TEST(DeferredResolve, HDROutputFormat_RGBA16F)` | Positive | Output texture format check | 1-3 |

## Design Decisions

1. **Execute lambda pattern** — `AddToGraph()` accepts `ExecuteFn`; caller captures lights/env/gbuffer via lambda closure (same pattern as GBufferPass).
2. **GPU dispatch deferred (2026-03-16 correction)** — Step 3 was marked `[x]` but Execute() only resolves handles without GPU commands. Reopened. The comment in code (line 88-96 of DeferredResolve.cpp) lists all missing prerequisites: shader compilation, pipeline creation, descriptor set, light SSBO, compute dispatch.

## Implementation Notes

- **Wiring complete**: Setup + AddToGraph + handle resolve work correctly. 0 build errors.
- **GPU dispatch missing**: No compute pipeline, no descriptor sets, no dispatch/draw calls in Execute().
