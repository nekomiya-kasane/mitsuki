/** @file Shader.h
 *  @brief Shader module creation from pre-compiled blobs.
 *  RHI accepts SPIR-V, DXIL, GLSL text, WGSL text — compilation is external (Slang).
 *  Namespace: miki::rhi
 */
#pragma once

#include <cstdint>
#include <span>

#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiEnums.h"

namespace miki::rhi {

    struct ShaderModuleDesc {
        ShaderStage              stage      = ShaderStage::Vertex;
        std::span<const uint8_t> code;          ///< SPIR-V, DXIL, GLSL text, or WGSL text
        const char*              entryPoint = "main";
        const char*              debugName  = nullptr;
    };

    struct SpecializationConstant {
        uint32_t constantId = 0;
        uint32_t size       = 0;    ///< sizeof the constant (4 for int/float, 8 for int64/double)
        const void* data    = nullptr;
    };

    struct RayTracingShaderGroup {
        RayTracingShaderGroupType type             = RayTracingShaderGroupType::General;
        uint32_t                  generalShader     = ~0u;  ///< Index into shader stages (or ~0 = unused)
        uint32_t                  closestHitShader  = ~0u;
        uint32_t                  anyHitShader      = ~0u;
        uint32_t                  intersectionShader = ~0u;
    };

}  // namespace miki::rhi
