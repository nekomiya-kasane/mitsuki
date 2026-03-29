/** @file CommandBuffer.h
 *  @brief CRTP command buffer base with full command recording API.
 *
 *  All commands are recorded via CRTP — zero virtual dispatch in hot paths.
 *  CommandListHandle provides type-erased dispatch-once facade for RenderGraph.
 *
 *  Namespace: miki::rhi
 */
#pragma once

#include <cassert>
#include <cstdint>
#include <span>
#include <utility>

#include "miki/rhi/AccelerationStructure.h"
#include "miki/rhi/Descriptors.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/Pipeline.h"
#include "miki/rhi/Query.h"
#include "miki/rhi/Resources.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"
#include "miki/rhi/Sync.h"

namespace miki::rhi {

    // =========================================================================
    // CommandBufferBase — CRTP base for all backend command buffers
    // =========================================================================

    template <typename Impl>
    class CommandBufferBase {
       public:
        // --- Lifecycle ---
        void Begin() { self().BeginImpl(); }
        void End() { self().EndImpl(); }
        void Reset() { self().ResetImpl(); }

        // --- State Binding ---
        void CmdBindPipeline(PipelineHandle pipeline) { self().CmdBindPipelineImpl(pipeline); }

        void CmdBindDescriptorSet(uint32_t set, DescriptorSetHandle ds, std::span<const uint32_t> dynamicOffsets = {}) {
            self().CmdBindDescriptorSetImpl(set, ds, dynamicOffsets);
        }

        void CmdPushConstants(ShaderStage stages, uint32_t offset, uint32_t size, const void* data) {
            self().CmdPushConstantsImpl(stages, offset, size, data);
        }

        void CmdBindVertexBuffer(uint32_t binding, BufferHandle buffer, uint64_t offset) {
            self().CmdBindVertexBufferImpl(binding, buffer, offset);
        }

        void CmdBindIndexBuffer(BufferHandle buffer, uint64_t offset, IndexType indexType) {
            self().CmdBindIndexBufferImpl(buffer, offset, indexType);
        }

        // --- Draw ---
        void CmdDraw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
            self().CmdDrawImpl(vertexCount, instanceCount, firstVertex, firstInstance);
        }

        void CmdDrawIndexed(
            uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
            uint32_t firstInstance
        ) {
            self().CmdDrawIndexedImpl(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
        }

        void CmdDrawIndirect(BufferHandle buffer, uint64_t offset, uint32_t drawCount, uint32_t stride) {
            self().CmdDrawIndirectImpl(buffer, offset, drawCount, stride);
        }

        void CmdDrawIndexedIndirect(BufferHandle buffer, uint64_t offset, uint32_t drawCount, uint32_t stride) {
            self().CmdDrawIndexedIndirectImpl(buffer, offset, drawCount, stride);
        }

        void CmdDrawIndexedIndirectCount(
            BufferHandle argsBuffer, uint64_t argsOffset, BufferHandle countBuffer, uint64_t countOffset,
            uint32_t maxDrawCount, uint32_t stride
        ) {
            self().CmdDrawIndexedIndirectCountImpl(
                argsBuffer, argsOffset, countBuffer, countOffset, maxDrawCount, stride
            );
        }

        void CmdDrawMeshTasks(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
            self().CmdDrawMeshTasksImpl(groupCountX, groupCountY, groupCountZ);
        }

        void CmdDrawMeshTasksIndirect(BufferHandle buffer, uint64_t offset, uint32_t drawCount, uint32_t stride) {
            self().CmdDrawMeshTasksIndirectImpl(buffer, offset, drawCount, stride);
        }

        void CmdDrawMeshTasksIndirectCount(
            BufferHandle argsBuffer, uint64_t argsOffset, BufferHandle countBuffer, uint64_t countOffset,
            uint32_t maxDrawCount, uint32_t stride
        ) {
            self().CmdDrawMeshTasksIndirectCountImpl(
                argsBuffer, argsOffset, countBuffer, countOffset, maxDrawCount, stride
            );
        }

        // --- Compute ---
        void CmdDispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
            self().CmdDispatchImpl(groupCountX, groupCountY, groupCountZ);
        }

        void CmdDispatchIndirect(BufferHandle buffer, uint64_t offset) {
            self().CmdDispatchIndirectImpl(buffer, offset);
        }

