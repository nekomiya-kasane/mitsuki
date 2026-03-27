# miki RHI (Rendering Hardware Interface) Design

> **Status**: Design blueprint  
> **Scope**: Multi-backend RHI supporting Vulkan 1.4, Vulkan 1.1 (Compat), D3D12, WebGPU, OpenGL 4.3  
> **Audience**: Engine team — architecture reference  
> **Companion**: `specs/rhi-migration-plan.md` (implementation roadmap)

---

## 1. Design Goals & Constraints

### 1.1 Primary Goals

| #   | Goal                                       | Rationale                                                                                                                        |
| --- | ------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------- |
| G1  | **Zero-overhead abstraction on Tier1**     | Vulkan 1.4 / D3D12 must achieve native-level performance; no virtual dispatch in hot paths                                       |
| G2  | **Maximum capability extraction per tier** | Each backend must expose its highest-performance path, not LCD (lowest common denominator)                                       |
| G3  | **Compile-time backend selection**         | Backend chosen at build time (template/`if constexpr`), not runtime vtable; eliminates branch misprediction in command recording |
| G4  | **Thin abstraction**                       | RHI is a vocabulary layer, not a framework; no hidden resource tracking, no implicit barriers                                    |
| G5  | **RenderGraph-friendly**                   | All barrier/transition/aliasing decisions delegated to RenderGraph; RHI provides primitives, not policy                          |
| G6  | **Shader IR agnostic**                     | RHI accepts pre-compiled blobs (SPIR-V, DXIL, GLSL, WGSL); Slang compilation is external                                         |
| G7  | **Deterministic resource lifetime**        | Explicit create/destroy with deferred-destruction queue (2-frame latency); no ref-counting in hot path                           |

### 1.2 Tier Mapping

| Tier          | Backend       | API Version             | Shader IR | Key Differentiator                                                                |
| ------------- | ------------- | ----------------------- | --------- | --------------------------------------------------------------------------------- |
| **T1-Vulkan** | Vulkan        | 1.4 (Roadmap 2026)      | SPIR-V    | Descriptor heap, mesh shader, ray query, timeline semaphore, async compute        |
| **T1-D3D12**  | Direct3D 12   | Agility SDK 1.719+      | DXIL      | Root signature, descriptor heap, mesh shader, DXR, work graphs, enhanced barriers |
| **T2**        | Vulkan Compat | 1.1 + select extensions | SPIR-V    | Traditional descriptor sets, no mesh shader, MDI                                  |
| **T3**        | WebGPU        | Dawn/wgpu               | WGSL      | Bind groups, 128MB SSBO limit, no MDI, single queue                               |
| **T4**        | OpenGL        | 4.3+                    | GLSL 4.30 | Direct state access, MDI, UBO/SSBO binding points                                 |

### 1.3 Non-Goals

- **Runtime vtable polymorphism in hot paths**: Command recording uses CRTP (compile-time dispatch). No `IDevice` virtual calls per draw.
- **Automatic barrier insertion**: That is RenderGraph's job. RHI exposes `CmdPipelineBarrier()` / `CmdTransition()`.
- **Implicit resource state tracking**: Caller (RenderGraph) tracks states. RHI trusts the caller.
- **Shader compilation**: Slang → target IR is a separate pipeline. RHI accepts blobs.
- **Window/surface creation**: Platform layer creates surfaces; RHI attaches to them.

### 1.4 Runtime Backend Switching (Device Hot-Swap)

miki supports **runtime backend switching** via full device teardown and recreation. This does **not** conflict with CRTP compile-time dispatch — all target backends are compiled into the binary simultaneously (each in its own translation unit), and the existing `DeviceHandle` (type-erased facade) selects the active backend.

**Switching protocol**:

```
1. Application requests backend switch (e.g., user selects "D3D12" in settings)
2. RenderGraph::Shutdown()        — release all pass resources, transient pools
3. Device::WaitIdle()             — drain all GPU queues
4. Scene::ReleaseGpuResources()   — destroy all persistent GPU resources (meshes, textures, pipelines)
5. DeviceHandle::Destroy()        — tear down the current device and all internal state
6. DeviceHandle = CreateDevice(newBackendDesc)  — create device with new backend
7. Scene::RecreateGpuResources()  — re-upload meshes, textures; rebuild pipelines from cache
8. RenderGraph::Initialize()      — rebuild pass graph, allocate transient resources
```

**Performance characteristics**:

| Aspect                       | Impact                                                                                                                    |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| Hot path (command recording) | **Zero overhead** — still CRTP, no vtable                                                                                 |
| `DeviceHandle` dispatch      | O(passes/frame) ≈ 50-100 switch cases, same as before                                                                     |
| Switch latency               | **1-5 seconds** (acceptable: GPU idle + resource rebuild + pipeline compile)                                              |
| Pipeline rebuild             | Mitigated by pipeline cache — `PipelineCacheData` is backend-agnostic blob key; only the compiled PSO is backend-specific |
| Memory                       | All backends linked into binary; unused backend code is cold (not paged in)                                               |

**Industry precedent**: Filament (`Engine::destroy()` + `Engine::create(Backend)`), UE5 (process restart with RHI DLL swap), Godot 4 (restart with different driver). miki's approach is closest to Filament — in-process, no restart required.

**Multi-backend compilation**: CMake builds all enabled backends. Each backend's CRTP specialization lives in its own `.cpp` → no cross-contamination, no compile-time cost increase per TU.

```cmake
# CMake: all backends compiled, runtime-selectable
option(MIKI_BACKEND_VULKAN    "Build Vulkan 1.4 backend"    ON)
option(MIKI_BACKEND_D3D12     "Build D3D12 backend"         ON)
option(MIKI_BACKEND_VULKAN_COMPAT "Build Vulkan Compat backend" ON)
option(MIKI_BACKEND_WEBGPU    "Build WebGPU backend"        ON)
option(MIKI_BACKEND_OPENGL    "Build OpenGL 4.3 backend"    ON)
```

---

## 2. Architecture Overview

### 2.1 Layer Diagram

```
┌─────────────────────────────────────────────────────────┐
│                    Application / Scene                    │
├─────────────────────────────────────────────────────────┤
│                      RenderGraph                         │
│  (barrier insertion, transient aliasing, pass scheduling) │
├─────────────────────────────────────────────────────────┤
│                     RHI Public API                        │
│  Device · CommandBuffer · Pipeline · Resource · Sync      │
├──────────┬──────────┬──────────┬──────────┬─────────────┤
│ Vulkan   │  D3D12   │ Vk Compat│  WebGPU  │  OpenGL     │
│ 1.4      │  12      │  1.1     │  Dawn    │  4.3        │
│ (T1)     │  (T1)    │  (T2)    │  (T3)    │  (T4)       │
└──────────┴──────────┴──────────┴──────────┴─────────────┘
```

### 2.2 Compile-Time Polymorphism (CRTP)

RHI uses **CRTP (Curiously Recurring Template Pattern)** instead of virtual dispatch:

```cpp
template <typename Impl>
class DeviceBase {
public:
    auto CreateBuffer(const BufferDesc& desc) -> Result<BufferHandle> {
        return static_cast<Impl*>(this)->CreateBufferImpl(desc);
    }
    auto CreateTexture(const TextureDesc& desc) -> Result<TextureHandle> {
        return static_cast<Impl*>(this)->CreateTextureImpl(desc);
    }
    // ...
};

class VulkanDevice : public DeviceBase<VulkanDevice> { /* ... */ };
class D3D12Device  : public DeviceBase<D3D12Device>  { /* ... */ };
```

**Rationale**: Command recording is the hottest path (millions of calls/frame on Compat tiers). Virtual dispatch adds ~2-5ns per call due to indirect branch + cache miss. CRTP eliminates this entirely — the compiler inlines backend-specific code at each call site.

**Type-erased facades** (for RenderGraph and higher layers that need backend-agnostic code):

```cpp
// ── DeviceHandle ─────────────────────────────────────────────────────────
// Thin type-erased wrapper — used ONLY by RenderGraph, init code, and debug
// profilers (GpuProfiler, MemProfiler, etc.). Never in per-draw paths.
class DeviceHandle {
    void*       ptr_;    // Points to concrete DeviceBase<Impl>
    BackendType tag_;    // 5-way enum

public:
    [[nodiscard]] bool IsValid() const noexcept { return ptr_ != nullptr; }

    // Dispatches via switch (5 cases). O(passes/frame) ≈ 50-100 calls.
    template <typename F>
    [[nodiscard]] auto Dispatch(F&& fn) -> decltype(auto) {
        MIKI_ASSERT(ptr_ != nullptr, "DeviceHandle::Dispatch on null handle");
        switch (tag_) {
            case BackendType::Vulkan14:    return fn(*static_cast<VulkanDevice*>(ptr_));
            case BackendType::D3D12:       return fn(*static_cast<D3D12Device*>(ptr_));
            case BackendType::VulkanCompat: return fn(*static_cast<VulkanCompatDevice*>(ptr_));
            case BackendType::WebGPU:      return fn(*static_cast<WebGPUDevice*>(ptr_));
            case BackendType::OpenGL43:    return fn(*static_cast<OpenGLDevice*>(ptr_));
        }
        std::unreachable();  // C++23: all enum values handled above
    }
};

// ── CommandListHandle ────────────────────────────────────────────────────
// Type-erased command buffer handle. RenderGraph holds these; the pass
// callback uses Dispatch() ONCE to obtain the concrete CRTP type, then
// records all commands with zero overhead (CRTP inline).
//
// Key insight: the type-erasure cost is paid O(passes/frame) ≈ 50-100,
// NOT O(draws/frame) ≈ 10000+. This makes the overhead negligible (<1μs)
// while preserving a single RenderGraph type that works across all backends.
class CommandListHandle {
    void*       ptr_;    // Points to concrete CommandBufferBase<Impl>
    BackendType tag_;

public:
    [[nodiscard]] bool IsValid() const noexcept { return ptr_ != nullptr; }

    // Single dispatch at pass entry — all subsequent CmdDraw/CmdDispatch
    // calls inside the lambda are CRTP-inlined with zero overhead.
    template <typename F>
    [[nodiscard]] auto Dispatch(F&& fn) -> decltype(auto) {
        MIKI_ASSERT(ptr_ != nullptr, "CommandListHandle::Dispatch on null handle");
        switch (tag_) {
            case BackendType::Vulkan14:
                return fn(*static_cast<VulkanCommandBuffer*>(ptr_));
            case BackendType::D3D12:
                return fn(*static_cast<D3D12CommandBuffer*>(ptr_));
            case BackendType::VulkanCompat:
                return fn(*static_cast<VulkanCompatCommandBuffer*>(ptr_));
            case BackendType::WebGPU:
                return fn(*static_cast<WebGPUCommandBuffer*>(ptr_));
            case BackendType::OpenGL43:
                return fn(*static_cast<OpenGLCommandBuffer*>(ptr_));
        }
        std::unreachable();  // C++23: all enum values handled above
    }
};
```

**Performance analysis — why NOT Command Packets**:

An alternative design serializes commands into POD packets in a linear allocator, then replays
them through a backend-specific translator at submit time (used by bgfx, early Filament for
OpenGL compat). This approach is **rejected** for miki:

| Criterion          |                    Command Packet (rejected)                    |    CRTP + CommandListHandle (chosen)     |
| ------------------ | :-------------------------------------------------------------: | :--------------------------------------: |
| Record overhead    |                     ~5-8ns/cmd (memcpy POD)                     |      ~10-20ns/cmd (direct API call)      |
| Replay overhead    |               ~8-15ns/cmd (decode + native call)                |         **0** (already recorded)         |
| **Total per-cmd**  |                          **~13-23ns**                           |               **~10-20ns**               |
| Debuggability      |       Callstack is `Translator::Execute()` — context lost       | Native callstack — RenderDoc/PIX perfect |
| GPU validation     |  Deferred to replay; error location decoupled from record site  |       Immediate; precise location        |
| Secondary cmd bufs |        Cannot leverage Vulkan secondary command buffers         |               Full support               |
| Maintenance        | N commands × M backends + Packet struct + Serialize + Translate |    N commands × M backend impls only     |

The CRTP + dispatch-once facade gives **lower total overhead**, **superior debuggability**, and
**zero maintenance tax** compared to command packets. The ~500ns/frame dispatch cost
(100 passes × 5ns switch) is negligible vs the ~100μs/frame replay cost of command packets.

**RenderGraph pass registration example**:

```cpp
// RenderGraph stores CommandListHandle (type-erased, backend-agnostic)
graph.AddPass("GBuffer", [](CommandListHandle& cmd, const PassResources& res) {
    cmd.Dispatch([&](auto& cb) {            // ONE switch here — O(passes)
        cb.CmdBeginRendering(gbufferDesc);  // CRTP inline — zero overhead
        cb.CmdBindPipeline(gbufferPSO);     // CRTP inline
        cb.CmdBindDescriptorSet(0, perFrameSet);
        for (auto& draw : drawList) {
            cb.CmdPushConstants(ShaderStage::Vertex, 0, sizeof(draw.pc), &draw.pc);
            cb.CmdDrawIndexed(draw.indexCount, 1, draw.firstIndex, draw.vertexOffset, 0);
        }                                   // All CRTP inline — zero overhead
        cb.CmdEndRendering();
    });
});
```

### 2.3 Module Decomposition

| Module              | Responsibility                                          | Header                                                 |
| ------------------- | ------------------------------------------------------- | ------------------------------------------------------ |
| **Core Types**      | Enums, formats, handles, descriptors                    | `rhi/RhiTypes.h`, `rhi/RhiEnums.h`, `rhi/RhiFormats.h` |
| **Device**          | GPU device creation, capability query, resource factory | `rhi/Device.h`                                         |
| **CommandBuffer**   | Command recording (graphics, compute, transfer)         | `rhi/CommandBuffer.h`                                  |
| **Pipeline**        | PSO creation (graphics, compute, ray tracing, mesh)     | `rhi/Pipeline.h`                                       |
| **Resources**       | Buffer, Texture, Sampler, AccelerationStructure         | `rhi/Resources.h`                                      |
| **Descriptors**     | Descriptor layout, binding, bindless table              | `rhi/Descriptors.h`                                    |
| **Synchronization** | Fence, Semaphore (timeline + binary), Barrier           | `rhi/Sync.h`                                           |
| **Swapchain**       | Surface attachment, present                             | `rhi/Swapchain.h`                                      |
| **Query**           | Timestamp, occlusion, pipeline statistics               | `rhi/Query.h`                                          |
| **Shader**          | Shader module creation from pre-compiled blobs          | `rhi/Shader.h`                                         |

---

## 3. Handle System

### 3.1 Typed Opaque Handles

All GPU resources are represented as **typed 64-bit handles** — no raw pointers cross the API boundary:

```cpp
// 64-bit handle: [generation:16 | index:32 | type:8 | backend:8]
template <typename Tag>
struct Handle {
    uint64_t value = 0;
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    constexpr auto operator<=>(const Handle&) const = default;
};

using BufferHandle       = Handle<struct BufferTag>;
using TextureHandle      = Handle<struct TextureTag>;
using SamplerHandle      = Handle<struct SamplerTag>;
using PipelineHandle     = Handle<struct PipelineTag>;
using PipelineLayoutHandle = Handle<struct PipelineLayoutTag>;
using ShaderModuleHandle = Handle<struct ShaderModuleTag>;
using FenceHandle        = Handle<struct FenceTag>;
using SemaphoreHandle    = Handle<struct SemaphoreTag>;
using QueryPoolHandle    = Handle<struct QueryPoolTag>;
using AccelStructHandle  = Handle<struct AccelStructTag>;
using SwapchainHandle         = Handle<struct SwapchainTag>;
using TextureViewHandle       = Handle<struct TextureViewTag>;
using DeviceMemoryHandle      = Handle<struct DeviceMemoryTag>; // Shared heap for aliasing / sparse
using DescriptorLayoutHandle  = Handle<struct DescriptorLayoutTag>;
using DescriptorSetHandle     = Handle<struct DescriptorSetTag>;
```

