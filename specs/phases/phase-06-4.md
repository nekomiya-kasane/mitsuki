# Phase 06: 4 — Resource Management & Bindless

**Sequence**: 06 / 27
**Status**: Not Started
**Started**: —
**Completed**: —

## Goal

Production resource pipeline — bindless, BDA, streaming, memory budget. Starts after Phase 3a milestone gate, runs in parallel with Phase 3b. **Tier-differentiated**: Tier1 uses descriptor buffer (`VK_EXT_descriptor_buffer`) + BDA; Tier2 uses descriptor sets + BDA; Tier4 (GL) uses `glBindBufferRange` + SSBO bindings; Tier3 (WebGPU) uses bind groups + storage buffers. All share the same `ResourceHandle` and `BindlessIndex` abstraction.

## Roadmap Digest

### Key Components (from roadmap table, expanded for volume)

| # | Component | Core deliverable | Test target |
|---|-----------|-----------------|-------------|
| 1 | ResourceHandle + SlotMap | 8B packed handle `[type:4][generation:12][index:32][reserved:16]`; generational slot allocator O(1) alloc/free/lookup | ~12 |
| 2 | BindlessTable | Global descriptor set (v1) + descriptor buffer backend (v2, `VK_EXT_descriptor_buffer`); `BindlessIndex` (4B); auto-grow; tier fallback | ~14 |
| 3 | BDAManager | Buffer device address tracking; `BDAPointer` (16B) with FNV-1a checksum; per-backend address query | ~10 |
| 4 | StagingRing | Per-frame ring buffer for CPU->GPU copies; auto-fence; wraparound; multi-backend | ~10 |
| 5 | ResourceManager | Unified create/destroy facade; deferred destruction (per-frame free lists); lifetime tracking; integrates SlotMap + BindlessTable + BDAManager + StagingRing | ~12 |
| 6 | Memory Budget | Per-category tracking (geometry/texture/staging/accel); pressure states (Normal=<70%/Warning=70-85%/Critical=85-95%/OOM=>95%, per arch doc §20.5); LRU eviction callbacks; VMA budget integration | ~10 |
| 7 | Residency Feedback | GPU access tracking buffer (per-resource counter in fragment/compute shader); residency compute shader analyzes access patterns -> load/evict priorities; overcommit detection | ~8 |
| 8 | Demo + Integration | `bindless_scene` — 10K objects via bindless indices; memory budget + residency feedback ImGui panel; golden image | ~10 |

### Critical Technical Decisions

- **Descriptor buffer vs descriptor set**: Tier1 prefers `VK_EXT_descriptor_buffer` (zero-allocation descriptor update, GPU-mapped descriptor memory). Fallback to traditional `vkAllocateDescriptorSets` when extension unavailable. Feature-detected at device init via `DeviceFeature::DescriptorBuffer`.
- **ResourceHandle != RHI Handle**: `ResourceHandle` (8B, `miki::resource` namespace) is the **application-level** handle for scene objects. `rhi::Handle<Tag>` (8B, `miki::rhi` namespace) is the **RHI-level** handle for GPU resources. `ResourceManager` maps ResourceHandle -> one or more RHI handles (texture + views + sampler).
- **SlotMap design**: generational, dense free-list, O(1) alloc/free/lookup. Separate from `VulkanHandlePool` (which is RHI-internal). SlotMap is a generic data structure usable by ECS (Phase 5) and resource layer.
- **BindlessIndex**: 4B index into the global descriptor set/buffer. Passed in push constants or SSBO. Shaders access resources via `bindlessTextures[index]` / `bindlessBuffers[index]`.
- **BDAPointer**: 16B `{address:uint64, checksum:uint32, size:uint32}`. FNV-1a checksum of address bytes for debug-build validation. `vkGetBufferDeviceAddress` (Vulkan 1.2 core) / `ID3D12Resource::GetGPUVirtualAddress` (D3D12).
- **StagingRing**: persistent-mapped ring buffer. Per-frame fence tracking for safe wraparound. Size = 64MB default (configurable). Overflow -> allocate temporary staging buffer.
- **Deferred destruction**: resources are not destroyed immediately. `ResourceManager::Destroy(handle)` pushes to a per-frame free list. After N frames (kMaxFramesInFlight), actual RHI destroy is called. Prevents use-after-free from in-flight GPU work.
- **Memory budget integration**: VMA `vmaSetCurrentFrameIndex` + `vmaGetBudget` (Vulkan). D3D12: `IDXGIAdapter3::QueryVideoMemoryInfo`. GL/WebGPU: application-level tracking only (no OS budget API).
- **Residency feedback**: GPU compute shader reads per-resource access counters (atomic increment in fragment/compute shaders), produces priority list. CPU reads back asynchronously (1-frame latency). Used by ChunkLoader (Phase 6b) for streaming priority.

