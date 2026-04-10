/** @file TransientHeapPool.cpp
 *  @brief Implementation of cross-frame transient memory management.
 *
 *  See: specs/04-render-graph.md §5.6.5-5.6.8
 */

#include "miki/rendergraph/TransientHeapPool.h"

#include "miki/rhi/backend/AllBackends.h"

#include <cassert>
#include <cstring>
#include <numeric>

namespace miki::rg {

    // =========================================================================
    // ComputeLayoutHash — deterministic hash from group config
    // =========================================================================

    auto TransientHeapPool::ComputeLayoutHash(
        HeapGroupType group, uint64_t heapSize, uint32_t slotCount
    ) noexcept -> uint64_t {
        // FNV-1a style hash combining group type, heap size, and slot count.
        // This uniquely identifies a particular aliasing configuration.
        constexpr uint64_t kFnvBasis = 14695981039346656037ULL;
        constexpr uint64_t kFnvPrime = 1099511628211ULL;

        auto h = kFnvBasis;
        auto mix = [&](uint64_t val) {
            h ^= val;
            h *= kFnvPrime;
        };
        mix(static_cast<uint64_t>(group));
        mix(heapSize);
        mix(static_cast<uint64_t>(slotCount));
        return h;
    }

    // =========================================================================
    // CountSlotsInGroup
    // =========================================================================

    auto TransientHeapPool::CountSlotsInGroup(
        const AliasingLayout& aliasing, HeapGroupType group
    ) noexcept -> uint32_t {
        uint32_t count = 0;
        for (auto& slot : aliasing.slots) {
            if (slot.heapGroup == group) {
                ++count;
            }
        }
        return count;
    }

    // =========================================================================
    // AcquireHeaps — main per-frame entry point
    // =========================================================================

    auto TransientHeapPool::AcquireHeaps(
        const AliasingLayout& aliasing, rhi::DeviceHandle device, uint32_t frameNumber, bool useMixedFallback
    ) -> core::Result<std::array<rhi::DeviceMemoryHandle, kHeapGroupCount>> {
        stats_ = {};  // Reset per-frame stats

        std::array<rhi::DeviceMemoryHandle, kHeapGroupCount> result = {};

        // Mark all entries inactive for this frame
        for (auto& e : entries_) {
            e.active = false;
        }

        for (uint32_t g = 0; g < kHeapGroupCount; ++g) {
            auto groupType = static_cast<HeapGroupType>(g);
            uint64_t requiredSize = aliasing.heapGroupSizes[g];

            // D3D12 Tier1 fallback: merge all groups into MixedFallback
            if (useMixedFallback && groupType != HeapGroupType::MixedFallback) {
                continue;  // Skip individual groups; MixedFallback accumulates total
            }
            if (useMixedFallback && groupType == HeapGroupType::MixedFallback) {
                // Sum all group sizes into mixed fallback
                requiredSize = 0;
                for (uint32_t i = 0; i < kHeapGroupCount; ++i) {
                    requiredSize += aliasing.heapGroupSizes[i];
                }
            }

            if (requiredSize == 0) {
                continue;
            }

            // Compute layout hash for matching
            uint32_t slotCount = useMixedFallback
                                     ? static_cast<uint32_t>(aliasing.slots.size())
                                     : CountSlotsInGroup(aliasing, groupType);
            auto targetHash = ComputeLayoutHash(groupType, requiredSize, slotCount);

            // --- Strategy 1: Exact layoutHash match (cache hit, zero-alloc) ---
            bool matched = false;
            for (auto& e : entries_) {
                if (!e.active && e.groupType == groupType && e.layoutHash == targetHash) {
                    e.active = true;
                    e.lastUsedFrame = frameNumber;
                    result[g] = e.heap;
                    stats_.heapsReused++;
                    stats_.activeBytes += e.size;
                    matched = true;
                    break;
                }
            }
            if (matched) {
                continue;
            }

            // --- Strategy 2: Size-compatible match (within overshoot tolerance) ---
            HeapPoolEntry* bestSizeMatch = nullptr;
            uint64_t bestOvershoot = UINT64_MAX;
            for (auto& e : entries_) {
                if (e.active || e.groupType != groupType) {
                    continue;
                }
                if (e.size >= requiredSize) {
                    auto maxAcceptable = static_cast<uint64_t>(
                        static_cast<double>(requiredSize) * (1.0 + config_.overshootTolerance)
                    );
                    if (e.size <= maxAcceptable) {
                        uint64_t overshoot = e.size - requiredSize;
                        if (overshoot < bestOvershoot) {
                            bestOvershoot = overshoot;
                            bestSizeMatch = &e;
                        }
                    }
                }
            }
            if (bestSizeMatch != nullptr) {
                bestSizeMatch->active = true;
                bestSizeMatch->lastUsedFrame = frameNumber;
                bestSizeMatch->layoutHash = targetHash;  // Update hash for future exact matches
                result[g] = bestSizeMatch->heap;
                stats_.heapsReused++;
                stats_.activeBytes += bestSizeMatch->size;
                stats_.wastedBytes += bestSizeMatch->size - requiredSize;
                continue;
            }

            // --- Strategy 3: Allocate new heap ---
            // Check for oversized entries to release before allocating
            for (auto it = entries_.begin(); it != entries_.end();) {
                if (!it->active && it->groupType == groupType
                    && it->size > static_cast<uint64_t>(
                           static_cast<double>(requiredSize) * config_.oversizedReleaseThreshold
                       )) {
                    device.Dispatch([&](auto& dev) { dev.DestroyMemoryHeap(it->heap); });
                    stats_.heapsEvicted++;
                    it = entries_.erase(it);
                } else {
                    ++it;
                }
            }

            auto heapResult = device.Dispatch([&](auto& dev) {
                return dev.CreateMemoryHeap(rhi::MemoryHeapDesc{
                    .size = requiredSize,
                    .memory = rhi::MemoryLocation::GpuOnly,
                    .groupHint = ToGroupHint(groupType),
                    .debugName = "RG Transient Heap",
                });
            });
            if (!heapResult) {
                return std::unexpected(core::ErrorCode::OutOfMemory);
            }

            entries_.push_back({
                .heap = *heapResult,
                .size = requiredSize,
                .groupType = groupType,
                .layoutHash = targetHash,
                .lastUsedFrame = frameNumber,
                .active = true,
            });
            result[g] = *heapResult;
            stats_.heapsAllocated++;
            stats_.activeBytes += requiredSize;
        }

        // Compute total pooled bytes
        stats_.totalPooledBytes = 0;
        for (auto& e : entries_) {
            stats_.totalPooledBytes += e.size;
        }
        stats_.wastedBytes = stats_.totalPooledBytes - stats_.activeBytes;

        // LRU eviction of stale entries
        EvictStale(device, frameNumber);

        return result;
    }

