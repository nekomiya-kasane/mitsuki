# T3b.7.2 — Auto-Exposure Histogram Compute + Temporal Adaptation

**Phase**: 05-3b
**Component**: 7 — Tone Mapping (expanded)
**Roadmap Ref**: `rendering-pipeline-architecture.md` S14.6 Auto-exposure
**Status**: Complete
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.7.1 | Tone Map Operators | Complete | Expanded ToneMapping, exposure push constant |

## Context Anchor

### This Task's Contract

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `AutoExposure.h` | **public** | **M** | `AutoExposure` -- histogram compute, percentile EV, temporal smooth |
| `auto_exposure.slang` | internal | L | Histogram compute shader (256-bin log2 luminance) |

- Histogram compute: 256 bins, log2 luminance range [-10, +10] EV. Dispatch ceil(w/16)*ceil(h/16).
- Percentile-based EV: discard bottom 5% and top 2% of histogram, compute weighted average of remaining bins.
- Temporal adaptation: exponential moving average with configurable speed (default 1.5 EV/s).
- Output: single float EV value fed to ToneMapping::Execute exposure parameter.
- Budget: less than 0.1ms GPU.

### Downstream Consumers

- T3b.16.2: demo feeds AutoExposure EV to ToneMapping
- Phase 7a-2: auto-exposure remains active in full pipeline

### Technical Direction

- Two-pass compute: (1) per-pixel log2(luminance) bin into shared memory histogram, then atomicAdd to global; (2) single-workgroup reduction to compute percentile average.
- Temporal smoothing on CPU: read back single float, lerp toward target at adaptation speed * deltaTime.
- Manual override: if user sets fixed exposure, AutoExposure is bypassed (conditional RG node).

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/AutoExposure.h` | **public** | **M** | Interface |
| Create | `src/miki/rendergraph/AutoExposure.cpp` | internal | L | Implementation |
| Create | `shaders/rendergraph/auto_exposure.slang` | internal | L | Histogram compute |
| Create | `tests/unit/test_auto_exposure.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define AutoExposure interface (Create/Compute/GetEV)
- [x] **Step 2**: Implement histogram compute shader (GPU dispatch deferred — CPU fallback active)
- [x] **Step 3**: Implement percentile reduction + temporal adaptation (CPU-side temporal lerp)
- [x] **Step 4**: Tests

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(AutoExposure, CreateReturnsValid)` | Positive | factory success |
| `TEST(AutoExposure, UniformScene_NeutralEV)` | Positive | uniform 1.0 luminance gives EV near 0 |
| `TEST(AutoExposure, BrightScene_NegativeEV)` | Positive | bright scene lowers exposure |
| `TEST(AutoExposure, DarkScene_PositiveEV)` | Positive | dark scene raises exposure |
| `TEST(AutoExposure, TemporalSmoothing)` | Positive | EV changes gradually over frames |
| `TEST(AutoExposure, PercentileClip)` | Positive | extreme outliers do not dominate |
| `TEST(AutoExposure, ManualOverride_Bypass)` | Boundary | manual exposure ignores histogram |
| `TEST(AutoExposure, EndToEnd_AdaptToSceneChange)` | Integration | exposure adapts after brightness change |
| `TEST(AutoExposure, Create_NullDevice_Error)` | Error | null device returns error |
| `TEST(AutoExposure, AllBlackScene)` | Boundary | all-black scene produces max positive EV, not NaN |
| `TEST(AutoExposure, MoveSemantics)` | State | move transfers histogram buffer, source empty |
