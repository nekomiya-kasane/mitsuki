// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Comprehensive WindowManager tests per specs/01-window-manager.md SS13.
// Uses GLFW real backend — requires a display server.

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "miki/platform/WindowManager.h"
#include "miki/platform/Event.h"
#include "miki/platform/glfw/GlfwWindowBackend.h"
#include "miki/core/ErrorCode.h"

using namespace miki::platform;
using namespace miki::core;

// ============================================================================
// Test fixture — real GLFW backend
// ============================================================================

class WMSpecTest : public ::testing::Test {
   protected:
    void SetUp() override {
        auto backend = std::make_unique<GlfwWindowBackend>(miki::rhi::BackendType::WebGPU, /*visible=*/false);
        backendPtr_ = backend.get();
        auto result = WindowManager::Create(std::move(backend));
        ASSERT_TRUE(result.has_value()) << "Failed to create WindowManager with GLFW backend";
        wm_ = std::make_unique<WindowManager>(std::move(*result));
    }

    void TearDown() override {
        wm_.reset();
        backendPtr_ = nullptr;
    }

    auto MakeWindow(
        std::string_view title = "Test", uint32_t w = 320, uint32_t h = 240, WindowHandle parent = {},
        EnumFlags<WindowFlags> flags = WindowFlags::Hidden
    ) -> WindowHandle {
        auto r = wm_->CreateWindow({.title = title, .width = w, .height = h, .parent = parent, .flags = flags});
        EXPECT_TRUE(r.has_value()) << "CreateWindow failed: " << title;
        return r.has_value() ? *r : WindowHandle{};
    }

    GlfwWindowBackend* backendPtr_ = nullptr;
    std::unique_ptr<WindowManager> wm_;
};

// ============================================================================
// SS13: WindowManager Creation & Destruction
// ============================================================================

TEST_F(WMSpecTest, CreateWithValidBackendSucceeds) {
    // Already tested in SetUp — wm_ is valid
    EXPECT_GE(wm_->GetWindowCount(), 0u);
}

TEST(WMSpecCreation, CreateWithNullBackendReturnsInvalidArgument) {
    auto result = WindowManager::Create(nullptr);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ErrorCode::InvalidArgument);
}

TEST(ZWMSpecCreation, MovedFromManagerIsEmpty) {
    auto backend = std::make_unique<GlfwWindowBackend>(miki::rhi::BackendType::WebGPU, false);
    auto wm = std::move(*WindowManager::Create(std::move(backend)));
    (void)wm.CreateWindow({.width = 100, .height = 100, .flags = WindowFlags::Hidden});
    auto wm2 = std::move(wm);
    // wm is moved-from — should report 0 windows, queries return defaults
    EXPECT_EQ(wm.GetWindowCount(), 0u);
    EXPECT_EQ(wm2.GetWindowCount(), 1u);
}

TEST(ZWMSpecCreation, DestructorSafeOnMovedFrom) {
    auto backend = std::make_unique<GlfwWindowBackend>(miki::rhi::BackendType::WebGPU, false);
    auto wm = std::move(*WindowManager::Create(std::move(backend)));
    auto wm2 = std::move(wm);
    // wm destructor called here — must not crash
}

TEST(ZWMSpecCreation, TwoManagersCoexist) {
    auto b1 = std::make_unique<GlfwWindowBackend>(miki::rhi::BackendType::WebGPU, false);
    auto b2 = std::make_unique<GlfwWindowBackend>(miki::rhi::BackendType::WebGPU, false);
    auto wm1 = std::move(*WindowManager::Create(std::move(b1)));
    auto wm2 = std::move(*WindowManager::Create(std::move(b2)));
    auto h1 = *wm1.CreateWindow({.width = 100, .height = 100, .flags = WindowFlags::Hidden});
    auto h2 = *wm2.CreateWindow({.width = 200, .height = 200, .flags = WindowFlags::Hidden});
    EXPECT_EQ(wm1.GetWindowCount(), 1u);
    EXPECT_EQ(wm2.GetWindowCount(), 1u);
    EXPECT_TRUE(wm1.GetWindowInfo(h1).alive);
    EXPECT_TRUE(wm2.GetWindowInfo(h2).alive);
}

// ============================================================================
// SS13: WindowHandle Identity & Generation
// ============================================================================

TEST_F(WMSpecTest, FreshHandleIsValid) {
    auto h = MakeWindow();
    EXPECT_TRUE(h.IsValid());
    EXPECT_GE(h.generation, 1u);
}

