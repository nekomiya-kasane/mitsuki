/** @brief EventSimulator implementation — Default method implementations.
 */

#include "miki/platform/EventSimulator.h"

#include <thread>

namespace miki::platform {

    // =========================================================================
    // IEventSimulator default implementations
    // =========================================================================

    auto IEventSimulator::InjectText(WindowHandle iWindow, std::string_view iText) -> void {
        for (unsigned char c : iText) {
            if ((c & 0x80) == 0) {
                // ASCII character
                InjectTextInput(iWindow, static_cast<char32_t>(c));
            } else if ((c & 0xE0) == 0xC0) {
                // 2-byte UTF-8 sequence start - simplified handling
                InjectTextInput(iWindow, static_cast<char32_t>(c));
            } else if ((c & 0xF0) == 0xE0) {
                // 3-byte UTF-8 sequence start
                InjectTextInput(iWindow, static_cast<char32_t>(c));
            } else if ((c & 0xF8) == 0xF0) {
                // 4-byte UTF-8 sequence start
                InjectTextInput(iWindow, static_cast<char32_t>(c));
            }
            // Continuation bytes are skipped in this simplified implementation
        }
    }

    auto IEventSimulator::InjectDrag(
        WindowHandle iWindow, neko::platform::MouseBtn iButton, double iFromX, double iFromY, double iToX, double iToY,
        uint32_t iSteps
    ) -> void {
        if (iSteps == 0) {
            iSteps = 1;
        }

        InjectMouseDown(iWindow, iButton, iFromX, iFromY);

        double dx = (iToX - iFromX) / static_cast<double>(iSteps);
        double dy = (iToY - iFromY) / static_cast<double>(iSteps);

        for (uint32_t i = 1; i <= iSteps; ++i) {
            double x = iFromX + dx * static_cast<double>(i);
            double y = iFromY + dy * static_cast<double>(i);
            InjectMouseMove(iWindow, x, y);
        }

        InjectMouseUp(iWindow, iButton, iToX, iToY);
    }

    // =========================================================================
    // IPhysicalEventSimulator default implementations
    // =========================================================================

    auto IPhysicalEventSimulator::Drag(
        int32_t iFromX, int32_t iFromY, int32_t iToX, int32_t iToY, neko::platform::MouseBtn iButton
    ) -> void {
        MoveTo(iFromX, iFromY);
        MouseDown(iButton);

        constexpr int kSteps = 10;
        int32_t dx = (iToX - iFromX) / kSteps;
        int32_t dy = (iToY - iFromY) / kSteps;

        for (int i = 1; i <= kSteps; ++i) {
            MoveTo(iFromX + dx * i, iFromY + dy * i);
        }

        MouseUp(iButton);
    }

    auto IPhysicalEventSimulator::KeyPress(neko::platform::Key iKey, neko::platform::Modifiers iMods) -> void {
        using enum neko::platform::Key;
        using enum neko::platform::Modifiers;

        if ((iMods & Shift) != None) {
            KeyDown(LeftShift);
        }
        if ((iMods & Control) != None) {
            KeyDown(LeftControl);
        }
        if ((iMods & Alt) != None) {
            KeyDown(LeftAlt);
        }
        if ((iMods & Super) != None) {
            KeyDown(LeftSuper);
        }

        KeyDown(iKey);
        KeyUp(iKey);

        if ((iMods & Super) != None) {
            KeyUp(LeftSuper);
        }
        if ((iMods & Alt) != None) {
            KeyUp(LeftAlt);
        }
        if ((iMods & Control) != None) {
            KeyUp(LeftControl);
        }
        if ((iMods & Shift) != None) {
            KeyUp(LeftShift);
        }
    }

    auto IPhysicalEventSimulator::TypeTextWithDelay(std::string_view iText, std::chrono::milliseconds iDelayPerChar)
        -> void {
        for (char c : iText) {
            // Type single character - simplified, just inject the character
            std::string single(1, c);
            TypeText(single);
            std::this_thread::sleep_for(iDelayPerChar);
        }
    }

}  // namespace miki::platform
