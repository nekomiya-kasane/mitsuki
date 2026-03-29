// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// §4 Device & Capability System tests.
// Covers: DeviceDesc creation, DeviceHandle lifecycle, GpuCapabilityProfile
// queries, tier detection, format support, DeviceHandle move semantics,
// backend type validation, capability consistency.

#include <gtest/gtest.h>

#include "RhiTestFixture.h"

#include "miki/rhi/Device.h"
#include "miki/rhi/DeviceFeature.h"
#include "miki/rhi/GpuCapabilityProfile.h"

using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// §4.1 Device Creation
// ============================================================================

class RhiDeviceTest : public RhiTest {};

TEST_P(RhiDeviceTest, DeviceIsValid) {
    EXPECT_TRUE(Dev().IsValid());
}

TEST_P(RhiDeviceTest, BackendTypeMatchesRequest) {
    EXPECT_EQ(Dev().GetBackendType(), GetParam());
}

TEST_P(RhiDeviceTest, CapabilityProfileNotEmpty) {
    auto& caps = Caps();
    EXPECT_NE(caps.deviceName.empty(), true);
    EXPECT_NE(caps.maxTextureSize2D, 0u);
    EXPECT_NE(caps.maxColorAttachments, 0u);
}

TEST_P(RhiDeviceTest, BackendTypeInCapsMatchesDevice) {
    EXPECT_EQ(Caps().backendType, GetParam());
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiDeviceTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);

// ============================================================================
// §4.1.1 DeviceHandle — Trivial Struct Validation
// ============================================================================

TEST(DeviceHandleBasic, DefaultConstructedIsInvalid) {
    DeviceHandle h;
    EXPECT_FALSE(h.IsValid());
    EXPECT_EQ(h.GetBackendType(), BackendType::Mock);
}

TEST(DeviceHandleBasic, DestroyMakesInvalid) {
    // Simulate a valid handle then destroy it
    int dummy = 0;
    DeviceHandle h(&dummy, BackendType::Vulkan14);
    EXPECT_TRUE(h.IsValid());
    h.Destroy();
    EXPECT_FALSE(h.IsValid());
    EXPECT_EQ(h.GetBackendType(), BackendType::Mock);
}

// ============================================================================
// §4.2 Capability Profile — Tier Detection
// ============================================================================

class RhiCapabilityTest : public RhiTest {};

TEST_P(RhiCapabilityTest, TierConsistentWithBackendType) {
    auto& caps = Caps();
    switch (GetParam()) {
        case BackendType::Vulkan14: EXPECT_EQ(caps.tier, CapabilityTier::Tier1_Vulkan); break;
        case BackendType::D3D12: EXPECT_EQ(caps.tier, CapabilityTier::Tier1_D3D12); break;
        case BackendType::VulkanCompat: EXPECT_EQ(caps.tier, CapabilityTier::Tier2_Compat); break;
        case BackendType::WebGPU: EXPECT_EQ(caps.tier, CapabilityTier::Tier3_WebGPU); break;
        case BackendType::OpenGL43: EXPECT_EQ(caps.tier, CapabilityTier::Tier4_OpenGL); break;
        default: break;
    }
}

TEST_P(RhiCapabilityTest, Tier1HasRequiredFeatures) {
    if (!IsTier1()) {
        GTEST_SKIP();
    }
    auto& caps = Caps();
    EXPECT_TRUE(caps.hasMeshShader);
    EXPECT_TRUE(caps.hasTimelineSemaphore);
    EXPECT_TRUE(caps.hasBindless);
    EXPECT_GE(caps.maxPushConstantSize, 128u);
}

TEST_P(RhiCapabilityTest, AllTiersHaveBasicLimits) {
    auto& caps = Caps();
    EXPECT_GE(caps.maxTextureSize2D, 4096u);
    EXPECT_GE(caps.maxColorAttachments, 4u);
    EXPECT_GE(caps.maxBoundDescriptorSets, 4u);
    EXPECT_GT(caps.maxComputeWorkGroupSize[0], 0u);
    EXPECT_GT(caps.maxComputeWorkGroupSize[1], 0u);
    EXPECT_GT(caps.maxComputeWorkGroupSize[2], 0u);
}

