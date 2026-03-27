# T3b.11.2 — TAA History Buffer + YCoCg Clamp + Motion Rejection + Reactive Mask

**Phase**: 05-3b
**Component**: 11 — TAA
**Roadmap Ref**: `rendering-pipeline-architecture.md` S14.7 TAA
**Status**: Complete
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.11.1 | TAA Jitter | Not Started | `TaaJitter`, Halton jitter in CameraUBO |
| T3b.7.1 | Tone Map Operators | Not Started | Post-tone-map LDR input |

## Context Anchor

### This Task's Contract

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `Taa.h` | **public** | **M** | `Taa` -- temporal AA compute pass, history management, reactive mask |
| `taa.slang` | internal | L | TAA compute shader (reproject, YCoCg clamp, blend) |

- History buffer: RGBA16F, same resolution as LDR output. Ping-pong (read prev, write current).
- Reproject: use motion vectors (GBuffer.RT2 RG16F) to find previous frame pixel.
- Neighborhood clamp: convert to YCoCg, 3x3 cross min/max, clamp history to neighborhood.
- Motion rejection: discard history if motion vector magnitude exceeds threshold (disocclusion).
- Reactive mask: R8_UNORM texture. Pixels marked reactive (gizmo/UI/annotation) force current frame weight = 1.0 (no ghosting).
- Blend: lerp(history, current, blendFactor). Default blendFactor = 0.1 (90% history, 10% current).
- Output: AA RGBA16F.
- Budget: less than 0.5ms at 4K.

### Downstream Consumers

- T3b.13.1: ITemporalUpscaler receives TAA output
- T3b.16.1: IPipelineFactory::CreateAAPass() uses TAA for Tier1/2
- T3b.16.2: demo integrates TAA pass

### Technical Direction

- **YCoCg color space**: better perceptual clamping than RGB. Cheap conversion (3 adds, 2 shifts).
- **Catmull-Rom history sample**: bicubic 5-tap for sharper history (avoids blur accumulation).
- **Anti-flicker**: luminance weighting on current frame to reduce flickering on high-contrast edges.
- **Reactive mask integration**: TextRenderer (Phase 3a deferred) and ImGui overlay set reactive mask pixels to 1.0 via stencil or dedicated pass.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/Taa.h` | **public** | **M** | Interface |
| Create | `src/miki/rendergraph/Taa.cpp` | internal | L | Implementation |
| Create | `shaders/rendergraph/taa.slang` | internal | L | TAA compute shader |
| Create | `tests/unit/test_taa.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define Taa interface (Create/Setup/AddToGraph/Execute, history management)
- [x] **Step 2**: Implement history buffer ping-pong management
- [x] **Step 3**: Implement TAA compute shader (reproject + YCoCg clamp + blend)
- [x] **Step 4**: Implement motion rejection + reactive mask
- [x] **Step 5**: Integration with deferred resolve output chain
- [x] **Step 6**: Tests (convergence, ghosting rejection, reactive mask)

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(Taa, CreateReturnsValid)` | Positive | factory success |
| `TEST(Taa, HistoryBufferDimensions)` | Positive | matches output resolution |
| `TEST(Taa, StaticScene_Converges)` | Positive | after 8 frames, output stabilizes (low variance) |
| `TEST(Taa, MotionRejection_Disocclusion)` | Positive | large motion discards history |
| `TEST(Taa, ReactiveMask_ForcesCurrentFrame)` | Positive | reactive pixel = no ghosting |
| `TEST(Taa, YCoCgClamp_NoColorShift)` | Positive | clamped history has no hue drift |
| `TEST(Taa, OutputFormat_RGBA16F)` | Positive | output is RGBA16F |
| `TEST(Taa, NoPrevHistory_FirstFrame)` | Boundary | first frame uses current only |
| `TEST(Taa, BlendFactor_CustomValue)` | Positive | custom blend factor affects sharpness |
| `TEST(Taa, EndToEnd_AAVisible)` | Integration | aliased edges become smooth |
| `TEST(Taa, Create_NullDevice_Error)` | Error | null device returns error |
| `TEST(Taa, MaxMotionVector_FullReject)` | Boundary | very large motion vector fully rejects history |
| `TEST(Taa, MoveSemantics)` | State | move transfers history buffer + pipeline, source empty |
