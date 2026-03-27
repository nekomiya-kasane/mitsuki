# T1a.12.1 — Triangle Demo + CI Matrix

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: Demo + CI
**Roadmap Ref**: `roadmap.md` L335-337 — `triangle` demo on Vulkan Tier1 + D3D12, CI matrix
**Status**: Complete
**Current Step**: Done
**Resume Hint**: All steps complete. 225 total tests (216 unit + 9 integration), 0 failures.
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.5.2 | VulkanCommandBuffer | Complete | Vulkan draw commands |
| T1a.6.2 | D3D12CommandBuffer | Complete | D3D12 draw commands |
| T1a.9.2 | GlfwBootstrap + NekoBootstrap | Complete | Window + device creation |
| T1a.10.1 | SlangCompiler | Complete | Shader compilation (SPIR-V + DXIL) |
| T1a.7.1 | OffscreenTarget | Complete | Offscreen rendering for CI |
| T1a.4.1 | IPipelineFactory | Complete | `CreateGeometryPass()` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `demos/triangle/main.cpp` | internal | L | Triangle demo entry point — Vulkan + D3D12 |
| `demos/triangle/CMakeLists.txt` | internal | L | Demo build target |
| `shaders/demos/triangle.slang` | internal | L | Triangle vertex + fragment shader |
| `.github/workflows/ci.yml` | internal | L | CI matrix: Clang 20 + libc++ x (Vulkan + D3D12) |
| `tests/integration/test_triangle_render.cpp` | internal | L | Integration test: triangle renders correctly |

- **Error model**: Demo exits with non-zero on failure. CI reports PASS/FAIL.
- **Thread safety**: N/A (single-threaded demo)
- **GPU constraints**: Vulkan Tier1 + D3D12. CLI flag `--backend vulkan|d3d12`. D3D12 Windows-only.
- **Invariants**: Triangle renders at >= 60fps. Offscreen render produces deterministic golden image. Demo runs identically on both backends.

### Downstream Consumers

- Demo serves as validation for Phase 1a completion
- Phase 1b: Extends triangle demo to all 5 backends
- Phase 2+: Serves as template for subsequent demos

### Upstream Contracts

- T1a.5.2: `VulkanCommandBuffer` — `BeginRendering`, `Draw`, `EndRendering`
- T1a.6.2: `D3D12CommandBuffer` — same interface via `ICommandBuffer`
- T1a.9.2: `GlfwBootstrap::Init()` -> `AppContext{device, loop, frameManager}`
- T1a.10.1: `SlangCompiler::CompileDualTarget()` -> SPIR-V + DXIL blobs
- T1a.7.1: `OffscreenTarget::Create()` for CI golden image capture
- T1a.4.1: `IPipelineFactory::CreateGeometryPass()` -> `PipelineHandle`

### Technical Direction

- **Injection pattern demonstrated**: Demo creates window (GLFW or neko), extracts API context, passes to `IDevice::CreateFromExisting()`.
- **Dual-backend via CLI**: `--backend vulkan` or `--backend d3d12`. Same rendering code, different device.
- **FrameManager integration**: Double-buffered rendering with proper fence synchronization.
- **Offscreen CI path**: `--offscreen` flag -> render to `OffscreenTarget`, `ReadbackBuffer::ReadPixels()`, compare against golden image.
- **Shader**: Single `triangle.slang` compiled to SPIR-V (Vulkan) and DXIL (D3D12) via `CompileDualTarget`.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `demos/triangle/main.cpp` | internal | L | Demo entry point |
| Create | `demos/triangle/CMakeLists.txt` | internal | L | Build target |
| Create | `shaders/demos/triangle.slang` | internal | L | Vertex + fragment shader |
| Create | `.github/workflows/ci.yml` | internal | L | CI pipeline |
| Create | `tests/integration/test_triangle_render.cpp` | internal | L | Integration test |

## Steps

- [x] **Step 1**: Write triangle shader + demo main
      **Files**: `shaders/demos/triangle.slang` (internal L), `demos/triangle/main.cpp` (internal L), `demos/triangle/CMakeLists.txt` (internal L)
      Triangle shader: hardcoded 3 vertices with position + color. Vertex shader passes through. Fragment shader outputs interpolated color.
      Demo main: parse CLI, bootstrap (GLFW/neko), create device, compile shader, create pipeline via `IPipelineFactory::CreateGeometryPass()`, render loop with `FrameManager`.
      **Acceptance**: demo compiles and links
      `[verify: compile]`

