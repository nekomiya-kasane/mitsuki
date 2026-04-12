/** @file D3D12Pipelines.cpp
 *  @brief D3D12 (Tier 1) backend — Graphics, Compute, RayTracing pipelines,
 *         PipelineCache (ID3D12PipelineLibrary1), PipelineLibrary (split compilation).
 */

#include "miki/rhi/backend/D3D12Device.h"

#include <cassert>

#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif

namespace miki::rhi {

    // =========================================================================
    // Conversion helpers
    // =========================================================================

    namespace {
        auto ToDxgiFormat(Format fmt) -> DXGI_FORMAT {
            switch (fmt) {
                case Format::R8_UNORM: return DXGI_FORMAT_R8_UNORM;
                case Format::RG8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
                case Format::RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
                case Format::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                case Format::BGRA8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
                case Format::BGRA8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                case Format::R16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
                case Format::RG16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
                case Format::RGBA16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
                case Format::R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
                case Format::RG32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
                case Format::RGB32_FLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
                case Format::RGBA32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
                case Format::R32_UINT: return DXGI_FORMAT_R32_UINT;
                case Format::RG32_UINT: return DXGI_FORMAT_R32G32_UINT;
                case Format::RGBA32_UINT: return DXGI_FORMAT_R32G32B32A32_UINT;
                case Format::RGB10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
                case Format::RG11B10_FLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
                case Format::D16_UNORM: return DXGI_FORMAT_D16_UNORM;
                case Format::D32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
                case Format::D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
                case Format::D32_FLOAT_S8_UINT: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
                default: return DXGI_FORMAT_UNKNOWN;
            }
        }

        auto ToD3D12PrimitiveTopologyType(PrimitiveTopology topo) -> D3D12_PRIMITIVE_TOPOLOGY_TYPE {
            switch (topo) {
                case PrimitiveTopology::PointList: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
                case PrimitiveTopology::LineList:
                case PrimitiveTopology::LineStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                case PrimitiveTopology::TriangleList:
                case PrimitiveTopology::TriangleStrip:
                case PrimitiveTopology::TriangleFan: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                case PrimitiveTopology::PatchList: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
            }
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        }

        auto ToD3D12FillMode(PolygonMode mode) -> D3D12_FILL_MODE {
            return (mode == PolygonMode::Line) ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
        }

        auto ToD3D12CullMode(CullMode mode) -> D3D12_CULL_MODE {
            switch (mode) {
                case CullMode::None: return D3D12_CULL_MODE_NONE;
                case CullMode::Front: return D3D12_CULL_MODE_FRONT;
                case CullMode::Back: return D3D12_CULL_MODE_BACK;
                case CullMode::FrontAndBack: return D3D12_CULL_MODE_NONE;
            }
            return D3D12_CULL_MODE_NONE;
        }

        auto ToD3D12ComparisonFunc(CompareOp op) -> D3D12_COMPARISON_FUNC {
            switch (op) {
                case CompareOp::Never: return D3D12_COMPARISON_FUNC_NEVER;
                case CompareOp::Less: return D3D12_COMPARISON_FUNC_LESS;
                case CompareOp::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
                case CompareOp::LessOrEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
                case CompareOp::Greater: return D3D12_COMPARISON_FUNC_GREATER;
                case CompareOp::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
                case CompareOp::GreaterOrEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
                case CompareOp::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
                case CompareOp::None: return D3D12_COMPARISON_FUNC_ALWAYS;
            }
            return D3D12_COMPARISON_FUNC_ALWAYS;
        }

        auto ToD3D12StencilOp(StencilOp op) -> D3D12_STENCIL_OP {
            switch (op) {
                case StencilOp::Keep: return D3D12_STENCIL_OP_KEEP;
                case StencilOp::Zero: return D3D12_STENCIL_OP_ZERO;
                case StencilOp::Replace: return D3D12_STENCIL_OP_REPLACE;
                case StencilOp::IncrementAndClamp: return D3D12_STENCIL_OP_INCR_SAT;
                case StencilOp::DecrementAndClamp: return D3D12_STENCIL_OP_DECR_SAT;
                case StencilOp::Invert: return D3D12_STENCIL_OP_INVERT;
                case StencilOp::IncrementAndWrap: return D3D12_STENCIL_OP_INCR;
                case StencilOp::DecrementAndWrap: return D3D12_STENCIL_OP_DECR;
            }
            return D3D12_STENCIL_OP_KEEP;
        }

