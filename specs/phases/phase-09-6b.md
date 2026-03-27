# Phase 09: 6b — ClusterDAG, Streaming, LOD & GPU Mesh Simplification

**Sequence**: 09 / 27
**Status**: Not Started
**Started**: —
**Completed**: —

## Goal

Nanite-grade hierarchical LOD, cluster streaming, persistent compute, meshlet compression. 100M+ tri at 60fps. Two-phase occlusion culling. Perceptual LOD selection. Dithered LOD transitions. GPU mesh simplification for LOD generation and export.

## Roadmap Digest

### Key Components (expanded from roadmap, split per research)

| # | Component | Core deliverable | Test target |
|---|-----------|-----------------|-------------|
| 0 | Pre-Phase Gate Fix | Fix HiZ EndToEnd test (Phase 6a residual) | ~1 |
| 1 | ClusterDAG Builder | Group-of-4 partition → border-locked QEM simplify → recursive DAG construction. SAH-optimal grouping. Error metric monotonicity. | ~15 |
| 2 | LOD Selector + DAG Cut | CPU LOD selector (projected sphere error). GPU DAG cut optimizer compute (budget-constrained). `isCorrectLOD(parentError, childError, threshold)`. | ~10 |
| 3 | Perceptual LOD | Silhouette/curvature/selection weighting. Per-meshlet curvature bound in MeshletDescriptor. ~25% meshlet reduction for CAD. | ~5 |
| 4 | Meshlet Compression | Encoder (CPU): 16-bit quantized pos, octahedral normal 2×8-bit, 8-bit local indices, delta encoding. Decoder (mesh shader): zero-CPU decompression. ~50% size reduction. | ~10 |
| 5 | Persistent Compute | `PersistentDispatch` — N workgroups loop on work queue. BVH refit (<0.1ms/1K dirty). Stream decompression (≥2GB/s). | ~8 |
| 6 | Cluster Streaming | `.miki` archive format (LZ4 per-cluster, octree page table). `ChunkLoader` (Coca async IO). `OctreeResidency` + LRU eviction. Progressive rendering (coarse LOD <100ms). | ~12 |
| 7 | Zero-Stall Async Streaming | 3-queue architecture (transfer + graphics + prefetch). Camera-predictive prefetch (>95% cache hit). Timeline semaphore sync. | ~5 |
| 8 | GPU Mesh Simplification (QEM) | CPU QEM core (meshoptimizer-compatible, border-locked, attribute-aware). GPU QEM optional accelerator (parallel edge collapse, independent set). 1M→100K in <50ms. | ~10 |
| 9 | Two-Phase Occlusion Culling | Early pass (prev-frame HiZ) → depth build → Late pass (current-frame HiZ). Visibility persistence buffer. Eliminates 1-frame temporal lag. | ~8 |
| 10 | LOD Transition Smoothing | Dithered fade (8-frame Bayer pattern, zero overdraw cost). Optional vertex geomorphing (200ms lerp). | ~3 |
| 11 | Demo + Integration | `virtual_geometry` — 100M tri (Dragon×1000), seamless LOD, streaming stats. `mesh_simplify_demo` — interactive QEM with quality slider. | ~8 |

### Critical Technical Decisions

- **ClusterDAG construction**: meshoptimizer `simplifyClusters` pattern — group-of-4 meshlets by adjacency → merge triangles → border-locked QEM simplify to 50% → split into new meshlets → recurse. This is the same algorithm used by Nanite and validated by meshoptimizer.
- **Error metric**: projected sphere error = `meshletError * screenHeight / (2 * tan(fov/2) * distance)`. Parent error > child error (monotonicity invariant). Perceptual weighting multiplies base error.
- **DAG cut**: GPU compute per-meshlet: `render if parentProjectedError > threshold AND myProjectedError <= threshold`. Single compute dispatch, O(totalMeshlets) threads.
- **CPU QEM first, GPU QEM optional**: CPU QEM runs at import-time (3M tri/s via meshoptimizer-compatible algorithm). GPU QEM is a Phase 6b stretch goal for interactive simplification.
- **Compression in mesh shader**: 16-bit quantized positions decoded per-vertex in mesh shader (dequant = `pos * scale + offset`, 2 FMA). No async compute pre-decode for standard compression (Phase 14 wavelet needs it).
- **Two-phase occlusion**: matches niagara's early+late pattern. Early pass renders prev-frame-visible instances → builds current HiZ → late pass culls remaining instances against current HiZ. Visibility persistence buffer tracks per-meshlet visibility across frames.
- **Streaming**: Vulkan 1.4 dedicated transfer queue + timeline semaphore. CPU prefetch thread predicts LOD 2-5 frames ahead. `.miki` archive = chunked LZ4 + octree page table + random-access seek.

