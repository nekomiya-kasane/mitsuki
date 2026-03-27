# T3a.2.1 — GBuffer Layout + Geometry Pass (MRT Output)

**Phase**: 04-3a (Render Graph & Deferred Pipeline + TextRenderer)
**Component**: 2 — GBuffer
**Roadmap Ref**: `roadmap.md` L1336 — GBuffer
**Status**: Partial (wiring-only)
**Current Step**: Step 3 (GPU render)
**Resume Hint**: Execute lambda is stub — needs BeginRendering + MRT pipeline bind + draw calls
**Effort**: H (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T3a.1.1 | RenderGraphBuilder | Complete | `RenderGraphBuilder::AddGraphicsPass()`, `CreateTexture()` for MRT declaration |
| T3a.1.3 | RenderGraphExecutor | Complete | `RenderGraphExecutor`, `RenderContext`, pass execution |
| (Phase 2) | MeshData, StagingUploader | Complete | Vertex/index buffers for geometry draw calls |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/rendergraph/GBuffer.h` | **public** | **M** | `GBufferLayout`, `GBufferPass` — MRT geometry pass node |
| `shaders/rendergraph/gbuffer.slang` | internal | L | GBuffer vertex + fragment shader (encode albedo/metallic, normal/roughness) |
| `src/miki/rendergraph/GBufferPass.cpp` | internal | L | Implementation |
| `tests/unit/test_gbuffer.cpp` | internal | L | Unit tests |

- **Error model**: `GBufferPass::Setup()` returns `Result<GBufferLayout>` (validates dimensions); execution may fail via `RenderContext`
- **Thread safety**: single-threaded (render graph execution)
- **GBuffer layout**: Albedo+Metallic (RGBA8), Normal+Roughness (RGBA16F), Depth (D32F), Motion (RG16F)
- **Normal encoding**: octahedral encoding (2-channel, 16-bit per channel) for compactness

### Downstream Consumers

- `GBuffer.h` (**public**, heat **M**):
  - T3a.3.1 (Deferred Resolve) — reads GBuffer textures for lighting
  - T3a.4.2 (IBL) — reads GBuffer depth for ground plane
  - Phase 3b: VSM reads depth, TAA reads motion, GTAO reads normal+depth

### Upstream Contracts
- T3a.1.3: `RenderGraphExecutor::Execute()`, `RenderContext` for cmd buf access
- T3a.1.1: `RenderGraphBuilder::AddGraphicsPass()`, `CreateTexture()`
- Phase 2: `ForwardPass` pattern (vertex transform, mesh draw), `StandardPBR`, `MaterialRegistry`

### Technical Direction
- **MRT rendering**: single geometry pass outputs to 3+ render targets simultaneously
- **Vulkan**: dynamic rendering with multiple color attachments
- **D3D12**: render pass with multiple RTVs
- **GL**: FBO with multiple `GL_COLOR_ATTACHMENT`
- **WebGPU**: render pass with multiple color targets
- **Depth prepass**: **deferred to Phase 3b**. Phase 3b's Hi-Z pyramid (for VSM page request + occlusion culling) naturally requires a depth prepass; adding it here would be premature without consumers. Phase 3a geometry pass writes depth as part of MRT output (hardware early-Z via `DepthWrite=On, DepthTest=LessEqual`). For <10K objects this is sufficient.
- **Velocity buffer**: current-frame vs previous-frame MVP for per-pixel motion vectors (TAA/temporal upscale in Phase 3b)

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/rendergraph/GBuffer.h` | **public** | **M** | GBuffer layout + pass |
| Create | `shaders/rendergraph/gbuffer.slang` | internal | L | GBuffer shaders |
| Create | `src/miki/rendergraph/GBufferPass.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_gbuffer.cpp` | internal | L | Unit tests |

## Steps

- [x] **Step 1**: Define GBuffer.h (public M)
      **Files**: `GBuffer.h` (**public** M)

      **Signatures**:

      | Symbol | Fields / Signature | Attrs |
      |--------|-------------------|-------|
      | `GBufferLayout` | `{ RGHandle albedoMetallic; RGHandle normalRoughness; RGHandle depth; RGHandle motion; }` | — |
      | `GBufferPass::Setup` | `(RenderGraphBuilder&, uint32_t width, uint32_t height) -> GBufferLayout` | `[[nodiscard]]` static — declares MRT resources |
      | `GBufferPass::Execute` | `(RenderContext&, GBufferLayout, span<const DrawCall>, OrbitCamera const&)` | static — records draw commands |

      **Acceptance**: compiles on both build paths
      `[verify: compile]`

- [x] **Step 2**: GBuffer Slang shaders
      **Files**: `gbuffer.slang` (internal L)
      Vertex: world-space position, normal, tangent, motion vector.
      Fragment: encode albedo+metallic to RGBA8, octahedral normal+roughness to RGBA16F, output depth.
      **Acceptance**: shader compiles via SlangCompiler
      `[verify: compile]`

- [ ] **Step 3**: Implement GBufferPass GPU dispatch
      **Files**: `GBufferPass.cpp` (internal L)
      Setup: declare 3 color attachments + depth (**done**). Execute: BeginRendering with MRT attachments, bind GBuffer pipeline, draw meshes (**not done — stub only resolves handles**).
      **Acceptance**: GBuffer renders non-zero MRT output on Vulkan
      `[verify: test]`

- [x] **Step 4**: Unit tests
      **Files**: `test_gbuffer.cpp` (internal L)
      **Acceptance**: all tests pass
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(GBuffer, MRT_Formats)` | Positive | All 4 RT formats match spec | 1-4 |
| `TEST(GBuffer, MotionVector_NonZero)` | Positive | After rendering a moving object, motion vector RT contains non-zero pixels | 3-4 |
| `TEST(GBuffer, ZeroSize_ReturnsError)` | Error | GBufferPass::Setup with width=0 or height=0 returns error | 1-2 |
| `TEST(GBuffer, AlbedoFormat_RGBA8)` | Positive | Albedo texture is RGBA8 | 1-3 |
| `TEST(GBuffer, NormalFormat_RGBA16F)` | Positive | Normal texture is RGBA16F | 1-3 |
| `TEST(GBuffer, DepthFormat_D32F)` | Positive | Depth is D32F | 1-3 |
| `TEST(GBuffer, MotionFormat_RG16F)` | Positive | Motion is RG16F | 1-3 |
| `TEST(GBuffer, RenderSingleCube)` | Integration | GBuffer pass produces non-zero albedo | 3-4 |

## Design Decisions

1. **`Setup` returns `Result<GBufferLayout>`** — spec said infallible, but zero-size validation is essential. Returns `ErrorCode::InvalidArgument` for width=0 or height=0.
2. **`Execute` replaced by `AddToGraph`** — spec's `Execute(RenderContext&, GBufferLayout, span<DrawCall>, OrbitCamera&)` had three problems: (a) `OrbitCamera` is demo code, engine shouldn't depend on it; (b) missing pipeline parameter; (c) render graph execute lambdas have fixed signature `void(PassData&, RenderContext&)`. Solution: `AddToGraph()` accepts an `ExecuteFn` lambda; caller captures pipeline/camera/draws via lambda closure. This is the UE5 RDG / Filament FrameGraph standard pattern.
3. **Separate vert/frag shaders** — spec said single `gbuffer.slang`; split to `gbuffer_vert.slang` + `gbuffer_frag.slang` to match existing `forward_vert.slang`/`forward_frag.slang` convention.
4. **Shader path** — placed in `shaders/render/` (not `shaders/rendergraph/`) to co-locate with forward pass shaders.
5. **`GBufferDesc` struct** — wraps width/height instead of bare parameters for extensibility (future: sample count, format overrides).
6. **Format constants exported** — `kGBufferAlbedoMetallicFormat` etc. as `inline constexpr` in header so downstream passes (deferred resolve, TAA) can reference them without magic numbers.
7. **GPU render tests deferred** — `MotionVector_NonZero` and `RenderSingleCube` require full pipeline bootstrap (shader compilation + pipeline creation + descriptor sets). ~~Deferred to T3a.7.1 demo integration.~~ **2026-03-16: T3a.7.1 did not activate GPU dispatch either. Step 3 reopened.** 13 CPU tests validate resource declaration correctness.

## Implementation Notes

- **Files created**: `include/miki/rendergraph/GBuffer.h`, `src/miki/rendergraph/GBufferPass.cpp`, `shaders/render/gbuffer_vert.slang`, `shaders/render/gbuffer_frag.slang`, `tests/unit/test_gbuffer.cpp`
- **Files modified**: `src/miki/rendergraph/CMakeLists.txt` (added GBufferPass.cpp), `tests/unit/CMakeLists.txt` (added test_gbuffer target)
- **Test count**: 13 (target was 8) — added DimensionsPropagated, TextureUsage_ColorAndSampled, EndToEnd_SetupAndAddToGraph, AddToGraph_CreatesPassWithEdges, MinimalSize_1x1
- **Shader features**: octahedral normal encoding (Cigolle et al. 2014 JCGT), per-pixel motion vectors via current/previous MVP, GBufferCameraUBO with prevViewProj for temporal effects
- **Build verification**: debug-d3d12 + debug-vulkan: 0 errors, 13/13 tests pass both paths
