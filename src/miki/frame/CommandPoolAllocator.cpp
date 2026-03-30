/** @file CommandPoolAllocator.cpp
 *  @brief Per-frame command pool lifecycle manager implementation (spec §19).
 */

#include "miki/frame/CommandPoolAllocator.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>

#include "miki/debug/StructuredLogger.h"
#include "miki/frame/CommandListArena.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/backend/AllBackends.h"

namespace miki::frame {

    // =========================================================================
    // Pool slot — one native pool + its arena
    // =========================================================================

    struct PoolSlot {
        rhi::CommandPoolHandle poolHandle;
        rhi::QueueType queue = rhi::QueueType::Graphics;
        CommandListArena<rhi::CommandListAcquisition> arena;
        bool oomObserved = false;
    };

    // =========================================================================
    // Impl — pimpl internals
    // =========================================================================

    struct CommandPoolAllocator::Impl {
        rhi::DeviceHandle device;
        uint32_t framesInFlight = 2;
        bool hasAsyncCompute = false;
        bool hasAsyncTransfer = false;
        bool enableHwmShrink = false;
        uint32_t initialArenaCapacity = 16;

        // PoolRing[frameSlot][queueIndex] — queueIndex: 0=Graphics, 1=Compute, 2=Transfer
        static constexpr uint32_t kMaxFrames = kMaxFramesInFlight;
        static constexpr uint32_t kMaxQueues = kMaxQueueTypes;
        std::array<std::array<PoolSlot, kMaxQueues>, kMaxFrames> poolRing_{};

        uint32_t activeQueueCount = 1;
        uint32_t highWaterMark = 0;

        [[nodiscard]] auto QueueIndex(rhi::QueueType q) const -> uint32_t {
            switch (q) {
                case rhi::QueueType::Graphics: return 0;
                case rhi::QueueType::Compute: return 1;
                case rhi::QueueType::Transfer: return 2;
            }
            return 0;
        }

        [[nodiscard]] auto IsQueueActive(rhi::QueueType q) const -> bool {
            if (q == rhi::QueueType::Graphics) {
                return true;
            }
            if (q == rhi::QueueType::Compute) {
                return hasAsyncCompute;
            }
            if (q == rhi::QueueType::Transfer) {
                return hasAsyncTransfer;
            }
            return false;
        }

        void DestroyAllPools() {
            for (uint32_t f = 0; f < framesInFlight; ++f) {
                for (uint32_t q = 0; q < activeQueueCount; ++q) {
                    auto& slot = poolRing_[f][q];
                    if (slot.poolHandle.IsValid()) {
                        device.Dispatch([&](auto& dev) { dev.DestroyCommandPool(slot.poolHandle); });
                        slot.poolHandle = {};
                    }
                }
            }
        }
    };

    // =========================================================================
    // Lifecycle
    // =========================================================================

    CommandPoolAllocator::CommandPoolAllocator() = default;

    CommandPoolAllocator::~CommandPoolAllocator() {
        if (impl_) {
            impl_->DestroyAllPools();
        }
    }

    CommandPoolAllocator::CommandPoolAllocator(CommandPoolAllocator&&) noexcept = default;
    auto CommandPoolAllocator::operator=(CommandPoolAllocator&&) noexcept -> CommandPoolAllocator& = default;

    auto CommandPoolAllocator::Create(const Desc& desc) -> core::Result<CommandPoolAllocator> {
        if (!desc.device.IsValid() || desc.framesInFlight == 0 || desc.framesInFlight > kMaxFramesInFlight) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        CommandPoolAllocator cpa;
        cpa.impl_ = std::make_unique<Impl>();
        auto& impl = *cpa.impl_;
        impl.device = desc.device;
        impl.framesInFlight = desc.framesInFlight;
        impl.hasAsyncCompute = desc.hasAsyncCompute;
        impl.hasAsyncTransfer = desc.hasAsyncTransfer;
        impl.enableHwmShrink = desc.enableHwmShrink;
        impl.initialArenaCapacity = desc.initialArenaCapacity;

        impl.activeQueueCount = 1;
        if (desc.hasAsyncCompute) {
            impl.activeQueueCount++;
        }
        if (desc.hasAsyncTransfer) {
            impl.activeQueueCount++;
        }

        // Create native pools for each (frameSlot, queue) pair
        static constexpr rhi::QueueType kQueues[]
            = {rhi::QueueType::Graphics, rhi::QueueType::Compute, rhi::QueueType::Transfer};

        for (uint32_t f = 0; f < impl.framesInFlight; ++f) {
            for (uint32_t q = 0; q < impl.activeQueueCount; ++q) {
                auto& slot = impl.poolRing_[f][q];
                slot.queue = kQueues[q];

                rhi::CommandPoolDesc poolDesc{.queue = kQueues[q], .transient = true};
                auto result = impl.device.Dispatch([&](auto& dev) { return dev.CreateCommandPool(poolDesc); });
                if (!result) {
                    impl.DestroyAllPools();
                    return std::unexpected(core::ErrorCode::OutOfMemory);
                }
                slot.poolHandle = *result;
                slot.arena.Reserve(desc.initialArenaCapacity);
            }
        }

        return cpa;
    }

    // =========================================================================
    // ResetSlot
    // =========================================================================