        // --- Transfer ---
        void CmdCopyBuffer(BufferHandle src, uint64_t srcOffset, BufferHandle dst, uint64_t dstOffset, uint64_t size) {
            self().CmdCopyBufferImpl(src, srcOffset, dst, dstOffset, size);
        }

        void CmdCopyBufferToTexture(BufferHandle src, TextureHandle dst, const BufferTextureCopyRegion& region) {
            self().CmdCopyBufferToTextureImpl(src, dst, region);
        }

        void CmdCopyTextureToBuffer(TextureHandle src, BufferHandle dst, const BufferTextureCopyRegion& region) {
            self().CmdCopyTextureToBufferImpl(src, dst, region);
        }

        void CmdCopyTexture(TextureHandle src, TextureHandle dst, const TextureCopyRegion& region) {
            self().CmdCopyTextureImpl(src, dst, region);
        }

        void CmdBlitTexture(TextureHandle src, TextureHandle dst, const TextureBlitRegion& region, Filter filter) {
            self().CmdBlitTextureImpl(src, dst, region, filter);
        }

        void CmdFillBuffer(BufferHandle buffer, uint64_t offset, uint64_t size, uint32_t value) {
            self().CmdFillBufferImpl(buffer, offset, size, value);
        }

        void CmdClearColorTexture(TextureHandle tex, const ClearColor& color, const TextureSubresourceRange& range) {
            self().CmdClearColorTextureImpl(tex, color, range);
        }

        void CmdClearDepthStencil(
            TextureHandle tex, float depth, uint8_t stencil, const TextureSubresourceRange& range
        ) {
            self().CmdClearDepthStencilImpl(tex, depth, stencil, range);
        }

        // --- Synchronization (explicit, RenderGraph-driven) ---
        void CmdPipelineBarrier(const PipelineBarrierDesc& desc) { self().CmdPipelineBarrierImpl(desc); }
        void CmdBufferBarrier(BufferHandle buffer, const BufferBarrierDesc& desc) {
            self().CmdBufferBarrierImpl(buffer, desc);
        }
        void CmdTextureBarrier(TextureHandle texture, const TextureBarrierDesc& desc) {
            self().CmdTextureBarrierImpl(texture, desc);
        }

        // --- Dynamic Rendering ---
        void CmdBeginRendering(const RenderingDesc& desc) { self().CmdBeginRenderingImpl(desc); }
        void CmdEndRendering() { self().CmdEndRenderingImpl(); }

        // --- Dynamic State ---
        void CmdSetViewport(const Viewport& vp) { self().CmdSetViewportImpl(vp); }
        void CmdSetScissor(const Rect2D& scissor) { self().CmdSetScissorImpl(scissor); }
        void CmdSetDepthBias(float constantFactor, float clamp, float slopeFactor) {
            self().CmdSetDepthBiasImpl(constantFactor, clamp, slopeFactor);
        }
        void CmdSetStencilReference(uint32_t ref) { self().CmdSetStencilReferenceImpl(ref); }
        void CmdSetBlendConstants(const float constants[4]) { self().CmdSetBlendConstantsImpl(constants); }
        void CmdSetDepthBounds(float minDepth, float maxDepth) { self().CmdSetDepthBoundsImpl(minDepth, maxDepth); }
        void CmdSetLineWidth(float width) { self().CmdSetLineWidthImpl(width); }

        // --- Variable Rate Shading (T1 only) ---
        void CmdSetShadingRate(ShadingRate baseRate, const ShadingRateCombinerOp combinerOps[2]) {
            self().CmdSetShadingRateImpl(baseRate, combinerOps);
        }
        void CmdSetShadingRateImage(TextureViewHandle rateImage) { self().CmdSetShadingRateImageImpl(rateImage); }

        // --- Secondary Command Buffer Execution ---
        void CmdExecuteSecondary(std::span<const CommandBufferHandle> secondaryBuffers) {
            self().CmdExecuteSecondaryImpl(secondaryBuffers);
        }

