/** @file ReadbackRing.cpp
 *  @brief Multi-chunk readback ring for GPU→CPU downloads.
 *  Ported from D:\repos\miki, adapted for mitsuki CRTP DeviceBase / DeviceHandle.
 *  See: ReadbackRing.h, specs/03-sync.md §7.2
 */

#include "miki/resource/ReadbackRing.h"
#include "ChunkPool.h"

#include "miki/rhi/backend/AllCommandBuffers.h"

#include <algorithm>
#include <cassert>
#include <vector>

namespace miki::resource {

    // =========================================================================
    // Internal types
    // =========================================================================

    struct RbChunk {
        rhi::BufferHandle buffer = {};
        std::byte* mapped = nullptr;
        uint64_t capacity = 0;
        uint64_t writePos = 0;
        bool alive = true;
        uint64_t retiredAtFence = ~0ull;
        uint32_t generation = 0;           ///< Incremented on each reuse; tickets carry a snapshot for stale detection
        mutable bool invalidated = false;  ///< True after InvalidateMappedRange called for this retirement cycle
    };

    struct PendingBufferReadback {
        uint32_t chunkIndex = 0;
        uint64_t dstOffset = 0;
        uint64_t size = 0;
        rhi::BufferHandle src = {};
        uint64_t srcOffset = 0;
    };

    struct PendingTextureReadback {
        uint32_t chunkIndex = 0;
        uint64_t dstOffset = 0;
        uint64_t size = 0;
        rhi::TextureHandle src = {};
        TextureReadbackRegion region = {};
    };

    // =========================================================================
    // Impl
    // =========================================================================

    struct ReadbackRing::Impl {
        detail::ChunkPool<RbChunk> pool;
        ReadbackRingDesc desc = {};

        std::vector<PendingBufferReadback> pendingBufferReadbacks;
        std::vector<PendingTextureReadback> pendingTextureReadbacks;

        uint64_t bytesReadbackThisFrame = 0;
        uint64_t lastCompletedFence = 0;

        auto CreateReadbackChunk(uint64_t iCapacity) -> core::Result<uint32_t> {
            return pool.CreateChunk(iCapacity, rhi::BufferUsage::TransferDst, rhi::MemoryLocation::GpuToCpu);
        }

        auto AllocateSpace(uint64_t iSize, uint64_t iAlignment) -> core::Result<std::pair<uint32_t, uint64_t>> {
            // 1. Try active chunks
            for (auto it = pool.activeChunks.rbegin(); it != pool.activeChunks.rend(); ++it) {
                auto r = pool.TryAllocFromChunk(*it, iSize, iAlignment);
                if (r.valid) {
                    return std::pair{r.chunkIndex, r.offset};
                }
            }

            // 2. Try free chunks
            for (size_t i = 0; i < pool.freeChunks.size(); ++i) {
                auto ci = pool.freeChunks[i];
                auto& c = pool.chunks[ci];
                c.writePos = 0;
                c.retiredAtFence = ~0ull;
                ++c.generation;
                c.invalidated = false;
                if (iSize <= c.capacity) {
                    pool.freeChunks[i] = pool.freeChunks.back();
                    pool.freeChunks.pop_back();
                    pool.ActivateChunk(ci);
                    auto r = pool.TryAllocFromChunk(ci, iSize, iAlignment);
                    if (r.valid) {
                        return std::pair{r.chunkIndex, r.offset};
                    }
                }
            }

            // 3. Create new chunk
            uint64_t needed = detail::ChunkPool<RbChunk>::AlignUp(iSize, iAlignment);
            uint64_t newCap = std::max(desc.chunkSize, needed);
            auto chunkResult = CreateReadbackChunk(newCap);
            if (!chunkResult) {
                return std::unexpected(chunkResult.error());
            }

            pool.ActivateChunk(*chunkResult);
            auto r = pool.TryAllocFromChunk(*chunkResult, iSize, iAlignment);
            assert(r.valid);
            return std::pair{r.chunkIndex, r.offset};
        }
    };

    // =========================================================================
    // Lifecycle
    // =========================================================================

