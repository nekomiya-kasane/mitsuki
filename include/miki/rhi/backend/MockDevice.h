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

#include "miki/rhi/GpuCapabilityProfile.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/backend/BackendStub.h"

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

        // Resource/pipeline/query/accel stubs (all return NotImplemented or no-op)
        MIKI_DEVICE_STUB_RESOURCE_IMPL

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
        HandlePool<MockSemaphoreData, SemaphoreTag, kMaxSemaphores> semaphores_;
        HandlePool<MockFenceData, FenceTag, kMaxFences> fences_;
    };

}  // namespace miki::rhi
