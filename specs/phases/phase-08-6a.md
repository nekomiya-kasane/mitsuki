# Phase 08: 6a — GPU-Driven Rendering Core

**Sequence**: 08 / 27
**Status**: Not Started
**Started**: —
**Completed**: —

## Goal

Task/mesh amplification, visibility buffer, GPU scene submission, GPU compute primitives. Zero CPU draw calls from the start. This phase replaces the existing forward/deferred draw-call-based rendering with a fully GPU-driven pipeline where the CPU only uploads dirty data and records 3 indirect draw commands per frame.

## Roadmap Digest

### Key Components (from roadmap table, expanded for volume)

| # | Component | Core deliverable | Test target |
|---|-----------|-----------------|-------------|
| 0 | RHI Extensions | `DrawMeshTasks`, `DrawMeshTasksIndirect`, `DrawMeshTasksIndirectCount`, `DispatchIndirect`, mesh shader pipeline desc, `BufferUsage::ShaderDeviceAddress` | ~10 |
| 1 | MeshletGenerator | Greedy partitioning (64v/124p), bounding sphere, normal cone, Morton reorder, `MeshletDescriptor` GPU struct | ~12 |
| 2 | GPU Compute Primitives | `GpuRadixSort` (Onesweep), `GpuPrefixSum` (Blelloch), `GpuCompact`, `GpuHistogram` | ~12 |
| 3 | HiZ Pyramid | Depth mip chain compute, single-pass or multi-pass downsample | ~6 |
| 4 | SceneBuffer + GPU Scene Submission | `GpuInstance` (128B), CPU mirror, dirty-flag compute update, 3-bucket macro-binning | ~12 |
| 5 | Task Shader + Mesh Shader | Slang task/mesh shaders, instance-level cull, meshlet-level cull, normal cone backface cull, BDA vertex fetch | ~10 |
| 6 | Visibility Buffer + Material Resolve | 64-bit atomic VisBuffer (full 32-bit instanceId + primitiveId), tile-based material resolve compute (FAST/MEDIUM/SLOW), `MaterialParameterBlock[]` bindless | ~12 |
| 7 | Software Rasterizer *(optional)* | Compute fine rasterizer for <4px triangles, uint64 SSBO atomicMax | ~6 |
| 8 | Demo + Integration | `gpu_driven_basic` — 10M tri, zero CPU draws, cull stats overlay | ~8 |

### Critical Technical Decisions

- **Mesh shader pipeline**: Slang task+mesh shader stages. `ICommandBuffer` gains `DrawMeshTasks()`, `DrawMeshTasksIndirect()`, `DrawMeshTasksIndirectCount()`. `IDevice` gains mesh shader `GraphicsPipelineDesc` variant.
- **Single-PSO geometry**: VisBuffer decouples geometry from material. Geometry pass = 1 PSO, 1 `DrawMeshTasksIndirectCount`. No material sorting in geometry stage.
- **3-bucket macro-binning**: GPU cull classifies instances into Opaque/Transparent/Wireframe buckets via atomic append. CPU records 3 `BindPipeline` + 3 `IndirectCount` per frame.
- **State-to-Data**: selection, section plane, color override — all in `GpuInstance` fields, NOT separate PSOs.
- **BDA vertex fetch**: mesh shader reads vertices via `BDAPointer`, no vertex input assembly. Tangent reconstructed in VisBuffer resolve.
- **Material resolve**: tile-based binning (16×16 tiles, FAST/MEDIUM/SLOW paths per `rendering-pipeline-architecture.md` §5.4). NOT full-screen radix sort. Evaluates `StandardPBR` from `MaterialParameterBlock[]` (192B, DSPBR) bindless array. Zero PSO switches. `GpuRadixSort` remains available for Phase 6b/7a-2 but is not used by material resolve.
- **HiZ from previous frame**: two-phase occlusion — HiZ pyramid from last frame's depth, re-test after current frame depth pre-pass (optional).
- **SW Rasterizer optional**: can defer to Phase 6b. Mesh shader handles all triangles correctly without it.
- **`DispatchIndirect`**: required for variable-count cull output → task shader dispatch.

### Performance Targets

| Metric | Target |
|--------|--------|
| 10M triangles | >= 60fps (RTX 4070) |
| CPU draw calls per frame | 3 (fixed, deterministic) |
| GPU culling (100K instances) | < 0.5ms |
| HiZ pyramid build | < 0.3ms |
| Material resolve (4K) | < 2ms |
| GpuRadixSort (16M keys) | < 2ms |
| GpuPrefixSum (16M) | < 0.5ms |

### Downstream Phase Expectations

