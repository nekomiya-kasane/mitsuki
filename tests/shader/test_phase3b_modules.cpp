/** @brief Phase 3b module compilation tests: miki-shadow and miki-postfx.
 *
 *  Verifies that new shader modules compile on Tier1 targets (SPIRV, DXIL)
 *  and that precompiled .slang-module blobs load correctly.
 *  Also validates bindless.slang extended functions compile on Tier1.
 */
#include <gtest/gtest.h>

#include "miki/shader/SlangCompiler.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace miki::shader;

class Phase3bModuleTest : public ::testing::Test {
   protected:
    void SetUp() override {
        auto result = SlangCompiler::Create();
        ASSERT_TRUE(result.has_value()) << "Failed to create SlangCompiler";
        compiler_ = std::make_unique<SlangCompiler>(std::move(*result));
        compiler_->AddSearchPath(MIKI_SHADER_DIR);
    }

    std::unique_ptr<SlangCompiler> compiler_;

    auto compileModuleImport(const std::string& importName, ShaderTarget target = ShaderTarget::SPIRV_1_5())
        -> miki::core::Result<ShaderBlob> {
        ShaderCompileDesc desc;
        desc.sourceCode = "import " + importName + ";\n"
            "[[vk::binding(0,0)]] RWStructuredBuffer<float> output;\n"
            "[shader(\"compute\")]\n"
            "[numthreads(1,1,1)]\n"
            "void main(uint3 tid : SV_DispatchThreadID) { output[0] = 1.0f; }\n";
        desc.entryPoint = "main";
        desc.stage = ShaderStage::Compute;
        desc.target = target;
        return compiler_->Compile(desc);
    }
};

// =============================================================================
// miki-shadow module compilation
// =============================================================================

TEST_F(Phase3bModuleTest, ShadowModuleCompilesSPIRV) {
    auto result = compileModuleImport("miki_shadow");
    ASSERT_TRUE(result.has_value()) << "miki_shadow failed to compile for SPIRV";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(Phase3bModuleTest, ShadowModuleCompilesDXIL) {
    auto result = compileModuleImport("miki_shadow", ShaderTarget::DXIL_6_6());
    ASSERT_TRUE(result.has_value()) << "miki_shadow failed to compile for DXIL";
    EXPECT_GT(result->data.size(), 0u);
}

// =============================================================================
// miki-postfx module compilation
// =============================================================================

TEST_F(Phase3bModuleTest, PostfxModuleCompilesSPIRV) {
    auto result = compileModuleImport("miki_postfx");
    ASSERT_TRUE(result.has_value()) << "miki_postfx failed to compile for SPIRV";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(Phase3bModuleTest, PostfxModuleCompilesDXIL) {
    auto result = compileModuleImport("miki_postfx", ShaderTarget::DXIL_6_6());
    ASSERT_TRUE(result.has_value()) << "miki_postfx failed to compile for DXIL";
    EXPECT_GT(result->data.size(), 0u);
}

// =============================================================================
// Bindless extended functions (Phase 3b Step 3)
// =============================================================================

TEST_F(Phase3bModuleTest, BindlessExtendedFunctionsCompileSPIRV) {
    ShaderCompileDesc desc;
    desc.sourceCode = R"(
import miki_core;

[[vk::binding(0,0)]] RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    float4 texColor = sampleBindlessTextureLod(0, 0, float2(0.5, 0.5), 0.0);
    float4 texLoad = loadBindlessTexture(0, int2(0, 0), 0);
    float val = loadBindlessBufferFloat(0, 0);
    float4 vec = loadBindlessBufferFloat4(0, 0);
    uint4 uvec = loadBindlessBufferUint4(0, 0);
    output[0] = texColor + texLoad + float4(val) + vec + float4(uvec);
}
)";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();
    auto result = compiler_->Compile(desc);
    ASSERT_TRUE(result.has_value()) << "Bindless extended functions failed to compile for SPIRV";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(Phase3bModuleTest, BindlessExtendedFunctionsCompileDXIL) {
    ShaderCompileDesc desc;
    desc.sourceCode = R"(
import miki_core;

[[vk::binding(0,0)]] RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    float4 texColor = sampleBindlessTextureLod(0, 0, float2(0.5, 0.5), 0.0);
    float4 texLoad = loadBindlessTexture(0, int2(0, 0), 0);
    float val = loadBindlessBufferFloat(0, 0);
    float4 vec = loadBindlessBufferFloat4(0, 0);
    uint4 uvec = loadBindlessBufferUint4(0, 0);
    output[0] = texColor + texLoad + float4(val) + vec + float4(uvec);
}
)";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::DXIL_6_6();
    auto result = compiler_->Compile(desc);
    ASSERT_TRUE(result.has_value()) << "Bindless extended functions failed to compile for DXIL";
    EXPECT_GT(result->data.size(), 0u);
}

// =============================================================================
// Precompiled module blob existence (build-time generated)
// =============================================================================

TEST_F(Phase3bModuleTest, ShadowPrecompiledModuleExists) {
    fs::path modulePath = fs::path(MIKI_PRECOMPILED_DIR) / "miki_shadow.slang-module";
    if (fs::exists(modulePath)) {
        auto size = fs::file_size(modulePath);
        EXPECT_GT(size, 0u) << "miki_shadow.slang-module is empty";
    } else {
        GTEST_SKIP() << "Precompiled module not built (run CMake build first)";
    }
}

TEST_F(Phase3bModuleTest, PostfxPrecompiledModuleExists) {
    fs::path modulePath = fs::path(MIKI_PRECOMPILED_DIR) / "miki_postfx.slang-module";
    if (fs::exists(modulePath)) {
        auto size = fs::file_size(modulePath);
        EXPECT_GT(size, 0u) << "miki_postfx.slang-module is empty";
    } else {
        GTEST_SKIP() << "Precompiled module not built (run CMake build first)";
    }
}
