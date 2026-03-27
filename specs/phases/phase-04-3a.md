# Phase 04: 3a — Render Graph & Deferred Pipeline + TextRenderer

**Sequence**: 04 / 27
**Status**: Not Started
**Started**: —
**Completed**: —

## Goal

Declarative render graph, GBuffer, deferred PBR lighting, IBL environment mapping, tone mapping. This is the engine's backbone — all subsequent GPU work plugs in as render graph nodes. **All 5 backends**. Render graph executor has backend-specific paths: Vulkan (dynamic rendering + pipeline barriers), D3D12 (render pass + resource barriers), GL (FBO bind/unbind + `glMemoryBarrier`), WebGPU (render pass encoder + buffer/texture transitions). Minimal `IUiBridge` skeleton for viewport interaction.

**Merged from Phase 2**: Component 7 (TextRenderer) — GPU text rendering infrastructure (FreeType + HarfBuzz + MSDF + virtual glyph atlas + RichText). Runs as independent parallel track alongside the Render Graph core.

## Roadmap Digest

### Key Components (from roadmap table + deferred TextRenderer)

| # | Component | Core deliverable | Test target |
|---|-----------|-----------------|-------------|
| 1 | Render Graph | `RenderGraphBuilder`, `RenderGraphCompiler` (Kahn sort, barriers, aliasing), `RenderGraphExecutor` (per-backend), `RenderGraphCache` | ~15 |
| 2 | GBuffer | Albedo+Metallic (RGBA8), Normal+Roughness (RGBA16F), Depth (D32F), Motion (RG16F) | ~6 |
| 3 | PBR Deferred Lighting | Cook-Torrance BRDF, deferred compute pass, multiple lights | ~8 |
| 4 | IBL & Environment | HDRI→cubemap, pre-filtered specular, diffuse SH, BRDF LUT, BackgroundMode (ground plane deferred to Phase 3b) | ~10 |
| 5 | Tone Mapping | ACES filmic (+ Neutral/Reinhard enum placeholders), exposure control, final blit | ~6 |
| 6 | IUiBridge Skeleton | Viewport input interface, NullBridge, GlfwBridge, NekoBridge, 6-DOF | ~6 |
| 7 | 5-Backend Sync + Demo | `deferred_pbr_basic` on all 5 backends, golden image | ~7 |
| 8 | TextRenderer — Font Loading | FreeType font load + metrics + system font discovery | ~7 |
| 9 | TextRenderer — Shaping | HarfBuzz shaping + script detection + fallback chain + ShapingCache | ~9 |
| 10 | TextRenderer — MSDF | msdfgen wrapper + AsyncBatch + quality tuning | ~6 |
| 11 | TextRenderer — Atlas | GlyphAtlas virtual page + shelf packing + LRU + GPU upload | ~7 |
| 12 | TextRenderer — GPU Pipeline | Instanced quad + MSDF fragment shader | ~8 |
| 13 | TextRenderer — RichText | RichTextSpan layout + bitmap fallback crossfade [8,16]px | ~7 |
| 14 | TextRenderer — Outlines & Symbols | Glyph outline extraction (Bezier) + GD&T/ISO symbol atlas | ~6 |
| 15 | TextRenderer Demo | `text_demo` on all 5 backends + golden image CI | ~5 |

### Critical Technical Decisions

