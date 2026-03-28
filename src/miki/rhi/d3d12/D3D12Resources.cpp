/** @file D3D12Resources.cpp
 *  @brief D3D12 (Tier 1) backend — Buffer, Texture, TextureView, Sampler,
 *         ShaderModule, Memory aliasing, Sparse binding.
 *
 *  All resource creation uses D3D12MA for memory management.
 *  Resources are stored in typed HandlePool slots for O(1) lookup.
 */

#include "miki/rhi/backend/D3D12Device.h"

#include <D3D12MemAlloc.h>

#include <cassert>
#include <cstring>

namespace miki::rhi {

    // =========================================================================
    // Format conversion: miki::rhi::Format -> DXGI_FORMAT
    // =========================================================================

    namespace {
        auto ToDxgiFormat(Format fmt) -> DXGI_FORMAT {
            switch (fmt) {
                case Format::R8_UNORM: return DXGI_FORMAT_R8_UNORM;
                case Format::R8_SNORM: return DXGI_FORMAT_R8_SNORM;
                case Format::R8_UINT: return DXGI_FORMAT_R8_UINT;
                case Format::R8_SINT: return DXGI_FORMAT_R8_SINT;
                case Format::RG8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
                case Format::RG8_SNORM: return DXGI_FORMAT_R8G8_SNORM;
                case Format::RG8_UINT: return DXGI_FORMAT_R8G8_UINT;
                case Format::RG8_SINT: return DXGI_FORMAT_R8G8_SINT;
                case Format::RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
                case Format::RGBA8_SNORM: return DXGI_FORMAT_R8G8B8A8_SNORM;
                case Format::RGBA8_UINT: return DXGI_FORMAT_R8G8B8A8_UINT;
                case Format::RGBA8_SINT: return DXGI_FORMAT_R8G8B8A8_SINT;
                case Format::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                case Format::BGRA8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
                case Format::BGRA8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                case Format::R16_UNORM: return DXGI_FORMAT_R16_UNORM;
                case Format::R16_SNORM: return DXGI_FORMAT_R16_SNORM;
                case Format::R16_UINT: return DXGI_FORMAT_R16_UINT;
                case Format::R16_SINT: return DXGI_FORMAT_R16_SINT;
                case Format::R16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
                case Format::RG16_UNORM: return DXGI_FORMAT_R16G16_UNORM;
                case Format::RG16_SNORM: return DXGI_FORMAT_R16G16_SNORM;
                case Format::RG16_UINT: return DXGI_FORMAT_R16G16_UINT;
                case Format::RG16_SINT: return DXGI_FORMAT_R16G16_SINT;
                case Format::RG16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
                case Format::RGBA16_UNORM: return DXGI_FORMAT_R16G16B16A16_UNORM;
                case Format::RGBA16_SNORM: return DXGI_FORMAT_R16G16B16A16_SNORM;
                case Format::RGBA16_UINT: return DXGI_FORMAT_R16G16B16A16_UINT;
                case Format::RGBA16_SINT: return DXGI_FORMAT_R16G16B16A16_SINT;
                case Format::RGBA16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
                case Format::R32_UINT: return DXGI_FORMAT_R32_UINT;
                case Format::R32_SINT: return DXGI_FORMAT_R32_SINT;
                case Format::R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
                case Format::RG32_UINT: return DXGI_FORMAT_R32G32_UINT;
                case Format::RG32_SINT: return DXGI_FORMAT_R32G32_SINT;
                case Format::RG32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
                case Format::RGB32_UINT: return DXGI_FORMAT_R32G32B32_UINT;
                case Format::RGB32_SINT: return DXGI_FORMAT_R32G32B32_SINT;
                case Format::RGB32_FLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
                case Format::RGBA32_UINT: return DXGI_FORMAT_R32G32B32A32_UINT;
                case Format::RGBA32_SINT: return DXGI_FORMAT_R32G32B32A32_SINT;
                case Format::RGBA32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
                case Format::RGB10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
                case Format::RG11B10_FLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
                case Format::D16_UNORM: return DXGI_FORMAT_D16_UNORM;
                case Format::D32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
                case Format::D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
                case Format::D32_FLOAT_S8_UINT: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
                case Format::BC1_UNORM: return DXGI_FORMAT_BC1_UNORM;
                case Format::BC1_SRGB: return DXGI_FORMAT_BC1_UNORM_SRGB;
                case Format::BC2_UNORM: return DXGI_FORMAT_BC2_UNORM;
                case Format::BC2_SRGB: return DXGI_FORMAT_BC2_UNORM_SRGB;
                case Format::BC3_UNORM: return DXGI_FORMAT_BC3_UNORM;
                case Format::BC3_SRGB: return DXGI_FORMAT_BC3_UNORM_SRGB;
                case Format::BC4_UNORM: return DXGI_FORMAT_BC4_UNORM;
                case Format::BC4_SNORM: return DXGI_FORMAT_BC4_SNORM;
                case Format::BC5_UNORM: return DXGI_FORMAT_BC5_UNORM;
                case Format::BC5_SNORM: return DXGI_FORMAT_BC5_SNORM;
                case Format::BC6H_UFLOAT: return DXGI_FORMAT_BC6H_UF16;
                case Format::BC6H_SFLOAT: return DXGI_FORMAT_BC6H_SF16;
                case Format::BC7_UNORM: return DXGI_FORMAT_BC7_UNORM;
                case Format::BC7_SRGB: return DXGI_FORMAT_BC7_UNORM_SRGB;
                case Format::ASTC_4x4_UNORM: return DXGI_FORMAT_UNKNOWN;  // No ASTC on D3D12
                case Format::ASTC_4x4_SRGB: return DXGI_FORMAT_UNKNOWN;
                default: return DXGI_FORMAT_UNKNOWN;
            }
        }

