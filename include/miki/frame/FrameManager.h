/** @file FrameManager.h
 *  @brief Timeline-first frame pacing: BeginFrame / EndFrame.
 *
 *  FrameManager handles CPU/GPU synchronization for a single window (windowed)
 *  or a headless render target (offscreen). It owns per-frame sync primitives
 *  and orchestrates swapchain acquire/present via the bound RenderSurface.
 *
 *  Tier-adaptive:
 *    T1 (Vulkan 1.4 / D3D12): Timeline semaphore — single object per queue.
 *    T2 (Vulkan Compat):       Binary semaphore + VkFence per frame slot.
 *    T3/T4 (WebGPU / OpenGL):  Implicit sync (blocking present).
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
        /// T1: CPU waits on timeline semaphore for oldest in-flight frame.
        /// T2: CPU waits on VkFence. T3/T4: Implicit sync.
        /// Acquires swapchain image (windowed) or advances offscreen slot.
        [[nodiscard]] auto BeginFrame() -> core::Result<FrameContext>;

        /// @brief Submit recorded command buffers and present (single submit).
        /// Pass the bufferHandle from CommandListAcquisition (NOT the listHandle).
        [[nodiscard]] auto EndFrame(std::span<const rhi::CommandBufferHandle> iCmdBuffers) -> core::Result<void>;

        /// @brief Single command buffer convenience overload.
        [[nodiscard]] auto EndFrame(rhi::CommandBufferHandle iCmd) -> core::Result<void>;

        /// @brief A batch of command buffers for split-submit (specs/03-sync.md §5.3).
        /// Each batch becomes a separate vkQueueSubmit2 with its own timeline signal.
        /// The last batch additionally signals renderDone binary sem for present.
        struct SubmitBatch {
            std::span<const rhi::CommandBufferHandle> commandBuffers;
            bool signalPartialTimeline = true;  ///< Allocate + signal a timeline value after this batch
        };

        /// @brief Split-submit EndFrame: multiple graphics queue submits per frame.
        /// Enables async compute to start after early batches (e.g., geometry done)
        /// without waiting for the full frame. Last batch handles present sync.
        [[nodiscard]] auto EndFrameSplit(std::span<const SubmitBatch> iBatches) -> core::Result<void>;

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
        /// Returns {timeline semaphore, value} — compute queue waits before
        /// reading graphics outputs (e.g., depth buffer after DepthPrePass).
        [[nodiscard]] auto GetGraphicsSyncPoint() const noexcept -> TimelineSyncPoint;

        /// @brief Register an async compute completion for this frame.
        /// Graphics queue will wait on this before passes that depend on
        /// compute results (e.g., Deferred Resolve waits for GTAO + Material Resolve).
        auto SetComputeSyncPoint(TimelineSyncPoint iPoint) noexcept -> void;

        // ── Transfer queue integration ──────────────────────────────

        /// @brief Eagerly dispatch pending StagingRing/ReadbackRing copies NOW.
        /// Call after CPU-side memcpy is done but BEFORE EndFrame — the transfer
        /// queue runs in parallel with command buffer recording (~2ms overlap).
        /// If not called, EndFrame will dispatch transfers itself (zero overlap).
        /// Safe to call multiple times per frame (no-op if already flushed).
        /// T1 + hasAsyncTransfer: submits to dedicated transfer queue.
        /// Fallback: defers to EndFrame (cannot eagerly dispatch on graphics queue
        /// because the user's cmd buffers aren't recorded yet).
        auto FlushTransfers() -> void;

        /// @brief Register a transfer completion for this frame.
        /// Graphics queue will wait at the appropriate stage.
        auto SetTransferSyncPoint(TimelineSyncPoint iPoint) noexcept -> void;

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
