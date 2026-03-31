/** @file VulkanDevice.h
 *  @brief Vulkan 1.4 (Tier 1) backend device.
 *
 *  Conditionally included by AllBackends.h only when MIKI_BUILD_VULKAN=1.
 *  Freely includes volk.h and Vulkan types.
 *  No PIMPL — all members are direct for zero-overhead Dispatch inlining.
 *
 *  Resource storage uses typed HandlePool (O(1) alloc/free/lookup, generation-safe).
 *  Memory management via VMA (Vulkan Memory Allocator).
 *  Synchronization is timeline-first (specs/03-sync.md §3).
 *
 *  Namespace: miki::rhi
 */
#pragma once

#include "miki/rhi/backend/BackendStub.h"

#include <volk.h>

#include <memory>
#include <vector>

// VMA forward declaration — full include only in .cpp files
struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T*;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;
struct VmaAllocationInfo;

namespace miki::rhi {

    // =========================================================================
    // Queue families
    // =========================================================================

    struct VulkanQueueFamilies {
        uint32_t graphics = UINT32_MAX;
        uint32_t compute = UINT32_MAX;
        uint32_t transfer = UINT32_MAX;
        uint32_t present = UINT32_MAX;
    };

    // =========================================================================
    // Per-resource backend payloads (stored in HandlePool slots)
    // =========================================================================

    struct VulkanBufferData {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        void* mappedPtr = nullptr;
        uint64_t size = 0;
        uint64_t deviceAddress = 0;
        BufferUsage usage{};
    };

    struct VulkanTextureData {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent3D extent{};
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        TextureDimension dimension = TextureDimension::Tex2D;  // For view type inference
        bool ownsImage = true;                                 // false for swapchain images
    };

    struct VulkanTextureViewData {
        VkImageView view = VK_NULL_HANDLE;
        TextureHandle parentTexture;
    };

    struct VulkanSamplerData {
        VkSampler sampler = VK_NULL_HANDLE;
    };

    struct VulkanShaderModuleData {
        VkShaderModule module = VK_NULL_HANDLE;
    };

    struct VulkanFenceData {
        VkFence fence = VK_NULL_HANDLE;
    };

    struct VulkanSemaphoreData {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        SemaphoreType type = SemaphoreType::Binary;
    };

    struct VulkanPipelineData {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;  // Cached for CmdBindDescriptorSets / CmdPushConstants
        VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    };

    struct VulkanPipelineLayoutData {
        VkPipelineLayout layout = VK_NULL_HANDLE;
    };

    struct VulkanDescriptorLayoutData {
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    };

    struct VulkanDescriptorSetData {
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorPool sourcePool = VK_NULL_HANDLE;
    };

    struct VulkanPipelineCacheData {
        VkPipelineCache cache = VK_NULL_HANDLE;
    };

    struct VulkanPipelineLibraryPartData {
        VkPipeline pipeline = VK_NULL_HANDLE;  // Library part (VK_PIPELINE_CREATE_LIBRARY_BIT_KHR)
        PipelineLibraryPart partType = PipelineLibraryPart::VertexInput;
    };

    struct VulkanQueryPoolData {
        VkQueryPool pool = VK_NULL_HANDLE;
        uint32_t count = 0;
        QueryType type = QueryType::Timestamp;
    };

    struct VulkanAccelStructData {
        VkAccelerationStructureKHR accelStruct = VK_NULL_HANDLE;
        VkBuffer backingBuffer = VK_NULL_HANDLE;
        VmaAllocation backingAllocation = nullptr;
        VkAccelerationStructureTypeKHR type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        uint64_t deviceAddress = 0;  // For TLAS instance buffer references
    };

    struct VulkanSwapchainData {
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        std::vector<VkImage> images;
        std::vector<TextureHandle> textureHandles;
        std::vector<TextureViewHandle> textureViewHandles;  // Pre-created views for each back buffer
        VkFormat format = VK_FORMAT_B8G8R8A8_SRGB;
        VkExtent2D extent{};
        uint32_t lastAcquiredIndex = 0;  // Set by AcquireNextImage, used by Present
    };

