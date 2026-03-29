/** @file ReadbackRing.h
 *  @brief Per-frame ring buffer for GPU→CPU readback.
 *
 *  16MB default persistent-mapped GpuToCpu buffer. Used for query results,
 *  screenshots, pick hit buffers, measurement results.
 *
 *  See: specs/03-sync.md §7.2
 *  Namespace: miki::resource
 */
#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "miki/core/Result.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"

namespace miki::resource {

    static constexpr uint64_t kDefaultReadbackRingCapacity = 16ULL * 1024 * 1024;  // 16MB

    /// @brief Handle to a pending readback request. Resolve after GPU completion.
    struct ReadbackFuture {
        uint64_t id = 0;
        [[nodiscard]] constexpr auto IsValid() const noexcept -> bool { return id != 0; }
    };

    class ReadbackRing {
       public:
        ~ReadbackRing();

        ReadbackRing(const ReadbackRing&) = delete;
        auto operator=(const ReadbackRing&) -> ReadbackRing& = delete;
        ReadbackRing(ReadbackRing&&) noexcept;
        auto operator=(ReadbackRing&&) noexcept -> ReadbackRing&;

        [[nodiscard]] static auto Create(rhi::DeviceHandle iDevice, uint64_t iCapacity = kDefaultReadbackRingCapacity)
            -> core::Result<ReadbackRing>;

        /// @brief Reserve space in the readback ring for a future GPU→CPU copy.
        /// Returns a future handle + the destination buffer/offset for CmdCopyBuffer.
        /// @param iSize  Number of bytes to read back.
        /// @return {future, dstBuffer, dstOffset} for recording the copy command.
        struct ReadbackReservation {
            ReadbackFuture future;
            rhi::BufferHandle dstBuffer;  ///< Readback ring buffer (use as CmdCopyBuffer dst)
            uint64_t dstOffset = 0;       ///< Offset into dstBuffer
        };
        [[nodiscard]] auto Reserve(uint64_t iSize, uint64_t iAlignment = 16) -> core::Result<ReadbackReservation>;

        /// @brief Tag all pending reservations with a frame number.
        /// Called at EndFrame.
        auto FlushFrame(uint64_t iFrameNumber) -> void;

        /// @brief Reclaim chunks from completed frames.
        /// After this call, futures from those frames can be Resolved.
        auto ReclaimCompleted(uint64_t iCompletedFrame) -> void;

        /// @brief Resolve a completed readback future to CPU-readable data.
        /// Returns empty span if future is not yet complete or invalid.
        [[nodiscard]] auto Resolve(ReadbackFuture iFuture) const noexcept -> std::span<const uint8_t>;

        [[nodiscard]] auto Capacity() const noexcept -> uint64_t;
        [[nodiscard]] auto Available() const noexcept -> uint64_t;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        explicit ReadbackRing(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::resource
