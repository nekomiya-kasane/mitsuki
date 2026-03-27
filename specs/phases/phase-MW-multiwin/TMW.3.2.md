# TMW.3.2 — multi_window_basic Demo (2 windows, shared device, independent cameras)

**Phase**: MW (Multi-Window Infrastructure)
**Component**: 3 — Demo + Migration
**Dependencies**: TMW.2.2 (GLFW multi-window), TMW.3.1 (Demo migration)
**Status**: Not Started
**Effort**: M (3-4h)

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `demos/multi_window_basic/main.cpp` | demo | **M** | Full multi-window demo with independent cameras |
| `demos/multi_window_basic/CMakeLists.txt` | demo | L | Build target |

- **Error model**: Demo exits cleanly on any window close or device error.
- **Invariants**:
  - 2 windows created at startup (configurable via CLI: `--windows N`, default 2, max 4).
  - Each window has independent `OrbitCamera` (different initial view angles).
  - Both windows share one `IDevice` and one resource pool (textures, pipelines).
  - Each window runs full deferred pipeline: GBuffer → DeferredResolve → ToneMap → Present.
  - ImGui panel on primary window showing: per-window FPS, total GPU time, window count.
  - Close any window → that window destroyed, others continue. Close all → exit.
  - Window resize handled gracefully (per-surface `Configure()`).
  - Demo validates: shared device, independent camera, independent present, resize.

### Downstream Consumers

| Consumer | File | Reads |
|----------|------|-------|
| Phase 12 | `multi_window` demo | Extends this demo with 4 viewports + linked selection |
| Phase 15a | SDK examples | Pattern reference for multi-window integration |

### Upstream Contracts

| Source | Provides |
|--------|----------|
| TMW.2.1 | `MultiWindowManager::Create()`, `CreateWindow()`, `GetRenderSurface()`, `GetFrameManager()`, `GetActiveWindows()`, `PollEvents()`, `ShouldClose()` |
| TMW.2.2 | `GlfwWindowBackend` — GLFW-specific IWindowBackend |
| Phase 3a | `RenderGraphBuilder`, `RenderGraphCompiler`, `RenderGraphExecutor` |
| Phase 3b | `ToneMapping`, `DeferredResolve`, `GBufferPass`, `EnvironmentRenderer` |
| Phase 2 | `OrbitCamera`, `StagingUploader` |

### Technical Direction

- **Per-window render graph**: Each window builds its own render graph per frame.
  The render graph is built fresh each frame (no cross-window sharing of compiled graph).
  Resources (pipelines, textures, descriptor sets) are shared via the single `IDevice`.
  Per-window transient resources (GBuffer MRT, depth, tone-mapped output) are window-local.

- **Shared scene, different views**: All windows render the same scene (7×7 PBR spheres,
  same as deferred_pbr_basic). Each window has its own `OrbitCamera` with different initial
  angles (e.g., window 0 = perspective, window 1 = top-down).

- **Per-window ImGui**: ImGui context is per-window (each window has its own ImGui context).
  Primary window (window 0) shows the stats panel. Secondary windows show minimal overlay.
  Alternative: single ImGui context on primary window only (simpler, Phase 12 adds per-view).

- **Demo render loop** (core pattern):
  ```cpp
  while (!manager.ShouldClose()) {
      manager.PollEvents();
      for (auto handle : manager.GetActiveWindows()) {
          auto* fm = manager.GetFrameManager(handle);
          auto ctx = fm->BeginFrame();
          if (!ctx) continue;  // minimized or error

          auto cmd = device->CreateCommandBuffer();
          auto& camera = cameras[handle.id];
          camera.Update(ctx->width, ctx->height);

          // Build per-window render graph
          RenderGraphBuilder builder;
          // ... GBuffer, DeferredResolve, ToneMap, present blit ...
          auto compiled = compiler.Compile(builder.Build());
          executor.Execute(compiled, *cmd);

          fm->EndFrame(*cmd);
      }
  }
  ```

