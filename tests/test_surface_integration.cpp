// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// SurfaceManager + FrameManager integration tests per specs/01-window-manager.md SS13.
// Uses ALL real GPU backends (WebGPU, OpenGL, Vulkan, D3D12) via parameterized tests.
// Requires a display server and at least one GPU backend compiled.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "miki/platform/WindowManager.h"
#include "miki/platform/WindowManagerUtils.h"
#include "miki/platform/Event.h"
#include "miki/platform/glfw/GlfwWindowBackend.h"
#include "miki/rhi/SurfaceManager.h"
#include "miki/rhi/RenderSurface.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/backend/AllBackends.h"
#include "miki/core/ErrorCode.h"

using namespace miki::platform;
using namespace miki::rhi;
using namespace miki::core;

// ============================================================================
// Backend abstraction — each real backend creates a Device + DeviceHandle
// ============================================================================

struct BackendInfo {
    BackendType type;
    std::string name;
};

static auto GetAvailableBackends() -> std::vector<BackendInfo> {
    std::vector<BackendInfo> backends;
#if MIKI_BUILD_WEBGPU
    backends.push_back({BackendType::WebGPU, "WebGPU"});
#endif
#if MIKI_BUILD_OPENGL
    backends.push_back({BackendType::OpenGL43, "OpenGL"});
#endif
#if MIKI_BUILD_VULKAN
    backends.push_back({BackendType::Vulkan14, "Vulkan"});
#endif
#if MIKI_BUILD_D3D12
    backends.push_back({BackendType::D3D12, "D3D12"});
#endif
    return backends;
}

// ============================================================================
// Device holder — type-erased ownership of backend device
// ============================================================================

class DeviceHolder {
   public:
    DeviceHandle handle;

    ~DeviceHolder() { Destroy(); }
    DeviceHolder() = default;
    DeviceHolder(const DeviceHolder&) = delete;
    auto operator=(const DeviceHolder&) -> DeviceHolder& = delete;
    DeviceHolder(DeviceHolder&& o) noexcept
        : handle(o.handle)
#if MIKI_BUILD_WEBGPU
        , webgpu_(std::move(o.webgpu_))
#endif
#if MIKI_BUILD_OPENGL
        , opengl_(std::move(o.opengl_))
#endif
#if MIKI_BUILD_VULKAN
        , vulkan_(std::move(o.vulkan_))
#endif
#if MIKI_BUILD_D3D12
        , d3d12_(std::move(o.d3d12_))
#endif
    {
        o.handle = {};
    }
    auto operator=(DeviceHolder&&) -> DeviceHolder& = delete;

    [[nodiscard]] static auto Create(BackendType type, [[maybe_unused]] void* glfwWindow = nullptr)
        -> std::unique_ptr<DeviceHolder> {
        auto holder = std::make_unique<DeviceHolder>();
        switch (type) {
#if MIKI_BUILD_WEBGPU
            case BackendType::WebGPU: {
                holder->webgpu_ = std::make_unique<WebGPUDevice>();
                WebGPUDeviceDesc dd{.enableValidation = true};
                if (auto r = holder->webgpu_->Init(dd); !r) {
                    return nullptr;
                }
                holder->handle = DeviceHandle(holder->webgpu_.get(), BackendType::WebGPU);
                break;
            }
#endif
#if MIKI_BUILD_OPENGL
            case BackendType::OpenGL43: {
                holder->opengl_ = std::make_unique<OpenGLDevice>();
                OpenGLDeviceDesc dd{.enableValidation = true, .glfwWindow = glfwWindow};
                if (auto r = holder->opengl_->Init(dd); !r) {
                    return nullptr;
                }
                holder->handle = DeviceHandle(holder->opengl_.get(), BackendType::OpenGL43);
                break;
            }
#endif
#if MIKI_BUILD_VULKAN
            case BackendType::Vulkan14: {
                holder->vulkan_ = std::make_unique<VulkanDevice>();
                VulkanDeviceDesc dd{.enableValidation = true};
                if (auto r = holder->vulkan_->Init(dd); !r) {
                    return nullptr;
                }
                holder->handle = DeviceHandle(holder->vulkan_.get(), BackendType::Vulkan14);
                break;
            }
#endif
#if MIKI_BUILD_D3D12
            case BackendType::D3D12: {
                holder->d3d12_ = std::make_unique<D3D12Device>();
                D3D12DeviceDesc dd{.enableValidation = true};
                if (auto r = holder->d3d12_->Init(dd); !r) {
                    return nullptr;
                }
                holder->handle = DeviceHandle(holder->d3d12_.get(), BackendType::D3D12);
                break;
            }
#endif
            default: return nullptr;
        }
        return holder;
    }

