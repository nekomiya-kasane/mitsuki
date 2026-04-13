/** @file MockDevice.h
 *  @brief Mock backend device for headless testing without a real GPU.
 *
 *  Provides emulated timeline semaphores (CPU-side counter) so that
 *  FrameManager, SyncScheduler and all frame-level tests run against
 *  the unified timeline code path — no tier-specific branching needed.
 *
 *  Namespace: miki::rhi
 */
#pragma once

#include "miki/rhi/Device.h"
#include "miki/rhi/GpuCapabilityProfile.h"
#include "miki/rhi/backend/MockCommandBuffer.h"
#include "miki/frame/SyncScheduler.h"

namespace miki::rhi {

    struct MockDeviceDesc {
        bool enableValidation = false;
    };

    struct MockSemaphoreData {
        uint64_t value = 0;
        SemaphoreType type = SemaphoreType::Binary;
    };

    struct MockFenceData {
        bool signaled = false;
    };

    class MockDevice : public DeviceBase<MockDevice> {
       public:
        MockDevice() = default;
        ~MockDevice() { DestroySyncObjects(); }

        auto Init(const MockDeviceDesc& = {}) -> RhiResult<void> {
            caps_.tier = CapabilityTier::Tier4_OpenGL;
            caps_.backendType = BackendType::Mock;
            caps_.hasTimelineSemaphore = true;  // Emulated
            caps_.deviceName = "miki Mock Device";
            caps_.driverVersion = "1.0.0";
            caps_.vendorId = 0xFFFF;
            caps_.deviceId = 0x0001;
            caps_.maxColorAttachments = 8;
            caps_.maxTextureSize2D = 16384;
            caps_.maxTextureSizeCube = 16384;
            caps_.maxFramebufferWidth = 16384;
            caps_.maxFramebufferHeight = 16384;
            caps_.maxViewports = 16;
            caps_.maxPushConstantSize = kMinPushConstantSize;
            caps_.maxBoundDescriptorSets = 4;
            caps_.formatSupport = GpuCapabilityProfile::BuildDefaultFormatSupport();
            CreateQueueTimelines();
            return {};
        }

        // --- Resource creation (synthetic handles for headless testing) ---
        auto CreateBufferImpl(const BufferDesc&) -> RhiResult<BufferHandle> { return BufferHandle{++nextHandle_}; }
        void DestroyBufferImpl(BufferHandle) {}
        auto MapBufferImpl(BufferHandle) -> RhiResult<void*> { return std::unexpected(RhiError::NotImplemented); }
        void UnmapBufferImpl(BufferHandle) {}
        void FlushMappedRangeImpl(BufferHandle, uint64_t, uint64_t) {}
        void InvalidateMappedRangeImpl(BufferHandle, uint64_t, uint64_t) {}
        auto GetBufferDeviceAddressImpl(BufferHandle) -> uint64_t { return 0; }

        auto CreateTextureImpl(const TextureDesc&) -> RhiResult<TextureHandle> { return TextureHandle{++nextHandle_}; }
        auto CreateTextureViewImpl(const TextureViewDesc&) -> RhiResult<TextureViewHandle> {
            return TextureViewHandle{++nextHandle_};
        }
        auto GetTextureViewTextureImpl(TextureViewHandle) -> TextureHandle { return {}; }
        void DestroyTextureViewImpl(TextureViewHandle) {}
        void DestroyTextureImpl(TextureHandle) {}

        auto CreateSamplerImpl(const SamplerDesc&) -> RhiResult<SamplerHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        void DestroySamplerImpl(SamplerHandle) {}

        // --- Memory aliasing (synthetic heaps) ---
        auto CreateMemoryHeapImpl(const MemoryHeapDesc&) -> RhiResult<DeviceMemoryHandle> {
            return DeviceMemoryHandle{++nextHandle_};
        }
        void DestroyMemoryHeapImpl(DeviceMemoryHandle) {}
        void AliasBufferMemoryImpl(BufferHandle, DeviceMemoryHandle, uint64_t) {}
        void AliasTextureMemoryImpl(TextureHandle, DeviceMemoryHandle, uint64_t) {}
        auto GetBufferMemoryRequirementsImpl(BufferHandle) -> MemoryRequirements { return {}; }
        auto GetTextureMemoryRequirementsImpl(TextureHandle) -> MemoryRequirements { return {}; }

        // --- Sparse binding ---
        auto GetSparsePageSizeImpl() const -> SparsePageSize { return {}; }
        void SubmitSparseBindsImpl(
            QueueType, const SparseBindDesc&, std::span<const SemaphoreSubmitInfo>, std::span<const SemaphoreSubmitInfo>
        ) {}

