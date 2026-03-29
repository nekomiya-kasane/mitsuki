/** @file SurfaceManager.h
 *  @brief GPU surface lifecycle manager for multi-window rendering.
 *
 *  SurfaceManager owns per-window RenderSurface + FrameManager instances.
 *  It is SEPARATE from WindowManager — the user explicitly binds a GPU
 *  surface to an OS window via AttachSurface/DetachSurface.
 *
 *  Key design:
 *    - Single DeviceHandle shared across all windows.
 *    - Per-surface timeline wait on detach (no global WaitIdle).
 *    - Dynamic present mode / color space changes via swapchain recreation.
 *    - Thread safety: main thread only (all OS/swapchain ops are inherently single-threaded).
 *
 *  See: specs/01-window-manager.md SS5, specs/02-rhi-design.md SS11.2
 *  Namespace: miki::rhi
 */
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "miki/core/Result.h"
#include "miki/frame/FrameContext.h"
#include "miki/frame/FrameManager.h"
#include "miki/platform/WindowHandle.h"
#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/RenderSurface.h"
#include "miki/rhi/Swapchain.h"

namespace miki::rhi {

    // =========================================================================
    // SurfaceManager — per-window GPU surface lifecycle
    // =========================================================================

    /** @brief Manages per-window RenderSurface + FrameManager.
     *
     *  Bound to a single DeviceHandle at creation. All windows share this device.
     *  Attach/Detach are explicit — SurfaceManager never creates OS windows.
     *
     *  Destruction protocol (cascade):
     *    1. WindowManager::GetDescendantsPostOrder(parent) + self
     *    2. SurfaceManager::DetachSurfaces(postOrderList)  — per-surface timeline wait
     *    3. WindowManager::DestroyWindow(parent)           — OS windows destroyed
     *
     *  See: specs/01-window-manager.md SS6 for the full cascade protocol.
     */
    class SurfaceManager {
       public:
        ~SurfaceManager();

        SurfaceManager(const SurfaceManager&) = delete;
        auto operator=(const SurfaceManager&) -> SurfaceManager& = delete;
        SurfaceManager(SurfaceManager&&) noexcept;
        auto operator=(SurfaceManager&&) noexcept -> SurfaceManager&;

        /// @brief Create a SurfaceManager bound to a shared device.
        /// @param iDevice Device handle (must outlive this SurfaceManager).
        /// @return SurfaceManager or error if device is invalid.
        [[nodiscard]] static auto Create(DeviceHandle iDevice) -> core::Result<SurfaceManager>;

        // ── Surface lifecycle ───────────────────────────────────────

        /// @brief Attach a RenderSurface + FrameManager to a window. Creates swapchain + sync objects using the shared
        /// DeviceHandle. The window must not already have an attached surface (no double-attach).
        /// @param iWindow       Platform window handle (POD, no WindowManager dependency).
        /// @param iNativeWindow Native window handle for surface creation.
        /// @param iConfig       Surface configuration (present mode, color space, format, VRR, image count).
        [[nodiscard]] auto AttachSurface(
            platform::WindowHandle iWindow, NativeWindowHandle iNativeWindow, const RenderSurfaceConfig& iConfig = {}
        ) -> core::Result<void>;

        /// @brief Detach and destroy the surface for a window. Waits for THIS surface's in-flight frames only
        /// (per-surface timeline wait). Other windows continue rendering uninterrupted.
        [[nodiscard]] auto DetachSurface(platform::WindowHandle iWindow) -> core::Result<void>;

        /// @brief Detach surfaces for multiple windows (batch, post-order safe).
        /// Each surface is waited on individually via FrameManager::WaitAll() (per-surface timeline wait, no
        /// device-wide WaitIdle). Windows without attached surfaces are silently skipped. Empty span is a no-op (not an
        /// error).
        [[nodiscard]] auto DetachSurfaces(std::span<const platform::WindowHandle> iWindows) -> core::Result<void>;

        /// @brief Check if a window has an attached surface.
        [[nodiscard]] auto HasSurface(platform::WindowHandle iWindow) const -> bool;

        // ── Per-window access ───────────────────────────────────────

        /// @brief Get the RenderSurface for a window (nullptr if not attached).
        [[nodiscard]] auto GetRenderSurface(platform::WindowHandle iWindow) -> RenderSurface*;

        /// @brief Get the FrameManager for a window (nullptr if not attached).
        [[nodiscard]] auto GetFrameManager(platform::WindowHandle iWindow) -> frame::FrameManager*;

        // ── Frame operations ────────────────────────────────────────

        /// @brief Begin frame for a specific window's surface.
        /// Waits on in-flight fence, acquires swapchain image.
        /// Returns error if window has no surface or is minimized.
        [[nodiscard]] auto BeginFrame(platform::WindowHandle iWindow) -> core::Result<frame::FrameContext>;

        /// @brief End frame for a specific window's surface.
        /// Submits command buffer with correct sync primitives, then presents.
        /// @return void on success, SwapchainOutOfDate if resize needed.
        [[nodiscard]] auto EndFrame(platform::WindowHandle iWindow, CommandBufferHandle iCmd) -> core::Result<void>;

        /// @brief Resize a window's surface (typically after WindowEvent::Resize).
        /// Recreates the swapchain at new dimensions. Waits for this surface's
        /// in-flight frames before recreation.
        [[nodiscard]] auto ResizeSurface(platform::WindowHandle iWindow, uint32_t iWidth, uint32_t iHeight)
            -> core::Result<void>;

        // ── Dynamic present configuration ───────────────────────────

        /// @brief Change present mode at runtime (triggers swapchain recreation).
        /// Waits for this surface's in-flight frames before recreating.
        /// Returns error if the mode is not in GetSupportedPresentModes().
        [[nodiscard]] auto SetPresentMode(platform::WindowHandle iWindow, PresentMode iMode) -> core::Result<void>;

        /// @brief Change color space at runtime (triggers swapchain recreation).
        /// May also change the swapchain format (e.g. scRGBLinear -> R16G16B16A16_SFLOAT).
        /// Returns error if the display/backend does not support the requested color space.
        [[nodiscard]] auto SetColorSpace(platform::WindowHandle iWindow, SurfaceColorSpace iSpace)
            -> core::Result<void>;

        /// @brief Query supported present modes for a window's display.
        /// Always contains at least PresentMode::Fifo (universally required).
        [[nodiscard]] auto GetSupportedPresentModes(platform::WindowHandle iWindow) const -> std::vector<PresentMode>;

        /// @brief Query supported color spaces for a window's display.
        /// Always contains at least SurfaceColorSpace::SRGB.
        [[nodiscard]] auto GetSupportedColorSpaces(platform::WindowHandle iWindow) const
            -> std::vector<SurfaceColorSpace>;

        // ── Bulk operations ─────────────────────────────────────────

        /// @brief Wait for all surfaces' GPU work to complete. Iterates all attached surfaces and calls
        /// FrameManager::WaitAll() on each. Used before shutdown or backend switching (spec 01 SS12.2).
        auto WaitAll() -> void;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        explicit SurfaceManager(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::rhi
