# T3b.13.1 — ITemporalUpscaler Interface + NativeUpscaler + Quality Modes

**Phase**: 05-3b
**Component**: 13 — Temporal Upscale Interface
**Roadmap Ref**: `roadmap.md` Phase 3b — TAA / Temporal Upscale
**Status**: Complete
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.11.2 | TAA History | Not Started | `Taa`, TAA output RGBA16F |

## Context Anchor

### This Task's Contract

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `ITemporalUpscaler.h` | **public** | **H** | `ITemporalUpscaler` interface, `UpscaleQuality` enum, `UpscaleInput` struct |
| `NativeUpscaler.h` | **shared** | **M** | `NativeUpscaler` -- TAA passthrough (no upscale); default implementation |

- `ITemporalUpscaler`: abstract interface for temporal upscaling. `Upscale(UpscaleInput) -> TextureHandle`.
- `UpscaleQuality` enum: `{ UltraQuality=77%, Quality=67%, Balanced=58%, Performance=50%, Off=100% }`
- `UpscaleInput`: `{ TextureHandle color, TextureHandle depth, TextureHandle motionVectors, TextureHandle reactiveMask, uint32_t renderWidth, renderHeight, displayWidth, displayHeight }`
- `NativeUpscaler`: implements ITemporalUpscaler, returns TAA output directly (no upscale). This is the default until FSR/DLSS SDKs are integrated (Phase 13).
- FSR 3.0 / DLSS 3.5 stubs: `FsrUpscaler` and `DlssUpscaler` classes declared but return `NotImplemented`. Phase 13 provides real implementations.

### Downstream Consumers

- `ITemporalUpscaler.h` (**public** H):
  - T3b.16.2: demo configures upscaler quality mode via ImGui
  - Phase 13: FSR/DLSS SDK integration implements ITemporalUpscaler

### Technical Direction

- **Strategy pattern**: rendering code calls `upscaler->Upscale()` regardless of backend (Native/FSR/DLSS).
- **Render resolution**: when quality < 100%, render at reduced resolution, upscale to display resolution. TAA jitter + motion vectors required for temporal upscale.
- **Off mode**: render at full resolution, skip upscale entirely.
- **SDK detection**: Phase 13 detects available SDKs at init; Phase 3b uses NativeUpscaler always.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/ITemporalUpscaler.h` | **public** | **H** | Interface |
| Create | `include/miki/rendergraph/NativeUpscaler.h` | **shared** | **M** | TAA passthrough |
| Create | `src/miki/rendergraph/NativeUpscaler.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_temporal_upscaler.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define ITemporalUpscaler interface + UpscaleQuality + UpscaleInput
- [x] **Step 2**: Implement NativeUpscaler (TAA passthrough)
- [x] **Step 3**: Declare FsrUpscaler/DlssUpscaler stubs (NotImplemented)
- [x] **Step 4**: Tests

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(ITemporalUpscaler, NativeUpscaler_ReturnsInput)` | Positive | passthrough returns TAA output |
| `TEST(ITemporalUpscaler, QualityMode_Off)` | Positive | Off mode = no upscale |
| `TEST(ITemporalUpscaler, FsrStub_NotImplemented)` | Boundary | FSR stub returns error |
| `TEST(ITemporalUpscaler, DlssStub_NotImplemented)` | Boundary | DLSS stub returns error |
| `TEST(ITemporalUpscaler, RenderResolution_Quality67)` | Positive | render res = 67% of display |
| `TEST(ITemporalUpscaler, InvalidQuality_Error)` | Error | out-of-range quality value returns error |
| `TEST(ITemporalUpscaler, MoveSemantics)` | State | move transfers upscaler, source empty |
| `TEST(ITemporalUpscaler, EndToEnd_NativePassthrough)` | Integration | NativeUpscaler with TAA output produces valid full-res image |