- **Render graph as backbone**: all GPU work from Phase 3b+ is a render graph node. No raw command buffer recording outside the graph (except debug/ImGui overlay/TextRenderer Phase 3a).
- **Deferred via compute**: Tier1/2 use compute shader for deferred resolve. Tier3/4 use fullscreen triangle fragment shader (no compute on WebGPU Tier3 for lighting resolve).
- **IBL integration model**: IBL terms (specular pre-filtered env lookup + BRDF LUT + diffuse irradiance SH) are computed **inside** the deferred resolve pass via `DeferredResolve::Execute(... EnvironmentRenderer const* env)`. Not a separate additive pass. `EnvironmentRenderer` is a data provider; skybox is the only independent graphics pass from the IBL component.
- **IBL split-sum**: pre-filtered specular (5 mip GGX) + diffuse irradiance SH L2 + BRDF LUT (512×512 RG16F). One-time compute.
- **BackgroundMode**: enum `{SolidColor, VerticalGradient, HDRI, Transparent}`. Zero-cost mode switch (skybox pass conditional).
- **Ground plane**: deferred to Phase 3b. Roadmap marks it "optional"; requires VSM shadow data from Phase 3b.
- **IUiBridge**: callback-only skeleton at Phase 3a gate. `OnInputEvent()` returns `bool` (consumed) for future Phase 9 propagation. `SetCamera(ICamera const*)` required for `ScreenToWorld()`/`WorldToScreen()`. Coroutine extension deferred to Phase 13.
- **ToneMappingMode enum**: `{AcesNarkowicz, Neutral, Reinhard}`. Phase 3a only implements ACES Narkowicz. Neutral/Reinhard are placeholder stubs (assert-fail in Phase 3a debug builds).
- **ExecuteFn lambda lifetime**: captured references/pointers must outlive `RenderGraphExecutor::Execute()` for the current frame. Builder owns the std::function but not the captured data. Same contract as UE5 RDG / Filament FrameGraph.
- **TextRenderer ↔ RenderGraph**: Phase 3a TextRenderer uses direct `ICommandBuffer` recording (post-tone-map UI overlay). `RegisterAsPass(RenderGraphBuilder&)` is declared as a stub (returns invalid handle). Phase 3b activates it for TAA reactive mask generation.
- **Pass culling (mandatory)**: `RenderGraphCompiler` performs reverse reachability BFS from external outputs (present target, readback). Unreachable passes are removed. Side-effect passes (`side_effect = true`) survive culling. Debug builds log culled pass names. This matches UE5 RDG / Frostbite FrameGraph behavior.
- **Per-backend barrier correctness**: RenderGraphExecutor has per-backend barrier tests (Vulkan, D3D12, GL, WebGPU) — not just Vulkan.
- **RenderGraphCache debug collision detection**: debug builds perform full structural comparison on hash hit; assert on mismatch.
- **AsyncMsdfBatch ↔ GlyphAtlas thread boundary**: SPSC lock-free ring buffer (256 entries). Producer = jthread, consumer = render thread `FlushUploads()`. No mutex on hot path.
- **TextRenderer MSDF**: 4-channel MSDF (msdfgen), 32×32 px/em, virtual atlas with LRU eviction. Bitmap fallback <8px with [8,16]px crossfade.
- **Parallel tracks**: Components 1–7 (render graph core) and Components 8–15 (TextRenderer) have no mutual dependency. Assignable to separate sub-teams.

### Performance Targets (from roadmap Part VII, if applicable to this phase)

| Metric | Target |
|--------|--------|
| Render graph compile (100 passes) | < 1ms CPU |
| Deferred resolve (49 spheres, 4 lights) | < 2ms GPU |
| IBL pre-filter (one-time) | < 50ms GPU |
| Tone mapping blit | < 0.1ms GPU |
| Text rendering (1000 glyphs) | < 0.1ms GPU |
| Text rendering (10K glyphs, CAE labels) | < 0.5ms GPU |

### Downstream Phase Expectations

| Phase | What it expects from this phase |
|-------|-------------------------------|
| Phase 3b | `RenderGraph` (add VSM/TAA/GTAO/VRS as nodes), `GBuffer` (depth+motion), `IPipelineFactory` extensions |
| Phase 4 | `RenderGraph` (transient aliasing), `IDevice` (5 backends), `Descriptor System` |
| Phase 5 | `RenderGraph`, `TextRenderer` (for debug labels) |
| Phase 7a1 | `RenderGraph` (edge rendering node), `GBuffer` |
| Phase 7b | `TextRenderer` (direct curve upgrade), `RichTextSpan` (PMI rendering) |
| Phase 9 | `TextRenderer`, `RichText` (for RichTextInput editor) |
| Phase 11 | `RenderGraph` (debug overlay nodes) |
| Phase 12 | `RenderGraph` (multi-view) |

## Phase Dependencies

