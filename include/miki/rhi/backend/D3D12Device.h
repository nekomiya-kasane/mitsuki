/** @file D3D12Device.h
 *  @brief Direct3D 12 (Tier 1) backend device.
 *
 *  Conditionally included by AllBackends.h only when MIKI_BUILD_D3D12=1.
 *  Uses Agility SDK via DirectX-Headers, D3D12MA for memory management.
 *  Enhanced Barriers (ID3D12Device10) when available, legacy fallback otherwise.
 *  ID3D12Fence is inherently timeline — used for both fence and semaphore semantics.
 *
 *  Resource storage uses typed HandlePool (O(1) alloc/free/lookup, generation-safe).
 *  Namespace: miki::rhi
 */
#pragma once

#include "miki/rhi/Device.h"
#include "miki/rhi/GpuCapabilityProfile.h"
#include "miki/frame/SyncScheduler.h"

#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include <directx/d3d12.h>
#include <directx/d3dx12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <memory>
#include <vector>

using Microsoft::WRL::ComPtr;

// D3D12MA forward declaration — full include only in .cpp files
namespace D3D12MA {
    class Allocator;
    class Allocation;
}  // namespace D3D12MA

namespace miki::rhi {

    struct D3D12BlitPipeline;  // Defined in D3D12BlitPipeline.h (src/)

    // =========================================================================
    // Queue families
    // =========================================================================

    struct D3D12Queues {
        ComPtr<ID3D12CommandQueue> graphics;
        ComPtr<ID3D12CommandQueue> compute;       ///< Frame-sync compute (HIGH priority for Level A/B)
        ComPtr<ID3D12CommandQueue> computeAsync;  ///< Async compute (NORMAL priority); may alias compute
        ComPtr<ID3D12CommandQueue> copy;
    };

    // =========================================================================
    // Per-resource backend payloads (stored in HandlePool slots)
    // =========================================================================

    struct D3D12BufferData {
        ComPtr<ID3D12Resource> resource;
        D3D12MA::Allocation* allocation = nullptr;
        void* mappedPtr = nullptr;
        uint64_t size = 0;
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
        BufferUsage usage{};
    };

    struct D3D12TextureData {
        ComPtr<ID3D12Resource> resource;
        D3D12MA::Allocation* allocation = nullptr;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        uint32_t width = 0, height = 0, depth = 0;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        TextureDimension dimension = TextureDimension::Tex2D;  // For view type inference
        bool ownsResource = true;                              // false for swapchain images
    };

    struct D3D12TextureViewData {
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle{};
        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle{};
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{};
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
        TextureHandle parentTexture;
        bool hasSrv = false;
        bool hasUav = false;
        bool hasRtv = false;
        bool hasDsv = false;
    };

    struct D3D12SamplerData {
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
    };

    struct D3D12ShaderModuleData {
        std::vector<uint8_t> bytecode;  // DXIL blob
        ShaderStage stage = ShaderStage::Vertex;
    };

    struct D3D12FenceData {
        ComPtr<ID3D12Fence> fence;
        uint64_t value = 0;
        HANDLE event = nullptr;  // CPU wait event
    };

    struct D3D12SemaphoreData {
        ComPtr<ID3D12Fence> fence;  // D3D12 fences are inherently timeline
        uint64_t value = 0;
        SemaphoreType type = SemaphoreType::Binary;
    };

    struct D3D12PipelineData {
        ComPtr<ID3D12PipelineState> pso;
        ComPtr<ID3D12StateObject> stateObject;  // DXR ray tracing pipeline (null for raster/compute)
        ComPtr<ID3D12RootSignature> rootSignature;
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        bool isCompute = false;
        bool isMeshShader = false;
        bool isRayTracing = false;
        // Per-binding vertex strides (set at pipeline creation, consumed by CmdBindVertexBuffer).
        // D3D12 IASetVertexBuffers needs stride per-slot; Vulkan gets it from pipeline or dynamic state.
        static constexpr uint32_t kMaxVertexBindings = 16;  // D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT
        std::array<uint32_t, kMaxVertexBindings> vertexStrides{};
    };

    struct D3D12PipelineLayoutData {
        ComPtr<ID3D12RootSignature> rootSignature;
        uint32_t pushConstantRootIndex = UINT32_MAX;
        uint32_t pushConstantSize = 0;
    };