        // --- Shader ---
        auto CreateShaderModuleImpl(const ShaderModuleDesc&) -> RhiResult<ShaderModuleHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        void DestroyShaderModuleImpl(ShaderModuleHandle) {}

        // --- Descriptors ---
        auto CreateDescriptorLayoutImpl(const DescriptorLayoutDesc&) -> RhiResult<DescriptorLayoutHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        void DestroyDescriptorLayoutImpl(DescriptorLayoutHandle) {}
        auto CreatePipelineLayoutImpl(const PipelineLayoutDesc&) -> RhiResult<PipelineLayoutHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        void DestroyPipelineLayoutImpl(PipelineLayoutHandle) {}
        auto CreateDescriptorSetImpl(const DescriptorSetDesc&) -> RhiResult<DescriptorSetHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        void UpdateDescriptorSetImpl(DescriptorSetHandle, std::span<const DescriptorWrite>) {}
        void DestroyDescriptorSetImpl(DescriptorSetHandle) {}

        // --- Pipelines ---
        auto CreateGraphicsPipelineImpl(const GraphicsPipelineDesc&) -> RhiResult<PipelineHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        auto CreateComputePipelineImpl(const ComputePipelineDesc&) -> RhiResult<PipelineHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        auto CreateRayTracingPipelineImpl(const RayTracingPipelineDesc&) -> RhiResult<PipelineHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        void DestroyPipelineImpl(PipelineHandle) {}

        // --- Pipeline cache ---
        auto CreatePipelineCacheImpl(std::span<const uint8_t>) -> RhiResult<PipelineCacheHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        auto GetPipelineCacheDataImpl(PipelineCacheHandle) -> std::vector<uint8_t> { return {}; }
        void DestroyPipelineCacheImpl(PipelineCacheHandle) {}

        // --- Pipeline library ---
        auto CreatePipelineLibraryPartImpl(const PipelineLibraryPartDesc&) -> RhiResult<PipelineLibraryPartHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        auto LinkGraphicsPipelineImpl(const LinkedPipelineDesc&) -> RhiResult<PipelineHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        void DestroyPipelineLibraryPartImpl(PipelineLibraryPartHandle) {}

        // --- Command pools (return synthetic pools + MockCommandBuffer) ---
        auto CreateCommandPoolImpl(const CommandPoolDesc&) -> RhiResult<CommandPoolHandle> {
            return CommandPoolHandle{++nextHandle_};
        }
        void DestroyCommandPoolImpl(CommandPoolHandle) {}
        void ResetCommandPoolImpl(CommandPoolHandle, CommandPoolResetFlags) {}
        auto AllocateFromPoolImpl(CommandPoolHandle, bool) -> RhiResult<CommandListAcquisition> {
            auto& buf = mockCmdBufs_.emplace_back();
            return CommandListAcquisition{
                .bufferHandle = CommandBufferHandle{++nextHandle_},
                .listHandle = CommandListHandle(&buf, BackendType::Mock),
            };
        }

        // --- Query ---
        auto CreateQueryPoolImpl(const QueryPoolDesc&) -> RhiResult<QueryPoolHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        void DestroyQueryPoolImpl(QueryPoolHandle) {}
        auto GetQueryResultsImpl(QueryPoolHandle, uint32_t, uint32_t, std::span<uint64_t>) -> RhiResult<void> {
            return std::unexpected(RhiError::NotImplemented);
        }
        auto GetTimestampPeriodImpl() -> double { return 0.0; }

        // --- Acceleration structure ---
        auto GetBLASBuildSizesImpl(const BLASDesc&) -> AccelStructBuildSizes { return {}; }
        auto GetTLASBuildSizesImpl(const TLASDesc&) -> AccelStructBuildSizes { return {}; }
        auto CreateBLASImpl(const BLASDesc&) -> RhiResult<AccelStructHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        auto CreateTLASImpl(const TLASDesc&) -> RhiResult<AccelStructHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        void DestroyAccelStructImpl(AccelStructHandle) {}

        // --- Memory stats ---
        auto GetMemoryStatsImpl() const -> MemoryStats { return {}; }
        auto GetMemoryHeapBudgetsImpl(std::span<MemoryHeapBudget>) const -> uint32_t { return 0; }