| Dependency Phase | What it provides |
|-----------------|-----------------|
| Phase 1a (01-1a) | `IDevice`, `ICommandBuffer`, `Handle<Tag>`, `Format`, `StagingUploader`, `SlangCompiler`, `OrbitCamera` |
| Phase 1b (02-1b) | Compat/GL/WebGPU backends, `OffscreenTarget`, `ReadbackBuffer` |
| Phase 2 (03-2) | `GraphicsPipelineDesc`, `CreateGeometryPass`, `DescriptorSetLayout/PipelineLayout/DescriptorSet`, `ForwardPass`, `MaterialRegistry`, `StandardPBR`, `ImGuiBackend`, `ISwapchain`, `MeshData`, `UploadMesh`, `TimestampQuery`, `CopyTextureToBuffer` |

---

## Components & Tasks

> **Status legend**: `[x]` = Complete, `[~]` = Partial (RG wiring + CPU tests done, GPU dispatch stub), `[ ]` = Not Started
>
> **GPU dispatch gap (identified 2026-03-16)**: Tasks T3a.2.1, T3a.3.1, T3a.4.1, T3a.4.2, T3a.5.1 were marked Complete but only implemented render graph resource declaration (`Setup`/`AddToGraph`) and CPU-side tests. All `Execute()` functions are stubs that resolve RG handles without issuing GPU commands (no `BeginRendering`, no draw/dispatch calls). T3a.7.1 was intended as the GPU activation point but itself deferred dispatch to "Phase 3b". These tasks are now marked `[~]` (Partial) and will be completed via GPU dispatch activation steps.

### Component 1: Render Graph

> Declarative pass graph with automatic barrier insertion, resource lifetime management, and transient aliasing.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3a.1.1 | RenderGraphBuilder — pass/resource declaration API | — | H |
| [x] | T3a.1.1-fixup | RenderGraphBuilder & Types hardening (variant, usage flags, generation, Reset, error tests) | T3a.1.1 | M |
| [x] | T3a.1.2 | RenderGraphCompiler — Kahn sort, cycle detection, barrier insertion, transient aliasing | T3a.1.1-fixup | XH |
| [x] | T3a.1.2-fixup | RenderGraphCompiler hardening (RAR barrier, Compute access, generation validation, edge merge, name ownership, move semantics) | T3a.1.2 | M |
| [x] | T3a.1.3 | RenderGraphExecutor — backend-agnostic execution via ICommandBuffer polymorphism | T3a.1.2-fixup | XH |
| [x] | T3a.1.4 | RenderGraphCache — structural hash, skip recompilation for static scenes | T3a.1.2 | M |

### Component 2: GBuffer

> Multi-render-target geometry pass producing albedo, normal, depth, motion vectors.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3a.2.1 | GBuffer layout + geometry pass (MRT output, depth prepass option) | T3a.1.3 | H |

### Component 3: PBR Deferred Lighting

> Deferred lighting resolve — Cook-Torrance BRDF, multiple point/directional lights, compute or fullscreen-tri path.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3a.3.1 | Deferred PBR resolve (compute Tier1/2, fullscreen-tri Tier3/4, multiple lights) | T3a.2.1 | H |

### Component 4: IBL & Environment

> HDRI environment mapping: equirect→cubemap, pre-filtered specular, diffuse irradiance SH, BRDF LUT, skybox, BackgroundMode, ground plane.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3a.4.1 | EnvironmentMap — HDRI→cubemap conversion + pre-filtered specular mip chain (compute) | T3a.1.3 | H |
| [x] | T3a.4.2 | Diffuse irradiance SH L2 + BRDF LUT generation + skybox pass + BackgroundMode | T3a.4.1, T3a.3.1 | H |

### Component 5: Tone Mapping

> ACES filmic tone mapping + exposure control + final blit.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3a.5.1 | ACES filmic tone mapping + exposure + final blit pass | T3a.3.1 | M |

### Component 6: IUiBridge Skeleton

> Minimal viewport interaction interface. Callback-only. NullBridge + GlfwBridge + NekoBridge.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3a.6.1 | IUiBridge interface + NullBridge + GlfwBridge + NekoBridge (6-DOF input) | — | M |

### Component 7: 5-Backend Sync + Demo (Deferred) + Phase 2 Debt Cleanup

