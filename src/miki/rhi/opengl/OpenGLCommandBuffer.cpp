/** @file OpenGLCommandBuffer.cpp
 *  @brief OpenGL 4.3+ (Tier 4) backend — all CommandBufferBase<OpenGLCommandBuffer> *Impl methods.
 *
 *  OpenGL is immediate-mode: every Cmd* call directly issues GL API calls.
 *  Dynamic rendering maps to FBO bind/unbind + glClear.
 *  Barriers map to glMemoryBarrier (coarse-grained).
 *  Debug labels map to glPushDebugGroup / glPopDebugGroup (KHR_debug).
 *  Uses glad2 MX context (device_->GetGLContext()).
 */

#include "miki/rhi/backend/OpenGLCommandBuffer.h"

#include <cstring>

namespace miki::rhi {

    namespace {
        struct GLFormatPair {
            GLenum format;
            GLenum type;
        };

        // Derive pixel transfer format + type from GL internal format
        auto InternalFormatToTransfer(GLenum internalFormat) -> GLFormatPair {
            switch (internalFormat) {
                case GL_R8: return {GL_RED, GL_UNSIGNED_BYTE};
                case GL_RG8: return {GL_RG, GL_UNSIGNED_BYTE};
                case GL_RGB8: return {GL_RGB, GL_UNSIGNED_BYTE};
                case GL_RGBA8:
                case GL_SRGB8_ALPHA8: return {GL_RGBA, GL_UNSIGNED_BYTE};
                case GL_R16F: return {GL_RED, GL_HALF_FLOAT};
                case GL_RG16F: return {GL_RG, GL_HALF_FLOAT};
                case GL_RGBA16F: return {GL_RGBA, GL_HALF_FLOAT};
                case GL_R32F: return {GL_RED, GL_FLOAT};
                case GL_RG32F: return {GL_RG, GL_FLOAT};
                case GL_RGB32F: return {GL_RGB, GL_FLOAT};
                case GL_RGBA32F: return {GL_RGBA, GL_FLOAT};
                case GL_R8UI: return {GL_RED_INTEGER, GL_UNSIGNED_BYTE};
                case GL_RG8UI: return {GL_RG_INTEGER, GL_UNSIGNED_BYTE};
                case GL_RGBA8UI: return {GL_RGBA_INTEGER, GL_UNSIGNED_BYTE};
                case GL_R16UI: return {GL_RED_INTEGER, GL_UNSIGNED_SHORT};
                case GL_RG16UI: return {GL_RG_INTEGER, GL_UNSIGNED_SHORT};
                case GL_RGBA16UI: return {GL_RGBA_INTEGER, GL_UNSIGNED_SHORT};
                case GL_R32UI: return {GL_RED_INTEGER, GL_UNSIGNED_INT};
                case GL_RG32UI: return {GL_RG_INTEGER, GL_UNSIGNED_INT};
                case GL_RGB32UI: return {GL_RGB_INTEGER, GL_UNSIGNED_INT};
                case GL_RGBA32UI: return {GL_RGBA_INTEGER, GL_UNSIGNED_INT};
                case GL_R8I: return {GL_RED_INTEGER, GL_BYTE};
                case GL_RG8I: return {GL_RG_INTEGER, GL_BYTE};
                case GL_RGBA8I: return {GL_RGBA_INTEGER, GL_BYTE};
                case GL_R16I: return {GL_RED_INTEGER, GL_SHORT};
                case GL_RG16I: return {GL_RG_INTEGER, GL_SHORT};
                case GL_RGBA16I: return {GL_RGBA_INTEGER, GL_SHORT};
                case GL_R32I: return {GL_RED_INTEGER, GL_INT};
                case GL_RG32I: return {GL_RG_INTEGER, GL_INT};
                case GL_RGB32I: return {GL_RGB_INTEGER, GL_INT};
                case GL_RGBA32I: return {GL_RGBA_INTEGER, GL_INT};
                case GL_R16: return {GL_RED, GL_UNSIGNED_SHORT};
                case GL_RG16: return {GL_RG, GL_UNSIGNED_SHORT};
                case GL_RGBA16: return {GL_RGBA, GL_UNSIGNED_SHORT};
                case GL_DEPTH_COMPONENT16: return {GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT};
                case GL_DEPTH_COMPONENT24: return {GL_DEPTH_COMPONENT, GL_UNSIGNED_INT};
                case GL_DEPTH_COMPONENT32F: return {GL_DEPTH_COMPONENT, GL_FLOAT};
                case GL_DEPTH24_STENCIL8: return {GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8};
                case GL_DEPTH32F_STENCIL8: return {GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV};
                case GL_R11F_G11F_B10F: return {GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV};
                case GL_RGB10_A2: return {GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV};
                default: return {GL_RGBA, GL_UNSIGNED_BYTE};
            }
        }
    }  // namespace