TEST_F(WMSpecTest, NHandlesArePairwiseDistinct) {
    constexpr int N = 8;
    std::vector<WindowHandle> handles;
    for (int i = 0; i < N; ++i) {
        handles.push_back(MakeWindow("W" + std::to_string(i)));
    }
    for (int i = 0; i < N; ++i) {
        for (int j = i + 1; j < N; ++j) {
            EXPECT_NE(handles[i].id, handles[j].id) << "Handles " << i << " and " << j << " collide";
        }
    }
}

TEST_F(WMSpecTest, GenerationIncrementsAfterDestroy) {
    auto h1 = MakeWindow();
    uint16_t gen1 = h1.generation;
    uint32_t id1 = h1.id;
    (void)wm_->DestroyWindow(h1);
    // Create again — if same slot is reused, generation must differ
    auto h2 = MakeWindow();
    if (h2.id == id1) {
        EXPECT_GT(h2.generation, gen1);
    }
}

TEST_F(WMSpecTest, StaleHandleRejectedByAllOps) {
    auto h = MakeWindow();
    auto stale = h;
    (void)wm_->DestroyWindow(h);
    // All queries on stale handle should fail or return defaults
    EXPECT_EQ(wm_->GetParent(stale), WindowHandle{});
    EXPECT_TRUE(wm_->GetChildren(stale).empty());
    EXPECT_EQ(wm_->GetNativeToken(stale), nullptr);
    EXPECT_FALSE(wm_->GetWindowInfo(stale).alive);
    EXPECT_FALSE(wm_->DestroyWindow(stale).has_value());
}

TEST_F(WMSpecTest, ReuseSlotNewGeneration) {
    auto h1 = MakeWindow();
    auto id1 = h1.id;
    auto gen1 = h1.generation;
    (void)wm_->DestroyWindow(h1);
    auto h2 = MakeWindow();
    // If allocator reuses slot, generation must differ
    if (h2.id == id1) {
        EXPECT_NE(h2.generation, gen1);
        // Old handle is still invalid
        EXPECT_FALSE(wm_->GetWindowInfo(h1).alive);
    }
}

TEST_F(WMSpecTest, NullHandleRejected) {
    WindowHandle null{};
    EXPECT_FALSE(null.IsValid());
    EXPECT_EQ(wm_->GetParent(null), WindowHandle{});
    EXPECT_TRUE(wm_->GetChildren(null).empty());
    EXPECT_FALSE(wm_->GetWindowInfo(null).alive);
    EXPECT_FALSE(wm_->DestroyWindow(null).has_value());
}

TEST_F(WMSpecTest, HandleEqualityConsidersBothIdAndGeneration) {
    WindowHandle a{.id = 1, .generation = 1};
    WindowHandle b{.id = 1, .generation = 2};
    EXPECT_NE(a, b);
    WindowHandle c{.id = 1, .generation = 1};
    EXPECT_EQ(a, c);
}

TEST_F(WMSpecTest, MassCreateDestroyNoAliasing) {
    // Create and destroy 1000 times — no UB, no aliasing
    for (int i = 0; i < 1000; ++i) {
        auto h = MakeWindow("Loop");
        ASSERT_TRUE(h.IsValid()) << "Failed at iteration " << i;
        auto r = wm_->DestroyWindow(h);
        ASSERT_TRUE(r.has_value()) << "Destroy failed at iteration " << i;
        // Stale handle must be rejected
        EXPECT_FALSE(wm_->GetWindowInfo(h).alive);
    }
}

// ============================================================================
// SS13: Window Tree — Topology
// ============================================================================

TEST_F(WMSpecTest, RootWindowHasNoParent) {
    auto root = MakeWindow("Root");
    EXPECT_EQ(wm_->GetParent(root), WindowHandle{});
    EXPECT_EQ(wm_->GetDepth(root), 1u);
}

TEST_F(WMSpecTest, ChildHasCorrectParent) {
    auto root = MakeWindow("Root");
    auto child = MakeWindow("Child", 200, 150, root);
    EXPECT_EQ(wm_->GetParent(child), root);
    EXPECT_EQ(wm_->GetDepth(child), 2u);
}

TEST_F(WMSpecTest, FourLevelChainDepths) {
    auto r = MakeWindow("R");
    auto c = MakeWindow("C", 200, 150, r);
    auto gc = MakeWindow("GC", 100, 75, c);
    auto ggc = MakeWindow("GGC", 50, 50, gc);
    EXPECT_EQ(wm_->GetDepth(r), 1u);
    EXPECT_EQ(wm_->GetDepth(c), 2u);
    EXPECT_EQ(wm_->GetDepth(gc), 3u);
    EXPECT_EQ(wm_->GetDepth(ggc), 4u);
    EXPECT_EQ(wm_->GetRoot(ggc), r);
}