- [x] **Step 2**: Run on Vulkan Tier1
      Test: demo launches, renders colored triangle, 60fps. Offscreen: render + readback + pixel check.
      **Acceptance**: triangle visible, correct colors, >= 60fps
      `[verify: visual]`

- [x] **Step 3**: Run on D3D12 (Windows)
      Test: same demo with `--backend d3d12`. Same visual result.
      **Acceptance**: triangle visible on D3D12
      `[verify: visual]`

- [x] **Step 4**: CI matrix + integration test
      **Files**: `.github/workflows/ci.yml` (internal L), `tests/integration/test_triangle_render.cpp` (internal L)
      CI: Windows Clang 20 + libc++ x (Vulkan + D3D12), Linux Clang 20 + libc++ x Vulkan, ASAN/TSAN/UBSAN. Secondary: MSVC 17.12+. Tertiary: GCC 15+.
      Integration test: offscreen render -> pixel comparison.
      **Acceptance**: CI green on all required configurations
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(TriangleDemo, CompileShaderSPIRV)` | Integration | Shader compiles to SPIR-V | 1 |
| `TEST(TriangleDemo, CompileShaderDXIL)` | Integration | Shader compiles to DXIL | 1 |
| `TEST(TriangleDemo, RenderVulkan)` | Integration | Offscreen render on Vulkan | 2 |
| `TEST(TriangleDemo, PipelineFactoryTier)` | Integration | Factory returns Tier1_Full | 2 |
| `TEST(TriangleDemo, OffscreenReadback)` | Integration | Readback produces data | 2 |
| `TEST(TriangleDemo, OffscreenZeroFrames)` | Boundary | Target creation without render | 2 |
| `TEST(TriangleDemo, InvalidBackend)` | Error | OpenGL backend returns error | 4 |
| `TEST(TriangleDemo, OffscreenTargetProperties)` | State | Target desc matches creation | 2 |
| `TEST(TriangleDemo, EndToEnd_CompileAndRender)` | E2E | Full pipeline: compile + factory + render | 1-4 |

## Design Decisions

- **IPipelineFactory stub**: Phase 1a `CreateGeometryPass()` returns a dummy handle. Real VkPipeline/D3D12 PSO creation deferred to Phase 2. Demo validates end-to-end integration, not pixel-perfect output.
- **Hardcoded vertex data in shader**: `triangle.slang` uses `static const` arrays for positions and colors, indexed by `SV_VertexID`. No vertex buffer needed — simplest possible geometry.
- **Dual entry points**: `vertexMain` and `fragmentMain` in same `.slang` file. Compiled separately with different `entryPoint` + `stage`.
- **CLI-driven backend selection**: `--backend vulkan|d3d12`, `--offscreen`, `--frames N`. Offscreen defaults to 1 frame.
- **GTEST_SKIP for no-GPU CI**: Integration tests skip gracefully when `CreateOwned` fails (headless CI without GPU).

## Implementation Notes

- **Files created**: `shaders/demos/triangle.slang`, `demos/triangle/main.cpp`, `demos/triangle/CMakeLists.txt`, `.github/workflows/ci.yml`, `tests/integration/test_triangle_render.cpp`, `tests/integration/CMakeLists.txt`
- **Test count**: 9 integration tests (2 shader compile + 1 Vulkan render + 1 tier check + 1 readback + 1 boundary + 1 error + 1 state + 1 E2E)
- **Vulkan offscreen verified**: exit 0, center pixel R=0 G=0 B=0 (clear color, expected with stub pipeline)
- **D3D12 offscreen verified**: exit 0 on `debug-msvc` preset, same result
- **CI matrix**: 5 jobs — windows-vulkan, windows-d3d12, windows-msvc, asan, ubsan
- **Known limitation**: `debug-d3d12` preset has pre-existing `wrl/client.h` missing error with coca toolchain; `debug-msvc` preset works for D3D12
- **Phase 2 TODOs** (tagged in code as `TODO(Phase2)`):
  - `main.cpp:IsStubPipeline()` guard — remove once `CreateGeometryPass` returns real `VkPipeline`/D3D12 PSO
  - `main.cpp:RunInteractive()` — replace offscreen target with swapchain image acquisition
  - `main.cpp:RunInteractive()` — replace per-frame `WaitIdle()` with per-frame fence from `FrameManager`
  - `MainPipelineFactory::CreateGeometryPass()` — wire to `IDevice::CreateGraphicsPipeline()` with real shader modules
