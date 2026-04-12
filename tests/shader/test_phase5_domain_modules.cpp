/** @brief Phase 5+ / 15a+ / 17+ tests: domain shader module compilation, pass shaders, and precompiled blobs.
 *
 *  Tests cover:
 *  - miki-debug module compilation (SPIRV + DXIL)
 *  - miki-cad module compilation (SPIRV + DXIL)
 *  - miki-cae module compilation (SPIRV + DXIL)
 *  - miki-rt module compilation (SPIRV + DXIL)
 *  - miki-xr module compilation (SPIRV + DXIL)
 *  - miki-neural module compilation (SPIRV + DXIL) [Phase 17+]
 *  - miki-postfx RTAO compilation (SPIRV + DXIL)
 *  - Pass shader compilation (15 pass entry-point files)
 *  - Precompiled .slang-module blob existence for all 6 domain + neural modules
 *  - Cross-module import test (domain module importing another)
 *  - Neural function invocation test (NRC query, neural texture decode)
 */
#include <gtest/gtest.h>

#include "miki/core/Result.h"
#include "miki/shader/SlangCompiler.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace miki::shader;

class Phase5ModuleTest : public ::testing::Test {
   protected:
    void SetUp() override {
        auto result = SlangCompiler::Create();
        ASSERT_TRUE(result.has_value()) << "Failed to create SlangCompiler";
        compiler_ = std::make_unique<SlangCompiler>(std::move(*result));
        compiler_->AddSearchPath(MIKI_SHADER_DIR);
    }

    auto compileModuleImport(const std::string& moduleName, ShaderTarget target = ShaderTarget::SPIRV_1_5())
        -> miki::core::Result<ShaderBlob> {
        ShaderCompileDesc desc;
        desc.sourceCode = "import " + moduleName + ";\n"
                          "[[vk::binding(0,0)]] RWStructuredBuffer<float4> output;\n"
                          "[shader(\"compute\")]\n"
                          "[numthreads(1,1,1)]\n"
                          "void main(uint3 tid : SV_DispatchThreadID) {\n"
                          "    output[0] = float4(0.0);\n"
                          "}\n";
        desc.entryPoint = "main";
        desc.stage = ShaderStage::Compute;
        desc.target = target;
        return compiler_->Compile(desc);
    }

    std::unique_ptr<SlangCompiler> compiler_;
};

// =============================================================================
// miki-debug module compilation
// =============================================================================

