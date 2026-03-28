/** @file VulkanPipelines.cpp
 *  @brief Vulkan 1.4 backend — Graphics, Compute, RayTracing pipelines,
 *         PipelineCache, PipelineLibrary (split compilation).
 */

#include "miki/rhi/backend/VulkanDevice.h"

#include <cassert>
#include <vector>

namespace miki::rhi {

    // =========================================================================
    // Conversion helpers (pipeline-specific)
    // =========================================================================

    namespace {
        auto ToVkFormat(Format fmt) -> VkFormat {
            // Reuse the same mapping as VulkanResources.cpp — in production this would
            // live in a shared VulkanConversions.h. For now, duplicated to keep each
            // .cpp self-contained (no additional header needed).
            switch (fmt) {
                case Format::R8_UNORM: return VK_FORMAT_R8_UNORM;
                case Format::RG8_UNORM: return VK_FORMAT_R8G8_UNORM;
                case Format::RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
                case Format::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
                case Format::BGRA8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
                case Format::BGRA8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
                case Format::R16_FLOAT: return VK_FORMAT_R16_SFLOAT;
                case Format::RG16_FLOAT: return VK_FORMAT_R16G16_SFLOAT;
                case Format::RGBA16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
                case Format::R32_FLOAT: return VK_FORMAT_R32_SFLOAT;
                case Format::RG32_FLOAT: return VK_FORMAT_R32G32_SFLOAT;
                case Format::RGB32_FLOAT: return VK_FORMAT_R32G32B32_SFLOAT;
                case Format::RGBA32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
                case Format::R32_UINT: return VK_FORMAT_R32_UINT;
                case Format::RG32_UINT: return VK_FORMAT_R32G32_UINT;
                case Format::RGBA32_UINT: return VK_FORMAT_R32G32B32A32_UINT;
                case Format::RGB10A2_UNORM: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
                case Format::RG11B10_FLOAT: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
                case Format::D16_UNORM: return VK_FORMAT_D16_UNORM;
                case Format::D32_FLOAT: return VK_FORMAT_D32_SFLOAT;
                case Format::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
                case Format::D32_FLOAT_S8_UINT: return VK_FORMAT_D32_SFLOAT_S8_UINT;
                default: return VK_FORMAT_UNDEFINED;
            }
        }

        auto ToVkPrimitiveTopology(PrimitiveTopology topo) -> VkPrimitiveTopology {
            switch (topo) {
                case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                case PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
                case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                case PrimitiveTopology::TriangleFan: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
                case PrimitiveTopology::PatchList: return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
            }
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }

        auto ToVkPolygonMode(PolygonMode mode) -> VkPolygonMode {
            switch (mode) {
                case PolygonMode::Fill: return VK_POLYGON_MODE_FILL;
                case PolygonMode::Line: return VK_POLYGON_MODE_LINE;
                case PolygonMode::Point: return VK_POLYGON_MODE_POINT;
            }
            return VK_POLYGON_MODE_FILL;
        }

        auto ToVkCullMode(CullMode mode) -> VkCullModeFlags {
            switch (mode) {
                case CullMode::None: return VK_CULL_MODE_NONE;
                case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
                case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
                case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
            }
            return VK_CULL_MODE_NONE;
        }

        auto ToVkFrontFace(FrontFace face) -> VkFrontFace {
            return (face == FrontFace::CounterClockwise) ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
        }

        auto ToVkCompareOp(CompareOp op) -> VkCompareOp {
            switch (op) {
                case CompareOp::Never: return VK_COMPARE_OP_NEVER;
                case CompareOp::Less: return VK_COMPARE_OP_LESS;
                case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
                case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
                case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
                case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
                case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
                case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
                case CompareOp::None: return VK_COMPARE_OP_ALWAYS;
            }
            return VK_COMPARE_OP_ALWAYS;
        }