### Performance Targets

| Metric | Target |
|--------|--------|
| 100M triangles | >= 60fps (RTX 4070) |
| ClusterDAG build (10M tri mesh) | < 5s (import-time) |
| GPU DAG cut optimizer (100K meshlets) | < 0.3ms |
| Meshlet compression ratio | >= 50% size reduction |
| Streaming throughput | >= 2GB/s (LZ4 decompress) |
| Progressive first-frame latency | < 100ms (coarse LOD visible) |
| Two-phase occlusion (100K instances) | < 0.5ms total (early + late) |
| GPU QEM (1M → 100K tri) | < 50ms |
| LOD transition | Zero visual popping (dithered fade) |

### Downstream Phase Expectations

| Phase | What it expects from this phase |
|-------|-------------------------------|
| Cooldown #1 | API freeze for `ClusterDAG`, `ChunkLoader`, `MeshletCompressor`. Performance baseline. |
| Phase 7a-1 (HLR) | Mesh shader pipeline with compressed meshlets. `ClusterDAG` for edge LOD. |
| Phase 7a-2 (Picking) | `ClusterDAG` for LOD-aware picking. Streaming-resident BLAS. |
| Phase 8 (CadScene) | `ChunkLoader` for scene streaming. `ClusterDAG` per-body. |
| Phase 10 (CAE) | Octree streaming infrastructure reused for point cloud. |
| Phase 14 (Scale) | `ClusterDAG` + streaming for 2B+ tri validation. OneSweep radix sort. Wavelet compression. Shader PGO. |

## Phase Dependencies

| Dependency Phase | What it provides |
|-----------------|-----------------|
| Phase 6a | `MeshletGenerator`, `MeshletDescriptor` (64B), `GpuCullPipeline`, `Task/Mesh shaders`, `VisibilityBuffer`, `SceneBuffer`, `GpuRadixSort`, `HiZPyramid`, `MacroBinning`, `SwRasterizer` |
| Phase 4 | `ResourceManager`, `MemoryBudget`, `ResidencyFeedback`, `StagingRing`, `BDAManager` |
| Phase 5 | `Octree`, `BVH`, `EntityManager` |
| Phase 3a | `RenderGraphBuilder/Compiler/Executor` |

---

## Components & Tasks

### Component 0: Pre-Phase Gate Fix

> Fix Phase 6a residual: HiZ EndToEnd_BuildFullPyramid test.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T6b.0.1 | Fix HiZ EndToEnd test — mip dimension tracking in readback | — | S |

### Component 1: ClusterDAG Builder

> Recursive hierarchical LOD tree construction from meshlets. Group-of-4 adjacency partition → merge → border-locked QEM simplify → split → recurse. SAH-optimal grouping. Error metric with parent/child monotonicity.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T6b.1.1 | ClusterDAG types — `ClusterNode`, `ClusterDAG`, `ClusterBuildConfig`, `ClusterBuildResult` | — | M |
| [ ] | T6b.1.2 | Meshlet adjacency graph — boundary edge analysis, shared-edge connectivity | T6b.1.1 | M |
| [ ] | T6b.1.3 | Group partition — group-of-4 meshlets by adjacency (graph partitioning) | T6b.1.2 | M |
| [ ] | T6b.1.4 | Border-locked QEM simplify — merge group → simplify to 50% → split into new meshlets | T6b.1.3 | H |
| [ ] | T6b.1.5 | Recursive DAG construction — iterate levels, compute error metrics, build tree | T6b.1.4 | M |
| [ ] | T6b.1.6 | Error metric monotonicity validation + ClusterDAG tests | T6b.1.5 | M |

### Component 2: LOD Selector + DAG Cut Optimizer

> CPU LOD selector for offline/debug. GPU DAG cut optimizer compute for per-frame LOD selection.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T6b.2.1 | LOD selector types — `LODSelection`, projected sphere error metric | T6b.1.1 | L |
| [ ] | T6b.2.2 | GPU DAG cut optimizer compute — per-meshlet isCorrectLOD, budget-constrained | T6b.2.1, T6b.1.5 | M |

### Component 3: Perceptual LOD

> Silhouette/curvature/selection weighting for CAD-optimized LOD selection.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T6b.3.1 | Perceptual LOD weights — curvature bound in MeshletDescriptor, silhouette factor, selection boost | T6b.2.1 | M |

### Component 4: Meshlet Compression

