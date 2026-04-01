/** @file VulkanCommandBuffer.cpp
 *  @brief Vulkan 1.4 backend — all CommandBufferBase<VulkanCommandBuffer> *Impl methods.
 *
 *  Every method is a thin wrapper over the corresponding vkCmd* call.
 *  HandlePool lookups resolve RHI handles to VkBuffer/VkImage/VkPipeline etc.
 *  Uses vkCmdPipelineBarrier2 (synchronization2, core 1.3) for all barriers.
 *  Uses vkCmdBeginRendering (dynamic rendering, core 1.3) for render passes.
 */

#include "miki/rhi/backend/VulkanCommandBuffer.h"

#include <cassert>

namespace miki::rhi {

    // =========================================================================
    // Conversion helpers
    // =========================================================================

    namespace {
        auto ToVkPipelineStageFlags2(PipelineStage stage) -> VkPipelineStageFlags2 {
            VkPipelineStageFlags2 flags = 0;
            auto has = [stage](PipelineStage bit) {
                return (static_cast<uint32_t>(stage) & static_cast<uint32_t>(bit)) != 0;
            };
            if (has(PipelineStage::TopOfPipe)) {
                flags |= VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            }
            if (has(PipelineStage::DrawIndirect)) {
                flags |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            }
            if (has(PipelineStage::VertexInput)) {
                flags |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
            }
            if (has(PipelineStage::VertexShader)) {
                flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
            }
            if (has(PipelineStage::TaskShader)) {
                flags |= VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT;
            }
            if (has(PipelineStage::MeshShader)) {
                flags |= VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT;
            }
            if (has(PipelineStage::FragmentShader)) {
                flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            }
            if (has(PipelineStage::EarlyFragmentTests)) {
                flags |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
            }
            if (has(PipelineStage::LateFragmentTests)) {
                flags |= VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            }
            if (has(PipelineStage::ColorAttachmentOutput)) {
                flags |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            }
            if (has(PipelineStage::ComputeShader)) {
                flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            }
            if (has(PipelineStage::Transfer)) {
                flags |= VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            }
            if (has(PipelineStage::BottomOfPipe)) {
                flags |= VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            }
            if (has(PipelineStage::Host)) {
                flags |= VK_PIPELINE_STAGE_2_HOST_BIT;
            }
            if (has(PipelineStage::AllGraphics)) {
                flags |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
            }
            if (has(PipelineStage::AllCommands)) {
                flags |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            }
            if (has(PipelineStage::AccelStructBuild)) {
                flags |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
            }
            if (has(PipelineStage::RayTracingShader)) {
                flags |= VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
            }
            if (has(PipelineStage::ShadingRateImage)) {
                flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
            }
            return flags;
        }

        auto ToVkAccessFlags2(AccessFlags access) -> VkAccessFlags2 {
            VkAccessFlags2 flags = 0;
            auto has = [access](AccessFlags bit) {
                return (static_cast<uint32_t>(access) & static_cast<uint32_t>(bit)) != 0;
            };
            if (has(AccessFlags::IndirectCommandRead)) {
                flags |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
            }
            if (has(AccessFlags::IndexRead)) {
                flags |= VK_ACCESS_2_INDEX_READ_BIT;
            }
            if (has(AccessFlags::VertexAttributeRead)) {
                flags |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
            }
            if (has(AccessFlags::UniformRead)) {
                flags |= VK_ACCESS_2_UNIFORM_READ_BIT;
            }
            if (has(AccessFlags::InputAttachmentRead)) {
                flags |= VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT;
            }
            if (has(AccessFlags::ShaderRead)) {
                flags |= VK_ACCESS_2_SHADER_READ_BIT;
            }
            if (has(AccessFlags::ShaderWrite)) {
                flags |= VK_ACCESS_2_SHADER_WRITE_BIT;
            }
            if (has(AccessFlags::ColorAttachmentRead)) {
                flags |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
            }
            if (has(AccessFlags::ColorAttachmentWrite)) {
                flags |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            }
            if (has(AccessFlags::DepthStencilRead)) {
                flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            }
            if (has(AccessFlags::DepthStencilWrite)) {
                flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            }
            if (has(AccessFlags::TransferRead)) {
                flags |= VK_ACCESS_2_TRANSFER_READ_BIT;
            }
            if (has(AccessFlags::TransferWrite)) {
                flags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
            }
            if (has(AccessFlags::HostRead)) {
                flags |= VK_ACCESS_2_HOST_READ_BIT;
            }
            if (has(AccessFlags::HostWrite)) {
                flags |= VK_ACCESS_2_HOST_WRITE_BIT;
            }
            if (has(AccessFlags::MemoryRead)) {
                flags |= VK_ACCESS_2_MEMORY_READ_BIT;
            }
            if (has(AccessFlags::MemoryWrite)) {
                flags |= VK_ACCESS_2_MEMORY_WRITE_BIT;
            }
            if (has(AccessFlags::AccelStructRead)) {
                flags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            }
            if (has(AccessFlags::AccelStructWrite)) {
                flags |= VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            }
            if (has(AccessFlags::ShadingRateImageRead)) {
                flags |= VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR;
            }
            return flags;
        }

