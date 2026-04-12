/** @file VulkanResources.cpp
 *  @brief Vulkan 1.4 backend — Buffer, Texture, TextureView, Sampler,
 *         ShaderModule, Memory aliasing, Sparse binding.
 *
 *  All resource creation uses VMA for memory management.
 *  Resources are stored in typed HandlePool slots for O(1) lookup.
 */

#include "miki/rhi/backend/VulkanDevice.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0

#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wnullability-extension"
#    pragma clang diagnostic ignored "-Wnullability-completeness"
#elif defined(_MSC_VER)
#    pragma warning(push)
#    pragma warning(disable : 5105)
#endif

#include <vk_mem_alloc.h>

#if defined(__clang__)
#    pragma clang diagnostic pop
#elif defined(_MSC_VER)
#    pragma warning(pop)
#endif

#include <cassert>
#include "miki/debug/StructuredLogger.h"

namespace miki::rhi {

    // =========================================================================
    // Buffer memory location detection helper
    // =========================================================================

    namespace {
        struct BufferLocationInfo {
            const char* location;  // "ReBAR VRAM", "System RAM", "Local VRAM"
            bool isDeviceLocal;    // VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            bool isHostVisible;    // VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            bool isHostCoherent;   // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            bool isHostCached;     // VK_MEMORY_PROPERTY_HOST_CACHED_BIT
        };