> CPU encoder + GPU decoder (mesh shader). 16-bit quantized positions, octahedral normals, 8-bit indices, delta encoding.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T6b.4.1 | Compression types — `CompressedMeshlet`, `MeshletCompressor::Encode()` | — | M |
| [ ] | T6b.4.2 | Mesh shader decoder — dequantize positions, decode oct normals in mesh_geo.slang | T6b.4.1 | M |
| [ ] | T6b.4.3 | Compression ratio + decode correctness tests | T6b.4.2 | L |

### Component 5: Persistent Compute

> Workgroup-persistent compute pattern for long-running GPU tasks.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T6b.5.1 | PersistentDispatch — N workgroups loop on atomic work queue | — | M |
| [ ] | T6b.5.2 | Incremental BVH refit via PersistentDispatch | T6b.5.1 | M |

### Component 6: Cluster Streaming

> Out-of-core rendering for 10B+ tri models.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T6b.6.1 | .miki archive format — chunked LZ4, octree page table, random-access seek | — | M |
| [ ] | T6b.6.2 | ChunkLoader — Coca async IO, LZ4 decompress, staging upload | T6b.6.1 | H |
| [ ] | T6b.6.3 | OctreeResidency + LRU eviction — streaming-aware page management | T6b.6.2 | M |
| [ ] | T6b.6.4 | Progressive rendering — coarse LOD <100ms first frame | T6b.6.3, T6b.2.2 | M |

### Component 7: Zero-Stall Async Streaming

> 3-queue architecture with predictive prefetch.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T6b.7.1 | Transfer queue + timeline semaphore — Vulkan 1.4 dedicated transfer | T6b.6.2 | M |
| [ ] | T6b.7.2 | Camera-predictive prefetch — velocity/acceleration/zoom prediction | T6b.7.1 | M |

### Component 8: GPU Mesh Simplification (QEM)

> CPU QEM core + optional GPU accelerator.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T6b.8.1 | CPU QEM simplifier — Garland-Heckbert, border-locked, attribute-aware | — | H |
| [ ] | T6b.8.2 | QEM integration with ClusterDAG — per-group simplification for LOD levels | T6b.8.1, T6b.1.3 | M |
| [ ] | T6b.8.3 | GPU QEM accelerator (optional) — parallel edge collapse, independent set | T6b.8.1 | H |

### Component 9: Two-Phase Occlusion Culling

> Early+late HiZ cull with visibility persistence.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T6b.9.1 | Two-phase cull types — `VisibilityPersistenceBuffer`, early/late dispatch | — | M |
| [ ] | T6b.9.2 | Early+late task shader — split cull into two passes, HiZ rebuild between | T6b.9.1 | M |
| [ ] | T6b.9.3 | Visibility persistence tests — temporal coherence, camera rotation correctness | T6b.9.2 | L |

### Component 10: LOD Transition Smoothing

> Dithered fade + optional geomorphing for seamless LOD switches.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T6b.10.1 | Dithered LOD fade — 8-frame Bayer pattern in mesh shader, alpha test | T6b.2.2 | M |

### Component 11: Demo + Integration

> Full virtual geometry demo.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T6b.11.1 | virtual_geometry demo — 100M tri, seamless LOD, streaming stats, cull overlay | T6b.6.4, T6b.2.2, T6b.4.2, T6b.9.2 | H |
| [ ] | T6b.11.2 | mesh_simplify_demo — interactive QEM with quality slider | T6b.8.1 | M |

---

## Demo Plan

### virtual_geometry

- **Name**: `demos/virtual_geometry/`
- **Shows**: 100M triangle scene (Dragon×1000), seamless LOD transitions, cluster streaming stats, two-phase occlusion stats, compression ratio display, GPU DAG cut optimizer
- **Requires Tasks**: T6b.1.1 through T6b.11.1
- **CLI**: `--backend vulkan --instances 1000 --mesh dragon.obj`
- **Acceptance**:
  - [ ] 100M triangles >= 60fps on RTX 4070
  - [ ] Seamless LOD transitions (no popping)
  - [ ] Progressive rendering: coarse LOD visible < 100ms
  - [ ] Streaming: >2GB/s sustained throughput
  - [ ] Compression ratio >= 50%

### mesh_simplify_demo

- **Name**: `demos/mesh_simplify/`
- **Shows**: Interactive QEM simplification with quality slider, boundary preservation, UV seam visualization
- **Requires Tasks**: T6b.8.1, T6b.11.2

## Test Summary

