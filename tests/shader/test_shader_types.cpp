// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Phase 1a unit tests for ShaderTypes: enums, structs, factory methods,
// permutation key, and PreferredShaderTarget logic.

#include <gtest/gtest.h>

#include "miki/shader/ShaderTypes.h"

using namespace miki::shader;
using namespace miki::rhi;

// ============================================================================
// ShaderTargetType enum values
// ============================================================================

TEST(ShaderTargetType, EnumValuesAreDefined) {
    EXPECT_NE(static_cast<uint8_t>(ShaderTargetType::SPIRV), static_cast<uint8_t>(ShaderTargetType::DXIL));
    EXPECT_NE(static_cast<uint8_t>(ShaderTargetType::GLSL), static_cast<uint8_t>(ShaderTargetType::WGSL));
    EXPECT_NE(static_cast<uint8_t>(ShaderTargetType::WGSL), static_cast<uint8_t>(ShaderTargetType::MSL));
}

// ============================================================================
// ShaderTarget factory methods
// ============================================================================

TEST(ShaderTarget, FactoryMethodsSPIRV) {
    auto t13 = ShaderTarget::SPIRV_1_3();
    EXPECT_EQ(t13.type, ShaderTargetType::SPIRV);
    EXPECT_EQ(t13.versionMajor, 1);
    EXPECT_EQ(t13.versionMinor, 3);

    auto t15 = ShaderTarget::SPIRV_1_5();
    EXPECT_EQ(t15.versionMinor, 5);

    auto t16 = ShaderTarget::SPIRV_1_6();
    EXPECT_EQ(t16.versionMinor, 6);
}

TEST(ShaderTarget, FactoryMethodsOtherTargets) {
    auto dxil = ShaderTarget::DXIL_6_6();
    EXPECT_EQ(dxil.type, ShaderTargetType::DXIL);
    EXPECT_EQ(dxil.versionMajor, 6);
    EXPECT_EQ(dxil.versionMinor, 6);

    auto glsl = ShaderTarget::GLSL_430();
    EXPECT_EQ(glsl.type, ShaderTargetType::GLSL);

    auto wgsl = ShaderTarget::WGSL_1_0();
    EXPECT_EQ(wgsl.type, ShaderTargetType::WGSL);

    auto msl = ShaderTarget::MSL_3_0();
    EXPECT_EQ(msl.type, ShaderTargetType::MSL);
    EXPECT_EQ(msl.versionMajor, 3);
}

TEST(ShaderTarget, EqualityComparison) {
    EXPECT_EQ(ShaderTarget::SPIRV_1_5(), ShaderTarget::SPIRV_1_5());
    EXPECT_NE(ShaderTarget::SPIRV_1_5(), ShaderTarget::SPIRV_1_6());
    EXPECT_NE(ShaderTarget::SPIRV_1_5(), ShaderTarget::DXIL_6_6());
}

// ============================================================================
// ShaderPermutationKey
// ============================================================================

TEST(ShaderPermutationKey, DefaultIsZero) {
    ShaderPermutationKey key;
    EXPECT_EQ(key.bits, 0u);
    EXPECT_FALSE(key.GetBit(0));
}

TEST(ShaderPermutationKey, SetAndGetBit) {
    ShaderPermutationKey key;
    key.SetBit(0, true);
    EXPECT_TRUE(key.GetBit(0));
    EXPECT_FALSE(key.GetBit(1));

    key.SetBit(63, true);
    EXPECT_TRUE(key.GetBit(63));

    key.SetBit(0, false);
    EXPECT_FALSE(key.GetBit(0));
    EXPECT_TRUE(key.GetBit(63));
}

TEST(ShaderPermutationKey, EqualityAndInequality) {
    ShaderPermutationKey a, b;
    EXPECT_EQ(a, b);

    a.SetBit(5, true);
    EXPECT_NE(a, b);

    b.SetBit(5, true);
    EXPECT_EQ(a, b);
}

// ============================================================================
// PreferredShaderTarget
// ============================================================================

TEST(PreferredShaderTarget, VulkanReturnsSPIRV) {
    GpuCapabilityProfile caps{};
    caps.backendType = BackendType::Vulkan14;
    auto target = PreferredShaderTarget(caps);
    EXPECT_EQ(target.type, ShaderTargetType::SPIRV);
}

TEST(PreferredShaderTarget, D3D12ReturnsDXIL) {
    GpuCapabilityProfile caps{};
    caps.backendType = BackendType::D3D12;
    auto target = PreferredShaderTarget(caps);
    EXPECT_EQ(target.type, ShaderTargetType::DXIL);
}

TEST(PreferredShaderTarget, WebGPUReturnsWGSL) {
    GpuCapabilityProfile caps{};
    caps.backendType = BackendType::WebGPU;
    auto target = PreferredShaderTarget(caps);
    EXPECT_EQ(target.type, ShaderTargetType::WGSL);
}
