/** @brief GLFW EventSimulator implementation.
 *
 * Provides two simulators:
 * 1. GlfwEventSimulator - Injects events into the backend's pending event queue
 * 2. GlfwPhysicalEventSimulator - Uses OS-level input APIs (SendInput on Win32)
 */

#include "miki/platform/EventSimulator.h"
#include "miki/platform/glfw/GlfwWindowBackend.h"

#include <vector>
#include <thread>
#include <chrono>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    include <windows.h>
#endif

#ifdef __EMSCRIPTEN__
#    include <emscripten/emscripten.h>
#endif

namespace miki::platform {

    // =========================================================================
    // GlfwEventSimulator — Event queue injection
    // =========================================================================

    class GlfwEventSimulator : public IEventSimulator {
       public:
        explicit GlfwEventSimulator(std::vector<WindowEvent>* iPendingEvents) : pendingEvents_(iPendingEvents) {}

        auto InjectKeyDown(WindowHandle iWindow, neko::platform::Key iKey, neko::platform::Modifiers iMods)
            -> void override {
            if (pendingEvents_) {
                pendingEvents_->push_back(
                    {iWindow, neko::platform::KeyDown{.key = iKey, .scancode = 0, .mods = iMods}}
                );
            }
        }

        auto InjectKeyUp(WindowHandle iWindow, neko::platform::Key iKey, neko::platform::Modifiers iMods)
            -> void override {
            if (pendingEvents_) {
                pendingEvents_->push_back({iWindow, neko::platform::KeyUp{.key = iKey, .scancode = 0, .mods = iMods}});
            }
        }

        auto InjectTextInput(WindowHandle iWindow, char32_t iCodepoint) -> void override {
            if (pendingEvents_) {
                pendingEvents_->push_back({iWindow, neko::platform::TextInput{.codepoint = iCodepoint}});
            }
        }

        auto InjectMouseMove(WindowHandle iWindow, double iX, double iY) -> void override {
            if (pendingEvents_) {
                double dx = iX - lastMouseX_;
                double dy = iY - lastMouseY_;
                pendingEvents_->push_back({iWindow, neko::platform::MouseMove{.x = iX, .y = iY, .dx = dx, .dy = dy}});
                lastMouseX_ = iX;
                lastMouseY_ = iY;
            }
        }

        auto InjectMouseDown(
            WindowHandle iWindow, neko::platform::MouseBtn iButton, double iX, double iY,
            neko::platform::Modifiers iMods
        ) -> void override {
            if (pendingEvents_) {
                // First inject a move to the position
                InjectMouseMove(iWindow, iX, iY);
                pendingEvents_->push_back(
                    {iWindow, neko::platform::MouseButton{
                                  .button = iButton, .action = neko::platform::Action::Press, .mods = iMods
                              }}
                );
            }
        }

        auto InjectMouseUp(
            WindowHandle iWindow, neko::platform::MouseBtn iButton, double iX, double iY,
            neko::platform::Modifiers iMods
        ) -> void override {
            if (pendingEvents_) {
                InjectMouseMove(iWindow, iX, iY);
                pendingEvents_->push_back(
                    {iWindow, neko::platform::MouseButton{
                                  .button = iButton, .action = neko::platform::Action::Release, .mods = iMods
                              }}
                );
            }
        }

        auto InjectScroll(WindowHandle iWindow, double iDeltaX, double iDeltaY) -> void override {
            if (pendingEvents_) {
                pendingEvents_->push_back({iWindow, neko::platform::Scroll{.dx = iDeltaX, .dy = iDeltaY}});
            }
        }

        auto InjectFocus(WindowHandle iWindow, bool iFocused) -> void override {
            if (pendingEvents_) {
                pendingEvents_->push_back({iWindow, neko::platform::Focus{.focused = iFocused}});
            }
        }

        auto InjectResize(WindowHandle iWindow, uint32_t iWidth, uint32_t iHeight) -> void override {
            if (pendingEvents_) {
                pendingEvents_->push_back({iWindow, neko::platform::Resize{.width = iWidth, .height = iHeight}});
            }
        }

        auto InjectCloseRequested(WindowHandle iWindow) -> void override {
            if (pendingEvents_) {
                pendingEvents_->push_back({iWindow, neko::platform::CloseRequested{}});
            }
        }

        auto InjectEvent(WindowHandle iWindow, const neko::platform::Event& iEvent) -> void override {
            if (pendingEvents_) {
                pendingEvents_->push_back({iWindow, iEvent});
            }
        }

       private:
        std::vector<WindowEvent>* pendingEvents_ = nullptr;
        double lastMouseX_ = 0.0;
        double lastMouseY_ = 0.0;
    };

    // =========================================================================
    // GlfwPhysicalEventSimulator — OS-level input simulation
    // =========================================================================

#ifdef _WIN32