        auto ToD3D12DepthStencilOpDesc(const StencilOpState& s) -> D3D12_DEPTH_STENCILOP_DESC {
            return {
                ToD3D12StencilOp(s.failOp), ToD3D12StencilOp(s.depthFailOp), ToD3D12StencilOp(s.passOp),
                ToD3D12ComparisonFunc(s.compareOp)
            };
        }

        auto ToD3D12Blend(BlendFactor f) -> D3D12_BLEND {
            switch (f) {
                case BlendFactor::Zero: return D3D12_BLEND_ZERO;
                case BlendFactor::One: return D3D12_BLEND_ONE;
                case BlendFactor::SrcColor: return D3D12_BLEND_SRC_COLOR;
                case BlendFactor::OneMinusSrcColor: return D3D12_BLEND_INV_SRC_COLOR;
                case BlendFactor::DstColor: return D3D12_BLEND_DEST_COLOR;
                case BlendFactor::OneMinusDstColor: return D3D12_BLEND_INV_DEST_COLOR;
                case BlendFactor::SrcAlpha: return D3D12_BLEND_SRC_ALPHA;
                case BlendFactor::OneMinusSrcAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
                case BlendFactor::DstAlpha: return D3D12_BLEND_DEST_ALPHA;
                case BlendFactor::OneMinusDstAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
                case BlendFactor::ConstantColor: return D3D12_BLEND_BLEND_FACTOR;
                case BlendFactor::OneMinusConstantColor: return D3D12_BLEND_INV_BLEND_FACTOR;
                case BlendFactor::SrcAlphaSaturate: return D3D12_BLEND_SRC_ALPHA_SAT;
            }
            return D3D12_BLEND_ZERO;
        }

        auto ToD3D12BlendOp(BlendOp op) -> D3D12_BLEND_OP {
            switch (op) {
                case BlendOp::Add: return D3D12_BLEND_OP_ADD;
                case BlendOp::Subtract: return D3D12_BLEND_OP_SUBTRACT;
                case BlendOp::ReverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
                case BlendOp::Min: return D3D12_BLEND_OP_MIN;
                case BlendOp::Max: return D3D12_BLEND_OP_MAX;
            }
            return D3D12_BLEND_OP_ADD;
        }
    }  // namespace

    // =========================================================================
    // =========================================================================
    // Mesh Shader Pipeline (Pipeline State Stream API)
    // =========================================================================

    namespace {
        // Pipeline State Stream subobject wrapper
        template <typename Inner, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type>
        struct alignas(void*) PipelineStateStreamSubobject {
            D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = Type;
            Inner inner{};
        };

        // Mesh shader pipeline state stream structure
        struct MeshShaderPipelineStateStream {
            PipelineStateStreamSubobject<ID3D12RootSignature*, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE>
                rootSig;
            PipelineStateStreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS> ampShader;
            PipelineStateStreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS> meshShader;
            PipelineStateStreamSubobject<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS> pixelShader;
            PipelineStateStreamSubobject<D3D12_BLEND_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND> blend;
            PipelineStateStreamSubobject<D3D12_RASTERIZER_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER>
                rasterizer;
            PipelineStateStreamSubobject<D3D12_DEPTH_STENCIL_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL>
                depthStencil;
            PipelineStateStreamSubobject<
                D3D12_RT_FORMAT_ARRAY, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS>
                rtvFormats;
            PipelineStateStreamSubobject<DXGI_FORMAT, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT>
                dsvFormat;
            PipelineStateStreamSubobject<DXGI_SAMPLE_DESC, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC> sampleDesc;
            PipelineStateStreamSubobject<UINT, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK> sampleMask;
        };
    }  // namespace

