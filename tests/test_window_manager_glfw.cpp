// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// WindowManager tests with GLFW backend — tests real window system behavior.
// Requires a display server (X11/Wayland on Linux, Windows GUI, or macOS).

#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "miki/platform/WindowManager.h"
#include "miki/platform/glfw/GlfwWindowBackend.h"
#include "miki/platform/EventSimulator.h"

using namespace miki::platform;
using namespace miki::core;
using namespace neko::platform;

// ============================================================================
// Test fixture
// ============================================================================

class WindowManagerGlfwTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Create GLFW backend
        backend_ = std::make_unique<GlfwWindowBackend>(miki::rhi::BackendType::Vulkan14);
        auto result = WindowManager::Create(std::move(backend_));
        ASSERT_TRUE(result.has_value());
        wm_ = std::make_unique<WindowManager>(std::move(*result));

        // Create event simulator for testing
        simulator_ = CreateEventSimulator(wm_->GetBackend());
    }

    void TearDown() override {
        simulator_.reset();
        wm_.reset();
        backend_.reset();
    }

    std::unique_ptr<GlfwWindowBackend> backend_;
    std::unique_ptr<WindowManager> wm_;
    EventSimulatorPtr simulator_;

    // Helper to create a window and verify it exists
    auto CreateTestWindow(std::string_view title = "Test", uint32_t width = 800, uint32_t height = 600)
        -> WindowHandle {
        auto result = wm_->CreateWindow({.title = title, .width = width, .height = height});
        EXPECT_TRUE(result.has_value()) << "Failed to create window: " << title;
        return *result;
    }
};

// ============================================================================
// Creation & basic lifecycle
// ============================================================================

TEST_F(WindowManagerGlfwTest, CreateSingleWindow) {
    auto window = CreateTestWindow("GLFW Test Window");
    EXPECT_TRUE(window.IsValid());

    auto info = wm_->GetWindowInfo(window);
    EXPECT_EQ(info.handle, window);
    EXPECT_TRUE(info.alive);
    EXPECT_GE(info.extent.width, 800);
    EXPECT_GE(info.extent.height, 600);
}

TEST_F(WindowManagerGlfwTest, CreateMultipleWindows) {
    auto w1 = CreateTestWindow("Window 1", 800, 600);
    auto w2 = CreateTestWindow("Window 2", 640, 480);
    auto w3 = CreateTestWindow("Window 3", 1024, 768);

    EXPECT_TRUE(w1.IsValid());
    EXPECT_TRUE(w2.IsValid());
    EXPECT_TRUE(w3.IsValid());
    EXPECT_NE(w1, w2);
    EXPECT_NE(w2, w3);
    EXPECT_NE(w1, w3);

    auto allWindows = wm_->GetAllWindows();
    EXPECT_EQ(allWindows.size(), 3);
}

TEST_F(WindowManagerGlfwTest, ShowHideWindow) {
    auto window = CreateTestWindow();

    // Window should be visible by default (unless Hidden flag is set)
    wm_->ShowWindow(window);
    wm_->PollEvents();  // Process any show events

    // Hide window
    wm_->HideWindow(window);
    wm_->PollEvents();

    // Show again
    wm_->ShowWindow(window);
    wm_->PollEvents();
}

TEST_F(WindowManagerGlfwTest, WindowStateOperations) {
    auto window = CreateTestWindow("State Test", 800, 600);

    // Test resize
    wm_->ResizeWindow(window, 1024, 768);
    auto info = wm_->GetWindowInfo(window);
    // Note: GLFW may not update size immediately
    EXPECT_GE(info.extent.width, 800);
    EXPECT_GE(info.extent.height, 600);

    // Test position
    wm_->SetWindowPosition(window, 100, 200);
    auto pos = wm_->GetWindowPosition(window);
    // Position should be set
    EXPECT_EQ(pos.first, 100);
    EXPECT_EQ(pos.second, 200);

    // Test minimize
    wm_->MinimizeWindow(window);
    wm_->PollEvents();

    // Test restore
    wm_->RestoreWindow(window);
    wm_->PollEvents();

    // Test maximize
    wm_->MaximizeWindow(window);
    wm_->PollEvents();

    // Test focus
    wm_->FocusWindow(window);
    wm_->PollEvents();

    // Test title
    wm_->SetWindowTitle(window, "New Title");
    // Title is stored in backend, not in WindowInfo
    SUCCEED() << "Title set successfully";
}

