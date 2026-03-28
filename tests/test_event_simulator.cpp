// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include "miki/platform/EventSimulator.h"
#include "miki/platform/WindowManager.h"
#include "platform/MockWindowBackend.h"

using namespace miki::platform;
using namespace miki::test;
using namespace neko::platform;

class EventSimulatorTest : public ::testing::Test {
   protected:
    void SetUp() override {
        backendPtr_ = new MockWindowBackend();
        backend_ = backendPtr_;  // Keep raw pointer for simulator
        auto result = WindowManager::Create(std::unique_ptr<IWindowBackend>(backendPtr_));
        ASSERT_TRUE(result.has_value());
        wm_ = std::make_unique<WindowManager>(std::move(*result));
        simulator_ = std::make_unique<MockEventSimulator>(backend_);

        // Create a test window
        auto handleResult = wm_->CreateWindow({.title = "Test", .width = 800, .height = 600});
        ASSERT_TRUE(handleResult.has_value());
        testWindow_ = *handleResult;
    }

    void TearDown() override {
        simulator_.reset();
        wm_.reset();  // This will delete backend_ via unique_ptr
        backend_ = nullptr;
    }

    MockWindowBackend* backendPtr_ = nullptr;  // Owned by WindowManager
    MockWindowBackend* backend_ = nullptr;
    std::unique_ptr<WindowManager> wm_;
    std::unique_ptr<MockEventSimulator> simulator_;
    WindowHandle testWindow_;
};

// =============================================================================
// Keyboard Event Tests
// =============================================================================

TEST_F(EventSimulatorTest, InjectKeyDown) {
    simulator_->InjectKeyDown(testWindow_, Key::A, Modifiers::None);

    auto events = wm_->PollEvents();
    ASSERT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].window, testWindow_);
    ASSERT_TRUE(std::holds_alternative<KeyDown>(events[0].event));

    auto& keyDown = std::get<KeyDown>(events[0].event);
    EXPECT_EQ(keyDown.key, Key::A);
    EXPECT_EQ(keyDown.mods, Modifiers::None);
}

TEST_F(EventSimulatorTest, InjectKeyUp) {
    simulator_->InjectKeyUp(testWindow_, Key::Escape, Modifiers::Shift);

    auto events = wm_->PollEvents();
    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(std::holds_alternative<KeyUp>(events[0].event));

    auto& keyUp = std::get<KeyUp>(events[0].event);
    EXPECT_EQ(keyUp.key, Key::Escape);
    EXPECT_EQ(keyUp.mods, Modifiers::Shift);
}

TEST_F(EventSimulatorTest, InjectKeyPress) {
    simulator_->InjectKeyPress(testWindow_, Key::Enter, Modifiers::Control);

    auto events = wm_->PollEvents();
    ASSERT_EQ(events.size(), 2);  // KeyDown + KeyUp

    ASSERT_TRUE(std::holds_alternative<KeyDown>(events[0].event));
    ASSERT_TRUE(std::holds_alternative<KeyUp>(events[1].event));

    EXPECT_EQ(std::get<KeyDown>(events[0].event).key, Key::Enter);
    EXPECT_EQ(std::get<KeyUp>(events[1].event).key, Key::Enter);
}

TEST_F(EventSimulatorTest, InjectTextInput) {
    simulator_->InjectTextInput(testWindow_, U'A');

    auto events = wm_->PollEvents();
    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(std::holds_alternative<TextInput>(events[0].event));

    auto& textInput = std::get<TextInput>(events[0].event);
    EXPECT_EQ(textInput.codepoint, U'A');
}

// =============================================================================
// Mouse Event Tests
// =============================================================================

TEST_F(EventSimulatorTest, InjectMouseMove) {
    simulator_->InjectMouseMove(testWindow_, 100.0, 200.0);

    auto events = wm_->PollEvents();
    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(std::holds_alternative<MouseMove>(events[0].event));

    auto& move = std::get<MouseMove>(events[0].event);
    EXPECT_DOUBLE_EQ(move.x, 100.0);
    EXPECT_DOUBLE_EQ(move.y, 200.0);
}