> `deferred_pbr_basic` demo on all 5 backends. Golden image parity.
> **Added**: Phase 2 rendering debt cleanup — migrate ForwardPass to RG, extend CameraUBO, add material texture slots, introduce sortKey.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T3a.7.1 | deferred_pbr_basic demo (49 PBR spheres, deferred lighting, orbit camera, ImGui) + CI | T3a.5.1, T3a.4.2, T3a.6.1 | M |
| [x] | T3a.7.2 | ForwardPass → RG-native pass: Setup/AddToGraph/Execute with BeginRendering/EndRendering inside Execute | T3a.1.3 | H |
| [x] | T3a.7.3 | CameraUBO extend: add view, proj, inverseViewProj, jitter (float2), resolution (float2). Update camera_ubo.slang + C++ struct. Needed by deferred resolve, TAA, screen-space effects | T3a.3.1 | M |
| [x] | T3a.7.4 | Per-material descriptor layout: add texture sampler bindings (albedoMap, normalMap, roughnessMetallicMap, aoMap) alongside UBO. Bind dummy white/flat textures when no map is present | T3a.3.1 | M |
| [x] | T3a.7.5 | Draw sorting with 64-bit sortKey (depth:16 | material:16 | pipeline:8 | ...). Move sort out of ForwardPass into shared DrawListBuilder utility. Used by both forward and deferred geometry passes | T3a.7.2 | L |
| [x] | T3a.7.6 | GPU dispatch activation — activate all stub Execute() in T3a.2.1/3.1/4.1/4.2/5.1/7.1. See implementation order below. | T3a.7.2 | XH |

#### T3a.7.6 Implementation Order

> Activates GPU dispatch for all `[~]` Partial tasks. Each sub-step is independently verifiable.
>
> | Sub-step | Task reopened | Deliverable | Verification |
> |----------|-------------|-------------|-------------|
> | **A** | T3a.5.1 Step 3 | ToneMapping fullscreen-tri: pipeline creation, HDR SRV bind, ACES shader, exposure push constant, draw(3,1) | Readback LDR texture → non-zero pixels from synthetic HDR input |
> | **B** | T3a.7.1 | Present blit: copy/blit LDR transient → swapchain image | Demo shows clear color through ToneMapping |
> | **C** | T3a.4.1 Step 4 | EnvironmentMap compute: equirect→cubemap dispatch + specular pre-filter mip chain dispatch | Readback cubemap face 0 → non-zero pixels |
> | **D** | T3a.4.2 Step 4 | BRDF LUT compute fill + SH L2 irradiance compute + Skybox draw (cubemap sample at infinity depth) | Demo shows skybox background |
> | **E** | T3a.2.1 Step 3 | GBuffer MRT geometry: BeginRendering with 3 color + depth attachments, bind GBuffer pipeline, draw 49 spheres | Readback albedo RT → non-zero pixels with material variation |
> | **F** | T3a.3.1 Step 3 | DeferredResolve compute: Cook-Torrance + IBL terms, light SSBO, GBuffer SRV bind, compute dispatch | Demo shows fully lit PBR spheres |
> | **G** | T3a.7.1 Step 2-3 | Wire activated passes into deferred_pbr_basic execute lambdas, replace stubs | Full deferred PBR demo renders on Vulkan |
>
> **Deps**: A→B (need ToneMapping before present makes sense), C→D (cubemap before SH/skybox), {B,E,F}→G (all passes before demo wiring). C/D can run in parallel with A/B.

### Component 8: TextRenderer — Font Loading (deferred from Phase 2 T2.7.1)

> FreeType font loading + metrics + system font discovery.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T3a.8.1 | FreeType font loading + metrics + system font discovery | — | M |

### Component 9: TextRenderer — Shaping (deferred from Phase 2 T2.7.2)

> HarfBuzz text shaping + script detection + font fallback chain + ShapingCache LRU.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T3a.9.1 | HarfBuzz text shaping + script detection + font fallback + ShapingCache | T3a.8.1 | H |

### Component 10: TextRenderer — MSDF Generation (deferred from Phase 2 T2.7.3)

> msdfgen wrapper + AsyncBatch background thread + quality tuning.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T3a.10.1 | MSDF glyph generation (msdfgen wrapper + AsyncBatch) | T3a.8.1 | M |

### Component 11: TextRenderer — Glyph Atlas (deferred from Phase 2 T2.7.4)

> Virtual page atlas with shelf packing, LRU eviction, GPU upload.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T3a.11.1 | GlyphAtlas (virtual page + shelf packing + LRU + GPU upload) | T3a.8.1, T3a.10.1 | H |

