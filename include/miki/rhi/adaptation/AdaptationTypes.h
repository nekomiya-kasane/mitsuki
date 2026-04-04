/** @file AdaptationTypes.h
 *  @brief Backend adaptation strategy enums, feature enums, rule struct, and constexpr predicates.
 *
 *  All types are constexpr-friendly. The RenderGraph compiler and `if constexpr` branches
 *  can evaluate adaptation queries at compile time when the backend is a template parameter.
 *
 *  Namespace: miki::rhi::adaptation
 *  Spec reference: rendering-pipeline-architecture.md §20b.3, §20b.4
 */
#pragma once

#include <cstdint>

namespace miki::rhi::adaptation {

    /// How a backend adapts an unsupported configuration.
    /// Ordered by typical performance impact (lower = cheaper).
    enum class Strategy : uint8_t {
        Native,             ///< Natively supported, no adaptation. Cost factor = 1.0.
        ParameterFixup,     ///< Silent parameter correction (e.g., stencil ops -> Undefined). O(1).
        UboEmulation,       ///< Push constants emulated via UBO. One extra buffer bind per frame.
        EphemeralMap,       ///< Per-access map/unmap cycle instead of persistent mapping.
        CallbackEmulation,  ///< Sync primitive emulated via async callback (WebGPU fence).
        ShadowBuffer,       ///< CPU shadow + queue write on unmap. Extra memcpy per update.
        StagingCopy,        ///< Intermediate staging resource + GPU copy. Extra command + sync.
        LoopUnroll,         ///< N API calls instead of 1 batched call. O(N) overhead.
        ShaderEmulation,    ///< Missing fixed-function emulated by shader (extra render pass).
        Unsupported,        ///< Cannot be adapted. Returns FeatureNotSupported.
    };

    /// Identifies which RHI feature is being adapted.
    enum class Feature : uint8_t {
        BufferMapWriteWithUsage,  ///< MapWrite combined with Vertex/Index/Uniform/Storage
        BufferPersistentMapping,  ///< Persistent (long-lived) buffer mapping
        PushConstants,            ///< Native push constant path
        CmdBlitTexture,           ///< Hardware blit
        CmdFillBufferNonZero,     ///< Fill buffer with non-zero value
        CmdClearTexture,          ///< Clear texture outside render pass
        MultiDrawIndirect,        ///< Batched indirect draw (N draws in 1 call)
        DepthOnlyStencilOps,      ///< Stencil ops on depth-only format
        TimelineSemaphore,        ///< Timeline semaphore sync
        SparseBinding,            ///< Sparse/virtual resource binding
        DynamicDepthBias,         ///< Runtime depth bias change
        ExecuteSecondary,         ///< Secondary command buffer / render bundles
        MeshShader,               ///< Mesh/task shader pipeline
        RayTracing,               ///< RT pipeline / acceleration structures
        kCount,
    };

    /// A single adaptation rule: for (backend, feature) -> strategy + cost.
    /// All fields are constexpr-friendly (no pointers to runtime data except string literals).
    struct Rule {
        Feature feature;
        Strategy strategy;
        float costFactor;         ///< 1.0 = native, 2.0 = ~2x slower. Compile-time constant.
        const char* description;  ///< Static string literal. Human-readable.
    };

    // =========================================================================
    // Constexpr predicates on Strategy
    // =========================================================================

    /// Does this strategy introduce hidden GPU work?
    constexpr auto HasGpuOverhead(Strategy s) noexcept -> bool {
        return s >= Strategy::StagingCopy;
    }

    /// Does this strategy introduce hidden CPU work?
    constexpr auto HasCpuOverhead(Strategy s) noexcept -> bool {
        return s >= Strategy::ShadowBuffer;
    }

    /// Is this a transparent fixup with no measurable cost?
    constexpr auto IsTransparent(Strategy s) noexcept -> bool {
        return s <= Strategy::CallbackEmulation;
    }

}  // namespace miki::rhi::adaptation
