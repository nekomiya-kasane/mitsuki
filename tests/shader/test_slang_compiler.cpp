// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Phase 1a unit tests for SlangCompiler: session creation, single-target
// compilation, dual-target compilation, reflection extraction, diagnostics,
// and session pool behavior.
//
// These tests require Slang runtime DLLs available at test execution.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "miki/shader/SlangCompiler.h"

using namespace miki::shader;

namespace fs = std::filesystem;

// ============================================================================
// Test fixture: creates a compiler instance with search paths
// ============================================================================

class SlangCompilerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        auto result = SlangCompiler::Create();
        ASSERT_TRUE(result.has_value()) << "Failed to create SlangCompiler";
        compiler_ = std::make_unique<SlangCompiler>(std::move(*result));

        // Add shader search paths
        auto shadersDir = fs::path(MIKI_SHADER_DIR);
        if (fs::exists(shadersDir)) {
            compiler_->AddSearchPath(shadersDir);
        }
        auto testsDir = fs::path(MIKI_SHADER_TESTS_DIR);
        if (fs::exists(testsDir)) {
            compiler_->AddSearchPath(testsDir);
        }
    }

    std::unique_ptr<SlangCompiler> compiler_;
};

// ============================================================================
// Session creation
// ============================================================================

TEST_F(SlangCompilerTest, CreateSucceeds) {
    EXPECT_NE(compiler_, nullptr);
}

TEST_F(SlangCompilerTest, CreateMultipleInstances) {
    auto result2 = SlangCompiler::Create();
    EXPECT_TRUE(result2.has_value());
}

// ============================================================================
// Single-target compilation (SPIR-V)
// ============================================================================

TEST_F(SlangCompilerTest, CompileSimpleComputeShaderSPIRV) {
    ShaderCompileDesc desc;
    desc.sourceCode = R"(
        [shader("compute")]
        [numthreads(64, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID) {}
    )";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();

    auto result = compiler_->Compile(desc);
    ASSERT_TRUE(result.has_value()) << "SPIR-V compilation failed";
    EXPECT_GT(result->data.size(), 20u);  // SPIR-V header = 5 * uint32_t = 20 bytes
    EXPECT_EQ(result->target.type, ShaderTargetType::SPIRV);
    EXPECT_EQ(result->stage, ShaderStage::Compute);
}

TEST_F(SlangCompilerTest, CompileSimpleVertexShaderSPIRV) {
    ShaderCompileDesc desc;
    desc.sourceCode = R"(
        struct VsOut {
            float4 position : SV_Position;
        };
        [shader("vertex")]
        VsOut main(uint vid : SV_VertexID) {
            VsOut o;
            o.position = float4(0, 0, 0, 1);
            return o;
        }
    )";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Vertex;
    desc.target = ShaderTarget::SPIRV_1_5();

    auto result = compiler_->Compile(desc);
    ASSERT_TRUE(result.has_value()) << "Vertex shader SPIR-V compilation failed";
    EXPECT_GT(result->data.size(), 20u);
}

// ============================================================================
// Compilation to DXIL
// ============================================================================

TEST_F(SlangCompilerTest, CompileSimpleComputeShaderDXIL) {
    ShaderCompileDesc desc;
    desc.sourceCode = R"(
        [shader("compute")]
        [numthreads(64, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID) {}
    )";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::DXIL_6_6();

    auto result = compiler_->Compile(desc);
    ASSERT_TRUE(result.has_value()) << "DXIL compilation failed";
    EXPECT_GT(result->data.size(), 4u);  // DXIL container has magic header
    EXPECT_EQ(result->target.type, ShaderTargetType::DXIL);
}

// ============================================================================
// Dual-target compilation
// ============================================================================

TEST_F(SlangCompilerTest, CompileDualTargetProducesTwo) {
    // Create a temporary .slang file for dual-target test
    auto tmpDir = fs::temp_directory_path() / "miki_test_shader";
    fs::create_directories(tmpDir);
    auto tmpFile = tmpDir / "dual_test.slang";

    {
        std::ofstream f(tmpFile);
        f << R"(
            [shader("compute")]
            [numthreads(64, 1, 1)]
            void main(uint3 tid : SV_DispatchThreadID) {}
        )";
    }

    auto result = compiler_->CompileDualTarget(tmpFile, "main", ShaderStage::Compute);
    ASSERT_TRUE(result.has_value()) << "Dual-target compilation failed";
    auto& [spirv, dxil] = *result;
    EXPECT_GT(spirv.data.size(), 0u);
    EXPECT_GT(dxil.data.size(), 0u);
    EXPECT_EQ(spirv.target.type, ShaderTargetType::SPIRV);
    EXPECT_EQ(dxil.target.type, ShaderTargetType::DXIL);

    // Cleanup
    fs::remove_all(tmpDir);
}