### Component 12: TextRenderer — GPU Pipeline (deferred from Phase 2 T2.7.5)

> Instanced quad rendering + MSDF fragment shader + screenPxRange clamp + frustum culling.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T3a.12.1 | GPU text rendering pipeline (instanced quad + MSDF fragment) | T3a.9.1, T3a.11.1 | H |

### Component 13: TextRenderer — RichText (deferred from Phase 2 T2.7.6)

> RichTextSpan layout engine + bitmap fallback with [8,16]px crossfade.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T3a.13.1 | RichTextSpan layout engine + bitmap fallback crossfade | T3a.12.1, T3a.9.1, T3a.11.1 | M |

### Component 14: TextRenderer — Outlines & Symbols (deferred from Phase 2 T2.7.7)

> Glyph outline extraction (Bezier paths) + GD&T/ISO special symbol atlas.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T3a.14.1 | Glyph outline extraction + special symbol atlas (GD&T/ISO) | T3a.8.1, T3a.11.1 | M |

### Component 15: TextRenderer Demo (deferred from Phase 2 T2.8.2)

> `text_demo` on all 5 backends + golden image CI.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [ ] | T3a.15.1 | text_demo (multi-script, MSDF vs bitmap, 5 backends) + golden image CI | T3a.13.1, T3a.14.1 | M |

---

## Demo Plan

### deferred_pbr_basic

- **Name**: `demos/deferred_pbr_basic/`
- **Shows**: 49 PBR spheres (7×7 metallic×roughness grid), deferred lighting, IBL environment, tone mapping, orbit camera, ImGui overlay
- **Requires Tasks**: T3a.1.1–T3a.1.4, T3a.2.1, T3a.3.1, T3a.4.1–T3a.4.2, T3a.5.1, T3a.6.1, T3a.7.1, **T3a.7.6** (GPU dispatch activation)
- **CLI**: `--backend vulkan|compat|d3d12|gl|webgpu`
- **Acceptance**:
  - [ ] renders 49 PBR spheres with deferred lighting on all 5 backends
  - [ ] IBL environment visible (skybox + reflections)
  - [ ] tone mapping applied (ACES filmic)
  - [ ] golden image parity: per-backend PSNR > 30 dB vs reference (Vulkan Tier1); RMSE < 0.02 normalized

### text_demo

- **Name**: `demos/text_demo/`
- **Shows**: ASCII + CJK + engineering symbols at multiple sizes, world-space billboarded labels, MSDF vs bitmap fallback
- **Requires Tasks**: T3a.8.1–T3a.14.1, T3a.15.1, T3a.6.1 (optional — for world-space label camera interaction via IUiBridge)
- **CLI**: `--backend vulkan|compat|d3d12|gl|webgpu`
- **Acceptance**:
  - [ ] renders multi-script text on all 5 backends
  - [ ] MSDF quality at 16-72px sizes
  - [ ] bitmap fallback at <8px, crossfade band [8,16]px
  - [ ] golden image parity: per-backend PSNR > 30 dB vs reference; RMSE < 0.02 normalized

## Test Summary

| Category    | Target | Actual | Notes |
|-------------|--------|--------|-------|
| Unit        | ~102   | 91     | RG Executor(17), RG Compiler(19), RG Builder(26), RG Cache(16), GBuffer(13), Deferred(8), IBL(11), ToneMap(6), IUiBridge(6), Font(7), Shaping(9), MSDF(6), Atlas(7), TextPipeline(8), RichText(7), Outline(6) |
| Integration | ~19    |        | deferred_pbr_basic E2E(7), text_demo E2E(5), render graph multi-backend(7) |
| Shader      | ~6     |        | GBuffer encode/decode, PBR BRDF energy, MSDF sampling, ACES tone map |
| **Total**   | ~127   |        | Roadmap target: ~65 (render graph) + 12 (builder fixup) + ~50 (text) = ~127 |

## Implementation Order (Layers)

> Two parallel tracks: Render Graph (RG) and TextRenderer (TR).

