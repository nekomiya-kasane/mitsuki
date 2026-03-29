/** @file StagingRing.h
 *  @brief Per-frame ring buffer for CPU→GPU streaming uploads.
 *
 *  64MB default persistent-mapped CpuToGpu buffer. Write pointer advances
 *  monotonically, wraps at capacity. Each allocation is tagged with a frame
 *  number; ReclaimCompleted frees chunks from completed frames.
 *
 *  See: specs/03-sync.md §7.1
 *  Namespace: miki::resource
 */
#pragma once

#include <cstdint>
#include <memory>

#include "miki/core/Result.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"

namespace miki::resource {

    static constexpr uint64_t kDefaultStagingRingCapacity = 64ULL * 1024 * 1024;  // 64MB

    struct StagingAllocation {
        void* cpuPtr = nullptr;       ///< Persistent-mapped CPU pointer for memcpy
        uint64_t gpuOffset = 0;       ///< Byte offset into the staging buffer
        uint64_t size = 0;            ///< Allocation size in bytes
        rhi::BufferHandle buffer;     ///< The staging buffer handle (for CmdCopyBuffer src)
    };

    class StagingRing {
       public:
        ~StagingRing();

        StagingRing(const StagingRing&) = delete;
        auto operator=(const StagingRing&) -> StagingRing& = delete;
        StagingRing(StagingRing&&) noexcept;
        auto operator=(StagingRing&&) noexcept -> StagingRing&;

        /// @brief Create a StagingRing with the given capacity.
        [[nodiscard]] static auto Create(rhi::DeviceHandle iDevice, uint64_t iCapacity = kDefaultStagingRingCapacity)
            -> core::Result<StagingRing>;

        /// @brief Allocate from the ring. Returns invalid if insufficient space.
        /// Non-blocking: returns error if ring is full (caller should wait or use dedicated buffer).
        [[nodiscard]] auto Allocate(uint64_t iSize, uint64_t iAlignment = 16) -> core::Result<StagingAllocation>;

        /// @brief Tag all unflushed allocations with the given frame number.
        /// Called by FrameManager at EndFrame.
        auto FlushFrame(uint64_t iFrameNumber) -> void;

        /// @brief Reclaim chunks from frames <= iCompletedFrame.
        /// Called by FrameManager at BeginFrame after GPU completion confirmed.
        auto ReclaimCompleted(uint64_t iCompletedFrame) -> void;

        /// @brief Total capacity in bytes.
        [[nodiscard]] auto Capacity() const noexcept -> uint64_t;

        /// @brief Currently available (unallocated + reclaimable) bytes.
        [[nodiscard]] auto Available() const noexcept -> uint64_t;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        explicit StagingRing(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::resource