        // --- Query ---
        void CmdBeginQuery(QueryPoolHandle pool, uint32_t index) { self().CmdBeginQueryImpl(pool, index); }
        void CmdEndQuery(QueryPoolHandle pool, uint32_t index) { self().CmdEndQueryImpl(pool, index); }
        void CmdWriteTimestamp(PipelineStage stage, QueryPoolHandle pool, uint32_t index) {
            self().CmdWriteTimestampImpl(stage, pool, index);
        }
        void CmdResetQueryPool(QueryPoolHandle pool, uint32_t first, uint32_t count) {
            self().CmdResetQueryPoolImpl(pool, first, count);
        }

        // --- Debug ---
        void CmdBeginDebugLabel(const char* name, const float color[4]) { self().CmdBeginDebugLabelImpl(name, color); }
        void CmdEndDebugLabel() { self().CmdEndDebugLabelImpl(); }
        void CmdInsertDebugLabel(const char* name, const float color[4]) {
            self().CmdInsertDebugLabelImpl(name, color);
        }

        // --- Acceleration Structure (T1 only) ---
        void CmdBuildBLAS(AccelStructHandle blas, BufferHandle scratch) { self().CmdBuildBLASImpl(blas, scratch); }
        void CmdBuildTLAS(AccelStructHandle tlas, BufferHandle scratch) { self().CmdBuildTLASImpl(tlas, scratch); }
        void CmdUpdateBLAS(AccelStructHandle blas, BufferHandle scratch) { self().CmdUpdateBLASImpl(blas, scratch); }

        // --- Decompression (T1 only, future) ---
        void CmdDecompressBuffer(const DecompressBufferDesc& desc) { self().CmdDecompressBufferImpl(desc); }

        // --- Work Graphs (D3D12 T1 only, future) ---
        void CmdDispatchGraph(const DispatchGraphDesc& desc) { self().CmdDispatchGraphImpl(desc); }

       private:
        [[nodiscard]] auto self() noexcept -> Impl& { return static_cast<Impl&>(*this); }
        [[nodiscard]] auto self() const noexcept -> const Impl& { return static_cast<const Impl&>(*this); }
    };

    // =========================================================================
    // CommandBufferDesc
    // =========================================================================

    struct CommandBufferDesc {
        QueueType type = QueueType::Graphics;
        bool secondary = false;
    };

    // =========================================================================
    // CommandListHandle — type-erased facade for RenderGraph
    // =========================================================================

    /// Forward declarations of concrete backend command buffers.
    /// Each backend defines its own class inheriting CommandBufferBase<Impl>.
    class VulkanCommandBuffer;
    class D3D12CommandBuffer;
    class WebGPUCommandBuffer;
    class OpenGLCommandBuffer;

    /** @brief Type-erased command buffer handle.
     *
     *  RenderGraph holds these. The pass callback calls Dispatch() ONCE to
     *  obtain the concrete CRTP type, then records all commands with zero
     *  overhead (CRTP inline).
     *
     *  Dispatch cost: O(passes/frame) ~50-100 switch cases, ~500ns total.
     */
    class CommandListHandle {
       public:
        CommandListHandle() = default;

        template <typename Impl>
        CommandListHandle(Impl* ptr, BackendType tag) : ptr_(ptr), tag_(tag) {}

        [[nodiscard]] auto IsValid() const noexcept -> bool { return ptr_ != nullptr; }

        template <typename F>
        [[nodiscard]] auto Dispatch(F&& fn) -> decltype(auto) {
            assert(ptr_ != nullptr && "CommandListHandle::Dispatch on null handle");
            switch (tag_) {
#if MIKI_BUILD_VULKAN
                case BackendType::Vulkan14: [[fallthrough]];
                case BackendType::VulkanCompat: return fn(*static_cast<VulkanCommandBuffer*>(ptr_));
#endif
#if MIKI_BUILD_D3D12
                case BackendType::D3D12: return fn(*static_cast<D3D12CommandBuffer*>(ptr_));
#endif
#if MIKI_BUILD_WEBGPU
                case BackendType::WebGPU: return fn(*static_cast<WebGPUCommandBuffer*>(ptr_));
#endif
#if MIKI_BUILD_OPENGL
                case BackendType::OpenGL43: return fn(*static_cast<OpenGLCommandBuffer*>(ptr_));
#endif
                default: break;
            }
            std::unreachable();
        }

       private:
        void* ptr_ = nullptr;
        BackendType tag_ = BackendType::Mock;
    };

}  // namespace miki::rhi
