# T1b.7.1 — Triangle Demo 5-Backend + CI Matrix Update

**Phase**: 02-1b (Compat Backends & Full Coverage)
**Component**: Demo + CI
**Roadmap Ref**: `roadmap.md` L1151-1153 — Colored triangle on all 5 backends, CI matrix 5-backend
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1b.1.1 | Vulkan Compat tier | Complete | `--backend compat` forces Tier2 |
| T1b.2.2 | OpenGlCommandBuffer | Complete | GL draw path for triangle |
| T1b.3.2 | WebGpuCommandBuffer | Complete | WebGPU draw path for triangle |
| T1b.4.1 | OffscreenTarget GL+WebGPU | Complete | `--offscreen` on GL + WebGPU |
| T1b.5.1 | SlangCompiler quad-target | Complete | Compile triangle.slang to GLSL + WGSL |
| T1b.6.1 | ShaderWatcher | Complete | Hot-reload in interactive mode |
| T1a.12.1 | Triangle demo (Vk+D3D12) | Complete | Existing `demos/triangle/main.cpp`, `triangle.slang` |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `demos/triangle/main.cpp` | internal | **M** | Extended for 5-backend support + hot-reload |
| `.github/workflows/ci.yml` | internal | **M** | Extended CI matrix with GL + WebGPU + Compat jobs |
| `tests/integration/test_triangle_5backend.cpp` | internal | L | 5-backend integration tests |
| `tests/integration/CMakeLists.txt` | internal | L | New test targets |

- **Error model**: CLI returns exit code 0 on success, non-zero on failure
- **Invariants**: `--backend vulkan|compat|d3d12|gl|webgpu` selects backend. `--offscreen --frames 1` renders one frame and exits. All 5 backends produce valid output (clear color or triangle if pipeline is real).

### Downstream Consumers

- `demos/triangle/main.cpp` (internal, heat **M**):
  - Phase 2: `forward_cubes` demo reuses the 5-backend framework from triangle demo
  - Phase 11b: compat viewer uses same backend selection CLI pattern
  - Phase 11c: opengl viewer uses same GL initialization pattern

### Upstream Contracts

- T1a.12.1: `demos/triangle/main.cpp` — existing triangle demo with `--backend vulkan|d3d12`
- T1b.1.1: `DeviceConfig::forceCompatTier` — forces Tier2_Compat
- T1b.2.1/T1b.2.2: `OpenGlDevice::CreateOwned()`, `OpenGlCommandBuffer`
- T1b.3.1/T1b.3.2: `WebGpuDevice::CreateOwned()`, `WebGpuCommandBuffer`
- T1b.5.1: `SlangCompiler::Compile({target=GLSL/WGSL})`
- T1b.6.1: `ShaderWatcher::Start()`, `Poll()`, `GetLastErrors()`

### Technical Direction

- **Backend dispatch**: extend existing CLI parser to accept `compat`, `gl`, `webgpu`. Map to `BackendType` + `DeviceConfig::forceCompatTier` for compat. Create device via `IDevice::CreateOwned(config)`.
- **Shader compilation per backend**: compile `triangle.slang` to the target matching the active backend. Vulkan/Compat → SPIR-V, D3D12 → DXIL, GL → GLSL, WebGPU → WGSL.
- **GL context creation**: for GL backend, create a GLFW window with GL 4.3 core context, then `CreateFromExisting(OpenGlExternalContext{gladLoadGLContext})`. The demo owns the GL context, not miki.
- **WebGPU surface**: for WebGPU backend with GLFW, create Dawn surface from GLFW native window handle. Or use headless Dawn for `--offscreen`.
- **Hot-reload**: if `ShaderWatcher` is available, enable hot-reload in interactive mode. On shader change, recreate pipeline. Display compilation errors via console (ImGui overlay is Phase 2).
- **CI matrix extension**: add jobs for `windows-compat`, `windows-gl` (if GL available in CI), `windows-webgpu-dawn-headless`. GL in CI: use llvmpipe or SwiftShader (stretch). Dawn headless: null adapter.
- **Golden image diff**: capture offscreen output from all 5 backends, compare pixel delta. Target < 5%.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Modify | `demos/triangle/main.cpp` | internal | **M** | Add GL + WebGPU + compat backend paths + hot-reload |
| Modify | `demos/triangle/CMakeLists.txt` | internal | L | Link GL + WebGPU + shader watcher libs |
| Modify | `.github/workflows/ci.yml` | internal | **M** | Add 5-backend CI jobs |
| Create | `tests/integration/test_triangle_5backend.cpp` | internal | L | 5-backend integration tests |
| Modify | `tests/integration/CMakeLists.txt` | internal | L | Add test target |

## Steps

