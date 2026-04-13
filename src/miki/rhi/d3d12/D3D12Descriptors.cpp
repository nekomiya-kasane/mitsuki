/** @file D3D12Descriptors.cpp
 *  @brief D3D12 (Tier 1) backend — DescriptorLayout, PipelineLayout (Root Signature),
 *         DescriptorSet (descriptor table in shader-visible heap).
 *
 *  D3D12 descriptor model: Root Signature defines parameter layout,
 *  shader-visible descriptor heaps provide CBV/SRV/UAV and sampler views.
 *  Push constants map to root constants (64 DWORDs max).
 */

#include "miki/rhi/backend/D3D12Device.h"

#include <cassert>

#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif

namespace miki::rhi {

    // =========================================================================
    // Conversion helpers
    // =========================================================================

    namespace {
        auto ToD3D12RangeType(BindingType type) -> D3D12_DESCRIPTOR_RANGE_TYPE {
            switch (type) {
                case BindingType::UniformBuffer: return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                case BindingType::StorageBuffer: return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                case BindingType::SampledTexture: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                case BindingType::StorageTexture: return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                case BindingType::Sampler: return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                case BindingType::CombinedTextureSampler: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                case BindingType::AccelerationStructure: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                case BindingType::BindlessTextures: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                case BindingType::BindlessBuffers: return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            }
            return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        }

        [[maybe_unused]] auto IsSamplerBinding(BindingType type) -> bool {
            return type == BindingType::Sampler;
        }
    }  // namespace

    // =========================================================================
    // DescriptorLayout
    // =========================================================================

    auto D3D12Device::CreateDescriptorLayoutImpl(const DescriptorLayoutDesc& desc)
        -> RhiResult<DescriptorLayoutHandle> {
        auto [handle, data] = descriptorLayouts_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        uint32_t totalDescriptors = 0;
        data->ranges.reserve(desc.bindings.size());

        for (auto& b : desc.bindings) {
            D3D12_DESCRIPTOR_RANGE1 range{};
            range.RangeType = ToD3D12RangeType(b.type);
            range.NumDescriptors = (b.count == 0) ? UINT_MAX : b.count;  // 0 = unbounded (bindless)
            range.BaseShaderRegister = b.binding;
            range.RegisterSpace = 0;
            range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
            range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            data->ranges.push_back(range);

            if (b.count != 0) {
                totalDescriptors += b.count;
            } else {
                totalDescriptors += 1;  // Placeholder for unbounded
            }
        }

        data->totalDescriptors = totalDescriptors;
        data->bindings.reserve(desc.bindings.size());
        for (auto& b : desc.bindings) {
            data->bindings.push_back({.binding = b.binding, .type = b.type});
        }
        return handle;
    }

    void D3D12Device::DestroyDescriptorLayoutImpl(DescriptorLayoutHandle h) {
        auto* data = descriptorLayouts_.Lookup(h);
        if (!data) {
            return;
        }
        descriptorLayouts_.Free(h);
    }

    // =========================================================================
    // PipelineLayout (Root Signature)
    // =========================================================================

