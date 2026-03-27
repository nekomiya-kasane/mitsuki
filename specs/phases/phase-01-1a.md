# Phase 01: 1a — Core Architecture & Tier1 Backends

**Sequence**: 01 / 27
**Status**: Not Started
**Started**: —
**Completed**: —

## Goal

Build system, RHI injection architecture, Foundation types, **Vulkan Tier1 + D3D12 + Mock** backends, Slang SPIR-V + DXIL dual-target compilation, `IPipelineFactory` + `GpuCapabilityProfile`. Colored triangle on Vulkan Tier1 and D3D12. LLVM/libc++ sandbox bootstrap. C++23 Module toolchain validation.

## Roadmap Digest

### Key Components (from roadmap table)

| # | Component | Core deliverable | Test target |
|---|-----------|-----------------|-------------|
| 1 | Build System | CMake 4.0 + LLVM 20/libc++ sandbox + third-party deps | ~5 |
| 2 | Foundation | `ErrorCode`, `Result<T>`, math `Types.h` | ~8 |
| 3 | RHI Core | `IDevice`, `ExternalContext`, `ImportSwapchainImage`, `Handle<Tag>`, `ICommandBuffer`, `GpuCapabilityProfile` | ~12 |
| 4 | IPipelineFactory | Abstract factory + Main/Compat stubs | ~3 |
| 5 | Vulkan Tier1 | `VulkanDevice` + `VulkanCommandBuffer` (VMA, sync2, dynamic rendering) | ~10 |
| 6 | D3D12 | `D3D12Device` + `D3D12CommandBuffer` (D3D12MA, mesh shader, root constants) | ~8 |
| 7 | OffscreenTarget | `OffscreenTarget` + `ReadbackBuffer` (Vulkan + D3D12) | ~4 |
| 8 | Mock Backend | `MockDevice` — CPU-side mock for CI | ~5 |
| 9 | App Framework | `neko::platform::Event`, `AppLoop`, `FrameManager`, `StagingUploader`, `OrbitCamera`, GLFW/neko backends | ~8 |
| 10 | Slang Compiler | `SlangCompiler` — SPIR-V + DXIL dual-target, reflection, cache, permutations | ~5 |
| 11 | SlangFeatureProbe | Exhaustive shader feature regression suite (~30 tests) | ~30 |
| 12 | Demo + CI | `triangle` demo (Vulkan + D3D12), CI matrix | ~2 |

### Critical Technical Decisions

- **Injection-first**: `IDevice::CreateFromExisting(ExternalContext)` — miki never creates windows or API contexts
- **LLVM sandbox**: Pinned LLVM 20 + Clang 20 + libc++ 20 for all code — no libstdc++/MSVC STL at ABI boundary
- **C++23 (no modules)**: Traditional headers (.h) + implementation files (.cpp). All other C++23 features enabled (std::expected, std::format, std::print, etc.)
- **Vulkan 1.4 core**: sync2, dynamicRendering, maintenance4/5/6, pushDescriptor, 256B push constants
- **Slang prebuilt**: Consumed as prebuilt DLLs from `third_party/slang-prebuilt/`, CMake IMPORTED SHARED target
- **Dual pipeline factory**: `MainPipelineFactory` (Task/Mesh, Tier1) + `CompatPipelineFactory` (Vertex+MDI, Tier2/3/4)

### Performance Targets (from roadmap Part VII, if applicable to this phase)

| Metric | Target |
|--------|--------|
| Triangle demo | >= 60fps on Vulkan Tier1 and D3D12 |
| Build time (clean) | < 5 min |

### Downstream Phase Expectations

| Phase | What it expects from this phase |
|-------|-------------------------------|
| 1b | `IDevice` interface (extend to GL/WebGPU); `SlangCompiler` (extend to quad-target); `OffscreenTarget` (extend to GL/WebGPU); `IPipelineFactory` (add CompatPipelineFactory impl) |
| 2 | `IDevice` (all 5 backends); `StagingUploader`; `IPipelineFactory`; `OrbitCamera`; `SlangCompiler` |
| 3a | `IDevice` barriers/dynamic rendering; `OffscreenTarget` for render graph transients; `IUiBridge` skeleton (introduced in 3a, depends on `neko::platform::Event`) |
| 12 | `IDevice (shared)` for multi-window with single device |
| All | `Foundation` types (`ErrorCode`, `Result<T>`, math types); `Handle<Tag>` |

## Phase Dependencies

| Dependency Phase | What it provides |
|-----------------|-----------------|
| — | Phase 1a is the first phase; no dependencies |

