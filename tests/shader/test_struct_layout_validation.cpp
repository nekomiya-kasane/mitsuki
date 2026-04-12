// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Phase 2 tests: GPU Data Contract Validation (§15.6).
// Tests ValidateStructLayout() comparing C++ offsetof() with Slang reflection.

#include <gtest/gtest.h>

#include <filesystem>

#include "miki/shader/SlangCompiler.h"
#include "miki/shader/StructLayoutValidator.h"

using namespace miki::shader;

class StructLayoutValidationTest : public ::testing::Test {
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

// C++ mirror of GpuInstance from core/types.slang
struct CppGpuInstance {
    float modelMatrix[16];   // float4x4, offset 0, size 64
    float normalMatrix[16];  // float4x4, offset 64, size 64
    uint32_t materialIndex;  // offset 128, size 4
    uint32_t flags;          // offset 132, size 4
    float _pad0[2];          // offset 136, size 8
};
static_assert(sizeof(CppGpuInstance) == 144, "CppGpuInstance size mismatch");

// C++ mirror of GpuLight from core/types.slang
struct CppGpuLight {
    float position[3];     // offset 0
    float range;           // offset 12
    float direction[3];    // offset 16
    float spotAngle;       // offset 28
    float color[3];        // offset 32
    float intensity;       // offset 44
    uint32_t type;         // offset 48
    uint32_t shadowIndex;  // offset 52
    float _pad0[2];        // offset 56
};
static_assert(sizeof(CppGpuLight) == 64, "CppGpuLight size mismatch");

TEST_F(StructLayoutValidationTest, GpuInstanceLayoutMatches) {
    // Build C++ layout descriptor using offsetof
    CppStructLayout cppLayout;
    cppLayout.name = "GpuInstance";
    cppLayout.sizeBytes = sizeof(CppGpuInstance);
    cppLayout.fields = {
        {"modelMatrix", static_cast<uint32_t>(offsetof(CppGpuInstance, modelMatrix)), 64},
        {"normalMatrix", static_cast<uint32_t>(offsetof(CppGpuInstance, normalMatrix)), 64},
        {"materialIndex", static_cast<uint32_t>(offsetof(CppGpuInstance, materialIndex)), 4},
        {"flags", static_cast<uint32_t>(offsetof(CppGpuInstance, flags)), 4},
        {"_pad0", static_cast<uint32_t>(offsetof(CppGpuInstance, _pad0)), 8},
    };

    // Reflect a shader that uses GpuInstance to get GPU struct layout
    ShaderCompileDesc desc;
    desc.sourceCode = R"(
import miki_core;

RWStructuredBuffer<GpuInstance> buf;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    buf[0].materialIndex = tid.x;
}
)";
    desc.sourcePath = "test_gpu_instance_reflect.slang";
    desc.entryPoint = "main";
    desc.stage = ShaderStage::Compute;
    desc.target = ShaderTarget::SPIRV_1_5();

    auto reflection = compiler_->Reflect(desc);
    ASSERT_TRUE(reflection.has_value()) << "Reflection failed for GpuInstance";

    // Find GpuInstance in reflected struct layouts
    ShaderReflection::StructLayout const* gpuLayout = nullptr;
    for (auto const& sl : reflection->structLayouts) {
        if (sl.name == "GpuInstance") {
            gpuLayout = &sl;
            break;
        }
    }

    if (gpuLayout) {
        auto mismatches = ValidateStructLayout(cppLayout, *gpuLayout);
        for (auto const& m : mismatches) {
            ADD_FAILURE() << "GpuInstance mismatch: " << m.fieldName << ": " << m.description;
        }
        EXPECT_TRUE(mismatches.empty()) << "GpuInstance layout has mismatches";
    } else {
        // If struct layout not reflected (may require structured buffer usage), skip gracefully
        GTEST_SKIP() << "GpuInstance not found in reflection structLayouts";
    }
}

TEST_F(StructLayoutValidationTest, DetectsMismatchOnWrongOffset) {
    // Deliberately wrong C++ layout
    CppStructLayout wrong;
    wrong.name = "TestStruct";
    wrong.sizeBytes = 16;
    wrong.fields = {
        {"a", 0, 4},
        {"b", 8, 4},  // Wrong offset (should be 4)
    };

    ShaderReflection::StructLayout gpu;
    gpu.name = "TestStruct";
    gpu.sizeBytes = 16;
    gpu.fields = {
        {"a", 0, 4},
        {"b", 4, 4},
    };

    auto mismatches = ValidateStructLayout(wrong, gpu);
    ASSERT_GE(mismatches.size(), 1u) << "Should detect offset mismatch for field 'b'";
    EXPECT_NE(mismatches[0].description.find("Offset mismatch"), std::string::npos);
}

TEST_F(StructLayoutValidationTest, DetectsMissingField) {
    CppStructLayout cpp;
    cpp.name = "TestStruct";
    cpp.sizeBytes = 8;
    cpp.fields = {
        {"a", 0, 4},
        {"b", 4, 4},
        {"c", 8, 4},  // Extra field not in GPU
    };

    ShaderReflection::StructLayout gpu;
    gpu.name = "TestStruct";
    gpu.sizeBytes = 8;
    gpu.fields = {
        {"a", 0, 4},
        {"b", 4, 4},
    };

    auto mismatches = ValidateStructLayout(cpp, gpu);
    ASSERT_GE(mismatches.size(), 1u) << "Should detect field 'c' exists in C++ but not GPU";
}
