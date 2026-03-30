/** @file ReadbackRing.h
 *  @brief Multi-chunk readback ring for GPU→CPU downloads.
 *
 *  Symmetric counterpart to StagingRing. Pool of persistently-mapped GpuToCpu
 *  buffer chunks. EnqueueBufferReadback() / EnqueueTextureReadback() return a
 *  ReadbackTicket. After the GPU fence completes, caller polls IsReady() then
 *  reads data via GetData(). InvalidateMappedRange called before CPU read for
 *  non-coherent memory correctness (mobile Vulkan).
 *
 *  Design (ported from D:\repos\miki, adapted for mitsuki CRTP DeviceBase):
 *    - Multi-chunk auto-grow (same pattern as StagingRing)
 *    - Per-frame fence retirement
 *    - ReadbackTicket with deferred poll (2-3 frame latency is inherent)
 *    - Texture readback support (CopyTextureToBuffer)
 *    - ShrinkToFit for long-running VRAM reclaim
 *    - InvalidateMappedRange for non-coherent memory
 *
 *  Thread safety: NOT thread-safe. Render thread only.
 *
 *  See: specs/03-sync.md §7.2
 *  Namespace: miki::resource
 */
#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "miki/core/Result.h"
#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"

namespace miki::resource {

    inline constexpr uint64_t kReadbackAlignment = 256;

    struct ReadbackRingDesc {
        uint64_t chunkSize = uint64_t{2} << 20;  ///< Per-chunk capacity (default 2 MB)
        uint32_t maxChunks = 32;                 ///< Max chunks (default 64 MB total cap)
    };

    struct ReadbackTicket {
        uint32_t chunkIndex_ = ~0u;
        uint64_t chunkOffset_ = 0;
        uint64_t size = 0;
        uint32_t generation_ = 0;  ///< Matches chunk generation at allocation time
        [[nodiscard]] constexpr auto IsValid() const noexcept -> bool { return chunkIndex_ != ~0u && size > 0; }
    };

    struct TextureReadbackRegion {
        uint32_t mipLevel = 0;
        uint32_t arrayLayer = 0;
        uint32_t offsetX = 0, offsetY = 0, offsetZ = 0;
        uint32_t width = 0, height = 0, depth = 1;
        uint32_t rowLength = 0;
        uint32_t imageHeight = 0;
    };

    class ReadbackRing {
       public:
        ~ReadbackRing();

        ReadbackRing(const ReadbackRing&) = delete;
        auto operator=(const ReadbackRing&) -> ReadbackRing& = delete;
        ReadbackRing(ReadbackRing&&) noexcept;
        auto operator=(ReadbackRing&&) noexcept -> ReadbackRing&;

        [[nodiscard]] static auto Create(rhi::DeviceHandle iDevice, ReadbackRingDesc iDesc = {})
            -> core::Result<ReadbackRing>;

        // ── Enqueue GPU→CPU copies ──────────────────────────────────

        [[nodiscard]] auto EnqueueBufferReadback(rhi::BufferHandle iSrc, uint64_t iSrcOffset, uint64_t iSize)
            -> core::Result<ReadbackTicket>;

        [[nodiscard]] auto EnqueueTextureReadback(
            rhi::TextureHandle iSrc, const TextureReadbackRegion& iRegion, uint64_t iDataSize
        ) -> core::Result<ReadbackTicket>;

        // ── Record GPU copy commands ──────────────────────────────────

        /// @brief Record all pending GPU→readback buffer copies into a command buffer.
        /// Emits CmdCopyBuffer and CmdCopyTextureToBuffer for all enqueued readbacks.
        /// Clears the pending lists after recording.
        /// @return Number of copy commands recorded.
        auto RecordTransfers(rhi::CommandListHandle& iCmd) -> uint32_t;

        /// @brief Number of pending copies not yet recorded.
        [[nodiscard]] auto GetPendingCopyCount() const noexcept -> uint32_t;

        // ── Frame lifecycle ─────────────────────────────────────────

        auto FlushFrame(uint64_t iFenceValue) -> void;
        auto ReclaimCompleted(uint64_t iCompletedFenceValue) -> void;

        // ── Data access ─────────────────────────────────────────────

        [[nodiscard]] auto IsReady(const ReadbackTicket& iTicket) const noexcept -> bool;
        [[nodiscard]] auto GetData(const ReadbackTicket& iTicket) const -> std::span<const std::byte>;

        // ── Memory management ───────────────────────────────────────

        auto ShrinkToFit(uint64_t iTargetTotalBytes) -> uint64_t;
        [[nodiscard]] auto GetUtilization() const noexcept -> float;

        // ── Metrics ─────────────────────────────────────────────────

        [[nodiscard]] auto Capacity() const noexcept -> uint64_t;
        [[nodiscard]] auto GetBytesReadbackThisFrame() const noexcept -> uint64_t;
        [[nodiscard]] auto GetActiveChunkCount() const noexcept -> uint32_t;
        [[nodiscard]] auto GetFreeChunkCount() const noexcept -> uint32_t;
        [[nodiscard]] auto GetTotalChunkCount() const noexcept -> uint32_t;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        explicit ReadbackRing(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::resource