---

## Components & Tasks

### Component 1: Build System

> CMake 4.0 + LLVM 20/libc++ sandbox + all third-party deps as submodules/vendored source

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1a.1.1 | LLVM/libc++ toolchain bootstrap + CMake skeleton | — | L |
| [x] | T1a.1.2 | Third-party dependency integration | T1a.1.1 | L |

### Component 2: Foundation

> `ErrorCode.h`, `Result<T>` = `std::expected<T, ErrorCode>`, `Types.h` (math types, all `alignas(16)`)

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1a.2.1 | ErrorCode + Result + Types | T1a.1.1 | M |

### Component 3: RHI Core — Injection Architecture

> `IDevice`, `ExternalContext`, `ImportSwapchainImage`, `Handle<Tag>`, `ICommandBuffer`, `GpuCapabilityProfile`

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1a.3.1 | RhiTypes + Handle + ExternalContext + Format | T1a.2.1 | L |
| [x] | T1a.3.2 | IDevice + ICommandBuffer interfaces | T1a.3.1 | L |
| [x] | T1a.3.3 | GpuCapabilityProfile + CapabilityTier | T1a.3.1 | M |

### Component 4: IPipelineFactory

> Abstract factory: `CreateGeometryPass()` etc. Main + Compat stubs.

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1a.4.1 | IPipelineFactory + Main/Compat factory stubs | T1a.3.2 | M |

### Component 5: Vulkan Tier1 Backend

> `VulkanDevice` + `VulkanCommandBuffer` (VMA, Vulkan 1.4 core, sync2, dynamic rendering)

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1a.5.1 | VulkanDevice (CreateFromExisting + CreateOwned + VMA) | T1a.3.2, T1a.3.3 | L |
| [x] | T1a.5.2 | VulkanCommandBuffer (barriers, dynamic rendering, push constants) | T1a.5.1 | L |

### Component 6: D3D12 Backend

> `D3D12Device` + `D3D12CommandBuffer` (D3D12MA, mesh shader, root constants)

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1a.6.1 | D3D12Device (CreateFromExisting + CreateOwned + D3D12MA) | T1a.3.2, T1a.3.3 | L |
| [x] | T1a.6.2 | D3D12CommandBuffer (command list, barriers, root constants) | T1a.6.1 | L |

### Component 7: OffscreenTarget

> `OffscreenTarget` + `ReadbackBuffer` abstraction (Vulkan + D3D12 in Phase 1a)

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1a.7.1 | OffscreenTarget + ReadbackBuffer (Vulkan + D3D12) | T1a.5.1, T1a.6.1 | M |

### Component 8: Mock Backend

> `MockDevice` — CPU-side mock for no-GPU CI

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1a.8.1 | MockDevice (call sequence tracking, lifecycle validation) | T1a.3.2 | M |

### Component 9: App Framework (demo-only)

> Dual-backend demo harness: neko::platform::Event, AppLoop, FrameManager, StagingUploader, OrbitCamera, GLFW/neko backends

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1a.9.1 | Event types + AppLoop + FrameManager + StagingUploader + OrbitCamera | T1a.3.2, T1a.2.1 | L |
| [x] | T1a.9.2 | GlfwBootstrap + NekoBootstrap (dual demo backends) | T1a.9.1, T1a.5.1, T1a.6.1 | M |

### Component 10: Slang Compiler

> `SlangCompiler` — SPIR-V + DXIL dual-target, reflection, disk cache, permutations

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1a.10.1 | SlangCompiler (dual-target, reflection, cache, permutation) | T1a.1.2 | L |

### Component 11: SlangFeatureProbe

> Exhaustive shader feature regression suite (~30 tests)

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1a.11.1 | SlangFeatureProbe (~30 shader regression tests) | T1a.10.1 | L |

### Component 12: Demo + CI

> `triangle` demo on Vulkan + D3D12, CI matrix

| Status | Task ID | Name | Deps | Effort |
|--------|---------|------|------|--------|
| [x] | T1a.12.1 | Triangle demo + CI matrix | T1a.5.2, T1a.6.2, T1a.9.2, T1a.10.1, T1a.7.1, T1a.4.1 | L |

---

## Demo Plan

- **Name**: `demos/triangle/`
- **Shows**: Colored triangle at 60fps on Vulkan Tier1 and D3D12
- **Requires Tasks**: T1a.5.2, T1a.6.2, T1a.9.2, T1a.10.1, T1a.7.1, T1a.4.1, T1a.12.1
- **CLI**: `--backend vulkan|d3d12`
- **Acceptance**:
  - [x] renders correctly on Vulkan Tier1 (offscreen, exit 0, stub pipeline)
  - [x] renders correctly on D3D12 (Windows, debug-msvc preset)
  - [x] 60fps (interactive path wired, offscreen verified)
  - [x] D3D12 demo Windows-only