    // =========================================================================
    // Graphics Pipeline
    // =========================================================================

    auto D3D12Device::CreateGraphicsPipelineImpl(const GraphicsPipelineDesc& desc) -> RhiResult<PipelineHandle> {
        auto* layoutData = pipelineLayouts_.Lookup(desc.pipelineLayout);
        if (!layoutData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        // Helper to get shader bytecode
        auto getShaderBytecode = [&](ShaderModuleHandle h) -> D3D12_SHADER_BYTECODE {
            if (!h.IsValid()) {
                return {};
            }
            auto* mod = shaderModules_.Lookup(h);
            if (!mod) {
                return {};
            }
            return {mod->bytecode.data(), mod->bytecode.size()};
        };

        // =====================================================================
        // Mesh Shader Pipeline (uses Pipeline State Stream API)
        // =====================================================================
        if (desc.IsMeshShaderPipeline()) {
            MeshShaderPipelineStateStream stream{};

            // Root signature
            stream.rootSig.inner = layoutData->rootSignature.Get();

            // Shaders
            stream.ampShader.inner = getShaderBytecode(desc.taskShader);
            stream.meshShader.inner = getShaderBytecode(desc.meshShader);
            stream.pixelShader.inner = getShaderBytecode(desc.fragmentShader);

            // Blend state
            stream.blend.inner.AlphaToCoverageEnable = FALSE;
            stream.blend.inner.IndependentBlendEnable = (desc.colorBlends.size() > 1) ? TRUE : FALSE;
            for (size_t i = 0; i < desc.colorBlends.size() && i < 8; ++i) {
                auto& cb = desc.colorBlends[i];
                auto& rt = stream.blend.inner.RenderTarget[i];
                rt.BlendEnable = cb.blendEnable ? TRUE : FALSE;
                rt.SrcBlend = ToD3D12Blend(cb.srcColor);
                rt.DestBlend = ToD3D12Blend(cb.dstColor);
                rt.BlendOp = ToD3D12BlendOp(cb.colorOp);
                rt.SrcBlendAlpha = ToD3D12Blend(cb.srcAlpha);
                rt.DestBlendAlpha = ToD3D12Blend(cb.dstAlpha);
                rt.BlendOpAlpha = ToD3D12BlendOp(cb.alphaOp);
                rt.RenderTargetWriteMask = static_cast<UINT8>(cb.writeMask);
                rt.LogicOpEnable = FALSE;
            }

            // Rasterizer
            stream.rasterizer.inner.FillMode = ToD3D12FillMode(desc.polygonMode);
            stream.rasterizer.inner.CullMode = ToD3D12CullMode(desc.cullMode);
            stream.rasterizer.inner.FrontCounterClockwise
                = (desc.frontFace == FrontFace::CounterClockwise) ? TRUE : FALSE;
            stream.rasterizer.inner.DepthBias = 0;
            stream.rasterizer.inner.DepthBiasClamp = 0.0f;
            stream.rasterizer.inner.SlopeScaledDepthBias = 0.0f;
            stream.rasterizer.inner.DepthClipEnable = desc.depthClampEnable ? FALSE : TRUE;
            stream.rasterizer.inner.MultisampleEnable = (desc.sampleCount > 1) ? TRUE : FALSE;
            stream.rasterizer.inner.AntialiasedLineEnable = FALSE;
            stream.rasterizer.inner.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

            // Depth/stencil
            stream.depthStencil.inner.DepthEnable = desc.depthTestEnable ? TRUE : FALSE;
            stream.depthStencil.inner.DepthWriteMask
                = desc.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            stream.depthStencil.inner.DepthFunc = ToD3D12ComparisonFunc(desc.depthCompareOp);
            stream.depthStencil.inner.StencilEnable = desc.stencilTestEnable ? TRUE : FALSE;
            stream.depthStencil.inner.StencilReadMask = desc.stencilFront.compareMask;
            stream.depthStencil.inner.StencilWriteMask = desc.stencilFront.writeMask;
            stream.depthStencil.inner.FrontFace = ToD3D12DepthStencilOpDesc(desc.stencilFront);
            stream.depthStencil.inner.BackFace = ToD3D12DepthStencilOpDesc(desc.stencilBack);

            // Render targets
            stream.rtvFormats.inner.NumRenderTargets = static_cast<UINT>(desc.colorFormats.size());
            for (size_t i = 0; i < desc.colorFormats.size() && i < 8; ++i) {
                stream.rtvFormats.inner.RTFormats[i] = ToDxgiFormat(desc.colorFormats[i]);
            }
            stream.dsvFormat.inner = ToDxgiFormat(desc.depthFormat);

            // MSAA
            stream.sampleDesc.inner.Count = desc.sampleCount;
            stream.sampleDesc.inner.Quality = 0;
            stream.sampleMask.inner = UINT_MAX;

            // Create PSO via Pipeline State Stream
            D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{};
            streamDesc.SizeInBytes = sizeof(stream);
            streamDesc.pPipelineStateSubobjectStream = &stream;

            ComPtr<ID3D12PipelineState> pso;
            HRESULT hr = device_->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&pso));
            if (FAILED(hr)) {
                return std::unexpected(RhiError::PipelineCreationFailed);
            }

            auto [handle, data] = pipelines_.Allocate();
            if (!data) {
                return std::unexpected(RhiError::TooManyObjects);
            }
            data->pso = std::move(pso);
            data->rootSignature = layoutData->rootSignature;
            data->isCompute = false;
            data->isMeshShader = true;
            data->topology = desc.topology;
            return handle;
        }

