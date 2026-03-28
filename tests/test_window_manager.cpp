// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Comprehensive tests for WindowManager (N-ary tree, generation handles, cascade destroy).
// Uses MockWindowBackend — no OS windows or GPU resources needed.

#include <gtest/gtest.h>

#include "miki/platform/WindowManager.h"
#include "miki/core/ErrorCode.h"
#include "platform/MockWindowBackend.h"

using namespace miki::platform;
using namespace miki::core;
using miki::test::MockWindowBackend;

// ============================================================================
// Test fixture
// ============================================================================

class WindowManagerTest : public ::testing::Test {
   protected:
    std::unique_ptr<MockWindowBackend> mockBackend_;
    MockWindowBackend* backendPtr_ = nullptr;

    void SetUp() override {
        mockBackend_ = std::make_unique<MockWindowBackend>();
        backendPtr_ = mockBackend_.get();
    }

    auto CreateManager() -> WindowManager {
        auto result = WindowManager::Create(std::move(mockBackend_));
        EXPECT_TRUE(result.has_value());
        return std::move(*result);
    }
};

// ============================================================================
// Creation & basic lifecycle
// ============================================================================

TEST_F(WindowManagerTest, CreateWithNullBackendFails) {
    auto result = WindowManager::Create(nullptr);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::InvalidArgument);
}

TEST_F(WindowManagerTest, CreateSingleWindow) {
    auto wm = CreateManager();
    auto result = wm.CreateWindow({.title = "Test", .width = 800, .height = 600});
    ASSERT_TRUE(result.has_value());
    auto handle = *result;
    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(handle.generation, 1);
    EXPECT_EQ(wm.GetWindowCount(), 1u);
    EXPECT_EQ(backendPtr_->createCount, 1u);
}

TEST_F(WindowManagerTest, CreateMultipleWindows) {
    auto wm = CreateManager();
    auto h1 = wm.CreateWindow({.title = "W1", .width = 800, .height = 600});
    auto h2 = wm.CreateWindow({.title = "W2", .width = 640, .height = 480});
    auto h3 = wm.CreateWindow({.title = "W3", .width = 320, .height = 240});
    ASSERT_TRUE(h1.has_value());
    ASSERT_TRUE(h2.has_value());
    ASSERT_TRUE(h3.has_value());
    EXPECT_NE(*h1, *h2);
    EXPECT_NE(*h2, *h3);
    EXPECT_EQ(wm.GetWindowCount(), 3u);
}

TEST_F(WindowManagerTest, CreateZeroSizeFails) {
    auto wm = CreateManager();
    auto r1 = wm.CreateWindow({.width = 0, .height = 600});
    auto r2 = wm.CreateWindow({.width = 800, .height = 0});
    EXPECT_FALSE(r1.has_value());
    EXPECT_FALSE(r2.has_value());
    EXPECT_EQ(r1.error(), ErrorCode::InvalidArgument);
    EXPECT_EQ(r2.error(), ErrorCode::InvalidArgument);
}

TEST_F(WindowManagerTest, DestroyWindow) {
    auto wm = CreateManager();
    auto h = *wm.CreateWindow({.width = 800, .height = 600});
    auto result = wm.DestroyWindow(h);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0], h);
    EXPECT_EQ(wm.GetWindowCount(), 0u);
    EXPECT_EQ(backendPtr_->destroyCount, 1u);
}

TEST_F(WindowManagerTest, DestroyInvalidHandleFails) {
    auto wm = CreateManager();
    WindowHandle bogus{999, 42};
    auto result = wm.DestroyWindow(bogus);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::InvalidArgument);
}

TEST_F(WindowManagerTest, DoubleDestroyFails) {
    auto wm = CreateManager();
    auto h = *wm.CreateWindow({.width = 800, .height = 600});
    EXPECT_TRUE(wm.DestroyWindow(h).has_value());
    auto result = wm.DestroyWindow(h);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::InvalidArgument);
}

// ============================================================================
// Generation counting (ABA safety)
// ============================================================================

