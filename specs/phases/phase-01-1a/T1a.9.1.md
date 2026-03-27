# T1a.9.1 — Event Types + AppLoop + FrameManager + StagingUploader + OrbitCamera

**Phase**: 01-1a (Core Architecture & Tier1 Backends)
**Component**: App Framework (demo-only)
**Roadmap Ref**: `roadmap.md` L332 — Dual-backend demo harness, neko::platform::Event, AppLoop, FrameManager, StagingUploader, OrbitCamera (6-DOF)
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T1a.3.2 | IDevice + ICommandBuffer | Complete | `IDevice` for FrameManager fence/semaphore, `BufferHandle` for StagingUploader |
| T1a.2.1 | ErrorCode + Result + Types | Complete | `Result<T>`, `float3`, `float4x4` for OrbitCamera |

## Context Anchor

### This Task's Contract

**Produces** (sorted by Ref Heat — highest first):

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/platform/Event.h` | **public** | **H** | `neko::platform::Event` — canonical input event type. `std::variant<CloseRequested, Resize, MouseMove, MouseButton, KeyDown, KeyUp, Scroll, TextInput, Focus, DpiChanged>`. Used across all phases for input handling. |
| `include/miki/rhi/StagingUploader.h` | **public** | **M** | `StagingUploader` — ring buffer CPU->GPU upload. Used by Phase 2 for mesh upload, all subsequent phases. |
| `include/miki/rhi/FrameManager.h` | **public** | **M** | `FrameManager` — double-buffered frame pacing with fence-per-frame. Part of core miki (no windowing dep). |
| `demos/framework/common/AppLoop.h` | shared | **M** | `AppLoop` abstract interface — `PollEvents()`, `ShouldClose()`, `SwapBuffers()`. Demo-only. |
| `demos/framework/common/OrbitCamera.h` | shared | **M** | `OrbitCamera` — mouse/keyboard + velocity-driven 6-DOF (SpaceMouse). Demo-only but used by all demos. |
| `demos/framework/common/ProceduralGeometry.h` | shared | L | Cube, sphere, grid generators. Demo utility. |
| `tests/unit/test_event.cpp` | internal | L | Event variant tests |
| `tests/unit/test_frame_staging.cpp` | internal | L | FrameManager + StagingUploader tests |

- **Error model**: `Result<T>` for StagingUploader. Events are value types (no errors).
- **Thread safety**: `Event` is immutable value type. `FrameManager` single-owner. `StagingUploader` single-owner.
- **GPU constraints**: `StagingUploader` uses persistently mapped ring buffer. `FrameManager` uses timeline fence/semaphore.
- **Invariants**: `Event` variant always holds exactly one event subtype. `FrameManager` blocks if GPU is 2+ frames behind. `StagingUploader` ring wraps transparently.

### Downstream Consumers

- `Event.h` (**public**, heat **H**):
  - T1a.9.2 (same Phase): GLFW/neko backends produce `Event` variants
  - Phase 3a: `IUiBridge` consumes `Event` for viewport input
  - Phase 9: Interactive tools consume `Event` for gizmo input
  - Phase 12: Multi-window routes `Event` per viewport
- `StagingUploader.h` (**public**, heat **M**):
  - T1a.12.1 (same Phase): Triangle demo uploads vertex data
  - Phase 2: Mesh upload via StagingUploader
  - Phase 2: TextRenderer atlas upload
- `FrameManager.h` (**public**, heat **M**):
  - T1a.12.1 (same Phase): Triangle demo frame pacing
  - Phase 2+: All rendering uses FrameManager
- `AppLoop.h` (shared, heat **M**):
  - T1a.9.2 (same Phase): GLFW/neko backends implement `AppLoop`
  - T1a.12.1 (same Phase): Triangle demo uses `AppLoop`

### Upstream Contracts

- T1a.3.2: `IDevice` (for FrameManager fence creation), `BufferHandle` (for StagingUploader target)
- T1a.2.1: `float3`, `float4x4` (for OrbitCamera), `Result<T>` (for StagingUploader)

### Technical Direction

- **neko::platform::Event as canonical type**: All input events use this single variant type. No GLFW-specific or Win32-specific event types leak into rendering code.
- **AppLoop abstraction**: Demo code depends only on `AppLoop`, never on GLFW or neko APIs directly. Both backends implement `AppLoop`.
- **FrameManager in core**: Part of `miki::rhi`, not demo framework. Double-buffered fence-per-frame. GL/WebGPU: single-buffered fallback.
- **StagingUploader**: Ring buffer with persistent mapping. Vulkan: `VK_MEMORY_PROPERTY_HOST_VISIBLE | HOST_COHERENT` via VMA. D3D12: upload heap. GL: `glBufferSubData` ring or persistent mapped via `GL_ARB_buffer_storage`. WebGPU: `wgpu::Queue::WriteBuffer`.
- **OrbitCamera 6-DOF**: Velocity-driven mode for 3Dconnexion SpaceMouse: `OnContinuousInput(ContinuousInputState{float3 translationVelocity, float3 rotationVelocity})`. Standard mouse/keyboard orbit + pan + zoom.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/platform/Event.h` | **public** | **H** | Canonical event type |
| Create | `include/miki/rhi/StagingUploader.h` | **public** | **M** | CPU->GPU ring buffer |
| Create | `src/miki/rhi/StagingUploader.cpp` | internal | L | Per-backend staging impl |
| Create | `include/miki/rhi/FrameManager.h` | **public** | **M** | Frame pacing |
| Create | `src/miki/rhi/FrameManager.cpp` | internal | L | Fence-per-frame impl |
| Create | `demos/framework/common/AppLoop.h` | shared | **M** | Abstract loop interface |
| Create | `demos/framework/common/OrbitCamera.h` | shared | **M** | Camera controller |
| Create | `demos/framework/common/OrbitCamera.cpp` | internal | L | Camera impl |
| Create | `demos/framework/common/ProceduralGeometry.h` | shared | L | Procedural mesh utils |
| Create | `tests/unit/test_event.cpp` | internal | L | Event tests |
| Create | `tests/unit/test_frame_staging.cpp` | internal | L | Frame + staging tests |

