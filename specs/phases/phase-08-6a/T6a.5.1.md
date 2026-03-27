# T6a.5.1 — Task Shader — Instance Frustum+Occlusion Cull, Emit Mesh Workgroups

**Phase**: 08-6a (GPU-Driven Rendering Core)
**Component**: Task Shader + Mesh Shader
**Roadmap Ref**: `roadmap.md` L1738 — Task Shader Amplification
**Status**: Not Started
**Current Step**: —
**Resume Hint**: —
**Effort**: L (2-4h)

## Dependencies

| Task ID | Name | Status | Key Output Used |
|---------|------|--------|-----------------|
| T6a.1.1 | MeshletGenerator | Not Started | `MeshletDescriptor.boundingSphere/normalCone` for per-meshlet cull |
| T6a.3.1 | HiZ Pyramid | Not Started | `HiZPyramid::GetTexture()` for occlusion test |
| T6a.4.1 | SceneBuffer | Not Started | `SceneBuffer::GetGpuBuffer()` for `GpuInstance` array |

## Context Anchor

### This Task's Contract

**Produces**:

| File | Exposure | Ref Heat | Defines |
|------|----------|----------|---------|
| `shaders/vgeo/task_cull.slang` | **shared** | **H** | Slang task shader: instance-level + meshlet-level frustum+occlusion cull, emit mesh workgroups |
| `include/miki/vgeo/GpuCullTypes.h` | **public** | **M** | `CullUniforms` push constant struct (frustum planes, HiZ mip count, near/far, camera pos), `TaskPayload` |
| `include/miki/vgeo/GpuCullPipeline.h` | **public** | **M** | `GpuCullPipeline::Create()`, `BuildPipeline()` — creates the task+mesh graphics pipeline |

- **Error model**: Pipeline creation → `Result<PipelineHandle>`. Task shader is GPU-side, no CPU error path.
- **Thread safety**: `GpuCullPipeline` stateful, single-owner.
- **GPU constraints**: Task shader workgroup size = 32 (1 thread per instance in payload). `TaskPayload { uint meshletIndices[32]; uint count; }` = 132B. Push constants: `CullUniforms` ≤ 128B (6 frustum planes = 96B + HiZ info = 16B + camera = 16B).
- **Invariants**: every visible meshlet is emitted to a mesh workgroup. No false negatives (conservative cull). False positives acceptable (minor overdraw from coarse bounding sphere test).

### Downstream Consumers

- `task_cull.slang` (**shared**, heat **H**):
  - T6a.5.2 (Mesh Shader): receives `TaskPayload` from task shader → processes emitted meshlets
  - T6a.8.1 (Demo): task+mesh pipeline used in render graph geometry pass
  - Phase 6b: extends task shader with LOD selection logic (reads `ClusterDAG` error metric)
- `GpuCullTypes.h` (**public**, heat **M**):
  - T6a.5.2: reads `TaskPayload` struct definition
  - T6a.8.1: fills `CullUniforms` from camera
- `GpuCullPipeline.h` (**public**, heat **M**):
  - T6a.8.1: calls `BuildPipeline()` to create task+mesh graphics pipeline

### Upstream Contracts
- T6a.1.1: `MeshletDescriptor` (64B) — `boundingSphere` (float4), `coneAxis` (float3), `coneHalfAngle` (float)
- T6a.3.1: `HiZPyramid::GetTexture()` → `TextureHandle` (R32F mip chain), `GetMipCount()` → `uint32_t`
- T6a.4.1: `SceneBuffer::GetGpuBuffer()` → `BufferHandle` containing `GpuInstance[]`
- T6a.0.1: `DrawMeshTasksIndirectCount()` on `ICommandBuffer`, mesh shader `GraphicsPipelineDesc`