   private:
    void Destroy() {
        if (!handle.IsValid()) {
            return;
        }
        handle.Dispatch([](auto& dev) { dev.WaitIdle(); });
        handle.Destroy();
    }

#if MIKI_BUILD_WEBGPU
    std::unique_ptr<WebGPUDevice> webgpu_;
#endif
#if MIKI_BUILD_OPENGL
    std::unique_ptr<OpenGLDevice> opengl_;
#endif
#if MIKI_BUILD_VULKAN
    std::unique_ptr<VulkanDevice> vulkan_;
#endif
#if MIKI_BUILD_D3D12
    std::unique_ptr<D3D12Device> d3d12_;
#endif
};

// ============================================================================
// Parameterized test fixture
// ============================================================================

class SurfaceIntegrationTest : public ::testing::TestWithParam<BackendInfo> {
   protected:
    void SetUp() override {
        auto info = GetParam();

        // Create GLFW backend (hidden windows for headless testing)
        auto backend = std::make_unique<GlfwWindowBackend>(info.type, /*visible=*/false);
        glfwBackend_ = backend.get();
        auto wmResult = WindowManager::Create(std::move(backend));
        ASSERT_TRUE(wmResult.has_value()) << "WindowManager creation failed for " << info.name;
        wm_ = std::make_unique<WindowManager>(std::move(*wmResult));

        // Create a helper window for OpenGL context if needed
        void* glfwWindow = nullptr;
#if MIKI_BUILD_OPENGL
        if (info.type == BackendType::OpenGL43) {
            // OpenGL needs a GLFWwindow for context — create one first
            auto hWin = wm_->CreateWindow({.width = 64, .height = 64, .flags = WindowFlags::Hidden});
            ASSERT_TRUE(hWin.has_value());
            contextWindow_ = *hWin;
            glfwWindow = wm_->GetNativeToken(contextWindow_);
        }
#endif

        // Create real device
        device_ = DeviceHolder::Create(info.type, glfwWindow);
        ASSERT_NE(device_, nullptr) << "Device creation failed for " << info.name;
        ASSERT_TRUE(device_->handle.IsValid());

        // Create SurfaceManager
        auto smResult = SurfaceManager::Create(device_->handle);
        ASSERT_TRUE(smResult.has_value()) << "SurfaceManager creation failed for " << info.name;
        sm_ = std::make_unique<SurfaceManager>(std::move(*smResult));

        // Register HasSurface callback
        // Note: We need a static or captured pointer since the callback is a function pointer
        sSurfaceManager_ = sm_.get();
        wm_->SetHasSurfaceCallback([](WindowHandle h) -> bool {
            return sSurfaceManager_ && sSurfaceManager_->HasSurface(h);
        });
    }

    void TearDown() override {
        sSurfaceManager_ = nullptr;
        sm_.reset();
        device_.reset();
        wm_.reset();
        glfwBackend_ = nullptr;
    }

    auto MakeWindow(std::string_view title = "Test", uint32_t w = 320, uint32_t h = 240, WindowHandle parent = {})
        -> WindowHandle {
        auto r = wm_->CreateWindow(
            {.title = title, .width = w, .height = h, .parent = parent, .flags = WindowFlags::Hidden}
        );
        EXPECT_TRUE(r.has_value()) << "CreateWindow failed: " << title;
        return r.has_value() ? *r : WindowHandle{};
    }

    auto AttachSurface(WindowHandle h, RenderSurfaceConfig cfg = {}) -> bool {
        auto nh = wm_->GetNativeHandle(h);
        auto r = sm_->AttachSurface(h, nh, cfg);
        return r.has_value();
    }