    struct VulkanCommandBufferData {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer buffer = VK_NULL_HANDLE;
        QueueType queueType = QueueType::Graphics;
    };

    struct VulkanCommandPoolData {
        VkCommandPool pool = VK_NULL_HANDLE;
        uint32_t queueFamilyIndex = UINT32_MAX;
        QueueType queueType = QueueType::Graphics;
        // Cached VkCommandBuffers + C++ wrappers for pool-reset reuse (spec §19)
        struct CachedEntry {
            VkCommandBuffer vkCB = VK_NULL_HANDLE;
            CommandBufferHandle bufHandle;
            std::unique_ptr<VulkanCommandBuffer> wrapper;
        };
        std::vector<CachedEntry> cachedBuffers;
        uint32_t nextFreeIndex = 0;
    };

    struct VulkanDeviceMemoryData {
        VmaAllocation allocation = nullptr;
        uint64_t size = 0;
    };

    // =========================================================================
    // Device creation descriptor
    // =========================================================================

    struct VulkanDeviceDesc {
        BackendType tier = BackendType::Vulkan14;  ///< Vulkan14 (full) or VulkanCompat (1.1 subset)
        bool enableValidation = true;
        bool enableDebugMessenger = true;
        const char* appName = "miki";
        uint32_t appVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    };

    // =========================================================================
    // VulkanDevice — Tier 1 backend
    // =========================================================================

    class VulkanDevice : public DeviceBase<VulkanDevice> {
       public:
        VulkanDevice();
        ~VulkanDevice();

        VulkanDevice(const VulkanDevice&) = delete;
        auto operator=(const VulkanDevice&) -> VulkanDevice& = delete;
        VulkanDevice(VulkanDevice&&) = delete;
        auto operator=(VulkanDevice&&) -> VulkanDevice& = delete;

        [[nodiscard]] auto Init(const VulkanDeviceDesc& desc = {}) -> RhiResult<void>;

        // -- Native accessors (for interop / swapchain surface creation) --
        [[nodiscard]] auto GetVkInstance() const noexcept -> VkInstance { return instance_; }
        [[nodiscard]] auto GetVkPhysicalDevice() const noexcept -> VkPhysicalDevice { return physicalDevice_; }
        [[nodiscard]] auto GetVkDevice() const noexcept -> VkDevice { return device_; }
        [[nodiscard]] auto GetGraphicsQueue() const noexcept -> VkQueue { return graphicsQueue_; }
        [[nodiscard]] auto GetPresentQueue() const noexcept -> VkQueue { return presentQueue_; }
        [[nodiscard]] auto GetQueueFamilies() const noexcept -> const VulkanQueueFamilies& { return queueFamilies_; }
        [[nodiscard]] auto GetVmaAllocator() const noexcept -> VmaAllocator { return allocator_; }

        // -- Capability --
        auto GetBackendTypeImpl() const -> BackendType { return tier_; }
        auto GetCapabilitiesImpl() const -> const GpuCapabilityProfile& { return capabilities_; }
        auto GetQueueTimelinesImpl() const -> QueueTimelines { return queueTimelines_; }

        // -- Swapchain (VulkanSwapchain.cpp) --
        auto CreateSwapchainImpl(const SwapchainDesc& desc) -> RhiResult<SwapchainHandle>;
        void DestroySwapchainImpl(SwapchainHandle h);
        auto ResizeSwapchainImpl(SwapchainHandle h, uint32_t w, uint32_t ht) -> RhiResult<void>;
        // Swapchain helpers (internal)
        void ReleaseSwapchainResources(VulkanSwapchainData* data);
        auto RegisterSwapchainImages(VulkanSwapchainData* data, VkExtent2D extent) -> RhiResult<void>;
        auto ResolveSwapchainExtent(VkSurfaceKHR surface, uint32_t requestedW, uint32_t requestedH)
            -> std::pair<VkSurfaceCapabilitiesKHR, VkExtent2D>;
        auto AcquireNextImageImpl(SwapchainHandle h, SemaphoreHandle signal, FenceHandle fence) -> RhiResult<uint32_t>;
        auto GetSwapchainTextureImpl(SwapchainHandle h, uint32_t imageIndex) -> TextureHandle;
        auto GetSwapchainTextureViewImpl(SwapchainHandle h, uint32_t imageIndex) -> TextureViewHandle;
        auto GetSwapchainImageCountImpl(SwapchainHandle h) -> uint32_t;
        void PresentImpl(SwapchainHandle h, std::span<const SemaphoreHandle> waitSemaphores);

