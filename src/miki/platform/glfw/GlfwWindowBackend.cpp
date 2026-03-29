/** @brief GlfwWindowBackend implementation.
 *
 * Absorbs GlfwBridge's callback logic: installs GLFW callbacks per-window,
 * converts to neko::platform::Event tagged with WindowHandle.
 */

#include "miki/platform/glfw/GlfwWindowBackend.h"

#include "miki/core/ErrorCode.h"

#include <string>
#include <atomic>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef __EMSCRIPTEN__
#    include <GLFW/emscripten_glfw3.h>
#    include <emscripten/emscripten.h>
#elif defined(_WIN32)
#    define GLFW_EXPOSE_NATIVE_WIN32
#    include <GLFW/glfw3native.h>
#endif

namespace miki::platform {

    // ---------------------------------------------------------------------------
    // GLFW ref-counted init + global gamepad backend pointer
    // ---------------------------------------------------------------------------

    static std::atomic<int> sGlfwRefCount{0};
    static GlfwWindowBackend* sGamepadOwner = nullptr;  // Only one backend owns joystick callbacks

    auto GlfwWindowBackend::InitGlfw() -> bool {
        if (sGlfwRefCount.fetch_add(1) == 0) {
            if (glfwInit() == GLFW_FALSE) {
                sGlfwRefCount.fetch_sub(1);
                return false;
            }
        }
        return true;
    }

    // ---------------------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------------------

