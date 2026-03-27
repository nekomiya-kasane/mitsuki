# T6a.6.2 — Material Resolve Compute — Tile-Based Binning + BDA Attribute Fetch + StandardPBR Eval

**Phase**: 08-6a (GPU-Driven Rendering Core)
**Component**: Visibility Buffer + Material Resolve
**Roadmap Ref**: `roadmap.md` L1741 — Material resolve compute
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6a.6.1 | Visibility Buffer | Not Started | `VisibilityBuffer::GetTexture()`, decode helpers |
| *(removed)* | *(GpuRadixSort no longer needed for material resolve — tile-based binning used instead)* | — | — |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `include/miki/vgeo/MaterialResolve.h` | **public** | **M** | `MaterialResolve::Create()`, `Execute()` |
| `shaders/vgeo/material_resolve.slang` | internal | L | VisBuffer → BDA attribute fetch → StandardPBR eval → deferred target write |
| `src/miki/vgeo/MaterialResolve.cpp` | internal | L | Pipeline creation + dispatch |

- **Error model**: `Create()` → `Result<MaterialResolve>`. `Execute()` is void.
- **Thread safety**: stateful. Single-owner.
- **GPU constraints**: compute dispatch `ceil(W/16) * ceil(H/16)` workgroups (16×16 tiles). Reads VisBuffer (R32G32_UINT — full 32-bit instanceId + primitiveId, materialId resolved via `GpuInstance[instanceId].materialId`), instance buffer (BDA), meshlet descriptors (BDA), vertex/index buffers (BDA), `MaterialParameterBlock[]` (bindless). Writes to deferred targets: albedo (RGBA8), normal (RGBA16F), roughness-metallic (RG8), motion (RG16F), optional emission (RGBA8).
- **Invariants**: every valid VisBuffer pixel produces a fully shaded deferred output. Invalid pixels (sentinel) write zero.

### Downstream Consumers

- `MaterialResolve.h` (**public**, heat **M**):
  - T6a.8.1 (Demo): calls `Execute()` in render graph after geometry pass
  - Phase 6b: material resolve unchanged (reads same VisBuffer format)
  - Phase 7a-2: material resolve extended with selection highlight color override

### Upstream Contracts
- T6a.6.1: `VisibilityBuffer::GetTexture()` → `TextureHandle` (R32G32_UINT)
- *(T6a.2.2 GpuRadixSort is NOT used by material resolve — tile-based binning replaces full-screen sort per `rendering-pipeline-architecture.md` §5.4)*
- T6a.4.1: `SceneBuffer::GetGpuBuffer()` → GpuInstance[] for worldMatrix, materialId
- T6a.1.1: `MeshletDescriptor` for vertexOffset/indexOffset
- Phase 4: `BDAManager::GetPointer()` for vertex/index buffer addresses
- Phase 4: `BindlessTable::GetDescriptorSet()` for material texture bindings
- Phase 3a: `DeferredResolve` existing deferred target format (albedo RGBA8, normal RGBA16F, depth D32F, motion RG16F)
- Phase 2: `StandardPBR` shader code for Cook-Torrance GGX evaluation

### Technical Direction
- **Tile-based material binning** (per `rendering-pipeline-architecture.md` §5.4, matching UE5 Nanite approach): dispatch 16×16 tile workgroups. Per-tile shared memory scans all pixels to build a materialId histogram. Three paths based on unique material count:
  - **FAST path** (1 material, ~70-85% of tiles in CAD): direct resolve, zero sorting overhead. All threads read the same `MaterialParameterBlock` — perfect L1 cache coherence.
  - **MEDIUM path** (2-8 materials): LDS sort by materialId → sequential material evaluation runs.
  - **SLOW path** (>8 materials): Z-Binning mitigation — stratify pixels by depth into 2-4 layers, each layer typically has fewer materials, then apply MEDIUM path per layer. Converts one expensive sort into 2-4 cheap LDS sorts.