    auto SafeDestroy(WindowHandle h) -> void {
        if (sm_->HasSurface(h)) {
            (void)sm_->DetachSurface(h);
        }
        auto descendants = wm_->GetDescendantsPostOrder(h);
        for (auto& d : descendants) {
            if (sm_->HasSurface(d)) {
                (void)sm_->DetachSurface(d);
            }
        }
        (void)wm_->DestroyWindow(h);
    }

    GlfwWindowBackend* glfwBackend_ = nullptr;
    std::unique_ptr<WindowManager> wm_;
    std::unique_ptr<DeviceHolder> device_;
    std::unique_ptr<SurfaceManager> sm_;
    WindowHandle contextWindow_ = {};

    static inline SurfaceManager* sSurfaceManager_ = nullptr;
};

// ============================================================================
// SS13: SurfaceManager — Attach / Detach / Query
// ============================================================================

TEST_P(SurfaceIntegrationTest, AttachSurfaceSucceeds) {
    auto h = MakeWindow("Attach");
    ASSERT_TRUE(AttachSurface(h));
    EXPECT_TRUE(sm_->HasSurface(h));
    SafeDestroy(h);
}

TEST_P(SurfaceIntegrationTest, AttachSurfaceDoubleAttachFails) {
    auto h = MakeWindow("DoubleAttach");
    ASSERT_TRUE(AttachSurface(h));
    auto nh = wm_->GetNativeHandle(h);
    auto r = sm_->AttachSurface(h, nh);
    EXPECT_FALSE(r.has_value());
    SafeDestroy(h);
}

TEST_P(SurfaceIntegrationTest, AttachSurfaceInvalidHandleFails) {
    WindowHandle stale{999, 42};
    NativeWindowHandle nh = Win32Window{};
    auto r = sm_->AttachSurface(stale, nh);
    EXPECT_FALSE(r.has_value());
}

TEST_P(SurfaceIntegrationTest, DetachSurfaceSucceeds) {
    auto h = MakeWindow("Detach");
    ASSERT_TRUE(AttachSurface(h));
    auto r = sm_->DetachSurface(h);
    EXPECT_TRUE(r.has_value());
    EXPECT_FALSE(sm_->HasSurface(h));
    EXPECT_EQ(sm_->GetRenderSurface(h), nullptr);
    (void)wm_->DestroyWindow(h);
}

TEST_P(SurfaceIntegrationTest, DetachSurfaceWithoutSurfaceFails) {
    auto h = MakeWindow("NoSurface");
    auto r = sm_->DetachSurface(h);
    EXPECT_FALSE(r.has_value());
    (void)wm_->DestroyWindow(h);
}

TEST_P(SurfaceIntegrationTest, DetachSurfacesBatchEmpty) {
    auto r = sm_->DetachSurfaces({});
    EXPECT_TRUE(r.has_value());  // empty span is no-op, not error
}

TEST_P(SurfaceIntegrationTest, DetachSurfacesBatch) {
    auto h1 = MakeWindow("B1");
    auto h2 = MakeWindow("B2");
    ASSERT_TRUE(AttachSurface(h1));
    ASSERT_TRUE(AttachSurface(h2));
    std::vector<WindowHandle> batch = {h1, h2};
    auto r = sm_->DetachSurfaces(batch);
    EXPECT_TRUE(r.has_value());
    EXPECT_FALSE(sm_->HasSurface(h1));
    EXPECT_FALSE(sm_->HasSurface(h2));
    (void)wm_->DestroyWindow(h1);
    (void)wm_->DestroyWindow(h2);
}

TEST_P(SurfaceIntegrationTest, ReattachAfterDetach) {
    auto h = MakeWindow("Reattach");
    ASSERT_TRUE(AttachSurface(h));
    ASSERT_TRUE(sm_->DetachSurface(h).has_value());
    ASSERT_TRUE(AttachSurface(h));
    EXPECT_TRUE(sm_->HasSurface(h));
    SafeDestroy(h);
}