TEST_P(RhiCapabilityTest, SupportsTierSelf) {
    auto& caps = Caps();
    EXPECT_TRUE(caps.SupportsTier(caps.tier));
}

TEST_P(RhiCapabilityTest, Tier1SupportsTier2AndBelow) {
    if (!IsTier1()) {
        GTEST_SKIP();
    }
    auto& caps = Caps();
    EXPECT_TRUE(caps.SupportsTier(CapabilityTier::Tier2_Compat));
    EXPECT_TRUE(caps.SupportsTier(CapabilityTier::Tier3_WebGPU));
    EXPECT_TRUE(caps.SupportsTier(CapabilityTier::Tier4_OpenGL));
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiCapabilityTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);

// ============================================================================
// §4.2.1 DeviceFeatureSet — Unit Tests (pure CPU logic)
// ============================================================================

TEST(DeviceFeatureSet, DefaultIsEmpty) {
    DeviceFeatureSet fs;
    EXPECT_TRUE(fs.IsEmpty());
    EXPECT_EQ(fs.Count(), 0u);
}

TEST(DeviceFeatureSet, AddAndHas) {
    DeviceFeatureSet fs;
    fs.Add(DeviceFeature::MeshShader);
    EXPECT_TRUE(fs.Has(DeviceFeature::MeshShader));
    EXPECT_FALSE(fs.Has(DeviceFeature::RayQuery));
    EXPECT_EQ(fs.Count(), 1u);
}

TEST(DeviceFeatureSet, Remove) {
    DeviceFeatureSet fs{DeviceFeature::MeshShader, DeviceFeature::RayQuery};
    EXPECT_EQ(fs.Count(), 2u);
    fs.Remove(DeviceFeature::MeshShader);
    EXPECT_FALSE(fs.Has(DeviceFeature::MeshShader));
    EXPECT_TRUE(fs.Has(DeviceFeature::RayQuery));
    EXPECT_EQ(fs.Count(), 1u);
}

TEST(DeviceFeatureSet, InitializerListConstruction) {
    DeviceFeatureSet fs{DeviceFeature::Present, DeviceFeature::DynamicRendering, DeviceFeature::TimelineSemaphore};
    EXPECT_EQ(fs.Count(), 3u);
    EXPECT_TRUE(fs.Has(DeviceFeature::Present));
    EXPECT_TRUE(fs.Has(DeviceFeature::DynamicRendering));
    EXPECT_TRUE(fs.Has(DeviceFeature::TimelineSemaphore));
}

TEST(DeviceFeatureSet, ContainsAll) {
    DeviceFeatureSet full{DeviceFeature::Present, DeviceFeature::MeshShader, DeviceFeature::RayQuery};
    DeviceFeatureSet subset{DeviceFeature::Present, DeviceFeature::MeshShader};
    DeviceFeatureSet disjoint{DeviceFeature::Float64};

    EXPECT_TRUE(full.ContainsAll(subset));
    EXPECT_FALSE(subset.ContainsAll(full));
    EXPECT_FALSE(full.ContainsAll(disjoint));
}

TEST(DeviceFeatureSet, Intersection) {
    DeviceFeatureSet a{DeviceFeature::Present, DeviceFeature::MeshShader};
    DeviceFeatureSet b{DeviceFeature::MeshShader, DeviceFeature::RayQuery};
    auto inter = a.Intersection(b);
    EXPECT_EQ(inter.Count(), 1u);
    EXPECT_TRUE(inter.Has(DeviceFeature::MeshShader));
}

TEST(DeviceFeatureSet, Union) {
    DeviceFeatureSet a{DeviceFeature::Present};
    DeviceFeatureSet b{DeviceFeature::RayQuery};
    auto u = a.Union(b);
    EXPECT_EQ(u.Count(), 2u);
    EXPECT_TRUE(u.Has(DeviceFeature::Present));
    EXPECT_TRUE(u.Has(DeviceFeature::RayQuery));
}

