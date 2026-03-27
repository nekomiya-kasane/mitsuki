# T3b.16.1 — IPipelineFactory::CreateShadowPass / CreateAOPass / CreateAAPass Implementations

**Phase**: 05-3b
**Component**: 16 — 5-Backend Tier Sync + Demo
**Roadmap Ref**: `roadmap.md` Phase 3b — 5-Backend Sync
**Status**: Complete
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.5.2 | VSM Shadows | Not Started | `VsmShadows` (Tier1 shadow) |
| T3b.8.1 | GTAO | Not Started | `Gtao` (Tier1/2 AO) |
| T3b.9.1 | SSAO | Not Started | `Ssao` (Tier3/4 AO) |
| T3b.11.2 | TAA | Not Started | `Taa` (Tier1/2 AA) |
| T3b.12.1 | FXAA | Not Started | `Fxaa` (Tier3/4 AA) |

## Context Anchor

### This Task's Contract

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `MainPipelineFactory.cpp` (modified) | internal | L | Tier1 implementations of CreateShadowPass/CreateAOPass/CreateAAPass |
| `CompatPipelineFactory.cpp` (modified) | internal | L | Tier2/3/4 implementations |
| `IPipelineFactory.h` (potentially modified) | **public** | **H** | Pass descriptors may gain new fields |

- **CreateShadowPass**: Tier1 returns VSM shadow pipeline; Tier2/3/4 returns CSM shadow pipeline.
- **CreateAOPass**: Tier1/2 returns GTAO compute pipeline; Tier3/4 returns SSAO fragment pipeline.
- **CreateAAPass**: Tier1/2 returns TAA compute pipeline; Tier3/4 returns FXAA fragment pipeline.
- Currently all three return `NotImplemented`. This task provides real implementations.
- Pipeline descriptors (`ShadowPassDesc`, `AOPassDesc`, `AAPassDesc`) may need field additions for the new pass types.

### Downstream Consumers

- T3b.16.2: demo uses factory-created passes
- Phase 6a+: all rendering uses factory-created passes

### Technical Direction

- **Shadow**: `ShadowPassDesc` extended with `ShadowMode { VSM, CSM }` and tier-specific config (VSM page count, CSM cascade count).
- **AO**: `AOPassDesc` extended with `AOMode { GTAO, SSAO }` and tier-specific config (GTAO half-res, SSAO sample count).
- **AA**: `AAPassDesc` extended with `AAMode { TAA, FXAA, None }` and quality params.
- **Factory dispatch**: `MainPipelineFactory` creates Tier1 variants; `CompatPipelineFactory` creates Tier2/3/4 variants. No `if (compat)` in rendering code.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Modify | `include/miki/rhi/IPipelineFactory.h` | **public** | **H** | Extend pass descriptors if needed |
| Modify | `src/miki/rhi/MainPipelineFactory.cpp` | internal | L | Tier1 implementations |
| Modify | `src/miki/rhi/CompatPipelineFactory.cpp` | internal | L | Tier2/3/4 implementations |
| Create | `tests/unit/test_pipeline_factory_passes.cpp` | internal | L | Tests |

## Steps

- [x] **Step 1**: Extend pass descriptors (ShadowPassDesc, AOPassDesc, AAPassDesc) if needed
- [x] **Step 2**: Implement CreateShadowPass (MainPipelineFactory: VSM, CompatPipelineFactory: CSM)
- [x] **Step 3**: Implement CreateAOPass (Main: GTAO, Compat: SSAO)
- [x] **Step 4**: Implement CreateAAPass (Main: TAA, Compat: FXAA)
- [x] **Step 5**: Tests for all tier combinations

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(MainPipelineFactory, CreateShadowPass_ReturnsValid)` | Positive | Tier1 shadow pipeline |
| `TEST(MainPipelineFactory, CreateAOPass_ReturnsValid)` | Positive | Tier1 AO pipeline |
| `TEST(MainPipelineFactory, CreateAAPass_ReturnsValid)` | Positive | Tier1 AA pipeline |
| `TEST(CompatPipelineFactory, CreateShadowPass_ReturnsValid)` | Positive | Tier2/3/4 shadow |
| `TEST(CompatPipelineFactory, CreateAOPass_ReturnsValid)` | Positive | Tier2/3/4 AO |
| `TEST(CompatPipelineFactory, CreateAAPass_ReturnsValid)` | Positive | Tier2/3/4 AA |
| `TEST(PipelineFactory, TierDispatch_Correct)` | Positive | Main for Tier1, Compat for others |
| `TEST(PipelineFactory, EndToEnd_AllPassesCreated)` | Integration | factory creates all passes |
| `TEST(PipelineFactory, InvalidDesc_Error)` | Error | invalid pass descriptor returns error |
| `TEST(PipelineFactory, MockBackend_ReturnsNotImpl)` | Boundary | mock backend returns NotImplemented for GPU passes |
| `TEST(PipelineFactory, MoveSemantics)` | State | factory is non-copyable, move transfers ownership |