    struct D3D12DescriptorLayoutData {
        std::vector<D3D12_DESCRIPTOR_RANGE1> ranges;
        std::vector<D3D12_ROOT_PARAMETER1> rootParams;
        uint32_t totalDescriptors = 0;
        struct BindingInfo {
            uint32_t binding = 0;
            BindingType type{};
        };
        std::vector<BindingInfo> bindings;
    };

    struct D3D12DescriptorSetData {
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
        uint32_t descriptorCount = 0;
        uint32_t heapOffset = 0;  // Offset in the shader-visible descriptor heap
        DescriptorLayoutHandle layoutHandle;
    };

    struct D3D12PipelineCacheData {
        ComPtr<ID3D12PipelineLibrary1> library;
        std::vector<uint8_t> blob;
    };

    struct D3D12PipelineLibraryPartData {
        ComPtr<ID3D12PipelineState> pso;
        PipelineLibraryPart partType = PipelineLibraryPart::VertexInput;
    };

    struct D3D12QueryPoolData {
        ComPtr<ID3D12QueryHeap> heap;
        ComPtr<ID3D12Resource> readbackBuffer;
        uint32_t count = 0;
        QueryType type = QueryType::Timestamp;
    };

    struct D3D12AccelStructData {
        ComPtr<ID3D12Resource> resource;
        D3D12MA::Allocation* allocation = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
        bool isTLAS = false;
    };

    struct D3D12SwapchainData {
        ComPtr<IDXGISwapChain4> swapchain;
        std::vector<ComPtr<ID3D12Resource>> backBuffers;
        std::vector<TextureHandle> textureHandles;
        std::vector<TextureViewHandle> textureViewHandles;  // Pre-created views for each back buffer
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles;
        DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
        uint32_t width = 0, height = 0;
        uint32_t currentBackBufferIndex = 0;
        HWND hwnd = nullptr;
    };

    struct D3D12CommandBufferData {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList7> list;
        QueueType queueType = QueueType::Graphics;
        bool isSecondary = false;
    };

    struct D3D12CommandPoolData {
        D3D12_COMMAND_LIST_TYPE listType = D3D12_COMMAND_LIST_TYPE_DIRECT;
        QueueType queueType = QueueType::Graphics;
        // Per-entry allocator model: each CachedEntry owns its own ID3D12CommandAllocator.
        // D3D12 spec requires at most one recording command list per allocator, so concurrent
        // multi-batch recording (AcquireCommandList x N) needs N separate allocators.
        struct CachedEntry {
            ComPtr<ID3D12CommandAllocator> allocator;
            ComPtr<ID3D12GraphicsCommandList7> list;
            CommandBufferHandle bufHandle;
            std::unique_ptr<D3D12CommandBuffer> wrapper;
        };
        std::vector<CachedEntry> cachedEntries;
        uint32_t nextFreeIndex = 0;
        ComPtr<ID3D12Device10> ownerDevice;  // for creating new allocators on demand
    };

    struct D3D12DeviceMemoryData {
        ComPtr<ID3D12Heap> heap;
        uint64_t size = 0;
    };

    // =========================================================================
    // Descriptor heap management
    // =========================================================================

    struct D3D12DescriptorHeapAllocator {
        ComPtr<ID3D12DescriptorHeap> heap;
        uint32_t capacity = 0;
        uint32_t allocatedCount = 0;
        uint32_t descriptorSize = 0;

        auto Allocate(uint32_t count) -> uint32_t {
            if (allocatedCount + count > capacity) {
                return UINT32_MAX;
            }
            uint32_t offset = allocatedCount;
            allocatedCount += count;
            return offset;
        }

        auto GetCpuHandle(uint32_t offset) const -> D3D12_CPU_DESCRIPTOR_HANDLE {
            D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
            h.ptr += static_cast<SIZE_T>(offset) * descriptorSize;
            return h;
        }

        auto GetGpuHandle(uint32_t offset) const -> D3D12_GPU_DESCRIPTOR_HANDLE {
            D3D12_GPU_DESCRIPTOR_HANDLE h = heap->GetGPUDescriptorHandleForHeapStart();
            h.ptr += static_cast<UINT64>(offset) * descriptorSize;
            return h;
        }
    };

    // =========================================================================
    // D3D12Device — Tier 1 backend
    // =========================================================================

    struct D3D12DeviceDesc {
        bool enableValidation = true;
        bool enableGpuCapture = false;
        uint32_t adapterIndex = 0;
    };