TEST_F(WindowManagerTest, GenerationIncrementsOnReuse) {
    auto wm = CreateManager();
    auto h1 = *wm.CreateWindow({.width = 800, .height = 600});
    EXPECT_TRUE(wm.DestroyWindow(h1).has_value());
    auto h2 = *wm.CreateWindow({.width = 800, .height = 600});
    // Same slot may be reused, but generation must differ
    if (h1.id == h2.id) {
        EXPECT_NE(h1.generation, h2.generation);
    }
    // Old handle must not resolve
    EXPECT_EQ(wm.GetNativeToken(h1), nullptr);
}

// ============================================================================
// Tree structure
// ============================================================================

TEST_F(WindowManagerTest, ParentChild) {
    auto wm = CreateManager();
    auto root = *wm.CreateWindow({.title = "Root", .width = 800, .height = 600});
    auto child = *wm.CreateWindow({.title = "Child", .width = 400, .height = 300, .parent = root});

    EXPECT_EQ(wm.GetParent(root), WindowHandle{});
    EXPECT_EQ(wm.GetParent(child), root);

    auto children = wm.GetChildren(root);
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], child);

    EXPECT_TRUE(wm.GetChildren(child).empty());
}

TEST_F(WindowManagerTest, GetRoot) {
    auto wm = CreateManager();
    auto root = *wm.CreateWindow({.title = "Root", .width = 800, .height = 600});
    auto child = *wm.CreateWindow({.title = "Child", .width = 400, .height = 300, .parent = root});
    auto grandchild = *wm.CreateWindow({.title = "GC", .width = 200, .height = 150, .parent = child});

    EXPECT_EQ(wm.GetRoot(root), root);
    EXPECT_EQ(wm.GetRoot(child), root);
    EXPECT_EQ(wm.GetRoot(grandchild), root);
}

TEST_F(WindowManagerTest, GetDepth) {
    auto wm = CreateManager();
    auto root = *wm.CreateWindow({.title = "R", .width = 800, .height = 600});
    auto c1 = *wm.CreateWindow({.title = "C1", .width = 400, .height = 300, .parent = root});
    auto c2 = *wm.CreateWindow({.title = "C2", .width = 200, .height = 150, .parent = c1});

    EXPECT_EQ(wm.GetDepth(root), 1u);
    EXPECT_EQ(wm.GetDepth(c1), 2u);
    EXPECT_EQ(wm.GetDepth(c2), 3u);
}

TEST_F(WindowManagerTest, MaxDepthEnforced) {
    auto wm = CreateManager();
    // Build chain up to kMaxDepth
    WindowHandle prev = *wm.CreateWindow({.width = 100, .height = 100});
    for (uint32_t i = 1; i < WindowManager::kMaxDepth; ++i) {
        auto r = wm.CreateWindow({.width = 100, .height = 100, .parent = prev});
        ASSERT_TRUE(r.has_value()) << "Failed at depth " << (i + 1);
        prev = *r;
    }
    // One more should fail
    auto tooDeep = wm.CreateWindow({.width = 100, .height = 100, .parent = prev});
    EXPECT_FALSE(tooDeep.has_value());
    EXPECT_EQ(tooDeep.error(), ErrorCode::InvalidArgument);
}

TEST_F(WindowManagerTest, CreateWithInvalidParentFails) {
    auto wm = CreateManager();
    WindowHandle bogus{999, 42};
    auto result = wm.CreateWindow({.width = 800, .height = 600, .parent = bogus});
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::InvalidArgument);
}

TEST_F(WindowManagerTest, MultipleRoots) {
    auto wm = CreateManager();
    auto r1 = *wm.CreateWindow({.title = "R1", .width = 800, .height = 600});
    auto r2 = *wm.CreateWindow({.title = "R2", .width = 640, .height = 480});
    auto roots = wm.GetRootWindows();
    EXPECT_EQ(roots.size(), 2u);
    EXPECT_TRUE(std::find(roots.begin(), roots.end(), r1) != roots.end());
    EXPECT_TRUE(std::find(roots.begin(), roots.end(), r2) != roots.end());
}

// ============================================================================
// Cascade destruction
// ============================================================================

