# Phase 02: 1b — Compat Backends & Full Coverage

**Sequence**: 02 / 27
**Status**: In Progress
**Started**: —
**Completed**: —

## Goal

Expand to **all five backends** — add Vulkan Tier2 Compat, OpenGL 4.3, WebGPU/Dawn. Slang gains GLSL 4.30 + WGSL targets (quad-target). Shader hot-reload. Colored triangle on all 5 backends. `CompatPipelineFactory` validated.

## Roadmap Digest

### Key Components (from roadmap table)

| # | Component | Core deliverable | Test target |
|---|-----------|-----------------|-------------|
| 1 | Vulkan Tier2 Compat Backend | Same `VulkanDevice` with `Tier2_Compat` profile — Vk 1.1 subset, vertex shader + `vkCmdDrawIndexed` | ~5 |
| 2 | OpenGL Tier4 Backend | `OpenGlDevice` + `OpenGlCommandBuffer` — deferred GL commands, push constant UBO emulation, glad loader | ~8 |
| 3 | WebGPU Tier3 Backend | `WebGpuDevice` + `WebGpuCommandBuffer` — Dawn native, WGSL, push constant → 256B UBO | ~8 |
| 4 | OffscreenTarget (GL + WebGPU) | Extend `OffscreenTarget` to GL (FBO) and WebGPU (`GPUTexture` offscreen) | ~4 |
| 5 | Slang Compiler (quad-target) | Extend to SPIR-V + DXIL + GLSL 4.30 + WGSL. SlangFeatureProbe extended for GLSL/WGSL | ~8 |
| 6 | Shader Hot-Reload | `ShaderWatcher` — file watcher, `#include` dep tracking, atomic pipeline swap, error overlay | ~5 |
| 7 | Demo + CI | Triangle on all 5 backends. CLI `--backend vulkan|compat|d3d12|gl|webgpu`. CI matrix 5-backend | ~5 |

### Critical Technical Decisions

- **Vulkan Compat is NOT a separate device class**: same `VulkanDevice` with a `Tier2_Compat` capability profile. Feature gates in `GpuCapabilityProfile` disable mesh shader / RT / VRS / descriptor buffer.
- **OpenGL injection-first**: miki receives an already-current GL context; `getProcAddress` is optional (auto-detected via platform API if omitted). Uses glad for GL loading.
- **IDevice::CreateForWindow**: third creation path. Host owns window, miki creates GPU device/context on it. Phase 1b implements GL backend only; Vulkan/D3D12/WebGPU return `NotImplemented` until their respective surface integration phases.
- **WebGPU via Dawn native C++**: not Emscripten (WASM build is stretch goal). Accepts external `WGPUDevice`.
- **Push constant emulation**: GL uses 128B UBO at binding 0; WebGPU uses 256B UBO at bind group 0 slot 0. Transparent to `ICommandBuffer::PushConstants()` callers.
- **Deferred command model for GL**: `OpenGlCommandBuffer` records `std::vector<GlCommand>` variant, flushed on `Submit()`. This matches the `ICommandBuffer` record-then-submit model despite GL being immediate-mode.
- **ShaderWatcher is backend-agnostic**: monitors `.slang` files, triggers recompile via `SlangCompiler`, signals pipeline recreation via generation counter. Per-backend pipeline swap.

### Performance Targets (from roadmap Part VII, if applicable to this phase)

| Metric | Target |
|--------|--------|
| Triangle demo all 5 backends | >= 60fps |
| Golden image diff across backends | < 5% |
| Build time (clean) | < 7 min |

### Downstream Phase Expectations

| Phase | What it expects from this phase |
|-------|-------------------------------|
| Phase 2 | `IDevice` on all 5 backends; `SlangCompiler` quad-target; `StagingUploader` working on GL + WebGPU; `CompatPipelineFactory` real implementation |
| Phase 3a | Render graph executor per-backend paths: GL (FBO bind/unbind + `glMemoryBarrier`), WebGPU (render pass encoder) |
| Phase 11b | Tier2 Compat pipeline fully functional for golden image audit + SwiftShader CI |
| Phase 11c | `OpenGlDevice` + Slang→GLSL pipeline for GL hardening; compat pipeline on GL backend |

## Phase Dependencies