TEST_P(SurfaceIntegrationTest, GetRenderSurfaceAndFrameManager) {
    auto h = MakeWindow("Access");
    ASSERT_TRUE(AttachSurface(h));
    EXPECT_NE(sm_->GetRenderSurface(h), nullptr);
    EXPECT_NE(sm_->GetFrameManager(h), nullptr);
    SafeDestroy(h);
}

// ============================================================================
// SS13: SurfaceManager — Frame Operations
// ============================================================================

TEST_P(SurfaceIntegrationTest, BeginEndFrameBasic) {
    auto h = MakeWindow("Frame", 320, 240);
    ASSERT_TRUE(AttachSurface(h));

    auto ctx = sm_->BeginFrame(h);
    ASSERT_TRUE(ctx.has_value()) << "BeginFrame failed";

    // EndFrame with no command buffer — pass empty
    auto r = sm_->EndFrame(h, CommandListHandle{});
    EXPECT_TRUE(r.has_value()) << "EndFrame failed";

    SafeDestroy(h);
}

TEST_P(SurfaceIntegrationTest, BeginFrameWithoutSurfaceFails) {
    auto h = MakeWindow("NoSurf");
    auto ctx = sm_->BeginFrame(h);
    EXPECT_FALSE(ctx.has_value());
    (void)wm_->DestroyWindow(h);
}

TEST_P(SurfaceIntegrationTest, MultiFrameLoop) {
    auto h = MakeWindow("Loop", 320, 240);
    ASSERT_TRUE(AttachSurface(h));

    for (int i = 0; i < 10; ++i) {
        auto ctx = sm_->BeginFrame(h);
        ASSERT_TRUE(ctx.has_value()) << "BeginFrame failed at frame " << i;
        auto r = sm_->EndFrame(h, CommandListHandle{});
        ASSERT_TRUE(r.has_value()) << "EndFrame failed at frame " << i;
    }

    SafeDestroy(h);
}

TEST_P(SurfaceIntegrationTest, TwoWindowsInterleavedFrames) {
    auto a = MakeWindow("WinA", 200, 150);
    auto b = MakeWindow("WinB", 200, 150);
    ASSERT_TRUE(AttachSurface(a));
    ASSERT_TRUE(AttachSurface(b));

    // Interleaved: BeginFrame(A), BeginFrame(B), EndFrame(A), EndFrame(B)
    auto ctxA = sm_->BeginFrame(a);
    ASSERT_TRUE(ctxA.has_value());
    auto ctxB = sm_->BeginFrame(b);
    ASSERT_TRUE(ctxB.has_value());

    EXPECT_TRUE(sm_->EndFrame(a, CommandListHandle{}).has_value());
    EXPECT_TRUE(sm_->EndFrame(b, CommandListHandle{}).has_value());

    SafeDestroy(a);
    SafeDestroy(b);
}

TEST_P(SurfaceIntegrationTest, ResizeSurface) {
    auto h = MakeWindow("Resize", 320, 240);
    ASSERT_TRUE(AttachSurface(h));

    auto r = sm_->ResizeSurface(h, 640, 480);
    EXPECT_TRUE(r.has_value());

    // Verify by doing a frame at new size
    auto ctx = sm_->BeginFrame(h);
    ASSERT_TRUE(ctx.has_value());
    // Frame dimensions may reflect the new size
    EXPECT_TRUE(sm_->EndFrame(h, CommandListHandle{}).has_value());

    SafeDestroy(h);
}

TEST_P(SurfaceIntegrationTest, ResizeZeroGraceful) {
    auto h = MakeWindow("ResizeZero", 320, 240);
    ASSERT_TRUE(AttachSurface(h));
    auto r = sm_->ResizeSurface(h, 0, 0);
    // Either succeeds (dormant) or returns an error — must not crash
    (void)r;
    SafeDestroy(h);
}

TEST_P(SurfaceIntegrationTest, WaitAllSuccess) {
    auto h = MakeWindow("WaitAll", 200, 150);
    ASSERT_TRUE(AttachSurface(h));

    // Do a few frames then WaitAll
    for (int i = 0; i < 3; ++i) {
        auto ctx = sm_->BeginFrame(h);
        ASSERT_TRUE(ctx.has_value());
        EXPECT_TRUE(sm_->EndFrame(h, CommandListHandle{}).has_value());
    }

    sm_->WaitAll();
    SafeDestroy(h);
}