| Layer | Tasks (parallel within layer) | Depends on Layer |
|-------|-------------------------------|------------------|
| L0 | T3a.1.1 (RG Builder) [DONE], T3a.1.1-fixup (RG Builder hardening), T3a.6.1 (IUiBridge), T3a.8.1 (TR Font) | — (Phase 2) |
| L1 | T3a.1.2 (RG Compiler), T3a.9.1 (TR Shaping), T3a.10.1 (TR MSDF) | L0 (T3a.1.2 blocked on T3a.1.1-fixup) |
| L2 | T3a.1.3 (RG Executor), T3a.1.4 (RG Cache), T3a.11.1 (TR Atlas) | L1 |
| L3 | T3a.2.1 (GBuffer), T3a.4.1 (IBL EnvMap), T3a.12.1 (TR GPU Pipeline), **T3a.7.2 (ForwardPass → RG)** | L2 |
| L4 | T3a.3.1 (Deferred PBR), T3a.13.1 (TR RichText), T3a.14.1 (TR Outlines), **T3a.7.3 (CameraUBO extend)**, **T3a.7.4 (Material texture slots)** | L3 |
| L5 | T3a.4.2 (IBL Irradiance + Skybox), T3a.5.1 (Tone Map), **T3a.7.5 (DrawListBuilder sortKey)** | L4 |
| L6 | T3a.7.1 (deferred_pbr_basic demo), T3a.15.1 (text_demo) | L5 (RG), L4 (TR) |

**Critical path (RG track)**: L0 → L1 → L2 → L3 → L4 → L5 → L6 = **7 layers** (unchanged — new tasks slot into existing layers)
**Critical path (TR track)**: L0 → L1 → L2 → L3 → L4 → L6 = **6 layers** (finishes at L4, demo waits for L6)
**TR track completes at L4**, then only T3a.15.1 (text_demo) waits for L6 or can run independently.

**Parallel opportunity**: TR L0–L4 runs fully in parallel with RG L0–L4. A second developer can complete all TextRenderer tasks while the primary developer handles the render graph.

**Rationale for T3a.4.1 at L3**: `EnvironmentMap` depends on `RenderGraphExecutor` (T3a.1.3) for compute dispatch, not on `DeferredResolve` (T3a.3.1). Moving it from L5 to L3 shortens the critical path by 1 layer. `T3a.4.2` remains after `T3a.3.1` because it integrates IBL terms into the deferred resolve pass.

**New tasks rationale**: T3a.7.2–7.5 are Phase 2 rendering debt identified during architectural review. They slot into existing layers without extending the critical path: T3a.7.2 (ForwardPass→RG) only needs RG Executor (L2→L3); T3a.7.3/7.4 need deferred resolve API shape (L3→L4); T3a.7.5 depends on the refactored pass (L4→L5).

---

## Forward Design Notes

| Future Phase | What this phase prepares | Interface / extension point |
|-------------|------------------------|---------------------------|
| Phase 3b | Render graph node API for shadows/AO/AA | `RenderGraphBuilder::AddGraphicsPass()`, `AddComputePass()` |
| Phase 4 | Transient aliasing for resource management | `RenderGraphCompiler::GetTransientAliasing()` |
| Phase 5 | TextRenderer for debug/entity labels | `TextRenderer::Draw(TextDrawCmd)` |
| Phase 7b | Text quality upgrade (direct curve) | `TextRenderer` path selection, `GlyphAtlas` storage buffer |
| Phase 9 | RichTextInput editor | `RichTextLayout::Layout()`, `RichTextSpan` |
| Phase 12 | Multi-view render graph | `RenderGraphBuilder` per-view subgraph |
| Phase 13 | IUiBridge coroutine extension | `IUiBridge::NextEvent()` (placeholder, not implemented here) |

---

## Cross-Component Contracts

