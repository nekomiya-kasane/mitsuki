/** @file AsyncTaskManager.h
 *  @brief Long-running cross-frame async compute task management.
 *
 *  Manages compute tasks that span multiple frames (BLAS rebuild, GDeflate decode,
 *  GPU QEM simplification). Tasks are submitted independently of BeginFrame/EndFrame
 *  lifecycle. Graphics queue polls or waits on task completion at its own pace.
 *
 *  Timeline values are allocated via SyncScheduler::AllocateSignal(Compute) to
 *  prevent conflicts with frame-sync compute work (GTAO, Material Resolve).
 *
 *  See: specs/03-sync.md §5.6
 *  Namespace: miki::frame
 */
#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "miki/core/Result.h"
#include "miki/frame/SyncScheduler.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/Sync.h"

namespace miki::frame {

    /// @brief Handle to a long-running async compute task.
    struct AsyncTaskHandle {
        uint64_t id = 0;
        [[nodiscard]] constexpr auto IsValid() const noexcept -> bool { return id != 0; }
    };

    /// @brief Sync point for cross-queue dependency: {semaphore, value}.
    struct TimelineSyncPoint {
        rhi::SemaphoreHandle semaphore;
        uint64_t value = 0;
    };

    // =========================================================================
    // AsyncTaskManager
    // =========================================================================

    class AsyncTaskManager {
       public:
        ~AsyncTaskManager();

        AsyncTaskManager(const AsyncTaskManager&) = delete;
        auto operator=(const AsyncTaskManager&) -> AsyncTaskManager& = delete;
        AsyncTaskManager(AsyncTaskManager&&) noexcept;
        auto operator=(AsyncTaskManager&&) noexcept -> AsyncTaskManager&;

        /// @brief Create an AsyncTaskManager bound to a device and SyncScheduler.
        [[nodiscard]] static auto Create(rhi::DeviceHandle iDevice, SyncScheduler& iScheduler)
            -> core::Result<AsyncTaskManager>;

        /// @brief Submit a long-running compute task.
        /// Command buffer is submitted to the async compute queue with optional waits.
        /// @param iCmd       Recorded command buffer for the compute queue.
        /// @param iWaits     Optional cross-queue waits (e.g., wait for transfer upload).
        /// @return Handle for polling completion.
        [[nodiscard]] auto Submit(rhi::CommandBufferHandle iCmd, std::span<const rhi::SemaphoreSubmitInfo> iWaits = {})
            -> core::Result<AsyncTaskHandle>;

        /// @brief Non-blocking poll: is this task done on the GPU?
        [[nodiscard]] auto IsComplete(AsyncTaskHandle iTask) const noexcept -> bool;

        /// @brief Get the timeline sync point signaled when task completes.
        /// Graphics queue can wait on this to consume task results.
        [[nodiscard]] auto GetCompletionPoint(AsyncTaskHandle iTask) const noexcept -> TimelineSyncPoint;

        /// @brief Blocking CPU wait for a task to complete.
        /// Use sparingly — only for shutdown or mandatory sync.
        auto WaitForCompletion(AsyncTaskHandle iTask, uint64_t iTimeoutNs = UINT64_MAX) -> core::Result<void>;

        /// @brief Submit a long-running compute task split into sub-batches.
        /// Each sub-batch gets its own timeline signal. Ensures no single dispatch
        /// blocks frame-sync compute work (GTAO) for more than ~2ms.
        /// @param iBatches  Pre-split command buffers (each <= 2ms GPU time).
        /// @param iWaits    Optional cross-queue waits (applied to first batch only).
        /// @return Handle for polling completion of the LAST batch.
        [[nodiscard]] auto SubmitBatched(
            std::span<const rhi::CommandBufferHandle> iBatches, std::span<const rhi::SemaphoreSubmitInfo> iWaits = {}
        ) -> core::Result<AsyncTaskHandle>;

        /// @brief Get the number of currently in-flight tasks.
        [[nodiscard]] auto ActiveTaskCount() const noexcept -> uint32_t;

        /// @brief Cancel all pending tasks and wait for GPU idle on compute queue.
        auto Shutdown() -> void;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        explicit AsyncTaskManager(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::frame