| Category    | Target | Actual | Notes |
|-------------|--------|--------|-------|
| Unit        | ~60    |        |       |
| Integration | ~15    |        |       |
| Benchmark   | ~5     |        |       |
| **Total**   | ~80    |        | Roadmap target: ~70 |

## Implementation Order (Layers)

| Layer | Tasks (parallel within layer) | Depends on Layer |
|-------|-------------------------------|------------------|
| L0 | T6b.0.1, T6b.1.1, T6b.4.1, T6b.5.1, T6b.6.1, T6b.8.1, T6b.9.1 | — |
| L1 | T6b.1.2, T6b.2.1, T6b.4.2, T6b.5.2, T6b.6.2, T6b.8.2, T6b.9.2 | L0 |
| L2 | T6b.1.3, T6b.2.2, T6b.3.1, T6b.4.3, T6b.6.3, T6b.7.1, T6b.8.3, T6b.9.3, T6b.10.1 | L1 |
| L3 | T6b.1.4, T6b.6.4, T6b.7.2 | L2 |
| L4 | T6b.1.5 | L3 |
| L5 | T6b.1.6, T6b.11.1, T6b.11.2 | L4 |

**Critical path**: L0 (types + encoders) → L1 (adjacency + decoder + loader) → L2 (partition + DAG cut + streaming) → L3 (QEM simplify + progressive) → L4 (recursive DAG) → L5 (validation + demos)

---

## Forward Design Notes

| Future Phase | What this phase prepares | Interface / extension point |
|-------------|------------------------|---------------------------|
| Cooldown #1 | API freeze for `ClusterDAG`, `ChunkLoader`, `MeshletCompressor` | Stable public API surface |
| Phase 7a-1 | Compressed meshlet pipeline for HLR edge extraction | `CompressedMeshlet` format consumed by edge classify compute |
| Phase 7a-2 | LOD-aware BLAS for picking | `ClusterDAG` provides per-LOD BLAS pages |
| Phase 8 | Per-body ClusterDAG in CadScene | `CadSegment` owns `ClusterDAG` handle |
| Phase 10 | Octree streaming for point cloud | `OctreeResidency` + `ChunkLoader` reused |
| Phase 14 | 2B+ tri validation, wavelet compression, OneSweep sort | `ClusterDAG` + streaming infrastructure + `PersistentDispatch` |

---

## Cross-Component Contracts

| Provider Task | File (Exposure) | Consumer Task | Agreed Signature |
|--------------|-----------------|---------------|------------------|
| T6b.1.1 | `ClusterDAG.h` (**public**) | T6b.2.1, T6b.6.3, Phase 7a-1, Phase 8 | `ClusterDAG::Build(MeshletBuildResult const&)`, `ClusterDAG::GetLevel(uint32_t)`, `ClusterNode` struct |
| T6b.1.4 | `ClusterSimplifier.h` (**shared**) | T6b.1.5, T6b.8.2 | `SimplifyClusterGroup(span<Meshlet>, MeshletBuildConfig) -> MeshletBuildResult` |
| T6b.2.2 | `DagCutOptimizer.h` (**public**) | T6b.11.1, Phase 14 | `DagCutOptimizer::Execute(ICommandBuffer&, ClusterDAG const&, CullUniforms const&)` |
| T6b.4.1 | `MeshletCompressor.h` (**public**) | T6b.4.2, T6b.6.1, Phase 7a-1 | `MeshletCompressor::Encode(MeshletBuildResult const&) -> CompressedMeshletData` |
| T6b.5.1 | `PersistentDispatch.h` (**public**) | T6b.5.2, T6b.6.2, Phase 14 | `PersistentDispatch::Create(IDevice&, SlangCompiler&)`, `Submit(ICommandBuffer&, workItems)` |
| T6b.6.1 | `MikiArchive.h` (**public**) | T6b.6.2, Phase 8, Phase 10 | `MikiArchive::Open(path)`, `ReadCluster(clusterId) -> span<byte>` |
| T6b.6.2 | `ChunkLoader.h` (**public**) | T6b.6.3, T6b.7.1, Phase 8 | `ChunkLoader::RequestLoad(clusterId)`, `ChunkLoader::IsResident(clusterId)` |
| T6b.8.1 | `MeshSimplifier.h` (**public**) | T6b.8.2, T6b.11.2, Phase 14 | `MeshSimplifier::Simplify(positions, indices, targetCount) -> SimplifiedMesh` |
| T6b.9.1 | `VisibilityPersistence.h` (**public**) | T6b.9.2, Phase 14 | `VisibilityPersistence::Create(maxMeshlets)`, `GetBuffer() -> BufferHandle` |