TEST_F(WMSpecTest, ExceedMaxDepthFails) {
    WindowHandle prev = MakeWindow("D1");
    for (uint32_t i = 1; i < WindowManager::kMaxDepth; ++i) {
        auto r = wm_->CreateWindow({.width = 100, .height = 100, .parent = prev, .flags = WindowFlags::Hidden});
        ASSERT_TRUE(r.has_value()) << "Depth " << (i + 1) << " should succeed";
        prev = *r;
    }
    auto tooDeep = wm_->CreateWindow({.width = 100, .height = 100, .parent = prev, .flags = WindowFlags::Hidden});
    EXPECT_FALSE(tooDeep.has_value());
    EXPECT_EQ(tooDeep.error(), ErrorCode::InvalidArgument);
}

TEST_F(WMSpecTest, ChildOfDestroyedParentFails) {
    auto root = MakeWindow("Root");
    (void)wm_->DestroyWindow(root);
    auto child = wm_->CreateWindow({.width = 100, .height = 100, .parent = root, .flags = WindowFlags::Hidden});
    EXPECT_FALSE(child.has_value());
    EXPECT_EQ(child.error(), ErrorCode::InvalidArgument);
}

TEST_F(WMSpecTest, GetChildrenCreationOrder) {
    auto root = MakeWindow("Root");
    auto c1 = MakeWindow("C1", 100, 100, root);
    auto c2 = MakeWindow("C2", 100, 100, root);
    auto c3 = MakeWindow("C3", 100, 100, root);
    auto children = wm_->GetChildren(root);
    ASSERT_EQ(children.size(), 3u);
    EXPECT_EQ(children[0], c1);
    EXPECT_EQ(children[1], c2);
    EXPECT_EQ(children[2], c3);
}

TEST_F(WMSpecTest, MultipleRootsIndependentTrees) {
    auto r1 = MakeWindow("R1");
    auto r2 = MakeWindow("R2");
    auto c1 = MakeWindow("C1", 100, 100, r1);
    auto c2 = MakeWindow("C2", 100, 100, r2);
    auto roots = wm_->GetRootWindows();
    EXPECT_EQ(roots.size(), 2u);
    EXPECT_EQ(wm_->GetRoot(c1), r1);
    EXPECT_EQ(wm_->GetRoot(c2), r2);
}

TEST_F(WMSpecTest, GetAllWindowsMatchesCount) {
    auto r = MakeWindow("R");
    (void)MakeWindow("C1", 100, 100, r);
    (void)MakeWindow("C2", 100, 100, r);
    auto all = wm_->GetAllWindows();
    EXPECT_EQ(all.size(), wm_->GetWindowCount());
    EXPECT_EQ(all.size(), 3u);
}

// ============================================================================
// SS13: Window Tree — Post-Order Traversal
// ============================================================================

TEST_F(WMSpecTest, PostOrderLeafReturnsEmpty) {
    auto leaf = MakeWindow("Leaf");
    auto desc = wm_->GetDescendantsPostOrder(leaf);
    EXPECT_TRUE(desc.empty());
}

TEST_F(WMSpecTest, PostOrderSingleChild) {
    auto parent = MakeWindow("P");
    auto child = MakeWindow("C", 100, 100, parent);
    auto desc = wm_->GetDescendantsPostOrder(parent);
    ASSERT_EQ(desc.size(), 1u);
    EXPECT_EQ(desc[0], child);
}

TEST_F(WMSpecTest, PostOrderComplexTree) {
    // root -> {A, B}, A -> {A1, A2}, B -> {B1}
    auto root = MakeWindow("root");
    auto A = MakeWindow("A", 100, 100, root);
    auto B = MakeWindow("B", 100, 100, root);
    auto A1 = MakeWindow("A1", 100, 100, A);
    auto A2 = MakeWindow("A2", 100, 100, A);
    auto B1 = MakeWindow("B1", 100, 100, B);
    auto desc = wm_->GetDescendantsPostOrder(root);
    ASSERT_EQ(desc.size(), 5u);
    // Every node appears after all its descendants
    auto posOf = [&](WindowHandle h) { return std::distance(desc.begin(), std::find(desc.begin(), desc.end(), h)); };
    EXPECT_LT(posOf(A1), posOf(A));
    EXPECT_LT(posOf(A2), posOf(A));
    EXPECT_LT(posOf(B1), posOf(B));
    EXPECT_LT(posOf(A), posOf(root));  // root should not be in desc, but A < B in desc
    // root is NOT in desc
    EXPECT_EQ(std::find(desc.begin(), desc.end(), root), desc.end());
}

TEST_F(WMSpecTest, PostOrderDoesNotIncludeSelf) {
    auto root = MakeWindow("root");
    (void)MakeWindow("C", 100, 100, root);
    auto desc = wm_->GetDescendantsPostOrder(root);
    EXPECT_EQ(std::find(desc.begin(), desc.end(), root), desc.end());
}