    auto D3D12Device::CreatePipelineLayoutImpl(const PipelineLayoutDesc& desc) -> RhiResult<PipelineLayoutHandle> {
        std::vector<D3D12_ROOT_PARAMETER1> rootParams;
        rootParams.reserve(desc.setLayouts.size() + desc.pushConstants.size());

        // Each descriptor set layout becomes a descriptor table root parameter
        for (auto& layoutHandle : desc.setLayouts) {
            auto* layoutData = descriptorLayouts_.Lookup(layoutHandle);
            if (!layoutData) {
                return std::unexpected(RhiError::InvalidHandle);
            }

            if (layoutData->ranges.empty()) {
                continue;
            }

            // Separate sampler ranges from CBV/SRV/UAV ranges
            std::vector<D3D12_DESCRIPTOR_RANGE1> srvUavCbvRanges;
            std::vector<D3D12_DESCRIPTOR_RANGE1> samplerRanges;

            for (auto& r : layoutData->ranges) {
                if (r.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER) {
                    samplerRanges.push_back(r);
                } else {
                    srvUavCbvRanges.push_back(r);
                }
            }

            if (!srvUavCbvRanges.empty()) {
                D3D12_ROOT_PARAMETER1 param{};
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(srvUavCbvRanges.size());
                // Store ranges — they are kept alive in layoutData->ranges
                param.DescriptorTable.pDescriptorRanges = layoutData->ranges.data();
                param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                rootParams.push_back(param);
            }

            if (!samplerRanges.empty()) {
                D3D12_ROOT_PARAMETER1 param{};
                param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(samplerRanges.size());
                param.DescriptorTable.pDescriptorRanges = samplerRanges.data();
                param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
                rootParams.push_back(param);
            }
        }

        // Push constants → root constants
        uint32_t pushConstantRootIndex = UINT32_MAX;
        uint32_t pushConstantTotalSize = 0;
        for (auto& pc : desc.pushConstants) {
            pushConstantRootIndex = static_cast<uint32_t>(rootParams.size());
            pushConstantTotalSize = pc.size;

            D3D12_ROOT_PARAMETER1 param{};
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            param.Constants.ShaderRegister = 0;
            param.Constants.RegisterSpace = 99;                  // Dedicated space for push constants
            param.Constants.Num32BitValues = (pc.size + 3) / 4;  // Round up to DWORDs
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            rootParams.push_back(param);
        }

        // Create root signature
        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc{};
        rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootSigDesc.Desc_1_1.NumParameters = static_cast<UINT>(rootParams.size());
        rootSigDesc.Desc_1_1.pParameters = rootParams.data();
        rootSigDesc.Desc_1_1.NumStaticSamplers = 0;
        rootSigDesc.Desc_1_1.pStaticSamplers = nullptr;
        rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
                                     | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
                                     | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> error;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &serialized, &error);
        if (FAILED(hr)) {
            return std::unexpected(RhiError::PipelineCreationFailed);
        }
        ComPtr<ID3D12RootSignature> rootSig;
        hr = device_->CreateRootSignature(
            0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&rootSig)
        );
        if (FAILED(hr)) {
            return std::unexpected(RhiError::PipelineCreationFailed);
        }

        auto [handle, data] = pipelineLayouts_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->rootSignature = std::move(rootSig);
        data->pushConstantRootIndex = pushConstantRootIndex;
        data->pushConstantSize = pushConstantTotalSize;
        return handle;
    }

    void D3D12Device::DestroyPipelineLayoutImpl(PipelineLayoutHandle h) {
        auto* data = pipelineLayouts_.Lookup(h);
        if (!data) {
            return;
        }
        pipelineLayouts_.Free(h);
    }

    // =========================================================================
    // DescriptorSet (allocate region in shader-visible descriptor heap)
    // =========================================================================

    auto D3D12Device::CreateDescriptorSetImpl(const DescriptorSetDesc& desc) -> RhiResult<DescriptorSetHandle> {
        auto* layoutData = descriptorLayouts_.Lookup(desc.layout);
        if (!layoutData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        uint32_t count = layoutData->totalDescriptors;
        uint32_t offset = shaderVisibleCbvSrvUav_.Allocate(count);
        if (offset == UINT32_MAX) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = descriptorSets_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->gpuHandle = shaderVisibleCbvSrvUav_.GetGpuHandle(offset);
        data->descriptorCount = count;
        data->heapOffset = offset;
        data->layoutHandle = desc.layout;

        if (!desc.writes.empty()) {
            UpdateDescriptorSetImpl(handle, desc.writes);
        }

        return handle;
    }

    void D3D12Device::UpdateDescriptorSetImpl(DescriptorSetHandle h, std::span<const DescriptorWrite> writes) {
        auto* setData = descriptorSets_.Lookup(h);
        if (!setData) {
            return;
        }

        auto* layoutData = descriptorLayouts_.Lookup(setData->layoutHandle);
        if (!layoutData) {
            return;
        }

        auto resolveBindingType = [&](uint32_t bindingIndex) -> BindingType {
            for (auto& info : layoutData->bindings) {
                if (info.binding == bindingIndex) {
                    return info.type;
                }
            }
            return BindingType::UniformBuffer;
        };

        for (auto& write : writes) {
            uint32_t destOffset = setData->heapOffset + write.binding + write.arrayElement;
            D3D12_CPU_DESCRIPTOR_HANDLE destCpu = shaderVisibleCbvSrvUav_.GetCpuHandle(destOffset);

            BindingType bindingType = resolveBindingType(write.binding);

            if (auto* bufBinding = std::get_if<BufferBinding>(&write.resource)) {
                auto* bufData = buffers_.Lookup(bufBinding->buffer);
                if (!bufData) {
                    continue;
                }

                if (bindingType == BindingType::StorageBuffer) {
                    // StorageBuffer → UAV (raw byte-address buffer)
                    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
                    uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                    uavDesc.Buffer.FirstElement = bufBinding->offset / 4;
                    uint64_t effectiveSize = (bufBinding->range == 0) ? bufData->size - bufBinding->offset : bufBinding->range;
                    uavDesc.Buffer.NumElements = static_cast<UINT>(effectiveSize / 4);
                    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                    device_->CreateUnorderedAccessView(bufData->resource.Get(), nullptr, &uavDesc, destCpu);
                } else {
                    // UniformBuffer → CBV
                    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
                    cbvDesc.BufferLocation = bufData->gpuAddress + bufBinding->offset;
                    cbvDesc.SizeInBytes = static_cast<UINT>(
                        (bufBinding->range == 0) ? bufData->size - bufBinding->offset : bufBinding->range
                    );
                    cbvDesc.SizeInBytes = (cbvDesc.SizeInBytes + 255) & ~255u;  // 256B alignment
                    device_->CreateConstantBufferView(&cbvDesc, destCpu);
                }

            } else if (auto* texBinding = std::get_if<TextureBinding>(&write.resource)) {
                if (texBinding->view.IsValid()) {
                    auto* viewData = textureViews_.Lookup(texBinding->view);
                    if (viewData && viewData->hasSrv) {
                        device_->CopyDescriptorsSimple(
                            1, destCpu, viewData->srvHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
                        );
                    }
                }
            } else if (auto* sampHandle = std::get_if<SamplerHandle>(&write.resource)) {
                auto* sampData = samplers_.Lookup(*sampHandle);
                if (sampData) {
                    // Samplers go into the sampler heap — separate path
                    // For simplicity, copy to shader-visible sampler heap
                    uint32_t sampOffset = shaderVisibleSampler_.Allocate(1);
                    if (sampOffset != UINT32_MAX) {
                        D3D12_CPU_DESCRIPTOR_HANDLE sampDest = shaderVisibleSampler_.GetCpuHandle(sampOffset);
                        device_->CopyDescriptorsSimple(
                            1, sampDest, sampData->handle, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
                        );
                    }
                }
            } else if (auto* accelHandle = std::get_if<AccelStructHandle>(&write.resource)) {
                auto* accelData = accelStructs_.Lookup(*accelHandle);
                if (accelData) {
                    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
                    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srvDesc.RaytracingAccelerationStructure.Location = accelData->gpuAddress;
                    device_->CreateShaderResourceView(nullptr, &srvDesc, destCpu);
                }
            }
        }
    }

    void D3D12Device::DestroyDescriptorSetImpl(DescriptorSetHandle h) {
        auto* data = descriptorSets_.Lookup(h);
        if (!data) {
            return;
        }
        // Descriptor heap space is not individually freed (linear allocator)
        // It is reclaimed when the heap is reset or device is destroyed
        descriptorSets_.Free(h);
    }

}  // namespace miki::rhi

#if defined(__clang__)
#    pragma clang diagnostic pop
#endif