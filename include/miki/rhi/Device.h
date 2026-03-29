/** @file Device.h
 *  @brief CRTP DeviceBase, DeviceHandle (type-erased facade), DeviceDesc.
 *
 *  DeviceBase<Impl> provides zero-overhead resource creation via CRTP.
 *  DeviceHandle wraps a concrete device with 5-way switch dispatch — used
 *  ONLY by RenderGraph, init code, and profilers (O(passes/frame) ≈ 50-100).
 *
 *  Namespace: miki::rhi
 */
#pragma once

#include <cassert>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "miki/rhi/AccelerationStructure.h"
#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Descriptors.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/Pipeline.h"
#include "miki/rhi/Query.h"
#include "miki/rhi/Resources.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiError.h"
#include "miki/rhi/RhiTypes.h"
#include "miki/rhi/Shader.h"
#include "miki/rhi/Swapchain.h"
#include "miki/rhi/Sync.h"

namespace miki::rhi {

    // Forward declarations
    struct GpuCapabilityProfile;

    // =========================================================================
    // DeviceDesc — device creation parameters
    // =========================================================================

    struct DeviceDesc {
        BackendType backend = BackendType::Vulkan14;
        uint32_t adapterIndex = 0;
        bool enableValidation = false;
        bool enableGpuCapture = false;
        std::span<const char*> requiredExtensions;
    };

    // =========================================================================
    // DeviceBase — CRTP base for all backend devices
    // =========================================================================

    template <typename Impl>
    class DeviceBase {
       public:
        // --- Resource creation ---
        [[nodiscard]] auto CreateBuffer(const BufferDesc& desc) -> RhiResult<BufferHandle> {
            return Self().CreateBufferImpl(desc);
        }
        void DestroyBuffer(BufferHandle h) { Self().DestroyBufferImpl(h); }
        [[nodiscard]] auto MapBuffer(BufferHandle h) -> RhiResult<void*> { return Self().MapBufferImpl(h); }
        void UnmapBuffer(BufferHandle h) { Self().UnmapBufferImpl(h); }
        [[nodiscard]] auto GetBufferDeviceAddress(BufferHandle h) -> uint64_t {
            return Self().GetBufferDeviceAddressImpl(h);
        }

        [[nodiscard]] auto CreateTexture(const TextureDesc& desc) -> RhiResult<TextureHandle> {
            return Self().CreateTextureImpl(desc);
        }
        [[nodiscard]] auto CreateTextureView(const TextureViewDesc& desc) -> RhiResult<TextureViewHandle> {
            return Self().CreateTextureViewImpl(desc);
        }
        void DestroyTextureView(TextureViewHandle h) { Self().DestroyTextureViewImpl(h); }
        void DestroyTexture(TextureHandle h) { Self().DestroyTextureImpl(h); }

        [[nodiscard]] auto CreateSampler(const SamplerDesc& desc) -> RhiResult<SamplerHandle> {
            return Self().CreateSamplerImpl(desc);
        }
        void DestroySampler(SamplerHandle h) { Self().DestroySamplerImpl(h); }

        // --- Memory aliasing ---
        [[nodiscard]] auto CreateMemoryHeap(const MemoryHeapDesc& desc) -> RhiResult<DeviceMemoryHandle> {
            return Self().CreateMemoryHeapImpl(desc);
        }
        void DestroyMemoryHeap(DeviceMemoryHandle h) { Self().DestroyMemoryHeapImpl(h); }
        void AliasBufferMemory(BufferHandle buf, DeviceMemoryHandle heap, uint64_t offset) {
            Self().AliasBufferMemoryImpl(buf, heap, offset);
        }
        void AliasTextureMemory(TextureHandle tex, DeviceMemoryHandle heap, uint64_t offset) {
            Self().AliasTextureMemoryImpl(tex, heap, offset);
        }
        [[nodiscard]] auto GetBufferMemoryRequirements(BufferHandle h) -> MemoryRequirements {
            return Self().GetBufferMemoryRequirementsImpl(h);
        }
        [[nodiscard]] auto GetTextureMemoryRequirements(TextureHandle h) -> MemoryRequirements {
            return Self().GetTextureMemoryRequirementsImpl(h);
        }

