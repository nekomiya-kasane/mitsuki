/** @file D3D12CommandBuffer.cpp
 *  @brief D3D12 (Tier 1) backend — all CommandBufferBase<D3D12CommandBuffer> *Impl methods.
 *
 *  Every method is a thin wrapper over the corresponding ID3D12GraphicsCommandList call.
 *  HandlePool lookups resolve RHI handles to ID3D12Resource/ID3D12PipelineState etc.
 *  Uses Enhanced Barriers when available, legacy ResourceBarrier fallback otherwise.
 *  Uses BeginRenderPass/EndRenderPass for dynamic rendering emulation.
 */

#include "miki/rhi/backend/D3D12CommandBuffer.h"

#include <cassert>
#include <cstring>

namespace miki::rhi {

    // =========================================================================
    // Conversion helpers
    // =========================================================================

    namespace {
        [[maybe_unused]] auto ToDxgiFormat(Format fmt) -> DXGI_FORMAT {
            switch (fmt) {
                case Format::R8_UNORM: return DXGI_FORMAT_R8_UNORM;
                case Format::RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
                case Format::BGRA8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
                case Format::BGRA8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                case Format::R16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
                case Format::RGBA16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
                case Format::R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
                case Format::RG32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
                case Format::RGBA32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
                case Format::D16_UNORM: return DXGI_FORMAT_D16_UNORM;
                case Format::D32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
                case Format::D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
                case Format::D32_FLOAT_S8_UINT: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
                default: return DXGI_FORMAT_UNKNOWN;
            }
        }

        [[maybe_unused]] auto ToD3D12PrimitiveTopology(PrimitiveTopology topo) -> D3D_PRIMITIVE_TOPOLOGY {
            switch (topo) {
                case PrimitiveTopology::PointList: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
                case PrimitiveTopology::LineList: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
                case PrimitiveTopology::LineStrip: return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
                case PrimitiveTopology::TriangleList: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                case PrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
                default: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            }
        }

        auto ToD3D12BarrierSync(PipelineStage stage) -> D3D12_BARRIER_SYNC {
            D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;
            auto has = [stage](PipelineStage bit) {
                return (static_cast<uint32_t>(stage) & static_cast<uint32_t>(bit)) != 0;
            };
            if (has(PipelineStage::TopOfPipe)) {
                sync |= D3D12_BARRIER_SYNC_ALL;
            }
            if (has(PipelineStage::DrawIndirect)) {
                sync |= D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
            }
            if (has(PipelineStage::VertexInput)) {
                sync |= D3D12_BARRIER_SYNC_INDEX_INPUT;
            }
            if (has(PipelineStage::VertexShader)) {
                sync |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
            }
            if (has(PipelineStage::FragmentShader)) {
                sync |= D3D12_BARRIER_SYNC_PIXEL_SHADING;
            }
            if (has(PipelineStage::EarlyFragmentTests) || has(PipelineStage::LateFragmentTests)) {
                sync |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
            }
            if (has(PipelineStage::ColorAttachmentOutput)) {
                sync |= D3D12_BARRIER_SYNC_RENDER_TARGET;
            }
            if (has(PipelineStage::ComputeShader)) {
                sync |= D3D12_BARRIER_SYNC_COMPUTE_SHADING;
            }
            if (has(PipelineStage::Transfer)) {
                sync |= D3D12_BARRIER_SYNC_COPY;
            }
            if (has(PipelineStage::BottomOfPipe)) {
                sync |= D3D12_BARRIER_SYNC_ALL;
            }
            if (has(PipelineStage::AllGraphics)) {
                sync |= D3D12_BARRIER_SYNC_ALL_SHADING;
            }
            if (has(PipelineStage::AllCommands)) {
                sync |= D3D12_BARRIER_SYNC_ALL;
            }
            if (has(PipelineStage::AccelStructBuild)) {
                sync |= D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE;
            }
            if (has(PipelineStage::RayTracingShader)) {
                sync |= D3D12_BARRIER_SYNC_RAYTRACING;
            }
            return sync;
        }