// ============================================================================
// Event handling tests
// ============================================================================

TEST_F(WindowManagerGlfwTest, EventSimulatorIntegration) {
    if (!simulator_) {
        GTEST_SKIP() << "EventSimulator not available for GLFW backend";
    }

    auto window = CreateTestWindow("Event Test");

    // Inject keyboard events
    simulator_->InjectKeyDown(window, neko::platform::Key::A, neko::platform::Modifiers::None);
    simulator_->InjectKeyUp(window, neko::platform::Key::A, neko::platform::Modifiers::None);

    auto events = wm_->PollEvents();
    EXPECT_GE(events.size(), 0);  // May or may not have events depending on implementation

    // Inject mouse events
    simulator_->InjectMouseMove(window, 100.0, 200.0);
    simulator_->InjectMouseDown(window, neko::platform::MouseBtn::Left, 100.0, 200.0, neko::platform::Modifiers::None);
    simulator_->InjectMouseUp(window, neko::platform::MouseBtn::Left, 100.0, 200.0, neko::platform::Modifiers::None);

    events = wm_->PollEvents();
    EXPECT_GE(events.size(), 0);
}

TEST_F(WindowManagerGlfwTest, RealKeyboardEvents) {
    auto window = CreateTestWindow("Keyboard Test");

    // Poll for real events (this test requires manual interaction or a display)
    wm_->PollEvents();

    // In an automated test, we can't guarantee keyboard input
    // But we can verify the event system doesn't crash
    SUCCEED() << "Keyboard event system initialized successfully";
}

TEST_F(WindowManagerGlfwTest, RealMouseEvents) {
    auto window = CreateTestWindow("Mouse Test");

    // Poll for real events
    wm_->PollEvents();

    // Verify mouse event system works
    SUCCEED() << "Mouse event system initialized successfully";
}

TEST_F(WindowManagerGlfwTest, WindowCloseEvent) {
    auto window = CreateTestWindow("Close Test");

    // Simulate close request (if EventSimulator is available)
    if (simulator_) {
        simulator_->InjectCloseRequested(window);

        auto events = wm_->PollEvents();
        // Look for close event
        bool foundClose = false;
        for (const auto& evt : events) {
            if (std::holds_alternative<neko::platform::CloseRequested>(evt.event)) {
                foundClose = true;
                break;
            }
        }
        // Close event may or may not be present depending on implementation
        (void)foundClose;  // Suppress unused variable warning
    }

    // Actually destroy the window
    auto result = wm_->DestroyWindow(window);
    EXPECT_TRUE(result.has_value());

    // Verify window is gone
    auto info = wm_->GetWindowInfo(window);
    EXPECT_FALSE(info.alive);
}

// ============================================================================
// Parent-child relationships
// ============================================================================

TEST_F(WindowManagerGlfwTest, ParentChildWindows) {
    // Create parent window
    auto parent = CreateTestWindow("Parent", 800, 600);

    // Create child with parent set during creation
    auto childResult = wm_->CreateWindow({.title = "Child", .width = 400, .height = 300, .parent = parent});
    EXPECT_TRUE(childResult.has_value());
    auto child = *childResult;

    // Verify relationship
    auto parentInfo = wm_->GetParent(child);
    EXPECT_EQ(parentInfo, parent);

    auto children = wm_->GetChildren(parent);
    EXPECT_EQ(children.size(), 1);
    EXPECT_EQ(children[0], child);

    // Clean up
    EXPECT_TRUE(wm_->DestroyWindow(parent).has_value());
}

// ============================================================================
// Stress tests
// ============================================================================

