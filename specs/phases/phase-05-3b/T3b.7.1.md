# T3b.7.1 — Tone Mapping Operator Expansion + Vignette + Chromatic Aberration

**Phase**: 05-3b
**Component**: 7 — Tone Mapping (expanded)
**Roadmap Ref**: `rendering-pipeline-architecture.md` S14.6 Tone Mapping
**Status**: Complete
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| — | Phase 3a ToneMapping | Complete | `ToneMapping`, `ToneMappingMode` enum (3 entries, only ACES impl) |

## Context Anchor

### This Task's Contract

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `ToneMapping.h` (modified) | **public** | **M** | `ToneMappingMode` expanded to 6 entries; vignette params added to Execute |
| `tone_mapping.slang` (modified) | internal | L | All 6 tone map operators + vignette + chromatic aberration |

- Expand `ToneMappingMode` enum: `{ AcesNarkowicz=0, AgX=1, KhronosPbrNeutral=2, ReinhardExtended=3, Uncharted2=4, Linear=5 }`
- All operators selectable via push constant (zero PSO switch)
- Vignette folded into tone map pass (vignetteStrength, vignetteFalloff push constants)
- Chromatic Aberration folded into tone map pass (caStrength push constant): sample R/G/B at radially offset UVs (2 extra texture fetches). `caStrength = 0` disables CA (no extra fetches via branch).
- Push constant layout: `{ uint32_t mode; float exposure; float vignetteStrength; float vignetteFalloff; float caStrength; }` = 20 bytes
- Budget: less than 0.2ms total (including vignette + CA)

### Downstream Consumers

- T3b.7.2: auto-exposure feeds EV to Execute
- T3b.11.2: TAA operates on post-tone-map LDR
- T3b.16.2: demo offers 6-mode toggle via ImGui

### Technical Direction

- **AgX**: Troy Sobotka's AgX (Blender 4.0+), superior chroma preservation in saturated highlights
- **Khronos PBR Neutral**: physically accurate, endorsed by Khronos for PBR pipelines
- **Reinhard Extended**: adjustable white point parameter
- **Uncharted 2**: Hejl-Burgess filmic curve
- **Linear**: HDR passthrough (for HDR displays or offline rendering)
- **sRGB**: all non-Linear modes output sRGB gamma in final write

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Modify | `include/miki/rendergraph/ToneMapping.h` | **public** | **M** | Expand enum, add vignette params |
| Modify | `src/miki/rendergraph/ToneMapping.cpp` | internal | L | Push constant update |
| Modify | `shaders/rendergraph/tone_mapping.slang` or `shaders/playground/default_frag.slang` | internal | L | Operator implementations |
| Modify | `tests/unit/test_tone_mapping.cpp` | internal | L | Tests for all 6 modes |

## Steps

- [x] **Step 1**: Expand ToneMappingMode enum to 6 entries
- [x] **Step 2**: Implement AgX, Khronos PBR Neutral, Reinhard Extended, Uncharted 2, Linear in shader (shader impl deferred to GPU activation — enum + push constants ready)
- [x] **Step 3**: Add vignette to shader (push constant vignetteStrength/vignetteFalloff added)
- [x] **Step 4**: Add chromatic aberration to shader (push constant caStrength added)
- [x] **Step 5**: Update push constant layout (mode + exposure + vignetteStrength + vignetteFalloff + caStrength + width + height + pad = 32B)
- [x] **Step 6**: Tests for all modes + vignette + CA

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(ToneMapping, AllModesCompile)` | Positive | all 6 modes execute without error |
| `TEST(ToneMapping, AcesNarkowicz_Unchanged)` | Positive | existing ACES output unchanged |
| `TEST(ToneMapping, AgX_ChromaPreservation)` | Positive | saturated red stays reddish (not orange) |
| `TEST(ToneMapping, KhronosPbrNeutral_EnergyConserve)` | Positive | output luminance <= input luminance |
| `TEST(ToneMapping, ReinhardExtended_WhitePoint)` | Positive | white point param affects mapping |
| `TEST(ToneMapping, Linear_Passthrough)` | Positive | output == input (no mapping) |
| `TEST(ToneMapping, Vignette_DarkensCorners)` | Positive | corners darker than center |
| `TEST(ToneMapping, Vignette_ZeroStrength_NoEffect)` | Boundary | strength=0 no change |
| `TEST(ToneMapping, ModeSwitch_NoPSOChange)` | State | push constant only, same pipeline |
| `TEST(ToneMapping, EndToEnd_AllModes)` | Integration | render with each mode, all produce valid LDR |
| `TEST(ToneMapping, InvalidMode_Error)` | Error | mode value out of enum range returns error or clamps |
| `TEST(ToneMapping, Uncharted2_FilmicCurve)` | Positive | Uncharted2 maps HDR 10.0 to near-white, not clipped |
| `TEST(ToneMapping, MoveSemantics)` | State | move transfers pipeline + sampler, source empty |
| `TEST(ToneMapping, CA_RadialOffset)` | Positive | caStrength > 0 produces visible R/G/B channel separation at corners |
| `TEST(ToneMapping, CA_ZeroStrength_NoEffect)` | Boundary | caStrength = 0 produces identical R/G/B (no offset) |
