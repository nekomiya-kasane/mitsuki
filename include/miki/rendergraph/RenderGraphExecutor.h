/** @file RenderGraphExecutor.h
 *  @brief Runtime execution engine for compiled render graphs.
 *
 *  RenderGraphExecutor is a thin orchestrator that delegates to three composable phases:
 *    Phase 1: TransientResourceAllocator — heap pool + placed resource creation
 *    Phase 2: PassRecorder — barrier emission + pass lambda invocation
 *    Phase 3: BatchSubmitter (free function) — queue submission with timeline sync
 *
 *  Backend-agnostic: all GPU operations go through DeviceHandle::Dispatch and
 *  CommandListHandle::Dispatch (CRTP, zero per-draw overhead).
 *
 *  Thread model:
 *    - Single-threaded (Phase 2): all recording on caller thread
 *    - Parallel recording (Phase 2, E-5): per-thread secondary cmd bufs, barriers in primary
 *    - Async execution (E-7): offload phases 1+2 to worker threads
 *
 *  See: specs/04-render-graph.md §6
 *  Namespace: miki::rg
 */
#pragma once

#include <cstdint>
#include <future>

#include "miki/core/LinearAllocator.h"
#include "miki/core/Result.h"
#include "miki/frame/CommandPoolAllocator.h"
#include "miki/frame/FrameContext.h"
#include "miki/frame/SyncScheduler.h"
#include "miki/rendergraph/BatchSubmitter.h"
#include "miki/rendergraph/PassRecorder.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rendergraph/TransientResourceAllocator.h"
#include "miki/rhi/Device.h"

namespace miki::resource {
    class ReadbackRing;
}

namespace miki::rg {

    class RenderGraphBuilder;

    // =========================================================================
    // Executor configuration
    // =========================================================================

    struct ExecutorConfig {
        uint32_t maxRecordingThreads = 1;              ///< 1 = single-threaded, >1 = parallel (T1 only)
        bool enableParallelRecording = false;          ///< Use secondary cmd bufs for parallel recording
        bool enableAsyncExecution = false;             ///< Offload allocation + recording to worker threads
        bool enableDebugLabels = true;                 ///< Emit per-pass debug labels for PIX/RenderDoc/NSight
        uint64_t frameAllocatorCapacity = 256 * 1024;  ///< Per-frame arena capacity (bytes)
        bool enableHeapPooling = true;                 ///< Cross-frame heap reuse (§5.6.5)
        bool enableBufferSuballocation = true;         ///< Transient buffer suballocation (§5.6.7)
        TransientHeapPoolConfig heapPoolConfig;        ///< Heap pool tuning parameters
    };

    // =========================================================================
    // Execution statistics (profiling) — aggregated from sub-components
    // =========================================================================

    struct ExecutionStats {
        AllocationStats allocation;
        RecordingStats recording;
        SubmissionStats submission;
        uint32_t defragTriggered = 0;
    };

    // =========================================================================
    // RenderGraphExecutor — thin orchestrator
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
        [[nodiscard]] auto Execute(
            const CompiledRenderGraph& graph, RenderGraphBuilder& builder, const frame::FrameContext& frame,
            rhi::DeviceHandle device, frame::SyncScheduler& scheduler, frame::CommandPoolAllocator& poolAllocator
        ) -> core::Result<void>;

        /// @brief Get execution statistics from the last Execute() call.
        [[nodiscard]] auto GetStats() const noexcept -> const ExecutionStats& { return stats_; }

        /// @brief Set optional ReadbackRing for GPU->CPU readback integration (E-10).
        void SetReadbackRing(resource::ReadbackRing* ring) noexcept { readbackRing_ = ring; }

        /// @brief Get the transient heap pool for inspection/defrag control.
        [[nodiscard]] auto GetHeapPool() noexcept -> TransientHeapPool& { return allocator_.GetHeapPool(); }
        [[nodiscard]] auto GetHeapPool() const noexcept -> const TransientHeapPool& { return allocator_.GetHeapPool(); }

        /// @brief Trigger heap pool defragmentation (§5.6.8).
        void DefragmentHeapPool(
            const CompiledRenderGraph& graph, rhi::DeviceHandle device, uint32_t frameNumber, float vramUsageRatio,
            bool force = false
        ) {
            allocator_.DefragmentHeapPool(graph, device, frameNumber, vramUsageRatio, force);
        }

        /// @brief Release all pooled resources. Call at shutdown.
        void ReleasePooledResources(rhi::DeviceHandle device) { allocator_.ReleasePooledResources(device); }

        /// @brief Async execution (E-7): offload Phase 1 + 2 to a worker thread.
        [[nodiscard]] auto ExecuteAsync(
            const CompiledRenderGraph& graph, RenderGraphBuilder& builder, const frame::FrameContext& frame,
            rhi::DeviceHandle device, frame::CommandPoolAllocator& poolAllocator
        ) -> std::future<core::Result<void>>;

        /// @brief Submit batches after async Phase 1+2 completes.
        [[nodiscard]] auto SubmitAfterAsync(
            const CompiledRenderGraph& graph, rhi::DeviceHandle device, frame::SyncScheduler& scheduler
        ) -> core::Result<void>;

        /// @brief Access sub-components for advanced use.
        [[nodiscard]] auto GetAllocator() noexcept -> TransientResourceAllocator& { return allocator_; }
        [[nodiscard]] auto GetRecorder() noexcept -> PassRecorder& { return recorder_; }

       private:
        // --- Sub-components ---
        TransientResourceAllocator allocator_;
        PassRecorder recorder_;
        core::LinearAllocator frameAllocator_;

        // --- State ---
        ExecutionStats stats_;
        uint32_t frameNumber_ = 0;

        // Optional integrations
        resource::ReadbackRing* readbackRing_ = nullptr;
    };

}  // namespace miki::rg