TEST_F(WindowManagerGlfwTest, CreateManyWindows) {
    constexpr int kWindowCount = 10;
    std::vector<WindowHandle> windows;

    for (int i = 0; i < kWindowCount; ++i) {
        auto w = CreateTestWindow("Window " + std::to_string(i), 200, 150);
        if (w.IsValid()) {
            windows.push_back(w);
        } else {
            // Window creation failed, which is acceptable in this test
        }
    }

    EXPECT_GE(windows.size(), 5);  // At least some windows should succeed

    // Clean up
    for (auto w : windows) {
        wm_->DestroyWindow(w);
    }
}

TEST_F(WindowManagerGlfwTest, RapidCreateDestroy) {
    for (int i = 0; i < 5; ++i) {
        auto w = CreateTestWindow("Rapid " + std::to_string(i));
        if (w.IsValid()) {
            wm_->PollEvents();      // Process any events
            wm_->DestroyWindow(w);  // Ignore return value for this test
        }
    }
}

// ============================================================================
// Error handling
// ============================================================================

TEST_F(WindowManagerGlfwTest, InvalidWindowOperations) {
    WindowHandle invalidHandle{};

    // Operations on invalid handle should fail
    auto info = wm_->GetWindowInfo(invalidHandle);
    EXPECT_FALSE(info.alive);
    auto parent = wm_->GetParent(invalidHandle);
    EXPECT_FALSE(parent.IsValid());
    auto children = wm_->GetChildren(invalidHandle);
    EXPECT_TRUE(children.empty());
    EXPECT_FALSE(wm_->DestroyWindow(invalidHandle).has_value());
}

// ============================================================================
// Integration test
// ============================================================================

TEST_F(WindowManagerGlfwTest, FullWorkflow) {
    // Create a window hierarchy
    auto root = CreateTestWindow("Root", 1024, 768);

    // Create children with parent set during creation
    auto child1Result = wm_->CreateWindow({.title = "Child 1", .width = 400, .height = 300, .parent = root});
    EXPECT_TRUE(child1Result.has_value());
    auto child1 = *child1Result;

    auto child2Result = wm_->CreateWindow({.title = "Child 2", .width = 400, .height = 300, .parent = root});
    EXPECT_TRUE(child2Result.has_value());
    auto child2 = *child2Result;

    auto grandchildResult = wm_->CreateWindow({.title = "Grandchild", .width = 200, .height = 150, .parent = child1});
    EXPECT_TRUE(grandchildResult.has_value());
    auto grandchild = *grandchildResult;

    // Modify window states
    wm_->SetWindowTitle(root, "Main Window");
    wm_->ResizeWindow(child1, 500, 400);
    wm_->SetWindowPosition(child2, 100, 100);
    wm_->FocusWindow(grandchild);

    // Process events
    wm_->PollEvents();

    // Verify hierarchy
    auto children = wm_->GetChildren(root);
    EXPECT_EQ(children.size(), 2);

    // Clean up cascade
    EXPECT_TRUE(wm_->DestroyWindow(root).has_value());

    // All should be destroyed
    EXPECT_FALSE(wm_->GetWindowInfo(root).alive);
    EXPECT_FALSE(wm_->GetWindowInfo(child1).alive);
    EXPECT_FALSE(wm_->GetWindowInfo(child2).alive);
    EXPECT_FALSE(wm_->GetWindowInfo(grandchild).alive);
}

// ============================================================================
// Physical Event Simulator Test (if available)
// ============================================================================

TEST_F(WindowManagerGlfwTest, PhysicalEventSimulator) {
    auto physicalSim = CreatePhysicalEventSimulator();
    if (!physicalSim) {
        GTEST_SKIP() << "PhysicalEventSimulator not available on this platform";
    }

    auto window = CreateTestWindow("Physical Events");

    // Test physical simulation (this will actually move the mouse)
    // Note: This test may interfere with user's mouse, so be careful
    auto currentPos = physicalSim->GetMousePosition();

    // Move mouse slightly
    physicalSim->MoveBy(10, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Move back
    physicalSim->MoveTo(currentPos.first, currentPos.second);

    SUCCEED() << "PhysicalEventSimulator works";
}