    class Win32PhysicalEventSimulator : public IPhysicalEventSimulator {
       public:
        auto MoveTo(int32_t iX, int32_t iY) -> void override {
            // Convert to normalized coordinates (0-65535)
            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int screenHeight = GetSystemMetrics(SM_CYSCREEN);
            LONG nx = static_cast<LONG>((iX * 65535) / screenWidth);
            LONG ny = static_cast<LONG>((iY * 65535) / screenHeight);

            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dx = nx;
            input.mi.dy = ny;
            input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
            SendInput(1, &input, sizeof(INPUT));

            mouseX_ = iX;
            mouseY_ = iY;
        }

        auto MoveBy(int32_t iDx, int32_t iDy) -> void override {
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dx = iDx;
            input.mi.dy = iDy;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            SendInput(1, &input, sizeof(INPUT));

            mouseX_ += iDx;
            mouseY_ += iDy;
        }

        [[nodiscard]] auto GetMousePosition() const -> std::pair<int32_t, int32_t> override {
            POINT pt;
            if (GetCursorPos(&pt)) {
                return {pt.x, pt.y};
            }
            return {mouseX_, mouseY_};
        }

        auto MouseDown(neko::platform::MouseBtn iButton) -> void override {
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MouseButtonToDownFlag(iButton);
            if (iButton == neko::platform::MouseBtn::X1 || iButton == neko::platform::MouseBtn::X2) {
                input.mi.mouseData = (iButton == neko::platform::MouseBtn::X1) ? XBUTTON1 : XBUTTON2;
            }
            SendInput(1, &input, sizeof(INPUT));
        }

        auto MouseUp(neko::platform::MouseBtn iButton) -> void override {
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MouseButtonToUpFlag(iButton);
            if (iButton == neko::platform::MouseBtn::X1 || iButton == neko::platform::MouseBtn::X2) {
                input.mi.mouseData = (iButton == neko::platform::MouseBtn::X1) ? XBUTTON1 : XBUTTON2;
            }
            SendInput(1, &input, sizeof(INPUT));
        }

        auto Scroll(int32_t iDeltaX, int32_t iDeltaY) -> void override {
            if (iDeltaY != 0) {
                INPUT input = {};
                input.type = INPUT_MOUSE;
                input.mi.dwFlags = MOUSEEVENTF_WHEEL;
                input.mi.mouseData = static_cast<DWORD>(iDeltaY * WHEEL_DELTA);
                SendInput(1, &input, sizeof(INPUT));
            }
            if (iDeltaX != 0) {
                INPUT input = {};
                input.type = INPUT_MOUSE;
                input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
                input.mi.mouseData = static_cast<DWORD>(iDeltaX * WHEEL_DELTA);
                SendInput(1, &input, sizeof(INPUT));
            }
        }

