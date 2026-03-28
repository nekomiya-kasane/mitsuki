/** @brief EventSimulator — Input event simulation for automated testing.
 *
 * Provides two simulation modes:
 * 1. **Injection Mode** (IEventSimulator): Injects events directly into the event queue.
 *    - Fast, deterministic, no OS interaction
 *    - Events appear in PollEvents() on next call
 *    - Ideal for unit tests and headless CI
 *
 * 2. **Physical Mode** (IPhysicalEventSimulator): Simulates OS-level input.
 *    - Uses platform APIs (SendInput on Win32, XTest on X11, etc.)
 *    - Events go through full OS input pipeline
 *    - Required for testing OS-level behavior (focus, window activation)
 *
 * Usage:
 *   auto& sim = windowManager.GetEventSimulator();
 *   sim.InjectKeyDown(handle, Key::A, Modifiers::None);
 *   sim.InjectMouseClick(handle, MouseBtn::Left, 100, 200);
 *   auto events = windowManager.PollEvents();  // Contains injected events
 *
 * Namespace: miki::platform
 */
#pragma once

#include "miki/platform/Event.h"
#include "miki/platform/WindowManager.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>

namespace miki::platform {

    // =========================================================================
    // IEventSimulator — Event queue injection (fast, deterministic)
    // =========================================================================

    /** @brief Interface for injecting events into the window event queue.
     *
     *  Injected events are collected by the backend and returned by PollEvents().
     *  This is the primary interface for automated UI testing.
     */
    class IEventSimulator {
       public:
        virtual ~IEventSimulator() = default;

        // ── Keyboard events ─────────────────────────────────────────────────

        /** @brief Inject a key down event. */
        virtual auto InjectKeyDown(
            WindowHandle iWindow, neko::platform::Key iKey,
            neko::platform::Modifiers iMods = neko::platform::Modifiers::None
        ) -> void
            = 0;

        /** @brief Inject a key up event. */
        virtual auto InjectKeyUp(
            WindowHandle iWindow, neko::platform::Key iKey,
            neko::platform::Modifiers iMods = neko::platform::Modifiers::None
        ) -> void
            = 0;

        /** @brief Inject a key press (down + up). */
        virtual auto InjectKeyPress(
            WindowHandle iWindow, neko::platform::Key iKey,
            neko::platform::Modifiers iMods = neko::platform::Modifiers::None
        ) -> void {
            InjectKeyDown(iWindow, iKey, iMods);
            InjectKeyUp(iWindow, iKey, iMods);
        }

        /** @brief Inject a text input event (Unicode codepoint). */
        virtual auto InjectTextInput(WindowHandle iWindow, char32_t iCodepoint) -> void = 0;

        /** @brief Inject a sequence of text input events. */
        virtual auto InjectText(WindowHandle iWindow, std::string_view iText) -> void;

        // ── Mouse events ────────────────────────────────────────────────────

        /** @brief Inject a mouse move event. */
        virtual auto InjectMouseMove(WindowHandle iWindow, double iX, double iY) -> void = 0;

        /** @brief Inject a mouse button down event. */
        virtual auto InjectMouseDown(
            WindowHandle iWindow, neko::platform::MouseBtn iButton, double iX, double iY,
            neko::platform::Modifiers iMods = neko::platform::Modifiers::None
        ) -> void
            = 0;

        /** @brief Inject a mouse button up event. */
        virtual auto InjectMouseUp(
            WindowHandle iWindow, neko::platform::MouseBtn iButton, double iX, double iY,
            neko::platform::Modifiers iMods = neko::platform::Modifiers::None
        ) -> void
            = 0;

        /** @brief Inject a mouse click (down + up). */
        virtual auto InjectMouseClick(
            WindowHandle iWindow, neko::platform::MouseBtn iButton, double iX, double iY,
            neko::platform::Modifiers iMods = neko::platform::Modifiers::None
        ) -> void {
            InjectMouseDown(iWindow, iButton, iX, iY, iMods);
            InjectMouseUp(iWindow, iButton, iX, iY, iMods);
        }

        /** @brief Inject a double click. */
        virtual auto InjectMouseDoubleClick(
            WindowHandle iWindow, neko::platform::MouseBtn iButton, double iX, double iY,
            neko::platform::Modifiers iMods = neko::platform::Modifiers::None
        ) -> void {
            InjectMouseClick(iWindow, iButton, iX, iY, iMods);
            InjectMouseClick(iWindow, iButton, iX, iY, iMods);
        }

        /** @brief Inject a scroll event. */
        virtual auto InjectScroll(WindowHandle iWindow, double iDeltaX, double iDeltaY) -> void = 0;

        /** @brief Inject a drag operation (down, move sequence, up). */
        virtual auto InjectDrag(
            WindowHandle iWindow, neko::platform::MouseBtn iButton, double iFromX, double iFromY, double iToX,
            double iToY, uint32_t iSteps = 10
        ) -> void;