        auto ToVkImageLayout(TextureLayout layout) -> VkImageLayout {
            switch (layout) {
                case TextureLayout::Undefined: return VK_IMAGE_LAYOUT_UNDEFINED;
                case TextureLayout::General: return VK_IMAGE_LAYOUT_GENERAL;
                case TextureLayout::ColorAttachment: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                case TextureLayout::DepthStencilAttachment: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                case TextureLayout::DepthStencilReadOnly: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                case TextureLayout::ShaderReadOnly: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                case TextureLayout::TransferSrc: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                case TextureLayout::TransferDst: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                case TextureLayout::Present: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                case TextureLayout::ShadingRate: return VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
            }
            return VK_IMAGE_LAYOUT_UNDEFINED;
        }

        auto ToVkImageAspect(TextureAspect aspect) -> VkImageAspectFlags {
            switch (aspect) {
                case TextureAspect::Color: return VK_IMAGE_ASPECT_COLOR_BIT;
                case TextureAspect::Depth: return VK_IMAGE_ASPECT_DEPTH_BIT;
                case TextureAspect::Stencil: return VK_IMAGE_ASPECT_STENCIL_BIT;
                case TextureAspect::DepthStencil: return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            }
            return VK_IMAGE_ASPECT_COLOR_BIT;
        }

        auto ToVkLoadOp(AttachmentLoadOp op) -> VkAttachmentLoadOp {
            switch (op) {
                case AttachmentLoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
                case AttachmentLoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
                case AttachmentLoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            }
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }

        auto ToVkStoreOp(AttachmentStoreOp op) -> VkAttachmentStoreOp {
            switch (op) {
                case AttachmentStoreOp::Store: return VK_ATTACHMENT_STORE_OP_STORE;
                case AttachmentStoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
            }
            return VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }

        auto ToVkFilter(Filter f) -> VkFilter {
            return (f == Filter::Nearest) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        }

        auto ToVkShaderStageFlags(ShaderStage stages) -> VkShaderStageFlags {
            VkShaderStageFlags flags = 0;
            auto has = [stages](ShaderStage bit) {
                return (static_cast<uint32_t>(stages) & static_cast<uint32_t>(bit)) != 0;
            };
            if (has(ShaderStage::Vertex)) {
                flags |= VK_SHADER_STAGE_VERTEX_BIT;
            }
            if (has(ShaderStage::Fragment)) {
                flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
            }
            if (has(ShaderStage::Compute)) {
                flags |= VK_SHADER_STAGE_COMPUTE_BIT;
            }
            if (has(ShaderStage::Task)) {
                flags |= VK_SHADER_STAGE_TASK_BIT_EXT;
            }
            if (has(ShaderStage::Mesh)) {
                flags |= VK_SHADER_STAGE_MESH_BIT_EXT;
            }
            if (stages == ShaderStage::All) {
                flags = VK_SHADER_STAGE_ALL;
            }
            return flags;
        }
    }  // namespace

    // =========================================================================
    // Lifecycle
    // =========================================================================

