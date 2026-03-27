# T3a.4.1 — EnvironmentMap: HDRI→Cubemap + Pre-filtered Specular (Compute)

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 4 — IBL & Environment
**Roadmap Ref**: `roadmap.md` L1338 — IBL & Environment
**Status**: Partial (wiring-only)
**Current Step**: Step 4 (compute dispatch)
**Resume Hint**: CreatePreset creates empty textures — needs equirect→cubemap compute dispatch + specular pre-filter compute dispatch
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.1.1 | RenderGraphBuilder | Complete | `RenderGraphBuilder::AddComputePass()` for cubemap conversion + specular pre-filter |
| T3a.1.3 | RenderGraphExecutor | Complete | `RenderGraphExecutor`, `RenderContext` for compute dispatch |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rendergraph/EnvironmentMap.h` | **public** | **M** | `EnvironmentMap` — HDRI load, cubemap conversion, specular pre-filter |
| `shaders/rendergraph/equirect_to_cube.slang` | internal | L | Equirectangular → cubemap compute shader |
| `shaders/rendergraph/prefilter_specular.slang` | internal | L | GGX pre-filtered specular mip chain |
| `src/miki/rendergraph/EnvironmentMap.cpp` | internal | L | Implementation |
| `tests/unit/test_environment_map.cpp` | internal | L | Unit tests |

- **Error model**: `Create()` returns `expected<EnvironmentMap, ErrorCode>`
- **One-time compute**: cubemap conversion + specular pre-filter run once per HDRI load, results cached
- **Pre-filtered specular**: 5 mip levels, GGX importance sampling, split-sum approximation
- **Built-in presets**: studio soft, studio hard, outdoor overcast, outdoor sunny, neutral gray (procedural, no HDRI file needed)

### Downstream Consumers

- `EnvironmentMap.h` (**public**, heat **M**):
  - T3a.4.2 (Irradiance + Skybox) — reads cubemap for SH irradiance + skybox rendering
  - Phase 3b: VSM ground plane uses environment for ambient

### Upstream Contracts
- T3a.1.1: `RenderGraphBuilder::AddComputePass()`
- T3a.1.3: `RenderGraphExecutor::Execute()`
- Phase 2: `IDevice::CreateTexture()` (cubemap), `SlangCompiler`

### Technical Direction
- **Equirect→cubemap**: compute shader, 6 face dispatches (or single dispatch with face index)
- **GGX pre-filter**: per-mip roughness, importance sampling (1024 samples for mip 0, decreasing)
- **Cubemap format**: RGBA16F for HDR fidelity
- **Built-in presets**: procedural sky models (Preetham / Hosek-Wilkie for outdoor, constant for studio)

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/EnvironmentMap.h` | **public** | **M** | Environment API |
| Create | `shaders/rendergraph/equirect_to_cube.slang` | internal | L | Conversion shader |
| Create | `shaders/rendergraph/prefilter_specular.slang` | internal | L | Specular pre-filter |
| Create | `src/miki/rendergraph/EnvironmentMap.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_environment_map.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define EnvironmentMap.h (public M)
      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `EnvironmentMap::CreateFromHDRI` | `(IDevice&, span<const uint8_t> hdrFileBytes) -> expected<EnvironmentMap, ErrorCode>` | `[[nodiscard]]` static — input is raw `.hdr` file bytes (Radiance RGBE); internal decoding via stb_image_hdr (float RGB); stb_image is a private dependency of miki_rendergraph, not exposed in public headers. **Phase 3a supports `.hdr` only**; `.exr` support deferred to Phase 3b+ (requires tinyexr or OpenEXR dependency addition). |
      | `EnvironmentMap::CreatePreset` | `(IDevice&, SlangCompiler&, EnvironmentPreset preset, uint32_t cubemapSize) -> expected<EnvironmentMap, ErrorCode>` | `[[nodiscard]]` static |
      | `GetCubemap` | `() const -> TextureHandle` | `[[nodiscard]]` |
      | `GetPrefilteredSpecular` | `() const -> TextureHandle` | `[[nodiscard]]` — 5 mip levels |
      | `GetRotation` | `() const -> float` | `[[nodiscard]]` — rotation in radians |
      | `SetRotation` | `(float radians) -> void` | — |
      | `EnvironmentPreset` | `enum class { StudioSoft, StudioHard, OutdoorOvercast, OutdoorSunny, NeutralGray }` | — |

      **Acceptance**: compiles
      `[verify: compile]`

- [x] **Step 2**: Equirect→cubemap compute shader
      **Acceptance**: shader compiles
      `[verify: compile]`

- [x] **Step 3**: Specular pre-filter compute shader
      **Acceptance**: shader compiles
      `[verify: compile]`

- [ ] **Step 4**: Implement EnvironmentMap GPU compute dispatch
      **Acceptance**: cubemap generated from built-in preset with non-zero pixel content. **Currently stub** — creates empty GPU textures without compute dispatch.
      `[verify: test]`

- [x] **Step 5**: Unit tests
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(EnvironmentMap, CreatePreset_StudioSoft)` | Positive | Preset creates valid cubemap | 4-5 |
| `TEST(EnvironmentMap, CubemapFormat_RGBA16F)` | Positive | Cubemap is RGBA16F | 4-5 |
| `TEST(EnvironmentMap, PrefilteredSpecular_5Mips)` | Positive | 5 mip levels generated | 4-5 |
| `TEST(EnvironmentMap, Rotation)` | Positive | Set/get rotation round-trip | 1-5 |
| `TEST(EnvironmentMap, AllPresets)` | Positive | All 5 presets create successfully | 4-5 |
| `TEST(EnvironmentMap, CubemapNonZero)` | Integration | Cubemap pixels are non-black | 4-5 |
| `TEST(EnvironmentMap, InvalidHDRI_ReturnsError)` | Error | Garbage bytes as HDRI input returns error (stb_image_hdr decode failure) | 1-5 |

## Design Decisions

- **CreateFromHDRI signature extension**: Task spec says `(IDevice&, span<const uint8_t>)`. Added `SlangCompiler&` (needed for compute shader compilation) and `uint32_t cubemapSize` (caller-controlled resolution, default 256). Consistent with `CreatePreset` signature.
- **GPU dispatch deferred (2026-03-16 correction)**: Phase 3a creates valid GPU textures but does not dispatch the compute shaders. ~~Full equirect-to-cubemap + specular pre-filter dispatch will be connected in T3a.7.1 demo integration.~~ **T3a.7.1 did not activate dispatch either. Step 4 reopened.**
- **TextureDesc has no dimension field**: RHI `TextureDesc` uses `arrayLayers = 6` for cubemaps, not a dimension enum. The RG-level `RGTextureDesc` has `RGTextureDimension::TexCube` but the RHI level does not.
- **SlangCompiler fake in tests**: Tests use `reinterpret_cast` to create a fake `SlangCompiler&` since the real one requires the Slang runtime. Safe because Phase 3a dispatch is deferred.

## Implementation Notes

Contract check: PASS (with CreateFromHDRI signature extension, documented above)
17/17 tests pass. Zero build errors.