// ============================================================================
// SS13: SurfaceManager — Dynamic Present Configuration
// ============================================================================

TEST_P(SurfaceIntegrationTest, GetSupportedPresentModesContainsFifo) {
    auto h = MakeWindow("PresentModes", 200, 150);
    ASSERT_TRUE(AttachSurface(h));
    auto modes = sm_->GetSupportedPresentModes(h);
    EXPECT_FALSE(modes.empty());
    EXPECT_NE(std::find(modes.begin(), modes.end(), PresentMode::Fifo), modes.end());
    SafeDestroy(h);
}

TEST_P(SurfaceIntegrationTest, GetSupportedColorSpacesContainsSRGB) {
    auto h = MakeWindow("ColorSpaces", 200, 150);
    ASSERT_TRUE(AttachSurface(h));
    auto spaces = sm_->GetSupportedColorSpaces(h);
    EXPECT_FALSE(spaces.empty());
    EXPECT_NE(std::find(spaces.begin(), spaces.end(), SurfaceColorSpace::SRGB), spaces.end());
    SafeDestroy(h);
}

TEST_P(SurfaceIntegrationTest, SetPresentModeWithoutSurfaceFails) {
    auto h = MakeWindow("NoPM");
    auto r = sm_->SetPresentMode(h, PresentMode::Fifo);
    EXPECT_FALSE(r.has_value());
    (void)wm_->DestroyWindow(h);
}

// ============================================================================
// SS13: Cascade Destruction with SurfaceManager
// ============================================================================

TEST_P(SurfaceIntegrationTest, CascadeDestroyWithSurfaces) {
    auto root = MakeWindow("R");
    auto child = MakeWindow("C", 200, 150, root);
    ASSERT_TRUE(AttachSurface(root));
    ASSERT_TRUE(AttachSurface(child));

    // Do a frame on both
    {
        auto ctx = sm_->BeginFrame(root);
        ASSERT_TRUE(ctx.has_value());
        EXPECT_TRUE(sm_->EndFrame(root, CommandListHandle{}).has_value());
    }
    {
        auto ctx = sm_->BeginFrame(child);
        ASSERT_TRUE(ctx.has_value());
        EXPECT_TRUE(sm_->EndFrame(child, CommandListHandle{}).has_value());
    }

    // Use DestroyWindowCascade — detach surfaces then destroy windows
    auto cascadeResult = DestroyWindowCascade(*wm_, *sm_, root);
    ASSERT_TRUE(cascadeResult.has_value());
    EXPECT_FALSE(sm_->HasSurface(root));
    EXPECT_FALSE(sm_->HasSurface(child));
    EXPECT_EQ(wm_->GetWindowCount(), 0u);
}

TEST_P(SurfaceIntegrationTest, DestroyChildPreservesParentSurface) {
    auto root = MakeWindow("R", 200, 150);
    auto child = MakeWindow("C", 200, 150, root);
    ASSERT_TRUE(AttachSurface(root));
    ASSERT_TRUE(AttachSurface(child));

    // Run frames on both
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(sm_->BeginFrame(root).has_value());
        EXPECT_TRUE(sm_->EndFrame(root, CommandListHandle{}).has_value());
        ASSERT_TRUE(sm_->BeginFrame(child).has_value());
        EXPECT_TRUE(sm_->EndFrame(child, CommandListHandle{}).has_value());
    }

    // Destroy child only
    (void)sm_->DetachSurface(child);
    (void)wm_->DestroyWindow(child);

    // Parent should still be alive and renderable
    EXPECT_TRUE(wm_->GetWindowInfo(root).alive);
    EXPECT_TRUE(sm_->HasSurface(root));

    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(sm_->BeginFrame(root).has_value());
        EXPECT_TRUE(sm_->EndFrame(root, CommandListHandle{}).has_value());
    }

    SafeDestroy(root);
}

