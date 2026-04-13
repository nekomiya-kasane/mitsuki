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
#include <string_view>
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
#include "miki/rhi/validation/RhiValidation.h"
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

        // OpenGL-specific: required for GL context creation via GLFW
        void* windowBackend = nullptr;  ///< platform::IWindowBackend* (opaque to avoid header dep)
        void* nativeToken = nullptr;    ///< Native window token from backend

        // Vulkan-specific
        const char* appName = "miki";
    };

    // =========================================================================
    // CommandListAcquisition — result of AcquireCommandList
    // =========================================================================

    /// Returned by DeviceBase::AcquireCommandList. Pairs an opaque buffer handle
    /// (for Submit/Destroy) with a type-erased recordable command list.
    struct CommandListAcquisition {
        CommandBufferHandle bufferHandle;  ///< Opaque handle for Submit/Destroy
        CommandListHandle listHandle;      ///< Type-erased recordable command list
    };

    // =========================================================================
    // DeviceBase — CRTP base for all backend devices
    // =========================================================================

    /// Concept: does Impl expose a static constexpr kBackendType?
    template <typename T>
    concept HasStaticBackendType = requires {
        { T::kBackendType } -> std::convertible_to<BackendType>;
    };

    template <typename Impl>
    class DeviceBase {
       public:
        // --- Resource creation ---
        [[nodiscard]] auto CreateBuffer(const BufferDesc& desc) -> RhiResult<BufferHandle> {
            if constexpr (HasStaticBackendType<Impl>) {
                if (!validation::ValidateBufferDesc(Impl::kBackendType, desc)) {
                    return std::unexpected(RhiError::InvalidParameter);
                }
            } else {
                if (!validation::ValidateBufferDesc(Self().GetBackendTypeImpl(), desc)) {
                    return std::unexpected(RhiError::InvalidParameter);
                }
            }
            return Self().CreateBufferImpl(desc);
        }
        void DestroyBuffer(BufferHandle h) { Self().DestroyBufferImpl(h); }
        [[nodiscard]] auto MapBuffer(BufferHandle h) -> RhiResult<void*> { return Self().MapBufferImpl(h); }
        void UnmapBuffer(BufferHandle h) { Self().UnmapBufferImpl(h); }
        /// @brief Flush CPU writes to make them visible to GPU (non-coherent memory).
        /// No-op on coherent memory (desktop Vulkan VMA default, D3D12 UPLOAD, GL persistent+coherent).
        /// Required on mobile Vulkan HOST_CACHED memory.
        void FlushMappedRange(BufferHandle h, uint64_t offset, uint64_t size) {
            Self().FlushMappedRangeImpl(h, offset, size);
        }
        /// @brief Invalidate CPU cache to see GPU writes (non-coherent readback memory).
        /// No-op on coherent memory. Required on mobile Vulkan HOST_CACHED readback buffers.
        void InvalidateMappedRange(BufferHandle h, uint64_t offset, uint64_t size) {
            Self().InvalidateMappedRangeImpl(h, offset, size);
        }
        [[nodiscard]] auto GetBufferDeviceAddress(BufferHandle h) -> uint64_t {
            return Self().GetBufferDeviceAddressImpl(h);
        }

        [[nodiscard]] auto CreateTexture(const TextureDesc& desc) -> RhiResult<TextureHandle> {
            if constexpr (HasStaticBackendType<Impl>) {
                if (!validation::ValidateTextureDesc(Impl::kBackendType, desc)) {
                    return std::unexpected(RhiError::InvalidParameter);
                }
            } else {
                if (!validation::ValidateTextureDesc(Self().GetBackendTypeImpl(), desc)) {
                    return std::unexpected(RhiError::InvalidParameter);
                }
            }
            return Self().CreateTextureImpl(desc);
        }
        [[nodiscard]] auto CreateTextureView(const TextureViewDesc& desc) -> RhiResult<TextureViewHandle> {
            return Self().CreateTextureViewImpl(desc);
        }
        [[nodiscard]] auto GetTextureViewTexture(TextureViewHandle h) -> TextureHandle {
            return Self().GetTextureViewTextureImpl(h);
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
        void DestroyPipelineLibraryPart(PipelineLibraryPartHandle h) { Self().DestroyPipelineLibraryPartImpl(h); }

        // --- Command pool management (§19 — pool-level API for CommandPoolAllocator) ---
        [[nodiscard]] auto CreateCommandPool(const CommandPoolDesc& desc) -> RhiResult<CommandPoolHandle> {
            return Self().CreateCommandPoolImpl(desc);
        }
        void DestroyCommandPool(CommandPoolHandle pool) { Self().DestroyCommandPoolImpl(pool); }
        void ResetCommandPool(CommandPoolHandle pool, CommandPoolResetFlags flags = CommandPoolResetFlags::None) {
            Self().ResetCommandPoolImpl(pool, flags);
        }
        [[nodiscard]] auto AllocateFromPool(CommandPoolHandle pool, bool secondary = false)
            -> RhiResult<CommandListAcquisition> {
            return Self().AllocateFromPoolImpl(pool, secondary);
        }

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
        [[nodiscard]] auto GetQueueTimelines() const -> QueueTimelines { return Self().GetQueueTimelinesImpl(); }

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
        [[nodiscard]] auto GetSwapchainTextureView(SwapchainHandle h, uint32_t imageIndex) -> TextureViewHandle {
            return Self().GetSwapchainTextureViewImpl(h, imageIndex);
        }
        [[nodiscard]] auto GetSwapchainImageCount(SwapchainHandle h) -> uint32_t {
            return Self().GetSwapchainImageCountImpl(h);
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

        // --- Debug names (non-invasive, debug builds only) ---

        /// @brief Attach a semantic debug name to any RHI handle.
        /// Calls the backend-specific object naming API (vkSetDebugUtilsObjectNameEXT / ID3D12Object::SetName)
        /// and stores the name in the HandlePool for log resolution.
        /// @param handle Valid handle returned by a Create* function.
        /// @param name   String literal or pointer with lifetime >= handle lifetime.
        void SetObjectDebugName(SemaphoreHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }
        void SetObjectDebugName(FenceHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }
        void SetObjectDebugName(BufferHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }
        void SetObjectDebugName(TextureHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }
        void SetObjectDebugName(TextureViewHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }
        void SetObjectDebugName(SamplerHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }
        void SetObjectDebugName(ShaderModuleHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }
        void SetObjectDebugName(PipelineHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }
        void SetObjectDebugName(PipelineLayoutHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }
        void SetObjectDebugName(DescriptorLayoutHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }
        void SetObjectDebugName(DescriptorSetHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }
        void SetObjectDebugName(QueryPoolHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }
        void SetObjectDebugName(AccelStructHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }
        void SetObjectDebugName(CommandPoolHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }
        void SetObjectDebugName(CommandBufferHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }
        void SetObjectDebugName(SwapchainHandle h, const char* name) { Self().SetObjectDebugNameImpl(h, name); }

        /// @brief Resolve debug name for any handle. Returns "(unnamed)" if not set or in Release.
        [[nodiscard]] auto GetObjectDebugName(SemaphoreHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }
        [[nodiscard]] auto GetObjectDebugName(FenceHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }
        [[nodiscard]] auto GetObjectDebugName(BufferHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }
        [[nodiscard]] auto GetObjectDebugName(TextureHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }
        [[nodiscard]] auto GetObjectDebugName(TextureViewHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }
        [[nodiscard]] auto GetObjectDebugName(SamplerHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }
        [[nodiscard]] auto GetObjectDebugName(ShaderModuleHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }
        [[nodiscard]] auto GetObjectDebugName(PipelineHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }
        [[nodiscard]] auto GetObjectDebugName(PipelineLayoutHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }
        [[nodiscard]] auto GetObjectDebugName(DescriptorLayoutHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }
        [[nodiscard]] auto GetObjectDebugName(DescriptorSetHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }
        [[nodiscard]] auto GetObjectDebugName(QueryPoolHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }
        [[nodiscard]] auto GetObjectDebugName(AccelStructHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }
        [[nodiscard]] auto GetObjectDebugName(CommandPoolHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }
        [[nodiscard]] auto GetObjectDebugName(CommandBufferHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }
        [[nodiscard]] auto GetObjectDebugName(SwapchainHandle h) const -> const char* {
            return Self().GetObjectDebugNameImpl(h);
        }

        // --- Capability query ---
        [[nodiscard]] auto GetCapabilities() const -> const GpuCapabilityProfile& {
            return Self().GetCapabilitiesImpl();
        }
        [[nodiscard]] auto GetBackendType() const -> BackendType { return Self().GetBackendTypeImpl(); }

        // --- Surface capabilities (for RenderSurface) ---
        [[nodiscard]] auto GetSurfaceCapabilities(const NativeWindowHandle& window) const -> RenderSurfaceCapabilities {
            return Self().GetSurfaceCapabilitiesImpl(window);
        }

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
    class MockDevice;

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
                case BackendType::Mock: return fn(*static_cast<MockDevice*>(ptr_));
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
                case BackendType::Mock: return fn(*static_cast<const MockDevice*>(ptr_));
                default: break;
            }
            std::unreachable();
        }

        [[nodiscard]] auto GetDeviceName() const noexcept -> std::string_view;

        [[nodiscard]] constexpr auto GetBackendName() const noexcept -> const char* {
            switch (tag_) {
                case BackendType::Vulkan14: return "Vulkan14";
                case BackendType::VulkanCompat: return "VulkanCompat";
                case BackendType::D3D12: return "D3D12";
                case BackendType::WebGPU: return "WebGPU";
                case BackendType::OpenGL43: return "OpenGL43";
                case BackendType::Mock: return "Mock";
            }
            return "Unknown";
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