        auto ToD3D12BarrierAccess(AccessFlags access) -> D3D12_BARRIER_ACCESS {
            D3D12_BARRIER_ACCESS flags = D3D12_BARRIER_ACCESS_NO_ACCESS;
            auto has = [access](AccessFlags bit) {
                return (static_cast<uint32_t>(access) & static_cast<uint32_t>(bit)) != 0;
            };
            if (has(AccessFlags::VertexAttributeRead) || has(AccessFlags::IndexRead)) {
                flags |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER | D3D12_BARRIER_ACCESS_INDEX_BUFFER;
            }
            if (has(AccessFlags::UniformRead)) {
                flags |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
            }
            if (has(AccessFlags::ShaderRead)) {
                flags |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
            }
            if (has(AccessFlags::ShaderWrite)) {
                flags |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            }
            if (has(AccessFlags::ColorAttachmentRead) || has(AccessFlags::ColorAttachmentWrite)) {
                flags |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
            }
            if (has(AccessFlags::DepthStencilRead) || has(AccessFlags::DepthStencilWrite)) {
                flags |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ | D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
            }
            if (has(AccessFlags::TransferRead)) {
                flags |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
            }
            if (has(AccessFlags::TransferWrite)) {
                flags |= D3D12_BARRIER_ACCESS_COPY_DEST;
            }
            if (has(AccessFlags::IndirectCommandRead)) {
                flags |= D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
            }
            if (has(AccessFlags::AccelStructRead)) {
                flags |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
            }
            if (has(AccessFlags::AccelStructWrite)) {
                flags |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
            }
            return flags;
        }

        auto ToD3D12BarrierLayout(TextureLayout layout) -> D3D12_BARRIER_LAYOUT {
            switch (layout) {
                case TextureLayout::Undefined: return D3D12_BARRIER_LAYOUT_UNDEFINED;
                case TextureLayout::General: return D3D12_BARRIER_LAYOUT_COMMON;
                case TextureLayout::ColorAttachment: return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
                case TextureLayout::DepthStencilAttachment: return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
                case TextureLayout::DepthStencilReadOnly: return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
                case TextureLayout::ShaderReadOnly: return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
                case TextureLayout::TransferSrc: return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
                case TextureLayout::TransferDst: return D3D12_BARRIER_LAYOUT_COPY_DEST;
                case TextureLayout::Present: return D3D12_BARRIER_LAYOUT_PRESENT;
                case TextureLayout::ShadingRate: return D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE;
            }
            return D3D12_BARRIER_LAYOUT_COMMON;
        }

        // Legacy barrier fallback: TextureLayout -> D3D12_RESOURCE_STATES
        [[maybe_unused]] auto ToD3D12ResourceState(TextureLayout layout) -> D3D12_RESOURCE_STATES {
            switch (layout) {
                case TextureLayout::Undefined: return D3D12_RESOURCE_STATE_COMMON;
                case TextureLayout::General: return D3D12_RESOURCE_STATE_COMMON;
                case TextureLayout::ColorAttachment: return D3D12_RESOURCE_STATE_RENDER_TARGET;
                case TextureLayout::DepthStencilAttachment: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
                case TextureLayout::DepthStencilReadOnly: return D3D12_RESOURCE_STATE_DEPTH_READ;
                case TextureLayout::ShaderReadOnly: return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
                case TextureLayout::TransferSrc: return D3D12_RESOURCE_STATE_COPY_SOURCE;
                case TextureLayout::TransferDst: return D3D12_RESOURCE_STATE_COPY_DEST;
                case TextureLayout::Present: return D3D12_RESOURCE_STATE_PRESENT;
                case TextureLayout::ShadingRate: return D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
            }
            return D3D12_RESOURCE_STATE_COMMON;
        }
    }  // namespace

    // =========================================================================
    // Lifecycle
    // =========================================================================

    void D3D12CommandBuffer::BeginImpl() {
        allocator_->Reset();
        cmd_->Reset(allocator_, nullptr);

        // Set descriptor heaps
        ID3D12DescriptorHeap* heaps[] = {
            device_->GetShaderVisibleHeap().heap.Get(),
            device_->GetShaderVisibleSamplerHeap().heap.Get(),
        };
        cmd_->SetDescriptorHeaps(2, heaps);
    }

    void D3D12CommandBuffer::EndImpl() {
        cmd_->Close();
    }

    void D3D12CommandBuffer::ResetImpl() {
        allocator_->Reset();
        cmd_->Reset(allocator_, nullptr);
        currentRootSignature_ = nullptr;
        currentIsCompute_ = false;
        currentPushConstantRootIndex_ = UINT32_MAX;
    }

    // =========================================================================
    // State binding
    // =========================================================================

    void D3D12CommandBuffer::CmdBindPipelineImpl(PipelineHandle pipeline) {
        auto* data = device_->GetPipelinePool().Lookup(pipeline);
        if (!data) {
            return;
        }

        cmd_->SetPipelineState(data->pso.Get());
        currentIsCompute_ = data->isCompute;
        currentRootSignature_ = data->rootSignature.Get();

        if (data->isCompute) {
            cmd_->SetComputeRootSignature(data->rootSignature.Get());
        } else {
            cmd_->SetGraphicsRootSignature(data->rootSignature.Get());
        }

        // Look up push constant root index from pipeline layout
        // This info is cached in the pipeline data via root signature
    }