    class D3D12Device : public DeviceBase<D3D12Device> {
       public:
        D3D12Device();
        ~D3D12Device();

        D3D12Device(const D3D12Device&) = delete;
        auto operator=(const D3D12Device&) -> D3D12Device& = delete;
        D3D12Device(D3D12Device&&) = delete;
        auto operator=(D3D12Device&&) -> D3D12Device& = delete;

        [[nodiscard]] auto Init(const D3D12DeviceDesc& desc = {}) -> RhiResult<void>;

        // -- Native accessors --
        [[nodiscard]] auto GetD3D12Device() const noexcept -> ID3D12Device10* { return device_.Get(); }
        [[nodiscard]] auto GetGraphicsQueue() const noexcept -> ID3D12CommandQueue* { return queues_.graphics.Get(); }
        [[nodiscard]] auto GetDXGIFactory() const noexcept -> IDXGIFactory6* { return factory_.Get(); }

        // -- Compile-time backend identity (for if constexpr adaptation paths) --
        static constexpr BackendType kBackendType = BackendType::D3D12;

        // -- Capability --
        [[nodiscard]] auto GetBackendTypeImpl() const -> BackendType { return kBackendType; }
        [[nodiscard]] auto GetCapabilitiesImpl() const -> const GpuCapabilityProfile& { return capabilities_; }
        [[nodiscard]] auto GetQueueTimelinesImpl() const -> QueueTimelines { return queueTimelines_; }
        [[nodiscard]] auto GetSyncSchedulerImpl() noexcept -> frame::SyncScheduler& { return syncScheduler_; }
        [[nodiscard]] auto GetSyncSchedulerImpl() const noexcept -> const frame::SyncScheduler& {
            return syncScheduler_;
        }

        // -- Swapchain (D3D12Swapchain.cpp) --
        auto CreateSwapchainImpl(const SwapchainDesc& desc) -> RhiResult<SwapchainHandle>;
        void DestroySwapchainImpl(SwapchainHandle h);
        auto ResizeSwapchainImpl(SwapchainHandle h, uint32_t w, uint32_t ht) -> RhiResult<void>;
        auto AcquireNextImageImpl(SwapchainHandle h, SemaphoreHandle signal, FenceHandle fence) -> RhiResult<uint32_t>;
        [[nodiscard]] auto GetSwapchainTextureImpl(SwapchainHandle h, uint32_t imageIndex) -> TextureHandle;
        [[nodiscard]] auto GetSwapchainTextureViewImpl(SwapchainHandle h, uint32_t imageIndex) -> TextureViewHandle;
        [[nodiscard]] auto GetSwapchainImageCountImpl(SwapchainHandle h) -> uint32_t;
        void PresentImpl(SwapchainHandle h, std::span<const SemaphoreHandle> waitSemaphores);

        // -- Sync (D3D12Device.cpp) --
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

        // -- Resources (D3D12Resources.cpp) --
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

        // -- Memory aliasing (D3D12Resources.cpp) --
        auto CreateMemoryHeapImpl(const MemoryHeapDesc& desc) -> RhiResult<DeviceMemoryHandle>;
        void DestroyMemoryHeapImpl(DeviceMemoryHandle h);
        void AliasBufferMemoryImpl(BufferHandle buf, DeviceMemoryHandle heap, uint64_t offset);
        void AliasTextureMemoryImpl(TextureHandle tex, DeviceMemoryHandle heap, uint64_t offset);
        auto GetBufferMemoryRequirementsImpl(BufferHandle h) -> MemoryRequirements;
        auto GetTextureMemoryRequirementsImpl(TextureHandle h) -> MemoryRequirements;

        // -- Sparse binding (D3D12Resources.cpp) --
        auto GetSparsePageSizeImpl() const -> SparsePageSize;
        void SubmitSparseBindsImpl(
            QueueType queue, const SparseBindDesc& binds, std::span<const SemaphoreSubmitInfo> wait,
            std::span<const SemaphoreSubmitInfo> signal
        );

        // -- Shader (D3D12Resources.cpp) --
        auto CreateShaderModuleImpl(const ShaderModuleDesc& desc) -> RhiResult<ShaderModuleHandle>;
        void DestroyShaderModuleImpl(ShaderModuleHandle h);