### 3.2 Generation Counter

16-bit generation prevents use-after-free:

```
Create buffer → Handle{gen=1, idx=42, type=Buffer, backend=Vulkan}
Destroy buffer → slot 42 freed, gen incremented to 2
Stale handle {gen=1, idx=42} → lookup fails (gen mismatch) → debug assert
```

### 3.3 Handle Pool

Each backend maintains a `HandlePool<T, Capacity>` — a slot array with free-list:

- **O(1)** create (pop free list)
- **O(1)** destroy (push free list, increment generation)
- **O(1)** lookup (index + generation check)
- No heap allocation after init (pre-allocated fixed-size pool)

---

## 4. Device & Capability System

### 4.1 Device Creation

```cpp
struct DeviceDesc {
    BackendType          backend;          // Vulkan14, D3D12, VulkanCompat, WebGPU, OpenGL43
    uint32_t             adapterIndex;     // GPU selection (0 = default)
    bool                 enableValidation; // Debug layers
    bool                 enableGpuCapture; // RenderDoc / PIX integration
    std::span<const char*> requiredExtensions; // Backend-specific
};

// Returns concrete device type (no virtual)
auto CreateDevice(const DeviceDesc&) -> Result<VulkanDevice>;   // or D3D12Device, etc.
```

### 4.2 Capability Profile

Queried once at device creation, immutable thereafter:

```cpp
struct GpuCapabilityProfile {
    CapabilityTier       tier;                    // T1_Vulkan, T1_D3D12, T2, T3, T4

    // Geometry
    bool                 hasMeshShader;
    bool                 hasTaskShader;
    bool                 hasMultiDrawIndirect;
    bool                 hasMultiDrawIndirectCount;
    uint32_t             maxDrawIndirectCount;

    // Descriptors
    DescriptorModel      descriptorModel;         // DescriptorHeap, DescriptorBuffer, DescriptorSet, BindGroup, DirectBind
    bool                 hasBindless;
    bool                 hasPushDescriptors;
    uint32_t             maxPushConstantSize;      // 128-256 bytes
    uint32_t             maxBoundDescriptorSets;
    uint64_t             maxStorageBufferSize;     // 128MB (WebGPU) to unlimited

    // Compute
    bool                 hasAsyncCompute;
    bool                 hasTimelineSemaphore;
    uint32_t             maxComputeWorkGroupCount[3];
    uint32_t             maxComputeWorkGroupSize[3];

    // Ray Tracing
    bool                 hasRayQuery;
    bool                 hasRayTracingPipeline;
    bool                 hasAccelerationStructure;

    // Shading
    bool                 hasVariableRateShading;
    bool                 hasFloat64;
    bool                 hasInt64Atomics;
    bool                 hasSubgroupOps;
    uint32_t             subgroupSize;             // 32 (NVIDIA/Intel) or 64 (AMD)

    // Memory
    uint64_t             deviceLocalMemoryBytes;
    uint64_t             hostVisibleMemoryBytes;
    bool                 hasResizableBAR;
    bool                 hasSparseBinding;
    bool                 hasHardwareDecompression;  // GDeflate HW decode (VK_NV_memory_decompression / DirectStorage)
    bool                 hasMemoryBudgetQuery;
    bool                 hasWorkGraphs;             // D3D12 SM 6.8 DispatchGraph (Phase 20)
    bool                 hasCooperativeMatrix;      // VK_KHR_cooperative_matrix / SM 6.9 wave matrix

    // Limits
    uint32_t             maxColorAttachments;       // 4-8
    uint32_t             maxTextureSize2D;           // 4096-16384
    uint32_t             maxTextureSizeCube;
    uint32_t             maxFramebufferWidth;
    uint32_t             maxFramebufferHeight;
    uint32_t             maxViewports;
    uint32_t             maxClipDistances;

    // Format support (queried per-format)
    auto IsFormatSupported(Format, FormatFeatureFlags) const -> bool;
};
```

### 4.3 Tier Detection Logic

```
Vulkan 1.4 + mesh_shader + ray_query + descriptor_buffer + timeline_semaphore → T1_Vulkan
D3D12 + SM 6.5+ + mesh_shader + DXR 1.1                                           → T1_D3D12
Vulkan 1.1 + ¬mesh_shader                                                          → T2
WebGPU (Dawn/wgpu)                                                                  → T3
OpenGL 4.3+                                                                         → T4
```

---

## 5. Resource System

### 5.1 Buffer

```cpp
enum class BufferUsage : uint32_t {
    Vertex          = 1 << 0,
    Index           = 1 << 1,
    Uniform         = 1 << 2,
    Storage         = 1 << 3,
    Indirect        = 1 << 4,
    TransferSrc     = 1 << 5,
    TransferDst     = 1 << 6,
    AccelStructInput = 1 << 7,
    AccelStructStorage = 1 << 8,
    ShaderDeviceAddress = 1 << 9,  // BDA (Vulkan/D3D12)
    SparseBinding    = 1 << 10, // Sparse residency (T1 only): page commit/evict without realloc
};

enum class MemoryLocation : uint8_t {
    GpuOnly,        // DEVICE_LOCAL
    CpuToGpu,       // HOST_VISIBLE | HOST_COHERENT (staging, uniform ring)
    GpuToCpu,       // HOST_VISIBLE | HOST_CACHED (readback)
    Auto,           // Backend decides (ReBAR if available for small buffers)
};

struct BufferDesc {
    uint64_t         size;
    BufferUsage      usage;
    MemoryLocation   memory;
    bool             transient = false;  // RenderGraph transient: eligible for memory aliasing
    const char*      debugName = nullptr;
};

// Device API
auto CreateBuffer(const BufferDesc&) -> Result<BufferHandle>;
void DestroyBuffer(BufferHandle);
auto MapBuffer(BufferHandle) -> Result<void*>;
void UnmapBuffer(BufferHandle);
auto GetBufferDeviceAddress(BufferHandle) -> uint64_t;  // BDA, T1 only
```

### 5.2 Texture

```cpp
enum class TextureUsage : uint32_t {
    Sampled         = 1 << 0,
    Storage         = 1 << 1,
    ColorAttachment = 1 << 2,
    DepthStencil    = 1 << 3,
    TransferSrc     = 1 << 4,
    TransferDst     = 1 << 5,
    InputAttachment = 1 << 6,
    ShadingRate     = 1 << 7,
    SparseBinding   = 1 << 8,  // Sparse residency (T1 only)
};

enum class TextureDimension : uint8_t {
    Tex1D, Tex2D, Tex3D, TexCube, Tex2DArray, TexCubeArray,
};

struct TextureDesc {
    TextureDimension dimension;
    Format           format;
    uint32_t         width, height, depth;
    uint32_t         mipLevels;
    uint32_t         arrayLayers;
    uint32_t         sampleCount;        // MSAA: 1, 2, 4, 8
    TextureUsage     usage;
    MemoryLocation   memory;             // GpuOnly for render targets
    bool             transient = false;  // RenderGraph transient: eligible for memory aliasing
    const char*      debugName = nullptr;
};

struct TextureViewDesc {
    TextureHandle    texture;
    TextureDimension viewDimension;
    Format           format;             // Reinterpret (e.g., SRGB view of UNORM)
    uint32_t         baseMipLevel, mipLevelCount;
    uint32_t         baseArrayLayer, arrayLayerCount;
    TextureAspect    aspect;             // Color, Depth, Stencil
};

auto CreateTexture(const TextureDesc&) -> Result<TextureHandle>;
auto CreateTextureView(const TextureViewDesc&) -> Result<TextureViewHandle>;
void DestroyTexture(TextureHandle);
```

### 5.3 Sampler

```cpp
struct SamplerDesc {
    Filter           magFilter, minFilter, mipFilter;
    AddressMode      addressU, addressV, addressW;
    float            mipLodBias;
    float            maxAnisotropy;      // 0 = disabled, 1-16
    CompareOp        compareOp;          // None = no compare
    float            minLod, maxLod;
    BorderColor      borderColor;
};

auto CreateSampler(const SamplerDesc&) -> Result<SamplerHandle>;
```

### 5.4 Deferred Destruction Queue

Resources are not destroyed immediately — they enter a per-frame destruction queue:

```
Frame N: user calls DestroyBuffer(h)
  → h pushed to destructionQueue_[frameIndex % kMaxFramesInFlight]
Frame N+2: fence for frame N signaled
  → destructionQueue_[frameIndex] drained, actual API objects freed
```

This eliminates use-after-free when GPU is still referencing the resource. No ref-counting overhead.

### 5.5 Sparse Binding (T1 Only)

Sparse binding enables page-granular memory commitment without reallocating the backing resource. Required by ClusterDAG streaming (§5.6 of rendering-pipeline-architecture) and VSM physical page management (§8).

```cpp
struct SparsePageSize {
    uint64_t bufferPageSize;         // Typical: 64KB
    uint64_t imagePageSize;          // Typical: 64KB (standard) or 2MB (large page)
};

auto GetSparsePageSize() const -> SparsePageSize;

struct SparseBufferBind {
    BufferHandle     buffer;
    uint64_t         resourceOffset;  // Byte offset (must be page-aligned)
    uint64_t         size;            // Byte count (must be page-aligned)
    DeviceMemoryHandle memory;        // Null = unbind (evict page)
    uint64_t         memoryOffset;
};

struct SparseTextureBind {
    TextureHandle    texture;
    TextureSubresourceRange subresource;
    Offset3D         offset;          // Texel offset (page-aligned)
    Extent3D         extent;          // Texel extent (page-aligned)
    DeviceMemoryHandle memory;        // Null = unbind
    uint64_t         memoryOffset;
};

// Bind data only — no synchronization semantics embedded.
// Synchronization is handled at the Submit call site (see below).
struct SparseBindDesc {
    std::span<const SparseBufferBind>  bufferBinds;
    std::span<const SparseTextureBind> textureBinds;
};

// Synchronization passed separately — Vulkan needs explicit semaphores for
// vkQueueBindSparse; D3D12 ignores them (UpdateTileMappings is queue-serialized).
// This avoids leaking Vulkan's queue-level semaphore semantics into the RHI API,
// keeping D3D12 backend thin (no fake ID3D12Fence management).
void SubmitSparseBinds(QueueType                              queue,
                       const SparseBindDesc&                  binds,
                       std::span<const SemaphoreSubmitInfo>   wait   = {},
                       std::span<const SemaphoreSubmitInfo>   signal = {});
```

**Backend mapping**:

| RHI                    | Vulkan 1.4                                                                      | D3D12                                                                                              |
| ---------------------- | ------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| `SubmitSparseBinds`    | `vkQueueBindSparse` (on sparse-capable queue, wait/signal → `VkBindSparseInfo`) | `UpdateTileMappings` on reserved resource (wait/signal **ignored** — queue-internal serialization) |
| `SparseBufferBind`     | `VkSparseBufferMemoryBindInfo` + `VkSparseMemoryBind`                           | `D3D12_TILED_RESOURCE_COORDINATE` + `D3D12_TILE_RANGE_FLAGS`                                       |
| `SparseTextureBind`    | `VkSparseImageMemoryBindInfo` + `VkSparseImageMemoryBind`                       | `UpdateTileMappings` with subresource tiling                                                       |
| wait/signal semaphores | Passed to `VkBindSparseInfo::pWait/pSignalSemaphoreInfos`                       | D3D12: `Signal(fence, value)` emulation only if `signal` is non-empty; otherwise no-op             |

T2/T3/T4: sparse binding unavailable. Streaming on these tiers uses full-resource reallocation or CPU-side paging.

### 5.6 Transient & Memory Aliasing API

RenderGraph-owned transient resources use `transient = true` in `BufferDesc` / `TextureDesc`. The RHI provides memory aliasing primitives; aliasing policy is determined by RenderGraph.

```cpp
// Allocate a shared memory heap for aliasing
struct MemoryHeapDesc {
    uint64_t         size;
    MemoryLocation   memory;          // Typically GpuOnly
    const char*      debugName = nullptr;
};

auto CreateMemoryHeap(const MemoryHeapDesc&) -> Result<DeviceMemoryHandle>;
void DestroyMemoryHeap(DeviceMemoryHandle);

// Bind a transient resource to an offset within a shared heap
void AliasBufferMemory(BufferHandle, DeviceMemoryHandle heap, uint64_t offset);
void AliasTextureMemory(TextureHandle, DeviceMemoryHandle heap, uint64_t offset);

// Query alignment/size requirements for aliasing
struct MemoryRequirements {
    uint64_t size;
    uint64_t alignment;
    uint32_t memoryTypeBits;   // Backend-specific type mask
};

auto GetBufferMemoryRequirements(BufferHandle) -> MemoryRequirements;
auto GetTextureMemoryRequirements(TextureHandle) -> MemoryRequirements;
```

**Backend mapping**:

| RHI                  | Vulkan 1.4                                       | D3D12                                         | WebGPU / OpenGL               |
| -------------------- | ------------------------------------------------ | --------------------------------------------- | ----------------------------- |
| `CreateMemoryHeap`   | `vkAllocateMemory`                               | `CreateHeap`                                  | N/A (no aliasing)             |
| `AliasBufferMemory`  | `vkBindBufferMemory2` to shared `VkDeviceMemory` | `CreatePlacedResource` in shared `ID3D12Heap` | Fallback: separate allocation |
| `AliasTextureMemory` | `vkBindImageMemory2` to shared `VkDeviceMemory`  | `CreatePlacedResource` in shared `ID3D12Heap` | Fallback: separate allocation |

---

## 6. Descriptor & Binding System

This is the most complex part of the RHI due to fundamental divergence across backends.

### 6.1 Unified Binding Model

The RHI exposes a **3-level binding hierarchy** that maps naturally to all backends:

```
Level 0: Push Constants / Root Constants     (per-draw, 128-256 bytes)
Level 1: Per-Frame Bindings                  (camera UBO, global SSBOs)
Level 2: Per-Material / Per-Pass Bindings    (textures, material UBOs)
Level 3: Bindless Table                      (all textures/buffers by index)
```

### 6.2 Descriptor Layout

```cpp
enum class BindingType : uint8_t {
    UniformBuffer,
    StorageBuffer,
    SampledTexture,
    StorageTexture,
    Sampler,
    CombinedTextureSampler,  // OpenGL/Vulkan convenience
    AccelerationStructure,
    // Bindless variants (T1 only)
    BindlessTextures,
    BindlessBuffers,
};

struct BindingDesc {
    uint32_t     binding;
    BindingType  type;
    ShaderStage  stages;
    uint32_t     count;          // Array size, 0 = runtime-sized (bindless)
};

struct DescriptorLayoutDesc {
    std::span<const BindingDesc> bindings;
    bool                         pushDescriptor;  // Vulkan push descriptor optimization
};

auto CreateDescriptorLayout(const DescriptorLayoutDesc&) -> Result<DescriptorLayoutHandle>;
```

### 6.3 Pipeline Layout

```cpp
struct PipelineLayoutDesc {
    std::span<const DescriptorLayoutHandle> setLayouts;  // max 4 sets
    struct PushConstantRange {
        ShaderStage stages;
        uint32_t    offset;
        uint32_t    size;
    };
    std::span<const PushConstantRange> pushConstants;    // max 256B total
};

auto CreatePipelineLayout(const PipelineLayoutDesc&) -> Result<PipelineLayoutHandle>;
```

### 6.4 Descriptor Set / Bind Group

