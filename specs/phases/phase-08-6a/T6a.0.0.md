# T6a.0.0 — Pre-Phase Gate Fix

**Phase**: 08-6a (GPU-Driven Rendering Core)
**Component**: -1 (Pre-Phase Gate Fix)
**Status**: In Progress (awaiting manual demo verification Steps 6-7)
**Current Step**: 6
**Effort**: H (estimated 4-6 hours)
**Resume Hint**: Steps 1-5, 8 complete. D1-D5 stale tests fixed, D6 AutoExposure move fixed, D7 GL MapBuffer fixed, D9-D11 compute pipelines implemented (D3D12 root sig + PSO, WebGPU shader module + pipeline), D8 GPU histogram wired (auto_exposure.slang + Compute() dispatch). Full test suite: 1363/1364 pass (1 pre-existing GL headless rendering failure). Steps 6-7 need manual demo run.

## Context

Phase 1a–5 completeness audit (2026-03-20) found 14 defects across 5 prior phases. These must ALL be resolved before any Phase 6a task starts. The defects range from stale test expectations (trivial) to real backend bugs (GL MapBuffer) and unverified demo acceptance criteria.

## Scope

| # | Defect | Phase | Severity | Category |
|---|--------|-------|----------|----------|
| D1 | `PipelineFactory.ShadowPassNotImplemented` test expects NotImplemented but shadow pass is now implemented | 3b | Stale test | Test fix |
| D2 | `PipelineFactory.AOPassNotImplemented` test expects NotImplemented but AO pass is now implemented | 3b | Stale test | Test fix |
| D3 | `PipelineFactory.AAPassNotImplemented` test expects NotImplemented but AA pass is now implemented | 3b | Stale test | Test fix |
| D4 | `MockDevice.CapabilityProfile` test hardcodes old DeviceFeature set (Phase 4 removed DescriptorBuffer from mock) | 1a/4 | Stale test | Test fix |
| D5 | `EnvironmentRenderer.BackgroundModeValues` test hardcodes old BackgroundMode enum count | 3a/3b | Stale test | Test fix |
| D6 | `AutoExposure.MoveSemantics` fails — move-from state not correctly cleared | 3b | Real bug | Code fix |
| D7 | OpenGL `ReadPixels`/`MapBuffer` returns GL error 5 (`GL_INVALID_VALUE`) — 3 tests fail | 1b | Real bug | Code fix |
| D8 | AutoExposure GPU histogram dispatch not wired (CPU fallback only, spec requires GPU compute) | 3b | Missing feature | Code fix |
| D9 | D3D12 compute pipeline `CreateComputePipeline` is stub (returns NotImplemented) | 1b | Stub | Code fix |
| D10 | OpenGL compute pipeline is stub | 1b | Stub | Code fix |
| D11 | WebGPU compute pipeline is stub | 1b | Stub | Code fix |
| D12 | Phase 3b `deferred_pbr` demo: 12 acceptance criteria unchecked (VSM visible, CSM visible, TAA active, etc.) | 3b | Unverified | Verification |
| D13 | Phase 5 `ecs_spatial` demo: 5 acceptance criteria unchecked | 5 | Unverified | Verification |
| D14 | Phase 5 `kernel_demo`: 4 acceptance criteria unchecked | 5 | Unverified | Verification |
| D15 | D3D12 `CreateGraphicsPipeline` returns NotImplemented — **D3D12 cannot render anything** | 1b | Real stub | Code fix |
| D16 | D3D12 `CreateSampler` returns NotImplemented | 1b | Real stub | Code fix |
| D17 | D3D12 `CreateTextureView` returns NotImplemented | 1b | Real stub | Code fix |
| D18 | D3D12 `CreateTimestampQueryPool` + `GetTimestampResults` NotImplemented | 1b | Stub | Code fix |
| D19 | D3D12 Swapchain Present/Resize are 9-line stubs | 1b | Real stub | Code fix |
| D20 | WebGPU `CreateGraphicsPipeline` returns NotImplemented — **WebGPU cannot render anything** | 1b | Real stub | Code fix |
| D21 | WebGPU `CreateTextureView` returns NotImplemented | 1b | Real stub | Code fix |
| D22 | OpenGL `CreateTextureView` returns NotImplemented | 1b | Real stub | Code fix |
| D23 | OpenGL `CreateTimestampQueryPool` + `GetTimestampResults` NotImplemented | 1b | Stub | Code fix |
| D24 | 5-backend tests silently skip failing backends via `continue` — **masks real failures** | 1b | Test design | Test fix |

