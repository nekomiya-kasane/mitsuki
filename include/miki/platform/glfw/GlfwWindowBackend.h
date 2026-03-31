/** @brief GlfwWindowBackend — GLFW3 implementation of IWindowBackend.
 *
 * Creates/destroys GLFW windows, extracts native handles (HWND on Win32,
 * canvas selector on Emscripten/WASM). Polls events with full input event
 * bridging (keyboard, mouse, scroll, resize, focus, close, text input).
 * Backend-aware window hints.
 *
 * On Emscripten, uses contrib.glfw3 (pongasoft/emscripten-glfw) which maps
 * each GLFWwindow to an HTML <canvas> element. See specs/01-window-manager.md §9.5.
 *
 * Absorbs the former GlfwBridge's callback logic — each window gets its own
 * event buffer, collected during PollEvents().
 *
 * Namespace: miki::platform
 */
#pragma once

#include "miki/platform/WindowManager.h"
#include "miki/rhi/RhiTypes.h"

#include <array>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

namespace miki::platform {

    class GlfwWindowBackend final : public miki::platform::IWindowBackend {
       public:
        /** @param iBackendType GPU backend — determines GLFW window hints.
         *  @param iVisible     When false, windows are hidden (headless testing).
         */
        explicit GlfwWindowBackend(miki::rhi::BackendType iBackendType, bool iVisible = true);
        ~GlfwWindowBackend() override;

        GlfwWindowBackend(const GlfwWindowBackend&) = delete;
        auto operator=(const GlfwWindowBackend&) -> GlfwWindowBackend& = delete;

        [[nodiscard]] auto CreateNativeWindow(
            const miki::platform::WindowDesc& iDesc, void* iParentToken, void*& oNativeToken
        ) -> miki::core::Result<miki::rhi::NativeWindowHandle> override;

        auto DestroyNativeWindow(void* iNativeToken) -> void override;
        auto PollEvents(std::vector<miki::platform::WindowEvent>& ioEvents) -> void override;

        [[nodiscard]] auto ShouldClose(void* iNativeToken) -> bool override;
        [[nodiscard]] auto GetFramebufferSize(void* iNativeToken) -> miki::rhi::Extent2D override;
        [[nodiscard]] auto IsMinimized(void* iNativeToken) -> bool override;
        auto ShowWindow(void* iNativeToken) -> void override;
        auto HideWindow(void* iNativeToken) -> void override;

        // Window state operations
        auto ResizeWindow(void* iNativeToken, uint32_t iWidth, uint32_t iHeight) -> void override;
        auto SetWindowPosition(void* iNativeToken, int32_t iX, int32_t iY) -> void override;
        [[nodiscard]] auto GetWindowPosition(void* iNativeToken) const -> std::pair<int32_t, int32_t> override;
        auto MinimizeWindow(void* iNativeToken) -> void override;
        auto MaximizeWindow(void* iNativeToken) -> void override;
        auto RestoreWindow(void* iNativeToken) -> void override;
        auto FocusWindow(void* iNativeToken) -> void override;
        auto SetWindowTitle(void* iNativeToken, std::string_view iTitle) -> void override;

        auto SetWindowHandle(void* iNativeToken, miki::platform::WindowHandle iHandle) -> void override;

        // -- Gamepad / Joystick --
        auto PollGamepadEvents(std::vector<neko::platform::GamepadEvent>& ioEvents) -> void override;
        [[nodiscard]] auto GetGamepadState(uint8_t iGamepadId, neko::platform::GamepadState& oState) const
            -> bool override;
        [[nodiscard]] auto IsGamepadConnected(uint8_t iGamepadId) const -> bool override;
        [[nodiscard]] auto GetGamepadName(uint8_t iGamepadId) const -> std::string_view override;

