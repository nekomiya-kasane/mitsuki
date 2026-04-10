/** @file TransientResourceAllocator.cpp
 *  @brief Phase 1 of render graph execution: transient resource allocation.
 *  See: TransientResourceAllocator.h
 */

#include "miki/rendergraph/TransientResourceAllocator.h"

#include "miki/rendergraph/BarrierEmitter.h"
#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rhi/backend/AllBackends.h"

#include <cassert>
#include <utility>

namespace miki::rg {

    // =========================================================================
    // Construction / Move
    // =========================================================================

    TransientResourceAllocator::TransientResourceAllocator(const TransientAllocatorConfig& config)
        : config_(config), heapPool_(config.heapPoolConfig) {}

    TransientResourceAllocator::~TransientResourceAllocator() = default;
    TransientResourceAllocator::TransientResourceAllocator(TransientResourceAllocator&&) noexcept = default;
    auto TransientResourceAllocator::operator=(TransientResourceAllocator&&) noexcept
        -> TransientResourceAllocator& = default;

    // =========================================================================
    // Allocate — main entry point
    // =========================================================================

    auto TransientResourceAllocator::Allocate(
        const CompiledRenderGraph& graph, RenderGraphBuilder& builder, rhi::DeviceHandle device, uint32_t frameNumber
    ) -> core::Result<void> {
        auto& resources = builder.GetResources();
        auto resourceCount = static_cast<uint16_t>(resources.size());

        // Reset stats
        stats_ = {};

        // Resize physical handle tables
        physicalTextures_.assign(resourceCount, {});
        physicalBuffers_.assign(resourceCount, {});
        physicalTextureViews_.assign(resourceCount, {});

        // Clean up previous frame's transient resources
        DestroyTransients(device);

        heapAllocations_.clear();
        bufferSuballocs_.clear();

        // Step 1: Acquire heaps
        auto heapResult = AcquireHeaps(graph.aliasing, device, frameNumber);
        if (!heapResult) {
            return heapResult;
        }

        // Propagate heap pool stats
        auto& poolStats = heapPool_.GetStats();
        stats_.heapsReused = poolStats.heapsReused;
        stats_.heapsEvicted = poolStats.heapsEvicted;
        stats_.bufferSuballocations = poolStats.bufferSuballocations;
        stats_.bufferSuballocBytes = poolStats.bufferSuballocBytes;

        // Step 2: Buffer suballocation
        bool useSuballoc
            = config_.enableBufferSuballocation && activeHeaps_[static_cast<uint32_t>(HeapGroupType::Buffer)].IsValid();
        if (useSuballoc) {
            auto subResult = heapPool_.PrepareBufferSuballocations(
                activeHeaps_[static_cast<uint32_t>(HeapGroupType::Buffer)], resources, graph.aliasing, device
            );
            if (subResult) {
                bufferSuballocs_ = std::move(*subResult);
            }
        }

        // Step 3: Pre-compute combined access (O(P+R) instead of O(P*R))
        auto combinedAccess = PrecomputeCombinedAccess(builder.GetPasses(), resourceCount);

        // Step 4: Create physical resources
        return CreatePhysicalResources(graph, builder, device, combinedAccess);
    }

    // =========================================================================
    // AcquireHeaps
    // =========================================================================