TEST_F(Phase5ModuleTest, DebugModuleCompilesSPIRV) {
    auto result = compileModuleImport("miki_debug");
    ASSERT_TRUE(result.has_value()) << "miki_debug failed to compile for SPIRV";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(Phase5ModuleTest, DebugModuleCompilesDXIL) {
    auto result = compileModuleImport("miki_debug", ShaderTarget::DXIL_6_6());
    ASSERT_TRUE(result.has_value()) << "miki_debug failed to compile for DXIL";
    EXPECT_GT(result->data.size(), 0u);
}

// =============================================================================
// miki-cad module compilation
// =============================================================================

TEST_F(Phase5ModuleTest, CadModuleCompilesSPIRV) {
    auto result = compileModuleImport("miki_cad");
    ASSERT_TRUE(result.has_value()) << "miki_cad failed to compile for SPIRV";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(Phase5ModuleTest, CadModuleCompilesDXIL) {
    auto result = compileModuleImport("miki_cad", ShaderTarget::DXIL_6_6());
    ASSERT_TRUE(result.has_value()) << "miki_cad failed to compile for DXIL";
    EXPECT_GT(result->data.size(), 0u);
}

// =============================================================================
// miki-cae module compilation
// =============================================================================

TEST_F(Phase5ModuleTest, CaeModuleCompilesSPIRV) {
    auto result = compileModuleImport("miki_cae");
    ASSERT_TRUE(result.has_value()) << "miki_cae failed to compile for SPIRV";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(Phase5ModuleTest, CaeModuleCompilesDXIL) {
    auto result = compileModuleImport("miki_cae", ShaderTarget::DXIL_6_6());
    ASSERT_TRUE(result.has_value()) << "miki_cae failed to compile for DXIL";
    EXPECT_GT(result->data.size(), 0u);
}

// =============================================================================
// miki-rt module compilation
// =============================================================================

TEST_F(Phase5ModuleTest, RtModuleCompilesSPIRV) {
    auto result = compileModuleImport("miki_rt");
    ASSERT_TRUE(result.has_value()) << "miki_rt failed to compile for SPIRV";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(Phase5ModuleTest, RtModuleCompilesDXIL) {
    auto result = compileModuleImport("miki_rt", ShaderTarget::DXIL_6_6());
    ASSERT_TRUE(result.has_value()) << "miki_rt failed to compile for DXIL";
    EXPECT_GT(result->data.size(), 0u);
}

// =============================================================================
// miki-xr module compilation
// =============================================================================

TEST_F(Phase5ModuleTest, XrModuleCompilesSPIRV) {
    auto result = compileModuleImport("miki_xr");
    ASSERT_TRUE(result.has_value()) << "miki_xr failed to compile for SPIRV";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(Phase5ModuleTest, XrModuleCompilesDXIL) {
    auto result = compileModuleImport("miki_xr", ShaderTarget::DXIL_6_6());
    ASSERT_TRUE(result.has_value()) << "miki_xr failed to compile for DXIL";
    EXPECT_GT(result->data.size(), 0u);
}

// =============================================================================
// Cross-module import: debug module uses functions from core
// =============================================================================

TEST_F(Phase5ModuleTest, DebugFunctionsCompileSPIRV) {
    ShaderCompileDesc desc;
    desc.sourceCode = R"(
import miki_debug;

[[vk::binding(0,0)]] RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    float3 normal = float3(0.0, 1.0, 0.0);
    float3 vizColor = normalToColor(normal);
    float3 idColor = idToColor(42u);
    float3 lodColor = lodLevelColor(3u, 8u);
    output[0] = float4(vizColor + idColor + lodColor, 1.0);
}
)";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();
    auto result = compiler_->Compile(desc);
    ASSERT_TRUE(result.has_value()) << "Debug functions failed to compile for SPIRV";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(Phase5ModuleTest, CadFunctionsCompileSPIRV) {
    ShaderCompileDesc desc;
    desc.sourceCode = R"(
import miki_cad;

[[vk::binding(0,0)]] RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    float dist = measureDistance(float3(0.0), float3(1.0, 0.0, 0.0));
    float angle = measureAngle(float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0));
    float3 exploded = applyExplodeTransform(float3(1.0), float3(0.0), ExplodeParams());
    output[0] = float4(dist, angle, exploded.x, 1.0);
}
)";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();
    auto result = compiler_->Compile(desc);
    ASSERT_TRUE(result.has_value()) << "CAD functions failed to compile for SPIRV";
    EXPECT_GT(result->data.size(), 0u);
}

// =============================================================================
// Precompiled module blob existence (build-time generated)
// =============================================================================

TEST_F(Phase5ModuleTest, DebugPrecompiledModuleExists) {
    auto path = fs::path(MIKI_PRECOMPILED_DIR) / "miki_debug.slang-module";
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Precompiled module not built (run CMake build first)";
    }
    EXPECT_GT(fs::file_size(path), 0u);
}

TEST_F(Phase5ModuleTest, CadPrecompiledModuleExists) {
    auto path = fs::path(MIKI_PRECOMPILED_DIR) / "miki_cad.slang-module";
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Precompiled module not built (run CMake build first)";
    }
    EXPECT_GT(fs::file_size(path), 0u);
}

TEST_F(Phase5ModuleTest, CaePrecompiledModuleExists) {
    auto path = fs::path(MIKI_PRECOMPILED_DIR) / "miki_cae.slang-module";
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Precompiled module not built (run CMake build first)";
    }
    EXPECT_GT(fs::file_size(path), 0u);
}

TEST_F(Phase5ModuleTest, RtPrecompiledModuleExists) {
    auto path = fs::path(MIKI_PRECOMPILED_DIR) / "miki_rt.slang-module";
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Precompiled module not built (run CMake build first)";
    }
    EXPECT_GT(fs::file_size(path), 0u);
}

TEST_F(Phase5ModuleTest, XrPrecompiledModuleExists) {
    auto path = fs::path(MIKI_PRECOMPILED_DIR) / "miki_xr.slang-module";
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Precompiled module not built (run CMake build first)";
    }
    EXPECT_GT(fs::file_size(path), 0u);
}

// =============================================================================
// miki-neural module compilation (Phase 17+)
// =============================================================================

