// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Phase 1a unit tests for IPipelineFactory: factory dispatch via DeviceHandle,
// tier detection, and stub method behavior (NotImplemented).
//
// Uses MockDevice since IPipelineFactory::Create() requires a DeviceHandle.

#include <gtest/gtest.h>

#include "miki/rhi/IPipelineFactory.h"
#include "miki/rhi/backend/MockDevice.h"  // MockDevice lives in miki::rhi namespace

using namespace miki::rhi;

// ============================================================================
// Test fixture: creates a MockDevice to get a DeviceHandle
// ============================================================================

class PipelineFactoryTest : public ::testing::Test {
   protected:
    void SetUp() override {
        mockDevice_ = std::make_unique<MockDevice>();
        auto initResult = mockDevice_->Init();
        ASSERT_TRUE(initResult.has_value()) << "MockDevice init failed";
        handle_ = DeviceHandle(mockDevice_.get(), BackendType::Mock);
    }

    std::unique_ptr<MockDevice> mockDevice_;
    DeviceHandle handle_;
};

// ============================================================================
// Factory dispatch
// ============================================================================

TEST_F(PipelineFactoryTest, CreateReturnsNonNull) {
    auto& handle = handle_;
    auto factory = IPipelineFactory::Create(handle);
    ASSERT_NE(factory, nullptr);
}

TEST_F(PipelineFactoryTest, MockDeviceReturnsCompatFactory) {
    // MockDevice defaults to Tier4_OpenGL -> CompatPipelineFactory
    auto& handle = handle_;
    auto factory = IPipelineFactory::Create(handle);
    ASSERT_NE(factory, nullptr);

    auto tier = factory->GetTier();
    // Compat factory should not be Tier1
    EXPECT_NE(tier, CapabilityTier::Tier1_Vulkan);
    EXPECT_NE(tier, CapabilityTier::Tier1_D3D12);
}

// ============================================================================
// Stub methods return NotImplemented
// ============================================================================

TEST_F(PipelineFactoryTest, StubMethodsReturnError) {
    auto& handle = handle_;
    auto factory = IPipelineFactory::Create(handle);
    ASSERT_NE(factory, nullptr);

    // These passes are stubbed in Phase 1a — should return error
    auto shadowResult = factory->CreateShadowPass({});
    EXPECT_FALSE(shadowResult.has_value());

    auto oitResult = factory->CreateOITPass({});
    EXPECT_FALSE(oitResult.has_value());

    auto aoResult = factory->CreateAOPass({});
    EXPECT_FALSE(aoResult.has_value());

    auto aaResult = factory->CreateAAPass({});
    EXPECT_FALSE(aaResult.has_value());
}

// ============================================================================
// Capability tier query
// ============================================================================

TEST_F(PipelineFactoryTest, GetTierReturnsValidTier) {
    auto& handle = handle_;
    auto factory = IPipelineFactory::Create(handle);
    ASSERT_NE(factory, nullptr);

    auto tier = factory->GetTier();
    // Should be a valid enum value (not garbage)
    EXPECT_GE(static_cast<uint8_t>(tier), static_cast<uint8_t>(CapabilityTier::Tier1_Vulkan));
    EXPECT_LE(static_cast<uint8_t>(tier), static_cast<uint8_t>(CapabilityTier::Tier4_OpenGL));
}
