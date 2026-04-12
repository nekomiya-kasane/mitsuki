/** @file WebGPUPipelines.cpp
 *  @brief WebGPU / Dawn (Tier 3) backend — graphics pipeline, compute pipeline, pipeline cache.
 */

#include "miki/rhi/backend/WebGPUDevice.h"
#include "miki/debug/StructuredLogger.h"

#include <dawn/webgpu.h>

namespace miki::rhi {

    // =========================================================================
    // Helpers
    // =========================================================================

    static auto ToWGPUPrimitiveTopology(PrimitiveTopology topo) -> WGPUPrimitiveTopology {
        switch (topo) {
            case PrimitiveTopology::PointList: return WGPUPrimitiveTopology_PointList;
            case PrimitiveTopology::LineList: return WGPUPrimitiveTopology_LineList;
            case PrimitiveTopology::LineStrip: return WGPUPrimitiveTopology_LineStrip;
            case PrimitiveTopology::TriangleList: return WGPUPrimitiveTopology_TriangleList;
            case PrimitiveTopology::TriangleStrip: return WGPUPrimitiveTopology_TriangleStrip;
            default: return WGPUPrimitiveTopology_TriangleList;
        }
    }

    static auto ToWGPUFrontFace(FrontFace face) -> WGPUFrontFace {
        switch (face) {
            case FrontFace::CounterClockwise: return WGPUFrontFace_CCW;
            case FrontFace::Clockwise: return WGPUFrontFace_CW;
            default: return WGPUFrontFace_CCW;
        }
    }

    static auto ToWGPUCullMode(CullMode mode) -> WGPUCullMode {
        switch (mode) {
            case CullMode::None: return WGPUCullMode_None;
            case CullMode::Front: return WGPUCullMode_Front;
            case CullMode::Back: return WGPUCullMode_Back;
            default: return WGPUCullMode_None;
        }
    }

    static auto ToWGPUCompareFunction(CompareOp op) -> WGPUCompareFunction {
        switch (op) {
            case CompareOp::Never: return WGPUCompareFunction_Never;
            case CompareOp::Less: return WGPUCompareFunction_Less;
            case CompareOp::Equal: return WGPUCompareFunction_Equal;
            case CompareOp::LessOrEqual: return WGPUCompareFunction_LessEqual;
            case CompareOp::Greater: return WGPUCompareFunction_Greater;
            case CompareOp::NotEqual: return WGPUCompareFunction_NotEqual;
            case CompareOp::GreaterOrEqual: return WGPUCompareFunction_GreaterEqual;
            case CompareOp::Always: return WGPUCompareFunction_Always;
            default: return WGPUCompareFunction_Always;
        }
    }

    static auto ToWGPUStencilOperation(StencilOp op) -> WGPUStencilOperation {
        switch (op) {
            case StencilOp::Keep: return WGPUStencilOperation_Keep;
            case StencilOp::Zero: return WGPUStencilOperation_Zero;
            case StencilOp::Replace: return WGPUStencilOperation_Replace;
            case StencilOp::IncrementAndClamp: return WGPUStencilOperation_IncrementClamp;
            case StencilOp::DecrementAndClamp: return WGPUStencilOperation_DecrementClamp;
            case StencilOp::Invert: return WGPUStencilOperation_Invert;
            case StencilOp::IncrementAndWrap: return WGPUStencilOperation_IncrementWrap;
            case StencilOp::DecrementAndWrap: return WGPUStencilOperation_DecrementWrap;
            default: return WGPUStencilOperation_Keep;
        }
    }

    static auto ToWGPUBlendFactor(BlendFactor f) -> WGPUBlendFactor {
        switch (f) {
            case BlendFactor::Zero: return WGPUBlendFactor_Zero;
            case BlendFactor::One: return WGPUBlendFactor_One;
            case BlendFactor::SrcColor: return WGPUBlendFactor_Src;
            case BlendFactor::OneMinusSrcColor: return WGPUBlendFactor_OneMinusSrc;
            case BlendFactor::DstColor: return WGPUBlendFactor_Dst;
            case BlendFactor::OneMinusDstColor: return WGPUBlendFactor_OneMinusDst;
            case BlendFactor::SrcAlpha: return WGPUBlendFactor_SrcAlpha;
            case BlendFactor::OneMinusSrcAlpha: return WGPUBlendFactor_OneMinusSrcAlpha;
            case BlendFactor::DstAlpha: return WGPUBlendFactor_DstAlpha;
            case BlendFactor::OneMinusDstAlpha: return WGPUBlendFactor_OneMinusDstAlpha;
            case BlendFactor::ConstantColor: return WGPUBlendFactor_Constant;
            case BlendFactor::OneMinusConstantColor: return WGPUBlendFactor_OneMinusConstant;
            case BlendFactor::SrcAlphaSaturate: return WGPUBlendFactor_SrcAlphaSaturated;
            default: return WGPUBlendFactor_One;
        }
    }

