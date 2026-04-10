/** @file BarrierSynthesizer.h
 *  @brief Stage 6: Barrier synthesis (split barrier model).
 *
 *  Transforms the topologically-sorted pass order + queue assignments into
 *  per-pass acquire/release BarrierCommand lists. Supports:
 *    - Full barriers (same queue, adjacent passes)
 *    - Split barriers (same queue, non-adjacent passes)
 *    - Cross-queue QFOT barriers (Vulkan EXCLUSIVE images)
 *    - Cross-queue single barriers (D3D12, CONCURRENT buffers)
 *
 *  See: specs/04-render-graph.md §5.5, §5.6, §7.2.2
 *  Namespace: miki::rg
 */
#pragma once

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "miki/rendergraph/AsyncComputeScheduler.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/RhiEnums.h"

namespace miki::rg {

    class RenderGraphBuilder;

    // =========================================================================
    // BarrierSynthesizer configuration (subset of compiler options)
    // =========================================================================

    struct BarrierSynthesizerConfig {
        rhi::BackendType backendType = rhi::BackendType::Mock;
        bool enableSplitBarriers = true;
    };

    // =========================================================================
    // BarrierSynthesizer — stateful single-pass barrier emitter
    // =========================================================================

    class BarrierSynthesizer {
       public:
        explicit BarrierSynthesizer(const BarrierSynthesizerConfig& config) noexcept : config_(config) {}

        /// @brief Run Stage 6: synthesize barriers for all passes in topological order.
        /// Populates compiledPasses with per-pass acquire/release barriers.
        void Synthesize(
            const RenderGraphBuilder& builder, std::span<const uint32_t> order,
            std::span<const RGQueueType> queueAssignments, std::vector<CompiledPassInfo>& compiledPasses
        );

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
        std::unordered_map<uint16_t, ResourceState> resourceStates_;
    };

}  // namespace miki::rg
