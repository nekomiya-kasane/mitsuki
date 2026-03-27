/** @brief RenderSurface — production-grade windowed present abstraction.
 *
 * Replaces the temporary ISwapchain (Phase 2 stopgap) with a Pimpl'd class
 * supporting multi-window rendering with shared IDevice.
 *
 * Design references:
 *   - Filament SwapChain: per-window, factory-created, Renderer binds to one
 *   - wgpu Surface: configure() decouples creation from config
 *   - Diligent NativeWindow: typed platform handle, not void*
 *
 * Thread safety: NOT thread-safe. All calls must be on the render thread.
 * Phase 13 (Coca coroutines) may add thread-safe wrappers.
 *
 * ABI: Pimpl'd for forward-compatible ABI (Phase 15a SDK).
 *      Internal Impl uses virtual dispatch for backend polymorphism.
 *
 * Namespace: miki::rhi
 */
#pragma once

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#include "miki/core/Result.h"
#include "miki/rhi/Format.h"
#include "miki/rhi/RhiDescriptors.h"
#include "miki/rhi/RhiTypes.h"

namespace miki::rhi {

    class IDevice;

    // ===========================================================================
    // PresentMode
    // ===========================================================================

    /** @brief Presentation mode for a RenderSurface.
     *
     * Maps to:
     *   Vulkan: VK_PRESENT_MODE_{FIFO,MAILBOX,IMMEDIATE}_KHR
     *   D3D12:  SyncInterval 1 / 0+ALLOW_TEARING
     *   WebGPU: wgpu::PresentMode::{Fifo,Mailbox,Immediate}
     *   GL:     glfwSwapInterval(1) / glfwSwapInterval(0)
     */
    enum class PresentMode : uint8_t {
        Fifo = 0,       ///< VSync on. Guaranteed available on all platforms.
        Mailbox = 1,    ///< Triple-buffered low-latency. May fall back to Fifo.
        Immediate = 2,  ///< VSync off. Tearing possible. May fall back to Fifo.
    };

    // ===========================================================================
    // NativeWindowHandle — typed platform window handle (std::variant)
    // ===========================================================================

    /** @brief Win32 native window handle (HWND). */
    struct Win32Window {
        void* hwnd = nullptr;
    };

    /** @brief X11/Xlib native window handle. */
    struct XlibWindow {
        void* display = nullptr;
        unsigned long window = 0;
    };

    /** @brief Wayland native window handle. */
    struct WaylandWindow {
        void* display = nullptr;
        void* surface = nullptr;
    };

    /** @brief macOS Cocoa native window handle (future). */
    struct CocoaWindow {
        void* nsWindow = nullptr;
    };

    /** @brief Android native window handle (future). */
    struct AndroidWindow {
        void* aNativeWindow = nullptr;
    };

    /** @brief Web/Emscripten canvas selector (borrowed pointer, caller owns string). */
    struct WebWindow {
        const char* canvasSelector = nullptr;
    };

    /** @brief Cross-platform native window handle.
     *
     * Type-safe tagged union via std::variant (C++23 best practice).
     * All variants are POD — variant is trivially destructible.
     * Inspired by Diligent Engine's NativeWindow abstraction.
     *
     * Usage:
     *   NativeWindowHandle handle = Win32Window{ .hwnd = myHwnd };
     *   if (auto* w = std::get_if<Win32Window>(&handle)) { ... }
     */
    using NativeWindowHandle
        = std::variant<Win32Window, XlibWindow, WaylandWindow, CocoaWindow, AndroidWindow, WebWindow>;

    static_assert(std::is_trivially_destructible_v<NativeWindowHandle>);
    static_assert(std::is_trivially_copyable_v<Win32Window>);
    static_assert(std::is_trivially_copyable_v<XlibWindow>);
    static_assert(std::is_trivially_copyable_v<WaylandWindow>);

    // ===========================================================================
    // RenderSurfaceConfig — mutable configuration (re-configurable)
    // ===========================================================================

    /** @brief Configuration for a RenderSurface.
     *
     * Passed to Configure() for creation and reconfiguration (resize, format change,
     * present mode change). Follows the wgpu surface.configure() pattern.
     */
    struct RenderSurfaceConfig {
        Format format = Format::BGRA8_UNORM;
        uint32_t width = 0;
        uint32_t height = 0;
        PresentMode presentMode = PresentMode::Fifo;
        uint32_t imageCount = 2;  ///< Swapchain image count (2 = double-buffer, 3 = triple)
        bool hdr = false;         ///< Future: HDR10/scRGB format negotiation

        constexpr auto operator==(const RenderSurfaceConfig&) const noexcept -> bool = default;
    };

    // ===========================================================================
    // RenderSurfaceCapabilities — queried from hardware
    // ===========================================================================

    /** @brief Hardware-reported capabilities for a RenderSurface.
     *
     * Populated by GetCapabilities(). Vulkan: vkGetPhysicalDeviceSurface*KHR.
     * D3D12: DXGI output enumeration. WebGPU: surface.GetCapabilities().
     * GL: fixed {RGBA8, Fifo}.
     */
    struct RenderSurfaceCapabilities {
        std::vector<Format> supportedFormats;
        std::vector<PresentMode> supportedPresentModes;
        Extent2D minExtent = {};
        Extent2D maxExtent = {};
        uint32_t minImageCount = 0;
        uint32_t maxImageCount = 0;
    };