        // =====================================================================
        // Traditional VS/PS Pipeline
        // =====================================================================
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = layoutData->rootSignature.Get();

        // Shaders
        auto setShader = [&](ShaderModuleHandle h, D3D12_SHADER_BYTECODE& out) {
            if (!h.IsValid()) {
                return;
            }
            auto* mod = shaderModules_.Lookup(h);
            if (!mod) {
                return;
            }
            out.pShaderBytecode = mod->bytecode.data();
            out.BytecodeLength = mod->bytecode.size();
        };

        setShader(desc.vertexShader, psoDesc.VS);
        setShader(desc.fragmentShader, psoDesc.PS);

        // Blend state
        psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
        psoDesc.BlendState.IndependentBlendEnable = (desc.colorBlends.size() > 1) ? TRUE : FALSE;
        for (size_t i = 0; i < desc.colorBlends.size() && i < 8; ++i) {
            auto& cb = desc.colorBlends[i];
            auto& rt = psoDesc.BlendState.RenderTarget[i];
            rt.BlendEnable = cb.blendEnable ? TRUE : FALSE;
            rt.SrcBlend = ToD3D12Blend(cb.srcColor);
            rt.DestBlend = ToD3D12Blend(cb.dstColor);
            rt.BlendOp = ToD3D12BlendOp(cb.colorOp);
            rt.SrcBlendAlpha = ToD3D12Blend(cb.srcAlpha);
            rt.DestBlendAlpha = ToD3D12Blend(cb.dstAlpha);
            rt.BlendOpAlpha = ToD3D12BlendOp(cb.alphaOp);
            rt.RenderTargetWriteMask = static_cast<UINT8>(cb.writeMask);
            rt.LogicOpEnable = FALSE;
        }