        // --- Debug names ---
        void SetObjectDebugNameImpl(SemaphoreHandle, const char*) {}
        void SetObjectDebugNameImpl(FenceHandle, const char*) {}
        void SetObjectDebugNameImpl(BufferHandle, const char*) {}
        void SetObjectDebugNameImpl(TextureHandle, const char*) {}
        void SetObjectDebugNameImpl(TextureViewHandle, const char*) {}
        void SetObjectDebugNameImpl(SamplerHandle, const char*) {}
        void SetObjectDebugNameImpl(ShaderModuleHandle, const char*) {}
        void SetObjectDebugNameImpl(PipelineHandle, const char*) {}
        void SetObjectDebugNameImpl(PipelineLayoutHandle, const char*) {}
        void SetObjectDebugNameImpl(DescriptorLayoutHandle, const char*) {}
        void SetObjectDebugNameImpl(DescriptorSetHandle, const char*) {}
        void SetObjectDebugNameImpl(QueryPoolHandle, const char*) {}
        void SetObjectDebugNameImpl(AccelStructHandle, const char*) {}
        void SetObjectDebugNameImpl(CommandPoolHandle, const char*) {}
        void SetObjectDebugNameImpl(CommandBufferHandle, const char*) {}
        void SetObjectDebugNameImpl(SwapchainHandle, const char*) {}
        [[nodiscard]] auto GetObjectDebugNameImpl(SemaphoreHandle) const -> const char* { return "(unnamed)"; }
        [[nodiscard]] auto GetObjectDebugNameImpl(FenceHandle) const -> const char* { return "(unnamed)"; }
        [[nodiscard]] auto GetObjectDebugNameImpl(BufferHandle) const -> const char* { return "(unnamed)"; }
        [[nodiscard]] auto GetObjectDebugNameImpl(TextureHandle) const -> const char* { return "(unnamed)"; }
        [[nodiscard]] auto GetObjectDebugNameImpl(TextureViewHandle) const -> const char* { return "(unnamed)"; }
        [[nodiscard]] auto GetObjectDebugNameImpl(SamplerHandle) const -> const char* { return "(unnamed)"; }
        [[nodiscard]] auto GetObjectDebugNameImpl(ShaderModuleHandle) const -> const char* { return "(unnamed)"; }
        [[nodiscard]] auto GetObjectDebugNameImpl(PipelineHandle) const -> const char* { return "(unnamed)"; }
        [[nodiscard]] auto GetObjectDebugNameImpl(PipelineLayoutHandle) const -> const char* { return "(unnamed)"; }
        [[nodiscard]] auto GetObjectDebugNameImpl(DescriptorLayoutHandle) const -> const char* { return "(unnamed)"; }
        [[nodiscard]] auto GetObjectDebugNameImpl(DescriptorSetHandle) const -> const char* { return "(unnamed)"; }
        [[nodiscard]] auto GetObjectDebugNameImpl(QueryPoolHandle) const -> const char* { return "(unnamed)"; }
        [[nodiscard]] auto GetObjectDebugNameImpl(AccelStructHandle) const -> const char* { return "(unnamed)"; }
        [[nodiscard]] auto GetObjectDebugNameImpl(CommandPoolHandle) const -> const char* { return "(unnamed)"; }
        [[nodiscard]] auto GetObjectDebugNameImpl(CommandBufferHandle) const -> const char* { return "(unnamed)"; }
        [[nodiscard]] auto GetObjectDebugNameImpl(SwapchainHandle) const -> const char* { return "(unnamed)"; }

        // --- Surface capabilities ---
        auto GetSurfaceCapabilitiesImpl(const NativeWindowHandle&) const -> RenderSurfaceCapabilities { return {}; }

        // --- Synchronization (emulated timeline) ---
        auto CreateFenceImpl(bool signaled) -> RhiResult<FenceHandle> {
            auto [handle, data] = fences_.Allocate();
            if (!handle.IsValid()) {
                return std::unexpected(RhiError::TooManyObjects);
            }
            data->signaled = signaled;
            return handle;
        }
        void DestroyFenceImpl(FenceHandle h) { fences_.Free(h); }
        void WaitFenceImpl(FenceHandle h, uint64_t) {
            auto* d = fences_.Lookup(h);
            if (d) {
                d->signaled = true;  // Mock: instant completion
            }
        }
        void ResetFenceImpl(FenceHandle h) {
            auto* d = fences_.Lookup(h);
            if (d) {
                d->signaled = false;
            }
        }
        auto GetFenceStatusImpl(FenceHandle h) -> bool {
            auto* d = fences_.Lookup(h);
            return d ? d->signaled : false;
        }