TEST_F(WindowManagerTest, CascadeDestroyPostOrder) {
    auto wm = CreateManager();
    auto root = *wm.CreateWindow({.title = "R", .width = 800, .height = 600});
    auto c1 = *wm.CreateWindow({.title = "C1", .width = 400, .height = 300, .parent = root});
    auto c2 = *wm.CreateWindow({.title = "C2", .width = 400, .height = 300, .parent = root});
    auto gc1 = *wm.CreateWindow({.title = "GC1", .width = 200, .height = 150, .parent = c1});

    auto result = wm.DestroyWindow(root);
    ASSERT_TRUE(result.has_value());
    auto& victims = *result;

    // Post-order: gc1 before c1, c1/c2 before root, root last
    ASSERT_EQ(victims.size(), 4u);
    EXPECT_EQ(victims.back(), root);

    // gc1 must come before c1
    auto posGc1 = std::find(victims.begin(), victims.end(), gc1) - victims.begin();
    auto posC1 = std::find(victims.begin(), victims.end(), c1) - victims.begin();
    EXPECT_LT(posGc1, posC1);

    EXPECT_EQ(wm.GetWindowCount(), 0u);
    EXPECT_EQ(backendPtr_->destroyCount, 4u);
}

TEST_F(WindowManagerTest, DestroySubtreePreserveSiblings) {
    auto wm = CreateManager();
    auto root = *wm.CreateWindow({.title = "R", .width = 800, .height = 600});
    auto c1 = *wm.CreateWindow({.title = "C1", .width = 400, .height = 300, .parent = root});
    auto c2 = *wm.CreateWindow({.title = "C2", .width = 400, .height = 300, .parent = root});

    auto result = wm.DestroyWindow(c1);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 1u);

    EXPECT_EQ(wm.GetWindowCount(), 2u);  // root + c2
    auto children = wm.GetChildren(root);
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], c2);
}

TEST_F(WindowManagerTest, GetDescendantsPostOrder) {
    auto wm = CreateManager();
    auto root = *wm.CreateWindow({.width = 800, .height = 600});
    auto c1 = *wm.CreateWindow({.width = 400, .height = 300, .parent = root});
    auto gc1 = *wm.CreateWindow({.width = 200, .height = 150, .parent = c1});

    auto desc = wm.GetDescendantsPostOrder(root);
    ASSERT_EQ(desc.size(), 2u);
    EXPECT_EQ(desc[0], gc1);
    EXPECT_EQ(desc[1], c1);
}

// ============================================================================
// WindowInfo & queries
// ============================================================================

TEST_F(WindowManagerTest, GetWindowInfo) {
    auto wm = CreateManager();
    auto h = *wm.CreateWindow({.title = "Info", .width = 1024, .height = 768});
    auto info = wm.GetWindowInfo(h);
    EXPECT_EQ(info.handle, h);
    EXPECT_EQ(info.extent.width, 1024u);
    EXPECT_EQ(info.extent.height, 768u);
    EXPECT_TRUE(info.alive);
    EXPECT_FALSE(info.minimized);
}

TEST_F(WindowManagerTest, GetWindowInfoInvalid) {
    auto wm = CreateManager();
    auto info = wm.GetWindowInfo(WindowHandle{999, 0});
    EXPECT_FALSE(info.alive);
}

TEST_F(WindowManagerTest, GetNativeToken) {
    auto wm = CreateManager();
    auto h = *wm.CreateWindow({.width = 800, .height = 600});
    EXPECT_NE(wm.GetNativeToken(h), nullptr);

    wm.DestroyWindow(h);
    EXPECT_EQ(wm.GetNativeToken(h), nullptr);
}

// ============================================================================
// WindowFlags
// ============================================================================

TEST_F(WindowManagerTest, FlagsPreservedInInfo) {
    auto wm = CreateManager();
    auto h = *wm.CreateWindow({
        .width = 800,
        .height = 600,
        .flags = WindowFlags::Borderless | WindowFlags::AlwaysOnTop,
    });
    auto info = wm.GetWindowInfo(h);
    EXPECT_TRUE(info.flags.Has(WindowFlags::Borderless));
    EXPECT_TRUE(info.flags.Has(WindowFlags::AlwaysOnTop));
    EXPECT_FALSE(info.flags.Has(WindowFlags::Hidden));
}