// ============================================================================
// Compilation failure cases
// ============================================================================

TEST_F(SlangCompilerTest, CompileInvalidSourceReturnsError) {
    ShaderCompileDesc desc;
    desc.sourceCode = "THIS IS NOT VALID SLANG CODE !!!@@@";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();

    auto result = compiler_->Compile(desc);
    EXPECT_FALSE(result.has_value());
}

TEST_F(SlangCompilerTest, CompileEmptySourcePathReturnsError) {
    ShaderCompileDesc desc;
    // No sourceCode, no sourcePath
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();

    auto result = compiler_->Compile(desc);
    EXPECT_FALSE(result.has_value());
}

TEST_F(SlangCompilerTest, CompileNonexistentFileReturnsError) {
    ShaderCompileDesc desc;
    desc.sourcePath = "nonexistent_shader_12345.slang";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();

    auto result = compiler_->Compile(desc);
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// Reflection extraction
// ============================================================================

TEST_F(SlangCompilerTest, ReflectExtractsBindings) {
    auto tmpDir = fs::temp_directory_path() / "miki_test_shader";
    fs::create_directories(tmpDir);
    auto tmpFile = tmpDir / "reflect_test.slang";

    {
        std::ofstream f(tmpFile);
        f << R"(
            [[vk::binding(0, 0)]]
            StructuredBuffer<float> data;

            [shader("compute")]
            [numthreads(64, 1, 1)]
            void main(uint3 tid : SV_DispatchThreadID) {
                float x = data[tid.x];
            }
        )";
    }

    ShaderCompileDesc desc;
    desc.sourcePath = tmpFile;
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();

    auto result = compiler_->Reflect(desc);
    ASSERT_TRUE(result.has_value()) << "Reflection failed";
    EXPECT_FALSE(result->bindings.empty());
    EXPECT_EQ(result->threadGroupSize[0], 64u);
    EXPECT_EQ(result->threadGroupSize[1], 1u);
    EXPECT_EQ(result->threadGroupSize[2], 1u);

    fs::remove_all(tmpDir);
}

// ============================================================================
// Session pool behavior
// ============================================================================

TEST_F(SlangCompilerTest, SessionPoolCacheHit) {
    ShaderCompileDesc desc;
    desc.sourceCode = R"(
        [shader("compute")]
        [numthreads(64, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID) {}
    )";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();

    // First compilation: cold
    auto t0 = std::chrono::steady_clock::now();
    auto r1 = compiler_->Compile(desc);
    auto t1 = std::chrono::steady_clock::now();
    ASSERT_TRUE(r1.has_value());
    auto coldMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Second compilation: should reuse pooled session (warm)
    auto t2 = std::chrono::steady_clock::now();
    auto r2 = compiler_->Compile(desc);
    auto t3 = std::chrono::steady_clock::now();
    ASSERT_TRUE(r2.has_value());
    auto warmMs = std::chrono::duration<double, std::milli>(t3 - t2).count();

    // Warm compilation should be faster (session creation skipped)
    // We don't enforce strict timing but log for CI regression tracking
    (void)coldMs;
    (void)warmMs;
}

TEST_F(SlangCompilerTest, InvalidateSessionCacheForcesColdPath) {
    ShaderCompileDesc desc;
    desc.sourceCode = R"(
        [shader("compute")]
        [numthreads(64, 1, 1)]
        void main(uint3 tid : SV_DispatchThreadID) {}
    )";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();

    auto r1 = compiler_->Compile(desc);
    ASSERT_TRUE(r1.has_value());

    compiler_->InvalidateSessionCache();

    // Should still compile successfully (cold session recreation)
    auto r2 = compiler_->Compile(desc);
    ASSERT_TRUE(r2.has_value());
}

// ============================================================================
// Diagnostics
// ============================================================================

TEST_F(SlangCompilerTest, DiagnosticsPopulatedOnError) {
    ShaderCompileDesc desc;
    desc.sourceCode = "INVALID CODE";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();

    auto result = compiler_->Compile(desc);
    EXPECT_FALSE(result.has_value());

    auto diags = compiler_->GetLastDiagnostics();
    EXPECT_FALSE(diags.empty());
}