    auto TransientResourceAllocator::AcquireHeaps(
        const AliasingLayout& aliasing, rhi::DeviceHandle device, uint32_t frameNumber
    ) -> core::Result<void> {
        if (config_.enableHeapPooling) {
            bool useMixed = device.Dispatch([](auto& dev) {
                using RT = decltype(dev.GetCapabilities().resourceHeapTier);
                return dev.GetCapabilities().resourceHeapTier == RT::Tier1;
            });

            auto heapsResult = heapPool_.AcquireHeaps(aliasing, device, frameNumber, useMixed);
            if (!heapsResult) {
                return std::unexpected(core::ErrorCode::OutOfMemory);
            }
            activeHeaps_ = *heapsResult;

            for (uint32_t g = 0; g < kHeapGroupCount; ++g) {
                if (activeHeaps_[g].IsValid()) {
                    uint64_t heapSize = aliasing.heapGroupSizes[g];
                    heapAllocations_.push_back({
                        .group = static_cast<HeapGroupType>(g),
                        .heap = activeHeaps_[g],
                        .size = heapSize,
                    });
                    stats_.heapsCreated += (heapPool_.GetStats().heapsAllocated > 0) ? 1 : 0;
                    stats_.transientMemoryBytes += heapSize;
                }
            }
        } else {
            for (uint32_t g = 0; g < kHeapGroupCount; ++g) {
                uint64_t heapSize = aliasing.heapGroupSizes[g];
                if (heapSize == 0) {
                    continue;
                }

                auto groupType = static_cast<HeapGroupType>(g);
                auto heapResult = device.Dispatch([&](auto& dev) {
                    return dev.CreateMemoryHeap(
                        rhi::MemoryHeapDesc{
                            .size = heapSize,
                            .memory = rhi::MemoryLocation::GpuOnly,
                            .groupHint = TransientHeapPool::ToGroupHint(groupType),
                            .debugName = "RG Transient Heap",
                        }
                    );
                });
                if (!heapResult) {
                    return std::unexpected(core::ErrorCode::OutOfMemory);
                }

                heapAllocations_.push_back({
                    .group = groupType,
                    .heap = *heapResult,
                    .size = heapSize,
                });
                activeHeaps_[g] = *heapResult;
                stats_.heapsCreated++;
                stats_.transientMemoryBytes += heapSize;
            }
        }
        return {};
    }

    // =========================================================================
    // CreatePhysicalResources
    // =========================================================================

    auto TransientResourceAllocator::CreatePhysicalResources(
        const CompiledRenderGraph& graph, RenderGraphBuilder& builder, rhi::DeviceHandle device,
        std::span<const ResourceAccess> combinedAccess
    ) -> core::Result<void> {
        auto& resources = builder.GetResources();
        auto resourceCount = static_cast<uint16_t>(resources.size());
        auto& aliasing = graph.aliasing;

        // Build suballocated buffer lookup
        std::vector<bool> isSuballocated(resourceCount, false);
        for (auto& sub : bufferSuballocs_) {
            if (sub.resourceIndex < resourceCount) {
                isSuballocated[sub.resourceIndex] = true;
                physicalBuffers_[sub.resourceIndex] = heapPool_.GetParentBuffer();
            }
        }

        for (uint16_t ri = 0; ri < resourceCount; ++ri) {
            auto& resNode = resources[ri];

            // --- Imported resources ---
            if (resNode.imported) {
                if (resNode.kind == RGResourceKind::Texture) {
                    physicalTextures_[ri] = resNode.importedTexture;
                    if (resNode.importedTexture.IsValid()) {
                        auto viewResult = device.Dispatch([&](auto& dev) {
                            return dev.CreateTextureView(rhi::TextureViewDesc{.texture = resNode.importedTexture});
                        });
                        if (viewResult) {
                            physicalTextureViews_[ri] = *viewResult;
                            transientTextures_.push_back({ri, {}, *viewResult});
                        }
                    }
                } else {
                    physicalBuffers_[ri] = resNode.importedBuffer;
                }
                continue;
            }

            // --- Suballocated buffers (already handled) ---
            if (isSuballocated[ri]) {
                stats_.transientBuffersAllocated++;
                continue;
            }

            auto access = ri < combinedAccess.size() ? combinedAccess[ri] : ResourceAccess::None;

            if (resNode.kind == RGResourceKind::Texture) {
                auto inferredUsage = InferTextureUsage(access);
                auto rhiDesc = resNode.textureDesc.ToRhiDesc(inferredUsage);

                auto texResult = device.Dispatch([&](auto& dev) { return dev.CreateTexture(rhiDesc); });
                if (!texResult) {
                    return std::unexpected(core::ErrorCode::OutOfMemory);
                }

                physicalTextures_[ri] = *texResult;
                stats_.transientTexturesAllocated++;

                BindToAliasingHeap(aliasing, ri, *texResult, device);

                // Create default texture view
                auto viewResult = device.Dispatch([&](auto& dev) {
                    return dev.CreateTextureView(rhi::TextureViewDesc{.texture = *texResult});
                });
                if (viewResult) {
                    physicalTextureViews_[ri] = *viewResult;
                    stats_.transientTextureViewsCreated++;
                }

                transientTextures_.push_back({
                    .resourceIndex = ri,
                    .texture = *texResult,
                    .view = viewResult ? *viewResult : rhi::TextureViewHandle{},
                });
            } else {
                auto inferredUsage = InferBufferUsage(access);
                auto rhiDesc = resNode.bufferDesc.ToRhiDesc(inferredUsage);

                auto bufResult = device.Dispatch([&](auto& dev) { return dev.CreateBuffer(rhiDesc); });
                if (!bufResult) {
                    return std::unexpected(core::ErrorCode::OutOfMemory);
                }

                physicalBuffers_[ri] = *bufResult;
                stats_.transientBuffersAllocated++;

                BindToAliasingHeap(aliasing, ri, *bufResult, device);

                transientBuffers_.push_back({.resourceIndex = ri, .buffer = *bufResult});
            }
        }

        return {};
    }