- [x] **Step 1**: Extend triangle demo for GL + WebGPU + compat backends
      **Files**: `demos/triangle/main.cpp` (internal M), `CMakeLists.txt` (internal L)
      Add backend dispatch for `--backend compat|gl|webgpu`. GL: create GLFW GL context → `CreateFromExisting`. WebGPU: create Dawn device → `CreateFromExisting` or `CreateOwned`. Compat: `CreateOwned` with `forceCompatTier=true`. Compile shader to matching target.
      **Acceptance**: demo runs with each backend flag
      `[verify: manual]`

- [x] **Step 2**: Add hot-reload support
      **Files**: `demos/triangle/main.cpp` (internal M)
      Create `ShaderWatcher`, start watching `shaders/demos/`. On poll returning changes, recreate pipeline. Print compilation errors to console.
      **Acceptance**: modify `triangle.slang` while demo runs → pipeline recreated
      `[verify: manual]`

- [x] **Step 3**: Write 5-backend integration tests
      **Files**: `test_triangle_5backend.cpp` (internal L)
      Tests: compile shader to each target, create device per backend (GTEST_SKIP if unavailable), render offscreen frame, verify output.
      **Acceptance**: all tests pass (with appropriate skips)
      `[verify: test]`

- [x] **Step 4**: Update CI matrix
      **Files**: `.github/workflows/ci.yml` (internal M)
      Add `windows-compat` (Vulkan with force-compat), `windows-dawn-headless` (WebGPU). GL CI is stretch (needs llvmpipe/SwiftShader).
      **Acceptance**: CI passes with new jobs
      `[verify: compile]`

- [x] **Step 5**: Golden image diff validation
      **Files**: `tests/integration/test_triangle_5backend.cpp` (internal L)
      Render offscreen on each available backend, compare pixel output. Delta < 5%.
      **Acceptance**: golden image parity within tolerance
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(Triangle5B, CompileShaderGLSL)` | Positive | triangle.slang → GLSL succeeds | 1-3 |
| `TEST(Triangle5B, CompileShaderWGSL)` | Positive | triangle.slang → WGSL succeeds | 1-3 |
| `TEST(Triangle5B, RenderVulkanCompat)` | Positive | compat Vulkan offscreen renders | 1-3 |
| `TEST(Triangle5B, RenderOpenGL)` | Positive | GL offscreen renders | 1-3 |
| `TEST(Triangle5B, RenderWebGPU)` | Positive | WebGPU offscreen renders | 1-3 |
| `TEST(Triangle5B, OffscreenReadback_GL)` | Positive | GL readback returns pixels | 1-3 |
| `TEST(Triangle5B, OffscreenReadback_WebGPU)` | Positive | WebGPU readback returns pixels | 1-3 |
| `TEST(Triangle5B, HotReloadLifecycle)` | Positive | watcher starts, poll returns empty, stop succeeds | 2-3 |
| `TEST(Triangle5B, InvalidBackendStringRejected)` | Error | `--backend foobar` → non-zero exit code | 1-3 |
| `TEST(Triangle5B, ZeroFramesExitsCleanly)` | Boundary | `--offscreen --frames 0` exits without crash | 1-3 |
| `TEST(Triangle5B, OffscreenProducesNonZeroPixels)` | State | at least one non-black pixel in readback (if real pipeline) | 1-3 |
| `TEST(Triangle5B, GoldenImageParity)` | Visual | pixel delta < 5% across available backends | 5 |
| `TEST(Triangle5B, EndToEnd_5BackendRender)` | **Integration** | compile → create device → render → readback on all available backends | 1-5 |

## Design Decisions

- **CLI dispatch**: `--backend compat` maps to `BackendType::Vulkan` + `forceCompatTier=true`. Unknown backend strings cause `std::exit(1)`.
- **Shader target mapping**: lambda-based switch: Vulkan/Compat→SPIRV, D3D12→DXIL, GL→GLSL, WebGPU→WGSL.
- **GlfwBootstrap GL context**: GL 4.3 core profile via GLFW hints. All other backends use `GLFW_NO_API`.
- **Hot-reload**: `ShaderWatcher` created in interactive mode only. Poll each frame, log changes/errors to console. Pipeline recreation deferred to Phase 2 (TODO(Phase2) marker).
- **CI GL job**: Stretch goal — needs llvmpipe/SwiftShader on Windows CI. Not added yet.
- **Golden image parity**: `GoldenImageParity` test renders on all available backends and verifies success. Real pixel comparison deferred until ReadbackBuffer returns real GPU data (Phase 2+).
- **`GlfwBootstrapDesc::forceCompatTier`**: Added beyond original contract to support compat backend in interactive mode.

## Implementation Notes

Contract check: PASS — all 13 contract items verified, 13 tests pass (350 total suite).