        auto CreateSemaphoreImpl(const SemaphoreDesc& desc) -> RhiResult<SemaphoreHandle> {
            auto [handle, data] = semaphores_.Allocate();
            if (!handle.IsValid()) {
                return std::unexpected(RhiError::TooManyObjects);
            }
            data->value = desc.initialValue;
            data->type = desc.type;
            return handle;
        }
        void DestroySemaphoreImpl(SemaphoreHandle h) { semaphores_.Free(h); }
        void SignalSemaphoreImpl(SemaphoreHandle h, uint64_t value) {
            auto* d = semaphores_.Lookup(h);
            if (d) {
                d->value = value;
            }
        }
        void WaitSemaphoreImpl(SemaphoreHandle h, uint64_t /*value*/, uint64_t /*timeout*/) {
            // Mock: instant completion — just mark the value as reached
            (void)h;
        }
        auto GetSemaphoreValueImpl(SemaphoreHandle h) -> uint64_t {
            auto* d = semaphores_.Lookup(h);
            return d ? d->value : 0;
        }

        // --- Submission (emulated: signal semaphores + fence immediately) ---
        void SubmitImpl(QueueType, const SubmitDesc& desc) {
            for (const auto& s : desc.signalSemaphores) {
                auto* d = semaphores_.Lookup(s.semaphore);
                if (d) {
                    d->value = s.value;
                }
            }
            if (desc.signalFence.IsValid()) {
                auto* d = fences_.Lookup(desc.signalFence);
                if (d) {
                    d->signaled = true;
                }
            }
        }
        void WaitIdleImpl() {}

        // --- Swapchain ---
        auto CreateSwapchainImpl(const SwapchainDesc&) -> RhiResult<SwapchainHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        void DestroySwapchainImpl(SwapchainHandle) {}
        auto ResizeSwapchainImpl(SwapchainHandle, uint32_t, uint32_t) -> RhiResult<void> {
            return std::unexpected(RhiError::NotImplemented);
        }
        auto AcquireNextImageImpl(SwapchainHandle, SemaphoreHandle, FenceHandle) -> RhiResult<uint32_t> {
            return std::unexpected(RhiError::NotImplemented);
        }
        [[nodiscard]] auto GetSwapchainTextureImpl(SwapchainHandle, uint32_t) -> TextureHandle { return {}; }
        [[nodiscard]] auto GetSwapchainTextureViewImpl(SwapchainHandle, uint32_t) -> TextureViewHandle { return {}; }
        [[nodiscard]] auto GetSwapchainImageCountImpl(SwapchainHandle) -> uint32_t { return 0; }
        void PresentImpl(SwapchainHandle, std::span<const SemaphoreHandle>) {}

        // --- Compile-time backend identity ---
        static constexpr BackendType kBackendType = BackendType::Mock;

        // --- Capability ---
        [[nodiscard]] auto GetCapabilitiesImpl() const -> const GpuCapabilityProfile& { return caps_; }
        [[nodiscard]] auto GetBackendTypeImpl() const -> BackendType { return kBackendType; }
        [[nodiscard]] auto GetQueueTimelinesImpl() const -> QueueTimelines { return queueTimelines_; }
        [[nodiscard]] auto GetSyncSchedulerImpl() noexcept -> frame::SyncScheduler& { return syncScheduler_; }
        [[nodiscard]] auto GetSyncSchedulerImpl() const noexcept -> const frame::SyncScheduler& {
            return syncScheduler_;
        }

       private:
        void CreateQueueTimelines() {
            SemaphoreDesc desc{.type = SemaphoreType::Timeline, .initialValue = 0};
            if (auto r = CreateSemaphoreImpl(desc)) {
                queueTimelines_.graphics = *r;
            }
            if (auto r = CreateSemaphoreImpl(desc)) {
                queueTimelines_.compute = *r;
            }
            if (auto r = CreateSemaphoreImpl(desc)) {
                queueTimelines_.transfer = *r;
            }
            syncScheduler_.Init(queueTimelines_);
        }
        void DestroySyncObjects() {
            if (queueTimelines_.graphics.IsValid()) {
                DestroySemaphoreImpl(queueTimelines_.graphics);
            }
            if (queueTimelines_.compute.IsValid()) {
                DestroySemaphoreImpl(queueTimelines_.compute);
            }
            if (queueTimelines_.transfer.IsValid()) {
                DestroySemaphoreImpl(queueTimelines_.transfer);
            }
            queueTimelines_ = {};
        }

        GpuCapabilityProfile caps_;
        QueueTimelines queueTimelines_;
        frame::SyncScheduler syncScheduler_;
        HandlePool<MockSemaphoreData, SemaphoreTag, kMaxSemaphores> semaphores_;
        HandlePool<MockFenceData, FenceTag, kMaxFences> fences_;
        static constexpr uint64_t kMockHandleBase = 1000;
        uint64_t nextHandle_ = kMockHandleBase;       ///< Counter for synthetic resource handles
        std::vector<MockCommandBuffer> mockCmdBufs_;  ///< Storage for allocated mock command buffers
    };

}  // namespace miki::rhi