    static auto ToWGPUBlendOperation(BlendOp op) -> WGPUBlendOperation {
        switch (op) {
            case BlendOp::Add: return WGPUBlendOperation_Add;
            case BlendOp::Subtract: return WGPUBlendOperation_Subtract;
            case BlendOp::ReverseSubtract: return WGPUBlendOperation_ReverseSubtract;
            case BlendOp::Min: return WGPUBlendOperation_Min;
            case BlendOp::Max: return WGPUBlendOperation_Max;
            default: return WGPUBlendOperation_Add;
        }
    }

    static auto ToWGPUVertexFormat(Format fmt) -> WGPUVertexFormat {
        switch (fmt) {
            // 8-bit
            case Format::RG8_UNORM: return WGPUVertexFormat_Unorm8x2;
            case Format::RGBA8_UNORM: return WGPUVertexFormat_Unorm8x4;
            case Format::RG8_SNORM: return WGPUVertexFormat_Snorm8x2;
            case Format::RGBA8_SNORM: return WGPUVertexFormat_Snorm8x4;
            case Format::RG8_UINT: return WGPUVertexFormat_Uint8x2;
            case Format::RGBA8_UINT: return WGPUVertexFormat_Uint8x4;
            case Format::RG8_SINT: return WGPUVertexFormat_Sint8x2;
            case Format::RGBA8_SINT: return WGPUVertexFormat_Sint8x4;
            // 16-bit
            case Format::RG16_UINT: return WGPUVertexFormat_Uint16x2;
            case Format::RGBA16_UINT: return WGPUVertexFormat_Uint16x4;
            case Format::RG16_SINT: return WGPUVertexFormat_Sint16x2;
            case Format::RGBA16_SINT: return WGPUVertexFormat_Sint16x4;
            case Format::RG16_UNORM: return WGPUVertexFormat_Unorm16x2;
            case Format::RGBA16_UNORM: return WGPUVertexFormat_Unorm16x4;
            case Format::RG16_SNORM: return WGPUVertexFormat_Snorm16x2;
            case Format::RGBA16_SNORM: return WGPUVertexFormat_Snorm16x4;
            case Format::RG16_FLOAT: return WGPUVertexFormat_Float16x2;
            case Format::RGBA16_FLOAT: return WGPUVertexFormat_Float16x4;
            // 32-bit
            case Format::R32_FLOAT: return WGPUVertexFormat_Float32;
            case Format::RG32_FLOAT: return WGPUVertexFormat_Float32x2;
            case Format::RGB32_FLOAT: return WGPUVertexFormat_Float32x3;
            case Format::RGBA32_FLOAT: return WGPUVertexFormat_Float32x4;
            case Format::R32_UINT: return WGPUVertexFormat_Uint32;
            case Format::RG32_UINT: return WGPUVertexFormat_Uint32x2;
            case Format::RGB32_UINT: return WGPUVertexFormat_Uint32x3;
            case Format::RGBA32_UINT: return WGPUVertexFormat_Uint32x4;
            case Format::R32_SINT: return WGPUVertexFormat_Sint32;
            case Format::RG32_SINT: return WGPUVertexFormat_Sint32x2;
            case Format::RGB32_SINT: return WGPUVertexFormat_Sint32x3;
            case Format::RGBA32_SINT: return WGPUVertexFormat_Sint32x4;
            default: return WGPUVertexFormat_Float32x4;
        }
    }

