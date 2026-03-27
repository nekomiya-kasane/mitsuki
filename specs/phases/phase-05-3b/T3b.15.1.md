# T3b.15.1 — Visual Regression Framework (Headless Render + PNG Capture + Golden Diff + CI)

**Phase**: 05-3b
**Component**: 15 — Visual Regression CI
**Roadmap Ref**: `roadmap.md` Phase 3b — Visual Regression
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
| `VisualRegression.h` | **shared** | **M** | `CaptureGoldenImage()`, `CompareGoldenImage()`, `DiffResult` |
| `VisualRegression.cpp` | internal | L | PNG capture + RMSE diff + diff image generation |

- **Headless render**: use existing `OffscreenTarget` from Phase 1b. Render scene at fixed resolution (e.g., 1920x1080).
- **PNG capture**: readback via `CopyTextureToBuffer` + `ReadbackBuffer`. Encode to PNG via stb_image_write.
- **Golden image diff**: per-pixel RMSE between captured frame and reference golden image. Generate diff image (abs difference amplified).
- **DiffResult**: `{ float psnr; float rmse; std::filesystem::path diffImagePath; bool passed; }`
- **Pass criteria**: PSNR > 30 dB AND RMSE < 0.02 (configurable per test)
- **CI integration**: test executable returns non-zero on failure. Golden images stored in `tests/golden/` per-backend.
- **Per-backend**: separate golden images for Vulkan, D3D12, GL, WebGPU, Compat (minor numerical differences expected).

### Downstream Consumers

- T3b.16.2: demo generates golden images on first run; subsequent runs compare
- Phase 11: visual regression framework reused for debug overlay tests
- All future phases: golden image CI catches visual regressions

### Technical Direction

- **Deterministic rendering**: TAA jitter is deterministic (Halton). No random seeds. Same scene + camera = identical frame.
- **stb_image_write**: PNG encoding. No external dependency.
- **Diff visualization**: `abs(captured - golden) * 10.0` clamped to [0,1], written as PNG. Red channel shows differences.
- **Threshold config**: per-test JSON or compile-time constants. Default: PSNR > 30, RMSE < 0.02.
- **CI workflow**: GitHub Actions runs headless Vulkan (lavapipe) or swiftshader for CI. D3D12/GL tested on self-hosted runners.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/test/VisualRegression.h` | **shared** | **M** | Interface |
| Create | `src/miki/test/VisualRegression.cpp` | internal | L | Implementation |
| Modify | `tests/CMakeLists.txt` | internal | L | Add visual regression lib |
| Create | `tests/golden/.gitkeep` | internal | L | Golden image directory |
| Create | `tests/unit/test_visual_regression.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Define VisualRegression interface (CaptureGoldenImage, CompareGoldenImage, DiffResult)
- [x] **Step 2**: Implement PNG capture from OffscreenTarget readback
- [x] **Step 3**: Implement RMSE/PSNR comparison + diff image generation
- [x] **Step 4**: CI integration (test harness, threshold config)
- [x] **Step 5**: Tests

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(VisualRegression, CaptureProducesPNG)` | Positive | PNG file written to disk |
| `TEST(VisualRegression, IdenticalImages_Pass)` | Positive | same image vs itself = PSNR infinity, pass |
| `TEST(VisualRegression, DifferentImages_Fail)` | Positive | random noise vs solid = PSNR low, fail |
| `TEST(VisualRegression, DiffImageGenerated)` | Positive | diff PNG exists and is valid |
| `TEST(VisualRegression, ThresholdConfigurable)` | Positive | custom threshold changes pass/fail |
| `TEST(VisualRegression, EndToEnd_RenderAndCompare)` | Integration | render scene, compare to reference |
| `TEST(VisualRegression, InvalidGoldenPath_Error)` | Error | non-existent golden path returns error |
| `TEST(VisualRegression, DifferentResolution_Error)` | Boundary | captured vs golden different resolution returns error |
| `TEST(VisualRegression, MoveSemantics)` | State | DiffResult is copyable/movable |
