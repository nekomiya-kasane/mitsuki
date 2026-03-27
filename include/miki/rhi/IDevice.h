/** @brief Abstract device interface for the miki RHI.
 *
 * IDevice is the single most referenced interface in the project.
 * Three creation paths:
 *   - CreateFromExisting(ExternalContext)          — injection-first (primary)
 *   - CreateOwned(DeviceConfig)                    — headless / standalone
 *   - CreateForWindow(NativeWindowInfo, DeviceConfig) — host window, miki device
 *
 * IDevice created via CreateFromExisting does NOT own the underlying API device.
 * CreateOwned does. Destroy() releases miki-internal resources only.
 *
 * Namespace: miki::rhi
 */
#pragma once

#include <memory>
#include <optional>

#include "miki/core/Result.h"
#include "miki/rhi/GpuCapabilityProfile.h"
#include "miki/rhi/RhiDescriptors.h"
#include "miki/rhi/RhiTypes.h"

namespace miki::rhi {

    class ICommandBuffer;

    /** @brief Abstract GPU device interface.
     *
     * All fallible methods return Result<T>. Pure virtual — backends implement.
     * Single-owner: not thread-safe. One IDevice per logical device.
     */
    class IDevice {
       public:
        virtual ~IDevice() = default;

        IDevice(const IDevice&) = delete;
        auto operator=(const IDevice&) -> IDevice& = delete;
        IDevice(IDevice&&) = default;
        auto operator=(IDevice&&) -> IDevice& = default;

        // --- Static factory methods ---

        /** @brief Create a device wrapping pre-existing API objects (injection path).
         *  @param iContext Backend-specific external context.
         *  @return Newly created device, or an error.
         */
        [[nodiscard]] static auto CreateFromExisting(ExternalContext iContext)
            -> miki::core::Result<std::unique_ptr<IDevice>>;

        /** @brief Create a device that owns its underlying API objects.
         *  @param iConfig Device creation configuration.
         *  @return Newly created device, or an error.
         */
        [[nodiscard]] static auto CreateOwned(DeviceConfig iConfig) -> miki::core::Result<std::unique_ptr<IDevice>>;

        /** @brief Create a device on a host-owned window (no existing GPU context).
         *
         *  Host owns the window but has NOT created a GPU device/context.
         *  miki creates the appropriate backend device on the provided window.
         *  Window ownership stays with the host.
         *
         *  @param iWindowInfo Native window handle + target backend.
         *  @param iConfig     Device creation configuration (enableValidation, etc.).
         *  @return Newly created device, or an error.
         */
        [[nodiscard]] static auto CreateForWindow(NativeWindowInfo iWindowInfo, DeviceConfig iConfig = {})
            -> miki::core::Result<std::unique_ptr<IDevice>>;

        // --- Resource creation ---

        /** @brief Create a texture resource.
         *  @param iDesc Texture creation descriptor.
         */
        [[nodiscard]] virtual auto CreateTexture(const TextureDesc& iDesc) -> miki::core::Result<TextureHandle> = 0;

        /** @brief Create an aliased texture view into an existing texture.
         *
         * The returned handle borrows the source image — DestroyTexture on the
         * view only destroys the view, not the underlying image/memory.
         * Used for per-mip storage views and view type overrides.
         *
         *  @param iDesc Texture view descriptor.
         */
        [[nodiscard]] virtual auto CreateTextureView(const TextureViewDesc& iDesc) -> miki::core::Result<TextureHandle>
            = 0;

        /** @brief Create a buffer resource.
         *  @param iDesc Buffer creation descriptor.
         */
        [[nodiscard]] virtual auto CreateBuffer(const BufferDesc& iDesc) -> miki::core::Result<BufferHandle> = 0;

        /** @brief Create a graphics pipeline.
         *  @param iDesc Graphics pipeline descriptor.
         */
        [[nodiscard]] virtual auto CreateGraphicsPipeline(const GraphicsPipelineDesc& iDesc)
            -> miki::core::Result<PipelineHandle>
            = 0;

        /** @brief Create a compute pipeline.
         *  @param iDesc Compute pipeline descriptor.
         */
        [[nodiscard]] virtual auto CreateComputePipeline(const ComputePipelineDesc& iDesc)
            -> miki::core::Result<PipelineHandle>
            = 0;