    static auto ToWGPUTextureFormat(Format fmt) -> WGPUTextureFormat {
        switch (fmt) {
            case Format::RGBA8_UNORM: return WGPUTextureFormat_RGBA8Unorm;
            case Format::RGBA8_SRGB: return WGPUTextureFormat_RGBA8UnormSrgb;
            case Format::BGRA8_UNORM: return WGPUTextureFormat_BGRA8Unorm;
            case Format::BGRA8_SRGB: return WGPUTextureFormat_BGRA8UnormSrgb;
            case Format::RGB10A2_UNORM: return WGPUTextureFormat_RGB10A2Unorm;
            case Format::RGBA16_FLOAT: return WGPUTextureFormat_RGBA16Float;
            case Format::R8_UNORM: return WGPUTextureFormat_R8Unorm;
            case Format::RG8_UNORM: return WGPUTextureFormat_RG8Unorm;
            case Format::R16_FLOAT: return WGPUTextureFormat_R16Float;
            case Format::RG16_FLOAT: return WGPUTextureFormat_RG16Float;
            case Format::R32_FLOAT: return WGPUTextureFormat_R32Float;
            case Format::RG32_FLOAT: return WGPUTextureFormat_RG32Float;
            case Format::RGBA32_FLOAT: return WGPUTextureFormat_RGBA32Float;
            case Format::D16_UNORM: return WGPUTextureFormat_Depth16Unorm;
            case Format::D24_UNORM_S8_UINT: return WGPUTextureFormat_Depth24PlusStencil8;
            case Format::D32_FLOAT: return WGPUTextureFormat_Depth32Float;
            case Format::D32_FLOAT_S8_UINT: return WGPUTextureFormat_Depth32FloatStencil8;
            default: return WGPUTextureFormat_RGBA8Unorm;
        }
    }

    // =========================================================================
    // Graphics Pipeline
    // =========================================================================

