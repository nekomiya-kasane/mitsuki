/** @file WebGPUDevice.h
 *  @brief WebGPU / Dawn (Tier 3) backend device.
 *
 *  Conditionally included by AllBackends.h only when MIKI_BUILD_WEBGPU=1.
 *  Uses Dawn's webgpu.h C API (chromium/7187+ prebuilt or Emscripten emdawnwebgpu).
 *  API-managed memory (wgpuDeviceCreateBuffer / wgpuDeviceCreateTexture).
 *  Push constants emulated via 256B UBO at @group(0) @binding(0) per spec section 6.5.1.
 *  Deferred command recording: commands recorded into WGPUCommandEncoder, submitted at Submit.
 *  Single queue (no async compute on T3).
 *
 *  Resource storage uses typed HandlePool (O(1) alloc/free/lookup, generation-safe).
 *  Namespace: miki::rhi
 */
#pragma once

#include "miki/rhi/backend/BackendStub.h"
#include "miki/rhi/adaptation/ShadowBuffer.h"

#include <dawn/webgpu.h>

#include <memory>
#include <string>
#include <vector>

namespace miki::rhi {

    // =========================================================================
    // Per-resource backend payloads (stored in HandlePool slots)
    // =========================================================================

    struct WGPUBufferData {
        WGPUBuffer buffer = nullptr;
        uint64_t size = 0;
        void* mappedPtr = nullptr;
        BufferUsage usage{};
        WGPUBufferUsage wgpuUsage = 0;
        // Adaptation: ShadowBuffer strategy (§20b Feature::BufferMapWriteWithUsage)
        // When CpuToGpu buffer has Vertex/Index/Uniform/Storage usage, WebGPU forbids
        // MapWrite combined with those usages. Instead we create a CopyDst GPU buffer +
        // CPU shadow. Map returns shadow ptr; Unmap flushes via wgpuQueueWriteBuffer.
        adaptation::ShadowBuffer shadow;
    };

    struct WGPUTextureData {
        WGPUTexture texture = nullptr;
        WGPUTextureDimension dimension = WGPUTextureDimension_2D;
        WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
        uint32_t width = 0, height = 0, depthOrArrayLayers = 0;
        uint32_t mipLevels = 1;
        uint32_t sampleCount = 1;
        bool ownsTexture = true;  // false for swapchain-acquired textures
    };

    struct WGPUTextureViewData {
        WGPUTextureView view = nullptr;
        TextureHandle parentTexture;
    };

    struct WGPUSamplerData {
        WGPUSampler sampler = nullptr;
    };

    struct WGPUShaderModuleData {
        WGPUShaderModule module = nullptr;
        ShaderStage stage = ShaderStage::Vertex;
        std::string entryPoint = "main";
        std::string wgslSource;  // Kept for debug / hot-reload
    };

    struct WGPUFenceData {
        // WebGPU has no explicit fence object; emulated via onSubmittedWorkDone callback
        bool signaled = false;
        uint64_t submittedSerial = 0;
    };

    struct WGPUSemaphoreData {
        // WebGPU has no GPU-GPU semaphores; single queue makes them unnecessary
        uint64_t value = 0;
        SemaphoreType type = SemaphoreType::Binary;
    };

    struct WGPUPipelineData {
        WGPURenderPipeline renderPipeline = nullptr;
        WGPUComputePipeline computePipeline = nullptr;
        bool isCompute = false;
        // Cached vertex input state for CmdBindVertexBuffer stride lookup
        struct VertexBinding {
            uint32_t binding = 0;
            uint32_t stride = 0;
            bool perInstance = false;
        };
        std::vector<VertexBinding> vertexBindings;
        // Stencil state cached for CmdSetStencilReference
        WGPUCompareFunction stencilFrontCompare = WGPUCompareFunction_Always;
        WGPUCompareFunction stencilBackCompare = WGPUCompareFunction_Always;
        uint32_t stencilReadMask = 0xFF;
        uint32_t stencilWriteMask = 0xFF;
    };

    struct WGPUPipelineLayoutData {
        WGPUPipelineLayout layout = nullptr;
        uint32_t pushConstantSize = 0;
        std::vector<DescriptorLayoutHandle> setLayouts;
    };

    struct WGPUDescriptorLayoutData {
        WGPUBindGroupLayout bindGroupLayout = nullptr;
        struct BindingInfo {
            uint32_t binding = 0;
            BindingType type{};
            uint32_t count = 1;
            ShaderStage stages{};
        };
        std::vector<BindingInfo> bindings;
    };