### Performance Targets

| Metric | Target |
|--------|--------|
| SlotMap alloc/free | < 50ns per op |
| BindlessTable grow (1K -> 2K entries) | < 1ms |
| StagingRing upload throughput | >= 4 GB/s (PCIe 3.0 x16) |
| Deferred destruction overhead | < 0.1ms/frame for 10K resources |
| Memory budget query | < 0.01ms |
| Residency feedback compute | < 0.5ms for 100K resources |

### Downstream Phase Expectations

| Phase | What it expects from this phase |
|-------|-------------------------------|
| Phase 5 (ECS) | `ResourceHandle` for entity resource references; `ResourceManager` for lifecycle; `BDAManager` for `IGpuGeometry` GPU pointers; `BindlessTable` for `IGpuGeometry` resource access |
| Phase 6a (GPU-Driven) | `BDAManager` for mesh shader BDA vertex fetch; `BindlessTable` for vis buffer material resolve; `ResourceManager` for meshlet data lifecycle |
| Phase 6b (Streaming) | `MemoryBudget` for VRAM pressure-driven LRU eviction; `ResidencyFeedback` for streaming priority; `StagingRing` for cluster upload |
| Phase 7a-2 (CAD) | `BDAManager` for double-precision measurement; `BindlessTable` for material bindless access |
| Phase 7b (CAD Precision) | `BDAManager` with float64 support for exact distance/mass-properties |
| Phase 14 (Scale) | `MemoryBudget` for 2B tri VRAM stress test; `ResidencyFeedback` for residency correctness validation |
| GPU Trim Spike | `BindlessTable` for SDF trim texture bindless access |

## Phase Dependencies

| Dependency Phase | What it provides |
|-----------------|-----------------|
| Phase 1a (01-1a) | `IDevice`, `ICommandBuffer`, `Handle<Tag>`, `Format`, `BackendType`, `DeviceConfig`, `DeviceFeature`, `ErrorCode`, `Result<T>` |
| Phase 1b (02-1b) | Compat/GL/WebGPU backends, `OffscreenTarget`, `ReadbackBuffer` |
| Phase 2 (03-2) | `DescriptorSetLayout/PipelineLayout/DescriptorSet`, `BufferDesc`, `TextureDesc`, `StagingUploader`, `IPipelineFactory`, `MeshData` |
| Phase 3a (04-3a) | `RenderGraphBuilder/Compiler/Executor` (transient aliasing integration for MemoryBudget), `DummyTextures` |

---

## Components & Tasks

### Component 1: ResourceHandle + SlotMap

> 8B packed application-level handle + generational O(1) slot allocator.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T4.1.1 | SlotMap — generational slot allocator (header-only, generic) | — | M |
| [x] | T4.1.2 | ResourceHandle — 8B packed handle + ResourceType enum + SlotMap integration | T4.1.1 | M |

### Component 2: BindlessTable