| Provider Task | File (Exposure) | Consumer Task | Agreed Signature |
|--------------|-----------------|---------------|------------------|
| T3a.1.1 | `RenderGraphBuilder.h` (**public** H) | T3a.1.2, T3a.2.1, T3a.3.1, T3a.4.1, T3a.5.1, Phase 3b+ | `AddGraphicsPass(name, setup, execute)`, `AddComputePass(name, setup, execute)`, `CreateTexture(desc)`, `CreateBuffer(desc)`, `Read(handle)`, `Write(handle)` |
| T3a.1.2 | `RenderGraphCompiler.h` (**public** H) | T3a.1.3, T3a.1.4, Phase 4 | `Compile(graph) -> CompiledGraph`, barrier info, lifetime data, transient aliasing |
| T3a.1.3 | `RenderGraphExecutor.h` (**public** H) | T3a.2.1, Phase 3b+ | `Execute(compiledGraph, device, cmdBuf)` |
| T3a.2.1 | `GBuffer.h` (**public** M) | T3a.3.1, T3a.4.2, Phase 3b | `GBufferLayout{albedoMetallic, normalRoughness, depth, motion}` |
| T3a.3.1 | `DeferredResolve.h` (**shared** M) | T3a.4.2, T3a.5.1 | `DeferredResolve::Execute(gbuffer, lights, EnvironmentRenderer const* env) -> HDR texture` |
| T3a.4.2 | `EnvironmentRenderer.h` (**public** M) | T3a.7.1, Phase 3b | `EnvironmentRenderer::RenderSkybox()`, IBL terms for deferred resolve |
| T3a.5.1 | `ToneMapping.h` (**shared** M) | T3a.7.1 | `ToneMapping::Apply(hdrInput, exposure) -> LDR output` |
| T3a.6.1 | `IUiBridge.h` (**public** H) | T3a.7.1, Phase 3b+, Phase 9, Phase 13 | `OnInputEvent(Event) -> bool`, `GetViewportRect()`, `ScreenToWorld()`, `SetCamera(ICamera const*)`, `OnContinuousInput()` |
| T3a.8.1 | `FontManager.h` (**public** H) | T3a.9.1, T3a.10.1, T3a.11.1, T3a.14.1, Phase 7b | `LoadFont() -> FontId`, `GetGlyphMetrics()`, system font discovery |
| T3a.9.1 | `TextShaper.h` (**public** H) | T3a.12.1, T3a.13.1 | `ShapeText() -> GlyphRun`, `SetFallbackChain()`, ASCII fast path |
| T3a.11.1 | `GlyphAtlas.h` (**shared** H) | T3a.12.1, T3a.13.1, T3a.14.1 | `GetGlyph() -> GlyphEntry`, `GetPageTexture()`, `FlushUploads()` |
| T3a.12.1 | `TextRenderer.h` (**public** H) | T3a.13.1, T3a.15.1, Phase 5, 7b, 9 | `Draw(TextDrawCmd)`, `Flush(cmdBuf, viewProj, screenSize)`, `RegisterAsPass(RenderGraphBuilder&) -> RGHandle` (stub Phase 3a) |
| T3a.13.1 | `RichText.h` (**public** M) | T3a.15.1, Phase 7b, 9 | `RichTextLayout::Layout(spans, maxWidth) -> LayoutResult` |

---

## Known Technical Debt (T3a.7.6 Code Review, 2026-03-17)

> Deferred fixes identified during GPU dispatch activation. Each has a `TODO(Phase X)` comment in the source code at the indicated location.

| # | Finding | Severity | Target Phase | Code Location |
|---|---------|----------|-------------|---------------|
| D1 | ~~`CreateTextureView` format fallback hardcoded `RGBA16_FLOAT`~~ | ~~🔴~~ | — | **FIXED**: now returns `InvalidArgument` if `format == Undefined` |
| D2 | `CompiledGraph` stores runtime mutable data (transient handles) — should be immutable compiler output | 🟡 | Phase 3b | `RenderGraphExecutor.cpp` → return `TransientResourceSet` |
| D3 | ~~Commented-out `DestroyAllocatedTransients` dead code~~ | ~~🟡~~ | — | **FIXED**: removed |
| D4 | `EnvironmentMap::CreatePreset` error path uses fragile manual chain-destroy — should use RAII guards | 🟡 | Phase 3b | `EnvironmentMap.cpp` |
| D5 | Interactive path calls `WaitIdle` every frame — disables frame pipelining | 🟡 | Phase 3b | `deferred_pbr_basic/main.cpp` → per-frame-in-flight transient pools |
| D6 | Cubemap detection uses `arrayLayers==6` heuristic — should use `TextureDesc.isCubemap` flag | 🟡 | Phase 3b | `VulkanDevice.cpp` |
| D7 | ~~`prevViewProj` repurposed as `inverseViewProj` in Phase 3a~~ | ~~🟡~~ | — | **FIXED**: T3a.7.3 added dedicated `inverseViewProj` field to CameraUBO (368B) |
| D8 | Light SSBO uses `CpuToGpu` persistent map instead of staging upload | 🟢 | Phase 3b | `DeferredResolve.cpp` |
| D9 | Per-frame `vkUpdateDescriptorSets` for transient textures | 🟢 | Phase 4 | `DeferredResolve.cpp` + `ToneMapping.cpp` → `VK_EXT_descriptor_buffer` |
| D10 | `RunOffscreen`/`RunInteractive` parameter count bloat | 🟢 | Phase 3b | `deferred_pbr_basic/main.cpp` → `FrameResources` struct |