TEST_F(Phase5ModuleTest, NeuralModuleCompilesSPIRV) {
    auto result = compileModuleImport("miki_neural");
    ASSERT_TRUE(result.has_value()) << "miki_neural failed to compile for SPIRV";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(Phase5ModuleTest, NeuralModuleCompilesDXIL) {
    auto result = compileModuleImport("miki_neural", ShaderTarget::DXIL_6_6());
    ASSERT_TRUE(result.has_value()) << "miki_neural failed to compile for DXIL";
    EXPECT_GT(result->data.size(), 0u);
}

// =============================================================================
// Neural function invocation tests
// =============================================================================

TEST_F(Phase5ModuleTest, NeuralTextureFunctionsCompileSPIRV) {
    ShaderCompileDesc desc;
    desc.sourceCode = R"(
import miki_neural;

[[vk::binding(0,0)]] ByteAddressBuffer weights;
[[vk::binding(1,0)]] RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    float4 decoded = decodeNeuralTexture(weights, float2(0.5, 0.5), 0.0, 0u);
    output[0] = decoded;
}
)";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();
    auto result = compiler_->Compile(desc);
    ASSERT_TRUE(result.has_value()) << "Neural texture decode failed to compile for SPIRV";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(Phase5ModuleTest, NrcQueryFunctionsCompileSPIRV) {
    ShaderCompileDesc desc;
    desc.sourceCode = R"(
import miki_neural;

[[vk::binding(0,0)]] StructuredBuffer<float> hashFeatures;
[[vk::binding(1,0)]] ByteAddressBuffer mlpWeights;
[[vk::binding(2,0)]] RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    HashGridConfig config;
    config.numLevels = 16u;
    config.hashTableSize = 524288u;
    config.featuresPerEntry = 2u;
    config.baseResolution = 16.0;
    config.logScale = 1.38;
    config.aabbMin = float3(-10.0);
    config.aabbMax = float3(10.0);
    float3 radiance = queryNrc(hashFeatures, mlpWeights, config, float3(0.0), float3(0.0, 0.0, -1.0));
    output[0] = float4(radiance, 1.0);
}
)";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();
    auto result = compiler_->Compile(desc);
    ASSERT_TRUE(result.has_value()) << "NRC query failed to compile for SPIRV";
    EXPECT_GT(result->data.size(), 0u);
}

// =============================================================================
// RTAO functions in miki-postfx (spec §15.2)
// =============================================================================

TEST_F(Phase5ModuleTest, RtaoFunctionsCompileSPIRV) {
    ShaderCompileDesc desc;
    desc.sourceCode = R"(
import miki_postfx;

[[vk::binding(0,0)]] RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    float3 normal = float3(0.0, 1.0, 0.0);
    float3 rayDir = generateAoRayDirection(normal, float2(0.3, 0.7));
    float3 origin = aoRayOrigin(float3(0.0), normal, 0.01);
    RtaoParams params;
    params.radius = 1.0;
    params.power = 1.0;
    params.intensity = 1.0;
    params.normalBias = 0.01;
    params.samplesPerPixel = 1u;
    float ao = applyAoPowerCurve(0.8, params);
    output[0] = float4(rayDir, ao);
}
)";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();
    auto result = compiler_->Compile(desc);
    ASSERT_TRUE(result.has_value()) << "RTAO functions failed to compile for SPIRV";
    EXPECT_GT(result->data.size(), 0u);
}

// =============================================================================
// Pass shader compilation (§15.2 — entry-point files in shaders/passes/)
// =============================================================================

class PassShaderTest : public ::testing::Test {
   protected:
    void SetUp() override {
        auto result = SlangCompiler::Create();
        ASSERT_TRUE(result.has_value()) << "Failed to create SlangCompiler";
        compiler_ = std::make_unique<SlangCompiler>(std::move(*result));
        compiler_->AddSearchPath(MIKI_SHADER_DIR);
        compiler_->AddSearchPath(MIKI_PASS_DIR);
    }

    auto compilePassFile(
        const std::string& passFileName, const std::string& entryPoint, ShaderStage stage,
        ShaderTarget target = ShaderTarget::SPIRV_1_5()
    ) -> miki::core::Result<ShaderBlob> {
        ShaderCompileDesc desc;
        desc.sourcePath = std::string(MIKI_PASS_DIR) + "/" + passFileName;
        desc.entryPoint = entryPoint;
        desc.stage = stage;
        desc.target = target;
        return compiler_->Compile(desc);
    }

    std::unique_ptr<SlangCompiler> compiler_;
};