> Global descriptor set (v1) + descriptor buffer backend (v2). Tier-differentiated. Auto-grow.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T4.2.1a | BindlessTable core — public API, auto-grow, free-list, Mock + Vulkan descriptor set backend | T4.1.2 | L |
| [x] | T4.2.1b | BindlessTable tier dispatch — D3D12 descriptor heap, GL UBO/SSBO, WebGPU bind group backends | T4.2.1a | M |
| [x] | T4.2.2 | BindlessTable descriptor buffer backend (v2) — `VK_EXT_descriptor_buffer` path, feature-detect + fallback | T4.2.1a | L |

### Component 3: BDAManager

> Buffer device address tracking. BDAPointer with FNV-1a checksum.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T4.3.1 | BDAManager — address query (Vulkan/D3D12), BDAPointer (16B), FNV-1a checksum, registration table | T4.1.2 | M |

### Component 4: StagingRing

> Per-frame ring buffer for CPU->GPU copies. Auto-fence. Wraparound.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T4.4.1 | StagingRing — persistent-mapped ring buffer, per-frame fence, wraparound, overflow fallback | — | L |

### Component 5: ResourceManager

> Unified create/destroy facade. Deferred destruction. Lifetime tracking.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T4.5.1a | ResourceManager core — SlotMap-backed create/destroy facade, deferred destruction, per-frame free lists, RHI handle retrieval | T4.1.2, T4.4.1 | L |
| [x] | T4.5.1b | ResourceManager integration — auto-register BindlessTable/BDA on create, auto-unregister on destroy, Tick logic, budget Track/Release callbacks | T4.5.1a, T4.2.1a, T4.3.1 | M |

### Component 6: Memory Budget

> Per-category VRAM tracking. Pressure states. LRU eviction callbacks.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T4.6.1 | MemoryBudget — per-category tracking, pressure states, LRU eviction callbacks, VMA/DXGI budget integration | T4.5.1b | L |

### Component 7: Residency Feedback

> GPU access counter buffer. Compute shader analysis. Overcommit detection.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T4.7.1 | ResidencyFeedback — GPU access counter buffer, residency compute shader, readback + priority analysis, overcommit detection | T4.6.1 | L |

### Component 8: Demo + Integration

> bindless_scene demo: 10K objects, memory budget + residency ImGui panel.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T4.8.1 | bindless_scene demo — 10K objects via bindless indices, memory budget + residency feedback ImGui panel, golden image | T4.5.1b, T4.6.1, T4.7.1 | L |

---

## Demo Plan

### bindless_scene

- **Name**: `demos/bindless_scene/`
- **Shows**: 10K procedural objects (cubes/spheres) rendered via bindless indices, memory budget ImGui panel (per-category bars, pressure state indicator), residency feedback heatmap overlay, StagingRing upload stats
- **Requires Tasks**: T4.8.1 (all preceding tasks)
- **CLI**: `--backend vulkan|compat|d3d12|gl|webgpu`
- **Acceptance**:
  - [ ] 10K objects rendered correctly on Vulkan (descriptor buffer path if available)
  - [ ] 10K objects rendered correctly on D3D12 (descriptor set fallback)
  - [ ] Memory budget panel shows correct per-category VRAM
  - [ ] Residency feedback counter increments visible in debug overlay
  - [ ] Golden image parity: PSNR > 30 dB vs reference (Vulkan Tier1)
  - [ ] FPS > 60 on RTX 4070 equivalent

## Test Summary

| Category    | Target | Actual | Notes |
|-------------|--------|--------|-------|
| Unit        | ~66    |        | T4.1.1(10), T4.1.2(8), T4.2.1a(8), T4.2.1b(6), T4.2.2(8), T4.3.1(10), T4.4.1(10), T4.5.1a(8), T4.5.1b(8), T4.6.1(10), T4.7.1(8), T4.8.1(6) |
| Integration | ~15    |        | EndToEnd tests per component + bindless_scene golden image |
| Shader      | ~3     |        | Residency counter increment, bindless texture fetch, BDA pointer dereference |
| Benchmark   | ~4     |        | SlotMap throughput, StagingRing bandwidth, BindlessTable grow, deferred destruction overhead |
| **Total**   | **~88**|        | Roadmap target: ~60. Expanded 1.5x due to: (a) descriptor buffer separate task, (b) benchmark tests, (c) tier-specific integration tests, (d) T4.2.1/T4.5.1 splits |