    // ===========================================================================
    // RenderSurfaceDesc — creation descriptor (immutable after Create)
    // ===========================================================================

    /** @brief Descriptor for RenderSurface creation.
     *
     * IDevice is NOT stored here (passed as Create() parameter) to keep
     * RenderSurfaceDesc a regular, copyable value type.
     */
    struct RenderSurfaceDesc {
        NativeWindowHandle window = Win32Window{};
        RenderSurfaceConfig initialConfig = {};
    };

    // ===========================================================================
    // RenderSurface — Pimpl'd windowed present abstraction
    // ===========================================================================

    /** @brief Production-grade windowed present surface.
     *
     * Replaces ISwapchain. API is a strict superset of the old ISwapchain interface.
     * Internally delegates to a virtual Impl base class with per-backend subclasses
     * (Vulkan, D3D12, WebGPU, GL, Mock).
     *
     * Ownership: typically owned by MultiWindowManager (one per window).
     * Move-only. Not thread-safe.
     *
     * Lifecycle:
     *   1. Create(device, desc) — creates platform surface + initial swapchain
     *   2. Configure(config)    — reconfigure (resize, format, present mode)
     *   3. AcquireNextImage()   — get next presentable texture
     *   4. Present()            — present the acquired image
     *   5. Destructor           — waits for GPU idle, destroys surface + sync objects
     */
    class RenderSurface {
       public:
        ~RenderSurface();

        RenderSurface(const RenderSurface&) = delete;
        auto operator=(const RenderSurface&) -> RenderSurface& = delete;
        RenderSurface(RenderSurface&& iOther) noexcept;
        auto operator=(RenderSurface&& iOther) noexcept -> RenderSurface&;

        /** @brief Create a RenderSurface for the given device and window.
         *  @param iDevice Device that will render to this surface (borrowed, must outlive surface).
         *  @param iDesc   Surface descriptor (window handle + initial config).
         *  @return RenderSurface on success, error otherwise.
         */
        [[nodiscard]] static auto Create(IDevice& iDevice, const RenderSurfaceDesc& iDesc)
            -> miki::core::Result<std::unique_ptr<RenderSurface>>;

        // --- Configuration ---

        /** @brief Reconfigure the surface (resize, format change, present mode change).
         *
         * Waits for this surface's GPU work to complete before reconfiguring.
         * Does NOT perform a global WaitIdle — only waits for commands targeting
         * this surface's images (per-surface wait, Filament/wgpu pattern).
         */
        [[nodiscard]] auto Configure(const RenderSurfaceConfig& iConfig) -> miki::core::Result<void>;

        /** @brief Convenience: resize only (preserves current format/present mode).
         *  Equivalent to Configure() with only width/height changed.
         */
        [[nodiscard]] auto Resize(uint32_t iWidth, uint32_t iHeight) -> miki::core::Result<void>;

        /** @brief Query current configuration. */
        [[nodiscard]] auto GetConfig() const noexcept -> RenderSurfaceConfig;

        /** @brief Query hardware capabilities for this surface. */
        [[nodiscard]] auto GetCapabilities() const -> RenderSurfaceCapabilities;

        // --- Per-frame operations (ISwapchain-compatible) ---

        /** @brief Acquire the next presentable image.
         *  @return TextureHandle for the acquired image, or SwapchainOutOfDate on resize needed.
         */
        [[nodiscard]] auto AcquireNextImage() -> miki::core::Result<TextureHandle>;

        /** @brief Present the most recently acquired image. */
        [[nodiscard]] auto Present() -> miki::core::Result<void>;

        /** @brief Get the surface image format. */
        [[nodiscard]] auto GetFormat() const noexcept -> Format;

        /** @brief Get the current surface extent. */
        [[nodiscard]] auto GetExtent() const noexcept -> Extent2D;

        /** @brief Get the current acquired texture (valid between Acquire and Present). */
        [[nodiscard]] auto GetCurrentTexture() const noexcept -> TextureHandle;

        /** @brief Compat-tier sync info (binary semaphores + fence).
         *  Pass to IDevice::Submit() when rendering to this surface.
         */
        [[nodiscard]] auto GetSubmitSyncInfo() const noexcept -> SubmitSyncInfo;

        /** @brief Tier1 sync info (timeline + binary semaphores, no fence).
         *  Pass to IDevice::Submit2() when rendering to this surface.
         *  Returns empty on Compat-tier surfaces.
         */
        [[nodiscard]] auto GetSubmitSyncInfo2() const noexcept -> SubmitSyncInfo2;

        /** @brief Whether this surface uses timeline semaphore frame pacing. */
        [[nodiscard]] auto UsesTimelineFramePacing() const noexcept -> bool;

        /** @brief Build a SubmitInfo2 with the correct sync primitives for this surface.
         *
         *  Tier1: timeline signal + binary semaphores, no fence.
         *  Compat: binary semaphores + fence, no timeline.
         *  Callers can append additional timeline waits/signals before passing to Submit2().
         */
        [[nodiscard]] auto BuildSubmitInfo() const noexcept -> SubmitInfo2;

        // --- Internal ---

        /** @brief Backend implementation base class (internal, not exported). */
        struct Impl;

       private:
        std::unique_ptr<Impl> impl_;

        explicit RenderSurface(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::rhi