```cpp
struct BufferBinding {
    BufferHandle buffer;
    uint64_t     offset = 0;
    uint64_t     range  = 0;  // 0 = whole buffer
};

struct TextureBinding {
    TextureViewHandle view;
    SamplerHandle     sampler;  // Used only for CombinedTextureSampler
};

using DescriptorResource = std::variant<
    BufferBinding,        // UniformBuffer, StorageBuffer
    TextureBinding,       // SampledTexture, StorageTexture, CombinedTextureSampler
    SamplerHandle,        // Sampler (standalone)
    AccelStructHandle     // AccelerationStructure
>;

struct DescriptorWrite {
    uint32_t           binding;
    uint32_t           arrayElement = 0;
    DescriptorResource resource;
};

struct DescriptorSetDesc {
    DescriptorLayoutHandle layout;
    std::span<const DescriptorWrite> writes;
};

auto CreateDescriptorSet(const DescriptorSetDesc&) -> Result<DescriptorSetHandle>;
void UpdateDescriptorSet(DescriptorSetHandle, std::span<const DescriptorWrite>);
```

### 6.5 Backend Mapping

| RHI Concept      | Vulkan 1.4 (T1)                                         | D3D12 (T1)                           | Vulkan Compat (T2)           | WebGPU (T3)               | OpenGL (T4)                                      |
| ---------------- | ------------------------------------------------------- | ------------------------------------ | ---------------------------- | ------------------------- | ------------------------------------------------ |
| DescriptorLayout | `VkDescriptorSetLayout` (or descriptor heap layout)     | Root signature                       | `VkDescriptorSetLayout`      | `GPUBindGroupLayout`      | N/A (implicit)                                   |
| DescriptorSet    | Descriptor heap offset                                  | Descriptor table in CBV/SRV/UAV heap | `VkDescriptorSet`            | `GPUBindGroup`            | Direct `glBindBufferRange` / `glBindTextureUnit` |
| PipelineLayout   | `VkPipelineLayout` (or descriptor heap pipeline layout) | Root signature                       | `VkPipelineLayout`           | `GPUPipelineLayout`       | N/A                                              |
| Push constants   | `vkCmdPushConstants` (256B)                             | Root constants (64 DWORDs)           | `vkCmdPushConstants`         | Emulated via 256B UBO     | Emulated via 128B UBO                            |
| Bindless         | Descriptor heap indexing                                | CBV/SRV/UAV heap indexing            | Descriptor indexing (Vk 1.2) | N/A (per-draw bind group) | `GL_ARB_bindless_texture` or array               |
| Update frequency | Offset into heap (zero-cost)                            | Offset into heap (zero-cost)         | `vkUpdateDescriptorSets`     | New `GPUBindGroup`        | `glBindBufferRange`                              |

### 6.5.1 Reserved Binding Convention (Push Constant Emulation)

On backends without native push constants (WebGPU T3, OpenGL T4), the RHI emulates push
constants via a small uniform buffer. This introduces an **implicit binding slot reservation**
that must be documented to prevent collision with user-declared descriptors:

| Backend        | Reserved Slot           | Size | Mechanism                                                   |
| -------------- | :---------------------- | :--- | :---------------------------------------------------------- |
| WebGPU         | `@group(0) @binding(0)` | 256B | `var<uniform>` in generated WGSL                            |
| OpenGL         | UBO binding point 0     | 128B | `layout(std140, binding = 0) uniform PushConstants { ... }` |
| Vulkan / D3D12 | None                    | N/A  | Native push constants / root constants                      |

**Slang integration contract**:

- Slang's `[vk::push_constant]` attribute, when targeting WGSL output, MUST map to
  `@group(0) @binding(0)`. The Slang compiler backend must be configured with this convention.
- User-declared descriptors in `set=0` start from `binding=1` on WebGPU/OpenGL.
  On Vulkan/D3D12, `binding=0` in `set=0` is available for user descriptors (no reservation).
- The `PipelineLayoutDesc` creation path on WebGPU/OpenGL backends automatically injects the
  push constant UBO into the generated bind group layout at `group(0), binding(0)`.
  User-provided `DescriptorLayoutDesc` for set 0 is shifted by +1 binding offset internally.

**Validation**: In debug builds, `CreatePipelineLayout` asserts that no user binding in set 0
occupies binding 0 on WebGPU/OpenGL backends, emitting a clear error message.

### 6.6 Bindless Table

The `BindlessTable` provides a unified resource indexing layer:

```cpp
class BindlessTable {
public:
    auto RegisterTexture(TextureViewHandle) -> uint32_t;   // Returns stable index
    auto RegisterBuffer(BufferHandle, uint64_t offset, uint64_t range) -> uint32_t;
    void Unregister(uint32_t index);

    // Bind the table to a command buffer (once per frame)
    void Bind(CommandBuffer&, uint32_t set);

    // Backend-specific: returns descriptor heap / descriptor set / bind group
    auto GetNativeBinding() const -> NativeBindingInfo;
};
```

| Backend       | Implementation                                                                          |
| ------------- | --------------------------------------------------------------------------------------- |
| Vulkan 1.4    | `VK_EXT_descriptor_buffer` (current); future: `VK_EXT_descriptor_heap` (proposed 2026+) |
| D3D12         | CBV/SRV/UAV descriptor heap (shader-visible)                                            |
| Vulkan Compat | Large descriptor set with `VK_EXT_descriptor_indexing` (update-after-bind)              |
| WebGPU        | Per-draw bind group with sampled textures (no true bindless)                            |
| OpenGL        | `GL_ARB_bindless_texture` if available; else texture array + index                      |

---

## 7. Command Buffer System

### 7.1 Command Buffer Types

```cpp
enum class QueueType : uint8_t {
    Graphics,       // Graphics + compute + transfer
    Compute,        // Compute + transfer (async compute)
    Transfer,       // Transfer only (DMA / copy engine)
};

struct CommandBufferDesc {
    QueueType type;
    bool      secondary = false;  // Secondary command buffer (for multi-threaded recording)
};

auto CreateCommandBuffer(const CommandBufferDesc&) -> Result<CommandBufferHandle>;
```

### 7.2 Command Recording API

All commands are recorded into a `CommandBuffer`. The API is designed for **zero-allocation recording** — no heap allocations during command recording.

```cpp
// --- State Binding ---
void CmdBindPipeline(PipelineHandle);
void CmdBindDescriptorSet(uint32_t set, DescriptorSetHandle, std::span<const uint32_t> dynamicOffsets = {});
void CmdPushConstants(ShaderStage stages, uint32_t offset, uint32_t size, const void* data);
void CmdBindVertexBuffer(uint32_t binding, BufferHandle, uint64_t offset);
void CmdBindIndexBuffer(BufferHandle, uint64_t offset, IndexType);

// --- Draw ---
void CmdDraw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
void CmdDrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
void CmdDrawIndirect(BufferHandle, uint64_t offset, uint32_t drawCount, uint32_t stride);
void CmdDrawIndexedIndirect(BufferHandle, uint64_t offset, uint32_t drawCount, uint32_t stride);
void CmdDrawIndexedIndirectCount(BufferHandle argsBuffer, uint64_t argsOffset, BufferHandle countBuffer, uint64_t countOffset, uint32_t maxDrawCount, uint32_t stride);
void CmdDrawMeshTasks(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
void CmdDrawMeshTasksIndirect(BufferHandle, uint64_t offset, uint32_t drawCount, uint32_t stride);
void CmdDrawMeshTasksIndirectCount(BufferHandle argsBuffer, uint64_t argsOffset, BufferHandle countBuffer, uint64_t countOffset, uint32_t maxDrawCount, uint32_t stride);

// --- Compute ---
void CmdDispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
void CmdDispatchIndirect(BufferHandle, uint64_t offset);

// --- Transfer ---
void CmdCopyBuffer(BufferHandle src, uint64_t srcOffset, BufferHandle dst, uint64_t dstOffset, uint64_t size);
void CmdCopyBufferToTexture(BufferHandle src, TextureHandle dst, const BufferTextureCopyRegion&);
void CmdCopyTextureToBuffer(TextureHandle src, BufferHandle dst, const BufferTextureCopyRegion&);
void CmdCopyTexture(TextureHandle src, TextureHandle dst, const TextureCopyRegion&);
void CmdBlitTexture(TextureHandle src, TextureHandle dst, const TextureBlitRegion&, Filter);
void CmdFillBuffer(BufferHandle, uint64_t offset, uint64_t size, uint32_t value);
void CmdClearColorTexture(TextureHandle, const ClearColor&, const TextureSubresourceRange&);
void CmdClearDepthStencil(TextureHandle, float depth, uint8_t stencil, const TextureSubresourceRange&);

// --- Synchronization (explicit, RenderGraph-driven) ---
void CmdPipelineBarrier(const PipelineBarrierDesc&);
void CmdBufferBarrier(BufferHandle, const BufferBarrierDesc&);
void CmdTextureBarrier(TextureHandle, const TextureBarrierDesc&);

// --- Dynamic Rendering (no render pass objects) ---
struct RenderingAttachment {
    TextureViewHandle  view;
    AttachmentLoadOp   loadOp;
    AttachmentStoreOp  storeOp;
    ClearValue         clearValue;
    TextureViewHandle  resolveView;  // MSAA resolve target
};

struct RenderingDesc {
    Rect2D                              renderArea;
    std::span<const RenderingAttachment> colorAttachments;
    const RenderingAttachment*          depthAttachment;
    const RenderingAttachment*          stencilAttachment;
    uint32_t                            viewMask;  // Multiview (XR)
};

void CmdBeginRendering(const RenderingDesc&);
void CmdEndRendering();

// --- Dynamic State ---
void CmdSetViewport(const Viewport&);
void CmdSetScissor(const Rect2D&);
void CmdSetDepthBias(float constantFactor, float clamp, float slopeFactor);
void CmdSetStencilReference(uint32_t ref);
void CmdSetBlendConstants(const float constants[4]);
void CmdSetDepthBounds(float minDepth, float maxDepth);  // Depth bounds test (T1/T2/T4)
void CmdSetLineWidth(float);  // T4 only

// --- Variable Rate Shading (T1 only) ---
enum class ShadingRate : uint8_t {
    Rate1x1 = 0,   // Full rate
    Rate1x2 = 1,
    Rate2x1 = 2,
    Rate2x2 = 3,   // Quarter rate
    Rate2x4 = 4,
    Rate4x2 = 5,
    Rate4x4 = 6,   // 1/16 rate
};

enum class ShadingRateCombinerOp : uint8_t {
    Keep,           // Use pipeline rate
    Replace,        // Use primitive/attachment rate
    Min,
    Max,
    Mul,            // Multiply rates (product)
};

// Set per-draw pipeline shading rate + combiner ops for [primitive, attachment] stages
void CmdSetShadingRate(ShadingRate baseRate, const ShadingRateCombinerOp combinerOps[2]);

// Bind a shading rate image (R8_UINT, per 16x16 tile) for attachment-based VRS
void CmdSetShadingRateImage(TextureViewHandle rateImage);  // Null = disable attachment VRS

// --- Secondary Command Buffer Execution ---
// Record commands in secondary buffers on worker threads, execute from primary.
// Vulkan: vkCmdExecuteCommands. D3D12: ExecuteBundle. WebGPU: executeBundles.
// OpenGL: no-op (secondary commands replayed inline by backend).
void CmdExecuteSecondary(std::span<const CommandBufferHandle> secondaryBuffers);

// --- Query ---
void CmdBeginQuery(QueryPoolHandle, uint32_t index);
void CmdEndQuery(QueryPoolHandle, uint32_t index);
void CmdWriteTimestamp(PipelineStage, QueryPoolHandle, uint32_t index);
void CmdResetQueryPool(QueryPoolHandle, uint32_t first, uint32_t count);

// --- Debug ---
void CmdBeginDebugLabel(const char* name, const float color[4]);
void CmdEndDebugLabel();
void CmdInsertDebugLabel(const char* name, const float color[4]);

// --- Acceleration Structure (T1 only, see §10 for types) ---
// CmdBuildBLAS, CmdBuildTLAS, CmdUpdateBLAS are defined in §10.
```

### 7.3 Backend Mapping for Dynamic Rendering

| RHI                 | Vulkan 1.4                                          | D3D12                                                        | Vulkan Compat                                                      | WebGPU                         | OpenGL                          |
| ------------------- | --------------------------------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------------ | ------------------------------ | ------------------------------- |
| `CmdBeginRendering` | `vkCmdBeginRendering` (dynamic rendering, core 1.3) | `OMSetRenderTargets` + `BeginRenderPass` (enhanced barriers) | `vkCmdBeginRendering` (if ext available) or `vkCmdBeginRenderPass` | `beginRenderPass()` on encoder | `glBindFramebuffer` + `glClear` |
| `CmdEndRendering`   | `vkCmdEndRendering`                                 | `EndRenderPass`                                              | `vkCmdEndRendering` / `vkCmdEndRenderPass`                         | `end()` on encoder             | (implicit)                      |

### 7.4 Command Buffer Submission

```cpp
// Semaphore synchronization info for GPU-GPU submit (binary or timeline)
struct SemaphoreSubmitInfo {
    SemaphoreHandle semaphore;
    uint64_t        value;          // Timeline value (ignored for Binary semaphores)
    PipelineStage   stageMask;      // Stage to wait/signal at
};

struct SubmitDesc {
    std::span<const CommandBufferHandle> commandBuffers;
    std::span<const SemaphoreSubmitInfo> waitSemaphores;
    std::span<const SemaphoreSubmitInfo> signalSemaphores;
    FenceHandle                          signalFence;
};

void Submit(QueueType, const SubmitDesc&);
void Present(SwapchainHandle, SemaphoreHandle renderFinished);
```

**Queue management**: The device internally manages queue families and indices. `Submit(QueueType, ...)` routes to the appropriate queue. The device guarantees:

- **Graphics queue**: always available (all tiers)
- **Async compute queue**: separate queue when `GpuCapabilityProfile::hasAsyncCompute` is true (T1). Falls back to graphics queue otherwise.
- **Transfer queue**: dedicated transfer queue when available (T1 Vulkan 1.4 dedicated transfer). Falls back to graphics queue otherwise.

Queue creation and family selection are internal to the device — no explicit `GetQueue()` API. This keeps the public interface thin while the backend selects optimal queue families at `CreateDevice` time. Timeline semaphores (`SemaphoreSubmitInfo::value`) synchronize work across different queue types.

---

## 8. Pipeline System

### 8.1 Graphics Pipeline

```cpp
struct VertexInputBinding {
    uint32_t    binding;
    uint32_t    stride;
    VertexInputRate inputRate;  // PerVertex, PerInstance
};

struct VertexInputAttribute {
    uint32_t    location;
    uint32_t    binding;
    Format      format;
    uint32_t    offset;
};

struct VertexInputState {
    std::span<const VertexInputBinding>   bindings;
    std::span<const VertexInputAttribute> attributes;
};

struct GraphicsPipelineDesc {
    // Shaders (set what's needed; mesh shader path ignores vertex input)
    ShaderModuleHandle   vertexShader;
    ShaderModuleHandle   fragmentShader;
    ShaderModuleHandle   taskShader;       // T1 mesh shader path
    ShaderModuleHandle   meshShader;       // T1 mesh shader path

    // Vertex input (ignored when meshShader is set)
    VertexInputState     vertexInput;

    // Fixed-function state
    PrimitiveTopology    topology;
    bool                 primitiveRestart;
    PolygonMode          polygonMode;
    CullMode             cullMode;
    FrontFace            frontFace;
    float                lineWidth;

    // Depth/stencil
    bool                 depthTestEnable;
    bool                 depthWriteEnable;
    CompareOp            depthCompareOp;
    bool                 stencilTestEnable;
    StencilOpState       stencilFront, stencilBack;
    bool                 depthBiasEnable;
    bool                 depthClampEnable;

    // Blend (per color attachment)
    struct ColorAttachmentBlend {
        bool             blendEnable;
        BlendFactor      srcColor, dstColor;
        BlendOp          colorOp;
        BlendFactor      srcAlpha, dstAlpha;
        BlendOp          alphaOp;
        ColorWriteMask   writeMask;
    };
    std::span<const ColorAttachmentBlend> colorBlends;

    // Render target formats (dynamic rendering — no render pass object)
    std::span<const Format> colorFormats;
    Format               depthFormat;
    Format               stencilFormat;
    uint32_t             sampleCount;
    uint32_t             viewMask;         // Multiview (XR)

    // Layout
    PipelineLayoutHandle pipelineLayout;

    // Specialization constants (Vulkan/Slang)
    std::span<const SpecializationConstant> specializationConstants;

    constexpr bool IsMeshShaderPipeline() const noexcept {
        return meshShader.IsValid();
    }
};

auto CreateGraphicsPipeline(const GraphicsPipelineDesc&) -> Result<PipelineHandle>;
```