        // --- Sparse binding (T1 only) ---
        [[nodiscard]] auto GetSparsePageSize() const -> SparsePageSize { return Self().GetSparsePageSizeImpl(); }
        void SubmitSparseBinds(
            QueueType queue, const SparseBindDesc& binds, std::span<const SemaphoreSubmitInfo> wait = {},
            std::span<const SemaphoreSubmitInfo> signal = {}
        ) {
            Self().SubmitSparseBindsImpl(queue, binds, wait, signal);
        }

        // --- Shader ---
        [[nodiscard]] auto CreateShaderModule(const ShaderModuleDesc& desc) -> RhiResult<ShaderModuleHandle> {
            return Self().CreateShaderModuleImpl(desc);
        }
        void DestroyShaderModule(ShaderModuleHandle h) { Self().DestroyShaderModuleImpl(h); }

        // --- Descriptors ---
        [[nodiscard]] auto CreateDescriptorLayout(const DescriptorLayoutDesc& desc)
            -> RhiResult<DescriptorLayoutHandle> {
            return Self().CreateDescriptorLayoutImpl(desc);
        }
        void DestroyDescriptorLayout(DescriptorLayoutHandle h) { Self().DestroyDescriptorLayoutImpl(h); }
        [[nodiscard]] auto CreatePipelineLayout(const PipelineLayoutDesc& desc) -> RhiResult<PipelineLayoutHandle> {
            return Self().CreatePipelineLayoutImpl(desc);
        }
        void DestroyPipelineLayout(PipelineLayoutHandle h) { Self().DestroyPipelineLayoutImpl(h); }
        [[nodiscard]] auto CreateDescriptorSet(const DescriptorSetDesc& desc) -> RhiResult<DescriptorSetHandle> {
            return Self().CreateDescriptorSetImpl(desc);
        }
        void UpdateDescriptorSet(DescriptorSetHandle h, std::span<const DescriptorWrite> writes) {
            Self().UpdateDescriptorSetImpl(h, writes);
        }
        void DestroyDescriptorSet(DescriptorSetHandle h) { Self().DestroyDescriptorSetImpl(h); }

        // --- Pipelines ---
        [[nodiscard]] auto CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) -> RhiResult<PipelineHandle> {
            return Self().CreateGraphicsPipelineImpl(desc);
        }
        [[nodiscard]] auto CreateComputePipeline(const ComputePipelineDesc& desc) -> RhiResult<PipelineHandle> {
            return Self().CreateComputePipelineImpl(desc);
        }
        [[nodiscard]] auto CreateRayTracingPipeline(const RayTracingPipelineDesc& desc) -> RhiResult<PipelineHandle> {
            return Self().CreateRayTracingPipelineImpl(desc);
        }
        void DestroyPipeline(PipelineHandle h) { Self().DestroyPipelineImpl(h); }

        // --- Pipeline cache ---
        [[nodiscard]] auto CreatePipelineCache(std::span<const uint8_t> initialData = {})
            -> RhiResult<PipelineCacheHandle> {
            return Self().CreatePipelineCacheImpl(initialData);
        }
        [[nodiscard]] auto GetPipelineCacheData(PipelineCacheHandle h) -> std::vector<uint8_t> {
            return Self().GetPipelineCacheDataImpl(h);
        }
        void DestroyPipelineCache(PipelineCacheHandle h) { Self().DestroyPipelineCacheImpl(h); }

        // --- Pipeline library (split compilation) ---
        [[nodiscard]] auto CreatePipelineLibraryPart(const PipelineLibraryPartDesc& desc)
            -> RhiResult<PipelineLibraryPartHandle> {
            return Self().CreatePipelineLibraryPartImpl(desc);
        }
        [[nodiscard]] auto LinkGraphicsPipeline(const LinkedPipelineDesc& desc) -> RhiResult<PipelineHandle> {
            return Self().LinkGraphicsPipelineImpl(desc);
        }

        // --- Command buffers ---
        [[nodiscard]] auto CreateCommandBuffer(const CommandBufferDesc& desc) -> RhiResult<CommandBufferHandle> {
            return Self().CreateCommandBufferImpl(desc);
        }
        void DestroyCommandBuffer(CommandBufferHandle h) { Self().DestroyCommandBufferImpl(h); }