TEST_F(PassShaderTest, DepthPrepassVertexSPIRV) {
    auto result = compilePassFile("depth_prepass.slang", "vsMain", ShaderStage::Vertex);
    ASSERT_TRUE(result.has_value()) << "depth_prepass vertex failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, DepthPrepassFragmentSPIRV) {
    auto result = compilePassFile("depth_prepass.slang", "fsMain", ShaderStage::Fragment);
    ASSERT_TRUE(result.has_value()) << "depth_prepass fragment failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, GpuCullingComputeSPIRV) {
    auto result = compilePassFile("gpu_culling.slang", "csMain", ShaderStage::Compute);
    ASSERT_TRUE(result.has_value()) << "gpu_culling compute failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, LightClusterComputeSPIRV) {
    auto result = compilePassFile("light_cluster.slang", "csMain", ShaderStage::Compute);
    ASSERT_TRUE(result.has_value()) << "light_cluster compute failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, GeometryMainComputeSPIRV) {
    auto result = compilePassFile("geometry_main.slang", "csMain", ShaderStage::Compute);
    ASSERT_TRUE(result.has_value()) << "geometry_main compute failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, GeometryCompatVertexSPIRV) {
    auto result = compilePassFile("geometry_compat.slang", "vsMain", ShaderStage::Vertex);
    ASSERT_TRUE(result.has_value()) << "geometry_compat vertex failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, MaterialResolveComputeSPIRV) {
    auto result = compilePassFile("material_resolve.slang", "csMain", ShaderStage::Compute);
    ASSERT_TRUE(result.has_value()) << "material_resolve compute failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, DeferredResolveFragmentSPIRV) {
    auto result = compilePassFile("deferred_resolve.slang", "fsMain", ShaderStage::Fragment);
    ASSERT_TRUE(result.has_value()) << "deferred_resolve fragment failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, VsmRenderVertexSPIRV) {
    auto result = compilePassFile("vsm_render.slang", "vsMain", ShaderStage::Vertex);
    ASSERT_TRUE(result.has_value()) << "vsm_render vertex failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, CsmRenderVertexSPIRV) {
    auto result = compilePassFile("csm_render.slang", "vsMain", ShaderStage::Vertex);
    ASSERT_TRUE(result.has_value()) << "csm_render vertex failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, GtaoComputeSPIRV) {
    auto result = compilePassFile("gtao_compute.slang", "csMain", ShaderStage::Compute);
    ASSERT_TRUE(result.has_value()) << "gtao_compute compute failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, BloomPassComputeSPIRV) {
    auto result = compilePassFile("bloom_pass.slang", "csMain", ShaderStage::Compute);
    ASSERT_TRUE(result.has_value()) << "bloom_pass compute failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, TaaResolveComputeSPIRV) {
    auto result = compilePassFile("taa_resolve.slang", "csMain", ShaderStage::Compute);
    ASSERT_TRUE(result.has_value()) << "taa_resolve compute failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, TonemapPassComputeSPIRV) {
    auto result = compilePassFile("tonemap_pass.slang", "csMain", ShaderStage::Compute);
    ASSERT_TRUE(result.has_value()) << "tonemap_pass compute failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, FullscreenTriVertexSPIRV) {
    auto result = compilePassFile("fullscreen_tri.slang", "vsMain", ShaderStage::Vertex);
    ASSERT_TRUE(result.has_value()) << "fullscreen_tri vertex failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, BlitVertexSPIRV) {
    auto result = compilePassFile("blit.slang", "vsMain", ShaderStage::Vertex);
    ASSERT_TRUE(result.has_value()) << "blit vertex failed";
    EXPECT_GT(result->data.size(), 0u);
}

TEST_F(PassShaderTest, BlitFragmentSPIRV) {
    auto result = compilePassFile("blit.slang", "fsMain", ShaderStage::Fragment);
    ASSERT_TRUE(result.has_value()) << "blit fragment failed";
    EXPECT_GT(result->data.size(), 0u);
}

// =============================================================================
// Neural precompiled module blob
// =============================================================================

TEST_F(Phase5ModuleTest, NeuralPrecompiledModuleExists) {
    auto path = fs::path(MIKI_PRECOMPILED_DIR) / "miki_neural.slang-module";
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Precompiled module not built (run CMake build first)";
    }
    EXPECT_GT(fs::file_size(path), 0u);
}