### 8.2 Compute Pipeline

```cpp
struct ComputePipelineDesc {
    ShaderModuleHandle   computeShader;
    PipelineLayoutHandle pipelineLayout;
    std::span<const SpecializationConstant> specializationConstants;
};

auto CreateComputePipeline(const ComputePipelineDesc&) -> Result<PipelineHandle>;
```

### 8.3 Ray Tracing Pipeline (T1 only)

```cpp
struct RayTracingPipelineDesc {
    std::span<const ShaderModuleHandle> shaderStages;
    std::span<const RayTracingShaderGroup> shaderGroups;
    uint32_t             maxRecursionDepth;
    PipelineLayoutHandle pipelineLayout;
};

auto CreateRayTracingPipeline(const RayTracingPipelineDesc&) -> Result<PipelineHandle>;
```

### 8.4 Pipeline Cache

```cpp
auto CreatePipelineCache(std::span<const uint8_t> initialData = {}) -> Result<PipelineCacheHandle>;
auto GetPipelineCacheData(PipelineCacheHandle) -> std::vector<uint8_t>;
```

All pipeline creation functions accept an optional `PipelineCacheHandle` for disk-persistent caching.

### 8.5 Pipeline Library (Split Compilation)

`VK_EXT_graphics_pipeline_library` (Vulkan) and D3D12 Pipeline State Streams allow **split compilation**: vertex input, pre-rasterization, fragment output, and fragment shader stages compile independently, then link at draw time. This reduces PSO creation stalls by 10-100x for material-heavy scenes.

```cpp
enum class PipelineLibraryPart : uint8_t {
    VertexInput,          // Vertex bindings + input assembly
    PreRasterization,     // VS/TS/MS + viewport/rasterizer state
    FragmentShader,       // FS + depth/stencil state
    FragmentOutput,       // Color blend + render target formats + MSAA
};

struct PipelineLibraryPartDesc {
    PipelineLibraryPart  part;
    // Subset of GraphicsPipelineDesc relevant to this part
    // (each part only needs its own fields, others ignored)
    GraphicsPipelineDesc partialDesc;
    PipelineLayoutHandle pipelineLayout;
};

using PipelineLibraryPartHandle = Handle<struct PipelineLibraryPartTag>;

auto CreatePipelineLibraryPart(const PipelineLibraryPartDesc&) -> Result<PipelineLibraryPartHandle>;

// Link pre-compiled parts into a final PSO. Near-zero latency if all parts cached.
struct LinkedPipelineDesc {
    PipelineLibraryPartHandle vertexInput;
    PipelineLibraryPartHandle preRasterization;
    PipelineLibraryPartHandle fragmentShader;
    PipelineLibraryPartHandle fragmentOutput;
};

auto LinkGraphicsPipeline(const LinkedPipelineDesc&) -> Result<PipelineHandle>;
```

**Backend mapping**:

| Backend       | Implementation                                                  |
| ------------- | --------------------------------------------------------------- |
| Vulkan 1.4    | `VK_EXT_graphics_pipeline_library` (4 library stages + link)    |
| D3D12         | `ID3D12PipelineLibrary` + Pipeline State Stream sub-objects     |
| Vulkan Compat | Fallback: full `CreateGraphicsPipeline` (ignore split)          |
| WebGPU / GL   | Fallback: full pipeline creation (no split compilation support) |

**Usage pattern**: RenderGraph pre-compiles vertex input and fragment output parts at startup (these are shared across many materials). Per-material fragment shader parts compile on first use. Linking combines 4 cached parts with near-zero latency.

---

## 9. Synchronization Primitives

### 9.1 Fence (CPU-GPU)

```cpp
auto CreateFence(bool signaled = false) -> Result<FenceHandle>;
void WaitFence(FenceHandle, uint64_t timeout = UINT64_MAX);
void ResetFence(FenceHandle);
auto GetFenceStatus(FenceHandle) -> bool;
```

### 9.2 Semaphore (GPU-GPU)

```cpp
enum class SemaphoreType : uint8_t {
    Binary,         // Swapchain acquire/present
    Timeline,       // Async compute sync (T1 only)
};

struct SemaphoreDesc {
    SemaphoreType type;
    uint64_t      initialValue;  // Timeline only
};

auto CreateSemaphore(const SemaphoreDesc&) -> Result<SemaphoreHandle>;
void SignalSemaphore(SemaphoreHandle, uint64_t value);    // Timeline, CPU signal
void WaitSemaphore(SemaphoreHandle, uint64_t value, uint64_t timeout); // Timeline, CPU wait
auto GetSemaphoreValue(SemaphoreHandle) -> uint64_t;      // Timeline
```

### 9.3 Barrier (In-Command-Buffer)

```cpp
struct BufferBarrierDesc {
    PipelineStage  srcStage, dstStage;
    AccessFlags    srcAccess, dstAccess;
    uint64_t       offset, size;         // WHOLE_SIZE = entire buffer
    QueueType      srcQueue, dstQueue;   // Queue ownership transfer
};

struct TextureBarrierDesc {
    PipelineStage  srcStage, dstStage;
    AccessFlags    srcAccess, dstAccess;
    TextureLayout  oldLayout, newLayout;
    TextureSubresourceRange subresource;
    QueueType      srcQueue, dstQueue;
};

struct PipelineBarrierDesc {
    PipelineStage  srcStage, dstStage;
    AccessFlags    srcAccess, dstAccess;  // Global memory barrier
};
```

### 9.4 Backend Mapping

| RHI                | Vulkan 1.4                      | D3D12                                       | Vulkan Compat                  | WebGPU              | OpenGL                                 |
| ------------------ | ------------------------------- | ------------------------------------------- | ------------------------------ | ------------------- | -------------------------------------- |
| Fence              | `VkFence`                       | `ID3D12Fence`                               | `VkFence`                      | `mapAsync` callback | `glFenceSync`                          |
| Timeline Semaphore | `VkSemaphore` (timeline)        | `ID3D12Fence` (inherently timeline)         | `VkSemaphore` (if ext)         | N/A                 | N/A                                    |
| Binary Semaphore   | `VkSemaphore` (binary)          | N/A (use fence)                             | `VkSemaphore`                  | N/A                 | N/A                                    |
| Buffer Barrier     | `vkCmdPipelineBarrier2`         | Enhanced barrier / legacy `ResourceBarrier` | `vkCmdPipelineBarrier`         | Implicit            | `glMemoryBarrier`                      |
| Texture Barrier    | `vkCmdPipelineBarrier2` (image) | Enhanced barrier (texture)                  | `vkCmdPipelineBarrier` (image) | Implicit            | `glMemoryBarrier` + `glTextureBarrier` |

**D3D12 Enhanced Barriers mapping note**: The RHI barrier model is Vulkan-centric (stage + access flags). D3D12 Enhanced Barriers use a **layout-based** model (`D3D12_BARRIER_LAYOUT`). The D3D12 backend maps RHI barriers as follows:

| RHI Concept               | D3D12 Enhanced Barrier Equivalent                                                              |
| ------------------------- | ---------------------------------------------------------------------------------------------- |
| `TextureLayout`           | `D3D12_BARRIER_LAYOUT` (1:1 mapping: `ColorAttachment` → `DIRECT_QUEUE_SHADER_RESOURCE`, etc.) |
| `srcStage` / `dstStage`   | `D3D12_BARRIER_SYNC` flags (merge stage masks into sync scope)                                 |
| `srcAccess` / `dstAccess` | `D3D12_BARRIER_ACCESS` flags (direct mapping)                                                  |
| Queue ownership transfer  | `D3D12_TEXTURE_BARRIER` with cross-queue layout transition                                     |

The D3D12 backend detects Enhanced Barriers support at init (`ID3D12Device10::CheckFeatureSupport`) and falls back to legacy `ResourceBarrier` if unavailable. The RHI public API remains unchanged — the mapping is internal to the D3D12 backend.

---

## 10. Acceleration Structure (T1 Only)

```cpp
struct AccelStructGeometryDesc {
    AccelStructGeometryType type;  // Triangles, AABBs
    BufferHandle     vertexBuffer;
    uint64_t         vertexOffset;
    uint32_t         vertexStride;
    Format           vertexFormat;
    uint32_t         vertexCount;
    BufferHandle     indexBuffer;
    uint64_t         indexOffset;
    IndexType        indexType;
    uint32_t         triangleCount;
    BufferHandle     transformBuffer;  // Optional 3x4 row-major
    uint64_t         transformOffset;
    AccelStructGeometryFlags flags;   // Opaque, NoDuplicateAnyHit
};

struct BLASDesc {
    std::span<const AccelStructGeometryDesc> geometries;
    AccelStructBuildFlags flags;  // PreferFastTrace, PreferFastBuild, AllowUpdate
};

// Instance data for TLAS (matches VkAccelerationStructureInstanceKHR / D3D12_RAYTRACING_INSTANCE_DESC)
struct AccelStructInstance {
    float            transform[3][4];      // 3x4 row-major affine transform
    uint32_t         instanceCustomIndex : 24;
    uint32_t         mask : 8;
    uint32_t         sbtRecordOffset : 24;
    uint32_t         flags : 8;            // AccelStructInstanceFlags (TriangleFacingCullDisable, ForceOpaque, etc.)
    uint64_t         accelerationStructureReference;  // BDA or GPU handle of BLAS
};
static_assert(sizeof(AccelStructInstance) == 64);

struct TLASDesc {
    BufferHandle     instanceBuffer;   // Array of AccelStructInstance (64B each)
    uint32_t         instanceCount;
    AccelStructBuildFlags flags;
};

// Pre-query scratch and result buffer sizes before building.
// Both Vulkan (vkGetAccelerationStructureBuildSizesKHR) and D3D12
// (GetRaytracingAccelerationStructurePrebuildInfo) require this.
struct AccelStructBuildSizes {
    uint64_t accelerationStructureSize;  // Result buffer size
    uint64_t buildScratchSize;           // Scratch buffer size for build
    uint64_t updateScratchSize;          // Scratch buffer size for refit (0 if !AllowUpdate)
};

auto GetBLASBuildSizes(const BLASDesc&) -> AccelStructBuildSizes;
auto GetTLASBuildSizes(const TLASDesc&) -> AccelStructBuildSizes;

auto CreateBLAS(const BLASDesc&) -> Result<AccelStructHandle>;
auto CreateTLAS(const TLASDesc&) -> Result<AccelStructHandle>;

// Build commands (in command buffer)
void CmdBuildBLAS(AccelStructHandle, BufferHandle scratch);
void CmdBuildTLAS(AccelStructHandle, BufferHandle scratch);
void CmdUpdateBLAS(AccelStructHandle, BufferHandle scratch);  // Refit
```

---

## 11. Swapchain & Multi-Window Surface Management

> **Companion document**: `specs/phases/phase-01-1a/01-window-manager.md` — tree-structured multi-window lifecycle, `SurfaceManager`, cascade destruction protocol, GPU resource sharing model.

### 11.1 Low-Level Swapchain API (Device-Level)

The raw swapchain API lives on the device. Higher-level abstractions (`RenderSurface`, `SurfaceManager`) wrap these calls.

```cpp
struct SwapchainDesc {
    NativeSurfaceHandle surface;       // Platform-specific (HWND, wl_surface, etc.)
    uint32_t            width, height;
    Format              preferredFormat;  // BGRA8_SRGB typical
    PresentMode         presentMode;     // Immediate, Mailbox, Fifo, FifoRelaxed
    uint32_t            imageCount;      // 2-3
    bool                hdr;             // HDR10 / scRGB
};

auto CreateSwapchain(const SwapchainDesc&) -> Result<SwapchainHandle>;
void DestroySwapchain(SwapchainHandle);
auto ResizeSwapchain(SwapchainHandle, uint32_t width, uint32_t height) -> Result<void>;
auto AcquireNextImage(SwapchainHandle, SemaphoreHandle signalSemaphore, FenceHandle signalFence = {}) -> Result<uint32_t>;
auto GetSwapchainTexture(SwapchainHandle, uint32_t imageIndex) -> TextureHandle;
void Present(SwapchainHandle, std::span<const SemaphoreHandle> waitSemaphores);
```

### 11.2 Multi-Window Architecture

Multi-window rendering uses three decoupled subsystems (see companion document for full API):

| Subsystem          | Namespace        | Responsibility                                                                       |
| ------------------ | ---------------- | ------------------------------------------------------------------------------------ |
| `WindowManager`    | `miki::platform` | OS window tree (create/destroy/events), parent–child hierarchy, cascade destruction  |
| `SurfaceManager`   | `miki::rhi`      | Per-window `RenderSurface` + `FrameManager` lifecycle, GPU surface attach/detach     |
| `IDevice` (shared) | `miki::rhi`      | Single GPU device shared by all windows; all GPU resources available to all surfaces |

**Key design properties**:

- **Tree-structured windows**: parent–child hierarchy with post-order cascade destruction (GPU surfaces drained before OS windows destroyed)
- **GPU resource sharing**: single `IDevice`, textures/buffers/pipelines created once and used in any window's render passes
- **Three-concern separation**: `WindowManager` never touches GPU; `SurfaceManager` never creates OS windows; events flow through `WindowManager` and are dispatched by application code
- **Runtime backend switching**: `SurfaceManager` detach/reattach cycle preserves OS windows while swapping GPU backend (§1.4)

---

## 12. Shader Module

```cpp
enum class ShaderStage : uint32_t {
    Vertex      = 1 << 0,
    Fragment    = 1 << 1,
    Compute     = 1 << 2,
    Task        = 1 << 3,
    Mesh        = 1 << 4,
    RayGen      = 1 << 5,
    AnyHit      = 1 << 6,
    ClosestHit  = 1 << 7,
    Miss        = 1 << 8,
    Intersection = 1 << 9,
    Callable    = 1 << 10,
    AllGraphics = Vertex | Fragment,
    All         = 0x7FF,
};

struct ShaderModuleDesc {
    ShaderStage              stage;
    std::span<const uint8_t> code;        // SPIR-V, DXIL, GLSL text, WGSL text
    const char*              entryPoint;  // "main" typical
    const char*              debugName;
};

auto CreateShaderModule(const ShaderModuleDesc&) -> Result<ShaderModuleHandle>;
```

---

## 13. Memory Management Strategy

### 13.1 Allocator Architecture

```
┌────────────────────────────────────────────────┐
│              RHI Memory Allocator               │
├──────────┬──────────┬──────────┬───────────────┤
│ VMA      │ D3D12MA  │ VMA      │ WebGPU/GL     │
│ (Vk 1.4) │          │ (Compat) │ (API-managed) │
└──────────┴──────────┴──────────┴───────────────┘
```

