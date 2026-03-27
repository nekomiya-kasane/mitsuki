# T3a.6.1 — IUiBridge Skeleton + NullBridge + GlfwBridge + NekoBridge

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 6 — IUiBridge Skeleton
**Roadmap Ref**: `roadmap.md` L1394 — IUiBridge Skeleton
**Status**: Complete
**Current Step**: —
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| — | (Phase 1a/1b) | Complete | `IDevice`, `OrbitCamera`, `neko::platform::Event` |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/platform/IUiBridge.h` | **public** | **H** | `IUiBridge` — viewport interaction interface |
| `include/miki/platform/ContinuousInputState.h` | **public** | **M** | `ContinuousInputState` — 6-DOF velocity input |
| `src/miki/platform/NullBridge.h` | internal | L | Headless no-op implementation |
| `src/miki/platform/NullBridge.cpp` | internal | L | — |
| `demos/framework/glfw/GlfwBridge.h` | shared | **M** | GLFW callback → Event bridge |
| `demos/framework/glfw/GlfwBridge.cpp` | shared | L | — |
| `demos/framework/neko/NekoBridge.h` | shared | **M** | neko EventLoop → Event bridge |
| `demos/framework/neko/NekoBridge.cpp` | shared | L | — |
| `tests/unit/test_uibridge.cpp` | internal | L | Unit tests |

- **Canonical event type**: `IUiBridge::OnInputEvent(neko::platform::Event)` — uses neko's `Event` variant directly
- **No miki-specific InputEvent**: neko's definition is canonical
- **Callback-only**: coroutine extensions (`NextEvent()`, `ExecuteOpAsync()`) deferred to Phase 13
- **6-DOF**: `ContinuousInputState` consumed by `OrbitCamera` via velocity × deltaTime

### Downstream Consumers

- `IUiBridge.h` (**public**, heat **H**):
  - T3a.7.1 (Demo) — deferred_pbr_basic receives input via bridge
  - Phase 3b+: all demos use IUiBridge, not raw GLFW/neko callbacks
  - Phase 9: interactive tools receive input via bridge
  - Phase 13: adds coroutine `NextEvent()` extension
- `GlfwBridge.h` (**shared**, heat **M**):
  - T3a.7.1, all GLFW-based demos
- `NekoBridge.h` (**shared**, heat **M**):
  - All neko-based demos

### Upstream Contracts
- Phase 1a: `OrbitCamera` — consumes `OnInputEvent` for camera control
- neko platform: `neko::platform::Event` (`std::variant<CloseRequested, Resize, MouseMove, ...>`)

### Technical Direction
- **Minimal interface**: `GetViewportRect()`, `ScreenToWorld()`, `WorldToScreen()`, `OnResize()`, `OnInputEvent()`, `OnContinuousInput()`, `OnFocusChange()`
- **Camera dependency**: `ScreenToWorld()` and `WorldToScreen()` require the current view-projection matrix. Bridge implementations must hold a `ICamera const*` reference (set via `SetCamera(ICamera const*)`). If no camera is set, `ScreenToWorld()` returns a zero ray and `WorldToScreen()` returns `{0,0}`. `ICamera` is the existing `OrbitCamera` interface from Phase 1a.
- **Event consumption**: `OnInputEvent()` returns `bool` (true = event consumed, false = propagate). This enables future Phase 9 event propagation control without API break.
- **GlfwBridge**: wraps GLFW callbacks → `neko::platform::Event` → `OnInputEvent`. Lives in `demos/framework/glfw/`
- **NekoBridge**: wraps `neko::platform::EventLoop` poll → `OnInputEvent`. Near 1:1 mapping.
- **NullBridge**: headless, all methods no-op. For tests and offscreen rendering.
- **Scene Model Queries deferred**: `GetAssemblyTree`, `GetEntityAttributes` etc. deferred to Phase 8+

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/platform/IUiBridge.h` | **public** | **H** | Core interface |
| Create | `include/miki/platform/ContinuousInputState.h` | **public** | **M** | 6-DOF state |
| Create | `src/miki/platform/NullBridge.h` | internal | L | No-op impl |
| Create | `src/miki/platform/NullBridge.cpp` | internal | L | — |
| Create | `demos/framework/glfw/GlfwBridge.h` | shared | **M** | GLFW bridge |
| Create | `demos/framework/glfw/GlfwBridge.cpp` | shared | L | — |
| Create | `demos/framework/neko/NekoBridge.h` | shared | **M** | neko bridge |
| Create | `demos/framework/neko/NekoBridge.cpp` | shared | L | — |
| Create | `tests/unit/test_uibridge.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define IUiBridge.h + ContinuousInputState.h (public H/M)
      **Signatures** (`IUiBridge.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `IUiBridge::GetViewportRect` | `() const -> Rect` | `[[nodiscard]]` virtual pure |
      | `IUiBridge::ScreenToWorld` | `(float x, float y) const -> Ray` | `[[nodiscard]]` virtual pure |
      | `IUiBridge::WorldToScreen` | `(float3 pos) const -> Point2D` | `[[nodiscard]]` virtual pure |
      | `IUiBridge::OnResize` | `(uint32_t w, uint32_t h) -> void` | virtual |
      | `IUiBridge::OnInputEvent` | `(neko::platform::Event const&) -> bool` | virtual — returns true if event consumed; enables Phase 9 propagation control |
      | `IUiBridge::OnContinuousInput` | `(ContinuousInputState const&) -> void` | virtual |
      | `IUiBridge::OnFocusChange` | `(bool focused) -> void` | virtual |
      | `IUiBridge::SetCamera` | `(ICamera const*) -> void` | virtual — required for ScreenToWorld/WorldToScreen; nullptr = no camera |
      | `~IUiBridge` | virtual default | — |

      **Signatures** (`ContinuousInputState.h`):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `ContinuousInputState` | `{ float3 translationVelocity; float3 rotationVelocity; float dominantAxis; }` | — |

      **Acceptance**: compiles
      `[verify: compile]`

