/** @file WebGPUCommandBuffer.cpp
 *  @brief WebGPU / Dawn (Tier 3) backend — all Cmd* methods.
 *
 *  Commands are recorded into WGPUCommandEncoder. Render/compute passes use
 *  WGPURenderPassEncoder / WGPUComputePassEncoder. Transfer commands require
 *  ending any active pass first (WebGPU encoder model).
 */

#include "miki/rhi/backend/WebGPUCommandBuffer.h"
#include "miki/debug/StructuredLogger.h"

#include <dawn/webgpu.h>
#include <cstring>

namespace miki::rhi {

    // =========================================================================
    // Lifecycle
    // =========================================================================

    void WebGPUCommandBuffer::BeginImpl() {
        auto wgpuDevice = device_->GetWGPUDevice();
        WGPUCommandEncoderDescriptor encDesc{};
        encDesc.label = {.data = "miki_cmd_encoder", .length = WGPU_STRLEN};
        encoder_ = wgpuDeviceCreateCommandEncoder(wgpuDevice, &encDesc);
        renderPass_ = nullptr;
        computePass_ = nullptr;
        finishedCommandBuffer_ = nullptr;
        inRenderPass_ = false;
        inComputePass_ = false;
    }

    void WebGPUCommandBuffer::EndImpl() {
        EndActivePass();
        if (encoder_) {
            WGPUCommandBufferDescriptor cbDesc{};
            cbDesc.label = {.data = "miki_cmd_buf", .length = WGPU_STRLEN};
            finishedCommandBuffer_ = wgpuCommandEncoderFinish(encoder_, &cbDesc);
            wgpuCommandEncoderRelease(encoder_);
            encoder_ = nullptr;

            // Store finished buffer in pool data so SubmitImpl can access it
            auto* poolData = device_->GetCommandBufferPool().Lookup(commandBufferHandle_);
            if (poolData) {
                poolData->encoder = nullptr;
                poolData->finishedBuffer = finishedCommandBuffer_;
            }
        }
    }

    void WebGPUCommandBuffer::ResetImpl() {
        EndActivePass();
        if (finishedCommandBuffer_) {
            wgpuCommandBufferRelease(finishedCommandBuffer_);
            finishedCommandBuffer_ = nullptr;
        }
        if (encoder_) {
            wgpuCommandEncoderRelease(encoder_);
            encoder_ = nullptr;
        }
    }

    void WebGPUCommandBuffer::EndActivePass() {
        if (inRenderPass_ && renderPass_) {
            wgpuRenderPassEncoderEnd(renderPass_);
            wgpuRenderPassEncoderRelease(renderPass_);
            renderPass_ = nullptr;
            inRenderPass_ = false;
        }
        if (inComputePass_ && computePass_) {
            wgpuComputePassEncoderEnd(computePass_);
            wgpuComputePassEncoderRelease(computePass_);
            computePass_ = nullptr;
            inComputePass_ = false;
        }
    }

    // =========================================================================
    // State binding
    // =========================================================================

    void WebGPUCommandBuffer::CmdBindPipelineImpl(PipelineHandle pipeline) {
        currentPipeline_ = pipeline;
        auto* data = device_->GetPipelinePool().Lookup(pipeline);
        if (!data) {
            return;
        }

        if (data->isCompute) {
            // Auto-begin compute pass if not already in one
            if (!inComputePass_) {
                EndActivePass();  // End any active render pass
                if (encoder_) {
                    WGPUComputePassDescriptor cpDesc{};
                    computePass_ = wgpuCommandEncoderBeginComputePass(encoder_, &cpDesc);
                    inComputePass_ = (computePass_ != nullptr);
                }
            }
            if (inComputePass_ && computePass_) {
                wgpuComputePassEncoderSetPipeline(computePass_, data->computePipeline);
            }
        } else if (!data->isCompute && inRenderPass_ && renderPass_) {
            wgpuRenderPassEncoderSetPipeline(renderPass_, data->renderPipeline);
        }
    }

