/** @file UploadManager.cpp
 *  @brief 4-tier upload routing for CPU→GPU data transfer.
 *  See: UploadManager.h, specs/03-sync.md §7.1.1
 */

#include "miki/resource/UploadManager.h"

#include <cassert>
#include <cstring>

#include "miki/frame/DeferredDestructor.h"
#include "miki/resource/StagingRing.h"
#include "miki/rhi/backend/AllBackends.h"

namespace miki::resource {

    struct UploadManager::Impl {
        rhi::DeviceHandle device;
        StagingRing* stagingRing = nullptr;
        frame::DeferredDestructor* deferredDestructor = nullptr;
        bool rebarAvailable = false;
        uint64_t rebarBudget = 0;
        uint64_t frameNumber = 0;
    };

    UploadManager::~UploadManager() = default;
    UploadManager::UploadManager(UploadManager&&) noexcept = default;
    auto UploadManager::operator=(UploadManager&&) noexcept -> UploadManager& = default;
    UploadManager::UploadManager(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    auto UploadManager::Create(
        rhi::DeviceHandle iDevice, StagingRing* iStagingRing, frame::DeferredDestructor* iDeferredDestructor,
        bool iRebarAvailable, uint64_t iRebarBudget
    ) -> core::Result<UploadManager> {
        if (!iDevice.IsValid() || !iStagingRing) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        auto impl = std::make_unique<Impl>();
        impl->device = iDevice;
        impl->stagingRing = iStagingRing;
        impl->deferredDestructor = iDeferredDestructor;
        impl->rebarAvailable = iRebarAvailable;
        impl->rebarBudget = iRebarBudget;
        return UploadManager(std::move(impl));
    }

    auto UploadManager::Upload(rhi::BufferHandle iDst, uint64_t iDstOffset, const void* iData, uint64_t iSize)
        -> core::Result<UploadResult> {
        assert(impl_ && "UploadManager used after move");
        if (iSize == 0 || !iData) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        // Path D: Direct VRAM write via ReBAR (largest data, zero copy)
        if (impl_->rebarAvailable && iSize > impl_->stagingRing->Capacity() && iSize <= impl_->rebarBudget) {
            auto mapResult = impl_->device.Dispatch([&](auto& dev) { return dev.MapBuffer(iDst); });
            if (mapResult) {
                std::memcpy(static_cast<uint8_t*>(*mapResult) + iDstOffset, iData, iSize);
                impl_->device.Dispatch([&](auto& dev) { dev.UnmapBuffer(iDst); });
                return UploadResult{.path = UploadPath::DirectVRAM, .size = iSize};
            }
            // Fall through to Path C if map fails
        }

        // Path C: Dedicated staging buffer (> ring capacity)
        if (iSize > impl_->stagingRing->Capacity()) {
            auto bufResult = impl_->device.Dispatch([&](auto& dev) {
                rhi::BufferDesc desc{
                    .size = iSize, .usage = rhi::BufferUsage::TransferSrc, .memory = rhi::MemoryLocation::CpuToGpu
                };
                return dev.CreateBuffer(desc);
            });
            if (!bufResult) {
                return std::unexpected(core::ErrorCode::OutOfMemory);
            }

            auto mapResult = impl_->device.Dispatch([&](auto& dev) { return dev.MapBuffer(*bufResult); });
            if (!mapResult) {
                impl_->device.Dispatch([&](auto& dev) { dev.DestroyBuffer(*bufResult); });
                return std::unexpected(core::ErrorCode::OutOfMemory);
            }
            std::memcpy(*mapResult, iData, iSize);
            impl_->device.Dispatch([&](auto& dev) {
                dev.FlushMappedRange(*bufResult, 0, iSize);
                dev.UnmapBuffer(*bufResult);
            });

            // Enqueue for deferred destruction after GPU consumes the copy
            if (impl_->deferredDestructor) {
                impl_->deferredDestructor->Destroy(*bufResult);
            }

            return UploadResult{
                .path = UploadPath::DedicatedBuffer, .stagingBuffer = *bufResult, .stagingOffset = 0, .size = iSize
            };
        }

        // Path A/B: StagingRing (small or medium data)
        auto allocResult = impl_->stagingRing->Allocate(iSize, kStagingAlignment);
        if (!allocResult) {
            return std::unexpected(core::ErrorCode::OutOfMemory);
        }

        std::memcpy(allocResult->mappedPtr, iData, iSize);
        impl_->stagingRing->EnqueueBufferCopy(*allocResult, iDst, iDstOffset);

        UploadPath path = (iSize <= kStagingRingThreshold) ? UploadPath::StagingRing : UploadPath::StagingRingLarge;
        return UploadResult{.path = path, .size = iSize};
    }

    auto UploadManager::UploadTexture(
        std::span<const std::byte> iData, rhi::TextureHandle iDst, const TextureUploadRegion& iRegion
    ) -> core::Result<void> {
        assert(impl_ && "UploadManager used after move");
        if (iData.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        // Texture uploads always route through StagingRing — layout transitions
        // require graphics queue, so dedicated buffer or ReBAR paths don't apply.
        return impl_->stagingRing->UploadTexture(iData, iDst, iRegion);
    }

}  // namespace miki::resource
