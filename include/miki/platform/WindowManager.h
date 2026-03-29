/** @brief WindowManager — platform-layer window lifecycle with N-ary tree structure.
 *
 * Pure window management: create/destroy OS windows, poll events, detect close/minimize.
 * Supports parent-child window hierarchies (forest of N-ary trees) with cascade destruction.
 * Does NOT own any GPU resources (RenderSurface, FrameManager, SurfaceManager).
 *
 * Design:
 *   - WindowManager delegates OS window ops to IWindowBackend (GLFW, SDL, Qt, etc.)
 *   - PollEvents() returns WindowEvent stream with per-window routing
 *   - CloseRequested is an event — application decides close policy (no auto-destroy)
 *   - Single-window is the N=1 special case of multi-window
 *
 * See: specs/01-window-manager.md §3–§4 for design rationale.
 *
 * Thread safety: NOT thread-safe. All calls must be on the main thread.
 * Namespace: miki::platform
 */
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "miki/core/Flags.h"
#include "miki/core/Result.h"
#include "miki/platform/Event.h"
#include "miki/rhi/RhiTypes.h"
#include "miki/platform/WindowHandle.h"

namespace miki::platform {

    // ===========================================================================
    // WindowFlags — bitfield for window creation options
    // ===========================================================================

    enum class WindowFlags : uint32_t {
        None = 0,
        Borderless = 1 << 0,
        AlwaysOnTop = 1 << 1,
        NoResize = 1 << 2,
        Hidden = 1 << 3,
    };
    MIKI_ENABLE_ENUM_FLAGS(WindowFlags);

    // ===========================================================================
    // WindowDesc — window creation descriptor (platform-only, no GPU config)
    // ===========================================================================

    struct WindowDesc {
        std::string_view title = "miki";
        uint32_t width = 1280;
        uint32_t height = 720;
        WindowHandle parent = {};
        core::EnumFlags<WindowFlags> flags = WindowFlags::None;
#ifdef __EMSCRIPTEN__
        /// CSS selector for the target canvas (e.g. "#canvas1").
        /// If empty, derived from title as "#<title>".
        std::string_view canvasSelector = {};
#endif
    };

    // ===========================================================================
    // WindowInfo — read-only window state snapshot
    // ===========================================================================

    struct WindowInfo {
        WindowHandle handle = {};
        rhi::Extent2D extent = {};
        rhi::NativeWindowHandle nativeWindow = rhi::Win32Window{};
        core::EnumFlags<WindowFlags> flags = WindowFlags::None;
        bool alive = false;
        bool minimized = false;
        bool maximized = false;
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

        /** @brief Create an OS window with optional parent.
         *  @param iDesc         Window descriptor (title, size, flags).
         *  @param iParentToken  Native token of parent window (nullptr = root).
         *  @param[out] oNativeToken Opaque backend-internal window ID (e.g. GLFWwindow*).
         *  @return NativeWindowHandle for RenderSurface creation.
         */
        [[nodiscard]] virtual auto CreateNativeWindow(const WindowDesc& iDesc, void* iParentToken, void*& oNativeToken)
            -> miki::core::Result<rhi::NativeWindowHandle>
            = 0;

        /** @brief Destroy an OS window.
         *  @param iNativeToken Token from CreateNativeWindow.
         */
        virtual auto DestroyNativeWindow(void* iNativeToken) -> void = 0;

        /** @brief Poll all pending window events and append to output buffer.
         *  @param ioEvents Buffer to append WindowEvent entries to.
         */
        virtual auto PollEvents(std::vector<WindowEvent>& ioEvents) -> void = 0;

        /** @brief Check if a window's close button was pressed. */
        [[nodiscard]] virtual auto ShouldClose(void* iNativeToken) -> bool = 0;

        /** @brief Get the current framebuffer size of a window. */
        [[nodiscard]] virtual auto GetFramebufferSize(void* iNativeToken) -> rhi::Extent2D = 0;

        /** @brief Check if a window is minimized (zero-size framebuffer). */
        [[nodiscard]] virtual auto IsMinimized(void* iNativeToken) -> bool = 0;

        /** @brief Show a hidden window. */
        virtual auto ShowWindow(void* iNativeToken) -> void = 0;

        /** @brief Hide a window without destroying it. */
        virtual auto HideWindow(void* iNativeToken) -> void = 0;

        // ── Window state operations ──────────────────────────────────

        /** @brief Resize window client area. */
        virtual auto ResizeWindow(void* iNativeToken, uint32_t iWidth, uint32_t iHeight) -> void = 0;

        /** @brief Set window position (top-left corner in screen coordinates). */
        virtual auto SetWindowPosition(void* iNativeToken, int32_t iX, int32_t iY) -> void = 0;