    void WebGPUCommandBuffer::CmdBindDescriptorSetImpl(
        uint32_t set, DescriptorSetHandle ds, [[maybe_unused]] std::span<const uint32_t> dynamicOffsets
    ) {
        auto* dsData = device_->GetDescriptorSetPool().Lookup(ds);
        if (!dsData || !dsData->bindGroup) {
            return;
        }

        // User set N maps to WebGPU group(N+1), because group(0) is reserved for push constants
        uint32_t groupIndex = set + 1;

        if (inRenderPass_ && renderPass_) {
            wgpuRenderPassEncoderSetBindGroup(renderPass_, groupIndex, dsData->bindGroup, 0, nullptr);
        } else if (inComputePass_ && computePass_) {
            wgpuComputePassEncoderSetBindGroup(computePass_, groupIndex, dsData->bindGroup, 0, nullptr);
        }
    }

    void WebGPUCommandBuffer::CmdPushConstantsImpl(
        [[maybe_unused]] ShaderStage stages, uint32_t offset, uint32_t size, const void* data
    ) {
        // Write to the push constant UBO via queue write, then bind group(0)
        auto queue = device_->GetWGPUQueue();
        auto pcBuffer = device_->GetPushConstantBuffer();
        wgpuQueueWriteBuffer(queue, pcBuffer, offset, data, size);

        // Bind the push constant bind group at group(0)
        auto pcBindGroup = device_->GetPushConstantBindGroup();
        if (inRenderPass_ && renderPass_) {
            wgpuRenderPassEncoderSetBindGroup(renderPass_, 0, pcBindGroup, 0, nullptr);
        } else if (inComputePass_ && computePass_) {
            wgpuComputePassEncoderSetBindGroup(computePass_, 0, pcBindGroup, 0, nullptr);
        }
    }

    void WebGPUCommandBuffer::CmdBindVertexBufferImpl(uint32_t binding, BufferHandle buffer, uint64_t offset) {
        if (!inRenderPass_ || !renderPass_) {
            return;
        }
        auto* bufData = device_->GetBufferPool().Lookup(buffer);
        if (!bufData) {
            return;
        }
        wgpuRenderPassEncoderSetVertexBuffer(renderPass_, binding, bufData->buffer, offset, bufData->size - offset);
    }

    void WebGPUCommandBuffer::CmdBindIndexBufferImpl(BufferHandle buffer, uint64_t offset, IndexType indexType) {
        if (!inRenderPass_ || !renderPass_) {
            return;
        }
        auto* bufData = device_->GetBufferPool().Lookup(buffer);
        if (!bufData) {
            return;
        }
        currentIndexFormat_ = (indexType == IndexType::Uint16) ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
        wgpuRenderPassEncoderSetIndexBuffer(
            renderPass_, bufData->buffer, currentIndexFormat_, offset, bufData->size - offset
        );
    }

    // =========================================================================
    // Draw
    // =========================================================================