    void CommandPoolAllocator::ResetSlot(uint32_t frameSlot) {
        assert(impl_ && frameSlot < impl_->framesInFlight);
        auto& impl = *impl_;

        for (uint32_t q = 0; q < impl.activeQueueCount; ++q) {
            auto& slot = impl.poolRing_[frameSlot][q];

            rhi::CommandPoolResetFlags flags = rhi::CommandPoolResetFlags::None;
            if (impl.enableHwmShrink && slot.oomObserved) {
                flags = rhi::CommandPoolResetFlags::ReleaseResources;
                slot.oomObserved = false;
                MIKI_LOG_INFO(
                    ::miki::debug::LogCategory::Rhi, "CommandPoolAllocator: OOM shrink for slot={} queue={}", frameSlot,
                    q
                );
            }

            impl.device.Dispatch([&](auto& dev) { dev.ResetCommandPool(slot.poolHandle, flags); });
            slot.arena.ResetAll();
        }
    }

    // =========================================================================
    // Acquire
    // =========================================================================

    auto CommandPoolAllocator::Acquire(uint32_t frameSlot, rhi::QueueType queue) -> core::Result<PooledAcquisition> {
        assert(impl_ && frameSlot < impl_->framesInFlight);
        auto& impl = *impl_;
        uint32_t qi = impl.QueueIndex(queue);
        assert(qi < impl.activeQueueCount && "Acquiring from inactive queue");

        auto& slot = impl.poolRing_[frameSlot][qi];

        auto result = impl.device.Dispatch([&](auto& dev) { return dev.AllocateFromPool(slot.poolHandle, false); });

        if (!result) {
            if (impl.enableHwmShrink) {
                NotifyOom(frameSlot, queue);
            }
            return std::unexpected(core::ErrorCode::OutOfMemory);
        }

        auto [arenaPtr, arenaIndex] = slot.arena.Acquire();
        *arenaPtr = *result;

        // Track high water mark
        uint32_t total = 0;
        for (uint32_t q = 0; q < impl.activeQueueCount; ++q) {
            total += impl.poolRing_[frameSlot][q].arena.GetAcquiredCount();
        }
        impl.highWaterMark = std::max(impl.highWaterMark, total);

        return PooledAcquisition{.acquisition = *result, .arenaIndex = arenaIndex};
    }

    // =========================================================================
    // Release
    // =========================================================================

    void CommandPoolAllocator::Release(uint32_t frameSlot, rhi::QueueType queue, uint32_t arenaIndex) {
        assert(impl_ && frameSlot < impl_->framesInFlight);
        auto& impl = *impl_;
        uint32_t qi = impl.QueueIndex(queue);
        auto& slot = impl.poolRing_[frameSlot][qi];

        auto* acq = slot.arena.Lookup(arenaIndex);
        if (acq) {
            impl.device.Dispatch([&](auto& dev) { dev.FreeFromPool(slot.poolHandle, *acq); });
        }
        slot.arena.Release(arenaIndex);
    }

    // =========================================================================
    // Stats / Debug
    // =========================================================================

    auto CommandPoolAllocator::GetAcquiredCount(uint32_t frameSlot) const -> uint32_t {
        assert(impl_ && frameSlot < impl_->framesInFlight);
        uint32_t count = 0;
        for (uint32_t q = 0; q < impl_->activeQueueCount; ++q) {
            count += impl_->poolRing_[frameSlot][q].arena.GetAcquiredCount();
        }
        return count;
    }

    auto CommandPoolAllocator::GetPoolCount() const -> uint32_t {
        if (!impl_) {
            return 0;
        }
        return impl_->framesInFlight * impl_->activeQueueCount;
    }

    auto CommandPoolAllocator::GetStats() const -> PoolStats {
        if (!impl_) {
            return {};
        }
        auto& impl = *impl_;
        uint32_t totalAcquired = 0;
        for (uint32_t f = 0; f < impl.framesInFlight; ++f) {
            for (uint32_t q = 0; q < impl.activeQueueCount; ++q) {
                totalAcquired += impl.poolRing_[f][q].arena.GetAcquiredCount();
            }
        }
        return PoolStats{
            .framePoolCount = impl.framesInFlight * impl.activeQueueCount,
            .currentAcquired = totalAcquired,
            .highWaterMark = impl.highWaterMark,
            .hwmShrinkEnabled = impl.enableHwmShrink,
        };
    }

    void CommandPoolAllocator::DumpStats(FILE* out) const {
        auto stats = GetStats();
        std::fprintf(
            out, "[CommandPoolAllocator] pools=%u acquired=%u hwm=%u hwmShrink=%s\n", stats.framePoolCount,
            stats.currentAcquired, stats.highWaterMark, stats.hwmShrinkEnabled ? "on" : "off"
        );
    }

    void CommandPoolAllocator::NotifyOom(uint32_t frameSlot, rhi::QueueType queue) {
        assert(impl_ && frameSlot < impl_->framesInFlight);
        uint32_t qi = impl_->QueueIndex(queue);
        impl_->poolRing_[frameSlot][qi].oomObserved = true;
        MIKI_LOG_WARN(
            ::miki::debug::LogCategory::Rhi, "CommandPoolAllocator: OOM observed for slot={} queue={}", frameSlot, qi
        );
    }

}  // namespace miki::frame
