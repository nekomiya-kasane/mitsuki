/** @brief WindowManager — platform-layer window lifecycle + per-window event dispatch.
 *
 * Pure window management: create/destroy OS windows, poll events, detect close/minimize.
 * Does NOT own any GPU resources (RenderSurface, FrameManager). Demos bind GPU resources
 * to windows externally.
 *
 * Design:
 *   - WindowManager delegates OS window ops to IWindowBackend (GLFW, SDL, Qt, etc.)
 *   - PollEvents() returns WindowEvent stream with per-window routing
 *   - Single-window is the N=1 special case of multi-window
 *
 * Thread safety: NOT thread-safe. All calls must be on the main/render thread.
 * Namespace: miki::platform
 */
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "miki/core/Result.h"
#include "miki/platform/Event.h"
#include "miki/rhi/RenderSurface.h"

namespace miki::platform {

    // ===========================================================================
    // WindowHandle — stable, monotonic, never-reused window identifier
    // ===========================================================================

    struct WindowHandle {
        uint32_t id = 0;

        [[nodiscard]] constexpr auto IsValid() const noexcept -> bool { return id != 0; }
        constexpr auto operator==(const WindowHandle&) const noexcept -> bool = default;
    };

    // ===========================================================================
    // WindowDesc — window creation descriptor (platform-only, no GPU config)
    // ===========================================================================

    struct WindowDesc {
        std::string_view title = "miki";
        uint32_t width = 1280;
        uint32_t height = 720;
    };

    // ===========================================================================
    // WindowInfo — read-only window state snapshot
    // ===========================================================================

    struct WindowInfo {
        WindowHandle handle = {};
        rhi::Extent2D extent = {};
        rhi::NativeWindowHandle nativeWindow = rhi::Win32Window{};
        bool alive = false;
        bool minimized = false;
    };

    // ===========================================================================
    // WindowEvent — event tagged with source window
    // ===========================================================================

    struct WindowEvent {
        WindowHandle window;
        neko::platform::Event event;
    };

    // ===========================================================================
    // IWindowBackend — platform abstraction for OS window management
    // ===========================================================================

    /** @brief Platform-specific window backend (GLFW, SDL, Qt, etc.).
     *
     * WindowManager delegates all OS window operations to this interface.
     * This keeps miki::platform free of any windowing library dependency.
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
            -> miki::core::Result<rhi::NativeWindowHandle>
            = 0;

        /** @brief Destroy an OS window.
         *  @param iNativeToken Token from CreateNativeWindow.
         */
        virtual auto DestroyNativeWindow(void* iNativeToken) -> void = 0;

        /** @brief Poll all pending window events and append to output buffer.
         *  @param ioEvents Buffer to append WindowEvent entries to.
         *
         * Called once per frame by WindowManager. Implementations should call the
         * platform event pump (e.g. glfwPollEvents()) and collect per-window events
         * via callbacks into ioEvents.
         */
        virtual auto PollEvents(std::vector<WindowEvent>& ioEvents) -> void = 0;

        /** @brief Check if a window's close button was pressed.
         *  @param iNativeToken Token from CreateNativeWindow.
         */
        [[nodiscard]] virtual auto ShouldClose(void* iNativeToken) -> bool = 0;

        /** @brief Get the current framebuffer size of a window.
         *  @param iNativeToken Token from CreateNativeWindow.
         */
        [[nodiscard]] virtual auto GetFramebufferSize(void* iNativeToken) -> rhi::Extent2D = 0;

        /** @brief Check if a window is minimized (zero-size framebuffer).
         *  @param iNativeToken Token from CreateNativeWindow.
         */
        [[nodiscard]] virtual auto IsMinimized(void* iNativeToken) -> bool = 0;

        /** @brief Inform the backend of the WindowHandle assigned to a native token.
         *
         *  Called by WindowManager immediately after CreateNativeWindow succeeds.
         *  Backends that produce WindowEvent in callbacks (e.g. GLFW) use this
         *  to tag events with the correct handle.
         *
         *  @param iNativeToken Token from CreateNativeWindow.
         *  @param iHandle      Handle assigned by WindowManager.
         */
        virtual auto SetWindowHandle(void* iNativeToken, WindowHandle iHandle) -> void {
            (void)iNativeToken;
            (void)iHandle;  // default no-op
        }
    };

    // ===========================================================================
    // WindowManager
    // ===========================================================================

    class WindowManager {
       public:
        static constexpr uint32_t kMaxWindows = 8;

        ~WindowManager();

        WindowManager(const WindowManager&) = delete;
        auto operator=(const WindowManager&) -> WindowManager& = delete;
        WindowManager(WindowManager&&) noexcept;
        auto operator=(WindowManager&&) noexcept -> WindowManager&;

        /** @brief Create a WindowManager.
         *  @param iBackend Platform window backend (owned by manager).
         */
        [[nodiscard]] static auto Create(std::unique_ptr<IWindowBackend> iBackend) -> miki::core::Result<WindowManager>;

        // --- Window lifecycle ---

        /** @brief Create a new OS window.
         *  @return Stable window handle, or error.
         */
        [[nodiscard]] auto CreateWindow(const WindowDesc& iDesc) -> miki::core::Result<WindowHandle>;

        /** @brief Destroy an OS window.
         *  @return Void on success, InvalidArgument if handle is invalid or already destroyed.
         */
        [[nodiscard]] auto DestroyWindow(WindowHandle iHandle) -> miki::core::Result<void>;

        // --- Query ---

        [[nodiscard]] auto GetWindowInfo(WindowHandle iHandle) const -> WindowInfo;
        [[nodiscard]] auto GetNativeHandle(WindowHandle iHandle) const -> rhi::NativeWindowHandle;
        [[nodiscard]] auto GetNativeToken(WindowHandle iHandle) const -> void*;

        // --- Enumeration ---

        [[nodiscard]] auto GetWindowCount() const noexcept -> uint32_t;
        [[nodiscard]] auto GetAllWindows() const -> std::vector<WindowHandle>;

        /** @brief Get windows that are alive and not minimized. */
        [[nodiscard]] auto GetActiveWindows() -> std::vector<WindowHandle>;

        // --- Frame-level operations ---

        /** @brief Poll OS events for all windows. Returns per-window event stream.
         *  Also detects close requests (marks windows as dead) and updates minimized state.
         *  Call once per frame.
         */
        auto PollEvents() -> std::span<const WindowEvent>;

        /** @brief Check if ALL windows have been closed (or destroyed). */
        [[nodiscard]] auto ShouldClose() const -> bool;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        explicit WindowManager(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::platform