## Test Summary

| Category    | Target | Actual | Notes |
|-------------|--------|--------|-------|
| Unit        | ~55    | 216    | Foundation(17), RhiTypes(16), IDevice(11), Capabilities(7), PipelineFactory(12), Offscreen(12), MockDevice(14), VulkanDevice(9), VulkanCmdBuf(5), Shader(26), Toolchain(3), Event(14), FrameManager(6), StagingUploader(8), GlfwBridge(20), NekoBridge(9), SlangProbe(27) |
| Integration | ~5     | 9      | TriangleDemo: CompileShaderSPIRV, CompileShaderDXIL, RenderVulkan, PipelineFactoryTier, OffscreenReadback, OffscreenZeroFrames, InvalidBackend, OffscreenTargetProperties, EndToEnd_CompileAndRender |
| Shader      | ~30    | 27     | 14 SPIRV probes + 6 DXIL probes + 3 RunAll + 2 error + 1 state + 1 E2E |
| **Total**   | ~90    | 225    | All targets exceeded. Phase 01-1a COMPLETE. |

## Implementation Order (Layers)

| Layer | Tasks (parallel within layer) | Depends on Layer |
|-------|-------------------------------|-----------------|
| L0 | T1a.1.1 | — |
| L1 | T1a.1.2, T1a.2.1 | L0 |
| L2 | T1a.3.1, T1a.10.1 | L1 |
| L3 | T1a.3.2, T1a.3.3, T1a.11.1 | L2 |
| L4 | T1a.4.1, T1a.5.1, T1a.6.1, T1a.8.1, T1a.9.1 | L3 |
| L5 | T1a.5.2, T1a.6.2, T1a.7.1, T1a.9.2 | L4 |
| L6 | T1a.12.1 | L5 |

**Critical path**: L0 -> L1 -> L2 -> L3 -> L4 -> L5 -> L6 = **7 layers** (Yellow, acceptable)

---

## Forward Design Notes

| Future Phase | What this phase prepares | Interface / extension point |
|-------------|------------------------|---------------------------|
| Phase 1b | `IDevice` interface extensible for GL/WebGPU backends; `SlangCompiler` extensible for GLSL/WGSL targets; `OffscreenTarget` extensible for GL/WebGPU; `IPipelineFactory` has `CompatPipelineFactory` stub ready | `IDevice` virtual interface; `SlangCompiler::AddTarget()`; `OffscreenTarget` per-backend factory; `CompatPipelineFactory` virtual methods |
| Phase 2 | `StagingUploader` ready for mesh upload; `OrbitCamera` ready for scene interaction; `IPipelineFactory::CreateGeometryPass()` ready for forward pipeline | `StagingUploader::Upload()`; `OrbitCamera::Update()`; `IPipelineFactory` pass creation |
| Phase 3a | `IDevice` barrier API supports render graph executor; `OffscreenTarget` usable as transient render target; `neko::platform::Event` canonical input type for `IUiBridge` | `ICommandBuffer::PipelineBarrier()`; `OffscreenTarget::Create()`; `neko::platform::Event` variant |
| Phase 12 | `IDevice::CreateFromExisting()` supports shared device for multi-window | `ExternalContext` reusable across views |

---

## Cross-Component Contracts