## Steps

### Step 1: Fix stale test expectations (D1-D5)
[verify: test]

- D1: `tests/unit/test_pipeline_factory.cpp` — `ShadowPassNotImplemented`: change expectation from `ErrorCode::NotImplemented` to `has_value()` (shadow pass now returns valid pipeline handle via `CreateShadowPass`)
- D2: Same file — `AOPassNotImplemented`: change to expect `has_value()`
- D3: Same file — `AAPassNotImplemented`: change to expect `has_value()`
- D4: `tests/unit/test_mock_device.cpp` — `CapabilityProfile`: update expected DeviceFeature set to match current MockDevice capabilities (Phase 4 removed `DescriptorBuffer`)
- D5: `tests/unit/test_environment_renderer.cpp` — `BackgroundModeValues`: update expected enum count to include Phase 3b additions (GroundPlane, etc.)

**Acceptance**: All 5 tests pass. Zero regression on remaining 1354 tests.

### Step 2: Fix AutoExposure move semantics bug (D6)
[verify: test]

- `src/miki/rendergraph/AutoExposure.cpp`: review move constructor and move-assignment. Ensure source object's pipeline/descriptor handles are nulled after move (likely missing `std::exchange` on one field).
- `include/miki/rendergraph/AutoExposure.h`: verify all resource-owning members are in move constructor initializer list.

**Acceptance**: `AutoExposure.MoveSemantics` test passes.

### Step 3: Fix OpenGL MapBuffer bug (D7)
[verify: test]

- `src/miki/rhi/opengl/OpenGlDevice.cpp`: investigate `MapBuffer` implementation. Error 5 = `GL_INVALID_VALUE` — likely mapping with incorrect size, offset, or access flags. Check `glMapBufferRange` parameters.
- If GL context doesn't support `glMapBufferRange` (e.g., headless context limitation), fall back to `glGetBufferSubData` for readback.

**Acceptance**: `OffscreenGL.ReadPixelsReturnsData`, `OffscreenGLWebGPU.EndToEnd_GL_CreateAndReadback`, `Triangle5B.OffscreenReadback_GL` all pass.

### Step 4: Implement D3D12 graphics pipeline + sampler + core stubs (D9, D15-D19)
[verify: compile]

**D3D12 is currently unable to render anything** — `CreateGraphicsPipeline`, `CreateSampler`, `CreateComputePipeline` all return NotImplemented. Swapchain is a 9-line stub.

- `src/miki/rhi/d3d12/D3D12Device.cpp`:
  - D15: `CreateGraphicsPipeline` — create D3D12 graphics PSO (vertex+pixel from DXIL, input layout, rasterizer state, blend, depth-stencil)
  - D16: `CreateSampler` — `ID3D12Device::CreateSampler` into a descriptor heap slot
  - D9: `CreateComputePipeline` — create D3D12 compute PSO from DXIL blob
  - D17: `CreateTextureView` — `ID3D12Device::CreateShaderResourceView` with SRV desc for mip/slice subset
- `src/miki/rhi/d3d12/D3D12CommandBuffer.cpp`: verify `BindComputePipeline` + `Dispatch` are functional (not just declared)
- `src/miki/rhi/d3d12/D3D12Swapchain.cpp` (D19): implement `Present` (IDXGISwapChain::Present) and `Resize` (ResizeBuffers)