        // --- Synchronization ---
        [[nodiscard]] auto CreateFence(bool signaled = false) -> RhiResult<FenceHandle> {
            return Self().CreateFenceImpl(signaled);
        }
        void DestroyFence(FenceHandle h) { Self().DestroyFenceImpl(h); }
        void WaitFence(FenceHandle h, uint64_t timeout = UINT64_MAX) { Self().WaitFenceImpl(h, timeout); }
        void ResetFence(FenceHandle h) { Self().ResetFenceImpl(h); }
        [[nodiscard]] auto GetFenceStatus(FenceHandle h) -> bool { return Self().GetFenceStatusImpl(h); }

        [[nodiscard]] auto CreateSemaphore(const SemaphoreDesc& desc) -> RhiResult<SemaphoreHandle> {
            return Self().CreateSemaphoreImpl(desc);
        }
        void DestroySemaphore(SemaphoreHandle h) { Self().DestroySemaphoreImpl(h); }
        void SignalSemaphore(SemaphoreHandle h, uint64_t value) { Self().SignalSemaphoreImpl(h, value); }
        void WaitSemaphore(SemaphoreHandle h, uint64_t value, uint64_t timeout) {
            Self().WaitSemaphoreImpl(h, value, timeout);
        }
        [[nodiscard]] auto GetSemaphoreValue(SemaphoreHandle h) -> uint64_t { return Self().GetSemaphoreValueImpl(h); }

        // --- Submission ---
        void Submit(QueueType queue, const SubmitDesc& desc) { Self().SubmitImpl(queue, desc); }
        void WaitIdle() { Self().WaitIdleImpl(); }

        // --- Swapchain ---
        [[nodiscard]] auto CreateSwapchain(const SwapchainDesc& desc) -> RhiResult<SwapchainHandle> {
            return Self().CreateSwapchainImpl(desc);
        }
        void DestroySwapchain(SwapchainHandle h) { Self().DestroySwapchainImpl(h); }
        [[nodiscard]] auto ResizeSwapchain(SwapchainHandle h, uint32_t w, uint32_t ht) -> RhiResult<void> {
            return Self().ResizeSwapchainImpl(h, w, ht);
        }
        [[nodiscard]] auto AcquireNextImage(SwapchainHandle h, SemaphoreHandle signal, FenceHandle fence = {})
            -> RhiResult<uint32_t> {
            return Self().AcquireNextImageImpl(h, signal, fence);
        }
        [[nodiscard]] auto GetSwapchainTexture(SwapchainHandle h, uint32_t imageIndex) -> TextureHandle {
            return Self().GetSwapchainTextureImpl(h, imageIndex);
        }
        void Present(SwapchainHandle h, std::span<const SemaphoreHandle> waitSemaphores) {
            Self().PresentImpl(h, waitSemaphores);
        }

        // --- Query ---
        [[nodiscard]] auto CreateQueryPool(const QueryPoolDesc& desc) -> RhiResult<QueryPoolHandle> {
            return Self().CreateQueryPoolImpl(desc);
        }
        void DestroyQueryPool(QueryPoolHandle h) { Self().DestroyQueryPoolImpl(h); }
        [[nodiscard]] auto GetQueryResults(
            QueryPoolHandle h, uint32_t first, uint32_t count, std::span<uint64_t> results
        ) -> RhiResult<void> {
            return Self().GetQueryResultsImpl(h, first, count, results);
        }
        [[nodiscard]] auto GetTimestampPeriod() -> double { return Self().GetTimestampPeriodImpl(); }

        // --- Acceleration structure (T1 only) ---
        [[nodiscard]] auto GetBLASBuildSizes(const BLASDesc& desc) -> AccelStructBuildSizes {
            return Self().GetBLASBuildSizesImpl(desc);
        }
        [[nodiscard]] auto GetTLASBuildSizes(const TLASDesc& desc) -> AccelStructBuildSizes {
            return Self().GetTLASBuildSizesImpl(desc);
        }
        [[nodiscard]] auto CreateBLAS(const BLASDesc& desc) -> RhiResult<AccelStructHandle> {
            return Self().CreateBLASImpl(desc);
        }
        [[nodiscard]] auto CreateTLAS(const TLASDesc& desc) -> RhiResult<AccelStructHandle> {
            return Self().CreateTLASImpl(desc);
        }
        void DestroyAccelStruct(AccelStructHandle h) { Self().DestroyAccelStructImpl(h); }

