#!/usr/bin/env python3
"""Rewrite DeferredResolve.cpp with push descriptor migration."""

import pathlib

content = r'''/** @brief DeferredResolve implementation \u2014 deferred PBR lighting compute dispatch.
 *
 * Create(): compile deferred_resolve.slang, create compute pipeline + 9-binding descriptor.
 * Setup(): create transient HDR output texture (static).
 * AddToGraph(): register compute pass (static).
 * Execute(): push descriptors, upload lights, dispatch (instance).
 */

#include "miki/rendergraph/DeferredResolve.h"

#include <cstring>
#include <filesystem>
#include <utility>

#include "miki/rendergraph/RenderContext.h"
#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/ICommandBuffer.h"
#include "miki/rhi/IDevice.h"
#include "miki/rhi/RhiDescriptors.h"
#include "miki/shader/ShaderTypes.h"
#include "miki/shader/SlangCompiler.h"

namespace fs = std::filesystem;

namespace miki::rendergraph {

// ---------------------------------------------------------------------------
// DeferredResolve::Setup
// ---------------------------------------------------------------------------

auto DeferredResolve::Setup(
    RenderGraphBuilder& ioBuilder,
    render::GBufferLayout const& /*iGBuffer*/,
    uint32_t iWidth,
    uint32_t iHeight) -> RGHandle
{
    RGTextureDesc hdrDesc{};
    hdrDesc.width  = iWidth;
    hdrDesc.height = iHeight;
    hdrDesc.format = rhi::Format::RGBA16_FLOAT;
    hdrDesc.usage  = RGTextureUsage::Storage
                   | RGTextureUsage::ColorAttachment
                   | RGTextureUsage::Sampled;

    return ioBuilder.CreateTexture("DeferredResolve_HDR", hdrDesc);
}

// ---------------------------------------------------------------------------
// DeferredResolve::AddToGraph
// ---------------------------------------------------------------------------

auto DeferredResolve::AddToGraph(
    RenderGraphBuilder& ioBuilder,
    render::GBufferLayout const& iGBuffer,
    RGHandle iHdrOutput,
    ExecuteFn iExecuteFn) -> RGPassHandle
{
    return ioBuilder.AddComputePass(
        "DeferredResolve",
        [iGBuffer, iHdrOutput](PassBuilder& pb) {
            (void)pb.Read(iGBuffer.albedoMetallic, rhi::PipelineStage::ComputeShader);
            (void)pb.Read(iGBuffer.normalRoughness, rhi::PipelineStage::ComputeShader);
            (void)pb.Read(iGBuffer.depth, rhi::PipelineStage::ComputeShader);

            (void)pb.Write(iHdrOutput, rhi::PipelineStage::ComputeShader);
        },
        std::move(iExecuteFn));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static auto GetShaderTarget(rhi::BackendType b) -> shader::ShaderTarget {
    return shader::ShaderTargetForBackend(b);
}

struct ResolvePushConstants {
    uint32_t lightCount;
    uint32_t width;
    uint32_t height;
    uint32_t _pad0;
};
static_assert(sizeof(ResolvePushConstants) == 16);

// ---------------------------------------------------------------------------
// DeferredResolve::Create
// ---------------------------------------------------------------------------

auto DeferredResolve::Create(const DeferredResolveCreateDesc& iDesc)
    -> core::Result<DeferredResolve>
{
    if (!iDesc.device || !iDesc.compiler) {
        return std::unexpected(core::ErrorCode::InvalidArgument);
    }

    auto& device   = *iDesc.device;
    auto& compiler = *iDesc.compiler;

    DeferredResolve dr;
    dr.device_    = &device;
    dr.eLut_      = iDesc.eLut;
    dr.eAvg_      = iDesc.eAvg;
    dr.maxLights_ = iDesc.maxLights;

    // Skip on Mock
    if (device.GetBackendType() == rhi::BackendType::Mock) {
        return dr;
    }

    auto target = GetShaderTarget(device.GetBackendType());

    auto rendergraphDir = fs::path(MIKI_RENDERGRAPH_SHADER_DIR_LITERAL);
    auto renderDir      = fs::path(MIKI_RENDER_SHADER_DIR_LITERAL);
    auto commonDir      = fs::path(MIKI_COMMON_SHADER_DIR_LITERAL);
    compiler.AddSearchPath(rendergraphDir);
    compiler.AddSearchPath(renderDir);
    compiler.AddSearchPath(commonDir);

    shader::ShaderCompileDesc compDesc{
        .sourcePath = rendergraphDir / "deferred_resolve.slang",
        .entryPoint = "computeMain",
        .stage      = shader::ShaderStage::Compute,
        .target     = target,
    };
    auto blob = compiler.Compile(compDesc);
    if (!blob) return std::unexpected(blob.error());

    rhi::DescriptorSetLayoutBinding bindings[] = {
        { .binding = 0,  .type = rhi::DescriptorType::SampledTexture, .count = 1, .stageFlags = rhi::ShaderStageFlag::Compute },
        { .binding = 1,  .type = rhi::DescriptorType::SampledTexture, .count = 1, .stageFlags = rhi::ShaderStageFlag::Compute },
        { .binding = 2,  .type = rhi::DescriptorType::SampledTexture, .count = 1, .stageFlags = rhi::ShaderStageFlag::Compute },
        { .binding = 3,  .type = rhi::DescriptorType::StorageTexture, .count = 1, .stageFlags = rhi::ShaderStageFlag::Compute },
        { .binding = 4,  .type = rhi::DescriptorType::StorageBuffer,  .count = 1, .stageFlags = rhi::ShaderStageFlag::Compute },
        { .binding = 5,  .type = rhi::DescriptorType::SampledTexture, .count = 1, .stageFlags = rhi::ShaderStageFlag::Compute },
        { .binding = 6,  .type = rhi::DescriptorType::SampledTexture, .count = 1, .stageFlags = rhi::ShaderStageFlag::Compute },
        { .binding = 7,  .type = rhi::DescriptorType::Sampler,        .count = 1, .stageFlags = rhi::ShaderStageFlag::Compute },
        { .binding = 8,  .type = rhi::DescriptorType::UniformBuffer,  .count = 1, .stageFlags = rhi::ShaderStageFlag::Compute },
        { .binding = 9,  .type = rhi::DescriptorType::UniformBuffer,  .count = 1, .stageFlags = rhi::ShaderStageFlag::Compute },
        { .binding = 10, .type = rhi::DescriptorType::SampledTexture, .count = 1, .stageFlags = rhi::ShaderStageFlag::Compute },
        { .binding = 11, .type = rhi::DescriptorType::UniformBuffer,  .count = 1, .stageFlags = rhi::ShaderStageFlag::Compute },
    };
    auto setLayout = device.CreateDescriptorSetLayout({.bindings = bindings, .pushDescriptor = true});
    if (!setLayout) return std::unexpected(setLayout.error());
    dr.setLayout_ = *setLayout;

    rhi::PushConstantRange pushRange{
        .stageFlags = rhi::ShaderStageFlag::Compute, .offset = 0, .size = sizeof(ResolvePushConstants),
    };
    rhi::DescriptorSetLayoutHandle layouts[] = { dr.setLayout_ };
    auto pipeLayout = device.CreatePipelineLayout({
        .setLayouts = layouts, .pushConstants = std::span(&pushRange, 1),
    });
    if (!pipeLayout) return std::unexpected(pipeLayout.error());
    dr.pipelineLayout_ = *pipeLayout;

    rhi::ComputePipelineDesc cpDesc{
        .computeShader = { .code = blob->data.data(), .codeSize = blob->data.size(), .stage = rhi::ShaderStageFlag::Compute },
        .pipelineLayout = dr.pipelineLayout_,
    };
    auto pipeline = device.CreateComputePipeline(cpDesc);
    if (!pipeline) return std::unexpected(pipeline.error());
    dr.pipeline_ = *pipeline;

    rhi::SamplerDesc sampDesc{ .magFilter = rhi::Filter::Linear, .minFilter = rhi::Filter::Linear,
        .addressModeU = rhi::AddressMode::ClampToEdge, .addressModeV = rhi::AddressMode::ClampToEdge };
    auto sampler = device.CreateSampler(sampDesc);
    if (!sampler) return std::unexpected(sampler.error());
    dr.sampler_ = *sampler;

    uint64_t lightBufSize = static_cast<uint64_t>(iDesc.maxLights) * sizeof(LightData);
    rhi::BufferDesc lightDesc{
        .size = lightBufSize,
        .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst,
        .memoryType = rhi::MemoryType::GpuOnly,
    };
    auto lightBuf = device.CreateBuffer(lightDesc);
    if (!lightBuf) return std::unexpected(lightBuf.error());
    dr.lightSsbo_ = *lightBuf;

    rhi::BufferDesc shDesc{
        .size = 9 * sizeof(float) * 4,
        .usage = rhi::BufferUsage::Uniform,
        .memoryType = rhi::MemoryType::CpuToGpu,
    };
    auto shBuf = device.CreateBuffer(shDesc);
    if (!shBuf) return std::unexpected(shBuf.error());
    dr.shUbo_ = *shBuf;

    rhi::BufferDesc stagingDesc{
        .size = lightBufSize,
        .usage = rhi::BufferUsage::TransferSrc,
        .memoryType = rhi::MemoryType::CpuToGpu,
    };
    auto stagingBuf = device.CreateBuffer(stagingDesc);
    if (!stagingBuf) return std::unexpected(stagingBuf.error());
    dr.lightStaging_ = *stagingBuf;

    rhi::TextureDesc dummyShadowTexDesc{};
    dummyShadowTexDesc.width  = 1;
    dummyShadowTexDesc.height = 1;
    dummyShadowTexDesc.format = rhi::Format::D32_FLOAT;
    dummyShadowTexDesc.usage  = rhi::TextureUsage::Sampled | rhi::TextureUsage::DepthStencilAttachment;
    dummyShadowTexDesc.mipLevels  = 1;
    dummyShadowTexDesc.arrayLayers = 1;
    auto dummyShadowTex = device.CreateTexture(dummyShadowTexDesc);
    if (!dummyShadowTex) return std::unexpected(dummyShadowTex.error());
    dr.dummyCsmAtlas_ = *dummyShadowTex;

    {
        auto initCmd = device.CreateCommandBuffer();
        if (initCmd.has_value()) {
            auto& cmd = *initCmd.value();
            (void)cmd.Begin();
            rhi::TextureBarrier toRead{
                .texture   = dr.dummyCsmAtlas_,
                .srcAccess = rhi::AccessFlags::None,
                .dstAccess = rhi::AccessFlags::ShaderRead,
                .oldLayout = rhi::TextureLayout::Undefined,
                .newLayout = rhi::TextureLayout::ShaderReadOnly,
            };
            cmd.PipelineBarrier({
                .srcStage       = rhi::PipelineStage::TopOfPipe,
                .dstStage       = rhi::PipelineStage::ComputeShader,
                .textureBarriers = std::span(&toRead, 1),
            });
            (void)cmd.End();
            (void)device.Submit(cmd);
            device.WaitIdle();
        }
    }

    rhi::BufferDesc dummyCascadeDesc{};
    dummyCascadeDesc.size = 288;
    dummyCascadeDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::TransferDst;
    dummyCascadeDesc.memoryType = rhi::MemoryType::GpuOnly;
    auto dummyCascadeBuf = device.CreateBuffer(dummyCascadeDesc);
    if (!dummyCascadeBuf) return std::unexpected(dummyCascadeBuf.error());
    dr.dummyCascadeUbo_ = *dummyCascadeBuf;

    return dr;
}

// ---------------------------------------------------------------------------
// Destructor + move
// ---------------------------------------------------------------------------

DeferredResolve::~DeferredResolve() {
    if (!device_) return;
    if (pipeline_.IsValid())       device_->DestroyPipeline(pipeline_);
    if (pipelineLayout_.IsValid()) device_->DestroyPipelineLayout(pipelineLayout_);
    if (setLayout_.IsValid())      device_->DestroyDescriptorSetLayout(setLayout_);
    if (sampler_.IsValid())        device_->DestroySampler(sampler_);
    if (lightSsbo_.IsValid())      device_->DestroyBuffer(lightSsbo_);
    if (lightStaging_.IsValid())   device_->DestroyBuffer(lightStaging_);
    if (shUbo_.IsValid())          device_->DestroyBuffer(shUbo_);
    if (dummyCsmAtlas_.IsValid())   device_->DestroyTexture(dummyCsmAtlas_);
    if (dummyCascadeUbo_.IsValid()) device_->DestroyBuffer(dummyCascadeUbo_);
}

DeferredResolve::DeferredResolve(DeferredResolve&& o) noexcept
    : device_{std::exchange(o.device_, nullptr)}
    , pipeline_{std::exchange(o.pipeline_, {})}
    , pipelineLayout_{std::exchange(o.pipelineLayout_, {})}
    , setLayout_{std::exchange(o.setLayout_, {})}
    , sampler_{std::exchange(o.sampler_, {})}
    , lightSsbo_{std::exchange(o.lightSsbo_, {})}
    , lightStaging_{std::exchange(o.lightStaging_, {})}
    , shUbo_{std::exchange(o.shUbo_, {})}
    , eLut_{o.eLut_}, eAvg_{o.eAvg_}, maxLights_{o.maxLights_}
    , dummyCsmAtlas_{std::exchange(o.dummyCsmAtlas_, {})}
    , dummyCascadeUbo_{std::exchange(o.dummyCascadeUbo_, {})}
{}

auto DeferredResolve::operator=(DeferredResolve&& o) noexcept -> DeferredResolve& {
    if (this != &o) {
        this->~DeferredResolve();
        new (this) DeferredResolve(std::move(o));
    }
    return *this;
}

// ---------------------------------------------------------------------------
// DeferredResolve::Execute
// ---------------------------------------------------------------------------

auto DeferredResolve::Execute(
    RenderContext& iCtx,
    render::GBufferLayout const& iGBuffer,
    RGHandle iHdrOutput,
    std::span<const LightData> iLights,
    rhi::BufferHandle iCameraUbo,
    std::span<const core::float3, 9> iIrradianceSH,
    uint32_t iWidth,
    uint32_t iHeight,
    uint32_t iFrameIndex,
    ResolveShadowParams iShadow) -> void
{
    if (!pipeline_.IsValid()) return;

    auto& cmd    = iCtx.GetCommandBuffer();
    auto& device = iCtx.GetDevice();

    auto albedoTex = iCtx.Resolve(iGBuffer.albedoMetallic);
    auto normalTex = iCtx.Resolve(iGBuffer.normalRoughness);
    auto depthTex  = iCtx.Resolve(iGBuffer.depth);
    auto hdrTex    = iCtx.Resolve(iHdrOutput);

    uint32_t lightCount = static_cast<uint32_t>(iLights.size());
    if (lightCount > maxLights_) lightCount = maxLights_;
    if (lightCount > 0 && lightStaging_.IsValid()) {
        auto mapRes = device.MapBuffer(lightStaging_);
        if (mapRes) {
            std::memcpy(*mapRes, iLights.data(), lightCount * sizeof(LightData));
            device.UnmapBuffer(lightStaging_);
            rhi::BufferCopyInfo copyInfo{
                .srcOffset = 0, .dstOffset = 0,
                .size = static_cast<uint64_t>(lightCount) * sizeof(LightData),
            };
            cmd.CopyBuffer(lightStaging_, lightSsbo_, copyInfo);

            rhi::BufferBarrier lightBarrier{
                .buffer    = lightSsbo_,
                .srcAccess = rhi::AccessFlags::TransferWrite,
                .dstAccess = rhi::AccessFlags::ShaderRead,
            };
            cmd.PipelineBarrier(rhi::PipelineBarrierInfo{
                .srcStage       = rhi::PipelineStage::Transfer,
                .dstStage       = rhi::PipelineStage::ComputeShader,
                .bufferBarriers = std::span(&lightBarrier, 1),
            });
        }
    }

    if (shUbo_.IsValid() && shDirty_) {
        auto mapRes = device.MapBuffer(shUbo_);
        if (mapRes) {
            struct SHCoeffFloat4 { float x, y, z, w; };
            auto* dst = static_cast<SHCoeffFloat4*>(*mapRes);
            for (uint32_t i = 0; i < 9; ++i) {
                dst[i] = { iIrradianceSH[i].x, iIrradianceSH[i].y, iIrradianceSH[i].z, 0.0f };
            }
            device.UnmapBuffer(shUbo_);
            shDirty_ = false;
        }
    }

    auto csmAtlas   = iShadow.csmAtlas.IsValid()  ? iShadow.csmAtlas   : dummyCsmAtlas_;
    auto cascadeUbo = iShadow.cascadeUbo.IsValid() ? iShadow.cascadeUbo : dummyCascadeUbo_;

    rhi::DescriptorWrite writes[] = {
        { .binding = 0,  .type = rhi::DescriptorType::SampledTexture, .texture = albedoTex },
        { .binding = 1,  .type = rhi::DescriptorType::SampledTexture, .texture = normalTex },
        { .binding = 2,  .type = rhi::DescriptorType::SampledTexture, .texture = depthTex },
        { .binding = 3,  .type = rhi::DescriptorType::StorageTexture, .texture = hdrTex },
        { .binding = 4,  .type = rhi::DescriptorType::StorageBuffer,  .buffer = lightSsbo_,
          .bufferRange = static_cast<uint64_t>(lightCount) * sizeof(LightData) },
        { .binding = 5,  .type = rhi::DescriptorType::SampledTexture, .texture = eLut_ },
        { .binding = 6,  .type = rhi::DescriptorType::SampledTexture, .texture = eAvg_ },
        { .binding = 7,  .type = rhi::DescriptorType::Sampler,        .sampler = sampler_ },
        { .binding = 8,  .type = rhi::DescriptorType::UniformBuffer,  .buffer = iCameraUbo,
          .bufferRange = 368 },
        { .binding = 9,  .type = rhi::DescriptorType::UniformBuffer,  .buffer = shUbo_,
          .bufferRange = 144 },
        { .binding = 10, .type = rhi::DescriptorType::SampledTexture, .texture = csmAtlas },
        { .binding = 11, .type = rhi::DescriptorType::UniformBuffer,  .buffer = cascadeUbo,
          .bufferRange = 288 },
    };

    cmd.BindComputePipeline(pipeline_);
    cmd.PushDescriptorSet(rhi::PipelineBindPoint::Compute, 0, pipelineLayout_, writes);

    ResolvePushConstants pc{
        .lightCount = lightCount,
        .width      = iWidth,
        .height     = iHeight,
    };
    cmd.PushConstants(rhi::PipelineStage::ComputeShader, 0, sizeof(pc), &pc);

    cmd.Dispatch((iWidth + 7) / 8, (iHeight + 7) / 8, 1);
}

} // namespace miki::rendergraph
'''

pathlib.Path(path).write_text(content, encoding='utf-8', newline='\n')
print('OK')
"""

path = r'd:\repos\miki\src\miki\rendergraph\DeferredResolve.cpp'

import pathlib
pathlib.Path(path).write_text(content, encoding='utf-8', newline='\n')
print('OK')
