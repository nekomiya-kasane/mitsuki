# T3b.9.1 — SSAO Fallback: Full-Res Fragment + Gaussian Blur (Tier3/4)

**Phase**: 05-3b
**Component**: 9 — SSAO Fallback
**Roadmap Ref**: `rendering-pipeline-architecture.md` S7.3 Pass #16
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
| `Ssao.h` | **public** | **M** | `Ssao` -- kernel-based SSAO, Gaussian blur, Tier3/4 |
| `ssao.slang` | internal | L | SSAO fragment shader (8-16 samples, noise tile) |

- Full-res fragment shader: fullscreen triangle, 8 samples (T3) or 16 samples (T4)
- Noise texture: RGBA8 4x4 random rotation vectors, tiled across screen
- Kernel: hemisphere samples in tangent space, distance-weighted attenuation
- Post-blur: 2-pass separable Gaussian (5-tap) for noise smoothing
- Output: AOBuffer R8_UNORM full-res
- Budget: less than 1ms at 4K

### Downstream Consumers

- T3b.10.1: IAOProvider wraps SSAO output for Tier3/4
- T3b.16.1: IPipelineFactory::CreateAOPass() uses SSAO for Tier3/4

### Technical Direction

- Classic Crytek-style SSAO with hemisphere kernel
- Noise tile: 4x4 RGBA8, random tangent-space rotation per tile
- Kernel: 16 vec3 samples in unit hemisphere, distributed via Hammersley
- Depth comparison: linearized Reverse-Z depth, range check to avoid far-field AO bleeding

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/Ssao.h` | **public** | **M** | Interface |
| Create | `src/miki/rendergraph/Ssao.cpp` | internal | L | Implementation |
| Create | `shaders/rendergraph/ssao.slang` | internal | L | SSAO fragment |
| Create | `shaders/rendergraph/ssao_blur.slang` | internal | L | Gaussian blur |
| Create | `tests/unit/test_ssao.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define Ssao interface (Create/Setup/AddToGraph/Execute)
- [x] **Step 2**: Implement SSAO fragment shader with hemisphere kernel
- [x] **Step 3**: Generate noise texture + kernel samples
- [x] **Step 4**: Implement 2-pass Gaussian blur
- [x] **Step 5**: Tests

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(Ssao, CreateReturnsValid)` | Positive | factory success |
| `TEST(Ssao, OutputFormat_R8UNORM)` | Positive | AOBuffer is R8_UNORM |
| `TEST(Ssao, FlatSurface_HighAO)` | Positive | flat plane AO near 1.0 |
| `TEST(Ssao, Corner_LowAO)` | Positive | corner region AO less than 1.0 |
| `TEST(Ssao, BlurSmoothsNoise)` | Positive | post-blur variance lower than pre-blur |
| `TEST(Ssao, EndToEnd_AOVisible)` | Integration | concave region darker |
| `TEST(Ssao, Create_NullDevice_Error)` | Error | null device returns error |
| `TEST(Ssao, KernelSamples_InHemisphere)` | Positive | all kernel samples have positive z (hemisphere) |
| `TEST(Ssao, NoiseTexture_4x4)` | Positive | noise texture is 4x4 RGBA8, tiled |
| `TEST(Ssao, MoveSemantics)` | State | move transfers pipeline + noise texture, source empty |