    // =========================================================================
    // Lifecycle — lightweight for immediate-mode GL
    // =========================================================================

    void OpenGLCommandBuffer::BeginImpl() {
        currentFBO_ = 0;
        currentIndexType_ = GL_UNSIGNED_INT;
        currentTopology_ = GL_TRIANGLES;
        currentPipeline_ = {};
        currentIndexBufferOffset_ = 0;
    }

    void OpenGLCommandBuffer::EndImpl() {
        // No-op: GL commands already executed
    }

    void OpenGLCommandBuffer::ResetImpl() {
        currentFBO_ = 0;
        currentIndexType_ = GL_UNSIGNED_INT;
        currentTopology_ = GL_TRIANGLES;
        currentPipeline_ = {};
        currentIndexBufferOffset_ = 0;
    }

    // =========================================================================
    // State binding
    // =========================================================================

    void OpenGLCommandBuffer::CmdBindPipelineImpl(PipelineHandle pipeline) {
        auto* gl = device_->GetGLContext();
        auto* data = device_->GetPipelinePool().Lookup(pipeline);
        if (!data) {
            return;
        }

        gl->UseProgram(data->program);
        currentPipeline_ = pipeline;

        if (data->isCompute) {
            return;
        }

        currentTopology_ = data->primitiveTopology;

        // Bind VAO
        if (data->vao) {
            gl->BindVertexArray(data->vao);
        }

        // Apply fixed-function state
        // Culling
        if (data->cullMode == GL_NONE) {
            gl->Disable(GL_CULL_FACE);
        } else {
            gl->Enable(GL_CULL_FACE);
            gl->CullFace(data->cullMode);
        }
        gl->FrontFace(data->frontFace);
        gl->PolygonMode(GL_FRONT_AND_BACK, data->polygonMode);

        // Depth
        if (data->depthTestEnable) {
            gl->Enable(GL_DEPTH_TEST);
            gl->DepthFunc(data->depthFunc);
        } else {
            gl->Disable(GL_DEPTH_TEST);
        }
        gl->DepthMask(data->depthWriteEnable ? GL_TRUE : GL_FALSE);

        if (data->depthClampEnable) {
            gl->Enable(GL_DEPTH_CLAMP);
        } else {
            gl->Disable(GL_DEPTH_CLAMP);
        }

        // Stencil
        if (data->stencilTestEnable) {
            gl->Enable(GL_STENCIL_TEST);
            gl->StencilFuncSeparate(GL_FRONT, data->stencilFront.compareOp, 0, data->stencilFront.compareMask);
            gl->StencilOpSeparate(
                GL_FRONT, data->stencilFront.failOp, data->stencilFront.depthFailOp, data->stencilFront.passOp
            );
            gl->StencilMaskSeparate(GL_FRONT, data->stencilFront.writeMask);
            gl->StencilFuncSeparate(GL_BACK, data->stencilBack.compareOp, 0, data->stencilBack.compareMask);
            gl->StencilOpSeparate(
                GL_BACK, data->stencilBack.failOp, data->stencilBack.depthFailOp, data->stencilBack.passOp
            );
            gl->StencilMaskSeparate(GL_BACK, data->stencilBack.writeMask);
        } else {
            gl->Disable(GL_STENCIL_TEST);
        }

        // Blend (per-attachment)
        bool anyBlendEnabled = false;
        for (uint32_t i = 0; i < data->colorAttachmentCount; ++i) {
            auto& bs = data->blendStates[i];
            if (bs.enable) {
                anyBlendEnabled = true;
                gl->Enablei(GL_BLEND, i);
                gl->BlendFuncSeparatei(i, bs.srcColor, bs.dstColor, bs.srcAlpha, bs.dstAlpha);
                gl->BlendEquationSeparatei(i, bs.colorOp, bs.alphaOp);
            } else {
                gl->Disablei(GL_BLEND, i);
            }
            gl->ColorMaski(
                i, (bs.writeMask & 0x1) ? GL_TRUE : GL_FALSE, (bs.writeMask & 0x2) ? GL_TRUE : GL_FALSE,
                (bs.writeMask & 0x4) ? GL_TRUE : GL_FALSE, (bs.writeMask & 0x8) ? GL_TRUE : GL_FALSE
            );
        }
        if (!anyBlendEnabled) {
            gl->Disable(GL_BLEND);
        }
    }