TEST_F(WMSpecTest, PostOrderIsDeterministic) {
    auto root = MakeWindow("root");
    (void)MakeWindow("C1", 100, 100, root);
    (void)MakeWindow("C2", 100, 100, root);
    auto d1 = wm_->GetDescendantsPostOrder(root);
    auto d2 = wm_->GetDescendantsPostOrder(root);
    EXPECT_EQ(d1, d2);
}

// ============================================================================
// SS13: Window Lifecycle Operations
// ============================================================================

TEST_F(WMSpecTest, CreateDefaultWindowIsRootVisible) {
    auto h = *wm_->CreateWindow({.width = 320, .height = 240});
    EXPECT_EQ(wm_->GetParent(h), WindowHandle{});
    auto info = wm_->GetWindowInfo(h);
    EXPECT_TRUE(info.alive);
    (void)wm_->DestroyWindow(h);
}

TEST_F(WMSpecTest, HiddenWindowCreation) {
    auto h = MakeWindow("Hidden", 320, 240, {}, WindowFlags::Hidden);
    auto info = wm_->GetWindowInfo(h);
    EXPECT_TRUE(info.alive);
    EXPECT_TRUE(info.flags.Has(WindowFlags::Hidden));
}

TEST_F(WMSpecTest, ShowHideIdempotent) {
    auto h = MakeWindow("SH", 200, 150, {}, WindowFlags::Hidden);
    wm_->ShowWindow(h);
    wm_->ShowWindow(h);  // no-op, no error
    wm_->HideWindow(h);
    wm_->HideWindow(h);  // no-op, no error
}

TEST_F(WMSpecTest, GetWindowInfoReturnsCorrectData) {
    auto h = MakeWindow("Info", 400, 300);
    auto info = wm_->GetWindowInfo(h);
    EXPECT_EQ(info.handle, h);
    EXPECT_TRUE(info.alive);
    EXPECT_FALSE(info.minimized);
}

TEST_F(WMSpecTest, GetNativeHandleNonNull) {
    auto h = MakeWindow();
    auto nh = wm_->GetNativeHandle(h);
    // On Win32, this should be a valid Win32Window
    EXPECT_TRUE(std::holds_alternative<miki::rhi::Win32Window>(nh));
}

TEST_F(WMSpecTest, GetNativeTokenNonNull) {
    auto h = MakeWindow();
    EXPECT_NE(wm_->GetNativeToken(h), nullptr);
}

TEST_F(WMSpecTest, DynamicCapacity17Windows) {
    std::vector<WindowHandle> handles;
    for (uint32_t i = 0; i < 17; ++i) {
        auto r = wm_->CreateWindow({.width = 100, .height = 100, .flags = WindowFlags::Hidden});
        ASSERT_TRUE(r.has_value()) << "Failed at window " << i;
        handles.push_back(*r);
    }
    EXPECT_EQ(wm_->GetWindowCount(), 17u);
    for (auto& h : handles) {
        (void)wm_->DestroyWindow(h);
    }
}

TEST_F(WMSpecTest, DynamicCapacity256Windows) {
    std::vector<WindowHandle> handles;
    for (uint32_t i = 0; i < 256; ++i) {
        auto r = wm_->CreateWindow({.width = 64, .height = 64, .flags = WindowFlags::Hidden});
        ASSERT_TRUE(r.has_value()) << "Failed at window " << i;
        handles.push_back(*r);
    }
    EXPECT_EQ(wm_->GetWindowCount(), 256u);
    for (auto& h : handles) {
        (void)wm_->DestroyWindow(h);
    }
}

TEST_F(WMSpecTest, SlotRecycleAfterDestroy) {
    auto h1 = MakeWindow();
    (void)wm_->DestroyWindow(h1);
    auto h2 = MakeWindow();
    EXPECT_TRUE(h2.IsValid());
    EXPECT_EQ(wm_->GetWindowCount(), 1u);
}

// ============================================================================
// SS13: Window State Operations
// ============================================================================

TEST_F(WMSpecTest, ResizeChangesExtent) {
    auto h = MakeWindow("Resize", 400, 300);
    wm_->ResizeWindow(h, 800, 600);
    wm_->PollEvents();
    auto info = wm_->GetWindowInfo(h);
    EXPECT_EQ(info.extent.width, 800u);
    EXPECT_EQ(info.extent.height, 600u);
}

TEST_F(WMSpecTest, ResizeZeroGraceful) {
    auto h = MakeWindow("ResizeZero", 400, 300);
    wm_->ResizeWindow(h, 0, 0);  // Should not crash
    wm_->PollEvents();
}