    ReadbackRing::ReadbackRing(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    ReadbackRing::~ReadbackRing() {
        if (impl_ && impl_->pool.device.IsValid()) {
            impl_->pool.DestroyAllChunks();
        }
    }

    ReadbackRing::ReadbackRing(ReadbackRing&&) noexcept = default;
    auto ReadbackRing::operator=(ReadbackRing&&) noexcept -> ReadbackRing& = default;

    auto ReadbackRing::Create(rhi::DeviceHandle iDevice, ReadbackRingDesc iDesc) -> core::Result<ReadbackRing> {
        if (!iDevice.IsValid() || iDesc.chunkSize == 0 || iDesc.maxChunks == 0) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        auto impl = std::make_unique<Impl>();
        impl->pool.device = iDevice;
        impl->pool.chunkSize = iDesc.chunkSize;
        impl->pool.maxChunks = iDesc.maxChunks;
        impl->desc = iDesc;

        auto chunkResult = impl->CreateReadbackChunk(iDesc.chunkSize);
        if (!chunkResult) {
            return std::unexpected(core::ErrorCode::OutOfMemory);
        }
        impl->pool.ActivateChunk(*chunkResult);

        return ReadbackRing(std::move(impl));
    }

    // =========================================================================
    // Enqueue readbacks
    // =========================================================================

    auto ReadbackRing::EnqueueBufferReadback(rhi::BufferHandle iSrc, uint64_t iSrcOffset, uint64_t iSize)
        -> core::Result<ReadbackTicket> {
        if (iSize == 0) {
            return ReadbackTicket{};
        }
        auto allocResult = impl_->AllocateSpace(iSize, kReadbackAlignment);
        if (!allocResult) {
            return std::unexpected(allocResult.error());
        }
        auto [ci, off] = *allocResult;
        impl_->pendingBufferReadbacks.push_back(
            {.chunkIndex = ci, .dstOffset = off, .size = iSize, .src = iSrc, .srcOffset = iSrcOffset}
        );
        impl_->bytesReadbackThisFrame += iSize;
        return ReadbackTicket{
            .chunkIndex_ = ci, .chunkOffset_ = off, .size = iSize, .generation_ = impl_->pool.chunks[ci].generation
        };
    }

    auto ReadbackRing::EnqueueTextureReadback(
        rhi::TextureHandle iSrc, const TextureReadbackRegion& iRegion, uint64_t iDataSize
    ) -> core::Result<ReadbackTicket> {
        if (iDataSize == 0) {
            return ReadbackTicket{};
        }
        auto allocResult = impl_->AllocateSpace(iDataSize, kReadbackAlignment);
        if (!allocResult) {
            return std::unexpected(allocResult.error());
        }
        auto [ci, off] = *allocResult;
        impl_->pendingTextureReadbacks.push_back(
            {.chunkIndex = ci, .dstOffset = off, .size = iDataSize, .src = iSrc, .region = iRegion}
        );
        impl_->bytesReadbackThisFrame += iDataSize;
        return ReadbackTicket{
            .chunkIndex_ = ci, .chunkOffset_ = off, .size = iDataSize, .generation_ = impl_->pool.chunks[ci].generation
        };
    }

    // =========================================================================
    // Record GPU copy commands
    // =========================================================================

    auto ReadbackRing::RecordTransfers(rhi::CommandListHandle& iCmd) -> uint32_t {
        uint32_t count = 0;

        for (auto const& rb : impl_->pendingBufferReadbacks) {
            auto const& c = impl_->pool.chunks[rb.chunkIndex];
            iCmd.Dispatch([&](auto& cmd) { cmd.CmdCopyBuffer(rb.src, rb.srcOffset, c.buffer, rb.dstOffset, rb.size); });
            ++count;
        }

        for (auto const& tr : impl_->pendingTextureReadbacks) {
            auto const& c = impl_->pool.chunks[tr.chunkIndex];
            rhi::BufferTextureCopyRegion region{
                .bufferOffset = tr.dstOffset,
                .bufferRowLength = tr.region.rowLength,
                .bufferImageHeight = tr.region.imageHeight,
                .subresource
                = {.baseMipLevel = tr.region.mipLevel,
                   .mipLevelCount = 1,
                   .baseArrayLayer = tr.region.arrayLayer,
                   .arrayLayerCount = 1},
                .textureOffset
                = {.x = static_cast<int32_t>(tr.region.offsetX),
                   .y = static_cast<int32_t>(tr.region.offsetY),
                   .z = static_cast<int32_t>(tr.region.offsetZ)},
                .textureExtent = {.width = tr.region.width, .height = tr.region.height, .depth = tr.region.depth},
            };
            iCmd.Dispatch([&](auto& cmd) { cmd.CmdCopyTextureToBuffer(tr.src, c.buffer, region); });
            ++count;
        }

        impl_->pendingBufferReadbacks.clear();
        impl_->pendingTextureReadbacks.clear();
        return count;
    }

    auto ReadbackRing::GetPendingCopyCount() const noexcept -> uint32_t {
        if (!impl_) {
            return 0;
        }
        return static_cast<uint32_t>(impl_->pendingBufferReadbacks.size() + impl_->pendingTextureReadbacks.size());
    }

    // =========================================================================
    // Frame lifecycle
    // =========================================================================

    auto ReadbackRing::FlushFrame(uint64_t iFenceValue) -> void {
        if (impl_->pool.activeChunks.empty()) {
            return;
        }
        for (auto ci : impl_->pool.activeChunks) {
            impl_->pool.chunks[ci].retiredAtFence = iFenceValue;
        }
        impl_->pool.FlushFrame(iFenceValue);
        impl_->bytesReadbackThisFrame = 0;
    }

    auto ReadbackRing::ReclaimCompleted(uint64_t iCompletedFenceValue) -> void {
        impl_->lastCompletedFence = iCompletedFenceValue;
        impl_->pool.ReclaimCompleted(iCompletedFenceValue);
    }

    // =========================================================================
    // Data access
    // =========================================================================

    auto ReadbackRing::IsReady(const ReadbackTicket& iTicket) const noexcept -> bool {
        if (!iTicket.IsValid()) {
            return false;
        }
        if (iTicket.chunkIndex_ >= impl_->pool.chunks.size()) {
            return false;
        }
        auto const& c = impl_->pool.chunks[iTicket.chunkIndex_];
        if (!c.alive || c.generation != iTicket.generation_ || c.retiredAtFence == ~0ull) {
            return false;  // Stale ticket or chunk not yet retired
        }
        return impl_->lastCompletedFence >= c.retiredAtFence;
    }

    auto ReadbackRing::GetData(const ReadbackTicket& iTicket) const -> std::span<const std::byte> {
        if (!iTicket.IsValid()) {
            return {};
        }
        if (iTicket.chunkIndex_ >= impl_->pool.chunks.size()) {
            return {};
        }
        auto& c = impl_->pool.chunks[iTicket.chunkIndex_];
        if (!c.alive || c.generation != iTicket.generation_ || iTicket.chunkOffset_ + iTicket.size > c.capacity) {
            return {};  // Stale ticket or out-of-bounds
        }
        // Invalidate CPU cache once per chunk retirement (non-coherent memory correctness)
        if (!c.invalidated) {
            impl_->pool.device.Dispatch([&](auto& dev) { dev.InvalidateMappedRange(c.buffer, 0, c.writePos); });
            c.invalidated = true;
        }
        return {c.mapped + iTicket.chunkOffset_, iTicket.size};
    }

    // =========================================================================
    // Memory management + Metrics
    // =========================================================================

    auto ReadbackRing::ShrinkToFit(uint64_t iTargetTotalBytes) -> uint64_t {
        return impl_->pool.ShrinkToFit(iTargetTotalBytes);
    }
    auto ReadbackRing::GetUtilization() const noexcept -> float {
        return impl_ ? impl_->pool.GetUtilization() : 0.0f;
    }
    auto ReadbackRing::Capacity() const noexcept -> uint64_t {
        return impl_ ? impl_->pool.GetTotalAllocatedBytes() : 0;
    }
    auto ReadbackRing::GetBytesReadbackThisFrame() const noexcept -> uint64_t {
        return impl_ ? impl_->bytesReadbackThisFrame : 0;
    }
    auto ReadbackRing::GetActiveChunkCount() const noexcept -> uint32_t {
        return impl_ ? impl_->pool.GetActiveChunkCount() : 0;
    }
    auto ReadbackRing::GetFreeChunkCount() const noexcept -> uint32_t {
        return impl_ ? impl_->pool.GetFreeChunkCount() : 0;
    }
    auto ReadbackRing::GetTotalChunkCount() const noexcept -> uint32_t {
        return impl_ ? impl_->pool.GetTotalChunkCount() : 0;
    }

}  // namespace miki::resource