| Backend       | Allocator                        | Strategy                                                                              |
| ------------- | -------------------------------- | ------------------------------------------------------------------------------------- |
| Vulkan 1.4    | VMA (Vulkan Memory Allocator)    | Dedicated alloc for large (>256KB); sub-alloc for small; sparse binding for streaming |
| D3D12         | D3D12MA (D3D12 Memory Allocator) | Placed resources in heaps; reserved resources for sparse                              |
| Vulkan Compat | VMA                              | Same as T1 minus sparse                                                               |
| WebGPU        | API-managed                      | `createBuffer` / `createTexture` (no explicit memory)                                 |
| OpenGL        | API-managed                      | `glBufferStorage` / `glTexStorage2D` (immutable)                                      |

### 13.2 Staging Ring Buffer

Per-frame ring buffer for CPU→GPU uploads:

```cpp
class StagingRing {
    BufferHandle ring_;          // CpuToGpu, persistent-mapped
    uint64_t     capacity_;     // 64MB default (configurable)
    uint64_t     writeOffset_;  // Monotonically increasing, wraps

    struct Allocation {
        void*    cpuPtr;
        uint64_t gpuOffset;
        uint64_t size;
    };

    auto Allocate(uint64_t size, uint64_t alignment) -> Result<Allocation>;
    void Reset(uint64_t fenceValue);  // Reclaim after GPU consumption
};
```

### 13.3 Readback Ring Buffer

Per-frame ring buffer for GPU→CPU readback (symmetric to `StagingRing`). Used by `GpuProfiler` (timestamp results), `ShaderPrintf` (SSBO readback), `GpuBreadcrumbs` (marker state), and `Telemetry`.

```cpp
class ReadbackRing {
    BufferHandle ring_;          // GpuToCpu, persistent-mapped
    uint64_t     capacity_;     // 4MB default (configurable)
    uint64_t     writeOffset_;  // Monotonically increasing, wraps

    struct Allocation {
        const void* cpuPtr;     // Read-only CPU pointer (after fence)
        uint64_t    gpuOffset;  // Offset in ring buffer for CmdCopyBuffer destination
        uint64_t    size;
    };

    // Reserve space for a readback. Returns GPU offset for CmdCopyBuffer dst.
    auto Allocate(uint64_t size, uint64_t alignment) -> Result<Allocation>;
    // After fence signals, caller reads cpuPtr. Reset reclaims space.
    void Reset(uint64_t fenceValue);
};
```

**Backend mapping**:

| Backend        | Implementation                                                       |
| -------------- | -------------------------------------------------------------------- | -------------------------------- |
| Vulkan / D3D12 | `GpuToCpu` buffer (`HOST_VISIBLE                                     | HOST_CACHED`), persistent-mapped |
| WebGPU         | `mapAsync(GPUMapMode::READ)` on staging buffer (async callback)      |
| OpenGL         | `glGetBufferSubData` or persistent-mapped `GL_MAP_READ_BIT` (GL 4.4) |

### 13.4 Transient Resource Pool (RenderGraph-Owned)

RenderGraph allocates transient textures (GBuffer, HiZ, etc.) from a pool that aliases non-overlapping lifetimes:

```
RenderGraph lifetime analysis (Kahn sort) → aliasing groups
  → VkDeviceMemory / ID3D12Heap shared by non-overlapping resources
  → 30-50% VRAM savings on render targets
```

The RHI provides the primitives (see §5.6 Transient & Memory Aliasing API):

- `BufferDesc::transient` / `TextureDesc::transient` — marks resources eligible for aliasing
- `CreateMemoryHeap` — allocates a shared heap
- `AliasBufferMemory` / `AliasTextureMemory` — binds transient resources to offsets within shared heaps
- `GetBufferMemoryRequirements` / `GetTextureMemoryRequirements` — queries size/alignment for placement

Aliasing policy (lifetime analysis, group assignment, heap sizing) is entirely in RenderGraph — RHI provides primitives only.

### 13.5 Memory Statistics API

Debug and profiling layers (`MemProfiler`, `Telemetry`) need GPU memory budget and usage data. Without an RHI-level query, these layers would bypass the abstraction and call VMA / D3D12MA / DXGI directly — violating the Thin Abstraction boundary.

```cpp
struct MemoryHeapBudget {
    uint64_t budgetBytes;       // OS-reported budget for this heap (DXGI / VK_EXT_memory_budget)
    uint64_t usageBytes;        // Current usage in this heap
    uint32_t heapIndex;         // Backend-specific heap index
    bool     isDeviceLocal;     // true = VRAM, false = system RAM
};

struct MemoryStats {
    uint32_t totalAllocationCount;   // Live allocations (VMA / D3D12MA tracked)
    uint64_t totalAllocatedBytes;    // Total bytes allocated (including sub-allocation overhead)
    uint64_t totalUsedBytes;         // Bytes actually in use by resources
    uint32_t heapCount;              // Number of heaps reported
};

// Device API
[[nodiscard]] auto GetMemoryStats() const -> MemoryStats;
[[nodiscard]] auto GetMemoryHeapBudgets(std::span<MemoryHeapBudget> out) const -> uint32_t;
```

| Backend | `GetMemoryStats`                                                                    | `GetMemoryHeapBudgets`                                  |
| ------- | ----------------------------------------------------------------------------------- | ------------------------------------------------------- |
| Vulkan  | `vmaCalculateStatistics()`                                                          | `vmaGetHeapBudgets()` (requires `VK_EXT_memory_budget`) |
| D3D12   | `D3D12MA::Allocator::GetBudget()`                                                   | `IDXGIAdapter3::QueryVideoMemoryInfo()` per segment     |
| OpenGL  | `GL_NVX_gpu_memory_info` / `GL_ATI_meminfo` (best-effort; returns 0 if unsupported) | Single heap, budget = queried VRAM                      |
| WebGPU  | Returns `{0, 0, 0, 0}` (no memory introspection API)                                | `heapCount = 0`                                         |

**Frequency**: These are **not** hot-path calls. Expected usage: once per frame by `MemProfiler::Snapshot()` (~1us). No caching needed at the RHI level.

---

## 14. Error Handling

All fallible operations return `std::expected<T, RhiError>`:

```cpp
enum class RhiError : uint32_t {
    OutOfDeviceMemory,
    OutOfHostMemory,
    DeviceLost,
    SurfaceLost,
    FormatNotSupported,
    FeatureNotSupported,
    InvalidHandle,
    InvalidParameter,
    ShaderCompilationFailed,
    PipelineCreationFailed,
    TooManyObjects,
};

template <typename T>
using Result = std::expected<T, RhiError>;
```

No exceptions in hot paths. `DeviceLost` triggers graceful recovery (re-create device + all resources).

---

## 15. Debug & Profiling Integration

### 15.1 Object Naming

All `*Desc` structs have `const char* debugName`. Backends forward to:

| Backend | API                            |
| ------- | ------------------------------ |
| Vulkan  | `vkSetDebugUtilsObjectNameEXT` |
| D3D12   | `SetName` (wide string)        |
| OpenGL  | `glObjectLabel`                |
| WebGPU  | `label` field                  |

### 15.2 Debug Labels

`CmdBeginDebugLabel` / `CmdEndDebugLabel` map to:

| Backend | API                                    |
| ------- | -------------------------------------- |
| Vulkan  | `vkCmdBeginDebugUtilsLabelEXT`         |
| D3D12   | `PIXBeginEvent` / `PIXEndEvent`        |
| OpenGL  | `glPushDebugGroup` / `glPopDebugGroup` |
| WebGPU  | `pushDebugGroup` / `popDebugGroup`     |

### 15.3 GPU Timestamps

```cpp
auto CreateQueryPool(QueryType type, uint32_t count) -> Result<QueryPoolHandle>;
auto GetQueryResults(QueryPoolHandle, uint32_t first, uint32_t count, std::span<uint64_t> results) -> Result<void>;
auto GetTimestampPeriod() -> double;  // Nanoseconds per tick
```

---

## 16. Per-Tier Optimization Paths

### 16.1 Tier1 Vulkan 1.4 — Maximum Performance Path

| Feature            | Implementation                                                                                                                                                                                                                                         |
| ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Descriptor binding | `VK_EXT_descriptor_buffer` (current primary); migration to `VK_EXT_descriptor_heap` planned when driver support matures (published Jan 2026, SDK Q1 2026, driver adoption expected H2 2026). Internal to T1 Vulkan backend — public RHI API unchanged. |
| Push constants     | Native 256B `vkCmdPushConstants` (future: `VK_EXT_descriptor_heap` push data interface)                                                                                                                                                                |
| Mesh shaders       | `VK_EXT_mesh_shader` (task + mesh)                                                                                                                                                                                                                     |
| Dynamic rendering  | `VK_KHR_dynamic_rendering` (core 1.3)                                                                                                                                                                                                                  |
| Barriers           | `vkCmdPipelineBarrier2` (core 1.3) with `VK_KHR_synchronization2`                                                                                                                                                                                      |
| Async compute      | Dedicated compute queue + timeline semaphore                                                                                                                                                                                                           |
| Transfer           | Dedicated transfer queue (Vulkan 1.4 streaming)                                                                                                                                                                                                        |
| BDA                | `VK_KHR_buffer_device_address` (core 1.2)                                                                                                                                                                                                              |
| Ray tracing        | `VK_KHR_ray_query` + `VK_KHR_acceleration_structure`                                                                                                                                                                                                   |
| VRS                | `VK_KHR_fragment_shading_rate`                                                                                                                                                                                                                         |
| Sparse binding     | `VkSparseBufferMemoryBindInfo` for streaming pages                                                                                                                                                                                                     |
| Subgroup ops       | `VK_KHR_shader_subgroup_extended_types` (core 1.2)                                                                                                                                                                                                     |

### 16.2 Tier1 D3D12 — Maximum Performance Path

| Feature            | Implementation                                                |
| ------------------ | ------------------------------------------------------------- |
| Descriptor binding | Root signature + CBV/SRV/UAV descriptor heap (shader-visible) |
| Root constants     | 64 DWORDs in root signature                                   |
| Mesh shaders       | Amplification + Mesh shader (SM 6.5)                          |
| Render pass        | `BeginRenderPass` / `EndRenderPass` with enhanced barriers    |
| Barriers           | Enhanced barriers (`D3D12_BARRIER_*`)                         |
| Async compute      | Compute command queue + `ID3D12Fence`                         |
| Transfer           | Copy command queue                                            |
| BDA                | `GetGPUVirtualAddress()` on buffers                           |
| Ray tracing        | DXR 1.1 (inline ray query in any shader)                      |
| VRS                | `RSSetShadingRate` + `RSSetShadingRateImage`                  |
| Work graphs        | `DispatchGraph` for GPU-driven work (future)                  |

### 16.3 Tier2 Vulkan Compat — Broad Compatibility

| Feature            | Implementation                                                        |
| ------------------ | --------------------------------------------------------------------- |
| Descriptor binding | Traditional `VkDescriptorSet` with auto-growing pool                  |
| Push constants     | Native (256B)                                                         |
| Geometry           | Vertex shader + `vkCmdDrawIndexedIndirect` (MDI)                      |
| Rendering          | `vkCmdBeginRenderPass` (legacy) or dynamic rendering if ext available |
| Barriers           | `vkCmdPipelineBarrier` (Vulkan 1.0 style)                             |
| No async compute   | Single queue                                                          |
| No ray tracing     | CPU BVH fallback                                                      |

### 16.4 Tier3 WebGPU — Browser Target

| Feature            | Implementation                                        |
| ------------------ | ----------------------------------------------------- |
| Descriptor binding | `GPUBindGroup` per material/pass (immutable)          |
| Push constants     | Emulated via 256B uniform buffer (offset 0, set 0)    |
| Geometry           | `draw()` / `drawIndexed()` (no MDI)                   |
| Rendering          | Render pass encoder                                   |
| Barriers           | Implicit (Dawn/wgpu handles transitions)              |
| SSBO limit         | 128MB `maxStorageBufferBindingSize` → chunked binding |
| Shader             | WGSL (Slang → WGSL)                                   |
| Single queue       | No async compute                                      |

### 16.5 Tier4 OpenGL 4.3 — Legacy / VM Target

| Feature            | Implementation                                                     |
| ------------------ | ------------------------------------------------------------------ |
| Descriptor binding | Direct `glBindBufferRange` / `glBindTextureUnit` / `glBindSampler` |
| Push constants     | Emulated via 128B UBO (binding 0)                                  |
| Geometry           | `glMultiDrawElementsIndirect` (MDI)                                |
| Rendering          | FBO bind/unbind                                                    |
| Barriers           | `glMemoryBarrier` (coarse)                                         |
| State              | Direct State Access (DSA, GL 4.5 preferred)                        |
| Compute            | `glDispatchCompute` (GL 4.3 core)                                  |

---

## 17. Thread Safety Model

| Operation                     | Thread Safety                                         |
| ----------------------------- | ----------------------------------------------------- |
| Device creation/destruction   | Main thread only                                      |
| Resource creation/destruction | Thread-safe (internal lock on handle pool)            |
| Command buffer recording      | **Per-command-buffer single-thread** (no lock needed) |
| Command buffer submission     | Thread-safe (queue lock)                              |
| Descriptor set creation       | Thread-safe                                           |
| Pipeline creation             | Thread-safe (pipeline cache has internal lock)        |
| Swapchain operations          | Main thread only                                      |
| Staging ring allocation       | Per-thread ring or lock-free ring                     |

**Design principle**: Command recording is lock-free. Multiple threads record into separate command buffers concurrently. Submission serializes at the queue level.

---

## 17.1 Implementation Suggestions

### 17.1.1 CRTP Compile-Time Optimization

The CRTP design eliminates virtual dispatch overhead but can increase compile time due to template instantiation. Recommended mitigation:

```cpp
// Device.h — public header
template <typename Impl>
class DeviceBase {
    // ... interface methods as inline wrappers
};

// Device.inl — implementation (included only in .cpp files)
template <typename Impl>
inline auto DeviceBase<Impl>::CreateBuffer(const BufferDesc& desc) -> Result<BufferHandle> {
    return static_cast<Impl*>(this)->CreateBufferImpl(desc);
}

// Explicit instantiations in Device.cpp (one per backend)
extern template class DeviceBase<VulkanDevice>;
extern template class DeviceBase<D3D12Device>;
extern template class DeviceBase<VulkanCompatDevice>;
extern template class DeviceBase<WebGPUDevice>;
extern template class DeviceBase<OpenGLDevice>;
```

**Benefits**:

- Header-only declarations keep API fast to compile for users
- Implementation details isolated to .cpp files
- Explicit instantiation prevents code bloat in translation units
- Backend selection remains compile-time (no vtable)

### 17.1.2 Handle Pool Lock Strategy

