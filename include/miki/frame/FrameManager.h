/** @file FrameManager.h
 *  @brief Unified-timeline frame pacing: BeginFrame / EndFrame.
 *
 *  FrameManager handles CPU/GPU synchronization for a single window (windowed)
 *  or a headless render target (offscreen). It owns per-frame sync primitives
 *  and orchestrates swapchain acquire/present via the bound RenderSurface.
 *
 *  All backends use the same timeline semaphore code path:
 *    T1 (Vulkan 1.4 / D3D12): Native timeline semaphore.
 *    T2 (Vulkan Compat):       Emulated via VK_KHR_timeline_semaphore or fence ring.
 *    T3/T4 (WebGPU / OpenGL):  Emulated via CPU-side counter + backend sync.
 *  This eliminates tier-specific branching in frame management.
 *
 *  See: specs/03-sync.md SS4
 *  Namespace: miki::frame
 */
#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "miki/core/Result.h"
#include "miki/frame/FrameContext.h"
#include "miki/frame/SyncScheduler.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/Swapchain.h"

namespace miki::rhi {
    class RenderSurface;
}

namespace miki::resource {
    class StagingRing;
    class ReadbackRing;
}  // namespace miki::resource

namespace miki::frame {

    class DeferredDestructor;

    // =========================================================================
    // FrameManager
    // =========================================================================

    class FrameManager {
       public:
        static constexpr uint32_t kMaxFramesInFlight = 3;
        static constexpr uint32_t kDefaultFramesInFlight = 2;

        ~FrameManager();

        FrameManager(const FrameManager&) = delete;
        auto operator=(const FrameManager&) -> FrameManager& = delete;
        FrameManager(FrameManager&&) noexcept;
        auto operator=(FrameManager&&) noexcept -> FrameManager&;

        /// @brief Create a windowed FrameManager bound to a RenderSurface.
        [[nodiscard]] static auto Create(
            rhi::DeviceHandle iDevice, rhi::RenderSurface& iSurface, uint32_t iFramesInFlight = kDefaultFramesInFlight
        ) -> core::Result<FrameManager>;

        /// @brief Create an offscreen FrameManager (timeline-only, no swapchain).
        [[nodiscard]] static auto CreateOffscreen(
            rhi::DeviceHandle iDevice, uint32_t iWidth, uint32_t iHeight,
            uint32_t iFramesInFlight = kDefaultFramesInFlight
        ) -> core::Result<FrameManager>;

        // ── Frame lifecycle ─────────────────────────────────────────

        /// @brief Begin a new frame.
        /// CPU waits on timeline semaphore for oldest in-flight frame.
        /// Acquires swapchain image (windowed) or advances offscreen slot.
        [[nodiscard]] auto BeginFrame() -> core::Result<FrameContext>;

        /// @brief A batch of command buffers for submit (specs/03-sync.md §5.3).
        /// Each batch becomes a separate queue submit with its own timeline signal.
        /// The last batch additionally signals renderDone binary sem for present.
        struct SubmitBatch {
            std::span<const rhi::CommandBufferHandle> commandBuffers;
            bool signalPartialTimeline = true;  ///< Allocate + signal a timeline value after this batch
        };

        /// @brief Submit recorded command buffers and present.
        /// Accepts one or more batches; each batch is a separate queue submit.
        /// Single-batch usage is equivalent to the old EndFrame(span<CmdBuf>).
        /// Multi-batch usage enables async compute to start after early batches
        /// (e.g., geometry done) without waiting for the full frame.
        [[nodiscard]] auto EndFrame(std::span<const SubmitBatch> iBatches) -> core::Result<void>;

        // ── Command buffer acquisition (pool-backed, §19) ────────────

        /// @brief Acquire a command buffer from the current frame's pool.
        /// The returned PooledAcquisition contains both the CommandListAcquisition
        /// (bufferHandle + listHandle) and an arenaIndex for optional fine-grained release.
        /// Caller must call Begin()/End() on listHandle, then pass bufferHandle to EndFrame.
        /// Buffers are automatically reclaimed by ResetSlot at the next BeginFrame for this slot.
        [[nodiscard]] auto AcquireCommandList(rhi::QueueType iQueue, uint32_t iThreadIndex = 0)
            -> core::Result<rhi::CommandListAcquisition>;

        // ── Async compute integration ───────────────────────────────

        /// @brief Get a sync point for async compute to wait on.
        /// Returns {timeline semaphore, value} — compute queue waits before reading graphics outputs (e.g., depth
        /// buffer after DepthPrePass).
        [[nodiscard]] auto GetGraphicsSyncPoint() const noexcept -> TimelineSyncPoint;