    struct WGPUDescriptorSetData {
        WGPUBindGroup bindGroup = nullptr;
        DescriptorLayoutHandle layout;
        // Cached writes for recreation on update
        struct BoundResource {
            uint32_t binding = 0;
            BindingType type{};
            WGPUBuffer buffer = nullptr;
            uint64_t offset = 0;
            uint64_t size = 0;
            WGPUTextureView textureView = nullptr;
            WGPUSampler sampler = nullptr;
        };
        std::vector<BoundResource> resources;
    };

    struct WGPUPipelineCacheData {
        std::vector<uint8_t> blob;  // No native pipeline cache in WebGPU
    };

    struct WGPUQueryPoolData {
        WGPUQuerySet querySet = nullptr;
        QueryType type = QueryType::Timestamp;
        uint32_t count = 0;
        WGPUBuffer resolveBuffer = nullptr;   // For timestamp readback
        WGPUBuffer readbackBuffer = nullptr;  // MAP_READ staging buffer
    };

    struct WGPUSwapchainData {
        WGPUSurface surface = nullptr;
        WGPUTexture currentTexture = nullptr;
        WGPUTextureView currentView = nullptr;
        WGPUTextureFormat format = WGPUTextureFormat_BGRA8Unorm;
        WGPUPresentMode presentMode = WGPUPresentMode_Fifo;
        uint32_t width = 0, height = 0;
        TextureHandle colorTexture;          // Wrapper handle for current back buffer
        TextureViewHandle colorTextureView;  // Pre-created view for the color texture
        uint32_t currentImage = 0;
    };

    struct WGPUCommandBufferData {
        QueueType queueType = QueueType::Graphics;
        bool isSecondary = false;
        WGPUCommandEncoder encoder = nullptr;
        WGPUCommandBuffer finishedBuffer = nullptr;  // Set by EndImpl, consumed by SubmitImpl
    };

    struct WGPUCommandPoolData {
        QueueType queueType = QueueType::Graphics;
        // Cached C++ wrappers + handles for pool-reset reuse (spec §19)
        struct CachedEntry {
            CommandBufferHandle bufHandle;
            std::unique_ptr<WebGPUCommandBuffer> wrapper;
        };
        std::vector<CachedEntry> cachedEntries;
        uint32_t nextFreeIndex = 0;
    };

    // =========================================================================
    // WebGPU device description
    // =========================================================================

    struct WebGPUDeviceDesc {
        bool enableValidation = true;
        WGPUInstance instance = nullptr;  // Optional: caller-provided instance
        WGPUAdapter adapter = nullptr;    // Optional: caller-provided adapter
        WGPUSurface surface = nullptr;    // Optional: for swapchain creation
    };

    // =========================================================================
    // WebGPUDevice — Tier 3 backend
    // =========================================================================

    class WebGPUDevice : public DeviceBase<WebGPUDevice> {
       public:
        WebGPUDevice();
        ~WebGPUDevice();

        WebGPUDevice(const WebGPUDevice&) = delete;
        auto operator=(const WebGPUDevice&) -> WebGPUDevice& = delete;
        WebGPUDevice(WebGPUDevice&&) = delete;
        auto operator=(WebGPUDevice&&) -> WebGPUDevice& = delete;

        [[nodiscard]] auto Init(const WebGPUDeviceDesc& desc = {}) -> RhiResult<void>;

        // -- Native accessors --
        [[nodiscard]] auto GetWGPUDevice() const noexcept -> WGPUDevice { return device_; }
        [[nodiscard]] auto GetWGPUQueue() const noexcept -> WGPUQueue { return queue_; }
        [[nodiscard]] auto GetWGPUInstance() const noexcept -> WGPUInstance { return instance_; }

        // -- Compile-time backend identity (for if constexpr adaptation paths) --
        static constexpr BackendType kBackendType = BackendType::WebGPU;

        // -- Capability --
        auto GetBackendTypeImpl() const -> BackendType { return kBackendType; }
        auto GetCapabilitiesImpl() const -> const GpuCapabilityProfile& { return capabilities_; }
        auto GetQueueTimelinesImpl() const -> QueueTimelines { return {}; }

