/** @brief Shader data types for the miki shader compilation system.
 *
 * Defines ShaderBlob, ShaderReflection, ShaderTarget, ShaderStage,
 * ShaderPermutationKey, ShaderCompileDesc, and related types.
 */
#pragma once

#include "miki/rhi/Format.h"
#include "miki/rhi/RhiTypes.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace miki::shader {

    /** @brief Compilation target backend. */
    enum class ShaderTarget : uint8_t {
        SPIRV,  // Vulkan (Tier1/Tier2), OpenGL (via GL_ARB_gl_spirv)
        DXIL,   // D3D12
        GLSL,   // Reserved -- not used at runtime (GL consumes SPIR-V)
        WGSL,   // WebGPU (Dawn)
    };

    /** @brief Map RHI backend type to shader compilation target.
     *
     * Canonical mapping used by all render passes. GL uses SPIR-V
     * (consumed via GL_ARB_gl_spirv) to avoid Slang GLSL codegen issues.
     * Call sites should NOT duplicate this logic.
     */
    [[nodiscard]] constexpr auto ShaderTargetForBackend(rhi::BackendType iBackend) noexcept -> ShaderTarget {
        switch (iBackend) {
            case rhi::BackendType::D3D12: return ShaderTarget::DXIL;
            case rhi::BackendType::WebGPU: return ShaderTarget::WGSL;
            case rhi::BackendType::OpenGL43: return ShaderTarget::SPIRV;  // GL_ARB_gl_spirv
            default: return ShaderTarget::SPIRV;                          // Vulkan14, VulkanCompat, Mock
        }
    }

    /** @brief Shader pipeline stage. */
    enum class ShaderStage : uint8_t {
        Vertex,
        Fragment,
        Compute,
        Mesh,
        Amplification,
        RayGen,
        ClosestHit,
        Miss,
        AnyHit,
        Intersection,
    };

    /** @brief Descriptor binding type. */
    enum class BindingType : uint8_t {
        UniformBuffer,
        StorageBuffer,
        SampledTexture,
        StorageTexture,
        Sampler,
        CombinedImageSampler,
    };

    /** @brief Single descriptor binding info from reflection. */
    struct BindingInfo {
        uint32_t set = 0;
        uint32_t binding = 0;
        BindingType type = BindingType::UniformBuffer;
        uint32_t count = 1;
        std::string name;

        struct UserAttrib {
            std::string name;
            std::vector<std::string> args;
        };
        std::vector<UserAttrib> userAttribs;
    };

    /** @brief Entry point info from reflection. */
    struct EntryPointInfo {
        std::string name;
        ShaderStage stage = ShaderStage::Vertex;
        std::vector<BindingInfo::UserAttrib> userAttribs;
    };

    /** @brief Vertex input attribute info from reflection. */
    struct VertexInputInfo {
        uint32_t location = 0;
        rhi::Format format = rhi::Format::Undefined;
        uint32_t offset = 0;
        std::string name;
    };

    /** @brief Shader reflection data extracted after compilation. */
    struct ShaderReflection {
        /** @brief Integer global/module constant (e.g. static const uint). */
        struct ModuleConstant {
            std::string name;
            bool hasIntValue = false;
            int64_t intValue = 0;
        };

        /** @brief One field of a reflected struct type (SSBO / structured buffer layout). */
        struct StructField {
            std::string name;
            uint32_t offsetBytes = 0;
            uint32_t sizeBytes = 0;
        };

        /** @brief Struct layout from Slang (for matching C++ and GPU structs). */
        struct StructLayout {
            std::string name;
            uint32_t sizeBytes = 0;
            uint32_t alignment = 0;
            std::vector<StructField> fields;
        };

        std::vector<EntryPointInfo> entryPoints;
        std::vector<BindingInfo> bindings;
        uint32_t pushConstantSize = 0;
        std::vector<VertexInputInfo> vertexInputs;
        uint32_t threadGroupSize[3] = {0, 0, 0};

        /** Module-level static const scalars with integer initializers (always filled by Reflect()). */
        std::vector<ModuleConstant> moduleConstants;
        /** Struct types declared in the module (AST walk); layouts from linked program. */
        std::vector<StructLayout> structLayouts;
    };

    /** @brief Compiled shader bytecode blob. Move-only. */
    struct ShaderBlob {
        std::vector<uint8_t> data;
        ShaderTarget target = ShaderTarget::SPIRV;
        ShaderStage stage = ShaderStage::Vertex;
        std::string entryPoint;

        ShaderBlob() = default;
        ShaderBlob(ShaderBlob&&) noexcept = default;
        auto operator=(ShaderBlob&&) noexcept -> ShaderBlob& = default;
        ShaderBlob(ShaderBlob const&) = delete;
        auto operator=(ShaderBlob const&) -> ShaderBlob& = delete;
    };

    /** @brief 64-bit bitfield for shader permutation variants. */
    struct ShaderPermutationKey {
        uint64_t bits = 0;

        constexpr auto operator==(ShaderPermutationKey const&) const noexcept -> bool = default;
        constexpr auto operator!=(ShaderPermutationKey const&) const noexcept -> bool = default;

        constexpr void SetBit(uint32_t iBit, bool iValue) noexcept {
            if (iValue) {
                bits |= (uint64_t{1} << iBit);
            } else {
                bits &= ~(uint64_t{1} << iBit);
            }
        }

        [[nodiscard]] constexpr auto GetBit(uint32_t iBit) const noexcept -> bool {
            return (bits & (uint64_t{1} << iBit)) != 0;
        }
    };

    /** @brief Descriptor for a shader compilation request. */
    struct ShaderCompileDesc {
        std::filesystem::path sourcePath;  ///< Path to .slang file (used for file I/O and diagnostics)
        std::string sourceCode;            ///< If non-empty, compile from this string instead of reading sourcePath
        std::string entryPoint;
        ShaderStage stage = ShaderStage::Vertex;
        ShaderTarget target = ShaderTarget::SPIRV;
        ShaderPermutationKey permutation;
        std::vector<std::pair<std::string, std::string>> defines;
    };

    /** @brief Configuration for the PermutationCache. */
    struct PermutationCacheConfig {
        bool enableDiskCache = false;
        std::filesystem::path cacheDir;
        uint32_t maxEntries = 1024;
    };

}  // namespace miki::shader
