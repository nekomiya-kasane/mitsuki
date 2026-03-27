/** @brief MultiWindowManager — dynamic multi-window lifecycle with shared IDevice.
 *
 * Owns per-window RenderSurface + FrameManager pairs. Delegates OS window
 * creation/destruction to an IWindowBackend interface (GLFW, SDL, Qt, etc.).
 *
 * Design references:
 *   - Filament Engine: owns SwapChain objects, user gets handle back
 *   - Diligent Tutorial15_MultipleWindows: shared IRenderDevice + per-window ISwapChain
 *
 * Thread safety: NOT thread-safe. All calls must be on the render thread.
 * Namespace: miki::rhi
 */
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "miki/core/Result.h"
#include "miki/rhi/FrameManager.h"
#include "miki/rhi/RenderSurface.h"

namespace miki::rhi {

    class IDevice;

    // ===========================================================================
    // WindowHandle — stable, monotonic, never-reused window identifier
    // ===========================================================================

    struct WindowHandle {
        uint32_t id = 0;

        [[nodiscard]] constexpr auto IsValid() const noexcept -> bool { return id != 0; }
        constexpr auto operator==(const WindowHandle&) const noexcept -> bool = default;
    };

    // ===========================================================================
    // WindowDesc — window creation descriptor
    // ===========================================================================

    struct WindowDesc {
        std::string_view title = "miki";
        uint32_t width = 1280;
        uint32_t height = 720;
        RenderSurfaceConfig surfaceConfig = {};
    };

    // ===========================================================================
    // WindowInfo — read-only window state snapshot
    // ===========================================================================

    struct WindowInfo {
        WindowHandle handle = {};
        Extent2D extent = {};
        bool alive = false;
        bool minimized = false;
    };

    // ===========================================================================
    // IWindowBackend — platform abstraction for OS window management
    // ===========================================================================

    /** @brief Platform-specific window backend (GLFW, SDL, Qt, etc.).
     *
     * MultiWindowManager delegates all OS window operations to this interface.
     * This keeps miki::rhi free of any windowing library dependency.
     *
     * The `nativeToken` is an opaque backend-internal identifier returned by
     * CreateNativeWindow. It is used to identify the window in all subsequent
     * calls (ShouldClose, GetFramebufferSize, DestroyNativeWindow).
     * For GLFW: nativeToken = GLFWwindow*.
     */
    class IWindowBackend {
       public:
        virtual ~IWindowBackend() = default;

        /** @brief Create an OS window and return native handle + backend token.
         *  @param iDesc Window descriptor (title, size).
         *  @param[out] oNativeToken Opaque backend-internal window ID (e.g. GLFWwindow*).
         *  @return NativeWindowHandle for RenderSurface creation.
         */
        [[nodiscard]] virtual auto CreateNativeWindow(const WindowDesc& iDesc, void*& oNativeToken)
            -> miki::core::Result<NativeWindowHandle>
            = 0;

        /** @brief Destroy an OS window.
         *  @param iNativeToken Token from CreateNativeWindow.
         */
        virtual auto DestroyNativeWindow(void* iNativeToken) -> void = 0;

        /** @brief Poll all pending window events (called once per frame). */
        virtual auto PollEvents() -> void = 0;

        /** @brief Check if a window's close button was pressed.
         *  @param iNativeToken Token from CreateNativeWindow.
         */
        [[nodiscard]] virtual auto ShouldClose(void* iNativeToken) -> bool = 0;

        /** @brief Get the current framebuffer size of a window.
         *  @param iNativeToken Token from CreateNativeWindow.
         */
        [[nodiscard]] virtual auto GetFramebufferSize(void* iNativeToken) -> Extent2D = 0;

        /** @brief Check if a window is minimized (zero-size framebuffer).
         *  @param iNativeToken Token from CreateNativeWindow.
         */
        [[nodiscard]] virtual auto IsMinimized(void* iNativeToken) -> bool = 0;
    };

    // ===========================================================================
    // MultiWindowManager
    // ===========================================================================

    class MultiWindowManager {
       public:
        static constexpr uint32_t kMaxWindows = 8;

        ~MultiWindowManager();

        MultiWindowManager(const MultiWindowManager&) = delete;
        auto operator=(const MultiWindowManager&) -> MultiWindowManager& = delete;
        MultiWindowManager(MultiWindowManager&&) noexcept;
        auto operator=(MultiWindowManager&&) noexcept -> MultiWindowManager&;

        /** @brief Create a MultiWindowManager.
         *  @param iDevice  Shared device (borrowed, must outlive manager).
         *  @param iBackend Platform window backend (owned by manager).
         */
        [[nodiscard]] static auto Create(IDevice& iDevice, std::unique_ptr<IWindowBackend> iBackend)
            -> miki::core::Result<MultiWindowManager>;

        // --- Window lifecycle ---

        /** @brief Create a new window with its own RenderSurface + FrameManager.
         *  @return Stable window handle, or error.
         */
        [[nodiscard]] auto CreateWindow(const WindowDesc& iDesc) -> miki::core::Result<WindowHandle>;

        /** @brief Destroy a window (per-surface WaitIdle, then cleanup).
         *  @return Void on success, InvalidArgument if handle is invalid or already destroyed.
         */
        [[nodiscard]] auto DestroyWindow(WindowHandle iHandle) -> miki::core::Result<void>;

        // --- Per-window resource access ---

        [[nodiscard]] auto GetRenderSurface(WindowHandle iHandle) -> RenderSurface*;
        [[nodiscard]] auto GetFrameManager(WindowHandle iHandle) -> FrameManager*;
        [[nodiscard]] auto GetWindowInfo(WindowHandle iHandle) const -> WindowInfo;

        // --- Enumeration ---

        [[nodiscard]] auto GetWindowCount() const noexcept -> uint32_t;
        [[nodiscard]] auto GetAllWindows() const -> std::vector<WindowHandle>;
        [[nodiscard]] auto GetActiveWindows() -> std::vector<WindowHandle>;

        // --- Frame-level operations ---

        /** @brief Poll OS events for all windows. Call once per frame. */
        auto PollEvents() -> void;

        /** @brief Check if ALL windows have been closed (or destroyed). */
        [[nodiscard]] auto ShouldClose() const -> bool;

        /** @brief Update window states: detect close requests, minimization, resize.
         *  Call after PollEvents(). Automatically destroys windows whose close was requested.
         */
        auto ProcessWindowEvents() -> void;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        explicit MultiWindowManager(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::rhi