        // -- Descriptors (D3D12Descriptors.cpp) --
        auto CreateDescriptorLayoutImpl(const DescriptorLayoutDesc& desc) -> RhiResult<DescriptorLayoutHandle>;
        void DestroyDescriptorLayoutImpl(DescriptorLayoutHandle h);
        auto CreatePipelineLayoutImpl(const PipelineLayoutDesc& desc) -> RhiResult<PipelineLayoutHandle>;
        void DestroyPipelineLayoutImpl(PipelineLayoutHandle h);
        auto CreateDescriptorSetImpl(const DescriptorSetDesc& desc) -> RhiResult<DescriptorSetHandle>;
        void UpdateDescriptorSetImpl(DescriptorSetHandle h, std::span<const DescriptorWrite> writes);
        void DestroyDescriptorSetImpl(DescriptorSetHandle h);

        // -- Pipelines (D3D12Pipelines.cpp) --
        auto CreateGraphicsPipelineImpl(const GraphicsPipelineDesc& desc) -> RhiResult<PipelineHandle>;
        auto CreateComputePipelineImpl(const ComputePipelineDesc& desc) -> RhiResult<PipelineHandle>;
        auto CreateRayTracingPipelineImpl(const RayTracingPipelineDesc& desc) -> RhiResult<PipelineHandle>;
        void DestroyPipelineImpl(PipelineHandle h);

        auto CreatePipelineCacheImpl(std::span<const uint8_t> initialData) -> RhiResult<PipelineCacheHandle>;
        auto GetPipelineCacheDataImpl(PipelineCacheHandle h) -> std::vector<uint8_t>;
        void DestroyPipelineCacheImpl(PipelineCacheHandle h);

        auto CreatePipelineLibraryPartImpl(const PipelineLibraryPartDesc& desc) -> RhiResult<PipelineLibraryPartHandle>;
        auto LinkGraphicsPipelineImpl(const LinkedPipelineDesc& desc) -> RhiResult<PipelineHandle>;
        void DestroyPipelineLibraryPartImpl(PipelineLibraryPartHandle h);

        // -- Command pools §19 (D3D12Query.cpp) --
        auto CreateCommandPoolImpl(const CommandPoolDesc& desc) -> RhiResult<CommandPoolHandle>;
        void DestroyCommandPoolImpl(CommandPoolHandle h);
        void ResetCommandPoolImpl(CommandPoolHandle h, CommandPoolResetFlags flags);
        auto AllocateFromPoolImpl(CommandPoolHandle pool, bool secondary) -> RhiResult<CommandListAcquisition>;

        // -- Query (D3D12Query.cpp) --
        auto CreateQueryPoolImpl(const QueryPoolDesc& desc) -> RhiResult<QueryPoolHandle>;
        void DestroyQueryPoolImpl(QueryPoolHandle h);
        auto GetQueryResultsImpl(QueryPoolHandle h, uint32_t first, uint32_t count, std::span<uint64_t> results)
            -> RhiResult<void>;
        auto GetTimestampPeriodImpl() -> double;

        // -- Acceleration structure (D3D12AccelStruct.cpp) --
        auto GetBLASBuildSizesImpl(const BLASDesc& desc) -> AccelStructBuildSizes;
        auto GetTLASBuildSizesImpl(const TLASDesc& desc) -> AccelStructBuildSizes;
        auto CreateBLASImpl(const BLASDesc& desc) -> RhiResult<AccelStructHandle>;
        auto CreateTLASImpl(const TLASDesc& desc) -> RhiResult<AccelStructHandle>;
        void DestroyAccelStructImpl(AccelStructHandle h);

        // -- Memory stats --
        auto GetMemoryStatsImpl() const -> MemoryStats;
        auto GetMemoryHeapBudgetsImpl(std::span<MemoryHeapBudget> out) const -> uint32_t;