        // -- Swapchain (WebGPUSwapchain.cpp) --
        auto CreateSwapchainImpl(const SwapchainDesc& desc) -> RhiResult<SwapchainHandle>;
        void DestroySwapchainImpl(SwapchainHandle h);
        auto ResizeSwapchainImpl(SwapchainHandle h, uint32_t w, uint32_t ht) -> RhiResult<void>;
        auto AcquireNextImageImpl(SwapchainHandle h, SemaphoreHandle signal, FenceHandle fence) -> RhiResult<uint32_t>;
        auto GetSwapchainTextureImpl(SwapchainHandle h, uint32_t imageIndex) -> TextureHandle;
        auto GetSwapchainTextureViewImpl(SwapchainHandle h, uint32_t imageIndex) -> TextureViewHandle;
        auto GetSwapchainImageCountImpl(SwapchainHandle h) -> uint32_t;
        void PresentImpl(SwapchainHandle h, std::span<const SemaphoreHandle> waitSemaphores);

        // -- Sync (WebGPUDevice.cpp) --
        auto CreateFenceImpl(bool signaled) -> RhiResult<FenceHandle>;
        void DestroyFenceImpl(FenceHandle h);
        void WaitFenceImpl(FenceHandle h, uint64_t timeout);
        void ResetFenceImpl(FenceHandle h);
        auto GetFenceStatusImpl(FenceHandle h) -> bool;

        auto CreateSemaphoreImpl(const SemaphoreDesc& desc) -> RhiResult<SemaphoreHandle>;
        void DestroySemaphoreImpl(SemaphoreHandle h);
        void SignalSemaphoreImpl(SemaphoreHandle h, uint64_t value);
        void WaitSemaphoreImpl(SemaphoreHandle h, uint64_t value, uint64_t timeout);
        auto GetSemaphoreValueImpl(SemaphoreHandle h) -> uint64_t;

        void WaitIdleImpl();
        void SubmitImpl(QueueType queue, const SubmitDesc& desc);

        // -- Resources (WebGPUResources.cpp) --
        auto CreateBufferImpl(const BufferDesc& desc) -> RhiResult<BufferHandle>;
        void DestroyBufferImpl(BufferHandle h);
        auto MapBufferImpl(BufferHandle h) -> RhiResult<void*>;
        void UnmapBufferImpl(BufferHandle h);
        void FlushMappedRangeImpl(BufferHandle h, uint64_t offset, uint64_t size);
        void InvalidateMappedRangeImpl(BufferHandle h, uint64_t offset, uint64_t size);
        auto GetBufferDeviceAddressImpl(BufferHandle h) -> uint64_t;

        auto CreateTextureImpl(const TextureDesc& desc) -> RhiResult<TextureHandle>;
        auto CreateTextureViewImpl(const TextureViewDesc& desc) -> RhiResult<TextureViewHandle>;
        auto GetTextureViewTextureImpl(TextureViewHandle h) -> TextureHandle;
        void DestroyTextureViewImpl(TextureViewHandle h);
        void DestroyTextureImpl(TextureHandle h);

        auto CreateSamplerImpl(const SamplerDesc& desc) -> RhiResult<SamplerHandle>;
        void DestroySamplerImpl(SamplerHandle h);

        // -- Memory aliasing (WebGPUResources.cpp) --
        auto CreateMemoryHeapImpl(const MemoryHeapDesc& desc) -> RhiResult<DeviceMemoryHandle>;
        void DestroyMemoryHeapImpl(DeviceMemoryHandle h);
        void AliasBufferMemoryImpl(BufferHandle buf, DeviceMemoryHandle heap, uint64_t offset);
        void AliasTextureMemoryImpl(TextureHandle tex, DeviceMemoryHandle heap, uint64_t offset);
        auto GetBufferMemoryRequirementsImpl(BufferHandle h) -> MemoryRequirements;
        auto GetTextureMemoryRequirementsImpl(TextureHandle h) -> MemoryRequirements;

        // -- Sparse binding (not available on T3) --
        auto GetSparsePageSizeImpl() const -> SparsePageSize;
        void SubmitSparseBindsImpl(
            QueueType queue, const SparseBindDesc& binds, std::span<const SemaphoreSubmitInfo> wait,
            std::span<const SemaphoreSubmitInfo> signal
        );

        // -- Shader (WebGPUResources.cpp) --
        auto CreateShaderModuleImpl(const ShaderModuleDesc& desc) -> RhiResult<ShaderModuleHandle>;
        void DestroyShaderModuleImpl(ShaderModuleHandle h);

