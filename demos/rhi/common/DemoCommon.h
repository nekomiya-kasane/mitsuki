/** @file DemoCommon.h
 *  @brief Shared utilities for RHI demos: CLI parsing, backend includes, shader compilation.
 *
 *  Header-only. All demos should include this instead of duplicating boilerplate.
 */
#pragma once

#include "miki/platform/WindowManager.h"
#include "miki/platform/glfw/GlfwWindowBackend.h"
#include "miki/rhi/backend/AllBackends.h"
#include "miki/debug/StructuredLogger.h"
#if MIKI_BUILD_VULKAN
#    include "miki/rhi/backend/VulkanCommandBuffer.h"
#endif
#if MIKI_BUILD_D3D12
#    include "miki/rhi/backend/D3D12CommandBuffer.h"
#endif
#if MIKI_BUILD_OPENGL
#    include "miki/rhi/backend/OpenGLCommandBuffer.h"
#endif
#if MIKI_BUILD_WEBGPU
#    include "miki/rhi/backend/WebGPUCommandBuffer.h"
#endif
#include "miki/rhi/backend/MockCommandBuffer.h"
#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Descriptors.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/DeviceFactory.h"
#include "miki/rhi/GpuCapabilityProfile.h"
#include "miki/rhi/Pipeline.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"
#include "miki/rhi/Resources.h"
#include "miki/rhi/Shader.h"
#include "miki/rhi/SurfaceManager.h"
#include "miki/shader/ShaderTypes.h"
#include "miki/shader/SlangCompiler.h"
#include "miki/core/MathUtils.h"

#ifdef CreateWindow
#    undef CreateWindow
#endif
#ifdef CreateSemaphore
#    undef CreateSemaphore
#endif

#if defined(__EMSCRIPTEN__)
#    include <emscripten/emscripten.h>
#endif

#include <optional>
#include <print>
#include <string_view>

namespace miki::demo {

using namespace miki::platform;
using namespace miki::rhi;
namespace shader = miki::shader;

// ============================================================================
// CLI parsing
// ============================================================================

[[nodiscard]] inline auto ParseBackend(std::string_view s) -> BackendType {
    if (s == "vulkan" || s == "vk") return BackendType::Vulkan14;
    if (s == "vulkan11" || s == "vk11") return BackendType::VulkanCompat;
    if (s == "d3d12" || s == "dx12") return BackendType::D3D12;
    if (s == "opengl" || s == "gl") return BackendType::OpenGL43;
    if (s == "webgpu" || s == "wgpu") return BackendType::WebGPU;
    return BackendType::VulkanCompat;
}

[[nodiscard]] inline auto BackendName(BackendType t) -> const char* {
    switch (t) {
        case BackendType::Vulkan14: return "Vulkan";
        case BackendType::VulkanCompat: return "VulkanCompat";
        case BackendType::D3D12: return "D3D12";
        case BackendType::OpenGL43: return "OpenGL";
        case BackendType::WebGPU: return "WebGPU";
        default: return "Unknown";
    }
}

[[nodiscard]] inline auto ParseBackendFromArgs(int argc, char** argv) -> BackendType {
#if defined(__EMSCRIPTEN__)
    (void)argc; (void)argv;
    return BackendType::WebGPU;
#else
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--backend" && i + 1 < argc) {
            return ParseBackend(argv[++i]);
        }
    }
    return BackendType::VulkanCompat;
#endif
}

// ============================================================================
// Shader compilation helpers
// ============================================================================

struct CompiledShaderPair {
    shader::ShaderBlob vs;
    shader::ShaderBlob fs;
};

[[nodiscard]] inline auto CompileStage(
    shader::SlangCompiler& compiler, const std::string& src, const char* entry,
    shader::ShaderStage stage, shader::ShaderTarget target, const char* label
) -> std::optional<shader::ShaderBlob> {
    shader::ShaderCompileDesc desc{
        .sourcePath = {}, .sourceCode = src, .entryPoint = entry, .stage = stage, .target = target
    };
    auto result = compiler.Compile(desc);
    if (!result) {
        std::println("[demo] {} compilation failed", label);
        for (auto& d : compiler.GetLastDiagnostics()) {
            std::println("  {}:{}:{}: {}", d.filePath, d.line, d.column, d.message);
        }
        return std::nullopt;
    }
    return std::move(*result);
}

[[nodiscard]] inline auto CompileShaderPair(
    const char* src, BackendType backend, const char* label
) -> std::optional<CompiledShaderPair> {
    auto compilerResult = shader::SlangCompiler::Create();
    if (!compilerResult) {
        std::println("[demo] SlangCompiler::Create failed for {}", label);
        return std::nullopt;
    }
    auto compiler = std::move(*compilerResult);
    auto target = shader::ShaderTargetForBackend(backend);
    std::string source(src);

    auto vs = CompileStage(compiler, source, "vs_main", shader::ShaderStage::Vertex, target, label);
    if (!vs) return std::nullopt;
    auto fs = CompileStage(compiler, source, "fs_main", shader::ShaderStage::Fragment, target, label);
    if (!fs) return std::nullopt;
    return CompiledShaderPair{.vs = std::move(*vs), .fs = std::move(*fs)};
}

}  // namespace miki::demo