        // -- Debug names --
        void SetObjectDebugNameImpl(SemaphoreHandle h, const char* name) { semaphores_.SetDebugName(h, name); }
        void SetObjectDebugNameImpl(FenceHandle h, const char* name) { fences_.SetDebugName(h, name); }
        void SetObjectDebugNameImpl(BufferHandle h, const char* name) { buffers_.SetDebugName(h, name); }
        void SetObjectDebugNameImpl(TextureHandle h, const char* name) { textures_.SetDebugName(h, name); }
        void SetObjectDebugNameImpl(TextureViewHandle h, const char* name) { textureViews_.SetDebugName(h, name); }
        void SetObjectDebugNameImpl(SamplerHandle h, const char* name) { samplers_.SetDebugName(h, name); }
        void SetObjectDebugNameImpl(ShaderModuleHandle h, const char* name) { shaderModules_.SetDebugName(h, name); }
        void SetObjectDebugNameImpl(PipelineHandle h, const char* name) { pipelines_.SetDebugName(h, name); }
        void SetObjectDebugNameImpl(PipelineLayoutHandle h, const char* name) {
            pipelineLayouts_.SetDebugName(h, name);
        }
        void SetObjectDebugNameImpl(DescriptorLayoutHandle h, const char* name) {
            descriptorLayouts_.SetDebugName(h, name);
        }
        void SetObjectDebugNameImpl(DescriptorSetHandle h, const char* name) { descriptorSets_.SetDebugName(h, name); }
        void SetObjectDebugNameImpl(QueryPoolHandle h, const char* name) { queryPools_.SetDebugName(h, name); }
        void SetObjectDebugNameImpl(AccelStructHandle h, const char* name) { accelStructs_.SetDebugName(h, name); }
        void SetObjectDebugNameImpl(CommandPoolHandle h, const char* name) { commandPools_.SetDebugName(h, name); }
        void SetObjectDebugNameImpl(CommandBufferHandle h, const char* name) { commandBuffers_.SetDebugName(h, name); }
        void SetObjectDebugNameImpl(SwapchainHandle h, const char* name) { swapchains_.SetDebugName(h, name); }
        [[nodiscard]] auto GetObjectDebugNameImpl(SemaphoreHandle h) const -> const char* {
            auto n = semaphores_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }
        [[nodiscard]] auto GetObjectDebugNameImpl(FenceHandle h) const -> const char* {
            auto n = fences_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }
        [[nodiscard]] auto GetObjectDebugNameImpl(BufferHandle h) const -> const char* {
            auto n = buffers_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }
        [[nodiscard]] auto GetObjectDebugNameImpl(TextureHandle h) const -> const char* {
            auto n = textures_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }
        [[nodiscard]] auto GetObjectDebugNameImpl(TextureViewHandle h) const -> const char* {
            auto n = textureViews_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }
        [[nodiscard]] auto GetObjectDebugNameImpl(SamplerHandle h) const -> const char* {
            auto n = samplers_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }
        [[nodiscard]] auto GetObjectDebugNameImpl(ShaderModuleHandle h) const -> const char* {
            auto n = shaderModules_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }
        [[nodiscard]] auto GetObjectDebugNameImpl(PipelineHandle h) const -> const char* {
            auto n = pipelines_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }
        [[nodiscard]] auto GetObjectDebugNameImpl(PipelineLayoutHandle h) const -> const char* {
            auto n = pipelineLayouts_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }
        [[nodiscard]] auto GetObjectDebugNameImpl(DescriptorLayoutHandle h) const -> const char* {
            auto n = descriptorLayouts_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }
        [[nodiscard]] auto GetObjectDebugNameImpl(DescriptorSetHandle h) const -> const char* {
            auto n = descriptorSets_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }
        [[nodiscard]] auto GetObjectDebugNameImpl(QueryPoolHandle h) const -> const char* {
            auto n = queryPools_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }
        [[nodiscard]] auto GetObjectDebugNameImpl(AccelStructHandle h) const -> const char* {
            auto n = accelStructs_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }
        [[nodiscard]] auto GetObjectDebugNameImpl(CommandPoolHandle h) const -> const char* {
            auto n = commandPools_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }
        [[nodiscard]] auto GetObjectDebugNameImpl(CommandBufferHandle h) const -> const char* {
            auto n = commandBuffers_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }
        [[nodiscard]] auto GetObjectDebugNameImpl(SwapchainHandle h) const -> const char* {
            auto n = swapchains_.GetDebugName(h);
            return n ? n : "(unnamed)";
        }

        // -- Surface capabilities --
        auto GetSurfaceCapabilitiesImpl(const NativeWindowHandle& window) const -> RenderSurfaceCapabilities;

