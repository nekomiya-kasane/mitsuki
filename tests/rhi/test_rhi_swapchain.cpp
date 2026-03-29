// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// §11 Swapchain tests.
// Covers: SwapchainDesc/RenderSurfaceConfig struct validation,
// SurfaceColorSpace enum, VRRMode enum, ImageCountHint enum,
// PresentMode enum coverage.
// NOTE: Actual swapchain creation requires a window (NativeWindowHandle).
// GPU-dependent swapchain tests live in test_surface_integration.cpp.

#include <gtest/gtest.h>

#include "miki/rhi/Swapchain.h"

using namespace miki::rhi;

// ============================================================================
// §11.1 SwapchainDesc — struct validation (pure CPU)
// ============================================================================

TEST(SwapchainDesc, DefaultsValid) {
    SwapchainDesc desc{};
    EXPECT_EQ(desc.width, 0u);
    EXPECT_EQ(desc.height, 0u);
    EXPECT_EQ(desc.preferredFormat, Format::BGRA8_SRGB);
    EXPECT_EQ(desc.presentMode, PresentMode::Fifo);
    EXPECT_EQ(desc.colorSpace, SurfaceColorSpace::SRGB);
    EXPECT_EQ(desc.imageCount, 2u);
    EXPECT_FALSE(desc.allowTearing);
    EXPECT_EQ(desc.debugName, nullptr);
}

TEST(SwapchainDesc, CustomValues) {
    SwapchainDesc desc{
        .width = 1920, .height = 1080,
        .preferredFormat = Format::RGBA16_FLOAT,
        .presentMode = PresentMode::Mailbox,
        .colorSpace = SurfaceColorSpace::scRGBLinear,
        .imageCount = 3,
        .allowTearing = true,
        .debugName = "MainSwapchain",
    };
    EXPECT_EQ(desc.width, 1920u);
    EXPECT_EQ(desc.height, 1080u);
    EXPECT_EQ(desc.preferredFormat, Format::RGBA16_FLOAT);
    EXPECT_EQ(desc.presentMode, PresentMode::Mailbox);
    EXPECT_EQ(desc.colorSpace, SurfaceColorSpace::scRGBLinear);
    EXPECT_EQ(desc.imageCount, 3u);
    EXPECT_TRUE(desc.allowTearing);
}

// ============================================================================
// §11.2 RenderSurfaceConfig — struct validation
// ============================================================================

TEST(RenderSurfaceConfig, DefaultsValid) {
    RenderSurfaceConfig cfg{};
    EXPECT_EQ(cfg.presentMode, PresentMode::Fifo);
    EXPECT_EQ(cfg.colorSpace, SurfaceColorSpace::SRGB);
    EXPECT_EQ(cfg.preferredFormat, Format::BGRA8_SRGB);
    EXPECT_EQ(cfg.vrrMode, VRRMode::Off);
    EXPECT_EQ(cfg.imageCount, ImageCountHint::Auto);
}

TEST(RenderSurfaceConfig, HDR10Config) {
    RenderSurfaceConfig cfg{
        .presentMode = PresentMode::Fifo,
        .colorSpace = SurfaceColorSpace::HDR10_ST2084,
        .preferredFormat = Format::RGBA16_FLOAT,
        .vrrMode = VRRMode::AdaptiveSync,
        .imageCount = ImageCountHint::Triple,
    };
    EXPECT_EQ(cfg.colorSpace, SurfaceColorSpace::HDR10_ST2084);
    EXPECT_EQ(cfg.vrrMode, VRRMode::AdaptiveSync);
    EXPECT_EQ(cfg.imageCount, ImageCountHint::Triple);
}

// ============================================================================
// §11.3 Enum Coverage
// ============================================================================

TEST(SurfaceColorSpace, AllValues) {
    EXPECT_EQ(static_cast<uint8_t>(SurfaceColorSpace::SRGB), 0);
    EXPECT_EQ(static_cast<uint8_t>(SurfaceColorSpace::HDR10_ST2084), 1);
    EXPECT_EQ(static_cast<uint8_t>(SurfaceColorSpace::scRGBLinear), 2);
}

TEST(VRRMode, AllValues) {
    EXPECT_EQ(static_cast<uint8_t>(VRRMode::Off), 0);
    EXPECT_EQ(static_cast<uint8_t>(VRRMode::AdaptiveSync), 1);
    EXPECT_EQ(static_cast<uint8_t>(VRRMode::GSync), 2);
}

TEST(ImageCountHint, AllValues) {
    EXPECT_EQ(static_cast<uint8_t>(ImageCountHint::Auto), 0);
    EXPECT_EQ(static_cast<uint8_t>(ImageCountHint::Minimal), 1);
    EXPECT_EQ(static_cast<uint8_t>(ImageCountHint::Triple), 2);
}
