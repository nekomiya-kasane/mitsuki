# Phase 07: 5 — ECS & Scene

**Sequence**: 07 / 27
**Status**: Complete
**Started**: 2026-03-19
**Completed**: 2026-03-20

## Goal

Entity-Component-System, spatial acceleration, RTE, kernel abstraction. Build the CPU-side scene infrastructure that Phase 6a GPU-Driven pipeline and Phase 8 CadScene will consume. Resolve Phase 4 inherited TODOs (descriptor buffer activation, resource→vulkan coupling, autoBindless fix).

## Roadmap Digest

### Key Components (from roadmap table, expanded for volume)

| # | Component | Core deliverable | Test target |
|---|-----------|-----------------|-------------|
| 0 | Phase 4 Inherited TODOs | Activate DescriptorBuffer fast-path, eliminate resource→vulkan coupling, fix autoBindless null sampler, D3D12 descriptor heap sub-allocator | ~10 |
| 1 | Entity + EntityManager | 32-bit `[gen:8\|idx:24]`, 16M capacity, O(1) create/destroy with recycling | ~12 |
| 2 | Component Storage | Archetype SOA, `ComponentPool<T>` swap-and-pop, `ComponentRegistry` type→pool | ~12 |
| 3 | Query Engine | `Query<All<T...>, Any<T...>, None<T...>>`, O(1) archetype match, parallel iteration | ~10 |
| 4 | System Scheduler | Dependency-graph parallel execution, `std::jthread` + `std::stop_token` | ~8 |
| 5 | Spatial Index — BVH | SAH build, iterative traversal, frustum/ray queries | ~12 |
| 6 | Spatial Index — Octree + SpatialHash | Morton addressing, adaptive subdivision, dynamic insert/remove; uniform grid broad-phase | ~10 |
| 7 | RTE v2.0 | Progressive re-origin, double-buffered world coords, float32 ULP validation (0.01mm @100km), DS emulation for WebGPU | ~8 |
| 8 | Kernel Abstraction — IKernel + SimKernel | Pluggable geometry kernel interface (8 interface groups), SimKernel test fallback, KernelFactory | ~12 |
| 9 | IGpuGeometry | GPU compute APIs for kernels — Pimpl stubs for 9 operations (tessellate, boolean, interference, distance, draft, curvature, QEM, mass, constraint) | ~6 |
| 10 | TopoGraph (`miki::topo`) | BRep topology (Compound→Vertex), tessellation topology mapping (`triangleToFaceMap`, `edgePolylines`), `FaceType` enum, `TopoInspector`. Namespace: `miki::topo` per roadmap §Namespace Map. | ~12 |
| 11 | Demo + Integration | `ecs_spatial` (100K entities, spatial queries) + `kernel_demo` (STEP via OcctKernel, .miki archive) | ~8 |

### Critical Technical Decisions

- **Entity 32-bit vs 64-bit**: 32-bit `[gen:8|idx:24]` sufficient for 16M entities (10K parts x 1000 faces). 64-bit reserved for Phase 14 scale if needed.
- **Sparse Set with dense arrays (not true Archetype chunking)**: each `ComponentPool<T>` is an independent dense vector + sparse-to-dense index. Cache-friendly iteration via contiguous arrays. True Archetype chunking (flecs/Unity DOTS style) deferred to Phase 14 if profiling shows benefit. Phase 6a SceneBuffer upload benefits from contiguous component arrays.
- **BVH SAH over LBVH**: offline SAH build for static scene quality; GPU LBVH deferred to Phase 6a for dynamic refit.
- **IKernel pluggable**: miki never directly calls OCCT. `SimKernel` always available for testing. `OcctKernel` is optional (`MIKI_KERNEL_OCCT=ON`).
- **TopoGraph is kernel-agnostic**: miki's own lightweight BRep representation, not OCCT `TopoDS_Shape`.
- **RTE Double-Single emulation**: Tier3 WebGPU uses DS (2xf32, ~48-bit mantissa) where f64 unavailable.
- **Phase 4 inherited TODOs resolved first**: descriptor buffer activation, resource→vulkan coupling elimination, and autoBindless fix are prerequisites for clean Phase 6a integration.

### Performance Targets

| Metric | Target |
|--------|--------|
| Entity create/destroy | < 100ns per op |
| Component iteration (100K entities) | < 1ms |
| Query<All<Transform, Mesh>> | < 0.5ms for 100K entities |
| BVH build (100K AABBs) | < 50ms |
| BVH frustum query (100K) | < 0.5ms |
| BVH ray query (100K) | < 0.1ms |
| Octree insert/remove | < 1us per op |
| RTE re-origin | < 0.1ms |