        auto ToVkStencilOp(StencilOp op) -> VkStencilOp {
            switch (op) {
                case StencilOp::Keep: return VK_STENCIL_OP_KEEP;
                case StencilOp::Zero: return VK_STENCIL_OP_ZERO;
                case StencilOp::Replace: return VK_STENCIL_OP_REPLACE;
                case StencilOp::IncrementAndClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
                case StencilOp::DecrementAndClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
                case StencilOp::Invert: return VK_STENCIL_OP_INVERT;
                case StencilOp::IncrementAndWrap: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
                case StencilOp::DecrementAndWrap: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
            }
            return VK_STENCIL_OP_KEEP;
        }

        auto ToVkStencilOpState(const StencilOpState& s) -> VkStencilOpState {
            return {
                ToVkStencilOp(s.failOp),
                ToVkStencilOp(s.passOp),
                ToVkStencilOp(s.depthFailOp),
                ToVkCompareOp(s.compareOp),
                s.compareMask,
                s.writeMask,
                s.reference
            };
        }

        auto ToVkBlendFactor(BlendFactor f) -> VkBlendFactor {
            switch (f) {
                case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
                case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
                case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
                case BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
                case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
                case BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
                case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
                case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
                case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
                case BlendFactor::ConstantColor: return VK_BLEND_FACTOR_CONSTANT_COLOR;
                case BlendFactor::OneMinusConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
                case BlendFactor::SrcAlphaSaturate: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
            }
            return VK_BLEND_FACTOR_ZERO;
        }

        auto ToVkBlendOp(BlendOp op) -> VkBlendOp {
            switch (op) {
                case BlendOp::Add: return VK_BLEND_OP_ADD;
                case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
                case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
                case BlendOp::Min: return VK_BLEND_OP_MIN;
                case BlendOp::Max: return VK_BLEND_OP_MAX;
            }
            return VK_BLEND_OP_ADD;
        }
    }  // namespace

    // =========================================================================
    // Graphics Pipeline
    // =========================================================================

