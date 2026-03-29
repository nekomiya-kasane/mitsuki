/** @file RenderSurface.h
 *  @brief Per-window swapchain wrapper with WebGPU-style configure/unconfigure pattern.
 *
 *  RenderSurface wraps the low-level swapchain API (DeviceBase::CreateSwapchain)
 *  and provides a higher-level interface for SurfaceManager and FrameManager.
 *
 *  Lifecycle:
 *    1. Create(device, nativeWindow)           -- creates VkSurfaceKHR / DXGI factory output
 *    2. Configure(RenderSurfaceConfig)         -- creates swapchain with resolved params
 *    3. AcquireNextImage / Present (per-frame) -- used by FrameManager
 *    4. Reconfigure(newConfig) or Resize(w,h)  -- recreates swapchain
 *    5. ~RenderSurface                         -- destroys swapchain + surface
 *
 *  See: specs/01-window-manager.md SS5, specs/03-sync.md SS4
 *  Namespace: miki::rhi
 */
#pragma once

#include <cstdint>
#include <memory>

#include "miki/core/Result.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/Swapchain.h"

namespace miki::rhi {

    // RenderSurfaceCapabilities is defined in Swapchain.h (available via Device.h include chain)

    // =========================================================================
    // SubmitSyncInfo — per-frame sync primitives for FrameManager
    // =========================================================================

    /// Returned by RenderSurface for FrameManager to wire into SubmitDesc.
    /// T1: binary semaphores for swapchain acquire/present.
    /// T3/T4: empty (implicit sync).
    struct SubmitSyncInfo {
        SemaphoreHandle imageAvailable;  ///< Wait before rendering (from AcquireNextImage)
        SemaphoreHandle renderFinished;  ///< Signal after rendering (before Present)
        FenceHandle inFlightFence;       ///< T2 only: CPU wait fence (T1 uses timeline instead)
    };

    // =========================================================================
    // RenderSurface — per-window swapchain wrapper
    // =========================================================================

    class RenderSurface {
       public:
        ~RenderSurface();

        RenderSurface(const RenderSurface&) = delete;
        auto operator=(const RenderSurface&) -> RenderSurface& = delete;
        RenderSurface(RenderSurface&&) noexcept;
        auto operator=(RenderSurface&&) noexcept -> RenderSurface&;

        /// @brief Create a RenderSurface from a native window handle.
        /// Creates the platform surface (VkSurfaceKHR / DXGI output) but NOT the swapchain.
        /// Call Configure() after creation to create the swapchain.
        [[nodiscard]] static auto Create(DeviceHandle iDevice, NativeWindowHandle iNativeWindow)
            -> core::Result<std::unique_ptr<RenderSurface>>;

        // ── Configuration ──────────────────────────────────────────

        /// @brief Create or recreate the swapchain with the given config.
        /// Resolves RenderSurfaceConfig (intent) → SwapchainDesc (precise params).
        /// Caller must ensure no in-flight frames reference the old swapchain.
        [[nodiscard]] auto Configure(const RenderSurfaceConfig& iConfig, uint32_t iWidth, uint32_t iHeight)
            -> core::Result<void>;

        /// @brief Resize the swapchain without changing other config.
        /// Caller must ensure no in-flight frames reference the old swapchain.
        [[nodiscard]] auto Resize(uint32_t iWidth, uint32_t iHeight) -> core::Result<void>;

        /// @brief Reconfigure with new settings (triggers swapchain recreation).
        [[nodiscard]] auto Reconfigure(const RenderSurfaceConfig& iConfig) -> core::Result<void>;

        // ── Per-frame operations ───────────────────────────────────

        /// @brief Acquire the next swapchain image.
        /// Signals imageAvailable semaphore (binary, for swapchain sync).
        [[nodiscard]] auto AcquireNextImage() -> core::Result<uint32_t>;

        /// @brief Present the current swapchain image.
        /// Waits on renderFinished semaphore before presentation.
        [[nodiscard]] auto Present() -> core::Result<void>;

        // ── Queries ────────────────────────────────────────────────

        [[nodiscard]] auto GetConfig() const noexcept -> const RenderSurfaceConfig&;
        [[nodiscard]] auto GetFormat() const noexcept -> Format;
        [[nodiscard]] auto GetExtent() const noexcept -> Extent2D;
        [[nodiscard]] auto GetCurrentImageIndex() const noexcept -> uint32_t;
        [[nodiscard]] auto GetCurrentTexture() const noexcept -> TextureHandle;
        [[nodiscard]] auto GetSwapchainHandle() const noexcept -> SwapchainHandle;
        [[nodiscard]] auto GetSubmitSyncInfo() const noexcept -> const SubmitSyncInfo&;
        [[nodiscard]] auto GetCapabilities() const -> RenderSurfaceCapabilities;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        explicit RenderSurface(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::rhi