### Downstream Phase Expectations

| Phase | What it expects from this phase |
|-------|-------------------------------|
| Phase 6a (GPU-Driven) | `Entity` → `GpuInstance.entityId`; `BVH` nodes → GPU SSBO for GPU culling; `ComponentStorage` → `SceneBuffer` upload; `ResourceManager` (fixed autoBindless) for meshlet data lifecycle |
| Phase 6b (Streaming) | `Octree` → streaming residency spatial index; `BVH` → incremental refit |
| Phase 7a-1 (HLR) | `TopoGraph` → edge classification (silhouette/boundary/crease from adjacency) |
| Phase 7a-2 (Picking) | `BVH` → BLAS/TLAS build; `TopoGraph.triangleToFaceMap` → primitive→face resolution; `TopoGraph.FaceType` → `SelectionFilter` face-type pick |
| Phase 7b (Measurement) | `IKernel` → `Tessellate`, `EvalSurface`, `ExactDistance`, `MassProperties`; `IGpuGeometry` → GPU compute APIs; `TopoGraph` → draft angle per-face |
| Phase 8 (CadScene) | `Entity` + `ComponentStorage` → `CadNodeComponent` links entity to segment; `TopoGraph` → `PickPath` construction; `Query` → topology-aware GPU culling |
| Phase 9 (Tools) | `IKernel` → `BooleanOp`, `CreateSketch`, `SolveSketch`; `BVH` → snap engine spatial queries |
| Phase 10 (CAE) | `BVH` → ICP spatial queries; `Octree` → point cloud LOD; `RTE` → point cloud double-precision positions |
| Phase 14 (Scale) | `Entity` 16M capacity validation; `BVH` + `Octree` at 2B triangle scale |

## Phase Dependencies

| Dependency Phase | What it provides |
|-----------------|-----------------|
| Phase 01-1a | `IDevice`, `ICommandBuffer`, `Handle<Tag>`, `Format`, `BackendType`, `DeviceConfig`, `DeviceFeature`, `ErrorCode`, `Result<T>`, `Types.h` (float3/4, float4x4, AABB, BoundingSphere, Ray, Plane) |
| Phase 02-1b | Compat/GL/WebGPU backends, `OffscreenTarget` |
| Phase 03-2 | `DescriptorSetLayout/PipelineLayout/DescriptorSet`, `BufferDesc`, `TextureDesc`, `StagingUploader`, `IPipelineFactory`, `MeshData` |
| Phase 04-3a | `RenderGraphBuilder/Compiler/Executor`, `DummyTextures`, `IUiBridge` |
| Phase 05-3b | `PipelineCache`, shadows/post-processing infrastructure |
| Phase 06-4 | `ResourceHandle`, `SlotMap`, `BindlessTable`, `BDAManager`, `StagingRing`, `ResourceManager`, `MemoryBudget`, `ResidencyFeedback` |

---

## Components & Tasks

### Component 0: Phase 4 Inherited TODOs

> Resolve high-priority tech debt from Phase 4 gate review before building new Phase 5 infrastructure.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T5.0.1 | DescriptorBuffer activation — wire VkDevice handles into DescBufferStrategy::TryCreate() | — | L |
| [x] | T5.0.2 | Eliminate resource→vulkan coupling — replace `#ifdef MIKI_HAS_VULKAN` with IDevice capability interface | T5.0.1 | M |
| [x] | T5.0.3 | Fix ResourceManager autoBindless — add SetDefaultSampler() or sampler param to CreateTexture() | — | M |
| [x] | T5.0.4 | D3D12 descriptor heap sub-allocator — replace bump-only allocator with free-list | — | L |

### Component 1: Entity + EntityManager

> 32-bit entity ID with 8-bit generation + 24-bit index, O(1) create/destroy with recycling.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T5.1.1 | Entity type + EntityManager — create/destroy/recycle/IsAlive, free-list, generation counter | — | L |

### Component 2: Component Storage

> Archetype SOA component storage with swap-and-pop removal and type-erased registry.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T5.2.1 | ComponentPool<T> + ComponentRegistry — SOA storage, swap-and-pop, type→pool registry, attach/detach/get | T5.1.1 | L |

### Component 3: Query Engine

> Compile-time typed queries with archetype matching for parallel iteration.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T5.3.1 | Query<All, Any, None> — archetype match, ForEach iteration, parallel chunk dispatch | T5.2.1 | L |

