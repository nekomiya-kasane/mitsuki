/** @file VulkanConvert.h
 *  @brief Vulkan enum conversion — zero-cost static_cast with compile-time guarantees.
 *
 *  miki::rhi::PipelineStage and AccessFlags enum values are defined to match
 *  VK_PIPELINE_STAGE_2_* and VK_ACCESS_2_* bit values exactly. This header
 *  provides thin inline casts and static_asserts to guarantee ABI compatibility.
 *
 *  Internal header — used by VulkanCommandBuffer.cpp, VulkanDevice.cpp, etc.
 */
#pragma once

#include "miki/rhi/RhiEnums.h"

#include <vulkan/vulkan.h>

namespace miki::rhi::vulkan {

    // =========================================================================
    // Compile-time guarantees: miki enum values == Vulkan values
    // =========================================================================

    // PipelineStage
    static_assert(uint32_t(PipelineStage::None) == VK_PIPELINE_STAGE_2_NONE);
    static_assert(uint32_t(PipelineStage::TopOfPipe) == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT);
    static_assert(uint32_t(PipelineStage::DrawIndirect) == VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT);
    static_assert(uint32_t(PipelineStage::VertexInput) == VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT);
    static_assert(uint32_t(PipelineStage::VertexShader) == VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
    static_assert(uint32_t(PipelineStage::FragmentShader) == VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    static_assert(uint32_t(PipelineStage::EarlyFragmentTests) == VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT);
    static_assert(uint32_t(PipelineStage::LateFragmentTests) == VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
    static_assert(uint32_t(PipelineStage::ColorAttachmentOutput) == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    static_assert(uint32_t(PipelineStage::ComputeShader) == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    static_assert(uint32_t(PipelineStage::Transfer) == VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT);
    static_assert(uint32_t(PipelineStage::BottomOfPipe) == VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);
    static_assert(uint32_t(PipelineStage::Host) == VK_PIPELINE_STAGE_2_HOST_BIT);
    static_assert(uint32_t(PipelineStage::AllGraphics) == VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
    static_assert(uint32_t(PipelineStage::AllCommands) == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    static_assert(uint32_t(PipelineStage::TaskShader) == VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT);
    static_assert(uint32_t(PipelineStage::MeshShader) == VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT);
    static_assert(uint32_t(PipelineStage::RayTracingShader) == VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR);
    static_assert(
        uint32_t(PipelineStage::ShadingRateImage) == VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR
    );
    static_assert(
        uint32_t(PipelineStage::AccelStructBuild) == VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
    );

    // AccessFlags
    static_assert(uint32_t(AccessFlags::None) == VK_ACCESS_2_NONE);
    static_assert(uint32_t(AccessFlags::IndirectCommandRead) == VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    static_assert(uint32_t(AccessFlags::IndexRead) == VK_ACCESS_2_INDEX_READ_BIT);
    static_assert(uint32_t(AccessFlags::VertexAttributeRead) == VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT);
    static_assert(uint32_t(AccessFlags::UniformRead) == VK_ACCESS_2_UNIFORM_READ_BIT);
    static_assert(uint32_t(AccessFlags::InputAttachmentRead) == VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT);
    static_assert(uint32_t(AccessFlags::ShaderRead) == VK_ACCESS_2_SHADER_READ_BIT);
    static_assert(uint32_t(AccessFlags::ShaderWrite) == VK_ACCESS_2_SHADER_WRITE_BIT);
    static_assert(uint32_t(AccessFlags::ColorAttachmentRead) == VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
    static_assert(uint32_t(AccessFlags::ColorAttachmentWrite) == VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    static_assert(uint32_t(AccessFlags::DepthStencilRead) == VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    static_assert(uint32_t(AccessFlags::DepthStencilWrite) == VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    static_assert(uint32_t(AccessFlags::TransferRead) == VK_ACCESS_2_TRANSFER_READ_BIT);
    static_assert(uint32_t(AccessFlags::TransferWrite) == VK_ACCESS_2_TRANSFER_WRITE_BIT);
    static_assert(uint32_t(AccessFlags::HostRead) == VK_ACCESS_2_HOST_READ_BIT);
    static_assert(uint32_t(AccessFlags::HostWrite) == VK_ACCESS_2_HOST_WRITE_BIT);
    static_assert(uint32_t(AccessFlags::MemoryRead) == VK_ACCESS_2_MEMORY_READ_BIT);
    static_assert(uint32_t(AccessFlags::MemoryWrite) == VK_ACCESS_2_MEMORY_WRITE_BIT);
    static_assert(uint32_t(AccessFlags::AccelStructRead) == VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    static_assert(uint32_t(AccessFlags::AccelStructWrite) == VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
    static_assert(
        uint32_t(AccessFlags::ShadingRateImageRead) == VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR
    );

    // =========================================================================
    // Zero-cost conversion (simple cast, guaranteed correct by static_assert)
    // =========================================================================

    [[nodiscard]] constexpr auto ToVkPipelineStageFlags2(PipelineStage stage) -> VkPipelineStageFlags2 {
        return static_cast<VkPipelineStageFlags2>(stage);
    }

    [[nodiscard]] constexpr auto ToVkAccessFlags2(AccessFlags access) -> VkAccessFlags2 {
        return static_cast<VkAccessFlags2>(access);
    }

}  // namespace miki::rhi::vulkan
