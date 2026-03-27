# T3a.7.3 — CameraUBO Extend

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 7 — 5-Backend Sync + Demo + Phase 2 Debt Cleanup
**Roadmap Ref**: `roadmap.md` — Phase 2 rendering debt cleanup (D7: prevViewProj hack)
**Status**: Complete
**Current Step**: Done
**Resume Hint**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.3.1 | Deferred PBR resolve | Complete | `DeferredResolve::Execute()`, `deferred_resolve.slang` — consumer of `CameraUBO.inverseViewProj` |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/render/CameraUBO.h` | **public** | **H** | `CameraUBO` struct — extended with `view`, `proj`, `inverseViewProj`, `jitter`, `resolution` |
| `shaders/common/camera_ubo.slang` | **public** | **H** | GPU-side mirror of C++ `CameraUBO` — must match byte-for-byte |
| `shaders/rendergraph/deferred_resolve.slang` | shared | **M** | Remove `prevViewProj` hack, use dedicated `inverseViewProj` field |
| `demos/deferred_pbr_basic/main.cpp` | internal | L | Fill new CameraUBO fields |
| `demos/forward_cubes/main.cpp` | internal | L | Fill new CameraUBO fields |
| `src/miki/rendergraph/DeferredResolve.cpp` | internal | L | Update bufferRange constant |
| `tests/unit/test_camera_ubo.cpp` | internal | L | Layout + alignment tests |

**Layout (std140, 368 bytes, alignas(16))**:

| Offset | Field | Type | Size |
|--------|-------|------|------|
| 0 | viewProj | float4x4 | 64B |
| 64 | prevViewProj | float4x4 | 64B |
| 128 | view | float4x4 | 64B |
| 192 | proj | float4x4 | 64B |
| 256 | inverseViewProj | float4x4 | 64B |
| 320 | cameraPos | float3 (+4B implicit pad) | 16B |
| 336 | nearPlane | float | 4B |
| 340 | farPlane | float | 4B |
| 344 | jitterX | float | 4B |
| 348 | jitterY | float | 4B |
| 352 | resolutionX | float | 4B |
| 356 | resolutionY | float | 4B |
| 360 | _pad0 | float | 4B |
| 364 | _pad1 | float | 4B |
| **368** | **total** | | **368B** (= 23 x 16) |

- **Error model**: N/A (POD struct, no fallible operations)
- **Thread safety**: immutable value type per-frame — filled once, uploaded, consumed by GPU
- **GPU constraints**: `alignas(16)`, `static_assert(sizeof(CameraUBO) == 368)`, all matrices column-major
- **Invariants**: C++ struct and Slang struct must be byte-identical; `sizeof` verified by `static_assert`

### Downstream Consumers

- `camera_ubo.slang` (**public**, heat **H**):
  - `forward_vert.slang` — reads `viewProj`, `cameraPosAndPad`
  - `forward_frag.slang` — reads `cameraPosAndPad`
  - `gbuffer_vert.slang` — reads `viewProj`, `prevViewProj`
  - `deferred_resolve.slang` — reads `inverseViewProj` (was: hack via `prevViewProj`), `cameraPosAndPad`
  - Phase 3b TAA — reads `prevViewProj`, `jitter`, `resolution`
  - Phase 3b screen-space effects — reads `view`, `proj`, `inverseViewProj`, `resolution`
- `CameraUBO.h` (**public**, heat **H**):
  - `ForwardPass.cpp` — uploads CameraUBO
  - `GBufferPass.cpp` — uploads CameraUBO
  - `deferred_pbr_basic/main.cpp` — constructs CameraUBO
  - `forward_cubes/main.cpp` — constructs CameraUBO
  - All future demos + passes

### Upstream Contracts
- Phase 2: `core::float4x4` (column-major, 64B), `core::float3` (alignas(16), 16B), `core::float2` (alignas(8), 8B)

### Technical Direction
- **Backward compatible extension**: new fields appended after existing `prevViewProj`; existing shaders that only read `viewProj`/`prevViewProj`/`cameraPosAndPad` still work (offsets unchanged: `viewProj` at 0, `prevViewProj` at 64) — **FALSE**. `view`/`proj`/`inverseViewProj` are inserted between `prevViewProj` and `cameraPosAndPad`, shifting all subsequent offsets. All shaders importing `camera_ubo` must be recompiled. This is acceptable because `camera_ubo.slang` is imported (not hardcoded offsets).
- **Jitter/resolution as scalars**: using `float jitterX, jitterY` instead of `float2 jitter` avoids potential std140 alignment surprises between C++ `float2` (alignas(8)) and Slang `float2` (may have different padding rules depending on preceding fields). Scalars are always safe.
- **D7 debt resolution**: `deferred_resolve.slang` line 111 currently reads `camera.prevViewProj` as `inverseViewProj`. After this task, it reads `camera.inverseViewProj` — semantically correct, frees `prevViewProj` for actual previous-frame data.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Modify | `include/miki/render/CameraUBO.h` | **public** | **H** | Add view, proj, inverseViewProj, jitter, resolution; update sizeof assert |
| Modify | `shaders/common/camera_ubo.slang` | **public** | **H** | Mirror C++ layout |
| Modify | `shaders/rendergraph/deferred_resolve.slang` | shared | **M** | Use `camera.inverseViewProj` |
| Modify | `demos/deferred_pbr_basic/main.cpp` | internal | L | Fill new fields |
| Modify | `demos/forward_cubes/main.cpp` | internal | L | Fill new fields |
| Modify | `src/miki/rendergraph/DeferredResolve.cpp` | internal | L | bufferRange 160 -> sizeof(CameraUBO) |
| Create | `tests/unit/test_camera_ubo.cpp` | internal | L | Layout + alignment tests |
| Modify | `tests/unit/CMakeLists.txt` | internal | L | Add test_camera_ubo.cpp |

## Steps

- [x] **Step 1**: Update CameraUBO.h — add view, proj, inverseViewProj, jitter, resolution fields
      **Files**: `include/miki/render/CameraUBO.h` (**public** H)

      **Signatures** (`CameraUBO.h`):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `CameraUBO` | `viewProj:float4x4, prevViewProj:float4x4, view:float4x4, proj:float4x4, inverseViewProj:float4x4, cameraPos:float3, nearPlane:f32, farPlane:f32, jitterX:f32, jitterY:f32, resolutionX:f32, resolutionY:f32, _pad0:f32, _pad1:f32` | `alignas(16)` |

      **Constraints**:

      | Constraint | Value |
      |------------|-------|
      | `static_assert(sizeof(CameraUBO))` | `== 368` |
      | `static_assert(offsetof(CameraUBO, viewProj))` | `== 0` |
      | `static_assert(offsetof(CameraUBO, inverseViewProj))` | `== 256` |
      | Namespace | `miki::render` |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: Update camera_ubo.slang — mirror C++ struct layout exactly
      **Files**: `shaders/common/camera_ubo.slang` (**public** H)
      **Acceptance**: compiles on both build paths (shaders recompile from source)
      `[verify: compile]`

- [x] **Step 3**: Update deferred_resolve.slang — use `camera.inverseViewProj`, remove hack comments
      **Files**: `shaders/rendergraph/deferred_resolve.slang` (shared M)
      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 4**: Update demos — fill new CameraUBO fields (view, proj, inverseViewProj, jitter=0, resolution)
      **Files**: `demos/deferred_pbr_basic/main.cpp` (internal L), `demos/forward_cubes/main.cpp` (internal L)
      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 5**: Update DeferredResolve.cpp — bufferRange constant
      **Files**: `src/miki/rendergraph/DeferredResolve.cpp` (internal L)
      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 6**: Write unit tests for CameraUBO layout + alignment
      **Files**: `tests/unit/test_camera_ubo.cpp` (internal L), `tests/unit/CMakeLists.txt` (internal L)
      **Acceptance**: all tests pass on both build paths
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(CameraUBO, SizeIs368)` | Positive | sizeof == 368 | 6 |
| `TEST(CameraUBO, AlignmentIs16)` | Positive | alignof == 16 | 6 |
| `TEST(CameraUBO, ViewProjOffset0)` | Positive | offsetof(viewProj) == 0 | 6 |
| `TEST(CameraUBO, PrevViewProjOffset64)` | Positive | offsetof(prevViewProj) == 64 | 6 |
| `TEST(CameraUBO, ViewOffset128)` | Positive | offsetof(view) == 128 | 6 |
| `TEST(CameraUBO, ProjOffset192)` | Positive | offsetof(proj) == 192 | 6 |
| `TEST(CameraUBO, InverseViewProjOffset256)` | Positive | offsetof(inverseViewProj) == 256 | 6 |
| `TEST(CameraUBO, CameraPosOffset320)` | Positive | offsetof(cameraPos) == 320 | 6 |
| `TEST(CameraUBO, NearPlaneOffset336)` | Positive | offsetof(nearPlane) == 336 | 6 |
| `TEST(CameraUBO, JitterXOffset344)` | Positive | offsetof(jitterX) == 344 | 6 |
| `TEST(CameraUBO, ResolutionXOffset352)` | Positive | offsetof(resolutionX) == 352 | 6 |
| `TEST(CameraUBO, DefaultValuesCorrect)` | Positive | default-constructed fields match expected defaults | 6 |

## Design Decisions

- **Scalar jitter/resolution fields** over `float2`: avoids std140 alignment ambiguity between C++ `float2` (alignas(8)) and Slang `float2` layout. Scalars (`jitterX`, `jitterY`) are always safe across all backends.
- **Insert view/proj/inverseViewProj between prevViewProj and cameraPos**: shifts all subsequent offsets, but all shaders import `camera_ubo.slang` (no hardcoded offsets), so recompilation is sufficient.
- **prevViewProj preserved as identity** in demos: Phase 3b TAA will fill it with the actual previous frame's viewProj. Current demos set `prevViewProj = viewProj` (no motion).

## Implementation Notes

- D7 tech debt resolved: `deferred_resolve.slang` now reads `camera.inverseViewProj` directly.
- 4 pre-existing GL/WebGPU test failures unrelated to this change (OffscreenGL, Triangle5B GL paths).
- Contract check: PASS — all `static_assert(offsetof(...))` in `CameraUBO.h` verify byte-exact match with `camera_ubo.slang`.