        // -- HandlePool accessors (for D3D12CommandBuffer cross-file access) --
        auto GetBufferPool() -> HandlePool<D3D12BufferData, BufferTag, kMaxBuffers>& { return buffers_; }
        auto GetTexturePool() -> HandlePool<D3D12TextureData, TextureTag, kMaxTextures>& { return textures_; }
        auto GetTextureViewPool() -> HandlePool<D3D12TextureViewData, TextureViewTag, kMaxTextureViews>& {
            return textureViews_;
        }
        auto GetSamplerPool() -> HandlePool<D3D12SamplerData, SamplerTag, kMaxSamplers>& { return samplers_; }
        auto GetPipelinePool() -> HandlePool<D3D12PipelineData, PipelineTag, kMaxPipelines>& { return pipelines_; }
        auto GetPipelineLayoutPool() -> HandlePool<D3D12PipelineLayoutData, PipelineLayoutTag, kMaxPipelineLayouts>& {
            return pipelineLayouts_;
        }
        auto GetDescriptorSetPool() -> HandlePool<D3D12DescriptorSetData, DescriptorSetTag, kMaxDescriptorSets>& {
            return descriptorSets_;
        }
        auto GetFencePool() -> HandlePool<D3D12FenceData, FenceTag, kMaxFences>& { return fences_; }
        auto GetSemaphorePool() -> HandlePool<D3D12SemaphoreData, SemaphoreTag, kMaxSemaphores>& { return semaphores_; }
        auto GetQueryPoolPool() -> HandlePool<D3D12QueryPoolData, QueryPoolTag, kMaxQueryPools>& { return queryPools_; }
        auto GetAccelStructPool() -> HandlePool<D3D12AccelStructData, AccelStructTag, kMaxAccelStructs>& {
            return accelStructs_;
        }
        auto GetCommandBufferPool() -> HandlePool<D3D12CommandBufferData, CommandBufferTag, kMaxCommandBuffers>& {
            return commandBuffers_;
        }
        auto GetCommandPoolPool() -> HandlePool<D3D12CommandPoolData, CommandPoolTag, kMaxCommandPools>& {
            return commandPools_;
        }
        auto GetShaderModulePool() -> HandlePool<D3D12ShaderModuleData, ShaderModuleTag, kMaxShaderModules>& {
            return shaderModules_;
        }
        auto GetShaderVisibleHeap() -> D3D12DescriptorHeapAllocator& { return shaderVisibleCbvSrvUav_; }
        auto GetShaderVisibleSamplerHeap() -> D3D12DescriptorHeapAllocator& { return shaderVisibleSampler_; }

        // Blit pipeline accessor (for CmdBlitTexture emulation)
        auto GetBlitPipeline() -> D3D12BlitPipeline& { return *blitPipeline_; }

        // Descriptor heap accessors (for standalone clear operations)
        auto GetRtvHeap() -> D3D12DescriptorHeapAllocator& { return rtvHeap_; }
        auto GetDsvHeap() -> D3D12DescriptorHeapAllocator& { return dsvHeap_; }

        // Command signature accessors (for indirect draw/dispatch)
        [[nodiscard]] auto GetCmdSigDraw() const noexcept -> ID3D12CommandSignature* { return cmdSigDraw_.Get(); }
        [[nodiscard]] auto GetCmdSigDrawIndexed() const noexcept -> ID3D12CommandSignature* {
            return cmdSigDrawIndexed_.Get();
        }
        [[nodiscard]] auto GetCmdSigDispatch() const noexcept -> ID3D12CommandSignature* {
            return cmdSigDispatch_.Get();
        }
        [[nodiscard]] auto GetCmdSigDispatchMesh() const noexcept -> ID3D12CommandSignature* {
            return cmdSigDispatchMesh_.Get();
        }

       private:
        // -- DXGI / D3D12 core objects --
        ComPtr<IDXGIFactory6> factory_;
        ComPtr<IDXGIAdapter4> adapter_;
        ComPtr<ID3D12Device10> device_;
        ComPtr<ID3D12Debug6> debugController_;
        ComPtr<ID3D12InfoQueue> infoQueue_;
        DWORD infoQueueCallbackCookie_ = 0;  // Non-zero if ID3D12InfoQueue1 callback registered
        D3D12MA::Allocator* allocator_ = nullptr;

        // -- Queues --
        D3D12Queues queues_;

        // -- Internal fence for frame synchronization --
        ComPtr<ID3D12Fence> frameFence_;
        uint64_t frameFenceValue_ = 0;
        HANDLE frameFenceEvent_ = nullptr;

        // -- Per-queue timeline semaphores (specs/03-sync.md §3.2) --
        QueueTimelines queueTimelines_;
        frame::SyncScheduler syncScheduler_;

        // -- Capabilities --
        GpuCapabilityProfile capabilities_;
        bool hasEnhancedBarriers_ = false;
        D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_12_0;