| Phase | What it expects from this phase |
|-------|-------------------------------|
| Phase 6b (Streaming & LOD) | `MeshletGenerator` partitioning, `MeshletDescriptor` GPU format, task/mesh shader pipeline, `SceneBuffer` instance management, `GpuRadixSort` for LOD selection sort |
| Phase 7a-1 (HLR) | Mesh shader pipeline infrastructure, `SceneBuffer` for edge classification, `GPU Culling (HiZ)` for edge visibility |
| Phase 7a-2 (Picking) | `VisBuffer` pixel readback for pick-at-pixel, `SceneBuffer.GpuInstance.entityId` for VisBuffer→Entity resolution, `GpuInstance.selectionMask/flags` for selectability enforcement |
| Phase 8 (CadScene) | `SceneBuffer` as the bridge between CadScene CPU assembly tree and GPU rendering. `GpuInstance.flags` for layer/style bits |
| Cooldown #1 | API freeze for `IDevice`, `ICommandBuffer`, `RenderGraphBuilder` |

## Phase Dependencies

| Dependency Phase | What it provides |
|-----------------|-----------------|
| Phase 01-1a | `IDevice`, `ICommandBuffer`, `Handle<Tag>`, `Format`, `BackendType`, `DeviceFeature`, `DeviceFeatureSet` |
| Phase 02-1b | Compat/GL/WebGPU backends, `SlangCompiler` quad-target |
| Phase 03-2 | `DescriptorSetLayout/PipelineLayout/DescriptorSet`, `MeshData`, `IPipelineFactory`, `StagingUploader`, `MaterialRegistry`, `StandardPBR` |
| Phase 04-3a | `RenderGraphBuilder/Compiler/Executor/Cache`, `GBuffer`, `DeferredResolve`, `ToneMapping`, `CameraUBO`, `DrawListBuilder` |
| Phase 05-3b | `PipelineCache`, VSM/CSM shadows, TAA, GTAO/SSAO, Bloom, `IPipelineFactory::CreateShadowPass/AOPass/AAPass` |
| Phase 06-4 | `ResourceHandle`, `SlotMap`, `BindlessTable`, `BDAManager`, `StagingRing`, `ResourceManager`, `MemoryBudget`, `ResidencyFeedback` |
| Phase 07-5 | `Entity`, `EntityManager`, `ComponentPool<T>`, `ComponentRegistry`, `QueryEngine`, `BVH`, `Octree` |

---

## Components & Tasks

### Component -1: Pre-Phase Gate Fix (mandatory before any Phase 6a work)

> Fix all outstanding test failures, stale test expectations, demo acceptance verification gaps, and backend stubs discovered in the Phase 1a–5 completeness audit (2026-03-20). **No Phase 6a task may start until T6a.0.0 is Complete.**

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T6a.0.0 | Pre-Phase Gate Fix — all prior-phase defects | — | H |

### Component 0: RHI Extensions for GPU-Driven

> Extend IDevice and ICommandBuffer with mesh shader dispatch, indirect dispatch, and buffer device address usage flags.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T6a.0.1 | RHI mesh shader + indirect dispatch — DrawMeshTasks, DrawMeshTasksIndirect, DrawMeshTasksIndirectCount, DispatchIndirect, mesh shader GraphicsPipelineDesc, BufferUsage::ShaderDeviceAddress | — | L |

### Component 1: MeshletGenerator

> Multi-strategy meshlet partitioning (Scan/Greedy/Spatial) with GPU-compatible 64B descriptor, snorm8-packed normal cone + apex, intra-meshlet triangle locality optimization (meshopt_optimizeMeshlet equivalent), bounding sphere, Morton reorder. Architecture derived from meshoptimizer v1.0, Jensen et al. JCGT 2023, Zeux 2024 triangle locality research.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T6a.1.1 | MeshletGenerator — 3-strategy partition + MeshletDescriptor 64B + bounds (sphere+cone+apex) + intra-meshlet locality optimizer + Morton reorder | — | M |

### Component 2: GPU Compute Primitives

> Reusable GPU sort/scan/compact/histogram used by culling, VisBuffer resolve, and downstream phases.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T6a.2.1 | GpuPrefixSum (Blelloch) + GpuCompact (stream compaction) + GpuHistogram | T6a.0.1 | L |
| [x] | T6a.2.2 | GpuRadixSort (DeviceRadixSort, 16M keys) — 4-kernel, wave multisplit, passHist scan, descending sort | T6a.2.1 | L |

### Component 3: HiZ Pyramid

> Hierarchical-Z depth pyramid for GPU occlusion culling.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T6a.3.1 | HiZ pyramid compute — multi-pass max-depth downsample, mip chain, memory barriers | T6a.0.1 | M |

### Component 4: SceneBuffer + GPU Scene Submission