        /** @brief Create a texture sampler.
         *  @param iDesc Sampler creation descriptor.
         */
        [[nodiscard]] virtual auto CreateSampler(const SamplerDesc& iDesc) -> miki::core::Result<SamplerHandle> = 0;

        // --- Descriptor system ---

        /** @brief Create a descriptor set layout.
         *  @param iDesc Layout descriptor with binding definitions.
         */
        [[nodiscard]] virtual auto CreateDescriptorSetLayout(const DescriptorSetLayoutDesc& iDesc)
            -> miki::core::Result<DescriptorSetLayoutHandle>
            = 0;

        /** @brief Create a pipeline layout.
         *  @param iDesc Pipeline layout descriptor with set layouts and push constant ranges.
         */
        [[nodiscard]] virtual auto CreatePipelineLayout(const PipelineLayoutDesc& iDesc)
            -> miki::core::Result<PipelineLayoutHandle>
            = 0;

        /** @brief Create a descriptor set from a layout.
         *  @param iLayout The layout that defines the set's binding structure.
         */
        [[nodiscard]] virtual auto CreateDescriptorSet(DescriptorSetLayoutHandle iLayout)
            -> miki::core::Result<DescriptorSetHandle>
            = 0;

        /** @brief Write descriptors into a descriptor set.
         *  @param iSet The descriptor set to update.
         *  @param iWrites Descriptor write operations.
         */
        virtual auto UpdateDescriptorSet(DescriptorSetHandle iSet, std::span<const DescriptorWrite> iWrites) -> void
            = 0;

        // --- Timestamp query ---

        /** @brief Create a timestamp query pool.
         *  @param iCount Number of timestamp slots in the pool.
         *  @return Query pool handle, or error.
         */
        [[nodiscard]] virtual auto CreateTimestampQueryPool(uint32_t iCount) -> miki::core::Result<QueryPoolHandle> = 0;

        /** @brief Read timestamp results from a query pool.
         *  @param iPool The query pool to read from.
         *  @param iResults Output span to write timestamp values into.
         *  @return Void on success, error if results not ready.
         */
        [[nodiscard]] virtual auto GetTimestampResults(QueryPoolHandle iPool, std::span<uint64_t> iResults)
            -> miki::core::Result<void>
            = 0;

        /** @brief Get the timestamp period in nanoseconds.
         *  @return Nanoseconds per timestamp tick. Multiply raw timestamp by this value.
         */
        [[nodiscard]] virtual auto GetTimestampPeriodNs() const noexcept -> double = 0;

        /** @brief Destroy a timestamp query pool.
         *  @param iHandle Handle to the query pool to destroy.
         */
        virtual auto DestroyQueryPool(QueryPoolHandle iHandle) -> void = 0;

        // --- Resource destruction ---

        /** @brief Destroy a texture resource.
         *  @param iHandle Handle to the texture to destroy.
         */
        virtual auto DestroyTexture(TextureHandle iHandle) -> void = 0;

        /** @brief Destroy a buffer resource.
         *  @param iHandle Handle to the buffer to destroy.
         */
        virtual auto DestroyBuffer(BufferHandle iHandle) -> void = 0;

        /** @brief Destroy a pipeline.
         *  @param iHandle Handle to the pipeline to destroy.
         */
        virtual auto DestroyPipeline(PipelineHandle iHandle) -> void = 0;

        /** @brief Destroy a sampler.
         *  @param iHandle Handle to the sampler to destroy.
         */
        virtual auto DestroySampler(SamplerHandle iHandle) -> void = 0;

        /** @brief Destroy a descriptor set layout.
         *  @param iHandle Handle to the layout to destroy.
         */
        virtual auto DestroyDescriptorSetLayout(DescriptorSetLayoutHandle iHandle) -> void = 0;

        /** @brief Destroy a pipeline layout.
         *  @param iHandle Handle to the pipeline layout to destroy.
         */
        virtual auto DestroyPipelineLayout(PipelineLayoutHandle iHandle) -> void = 0;

        /** @brief Destroy a descriptor set.
         *  @param iHandle Handle to the descriptor set to destroy.
         */
        virtual auto DestroyDescriptorSet(DescriptorSetHandle iHandle) -> void = 0;