## Implementation Order (Layers)

| Layer | Tasks (parallel within layer) | Depends on Layer |
|-------|-------------------------------|------------------|
| L0 | T4.1.1 (SlotMap), T4.4.1 (StagingRing) | — (Phase 3a) |
| L1 | T4.1.2 (ResourceHandle) | L0 |
| L2 | T4.2.1a (BindlessTable core + Vulkan/Mock), T4.3.1 (BDAManager) | L1 |
| L3 | T4.2.1b (BindlessTable D3D12/GL/WebGPU), T4.5.1a (ResourceManager core) | L2 |
| L4 | T4.2.2 (BindlessTable v2 descriptor buffer), T4.5.1b (ResourceManager integration) | L3 |
| L5 | T4.6.1 (MemoryBudget) | L4 |
| L6 | T4.7.1 (ResidencyFeedback) | L5 |
| L7 | T4.8.1 (Demo + golden images) | L6 |

**Critical path**: L0 -> L1 -> L2 -> L3 -> L4 -> L5 -> L6 -> L7 = **8 layers**

**Parallel opportunity**: L0 has SlotMap + StagingRing (independent). L2 has BindlessTable core + BDAManager (independent). L3 has BindlessTable tier dispatch + ResourceManager core (independent). L4 has descriptor buffer v2 + ResourceManager integration (independent).

**Rationale for layer assignments**:
- **L0**: SlotMap and StagingRing have zero inter-dependency. Both are foundational data structures.
- **L1**: ResourceHandle needs SlotMap for backing storage.
- **L2**: BindlessTable core (Vulkan/Mock) + BDAManager are independent; both need ResourceHandle.
- **L3**: Tier dispatch (D3D12/GL/WebGPU) extends core. ResourceManager core needs SlotMap + StagingRing (L0-L1) but not BindlessTable yet.
- **L4**: Descriptor buffer (v2) extends BindlessTable core. ResourceManager integration wires BindlessTable + BDA auto-register.
- **L5-L6**: Budget and Residency are layered on top of ResourceManager integration.
- **L7**: Demo needs everything.

---

## Forward Design Notes

| Future Phase | What this phase prepares | Interface / extension point |
|-------------|------------------------|---------------------------|
| Phase 5 | `ResourceHandle` used as ECS component field for entity->resource mapping | `ResourceHandle` value type, `ResourceManager::IsAlive(handle)` |
| Phase 6a | BDA vertex fetch in mesh shader; BindlessTable for material resolve | `BDAManager::GetPointer(bufferHandle) -> BDAPointer`, `BindlessTable::GetIndex(textureHandle) -> BindlessIndex` |
| Phase 6b | Streaming LRU integrated with MemoryBudget pressure callbacks | `MemoryBudget::RegisterEvictionCallback(category, fn)`, `ResidencyFeedback::GetPriorityList()` |
| Phase 7b | BDAPointer float64 for exact measurement | `BDAPointer.address` is `uint64_t` — compatible with float64 buffer addresses |
| Phase 14 | VRAM stress test uses MemoryBudget hard limit mode | `MemoryBudget::SetHardLimit(category, bytes)` |
| Phase 5+ | `StagingUploader` (Phase 2) coexists with `StagingRing` — different use cases. `StagingUploader` = one-time init uploads (texture, mesh). `StagingRing` = per-frame dynamic uploads (SceneBuffer, uniform updates). Evaluate deprecation of `StagingUploader` at Phase 5 gate: if all call sites can migrate to `StagingRing::Allocate`, deprecate. | `StagingRing` replaces `StagingUploader` for dynamic; keeps `StagingUploader` for large one-time uploads that exceed ring capacity |

---

## Cross-Component Contracts

