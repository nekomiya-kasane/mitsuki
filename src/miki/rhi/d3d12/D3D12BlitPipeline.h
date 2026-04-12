/** @file D3D12BlitPipeline.h
 *  @brief Internal blit pipeline for D3D12 CmdBlitTexture emulation.
 *
 *  D3D12 has no native vkCmdBlitImage equivalent. This module provides a
 *  fullscreen-triangle graphics pipeline that samples the source texture and
 *  writes to the destination via a render target. Created lazily on first use.
 *
 *  Architecture:
 *    - Root signature: 32B root constants (UV rect) + 1 SRV (t0) + 1 static sampler
 *    - VS: fullscreen triangle from SV_VertexID (no vertex buffer)
 *    - PS: sample source with UV rect transform
 *    - Two PSO variants: nearest / linear (different static samplers)
 *    - One PSO per (filter, dstFormat) pair, cached in a small flat map
 *    - Shader HLSL compiled at device init via D3DCompile
 *
 *  Namespace: miki::rhi
 */
#pragma once

#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <unordered_map>

using Microsoft::WRL::ComPtr;

namespace miki::rhi {

    struct D3D12BlitPipeline {
        // Push constants layout (matches HLSL cbuffer BlitConstants)
        struct BlitConstants {
            float srcMinU, srcMinV;
            float srcMaxU, srcMaxV;
        };

        auto Init(ID3D12Device* device) -> bool;
        auto GetPSO(ID3D12Device* device, DXGI_FORMAT dstFormat, bool linear) -> ID3D12PipelineState*;
        [[nodiscard]] auto GetRootSignature() const noexcept -> ID3D12RootSignature* { return rootSigNearest_.Get(); }
        [[nodiscard]] auto GetRootSignatureLinear() const noexcept -> ID3D12RootSignature* {
            return rootSigLinear_.Get();
        }

       private:
        auto CreateRootSignature(ID3D12Device* device, D3D12_FILTER filter, ComPtr<ID3D12RootSignature>& out) -> bool;
        auto CreatePSO(ID3D12Device* device, ID3D12RootSignature* rootSig, DXGI_FORMAT dstFormat)
            -> ComPtr<ID3D12PipelineState>;

        ComPtr<ID3D12RootSignature> rootSigNearest_;
        ComPtr<ID3D12RootSignature> rootSigLinear_;
        ComPtr<ID3DBlob> vsBytecode_;
        ComPtr<ID3DBlob> psBytecode_;

        // PSO cache: key = (dstFormat << 1 | linear)
        std::unordered_map<uint32_t, ComPtr<ID3D12PipelineState>> psoCache_;
        bool initialized_ = false;
    };

}  // namespace miki::rhi