        // -- Internal blit pipeline for CmdBlitTexture emulation --
        std::unique_ptr<D3D12BlitPipeline> blitPipeline_;

        // -- Pre-created command signatures for indirect draw/dispatch --
        ComPtr<ID3D12CommandSignature> cmdSigDraw_;          // D3D12_INDIRECT_ARGUMENT_TYPE_DRAW
        ComPtr<ID3D12CommandSignature> cmdSigDrawIndexed_;   // D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED
        ComPtr<ID3D12CommandSignature> cmdSigDispatch_;      // D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH
        ComPtr<ID3D12CommandSignature> cmdSigDispatchMesh_;  // D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH

        // -- Descriptor heaps --
        D3D12DescriptorHeapAllocator rtvHeap_;                 // Render target views (CPU-only)
        D3D12DescriptorHeapAllocator dsvHeap_;                 // Depth-stencil views (CPU-only)
        D3D12DescriptorHeapAllocator shaderVisibleCbvSrvUav_;  // Shader-visible CBV/SRV/UAV
        D3D12DescriptorHeapAllocator shaderVisibleSampler_;    // Shader-visible samplers
        D3D12DescriptorHeapAllocator stagingCbvSrvUav_;        // CPU-only staging for copies
        D3D12DescriptorHeapAllocator stagingSampler_;          // CPU-only staging for sampler copies

        // -- Resource pools (same capacities as Vulkan backend) --
        HandlePool<D3D12BufferData, BufferTag, kMaxBuffers> buffers_;
        HandlePool<D3D12TextureData, TextureTag, kMaxTextures> textures_;
        HandlePool<D3D12TextureViewData, TextureViewTag, kMaxTextureViews> textureViews_;
        HandlePool<D3D12SamplerData, SamplerTag, kMaxSamplers> samplers_;
        HandlePool<D3D12ShaderModuleData, ShaderModuleTag, kMaxShaderModules> shaderModules_;
        HandlePool<D3D12FenceData, FenceTag, kMaxFences> fences_;
        HandlePool<D3D12SemaphoreData, SemaphoreTag, kMaxSemaphores> semaphores_;
        HandlePool<D3D12PipelineData, PipelineTag, kMaxPipelines> pipelines_;
        HandlePool<D3D12PipelineLayoutData, PipelineLayoutTag, kMaxPipelineLayouts> pipelineLayouts_;
        HandlePool<D3D12DescriptorLayoutData, DescriptorLayoutTag, kMaxDescriptorLayouts> descriptorLayouts_;
        HandlePool<D3D12DescriptorSetData, DescriptorSetTag, kMaxDescriptorSets> descriptorSets_;
        HandlePool<D3D12PipelineCacheData, PipelineCacheTag, kMaxPipelineCaches> pipelineCaches_;
        HandlePool<D3D12PipelineLibraryPartData, PipelineLibraryPartTag, kMaxPipelineLibraryParts>
            pipelineLibraryParts_;
        HandlePool<D3D12QueryPoolData, QueryPoolTag, kMaxQueryPools> queryPools_;
        HandlePool<D3D12AccelStructData, AccelStructTag, kMaxAccelStructs> accelStructs_;
        HandlePool<D3D12SwapchainData, SwapchainTag, kMaxSwapchains> swapchains_;
        HandlePool<D3D12CommandBufferData, CommandBufferTag, kMaxCommandBuffers> commandBuffers_;
        HandlePool<D3D12CommandPoolData, CommandPoolTag, kMaxCommandPools> commandPools_;
        HandlePool<D3D12DeviceMemoryData, DeviceMemoryTag, kMaxDeviceMemory> deviceMemory_;

        // -- Init helpers --
        auto CreateFactory(bool enableValidation) -> RhiResult<void>;
        auto SelectAdapter(uint32_t adapterIndex) -> RhiResult<void>;
        auto CreateDevice() -> RhiResult<void>;
        auto CreateAllocator() -> RhiResult<void>;
        auto CreateQueues() -> RhiResult<void>;
        auto CreateQueueTimelines() -> RhiResult<void>;
        auto CreateDescriptorHeaps() -> RhiResult<void>;
        auto CreateCommandSignatures() -> RhiResult<void>;
        void PopulateCapabilities();
        void PopulateFormatSupport();
        void SetupInfoQueue();
        void DrainInfoQueueMessages();
    };

}  // namespace miki::rhi
