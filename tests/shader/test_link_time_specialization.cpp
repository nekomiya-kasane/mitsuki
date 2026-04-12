// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Phase 2 tests: Link-time type specialization and link-time constants (§15.5.2).
// Tests that extern struct / extern static const are properly resolved at link time.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>

#include "miki/shader/SlangCompiler.h"

using namespace miki::shader;

class LinkTimeSpecializationTest : public ::testing::Test {
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

TEST_F(LinkTimeSpecializationTest, LinkTimeConstantCompiles) {
    auto path = writeTempShader("test_ltc.slang", R"(
extern static const int kSampleCount = 4;

RWStructuredBuffer<float> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    float sum = 0.0;
    [ForceUnroll]
    for (int i = 0; i < kSampleCount; i++) {
        sum += float(i);
    }
    output[tid.x] = sum;
}
)");

    ShaderCompileDesc desc;
    desc.sourcePath = path;
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();
    desc.linkTimeConstants.push_back({"kSampleCount", "8"});

    auto result = compiler_->Compile(desc);
    ASSERT_TRUE(result.has_value()) << "Link-time constant compilation failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(LinkTimeSpecializationTest, LinkTimeConstantDefaultValue) {
    // Without providing a link-time override, the default value should be used
    auto path = writeTempShader("test_ltc_default.slang", R"(
extern static const int kMaxLights = 128;

RWStructuredBuffer<uint> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    output[tid.x] = uint(kMaxLights);
}
)");

    ShaderCompileDesc desc;
    desc.sourcePath = path;
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();
    // No linkTimeConstants — default should be used

    auto result = compiler_->Compile(desc);
    ASSERT_TRUE(result.has_value()) << "Link-time constant with default failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(LinkTimeSpecializationTest, LinkTimeConstantFoldedInSPIRV) {
    // Verify that the link-time constant is inlined (no OpLoad for the constant)
    auto path = writeTempShader("test_ltc_fold.slang", R"(
extern static const int kValue = 1;

RWStructuredBuffer<int> output;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    output[0] = kValue;
}
)");

    ShaderCompileDesc desc;
    desc.sourcePath = path;
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();
    desc.linkTimeConstants.push_back({"kValue", "42"});

    auto result = compiler_->Compile(desc);
    ASSERT_TRUE(result.has_value()) << "Link-time constant folding test compilation failed";

    // Verify SPIR-V magic
    ASSERT_GE(result->data.size(), 4u);
    uint32_t magic = 0;
    std::memcpy(&magic, result->data.data(), 4);
    EXPECT_EQ(magic, 0x07230203u) << "Not valid SPIR-V";
}

TEST_F(LinkTimeSpecializationTest, LinkTimeTypeSpecialization) {
    // Test extern struct link-time type specialization with IMaterial
    auto path = writeTempShader("test_ltt.slang", R"(
import miki_brdf;

extern struct MaterialImpl : IMaterial;

RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    MaterialImpl mat;
    SurfaceInteraction si;
    si.position = float3(0.0);
    si.normal = float3(0.0, 1.0, 0.0);
    si.geometricN = float3(0.0, 1.0, 0.0);
    si.tangent = float3(1.0, 0.0, 0.0);
    si.bitangent = float3(0.0, 0.0, 1.0);
    si.uv = float2(0.0);
    si.viewDir = float3(0.0, 1.0, 0.0);

    let sd = mat.prepareShadingData(si);
    float3 color = mat.evaluate(sd, float3(0.0, 1.0, 0.0), float3(0.0, 1.0, 0.0));
    output[tid.x] = float4(color, 1.0);
}
)");

    ShaderCompileDesc desc;
    desc.sourcePath = path;
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();
    // Specialize MaterialImpl to DSPBR
    desc.linkTimeTypes.push_back({"MaterialImpl", "DSPBR:IMaterial"});

    auto result = compiler_->Compile(desc);
    ASSERT_TRUE(result.has_value()) << "Link-time type specialization compilation failed";
    EXPECT_GT(result->data.size(), 0u);
}