    GlfwWindowBackend::GlfwWindowBackend(miki::rhi::BackendType iBackendType, bool iVisible)
        : backendType_(iBackendType), visible_(iVisible) {
        InitGlfw();

        // Register as gamepad owner (first backend wins)
        if (!sGamepadOwner) {
            sGamepadOwner = this;
            glfwSetJoystickCallback(JoystickCallback);
            // Detect already-connected joysticks
            for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST && jid < neko::platform::kMaxGamepads; ++jid) {
                if (glfwJoystickPresent(jid)) {
                    auto& slot = gamepadSlots_[static_cast<uint8_t>(jid)];
                    slot.connected = true;
                    slot.isGamepad = (glfwJoystickIsGamepad(jid) == GLFW_TRUE);
                }
            }
        }
    }

    GlfwWindowBackend::~GlfwWindowBackend() {
        if (sGamepadOwner == this) {
            glfwSetJoystickCallback(nullptr);
            sGamepadOwner = nullptr;
        }

        for (auto& [token, data] : windowData_) {
            auto* window = static_cast<GLFWwindow*>(token);
            glfwSetWindowUserPointer(window, nullptr);
            glfwDestroyWindow(window);
        }
        windowData_.clear();

        if (sGlfwRefCount.fetch_sub(1) == 1) {
            glfwTerminate();
        } else {
            // Flush pending Win32 WM_DESTROY messages so subsequent
            // glfwCreateWindow calls from other backends succeed immediately.
            glfwPollEvents();
        }
    }

    // ---------------------------------------------------------------------------
    // IWindowBackend — CreateNativeWindow
    // ---------------------------------------------------------------------------

    auto GlfwWindowBackend::CreateNativeWindow(
        const miki::platform::WindowDesc& iDesc, void* iParentToken, void*& oNativeToken
    ) -> miki::core::Result<miki::rhi::NativeWindowHandle> {
        glfwDefaultWindowHints();

        // WindowFlags → GLFW hints
        bool hidden = !visible_ || iDesc.flags.Has(miki::platform::WindowFlags::Hidden);
        if (hidden) {
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        }
        if (iDesc.flags.Has(miki::platform::WindowFlags::Borderless)) {
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        }
        if (iDesc.flags.Has(miki::platform::WindowFlags::NoResize)) {
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        } else {
            glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        }
        if (iDesc.flags.Has(miki::platform::WindowFlags::AlwaysOnTop)) {
            glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
        }

#ifdef __EMSCRIPTEN__
        // Emscripten: determine canvas selector and bind before glfwCreateWindow
        std::string selector;
        if (!iDesc.canvasSelector.empty()) {
            selector = std::string(iDesc.canvasSelector);
        } else {
            selector = "#" + std::string(iDesc.title);
        }
        emscripten::glfw3::SetNextWindowCanvasSelector(selector.c_str());

        // WebGPU is the only GPU backend on Emscripten — no GL context needed
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        (void)iParentToken;  // parent-child is purely logical on Emscripten

        std::string titleStr(iDesc.title);
        auto* window = glfwCreateWindow(
            static_cast<int>(iDesc.width), static_cast<int>(iDesc.height), titleStr.c_str(), nullptr, nullptr
        );
#else
        GLFWwindow* shareCtx = nullptr;

        if (backendType_ == miki::rhi::BackendType::OpenGL43) {
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            shareCtx = iParentToken ? static_cast<GLFWwindow*>(iParentToken) : glShareContext_;
        } else {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            (void)iParentToken;
        }

        std::string titleStr(iDesc.title);
        auto* window = glfwCreateWindow(
            static_cast<int>(iDesc.width), static_cast<int>(iDesc.height), titleStr.c_str(), nullptr, shareCtx
        );
#endif

        if (!window) {
            return std::unexpected(miki::core::ErrorCode::DeviceNotReady);
        }

#ifndef __EMSCRIPTEN__
        if (backendType_ == miki::rhi::BackendType::OpenGL43 && !glShareContext_) {
            glShareContext_ = window;
        }
#endif

        oNativeToken = static_cast<void*>(window);

        // Assign a WindowHandle — use a monotonic counter embedded in the data map
        // The actual handle ID is assigned by WindowManager; we use a placeholder here
        // that WindowManager will correlate via the token pointer.
        // We store 0 as placeholder — WindowManager matches tokens to handles.
        auto& data = windowData_[oNativeToken];
        data.owner = this;
        data.handle = {};  // placeholder, correlated by WindowManager

        // Get initial mouse position
        glfwGetCursorPos(window, &data.lastMouseX, &data.lastMouseY);

        // Install callbacks
        glfwSetWindowUserPointer(window, &data);
        glfwSetKeyCallback(window, KeyCallback);
        glfwSetMouseButtonCallback(window, MouseButtonCallback);
        glfwSetCursorPosCallback(window, CursorPosCallback);
        glfwSetScrollCallback(window, ScrollCallback);
        glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
        glfwSetCharCallback(window, CharCallback);
        glfwSetWindowFocusCallback(window, WindowFocusCallback);
        glfwSetWindowCloseCallback(window, WindowCloseCallback);
        glfwSetWindowIconifyCallback(window, WindowIconifyCallback);
#ifndef __EMSCRIPTEN__
        glfwSetWindowMaximizeCallback(window, WindowMaximizeCallback);
#endif

#ifdef __EMSCRIPTEN__
        data.canvasSelector = std::move(selector);
        return miki::rhi::NativeWindowHandle{miki::rhi::WebWindow{.canvasSelector = data.canvasSelector.c_str()}};
#elif defined(_WIN32)
        HWND hwnd = glfwGetWin32Window(window);
        return miki::rhi::NativeWindowHandle{miki::rhi::Win32Window{.hwnd = static_cast<void*>(hwnd)}};
#elif defined(__linux__)
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
            struct wl_surface* surface = glfwGetWaylandWindow(window);
            struct wl_display* display = glfwGetWaylandDisplay();
            return miki::rhi::NativeWindowHandle{miki::rhi::WaylandWindow{.surface = surface, .display = display}};
        } else {
            Window x11Window = glfwGetX11Window(window);
            Display* x11Display = glfwGetX11Display();
            return miki::rhi::NativeWindowHandle{miki::rhi::X11Window{.window = x11Window, .display = x11Display}};
        }
#elif defined(__APPLE__)
        id metalLayer = glfwGetCocoaWindow(window);
        return miki::rhi::NativeWindowHandle{miki::rhi::CocoaWindow{.nsWindow = metalLayer}};
#else
        return std::unexpected(miki::core::ErrorCode::NotSupported);