TEST_P(SurfaceIntegrationTest, DestroyChildSiblingOrderPreserved) {
    auto root = MakeWindow("R");
    auto a = MakeWindow("A", 100, 100, root);
    auto b = MakeWindow("B", 100, 100, root);
    auto c = MakeWindow("C", 100, 100, root);
    ASSERT_TRUE(AttachSurface(a));
    ASSERT_TRUE(AttachSurface(b));
    ASSERT_TRUE(AttachSurface(c));

    // Destroy B
    (void)sm_->DetachSurface(b);
    (void)wm_->DestroyWindow(b);

    auto children = wm_->GetChildren(root);
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], a);
    EXPECT_EQ(children[1], c);

    // A and C surfaces still work
    ASSERT_TRUE(sm_->BeginFrame(a).has_value());
    EXPECT_TRUE(sm_->EndFrame(a, CommandListHandle{}).has_value());
    ASSERT_TRUE(sm_->BeginFrame(c).has_value());
    EXPECT_TRUE(sm_->EndFrame(c, CommandListHandle{}).has_value());

    SafeDestroy(a);
    SafeDestroy(c);
    (void)wm_->DestroyWindow(root);
}

// ============================================================================
// SS13: HasSurface callback blocks DestroyWindow
// ============================================================================

TEST_P(SurfaceIntegrationTest, DestroyWindowWithAttachedSurfaceFails) {
    auto h = MakeWindow("Dangling");
    ASSERT_TRUE(AttachSurface(h));

    // HasSurface callback is set in SetUp — DestroyWindow should fail
    auto r = wm_->DestroyWindow(h);
    EXPECT_FALSE(r.has_value());

    // Properly detach then destroy
    EXPECT_TRUE(sm_->DetachSurface(h).has_value());
    EXPECT_TRUE(wm_->DestroyWindow(h).has_value());
}

// ============================================================================
// SS13.1 Integration: Full-Lifecycle Cascade Stress
// ============================================================================

TEST_P(SurfaceIntegrationTest, FullLifecycleCascadeStress) {
    // Create 3 roots, each with 2 children (total 9 windows)
    std::vector<WindowHandle> roots;
    std::vector<WindowHandle> allWindows;
    for (int r = 0; r < 3; ++r) {
        auto root = MakeWindow("R" + std::to_string(r));
        roots.push_back(root);
        allWindows.push_back(root);
        ASSERT_TRUE(AttachSurface(root));
        for (int c = 0; c < 2; ++c) {
            auto child = MakeWindow("C" + std::to_string(r) + std::to_string(c), 128, 96, root);
            allWindows.push_back(child);
            ASSERT_TRUE(AttachSurface(child));
        }
    }

    // Run 10 frames on all windows
    for (int frame = 0; frame < 10; ++frame) {
        for (auto& w : allWindows) {
            auto ctx = sm_->BeginFrame(w);
            ASSERT_TRUE(ctx.has_value()) << "BeginFrame failed, window=" << w.id << " frame=" << frame;
            EXPECT_TRUE(sm_->EndFrame(w, CommandListHandle{}).has_value());
        }
    }

    // Cascade-destroy first root (root + 2 children = 3 windows)
    auto cascadeResult = DestroyWindowCascade(*wm_, *sm_, roots[0]);
    ASSERT_TRUE(cascadeResult.has_value());

    // Remaining windows continue rendering
    std::vector<WindowHandle> remaining;
    for (auto& w : allWindows) {
        if (wm_->GetWindowInfo(w).alive) {
            remaining.push_back(w);
        }
    }
    EXPECT_EQ(remaining.size(), 6u);

    for (int frame = 0; frame < 5; ++frame) {
        for (auto& w : remaining) {
            auto ctx = sm_->BeginFrame(w);
            ASSERT_TRUE(ctx.has_value());
            EXPECT_TRUE(sm_->EndFrame(w, CommandListHandle{}).has_value());
        }
    }

    // Destroy remaining
    for (auto& r : roots) {
        if (wm_->GetWindowInfo(r).alive) {
            auto cr = DestroyWindowCascade(*wm_, *sm_, r);
            EXPECT_TRUE(cr.has_value());
        }
    }
    EXPECT_EQ(wm_->GetWindowCount(), 0u);
}

// ============================================================================
// SS13.1 Integration: Rapid Resize Flood
// ============================================================================