    void WebGPUCommandBuffer::CmdDrawImpl(
        uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance
    ) {
        if (!inRenderPass_ || !renderPass_) {
            return;
        }
        wgpuRenderPassEncoderDraw(renderPass_, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void WebGPUCommandBuffer::CmdDrawIndexedImpl(
        uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance
    ) {
        if (!inRenderPass_ || !renderPass_) {
            return;
        }
        wgpuRenderPassEncoderDrawIndexed(
            renderPass_, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance
        );
    }

    void WebGPUCommandBuffer::CmdDrawIndirectImpl(
        BufferHandle buffer, uint64_t offset, uint32_t drawCount, [[maybe_unused]] uint32_t stride
    ) {
        if (!inRenderPass_ || !renderPass_) {
            return;
        }
        auto* bufData = device_->GetBufferPool().Lookup(buffer);
        if (!bufData) {
            return;
        }
        // WebGPU drawIndirect only supports 1 draw per call
        uint32_t effectiveStride = stride > 0 ? stride : 16;  // DrawIndirectArgs = 4 x uint32 = 16B
        for (uint32_t i = 0; i < drawCount; ++i) {
            wgpuRenderPassEncoderDrawIndirect(renderPass_, bufData->buffer, offset + i * effectiveStride);
        }
    }

    void WebGPUCommandBuffer::CmdDrawIndexedIndirectImpl(
        BufferHandle buffer, uint64_t offset, uint32_t drawCount, [[maybe_unused]] uint32_t stride
    ) {
        if (!inRenderPass_ || !renderPass_) {
            return;
        }
        auto* bufData = device_->GetBufferPool().Lookup(buffer);
        if (!bufData) {
            return;
        }
        uint32_t effectiveStride = stride > 0 ? stride : 20;  // DrawIndexedIndirectArgs = 5 x uint32 = 20B
        for (uint32_t i = 0; i < drawCount; ++i) {
            wgpuRenderPassEncoderDrawIndexedIndirect(renderPass_, bufData->buffer, offset + i * effectiveStride);
        }
    }

    void WebGPUCommandBuffer::CmdDrawIndexedIndirectCountImpl(
        [[maybe_unused]] BufferHandle argsBuffer, [[maybe_unused]] uint64_t argsOffset,
        [[maybe_unused]] BufferHandle countBuffer, [[maybe_unused]] uint64_t countOffset,
        [[maybe_unused]] uint32_t maxDrawCount, [[maybe_unused]] uint32_t stride
    ) {
        MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU T3: DrawIndexedIndirectCount not supported");
    }

    void WebGPUCommandBuffer::CmdDrawMeshTasksImpl(
        [[maybe_unused]] uint32_t groupCountX, [[maybe_unused]] uint32_t groupCountY,
        [[maybe_unused]] uint32_t groupCountZ
    ) {
        MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU T3: mesh shaders not supported");
    }

    void WebGPUCommandBuffer::CmdDrawMeshTasksIndirectImpl(
        [[maybe_unused]] BufferHandle buffer, [[maybe_unused]] uint64_t offset, [[maybe_unused]] uint32_t drawCount,
        [[maybe_unused]] uint32_t stride
    ) {
        MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU T3: mesh shaders not supported");
    }

    void WebGPUCommandBuffer::CmdDrawMeshTasksIndirectCountImpl(
        [[maybe_unused]] BufferHandle argsBuffer, [[maybe_unused]] uint64_t argsOffset,
        [[maybe_unused]] BufferHandle countBuffer, [[maybe_unused]] uint64_t countOffset,
        [[maybe_unused]] uint32_t maxDrawCount, [[maybe_unused]] uint32_t stride
    ) {
        MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU T3: mesh shaders not supported");
    }

    // =========================================================================
    // Compute
    // =========================================================================

    void WebGPUCommandBuffer::CmdDispatchImpl(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
        if (!inComputePass_ || !computePass_) {
            return;
        }
        wgpuComputePassEncoderDispatchWorkgroups(computePass_, groupCountX, groupCountY, groupCountZ);
    }

    void WebGPUCommandBuffer::CmdDispatchIndirectImpl(BufferHandle buffer, uint64_t offset) {
        if (!inComputePass_ || !computePass_) {
            return;
        }
        auto* bufData = device_->GetBufferPool().Lookup(buffer);
        if (!bufData) {
            return;
        }
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(computePass_, bufData->buffer, offset);
    }

    // =========================================================================
    // Transfer (require ending active render/compute pass)
    // =========================================================================

    void WebGPUCommandBuffer::CmdCopyBufferImpl(
        BufferHandle src, uint64_t srcOffset, BufferHandle dst, uint64_t dstOffset, uint64_t size
    ) {
        EndActivePass();
        if (!encoder_) {
            return;
        }
        auto* srcData = device_->GetBufferPool().Lookup(src);
        auto* dstData = device_->GetBufferPool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }
        wgpuCommandEncoderCopyBufferToBuffer(encoder_, srcData->buffer, srcOffset, dstData->buffer, dstOffset, size);
    }

    void WebGPUCommandBuffer::CmdCopyBufferToTextureImpl(
        BufferHandle src, TextureHandle dst, const BufferTextureCopyRegion& region
    ) {
        EndActivePass();
        if (!encoder_) {
            return;
        }
        auto* srcData = device_->GetBufferPool().Lookup(src);
        auto* dstData = device_->GetTexturePool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }

        WGPUTexelCopyBufferLayout srcLayout{};
        srcLayout.offset = region.bufferOffset;
        srcLayout.bytesPerRow = region.bufferRowLength;
        srcLayout.rowsPerImage = region.bufferImageHeight;

        WGPUTexelCopyBufferInfo srcInfo{};
        srcInfo.buffer = srcData->buffer;
        srcInfo.layout = srcLayout;

        WGPUTexelCopyTextureInfo dstInfo{};
        dstInfo.texture = dstData->texture;
        dstInfo.mipLevel = region.subresource.baseMipLevel;
        dstInfo.origin
            = {static_cast<uint32_t>(region.textureOffset.x), static_cast<uint32_t>(region.textureOffset.y),
               static_cast<uint32_t>(region.textureOffset.z)};
        dstInfo.aspect = WGPUTextureAspect_All;

        WGPUExtent3D copySize = {region.textureExtent.width, region.textureExtent.height, region.textureExtent.depth};
        wgpuCommandEncoderCopyBufferToTexture(encoder_, &srcInfo, &dstInfo, &copySize);
    }