    auto WebGPUDevice::CreateGraphicsPipelineImpl(const GraphicsPipelineDesc& desc) -> RhiResult<PipelineHandle> {
        auto [handle, data] = pipelines_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        data->isCompute = false;

        // --- Pipeline layout ---
        auto* layoutData = pipelineLayouts_.Lookup(desc.pipelineLayout);
        if (!layoutData) {
            pipelines_.Free(handle);
            return std::unexpected(RhiError::InvalidHandle);
        }

        // --- Vertex state ---
        std::vector<WGPUVertexBufferLayout> vertexBuffers;
        std::vector<std::vector<WGPUVertexAttribute>> allAttributes;

        for (const auto& binding : desc.vertexInput.bindings) {
            std::vector<WGPUVertexAttribute> attrs;
            for (const auto& attr : desc.vertexInput.attributes) {
                if (attr.binding == binding.binding) {
                    WGPUVertexAttribute a{};
                    a.format = ToWGPUVertexFormat(attr.format);
                    a.offset = attr.offset;
                    a.shaderLocation = attr.location;
                    attrs.push_back(a);
                }
            }
            allAttributes.push_back(std::move(attrs));

            WGPUVertexBufferLayout vbl{};
            vbl.arrayStride = binding.stride;
            vbl.stepMode = (binding.inputRate == VertexInputRate::PerInstance) ? WGPUVertexStepMode_Instance
                                                                               : WGPUVertexStepMode_Vertex;
            vertexBuffers.push_back(vbl);

            data->vertexBindings.push_back({
                .binding = binding.binding,
                .stride = binding.stride,
                .perInstance = (binding.inputRate == VertexInputRate::PerInstance),
            });
        }

        // Patch attribute pointers (must survive until pipeline creation)
        for (size_t i = 0; i < vertexBuffers.size(); ++i) {
            vertexBuffers[i].attributeCount = static_cast<uint32_t>(allAttributes[i].size());
            vertexBuffers[i].attributes = allAttributes[i].data();
        }

        // --- Vertex shader ---
        auto* vsData = shaderModules_.Lookup(desc.vertexShader);
        if (!vsData) {
            pipelines_.Free(handle);
            return std::unexpected(RhiError::InvalidHandle);
        }

        WGPUVertexState vertexState{};
        vertexState.module = vsData->module;
        vertexState.entryPoint = {.data = vsData->entryPoint.c_str(), .length = WGPU_STRLEN};
        vertexState.bufferCount = static_cast<uint32_t>(vertexBuffers.size());
        vertexState.buffers = vertexBuffers.data();

        // --- Fragment shader ---
        WGPUFragmentState fragmentState{};
        std::vector<WGPUColorTargetState> colorTargets;
        std::vector<WGPUBlendState> blendStates;
        bool hasFragment = desc.fragmentShader.IsValid();

        if (hasFragment) {
            auto* fsData = shaderModules_.Lookup(desc.fragmentShader);
            if (!fsData) {
                pipelines_.Free(handle);
                return std::unexpected(RhiError::InvalidHandle);
            }
            fragmentState.module = fsData->module;
            fragmentState.entryPoint = {.data = fsData->entryPoint.c_str(), .length = WGPU_STRLEN};

            // colorBlends[] and colorFormats[] are parallel arrays
            size_t colorCount = desc.colorFormats.size();
            for (size_t i = 0; i < colorCount; ++i) {
                WGPUBlendState blend{};
                bool blendEnabled = false;
                WGPUColorWriteMask writeMask = WGPUColorWriteMask_All;

                if (i < desc.colorBlends.size()) {
                    const auto& cb = desc.colorBlends[i];
                    blendEnabled = cb.blendEnable;
                    blend.color.srcFactor = ToWGPUBlendFactor(cb.srcColor);
                    blend.color.dstFactor = ToWGPUBlendFactor(cb.dstColor);
                    blend.color.operation = ToWGPUBlendOperation(cb.colorOp);
                    blend.alpha.srcFactor = ToWGPUBlendFactor(cb.srcAlpha);
                    blend.alpha.dstFactor = ToWGPUBlendFactor(cb.dstAlpha);
                    blend.alpha.operation = ToWGPUBlendOperation(cb.alphaOp);
                    writeMask = static_cast<WGPUColorWriteMask>(cb.writeMask);
                }
                blendStates.push_back(blend);

                WGPUColorTargetState ct{};
                ct.format = ToWGPUTextureFormat(desc.colorFormats[i]);
                ct.writeMask = writeMask;
                ct.blend = blendEnabled ? &blendStates.back() : nullptr;
                colorTargets.push_back(ct);
            }

            fragmentState.targetCount = static_cast<uint32_t>(colorTargets.size());
            fragmentState.targets = colorTargets.data();
        }

        // --- Primitive state ---
        WGPUPrimitiveState primitive{};
        primitive.topology = ToWGPUPrimitiveTopology(desc.topology);
        primitive.stripIndexFormat
            = (desc.topology == PrimitiveTopology::TriangleStrip || desc.topology == PrimitiveTopology::LineStrip)
                  ? WGPUIndexFormat_Uint32
                  : WGPUIndexFormat_Undefined;
        primitive.frontFace = ToWGPUFrontFace(desc.frontFace);
        primitive.cullMode = ToWGPUCullMode(desc.cullMode);
        primitive.unclippedDepth = desc.depthClampEnable;

        // --- Depth stencil ---
        WGPUDepthStencilState depthStencilState{};
        bool hasDepth = (desc.depthFormat != Format::Undefined);
        if (hasDepth) {
            depthStencilState.format = ToWGPUTextureFormat(desc.depthFormat);
            depthStencilState.depthWriteEnabled
                = desc.depthWriteEnable ? WGPUOptionalBool_True : WGPUOptionalBool_False;
            depthStencilState.depthCompare
                = desc.depthTestEnable ? ToWGPUCompareFunction(desc.depthCompareOp) : WGPUCompareFunction_Always;
            depthStencilState.stencilFront.compare = ToWGPUCompareFunction(desc.stencilFront.compareOp);
            depthStencilState.stencilFront.failOp = ToWGPUStencilOperation(desc.stencilFront.failOp);
            depthStencilState.stencilFront.depthFailOp = ToWGPUStencilOperation(desc.stencilFront.depthFailOp);
            depthStencilState.stencilFront.passOp = ToWGPUStencilOperation(desc.stencilFront.passOp);
            depthStencilState.stencilBack.compare = ToWGPUCompareFunction(desc.stencilBack.compareOp);
            depthStencilState.stencilBack.failOp = ToWGPUStencilOperation(desc.stencilBack.failOp);
            depthStencilState.stencilBack.depthFailOp = ToWGPUStencilOperation(desc.stencilBack.depthFailOp);
            depthStencilState.stencilBack.passOp = ToWGPUStencilOperation(desc.stencilBack.passOp);
            depthStencilState.stencilReadMask = desc.stencilFront.compareMask;
            depthStencilState.stencilWriteMask = desc.stencilFront.writeMask;
            // WebGPU depth bias is set at pipeline creation (not dynamic)
            depthStencilState.depthBias = 0;
            depthStencilState.depthBiasSlopeScale = 0.0f;
            depthStencilState.depthBiasClamp = 0.0f;

            data->stencilFrontCompare = depthStencilState.stencilFront.compare;
            data->stencilBackCompare = depthStencilState.stencilBack.compare;
            data->stencilReadMask = desc.stencilFront.compareMask;
            data->stencilWriteMask = desc.stencilFront.writeMask;
        }

        // --- Multisample ---
        WGPUMultisampleState multisample{};
        multisample.count = desc.sampleCount;
        multisample.mask = 0xFFFFFFFF;
        multisample.alphaToCoverageEnabled = false;

        // --- Assemble render pipeline descriptor ---
        WGPURenderPipelineDescriptor pipelineDesc{};
        pipelineDesc.layout = layoutData->layout;
        pipelineDesc.vertex = vertexState;
        pipelineDesc.primitive = primitive;
        pipelineDesc.depthStencil = hasDepth ? &depthStencilState : nullptr;
        pipelineDesc.multisample = multisample;
        pipelineDesc.fragment = hasFragment ? &fragmentState : nullptr;

        data->renderPipeline = wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
        if (!data->renderPipeline) {
            pipelines_.Free(handle);
            return std::unexpected(RhiError::PipelineCreationFailed);
        }

        return handle;
    }

