# Phase 03: 2 — Forward Rendering & Depth

**Sequence**: 03 / 27
**Status**: Complete (TextRenderer deferred)
**Started**: 2026-02-01
**Completed**: 2026-03-14

## Goal

Render 1000+ meshes with depth testing, lighting, camera control, ImGui overlay. **All 5 backends**. Real `VkPipeline`/D3D12 PSO/GL program/WebGPU pipeline creation replaces Phase 1a stubs. Descriptor system with push constants and descriptor sets/buffer. Material system with StandardPBR (Cook-Torrance GGX + anisotropic BRDF). GPU text rendering (FreeType + HarfBuzz + MSDF + virtual glyph atlas). Two demos: `forward_cubes` (1000 cubes, orbit camera, ImGui FPS) and `text_demo` (multi-script text rendering).

## Roadmap Digest

### Key Components (from roadmap table)

| # | Component | Core deliverable | Test target |
|---|-----------|-----------------|-------------|
| 1 | Mesh Upload | `MeshData` + `OwnedMeshData` struct (`miki::gfx`), vertex/index buffer creation via `StagingUploader` | ~7 |
| 2 | Pipeline | `GraphicsPipelineDesc` extension (depth, cull, polygon mode), real `CreateGeometryPass` on all 5 backends | ~12 |
| 2b | Swapchain (Windowed Present) | `ISwapchain` (Vulkan + D3D12 single-window present), `GlfwBootstrap` integration. **⚠ Phase 12 refactor target → RenderSurface** | ~6 |
| 3 | Descriptor System | `DescriptorSetLayout`, `PipelineLayout`, `DescriptorSet` on `IDevice`. Push constant unification. Traditional descriptor sets on all backends (descriptor buffer deferred to Phase 4) | ~10 |
| 4 | Material System | `IMaterial` (C++ data + Slang shader), `StandardPBR` (Cook-Torrance GGX + aniso + **Kulla-Conty multi-scattering** + **clearcoat top-layer**), `MaterialRegistry` (hash-dedup), built-in CAD presets, `KullaContyLut` (E_lut + E_avg) | ~12 |
| 5 | Forward Pass | Single-pass forward: vertex transform + PBR + depth, LightBuffer UBO (Phase 2 all tiers, MAX_LIGHTS=4; Phase 3a upgrades Tier1/2 to SSBO), 3-step API (BeginPass/RecordDraws/EndPass), GPU timestamp query, revised Duff-Frisvad ONB | ~11 |
| 6 | ImGui Backend | `ImGuiBackend` — multi-backend ImGui (Vulkan, D3D12, GL, WebGPU), frame stats panel | ~8 |
| 7 | TextRenderer | FreeType font load + HarfBuzz shaping + MSDF generation + virtual glyph atlas + GPU pipeline + RichText + bitmap fallback + outline extraction + GD&T symbol atlas | ~49 |
| 8 | 5-Backend Sync + Demo | `forward_cubes` + `text_demo` on all 5 backends, golden image, CI | ~7 |

### Critical Technical Decisions