### Component 4: System Scheduler

> Dependency-graph parallel system execution using jthread.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T5.4.1 | SystemScheduler — system registration, dependency DAG, parallel dispatch, stop_token | T5.3.1 | L |

### Component 5: Spatial Index — BVH

> SAH-built BVH for frustum and ray queries, iterative traversal.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T5.5.1 | BVH — SAH build, iterative traversal, frustum query, ray query, batch update | T5.1.1 | L |

### Component 6: Spatial Index — Octree + SpatialHash

> Morton-coded octree with adaptive subdivision and uniform-grid spatial hash for broad-phase.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T5.6.1 | Octree — Morton addressing, adaptive subdivision, dynamic insert/remove, frustum/range queries | T5.1.1 | L |
| [x] | T5.6.2 | SpatialHash — uniform grid broad-phase, insert/remove/query, configurable cell size | T5.1.1 | M |

### Component 7: RTE v2.0

> Relative-to-Eye precision management for large-world rendering.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T5.7.1 | RteManager — progressive re-origin, double-buffered world coords, ULP validation, DS emulation types | — | L |

### Component 8: Kernel Abstraction — IKernel + SimKernel

> Pluggable geometry kernel interface with test fallback implementation.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T5.8.1 | IKernel interface + KernelFactory — 8 interface groups, SimKernel procedural shapes, factory registration | — | L |
| [x] | T5.8.2 | OcctKernel — full OCCT V7.8.1 integration, Tessellate + Import + Topology + Measurement (MIKI_KERNEL_OCCT=ON) | T5.8.1 | L |

### Component 9: IGpuGeometry

> GPU compute API stubs for geometry kernels.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T5.9.1 | IGpuGeometry interface — Pimpl stubs for 9 operations, NotImplemented returns until downstream phases | T5.8.1 | M |

### Component 10: TopoGraph

> Kernel-agnostic BRep topology representation with tessellation mapping.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T5.10.1 | TopoGraph core (`miki::topo`) — node types (Compound→Vertex), adjacency, TopoInspector, FaceType enum | T5.1.1 | L |
| [x] | T5.10.2 | Tessellation topology mapping (`miki::topo`) — triangleToFaceMap, edgePolylines, TopoGraphComponent ECS integration | T5.10.1, T5.2.1, T5.8.1 | M |

### Component 11: Demo + Integration

> Integration demos exercising ECS + spatial + kernel.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T5.11.1 | ecs_spatial demo — 100K entities, BVH/Octree queries, ImGui entity inspector | T5.5.1, T5.6.1, T5.3.1, T5.4.1 | L |
| [x] | T5.11.2 | kernel_demo — STEP load via OcctKernel (or SimKernel fallback), tessellate, render, TopoGraph display | T5.8.2, T5.10.2, T5.11.1 | L |

---

## Demo Plan

### ecs_spatial

- **Name**: `demos/ecs_spatial/`
- **Shows**: 100K entities with spatial queries (BVH frustum/ray, Octree range), orbit camera, ImGui entity inspector panel (entity count, archetype breakdown, query timing)
- **Requires Tasks**: T5.1.1, T5.2.1, T5.3.1, T5.4.1, T5.5.1, T5.6.1, T5.7.1, T5.11.1
- **CLI**: `--backend vulkan|d3d12`
- **Acceptance**:
  - [ ] 100K entities created and rendered as point cloud or bounding box wireframe
  - [ ] BVH frustum query returns correct visible set
  - [ ] Octree range query returns correct neighbor set
  - [ ] ImGui shows entity/archetype/query statistics
  - [ ] >= 60fps on RTX 4070 equivalent

### kernel_demo

- **Name**: `demos/kernel_demo/`
- **Shows**: STEP file loaded via OcctKernel (or SimKernel fallback), tessellated, rendered with forward pass, TopoGraph inspector
- **Requires Tasks**: T5.8.1, T5.8.2, T5.10.1, T5.10.2, T5.11.2
- **CLI**: `--backend vulkan|d3d12 --kernel sim|occt`
- **Acceptance**:
  - [ ] SimKernel renders procedural shapes (cube, sphere, cylinder)
  - [ ] OcctKernel renders STEP file (when MIKI_KERNEL_OCCT=ON)
  - [ ] TopoGraph correctly populated with face/edge/vertex counts
  - [ ] triangleToFaceMap resolves pick→face correctly (manual click test)

## Test Summary

