/** @file Resources.h
 *  @brief GPU resource descriptors: Buffer, Texture, TextureView, Sampler,
 *         MemoryHeap, Sparse binding, Transient aliasing.
 *  Namespace: miki::rhi
 */
#pragma once

#include <cstdint>
#include <span>

#include "miki/rhi/Format.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"

namespace miki::rhi {

    // =========================================================================
    // Buffer
    // =========================================================================

    struct BufferDesc {
        uint64_t size = 0;
        BufferUsage usage = BufferUsage::Vertex;
        MemoryLocation memory = MemoryLocation::GpuOnly;
        bool transient = false;  ///< Eligible for RenderGraph memory aliasing
        const char* debugName = nullptr;
    };

    // =========================================================================
    // Texture
    // =========================================================================

    struct TextureDesc {
        TextureDimension dimension = TextureDimension::Tex2D;
        Format format = Format::RGBA8_UNORM;
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        uint32_t sampleCount = 1;  ///< MSAA: 1, 2, 4, 8
        TextureUsage usage = TextureUsage::Sampled;
        MemoryLocation memory = MemoryLocation::GpuOnly;
        bool transient = false;  ///< Eligible for RenderGraph memory aliasing
        const char* debugName = nullptr;
    };

    // =========================================================================
    // Texture subresource range
    // =========================================================================

    struct TextureSubresourceRange {
        TextureAspect aspect = TextureAspect::Color;
        uint32_t baseMipLevel = 0;
        uint32_t mipLevelCount = 0;  ///< 0 = all remaining mip levels
        uint32_t baseArrayLayer = 0;
        uint32_t arrayLayerCount = 0;  ///< 0 = all remaining array layers
    };

    // =========================================================================
    // Texture view
    // =========================================================================

    struct TextureViewDesc {
        TextureHandle texture = {};
        TextureDimension viewDimension = TextureDimension::Tex2D;
        Format format = Format::Undefined;  ///< Inherit from texture if Undefined
        uint32_t baseMipLevel = 0;
        uint32_t mipLevelCount = 0;  ///< 0 = all remaining mip levels (VK_REMAINING_MIP_LEVELS)
        uint32_t baseArrayLayer = 0;
        uint32_t arrayLayerCount = 0;  ///< 0 = all remaining array layers (VK_REMAINING_ARRAY_LAYERS)
        TextureAspect aspect = TextureAspect::Color;
    };

    // =========================================================================
    // Sampler
    // =========================================================================

    struct SamplerDesc {
        Filter magFilter = Filter::Linear;
        Filter minFilter = Filter::Linear;
        Filter mipFilter = Filter::Linear;
        AddressMode addressU = AddressMode::Repeat;
        AddressMode addressV = AddressMode::Repeat;
        AddressMode addressW = AddressMode::Repeat;
        float mipLodBias = 0.0f;
        float maxAnisotropy = 0.0f;  ///< 0 = disabled, 1-16
        CompareOp compareOp = CompareOp::None;
        float minLod = 0.0f;
        float maxLod = 1000.0f;
        BorderColor borderColor = BorderColor::TransparentBlack;
    };

    // =========================================================================
    // Memory requirements (for aliasing queries)
    // =========================================================================

    struct MemoryRequirements {
        uint64_t size = 0;
        uint64_t alignment = 0;
        uint32_t memoryTypeBits = 0;  ///< Backend-specific type mask
    };

    // =========================================================================
    // Memory heap (for transient aliasing)
    // =========================================================================

    struct MemoryHeapDesc {
        uint64_t size = 0;
        MemoryLocation memory = MemoryLocation::GpuOnly;
        const char* debugName = nullptr;
    };

    // =========================================================================
    // Copy regions
    // =========================================================================

    struct BufferTextureCopyRegion {
        uint64_t bufferOffset = 0;
        uint32_t bufferRowLength = 0;    ///< 0 = tightly packed
        uint32_t bufferImageHeight = 0;  ///< 0 = tightly packed
        TextureSubresourceRange subresource;
        Offset3D textureOffset;
        Extent3D textureExtent;
    };

    struct TextureCopyRegion {
        TextureSubresourceRange srcSubresource;
        Offset3D srcOffset;
        TextureSubresourceRange dstSubresource;
        Offset3D dstOffset;
        Extent3D extent;
    };

    struct TextureBlitRegion {
        TextureSubresourceRange srcSubresource;
        Offset3D srcOffsetMin;
        Offset3D srcOffsetMax;
        TextureSubresourceRange dstSubresource;
        Offset3D dstOffsetMin;
        Offset3D dstOffsetMax;
    };

    // =========================================================================
    // Sparse binding (T1 only)
    // =========================================================================

    struct SparsePageSize {
        uint64_t bufferPageSize = 0;  ///< Typical: 64KB
        uint64_t imagePageSize = 0;   ///< Typical: 64KB or 2MB
    };

    struct SparseBufferBind {
        BufferHandle buffer;
        uint64_t resourceOffset = 0;  ///< Must be page-aligned
        uint64_t size = 0;            ///< Must be page-aligned
        DeviceMemoryHandle memory;    ///< Null = unbind (evict)
        uint64_t memoryOffset = 0;
    };

    struct SparseTextureBind {
        TextureHandle texture;
        TextureSubresourceRange subresource;
        Offset3D offset;            ///< Texel offset (page-aligned)
        Extent3D extent;            ///< Texel extent (page-aligned)
        DeviceMemoryHandle memory;  ///< Null = unbind
        uint64_t memoryOffset = 0;
    };

    struct SparseBindDesc {
        std::span<const SparseBufferBind> bufferBinds;
        std::span<const SparseTextureBind> textureBinds;
    };

    // =========================================================================
    // Memory statistics (debug/profiling)
    // =========================================================================

    struct MemoryHeapBudget {
        uint64_t budgetBytes = 0;
        uint64_t usageBytes = 0;
        uint32_t heapIndex = 0;
        bool isDeviceLocal = false;
    };

    struct MemoryStats {
        uint32_t totalAllocationCount = 0;
        uint64_t totalAllocatedBytes = 0;
        uint64_t totalUsedBytes = 0;
        uint32_t heapCount = 0;
    };

}  // namespace miki::rhi
