/** @file D3D12AccelStruct.cpp
 *  @brief D3D12 (Tier 1) backend — BLAS/TLAS build sizes, creation, destruction.
 *
 *  Requires DXR 1.0+ (D3D12_RAYTRACING_TIER_1_0). Methods check capability at
 *  runtime and return FeatureNotSupported if RT hardware is unavailable.
 */

#include "miki/rhi/backend/D3D12Device.h"

#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wnested-anon-types"
#    pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif

#include <D3D12MemAlloc.h>

namespace miki::rhi {

    auto D3D12Device::GetBLASBuildSizesImpl(const BLASDesc& desc) -> AccelStructBuildSizes {
        if (!capabilities_.hasAccelerationStructure) {
            return {};
        }

        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
        geometries.reserve(desc.geometries.size());

        for (auto& geom : desc.geometries) {
            D3D12_RAYTRACING_GEOMETRY_DESC d3dGeom{};
            d3dGeom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            d3dGeom.Triangles.VertexBuffer.StartAddress = 0;  // Filled at build time
            d3dGeom.Triangles.VertexBuffer.StrideInBytes = geom.vertexStride;
            d3dGeom.Triangles.VertexCount = geom.vertexCount;
            d3dGeom.Triangles.IndexFormat
                = (geom.indexType == IndexType::Uint16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
            d3dGeom.Triangles.IndexCount = geom.triangleCount * 3;

            if (static_cast<uint8_t>(geom.flags) & static_cast<uint8_t>(AccelStructGeometryFlags::Opaque)) {
                d3dGeom.Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            }
            if (static_cast<uint8_t>(geom.flags) & static_cast<uint8_t>(AccelStructGeometryFlags::NoDuplicateAnyHit)) {
                d3dGeom.Flags |= D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;
            }

            geometries.push_back(d3dGeom);
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.NumDescs = static_cast<UINT>(geometries.size());
        inputs.pGeometryDescs = geometries.data();
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

        if (static_cast<uint8_t>(desc.flags) & static_cast<uint8_t>(AccelStructBuildFlags::PreferFastTrace)) {
            inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        }
        if (static_cast<uint8_t>(desc.flags) & static_cast<uint8_t>(AccelStructBuildFlags::PreferFastBuild)) {
            inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
        }
        if (static_cast<uint8_t>(desc.flags) & static_cast<uint8_t>(AccelStructBuildFlags::AllowUpdate)) {
            inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        }

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
        device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

        return {
            .accelerationStructureSize = info.ResultDataMaxSizeInBytes,
            .buildScratchSize = info.ScratchDataSizeInBytes,
            .updateScratchSize = info.UpdateScratchDataSizeInBytes
        };
    }

    auto D3D12Device::GetTLASBuildSizesImpl(const TLASDesc& desc) -> AccelStructBuildSizes {
        if (!capabilities_.hasAccelerationStructure) {
            return {};
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.NumDescs = desc.instanceCount;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

        if (static_cast<uint8_t>(desc.flags) & static_cast<uint8_t>(AccelStructBuildFlags::PreferFastTrace)) {
            inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        }
        if (static_cast<uint8_t>(desc.flags) & static_cast<uint8_t>(AccelStructBuildFlags::AllowUpdate)) {
            inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        }

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
        device_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

        return {
            .accelerationStructureSize = info.ResultDataMaxSizeInBytes,
            .buildScratchSize = info.ScratchDataSizeInBytes,
            .updateScratchSize = info.UpdateScratchDataSizeInBytes
        };
    }

    auto D3D12Device::CreateBLASImpl(const BLASDesc& desc) -> RhiResult<AccelStructHandle> {
        if (!capabilities_.hasAccelerationStructure) {
            return std::unexpected(RhiError::FeatureNotSupported);
        }

        auto sizes = GetBLASBuildSizesImpl(desc);
        if (sizes.accelerationStructureSize == 0) {
            return std::unexpected(RhiError::InvalidParameter);
        }

        D3D12MA::ALLOCATION_DESC allocDesc{};
        allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = sizes.accelerationStructureSize;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12MA::Allocation* allocation = nullptr;
        ComPtr<ID3D12Resource> resource;
        HRESULT hr = allocator_->CreateResource(
            &allocDesc, &resourceDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, &allocation,
            IID_PPV_ARGS(&resource)
        );
        if (FAILED(hr)) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = accelStructs_.Allocate();
        if (!data) {
            allocation->Release();
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->resource = std::move(resource);
        data->allocation = allocation;
        data->gpuAddress = data->resource->GetGPUVirtualAddress();
        data->isTLAS = false;
        return handle;
    }

    auto D3D12Device::CreateTLASImpl(const TLASDesc& desc) -> RhiResult<AccelStructHandle> {
        if (!capabilities_.hasAccelerationStructure) {
            return std::unexpected(RhiError::FeatureNotSupported);
        }

        auto sizes = GetTLASBuildSizesImpl(desc);
        if (sizes.accelerationStructureSize == 0) {
            return std::unexpected(RhiError::InvalidParameter);
        }

        D3D12MA::ALLOCATION_DESC allocDesc{};
        allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = sizes.accelerationStructureSize;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        D3D12MA::Allocation* allocation = nullptr;
        ComPtr<ID3D12Resource> resource;
        HRESULT hr = allocator_->CreateResource(
            &allocDesc, &resourceDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, &allocation,
            IID_PPV_ARGS(&resource)
        );
        if (FAILED(hr)) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = accelStructs_.Allocate();
        if (!data) {
            allocation->Release();
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->resource = std::move(resource);
        data->allocation = allocation;
        data->gpuAddress = data->resource->GetGPUVirtualAddress();
        data->isTLAS = true;
        return handle;
    }

    void D3D12Device::DestroyAccelStructImpl(AccelStructHandle h) {
        auto* data = accelStructs_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->allocation) {
            data->allocation->Release();
        }
        accelStructs_.Free(h);
    }

}  // namespace miki::rhi

#if defined(__clang__)
#    pragma clang diagnostic pop
#endif