        auto ToD3D12HeapType(MemoryLocation loc) -> D3D12_HEAP_TYPE {
            switch (loc) {
                case MemoryLocation::GpuOnly: return D3D12_HEAP_TYPE_DEFAULT;
                case MemoryLocation::CpuToGpu: return D3D12_HEAP_TYPE_UPLOAD;
                case MemoryLocation::GpuToCpu: return D3D12_HEAP_TYPE_READBACK;
                case MemoryLocation::Auto: return D3D12_HEAP_TYPE_DEFAULT;
            }
            return D3D12_HEAP_TYPE_DEFAULT;
        }

        auto ToD3D12ResourceFlags(BufferUsage usage) -> D3D12_RESOURCE_FLAGS {
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
            auto has
                = [usage](BufferUsage bit) { return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(bit)) != 0; };
            if (has(BufferUsage::Storage)) {
                flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            }
            return flags;
        }

        auto ToD3D12TextureResourceFlags(TextureUsage usage) -> D3D12_RESOURCE_FLAGS {
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
            auto has = [usage](TextureUsage bit) {
                return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(bit)) != 0;
            };
            if (has(TextureUsage::ColorAttachment)) {
                flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            }
            if (has(TextureUsage::DepthStencil)) {
                flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            }
            if (has(TextureUsage::Storage)) {
                flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            }
            return flags;
        }

        auto ToD3D12ResourceDimension(TextureDimension dim) -> D3D12_RESOURCE_DIMENSION {
            switch (dim) {
                case TextureDimension::Tex1D: return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
                case TextureDimension::Tex3D: return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
                default: return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            }
        }

        auto ToD3D12SrvDimension(TextureDimension dim) -> D3D12_SRV_DIMENSION {
            switch (dim) {
                case TextureDimension::Tex1D: return D3D12_SRV_DIMENSION_TEXTURE1D;
                case TextureDimension::Tex2D: return D3D12_SRV_DIMENSION_TEXTURE2D;
                case TextureDimension::Tex3D: return D3D12_SRV_DIMENSION_TEXTURE3D;
                case TextureDimension::TexCube: return D3D12_SRV_DIMENSION_TEXTURECUBE;
                case TextureDimension::Tex2DArray: return D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                case TextureDimension::TexCubeArray: return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
            }
            return D3D12_SRV_DIMENSION_TEXTURE2D;
        }

        auto IsDepthFormat(DXGI_FORMAT fmt) -> bool {
            return fmt == DXGI_FORMAT_D16_UNORM || fmt == DXGI_FORMAT_D32_FLOAT || fmt == DXGI_FORMAT_D24_UNORM_S8_UINT
                   || fmt == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        }

        auto ToTypelessDepthFormat(DXGI_FORMAT fmt) -> DXGI_FORMAT {
            switch (fmt) {
                case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_TYPELESS;
                case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_TYPELESS;
                case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24G8_TYPELESS;
                case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32G8X24_TYPELESS;
                default: return fmt;
            }
        }

        auto ToSrvDepthFormat(DXGI_FORMAT fmt) -> DXGI_FORMAT {
            switch (fmt) {
                case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_UNORM;
                case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
                case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
                case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
                default: return fmt;
            }
        }
    }  // namespace

    // =========================================================================
    // Buffer
    // =========================================================================

    auto D3D12Device::CreateBufferImpl(const BufferDesc& desc) -> RhiResult<BufferHandle> {
        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = desc.size;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = ToD3D12ResourceFlags(desc.usage);

        D3D12MA::ALLOCATION_DESC allocDesc{};
        allocDesc.HeapType = ToD3D12HeapType(desc.memory);

        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
        if (desc.memory == MemoryLocation::CpuToGpu) {
            initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
        } else if (desc.memory == MemoryLocation::GpuToCpu) {
            initialState = D3D12_RESOURCE_STATE_COPY_DEST;
        }

        D3D12MA::Allocation* allocation = nullptr;
        ComPtr<ID3D12Resource> resource;
        HRESULT hr = allocator_->CreateResource(
            &allocDesc, &resourceDesc, initialState, nullptr, &allocation, IID_PPV_ARGS(&resource)
        );
        if (FAILED(hr)) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = buffers_.Allocate();
        if (!data) {
            allocation->Release();
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->resource = std::move(resource);
        data->allocation = allocation;
        data->size = desc.size;
        data->usage = desc.usage;
        data->gpuAddress = data->resource->GetGPUVirtualAddress();

        // Map persistently for upload/readback
        if (desc.memory == MemoryLocation::CpuToGpu || desc.memory == MemoryLocation::GpuToCpu) {
            D3D12_RANGE readRange{0, 0};
            if (desc.memory == MemoryLocation::GpuToCpu) {
                readRange.End = desc.size;
            }
            data->resource->Map(0, &readRange, &data->mappedPtr);
        }

        if (desc.debugName) {
            wchar_t wname[256]{};
            mbstowcs(wname, desc.debugName, 255);
            data->resource->SetName(wname);
        }

        return handle;
    }

    void D3D12Device::DestroyBufferImpl(BufferHandle h) {
        auto* data = buffers_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->mappedPtr) {
            data->resource->Unmap(0, nullptr);
        }
        if (data->allocation) {
            data->allocation->Release();
        }
        buffers_.Free(h);
    }

    auto D3D12Device::MapBufferImpl(BufferHandle h) -> RhiResult<void*> {
        auto* data = buffers_.Lookup(h);
        if (!data) {
            return std::unexpected(RhiError::InvalidHandle);
        }
        if (data->mappedPtr) {
            return data->mappedPtr;
        }

        D3D12_RANGE readRange{0, 0};
        HRESULT hr = data->resource->Map(0, &readRange, &data->mappedPtr);
        if (FAILED(hr)) {
            return std::unexpected(RhiError::OutOfHostMemory);
        }
        return data->mappedPtr;
    }

    void D3D12Device::UnmapBufferImpl(BufferHandle h) {
        auto* data = buffers_.Lookup(h);
        if (!data || !data->mappedPtr) {
            return;
        }
        data->resource->Unmap(0, nullptr);
        data->mappedPtr = nullptr;
    }

    auto D3D12Device::GetBufferDeviceAddressImpl(BufferHandle h) -> uint64_t {
        auto* data = buffers_.Lookup(h);
        if (!data) {
            return 0;
        }
        return data->gpuAddress;
    }

    // =========================================================================
    // Texture
    // =========================================================================

    auto D3D12Device::CreateTextureImpl(const TextureDesc& desc) -> RhiResult<TextureHandle> {
        DXGI_FORMAT dxgiFormat = ToDxgiFormat(desc.format);
        bool isDepth = IsDepthFormat(dxgiFormat);

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = ToD3D12ResourceDimension(desc.dimension);
        resourceDesc.Alignment = 0;
        resourceDesc.Width = desc.width;
        resourceDesc.Height = desc.height;
        resourceDesc.DepthOrArraySize = (desc.dimension == TextureDimension::Tex3D)
                                            ? static_cast<UINT16>(desc.depth)
                                            : static_cast<UINT16>(desc.arrayLayers);
        resourceDesc.MipLevels = static_cast<UINT16>(desc.mipLevels);
        resourceDesc.Format = isDepth ? ToTypelessDepthFormat(dxgiFormat) : dxgiFormat;
        resourceDesc.SampleDesc.Count = desc.sampleCount;
        resourceDesc.SampleDesc.Quality = 0;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resourceDesc.Flags = ToD3D12TextureResourceFlags(desc.usage);

        D3D12MA::ALLOCATION_DESC allocDesc{};
        allocDesc.HeapType = ToD3D12HeapType(desc.memory);
        if (desc.transient) {
            allocDesc.Flags |= D3D12MA::ALLOCATION_FLAG_CAN_ALIAS;
        }

        D3D12_CLEAR_VALUE* pClearValue = nullptr;
        D3D12_CLEAR_VALUE clearValue{};
        if (isDepth) {
            clearValue.Format = dxgiFormat;
            clearValue.DepthStencil.Depth = 1.0f;
            clearValue.DepthStencil.Stencil = 0;
            pClearValue = &clearValue;
        } else if ((static_cast<uint32_t>(desc.usage) & static_cast<uint32_t>(TextureUsage::ColorAttachment)) != 0) {
            clearValue.Format = dxgiFormat;
            clearValue.Color[0] = clearValue.Color[1] = clearValue.Color[2] = 0.0f;
            clearValue.Color[3] = 1.0f;
            pClearValue = &clearValue;
        }

        D3D12MA::Allocation* allocation = nullptr;
        ComPtr<ID3D12Resource> resource;
        HRESULT hr = allocator_->CreateResource(
            &allocDesc, &resourceDesc, D3D12_RESOURCE_STATE_COMMON, pClearValue, &allocation, IID_PPV_ARGS(&resource)
        );
        if (FAILED(hr)) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = textures_.Allocate();
        if (!data) {
            allocation->Release();
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->resource = std::move(resource);
        data->allocation = allocation;
        data->format = dxgiFormat;
        data->width = desc.width;
        data->height = desc.height;
        data->depth = desc.depth;
        data->mipLevels = desc.mipLevels;
        data->arrayLayers = desc.arrayLayers;
        data->ownsResource = true;

        if (desc.debugName) {
            wchar_t wname[256]{};
            mbstowcs(wname, desc.debugName, 255);
            data->resource->SetName(wname);
        }

        return handle;
    }

    void D3D12Device::DestroyTextureImpl(TextureHandle h) {
        auto* data = textures_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->ownsResource && data->allocation) {
            data->allocation->Release();
        }
        textures_.Free(h);
    }

    // =========================================================================
    // TextureView
    // =========================================================================

    auto D3D12Device::CreateTextureViewImpl(const TextureViewDesc& desc) -> RhiResult<TextureViewHandle> {
        auto* texData = textures_.Lookup(desc.texture);
        if (!texData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        DXGI_FORMAT viewFormat = ToDxgiFormat(desc.format);

        auto [handle, data] = textureViews_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->parentTexture = desc.texture;

        bool isDepthDsv = IsDepthFormat(viewFormat);

        // SRV
        if (desc.aspect == TextureAspect::Color || desc.aspect == TextureAspect::Depth) {
            uint32_t srvOffset = stagingCbvSrvUav_.Allocate(1);
            if (srvOffset != UINT32_MAX) {
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = isDepthDsv ? ToSrvDepthFormat(viewFormat) : viewFormat;
                srvDesc.ViewDimension = ToD3D12SrvDimension(desc.viewDimension);
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

                switch (srvDesc.ViewDimension) {
                    case D3D12_SRV_DIMENSION_TEXTURE1D:
                        srvDesc.Texture1D.MostDetailedMip = desc.baseMipLevel;
                        srvDesc.Texture1D.MipLevels = desc.mipLevelCount;
                        break;
                    case D3D12_SRV_DIMENSION_TEXTURE2D:
                        srvDesc.Texture2D.MostDetailedMip = desc.baseMipLevel;
                        srvDesc.Texture2D.MipLevels = desc.mipLevelCount;
                        break;
                    case D3D12_SRV_DIMENSION_TEXTURE3D:
                        srvDesc.Texture3D.MostDetailedMip = desc.baseMipLevel;
                        srvDesc.Texture3D.MipLevels = desc.mipLevelCount;
                        break;
                    case D3D12_SRV_DIMENSION_TEXTURECUBE:
                        srvDesc.TextureCube.MostDetailedMip = desc.baseMipLevel;
                        srvDesc.TextureCube.MipLevels = desc.mipLevelCount;
                        break;
                    case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
                        srvDesc.Texture2DArray.MostDetailedMip = desc.baseMipLevel;
                        srvDesc.Texture2DArray.MipLevels = desc.mipLevelCount;
                        srvDesc.Texture2DArray.FirstArraySlice = desc.baseArrayLayer;
                        srvDesc.Texture2DArray.ArraySize = desc.arrayLayerCount;
                        break;
                    case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
                        srvDesc.TextureCubeArray.MostDetailedMip = desc.baseMipLevel;
                        srvDesc.TextureCubeArray.MipLevels = desc.mipLevelCount;
                        srvDesc.TextureCubeArray.First2DArrayFace = desc.baseArrayLayer;
                        srvDesc.TextureCubeArray.NumCubes = desc.arrayLayerCount / 6;
                        break;
                    default: break;
                }

                data->srvHandle = stagingCbvSrvUav_.GetCpuHandle(srvOffset);
                device_->CreateShaderResourceView(texData->resource.Get(), &srvDesc, data->srvHandle);
                data->hasSrv = true;
            }
        }

        // RTV (for color attachments)
        if (!isDepthDsv && (desc.aspect == TextureAspect::Color)) {
            uint32_t rtvOffset = rtvHeap_.Allocate(1);
            if (rtvOffset != UINT32_MAX) {
                D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
                rtvDesc.Format = viewFormat;
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                rtvDesc.Texture2D.MipSlice = desc.baseMipLevel;

                data->rtvHandle = rtvHeap_.GetCpuHandle(rtvOffset);
                device_->CreateRenderTargetView(texData->resource.Get(), &rtvDesc, data->rtvHandle);
                data->hasRtv = true;
            }
        }

        // DSV (for depth/stencil)
        if (isDepthDsv) {
            uint32_t dsvOffset = dsvHeap_.Allocate(1);
            if (dsvOffset != UINT32_MAX) {
                D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
                dsvDesc.Format = viewFormat;
                dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                dsvDesc.Texture2D.MipSlice = desc.baseMipLevel;

                data->dsvHandle = dsvHeap_.GetCpuHandle(dsvOffset);
                device_->CreateDepthStencilView(texData->resource.Get(), &dsvDesc, data->dsvHandle);
                data->hasDsv = true;
            }
        }

        return handle;
    }

    void D3D12Device::DestroyTextureViewImpl(TextureViewHandle h) {
        auto* data = textureViews_.Lookup(h);
        if (!data) {
            return;
        }
        textureViews_.Free(h);
    }

    // =========================================================================
    // Sampler
    // =========================================================================

    namespace {
        auto ToD3D12Filter(Filter mag, Filter min, Filter mip) -> D3D12_FILTER {
            bool magLinear = (mag == Filter::Linear);
            bool minLinear = (min == Filter::Linear);
            bool mipLinear = (mip == Filter::Linear);

            if (!magLinear && !minLinear && !mipLinear) {
                return D3D12_FILTER_MIN_MAG_MIP_POINT;
            }
            if (!magLinear && !minLinear && mipLinear) {
                return D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
            }
            if (!magLinear && minLinear && !mipLinear) {
                return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
            }
            if (!magLinear && minLinear && mipLinear) {
                return D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
            }
            if (magLinear && !minLinear && !mipLinear) {
                return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
            }
            if (magLinear && !minLinear && mipLinear) {
                return D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR;
            }
            if (magLinear && minLinear && !mipLinear) {
                return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            }
            return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        }

        auto ToD3D12AddressMode(AddressMode m) -> D3D12_TEXTURE_ADDRESS_MODE {
            switch (m) {
                case AddressMode::Repeat: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                case AddressMode::MirroredRepeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
                case AddressMode::ClampToEdge: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                case AddressMode::ClampToBorder: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
            }
            return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        }

        auto ToD3D12ComparisonFunc(CompareOp op) -> D3D12_COMPARISON_FUNC {
            switch (op) {
                case CompareOp::Never: return D3D12_COMPARISON_FUNC_NEVER;
                case CompareOp::Less: return D3D12_COMPARISON_FUNC_LESS;
                case CompareOp::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
                case CompareOp::LessOrEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
                case CompareOp::Greater: return D3D12_COMPARISON_FUNC_GREATER;
                case CompareOp::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
                case CompareOp::GreaterOrEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
                case CompareOp::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
                case CompareOp::None: return D3D12_COMPARISON_FUNC_NEVER;
            }
            return D3D12_COMPARISON_FUNC_NEVER;
        }

        auto ToD3D12BorderColor(BorderColor c) -> void {
            (void)c;  // Border color set via float[4] in D3D12_STATIC_BORDER_COLOR
        }
    }  // namespace

    auto D3D12Device::CreateSamplerImpl(const SamplerDesc& desc) -> RhiResult<SamplerHandle> {
        D3D12_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = (desc.maxAnisotropy > 0.0f) ? D3D12_FILTER_ANISOTROPIC
                             : (desc.compareOp != CompareOp::None)
                                 ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR
                                 : ToD3D12Filter(desc.magFilter, desc.minFilter, desc.mipFilter);
        samplerDesc.AddressU = ToD3D12AddressMode(desc.addressU);
        samplerDesc.AddressV = ToD3D12AddressMode(desc.addressV);
        samplerDesc.AddressW = ToD3D12AddressMode(desc.addressW);
        samplerDesc.MipLODBias = desc.mipLodBias;
        samplerDesc.MaxAnisotropy = static_cast<UINT>(desc.maxAnisotropy);
        samplerDesc.ComparisonFunc
            = (desc.compareOp != CompareOp::None) ? ToD3D12ComparisonFunc(desc.compareOp) : D3D12_COMPARISON_FUNC_NEVER;
        samplerDesc.MinLOD = desc.minLod;
        samplerDesc.MaxLOD = desc.maxLod;

        switch (desc.borderColor) {
            case BorderColor::TransparentBlack:
                samplerDesc.BorderColor[0] = samplerDesc.BorderColor[1] = samplerDesc.BorderColor[2]
                    = samplerDesc.BorderColor[3] = 0.0f;
                break;
            case BorderColor::OpaqueBlack:
                samplerDesc.BorderColor[0] = samplerDesc.BorderColor[1] = samplerDesc.BorderColor[2] = 0.0f;
                samplerDesc.BorderColor[3] = 1.0f;
                break;
            case BorderColor::OpaqueWhite:
                samplerDesc.BorderColor[0] = samplerDesc.BorderColor[1] = samplerDesc.BorderColor[2]
                    = samplerDesc.BorderColor[3] = 1.0f;
                break;
        }

        uint32_t offset = stagingSampler_.Allocate(1);
        if (offset == UINT32_MAX) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = stagingSampler_.GetCpuHandle(offset);
        device_->CreateSampler(&samplerDesc, cpuHandle);

        auto [handle, data] = samplers_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->handle = cpuHandle;
        return handle;
    }

    void D3D12Device::DestroySamplerImpl(SamplerHandle h) {
        auto* data = samplers_.Lookup(h);
        if (!data) {
            return;
        }
        samplers_.Free(h);
    }

    // =========================================================================
    // ShaderModule
    // =========================================================================

    auto D3D12Device::CreateShaderModuleImpl(const ShaderModuleDesc& desc) -> RhiResult<ShaderModuleHandle> {
        auto [handle, data] = shaderModules_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        data->bytecode.assign(desc.code.begin(), desc.code.end());
        data->stage = desc.stage;
        return handle;
    }

    void D3D12Device::DestroyShaderModuleImpl(ShaderModuleHandle h) {
        auto* data = shaderModules_.Lookup(h);
        if (!data) {
            return;
        }
        shaderModules_.Free(h);
    }

    // =========================================================================
    // Memory aliasing (RenderGraph transient resources)
    // =========================================================================

    auto D3D12Device::CreateMemoryHeapImpl(const MemoryHeapDesc& desc) -> RhiResult<DeviceMemoryHandle> {
        D3D12_HEAP_DESC heapDesc{};
        heapDesc.SizeInBytes = desc.size;
        heapDesc.Properties.Type = ToD3D12HeapType(desc.memory);
        heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        heapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;

        ComPtr<ID3D12Heap> heap;
        HRESULT hr = device_->CreateHeap(&heapDesc, IID_PPV_ARGS(&heap));
        if (FAILED(hr)) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = deviceMemory_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->heap = std::move(heap);
        data->size = desc.size;
        return handle;
    }

    void D3D12Device::DestroyMemoryHeapImpl(DeviceMemoryHandle h) {
        auto* data = deviceMemory_.Lookup(h);
        if (!data) {
            return;
        }
        deviceMemory_.Free(h);
    }

    void D3D12Device::AliasBufferMemoryImpl(BufferHandle buf, DeviceMemoryHandle heap, uint64_t offset) {
        auto* bufData = buffers_.Lookup(buf);
        auto* memData = deviceMemory_.Lookup(heap);
        if (!bufData || !memData) {
            return;
        }

        // For D3D12, aliasing requires CreatePlacedResource — buffer must be recreated
        // This is a simplified path; production code would destroy old and create new placed resource
        D3D12_RESOURCE_DESC resourceDesc = bufData->resource->GetDesc();
        ComPtr<ID3D12Resource> placed;
        device_->CreatePlacedResource(
            memData->heap.Get(), offset, &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&placed)
        );
        if (placed) {
            bufData->resource = std::move(placed);
            bufData->gpuAddress = bufData->resource->GetGPUVirtualAddress();
        }
    }

    void D3D12Device::AliasTextureMemoryImpl(TextureHandle tex, DeviceMemoryHandle heap, uint64_t offset) {
        auto* texData = textures_.Lookup(tex);
        auto* memData = deviceMemory_.Lookup(heap);
        if (!texData || !memData) {
            return;
        }

        D3D12_RESOURCE_DESC resourceDesc = texData->resource->GetDesc();
        ComPtr<ID3D12Resource> placed;
        device_->CreatePlacedResource(
            memData->heap.Get(), offset, &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&placed)
        );
        if (placed) {
            texData->resource = std::move(placed);
        }
    }

    auto D3D12Device::GetBufferMemoryRequirementsImpl(BufferHandle h) -> MemoryRequirements {
        auto* data = buffers_.Lookup(h);
        if (!data) {
            return {};
        }

        D3D12_RESOURCE_DESC resDesc = data->resource->GetDesc();
        D3D12_RESOURCE_ALLOCATION_INFO info = device_->GetResourceAllocationInfo(0, 1, &resDesc);
        return {info.SizeInBytes, info.Alignment, UINT32_MAX};
    }

    auto D3D12Device::GetTextureMemoryRequirementsImpl(TextureHandle h) -> MemoryRequirements {
        auto* data = textures_.Lookup(h);
        if (!data) {
            return {};
        }

        D3D12_RESOURCE_DESC resDesc = data->resource->GetDesc();
        D3D12_RESOURCE_ALLOCATION_INFO info = device_->GetResourceAllocationInfo(0, 1, &resDesc);
        return {info.SizeInBytes, info.Alignment, UINT32_MAX};
    }

    // =========================================================================
    // Sparse binding (Tiled Resources)
    // =========================================================================

    auto D3D12Device::GetSparsePageSizeImpl() const -> SparsePageSize {
        return {D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES, D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES};
    }

    void D3D12Device::SubmitSparseBindsImpl(
        QueueType queue, const SparseBindDesc& binds, std::span<const SemaphoreSubmitInfo> /*wait*/,
        std::span<const SemaphoreSubmitInfo> signal
    ) {
        ID3D12CommandQueue* targetQueue = queues_.graphics.Get();
        if (queue == QueueType::Compute && queues_.compute) {
            targetQueue = queues_.compute.Get();
        } else if (queue == QueueType::Transfer && queues_.copy) {
            targetQueue = queues_.copy.Get();
        }

        // D3D12 UpdateTileMappings is queue-serialized — no wait semaphores needed
        for (auto& b : binds.bufferBinds) {
            auto* bufData = buffers_.Lookup(b.buffer);
            if (!bufData) {
                continue;
            }

            D3D12_TILED_RESOURCE_COORDINATE coord{};
            coord.X = static_cast<UINT>(b.resourceOffset / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);
            D3D12_TILE_REGION_SIZE regionSize{};
            regionSize.NumTiles = static_cast<UINT>(b.size / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);

            if (b.memory.IsValid()) {
                auto* memData = deviceMemory_.Lookup(b.memory);
                if (!memData) {
                    continue;
                }
                UINT rangeOffset = static_cast<UINT>(b.memoryOffset / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);
                D3D12_TILE_RANGE_FLAGS flag = D3D12_TILE_RANGE_FLAG_NONE;
                targetQueue->UpdateTileMappings(
                    bufData->resource.Get(), 1, &coord, &regionSize, memData->heap.Get(), 1, &flag, &rangeOffset,
                    &regionSize.NumTiles, D3D12_TILE_MAPPING_FLAG_NONE
                );
            } else {
                // Unbind (NULL mapping)
                D3D12_TILE_RANGE_FLAGS flag = D3D12_TILE_RANGE_FLAG_NULL;
                targetQueue->UpdateTileMappings(
                    bufData->resource.Get(), 1, &coord, &regionSize, nullptr, 1, &flag, nullptr, &regionSize.NumTiles,
                    D3D12_TILE_MAPPING_FLAG_NONE
                );
            }
        }

        // Signal semaphores after tile mapping updates
        for (auto& s : signal) {
            auto* semData = semaphores_.Lookup(s.semaphore);
            if (!semData) {
                continue;
            }
            targetQueue->Signal(semData->fence.Get(), s.value);
            semData->value = s.value;
        }
    }

}  // namespace miki::rhi