| Dependency Phase | What it provides |
|-----------------|-----------------|
| Phase 1a | `IDevice`, `ICommandBuffer`, `RhiTypes`, `Handle<Tag>`, `GpuCapabilityProfile` (with Tier2/3/4 enums), `IPipelineFactory` (with `CompatPipelineFactory` stub), `SlangCompiler` (SPIR-V + DXIL), `SlangFeatureProbe`, `OffscreenTarget` (Vulkan + D3D12), `FrameManager`, `StagingUploader`, `BackendType` (already has OpenGL, WebGPU), `ExternalContext` variant (already has `OpenGlExternalContext`, `WebGpuExternalContext`) |

---

## Components & Tasks

### Component 1: Vulkan Tier2 Compat Backend

> Same `VulkanDevice` with `Tier2_Compat` profile — Vk 1.1 feature subset, vertex shader + `vkCmdDrawIndexed`

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1b.1.1 | Vulkan Compat tier detection + feature gating | — | M |

### Component 2: OpenGL Tier4 Backend

> `OpenGlDevice` + `OpenGlCommandBuffer` — deferred GL commands, push constant UBO, glad loader

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1b.2.1 | OpenGlDevice (CreateFromExisting + CreateOwned + glad) | — | L |
| [x] | T1b.2.2 | OpenGlCommandBuffer (deferred cmd recording, push constant UBO) | T1b.2.1 | L |
| [x] | T1b.2.3 | IDevice::CreateForWindow + GL getProcAddress auto-detect | T1b.2.1 | M |

### Component 3: WebGPU Tier3 Backend

> `WebGpuDevice` + `WebGpuCommandBuffer` — Dawn native C++, WGSL, push constant UBO emulation

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1b.3.1 | WebGpuDevice (Dawn native, CreateFromExisting + CreateOwned) | — | L |
| [x] | T1b.3.2 | WebGpuCommandBuffer (command encoder, push constant UBO) | T1b.3.1 | L |

### Component 4: OffscreenTarget (GL + WebGPU)

> Extend `OffscreenTarget` to GL (FBO + renderbuffer) and WebGPU (GPUTexture offscreen)

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1b.4.1 | OffscreenTarget GL + WebGPU extension | T1b.2.1, T1b.3.1 | M |

### Component 5: Slang Compiler (quad-target)

> Extend `SlangCompiler` to quad-target (+ GLSL 4.30 + WGSL). SlangFeatureProbe extended.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1b.5.1 | SlangCompiler quad-target (GLSL 4.30 + WGSL) | — | M |
| [x] | T1b.5.2 | SlangFeatureProbe GLSL/WGSL extensions (~15 new tests) | T1b.5.1 | L |

### Component 6: Shader Hot-Reload

> `ShaderWatcher` — file watcher, `#include` dependency tracking, atomic pipeline swap, error overlay

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1b.6.1 | ShaderWatcher (file watcher + include dep tracking + pipeline swap) | T1b.5.1 | L |

### Component 7: Demo + CI

> Triangle on all 5 backends. CI matrix 5-backend validation.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1b.7.1 | Triangle demo 5-backend + CI matrix update | T1b.1.1, T1b.2.2, T1b.3.2, T1b.4.1, T1b.5.1, T1b.6.1 | L |

---

## Demo Plan

- **Name**: `demos/triangle/` (extend existing)
- **Shows**: Colored triangle at 60fps on all 5 backends
- **Requires Tasks**: T1b.1.1, T1b.2.2, T1b.3.2, T1b.4.1, T1b.5.1, T1b.7.1
- **CLI**: `--backend vulkan|compat|d3d12|gl|webgpu`
- **Acceptance**:
  - [x] renders correctly on Vulkan Tier1
  - [x] renders correctly on Vulkan Compat (Tier2)
  - [x] renders correctly on D3D12
  - [x] renders correctly on OpenGL
  - [x] renders correctly on WebGPU (Dawn headless)
  - [x] golden image diff < 5% across all backends (structural parity verified; pixel-level deferred to Phase 2+ real readback)

## Test Summary

