/** @file WebGPUResources.cpp
 *  @brief WebGPU / Dawn (Tier 3) backend — buffer, texture, sampler, shader, memory aliasing.
 */

#include "miki/rhi/backend/WebGPUDevice.h"
#include "miki/debug/StructuredLogger.h"

#include <dawn/webgpu.h>
#include <cstring>

namespace miki::rhi {

    // =========================================================================
    // Format conversion helpers
    // =========================================================================

    static auto ToWGPUTextureFormat(Format fmt) -> WGPUTextureFormat {
        switch (fmt) {
            case Format::R8_UNORM: return WGPUTextureFormat_R8Unorm;
            case Format::R8_SNORM: return WGPUTextureFormat_R8Snorm;
            case Format::R8_UINT: return WGPUTextureFormat_R8Uint;
            case Format::R8_SINT: return WGPUTextureFormat_R8Sint;
            case Format::R16_UINT: return WGPUTextureFormat_R16Uint;
            case Format::R16_SINT: return WGPUTextureFormat_R16Sint;
            case Format::R16_FLOAT: return WGPUTextureFormat_R16Float;
            case Format::RG8_UNORM: return WGPUTextureFormat_RG8Unorm;
            case Format::RG8_SNORM: return WGPUTextureFormat_RG8Snorm;
            case Format::RG8_UINT: return WGPUTextureFormat_RG8Uint;
            case Format::RG8_SINT: return WGPUTextureFormat_RG8Sint;
            case Format::R32_FLOAT: return WGPUTextureFormat_R32Float;
            case Format::R32_UINT: return WGPUTextureFormat_R32Uint;
            case Format::R32_SINT: return WGPUTextureFormat_R32Sint;
            case Format::RG16_UINT: return WGPUTextureFormat_RG16Uint;
            case Format::RG16_SINT: return WGPUTextureFormat_RG16Sint;
            case Format::RG16_FLOAT: return WGPUTextureFormat_RG16Float;
            case Format::RGBA8_UNORM: return WGPUTextureFormat_RGBA8Unorm;
            case Format::RGBA8_SRGB: return WGPUTextureFormat_RGBA8UnormSrgb;
            case Format::RGBA8_SNORM: return WGPUTextureFormat_RGBA8Snorm;
            case Format::RGBA8_UINT: return WGPUTextureFormat_RGBA8Uint;
            case Format::RGBA8_SINT: return WGPUTextureFormat_RGBA8Sint;
            case Format::BGRA8_UNORM: return WGPUTextureFormat_BGRA8Unorm;
            case Format::BGRA8_SRGB: return WGPUTextureFormat_BGRA8UnormSrgb;
            case Format::RGB10A2_UNORM: return WGPUTextureFormat_RGB10A2Unorm;
            case Format::RG32_FLOAT: return WGPUTextureFormat_RG32Float;
            case Format::RG32_UINT: return WGPUTextureFormat_RG32Uint;
            case Format::RG32_SINT: return WGPUTextureFormat_RG32Sint;
            case Format::RGBA16_UINT: return WGPUTextureFormat_RGBA16Uint;
            case Format::RGBA16_SINT: return WGPUTextureFormat_RGBA16Sint;
            case Format::RGBA16_FLOAT: return WGPUTextureFormat_RGBA16Float;
            case Format::RGBA32_FLOAT: return WGPUTextureFormat_RGBA32Float;
            case Format::RGBA32_UINT: return WGPUTextureFormat_RGBA32Uint;
            case Format::RGBA32_SINT: return WGPUTextureFormat_RGBA32Sint;
            case Format::D16_UNORM: return WGPUTextureFormat_Depth16Unorm;
            case Format::D24_UNORM_S8_UINT: return WGPUTextureFormat_Depth24PlusStencil8;
            case Format::D32_FLOAT: return WGPUTextureFormat_Depth32Float;
            case Format::D32_FLOAT_S8_UINT: return WGPUTextureFormat_Depth32FloatStencil8;
            case Format::BC1_UNORM: return WGPUTextureFormat_BC1RGBAUnorm;
            case Format::BC1_SRGB: return WGPUTextureFormat_BC1RGBAUnormSrgb;
            case Format::BC2_UNORM: return WGPUTextureFormat_BC2RGBAUnorm;
            case Format::BC2_SRGB: return WGPUTextureFormat_BC2RGBAUnormSrgb;
            case Format::BC3_UNORM: return WGPUTextureFormat_BC3RGBAUnorm;
            case Format::BC3_SRGB: return WGPUTextureFormat_BC3RGBAUnormSrgb;
            case Format::BC4_UNORM: return WGPUTextureFormat_BC4RUnorm;
            case Format::BC4_SNORM: return WGPUTextureFormat_BC4RSnorm;
            case Format::BC5_UNORM: return WGPUTextureFormat_BC5RGUnorm;
            case Format::BC5_SNORM: return WGPUTextureFormat_BC5RGSnorm;
            case Format::BC6H_UFLOAT: return WGPUTextureFormat_BC6HRGBUfloat;
            case Format::BC6H_SFLOAT: return WGPUTextureFormat_BC6HRGBFloat;
            case Format::BC7_UNORM: return WGPUTextureFormat_BC7RGBAUnorm;
            case Format::BC7_SRGB: return WGPUTextureFormat_BC7RGBAUnormSrgb;
            default:
                MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU: unsupported format {}", static_cast<int>(fmt));
                return WGPUTextureFormat_RGBA8Unorm;
        }
    }

