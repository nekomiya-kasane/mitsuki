/** @file CommandPoolAllocator.h
 *  @brief Per-frame command pool lifecycle manager (spec §19).
 *
 *  Manages a ring of native command pools (VkCommandPool / ID3D12CommandAllocator)
 *  indexed by [frameSlot][queueType]. Pools are created once at initialization
 *  and reset in bulk at BeginFrame after GPU completion.
 *
 *  Thread safety: single-threaded per queue (v1). See §19.8 for multi-thread extension.
 *
 *  Namespace: miki::frame
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>

#include "miki/core/Result.h"
#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiEnums.h"

namespace miki::frame {

    class FrameManager;

    class CommandPoolAllocator {
       public:
        static constexpr uint32_t kMaxFramesInFlight = 4;
        static constexpr uint32_t kMaxQueueTypes = 3;  // Graphics, Compute, Transfer

        struct Desc {
            rhi::DeviceHandle device;
            uint32_t framesInFlight = 2;
            bool hasAsyncCompute = false;
            bool hasAsyncTransfer = false;
            uint32_t initialArenaCapacity = 16;
            bool enableHwmShrink = false;
        };

        struct PoolStats {
            uint32_t framePoolCount = 0;
            uint32_t currentAcquired = 0;
            uint32_t highWaterMark = 0;
            bool hwmShrinkEnabled = false;
        };

        struct PooledAcquisition {
            rhi::CommandListAcquisition acquisition;
            uint32_t arenaIndex = 0;
        };

        CommandPoolAllocator();
        ~CommandPoolAllocator();

        CommandPoolAllocator(CommandPoolAllocator&&) noexcept;
        auto operator=(CommandPoolAllocator&&) noexcept -> CommandPoolAllocator&;
        CommandPoolAllocator(const CommandPoolAllocator&) = delete;
        auto operator=(const CommandPoolAllocator&) -> CommandPoolAllocator& = delete;

        [[nodiscard]] static auto Create(const Desc& desc) -> core::Result<CommandPoolAllocator>;

        void ResetSlot(uint32_t frameSlot);

        [[nodiscard]] auto Acquire(uint32_t frameSlot, rhi::QueueType queue) -> core::Result<PooledAcquisition>;

        void Release(uint32_t frameSlot, rhi::QueueType queue, uint32_t arenaIndex);

        [[nodiscard]] auto GetAcquiredCount(uint32_t frameSlot) const -> uint32_t;
        [[nodiscard]] auto GetPoolCount() const -> uint32_t;
        [[nodiscard]] auto GetStats() const -> PoolStats;
        void DumpStats(FILE* out = stderr) const;
        void NotifyOom(uint32_t frameSlot, rhi::QueueType queue);

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

}  // namespace miki::frame