### Technical Direction
- **Two-level cull in task shader**: (1) per-instance: test `GpuInstance.boundingSphere` against 6 frustum planes + HiZ sample at projected screen rect. If visible, iterate instance's meshlets. (2) per-meshlet: test `MeshletDescriptor.boundingSphere` against frustum + HiZ. Test normal cone backface: `dot(coneAxis, viewDir) > coneHalfAngle → fully backfacing → skip`. Emit surviving meshlets via `SetMeshOutputCounts()`.
- **Subgroup ops**: `WaveBallot` for wave-level early-out when all threads in wave reject. `WavePrefixSum` for compact meshlet indices in `TaskPayload`.
- **HiZ occlusion test**: project bounding sphere to screen rect → compute mip level from rect area → sample HiZ at that mip → compare sphere nearest depth (Reverse-Z: sphere max Z) against HiZ value.
- **`dp4a` quantized normal cones** (roadmap L1740, high-value wide-coverage optimization): `VK_KHR_shader_integer_dot_product` (`dp4a`) for INT8 dot product on normal cone axis vs quantized view direction. Reduces normal cone test from 3 FMA (float) to 1 `dp4a` (int8). Hardware coverage: RDNA2+, Ampere+, Intel Arc — all Tier1 targets. Scalar float fallback if `ShaderIntegerDotProduct` feature unavailable. `MeshletDescriptor.coneAxis` stored as `int8_t[4]` quantized (±127 range) alongside float version. ~2 ALU savings per meshlet × 1M meshlets = measurable.
- **DepthPrePass** (per `rendering-pipeline-architecture.md` §4.1 Pass #1): the task+mesh pipeline also serves as DepthPrePass when configured with a depth-only PSO (depthWrite=true, colorWrite=none, no VisBuffer attachment). In Phase 6a render graph: (1) DepthPrePass (depth-only task→mesh) → (2) HiZ Build → (3) GPU Cull with HiZ → (4) VisBuffer geometry pass (task→mesh with VisBuffer attachment). First frame bootstraps from Phase 3a GBuffer depth. `GpuCullPipeline::BuildPipeline()` accepts a `depthOnly` flag for this variant.
- **Adaptive near-plane** (roadmap L1740, optional): during instance-level cull, compute tight near plane from visible geometry AABB min-Z via parallel reduction (`subgroupMin` + atomic). Push constant updated next frame. Cost: ~0.02ms. Improves far-field depth precision. Implemented as an optional Step.
- **Slang task shader entry**: `[shader("amplification")] void taskMain(...)` with `[numthreads(32, 1, 1)]`.

## Files

| Action | Path | Exposure | Ref Heat | Notes |
|--------|------|----------|----------|-------|
| Create | `shaders/vgeo/task_cull.slang` | **shared** | **H** | Task shader (amplification stage) |
| Create | `include/miki/vgeo/GpuCullTypes.h` | **public** | **M** | CullUniforms, TaskPayload |
| Create | `include/miki/vgeo/GpuCullPipeline.h` | **public** | **M** | Pipeline builder |
| Create | `src/miki/vgeo/GpuCullPipeline.cpp` | internal | L | Pipeline creation impl |
| Create | `tests/unit/test_gpu_cull.cpp` | internal | L | Tests |

## Steps

- [ ] **Step 1**: Define GpuCullTypes.h + GpuCullPipeline.h (heat M)

      **Signatures** (`GpuCullTypes.h`):

      | Symbol | Fields | Attrs |
      |--------|--------|-------|
      | `CullUniforms` | `{ frustumPlanes[6]:float4, hizMipCount:u32, nearPlane:f32, farPlane:f32, cameraPos:float3, _pad:f32 }` | `alignas(16)`, ≤128B push constant |
      | `TaskPayload` | `{ meshletIndices[32]:u32, count:u32 }` | 132B, matches Slang `TaskPayload` |

      **Signatures** (`GpuCullPipeline.h`):

      | Symbol | Signature | Attrs |
      |--------|-----------|-------|
      | `GpuCullPipeline::Create` | `(IDevice&, SlangCompiler&) -> Result<GpuCullPipeline>` | `[[nodiscard]]` static |
      | `GpuCullPipeline::BuildPipeline` | `(GpuCullPipelineDesc const&) -> Result<PipelineHandle>` | `[[nodiscard]]` |
      | `GpuCullPipeline::GetPipelineLayout` | `() const noexcept -> PipelineLayoutHandle` | `[[nodiscard]]` |

      `[verify: compile]`

- [ ] **Step 2**: Implement task_cull.slang
      Slang task shader with 2-level cull (instance + meshlet), HiZ occlusion, normal cone backface, subgroup compaction.
      `[verify: compile]`

- [ ] **Step 3**: Implement GpuCullPipeline.cpp — pipeline creation with task+mesh stages
      `[verify: compile]`

- [ ] **Step 4**: Unit tests (CPU reference comparison via readback of indirect args)
      `[verify: test]`

## Tests

| Test Name | Category | Validates | Steps |
|-----------|----------|-----------|-------|
| `TEST(GpuCull, CreatePipelineValid)` | Positive | Task+mesh pipeline creates on Tier1 (GTEST_SKIP no-GPU) | 3,4 |
| `TEST(GpuCull, FrustumCull_AllVisible)` | Positive | All instances inside frustum → all emitted | 2-4 |
| `TEST(GpuCull, FrustumCull_AllCulled)` | Positive | All outside frustum → 0 emitted | 2-4 |
| `TEST(GpuCull, FrustumCull_Partial)` | Positive | 50% inside → correct subset emitted | 2-4 |
| `TEST(GpuCull, OcclusionCull_FullyOccluded)` | Positive | Instance behind opaque wall → culled | 2-4 |
| `TEST(GpuCull, NormalConeCull_BackFacing)` | Positive | All normals face away → culled | 2-4 |
| `TEST(GpuCull, NormalConeCull_Degenerate)` | Boundary | coneHalfAngle >= π → never culled | 2-4 |
| `TEST(GpuCull, CullUniforms_Layout)` | Positive | sizeof ≤ 128, correct field offsets | 1,4 |
| `TEST(GpuCull, Perf_100K_Under500us)` | Benchmark | 100K instances cull < 0.5ms | 2-4 |
| `TEST(GpuCull, EndToEnd_CullAndDispatch)` | **Integration** | Upload scene → build HiZ → cull → verify indirect args count | 2-4 |

## Design Decisions

*(Fill during implementation.)*

## Implementation Notes

*(Fill during implementation.)*
