/** @file MockCommandBuffer.h
 *  @brief Mock command buffer — all recording methods are no-ops.
 *
 *  Enables unit tests to exercise command list acquisition, render graph
 *  pass recording, and frame management without a real GPU backend.
 *
 *  Namespace: miki::rhi
 */
#pragma once

#include "miki/rhi/CommandBuffer.h"

namespace miki::rhi {

    class MockCommandBuffer : public CommandBufferBase<MockCommandBuffer> {
       public:
        // --- Lifecycle ---
        void BeginImpl() {}
        void EndImpl() {}
        void ResetImpl() {}

        // --- State Binding ---
        void CmdBindPipelineImpl(PipelineHandle) {}
        void CmdBindDescriptorSetImpl(uint32_t, DescriptorSetHandle, std::span<const uint32_t>) {}
        void CmdPushConstantsImpl(ShaderStage, uint32_t, uint32_t, const void*) {}
        void CmdBindVertexBufferImpl(uint32_t, BufferHandle, uint64_t) {}
        void CmdBindIndexBufferImpl(BufferHandle, uint64_t, IndexType) {}

        // --- Draw ---
        void CmdDrawImpl(uint32_t, uint32_t, uint32_t, uint32_t) {}
        void CmdDrawIndexedImpl(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) {}
        void CmdDrawIndirectImpl(BufferHandle, uint64_t, uint32_t, uint32_t) {}
        void CmdDrawIndexedIndirectImpl(BufferHandle, uint64_t, uint32_t, uint32_t) {}
        void CmdDrawIndexedIndirectCountImpl(BufferHandle, uint64_t, BufferHandle, uint64_t, uint32_t, uint32_t) {}
        void CmdDrawMeshTasksImpl(uint32_t, uint32_t, uint32_t) {}
        void CmdDrawMeshTasksIndirectImpl(BufferHandle, uint64_t, uint32_t, uint32_t) {}
        void CmdDrawMeshTasksIndirectCountImpl(BufferHandle, uint64_t, BufferHandle, uint64_t, uint32_t, uint32_t) {}

        // --- Compute ---
        void CmdDispatchImpl(uint32_t, uint32_t, uint32_t) {}
        void CmdDispatchIndirectImpl(BufferHandle, uint64_t) {}

        // --- Transfer ---
        void CmdCopyBufferImpl(BufferHandle, uint64_t, BufferHandle, uint64_t, uint64_t) {}
        void CmdCopyBufferToTextureImpl(BufferHandle, TextureHandle, const BufferTextureCopyRegion&) {}
        void CmdCopyTextureToBufferImpl(TextureHandle, BufferHandle, const BufferTextureCopyRegion&) {}
        void CmdCopyTextureImpl(TextureHandle, TextureHandle, const TextureCopyRegion&) {}
        void CmdBlitTextureImpl(TextureHandle, TextureHandle, const TextureBlitRegion&, Filter) {}
        void CmdFillBufferImpl(BufferHandle, uint64_t, uint64_t, uint32_t) {}
        void CmdClearColorTextureImpl(TextureHandle, const ClearColor&, const TextureSubresourceRange&) {}
        void CmdClearDepthStencilImpl(TextureHandle, float, uint8_t, const TextureSubresourceRange&) {}

        // --- Synchronization ---
        void CmdPipelineBarrierImpl(const PipelineBarrierDesc&) {}
        void CmdBufferBarrierImpl(BufferHandle, const BufferBarrierDesc&) {}
        void CmdTextureBarrierImpl(TextureHandle, const TextureBarrierDesc&) {}

        // --- Dynamic Rendering ---
        void CmdBeginRenderingImpl(const RenderingDesc&) {}
        void CmdEndRenderingImpl() {}

        // --- Dynamic State ---
        void CmdSetViewportImpl(const Viewport&) {}
        void CmdSetScissorImpl(const Rect2D&) {}
        void CmdSetDepthBiasImpl(float, float, float) {}
        void CmdSetStencilReferenceImpl(uint32_t) {}
        void CmdSetBlendConstantsImpl(const float[4]) {}
        void CmdSetDepthBoundsImpl(float, float) {}
        void CmdSetLineWidthImpl(float) {}

        // --- VRS ---
        void CmdSetShadingRateImpl(ShadingRate, const ShadingRateCombinerOp[2]) {}
        void CmdSetShadingRateImageImpl(TextureViewHandle) {}

        // --- Secondary ---
        void CmdExecuteSecondaryImpl(std::span<const CommandBufferHandle>) {}

        // --- Query ---
        void CmdBeginQueryImpl(QueryPoolHandle, uint32_t) {}
        void CmdEndQueryImpl(QueryPoolHandle, uint32_t) {}
        void CmdWriteTimestampImpl(PipelineStage, QueryPoolHandle, uint32_t) {}
        void CmdResetQueryPoolImpl(QueryPoolHandle, uint32_t, uint32_t) {}

        // --- Debug ---
        void CmdBeginDebugLabelImpl(const char*, const float[4]) {}
        void CmdEndDebugLabelImpl() {}
        void CmdInsertDebugLabelImpl(const char*, const float[4]) {}

        // --- Acceleration Structure ---
        void CmdBuildBLASImpl(AccelStructHandle, BufferHandle) {}
        void CmdBuildTLASImpl(AccelStructHandle, BufferHandle) {}
        void CmdUpdateBLASImpl(AccelStructHandle, BufferHandle) {}

        // --- Decompression ---
        void CmdDecompressBufferImpl(const DecompressBufferDesc&) {}

        // --- Work Graphs ---
        void CmdDispatchGraphImpl(const DispatchGraphDesc&) {}
    };

}  // namespace miki::rhi