        // -- Descriptors (WebGPUDescriptors.cpp) --
        auto CreateDescriptorLayoutImpl(const DescriptorLayoutDesc& desc) -> RhiResult<DescriptorLayoutHandle>;
        void DestroyDescriptorLayoutImpl(DescriptorLayoutHandle h);
        auto CreatePipelineLayoutImpl(const PipelineLayoutDesc& desc) -> RhiResult<PipelineLayoutHandle>;
        void DestroyPipelineLayoutImpl(PipelineLayoutHandle h);
        auto CreateDescriptorSetImpl(const DescriptorSetDesc& desc) -> RhiResult<DescriptorSetHandle>;
        void UpdateDescriptorSetImpl(DescriptorSetHandle h, std::span<const DescriptorWrite> writes);
        void DestroyDescriptorSetImpl(DescriptorSetHandle h);

        // -- Pipelines (WebGPUPipelines.cpp) --
        auto CreateGraphicsPipelineImpl(const GraphicsPipelineDesc& desc) -> RhiResult<PipelineHandle>;
        auto CreateComputePipelineImpl(const ComputePipelineDesc& desc) -> RhiResult<PipelineHandle>;
        auto CreateRayTracingPipelineImpl(const RayTracingPipelineDesc& desc) -> RhiResult<PipelineHandle>;
        void DestroyPipelineImpl(PipelineHandle h);

        auto CreatePipelineCacheImpl(std::span<const uint8_t> initialData) -> RhiResult<PipelineCacheHandle>;
        auto GetPipelineCacheDataImpl(PipelineCacheHandle h) -> std::vector<uint8_t>;
        void DestroyPipelineCacheImpl(PipelineCacheHandle h);

        auto CreatePipelineLibraryPartImpl(const PipelineLibraryPartDesc& desc) -> RhiResult<PipelineLibraryPartHandle>;
        auto LinkGraphicsPipelineImpl(const LinkedPipelineDesc& desc) -> RhiResult<PipelineHandle>;

        // -- Command pools §19 (WebGPUQuery.cpp) --
        auto CreateCommandPoolImpl(const CommandPoolDesc& desc) -> RhiResult<CommandPoolHandle>;
        void DestroyCommandPoolImpl(CommandPoolHandle h);
        void ResetCommandPoolImpl(CommandPoolHandle h, CommandPoolResetFlags flags);
        auto AllocateFromPoolImpl(CommandPoolHandle pool, bool secondary) -> RhiResult<CommandListAcquisition>;
        void FreeFromPoolImpl(CommandPoolHandle pool, const CommandListAcquisition& acq);

        // -- Query (WebGPUQuery.cpp) --
        auto CreateQueryPoolImpl(const QueryPoolDesc& desc) -> RhiResult<QueryPoolHandle>;
        void DestroyQueryPoolImpl(QueryPoolHandle h);
        auto GetQueryResultsImpl(QueryPoolHandle h, uint32_t first, uint32_t count, std::span<uint64_t> results)
            -> RhiResult<void>;
        auto GetTimestampPeriodImpl() -> double;

        // -- Acceleration structure (not available on T3) --
        auto GetBLASBuildSizesImpl(const BLASDesc& desc) -> AccelStructBuildSizes;
        auto GetTLASBuildSizesImpl(const TLASDesc& desc) -> AccelStructBuildSizes;
        auto CreateBLASImpl(const BLASDesc& desc) -> RhiResult<AccelStructHandle>;
        auto CreateTLASImpl(const TLASDesc& desc) -> RhiResult<AccelStructHandle>;
        void DestroyAccelStructImpl(AccelStructHandle h);

        // -- Memory stats --
        auto GetMemoryStatsImpl() const -> MemoryStats;
        auto GetMemoryHeapBudgetsImpl(std::span<MemoryHeapBudget> out) const -> uint32_t;

        // -- Surface capabilities --
        auto GetSurfaceCapabilitiesImpl(const NativeWindowHandle& window) const -> RenderSurfaceCapabilities;

