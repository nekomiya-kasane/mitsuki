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
    // TimelineSyncPoint — cross-queue sync token
    // =========================================================================

    struct TimelineSyncPoint {
        rhi::SemaphoreHandle semaphore;
        uint64_t value = 0;
    };

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

        /// @brief Submit recorded command buffers and present.
        /// Multi-submit: accepts multiple command buffers for multi-threaded recording.
        /// Pass the bufferHandle from CommandListAcquisition (NOT the listHandle).
        [[nodiscard]] auto EndFrame(std::span<const rhi::CommandBufferHandle> iCmdBuffers) -> core::Result<void>;

        /// @brief Single command buffer convenience overload.
        [[nodiscard]] auto EndFrame(rhi::CommandBufferHandle iCmd) -> core::Result<void>;

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