    // =========================================================================
    // PrepareBufferSuballocations — linear offset packing (§5.6.7)
    // =========================================================================

    auto TransientHeapPool::PrepareBufferSuballocations(
        rhi::DeviceMemoryHandle bufferHeap, std::span<const RGResourceNode> resources,
        const AliasingLayout& aliasing, rhi::DeviceHandle device
    ) -> core::Result<std::vector<BufferSuballocation>> {
        std::vector<BufferSuballocation> result;

        // Collect transient buffer indices assigned to aliasing slots
        struct BufferInfo {
            uint16_t resourceIndex;
            uint64_t size;
            uint64_t alignment;
        };
        std::vector<BufferInfo> buffers;

        for (uint16_t ri = 0; ri < resources.size(); ++ri) {
            auto& res = resources[ri];
            if (res.imported || res.kind != RGResourceKind::Buffer) {
                continue;
            }
            if (ri < aliasing.resourceToSlot.size() && aliasing.resourceToSlot[ri] != AliasingLayout::kNotAliased) {
                auto& slot = aliasing.slots[aliasing.resourceToSlot[ri]];
                if (slot.heapGroup == HeapGroupType::Buffer) {
                    buffers.push_back({
                        .resourceIndex = ri,
                        .size = res.bufferDesc.size,
                        .alignment = std::max(slot.alignment, kAlignmentBuffer),
                    });
                }
            }
        }

        if (buffers.empty()) {
            return result;
        }

        // Compute total required size with alignment
        uint64_t totalSize = 0;
        for (auto& b : buffers) {
            totalSize = AlignUp(totalSize, b.alignment) + b.size;
        }

        // Destroy previous parent buffer if exists
        if (parentBuffer_.IsValid()) {
            device.Dispatch([&](auto& dev) { dev.DestroyBuffer(parentBuffer_); });
            parentBuffer_ = {};
            parentBufferSize_ = 0;
        }

        // Create a single parent buffer spanning the entire buffer heap
        auto bufResult = device.Dispatch([&](auto& dev) {
            return dev.CreateBuffer(rhi::BufferDesc{
                .size = totalSize,
                .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect | rhi::BufferUsage::TransferSrc
                         | rhi::BufferUsage::TransferDst,
                .memory = rhi::MemoryLocation::GpuOnly,
                .debugName = "RG Transient Buffer Pool",
            });
        });
        if (!bufResult) {
            return std::unexpected(core::ErrorCode::OutOfMemory);
        }

        parentBuffer_ = *bufResult;
        parentBufferSize_ = totalSize;

        // Bind parent buffer to heap
        if (bufferHeap.IsValid()) {
            device.Dispatch([&](auto& dev) { dev.AliasBufferMemory(parentBuffer_, bufferHeap, 0); });
        }

        // Compute per-buffer offsets (linear packing, 256B minimum alignment)
        uint64_t offset = 0;
        result.reserve(buffers.size());
        for (auto& b : buffers) {
            offset = AlignUp(offset, b.alignment);
            result.push_back({
                .resourceIndex = b.resourceIndex,
                .offset = offset,
                .size = b.size,
            });
            offset += b.size;
        }

        stats_.bufferSuballocations = static_cast<uint32_t>(result.size());
        stats_.bufferSuballocBytes = offset;

        return result;
    }

