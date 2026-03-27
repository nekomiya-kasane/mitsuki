# T3b.17.1 — Ground Shadow Plane (Deferred from Phase 3a)

**Phase**: 05-3b
**Component**: 17 — Ground Plane
**Roadmap Ref**: `roadmap.md` Phase 3a Critical Decisions — "Ground plane: deferred to Phase 3b. Requires VSM shadow data."; `rendering-pipeline-architecture.md` S7.4 row "Ground shadow plane"
**Status**: Complete
**Current Step**: —
**Effort**: M (1-2h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.5.2 | VSM Shadows (resolve) | Not Started | VSM shadow sampling in deferred resolve |
| T3b.3.2 | CSM Shadows (resolve) | Not Started | CSM shadow sampling in deferred resolve |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `GroundPlane.h` | **public** | **M** | `GroundPlane` -- shadow-receiving infinite ground plane at y=0, configurable opacity |
| `GroundPlane.cpp` | internal | L | Fullscreen fragment pass: reconstruct world position, shadow sample, alpha-blended shadow |
| `ground_plane.slang` | internal | L | Fragment shader: world-space y=0 intersection, shadow lookup, configurable opacity + fade |

- **Rendering**: fullscreen fragment shader in Scene layer. Per-pixel: reconstruct world position from depth via `invViewProj`, check if `worldPos.y ~= 0` (within epsilon), sample shadow map (VSM on T1, CSM on T2/3/4), output shadow color with configurable opacity.
- **Shadow source**: reads same shadow atlas/page table as deferred resolve (shared descriptor binding). No separate shadow render -- reuses existing shadow data.
- **Conditional**: RenderGraph conditional node. Disabled when `BackgroundMode != GroundPlane` or no directional light. Zero cost when inactive.
- **Fade**: distance fade from camera to avoid infinite plane visual artifacts. `alpha *= 1.0 - smoothstep(fadeStart, fadeEnd, distFromCamera)`.
- **Configuration**: `GroundPlaneConfig { float opacity = 0.5f; float fadeStart = 100.f; float fadeEnd = 500.f; float3 shadowColor = {0,0,0}; float yOffset = 0.f; }`.
- **Error model**: `Result<GroundPlane>` from `Create()`
- **Thread safety**: single-threaded render graph execution
- **Budget**: < 0.1ms GPU (single fullscreen fragment, 1 shadow sample per pixel)

### Downstream Consumers

- `GroundPlane.h` (**public** M):
  - T3b.16.2: demo enables ground plane in deferred_pbr when shadow is active
  - Phase 7a-1: ground plane remains active under HLR display styles (shadow on ground)
  - Phase 9: gizmo ground snap uses ground plane y-level

### Upstream Contracts

- T3b.5.2 / T3b.3.2: shadow atlas/page table available as sampled textures in deferred resolve descriptor set
- Phase 3a: `RenderGraphBuilder::AddGraphicsPass()`, `CameraUBO` (invViewProj for world reconstruct)
- Phase 3a: `BackgroundMode` enum (add `GroundPlane` value)

### Technical Direction

- **Architecture ref**: `rendering-pipeline-architecture.md` S7.4 row: "Ground shadow plane: Shadow map, shadow-only y=0, configurable opacity, <0.1ms, per frame"
- **Implementation**: fullscreen triangle pass after deferred resolve, before post-processing chain. Reads depth buffer to reconstruct world position. If `abs(worldPos.y - yOffset) < epsilon`, sample shadow and output `shadowColor * opacity * shadowFactor * fadeFactor`.
- **BackgroundMode extension**: add `GroundPlane = 4` to existing `BackgroundMode` enum (`{SolidColor=0, VerticalGradient=1, HDRI=2, Transparent=3, GroundPlane=4}`). When active, skybox pass renders normally for sky hemisphere; ground plane pass renders for ground hemisphere.
- **Depth test**: ground plane writes depth at y=0 intersection. Objects below ground plane are occluded (correct for CAD tabletop visualization).
- **No geometry**: pure screen-space ray-plane intersection in fragment shader. Zero vertex data, zero draw calls.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/GroundPlane.h` | **public** | **M** | Interface |
| Create | `src/miki/rendergraph/GroundPlane.cpp` | internal | L | Implementation |
| Create | `shaders/rendergraph/ground_plane.slang` | internal | L | Fragment shader |
| Modify | `include/miki/rendergraph/EnvironmentRenderer.h` | **public** | **M** | Add `GroundPlane` to `BackgroundMode` enum |
| Modify | `src/miki/rendergraph/CMakeLists.txt` | internal | L | Add GroundPlane.cpp |
| Create | `tests/unit/test_ground_plane.cpp` | internal | L | Tests |
| Modify | `tests/unit/CMakeLists.txt` | internal | L | Add test target |

## Steps

- [x] **Step 1**: Extend `BackgroundMode` enum with `GroundPlane = 4`
      **Files**: `EnvironmentRenderer.h` (**public** M)
      `[verify: compile]`

- [x] **Step 2**: Define `GroundPlane` interface and `GroundPlaneConfig`
      **Signatures** (`GroundPlane.h`):

      | Symbol | Signature / Fields | Attrs |
      |--------|-------------------|-------|
      | `GroundPlaneConfig` | `{ float opacity=0.5f; float fadeStart=100.f; float fadeEnd=500.f; float3 shadowColor={0,0,0}; float yOffset=0.f; }` | -- |
      | `GroundPlane::Create` | `(GroundPlaneDesc) -> Result<GroundPlane>` | `[[nodiscard]]` static |
      | `GroundPlane::Setup` | `(RenderGraphBuilder&, uint32_t w, uint32_t h) -> RGHandle` | `[[nodiscard]]` static |
      | `GroundPlane::AddToGraph` | `(RenderGraphBuilder&, RGHandle, ExecuteFn) -> RGPassHandle` | `[[nodiscard]]` static |
      | `GroundPlane::Execute` | `(RenderContext&, GroundPlaneConfig, CameraUBO, TextureHandle shadowAtlas, CascadeData or VsmPageTable) -> void` | -- |

      `[verify: compile]`

- [x] **Step 3**: Implement ground_plane.slang fragment shader
      Ray-plane intersection at y = yOffset. Shadow sample from VSM/CSM. Distance fade. Alpha blend.
      `[verify: compile]`

- [x] **Step 4**: Implement GroundPlane.cpp (pipeline creation, RG integration)
      `[verify: compile]`

- [x] **Step 5**: Integration with demo (enable when BackgroundMode == GroundPlane)
      `[verify: compile]`

- [x] **Step 6**: Tests
      `[verify: test]`

## Tests

| Test Name | Category | Validates |
|-----------|----------|-----------|
| `TEST(GroundPlane, CreateReturnsValid)` | Positive | factory success |
| `TEST(GroundPlane, Create_NullDevice_Error)` | Error | null device returns error |
| `TEST(GroundPlane, ShadowVisibleOnGround)` | Positive | object above y=0 casts shadow on ground plane |
| `TEST(GroundPlane, NoShadow_FullyLit)` | Positive | no directional light = no shadow on ground |
| `TEST(GroundPlane, OpacityZero_Invisible)` | Boundary | opacity=0 produces fully transparent ground |
| `TEST(GroundPlane, OpacityOne_FullShadow)` | Boundary | opacity=1 produces maximum shadow darkness |
| `TEST(GroundPlane, DistanceFade)` | Positive | ground fades to transparent at fadeEnd distance |
| `TEST(GroundPlane, YOffset_ShiftsPlane)` | Positive | yOffset=5 places ground plane at y=5 |
| `TEST(GroundPlane, ConditionalDisabled_ZeroCost)` | Boundary | BackgroundMode != GroundPlane = no RG node |
| `TEST(GroundPlane, MoveSemantics)` | State | move transfers pipeline, source empty |
| `TEST(GroundPlane, VsmShadow_OnGround)` | Positive | Tier1 VSM shadow visible on ground |
| `TEST(GroundPlane, CsmShadow_OnGround)` | Positive | Tier2/3/4 CSM shadow visible on ground |
| `TEST(GroundPlane, EndToEnd_GroundPlaneDemo)` | Integration | full scene with ground plane renders correctly |
