/** @file StagingRing.h
 *  @brief Multi-chunk staging ring for CPU→GPU uploads.
 *
 *  Pool of persistently-mapped CpuToGpu buffer chunks. Allocate() returns a
 *  mapped CPU pointer for direct write (zero intermediate copy). Pending
 *  buffer and texture copies are recorded via RecordTransfers() into a
 *  command buffer. Chunks are retired per-frame via fence values.
 *
 *  Design (ported from D:\repos\miki, adapted for mitsuki CRTP DeviceBase):
 *    - Multi-chunk auto-grow (no single-buffer OOM)
 *    - Write-in-place: Allocate() returns mapped ptr, caller writes directly
 *    - Unified buffer + texture upload path
 *    - Per-frame fence retirement (no manual Reset/WaitIdle)
 *    - Oversized allocs get dedicated chunks (no multi-batch flush)
 *    - ReBAR tiered fast-path for small allocs (zero DMA copy)
 *    - WebGPU shadow buffer fallback (no persistent mapping)
 *    - FlushMappedRange for non-coherent memory (mobile Vulkan)
 *    - ShrinkToFit for long-running VRAM reclaim
 *
 *  Thread safety: NOT thread-safe. Render thread only.
 *
 *  See: specs/03-sync.md §7.1
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

    inline constexpr uint64_t kStagingAlignment = 256;  ///< Covers Vulkan/D3D12/WebGPU requirements

    struct StagingRingDesc {
        uint64_t chunkSize = uint64_t{4} << 20;  ///< Per-chunk capacity (default 4 MB)
        uint32_t maxChunks = 64;                 ///< Max chunks (default 256 MB total cap)
    };

    struct StagingAllocation {
        std::byte* mappedPtr = nullptr;  ///< CPU write destination
        uint64_t size = 0;               ///< Allocation size in bytes
        uint32_t chunkIndex_ = ~0u;      ///< Internal: chunk pool index
        uint64_t chunkOffset_ = 0;       ///< Internal: byte offset within chunk
        bool isReBAR_ = false;           ///< Internal: true if device-local host-visible (ReBAR) memory

        [[nodiscard]] constexpr auto IsValid() const noexcept -> bool { return mappedPtr != nullptr; }
        [[nodiscard]] constexpr auto IsReBAR() const noexcept -> bool { return isReBAR_; }
    };

    struct TextureUploadRegion {
        uint32_t mipLevel = 0;
        uint32_t arrayLayer = 0;
        uint32_t offsetX = 0, offsetY = 0, offsetZ = 0;
        uint32_t width = 0, height = 0, depth = 1;
        uint32_t rowLength = 0;    ///< Texels per row in buffer. 0 = tightly packed.
        uint32_t imageHeight = 0;  ///< Rows per image in buffer. 0 = tightly packed.
    };

    class StagingRing {
       public:
        ~StagingRing();

        StagingRing(const StagingRing&) = delete;
        auto operator=(const StagingRing&) -> StagingRing& = delete;
        StagingRing(StagingRing&&) noexcept;
        auto operator=(StagingRing&&) noexcept -> StagingRing&;

        [[nodiscard]] static auto Create(rhi::DeviceHandle iDevice, StagingRingDesc iDesc = {})
            -> core::Result<StagingRing>;

        // ── Write-in-place (zero intermediate copy) ─────────────────

        [[nodiscard]] auto Allocate(uint64_t iSize, uint64_t iAlignment = kStagingAlignment)
            -> core::Result<StagingAllocation>;

        void EnqueueBufferCopy(const StagingAllocation& iAlloc, rhi::BufferHandle iDst, uint64_t iDstOffset);
        void EnqueueTextureCopy(
            const StagingAllocation& iAlloc, rhi::TextureHandle iDst, const TextureUploadRegion& iRegion
        );

        // ── Convenience wrappers (alloc + memcpy + enqueue) ─────────

        [[nodiscard]] auto UploadBuffer(std::span<const std::byte> iData, rhi::BufferHandle iDst, uint64_t iDstOffset)
            -> core::Result<void>;
        [[nodiscard]] auto UploadTexture(
            std::span<const std::byte> iData, rhi::TextureHandle iDst, const TextureUploadRegion& iRegion
        ) -> core::Result<void>;

        // ── Frame lifecycle ─────────────────────────────────────────

        auto FlushFrame(uint64_t iFenceValue) -> void;
        auto ReclaimCompleted(uint64_t iCompletedFenceValue) -> void;

        // ── Memory management ───────────────────────────────────────

        auto ShrinkToFit(uint64_t iTargetTotalBytes) -> uint64_t;
        [[nodiscard]] auto GetUtilization() const noexcept -> float;

        // ── Metrics ─────────────────────────────────────────────────

        [[nodiscard]] auto Capacity() const noexcept -> uint64_t;
        [[nodiscard]] auto GetBytesUploadedThisFrame() const noexcept -> uint64_t;
        [[nodiscard]] auto GetActiveChunkCount() const noexcept -> uint32_t;
        [[nodiscard]] auto GetFreeChunkCount() const noexcept -> uint32_t;
        [[nodiscard]] auto GetTotalChunkCount() const noexcept -> uint32_t;
        [[nodiscard]] auto GetTotalAllocatedBytes() const noexcept -> uint64_t;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        explicit StagingRing(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::resource