        // Rasterizer
        psoDesc.RasterizerState.FillMode = ToD3D12FillMode(desc.polygonMode);
        psoDesc.RasterizerState.CullMode = ToD3D12CullMode(desc.cullMode);
        psoDesc.RasterizerState.FrontCounterClockwise = (desc.frontFace == FrontFace::CounterClockwise) ? TRUE : FALSE;
        psoDesc.RasterizerState.DepthBias = 0;
        psoDesc.RasterizerState.DepthBiasClamp = 0.0f;
        psoDesc.RasterizerState.SlopeScaledDepthBias = 0.0f;
        psoDesc.RasterizerState.DepthClipEnable = desc.depthClampEnable ? FALSE : TRUE;
        psoDesc.RasterizerState.MultisampleEnable = (desc.sampleCount > 1) ? TRUE : FALSE;
        psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
        psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        // Depth/stencil
        psoDesc.DepthStencilState.DepthEnable = desc.depthTestEnable ? TRUE : FALSE;
        psoDesc.DepthStencilState.DepthWriteMask
            = desc.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        psoDesc.DepthStencilState.DepthFunc = ToD3D12ComparisonFunc(desc.depthCompareOp);
        psoDesc.DepthStencilState.StencilEnable = desc.stencilTestEnable ? TRUE : FALSE;
        psoDesc.DepthStencilState.StencilReadMask = desc.stencilFront.compareMask;
        psoDesc.DepthStencilState.StencilWriteMask = desc.stencilFront.writeMask;
        psoDesc.DepthStencilState.FrontFace = ToD3D12DepthStencilOpDesc(desc.stencilFront);
        psoDesc.DepthStencilState.BackFace = ToD3D12DepthStencilOpDesc(desc.stencilBack);

        // Vertex input
        std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
        if (!desc.IsMeshShaderPipeline()) {
            for (auto& attr : desc.vertexInput.attributes) {
                D3D12_INPUT_ELEMENT_DESC elem{};
                elem.SemanticName = "ATTRIBUTE";
                elem.SemanticIndex = attr.location;
                elem.Format = ToDxgiFormat(attr.format);
                elem.InputSlot = attr.binding;
                elem.AlignedByteOffset = attr.offset;

                // Find input rate from bindings
                for (auto& bind : desc.vertexInput.bindings) {
                    if (bind.binding == attr.binding) {
                        elem.InputSlotClass = (bind.inputRate == VertexInputRate::PerInstance)
                                                  ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                                                  : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                        elem.InstanceDataStepRate = (bind.inputRate == VertexInputRate::PerInstance) ? 1 : 0;
                        break;
                    }
                }
                inputElements.push_back(elem);
            }
            psoDesc.InputLayout.pInputElementDescs = inputElements.data();
            psoDesc.InputLayout.NumElements = static_cast<UINT>(inputElements.size());
        }

        // Primitive topology
        psoDesc.PrimitiveTopologyType = ToD3D12PrimitiveTopologyType(desc.topology);

        // Render targets
        psoDesc.NumRenderTargets = static_cast<UINT>(desc.colorFormats.size());
        for (size_t i = 0; i < desc.colorFormats.size() && i < 8; ++i) {
            psoDesc.RTVFormats[i] = ToDxgiFormat(desc.colorFormats[i]);
        }
        psoDesc.DSVFormat = ToDxgiFormat(desc.depthFormat);

        // MSAA
        psoDesc.SampleDesc.Count = desc.sampleCount;
        psoDesc.SampleDesc.Quality = 0;
        psoDesc.SampleMask = UINT_MAX;