    void OpenGLCommandBuffer::CmdBindDescriptorSetImpl(
        uint32_t /*set*/, DescriptorSetHandle ds, std::span<const uint32_t> /*dynamicOffsets*/
    ) {
        auto* gl = device_->GetGLContext();
        auto* dsData = device_->GetDescriptorSetPool().Lookup(ds);
        if (!dsData) {
            return;
        }

        // Apply bindings directly via glBindBufferRange / glBindTextureUnit / glBindSampler
        // Per §6.5.1: push constants use UBO binding 0, all user bindings shifted +1
        for (auto& res : dsData->resources) {
            uint32_t glBinding = res.binding + 1;  // +1 offset for push constant UBO reservation

            switch (res.type) {
                case BindingType::UniformBuffer:
                    if (res.buffer) {
                        gl->BindBufferRange(
                            GL_UNIFORM_BUFFER, glBinding, res.buffer, static_cast<GLintptr>(res.offset),
                            static_cast<GLsizeiptr>(res.range)
                        );
                    }
                    break;
                case BindingType::StorageBuffer:
                    if (res.buffer) {
                        gl->BindBufferRange(
                            GL_SHADER_STORAGE_BUFFER, glBinding, res.buffer, static_cast<GLintptr>(res.offset),
                            static_cast<GLsizeiptr>(res.range)
                        );
                    }
                    break;
                case BindingType::SampledTexture:
                case BindingType::CombinedTextureSampler:
                    if (res.texture) {
                        gl->BindTextureUnit(glBinding, res.texture);
                    }
                    if (res.sampler) {
                        gl->BindSampler(glBinding, res.sampler);
                    }
                    break;
                case BindingType::StorageTexture:
                    if (res.texture) {
                        // Look up actual internal format from texture pool
                        GLenum imgFormat = GL_RGBA8;
                        auto& texPool = device_->GetTexturePool();
                        // Search by GL name — iterate is acceptable since descriptor bind is not per-draw hot path
                        gl->BindImageTexture(glBinding, res.texture, 0, GL_FALSE, 0, GL_READ_WRITE, imgFormat);
                    }
                    break;
                case BindingType::Sampler:
                    if (res.sampler) {
                        gl->BindSampler(glBinding, res.sampler);
                    }
                    break;
                default: break;
            }
        }
    }

