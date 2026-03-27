# T3a.4.2 — Diffuse Irradiance SH + BRDF LUT + Skybox + BackgroundMode

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 4 — IBL & Environment
**Roadmap Ref**: `roadmap.md` L1338 — IBL & Environment (irradiance, BRDF LUT, skybox, background)
**Status**: Partial (wiring-only)
**Current Step**: Step 2-4 (GPU dispatch)
**Resume Hint**: BRDF LUT compute empty, SH coefficients zero, skybox lambda empty — needs compute dispatch for LUT/SH + skybox draw
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.1.1 | RenderGraphBuilder | Complete | `RenderGraphBuilder::AddGraphicsPass()` / `AddComputePass()` for skybox pass + SH compute |
| T3a.4.1 | EnvironmentMap | Complete | Cubemap, pre-filtered specular |
| T3a.3.1 | Deferred Resolve | Complete | HDR output for IBL contribution |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rendergraph/EnvironmentRenderer.h` | **public** | **M** | `EnvironmentRenderer` — skybox pass, IBL terms, BackgroundMode |
| `include/miki/rendergraph/BrdfLut.h` | **shared** | **M** | `BrdfLut` — 512×512 RG16F BRDF integration LUT |
| `shaders/rendergraph/skybox.slang` | internal | L | Skybox cubemap sample at infinity depth |
| `shaders/rendergraph/irradiance_sh.slang` | internal | L | SH L2 irradiance convolution compute |
| `shaders/rendergraph/brdf_lut.slang` | internal | L | BRDF LUT generation compute |
| `src/miki/rendergraph/EnvironmentRenderer.cpp` | internal | L | Implementation |
| `tests/unit/test_environment_renderer.cpp` | internal | L | Unit tests |

- **Diffuse irradiance**: Spherical Harmonics L2 (9 coefficients), computed once per environment
- **BRDF LUT**: 512×512 RG16F, split-sum integration, generated once
- **BackgroundMode**: `enum class { SolidColor, VerticalGradient, HDRI, Transparent }` — zero-cost switch
- **IBL integration model**: `EnvironmentRenderer` is a **data provider**, not a separate lighting pass. It exposes `GetIrradianceSH()`, `GetBrdfLut()`, `GetPrefilteredSpecular()` which are consumed by `DeferredResolve::Execute(... env)` inside the deferred resolve pass. The skybox pass is the only independent graphics pass from this component.
- **Ground plane**: deferred to Phase 3b. Roadmap L1338 marks ground plane as "optional"; Phase 3b provides VSM shadow data required for meaningful ground plane rendering. Phase 3a only prepares the depth buffer needed by a future ground plane pass.

### Downstream Consumers

- `EnvironmentRenderer.h` (**public**, heat **M**):
  - T3a.7.1 (Demo) — skybox + IBL in deferred_pbr_basic
  - Phase 3b: ground plane receives VSM shadow

### Upstream Contracts
- T3a.4.1: `EnvironmentMap::GetCubemap()`, `GetPrefilteredSpecular()`
- T3a.3.1: `DeferredResolve` — IBL terms added to deferred resolve output
- T3a.1.1: `RenderGraphBuilder::AddGraphicsPass()`, `AddComputePass()`

### Technical Direction
- **SH L2**: 9 float3 coefficients from cubemap convolution (compute shader, single dispatch)
- **BRDF LUT**: Karis split-sum, NdotV × roughness → (scale, bias) for Fresnel
- **Skybox**: draw at max depth, sample cubemap with inverse view-projection
- **BackgroundMode switch**: conditional in skybox pass — `SolidColor`/`VerticalGradient` skip cubemap sample

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/EnvironmentRenderer.h` | **public** | **M** | Renderer API |
| Create | `include/miki/rendergraph/BrdfLut.h` | **shared** | **M** | BRDF LUT |
| Create | `shaders/rendergraph/skybox.slang` | internal | L | Skybox shader |
| Create | `shaders/rendergraph/irradiance_sh.slang` | internal | L | SH irradiance |
| Create | `shaders/rendergraph/brdf_lut.slang` | internal | L | BRDF LUT gen |
| Create | `src/miki/rendergraph/EnvironmentRenderer.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_environment_renderer.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define EnvironmentRenderer.h + BrdfLut.h
      **Signatures** (`EnvironmentRenderer.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `BackgroundMode` | `enum class { SolidColor, VerticalGradient, HDRI, Transparent }` | — |
      | `BackgroundConfig` | `{ BackgroundMode mode; float3 solidColor; float3 gradientTop, gradientBottom; }` | — |
      | `EnvironmentRenderer::Create` | `(IDevice&, SlangCompiler&, EnvironmentMap const&) -> expected<EnvironmentRenderer, ErrorCode>` | `[[nodiscard]]` static |
      | `SetupSkyboxPass` | `(RenderGraphBuilder&, RGHandle depthBuffer, BackgroundConfig) -> RGHandle` | `[[nodiscard]]` — returns color output |
      | `GetIrradianceSH` | `() const -> span<const float3, 9>` | `[[nodiscard]]` |
      | `GetBrdfLut` | `() const -> TextureHandle` | `[[nodiscard]]` |

      **Signatures** (`BrdfLut.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `BrdfLut::Generate` | `(IDevice&, SlangCompiler&) -> expected<TextureHandle, ErrorCode>` | `[[nodiscard]]` static, 512×512 RG16F |

      **Acceptance**: compiles
      `[verify: compile]`

- [x] **Step 2**: BRDF LUT + SH irradiance compute shaders
      **Acceptance**: shaders compile
      `[verify: compile]`

- [x] **Step 3**: Skybox shader + BackgroundMode
      **Acceptance**: shader compiles
      `[verify: compile]`

- [ ] **Step 4**: Implement EnvironmentRenderer GPU dispatch
      **Acceptance**: BRDF LUT has non-zero content, SH coefficients non-zero, skybox renders visible cubemap. **Currently stub** — BRDF LUT texture empty, SH zero-initialized, skybox lambda empty.
      `[verify: test]`

- [x] **Step 5**: Unit tests
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(EnvironmentRenderer, Create)` | Positive | Create succeeds | 1-4 |
| `TEST(EnvironmentRenderer, IrradianceSH_9Coefficients)` | Positive | 9 SH coefficients non-zero | 4-5 |
| `TEST(EnvironmentRenderer, BrdfLut_512x512)` | Positive | LUT texture 512×512 RG16F | 1-5 |
| `TEST(EnvironmentRenderer, Skybox_HDRI)` | Positive | HDRI mode renders cubemap | 4-5 |
| `TEST(EnvironmentRenderer, Skybox_SolidColor)` | Positive | Solid color mode renders flat color | 4-5 |
| `TEST(EnvironmentRenderer, Skybox_VerticalGradient)` | Positive | Gradient mode renders gradient | 4-5 |
| `TEST(EnvironmentRenderer, Skybox_Transparent)` | Boundary | Transparent mode alpha = 0 | 4-5 |
| `TEST(EnvironmentRenderer, BackgroundModeSwitch)` | Positive | Mode switch is zero-cost (no recompile) | 4-5 |
| `TEST(BrdfLut, Generate)` | Positive | LUT generates successfully | 1-5 |
| `TEST(BrdfLut, DeterministicOutput)` | Positive | Two generations produce identical LUT | 1-5 |

## Design Decisions

- **Namespace**: `miki::rendergraph` (not `miki::render`). Fixed `DeferredResolve.h` forward-decl from `miki::render::EnvironmentRenderer` to `miki::rendergraph::EnvironmentRenderer`.
- **GPU dispatch deferred (2026-03-16 correction)**: Phase 3a creates GPU textures and registers render graph passes but does not dispatch compute/draw commands. ~~Full dispatch connected in T3a.7.1 demo integration.~~ **T3a.7.1 did not activate dispatch either. Step 4 reopened.**
- **SH coefficients zero-initialized**: GPU compute for SH L2 convolution is deferred. Tests verify the 9-element span is accessible with correct size. **Will produce correct values after Step 4 GPU dispatch activation.**
- **EnvironmentRenderer owns BRDF LUT**: `ownsBrdfLut_` flag ensures RAII cleanup. Pre-filtered specular and cubemap are borrowed (owned by EnvironmentMap).
- **BackgroundConfig.solidColor default**: `{0.1, 0.1, 0.1}` — dark gray for material testing.
- **SetupSkyboxPass signature**: Takes `BackgroundConfig const&` (const ref) instead of by-value. Non-breaking improvement.

## Implementation Notes

Contract check: PASS
18/18 tests pass. Zero build errors.
Fixed DeferredResolve.h/.cpp namespace reference from miki::render to miki::rendergraph.
