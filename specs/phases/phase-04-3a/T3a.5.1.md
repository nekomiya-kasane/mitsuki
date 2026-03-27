# T3a.5.1 — ACES Filmic Tone Mapping + Exposure + Final Blit

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 5 — Tone Mapping
**Roadmap Ref**: `roadmap.md` L1339 — Tone Mapping
**Status**: Partial (wiring-only)
**Current Step**: Step 3 (GPU dispatch)
**Resume Hint**: Execute() is stub — needs fullscreen-tri pipeline creation, HDR input SRV binding, exposure push constants, fullscreen-tri draw
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.1.1 | RenderGraphBuilder | Not Started | `RenderGraphBuilder::AddGraphicsPass()` for tone mapping pass declaration |
| T3a.3.1 | Deferred Resolve | Not Started | HDR output texture |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rendergraph/ToneMapping.h` | **shared** | **M** | `ToneMapping` — ACES filmic + exposure render graph node |
| `shaders/rendergraph/tone_mapping.slang` | internal | L | ACES filmic shader |
| `src/miki/rendergraph/ToneMapping.cpp` | internal | L | Implementation |
| `tests/unit/test_tone_mapping.cpp` | internal | L | Unit tests |

- **ACES filmic**: Narkowicz 2015 fitted curve (simple, predictable)
- **Exposure**: manual exposure value (auto-exposure deferred to Phase 3b)
- **Final blit**: HDR → LDR (RGBA8 sRGB) via fullscreen triangle

### Downstream Consumers

- `ToneMapping.h` (**shared**, heat **M**):
  - T3a.7.1 (Demo) — final output for display
  - Phase 3b: TAA operates pre-tone-map; tone mapping is final step

### Upstream Contracts
- T3a.3.1: HDR output (`RGHandle` from deferred resolve)
- T3a.1.1: `RenderGraphBuilder::AddGraphicsPass()`

### Technical Direction
- **ACES Narkowicz fit**: `color = (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14)`
- **Gamma correction**: linear → sRGB in shader (or use sRGB render target format)
- **Exposure uniform**: `exposedColor = hdrColor * exp2(exposure)`

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/ToneMapping.h` | **shared** | **M** | Tone map API |
| Create | `shaders/rendergraph/tone_mapping.slang` | internal | L | ACES shader |
| Create | `src/miki/rendergraph/ToneMapping.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_tone_mapping.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define ToneMapping.h (shared M)
      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `ToneMappingMode` | `enum class : uint8_t { AcesNarkowicz, Neutral /* Khronos PBR Neutral — stub in Phase 3a, implemented Phase 3b+ */, Reinhard /* placeholder */ }` | — |
      | `ToneMapping::Setup` | `(RenderGraphBuilder&, RGHandle hdrInput, uint32_t w, uint32_t h) -> RGHandle` | `[[nodiscard]]` static — returns LDR output |
      | `ToneMapping::Execute` | `(RenderContext&, RGHandle hdrInput, RGHandle ldrOutput, float exposure, ToneMappingMode mode = ToneMappingMode::AcesNarkowicz)` | static — Phase 3a only implements AcesNarkowicz; other modes assert-fail |

      **Acceptance**: compiles
      `[verify: compile]`

- [x] **Step 2**: ACES Slang shader
      **Acceptance**: shader compiles
      `[verify: compile]`

- [ ] **Step 3**: Implement tone mapping pass GPU dispatch
      **Acceptance**: HDR → LDR conversion produces valid output (non-zero LDR pixels). **Currently stub** — Execute() only resolves handles, no pipeline/draw.
      `[verify: test]`

- [x] **Step 4**: Unit tests
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(ToneMapping, SetupReturnsLDR)` | Positive | LDR output handle valid | 1-3 |
| `TEST(ToneMapping, ACES_WhitePreserved)` | Positive | White input maps to near-white | 3-4 |
| `TEST(ToneMapping, ACES_BlackPreserved)` | Positive | Black input maps to black | 3-4 |
| `TEST(ToneMapping, ExposureScaling)` | Positive | Higher exposure → brighter output | 3-4 |
| `TEST(ToneMapping, HDR_HighExposure_Clamp)` | Boundary | Extreme exposure (e.g. +10 EV) output clamped to [0,1] after ACES | 3-4 |
| `TEST(ToneMapping, UnsupportedMode_AssertFails)` | Error | ToneMappingMode::Neutral asserts in Phase 3a (debug build) | 1-3 |

## Design Decisions

- Added `AddToGraph()` static method (same pattern as `DeferredResolve`) for RG pass registration.
- Shader uses IEC 61966-2-1 piecewise linear→sRGB conversion (not `pow(1/2.2)` approximation).
- Neutral/Reinhard modes are `saturate(hdr)` passthrough in shader; C++ `Execute()` assert-fails on non-ACES mode.
- **GPU dispatch deferred (2026-03-16 correction)**: Step 3 was marked `[x]` but Execute() only resolves handles. Reopened.

## Implementation Notes

Contract check: PASS