TEST_F(WMSpecTest, SetAndGetPosition) {
    auto h = MakeWindow("Pos", 200, 150);
    wm_->ShowWindow(h);
    wm_->SetWindowPosition(h, 100, 200);
    auto [x, y] = wm_->GetWindowPosition(h);
    EXPECT_EQ(x, 100);
    EXPECT_EQ(y, 200);
}

TEST_F(WMSpecTest, NegativePositionValid) {
    auto h = MakeWindow("NegPos", 200, 150);
    wm_->ShowWindow(h);
    wm_->SetWindowPosition(h, -50, -30);
    auto [x, y] = wm_->GetWindowPosition(h);
    EXPECT_EQ(x, -50);
    EXPECT_EQ(y, -30);
}

TEST_F(WMSpecTest, MinimizeAndGetInfo) {
    auto h = MakeWindow("Min", 300, 200);
    wm_->ShowWindow(h);
    wm_->MinimizeWindow(h);
    wm_->PollEvents();
    auto info = wm_->GetWindowInfo(h);
    EXPECT_TRUE(info.minimized);
}

TEST_F(WMSpecTest, MinimizeIdempotent) {
    auto h = MakeWindow("MinIdem", 300, 200);
    wm_->ShowWindow(h);
    wm_->MinimizeWindow(h);
    wm_->MinimizeWindow(h);  // no-op, no error
    wm_->PollEvents();
}

TEST_F(WMSpecTest, MaximizeAndGetInfo) {
    auto h = MakeWindow("Max", 300, 200);
    wm_->ShowWindow(h);
    wm_->MaximizeWindow(h);
    wm_->PollEvents();
    auto info = wm_->GetWindowInfo(h);
    EXPECT_TRUE(info.maximized);
}

TEST_F(WMSpecTest, MaximizeIdempotent) {
    auto h = MakeWindow("MaxIdem", 300, 200);
    wm_->ShowWindow(h);
    wm_->MaximizeWindow(h);
    wm_->MaximizeWindow(h);  // no-op
    wm_->PollEvents();
}

TEST_F(WMSpecTest, RestoreFromMinimized) {
    auto h = MakeWindow("RestoreMin", 300, 200);
    wm_->ShowWindow(h);
    wm_->MinimizeWindow(h);
    wm_->PollEvents();
    wm_->RestoreWindow(h);
    wm_->PollEvents();
    auto info = wm_->GetWindowInfo(h);
    EXPECT_FALSE(info.minimized);
}

TEST_F(WMSpecTest, RestoreFromMaximized) {
    auto h = MakeWindow("RestoreMax", 300, 200);
    wm_->ShowWindow(h);
    wm_->MaximizeWindow(h);
    wm_->PollEvents();
    wm_->RestoreWindow(h);
    wm_->PollEvents();
    auto info = wm_->GetWindowInfo(h);
    EXPECT_FALSE(info.maximized);
}

TEST_F(WMSpecTest, RestoreOnNormalIsNoop) {
    auto h = MakeWindow("RestoreNorm", 300, 200);
    wm_->ShowWindow(h);
    wm_->RestoreWindow(h);  // no-op
    auto info = wm_->GetWindowInfo(h);
    EXPECT_FALSE(info.minimized);
    EXPECT_FALSE(info.maximized);
}

TEST_F(WMSpecTest, SetWindowTitle) {
    auto h = MakeWindow("Title", 200, 150);
    wm_->SetWindowTitle(h, "New Title");
    // No crash = success (title is backend-stored)
}

TEST_F(WMSpecTest, SetWindowTitleEmpty) {
    auto h = MakeWindow("TitleEmpty", 200, 150);
    wm_->SetWindowTitle(h, "");
}

TEST_F(WMSpecTest, SetWindowTitleUtf8) {
    auto h = MakeWindow("TitleUTF8", 200, 150);
    wm_->SetWindowTitle(h, "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");  // 日本語
}

TEST_F(WMSpecTest, StateOpsOnStaleHandleSilent) {
    auto h = MakeWindow("Stale", 200, 150);
    (void)wm_->DestroyWindow(h);
    // All state ops on stale handle should be silent (no crash)
    wm_->ShowWindow(h);
    wm_->HideWindow(h);
    wm_->ResizeWindow(h, 100, 100);
    wm_->SetWindowPosition(h, 0, 0);
    wm_->MinimizeWindow(h);
    wm_->MaximizeWindow(h);
    wm_->RestoreWindow(h);
    wm_->FocusWindow(h);
    wm_->SetWindowTitle(h, "Ghost");
}

// ============================================================================
// SS13: Cascade Destruction
// ============================================================================

TEST_F(WMSpecTest, DestroyLeafReturnsSingleHandle) {
    auto leaf = MakeWindow("Leaf");
    auto r = wm_->DestroyWindow(leaf);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 1u);
    EXPECT_EQ((*r)[0], leaf);
}