        auto DetectBufferLocation(VmaAllocator allocator, VmaAllocation allocation) -> BufferLocationInfo {
            VmaAllocationInfo allocInfo{};
            vmaGetAllocationInfo(allocator, allocation, &allocInfo);

            VkMemoryPropertyFlags memFlags{};
            vmaGetMemoryTypeProperties(allocator, allocInfo.memoryType, &memFlags);

            BufferLocationInfo info{};
            info.isDeviceLocal = (memFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
            info.isHostVisible = (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
            info.isHostCoherent = (memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
            info.isHostCached = (memFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0;

            // Determine location based on memory property flags
            if (info.isDeviceLocal && info.isHostVisible) {
                info.location = "ReBAR VRAM";  // CPU-visible VRAM (Resizable BAR)
            } else if (info.isDeviceLocal && !info.isHostVisible) {
                info.location = "Local VRAM";  // GPU-only VRAM
            } else if (!info.isDeviceLocal && info.isHostVisible) {
                info.location = "System RAM";  // CPU RAM (staging)
            } else {
                info.location = "Unknown";
            }

            return info;
        }

        void LogBufferCreation(const BufferDesc& desc, const BufferLocationInfo& locInfo, uint64_t deviceAddress) {
            MIKI_LOG_INFO(
                ::miki::debug::LogCategory::Rhi,
                "[vulkan] Buffer created: name='{}', size={} bytes, location={}, "
                "deviceLocal={}, hostVisible={}, hostCoherent={}, hostCached={}, BDA=0x{:016X}",
                desc.debugName ? desc.debugName : "<unnamed>", desc.size, locInfo.location, locInfo.isDeviceLocal,
                locInfo.isHostVisible, locInfo.isHostCoherent, locInfo.isHostCached, deviceAddress
            );
        }
    }  // namespace

    // =========================================================================
    // Format conversion: miki::rhi::Format -> VkFormat
    // =========================================================================

    namespace {
        auto ToVkFormat(Format fmt) -> VkFormat {
            switch (fmt) {
                case Format::R8_UNORM: return VK_FORMAT_R8_UNORM;
                case Format::R8_SNORM: return VK_FORMAT_R8_SNORM;
                case Format::R8_UINT: return VK_FORMAT_R8_UINT;
                case Format::R8_SINT: return VK_FORMAT_R8_SINT;
                case Format::RG8_UNORM: return VK_FORMAT_R8G8_UNORM;
                case Format::RG8_SNORM: return VK_FORMAT_R8G8_SNORM;
                case Format::RG8_UINT: return VK_FORMAT_R8G8_UINT;
                case Format::RG8_SINT: return VK_FORMAT_R8G8_SINT;
                case Format::RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
                case Format::RGBA8_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;
                case Format::RGBA8_UINT: return VK_FORMAT_R8G8B8A8_UINT;
                case Format::RGBA8_SINT: return VK_FORMAT_R8G8B8A8_SINT;
                case Format::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
                case Format::BGRA8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
                case Format::BGRA8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
                case Format::R16_UNORM: return VK_FORMAT_R16_UNORM;
                case Format::R16_SNORM: return VK_FORMAT_R16_SNORM;
                case Format::R16_UINT: return VK_FORMAT_R16_UINT;
                case Format::R16_SINT: return VK_FORMAT_R16_SINT;
                case Format::R16_FLOAT: return VK_FORMAT_R16_SFLOAT;
                case Format::RG16_UNORM: return VK_FORMAT_R16G16_UNORM;
                case Format::RG16_SNORM: return VK_FORMAT_R16G16_SNORM;
                case Format::RG16_UINT: return VK_FORMAT_R16G16_UINT;
                case Format::RG16_SINT: return VK_FORMAT_R16G16_SINT;
                case Format::RG16_FLOAT: return VK_FORMAT_R16G16_SFLOAT;
                case Format::RGBA16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
                case Format::RGBA16_SNORM: return VK_FORMAT_R16G16B16A16_SNORM;
                case Format::RGBA16_UINT: return VK_FORMAT_R16G16B16A16_UINT;
                case Format::RGBA16_SINT: return VK_FORMAT_R16G16B16A16_SINT;
                case Format::RGBA16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
                case Format::R32_UINT: return VK_FORMAT_R32_UINT;
                case Format::R32_SINT: return VK_FORMAT_R32_SINT;
                case Format::R32_FLOAT: return VK_FORMAT_R32_SFLOAT;
                case Format::RG32_UINT: return VK_FORMAT_R32G32_UINT;
                case Format::RG32_SINT: return VK_FORMAT_R32G32_SINT;
                case Format::RG32_FLOAT: return VK_FORMAT_R32G32_SFLOAT;
                case Format::RGB32_UINT: return VK_FORMAT_R32G32B32_UINT;
                case Format::RGB32_SINT: return VK_FORMAT_R32G32B32_SINT;
                case Format::RGB32_FLOAT: return VK_FORMAT_R32G32B32_SFLOAT;
                case Format::RGBA32_UINT: return VK_FORMAT_R32G32B32A32_UINT;
                case Format::RGBA32_SINT: return VK_FORMAT_R32G32B32A32_SINT;
                case Format::RGBA32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
                case Format::RGB10A2_UNORM: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
                case Format::RG11B10_FLOAT: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
                case Format::D16_UNORM: return VK_FORMAT_D16_UNORM;
                case Format::D32_FLOAT: return VK_FORMAT_D32_SFLOAT;
                case Format::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
                case Format::D32_FLOAT_S8_UINT: return VK_FORMAT_D32_SFLOAT_S8_UINT;
                case Format::BC1_UNORM: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
                case Format::BC1_SRGB: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
                case Format::BC2_UNORM: return VK_FORMAT_BC2_UNORM_BLOCK;
                case Format::BC2_SRGB: return VK_FORMAT_BC2_SRGB_BLOCK;
                case Format::BC3_UNORM: return VK_FORMAT_BC3_UNORM_BLOCK;
                case Format::BC3_SRGB: return VK_FORMAT_BC3_SRGB_BLOCK;
                case Format::BC4_UNORM: return VK_FORMAT_BC4_UNORM_BLOCK;
                case Format::BC4_SNORM: return VK_FORMAT_BC4_SNORM_BLOCK;
                case Format::BC5_UNORM: return VK_FORMAT_BC5_UNORM_BLOCK;
                case Format::BC5_SNORM: return VK_FORMAT_BC5_SNORM_BLOCK;
                case Format::BC6H_UFLOAT: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
                case Format::BC6H_SFLOAT: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
                case Format::BC7_UNORM: return VK_FORMAT_BC7_UNORM_BLOCK;
                case Format::BC7_SRGB: return VK_FORMAT_BC7_SRGB_BLOCK;
                case Format::ASTC_4x4_UNORM: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
                case Format::ASTC_4x4_SRGB: return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
                case Format::ASTC_4x4_HDR: return VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK;
                default: return VK_FORMAT_UNDEFINED;
            }
        }

        auto ToVkBufferUsage(BufferUsage usage) -> VkBufferUsageFlags {
            VkBufferUsageFlags flags = 0;
            auto has
                = [usage](BufferUsage bit) { return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(bit)) != 0; };
            if (has(BufferUsage::Vertex)) {
                flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            }
            if (has(BufferUsage::Index)) {
                flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            }
            if (has(BufferUsage::Uniform)) {
                flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            }
            if (has(BufferUsage::Storage)) {
                flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            }
            if (has(BufferUsage::Indirect)) {
                flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            }
            if (has(BufferUsage::TransferSrc)) {
                flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            }
            if (has(BufferUsage::TransferDst)) {
                flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            }
            if (has(BufferUsage::AccelStructInput)) {
                flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
            }
            if (has(BufferUsage::AccelStructStorage)) {
                flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
            }
            if (has(BufferUsage::ShaderDeviceAddress)) {
                flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            }
            return flags;
        }

        auto ToVmaMemoryUsage(MemoryLocation loc) -> VmaMemoryUsage {
            switch (loc) {
                case MemoryLocation::GpuOnly: return VMA_MEMORY_USAGE_GPU_ONLY;
                case MemoryLocation::CpuToGpu: return VMA_MEMORY_USAGE_CPU_TO_GPU;
                case MemoryLocation::GpuToCpu: return VMA_MEMORY_USAGE_GPU_TO_CPU;
                case MemoryLocation::Auto: return VMA_MEMORY_USAGE_AUTO;
            }
            return VMA_MEMORY_USAGE_AUTO;
        }

        auto ToVkImageType(TextureDimension dim) -> VkImageType {
            switch (dim) {
                case TextureDimension::Tex1D: return VK_IMAGE_TYPE_1D;
                case TextureDimension::Tex2D:
                case TextureDimension::TexCube:
                case TextureDimension::Tex2DArray:
                case TextureDimension::TexCubeArray: return VK_IMAGE_TYPE_2D;
                case TextureDimension::Tex3D: return VK_IMAGE_TYPE_3D;
            }
            return VK_IMAGE_TYPE_2D;
        }

        auto ToVkImageViewType(TextureDimension dim) -> VkImageViewType {
            switch (dim) {
                case TextureDimension::Tex1D: return VK_IMAGE_VIEW_TYPE_1D;
                case TextureDimension::Tex2D: return VK_IMAGE_VIEW_TYPE_2D;
                case TextureDimension::Tex3D: return VK_IMAGE_VIEW_TYPE_3D;
                case TextureDimension::TexCube: return VK_IMAGE_VIEW_TYPE_CUBE;
                case TextureDimension::Tex2DArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
                case TextureDimension::TexCubeArray: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            }
            return VK_IMAGE_VIEW_TYPE_2D;
        }

        auto ToVkImageUsage(TextureUsage usage) -> VkImageUsageFlags {
            VkImageUsageFlags flags = 0;
            auto has = [usage](TextureUsage bit) {
                return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(bit)) != 0;
            };
            if (has(TextureUsage::Sampled)) {
                flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
            }
            if (has(TextureUsage::Storage)) {
                flags |= VK_IMAGE_USAGE_STORAGE_BIT;
            }
            if (has(TextureUsage::ColorAttachment)) {
                flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            }
            if (has(TextureUsage::DepthStencil)) {
                flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            }
            if (has(TextureUsage::TransferSrc)) {
                flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            }
            if (has(TextureUsage::TransferDst)) {
                flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            }
            if (has(TextureUsage::InputAttachment)) {
                flags |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
            }
            if (has(TextureUsage::ShadingRate)) {
                flags |= VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
            }
            return flags;
        }

        auto ToVkImageAspect(TextureAspect aspect) -> VkImageAspectFlags {
            switch (aspect) {
                case TextureAspect::Color: return VK_IMAGE_ASPECT_COLOR_BIT;
                case TextureAspect::Depth: return VK_IMAGE_ASPECT_DEPTH_BIT;
                case TextureAspect::Stencil: return VK_IMAGE_ASPECT_STENCIL_BIT;
                case TextureAspect::DepthStencil: return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            }
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }

        auto ToVkFilter(Filter f) -> VkFilter {
            return (f == Filter::Nearest) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        }

        auto ToVkSamplerMipmapMode(Filter f) -> VkSamplerMipmapMode {
            return (f == Filter::Nearest) ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
        }

        auto ToVkAddressMode(AddressMode m) -> VkSamplerAddressMode {
            switch (m) {
                case AddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
                case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                case AddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                case AddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            }
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }

        auto ToVkBorderColor(BorderColor c) -> VkBorderColor {
            switch (c) {
                case BorderColor::TransparentBlack: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
                case BorderColor::OpaqueBlack: return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
                case BorderColor::OpaqueWhite: return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            }
            return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        }

        auto ToVkCompareOp(CompareOp op) -> VkCompareOp {
            switch (op) {
                case CompareOp::Never: return VK_COMPARE_OP_NEVER;
                case CompareOp::Less: return VK_COMPARE_OP_LESS;
                case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
                case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
                case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
                case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
                case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
                case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
                case CompareOp::None: return VK_COMPARE_OP_NEVER;
            }
            return VK_COMPARE_OP_NEVER;
        }

        [[maybe_unused]] auto ToVkShaderStage(ShaderStage stage) -> VkShaderStageFlagBits {
            switch (stage) {
                case ShaderStage::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
                case ShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
                case ShaderStage::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
                case ShaderStage::Task: return VK_SHADER_STAGE_TASK_BIT_EXT;
                case ShaderStage::Mesh: return VK_SHADER_STAGE_MESH_BIT_EXT;
                case ShaderStage::RayGen: return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
                case ShaderStage::AnyHit: return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
                case ShaderStage::ClosestHit: return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
                case ShaderStage::Miss: return VK_SHADER_STAGE_MISS_BIT_KHR;
                case ShaderStage::Intersection: return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
                case ShaderStage::Callable: return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
                default: return VK_SHADER_STAGE_ALL;
            }
        }
    }  // namespace

    // =========================================================================
    // Buffer
    // =========================================================================

    auto VulkanDevice::CreateBufferImpl(const BufferDesc& desc) -> RhiResult<BufferHandle> {
        // 1. fill buffer info
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = desc.size;
        bufferInfo.usage = ToVkBufferUsage(desc.usage);
        // GpuOnly buffers require staging upload → implicitly add TransferDst.
        // CpuToGpu/GpuToCpu can be directly mapped → only add if user explicitly requests.
        if (desc.memory == MemoryLocation::GpuOnly || desc.memory == MemoryLocation::Auto) {
            bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        }
        // SharingMode: always EXCLUSIVE. Queue family ownership transfers are handled
        // via barriers (CmdPipelineBarrier with srcQueue/dstQueue). This avoids the
        // performance overhead of CONCURRENT mode while keeping the API thin (no
        // SharingMode in BufferDesc — D3D12/WebGPU/OpenGL have no equivalent concept).
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        // 2. fill alloc info
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = ToVmaMemoryUsage(desc.memory);
        if (desc.transient) {
            allocInfo.flags |= VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
        }
        if (desc.memory == MemoryLocation::CpuToGpu || desc.memory == MemoryLocation::GpuToCpu) {
            allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        // 3. apply for the buffer via VMA (implicitly mapped), VMA will automatically choose ReBAR or RAM staging
        // buffer to use.
        VkBuffer vkBuffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo vmaAllocInfo{};
        VkResult r = vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &vkBuffer, &allocation, &vmaAllocInfo);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        // 4. apply for a buffer handle from the pool
        auto [handle, data] = buffers_.Allocate();
        if (!data) {
            vmaDestroyBuffer(allocator_, vkBuffer, allocation);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->buffer = vkBuffer;
        data->allocation = allocation;
        data->mappedPtr = vmaAllocInfo.pMappedData;
        data->size = desc.size;
        data->usage = desc.usage;
        data->isPersistentlyMapped = (vmaAllocInfo.pMappedData != nullptr);

        // 5. query BDA if requested
        if ((static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(BufferUsage::ShaderDeviceAddress)) != 0) {
            VkBufferDeviceAddressInfo addrInfo{};
            addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            addrInfo.buffer = vkBuffer;
            data->deviceAddress = vkGetBufferDeviceAddress(device_, &addrInfo);
        }

        // 6. set debug name if provided
        if (desc.debugName && vkSetDebugUtilsObjectNameEXT) {
            VkDebugUtilsObjectNameInfoEXT nameInfo{};
            nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            nameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(vkBuffer);
            nameInfo.pObjectName = desc.debugName;
            vkSetDebugUtilsObjectNameEXT(device_, &nameInfo);
        }

        // 7. log buffer creation info (memory location detection)
        auto locInfo = DetectBufferLocation(allocator_, allocation);
        LogBufferCreation(desc, locInfo, data->deviceAddress);

        // TODO (nekomiya): Consider automatic cache management in Map/Unmap
        // Current VMA defaults to HOST_COHERENT memory for CpuToGpu/GpuToCpu,
        // so explicit Flush/Invalidate is not needed. If we need to support
        // non-coherent memory (e.g., mobile GPUs), implement automatic
        // cache management in MapBuffer/UnmapBuffer based on memory properties.
        //
        // What is HOST_COHERENT memory:
        // - Host (CPU) writes are immediately visible to device (GPU)
        // - Device writes are immediately visible to host
        // - No need for explicit vkFlushMappedMemoryRanges/vkInvalidateMappedMemoryRanges
        // - Performance: slightly slower than non-coherent due to automatic cache maintenance
        // - Trade-off: simplicity vs fine-grained cache control

        return handle;
    }

    void VulkanDevice::DestroyBufferImpl(BufferHandle h) {
        auto* data = buffers_.Lookup(h);
        if (!data) {
            return;
        }
        vmaDestroyBuffer(allocator_, data->buffer, data->allocation);
        buffers_.Free(h);
    }

    auto VulkanDevice::MapBufferImpl(BufferHandle h) -> RhiResult<void*> {
        auto* data = buffers_.Lookup(h);
        if (!data) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        if (data->mappedPtr) {
            return data->mappedPtr;
        }

        void* mapped = nullptr;
        VkResult r = vmaMapMemory(allocator_, data->allocation, &mapped);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfHostMemory);
        }
        data->mappedPtr = mapped;
        return mapped;
    }