        /** @brief Get window position (top-left corner in screen coordinates). */
        [[nodiscard]] virtual auto GetWindowPosition(void* iNativeToken) const -> std::pair<int32_t, int32_t> = 0;

        /** @brief Minimize (iconify) a window. */
        virtual auto MinimizeWindow(void* iNativeToken) -> void = 0;

        /** @brief Maximize a window. */
        virtual auto MaximizeWindow(void* iNativeToken) -> void = 0;

        /** @brief Restore a minimized or maximized window to normal state. */
        virtual auto RestoreWindow(void* iNativeToken) -> void = 0;

        /** @brief Request input focus for a window. */
        virtual auto FocusWindow(void* iNativeToken) -> void = 0;

        /** @brief Change the window title. */
        virtual auto SetWindowTitle(void* iNativeToken, std::string_view iTitle) -> void = 0;

        /** @brief Inform the backend of the WindowHandle assigned to a native token.
         *
         *  Called by WindowManager immediately after CreateNativeWindow succeeds.
         *  Backends that produce WindowEvent in callbacks (e.g. GLFW) use this
         *  to tag events with the correct handle.
         */
        virtual auto SetWindowHandle(void* iNativeToken, WindowHandle iHandle) -> void {
            (void)iNativeToken;
            (void)iHandle;
        }

        // -- Gamepad / Joystick support ------------------------------------

        /** @brief Poll gamepad connection and input events since last call.
         *  @param ioEvents Buffer to append GamepadEvent entries to.
         *
         *  Default implementation is a no-op (backend has no gamepad support).
         */
        virtual auto PollGamepadEvents(std::vector<neko::platform::GamepadEvent>& ioEvents) -> void { (void)ioEvents; }

        /** @brief Query the current state of a connected gamepad.
         *  @param iGamepadId  Gamepad slot [0, kMaxGamepads).
         *  @param oState      Filled with current axes/buttons if connected.
         *  @return true if gamepad is connected and state was filled.
         */
        [[nodiscard]] virtual auto GetGamepadState(uint8_t iGamepadId, neko::platform::GamepadState& oState) const
            -> bool {
            (void)iGamepadId;
            (void)oState;
            return false;
        }

        /** @brief Check if a gamepad slot is currently connected. */
        [[nodiscard]] virtual auto IsGamepadConnected(uint8_t iGamepadId) const -> bool {
            (void)iGamepadId;
            return false;
        }

        /** @brief Get the human-readable name of a connected gamepad.
         *  @return Name string, or empty if not connected.
         */
        [[nodiscard]] virtual auto GetGamepadName(uint8_t iGamepadId) const -> std::string_view {
            (void)iGamepadId;
            return {};
        }
    };

    // ===========================================================================
    // WindowManager — forest of N-ary trees with cascade destruction
    // ===========================================================================

    class WindowManager {
       public:
        static constexpr uint32_t kDefaultChunkSize = 16;
        static constexpr uint32_t kMaxDepth = 4;

        ~WindowManager();

        WindowManager(const WindowManager&) = delete;
        auto operator=(const WindowManager&) -> WindowManager& = delete;
        WindowManager(WindowManager&&) noexcept;
        auto operator=(WindowManager&&) noexcept -> WindowManager&;

        /** @brief Create a WindowManager with a platform backend.
         *  @param iBackend Platform window backend (owned by manager).
         */
        [[nodiscard]] static auto Create(std::unique_ptr<IWindowBackend> iBackend) -> miki::core::Result<WindowManager>;

        // ── Window lifecycle ────────────────────────────────────────

        /** @brief Create a window. If desc.parent is valid, the new window
         *         becomes a child of that parent.
         *  @return Stable window handle with generation, or error.
         */
        [[nodiscard]] auto CreateWindow(const WindowDesc& iDesc) -> miki::core::Result<WindowHandle>;

        /** @brief Destroy a window and all its descendants (post-order cascade).
         *  @return List of destroyed handles (deepest-first), or error.
         *          Caller is responsible for tearing down GPU surfaces BEFORE calling this.
         *          In debug builds, asserts that no surface is attached.
         */
        [[nodiscard]] auto DestroyWindow(WindowHandle iHandle) -> miki::core::Result<std::vector<WindowHandle>>;

        /** @brief Show a hidden window (created with WindowFlags::Hidden). */
        auto ShowWindow(WindowHandle iHandle) -> void;

        /** @brief Hide a window without destroying it. */
        auto HideWindow(WindowHandle iHandle) -> void;

        // ── Window state operations ──────────────────────────────────

        /** @brief Programmatically resize a window's client area.
         *         On Emscripten: may be limited by CSS layout constraints.
         */
        auto ResizeWindow(WindowHandle iHandle, uint32_t iWidth, uint32_t iHeight) -> void;