        // --- Buffer mapping ---

        /** @brief Map a buffer for CPU access.
         *  @param iHandle Handle to the buffer (must be GpuToCpu or CpuToGpu memory).
         *  @return Pointer to the mapped memory, or error.
         */
        [[nodiscard]] virtual auto MapBuffer(BufferHandle iHandle) -> miki::core::Result<void*> = 0;

        /** @brief Unmap a previously mapped buffer.
         *  @param iHandle Handle to the buffer to unmap.
         */
        virtual auto UnmapBuffer(BufferHandle iHandle) -> void = 0;

        /** @brief Flush a range of mapped memory to make CPU writes visible to the GPU.
         *
         *  Required on non-coherent memory (some mobile Vulkan devices).
         *  No-op on HOST_COHERENT memory (all desktop GPUs) and non-Vulkan backends.
         *  Default implementation is a no-op.
         *
         *  @param iHandle Buffer whose mapped memory range to flush.
         *  @param iOffset Byte offset of the range to flush.
         *  @param iSize   Byte size of the range to flush. 0 = flush entire buffer.
         */
        virtual auto FlushMappedRange(BufferHandle iHandle, uint64_t iOffset = 0, uint64_t iSize = 0) -> void;

        /** @brief Invalidate CPU cache for a mapped buffer range after GPU writes.
         *
         *  Must be called before the CPU reads GPU-written data from a
         *  persistently-mapped GpuToCpu buffer on non-coherent memory (e.g. mobile
         *  Vulkan with HOST_CACHED). On coherent memory this is a no-op.
         *  Symmetric counterpart to FlushMappedRange().
         *
         *  Default implementation is a no-op.
         *
         *  @param iHandle Buffer whose mapped memory range to invalidate.
         *  @param iOffset Byte offset of the range to invalidate.
         *  @param iSize   Byte size of the range to invalidate. 0 = entire buffer.
         */
        virtual auto InvalidateMappedRange(BufferHandle iHandle, uint64_t iOffset = 0, uint64_t iSize = 0) -> void;

        /** @brief Write CPU data directly into a GPU buffer.
         *
         * Preferred over Map/Unmap for one-shot uploads on backends where
         * persistent mapping is expensive or unsupported (e.g. WebGPU).
         * Default implementation uses MapBuffer + memcpy + UnmapBuffer.
         *
         *  @param iHandle Destination buffer (must have TransferDst or CopyDst usage).
         *  @param iOffset Byte offset into the destination buffer.
         *  @param iData   Pointer to source data.
         *  @param iSize   Number of bytes to write.
         *  @return Void on success, error on failure.
         */
        [[nodiscard]] virtual auto WriteBuffer(
            BufferHandle iHandle, uint64_t iOffset, const void* iData, uint64_t iSize
        ) -> miki::core::Result<void>;

        /** @brief Write CPU data directly into a GPU texture.
         *
         * Bypasses staging buffers entirely. On WebGPU this maps to
         * queue.writeTexture() which handles row-pitch alignment internally.
         * Default implementation creates a temporary staging buffer + CopyBufferToTexture.
         *
         *  @param iTexture   Destination texture.
         *  @param iData      Pointer to tightly-packed source texel data.
         *  @param iDataSize  Total byte size of source data.
         *  @param iCopyInfo  Copy region descriptor (bufferRowLength in texels, 0 = tight).
         *  @return Void on success, error on failure.
         */
        [[nodiscard]] virtual auto WriteTexture(
            TextureHandle iTexture, const void* iData, uint64_t iDataSize, const BufferTextureCopyInfo& iCopyInfo
        ) -> miki::core::Result<void>;

        // --- Swapchain interop ---

        /** @brief Import an externally-owned swapchain image.
         *  @param iNativeImage Opaque native image handle + backend type.
         */
        [[nodiscard]] virtual auto ImportSwapchainImage(NativeImageHandle iNativeImage)
            -> miki::core::Result<TextureHandle>
            = 0;

        // --- Command submission ---

        /** @brief Create a command buffer for recording GPU commands.
         *  Default overload: creates for the Graphics queue.
         */
        [[nodiscard]] virtual auto CreateCommandBuffer() -> miki::core::Result<std::unique_ptr<ICommandBuffer>> = 0;