- **DemoControlServer**: Single MCP server shared across windows.
  Parameters: `window_count` (read-only), `active_window` (select which window's camera to control),
  `tone_map_mode`, `exposure`.

## Acceptance Criteria

| # | Criterion | Verification |
|---|-----------|--------------|
| 1 | Demo launches with 2 visible windows | Visual check |
| 2 | Each window renders the PBR sphere grid | Visual check |
| 3 | Cameras are independent (different angles) | Orbit one, other stays |
| 4 | Closing one window does not crash the other | Close secondary, primary continues |
| 5 | Closing all windows exits cleanly | Exit code 0 |
| 6 | Window resize works (no artifacts, no crash) | Resize secondary window |
| 7 | Shared device: both windows use same IDevice | Code inspection / debug log |
| 8 | `--windows 3` creates 3 windows | CLI test |
| 9 | Both build presets compile | CMake build |
| 10 | 8 integration tests pass | CTest |

### Tests (8 new — integration)

| # | Test Name | What It Verifies |
|---|-----------|-----------------|
| 1 | `MultiWindowBasic_TwoWindows` | Demo creates 2 windows (headless, --frames 2) |
| 2 | `MultiWindowBasic_SharedDevice` | Both windows reference same IDevice |
| 3 | `MultiWindowBasic_IndependentCamera` | Camera state differs between windows |
| 4 | `MultiWindowBasic_CloseOne` | Destroying one window, other continues rendering |
| 5 | `MultiWindowBasic_ResizeWindow` | Resize triggers RenderSurface::Configure, no crash |
| 6 | `MultiWindowBasic_ThreeWindows` | --windows 3 creates 3 surfaces |
| 7 | `MultiWindowBasic_PerWindowRenderGraph` | Each window has separate RG execution |
| 8 | `MultiWindowBasic_CleanExit` | All windows closed → exit code 0 |

## Implementation Steps

### Step 1: CMakeLists.txt

Create `demos/multi_window_basic/CMakeLists.txt`:
- Link miki::rhi, miki::rendergraph, miki::render, demo_framework
- Add to demos/CMakeLists.txt

### Step 2: Main structure

Create `demos/multi_window_basic/main.cpp`:
- Parse CLI: `--backend`, `--windows N`, `--frames N` (for headless testing)
- Create `IDevice` via `CreateOwned()`
- Create `GlfwWindowBackend`
- Create `MultiWindowManager`
- Create N windows via `manager.CreateWindow()`

### Step 3: Per-window camera setup

- `std::unordered_map<uint32_t, OrbitCamera> cameras;`
- Window 0: perspective (45° azimuth, 30° elevation)
- Window 1: top-down (0° azimuth, 89° elevation)
- Window 2+: side views

### Step 4: Shared rendering resources

Create shared resources (one-time):
- GBuffer pipeline, deferred resolve pipeline, tone mapping pipeline
- Environment map, BRDF LUT
- Scene geometry (PBR spheres)
- These are allocated on the shared IDevice — usable from any window's command buffer

### Step 5: Per-window transient resources

Each window needs its own:
- GBuffer MRT textures (sized to window dimensions)
- Depth texture
- Tone-mapped output texture
- Descriptor sets referencing per-window textures

Store in `std::unordered_map<uint32_t, WindowResources>`.
Recreate on resize (triggered by FrameManager ctx.width/height change).

### Step 6: Render loop

Implement core render loop per Technical Direction above.
Handle minimize (BeginFrame returns error → skip), resize (detect size change → recreate transients).

### Step 7: ImGui integration

Primary window: stats panel with per-window FPS counter.
Use `IUiBridge` (NullBridge for non-primary windows).

### Step 8: DemoControlServer

Register parameters: window_count, active_window, tone_map_mode, exposure.

### Step 9: Integration tests

Create `tests/integration/test_multi_window_basic.cpp`:
- 8 tests using headless mode (GLFW_VISIBLE=false, --frames N)
- Validate window creation, independent state, clean exit

### Step 10: Build + visual verification

- Both presets compile
- Manual visual test: 2 windows, orbit cameras, resize, close