        /** @brief Move a window to a new screen position (top-left corner).
         *         On Emscripten: no-op (canvas position is CSS-controlled).
         */
        auto SetWindowPosition(WindowHandle iHandle, int32_t iX, int32_t iY) -> void;

        /** @brief Get the current window position (top-left corner).
         *         On Emscripten: returns (0, 0).
         */
        [[nodiscard]] auto GetWindowPosition(WindowHandle iHandle) const -> std::pair<int32_t, int32_t>;

        /** @brief Minimize (iconify) a window.
         *         On Emscripten: no-op (browsers have no minimize).
         */
        auto MinimizeWindow(WindowHandle iHandle) -> void;

        /** @brief Maximize a window to fill the screen/work area.
         *         On Emscripten: requests fullscreen (requires user gesture).
         */
        auto MaximizeWindow(WindowHandle iHandle) -> void;

        /** @brief Restore a minimized or maximized window to normal state.
         *         On Emscripten: exits fullscreen if active.
         */
        auto RestoreWindow(WindowHandle iHandle) -> void;

        /** @brief Request input focus for a window.
         *         May be ignored by the OS if the application is not in foreground.
         */
        auto FocusWindow(WindowHandle iHandle) -> void;

        /** @brief Change the window title at runtime.
         *         On Emscripten: sets document.title for the first window.
         */
        auto SetWindowTitle(WindowHandle iHandle, std::string_view iTitle) -> void;

        // ── Tree queries ────────────────────────────────────────────

        [[nodiscard]] auto GetParent(WindowHandle iHandle) const -> WindowHandle;
        [[nodiscard]] auto GetChildren(WindowHandle iHandle) const -> std::span<const WindowHandle>;
        [[nodiscard]] auto GetRoot(WindowHandle iHandle) const -> WindowHandle;
        [[nodiscard]] auto GetDepth(WindowHandle iHandle) const -> uint32_t;

        /** @brief Enumerate all descendants in post-order (leaves first).
         *         Used by SurfaceManager for safe cascade teardown.
         */
        [[nodiscard]] auto GetDescendantsPostOrder(WindowHandle iHandle) const -> std::vector<WindowHandle>;

        // ── State queries ───────────────────────────────────────────

        [[nodiscard]] auto GetWindowInfo(WindowHandle iHandle) const -> WindowInfo;
        [[nodiscard]] auto GetNativeHandle(WindowHandle iHandle) const -> rhi::NativeWindowHandle;
        [[nodiscard]] auto GetNativeToken(WindowHandle iHandle) const -> void*;

        // ── Enumeration ─────────────────────────────────────────────

        [[nodiscard]] auto GetWindowCount() const noexcept -> uint32_t;
        [[nodiscard]] auto GetAllWindows() const -> std::vector<WindowHandle>;
        [[nodiscard]] auto GetRootWindows() const -> std::vector<WindowHandle>;
        [[nodiscard]] auto GetActiveWindows() -> std::vector<WindowHandle>;

        // ── Event polling ───────────────────────────────────────────

        /** @brief Poll OS events. Returns per-window event stream.
         *         Updates alive/minimized state. Does NOT auto-destroy closed windows
         *         (caller decides policy via CloseRequested event).
         */
        auto PollEvents() -> std::span<const WindowEvent>;

        /** @brief Check if ALL root windows have been closed. */
        [[nodiscard]] auto ShouldClose() const -> bool;

        // -- Gamepad / Joystick -------------------------------------------

        /** @brief Poll gamepad events (connect/disconnect, button, axis).
         *  @return Span of gamepad events valid until next PollGamepadEvents call.
         */
        auto PollGamepadEvents() -> std::span<const neko::platform::GamepadEvent>;

        /** @brief Query current state of a connected gamepad.
         *  @return true if gamepad is connected and state was filled.
         */
        [[nodiscard]] auto GetGamepadState(uint8_t iGamepadId, neko::platform::GamepadState& oState) const -> bool;

        /** @brief Check if a gamepad slot is connected. */
        [[nodiscard]] auto IsGamepadConnected(uint8_t iGamepadId) const -> bool;

        /** @brief Get human-readable name of a connected gamepad. */
        [[nodiscard]] auto GetGamepadName(uint8_t iGamepadId) const -> std::string_view;

        /** @brief Register a callback to check if a window has an attached GPU surface.
         *         Used by DestroyWindow to assert surfaces are detached before OS destroy.
         *         SurfaceManager should call this after creation.
         */
        using HasSurfaceFn = bool (*)(WindowHandle);
        auto SetHasSurfaceCallback(HasSurfaceFn iCallback) -> void;

        /** @brief Get the backend for advanced operations (e.g., EventSimulator). */
        [[nodiscard]] auto GetBackend() noexcept -> IWindowBackend*;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        explicit WindowManager(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::platform