## TextRenderer Deferral Decision (2026-03-17)

> **Decision**: Phase 3a TextRenderer track (Components 8-15, Tasks T3a.8.1–T3a.15.1) is **deferred**.
> RG track (Components 1-7) gates as complete. TextRenderer will be executed as a **parallel workstream**
> alongside Phase 3b, assignable to a second developer.
>
> **Rationale**:
> - Components 1-7 form a self-contained, fully tested render graph backbone (639 tests, 18 tasks)
> - TextRenderer has zero dependency on Phase 3b features (independent parallel track)
> - Phase 3b (VSM, TAA, GTAO, VRS, Bloom) has higher visual impact priority for the deferred pipeline
> - TextRenderer Components 8-15 remain in Phase 3a spec and can resume at any time
>
> **Impact**: Phase 3a will NOT be marked Complete until TextRenderer is done.
> However, Phase 3b may proceed — its only dependencies are on Components 1-7 (all ✅).

## Completion Summary

*(Filled on phase completion — Components 1-7 done, Components 8-15 deferred)*

- **Date**: — (partial: Components 1-7 done 2026-03-17)
- **Tests**: 639 pass / 643 total (4 pre-existing GL/WebGPU failures)
- **Known limitations**: TextRenderer track (Components 8-15) deferred to parallel workstream
- **Design decisions**: See Known Technical Debt table (D1-D10). D1/D3/D7 fixed. D2/D4/D5/D6 → Phase 3b. D8/D9 → Phase 3b/4. D10 → Phase 3b.
- **Next phase**: Phase 05-3b (Shadows, Post-Processing & Visual Regression)

### Locked API Surface (Components 1-7)

| File | Exposure | Key Exports | Consumed by Phase(s) |
|------|----------|-------------|---------------------|
| `RenderGraphBuilder.h` | **public** | `AddGraphicsPass`, `AddComputePass`, `CreateTexture`, `CreateBuffer`, `ImportTexture` | Phase 3b+, Phase 4, Phase 12 |
| `RenderGraphCompiler.h` | **public** | `Compile() → CompiledGraph`, `ComputeStructuralHash()` | Phase 3b+, Phase 4 |
| `RenderGraphExecutor.h` | **public** | `Execute(compiledGraph, device, cmdBuf)` | Phase 3b+ |
| `RenderGraphCache.h` | **public** | `TryGet()`, `Store()`, `Invalidate()` | Phase 3b+ |
| `GBuffer.h` | **public** | `GBufferLayout`, `GBufferPass::Setup/AddToGraph/Execute` | Phase 3b (VSM, TAA, GTAO) |
| `DeferredResolve.h` | shared | `DeferredResolve::Create/Execute`, `LightData` | Phase 3b |
| `EnvironmentRenderer.h` | **public** | `EnvironmentRenderer::Create`, `SetupSkyboxPass`, `EnvironmentMap` | Phase 3b |
| `ToneMapping.h` | shared | `ToneMapping::Create/Execute`, `ToneMappingMode` | Phase 3b (expanded modes) |
| `IUiBridge.h` | **public** | `OnInputEvent → bool`, `ScreenToWorld`, `SetCamera` | Phase 3b+, Phase 9, Phase 13 |
| `ForwardPass.h` | **public** | `ForwardPass::Setup/AddToGraph/Execute`, `DrawCall`, `DummyTextures` | Phase 3b |
| `CameraUBO.h` | **public** | `CameraUBO` (368B: viewProj, prevViewProj, view, proj, inverseViewProj, jitter, resolution) | Phase 3b (TAA, screen-space) |
| `DrawListBuilder.h` | **public** | `BuildSortKey`, `SortDrawList`, field extractors | Phase 3b+ |