- **Why tile-based over full-screen GpuRadixSort**: full-screen radix sort on 8.3M pixels (4K) costs ~1.0-1.5ms in bandwidth alone. Tile-based exploits spatial coherence — CAD models have large single-material regions. `GpuRadixSort` remains available for Phase 6b (DAG cut optimizer) and Phase 7a-2 (pick dedup) but is NOT used by material resolve.
- **materialId resolution**: VisBuffer stores full 32-bit `{instanceId, primitiveId}`. materialId obtained via `GpuInstance[instanceId].materialId` indirection (1 SSBO read per pixel, coherent within tile).
- **BDA attribute reconstruction**: for each pixel, read `{instanceId, primitiveId}` → look up `GpuInstance` → get `meshletBaseIndex` → compute meshlet index from primitiveId → look up `MeshletDescriptor` → compute local triangle index → BDA load 3 vertex positions + 3 normals from vertex buffer → barycentric interpolation.
- **Tangent reconstruction**: compute tangent from UV partial derivatives via `dFdx`/`dFdy` of adjacent pixels in tile (compute shader approximation of screen-space derivatives). No tangent attribute stored.
- **StandardPBR in compute**: same BRDF code as `deferred_resolve.slang` (shared `pbr_common.slang`). Reads `MaterialParameterBlock` (192B, per `rendering-pipeline-architecture.md` §6.1) from bindless array. BSDF layers conditionally evaluated (clearcoat/anisotropy/sheen/SSS/transmission skipped when factor==0).
- **Deferred target write**: material resolve writes to the same GBuffer targets that `DeferredResolve` (lighting pass) currently reads. This replaces the GBuffer geometry pass with a pure compute path.
- **Performance budget**: at 4K (8.3M pixels), ~50% valid pixels → 4M pixels to resolve. 70-85% tiles hit FAST path in CAD. Target < 1ms (FAST) to < 2ms (mixed).

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `include/miki/vgeo/MaterialResolve.h` | **public** | **M** | Resolve interface |
| Create | `shaders/vgeo/material_resolve.slang` | internal | L | Mega-kernel resolve |
| Create | `src/miki/vgeo/MaterialResolve.cpp` | internal | L | Implementation |
| Create | `tests/unit/test_material_resolve.cpp` | internal | L | Tests |

## Steps

- [ ] **Step 1**: Define MaterialResolve.h (heat M)

      **Signatures**:

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `MaterialResolve::Create` | `(IDevice&, SlangCompiler&, MaterialResolveDesc const&) -> Result<MaterialResolve>` | `[[nodiscard]]` static |
      | `MaterialResolve::Execute` | `(ICommandBuffer&, MaterialResolveParams const&) -> void` | — |

      **Signatures** (`MaterialResolveDesc/Params`):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `MaterialResolveDesc` | `{ device, compiler, gBufferFormat, width, height }` | Creation config |
      | `MaterialResolveParams` | `{ visBuffer:TextureHandle, sceneBuffer:BufferHandle, meshletBuffer:BufferHandle, vertexBDA:BDAPointer, indexBDA:BDAPointer, bindlessSet:DescriptorSetHandle, cameraUbo:BufferHandle, outputAlbedo:TextureHandle, outputNormal:TextureHandle, outputRoughMetal:TextureHandle, outputDepth:TextureHandle, outputMotion:TextureHandle, width:u32, height:u32 }` | Per-frame params |

      `[verify: compile]`

- [ ] **Step 2**: Implement material_resolve.slang — tile-based binning (FAST/MEDIUM/SLOW paths) + VisBuffer decode + BDA attribute fetch + BRDF eval
      `[verify: compile]`

- [ ] **Step 3**: Implement MaterialResolve.cpp — pipeline + dispatch
      `[verify: compile]`

- [ ] **Step 4**: Unit tests
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(MaterialResolve, CreateValid)` | Positive | Create returns valid instance | 1-3 |
| `TEST(MaterialResolve, ResolveKnownGeometry)` | Positive | Known VisBuffer + geometry → correct albedo output | 2-4 |
| `TEST(MaterialResolve, InvalidPixel_WritesZero)` | Positive | Sentinel pixel → zero output in deferred targets | 2-4 |
| `TEST(MaterialResolve, MaterialIdFetch_Correct)` | Positive | Different materialIds → different albedo colors | 2-4 |
| `TEST(MaterialResolve, BarycentricInterpolation)` | Positive | Smooth normal interpolation across triangle | 2-4 |
| `TEST(MaterialResolve, TangentReconstruction)` | Positive | dFdx/dFdy tangent matches precomputed reference within tolerance | 2-4 |
| `TEST(MaterialResolve, EmptyVisBuffer)` | Boundary | All sentinel → all zero output | 2-4 |
| `TEST(MaterialResolve, FullVisBuffer)` | Boundary | 100% coverage → all pixels resolved | 2-4 |
| `TEST(MaterialResolve, Perf_4K_Under2ms)` | Benchmark | 3840×2160 resolve < 2ms | 2-4 |
| `TEST(MaterialResolve, EndToEnd_GeometryToLighting)` | **Integration** | Full pipeline: meshlet → task → mesh → VisBuffer → resolve → lighting → verify final pixel | 2-4 |

## Design Decisions

*(Fill during implementation.)*

## Implementation Notes

*(Fill during implementation.)*