    auto VulkanDevice::CreateGraphicsPipelineImpl(const GraphicsPipelineDesc& desc) -> RhiResult<PipelineHandle> {
        auto* layoutData = pipelineLayouts_.Lookup(desc.pipelineLayout);
        if (!layoutData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        // --- Shader stages ---
        std::vector<VkPipelineShaderStageCreateInfo> stages;
        auto addStage = [&](ShaderModuleHandle h, VkShaderStageFlagBits stage) {
            if (!h.IsValid()) {
                return;
            }
            auto* mod = shaderModules_.Lookup(h);
            if (!mod) {
                return;
            }
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = stage;
            stageInfo.module = mod->module;
            stageInfo.pName = "main";
            stages.push_back(stageInfo);
        };

        if (desc.IsMeshShaderPipeline()) {
            addStage(desc.taskShader, VK_SHADER_STAGE_TASK_BIT_EXT);
            addStage(desc.meshShader, VK_SHADER_STAGE_MESH_BIT_EXT);
        } else {
            addStage(desc.vertexShader, VK_SHADER_STAGE_VERTEX_BIT);
        }
        addStage(desc.fragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT);

        // --- Vertex input (ignored for mesh shader path) ---
        std::vector<VkVertexInputBindingDescription> vkBindings;
        std::vector<VkVertexInputAttributeDescription> vkAttribs;
        if (!desc.IsMeshShaderPipeline()) {
            for (auto& b : desc.vertexInput.bindings) {
                vkBindings.push_back(
                    {b.binding, b.stride,
                     (b.inputRate == VertexInputRate::PerInstance) ? VK_VERTEX_INPUT_RATE_INSTANCE
                                                                   : VK_VERTEX_INPUT_RATE_VERTEX}
                );
            }
            for (auto& a : desc.vertexInput.attributes) {
                vkAttribs.push_back({a.location, a.binding, ToVkFormat(a.format), a.offset});
            }
        }

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(vkBindings.size());
        vertexInput.pVertexBindingDescriptions = vkBindings.data();
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vkAttribs.size());
        vertexInput.pVertexAttributeDescriptions = vkAttribs.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = ToVkPrimitiveTopology(desc.topology);
        inputAssembly.primitiveRestartEnable = desc.primitiveRestart ? VK_TRUE : VK_FALSE;

        // --- Viewport / Scissor (dynamic) ---
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // --- Rasterizer ---
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = desc.depthClampEnable ? VK_TRUE : VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = ToVkPolygonMode(desc.polygonMode);
        rasterizer.lineWidth = desc.lineWidth;
        rasterizer.cullMode = ToVkCullMode(desc.cullMode);
        rasterizer.frontFace = ToVkFrontFace(desc.frontFace);
        rasterizer.depthBiasEnable = desc.depthBiasEnable ? VK_TRUE : VK_FALSE;

        // --- Multisample ---
        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(desc.sampleCount);
        multisample.sampleShadingEnable = VK_FALSE;

        // --- Depth/stencil ---
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = desc.depthTestEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = desc.depthWriteEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = ToVkCompareOp(desc.depthCompareOp);
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = desc.stencilTestEnable ? VK_TRUE : VK_FALSE;
        depthStencil.front = ToVkStencilOpState(desc.stencilFront);
        depthStencil.back = ToVkStencilOpState(desc.stencilBack);

        // --- Color blend ---
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
        blendAttachments.reserve(desc.colorBlends.size());
        for (auto& cb : desc.colorBlends) {
            VkPipelineColorBlendAttachmentState att{};
            att.blendEnable = cb.blendEnable ? VK_TRUE : VK_FALSE;
            att.srcColorBlendFactor = ToVkBlendFactor(cb.srcColor);
            att.dstColorBlendFactor = ToVkBlendFactor(cb.dstColor);
            att.colorBlendOp = ToVkBlendOp(cb.colorOp);
            att.srcAlphaBlendFactor = ToVkBlendFactor(cb.srcAlpha);
            att.dstAlphaBlendFactor = ToVkBlendFactor(cb.dstAlpha);
            att.alphaBlendOp = ToVkBlendOp(cb.alphaOp);
            att.colorWriteMask = static_cast<VkColorComponentFlags>(cb.writeMask);
            blendAttachments.push_back(att);
        }

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.logicOpEnable = VK_FALSE;
        colorBlend.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
        colorBlend.pAttachments = blendAttachments.data();

        // --- Dynamic state ---
        VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,          VK_DYNAMIC_STATE_SCISSOR,         VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 5;
        dynamicState.pDynamicStates = dynamicStates;

        // --- Dynamic rendering (VK_KHR_dynamic_rendering, core 1.3) ---
        std::vector<VkFormat> vkColorFormats;
        vkColorFormats.reserve(desc.colorFormats.size());
        for (auto f : desc.colorFormats) {
            vkColorFormats.push_back(ToVkFormat(f));
        }

        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(vkColorFormats.size());
        renderingInfo.pColorAttachmentFormats = vkColorFormats.data();
        renderingInfo.depthAttachmentFormat = ToVkFormat(desc.depthFormat);
        renderingInfo.stencilAttachmentFormat = ToVkFormat(desc.stencilFormat);
        renderingInfo.viewMask = desc.viewMask;

        // --- Create pipeline ---
        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = desc.IsMeshShaderPipeline() ? nullptr : &vertexInput;
        pipelineInfo.pInputAssemblyState = desc.IsMeshShaderPipeline() ? nullptr : &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = layoutData->layout;
        pipelineInfo.renderPass = VK_NULL_HANDLE;  // Dynamic rendering — no render pass object

        VkPipeline pipeline = VK_NULL_HANDLE;
        VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::PipelineCreationFailed);
        }

        auto [handle, data] = pipelines_.Allocate();
        if (!data) {
            vkDestroyPipeline(device_, pipeline, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->pipeline = pipeline;
        data->layout = layoutData->layout;
        data->bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        return handle;
    }

    // =========================================================================
    // Compute Pipeline
    // =========================================================================

    auto VulkanDevice::CreateComputePipelineImpl(const ComputePipelineDesc& desc) -> RhiResult<PipelineHandle> {
        auto* layoutData = pipelineLayouts_.Lookup(desc.pipelineLayout);
        if (!layoutData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        auto* shaderData = shaderModules_.Lookup(desc.computeShader);
        if (!shaderData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = shaderData->module;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = layoutData->layout;

        VkPipeline pipeline = VK_NULL_HANDLE;
        VkResult r = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::PipelineCreationFailed);
        }

        auto [handle, data] = pipelines_.Allocate();
        if (!data) {
            vkDestroyPipeline(device_, pipeline, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->pipeline = pipeline;
        data->layout = layoutData->layout;
        data->bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        return handle;
    }

    // =========================================================================
    // Ray Tracing Pipeline (T1 only)
    // =========================================================================

    auto VulkanDevice::CreateRayTracingPipelineImpl(const RayTracingPipelineDesc& desc) -> RhiResult<PipelineHandle> {
        auto* layoutData = pipelineLayouts_.Lookup(desc.pipelineLayout);
        if (!layoutData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        if (!capabilities_.hasRayTracingPipeline) {
            return std::unexpected(RhiError::FeatureNotSupported);
        }

        std::vector<VkPipelineShaderStageCreateInfo> stages;
        stages.reserve(desc.shaderStages.size());
        for (auto& sh : desc.shaderStages) {
            auto* mod = shaderModules_.Lookup(sh);
            if (!mod) {
                return std::unexpected(RhiError::InvalidHandle);
            }
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = VK_SHADER_STAGE_ALL;  // Will be inferred by the group type
            stageInfo.module = mod->module;
            stageInfo.pName = "main";
            stages.push_back(stageInfo);
        }

        std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
        groups.reserve(desc.shaderGroups.size());
        for (auto& g : desc.shaderGroups) {
            VkRayTracingShaderGroupCreateInfoKHR groupInfo{};
            groupInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            switch (g.type) {
                case RayTracingShaderGroupType::General:
                    groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                    break;
                case RayTracingShaderGroupType::TrianglesHitGroup:
                    groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
                    break;
                case RayTracingShaderGroupType::ProceduralHitGroup:
                    groupInfo.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
                    break;
            }
            groupInfo.generalShader = g.generalShader;
            groupInfo.closestHitShader = g.closestHitShader;
            groupInfo.anyHitShader = g.anyHitShader;
            groupInfo.intersectionShader = g.intersectionShader;
            groups.push_back(groupInfo);
        }

        VkRayTracingPipelineCreateInfoKHR pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.groupCount = static_cast<uint32_t>(groups.size());
        pipelineInfo.pGroups = groups.data();
        pipelineInfo.maxPipelineRayRecursionDepth = desc.maxRecursionDepth;
        pipelineInfo.layout = layoutData->layout;

        VkPipeline pipeline = VK_NULL_HANDLE;
        VkResult r = vkCreateRayTracingPipelinesKHR(
            device_, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline
        );
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::PipelineCreationFailed);
        }

        auto [handle, data] = pipelines_.Allocate();
        if (!data) {
            vkDestroyPipeline(device_, pipeline, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->pipeline = pipeline;
        data->layout = layoutData->layout;
        data->bindPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
        return handle;
    }

    // =========================================================================
    // Pipeline destroy
    // =========================================================================

    void VulkanDevice::DestroyPipelineImpl(PipelineHandle h) {
        auto* data = pipelines_.Lookup(h);
        if (!data) {
            return;
        }
        vkDestroyPipeline(device_, data->pipeline, nullptr);
        pipelines_.Free(h);
    }

    // =========================================================================
    // Pipeline cache
    // =========================================================================

    auto VulkanDevice::CreatePipelineCacheImpl(std::span<const uint8_t> initialData) -> RhiResult<PipelineCacheHandle> {
        VkPipelineCacheCreateInfo cacheInfo{};
        cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        cacheInfo.initialDataSize = initialData.size();
        cacheInfo.pInitialData = initialData.empty() ? nullptr : initialData.data();

        VkPipelineCache cache = VK_NULL_HANDLE;
        VkResult r = vkCreatePipelineCache(device_, &cacheInfo, nullptr, &cache);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = pipelineCaches_.Allocate();
        if (!data) {
            vkDestroyPipelineCache(device_, cache, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->cache = cache;
        return handle;
    }

    auto VulkanDevice::GetPipelineCacheDataImpl(PipelineCacheHandle h) -> std::vector<uint8_t> {
        auto* data = pipelineCaches_.Lookup(h);
        if (!data) {
            return {};
        }

        size_t dataSize = 0;
        vkGetPipelineCacheData(device_, data->cache, &dataSize, nullptr);
        std::vector<uint8_t> result(dataSize);
        vkGetPipelineCacheData(device_, data->cache, &dataSize, result.data());
        return result;
    }

    void VulkanDevice::DestroyPipelineCacheImpl(PipelineCacheHandle h) {
        auto* data = pipelineCaches_.Lookup(h);
        if (!data) {
            return;
        }
        vkDestroyPipelineCache(device_, data->cache, nullptr);
        pipelineCaches_.Free(h);
    }

    // =========================================================================
    // Pipeline library (split compilation — VK_EXT_graphics_pipeline_library)
    // =========================================================================

    auto VulkanDevice::CreatePipelineLibraryPartImpl(const PipelineLibraryPartDesc& desc)
        -> RhiResult<PipelineLibraryPartHandle> {
        // VK_EXT_graphics_pipeline_library: create a partial pipeline for one of the 4 stages.
        // Extension availability was probed once at Init (PopulateCapabilities).
        // VK_GRAPHICS_PIPELINE_LIBRARY_*_BIT_EXT types only exist when the extension is enabled.
        // We check for the vkCreateGraphicsPipelines function pointer for the library-specific
        // struct types — if the extension is not available, the driver will reject the flags.
        // This is a best-effort path; callers should check capabilities before using pipeline library.

        auto* layoutData = pipelineLayouts_.Lookup(desc.pipelineLayout);
        if (!layoutData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        // Determine which library flag to use
        VkGraphicsPipelineLibraryFlagBitsEXT libraryFlag{};
        switch (desc.part) {
            case PipelineLibraryPart::VertexInput:
                libraryFlag = VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT;
                break;
            case PipelineLibraryPart::PreRasterization:
                libraryFlag = VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT;
                break;
            case PipelineLibraryPart::FragmentShader:
                libraryFlag = VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT;
                break;
            case PipelineLibraryPart::FragmentOutput:
                libraryFlag = VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT;
                break;
        }

        VkGraphicsPipelineLibraryCreateInfoEXT libraryInfo{};
        libraryInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT;
        libraryInfo.flags = libraryFlag;

        // Build a minimal graphics pipeline create info for this part.
        // Each part only uses the fields relevant to its stage; others are ignored by the driver.
        auto& pd = desc.partialDesc;

        // Shader stages (only for PreRasterization and FragmentShader parts)
        std::vector<VkPipelineShaderStageCreateInfo> stages;
        auto addStage = [&](ShaderModuleHandle h, VkShaderStageFlagBits stage) {
            if (!h.IsValid()) {
                return;
            }
            auto* mod = shaderModules_.Lookup(h);
            if (!mod) {
                return;
            }
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = stage;
            stageInfo.module = mod->module;
            stageInfo.pName = "main";
            stages.push_back(stageInfo);
        };

        if (desc.part == PipelineLibraryPart::PreRasterization) {
            if (pd.IsMeshShaderPipeline()) {
                addStage(pd.taskShader, VK_SHADER_STAGE_TASK_BIT_EXT);
                addStage(pd.meshShader, VK_SHADER_STAGE_MESH_BIT_EXT);
            } else {
                addStage(pd.vertexShader, VK_SHADER_STAGE_VERTEX_BIT);
            }
        } else if (desc.part == PipelineLibraryPart::FragmentShader) {
            addStage(pd.fragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT);
        }

        // Vertex input (VertexInput part only)
        std::vector<VkVertexInputBindingDescription> vkBindings;
        std::vector<VkVertexInputAttributeDescription> vkAttribs;
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        if (desc.part == PipelineLibraryPart::VertexInput && !pd.IsMeshShaderPipeline()) {
            for (auto& b : pd.vertexInput.bindings) {
                vkBindings.push_back(
                    {b.binding, b.stride,
                     (b.inputRate == VertexInputRate::PerInstance) ? VK_VERTEX_INPUT_RATE_INSTANCE
                                                                   : VK_VERTEX_INPUT_RATE_VERTEX}
                );
            }
            for (auto& a : pd.vertexInput.attributes) {
                vkAttribs.push_back({a.location, a.binding, ToVkFormat(a.format), a.offset});
            }
            vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(vkBindings.size());
            vertexInput.pVertexBindingDescriptions = vkBindings.data();
            vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vkAttribs.size());
            vertexInput.pVertexAttributeDescriptions = vkAttribs.data();
        }

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = ToVkPrimitiveTopology(pd.topology);
        inputAssembly.primitiveRestartEnable = pd.primitiveRestart ? VK_TRUE : VK_FALSE;

        // Rasterizer (PreRasterization part)
        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = ToVkPolygonMode(pd.polygonMode);
        rasterizer.cullMode = ToVkCullMode(pd.cullMode);
        rasterizer.frontFace = ToVkFrontFace(pd.frontFace);
        rasterizer.lineWidth = pd.lineWidth;
        rasterizer.depthClampEnable = pd.depthClampEnable ? VK_TRUE : VK_FALSE;
        rasterizer.depthBiasEnable = pd.depthBiasEnable ? VK_TRUE : VK_FALSE;

        // Viewport (dynamic)
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // Depth/stencil (FragmentShader part)
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = pd.depthTestEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = pd.depthWriteEnable ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = ToVkCompareOp(pd.depthCompareOp);
        depthStencil.stencilTestEnable = pd.stencilTestEnable ? VK_TRUE : VK_FALSE;
        depthStencil.front = ToVkStencilOpState(pd.stencilFront);
        depthStencil.back = ToVkStencilOpState(pd.stencilBack);

        // Color blend (FragmentOutput part)
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
        for (auto& cb : pd.colorBlends) {
            VkPipelineColorBlendAttachmentState att{};
            att.blendEnable = cb.blendEnable ? VK_TRUE : VK_FALSE;
            att.srcColorBlendFactor = ToVkBlendFactor(cb.srcColor);
            att.dstColorBlendFactor = ToVkBlendFactor(cb.dstColor);
            att.colorBlendOp = ToVkBlendOp(cb.colorOp);
            att.srcAlphaBlendFactor = ToVkBlendFactor(cb.srcAlpha);
            att.dstAlphaBlendFactor = ToVkBlendFactor(cb.dstAlpha);
            att.alphaBlendOp = ToVkBlendOp(cb.alphaOp);
            att.colorWriteMask = static_cast<VkColorComponentFlags>(cb.writeMask);
            blendAttachments.push_back(att);
        }
        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
        colorBlend.pAttachments = blendAttachments.data();

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(pd.sampleCount);

        // Dynamic rendering formats (FragmentOutput part)
        std::vector<VkFormat> vkColorFormats;
        for (auto f : pd.colorFormats) {
            vkColorFormats.push_back(ToVkFormat(f));
        }
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(vkColorFormats.size());
        renderingInfo.pColorAttachmentFormats = vkColorFormats.data();
        renderingInfo.depthAttachmentFormat = ToVkFormat(pd.depthFormat);
        renderingInfo.stencilAttachmentFormat = ToVkFormat(pd.stencilFormat);

        // Dynamic state
        VkDynamicState dynamicStates[]
            = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS,
               VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_BLEND_CONSTANTS};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 5;
        dynamicState.pDynamicStates = dynamicStates;

        // Chain the library create info
        libraryInfo.pNext = &renderingInfo;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &libraryInfo;
        pipelineInfo.flags
            = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT;
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.empty() ? nullptr : stages.data();
        pipelineInfo.pVertexInputState = (desc.part == PipelineLibraryPart::VertexInput) ? &vertexInput : nullptr;
        pipelineInfo.pInputAssemblyState = (desc.part == PipelineLibraryPart::VertexInput) ? &inputAssembly : nullptr;
        pipelineInfo.pViewportState = (desc.part == PipelineLibraryPart::PreRasterization) ? &viewportState : nullptr;
        pipelineInfo.pRasterizationState = (desc.part == PipelineLibraryPart::PreRasterization) ? &rasterizer : nullptr;
        pipelineInfo.pMultisampleState = (desc.part == PipelineLibraryPart::FragmentOutput) ? &multisample : nullptr;
        pipelineInfo.pDepthStencilState = (desc.part == PipelineLibraryPart::FragmentShader) ? &depthStencil : nullptr;
        pipelineInfo.pColorBlendState = (desc.part == PipelineLibraryPart::FragmentOutput) ? &colorBlend : nullptr;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = layoutData->layout;
        pipelineInfo.renderPass = VK_NULL_HANDLE;

        VkPipeline pipeline = VK_NULL_HANDLE;
        VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::PipelineCreationFailed);
        }

        auto [handle, data] = pipelineLibraryParts_.Allocate();
        if (!data) {
            vkDestroyPipeline(device_, pipeline, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->pipeline = pipeline;
        data->partType = desc.part;
        return handle;
    }

    auto VulkanDevice::LinkGraphicsPipelineImpl(const LinkedPipelineDesc& desc) -> RhiResult<PipelineHandle> {
        // Collect the 4 library parts
        VkPipeline libraries[4]{};
        uint32_t libCount = 0;

        auto addLib = [&](PipelineLibraryPartHandle h) {
            if (!h.IsValid()) {
                return;
            }
            auto* data = pipelineLibraryParts_.Lookup(h);
            if (data && libCount < 4) {
                libraries[libCount++] = data->pipeline;
            }
        };
        addLib(desc.vertexInput);
        addLib(desc.preRasterization);
        addLib(desc.fragmentShader);
        addLib(desc.fragmentOutput);

        if (libCount == 0) {
            return std::unexpected(RhiError::InvalidParameter);
        }

        VkPipelineLibraryCreateInfoKHR libraryCreateInfo{};
        libraryCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR;
        libraryCreateInfo.libraryCount = libCount;
        libraryCreateInfo.pLibraries = libraries;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.pNext = &libraryCreateInfo;
        pipelineInfo.flags = VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT;

        VkPipeline pipeline = VK_NULL_HANDLE;
        VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::PipelineCreationFailed);
        }

        auto [handle, data] = pipelines_.Allocate();
        if (!data) {
            vkDestroyPipeline(device_, pipeline, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->pipeline = pipeline;
        data->layout = VK_NULL_HANDLE;  // Linked pipeline inherits layout from parts
        data->bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        return handle;
    }

}  // namespace miki::rhi
