# T3b.11.1 — TAA Jitter Integration into CameraUBO + Projection Offset

**Phase**: 05-3b
**Component**: 11 — TAA
**Roadmap Ref**: `rendering-pipeline-architecture.md` S14.7 TAA
**Status**: Complete
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.1.2 | Tech Debt B | Not Started | FrameResources, per-frame-in-flight |

## Context Anchor

### This Task's Contract

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `TaaJitter.h` | **public** | **M** | `TaaJitter` -- Halton(2,3) 8-sample sequence, projection matrix sub-pixel offset |
| `CameraUBO.h` (modified) | **public** | **H** | Existing `jitterX`/`jitterY` fields now populated by TaaJitter |

- Halton(2,3) low-discrepancy sequence, 8-sample cycle
- Sub-pixel offset: jitter applied to projection matrix NDC xy translation ([-0.5, +0.5] pixel)
- CameraUBO `jitterX`/`jitterY` already exist (Phase 3a T3b.7.3) -- this task populates them
- Frame index drives sequence: `jitterIndex = frameIndex % 8`
- Unjitter for motion vectors: `prevJitter` stored for correct motion vector computation

### Downstream Consumers

- T3b.11.2: TAA history/resolve reads jitter to unjitter current frame
- T3b.16.2: demo applies jitter each frame
- Phase 7a-2: motion vectors use jitter for accurate reprojection

### Technical Direction

- **Halton sequence**: `Halton(i, base)` = van der Corput in given base. Precomputed 8-entry LUT.
- **Projection offset**: `proj[2][0] += jitterX * 2.0 / width; proj[2][1] += jitterY * 2.0 / height;`
- **CameraUBO update**: fill `jitterX`, `jitterY` from Halton sequence. Store `prevJitter` for motion vector unjitter.
- **Deterministic**: same frameIndex = same jitter = deterministic rendering (golden image CI compatible).

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/TaaJitter.h` | **public** | **M** | Jitter utility |
| Create | `src/miki/rendergraph/TaaJitter.cpp` | internal | L | Halton computation |
| Create | `tests/unit/test_taa_jitter.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define TaaJitter utility (Halton sequence, Apply to projection matrix)
- [x] **Step 2**: Implement Halton(2,3) 8-sample LUT
- [x] **Step 3**: Implement projection offset + CameraUBO jitter fill
- [x] **Step 4**: Tests

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(TaaJitter, HaltonSequence_8Samples)` | Positive | 8 unique jitter offsets |
| `TEST(TaaJitter, HaltonRange)` | Positive | all offsets in [-0.5, 0.5] |
| `TEST(TaaJitter, Deterministic)` | Positive | same index = same jitter |
| `TEST(TaaJitter, CycleWraps)` | Positive | index 8 == index 0 |
| `TEST(TaaJitter, ApplyToProjection)` | Positive | modified proj differs from original |
| `TEST(TaaJitter, UnjitterRestoresOriginal)` | Positive | unjitter(jittered) ~= original |
| `TEST(TaaJitter, CameraUBO_JitterFilled)` | Positive | jitterX/Y populated after apply |
| `TEST(TaaJitter, Frame0_Valid)` | Boundary | frame index 0 produces valid non-zero jitter |
| `TEST(TaaJitter, NegativeIndex_Clamped)` | Error | negative frame index clamped to 0 or wraps correctly |
| `TEST(TaaJitter, EndToEnd_ProjectionJittered)` | Integration | render with jitter produces sub-pixel offset visible in readback |