        // -- HandlePool accessors (for WebGPUCommandBuffer cross-file access) --
        auto GetBufferPool() -> HandlePool<WGPUBufferData, BufferTag, kMaxBuffers>& { return buffers_; }
        auto GetTexturePool() -> HandlePool<WGPUTextureData, TextureTag, kMaxTextures>& { return textures_; }
        auto GetTextureViewPool() -> HandlePool<WGPUTextureViewData, TextureViewTag, kMaxTextureViews>& {
            return textureViews_;
        }
        auto GetSamplerPool() -> HandlePool<WGPUSamplerData, SamplerTag, kMaxSamplers>& { return samplers_; }
        auto GetPipelinePool() -> HandlePool<WGPUPipelineData, PipelineTag, kMaxPipelines>& { return pipelines_; }
        auto GetPipelineLayoutPool() -> HandlePool<WGPUPipelineLayoutData, PipelineLayoutTag, kMaxPipelineLayouts>& {
            return pipelineLayouts_;
        }
        auto GetDescriptorLayoutPool()
            -> HandlePool<WGPUDescriptorLayoutData, DescriptorLayoutTag, kMaxDescriptorLayouts>& {
            return descriptorLayouts_;
        }
        auto GetDescriptorSetPool() -> HandlePool<WGPUDescriptorSetData, DescriptorSetTag, kMaxDescriptorSets>& {
            return descriptorSets_;
        }
        auto GetQueryPoolPool() -> HandlePool<WGPUQueryPoolData, QueryPoolTag, kMaxQueryPools>& { return queryPools_; }
        auto GetCommandBufferPool() -> HandlePool<WGPUCommandBufferData, CommandBufferTag, kMaxCommandBuffers>& {
            return commandBuffers_;
        }
        auto GetCommandPoolPool() -> HandlePool<WGPUCommandPoolData, CommandPoolTag, kMaxCommandPools>& {
            return commandPools_;
        }
        auto GetShaderModulePool() -> HandlePool<WGPUShaderModuleData, ShaderModuleTag, kMaxShaderModules>& {
            return shaderModules_;
        }

        // -- Push constant UBO --
        auto GetPushConstantBuffer() const noexcept -> WGPUBuffer { return pushConstantBuffer_; }
        auto GetPushConstantBindGroup() const noexcept -> WGPUBindGroup { return pushConstantBindGroup_; }
        auto GetPushConstantBindGroupLayout() const noexcept -> WGPUBindGroupLayout {
            return pushConstantBindGroupLayout_;
        }

       private:
        // -- Dawn objects --
        WGPUInstance instance_ = nullptr;
        WGPUAdapter adapter_ = nullptr;
        WGPUDevice device_ = nullptr;
        WGPUQueue queue_ = nullptr;
        bool ownsInstance_ = false;
        bool ownsAdapter_ = false;

        // -- Capabilities --
        GpuCapabilityProfile capabilities_{};

        // -- Push constant emulation (256B UBO at group(0), binding(0)) --
        WGPUBuffer pushConstantBuffer_ = nullptr;
        WGPUBindGroupLayout pushConstantBindGroupLayout_ = nullptr;
        WGPUBindGroup pushConstantBindGroup_ = nullptr;

        // -- Tracking --
        uint64_t totalAllocatedBytes_ = 0;
        uint32_t totalAllocationCount_ = 0;
        uint64_t submittedSerial_ = 0;  // Monotonic serial for fence emulation

        // -- Resource pools --
        HandlePool<WGPUBufferData, BufferTag, kMaxBuffers> buffers_;
        HandlePool<WGPUTextureData, TextureTag, kMaxTextures> textures_;
        HandlePool<WGPUTextureViewData, TextureViewTag, kMaxTextureViews> textureViews_;
        HandlePool<WGPUSamplerData, SamplerTag, kMaxSamplers> samplers_;
        HandlePool<WGPUShaderModuleData, ShaderModuleTag, kMaxShaderModules> shaderModules_;
        HandlePool<WGPUFenceData, FenceTag, kMaxFences> fences_;
        HandlePool<WGPUSemaphoreData, SemaphoreTag, kMaxSemaphores> semaphores_;
        HandlePool<WGPUPipelineData, PipelineTag, kMaxPipelines> pipelines_;
        HandlePool<WGPUPipelineLayoutData, PipelineLayoutTag, kMaxPipelineLayouts> pipelineLayouts_;
        HandlePool<WGPUDescriptorLayoutData, DescriptorLayoutTag, kMaxDescriptorLayouts> descriptorLayouts_;
        HandlePool<WGPUDescriptorSetData, DescriptorSetTag, kMaxDescriptorSets> descriptorSets_;
        HandlePool<WGPUPipelineCacheData, PipelineCacheTag, kMaxPipelineCaches> pipelineCaches_;
        HandlePool<WGPUQueryPoolData, QueryPoolTag, kMaxQueryPools> queryPools_;
        HandlePool<WGPUSwapchainData, SwapchainTag, kMaxSwapchains> swapchains_;
        HandlePool<WGPUCommandBufferData, CommandBufferTag, kMaxCommandBuffers> commandBuffers_;
        HandlePool<WGPUCommandPoolData, CommandPoolTag, kMaxCommandPools> commandPools_;

        // -- Init helpers --
        void PopulateCapabilities();
        void PopulateFormatSupport();
        void CreatePushConstantResources();
    };

}  // namespace miki::rhi
