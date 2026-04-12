/** @file D3D12BlitPipeline.cpp
 *  @brief Implementation of the internal D3D12 blit pipeline.
 */

#include "D3D12BlitPipeline.h"

#include <d3dcompiler.h>

#include <cstring>

#pragma comment(lib, "d3dcompiler.lib")

namespace miki::rhi {

    // =========================================================================
    // Embedded HLSL source for the blit shaders
    // =========================================================================

    static constexpr const char* kBlitHLSL = R"(
cbuffer BlitConstants : register(b0) {
    float2 srcMin;  // UV min
    float2 srcMax;  // UV max
};

Texture2D<float4> srcTexture : register(t0);
SamplerState srcSampler : register(s0);

struct VSOutput {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

// Fullscreen triangle: 3 vertices cover the entire screen.
// No vertex buffer needed — positions generated from SV_VertexID.
VSOutput VSMain(uint vertexID : SV_VertexID) {
    // Generate fullscreen triangle vertices:
    //   ID 0: (-1, -1)  ID 1: (-1, 3)  ID 2: (3, -1)
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    VSOutput output;
    output.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    // Map [0,1] UV to source region UV
    output.uv = lerp(srcMin, srcMax, uv);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target {
    return srcTexture.Sample(srcSampler, input.uv);
}
)";

    // =========================================================================
    // Compile shader helper
    // =========================================================================

    static auto CompileShader(const char* source, const char* entryPoint, const char* target) -> ComPtr<ID3DBlob> {
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> errorBlob;
        UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
        flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        HRESULT hr = D3DCompile(
            source, strlen(source), nullptr, nullptr, nullptr, entryPoint, target, flags, 0, &blob, &errorBlob
        );
        if (FAILED(hr)) {
            return nullptr;
        }
        return blob;
    }

    // =========================================================================
    // Root signature creation (static sampler baked in)
    // =========================================================================

    auto D3D12BlitPipeline::CreateRootSignature(
        ID3D12Device* device, D3D12_FILTER filter, ComPtr<ID3D12RootSignature>& out
    ) -> bool {
        // Param 0: Root constants (4 floats = 16 bytes for UV rect)
        D3D12_ROOT_PARAMETER1 params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace = 0;
        params[0].Constants.Num32BitValues = sizeof(BlitConstants) / 4;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        // Param 1: SRV descriptor table (t0) for source texture
        D3D12_DESCRIPTOR_RANGE1 srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 0;
        srvRange.RegisterSpace = 0;
        srvRange.OffsetInDescriptorsFromTableStart = 0;
        srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &srvRange;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // Static sampler — baked into root signature, no descriptor needed at draw time
        D3D12_STATIC_SAMPLER_DESC staticSampler{};
        staticSampler.Filter = filter;
        staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.ShaderRegister = 0;
        staticSampler.RegisterSpace = 0;
        staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc{};
        rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootSigDesc.Desc_1_1.NumParameters = 2;
        rootSigDesc.Desc_1_1.pParameters = params;
        rootSigDesc.Desc_1_1.NumStaticSamplers = 1;
        rootSigDesc.Desc_1_1.pStaticSamplers = &staticSampler;
        rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> sigBlob, errorBlob;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &sigBlob, &errorBlob);
        if (FAILED(hr)) {
            return false;
        }
        hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&out));
        return SUCCEEDED(hr);
    }

    // =========================================================================
    // PSO creation (one per dstFormat)
    // =========================================================================

    auto D3D12BlitPipeline::CreatePSO(ID3D12Device* device, ID3D12RootSignature* rootSig, DXGI_FORMAT dstFormat)
        -> ComPtr<ID3D12PipelineState> {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = rootSig;
        psoDesc.VS = {vsBytecode_->GetBufferPointer(), vsBytecode_->GetBufferSize()};
        psoDesc.PS = {psBytecode_->GetBufferPointer(), psBytecode_->GetBufferSize()};
        psoDesc.InputLayout = {nullptr, 0};
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        psoDesc.RasterizerState.DepthClipEnable = FALSE;
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = dstFormat;
        psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        psoDesc.SampleDesc.Count = 1;
        psoDesc.SampleMask = UINT_MAX;

        ComPtr<ID3D12PipelineState> pso;
        if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)))) {
            return nullptr;
        }
        return pso;
    }

    // =========================================================================
    // Init — compile shaders + create root signatures
    // =========================================================================

    auto D3D12BlitPipeline::Init(ID3D12Device* device) -> bool {
        if (initialized_) {
            return true;
        }

        vsBytecode_ = CompileShader(kBlitHLSL, "VSMain", "vs_5_0");
        psBytecode_ = CompileShader(kBlitHLSL, "PSMain", "ps_5_0");
        if (!vsBytecode_ || !psBytecode_) {
            return false;
        }

        if (!CreateRootSignature(device, D3D12_FILTER_MIN_MAG_MIP_POINT, rootSigNearest_)) {
            return false;
        }
        if (!CreateRootSignature(device, D3D12_FILTER_MIN_MAG_MIP_LINEAR, rootSigLinear_)) {
            return false;
        }

        initialized_ = true;
        return true;
    }

    // =========================================================================
    // GetPSO — lazy creation + cache by (format, filter)
    // =========================================================================

    auto D3D12BlitPipeline::GetPSO(ID3D12Device* device, DXGI_FORMAT dstFormat, bool linear) -> ID3D12PipelineState* {
        uint32_t key = (static_cast<uint32_t>(dstFormat) << 1) | static_cast<uint32_t>(linear);
        auto it = psoCache_.find(key);
        if (it != psoCache_.end()) {
            return it->second.Get();
        }
        auto* rootSig = linear ? rootSigLinear_.Get() : rootSigNearest_.Get();
        auto pso = CreatePSO(device, rootSig, dstFormat);
        if (!pso) {
            return nullptr;
        }
        auto* raw = pso.Get();
        psoCache_.emplace(key, std::move(pso));
        return raw;
    }

}  // namespace miki::rhi