    void VulkanDevice::UnmapBufferImpl(BufferHandle h) {
        auto* data = buffers_.Lookup(h);
        if (!data || !data->mappedPtr) {
            return;
        }
        // Persistently mapped buffers (created with VMA_ALLOCATION_CREATE_MAPPED_BIT) should not be unmapped - they
        // remain mapped for the lifetime of the buffer.
        if (data->isPersistentlyMapped) {
            return;
        }
        vmaUnmapMemory(allocator_, data->allocation);
        data->mappedPtr = nullptr;
    }

    void VulkanDevice::FlushMappedRangeImpl(BufferHandle h, uint64_t offset, uint64_t size) {
        auto* data = buffers_.Lookup(h);
        if (!data) {
            return;
        }
        vmaFlushAllocation(allocator_, data->allocation, offset, size);
    }

    void VulkanDevice::InvalidateMappedRangeImpl(BufferHandle h, uint64_t offset, uint64_t size) {
        auto* data = buffers_.Lookup(h);
        if (!data) {
            return;
        }
        vmaInvalidateAllocation(allocator_, data->allocation, offset, size);
    }

    auto VulkanDevice::GetBufferDeviceAddressImpl(BufferHandle h) -> uint64_t {
        auto* data = buffers_.Lookup(h);
        if (!data) {
            return 0;
        }
        if (data->deviceAddress != 0) {
            return data->deviceAddress;
        }

        VkBufferDeviceAddressInfo addrInfo{};
        addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addrInfo.buffer = data->buffer;
        data->deviceAddress = vkGetBufferDeviceAddress(device_, &addrInfo);
        return data->deviceAddress;
    }