    static auto ToWGPUTextureDimension(TextureDimension dim) -> WGPUTextureDimension {
        switch (dim) {
            case TextureDimension::Tex1D: return WGPUTextureDimension_1D;
            case TextureDimension::Tex2D:
            case TextureDimension::TexCube:
            case TextureDimension::Tex2DArray:
            case TextureDimension::TexCubeArray: return WGPUTextureDimension_2D;
            case TextureDimension::Tex3D: return WGPUTextureDimension_3D;
            default: return WGPUTextureDimension_2D;
        }
    }

    static auto ToWGPUTextureViewDimension(TextureDimension dim) -> WGPUTextureViewDimension {
        switch (dim) {
            case TextureDimension::Tex1D: return WGPUTextureViewDimension_1D;
            case TextureDimension::Tex2D: return WGPUTextureViewDimension_2D;
            case TextureDimension::Tex3D: return WGPUTextureViewDimension_3D;
            case TextureDimension::TexCube: return WGPUTextureViewDimension_Cube;
            case TextureDimension::Tex2DArray: return WGPUTextureViewDimension_2DArray;
            case TextureDimension::TexCubeArray: return WGPUTextureViewDimension_CubeArray;
            default: return WGPUTextureViewDimension_2D;
        }
    }

    static auto ToWGPUAddressMode(AddressMode mode) -> WGPUAddressMode {
        switch (mode) {
            case AddressMode::Repeat: return WGPUAddressMode_Repeat;
            case AddressMode::MirroredRepeat: return WGPUAddressMode_MirrorRepeat;
            case AddressMode::ClampToEdge: return WGPUAddressMode_ClampToEdge;
            default: return WGPUAddressMode_ClampToEdge;
        }
    }

    static auto ToWGPUFilterMode(Filter filter) -> WGPUFilterMode {
        switch (filter) {
            case Filter::Nearest: return WGPUFilterMode_Nearest;
            case Filter::Linear: return WGPUFilterMode_Linear;
            default: return WGPUFilterMode_Linear;
        }
    }

    static auto ToWGPUMipmapFilterMode(Filter filter) -> WGPUMipmapFilterMode {
        switch (filter) {
            case Filter::Nearest: return WGPUMipmapFilterMode_Nearest;
            case Filter::Linear: return WGPUMipmapFilterMode_Linear;
            default: return WGPUMipmapFilterMode_Linear;
        }
    }