    void WebGPUCommandBuffer::CmdCopyTextureToBufferImpl(
        TextureHandle src, BufferHandle dst, const BufferTextureCopyRegion& region
    ) {
        EndActivePass();
        if (!encoder_) {
            return;
        }
        auto* srcData = device_->GetTexturePool().Lookup(src);
        auto* dstData = device_->GetBufferPool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }

        WGPUTexelCopyTextureInfo srcInfo{};
        srcInfo.texture = srcData->texture;
        srcInfo.mipLevel = region.subresource.baseMipLevel;
        srcInfo.origin
            = {static_cast<uint32_t>(region.textureOffset.x), static_cast<uint32_t>(region.textureOffset.y),
               static_cast<uint32_t>(region.textureOffset.z)};
        srcInfo.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout dstLayout{};
        dstLayout.offset = region.bufferOffset;
        dstLayout.bytesPerRow = region.bufferRowLength;
        dstLayout.rowsPerImage = region.bufferImageHeight;

        WGPUTexelCopyBufferInfo dstInfo{};
        dstInfo.buffer = dstData->buffer;
        dstInfo.layout = dstLayout;

        WGPUExtent3D copySize = {region.textureExtent.width, region.textureExtent.height, region.textureExtent.depth};
        wgpuCommandEncoderCopyTextureToBuffer(encoder_, &srcInfo, &dstInfo, &copySize);
    }

    void WebGPUCommandBuffer::CmdCopyTextureImpl(
        TextureHandle src, TextureHandle dst, const TextureCopyRegion& region
    ) {
        EndActivePass();
        if (!encoder_) {
            return;
        }
        auto* srcData = device_->GetTexturePool().Lookup(src);
        auto* dstData = device_->GetTexturePool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }

        WGPUTexelCopyTextureInfo srcInfo{};
        srcInfo.texture = srcData->texture;
        srcInfo.mipLevel = region.srcSubresource.baseMipLevel;
        srcInfo.origin
            = {static_cast<uint32_t>(region.srcOffset.x), static_cast<uint32_t>(region.srcOffset.y),
               static_cast<uint32_t>(region.srcOffset.z)};
        srcInfo.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyTextureInfo dstInfo{};
        dstInfo.texture = dstData->texture;
        dstInfo.mipLevel = region.dstSubresource.baseMipLevel;
        dstInfo.origin
            = {static_cast<uint32_t>(region.dstOffset.x), static_cast<uint32_t>(region.dstOffset.y),
               static_cast<uint32_t>(region.dstOffset.z)};
        dstInfo.aspect = WGPUTextureAspect_All;

        WGPUExtent3D copySize = {region.extent.width, region.extent.height, region.extent.depth};
        wgpuCommandEncoderCopyTextureToTexture(encoder_, &srcInfo, &dstInfo, &copySize);
    }

    void WebGPUCommandBuffer::CmdBlitTextureImpl(
        [[maybe_unused]] TextureHandle src, [[maybe_unused]] TextureHandle dst,
        [[maybe_unused]] const TextureBlitRegion& region, [[maybe_unused]] Filter filter
    ) {
        // WebGPU has no native blit — requires fullscreen quad shader (deferred to higher-level)
        MIKI_LOG_WARN(
            ::miki::debug::LogCategory::Rhi,
            "WebGPU T3: CmdBlitTexture not natively supported, "
            "requires fullscreen quad shader (deferred)"
        );
    }

    void WebGPUCommandBuffer::CmdFillBufferImpl(BufferHandle buffer, uint64_t offset, uint64_t size, uint32_t value) {
        EndActivePass();
        if (!encoder_) {
            return;
        }
        auto* bufData = device_->GetBufferPool().Lookup(buffer);
        if (!bufData) {
            return;
        }

        if (value == 0) {
            // WebGPU wgpuCommandEncoderClearBuffer only supports clearing to 0
            wgpuCommandEncoderClearBuffer(encoder_, bufData->buffer, offset, size);
        } else {
            MIKI_LOG_WARN(
                ::miki::debug::LogCategory::Rhi,
                "WebGPU T3: CmdFillBuffer with non-zero value requires compute shader (deferred)"
            );
        }
    }

    void WebGPUCommandBuffer::CmdClearColorTextureImpl(
        [[maybe_unused]] TextureHandle tex, [[maybe_unused]] const ClearColor& color,
        [[maybe_unused]] const TextureSubresourceRange& range
    ) {
        // WebGPU clears are done via render pass load ops, not standalone commands
        MIKI_LOG_WARN(
            ::miki::debug::LogCategory::Rhi,
            "WebGPU T3: standalone CmdClearColorTexture not supported, use render pass loadOp::Clear"
        );
    }

    void WebGPUCommandBuffer::CmdClearDepthStencilImpl(
        [[maybe_unused]] TextureHandle tex, [[maybe_unused]] float depth, [[maybe_unused]] uint8_t stencil,
        [[maybe_unused]] const TextureSubresourceRange& range
    ) {
        MIKI_LOG_WARN(
            ::miki::debug::LogCategory::Rhi,
            "WebGPU T3: standalone CmdClearDepthStencil not supported, use render pass loadOp::Clear"
        );
    }

    // =========================================================================
    // Synchronization (implicit on WebGPU — barriers are no-ops)
    // =========================================================================

    void WebGPUCommandBuffer::CmdPipelineBarrierImpl([[maybe_unused]] const PipelineBarrierDesc& desc) {
        // WebGPU/Dawn handles resource transitions implicitly
    }

    void WebGPUCommandBuffer::CmdBufferBarrierImpl(
        [[maybe_unused]] BufferHandle buffer, [[maybe_unused]] const BufferBarrierDesc& desc
    ) {}

    void WebGPUCommandBuffer::CmdTextureBarrierImpl(
        [[maybe_unused]] TextureHandle texture, [[maybe_unused]] const TextureBarrierDesc& desc
    ) {}

    // =========================================================================
    // Dynamic rendering
    // =========================================================================

    static auto ToWGPULoadOp(AttachmentLoadOp op) -> WGPULoadOp {
        switch (op) {
            case AttachmentLoadOp::Load: return WGPULoadOp_Load;
            case AttachmentLoadOp::Clear: return WGPULoadOp_Clear;
            case AttachmentLoadOp::DontCare: return WGPULoadOp_Clear;  // WebGPU has no DontCare
            default: return WGPULoadOp_Clear;
        }
    }

    static auto ToWGPUStoreOp(AttachmentStoreOp op) -> WGPUStoreOp {
        switch (op) {
            case AttachmentStoreOp::Store: return WGPUStoreOp_Store;
            case AttachmentStoreOp::DontCare: return WGPUStoreOp_Discard;
            default: return WGPUStoreOp_Store;
        }
    }

    void WebGPUCommandBuffer::CmdBeginRenderingImpl(const RenderingDesc& desc) {
        EndActivePass();  // End any previous pass
        if (!encoder_) {
            return;
        }

        std::vector<WGPURenderPassColorAttachment> colorAttachments;
        for (const auto& att : desc.colorAttachments) {
            auto* viewData = device_->GetTextureViewPool().Lookup(att.view);
            if (!viewData) {
                continue;
            }

            WGPURenderPassColorAttachment ca{};
            ca.view = viewData->view;

            if (att.resolveView.IsValid()) {
                auto* resolveData = device_->GetTextureViewPool().Lookup(att.resolveView);
                ca.resolveTarget = resolveData ? resolveData->view : nullptr;
            }

            ca.loadOp = ToWGPULoadOp(att.loadOp);
            ca.storeOp = ToWGPUStoreOp(att.storeOp);
            ca.clearValue
                = {att.clearValue.color.r, att.clearValue.color.g, att.clearValue.color.b, att.clearValue.color.a};
            ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

            colorAttachments.push_back(ca);
        }

        WGPURenderPassDepthStencilAttachment depthStencilAtt{};
        bool hasDepthStencil = (desc.depthAttachment != nullptr);
        if (hasDepthStencil) {
            auto* viewData = device_->GetTextureViewPool().Lookup(desc.depthAttachment->view);
            if (viewData) {
                depthStencilAtt.view = viewData->view;

                // Depth load/store from the depth attachment
                depthStencilAtt.depthLoadOp = ToWGPULoadOp(desc.depthAttachment->loadOp);
                depthStencilAtt.depthStoreOp = ToWGPUStoreOp(desc.depthAttachment->storeOp);
                depthStencilAtt.depthClearValue = desc.depthAttachment->clearValue.depthStencil.depth;
                depthStencilAtt.depthReadOnly = false;

                // Adaptation: §20b Feature::DepthOnlyStencilOps → Strategy::ParameterFixup
                // For depth-only formats (D16, D32_FLOAT) that have no stencil aspect,
                // WebGPU validation requires stencilLoadOp/stencilStoreOp = Undefined
                // and stencilReadOnly = true. Zero overhead — pure parameter correction.
                bool isDepthOnly = false;
                auto* texData = device_->GetTexturePool().Lookup(viewData->parentTexture);
                if (texData) {
                    isDepthOnly
                        = (texData->format == WGPUTextureFormat_Depth16Unorm
                           || texData->format == WGPUTextureFormat_Depth32Float);
                }

                if (isDepthOnly) {
                    // ParameterFixup: force stencil ops to Undefined for depth-only formats
                    depthStencilAtt.stencilLoadOp = WGPULoadOp_Undefined;
                    depthStencilAtt.stencilStoreOp = WGPUStoreOp_Undefined;
                    depthStencilAtt.stencilClearValue = 0;
                    depthStencilAtt.stencilReadOnly = true;
                } else if (desc.stencilAttachment) {
                    depthStencilAtt.stencilLoadOp = ToWGPULoadOp(desc.stencilAttachment->loadOp);
                    depthStencilAtt.stencilStoreOp = ToWGPUStoreOp(desc.stencilAttachment->storeOp);
                    depthStencilAtt.stencilClearValue = desc.stencilAttachment->clearValue.depthStencil.stencil;
                    depthStencilAtt.stencilReadOnly = false;
                } else {
                    depthStencilAtt.stencilLoadOp = WGPULoadOp_Clear;
                    depthStencilAtt.stencilStoreOp = WGPUStoreOp_Discard;
                    depthStencilAtt.stencilClearValue = 0;
                    depthStencilAtt.stencilReadOnly = true;
                }
            } else {
                hasDepthStencil = false;
            }
        }

        WGPURenderPassDescriptor rpDesc{};
        rpDesc.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
        rpDesc.colorAttachments = colorAttachments.data();
        rpDesc.depthStencilAttachment = hasDepthStencil ? &depthStencilAtt : nullptr;

        renderPass_ = wgpuCommandEncoderBeginRenderPass(encoder_, &rpDesc);
        inRenderPass_ = (renderPass_ != nullptr);
    }

    void WebGPUCommandBuffer::CmdEndRenderingImpl() {
        if (inRenderPass_ && renderPass_) {
            wgpuRenderPassEncoderEnd(renderPass_);
            wgpuRenderPassEncoderRelease(renderPass_);
            renderPass_ = nullptr;
            inRenderPass_ = false;
        }
    }

    // =========================================================================
    // Dynamic state
    // =========================================================================

    void WebGPUCommandBuffer::CmdSetViewportImpl(const Viewport& vp) {
        if (!inRenderPass_ || !renderPass_) {
            return;
        }
        wgpuRenderPassEncoderSetViewport(renderPass_, vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth);
    }

    void WebGPUCommandBuffer::CmdSetScissorImpl(const Rect2D& scissor) {
        if (!inRenderPass_ || !renderPass_) {
            return;
        }
        wgpuRenderPassEncoderSetScissorRect(
            renderPass_, static_cast<uint32_t>(scissor.offset.x), static_cast<uint32_t>(scissor.offset.y),
            scissor.extent.width, scissor.extent.height
        );
    }

    void WebGPUCommandBuffer::CmdSetDepthBiasImpl(
        [[maybe_unused]] float constantFactor, [[maybe_unused]] float clamp, [[maybe_unused]] float slopeFactor
    ) {
        // Adaptation: §20b Feature::DynamicDepthBias → Strategy::Unsupported
        // WebGPU depth bias is set at pipeline creation time, not dynamically.
        MIKI_LOG_WARN(
            ::miki::debug::LogCategory::Rhi, "WebGPU T3: dynamic depth bias not supported (set at pipeline creation)"
        );
    }

    void WebGPUCommandBuffer::CmdSetStencilReferenceImpl(uint32_t ref) {
        if (!inRenderPass_ || !renderPass_) {
            return;
        }
        wgpuRenderPassEncoderSetStencilReference(renderPass_, ref);
    }

    void WebGPUCommandBuffer::CmdSetBlendConstantsImpl(const float constants[4]) {
        if (!inRenderPass_ || !renderPass_) {
            return;
        }
        WGPUColor color = {constants[0], constants[1], constants[2], constants[3]};
        wgpuRenderPassEncoderSetBlendConstant(renderPass_, &color);
    }

    void WebGPUCommandBuffer::CmdSetDepthBoundsImpl([[maybe_unused]] float minDepth, [[maybe_unused]] float maxDepth) {
        // WebGPU doesn't support depth bounds testing
    }

    void WebGPUCommandBuffer::CmdSetLineWidthImpl([[maybe_unused]] float width) {
        // WebGPU doesn't support dynamic line width (always 1.0)
    }

    // =========================================================================
    // VRS (not available on T3)
    // =========================================================================

    void WebGPUCommandBuffer::CmdSetShadingRateImpl(
        [[maybe_unused]] ShadingRate baseRate, [[maybe_unused]] const ShadingRateCombinerOp combinerOps[2]
    ) {
        MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU T3: VRS not supported");
    }

    void WebGPUCommandBuffer::CmdSetShadingRateImageImpl([[maybe_unused]] TextureViewHandle rateImage) {
        MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU T3: VRS image not supported");
    }

    // =========================================================================
    // Secondary command buffers (render bundles on WebGPU)
    // =========================================================================

    void WebGPUCommandBuffer::CmdExecuteSecondaryImpl(
        [[maybe_unused]] std::span<const CommandBufferHandle> secondaryBuffers
    ) {
        // WebGPU render bundles require separate API; stub for now
        MIKI_LOG_WARN(
            ::miki::debug::LogCategory::Rhi,
            "WebGPU T3: secondary command buffers (render bundles) "
            "not yet implemented"
        );
    }

    // =========================================================================
    // Query
    // =========================================================================

    void WebGPUCommandBuffer::CmdBeginQueryImpl(QueryPoolHandle pool, uint32_t index) {
        auto* poolData = device_->GetQueryPoolPool().Lookup(pool);
        if (!poolData || !poolData->querySet) {
            return;
        }

        if (poolData->type == QueryType::Occlusion) {
            if (inRenderPass_ && renderPass_) {
                wgpuRenderPassEncoderBeginOcclusionQuery(renderPass_, index);
            }
        }
        // Timestamp queries use WriteTimestamp, not Begin/End
    }

    void WebGPUCommandBuffer::CmdEndQueryImpl(QueryPoolHandle pool, uint32_t index) {
        auto* poolData = device_->GetQueryPoolPool().Lookup(pool);
        if (!poolData || !poolData->querySet) {
            return;
        }

        if (poolData->type == QueryType::Occlusion) {
            if (inRenderPass_ && renderPass_) {
                wgpuRenderPassEncoderEndOcclusionQuery(renderPass_);
            }
        }
        (void)index;
    }

    void WebGPUCommandBuffer::CmdWriteTimestampImpl(
        [[maybe_unused]] PipelineStage stage, QueryPoolHandle pool, uint32_t index
    ) {
        auto* poolData = device_->GetQueryPoolPool().Lookup(pool);
        if (!poolData || !poolData->querySet) {
            return;
        }

        // WebGPU timestamp writes are done via render/compute pass descriptors, not inline commands
        // For inline timestamps, we can use wgpuCommandEncoderWriteTimestamp (Dawn extension)
        if (encoder_) {
            wgpuCommandEncoderWriteTimestamp(encoder_, poolData->querySet, index);
        }
    }

    void WebGPUCommandBuffer::CmdResetQueryPoolImpl(
        [[maybe_unused]] QueryPoolHandle pool, [[maybe_unused]] uint32_t first, [[maybe_unused]] uint32_t count
    ) {
        // WebGPU query sets don't need explicit reset
    }

    // =========================================================================
    // Debug labels
    // =========================================================================

    void WebGPUCommandBuffer::CmdBeginDebugLabelImpl(const char* name, [[maybe_unused]] const float color[4]) {
        WGPUStringView label = {.data = name, .length = WGPU_STRLEN};
        if (inRenderPass_ && renderPass_) {
            wgpuRenderPassEncoderPushDebugGroup(renderPass_, label);
        } else if (inComputePass_ && computePass_) {
            wgpuComputePassEncoderPushDebugGroup(computePass_, label);
        } else if (encoder_) {
            wgpuCommandEncoderPushDebugGroup(encoder_, label);
        }
    }

    void WebGPUCommandBuffer::CmdEndDebugLabelImpl() {
        if (inRenderPass_ && renderPass_) {
            wgpuRenderPassEncoderPopDebugGroup(renderPass_);
        } else if (inComputePass_ && computePass_) {
            wgpuComputePassEncoderPopDebugGroup(computePass_);
        } else if (encoder_) {
            wgpuCommandEncoderPopDebugGroup(encoder_);
        }
    }

    void WebGPUCommandBuffer::CmdInsertDebugLabelImpl(const char* name, [[maybe_unused]] const float color[4]) {
        WGPUStringView label = {.data = name, .length = WGPU_STRLEN};
        if (inRenderPass_ && renderPass_) {
            wgpuRenderPassEncoderInsertDebugMarker(renderPass_, label);
        } else if (inComputePass_ && computePass_) {
            wgpuComputePassEncoderInsertDebugMarker(computePass_, label);
        } else if (encoder_) {
            wgpuCommandEncoderInsertDebugMarker(encoder_, label);
        }
    }

    // =========================================================================
    // Acceleration structure (not available on T3)
    // =========================================================================

    void WebGPUCommandBuffer::CmdBuildBLASImpl(
        [[maybe_unused]] AccelStructHandle blas, [[maybe_unused]] BufferHandle scratch
    ) {
        MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU T3: acceleration structure not supported");
    }

    void WebGPUCommandBuffer::CmdBuildTLASImpl(
        [[maybe_unused]] AccelStructHandle tlas, [[maybe_unused]] BufferHandle scratch
    ) {
        MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU T3: acceleration structure not supported");
    }

    void WebGPUCommandBuffer::CmdUpdateBLASImpl(
        [[maybe_unused]] AccelStructHandle blas, [[maybe_unused]] BufferHandle scratch
    ) {
        MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU T3: acceleration structure not supported");
    }

    // =========================================================================
    // Decompression (not available on T3)
    // =========================================================================

    void WebGPUCommandBuffer::CmdDecompressBufferImpl([[maybe_unused]] const DecompressBufferDesc& desc) {
        MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU T3: hardware decompression not supported");
    }

    // =========================================================================
    // Work Graphs (not available on T3)
    // =========================================================================

    void WebGPUCommandBuffer::CmdDispatchGraphImpl([[maybe_unused]] const DispatchGraphDesc& desc) {
        MIKI_LOG_WARN(::miki::debug::LogCategory::Rhi, "WebGPU T3: work graphs not supported");
    }

}  // namespace miki::rhi