        // -- Sync (VulkanSync.cpp) --
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

        // -- Resources (VulkanResources.cpp) --
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

        // -- Memory aliasing (VulkanResources.cpp) --
        auto CreateMemoryHeapImpl(const MemoryHeapDesc& desc) -> RhiResult<DeviceMemoryHandle>;
        void DestroyMemoryHeapImpl(DeviceMemoryHandle h);
        void AliasBufferMemoryImpl(BufferHandle buf, DeviceMemoryHandle heap, uint64_t offset);
        void AliasTextureMemoryImpl(TextureHandle tex, DeviceMemoryHandle heap, uint64_t offset);
        auto GetBufferMemoryRequirementsImpl(BufferHandle h) -> MemoryRequirements;
        auto GetTextureMemoryRequirementsImpl(TextureHandle h) -> MemoryRequirements;

        // -- Sparse binding (VulkanResources.cpp) --
        auto GetSparsePageSizeImpl() const -> SparsePageSize;
        void SubmitSparseBindsImpl(
            QueueType queue, const SparseBindDesc& binds, std::span<const SemaphoreSubmitInfo> wait,
            std::span<const SemaphoreSubmitInfo> signal
        );

        // -- Shader (VulkanResources.cpp) --
        auto CreateShaderModuleImpl(const ShaderModuleDesc& desc) -> RhiResult<ShaderModuleHandle>;
        void DestroyShaderModuleImpl(ShaderModuleHandle h);

        // -- Descriptors (VulkanDescriptors.cpp) --
        auto CreateDescriptorLayoutImpl(const DescriptorLayoutDesc& desc) -> RhiResult<DescriptorLayoutHandle>;
        void DestroyDescriptorLayoutImpl(DescriptorLayoutHandle h);
        auto CreatePipelineLayoutImpl(const PipelineLayoutDesc& desc) -> RhiResult<PipelineLayoutHandle>;
        void DestroyPipelineLayoutImpl(PipelineLayoutHandle h);
        auto CreateDescriptorSetImpl(const DescriptorSetDesc& desc) -> RhiResult<DescriptorSetHandle>;
        void UpdateDescriptorSetImpl(DescriptorSetHandle h, std::span<const DescriptorWrite> writes);
        void DestroyDescriptorSetImpl(DescriptorSetHandle h);

        // -- Pipelines (VulkanPipelines.cpp) --
        auto CreateGraphicsPipelineImpl(const GraphicsPipelineDesc& desc) -> RhiResult<PipelineHandle>;
        auto CreateComputePipelineImpl(const ComputePipelineDesc& desc) -> RhiResult<PipelineHandle>;
        auto CreateRayTracingPipelineImpl(const RayTracingPipelineDesc& desc) -> RhiResult<PipelineHandle>;
        void DestroyPipelineImpl(PipelineHandle h);

        auto CreatePipelineCacheImpl(std::span<const uint8_t> initialData) -> RhiResult<PipelineCacheHandle>;
        auto GetPipelineCacheDataImpl(PipelineCacheHandle h) -> std::vector<uint8_t>;
        void DestroyPipelineCacheImpl(PipelineCacheHandle h);

        auto CreatePipelineLibraryPartImpl(const PipelineLibraryPartDesc& desc) -> RhiResult<PipelineLibraryPartHandle>;
        auto LinkGraphicsPipelineImpl(const LinkedPipelineDesc& desc) -> RhiResult<PipelineHandle>;

