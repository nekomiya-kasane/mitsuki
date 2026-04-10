/** @file RenderGraphExecutor.h
 *  @brief Runtime execution engine for compiled render graphs.
 *
 *  RenderGraphExecutor takes a CompiledRenderGraph (immutable output of the compiler)
 *  and executes it through three phases:
 *    1. AllocateTransients — create/recycle transient GPU resources from heap pools
 *    2. RecordPasses — emit barriers + invoke pass execute lambdas into command buffers
 *    3. SubmitBatches — submit command buffers to queues with timeline sync
 *
 *  Backend-agnostic: all GPU operations go through DeviceHandle::Dispatch and
 *  CommandListHandle::Dispatch (CRTP, zero per-draw overhead).
 *
 *  Thread model:
 *    - Single-threaded (Step 1): all recording on caller thread
 *    - Parallel recording (Step 2, E-5): per-thread secondary cmd bufs, barriers in primary
 *    - Async execution (Step 3, E-7): offload phases 1-2 to worker threads
 *
 *  See: specs/04-render-graph.md §6
 *  Namespace: miki::rg
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <future>
#include <latch>
#include <span>
#include <thread>
#include <vector>

#include "miki/core/LinearAllocator.h"
#include "miki/core/Result.h"
#include "miki/frame/CommandPoolAllocator.h"
#include "miki/frame/FrameContext.h"
#include "miki/frame/SyncScheduler.h"
#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rendergraph/RenderPassContext.h"
#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/Pipeline.h"
#include "miki/rhi/Resources.h"
#include "miki/rhi/Sync.h"

namespace miki::resource {
    class ReadbackRing;
}

namespace miki::rg {

    // =========================================================================
    // Queue type mapping
    // =========================================================================

    /// @brief Map graph-level RGQueueType to RHI QueueType.
    [[nodiscard]] constexpr auto ToRhiQueueType(RGQueueType q) noexcept -> rhi::QueueType {
        switch (q) {
            case RGQueueType::Graphics: return rhi::QueueType::Graphics;
            case RGQueueType::AsyncCompute: return rhi::QueueType::Compute;
            case RGQueueType::Transfer: return rhi::QueueType::Transfer;
        }
        return rhi::QueueType::Graphics;
    }

    // =========================================================================
    // Executor configuration
    // =========================================================================

    struct ExecutorConfig {
        uint32_t maxRecordingThreads = 1;              ///< 1 = single-threaded, >1 = parallel (T1 only)
        bool enableParallelRecording = false;          ///< Use secondary cmd bufs for parallel recording
        bool enableAsyncExecution = false;             ///< Offload allocation + recording to worker threads
        bool enableDebugLabels = true;                 ///< Emit per-pass debug labels for PIX/RenderDoc/NSight
        uint64_t frameAllocatorCapacity = 256 * 1024;  ///< Per-frame arena capacity (bytes)
    };

    // =========================================================================
    // Execution statistics (profiling)
    // =========================================================================

    struct ExecutionStats {
        uint32_t transientTexturesAllocated = 0;
        uint32_t transientBuffersAllocated = 0;
        uint32_t transientTextureViewsCreated = 0;
        uint32_t heapsCreated = 0;
        uint32_t barriersEmitted = 0;
        uint32_t aliasingBarriersEmitted = 0;
        uint32_t batchesSubmitted = 0;
        uint32_t passesRecorded = 0;
        uint32_t secondaryCmdBufsUsed = 0;
        uint64_t transientMemoryBytes = 0;
    };

    // =========================================================================
    // RenderGraphExecutor
    // =========================================================================

    class RenderGraphExecutor {
       public:
        explicit RenderGraphExecutor(const ExecutorConfig& config = {});
        ~RenderGraphExecutor();

        RenderGraphExecutor(const RenderGraphExecutor&) = delete;
        auto operator=(const RenderGraphExecutor&) -> RenderGraphExecutor& = delete;
        RenderGraphExecutor(RenderGraphExecutor&&) noexcept;
        auto operator=(RenderGraphExecutor&&) noexcept -> RenderGraphExecutor&;

        /// @brief Execute a compiled render graph: allocate, record, submit.
        /// This is the main entry point. Caller must ensure:
        ///   - graph is a valid CompiledRenderGraph from RenderGraphCompiler::Compile()
        ///   - builder is the RenderGraphBuilder that produced the graph (for pass/resource metadata)
        ///   - frame is the current FrameContext from FrameManager::BeginFrame()
        ///   - device is a valid DeviceHandle
        ///   - scheduler is initialized with device queue timelines
        ///   - poolAllocator is initialized with matching framesInFlight
        [[nodiscard]] auto Execute(
            const CompiledRenderGraph& graph, RenderGraphBuilder& builder, const frame::FrameContext& frame,
            rhi::DeviceHandle device, frame::SyncScheduler& scheduler, frame::CommandPoolAllocator& poolAllocator
        ) -> core::Result<void>;

        /// @brief Get execution statistics from the last Execute() call.
        [[nodiscard]] auto GetStats() const noexcept -> const ExecutionStats& { return stats_; }

        /// @brief Set optional ReadbackRing for GPU->CPU readback integration (E-10).
        void SetReadbackRing(resource::ReadbackRing* ring) noexcept { readbackRing_ = ring; }

        /// @brief Async execution (E-7): offload Phase 1 + 2 to a worker thread.
        /// Returns a future that resolves when allocation + recording are done.
        /// Caller must call SubmitAfterAsync() on the render thread after .get().
        /// Requires config.enableAsyncExecution = true.
        [[nodiscard]] auto ExecuteAsync(
            const CompiledRenderGraph& graph, RenderGraphBuilder& builder, const frame::FrameContext& frame,
            rhi::DeviceHandle device, frame::CommandPoolAllocator& poolAllocator
        ) -> std::future<core::Result<void>>;

        /// @brief Submit batches after async Phase 1+2 completes.
        /// Must be called on the render thread after ExecuteAsync future resolves successfully.
        [[nodiscard]] auto SubmitAfterAsync(
            const CompiledRenderGraph& graph, rhi::DeviceHandle device, frame::SyncScheduler& scheduler
        ) -> core::Result<void>;

       private:
        // Phase 1: Allocate/recycle transient resources from heap pools
        auto AllocateTransients(const CompiledRenderGraph& graph, RenderGraphBuilder& builder, rhi::DeviceHandle device)
            -> core::Result<void>;

        // Phase 2a: Record command buffers (single-threaded path)
        auto RecordPasses(
            const CompiledRenderGraph& graph, RenderGraphBuilder& builder, const frame::FrameContext& frame,
            rhi::DeviceHandle device, frame::CommandPoolAllocator& poolAllocator
        ) -> core::Result<void>;

        // Phase 2b: Record command buffers (parallel path, E-5/E-6)
        // Barriers in primary, pass lambdas in per-thread secondary cmd bufs.
        auto RecordPassesParallel(
            const CompiledRenderGraph& graph, RenderGraphBuilder& builder, const frame::FrameContext& frame,
            rhi::DeviceHandle device, frame::CommandPoolAllocator& poolAllocator
        ) -> core::Result<void>;

        // Merged group membership info for a compiled pass
        struct MergedGroupMembership {
            uint32_t groupIndex = UINT32_MAX;  ///< Index into CompiledRenderGraph::mergedGroups
            uint32_t subpassPosition = 0;      ///< 0-based position within the merged group
            bool isFirst = false;              ///< true if this is the first subpass
            bool isLast = false;               ///< true if this is the last subpass
        };

        // Record a single pass into a command list (shared by single/parallel paths)
        // If mergedMembership is non-null and the pass is inside a merged group, per-pass
        // CmdBeginRendering/CmdEndRendering is skipped — the caller must bracket the group.
        void RecordSinglePass(
            rhi::CommandListHandle& cmdList, const CompiledRenderGraph& graph, uint32_t compiledPassIndex,
            const CompiledPassInfo& compiledPass, RGPassNode& passNode, const frame::FrameContext& frame,
            bool emitBarriers, RenderGraphBuilder& builder, const MergedGroupMembership* mergedMembership = nullptr
        );

        // Phase 3: Submit batches to queues with sync metadata
        auto SubmitBatches(const CompiledRenderGraph& graph, rhi::DeviceHandle device, frame::SyncScheduler& scheduler)
            -> core::Result<void>;

        // Destroy transient resources created in AllocateTransients
        void DestroyTransients(rhi::DeviceHandle device);

        // Translate a BarrierCommand to RHI barrier descriptors and record into command list
        void EmitBarriers(
            rhi::CommandListHandle& cmd, std::span<const BarrierCommand> barriers, RenderGraphBuilder& builder
        );

        // Build RenderingAttachment list for a graphics pass from its write accesses
        void BuildRenderingAttachments(
            const RGPassNode& passNode, RenderGraphBuilder& builder, std::vector<rhi::RenderingAttachment>& outColor,
            rhi::RenderingAttachment& outDepth, bool& hasDepth
        );

        // Build a lookup table: compiledPassIndex -> merged group membership
        auto BuildMergedGroupLookup(const CompiledRenderGraph& graph) const -> std::vector<MergedGroupMembership>;

        // Build combined rendering attachments for a merged group (union of all subpass attachments)
        void BuildMergedGroupAttachments(
            const MergedRenderPassGroup& group, const CompiledRenderGraph& graph, RenderGraphBuilder& builder,
            std::vector<rhi::RenderingAttachment>& outColor, rhi::RenderingAttachment& outDepth, bool& hasDepth
        );

        // --- State ---
        ExecutorConfig config_;
        ExecutionStats stats_;
        core::LinearAllocator frameAllocator_;

        // Physical resource tables (indexed by resource index, not SSA handle)
        std::vector<rhi::TextureHandle> physicalTextures_;
        std::vector<rhi::BufferHandle> physicalBuffers_;
        std::vector<rhi::TextureViewHandle> physicalTextureViews_;

        // Transient resource tracking (for cleanup)
        struct TransientTexture {
            uint16_t resourceIndex;
            rhi::TextureHandle texture;
            rhi::TextureViewHandle view;
        };
        struct TransientBuffer {
            uint16_t resourceIndex;
            rhi::BufferHandle buffer;
        };
        std::vector<TransientTexture> transientTextures_;
        std::vector<TransientBuffer> transientBuffers_;

        // Heap pool for aliased transient resources
        struct HeapAllocation {
            HeapGroupType group;
            rhi::DeviceMemoryHandle heap;
            uint64_t size = 0;
        };
        std::vector<HeapAllocation> heapAllocations_;

        // Per-batch recorded command buffers
        struct BatchRecording {
            RGQueueType queue;
            std::vector<rhi::CommandBufferHandle> commandBuffers;
        };
        std::vector<BatchRecording> batchRecordings_;

        // Optional integrations
        resource::ReadbackRing* readbackRing_ = nullptr;
    };

}  // namespace miki::rg