        /** @brief Get the GLFWwindow* for a given native token. Used by ImGui init. */
        [[nodiscard]] static auto GetGlfwWindow(void* iNativeToken) noexcept -> GLFWwindow* {
            return static_cast<GLFWwindow*>(iNativeToken);
        }

        /** @brief Get mutable reference to pending events for EventSimulator injection. */
        [[nodiscard]] auto GetPendingEvents() noexcept -> std::vector<miki::platform::WindowEvent>& {
            return pendingEvents_;
        }

        // -- Live resize callback --
        auto SetLiveResizeCallback(LiveResizeCallback iCallback) -> void override;

        // -- OpenGL-specific operations --
        [[nodiscard]] auto GetGLProcLoader() const noexcept -> GLProcLoader override;
        auto SwapBuffers(void* iNativeToken) -> void override;

       private:
        static auto InitGlfw() -> bool;

        // Per-window data stored via glfwSetWindowUserPointer
        struct PerWindowData {
            GlfwWindowBackend* owner = nullptr;
            miki::platform::WindowHandle handle = {};
            double lastMouseX = 0.0;
            double lastMouseY = 0.0;
#ifdef __EMSCRIPTEN__
            std::string canvasSelector;  // e.g. "#canvas1" — owned string for NativeWindowHandle lifetime
#endif
        };

        miki::rhi::BackendType backendType_;
        bool visible_;
#ifndef __EMSCRIPTEN__
        GLFWwindow* glShareContext_ = nullptr;  // OpenGL shared context (desktop only)
#endif

        // token (GLFWwindow*) → PerWindowData
        std::unordered_map<void*, PerWindowData> windowData_;

        // Accumulated events during glfwPollEvents callback storm
        std::vector<miki::platform::WindowEvent> pendingEvents_;

        // Live resize callback (invoked from FramebufferSizeCallback during OS modal resize)
        LiveResizeCallback liveResizeCallback_;

        // -- Gamepad state tracking --
        static constexpr float kAxisDeadzone = 0.05f;

        struct GamepadSlot {
            bool connected = false;
            bool isGamepad = false;  // has SDL gamepad mapping
            neko::platform::GamepadState prevState = {};
        };
        std::array<GamepadSlot, neko::platform::kMaxGamepads> gamepadSlots_ = {};

        // Pending gamepad events from GLFW joystick callback
        std::vector<neko::platform::GamepadEvent> pendingGamepadEvents_;

        // --- GLFW callback trampolines ---
        static auto KeyCallback(GLFWwindow* w, int key, int scancode, int action, int mods) -> void;
        static auto MouseButtonCallback(GLFWwindow* w, int button, int action, int mods) -> void;
        static auto CursorPosCallback(GLFWwindow* w, double x, double y) -> void;
        static auto ScrollCallback(GLFWwindow* w, double xoff, double yoff) -> void;
        static auto FramebufferSizeCallback(GLFWwindow* w, int width, int height) -> void;
        static auto CharCallback(GLFWwindow* w, unsigned int codepoint) -> void;
        static auto WindowFocusCallback(GLFWwindow* w, int focused) -> void;
        static auto WindowCloseCallback(GLFWwindow* w) -> void;
        static auto WindowIconifyCallback(GLFWwindow* w, int iconified) -> void;
        static auto WindowMaximizeCallback(GLFWwindow* w, int maximized) -> void;
        static auto JoystickCallback(int jid, int event) -> void;

       public:
        // --- Key/mouse mapping (from GlfwBridge) ---
        [[nodiscard]] static auto MapKey(int iGlfwKey) noexcept -> neko::platform::Key;
        [[nodiscard]] static auto MapModifiers(int iGlfwMods) noexcept -> neko::platform::Modifiers;
        [[nodiscard]] static auto MapMouseButton(int iGlfwButton) noexcept -> neko::platform::MouseBtn;
        [[nodiscard]] static auto MapAction(int iGlfwAction) noexcept -> neko::platform::Action;
    };

}  // namespace miki::platform
