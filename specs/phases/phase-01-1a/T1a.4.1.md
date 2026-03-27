# T1a.4.1 — IPipelineFactory + Main/Compat Factory Stubs

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: IPipelineFactory
**Roadmap Ref**: `roadmap.md` L327 — Abstract factory: `CreateGeometryPass()` etc. Main (Task/Mesh, Tier1) + Compat (Vertex+MDI, Tier2/3/4)
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.3.2 | IDevice + ICommandBuffer | Not Started | `IDevice`, `PipelineHandle`, `Result<T>` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rhi/IPipelineFactory.h` | **public** | **H** | `IPipelineFactory` abstract — `CreateGeometryPass()`, `CreateShadowPass()`, etc. `MainPipelineFactory` + `CompatPipelineFactory` concrete classes (stubs). |
| `src/miki/rhi/MainPipelineFactory.cpp` | internal | L | Tier1 factory — only `CreateGeometryPass()` implemented in Phase 1a |
| `src/miki/rhi/CompatPipelineFactory.cpp` | internal | L | Tier2/3/4 factory — all methods return `NotImplemented` in Phase 1a |
| `tests/unit/test_pipeline_factory.cpp` | internal | L | Factory dispatch, tier selection tests |

- **Error model**: `Result<PipelineHandle>`. Unimplemented methods return `ErrorCode::NotImplemented`.
- **Thread safety**: Factory is single-owner, created once at init.
- **GPU constraints**: N/A (factory pattern)
- **Invariants**: `MainPipelineFactory` selected when `CapabilityTier == Tier1_Full`. `CompatPipelineFactory` for all other tiers. No `if (compat)` branches in rendering code — factory dispatch handles it.

### Downstream Consumers

- `IPipelineFactory.h` (**public**, heat **H**):
  - T1a.5.2 (same Phase): Vulkan backend uses factory to create geometry pass pipeline
  - T1a.6.2 (same Phase): D3D12 backend uses factory to create geometry pass pipeline
  - T1a.12.1 (same Phase): Triangle demo calls `CreateGeometryPass()`
  - Phase 1b: `CompatPipelineFactory` gets real implementations for GL/WebGPU
  - Phase 2: Forward pass uses `CreateGeometryPass()`
  - Phase 3a: Render graph passes use factory for all pass types
  - Phase 6a: GPU-driven core uses `MainPipelineFactory` for mesh shader pipeline

### Upstream Contracts

- T1a.3.2: `IDevice` (virtual interface), `PipelineHandle`, `Result<T>`, `GraphicsPipelineDesc`

### Technical Direction

- **Dual factory**: `MainPipelineFactory` (Task/Mesh shader, Tier1+D3D12) and `CompatPipelineFactory` (Vertex+MDI, Tier2/3/4). Selected at device creation based on `CapabilityTier`.
- **No `if (compat)` branches**: Rendering code calls `IPipelineFactory::CreateXxxPass()`. The factory returns the appropriate pipeline for the tier. Independent render paths, not degradation.
- **Incremental implementation**: Only `CreateGeometryPass()` implemented in Phase 1a. All other methods (`CreateShadowPass`, `CreateOITPass`, etc.) return `NotImplemented` until their phases.
- **Factory per device**: `IPipelineFactory` created via `IPipelineFactory::Create(IDevice&)` — returns Main or Compat based on device capabilities.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rhi/IPipelineFactory.h` | **public** | **H** | Abstract factory — 6+ phase consumers |
| Create | `src/miki/rhi/MainPipelineFactory.cpp` | internal | L | Tier1 implementation |
| Create | `src/miki/rhi/CompatPipelineFactory.cpp` | internal | L | Tier2/3/4 stub |
| Create | `tests/unit/test_pipeline_factory.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define `IPipelineFactory` interface + concrete classes
      **Files**: `IPipelineFactory.h` (**public** H), `MainPipelineFactory.cpp` (internal L), `CompatPipelineFactory.cpp` (internal L)

      **Signatures** (`IPipelineFactory.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `IPipelineFactory::Create` | `static (IDevice&) -> unique_ptr<IPipelineFactory>` | `[[nodiscard]]` — dispatches Main vs Compat |
      | `IPipelineFactory::CreateGeometryPass` | `(GeometryPassDesc const&) -> Result<PipelineHandle>` | `[[nodiscard]]` virtual pure |
      | `IPipelineFactory::CreateShadowPass` | `(ShadowPassDesc const&) -> Result<PipelineHandle>` | `[[nodiscard]]` virtual pure |
      | `IPipelineFactory::CreateOITPass` | `(OITPassDesc const&) -> Result<PipelineHandle>` | `[[nodiscard]]` virtual pure |
      | `IPipelineFactory::CreateAOPass` | `(AOPassDesc const&) -> Result<PipelineHandle>` | `[[nodiscard]]` virtual pure |
      | `IPipelineFactory::CreateAAPass` | `(AAPassDesc const&) -> Result<PipelineHandle>` | `[[nodiscard]]` virtual pure |
      | `IPipelineFactory::CreatePickPass` | `(PickPassDesc const&) -> Result<PipelineHandle>` | `[[nodiscard]]` virtual pure |
      | `IPipelineFactory::CreateHLRPass` | `(HLRPassDesc const&) -> Result<PipelineHandle>` | `[[nodiscard]]` virtual pure |
      | `IPipelineFactory::GetTier` | `() const noexcept -> CapabilityTier` | `[[nodiscard]]` virtual pure |
      | `~IPipelineFactory` | | virtual default |
      | `GeometryPassDesc` | `{ vertexShader:ShaderHandle, fragmentShader:ShaderHandle, vertexLayout:VertexLayout, depthTest:bool, depthWrite:bool, cullMode:CullMode, polygonMode:PolygonMode, colorFormats:span<Format>, depthFormat:Format }` | — |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | Namespace | `miki::rhi` |
      | Main tier | `CapabilityTier::Tier1_Full` |
      | Compat tier | `Tier2_Compat`, `Tier3_WebGPU`, `Tier4_OpenGL` |

      **Acceptance**: compiles; `IPipelineFactory::Create` returns correct factory type based on tier
      `[verify: compile]`