        // --- Memory stats (debug/profiling) ---
        [[nodiscard]] auto GetMemoryStats() const -> MemoryStats { return Self().GetMemoryStatsImpl(); }
        [[nodiscard]] auto GetMemoryHeapBudgets(std::span<MemoryHeapBudget> out) const -> uint32_t {
            return Self().GetMemoryHeapBudgetsImpl(out);
        }

        // --- Capability query ---
        [[nodiscard]] auto GetCapabilities() const -> const GpuCapabilityProfile& {
            return Self().GetCapabilitiesImpl();
        }
        [[nodiscard]] auto GetBackendType() const -> BackendType { return Self().GetBackendTypeImpl(); }

       private:
        [[nodiscard]] auto Self() noexcept -> Impl& { return static_cast<Impl&>(*this); }
        [[nodiscard]] auto Self() const noexcept -> const Impl& { return static_cast<const Impl&>(*this); }
    };

    // =========================================================================
    // Forward declarations of concrete backend devices
    // =========================================================================

    class VulkanDevice;
    class D3D12Device;
    class WebGPUDevice;
    class OpenGLDevice;

    // =========================================================================
    // DeviceHandle — type-erased device facade
    // =========================================================================

    /** @brief Thin type-erased wrapper.
     *
     *  Used ONLY by RenderGraph, init code, and debug profilers.
     *  Never in per-draw paths. Dispatch cost: 5-way switch, O(passes/frame).
     */
    class DeviceHandle {
       public:
        DeviceHandle() = default;

        template <typename Impl>
        DeviceHandle(Impl* ptr, BackendType tag) : ptr_(ptr), tag_(tag) {}

        [[nodiscard]] auto IsValid() const noexcept -> bool { return ptr_ != nullptr; }
        [[nodiscard]] auto GetBackendType() const noexcept -> BackendType { return tag_; }

        template <typename F>
        [[nodiscard]] auto Dispatch(F&& fn) -> decltype(auto) {
            assert(ptr_ != nullptr && "DeviceHandle::Dispatch on null handle");
            switch (tag_) {
#if MIKI_BUILD_VULKAN
                case BackendType::Vulkan14: [[fallthrough]];
                case BackendType::VulkanCompat: return fn(*static_cast<VulkanDevice*>(ptr_));
#endif
#if MIKI_BUILD_D3D12
                case BackendType::D3D12: return fn(*static_cast<D3D12Device*>(ptr_));
#endif
#if MIKI_BUILD_WEBGPU
                case BackendType::WebGPU: return fn(*static_cast<WebGPUDevice*>(ptr_));
#endif
#if MIKI_BUILD_OPENGL
                case BackendType::OpenGL43: return fn(*static_cast<OpenGLDevice*>(ptr_));
#endif
                default: break;
            }
            std::unreachable();
        }

        template <typename F>
        [[nodiscard]] auto Dispatch(F&& fn) const -> decltype(auto) {
            assert(ptr_ != nullptr && "DeviceHandle::Dispatch on null handle");
            switch (tag_) {
#if MIKI_BUILD_VULKAN
                case BackendType::Vulkan14: [[fallthrough]];
                case BackendType::VulkanCompat: return fn(*static_cast<const VulkanDevice*>(ptr_));
#endif
#if MIKI_BUILD_D3D12
                case BackendType::D3D12: return fn(*static_cast<const D3D12Device*>(ptr_));
#endif
#if MIKI_BUILD_WEBGPU
                case BackendType::WebGPU: return fn(*static_cast<const WebGPUDevice*>(ptr_));
#endif
#if MIKI_BUILD_OPENGL
                case BackendType::OpenGL43: return fn(*static_cast<const OpenGLDevice*>(ptr_));
#endif
                default: break;
            }
            std::unreachable();
        }

        void Destroy() {
            ptr_ = nullptr;
            tag_ = BackendType::Mock;
        }

       private:
        void* ptr_ = nullptr;
        BackendType tag_ = BackendType::Mock;
    };

}  // namespace miki::rhi