> **Handle type convention**: `rhi::TextureHandle` / `rhi::BufferHandle` = RHI-level GPU handles (`miki::rhi`). `resource::ResourceHandle` = application-level handle (`miki::resource`). `BindlessTable` and `BDAManager` operate on **RHI handles** (they are resource-layer internals). `ResourceManager` is the facade that maps `resource::ResourceHandle` ↔ `rhi::*Handle`.

| Provider Task | File (Exposure) | Consumer Task | Agreed Signature |
|--------------|-----------------|---------------|------------------|
| T4.1.1 | `SlotMap.h` (**public** H) | T4.1.2, T4.5.1, Phase 5 ECS | `SlotMap<T>::Allocate() -> {Key, T&}`, `SlotMap<T>::Free(Key)`, `SlotMap<T>::Get(Key) -> T*` |
| T4.1.2 | `ResourceHandle.h` (**public** H) | T4.2.1, T4.3.1, T4.5.1, Phase 5, Phase 6a | `resource::ResourceHandle` 8B packed, `ResourceType` enum, `IsValid()`, `Index()`, `Generation()`, `Type()` |
| T4.2.1 | `BindlessTable.h` (**public** H) | T4.2.2, T4.5.1, T4.8.1, Phase 6a, Phase 6b, GPU Trim Spike | `BindlessTable::Create(rhi::IDevice&)`, `RegisterTexture(rhi::TextureHandle, rhi::SamplerHandle) -> BindlessIndex`, `RegisterBuffer(rhi::BufferHandle, offset, range) -> BindlessIndex`, `Remove(BindlessIndex)`, `GetDescriptorSet() -> rhi::DescriptorSetHandle` |
| T4.3.1 | `BDAManager.h` (**public** H) | T4.5.1, Phase 5, Phase 6a, Phase 7b | `BDAManager::Create(rhi::IDevice&)`, `Register(rhi::BufferHandle, size) -> BDAPointer`, `Remove(rhi::BufferHandle)`, `Validate(BDAPointer) -> bool` |
| T4.4.1 | `StagingRing.h` (**public** M) | T4.5.1, Phase 6b | `StagingRing::Create(rhi::IDevice&, capacity)`, `Allocate(size) -> StagingAllocation{rhi::BufferHandle, offset, mappedPtr}`, `FlushFrame(fenceValue)` |
| T4.5.1 | `ResourceManager.h` (**public** H) | T4.6.1, T4.7.1, T4.8.1, Phase 5, Phase 6a, Phase 6b | `ResourceManager::CreateTexture/Buffer/... -> Result<resource::ResourceHandle>`, `Destroy(resource::ResourceHandle)`, `GetTextureHandle(resource::ResourceHandle) -> optional<rhi::TextureHandle>`, `GetBindlessIndex(resource::ResourceHandle) -> BindlessIndex`, `Tick(frameIndex)` |
| T4.6.1 | `MemoryBudget.h` (**public** M) | T4.7.1, T4.8.1, Phase 6b, Phase 14 | `MemoryBudget::Track(category, bytes)`, `Release(category, bytes)`, `GetPressureState() -> PressureState`, `RegisterEvictionCallback(category, fn)` |
| T4.7.1 | `ResidencyFeedback.h` (**public** M) | T4.8.1, Phase 6b, Phase 14 | `ResidencyFeedback::Create(rhi::IDevice&, maxResources)`, `GetCounterBuffer() -> rhi::BufferHandle`, `Analyze() -> PriorityList`, `ResetCounters()` |

---

## Completion Summary

*(Filled on phase completion)*

- **Date**: —
- **Tests**: — pass / — total
- **Known limitations**: Descriptor buffer (v2) requires `VK_EXT_descriptor_buffer` — Tier2/3/4 always use descriptor set fallback. GL/WebGPU residency feedback is application-level only (no OS budget API). ChunkLoader deferred to Phase 6b.
- **Design decisions**: —
- **Next phase**: Phase 07-5 (ECS & Scene)

### Locked API Surface

*(Filled on phase completion)*

| File | Exposure | Key Exports | Consumed by Phase(s) |
|------|----------|-------------|---------------------|