- [x] **Step 2**: Unit tests
      **Files**: `tests/unit/test_pipeline_factory.cpp` (internal L)
      Cover: factory dispatch (Tier1 -> Main, Tier2 -> Compat), `CreateGeometryPass` succeeds on Main, all other methods return `NotImplemented`, `GetTier` correctness.
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(PipelineFactory, Tier1ReturnsMain)` | Unit | Step 1 — dispatch | 1 |
| `TEST(PipelineFactory, Tier2ReturnsCompat)` | Unit | Step 1 — dispatch | 1 |
| `TEST(PipelineFactory, CreateGeometryPassMain)` | Unit | Step 1 — Tier1 geometry pass | 1 |
| `TEST(PipelineFactory, ShadowPassNotImplemented)` | Unit | Step 1 — stub returns error | 1 |

## Design Decisions

- **Supporting types added**: `CullMode`, `PolygonMode`, `VertexAttributeFormat`, `VertexAttribute`, `VertexInputRate`, `VertexBinding`, `VertexLayout` defined in `IPipelineFactory.h` since they are needed by `GeometryPassDesc` and don't belong in lower-level headers.
- **Pass descriptors co-located**: All pass descriptors (`GeometryPassDesc`, `ShadowPassDesc`, etc.) in `IPipelineFactory.h` rather than separate files — they are only consumed by the factory.
- **MainPipelineFactory::CreateGeometryPass returns dummy handle**: Returns `PipelineHandle::Create(1, 0, 0)` as a valid placeholder. Real implementation will call `device_.CreateGraphicsPipeline()` once shader compilation is available.
- **CompatPipelineFactory::GetTier returns device tier**: Not hardcoded to a single tier — returns the actual tier from the device profile (could be Tier2, Tier3, or Tier4).
- **Factory static Create uses SupportsTier**: `iDevice.GetCapabilities().SupportsTier(Tier1_Full)` cleanly dispatches Main vs Compat.
- **Compat factory function in separate TU**: `CreateCompatPipelineFactory()` is a free function in `CompatPipelineFactory.cpp`, forward-declared in `MainPipelineFactory.cpp` where `IPipelineFactory::Create()` lives. Avoids exposing concrete class in header.
- **TestDevice inline in test file**: Minimal `IDevice` implementation for tests, since `MockDevice` (T1a.8.1) is not yet available.

## Implementation Notes

- Contract check: PASS (14/14 items)
- Build: 0 errors, 0 warnings
- Tests: 66/66 pass (12 PipelineFactory + 7 Capabilities + 11 IDevice + 16 RhiTypes + 17 Foundation + 3 Toolchain)
- No TODO/STUB/FIXME in task files