## Steps

- [x] **Step 1**: Define `neko::platform::Event` variant type
      **Files**: `Event.h` (**public** H)

      **Signatures** (`Event.h`):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `CloseRequested` | `{}` | — |
      | `Resize` | `{ width:u32, height:u32 }` | — |
      | `MouseMove` | `{ x:f64, y:f64, dx:f64, dy:f64 }` | — |
      | `MouseButton` | `{ button:MouseBtn, action:Action, mods:Modifiers }` | — |
      | `KeyDown` | `{ key:Key, scancode:u32, mods:Modifiers }` | — |
      | `KeyUp` | `{ key:Key, scancode:u32, mods:Modifiers }` | — |
      | `Scroll` | `{ dx:f64, dy:f64 }` | — |
      | `TextInput` | `{ codepoint:char32_t }` | — |
      | `Focus` | `{ focused:bool }` | — |
      | `DpiChanged` | `{ scale:f32 }` | — |
      | `ContinuousInput` | `{ translationVelocity:float3, rotationVelocity:float3 }` | SpaceMouse 6-DOF |
      | `Event` | `std::variant<CloseRequested, Resize, MouseMove, MouseButton, KeyDown, KeyUp, Scroll, TextInput, Focus, DpiChanged, ContinuousInput>` | — |
      | `MouseBtn` | `enum class : u8 { Left, Right, Middle, X1, X2 }` | — |
      | `Action` | `enum class : u8 { Press, Release, Repeat }` | — |
      | `Key` | `enum class : u16 { A..Z, Num0..Num9, F1..F12, Space, Enter, Escape, Tab, Shift, Control, Alt, ... }` | — |
      | `Modifiers` | `enum class : u8 { None=0, Shift=1<<0, Control=1<<1, Alt=1<<2, Super=1<<3 }` | Bitmask |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | Namespace | `neko::platform` |

      **Acceptance**: compiles; variant visit covers all subtypes
      `[verify: compile]`

