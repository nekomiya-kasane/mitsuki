/** @file D3D12CommandBuffer.h
 *  @brief D3D12 (Tier 1) backend command buffer — CRTP implementation.
 *
 *  D3D12CommandBuffer inherits CommandBufferBase<D3D12CommandBuffer> and
 *  implements all *Impl methods using direct D3D12 API calls.
 *  Uses ID3D12GraphicsCommandList7 for Enhanced Barriers + mesh shaders.
 *
 *  This header is only included when MIKI_BUILD_D3D12=1.
 *  Namespace: miki::rhi
 */
#pragma once

#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/backend/D3D12Device.h"

namespace miki::rhi {

    class D3D12CommandBuffer : public CommandBufferBase<D3D12CommandBuffer> {
       public:
        D3D12CommandBuffer() = default;

        void Init(D3D12Device* device, ID3D12GraphicsCommandList7* cmd, ID3D12CommandAllocator* alloc, QueueType qt) {
            device_ = device;
            cmd_ = cmd;
            allocator_ = alloc;
            queueType_ = qt;
        }

        [[nodiscard]] auto GetCommandList() const noexcept -> ID3D12GraphicsCommandList7* { return cmd_; }

        // --- Lifecycle ---
        void BeginImpl();
        void EndImpl();
        void ResetImpl();

        // --- State binding ---
        void CmdBindPipelineImpl(PipelineHandle pipeline);
        void CmdBindDescriptorSetImpl(uint32_t set, DescriptorSetHandle ds, std::span<const uint32_t> dynamicOffsets);
        void CmdPushConstantsImpl(ShaderStage stages, uint32_t offset, uint32_t size, const void* data);
        void CmdBindVertexBufferImpl(uint32_t binding, BufferHandle buffer, uint64_t offset);
        void CmdBindIndexBufferImpl(BufferHandle buffer, uint64_t offset, IndexType indexType);

        // --- Draw ---
        void CmdDrawImpl(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
        void CmdDrawIndexedImpl(
            uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
            uint32_t firstInstance
        );
        void CmdDrawIndirectImpl(BufferHandle buffer, uint64_t offset, uint32_t drawCount, uint32_t stride);
        void CmdDrawIndexedIndirectImpl(BufferHandle buffer, uint64_t offset, uint32_t drawCount, uint32_t stride);
        void CmdDrawIndexedIndirectCountImpl(
            BufferHandle argsBuffer, uint64_t argsOffset, BufferHandle countBuffer, uint64_t countOffset,
            uint32_t maxDrawCount, uint32_t stride
        );
        void CmdDrawMeshTasksImpl(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
        void CmdDrawMeshTasksIndirectImpl(BufferHandle buffer, uint64_t offset, uint32_t drawCount, uint32_t stride);
        void CmdDrawMeshTasksIndirectCountImpl(
            BufferHandle argsBuffer, uint64_t argsOffset, BufferHandle countBuffer, uint64_t countOffset,
            uint32_t maxDrawCount, uint32_t stride
        );

        // --- Compute ---
        void CmdDispatchImpl(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
        void CmdDispatchIndirectImpl(BufferHandle buffer, uint64_t offset);

        // --- Transfer ---
        void CmdCopyBufferImpl(
            BufferHandle src, uint64_t srcOffset, BufferHandle dst, uint64_t dstOffset, uint64_t size
        );
        void CmdCopyBufferToTextureImpl(BufferHandle src, TextureHandle dst, const BufferTextureCopyRegion& region);
        void CmdCopyTextureToBufferImpl(TextureHandle src, BufferHandle dst, const BufferTextureCopyRegion& region);
        void CmdCopyTextureImpl(TextureHandle src, TextureHandle dst, const TextureCopyRegion& region);
        void CmdBlitTextureImpl(TextureHandle src, TextureHandle dst, const TextureBlitRegion& region, Filter filter);
        void CmdFillBufferImpl(BufferHandle buffer, uint64_t offset, uint64_t size, uint32_t value);
        void CmdClearColorTextureImpl(TextureHandle tex, const ClearColor& color, const TextureSubresourceRange& range);
        void CmdClearDepthStencilImpl(
            TextureHandle tex, float depth, uint8_t stencil, const TextureSubresourceRange& range
        );

        // --- Synchronization ---
        void CmdPipelineBarrierImpl(const PipelineBarrierDesc& desc);
        void CmdBufferBarrierImpl(BufferHandle buffer, const BufferBarrierDesc& desc);
        void CmdTextureBarrierImpl(TextureHandle texture, const TextureBarrierDesc& desc);

        // --- Dynamic rendering ---
        void CmdBeginRenderingImpl(const RenderingDesc& desc);
        void CmdEndRenderingImpl();

        // --- Dynamic state ---
        void CmdSetViewportImpl(const Viewport& vp);
        void CmdSetScissorImpl(const Rect2D& scissor);
        void CmdSetDepthBiasImpl(float constantFactor, float clamp, float slopeFactor);
        void CmdSetStencilReferenceImpl(uint32_t ref);
        void CmdSetBlendConstantsImpl(const float constants[4]);
        void CmdSetDepthBoundsImpl(float minDepth, float maxDepth);
        void CmdSetLineWidthImpl(float width);

        // --- VRS ---
        void CmdSetShadingRateImpl(ShadingRate baseRate, const ShadingRateCombinerOp combinerOps[2]);
        void CmdSetShadingRateImageImpl(TextureViewHandle rateImage);

        // --- Secondary ---
        void CmdExecuteSecondaryImpl(std::span<const CommandBufferHandle> secondaryBuffers);

        // --- Query ---
        void CmdBeginQueryImpl(QueryPoolHandle pool, uint32_t index);
        void CmdEndQueryImpl(QueryPoolHandle pool, uint32_t index);
        void CmdWriteTimestampImpl(PipelineStage stage, QueryPoolHandle pool, uint32_t index);
        void CmdResetQueryPoolImpl(QueryPoolHandle pool, uint32_t first, uint32_t count);

        // --- Debug ---
        void CmdBeginDebugLabelImpl(const char* name, const float color[4]);
        void CmdEndDebugLabelImpl();
        void CmdInsertDebugLabelImpl(const char* name, const float color[4]);

        // --- Acceleration structure ---
        void CmdBuildBLASImpl(AccelStructHandle blas, BufferHandle scratch);
        void CmdBuildTLASImpl(AccelStructHandle tlas, BufferHandle scratch);
        void CmdUpdateBLASImpl(AccelStructHandle blas, BufferHandle scratch);

        // --- Decompression ---
        void CmdDecompressBufferImpl(const DecompressBufferDesc& desc);

        // --- Work Graphs (D3D12 SM 6.8+) ---
        void CmdDispatchGraphImpl(const DispatchGraphDesc& desc);

       private:
        D3D12Device* device_ = nullptr;
        ID3D12GraphicsCommandList7* cmd_ = nullptr;
        ID3D12CommandAllocator* allocator_ = nullptr;
        QueueType queueType_ = QueueType::Graphics;
        ID3D12RootSignature* currentRootSignature_ = nullptr;
        const D3D12PipelineData* currentPipeline_ = nullptr;  // For vertex stride lookup
        bool currentIsCompute_ = false;
        uint32_t currentPushConstantRootIndex_ = UINT32_MAX;
    };

}  // namespace miki::rhi
