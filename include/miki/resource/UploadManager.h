/** @file UploadManager.h
 *  @brief 4-tier upload routing for CPU→GPU data transfer.
 *
 *  Routes uploads based on data size to the optimal transfer path:
 *    Path A: StagingRing (< 256KB, zero-alloc, frame-paced reclaim)
 *    Path B: StagingRing large block (256KB-64MB, may stall waiting for reclaim)
 *    Path C: Dedicated staging buffer (> 64MB, one-shot + deferred destroy)
 *    Path D: Direct VRAM write via ReBAR (> 256MB + ReBAR available)
 *
 *  See: specs/03-sync.md §7.1.1
 *  Namespace: miki::resource
 */
#pragma once

#include <cstdint>
#include <memory>

#include "miki/core/Result.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"

namespace miki::frame {
    class DeferredDestructor;
}

namespace miki::resource {

    class StagingRing;

    static constexpr uint64_t kStagingRingThreshold = 256ULL * 1024;  // 256KB

    /// @brief Describes which upload path was used.
    enum class UploadPath : uint8_t {
        StagingRing,       ///< Path A: small data, ring buffer
        StagingRingLarge,  ///< Path B: medium data, ring buffer (may stall)
        DedicatedBuffer,   ///< Path C: large data, one-shot buffer
        DirectVRAM,        ///< Path D: huge data, ReBAR direct write
    };

    struct UploadResult {
        UploadPath path = UploadPath::StagingRing;
        rhi::BufferHandle stagingBuffer;  ///< Path A/B: staging ring buffer. Path C: dedicated buffer. Path D: invalid.
        uint64_t stagingOffset = 0;       ///< Offset into staging buffer (Path A/B only)
        uint64_t size = 0;
    };

    class UploadManager {
       public:
        ~UploadManager();

        UploadManager(const UploadManager&) = delete;
        auto operator=(const UploadManager&) -> UploadManager& = delete;
        UploadManager(UploadManager&&) noexcept;
        auto operator=(UploadManager&&) noexcept -> UploadManager&;

        /// @brief Create an UploadManager.
        /// @param iDevice             Device handle.
        /// @param iStagingRing        Pointer to the shared StagingRing (must outlive UploadManager).
        /// @param iDeferredDestructor Pointer to deferred destructor for Path C cleanup (must outlive).
        /// @param iRebarAvailable     Whether ReBAR (resizable BAR) is available for direct VRAM writes.
        /// @param iRebarBudget        Maximum size for ReBAR direct writes (bytes).
        [[nodiscard]] static auto Create(rhi::DeviceHandle iDevice, StagingRing* iStagingRing,
                                         frame::DeferredDestructor* iDeferredDestructor, bool iRebarAvailable = false,
                                         uint64_t iRebarBudget = 0) -> core::Result<UploadManager>;

        /// @brief Upload data to a GPU buffer, automatically choosing the optimal path.
        /// @param iDst   Destination GPU buffer.
        /// @param iDstOffset Byte offset into destination buffer.
        /// @param iData  Source CPU data pointer.
        /// @param iSize  Number of bytes to upload.
        /// @return UploadResult describing which path was used and staging info for copy recording.
        [[nodiscard]] auto Upload(rhi::BufferHandle iDst, uint64_t iDstOffset, const void* iData, uint64_t iSize)
            -> core::Result<UploadResult>;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        explicit UploadManager(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::resource