Resource creation is **not** hot path (startup / level load). Use a simple `std::mutex` for
correctness and simplicity. A lock-free free-list with bare `atomic<uint32_t>` head pointer
is susceptible to the **ABA problem** (thread A pops slot 5, thread B pops 5 then pushes 5
with different next, thread A's CAS succeeds with stale next pointer → corrupted free list).
Avoiding ABA requires either a tagged pointer (`atomic<uint64_t>` with epoch counter) or
hazard pointers — both add complexity unjustified for a non-hot-path operation.

```cpp
template <typename T, size_t Capacity>
class HandlePool {
    mutable std::mutex mutex_;              // Simple lock — not hot path
    uint32_t           freeListHead_ = 0;   // Index into slots_ free chain
    uint32_t           freeCount_    = Capacity;
    std::array<Slot, Capacity> slots_;

    struct Slot {
        T        object;
        uint32_t nextFree;
        uint16_t generation = 0;            // Incremented on each destroy
    };

public:
    // O(1) create: lock, pop free list, unlock
    auto Allocate() -> std::pair<Handle<T>, T*>;
    // O(1) destroy: lock, push free list, increment generation, unlock
    void Free(Handle<T>);
    // O(1) lookup: no lock needed (generation check is read-only, slot is stable)
    auto Lookup(Handle<T>) const -> T*;
};
```

**Rationale**: `Allocate`/`Free` are O(1) under lock, called O(100)/frame at most. Mutex
contention is unmeasurable (<1μs/frame). `Lookup` is lock-free — only a generation check on
a stable slot, safe for concurrent reads. This avoids ABA entirely with zero complexity cost.

### 17.1.3 Command Buffer Recording Validation

In debug builds, add lightweight state validation:

```cpp
#ifdef MIKI_RHI_DEBUG
void CommandBuffer::CmdDraw(uint32_t vertexCount, uint32_t instanceCount) {
    MIKI_ASSERT(state_.pipelineBound, "Pipeline must be bound before draw");
    MIKI_ASSERT(state_.indexBufferBound || vertexCount > 0, "Vertex count must be >0");
    // ... other cheap checks
}
#endif
```

**Principle**: Validation should be O(1) and compile out in release builds. No hidden state tracking in hot paths.

---

## 18. Extension & Future-Proofing

### 18.1 Vulkan Extension Negotiation

```cpp
struct VulkanDeviceExtensions {
    bool descriptorBuffer;      // VK_EXT_descriptor_buffer (current primary path)
    bool descriptorHeap;        // VK_EXT_descriptor_heap (proposed 2026+, future upgrade)
    bool meshShader;            // VK_EXT_mesh_shader
    bool rayQuery;              // VK_KHR_ray_query
    bool accelStruct;           // VK_KHR_acceleration_structure
    bool fragmentShadingRate;   // VK_KHR_fragment_shading_rate
    bool cooperativeMatrix;     // VK_KHR_cooperative_matrix
    bool memoryDecompression;   // VK_NV_memory_decompression (GDeflate)
    bool pipelineLibrary;       // VK_EXT_graphics_pipeline_library
    bool deviceGeneratedCommands; // VK_EXT_device_generated_commands (GPU-driven indirect)
    // ...
};
```

### 18.2 Future Extensions

| Feature            | Vulkan                                    | D3D12                         | RHI Impact                                           |
| ------------------ | ----------------------------------------- | ----------------------------- | ---------------------------------------------------- |
| Descriptor heap    | `VK_EXT_descriptor_heap` (proposed 2026+) | Already native                | New descriptor path, coexists with descriptor buffer |
| Work graphs        | N/A (Vulkan equivalent TBD)               | `DispatchGraph` (SM 6.8)      | New `CmdDispatchGraph` command                       |
| Cooperative matrix | `VK_KHR_cooperative_matrix`               | SM 6.9 wave matrix            | New shader intrinsics, no RHI API change             |
| GDeflate HW decode | `VK_NV_memory_decompression`              | DirectStorage                 | New `CmdDecompressBuffer` command (see below)        |
| Fence barriers     | N/A                                       | D3D12 Fence Barriers (Tier-1) | Enhanced barrier model (see §9.4 mapping note)       |

**`CmdDecompressBuffer` signature preview** (Phase 6b+, not yet active):

```cpp
enum class CompressionFormat : uint8_t {
    GDeflate,       // NVIDIA RTX 40+ / RDNA3+ hardware decode
    // Future: LZ4_HW, Zstd_HW
};

struct DecompressBufferDesc {
    BufferHandle     srcBuffer;       // Compressed source
    uint64_t         srcOffset;
    uint64_t         srcSize;
    BufferHandle     dstBuffer;       // Decompressed destination
    uint64_t         dstOffset;
    uint64_t         dstSize;         // Expected decompressed size
    CompressionFormat format;
};

// T1 only. Requires GpuCapabilityProfile.hasHardwareDecompression.
// Falls back to async compute shader decode or CPU LZ4 (not RHI scope).
void CmdDecompressBuffer(const DecompressBufferDesc&);
```

| Backend     | Implementation                                                                             |
| ----------- | ------------------------------------------------------------------------------------------ |
| Vulkan (NV) | `vkCmdDecompressMemoryNV` (`VK_NV_memory_decompression`)                                   |
| D3D12       | DirectStorage `IDStorageQueue::EnqueueRequest` with GPU decompression                      |
| Others      | Not available — fallback to compute shader or CPU decode (handled by ChunkLoader, not RHI) |

---

## 19. Naming Conventions & Code Style

```
Namespace:        miki::rhi
Types:            PascalCase (BufferDesc, TextureHandle)
Functions:        PascalCase (CreateBuffer, CmdDraw)
Enums:            PascalCase values (BufferUsage::Storage)
Constants:        kPascalCase (kMaxFramesInFlight)
Handles:          <Type>Handle (BufferHandle, PipelineHandle)
Backend impl:     miki::rhi::vk, miki::rhi::d3d12, miki::rhi::gl, miki::rhi::wgpu
Files:            PascalCase.h / PascalCase.cpp
```

---

## 20. Summary: Design Decisions & Rationale

| Decision                                                                   | Alternatives Considered                                                         | Rationale                                                                                                                                                                                                                                                                        |
| -------------------------------------------------------------------------- | ------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CRTP over virtual dispatch                                                 | vtable (NVRHI), type-erasure (wgpu)                                             | Zero overhead in command recording hot path; backend known at compile time                                                                                                                                                                                                       |
| Explicit barriers over auto-tracking                                       | NVRHI auto-tracking, wgpu implicit                                              | RenderGraph already tracks states; double-tracking wastes CPU; explicit is debuggable                                                                                                                                                                                            |
| 64-bit typed handles over raw pointers                                     | COM pointers (D3D), shared_ptr                                                  | Cache-friendly (8B), generation-safe, no ref-count overhead, trivially copyable                                                                                                                                                                                                  |
| Dynamic rendering over render pass objects                                 | VkRenderPass (Vulkan 1.0)                                                       | Simpler API, matches D3D12/WebGPU/GL model, core in Vulkan 1.3                                                                                                                                                                                                                   |
| Deferred destruction over ref-counting                                     | COM AddRef/Release, shared_ptr                                                  | Deterministic, no atomic ref-count in hot path, 2-frame latency matches frames-in-flight                                                                                                                                                                                         |
| `VK_EXT_descriptor_buffer` as primary, `descriptor_heap` as future upgrade | Descriptor sets only, descriptor heap only                                      | `descriptor_buffer` is shipping in drivers today; `descriptor_heap` is Vulkan Roadmap 2026+ proposed. Both paths will coexist; D3D12 maps 1:1 to descriptor heap natively                                                                                                        |
| Slang as shader language                                                   | HLSL, GLSL, hand-written per-backend                                            | Single source → all targets; Slang v2026.5 has mature WGSL/GLSL/SPIR-V/DXIL output                                                                                                                                                                                               |
| Secondary command buffers (`CmdExecuteSecondary`)                          | Primary-only multi-CmdBuf submit                                                | Enables multi-threaded recording within a render pass. Vulkan secondary cmd bufs, D3D12 bundles, WebGPU render bundles. Without this, parallel recording requires splitting render passes                                                                                        |
| `ReadbackRing` symmetric to `StagingRing`                                  | Per-subsystem ad-hoc readback buffers                                           | GpuProfiler, ShaderPrintf, GpuBreadcrumbs all need GPU→CPU readback. Shared ring avoids code duplication and memory fragmentation                                                                                                                                                |
| Pipeline Library (split compilation)                                       | Monolithic PSO creation only                                                    | `VK_EXT_graphics_pipeline_library` / D3D12 Pipeline State Streams reduce PSO creation stalls 10-100x. Critical for material-heavy CAD scenes with thousands of unique PSOs                                                                                                       |
| `DescriptorWrite` uses `std::variant<BufferBinding, TextureBinding, ...>`  | Flat struct with all fields (wastes 40+ bytes, ambiguous which field is active) | Type-safe, self-documenting, compiler can optimize variant dispatch. No runtime ambiguity about which resource is bound                                                                                                                                                          |
| `HandlePool` uses `std::mutex` (not lock-free)                             | Lock-free CAS free-list with `atomic<uint32_t>` head                            | Resource creation is not hot path (O(100)/frame). Bare `atomic<uint32_t>` CAS free-list has ABA problem. Mutex is correct, simple, and unmeasurable in profiling (<1μs/frame)                                                                                                    |
| No built-in render graph                                                   | Integrated RG (UE5 RDG)                                                         | Separation of concerns; RHI is reusable without RG; RG can be replaced independently                                                                                                                                                                                             |
| `CommandListHandle` dispatch-once facade                                   | Command Packet serialization (bgfx, Filament/GL), full virtual `ICommandBuffer` | Type-erasure cost paid O(passes) ~50-100/frame (<1μs), not O(draws) ~10000+. Command Packets add ~100μs replay overhead, destroy debuggability (RenderDoc/PIX see `Translator::Execute()`), and double maintenance cost. Virtual dispatch adds ~2-5ns/draw ×10K = ~25-50μs/frame |
| `GetMemoryStats()` / `GetMemoryHeapBudgets()` in RHI                       | Debug layer calls VMA/D3D12MA directly                                          | Maintains Thin Abstraction boundary; profilers should not bypass RHI to query allocator internals                                                                                                                                                                                |
| Reserved binding `@group(0) @binding(0)` for push constant UBO (WebGPU/GL) | No reservation (implicit conflict)                                              | Prevents slot collision when Slang cross-compiles `[vk::push_constant]` to WGSL `var<uniform>`. Debug validation catches violations. Vulkan/D3D12 unaffected (native push constants)                                                                                             |
| `SparseBindDesc` separates data from sync                                  | Vulkan-style embedded semaphores in `SparseBindDesc`                            | D3D12 `UpdateTileMappings` is queue-serialized — embedding semaphores forces D3D12 backend to fake `ID3D12Fence` management, violating Thin Abstraction. Sync passed at `SubmitSparseBinds()` call site instead                                                                  |

---

## Appendix A: Format Table (Subset)

```cpp
enum class Format : uint32_t {
    Undefined = 0,
    // 8-bit
    R8_UNORM, R8_SNORM, R8_UINT, R8_SINT,
    RG8_UNORM, RG8_SNORM, RG8_UINT, RG8_SINT,
    RGBA8_UNORM, RGBA8_SNORM, RGBA8_UINT, RGBA8_SINT, RGBA8_SRGB,
    BGRA8_UNORM, BGRA8_SRGB,
    // 16-bit
    R16_UNORM, R16_SNORM, R16_UINT, R16_SINT, R16_SFLOAT,
    RG16_UNORM, RG16_SNORM, RG16_UINT, RG16_SINT, RG16_SFLOAT,
    RGBA16_UNORM, RGBA16_SNORM, RGBA16_UINT, RGBA16_SINT, RGBA16_SFLOAT,
    // 32-bit
    R32_UINT, R32_SINT, R32_SFLOAT,
    RG32_UINT, RG32_SINT, RG32_SFLOAT,  // RG32_UINT: VisBuffer (instanceId + primitiveId)
    RGB32_UINT, RGB32_SINT, RGB32_SFLOAT,
    RGBA32_UINT, RGBA32_SINT, RGBA32_SFLOAT,
    // Depth/stencil
    D16_UNORM, D32_SFLOAT, D24_UNORM_S8_UINT, D32_SFLOAT_S8_UINT,
    // Compressed
    BC1_RGBA_UNORM, BC1_RGBA_SRGB,
    BC3_RGBA_UNORM, BC3_RGBA_SRGB,
    BC4_R_UNORM, BC4_R_SNORM,
    BC5_RG_UNORM, BC5_RG_SNORM,
    BC6H_RGB_UFLOAT, BC6H_RGB_SFLOAT,
    BC7_RGBA_UNORM, BC7_RGBA_SRGB,
    // ASTC (mobile/WebGPU)
    ASTC_4x4_UNORM, ASTC_4x4_SRGB,
};
```

## Appendix B: Pipeline Stage & Access Flags

```cpp
enum class PipelineStage : uint32_t {
    TopOfPipe           = 1 << 0,
    DrawIndirect        = 1 << 1,
    VertexInput         = 1 << 2,
    VertexShader        = 1 << 3,
    TaskShader          = 1 << 4,
    MeshShader          = 1 << 5,
    FragmentShader      = 1 << 6,
    EarlyFragmentTests  = 1 << 7,
    LateFragmentTests   = 1 << 8,
    ColorAttachmentOutput = 1 << 9,
    ComputeShader       = 1 << 10,
    Transfer            = 1 << 11,
    BottomOfPipe        = 1 << 12,
    Host                = 1 << 13,
    AllGraphics         = 1 << 14,
    AllCommands         = 1 << 15,
    AccelStructBuild    = 1 << 16,
    RayTracingShader    = 1 << 17,
    ShadingRateImage    = 1 << 18,
};

enum class AccessFlags : uint32_t {
    None                    = 0,
    IndirectCommandRead     = 1 << 0,
    IndexRead               = 1 << 1,
    VertexAttributeRead     = 1 << 2,
    UniformRead             = 1 << 3,
    InputAttachmentRead     = 1 << 4,
    ShaderRead              = 1 << 5,
    ShaderWrite             = 1 << 6,
    ColorAttachmentRead     = 1 << 7,
    ColorAttachmentWrite    = 1 << 8,
    DepthStencilRead        = 1 << 9,
    DepthStencilWrite       = 1 << 10,
    TransferRead            = 1 << 11,
    TransferWrite           = 1 << 12,
    HostRead                = 1 << 13,
    HostWrite               = 1 << 14,
    MemoryRead              = 1 << 15,
    MemoryWrite             = 1 << 16,
    AccelStructRead         = 1 << 17,
    AccelStructWrite        = 1 << 18,
    ShadingRateImageRead    = 1 << 19,
};

enum class TextureLayout : uint8_t {
    Undefined,
    General,
    ColorAttachment,
    DepthStencilAttachment,
    DepthStencilReadOnly,
    ShaderReadOnly,
    TransferSrc,
    TransferDst,
    Present,
    ShadingRate,
};
```

---

## 21. Architecture Review (逐条审核)

> **Audit baseline**: `specs/rendering-pipeline-architecture.md` (target architecture) + industry SOTA as of 2026-Q1.
> **Audit date**: 2026-03-27.
> **Verdict legend**: ✅ PASS — no action needed. ⚠️ WARN — minor gap, document or plan fix. ❌ FAIL — blocking issue, must fix before implementation.

### 21.1 §1 Design Goals & Constraints

| Goal                       | Verdict | Notes                                                                                                                                                                                                            |
| -------------------------- | :-----: | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| G1 Zero-overhead T1        |   ✅    | CRTP + `CommandListHandle` dispatch-once facade correctly eliminates virtual dispatch from hot path. Performance analysis (§2.2) is quantitatively sound: 100 passes × 5ns switch = 500ns/frame ≪ 16.7ms budget. |
| G2 Max capability per tier |   ✅    | 5-tier system (T1-Vk, T1-D3D12, T2, T3, T4) matches `rendering-pipeline-architecture.md` §1.3 Tier Feature Matrix exactly.                                                                                       |
| G3 Compile-time backend    |   ✅    | CRTP + explicit template instantiation. Consistent with `rendering-pipeline-architecture.md` §1.5 `DeviceBase<Impl>`.                                                                                            |
| G4 Thin abstraction        |   ✅    | No hidden state tracking, no implicit barriers. Consistent with RenderGraph-driven barrier model in `rendering-pipeline-architecture.md` §2.1.                                                                   |
| G5 RenderGraph-friendly    |   ✅    | Transient aliasing primitives (§5.6), explicit barriers (§9.3), `CommandListHandle` dispatch-once pattern all align.                                                                                             |
| G6 Shader IR agnostic      |   ✅    | `ShaderModuleDesc` accepts SPIR-V/DXIL/GLSL/WGSL blobs. Matches `rendering-pipeline-architecture.md` §1.3 Shader IR column.                                                                                      |
| G7 Deterministic lifetime  |   ✅    | Deferred destruction queue (§5.4), 2-frame latency, no ref-counting.                                                                                                                                             |

**§1.4 Runtime Backend Switching**: ✅ Well-designed. Protocol (8-step teardown/recreate) is correct. Pipeline cache blob-key approach for cross-backend cache is sound. Filament precedent correctly cited.

**Overall §1 verdict**: ✅ **PASS**. No issues.

---

### 21.2 §2 Architecture Overview

| Item                        | Verdict | Notes                                                                                                            |
| --------------------------- | :-----: | ---------------------------------------------------------------------------------------------------------------- |
| Layer diagram               |   ✅    | 5-backend + RHI + RenderGraph + Application. Matches `rendering-pipeline-architecture.md` §2.1.                  |
| CRTP (§2.2)                 |   ✅    | `DeviceBase<Impl>` / `CommandBufferBase<Impl>` design correct.                                                   |
| `DeviceHandle`              |   ✅    | Type-erased facade with 5-way switch. O(passes) frequency.                                                       |
| `CommandListHandle`         |   ✅    | Dispatch-once pattern resolves CRTP vs type-erasure contradiction. Command Packet rejection well-justified.      |
| Module decomposition (§2.3) |   ⚠️    | Missing `rhi/MemoryStats.h` for §13.4 `GetMemoryStats`/`GetMemoryHeapBudgets`. Minor — can be in `rhi/Device.h`. |

**Action**: Add `MemoryStats` / `MemoryHeapBudget` structs to module table or note they live in `rhi/Device.h`.

---

### 21.3 §3 Handle System

| Item                                                             | Verdict | Notes                                                                                                                                    |
| ---------------------------------------------------------------- | :-----: | ---------------------------------------------------------------------------------------------------------------------------------------- |
| 64-bit typed handle layout `[gen:16\|idx:32\|type:8\|backend:8]` |   ✅    | Industry standard (The Forge, Diligent Engine). 16-bit generation gives 65536 reuse cycles before wrap — sufficient.                     |
| Handle types coverage                                            |   ⚠️    | Missing `DescriptorLayoutHandle` in the handle list but it appears in §6.2 `CreateDescriptorLayout`. Also missing `DescriptorSetHandle`. |
| HandlePool (§3.3)                                                |   ✅    | O(1) create/destroy/lookup. Fixed-size pool. No heap alloc after init.                                                                   |

**Action**: Add `DescriptorLayoutHandle` and `DescriptorSetHandle` to the §3.1 handle type list.

---

### 21.4 §4 Device & Capability System

| Item                   | Verdict | Notes                                                                                                                                                                                                                                                                                                                                   |
| ---------------------- | :-----: | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `DeviceDesc`           |   ✅    | Backend selection, adapter index, validation, capture flags. Complete.                                                                                                                                                                                                                                                                  |
| `GpuCapabilityProfile` |   ⚠️    | Missing `hasWorkGraphs` (D3D12 SM 6.8, §18.2 references it). Missing `hasCooperativeMatrix` (§18.1 lists it). Missing `hasHardwareDecompression` — wait, it IS present (L361). OK.                                                                                                                                                      |
| Tier detection logic   |   ✅    | Matches `rendering-pipeline-architecture.md` §1.3. T1 requires mesh shader + ray query + descriptor buffer/heap + timeline semaphore.                                                                                                                                                                                                   |
| `DescriptorModel` enum |   ⚠️    | References `DescriptorHeap` as a variant but `VK_EXT_descriptor_heap` was just published (2026-01-23 Khronos blog). The RHI correctly lists `descriptor_buffer` as current primary and `descriptor_heap` as future upgrade — good forward-planning. However, the `DescriptorModel` enum should explicitly list both as separate values. |

**Action**: (1) Add `hasWorkGraphs` bool to `GpuCapabilityProfile`. (2) Ensure `DescriptorModel` enum has both `DescriptorBuffer` and `DescriptorHeap` values.

---

### 21.5 §5 Resource System

| Item                           | Verdict | Notes                                                                                                                                                                            |
| ------------------------------ | :-----: | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `BufferDesc` / `BufferUsage`   |   ✅    | Covers vertex, index, uniform, storage, indirect, transfer, accel struct, BDA, sparse. Complete for all 88 passes.                                                               |
| `TextureDesc` / `TextureUsage` |   ✅    | Covers sampled, storage, color/depth attachment, transfer, input attachment, shading rate, sparse.                                                                               |
| `SamplerDesc`                  |   ✅    | Standard filter/address/LOD/anisotropy/compare.                                                                                                                                  |
| Deferred destruction (§5.4)    |   ✅    | 2-frame latency queue. No ref-counting. Industry standard.                                                                                                                       |
| Sparse binding (§5.5)          |   ✅    | `SparseBindDesc` separated from sync semantics (fixed in prior review). Backend mapping correct. Used by ClusterDAG streaming (§5.6 of pipeline arch) and VSM page management.   |
| Transient aliasing (§5.6)      |   ✅    | `CreateMemoryHeap` + `AliasBufferMemory` / `AliasTextureMemory`. Primitives-only, policy in RenderGraph. Matches `rendering-pipeline-architecture.md` §2.1 "transient aliasing". |
| `MemoryLocation` enum          |   ⚠️    | Missing `GpuOnly_LazilyAllocated` for tiled architectures. Not critical for desktop CAD target, but WebGPU (mobile future) may benefit. Low priority.                            |

**Action**: Consider adding `LazilyAllocated` to `MemoryLocation` for future mobile/WebGPU tile-based GPU support.

---

### 21.6 §6 Descriptor & Binding System

| Item                                          | Verdict | Notes                                                                                                                                                                                                                                  |
| --------------------------------------------- | :-----: | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 4-level binding hierarchy                     |   ✅    | Push Constants (L0), Per-Frame (L1), Per-Material (L2), Bindless (L3). Matches `rendering-pipeline-architecture.md` §1.6 descriptor strategy.                                                                                          |
| `DescriptorLayoutDesc` / `PipelineLayoutDesc` |   ✅    | Max 4 sets, 256B push constants. Sufficient for all 88 passes.                                                                                                                                                                         |
| `DescriptorWrite` using `std::variant`        |   ✅    | Type-safe, self-documenting. Better than flat struct with ambiguous fields.                                                                                                                                                            |
| Backend mapping (§6.5)                        |   ✅    | Vulkan descriptor buffer/heap, D3D12 root signature + descriptor heap, VkCompat descriptor sets, WebGPU bind groups, OpenGL direct bind. Comprehensive.                                                                                |
| Reserved binding convention (§6.5.1)          |   ✅    | Push constant UBO emulation at `@group(0)@binding(0)` for WebGPU/GL. Slang integration contract documented. Debug validation.                                                                                                          |
| `BindlessTable` (§6.6)                        |   ✅    | `RegisterTexture`/`RegisterBuffer`/`Unregister` + `Bind`. Backend mapping covers all 5 backends.                                                                                                                                       |
| **VK_EXT_descriptor_heap transition plan**    |   ⚠️    | §16.1 lists `descriptor_buffer` as current and `descriptor_heap` as future. Good. But the `BindlessTable` backend mapping (§6.6) lists `VK_EXT_descriptor_buffer` for T1 — needs a note about future `descriptor_heap` migration path. |

**`VK_EXT_descriptor_heap` update** (January 2026 Khronos release): The newly published `VK_EXT_descriptor_heap` introduces two distinct heaps (sampler heap + resource heap), replaces `VK_EXT_descriptor_buffer`, and introduces a new "push data" interface replacing both push constants and push descriptors. The RHI currently uses `descriptor_buffer` as primary — this is correct for current drivers. Migration to `descriptor_heap` should be planned as a Phase 6a+ task when driver support matures (expected H2 2026). The abstraction is forward-compatible: `BindlessTable` already encapsulates the descriptor backend, so the transition is internal to T1 Vulkan implementation.

**Action**: Add a migration note to §6.6 or §18.2 about `VK_EXT_descriptor_heap` timeline and internal migration plan.

---

### 21.7 §7 Command Buffer System

| Item                                                | Verdict | Notes                                                                                                                                                                                                                       |
| --------------------------------------------------- | :-----: | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `QueueType` (Graphics/Compute/Transfer)             |   ✅    | Matches `rendering-pipeline-architecture.md` §4.1 (3-queue architecture for T1).                                                                                                                                            |
| Command recording API coverage                      |   ✅    | State binding, draw (indexed, indirect, indirect count, mesh tasks), compute, transfer, barriers, dynamic rendering, dynamic state, VRS, secondary cmd bufs, query, debug labels. **Complete for all 88 passes.**           |
| `CmdDrawMeshTasksIndirectCount`                     |   ✅    | Required for `rendering-pipeline-architecture.md` §4.2 Step 7 (Task→Mesh→VisBuffer). Present.                                                                                                                               |
| `CmdBeginRendering` / `CmdEndRendering`             |   ✅    | Dynamic rendering, no render pass objects. Matches `rendering-pipeline-architecture.md` §2.2 backend table.                                                                                                                 |
| `RenderingDesc::viewMask`                           |   ✅    | Multiview for XR stereo (Pass #69).                                                                                                                                                                                         |
| VRS (`CmdSetShadingRate`, `CmdSetShadingRateImage`) |   ✅    | Pass #59 VRS Image generation. T1 only.                                                                                                                                                                                     |
| `CmdExecuteSecondary`                               |   ✅    | Multi-threaded recording. Vulkan secondary cmd bufs, D3D12 bundles, WebGPU render bundles.                                                                                                                                  |
| Queue management (§7.4)                             |   ✅    | Internal queue family selection. No explicit `GetQueue()`. Timeline semaphores for cross-queue sync.                                                                                                                        |
| **Missing: `CmdDispatchIndirect` for ray tracing**  |   ⚠️    | `rendering-pipeline-architecture.md` Passes #17, #20-#24, #71, #83-#85 use ray query in compute dispatches. The `CmdDispatch` / `CmdDispatchIndirect` cover this. OK — ray query is shader-side, no special command needed. |
| **Missing: `CmdBuildBLAS`/`CmdBuildTLAS` in §7.2**  |   ⚠️    | These are listed in §10 but not in the §7.2 command recording API. They ARE command buffer commands. Should be cross-referenced or included in §7.2 for completeness.                                                       |

**Action**: Add cross-reference note in §7.2 pointing to §10 for BLAS/TLAS build commands.

---

### 21.8 §8 Pipeline System

| Item                                           | Verdict | Notes                                                                                                                                                                                                                                                                                    |
| ---------------------------------------------- | :-----: | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `GraphicsPipelineDesc`                         |   ✅    | Vertex shader, fragment shader, task shader, mesh shader, vertex input, topology, rasterizer, depth/stencil, blend, render target formats, pipeline layout, specialization constants, multiview. **Complete.** Matches `rendering-pipeline-architecture.md` §1.5 `GraphicsPipelineDesc`. |
| `IsMeshShaderPipeline()`                       |   ✅    | Used to skip vertex input when mesh shader path active. Matches T1 geometry path.                                                                                                                                                                                                        |
| `ComputePipelineDesc`                          |   ✅    | Used by 30+ compute passes in the 88-pass pipeline.                                                                                                                                                                                                                                      |
| `RayTracingPipelineDesc`                       |   ✅    | T1 only. Pass #23 Path Tracer.                                                                                                                                                                                                                                                           |
| Pipeline cache (§8.4)                          |   ✅    | Disk-persistent. Matches `rendering-pipeline-architecture.md` §1.5 "All PSOs pre-built at init".                                                                                                                                                                                         |
| Pipeline library / split compilation (§8.5)    |   ✅    | `VK_EXT_graphics_pipeline_library` / D3D12 Pipeline State Streams. 10-100x PSO creation speedup. Critical for CAD with 1000s of materials.                                                                                                                                               |
| **Missing: `PolygonMode::Line` for wireframe** |   ⚠️    | `rendering-pipeline-architecture.md` §5.9.2 has Wireframe/HLR/Pen display styles requiring `PolygonMode::Line`. The enum is referenced in `GraphicsPipelineDesc::polygonMode` but not defined. Ensure `PolygonMode::Fill`, `Line`, `Point` are in `RhiEnums.h`.                          |

**Action**: Verify `PolygonMode` enum includes `Fill`, `Line`, `Point` values.

---

### 21.9 §9 Synchronization Primitives

| Item                                                  | Verdict | Notes                                                                                                                                                                                                                                                                                                                                                                     |
| ----------------------------------------------------- | :-----: | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Fence (CPU-GPU)                                       |   ✅    | Standard create/wait/reset/status.                                                                                                                                                                                                                                                                                                                                        |
| Timeline semaphore (GPU-GPU)                          |   ✅    | Required for T1 async compute. `rendering-pipeline-architecture.md` §4.1 shows async compute queue with timeline semaphore.                                                                                                                                                                                                                                               |
| Binary semaphore                                      |   ✅    | Swapchain acquire/present.                                                                                                                                                                                                                                                                                                                                                |
| Barrier model (§9.3)                                  |   ✅    | `BufferBarrierDesc`, `TextureBarrierDesc`, `PipelineBarrierDesc`. Stage + access + layout + queue transfer.                                                                                                                                                                                                                                                               |
| D3D12 Enhanced Barriers mapping (§9.4)                |   ✅    | Vulkan-centric RHI barrier model mapped to `D3D12_BARRIER_*` types. Fallback to legacy `ResourceBarrier` documented.                                                                                                                                                                                                                                                      |
| **Barrier model is Vulkan-centric**                   |   ⚠️    | The stage+access model maps naturally to `vkCmdPipelineBarrier2`. D3D12 Enhanced Barriers use a layout-based model — the mapping table (§9.4) handles this, but the D3D12 backend has extra translation work. This is acceptable (Thin Abstraction from RenderGraph's perspective), but document the D3D12 translation cost (~1-2ns per barrier, negligible vs GPU cost). |
| **Missing: `Event` (fine-grained GPU split barrier)** |   ⚠️    | §2.3 Module Decomposition mentions `Event` in Synchronization module but no API is defined. Vulkan `VkEvent` and D3D12 split barriers can improve barrier granularity. Low priority — `PipelineBarrier` is sufficient for RenderGraph.                                                                                                                                    |

**Action**: Either define `Event` API or remove from §2.3 module list.

---

### 21.10 §10 Acceleration Structure

| Item                                              | Verdict | Notes                                                                                                                                                                                                                                                                                                                             |
| ------------------------------------------------- | :-----: | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `AccelStructGeometryDesc`                         |   ✅    | Triangles, AABBs. Vertex buffer + index buffer + transform.                                                                                                                                                                                                                                                                       |
| `BLASDesc` / `TLASDesc`                           |   ✅    | Build flags (fast trace, fast build, allow update).                                                                                                                                                                                                                                                                               |
| `CmdBuildBLAS` / `CmdBuildTLAS` / `CmdUpdateBLAS` |   ✅    | Matches `rendering-pipeline-architecture.md` §4.2 Step 2 (BLAS refit/rebuild + TLAS rebuild).                                                                                                                                                                                                                                     |
| **Missing: scratch buffer size query**            |   ❌    | No `GetAccelStructBuildSizes(BLASDesc) -> AccelStructBuildSizes` API. Both Vulkan (`vkGetAccelerationStructureBuildSizesKHR`) and D3D12 (`GetRaytracingAccelerationStructurePrebuildInfo`) require pre-querying scratch/result buffer sizes before building. Without this, the caller cannot allocate the correct scratch buffer. |
| **Missing: `AccelStructInstance` definition**     |   ⚠️    | `TLASDesc::instanceBuffer` references "Array of AccelStructInstance (64B each)" but the struct is not defined. Should define `AccelStructInstance` (transform 3x4 + instanceId + mask + SBT offset + flags).                                                                                                                      |

**Action**: (1) **Add `GetAccelStructBuildSizes()` API** — this is a blocking omission. (2) Define `AccelStructInstance` struct.

---

### 21.11 §11 Swapchain & Multi-Window

| Item                              | Verdict | Notes                                                                                                    |
| --------------------------------- | :-----: | -------------------------------------------------------------------------------------------------------- |
| `SwapchainDesc`                   |   ✅    | Surface, size, format, present mode, image count, HDR.                                                   |
| `AcquireNextImage` / `Present`    |   ✅    | Semaphore-based.                                                                                         |
| `ResizeSwapchain`                 |   ✅    | Needed for window resize.                                                                                |
| Multi-window architecture (§11.2) |   ✅    | 3-subsystem design (WindowManager, SurfaceManager, shared Device). Matches `specs/01-window-manager.md`. |

**Overall §11 verdict**: ✅ **PASS**.

---

### 21.12 §12 Shader Module

| Item                                                      | Verdict | Notes                                                                                                                                                                                                                   |
| --------------------------------------------------------- | :-----: | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ShaderStage` flags                                       |   ✅    | Vertex, Fragment, Compute, Task, Mesh, Ray Gen/AnyHit/ClosestHit/Miss/Intersection/Callable. Complete.                                                                                                                  |
| `ShaderModuleDesc`                                        |   ✅    | Stage + code blob + entry point + debug name. IR-agnostic (SPIR-V/DXIL/GLSL/WGSL).                                                                                                                                      |
| **Missing: specialization constants in ShaderModuleDesc** |   ⚠️    | Specialization constants are in `GraphicsPipelineDesc` (§8.1) and `ComputePipelineDesc` (§8.2) — this is the correct location (Vulkan spec applies specialization at pipeline creation, not module creation). No issue. |

**Overall §12 verdict**: ✅ **PASS**.

---

### 21.13 §13 Memory Management Strategy

| Item                                              | Verdict | Notes                                                                                                 |
| ------------------------------------------------- | :-----: | ----------------------------------------------------------------------------------------------------- |
| Allocator architecture                            |   ✅    | VMA (Vulkan), D3D12MA (D3D12), API-managed (WebGPU/GL). Industry standard.                            |
| `StagingRing` (§13.2)                             |   ✅    | CPU→GPU ring buffer. 64MB default. Persistent-mapped. Used for SceneBuffer upload, texture streaming. |
| `ReadbackRing` (§13.3)                            |   ✅    | GPU→CPU symmetric ring. 4MB default. Used by GpuProfiler, ShaderPrintf, GpuBreadcrumbs, Telemetry.    |
| Transient resource pool (§13.4)                   |   ✅    | RenderGraph-driven aliasing via shared heaps. 30-50% VRAM savings.                                    |
| `GetMemoryStats` / `GetMemoryHeapBudgets` (§13.4) |   ✅    | Added in prior review. Backend mapping complete.                                                      |
| **Duplicate §13.4 numbering**                     |   ⚠️    | Two subsections numbered §13.4 (Transient Resource Pool and Memory Statistics API). Renumber.         |

**Action**: Renumber Memory Statistics API to §13.5.

---

### 21.14 §14 Error Handling

| Item                         | Verdict | Notes                                                                                                                                                                                          |
| ---------------------------- | :-----: | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `std::expected<T, RhiError>` |   ✅    | C++23 error handling. No exceptions in hot paths.                                                                                                                                              |
| `RhiError` enum coverage     |   ✅    | OutOfMemory (device/host), DeviceLost, SurfaceLost, FormatNotSupported, FeatureNotSupported, InvalidHandle, InvalidParameter, ShaderCompilationFailed, PipelineCreationFailed, TooManyObjects. |
| DeviceLost recovery          |   ✅    | Triggers graceful re-creation (§1.4 protocol).                                                                                                                                                 |

**Overall §14 verdict**: ✅ **PASS**.

---

### 21.15 §15 Debug & Profiling Integration

| Item                   | Verdict | Notes                                                                                |
| ---------------------- | :-----: | ------------------------------------------------------------------------------------ |
| Object naming (§15.1)  |   ✅    | `debugName` on all Desc structs. Backend mapping complete.                           |
| Debug labels (§15.2)   |   ✅    | `CmdBeginDebugLabel`/`CmdEndDebugLabel`/`CmdInsertDebugLabel`. All 5 backends.       |
| GPU timestamps (§15.3) |   ✅    | `CreateQueryPool` + `GetQueryResults` + `GetTimestampPeriod`. Used by `GpuProfiler`. |

**Overall §15 verdict**: ✅ **PASS**.

---

### 21.16 §16 Per-Tier Optimization Paths

| Item                                                                  | Verdict | Notes                                                                                                                                                                                                                                |
| --------------------------------------------------------------------- | :-----: | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| T1 Vulkan (§16.1)                                                     |   ✅    | Descriptor buffer, mesh shader, ray query, timeline semaphore, dedicated transfer, BDA, VRS, sparse binding, subgroup ops. Complete.                                                                                                 |
| T1 D3D12 (§16.2)                                                      |   ✅    | Root signature, mesh shader, DXR 1.1, enhanced barriers, work graphs (future).                                                                                                                                                       |
| T2 Vulkan Compat (§16.3)                                              |   ✅    | Traditional descriptor sets, vertex+MDI, legacy render pass, single queue.                                                                                                                                                           |
| T3 WebGPU (§16.4)                                                     |   ✅    | Bind groups, 128MB SSBO limit, no MDI, single queue, WGSL.                                                                                                                                                                           |
| T4 OpenGL (§16.5)                                                     |   ✅    | DSA, MDI, UBO/SSBO, FBO, coarse barriers.                                                                                                                                                                                            |
| **T1 Vulkan: `VK_EXT_descriptor_buffer` vs `VK_EXT_descriptor_heap`** |   ⚠️    | As noted in §21.6, `descriptor_heap` was published Jan 2026. Current plan (`descriptor_buffer` primary, `descriptor_heap` future) is correct. Update §16.1 to note `descriptor_heap` as planned upgrade when driver support matures. |

**Action**: Update §16.1 descriptor binding row to mention `VK_EXT_descriptor_heap` migration timeline.

---

### 21.17 §17 Thread Safety Model

| Item                                | Verdict | Notes                                                                                                                                                                                                                   |
| ----------------------------------- | :-----: | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Per-cmd-buf single-thread recording |   ✅    | Lock-free. Industry standard (Vulkan spec requirement).                                                                                                                                                                 |
| Thread-safe resource creation       |   ✅    | Internal mutex on handle pool.                                                                                                                                                                                          |
| Thread-safe submission              |   ✅    | Queue lock.                                                                                                                                                                                                             |
| Thread-safe pipeline creation       |   ✅    | Pipeline cache has internal lock.                                                                                                                                                                                       |
| `StagingRing` threading             |   ⚠️    | §17 says "Per-thread ring or lock-free ring" but §13.2 `StagingRing` shows a single ring with no thread safety discussion. Clarify: if per-thread, document the per-thread pool; if shared, add atomic bump allocation. |

**Action**: Clarify `StagingRing` thread safety model in §13.2.

---

### 21.18 §17.1 Implementation Suggestions

| Item                                     | Verdict | Notes                                                                                           |
| ---------------------------------------- | :-----: | ----------------------------------------------------------------------------------------------- |
| CRTP compile-time optimization (§17.1.1) |   ✅    | `.h` declarations + `.inl` implementation + explicit instantiation in `.cpp`. Standard pattern. |
| Handle pool mutex (§17.1.2)              |   ✅    | `std::mutex` for non-hot-path. ABA problem correctly identified and avoided.                    |
| Command buffer validation (§17.1.3)      |   ✅    | Debug-only O(1) asserts. Compiles out in release.                                               |

**Overall §17.1 verdict**: ✅ **PASS**.

---

### 21.19 §18 Extension & Future-Proofing

| Item                                            | Verdict | Notes                                                                                                                                                                                                                                                       |
| ----------------------------------------------- | :-----: | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Vulkan extension negotiation                    |   ✅    | Comprehensive list including descriptor buffer/heap, mesh shader, ray query, VRS, cooperative matrix, GDeflate, pipeline library.                                                                                                                           |
| `CmdDecompressBuffer` preview                   |   ✅    | GDeflate HW decode for cluster streaming. Matches `rendering-pipeline-architecture.md` §5.8.1 GPU decompression acceleration.                                                                                                                               |
| Future extensions table                         |   ✅    | Descriptor heap, work graphs, cooperative matrix, GDeflate, fence barriers.                                                                                                                                                                                 |
| **Missing: `VK_EXT_device_generated_commands`** |   ⚠️    | `rendering-pipeline-architecture.md` §3 mentions "DGC / Work Graphs (roadmap Phase 20)". The RHI §18.2 mentions D3D12 work graphs but not Vulkan DGC (`VK_EXT_device_generated_commands`). This is the Vulkan equivalent for GPU-driven command generation. |

**Action**: Add `VK_EXT_device_generated_commands` to §18.1 extension list and §18.2 future table.

---

### 21.20 §19 Naming Conventions

✅ **PASS**. Consistent PascalCase. Namespace `miki::rhi`. Backend sub-namespaces. Standard.

---

### 21.21 §20 Design Decisions Summary

✅ **PASS**. All 16 decisions are well-justified with alternatives and rationale. Updated in prior review with CommandListHandle, GetMemoryStats, Reserved Binding, SparseBindDesc decisions.

---

### 21.22 Appendix A & B

| Item                  | Verdict | Notes                                                                                                                                                                             |
| --------------------- | :-----: | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Format table          |   ⚠️    | Missing `R32G32_UINT` — this is the VisBuffer format (`rendering-pipeline-architecture.md` §5.4). Also missing `R16_SFLOAT` (used for AO output), `RG16_SFLOAT` (motion vectors). |
| `PipelineStage` flags |   ✅    | Covers all stages including Task/Mesh shader, AccelStruct build, RT shader, shading rate image.                                                                                   |
| `AccessFlags`         |   ✅    | Complete for all usage patterns.                                                                                                                                                  |
| `TextureLayout`       |   ✅    | Covers Undefined, General, ColorAttachment, DepthStencil, ShaderReadOnly, Transfer, Present, ShadingRate.                                                                         |

**Action**: Add `R32G32_UINT`, `R16_SFLOAT`, `RG16_SFLOAT` to Format table.

---

## 21.23 Cross-Document Consistency Check

| Pipeline Arch Requirement                     | RHI Coverage                                                           | Verdict |
| --------------------------------------------- | ---------------------------------------------------------------------- | :-----: |
| VisBuffer R32G32_UINT                         | §5.2 TextureUsage::Storage + imageStore                                |   ✅    |
| BDA (Buffer Device Address)                   | §5.1 `GetBufferDeviceAddress`, §5.1 `ShaderDeviceAddress` usage flag   |   ✅    |
| 3-bucket macro-binning (indirect args)        | §7.2 `CmdDrawMeshTasksIndirectCount`, `CmdDrawIndexedIndirectCount`    |   ✅    |
| VSM sparse page management                    | §5.5 Sparse Binding                                                    |   ✅    |
| ClusterDAG streaming                          | §5.5 Sparse Binding + §18 GDeflate decompression                       |   ✅    |
| HiZ mip pyramid compute                       | §7.2 `CmdDispatch` + §5.2 texture mip chain                            |   ✅    |
| LL-OIT linked list (per-pixel atomic)         | §5.1 Storage buffer + §7.2 compute dispatch                            |   ✅    |
| RT ray query (Passes #17,#20-#24,#71,#83-#85) | §10 Acceleration Structure + §8.3 RayTracingPipelineDesc               |   ✅    |
| Timeline semaphore async compute              | §9.2 Timeline semaphore + §7.4 Submit with semaphores                  |   ✅    |
| Multi-window GPU sharing                      | §11.2 Single shared Device                                             |   ✅    |
| DisplayStyle wireframe/HLR                    | §8.1 `PolygonMode` + §7.2 `CmdSetLineWidth`                            |   ✅    |
| Section plane stencil                         | §8.1 `StencilOpState` + §7.2 `CmdSetStencilReference`                  |   ✅    |
| XR stereo multiview                           | §7.2 `RenderingDesc::viewMask` + §8.1 `GraphicsPipelineDesc::viewMask` |   ✅    |
| HDR swapchain                                 | §11.1 `SwapchainDesc::hdr`                                             |   ✅    |
| MSDF text rendering (PMI)                     | §5.2 SampledTexture + §8.1 graphics pipeline                           |   ✅    |
| Offscreen hi-res render (Pass #66)            | §5.2 large TextureDesc + §11.1 without swapchain                       |   ✅    |

---

## 21.24 Audit Summary

| Severity               |  Count  | Items                                          |
| ---------------------- | :-----: | ---------------------------------------------- |
| ❌ FAIL (blocking)     |  **1**  | Missing `GetAccelStructBuildSizes()` API (§10) |
| ⚠️ WARN (non-blocking) | **12**  | See action items below                         |
| ✅ PASS                | **~50** | All other items                                |

### Required Actions (ordered by priority)

| #   | Severity | Section | Action                                                                                                                            |
| --- | :------: | ------- | --------------------------------------------------------------------------------------------------------------------------------- |
| 1   |    ❌    | §10     | Add `GetAccelStructBuildSizes(BLASDesc/TLASDesc) -> AccelStructBuildSizes` API (scratch size + result size + update scratch size) |
| 2   |    ⚠️    | §10     | Define `AccelStructInstance` struct (transform 3x4 + instanceCustomIndex + mask + SBTRecordOffset + flags)                        |
| 3   |    ⚠️    | §3.1    | Add `DescriptorLayoutHandle` and `DescriptorSetHandle` to handle type list                                                        |
| 4   |    ⚠️    | §4.2    | Add `hasWorkGraphs` to `GpuCapabilityProfile`                                                                                     |
| 5   |    ⚠️    | §7.2    | Add cross-reference to §10 for BLAS/TLAS build commands                                                                           |
| 6   |    ⚠️    | §9      | Define `Event` API or remove from §2.3 module list                                                                                |
| 7   |    ⚠️    | §13     | Fix duplicate §13.4 numbering (renumber Memory Statistics to §13.5)                                                               |
| 8   |    ⚠️    | §13.2   | Clarify `StagingRing` thread safety model                                                                                         |
| 9   |    ⚠️    | §16.1   | Update descriptor binding row to note `VK_EXT_descriptor_heap` migration                                                          |
| 10  |    ⚠️    | §18     | Add `VK_EXT_device_generated_commands` to extension list                                                                          |
| 11  |    ⚠️    | App A   | Add missing formats: `R32G32_UINT`, `R16_SFLOAT`, `RG16_SFLOAT`                                                                   |
| 12  |    ⚠️    | §2.3    | Note `MemoryStats`/`MemoryHeapBudget` location in module table                                                                    |
| 13  |    ⚠️    | §5.1    | Consider `LazilyAllocated` memory location for future mobile support                                                              |

### Overall Assessment

**Rating: ★★★★☆ (4/5)**

The RHI design is architecturally sound and well-aligned with the target rendering pipeline. The CRTP + dispatch-once facade pattern is the correct choice for 2026. All 88 pipeline passes are supportable with the current API surface. The single blocking issue (`GetAccelStructBuildSizes`) is a straightforward API omission, not a design flaw. The 12 warnings are minor documentation gaps and forward-planning items, none requiring architectural changes.