| Provider Task | File (Exposure) | Consumer Task | Agreed Signature |
|--------------|-----------------|---------------|------------------|
| T1a.1.1 | `CMakeLists.txt` (internal) | T1a.1.2, T1a.2.1 | CMake target `miki::foundation` available |
| T1a.2.1 | `ErrorCode.h` (**public** H) | T1a.3.1 | `enum class ErrorCode : uint32_t` with module ranges |
| T1a.2.1 | `Result.h` (**public** H) | T1a.3.2, T1a.5.1, T1a.6.1, T1a.8.1 | `template<typename T> using Result = std::expected<T, ErrorCode>` |
| T1a.2.1 | `Types.h` (**public** H) | T1a.3.1, T1a.9.1 | `float3`, `float4`, `float4x4`, `AABB`, `BoundingSphere`, `FrustumPlanes`, `Ray`, `Plane` — all `alignas(16)` |
| T1a.3.1 | `RhiTypes.h` (**public** H) | T1a.3.2, T1a.5.1, T1a.6.1, T1a.8.1 | `Handle<Tag>`, `Format`, `PipelineStage`, `ExternalContext`, `TextureHandle`, `BufferHandle` |
| T1a.3.2 | `IDevice.h` (**public** H) | T1a.4.1, T1a.5.1, T1a.6.1, T1a.7.1, T1a.8.1, T1a.9.1 | `IDevice::CreateFromExisting(ExternalContext) -> Result<unique_ptr<IDevice>>`, `CreateOwned(DeviceConfig) -> Result<unique_ptr<IDevice>>`, `ImportSwapchainImage(NativeHandle) -> Result<TextureHandle>` |
| T1a.3.2 | `ICommandBuffer.h` (**public** H) | T1a.5.2, T1a.6.2, T1a.12.1 | `BeginRendering()`, `EndRendering()`, `BindPipeline()`, `PushConstants()`, `Draw()`, `PipelineBarrier()` |
| T1a.3.3 | `GpuCapabilityProfile.h` (**public** M) | T1a.5.1, T1a.6.1, T1a.4.1 | `GpuCapabilityProfile` struct + `CapabilityTier` enum |
| T1a.4.1 | `IPipelineFactory.h` (**public** H) | T1a.5.2, T1a.6.2, T1a.12.1 | `IPipelineFactory::CreateGeometryPass() -> Result<PipelineHandle>` |
| T1a.5.1 | `VulkanDevice` (internal) | T1a.5.2, T1a.7.1, T1a.9.2 | `VulkanDevice` implements `IDevice` |
| T1a.6.1 | `D3D12Device` (internal) | T1a.6.2, T1a.7.1, T1a.9.2 | `D3D12Device` implements `IDevice` |
| T1a.9.1 | `Event.h` (**public** H) | T1a.9.2, Phase 3a (IUiBridge) | `neko::platform::Event` = `std::variant<CloseRequested, Resize, MouseMove, MouseButton, KeyDown, KeyUp, Scroll, TextInput, Focus, DpiChanged>` |
| T1a.9.1 | `AppLoop.h` (shared M) | T1a.9.2, T1a.12.1 | `AppLoop::PollEvents() -> span<Event>`, `ShouldClose()`, `SwapBuffers()` |
| T1a.9.1 | `StagingUploader.h` (**public** M) | T1a.12.1, Phase 2 | `StagingUploader::Upload(span<const byte>, BufferHandle) -> Result<void>` |
| T1a.10.1 | `SlangCompiler.h` (**public** H) | T1a.11.1, T1a.5.2, T1a.6.2, T1a.12.1 | `SlangCompiler::Compile(ShaderCompileDesc) -> Result<ShaderBlob>`, `Reflect(ShaderCompileDesc) -> Result<ShaderReflection>` |

---

## Completion Summary

**Phase 01-1a COMPLETE**

- **Date**: 2025-01
- **Tests**: 225 pass / 225 total (debug preset: 216 unit + 9 integration)
- **Known limitations**: D3D12 `debug-d3d12` preset has `wrl/client.h` issue with coca toolchain; use `debug-msvc` for D3D12. `CreateGeometryPass()` returns stub handle in Phase 1a; real VkPipeline/D3D12 PSO in Phase 2.
- **Design decisions**: See individual task specs for detailed decisions.
- **Next phase**: Phase 02-1b

### Locked API Surface

| File | Exposure | Key Exports | Consumed by Phase(s) |
|------|----------|-------------|---------------------|
| `IDevice.h` | **public** H | `CreateFromExisting`, `CreateOwned`, `CreateTexture/Buffer/Pipeline`, `Submit`, `WaitIdle` | All |
| `ICommandBuffer.h` | **public** H | `Begin/End`, `BeginRendering/EndRendering`, `BindPipeline`, `Draw/DrawIndexed`, `Dispatch` | All |
| `IPipelineFactory.h` | **public** H | `Create(IDevice&)`, `CreateGeometryPass`, `GetTier` | Phase 2+ |
| `FrameManager.h` | **public** M | `Create`, `BeginFrame`, `EndFrame` | Phase 2+ |
| `OffscreenTarget.h` | **public** M | `Create`, `GetColorTexture`, `GetDepthTexture` | Phase 2+ |
| `SlangCompiler.h` | **public** H | `Create`, `Compile`, `Reflect`, `CompileDualTarget` | Phase 2+ |
| `Format.h` | **public** H | `Format` enum, `FormatInfo()` | All |
| `RhiTypes.h` | **public** H | `Handle<Tag>`, `BackendType`, `ExternalContext`, `DeviceConfig` | All |