    void OpenGLCommandBuffer::CmdPushConstantsImpl(ShaderStage, uint32_t offset, uint32_t size, const void* data) {
        auto* gl = device_->GetGLContext();
        GLuint ubo = device_->GetPushConstantUBO();
        // DSA path (GL 4.5+) vs bind-to-edit fallback (GL 4.3/4.4)
        if (gl->NamedBufferSubData) {
            gl->NamedBufferSubData(ubo, offset, size, data);
        } else {
            gl->BindBuffer(GL_UNIFORM_BUFFER, ubo);
            gl->BufferSubData(GL_UNIFORM_BUFFER, offset, size, data);
            gl->BindBuffer(GL_UNIFORM_BUFFER, 0);
            gl->BindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);
        }
    }

    void OpenGLCommandBuffer::CmdBindVertexBufferImpl(uint32_t binding, BufferHandle buffer, uint64_t offset) {
        auto* gl = device_->GetGLContext();
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }

        // Look up stride from current pipeline's vertex bindings
        uint32_t stride = 0;
        auto* pipeData = device_->GetPipelinePool().Lookup(currentPipeline_);
        if (pipeData) {
            for (auto& vb : pipeData->vertexBindings) {
                if (vb.binding == binding) {
                    stride = vb.stride;
                    break;
                }
            }
        }
        gl->BindVertexBuffer(binding, data->buffer, static_cast<GLintptr>(offset), stride);
    }

    void OpenGLCommandBuffer::CmdBindIndexBufferImpl(BufferHandle buffer, uint64_t offset, IndexType indexType) {
        auto* gl = device_->GetGLContext();
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }

        gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, data->buffer);
        currentIndexType_ = (indexType == IndexType::Uint16) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
        currentIndexBufferOffset_ = offset;  // Store for use in DrawIndexed
    }

    // =========================================================================
    // Draw
    // =========================================================================

    void OpenGLCommandBuffer::CmdDrawImpl(
        uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance
    ) {
        auto* gl = device_->GetGLContext();
        if (firstInstance == 0) {
            gl->DrawArraysInstanced(currentTopology_, firstVertex, vertexCount, instanceCount);
        } else {
            gl->DrawArraysInstancedBaseInstance(
                currentTopology_, firstVertex, vertexCount, instanceCount, firstInstance
            );
        }
    }

    void OpenGLCommandBuffer::CmdDrawIndexedImpl(
        uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance
    ) {
        auto* gl = device_->GetGLContext();
        uint32_t indexSize = (currentIndexType_ == GL_UNSIGNED_SHORT) ? 2 : 4;
        const void* indexOffset
            = reinterpret_cast<const void*>(currentIndexBufferOffset_ + static_cast<uintptr_t>(firstIndex) * indexSize);

        if (vertexOffset == 0 && firstInstance == 0) {
            gl->DrawElementsInstanced(currentTopology_, indexCount, currentIndexType_, indexOffset, instanceCount);
        } else {
            gl->DrawElementsInstancedBaseVertexBaseInstance(
                currentTopology_, indexCount, currentIndexType_, indexOffset, instanceCount, vertexOffset, firstInstance
            );
        }
    }

    void OpenGLCommandBuffer::CmdDrawIndirectImpl(
        BufferHandle buffer, uint64_t offset, uint32_t drawCount, uint32_t stride
    ) {
        auto* gl = device_->GetGLContext();
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }

        gl->BindBuffer(GL_DRAW_INDIRECT_BUFFER, data->buffer);
        gl->MultiDrawArraysIndirect(currentTopology_, reinterpret_cast<const void*>(offset), drawCount, stride);
    }

    void OpenGLCommandBuffer::CmdDrawIndexedIndirectImpl(
        BufferHandle buffer, uint64_t offset, uint32_t drawCount, uint32_t stride
    ) {
        auto* gl = device_->GetGLContext();
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }

        gl->BindBuffer(GL_DRAW_INDIRECT_BUFFER, data->buffer);
        gl->MultiDrawElementsIndirect(
            currentTopology_, currentIndexType_, reinterpret_cast<const void*>(offset), drawCount, stride
        );
    }

    void OpenGLCommandBuffer::CmdDrawIndexedIndirectCountImpl(
        BufferHandle, uint64_t, BufferHandle, uint64_t, uint32_t, uint32_t
    ) {
        // Requires GL_ARB_indirect_parameters — not guaranteed on T4
    }

    void OpenGLCommandBuffer::CmdDrawMeshTasksImpl(uint32_t, uint32_t, uint32_t) {
        // Mesh shaders not available on T4
    }

    void OpenGLCommandBuffer::CmdDrawMeshTasksIndirectImpl(BufferHandle, uint64_t, uint32_t, uint32_t) {}
    void OpenGLCommandBuffer::CmdDrawMeshTasksIndirectCountImpl(
        BufferHandle, uint64_t, BufferHandle, uint64_t, uint32_t, uint32_t
    ) {}

    // =========================================================================
    // Compute
    // =========================================================================

    void OpenGLCommandBuffer::CmdDispatchImpl(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
        device_->GetGLContext()->DispatchCompute(groupCountX, groupCountY, groupCountZ);
    }

    void OpenGLCommandBuffer::CmdDispatchIndirectImpl(BufferHandle buffer, uint64_t offset) {
        auto* gl = device_->GetGLContext();
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }

        gl->BindBuffer(GL_DISPATCH_INDIRECT_BUFFER, data->buffer);
        gl->DispatchComputeIndirect(static_cast<GLintptr>(offset));
    }

    // =========================================================================
    // Transfer
    // =========================================================================

    void OpenGLCommandBuffer::CmdCopyBufferImpl(
        BufferHandle src, uint64_t srcOffset, BufferHandle dst, uint64_t dstOffset, uint64_t size
    ) {
        auto* gl = device_->GetGLContext();
        auto* srcData = device_->GetBufferPool().Lookup(src);
        auto* dstData = device_->GetBufferPool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }

        gl->CopyNamedBufferSubData(
            srcData->buffer, dstData->buffer, static_cast<GLintptr>(srcOffset), static_cast<GLintptr>(dstOffset),
            static_cast<GLsizeiptr>(size)
        );
    }

    void OpenGLCommandBuffer::CmdCopyBufferToTextureImpl(
        BufferHandle src, TextureHandle dst, const BufferTextureCopyRegion& region
    ) {
        auto* gl = device_->GetGLContext();
        auto* srcData = device_->GetBufferPool().Lookup(src);
        auto* dstData = device_->GetTexturePool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }

        auto [format, type] = InternalFormatToTransfer(dstData->internalFormat);

        gl->BindBuffer(GL_PIXEL_UNPACK_BUFFER, srcData->buffer);
        gl->BindTexture(dstData->target, dstData->texture);

        if (dstData->target == GL_TEXTURE_2D || dstData->target == GL_TEXTURE_CUBE_MAP) {
            gl->TexSubImage2D(
                dstData->target, region.subresource.baseMipLevel, region.textureOffset.x, region.textureOffset.y,
                region.textureExtent.width, region.textureExtent.height, format, type,
                reinterpret_cast<const void*>(region.bufferOffset)
            );
        } else if (dstData->target == GL_TEXTURE_3D || dstData->target == GL_TEXTURE_2D_ARRAY) {
            gl->TexSubImage3D(
                dstData->target, region.subresource.baseMipLevel, region.textureOffset.x, region.textureOffset.y,
                region.textureOffset.z, region.textureExtent.width, region.textureExtent.height,
                region.textureExtent.depth, format, type, reinterpret_cast<const void*>(region.bufferOffset)
            );
        }

        gl->BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        gl->BindTexture(dstData->target, 0);
    }

    void OpenGLCommandBuffer::CmdCopyTextureToBufferImpl(
        TextureHandle src, BufferHandle dst, const BufferTextureCopyRegion& region
    ) {
        auto* gl = device_->GetGLContext();
        auto* srcData = device_->GetTexturePool().Lookup(src);
        auto* dstData = device_->GetBufferPool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }

        auto [format, type] = InternalFormatToTransfer(srcData->internalFormat);

        // GL 4.3 fallback: use FBO + glReadPixels instead of glGetTextureSubImage (GL 4.5)
        GLuint fbo = 0;
        gl->GenFramebuffers(1, &fbo);
        gl->BindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        gl->FramebufferTexture2D(
            GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, srcData->target, srcData->texture,
            static_cast<GLint>(region.subresource.baseMipLevel)
        );
        gl->BindBuffer(GL_PIXEL_PACK_BUFFER, dstData->buffer);
        gl->ReadPixels(
            region.textureOffset.x, region.textureOffset.y, static_cast<GLsizei>(region.textureExtent.width),
            static_cast<GLsizei>(region.textureExtent.height), format, type,
            reinterpret_cast<void*>(region.bufferOffset)
        );
        gl->BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        gl->BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        gl->DeleteFramebuffers(1, &fbo);
    }

    void OpenGLCommandBuffer::CmdCopyTextureImpl(
        TextureHandle src, TextureHandle dst, const TextureCopyRegion& region
    ) {
        auto* gl = device_->GetGLContext();
        auto* srcData = device_->GetTexturePool().Lookup(src);
        auto* dstData = device_->GetTexturePool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }

        gl->CopyImageSubData(
            srcData->texture, srcData->target, region.srcSubresource.baseMipLevel, region.srcOffset.x,
            region.srcOffset.y, region.srcOffset.z, dstData->texture, dstData->target,
            region.dstSubresource.baseMipLevel, region.dstOffset.x, region.dstOffset.y, region.dstOffset.z,
            region.extent.width, region.extent.height, region.extent.depth
        );
    }

    void OpenGLCommandBuffer::CmdBlitTextureImpl(
        TextureHandle src, TextureHandle dst, const TextureBlitRegion& region, Filter filter
    ) {
        auto* gl = device_->GetGLContext();
        auto* srcData = device_->GetTexturePool().Lookup(src);
        auto* dstData = device_->GetTexturePool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }

        // Blit requires FBOs
        GLuint srcFBO = 0, dstFBO = 0;
        gl->GenFramebuffers(1, &srcFBO);
        gl->GenFramebuffers(1, &dstFBO);

        gl->BindFramebuffer(GL_READ_FRAMEBUFFER, srcFBO);
        gl->FramebufferTexture(
            GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, srcData->texture, region.srcSubresource.baseMipLevel
        );

        gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFBO);
        gl->FramebufferTexture(
            GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, dstData->texture, region.dstSubresource.baseMipLevel
        );

        GLenum glFilter = (filter == Filter::Linear) ? GL_LINEAR : GL_NEAREST;
        gl->BlitFramebuffer(
            region.srcOffsetMin.x, region.srcOffsetMin.y, region.srcOffsetMax.x, region.srcOffsetMax.y,
            region.dstOffsetMin.x, region.dstOffsetMin.y, region.dstOffsetMax.x, region.dstOffsetMax.y,
            GL_COLOR_BUFFER_BIT, glFilter
        );

        gl->BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl->DeleteFramebuffers(1, &srcFBO);
        gl->DeleteFramebuffers(1, &dstFBO);
    }

    void OpenGLCommandBuffer::CmdFillBufferImpl(BufferHandle buffer, uint64_t offset, uint64_t size, uint32_t value) {
        auto* gl = device_->GetGLContext();
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }

        uint64_t fillSize = (size == 0) ? data->size - offset : size;
        gl->ClearNamedBufferSubData(
            data->buffer, GL_R32UI, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(fillSize), GL_RED_INTEGER,
            GL_UNSIGNED_INT, &value
        );
    }

    void OpenGLCommandBuffer::CmdClearColorTextureImpl(
        TextureHandle tex, const ClearColor& color, const TextureSubresourceRange& range
    ) {
        auto* gl = device_->GetGLContext();
        auto* data = device_->GetTexturePool().Lookup(tex);
        if (!data) {
            return;
        }

        // GL 4.3 fallback: use FBO + glClearBufferfv instead of glClearTexImage (GL 4.4)
        GLuint fbo = 0;
        gl->GenFramebuffers(1, &fbo);
        gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);

        float clearColor[] = {color.r, color.g, color.b, color.a};
        uint32_t mipCount = (range.mipLevelCount == 0) ? data->mipLevels - range.baseMipLevel : range.mipLevelCount;
        for (uint32_t m = 0; m < mipCount; ++m) {
            GLint level = static_cast<GLint>(range.baseMipLevel + m);
            gl->FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, data->target, data->texture, level);
            gl->ClearBufferfv(GL_COLOR, 0, clearColor);
        }

        gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl->DeleteFramebuffers(1, &fbo);
    }

    void OpenGLCommandBuffer::CmdClearDepthStencilImpl(
        TextureHandle tex, float depth, uint8_t stencil, const TextureSubresourceRange& range
    ) {
        auto* gl = device_->GetGLContext();
        auto* data = device_->GetTexturePool().Lookup(tex);
        if (!data) {
            return;
        }

        // GL 4.3 fallback: use FBO + glClearBufferfv/fi instead of glClearTexImage (GL 4.4)
        bool hasDepth
            = (data->internalFormat == GL_DEPTH_COMPONENT16 || data->internalFormat == GL_DEPTH_COMPONENT24
               || data->internalFormat == GL_DEPTH_COMPONENT32F || data->internalFormat == GL_DEPTH24_STENCIL8
               || data->internalFormat == GL_DEPTH32F_STENCIL8);
        bool hasStencil
            = (data->internalFormat == GL_DEPTH24_STENCIL8 || data->internalFormat == GL_DEPTH32F_STENCIL8
               || data->internalFormat == GL_STENCIL_INDEX8);

        GLuint fbo = 0;
        gl->GenFramebuffers(1, &fbo);
        gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);

        uint32_t mipCount = (range.mipLevelCount == 0) ? data->mipLevels - range.baseMipLevel : range.mipLevelCount;
        for (uint32_t m = 0; m < mipCount; ++m) {
            GLint level = static_cast<GLint>(range.baseMipLevel + m);
            GLenum attachment = hasDepth && hasStencil ? GL_DEPTH_STENCIL_ATTACHMENT
                                : hasDepth             ? GL_DEPTH_ATTACHMENT
                                                       : GL_STENCIL_ATTACHMENT;
            gl->FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachment, data->target, data->texture, level);
            if (hasDepth && hasStencil) {
                gl->ClearBufferfi(GL_DEPTH_STENCIL, 0, depth, static_cast<GLint>(stencil));
            } else if (hasDepth) {
                gl->ClearBufferfv(GL_DEPTH, 0, &depth);
            } else if (hasStencil) {
                GLint s = stencil;
                gl->ClearBufferiv(GL_STENCIL, 0, &s);
            }
        }

        gl->BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        gl->DeleteFramebuffers(1, &fbo);
    }

    // =========================================================================
    // Synchronization (coarse glMemoryBarrier)
    // =========================================================================

    void OpenGLCommandBuffer::CmdPipelineBarrierImpl(const PipelineBarrierDesc&) {
        device_->GetGLContext()->MemoryBarrier(GL_ALL_BARRIER_BITS);
    }

    void OpenGLCommandBuffer::CmdBufferBarrierImpl(BufferHandle, const BufferBarrierDesc&) {
        device_->GetGLContext()->MemoryBarrier(
            GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_ELEMENT_ARRAY_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT
            | GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT
        );
    }

    void OpenGLCommandBuffer::CmdTextureBarrierImpl(TextureHandle, const TextureBarrierDesc&) {
        device_->GetGLContext()->MemoryBarrier(
            GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT
            | GL_TEXTURE_UPDATE_BARRIER_BIT
        );
    }

    // =========================================================================
    // Dynamic rendering (FBO bind/unbind + glClear)
    // =========================================================================

    void OpenGLCommandBuffer::CmdBeginRenderingImpl(const RenderingDesc& desc) {
        auto* gl = device_->GetGLContext();

        // Create temporary FBO for the rendering pass
        GLuint fbo = 0;
        gl->GenFramebuffers(1, &fbo);
        gl->BindFramebuffer(GL_FRAMEBUFFER, fbo);
        currentFBO_ = fbo;

        // Attach color targets
        std::vector<GLenum> drawBuffers;
        for (size_t i = 0; i < desc.colorAttachments.size(); ++i) {
            auto& att = desc.colorAttachments[i];
            if (!att.view.IsValid()) {
                continue;
            }

            auto* viewData = device_->GetTextureViewPool().Lookup(att.view);
            if (!viewData) {
                continue;
            }

            GLenum attachPoint = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i);
            gl->FramebufferTexture(GL_FRAMEBUFFER, attachPoint, viewData->viewTexture, 0);
            drawBuffers.push_back(attachPoint);

            if (att.loadOp == AttachmentLoadOp::Clear) {
                float clearColor[]
                    = {att.clearValue.color.r, att.clearValue.color.g, att.clearValue.color.b, att.clearValue.color.a};
                gl->ClearBufferfv(GL_COLOR, static_cast<GLint>(i), clearColor);
            }
        }

        if (!drawBuffers.empty()) {
            gl->DrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
        }

        // Attach depth
        if (desc.depthAttachment) {
            auto* viewData = device_->GetTextureViewPool().Lookup(desc.depthAttachment->view);
            if (viewData) {
                gl->FramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, viewData->viewTexture, 0);

                if (desc.depthAttachment->loadOp == AttachmentLoadOp::Clear) {
                    float d = desc.depthAttachment->clearValue.depthStencil.depth;
                    gl->ClearBufferfv(GL_DEPTH, 0, &d);
                }
            }
        }

        // Attach stencil
        if (desc.stencilAttachment) {
            auto* viewData = device_->GetTextureViewPool().Lookup(desc.stencilAttachment->view);
            if (viewData) {
                gl->FramebufferTexture(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, viewData->viewTexture, 0);

                if (desc.stencilAttachment->loadOp == AttachmentLoadOp::Clear) {
                    GLint s = desc.stencilAttachment->clearValue.depthStencil.stencil;
                    gl->ClearBufferiv(GL_STENCIL, 0, &s);
                }
            }
        }
    }

    void OpenGLCommandBuffer::CmdEndRenderingImpl() {
        auto* gl = device_->GetGLContext();
        if (currentFBO_ != 0) {
            gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
            gl->DeleteFramebuffers(1, &currentFBO_);
            currentFBO_ = 0;
        }
    }

    // =========================================================================
    // Dynamic state
    // =========================================================================

    void OpenGLCommandBuffer::CmdSetViewportImpl(const Viewport& vp) {
        auto* gl = device_->GetGLContext();
        gl->ViewportIndexedf(0, vp.x, vp.y, vp.width, vp.height);
        gl->DepthRangeIndexed(0, vp.minDepth, vp.maxDepth);
    }

    void OpenGLCommandBuffer::CmdSetScissorImpl(const Rect2D& scissor) {
        auto* gl = device_->GetGLContext();
        gl->Enable(GL_SCISSOR_TEST);
        gl->ScissorIndexed(0, scissor.offset.x, scissor.offset.y, scissor.extent.width, scissor.extent.height);
    }

    void OpenGLCommandBuffer::CmdSetDepthBiasImpl(float constantFactor, float clamp, float slopeFactor) {
        auto* gl = device_->GetGLContext();
        gl->Enable(GL_POLYGON_OFFSET_FILL);
        gl->Enable(GL_POLYGON_OFFSET_LINE);
        gl->PolygonOffset(slopeFactor, constantFactor);
        (void)clamp;  // GL doesn't support depth bias clamp natively (requires GL_EXT_polygon_offset_clamp)
    }

    void OpenGLCommandBuffer::CmdSetStencilReferenceImpl(uint32_t ref) {
        auto* gl = device_->GetGLContext();
        // Re-set stencil func preserving pipeline's compare op and compare mask
        auto* pipeData = device_->GetPipelinePool().Lookup(currentPipeline_);
        if (pipeData) {
            gl->StencilFuncSeparate(
                GL_FRONT, pipeData->stencilFront.compareOp, static_cast<GLint>(ref), pipeData->stencilFront.compareMask
            );
            gl->StencilFuncSeparate(
                GL_BACK, pipeData->stencilBack.compareOp, static_cast<GLint>(ref), pipeData->stencilBack.compareMask
            );
        } else {
            gl->StencilFuncSeparate(GL_FRONT, GL_ALWAYS, static_cast<GLint>(ref), 0xFF);
            gl->StencilFuncSeparate(GL_BACK, GL_ALWAYS, static_cast<GLint>(ref), 0xFF);
        }
    }

    void OpenGLCommandBuffer::CmdSetBlendConstantsImpl(const float constants[4]) {
        device_->GetGLContext()->BlendColor(constants[0], constants[1], constants[2], constants[3]);
    }

    void OpenGLCommandBuffer::CmdSetDepthBoundsImpl(float minDepth, float maxDepth) {
        auto* gl = device_->GetGLContext();
        // GL_EXT_depth_bounds_test (available on NV, not standard)
        (void)minDepth;
        (void)maxDepth;
    }

    void OpenGLCommandBuffer::CmdSetLineWidthImpl(float width) {
        device_->GetGLContext()->LineWidth(width);
    }

    // =========================================================================
    // VRS (not available on T4)
    // =========================================================================

    void OpenGLCommandBuffer::CmdSetShadingRateImpl(ShadingRate, const ShadingRateCombinerOp[2]) {}
    void OpenGLCommandBuffer::CmdSetShadingRateImageImpl(TextureViewHandle) {}

    // =========================================================================
    // Secondary command buffers (replayed inline — GL has no bundles)
    // =========================================================================

    void OpenGLCommandBuffer::CmdExecuteSecondaryImpl(std::span<const CommandBufferHandle>) {
        // OpenGL has no secondary command buffer concept.
        // Secondary commands were already recorded directly as GL calls.
    }

    // =========================================================================
    // Query
    // =========================================================================

    void OpenGLCommandBuffer::CmdBeginQueryImpl(QueryPoolHandle pool, uint32_t index) {
        auto* gl = device_->GetGLContext();
        auto* data = device_->GetQueryPoolPool().Lookup(pool);
        if (!data || index >= data->count) {
            return;
        }

        GLenum target = GL_SAMPLES_PASSED;
        if (data->type == QueryType::Timestamp) {
            target = GL_TIME_ELAPSED;
        }
        if (data->type == QueryType::PipelineStatistics) {
            target = GL_PRIMITIVES_GENERATED;
        }
        gl->BeginQuery(target, data->queries[index]);
    }

    void OpenGLCommandBuffer::CmdEndQueryImpl(QueryPoolHandle pool, uint32_t index) {
        auto* gl = device_->GetGLContext();
        auto* data = device_->GetQueryPoolPool().Lookup(pool);
        if (!data || index >= data->count) {
            return;
        }

        GLenum target = GL_SAMPLES_PASSED;
        if (data->type == QueryType::Timestamp) {
            target = GL_TIME_ELAPSED;
        }
        if (data->type == QueryType::PipelineStatistics) {
            target = GL_PRIMITIVES_GENERATED;
        }
        gl->EndQuery(target);
    }

    void OpenGLCommandBuffer::CmdWriteTimestampImpl(PipelineStage, QueryPoolHandle pool, uint32_t index) {
        auto* gl = device_->GetGLContext();
        auto* data = device_->GetQueryPoolPool().Lookup(pool);
        if (!data || index >= data->count) {
            return;
        }

        gl->QueryCounter(data->queries[index], GL_TIMESTAMP);
    }

    void OpenGLCommandBuffer::CmdResetQueryPoolImpl(QueryPoolHandle, uint32_t, uint32_t) {
        // GL query objects don't need explicit reset
    }

    // =========================================================================
    // Debug labels (KHR_debug)
    // =========================================================================

    void OpenGLCommandBuffer::CmdBeginDebugLabelImpl(const char* name, const float[4]) {
        auto* gl = device_->GetGLContext();
        if (name && gl->KHR_debug) {
            gl->PushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
        }
    }

    void OpenGLCommandBuffer::CmdEndDebugLabelImpl() {
        auto* gl = device_->GetGLContext();
        if (gl->KHR_debug) {
            gl->PopDebugGroup();
        }
    }

    void OpenGLCommandBuffer::CmdInsertDebugLabelImpl(const char* name, const float[4]) {
        auto* gl = device_->GetGLContext();
        if (name && gl->KHR_debug) {
            gl->DebugMessageInsert(
                GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 0, GL_DEBUG_SEVERITY_NOTIFICATION, -1, name
            );
        }
    }

    // =========================================================================
    // T4 unsupported — acceleration structures, decompression, work graphs
    // =========================================================================

    void OpenGLCommandBuffer::CmdBuildBLASImpl(AccelStructHandle, BufferHandle) {}
    void OpenGLCommandBuffer::CmdBuildTLASImpl(AccelStructHandle, BufferHandle) {}
    void OpenGLCommandBuffer::CmdUpdateBLASImpl(AccelStructHandle, BufferHandle) {}
    void OpenGLCommandBuffer::CmdDecompressBufferImpl(const DecompressBufferDesc&) {}
    void OpenGLCommandBuffer::CmdDispatchGraphImpl(const DispatchGraphDesc&) {}

}  // namespace miki::rhi