        /** @brief Create a command buffer targeting a specific queue type.
         *
         *  Vulkan: allocates from the command pool of the corresponding queue family.
         *  Other backends: iQueue is ignored; behaves identically to the default overload.
         *
         *  @param iQueue Queue type for the command buffer.
         */
        [[nodiscard]] virtual auto CreateCommandBuffer(QueueType iQueue)
            -> miki::core::Result<std::unique_ptr<ICommandBuffer>>;

        /** @brief Submit a recorded command buffer for execution.
         *  @param iCmdBuffer Command buffer to submit.
         *  @param iSync Optional GPU↔GPU synchronization (semaphores).
         *               Default empty = no semaphore wait/signal (same as Phase 1a behavior).
         *               Vulkan: maps to VkSubmitInfo wait/signal semaphores.
         *               Other backends: ignored.
         */
        virtual auto Submit(ICommandBuffer& iCmdBuffer, const SubmitSyncInfo& iSync = {}) -> miki::core::Result<void>
            = 0;

        /** @brief Extended submit with timeline semaphore + queue targeting.
         *
         *  Supersedes Submit() for new code that needs async transfer/compute.
         *  Default implementation delegates to Submit() on the graphics queue,
         *  ignoring timeline fields. Vulkan backend provides full implementation.
         *
         *  @param iCmdBuffer Command buffer to submit.
         *  @param iInfo Extended submit info (queue type, timeline waits/signals).
         */
        [[nodiscard]] virtual auto Submit2(ICommandBuffer& iCmdBuffer, const SubmitInfo2& iInfo)
            -> miki::core::Result<void>;

        /** @brief Wait for all GPU work on all queues to complete. */
        virtual auto WaitIdle() -> void = 0;

        // --- Timeline semaphores ---

        /** @brief Create a timeline semaphore with an initial value.
         *
         *  Vulkan: VkSemaphore with VK_SEMAPHORE_TYPE_TIMELINE.
         *  D3D12:  ID3D12Fence.
         *  Other backends: returns a no-op handle (value tracking only).
         *
         *  @param iInitialValue Initial counter value (typically 0).
         */
        [[nodiscard]] virtual auto CreateTimelineSemaphore(uint64_t iInitialValue = 0)
            -> miki::core::Result<SemaphoreHandle>;

        /** @brief Destroy a timeline semaphore. */
        virtual auto DestroyTimelineSemaphore(SemaphoreHandle iHandle) -> void;

        /** @brief Query the current value of a timeline semaphore (non-blocking).
         *
         *  Returns the highest value signaled by the GPU so far.
         */
        [[nodiscard]] virtual auto GetSemaphoreValue(SemaphoreHandle iHandle) -> miki::core::Result<uint64_t>;

        /** @brief CPU-side blocking wait until a timeline semaphore reaches a value.
         *
         *  @param iHandle   Semaphore to wait on.
         *  @param iValue    Target value.
         *  @param iTimeoutNs Timeout in nanoseconds (UINT64_MAX = infinite).
         */
        [[nodiscard]] virtual auto WaitSemaphoreValue(
            SemaphoreHandle iHandle, uint64_t iValue, uint64_t iTimeoutNs = UINT64_MAX
        ) -> miki::core::Result<void>;

        // --- Queue queries ---

        /** @brief Check if the device has a dedicated compute queue separate from graphics.
         *
         *  When true, the RenderGraph compiler may schedule compute passes on a
         *  separate async compute queue for GPU-GPU overlap with graphics work.
         *  When false, all compute passes run on the graphics queue.
         */
        [[nodiscard]] virtual auto HasDedicatedComputeQueue() const noexcept -> bool { return false; }

        /** @brief Check if the device has a dedicated transfer queue separate from graphics.
         *
         *  When true, TransferQueue can run DMA copies concurrently with rendering.
         *  When false, transfer commands share the graphics queue (still correct, no concurrency).
         */
        [[nodiscard]] virtual auto HasDedicatedTransferQueue() const noexcept -> bool { return false; }

        /** @brief Get the queue family index for a given queue type.
         *
         *  Used by TransferQueue for queue ownership transfer barriers.
         *  Returns UINT32_MAX if the backend does not expose queue families.
         */
        [[nodiscard]] virtual auto GetQueueFamilyIndex(QueueType iQueue) const noexcept -> uint32_t {
            return UINT32_MAX;
        }