    // =========================================================================
    // BindToAliasingHeap (texture overload)
    // =========================================================================

    void TransientResourceAllocator::BindToAliasingHeap(
        const AliasingLayout& aliasing, uint16_t resourceIndex, rhi::TextureHandle texture, rhi::DeviceHandle device
    ) {
        if (resourceIndex >= aliasing.resourceToSlot.size()
            || aliasing.resourceToSlot[resourceIndex] == AliasingLayout::kNotAliased) {
            return;
        }
        uint32_t slotIdx = aliasing.resourceToSlot[resourceIndex];
        if (slotIdx >= aliasing.slots.size()) {
            return;
        }
        auto& slot = aliasing.slots[slotIdx];
        for (auto& heap : heapAllocations_) {
            if (heap.group == slot.heapGroup) {
                device.Dispatch([&](auto& dev) { dev.AliasTextureMemory(texture, heap.heap, slot.heapOffset); });
                break;
            }
        }
    }

    // =========================================================================
    // BindToAliasingHeap (buffer overload)
    // =========================================================================

    void TransientResourceAllocator::BindToAliasingHeap(
        const AliasingLayout& aliasing, uint16_t resourceIndex, rhi::BufferHandle buffer, rhi::DeviceHandle device
    ) {
        if (resourceIndex >= aliasing.resourceToSlot.size()
            || aliasing.resourceToSlot[resourceIndex] == AliasingLayout::kNotAliased) {
            return;
        }
        uint32_t slotIdx = aliasing.resourceToSlot[resourceIndex];
        if (slotIdx >= aliasing.slots.size()) {
            return;
        }
        auto& slot = aliasing.slots[slotIdx];
        for (auto& heap : heapAllocations_) {
            if (heap.group == slot.heapGroup) {
                device.Dispatch([&](auto& dev) { dev.AliasBufferMemory(buffer, heap.heap, slot.heapOffset); });
                break;
            }
        }
    }

    // =========================================================================
    // DestroyTransients
    // =========================================================================

    void TransientResourceAllocator::DestroyTransients(rhi::DeviceHandle device) {
        for (auto& tt : transientTextures_) {
            if (tt.view.IsValid()) {
                device.Dispatch([&](auto& dev) { dev.DestroyTextureView(tt.view); });
            }
            if (tt.texture.IsValid()) {
                device.Dispatch([&](auto& dev) { dev.DestroyTexture(tt.texture); });
            }
        }
        transientTextures_.clear();

        for (auto& tb : transientBuffers_) {
            if (tb.buffer.IsValid()) {
                device.Dispatch([&](auto& dev) { dev.DestroyBuffer(tb.buffer); });
            }
        }
        transientBuffers_.clear();

        for (auto& ha : heapAllocations_) {
            if (ha.heap.IsValid()) {
                device.Dispatch([&](auto& dev) { dev.DestroyMemoryHeap(ha.heap); });
            }
        }
        heapAllocations_.clear();
    }

    // =========================================================================
    // GetPhysicalTable
    // =========================================================================

    auto TransientResourceAllocator::GetPhysicalTable() const noexcept -> PhysicalResourceTable {
        return {
            .textures = physicalTextures_,
            .buffers = physicalBuffers_,
            .textureViews = physicalTextureViews_,
        };
    }

}  // namespace miki::rg