TEST_F(WMSpecTest, CascadeDestroyPostOrder) {
    auto root = MakeWindow("R");
    auto c1 = MakeWindow("C1", 100, 100, root);
    (void)MakeWindow("C2", 100, 100, root);
    auto gc1 = MakeWindow("GC1", 50, 50, c1);
    auto r = wm_->DestroyWindow(root);
    ASSERT_TRUE(r.has_value());
    auto& v = *r;
    ASSERT_EQ(v.size(), 4u);
    EXPECT_EQ(v.back(), root);  // root last
    auto pos = [&](WindowHandle h) { return std::distance(v.begin(), std::find(v.begin(), v.end(), h)); };
    EXPECT_LT(pos(gc1), pos(c1));  // gc1 before c1
}

TEST_F(WMSpecTest, CascadeAllHandlesStale) {
    auto root = MakeWindow("R");
    (void)MakeWindow("C", 100, 100, root);
    auto r = wm_->DestroyWindow(root);
    ASSERT_TRUE(r.has_value());
    for (auto& h : *r) {
        EXPECT_FALSE(wm_->GetWindowInfo(h).alive);
    }
}

TEST_F(WMSpecTest, CascadePreservesSiblings) {
    auto root = MakeWindow("R");
    auto c1 = MakeWindow("C1", 100, 100, root);
    auto c2 = MakeWindow("C2", 100, 100, root);
    (void)wm_->DestroyWindow(c1);
    EXPECT_TRUE(wm_->GetWindowInfo(root).alive);
    EXPECT_TRUE(wm_->GetWindowInfo(c2).alive);
    auto children = wm_->GetChildren(root);
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], c2);
}

TEST_F(WMSpecTest, DestroyRootReducesCount) {
    auto root = MakeWindow("R");
    (void)MakeWindow("C1", 100, 100, root);
    (void)MakeWindow("C2", 100, 100, root);
    EXPECT_EQ(wm_->GetWindowCount(), 3u);
    (void)wm_->DestroyWindow(root);
    EXPECT_EQ(wm_->GetWindowCount(), 0u);
}

TEST_F(WMSpecTest, DestroyAlreadyDestroyedFails) {
    auto h = MakeWindow();
    (void)wm_->DestroyWindow(h);
    auto r = wm_->DestroyWindow(h);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::InvalidArgument);
}

TEST_F(WMSpecTest, DestroyNullHandleFails) {
    auto r = wm_->DestroyWindow(WindowHandle{});
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::InvalidArgument);
}

TEST_F(WMSpecTest, DestroyMultipleIndependentRoots) {
    auto r1 = MakeWindow("R1");
    auto r2 = MakeWindow("R2");
    auto r3 = MakeWindow("R3");
    (void)wm_->DestroyWindow(r1);
    EXPECT_EQ(wm_->GetWindowCount(), 2u);
    (void)wm_->DestroyWindow(r2);
    EXPECT_EQ(wm_->GetWindowCount(), 1u);
    (void)wm_->DestroyWindow(r3);
    EXPECT_EQ(wm_->GetWindowCount(), 0u);
}

TEST_F(WMSpecTest, DestroyParentWhileChildHidden) {
    auto root = MakeWindow("R");
    auto child = MakeWindow("HiddenChild", 100, 100, root, WindowFlags::Hidden);
    wm_->HideWindow(child);
    auto r = wm_->DestroyWindow(root);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 2u);
    EXPECT_FALSE(wm_->GetWindowInfo(child).alive);
}

TEST_F(WMSpecTest, DestroyChildDoesNotAffectParent) {
    auto parent = MakeWindow("P");
    auto child = MakeWindow("C", 100, 100, parent);
    (void)wm_->DestroyWindow(child);
    EXPECT_TRUE(wm_->GetWindowInfo(parent).alive);
}

TEST_F(WMSpecTest, DestroyChildPreservesSiblingOrder) {
    auto root = MakeWindow("R");
    auto a = MakeWindow("A", 100, 100, root);
    auto b = MakeWindow("B", 100, 100, root);
    auto c = MakeWindow("C", 100, 100, root);
    (void)wm_->DestroyWindow(b);
    auto children = wm_->GetChildren(root);
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], a);
    EXPECT_EQ(children[1], c);
}

// ============================================================================
// SS13: IWindowBackend Contract
// ============================================================================

TEST_F(WMSpecTest, BackendParentTokenForwarded) {
    auto root = MakeWindow("Root");
    auto rootToken = wm_->GetNativeToken(root);
    auto child = MakeWindow("Child", 200, 150, root);
    auto childToken = wm_->GetNativeToken(child);
    EXPECT_NE(rootToken, nullptr);
    EXPECT_NE(childToken, nullptr);
    EXPECT_NE(rootToken, childToken);
}

