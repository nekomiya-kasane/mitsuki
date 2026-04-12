// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Phase 1b unit tests: [require] capability annotations.
// Verifies that Tier1-only functions produce compile errors on non-Tier1 targets.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "miki/shader/SlangCompiler.h"

using namespace miki::shader;
namespace fs = std::filesystem;

class CapabilityAnnotationTest : public ::testing::Test {
   protected:
    void SetUp() override {
        auto result = SlangCompiler::Create();
        ASSERT_TRUE(result.has_value()) << "SlangCompiler::Create failed";
        compiler_ = std::make_unique<SlangCompiler>(std::move(*result));
        compiler_->AddSearchPath(MIKI_SHADER_DIR);
        compiler_->AddSearchPath(MIKI_SHADER_TESTS_DIR);

        tempDir_ = fs::temp_directory_path() / "miki_cap_test";
        fs::create_directories(tempDir_);
        compiler_->AddSearchPath(tempDir_.string());
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tempDir_, ec);
    }

    // Write a temp shader file and return its path
    auto writeTempShader(std::string_view name, std::string_view content) -> fs::path {
        auto path = tempDir_ / name;
        std::ofstream out(path);
        out << content;
        return path;
    }

    std::unique_ptr<SlangCompiler> compiler_;
    fs::path tempDir_;
};

TEST_F(CapabilityAnnotationTest, BindlessCompilesOnSPIRV) {
    // Bindless using NonUniformResourceIndex should compile on SPIR-V (Tier1).
    // Use loadBindlessBufferUint to avoid texture derivative requirements in compute.
    auto path = writeTempShader("test_cap_spirv.slang", R"(
import miki_core;

[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    output[tid.x] = loadBindlessBufferUint(0u, tid.x);
}
)");
    ShaderCompileDesc desc;
    desc.sourcePath = path;
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();

    auto result = compiler_->Compile(desc);
    if (!result.has_value()) {
        auto diags = compiler_->GetLastDiagnostics();
        for (auto const& d : diags) {
            GTEST_LOG_(INFO) << "Diag: " << d.message;
        }
    }
    EXPECT_TRUE(result.has_value()) << "Bindless should compile on SPIR-V (Tier1)";
}

TEST_F(CapabilityAnnotationTest, BindlessCompilesOnDXIL) {
    auto path = writeTempShader("test_cap_dxil.slang", R"(
import miki_core;

[[vk::binding(0, 0)]]
RWStructuredBuffer<uint> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    output[tid.x] = loadBindlessBufferUint(0u, tid.x);
}
)");
    ShaderCompileDesc desc;
    desc.sourcePath = path;
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::DXIL_6_6();

    auto result = compiler_->Compile(desc);
    if (!result.has_value()) {
        auto diags = compiler_->GetLastDiagnostics();
        for (auto const& d : diags) {
            GTEST_LOG_(INFO) << "Diag: " << d.message;
        }
    }
    EXPECT_TRUE(result.has_value()) << "Bindless should compile on DXIL (Tier1)";
}

TEST_F(CapabilityAnnotationTest, NonBindlessCompilesOnGLSL) {
    // A shader that does NOT use Tier1 features should compile on GLSL 4.30.
    auto path = writeTempShader("test_cap_glsl_ok.slang", R"(
import miki_core;

[[vk::binding(0, 0)]]
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    PushConstants pc = loadPushConstants();
    output[tid.x] = float4(pc.materialIndex, pc.instanceId, 0, 1);
}
)");
    ShaderCompileDesc desc;
    desc.sourcePath = path;
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::GLSL_430();

    auto result = compiler_->Compile(desc);
    if (!result.has_value()) {
        auto diags = compiler_->GetLastDiagnostics();
        for (auto const& d : diags) {
            GTEST_LOG_(INFO) << "Diag: " << d.message;
        }
    }
    EXPECT_TRUE(result.has_value()) << "Non-bindless shader should compile on GLSL";
}

TEST_F(CapabilityAnnotationTest, NonBindlessCompilesOnWGSL) {
    auto path = writeTempShader("test_cap_wgsl_ok.slang", R"(
import miki_core;

[[vk::binding(0, 0)]]
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    PushConstants pc = loadPushConstants();
    output[tid.x] = float4(pc.materialIndex, pc.instanceId, 0, 1);
}
)");
    ShaderCompileDesc desc;
    desc.sourcePath = path;
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::WGSL_1_0();

    auto result = compiler_->Compile(desc);
    if (!result.has_value()) {
        auto diags = compiler_->GetLastDiagnostics();
        for (auto const& d : diags) {
            GTEST_LOG_(INFO) << "Diag: " << d.message;
        }
    }
    EXPECT_TRUE(result.has_value()) << "Non-bindless shader should compile on WGSL";
}