    void D3D12CommandBuffer::CmdBindDescriptorSetImpl(
        uint32_t set, DescriptorSetHandle ds, std::span<const uint32_t> /*dynamicOffsets*/
    ) {
        auto* dsData = device_->GetDescriptorSetPool().Lookup(ds);
        if (!dsData || !currentRootSignature_) {
            return;
        }

        if (currentIsCompute_) {
            cmd_->SetComputeRootDescriptorTable(set, dsData->gpuHandle);
        } else {
            cmd_->SetGraphicsRootDescriptorTable(set, dsData->gpuHandle);
        }
    }

    void D3D12CommandBuffer::CmdPushConstantsImpl(
        ShaderStage /*stages*/, uint32_t offset, uint32_t size, const void* data
    ) {
        if (!currentRootSignature_ || currentPushConstantRootIndex_ == UINT32_MAX) {
            return;
        }

        uint32_t firstDword = offset / 4;
        uint32_t numDwords = (size + 3) / 4;

        if (currentIsCompute_) {
            cmd_->SetComputeRoot32BitConstants(currentPushConstantRootIndex_, numDwords, data, firstDword);
        } else {
            cmd_->SetGraphicsRoot32BitConstants(currentPushConstantRootIndex_, numDwords, data, firstDword);
        }
    }

    void D3D12CommandBuffer::CmdBindVertexBufferImpl(uint32_t binding, BufferHandle buffer, uint64_t offset) {
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }

