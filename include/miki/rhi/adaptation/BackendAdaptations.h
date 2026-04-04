/** @file BackendAdaptations.h
 *  @brief Per-backend constexpr adaptation rule tables.
 *
 *  Each backend declares its adaptation rules as a constexpr array of Rule.
 *  Tables use inline const (not constexpr) due to std::span pointer semantics.
 *
 *  Namespace: miki::rhi::adaptation
 *  Spec reference: rendering-pipeline-architecture.md §20b.5
 */
#pragma once

#include <array>
#include <span>

#include "miki/rhi/RhiTypes.h"
#include "miki/rhi/adaptation/AdaptationTypes.h"

namespace miki::rhi::adaptation {

    // =========================================================================
    // WebGPU (Tier 3) — Most adaptations
    // =========================================================================

    inline constexpr auto kWebGPUAdaptations = std::to_array<Rule>({
        {.feature = Feature::BufferMapWriteWithUsage,
         .strategy = Strategy::ShadowBuffer,
         .costFactor = 1.5f,
         .description = "CPU shadow buffer + wgpuQueueWriteBuffer on unmap. Extra memcpy per buffer update."},
        {.feature = Feature::BufferPersistentMapping,
         .strategy = Strategy::EphemeralMap,
         .costFactor = 1.1f,
         .description = "mapAsync/unmap per access cycle. Non-blocking but not persistent."},
        {.feature = Feature::PushConstants,
         .strategy = Strategy::UboEmulation,
         .costFactor = 1.05f,
         .description = "256B UBO at @group(0) @binding(0). User bindings shift to group(N+1)."},
        {.feature = Feature::CmdBlitTexture,
         .strategy = Strategy::ShaderEmulation,
         .costFactor = 2.0f,
         .description = "Fullscreen quad shader blit. Requires extra render pass + pipeline."},
        {.feature = Feature::CmdFillBufferNonZero,
         .strategy = Strategy::ShaderEmulation,
         .costFactor = 1.8f,
         .description = "Compute shader fill. Extra dispatch + barrier."},
        {.feature = Feature::CmdClearTexture,
         .strategy = Strategy::ShaderEmulation,
         .costFactor = 1.5f,
         .description = "Clear via render pass loadOp. Requires begin/end render pass."},
        {.feature = Feature::MultiDrawIndirect,
         .strategy = Strategy::LoopUnroll,
         .costFactor = 3.0f,
         .description = "WebGPU: 1 draw per indirect call. N draws = N API calls."},
        {.feature = Feature::DepthOnlyStencilOps,
         .strategy = Strategy::ParameterFixup,
         .costFactor = 1.0f,
         .description = "Stencil ops forced to Undefined, stencilReadOnly=true. Zero overhead."},
        {.feature = Feature::TimelineSemaphore,
         .strategy = Strategy::CallbackEmulation,
         .costFactor = 1.0f,
         .description = "onSubmittedWorkDone callback + monotonic serial. Semantically equivalent."},
        {.feature = Feature::DynamicDepthBias,
         .strategy = Strategy::Unsupported,
         .costFactor = 0.0f,
         .description = "WebGPU sets depth bias at pipeline creation. No dynamic override."},
        {.feature = Feature::SparseBinding,
         .strategy = Strategy::Unsupported,
         .costFactor = 0.0f,
         .description = "WebGPU has no sparse resource API."},
        {.feature = Feature::ExecuteSecondary,
         .strategy = Strategy::Unsupported,
         .costFactor = 0.0f,
         .description = "WebGPU render bundles API not yet wired."},
        {.feature = Feature::MeshShader,
         .strategy = Strategy::Unsupported,
         .costFactor = 0.0f,
         .description = "WebGPU has no mesh/task shader support."},
        {.feature = Feature::RayTracing,
         .strategy = Strategy::Unsupported,
         .costFactor = 0.0f,
         .description = "WebGPU has no ray tracing API."},
    });

    // =========================================================================
    // D3D12 (Tier 1)
    // =========================================================================

    inline constexpr auto kD3D12Adaptations = std::to_array<Rule>({
        {.feature = Feature::BufferMapWriteWithUsage,
         .strategy = Strategy::StagingCopy,
         .costFactor = 1.3f,
         .description = "UPLOAD heap + CopyBufferRegion to DEFAULT. One extra copy command."},
        {.feature = Feature::CmdBlitTexture,
         .strategy = Strategy::ShaderEmulation,
         .costFactor = 2.0f,
         .description = "Fullscreen quad shader. D3D12 has no native blit."},
        {.feature = Feature::CmdFillBufferNonZero,
         .strategy = Strategy::ShaderEmulation,
         .costFactor = 1.5f,
         .description = "UAV clear or compute shader. No native non-zero fill."},
    });

    // =========================================================================
    // OpenGL 4.3+ (Tier 4)
    // =========================================================================

    inline constexpr auto kOpenGLAdaptations = std::to_array<Rule>({
        {.feature = Feature::PushConstants,
         .strategy = Strategy::UboEmulation,
         .costFactor = 1.05f,
         .description = "128B UBO at binding 0. User UBO bindings shifted +1."},
        {.feature = Feature::MeshShader,
         .strategy = Strategy::Unsupported,
         .costFactor = 0.0f,
         .description = "OpenGL 4.3 has no mesh shader extension."},
        {.feature = Feature::RayTracing,
         .strategy = Strategy::Unsupported,
         .costFactor = 0.0f,
         .description = "OpenGL has no ray tracing API."},
        {.feature = Feature::SparseBinding,
         .strategy = Strategy::Unsupported,
         .costFactor = 0.0f,
         .description = "OpenGL sparse texture exists but not wired in T4."},
    });

    // =========================================================================
    // Table structs with std::span (inline const, not constexpr)
    // =========================================================================

    struct BackendAdaptationTable {
        BackendType backend;
        std::span<const Rule> rules;
    };

    inline const BackendAdaptationTable kWebGPUAdaptationTable = {
        .backend = BackendType::WebGPU,
        .rules = kWebGPUAdaptations,
    };

    inline const BackendAdaptationTable kD3D12AdaptationTable = {
        .backend = BackendType::D3D12,
        .rules = kD3D12Adaptations,
    };

    // Vulkan 1.4 supports all RHI features natively. Empty span = no adaptations.
    // VulkanCompat shares this table. Mesh/RT unsupported on Compat is handled at
    // runtime via GpuCapabilityProfile, not a static adaptation table.
    inline const BackendAdaptationTable kVulkanAdaptationTable = {
        .backend = BackendType::Vulkan14,
        .rules = std::span<const Rule>{},
    };

    inline const BackendAdaptationTable kOpenGLAdaptationTable = {
        .backend = BackendType::OpenGL43,
        .rules = kOpenGLAdaptations,
    };

}  // namespace miki::rhi::adaptation