        // -- Command pools §19 (VulkanQuery.cpp) --
        auto CreateCommandPoolImpl(const CommandPoolDesc& desc) -> RhiResult<CommandPoolHandle>;
        void DestroyCommandPoolImpl(CommandPoolHandle h);
        void ResetCommandPoolImpl(CommandPoolHandle h, CommandPoolResetFlags flags);
        auto AllocateFromPoolImpl(CommandPoolHandle pool, bool secondary) -> RhiResult<CommandListAcquisition>;
        void FreeFromPoolImpl(CommandPoolHandle pool, const CommandListAcquisition& acq);

        // -- Query (VulkanQuery.cpp) --
        auto CreateQueryPoolImpl(const QueryPoolDesc& desc) -> RhiResult<QueryPoolHandle>;
        void DestroyQueryPoolImpl(QueryPoolHandle h);
        auto GetQueryResultsImpl(QueryPoolHandle h, uint32_t first, uint32_t count, std::span<uint64_t> results)
            -> RhiResult<void>;
        auto GetTimestampPeriodImpl() -> double;

        // -- Acceleration structure (VulkanAccelStruct.cpp) --
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

        // -- HandlePool accessors (for cross-file backend code, e.g. VulkanCommandBuffer) --
        auto GetBufferPool() -> HandlePool<VulkanBufferData, BufferTag, kMaxBuffers>& { return buffers_; }
        auto GetTexturePool() -> HandlePool<VulkanTextureData, TextureTag, kMaxTextures>& { return textures_; }
        auto GetTextureViewPool() -> HandlePool<VulkanTextureViewData, TextureViewTag, kMaxTextureViews>& {
            return textureViews_;
        }
        auto GetSamplerPool() -> HandlePool<VulkanSamplerData, SamplerTag, kMaxSamplers>& { return samplers_; }
        auto GetPipelinePool() -> HandlePool<VulkanPipelineData, PipelineTag, kMaxPipelines>& { return pipelines_; }
        auto GetPipelineLayoutPool() -> HandlePool<VulkanPipelineLayoutData, PipelineLayoutTag, kMaxPipelineLayouts>& {
            return pipelineLayouts_;
        }
        auto GetDescriptorSetPool() -> HandlePool<VulkanDescriptorSetData, DescriptorSetTag, kMaxDescriptorSets>& {
            return descriptorSets_;
        }
        auto GetFencePool() -> HandlePool<VulkanFenceData, FenceTag, kMaxFences>& { return fences_; }
        auto GetSemaphorePool() -> HandlePool<VulkanSemaphoreData, SemaphoreTag, kMaxSemaphores>& {
            return semaphores_;
        }
        auto GetSwapchainPool() -> HandlePool<VulkanSwapchainData, SwapchainTag, kMaxSwapchains>& {
            return swapchains_;
        }
        auto GetQueryPoolPool() -> HandlePool<VulkanQueryPoolData, QueryPoolTag, kMaxQueryPools>& {
            return queryPools_;
        }
        auto GetAccelStructPool() -> HandlePool<VulkanAccelStructData, AccelStructTag, kMaxAccelStructs>& {
            return accelStructs_;
        }
        auto GetCommandBufferPool() -> HandlePool<VulkanCommandBufferData, CommandBufferTag, kMaxCommandBuffers>& {
            return commandBuffers_;
        }
        auto GetCommandPoolPool() -> HandlePool<VulkanCommandPoolData, CommandPoolTag, kMaxCommandPools>& {
            return commandPools_;
        }

       private:
        // -- Backend tier (Vulkan14 or VulkanCompat) --
        BackendType tier_ = BackendType::Vulkan14;

        // -- Vulkan core objects --
        VkInstance instance_ = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
        VkDevice device_ = VK_NULL_HANDLE;
        VmaAllocator allocator_ = nullptr;

        // -- Queues --
        VkQueue graphicsQueue_ = VK_NULL_HANDLE;
        VkQueue presentQueue_ = VK_NULL_HANDLE;
        VkQueue computeQueue_ = VK_NULL_HANDLE;
        VkQueue transferQueue_ = VK_NULL_HANDLE;
        VulkanQueueFamilies queueFamilies_;