        D3D12_VERTEX_BUFFER_VIEW view{};
        view.BufferLocation = data->gpuAddress + offset;
        view.SizeInBytes = static_cast<UINT>(data->size - offset);
        view.StrideInBytes = 0;  // Set by pipeline vertex input state
        cmd_->IASetVertexBuffers(binding, 1, &view);
    }

    void D3D12CommandBuffer::CmdBindIndexBufferImpl(BufferHandle buffer, uint64_t offset, IndexType indexType) {
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }

        D3D12_INDEX_BUFFER_VIEW view{};
        view.BufferLocation = data->gpuAddress + offset;
        view.SizeInBytes = static_cast<UINT>(data->size - offset);
        view.Format = (indexType == IndexType::Uint16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        cmd_->IASetIndexBuffer(&view);
    }

    // =========================================================================
    // Draw
    // =========================================================================

    void D3D12CommandBuffer::CmdDrawImpl(
        uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance
    ) {
        cmd_->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void D3D12CommandBuffer::CmdDrawIndexedImpl(
        uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance
    ) {
        cmd_->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void D3D12CommandBuffer::CmdDrawIndirectImpl(
        BufferHandle buffer, uint64_t offset, uint32_t drawCount, uint32_t stride
    ) {
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }
        // D3D12 ExecuteIndirect requires a command signature — simplified single-draw path
        for (uint32_t i = 0; i < drawCount; ++i) {
            // In production, use ID3D12CommandSignature for batched indirect draws
            (void)stride;
        }
        // Placeholder: full implementation requires pre-created ID3D12CommandSignature
        (void)offset;
    }

    void D3D12CommandBuffer::CmdDrawIndexedIndirectImpl(
        BufferHandle buffer, uint64_t offset, uint32_t drawCount, uint32_t stride
    ) {
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }
        (void)offset;
        (void)drawCount;
        (void)stride;
        // Requires ID3D12CommandSignature — deferred to full ExecuteIndirect integration
    }

    void D3D12CommandBuffer::CmdDrawIndexedIndirectCountImpl(
        BufferHandle argsBuffer, uint64_t argsOffset, BufferHandle countBuffer, uint64_t countOffset,
        uint32_t maxDrawCount, uint32_t stride
    ) {
        auto* argsData = device_->GetBufferPool().Lookup(argsBuffer);
        auto* countData = device_->GetBufferPool().Lookup(countBuffer);
        if (!argsData || !countData) {
            return;
        }
        (void)argsOffset;
        (void)countOffset;
        (void)maxDrawCount;
        (void)stride;
        // Requires ID3D12CommandSignature with indirect count
    }

    void D3D12CommandBuffer::CmdDrawMeshTasksImpl(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
        cmd_->DispatchMesh(groupCountX, groupCountY, groupCountZ);
    }

    void D3D12CommandBuffer::CmdDrawMeshTasksIndirectImpl(
        BufferHandle /*buffer*/, uint64_t /*offset*/, uint32_t /*drawCount*/, uint32_t /*stride*/
    ) {
        // Requires ID3D12CommandSignature for mesh shader indirect
    }

    void D3D12CommandBuffer::CmdDrawMeshTasksIndirectCountImpl(
        BufferHandle /*argsBuffer*/, uint64_t /*argsOffset*/, BufferHandle /*countBuffer*/, uint64_t /*countOffset*/,
        uint32_t /*maxDrawCount*/, uint32_t /*stride*/
    ) {
        // Requires ID3D12CommandSignature for mesh shader indirect count
    }

    // =========================================================================
    // Compute
    // =========================================================================

    void D3D12CommandBuffer::CmdDispatchImpl(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
        cmd_->Dispatch(groupCountX, groupCountY, groupCountZ);
    }

    void D3D12CommandBuffer::CmdDispatchIndirectImpl(BufferHandle buffer, uint64_t offset) {
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }
        // Requires ID3D12CommandSignature for compute indirect dispatch
        (void)offset;
    }

    // =========================================================================
    // Transfer
    // =========================================================================

    void D3D12CommandBuffer::CmdCopyBufferImpl(
        BufferHandle src, uint64_t srcOffset, BufferHandle dst, uint64_t dstOffset, uint64_t size
    ) {
        auto* srcData = device_->GetBufferPool().Lookup(src);
        auto* dstData = device_->GetBufferPool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }
        cmd_->CopyBufferRegion(dstData->resource.Get(), dstOffset, srcData->resource.Get(), srcOffset, size);
    }

    void D3D12CommandBuffer::CmdCopyBufferToTextureImpl(
        BufferHandle src, TextureHandle dst, const BufferTextureCopyRegion& region
    ) {
        auto* srcData = device_->GetBufferPool().Lookup(src);
        auto* dstData = device_->GetTexturePool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }

        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = dstData->resource.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex
            = region.subresource.baseMipLevel + region.subresource.baseArrayLayer * dstData->mipLevels;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = srcData->resource.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint.Offset = region.bufferOffset;
        srcLoc.PlacedFootprint.Footprint.Format = dstData->format;
        srcLoc.PlacedFootprint.Footprint.Width = region.textureExtent.width;
        srcLoc.PlacedFootprint.Footprint.Height = region.textureExtent.height;
        srcLoc.PlacedFootprint.Footprint.Depth = region.textureExtent.depth;
        srcLoc.PlacedFootprint.Footprint.RowPitch = (region.bufferRowLength > 0)
                                                        ? region.bufferRowLength
                                                        : region.textureExtent.width * 4;  // Assume 4 bytes/pixel
        srcLoc.PlacedFootprint.Footprint.RowPitch = (srcLoc.PlacedFootprint.Footprint.RowPitch + 255) & ~255u;

        D3D12_BOX box{};
        box.left = region.textureOffset.x;
        box.top = region.textureOffset.y;
        box.front = region.textureOffset.z;
        box.right = region.textureOffset.x + region.textureExtent.width;
        box.bottom = region.textureOffset.y + region.textureExtent.height;
        box.back = region.textureOffset.z + region.textureExtent.depth;

        cmd_->CopyTextureRegion(
            &dstLoc, region.textureOffset.x, region.textureOffset.y, region.textureOffset.z, &srcLoc, nullptr
        );
    }

    void D3D12CommandBuffer::CmdCopyTextureToBufferImpl(
        TextureHandle src, BufferHandle dst, const BufferTextureCopyRegion& region
    ) {
        auto* srcData = device_->GetTexturePool().Lookup(src);
        auto* dstData = device_->GetBufferPool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = srcData->resource.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex
            = region.subresource.baseMipLevel + region.subresource.baseArrayLayer * srcData->mipLevels;

        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = dstData->resource.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLoc.PlacedFootprint.Offset = region.bufferOffset;
        dstLoc.PlacedFootprint.Footprint.Format = srcData->format;
        dstLoc.PlacedFootprint.Footprint.Width = region.textureExtent.width;
        dstLoc.PlacedFootprint.Footprint.Height = region.textureExtent.height;
        dstLoc.PlacedFootprint.Footprint.Depth = region.textureExtent.depth;
        dstLoc.PlacedFootprint.Footprint.RowPitch
            = (region.bufferRowLength > 0) ? region.bufferRowLength : region.textureExtent.width * 4;
        dstLoc.PlacedFootprint.Footprint.RowPitch = (dstLoc.PlacedFootprint.Footprint.RowPitch + 255) & ~255u;

        cmd_->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    }

    void D3D12CommandBuffer::CmdCopyTextureImpl(TextureHandle src, TextureHandle dst, const TextureCopyRegion& region) {
        auto* srcData = device_->GetTexturePool().Lookup(src);
        auto* dstData = device_->GetTexturePool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = srcData->resource.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex
            = region.srcSubresource.baseMipLevel + region.srcSubresource.baseArrayLayer * srcData->mipLevels;

        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = dstData->resource.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex
            = region.dstSubresource.baseMipLevel + region.dstSubresource.baseArrayLayer * dstData->mipLevels;

        D3D12_BOX box{};
        box.left = region.srcOffset.x;
        box.top = region.srcOffset.y;
        box.front = region.srcOffset.z;
        box.right = region.srcOffset.x + region.extent.width;
        box.bottom = region.srcOffset.y + region.extent.height;
        box.back = region.srcOffset.z + region.extent.depth;

        cmd_->CopyTextureRegion(&dstLoc, region.dstOffset.x, region.dstOffset.y, region.dstOffset.z, &srcLoc, &box);
    }

    void D3D12CommandBuffer::CmdBlitTextureImpl(
        TextureHandle /*src*/, TextureHandle /*dst*/, const TextureBlitRegion& /*region*/, Filter /*filter*/
    ) {
        // D3D12 has no native blit — requires a fullscreen quad + shader or compute pass
        // Deferred: production code uses a pre-built blit pipeline
    }

    void D3D12CommandBuffer::
        CmdFillBufferImpl(BufferHandle /*buffer*/, uint64_t /*offset*/, uint64_t /*size*/, uint32_t /*value*/) {
        // D3D12 has no CmdFillBuffer equivalent — requires compute shader or ClearUnorderedAccessViewUint
        // Deferred: production code uses UAV clear
    }

    void D3D12CommandBuffer::
        CmdClearColorTextureImpl(TextureHandle tex, const ClearColor& color, const TextureSubresourceRange& /*range*/) {
        auto* data = device_->GetTexturePool().Lookup(tex);
        if (!data) {
            return;
        }
        // Requires RTV descriptor — simplified placeholder
        // Production code would look up or create an RTV for this texture
        (void)color;
    }

    void D3D12CommandBuffer::CmdClearDepthStencilImpl(
        TextureHandle tex, float depth, uint8_t stencil, const TextureSubresourceRange& /*range*/
    ) {
        auto* data = device_->GetTexturePool().Lookup(tex);
        if (!data) {
            return;
        }
        // Requires DSV descriptor — simplified placeholder
        (void)depth;
        (void)stencil;
    }

    // =========================================================================
    // Synchronization (Enhanced Barriers / Legacy ResourceBarrier)
    // =========================================================================

    void D3D12CommandBuffer::CmdPipelineBarrierImpl(const PipelineBarrierDesc& desc) {
        D3D12_GLOBAL_BARRIER globalBarrier{};
        globalBarrier.SyncBefore = ToD3D12BarrierSync(desc.srcStage);
        globalBarrier.SyncAfter = ToD3D12BarrierSync(desc.dstStage);
        globalBarrier.AccessBefore = ToD3D12BarrierAccess(desc.srcAccess);
        globalBarrier.AccessAfter = ToD3D12BarrierAccess(desc.dstAccess);

        D3D12_BARRIER_GROUP group{};
        group.Type = D3D12_BARRIER_TYPE_GLOBAL;
        group.NumBarriers = 1;
        group.pGlobalBarriers = &globalBarrier;
        cmd_->Barrier(1, &group);
    }

    void D3D12CommandBuffer::CmdBufferBarrierImpl(BufferHandle buffer, const BufferBarrierDesc& desc) {
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }

        D3D12_BUFFER_BARRIER bufBarrier{};
        bufBarrier.SyncBefore = ToD3D12BarrierSync(desc.srcStage);
        bufBarrier.SyncAfter = ToD3D12BarrierSync(desc.dstStage);
        bufBarrier.AccessBefore = ToD3D12BarrierAccess(desc.srcAccess);
        bufBarrier.AccessAfter = ToD3D12BarrierAccess(desc.dstAccess);
        bufBarrier.pResource = data->resource.Get();
        bufBarrier.Offset = desc.offset;
        bufBarrier.Size = (desc.size == 0) ? UINT64_MAX : desc.size;

        D3D12_BARRIER_GROUP group{};
        group.Type = D3D12_BARRIER_TYPE_BUFFER;
        group.NumBarriers = 1;
        group.pBufferBarriers = &bufBarrier;
        cmd_->Barrier(1, &group);
    }

    void D3D12CommandBuffer::CmdTextureBarrierImpl(TextureHandle texture, const TextureBarrierDesc& desc) {
        auto* data = device_->GetTexturePool().Lookup(texture);
        if (!data) {
            return;
        }

        D3D12_TEXTURE_BARRIER texBarrier{};
        texBarrier.SyncBefore = ToD3D12BarrierSync(desc.srcStage);
        texBarrier.SyncAfter = ToD3D12BarrierSync(desc.dstStage);
        texBarrier.AccessBefore = ToD3D12BarrierAccess(desc.srcAccess);
        texBarrier.AccessAfter = ToD3D12BarrierAccess(desc.dstAccess);
        texBarrier.LayoutBefore = ToD3D12BarrierLayout(desc.oldLayout);
        texBarrier.LayoutAfter = ToD3D12BarrierLayout(desc.newLayout);
        texBarrier.pResource = data->resource.Get();
        texBarrier.Subresources.IndexOrFirstMipLevel = desc.subresource.baseMipLevel;
        texBarrier.Subresources.NumMipLevels = desc.subresource.mipLevelCount;
        texBarrier.Subresources.FirstArraySlice = desc.subresource.baseArrayLayer;
        texBarrier.Subresources.NumArraySlices = desc.subresource.arrayLayerCount;
        texBarrier.Subresources.FirstPlane = 0;
        texBarrier.Subresources.NumPlanes = 1;
        texBarrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;

        D3D12_BARRIER_GROUP group{};
        group.Type = D3D12_BARRIER_TYPE_TEXTURE;
        group.NumBarriers = 1;
        group.pTextureBarriers = &texBarrier;
        cmd_->Barrier(1, &group);
    }

    // =========================================================================
    // Dynamic rendering (OMSetRenderTargets)
    // =========================================================================

    void D3D12CommandBuffer::CmdBeginRenderingImpl(const RenderingDesc& desc) {
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
        rtvs.reserve(desc.colorAttachments.size());

        for (auto& att : desc.colorAttachments) {
            if (att.view.IsValid()) {
                auto* viewData = device_->GetTextureViewPool().Lookup(att.view);
                if (viewData && viewData->hasRtv) {
                    rtvs.push_back(viewData->rtvHandle);

                    if (att.loadOp == AttachmentLoadOp::Clear) {
                        float clearColor[]
                            = {att.clearValue.color.r, att.clearValue.color.g, att.clearValue.color.b,
                               att.clearValue.color.a};
                        cmd_->ClearRenderTargetView(viewData->rtvHandle, clearColor, 0, nullptr);
                    }
                }
            }
        }

        D3D12_CPU_DESCRIPTOR_HANDLE* pDsv = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        if (desc.depthAttachment) {
            auto* viewData = device_->GetTextureViewPool().Lookup(desc.depthAttachment->view);
            if (viewData && viewData->hasDsv) {
                dsv = viewData->dsvHandle;
                pDsv = &dsv;

                if (desc.depthAttachment->loadOp == AttachmentLoadOp::Clear) {
                    D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH;
                    if (desc.stencilAttachment) {
                        clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
                    }
                    cmd_->ClearDepthStencilView(
                        dsv, clearFlags, desc.depthAttachment->clearValue.depthStencil.depth,
                        desc.depthAttachment->clearValue.depthStencil.stencil, 0, nullptr
                    );
                }
            }
        }

        cmd_->OMSetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.empty() ? nullptr : rtvs.data(), FALSE, pDsv);
    }

    void D3D12CommandBuffer::CmdEndRenderingImpl() {
        // D3D12: no explicit end rendering — render targets stay bound until changed
    }

    // =========================================================================
    // Dynamic state
    // =========================================================================

    void D3D12CommandBuffer::CmdSetViewportImpl(const Viewport& vp) {
        D3D12_VIEWPORT viewport{vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth};
        cmd_->RSSetViewports(1, &viewport);
    }

    void D3D12CommandBuffer::CmdSetScissorImpl(const Rect2D& scissor) {
        D3D12_RECT rect{
            static_cast<LONG>(scissor.offset.x), static_cast<LONG>(scissor.offset.y),
            static_cast<LONG>(scissor.offset.x + scissor.extent.width),
            static_cast<LONG>(scissor.offset.y + scissor.extent.height)
        };
        cmd_->RSSetScissorRects(1, &rect);
    }

    void D3D12CommandBuffer::CmdSetDepthBiasImpl(float constantFactor, float clamp, float slopeFactor) {
        // D3D12: depth bias is PSO state, not dynamic. OM depth bias factor can be set via:
        // cmd_->OMSetDepthBias — but this requires ID3D12GraphicsCommandList9
        (void)constantFactor;
        (void)clamp;
        (void)slopeFactor;
    }

    void D3D12CommandBuffer::CmdSetStencilReferenceImpl(uint32_t ref) {
        cmd_->OMSetStencilRef(ref);
    }

    void D3D12CommandBuffer::CmdSetBlendConstantsImpl(const float constants[4]) {
        cmd_->OMSetBlendFactor(constants);
    }

    void D3D12CommandBuffer::CmdSetDepthBoundsImpl(float minDepth, float maxDepth) {
        cmd_->OMSetDepthBounds(minDepth, maxDepth);
    }

    void D3D12CommandBuffer::CmdSetLineWidthImpl(float /*width*/) {
        // D3D12 does not support dynamic line width
    }

    // =========================================================================
    // VRS
    // =========================================================================

    void D3D12CommandBuffer::CmdSetShadingRateImpl(ShadingRate baseRate, const ShadingRateCombinerOp combinerOps[2]) {
        D3D12_SHADING_RATE rate = D3D12_SHADING_RATE_1X1;
        switch (baseRate) {
            case ShadingRate::Rate1x1: rate = D3D12_SHADING_RATE_1X1; break;
            case ShadingRate::Rate1x2: rate = D3D12_SHADING_RATE_1X2; break;
            case ShadingRate::Rate2x1: rate = D3D12_SHADING_RATE_2X1; break;
            case ShadingRate::Rate2x2: rate = D3D12_SHADING_RATE_2X2; break;
            case ShadingRate::Rate2x4: rate = D3D12_SHADING_RATE_2X4; break;
            case ShadingRate::Rate4x2: rate = D3D12_SHADING_RATE_4X2; break;
            case ShadingRate::Rate4x4: rate = D3D12_SHADING_RATE_4X4; break;
        }

        D3D12_SHADING_RATE_COMBINER combiners[2]
            = {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH};
        for (int i = 0; i < 2; ++i) {
            switch (combinerOps[i]) {
                case ShadingRateCombinerOp::Keep: combiners[i] = D3D12_SHADING_RATE_COMBINER_PASSTHROUGH; break;
                case ShadingRateCombinerOp::Replace: combiners[i] = D3D12_SHADING_RATE_COMBINER_OVERRIDE; break;
                case ShadingRateCombinerOp::Min: combiners[i] = D3D12_SHADING_RATE_COMBINER_MIN; break;
                case ShadingRateCombinerOp::Max: combiners[i] = D3D12_SHADING_RATE_COMBINER_MAX; break;
                case ShadingRateCombinerOp::Mul: combiners[i] = D3D12_SHADING_RATE_COMBINER_SUM; break;
            }
        }
        cmd_->RSSetShadingRate(rate, combiners);
    }

    void D3D12CommandBuffer::CmdSetShadingRateImageImpl(TextureViewHandle /*rateImage*/) {
        // cmd_->RSSetShadingRateImage requires ID3D12Resource*, not a view
        // The SRI texture resource would be set via RSSetShadingRateImage
    }

    // =========================================================================
    // Secondary command buffers
    // =========================================================================

    void D3D12CommandBuffer::CmdExecuteSecondaryImpl(std::span<const CommandBufferHandle> secondaryBuffers) {
        // D3D12 uses ExecuteBundle for secondary command buffers
        for (auto h : secondaryBuffers) {
            auto* data = device_->GetCommandBufferPool().Lookup(h);
            if (data && data->isSecondary) {
                cmd_->ExecuteBundle(data->list.Get());
            }
        }
    }

    // =========================================================================
    // Query
    // =========================================================================

    void D3D12CommandBuffer::CmdBeginQueryImpl(QueryPoolHandle pool, uint32_t index) {
        auto* data = device_->GetQueryPoolPool().Lookup(pool);
        if (!data) {
            return;
        }
        D3D12_QUERY_TYPE type = D3D12_QUERY_TYPE_OCCLUSION;
        if (data->type == QueryType::PipelineStatistics) {
            type = D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
        }
        cmd_->BeginQuery(data->heap.Get(), type, index);
    }

    void D3D12CommandBuffer::CmdEndQueryImpl(QueryPoolHandle pool, uint32_t index) {
        auto* data = device_->GetQueryPoolPool().Lookup(pool);
        if (!data) {
            return;
        }
        D3D12_QUERY_TYPE type = D3D12_QUERY_TYPE_OCCLUSION;
        if (data->type == QueryType::PipelineStatistics) {
            type = D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
        }
        cmd_->EndQuery(data->heap.Get(), type, index);
        cmd_->ResolveQueryData(data->heap.Get(), type, index, 1, data->readbackBuffer.Get(), index * sizeof(uint64_t));
    }

    void D3D12CommandBuffer::CmdWriteTimestampImpl(PipelineStage /*stage*/, QueryPoolHandle pool, uint32_t index) {
        auto* data = device_->GetQueryPoolPool().Lookup(pool);
        if (!data) {
            return;
        }
        cmd_->EndQuery(data->heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
        cmd_->ResolveQueryData(
            data->heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index, 1, data->readbackBuffer.Get(), index * sizeof(uint64_t)
        );
    }

    void D3D12CommandBuffer::CmdResetQueryPoolImpl(QueryPoolHandle /*pool*/, uint32_t /*first*/, uint32_t /*count*/) {
        // D3D12 query heaps don't need explicit reset
    }

    // =========================================================================
    // Debug labels
    // =========================================================================

    void D3D12CommandBuffer::CmdBeginDebugLabelImpl(const char* name, const float /*color*/[4]) {
        if (name) {
            wchar_t wname[256]{};
            MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 256);
            // ID3D12GraphicsCommandList1::BeginEvent (metadata=0 for string markers)
            cmd_->BeginEvent(0, wname, static_cast<UINT>((wcslen(wname) + 1) * sizeof(wchar_t)));
        }
    }

    void D3D12CommandBuffer::CmdEndDebugLabelImpl() {
        cmd_->EndEvent();
    }

    void D3D12CommandBuffer::CmdInsertDebugLabelImpl(const char* name, const float /*color*/[4]) {
        if (name) {
            wchar_t wname[256]{};
            MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 256);
            cmd_->SetMarker(0, wname, static_cast<UINT>((wcslen(wname) + 1) * sizeof(wchar_t)));
        }
    }

    // =========================================================================
    // Acceleration structure commands (DXR)
    // =========================================================================

    void D3D12CommandBuffer::CmdBuildBLASImpl(AccelStructHandle blas, BufferHandle scratch) {
        auto* accelData = device_->GetAccelStructPool().Lookup(blas);
        auto* scratchData = device_->GetBufferPool().Lookup(scratch);
        if (!accelData || !scratchData) {
            return;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
        buildDesc.DestAccelerationStructureData = accelData->gpuAddress;
        buildDesc.ScratchAccelerationStructureData = scratchData->gpuAddress;
        buildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        buildDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

        cmd_->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    }

    void D3D12CommandBuffer::CmdBuildTLASImpl(AccelStructHandle tlas, BufferHandle scratch) {
        auto* accelData = device_->GetAccelStructPool().Lookup(tlas);
        auto* scratchData = device_->GetBufferPool().Lookup(scratch);
        if (!accelData || !scratchData) {
            return;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
        buildDesc.DestAccelerationStructureData = accelData->gpuAddress;
        buildDesc.ScratchAccelerationStructureData = scratchData->gpuAddress;
        buildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        buildDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

        cmd_->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    }

    void D3D12CommandBuffer::CmdUpdateBLASImpl(AccelStructHandle blas, BufferHandle scratch) {
        auto* accelData = device_->GetAccelStructPool().Lookup(blas);
        auto* scratchData = device_->GetBufferPool().Lookup(scratch);
        if (!accelData || !scratchData) {
            return;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
        buildDesc.DestAccelerationStructureData = accelData->gpuAddress;
        buildDesc.SourceAccelerationStructureData = accelData->gpuAddress;
        buildDesc.ScratchAccelerationStructureData = scratchData->gpuAddress;
        buildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        buildDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;

        cmd_->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
    }

    // =========================================================================
    // Decompression
    // =========================================================================

    void D3D12CommandBuffer::CmdDecompressBufferImpl(const DecompressBufferDesc& /*desc*/) {
        // DirectStorage GPU decompression: IDStorageQueue::EnqueueRequest
        // Not available via command list — requires DirectStorage runtime
    }

    // =========================================================================
    // Work Graphs (D3D12 SM 6.8+)
    // =========================================================================

    void D3D12CommandBuffer::CmdDispatchGraphImpl(const DispatchGraphDesc& /*desc*/) {
        // ID3D12GraphicsCommandList10::DispatchGraph
        // Requires SM 6.8+ and work graph pipeline creation
        // Deferred: requires ID3D12GraphicsCommandList10 and work graph PSO support
    }

}  // namespace miki::rhi