TEST_F(EventSimulatorTest, InjectMouseDown) {
    simulator_->InjectMouseDown(testWindow_, MouseBtn::Left, 50.0, 75.0, Modifiers::None);

    auto events = wm_->PollEvents();
    ASSERT_EQ(events.size(), 2);  // MouseMove + MouseButton

    ASSERT_TRUE(std::holds_alternative<MouseMove>(events[0].event));
    ASSERT_TRUE(std::holds_alternative<MouseButton>(events[1].event));

    auto& button = std::get<MouseButton>(events[1].event);
    EXPECT_EQ(button.button, MouseBtn::Left);
    EXPECT_EQ(button.action, Action::Press);
}

TEST_F(EventSimulatorTest, InjectMouseUp) {
    simulator_->InjectMouseUp(testWindow_, MouseBtn::Right, 150.0, 250.0, Modifiers::None);

    auto events = wm_->PollEvents();
    ASSERT_GE(events.size(), 1);

    // Find the MouseButton event
    bool foundMouseUp = false;
    for (const auto& evt : events) {
        if (std::holds_alternative<MouseButton>(evt.event)) {
            auto& button = std::get<MouseButton>(evt.event);
            if (button.action == Action::Release) {
                EXPECT_EQ(button.button, MouseBtn::Right);
                foundMouseUp = true;
            }
        }
    }
    EXPECT_TRUE(foundMouseUp);
}

TEST_F(EventSimulatorTest, InjectMouseClick) {
    simulator_->InjectMouseClick(testWindow_, MouseBtn::Middle, 300.0, 400.0);

    auto events = wm_->PollEvents();
    // Should have: MouseMove, MouseButton(Press), MouseMove, MouseButton(Release)
    ASSERT_GE(events.size(), 2);

    int pressCount = 0;
    int releaseCount = 0;
    for (const auto& evt : events) {
        if (std::holds_alternative<MouseButton>(evt.event)) {
            auto& button = std::get<MouseButton>(evt.event);
            if (button.action == Action::Press) {
                ++pressCount;
            } else if (button.action == Action::Release) {
                ++releaseCount;
            }
        }
    }
    EXPECT_EQ(pressCount, 1);
    EXPECT_EQ(releaseCount, 1);
}

TEST_F(EventSimulatorTest, InjectMouseDoubleClick) {
    simulator_->InjectMouseDoubleClick(testWindow_, MouseBtn::Left, 100.0, 100.0);

    auto events = wm_->PollEvents();

    int pressCount = 0;
    int releaseCount = 0;
    for (const auto& evt : events) {
        if (std::holds_alternative<MouseButton>(evt.event)) {
            auto& button = std::get<MouseButton>(evt.event);
            if (button.action == Action::Press) {
                ++pressCount;
            } else if (button.action == Action::Release) {
                ++releaseCount;
            }
        }
    }
    EXPECT_EQ(pressCount, 2);
    EXPECT_EQ(releaseCount, 2);
}

TEST_F(EventSimulatorTest, InjectScroll) {
    simulator_->InjectScroll(testWindow_, 0.0, 3.0);

    auto events = wm_->PollEvents();
    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(std::holds_alternative<Scroll>(events[0].event));

    auto& scroll = std::get<Scroll>(events[0].event);
    EXPECT_DOUBLE_EQ(scroll.dx, 0.0);
    EXPECT_DOUBLE_EQ(scroll.dy, 3.0);
}

// =============================================================================
// Window Event Tests
// =============================================================================

TEST_F(EventSimulatorTest, InjectFocus) {
    simulator_->InjectFocus(testWindow_, true);

    auto events = wm_->PollEvents();
    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(std::holds_alternative<Focus>(events[0].event));

    auto& focus = std::get<Focus>(events[0].event);
    EXPECT_TRUE(focus.focused);
}

TEST_F(EventSimulatorTest, InjectFocusLost) {
    simulator_->InjectFocus(testWindow_, false);

    auto events = wm_->PollEvents();
    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(std::holds_alternative<Focus>(events[0].event));

    auto& focus = std::get<Focus>(events[0].event);
    EXPECT_FALSE(focus.focused);
}

TEST_F(EventSimulatorTest, InjectResize) {
    simulator_->InjectResize(testWindow_, 1920, 1080);

    auto events = wm_->PollEvents();
    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(std::holds_alternative<Resize>(events[0].event));

    auto& resize = std::get<Resize>(events[0].event);
    EXPECT_EQ(resize.width, 1920u);
    EXPECT_EQ(resize.height, 1080u);
}