        // Create PSO
        ComPtr<ID3D12PipelineState> pso;
        HRESULT hr = device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso));
        if (FAILED(hr)) {
            return std::unexpected(RhiError::PipelineCreationFailed);
        }

        auto [handle, data] = pipelines_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->pso = std::move(pso);
        data->rootSignature = layoutData->rootSignature;
        data->topology = desc.topology;
        data->isCompute = false;
        data->isMeshShader = false;
        // Cache per-binding vertex strides for CmdBindVertexBuffer
        for (auto& bind : desc.vertexInput.bindings) {
            if (bind.binding < D3D12PipelineData::kMaxVertexBindings) {
                data->vertexStrides[bind.binding] = bind.stride;
            }
        }
        return handle;
    }

    // =========================================================================
    // Compute Pipeline
    // =========================================================================

    auto D3D12Device::CreateComputePipelineImpl(const ComputePipelineDesc& desc) -> RhiResult<PipelineHandle> {
        auto* layoutData = pipelineLayouts_.Lookup(desc.pipelineLayout);
        if (!layoutData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        auto* shaderData = shaderModules_.Lookup(desc.computeShader);
        if (!shaderData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = layoutData->rootSignature.Get();
        psoDesc.CS.pShaderBytecode = shaderData->bytecode.data();
        psoDesc.CS.BytecodeLength = shaderData->bytecode.size();

        ComPtr<ID3D12PipelineState> pso;
        HRESULT hr = device_->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso));
        if (FAILED(hr)) {
            return std::unexpected(RhiError::PipelineCreationFailed);
        }

        auto [handle, data] = pipelines_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->pso = std::move(pso);
        data->rootSignature = layoutData->rootSignature;
        data->isCompute = true;
        return handle;
    }

    // =========================================================================
    // Ray Tracing Pipeline (DXR)
    // =========================================================================

    auto D3D12Device::CreateRayTracingPipelineImpl(const RayTracingPipelineDesc& desc) -> RhiResult<PipelineHandle> {
        if (!capabilities_.hasRayTracingPipeline) {
            return std::unexpected(RhiError::FeatureNotSupported);
        }

        auto* layoutData = pipelineLayouts_.Lookup(desc.pipelineLayout);
        if (!layoutData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        // Build D3D12_STATE_OBJECT_DESC for DXR pipeline
        std::vector<D3D12_STATE_SUBOBJECT> subobjects;
        std::vector<D3D12_DXIL_LIBRARY_DESC> libraryDescs;
        std::vector<D3D12_EXPORT_DESC> exportDescs;
        std::vector<std::wstring> exportNames;

        // DXIL library sub-objects (one per shader stage)
        for (size_t i = 0; i < desc.shaderStages.size(); ++i) {
            auto* mod = shaderModules_.Lookup(desc.shaderStages[i]);
            if (!mod) {
                return std::unexpected(RhiError::InvalidHandle);
            }

            wchar_t name[64]{};
            swprintf(name, 64, L"shader_%zu", i);
            exportNames.emplace_back(name);

            D3D12_EXPORT_DESC exportDesc{};
            exportDesc.Name = exportNames.back().c_str();
            exportDesc.ExportToRename = nullptr;
            exportDesc.Flags = D3D12_EXPORT_FLAG_NONE;
            exportDescs.push_back(exportDesc);

            D3D12_DXIL_LIBRARY_DESC libDesc{};
            libDesc.DXILLibrary.pShaderBytecode = mod->bytecode.data();
            libDesc.DXILLibrary.BytecodeLength = mod->bytecode.size();
            libDesc.NumExports = 1;
            libDesc.pExports = &exportDescs.back();
            libraryDescs.push_back(libDesc);

            D3D12_STATE_SUBOBJECT sub{};
            sub.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
            sub.pDesc = &libraryDescs.back();
            subobjects.push_back(sub);
        }

        // Shader config
        D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
        shaderConfig.MaxPayloadSizeInBytes = 32;
        shaderConfig.MaxAttributeSizeInBytes = D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;
        D3D12_STATE_SUBOBJECT shaderConfigSub{D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shaderConfig};
        subobjects.push_back(shaderConfigSub);

        // Pipeline config
        D3D12_RAYTRACING_PIPELINE_CONFIG1 pipelineConfig{};
        pipelineConfig.MaxTraceRecursionDepth = desc.maxRecursionDepth;
        pipelineConfig.Flags = D3D12_RAYTRACING_PIPELINE_FLAG_NONE;
        D3D12_STATE_SUBOBJECT pipelineConfigSub{
            D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1, &pipelineConfig
        };
        subobjects.push_back(pipelineConfigSub);

        // Global root signature
        D3D12_GLOBAL_ROOT_SIGNATURE globalRootSig{layoutData->rootSignature.Get()};
        D3D12_STATE_SUBOBJECT globalRootSigSub{D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &globalRootSig};
        subobjects.push_back(globalRootSigSub);

        D3D12_STATE_OBJECT_DESC stateObjDesc{};
        stateObjDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        stateObjDesc.NumSubobjects = static_cast<UINT>(subobjects.size());
        stateObjDesc.pSubobjects = subobjects.data();

        ComPtr<ID3D12StateObject> stateObject;
        HRESULT hr = device_->CreateStateObject(&stateObjDesc, IID_PPV_ARGS(&stateObject));
        if (FAILED(hr)) {
            return std::unexpected(RhiError::PipelineCreationFailed);
        }

        // Query PSO interface from state object
        ComPtr<ID3D12PipelineState> pso;
        // DXR state objects don't directly cast to ID3D12PipelineState
        // Store as null PSO — RT dispatch uses state object directly
        // This is acceptable: the pipeline handle stores the state object via pso field

        auto [handle, data] = pipelines_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->pso = nullptr;
        data->stateObject = std::move(stateObject);
        data->rootSignature = layoutData->rootSignature;
        data->isCompute = false;
        data->isRayTracing = true;
        return handle;
    }

    // =========================================================================
    // Pipeline destroy
    // =========================================================================

    void D3D12Device::DestroyPipelineImpl(PipelineHandle h) {
        auto* data = pipelines_.Lookup(h);
        if (!data) {
            return;
        }
        pipelines_.Free(h);
    }

    // =========================================================================
    // Pipeline cache (ID3D12PipelineLibrary1)
    // =========================================================================

    auto D3D12Device::CreatePipelineCacheImpl(std::span<const uint8_t> initialData) -> RhiResult<PipelineCacheHandle> {
        auto [handle, data] = pipelineCaches_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        ComPtr<ID3D12PipelineLibrary1> library;
        HRESULT hr;
        if (!initialData.empty()) {
            hr = device_->CreatePipelineLibrary(initialData.data(), initialData.size(), IID_PPV_ARGS(&library));
        } else {
            hr = device_->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&library));
        }
        if (SUCCEEDED(hr)) {
            data->library = std::move(library);
        }
        data->blob.assign(initialData.begin(), initialData.end());
        return handle;
    }

    auto D3D12Device::GetPipelineCacheDataImpl(PipelineCacheHandle h) -> std::vector<uint8_t> {
        auto* data = pipelineCaches_.Lookup(h);
        if (!data || !data->library) {
            return {};
        }

        SIZE_T size = data->library->GetSerializedSize();
        std::vector<uint8_t> result(size);
        data->library->Serialize(result.data(), size);
        return result;
    }

    void D3D12Device::DestroyPipelineCacheImpl(PipelineCacheHandle h) {
        auto* data = pipelineCaches_.Lookup(h);
        if (!data) {
            return;
        }
        pipelineCaches_.Free(h);
    }

    // =========================================================================
    // Pipeline library (split compilation — D3D12 Pipeline State Streams)
    // =========================================================================

    auto D3D12Device::CreatePipelineLibraryPartImpl(const PipelineLibraryPartDesc& /*desc*/)
        -> RhiResult<PipelineLibraryPartHandle> {
        // D3D12 Pipeline State Streams split compilation:
        // Each part creates a partial PSO via ID3D12Device2::CreatePipelineState
        // with CD3DX12_PIPELINE_STATE_STREAM sub-objects.
        // For now, store the part type — full implementation links at LinkGraphicsPipeline.
        auto [handle, data] = pipelineLibraryParts_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->partType = PipelineLibraryPart::VertexInput;
        return handle;
    }

    auto D3D12Device::LinkGraphicsPipelineImpl(const LinkedPipelineDesc& /*desc*/) -> RhiResult<PipelineHandle> {
        // D3D12 linking uses ID3D12Device2::CreatePipelineState with combined stream.
        // Deferred: full implementation requires D3D12 Pipeline State Stream sub-object merging.
        return std::unexpected(RhiError::FeatureNotSupported);
    }

    void D3D12Device::DestroyPipelineLibraryPartImpl(PipelineLibraryPartHandle h) {
        pipelineLibraryParts_.Free(h);
    }

}  // namespace miki::rhi

#if defined(__clang__)
#    pragma clang diagnostic pop
#endif