**Acceptance**: D3D12 `triangle_demo --backend d3d12 --offscreen --frames 1` produces non-black output. `CreateGraphicsPipeline` + `CreateSampler` return valid handles.

### Step 4b: Implement WebGPU graphics pipeline + compute pipeline stubs (D11, D20-D21)
[verify: compile]

**WebGPU is currently unable to render anything** — `CreateGraphicsPipeline` and `CreateComputePipeline` both return NotImplemented.

- `src/miki/rhi/webgpu/WebGpuDevice.cpp`:
  - D20: `CreateGraphicsPipeline` — `wgpuDeviceCreateRenderPipeline` from WGSL vertex+fragment
  - D11: `CreateComputePipeline` — `wgpuDeviceCreateComputePipeline` from WGSL compute
  - D21: `CreateTextureView` — `wgpuTextureCreateView` with mip/layer subset

**Acceptance**: WebGPU `triangle_demo --backend webgpu --offscreen --frames 1` produces non-black output.

### Step 4c: Fix OpenGL remaining stubs (D10, D22-D23)
[verify: compile]

OpenGL compute pipeline already works (`CreateComputePipeline` has real implementation). Fix remaining stubs:

- `src/miki/rhi/opengl/OpenGlDevice.cpp`:
  - D22: `CreateTextureView` — `glTextureView` (GL 4.3 ARB_texture_view) for mip/layer aliased view
  - D23: `CreateTimestampQueryPool` + `GetTimestampResults` — `glGenQueries` + `glGetQueryObjectui64v`
- D10 is **already implemented** (confirmed in second-round audit) — remove from defect list

**Acceptance**: GL `CreateTextureView` returns valid handle. Timestamp queries functional.

### Step 5: Wire AutoExposure GPU histogram (D8)
[verify: test]

- `src/miki/rendergraph/AutoExposure.cpp`: implement GPU dispatch path:
  - Pass 1: clear histogram buffer + per-pixel binning compute shader
  - Pass 2: single-workgroup reduction, write `targetEV_` to result buffer
  - Readback: map result buffer, read float, store to `targetEV_`
- If shader compilation fails at runtime (GL/WebGPU), gracefully fall back to CPU path (current behavior).
- Create or update `shaders/rendergraph/auto_exposure.slang` with compute entry points.

**Acceptance**: `AutoExposure` tests pass. GPU path produces non-zero EV value on Vulkan. CPU fallback still works on Mock/GL/WebGPU.

### Step 6: Verify Phase 3b demo acceptance (D12)
[verify: manual]

Run `deferred_pbr` demo interactively and verify each criterion:

1. CSM shadows visible (directional light, 4-cascade atlas)
2. TAA active on Vulkan (Halton jitter visible in ImGui stats)
3. FXAA active when TAA disabled (via ImGui toggle)
4. GTAO active (half-res AO buffer, bilateral upsample)
5. SSAO active when GTAO disabled
6. Bloom visible (6-level Gaussian chain)
7. All 6 tone mapping modes switchable via ImGui combo
8. Auto-exposure adapts to scene brightness (toggle in ImGui)
9. Vignette + chromatic aberration visible (ImGui sliders)
10. Ground shadow plane visible (BackgroundMode toggle)
11. Material editor works (row/col select, albedo/metallic/roughness adjust)
12. Per-frame-in-flight transient cleanup works (no VUID errors in validation layer)

Check off each in `specs/phases/phase-05-3b.md` demo acceptance section.

Note: VSM shadows (Tier1) are NOT expected to be visible — VsmShadowRender is data-structure-only in Phase 3b. CSM is the active shadow path.

**Acceptance**: All 12 criteria checked in phase spec. Screenshot captured.

### Step 7: Verify Phase 5 demo acceptance (D13, D14)
[verify: manual]

Run `ecs_spatial` demo (or `spatial_debug`) and verify:
1. 100K+ entities created and rendered (wireframe AABBs)
2. BVH frustum query returns correct visible set
3. Octree range query returns correct neighbor set
4. ImGui shows entity/archetype/query statistics
5. >= 60fps on development GPU