    // =========================================================================
    // Texture
    // =========================================================================

    auto VulkanDevice::CreateTextureImpl(const TextureDesc& desc) -> RhiResult<TextureHandle> {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = ToVkImageType(desc.dimension);
        imageInfo.format = ToVkFormat(desc.format);
        imageInfo.extent = {desc.width, desc.height, desc.depth};
        imageInfo.mipLevels = desc.mipLevels;
        imageInfo.arrayLayers = desc.arrayLayers;
        imageInfo.samples = static_cast<VkSampleCountFlagBits>(desc.sampleCount);
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = ToVkImageUsage(desc.usage);
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (desc.dimension == TextureDimension::TexCube || desc.dimension == TextureDimension::TexCubeArray) {
            imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        }

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = ToVmaMemoryUsage(desc.memory);
        if (desc.transient) {
            allocInfo.flags |= VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
        }

        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VkResult r = vmaCreateImage(allocator_, &imageInfo, &allocInfo, &image, &allocation, nullptr);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = textures_.Allocate();
        if (!data) {
            vmaDestroyImage(allocator_, image, allocation);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->image = image;
        data->allocation = allocation;
        data->format = imageInfo.format;
        data->extent = imageInfo.extent;
        data->mipLevels = desc.mipLevels;
        data->arrayLayers = desc.arrayLayers;
        data->dimension = desc.dimension;
        data->ownsImage = true;

        if (desc.debugName && vkSetDebugUtilsObjectNameEXT) {
            VkDebugUtilsObjectNameInfoEXT nameInfo{};
            nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(image);
            nameInfo.pObjectName = desc.debugName;
            vkSetDebugUtilsObjectNameEXT(device_, &nameInfo);
        }

        return handle;
    }

    void VulkanDevice::DestroyTextureImpl(TextureHandle h) {
        auto* data = textures_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->ownsImage) {
            vmaDestroyImage(allocator_, data->image, data->allocation);
        }
        textures_.Free(h);
    }

