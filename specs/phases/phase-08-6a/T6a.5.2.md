# T6a.5.2 — Mesh Shader — Meshlet BDA Vertex Unpack, Normal Cone Backface Cull, Output Primitives

**Phase**: 08-6a (GPU-Driven Rendering Core)
**Component**: Task Shader + Mesh Shader
**Roadmap Ref**: `roadmap.md` L1739 — Mesh Shader
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6a.5.1 | Task Shader | Not Started | `TaskPayload` struct, `GpuCullPipeline` for task+mesh pipeline |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `shaders/vgeo/mesh_geo.slang` | **shared** | **H** | Slang mesh shader: BDA vertex fetch, normal cone re-test, output triangles with instanceId+primitiveId |
| `shaders/vgeo/vgeo_common.slang` | **shared** | **M** | Shared types: `MeshletDescriptor` Slang mirror, BDA fetch helpers, `VisBufferPayload` |

- **Error model**: GPU-side, no CPU error path.
- **Thread safety**: N/A (shader).
- **GPU constraints**: Mesh shader workgroup = 64 threads (1 thread per vertex, max 64 vertices). Output: max 124 primitives × 3 vertices. Outputs `{float4 position, uint instanceId, uint primitiveId}` per vertex. VisBuffer write = `uint2(instanceId, primitiveId)` — **full 32-bit each**, materialId is NOT stored in VisBuffer (resolved at material resolve time via `GpuInstance[instanceId].materialId`). Matches `rendering-pipeline-architecture.md` §5.4.
- **Invariants**: every triangle output has valid instanceId + primitiveId. BDA pointer validity guaranteed by SceneBuffer upload ordering.

### Downstream Consumers

- `mesh_geo.slang` (**shared**, heat **H**):
  - T6a.6.1 (VisBuffer): mesh shader output writes to VisBuffer via color attachment
  - T6a.7.1 (SW Raster): optional hybrid path feeds same primitives to SW raster
  - Phase 6b: mesh shader extended with compressed vertex decode
  - Phase 7a-1 (HLR): separate mesh shader for edge line rendering reuses `vgeo_common.slang` helpers
- `vgeo_common.slang` (**shared**, heat **M**):
  - T6a.5.1 (Task Shader): includes shared descriptor types
  - T6a.6.2 (Material Resolve): includes BDA fetch helpers for attribute reconstruction

### Upstream Contracts
- T6a.5.1: `TaskPayload { meshletIndices[32]:u32, count:u32 }` emitted by task shader
- T6a.1.1: `MeshletDescriptor` (64B) — vertexOffset, vertexCount, indexOffset, indexCount, coneAxis, coneHalfAngle
- T6a.4.1: `GpuInstance` (128B) — worldMatrix, meshletBaseIndex, materialId
- Phase 4: `BDAPointer` — 64-bit GPU address for vertex/index buffer reads

### Technical Direction
- **BDA vertex fetch**: mesh shader reads vertex positions/normals via buffer device address (`vk::RawBufferLoad<float3>(bdaAddress + vertexOffset + threadIdx * stride)`). No vertex input assembly. Slang `ByteAddressBuffer` or `StructuredBuffer` with BDA.
- **Normal cone re-test**: task shader performs coarse instance-level normal cone test. Mesh shader performs fine meshlet-level re-test (optional, can be skipped if task shader already tested meshlet cones).
- **Tangent NOT stored**: tangent reconstructed in VisBuffer material resolve via `dFdx`/`dFdy` of UV coordinates (screen-space derivatives). Saves 12-16B/vertex.
- **Output format**: mesh shader outputs `uint2` per-pixel in VisBuffer: `R32 = instanceId (full 32-bit)`, `G32 = primitiveId (full 32-bit)`. materialId is NOT stored in VisBuffer — resolved at material resolve time via `GpuInstance[instanceId].materialId` indirection. This simplifies encoding (no bitfield packing), supports 4G instances (vs 16M with 24-bit), and matches `rendering-pipeline-architecture.md` §5.4.
- **Slang mesh shader entry**: `[shader("mesh")] [outputtopology("triangle")] [numthreads(64, 1, 1)] void meshMain(...)`.
- **SetMeshOutputCounts**: called with actual vertex/primitive counts for this meshlet (not max).

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `shaders/vgeo/mesh_geo.slang` | **shared** | **H** | Mesh shader |
| Create | `shaders/vgeo/vgeo_common.slang` | **shared** | **M** | Shared types + BDA helpers |
| Modify | `src/miki/vgeo/GpuCullPipeline.cpp` | internal | L | Wire mesh shader blob into pipeline |
| Create | `tests/unit/test_mesh_shader.cpp` | internal | L | Tests |

## Steps

- [ ] **Step 1**: Create vgeo_common.slang (heat M)
      Define Slang mirror of `MeshletDescriptor`, `GpuInstance`, BDA load helpers, `VisBufferPayload` encoding/decoding.
      `[verify: compile]`

- [ ] **Step 2**: Implement mesh_geo.slang (heat H)
      Mesh shader: receive `TaskPayload` → load `MeshletDescriptor` via BDA → fetch vertices → transform → output triangles with VisBuffer payload.
      `[verify: compile]`

- [ ] **Step 3**: Wire into GpuCullPipeline — compile mesh shader blob, include in pipeline desc
      `[verify: compile]`

- [ ] **Step 4**: Unit tests (visual validation + readback)
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(MeshShader, CompileSuccess)` | Positive | mesh_geo.slang compiles to SPIR-V via Slang | 2,4 |
| `TEST(MeshShader, OutputPrimitiveCount)` | Positive | known meshlet → correct primitive count in output | 2-4 |
| `TEST(MeshShader, VisBufferPayload_Encoding)` | Positive | instanceId + primitiveId correctly packed in R32G32 | 2-4 |
| `TEST(MeshShader, BDAVertexFetch_Correctness)` | Positive | vertex positions match CPU-side reference | 2-4 |
| `TEST(MeshShader, NormalConeBackfaceCull)` | Positive | fully backfacing meshlet → 0 output primitives | 2-4 |
| `TEST(MeshShader, EmptyMeshlet)` | Boundary | meshlet with 0 primitives → no output | 2-4 |
| `TEST(MeshShader, MaxMeshlet_64v124p)` | Boundary | max-size meshlet outputs correctly | 2-4 |
| `TEST(MeshShader, VgeoCommon_DescriptorLayout)` | Positive | Slang MeshletDescriptor matches C++ sizeof/offsets | 1,4 |
| `TEST(MeshShader, EndToEnd_TaskMeshPipeline)` | **Integration** | Full task→mesh pipeline renders known geometry, readback VisBuffer verifies instanceId/primitiveId | 2-4 |

## Design Decisions

*(Fill during implementation.)*

## Implementation Notes

*(Fill during implementation.)*