        // --- Memory queries ---

        /** @brief Check if the device supports Resizable BAR (device-local + host-visible).
         *
         *  When true, small buffers (uniforms, dirty patches) can be allocated with
         *  MemoryType::DeviceLocalHostVisible for zero-copy CPU-to-VRAM writes.
         *  Vulkan: checks for VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | HOST_VISIBLE_BIT heap.
         *  D3D12: checks for D3D12_HEAP_TYPE_CUSTOM with CPU write combine + GPU read.
         */
        [[nodiscard]] virtual auto HasResizableBAR() const noexcept -> bool { return false; }

        /** @brief Whether the backend supports persistent buffer mapping.
         *
         *  When true, MapBuffer returns a pointer that remains valid until DestroyBuffer.
         *  StagingRing/ChunkPool can hold mapped pointers across frames.
         *
         *  When false (WebGPU), buffers must be unmapped before GPU use. StagingRing
         *  falls back to CPU shadow buffers + IDevice::WriteBuffer().
         *
         *  Default: true (Vulkan, D3D12, OpenGL, Mock all support persistent mapping).
         */
        [[nodiscard]] virtual auto SupportsPersistentMapping() const noexcept -> bool { return true; }

        // --- Queries ---

        /** @brief Get the device's GPU capability profile. */
        [[nodiscard]] virtual auto GetCapabilities() const noexcept -> const GpuCapabilityProfile& = 0;

        /** @brief Get the backend type of this device. */
        [[nodiscard]] virtual auto GetBackendType() const noexcept -> BackendType = 0;

        /** @brief GPU memory budget information from the driver.
         *
         * Maps to VMA vmaGetHeapBudgets / D3D12 QueryVideoMemoryInfo.
         * Backends that don't support budget queries return zeros.
         */
        struct GpuMemoryBudgetInfo {
            uint64_t totalDeviceLocalBytes = 0;  ///< Total device-local heap size.
            uint64_t usedDeviceLocalBytes = 0;   ///< Driver-reported usage.
            uint64_t budgetBytes = 0;            ///< OS-recommended budget (may be < total).
        };

        /** @brief Query the GPU memory budget from the driver/allocator.
         *
         * Default implementation returns zeros. Vulkan backend overrides
         * with vmaGetHeapBudgets. D3D12 backend overrides with DXGI QueryVideoMemoryInfo.
         */
        [[nodiscard]] virtual auto QueryMemoryBudget() const noexcept -> GpuMemoryBudgetInfo { return {}; }

        /** @brief Query the GPU virtual address of a buffer for bindless access.
         *
         *  Vulkan: vkGetBufferDeviceAddress. D3D12: GetGPUVirtualAddress.
         *  GL/WebGPU/Mock: returns 0 (BDA unsupported).
         *
         *  @param iHandle Buffer handle (must have been created with appropriate usage flags).
         *  @return GPU virtual address, or 0 if BDA is not supported/available.
         */
        [[nodiscard]] virtual auto GetBufferDeviceAddress(BufferHandle iHandle) const noexcept -> uint64_t { return 0; }

        /** @brief Type-erased descriptor buffer capability info.
         *
         * Returned by GetDescriptorBufferInfo() on backends that support
         * VK_EXT_descriptor_buffer. Contains opaque device handles needed
         * by DescBufferStrategy::TryCreate(). No Vulkan types in this POD.
         */
        struct DescriptorBufferInfo {
            void* deviceHandle = nullptr;          ///< VkDevice (opaque)
            void* physicalDeviceHandle = nullptr;  ///< VkPhysicalDevice (opaque)
        };

        /** @brief Query descriptor buffer capability handles.
         *
         * Only VulkanDevice overrides this (returns VkDevice + VkPhysicalDevice).
         * All other backends return std::nullopt — caller falls back to
         * descriptor set path.
         *
         * Thread-safe: device handles are immutable after init.
         */
        [[nodiscard]] virtual auto GetDescriptorBufferInfo() const noexcept -> std::optional<DescriptorBufferInfo> {
            return std::nullopt;
        }

       protected:
        IDevice() = default;
    };

}  // namespace miki::rhi
