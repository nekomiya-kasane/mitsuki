/** @file MockDevice.h
 *  @brief Mock backend device for headless testing without a real GPU.
 *
 *  Uses MIKI_DEVICE_STUB_IMPL to provide no-op implementations for all
 *  DeviceBase methods, then overrides capability queries with meaningful
 *  mock values so tests that check device name, format support, etc. pass.
 *
 *  Namespace: miki::rhi
 */
#pragma once

#include "miki/rhi/GpuCapabilityProfile.h"
#include "miki/rhi/backend/BackendStub.h"

namespace miki::rhi {

    struct MockDeviceDesc {
        bool enableValidation = false;
    };

    class MockDevice : public DeviceBase<MockDevice> {
       public:
        MockDevice() = default;

        auto Init(const MockDeviceDesc& = {}) -> RhiResult<void> {
            caps_.tier = CapabilityTier::Tier4_OpenGL;
            caps_.backendType = BackendType::Mock;
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
            caps_.maxPushConstantSize = 128;
            caps_.maxBoundDescriptorSets = 4;
            caps_.formatSupport = GpuCapabilityProfile::BuildDefaultFormatSupport();
            return {};
        }

        // Resource/pipeline/query/accel stubs (all return NotImplemented or no-op)
        MIKI_DEVICE_STUB_RESOURCE_IMPL

        // --- Synchronization ---
        auto CreateFenceImpl(bool) -> RhiResult<FenceHandle> { return std::unexpected(RhiError::NotImplemented); }
        void DestroyFenceImpl(FenceHandle) {}
        void WaitFenceImpl(FenceHandle, uint64_t) {}
        void ResetFenceImpl(FenceHandle) {}
        auto GetFenceStatusImpl(FenceHandle) -> bool { return false; }
        auto CreateSemaphoreImpl(const SemaphoreDesc&) -> RhiResult<SemaphoreHandle> {
            return std::unexpected(RhiError::NotImplemented);
        }
        void DestroySemaphoreImpl(SemaphoreHandle) {}
        void SignalSemaphoreImpl(SemaphoreHandle, uint64_t) {}
        void WaitSemaphoreImpl(SemaphoreHandle, uint64_t, uint64_t) {}
        auto GetSemaphoreValueImpl(SemaphoreHandle) -> uint64_t { return 0; }

        // --- Submission ---
        void SubmitImpl(QueueType, const SubmitDesc&) {}
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
        [[nodiscard]] auto GetQueueTimelinesImpl() const -> QueueTimelines { return {}; }

       private:
        GpuCapabilityProfile caps_;
    };

}  // namespace miki::rhi