TEST_F(WMSpecTest, PollEventsReturnsEmptyWhenNoEvents) {
    (void)MakeWindow();
    auto events = wm_->PollEvents();
    // In headless mode, no OS events should be pending
    // (events.size() may be 0 or contain focus events from window creation)
    (void)events;
}

TEST_F(WMSpecTest, ShouldCloseWhenNoWindows) {
    EXPECT_TRUE(wm_->ShouldClose());
    auto h = MakeWindow();
    EXPECT_FALSE(wm_->ShouldClose());
    (void)wm_->DestroyWindow(h);
    EXPECT_TRUE(wm_->ShouldClose());
}

// ============================================================================
// SS13: Window Event Dispatch
// ============================================================================

TEST_F(WMSpecTest, PollEventsEmptySpan) {
    auto events = wm_->PollEvents();
    // No windows = no events
    EXPECT_TRUE(events.empty());
}

TEST_F(WMSpecTest, PollEventsZeroWindowsNotError) {
    auto events = wm_->PollEvents();
    EXPECT_TRUE(events.empty());  // Not an error, just empty
}

// ============================================================================
// SS13: WindowFlags Behavior
// ============================================================================

TEST_F(WMSpecTest, BorderlessFlagPreserved) {
    auto h = MakeWindow("BL", 200, 150, {}, WindowFlags::Borderless | WindowFlags::Hidden);
    auto info = wm_->GetWindowInfo(h);
    EXPECT_TRUE(info.flags.Has(WindowFlags::Borderless));
}

TEST_F(WMSpecTest, AlwaysOnTopFlagPreserved) {
    auto h = MakeWindow("AOT", 200, 150, {}, WindowFlags::AlwaysOnTop | WindowFlags::Hidden);
    auto info = wm_->GetWindowInfo(h);
    EXPECT_TRUE(info.flags.Has(WindowFlags::AlwaysOnTop));
}

TEST_F(WMSpecTest, NoResizeFlagPreserved) {
    auto h = MakeWindow("NR", 200, 150, {}, WindowFlags::NoResize | WindowFlags::Hidden);
    auto info = wm_->GetWindowInfo(h);
    EXPECT_TRUE(info.flags.Has(WindowFlags::NoResize));
}

TEST_F(WMSpecTest, CombinedFlagsPreserved) {
    auto flags = WindowFlags::Borderless | WindowFlags::AlwaysOnTop | WindowFlags::Hidden;
    auto h = MakeWindow("CF", 200, 150, {}, flags);
    auto info = wm_->GetWindowInfo(h);
    EXPECT_TRUE(info.flags.Has(WindowFlags::Borderless));
    EXPECT_TRUE(info.flags.Has(WindowFlags::AlwaysOnTop));
    EXPECT_TRUE(info.flags.Has(WindowFlags::Hidden));
}

// ============================================================================
// SS13: Error Handling & Robustness
// ============================================================================

TEST_F(WMSpecTest, StaleHandleReturnsInvalidArgument) {
    auto h = MakeWindow();
    (void)wm_->DestroyWindow(h);
    auto r = wm_->DestroyWindow(h);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::InvalidArgument);
}

TEST_F(WMSpecTest, DepthExceedReturnsInvalidArgument) {
    WindowHandle prev = MakeWindow("D1");
    for (uint32_t i = 1; i < WindowManager::kMaxDepth; ++i) {
        prev = *wm_->CreateWindow({.width = 64, .height = 64, .parent = prev, .flags = WindowFlags::Hidden});
    }
    auto r = wm_->CreateWindow({.width = 64, .height = 64, .parent = prev, .flags = WindowFlags::Hidden});
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), ErrorCode::InvalidArgument);
}

#ifdef NDEBUG
// Debug builds use assert() for this check, so only test in release mode
TEST_F(WMSpecTest, HasSurfaceCallbackBlocksDestroy) {
    auto h = MakeWindow();
    // Simulate a surface callback that says "yes, surface attached"
    wm_->SetHasSurfaceCallback([](WindowHandle) -> bool { return true; });
    auto r = wm_->DestroyWindow(h);
    EXPECT_FALSE(r.has_value());
    // Clean up by removing callback and destroying
    wm_->SetHasSurfaceCallback(nullptr);
    auto r2 = wm_->DestroyWindow(h);
    EXPECT_TRUE(r2.has_value());
}
#endif

// ============================================================================
// SS13.1 Integration: Max-Depth Tree Operations
// ============================================================================

