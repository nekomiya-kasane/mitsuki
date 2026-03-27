/** @brief GlfwWindowBackend implementation.
 *
 * Absorbs GlfwBridge's callback logic: installs GLFW callbacks per-window,
 * converts to neko::platform::Event tagged with WindowHandle.
 */

#include "GlfwWindowBackend.h"

#include "miki/core/ErrorCode.h"

#include <string>
#include <atomic>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef _WIN32
#    define GLFW_EXPOSE_NATIVE_WIN32
#    include <GLFW/glfw3native.h>
#endif

#include <atomic>

namespace miki::demo {

    // ---------------------------------------------------------------------------
    // GLFW ref-counted init
    // ---------------------------------------------------------------------------

    static std::atomic<int> sGlfwRefCount{0};

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
        : backendType_(iBackendType), visible_(iVisible) {}

    GlfwWindowBackend::~GlfwWindowBackend() {
        for (auto& [token, data] : windowData_) {
            auto* window = static_cast<GLFWwindow*>(token);
            glfwSetWindowUserPointer(window, nullptr);
            glfwDestroyWindow(window);
        }
        windowData_.clear();

        if (sGlfwRefCount.fetch_sub(1) == 1) {
            glfwTerminate();
        }
    }

    // ---------------------------------------------------------------------------
    // IWindowBackend — CreateNativeWindow
    // ---------------------------------------------------------------------------

    auto GlfwWindowBackend::CreateNativeWindow(const miki::platform::WindowDesc& iDesc, void*& oNativeToken)
        -> miki::core::Result<miki::rhi::NativeWindowHandle> {
        if (!InitGlfw()) {
            return std::unexpected(miki::core::ErrorCode::DeviceNotReady);
        }

        glfwDefaultWindowHints();

        if (!visible_) {
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        }

        GLFWwindow* shareCtx = nullptr;

        if (backendType_ == miki::rhi::BackendType::OpenGL) {
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            shareCtx = glShareContext_;
        } else {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        }

        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        std::string titleStr(iDesc.title);
        auto* window = glfwCreateWindow(
            static_cast<int>(iDesc.width), static_cast<int>(iDesc.height), titleStr.c_str(), nullptr, shareCtx
        );

        if (!window) {
            return std::unexpected(miki::core::ErrorCode::DeviceNotReady);
        }

        if (backendType_ == miki::rhi::BackendType::OpenGL && !glShareContext_) {
            glShareContext_ = window;
        }

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

#ifdef _WIN32
        HWND hwnd = glfwGetWin32Window(window);
        return miki::rhi::NativeWindowHandle{miki::rhi::Win32Window{.hwnd = static_cast<void*>(hwnd)}};
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

        if (window == glShareContext_) {
            glShareContext_ = nullptr;
        }

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
        ioEvents.insert(ioEvents.end(), pendingEvents_.begin(), pendingEvents_.end());
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
        return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
    }

    auto GlfwWindowBackend::IsMinimized(void* iNativeToken) -> bool {
        auto* window = static_cast<GLFWwindow*>(iNativeToken);
        if (!window) {
            return true;
        }
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        return w == 0 || h == 0;
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

}  // namespace miki::demo