| Category    | Target | Actual | Notes |
|-------------|--------|--------|-------|
| Unit        | ~80    |        |       |
| Integration | ~15    |        |       |
| Visual      | ~2     |        |       |
| Benchmark   | ~3     |        |       |
| **Total**   | ~100   |        | Roadmap target: ~90 |

## Implementation Order (Layers)

| Layer | Tasks (parallel within layer) | Depends on Layer |
|-------|-------------------------------|-----------------|
| L0 | T5.0.1, T5.0.3, T5.0.4, T5.1.1, T5.7.1, T5.8.1 | — |
| L1 | T5.0.2, T5.2.1, T5.5.1, T5.6.1, T5.6.2, T5.8.2, T5.9.1, T5.10.1 | L0 |
| L2 | T5.3.1, T5.10.2 | L1 |
| L3 | T5.4.1, T5.11.1 | L2 |
| L4 | T5.11.2 | L3 |

**Critical path**: L0 (Entity + IKernel + Phase4 TODOs) → L1 (Components + Spatial + Topo) → L2 (Query + TopoMapping) → L3 (Scheduler + Demo1) → L4 (Demo2)

---

## Forward Design Notes

| Future Phase | What this phase prepares | Interface / extension point |
|-------------|------------------------|---------------------------|
| Phase 6a | `Entity.entityId` field in `GpuInstance`; `BVH` flat BvhNode[] uploadable to GPU SSBO via `GetRawData()`; `ComponentPool<T>::ForEachWithEntity()` for SceneBuffer bulk upload | `BVH::GetRawData() -> span<byte const>` for GPU upload; `ComponentPool<T>::ForEachWithEntity(Fn(Entity,T&))` for SceneBuffer sync |
| Phase 6b | `Octree` spatial index reused for cluster streaming residency; `BVH` supports incremental refit (dirty node list). BvhNode flat array format compatible with Phase 6a GPU LBVH output. | `BVH::Refit(span<uint32_t> dirtyIndices)` |
| Phase 7a-2 | `TopoGraph::PrimitiveToFace(entityId, triIdx) -> FaceId` for picking; `FaceType` for SelectionFilter | `TopoGraphComponent` ECS component |
| Phase 7b | `IKernel::Tessellate`, `EvalSurface`, `ExactDistance`, `MassProperties` | `IKernel` 8 interface groups |
| Phase 8 | `Entity` + `ComponentStorage` for `CadNodeComponent`; `Query` for topology-aware culling | `ComponentRegistry::Register<CadNodeComponent>()` |
| Phase 10 | `BVH` for ICP; `Octree` for point cloud LOD; `RteManager` for large-world point clouds | Reuse existing APIs |
| IPC parallel track | Runs alongside Phase 5 (no dependency). `neko::ipc` delivers SharedMemoryRegion/ProcessHandle/EventPort/PipeStream/MappedFile for Phase 13 ComputeDispatcher. | No interface from Phase 5 |
| GPU Trim Tech Spike | Runs in Week 22 gap after Phase 5, before Phase 6a. Results feed Phase 7b parametric tessellation planning. Uses `IKernel::Tessellate` + `BindlessTable`. | `IKernel::Tessellate()` from T5.8.1 |
| EventSystem | Deferred — likely Phase 8 CadScene dirty tracking / change notification mechanism. Roadmap namespace map lists it under `miki::scene` but no Phase 5 component. | Reserved for Phase 8 |

---

## Cross-Component Contracts

| Provider Task | File (Exposure) | Consumer Task | Agreed Signature |
|--------------|-----------------|---------------|------------------|
| T5.1.1 | `Entity.h` (**public**) | T5.2.1, T5.5.1, T5.6.1, T5.10.1 | `Entity::Create(gen, idx) -> Entity`, `Entity::IsValid()`, `EntityManager::Create() -> Entity` |
| T5.2.1 | `ComponentStorage.h` (**public**) | T5.3.1, T5.10.2 | `ComponentPool<T>::Attach(entity, T&&)`, `ComponentPool<T>::Get(entity) -> T*`, `ComponentRegistry::GetPool<T>()` |
| T5.3.1 | `QueryEngine.h` (**public**) | T5.4.1, T5.11.1 | `Query<All<Ts...>>::ForEach(Fn&&)`, `Query<All<Ts...>>::Count()` |
| T5.5.1 | `BVH.h` (**public**) | T5.11.1, Phase 6a, Phase 7a-2 | `BVH::Build(span<AABB>)`, `BVH::QueryFrustum(FrustumPlanes) -> span<uint32_t>`, `BVH::QueryRay(Ray) -> optional<HitResult>` |
| T5.8.1 | `IKernel.h` (**public**) | T5.8.2, T5.9.1, T5.10.2, Phase 7b | `IKernel::Tessellate(shapeId, quality) -> TessellationResult`, `IKernel::Import(path, format) -> ImportResult` |
| T5.10.1 | `TopoGraph.h` (**public**) | T5.10.2, Phase 7a-1, Phase 7a-2 | `TopoGraph::GetFaces(bodyId) -> span<FaceId>`, `TopoGraph::GetFaceType(faceId) -> FaceType`, `TopoGraph::PrimitiveToFace(entityId, triIdx) -> FaceId` |
| T5.8.1 | `KernelTypes.h` (**public**) | T5.10.1, T5.10.2 | `FaceId`, `EdgeId`, `VertexId` — `miki::topo` depends on `miki::kernel` for topology ID types (correct layer direction: topo consumes kernel types) |