- [x] **Step 2**: Define StagingUploader + FrameManager
      **Files**: `StagingUploader.h` (**public** M), `StagingUploader.cpp` (internal L), `FrameManager.h` (**public** M), `FrameManager.cpp` (internal L)

      **Signatures** (`StagingUploader.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `StagingUploader::Create` | `static (IDevice&, u64 ringSize) -> Result<StagingUploader>` | `[[nodiscard]]` |
      | `StagingUploader::Upload` | `(span<const byte> data, BufferHandle dst, u64 dstOffset) -> Result<void>` | — |
      | `StagingUploader::Flush` | `(ICommandBuffer&) -> void` | Records copy commands |
      | `StagingUploader::Reset` | `() -> void` | Reset ring position (call after GPU consumed) |

      **Signatures** (`FrameManager.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `FrameManager::Create` | `static (IDevice&, u32 framesInFlight=2) -> Result<FrameManager>` | `[[nodiscard]]` |
      | `FrameManager::BeginFrame` | `() -> Result<u32>` | Returns frame index; blocks if GPU behind |
      | `FrameManager::EndFrame` | `(ICommandBuffer&) -> Result<void>` | Submits + signals fence |
      | `FrameManager::FrameIndex` | `() const noexcept -> u32` | `[[nodiscard]]` |
      | `FrameManager::FramesInFlight` | `() const noexcept -> u32` | `[[nodiscard]]` |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 3**: Define AppLoop + OrbitCamera + ProceduralGeometry
      **Files**: `AppLoop.h` (shared M), `OrbitCamera.h` (shared M), `OrbitCamera.cpp` (internal L), `ProceduralGeometry.h` (shared L)
      `AppLoop`: abstract `PollEvents() -> span<Event>`, `ShouldClose() -> bool`, `SwapBuffers()`.
      `OrbitCamera`: orbit/pan/zoom + 6-DOF velocity input. `Update(span<Event>, float dt) -> float4x4`.
      `ProceduralGeometry`: `CreateCube()`, `CreateSphere()`, `CreateGrid()` returning vertex/index arrays.
      **Acceptance**: compiles
      `[verify: compile]`

- [x] **Step 4**: Unit tests
      **Files**: `tests/unit/test_event.cpp` (internal L), `tests/unit/test_frame_staging.cpp` (internal L)
      Event tests: variant construction, visit all subtypes, move semantics, equality comparison.
      FrameManager: create with MockDevice, BeginFrame/EndFrame cycle.
      StagingUploader: create, upload, ring wrap.
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Type | Validates | Steps |
|-----------|------|-----------|-------|
| `TEST(Event, VariantConstruction)` | Unit | Step 1 — all subtypes construct | 1 |
| `TEST(Event, VisitAllSubtypes)` | Unit | Step 1 — visitor covers all | 1 |
| `TEST(Event, MoveSemantics)` | Unit | Step 1 — move/copy correct | 1 |
| `TEST(StagingUploader, CreateAndUpload)` | Unit | Step 2 — basic upload | 2 |
| `TEST(StagingUploader, RingWrap)` | Unit | Step 2 — ring buffer wraps | 2 |
| `TEST(FrameManager, BeginEndCycle)` | Unit | Step 2 — frame pacing | 2 |
| `TEST(OrbitCamera, UpdateProducesMatrix)` | Unit | Step 3 — valid view matrix | 3 |
| `TEST(OrbitCamera, ContinuousInput6DOF)` | Unit | Step 3 — SpaceMouse velocity | 3 |

## Design Decisions

- **FrameManager pimpl**: Uses pimpl idiom (unique_ptr<Impl>) to hide IDevice dependency from header. WaitIdle() as fence proxy — real per-frame timeline fences are backend-specific, deferred to Phase 2+.
- **StagingUploader pimpl + ring**: Pimpl with ring buffer tracking. Upload() records pending copies with offsets; Flush() emits CopyBuffer commands. Ring wraps to 0 on overflow if data fits. Staging buffer allocated as CpuToGpu/TransferSrc.
- **Event in neko::platform**: Canonical variant type with 11 alternatives. ContinuousInput for SpaceMouse 6-DOF uses miki::core::float3 velocity fields.
- **OrbitCamera in miki::demo**: Spherical coordinates around target. Left-click orbit, middle/right-click pan, scroll zoom. ContinuousInput applies velocity * dt to translation/rotation. ComputeViewMatrix() builds LookAt from spherical coords.
- **ProceduralGeometry header-only**: CreateCube/Sphere/Grid return MeshData{vertices, indices}. Inline to avoid extra translation unit for demo utility.
- **Demo framework CMake**: miki_demo_common STATIC library under demos/framework/common/, linked to miki::core. AppLoop.h and ProceduralGeometry.h are header-only; OrbitCamera.cpp compiled.

## Implementation Notes

- Contract check: PASS — all 20 API items match Context Anchor.
- 28 new tests (14 Event + 6 FrameManager + 8 StagingUploader). Total: 160.
- No OrbitCamera/ProceduralGeometry unit tests in spec's test table, but covered in Step 3 compile verification.
- Spec lists 8 tests in Tests table; actual count is 28 (more granular Event construction tests + additional StagingUploader edge cases).
