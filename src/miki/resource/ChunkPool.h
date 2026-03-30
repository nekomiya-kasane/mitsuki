/** @brief ChunkPool — shared internal chunk management for StagingRing / ReadbackRing.
 *
 * Template utility that handles:
 *   - Chunk allocation / deallocation with aliveCount O(1) tracking
 *   - Active / free chunk lists with swap-and-pop O(1) removal
 *   - Bump-allocator within chunks
 *   - Frame retirement queue (FlushFrame / ReclaimCompleted)
 *   - ShrinkToFit with swap-and-pop bulk erase
 *   - Dead slot reuse to bound chunks vector growth
 *   - Utilization / metrics
 *
 * Template parameter `Chunk` must have:
 *   - rhi::BufferHandle buffer
 *   - std::byte*        mapped
 *   - uint64_t          capacity
 *   - uint64_t          writePos
 *   - bool              alive
 *
 * Adapted from D:\repos\miki for mitsuki's DeviceHandle (type-erased dispatch).
 * NOT a public API. Internal to src/miki/resource/.
 *
 * Namespace: miki::resource::detail
 */
#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <deque>
#include <vector>

#include "miki/core/Result.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/Resources.h"
#include "miki/rhi/backend/AllBackends.h"

namespace miki::resource::detail {

    struct FrameRetirement {
        std::vector<uint32_t> chunkIndices;
        uint64_t fenceValue = 0;
    };

    template <typename Chunk>
    class ChunkPool {
       public:
        rhi::DeviceHandle device;
        uint64_t chunkSize = 0;
        uint32_t maxChunks = 0;

        std::vector<Chunk> chunks;
        std::vector<uint32_t> activeChunks;
        std::vector<uint32_t> freeChunks;
        std::deque<FrameRetirement> retirements;
        std::vector<uint32_t> deadSlots_;

        uint32_t aliveCount_ = 0;
        uint64_t totalAliveBytes_ = 0;

        [[nodiscard]] static constexpr auto AlignUp(uint64_t v, uint64_t a) noexcept -> uint64_t {
            return (v + a - 1) & ~(a - 1);
        }

        // =====================================================================
        // Chunk creation — uses DeviceHandle::Dispatch
        // =====================================================================

        [[nodiscard]] auto CreateChunk(uint64_t iCapacity, rhi::BufferUsage iUsage, rhi::MemoryLocation iMemory)
            -> core::Result<uint32_t> {
            if (aliveCount_ >= maxChunks) {
                return std::unexpected(core::ErrorCode::OutOfMemory);
            }

            rhi::BufferDesc bufDesc{.size = iCapacity, .usage = iUsage, .memory = iMemory};

            auto bufResult = device.Dispatch([&](auto& dev) { return dev.CreateBuffer(bufDesc); });
            if (!bufResult) return std::unexpected(core::ErrorCode::OutOfMemory);

            auto mapResult = device.Dispatch([&](auto& dev) { return dev.MapBuffer(*bufResult); });
            if (!mapResult) {
                device.Dispatch([&](auto& dev) { dev.DestroyBuffer(*bufResult); });
                return std::unexpected(core::ErrorCode::OutOfMemory);
            }

            Chunk c{};
            c.buffer = *bufResult;
            c.mapped = static_cast<std::byte*>(*mapResult);
            c.capacity = iCapacity;
            c.writePos = 0;
            c.alive = true;

            uint32_t idx;
            if (!deadSlots_.empty()) {
                idx = deadSlots_.back();
                deadSlots_.pop_back();
                chunks[idx] = c;
            } else {
                idx = static_cast<uint32_t>(chunks.size());
                chunks.push_back(c);
            }
            ++aliveCount_;
            totalAliveBytes_ += iCapacity;
            return idx;
        }

        // =====================================================================
        // Chunk destruction
        // =====================================================================

        auto DestroyChunk(uint32_t iIndex) -> void {
            auto& c = chunks[iIndex];
            if (!c.alive) return;
            device.Dispatch([&](auto& dev) {
                dev.UnmapBuffer(c.buffer);
                dev.DestroyBuffer(c.buffer);
            });
            totalAliveBytes_ -= c.capacity;
            c.alive = false;
            c.mapped = nullptr;
            c.buffer = {};
            --aliveCount_;
            deadSlots_.push_back(iIndex);
        }