    // =========================================================================
    // Compute Pipeline
    // =========================================================================

    auto WebGPUDevice::CreateComputePipelineImpl(const ComputePipelineDesc& desc) -> RhiResult<PipelineHandle> {
        auto [handle, data] = pipelines_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        data->isCompute = true;

        auto* layoutData = pipelineLayouts_.Lookup(desc.pipelineLayout);
        if (!layoutData) {
            pipelines_.Free(handle);
            return std::unexpected(RhiError::InvalidHandle);
        }

        auto* csData = shaderModules_.Lookup(desc.computeShader);
        if (!csData) {
            pipelines_.Free(handle);
            return std::unexpected(RhiError::InvalidHandle);
        }

        WGPUComputePipelineDescriptor pipelineDesc{};
        pipelineDesc.layout = layoutData->layout;
        pipelineDesc.compute.module = csData->module;
        pipelineDesc.compute.entryPoint = {.data = csData->entryPoint.c_str(), .length = WGPU_STRLEN};

        data->computePipeline = wgpuDeviceCreateComputePipeline(device_, &pipelineDesc);
        if (!data->computePipeline) {
            pipelines_.Free(handle);
            return std::unexpected(RhiError::PipelineCreationFailed);
        }

        return handle;
    }

    // =========================================================================
    // Ray Tracing Pipeline (not available on T3)
    // =========================================================================

    auto WebGPUDevice::CreateRayTracingPipelineImpl([[maybe_unused]] const RayTracingPipelineDesc& desc)
        -> RhiResult<PipelineHandle> {
        return std::unexpected(RhiError::FeatureNotSupported);
    }

    // =========================================================================
    // Pipeline destroy
    // =========================================================================

    void WebGPUDevice::DestroyPipelineImpl(PipelineHandle h) {
        auto* data = pipelines_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->renderPipeline) {
            wgpuRenderPipelineRelease(data->renderPipeline);
        }
        if (data->computePipeline) {
            wgpuComputePipelineRelease(data->computePipeline);
        }
        pipelines_.Free(h);
    }

    // =========================================================================
    // Pipeline cache (no native cache in WebGPU — in-memory blob store)
    // =========================================================================

    auto WebGPUDevice::CreatePipelineCacheImpl(std::span<const uint8_t> initialData) -> RhiResult<PipelineCacheHandle> {
        auto [handle, data] = pipelineCaches_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->blob.assign(initialData.begin(), initialData.end());
        return handle;
    }

    auto WebGPUDevice::GetPipelineCacheDataImpl(PipelineCacheHandle h) -> std::vector<uint8_t> {
        auto* data = pipelineCaches_.Lookup(h);
        if (!data) {
            return {};
        }
        return data->blob;
    }

    void WebGPUDevice::DestroyPipelineCacheImpl(PipelineCacheHandle h) {
        pipelineCaches_.Free(h);
    }

    // =========================================================================
    // Pipeline library (not available on T3)
    // =========================================================================

    auto WebGPUDevice::CreatePipelineLibraryPartImpl([[maybe_unused]] const PipelineLibraryPartDesc& desc)
        -> RhiResult<PipelineLibraryPartHandle> {
        return std::unexpected(RhiError::FeatureNotSupported);
    }

    auto WebGPUDevice::LinkGraphicsPipelineImpl([[maybe_unused]] const LinkedPipelineDesc& desc)
        -> RhiResult<PipelineHandle> {
        return std::unexpected(RhiError::FeatureNotSupported);
    }

    void WebGPUDevice::DestroyPipelineLibraryPartImpl(PipelineLibraryPartHandle) {}

}  // namespace miki::rhi
