/** @file TransientHeapPool.h
 *  @brief Cross-frame transient memory management for render graph execution.
 *
 *  Implements the Phase F transient memory subsystem (specs/04-render-graph.md §5.6.5-5.6.8):
 *
 *  - **HeapPool**: Cross-frame heap reuse with layoutHash matching. On graph cache hit,
 *    existing heaps are reused without memory allocation (zero-alloc steady state).
 *    On cache miss, heaps are size-matched with 20% overshoot tolerance.
 *    LRU eviction with configurable grace period prevents VRAM leaks.
 *
 *  - **TransientBufferSuballocator**: Frame-scoped linear suballocator for transient
 *    buffers within a single large buffer resource (§5.6.7). Reduces 15+
 *    CreatePlacedResource calls to 1, with 256B-aligned offset packing.
 *
 *  - **Heap defragmentation**: Periodic compaction for long-running sessions (§5.6.8).
 *    Triggered by VRAM budget pressure, heap count proliferation, or explicit request.
 *
 *  Thread safety: NOT thread-safe. Called only from executor's allocation phase
 *  (single-threaded Phase 1 of Execute/ExecuteAsync).
 *
 *  Namespace: miki::rg
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "miki/core/Result.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/Resources.h"

namespace miki::rg {

    // =========================================================================
    // Configuration
    // =========================================================================

    struct TransientHeapPoolConfig {
        uint32_t lruGraceFrames = 3;              ///< Frames before unused heaps are evicted
        float overshootTolerance = 0.20f;         ///< 20% — reuse heap if size >= required within this margin
        float oversizedReleaseThreshold = 2.0f;   ///< Release heaps exceeding 2x required size
        uint32_t defragVramThresholdFrames = 30;  ///< Consecutive frames at >90% VRAM before defrag
        float defragVramUsageRatio = 0.90f;       ///< VRAM usage ratio trigger for defrag
        float defragHeapCountRatio = 2.0f;        ///< Pool entries > this * active groups triggers defrag
    };

    // =========================================================================
    // HeapPool statistics
    // =========================================================================

    struct HeapPoolStats {
        uint32_t heapsReused = 0;           ///< Heaps matched from pool (zero-alloc)
        uint32_t heapsAllocated = 0;        ///< New heap allocations this frame
        uint32_t heapsEvicted = 0;          ///< Heaps LRU-evicted this frame
        uint32_t heapsRightsized = 0;       ///< Heaps replaced during defrag
        uint64_t totalPooledBytes = 0;      ///< Total VRAM held by pool (active + stale)
        uint64_t activeBytes = 0;           ///< VRAM actively used this frame
        uint64_t wastedBytes = 0;           ///< Over-provisioned bytes (pooled - active)
        uint32_t defragTriggered = 0;       ///< Number of defrag runs this session
        uint32_t bufferSuballocations = 0;  ///< Transient buffers suballocated (vs. placed)
        uint64_t bufferSuballocBytes = 0;   ///< Total bytes served by buffer suballocator
    };

    // =========================================================================
    // HeapPool entry — a single pooled heap
    // =========================================================================

    struct HeapPoolEntry {
        rhi::DeviceMemoryHandle heap;  ///< Backing heap handle
        uint64_t size = 0;             ///< Heap size in bytes
        HeapGroupType groupType = HeapGroupType::RtDs;
        uint64_t layoutHash = 0;     ///< Hash of aliasing layout that produced this heap
        uint32_t lastUsedFrame = 0;  ///< Frame number of last use (for LRU)
        bool active = false;         ///< true = matched to current frame's aliasing layout
    };

    // =========================================================================
    // Buffer suballocation entry
    // =========================================================================

    struct BufferSuballocation {
        uint16_t resourceIndex = 0;
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    // =========================================================================
    // TransientHeapPool
    // =========================================================================

    class TransientHeapPool {
       public:
        explicit TransientHeapPool(const TransientHeapPoolConfig& config = {}) : config_(config) {}
        ~TransientHeapPool() = default;

        TransientHeapPool(const TransientHeapPool&) = delete;
        auto operator=(const TransientHeapPool&) -> TransientHeapPool& = delete;
        TransientHeapPool(TransientHeapPool&&) noexcept = default;
        auto operator=(TransientHeapPool&&) noexcept -> TransientHeapPool& = default;

        /// @brief Acquire heaps for the current frame's aliasing layout.
        ///
        /// For each heap group with non-zero size in the aliasing layout:
        ///   1. Compute a layoutHash from group type + heap size + slot count.
        ///   2. Search pool for exact layoutHash match -> reuse (zero-alloc).
        ///   3. Else search for size-compatible heap (within overshoot tolerance) -> reuse.
        ///   4. Else allocate a new heap via device.CreateMemoryHeap.
        ///
        /// After acquisition, unused pool entries are LRU-evicted.
        ///
        /// @param aliasing   Compiled aliasing layout from RenderGraphCompiler.
        /// @param device     RHI device handle for heap creation/destruction.
        /// @param frameNumber Current frame number for LRU tracking.
        /// @param useMixedFallback If true (D3D12 Tier1), merge all groups into MixedFallback.
        /// @return Active heap handles indexed by HeapGroupType, or error.
        [[nodiscard]] auto AcquireHeaps(
            const AliasingLayout& aliasing, rhi::DeviceHandle device, uint32_t frameNumber, bool useMixedFallback
        ) -> core::Result<std::array<rhi::DeviceMemoryHandle, kHeapGroupCount>>;

        /// @brief Prepare buffer suballocations for all transient buffers.
        ///
        /// Instead of creating one placed resource per transient buffer, all buffers
        /// share a single large VkBuffer/ID3D12Resource spanning the Buffer heap.
        /// This function computes aligned offsets for each buffer.
        ///
        /// @param bufferHeap The heap backing the Buffer group.
        /// @param resources  All resource nodes from the builder.
        /// @param aliasing   Compiled aliasing layout.
        /// @param device     RHI device for parent buffer creation.
        /// @return Suballocation entries, or error if parent buffer creation fails.
        [[nodiscard]] auto PrepareBufferSuballocations(
            rhi::DeviceMemoryHandle bufferHeap, std::span<const RGResourceNode> resources,
            const AliasingLayout& aliasing, rhi::DeviceHandle device
        ) -> core::Result<std::vector<BufferSuballocation>>;

        /// @brief Get the parent buffer handle for suballocated transient buffers.
        [[nodiscard]] auto GetParentBuffer() const noexcept -> rhi::BufferHandle { return parentBuffer_; }

        /// @brief Get the parent buffer's total size.
        [[nodiscard]] auto GetParentBufferSize() const noexcept -> uint64_t { return parentBufferSize_; }

        /// @brief Trigger defragmentation if conditions are met.
        ///
        /// Conditions (any one triggers):
        ///   1. VRAM usage > config.defragVramUsageRatio for config.defragVramThresholdFrames frames.
        ///   2. Pool entry count > config.defragHeapCountRatio * active group count.
        ///   3. forceDefrag = true (explicit user request / scene transition).
        ///
        /// Algorithm:
        ///   - Compute ideal size for each active group from current aliasing layout.
        ///   - Replace over-provisioned heaps with right-sized ones.
        ///   - Evict all entries not matched to any active group.
        ///
        /// @param aliasing   Current aliasing layout.
        /// @param device     RHI device.
        /// @param frameNumber Current frame.
        /// @param vramUsageRatio Current VRAM usage / budget ratio (0-1).
        /// @param forceDefrag Force defrag regardless of conditions.
        void DefragmentIfNeeded(
            const AliasingLayout& aliasing, rhi::DeviceHandle device, uint32_t frameNumber, float vramUsageRatio,
            bool forceDefrag = false
        );

        /// @brief Release all pooled heaps and parent buffer. Call at shutdown.
        void ReleaseAll(rhi::DeviceHandle device);

        /// @brief Get statistics for the last frame.
        [[nodiscard]] auto GetStats() const noexcept -> const HeapPoolStats& { return stats_; }

        /// @brief Get total pooled heap count (active + stale).
        [[nodiscard]] auto GetPooledHeapCount() const noexcept -> uint32_t {
            return static_cast<uint32_t>(entries_.size());
        }

        /// @brief Convert HeapGroupType to RHI HeapGroupHint.
        [[nodiscard]] static constexpr auto ToGroupHint(HeapGroupType g) noexcept -> rhi::HeapGroupHint {
            switch (g) {
                case HeapGroupType::RtDs: return rhi::HeapGroupHint::RtDs;
                case HeapGroupType::NonRtDs: return rhi::HeapGroupHint::NonRtDs;
                case HeapGroupType::Buffer: return rhi::HeapGroupHint::Buffer;
                case HeapGroupType::MixedFallback: return rhi::HeapGroupHint::MixedFallback;
            }
            return rhi::HeapGroupHint::MixedFallback;
        }

       private:
        /// @brief Compute a layout hash for a heap group from its configuration.
        [[nodiscard]] static auto ComputeLayoutHash(HeapGroupType group, uint64_t heapSize, uint32_t slotCount) noexcept
            -> uint64_t;

        /// @brief Evict unused entries beyond LRU grace period.
        void EvictStale(rhi::DeviceHandle device, uint32_t frameNumber);

        /// @brief Count slots in a given heap group.
        [[nodiscard]] static auto CountSlotsInGroup(const AliasingLayout& aliasing, HeapGroupType group) noexcept
            -> uint32_t;

        // --- State ---
        TransientHeapPoolConfig config_;
        HeapPoolStats stats_;

        std::vector<HeapPoolEntry> entries_;

        // Buffer suballocator state
        rhi::BufferHandle parentBuffer_;
        rhi::DeviceMemoryHandle parentBufferHeap_;
        uint64_t parentBufferSize_ = 0;

        // Defrag tracking
        uint32_t consecutiveHighVramFrames_ = 0;
    };

    // =========================================================================
    // Alignment utilities (§5.6.6)
    // =========================================================================

    /// @brief Align a value up to the given alignment (must be power of 2).
    [[nodiscard]] constexpr auto AlignUp(uint64_t value, uint64_t alignment) noexcept -> uint64_t {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    /// @brief Compute alignment-aware heap size for a set of aliasing slots.
    /// Accounts for per-resource alignment requirements to minimize internal fragmentation.
    /// MSAA resources (4MB alignment) are packed at the end of the heap to avoid
    /// fragmenting 64KB-aligned resources (§5.6.6).
    [[nodiscard]] inline auto ComputeAlignedHeapSize(std::span<const AliasingSlot> slots, HeapGroupType group) noexcept
        -> uint64_t {
        // Separate MSAA (4MB-aligned) from non-MSAA (64KB/256B-aligned)
        uint64_t nonMsaaOffset = 0;
        uint64_t msaaOffset = 0;

        for (auto& slot : slots) {
            if (slot.heapGroup != group) {
                continue;
            }
            if (slot.alignment >= kAlignmentMsaa) {
                msaaOffset = AlignUp(msaaOffset, slot.alignment) + slot.size;
            } else {
                nonMsaaOffset = AlignUp(nonMsaaOffset, slot.alignment) + slot.size;
            }
        }

        // MSAA region starts after non-MSAA, aligned to 4MB
        if (msaaOffset > 0) {
            nonMsaaOffset = AlignUp(nonMsaaOffset, kAlignmentMsaa);
        }
        return nonMsaaOffset + msaaOffset;
    }

    /// @brief Recompute heap offsets for slots with alignment-aware first-fit-decreasing packing.
    /// Modifies slot.heapOffset in-place. MSAA resources are packed into their own sub-region.
    /// Returns the total aligned heap size.
    [[nodiscard]] inline auto PackSlotsAligned(std::span<AliasingSlot> slots, HeapGroupType group) noexcept
        -> uint64_t {
        // Collect indices for this group, separate MSAA from non-MSAA
        struct SlotRef {
            uint32_t idx;
            bool isMsaa;
        };
        std::vector<SlotRef> refs;
        refs.reserve(slots.size());
        for (uint32_t i = 0; i < slots.size(); ++i) {
            if (slots[i].heapGroup == group) {
                refs.push_back({i, slots[i].alignment >= kAlignmentMsaa});
            }
        }

        // Sort by size descending (first-fit-decreasing) — MSAA last for sub-region packing
        std::sort(refs.begin(), refs.end(), [&](const SlotRef& a, const SlotRef& b) {
            if (a.isMsaa != b.isMsaa) {
                return !a.isMsaa;  // non-MSAA first
            }
            return slots[a.idx].size > slots[b.idx].size;
        });

        uint64_t offset = 0;
        for (auto& ref : refs) {
            auto& s = slots[ref.idx];
            offset = AlignUp(offset, s.alignment);
            s.heapOffset = offset;
            offset += s.size;
        }
        return offset;
    }

}  // namespace miki::rg