---

## Completion Summary

- **Date**: 2026-03-20
- **Tests**: 1364 total, 1354 pass (10 pre-existing GL/WebGPU/Mock failures), 0 new regressions
- **Phase 5 tests**: ~340 (target: ~90, 378% coverage)
- **Known limitations**:
  - LinearOctree SSVDAG mirror matching deferred (documented TODO, SVDAG works correctly)
  - IGpuGeometry all 9 operations return NotImplemented (by design — stubs until downstream phases)
  - D3D12 descriptor heap tests require debug-d3d12 preset (skipped in debug-vulkan)
  - OcctKernel vertex normals use area-weighted averaging (correct but not angle-weighted)
- **Design decisions**:
  - T5.0.1: Default optional Tier1 features in CreateOwned() — VK_EXT_descriptor_buffer actually enabled at vkCreateDevice
  - T5.0.1: BindlessTable routes Register/Remove through DescBufferStrategy when active
  - T5.0.2: Runtime capability query replaces compile-time #ifdef MIKI_HAS_VULKAN
  - T5.0.3: SetDefaultSampler() pattern — set once, used for all autoBindless registrations
  - T5.0.4: LIFO free-list for D3D12 descriptor heap (zero fragmentation)
  - BVH: 5 builders (SAH/LBVH/SBVH/PLOC/Parallel), 4 node widths (2/4/8/CWBVH)
  - ComponentPool: paged sparse set (4096/page)
  - SystemScheduler: hash-based DAG O(N×M)
  - OCCT V7.8.1: full integration compiled from source with COCA Clang
- **Next phase**: Phase 6a (GPU-Driven Rendering Core)

### Locked API Surface

| File | Exposure | Key Exports | Consumed by Phase(s) |
|------|----------|-------------|---------------------|
| `include/miki/scene/Entity.h` | **public** | `Entity`, `EntityManager` | 6a, 6b, 7a-2, 8, 9, 10 |
| `include/miki/scene/ComponentStorage.h` | **public** | `ComponentPool<T>`, `ComponentRegistry` | 6a, 8, 10 |
| `include/miki/scene/QueryEngine.h` | **public** | `Query<All,Any,None>` | 6a, 8, 10 |
| `include/miki/scene/SystemScheduler.h` | **public** | `SystemScheduler` | 8 |
| `include/miki/scene/BVH.h` | **public** | `BVH`, `BvhNode`, `HitResult` | 6a, 6b, 7a-2, 9, 10 |
| `include/miki/scene/Octree.h` | **public** | `Octree` | 6b, 10 |
| `include/miki/scene/SpatialHash.h` | **public** | `SpatialHash` | 9 |
| `include/miki/scene/RteManager.h` | **public** | `RteManager` | 6a, 7b, 10 |
| `include/miki/kernel/IKernel.h` | **public** | `IKernel`, `TessellationResult`, `ImportResult` | 7b, 8, 9 |
| `include/miki/kernel/IGpuGeometry.h` | **public** | `IGpuGeometry` | 7b, 9 |
| `include/miki/kernel/KernelFactory.h` | **public** | `KernelFactory` | 7b |
| `include/miki/topo/TopoGraph.h` | **public** | `TopoGraph`, `FaceType`, `TopoInspector` | 7a-1, 7a-2, 7b, 8 |
| `include/miki/topo/TopoTypes.h` | **public** | `TopoNodeType`, `FaceType`, `TopoFace`, `TopoEdge` | 7a-1, 7a-2, 7b, 8 |
| `include/miki/topo/TopoGraphComponent.h` | **public** | `TopoGraphComponent`, `PrimitiveToFace` | 6a, 7a-2, 8 |