Run `kernel_demo` and verify:
1. SimKernel renders procedural shapes (cube, sphere, cylinder)
2. OcctKernel renders STEP file (when MIKI_KERNEL_OCCT=ON, or skip if OCCT not configured)
3. TopoGraph correctly populated with face/edge/vertex counts
4. triangleToFaceMap resolves pick→face correctly

Check off each in `specs/phases/phase-07-5.md` demo acceptance section.

**Acceptance**: All 9 criteria checked in phase spec.

### Step 8: Fix 5-backend test design (D24)
[verify: test]

The 5-backend integration tests (`test_triangle_5backend.cpp`) silently skip backends that fail to create a device or pipeline — this **masks real failures**. D3D12 and WebGPU have never actually rendered anything, but tests report "passed".

- `tests/integration/test_triangle_5backend.cpp`:
  - Change `if (!device) continue` to `if (!device) { GTEST_SKIP() << name << " device unavailable"; continue; }`
  - For backends that DO create a device but fail on `CreateGraphicsPipeline`: add explicit `EXPECT_TRUE(pipeline.has_value()) << name << " CreateGraphicsPipeline failed"`
  - `EndToEnd_5BackendRender`: require at least **3 backends** succeed (Vulkan + VulkanCompat + OpenGL), not just 1
  - Add per-backend result tracking and summary output

**Acceptance**: Test output explicitly shows which backends succeeded/skipped/failed. No silent `continue` past real failures.

### Step 9: Final verification
[verify: test]

- Run full test suite: `ctest --test-dir build/debug-vulkan --output-on-failure`
- Target: **0 failures** (down from current 10)
- Verify D3D12 triangle renders (if D3D12 runtime available on CI machine)
- Update `specs/phases/phase-05-3b.md` and `specs/phases/phase-07-5.md` acceptance checkboxes
- Update pipeline viz T0 if any new passes were implemented

**Acceptance**: `ctest` reports 0 failures. All prior-phase demo acceptance criteria checked off. No backend silently skipped in 5-backend tests.

## Files (estimated)

| File | Changes |
|------|---------|
| `tests/unit/test_pipeline_factory.cpp` | Update 3 test expectations (D1-D3) |
| `tests/unit/test_mock_device.cpp` | Update feature set expectation (D4) |
| `tests/unit/test_environment_renderer.cpp` | Update enum count (D5) |
| `src/miki/rendergraph/AutoExposure.cpp` | Fix move semantics (D6) + GPU histogram (D8) |
| `include/miki/rendergraph/AutoExposure.h` | Fix move member list (D6) |
| `src/miki/rhi/opengl/OpenGlDevice.cpp` | Fix MapBuffer (D7) + CreateTextureView (D22) + TimestampQuery (D23) |
| `src/miki/rhi/d3d12/D3D12Device.cpp` | CreateGraphicsPipeline (D15) + CreateSampler (D16) + CreateComputePipeline (D9) + CreateTextureView (D17) + TimestampQuery (D18) |
| `src/miki/rhi/d3d12/D3D12CommandBuffer.cpp` | Verify BindComputePipeline + Dispatch functional |
| `src/miki/rhi/d3d12/D3D12Swapchain.cpp` | Present + Resize real implementation (D19) |
| `src/miki/rhi/webgpu/WebGpuDevice.cpp` | CreateGraphicsPipeline (D20) + CreateComputePipeline (D11) + CreateTextureView (D21) |
| `src/miki/rhi/webgpu/WebGpuCommandBuffer.cpp` | Verify BindComputePipeline + Dispatch functional |
| `shaders/rendergraph/auto_exposure.slang` | NEW: GPU histogram compute shader (D8) |
| `tests/integration/test_triangle_5backend.cpp` | Fix silent skip design (D24) |
| `specs/phases/phase-05-3b.md` | Check off demo acceptance (D12) |
| `specs/phases/phase-07-5.md` | Check off demo acceptance (D13, D14) |