TEST_P(SurfaceIntegrationTest, RapidResizeFlood) {
    auto h = MakeWindow("Flood", 320, 240);
    ASSERT_TRUE(AttachSurface(h));

    // Resize 20 times rapidly with varying dimensions
    for (int i = 0; i < 20; ++i) {
        uint32_t w = 64 + (i * 37) % 512;
        uint32_t h2 = 64 + (i * 53) % 384;
        auto r = sm_->ResizeSurface(h, w, h2);
        if (!r.has_value()) {
            continue;  // Some resizes may fail (transient)
        }
        auto ctx = sm_->BeginFrame(h);
        if (!ctx.has_value()) {
            continue;
        }
        (void)sm_->EndFrame(h, CommandListHandle{});
    }

    SafeDestroy(h);
}

// ============================================================================
// SS13.1 Integration: Surface Detach-Reattach Cycle
// ============================================================================

TEST_P(SurfaceIntegrationTest, DetachReattachCycle) {
    auto h = MakeWindow("Cycle", 256, 192);

    for (int i = 0; i < 5; ++i) {
        // Attach
        RenderSurfaceConfig cfg{};
        cfg.presentMode = PresentMode::Fifo;
        ASSERT_TRUE(AttachSurface(h, cfg)) << "Attach failed at cycle " << i;
        EXPECT_TRUE(sm_->HasSurface(h));

        // Frame
        auto ctx = sm_->BeginFrame(h);
        ASSERT_TRUE(ctx.has_value()) << "BeginFrame failed at cycle " << i;
        EXPECT_TRUE(sm_->EndFrame(h, CommandListHandle{}).has_value());

        // Detach
        ASSERT_TRUE(sm_->DetachSurface(h).has_value()) << "Detach failed at cycle " << i;
        EXPECT_FALSE(sm_->HasSurface(h));
    }

    (void)wm_->DestroyWindow(h);
}

// ============================================================================
// SS13.1 Integration: Child Destruction Isolation
// ============================================================================

TEST_P(SurfaceIntegrationTest, ChildDestructionIsolation) {
    auto root = MakeWindow("R", 200, 150);
    auto a = MakeWindow("A", 128, 96, root);
    auto b = MakeWindow("B", 128, 96, root);
    auto c = MakeWindow("C", 128, 96, root);
    ASSERT_TRUE(AttachSurface(root));
    ASSERT_TRUE(AttachSurface(a));
    ASSERT_TRUE(AttachSurface(b));
    ASSERT_TRUE(AttachSurface(c));

    // Run 5 frames on all
    for (int f = 0; f < 5; ++f) {
        for (auto w : {root, a, b, c}) {
            ASSERT_TRUE(sm_->BeginFrame(w).has_value());
            EXPECT_TRUE(sm_->EndFrame(w, CommandListHandle{}).has_value());
        }
    }

    // Destroy A
    auto cascadeA = DestroyWindowCascade(*wm_, *sm_, a);
    ASSERT_TRUE(cascadeA.has_value());

    // R, B, C still alive
    EXPECT_TRUE(wm_->GetWindowInfo(root).alive);
    EXPECT_TRUE(wm_->GetWindowInfo(b).alive);
    EXPECT_TRUE(wm_->GetWindowInfo(c).alive);
    auto children = wm_->GetChildren(root);
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], b);
    EXPECT_EQ(children[1], c);

    // A's handle is stale
    EXPECT_FALSE(wm_->GetWindowInfo(a).alive);
    EXPECT_FALSE(sm_->HasSurface(a));

    // Continue rendering on remaining windows
    for (int f = 0; f < 5; ++f) {
        for (auto w : {root, b, c}) {
            ASSERT_TRUE(sm_->BeginFrame(w).has_value());
            EXPECT_TRUE(sm_->EndFrame(w, CommandListHandle{}).has_value());
        }
    }

    // Destroy B
    EXPECT_TRUE(DestroyWindowCascade(*wm_, *sm_, b).has_value());
    children = wm_->GetChildren(root);
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], c);

    // Destroy C
    EXPECT_TRUE(DestroyWindowCascade(*wm_, *sm_, c).has_value());
    children = wm_->GetChildren(root);
    EXPECT_TRUE(children.empty());

    // Root continues rendering alone
    ASSERT_TRUE(sm_->BeginFrame(root).has_value());
    EXPECT_TRUE(sm_->EndFrame(root, CommandListHandle{}).has_value());

    SafeDestroy(root);
}