TEST_F(WindowManagerTest, HiddenFlagPassedToBackend) {
    auto wm = CreateManager();
    auto h = *wm.CreateWindow({
        .width = 800,
        .height = 600,
        .flags = WindowFlags::Hidden,
    });
    auto* token = wm.GetNativeToken(h);
    auto it = backendPtr_->windows.find(token);
    ASSERT_NE(it, backendPtr_->windows.end());
    EXPECT_FALSE(it->second.visible);
}

// ============================================================================
// Show / Hide
// ============================================================================

TEST_F(WindowManagerTest, ShowHideWindow) {
    auto wm = CreateManager();
    auto h = *wm.CreateWindow({
        .width = 800,
        .height = 600,
        .flags = WindowFlags::Hidden,
    });
    auto* token = wm.GetNativeToken(h);
    EXPECT_FALSE(backendPtr_->windows[token].visible);

    wm.ShowWindow(h);
    EXPECT_TRUE(backendPtr_->windows[token].visible);

    wm.HideWindow(h);
    EXPECT_FALSE(backendPtr_->windows[token].visible);
}

// ============================================================================
// Event polling
// ============================================================================

TEST_F(WindowManagerTest, PollEventsReturnsInjected) {
    auto wm = CreateManager();
    auto h = *wm.CreateWindow({.width = 800, .height = 600});

    backendPtr_->injectedEvents.push_back({h, neko::platform::CloseRequested{}});
    auto events = wm.PollEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].window, h);
    EXPECT_TRUE(std::holds_alternative<neko::platform::CloseRequested>(events[0].event));
}

TEST_F(WindowManagerTest, PollEventsDoesNotAutoDestroy) {
    auto wm = CreateManager();
    auto h = *wm.CreateWindow({.width = 800, .height = 600});

    backendPtr_->injectedEvents.push_back({h, neko::platform::CloseRequested{}});
    wm.PollEvents();

    // Window should still be alive — user decides close policy
    EXPECT_EQ(wm.GetWindowCount(), 1u);
    EXPECT_TRUE(wm.GetWindowInfo(h).alive);
}

TEST_F(WindowManagerTest, PollEventsFiltersDeadWindows) {
    auto wm = CreateManager();
    auto h1 = *wm.CreateWindow({.width = 800, .height = 600});
    auto h2 = *wm.CreateWindow({.width = 640, .height = 480});

    wm.DestroyWindow(h1);

    // Inject event for destroyed window — should be filtered
    backendPtr_->injectedEvents.push_back({h1, neko::platform::Focus{.focused = true}});
    backendPtr_->injectedEvents.push_back({h2, neko::platform::Focus{.focused = true}});
    auto events = wm.PollEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].window, h2);
}

// ============================================================================
// ShouldClose
// ============================================================================

TEST_F(WindowManagerTest, ShouldCloseWhenAllDestroyed) {
    auto wm = CreateManager();
    EXPECT_TRUE(wm.ShouldClose());  // no windows = should close

    auto h = *wm.CreateWindow({.width = 800, .height = 600});
    EXPECT_FALSE(wm.ShouldClose());

    wm.DestroyWindow(h);
    EXPECT_TRUE(wm.ShouldClose());
}

// ============================================================================
// Enumeration
// ============================================================================

TEST_F(WindowManagerTest, GetAllWindows) {
    auto wm = CreateManager();
    auto h1 = *wm.CreateWindow({.width = 800, .height = 600});
    auto h2 = *wm.CreateWindow({.width = 640, .height = 480});
    auto all = wm.GetAllWindows();
    EXPECT_EQ(all.size(), 2u);
}

TEST_F(WindowManagerTest, GetActiveWindowsExcludesMinimized) {
    auto wm = CreateManager();
    auto h1 = *wm.CreateWindow({.width = 800, .height = 600});
    auto h2 = *wm.CreateWindow({.width = 640, .height = 480});

    auto* token2 = wm.GetNativeToken(h2);
    backendPtr_->SimulateMinimize(token2);

    auto active = wm.GetActiveWindows();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0], h1);
}

// ============================================================================
// Destructor cleanup
// ============================================================================

