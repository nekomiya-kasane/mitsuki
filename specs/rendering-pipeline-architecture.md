# miki Rendering Pipeline Architecture

> **Audience**: Engine team (30 engineers). Architecture reference — not a tutorial.
> **Scope**: Complete GPU rendering pipeline design — pass graph, shader stages, data structures, tier differentiation, performance budgets. Application-layer concerns (camera controller UX, animation keyframe evaluation, UI layout, collaboration protocol) are out of scope; only their **GPU-side data contracts** are documented in §21.
> **Status**: Design blueprint. Implementation phases noted where relevant.

---

## 1. Overview & Tier Strategy

### 1.1 Core Architecture

miki is a GPU-native CAD/CAE rendering engine. The rendering pipeline achieves **zero CPU draw calls** in steady state:

- **Visibility Buffer** decouples geometry from materials -- 1 PSO for all opaque geometry
- **Task/Mesh shaders** perform hierarchical culling (instance -> meshlet -> triangle)
- **RenderGraph** orchestrates all passes as a DAG with automatic barrier insertion and transient resource aliasing
- **3-bucket macro-binning** classifies all visible geometry into exactly 3 render paths per frame
- **Bindless + BDA** eliminates per-draw descriptor updates
- **Reverse-Z** [1,0] depth for precision at distance (near=1.0, far=0.0, `GreaterOrEqual` compare)
- **Clustered light culling** supports 4096 simultaneous lights (Tier1)
- **Layered DSPBR BSDF** -- full Dassault Enterprise PBR (8 material layers, conditional skip)

Target Architecture (11 Layers)

```
Layer 11 ─ Application & Integration
           SDK (MikiEngine/MikiView), Reference UI (miki_editor), Cloud Render
           Collaborative Viewer, LLM Agent Adapter, Multi-Window, HeadlessDevice
Layer 10 ─ Plugin / Extension
           IPlugin (lifecycle: Discover→Load→Init→Activate→Deactivate→Unload)
           PluginRegistry, PluginManifest (JSON), versioned API surface
           Extension points: ITranslator, IKernel, IUiBridge, custom RenderGraph passes
           Vertical industry plugins: Piping, HVAC, Electrical, Shipbuilding (post-1.1)
Layer 9  ─ Interactive Tools
           Gizmo, Compass, Snap, CommandBus, OpHistory, PreviewManager
           SelectionOutline, RichTextInput, MeshEditor, TopoEditEngine
Layer 8b ─ CAE / DFM / PointCloud / Import
           CAE Vis (FEM/Scalar/Vector/Contour/Streamline/Deformation/Tensor)
           PointCloud (Loader/Renderer/ICP/ScanToCAD), PointCloudFilter
           Import (STEP/JT/glTF/3DXML), GPU Tess, Boolean Preview
           DFM (DraftAngle/WallThickness/Undercut/Curvature/Interference/Distance/MassProps)
Layer 8a ─ CAD Core
           CadScene, LayerStack, ConfigurationManager, DisplayStyles
           GPU HLR, SectionPlane, SectionVolume, OIT, Explode, RTAO
           PMI Renderer, DrawingProjector, 2D Markup
Layer 7  ─ Rendering Layer Stack
           Scene / Preview / Overlay / HUD — per-layer render graph
Layer 6  ─ GPU-Driven Geometry
           Task/Mesh Amplification, VisBuffer, ClusterDAG, Radix Sort
           GPU Scene Submission, Persistent Compute, Nanite-grade LOD
           GPU QEM (Mesh Simplification), Meshlet Compression, Cooperative Matrix
Layer 5  ─ Core Rendering
           RenderGraph (conditional + async), Deferred, PBR, VSM Shadows
           TAA/FSR/DLSS, FXAA/MSAA, GTAO/SSAO, VRS, BackgroundMode
           Material Graph, Shader Permutation Cache, Hot-Reload, SlangCompiler
           ReSTIR DI/GI, DDGI, Neural Denoiser (1.1 optional quality tier)
Layer 4  ─ Scene & Data
           ECS, SpatialIndex (BVH+Octree), RTE, IUiBridge
           IKernel (pluggable, OCCT=ref impl), IGpuGeometry
Layer 3  ─ Resource & Memory
           Bindless (descriptor buffer), BDA, SlotMap, StagingRing
           ChunkLoader, MemBudget, Residency Feedback
Layer 2  ─ RHI
           IDevice, ICommandBuffer, VulkanDevice, D3D12Device
           OpenGlDevice, WebGpuDevice, MockDevice, ImGuiBackend
Layer 1  ─ Foundation
           Math, GFX Types, Coca Runtime, ErrorCode
           Debug & Profiling, Telemetry, StructuredLogger
```

### 1.2 Two Render Paths

| Path       | Factory                 | Backends                                       | Geometry            | Shadows         | AO          | AA             | OIT         |
| ---------- | ----------------------- | ---------------------------------------------- | ------------------- | --------------- | ----------- | -------------- | ----------- |
| **Main**   | `MainPipelineFactory`   | Vulkan (Tier1), D3D12 (Tier1)                  | Task/Mesh           | VSM 16K-sq      | GTAO / RTAO | TAA + FSR/DLSS | Linked-List |
| **Compat** | `CompatPipelineFactory` | Compat (Tier2), WebGPU (Tier3), OpenGL (Tier4) | Vertex + MDI / draw | CSM 2-4 cascade | SSAO        | FXAA / MSAA    | Weighted    |

Selection at startup via `GpuCapabilityProfile`. **No `if (compat)` branches** -- `IPipelineFactory` virtualizes all pass creation.

### 1.3 Tier Feature Matrix

| Feature          |           Vulkan (T1)            |      D3D12 (T1)       |        Compat (T2)         |        WebGPU (T3)         |         OpenGL (T4)         |
| ---------------- | :------------------------------: | :-------------------: | :------------------------: | :------------------------: | :-------------------------: |
| Geometry         |            Task/Mesh             |      Mesh Shader      |         Vertex+MDI         |        Vertex+draw         |         Vertex+MDI          |
| MDI              |     DrawIndexedIndirectCount     | ExecuteIndirect+count | DrawIndexedIndirect (loop) | DrawIndexedIndirect (loop) | glMultiDrawElementsIndirect |
| Shadow           |            VSM 16K-sq            |      VSM 16K-sq       |       CSM 4-cascade        |       CSM 2-cascade        |        CSM 4-cascade        |
| AO               |           GTAO / RTAO            |      GTAO / RTAO      |            SSAO            |       SSAO 8-sample        |            SSAO             |
| AA               |            TAA + FSR             |       TAA + FSR       |       FXAA / MSAA 4x       |         FXAA only          |       FXAA / MSAA 4x        |
| OIT              |           Linked-list            |      Linked-list      |          Weighted          |          Weighted          |          Weighted           |
| Pick             |           RT ray query           |       DXR pick        |          CPU BVH           |        CPU BVH WASM        |           CPU BVH           |
| VRS              |               Yes                |          Yes          |             No             |             No             |             No              |
| Async compute    |        Timeline semaphore        |         Fence         |             No             |             No             |             No              |
| Descriptor model |        Descriptor buffer         |    Descriptor heap    |      Descriptor sets       |        Bind groups         |          UBO/SSBO           |
| Push constants   |          Native (256B)           |    Root constants     |           Native           |        256B UBO emu        |          128B UBO           |
| Float64 compute  |              Native              |        Native         |           Native           |       DS emu (2xf32)       |   GL_ARB_gpu_shader_fp64    |
| Shader IR        |              SPIR-V              |         DXIL          |           SPIR-V           |            WGSL            |          GLSL 4.30          |
| Max scene        |        2B tri / 10M inst         |   2B tri / 10M inst   |     100M tri / 1M inst     |    10M tri / 500K inst     |     100M tri / 1M inst      |
| Clustered lights |               4096               |         4096          |      256 (CPU sorted)      |          64 (UBO)          |      256 (CPU sorted)       |
| RT features      | RTAO+Reflect+Shadow+GI+PathTrace |         Same          |            None            |            None            |            None             |
| SSR              |               Yes                |          Yes          |             No             |             No             |             No              |
| Bloom/DoF/MB     |               All                |          All          |         Bloom only         |         Bloom only         |         Bloom only          |
| Stages           |          Task+Mesh+Frag          |       AS+MS+PS        |         Vert+Frag          |         Vert+Frag          |          Vert+Frag          |

Tessellation and Geometry shaders **intentionally excluded** -- GPU surface subdivision uses compute (Phase 7b), primitive amplification uses mesh shader (T1) or vertex index tricks (T2/3/4).

### 1.4 Tier Fallback Chains

```
Shadows:     VSM 16K-sq -> CSM 4-cascade -> CSM 2-cascade
AO:          RTAO -> GTAO (half-res) -> SSAO (8-16 samples)
AA:          DLSS 3.5 -> FSR 3.0 -> TAA -> FXAA -> MSAA 4x
OIT:         LL-OIT (exact) -> Weighted OIT
Geometry:    Task/Mesh -> Vertex+MDI -> Vertex+draw
Reflections: RT Reflect -> SSR (Hi-Z) -> IBL cubemap
GI:          RT GI (1 bounce) -> IBL ambient
Shadows/pt:  RT Shadow -> VSM -> CSM
```

### 1.5 Pipeline Creation Architecture

**Two-layer design** — `DeviceBase<Impl>` (CRTP, zero-overhead) owns raw PSO creation, `IPipelineFactory` is a convenience wrapper:

```cpp
// Layer 1: DeviceBase<Impl> — unified PSO creation (CRTP, all backends)
struct GraphicsPipelineDesc {
    ShaderModuleDesc vertexShader, fragmentShader;
    ShaderModuleDesc taskShader, meshShader;  // Phase 6a: Tier1 mesh shader path
    VertexLayout vertexLayout;                // ignored when meshShader is set
    bool depthTest, depthWrite; CompareOp depthCompareOp;
    CullMode cullMode; PolygonMode polygonMode; BlendState colorBlend;
    std::array<Format, 8> colorFormats; uint32_t colorFormatCount; Format depthFormat;
    PipelineLayoutHandle pipelineLayout;
    constexpr bool IsMeshShaderPipeline() const noexcept;
};
auto CreateGraphicsPipeline(const GraphicsPipelineDesc&) -> Result<PipelineHandle>;
auto CreateComputePipeline(const ComputePipelineDesc&)   -> Result<PipelineHandle>;

// Layer 2: IPipelineFactory — thin wrapper, forwards to DeviceHandle
using GeometryPassDesc = GraphicsPipelineDesc;  // alias, not separate struct
class IPipelineFactory {
    static auto Create(DeviceHandle&) -> unique_ptr<IPipelineFactory>;
    virtual auto CreateGeometryPass(const GeometryPassDesc& d) -> Result<PipelineHandle> {
        return device_.Dispatch([&](auto& dev) { return dev.CreateGraphicsPipeline(d); });
    }
    virtual auto CreateShadowPass(const ShadowPassDesc&)  -> Result<PipelineHandle> = 0;
    virtual auto CreateOITPass(const OITPassDesc&)         -> Result<PipelineHandle> = 0;
    virtual auto CreateAOPass(const AOPassDesc&)           -> Result<PipelineHandle> = 0;
    virtual auto CreateAAPass(const AAPassDesc&)           -> Result<PipelineHandle> = 0;
    virtual auto CreatePickPass(const PickPassDesc&)       -> Result<PipelineHandle> = 0;
    virtual auto CreateHLRPass(const HLRPassDesc&)         -> Result<PipelineHandle> = 0;
    virtual auto GetTier() const noexcept -> CapabilityTier = 0;
};
```

`MainPipelineFactory` (Tier1) and `CompatPipelineFactory` (Tier2/3/4) both forward `CreateGeometryPass` to `DeviceHandle::Dispatch()` → `DeviceBase<Impl>::CreateGraphicsPipeline`. Pass-specific methods (Shadow, AO, AA, etc.) remain for tier-differentiated algorithm selection (VSM vs CSM, GTAO vs SSAO). **All PSOs pre-built at init** -- zero runtime compilation on mode switch.

Pipeline config types (`CullMode`, `PolygonMode`, `CompareOp`, `BlendState`, `VertexLayout`, etc.) live in `RhiDescriptors.h` alongside `GraphicsPipelineDesc`.

### 1.6 Descriptor Strategy

The RHI abstracts four distinct descriptor binding strategies behind a unified `DeviceBase<Impl>` / `CommandBufferBase<Impl>` CRTP interface (type-erased via `DeviceHandle` / `CommandListHandle` at RenderGraph boundary):

| Backend            | Primary Strategy                                                                                                           | Fallback                                                                                                      |
| ------------------ | -------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| Vulkan (T1)        | `VK_EXT_descriptor_buffer` — GPU-visible buffer holding descriptor data directly. Zero descriptor set allocation overhead. | Push descriptors (`VK_KHR_push_descriptor`) for per-draw data; traditional descriptor sets for compatibility. |
| D3D12 (T1)         | Root descriptor tables + CBV/SRV/UAV descriptor heaps. Shader-visible heap copied from staging heap per-frame.             | Root constants (32-bit values) for push-constant emulation.                                                   |
| Vulkan Compat (T2) | Traditional `VkDescriptorSet` with auto-growing `VkDescriptorPool`.                                                        | Same.                                                                                                         |
| WebGPU (T3)        | `GPUBindGroup` created per material/pass. Immutable once created.                                                          | N/A — WebGPU has no push descriptor equivalent.                                                               |
| OpenGL (T4)        | Direct `glBindBufferRange` / `glBindTextureUnit` / `glBindSampler` calls. UBO/SSBO binding points.                         | N/A.                                                                                                          |

**BindlessTable** (`miki::rhi::BindlessTable`) provides a unified bindless resource indexing layer on top of these strategies. All textures and buffers registered via `BindlessTable::Register()` receive a stable `uint32_t` index usable in shaders via `BDA + index` (Vulkan/D3D12) or explicit binding (OpenGL/WebGPU).

**IPipelineFactory** is a thin convenience wrapper over `DeviceHandle::Dispatch()` → `CreateGraphicsPipeline` / `CreateComputePipeline`. It does **not** own pipeline state — it only selects tier-appropriate algorithms (VSM vs CSM, GTAO vs SSAO) and forwards to the device. `MainPipelineFactory` (Tier1) and `CompatPipelineFactory` (Tier2/3/4) are the two concrete implementations.

---

## 2. RenderGraph Infrastructure

### 2.1 Architecture

> **Full specification**: See `04-render-graph.md` for the complete RenderGraph design (10-stage compilation pipeline, split barrier model, transient aliasing, graph caching, render pass merging, conditional execution, PSO miss handling, plugin extension system, and debugging/profiling tools).

All GPU work is expressed as **RenderGraph passes** — nodes in a DAG. The Builder→Compiler→Executor pipeline automatically handles barrier insertion, transient resource aliasing (30-50% RT VRAM savings), conditional pass culling (zero-cost DCE), structural hash caching, and render pass merging (subpass consolidation for tile-based GPUs). Each LayerStack layer owns its own RenderGraph instance.

### 2.2 Backend-Specific Execution

| Backend | Render Pass Model                            | Barrier Model                               | Async Compute                          |
| ------- | -------------------------------------------- | ------------------------------------------- | -------------------------------------- |
| Vulkan  | Dynamic rendering (VK_KHR_dynamic_rendering) | vkCmdPipelineBarrier2                       | Timeline semaphore                     |
| D3D12   | BeginRenderPass/EndRenderPass                | Enhanced Barriers + Fence Barriers (1.719+) | ID3D12Fence (timeline) + compute queue |
| OpenGL  | FBO bind/unbind                              | glMemoryBarrier                             | N/A (single queue)                     |
| WebGPU  | Render pass encoder                          | Implicit (Dawn)                             | N/A (single queue)                     |

### 2.3 Resource Lifetime

```
Create (CPU) -> Upload (StagingRing) -> Register (BindlessTable) -> Use (GPU shader)
                                                                       |
                                      ResidencyFeedback <- GPU counters (ReadbackRing)
                                                                       |
                                      MemoryBudget -> LRU Evict -> Destroy (deferred, framesInFlight latency; see 03-sync.md §6)

GPU -> CPU readback path (timestamps, shader printf, breadcrumbs, telemetry):
  GPU write -> CmdCopyBuffer -> ReadbackRing (GpuToCpu, persistent-mapped) -> CPU read (after timeline wait at BeginFrame on T1; fence on T2; see 03-sync.md §7)
```

> **Cross-ref**: Frame pipelining (BeginFrame/EndFrame semantics, CPU wait formula, split-submit, multi-window frame pacing) is specified in `03-sync.md` §4-§9.

---

## 3. Complete Pass Reference

88 passes organized by pipeline stage. Passes #1-#69: core pipeline (always available). Passes #70-#88: extended passes (conditional RG nodes, zero cost when inactive -- see S3.3). Each row: pass name, shader type, tier availability, inputs, outputs, GPU time budget.