        // ── Window events ───────────────────────────────────────────────────

        /** @brief Inject a focus gained/lost event. */
        virtual auto InjectFocus(WindowHandle iWindow, bool iFocused) -> void = 0;

        /** @brief Inject a resize event. */
        virtual auto InjectResize(WindowHandle iWindow, uint32_t iWidth, uint32_t iHeight) -> void = 0;

        /** @brief Inject a close requested event. */
        virtual auto InjectCloseRequested(WindowHandle iWindow) -> void = 0;

        // ── Generic event injection ─────────────────────────────────────────

        /** @brief Inject any event directly. */
        virtual auto InjectEvent(WindowHandle iWindow, const neko::platform::Event& iEvent) -> void = 0;
    };

    // =========================================================================
    // IPhysicalEventSimulator — OS-level input simulation
    // =========================================================================

    /** @brief Interface for OS-level input simulation.
     *
     *  Uses platform-specific APIs to generate real input events:
     *  - Win32: SendInput()
     *  - X11: XTest extension
     *  - macOS: CGEvent
     *  - Emscripten: dispatchEvent()
     *
     *  Events go through the full OS input pipeline and may affect other windows.
     */
    class IPhysicalEventSimulator {
       public:
        virtual ~IPhysicalEventSimulator() = default;

        // ── Mouse control ───────────────────────────────────────────────────

        /** @brief Move mouse cursor to absolute screen position. */
        virtual auto MoveTo(int32_t iX, int32_t iY) -> void = 0;

        /** @brief Move mouse cursor by relative offset. */
        virtual auto MoveBy(int32_t iDx, int32_t iDy) -> void = 0;

        /** @brief Get current mouse cursor position. */
        [[nodiscard]] virtual auto GetMousePosition() const -> std::pair<int32_t, int32_t> = 0;

        /** @brief Simulate mouse button down. */
        virtual auto MouseDown(neko::platform::MouseBtn iButton) -> void = 0;

        /** @brief Simulate mouse button up. */
        virtual auto MouseUp(neko::platform::MouseBtn iButton) -> void = 0;

        /** @brief Simulate mouse click (down + up). */
        virtual auto Click(neko::platform::MouseBtn iButton = neko::platform::MouseBtn::Left) -> void {
            MouseDown(iButton);
            MouseUp(iButton);
        }

        /** @brief Simulate double click. */
        virtual auto DoubleClick(neko::platform::MouseBtn iButton = neko::platform::MouseBtn::Left) -> void {
            Click(iButton);
            Click(iButton);
        }

        /** @brief Simulate scroll wheel. */
        virtual auto Scroll(int32_t iDeltaX, int32_t iDeltaY) -> void = 0;

        /** @brief Simulate drag operation. */
        virtual auto Drag(
            int32_t iFromX, int32_t iFromY, int32_t iToX, int32_t iToY,
            neko::platform::MouseBtn iButton = neko::platform::MouseBtn::Left
        ) -> void;

        // ── Keyboard control ────────────────────────────────────────────────

        /** @brief Simulate key down. */
        virtual auto KeyDown(neko::platform::Key iKey) -> void = 0;

        /** @brief Simulate key up. */
        virtual auto KeyUp(neko::platform::Key iKey) -> void = 0;

        /** @brief Simulate key press (down + up) with optional modifiers. */
        virtual auto KeyPress(
            neko::platform::Key iKey, neko::platform::Modifiers iMods = neko::platform::Modifiers::None
        ) -> void;

        /** @brief Type a string of text. */
        virtual auto TypeText(std::string_view iText) -> void = 0;

        /** @brief Type text with delay between characters. */
        virtual auto TypeTextWithDelay(std::string_view iText, std::chrono::milliseconds iDelayPerChar) -> void;

        // ── State queries ───────────────────────────────────────────────────

        /** @brief Check if a key is currently pressed. */
        [[nodiscard]] virtual auto IsKeyPressed(neko::platform::Key iKey) const -> bool = 0;

        /** @brief Check if a mouse button is currently pressed. */
        [[nodiscard]] virtual auto IsMouseButtonPressed(neko::platform::MouseBtn iButton) const -> bool = 0;
    };

    // =========================================================================
    // Factory functions
    // =========================================================================

    using EventSimulatorPtr = std::unique_ptr<IEventSimulator>;
    using PhysicalEventSimulatorPtr = std::unique_ptr<IPhysicalEventSimulator>;

    /** @brief Create an event simulator for a given backend.
     *  @param iBackend The window backend to inject events into.
     *  @return Event simulator instance.
     */
    [[nodiscard]] auto CreateEventSimulator(IWindowBackend* iBackend) -> EventSimulatorPtr;

    /** @brief Create a physical event simulator for the current platform.
     *  @return Physical event simulator instance, or nullptr if not supported.
     */
    [[nodiscard]] auto CreatePhysicalEventSimulator() -> PhysicalEventSimulatorPtr;

}  // namespace miki::platform