- **Real pipeline creation**: Phase 1a `CreateGeometryPass()` returned stub handles. Phase 2 creates real `VkPipeline` / D3D12 PSO / GL program / WebGPU `GPURenderPipeline`. The `IsStubPipeline()` guard in triangle demo is removed.
- **Descriptor system (traditional, Phase 4 upgrades to descriptor buffer)**: Phase 2 uses traditional descriptor sets on **all** backends: Vulkan descriptor sets (`vkAllocateDescriptorSets`), D3D12 root descriptors / descriptor heap, GL UBO/SSBO bindings (`glBindBufferRange`), WebGPU bind groups. `VK_EXT_descriptor_buffer` is **deferred to Phase 4** (`BindlessTable` backend). Phase 2 establishes the `DescriptorSetLayout` / `PipelineLayout` / `DescriptorSet` abstraction on `IDevice` that Phase 4 extends to bindless.
  - **Phase 4 descriptor strategy — `VK_EXT_descriptor_buffer` (confirmed)**:
    Some engine teams have adopted `VK_KHR_push_descriptor` as a simpler alternative to descriptor buffers (see [Reddit r/vulkan, "scrapped descriptor buffers in favour of push descriptors"](https://www.reddit.com/r/vulkan/comments/1jtzhxi/is_it_worth_use_descriptors_buffers/), 2025). However, **miki requires `VK_EXT_descriptor_buffer`** for the following reasons:
    - **Bindless at scale**: Phase 6 virtual geometry (ClusterDAG) requires 10K+ mesh clusters, each indexing vertex/index buffers via `BindlessIndex`. Push descriptors have a per-call limit of `maxPushDescriptors` (typically 32) — fundamentally incompatible with bindless indexing of thousands of resources.
    - **Multi-vendor target**: miki's 5-backend × 4-tier architecture targets AMD RDNA, Intel Arc, and mobile in addition to NVIDIA. Architectural decisions cannot be based on single-vendor performance characteristics.
    - **CAD draw call volume**: Large CAD assemblies have 100K+ parts; even after GPU culling, 10K+ visible draws remain. Push descriptors add per-draw CPU overhead via `vkCmdPushDescriptorSet` calls, causing command buffer bloat at this scale.
    - **D3D12 parity**: D3D12 has no push descriptor equivalent. Its root descriptors (root parameter type `DESCRIPTOR`) support only CBV/SRV/UAV within a 64-DWORD root signature limit. `VK_EXT_descriptor_buffer`'s memory-based model maps directly to D3D12's descriptor heap, enabling a unified cross-API abstraction.
    - **Khronos endorsement**: The official Khronos blog states *"Long term, I think descriptor buffers will change how Vulkan backends are designed"* ([VK_EXT_descriptor_buffer, Khronos Blog](https://www.khronos.org/blog/vk-ext-descriptor-buffer), Hans-Kristian Arntzen).
    - Phase 4 `BindlessTable` will use descriptor buffer on Tier1, traditional descriptor sets with `VK_EXT_descriptor_indexing` on Tier2, `glBindBufferRange`+SSBO on Tier4 (GL), and bind groups+storage buffers on Tier3 (WebGPU).
- **Push constant bridge**: Vulkan/D3D12 use native push constants / root constants. GL uses 128B UBO at binding 0. WebGPU uses 256B UBO at bind group 0 slot 0. Already implemented in Phase 1b — Phase 2 extends to per-draw MVP transforms.
- **Anisotropic BRDF**: GGX-Smith anisotropic NDF (Heitz 2014). `roughnessX = roughness * (1 + anisotropy)`, `roughnessY = roughness * (1 - anisotropy)`. +2 ALU ops vs isotropic. Required for brushed metal, carbon fiber, hair/fabric.
- **Multi-scattering energy compensation (Kulla-Conty 2017)**: Single-scattering GGX loses ~40% energy at roughness=1.0. Kulla-Conty multi-scattering compensation via pre-computed `E_lut` (512×512 R16F) + `E_avg` (512×1 R16F) LUT. Matches Dassault Enterprise PBR spec-2025x. +1 texture fetch + 3 ALU per light.
- **Revised ONB construction**: Duff-Frisvad-Nayar-Stein-Ling (JCGT 2017) replaces original Frisvad method for TBN frame construction from normal. Fixes numerical singularity at normal ≈ (0,0,-1).
- **GPU timestamp query**: Basic `CreateTimestampQueryPool` / `WriteTimestamp` / `GetTimestampResults` on IDevice, implemented on all 5 backends. Enables GPU time measurement for performance targets.
- **Golden image comparison utility**: `GoldenImageCompare()` RMSE-based utility shared by all integration tests.
- **Per-frame-in-flight synchronization (T2.2.3)**: `VulkanSwapchain` uses `kMaxFramesInFlight=2` arrays of semaphores + fences (Diligent/Filament/wgpu industry pattern). `SubmitSyncInfo` struct added to `RhiDescriptors.h` with `waitSemaphores`, `signalSemaphores`, `signalFence` — passed to `IDevice::Submit()` (default `{}` = backward compatible). `AcquireNextImage()` fence-waits for CPU↔GPU throttle; `Present()` rotates `currentFrame_`. All 5 backend `Submit()` signatures updated. See T2.2.3 spec §Architecture Decision for details.
- **DeviceFeature extension management (T2.2.3 prerequisite)**: Backend-agnostic `DeviceFeature` enum + `DeviceFeatureSet` (bitset-backed, O(1) lookup, zero heap). `DeviceConfig` extended with `requiredFeatures` / `optionalFeatures`. Vulkan backend uses `VulkanFeatureMap` to resolve `DeviceFeature` → instance/device extensions with implicit dependency expansion (e.g. `RayTracingPipeline` → `AccelerationStructure` → `BufferDeviceAddress`), Vulkan version awareness (core features skip extensions), and required/optional validation. `GpuCapabilityProfile.enabledFeatures` is the single source of truth — old `bool has*` fields replaced with `Has*()` convenience accessors. All 5 backends + all tests migrated. Inspired by WebGPU `requiredFeatures` / `adapter.features` model.
- **MSDF text**: 4-channel MSDF via msdfgen (CPU, MIT). 32x32 px/em. Virtual texture atlas with LRU page eviction. Resident pages: ASCII + Latin Extended + engineering + GD&T symbols (~580 glyphs). On-demand: CJK, Hangul, Arabic, Cyrillic.
- **ImGui multi-backend**: Uses Dear ImGui's official Vulkan/D3D12/OpenGL3/WebGPU backends. Auto-detected from `IDevice::GetBackendType()`.

### Performance Targets

| Metric | Target |
|--------|--------|
| forward_cubes 1000 cubes | >= 60fps |
| Text rendering 1000 glyphs | < 0.1ms GPU |
| Text rendering 10K glyphs | < 0.5ms GPU |
| Golden image diff | < 5% |
| Build time (clean) | < 10 min |

### Downstream Phase Expectations

| Phase | What it expects from this phase |
|-------|-------------------------------|
| Phase 3a | `GraphicsPipelineDesc` (real pipelines), `Descriptor System` (for render graph resource binding), `Material System` (StandardPBR for deferred GBuffer pass), `Mesh Upload` (for scene geometry), `ImGuiBackend` (for overlay in deferred demo) |
| Phase 3b | `IPipelineFactory::CreateShadowPass()` / `CreateAOPass()` / `CreateAAPass()` — real pipeline creation framework established here; shadow/AO/AA passes build on same infrastructure |
| Phase 4 | `DescriptorSet` / `PipelineLayout` abstraction — Phase 4 extends to `BindlessTable` + `VK_EXT_descriptor_buffer` backend. Phase 2 uses traditional descriptor sets on all backends |
| Phase 5 | `MeshData` struct consumed by ECS MeshComponent; `MaterialRegistry` consumed by ECS MaterialComponent |
| Phase 7b | `TextRenderer` consumed by PMI annotation, measurement labels |
| Phase 9 | `TextRenderer::RichTextSpan` consumed by `RichTextInput` tool |
| Phase 15a | `TextRenderer::GetGlyphOutlines()` consumed by SVG/PDF vector text export |
| Phase 12 | `ISwapchain` (T2.2.3) absorbed and replaced by `RenderSurface`. See T2.2.3 spec §Phase 2↔Phase 12 Relationship for migration contract |

## Phase Dependencies

| Dependency Phase | What it provides |
|-----------------|-----------------|
| Phase 1a | `IDevice` (5 backends), `ICommandBuffer`, `IPipelineFactory` (stub `CreateGeometryPass`), `StagingUploader`, `OrbitCamera`, `FrameManager`, `SlangCompiler` (SPIR-V + DXIL), `OffscreenTarget`, `Format`, `RhiTypes`, `Handle<Tag>`, `GpuCapabilityProfile` |
| Phase 1b | All 5 backend implementations (Vulkan Compat, OpenGL, WebGPU), `SlangCompiler` quad-target (+ GLSL + WGSL), `ShaderWatcher`, `OffscreenTarget` (GL + WebGPU) |

---

## Components & Tasks

### Component 1: Mesh Upload

> Vertex/index buffer creation via `StagingUploader`. `MeshData` struct. Backend-agnostic.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T2.1.1 | MeshData struct + vertex/index upload via StagingUploader | — | M |

### Component 2: Pipeline (Real CreateGeometryPass)

> `GraphicsPipelineDesc` extended with depth test, cull, polygon mode. Real pipeline creation on all backends.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T2.2.1a | GraphicsPipelineDesc extension + real CreateGeometryPass (Vulkan + D3D12) | T2.1.1 | H |
| [x] | T2.2.1b | CopyTextureToBuffer + CopyBufferToTexture + real ReadPixels (all 5 backends) | T2.2.1a | H |
| [x] | T2.2.1c | Golden image comparison utility | T2.2.1b | S |
| [x] | T2.2.2 | CreateGeometryPass (GL + WebGPU + Compat) | T2.2.1a | H |
| [x] | T2.2.3 | ISwapchain (Vulkan + D3D12) + GlfwBootstrap present. **⚠ Phase 12 → RenderSurface** | T2.2.1a | M |

### Component 3: Descriptor System

> `DescriptorSetLayout`, `PipelineLayout`, `DescriptorSet` creation + update. Traditional descriptor sets on all backends (descriptor buffer deferred to Phase 4).

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T2.3.1 | DescriptorSetLayout + PipelineLayout + DescriptorSet on IDevice | — | H |
| [x] | T2.3.2 | Per-draw push constant MVP + descriptor update helpers | T2.3.1 | M |

### Component 4: Material System

> `IMaterial` Slang interface, `StandardPBR` (Cook-Torrance GGX + anisotropic BRDF), `MaterialRegistry`, built-in presets.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T2.4.1 | IMaterial + StandardPBR shader (Cook-Torrance GGX + aniso + clearcoat top-layer) + KullaContyLut | T2.2.1a, T2.3.1 | XH |
| [x] | T2.4.2 | MaterialRegistry (hash-dedup) + CAD material presets | T2.4.1 | M |

### Component 5: Forward Pass

> Single-pass forward: vertex transform + PBR + depth. `LightBuffer` UBO (Phase 2 all tiers, MAX_LIGHTS=4; Phase 3a upgrades Tier1/2 to SSBO), 3-step API (`BeginPass/RecordDraws/EndPass`), revised Duff-Frisvad ONB (JCGT 2017), depthFormat from `CapabilityProfile`.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T2.5.0 | GPU Timestamp Query Infrastructure (all 5 backends) | — | M |
| [x] | T2.5.1 | Forward rendering pass (PBR + depth + directional light) | T2.1.1, T2.2.1a, T2.3.2, T2.4.1, T2.5.0 | H |

### Component 6: ImGui Backend

> Multi-backend ImGui: Vulkan, D3D12, GL, WebGPU. Frame stats panel.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T2.6.1 | ImGuiBackend (Vulkan + D3D12 + GL + WebGPU) + frame stats | T2.2.1a | H |

### Component 7: TextRenderer

> GPU text rendering subsystem. 7 tasks decomposed by subsystem boundary: font loading, shaping, MSDF generation, atlas management, GPU pipeline, rich text layout, and special features (outline extraction + CAD symbols).
>
> **Parallel Track**: Component 7 runs as an independent parallel track alongside Components 1-6. Its only cross-dependency is T2.7.5 requiring T2.2.1a (pipeline) and T2.3.1 (descriptors). A dedicated sub-team can execute T2.7.1-T2.7.4 concurrently with L0-L1 of the forward rendering track.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| DEFERRED | T2.7.1 | FreeType font loading + metrics + system font discovery | — | M |
| DEFERRED | T2.7.2 | HarfBuzz text shaping + script detection + font fallback chain | T2.7.1 | H |
| DEFERRED | T2.7.3 | MSDF glyph generation (msdfgen wrapper + quality tuning) | T2.7.1 | M |
| DEFERRED | T2.7.4 | GlyphAtlas (virtual page + shelf packing + LRU + GPU upload, tier-adaptive budget) | T2.7.1, T2.7.3, T2.2.1b | H |
| DEFERRED | T2.7.5 | GPU text rendering pipeline (instanced quad + MSDF fragment) | T2.7.2, T2.7.4, T2.2.1a, T2.3.1 | H |
| DEFERRED | T2.7.6 | RichTextSpan layout engine + bitmap fallback (crossfade [8,16]px) | T2.7.5, T2.7.2, T2.7.4 | M |
| DEFERRED | T2.7.7 | Glyph outline extraction (Bezier) + special symbol atlas (GD&T/ISO) | T2.7.1, T2.7.4 | M |

### Component 8: 5-Backend Sync + Demo

> `forward_cubes` + `text_demo` on all 5 backends. Golden image diff. CI.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T2.8.1 | forward_cubes demo (1000 cubes, orbit camera, ImGui, 5 backends) + CI | T2.5.1, T2.6.1, T2.2.3 | M |
| DEFERRED | T2.8.2 | text_demo (multi-script, MSDF vs bitmap, 5 backends) + golden image CI | T2.7.6, T2.7.7, T2.8.1 | M |

---

## Demo Plan

### forward_cubes

- **Name**: `demos/forward_cubes/`
- **Shows**: 1000 procedural cubes, orbit camera, Blinn-Phong/PBR, ImGui FPS overlay
- **Requires Tasks**: T2.1.1, T2.2.1a, T2.2.1b, T2.2.1c, T2.2.2, T2.2.3, T2.3.2, T2.4.1, T2.5.1, T2.6.1, T2.8.1
- **CLI**: `--backend vulkan|compat|d3d12|gl|webgpu`
- **Acceptance**:
  - [ ] renders 1000 cubes with depth on all 5 backends
  - [ ] >= 60fps on Vulkan Tier1
  - [ ] ImGui overlay functional on all backends
  - [ ] golden image diff < 5% across backends

### text_demo

- **Name**: `demos/text_demo/`
- **Shows**: ASCII + CJK + engineering symbols at multiple sizes, world-space billboarded labels, MSDF vs bitmap fallback
- **Requires Tasks**: T2.7.1-T2.7.7, T2.8.2
- **CLI**: `--backend vulkan|compat|d3d12|gl|webgpu`
- **Acceptance**:
  - [ ] renders multi-script text on all 5 backends
  - [ ] MSDF quality at 16-72px sizes
  - [ ] bitmap fallback at <8px, crossfade band [8,16]px
  - [ ] golden image diff < 5%

## Test Summary

| Category    | Target | Actual | Notes |
|-------------|--------|--------|-------|
| Unit        | ~127   |        | MeshData(7), Pipeline(6 T2.2.1a + 6 T2.2.2 = 12), TextureCopy(4 T2.2.1b), GoldenImage(3 T2.2.1c), Swapchain(6 T2.2.3), Descriptor(10+4=14), Material(13: T2.4.1=7 incl clearcoat+KullaContyLut, T2.4.2=6), ImGui(8), ForwardPass(11: 9+timestamp+Frisvad), TextRenderer(50: T2.7.1=7, T2.7.2=9, T2.7.3=6, T2.7.4=7, T2.7.5=8, T2.7.6=7, T2.7.7=6) |
| Integration | ~16    |        | ForwardPass(9), forward_cubes E2E(7), text_demo E2E(5) |
| Shader      | ~10    |        | PBR BRDF, MSDF sampling, material permutations |
| **Total**   | ~147   |        | Roadmap target: ~70 (exceeded; text subsystem = 50, ForwardPass = 11, Pipeline = 12, TextureCopy = 4, GoldenImage = 3, Swapchain = 6, Descriptor = 14, Material = 13, MeshData = 7, ImGui = 8, TimestampQuery = 4, forward_cubes E2E = 7, text_demo E2E = 5, shader = 3) |

## Implementation Order (Layers)

| Layer | Tasks (parallel within layer) | Depends on Layer |
|-------|-------------------------------|------------------|
| L0 | T2.1.1, T2.3.1, T2.5.0, T2.7.1 | — (Phase 1a/1b) |
| L1 | T2.2.1a, T2.3.2, T2.7.2, T2.7.3 | L0 |
| L1.5 | T2.2.1b, T2.2.2, T2.2.3 | L1 (T2.2.1a) |
| L2 | T2.2.1c, T2.4.1, T2.6.1, T2.7.4 | L1.5 |
| L3 | T2.4.2, T2.5.1, T2.7.5, T2.7.7 | L2 |
| L4 | T2.8.1, T2.7.6 | L3 |
| L5 | T2.8.2 | L4 |

**Critical path**: L0 → L1 → L1.5 → L2 → L3 → L4 → L5 = **7 layers** (T2.2.1 split adds L1.5 but L1.5 tasks are small; net schedule impact < 0.5 day)

**Total Tasks**: 22 (19 original + 2 from T2.2.1 split + 1 swapchain gap fix T2.2.3) | **Estimated effort**: ~6 weeks (Weeks 5-10 per roadmap) — extended 1 week vs original estimate to accommodate: T2.2.1 split for reviewability, Kulla-Conty LUT + shader, clearcoat top-layer, GPU timestamp query infrastructure, golden image utility, Linux fontconfig, revised Frisvad ONB, bitmap fallback threshold adjustment [8,16]px

**Parallel tracks**: Components 1-6 (forward rendering core) and Component 7 (TextRenderer) can be assigned to separate sub-teams. Component 7 only joins the critical path at L2 (T2.7.4 needs T2.2.1b for CopyBufferToTexture) and L3 (T2.7.5 needs T2.2.1a + T2.3.1)

---

## Forward Design Notes

| Future Phase | What this phase prepares | Interface / extension point |
|-------------|------------------------|---------------------------|
| Phase 3a | `GraphicsPipelineDesc` ready for GBuffer geometry pass; `DescriptorSet`/`PipelineLayout` for render graph resource binding; `Material System` for deferred GBuffer fill; `MeshData` for scene geometry upload | `IPipelineFactory::CreateGeometryPass()` now returns real pipelines; `IDevice::CreateDescriptorSet()` / `UpdateDescriptorSet()` |
| Phase 3b | Pipeline creation framework established; `CreateShadowPass()` / `CreateAOPass()` / `CreateAAPass()` build on same `GraphicsPipelineDesc` + backend dispatch | `IPipelineFactory` virtual methods; `GraphicsPipelineDesc` extensible with shadow/AO-specific fields |
| Phase 4 | `DescriptorSet` / `PipelineLayout` → Phase 4 extends to `BindlessTable` + `VK_EXT_descriptor_buffer` backend + `ResourceHandle` | `IDevice` descriptor methods (traditional descriptor sets); Phase 4 adds descriptor buffer path |
| Phase 5 | `MeshData` → ECS `MeshComponent`; `MaterialRegistry` → ECS `MaterialComponent` | `MeshData` struct; `MaterialRegistry::GetMaterial()` |
| Phase 7b | `TextRenderer` → PMI annotation text; measurement label rendering | `TextRenderer::Draw()`, `RichTextSpan` |
| Phase 9 | `RichTextSpan` → `RichTextInput` interactive tool | `RichTextLayout` + `RichTextSpan` |
| Phase 15a | `TextRenderer::GetGlyphOutlines()` → SVG/PDF vector text export | `GetGlyphOutlines() -> vector<BezierPath>` |
| Phase 12 | `ISwapchain` (T2.2.3) absorbed and replaced by `RenderSurface`. See T2.2.3 spec §Phase 2↔Phase 12 Relationship for migration contract |

---

## Cross-Component Contracts

| Provider Task | File (Exposure) | Consumer Task | Agreed Signature |
|--------------|-----------------|---------------|------------------|
| T2.1.1 | `MeshData.h` (**public** M) | T2.5.1, T2.8.1, Phase 5 | `MeshData{positions, normals, indices}`, `UploadMesh(StagingUploader&, IDevice&, MeshData) -> Result<MeshBuffers>` |
| T2.2.1 | `GraphicsPipelineDesc` (**public** H) | T2.2.2, T2.4.1, T2.5.1, T2.6.1, Phase 3a, 3b | Extended `GeometryPassDesc{ShaderModuleDesc vertexShader, ShaderModuleDesc fragmentShader, vertexLayout, depthTest, depthWrite, depthCompareOp, cullMode, polygonMode, colorFormats, depthFormat, BlendState colorBlend}` |
| T2.2.1 | `IPipelineFactory` (**public** H) | T2.5.1, Phase 3a, 3b | `CreateGeometryPass(GeometryPassDesc) -> Result<PipelineHandle>` returns real pipeline |
| T2.3.1 | `IDevice` descriptor methods (**public** H) | T2.3.2, T2.4.1, T2.5.1, T2.6.1, T2.7.5, Phase 3a, 4 | `CreateDescriptorSetLayout()`, `CreatePipelineLayout()`, `CreateDescriptorSet()`, `UpdateDescriptorSet()`, `DestroyDescriptorSetLayout()`, `DestroyPipelineLayout()`, `DestroyDescriptorSet()` |
| T2.3.2 | Descriptor helpers (**shared** M) | T2.5.1, T2.6.1, T2.7.5 | `DescriptorWriter::WriteBuffer()`, `WriteTexture()`, `Commit()` |
| T2.4.1 | `IMaterial.h` (**public** M), `BrdfLut.h` (**shared** M) | T2.4.2, T2.5.1, Phase 3a, 5 | `IMaterial::ShadeSurface() -> float4` (Slang interface); `StandardPBR` struct with albedo, metallic, roughness, anisotropy, anisotropyAngle, clearcoat, IOR; `BrdfLut::Generate() -> Result<BrdfLutHandles{E_lut, E_avg}>` |
| T2.4.2 | `MaterialRegistry.h` (**public** M) | T2.5.1, T2.8.1, Phase 5 | `MaterialRegistry::Register(StandardPBR) -> MaterialId`, `GetMaterial(MaterialId) -> Result<const StandardPBR&>`, built-in presets |
| T2.5.1 | `ForwardPass.h` (**shared** M) | T2.8.1 | `ForwardPass::BeginPass(cmdBuf, renderTarget)`, `RecordDraws(cmdBuf, span<DrawCall>, camera, LightBuffer)`, `EndPass(cmdBuf)` |
| T2.2.3 | `ISwapchain.h` (**public** H), `RhiDescriptors.h` `SubmitSyncInfo` | T2.8.1, T2.8.2, Phase 3a+, **Phase 12 (replaces)** | `ISwapchain::Create(device, desc) -> Result<unique_ptr<ISwapchain>>`, `AcquireNextImage() -> Result<TextureHandle>`, `Present() -> Result<void>`, `Resize(w,h) -> Result<void>`, `GetFormat()`, `GetExtent()`, `GetSubmitSyncInfo() -> SubmitSyncInfo`. `SubmitSyncInfo{waitSemaphores, signalSemaphores, signalFence}` passed to `IDevice::Submit()`. Demo loop: `Acquire → GetSubmitSyncInfo → Submit(cmd, sync) → Present`. **⚠ Phase 12 deletes ISwapchain, migrates to RenderSurface** |
| T2.5.0 | `IDevice.h` + `ICommandBuffer.h` (**public** H) | T2.5.1, T2.8.1, Phase 3b, 11 | `CreateTimestampQueryPool(count) -> Result<QueryPoolHandle>`, `WriteTimestamp(pool, index)`, `GetTimestampResults(pool, span<uint64_t>)`, `GetTimestampPeriodNs()`, `DestroyQueryPool()` |
| T2.6.1 | `ImGuiBackend.h` (**public** M) | T2.8.1, T2.8.2, Phase 3a+ | `ImGuiBackend::Create(IDevice&, ImGuiPlatformInfo)`, `BeginFrame()`, `EndFrame(ICommandBuffer&)`, `Shutdown()` |
| T2.7.1 | `FontManager.h` (**public** H) | T2.7.2, T2.7.3, T2.7.4, T2.7.7, Phase 7b | `FontManager::Create()`, `LoadFont(path) -> FontId`, `LoadSystemFont()`, `GetGlyphMetrics()`, internal `GetFreetypeFace()` |
| T2.7.2 | `TextShaper.h` (**public** H) | T2.7.5, T2.7.6 | `TextShaper::ShapeText(text, fontId, options) -> GlyphRun`, `SetFallbackChain()`, ASCII fast path |
| T2.7.3 | `MsdfGenerator.h` (**shared** M) | T2.7.4 | `MsdfGenerator::GenerateGlyph(fontMgr, fontId, glyphIdx, config) -> MsdfBitmap`, `GenerateBatch()` |
| T2.7.4 | `GlyphAtlas.h` (**shared** H) | T2.7.5, T2.7.6, T2.7.7 | `GlyphAtlas::GetGlyph(fontId, codepoint) -> GlyphEntry`, `GetPageTexture()`, `FlushUploads()`, shelf packing + LRU |
| T2.7.5 | `TextRenderer.h` (**public** H) | T2.7.6, T2.8.2, Phase 7b, 9 | `TextRenderer::Create()`, `Draw(TextDrawCmd)`, `Flush(cmdBuf, viewProj, screenSize)` |
| T2.7.6 | `RichText.h` (**public** M) | T2.8.2, Phase 7b, 9 | `RichTextLayout::Layout(span<RichTextSpan>, maxWidth) -> Result<LayoutResult>` (flat convenience) + `Layout(span<RichTextNode>, maxWidth) -> Result<LayoutResult>` (tree input), `RichTextNode` tree-of-nodes for nesting, `BitmapGlyphCache` for <8px |
| T2.7.7 | `GlyphOutline.h` (**public** M), `SymbolAtlas.h` (**shared** M) | T2.8.2, Phase 7b, 15a | `GetGlyphOutlines() -> vector<BezierPath>`, `SymbolAtlas::GetSymbol(GdtSymbol)` |

---

## Deferral Record

> **2026-03-14**: Component 7 (TextRenderer, T2.7.1–T2.7.7) and T2.8.2 (text_demo) deferred.
> **Reason**: Forward rendering core (Components 1–6, T2.8.1) complete. TextRenderer is an
> independent parallel track with no downstream blockers for Phase 3a/3b/4. Deferring allows
> the critical path (Render Graph → Deferred → Shadows) to proceed immediately.
> **Destination**: Merged into Phase 3a as an additional component. T2.7.x tasks re-numbered
> as T3a.N.x within Phase 04-3a. T2.8.2 becomes T3a.N+1.1.
> **Impact**: Phase 03-2 is functionally complete for gate purposes (all non-deferred tasks done).
> Phase 04-3a gains ~7+1 tasks and ~3 weeks of parallel-track work.

## Completion Summary

- **Date**: 2026-03-14
- **Tests**: 3 pass / 7 total (4 skipped: D3D12/GL/WebGPU/GoldenParity env-limited)
- **Known limitations**: Component 7 (TextRenderer) + T2.8.2 deferred to Phase 04-3a
- **Design decisions**: See individual task files for architecture decisions
- **Next phase**: Phase 04-3a (Render Graph & Deferred Pipeline + TextRenderer)

### Locked API Surface

*(Filled on phase completion)*

| File | Exposure | Key Exports | Consumed by Phase(s) |
|------|----------|-------------|---------------------|