        // -- Timeline semaphores (specs/03-sync.md §3.2) --
        // Created once at Init, registered in HandlePool, shared across all frames and windows.
        // Binary semaphores for swapchain are per-surface (in FrameManager).
        QueueTimelines queueTimelines_;
        uint64_t graphicsTimelineValue_ = 0;
        uint64_t computeTimelineValue_ = 0;
        uint64_t transferTimelineValue_ = 0;

        // -- Capabilities --
        GpuCapabilityProfile capabilities_;

        // -- Descriptor pool management (traditional VkDescriptorSet path) --
        VkDescriptorPool activeDescriptorPool_ = VK_NULL_HANDLE;
        std::vector<VkDescriptorPool> retiredDescriptorPools_;
        auto AllocateDescriptorPool() -> VkDescriptorPool;

        // -- Resource pools --
        HandlePool<VulkanBufferData, BufferTag, kMaxBuffers> buffers_;
        HandlePool<VulkanTextureData, TextureTag, kMaxTextures> textures_;
        HandlePool<VulkanTextureViewData, TextureViewTag, kMaxTextureViews> textureViews_;
        HandlePool<VulkanSamplerData, SamplerTag, kMaxSamplers> samplers_;
        HandlePool<VulkanShaderModuleData, ShaderModuleTag, kMaxShaderModules> shaderModules_;
        HandlePool<VulkanFenceData, FenceTag, kMaxFences> fences_;
        HandlePool<VulkanSemaphoreData, SemaphoreTag, kMaxSemaphores> semaphores_;
        HandlePool<VulkanPipelineData, PipelineTag, kMaxPipelines> pipelines_;
        HandlePool<VulkanPipelineLayoutData, PipelineLayoutTag, kMaxPipelineLayouts> pipelineLayouts_;
        HandlePool<VulkanDescriptorLayoutData, DescriptorLayoutTag, kMaxDescriptorLayouts> descriptorLayouts_;
        HandlePool<VulkanDescriptorSetData, DescriptorSetTag, kMaxDescriptorSets> descriptorSets_;
        HandlePool<VulkanPipelineCacheData, PipelineCacheTag, kMaxPipelineCaches> pipelineCaches_;
        HandlePool<VulkanPipelineLibraryPartData, PipelineLibraryPartTag, kMaxPipelineLibraryParts>
            pipelineLibraryParts_;
        HandlePool<VulkanQueryPoolData, QueryPoolTag, kMaxQueryPools> queryPools_;
        HandlePool<VulkanAccelStructData, AccelStructTag, kMaxAccelStructs> accelStructs_;
        HandlePool<VulkanSwapchainData, SwapchainTag, kMaxSwapchains> swapchains_;
        HandlePool<VulkanCommandBufferData, CommandBufferTag, kMaxCommandBuffers> commandBuffers_;
        HandlePool<VulkanCommandPoolData, CommandPoolTag, kMaxCommandPools> commandPools_;
        HandlePool<VulkanDeviceMemoryData, DeviceMemoryTag, kMaxDeviceMemory> deviceMemory_;

        // -- Init helpers --
        auto CreateInstance(const VulkanDeviceDesc& desc) -> RhiResult<void>;
        auto SelectPhysicalDevice() -> RhiResult<void>;
        auto CreateLogicalDevice() -> RhiResult<void>;
        auto CreateVmaAllocator() -> RhiResult<void>;
        auto CreateTimelineSemaphores() -> RhiResult<void>;
        void PopulateCapabilities();
        void PopulateCapabilities_Tier1(
            const VkPhysicalDeviceFeatures& deviceFeatures, const std::vector<VkExtensionProperties>& availableExts
        );
        void PopulateCapabilities_Tier2(
            const VkPhysicalDeviceFeatures& deviceFeatures, const std::vector<VkExtensionProperties>& availableExts
        );
        void PopulateFormatSupport();
    };

}  // namespace miki::rhi