> GPU-resident instance buffer with CPU mirror, dirty-flag update, 3-bucket macro-binning.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T6a.4.1 | GpuInstance struct + SceneBuffer CPU mirror + dirty-flag compute upload | T6a.0.1 | L |
| [x] | T6a.4.2 | Macro-binning compute — 3-bucket classify (Opaque/Transparent/Wireframe) + indirect args | T6a.4.1, T6a.2.1 | L |

### Component 5: Task Shader + Mesh Shader

> Slang task/mesh shader pair: instance-level and meshlet-level culling, BDA vertex fetch.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T6a.5.1 | Task shader — instance frustum+occlusion cull, emit mesh workgroups | T6a.1.1, T6a.3.1, T6a.4.1 | L |
| [x] | T6a.5.2 | Mesh shader — meshlet BDA vertex unpack, normal cone backface cull, output primitives | T6a.5.1 | L |

### Component 6: Visibility Buffer + Material Resolve

> 64-bit atomic visibility buffer write, material resolve compute dispatch.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T6a.6.1 | Visibility Buffer — R32G32_UINT atomic write from mesh shader, clear pass | T6a.5.2 | L |
| [x] | T6a.6.2 | Material Resolve compute — tile-based binning (FAST/MEDIUM/SLOW), BDA attribute fetch, StandardPBR eval, deferred target write | T6a.6.1 | L |

### Component 7: Software Rasterizer *(optional)*

> Compute fine rasterizer for sub-4px triangles. Can defer to Phase 6b.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T6a.7.1 | SW Rasterizer — compute shader fine raster, uint64 SSBO atomicMax, resolve to VisBuffer | T6a.6.1, T6a.5.2 | L |

### Component 8: Demo + Integration

> Full GPU-driven demo integrating all components.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T6a.8.1 | gpu_driven_basic demo — 10M tri procedural scene, full pipeline, cull stats overlay, zero CPU draws | T6a.6.2, T6a.4.2 | L |

---

## Demo Plan

### gpu_driven_basic

- **Name**: `demos/gpu_driven_basic/` *(Note: roadmap L257 uses `gpu_cull/` — `gpu_driven_basic` is more descriptive. Roadmap will be synced.)*
- **Shows**: 10M triangle procedural scene (1000 instances × 10K tri meshlets), full GPU-driven pipeline (task→mesh→VisBuffer→resolve), SceneBuffer stats, culling stats (visible instances/meshlets/triangles), HiZ pyramid visualization, zero CPU draw calls
- **Requires Tasks**: T6a.0.1 through T6a.8.1 (T6a.7.1 optional)
- **CLI**: `--backend vulkan` (Tier1 only for mesh shader)
- **Acceptance**:
  - [ ] Zero CPU draw calls in steady state (3 IndirectCount dispatches)
  - [ ] Correct rendering (visual match vs forward reference)
  - [ ] ImGui overlay: instance/meshlet/triangle cull ratios
  - [ ] >= 60fps on RTX 4070 equivalent with 10M triangles
  - [ ] HiZ pyramid correctly generated and visualizable

## Test Summary

| Category    | Target | Actual | Notes |
|-------------|--------|--------|-------|
| Unit        | ~50    |        |       |
| Integration | ~15    |        |       |
| Benchmark   | ~3     |        |       |
| **Total**   | ~68    |        | Roadmap target: ~60 |

## Implementation Order (Layers)

| Layer | Tasks (parallel within layer) | Depends on Layer |
|-------|-------------------------------|------------------|
| L0 | T6a.0.1, T6a.1.1 | — |
| L1 | T6a.2.1, T6a.3.1, T6a.4.1 | L0 |
| L2 | T6a.2.2, T6a.4.2, T6a.5.1 | L1 |
| L3 | T6a.5.2, T6a.6.1 | L2 |
| L4 | T6a.6.2, T6a.7.1 (optional) | L3 |
| L5 | T6a.8.1 | L4 |

**Critical path**: L0 (RHI extensions ∥ MeshletGenerator) → L1 (Compute + HiZ + SceneBuffer) → L2 (RadixSort + MacroBin + TaskShader) → L3 (MeshShader + VisBuffer) → L4 (MaterialResolve) → L5 (Demo)

**Optimization vs previous plan**: T6a.1.1 moved to L0 (was L1). MeshletGenerator is CPU-only — no RHI dependency. This enables meshlet data to be ready earlier for T6a.5.1.

---

## Forward Design Notes