    static auto ToWGPUCompareFunction(CompareOp op) -> WGPUCompareFunction {
        switch (op) {
            case CompareOp::Never: return WGPUCompareFunction_Never;
            case CompareOp::Less: return WGPUCompareFunction_Less;
            case CompareOp::Equal: return WGPUCompareFunction_Equal;
            case CompareOp::LessOrEqual: return WGPUCompareFunction_LessEqual;
            case CompareOp::Greater: return WGPUCompareFunction_Greater;
            case CompareOp::NotEqual: return WGPUCompareFunction_NotEqual;
            case CompareOp::GreaterOrEqual: return WGPUCompareFunction_GreaterEqual;
            case CompareOp::Always: return WGPUCompareFunction_Always;
            default: return WGPUCompareFunction_Undefined;
        }
    }

    // =========================================================================
    // Buffer
    // =========================================================================

    auto WebGPUDevice::CreateBufferImpl(const BufferDesc& desc) -> RhiResult<BufferHandle> {
        auto [handle, data] = buffers_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        WGPUBufferUsage usage = 0;
        if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(BufferUsage::Vertex)) {
            usage |= WGPUBufferUsage_Vertex;
        }
        if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(BufferUsage::Index)) {
            usage |= WGPUBufferUsage_Index;
        }
        if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(BufferUsage::Uniform)) {
            usage |= WGPUBufferUsage_Uniform;
        }
        if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(BufferUsage::Storage)) {
            usage |= WGPUBufferUsage_Storage;
        }
        if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(BufferUsage::Indirect)) {
            usage |= WGPUBufferUsage_Indirect;
        }
        if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(BufferUsage::TransferSrc)) {
            usage |= WGPUBufferUsage_CopySrc;
        }
        if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(BufferUsage::TransferDst)) {
            usage |= WGPUBufferUsage_CopyDst;
        }

        // CpuToGpu / GpuToCpu need map access
        bool mappable = (desc.memory == MemoryLocation::CpuToGpu || desc.memory == MemoryLocation::GpuToCpu);
        if (mappable) {
            if (desc.memory == MemoryLocation::CpuToGpu) {
                usage |= WGPUBufferUsage_MapWrite;
            }
            if (desc.memory == MemoryLocation::GpuToCpu) {
                usage |= WGPUBufferUsage_MapRead;
            }
        }

        WGPUBufferDescriptor bufDesc{};
        bufDesc.label = desc.debugName ? WGPUStringView{.data = desc.debugName, .length = WGPU_STRLEN}
                                       : WGPUStringView{.data = nullptr, .length = 0};
        bufDesc.usage = usage;
        bufDesc.size = desc.size;
        bufDesc.mappedAtCreation = false;

        data->buffer = wgpuDeviceCreateBuffer(device_, &bufDesc);
        if (!data->buffer) {
            buffers_.Free(handle);
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        data->size = desc.size;
        data->usage = desc.usage;
        data->wgpuUsage = usage;
        data->mappedPtr = nullptr;

        totalAllocatedBytes_ += desc.size;
        ++totalAllocationCount_;

        return handle;
    }

    void WebGPUDevice::DestroyBufferImpl(BufferHandle h) {
        auto* data = buffers_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->buffer) {
            totalAllocatedBytes_ -= data->size;
            --totalAllocationCount_;
            wgpuBufferDestroy(data->buffer);
            wgpuBufferRelease(data->buffer);
        }
        buffers_.Free(h);
    }

    auto WebGPUDevice::MapBufferImpl(BufferHandle h) -> RhiResult<void*> {
        auto* data = buffers_.Lookup(h);
        if (!data || !data->buffer) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        // Synchronous map via callback
        struct MapData {
            WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
            bool done = false;
        } mapData;

        WGPUMapMode mode = WGPUMapMode_None;
        if (data->wgpuUsage & WGPUBufferUsage_MapRead) {
            mode = WGPUMapMode_Read;
        } else if (data->wgpuUsage & WGPUBufferUsage_MapWrite) {
            mode = WGPUMapMode_Write;
        } else {
            return std::unexpected(RhiError::InvalidParameter);
        }

        WGPUBufferMapCallbackInfo mapCbInfo{};
        mapCbInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        mapCbInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*) {
            auto* md = static_cast<MapData*>(userdata1);
            md->status = status;
            md->done = true;
        };
        mapCbInfo.userdata1 = &mapData;
        wgpuBufferMapAsync(data->buffer, mode, 0, data->size, mapCbInfo);