    // =========================================================================
    // EvictStale — LRU eviction
    // =========================================================================

    void TransientHeapPool::EvictStale(rhi::DeviceHandle device, uint32_t frameNumber) {
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (!it->active && frameNumber >= it->lastUsedFrame + config_.lruGraceFrames) {
                device.Dispatch([&](auto& dev) { dev.DestroyMemoryHeap(it->heap); });
                stats_.heapsEvicted++;
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // =========================================================================
    // DefragmentIfNeeded — periodic compaction (§5.6.8)
    // =========================================================================

    void TransientHeapPool::DefragmentIfNeeded(
        const AliasingLayout& aliasing, rhi::DeviceHandle device, uint32_t frameNumber, float vramUsageRatio,
        bool forceDefrag
    ) {
        // Track consecutive high-VRAM frames
        if (vramUsageRatio >= config_.defragVramUsageRatio) {
            ++consecutiveHighVramFrames_;
        } else {
            consecutiveHighVramFrames_ = 0;
        }

        // Count active groups
        uint32_t activeGroupCount = 0;
        for (uint32_t g = 0; g < kHeapGroupCount; ++g) {
            if (aliasing.heapGroupSizes[g] > 0) {
                ++activeGroupCount;
            }
        }

        // Check trigger conditions
        bool vramPressure = consecutiveHighVramFrames_ >= config_.defragVramThresholdFrames;
        bool heapProliferation = activeGroupCount > 0
                                 && entries_.size()
                                        > static_cast<size_t>(
                                            static_cast<float>(activeGroupCount) * config_.defragHeapCountRatio
                                        );

        if (!forceDefrag && !vramPressure && !heapProliferation) {
            return;
        }

        stats_.defragTriggered++;
        consecutiveHighVramFrames_ = 0;

        // Phase 1: Right-size over-provisioned active heaps
        for (auto& e : entries_) {
            if (!e.active) {
                continue;
            }
            uint32_t g = static_cast<uint32_t>(e.groupType);
            uint64_t idealSize = aliasing.heapGroupSizes[g];
            if (idealSize == 0 || e.size <= idealSize) {
                continue;
            }

            // Heap is over-provisioned — replace with right-sized one
            auto heapResult = device.Dispatch([&](auto& dev) {
                return dev.CreateMemoryHeap(rhi::MemoryHeapDesc{
                    .size = idealSize,
                    .memory = rhi::MemoryLocation::GpuOnly,
                    .groupHint = ToGroupHint(e.groupType),
                    .debugName = "RG Transient Heap (defrag)",
                });
            });
            if (heapResult) {
                device.Dispatch([&](auto& dev) { dev.DestroyMemoryHeap(e.heap); });
                e.heap = *heapResult;
                e.size = idealSize;
                e.layoutHash = ComputeLayoutHash(
                    e.groupType, idealSize, CountSlotsInGroup(aliasing, e.groupType)
                );
                stats_.heapsRightsized++;
            }
        }

        // Phase 2: Evict all entries not matched to any active group
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (!it->active) {
                device.Dispatch([&](auto& dev) { dev.DestroyMemoryHeap(it->heap); });
                stats_.heapsEvicted++;
                it = entries_.erase(it);
            } else {
                it->lastUsedFrame = frameNumber;  // Reset LRU for surviving entries
                ++it;
            }
        }
    }

    // =========================================================================
    // ReleaseAll — shutdown cleanup
    // =========================================================================

    void TransientHeapPool::ReleaseAll(rhi::DeviceHandle device) {
        for (auto& e : entries_) {
            if (e.heap.IsValid()) {
                device.Dispatch([&](auto& dev) { dev.DestroyMemoryHeap(e.heap); });
            }
        }
        entries_.clear();

        if (parentBuffer_.IsValid()) {
            device.Dispatch([&](auto& dev) { dev.DestroyBuffer(parentBuffer_); });
            parentBuffer_ = {};
            parentBufferSize_ = 0;
        }

        stats_ = {};
        consecutiveHighVramFrames_ = 0;
    }

}  // namespace miki::rg