TEST_F(EventSimulatorTest, InjectCloseRequested) {
    simulator_->InjectCloseRequested(testWindow_);

    auto events = wm_->PollEvents();
    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(std::holds_alternative<CloseRequested>(events[0].event));
}

// =============================================================================
// Complex Interaction Tests
// =============================================================================

TEST_F(EventSimulatorTest, SimulateTyping) {
    // Simulate typing "Hi"
    simulator_->InjectKeyPress(testWindow_, Key::H, Modifiers::Shift);
    simulator_->InjectTextInput(testWindow_, U'H');
    simulator_->InjectKeyPress(testWindow_, Key::I);
    simulator_->InjectTextInput(testWindow_, U'i');

    auto events = wm_->PollEvents();
    // H: KeyDown, KeyUp, TextInput
    // i: KeyDown, KeyUp, TextInput
    EXPECT_GE(events.size(), 6);

    // Verify text inputs are present
    int textInputCount = 0;
    for (const auto& evt : events) {
        if (std::holds_alternative<TextInput>(evt.event)) {
            ++textInputCount;
        }
    }
    EXPECT_EQ(textInputCount, 2);
}

TEST_F(EventSimulatorTest, SimulateButtonClickWithModifier) {
    simulator_->InjectMouseClick(testWindow_, MouseBtn::Left, 100.0, 100.0, Modifiers::Control);

    auto events = wm_->PollEvents();

    bool foundCtrlClick = false;
    for (const auto& evt : events) {
        if (std::holds_alternative<MouseButton>(evt.event)) {
            auto& button = std::get<MouseButton>(evt.event);
            if (button.action == Action::Press && button.mods == Modifiers::Control) {
                foundCtrlClick = true;
            }
        }
    }
    EXPECT_TRUE(foundCtrlClick);
}

TEST_F(EventSimulatorTest, MultipleWindowsReceiveCorrectEvents) {
    // Create a second window
    auto handle2Result = wm_->CreateWindow({.title = "Test2", .width = 640, .height = 480});
    ASSERT_TRUE(handle2Result.has_value());
    auto window2 = *handle2Result;

    // Inject events to different windows
    simulator_->InjectKeyDown(testWindow_, Key::A, Modifiers::None);
    simulator_->InjectKeyDown(window2, Key::B, Modifiers::None);

    auto events = wm_->PollEvents();
    ASSERT_EQ(events.size(), 2);

    // Verify each event goes to the correct window
    for (const auto& evt : events) {
        if (std::holds_alternative<KeyDown>(evt.event)) {
            auto& keyDown = std::get<KeyDown>(evt.event);
            if (keyDown.key == Key::A) {
                EXPECT_EQ(evt.window, testWindow_);
            } else if (keyDown.key == Key::B) {
                EXPECT_EQ(evt.window, window2);
            }
        }
    }
}

TEST_F(EventSimulatorTest, InjectDrag) {
    simulator_->InjectDrag(testWindow_, MouseBtn::Left, 0.0, 0.0, 100.0, 100.0, 5);

    auto events = wm_->PollEvents();

    // Should have: MouseDown, multiple MouseMoves, MouseUp
    int moveCount = 0;
    int pressCount = 0;
    int releaseCount = 0;

    for (const auto& evt : events) {
        if (std::holds_alternative<MouseMove>(evt.event)) {
            ++moveCount;
        } else if (std::holds_alternative<MouseButton>(evt.event)) {
            auto& button = std::get<MouseButton>(evt.event);
            if (button.action == Action::Press) {
                ++pressCount;
            } else if (button.action == Action::Release) {
                ++releaseCount;
            }
        }
    }

    EXPECT_EQ(pressCount, 1);
    EXPECT_EQ(releaseCount, 1);
    EXPECT_GE(moveCount, 5);  // At least 5 move events for 5 steps
}

TEST_F(EventSimulatorTest, InjectGenericEvent) {
    // Test InjectEvent with a custom event
    Scroll scrollEvent{.dx = 1.5, .dy = -2.5};
    simulator_->InjectEvent(testWindow_, scrollEvent);

    auto events = wm_->PollEvents();
    ASSERT_EQ(events.size(), 1);
    ASSERT_TRUE(std::holds_alternative<Scroll>(events[0].event));

    auto& scroll = std::get<Scroll>(events[0].event);
    EXPECT_DOUBLE_EQ(scroll.dx, 1.5);
    EXPECT_DOUBLE_EQ(scroll.dy, -2.5);
}
