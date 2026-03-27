# T3a.7.2 — ForwardPass → RG-native Pass

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 7 — 5-Backend Sync + Demo + Phase 2 Debt Cleanup
**Roadmap Ref**: `roadmap.md` — Phase 2 rendering debt cleanup
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.1.3 | RenderGraphExecutor | Complete | `RenderGraphExecutor::Execute()`, `RenderContext` |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/render/ForwardPass.h` | **public** | **H** | `ForwardPass` — RG-native API: `Setup()`, `AddToGraph()`, `Execute()` |
| `src/miki/render/ForwardPass.cpp` | internal | L | Implementation |
| `tests/unit/test_forward_pass.cpp` | internal | L | Updated unit tests |

**Migration scope**:

1. **Remove `BeginPass()`/`EndPass()`**: the render graph executor owns `BeginRendering()`/`EndRendering()` and barrier insertion. These hand-coded barriers in `BeginPass()` violate the RG contract.
2. **Add `Setup()` + `AddToGraph()`**: follow GBufferPass/DeferredResolve pattern. `Setup()` creates/imports the color+depth targets in the RG. `AddToGraph()` registers a graphics pass with read/write edges.
3. **Move `EnsureMaterialDescriptor` to `PrepareMaterials()`**: a frame-start pre-pass that ensures all material descriptor sets exist before the execute lambda runs. Called before `RenderGraphExecutor::Execute()`.
4. **`Execute()`**: static method called from the execute lambda. Records: bind pipeline, bind per-frame set, sort+iterate draws, push model matrix, draw indexed.
5. **Keep `Create()`/destructor/RAII**: resource ownership unchanged (pipeline, layouts, UBOs, LUT).

- **Error model**: `std::expected<T, ErrorCode>` — no exceptions
- **Thread safety**: single-threaded (render graph execution)
- **Backward compatibility**: `forward_cubes` demo and integration tests must be updated to use the new API

### Downstream Consumers

- `forward_cubes` demo — uses ForwardPass for rendering
- `test_forward_pass.cpp` — unit tests
- `test_forward_pass_integration.cpp` — integration tests
- `test_forward_cubes.cpp` — integration tests
- `deferred_pbr_basic` demo — references ForwardPass (but uses deferred pipeline)

### Upstream Contracts
- T3a.1.3: `RenderGraphExecutor::Execute()`, `RenderContext`, `PassData`
- T3a.1.1: `RenderGraphBuilder`, `PassBuilder`, `ExecuteFn`, `SetupFn`
- Phase 2: `IDevice`, `ICommandBuffer`, `IPipelineFactory`, `StagingUploader`, `MaterialRegistry`, `StandardPBR`

### Technical Direction
- **Pattern**: follow GBufferPass/DeferredResolve/ToneMapping: `Setup()` → `AddToGraph()` → `Execute()`
- **Barriers**: all barriers handled by `RenderGraphCompiler` + `RenderGraphExecutor`. Zero manual `PipelineBarrier` calls.
- **Material descriptors**: `PrepareMaterials()` called before graph execution; execute lambda only binds pre-existing sets.
- **UBO upload**: `UploadFrameData()` retained but simplified (no barrier responsibility).

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Modify | `include/miki/render/ForwardPass.h` | **public** | **H** | Replace BeginPass/EndPass with Setup/AddToGraph/Execute |
| Modify | `src/miki/render/ForwardPass.cpp` | internal | L | Remove hand-coded barriers, add RG-native methods |
| Modify | `demos/forward_cubes/main.cpp` | internal | L | Update to use RG-native ForwardPass API |
| Modify | `tests/unit/test_forward_pass.cpp` | internal | L | Update tests for new API |
| Modify | `tests/integration/test_forward_pass_integration.cpp` | internal | L | Update integration tests |

## Steps

- [ ] **Step 1**: Modify ForwardPass.h — replace BeginPass/EndPass with Setup/AddToGraph/Execute + PrepareMaterials
      **Files**: `include/miki/render/ForwardPass.h` (**public** H)
      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [ ] **Step 2**: Implement new API in ForwardPass.cpp — Setup, AddToGraph, Execute, PrepareMaterials; remove BeginPass/EndPass barriers
      **Files**: `src/miki/render/ForwardPass.cpp` (internal L)
      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [ ] **Step 3**: Update forward_cubes demo to use RG-native ForwardPass
      **Files**: `demos/forward_cubes/main.cpp` (internal L)
      **Acceptance**: compiles and runs on Vulkan offscreen
      `[verify: compile]`

- [ ] **Step 4**: Update unit tests + integration tests
      **Files**: `tests/unit/test_forward_pass.cpp`, `tests/integration/test_forward_pass_integration.cpp` (internal L)
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(ForwardPass, SetupCreatesResources)` | Positive | Setup returns valid RG handles | 4 |
| `TEST(ForwardPass, AddToGraphRegistersPass)` | Positive | Pass registered in builder | 4 |
| `TEST(ForwardPass, PrepareMaterialsCreatesDescriptors)` | Positive | Material descriptors created for draw list | 4 |
| `TEST(ForwardPass, ExecuteRecordsDraw)` | Positive | Execute records draw commands | 4 |
| `TEST(ForwardPass, SetupRejectsZeroDimensions)` | Boundary | Zero width/height rejected | 4 |
| `TEST(ForwardPass, ExecuteEmptyDrawList)` | Boundary | Empty draw list handled gracefully | 4 |
| `TEST(ForwardPass, EndToEnd_RGForwardRender)` | Integration | Full RG build+compile+execute with ForwardPass | 4 |

## Design Decisions

*(Fill during implementation)*

## Implementation Notes

*(Fill during implementation)*