| Future Phase | What this phase prepares | Interface / extension point |
|-------------|------------------------|---------------------------|
| Phase 6b | `MeshletGenerator` partitioning format reused by `ClusterDAG`. `MeshletDescriptor` GPU struct = input to compression encoder. `GpuRadixSort` reused by DAG cut optimizer. `SceneBuffer` extended with LOD fields. | `MeshletDescriptor::boundingSphere/normalCone` consumed by LOD error metric. `GpuRadixSort::Sort(span<uint32_t> keys, span<uint32_t> values)` |
| Phase 6b | `PersistentCompute` extends `DispatchIndirect` path. `SceneBuffer` dirty-flag pattern reused for incremental TLAS rebuild. | `SceneBuffer::GetDirtyInstances() -> span<uint32_t>` |
| Phase 7a-1 | HLR edge extraction reads `SceneBuffer` + mesh adjacency. Mesh shader line rendering reuses mesh shader pipeline infrastructure. | `GpuInstance.entityId` for edge→entity lookup |
| Phase 7a-2 | `VisBuffer` pixel readback → `instanceId` → `GpuInstance.entityId` → `Entity` → `TopoGraph::PrimitiveToFace`. `GpuInstance.selectionMask` + `flags` for selectability. `GpuRadixSort` reused by pick dedup. | `SceneBuffer::GetCpuMirror() -> span<GpuInstance const>` for O(1) entity resolution |
| Phase 8 | `GpuInstance.flags` carries `layerBits:16` + `styleBits:16` from CadScene. `SceneBuffer` is the CPU→GPU bridge for CadScene presentation. | `GpuInstance.flags` field layout frozen here |
| Phase 14 | **GpuRadixSort performance upgrade** (current: ~49ms/16M, target: <2ms). Three deferred items: **(1)** Barrier-free wave-local atomics — investigate `subgroupMemoryBarrierShared()` in Slang/SPIR-V to eliminate per-row `GroupMemoryBarrierWithGroupSync()` (root cause of ~25× perf gap vs b0nes164). Attempted barrier-free single-pass in Phase 6a: failed due to Vulkan shared memory atomic visibility. **(2)** OneSweep (decoupled lookback) — combine Upsweep+Downsweep into single kernel, requires `VK_KHR_shader_atomic_int64` or status-flag polling. Keep DeviceRadixSort as fallback for non-Tier1 GPUs. **(3)** Adaptive partition tuning — profile PART_SIZE variants (1792–7680) per GPU arch at `Create()` time. | `GpuRadixSort::Sort/SortKeyValue/SortDescending/SortKeyValueDescending` API frozen. Internal kernel replacement only. See `specs/roadmap.md` Appendix A for full competitive analysis. |

---

## Cross-Component Contracts

| Provider Task | File (Exposure) | Consumer Task | Agreed Signature |
|--------------|-----------------|---------------|------------------|
| T6a.0.1 | `ICommandBuffer.h` (**public**) | T6a.5.1, T6a.5.2, T6a.8.1 | `DrawMeshTasks(groupX, groupY, groupZ)`, `DrawMeshTasksIndirectCount(argBuf, argOffset, countBuf, countOffset, maxDrawCount, stride)` |
| T6a.0.1 | `ICommandBuffer.h` (**public**) | T6a.2.1, T6a.3.1, T6a.4.2 | `DispatchIndirect(argBuffer, argOffset)` |
| T6a.1.1 | `MeshletTypes.h` (**public**) | T6a.5.1, T6a.5.2, T6a.6.2, Phase 6b | `MeshletDescriptor` (64B GPU struct), `MeshletBuildResult` |
| T6a.2.1 | `GpuPrefixSum.h` (**public**) | T6a.2.2, T6a.4.2 | `GpuPrefixSum::Scan(cmdBuf, inputBuf, outputBuf, count)` |
| T6a.2.2 | `GpuRadixSort.h` (**public**) | Phase 6b, Phase 7a-2 | `GpuRadixSort::Sort(cmdBuf, keyBuf, valueBuf, count)` *(NOT used by T6a.6.2 material resolve — tile-based binning instead)* |
| T6a.3.1 | `HiZPyramid.h` (**public**) | T6a.5.1, Phase 6b | `HiZPyramid::Build(cmdBuf, depthTexture) -> TextureHandle`, `HiZPyramid::GetMipCount()` |
| T6a.4.1 | `SceneBuffer.h` (**public**) | T6a.4.2, T6a.5.1, T6a.8.1, Phase 6b, 7a-1, 7a-2, 8 | `SceneBuffer::Upload(cmdBuf)`, `SceneBuffer::GetGpuBuffer()`, `SceneBuffer::GetCpuMirror()` |
| T6a.4.1 | `GpuInstance.h` (**public**) | T6a.5.1, T6a.6.2, Phase 7a-2, 8 | `GpuInstance` (128B, `alignas(16)`, `entityId`, `meshletBaseIndex`, `materialId`, `selectionMask`, `flags`) |
| T6a.6.1 | `VisibilityBuffer.h` (**public**) | T6a.6.2, T6a.7.1, Phase 7a-2 | `VisibilityBuffer::GetTexture()`, `VisibilityBuffer::Clear(cmdBuf)` |
