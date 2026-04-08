// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// ComputeQueueLevel integration tests -- specs/03-sync.md SS5.8.2
// GPU device required. Parameterized across all backends.
// BDD style: GIVEN / WHEN / THEN.

#include <gtest/gtest.h>

#include "RhiTestFixture.h"
#include "miki/frame/ComputeQueueLevel.h"
#include "miki/rhi/GpuCapabilityProfile.h"

using namespace miki;
using namespace miki::frame;
using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// Test fixture
// ============================================================================

class ComputeQueueLevelIntegrationTest : public RhiTest {};

// ============================================================================
// SS5.8.2 CQL-INT -- Integration: caps filled + detection consistent
// ============================================================================

// CQL-INT-01: GIVEN any backend WHEN DetectComputeQueueLevel(Caps()) THEN valid level returned
TEST_P(ComputeQueueLevelIntegrationTest, Int01_DetectionReturnsValidLevel) {
    auto caps = Caps();
    auto level = DetectComputeQueueLevel(caps);
    EXPECT_GE(static_cast<uint8_t>(level), static_cast<uint8_t>(ComputeQueueLevel::A_DualQueuePriority));
    EXPECT_LE(static_cast<uint8_t>(level), static_cast<uint8_t>(ComputeQueueLevel::D_GraphicsOnly));
}

// CQL-INT-02: GIVEN backend with hasAsyncCompute WHEN detect THEN level <= C (not D)
TEST_P(ComputeQueueLevelIntegrationTest, Int02_AsyncComputeImpliesNotD) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute on this backend";
    }
    auto level = DetectComputeQueueLevel(caps);
    EXPECT_NE(level, ComputeQueueLevel::D_GraphicsOnly);
}

// CQL-INT-03: GIVEN backend without hasAsyncCompute WHEN detect THEN level == D
TEST_P(ComputeQueueLevelIntegrationTest, Int03_NoAsyncComputeIsLevelD) {
    auto caps = Caps();
    if (caps.hasAsyncCompute) {
        GTEST_SKIP() << "Has async compute, testing D path not applicable";
    }
    auto level = DetectComputeQueueLevel(caps);
    EXPECT_EQ(level, ComputeQueueLevel::D_GraphicsOnly);
}

// CQL-INT-04: GIVEN Tier1 backend WHEN check computeQueueFamilyCount THEN >= 0
TEST_P(ComputeQueueLevelIntegrationTest, Int04_ComputeQueueFamilyCountNonNegative) {
    auto caps = Caps();
    // computeQueueFamilyCount is uint32_t, always >= 0, but verify it's been set
    // For D3D12, should be 1. For Vulkan, hardware-dependent. For T3/T4, 0.
    auto backend = GetParam();
    if (backend == BackendType::D3D12) {
        EXPECT_EQ(caps.computeQueueFamilyCount, 1u);
    } else if (backend == BackendType::OpenGL43 || backend == BackendType::WebGPU) {
        EXPECT_EQ(caps.computeQueueFamilyCount, 0u);
    }
    // Vulkan: hardware-dependent, just ensure consistency
    if (backend == BackendType::Vulkan14 || backend == BackendType::VulkanCompat) {
        if (caps.hasAsyncCompute) {
            EXPECT_GE(caps.computeQueueFamilyCount, 1u);
        }
    }
}

// CQL-INT-05: GIVEN D3D12 WHEN check hasGlobalPriority THEN true
TEST_P(ComputeQueueLevelIntegrationTest, Int05_D3D12HasGlobalPriority) {
    if (GetParam() != BackendType::D3D12) {
        GTEST_SKIP() << "D3D12-specific test";
    }
    EXPECT_TRUE(Caps().hasGlobalPriority);
}

// CQL-INT-06: GIVEN OpenGL/WebGPU WHEN check hasGlobalPriority THEN false
TEST_P(ComputeQueueLevelIntegrationTest, Int06_T3T4NoGlobalPriority) {
    auto backend = GetParam();
    if (backend != BackendType::OpenGL43 && backend != BackendType::WebGPU) {
        GTEST_SKIP() << "T3/T4-specific test";
    }
    EXPECT_FALSE(Caps().hasGlobalPriority);
}

// CQL-INT-07: GIVEN hasGlobalPriority && hasAsyncCompute WHEN detect THEN level <= B
TEST_P(ComputeQueueLevelIntegrationTest, Int07_PriorityPlusAsyncImpliesAorB) {
    auto caps = Caps();
    if (!caps.hasGlobalPriority || !caps.hasAsyncCompute) {
        GTEST_SKIP() << "Need both global priority and async compute";
    }
    auto level = DetectComputeQueueLevel(caps);
    EXPECT_TRUE(level == ComputeQueueLevel::A_DualQueuePriority || level == ComputeQueueLevel::B_SingleQueuePriority)
        << "Expected A or B, got " << ComputeQueueLevelName(level);
}

// CQL-INT-08: GIVEN Mock backend WHEN detect THEN level D (mock has no real GPU)
TEST_P(ComputeQueueLevelIntegrationTest, Int08_MockIsLevelD) {
    if (GetParam() != BackendType::Mock) {
        GTEST_SKIP() << "Mock-specific test";
    }
    EXPECT_EQ(DetectComputeQueueLevel(Caps()), ComputeQueueLevel::D_GraphicsOnly);
}

// ============================================================================
// Parameterized instantiation
// ============================================================================

INSTANTIATE_TEST_SUITE_P(AllBackends, ComputeQueueLevelIntegrationTest, ::testing::ValuesIn(GetAvailableBackends()),
                         BackendName);