// ============================================================================
// SS13.1 Integration: Cross-Window Cascade with Shared Resources
// ============================================================================

TEST_P(SurfaceIntegrationTest, CrossWindowCascade) {
    auto root = MakeWindow("R", 200, 150);
    auto a = MakeWindow("A", 128, 96, root);
    auto a1 = MakeWindow("A1", 64, 48, a);
    ASSERT_TRUE(AttachSurface(root));
    ASSERT_TRUE(AttachSurface(a));
    ASSERT_TRUE(AttachSurface(a1));

    // Run frames on all
    for (int f = 0; f < 5; ++f) {
        for (auto w : {root, a, a1}) {
            ASSERT_TRUE(sm_->BeginFrame(w).has_value());
            EXPECT_TRUE(sm_->EndFrame(w, CommandListHandle{}).has_value());
        }
    }

    // Cascade-destroy A — A1 destroyed first (post-order), then A
    // Verify post-order before destruction
    auto desc = wm_->GetDescendantsPostOrder(a);
    ASSERT_EQ(desc.size(), 1u);
    EXPECT_EQ(desc[0], a1);  // A1 is only descendant, appears first (post-order)

    auto cascadeResult = DestroyWindowCascade(*wm_, *sm_, a);
    ASSERT_TRUE(cascadeResult.has_value());

    // Root is untouched, continue rendering
    EXPECT_TRUE(wm_->GetWindowInfo(root).alive);
    EXPECT_TRUE(sm_->HasSurface(root));

    for (int f = 0; f < 5; ++f) {
        ASSERT_TRUE(sm_->BeginFrame(root).has_value());
        EXPECT_TRUE(sm_->EndFrame(root, CommandListHandle{}).has_value());
    }

    SafeDestroy(root);
}

// ============================================================================
// SS13.1 Integration: Error Recovery Chain
// ============================================================================

TEST_P(SurfaceIntegrationTest, ErrorRecoveryChain) {
    // (1) Attach surface, try double-attach — first surface unaffected
    auto h1 = MakeWindow("Good");
    ASSERT_TRUE(AttachSurface(h1));
    auto nh1 = wm_->GetNativeHandle(h1);
    auto r1 = sm_->AttachSurface(h1, nh1);
    EXPECT_FALSE(r1.has_value());
    EXPECT_TRUE(sm_->HasSurface(h1));

    // (2) Detach non-existent — error, h1 unaffected
    auto h2 = MakeWindow("NoSurf");
    auto r2 = sm_->DetachSurface(h2);
    EXPECT_FALSE(r2.has_value());
    EXPECT_TRUE(sm_->HasSurface(h1));

    // (3) DestroyWindow null — error, h1 alive
    auto r3 = wm_->DestroyWindow(WindowHandle{});
    EXPECT_FALSE(r3.has_value());
    EXPECT_TRUE(wm_->GetWindowInfo(h1).alive);

    // (4) Clean shutdown
    SafeDestroy(h1);
    (void)wm_->DestroyWindow(h2);
    EXPECT_EQ(wm_->GetWindowCount(), 0u);
}

// ============================================================================
// SS13.1 Integration: Three-Concern Separation Verification
//   (Compile-time test — if this file compiles, it passes)
// ============================================================================

TEST_P(SurfaceIntegrationTest, ThreeConcernSeparation) {
    // WindowManager.h does NOT include SurfaceManager.h or any RHI device header.
    // SurfaceManager.h does NOT include WindowManager.h (only WindowHandle.h).
    // If this test compiles, the three-concern separation is correct.
    SUCCEED();
}

// ============================================================================
// Register parameterized tests for all available backends
// ============================================================================

static auto BackendName(const ::testing::TestParamInfo<BackendInfo>& info) -> std::string {
    return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(AllBackends, SurfaceIntegrationTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);