    // =========================================================================
    // TextureView
    // =========================================================================

    auto VulkanDevice::CreateTextureViewImpl(const TextureViewDesc& desc) -> RhiResult<TextureViewHandle> {
        auto* texData = textures_.Lookup(desc.texture);
        if (!texData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        // Determine effective view dimension: inherit from parent texture if not explicitly specified.
        // TextureViewDesc defaults to Tex2D, but we use a sentinel check via comparing with the parent.
        // For most common cases (viewing entire texture), user can omit viewDimension and it will match.
        TextureDimension effectiveDimension = desc.viewDimension;
        // Note: We trust the user's explicit viewDimension. If they want a 2D view of a 2DArray slice,
        // they must set viewDimension=Tex2D explicitly. The default Tex2D works for swapchain/simple textures.

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texData->image;
        viewInfo.viewType = ToVkImageViewType(effectiveDimension);
        // Inherit format from parent texture if not explicitly specified. This is required by
        // VUID-VkImageViewCreateInfo-image-01762: when the image was not created with
        // VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, the view format must match exactly.
        viewInfo.format = (desc.format == Format::Undefined) ? texData->format : ToVkFormat(desc.format);
        viewInfo.subresourceRange.aspectMask = ToVkImageAspect(desc.aspect);
        viewInfo.subresourceRange.baseMipLevel = desc.baseMipLevel;
        // Use VK_REMAINING_MIP_LEVELS / VK_REMAINING_ARRAY_LAYERS when count is 0, allowing users to
        // request "all remaining" without knowing the exact texture dimensions at view creation time.
        viewInfo.subresourceRange.levelCount = (desc.mipLevelCount == 0) ? VK_REMAINING_MIP_LEVELS : desc.mipLevelCount;
        viewInfo.subresourceRange.baseArrayLayer = desc.baseArrayLayer;
        viewInfo.subresourceRange.layerCount
            = (desc.arrayLayerCount == 0) ? VK_REMAINING_ARRAY_LAYERS : desc.arrayLayerCount;

        VkImageView view = VK_NULL_HANDLE;
        VkResult r = vkCreateImageView(device_, &viewInfo, nullptr, &view);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = textureViews_.Allocate();
        if (!data) {
            vkDestroyImageView(device_, view, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->view = view;
        data->parentTexture = desc.texture;
        return handle;
    }

    auto VulkanDevice::GetTextureViewTextureImpl(TextureViewHandle h) -> TextureHandle {
        auto* data = textureViews_.Lookup(h);
        return data ? data->parentTexture : TextureHandle{};
    }

    void VulkanDevice::DestroyTextureViewImpl(TextureViewHandle h) {
        auto* data = textureViews_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->view != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, data->view, nullptr);
            data->view = VK_NULL_HANDLE;
        }
        textureViews_.Free(h);
    }

    // =========================================================================
    // Sampler
    // =========================================================================

    auto VulkanDevice::CreateSamplerImpl(const SamplerDesc& desc) -> RhiResult<SamplerHandle> {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = ToVkFilter(desc.magFilter);
        samplerInfo.minFilter = ToVkFilter(desc.minFilter);
        samplerInfo.mipmapMode = ToVkSamplerMipmapMode(desc.mipFilter);
        samplerInfo.addressModeU = ToVkAddressMode(desc.addressU);
        samplerInfo.addressModeV = ToVkAddressMode(desc.addressV);
        samplerInfo.addressModeW = ToVkAddressMode(desc.addressW);
        samplerInfo.mipLodBias = desc.mipLodBias;
        samplerInfo.anisotropyEnable = (desc.maxAnisotropy > 0.0f) ? VK_TRUE : VK_FALSE;
        samplerInfo.maxAnisotropy = desc.maxAnisotropy;
        samplerInfo.compareEnable = (desc.compareOp != CompareOp::None) ? VK_TRUE : VK_FALSE;
        samplerInfo.compareOp = ToVkCompareOp(desc.compareOp);
        samplerInfo.minLod = desc.minLod;
        samplerInfo.maxLod = desc.maxLod;
        samplerInfo.borderColor = ToVkBorderColor(desc.borderColor);
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        VkSampler sampler = VK_NULL_HANDLE;
        VkResult r = vkCreateSampler(device_, &samplerInfo, nullptr, &sampler);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = samplers_.Allocate();
        if (!data) {
            vkDestroySampler(device_, sampler, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->sampler = sampler;
        return handle;
    }

    void VulkanDevice::DestroySamplerImpl(SamplerHandle h) {
        auto* data = samplers_.Lookup(h);
        if (!data) {
            return;
        }
        vkDestroySampler(device_, data->sampler, nullptr);
        samplers_.Free(h);
    }

    // =========================================================================
    // ShaderModule
    // =========================================================================

    auto VulkanDevice::CreateShaderModuleImpl(const ShaderModuleDesc& desc) -> RhiResult<ShaderModuleHandle> {
        VkShaderModuleCreateInfo moduleInfo{};
        moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        moduleInfo.codeSize = desc.code.size();
        moduleInfo.pCode = reinterpret_cast<const uint32_t*>(desc.code.data());

        VkShaderModule module = VK_NULL_HANDLE;
        VkResult r = vkCreateShaderModule(device_, &moduleInfo, nullptr, &module);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::ShaderCompilationFailed);
        }

        auto [handle, data] = shaderModules_.Allocate();
        if (!data) {
            vkDestroyShaderModule(device_, module, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->module = module;

        if (desc.debugName && vkSetDebugUtilsObjectNameEXT) {
            VkDebugUtilsObjectNameInfoEXT nameInfo{};
            nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            nameInfo.objectType = VK_OBJECT_TYPE_SHADER_MODULE;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(module);
            nameInfo.pObjectName = desc.debugName;
            vkSetDebugUtilsObjectNameEXT(device_, &nameInfo);
        }

        return handle;
    }

    void VulkanDevice::DestroyShaderModuleImpl(ShaderModuleHandle h) {
        auto* data = shaderModules_.Lookup(h);
        if (!data) {
            return;
        }
        vkDestroyShaderModule(device_, data->module, nullptr);
        shaderModules_.Free(h);
    }

    // =========================================================================
    // Memory aliasing (RenderGraph transient resources)
    // =========================================================================

    auto VulkanDevice::CreateMemoryHeapImpl(const MemoryHeapDesc& desc) -> RhiResult<DeviceMemoryHandle> {
        VkMemoryRequirements memReq{};
        memReq.size = desc.size;
        memReq.alignment = 256;
        memReq.memoryTypeBits = UINT32_MAX;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = ToVmaMemoryUsage(desc.memory);

        VmaAllocation allocation = nullptr;
        VkResult r = vmaAllocateMemory(allocator_, &memReq, &allocInfo, &allocation, nullptr);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = deviceMemory_.Allocate();
        if (!data) {
            vmaFreeMemory(allocator_, allocation);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->allocation = allocation;
        data->size = desc.size;
        return handle;
    }

    void VulkanDevice::DestroyMemoryHeapImpl(DeviceMemoryHandle h) {
        auto* data = deviceMemory_.Lookup(h);
        if (!data) {
            return;
        }
        vmaFreeMemory(allocator_, data->allocation);
        deviceMemory_.Free(h);
    }

    void VulkanDevice::AliasBufferMemoryImpl(BufferHandle buf, DeviceMemoryHandle heap, uint64_t offset) {
        auto* bufData = buffers_.Lookup(buf);
        auto* memData = deviceMemory_.Lookup(heap);
        if (!bufData || !memData) {
            return;
        }

        VmaAllocationInfo allocInfo{};
        vmaGetAllocationInfo(allocator_, memData->allocation, &allocInfo);
        vkBindBufferMemory(device_, bufData->buffer, allocInfo.deviceMemory, allocInfo.offset + offset);
    }

    void VulkanDevice::AliasTextureMemoryImpl(TextureHandle tex, DeviceMemoryHandle heap, uint64_t offset) {
        auto* texData = textures_.Lookup(tex);
        auto* memData = deviceMemory_.Lookup(heap);
        if (!texData || !memData) {
            return;
        }

        VmaAllocationInfo allocInfo{};
        vmaGetAllocationInfo(allocator_, memData->allocation, &allocInfo);
        vkBindImageMemory(device_, texData->image, allocInfo.deviceMemory, allocInfo.offset + offset);
    }

    auto VulkanDevice::GetBufferMemoryRequirementsImpl(BufferHandle h) -> MemoryRequirements {
        auto* data = buffers_.Lookup(h);
        if (!data) {
            return {};
        }

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(device_, data->buffer, &memReq);
        return {memReq.size, memReq.alignment, memReq.memoryTypeBits};
    }

    auto VulkanDevice::GetTextureMemoryRequirementsImpl(TextureHandle h) -> MemoryRequirements {
        auto* data = textures_.Lookup(h);
        if (!data) {
            return {};
        }

        VkMemoryRequirements memReq{};
        vkGetImageMemoryRequirements(device_, data->image, &memReq);
        return {memReq.size, memReq.alignment, memReq.memoryTypeBits};
    }

    // =========================================================================
    // Sparse binding (bypasses VMA — direct Vulkan API)
    // =========================================================================

    auto VulkanDevice::GetSparsePageSizeImpl() const -> SparsePageSize {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &props);

        SparsePageSize result{};
        if (props.limits.sparseAddressSpaceSize == 0) {
            return result;
        }

        // Buffer sparse page: universally 64KB on all known implementations
        result.bufferPageSize = 65536ULL;

        // Query sparse image format properties for a representative format (RGBA8)
        // to determine the actual image page size
        VkSparseImageFormatProperties sparseImgProps[8]{};
        uint32_t propCount = 0;
        vkGetPhysicalDeviceSparseImageFormatProperties(
            physicalDevice_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_TILING_OPTIMAL, &propCount, nullptr
        );
        propCount = std::min(propCount, 8u);
        if (propCount > 0) {
            vkGetPhysicalDeviceSparseImageFormatProperties(
                physicalDevice_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_SAMPLE_COUNT_1_BIT,
                VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_TILING_OPTIMAL, &propCount, sparseImgProps
            );
            // Image page size = granularity.width * height * depth * bytesPerTexel
            // For RGBA8 (4 bpp): typical 128x128x1 * 4 = 65536 (64KB)
            auto& g = sparseImgProps[0].imageGranularity;
            result.imagePageSize = static_cast<uint64_t>(g.width) * g.height * g.depth * 4;
        } else {
            result.imagePageSize = 65536ULL;  // fallback
        }

        // Sparse properties from device
        result.standardBlockShape2D = (props.sparseProperties.residencyStandard2DBlockShape == VK_TRUE);
        result.standardBlockShape3D = (props.sparseProperties.residencyStandard3DBlockShape == VK_TRUE);
        result.standardBlockShapeMultisample
            = (props.sparseProperties.residencyStandard2DMultisampleBlockShape == VK_TRUE);
        result.alignedMipSize = (props.sparseProperties.residencyAlignedMipSize == VK_TRUE);
        result.nonResidentStrict = (props.sparseProperties.residencyNonResidentStrict == VK_TRUE);

        return result;
    }

    void VulkanDevice::SubmitSparseBindsImpl(
        QueueType queue, const SparseBindDesc& binds, std::span<const SemaphoreSubmitInfo> wait,
        std::span<const SemaphoreSubmitInfo> signal
    ) {
        VkQueue targetQueue = graphicsQueue_;
        if (queue == QueueType::Compute) {
            targetQueue = computeQueue_;
        } else if (queue == QueueType::Transfer) {
            targetQueue = transferQueue_;
        }

        // Marshal buffer sparse binds: group by buffer handle
        // Vulkan requires one VkSparseBufferMemoryBindInfo per buffer
        struct BufferBindGroup {
            VkBuffer buffer = VK_NULL_HANDLE;
            std::vector<VkSparseMemoryBind> binds;
        };
        std::vector<BufferBindGroup> bufferGroups;

        for (auto& b : binds.bufferBinds) {
            auto* bufData = buffers_.Lookup(b.buffer);
            if (!bufData) {
                continue;
            }

            VkSparseMemoryBind memBind{};
            memBind.resourceOffset = b.resourceOffset;
            memBind.size = b.size;
            if (b.memory.IsValid()) {
                auto* memData = deviceMemory_.Lookup(b.memory);
                if (memData) {
                    VmaAllocationInfo allocInfo{};
                    vmaGetAllocationInfo(allocator_, memData->allocation, &allocInfo);
                    memBind.memory = allocInfo.deviceMemory;
                    memBind.memoryOffset = allocInfo.offset + b.memoryOffset;
                }
            }
            // else: memory = VK_NULL_HANDLE means unbind (evict page)

            // Find or create group for this buffer
            bool found = false;
            for (auto& g : bufferGroups) {
                if (g.buffer == bufData->buffer) {
                    g.binds.push_back(memBind);
                    found = true;
                    break;
                }
            }
            if (!found) {
                bufferGroups.push_back({bufData->buffer, {memBind}});
            }
        }

        std::vector<VkSparseBufferMemoryBindInfo> vkBufferBindInfos;
        vkBufferBindInfos.reserve(bufferGroups.size());
        for (auto& group : bufferGroups) {
            VkSparseBufferMemoryBindInfo info{};
            info.buffer = group.buffer;
            info.bindCount = static_cast<uint32_t>(group.binds.size());
            info.pBinds = group.binds.data();
            vkBufferBindInfos.push_back(info);
        }

        // Marshal image sparse binds: group by texture handle
        struct ImageBindGroup {
            VkImage image = VK_NULL_HANDLE;
            std::vector<VkSparseImageMemoryBind> binds;
        };
        std::vector<ImageBindGroup> imageGroups;

        for (auto& t : binds.textureBinds) {
            auto* texData = textures_.Lookup(t.texture);
            if (!texData) {
                continue;
            }

            VkSparseImageMemoryBind imgBind{};
            imgBind.subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imgBind.subresource.mipLevel = t.subresource.baseMipLevel;
            imgBind.subresource.arrayLayer = t.subresource.baseArrayLayer;
            imgBind.offset = {t.offset.x, t.offset.y, t.offset.z};
            imgBind.extent = {t.extent.width, t.extent.height, t.extent.depth};
            if (t.memory.IsValid()) {
                auto* memData = deviceMemory_.Lookup(t.memory);
                if (memData) {
                    VmaAllocationInfo allocInfo{};
                    vmaGetAllocationInfo(allocator_, memData->allocation, &allocInfo);
                    imgBind.memory = allocInfo.deviceMemory;
                    imgBind.memoryOffset = allocInfo.offset + t.memoryOffset;
                }
            }

            bool found = false;
            for (auto& g : imageGroups) {
                if (g.image == texData->image) {
                    g.binds.push_back(imgBind);
                    found = true;
                    break;
                }
            }
            if (!found) {
                imageGroups.push_back({texData->image, {imgBind}});
            }
        }

        std::vector<VkSparseImageMemoryBindInfo> vkImageBindInfos;
        vkImageBindInfos.reserve(imageGroups.size());
        for (auto& group : imageGroups) {
            VkSparseImageMemoryBindInfo info{};
            info.image = group.image;
            info.bindCount = static_cast<uint32_t>(group.binds.size());
            info.pBinds = group.binds.data();
            vkImageBindInfos.push_back(info);
        }

        // Marshal wait semaphores
        std::vector<VkSemaphore> vkWaitSems;
        std::vector<uint64_t> vkWaitValues;
        vkWaitSems.reserve(wait.size());
        vkWaitValues.reserve(wait.size());
        for (auto& w : wait) {
            auto* semData = semaphores_.Lookup(w.semaphore);
            if (!semData) {
                continue;
            }
            vkWaitSems.push_back(semData->semaphore);
            vkWaitValues.push_back(w.value);
        }

        // Marshal signal semaphores
        std::vector<VkSemaphore> vkSignalSems;
        std::vector<uint64_t> vkSignalValues;
        vkSignalSems.reserve(signal.size());
        vkSignalValues.reserve(signal.size());
        for (auto& s : signal) {
            auto* semData = semaphores_.Lookup(s.semaphore);
            if (!semData) {
                continue;
            }
            vkSignalSems.push_back(semData->semaphore);
            vkSignalValues.push_back(s.value);
        }

        // Timeline semaphore info for sparse bind (Vulkan 1.2+)
        VkTimelineSemaphoreSubmitInfo timelineInfo{};
        timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timelineInfo.waitSemaphoreValueCount = static_cast<uint32_t>(vkWaitValues.size());
        timelineInfo.pWaitSemaphoreValues = vkWaitValues.data();
        timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(vkSignalValues.size());
        timelineInfo.pSignalSemaphoreValues = vkSignalValues.data();

        VkBindSparseInfo bindInfo{};
        bindInfo.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
        bindInfo.pNext = &timelineInfo;
        bindInfo.waitSemaphoreCount = static_cast<uint32_t>(vkWaitSems.size());
        bindInfo.pWaitSemaphores = vkWaitSems.data();
        bindInfo.bufferBindCount = static_cast<uint32_t>(vkBufferBindInfos.size());
        bindInfo.pBufferBinds = vkBufferBindInfos.data();
        bindInfo.imageBindCount = static_cast<uint32_t>(vkImageBindInfos.size());
        bindInfo.pImageBinds = vkImageBindInfos.data();
        bindInfo.signalSemaphoreCount = static_cast<uint32_t>(vkSignalSems.size());
        bindInfo.pSignalSemaphores = vkSignalSems.data();

        vkQueueBindSparse(targetQueue, 1, &bindInfo, VK_NULL_HANDLE);
    }

}  // namespace miki::rhi
