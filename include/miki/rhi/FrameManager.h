/** @brief Frame pacing with real per-frame fence synchronization.
 *
 * FrameManager owns the frame-in-flight lifecycle:
 *   - **Windowed mode**: integrates RenderSurface (acquire/present/configure).
 *   - **Offscreen mode**: fence-only, no surface dependency.
 *
 * Users never touch semaphores, fences, or SubmitSyncInfo directly.
 * The render loop reduces to:
 *
 *   auto ctx = frameManager.BeginFrame();
 *   auto cmd = device.CreateCommandBuffer();
 *   // ... record ...
 *   frameManager.EndFrame(*cmd);
 *
 * BeginFrame() blocks until the GPU finishes the oldest in-flight frame,
 * then (in windowed mode) acquires the next swapchain image.
 * EndFrame() submits the command buffer with correct sync primitives
 * and (in windowed mode) presents.
 *
 * Namespace: miki::rhi
 */
#pragma once

#include <cstdint>
#include <memory>

#include "miki/core/Result.h"
#include "miki/rhi/RhiTypes.h"

namespace miki::resource {
    class StagingRing;
    class ReadbackRing;
    class TransferQueue;
}  // namespace miki::resource

namespace miki::rhi {

    class IDevice;
    class ICommandBuffer;
    class RenderSurface;
    struct RenderSurfaceConfig;

    // ===========================================================================
    // FrameContext — returned by BeginFrame(), consumed by EndFrame()
    // ===========================================================================

    /** @brief Opaque per-frame context holding synchronization state.
     *
     * Provides:
     *   - frameIndex: [0, framesInFlight) for descriptor set / transient rotation.
     *   - swapchainImage: current swapchain texture (invalid in offscreen mode).
     *   - width/height: current render target dimensions.
     *
     * This is a value type — cheap to copy, no ownership.
     */
    struct FrameContext {
        uint32_t frameIndex = 0;            ///< Frame-in-flight slot index
        uint64_t frameNumber = 0;           ///< Monotonic frame counter (never wraps in practice)
        TextureHandle swapchainImage = {};  ///< Current swapchain image (invalid if offscreen)
        uint32_t width = 0;                 ///< Current render target width
        uint32_t height = 0;                ///< Current render target height
    };

    // ===========================================================================
    // FrameManager
    // ===========================================================================

    /** @brief Frame pacing manager with real fence-based GPU/CPU synchronization.
     *
     * Single-owner, not thread-safe. Create via static factory; move-only.
     */
    class FrameManager {
       public:
        static constexpr uint32_t kDefaultFramesInFlight = 2;

        ~FrameManager();

        FrameManager(const FrameManager&) = delete;
        auto operator=(const FrameManager&) -> FrameManager& = delete;
        FrameManager(FrameManager&& iOther) noexcept;
        auto operator=(FrameManager&& iOther) noexcept -> FrameManager&;

        /** @brief Create a windowed FrameManager bound to a RenderSurface.
         *  @param iDevice         Device for fence creation.
         *  @param iSurface        RenderSurface for acquire/present (borrowed, must outlive FrameManager).
         *  @param iFramesInFlight Number of frames allowed in flight (default 2).
         */
        [[nodiscard]] static auto Create(
            IDevice& iDevice, RenderSurface& iSurface, uint32_t iFramesInFlight = kDefaultFramesInFlight
        ) -> miki::core::Result<FrameManager>;

        /** @brief Create an offscreen FrameManager (fence-only, no swapchain).
         *  @param iDevice         Device for fence creation.
         *  @param iWidth          Offscreen render target width.
         *  @param iHeight         Offscreen render target height.
         *  @param iFramesInFlight Number of frames allowed in flight (default 2).
         */
        [[nodiscard]] static auto CreateOffscreen(
            IDevice& iDevice, uint32_t iWidth, uint32_t iHeight, uint32_t iFramesInFlight = kDefaultFramesInFlight
        ) -> miki::core::Result<FrameManager>;

        /** @brief Begin a new frame.
         *
         *  Windowed:  waits on in-flight fence → acquires swapchain image.
         *  Offscreen: waits on in-flight fence only.
         *
         *  @return FrameContext with frameIndex, swapchainImage, dimensions.
         *          Returns SwapchainOutOfDate if resize is needed (caller should
         *          call Resize() then retry).
         */
        [[nodiscard]] auto BeginFrame() -> miki::core::Result<FrameContext>;

        /** @brief End the current frame: submit command buffer + present.
         *
         *  Windowed:  submits with semaphore wait/signal + fence, then presents.
         *  Offscreen: submits with fence only.
         *
         *  @param iCmdBuffer Recorded command buffer (must have called End()).
         */
        [[nodiscard]] auto EndFrame(ICommandBuffer& iCmdBuffer) -> miki::core::Result<void>;

        /** @brief Resize the surface (windowed mode only).
         *  Waits for GPU idle, reconfigures surface at new dimensions.
         */
        [[nodiscard]] auto Resize(uint32_t iWidth, uint32_t iHeight) -> miki::core::Result<void>;

        /** @brief Reconfigure the surface (format, present mode, size).
         *  Windowed mode only. Waits for GPU idle first.
         */
        [[nodiscard]] auto Reconfigure(const RenderSurfaceConfig& iConfig) -> miki::core::Result<void>;

        /** @brief Get the current frame-in-flight index. */
        [[nodiscard]] auto FrameIndex() const noexcept -> uint32_t;

        /** @brief Get the monotonic frame counter. */
        [[nodiscard]] auto FrameNumber() const noexcept -> uint64_t;

        /** @brief Get the number of frames in flight. */
        [[nodiscard]] auto FramesInFlight() const noexcept -> uint32_t;

        /** @brief Whether this FrameManager has a RenderSurface (windowed mode). */
        [[nodiscard]] auto IsWindowed() const noexcept -> bool;

        /** @brief Get the bound RenderSurface (nullptr if offscreen). */
        [[nodiscard]] auto GetSurface() const noexcept -> RenderSurface*;

        /** @brief Inject a StagingRing for automatic frame lifecycle management.
         *
         *  When set, BeginFrame() calls ReclaimCompleted() and EndFrame() calls
         *  RecordTransfers() + FlushFrame() automatically. Optional — if not set,
         *  caller manages StagingRing lifecycle manually.
         */
        auto SetStagingRing(miki::resource::StagingRing* iRing) noexcept -> void;

        /** @brief Inject a ReadbackRing for automatic frame lifecycle management.
         *
         *  When set, BeginFrame() calls ReclaimCompleted() and EndFrame() calls
         *  RecordTransfers() + FlushFrame() automatically.
         */
        auto SetReadbackRing(miki::resource::ReadbackRing* iRing) noexcept -> void;

        /** @brief Inject a TransferQueue for async DMA integration.
         *
         *  When set, EndFrame() routes StagingRing buffer copies through
         *  RecordTransfersAsync() and submits the TransferQueue.
         *  ReadbackRing buffer readbacks are also routed through the async queue.
         *  Texture copies remain on the graphics queue.
         */
        auto SetTransferQueue(miki::resource::TransferQueue* iQueue) noexcept -> void;

        /** @brief Wait for all in-flight frames to complete. Call before shutdown. */
        auto WaitAll() -> void;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        explicit FrameManager(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::rhi
