/** @file SyncScheduler.h
 *  @brief Cross-queue timeline semaphore dependency resolution.
 *
 *  SyncScheduler manages the monotonic timeline counters across Graphics, Compute,
 *  and Transfer queues. RenderGraph and FrameManager use it to express inter-queue
 *  dependencies without manually tracking timeline values.
 *
 *  All timeline values are globally allocated — no per-window encoding.
 *  Multi-window safe: Window A and Window B share the same SyncScheduler instance.
 *
 *  See: specs/03-sync.md §13
 *  Namespace: miki::frame
 */
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/Sync.h"

namespace miki::frame {

    /// @brief Cross-queue sync token: {semaphore, value}.
    /// Canonical definition — used by FrameManager, AsyncTaskManager, RenderGraph.
    struct TimelineSyncPoint {
        rhi::SemaphoreHandle semaphore;
        uint64_t value = 0;
    };

    /// @brief Cross-queue sync point: {semaphore, value, waitStage}.
    struct SyncWaitEntry {
        rhi::SemaphoreHandle semaphore;
        uint64_t value = 0;
        rhi::PipelineStage stageMask = rhi::PipelineStage::AllCommands;
    };

    // =========================================================================
    // SyncScheduler
    // =========================================================================

    class SyncScheduler {
       public:
        SyncScheduler() = default;
        ~SyncScheduler() = default;

        SyncScheduler(const SyncScheduler&) = delete;
        auto operator=(const SyncScheduler&) -> SyncScheduler& = delete;
        SyncScheduler(SyncScheduler&&) noexcept = default;
        auto operator=(SyncScheduler&&) noexcept -> SyncScheduler& = default;

        /// @brief Initialize with device-global timeline semaphores.
        /// Call once after device creation.
        void Init(const rhi::QueueTimelines& timelines);

        /// @brief Allocate the next timeline value for a queue.
        /// Returns the value that should be signaled in the next submit to this queue.
        /// Thread-safety: single-threaded (main thread only).
        [[nodiscard]] auto AllocateSignal(rhi::QueueType queue) -> uint64_t;

        /// @brief Record a dependency: waitQueue must wait for signalQueue's timeline
        /// to reach signalValue before executing work at waitStage.
        void AddDependency(
            rhi::QueueType waitQueue, rhi::QueueType signalQueue, uint64_t signalValue, rhi::PipelineStage waitStage
        );

        /// @brief Get all pending waits for a queue's next submit.
        /// Returns the accumulated wait entries since the last CommitSubmit for this queue.
        [[nodiscard]] auto GetPendingWaits(rhi::QueueType queue) const -> std::span<const SyncWaitEntry>;

        /// @brief Get the signal value for a queue's next submit.
        /// This is the value most recently returned by AllocateSignal for this queue.
        [[nodiscard]] auto GetSignalValue(rhi::QueueType queue) const -> uint64_t;

        /// @brief Get the timeline semaphore handle for a queue.
        [[nodiscard]] auto GetSemaphore(rhi::QueueType queue) const -> rhi::SemaphoreHandle;

        /// @brief Get the current counter value (last committed) for a queue.
        [[nodiscard]] auto GetCurrentValue(rhi::QueueType queue) const -> uint64_t;

        /// @brief Peek at the value that the next AllocateSignal will return.
        /// Used by FrameManager::BeginFrame to compute graphicsTimelineTarget.
        [[nodiscard]] auto PeekNextSignal(rhi::QueueType queue) const -> uint64_t;

        /// @brief Clear pending waits after a submit has been executed.
        /// Call after each vkQueueSubmit2 / ExecuteCommandLists.
        void CommitSubmit(rhi::QueueType queue);

        /// @brief Reset all state (for shutdown / device recreation).
        void Reset();

        // ── Wait-Graph Diagnostic (specs/03-sync.md §12.4) ──────────

        /// @brief Dump current wait-graph state to a FILE stream (human-readable).
        void DumpWaitGraph(FILE* iOut) const;

        /// @brief Export wait-graph as DOT/GraphViz format string.
        void ExportWaitGraphDOT(std::string& oOut) const;

        /// @brief Export wait-graph as JSON (machine-readable, CI integration).
        void ExportWaitGraphJSON(std::string& oOut) const;

        /// @brief Detect deadlock cycles in the current wait-graph.
        /// Returns true if a cycle is detected (DFS on queue adjacency, O(V+E) where V=3).
        [[nodiscard]] auto DetectDeadlock() const -> bool;

       private:
        static constexpr uint32_t kQueueCount = 3;

        static auto QueueIndex(rhi::QueueType q) -> uint32_t {
            switch (q) {
                case rhi::QueueType::Graphics: return 0;
                case rhi::QueueType::Compute: return 1;
                case rhi::QueueType::Transfer: return 2;
            }
            return 0;
        }

        struct QueueState {
            rhi::SemaphoreHandle semaphore;
            uint64_t nextValue = 1;     // Next value to allocate
            uint64_t currentValue = 0;  // Last committed (signaled) value
            std::vector<SyncWaitEntry> pendingWaits;
        };

        std::array<QueueState, kQueueCount> queues_{};
    };

}  // namespace miki::frame
