/** @file AdaptationQuery.h
 *  @brief Constexpr + consteval query API for backend adaptation strategies.
 *
 *  Dual-path design:
 *  - QueryStrategy() / QueryRule(): constexpr, table-based, runtime-extensible
 *  - QueryStrategyConsteval(): consteval, switch-based, guaranteed ICE for if constexpr
 *
 *  Namespace: miki::rhi::adaptation
 *  Spec reference: rendering-pipeline-architecture.md §20b.6, §20b.12
 */
#pragma once

#include "miki/rhi/adaptation/AdaptationTypes.h"
#include "miki/rhi/adaptation/BackendAdaptations.h"

namespace miki::rhi::adaptation {

    // =========================================================================
    // Runtime table-based queries (constexpr when inputs are known)
    // =========================================================================

    /// Lookup the adaptation table for a backend. Returns nullptr for Mock.
    constexpr auto GetAdaptationTable(BackendType backend) -> const BackendAdaptationTable* {
        switch (backend) {
            case BackendType::WebGPU: return &kWebGPUAdaptationTable;
            case BackendType::D3D12: return &kD3D12AdaptationTable;
            case BackendType::Vulkan14: [[fallthrough]];
            case BackendType::VulkanCompat: return &kVulkanAdaptationTable;
            case BackendType::OpenGL43: return &kOpenGLAdaptationTable;
            case BackendType::Mock: return nullptr;
        }
        return nullptr;
    }

    /// Query the adaptation strategy for a specific feature on a backend.
    /// Returns Strategy::Native if no adaptation rule exists (natively supported).
    /// O(N) linear scan, N <= 15.
    constexpr auto QueryStrategy(BackendType backend, Feature feature) -> Strategy {
        auto* table = GetAdaptationTable(backend);
        if (!table) {
            return Strategy::Native;
        }
        for (auto& rule : table->rules) {
            if (rule.feature == feature) {
                return rule.strategy;
            }
        }
        return Strategy::Native;
    }

    /// Query the full rule (strategy + cost + description) for a feature.
    /// Returns nullptr if natively supported.
    constexpr auto QueryRule(BackendType backend, Feature feature) -> const Rule* {
        auto* table = GetAdaptationTable(backend);
        if (!table) {
            return nullptr;
        }
        for (auto& rule : table->rules) {
            if (rule.feature == feature) {
                return &rule;
            }
        }
        return nullptr;
    }

    /// Query the cost factor for a feature. Returns 1.0f if native.
    constexpr auto QueryCostFactor(BackendType backend, Feature feature) -> float {
        auto* rule = QueryRule(backend, feature);
        return rule ? rule->costFactor : 1.0f;
    }

    /// Check if a feature is available (native or adapted, but not Unsupported).
    constexpr auto IsFeatureAvailable(BackendType backend, Feature feature) -> bool {
        return QueryStrategy(backend, feature) != Strategy::Unsupported;
    }

    /// Check if a feature requires hidden GPU work on this backend.
    constexpr auto RequiresHiddenGpuWork(BackendType backend, Feature feature) -> bool {
        return HasGpuOverhead(QueryStrategy(backend, feature));
    }

    // =========================================================================
    // Compile-time guaranteed path (consteval, switch-based)
    // Bypasses BackendAdaptationTable — hardcoded per-backend for ICE guarantee.
    // =========================================================================

