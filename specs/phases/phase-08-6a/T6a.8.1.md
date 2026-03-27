# T6a.8.1 — gpu_driven_basic Demo — 10M Tri, Full Pipeline, Zero CPU Draws

**Phase**: 08-6a (GPU-Driven Rendering Core)
**Component**: Demo + Integration
**Roadmap Ref**: `roadmap.md` L1747 — gpu_driven_basic demo
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6a.6.2 | Material Resolve | Not Started | `MaterialResolve::Execute()` for deferred target generation |
| T6a.4.2 | Macro-Binning | Not Started | `MacroBinning::Classify()` + `GetBucketArgs()` for 3-bucket dispatch |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `demos/gpu_driven_basic/main.cpp` | internal | L | Full GPU-driven demo: procedural 10M tri scene, task→mesh→VisBuffer→resolve→lighting→present |
| `demos/gpu_driven_basic/CMakeLists.txt` | internal | L | Demo target |
| `shaders/demos/gpu_driven_basic.slang` | internal | L | Any demo-specific shader utilities |
| `tests/integration/test_gpu_driven_basic.cpp` | internal | L | Integration tests |

- **Error model**: demo exits on fatal error, reports via ImGui overlay otherwise
- **Thread safety**: single-threaded render loop
- **GPU constraints**: Tier1 only (mesh shader required). CLI `--backend vulkan`
- **Invariants**: zero `DrawIndexed`/`Draw` calls in steady state. Only `DrawMeshTasksIndirectCount` ×3.

### Downstream Consumers

- No direct code consumers (demo is a leaf). But serves as:
  - **Visual reference** for Phase 6b (virtual_geometry extends this demo)
  - **Performance baseline** for Cooldown #1 profiling
  - **Integration test harness** validating all Phase 6a components together

### Upstream Contracts
- T6a.0.1: `DrawMeshTasksIndirectCount()` on `ICommandBuffer`
- T6a.1.1: `MeshletGenerator::Build()` for procedural mesh partitioning
- T6a.2.1: `GpuPrefixSum`, `GpuCompact` (used by T6a.4.2 internally)
- T6a.2.2: `GpuRadixSort::SortKeyValue()` (used by T6a.6.2 internally)
- T6a.3.1: `HiZPyramid::Build()` for occlusion culling
- T6a.4.1: `SceneBuffer::Add/Upload()`, `GpuInstance` struct
- T6a.4.2: `MacroBinning::Classify()`, `GetBucketArgs()`, `GetBucketCount()`
- T6a.5.1: `GpuCullPipeline::BuildPipeline()`, `CullUniforms`
- T6a.5.2: mesh shader (compiled into pipeline)
- T6a.6.1: `VisibilityBuffer::Create/Clear()`
- T6a.6.2: `MaterialResolve::Execute()`
- T6a.7.1: `SwRasterizer` (optional, feature-detected)
- Phase 3a: `RenderGraphBuilder/Compiler/Executor` for frame organization
- Phase 3a: `DeferredResolve::Execute()` for lighting after material resolve
- Phase 3b: `ToneMapping::Execute()` for final output
- Phase 4: `BindlessTable`, `BDAManager`, `ResourceManager` for resource lifecycle
- Phase 5: `EntityManager`, `ComponentPool<Transform>` for ECS integration

### Technical Direction
- **Procedural scene**: generate 1000 instances of a 10K-triangle sphere/torus mesh (= 10M total triangles). Each instance has a unique transform (grid layout) and random material (from 16 StandardPBR presets). `MeshletGenerator::Build()` partitions each mesh into meshlets at startup.
- **Render loop (per-frame)**:
  1. ECS update: mark dirty instances → `SceneBuffer::Update()` → `SceneBuffer::Upload()`
  2. RenderGraph build:
     a. HiZ pyramid from previous frame depth
     b. VisBuffer clear
     c. MacroBinning classify
     d. Geometry pass: 3× `DrawMeshTasksIndirectCount` (opaque/transparent/wireframe)
     e. Material resolve compute
     f. Deferred lighting (`DeferredResolve`)
     g. Post-process (Bloom, TAA, ToneMapping)
     h. ImGui overlay
     i. Present
  3. RenderGraph compile + execute
- **ImGui overlay panels**:
  - Cull Stats: total instances, visible instances, visible meshlets, visible triangles, cull ratios
  - Scene Stats: total meshlets, total triangles, buffer memory usage
  - HiZ: mip chain visualization toggle
  - Performance: per-pass GPU time (via timestamp queries)
  - SW Raster: enabled/disabled toggle, triangle classification stats (if T6a.7.1 available)
- **DemoControlServer MCP integration**: expose `cull_enabled`, `sw_raster_enabled`, `wireframe_mode` as MCP parameters
- **CLI flags**: `--backend vulkan`, `--instances N`, `--no-cull`, `--no-sw-raster`

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `demos/gpu_driven_basic/main.cpp` | internal | L | Full demo |
| Create | `demos/gpu_driven_basic/CMakeLists.txt` | internal | L | Build target |
| Modify | `demos/CMakeLists.txt` | internal | L | Add subdirectory |
| Create | `tests/integration/test_gpu_driven_basic.cpp` | internal | L | Integration tests |
| Modify | `tests/integration/CMakeLists.txt` | internal | L | Add test target |

## Steps

- [ ] **Step 1**: Create CMakeLists.txt + demo skeleton
      Set up build target, link all Phase 6a libraries + Phase 3a-5 dependencies.
      `[verify: compile]`

- [ ] **Step 2**: Implement procedural scene generation
      Generate 1000 instances, partition meshlets, create SceneBuffer, register BDA/bindless.
      `[verify: compile]`

- [ ] **Step 3**: Implement render graph assembly
      Wire all passes: HiZ → VisBuffer clear → MacroBin → geometry (3× IndirectCount) → material resolve → deferred lighting → post-process → present.
      `[verify: compile]`

- [ ] **Step 4**: Implement ImGui overlay + DemoControlServer
      Cull stats, scene stats, HiZ viz, per-pass GPU timing.
      `[verify: compile]`

- [ ] **Step 5**: Integration tests
      `[verify: test]`

- [ ] **Step 6**: Visual verification + performance validation
      `[verify: visual]` `[verify: manual]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(GpuDrivenBasic, HeadlessRender)` | Positive | Full pipeline executes without crash, produces non-black output | 3,5 |
| `TEST(GpuDrivenBasic, ZeroCpuDrawCalls)` | Positive | Only DrawMeshTasksIndirectCount recorded (no Draw/DrawIndexed) | 3,5 |
| `TEST(GpuDrivenBasic, CullReducesDrawCount)` | Positive | With partial frustum, visible count < total count | 3,5 |
| `TEST(GpuDrivenBasic, VisBufferNonEmpty)` | Positive | After geometry pass, VisBuffer has non-sentinel pixels | 3,5 |
| `TEST(GpuDrivenBasic, MaterialResolveOutput)` | Positive | Deferred targets have correct albedo for known material | 3,5 |
| `TEST(GpuDrivenBasic, SceneBufferEntityRoundTrip)` | Positive | ECS entity → GpuInstance.entityId → readback matches | 2,5 |
| `TEST(GpuDrivenBasic, HiZPyramidGenerated)` | Positive | HiZ texture has valid depth values after build | 3,5 |
| `TEST(GpuDrivenBasic, Perf_10M_Above60fps)` | Benchmark | 10M triangles >= 60fps on RTX 4070 (GTEST_SKIP) | 3,5 |

## Design Decisions

*(Fill during implementation.)*

## Implementation Notes

*(Fill during implementation.)*
