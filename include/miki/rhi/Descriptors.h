/** @file Descriptors.h
 *  @brief Descriptor layout, descriptor set, pipeline layout, bindless table.
 *  Namespace: miki::rhi
 */
#pragma once

#include <cstdint>
#include <span>
#include <variant>

#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"

namespace miki::rhi {

    // =========================================================================
    // Binding type
    // =========================================================================

    enum class BindingType : uint8_t {
        UniformBuffer,
        StorageBuffer,
        SampledTexture,
        StorageTexture,
        Sampler,
        CombinedTextureSampler,
        AccelerationStructure,
        BindlessTextures,  ///< T1 only
        BindlessBuffers,   ///< T1 only
    };

    // =========================================================================
    // Binding description
    // =========================================================================

    struct BindingDesc {
        uint32_t binding = 0;
        BindingType type = BindingType::UniformBuffer;
        ShaderStage stages = ShaderStage::All;
        uint32_t count = 1;  ///< Array size, 0 = runtime-sized (bindless)
    };

    // =========================================================================
    // Descriptor layout
    // =========================================================================

    struct DescriptorLayoutDesc {
        std::span<const BindingDesc> bindings;
        bool pushDescriptor = false;  ///< Vulkan push descriptor optimization
    };

    // =========================================================================
    // Pipeline layout
    // =========================================================================

    struct PushConstantRange {
        ShaderStage stages = ShaderStage::All;
        uint32_t offset = 0;
        uint32_t size = 0;
    };

    struct PipelineLayoutDesc {
        std::span<const DescriptorLayoutHandle> setLayouts;  ///< max 4 sets
        std::span<const PushConstantRange> pushConstants;    ///< max 256B total
    };

    // =========================================================================
    // Descriptor writes
    // =========================================================================

    struct BufferBinding {
        BufferHandle buffer;
        uint64_t offset = 0;
        uint64_t range = 0;  ///< 0 = whole buffer
    };

    struct TextureBinding {
        TextureViewHandle view;
        SamplerHandle sampler;  ///< Used only for CombinedTextureSampler
    };

    using DescriptorResource = std::variant<BufferBinding, TextureBinding, SamplerHandle, AccelStructHandle>;

    struct DescriptorWrite {
        uint32_t binding = 0;
        uint32_t arrayElement = 0;
        DescriptorResource resource;
    };

    struct DescriptorSetDesc {
        DescriptorLayoutHandle layout;
        std::span<const DescriptorWrite> writes;
    };

    // =========================================================================
    // Bindless table (§6.6)
    // =========================================================================

    struct NativeBindingInfo {
        void* nativeHandle = nullptr;  ///< Backend-specific (descriptor heap / set / bind group)
        BackendType backend = BackendType::Mock;
    };

}  // namespace miki::rhi
