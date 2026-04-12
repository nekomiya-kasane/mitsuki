// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Phase 1b unit tests: CompileAllTargets (5 targets) and CompileActiveTargets.

#include <gtest/gtest.h>

#include <filesystem>

#include "miki/shader/SlangCompiler.h"

using namespace miki::shader;

class MultiTargetTest : public ::testing::Test {
   protected:
    void SetUp() override {
        auto result = SlangCompiler::Create();
        ASSERT_TRUE(result.has_value()) << "SlangCompiler::Create failed";
        compiler_ = std::make_unique<SlangCompiler>(std::move(*result));
        compiler_->AddSearchPath(MIKI_SHADER_DIR);
        compiler_->AddSearchPath(MIKI_SHADER_TESTS_DIR);
    }
    std::unique_ptr<SlangCompiler> compiler_;
};

TEST_F(MultiTargetTest, CompileAllTargetsProduces5Blobs) {
    auto shaderPath = std::filesystem::path(MIKI_SHADER_TESTS_DIR) / "probe_struct_array.slang";
    auto result = compiler_->CompileAllTargets(shaderPath, "main", ShaderStage::Compute);
    ASSERT_TRUE(result.has_value()) << "CompileAllTargets failed";

    auto& blobs = *result;
    EXPECT_EQ(blobs.size(), SlangCompiler::kTargetCount);
    for (size_t i = 0; i < SlangCompiler::kTargetCount; ++i) {
        EXPECT_GT(blobs[i].data.size(), 0u) << "Blob " << i << " is empty";
    }
}

TEST_F(MultiTargetTest, AllTargetsSPIRVBlobIsValid) {
    auto shaderPath = std::filesystem::path(MIKI_SHADER_TESTS_DIR) / "probe_struct_array.slang";
    auto result = compiler_->CompileAllTargets(shaderPath, "main", ShaderStage::Compute);
    ASSERT_TRUE(result.has_value());

    // Index 0 = SPIRV, should start with SPIR-V magic number 0x07230203
    auto& spirvBlob = (*result)[0];
    ASSERT_GE(spirvBlob.data.size(), 4u);
    uint32_t magic = 0;
    std::memcpy(&magic, spirvBlob.data.data(), 4);
    EXPECT_EQ(magic, 0x07230203u) << "SPIR-V blob missing magic number";
}

TEST_F(MultiTargetTest, AllTargetsGLSLBlobContainsVersion) {
    auto shaderPath = std::filesystem::path(MIKI_SHADER_TESTS_DIR) / "probe_struct_array.slang";
    auto result = compiler_->CompileAllTargets(shaderPath, "main", ShaderStage::Compute);
    ASSERT_TRUE(result.has_value());

    // Index 2 = GLSL, should contain "#version"
    auto& glslBlob = (*result)[2];
    std::string glslStr(reinterpret_cast<const char*>(glslBlob.data.data()), glslBlob.data.size());
    EXPECT_NE(glslStr.find("#version"), std::string::npos) << "GLSL blob missing #version directive";
}

TEST_F(MultiTargetTest, AllTargetsWGSLBlobMinSize) {
    auto shaderPath = std::filesystem::path(MIKI_SHADER_TESTS_DIR) / "probe_struct_array.slang";
    auto result = compiler_->CompileAllTargets(shaderPath, "main", ShaderStage::Compute);
    ASSERT_TRUE(result.has_value());

    // Index 3 = WGSL, spec says >= 8 bytes
    EXPECT_GE((*result)[3].data.size(), 8u) << "WGSL blob < 8 bytes";
}

TEST_F(MultiTargetTest, CompileActiveTargetsSubset) {
    auto shaderPath = std::filesystem::path(MIKI_SHADER_TESTS_DIR) / "probe_struct_array.slang";
    std::array<ShaderTarget, 2> targets = {ShaderTarget::SPIRV_1_5(), ShaderTarget::GLSL_430()};
    auto result = compiler_->CompileActiveTargets(shaderPath, "main", ShaderStage::Compute, targets);
    ASSERT_TRUE(result.has_value());

    auto& blobs = *result;
    EXPECT_EQ(blobs.size(), 2u);
    EXPECT_GT(blobs[0].data.size(), 0u) << "SPIRV blob empty";
    EXPECT_GT(blobs[1].data.size(), 0u) << "GLSL blob empty";
}

TEST_F(MultiTargetTest, CompileActiveTargetsEmptyReturnsError) {
    auto shaderPath = std::filesystem::path(MIKI_SHADER_TESTS_DIR) / "probe_struct_array.slang";
    std::span<const ShaderTarget> empty;
    auto result = compiler_->CompileActiveTargets(shaderPath, "main", ShaderStage::Compute, empty);
    EXPECT_FALSE(result.has_value()) << "Empty targets should fail";
}
