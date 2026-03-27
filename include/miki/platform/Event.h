/** @brief Canonical input event types for the miki renderer.
 *
 * neko::platform::Event is a std::variant covering all window/input events.
 * Immutable value type — no errors, no allocation, trivially copyable subtypes.
 * Used across all phases for input handling (GLFW, neko, ImGui bridge, etc.).
 *
 * Namespace: neko::platform
 */
#pragma once

#include <cstdint>
#include <variant>

#include "miki/core/Types.h"

namespace neko::platform {

    // ===========================================================================
    // Supporting enums
    // ===========================================================================

    /** @brief Mouse button identifier. */
    enum class MouseBtn : uint8_t {
        Left,
        Right,
        Middle,
        X1,
        X2,
    };

    /** @brief Key/button action. */
    enum class Action : uint8_t {
        Press,
        Release,
        Repeat,
    };

    /** @brief Keyboard key codes (subset — extend as needed). */
    enum class Key : uint16_t {
        Unknown = 0,

        // Letters
        A,
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,

        // Digits
        Num0,
        Num1,
        Num2,
        Num3,
        Num4,
        Num5,
        Num6,
        Num7,
        Num8,
        Num9,

        // Function keys
        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,

        // Whitespace / editing
        Space,
        Enter,
        Escape,
        Tab,
        Backspace,
        Delete,
        Insert,

        // Arrow keys
        Left,
        Right,
        Up,
        Down,

        // Navigation
        Home,
        End,
        PageUp,
        PageDown,

        // Modifiers (as key events)
        LeftShift,
        RightShift,
        LeftControl,
        RightControl,
        LeftAlt,
        RightAlt,
        LeftSuper,
        RightSuper,

        // Punctuation / misc
        Comma,
        Period,
        Slash,
        Semicolon,
        Apostrophe,
        LeftBracket,
        RightBracket,
        Backslash,
        GraveAccent,
        Minus,
        Equal,

        // Numpad
        KP0,
        KP1,
        KP2,
        KP3,
        KP4,
        KP5,
        KP6,
        KP7,
        KP8,
        KP9,
        KPDecimal,
        KPDivide,
        KPMultiply,
        KPSubtract,
        KPAdd,
        KPEnter,
        KPEqual,

        // Lock keys
        CapsLock,
        ScrollLock,
        NumLock,
        PrintScreen,
        Pause,
        Menu,
    };

    /** @brief Modifier key bitmask. */
    enum class Modifiers : uint8_t {
        None = 0,
        Shift = 1 << 0,
        Control = 1 << 1,
        Alt = 1 << 2,
        Super = 1 << 3,
    };

    /** @brief Bitwise OR for Modifiers. */
    [[nodiscard]] constexpr auto operator|(Modifiers iLhs, Modifiers iRhs) noexcept -> Modifiers {
        return static_cast<Modifiers>(static_cast<uint8_t>(iLhs) | static_cast<uint8_t>(iRhs));
    }

    /** @brief Bitwise AND for Modifiers. */
    [[nodiscard]] constexpr auto operator&(Modifiers iLhs, Modifiers iRhs) noexcept -> Modifiers {
        return static_cast<Modifiers>(static_cast<uint8_t>(iLhs) & static_cast<uint8_t>(iRhs));
    }

    // ===========================================================================
    // Event subtypes
    // ===========================================================================

    /** @brief Window close requested. */
    struct CloseRequested {};

    /** @brief Window resize event. */
    struct Resize {
        uint32_t width = 0;
        uint32_t height = 0;
    };

    /** @brief Mouse movement event. */
    struct MouseMove {
        double x = 0.0;
        double y = 0.0;
        double dx = 0.0;
        double dy = 0.0;
    };

    /** @brief Mouse button event. */
    struct MouseButton {
        MouseBtn button = MouseBtn::Left;
        Action action = Action::Press;
        Modifiers mods = Modifiers::None;
    };

    /** @brief Key press event. */
    struct KeyDown {
        Key key = Key::Unknown;
        uint32_t scancode = 0;
        Modifiers mods = Modifiers::None;
    };

    /** @brief Key release event. */
    struct KeyUp {
        Key key = Key::Unknown;
        uint32_t scancode = 0;
        Modifiers mods = Modifiers::None;
    };

    /** @brief Scroll wheel event. */
    struct Scroll {
        double dx = 0.0;
        double dy = 0.0;
    };

    /** @brief Text input event (Unicode codepoint). */
    struct TextInput {
        char32_t codepoint = 0;
    };

    /** @brief Window focus change. */
    struct Focus {
        bool focused = false;
    };

    /** @brief DPI / content scale change. */
    struct DpiChanged {
        float scale = 1.0f;
    };

    /** @brief Continuous 6-DOF input (e.g. 3Dconnexion SpaceMouse).
     *
     * Velocity-driven — values represent speed, not absolute position.
     */
    struct ContinuousInput {
        miki::core::float3 translationVelocity = {};
        miki::core::float3 rotationVelocity = {};
    };

    // ===========================================================================
    // Event variant
    // ===========================================================================

    /** @brief Canonical input event — std::variant of all subtypes. */
    using Event = std::variant<
        CloseRequested, Resize, MouseMove, MouseButton, KeyDown, KeyUp, Scroll, TextInput, Focus, DpiChanged,
        ContinuousInput>;

}  // namespace neko::platform