#endif
    }

    // ---------------------------------------------------------------------------
    // IWindowBackend — DestroyNativeWindow
    // ---------------------------------------------------------------------------

    auto GlfwWindowBackend::DestroyNativeWindow(void* iNativeToken) -> void {
        auto it = windowData_.find(iNativeToken);
        if (it == windowData_.end()) {
            return;
        }

        auto* window = static_cast<GLFWwindow*>(iNativeToken);

#ifndef __EMSCRIPTEN__
        if (window == glShareContext_) {
            glShareContext_ = nullptr;
        }
#endif

        glfwSetWindowUserPointer(window, nullptr);
        glfwDestroyWindow(window);
        windowData_.erase(it);
    }

    // ---------------------------------------------------------------------------
    // IWindowBackend — PollEvents (returns per-window events)
    // ---------------------------------------------------------------------------

    auto GlfwWindowBackend::PollEvents(std::vector<miki::platform::WindowEvent>& ioEvents) -> void {
        pendingEvents_.clear();
        glfwPollEvents();  // triggers all callbacks, which fill pendingEvents_
        ioEvents.append_range(pendingEvents_);
    }

    // ---------------------------------------------------------------------------
    // IWindowBackend — query
    // ---------------------------------------------------------------------------

    auto GlfwWindowBackend::SetWindowHandle(void* iNativeToken, miki::platform::WindowHandle iHandle) -> void {
        auto it = windowData_.find(iNativeToken);
        if (it != windowData_.end()) {
            it->second.handle = iHandle;
        }
    }

    auto GlfwWindowBackend::ShouldClose(void* iNativeToken) -> bool {
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (!window) {
            return true;
        }
        return glfwWindowShouldClose(window) != 0;
    }

    auto GlfwWindowBackend::GetFramebufferSize(void* iNativeToken) -> miki::rhi::Extent2D {
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (!window) {
            return {};
        }
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        return {.width = static_cast<uint32_t>(w), .height = static_cast<uint32_t>(h)};
    }

    auto GlfwWindowBackend::IsMinimized(void* iNativeToken) -> bool {
#ifdef __EMSCRIPTEN__
        (void)iNativeToken;
        return false;  // Browsers have no minimize concept for canvases
#else
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (!window) {
            return true;
        }
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        return w == 0 || h == 0;
#endif
    }

    auto GlfwWindowBackend::ShowWindow(void* iNativeToken) -> void {
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (window) {
            glfwShowWindow(window);
        }
    }

    auto GlfwWindowBackend::HideWindow(void* iNativeToken) -> void {
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (window) {
            glfwHideWindow(window);
        }
    }

    // ---------------------------------------------------------------------------
    // IWindowBackend — Window state operations
    // ---------------------------------------------------------------------------

    auto GlfwWindowBackend::ResizeWindow(void* iNativeToken, uint32_t iWidth, uint32_t iHeight) -> void {
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (window) {
            glfwSetWindowSize(window, static_cast<int>(iWidth), static_cast<int>(iHeight));
        }
    }

    auto GlfwWindowBackend::SetWindowPosition(void* iNativeToken, int32_t iX, int32_t iY) -> void {
#ifdef __EMSCRIPTEN__
        auto it = windowData_.find(iNativeToken);
        if (it != windowData_.end() && !it->second.canvasSelector.empty()) {
            // Set canvas position via CSS
            EM_ASM_(
                {
                    var selector = UTF8ToString($0);
                    var canvas = document.querySelector(selector);
                    if (canvas) {
                        canvas.style.position = 'absolute';
                        canvas.style.left = $1 + 'px';
                        canvas.style.top = $2 + 'px';
                    }
                },
                it->second.canvasSelector.c_str(), iX, iY
            );
        }
#else
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (window) {
            glfwSetWindowPos(window, iX, iY);
        }
#endif
    }

    auto GlfwWindowBackend::GetWindowPosition(void* iNativeToken) const -> std::pair<int32_t, int32_t> {
#ifdef __EMSCRIPTEN__
        auto it = windowData_.find(iNativeToken);
        if (it != windowData_.end() && !it->second.canvasSelector.empty()) {
            int x = EM_ASM_INT(
                {
                    var selector = UTF8ToString($0);
                    var canvas = document.querySelector(selector);
                    if (canvas) {
                        var rect = canvas.getBoundingClientRect();
                        return Math.round(rect.left);
                    }
                    return 0;
                },
                it->second.canvasSelector.c_str()
            );
            int y = EM_ASM_INT(
                {
                    var selector = UTF8ToString($0);
                    var canvas = document.querySelector(selector);
                    if (canvas) {
                        var rect = canvas.getBoundingClientRect();
                        return Math.round(rect.top);
                    }
                    return 0;
                },
                it->second.canvasSelector.c_str()
            );
            return {x, y};
        }
        return {0, 0};
#else
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (!window) {
            return {0, 0};
        }
        int x = 0, y = 0;
        glfwGetWindowPos(window, &x, &y);
        return {x, y};
#endif
    }

    auto GlfwWindowBackend::MinimizeWindow(void* iNativeToken) -> void {
#ifdef __EMSCRIPTEN__
        (void)iNativeToken;
        // No-op on Emscripten — browsers have no minimize concept
#else
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (window) {
            glfwIconifyWindow(window);
        }
#endif
    }

    auto GlfwWindowBackend::MaximizeWindow(void* iNativeToken) -> void {
#ifdef __EMSCRIPTEN__
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (window) {
            // Request fullscreen on Emscripten (requires user gesture)
            emscripten::glfw3::RequestFullscreen(window, false, true);
        }
#else
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (window) {
            glfwMaximizeWindow(window);
        }
#endif
    }

    auto GlfwWindowBackend::RestoreWindow(void* iNativeToken) -> void {
#ifdef __EMSCRIPTEN__
        (void)iNativeToken;
        // Exit fullscreen if active
        emscripten_exit_fullscreen();
#else
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (window) {
            glfwRestoreWindow(window);
        }
#endif
    }

    auto GlfwWindowBackend::FocusWindow(void* iNativeToken) -> void {
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (window) {
            glfwFocusWindow(window);
        }
    }

    auto GlfwWindowBackend::SetWindowTitle(void* iNativeToken, std::string_view iTitle) -> void {
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (window) {
            std::string titleStr(iTitle);
            glfwSetWindowTitle(window, titleStr.c_str());
        }
    }

    // ===========================================================================
    // OpenGL-specific operations
    // ===========================================================================

    auto GlfwWindowBackend::GetGLProcLoader() const noexcept -> GLProcLoader {
        return reinterpret_cast<GLProcLoader>(glfwGetProcAddress);
    }

    auto GlfwWindowBackend::SwapBuffers(void* iNativeToken) -> void {
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (window) {
            glfwSwapBuffers(window);
        }
    }

    // ===========================================================================
    // Key mapping (absorbed from GlfwBridge)
    // ===========================================================================

    auto GlfwWindowBackend::MapKey(int iGlfwKey) noexcept -> neko::platform::Key {
        using enum neko::platform::Key;
        switch (iGlfwKey) {
            case GLFW_KEY_A: return A;
            case GLFW_KEY_B: return B;
            case GLFW_KEY_C: return C;
            case GLFW_KEY_D: return D;
            case GLFW_KEY_E: return E;
            case GLFW_KEY_F: return F;
            case GLFW_KEY_G: return G;
            case GLFW_KEY_H: return H;
            case GLFW_KEY_I: return I;
            case GLFW_KEY_J: return J;
            case GLFW_KEY_K: return K;
            case GLFW_KEY_L: return L;
            case GLFW_KEY_M: return M;
            case GLFW_KEY_N: return N;
            case GLFW_KEY_O: return O;
            case GLFW_KEY_P: return P;
            case GLFW_KEY_Q: return Q;
            case GLFW_KEY_R: return R;
            case GLFW_KEY_S: return S;
            case GLFW_KEY_T: return T;
            case GLFW_KEY_U: return U;
            case GLFW_KEY_V: return V;
            case GLFW_KEY_W: return W;
            case GLFW_KEY_X: return X;
            case GLFW_KEY_Y: return Y;
            case GLFW_KEY_Z: return Z;

            case GLFW_KEY_0: return Num0;
            case GLFW_KEY_1: return Num1;
            case GLFW_KEY_2: return Num2;
            case GLFW_KEY_3: return Num3;
            case GLFW_KEY_4: return Num4;
            case GLFW_KEY_5: return Num5;
            case GLFW_KEY_6: return Num6;
            case GLFW_KEY_7: return Num7;
            case GLFW_KEY_8: return Num8;
            case GLFW_KEY_9: return Num9;

            case GLFW_KEY_F1: return F1;
            case GLFW_KEY_F2: return F2;
            case GLFW_KEY_F3: return F3;
            case GLFW_KEY_F4: return F4;
            case GLFW_KEY_F5: return F5;
            case GLFW_KEY_F6: return F6;
            case GLFW_KEY_F7: return F7;
            case GLFW_KEY_F8: return F8;
            case GLFW_KEY_F9: return F9;
            case GLFW_KEY_F10: return F10;
            case GLFW_KEY_F11: return F11;
            case GLFW_KEY_F12: return F12;

            case GLFW_KEY_SPACE: return Space;
            case GLFW_KEY_ENTER: return Enter;
            case GLFW_KEY_ESCAPE: return Escape;
            case GLFW_KEY_TAB: return Tab;
            case GLFW_KEY_BACKSPACE: return Backspace;
            case GLFW_KEY_DELETE: return Delete;
            case GLFW_KEY_INSERT: return Insert;

            case GLFW_KEY_LEFT: return Left;
            case GLFW_KEY_RIGHT: return Right;
            case GLFW_KEY_UP: return Up;
            case GLFW_KEY_DOWN: return Down;

            case GLFW_KEY_HOME: return Home;
            case GLFW_KEY_END: return End;
            case GLFW_KEY_PAGE_UP: return PageUp;
            case GLFW_KEY_PAGE_DOWN: return PageDown;

            case GLFW_KEY_LEFT_SHIFT: return LeftShift;
            case GLFW_KEY_RIGHT_SHIFT: return RightShift;
            case GLFW_KEY_LEFT_CONTROL: return LeftControl;
            case GLFW_KEY_RIGHT_CONTROL: return RightControl;
            case GLFW_KEY_LEFT_ALT: return LeftAlt;
            case GLFW_KEY_RIGHT_ALT: return RightAlt;
            case GLFW_KEY_LEFT_SUPER: return LeftSuper;
            case GLFW_KEY_RIGHT_SUPER: return RightSuper;

            case GLFW_KEY_COMMA: return Comma;
            case GLFW_KEY_PERIOD: return Period;
            case GLFW_KEY_SLASH: return Slash;
            case GLFW_KEY_SEMICOLON: return Semicolon;
            case GLFW_KEY_APOSTROPHE: return Apostrophe;
            case GLFW_KEY_LEFT_BRACKET: return LeftBracket;
            case GLFW_KEY_RIGHT_BRACKET: return RightBracket;
            case GLFW_KEY_BACKSLASH: return Backslash;
            case GLFW_KEY_GRAVE_ACCENT: return GraveAccent;
            case GLFW_KEY_MINUS: return Minus;
            case GLFW_KEY_EQUAL: return Equal;

            case GLFW_KEY_KP_0: return KP0;
            case GLFW_KEY_KP_1: return KP1;
            case GLFW_KEY_KP_2: return KP2;
            case GLFW_KEY_KP_3: return KP3;
            case GLFW_KEY_KP_4: return KP4;
            case GLFW_KEY_KP_5: return KP5;
            case GLFW_KEY_KP_6: return KP6;
            case GLFW_KEY_KP_7: return KP7;
            case GLFW_KEY_KP_8: return KP8;
            case GLFW_KEY_KP_9: return KP9;
            case GLFW_KEY_KP_DECIMAL: return KPDecimal;
            case GLFW_KEY_KP_DIVIDE: return KPDivide;
            case GLFW_KEY_KP_MULTIPLY: return KPMultiply;
            case GLFW_KEY_KP_SUBTRACT: return KPSubtract;
            case GLFW_KEY_KP_ADD: return KPAdd;
            case GLFW_KEY_KP_ENTER: return KPEnter;
            case GLFW_KEY_KP_EQUAL: return KPEqual;

            case GLFW_KEY_CAPS_LOCK: return CapsLock;
            case GLFW_KEY_SCROLL_LOCK: return ScrollLock;
            case GLFW_KEY_NUM_LOCK: return NumLock;
            case GLFW_KEY_PRINT_SCREEN: return PrintScreen;
            case GLFW_KEY_PAUSE: return Pause;
            case GLFW_KEY_MENU: return Menu;

            default: return Unknown;
        }
    }

    auto GlfwWindowBackend::MapModifiers(int iGlfwMods) noexcept -> neko::platform::Modifiers {
        auto mods = neko::platform::Modifiers::None;
        if (iGlfwMods & GLFW_MOD_SHIFT) {
            mods = mods | neko::platform::Modifiers::Shift;
        }
        if (iGlfwMods & GLFW_MOD_CONTROL) {
            mods = mods | neko::platform::Modifiers::Control;
        }
        if (iGlfwMods & GLFW_MOD_ALT) {
            mods = mods | neko::platform::Modifiers::Alt;
        }
        if (iGlfwMods & GLFW_MOD_SUPER) {
            mods = mods | neko::platform::Modifiers::Super;
        }
        return mods;
    }

    auto GlfwWindowBackend::MapMouseButton(int iGlfwButton) noexcept -> neko::platform::MouseBtn {
        switch (iGlfwButton) {
            case GLFW_MOUSE_BUTTON_LEFT: return neko::platform::MouseBtn::Left;
            case GLFW_MOUSE_BUTTON_RIGHT: return neko::platform::MouseBtn::Right;
            case GLFW_MOUSE_BUTTON_MIDDLE: return neko::platform::MouseBtn::Middle;
            case GLFW_MOUSE_BUTTON_4: return neko::platform::MouseBtn::X1;
            case GLFW_MOUSE_BUTTON_5: return neko::platform::MouseBtn::X2;
            case GLFW_MOUSE_BUTTON_6: return neko::platform::MouseBtn::X3;
            case GLFW_MOUSE_BUTTON_7: return neko::platform::MouseBtn::X4;
            case GLFW_MOUSE_BUTTON_8: return neko::platform::MouseBtn::X5;
            default: return neko::platform::MouseBtn::Left;
        }
    }

    auto GlfwWindowBackend::MapAction(int iGlfwAction) noexcept -> neko::platform::Action {
        switch (iGlfwAction) {
            case GLFW_PRESS: return neko::platform::Action::Press;
            case GLFW_RELEASE: return neko::platform::Action::Release;
            case GLFW_REPEAT: return neko::platform::Action::Repeat;
            default: return neko::platform::Action::Press;
        }
    }

    // ===========================================================================
    // GLFW callback trampolines (absorbed from GlfwBridge)
    // ===========================================================================

    auto GlfwWindowBackend::KeyCallback(GLFWwindow* w, int key, int scancode, int action, int mods) -> void {
        auto* data = static_cast<PerWindowData*>(glfwGetWindowUserPointer(w));
        if (!data || !data->owner) {
            return;
        }

        neko::platform::Event evt;
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            evt = neko::platform::KeyDown{
                .key = MapKey(key), .scancode = static_cast<uint32_t>(scancode), .mods = MapModifiers(mods)
            };
        } else {
            evt = neko::platform::KeyUp{
                .key = MapKey(key), .scancode = static_cast<uint32_t>(scancode), .mods = MapModifiers(mods)
            };
        }
        data->owner->pendingEvents_.push_back({data->handle, evt});
    }

    auto GlfwWindowBackend::MouseButtonCallback(GLFWwindow* w, int button, int action, int mods) -> void {
        auto* data = static_cast<PerWindowData*>(glfwGetWindowUserPointer(w));
        if (!data || !data->owner) {
            return;
        }

        data->owner->pendingEvents_.push_back(
            {data->handle, neko::platform::MouseButton{
                               .button = MapMouseButton(button), .action = MapAction(action), .mods = MapModifiers(mods)
                           }}
        );
    }

    auto GlfwWindowBackend::CursorPosCallback(GLFWwindow* w, double x, double y) -> void {
        auto* data = static_cast<PerWindowData*>(glfwGetWindowUserPointer(w));
        if (!data || !data->owner) {
            return;
        }

        data->owner->pendingEvents_.push_back(
            {data->handle,
             neko::platform::MouseMove{.x = x, .y = y, .dx = x - data->lastMouseX, .dy = y - data->lastMouseY}}
        );
        data->lastMouseX = x;
        data->lastMouseY = y;
    }

    auto GlfwWindowBackend::ScrollCallback(GLFWwindow* w, double xoff, double yoff) -> void {
        auto* data = static_cast<PerWindowData*>(glfwGetWindowUserPointer(w));
        if (!data || !data->owner) {
            return;
        }

        data->owner->pendingEvents_.push_back({data->handle, neko::platform::Scroll{.dx = xoff, .dy = yoff}});
    }

    auto GlfwWindowBackend::FramebufferSizeCallback(GLFWwindow* w, int width, int height) -> void {
        auto* data = static_cast<PerWindowData*>(glfwGetWindowUserPointer(w));
        if (!data || !data->owner) {
            return;
        }

        data->owner->pendingEvents_.push_back(
            {data->handle,
             neko::platform::Resize{.width = static_cast<uint32_t>(width), .height = static_cast<uint32_t>(height)}}
        );
    }

    auto GlfwWindowBackend::CharCallback(GLFWwindow* w, unsigned int codepoint) -> void {
        auto* data = static_cast<PerWindowData*>(glfwGetWindowUserPointer(w));
        if (!data || !data->owner) {
            return;
        }

        data->owner->pendingEvents_.push_back(
            {data->handle, neko::platform::TextInput{.codepoint = static_cast<char32_t>(codepoint)}}
        );
    }

    auto GlfwWindowBackend::WindowFocusCallback(GLFWwindow* w, int focused) -> void {
        auto* data = static_cast<PerWindowData*>(glfwGetWindowUserPointer(w));
        if (!data || !data->owner) {
            return;
        }

        data->owner->pendingEvents_.push_back({data->handle, neko::platform::Focus{.focused = (focused != 0)}});
    }

    auto GlfwWindowBackend::WindowCloseCallback(GLFWwindow* w) -> void {
        auto* data = static_cast<PerWindowData*>(glfwGetWindowUserPointer(w));
        if (!data || !data->owner) {
            return;
        }

        data->owner->pendingEvents_.push_back({data->handle, neko::platform::CloseRequested{}});
    }

    auto GlfwWindowBackend::WindowIconifyCallback(GLFWwindow* w, int iconified) -> void {
        auto* data = static_cast<PerWindowData*>(glfwGetWindowUserPointer(w));
        if (!data || !data->owner) {
            return;
        }

        if (iconified) {
            data->owner->pendingEvents_.push_back({data->handle, neko::platform::Minimized{}});
        } else {
            data->owner->pendingEvents_.push_back({data->handle, neko::platform::Restored{}});
        }
    }

    auto GlfwWindowBackend::WindowMaximizeCallback(GLFWwindow* w, int maximized) -> void {
        auto* data = static_cast<PerWindowData*>(glfwGetWindowUserPointer(w));
        if (!data || !data->owner) {
            return;
        }

        if (!maximized) {
            data->owner->pendingEvents_.push_back({data->handle, neko::platform::Restored{}});
        }
        // Maximize itself doesn't get a special event — it's detected via Resize.
        // The WindowManager tracks maximized state internally via MaximizeWindow().
    }

    // ===========================================================================
    // Gamepad / Joystick
    // ===========================================================================

    auto GlfwWindowBackend::JoystickCallback(int jid, int event) -> void {
        if (!sGamepadOwner || jid < 0 || jid >= neko::platform::kMaxGamepads) {
            return;
        }
        auto id = static_cast<uint8_t>(jid);
        auto& slot = sGamepadOwner->gamepadSlots_[id];

        if (event == GLFW_CONNECTED) {
            slot.connected = true;
            slot.isGamepad = (glfwJoystickIsGamepad(jid) == GLFW_TRUE);
            slot.prevState = {};
            const char* name = slot.isGamepad ? glfwGetGamepadName(jid) : glfwGetJoystickName(jid);
            sGamepadOwner->pendingGamepadEvents_.push_back(
                {id, neko::platform::GamepadConnected{.name = name ? name : "", .isGamepad = slot.isGamepad}}
            );
        } else if (event == GLFW_DISCONNECTED) {
            slot.connected = false;
            slot.isGamepad = false;
            slot.prevState = {};
            sGamepadOwner->pendingGamepadEvents_.push_back({id, neko::platform::GamepadDisconnected{}});
        }
    }

    auto GlfwWindowBackend::PollGamepadEvents(std::vector<neko::platform::GamepadEvent>& ioEvents) -> void {
        // Flush connection/disconnection events from callback
        ioEvents.append_range(pendingGamepadEvents_);
        pendingGamepadEvents_.clear();

        // Diff-poll each connected gamepad for button/axis changes
        for (uint8_t id = 0; id < neko::platform::kMaxGamepads; ++id) {
            auto& slot = gamepadSlots_[id];
            if (!slot.connected) {
                continue;
            }

            neko::platform::GamepadState current = {};

            if (slot.isGamepad) {
                // Use GLFW gamepad mapping (SDL layout)
                GLFWgamepadstate gs = {};
                if (glfwGetGamepadState(static_cast<int>(id), &gs) != GLFW_TRUE) {
                    continue;
                }
                for (uint8_t b = 0; b < neko::platform::kMaxGamepadButtons && b <= GLFW_GAMEPAD_BUTTON_LAST; ++b) {
                    current.buttons[b] = (gs.buttons[b] == GLFW_PRESS);
                }
                for (uint8_t a = 0; a < neko::platform::kMaxGamepadAxes && a <= GLFW_GAMEPAD_AXIS_LAST; ++a) {
                    current.axes[a] = gs.axes[a];
                }
            } else {
                // Raw joystick — map axes/buttons directly
                int axisCount = 0;
                const float* axes = glfwGetJoystickAxes(static_cast<int>(id), &axisCount);
                for (int a = 0; a < axisCount && a < neko::platform::kMaxGamepadAxes; ++a) {
                    current.axes[static_cast<uint8_t>(a)] = axes[a];
                }
                int btnCount = 0;
                const unsigned char* btns = glfwGetJoystickButtons(static_cast<int>(id), &btnCount);
                for (int b = 0; b < btnCount && b < neko::platform::kMaxGamepadButtons; ++b) {
                    current.buttons[static_cast<uint8_t>(b)] = (btns[b] == GLFW_PRESS);
                }
            }

            // Emit button change events
            for (uint8_t b = 0; b < neko::platform::kMaxGamepadButtons; ++b) {
                if (current.buttons[b] != slot.prevState.buttons[b]) {
                    ioEvents.push_back(
                        {id, neko::platform::GamepadButtonEvent{
                                 .button = static_cast<neko::platform::GamepadButton>(b),
                                 .action
                                 = current.buttons[b] ? neko::platform::Action::Press : neko::platform::Action::Release,
                             }}
                    );
                }
            }

            // Emit axis change events (with deadzone filtering)
            for (uint8_t a = 0; a < neko::platform::kMaxGamepadAxes; ++a) {
                float delta = current.axes[a] - slot.prevState.axes[a];
                if (delta > kAxisDeadzone || delta < -kAxisDeadzone) {
                    ioEvents.push_back(
                        {id, neko::platform::GamepadAxisEvent{
                                 .axis = static_cast<neko::platform::GamepadAxis>(a),
                                 .value = current.axes[a],
                             }}
                    );
                }
            }

            slot.prevState = current;
        }
    }

    auto GlfwWindowBackend::GetGamepadState(uint8_t iGamepadId, neko::platform::GamepadState& oState) const -> bool {
        if (iGamepadId >= neko::platform::kMaxGamepads || !gamepadSlots_[iGamepadId].connected) {
            return false;
        }

        if (gamepadSlots_[iGamepadId].isGamepad) {
            GLFWgamepadstate gs = {};
            if (glfwGetGamepadState(static_cast<int>(iGamepadId), &gs) != GLFW_TRUE) {
                return false;
            }
            for (uint8_t b = 0; b < neko::platform::kMaxGamepadButtons && b <= GLFW_GAMEPAD_BUTTON_LAST; ++b) {
                oState.buttons[b] = (gs.buttons[b] == GLFW_PRESS);
            }
            for (uint8_t a = 0; a < neko::platform::kMaxGamepadAxes && a <= GLFW_GAMEPAD_AXIS_LAST; ++a) {
                oState.axes[a] = gs.axes[a];
            }
        } else {
            int axisCount = 0;
            const float* axes = glfwGetJoystickAxes(static_cast<int>(iGamepadId), &axisCount);
            oState.axes = {};
            for (int a = 0; a < axisCount && a < neko::platform::kMaxGamepadAxes; ++a) {
                oState.axes[static_cast<uint8_t>(a)] = axes[a];
            }
            int btnCount = 0;
            const unsigned char* btns = glfwGetJoystickButtons(static_cast<int>(iGamepadId), &btnCount);
            oState.buttons = {};
            for (int b = 0; b < btnCount && b < neko::platform::kMaxGamepadButtons; ++b) {
                oState.buttons[static_cast<uint8_t>(b)] = (btns[b] == GLFW_PRESS);
            }
        }
        return true;
    }

    auto GlfwWindowBackend::IsGamepadConnected(uint8_t iGamepadId) const -> bool {
        if (iGamepadId >= neko::platform::kMaxGamepads) {
            return false;
        }
        return gamepadSlots_[iGamepadId].connected;
    }

    auto GlfwWindowBackend::GetGamepadName(uint8_t iGamepadId) const -> std::string_view {
        if (iGamepadId >= neko::platform::kMaxGamepads || !gamepadSlots_[iGamepadId].connected) {
            return {};
        }
        const char* name = gamepadSlots_[iGamepadId].isGamepad ? glfwGetGamepadName(static_cast<int>(iGamepadId))
                                                               : glfwGetJoystickName(static_cast<int>(iGamepadId));
        return name ? std::string_view{name} : std::string_view{};
    }

}  // namespace miki::platform