| #   | Pass                                                                                                         | Type                              | Tier    | Input                                                        | Output                                                                                           | Budget                 |
| --- | ------------------------------------------------------------------------------------------------------------ | --------------------------------- | ------- | ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------ | ---------------------- |
| 1   | DepthPrePass + HiZ                                                                                           | Graphics                          | All     | SceneBuffer                                                  | D32F (Reverse-Z) + HiZ mip pyramid                                                               | <0.5ms                 |
| 2   | GPU Culling                                                                                                  | Compute                           | All     | HiZ, SceneBuffer, BVH                                        | Visible instance/meshlet list                                                                    | <0.3ms                 |
| 3   | Light Cluster Assign                                                                                         | Compute                           | T1      | GpuLight[], depth slices                                     | 3D froxel cluster grid                                                                           | <0.1ms @1K             |
| 4   | Macro-Binning                                                                                                | Compute                           | All     | Visible list, GpuInstance.flags                              | 3 bucket append lists                                                                            | <0.1ms                 |
| 5   | Task Shader                                                                                                  | Graphics (Amp)                    | T1      | Visible list                                                 | Meshlet workgroups                                                                               | --                     |
| 6   | Mesh Shader                                                                                                  | Graphics (Mesh)                   | T1      | Meshlet descriptors (BDA)                                    | Triangles -> VisBuffer                                                                           | --                     |
|     | _Note: #5+#6+#9 are stages of a single graphics pipeline (Task→Mesh→VisBuffer), not independent dispatches._ |                                   |         |                                                              |                                                                                                  |                        |
| 7   | SW Rasterizer                                                                                                | Compute                           | T1 opt  | Small tri (<4px-sq)                                          | uint64 SSBO atomicMax -> resolve -> VisBuffer                                                    | <0.3ms                 |
| 8   | Vertex+MDI                                                                                                   | Graphics (Vert)                   | T2/3/4  | VB/IB                                                        | Triangles -> GBuffer                                                                             | --                     |
| 9   | VisBuffer Write                                                                                              | Graphics                          | T1      | Triangles                                                    | R32G32_UINT (instId+primId)                                                                      | <1ms                   |
| 10  | Material Resolve                                                                                             | Compute                           | T1 only | VisBuffer, BindlessTable, MaterialParameterBlock[]           | GBuffer (DSPBR layered)                                                                          | <1ms                   |
| 11  | GBuffer Forward                                                                                              | Graphics                          | T2/3/4  | Triangles, materials                                         | Albedo+Normal+MetalRough+Motion+Depth (PBR eval in fragment shader, replaces Pass #10 on compat) | <2ms                   |
| 12  | VSM Render                                                                                                   | Graphics (Mesh)                   | T1      | Shadow casters, dirty pages                                  | 16K-sq virtual page depth                                                                        | <2ms                   |
| 13  | CSM Render                                                                                                   | Graphics (Vert)                   | T2/3/4  | Shadow casters                                               | 2-4 cascade depth                                                                                | <2ms                   |
| 14  | Shadow Atlas                                                                                                 | Graphics                          | T1/T2   | Point/spot casters                                           | 8K-sq/4K-sq atlas tiles                                                                          | <1ms                   |
| 15  | GTAO                                                                                                         | Compute                           | T1/T2   | Depth (half-res)                                             | R8 AO                                                                                            | <1ms                   |
| 16  | SSAO                                                                                                         | Fragment                          | T3/T4   | Depth                                                        | R8 AO                                                                                            | <1ms                   |
| 17  | RTAO                                                                                                         | Compute (ray query)               | T1      | BLAS/TLAS, depth                                             | R8 AO (1spp+temporal)                                                                            | <2ms                   |
| 18  | Deferred Resolve                                                                                             | Compute/Fragment                  | All     | GBuffer, shadows, AO, GpuLight[], clusters, IBL              | HDR RGBA16F                                                                                      | <1ms                   |
| 19  | IBL Precompute                                                                                               | Compute (one-time)                | All     | HDRI equirect                                                | Cubemap+SH L2+BRDF LUT+specular 5-mip                                                            | <10ms                  |
| 20  | RT Reflections                                                                                               | Compute (ray query)               | T1      | BLAS/TLAS, GBuffer                                           | RGBA16F reflection                                                                               | <2ms                   |
| 21  | RT Shadows                                                                                                   | Compute (ray query)               | T1      | BLAS/TLAS, GpuLight[]                                        | Per-light shadow mask                                                                            | <1ms/light             |
| 22  | RT GI                                                                                                        | Compute (ray query)               | T1      | BLAS/TLAS (half-res)                                         | Indirect diffuse (SH cache)                                                                      | <3ms                   |
| 23  | Path Tracer                                                                                                  | Compute (ray query)               | T1      | BLAS/TLAS, MaterialParameterBlock[]                          | Progressive RGBA32F                                                                              | ~50ms/spp              |
| 24  | Denoiser                                                                                                     | Compute/External                  | T1      | Noisy RT + albedo + normal                                   | Denoised RGBA16F                                                                                 | <10ms                  |
| 25  | LL-OIT Insert                                                                                                | Fragment+Compute                  | T1/T2   | Transparent geom                                             | Linked-list node pool (16M, 256MB)                                                               | <1ms                   |
| 26  | LL-OIT Resolve                                                                                               | Compute                           | T1/T2   | Node pool + head ptrs                                        | Sorted composited RGBA16F                                                                        | <1ms                   |
| 27  | Weighted OIT                                                                                                 | Fragment                          | T3/T4   | Transparent geom                                             | Blended RGBA16F                                                                                  | <1ms                   |
| 28  | HLR Classify                                                                                                 | Compute                           | All     | Normals, adjacency SSBO (BDA)                                | `EdgeDescriptor` 32B SSBO `{v0[3],packed,v1[3],instanceId}`                                      | <1ms @10M              |
| 29  | HLR Visibility                                                                                               | Compute                           | All     | EdgeBuffer, HiZ                                              | Visible+Hidden edge buffers (paramT intervals)                                                   | <1.5ms @10M            |
| 30  | HLR Render                                                                                                   | Graphics (Task/Mesh or Vert)      | All     | Edge buffers, LinePattern SSBO                               | SDF AA edge color RGBA8                                                                          | <1.5ms @5M             |
| 31  | Section Plane/Volume                                                                                         | Fragment (stencil)                | All     | Scene depth, clipPlaneMask                                   | Clip + cap + ISO 128 hatch                                                                       | <0.5ms                 |
| 32  | Ray Pick                                                                                                     | Compute (ray query)/CPU           | All     | BLAS/TLAS or BVH + request                                   | HitBuffer                                                                                        | <0.5ms T1              |
| 33  | Lasso Pick                                                                                                   | Compute                           | All     | Polygon SSBO + VisBuffer                                     | R8 stencil mask -> entity set                                                                    | <1.5ms @4K             |
| 34  | Boolean Preview                                                                                              | Compute                           | T1      | Depth layers (N=8)                                           | CSG composite                                                                                    | <16ms                  |
| 35  | Draft Angle                                                                                                  | Compute                           | All     | Normals, pull dir                                            | Per-face angle -> color map                                                                      | <1ms @1M               |
| 36  | GPU Measurement                                                                                              | Compute (ray query)               | All     | BDA geom (float64/DS)                                        | Distance/angle/thickness                                                                         | <2ms                   |
| 37  | Explode Transform                                                                                            | Compute                           | All     | Assembly hierarchy                                           | Per-instance transforms                                                                          | <0.1ms                 |
| 38  | FEM Mesh                                                                                                     | Graphics                          | All     | Element mesh + scalars                                       | Colored elements (+ element shrink, quality coloring)                                            | <1ms                   |
| 39  | Scalar/Vector Field                                                                                          | Graphics (instanced)              | All     | Scalar/vector arrays                                         | Color-mapped mesh / arrow glyphs                                                                 | <1ms                   |
| 40  | Streamline                                                                                                   | Compute+Graphics                  | All     | Vector field                                                 | RK4 tube geometry                                                                                | <2ms                   |
| 41  | Isosurface                                                                                                   | Compute                           | All     | Scalar volume                                                | Marching Cubes mesh                                                                              | <5ms                   |
| 42  | Tensor Glyph                                                                                                 | Graphics (instanced)              | All     | Tensor data                                                  | Stress ellipsoids                                                                                | <2ms                   |
| 43  | Point Cloud Splat                                                                                            | Graphics (Task/Mesh or instanced) | All     | GpuPoint[] 16B/pt, octree LOD                                | Disc SDF quads + depth                                                                           | <2ms @10M              |
| 44  | Eye-Dome Lighting                                                                                            | Compute                           | All     | Point cloud depth                                            | Occlusion modulation                                                                             | <0.3ms                 |
| 45  | PMI Render                                                                                                   | Graphics (instanced)              | All     | PmiAnnotation[], MSDF atlas, LeaderLine SSBO                 | Text+leaders+symbols                                                                             | <0.1ms @1K             |
| 46  | Analysis Overlay                                                                                             | Fragment (fullscreen)             | All     | GBuffer normals                                              | Zebra/iso/curv/draft/deviation/thickness/interference/dihedral                                   | <0.2ms                 |
| 47  | Color Bar / Legend                                                                                           | Fragment                          | All     | Color map range                                              | Screen-space bar + MSDF labels                                                                   | <0.05ms                |
| 48  | SSR                                                                                                          | Compute                           | T1/T2   | HDR (#18), depth (#1), GBuffer normals+roughness (#10)       | RGBA16F reflection (half-res+upsample)                                                           | <1.5ms                 |
| 49  | Bloom                                                                                                        | Compute/Fragment                  | All     | HDR >1.0 lum                                                 | 6-level Gaussian                                                                                 | <0.5ms T1, <0.8ms T3/4 |
| 50  | DoF                                                                                                          | Compute                           | T1/T2   | HDR (post-Bloom), depth (#1), push(aperture, focalLen)       | Gather bokeh (half-res, 16 samp)                                                                 | <1.5ms                 |
| 51  | Motion Blur                                                                                                  | Compute                           | T1/T2   | HDR (post-DoF), GBuffer.RT2 motion vectors (#10), depth (#1) | Directional blur (McGuire 2012)                                                                  | <1.0ms                 |
| 52  | Tone Mapping                                                                                                 | Fragment                          | All     | HDR                                                          | LDR (ACES/AgX/Khronos/Reinhard/Linear + vignette + CA + auto-exposure)                           | <0.2ms                 |
| 53  | TAA                                                                                                          | Compute                           | T1/T2   | LDR (post-ToneMap), GBuffer.RT2 motion (#10), TAA_History    | AA RGBA16F (Halton jitter, YCoCg clamp)                                                          | <0.5ms                 |
| 54  | Temporal Upscale                                                                                             | Compute                           | T1/T2   | 67% res                                                      | Full-res (FSR 3.0 / DLSS 3.5)                                                                    | <1ms                   |
| 55  | FXAA                                                                                                         | Fragment                          | T3/T4   | LDR (luma in alpha)                                          | AA RGBA8                                                                                         | <0.5ms                 |
| 56  | CAS Sharpen                                                                                                  | Compute                           | All     | Post-AA color                                                | AMD FidelityFX CAS                                                                               | <0.2ms                 |
| 57  | Color Grading                                                                                                | Fragment                          | All     | LDR                                                          | 3D LUT (32-cube RGBA8) transformed                                                               | <0.1ms                 |
| 58  | Outline                                                                                                      | Fragment                          | All     | Depth (#1) + GBuffer normals (#10) + push(outlineColor)      | Sobel edge mask -> outline RGBA8 -> Compositor (#65)                                             | <0.2ms                 |
| 59  | VRS Image                                                                                                    | Compute                           | T1      | Luminance gradient                                           | VRS rate image (16x16 tiles)                                                                     | <0.2ms                 |
| 60  | Gizmo Render                                                                                                 | Graphics (instanced)              | All     | GizmoState SSBO, GizmoMeshPool                               | Unlit colored handles (Overlay)                                                                  | <0.1ms                 |
| 61  | Ground Grid                                                                                                  | Fragment (fullscreen)             | All     | Camera UBO                                                   | Adaptive grid (fwidth AA, distance fade)                                                         | <0.2ms                 |
| 62  | ViewCube                                                                                                     | Graphics                          | All     | Camera orient                                                | Mini cube (Overlay corner)                                                                       | <0.05ms                |
| 63  | Snap Indicators                                                                                              | Graphics (instanced)              | All     | Snap point SSBO                                              | SDF dots/crosses                                                                                 | <0.01ms                |
| 64  | Measurement Viz                                                                                              | Graphics (instanced)              | All     | MeasurementResult[]                                          | SDF leaders + MSDF text                                                                          | <0.1ms                 |
| 65  | LayerStack Compositor                                                                                        | Fragment                          | All     | 6 layer color+depth                                          | Final composited framebuffer                                                                     | <0.2ms                 |
| 66  | Offscreen Render                                                                                             | (full pipeline)                   | All     | Scene + view def                                             | Tile-based hi-res (up to 16K-sq)                                                                 | Nx frame               |
| 67  | HLR->SVG/PDF                                                                                                 | CPU readback                      | All     | EdgeBuffer GPU->CPU                                          | 2D vector paths                                                                                  | <5s @1M                |
| 68  | 3D->2D Drawing                                                                                               | HLR+projection                    | All     | 3D model + ortho view                                        | 2D drawing (section/detail/aux/break)                                                            | <15s 6-view            |
| 69  | XR Stereo                                                                                                    | full pipeline, multiview          | T1      | OpenXR swapchain L+R                                         | 2x width VisBuffer+resolve+post                                                                  | <11.1ms                |

| 70 | Selection Outline (JFA) | Compute (3-pass) | All | VisBuffer selection mask + depth | JFA distance field -> outline color (Overlay) | <1.2ms @4K |
| 71 | GPU Interference | Compute (ray query) | T1 | BLAS/TLAS, body pair list | Per-face interference flag + volume | <5ms |
| 72 | GPU Curvature | Compute (quadric fit) | All | Vertex positions + adjacency | Per-vertex Gaussian/mean curvature SSBO | <1ms @1M vtx |
| 73 | Contour Plot (Isolines) | Compute (marching-sq-on-tri) | All | Scalar field + threshold(s) | Isoline polyline SSBO -> SDF line render | <1ms |
| 74 | Probe Query | Compute + readback | All | Scalar/vector field + probe position | Interpolated value(s) -> CPU | <0.1ms |
| 75 | Result Compare (A-B) | Compute | All | Two scalar arrays | Per-node diff scalar -> color map | <0.5ms |
| 76 | Trace Curves | Compute + Graphics | All | Track point ring buffer SSBO | Tube mesh (Frenet frame) -> Overlay | <0.5ms |
| 77 | Sketch Renderer | Graphics (SDF) | All | Sketch entity SSBO (lines/arcs/splines) | SDF anti-aliased 2D sketch on sketch plane | <0.3ms |
| 78 | Polyhedral CFD Mesh | Compute + Graphics | All | Polyhedral connectivity + scalars | Fan-triangulated colored elements | <2ms @500K cells |
| 79 | Surface LIC | Compute | All | Vector field + surface mesh | LIC texture (line integral convolution) | <3ms |
| 80 | Pathlines/Streaklines | Compute + Graphics | All | Time-varying vector field + seed pts | Time-integrated tube geometry | <3ms |
| 81 | Decal Projector | Fragment | All | Decal texture + OBB projection matrix | Projected texture on scene surfaces | <0.2ms @64 decals |
| 82 | GPU QEM Simplification | Compute | T1 | Meshlet vertex/index data | Simplified LOD mesh | <50ms (offline) |
| 83 | ReSTIR DI | Compute (ray query) | T1 | BLAS/TLAS, GpuLight[], GBuffer | Reservoir-sampled direct illumination | <4ms @1080p |
| 84 | ReSTIR GI | Compute (ray query) | T1 | BLAS/TLAS, GBuffer | Reservoir-reused indirect illumination | <6ms @1080p |
| 85 | DDGI Probes | Compute (ray trace) + Fragment (sample) | T1 | BLAS/TLAS, probe grid | Irradiance + visibility octahedral maps | <2ms @1K probes |
| 86 | GPU Debug Vis | Graphics (instanced) | All | Meshlet/BVH metadata SSBO | Heatmap overlay (Overlay layer) | <0.5ms |
| 87 | Point Cloud Filter | Compute | All | GpuPoint[] + KD-tree | Filtered point buffer + rejection mask | <5ms @10M |
| 88 | GPU ICP | Compute | All | Source + target point clouds | 4x4 rigid transform (per-iteration) | <2ms/iter |

**Total PSO binds/frame** (Tier1 steady state): 3 geometry buckets + 1 shadow + 1 OIT resolve + 1 HLR + 1 composite = **<=7 vkCmdBindPipeline**. All 19 new passes (#70-#88) are **compute dispatches or reuse existing PSOs** -- zero additional PSO binds. All are **conditional RG nodes** -- zero cost when inactive.

### 3.3 Zero-Cost Integration Strategy for Extended Passes (#70-#88)

**Core principle**: RenderGraph conditional nodes. Inactive pass = no resource allocation, no barrier insertion, no command recording. The 88-pass pipeline has the **same GPU cost as a 69-pass pipeline** when extended passes are disabled.

| Strategy                         | Passes                                                | How It Works                                                                                                       | PSO Impact         |
| -------------------------------- | ----------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ | ------------------ |
| **Compute-only (no new PSO)**    | #70-#76, #79, #80, #82-#85, #87, #88                  | Pure compute dispatches. Share existing compute pipeline layout (push constants + SSBO). No rasterization state.   | 0 new PSOs         |
| **Reuse HLR SDF line PSO**       | #73 (Contour isolines), #77 (Sketch)                  | Output polyline SSBO → feed into existing HLR Render pass (#30) SDF line pipeline. Same PSO, same fragment shader. | 0 new PSOs         |
| **Reuse Analysis Overlay PSO**   | #72 (Curvature data gen)                              | Compute generates per-vertex curvature SSBO → existing Analysis Overlay pass (#46) reads it. No new PSO.           | 0                  |
| **Reuse instanced Graphics PSO** | #76 (Trace Curves), #78 (Polyhedral), #86 (Debug Vis) | Same instanced draw PSO as #38 FEM / #42 Tensor. Different SSBO data, same pipeline layout.                        | 0                  |
| **Single new PSO**               | #81 (Decal Projector)                                 | Deferred decal projection requires 1 new fragment PSO (reads GBuffer, writes albedo+normal). Pre-built at init.    | +1 PSO (init only) |

**Infrastructure reuse map** (pass → existing infrastructure dependency):

```
#70 Selection Outline → VisBuffer (#9) selectionMask + JFA compute (3-pass: init+flood+extract)
#71 Interference     → BLAS/TLAS (#15.4) + ray query (same as #32 Ray Pick)
#72 Curvature        → TopoGraph adjacency SSBO (same as #28 HLR Classify)
#73 Contour          → Scalar SSBO (same as #39) + HLR SDF render pipeline (#30)
#74 Probe            → Scalar SSBO (same as #39) + single-pixel compute readback
#75 Result Compare   → Two scalar SSBOs → per-node subtract → existing color map (#39)
#76 Trace Curves     → Ring buffer SSBO → compute tube mesh → instanced draw (same PSO as #42)
#77 Sketch           → Sketch entity SSBO → SDF line render (same PSO as #30 HLR Render)
#78 Polyhedral       → Compute fan-triangulate → instanced draw (same PSO as #38 FEM)
#79 Surface LIC      → Vector field SSBO + noise texture → compute convolution → texture output
#80 Pathlines        → Time-varying vector SSBO → RK4 compute (same as #40) + tube render
#81 Decal            → GBuffer read + decal OBB → 1 new fragment PSO (deferred decal)
#82 GPU QEM          → Meshlet vertex/index SSBO → offline compute (not per-frame)
#83 ReSTIR DI        → BLAS/TLAS + GpuLight[] + GBuffer → reservoir compute (same bindings as #18)
#84 ReSTIR GI        → BLAS/TLAS + GBuffer → path reuse compute (same bindings as #22 RT GI)
#85 DDGI             → BLAS/TLAS → probe update compute + irradiance sample in #18 resolve
#86 Debug Vis        → Meshlet metadata SSBO → instanced quad overlay (same PSO as #60 Gizmo)
#87 PC Filter        → GpuPoint[] + KD-tree SSBO → compute filter (reuse #43 point buffer)
#88 GPU ICP          → Two GpuPoint[] SSBOs → compute closest-point + SVD reduction
```

### 3.4 Extended Pass I/O Specifications (#70--#88)

#### Pass #70: Selection Outline (JFA)

| Item          | Specification                                                                                                                                                                          |
| ------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Condition** | Any entity selected/hovered. All tiers.                                                                                                                                                |
| **Input**     | `VisBuffer` R32G32_UINT (instanceId) + `GpuInstance[].selectionMask` (selected/hovered bits)                                                                                           |
| **Output**    | `SelectionSDF` R16F, viewport resolution (signed distance to nearest selected pixel)                                                                                                   |
|               | `SelectionOutline` RGBA8, Overlay layer (outline color at distance == outlineWidth)                                                                                                    |
| **Algorithm** | 3-pass JFA: (1) Init: mark selected pixels as seed (distance=0); (2) Jump Flood: log2(max(w,h)) passes, 8-neighbor propagation; (3) Extract: distance == outlineWidth -> outline color |
| **Dispatch**  | Compute: init `ceil(w/8)*ceil(h/8)`, flood 12 passes @4K, extract `ceil(w/8)*ceil(h/8)`                                                                                                |
| **Budget**    | <1.2ms @4K                                                                                                                                                                             |

#### Pass #71: GPU Interference Detection

| Item          | Specification                                                                                                                                        |
| ------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Condition** | Interference check requested (user-triggered, not per-frame). T1 only.                                                                               |
| **Input**     | `BLAS/TLAS` acceleration structure + `BodyPairList` SSBO (uint32 pairs to check)                                                                     |
| **Output**    | `InterferenceResult` SSBO: per-pair `{bool intersects, float volume, uint32 faceIdA, faceIdB}`                                                       |
| **Algorithm** | Per body-pair: dense ray-cast sampling from surface A into body B (reuse ray query). Intersection volume estimated by integral of penetration depth. |
| **Dispatch**  | Compute (ray query): 1 workgroup per body pair, 256 rays per pair                                                                                    |
| **Budget**    | <5ms @100 pairs                                                                                                                                      |

#### Pass #72: GPU Curvature Analysis

| Item          | Specification                                                                                    |
| ------------- | ------------------------------------------------------------------------------------------------ |
| **Condition** | Curvature analysis overlay active. All tiers.                                                    |
| **Input**     | `MeshletVertexData` SSBO (BDA): vertex positions + `TopoGraph` adjacency SSBO (1-ring neighbors) |
| **Output**    | `CurvatureBuffer` SSBO: per-vertex `{float gaussian, float mean, float2 principalDir}`           |
| **Algorithm** | Quadric fit on 1-ring neighborhood -> eigenvalue decomposition -> Gaussian/mean curvature        |
| **Dispatch**  | Compute: `ceil(vertexCount/256)` workgroups                                                      |
| **Budget**    | <1ms @1M vertices                                                                                |
| **Consumer**  | Analysis Overlay pass (#46) reads `CurvatureBuffer` -> color map via LUT                         |

#### Pass #73: Contour Plot (Isolines)

| Item          | Specification                                                                                                      |
| ------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Condition** | CAE contour display active. All tiers.                                                                             |
| **Input**     | `ScalarArray` SSBO (R32F per node) + `ElementConnectivity` SSBO + push(`isoValues[]` float array, `isoCount` uint) |
| **Output**    | `IsolineBuffer` SSBO: polyline segments `{float3 startPos, float3 endPos, float scalarValue}` (atomic append)      |
| **Algorithm** | Marching-squares-on-triangles: per-triangle, for each iso-value, interpolate crossing points on edges              |
| **Dispatch**  | Compute: `ceil(triangleCount/256)` workgroups                                                                      |
| **Render**    | `IsolineBuffer` -> HLR SDF Render pass (#30) pipeline (same PSO, LinePattern = continuous thin)                    |
| **Budget**    | <1ms compute + <0.5ms render                                                                                       |

#### Pass #74: Probe Query

| Item          | Specification                                                                                                 |
| ------------- | ------------------------------------------------------------------------------------------------------------- |
| **Condition** | Probe tool active (user placed probe point/line/plane). All tiers.                                            |
| **Input**     | `ScalarArray` or `VectorArray` SSBO + probe geometry (push constant: float3 position, or SSBO for line/plane) |
| **Output**    | `ProbeResult` SSBO: `{float3 position, float scalarValue, float3 vectorValue}` -> CPU readback                |
| **Algorithm** | Barycentric interpolation at probe position within containing element (element search via spatial hash)       |
| **Dispatch**  | Compute: 1 workgroup (single probe) or `ceil(probeCount/64)` (probe line/rake)                                |
| **Budget**    | <0.1ms                                                                                                        |

#### Pass #75: Result Compare (A-B Diff)

| Item          | Specification                                                                                               |
| ------------- | ----------------------------------------------------------------------------------------------------------- |
| **Condition** | Two result sets loaded, comparison mode active. All tiers.                                                  |
| **Input**     | `ScalarArrayA` SSBO (R32F) + `ScalarArrayB` SSBO (R32F), same node count                                    |
| **Output**    | `DiffScalar` SSBO (R32F): per-node `A[i] - B[i]` -> fed to Scalar Field pass (#39) with diverging color map |
| **Dispatch**  | Compute: `ceil(nodeCount/256)` workgroups                                                                   |
| **Budget**    | <0.5ms @1M nodes                                                                                            |

#### Pass #76: Trace Curves

| Item          | Specification                                                                                                        |
| ------------- | -------------------------------------------------------------------------------------------------------------------- |
| **Condition** | Animation active + track points designated. All tiers.                                                               |
| **Input**     | `TrackPointRingBuffer` SSBO: per-track-point `{float3 positions[maxFrames]}` (ring, configurable depth default 1000) |
| **Output**    | `TraceCurveVB` (vertex buffer): tube mesh via Frenet frame (compute-generated) -> instanced draw in Overlay layer    |
| **Algorithm** | Per segment: tangent from adjacent positions, Frenet frame (T,N,B), generate tube cross-section (8-vertex circle)    |
| **Dispatch**  | Compute: `ceil(totalSegments/256)` -> Graphics: instanced draw (same PSO as #42 Tensor Glyph)                        |
| **Budget**    | <0.5ms @16 tracks x 1000 frames                                                                                      |

#### Pass #77: Sketch Renderer

| Item          | Specification                                                                                                                       |
| ------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| **Condition** | Sketch edit mode active. All tiers.                                                                                                 |
| **Input**     | `SketchEntityBuffer` SSBO: per-entity `{uint8 type(line/arc/spline/circle), float3 params[], float4 color, uint32 constraintFlags}` |
|               | `SketchPlane` push constant: float4 planeEquation + float4x4 sketchToWorld                                                          |
| **Output**    | SDF anti-aliased 2D entities on sketch plane (Scene layer, depth-tested against 3D geometry)                                        |
| **Render**    | Same SDF line PSO as HLR Render (#30): project sketch entities to screen, expand to quads, SDF fragment                             |
| **Budget**    | <0.3ms @1K entities                                                                                                                 |

#### Pass #78: Polyhedral CFD Mesh

| Item          | Specification                                                                                                                                                                                |
| ------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Condition** | CFD polyhedral mesh loaded. All tiers.                                                                                                                                                       |
| **Input**     | `PolyhedralConnectivity` SSBO: per-cell `{uint faceOffset, uint faceCount}` + per-face `{uint vertexOffset, uint vertexCount}` + `VertexPositions` SSBO (float3) + `CellScalars` SSBO (R32F) |
| **Output**    | `TriangulatedMeshVB` (vertex buffer): fan-triangulated faces (compute append) -> instanced draw                                                                                              |
| **Algorithm** | Compute: per-face fan triangulation (centroid-based for non-convex), per-vertex scalar interpolation                                                                                         |
| **Dispatch**  | Compute: `ceil(faceCount/256)` -> Graphics: DrawIndexedIndirect (same PSO as #38 FEM)                                                                                                        |
| **Budget**    | <2ms @500K cells                                                                                                                                                                             |

#### Pass #79: Surface LIC

| Item          | Specification                                                                                                                         |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| **Condition** | Surface LIC visualization active. All tiers.                                                                                          |
| **Input**     | `VectorField` SSBO (float3 per node, tangent-plane projected) + `NoiseTexture` R8 (white noise, surface UV-mapped) + `SurfaceMesh` VB |
| **Output**    | `LIC_Texture` R8, same resolution as surface UV atlas (line-integral convolved noise)                                                 |
| **Algorithm** | Per texel: trace streamline forward+backward in vector field (20 steps each direction), accumulate noise samples along path           |
| **Dispatch**  | Compute: `ceil(texWidth/8) * ceil(texHeight/8)` workgroups                                                                            |
| **Budget**    | <3ms @512x512 UV atlas                                                                                                                |

#### Pass #80: Pathlines / Streaklines

| Item          | Specification                                                                                                                                                              |
| ------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Condition** | Time-varying vector field + animation active. All tiers.                                                                                                                   |
| **Input**     | `VectorFieldTimeSeries` SSBO array (float3 per node per timestep) + `SeedPoints` SSBO (float3) + push(`currentTime` float, `dt` float)                                     |
| **Output**    | Tube geometry VB (same format as #40 Streamline) -> instanced draw in Scene layer                                                                                          |
| **Algorithm** | RK4 integration through time-varying field (interpolate between timesteps). Pathlines: track individual particles. Streaklines: continuous release from fixed seed points. |
| **Dispatch**  | Compute: `ceil(seedCount * timeSteps / 256)` -> Graphics: tube instanced draw (same PSO as #40)                                                                            |
| **Budget**    | <3ms @100 seed points x 500 steps                                                                                                                                          |

#### Pass #81: Decal Projector

| Item          | Specification                                                                                                                                                                                         |
| ------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Condition** | Any decal active. All tiers.                                                                                                                                                                          |
| **Input**     | `GBuffer.RT0` RGBA8 (albedo) + `GBuffer.RT1` RGBA16F (normal) + `GBuffer.Depth` D32F + `DecalList` SSBO: per-decal `{float4x4 invProjection, uint32 albedoTex, uint32 normalTex, float4 blendParams}` |
| **Output**    | Modified `GBuffer.RT0` (albedo) + `GBuffer.RT1` (normal) — in-place write via deferred decal blending                                                                                                 |
| **PSO**       | **1 new fragment PSO** (the only new PSO in extended passes): fullscreen triangle per decal, reads GBuffer + decal textures, writes albedo+normal with blend mask based on decal OBB containment test |
| **Dispatch**  | Fragment: 1 fullscreen draw per decal (max 64 recommended), depth-tested against GBuffer                                                                                                              |
| **Budget**    | <0.2ms @64 decals                                                                                                                                                                                     |

#### Pass #82: GPU QEM Mesh Simplification

| Item          | Specification                                                                                                                                 |
| ------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| **Condition** | Offline (import time or explicit user request). Not per-frame. T1 only.                                                                       |
| **Input**     | `MeshletVertexData` SSBO: vertex positions + indices + adjacency                                                                              |
| **Output**    | Simplified LOD mesh (lower meshlet count) -> written to ClusterDAG hierarchy                                                                  |
| **Algorithm** | Iterative edge collapse with Quadric Error Metric (GPU parallel: per-edge QEM compute, min-heap selection, collapse + local re-triangulation) |
| **Dispatch**  | Compute: multiple iterations, each `ceil(edgeCount/256)` workgroups                                                                           |
| **Budget**    | <50ms per LOD level (offline, not frame-budget-constrained)                                                                                   |

#### Pass #83: ReSTIR DI (Direct Illumination)

| Item          | Specification                                                                                                                                                         |
| ------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Condition** | T1 + RT HW. Enhanced/Ultra quality preset. Phase 19.                                                                                                                  |
| **Input**     | `BLAS/TLAS` + `GpuLight[]` SSBO (64B/light) + `GBuffer` (RT0, RT1, Depth) + `Reservoir_History` SSBO (per-pixel reservoir from previous frame)                        |
| **Output**    | `Reservoir_Current` SSBO: per-pixel `{uint32 lightIndex, float weightSum, float M, float W}` (16B/pixel)                                                              |
|               | `ReSTIR_DirectColor` RGBA16F, viewport resolution (shaded direct illumination)                                                                                        |
| **Algorithm** | Candidate generation (random light sample) -> temporal reuse (read history reservoir, combine) -> spatial reuse (5 random neighbors, combine) -> shade selected light |
| **Dispatch**  | Compute (ray query): 3 passes, each `ceil(w/8)*ceil(h/8)` workgroups                                                                                                  |
| **Budget**    | <4ms @1080p                                                                                                                                                           |

#### Pass #84: ReSTIR GI (Global Illumination)

| Item          | Specification                                                                                        |
| ------------- | ---------------------------------------------------------------------------------------------------- |
| **Condition** | T1 + RT HW. Ultra quality preset. Phase 19.                                                          |
| **Input**     | `BLAS/TLAS` + `GBuffer` (RT1 normals, Depth) + `GI_Reservoir_History` SSBO                           |
| **Output**    | `ReSTIR_IndirectColor` RGBA16F, viewport resolution (1-2 bounce indirect)                            |
| **Algorithm** | Short path trace from GBuffer hit -> reservoir resampling across pixels and frames (Lin et al. 2022) |
| **Dispatch**  | Compute (ray query): 3 passes (candidate + temporal + spatial), `ceil(w/8)*ceil(h/8)`                |
| **Budget**    | <6ms @1080p (1-bounce)                                                                               |

#### Pass #85: DDGI Probes

| Item          | Specification                                                                                                                                                                                                                                                                                                                                        |
| ------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Condition** | T1 + RT HW. Enhanced/Ultra preset. Phase 19.                                                                                                                                                                                                                                                                                                         |
| **Input**     | `BLAS/TLAS` + `ProbeGrid` SSBO: `{float3 position, float3[9] irradianceSH, float[8] visibilityOct}` per probe                                                                                                                                                                                                                                        |
| **Output**    | Updated `ProbeGrid` SSBO (irradiance + visibility octahedral maps, exponential moving average). **Double-buffered**: write to `ProbeGrid[writeIndex]`, Deferred Resolve (#18) reads `ProbeGrid[readIndex]`; indices swap each frame. Required to prevent cross-frame write/read race when async compute overlaps graphics (see `03-sync.md` §7.4.2). |
| **Algorithm** | Per probe: trace 32-64 short rays -> update irradiance SH + visibility octahedral via EMA. Probe relocation (move out of geometry).                                                                                                                                                                                                                  |
| **Dispatch**  | Compute (ray query): `ceil(probeCount/32)` workgroups, 32-64 rays per probe                                                                                                                                                                                                                                                                          |
| **Consumer**  | Deferred Resolve (#18) samples probe grid for stable diffuse GI (trilinear + Chebyshev visibility)                                                                                                                                                                                                                                                   |
| **Budget**    | <2ms @1K probes                                                                                                                                                                                                                                                                                                                                      |

#### Pass #86: GPU Debug Visualization

| Item          | Specification                                                                                             |
| ------------- | --------------------------------------------------------------------------------------------------------- |
| **Condition** | Debug mode enabled (dev only). All tiers.                                                                 |
| **Input**     | `MeshletMetadata` SSBO: per-meshlet `{float3 center, float radius, uint32 triangleCount, float lodError}` |
|               | `BVHNodes` SSBO: `{float3 aabbMin, float3 aabbMax, uint32 childMask}`                                     |
| **Output**    | Heatmap overlay RGBA8 (Overlay layer): meshlet density / BVH depth / LOD level color-coded                |
| **Dispatch**  | Graphics (instanced): same PSO as Gizmo (#60), per-meshlet/BVH-node colored quad                          |
| **Budget**    | <0.5ms                                                                                                    |

#### Pass #87: Point Cloud Filter

| Item          | Specification                                                                                                                                                                        |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Condition** | Point cloud loaded + filter requested. All tiers.                                                                                                                                    |
| **Input**     | `GpuPoint[]` SSBO (16B/pt) + `KDTree` SSBO (GPU-built from #88 ICP infrastructure or Phase 10 spatial index)                                                                         |
| **Output**    | `FilteredPointBuffer` SSBO (surviving points, atomic append) + `RejectionMask` R8 SSBO (per-point accept/reject)                                                                     |
| **Algorithm** | Statistical Outlier Removal: per-point k-NN mean distance (k=20) -> reject if > mu + alpha\*sigma (configurable alpha, default 1.0). Radius filter: reject if <N neighbors within R. |
| **Dispatch**  | Compute: `ceil(pointCount/256)` workgroups                                                                                                                                           |
| **Budget**    | <5ms @10M points                                                                                                                                                                     |

#### Pass #88: GPU ICP Registration

| Item          | Specification                                                                                                                                                                                                                             |
| ------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Condition** | ICP alignment requested (user-triggered). All tiers.                                                                                                                                                                                      |
| **Input**     | `SourcePoints` SSBO (float3) + `TargetPoints` SSBO (float3) + `TargetKDTree` SSBO                                                                                                                                                         |
| **Output**    | `ICPTransform` SSBO: `float4x4` rigid transform (per-iteration, converges over 20-50 iterations)                                                                                                                                          |
|               | `ICPResidual` SSBO: per-point `{float distance, uint32 correspondenceIdx}` (for visualization)                                                                                                                                            |
| **Algorithm** | Per iteration: (1) Closest-point query via KD-tree (GPU parallel, <2ms @1M pts), (2) SVD alignment (Kabsch: GPU covariance matrix reduction -> CPU 3x3 SVD -> transform), (3) Apply transform, check convergence (RMSE delta < threshold) |
| **Dispatch**  | Compute: closest-point `ceil(sourceCount/256)`, covariance reduction `ceil(sourceCount/256)` -> CPU SVD -> push constant transform update                                                                                                 |
| **Budget**    | <2ms per iteration, 20-50 iterations for convergence                                                                                                                                                                                      |

**Topology-Aware Culling** (roadmap Phase 8): NOT a separate pass. Implemented as an extension of existing GPU Culling pass (#2) — adds `topoMask` field read from `GpuInstance.flags` during instance-level cull. Zero additional dispatch, ~1 AND instruction per instance.

**DGC / Work Graphs** (roadmap Phase 20): NOT a pass. Optimization of existing indirect dispatch mechanism — replaces CPU-side indirect buffer updates with GPU-generated command sequences. Transparent to pass graph structure.

---

## 4. Frame Data Flow

### 4.1 Tier1 Main Pipeline (Vulkan / D3D12)

```
Frame N:
+-----------------------------------------------------------------------+
| Graphics Queue                                                         |
| +------+ +-------+ +-----------------+ +----------+ +----+ +--------+|
| |BLAS/ | |GPU    | |Task->Mesh->      | |VSM Shadow| |Def.| |TAA->FSR||
| |TLAS  | |Cull   | |VisBuffer(1 PSO)  | |(dirty pg)| |Res.| |->Tone  ||
| +------+ +-------+ +-----------------+ +----------+ +----+ +--------+|
|                                                                        |
| Async Compute Queue (optional; see 03-sync.md §5.8.2 ComputeQueueLevel A-D degradation) |
| +----------+ +--------------+                                         |
| |GTAO      | |Material Sort |                                         |
| |(half-res)| |+ Resolve     |                                         |
| +----------+ +--------------+                                         |
|                                                                        |
| Transfer Queue (Vulkan 1.4 streaming; see 03-sync.md §7 for sync)     |
| +-------------------------+                                            |
| |Cluster stream upload    |                                            |
| |(non-blocking)           |                                            |
| +-------------------------+                                            |
+-----------------------------------------------------------------------+
```

**Detailed Tier1 data flow**:

```
CPU: SceneBuffer upload (dirty instances only)
  |
  v
BLAS Update (per-body, mode-selected):
  Rigid-only bodies: Refit (<0.2ms total)
  Topology-changed bodies: Rebuild (1-5ms/body, max 2 inline, overflow -> async queue)
  -> TLAS Rebuild (<0.1ms)
  |
  v
GPU Culling Pass A (frustum + prev-frame HiZ + LOD + normal cone)
  |                              |
  v                              v
VisibleList_A              RejectedList_A
  |                              |
  v                              |
DepthPrePass 1A -> HiZ Build    |
                     |           |
                     v           v
              GPU Re-Culling Pass B (frustum + current-frame HiZ)
                     |
                     v
               VisibleList_B -> DepthPrePass 1B (incremental)
                     |
                     v
Macro-Binning (3 buckets via atomic append, VisibleList_A + VisibleList_B)
  |                    |                    |
  v                    v                    v
Bucket 1: Opaque    Bucket 2: Transparent  Bucket 3: Edge/HLR
Task(meshlet cull)    LL-OIT Insert         Edge Classify
  ->Mesh(tri cull)                          Edge Visibility
  ->VisBuffer                               Edge SDF Render
  |                    |                      |
  v                    v                      |
Material Resolve    OIT Sort+Resolve          |
(tile-based DSPBR)     |                      |
  |                    |                      |
  v                    v                      v
GBuffer targets     Sorted transparency    Edge overlay
  |
  v
Light Cluster Assign (3D froxels, <0.1ms @1K lights)
  |
  v
Deferred Resolve (clustered BRDF + all lights + IBL + VSM + AO)
  |
  +-- SSR (Hi-Z march, half-res) [optional]
  |
  +-- RT Reflections / RT Shadows / RT GI [optional, Tier1]
  |
  v
HDR color
  |
  v
SSR -> Bloom -> DoF -> Motion Blur -> Tone Map -> TAA -> FSR/DLSS -> CAS -> Color Grade
  |
  v
Scene layer color + depth
  |
  +-- Analysis overlays (#46: Zebra/Curvature/Draft/...) [if active]
  +-- Section plane output (#31) [reads DepthTarget from #1]
  +-- OIT resolve output (#26 or #27)
  +-- HLR edge output (#30)
  +-- Point cloud splat (#43) + EDL (#44)
  +-- CAE visualization (#38-#42: FEM/Scalar/Streamline/Isosurface/Tensor)
  +-- PMI annotations (#45)
  +-- Outline post-process (#58) [reads depth #1 + normals #10]
  +-- Boolean preview (#34) [reads depth layers from #1, Preview layer]
  +-- Overlay: Gizmo (#60), Grid (#61), ViewCube (#62), Snap (#63), Measurement (#64)
  +-- Color bar / Legend (#47)
  |
  v
LayerStack Compositor (#65: Scene + Preview + Overlay + Widgets + SVG + HUD)
  |
  +-- Present / Readback
  +-- Offscreen Render (#66)
  +-- HLR -> SVG/PDF (#67 reads EdgeBuffer from #30) -> 2D Drawing (#68)
  +-- XR Stereo (#69)
```

### 4.2 Tier1 Complete Step-by-Step (Vulkan / D3D12)

Every step references the DAG pass number from §3.

| Step | Pass #      | Name                    | Type                 | Description                                                                                                                            |
| ---- | ----------- | ----------------------- | -------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| 1    | —           | CPU: SceneBuffer upload | CPU→GPU              | Upload dirty GpuInstance[128B] to GPU SSBO (only changed instances)                                                                    |
| 2    | —           | BLAS/TLAS update        | Compute/RT           | Refit (rigid) or Rebuild (topology change) → TLAS rebuild. §15.4                                                                       |
| 3    | #2A         | GPU Culling Pass A      | Compute              | Instance-level cull (frustum + prev-frame HiZ occlusion + LOD + normal cone) → VisibleList_A + RejectedList_A                          |
| 4    | #1A         | DepthPrePass + HiZ      | Graphics             | Render VisibleList_A (depth-only) → D32F Reverse-Z. Compute current-frame HiZ mip pyramid (min per 2×2)                                |
| 5    | #2B         | GPU Re-Culling Pass B   | Compute              | Re-test RejectedList_A against current-frame HiZ → VisibleList_B. Static frames: skipped (dirty flag)                                  |
| 5b   | #1B         | DepthPrePass Append     | Graphics             | Render VisibleList_B → incremental depth update + HiZ refresh. Skipped if VisibleList_B empty                                          |
| 6    | #3          | Light Cluster Assign    | Compute              | 3D froxel grid, light-centric atomicAdd, up to 4096 lights                                                                             |
| 7    | #4          | Macro-Binning           | Compute              | Classify visible instances into 3 buckets: Opaque / Transparent / Edge                                                                 |
| 8    | #5→#6→#9    | Task→Mesh→VisBuffer     | Graphics             | Single PSO, 1 draw. Task: meshlet cull + compact. Mesh: per-triangle cull + VisBuffer R32G32_UINT                                      |
| 9    | #7          | SW Rasterizer (opt)     | Compute              | Small triangles (<4px²) → uint64 SSBO atomicMax → resolve pass → VisBuffer                                                             |
| 10   | #10         | Material Resolve        | Compute (Async)      | Tile-based (16×16): BDA vertex fetch → DSPBR 8-layer BSDF → GBuffer MRT (RT0 albedo+metallic, RT1 normal+roughness, RT2 motion, Depth) |
| 11   | #12         | VSM Render              | Graphics (Mesh)      | Virtual Shadow Maps: render only dirty pages (128×128 tiles, 16K² virtual)                                                             |
| 12   | #14         | Shadow Atlas            | Graphics             | Point/spot light shadow: LRU tile atlas, distance-scaled resolution                                                                    |
| 13   | #15         | GTAO                    | Compute              | Half-res, 8 directions × 2 horizon steps, bilateral upsample. Async compute candidate                                                  |
| 14   | #17         | RTAO (opt)              | Compute (ray query)  | 1spp + 8-frame EMA, short-range ray (<1m)                                                                                              |
| 15   | #18         | Deferred Resolve        | Compute              | Central lighting: GBuffer + ClusterGrid + IBL (SH L2 + specular cubemap) + VSM/Atlas + AO → HDR RGBA16F                                |
| 16   | #19         | IBL Precompute          | Compute              | One-time: equirect→cubemap + SH L2 + specular prefilter + BRDF LUT. Cached                                                             |
| 17   | #20         | RT Reflections (opt)    | Compute (ray query)  | Roughness < 0.3, full material eval on hit                                                                                             |
| 18   | #21         | RT Shadows (opt)        | Compute (ray query)  | 1spp + temporal, soft penumbra                                                                                                         |
| 19   | #22         | RT GI (opt)             | Compute (ray query)  | Half-res, SH probe cache                                                                                                               |
| 20   | #25→#26     | LL-OIT Insert→Resolve   | Fragment+Compute     | Per-pixel linked list (per-tile counter), insertion sort ≤16 layers                                                                    |
| 21   | #28→#29→#30 | HLR 4-stage             | Compute+Graphics     | Classify → HiZ visibility → SDF edge render → dash pattern                                                                             |
| 22   | #31         | Section Plane           | Fragment (stencil)   | Clip + cap + ISO 128 hatch                                                                                                             |
| 23   | #48         | SSR (opt)               | Compute              | Hi-Z ray march, half-res, temporal accumulation                                                                                        |
| 24   | #49         | Bloom                   | Compute              | 6-level Gaussian chain, brightness extract > 1.0                                                                                       |
| 25   | #50         | DoF (opt)               | Compute              | Gather-based bokeh (Jimenez 2014), half-res 16 samples                                                                                 |
| 26   | #51         | Motion Blur (opt)       | Fragment             | McGuire 2012 tile-based, per-pixel directional                                                                                         |
| 27   | #52         | Tone Mapping            | Fragment             | ACES/AgX/Khronos/Reinhard/Uncharted2/Linear + vignette + CA                                                                            |
| 28   | #53         | TAA                     | Compute              | Halton(2,3) jitter, YCoCg clamp, motion rejection, reactive mask                                                                       |
| 29   | #54         | Temporal Upscale (opt)  | Compute              | FSR 3.0 / DLSS 3.5                                                                                                                     |
| 30   | #56         | CAS Sharpen             | Compute              | AMD FidelityFX CAS                                                                                                                     |
| 31   | #57         | Color Grading (opt)     | Fragment             | 3D LUT 32³ RGBA8                                                                                                                       |
| 32   | #59         | VRS Image (opt)         | Compute              | Luminance gradient → per-16×16 shading rate                                                                                            |
| 33   | #60–#64     | Overlays                | Graphics (instanced) | Gizmo, Grid, ViewCube, Snap, Measurement                                                                                               |
| 34   | #45         | PMI Render              | Graphics (instanced) | MSDF text + leaders + symbols                                                                                                          |
| 35   | #46         | Analysis Overlay (opt)  | Fragment             | Zebra/Iso/Curvature/Draft/...                                                                                                          |
| 36   | #47         | Color Bar               | Fragment             | Auto-generated legend                                                                                                                  |
| 37   | #65         | LayerStack Compositor   | Fragment             | 6-layer alpha-blend: Scene + Preview + Overlay + Widgets + SVG + HUD                                                                   |
| 38   | —           | Present                 | —                    | Swapchain present (VkQueuePresent / IDXGISwapChain::Present)                                                                           |

### 4.3 Tier2 Complete Step-by-Step (Compat Vulkan 1.1+, no mesh shader)

| Step | Pass #      | Name                  | Type                      | Difference from T1                                                          |
| ---- | ----------- | --------------------- | ------------------------- | --------------------------------------------------------------------------- |
| 1    | —           | CPU: Draw batch sort  | CPU                       | Material → distance sort, generate MDI indirect buffer                      |
| 2    | #1          | DepthPrePass          | Graphics (Vert)           | Vertex shader, no HiZ pyramid (CPU frustum culling only)                    |
| 3    | —           | CPU Frustum Culling   | CPU                       | AABB vs 6 planes, no GPU culling pass                                       |
| 4    | #4          | Macro-Binning         | CPU/GPU                   | CPU classifies buckets (no atomic append compute)                           |
| 5    | #8          | Vertex+MDI            | Graphics (Vert)           | N PSOs (material-sorted), `DrawIndexedIndirectCount`                        |
| 6    | #11         | GBuffer Forward       | Graphics (Vert+Frag)      | Fragment shader evaluates DSPBR per-draw → GBuffer MRT. **Replaces #5-#10** |
| 7    | #13         | CSM Render            | Graphics (Vert)           | 4-cascade, log split λ=0.7, 10% overlap                                     |
| 8    | #14         | Shadow Atlas          | Graphics                  | Same as T1 but max 8 lights (vs 32)                                         |
| 9    | #15         | GTAO                  | Compute                   | Same as T1                                                                  |
| 10   | #18         | Deferred Resolve      | Fragment (fullscreen-tri) | CPU-sorted light UBO (max 256 lights), **no clustered grid**                |
| 11   | #19         | IBL Precompute        | Compute                   | Same as T1                                                                  |
| 12   | #25→#26     | LL-OIT                | Fragment+Compute          | Same as T1 (smaller pool: 10M nodes / 160MB on 8GB GPU)                     |
| 13   | #28→#29→#30 | HLR 4-stage           | Compute+Graphics          | Compute classify/visibility same; Render uses Vertex shader (not Mesh)      |
| 14   | #31         | Section Plane         | Fragment                  | Same as T1                                                                  |
| 15   | #49         | Bloom                 | Compute                   | Same as T1                                                                  |
| 16   | #52         | Tone Mapping          | Fragment                  | Same as T1                                                                  |
| 17   | #53         | TAA                   | Compute                   | Same as T1                                                                  |
| 18   | #55         | FXAA / MSAA 4x        | Fragment                  | **Replaces FSR/DLSS**                                                       |
| 19   | #56         | CAS Sharpen           | Compute                   | Same as T1                                                                  |
| 20   | #60–#65     | Overlays → Compositor | Graphics+Fragment         | Same as T1                                                                  |
| 21   | —           | Present               | —                         | Same                                                                        |

**Not available on T2**: #3 Light Cluster, #5-#6 Task/Mesh, #7 SW Raster, #9 VisBuffer, #10 Material Resolve, #12 VSM, #17 RTAO, #20-#24 RT features, #48 SSR, #50 DoF, #51 Motion Blur, #54 Temporal Upscale, #59 VRS, #69 XR

### 4.4 Tier3 Complete Step-by-Step (WebGPU / Browser)

| Step | Pass #      | Name                             | Type                      | Difference from T2                                                      |
| ---- | ----------- | -------------------------------- | ------------------------- | ----------------------------------------------------------------------- |
| 1    | —           | CPU: Draw sort + chunked binding | CPU                       | 32K instances/bind group, CPU frustum pre-filter selects visible chunks |
| 2    | #1          | DepthPrePass                     | Graphics (Vert)           | Per-draw `draw()` (no MDI), no HiZ                                      |
| 3    | #8          | Vertex+draw                      | Graphics (Vert)           | **No MDI** — individual `draw()` calls per draw batch                   |
| 4    | #11         | GBuffer Forward                  | Graphics (Vert+Frag)      | Same as T2                                                              |
| 5    | #13         | CSM Render                       | Graphics (Vert)           | **2-cascade** (vs T2's 4), smaller atlas                                |
| 6    | #16         | SSAO                             | Fragment                  | **8 samples** (vs T2 GTAO). Replaces #15                                |
| 7    | #18         | Deferred Resolve                 | Fragment (fullscreen-tri) | CPU-sorted light UBO (max **64 lights**)                                |
| 8    | #19         | IBL Precompute                   | Compute                   | Same                                                                    |
| 9    | #27         | Weighted OIT                     | Fragment                  | **Replaces LL-OIT**. McGuire-Bavoil, approximate                        |
| 10   | #28→#29→#30 | HLR 4-stage                      | Compute+Vert              | Same as T2                                                              |
| 11   | #31         | Section Plane                    | Fragment                  | Same                                                                    |
| 12   | #49         | Bloom                            | Fragment                  | Fragment path (no compute)                                              |
| 13   | #52         | Tone Mapping                     | Fragment                  | Same                                                                    |
| 14   | #55         | FXAA                             | Fragment                  | **FXAA only** (no MSAA, no TAA)                                         |
| 15   | #60–#65     | Overlays → Compositor            | Graphics+Fragment         | Same                                                                    |
| 16   | —           | Present                          | —                         | Canvas / OffscreenCanvas                                                |

**Additional T3 limitations**: `maxStorageBufferBindingSize` 128-256MB → chunked binding; push constants emulated as 256B UBO; float64 via DS emulation (2×f32); WGSL shader target; single-queue (no async compute). Max scene: 10M tri / 500K instances. Recommended for viewing/annotation only.

### 4.5 Tier4 Complete Step-by-Step (OpenGL 4.3+)

| Step | Pass #     | Name                                                                             | Type                      | Difference from T3                                                  |
| ---- | ---------- | -------------------------------------------------------------------------------- | ------------------------- | ------------------------------------------------------------------- |
| 1-4  | Same as T2 | Draw sort → DepthPrePass → MDI → GBuffer                                         | Graphics (Vert)           | **Has MDI** (GL 4.3 `glMultiDrawElementsIndirect`) — faster than T3 |
| 5    | #13        | CSM Render                                                                       | Graphics (Vert)           | **4-cascade** (same as T2, better than T3's 2)                      |
| 6    | #16        | SSAO                                                                             | Fragment                  | Same as T3                                                          |
| 7    | #18        | Deferred Resolve                                                                 | Fragment (fullscreen-tri) | CPU-sorted UBO, max 256 lights (same as T2)                         |
| 8-15 | Same as T3 | IBL → Weighted OIT → HLR → Section → Bloom → ToneMap → FXAA/MSAA 4x → Compositor |                           | **Has MSAA 4x option** (T3 doesn't)                                 |

**T4 vs T3 advantages**: MDI support, MSAA 4x, 4-cascade CSM, 256 lights (vs 64). **T4 vs T2 disadvantage**: no TAA, no GTAO, coarser `glMemoryBarrier`. Primary use case: remote desktop / VM / containers without Vulkan support.

### 4.6 Key Tier Differences Summary

| Aspect        |            T1 (Vulkan/D3D12)            |      T2 (Compat Vk)       |       T3 (WebGPU)        |       T4 (GL 4.3)       |
| ------------- | :-------------------------------------: | :-----------------------: | :----------------------: | :---------------------: |
| Geometry      |      #5→#6→#9 Task/Mesh/VisBuffer       |  #8+#11 Vert+MDI+GBuffer  | #8+#11 Vert+draw+GBuffer | #8+#11 Vert+MDI+GBuffer |
| Culling       |             #2 GPU compute              |        CPU frustum        |  CPU frustum + chunked   |       CPU frustum       |
| Light culling |           #3 Clustered (4096)           |       CPU UBO (256)       |       CPU UBO (64)       |      CPU UBO (256)      |
| Shadow        |        #12 VSM + #14 Atlas (32)         | #13 CSM-4 + #14 Atlas (8) |        #13 CSM-2         |        #13 CSM-4        |
| AO            |           #15 GTAO / #17 RTAO           |         #15 GTAO          |        #16 SSAO-8        |        #16 SSAO         |
| OIT           |             #25→#26 LL-OIT              |      #25→#26 LL-OIT       |       #27 Weighted       |      #27 Weighted       |
| AA            |            #53 TAA + #54 FSR            |          #53 TAA          |         #55 FXAA         |   #55 FXAA / MSAA 4x    |
| Post-process  | SSR+Bloom+DoF+MB+ToneMap+CAS+ColorGrade |     Bloom+ToneMap+CAS     |      Bloom+ToneMap       |      Bloom+ToneMap      |
| RT            |             #17/#20–#24 all             |             —             |            —             |            —            |
| Async compute |           Timeline semaphore            |             —             |            —             |            —            |
| Max scene     |            2B tri / 10M inst            |    100M tri / 1M inst     |   10M tri / 500K inst    |   100M tri / 1M inst    |

---

## 5. Geometry Stage

### 5.1 Pass I/O Specifications (Geometry Stage)

Detailed resource formats for passes #1--#11:

#### Pass #1: DepthPrePass + HiZ (Two-Pass)

Runs in the two-pass occlusion culling loop: **Pass 1A** renders `VisibleList_A` (from GPU Culling Pass A), **Pass 1B** renders `VisibleList_B` (from GPU Re-Culling Pass B). HiZ is rebuilt after each sub-pass. See §5.3.

| Item          | Specification                                                                             |
| ------------- | ----------------------------------------------------------------------------------------- |
| **Condition** | Always active (all tiers, all frames)                                                     |
| **Input**     | Pass 1A: `VisibleList_A` SSBO (from GPU Culling Pass A) + `SceneBuffer` + `MeshletBuffer` |
|               | Pass 1B: `VisibleList_B` SSBO (from GPU Re-Culling Pass B). Skipped if empty              |
|               | `GpuCameraUBO` (set 0, binding 0): viewProj, near, far                                    |
| **Output**    | `DepthTarget`: D32_SFLOAT, viewport resolution, Reverse-Z (clear=0.0 on Pass 1A only)     |
|               | `HiZPyramid`: R32_SFLOAT, mip chain from viewport to 1x1 (rebuilt after each sub-pass)    |
| **PSO**       | Graphics: depthTest=GreaterOrEqual, depthWrite=true, colorWrite=none                      |
| **Dispatch**  | Tier1: `vkCmdDrawMeshTasksIndirectCountEXT` (same meshlet path as #5/#6)                  |
|               | Tier2/3/4: `vkCmdDrawIndexedIndirect` / `glMultiDrawElementsIndirect`                     |
| **Budget**    | <0.5ms total (Pass 1A + 1B) @10M tri @4K on RTX 4070                                      |

#### Pass #2: GPU Culling (Two-Pass)

Runs twice per frame: **Pass A** (prev-frame HiZ) then **Pass B** (current-frame HiZ). See §5.3 for full two-pass protocol.

| Item          | Specification                                                                                          |
| ------------- | ------------------------------------------------------------------------------------------------------ |
| **Condition** | Always active (all tiers). Skipped if dirty flag clean (static scene optimization).                    |
| **Input**     | `HiZPyramid` R32_SFLOAT (Pass A: previous frame, Pass B: current frame)                                |
|               | `SceneBuffer` SSBO: `GpuInstance[N]`                                                                   |
|               | `BVH` SSBO: flat BvhNode[] array (SAH build Phase 5, LBVH refit Phase 6a)                              |
|               | `GpuCameraUBO`: frustum planes (6x float4), viewProj                                                   |
|               | Push constant: `visibleLayerMask` (uint16), `selectableLayerMask` (uint16), `passId` (uint8: 0=A, 1=B) |
| **Output**    | Pass A: `VisibleList_A` SSBO (atomic append) + `RejectedList_A` SSBO (atomic append)                   |
|               | Pass B: `VisibleList_B` SSBO (atomic append). Input: `RejectedList_A`                                  |
|               | Combined: `VisibleInstanceList` = `VisibleList_A` ∪ `VisibleList_B`                                    |
| **Dispatch**  | Compute: `ceil(instanceCount / 256)` workgroups, 256 threads each (Pass A)                             |
|               | Compute: `ceil(rejectedCount / 256)` workgroups (Pass B, typically 10-40% of Pass A)                   |
| **Budget**    | <0.3ms total (Pass A + Pass B) @100K instances @4K                                                     |

#### Pass #3: Light Cluster Assign

| Item          | Specification                                                                     |
| ------------- | --------------------------------------------------------------------------------- |
| **Condition** | Tier1 only. Tier2/3/4: CPU-sorted light UBO (no GPU pass).                        |
| **Input**     | `GpuLight[]` SSBO: 64B per light                                                  |
|               | `DepthTarget` D32_SFLOAT (for depth slice boundaries)                             |
|               | Push constant: lightCount, grid dimensions                                        |
| **Output**    | `ClusterGrid` SSBO: per-froxel `{uint lightIndices[MAX_PER_CLUSTER], uint count}` |
|               | Grid: `ceil(w/64) x ceil(h/64) x 32` froxels, log2 depth distribution (Reverse-Z) |
| **Dispatch**  | Compute: 1 workgroup per light (light-centric assignment via atomicAdd)           |
| **Budget**    | <0.1ms @1K lights, <0.3ms @4K lights                                              |

#### Pass #4: Macro-Binning

| Item          | Specification                                             |
| ------------- | --------------------------------------------------------- |
| **Condition** | Always active (all tiers).                                |
| **Input**     | `VisibleInstanceList` SSBO (from #2)                      |
|               | `GpuInstance[].flags` (displayStyle bits 16--19)          |
| **Output**    | 3 append-only SSBOs (atomic counters):                    |
|               | `OpaqueBucket` SSBO: instance indices for Bucket 1        |
|               | `TransparentBucket` SSBO: instance indices for Bucket 2   |
|               | `EdgeBucket` SSBO: instance indices for Bucket 3          |
|               | `IndirectDrawArgs[3]`: dispatch arguments for each bucket |
| **Dispatch**  | Compute: `ceil(visibleCount / 256)` workgroups            |
| **Budget**    | <0.1ms                                                    |

#### Passes #5--#9: Geometry Rendering

| Item          | Tier1 (#5 Task + #6 Mesh + #9 VisBuffer)                                                                                          | Tier2/3/4 (#8 Vertex+MDI + #11 GBuffer)                                                                           |
| ------------- | --------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| **Condition** | T1: always for Bucket 1 opaque                                                                                                    | T2/3/4: always for all opaque geometry                                                                            |
| **Input**     | `OpaqueBucket` SSBO (from #4)                                                                                                     | Sorted draw list (CPU material sort)                                                                              |
|               | `MeshletDescriptor[]` SSBO (BDA): per-meshlet {vertexOffset, indexOffset, vertexCount, triangleCount, boundingSphere, normalCone} | `VertexBuffer`: pos(12B)+normal(12B)+uv(8B)+tangent(16B) = 48B/vert (Tier C) or 24B (Tier B)                      |
|               | `GpuInstance[]` SSBO (BDA)                                                                                                        | `IndexBuffer`: uint32                                                                                             |
| **Output**    | `VisBuffer`: R32G32_UINT, viewport resolution (instanceId + primitiveId)                                                          | `GBuffer` MRT:                                                                                                    |
|               |                                                                                                                                   | &nbsp; RT0: RGBA8 (albedo.rgb + metallic)                                                                         |
|               |                                                                                                                                   | &nbsp; RT1: RGBA16F (normal.xyz + roughness)                                                                      |
|               |                                                                                                                                   | &nbsp; RT2: RG16F (motion vectors)                                                                                |
|               |                                                                                                                                   | &nbsp; DS: D32_SFLOAT (Reverse-Z)                                                                                 |
| **PSO**       | 1 PSO: mesh shader, depthTest=GreaterOrEqual, depthWrite=true, no color attachment (VisBuffer is UAV imageStore)                  | N PSOs (material-sorted): vertex+fragment, depthTest=GreaterOrEqual, depthWrite=true, 3 color attachments + depth |
| **Dispatch**  | `vkCmdDrawMeshTasksIndirectCountEXT` (1 draw, all geometry)                                                                       | `vkCmdDrawIndexedIndirectCount` or per-draw `DrawIndexed`                                                         |
| **Budget**    | <1ms @10M tri (Task+Mesh+VisBuffer combined)                                                                                      | <2ms @10M tri                                                                                                     |

#### Pass #7: SW Rasterizer (optional, Tier1)

| Item                | Specification                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Condition**       | Tier1, enabled when small-triangle ratio >30%. Optional optimization.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| **Input**           | `SmallTriList` SSBO: triangles <4px^2 (classified during #6 Mesh shader per-triangle cull, see §5.3 Level 3)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| **Output**          | `VisBuffer` via SSBO atomic (same pixel space as #9)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| **Dispatch**        | Compute: 1 thread per small triangle                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| **Budget**          | <0.3ms                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| **Atomic strategy** | Uses 64-bit packed value `{depth:32 (upper), payload:32 (lower)}` with `atomicMax` on a **uint64 SSBO** (not image atomic). The SSBO path uses `VK_KHR_shader_atomic_int64` (buffer atomics, universally supported on T1 HW since Vulkan 1.2 core). A lightweight resolve pass copies surviving pixels from the SSBO to the VisBuffer R32G32_UINT image. This avoids any dependency on 64-bit **image** atomics (`VK_EXT_shader_image_atomic_int64`), which have limited hardware support. Fallback if `shaderBufferInt64Atomics` unavailable: disable SW rasterizer, mesh shader handles all triangles (correct but suboptimal for <4px). |

#### Pass #10: Material Resolve (Tier1)

| Item          | Specification                                                                                         |
| ------------- | ----------------------------------------------------------------------------------------------------- |
| **Condition** | Tier1 only. Tier2/3/4 do PBR eval in Pass #11 fragment shader.                                        |
| **Input**     | `VisBuffer` R32G32_UINT (sampled)                                                                     |
|               | `GpuInstance[]` SSBO (BDA): instanceId -> materialId lookup                                           |
|               | `MaterialParameterBlock[]` SSBO (bindless): 192B per material                                         |
|               | `BindlessTextureTable`: all material textures (albedo, normal, metalRough, etc.)                      |
|               | `MeshletVertexData` SSBO (BDA): vertex positions, normals, UVs, tangents for attribute reconstruction |
| **Output**    | `GBuffer` MRT (same format as Tier2/3/4 #11 output):                                                  |
|               | &nbsp; RT0: RGBA8 (albedo.rgb + metallic)                                                             |
|               | &nbsp; RT1: RGBA16F (normal.xyz + roughness)                                                          |
|               | &nbsp; RT2: RG16F (motion vectors)                                                                    |
|               | &nbsp; RT3: RGBA8 (emission.rgb + AO, optional -- only if emission/AO active in tile)                 |
| **Dispatch**  | Compute: `ceil(w/16) x ceil(h/16)` workgroups (16x16 tiles)                                           |
|               | Per-tile: classify materialId histogram -> FAST/MEDIUM/SLOW path (see §5.4)                           |
| **Budget**    | <1ms @4K (70--85% tiles hit fast path in CAD)                                                         |

### 5.2 Macro-Binning (3 Render Buckets)

GPU cull compute classifies each visible instance into one of 3 buckets via atomic append:

| Bucket                   | Pass               | PSO Count | Dispatch                           |
| ------------------------ | ------------------ | --------- | ---------------------------------- |
| 1. Opaque Solid          | VisBuffer geometry | 1         | vkCmdDrawMeshTasksIndirectCountEXT |
| 2. Transparent / X-Ray   | Linked-list OIT    | 1         | IndirectCount                      |
| 3. Wireframe / HLR edges | SDF line pass      | 1         | IndirectCount                      |

**Result**: 100K instances x 300 materials = still **3 PSO binds** per frame.

### 5.3 Culling Pipeline

**Three-level hierarchical culling with two-pass occlusion** (Nanite-style):

```
=== Two-Pass Occlusion Culling (eliminates false-negatives from stale HiZ) ===

Pass A -- Cull against PREVIOUS FRAME HiZ (conservative):
  GPU Culling compute (#2): instance-level frustum + prev-frame HiZ occlusion
    → VisibleList_A (definitely visible) + RejectedList_A (potentially occluded)
  DepthPrePass (#1): render VisibleList_A → current-frame depth
  HiZ Build: generate current-frame HiZ pyramid from fresh depth

Pass B -- Re-test rejected geometry against CURRENT FRAME HiZ:
  GPU Re-Culling compute: test RejectedList_A against current-frame HiZ
    → VisibleList_B (newly visible, were false-negatives in Pass A)
  DepthPrePass Append: render VisibleList_B → update depth (incremental)

Combined: VisibleList = VisibleList_A ∪ VisibleList_B
  → Macro-Binning (#4) → Task/Mesh pipeline (#5/#6)

Benefit: 10-30% additional geometry culled vs single-pass (dynamic scenes).
Cost: ~0.1ms for re-cull compute + incremental depth write. Net gain in all
non-trivial scenes. Static scenes: Pass B skipped (dirty flag optimization).

=== Level 1 -- Instance Level (GPU Culling Compute #2): ===
  For each GpuInstance:
    +-- Frustum test (AABB vs 6 planes)
    +-- HiZ occlusion test (project AABB → HiZ pyramid)
    +-- LOD select (ClusterDAG projected-sphere-error metric)
    +-- Normal cone backface test (dp4a quantized, subgroup early-out)
  Output: per-instance visible/rejected classification

=== Level 2 -- Meshlet Level (Task Shader #5): ===
  For each visible instance, process kTaskShaderMeshletCount meshlets per workgroup:
    +-- Meshlet normal cone backface cull (meshoptimizer cone data)
    +-- Meshlet frustum test (bounding sphere)
    +-- Compact visible meshlet indices into MeshPayload via WavePrefixCountBits
    +-- DispatchMesh(visibleCount, 1, 1, payload)
  AMD: ≥32 meshlets/workgroup to amortize DispatchMesh CP latency.

=== Level 3 -- Triangle Level (Mesh Shader #6): ===
  For each meshlet:
    +-- Vertex phase: transform vertices to clip space (1:1 thread-to-vertex)
    +-- Per-triangle culling (Zeux/niagara 2023, AMD GPUOpen 2024):
        +-- Backface cull (cross product sign in clip space)
        +-- Degenerate triangle rejection (zero area)
        +-- Small triangle cull (<1 pixel, subpixel grid test)
        +-- Micro-frustum cull (all 3 verts outside same clip plane)
    +-- Surviving triangles → VisBuffer write
    +-- Culled triangles → emit degenerate index (0,0,0), zero raster cost
  Benefit: eliminates 30-50% additional triangles beyond meshlet-level culling.
  Critical for CAD: dense planar regions have ~60% backface ratio per meshlet.
```

Subgroup ops (WaveBallot, WavePrefixCountBits, WavePrefixSum) for wave-level early-out. Static frames: skip cull entirely (dirty flag optimization).

**Vendor-adaptive meshlet sizing**: Meshlet vertex/primitive limits are link-time constants (see `05-shader-pipeline.md` §15.9 `core/constants.slang`). NVidia optimal: 64V/126T/numthreads(64). AMD RDNA3+ optimal: 128V/256T/numthreads(128). `meshoptimizer::buildMeshlets()` receives the active constants at asset build time. Runtime `SlangCompiler` selects the matching link-time override based on `GpuCapabilityProfile::GetVendor()`.

### 5.4 Visibility Buffer

```
Format:  R32G32_UINT per pixel (64 bits total)
Layout:  R32 = instanceId (full 32-bit)
         G32 = primitiveId (full 32-bit)
         materialId is NOT stored in VisBuffer — resolved at material resolve time:
           GpuInstance[instanceId].materialId -> MaterialParameterBlock[]
Size:    4K (3840x2160) = 3840 * 2160 * 8B = ~66 MB
```

**Material resolve -- Tile-based Binning** (compute dispatch):

```
Step 1 -- Tile Classification (16x16 tiles, shared memory):
  For each tile:
    Scan all pixels -> build per-tile materialId histogram in LDS
    If uniqueMaterialCount == 1 -> FAST PATH (single material, no sorting)
    If uniqueMaterialCount <= 8 -> MEDIUM PATH (local LDS sort + resolve)
    If uniqueMaterialCount > 8  -> SLOW PATH with Z-Binning mitigation (see below)

Step 2 -- Material Resolve:
  Fast path tiles (~70-85% in typical CAD): direct resolve, zero sorting overhead
  Medium path tiles: sorted pixel runs -> sequential material evaluation
  Slow path tiles: Z-Binning -> depth-stratified sub-tiles -> per-layer medium path
```

**Slow path Z-Binning mitigation**: When a tile has >8 unique materials, a full radix sort is expensive (register pressure + shared memory). Instead, the slow path first stratifies pixels by depth into 2-4 depth layers (using depth discontinuity thresholds from HiZ). Each depth layer typically contains fewer distinct materials (objects at different depths rarely share a tile). Each sub-layer then falls back to the medium path (<=8 materials, LDS sort). This converts one expensive radix sort into 2-4 cheap LDS sorts, reducing worst-case from ~0.5ms/tile to ~0.15ms/tile. Benchmark target: PCB/wire-harness scenes with 50+ materials/tile should not exceed 2ms total resolve.

Resolve mega-kernel reads MaterialParameterBlock[] from bindless array, evaluates full DSPBR BSDF (see §6), writes to deferred targets (albedo, normal, roughness, metallic, emission).

**Why tile-based over full-screen radix sort**: full-screen GpuRadixSort on 8.3M pixels (4K) costs ~1.0-1.5ms in bandwidth alone. Tile-based exploits spatial coherence -- CAD models have large single-material regions where >70% of tiles contain only 1 material. UE5 Nanite uses the same approach.

**Cache-aware BDA attribute fetch**: Material Resolve reads vertex attributes via BDA from `MeshletVertexData`. Adjacent pixels may reference different meshlets → scattered memory access. Mitigation: MeshletGenerator (Phase 6a) applies **Morton Code geometry reordering** at partition time — triangle indices and vertex data within each meshlet are physically contiguous in VRAM. Combined with tile-based resolve (spatially coherent pixel groups), L1/L2 cache hit rate for BDA fetch is expected >80% on fast path tiles. Phase 14 Shader PGO validates this with NSight memory traffic analysis.

### 5.5 GpuInstance Layout

```cpp
struct GpuInstance {                    // alignas(16), 128 bytes
    float4x4    worldMatrix;            // 64B -- CAMERA-RELATIVE (RTE): CPU computes
                                        //   worldMatrix = double_modelMatrix - double_cameraPos
                                        //   then casts to float4x4. Eliminates FP32 jitter at >10km.
                                        //   (demands LW35.1, LW35.2)
    float4      boundingSphere;         // 16B -- xyz=center, w=radius
    uint32_t    entityId;               // 4B  -- ECS Entity [gen:8|idx:24]
    uint32_t    meshletBaseIndex;       // 4B
    uint32_t    meshletCount;           // 4B
    uint32_t    materialId;             // 4B
    uint32_t    selectionMask;          // 4B  -- selected/hovered/ghosted/isolated
    uint32_t    colorOverride;          // 4B  -- RGBA8 packed, 0 = no override
    uint32_t    clipPlaneMask;          // 4B  -- section plane enable bits
    uint32_t    flags;                  // 4B  -- assembly level, layer bits, style
    uint32_t    _padding[4];            // 16B -- pad to 128B
};
static_assert(sizeof(GpuInstance) == 128);
```

**flags layout (32 bits)**:

```
bits 0-15:   layerBits        (16 layers, 1 bit each)
bits 16-19:  displayStyle     (14 modes, 4 bits)
bits 20-23:  lineStyle        (8 modes: Solid/Dash/Dot/DashDot/DashDotDot/Phantom/Center/Hidden)
bits 24-27:  assemblyLevel    (depth in segment tree, 4 bits)
bits 28-31:  analysisOverlay  (Zebra/Iso/Curv/Draft, 4 bits)
```

**VisBuffer -> Entity + Topology resolution** (demands TM38.1, TM38.2):

```
VisBuffer[x,y].R32 -> instanceId -> GpuInstance.entityId -> ECS Entity
                                  -> meshletBaseIndex + primitiveId -> global triangle index
VisBuffer[x,y].G32 -> primitiveId -> TopoGraph::PrimitiveToFace() -> FaceId  (O(1) lookup)
                                  -> TopoGraph::FaceToEdges() -> EdgeId[]    (O(1) adjacency)
                                  -> TopoGraph::FaceToVertices() -> VertexId[] (O(1))

Full topology path from single pixel (no CPU re-intersection):
  pixel -> instanceId -> entityId (Body) -> primitiveId -> FaceId -> EdgeId -> VertexId
  + parametric UV on face via barycentric interpolation of vertex UVs

Persistent naming (demands TM38.3):
  TopoGraph stores kernel-provided persistent IDs (OCCT TNaming / Parasolid).
  Selection state maps to persistent name, survives model edits.

Deterministic rendering (demands TM38.4):
  TAA jitter sequence is deterministic (Halton). No random seeds.
  All GPU dispatches are deterministic (no non-deterministic atomics in final output).
  Identical scene + camera -> bit-exact framebuffer (required for golden image CI).

Compat visual regression thresholds (CI gate):
  Overall per-frame RMSE: <15% (VSM vs CSM, GTAO vs SSAO, TAA vs FXAA cause large global diff)
  HLR edge overlay:       <3% (line type, width, visibility must match exactly)
  PMI text + symbols:     <3% (MSDF rendering identical across tiers)
  Color Bar / Legend:      <1% (same color ramp LUT, only AA differs)
  CAE scalar mapping:     <3% (same ColorRampLUT, only interpolation filtering differs)
```

### 5.6 ClusterDAG & Streaming (Out-of-Core)

For ultra-large models (10B+ triangles exceeding VRAM):

```
.miki archive (LZ4 per-cluster)
     |
ChunkLoader (async IO + Vulkan 1.4 streaming transfer)
     |
OctreeResidency + LODSelector (GPU DAG cut optimizer)
     |
Decompressed meshlet pages -> Mesh Shader
     |
Missing cluster -> coarser ancestor (zero visual holes)
```

**Progressive rendering**: first frame renders coarse LOD within 100ms of file open; full detail streams in over subsequent frames.

**Camera-predictive prefetch**: velocity/acceleration/zoom-rate -> predict target LOD -> prefetch clusters before visible. Budget: prefetch consumes <=20% streaming bandwidth.

**LOD transition smoothing**: dithered fade (8-frame Bayer pattern, no overdraw cost) + optional vertex geomorphing (lerp parent/child position over 200ms).

**VRAM page management**: Cluster page pool and VSM physical page pool share a unified sparse binding strategy. Vulkan: `VkSparseBufferMemoryBindInfo` with 64KB/2MB page granularity — individual pages committed/evicted without reallocating the backing buffer. D3D12: `CreateReservedResource` + `UpdateTileMappings`. Avoids VMA sub-allocation fragmentation under streaming churn. `MemoryBudget` (Phase 4) tracks committed page count; `ResidencyFeedback` (Phase 4) drives eviction priority.

### 5.7 Meshlet Compression

| Technique                                      | Savings              |
| ---------------------------------------------- | -------------------- |
| 16-bit quantized positions (per-meshlet AABB)  | ~50% vertex data     |
| Octahedral normal encoding (2x8-bit)           | 83% normal data      |
| 8-bit local triangle indices (max 64 vertices) | 75% index data       |
| Delta encoding + variable-length packing       | Additional ~20%      |
| **Total**                                      | **~50% per-meshlet** |

Decompression in mesh shader -- zero CPU involvement.

### 5.8 Beyond-Nanite Optimizations (10B+ Triangle Target)

This section documents architectural decisions that **exceed UE5 Nanite's capabilities** specifically for CAD/CAE workloads at 10B+ triangle scale.

#### 5.8.1 Zero-Stall Async Cluster Streaming

Nanite's cluster streaming is synchronous per-frame: the LOD selector requests clusters, waits for disk/network IO, then renders. On cache miss, a coarser ancestor is rendered (correct but blurry).

miki uses a **3-queue streaming architecture** to eliminate stalls entirely:

```
Transfer Queue (Vulkan 1.4 dedicated):
  Runs continuously. Feeds from IO thread → staging ring → GPU cluster pages.
  No synchronization with graphics queue except timeline semaphore at page commit.

Graphics Queue:
  Reads only committed pages. Never waits for transfer.
  Missing cluster → coarser ancestor (zero visual holes, zero stall).

CPU Prefetch Thread:
  Camera velocity + acceleration + zoom-rate → predict target LOD 2-5 frames ahead.
  Submit prefetch requests to transfer queue at low priority (<=20% bandwidth).
  Result: >95% cache hit rate during smooth navigation.
  Turntable/flythrough: 100% hit rate (path fully predictable).
```

**Advantage over Nanite**: Nanite's streaming pipeline stalls on complex camera transitions (e.g., section plane sweep revealing hidden geometry). miki's predictive prefetch handles this because section plane animation trajectory is known in advance.

**GPU decompression acceleration**: Cluster pages arrive LZ4-compressed from disk. Decompression path (in degradation order):

1. **Hardware GDeflate** (`VK_NV_memory_decompression`, RTX 40+ / RDNA3+): memory controller-level decode, >12 GB/s on PCIe Gen5, zero compute unit usage. Requires GDeflate-compressed `.miki` archive variant.
2. **Compute GDeflate** (RTX 30+ / RDNA2+): async compute shader decode, ~6 GB/s, overlaps with graphics queue.
3. **CPU LZ4** (fallback): Coca async IO thread decodes to staging ring, DMA to VRAM. ~2-3 GB/s.

Feature-detected at device init. Archive format supports dual-compressed clusters (GDeflate + LZ4 side-by-side) for zero-overhead fallback — `ChunkLoader` reads the appropriate variant per page.

**ReBAR fast-path** _(opportunistic)_: When `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | HOST_VISIBLE_BIT` is available with >256MB BAR, per-frame uniform updates (CameraUBO, dirty GpuInstance patches) bypass StagingRing — CPU writes directly to VRAM via PCIe MMIO. Timeline semaphore ring prevents write-while-read. Value: marginal for current uniform volumes but enables future real-time editing (displacement brush, live constraint solving) where sub-ms CPU→GPU latency is critical.

#### 5.8.2 Wavelet Mesh Compression (Phase 6b+)

Beyond standard meshlet compression (§5.7, ~50% savings), miki targets **wavelet-based mesh compression** for an additional 30-50% reduction:

| Level          | Technique                                      | Compression   | Decode Cost              |
| -------------- | ---------------------------------------------- | ------------- | ------------------------ |
| L1 (Phase 6b)  | Standard meshlet compression (§5.7)            | ~50%          | Mesh shader ALU          |
| L2 (Phase 14+) | Wavelet coefficient encoding (Hou et al. 2024) | ~70-80% total | Async compute pre-decode |

Wavelet approach: encode meshlet vertex positions as **low-frequency base + high-frequency wavelet coefficients**. CAD models are ideal candidates — large flat/cylindrical regions have near-zero high-frequency coefficients (extreme sparsity).

**Decode strategy: Compute Pre-Decode (not Mesh Shader inline)**. Inverse DWT runs in a dedicated async compute pass immediately after cluster streaming upload — NOT inside the Mesh Shader. Rationale: Mesh Shader LDS is already heavily committed to vertex/primitive output payload; inserting DWT decode buffers (~2-4KB/workgroup) would reduce GPU occupancy by 25-50%. The async compute pre-decode pass writes standard 16-bit quantized positions to a staging buffer, which the Mesh Shader reads as usual. Async compute overlaps with graphics queue depth pre-pass → zero frame-time overhead.

VRAM impact @10B triangles: standard compression = ~30GB resident → wavelet = ~15-20GB → fits in 24GB GPU with headroom.

#### 5.8.3 Perceptual LOD Selection

Nanite uses a fixed screen-space error metric (projected sphere error < 1px). This is geometry-agnostic — a silhouette edge and a flat interior face get the same LOD.

miki adds **perceptual weighting** to the LOD selector:

```
LOD_error = base_screen_error * perceptual_weight

perceptual_weight = max(
  silhouette_factor,    // 1.0 at silhouette, 0.3 at interior (from normal cone)
  curvature_factor,     // high curvature → preserve detail
  selection_factor      // selected entity → force max LOD within 2m radius
)
```

Effect: flat interior faces simplify 3× more aggressively than silhouette edges. For a typical CAD assembly (70% flat faces), this reduces visible meshlet count by ~25% with zero perceptual quality loss.

Implementation: Task shader evaluates `perceptual_weight` per-instance using precomputed per-meshlet curvature bound (stored in MeshletDescriptor, 4 bytes). Total overhead: ~2 ALU per meshlet in task shader.

#### 5.8.4 Software GI without RT Hardware

UE5 Lumen provides GI without RT hardware via mesh SDF + screen-space probes. miki's current design requires RT hardware for GI (#22 RT GI, #85 DDGI).

For T2/3/4 (no RT), miki can add a **screen-space diffuse GI approximation** as a fallback:

```
SSGI (screen-space global illumination):
  Input: HDR color (previous frame) + depth + normals
  Method: short-range screen-space ray march (same technique as SSR #48 but for diffuse)
  8 random directions per pixel, half-res, temporal accumulation
  Cost: <2ms @4K (half-res, same infrastructure as GTAO #15)
  Quality: inferior to RT GI but far superior to IBL-only ambient
```

This would be a new Pass #89 (extended, conditional, T2/3/4 only). Budget: <2ms. Reuses SSR Hi-Z march infrastructure.

### 5.9 Layer, Style & Attribute Pipeline

#### 5.9.1 Attribute Resolution (CPU)

`AttributeResolver` resolves each visual attribute per-segment using a **5-level priority chain** (highest wins):

| Priority | Source           | Example                                                                 |
| -------- | ---------------- | ----------------------------------------------------------------------- |
| 1        | ConditionalStyle | "All cylindrical faces -> blue"                                         |
| 2        | Explicit         | Per-segment override set by user                                        |
| 3        | Layer Override   | CadSceneLayer.color / .lineStyle / .transparency                        |
| 4        | Inherited        | Walk Segment.parent chain upward                                        |
| 5        | Scene Default    | CadScene::sceneDefaults\_ (color=gray, transparency=0, lineStyle=solid) |

Resolution cached per-segment per-key with dirty invalidation. GPU sees only flat, pre-resolved per-instance data.

#### 5.9.2 DisplayStyle -> Pass Activation Matrix

DisplayStyle determines which render passes execute for an instance:

| DisplayStyle    | Shader Tier | Bucket 1 (Opaque)            | Bucket 2 (OIT)               | Bucket 3 (Edge)                            | Additional          |
| --------------- | ----------- | ---------------------------- | ---------------------------- | ------------------------------------------ | ------------------- |
| Shaded          | B           | All opaque faces             | Transparent faces            | --                                         | Simplified PBR      |
| ShadedEdges     | B           | All opaque faces             | Transparent faces            | Silhouette+boundary+crease                 | PBR + edge          |
| Wireframe       | A           | Skip                         | Skip                         | All edges                                  | Edge-only           |
| HLR             | A           | Skip                         | Skip                         | Visible(solid)+hidden(dashed)              | ISO 128             |
| HLR_VisibleOnly | A           | Skip                         | Skip                         | Visible edges only                         | Hidden suppressed   |
| X-Ray           | B           | Skip                         | All faces (forced alpha=0.3) | All edges                                  | OIT forced          |
| Ghosted         | B           | All faces (desat, alpha=0.5) | --                           | Silhouette                                 | Ambient only        |
| Realistic       | C           | GBuffer (full attribs)       | Transparent (LL-OIT)         | Silhouette                                 | Full PBR+IBL+VSM+AO |
| NoShading       | B           | All faces (flat color)       | Skip                         | --                                         | Layer color only    |
| Matcap          | B           | All faces (sphere lookup)    | Skip                         | --                                         | 1 tex read          |
| Arctic          | B           | All faces (white albedo)     | Skip                         | --                                         | AO compute          |
| Pen             | A           | Skip                         | Skip                         | Silhouette+boundary+crease (black, jitter) | Pen style           |
| Artistic        | A           | Skip                         | Skip                         | Silhouette+boundary (soft pencil)          | Noise-modulated     |
| Sketchy         | A           | Skip                         | Skip                         | All edges (jitter+extension)               | Hand-drawn          |

Shader Tier: A = pos only (12B/vert), B = pos+normal (24B/vert), C = pos+normal+uv+tangent (56B/vert).

**Per-instance style mixing**: different instances in same frame can have different DisplayStyle. GPU cull compute reads displayStyle from GpuInstance.flags and routes each to correct bucket(s).

**DisplayStyle pass mutual exclusion** (budget constraint): the full post-process chain (SSR+Bloom+DoF+MotionBlur+CAS+ColorGrade = ~6.5ms) and HLR 4-stage (~4ms) are never simultaneously active at full cost. The following rules enforce this:

| Active DisplayStyle   | Post-Process Set                                     | HLR                              | Total Budget (Tier1 @4K) |
| --------------------- | ---------------------------------------------------- | -------------------------------- | ------------------------ |
| Shaded / ShadedEdges  | TAA + ToneMap + CAS                                  | None or silhouette-only (<0.5ms) | ~11ms → 90fps            |
| Wireframe / HLR / Pen | ToneMap + FXAA                                       | Full 4-stage HLR                 | ~13ms → 76fps            |
| Realistic             | SSR + Bloom + DoF + TAA + ToneMap + CAS + ColorGrade | Silhouette-only (<0.5ms)         | ~16ms → 62fps            |
| X-Ray / Ghosted       | TAA + ToneMap + OIT                                  | None                             | ~12ms → 83fps            |
| Arctic / Matcap       | ToneMap + AO                                         | None                             | ~10ms → 100fps           |

Realistic + full HLR + full post is architecturally possible but exceeds 16.7ms — the UI should prevent this combination or warn the user. Per-viewport DisplayStyle determines which conditional RenderGraph nodes activate.

#### 5.9.3 Line Type & Line Weight

Edge rendering reads line style from:

| Source                           | Priority                       | Encoded In               |
| -------------------------------- | ------------------------------ | ------------------------ |
| Per-segment lineStyle attribute  | Resolver priority 1-5          | GpuInstance.flags[20:23] |
| Edge classification auto-mapping | Fallback when lineStyle = Auto | ISO 128 rule table       |

**ISO 128 auto-mapping**:

| Edge Type        | Line Type        | Line Weight |
| ---------------- | ---------------- | ----------- |
| Silhouette       | Continuous       | 0.7mm       |
| Visible boundary | Continuous       | 0.35mm      |
| Hidden edge      | Dashed (12:3)    | 0.18mm      |
| Center line      | Chain (24:3:2:3) | 0.25mm      |
| Symmetry line    | ChainDouble      | 0.25mm      |
| Dimension line   | Continuous thin  | 0.18mm      |

**LinePattern SSBO**:

```cpp
// 4B per segment — fixed-point length (0.01mm units), type + symbol index
struct LinePatternSegment {            // 4B, alignas(4)
    uint16_t length;                   // 0.01mm units (range 0-655.35mm, precision 0.01mm)
    uint8_t  type;                     // 0=dash, 1=gap, 2=symbol (P&ID/electrical/GIS)
    uint8_t  symbolIdx;                // symbol atlas index (type=2 only), 0 otherwise
};

// 80B = 5×16B per pattern entry — GPU cache-friendly
struct LinePatternEntry {              // 80B, alignas(16)
    LinePatternSegment segments[16];   // 64B — max 16 per pattern period
    uint  segmentCount;                //  4B
    float totalLength;                 //  4B — precomputed sum (mm)
    float scale;                       //  4B — DPI-aware
    uint  flags;                       //  4B — bit 0: hasSymbols
};
```

Fragment shader samples pattern by edge parametric coordinate. Symbol line patterns (type=2) use MSDF SymbolAtlas.

#### 5.9.4 Halo / Gap Lines

T-junction clarity for technical illustration:

```
Per edge fragment:
  Sample depth in perpendicular direction (+/- haloWidth px)
  If crossing edge is closer -> discard (gap)
  If this edge is closer -> render white halo margin
  Cost: 2 extra depth samples per edge fragment, ~0.1ms @1M edges
```

#### 5.9.5 ConditionalStyle & StyleCompiler

Predicate-driven visual overrides evaluated at render-time:

```
ConditionalStyle examples:
  "FaceType == Cylindrical"  -> color=blue, transparency=0.3
  "DraftAngle < 3 deg"       -> color=red (DFM warning)
  "AssemblyLevel >= 2"       -> ghosted
  "SelectionSet('Machined')" -> highlight=orange
```

StyleCompiler resolves all active rules -> per-entity effective style stored in GpuInstance. ConditionalStyle has **highest priority** (level 1).

---

## 6. Material Stage

### 6.1 DSPBR Material Model

Full Dassault Enterprise PBR 2025 conformant:

```cpp
struct MaterialParameterBlock {           // 192B, alignas(16)
    // --- Base PBR (64B) ---
    float4   albedoFactor;                // 16B  base color x alpha (cut-out if alpha < cutoff)
    float    metallicFactor;              // 4B
    float    roughnessFactor;             // 4B
    float    normalScale;                 // 4B
    float    occlusionStrength;           // 4B   → 32B
    float3   emissiveFactor;              // 12B  HDR emission
    float    emissiveIntensity;           // 4B   → 48B
    float    clearcoatFactor;             // 4B   0=off
    float    clearcoatRoughness;          // 4B
    float    anisotropyStrength;          // 4B   0=isotropic
    float    anisotropyRotation;          // 4B   → 64B

    // --- Extended BxDF (48B) ---
    float3   sheenColor;                  // 12B  0=off (fabric)
    float    sheenRoughness;              // 4B   → 80B
    float3   subsurfaceColor;             // 12B  SSS diffusion
    float    subsurfaceRadius;            // 4B   mean free path mm → 96B
    float    transmissionFactor;          // 4B   0=opaque
    float    ior;                         // 4B   default 1.5
    float3   attenuationColor;            // 12B  volumetric absorption → 116B
    float    attenuationDistance;          // 4B   → 120B

    // --- Projection & Iridescence (8B) ---
    uint32_t projectionMode;              // 4B   0=UV,1=Triplanar,2=Box,3=Sphere,4=Cyl,5=Decal
    float    thinFilmThickness;           // 4B   nm, 0=off → 128B

    // --- Bindless texture indices (32B, 0xFFFFFFFF = no texture) ---
    uint32_t albedoTex;                   // 4B
    uint32_t normalTex;                   // 4B
    uint32_t metalRoughTex;               // 4B
    uint32_t emissiveTex;                 // 4B   → 144B
    uint32_t clearcoatTex;                // 4B
    uint32_t sheenTex;                    // 4B
    uint32_t transmissionTex;             // 4B
    uint32_t occlusionTex;                // 4B   → 160B

    // --- Reserved (32B) ---
    uint32_t displacementTex;             // 4B   (M3.11, 0xFFFFFFFF=none)
    float    displacementScale;           // 4B
    uint32_t _reserved[6];               // 24B  future: AO map, detail normal, etc. → 192B
};
static_assert(sizeof(MaterialParameterBlock) == 192);
```

**Performance note**: 192B per material is ~1.5x cache line (128B). For bindless array of 1K materials = 192KB — trivial VRAM. The extra 64B (vs 128B) adds zero per-pixel cost because material resolve reads individual fields, not the entire struct. Tile-based resolve coherence ensures the same material struct is cached across the tile.

### 6.2 BSDF Evaluation Order (Material Resolve Compute)

| #   | Layer                                            | Cost/px             | Skip Condition                 |
| --- | ------------------------------------------------ | ------------------- | ------------------------------ |
| 1   | Base PBR (Cook-Torrance GGX + Lambertian)        | ~20 ALU             | Always evaluated               |
| 2   | Multi-scatter (Kulla-Conty LUT 32x32 R16F)       | ~3 ALU + 1 tex      | Always on                      |
| 3   | Clearcoat (GGX, separate roughness)              | ~12 ALU + 1 tex     | clearcoatFactor==0             |
| 4   | Anisotropy (Ashikhmin-Shirley tangent/bitangent) | ~8 ALU              | anisotropyStrength==0          |
| 5   | Sheen (Charlie NDF)                              | ~10 ALU             | sheenColor==0                  |
| 6   | SSS (Burley profile, screen-space blur)          | Separate pass 0.5ms | No SSS materials visible       |
| 7   | Transmission (screen-space refraction)           | ~15 ALU + 1 tex     | transmissionFactor==0          |
| 8   | Emission (additive HDR, bypass lighting)         | ~2 ALU              | emissiveFactor==0              |
|     | **Total worst case**                             | ~70 ALU + 4 tex     | Typical CAD: base only ~20 ALU |

**Performance**: tile-based resolve (§5.4) enables per-tile feature detection. 90%+ tiles in CAD hit base-only fast path (single material, no optional BxDF layers).

**VGPR control via Vulkan Specialization Constants**: The mega-kernel uses `[[vk::constant_id(N)]]` booleans (`kHasClearcoat`, `kHasAnisotropy`, `kHasSheen`, `kHasSSS`, `kHasTransmission`, `kHasThinFilm`) to compile-time prune dead BSDF layers. At pipeline creation, the scene's active material feature mask determines which permutations are compiled. Typical CAD: 1-3 permutations (base / base+clearcoat / base+transparency). VGPR impact: full 8-layer ~96-112 VGPR (50-62% occupancy on RDNA3); base-only ~40-48 VGPR (100% occupancy). Permutation cache: `PipelineCache` (§Phase 3b) stores compiled permutations; second-launch cost <1ms. This approach avoids runtime if-branch divergence — the GPU compiler eliminates dead code entirely, yielding the same register pressure as a hand-written single-layer shader for base-only materials.

SSS tier split: Tier1 = separable screen-space blur (Burley); Tier2/3/4 = wrap-diffuse approximation (no extra pass).

Transmission tier split: Tier1 = screen-space ray march through depth; Tier2/3/4 = single-sample background grab.

### 6.3 Pass I/O Specifications (Material Stage)

#### Pass #10: Material Resolve — Detailed BSDF Pipeline

See §5.1 Pass #10 for resource formats. This section details the BSDF evaluation within the compute kernel.

| Stage              | Input Resource                         | Format                      | Read Pattern                      |
| ------------------ | -------------------------------------- | --------------------------- | --------------------------------- |
| VisBuffer lookup   | `VisBuffer`                            | R32G32_UINT                 | Per-pixel random                  |
| Instance lookup    | `GpuInstance[instanceId]`              | 128B struct (BDA)           | Coherent per-tile (same instance) |
| Material lookup    | `MaterialParameterBlock[materialId]`   | 192B struct (bindless SSBO) | Coherent per-tile (same material) |
| Vertex reconstruct | `MeshletVertexData`                    | Compressed (§5.7)           | 3 vertices per triangle (BDA)     |
| Texture sample     | `BindlessTextureTable[albedoTex]` etc. | Various (RGBA8, BC7, etc.)  | Per-pixel, filtered               |
| Kulla-Conty LUT    | `KullaContyLUT`                        | R16F 32x32                  | Per-pixel, bilinear               |

| Output          | Format                 | Content                                                                        |
| --------------- | ---------------------- | ------------------------------------------------------------------------------ |
| `GBuffer.RT0`   | RGBA8_UNORM            | albedo.rgb (sRGB), metallic (linear)                                           |
| `GBuffer.RT1`   | RGBA16F                | normal.xyz (octahedral decoded to world), roughness                            |
| `GBuffer.RT2`   | RG16F                  | motion vector (screen-space pixel delta)                                       |
| `GBuffer.RT3`   | RGBA8_UNORM (optional) | emission.rgb, per-material AO (only allocated if any material has emission/AO) |
| `GBuffer.Depth` | D32_SFLOAT             | Reverse-Z depth (shared with Pass #1)                                          |

---

## 7. Lighting Stage

### 7.1 GpuLight Data Model

```cpp
struct GpuLight {                          // 64 bytes, alignas(16)
    float3   position;                     // 12B  point/spot/area world-space
    float    range;                        // 4B   attenuation cutoff          → 16B
    float3   direction;                    // 12B  directional/spot normalized
    float    innerConeAngle;               // 4B   spot inner cos, 0=non-spot
                                           //       area light: reused as areaWidth   → 32B
    float3   color;                        // 12B  linear RGB radiance
    float    outerConeAngle;               // 4B   spot outer cos
                                           //       area light: reused as areaHeight  → 48B
    float    intensity;                    // 4B   lm (point/spot/area) or lx (dir)
    uint32_t type;                         // 4B   0=Dir,1=Point,2=Spot,3=AreaRect,4=AreaDisc,5=AreaTube
    uint32_t shadowIndex;                  // 4B   ShadowAtlas tile, 0xFFFFFFFF=none
    uint32_t flags;                        // 4B   castShadow|volumetric|animated  → 64B
};
static_assert(sizeof(GpuLight) == 64);
// Area light dimensions: areaWidth = innerConeAngle, areaHeight = outerConeAngle (union semantics)
// Spot lights use innerConeAngle/outerConeAngle as cone angles; area lights reuse as width/height.
```

Physical units: Directional = lux (lx), Point/Spot/Area = lumens (lm). Matches Filament/UE5/DSPBR convention.

### 7.2 Clustered Light Culling

**Tier1** -- GPU 3D froxel grid:

```
Grid: ceil(w/64) x ceil(h/64) x 32 depth slices (log2, Reverse-Z)
Pass 1 (compute): per-light project AABB -> atomicAdd to overlapping clusters
Pass 2 (deferred resolve): per-pixel read cluster -> iterate lights -> accumulate BRDF
```

| Tier  | Max Lights | Method                   | Budget     |
| ----- | ---------- | ------------------------ | ---------- |
| Tier1 | 4096       | GPU clustered 3D froxels | <0.3ms     |
| Tier2 | 256        | CPU sorted UBO           | <0.1ms CPU |
| Tier3 | 64         | CPU sorted UBO           | <0.1ms CPU |
| Tier4 | 256        | CPU sorted UBO           | <0.1ms CPU |

### 7.3 Pass I/O Specifications (Lighting + Shadow + AO)

#### Pass #12: VSM Render (Tier1)

| Item          | Specification                                                                                                                     |
| ------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| **Condition** | Tier1 only. Active when directional light has `castShadow=true`.                                                                  |
| **Input**     | `ShadowCasterList` SSBO (from GPU Culling, light-frustum variant)                                                                 |
|               | `VSMPageTable` SSBO: virtual-to-physical page mapping                                                                             |
|               | `VSMDirtyPageList` SSBO: pages invalidated by scene change                                                                        |
| **Output**    | `VSMPhysicalPages`: D32_SFLOAT array texture, 128x128 per page, 16K^2 virtual                                                     |
| **PSO**       | Graphics (mesh shader): depthTest=GreaterOrEqual, depthWrite=true, no color, front-face cull, depthBias=(2, 1.5), depthClamp=true |
| **Budget**    | <2ms (dirty pages only; static scene ~0ms)                                                                                        |

#### Pass #13: CSM Render (Tier2/3/4)

| Item          | Specification                                                                                                                |
| ------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| **Condition** | Tier2/3/4. Active when directional light has `castShadow=true`.                                                              |
| **Input**     | Shadow casters (CPU frustum-culled per cascade)                                                                              |
| **Output**    | `CSMCascadeAtlas`: D32_SFLOAT, 2-4 cascades (T2/T4: 4x2048^2, T3: 2x1024^2)                                                  |
| **PSO**       | Graphics (vertex): depthTest=GreaterOrEqual, depthWrite=true, no color, front-face cull, depthBias=(4, 2.0), depthClamp=true |
| **Budget**    | <2ms                                                                                                                         |

#### Pass #14: Shadow Atlas (Tier1/T2, point/spot/area)

| Item          | Specification                                                                               |
| ------------- | ------------------------------------------------------------------------------------------- |
| **Condition** | Active when any non-directional light has `castShadow=true`.                                |
| **Input**     | Per-light shadow caster geometry (frustum-culled per light)                                 |
|               | `ShadowAtlasTileAlloc` SSBO: LRU tile assignment per light                                  |
| **Output**    | `ShadowAtlas`: D32_SFLOAT, T1=8192^2, T2=4096^2 (single shared texture, per-light viewport) |
| **PSO**       | Same as #13 but per-light viewport/scissor                                                  |
| **Budget**    | <1ms (max 32 lights T1, 8 lights T2/3/4)                                                    |

#### Pass #15: GTAO (Tier1/T2)

| Item          | Specification                                                                                    |
| ------------- | ------------------------------------------------------------------------------------------------ |
| **Condition** | Tier1/T2. Disabled if RTAO (#17) is active and provides superior quality.                        |
| **Input**     | `DepthTarget` D32_SFLOAT (half-res downsample: R32_SFLOAT `ceil(w/2) x ceil(h/2)`)               |
|               | `GpuCameraUBO`: projection params for depth linearization                                        |
| **Output**    | `AOBuffer`: R8_UNORM, half-res. Bilateral upsample to full-res before deferred resolve reads it. |
| **Dispatch**  | Compute: `ceil(w/2/8) x ceil(h/2/8)` workgroups, 8x8 threads. 8 directions x 2 horizon steps.    |
| **Budget**    | <1ms @4K (half-res = 1920x1080 effective)                                                        |
| **Async**     | Candidate for async compute queue overlap with Material Resolve (#10).                           |

#### Pass #16: SSAO (Tier3/T4)

| Item          | Specification                                                         |
| ------------- | --------------------------------------------------------------------- |
| **Condition** | Tier3/T4 only (GTAO unavailable).                                     |
| **Input**     | `DepthTarget` D32_SFLOAT (full-res)                                   |
|               | `NoiseTexture`: RGBA8 4x4 random rotation vectors (tiled)             |
| **Output**    | `AOBuffer`: R8_UNORM, full-res. Separable Gaussian blur (2-pass).     |
| **Dispatch**  | Fragment (fullscreen triangle): 8-16 samples per pixel (T3=8, T4=16). |
| **Budget**    | <1ms                                                                  |

#### Pass #17: RTAO (Tier1, optional)

| Item          | Specification                                                                                      |
| ------------- | -------------------------------------------------------------------------------------------------- |
| **Condition** | Tier1 with RT hardware. Disabled if user prefers GTAO.                                             |
| **Input**     | `BLAS/TLAS` acceleration structure (from §15.4)                                                    |
|               | `DepthTarget` D32_SFLOAT + `GBuffer.RT1` normals                                                   |
| **Output**    | `AOBuffer`: R8_UNORM, full-res. 1spp + 8-frame exponential moving average.                         |
| **Dispatch**  | Compute (ray query): `ceil(w/8) x ceil(h/8)` workgroups, 8x8 threads. 1 short-range ray per pixel. |
| **Budget**    | <2ms @4K                                                                                           |

#### Pass #18: Deferred Resolve

| Item                | Specification              |
| ------------------- | -------------------------- |
| **Condition**       | Always active (all tiers). |
| **Input resources** |                            |

| Resource                               | Format                              | Binding                 |
| -------------------------------------- | ----------------------------------- | ----------------------- |
| `GBuffer.RT0`                          | RGBA8_UNORM                         | sampled texture (set 1) |
| `GBuffer.RT1`                          | RGBA16F                             | sampled texture (set 1) |
| `GBuffer.RT2`                          | RG16F                               | sampled texture (set 1) |
| `GBuffer.Depth`                        | D32_SFLOAT                          | sampled texture (set 1) |
| `AOBuffer`                             | R8_UNORM                            | sampled texture (set 1) |
| `VSMPhysicalPages` / `CSMCascadeAtlas` | D32_SFLOAT                          | sampled texture (set 2) |
| `ShadowAtlas`                          | D32_SFLOAT                          | sampled texture (set 2) |
| `GpuLight[]`                           | SSBO 64B/light                      | set 2                   |
| `ClusterGrid`                          | SSBO (T1) / UBO (T2/3/4)            | set 2                   |
| `IBL_SH`                               | UBO: 9x float4 (SH L2 coefficients) | set 0                   |
| `IBL_Specular`                         | TextureCube RGBA16F, 5 mip          | sampled (set 2)         |
| `BRDF_LUT`                             | R16G16F 256x256                     | sampled (set 2)         |
| `LTC_LUT1` / `LTC_LUT2`                | RGBA32F 64x64 (area lights)         | sampled (set 2)         |
| `GpuCameraUBO`                         | UBO 256B                            | set 0                   |

| **Output** | `HDRColor`: RGBA16F, viewport resolution |

| **Algorithm** |                                                                                                   |
| ------------- | ------------------------------------------------------------------------------------------------- |
| 1             | Reconstruct world position from depth (Reverse-Z: `pos = invViewProj * float4(uv*2-1, depth, 1)`) |
| 2             | Read material properties from GBuffer (albedo, normal, metallic, roughness)                       |
| 3             | Evaluate directional light(s): shadow lookup (VSM page table / CSM cascade select), BRDF eval     |
| 4             | T1: read cluster grid -> iterate point/spot/area lights per froxel                                |
|               | T2/3/4: iterate CPU-sorted light UBO array                                                        |
| 5             | Per light: attenuation (distance, cone), shadow (atlas lookup), BRDF (Cook-Torrance + Lambertian) |
|               | Area lights: LTC specular + analytic polygon diffuse                                              |
| 6             | IBL ambient: SH irradiance (diffuse) + pre-filtered env (specular, roughness-mip)                 |
| 7             | Multiply by AO                                                                                    |
| 8             | Add emission (bypasses lighting)                                                                  |
| 9             | Optional: add RT Reflections (#20), RT Shadows (#21), RT GI (#22) contributions                   |
| 10            | Write HDR output                                                                                  |

| **Dispatch** | T1: Compute `ceil(w/8) x ceil(h/8)` workgroups, 8x8 threads |
| | T2/3/4: Fragment (fullscreen triangle) |
| **Budget** | <1ms @4K with 100 lights (T1 clustered), <1.5ms @4K with 256 lights (T2 UBO) |

#### Pass #19: IBL Precompute (one-time)

| Item          | Specification                                     |
| ------------- | ------------------------------------------------- |
| **Condition** | Once per environment change. Not per-frame.       |
| **Input**     | HDRI equirectangular texture (RGBA16F or RGBA32F) |
| **Output**    |                                                   |

| Resource       | Format         | Size                               |
| -------------- | -------------- | ---------------------------------- |
| `IBL_Cubemap`  | RGBA16F        | 1024^2 per face, 6 faces           |
| `IBL_SH`       | 9x float4      | 144B UBO                           |
| `IBL_Specular` | RGBA16F, 5 mip | 1024^2 -> 16^2                     |
| `BRDF_LUT`     | RG16F          | 256x256 (once at init, analytical) |

| **Budget** | <10ms total (equirect->cube 2ms, SH 1ms, specular filter 5ms, BRDF LUT 1ms) |

### 7.4 IBL Pipeline

| Step                | Input                  | Output                                | Budget | Frequency           |
| ------------------- | ---------------------- | ------------------------------------- | ------ | ------------------- |
| Equirect -> Cubemap | HDRI equirect          | 1024-sq cubemap                       | <2ms   | Once per env change |
| Diffuse SH          | Cubemap                | 9 SH L2 coefficients                  | <1ms   | Once                |
| Specular pre-filter | Cubemap                | 5 mip levels (split-sum)              | <5ms   | Once                |
| BRDF LUT            | (analytical)           | 256x256 R16G16F                       | <1ms   | Once at init        |
| Skybox render       | Cubemap                | Fullscreen at infinite depth          | <0.1ms | Per frame           |
| Solid/gradient bg   | Push constant color(s) | Clear/gradient in tone-map            | ~0     | Per frame           |
| Ground shadow plane | Shadow map             | Shadow-only y=0, configurable opacity | <0.1ms | Per frame           |

### 7.5 Area Light -- LTC

Specular: LTC integration using 2x RGBA32F 64x64 LUT (128KB total, pre-computed once). Diffuse: analytic polygon integration. Per area light per pixel: ~15 ALU + 2 texture fetches.

---

## 8. Shadow Stage

Shadow passes #12--#14 are detailed in §7.3. This section provides supplementary architecture notes.

**VSM page lifecycle** (Tier1, Pass #12):

```
Frame N:
  1. Page Request (compute): project visible geometry bounding spheres -> mark needed virtual pages
  2. Page Alloc (CPU callback or compute): LRU allocator assigns physical tiles from pool
     - Pool: 128x128 physical pages, each 128x128 D32F texels
     - New page: allocate from free list. Full: evict least-recently-sampled page.
  3. Shadow Render (mesh shader): render ONLY dirty pages (newly allocated or geometry changed)
     - Static scenes: 0 pages rendered after first frame -> ~0ms shadow cost
  4. Deferred resolve (#18): sample VSM page table (uint32 virtual->physical indirection)
     - Page table stored as R32_UINT 2D texture matching virtual grid dimensions
```

**CSM cascade split** (Tier2/3/4, Pass #13): logarithmic split lambda=0.7 (blend between linear and log). Cascade overlap: 10% border for smooth transition. Cascade selection in deferred resolve: per-pixel depth -> cascade index -> atlas viewport UV.

**Shadow Atlas tile allocation** (Pass #14): `ShadowAtlasTileAlloc` SSBO tracks per-light `{tileOffset, tileSize, lastUsedFrame}`. CPU-side LRU on light visibility change. Static lights retain tiles indefinitely. Moving lights re-render each frame.

---

## 9. Transparency Stage

### 9.1 Pass I/O Specifications (Transparency)

#### Pass #25: LL-OIT Insert (Tier1/2)

| Item          | Specification                                                                                                          |
| ------------- | ---------------------------------------------------------------------------------------------------------------------- |
| **Condition** | Bucket 2 non-empty (transparent/X-Ray instances exist). Tier1/2 only.                                                  |
| **Input**     | `TransparentBucket` SSBO (from Macro-Binning #4)                                                                       |
|               | `GpuInstance[]` SSBO: per-instance colorOverride, alpha                                                                |
|               | `MaterialParameterBlock[]` SSBO (192B): albedoFactor.a for per-material transparency                                   |
|               | `DepthTarget` D32_SFLOAT (read-only, reject behind opaque)                                                             |
| **Output**    | `OIT_NodePool` SSBO: `{RGBA16F color, float depth, uint32 next}` = 16B/node                                            |
|               | `OIT_HeadPointers` image: R32_UINT, viewport resolution (per-pixel head index)                                         |
|               | `OIT_TileCounters` SSBO: uint32 per 16x16 tile (per-tile node allocation counter, eliminates global atomic contention) |
| **PSO**       | Graphics: cullMode=None, depthTest=GreaterOrEqual (read-only), depthWrite=false, no color attachment (UAV)             |
| **Pool size** | `min(16M, VRAM_total * 2%)` nodes. 8GB GPU -> 10M nodes (160MB). 24GB -> 32M (512MB).                                  |
| **Budget**    | <1ms                                                                                                                   |

#### Pass #26: LL-OIT Resolve (Tier1/2)

| Item          | Specification                                                                                                               |
| ------------- | --------------------------------------------------------------------------------------------------------------------------- |
| **Condition** | Active when #25 was active.                                                                                                 |
| **Input**     | `OIT_NodePool` SSBO + `OIT_HeadPointers` R32_UINT image                                                                     |
| **Output**    | `OIT_Color`: RGBA16F, viewport resolution (sorted composited transparency)                                                  |
| **Dispatch**  | Compute: `ceil(w/8) x ceil(h/8)` workgroups. Per-pixel: walk linked list, insertion sort (<=16 layers) or merge sort (>16). |
| **Budget**    | <1ms @8 avg layers                                                                                                          |

#### Pass #27: Weighted OIT (Tier3/4)

| Item                      | Specification                                                                                        |
| ------------------------- | ---------------------------------------------------------------------------------------------------- |
| **Condition**             | Tier3/4 only. Replaces #25+#26.                                                                      |
| **Input**                 | Transparent geometry (same as #25) + `DepthTarget` D32_SFLOAT                                        |
| **Output (accumulation)** | `WOIT_Accum`: RGBA16F (blend: ONE/ONE additive)                                                      |
|                           | `WOIT_Revealage`: R8_UNORM (blend: ZERO/ONE_MINUS_SRC_ALPHA)                                         |
| **Output (resolve)**      | `WOIT_Color`: RGBA16F (fullscreen triangle resolve: ONE_MINUS_SRC_ALPHA/SRC_ALPHA)                   |
| **PSO**                   | Graphics: cullMode=None, depthTest=GreaterOrEqual (read-only), depthWrite=false, 2 color attachments |
| **Budget**                | <1ms                                                                                                 |

### 9.2 OIT Overflow Protection

```
Per-pixel:  imageAtomicExchange -> linked list
Node pool:  SSBO {color RGBA16F, depth float, next uint32} = 16B/node
Capacity:   min(16M, VRAM_total * 2%) nodes -- scales with GPU memory (e.g., 8GB -> 10M nodes/160MB, 24GB -> 32M/512MB)
Resolve:    insertion sort <=16 layers, merge sort >16
Hybrid:     linked-list <=8 (CAD), weighted >8 (CAE)
X-Ray mode: correct depth-sorted transparency
```

**Per-tile node allocation** (eliminates global atomic contention):

Node pool is partitioned into per-tile sub-pools (one uint32 counter per 16x16 tile). At 4K: `ceil(3840/16) × ceil(2160/16) = 240 × 135 = 32,400 tile counters`. Each tile allocates from its own sub-pool region via `atomicAdd(tileCounter[tileId], 1)`. Benefits:

- L2 atomic contention distributed across ~32K addresses (vs 1 global address)
- X-Ray mode with 10M instances: each tile processes ~255 pixels × avg 5-10 layers = ~1,300-2,550 atomics/tile — trivial for per-tile L2 cache line
- Sub-pool overflow: tile spills to global overflow region (rare, <0.1% of tiles in practice)

**Overflow protection** (mandatory):

```glsl
uint tileId = (gl_FragCoord.x / 16) + (gl_FragCoord.y / 16) * tilesPerRow;
uint localIdx = atomicAdd(tileCounter[tileId], 1);
uint nodeIndex = tileBaseOffset[tileId] + localIdx;
if (localIdx >= tilePoolSize[tileId]) {
    // Tile sub-pool exhausted — fallback to weighted OIT for this fragment
    atomicAdd(tileCounter[tileId], -1);
    weightedOitAccumulate(color, depth, alpha); // graceful degradation
    return;
}
// Normal linked-list insertion into OIT_NodePool[nodeIndex]
```

**Adaptive pool**: CPU reads peak usage via readback. Grow if >80%, shrink if <30% for 60 frames. Clamp: 4M-64M nodes.

### 9.2 Weighted OIT (Tier3/4)

McGuire-Bavoil accumulation. 2 attachments: accum (RGBA16F, ONE/ONE additive) + revealage (R8, ZERO/ONE_MINUS_SRC_ALPHA). Single resolve pass.

### 9.3 PSO Parameters (OIT)

| Parameter     | Tier1 (LL-OIT)                        | Tier2/3/4 (Weighted)            |
| ------------- | ------------------------------------- | ------------------------------- |
| cullMode      | None (both faces)                     | None                            |
| depthTest     | GreaterOrEqual (read-only, Reverse-Z) | GreaterOrEqual (read-only)      |
| depthWrite    | false                                 | false                           |
| Insert blend  | No color attachment (UAV)             | Additive (accum) + revealage    |
| Resolve blend | SRC_ALPHA / ONE_MINUS_SRC_ALPHA       | ONE_MINUS_SRC_ALPHA / SRC_ALPHA |

---

## 10. CAD Domain Passes

### 10.0 Pass I/O Summary (CAD #28--#37)

| #   | Pass              | Condition                            | Key Input                                                                                                | Key Output                                                                                                                                                     | Budget      |
| --- | ----------------- | ------------------------------------ | -------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------- |
| 28  | HLR Classify      | Bucket 3 non-empty                   | `SceneBuffer` SSBO + `TopoGraph` adjacency SSBO (BDA) + `GpuCameraUBO`                                   | `EdgeBuffer` SSBO: `EdgeDescriptor` 32B `{float v0[3], uint32 packed(edgeType:8\|lineType:8\|lineWeight:8\|flags:8), float v1[3], uint32 instanceId}` per edge | <1ms @10M   |
| 29  | HLR Visibility    | #28 output exists                    | `EdgeBuffer` SSBO + `HiZPyramid` R32_SFLOAT                                                              | `VisibleEdgeBuffer` SSBO + `HiddenEdgeBuffer` SSBO (with `[t0,t1]` intervals)                                                                                  | <1.5ms @10M |
| 30  | HLR Render        | #29 output exists                    | `VisibleEdgeBuffer` + `HiddenEdgeBuffer` + `LinePattern[]` SSBO                                          | Edge color RGBA8 (SDF AA quads, Scene layer)                                                                                                                   | <1.5ms @5M  |
| 31  | Section Plane     | `clipPlaneMask != 0` on any instance | `GpuInstance[].clipPlaneMask` + `ClipPlane[8]` push constant (float4 equation each) + `DepthTarget` D32F | Clipped scene + stencil cap D32F/S8 + hatch fill RGBA8                                                                                                         | <0.5ms      |
| 32  | Ray Pick          | Pick request pending                 | T1: `BLAS/TLAS` + pick ray (push constant). T2/3/4: CPU BVH                                              | `HitBuffer` SSBO: `{uint32 instanceId, primitiveId, float2 bary, float tHit}`                                                                                  | <0.5ms T1   |
| 33  | Lasso Pick        | Lasso request pending                | `PolygonVertices` SSBO (screen-space) + `VisBuffer` R32G32_UINT                                          | `SelectionMask` R8_UNORM image + `SelectedEntityList` SSBO                                                                                                     | <1.5ms @4K  |
| 34  | Boolean Preview   | Preview active                       | `DepthLayers[8]` D32F array (depth peeling, 8 passes)                                                    | `CSG_Composite` RGBA16F (Preview layer)                                                                                                                        | <16ms       |
| 35  | Draft Angle       | Analysis overlay active              | `GBuffer.RT1` normals RGBA16F + push(`pullDirection` float3)                                             | Per-face angle R16F -> color map via LUT (Analysis overlay)                                                                                                    | <1ms @1M    |
| 36  | GPU Measurement   | Measurement request                  | T1: `BLAS/TLAS` + query points (BDA, float64/DS). T2/3/4: CPU BVH                                        | `MeasurementResult` SSBO: `{float64 value, uint32 type}` -> Overlay viz (#64)                                                                                  | <2ms T1     |
| 37  | Explode Transform | Explode active                       | `AssemblyHierarchy` SSBO + push(`explodeFactor` float, `explodeCenter` float3)                           | Modified `GpuInstance[].worldMatrix` in SceneBuffer (compute write)                                                                                            | <0.1ms      |

### 10.1 GPU Hidden Line Removal (HLR)

**ISO 128 compliant** edge rendering. 4-stage pipeline:

```
Stage 1: Edge Classify (compute)
  Input:  SceneBuffer (GpuInstance[]), vertex positions (BDA), adjacency (TopoGraph SSBO)
  Method: per-edge -- dot(faceNormal_L, viewDir) vs dot(faceNormal_R, viewDir)
          sign change -> silhouette; single-face -> boundary; dihedral > threshold -> crease
  Output: EdgeBuffer SSBO — EdgeDescriptor 32B per edge {float v0[3], uint32 packed, float v1[3], uint32 instanceId}
          Classify resolves vertex indices to world positions via BDA; downstream avoids random BDA fetches.
          faceL/faceR consumed only during classify (from TopoGraph), not stored in EdgeDescriptor.
          paramT intervals written by visibility pass into separate VisibleEdgeBuffer/HiddenEdgeBuffer.
  Cost:   ~1ms @10M edges

Stage 2: Visibility (compute)
  Input:  EdgeBuffer, HiZ pyramid (from depth pre-pass)
  Method: per-edge ray-march along screen-space projection; sample HiZ at each step
          edge depth > HiZ depth + bias -> hidden segment; else -> visible
          segment splitting: single edge may produce multiple visible+hidden sub-segments
  Output: VisibleEdgeBuffer, HiddenEdgeBuffer (with parametric [t0, t1] intervals)
  Cost:   ~1.5ms @10M edges
  Tier3 fallback: depth buffer direct sample (no HiZ mip chain)

Stage 3: Visible Edge Render (task/mesh or vertex shader)
  Tier1: task shader per-edge amplification (1 edge -> 1 quad)
         mesh shader expands to screen-space quad (2 tri, half-width from lineWeight)
         fragment: SDF coverage (smooth AA) + solid line pattern
  Tier2/3/4: CPU pre-pass expands edges to quads in vertex buffer
             vertex shader projects + offsets; fragment identical to Tier1
             single DrawIndexed (all edges batched)
  Output: edge color RGBA8
  Cost:   ~0.7ms @5M visible edges (Tier1), ~4ms @1M edges (Tier2/3/4)

Stage 4: Hidden Edge Render (same pipeline as Stage 3)
  Fragment: SDF coverage + dash pattern from LinePattern SSBO
  Output: same edge RGBA8 (alpha-blended, dimmer)
  Cost:   ~0.8ms @5M hidden edges
```

Total: **<4ms @10M edges** (Tier1 RTX 4070). Compat: **<12ms @1M edges**.

### 10.2 Section Plane & Volume

```
Section Plane Pipeline:
  Input:  up to 8 clip planes (AND/OR boolean), per-instance clipPlaneMask
  Method: per-fragment clip test (dot product vs plane equation)
          stencil capping: watertight cap surface via stencil increment/decrement
          contour extraction on cut face
          ISO 128 hatch pattern library (12+ patterns: steel, aluminum, rubber, concrete, wood, copper)
  Output: clipped scene + cap surface + hatch fill
  Cost:   <0.5ms

Section Volume:
  OBB / Cylinder / Boolean clip
  Per-fragment inside-volume test (6 dot-products for OBB)
  Multi-volume boolean (AND/OR/SUBTRACT)

Animated Section: plane position/normal updated per-frame via push constant (CPU animation timeline)
Explode Lines: original-to-exploded trace lines as SDF lines in Overlay layer
Partial Explosion: per-instance explosion bit in GpuInstance.flags
```

### 10.3 Ray Picking

6 interaction modes (3 shapes x 2 penetration):

|       | No-Drill (front-most)                    | Drill (all layers)                |
| ----- | ---------------------------------------- | --------------------------------- |
| Point | VisBuffer single-pixel readback (0.05ms) | RT ray query multi-hit (<0.5ms)   |
| Box   | VisBuffer mask + collect + dedup (<1ms)  | 3-stage GPU volume culling (<3ms) |
| Lasso | Lasso mask + VisBuffer collect (<1.5ms)  | 3-stage GPU volume culling (<5ms) |

Drill box/lasso uses **3-stage GPU volume culling** (not RT):

```
1. Instance cull (AABB project -> screen rect test) -- 0.1ms @100K instances
2. Meshlet cull (bounding sphere project -> mask overlap) -- 0.3ms @1M meshlets
3. Triangle collect (vertex project -> mask test) -- 0.5ms @100K surviving tri
```

### 10.4 Boolean Preview

Depth peeling CSG: render N=8 depth layers -> composite boolean result in compute. Tier1 only. <16ms.

### 10.5 Draft Angle Analysis

Single compute dispatch: per-face `dot(normal, pullDirection)` -> traffic-light color map. <1ms @1M tri. Output composited as analysis overlay.

### 10.6 GPU Measurement

```
Compute shader (ray query on Tier1, CPU BVH on Tier2/3/4):
  Point-to-point: Euclidean distance from 2 picked points
  Wall thickness: bidirectional ray-cast (point, +/-normal) -> min hit distance
  Clearance: min distance between two bodies (BVH nearest-point)
  Angle: dihedral from face normals or edge tangents
  Radius: circle fit on circular edge

  Output: MeasurementResult[] -> rendered in Overlay layer (SDF lines + MSDF text)
  Tier1 RT: <0.5ms per measurement
  Tier2/3/4 CPU: <5ms per measurement
```

### 10.7 Explode Transform

Compute pass: read assembly hierarchy -> apply per-instance explode vector to GpuInstance.worldMatrix. <0.1ms. Partial explosion via per-instance flag.

---

## 11. CAE Domain Passes

All CAE passes are conditional RenderGraph nodes composited into the Scene layer. Zero cost when no CAE data loaded.

### 11.1 Pass I/O Summary (CAE #38--#42, Point Cloud #43--#44)

| #   | Pass                | Condition                  | Key Input Format                                                                                | Key Output Format                                                           | Budget        |
| --- | ------------------- | -------------------------- | ----------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------- | ------------- |
| 38  | FEM Mesh            | CAE model loaded           | `ElementMesh` VB (per-element connectivity) + `ScalarArray` SSBO (R32F per node/element)        | Colored elements RGBA8 (Scene layer, same PSO as opaque bucket)             | <1ms @1M elem |
| 39  | Scalar/Vector Field | Scalar/vector data active  | `ScalarArray` SSBO (R32F) or `VectorArray` SSBO (float3) + `ColorRampLUT` R8G8B8A8 1D 256-texel | Color-mapped mesh RGBA8 / instanced arrow glyphs (scale+color by magnitude) | <1ms          |
| 40  | Streamline          | Vector field + seed points | `VectorField` SSBO (float3 per cell) + `SeedPoints` SSBO                                        | RK4 tube geometry VB (compute generate) -> instanced draw                   | <2ms          |
| 41  | Isosurface          | Scalar volume + threshold  | `ScalarVolume` 3D texture R32F + push(`isoValue` float)                                         | Triangle mesh VB+IB (Marching Cubes compute -> append buffer)               | <5ms          |
| 42  | Tensor Glyph        | Tensor data active         | `TensorData` SSBO (`float[6]` Voigt per element) + eigendecomp compute                          | Instanced ellipsoids (axes=principal dirs, radii=eigenvalues)               | <2ms          |
| 43  | Point Cloud Splat   | Point cloud loaded         | `GpuPoint[]` SSBO (16B/pt) + octree LOD nodes                                                   | Disc SDF quads + D32F depth (T1: task/mesh, T2/3/4: instanced)              | <2ms @10M     |
| 44  | Eye-Dome Lighting   | #43 active, normals absent | Point cloud `DepthTarget` D32F (separate from scene depth)                                      | `EDL_Shade` R8_UNORM (8-neighbor log2 depth gradient -> shade)              | <0.3ms        |

### 11.2 Color Map Infrastructure

Shared across CAE, point cloud, and analysis overlays:

```
1D LUT texture: 256 texels, R8G8B8A8
Built-in ramps: viridis, plasma, magma, inferno, coolwarm, rainbow, grayscale, diverging
User-uploadable: custom LUT via texture upload
Scalar normalization: linear or logarithmic, configurable min/max clamp
Color bar / legend: auto-generated in Widgets layer (Layer 4) with MSDF tick labels
```

---

## 12. Point Cloud Passes

### 12.1 Data Model

```cpp
struct GpuPoint {                           // 16B per point (quantized)
    uint16_t x, y, z;                       // relative to chunk AABB (49um precision in 3.2m chunk)
    uint8_t  r, g, b;                       // RGB color
    uint8_t  normal_oct;                    // octahedral normal, 0xFF=none
    uint16_t intensity;                     // scalar attribute
    uint16_t _pad;
};
```

### 12.2 LOD & Streaming

Octree-based hierarchical LOD (Potree 2.0 / 3D Tiles style). Screen-space error metric. Same ChunkLoader + camera-predictive prefetch as mesh streaming (§5.6). Shares VRAM budget (§20). Capacity: 10B+ points.

### 12.3 Splat Render Pass

| Tier      | Method                                                     | Budget        |
| --------- | ---------------------------------------------------------- | ------------- |
| Tier1     | Task/Mesh shader: point->quad, disc SDF + paraboloid depth | <2ms @10M pts |
| Tier2/3/4 | Instanced vertex shader: billboard quad expansion          | <4ms @5M pts  |

### 12.4 Eye-Dome Lighting

Compute pass: 8-neighbor depth gradient -> occlusion response (Boucheny 2009). Applied only to point cloud depth. Hybrid: EDL for points without normals, Lambertian N.L for points with. <0.3ms @4K.

### 12.5 Point Cloud Clipping

Reuses section plane infrastructure (§10.2). Per-point clip test in mesh/vertex shader. 1 dot product per plane per point. <0.1ms overhead for 8 planes.

---

## 13. Annotation & PMI Pass

### 13.1 PMI Rendering Pipeline

Dedicated RenderGraph pass in Scene layer:

```
PMI Pass:
  Input: PmiAnnotation[] SSBO, GlyphAtlas (MSDF), LeaderLine SSBO, DimensionStyle[]

  Rendering order (single pass, instanced):
    1. Leader lines: SDF line rendering (same tech as HLR S10.1)
       Arrow heads: pre-defined glyph from SymbolAtlas
    2. Datum symbols: instanced filled triangle + text letter
    3. GD&T frames: instanced rectangles + MSDF text compartments
    4. Surface finish: ISO 1302 MSDF glyphs
    5. Weld symbols: ISO 2553 MSDF glyphs
    6. Dimension text: MSDF + DimensionStyle (arrow type, text placement, tolerance)
    7. Balloons/callouts: circle/rectangle + leader + text

  Modes: screen-facing billboard (constant size) or model-space (distance-scaled)
  Annotation planes: dot(viewDir, annotationNormal) > threshold -> visible
  Budget: <0.1ms @1K annotations
```

**Text Engine Pipeline** (demands TX36.1-TX36.6):

```
Text processing pipeline (CPU -> GPU):
  1. HarfBuzz text shaping: ligatures, kerning, contextual alternates, mark positioning (TX36.1)
     Input: UTF-8 string + fontId + fontSize + script/language
     Output: positioned glyph sequence with advances + offsets
  2. Font fallback: ShapingCache checks primary font -> fallback chain (TX36.2)
     On miss: runtime glyph generation -> atlas upload (virtual page, LRU eviction)
  3. CJK support: 80K+ codepoints via virtual atlas paging (TX36.4)
     No pre-computation of full character set -- on-demand generation
  4. BiDi: FriBidi paragraph reorder before HarfBuzz shaping (TX36.5)
  5. GPU rendering:
     <48px: MSDF instanced quads (Phase 2)
     >=48px or print: GPU direct Bezier curve (Slug/GreenLightning, Phase 7b) (TX36.3)
     Mono-stroke SHX fonts for CNC engraving (TX36.6): SDF line pass
```

### 13.2 PMI Data Model

```cpp
struct PmiAnnotation {                      // 96B
    float3     anchor;                      // world-space
    float3     annotationNormal;            // view-direction filter
    float2     screenOffset;                // px offset from projected anchor
    uint32_t   type;                        // Dimension|GDT|SurfFinish|Weld|Datum|Balloon|Note|Markup
    uint32_t   styleIndex;                  // DimensionStyle array index
    uint32_t   textOffset, textLength;      // into text string buffer
    uint32_t   leaderIndex, leaderCount;    // LeaderLine SSBO range
    float4     color;                       // override (0 = style default)
    uint32_t   flags;                       // visible|selected|screenSpace
};
```

### 13.3 Analysis Overlays

Fullscreen fragment shader pass composited on GBuffer normals:

| Overlay        | Method                                          | Budget |
| -------------- | ----------------------------------------------- | ------ |
| Zebra Stripes  | `step(fmod(dot(N,V)*freq, 1), 0.5)`             | <0.1ms |
| Isophotes      | `dot(N,L)` contour lines                        | <0.1ms |
| Curvature Map  | Pre-computed quadric fit -> color LUT           | <0.2ms |
| Draft Angle    | `dot(normal, pullDir)` -> traffic-light         | <0.1ms |
| Deviation Map  | Nearest-point compute -> color map              | <0.5ms |
| Thickness Map  | GPU ray-based sampling -> color map             | <0.5ms |
| Interference   | Boolean intersection compute -> red translucent | <5ms   |
| Dihedral Angle | Per-edge dihedral from adjacency -> color map   | <1ms   |

Analysis overlays are orthogonal to DisplayStyle -- composited on top of any base mode. Activation adds/removes a single RenderGraph node.

### 13.4 Markup / Redline

SVG Overlay layer (Layer 5): freehand stroke, arrow, rectangle, circle, cloud, text note. SDF instanced rendering. <0.3ms @1K markups.

---

## 14. Post-Processing Chain

### 14.1 Pass Order & I/O Summary

```
HDR -> SSR -> Bloom -> DoF -> MotionBlur -> ToneMap -> TAA/FXAA -> CAS -> ColorGrade -> Vignette -> LDR
```

Each pass is a conditional RenderGraph node. Passes not applicable to current tier or disabled by user are skipped (zero cost). All post-process passes read/write to ping-pong RGBA16F (HDR) or RGBA8 (LDR) targets at viewport resolution unless noted.

| #   | Pass             | Condition                               | Input Format                                                   | Output Format                                      | Budget                 |
| --- | ---------------- | --------------------------------------- | -------------------------------------------------------------- | -------------------------------------------------- | ---------------------- |
| 48  | SSR              | T1/T2, Realistic mode                   | HDR RGBA16F + D32F + GBuffer.RT1 (normals) + GBuffer roughness | RGBA16F reflection (half-res, bilateral upsample)  | <1.5ms                 |
| 49  | Bloom            | All tiers, always (if Realistic/Shaded) | HDR RGBA16F                                                    | HDR RGBA16F (6-level Gaussian mip chain, additive) | <0.5ms T1, <0.8ms T3/4 |
| 50  | DoF              | T1/T2, Realistic only                   | HDR RGBA16F + D32F + push(focusDist, aperture, focalLen)       | RGBA16F (half-res gather, 16 samples)              | <1.5ms                 |
| 51  | Motion Blur      | T1/T2, animation active                 | HDR RGBA16F + GBuffer.RT2 (motion vec RG16F) + D32F            | RGBA16F (per-pixel directional, max 16 samples)    | <1.0ms                 |
| 52  | Tone Mapping     | All tiers, always                       | HDR RGBA16F + push(toneMapMode, exposure, vignetteParams)      | LDR RGBA8_SRGB (includes vignette + CA)            | <0.2ms                 |
| 53  | TAA              | T1/T2, always                           | LDR RGBA16F + GBuffer.RT2 (motion) + `TAA_History` RGBA16F     | AA RGBA16F (Halton jitter, YCoCg clamp)            | <0.5ms                 |
| 54  | Temporal Upscale | T1/T2, when render res < display res    | TAA output (67% res)                                           | Full-res RGBA16F (FSR 3.0 / DLSS 3.5)              | <1ms                   |
| 55  | FXAA             | T3/T4, always                           | LDR RGBA8 (luma in alpha)                                      | AA RGBA8                                           | <0.5ms                 |
| 56  | CAS Sharpen      | All tiers, always                       | Post-AA color (RGBA8 or RGBA16F)                               | Sharpened same format                              | <0.2ms                 |
| 57  | Color Grading    | All tiers, when LUT loaded              | LDR color + `ColorGradingLUT` sampler3D (32^3 RGBA8, 128KB)    | LUT-transformed LDR                                | <0.1ms                 |
| 58  | Outline          | All tiers, when enabled                 | D32F + GBuffer.RT1 (normals) + push(outlineColor, thickness)   | R8 edge mask -> composited with outline color      | <0.2ms                 |

Each pass is a **conditional RG node**: disabled pass = zero resource allocation, zero barrier, zero dispatch.

### 14.2 SSR -- Screen-Space Reflections

**Tier1/2 only**. Compute shader.

```
Input:  HDR color, depth (Reverse-Z), GBuffer normals, roughness
Method: Hi-Z ray march in screen space
  - Max 64 steps, early-out on miss
  - Roughness > 0.5 -> skip (too rough for coherent reflection)
  - Half-res trace + bilateral upsample to full-res
  - Temporal accumulation (blend with history, similar to TAA)
  - Fade at screen edges + depth discontinuities
Output: RGBA16F reflection color + confidence mask
Fallback: IBL cubemap for rays that miss screen
Budget: <1.5ms @4K
```

Tier3/4: SSR disabled -- IBL-only reflections.

### 14.3 Bloom

**All tiers**. Compute (T1/T2), Fragment (T3/T4).

```
1. Brightness extract: threshold HDR at luminance > 1.0 -> downsample 1/2
2. Gaussian blur chain: 6 mip levels (1/2 -> 1/4 -> ... -> 1/64)
   Each level: separable 9-tap Gaussian
3. Upsample chain: bilinear upsample + additive blend back up
4. Composite: lerp(HDR, HDR + bloom, bloomIntensity)

Parameters: bloomThreshold, bloomIntensity, bloomRadius (push constants)
Budget: <0.5ms (T1/T2), <0.8ms (T3/T4)
```

### 14.4 Depth of Field

**Tier1/2**. Compute.

```
Input:  HDR color, depth, focus distance, aperture, focal length
Method: Gather-based bokeh (Jimenez 2014, UE5-style)
  1. CoC compute: per-pixel circle of confusion from depth + lens params
  2. Near-field presort: separate near/far layers
  3. Gather: 16-sample disc kernel, weighted by CoC radius (half-res)
  4. Composite near + far + in-focus
Output: RGBA16F
Budget: <1.5ms @4K

Tier3/4: simplified 2-pass Gaussian blur weighted by CoC. <0.8ms.
Activation: Realistic mode or explicit user request only.
```

### 14.5 Motion Blur

**Tier1/2 only** (requires GBuffer motion vectors).

```
Input:  HDR color, per-pixel motion vectors, depth
Method: Per-pixel directional blur along motion vector (McGuire 2012)
  1. Tile max velocity: downsample motion to 1/20 -> max per tile
  2. Neighbor max: 3x3 dilation
  3. Gather: per-pixel along motion vector, depth-weighted (max 16 samples)
  4. Reconstruction: depth comparison to prevent BG bleeding into FG
Output: RGBA16F
Budget: <1.0ms @4K

Activation: turntable animation, kinematic playback, camera transitions. Not during interactive editing.
```

### 14.6 Tone Mapping

**All tiers**. Fragment shader (folded with vignette + chromatic aberration).

```
Tone map options (push constant selector):
  0: ACES Filmic (default) -- industry standard
  1: AgX (Blender 4.0+) -- superior chroma preservation
  2: Khronos PBR Neutral -- physically accurate
  3: Reinhard Extended -- adjustable white point
  4: Uncharted 2 (Hejl) -- filmic
  5: Linear -- HDR passthrough for HDR displays

Auto-exposure: histogram compute (<0.1ms) -> EV adjustment
sRGB output: linear -> sRGB gamma in final write

Vignette (folded, ~0 extra cost):
  output.rgb *= 1.0 - vignetteStrength * pow(length(uv - 0.5) * 2.0, vignetteFalloff)

Chromatic Aberration (folded, 2 extra tex fetches):
  Sample R/G/B at radially offset UVs

Budget: <0.2ms total
```

### 14.7 Anti-Aliasing

**TAA (Tier1/2)** -- compute:

```
Jitter: Halton(2,3), 8-sample cycle
History: RGBA16F, same resolution
Neighborhood clamp: YCoCg min/max (3x3 cross)
Motion rejection: discard history if |motionVector| > threshold
Reactive mask: gizmo/annotation/UI -> force current frame (no ghosting)
Output -> temporal upscaler: FSR 3.0 / DLSS 3.5
  Ultra Quality 77%, Quality 67%, Balanced 58%, Perf 50%
Budget: <0.5ms TAA + <1ms upscale
```

**FXAA (Tier3/4)** -- fragment:

```
FXAA 3.11, quality preset 29. Input: RGBA8 (luma in alpha). <0.5ms.
```

**MSAA (Tier2/4, optional)**: 4x, mutually exclusive with FXAA.

### 14.8 CAS Sharpen

**All tiers**. AMD FidelityFX CAS. Single compute pass post-TAA/FXAA. <0.2ms @4K.

### 14.9 Color Grading

**All tiers**. User-authored 3D LUT (32x32x32 RGBA8, 128KB) as sampler3D. Optional curves (shadow/midtone/highlight). <0.1ms.

### 14.10 Outline Post-Process

**All tiers**. Sobel filter on depth + normal discontinuities -> edge mask -> configurable outline color. <0.2ms. Separate from GPU HLR (§10.1): screen-space approximation for illustration style.

---

## 15. Ray Tracing Passes

**Tier1 only**. All RT features use `VK_KHR_ray_query` / DXR ray query in compute shaders. Rasterization remains for primary visibility (VisBuffer unchanged).

### 15.1 Pass I/O Specifications (Ray Tracing #20--#24)

All RT passes: Tier1 only, compute shader via `VK_KHR_ray_query` / DXR ray query. Conditional RG nodes.

| #   | Pass           | Condition               | Input                                                                                       | Output                                                            | Fallback         | Budget                           |
| --- | -------------- | ----------------------- | ------------------------------------------------------------------------------------------- | ----------------------------------------------------------------- | ---------------- | -------------------------------- |
| 20  | RT Reflections | T1+RT, roughness<0.3    | BLAS/TLAS + GBuffer (RT0 metallic, RT1 normal+rough, Depth D32F) + MaterialParameterBlock[] | `RT_Reflection` RGBA16F viewport-res                              | SSR (#48) -> IBL | <2ms @4K                         |
| 21  | RT Shadows     | T1+RT, per-light opt-in | BLAS/TLAS + GpuLight[] + GBuffer.Depth D32F                                                 | `RT_ShadowMask` R8_UNORM per-light (0=shadow, 1=lit)              | VSM/CSM          | <1ms/light                       |
| 22  | RT GI          | T1+RT, optional         | BLAS/TLAS + GBuffer.RT1 + Depth + `SH_ProbeCache` SSBO                                      | `RT_IndirectDiffuse` RGBA16F half-res + bilateral upsample        | IBL ambient      | <3ms                             |
| 23  | Path Tracer    | T1+RT, offline mode     | BLAS/TLAS + MaterialParameterBlock[] + GpuLight[] + GpuCameraUBO                            | `PT_Accumulation` RGBA32F (progressive, /sampleCount for display) | N/A (offline)    | ~50ms/spp                        |
| 24  | Denoiser       | T1, after #20-#23       | Noisy RT RGBA16F + GBuffer.RT0 (albedo) + GBuffer.RT1 (normal)                              | `Denoised` RGBA16F                                                | Raw noisy        | <10ms (OptiX), ~100ms (OIDN CPU) |

RT contributions (#20, #21, #22) are composited into Deferred Resolve (#18) step 9. Path Tracer (#23) bypasses the entire raster pipeline -- direct to tone map.

### 15.2 Path Tracer

Progressive path tracer for offline/preview quality:

```
Algorithm: unidirectional + NEE (Next Event Estimation)
Per pixel per frame:
  1. Primary ray from camera (no rasterization)
  2. Hit -> evaluate full DSPBR BSDF (§6.1)
  3. Russian roulette (min 3, max 16 bounces)
  4. NEE: explicit light sampling at each hit
  5. Accumulate radiance in RGBA32F buffer
Display: accumBuffer / sampleCount -> tone map -> present
Convergence: 64 spp usable, 256 spp clean, 1024 spp reference
Budget: ~50ms/spp @4K on RTX 4070

TLAS snapshot: path tracer copies TLAS at trace start to avoid blocking interactive pick queries during long accumulation runs.
```

### 15.3 Denoiser

```
NVIDIA OptiX Denoiser (Tier1 NVIDIA): <10ms @4K
Intel OIDN (CPU fallback): ~100ms @4K
Auxiliary inputs: albedo + normal -> denoiser
Result: 2-4 spp + denoise ~ 64 spp perceptual quality
```

### 15.4 BLAS/TLAS Management

```
Invariant: at pick/RT execution, BLAS/TLAS reflects same geometry version as VisBuffer.
```

**Three update modes** (selected per-body based on change type):

| Mode              | Trigger                                                                                      | Cost                  | Method                                                                                                                                                                                                                            |
| ----------------- | -------------------------------------------------------------------------------------------- | --------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Refit**         | Rigid transform only (translate/rotate/scale)                                                | <0.2ms per body       | `vkCmdBuildAccelerationStructuresKHR` with `VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR`. Preserves BVH topology, updates AABBs. Valid when vertex count and connectivity unchanged.                                          |
| **Rebuild**       | Topology change (boolean, fillet, chamfer, tessellation change, FEM deformation)             | 1-5ms per 1M tri body | Full BLAS build. Required when vertex count changes or mesh connectivity mutates.                                                                                                                                                 |
| **Async rebuild** | Bulk import (>100 bodies changed) or non-interactive (section plane animation, FEM timestep) | N/A (overlapped)      | BLAS build batches on async compute queue → signal timeline semaphore → graphics queue waits → TLAS rebuild. During build: pick returns `PickResult::Stale`, RT passes use previous-frame TLAS (graceful degradation, not stall). |

```
Interactive path (same command buffer, <10 bodies dirty):
  1. Refit dirty BLAS (<0.2ms total for rigid-only bodies)
  2. Rebuild dirty BLAS (1-5ms per topology-changed body — budget: max 2 bodies/frame inline)
  3. Rebuild TLAS (<0.1ms)
  4. Pipeline barrier (accel struct -> ray query)
  5. Geometry render pass
  6. RT queries use steps 1-3 accel struct

Budget overflow protection:
  If rebuild queue > 2 bodies/frame: spill to async compute queue (mode 3).
  Prevents frame-time spikes from large topology edits.

Bulk import / FEM deformation (async compute queue):
  BLAS build batches on async queue -> signal timeline semaphore
  Graphics queue waits semaphore -> TLAS rebuild -> pick valid
  During build: pick returns PickResult::Stale, RT uses previous-frame TLAS
  Stale duration: typically 1-3 frames for 1000-body assembly import
```

---

## 16. XR Stereo Pipeline

**Tier1 only**. OpenXR integration.

### 16.1 Single-Pass Stereo

```
XR render path:
  1. Acquire XR swapchain images (left + right eye)
  2. Per-eye: Reverse-Z projection with per-eye view matrix + IPD offset
  3. Single-pass stereo via SV_ViewID (Vulkan multiview extension)
     - VisBuffer write: 2x width, single dispatch
     - Material resolve: 2x width
     - Post-process: 2x width or VRS (foveated)
  4. Submit to XR compositor (OpenXR xrEndFrame)

Target: 90 fps per eye, <11.1ms total GPU

XR automatic quality reduction (mandatory for budget compliance):
  - Force VRS 2x2 peripheral (foveated via #59 + eye-tracking)
  - Disable DoF (#50) and Motion Blur (#51) — VR runtime handles reprojection
  - Reduce render resolution to 67% + FSR upscale (#54)
  - Disable SSR (#48) — IBL cubemap only
  - Estimated budget: ~8ms base + ~2ms multiview overhead = ~10ms → 100fps
```

### 16.2 Optimizations

| Optimization       | Method                                                                                                               |
| ------------------ | -------------------------------------------------------------------------------------------------------------------- |
| Foveated rendering | VRS image generator (pass #59) with eye-tracking input. Center: 1x1, periphery: 2x2 or 4x4. Saves ~30% shading cost. |
| Late-latch         | Camera pose updated after CPU frame start (reduce motion-to-photon)                                                  |
| Resolution scaling | Reduced render res + FSR upscale for headroom                                                                        |
| Pass-through AR    | Render with transparent background -> alpha blend over passthrough video                                             |

### 16.3 Controller / Hand Input

6-DOF controller -> camera navigation + gizmo interaction. Hand tracking: pinch=select, grab=orbit. Room-scale: physical walk -> scene navigation with configurable scale.

GPU impact: zero -- input is CPU-side, produces CameraUBO + GizmoState updates consumed by existing passes.

---

## 17. LayerStack Compositing

### 17.1 Layer Architecture

6 built-in layers, bottom-to-top alpha-blend compositing. Each layer has its own RenderGraph instance, color+depth targets.

| Order | Layer                | Content                                                                                 | Render Quality                                  | Depth                                                                 |
| ----- | -------------------- | --------------------------------------------------------------------------------------- | ----------------------------------------------- | --------------------------------------------------------------------- |
| 1     | **Scene**            | Full pipeline (all passes S5-S16)                                                       | Full (VSM, GTAO, PBR, post-process)             | Own D32F                                                              |
| 2     | **Preview**          | Transient: operation previews, placement ghosts, measurement feedback                   | Reduced (ambient + 1 dir light, no VSM, LL-OIT) | Reads Scene depth (explicit RG read dependency on Scene depth target) |
| 3     | **Overlay**          | Screen-space 3D: gizmo, compass, snap, grid, measurement viz                            | Unlit, SDF AA                                   | Own depth (active axis: force near)                                   |
| 4     | **Viewport Widgets** | In-viewport 2D controls: scale bar, coordinate readout, crosshair, mini-map, safe frame | 2D raster / MSDF                                | No depth                                                              |
| 5     | **SVG Overlay**      | Vector graphics: markup/redline (SDF stroke, stencil fill)                              | SDF / raster                                    | No depth                                                              |
| 6     | **HUD**              | Debug: FPS, progress bar, toast notifications, status bar, ImGui                        | ImGui backend                                   | No depth                                                              |

Up to 16 layers total (10 user-defined custom layers).

### 17.2 Compositor Pass

```
LayerStack Compositor (fragment shader):
  Input: 6 layer color+depth targets (sampled textures)
  Algorithm:
    For each pixel, bottom-to-top:
      Layer 1 (Scene): base color + depth
      Layer 2 (Preview): depth-aware alpha blend (preview respects scene depth via depth bias)
      Layer 3 (Overlay): depth-aware alpha blend (gizmo active axis: depth forced near = always visible)
      Layer 4-6: simple alpha blend (no depth test -- 2D)
  Output: final composited framebuffer
  Budget: <0.2ms @4K
```

### 17.3 Preview Layer Rendering

Transient objects (boolean preview, fillet preview, placement ghost, sketch rubber-band):

```
Preview Render Graph (per frame, if preview active):
  Tier1:
    1. Cull preview instances (trivial -- <100 meshlets)
    2. Task/Mesh -> small VisBuffer (1K x 1K)
    3. Simplified PBR: PhantomStyle albedo, fixed roughness 0.6
    4. Ambient + 1 directional (no shadow, no AO)
    5. LL-OIT (preview objects are semi-transparent)
    6. Silhouette edges only (no hidden edges)
    Cost: <2ms

  Tier2/3/4:
    Forward-lit + Weighted OIT + silhouette edges
    Cost: <4ms

PhantomStyle per operation:
  Fillet/Chamfer: green #4CAF50, 40% opacity
  Boolean subtract: red #F44336, 50%
  Boolean add: blue #2196F3, 40%
  Extrude/Revolve: yellow #FFC107, 35%
  Placement ghost: gray #9E9E9E, 60%
  Measurement: cyan #00BCD4, 100% (lines only)
  Error/interference: red #FF1744, 80%, pulse 0.5Hz
```

---

## 18. Overlay & Gizmo Passes

### 18.0 Pass I/O Summary (Overlay + Compositor #45, #47, #59--#65)

| #   | Pass                  | Condition               | Key Input Format                                                                                                                | Key Output Format                                                                     | Budget     |
| --- | --------------------- | ----------------------- | ------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- | ---------- |
| 45  | PMI Render            | PMI data loaded         | `PmiAnnotation[]` SSBO (96B each) + `GlyphAtlas` RGBA8 (MSDF) + `LeaderLine[]` SSBO + `DimensionStyle[]` SSBO                   | Instanced text+leaders+symbols RGBA8 (Scene layer)                                    | <0.1ms @1K |
| 47  | Color Bar / Legend    | Analysis/scalar active  | Push(`minVal`, `maxVal`, `colorRampIdx`) + `ColorRampLUT` R8G8B8A8 1D                                                           | Screen-space colored bar + MSDF tick labels (Widgets layer)                           | <0.05ms    |
| 59  | VRS Image             | T1, enabled             | `HDRColor` RGBA16F (luminance gradient analysis)                                                                                | VRS rate image R8_UINT per 16x16 tile (1=1x1, 2=2x2, 4=4x4)                           | <0.2ms     |
| 60  | Gizmo Render          | Gizmo active            | `GizmoState` SSBO + `GizmoMeshPool` VB (pre-built, ~500 tri) + Scene `DepthTarget` D32F (read)                                  | Unlit colored handles RGBA8 + Overlay depth D32F (active axis: depth forced near=1.0) | <0.1ms     |
| 61  | Ground Grid           | Always (Overlay layer)  | `GpuCameraUBO` (worldPos reconstruction from depth)                                                                             | Grid lines RGBA8 (fwidth AA, distance fade alpha) + Overlay depth D32F                | <0.2ms     |
| 62  | ViewCube              | Always (Overlay corner) | `GpuCameraUBO.viewMatrix` (orientation only)                                                                                    | Mini cube RGBA8 (120x120 px fixed viewport)                                           | <0.05ms    |
| 63  | Snap Indicators       | Snap mode active        | `SnapPoint[]` SSBO (float3 positions)                                                                                           | SDF dots/crosses RGBA8 (Overlay layer)                                                | <0.01ms    |
| 64  | Measurement Viz       | Measurements exist      | `MeasurementResult[]` SSBO (from #36)                                                                                           | SDF leader lines + MSDF dimension text RGBA8 (Overlay layer)                          | <0.1ms     |
| 65  | LayerStack Compositor | Always (final pass)     | 6 layer color+depth textures: Scene RGBA16F+D32F, Preview RGBA16F+D32F, Overlay RGBA8+D32F, Widgets RGBA8, SVG RGBA8, HUD RGBA8 | Final composited framebuffer RGBA8_SRGB (swapchain or OffscreenTarget)                | <0.2ms     |

### 18.1 Gizmo Render Pass

Renders in Overlay layer (Layer 3):

```
Input:  GizmoState SSBO {type, transform, activeAxis, hoverAxis, mode}
Geometry: GizmoMeshPool (pre-built: arrows, rings, cubes, planes -- ~500 tri total)
Shading: unlit, solid color per-axis (R=X, G=Y, B=Z, yellow=uniform) + hover highlight
Depth: read scene depth for occlusion; active axis: depth forced to near plane (always visible)
Budget: <0.1ms
```

Gizmo types supported by the render pass:

| Gizmo         | Handles                                  | GPU Data          |
| ------------- | ---------------------------------------- | ----------------- |
| Translate     | 3 arrows + 3 plane squares + center      | GizmoState.type=0 |
| Rotate        | 3 rings + trackball sphere + screen ring | GizmoState.type=1 |
| Scale         | 3 cube handles + center                  | GizmoState.type=2 |
| Combined      | Merged translate+rotate+scale            | GizmoState.type=3 |
| Section Plane | Normal arrow + rotation ring             | GizmoState.type=4 |
| Light         | Position + direction + cone              | GizmoState.type=5 |
| Camera        | Frustum wireframe + focal length         | GizmoState.type=6 |

Coordinate space (world/local/parent/screen/UCS) and pivot point resolved CPU-side -> final transform in GizmoState.transform. GPU sees only pre-resolved world matrix.

### 18.2 Ground Grid Pass

Overlay layer fullscreen fragment shader:

```
Algorithm: screen-space adaptive infinite grid (fwidth-based AA)
  UV = worldPosition.xz (reconstructed from depth)
  grid = abs(frac(UV * gridScale - 0.5) - 0.5) / fwidth(UV * gridScale)
  alpha = 1.0 - min(grid.x, grid.y) / lineWidth

Major/minor lines: configurable spacing (1/10/100 units)
Distance fade: alpha *= 1.0 - smoothstep(fadeStart, fadeEnd, distToCamera)
Log-spacing: grid density adapts to camera distance
Depth: writes to Overlay depth for scene occlusion
Budget: <0.2ms
```

### 18.3 ViewCube Pass

Pre-built cube mesh in Overlay layer corner. Unlit, clickable faces. Renders with fixed viewport (e.g., 120x120 px top-right). Camera orientation drives cube rotation. <0.05ms.

### 18.4 Other Overlay Elements

| Element             | Layer      | Rendering                                  | Budget  |
| ------------------- | ---------- | ------------------------------------------ | ------- |
| Axis Triad          | Overlay(3) | 3 colored lines + MSDF labels              | <0.02ms |
| Compass Rose        | Overlay(3) | SDF ring + NSEW labels                     | <0.02ms |
| World Origin        | Overlay(3) | 3 axis arrows at (0,0,0)                   | <0.01ms |
| UCS Indicator       | Overlay(3) | Colored axes at UCS origin                 | <0.01ms |
| Construction Planes | Overlay(3) | Semi-transparent quads, depth-composited   | <0.05ms |
| Snap Indicators     | Overlay(3) | SDF dot/cross at snap point                | <0.01ms |
| Bounding Box        | Overlay(3) | 12 wireframe edges from AABB/OBB           | <0.02ms |
| CoM Marker          | Overlay(3) | Crosshair + sphere                         | <0.01ms |
| Measurement Viz     | Overlay(3) | SDF leaders + MSDF dimension text          | <0.1ms  |
| Scale Bar           | Widgets(4) | 2D ruler, auto-zoom-update                 | <0.01ms |
| Coordinate Readout  | Widgets(4) | MSDF text at fixed screen pos              | <0.01ms |
| Dynamic Input       | Widgets(4) | Tooltip near cursor                        | <0.01ms |
| Crosshair           | Widgets(4) | 2 screen-space lines                       | <0.01ms |
| Safe Frame          | Widgets(4) | Rectangle at render resolution             | <0.01ms |
| Mini-Map            | Widgets(4) | Render-to-texture (low-res ortho top-down) | <1ms    |
| FPS Overlay         | HUD(6)     | ImGui text                                 | <0.01ms |
| Progress            | HUD(6)     | ImGui progress bar                         | <0.01ms |
| Toast               | HUD(6)     | Animated fade text box                     | <0.01ms |
| Status Bar          | HUD(6)     | ImGui or custom 2D                         | <0.05ms |
| Navigation Bar      | HUD(6)     | ImGui toolbar                              | <0.05ms |

---

## 19. Export & Drawing Generation Pipeline

### 19.1 Offscreen Render Pipeline

All exports render to `OffscreenTarget` (no swapchain). Resolution: viewport / custom / DPI-based (up to 16K x 16K).

**Tile-based hi-res**: for resolutions > GPU max texture, split into 4K x 4K tiles. Each tile: set camera to sub-frustum (jittered projection) -> full pipeline render -> readback. CPU stitches tiles. Memory: 1 tile at a time.

### 19.2 Export Formats

| Format         | Alpha | HDR           | Method                                    |
| -------------- | ----- | ------------- | ----------------------------------------- |
| PNG            | Yes   | No            | stb_image_write / libpng                  |
| JPEG           | No    | No            | libjpeg-turbo                             |
| BMP / TIFF     | Yes   | TIFF: float32 | stb / libtiff                             |
| EXR            | Yes   | float16/32    | tinyexr / OpenEXR                         |
| SVG / PDF      | N/A   | N/A           | HLR edge readback -> vector paths (§19.4) |
| 3D PDF (PRC)   | 3D    | N/A           | PRC/U3D embedding                         |
| glTF 2.0 / USD | N/A   | N/A           | Scene export: mesh + material + lights    |

### 19.3 Batch Rendering

Queue of `{NamedView, Format, Resolution, OutputPath}`. Sequential offscreen render + export. Progress callback per frame.

### 19.4 Vector Export from HLR

```
Pipeline:
  1. Run GPU HLR classify + visibility (§10.1 stages 1-2)
  2. Readback EdgeBuffer GPU -> CPU
  3. Project 3D edge endpoints to 2D (ortho or perspective)
  4. Generate SVG path elements:
     Visible edges: solid stroke, ISO 128 line weight
     Hidden edges: dashed stroke from LinePattern
     Section hatching: fill patterns
  5. Write SVG file or convert to PDF via cairo

Accuracy: sub-pixel precise (GPU edge classification), vector-sharp at any zoom
Budget: <5s @1M edges (dominated by readback + SVG generation)
```

### 19.5 3D-to-2D Drawing Generation

```
Drawing pipeline (#68 in pass table):
  1. Set orthographic camera for view direction
  2. Run GPU HLR (§10.1) -> visible/hidden edges
  3. Project edges to 2D view plane with scale
  4. ISO 128 line types (visible=continuous, hidden=dashed)
  5. Section views: section plane -> contour + hatch
  6. Detail views: circular/rect region -> scale up
  7. Auxiliary views: project onto inclined datum plane
  8. Break views: insert break lines, compress middle
  9. Export SVG/PDF or display in Layout tab

Associativity: views store 3D model ref + view params.
  On model change -> re-project dirty bodies (ECS dirty flags).
  Incremental: <500ms for 1 body change.

Performance:
  Single ortho view (1M tri): <2s
  Section view + hatch: <3s
  Full sheet (6 views): <15s
```

### 19.6 Cloud / Pixel Streaming Export Path

```
Server-side rendering -> GPU video encode:
  1. Render to offscreen (standard pipeline)
  2. GPU encode: NVENC (NVIDIA) / AMF (AMD) / VA-API (Intel)
     Encode latency: <2ms per frame
  3. Transport: WebRTC (target <50ms end-to-end latency)
  4. Client: decode + display; send input events back

Hybrid rendering: server renders scene geometry; client renders UI overlays (gizmo/toolbar/HUD) locally in WebGPU.

Adaptive quality: monitor bandwidth -> adjust resolution, frame rate, encode quality.
  Low BW: 720p/30fps/CRF28 -> High BW: 4K/60fps/CRF18
```

---

## 20. Synchronization & Memory

### 20.1 Per-Frame-In-Flight

```
kMaxFramesInFlight = 2 (configurable, default for all tiers)
```

**Tier1 (Timeline Semaphore Model)** — see `03-sync.md` §3-§5 for full specification:

| Resource (per queue)        | Count | Purpose                                                   |
| --------------------------- | ----- | --------------------------------------------------------- |
| Graphics timeline semaphore | 1     | CPU wait (BeginFrame) + cross-queue sync + frame ordering |
| Compute timeline semaphore  | 1     | Async compute completion tracking                         |
| Transfer timeline semaphore | 1     | Staging upload completion                                 |
| Binary semaphore (acquire)  | 2     | Swapchain image acquisition (per slot)                    |
| Binary semaphore (present)  | N     | Render-done signal for present (per swapchain image)      |

CPU waits on `G.timeline >= slot[oldest].lastSignaledValue` at `BeginFrame` — no `VkFence` needed. Timeline values are globally allocated by `SyncScheduler::AllocateSignal()`, enabling multi-window interleaving without value conflicts. Split-submit (§5.3 of `03-sync.md`) allows partial timeline signals within a frame for early async compute start.

**Tier2 (Fence Model)**:

| Resource         | Count | Purpose                           |
| ---------------- | ----- | --------------------------------- |
| `VkFence`        | 2     | CPU wait per slot (BeginFrame)    |
| Binary semaphore | 2     | Swapchain acquire (per slot)      |
| Binary semaphore | N     | Render-done (per swapchain image) |

Single queue, single submit per frame. No async compute, no split-submit.

**Tier3/Tier4**: Implicit sync — `requestAnimationFrame()` (WebGPU) or `glfwSwapBuffers()` (OpenGL) provides frame pacing. No explicit semaphores or fences.

**Frame lifecycle** (all tiers, managed by `FrameManager`):

```
BeginFrame: wait for oldest slot → reset CommandPoolAllocator (§19 of 03-sync.md)
            → drain DeferredDestructor bin → acquire swapchain image
Record:     RenderGraph compile + execute → command buffers recorded
EndFrame:   submit command buffers + sync primitives → present
```

### 20.2 GPU-GPU Synchronization

| Mechanism                  | Usage                                                                                                                                         |
| -------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| Pipeline barrier           | Pass-to-pass resource transitions (same queue)                                                                                                |
| Timeline semaphore (Tier1) | Graphics <-> async compute overlap                                                                                                            |
| Fence                      | Per-frame CPU-GPU sync, command buffer reuse guard                                                                                            |
| Event                      | Fine-grained intra-queue dependency (split barriers)                                                                                          |
| D3D12 Fence Barriers       | Command-list-level SignalBarrier/WaitBarrier (Agility SDK 1.719+); Tier-2 adds cross-queue WaitBarrier (see `04-render-graph.md` §5.4, §16.8) |

**Per-tier sync primitive availability** (see `02-rhi-design.md` §9, `03-sync.md` §2.2):

| Primitive                       | T1 Vulkan                 | T1 D3D12                                        | T2 Compat                | T3 WebGPU | T4 OpenGL         |
| ------------------------------- | ------------------------- | ----------------------------------------------- | ------------------------ | --------- | ----------------- |
| Timeline semaphore              | Native (core 1.2)         | Emulated via `ID3D12Fence`                      | Extension (if present)   | N/A       | N/A               |
| Binary semaphore                | Swapchain acquire/present | N/A (fence-based present)                       | Swapchain only           | N/A       | N/A               |
| Fence (CPU-GPU)                 | `VkFence`                 | `ID3D12Fence` + event                           | `VkFence`                | Implicit  | `glFinish`        |
| Pipeline barrier                | `vkCmdPipelineBarrier2`   | Enhanced Barriers / Legacy                      | `vkCmdPipelineBarrier`   | Implicit  | `glMemoryBarrier` |
| Split barrier                   | Separate release/acquire  | Enhanced `SYNC_SPLIT` / `BEGIN_ONLY`+`END_ONLY` | Separate release/acquire | N/A       | N/A               |
| Fence barrier (D3D12)           | N/A                       | `SignalBarrier`/`WaitBarrier`                   | N/A                      | N/A       | N/A               |
| QFOT (queue ownership transfer) | Required (EXCLUSIVE mode) | N/A (implicit cross-queue)                      | Required                 | N/A       | N/A               |
| Async compute queue             | Dedicated queue family    | `COMPUTE` type command queue                    | Fallback to graphics     | N/A       | N/A               |
| Transfer queue                  | Dedicated DMA queue       | `COPY` type command queue                       | Fallback to graphics     | N/A       | N/A               |

### 20.3 RenderGraph Barrier Strategy

> **Full specification**: See `04-render-graph.md` §5.4 (split barrier model), §5.6 (per-backend aliasing barriers with Vulkan/D3D12/OpenGL/WebGPU details), §5.6.4 (barrier batching), and §5.7 (render pass merging / subpass consolidation).

Summary: The RenderGraph compiler synthesizes split barriers (release after last writer, acquire before first reader) to maximize GPU overlap. Transient resources with non-overlapping lifetimes are aliased to the same `VkDeviceMemory` / `ID3D12Heap`. Consecutive graphics passes sharing attachments are merged into subpasses on supporting backends (Vulkan `VK_KHR_dynamic_rendering_local_read`, D3D12 Render Pass Tier 2).

**Barrier optimization pipeline** (see `04-render-graph.md` §5 for full detail):

| Stage                                 | Optimization                                       | Effect                                     |
| ------------------------------------- | -------------------------------------------------- | ------------------------------------------ |
| 1. Barrier-aware reordering (§5.3.1)  | Topological reorder to minimize layout transitions | Fewer barriers emitted                     |
| 2. Split barrier placement (§5.4)     | Release at writer, acquire at reader with gap      | GPU overlap during resolution              |
| 3. Aliasing barrier batching (§5.6.4) | Co-locate aliasing + acquire barriers per-pass     | Fewer API calls                            |
| 4. Render pass merging (§5.7)         | Merge consecutive passes into subpasses            | Replace barriers with subpass dependencies |

**Access flag translation**: Passes declare `ResourceAccess` (render-graph layer); the compiler converts to `(PipelineStage, AccessFlags)` (RHI layer) via the mapping table in `04-render-graph.md` §3.2. This two-layer design keeps pass code barrier-free while generating optimal per-backend barrier commands.

### 20.4 VRAM Budget Strategy

Target: **2B triangles in <12GB VRAM**.

| Level                    | Mechanism                                  | Savings                  |
| ------------------------ | ------------------------------------------ | ------------------------ |
| 1. Out-of-core streaming | Only active LOD clusters resident          | ~80% raw data not loaded |
| 2. Meshlet compression   | 16-bit quantize + oct normal + delta index | ~50% per meshlet         |
| 3. Transient aliasing    | RenderGraph lifetime analysis (Kahn sort)  | 30-50% RT VRAM           |
| 4. On-demand pages       | VSM active tiles only; VisBuffer 4K = 67MB | Pay-per-use              |

**Fixed-cost VRAM allocations** (always present, independent of scene size):

| Resource                                 | Size @4K                 | Condition          |
| ---------------------------------------- | ------------------------ | ------------------ |
| VisBuffer                                | 67 MB                    | Always             |
| GBuffer MRT (RT0+RT1+RT2+Depth)          | 132 MB                   | Always             |
| HiZ pyramid                              | ~22 MB                   | Always             |
| TAA history                              | 66 MB                    | T1/T2              |
| HDR ping-pong                            | 66 MB                    | Always             |
| VSM page pool (physical)                 | 256 MB                   | T1                 |
| Shadow Atlas                             | 256 MB (T1) / 64 MB (T2) | Shadow on          |
| OIT node pool                            | 160-512 MB               | OIT active         |
| ReSTIR DI reservoirs (current + history) | 266 MB                   | Phase 19 Ultra     |
| ReSTIR GI reservoirs                     | 133 MB                   | Phase 19 Ultra     |
| DDGI probe grid (1K probes)              | ~32 MB                   | Phase 19 Enhanced+ |
| IBL cubemap + specular + BRDF LUT        | ~50 MB                   | Always             |
| **Subtotal (Shaded mode, no RT)**        | **~660 MB**              |                    |
| **Subtotal (Ultra mode, all RT)**        | **~1,550 MB**            |                    |

Remaining VRAM (12GB - 1.55GB = ~10.45GB) available for scene data (meshlets, textures, SceneBuffer).

### 20.5 Memory Pressure Response

| State    | Trigger     | Action                                                  |
| -------- | ----------- | ------------------------------------------------------- |
| Normal   | Usage < 70% | Full quality, background prefetch                       |
| Warning  | 70-85%      | Halt prefetch, reduce LOD bias                          |
| Critical | 85-95%      | Aggressive LRU eviction, lower VSM resolution           |
| OOM      | >95%        | Emergency evict, disable RTAO, reduce render resolution |

`ResidencyFeedback` (GPU access counters readback) drives load/evict priority.

### 20.6 WebGPU Chunked Binding (Tier3)

`maxStorageBufferBindingSize` typically 128-256 MB. Design budget for T3:

| Scenario     | Instances | GpuInstance SSBO | Status                                                                                   |
| ------------ | --------- | ---------------- | ---------------------------------------------------------------------------------------- |
| Within limit | <=500K    | 64 MB            | Single bind group, no chunking                                                           |
| Over limit   | 500K-2M   | 64-256 MB        | Chunked binding: 32K instances/bind group, CPU frustum pre-filter selects visible chunks |

Chunked binding overhead: ~0.01ms per rebind (negligible). CPU pre-filter: spatial hash selects which 32K-instance chunks overlap camera frustum, typically 1-4 rebinds per frame.

**T3 is NOT zero-CPU-overhead by design**: T3 uses `Vertex+draw` (no MDI), CPU-sorted light UBO, and CPU frustum culling. The chunked binding is consistent with this design — T3 trades GPU efficiency for broad hardware compatibility (browsers, integrated GPUs). For scenes >500K instances on WebGPU, the recommended path is server-side rendering via Phase 20 Cloud Render (Tier1 GPU on server, H.265 stream to browser).

---

## 20a. RHI Validation Layer

### 20a.1 Motivation & Scope

RHI backends have **divergent constraints** on resource creation, command recording, and render pass configuration. Without a structured validation layer, these constraints surface as:

- Opaque backend error messages (Dawn validation errors, D3D12 debug layer, Vulkan validation layer)
- Runtime failures that could be caught at resource creation time
- Silent behavioral differences across backends (e.g., WebGPU shadow buffer vs Vulkan direct map)

The RHI Validation Layer provides:

1. **Structured capability rules** — constexpr tables declaring per-backend constraints
2. **Early fail-fast validation** — catch illegal parameter combinations before calling backend APIs
3. **Debug diagnostics** — rich error messages with cross-backend comparison hints
4. **Compile-time opt-in** — zero binary size and zero runtime cost when disabled

**Non-goals**: The validation layer does NOT replace backend-native validation (Vulkan Validation Layers, D3D12 Debug Layer, Dawn error callbacks). It complements them by catching RHI-level semantic errors earlier and with better diagnostics.

### 20a.2 Architecture

```
User Code
   │
   ▼
DeviceBase<Impl>::CreateBuffer(desc)
   │
   ├─ [MIKI_RHI_VALIDATION=1] ──► RhiValidator::ValidateBufferDesc(backend, desc)
   │                                  │
   │                                  ├─ Query BackendCapabilityTable[backend]
   │                                  ├─ Check constraints (constexpr rule evaluation)
   │                                  ├─ Return RhiResult<void> or RhiValidationError
   │                                  │
   │◄─────────────────────────────────┘
   │
   ▼
Impl::CreateBufferImpl(desc)   ◄── Backend-specific adaptation (shadow buffer, heap type, etc.)
```

**Key design principle**: The validation layer is a **pure predicate** — it checks, warns, and optionally rejects. It never modifies the desc or performs adaptation. Backend adaptation (e.g., WebGPU shadow buffer) remains in the backend `*Impl` methods.

### 20a.3 Compile-Time Opt-In

```cpp
// CMake: -DMIKI_RHI_VALIDATION=1 (default ON for Debug/RelWithDebInfo, OFF for Release)
// Can also be toggled per-TU via #define before including Device.h

#ifndef MIKI_RHI_VALIDATION
#   if !defined(NDEBUG) || defined(MIKI_FORCE_RHI_VALIDATION)
#       define MIKI_RHI_VALIDATION 1
#   else
#       define MIKI_RHI_VALIDATION 0
#   endif
#endif
```

When `MIKI_RHI_VALIDATION=0`:

- All validation functions are `[[gnu::always_inline]] constexpr` no-ops returning success
- The `BackendCapabilityTable` data is not instantiated (no static storage)
- The compiler eliminates all validation branches via dead code elimination
- **Zero binary size overhead, zero runtime overhead** — verified by checking `.text` section size delta

When `MIKI_RHI_VALIDATION=1`:

- Validation runs on resource creation, pipeline creation, and render pass begin
- **Never** on per-draw/dispatch hot paths (CmdDraw, CmdDispatch, CmdBindPipeline)
- Estimated overhead: <1μs per validated call (constexpr table lookup + bitwise checks)

### 20a.4 BackendCapabilityTable

The central data structure. Each backend declares its constraints as a constexpr struct:

```cpp
namespace miki::rhi::validation {

/// Buffer memory constraints for a specific backend.
struct BufferMemoryRule {
    MemoryLocation memory;
    BufferUsage forbiddenUsageCombination;  ///< Usage flags that CANNOT coexist with this memory location
    BufferUsage requiredUsageAddition;      ///< Usage flags the backend will implicitly add
    bool requiresStagingCopy;               ///< Backend uses staging buffer pattern internally
    const char* rationale;                  ///< Human-readable reason for the constraint
};

/// Depth/stencil format constraints for a specific backend.
struct DepthStencilRule {
    Format format;
    bool hasStencilAspect;
    bool stencilOpsMustBeUndefined;  ///< stencilLoadOp/stencilStoreOp must be Undefined
    bool stencilReadOnlyRequired;    ///< stencilReadOnly must be true
};

/// Texture creation constraints for a specific backend.
struct TextureMemoryRule {
    MemoryLocation memory;
    TextureUsage forbiddenUsageCombination;
    const char* rationale;
};

/// Complete capability table for one backend.
/// Note: Uses std::span for rule arrays. Tables are declared `inline const` (not constexpr)
/// at namespace scope because std::span stores a pointer to the constexpr rule arrays.
/// The compiler will place these in .rodata and they are effectively immutable.
struct BackendCapabilityTable {
    BackendType backend;

    // Buffer constraints
    std::span<const BufferMemoryRule> bufferMemoryRules;

    // Depth/stencil constraints
    std::span<const DepthStencilRule> depthStencilRules;

    // Texture constraints
    std::span<const TextureMemoryRule> textureMemoryRules;

    // Limits
    uint32_t maxPushConstantSize;
    uint32_t maxBindGroups;              ///< WebGPU: 4, Vulkan: 8+
    uint32_t maxStorageBufferBindingSize;
    uint32_t maxVertexBufferStride;
    uint32_t maxComputeWorkgroupStorageSize;

    // Feature gates
    bool supportsMapWriteWithResourceUsage;  ///< Vulkan: true, WebGPU: false, D3D12: false
    bool supportsPersistentMapping;          ///< Vulkan: true, D3D12: true (Upload), WebGPU: false
    bool supportsSimultaneousMapAndGpuAccess; ///< Vulkan (coherent): true, others: false
};

}  // namespace miki::rhi::validation
```

### 20a.5 Per-Backend Rule Tables

#### WebGPU (Tier 3)

```cpp
inline constexpr BufferMemoryRule kWebGPUBufferRules[] = {
    {
        .memory = MemoryLocation::CpuToGpu,
        .forbiddenUsageCombination = BufferUsage::Vertex | BufferUsage::Index
                                   | BufferUsage::Uniform | BufferUsage::Storage
                                   | BufferUsage::Indirect,
        // ↑ MapWrite cannot coexist with ANY of these in WebGPU
        .requiredUsageAddition = BufferUsage::TransferDst,  // CopyDst added instead
        .requiresStagingCopy = true,
        .rationale = "WebGPU forbids MAP_WRITE combined with resource usage flags. "
                     "Backend uses CPU shadow buffer + wgpuQueueWriteBuffer."
    },
    {
        .memory = MemoryLocation::GpuToCpu,
        .forbiddenUsageCombination = BufferUsage::Vertex | BufferUsage::Index
                                   | BufferUsage::Uniform | BufferUsage::Storage,
        .requiredUsageAddition = BufferUsage::TransferDst,
        .requiresStagingCopy = false,
        .rationale = "WebGPU MapRead buffers can only have MAP_READ | COPY_DST usage."
    },
};

inline constexpr DepthStencilRule kWebGPUDepthStencilRules[] = {
    {Format::D16_UNORM,        .hasStencilAspect = false, .stencilOpsMustBeUndefined = true, .stencilReadOnlyRequired = true},
    {Format::D32_FLOAT,        .hasStencilAspect = false, .stencilOpsMustBeUndefined = true, .stencilReadOnlyRequired = true},
    {Format::D24_UNORM_S8_UINT,.hasStencilAspect = true,  .stencilOpsMustBeUndefined = false,.stencilReadOnlyRequired = false},
    {Format::D32_FLOAT_S8_UINT,.hasStencilAspect = true,  .stencilOpsMustBeUndefined = false,.stencilReadOnlyRequired = false},
};

inline const BackendCapabilityTable kWebGPUTable = {
    .backend = BackendType::WebGPU,
    .bufferMemoryRules = kWebGPUBufferRules,
    .depthStencilRules = kWebGPUDepthStencilRules,
    .textureMemoryRules = {},
    .maxPushConstantSize = 256,   // Emulated via UBO
    .maxBindGroups = 4,
    .maxStorageBufferBindingSize = 256 * 1024 * 1024,  // 256MB typical
    .maxVertexBufferStride = 2048,
    .maxComputeWorkgroupStorageSize = 16384,
    .supportsMapWriteWithResourceUsage = false,
    .supportsPersistentMapping = false,
    .supportsSimultaneousMapAndGpuAccess = false,
};
```

#### Vulkan (Tier 1 / Tier 2)

`VulkanCompat` (Tier 2) shares the same capability table as `Vulkan14` (Tier 1) for validation purposes — the mapping and buffer usage rules are identical. `GetCapabilityTable()` maps both `BackendType::Vulkan14` and `BackendType::VulkanCompat` to `kVulkanTable`.

```cpp
inline constexpr BufferMemoryRule kVulkanBufferRules[] = {
    // Vulkan has no forbidden usage combinations for mapped buffers.
    // HOST_VISIBLE memory can be used with any buffer usage.
    // Empty rule set = no restrictions.
};

inline constexpr DepthStencilRule kVulkanDepthStencilRules[] = {
    // Vulkan allows stencil ops on depth-only formats (they are simply ignored).
    // No special rules needed — Vulkan validation layer handles edge cases.
    {Format::D16_UNORM,        .hasStencilAspect = false, .stencilOpsMustBeUndefined = false, .stencilReadOnlyRequired = false},
    {Format::D32_FLOAT,        .hasStencilAspect = false, .stencilOpsMustBeUndefined = false, .stencilReadOnlyRequired = false},
    {Format::D24_UNORM_S8_UINT,.hasStencilAspect = true,  .stencilOpsMustBeUndefined = false, .stencilReadOnlyRequired = false},
    {Format::D32_FLOAT_S8_UINT,.hasStencilAspect = true,  .stencilOpsMustBeUndefined = false, .stencilReadOnlyRequired = false},
};

inline const BackendCapabilityTable kVulkanTable = {
    .backend = BackendType::Vulkan14,  // Also used for VulkanCompat
    .bufferMemoryRules = kVulkanBufferRules,
    .depthStencilRules = kVulkanDepthStencilRules,
    .textureMemoryRules = {},
    .maxPushConstantSize = 256,
    .maxBindGroups = 8,             // maxBoundDescriptorSets
    .maxStorageBufferBindingSize = UINT32_MAX,
    .maxVertexBufferStride = 4096,
    .maxComputeWorkgroupStorageSize = 32768,
    .supportsMapWriteWithResourceUsage = true,
    .supportsPersistentMapping = true,
    .supportsSimultaneousMapAndGpuAccess = true,  // HOST_COHERENT
};
```

#### D3D12 (Tier 1)

```cpp
inline constexpr BufferMemoryRule kD3D12BufferRules[] = {
    {
        .memory = MemoryLocation::CpuToGpu,
        .forbiddenUsageCombination = BufferUsage{0},  // No forbidden combos at usage level
        .requiredUsageAddition = BufferUsage{0},
        .requiresStagingCopy = true,  // D3D12_HEAP_TYPE_UPLOAD -> must CopyBufferRegion to DEFAULT
        .rationale = "D3D12 UPLOAD heap resources cannot be used directly as VB/IB/CBV on GPU. "
                     "Backend copies to DEFAULT heap resource."
    },
};

inline const BackendCapabilityTable kD3D12Table = {
    .backend = BackendType::D3D12,
    .bufferMemoryRules = kD3D12BufferRules,
    .depthStencilRules = {},  // D3D12 handles depth-only stencil ops gracefully
    .textureMemoryRules = {},
    .maxPushConstantSize = 256,  // Root constants: 64 DWORDs
    .maxBindGroups = 64,         // Root signature slots
    .maxStorageBufferBindingSize = UINT32_MAX,
    .maxVertexBufferStride = 2048,
    .maxComputeWorkgroupStorageSize = 32768,
    .supportsMapWriteWithResourceUsage = false,  // UPLOAD heap -> needs copy
    .supportsPersistentMapping = true,
    .supportsSimultaneousMapAndGpuAccess = false,
};
```

#### OpenGL (Tier 4)

```cpp
inline const BackendCapabilityTable kOpenGLTable = {
    .backend = BackendType::OpenGL43,
    .bufferMemoryRules = {},      // GL driver handles everything
    .depthStencilRules = {},      // GL is lenient with depth-only stencil ops
    .textureMemoryRules = {},
    .maxPushConstantSize = 128,   // Emulated via 128B UBO
    .maxBindGroups = 4,           // Practical UBO binding limit
    .maxStorageBufferBindingSize = 128 * 1024 * 1024,
    .maxVertexBufferStride = 2048,
    .maxComputeWorkgroupStorageSize = 32768,
    .supportsMapWriteWithResourceUsage = true,   // glMapBuffer works with any target
    .supportsPersistentMapping = true,            // GL_MAP_PERSISTENT_BIT
    .supportsSimultaneousMapAndGpuAccess = true,  // GL_MAP_COHERENT_BIT
};
```

### 20a.6 Validator API

```cpp
namespace miki::rhi::validation {

/// Lookup the capability table for a given backend.
/// Returns nullptr for Mock backend (no validation).
constexpr auto GetCapabilityTable(BackendType backend) -> const BackendCapabilityTable*;

/// Severity levels for validation messages.
enum class Severity : uint8_t {
    Error,    ///< Will fail on this backend. Block the call.
    Warning,  ///< Will work but with hidden cost (e.g., staging copy). Log and continue.
    Info,     ///< Informational hint (e.g., "consider GpuOnly for static geometry").
};

/// Validation diagnostic message.
struct ValidationDiagnostic {
    Severity severity;
    const char* message;       ///< Static string (no heap allocation)
    const char* rationale;     ///< Why this is a problem
    BackendType backend;       ///< Which backend triggered this
};

/// Validation result: success or a list of diagnostics.
/// Uses inline storage (SmallVector<4>) to avoid heap allocation in the common case.
struct ValidationResult {
    bool passed = true;
    core::SmallVector<ValidationDiagnostic, 4> diagnostics;

    explicit operator bool() const noexcept { return passed; }
};

/// The stateless validator. All methods are pure functions of (backend, desc).
/// When MIKI_RHI_VALIDATION=0, all methods are constexpr no-ops.
class RhiValidator {
public:
#if MIKI_RHI_VALIDATION
    static auto ValidateBufferDesc(BackendType backend, const BufferDesc& desc) -> ValidationResult;
    static auto ValidateTextureDesc(BackendType backend, const TextureDesc& desc) -> ValidationResult;
    static auto ValidateRenderPassDesc(BackendType backend, const RenderingDesc& desc) -> ValidationResult;
    static auto ValidateGraphicsPipelineDesc(BackendType backend, const GraphicsPipelineDesc& desc) -> ValidationResult;
    static auto ValidateComputePipelineDesc(BackendType backend, const ComputePipelineDesc& desc) -> ValidationResult;
    static auto ValidateDescriptorLayoutDesc(BackendType backend, const DescriptorLayoutDesc& desc) -> ValidationResult;
    static auto ValidatePipelineLayoutDesc(BackendType backend, const PipelineLayoutDesc& desc) -> ValidationResult;
#else
    // Zero-cost no-ops when validation is disabled
    static constexpr auto ValidateBufferDesc(BackendType, const BufferDesc&) -> ValidationResult { return {}; }
    static constexpr auto ValidateTextureDesc(BackendType, const TextureDesc&) -> ValidationResult { return {}; }
    static constexpr auto ValidateRenderPassDesc(BackendType, const RenderingDesc&) -> ValidationResult { return {}; }
    static constexpr auto ValidateGraphicsPipelineDesc(BackendType, const GraphicsPipelineDesc&) -> ValidationResult { return {}; }
    static constexpr auto ValidateComputePipelineDesc(BackendType, const ComputePipelineDesc&) -> ValidationResult { return {}; }
    static constexpr auto ValidateDescriptorLayoutDesc(BackendType, const DescriptorLayoutDesc&) -> ValidationResult { return {}; }
    static constexpr auto ValidatePipelineLayoutDesc(BackendType, const PipelineLayoutDesc&) -> ValidationResult { return {}; }
#endif
};

}  // namespace miki::rhi::validation
```

### 20a.7 Integration Point: DeviceBase CRTP

Validation is injected in `DeviceBase<Impl>` public methods, **before** delegating to `*Impl`:

```cpp
template <typename Impl>
class DeviceBase {
public:
    [[nodiscard]] auto CreateBuffer(const BufferDesc& desc) -> RhiResult<BufferHandle> {
#if MIKI_RHI_VALIDATION
        if (auto vr = validation::RhiValidator::ValidateBufferDesc(Self().GetBackendType(), desc); !vr) {
            for (auto& d : vr.diagnostics) {
                if (d.severity == validation::Severity::Error) {
                    MIKI_LOG_ERROR("[RHI Validation] {}: {}", d.message, d.rationale);
                    return std::unexpected(RhiError::InvalidArgument);
                }
                MIKI_LOG_WARN("[RHI Validation] {}: {}", d.message, d.rationale);
            }
        }
#endif
        return Self().CreateBufferImpl(desc);
    }
    // ... same pattern for CreateTexture, CreateGraphicsPipeline, etc.
};
```

**Properties**:

- `#if MIKI_RHI_VALIDATION` ensures zero overhead in Release
- Validation runs BEFORE backend call — fail-fast semantics
- Warnings (e.g., "staging copy will be used") log but do not block
- Errors (e.g., "push constant size exceeds backend limit") return `RhiError::InvalidArgument`

### 20a.8 Validation Scope & Timing

| Validation Point         | When              | Hot Path?            | Validated Properties                                         |
| ------------------------ | ----------------- | -------------------- | ------------------------------------------------------------ |
| `CreateBuffer`           | Resource creation | No                   | Usage + memory combination, size limits                      |
| `CreateTexture`          | Resource creation | No                   | Usage + memory + format + dimension limits                   |
| `CreateGraphicsPipeline` | Pipeline creation | No                   | Shader stage compatibility, vertex layout, attachment format |
| `CreateComputePipeline`  | Pipeline creation | No                   | Workgroup size limits                                        |
| `CreatePipelineLayout`   | Layout creation   | No                   | Push constant size, descriptor set count                     |
| `CreateDescriptorLayout` | Layout creation   | No                   | Binding count, binding type support                          |
| `CmdBeginRendering`      | Render pass begin | No (O(passes/frame)) | Depth/stencil format vs stencil ops, attachment count        |
| `CmdDraw*`               | Per-draw          | **NEVER**            | —                                                            |
| `CmdDispatch*`           | Per-dispatch      | **NEVER**            | —                                                            |
| `CmdBind*`               | Per-bind          | **NEVER**            | —                                                            |

### 20a.9 Cross-Backend Constraint Comparison

Reference table for the validation rules. This is the **source of truth** for `BackendCapabilityTable` initialization.

#### Buffer Memory × Usage Rules

| MemoryLocation | Allowed Usage Combos                     | WebGPU              | Vulkan           | D3D12               | OpenGL           |
| -------------- | ---------------------------------------- | ------------------- | ---------------- | ------------------- | ---------------- |
| `GpuOnly`      | Any resource usage                       | `✓`                 | `✓`              | `✓ (DEFAULT heap)`  | `✓`              |
| `CpuToGpu`     | `CopyDst + Vertex/Index/Uniform/Storage` | `✓ (shadow buffer)` | `✓ (direct map)` | `✓ (Upload→Copy)`   | `✓ (direct map)` |
| `CpuToGpu`     | `MapWrite + CopySrc` (pure staging)      | `✓`                 | `✓`              | `✓`                 | `✓`              |
| `CpuToGpu`     | `MapWrite + Vertex/Index`                | `✗ FORBIDDEN`       | `✓`              | `✗ (Upload heap)`   | `✓`              |
| `GpuToCpu`     | `MapRead + CopyDst`                      | `✓`                 | `✓`              | `✓ (READBACK heap)` | `✓`              |
| `GpuToCpu`     | `MapRead + Vertex/Index`                 | `✗ FORBIDDEN`       | `✓`              | `✗`                 | `✓`              |

#### Depth/Stencil Format × Stencil Ops

| Format              | Has Stencil | WebGPU stencil ops | Vulkan stencil ops | D3D12 stencil ops | OpenGL stencil ops |
| ------------------- | ----------- | ------------------ | ------------------ | ----------------- | ------------------ |
| `D16_UNORM`         | No          | Must be Undefined  | Ignored            | Ignored           | Ignored            |
| `D32_FLOAT`         | No          | Must be Undefined  | Ignored            | Ignored           | Ignored            |
| `D24_UNORM_S8_UINT` | Yes         | Required           | Required           | Required          | Required           |
| `D32_FLOAT_S8_UINT` | Yes         | Required           | Required           | Required          | Required           |

#### Mapping Semantics

| Property                          | WebGPU         | Vulkan               | D3D12                 | OpenGL               |
| --------------------------------- | -------------- | -------------------- | --------------------- | -------------------- |
| Persistent mapping                | `✗`            | `✓ (VMA persistent)` | `✓ (Upload/Readback)` | `✓ (MAP_PERSISTENT)` |
| Simultaneous CPU+GPU              | `✗`            | `✓ (HOST_COHERENT)`  | `✗`                   | `✓ (MAP_COHERENT)`   |
| Async map                         | `✓ (mapAsync)` | `✗ (synchronous)`    | `✗ (synchronous)`     | `✗ (synchronous)`    |
| Map requires unmap before GPU use | `✓`            | `✗ (coherent)`       | `✓`                   | `✗ (coherent)`       |

### 20a.10 File Layout

```
include/miki/rhi/validation/
├── RhiValidator.h             // RhiValidator class, ValidationResult, ValidationDiagnostic
├── BackendCapabilityTable.h   // BackendCapabilityTable struct, rule structs
├── BackendRules.h             // constexpr rule tables for all backends
└── ValidationMacros.h         // MIKI_RHI_VALIDATION toggle, MIKI_VALIDATE_OR_RETURN macro

src/miki/rhi/validation/
└── RhiValidator.cpp           // ValidateBufferDesc, ValidateTextureDesc, etc.
                               // (only compiled when MIKI_RHI_VALIDATION=1)
```

**Build integration** (`cmake/targets/miki_rhi.cmake`):

```cmake
# Default: ON for Debug/RelWithDebInfo, OFF for Release
if(NOT DEFINED MIKI_RHI_VALIDATION)
    if(CMAKE_BUILD_TYPE MATCHES "Debug|RelWithDebInfo")
        set(MIKI_RHI_VALIDATION ON)
    else()
        set(MIKI_RHI_VALIDATION OFF)
    endif()
endif()

if(MIKI_RHI_VALIDATION)
    target_sources(miki_rhi PRIVATE src/miki/rhi/validation/RhiValidator.cpp)
    target_compile_definitions(miki_rhi PUBLIC MIKI_RHI_VALIDATION=1)
else()
    target_compile_definitions(miki_rhi PUBLIC MIKI_RHI_VALIDATION=0)
endif()
```

### 20a.11 Performance Analysis

| Metric                     | MIKI_RHI_VALIDATION=0 | MIKI_RHI_VALIDATION=1                       |
| -------------------------- | --------------------- | ------------------------------------------- |
| Binary size overhead       | 0 bytes               | ~8 KB (.text) + ~4 KB (.rodata rule tables) |
| Per-CreateBuffer cost      | 0 ns                  | ~200 ns (table lookup + bitmask check)      |
| Per-CreatePipeline cost    | 0 ns                  | ~500 ns (multi-rule check)                  |
| Per-CmdBeginRendering cost | 0 ns                  | ~100 ns (format + stencil check)            |
| Per-CmdDraw cost           | 0 ns                  | 0 ns (**never validated**)                  |
| Memory overhead            | 0 bytes               | ~2 KB static (constexpr tables, no heap)    |
| Compile time impact        | None                  | +~0.2s (one additional TU)                  |

### 20a.12 Design Decisions & Rationale

| Decision                                              | Rationale                                                                        | Alternative Considered                                             |
| ----------------------------------------------------- | -------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| constexpr rule tables                                 | Zero-cost when disabled, cache-friendly when enabled                             | Runtime-registered rules (rejected: unnecessary heap allocation)   |
| Stateless validator                                   | Thread-safe by design, no initialization order issues                            | Singleton validator (rejected: hidden global state)                |
| Validation in DeviceBase, not DeviceHandle            | Catches errors even when using CRTP path directly, not only type-erased dispatch | DeviceHandle-only (rejected: misses CRTP callers)                  |
| Warnings for staging copies, errors for hard failures | Allows portable code to work everywhere while informing about hidden costs       | Error-only (rejected: too strict for Vulkan-legal patterns)        |
| Per-backend tables, not intersection/union            | Preserves backend-specific flexibility (Vulkan direct map is zero-cost)          | Strictest-backend-wins (rejected: degrades Vulkan to WebGPU level) |
| Never validate hot paths                              | Per-draw validation would cost ~5% frame time at 10K draws                       | Sample-based validation (rejected: unpredictable overhead)         |

### 20a.13 Roadmap Integration

| Phase                        | Validation Content                                                            |
| ---------------------------- | ----------------------------------------------------------------------------- |
| Phase 2a (RHI Core)          | Buffer + Texture creation validation, basic limit checks                      |
| Phase 2b (Command Recording) | RenderPass depth/stencil format validation                                    |
| Phase 3 (Descriptors)        | Descriptor layout + pipeline layout limit validation                          |
| Phase 4 (Pipelines)          | Graphics/compute pipeline validation (shader stage compat, attachment format) |
| Phase 6a (Mesh Shader)       | Mesh/task shader workgroup size validation                                    |
| Phase 10 (Sparse)            | Sparse binding page alignment validation                                      |

---

## 20b. Backend Adaptation Layer

### 20b.1 Motivation

§20a validates and rejects or warns. But many "unsupported" configurations are **adaptable** — the backend can transparently emulate the missing capability with a different strategy. Examples:

- WebGPU: `MapWrite + Vertex` → shadow buffer + `wgpuQueueWriteBuffer`
- D3D12: `CpuToGpu + Vertex` → UPLOAD heap + `CopyBufferRegion` to DEFAULT
- WebGPU: push constants → 256B UBO at `@group(0) @binding(0)`
- WebGPU: `CmdBlitTexture` → fullscreen quad shader
- WebGPU: multi-draw indirect → loop over single draws

Currently these adaptations are ad-hoc `if` branches scattered across `*Impl` methods. The Backend Adaptation Layer **structures and centralizes** this knowledge as constexpr data, enabling:

1. **Compile-time query** — "does this backend need adaptation for this config?"
2. **RenderGraph compiler integration** — auto-inject staging passes at graph compile time
3. **Performance diagnostics** — report estimated overhead for each adaptation
4. **Cross-backend portability audit** — enumerate all adaptations for a target backend

### 20b.2 Design Principles

| Principle                         | Detail                                                                                                                                                              |
| --------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **constexpr-first**               | All strategy enums, rule tables, and query functions are `constexpr`. The RenderGraph compiler and `if constexpr` branches can evaluate them at compile time.       |
| **Data, not code**                | Adaptation rules are declarative tables, not imperative `if` chains. Adding a new backend or new adaptation requires adding data rows, not control flow.            |
| **Pure query, no mutation**       | The adaptation layer is a pure function: `(BackendType, config) → AdaptationInfo`. It never modifies descriptors. Actual adaptation logic stays in `*Impl` methods. |
| **Zero runtime cost when unused** | All tables are `constexpr` / `inline const`. Unused tables are DCE'd. Query functions inline to direct comparisons.                                                 |
| **Composable with §20a**          | The validation layer (§20a) calls adaptation queries to decide severity: if adaptation exists → Warning; if no adaptation → Error.                                  |

### 20b.3 AdaptationStrategy Enum

```cpp
namespace miki::rhi::adaptation {

/// How a backend adapts an unsupported configuration.
/// Ordered by typical performance impact (lower = cheaper).
enum class Strategy : uint8_t {
    Native,             ///< Natively supported, no adaptation. Cost factor = 1.0.
    ParameterFixup,     ///< Silent parameter correction (e.g., stencil ops → Undefined). O(1), no extra work.
    UboEmulation,       ///< Push constants emulated via UBO. One extra buffer bind per frame.
    EphemeralMap,       ///< Per-access map/unmap cycle instead of persistent mapping.
    CallbackEmulation,  ///< Sync primitive emulated via async callback (WebGPU fence).
    ShadowBuffer,       ///< CPU shadow + queue write on unmap. Extra memcpy per update.
    StagingCopy,        ///< Intermediate staging resource + GPU copy. Extra command + sync.
    LoopUnroll,         ///< N API calls instead of 1 batched call. O(N) overhead.
    ShaderEmulation,    ///< Missing fixed-function emulated by shader (extra render pass).
    Unsupported,        ///< Cannot be adapted. Returns FeatureNotSupported.
};

/// Compile-time predicate: does this strategy introduce hidden GPU work?
constexpr auto HasGpuOverhead(Strategy s) noexcept -> bool {
    return s >= Strategy::StagingCopy;
}

/// Compile-time predicate: does this strategy introduce hidden CPU work?
constexpr auto HasCpuOverhead(Strategy s) noexcept -> bool {
    return s >= Strategy::ShadowBuffer;
}

/// Compile-time predicate: is this a transparent fixup with no measurable cost?
constexpr auto IsTransparent(Strategy s) noexcept -> bool {
    return s <= Strategy::CallbackEmulation;
}

}  // namespace miki::rhi::adaptation
```

### 20b.4 AdaptationRule Structure

```cpp
namespace miki::rhi::adaptation {

/// Identifies which RHI feature is being adapted.
enum class Feature : uint8_t {
    BufferMapWriteWithUsage,   ///< MapWrite combined with Vertex/Index/Uniform/Storage
    BufferPersistentMapping,   ///< Persistent (long-lived) buffer mapping
    PushConstants,             ///< Native push constant path
    CmdBlitTexture,            ///< Hardware blit
    CmdFillBufferNonZero,      ///< Fill buffer with non-zero value
    CmdClearTexture,           ///< Clear texture outside render pass
    MultiDrawIndirect,         ///< Batched indirect draw (N draws in 1 call)
    DepthOnlyStencilOps,       ///< Stencil ops on depth-only format
    TimelineSemaphore,         ///< Timeline semaphore sync
    SparseBinding,             ///< Sparse/virtual resource binding
    DynamicDepthBias,          ///< Runtime depth bias change
    ExecuteSecondary,          ///< Secondary command buffer / render bundles
    MeshShader,                ///< Mesh/task shader pipeline
    RayTracing,                ///< RT pipeline / acceleration structures
};

/// A single adaptation rule: for (backend, feature) → strategy + cost.
/// All fields are constexpr-friendly (no pointers to runtime data except string literals).
struct Rule {
    Feature feature;
    Strategy strategy;
    float costFactor;           ///< 1.0 = native, 2.0 = ~2x slower. Compile-time constant.
    const char* description;    ///< Static string literal. Human-readable.
};

/// Complete adaptation table for one backend.
/// Declared as inline constexpr arrays + inline const table (same pattern as §20a).
struct BackendAdaptationTable {
    BackendType backend;
    std::span<const Rule> rules;
};

}  // namespace miki::rhi::adaptation
```

### 20b.5 Per-Backend Adaptation Tables

#### WebGPU (Tier 3) — Most Adaptations

```cpp
inline constexpr adaptation::Rule kWebGPUAdaptations[] = {
    {
        .feature = Feature::BufferMapWriteWithUsage,
        .strategy = Strategy::ShadowBuffer,
        .costFactor = 1.5f,
        .description = "CPU shadow buffer + wgpuQueueWriteBuffer on unmap. "
                       "Extra memcpy per buffer update."
    },
    {
        .feature = Feature::BufferPersistentMapping,
        .strategy = Strategy::EphemeralMap,
        .costFactor = 1.1f,
        .description = "mapAsync/unmap per access cycle. Non-blocking but not persistent."
    },
    {
        .feature = Feature::PushConstants,
        .strategy = Strategy::UboEmulation,
        .costFactor = 1.05f,
        .description = "256B UBO at @group(0) @binding(0). User bindings shift to group(N+1)."
    },
    {
        .feature = Feature::CmdBlitTexture,
        .strategy = Strategy::ShaderEmulation,
        .costFactor = 2.0f,
        .description = "Fullscreen quad shader blit. Requires extra render pass + pipeline."
    },
    {
        .feature = Feature::CmdFillBufferNonZero,
        .strategy = Strategy::ShaderEmulation,
        .costFactor = 1.8f,
        .description = "Compute shader fill. Extra dispatch + barrier."
    },
    {
        .feature = Feature::CmdClearTexture,
        .strategy = Strategy::ShaderEmulation,
        .costFactor = 1.5f,
        .description = "Clear via render pass loadOp. Requires begin/end render pass."
    },
    {
        .feature = Feature::MultiDrawIndirect,
        .strategy = Strategy::LoopUnroll,
        .costFactor = 3.0f,
        .description = "WebGPU: 1 draw per indirect call. N draws = N wgpuRenderPassEncoderDrawIndirect calls."
    },
    {
        .feature = Feature::DepthOnlyStencilOps,
        .strategy = Strategy::ParameterFixup,
        .costFactor = 1.0f,
        .description = "Stencil ops forced to Undefined, stencilReadOnly=true. Zero overhead."
    },
    {
        .feature = Feature::TimelineSemaphore,
        .strategy = Strategy::CallbackEmulation,
        .costFactor = 1.0f,
        .description = "onSubmittedWorkDone callback + monotonic serial. Semantically equivalent."
    },
    {
        .feature = Feature::DynamicDepthBias,
        .strategy = Strategy::Unsupported,
        .costFactor = 0.0f,
        .description = "WebGPU sets depth bias at pipeline creation. No dynamic override."
    },
    {
        .feature = Feature::SparseBinding,
        .strategy = Strategy::Unsupported,
        .costFactor = 0.0f,
        .description = "WebGPU has no sparse resource API."
    },
    {
        .feature = Feature::MeshShader,
        .strategy = Strategy::Unsupported,
        .costFactor = 0.0f,
        .description = "WebGPU has no mesh/task shader support."
    },
    {
        .feature = Feature::RayTracing,
        .strategy = Strategy::Unsupported,
        .costFactor = 0.0f,
        .description = "WebGPU has no ray tracing API."
    },
};

inline const adaptation::BackendAdaptationTable kWebGPUAdaptationTable = {
    .backend = BackendType::WebGPU,
    .rules = kWebGPUAdaptations,
};
```

#### D3D12 (Tier 1)

```cpp
inline constexpr adaptation::Rule kD3D12Adaptations[] = {
    {
        .feature = Feature::BufferMapWriteWithUsage,
        .strategy = Strategy::StagingCopy,
        .costFactor = 1.3f,
        .description = "UPLOAD heap + CopyBufferRegion to DEFAULT. One extra copy command."
    },
    {
        .feature = Feature::CmdBlitTexture,
        .strategy = Strategy::ShaderEmulation,
        .costFactor = 2.0f,
        .description = "Fullscreen quad shader. D3D12 has no native blit."
    },
    {
        .feature = Feature::CmdFillBufferNonZero,
        .strategy = Strategy::ShaderEmulation,
        .costFactor = 1.5f,
        .description = "UAV clear or compute shader. No native non-zero fill."
    },
};

inline const adaptation::BackendAdaptationTable kD3D12AdaptationTable = {
    .backend = BackendType::D3D12,
    .rules = kD3D12Adaptations,
};
```

#### Vulkan (Tier 1 / Tier 2)

```cpp
inline constexpr adaptation::Rule kVulkanAdaptations[] = {
    // Vulkan 1.4 supports all RHI features natively.
    // Empty table = no adaptations needed.
    // VulkanCompat (Tier 2) may need mesh shader / RT as Unsupported — added at runtime
    // via GpuCapabilityProfile query, not static table.
};

inline const adaptation::BackendAdaptationTable kVulkanAdaptationTable = {
    .backend = BackendType::Vulkan14,
    .rules = kVulkanAdaptations,
};
```

#### OpenGL (Tier 4)

```cpp
inline constexpr adaptation::Rule kOpenGLAdaptations[] = {
    {
        .feature = Feature::PushConstants,
        .strategy = Strategy::UboEmulation,
        .costFactor = 1.05f,
        .description = "128B UBO at binding 0. User UBO bindings shifted +1."
    },
    {
        .feature = Feature::MeshShader,
        .strategy = Strategy::Unsupported,
        .costFactor = 0.0f,
        .description = "OpenGL 4.3 has no mesh shader extension."
    },
    {
        .feature = Feature::RayTracing,
        .strategy = Strategy::Unsupported,
        .costFactor = 0.0f,
        .description = "OpenGL has no ray tracing API."
    },
    {
        .feature = Feature::SparseBinding,
        .strategy = Strategy::Unsupported,
        .costFactor = 0.0f,
        .description = "OpenGL sparse texture exists but not wired in T4."
    },
};

inline const adaptation::BackendAdaptationTable kOpenGLAdaptationTable = {
    .backend = BackendType::OpenGL43,
    .rules = kOpenGLAdaptations,
};
```

### 20b.6 Constexpr Query API

The core API: all functions are `constexpr` (evaluable at compile time when inputs are known).

```cpp
namespace miki::rhi::adaptation {

/// Lookup the adaptation table for a backend.
/// Returns nullptr for Mock backend.
constexpr auto GetAdaptationTable(BackendType backend) -> const BackendAdaptationTable*;

/// Query the adaptation strategy for a specific feature on a backend.
/// Returns Strategy::Native if no adaptation rule exists (feature natively supported).
/// This is the primary query function — O(N) linear scan over rules, N ≤ 15.
constexpr auto QueryStrategy(BackendType backend, Feature feature) -> Strategy {
    auto* table = GetAdaptationTable(backend);
    if (!table) return Strategy::Native;
    for (auto& rule : table->rules) {
        if (rule.feature == feature) return rule.strategy;
    }
    return Strategy::Native;  // Not in table → natively supported
}

/// Query the full rule (strategy + cost + description) for a feature.
/// Returns nullptr if natively supported.
constexpr auto QueryRule(BackendType backend, Feature feature) -> const Rule* {
    auto* table = GetAdaptationTable(backend);
    if (!table) return nullptr;
    for (auto& rule : table->rules) {
        if (rule.feature == feature) return &rule;
    }
    return nullptr;
}

/// Query the cost factor for a feature. Returns 1.0f if native.
constexpr auto QueryCostFactor(BackendType backend, Feature feature) -> float {
    auto* rule = QueryRule(backend, feature);
    return rule ? rule->costFactor : 1.0f;
}

/// Check if a feature is supported (native or adapted, but not Unsupported).
constexpr auto IsFeatureAvailable(BackendType backend, Feature feature) -> bool {
    return QueryStrategy(backend, feature) != Strategy::Unsupported;
}

/// Check if a feature requires hidden GPU work on this backend.
constexpr auto RequiresHiddenGpuWork(BackendType backend, Feature feature) -> bool {
    return HasGpuOverhead(QueryStrategy(backend, feature));
}

}  // namespace miki::rhi::adaptation
```

**Compile-time usage example** (in template code or `if constexpr`):

```cpp
// At RenderGraph compile time, check if staging passes are needed
if constexpr (adaptation::QueryStrategy(BackendType::WebGPU,
              adaptation::Feature::BufferMapWriteWithUsage) == adaptation::Strategy::ShadowBuffer) {
    // Compiler knows at compile time: WebGPU needs shadow buffer pattern
    // Can auto-insert a staging copy pass in the render graph
}
```

**Runtime usage example** (when backend is not known at compile time):

```cpp
auto strategy = adaptation::QueryStrategy(device.GetBackendType(),
                                          adaptation::Feature::MultiDrawIndirect);
if (strategy == adaptation::Strategy::LoopUnroll) {
    MIKI_LOG_WARN("[Adaptation] Multi-draw indirect will loop {} draws on {}",
                  drawCount, device.GetBackendName());
}
```

### 20b.7 Integration with §20a Validation Layer

The validation layer (§20a) composes with the adaptation layer (§20b) to determine diagnostic severity:

```cpp
// Inside RhiValidator::ValidateBufferDesc (§20a)
auto strategy = adaptation::QueryStrategy(backend, adaptation::Feature::BufferMapWriteWithUsage);

switch (strategy) {
    case adaptation::Strategy::Native:
        // No diagnostic — natively supported
        break;
    case adaptation::Strategy::Unsupported:
        result.passed = false;
        result.diagnostics.push_back({
            .severity = Severity::Error,
            .message = "MapWrite + resource usage",
            .rationale = "Not supported and cannot be adapted",
            .backend = backend,
            .strategy = strategy,
        });
        break;
    default:
        // Adapted — emit warning with cost info
        auto* rule = adaptation::QueryRule(backend, feature);
        result.diagnostics.push_back({
            .severity = Severity::Warning,
            .message = rule->description,
            .rationale = "Adapted via hidden mechanism",
            .backend = backend,
            .strategy = strategy,
            .costFactor = rule->costFactor,
        });
        break;
}
```

Updated `ValidationDiagnostic` struct (§20a.6 amendment):

```cpp
struct ValidationDiagnostic {
    Severity severity;
    const char* message;
    const char* rationale;
    BackendType backend;
    adaptation::Strategy strategy = adaptation::Strategy::Native;  ///< §20b integration
    float costFactor = 1.0f;                                       ///< §20b integration
};
```

### 20b.8 RenderGraph Compiler Integration

The RenderGraph compiler (see `04-render-graph.md` §5.1 Stage 9) can use the adaptation layer at **graph compile time** to automatically inject auxiliary passes when the target backend requires adaptation.

```
RenderGraphBuilder (user declares passes)
    │
    ▼
RenderGraphCompiler::Compile(builder, backendType)
    │
    ├─ [1] Kahn sort + dead pass culling (see `04-render-graph.md` §5.1 Stages 1-3)
    │
    ├─ [2] Adaptation pass injection (NEW — §20b)    ◄── uses constexpr QueryStrategy
    │       │
    │       ├─ For each buffer resource with CpuToGpu + resource usage:
    │       │   if QueryStrategy(backend, BufferMapWriteWithUsage) == StagingCopy:
    │       │       → inject Transfer pass: staging buffer → resource buffer
    │       │       → rewrite original pass to read from device-local buffer
    │       │
    │       ├─ For each CmdBlitTexture in a pass:
    │       │   if QueryStrategy(backend, CmdBlitTexture) == ShaderEmulation:
    │       │       → inject Graphics pass: fullscreen quad blit shader
    │       │       → update dependency edges
    │       │
    │       └─ For each resource requiring ParameterFixup:
    │           → modify CompiledPass metadata (no new pass needed)
    │
    ├─ [3] Barrier insertion (see `04-render-graph.md` §5.4, operates on expanded pass list)
    │
    ├─ [4] Transient aliasing / lifetime analysis (existing)
    │
    └─ [5] Emit AdaptationReport (§20b.9)
```

**Key property**: The compiler sees the full graph and can make globally optimal decisions. For example:

- If multiple passes write to the same `CpuToGpu` buffer, a single staging copy pass can serve all of them
- If a `StagingCopy` adaptation creates a new temporary buffer, the transient aliasing system can alias it with other temporaries
- If `ShaderEmulation` is needed, the compiler can batch multiple blit operations into one pass

### 20b.9 AdaptationReport

The compiler produces an `AdaptationReport` alongside the `CompiledGraph`:

```cpp
namespace miki::rhi::adaptation {

/// One adaptation action taken by the compiler.
struct AdaptationAction {
    Feature feature;
    Strategy strategy;
    float costFactor;
    const char* description;
    uint32_t affectedPassIndex;    ///< Index in CompiledGraph.passes
    uint32_t injectedPassCount;    ///< Number of passes injected (0 for ParameterFixup)
};

/// Summary of all adaptations applied during graph compilation.
struct AdaptationReport {
    BackendType backend;
    std::vector<AdaptationAction> actions;
    float totalEstimatedOverhead;  ///< Product of all costFactors (multiplicative)

    /// Human-readable summary for debug overlay / profiling.
    [[nodiscard]] auto Summary() const -> std::string;

    /// Check if any adaptation has GPU overhead.
    [[nodiscard]] auto HasGpuOverhead() const noexcept -> bool {
        return std::ranges::any_of(actions, [](auto& a) {
            return adaptation::HasGpuOverhead(a.strategy);
        });
    }

    /// Count adaptations by strategy type.
    [[nodiscard]] auto CountByStrategy(Strategy s) const noexcept -> uint32_t {
        return static_cast<uint32_t>(std::ranges::count_if(actions, [s](auto& a) {
            return a.strategy == s;
        }));
    }
};

}  // namespace miki::rhi::adaptation
```

**Usage in debug overlay**:

```
[Adaptation Report — WebGPU]
  3 ShadowBuffer adaptations (cost: 1.5x each)
  1 ShaderEmulation adaptation (CmdBlitTexture, cost: 2.0x)
  2 ParameterFixup adaptations (zero cost)
  1 LoopUnroll adaptation (MultiDrawIndirect, cost: 3.0x)
  Total estimated overhead: ~9.0x over native Vulkan path
  Injected passes: 2 (1 staging copy, 1 blit shader)
```

### 20b.10 Constexpr Adaptation Matrix

Complete cross-backend feature × strategy matrix. This table is the **single source of truth** for all adaptation tables.

| Feature                   | Vulkan T1 | VulkanCompat T2 | D3D12 T1               | WebGPU T3                | OpenGL T4            |
| ------------------------- | --------- | --------------- | ---------------------- | ------------------------ | -------------------- |
| `BufferMapWriteWithUsage` | Native    | Native          | StagingCopy (1.3x)     | ShadowBuffer (1.5x)      | Native               |
| `BufferPersistentMapping` | Native    | Native          | Native                 | EphemeralMap (1.1x)      | Native               |
| `PushConstants`           | Native    | Native          | Native                 | UboEmulation (1.05x)     | UboEmulation (1.05x) |
| `CmdBlitTexture`          | Native    | Native          | ShaderEmulation (2.0x) | ShaderEmulation (2.0x)   | Native               |
| `CmdFillBufferNonZero`    | Native    | Native          | ShaderEmulation (1.5x) | ShaderEmulation (1.8x)   | Native               |
| `CmdClearTexture`         | Native    | Native          | Native                 | ShaderEmulation (1.5x)   | Native               |
| `MultiDrawIndirect`       | Native    | Native          | Native                 | LoopUnroll (3.0x)        | Native               |
| `DepthOnlyStencilOps`     | Native    | Native          | Native                 | ParameterFixup (1.0x)    | Native               |
| `TimelineSemaphore`       | Native    | Native          | Native                 | CallbackEmulation (1.0x) | Native               |
| `DynamicDepthBias`        | Native    | Native          | Native                 | Unsupported              | Native               |
| `SparseBinding`           | Native    | Native          | Native                 | Unsupported              | Unsupported          |
| `MeshShader`              | Native    | Unsupported     | Native                 | Unsupported              | Unsupported          |
| `RayTracing`              | Native    | Unsupported     | Native                 | Unsupported              | Unsupported          |
| `ExecuteSecondary`        | Native    | Native          | Native                 | Unsupported              | Native               |

### 20b.11 Compile-Time Adaptation for if constexpr Paths

When the backend is a template parameter (CRTP path through `DeviceBase<Impl>`), the adaptation query can be resolved entirely at compile time:

```cpp
template <typename Impl>
class DeviceBase {
    static constexpr BackendType kBackend = Impl::kBackendType;

public:
    [[nodiscard]] auto CreateBuffer(const BufferDesc& desc) -> RhiResult<BufferHandle> {
        // §20a validation (compile-time conditional)
#if MIKI_RHI_VALIDATION
        if (auto vr = validation::RhiValidator::ValidateBufferDesc(kBackend, desc); !vr) {
            // ... handle diagnostics
        }
#endif
        // §20b adaptation query — resolved at compile time via if constexpr
        if constexpr (adaptation::QueryStrategy(kBackend,
                      adaptation::Feature::BufferMapWriteWithUsage) == adaptation::Strategy::ShadowBuffer) {
            // Compiler statically knows: this backend uses shadow buffers
            // The *Impl method handles the actual adaptation
            // This branch is for diagnostic / metric purposes only
        }

        return Self().CreateBufferImpl(desc);
    }
};
```

**Requirement**: Each backend device class must declare a `static constexpr BackendType kBackendType`:

```cpp
class VulkanDevice : public DeviceBase<VulkanDevice> {
public:
    static constexpr BackendType kBackendType = BackendType::Vulkan14;
    // ...
};

class WebGPUDevice : public DeviceBase<WebGPUDevice> {
public:
    static constexpr BackendType kBackendType = BackendType::WebGPU;
    // ...
};
```

This enables the compiler to eliminate dead adaptation branches entirely in Release builds.

### 20b.12 Constexpr Correctness Guarantees

All adaptation structures and query functions are designed for maximum compile-time evaluability:

| Component                 | constexpr?                | Rationale                                                            |
| ------------------------- | ------------------------- | -------------------------------------------------------------------- |
| `Strategy` enum           | `constexpr` (scoped enum) | Enum values are ICE (integral constant expressions)                  |
| `Feature` enum            | `constexpr` (scoped enum) | Same                                                                 |
| `Rule` struct             | `constexpr` constructible | All fields are literals or `const char*` (pointer to string literal) |
| `HasGpuOverhead()`        | `constexpr`               | Must be callable from both constexpr and runtime contexts            |
| `HasCpuOverhead()`        | `constexpr`               | Same                                                                 |
| `IsTransparent()`         | `constexpr`               | Same                                                                 |
| `QueryStrategy()`         | `constexpr`               | Evaluable at compile time when `backend` is constexpr                |
| `QueryRule()`             | `constexpr`               | Same (returns pointer into constexpr array)                          |
| `QueryCostFactor()`       | `constexpr`               | Same                                                                 |
| `IsFeatureAvailable()`    | `constexpr`               | Same                                                                 |
| `RequiresHiddenGpuWork()` | `constexpr`               | Same                                                                 |
| `kWebGPUAdaptations[]`    | `constexpr` array         | All elements are literal types                                       |
| `BackendAdaptationTable`  | `inline const`            | Contains `std::span` (non-constexpr pointer member)                  |
| `GetAdaptationTable()`    | `constexpr`               | Switch on BackendType, returns address of `inline const` table       |

**Note on `std::span` and constexpr**: The `BackendAdaptationTable` struct contains `std::span<const Rule>` which stores a pointer. In C++23, `constexpr` variables cannot hold pointers to non-constexpr objects. The tables are therefore `inline const` (not `constexpr`). However, `QueryStrategy()` is still `constexpr`-evaluable when the compiler can prove the table pointer is a constant expression (e.g., when `backend` is a template parameter and the function is inlined). In practice, GCC/Clang/MSVC all optimize this to a direct table lookup. For guaranteed `consteval` paths, use `QueryStrategyConsteval()`:

```cpp
/// Guaranteed compile-time evaluation. Uses switch-case instead of table lookup.
/// This bypasses BackendAdaptationTable entirely — hardcoded per-backend.
consteval auto QueryStrategyConsteval(BackendType backend, Feature feature) -> Strategy {
    if (backend == BackendType::WebGPU) {
        switch (feature) {
            case Feature::BufferMapWriteWithUsage: return Strategy::ShadowBuffer;
            case Feature::BufferPersistentMapping:  return Strategy::EphemeralMap;
            case Feature::PushConstants:            return Strategy::UboEmulation;
            case Feature::CmdBlitTexture:           return Strategy::ShaderEmulation;
            case Feature::CmdFillBufferNonZero:     return Strategy::ShaderEmulation;
            case Feature::CmdClearTexture:          return Strategy::ShaderEmulation;
            case Feature::MultiDrawIndirect:        return Strategy::LoopUnroll;
            case Feature::DepthOnlyStencilOps:      return Strategy::ParameterFixup;
            case Feature::TimelineSemaphore:        return Strategy::CallbackEmulation;
            case Feature::DynamicDepthBias:         return Strategy::Unsupported;
            case Feature::SparseBinding:            return Strategy::Unsupported;
            case Feature::MeshShader:               return Strategy::Unsupported;
            case Feature::RayTracing:               return Strategy::Unsupported;
            case Feature::ExecuteSecondary:         return Strategy::Unsupported;
            default: return Strategy::Native;
        }
    }
    if (backend == BackendType::D3D12) {
        switch (feature) {
            case Feature::BufferMapWriteWithUsage: return Strategy::StagingCopy;
            case Feature::CmdBlitTexture:          return Strategy::ShaderEmulation;
            case Feature::CmdFillBufferNonZero:    return Strategy::ShaderEmulation;
            default: return Strategy::Native;
        }
    }
    if (backend == BackendType::OpenGL43) {
        switch (feature) {
            case Feature::PushConstants: return Strategy::UboEmulation;
            case Feature::MeshShader:    return Strategy::Unsupported;
            case Feature::RayTracing:    return Strategy::Unsupported;
            case Feature::SparseBinding: return Strategy::Unsupported;
            default: return Strategy::Native;
        }
    }
    // Vulkan14, VulkanCompat — all native except VulkanCompat mesh/RT
    return Strategy::Native;
}
```

This dual-path design (`constexpr` table-based for runtime + `consteval` switch-based for compile-time) ensures:

- **Runtime path**: data-driven, extensible, no code changes to add rules
- **Compile-time path**: guaranteed ICE evaluation, enables `if constexpr` and `static_assert`

### 20b.13 RenderGraph Compile-Time Resource Fixup

When the RenderGraph compiler knows the target backend at compile time (template parameter), it can use `consteval` queries to **statically transform** the graph:

```cpp
template <BackendType Backend>
class RenderGraphCompiler {
public:
    static auto Compile(RenderGraphBuilder&& builder) -> expected<CompiledGraph, ErrorCode> {
        // Phase 1: existing Kahn sort + culling
        auto sorted = KahnSort(builder);

        // Phase 2: constexpr adaptation pass injection
        if constexpr (adaptation::QueryStrategyConsteval(Backend,
                      adaptation::Feature::BufferMapWriteWithUsage) != adaptation::Strategy::Native) {
            InjectStagingPasses(sorted, builder);  // compile-time decision, runtime execution
        }

        if constexpr (adaptation::QueryStrategyConsteval(Backend,
                      adaptation::Feature::CmdBlitTexture) == adaptation::Strategy::ShaderEmulation) {
            RewriteBlitToShaderPass(sorted, builder);
        }

        // Phase 3: barrier insertion (on expanded graph)
        InsertBarriers(sorted);

        // Phase 4: lifetime analysis
        ComputeLifetimes(sorted);

        return CompiledGraph{std::move(sorted)};
    }
};
```

**Key benefit**: When compiling a RenderGraph for Vulkan, the `if constexpr` branches for ShadowBuffer, ShaderEmulation blit, etc. are **completely eliminated**. The Vulkan compilation path has zero adaptation overhead — not even a branch instruction.

### 20b.14 File Layout

```
include/miki/rhi/adaptation/
├── AdaptationTypes.h           // Strategy enum, Feature enum, Rule struct, predicates
├── BackendAdaptationTable.h    // BackendAdaptationTable struct
├── BackendAdaptations.h        // Per-backend constexpr rule tables
├── AdaptationQuery.h           // QueryStrategy, QueryRule, constexpr + consteval APIs
└── AdaptationReport.h          // AdaptationReport, AdaptationAction (runtime only)

src/miki/rhi/adaptation/
└── AdaptationQuery.cpp         // GetAdaptationTable() implementation
                                // (trivial switch — could be header-only)
```

**Build integration**: The adaptation module has no separate build toggle — it is always available as constexpr data. The `AdaptationReport` (§20b.9) is runtime-only and guarded by `MIKI_RHI_VALIDATION`.

### 20b.15 Performance Analysis

| Metric                        | Compile-time path            | Runtime path                             |
| ----------------------------- | ---------------------------- | ---------------------------------------- |
| Query cost                    | 0 ns (ICE / `consteval`)     | ~50 ns (linear scan, N ≤ 15)             |
| Binary size                   | 0 bytes (eliminated by DCE)  | ~6 KB (.rodata tables + query functions) |
| RenderGraph compiler overhead | 0 (dead branches eliminated) | ~100 ns per resource (adaptation check)  |
| AdaptationReport generation   | N/A                          | ~500 ns (small vector + string format)   |
| Memory overhead               | 0 bytes                      | ~1 KB static (constexpr arrays)          |

### 20b.16 Design Decisions & Rationale

| Decision                                          | Rationale                                                      | Alternative Considered                                        |
| ------------------------------------------------- | -------------------------------------------------------------- | ------------------------------------------------------------- |
| Dual-path: `constexpr` table + `consteval` switch | Table for extensibility, switch for guaranteed ICE             | Table-only (rejected: `std::span` prevents `consteval`)       |
| `Feature` enum instead of string tags             | Type-safe, `switch`-friendly, constexpr-compatible             | `const char*` tags (rejected: no compile-time matching)       |
| `costFactor` as `float`                           | Intuitive multiplier, composable (multiply for chain)          | Microsecond estimate (rejected: hardware-dependent)           |
| Adaptation layer separate from validation layer   | SRP: validation checks legality, adaptation describes strategy | Merged layer (rejected: conflates "is it legal?" with "how?") |
| `inline const` tables, not `constexpr`            | `std::span` member prevents constexpr aggregate                | Raw arrays + size (rejected: loses span ergonomics)           |
| RenderGraph integration at compiler, not executor | Compiler sees full graph → globally optimal decisions          | Per-pass runtime check (rejected: local, cannot batch)        |

### 20b.17 Roadmap Integration

| Phase                        | Adaptation Content                                                                              |
| ---------------------------- | ----------------------------------------------------------------------------------------------- |
| Phase 2a (RHI Core)          | `AdaptationTypes.h`, `BackendAdaptations.h`, `AdaptationQuery.h` — constexpr tables and queries |
| Phase 2b (Command Recording) | `DepthOnlyStencilOps` adaptation wired in `CmdBeginRendering`                                   |
| Phase 3a (RenderGraph)       | RenderGraph compiler integration: staging pass injection, blit rewrite                          |
| Phase 3a (RenderGraph)       | `AdaptationReport` generation at compile time                                                   |
| Phase 4 (Pipelines)          | `PushConstants` UBO emulation adaptation tracking                                               |
| Phase 5 (Advanced Rendering) | `CmdBlitTexture`, `CmdClearTexture` shader emulation adaptation                                 |

---

## 21. Non-Pipeline Data Contracts

This section documents the **GPU-side data contracts** for application-layer systems. These systems are NOT part of the rendering pipeline -- they produce data that the pipeline consumes via UBOs/SSBOs/push constants.

### 21.1 Camera UBO Contract

Application-layer camera controller (orbit/pan/zoom/walk/fly/turntable/trackball/6-DOF) produces:

```cpp
struct GpuCameraUBO {                       // 368B, set 0 binding 0, alignas(16)
    float4x4  viewProjMatrix;               // offset   0, 64B -- current frame V*P
    float4x4  prevViewProjMatrix;           // offset  64, 64B -- previous frame V*P (motion vectors, TAA reprojection)
    float4x4  viewMatrix;                   // offset 128, 64B -- camera view matrix (screen-space effects)
    float4x4  projMatrix;                   // offset 192, 64B -- Reverse-Z projection (infinite far optional)
    float4x4  invViewProjMatrix;            // offset 256, 64B -- screen->world depth unprojection
    float3    cameraPosition;               // offset 320, 16B (float3 is alignas(16), 4B pad)
    float     nearPlane;                    // offset 336, 4B
    float     farPlane;                     // offset 340, 4B
    float     jitterX;                      // offset 344, 4B -- TAA Halton(2,3) sub-pixel X (NDC)
    float     jitterY;                      // offset 348, 4B -- TAA Halton(2,3) sub-pixel Y (NDC)
    float     resolutionX;                  // offset 352, 4B -- render target width (px)
    float     resolutionY;                  // offset 356, 4B -- render target height (px)
    float     _pad[2];                      // offset 360, 8B -- pad to 368B
};
static_assert(sizeof(GpuCameraUBO) == 368);
// Matches include/miki/render/CameraUBO.h and shaders/common/camera_ubo.slang
```

**Design rationale** (368B vs 256B):

- `prevViewProjMatrix` (64B): required for TAA reprojection (#53), motion vectors (#10 RT2), motion blur (#51). Cannot be derived from current frame data alone.
- Separate `viewMatrix` + `projMatrix` (128B): SSR (#48), GTAO (#15), and depth linearization need individual matrices, not just the product. Avoids redundant per-pass push constants.
- `resolutionX/Y` (8B): eliminates `viewport` float4 (saves 8B) while providing the commonly-needed width/height. `1/w, 1/h` computed in shader (1 RCP each, negligible).
- Total cost: +112B vs 256B design. One UBO per frame = 368B. Negligible vs GBuffer (132MB) or VisBuffer (67MB).

Projection modes: Perspective, Orthographic, Axonometric (Iso/Dimetric/Trimetric), Stereographic (XR). Smooth transitions: SLERP orientation + cubic Bezier position, 300ms ease-in-out. Named views: serialized `{CameraState, DisplayStyle, ClipPlaneSet}`.

### 21.2 Animation Data Contract

Animation timeline (CPU) evaluates keyframes -> updates GPU buffers each frame:

| Animation Type          | GPU Buffer Updated                     | Cost             |
| ----------------------- | -------------------------------------- | ---------------- |
| Exploded view           | GpuInstance.worldMatrix in SceneBuffer | ~0               |
| Turntable / Flythrough  | GpuCameraUBO                           | ~0               |
| Kinematic (joints)      | GpuInstance.worldMatrix                | ~0               |
| Transient CAE results   | Scalar SSBO swap                       | Streaming upload |
| Light animation         | GpuLight[] dirty entries               | ~0               |
| Section plane animation | clipPlane push constant                | ~0               |

Video export: offline render loop (set time -> full pipeline -> readback -> FFmpeg encode).

### 21.3 Gizmo State Contract

Application-layer gizmo controller produces `GizmoState SSBO`:

```
GizmoState {type, transform, activeAxis, hoverAxis, mode, pivotMode, coordSpace}
```

Snap engine (CPU): grid snap `round(delta, increment)` + geometry snap via GPU pick (§10.3). Numeric input: dynamic tooltip text in HUD layer.

### 21.4 Measurement Contract

Application-layer measurement engine (CPU) computes results, GPU renders visualization:

```
MeasurementResult[] -> Overlay layer (SDF lines + MSDF text)
  Point-to-point, edge length, face-to-face, angle, radius, wall thickness, clearance
  Cumulative chain with running total
  Persistent until user clears
  Export: clipboard / CSV / JSON
```

Wall thickness uses GPU ray query (§10.6) for acceleration.

### 21.5 Scene Tree / Property API Contract

Scene tree and property panel are application-layer UI. Renderer provides:

| Feature             | Renderer API                                            |
| ------------------- | ------------------------------------------------------- |
| Visibility toggle   | `CadScene::SetVisibility(entityId, bool)` -> layer mask |
| Transparency toggle | `AttributeResolver::SetTransparency(entityId, float)`   |
| Color override      | `GpuInstance.colorOverride` via AttributeResolver       |
| Tree-3D sync        | Pick->entityId (§10.3) + SelectionManager               |
| Layer management    | `CadSceneLayer` visibility/selectability/color (§5.8)   |
| Freeze/lock         | `GpuInstance.flags` freeze bit                          |
| Isolate/focus       | Temporary layer mask                                    |

### 21.6 Accessibility / Theming Contract

Push constants / LUT swaps only -- zero pipeline changes:

| Feature              | GPU Mechanism                                            |
| -------------------- | -------------------------------------------------------- |
| Dark/light theme     | Background clear color push constant                     |
| High-contrast        | Force high-contrast palette in GpuInstance.colorOverride |
| Color-blind palettes | Color ramp LUT swap (viridis/cividis)                    |
| Configurable colors  | Selection/hover/background push constants                |
| Font DPI scaling     | TextRenderer DPI-aware scaling factor                    |

### 21.7 Multi-Viewport Contract

Each viewport owns independent: `{CameraState, DisplayStyle, RenderGraph, LayerStack, analysisOverlay, ClipPlaneSet}`. Layout modes: Single, SplitH/V, Quad, Tri, Custom (1-8), PiP. Linked views: selection sync always on, optional camera sync. Multi-window: each OS window owns ViewportLayout + swapchain; shared SceneBuffer/BindlessTable/LightBuffer.

### 21.8 Collaboration / Digital Twin / Additive Mfg Contracts

These are application-layer protocols that produce data consumed by existing pipeline passes:

| Domain                 | GPU Data Source                      | Pipeline Pass            |
| ---------------------- | ------------------------------------ | ------------------------ |
| Shared viewport        | Camera UBO broadcast                 | Standard rendering       |
| Real-time markup       | MarkupAnnotation[]                   | SVG Overlay (§13.4)      |
| Presence indicators    | Cursor position SSBO                 | Overlay layer dot render |
| Sensor overlay (IoT)   | PmiAnnotation[]                      | PMI pass (§13.1)         |
| Heatmap (IoT)          | Scalar array                         | Color map (§11.2)        |
| Alert visualization    | GpuInstance.colorOverride (animated) | Standard rendering       |
| Slice preview (AM)     | Section plane                        | Section pass (§10.2)     |
| Build orientation (AM) | Draft angle pull direction           | Draft angle (§10.5)      |

No new GPU passes required for any of these domains.

---

## Appendix A: Shader IR Compilation

All shaders authored in **Slang**. Compiled at runtime to target IR:

```
Slang source (.slang)
    | SlangCompiler
    +-- SPIR-V  (Vulkan, Compat, OpenGL via GL_ARB_gl_spirv)
    +-- DXIL    (D3D12)
    +-- GLSL    (offline/debug only)
    +-- WGSL    (WebGPU)
    +-- MSL     (Metal — Phase 15a+)
```

- Reflection-driven descriptor layout generation
- File-hash disk cache for compiled modules
- Shader permutation cache for material graph variants
- Hot-reload in debug builds

## Appendix B: Key Vulkan Extensions

| Extension                         | Usage                                    | Core In                                                        |
| --------------------------------- | ---------------------------------------- | -------------------------------------------------------------- |
| VK_KHR_dynamic_rendering          | Renderpass-less rendering                | Vulkan 1.3                                                     |
| VK_EXT_mesh_shader                | Task/Mesh shader                         | --                                                             |
| VK_KHR_ray_query                  | Picking, RTAO, RT reflections/shadows/GI | --                                                             |
| VK_KHR_acceleration_structure     | BLAS/TLAS                                | --                                                             |
| VK_EXT_descriptor_buffer          | Bindless descriptor                      | Vulkan 1.4 (optional feature, requires `descriptorBuffer` bit) |
| VK_KHR_fragment_shading_rate      | VRS                                      | --                                                             |
| VK_KHR_timeline_semaphore         | Async compute sync                       | Vulkan 1.2                                                     |
| VK_KHR_cooperative_matrix         | Batched 4x4 transform                    | --                                                             |
| VK_KHR_shader_integer_dot_product | dp4a normal cone cull                    | Vulkan 1.3                                                     |
| VK_KHR_multiview                  | XR single-pass stereo                    | Vulkan 1.1                                                     |
| VK_EXT_headless_surface           | Offscreen/CI rendering                   | --                                                             |

## Appendix C: Performance Targets

| Metric                    | Target                    | Hardware        |
| ------------------------- | ------------------------- | --------------- |
| <1M tri interactive       | >=60 fps                  | Any tier        |
| 1-10M tri interactive     | >=60 fps @4K              | RTX 4070        |
| 10-100M tri + LOD         | >=30 fps @4K              | RTX 4070        |
| 100M+ tri streaming       | >=15 fps @4K              | RTX 4070        |
| 2B tri streaming          | >=30 fps @4K              | RTX 4090 (24GB) |
| VR/XR per-eye             | >=90 fps, <11.1ms         | RTX 4070        |
| CAE 10M elements          | >=30 fps                  | RTX 4070        |
| Point cloud 1B pts        | >=20 fps                  | RTX 4070        |
| Single pick               | <0.5ms                    | Tier1           |
| Area pick (box drill)     | <3ms                      | Tier1           |
| HLR 10M edges             | <4ms                      | RTX 4070        |
| Clustered lights 1K       | <0.3ms culling            | RTX 4070        |
| Full post-process chain   | <5ms total                | RTX 4070 @4K    |
| VRAM 2B tri scene         | <12GB                     | --              |
| First frame (streaming)   | <100ms                    | --              |
| Input-to-display          | <=50ms                    | --              |
| Selection feedback        | <=100ms                   | --              |
| Mode switch               | <=1 frame (pre-built PSO) | --              |
| Point cloud splat 10M     | <2ms                      | RTX 4070        |
| Base VRAM (empty scene)   | <=50 MB                   | --              |
| Per 1M tri (shaded)       | <=40 MB                   | --              |
| Per 1M tri (PBR textured) | <=100 MB                  | --              |
| Triangle throughput       | >=100M tri/s sustained    | --              |
| Texture upload            | >=1 GB/s                  | --              |
| Mesh streaming            | >=2 GB/s                  | --              |
| Max lights (clustered)    | 4096 (T1)                 | --              |
| Max viewports             | 8                         | --              |
| Max point cloud pts       | 10B+ streaming            | --              |
| Max CAE elements          | 100M+                     | --              |

## Appendix D: Quality Metrics

| Metric              | Architecture Response                                            |
| ------------------- | ---------------------------------------------------------------- |
| Edge aliasing       | TAA/FXAA/MSAA (§14.7) + SDF AA for edges (§10.1)                 |
| Wireframe precision | SDF coverage -- sub-pixel accurate                               |
| Depth precision     | Reverse-Z (§1.1) + D32F -- eliminates Z-fighting                 |
| Color accuracy      | Linear internal pipeline, sRGB output, correct gamma in tone-map |
| HDR range           | RGBA16F intermediate -- >=10 stops                               |
| Shadow acne         | Front-face cull + depth bias (§8)                                |
| Temporal stability  | TAA YCoCg neighborhood clamp + motion rejection (§14.7)          |
| Energy conservation | Kulla-Conty multi-scatter (§6.2) -- normalized BRDF              |
| Transparency        | LL-OIT exact sorted (§9.1)                                       |
| Text legibility     | MSDF + Phase 7b direct curve (§13.1)                             |

---

**Last updated**: 2026-03-16 (pass I/O specs added for §5 Geometry #1-#11, §6.3 Material BSDF pipeline, §7.3 Lighting+Shadow+AO #12-#19, §9.1 OIT #25-#27; data structure bugs fixed: GpuLight 64B, MaterialParameterBlock 192B, VisBuffer R32+R32)
