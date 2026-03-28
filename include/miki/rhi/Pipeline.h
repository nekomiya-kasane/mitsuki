/** @file Pipeline.h
 *  @brief Graphics, Compute, Ray Tracing pipeline descriptors, Pipeline Library (split compilation).
 *  Namespace: miki::rhi
 */
#pragma once

#include <cstdint>
#include <span>

#include "miki/rhi/Format.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"
#include "miki/rhi/Shader.h"

namespace miki::rhi {

    // =========================================================================
    // Vertex input
    // =========================================================================

    struct VertexInputBinding {
        uint32_t binding = 0;
        uint32_t stride = 0;
        VertexInputRate inputRate = VertexInputRate::PerVertex;
    };

    struct VertexInputAttribute {
        uint32_t location = 0;
        uint32_t binding = 0;
        Format format = Format::Undefined;
        uint32_t offset = 0;
    };

    struct VertexInputState {
        std::span<const VertexInputBinding> bindings;
        std::span<const VertexInputAttribute> attributes;
    };

    // =========================================================================
    // Color attachment blend
    // =========================================================================

    struct ColorAttachmentBlend {
        bool blendEnable = false;
        BlendFactor srcColor = BlendFactor::One;
        BlendFactor dstColor = BlendFactor::Zero;
        BlendOp colorOp = BlendOp::Add;
        BlendFactor srcAlpha = BlendFactor::One;
        BlendFactor dstAlpha = BlendFactor::Zero;
        BlendOp alphaOp = BlendOp::Add;
        ColorWriteMask writeMask = ColorWriteMask::All;
    };

    // =========================================================================
    // Graphics pipeline (§8.1)
    // =========================================================================

    struct GraphicsPipelineDesc {
        // Shaders
        ShaderModuleHandle vertexShader;
        ShaderModuleHandle fragmentShader;
        ShaderModuleHandle taskShader;  ///< T1 mesh shader path
        ShaderModuleHandle meshShader;  ///< T1 mesh shader path

        // Vertex input (ignored when meshShader is set)
        VertexInputState vertexInput;

        // Fixed-function state
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        bool primitiveRestart = false;
        PolygonMode polygonMode = PolygonMode::Fill;
        CullMode cullMode = CullMode::Back;
        FrontFace frontFace = FrontFace::CounterClockwise;
        float lineWidth = 1.0f;

        // Depth/stencil
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        CompareOp depthCompareOp = CompareOp::Less;
        bool stencilTestEnable = false;
        StencilOpState stencilFront;
        StencilOpState stencilBack;
        bool depthBiasEnable = false;
        bool depthClampEnable = false;

        // Blend (per color attachment)
        std::span<const ColorAttachmentBlend> colorBlends;

        // Render target formats (dynamic rendering — no render pass object)
        std::span<const Format> colorFormats;
        Format depthFormat = Format::Undefined;
        Format stencilFormat = Format::Undefined;
        uint32_t sampleCount = 1;
        uint32_t viewMask = 0;  ///< Multiview (XR)

        // Layout
        PipelineLayoutHandle pipelineLayout;

        // Specialization constants
        std::span<const SpecializationConstant> specializationConstants;

        // Pipeline cache (optional)
        PipelineCacheHandle pipelineCache;

        [[nodiscard]] constexpr auto IsMeshShaderPipeline() const noexcept -> bool { return meshShader.IsValid(); }
    };

    // =========================================================================
    // Compute pipeline (§8.2)
    // =========================================================================

    struct ComputePipelineDesc {
        ShaderModuleHandle computeShader;
        PipelineLayoutHandle pipelineLayout;
        std::span<const SpecializationConstant> specializationConstants;
        PipelineCacheHandle pipelineCache;
    };

    // =========================================================================
    // Ray tracing pipeline (§8.3, T1 only)
    // =========================================================================

    struct RayTracingPipelineDesc {
        std::span<const ShaderModuleHandle> shaderStages;
        std::span<const RayTracingShaderGroup> shaderGroups;
        uint32_t maxRecursionDepth = 1;
        PipelineLayoutHandle pipelineLayout;
        PipelineCacheHandle pipelineCache;
    };

    // =========================================================================
    // Pipeline library — split compilation (§8.5)
    // =========================================================================

    struct PipelineLibraryPartDesc {
        PipelineLibraryPart part;
        GraphicsPipelineDesc partialDesc;  ///< Only fields relevant to this part are read
        PipelineLayoutHandle pipelineLayout;
    };

    struct LinkedPipelineDesc {
        PipelineLibraryPartHandle vertexInput;
        PipelineLibraryPartHandle preRasterization;
        PipelineLibraryPartHandle fragmentShader;
        PipelineLibraryPartHandle fragmentOutput;
    };

    // =========================================================================
    // Dynamic rendering (§7.2)
    // =========================================================================

    struct RenderingAttachment {
        TextureViewHandle view;
        AttachmentLoadOp loadOp = AttachmentLoadOp::Clear;
        AttachmentStoreOp storeOp = AttachmentStoreOp::Store;
        ClearValue clearValue;
        TextureViewHandle resolveView;  ///< MSAA resolve target
    };

    struct RenderingDesc {
        Rect2D renderArea;
        std::span<const RenderingAttachment> colorAttachments;
        const RenderingAttachment* depthAttachment = nullptr;
        const RenderingAttachment* stencilAttachment = nullptr;
        uint32_t viewMask = 0;  ///< Multiview (XR)
    };

}  // namespace miki::rhi