        auto KeyDown(neko::platform::Key iKey) -> void override {
            INPUT input = {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = static_cast<WORD>(KeyToVK(iKey));
            SendInput(1, &input, sizeof(INPUT));
        }

        auto KeyUp(neko::platform::Key iKey) -> void override {
            INPUT input = {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = static_cast<WORD>(KeyToVK(iKey));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }

        auto TypeText(std::string_view iText) -> void override {
            for (char c : iText) {
                INPUT inputs[2] = {};
                inputs[0].type = INPUT_KEYBOARD;
                inputs[0].ki.wScan = static_cast<WORD>(c);
                inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
                inputs[1].type = INPUT_KEYBOARD;
                inputs[1].ki.wScan = static_cast<WORD>(c);
                inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                SendInput(2, inputs, sizeof(INPUT));
            }
        }

        [[nodiscard]] auto IsKeyPressed(neko::platform::Key iKey) const -> bool override {
            return (GetAsyncKeyState(KeyToVK(iKey)) & 0x8000) != 0;
        }

        [[nodiscard]] auto IsMouseButtonPressed(neko::platform::MouseBtn iButton) const -> bool override {
            int vk = 0;
            switch (iButton) {
                case neko::platform::MouseBtn::Left: vk = VK_LBUTTON; break;
                case neko::platform::MouseBtn::Right: vk = VK_RBUTTON; break;
                case neko::platform::MouseBtn::Middle: vk = VK_MBUTTON; break;
                case neko::platform::MouseBtn::X1: vk = VK_XBUTTON1; break;
                case neko::platform::MouseBtn::X2: vk = VK_XBUTTON2; break;
            }
            return (GetAsyncKeyState(vk) & 0x8000) != 0;
        }

       private:
        int32_t mouseX_ = 0;
        int32_t mouseY_ = 0;

        static auto MouseButtonToDownFlag(neko::platform::MouseBtn iButton) -> DWORD {
            switch (iButton) {
                case neko::platform::MouseBtn::Left: return MOUSEEVENTF_LEFTDOWN;
                case neko::platform::MouseBtn::Right: return MOUSEEVENTF_RIGHTDOWN;
                case neko::platform::MouseBtn::Middle: return MOUSEEVENTF_MIDDLEDOWN;
                case neko::platform::MouseBtn::X1:
                case neko::platform::MouseBtn::X2: return MOUSEEVENTF_XDOWN;
            }
            return MOUSEEVENTF_LEFTDOWN;
        }

        static auto MouseButtonToUpFlag(neko::platform::MouseBtn iButton) -> DWORD {
            switch (iButton) {
                case neko::platform::MouseBtn::Left: return MOUSEEVENTF_LEFTUP;
                case neko::platform::MouseBtn::Right: return MOUSEEVENTF_RIGHTUP;
                case neko::platform::MouseBtn::Middle: return MOUSEEVENTF_MIDDLEUP;
                case neko::platform::MouseBtn::X1:
                case neko::platform::MouseBtn::X2: return MOUSEEVENTF_XUP;
            }
            return MOUSEEVENTF_LEFTUP;
        }

        static auto KeyToVK(neko::platform::Key iKey) -> int {
            using enum neko::platform::Key;
            switch (iKey) {
                case A: return 'A';
                case B: return 'B';
                case C: return 'C';
                case D: return 'D';
                case E: return 'E';
                case F: return 'F';
                case G: return 'G';
                case H: return 'H';
                case I: return 'I';
                case J: return 'J';
                case K: return 'K';
                case L: return 'L';
                case M: return 'M';
                case N: return 'N';
                case O: return 'O';
                case P: return 'P';
                case Q: return 'Q';
                case R: return 'R';
                case S: return 'S';
                case T: return 'T';
                case U: return 'U';
                case V: return 'V';
                case W: return 'W';
                case X: return 'X';
                case Y: return 'Y';
                case Z: return 'Z';
                case Num0: return '0';
                case Num1: return '1';
                case Num2: return '2';
                case Num3: return '3';
                case Num4: return '4';
                case Num5: return '5';
                case Num6: return '6';
                case Num7: return '7';
                case Num8: return '8';
                case Num9: return '9';
                case Space: return VK_SPACE;
                case Enter: return VK_RETURN;
                case Escape: return VK_ESCAPE;
                case Tab: return VK_TAB;
                case Backspace: return VK_BACK;
                case Delete: return VK_DELETE;
                case Insert: return VK_INSERT;
                case Left: return VK_LEFT;
                case Right: return VK_RIGHT;
                case Up: return VK_UP;
                case Down: return VK_DOWN;
                case Home: return VK_HOME;
                case End: return VK_END;
                case PageUp: return VK_PRIOR;
                case PageDown: return VK_NEXT;
                case LeftShift:
                case RightShift: return VK_SHIFT;
                case LeftControl:
                case RightControl: return VK_CONTROL;
                case LeftAlt:
                case RightAlt: return VK_MENU;
                case LeftSuper:
                case RightSuper: return VK_LWIN;
                case F1: return VK_F1;
                case F2: return VK_F2;
                case F3: return VK_F3;
                case F4: return VK_F4;
                case F5: return VK_F5;
                case F6: return VK_F6;
                case F7: return VK_F7;
                case F8: return VK_F8;
                case F9: return VK_F9;
                case F10: return VK_F10;
                case F11: return VK_F11;
                case F12: return VK_F12;
                default: return 0;
            }
        }
    };

#endif  // _WIN32

#ifdef __EMSCRIPTEN__

    class EmscriptenPhysicalEventSimulator : public IPhysicalEventSimulator {
       public:
        auto MoveTo(int32_t iX, int32_t iY) -> void override {
            mouseX_ = iX;
            mouseY_ = iY;
            EM_ASM_(
                {
                    var event = new MouseEvent('mousemove', {clientX : $0, clientY : $1, bubbles : true});
                    document.dispatchEvent(event);
                },
                iX, iY
            );
        }

        auto MoveBy(int32_t iDx, int32_t iDy) -> void override { MoveTo(mouseX_ + iDx, mouseY_ + iDy); }

        [[nodiscard]] auto GetMousePosition() const -> std::pair<int32_t, int32_t> override {
            return {mouseX_, mouseY_};
        }

        auto MouseDown(neko::platform::MouseBtn iButton) -> void override {
            int button = MouseBtnToJS(iButton);
            EM_ASM_(
                {
                    var event = new MouseEvent('mousedown', {button : $0, clientX : $1, clientY : $2, bubbles : true});
                    document.dispatchEvent(event);
                },
                button, mouseX_, mouseY_
            );
        }

        auto MouseUp(neko::platform::MouseBtn iButton) -> void override {
            int button = MouseBtnToJS(iButton);
            EM_ASM_(
                {
                    var event = new MouseEvent('mouseup', {button : $0, clientX : $1, clientY : $2, bubbles : true});
                    document.dispatchEvent(event);
                },
                button, mouseX_, mouseY_
            );
        }

        auto Scroll(int32_t iDeltaX, int32_t iDeltaY) -> void override {
            EM_ASM_(
                {
                    var event = new WheelEvent('wheel', {deltaX : $0, deltaY : $1, bubbles : true});
                    document.dispatchEvent(event);
                },
                iDeltaX, iDeltaY
            );
        }

        auto KeyDown(neko::platform::Key iKey) -> void override {
            const char* code = KeyToJSCode(iKey);
            EM_ASM_(
                {
                    var code = UTF8ToString($0);
                    var event = new KeyboardEvent('keydown', {code : code, bubbles : true});
                    document.dispatchEvent(event);
                },
                code
            );
        }

        auto KeyUp(neko::platform::Key iKey) -> void override {
            const char* code = KeyToJSCode(iKey);
            EM_ASM_(
                {
                    var code = UTF8ToString($0);
                    var event = new KeyboardEvent('keyup', {code : code, bubbles : true});
                    document.dispatchEvent(event);
                },
                code
            );
        }

        auto TypeText(std::string_view iText) -> void override {
            for (char c : iText) {
                EM_ASM_(
                    {
                        var char = String.fromCharCode($0);
                        var event = new KeyboardEvent('keypress', {key : char, bubbles : true});
                        document.dispatchEvent(event);
                    },
                    static_cast<int>(c)
                );
            }
        }

        [[nodiscard]] auto IsKeyPressed(neko::platform::Key iKey) const -> bool override {
            (void)iKey;
            return false;  // Cannot query key state in browser
        }

        [[nodiscard]] auto IsMouseButtonPressed(neko::platform::MouseBtn iButton) const -> bool override {
            (void)iButton;
            return false;  // Cannot query button state in browser
        }

       private:
        int32_t mouseX_ = 0;
        int32_t mouseY_ = 0;

        static auto MouseBtnToJS(neko::platform::MouseBtn iButton) -> int {
            switch (iButton) {
                case neko::platform::MouseBtn::Left: return 0;
                case neko::platform::MouseBtn::Middle: return 1;
                case neko::platform::MouseBtn::Right: return 2;
                case neko::platform::MouseBtn::X1: return 3;
                case neko::platform::MouseBtn::X2: return 4;
            }
            return 0;
        }

        static auto KeyToJSCode(neko::platform::Key iKey) -> const char* {
            using enum neko::platform::Key;
            switch (iKey) {
                case A: return "KeyA";
                case B: return "KeyB";
                case C: return "KeyC";
                case D: return "KeyD";
                case E: return "KeyE";
                case F: return "KeyF";
                case G: return "KeyG";
                case H: return "KeyH";
                case I: return "KeyI";
                case J: return "KeyJ";
                case K: return "KeyK";
                case L: return "KeyL";
                case M: return "KeyM";
                case N: return "KeyN";
                case O: return "KeyO";
                case P: return "KeyP";
                case Q: return "KeyQ";
                case R: return "KeyR";
                case S: return "KeyS";
                case T: return "KeyT";
                case U: return "KeyU";
                case V: return "KeyV";
                case W: return "KeyW";
                case X: return "KeyX";
                case Y: return "KeyY";
                case Z: return "KeyZ";
                case Space: return "Space";
                case Enter: return "Enter";
                case Escape: return "Escape";
                case Tab: return "Tab";
                case Left: return "ArrowLeft";
                case Right: return "ArrowRight";
                case Up: return "ArrowUp";
                case Down: return "ArrowDown";
                default: return "";
            }
        }
    };

#endif  // __EMSCRIPTEN__

    // =========================================================================
    // Factory functions
    // =========================================================================

    auto CreateEventSimulator(IWindowBackend* iBackend) -> EventSimulatorPtr {
        if (auto* glfwBackend = dynamic_cast<GlfwWindowBackend*>(iBackend)) {
            return std::make_unique<GlfwEventSimulator>(&glfwBackend->GetPendingEvents());
        }
        // Fallback: return simulator with no backend (events won't be delivered)
        return std::make_unique<GlfwEventSimulator>(nullptr);
    }

    auto CreatePhysicalEventSimulator() -> PhysicalEventSimulatorPtr {
#ifdef _WIN32
        return std::make_unique<Win32PhysicalEventSimulator>();
#elif defined(__EMSCRIPTEN__)
        return std::make_unique<EmscriptenPhysicalEventSimulator>();
#else
        return nullptr;  // Not implemented for this platform
#endif
    }

}  // namespace miki::platform