TEST(DeviceFeatureSet, ForEach) {
    DeviceFeatureSet fs{DeviceFeature::Present, DeviceFeature::MeshShader};
    std::vector<DeviceFeature> features;
    fs.ForEach([&](DeviceFeature f) { features.push_back(f); });
    EXPECT_EQ(features.size(), 2u);
    EXPECT_NE(std::find(features.begin(), features.end(), DeviceFeature::Present), features.end());
    EXPECT_NE(std::find(features.begin(), features.end(), DeviceFeature::MeshShader), features.end());
}

TEST(DeviceFeatureSet, Equality) {
    DeviceFeatureSet a{DeviceFeature::Present, DeviceFeature::MeshShader};
    DeviceFeatureSet b{DeviceFeature::Present, DeviceFeature::MeshShader};
    DeviceFeatureSet c{DeviceFeature::Present};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// ============================================================================
// §4.3 Format Support Query
// ============================================================================

class RhiFormatSupportTest : public RhiTest {};

TEST_P(RhiFormatSupportTest, RGBA8UnormSupported) {
    EXPECT_TRUE(Caps().IsFormatSupported(Format::RGBA8_UNORM));
}

TEST_P(RhiFormatSupportTest, BGRA8SrgbSupported) {
    // Swapchain format — must be supported on all backends
    EXPECT_TRUE(Caps().IsFormatSupported(Format::BGRA8_SRGB));
}

TEST_P(RhiFormatSupportTest, UndefinedFormatNotSupported) {
    EXPECT_FALSE(Caps().IsFormatSupported(Format::Undefined));
}

TEST_P(RhiFormatSupportTest, Depth32FloatSupported) {
    // Depth format — should be supported on all modern backends
    EXPECT_TRUE(Caps().IsFormatSupported(Format::D32_FLOAT));
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiFormatSupportTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);

// ============================================================================
// §4.4 Capability Profile — Feature Consistency
// ============================================================================

class RhiFeatureConsistencyTest : public RhiTest {};

TEST_P(RhiFeatureConsistencyTest, MeshShaderFlagConsistent) {
    auto& caps = Caps();
    EXPECT_EQ(caps.hasMeshShader, caps.HasMeshShader());
}

TEST_P(RhiFeatureConsistencyTest, TimelineSemaphoreFlagConsistent) {
    auto& caps = Caps();
    EXPECT_EQ(caps.hasTimelineSemaphore, caps.HasTimelineSemaphore());
}

TEST_P(RhiFeatureConsistencyTest, AsyncComputeFlagConsistent) {
    auto& caps = Caps();
    EXPECT_EQ(caps.hasAsyncCompute, caps.HasAsyncCompute());
}

TEST_P(RhiFeatureConsistencyTest, SparseBindingFlagConsistent) {
    auto& caps = Caps();
    EXPECT_EQ(caps.hasSparseBinding, caps.HasSparseBinding());
}

TEST_P(RhiFeatureConsistencyTest, VRSFlagConsistent) {
    auto& caps = Caps();
    EXPECT_EQ(caps.hasVariableRateShading, caps.HasVariableRateShading());
}

TEST_P(RhiFeatureConsistencyTest, MeshShaderLimitsConsistent) {
    auto& caps = Caps();
    if (caps.hasMeshShader) {
        EXPECT_GT(caps.maxMeshWorkGroupInvocations, 0u);
        EXPECT_GT(caps.maxMeshOutputVertices, 0u);
        EXPECT_GT(caps.maxMeshOutputPrimitives, 0u);
    } else {
        EXPECT_EQ(caps.maxMeshWorkGroupInvocations, 0u);
        EXPECT_EQ(caps.maxMeshOutputVertices, 0u);
        EXPECT_EQ(caps.maxMeshOutputPrimitives, 0u);
    }
}

TEST_P(RhiFeatureConsistencyTest, DeviceInfoPopulated) {
    auto& caps = Caps();
    EXPECT_FALSE(caps.deviceName.empty());
    EXPECT_NE(caps.vendorId, 0u);
}

INSTANTIATE_TEST_SUITE_P(
    AllBackends, RhiFeatureConsistencyTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName
);