TEST_F(WindowManagerTest, DestructorCleansUpAllWindows) {
    // backendPtr_ becomes dangling after WindowManager dtor destroys the backend.
    // Use a shared counter to observe destroy calls after backend lifetime.
    auto destroyCounter = std::make_shared<uint32_t>(0);
    struct CountingBackend final : MockWindowBackend {
        std::shared_ptr<uint32_t> counter;
        explicit CountingBackend(std::shared_ptr<uint32_t> c) : counter(std::move(c)) {}
        auto DestroyNativeWindow(void* t) -> void override {
            ++(*counter);
            MockWindowBackend::DestroyNativeWindow(t);
        }
    };
    {
        auto backend = std::make_unique<CountingBackend>(destroyCounter);
        auto wm = *WindowManager::Create(std::move(backend));
        (void)wm.CreateWindow({.width = 800, .height = 600});
        (void)wm.CreateWindow({.width = 640, .height = 480});
    }  // wm destructor runs here
    EXPECT_EQ(*destroyCounter, 2u);
}

// ============================================================================
// Move semantics
// ============================================================================

TEST_F(WindowManagerTest, MoveConstruct) {
    auto wm1 = CreateManager();
    auto h = *wm1.CreateWindow({.width = 800, .height = 600});
    auto wm2 = std::move(wm1);
    EXPECT_EQ(wm2.GetWindowCount(), 1u);
    EXPECT_NE(wm2.GetNativeToken(h), nullptr);
}

TEST_F(WindowManagerTest, MoveAssign) {
    auto wm1 = CreateManager();
    auto h = *wm1.CreateWindow({.width = 800, .height = 600});

    auto backend2 = std::make_unique<MockWindowBackend>();
    auto wm2 = *WindowManager::Create(std::move(backend2));
    wm2 = std::move(wm1);
    EXPECT_EQ(wm2.GetWindowCount(), 1u);
    EXPECT_NE(wm2.GetNativeToken(h), nullptr);
}

// ============================================================================
// Backend parent token forwarding
// ============================================================================

TEST_F(WindowManagerTest, ParentTokenForwardedToBackend) {
    auto wm = CreateManager();
    auto root = *wm.CreateWindow({.title = "Root", .width = 800, .height = 600});
    auto* rootToken = wm.GetNativeToken(root);

    auto child = *wm.CreateWindow({.title = "Child", .width = 400, .height = 300, .parent = root});
    auto* childToken = wm.GetNativeToken(child);

    // Mock backend stores parentToken — verify it matches root's native token
    auto it = backendPtr_->windows.find(childToken);
    ASSERT_NE(it, backendPtr_->windows.end());
    EXPECT_EQ(it->second.parentToken, rootToken);
}

// ============================================================================
// SetHasSurfaceCallback (debug assert path)
// ============================================================================

TEST_F(WindowManagerTest, HasSurfaceCallbackChecked) {
    auto wm = CreateManager();
    auto h = *wm.CreateWindow({.width = 800, .height = 600});

    // No callback → destroy should succeed
    auto r1 = wm.DestroyWindow(h);
    EXPECT_TRUE(r1.has_value());

    auto h2 = *wm.CreateWindow({.width = 800, .height = 600});
    // Callback returns false → destroy should succeed
    wm.SetHasSurfaceCallback([](WindowHandle) -> bool { return false; });
    auto r2 = wm.DestroyWindow(h2);
    EXPECT_TRUE(r2.has_value());
}

// ============================================================================
// Dynamic capacity (ChunkedSlotMap, no kMaxWindows limit)
// ============================================================================

TEST_F(WindowManagerTest, DynamicCapacityBeyond16) {
    auto wm = CreateManager();
    std::vector<WindowHandle> handles;
    for (uint32_t i = 0; i < 32; ++i) {
        auto r = wm.CreateWindow({.width = 100, .height = 100});
        ASSERT_TRUE(r.has_value()) << "Failed at window " << i;
        handles.push_back(*r);
    }
    EXPECT_EQ(wm.GetWindowCount(), 32u);

    // Destroy half
    for (uint32_t i = 0; i < 16; ++i) {
        EXPECT_TRUE(wm.DestroyWindow(handles[i]).has_value());
    }
    EXPECT_EQ(wm.GetWindowCount(), 16u);
}