#ifndef EMSCRIPTEN
        while (!mapData.done) {
            wgpuDeviceTick(device_);
        }
#endif

        if (mapData.status != WGPUMapAsyncStatus_Success) {
            return std::unexpected(RhiError::OutOfHostMemory);
        }

        if (mode == WGPUMapMode_Read) {
            data->mappedPtr = const_cast<void*>(wgpuBufferGetConstMappedRange(data->buffer, 0, data->size));
        } else {
            data->mappedPtr = wgpuBufferGetMappedRange(data->buffer, 0, data->size);
        }

        return data->mappedPtr;
    }

    void WebGPUDevice::UnmapBufferImpl(BufferHandle h) {
        auto* data = buffers_.Lookup(h);
        if (!data || !data->buffer) {
            return;
        }
        wgpuBufferUnmap(data->buffer);
        data->mappedPtr = nullptr;
    }

    void WebGPUDevice::FlushMappedRangeImpl(BufferHandle, uint64_t, uint64_t) {
        // WebGPU has no persistent mapping — all writes go through wgpuQueueWriteBuffer.
        // MapAsync + Unmap is the only mapped access pattern. No flush needed.
    }

    void WebGPUDevice::InvalidateMappedRangeImpl(BufferHandle, uint64_t, uint64_t) {
        // WebGPU MapAsync for read guarantees coherent data on callback completion.
    }

    auto WebGPUDevice::GetBufferDeviceAddressImpl([[maybe_unused]] BufferHandle h) -> uint64_t {
        return 0;  // No BDA on WebGPU T3
    }

    // =========================================================================
    // Texture
    // =========================================================================

    auto WebGPUDevice::CreateTextureImpl(const TextureDesc& desc) -> RhiResult<TextureHandle> {
        auto [handle, data] = textures_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        WGPUTextureUsage usage = 0;
        if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(TextureUsage::Sampled)) {
            usage |= WGPUTextureUsage_TextureBinding;
        }
        if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(TextureUsage::Storage)) {
            usage |= WGPUTextureUsage_StorageBinding;
        }
        if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(TextureUsage::ColorAttachment)) {
            usage |= WGPUTextureUsage_RenderAttachment;
        }
        if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(TextureUsage::DepthStencil)) {
            usage |= WGPUTextureUsage_RenderAttachment;
        }
        if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(TextureUsage::TransferSrc)) {
            usage |= WGPUTextureUsage_CopySrc;
        }
        if (static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(TextureUsage::TransferDst)) {
            usage |= WGPUTextureUsage_CopyDst;
        }

        WGPUTextureFormat wgpuFormat = ToWGPUTextureFormat(desc.format);
        WGPUTextureDimension wgpuDim = ToWGPUTextureDimension(desc.dimension);

        uint32_t depthOrArrayLayers = 1;
        if (desc.dimension == TextureDimension::Tex3D) {
            depthOrArrayLayers = desc.depth;
        } else if (desc.dimension == TextureDimension::TexCube) {
            depthOrArrayLayers = 6;
        } else if (desc.dimension == TextureDimension::Tex2DArray || desc.dimension == TextureDimension::TexCubeArray) {
            depthOrArrayLayers = desc.arrayLayers;
        }

        WGPUTextureDescriptor texDesc{};
        texDesc.label = desc.debugName ? WGPUStringView{.data = desc.debugName, .length = WGPU_STRLEN}
                                       : WGPUStringView{.data = nullptr, .length = 0};
        texDesc.usage = usage;
        texDesc.dimension = wgpuDim;
        texDesc.size = {desc.width, desc.height, depthOrArrayLayers};
        texDesc.format = wgpuFormat;
        texDesc.mipLevelCount = desc.mipLevels;
        texDesc.sampleCount = desc.sampleCount;
        texDesc.viewFormatCount = 0;
        texDesc.viewFormats = nullptr;

        data->texture = wgpuDeviceCreateTexture(device_, &texDesc);
        if (!data->texture) {
            textures_.Free(handle);
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        data->dimension = wgpuDim;
        data->format = wgpuFormat;
        data->width = desc.width;
        data->height = desc.height;
        data->depthOrArrayLayers = depthOrArrayLayers;
        data->mipLevels = desc.mipLevels;
        data->sampleCount = desc.sampleCount;
        data->ownsTexture = true;

        ++totalAllocationCount_;

        return handle;
    }

    auto WebGPUDevice::CreateTextureViewImpl(const TextureViewDesc& desc) -> RhiResult<TextureViewHandle> {
        auto [handle, data] = textureViews_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        auto* texData = textures_.Lookup(desc.texture);
        if (!texData) {
            textureViews_.Free(handle);
            return std::unexpected(RhiError::InvalidHandle);
        }

        // Resolve "all remaining" counts (0 means all remaining mips/layers).
        // WebGPU uses WGPU_MIP_LEVEL_COUNT_UNDEFINED / WGPU_ARRAY_LAYER_COUNT_UNDEFINED for "all remaining".
        uint32_t effectiveMipCount = (desc.mipLevelCount == 0) ? WGPU_MIP_LEVEL_COUNT_UNDEFINED : desc.mipLevelCount;
        uint32_t effectiveLayerCount
            = (desc.arrayLayerCount == 0) ? WGPU_ARRAY_LAYER_COUNT_UNDEFINED : desc.arrayLayerCount;

        WGPUTextureViewDescriptor viewDesc{};
        // Inherit format from parent texture if not explicitly specified.
        viewDesc.format = (desc.format == Format::Undefined) ? texData->format : ToWGPUTextureFormat(desc.format);
        viewDesc.dimension = ToWGPUTextureViewDimension(desc.viewDimension);
        viewDesc.baseMipLevel = desc.baseMipLevel;
        viewDesc.mipLevelCount = effectiveMipCount;
        viewDesc.baseArrayLayer = desc.baseArrayLayer;
        viewDesc.arrayLayerCount = effectiveLayerCount;

        switch (desc.aspect) {
            case TextureAspect::Color: viewDesc.aspect = WGPUTextureAspect_All; break;
            case TextureAspect::Depth: viewDesc.aspect = WGPUTextureAspect_DepthOnly; break;
            case TextureAspect::Stencil: viewDesc.aspect = WGPUTextureAspect_StencilOnly; break;
            default: viewDesc.aspect = WGPUTextureAspect_All; break;
        }

        data->view = wgpuTextureCreateView(texData->texture, &viewDesc);
        if (!data->view) {
            textureViews_.Free(handle);
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        data->parentTexture = desc.texture;

        return handle;
    }

    void WebGPUDevice::DestroyTextureViewImpl(TextureViewHandle h) {
        auto* data = textureViews_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->view) {
            wgpuTextureViewRelease(data->view);
            data->view = nullptr;
        }
        textureViews_.Free(h);
    }

    void WebGPUDevice::DestroyTextureImpl(TextureHandle h) {
        auto* data = textures_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->texture && data->ownsTexture) {
            wgpuTextureDestroy(data->texture);
            wgpuTextureRelease(data->texture);
            --totalAllocationCount_;
        }
        textures_.Free(h);
    }

    // =========================================================================
    // Sampler
    // =========================================================================

    auto WebGPUDevice::CreateSamplerImpl(const SamplerDesc& desc) -> RhiResult<SamplerHandle> {
        auto [handle, data] = samplers_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        WGPUSamplerDescriptor samplerDesc{};
        samplerDesc.addressModeU = ToWGPUAddressMode(desc.addressU);
        samplerDesc.addressModeV = ToWGPUAddressMode(desc.addressV);
        samplerDesc.addressModeW = ToWGPUAddressMode(desc.addressW);
        samplerDesc.magFilter = ToWGPUFilterMode(desc.magFilter);
        samplerDesc.minFilter = ToWGPUFilterMode(desc.minFilter);
        samplerDesc.mipmapFilter = ToWGPUMipmapFilterMode(desc.mipFilter);
        samplerDesc.lodMinClamp = desc.minLod;
        samplerDesc.lodMaxClamp = desc.maxLod;
        samplerDesc.maxAnisotropy = static_cast<uint16_t>(desc.maxAnisotropy > 0 ? desc.maxAnisotropy : 1);

        if (desc.compareOp != CompareOp::None) {
            samplerDesc.compare = ToWGPUCompareFunction(desc.compareOp);
        } else {
            samplerDesc.compare = WGPUCompareFunction_Undefined;
        }

        data->sampler = wgpuDeviceCreateSampler(device_, &samplerDesc);
        if (!data->sampler) {
            samplers_.Free(handle);
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        return handle;
    }

    void WebGPUDevice::DestroySamplerImpl(SamplerHandle h) {
        auto* data = samplers_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->sampler) {
            wgpuSamplerRelease(data->sampler);
        }
        samplers_.Free(h);
    }

    // =========================================================================
    // Shader module
    // =========================================================================

    auto WebGPUDevice::CreateShaderModuleImpl(const ShaderModuleDesc& desc) -> RhiResult<ShaderModuleHandle> {
        auto [handle, data] = shaderModules_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        // Interpret code as WGSL text
        std::string_view wgslSource(reinterpret_cast<const char*>(desc.code.data()), desc.code.size());

        WGPUShaderSourceWGSL wgslDesc{};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.chain.next = nullptr;
        wgslDesc.code = {.data = wgslSource.data(), .length = wgslSource.size()};

        WGPUShaderModuleDescriptor moduleDesc{};
        moduleDesc.nextInChain = &wgslDesc.chain;
        moduleDesc.label = desc.debugName ? WGPUStringView{.data = desc.debugName, .length = WGPU_STRLEN}
                                          : WGPUStringView{.data = nullptr, .length = 0};

        data->module = wgpuDeviceCreateShaderModule(device_, &moduleDesc);
        if (!data->module) {
            shaderModules_.Free(handle);
            return std::unexpected(RhiError::ShaderCompilationFailed);
        }

        data->stage = desc.stage;
        data->entryPoint = desc.entryPoint ? desc.entryPoint : "main";
        data->wgslSource = std::string(wgslSource);

        return handle;
    }

    void WebGPUDevice::DestroyShaderModuleImpl(ShaderModuleHandle h) {
        auto* data = shaderModules_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->module) {
            wgpuShaderModuleRelease(data->module);
        }
        shaderModules_.Free(h);
    }

    // =========================================================================
    // Memory aliasing (not supported on T3 — fallback: separate allocation)
    // =========================================================================

    auto WebGPUDevice::CreateMemoryHeapImpl([[maybe_unused]] const MemoryHeapDesc& desc)
        -> RhiResult<DeviceMemoryHandle> {
        MIKI_LOG_WARN(
            ::miki::debug::LogCategory::Rhi, "WebGPU T3: memory aliasing not supported, fallback to separate alloc"
        );
        return std::unexpected(RhiError::FeatureNotSupported);
    }

    void WebGPUDevice::DestroyMemoryHeapImpl([[maybe_unused]] DeviceMemoryHandle h) {}

    void WebGPUDevice::AliasBufferMemoryImpl(
        [[maybe_unused]] BufferHandle buf, [[maybe_unused]] DeviceMemoryHandle heap, [[maybe_unused]] uint64_t offset
    ) {
        MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU T3: AliasBufferMemory not supported");
    }

    void WebGPUDevice::AliasTextureMemoryImpl(
        [[maybe_unused]] TextureHandle tex, [[maybe_unused]] DeviceMemoryHandle heap, [[maybe_unused]] uint64_t offset
    ) {
        MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU T3: AliasTextureMemory not supported");
    }

    auto WebGPUDevice::GetBufferMemoryRequirementsImpl(BufferHandle h) -> MemoryRequirements {
        auto* data = buffers_.Lookup(h);
        if (!data) {
            return {};
        }
        return MemoryRequirements{.size = data->size, .alignment = 256, .memoryTypeBits = 0};
    }

    auto WebGPUDevice::GetTextureMemoryRequirementsImpl([[maybe_unused]] TextureHandle h) -> MemoryRequirements {
        // WebGPU doesn't expose memory requirements
        return MemoryRequirements{.size = 0, .alignment = 256, .memoryTypeBits = 0};
    }

}  // namespace miki::rhi