TEST_F(WMSpecTest, MaxDepthTreeFullTest) {
    auto root = MakeWindow("D1");
    auto child = MakeWindow("D2", 100, 100, root);
    auto gc = MakeWindow("D3", 100, 100, child);
    auto ggc = MakeWindow("D4", 100, 100, gc);

    EXPECT_EQ(wm_->GetDepth(root), 1u);
    EXPECT_EQ(wm_->GetDepth(ggc), 4u);
    EXPECT_EQ(wm_->GetRoot(ggc), root);

    auto desc = wm_->GetDescendantsPostOrder(root);
    ASSERT_EQ(desc.size(), 3u);
    EXPECT_EQ(desc[0], ggc);
    EXPECT_EQ(desc[1], gc);
    EXPECT_EQ(desc[2], child);

    // Attempt depth 5 — must fail
    auto tooDeep = wm_->CreateWindow({.width = 64, .height = 64, .parent = ggc, .flags = WindowFlags::Hidden});
    EXPECT_FALSE(tooDeep.has_value());

    // Destroy grandchild — great-grandchild also destroyed, child/root survive
    (void)wm_->DestroyWindow(gc);
    EXPECT_FALSE(wm_->GetWindowInfo(gc).alive);
    EXPECT_FALSE(wm_->GetWindowInfo(ggc).alive);
    EXPECT_TRUE(wm_->GetWindowInfo(child).alive);
    EXPECT_TRUE(wm_->GetWindowInfo(root).alive);
    EXPECT_EQ(wm_->GetDepth(child), 2u);
}

// ============================================================================
// SS13.1 Integration: Handle Generation Wraparound
// ============================================================================

TEST_F(WMSpecTest, HandleGenerationWraparound) {
    // Create/destroy many times to stress generation counting
    constexpr int kIterations = 2000;
    for (int i = 0; i < kIterations; ++i) {
        auto h = MakeWindow("Wrap");
        ASSERT_TRUE(h.IsValid()) << "Failed at iteration " << i;
        auto r = wm_->DestroyWindow(h);
        ASSERT_TRUE(r.has_value()) << "Destroy failed at iteration " << i;
        EXPECT_FALSE(wm_->GetWindowInfo(h).alive);
    }
    // After all, create 3 windows and verify all distinct
    auto a = MakeWindow("A");
    auto b = MakeWindow("B");
    auto c = MakeWindow("C");
    EXPECT_NE(a, b);
    EXPECT_NE(b, c);
    EXPECT_NE(a, c);
    EXPECT_EQ(wm_->GetWindowCount(), 3u);
}

// ============================================================================
// SS13.1 Integration: Event Ordering Under Concurrent Window Operations
// ============================================================================

TEST_F(WMSpecTest, EventOrderingWithWindowOps) {
    // Use the EventSimulator to inject events
    auto* sim = dynamic_cast<GlfwWindowBackend*>(wm_->GetBackend());
    if (!sim) {
        GTEST_SKIP() << "Need GlfwWindowBackend for event injection";
    }

    std::vector<WindowHandle> windows;
    for (int i = 0; i < 4; ++i) {
        windows.push_back(MakeWindow("EO" + std::to_string(i)));
    }

    // Inject events for all windows
    auto& pending = sim->GetPendingEvents();
    for (int i = 0; i < 20; ++i) {
        for (auto& w : windows) {
            pending.push_back({w, neko::platform::Focus{.focused = true}});
        }
    }

    // Destroy one window mid-stream
    (void)wm_->DestroyWindow(windows[1]);

    auto events = wm_->PollEvents();
    // Events for destroyed window should be filtered
    for (auto& evt : events) {
        EXPECT_NE(evt.window, windows[1]) << "Got event for destroyed window";
    }
}

// ============================================================================
// SS13.1 Integration: Error Recovery Chain
// ============================================================================

TEST_F(WMSpecTest, ErrorRecoveryChain) {
    // (1) Destroy null handle — error, other windows unaffected
    auto h1 = MakeWindow("Good1");
    auto r1 = wm_->DestroyWindow(WindowHandle{});
    EXPECT_FALSE(r1.has_value());
    EXPECT_TRUE(wm_->GetWindowInfo(h1).alive);

    // (2) Create 17 windows to test dynamic expansion
    std::vector<WindowHandle> handles;
    handles.push_back(h1);
    for (uint32_t i = 1; i < 17; ++i) {
        auto r = wm_->CreateWindow({.width = 64, .height = 64, .flags = WindowFlags::Hidden});
        ASSERT_TRUE(r.has_value()) << "Failed at window " << i;
        handles.push_back(*r);
    }
    EXPECT_EQ(wm_->GetWindowCount(), 17u);

    // (3) Clean shutdown — destroy all
    for (auto& h : handles) {
        (void)wm_->DestroyWindow(h);
    }
    EXPECT_EQ(wm_->GetWindowCount(), 0u);
}
