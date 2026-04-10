/** @file BarrierSynthesizer.h
 *  @brief Stage 6: Barrier synthesis (split barrier model).
 *
 *  Transforms the topologically-sorted pass order + queue assignments into
 *  per-pass acquire/release BarrierCommand lists. Supports:
 *    - Full barriers (same queue, adjacent passes)
 *    - Split barriers (same queue, non-adjacent passes)
 *    - D3D12 Fence Barriers Tier 1/2 (Agility SDK 1.719+, replaces split barriers)
 *    - Cross-queue QFOT barriers (Vulkan EXCLUSIVE images)
 *    - Cross-queue single barriers (D3D12, CONCURRENT buffers)
 *    - Read-to-read barrier elision (AMD RDNA guidance: no read→read barriers)
 *
 *  See: specs/04-render-graph.md §5.5, §5.6, §7.2.2
 *  Namespace: miki::rg
 */
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "miki/rendergraph/AsyncComputeScheduler.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/GpuCapabilityProfile.h"
#include "miki/rhi/RhiEnums.h"

namespace miki::rg {

    class RenderGraphBuilder;

    // =========================================================================
    // BarrierSynthesizer configuration (subset of compiler options)
    // =========================================================================

    struct BarrierSynthesizerConfig {
        rhi::BackendType backendType = rhi::BackendType::Mock;
        bool enableSplitBarriers = true;
        rhi::GpuCapabilityProfile::FenceBarrierTier fenceBarrierTier
            = rhi::GpuCapabilityProfile::FenceBarrierTier::None;  ///< D3D12 Fence Barrier Tier
        bool enableEnhancedBarriers = false;                      ///< D3D12 Enhanced Barriers
    };

    // =========================================================================
    // Barrier synthesis statistics (for profiling)
    // =========================================================================

    struct BarrierSynthesisStats {
        uint32_t fullBarriers = 0;        ///< Full (non-split) same-queue barriers
        uint32_t splitBarriers = 0;       ///< Split same-queue barrier pairs (legacy + fence)
        uint32_t fenceBarriers = 0;       ///< D3D12 Fence Barrier pairs (subset of splitBarriers)
        uint32_t crossQueueBarriers = 0;  ///< Cross-queue barriers (any type)
        uint32_t qfotPairs = 0;           ///< QFOT release/acquire pairs
        uint32_t elidedReadRead = 0;      ///< Read-to-read barriers elided
        uint32_t totalBarriers = 0;       ///< Sum of all emitted barriers
    };

    // =========================================================================
    // BarrierSynthesizer — stateful single-pass barrier emitter
    // =========================================================================

    class BarrierSynthesizer {
       public:
        explicit BarrierSynthesizer(const BarrierSynthesizerConfig& config) noexcept : config_(config) {}

        /// @brief Run Stage 6: synthesize barriers for all passes in topological order.
        void Synthesize(
            const RenderGraphBuilder& builder, std::span<const uint32_t> order,
            std::span<const RGQueueType> queueAssignments, std::vector<CompiledPassInfo>& compiledPasses
        );

        /// @brief Get barrier synthesis statistics from the last Synthesize() call.
        [[nodiscard]] auto GetStats() const noexcept -> const BarrierSynthesisStats& { return stats_; }

       private:
        // Per-resource tracked state during synthesis
        struct ResourceState {
            ResourceAccess lastAccess = ResourceAccess::None;
            rhi::TextureLayout lastLayout = rhi::TextureLayout::Undefined;
            RGQueueType lastQueue = RGQueueType::Graphics;
            uint32_t lastPassOrderPos = std::numeric_limits<uint32_t>::max();
        };

        // Determine whether a barrier is needed and what kind
        struct BarrierDecision {
            bool needed = false;
            bool isCrossQueue = false;
            BarrierCommand barrier{};
        };

        // Evaluate whether an access transition requires a barrier
        [[nodiscard]] auto EvaluateTransition(
            uint16_t resIdx, bool isTexture, ResourceAccess dstAccess, bool isWrite, const ResourceState& state,
            RGQueueType dstQueue
        ) const -> BarrierDecision;

        // Emit a cross-queue barrier (QFOT or single)
        void EmitCrossQueueBarrier(
            const BarrierCommand& barrier, bool isTexture, uint32_t srcPos, uint32_t dstPos,
            std::vector<CompiledPassInfo>& compiledPasses
        );

        // Emit a same-queue barrier (split or full)
        void EmitSameQueueBarrier(
            const BarrierCommand& barrier, uint32_t srcPos, uint32_t dstPos,
            std::vector<CompiledPassInfo>& compiledPasses
        );

        // Process a single resource access and emit barriers as needed
        void ProcessAccess(
            RGResourceHandle handle, ResourceAccess access, bool isWrite, uint32_t pos, RGQueueType dstQueue,
            std::span<const RGResourceNode> resources, std::vector<CompiledPassInfo>& compiledPasses
        );

        BarrierSynthesizerConfig config_;
        std::vector<ResourceState> resourceStates_;  ///< Flat vector indexed by resource index
        mutable BarrierSynthesisStats stats_;
        uint64_t fenceValueCounter_ = 0;  ///< Monotonic fence value for D3D12 Fence Barrier emission
    };

}  // namespace miki::rg