    void VulkanCommandBuffer::BeginImpl() {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_, &beginInfo);
    }

    void VulkanCommandBuffer::EndImpl() {
        vkEndCommandBuffer(cmd_);
    }

    void VulkanCommandBuffer::ResetImpl() {
        vkResetCommandBuffer(cmd_, 0);
        currentPipelineLayout_ = VK_NULL_HANDLE;
        currentBindPoint_ = VK_PIPELINE_BIND_POINT_GRAPHICS;
    }

    // =========================================================================
    // State binding
    // =========================================================================

    void VulkanCommandBuffer::CmdBindPipelineImpl(PipelineHandle pipeline) {
        auto* data = device_->GetPipelinePool().Lookup(pipeline);
        if (!data) {
            return;
        }
        currentBindPoint_ = data->bindPoint;
        currentPipelineLayout_ = data->layout;
        vkCmdBindPipeline(cmd_, data->bindPoint, data->pipeline);
    }

    void VulkanCommandBuffer::CmdBindDescriptorSetImpl(
        uint32_t set, DescriptorSetHandle ds, std::span<const uint32_t> dynamicOffsets
    ) {
        auto* dsData = device_->GetDescriptorSetPool().Lookup(ds);
        if (!dsData || currentPipelineLayout_ == VK_NULL_HANDLE) {
            return;
        }
        vkCmdBindDescriptorSets(
            cmd_, currentBindPoint_, currentPipelineLayout_, set, 1, &dsData->set,
            static_cast<uint32_t>(dynamicOffsets.size()), dynamicOffsets.data()
        );
    }

    void VulkanCommandBuffer::CmdPushConstantsImpl(
        ShaderStage stages, uint32_t offset, uint32_t size, const void* data
    ) {
        if (currentPipelineLayout_ == VK_NULL_HANDLE) {
            return;
        }
        vkCmdPushConstants(cmd_, currentPipelineLayout_, ToVkShaderStageFlags(stages), offset, size, data);
    }

    void VulkanCommandBuffer::CmdBindVertexBufferImpl(uint32_t binding, BufferHandle buffer, uint64_t offset) {
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }
        VkDeviceSize vkOffset = offset;
        vkCmdBindVertexBuffers(cmd_, binding, 1, &data->buffer, &vkOffset);
    }

    void VulkanCommandBuffer::CmdBindIndexBufferImpl(BufferHandle buffer, uint64_t offset, IndexType indexType) {
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }
        VkIndexType vkType = (indexType == IndexType::Uint16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        vkCmdBindIndexBuffer(cmd_, data->buffer, offset, vkType);
    }

    // =========================================================================
    // Draw
    // =========================================================================

    void VulkanCommandBuffer::CmdDrawImpl(
        uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance
    ) {
        vkCmdDraw(cmd_, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void VulkanCommandBuffer::CmdDrawIndexedImpl(
        uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance
    ) {
        vkCmdDrawIndexed(cmd_, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void VulkanCommandBuffer::CmdDrawIndirectImpl(
        BufferHandle buffer, uint64_t offset, uint32_t drawCount, uint32_t stride
    ) {
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }
        vkCmdDrawIndirect(cmd_, data->buffer, offset, drawCount, stride);
    }

    void VulkanCommandBuffer::CmdDrawIndexedIndirectImpl(
        BufferHandle buffer, uint64_t offset, uint32_t drawCount, uint32_t stride
    ) {
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }
        vkCmdDrawIndexedIndirect(cmd_, data->buffer, offset, drawCount, stride);
    }

    void VulkanCommandBuffer::CmdDrawIndexedIndirectCountImpl(
        BufferHandle argsBuffer, uint64_t argsOffset, BufferHandle countBuffer, uint64_t countOffset,
        uint32_t maxDrawCount, uint32_t stride
    ) {
        auto* argsData = device_->GetBufferPool().Lookup(argsBuffer);
        auto* countData = device_->GetBufferPool().Lookup(countBuffer);
        if (!argsData || !countData) {
            return;
        }
        vkCmdDrawIndexedIndirectCount(
            cmd_, argsData->buffer, argsOffset, countData->buffer, countOffset, maxDrawCount, stride
        );
    }

    void VulkanCommandBuffer::CmdDrawMeshTasksImpl(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
        if (vkCmdDrawMeshTasksEXT) {
            vkCmdDrawMeshTasksEXT(cmd_, groupCountX, groupCountY, groupCountZ);
        }
    }

    void VulkanCommandBuffer::CmdDrawMeshTasksIndirectImpl(
        BufferHandle buffer, uint64_t offset, uint32_t drawCount, uint32_t stride
    ) {
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data || !vkCmdDrawMeshTasksIndirectEXT) {
            return;
        }
        vkCmdDrawMeshTasksIndirectEXT(cmd_, data->buffer, offset, drawCount, stride);
    }

    void VulkanCommandBuffer::CmdDrawMeshTasksIndirectCountImpl(
        BufferHandle argsBuffer, uint64_t argsOffset, BufferHandle countBuffer, uint64_t countOffset,
        uint32_t maxDrawCount, uint32_t stride
    ) {
        auto* argsData = device_->GetBufferPool().Lookup(argsBuffer);
        auto* countData = device_->GetBufferPool().Lookup(countBuffer);
        if (!argsData || !countData || !vkCmdDrawMeshTasksIndirectCountEXT) {
            return;
        }
        vkCmdDrawMeshTasksIndirectCountEXT(
            cmd_, argsData->buffer, argsOffset, countData->buffer, countOffset, maxDrawCount, stride
        );
    }

    // =========================================================================
    // Compute
    // =========================================================================

    void VulkanCommandBuffer::CmdDispatchImpl(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
        vkCmdDispatch(cmd_, groupCountX, groupCountY, groupCountZ);
    }

    void VulkanCommandBuffer::CmdDispatchIndirectImpl(BufferHandle buffer, uint64_t offset) {
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }
        vkCmdDispatchIndirect(cmd_, data->buffer, offset);
    }

    // =========================================================================
    // Transfer
    // =========================================================================

    void VulkanCommandBuffer::CmdCopyBufferImpl(
        BufferHandle src, uint64_t srcOffset, BufferHandle dst, uint64_t dstOffset, uint64_t size
    ) {
        auto* srcData = device_->GetBufferPool().Lookup(src);
        auto* dstData = device_->GetBufferPool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }
        VkBufferCopy region{srcOffset, dstOffset, size};
        vkCmdCopyBuffer(cmd_, srcData->buffer, dstData->buffer, 1, &region);
    }

    void VulkanCommandBuffer::CmdCopyBufferToTextureImpl(
        BufferHandle src, TextureHandle dst, const BufferTextureCopyRegion& region
    ) {
        auto* srcData = device_->GetBufferPool().Lookup(src);
        auto* dstData = device_->GetTexturePool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }

        VkBufferImageCopy copy{};
        copy.bufferOffset = region.bufferOffset;
        copy.bufferRowLength = region.bufferRowLength;
        copy.bufferImageHeight = region.bufferImageHeight;
        copy.imageSubresource.aspectMask = ToVkImageAspect(region.subresource.aspect);
        copy.imageSubresource.mipLevel = region.subresource.baseMipLevel;
        copy.imageSubresource.baseArrayLayer = region.subresource.baseArrayLayer;
        copy.imageSubresource.layerCount = region.subresource.arrayLayerCount;
        copy.imageOffset
            = {static_cast<int32_t>(region.textureOffset.x), static_cast<int32_t>(region.textureOffset.y),
               static_cast<int32_t>(region.textureOffset.z)};
        copy.imageExtent = {region.textureExtent.width, region.textureExtent.height, region.textureExtent.depth};

        vkCmdCopyBufferToImage(cmd_, srcData->buffer, dstData->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    }

    void VulkanCommandBuffer::CmdCopyTextureToBufferImpl(
        TextureHandle src, BufferHandle dst, const BufferTextureCopyRegion& region
    ) {
        auto* srcData = device_->GetTexturePool().Lookup(src);
        auto* dstData = device_->GetBufferPool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }

        VkBufferImageCopy copy{};
        copy.bufferOffset = region.bufferOffset;
        copy.bufferRowLength = region.bufferRowLength;
        copy.bufferImageHeight = region.bufferImageHeight;
        copy.imageSubresource.aspectMask = ToVkImageAspect(region.subresource.aspect);
        copy.imageSubresource.mipLevel = region.subresource.baseMipLevel;
        copy.imageSubresource.baseArrayLayer = region.subresource.baseArrayLayer;
        copy.imageSubresource.layerCount = region.subresource.arrayLayerCount;
        copy.imageOffset
            = {static_cast<int32_t>(region.textureOffset.x), static_cast<int32_t>(region.textureOffset.y),
               static_cast<int32_t>(region.textureOffset.z)};
        copy.imageExtent = {region.textureExtent.width, region.textureExtent.height, region.textureExtent.depth};

        vkCmdCopyImageToBuffer(cmd_, srcData->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstData->buffer, 1, &copy);
    }

    void VulkanCommandBuffer::CmdCopyTextureImpl(
        TextureHandle src, TextureHandle dst, const TextureCopyRegion& region
    ) {
        auto* srcData = device_->GetTexturePool().Lookup(src);
        auto* dstData = device_->GetTexturePool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }

        VkImageCopy copy{};
        copy.srcSubresource.aspectMask = ToVkImageAspect(region.srcSubresource.aspect);
        copy.srcSubresource.mipLevel = region.srcSubresource.baseMipLevel;
        copy.srcSubresource.baseArrayLayer = region.srcSubresource.baseArrayLayer;
        copy.srcSubresource.layerCount = region.srcSubresource.arrayLayerCount;
        copy.srcOffset
            = {static_cast<int32_t>(region.srcOffset.x), static_cast<int32_t>(region.srcOffset.y),
               static_cast<int32_t>(region.srcOffset.z)};
        copy.dstSubresource.aspectMask = ToVkImageAspect(region.dstSubresource.aspect);
        copy.dstSubresource.mipLevel = region.dstSubresource.baseMipLevel;
        copy.dstSubresource.baseArrayLayer = region.dstSubresource.baseArrayLayer;
        copy.dstSubresource.layerCount = region.dstSubresource.arrayLayerCount;
        copy.dstOffset
            = {static_cast<int32_t>(region.dstOffset.x), static_cast<int32_t>(region.dstOffset.y),
               static_cast<int32_t>(region.dstOffset.z)};
        copy.extent = {region.extent.width, region.extent.height, region.extent.depth};

        vkCmdCopyImage(
            cmd_, srcData->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstData->image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy
        );
    }

    void VulkanCommandBuffer::CmdBlitTextureImpl(
        TextureHandle src, TextureHandle dst, const TextureBlitRegion& region, Filter filter
    ) {
        auto* srcData = device_->GetTexturePool().Lookup(src);
        auto* dstData = device_->GetTexturePool().Lookup(dst);
        if (!srcData || !dstData) {
            return;
        }

        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = ToVkImageAspect(region.srcSubresource.aspect);
        blit.srcSubresource.mipLevel = region.srcSubresource.baseMipLevel;
        blit.srcSubresource.baseArrayLayer = region.srcSubresource.baseArrayLayer;
        blit.srcSubresource.layerCount = region.srcSubresource.arrayLayerCount;
        blit.srcOffsets[0]
            = {static_cast<int32_t>(region.srcOffsetMin.x), static_cast<int32_t>(region.srcOffsetMin.y),
               static_cast<int32_t>(region.srcOffsetMin.z)};
        blit.srcOffsets[1]
            = {static_cast<int32_t>(region.srcOffsetMax.x), static_cast<int32_t>(region.srcOffsetMax.y),
               static_cast<int32_t>(region.srcOffsetMax.z)};
        blit.dstSubresource.aspectMask = ToVkImageAspect(region.dstSubresource.aspect);
        blit.dstSubresource.mipLevel = region.dstSubresource.baseMipLevel;
        blit.dstSubresource.baseArrayLayer = region.dstSubresource.baseArrayLayer;
        blit.dstSubresource.layerCount = region.dstSubresource.arrayLayerCount;
        blit.dstOffsets[0]
            = {static_cast<int32_t>(region.dstOffsetMin.x), static_cast<int32_t>(region.dstOffsetMin.y),
               static_cast<int32_t>(region.dstOffsetMin.z)};
        blit.dstOffsets[1]
            = {static_cast<int32_t>(region.dstOffsetMax.x), static_cast<int32_t>(region.dstOffsetMax.y),
               static_cast<int32_t>(region.dstOffsetMax.z)};

        vkCmdBlitImage(
            cmd_, srcData->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstData->image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, ToVkFilter(filter)
        );
    }

    void VulkanCommandBuffer::CmdFillBufferImpl(BufferHandle buffer, uint64_t offset, uint64_t size, uint32_t value) {
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }
        vkCmdFillBuffer(cmd_, data->buffer, offset, size, value);
    }

    void VulkanCommandBuffer::CmdClearColorTextureImpl(
        TextureHandle tex, const ClearColor& color, const TextureSubresourceRange& range
    ) {
        auto* data = device_->GetTexturePool().Lookup(tex);
        if (!data) {
            return;
        }

        VkClearColorValue vkColor{};
        vkColor.float32[0] = color.r;
        vkColor.float32[1] = color.g;
        vkColor.float32[2] = color.b;
        vkColor.float32[3] = color.a;

        VkImageSubresourceRange vkRange{};
        vkRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vkRange.baseMipLevel = range.baseMipLevel;
        // 0 = all remaining (VK_REMAINING_MIP_LEVELS / VK_REMAINING_ARRAY_LAYERS)
        vkRange.levelCount = (range.mipLevelCount == 0) ? VK_REMAINING_MIP_LEVELS : range.mipLevelCount;
        vkRange.baseArrayLayer = range.baseArrayLayer;
        vkRange.layerCount = (range.arrayLayerCount == 0) ? VK_REMAINING_ARRAY_LAYERS : range.arrayLayerCount;

        vkCmdClearColorImage(cmd_, data->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &vkColor, 1, &vkRange);
    }

    void VulkanCommandBuffer::CmdClearDepthStencilImpl(
        TextureHandle tex, float depth, uint8_t stencil, const TextureSubresourceRange& range
    ) {
        auto* data = device_->GetTexturePool().Lookup(tex);
        if (!data) {
            return;
        }

        VkClearDepthStencilValue vkValue{depth, stencil};

        VkImageSubresourceRange vkRange{};
        vkRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        vkRange.baseMipLevel = range.baseMipLevel;
        // 0 = all remaining (VK_REMAINING_MIP_LEVELS / VK_REMAINING_ARRAY_LAYERS)
        vkRange.levelCount = (range.mipLevelCount == 0) ? VK_REMAINING_MIP_LEVELS : range.mipLevelCount;
        vkRange.baseArrayLayer = range.baseArrayLayer;
        vkRange.layerCount = (range.arrayLayerCount == 0) ? VK_REMAINING_ARRAY_LAYERS : range.arrayLayerCount;

        vkCmdClearDepthStencilImage(cmd_, data->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &vkValue, 1, &vkRange);
    }

    // =========================================================================
    // Synchronization (vkCmdPipelineBarrier2, synchronization2 core 1.3)
    // =========================================================================

    void VulkanCommandBuffer::CmdPipelineBarrierImpl(const PipelineBarrierDesc& desc) {
        VkMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        barrier.srcStageMask = ToVkPipelineStageFlags2(desc.srcStage);
        barrier.srcAccessMask = ToVkAccessFlags2(desc.srcAccess);
        barrier.dstStageMask = ToVkPipelineStageFlags2(desc.dstStage);
        barrier.dstAccessMask = ToVkAccessFlags2(desc.dstAccess);

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.memoryBarrierCount = 1;
        depInfo.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd_, &depInfo);
    }

    void VulkanCommandBuffer::CmdBufferBarrierImpl(BufferHandle buffer, const BufferBarrierDesc& desc) {
        auto* data = device_->GetBufferPool().Lookup(buffer);
        if (!data) {
            return;
        }

        VkBufferMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.srcStageMask = ToVkPipelineStageFlags2(desc.srcStage);
        barrier.srcAccessMask = ToVkAccessFlags2(desc.srcAccess);
        barrier.dstStageMask = ToVkPipelineStageFlags2(desc.dstStage);
        barrier.dstAccessMask = ToVkAccessFlags2(desc.dstAccess);
        // TODO (Nekomiya) this should be reconsidered after completing render graph
        if (desc.srcQueue != desc.dstQueue) {
            barrier.srcQueueFamilyIndex = device_->QueueFamilyIndex(desc.srcQueue);
            barrier.dstQueueFamilyIndex = device_->QueueFamilyIndex(desc.dstQueue);
        } else {
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        }
        barrier.buffer = data->buffer;
        barrier.offset = desc.offset;
        barrier.size = (desc.size == 0) ? VK_WHOLE_SIZE : desc.size;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.bufferMemoryBarrierCount = 1;
        depInfo.pBufferMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd_, &depInfo);
    }

    void VulkanCommandBuffer::CmdTextureBarrierImpl(TextureHandle texture, const TextureBarrierDesc& desc) {
        auto* data = device_->GetTexturePool().Lookup(texture);
        if (!data) {
            return;
        }

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = ToVkPipelineStageFlags2(desc.srcStage);
        barrier.srcAccessMask = ToVkAccessFlags2(desc.srcAccess);
        barrier.dstStageMask = ToVkPipelineStageFlags2(desc.dstStage);
        barrier.dstAccessMask = ToVkAccessFlags2(desc.dstAccess);
        barrier.oldLayout = ToVkImageLayout(desc.oldLayout);
        barrier.newLayout = ToVkImageLayout(desc.newLayout);
        if (desc.srcQueue != desc.dstQueue) {
            barrier.srcQueueFamilyIndex = device_->QueueFamilyIndex(desc.srcQueue);
            barrier.dstQueueFamilyIndex = device_->QueueFamilyIndex(desc.dstQueue);
        } else {
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        }
        barrier.image = data->image;
        barrier.subresourceRange.aspectMask = ToVkImageAspect(desc.subresource.aspect);
        barrier.subresourceRange.baseMipLevel = desc.subresource.baseMipLevel;
        // 0 = all remaining (VK_REMAINING_MIP_LEVELS / VK_REMAINING_ARRAY_LAYERS)
        barrier.subresourceRange.levelCount
            = (desc.subresource.mipLevelCount == 0) ? VK_REMAINING_MIP_LEVELS : desc.subresource.mipLevelCount;
        barrier.subresourceRange.baseArrayLayer = desc.subresource.baseArrayLayer;
        barrier.subresourceRange.layerCount
            = (desc.subresource.arrayLayerCount == 0) ? VK_REMAINING_ARRAY_LAYERS : desc.subresource.arrayLayerCount;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd_, &depInfo);
    }

    // =========================================================================
    // Dynamic rendering (VK_KHR_dynamic_rendering, core 1.3)
    // =========================================================================

    void VulkanCommandBuffer::CmdBeginRenderingImpl(const RenderingDesc& desc) {
        auto resolveAttachment = [&](const RenderingAttachment& att) -> VkRenderingAttachmentInfo {
            VkRenderingAttachmentInfo info{};
            info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            info.loadOp = ToVkLoadOp(att.loadOp);
            info.storeOp = ToVkStoreOp(att.storeOp);
            info.clearValue.color
                = {{att.clearValue.color.r, att.clearValue.color.g, att.clearValue.color.b, att.clearValue.color.a}};

            if (att.view.IsValid()) {
                auto* viewData = device_->GetTextureViewPool().Lookup(att.view);
                if (viewData) {
                    info.imageView = viewData->view;
                }
            }
            if (att.resolveView.IsValid()) {
                auto* resolveData = device_->GetTextureViewPool().Lookup(att.resolveView);
                if (resolveData) {
                    info.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                    info.resolveImageView = resolveData->view;
                    info.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                }
            }
            return info;
        };

        // Color attachments
        std::vector<VkRenderingAttachmentInfo> colorInfos;
        colorInfos.reserve(desc.colorAttachments.size());
        for (auto& att : desc.colorAttachments) {
            colorInfos.push_back(resolveAttachment(att));
        }

        // Depth attachment
        VkRenderingAttachmentInfo depthInfo{};
        if (desc.depthAttachment) {
            depthInfo = resolveAttachment(*desc.depthAttachment);
            depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthInfo.clearValue.depthStencil
                = {.depth = desc.depthAttachment->clearValue.depthStencil.depth,
                   .stencil = desc.depthAttachment->clearValue.depthStencil.stencil};
        }

        // Stencil attachment
        VkRenderingAttachmentInfo stencilInfo{};
        if (desc.stencilAttachment) {
            stencilInfo = resolveAttachment(*desc.stencilAttachment);
            stencilInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            stencilInfo.clearValue.depthStencil
                = {.depth = desc.stencilAttachment->clearValue.depthStencil.depth,
                   .stencil = desc.stencilAttachment->clearValue.depthStencil.stencil};
        }

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea
            = {.offset = {desc.renderArea.offset.x, desc.renderArea.offset.y},
               .extent = {desc.renderArea.extent.width, desc.renderArea.extent.height}};
        renderingInfo.layerCount = 1;
        renderingInfo.viewMask = desc.viewMask;
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorInfos.size());
        renderingInfo.pColorAttachments = colorInfos.data();
        renderingInfo.pDepthAttachment = desc.depthAttachment ? &depthInfo : nullptr;
        renderingInfo.pStencilAttachment = desc.stencilAttachment ? &stencilInfo : nullptr;

        vkCmdBeginRendering(cmd_, &renderingInfo);
    }

    void VulkanCommandBuffer::CmdEndRenderingImpl() {
        vkCmdEndRendering(cmd_);
    }

    // =========================================================================
    // Dynamic state
    // =========================================================================

    void VulkanCommandBuffer::CmdSetViewportImpl(const Viewport& vp) {
        VkViewport viewport{vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth};
        vkCmdSetViewport(cmd_, 0, 1, &viewport);
    }

    void VulkanCommandBuffer::CmdSetScissorImpl(const Rect2D& scissor) {
        VkRect2D rect{{scissor.offset.x, scissor.offset.y}, {scissor.extent.width, scissor.extent.height}};
        vkCmdSetScissor(cmd_, 0, 1, &rect);
    }

    void VulkanCommandBuffer::CmdSetDepthBiasImpl(float constantFactor, float clamp, float slopeFactor) {
        vkCmdSetDepthBias(cmd_, constantFactor, clamp, slopeFactor);
    }

    void VulkanCommandBuffer::CmdSetStencilReferenceImpl(uint32_t ref) {
        vkCmdSetStencilReference(cmd_, VK_STENCIL_FACE_FRONT_AND_BACK, ref);
    }

    void VulkanCommandBuffer::CmdSetBlendConstantsImpl(const float constants[4]) {
        vkCmdSetBlendConstants(cmd_, constants);
    }

    void VulkanCommandBuffer::CmdSetDepthBoundsImpl(float minDepth, float maxDepth) {
        vkCmdSetDepthBounds(cmd_, minDepth, maxDepth);
    }

    void VulkanCommandBuffer::CmdSetLineWidthImpl(float width) {
        vkCmdSetLineWidth(cmd_, width);
    }

    // =========================================================================
    // Variable Rate Shading (T1 only)
    // =========================================================================

    void VulkanCommandBuffer::CmdSetShadingRateImpl(
        ShadingRate /*baseRate*/, const ShadingRateCombinerOp /*combinerOps*/[2]
    ) {
        // VK_KHR_fragment_shading_rate: vkCmdSetFragmentShadingRateKHR
        // Deferred: requires mapping ShadingRate enum to VkExtent2D and combiner ops
    }

    void VulkanCommandBuffer::CmdSetShadingRateImageImpl(TextureViewHandle /*rateImage*/) {
        // VRS image binding is done via rendering attachment, not a separate command.
    }

    // =========================================================================
    // Secondary command buffers
    // =========================================================================

    void VulkanCommandBuffer::CmdExecuteSecondaryImpl(std::span<const CommandBufferHandle> secondaryBuffers) {
        std::vector<VkCommandBuffer> vkBuffers;
        vkBuffers.reserve(secondaryBuffers.size());
        for (auto h : secondaryBuffers) {
            auto* data = device_->GetCommandBufferPool().Lookup(h);
            if (data) {
                vkBuffers.push_back(data->buffer);
            }
        }
        if (!vkBuffers.empty()) {
            vkCmdExecuteCommands(cmd_, static_cast<uint32_t>(vkBuffers.size()), vkBuffers.data());
        }
    }

    // =========================================================================
    // Query
    // =========================================================================

    void VulkanCommandBuffer::CmdBeginQueryImpl(QueryPoolHandle pool, uint32_t index) {
        auto* data = device_->GetQueryPoolPool().Lookup(pool);
        if (!data) {
            return;
        }
        vkCmdBeginQuery(cmd_, data->pool, index, 0);
    }

    void VulkanCommandBuffer::CmdEndQueryImpl(QueryPoolHandle pool, uint32_t index) {
        auto* data = device_->GetQueryPoolPool().Lookup(pool);
        if (!data) {
            return;
        }
        vkCmdEndQuery(cmd_, data->pool, index);
    }

    void VulkanCommandBuffer::CmdWriteTimestampImpl(PipelineStage stage, QueryPoolHandle pool, uint32_t index) {
        auto* data = device_->GetQueryPoolPool().Lookup(pool);
        if (!data) {
            return;
        }
        vkCmdWriteTimestamp2(cmd_, ToVkPipelineStageFlags2(stage), data->pool, index);
    }

    void VulkanCommandBuffer::CmdResetQueryPoolImpl(QueryPoolHandle pool, uint32_t first, uint32_t count) {
        auto* data = device_->GetQueryPoolPool().Lookup(pool);
        if (!data) {
            return;
        }
        vkCmdResetQueryPool(cmd_, data->pool, first, count);
    }

    // =========================================================================
    // Debug labels
    // =========================================================================

    void VulkanCommandBuffer::CmdBeginDebugLabelImpl(const char* name, const float color[4]) {
        if (!vkCmdBeginDebugUtilsLabelEXT) {
            return;
        }
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        if (color) {
            label.color[0] = color[0];
            label.color[1] = color[1];
            label.color[2] = color[2];
            label.color[3] = color[3];
        }
        vkCmdBeginDebugUtilsLabelEXT(cmd_, &label);
    }

    void VulkanCommandBuffer::CmdEndDebugLabelImpl() {
        if (vkCmdEndDebugUtilsLabelEXT) {
            vkCmdEndDebugUtilsLabelEXT(cmd_);
        }
    }

    void VulkanCommandBuffer::CmdInsertDebugLabelImpl(const char* name, const float color[4]) {
        if (!vkCmdInsertDebugUtilsLabelEXT) {
            return;
        }
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        if (color) {
            label.color[0] = color[0];
            label.color[1] = color[1];
            label.color[2] = color[2];
            label.color[3] = color[3];
        }
        vkCmdInsertDebugUtilsLabelEXT(cmd_, &label);
    }

    // =========================================================================
    // Acceleration structure commands
    // =========================================================================

    void VulkanCommandBuffer::CmdBuildBLASImpl(AccelStructHandle blas, BufferHandle scratch) {
        if (!vkCmdBuildAccelerationStructuresKHR) {
            return;
        }
        auto* accelData = device_->GetAccelStructPool().Lookup(blas);
        auto* scratchData = device_->GetBufferPool().Lookup(scratch);
        if (!accelData || !scratchData) {
            return;
        }

        // NOTE: Full BLAS build requires geometry descriptors (vertex/index buffers, counts).
        // The RHI API CmdBuildBLAS(handle, scratch) assumes geometry was specified at CreateBLAS.
        // A production implementation would store VkAccelerationStructureGeometryKHR[] in
        // VulkanAccelStructData or accept them as an additional parameter.
        // For now, the build info is constructed with the dst accel struct and scratch address.
        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.dstAccelerationStructure = accelData->accelStruct;
        VkBufferDeviceAddressInfo addrInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, scratchData->buffer};
        buildInfo.scratchData.deviceAddress = vkGetBufferDeviceAddress(device_->GetVkDevice(), &addrInfo);

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
        vkCmdBuildAccelerationStructuresKHR(cmd_, 1, &buildInfo, &pRangeInfo);
    }

    void VulkanCommandBuffer::CmdBuildTLASImpl(AccelStructHandle tlas, BufferHandle scratch) {
        if (!vkCmdBuildAccelerationStructuresKHR) {
            return;
        }
        auto* accelData = device_->GetAccelStructPool().Lookup(tlas);
        auto* scratchData = device_->GetBufferPool().Lookup(scratch);
        if (!accelData || !scratchData) {
            return;
        }

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.dstAccelerationStructure = accelData->accelStruct;
        VkBufferDeviceAddressInfo addrInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, scratchData->buffer};
        buildInfo.scratchData.deviceAddress = vkGetBufferDeviceAddress(device_->GetVkDevice(), &addrInfo);

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
        vkCmdBuildAccelerationStructuresKHR(cmd_, 1, &buildInfo, &pRangeInfo);
    }

    void VulkanCommandBuffer::CmdUpdateBLASImpl(AccelStructHandle blas, BufferHandle scratch) {
        if (!vkCmdBuildAccelerationStructuresKHR) {
            return;
        }
        auto* accelData = device_->GetAccelStructPool().Lookup(blas);
        auto* scratchData = device_->GetBufferPool().Lookup(scratch);
        if (!accelData || !scratchData) {
            return;
        }

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        buildInfo.srcAccelerationStructure = accelData->accelStruct;
        buildInfo.dstAccelerationStructure = accelData->accelStruct;
        VkBufferDeviceAddressInfo addrInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, scratchData->buffer};
        buildInfo.scratchData.deviceAddress = vkGetBufferDeviceAddress(device_->GetVkDevice(), &addrInfo);

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
        vkCmdBuildAccelerationStructuresKHR(cmd_, 1, &buildInfo, &pRangeInfo);
    }

    // =========================================================================
    // Decompression
    // =========================================================================

    void VulkanCommandBuffer::CmdDecompressBufferImpl(const DecompressBufferDesc& /*desc*/) {
        // VK_NV_memory_decompression: vkCmdDecompressMemoryNV
        // Deferred: requires runtime extension check
    }

    // =========================================================================
    // Work Graphs (not supported on Vulkan)
    // =========================================================================

    void VulkanCommandBuffer::CmdDispatchGraphImpl(const DispatchGraphDesc& /*desc*/) {
        // Work graphs are D3D12-only (SM 6.8). No Vulkan equivalent as of 2026-Q1.
        // No-op on Vulkan backend.
    }

}  // namespace miki::rhi