        /// @brief Accumulate a compute sync dependency for this frame (additive).
        /// Multiple calls add multiple waits. EndFrame consumes all accumulated waits
        /// on the last batch, then auto-clears. Use for frames with multiple compute dependencies
        /// (e.g., GTAO + GDeflate decode). See specs/03-sync.md §4.4.4.
        auto AddComputeSyncPoint(TimelineSyncPoint iPoint, rhi::PipelineStage iStage) noexcept -> void;

        /// @brief Clear accumulated compute sync points. Called automatically by EndFrame.
        auto ClearComputeSyncPoints() noexcept -> void;

        // ── Transfer queue integration ──────────────────────────────

        /// @brief Eagerly dispatch pending StagingRing/ReadbackRing copies NOW.
        /// Call after CPU-side memcpy is done but BEFORE EndFrame — the transfer queue runs in parallel with command
        /// buffer recording (~2ms overlap). If not called, EndFrame will dispatch transfers itself (zero overlap). Safe
        /// to call multiple times per frame (no-op if already flushed). T1 + hasAsyncTransfer: submits to dedicated
        /// transfer queue. Fallback: defers to EndFrame (cannot eagerly dispatch on graphics queue because the user's
        /// cmd buffers aren't recorded yet).
        auto FlushTransfers() -> void;

        /// @brief Submit any pending staging/readback copies immediately.
        /// For use OUTSIDE the BeginFrame/EndFrame cycle (e.g., bulk asset loading, headless batch).
        /// Internally: FlushFrame -> Acquire cmd -> RecordTransfers -> Submit -> advance timeline.
        /// Does NOT rotate frame slots or affect frame pacing.
        /// No-op if no pending copies exist.
        /// Safe to call between frames or before the first BeginFrame.
        /// @return Number of copy commands submitted, or 0 if nothing to drain.
        [[nodiscard]] auto DrainPendingTransfers() -> uint32_t;

        /// @brief Register a transfer completion for this frame.
        /// Graphics queue will wait at the appropriate stage.
        auto SetTransferSyncPoint(TimelineSyncPoint iPoint) noexcept -> void;

        // ── SyncScheduler integration (specs/03-sync.md §4.4) ───────

        /// @brief Bind a SyncScheduler for global timeline value allocation.
        /// When bound, EndFrame delegates AllocateSignal/CommitSubmit to the scheduler.
        /// When null (default), local ++currentTimelineValue fallback is used.
        /// Must be called before the first BeginFrame. Typically set by FrameOrchestrator.
        auto SetSyncScheduler(SyncScheduler* iScheduler) noexcept -> void;

        /// @brief Get the partial timeline value from the first signaled batch of the last EndFrame.
        /// Returns 0 if the last frame used a single batch (no partial signal).
        /// Use case: async compute waits on geometry-done (batch #1), not frame-done.
        [[nodiscard]] auto GetLastPartialTimelineValue() const noexcept -> uint64_t;

        // ── Resource lifecycle hooks ────────────────────────────────

        auto SetStagingRing(resource::StagingRing* iRing) noexcept -> void;
        auto SetReadbackRing(resource::ReadbackRing* iRing) noexcept -> void;
        auto SetDeferredDestructor(DeferredDestructor* iDestructor) noexcept -> void;

        // ── Resize / reconfigure ────────────────────────────────────

        [[nodiscard]] auto Resize(uint32_t iWidth, uint32_t iHeight) -> core::Result<void>;
        [[nodiscard]] auto Reconfigure(const rhi::RenderSurfaceConfig& iConfig) -> core::Result<void>;

        // ── Queries ─────────────────────────────────────────────────

        [[nodiscard]] auto FrameIndex() const noexcept -> uint32_t;
        [[nodiscard]] auto FrameNumber() const noexcept -> uint64_t;
        [[nodiscard]] auto FramesInFlight() const noexcept -> uint32_t;
        [[nodiscard]] auto IsWindowed() const noexcept -> bool;
        [[nodiscard]] auto GetSurface() const noexcept -> rhi::RenderSurface*;
        [[nodiscard]] auto CurrentTimelineValue() const noexcept -> uint64_t;
        [[nodiscard]] auto IsFrameComplete(uint64_t iFrameNumber) const noexcept -> bool;

        /// @brief Wait for ALL in-flight frames to complete.
        /// Per-surface timeline wait (NOT device->WaitIdle).
        auto WaitAll() -> void;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        explicit FrameManager(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::frame