        auto DestroyAllChunks() -> void {
            for (uint32_t i = 0; i < static_cast<uint32_t>(chunks.size()); ++i) {
                if (chunks[i].alive) {
                    device.Dispatch([&, i](auto& dev) {
                        dev.UnmapBuffer(chunks[i].buffer);
                        dev.DestroyBuffer(chunks[i].buffer);
                    });
                }
            }
            chunks.clear();
            activeChunks.clear();
            freeChunks.clear();
            deadSlots_.clear();
            retirements.clear();
            aliveCount_ = 0;
            totalAliveBytes_ = 0;
        }

        // =====================================================================
        // Allocation — bump allocator within chunk
        // =====================================================================

        struct AllocResult {
            uint32_t chunkIndex = ~0u;
            uint64_t offset = 0;
            std::byte* mapped = nullptr;
            bool valid = false;
        };

        [[nodiscard]] auto TryAllocFromChunk(uint32_t ci, uint64_t size, uint64_t align) -> AllocResult {
            auto& c = chunks[ci];
            uint64_t aligned = AlignUp(c.writePos, align);
            if (aligned + size > c.capacity) return {};
            c.writePos = aligned + size;
            return {.chunkIndex = ci, .offset = aligned, .mapped = c.mapped + aligned, .valid = true};
        }

        // =====================================================================
        // Active/free chunk management (O(1) swap-and-pop)
        // =====================================================================

        auto ActivateChunk(uint32_t ci) -> void { activeChunks.push_back(ci); }

        auto ActivateFromFree(size_t freeIdx) -> void {
            auto ci = freeChunks[freeIdx];
            chunks[ci].writePos = 0;
            freeChunks[freeIdx] = freeChunks.back();
            freeChunks.pop_back();
            ActivateChunk(ci);
        }

        // =====================================================================
        // Frame lifecycle
        // =====================================================================

        auto FlushFrame(uint64_t iFenceValue) -> void {
            if (activeChunks.empty()) return;

            // Flush non-coherent mapped memory so GPU sees CPU writes
            for (auto ci : activeChunks) {
                auto& c = chunks[ci];
                if (c.alive && c.writePos > 0) {
                    device.Dispatch([&](auto& dev) { dev.FlushMappedRange(c.buffer, 0, c.writePos); });
                }
            }

            retirements.push_back({.chunkIndices = std::move(activeChunks), .fenceValue = iFenceValue});
            activeChunks.clear();
        }

        auto ReclaimCompleted(uint64_t iCompletedFenceValue) -> void {
            size_t completedCount = 0;
            for (auto const& r : retirements) {
                if (r.fenceValue > iCompletedFenceValue) break;
                ++completedCount;
            }

            for (size_t i = 0; i < completedCount; ++i) {
                for (auto ci : retirements[i].chunkIndices) {
                    chunks[ci].writePos = 0;
                    freeChunks.push_back(ci);
                }
            }

            for (size_t i = 0; i < completedCount; ++i) {
                retirements.pop_front();
            }

            if (activeChunks.empty() && !freeChunks.empty()) {
                auto ci = freeChunks.back();
                freeChunks.pop_back();
                activeChunks.push_back(ci);
            }
        }

        // =====================================================================
        // Memory management (ShrinkToFit)
        // =====================================================================

        auto ShrinkToFit(uint64_t iTargetTotalBytes) -> uint64_t {
            uint64_t currentTotal = totalAliveBytes_;
            if (currentTotal <= iTargetTotalBytes) return 0;

            uint64_t freed = 0;
            for (size_t i = freeChunks.size(); i > 0 && currentTotal > iTargetTotalBytes; --i) {
                auto ci = freeChunks[i - 1];
                auto& c = chunks[ci];
                if (c.alive) {
                    freed += c.capacity;
                    currentTotal -= c.capacity;
                    DestroyChunk(ci);
                }
                freeChunks[i - 1] = freeChunks.back();
                freeChunks.pop_back();
            }
            return freed;
        }

        // =====================================================================
        // Metrics
        // =====================================================================

        [[nodiscard]] auto GetUtilization() const noexcept -> float {
            if (aliveCount_ == 0) return 0.0f;
            return 1.0f - static_cast<float>(freeChunks.size()) / static_cast<float>(aliveCount_);
        }

        [[nodiscard]] auto GetActiveChunkCount() const noexcept -> uint32_t {
            return static_cast<uint32_t>(activeChunks.size());
        }
        [[nodiscard]] auto GetFreeChunkCount() const noexcept -> uint32_t {
            return static_cast<uint32_t>(freeChunks.size());
        }
        [[nodiscard]] auto GetTotalChunkCount() const noexcept -> uint32_t { return aliveCount_; }
        [[nodiscard]] auto GetTotalAllocatedBytes() const noexcept -> uint64_t { return totalAliveBytes_; }
    };

}  // namespace miki::resource::detail
