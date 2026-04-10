/** @file TransientResourceAllocator.h
 *  @brief Phase 1 of render graph execution: transient resource allocation.
 *
 *  Owns the physical resource handle tables and manages:
 *    - Heap acquisition from TransientHeapPool (cross-frame reuse)
 *    - Placed resource creation (texture/buffer) with aliasing binding
 *    - Buffer suballocation
 *    - Imported resource registration
 *    - Physical handle table lifecycle (allocate / destroy / query)
 *
 *  The output is a PhysicalResourceTable — a set of immutable spans consumed
 *  by PassRecorder (Phase 2) and RenderPassContext.
 *
 *  Namespace: miki::rg
 */
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "miki/core/Result.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rendergraph/TransientHeapPool.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"

namespace miki::rg {

    class RenderGraphBuilder;

    // =========================================================================
    // PhysicalResourceTable — immutable view of resolved GPU handles
    // =========================================================================

    /// @brief Read-only spans into the physical handle arrays.
    /// Produced by TransientResourceAllocator::Allocate(), consumed by PassRecorder.
    struct PhysicalResourceTable {
        std::span<const rhi::TextureHandle> textures;
        std::span<const rhi::BufferHandle> buffers;
        std::span<const rhi::TextureViewHandle> textureViews;
    };

    // =========================================================================
    // Allocation statistics (Phase 1 output)
    // =========================================================================

    struct AllocationStats {
        uint32_t transientTexturesAllocated = 0;
        uint32_t transientBuffersAllocated = 0;
        uint32_t transientTextureViewsCreated = 0;
        uint32_t heapsCreated = 0;
        uint32_t heapsReused = 0;
        uint32_t heapsEvicted = 0;
        uint64_t transientMemoryBytes = 0;
        uint32_t bufferSuballocations = 0;
        uint64_t bufferSuballocBytes = 0;
    };

    // =========================================================================
    // TransientResourceAllocator
    // =========================================================================

    struct TransientAllocatorConfig {
        bool enableHeapPooling = true;
        bool enableBufferSuballocation = true;
        TransientHeapPoolConfig heapPoolConfig;
    };

    class TransientResourceAllocator {
       public:
        explicit TransientResourceAllocator(const TransientAllocatorConfig& config = {});
        ~TransientResourceAllocator();

        TransientResourceAllocator(const TransientResourceAllocator&) = delete;
        auto operator=(const TransientResourceAllocator&) -> TransientResourceAllocator& = delete;
        TransientResourceAllocator(TransientResourceAllocator&&) noexcept;
        auto operator=(TransientResourceAllocator&&) noexcept -> TransientResourceAllocator&;

        /// @brief Allocate or recycle all transient resources for a compiled graph.
        /// Populates the physical handle tables. Must be called once per frame before recording.
        [[nodiscard]] auto Allocate(
            const CompiledRenderGraph& graph, RenderGraphBuilder& builder, rhi::DeviceHandle device,
            uint32_t frameNumber
        ) -> core::Result<void>;

        /// @brief Destroy transient resources from the previous allocation.
        /// Called at the start of Allocate() and at shutdown.
        void DestroyTransients(rhi::DeviceHandle device);

        /// @brief Get a read-only view of the physical resource tables.
        [[nodiscard]] auto GetPhysicalTable() const noexcept -> PhysicalResourceTable;

        /// @brief Get allocation statistics from the last Allocate() call.
        [[nodiscard]] auto GetStats() const noexcept -> const AllocationStats& { return stats_; }

        /// @brief Access the underlying heap pool (for defrag / inspection).
        [[nodiscard]] auto GetHeapPool() noexcept -> TransientHeapPool& { return heapPool_; }
        [[nodiscard]] auto GetHeapPool() const noexcept -> const TransientHeapPool& { return heapPool_; }

        /// @brief Release all pooled resources. Call at shutdown.
        void ReleasePooledResources(rhi::DeviceHandle device) { heapPool_.ReleaseAll(device); }

        /// @brief Trigger heap pool defragmentation.
        void DefragmentHeapPool(
            const CompiledRenderGraph& graph, rhi::DeviceHandle device, uint32_t frameNumber, float vramUsageRatio,
            bool force = false
        ) {
            heapPool_.DefragmentIfNeeded(graph.aliasing, device, frameNumber, vramUsageRatio, force);
        }

       private:
        // Heap acquisition (pooled or fallback per-frame)
        auto AcquireHeaps(const AliasingLayout& aliasing, rhi::DeviceHandle device, uint32_t frameNumber)
            -> core::Result<void>;

        // Create physical resources (textures, buffers, views)
        auto CreatePhysicalResources(
            const CompiledRenderGraph& graph, RenderGraphBuilder& builder, rhi::DeviceHandle device,
            std::span<const ResourceAccess> combinedAccess
        ) -> core::Result<void>;

        // Bind a placed resource to its aliasing heap slot
        void BindToAliasingHeap(
            const AliasingLayout& aliasing, uint16_t resourceIndex, rhi::TextureHandle texture, rhi::DeviceHandle device
        );
        void BindToAliasingHeap(
            const AliasingLayout& aliasing, uint16_t resourceIndex, rhi::BufferHandle buffer, rhi::DeviceHandle device
        );

        // --- Config ---
        TransientAllocatorConfig config_;

        // --- Physical handle tables ---
        std::vector<rhi::TextureHandle> physicalTextures_;
        std::vector<rhi::BufferHandle> physicalBuffers_;
        std::vector<rhi::TextureViewHandle> physicalTextureViews_;

        // --- Transient tracking (for cleanup) ---
        struct TransientTexture {
            uint16_t resourceIndex = 0;
            rhi::TextureHandle texture;
            rhi::TextureViewHandle view;
        };
        struct TransientBuffer {
            uint16_t resourceIndex = 0;
            rhi::BufferHandle buffer;
        };
        std::vector<TransientTexture> transientTextures_;
        std::vector<TransientBuffer> transientBuffers_;

        // --- Heap management ---
        struct HeapAllocation {
            HeapGroupType group = HeapGroupType::RtDs;
            rhi::DeviceMemoryHandle heap;
            uint64_t size = 0;
        };
        std::vector<HeapAllocation> heapAllocations_;
        TransientHeapPool heapPool_;
        std::array<rhi::DeviceMemoryHandle, kHeapGroupCount> activeHeaps_ = {};

        // --- Buffer suballocation ---
        std::vector<BufferSuballocation> bufferSuballocs_;

        // --- Stats ---
        AllocationStats stats_;
    };

}  // namespace miki::rg
