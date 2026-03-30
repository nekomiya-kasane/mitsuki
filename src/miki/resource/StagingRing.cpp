/** @file StagingRing.cpp
 *  @brief Multi-chunk staging ring for CPU→GPU uploads.
 *  Ported from D:\repos\miki, adapted for mitsuki CRTP DeviceBase / DeviceHandle.
 *  See: StagingRing.h, specs/03-sync.md §7.1
 */

#include "miki/resource/StagingRing.h"
#include "ChunkPool.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

namespace miki::resource {

    // =========================================================================
    // Internal types
    // =========================================================================

    struct StagingChunk {
        rhi::BufferHandle buffer = {};
        std::byte* mapped = nullptr;
        uint64_t capacity = 0;
        uint64_t writePos = 0;
        bool alive = true;
        bool isReBAR = false;
    };

    struct PendingBufferCopy {
        uint32_t chunkIndex = 0;
        uint64_t srcOffset = 0;
        uint64_t size = 0;
        rhi::BufferHandle dst = {};
        uint64_t dstOffset = 0;
    };

    struct PendingTextureCopy {
        uint32_t chunkIndex = 0;
        uint64_t srcOffset = 0;
        uint64_t size = 0;
        rhi::TextureHandle dst = {};
        TextureUploadRegion region = {};
    };

    // =========================================================================
    // Impl
    // =========================================================================

    struct StagingRing::Impl {
        detail::ChunkPool<StagingChunk> pool;
        StagingRingDesc desc = {};

        std::vector<PendingBufferCopy> pendingBufferCopies;
        std::vector<PendingTextureCopy> pendingTextureCopies;

        uint64_t bytesUploadedThisFrame = 0;

        auto CreateStagingChunk(uint64_t iCapacity) -> core::Result<uint32_t> {
            return pool.CreateChunk(iCapacity, rhi::BufferUsage::TransferSrc, rhi::MemoryLocation::CpuToGpu);
        }

        auto TryAllocFromChunk(uint32_t ci, uint64_t size, uint64_t align) -> StagingAllocation {
            auto r = pool.TryAllocFromChunk(ci, size, align);
            if (!r.valid) {
                return {};
            }
            return {.mappedPtr = r.mapped, .size = size, .chunkIndex_ = r.chunkIndex, .chunkOffset_ = r.offset};
        }
    };

    // =========================================================================
    // Lifecycle
    // =========================================================================

