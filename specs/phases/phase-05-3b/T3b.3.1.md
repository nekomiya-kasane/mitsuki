# T3b.3.1 — CSM Cascade Computation + Shadow Render Pass

**Phase**: 05-3b (Shadows, Post-Processing & Visual Regression)
**Component**: 3 — CSM Shadows
**Roadmap Ref**: `roadmap.md` Phase 3b — CSM; `rendering-pipeline-architecture.md` §7.3 Pass #13
**Status**: Complete
**Current Step**: —
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3b.1.2 | Tech Debt B (FrameResources) | Not Started | `FrameResources`, per-frame-in-flight pools |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `CsmShadows.h` | **public** | **M** | `CsmShadows` — cascade shadow map system (2-4 cascades), `CascadeData`, `CsmConfig` |
| `CsmShadows.cpp` | internal | L | Cascade split computation, atlas layout, shadow render pass |
| `csm_depth.slang` | internal | L | Depth-only vertex shader for shadow render |

- **Error model**: `Result<CsmShadows>` from `Create()`
- **Thread safety**: single-threaded render graph execution
- **Cascade split**: logarithmic lambda=0.7, blend log/linear. Tier2/T4: 4 cascades (2048² each). Tier3: 2 cascades (1024² each).
- **Atlas layout**: single D32_SFLOAT texture, cascades packed as 2x2 (4-cascade) or 1x2 (2-cascade). Total: T2/T4 = 4096x4096, T3 = 2048x1024.
- **PSO**: depth-only, front-face cull, depthBias=(4, 2.0), depthClamp=true, CompareOp::GreaterEqual (Reverse-Z).
- **Render**: CPU frustum-culled draw list per cascade. Vertex shader transforms to light-space.

### Downstream Consumers

- `CsmShadows.h` (**public** M):
  - T3b.3.2: reads `CascadeData` (VP matrices, split distances) for deferred resolve sampling
  - T3b.16.1: `IPipelineFactory::CreateShadowPass()` uses CSM for Tier2/3/4
  - Phase 7a-2: shadow atlas extends CSM infrastructure for point/spot lights

### Upstream Contracts

- Phase 3a: `RenderGraphBuilder::AddGraphicsPass()`, `ICommandBuffer::BeginRendering/EndRendering`
- Phase 3a: `GBuffer` provides depth for cascade split distance reference
- Phase 2: `GeometryPassDesc`, `IPipelineFactory::CreateGeometryPass()` for depth-only PSO
- T3b.1.2: `FrameResources` for per-frame resource access

### Technical Direction

- **Cascade computation**: per-frame CPU computation of cascade VP matrices from directional light direction + camera frustum. Each cascade covers a sub-frustum slice.
- **Frustum stabilization**: snap cascade AABB to texel grid to prevent shadow shimmer on camera rotation.
- **Depth-only render**: reuse existing `DrawCall` list from ForwardPass/GBufferPass; filter by per-cascade frustum cull (CPU). Single `BeginRendering` per cascade with viewport/scissor to atlas sub-region.
- **Cascade overlap**: 10% border for smooth blend at transition boundaries.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/CsmShadows.h` | **public** | **M** | CSM system interface |
| Create | `src/miki/rendergraph/CsmShadows.cpp` | internal | L | Cascade computation + render |
| Create | `shaders/render/csm_depth_vert.slang` | internal | L | Depth-only vertex shader |
| Modify | `src/miki/rendergraph/CMakeLists.txt` | internal | L | Add CsmShadows.cpp |
| Create | `tests/unit/test_csm_shadows.cpp` | internal | L | Unit tests |
| Modify | `tests/unit/CMakeLists.txt` | internal | L | Add test target |

## Steps

- [x] **Step 1**: Define `CsmShadows` interface and `CascadeData`
      **Signatures** (`CsmShadows.h`):

      | Symbol | Signature / Fields | Attrs |
      |--------|-------------------|-------|
      | `CsmConfig` | `{ uint32_t cascadeCount=4; uint32_t resolution=2048; float splitLambda=0.7f; float maxShadowDistance=500.0f; }` | — |
      | `CascadeData` | `{ float4x4 lightVP[4]; float splitDistances[4]; uint32_t activeCascades; }` | `alignas(16)` |
      | `CsmShadows::Create` | `(CsmShadowsDesc) -> Result<CsmShadows>` | `[[nodiscard]]` static |
      | `CsmShadows::ComputeCascades` | `(CameraUBO, float3 lightDir, CsmConfig) -> CascadeData` | `[[nodiscard]]` |
      | `CsmShadows::Setup` | `(RenderGraphBuilder&, CsmConfig) -> RGHandle` | `[[nodiscard]]` static |
      | `CsmShadows::AddToGraph` | `(RenderGraphBuilder&, RGHandle, ExecuteFn) -> RGPassHandle` | `[[nodiscard]]` static |
      | `CsmShadows::Execute` | `(RenderContext&, CascadeData, span<const DrawCall>) -> void` | — |
      | `CsmShadows::GetAtlasTexture` | `() const -> TextureHandle` | `[[nodiscard]]` |

      `[verify: compile]`

- [x] **Step 2**: Implement cascade split computation
      `[verify: test]` — unit test cascade distances + VP matrices

- [x] **Step 3**: Implement shadow render pass (depth-only, per-cascade viewport)
      `[verify: compile]`

- [x] **Step 4**: Write csm_depth_vert.slang shader
      `[verify: compile]`

- [x] **Step 5**: Tests (cascade correctness, atlas layout, frustum stabilization)
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(CsmShadows, CascadeSplitDistances)` | Positive | log/linear blend produces monotonic splits | 2 |
| `TEST(CsmShadows, CascadeVPMatricesValid)` | Positive | light VP matrices produce valid NDC for known points | 2 |
| `TEST(CsmShadows, FrustumStabilization)` | Positive | small camera rotation does not shift cascade AABB by more than 1 texel | 2 |
| `TEST(CsmShadows, AtlasLayout4Cascade)` | Positive | 4 cascades pack into 2x2 grid | 1 |
| `TEST(CsmShadows, AtlasLayout2Cascade)` | Boundary | 2 cascades pack into 1x2 grid | 1 |
| `TEST(CsmShadows, CreateReturnsValid)` | Positive | factory produces valid instance | 1 |
| `TEST(CsmShadows, MaxShadowDistance)` | Boundary | objects beyond maxDistance not in any cascade | 2 |
| `TEST(CsmShadows, CascadeOverlap10Percent)` | Positive | adjacent cascades overlap by ~10% | 2 |
| `TEST(CsmShadows, Setup_DeclaresShadowAtlasTexture)` | Positive | RG resource created with correct format/size | 1 |
| `TEST(CsmShadows, EndToEnd_RenderAndReadback)` | Integration | render depth -> readback -> non-zero pixels | 2-4 |
| `TEST(CsmShadows, Create_NullDevice_Error)` | Error | null device returns error | 1 |
| `TEST(CsmShadows, MoveSemantics)` | State | move transfers atlas handle, source empty | 1 |
| `TEST(CsmShadows, SingleCascade)` | Boundary | cascadeCount=1 produces single viewport | 2 |
