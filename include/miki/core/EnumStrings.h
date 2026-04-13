/** @file EnumStrings.h
 *  @brief Centralized ToString / FromString for all enums used in serialization.
 *
 *  Covers: rhi::BackendType, rhi::TextureLayout, rhi::TextureDimension, rhi::Format,
 *          rhi::adaptation::{Feature, Strategy},
 *          rg::{RGQueueType, RGPassFlags, ResourceAccess, RGResourceKind, HazardType,
 *               HeapGroupType, SchedulerStrategy}
 *
 *  Namespace: miki::rhi, miki::rg
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "miki/rhi/Format.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"
#include "miki/rhi/adaptation/AdaptationTypes.h"
#include "miki/rendergraph/RenderGraphTypes.h"

namespace miki::rhi {

    // =========================================================================
    // rhi::QueueType
    // =========================================================================

    constexpr auto ToString(QueueType v) -> const char* {
        switch (v) {
            case QueueType::Graphics: return "Graphics";
            case QueueType::Compute: return "Compute";
            case QueueType::AsyncCompute: return "AsyncCompute";
            case QueueType::Transfer: return "Transfer";
            case QueueType::Count_: return "Count_";
        }
        return "Unknown";
    }

    // =========================================================================
    // rhi::BackendType
    // =========================================================================

    constexpr auto ToString(BackendType v) -> const char* {
        switch (v) {
            case BackendType::Vulkan14: return "Vulkan14";
            case BackendType::D3D12: return "D3D12";
            case BackendType::VulkanCompat: return "VulkanCompat";
            case BackendType::WebGPU: return "WebGPU";
            case BackendType::OpenGL43: return "OpenGL43";
            case BackendType::Mock: return "Mock";
        }
        return "Unknown";
    }

    constexpr auto BackendTypeFromString(std::string_view str) -> std::optional<BackendType> {
        if (str == "Vulkan14") {
            return BackendType::Vulkan14;
        }
        if (str == "D3D12") {
            return BackendType::D3D12;
        }
        if (str == "VulkanCompat") {
            return BackendType::VulkanCompat;
        }
        if (str == "WebGPU") {
            return BackendType::WebGPU;
        }
        if (str == "OpenGL43") {
            return BackendType::OpenGL43;
        }
        if (str == "Mock") {
            return BackendType::Mock;
        }
        return std::nullopt;
    }

    // =========================================================================
    // rhi::TextureLayout
    // =========================================================================

    constexpr auto ToString(TextureLayout v) -> const char* {
        switch (v) {
            case TextureLayout::Undefined: return "Undefined";
            case TextureLayout::General: return "General";
            case TextureLayout::ColorAttachment: return "ColorAttachment";
            case TextureLayout::DepthStencilAttachment: return "DepthStencilAttachment";
            case TextureLayout::DepthStencilReadOnly: return "DepthStencilReadOnly";
            case TextureLayout::ShaderReadOnly: return "ShaderReadOnly";
            case TextureLayout::TransferSrc: return "TransferSrc";
            case TextureLayout::TransferDst: return "TransferDst";
            case TextureLayout::Present: return "Present";
            case TextureLayout::ShadingRate: return "ShadingRate";
        }
        return "Unknown";
    }

    constexpr auto TextureLayoutFromString(std::string_view str) -> std::optional<TextureLayout> {
        if (str == "Undefined") {
            return TextureLayout::Undefined;
        }
        if (str == "General") {
            return TextureLayout::General;
        }
        if (str == "ColorAttachment") {
            return TextureLayout::ColorAttachment;
        }
        if (str == "DepthStencilAttachment") {
            return TextureLayout::DepthStencilAttachment;
        }
        if (str == "DepthStencilReadOnly") {
            return TextureLayout::DepthStencilReadOnly;
        }
        if (str == "ShaderReadOnly") {
            return TextureLayout::ShaderReadOnly;
        }
        if (str == "TransferSrc") {
            return TextureLayout::TransferSrc;
        }
        if (str == "TransferDst") {
            return TextureLayout::TransferDst;
        }
        if (str == "Present") {
            return TextureLayout::Present;
        }
        if (str == "ShadingRate") {
            return TextureLayout::ShadingRate;
        }
        return std::nullopt;
    }

    // =========================================================================
    // rhi::TextureDimension
    // =========================================================================

    constexpr auto ToString(TextureDimension v) -> const char* {
        switch (v) {
            case TextureDimension::Tex1D: return "Tex1D";
            case TextureDimension::Tex2D: return "Tex2D";
            case TextureDimension::Tex3D: return "Tex3D";
            case TextureDimension::TexCube: return "TexCube";
            case TextureDimension::Tex2DArray: return "Tex2DArray";
            case TextureDimension::TexCubeArray: return "TexCubeArray";
        }
        return "Unknown";
    }

    constexpr auto TextureDimensionFromString(std::string_view str) -> std::optional<TextureDimension> {
        if (str == "Tex1D") {
            return TextureDimension::Tex1D;
        }
        if (str == "Tex2D") {
            return TextureDimension::Tex2D;
        }
        if (str == "Tex3D") {
            return TextureDimension::Tex3D;
        }
        if (str == "TexCube") {
            return TextureDimension::TexCube;
        }
        if (str == "Tex2DArray") {
            return TextureDimension::Tex2DArray;
        }
        if (str == "TexCubeArray") {
            return TextureDimension::TexCubeArray;
        }
        return std::nullopt;
    }

    // =========================================================================
    // rhi::Format
    // =========================================================================

    constexpr auto ToString(Format v) -> const char* {
        switch (v) {
            case Format::Undefined: return "Undefined";
            case Format::R8_UNORM: return "R8_UNORM";
            case Format::R8_SNORM: return "R8_SNORM";
            case Format::R8_UINT: return "R8_UINT";
            case Format::R8_SINT: return "R8_SINT";
            case Format::RG8_UNORM: return "RG8_UNORM";
            case Format::RG8_SNORM: return "RG8_SNORM";
            case Format::RG8_UINT: return "RG8_UINT";
            case Format::RG8_SINT: return "RG8_SINT";
            case Format::RGBA8_UNORM: return "RGBA8_UNORM";
            case Format::RGBA8_SNORM: return "RGBA8_SNORM";
            case Format::RGBA8_UINT: return "RGBA8_UINT";
            case Format::RGBA8_SINT: return "RGBA8_SINT";
            case Format::RGBA8_SRGB: return "RGBA8_SRGB";
            case Format::BGRA8_UNORM: return "BGRA8_UNORM";
            case Format::BGRA8_SRGB: return "BGRA8_SRGB";
            case Format::R16_UNORM: return "R16_UNORM";
            case Format::R16_SNORM: return "R16_SNORM";
            case Format::R16_UINT: return "R16_UINT";
            case Format::R16_SINT: return "R16_SINT";
            case Format::R16_FLOAT: return "R16_FLOAT";
            case Format::RG16_UNORM: return "RG16_UNORM";
            case Format::RG16_SNORM: return "RG16_SNORM";
            case Format::RG16_UINT: return "RG16_UINT";
            case Format::RG16_SINT: return "RG16_SINT";
            case Format::RG16_FLOAT: return "RG16_FLOAT";
            case Format::RGBA16_UNORM: return "RGBA16_UNORM";
            case Format::RGBA16_SNORM: return "RGBA16_SNORM";
            case Format::RGBA16_UINT: return "RGBA16_UINT";
            case Format::RGBA16_SINT: return "RGBA16_SINT";
            case Format::RGBA16_FLOAT: return "RGBA16_FLOAT";
            case Format::R32_UINT: return "R32_UINT";
            case Format::R32_SINT: return "R32_SINT";
            case Format::R32_FLOAT: return "R32_FLOAT";
            case Format::RG32_UINT: return "RG32_UINT";
            case Format::RG32_SINT: return "RG32_SINT";
            case Format::RG32_FLOAT: return "RG32_FLOAT";
            case Format::RGB32_UINT: return "RGB32_UINT";
            case Format::RGB32_SINT: return "RGB32_SINT";
            case Format::RGB32_FLOAT: return "RGB32_FLOAT";
            case Format::RGBA32_UINT: return "RGBA32_UINT";
            case Format::RGBA32_SINT: return "RGBA32_SINT";
            case Format::RGBA32_FLOAT: return "RGBA32_FLOAT";
            case Format::RGB10A2_UNORM: return "RGB10A2_UNORM";
            case Format::RG11B10_FLOAT: return "RG11B10_FLOAT";
            case Format::D16_UNORM: return "D16_UNORM";
            case Format::D32_FLOAT: return "D32_FLOAT";
            case Format::D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
            case Format::D32_FLOAT_S8_UINT: return "D32_FLOAT_S8_UINT";
            case Format::BC1_UNORM: return "BC1_UNORM";
            case Format::BC1_SRGB: return "BC1_SRGB";
            case Format::BC2_UNORM: return "BC2_UNORM";
            case Format::BC2_SRGB: return "BC2_SRGB";
            case Format::BC3_UNORM: return "BC3_UNORM";
            case Format::BC3_SRGB: return "BC3_SRGB";
            case Format::BC4_UNORM: return "BC4_UNORM";
            case Format::BC4_SNORM: return "BC4_SNORM";
            case Format::BC5_UNORM: return "BC5_UNORM";
            case Format::BC5_SNORM: return "BC5_SNORM";
            case Format::BC6H_UFLOAT: return "BC6H_UFLOAT";
            case Format::BC6H_SFLOAT: return "BC6H_SFLOAT";
            case Format::BC7_UNORM: return "BC7_UNORM";
            case Format::BC7_SRGB: return "BC7_SRGB";
            case Format::ASTC_4x4_UNORM: return "ASTC_4x4_UNORM";
            case Format::ASTC_4x4_SRGB: return "ASTC_4x4_SRGB";
            default: return "Unknown";
        }
    }

    // =========================================================================
    // rhi::adaptation
    // =========================================================================
    namespace adaptation {

        constexpr auto ToString(Feature v) -> const char* {
            switch (v) {
                case Feature::BufferMapWriteWithUsage: return "BufferMapWriteWithUsage";
                case Feature::BufferPersistentMapping: return "BufferPersistentMapping";
                case Feature::PushConstants: return "PushConstants";
                case Feature::CmdBlitTexture: return "CmdBlitTexture";
                case Feature::CmdFillBufferNonZero: return "CmdFillBufferNonZero";
                case Feature::CmdClearTexture: return "CmdClearTexture";
                case Feature::MultiDrawIndirect: return "MultiDrawIndirect";
                case Feature::DepthOnlyStencilOps: return "DepthOnlyStencilOps";
                case Feature::TimelineSemaphore: return "TimelineSemaphore";
                case Feature::SparseBinding: return "SparseBinding";
                case Feature::DynamicDepthBias: return "DynamicDepthBias";
                case Feature::ExecuteSecondary: return "ExecuteSecondary";
                case Feature::MeshShader: return "MeshShader";
                case Feature::RayTracing: return "RayTracing";
                default: return "Unknown";
            }
        }

        constexpr auto ToString(Strategy v) -> const char* {
            switch (v) {
                case Strategy::Native: return "Native";
                case Strategy::ParameterFixup: return "ParameterFixup";
                case Strategy::UboEmulation: return "UboEmulation";
                case Strategy::EphemeralMap: return "EphemeralMap";
                case Strategy::CallbackEmulation: return "CallbackEmulation";
                case Strategy::ShadowBuffer: return "ShadowBuffer";
                case Strategy::StagingCopy: return "StagingCopy";
                case Strategy::LoopUnroll: return "LoopUnroll";
                case Strategy::ShaderEmulation: return "ShaderEmulation";
                case Strategy::Unsupported: return "Unsupported";
            }
            return "Unknown";
        }

    }  // namespace adaptation

}  // namespace miki::rhi

namespace miki::rg {

    // =========================================================================
    // rg:: enums
    // =========================================================================

    constexpr auto ToString(RGQueueType v) -> const char* {
        switch (v) {
            case RGQueueType::Graphics: return "Graphics";
            case RGQueueType::AsyncCompute: return "AsyncCompute";
            case RGQueueType::Transfer: return "Transfer";
        }
        return "Unknown";
    }

    constexpr auto QueueTypeFromString(std::string_view str) -> std::optional<RGQueueType> {
        if (str == "Graphics") {
            return RGQueueType::Graphics;
        }
        if (str == "AsyncCompute") {
            return RGQueueType::AsyncCompute;
        }
        if (str == "Transfer") {
            return RGQueueType::Transfer;
        }
        return std::nullopt;
    }

    constexpr auto ToString(HazardType v) -> const char* {
        switch (v) {
            case HazardType::RAW: return "RAW";
            case HazardType::WAR: return "WAR";
            case HazardType::WAW: return "WAW";
        }
        return "Unknown";
    }

    constexpr auto ToString(HeapGroupType v) -> const char* {
        switch (v) {
            case HeapGroupType::RtDs: return "RtDs";
            case HeapGroupType::NonRtDs: return "NonRtDs";
            case HeapGroupType::Buffer: return "Buffer";
            case HeapGroupType::MixedFallback: return "MixedFallback";
        }
        return "Unknown";
    }

    constexpr auto ToString(RGResourceKind v) -> const char* {
        switch (v) {
            case RGResourceKind::Texture: return "Texture";
            case RGResourceKind::Buffer: return "Buffer";
            case RGResourceKind::AccelerationStructure: return "AccelerationStructure";
            case RGResourceKind::SparseTexture: return "SparseTexture";
            case RGResourceKind::SparseBuffer: return "SparseBuffer";
        }
        return "Unknown";
    }

    constexpr auto ToString(SchedulerStrategy v) -> const char* {
        switch (v) {
            case SchedulerStrategy::MinBarriers: return "MinBarriers";
            case SchedulerStrategy::MinMemory: return "MinMemory";
            case SchedulerStrategy::MinLatency: return "MinLatency";
            case SchedulerStrategy::Balanced: return "Balanced";
        }
        return "Unknown";
    }

    /// @brief Decompose a ResourceAccess bitmask into individual flag name strings.
    constexpr auto ResourceAccessToStrings(ResourceAccess access, const char** out, size_t capacity) -> size_t {
        struct Entry {
            uint32_t bit;
            const char* name;
        };
        constexpr std::array kEntries = {
            Entry{1u << 0, "ShaderReadOnly"},     Entry{1u << 1, "DepthReadOnly"},
            Entry{1u << 2, "IndirectBuffer"},     Entry{1u << 3, "TransferSrc"},
            Entry{1u << 4, "InputAttachment"},    Entry{1u << 5, "AccelStructRead"},
            Entry{1u << 6, "ShadingRateRead"},    Entry{1u << 7, "PresentSrc"},
            Entry{1u << 8, "ShaderWrite"},        Entry{1u << 9, "ColorAttachWrite"},
            Entry{1u << 10, "DepthStencilWrite"}, Entry{1u << 11, "TransferDst"},
            Entry{1u << 12, "AccelStructWrite"},
        };
        size_t count = 0;
        auto bits = static_cast<uint32_t>(access);
        for (auto& [bit, name] : kEntries) {
            if ((bits & bit) && count < capacity) {
                out[count++] = name;
            }
        }
        return count;
    }

    /// @brief Decompose RGPassFlags bitmask into flag name strings.
    constexpr auto PassFlagsToStrings(RGPassFlags flags, const char** out, size_t capacity) -> size_t {
        struct Entry {
            uint16_t bit;
            const char* name;
        };
        constexpr std::array kEntries = {
            Entry{.bit = 1 << 0, .name = "Graphics"},      Entry{.bit = 1 << 1, .name = "Compute"},
            Entry{.bit = 1 << 2, .name = "AsyncEligible"}, Entry{.bit = 1 << 3, .name = "TransferOnly"},
            Entry{.bit = 1 << 4, .name = "Present"},       Entry{.bit = 1 << 5, .name = "SideEffects"},
            Entry{.bit = 1 << 6, .name = "NeverCull"},     Entry{.bit = 1 << 7, .name = "MeshShader"},
            Entry{.bit = 1 << 8, .name = "SparseBind"},
        };
        size_t count = 0;
        auto bits = static_cast<uint16_t>(flags);
        for (auto& [bit, name] : kEntries) {
            if ((bits & bit) && count < capacity) {
                out[count++] = name;
            }
        }
        return count;
    }

}  // namespace miki::rg