    consteval auto QueryStrategyConsteval(BackendType backend, Feature feature) -> Strategy {
        if (backend == BackendType::WebGPU) {
            switch (feature) {
                case Feature::BufferMapWriteWithUsage: return Strategy::ShadowBuffer;
                case Feature::BufferPersistentMapping: return Strategy::EphemeralMap;
                case Feature::PushConstants: return Strategy::UboEmulation;
                case Feature::CmdBlitTexture: return Strategy::ShaderEmulation;
                case Feature::CmdFillBufferNonZero: return Strategy::ShaderEmulation;
                case Feature::CmdClearTexture: return Strategy::ShaderEmulation;
                case Feature::MultiDrawIndirect: return Strategy::LoopUnroll;
                case Feature::DepthOnlyStencilOps: return Strategy::ParameterFixup;
                case Feature::TimelineSemaphore: return Strategy::CallbackEmulation;
                case Feature::DynamicDepthBias: return Strategy::Unsupported;
                case Feature::SparseBinding: return Strategy::Unsupported;
                case Feature::ExecuteSecondary: return Strategy::Unsupported;
                case Feature::MeshShader: return Strategy::Unsupported;
                case Feature::RayTracing: return Strategy::Unsupported;
                default: return Strategy::Native;
            }
        }
        if (backend == BackendType::D3D12) {
            switch (feature) {
                case Feature::BufferMapWriteWithUsage: return Strategy::StagingCopy;
                case Feature::CmdBlitTexture: return Strategy::ShaderEmulation;
                case Feature::CmdFillBufferNonZero: return Strategy::ShaderEmulation;
                default: return Strategy::Native;
            }
        }
        if (backend == BackendType::OpenGL43) {
            switch (feature) {
                case Feature::PushConstants: return Strategy::UboEmulation;
                case Feature::MeshShader: return Strategy::Unsupported;
                case Feature::RayTracing: return Strategy::Unsupported;
                case Feature::SparseBinding: return Strategy::Unsupported;
                default: return Strategy::Native;
            }
        }
        // Vulkan14, VulkanCompat — all native
        return Strategy::Native;
    }

    // =========================================================================
    // RenderGraph cost model — aggregate adaptation cost for a set of features.
    // Designed for pass reordering, batching, and hidden-work insertion.
    // =========================================================================

    struct PassAdaptationCost {
        float totalCostFactor = 1.0f;     ///< Product of per-feature cost factors. 1.0 = all native.
        uint32_t hiddenGpuPassCount = 0;  ///< Features requiring extra GPU passes (ShaderEmulation, StagingCopy).
        uint32_t hiddenCpuWorkCount = 0;  ///< Features requiring extra CPU work (ShadowBuffer, LoopUnroll).
        uint32_t unsupportedCount = 0;    ///< Features that cannot be adapted at all.
    };

    /// Estimate the aggregate adaptation cost for a render pass that uses the given features.
    /// RenderGraph compiler uses this to:
    ///   - Insert extra barrier/copy passes for hiddenGpuPassCount > 0
    ///   - Warn about unsupportedCount > 0
    ///   - Use totalCostFactor for pass scheduling heuristics
    constexpr auto EstimatePassCost(BackendType backend, std::span<const Feature> features) -> PassAdaptationCost {
        PassAdaptationCost cost{};
        for (auto f : features) {
            auto strategy = QueryStrategy(backend, f);
            float factor = QueryCostFactor(backend, f);
            cost.totalCostFactor *= factor;
            if (HasGpuOverhead(strategy)) {
                ++cost.hiddenGpuPassCount;
            }
            if (HasCpuOverhead(strategy) && !HasGpuOverhead(strategy)) {
                ++cost.hiddenCpuWorkCount;
            }
            if (strategy == Strategy::Unsupported) {
                ++cost.unsupportedCount;
            }
        }
        return cost;
    }

    /// Compile-time variant for fixed feature lists (e.g., known at template instantiation).
    template <Feature... Fs>
    consteval auto EstimatePassCostConsteval(BackendType backend) -> PassAdaptationCost {
        PassAdaptationCost cost{};
        (
            (void)[&] {
                auto s = QueryStrategyConsteval(backend, Fs);
                float factor = QueryCostFactor(backend, Fs);
                cost.totalCostFactor *= factor;
                if (HasGpuOverhead(s)) {
                    ++cost.hiddenGpuPassCount;
                }
                if (HasCpuOverhead(s) && !HasGpuOverhead(s)) {
                    ++cost.hiddenCpuWorkCount;
                }
                if (s == Strategy::Unsupported) {
                    ++cost.unsupportedCount;
                }
            }(),
            ...
        );
        return cost;
    }

}  // namespace miki::rhi::adaptation