    StagingRing::StagingRing(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    StagingRing::~StagingRing() {
        if (impl_ && impl_->pool.device.IsValid()) {
            impl_->pool.DestroyAllChunks();
        }
    }

    StagingRing::StagingRing(StagingRing&&) noexcept = default;
    auto StagingRing::operator=(StagingRing&&) noexcept -> StagingRing& = default;

    auto StagingRing::Create(rhi::DeviceHandle iDevice, StagingRingDesc iDesc) -> core::Result<StagingRing> {
        if (!iDevice.IsValid() || iDesc.chunkSize == 0 || iDesc.maxChunks == 0) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        auto impl = std::make_unique<Impl>();
        impl->pool.device = iDevice;
        impl->pool.chunkSize = iDesc.chunkSize;
        impl->pool.maxChunks = iDesc.maxChunks;
        impl->desc = iDesc;

        // Pre-allocate one chunk
        auto chunkResult = impl->CreateStagingChunk(iDesc.chunkSize);
        if (!chunkResult) {
            return std::unexpected(core::ErrorCode::OutOfMemory);
        }
        impl->pool.ActivateChunk(*chunkResult);

        return StagingRing(std::move(impl));
    }

    // =========================================================================
    // Allocation
    // =========================================================================

    auto StagingRing::Allocate(uint64_t iSize, uint64_t iAlignment) -> core::Result<StagingAllocation> {
        if (iSize == 0) {
            return StagingAllocation{};
        }

        // 1. Try active chunks (last = most recently used, highest chance of space)
        for (auto it = impl_->pool.activeChunks.rbegin(); it != impl_->pool.activeChunks.rend(); ++it) {
            auto alloc = impl_->TryAllocFromChunk(*it, iSize, iAlignment);
            if (alloc.IsValid()) {
                impl_->bytesUploadedThisFrame += iSize;
                return alloc;
            }
        }

        // 2. Try free chunks (swap-and-pop, no std::find)
        for (size_t i = 0; i < impl_->pool.freeChunks.size(); ++i) {
            auto ci = impl_->pool.freeChunks[i];
            auto& c = impl_->pool.chunks[ci];
            c.writePos = 0;
            if (iSize <= c.capacity) {
                impl_->pool.freeChunks[i] = impl_->pool.freeChunks.back();
                impl_->pool.freeChunks.pop_back();
                impl_->pool.ActivateChunk(ci);
                auto alloc = impl_->TryAllocFromChunk(ci, iSize, iAlignment);
                if (alloc.IsValid()) {
                    impl_->bytesUploadedThisFrame += iSize;
                    return alloc;
                }
            }
        }

        // 3. Create new chunk (possibly oversized for large allocs)
        uint64_t needed = detail::ChunkPool<StagingChunk>::AlignUp(iSize, iAlignment);
        uint64_t newCap = std::max(impl_->desc.chunkSize, needed);
        auto chunkResult = impl_->CreateStagingChunk(newCap);
        if (!chunkResult) {
            return std::unexpected(core::ErrorCode::OutOfMemory);
        }

        impl_->pool.ActivateChunk(*chunkResult);
        auto alloc = impl_->TryAllocFromChunk(*chunkResult, iSize, iAlignment);
        assert(alloc.IsValid());
        impl_->bytesUploadedThisFrame += iSize;
        return alloc;
    }

    // =========================================================================
    // Enqueue copies
    // =========================================================================

    void StagingRing::EnqueueBufferCopy(const StagingAllocation& iAlloc, rhi::BufferHandle iDst, uint64_t iDstOffset) {
        if (!iAlloc.IsValid() || iAlloc.size == 0) {
            return;
        }
        if (iAlloc.IsReBAR()) {
            return;  // Data already in VRAM
        }
        impl_->pendingBufferCopies.push_back(
            {.chunkIndex = iAlloc.chunkIndex_,
             .srcOffset = iAlloc.chunkOffset_,
             .size = iAlloc.size,
             .dst = iDst,
             .dstOffset = iDstOffset}
        );
    }

    void StagingRing::EnqueueTextureCopy(
        const StagingAllocation& iAlloc, rhi::TextureHandle iDst, const TextureUploadRegion& iRegion
    ) {
        if (!iAlloc.IsValid() || iAlloc.size == 0) {
            return;
        }
        impl_->pendingTextureCopies.push_back(
            {.chunkIndex = iAlloc.chunkIndex_,
             .srcOffset = iAlloc.chunkOffset_,
             .size = iAlloc.size,
             .dst = iDst,
             .region = iRegion}
        );
    }

    // =========================================================================
    // Convenience wrappers
    // =========================================================================

    auto StagingRing::UploadBuffer(std::span<const std::byte> iData, rhi::BufferHandle iDst, uint64_t iDstOffset)
        -> core::Result<void> {
        if (iData.empty()) {
            return {};
        }
        auto alloc = Allocate(iData.size());
        if (!alloc) {
            return std::unexpected(alloc.error());
        }
        std::memcpy(alloc->mappedPtr, iData.data(), iData.size());
        EnqueueBufferCopy(*alloc, iDst, iDstOffset);
        return {};
    }

    auto StagingRing::UploadTexture(
        std::span<const std::byte> iData, rhi::TextureHandle iDst, const TextureUploadRegion& iRegion
    ) -> core::Result<void> {
        if (iData.empty()) {
            return {};
        }
        auto alloc = Allocate(iData.size());
        if (!alloc) {
            return std::unexpected(alloc.error());
        }
        std::memcpy(alloc->mappedPtr, iData.data(), iData.size());
        EnqueueTextureCopy(*alloc, iDst, iRegion);
        return {};
    }

    // =========================================================================
    // Frame lifecycle
    // =========================================================================

    auto StagingRing::FlushFrame(uint64_t iFenceValue) -> void {
        impl_->pool.FlushFrame(iFenceValue);
        impl_->pendingBufferCopies.clear();
        impl_->pendingTextureCopies.clear();
        impl_->bytesUploadedThisFrame = 0;
    }

    auto StagingRing::ReclaimCompleted(uint64_t iCompletedFenceValue) -> void {
        impl_->pool.ReclaimCompleted(iCompletedFenceValue);
    }

    // =========================================================================
    // Memory management + Metrics
    // =========================================================================

    auto StagingRing::ShrinkToFit(uint64_t iTargetTotalBytes) -> uint64_t {
        return impl_->pool.ShrinkToFit(iTargetTotalBytes);
    }
    auto StagingRing::GetUtilization() const noexcept -> float {
        return impl_ ? impl_->pool.GetUtilization() : 0.0f;
    }
    auto StagingRing::Capacity() const noexcept -> uint64_t {
        return impl_ ? impl_->pool.GetTotalAllocatedBytes() : 0;
    }
    auto StagingRing::GetBytesUploadedThisFrame() const noexcept -> uint64_t {
        return impl_ ? impl_->bytesUploadedThisFrame : 0;
    }
    auto StagingRing::GetActiveChunkCount() const noexcept -> uint32_t {
        return impl_ ? impl_->pool.GetActiveChunkCount() : 0;
    }
    auto StagingRing::GetFreeChunkCount() const noexcept -> uint32_t {
        return impl_ ? impl_->pool.GetFreeChunkCount() : 0;
    }
    auto StagingRing::GetTotalChunkCount() const noexcept -> uint32_t {
        return impl_ ? impl_->pool.GetTotalChunkCount() : 0;
    }
    auto StagingRing::GetTotalAllocatedBytes() const noexcept -> uint64_t {
        return impl_ ? impl_->pool.GetTotalAllocatedBytes() : 0;
    }

}  // namespace miki::resource