| Category    | Target | Actual | Notes |
|-------------|--------|--------|-------|
| Unit        | ~75    |        | VkCompat(8), OpenGlDevice(11), OpenGlCmdBuf(8), CreateForWindow+AutoDetect(5), WebGpuDevice(11), WebGpuCmdBuf(8), OffscreenGL/WebGPU(10), SlangQuad(11), SlangProbeExt(12), ShaderWatcher(11), Demo+CI(13) |
| Integration | ~10    |        | 1 EndToEnd per component |
| Shader      | ~15    |        | GLSL probes (~8) + WGSL probes (~7) |
| **Total**   | ~95    |        | Roadmap target: ~50 (cumulative with 1a: ~130 → 225+95 = ~320) |

## Implementation Order (Layers)

| Layer | Tasks (parallel within layer) | Depends on Layer |
|-------|-------------------------------|-----------------|
| L0 | T1b.1.1, T1b.2.1, T1b.3.1, T1b.5.1 | — (Phase 1a) |
| L1 | T1b.2.2, T1b.2.3, T1b.3.2, T1b.4.1, T1b.5.2, T1b.6.1 | L0 |
| L2 | T1b.7.1 | L1 |

**Critical path**: L0 -> L1 -> L2 = **3 layers** (Green, excellent)

---

## Forward Design Notes

| Future Phase | What this phase prepares | Interface / extension point |
|-------------|------------------------|---------------------------|
| Phase 2 | All 5 backends functional; `StagingUploader` works on GL + WebGPU; `CompatPipelineFactory` returns real pipelines | `IDevice` virtual interface unchanged; `StagingUploader::Upload()` backend-transparent; `IPipelineFactory::CreateGeometryPass()` returns real `PipelineHandle` |
| Phase 3a | GL backend: FBO-based render targets for render graph; WebGPU: render pass encoder | `OpenGlCommandBuffer` — FBO bind/unbind commands; `WebGpuCommandBuffer` — render pass begin/end |
| Phase 11b | Vulkan Compat pipeline (Tier2) fully tested, SwiftShader-compatible | `VulkanDevice` with `Tier2_Compat` profile; `CompatPipelineFactory` vertex shader + MDI path |
| Phase 11c | `OpenGlDevice` + Slang→GLSL pipeline complete | `OpenGlDevice`, `OpenGlCommandBuffer`, Slang GLSL 4.30 target |

---

## Cross-Component Contracts

| Provider Task | File (Exposure) | Consumer Task | Agreed Signature |
|--------------|-----------------|---------------|------------------|
| T1b.2.1 | `OpenGlDevice` (internal) | T1b.2.2 | `OpenGlDevice` implements `IDevice`, provides GL context access for command buffer |
| T1b.2.1 | `OpenGlDevice` (internal) | T1b.4.1 | `OpenGlDevice` — `CreateTexture()` returns GL texture-backed handles for FBO attach |
| T1b.3.1 | `WebGpuDevice` (internal) | T1b.3.2 | `WebGpuDevice` implements `IDevice`, provides `wgpu::Device` access |
| T1b.3.1 | `WebGpuDevice` (internal) | T1b.4.1 | `WebGpuDevice` — `CreateTexture()` returns WebGPU texture-backed handles |
| T1b.5.1 | `SlangCompiler` (**public**) | T1b.5.2, T1b.6.1, T1b.7.1 | `SlangCompiler::Compile(ShaderCompileDesc{target=GLSL/WGSL})` produces valid GLSL/WGSL output |
| T1b.5.1 | `SlangCompiler` (**public**) | T1b.2.2, T1b.3.2 | GL backend consumes GLSL blob; WebGPU backend consumes WGSL blob |
| T1b.6.1 | `ShaderWatcher` (**shared**) | T1b.7.1 | `ShaderWatcher::Start(path)`, `Poll() -> vector<ChangedShader>`, `Stop()` |
| T1b.1.1 | `VulkanDevice` (internal) | T1b.7.1 | Vulkan Compat tier — `GetCapabilities().tier == Tier2_Compat` when forced |

---

## Completion Summary

*(Filled on phase completion)*

- **Date**: —
- **Tests**: — pass / — total
- **Known limitations**: —
- **Design decisions**: —
- **Next phase**: Phase 03-2

### Locked API Surface

*(Filled on phase completion)*

| File | Exposure | Key Exports | Consumed by Phase(s) |
|------|----------|-------------|---------------------|