- [x] **Step 2**: Implement NullBridge + GlfwUiBridge + NekoUiBridge
      **Acceptance**: all three compile and link
      `[verify: compile]`

- [x] **Step 3**: Unit tests
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(IUiBridge, NullBridge_NoThrow)` | Positive | All NullBridge methods callable without crash | 2-3 |
| `TEST(IUiBridge, NullBridge_ViewportRect)` | Boundary | Returns zero rect | 2-3 |
| `TEST(IUiBridge, GlfwBridge_Resize)` | Positive | OnResize updates viewport | 2-3 |
| `TEST(IUiBridge, GlfwBridge_MouseEvent)` | Positive | GLFW mouse callback → OnInputEvent | 2-3 |
| `TEST(IUiBridge, ContinuousInput_6DOF)` | Positive | 6-DOF state round-trip | 1-3 |
| `TEST(IUiBridge, FocusChange)` | Positive | Focus event forwarded | 2-3 |

## Design Decisions

- **SetCamera → SetViewProjection**: Task spec says `SetCamera(ICamera const*)`. OrbitCamera has no ICamera interface, and introducing one would create a platform→demo dependency. Changed to `SetViewProjection(float4x4 const&)` — caller passes the matrix directly. Strictly simpler, same intent.
- **GlfwUiBridge / NekoUiBridge**: Separate classes from existing GlfwBridge/NekoBridge (which are AppLoop event sources). UiBridge classes are event consumers + viewport query interfaces. No code duplication — orthogonal concerns.
- **InvertMatrix**: Cramer's rule 4x4 inverse inlined in GlfwUiBridge/NekoUiBridge for ScreenToWorld. No external math library dependency.
- **NullBridge**: Header-only inline implementation. `.cpp` exists only to give the static library a translation unit.

## Implementation Notes

Contract check: PASS (with SetCamera→SetViewProjection deviation, documented above)
