# T3b.12.1 — FXAA 3.11 Fragment Shader + Luma-in-Alpha (Tier3/4)

**Phase**: 05-3b
**Component**: 12 — FXAA Fallback
**Roadmap Ref**: `rendering-pipeline-architecture.md` S14.7 FXAA
**Status**: Complete
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.7.1 | Tone Map Operators | Not Started | Post-tone-map LDR with luma prep |

## Context Anchor

### This Task's Contract

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `Fxaa.h` | **public** | **M** | `Fxaa` -- FXAA 3.11 quality 29, luma-in-alpha prep pass |
| `fxaa.slang` | internal | L | FXAA fragment shader |

- FXAA 3.11, quality preset 29 (high quality, 12 search steps)
- Input: RGBA8 with luma in alpha channel (or separate luma compute pass)
- Luma prep: single compute/fragment pass writing `rgb2luma(color)` into alpha
- Output: AA RGBA8
- Tier3/4 only (TAA unavailable)
- Budget: less than 0.5ms at 4K

### Downstream Consumers

- T3b.16.1: IPipelineFactory::CreateAAPass() uses FXAA for Tier3/4
- T3b.16.2: demo applies FXAA on Tier3/4 backends

### Technical Direction

- **FXAA 3.11**: Timothy Lottes's algorithm. Edge detection via luma contrast, sub-pixel shift along edge direction.
- **Luma-in-alpha**: tone mapping pass writes `dot(rgb, vec3(0.299, 0.587, 0.114))` into alpha. FXAA reads alpha for edge detection.
- **Quality 29**: 12 search steps, good balance of quality vs performance for CAD use.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/Fxaa.h` | **public** | **M** | Interface |
| Create | `src/miki/rendergraph/Fxaa.cpp` | internal | L | Implementation |
| Create | `shaders/rendergraph/fxaa.slang` | internal | L | FXAA fragment |
| Create | `tests/unit/test_fxaa.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define Fxaa interface (Create/Setup/AddToGraph/Execute)
- [x] **Step 2**: Implement luma-in-alpha prep (in tone mapping pass or separate)
- [x] **Step 3**: Port FXAA 3.11 to Slang
- [x] **Step 4**: Tests

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(Fxaa, CreateReturnsValid)` | Positive | factory success |
| `TEST(Fxaa, OutputFormat_RGBA8)` | Positive | output is RGBA8 |
| `TEST(Fxaa, LumaInAlpha_Populated)` | Positive | alpha channel contains luma |
| `TEST(Fxaa, EdgeSmoothing)` | Positive | aliased edge becomes smoother |
| `TEST(Fxaa, EndToEnd_AAVisible)` | Integration | diagonal lines less jagged |
| `TEST(Fxaa, Create_NullDevice_Error)` | Error | null device returns error |
| `TEST(Fxaa, UniformInput_NoChange)` | Boundary | uniform color input passes through near-identical |
| `TEST(Fxaa, MoveSemantics)` | State | move transfers pipeline, source empty |
