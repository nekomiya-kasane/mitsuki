// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Phase 2 tests: Reflection-driven descriptor layout generation (§9.3).
// Tests GeneratePipelineLayout() and DetectLayoutChanges().

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "miki/shader/ShaderLayoutGenerator.h"
#include "miki/shader/SlangCompiler.h"

using namespace miki::shader;
using namespace miki::rhi;

class ReflectionLayoutTest : public ::testing::Test {
   protected:
    void SetUp() override {
        auto result = SlangCompiler::Create();
        ASSERT_TRUE(result.has_value()) << "SlangCompiler::Create failed";
        compiler_ = std::make_unique<SlangCompiler>(std::move(*result));
        compiler_->AddSearchPath(MIKI_SHADER_DIR);
        compiler_->AddSearchPath(MIKI_SHADER_TESTS_DIR);
    }

    auto writeTempShader(std::string const& iName, std::string const& iContent) -> std::filesystem::path {
        auto path = std::filesystem::temp_directory_path() / iName;
        std::ofstream ofs(path, std::ios::binary);
        ofs.write(iContent.data(), static_cast<std::streamsize>(iContent.size()));
        return path;
    }

    std::unique_ptr<SlangCompiler> compiler_;
};

TEST_F(ReflectionLayoutTest, ReflectionGeneratesPerSetLayout) {
    auto path = writeTempShader("test_reflect_layout.slang", R"(
import miki_core;

[[vk::binding(0, 0)]]
ConstantBuffer<float4x4> viewProjection;

[[vk::binding(1, 0)]]
StructuredBuffer<float4> positions;

[[vk::binding(0, 1)]]
Texture2D<float4> albedoTex;

[[vk::binding(1, 1)]]
SamplerState albedoSampler;

[[vk::binding(0, 2)]]
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    output[tid.x] = positions[tid.x];
}
)");

    ShaderCompileDesc desc;
    desc.sourcePath = path;
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();

    auto reflection = compiler_->Reflect(desc);
    ASSERT_TRUE(reflection.has_value()) << "Reflection failed";

    auto layout = GeneratePipelineLayout(*reflection);

    // Should have bindings (at least some sets used)
    EXPECT_FALSE(layout.sets.empty()) << "No descriptor sets generated from reflection";

    // All sets should have non-empty bindings
    for (auto const& setLayout : layout.sets) {
        EXPECT_FALSE(setLayout.layout.bindings.empty()) << "Set " << setLayout.set << " has no bindings";
    }
}

TEST_F(ReflectionLayoutTest, ReflectionBindingsFilterSet3AndAbove) {
    // Bindings in set 3+ (bindless) should be excluded from auto-generation
    ShaderReflection reflection;
    reflection.bindings.push_back(
        {.set = 0, .binding = 0, .type = BindingType::UniformBuffer, .count = 1, .name = "ubo"}
    );
    reflection.bindings.push_back(
        {.set = 3, .binding = 0, .type = BindingType::BindlessTextures, .count = 0, .name = "bindless"}
    );

    auto layout = GeneratePipelineLayout(reflection);

    // Only set 0 should be present
    EXPECT_EQ(layout.sets.size(), 1u);
    EXPECT_EQ(layout.sets[0].set, 0u);
}

TEST_F(ReflectionLayoutTest, DetectLayoutBreakingChanges) {
    ReflectedPipelineLayout prev;
    {
        ReflectedSetLayout set0;
        set0.set = 0;
        set0.layout.bindings.push_back({.binding = 0, .type = BindingType::UniformBuffer, .count = 1});
        set0.layout.bindings.push_back({.binding = 1, .type = BindingType::StorageBuffer, .count = 1});
        prev.sets.push_back(std::move(set0));
    }

    ReflectedPipelineLayout curr;
    {
        ReflectedSetLayout set0;
        set0.set = 0;
        // Changed binding 0 type (breaking)
        set0.layout.bindings.push_back({.binding = 0, .type = BindingType::StorageBuffer, .count = 1});
        // Binding 1 removed (breaking)
        curr.sets.push_back(std::move(set0));
    }

    auto changes = DetectLayoutChanges(prev, curr);
    EXPECT_GE(changes.size(), 2u) << "Should detect type change + binding removal";
}
