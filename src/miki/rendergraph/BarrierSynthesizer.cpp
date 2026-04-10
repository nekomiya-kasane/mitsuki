/** @file BarrierSynthesizer.cpp
 *  @brief Stage 6: Barrier synthesis (split barrier model).
 *  See: BarrierSynthesizer.h, specs/04-render-graph.md §5.5, §5.6
 */

#include "miki/rendergraph/BarrierSynthesizer.h"

#include "miki/rendergraph/RenderGraphBuilder.h"

namespace miki::rg {

    // =========================================================================
    // EvaluateTransition — decide if a barrier is needed
    // =========================================================================

    auto BarrierSynthesizer::EvaluateTransition(
        uint16_t resIdx, bool isTexture, ResourceAccess dstAccess, bool isWrite, const ResourceState& state,
        RGQueueType dstQueue
    ) const -> BarrierDecision {
        BarrierDecision decision;

        if (state.lastAccess == ResourceAccess::None) {
            return decision;  // No prior access — no barrier needed
        }

        auto srcBarrier = ResolveBarrierCombined(state.lastAccess);
        auto dstBarrier = ResolveBarrier(dstAccess);

        // Determine if barrier is needed:
        // 1. Any write involved (RAW, WAW, WAR-with-layout-change)
        // 2. Layout transition needed (texture only)
        // 3. Cross-queue transition
        bool srcHasWrite = IsWriteAccess(state.lastAccess);
        decision.needed = srcHasWrite || isWrite;

        if (isTexture && srcBarrier.layout != dstBarrier.layout && dstBarrier.layout != rhi::TextureLayout::Undefined) {
            decision.needed = true;
        }

        decision.isCrossQueue = (state.lastQueue != dstQueue);
        if (decision.isCrossQueue) {
            decision.needed = true;
        }

        if (decision.needed) {
            decision.barrier = BarrierCommand{
                .resourceIndex = resIdx,
                .srcAccess = state.lastAccess,
                .dstAccess = dstAccess,
                .srcLayout = isTexture ? srcBarrier.layout : rhi::TextureLayout::Undefined,
                .dstLayout = isTexture ? dstBarrier.layout : rhi::TextureLayout::Undefined,
                .isCrossQueue = decision.isCrossQueue,
                .srcQueue = state.lastQueue,
                .dstQueue = dstQueue,
            };
        }

        return decision;
    }

    // =========================================================================
    // EmitCrossQueueBarrier — QFOT (Exclusive) or single barrier (D3D12/Concurrent)
    // =========================================================================

    void BarrierSynthesizer::EmitCrossQueueBarrier(
        const BarrierCommand& barrier, bool isTexture, uint32_t srcPos, uint32_t dstPos,
        std::vector<CompiledPassInfo>& compiledPasses
    ) {
        auto qfot
            = DetermineQfotStrategy(isTexture ? RGResourceKind::Texture : RGResourceKind::Buffer, config_.backendType);

        if (qfot == QfotStrategy::Exclusive) {
            // Vulkan EXCLUSIVE images: release on src queue, acquire on dst queue
            BarrierCommand release = barrier;
            release.isSplitRelease = true;
            release.isSplitAcquire = false;
            release.dstAccess = ResourceAccess::None;

            BarrierCommand acquire = barrier;
            acquire.isSplitRelease = false;
            acquire.isSplitAcquire = true;
            acquire.srcAccess = ResourceAccess::None;

            compiledPasses[srcPos].releaseBarriers.push_back(release);
            compiledPasses[dstPos].acquireBarriers.push_back(acquire);
        } else {
            // D3D12 or CONCURRENT: single barrier at dst pass
            compiledPasses[dstPos].acquireBarriers.push_back(barrier);
        }
    }

    // =========================================================================
    // EmitSameQueueBarrier — split or full barrier
    // =========================================================================

    void BarrierSynthesizer::EmitSameQueueBarrier(
        const BarrierCommand& barrier, uint32_t srcPos, uint32_t dstPos, std::vector<CompiledPassInfo>& compiledPasses
    ) {
        if (config_.enableSplitBarriers && srcPos != std::numeric_limits<uint32_t>::max() && dstPos - srcPos > 1) {
            // Same-queue split barrier: release at previous pass, acquire at current
            BarrierCommand release = barrier;
            release.isSplitRelease = true;
            release.isSplitAcquire = false;

            BarrierCommand acquire = barrier;
            acquire.isSplitRelease = false;
            acquire.isSplitAcquire = true;

            compiledPasses[srcPos].releaseBarriers.push_back(release);
            compiledPasses[dstPos].acquireBarriers.push_back(acquire);
        } else {
            // Full barrier at current pass
            compiledPasses[dstPos].acquireBarriers.push_back(barrier);
        }
    }

    // =========================================================================
    // ProcessAccess — evaluate + emit for a single resource access
    // =========================================================================

    void BarrierSynthesizer::ProcessAccess(
        RGResourceHandle handle, ResourceAccess access, bool isWrite, uint32_t pos, RGQueueType dstQueue,
        std::span<const RGResourceNode> resources, std::vector<CompiledPassInfo>& compiledPasses
    ) {
        auto resIdx = handle.GetIndex();
        auto& state = resourceStates_[resIdx];
        bool isTexture = (resIdx < resources.size() && resources[resIdx].kind == RGResourceKind::Texture);

        auto decision = EvaluateTransition(resIdx, isTexture, access, isWrite, state, dstQueue);

        if (decision.needed) {
            if (decision.isCrossQueue && state.lastPassOrderPos != std::numeric_limits<uint32_t>::max()) {
                EmitCrossQueueBarrier(decision.barrier, isTexture, state.lastPassOrderPos, pos, compiledPasses);
            } else {
                EmitSameQueueBarrier(decision.barrier, state.lastPassOrderPos, pos, compiledPasses);
            }
        }

        // Update state
        state.lastAccess = access;
        auto dstBarrier = ResolveBarrier(access);
        if (isTexture && dstBarrier.layout != rhi::TextureLayout::Undefined) {
            state.lastLayout = dstBarrier.layout;
        }
        state.lastQueue = dstQueue;
        state.lastPassOrderPos = pos;
    }

    // =========================================================================
    // Synthesize — main entry point
    // =========================================================================

    void BarrierSynthesizer::Synthesize(
        const RenderGraphBuilder& builder, std::span<const uint32_t> order,
        std::span<const RGQueueType> queueAssignments, std::vector<CompiledPassInfo>& compiledPasses
    ) {
        auto& passes = builder.GetPasses();
        auto& resources = builder.GetResources();

        // Build compiled pass info for each pass in topological order
        compiledPasses.clear();
        compiledPasses.reserve(order.size());

        for (auto passIdx : order) {
            compiledPasses.push_back(
                CompiledPassInfo{
                    .passIndex = passIdx,
                    .queue = queueAssignments[passIdx],
                    .acquireBarriers = {},
                    .releaseBarriers = {},
                }
            );
        }

        // Reset per-synthesis state
        resourceStates_.clear();

        // Process passes in topological order
        for (uint32_t pos = 0; pos < order.size(); ++pos) {
            auto passIdx = order[pos];
            auto& pass = passes[passIdx];
            auto dstQueue = queueAssignments[passIdx];

            // Process reads first, then writes (reads see previous state)
            for (auto& r : pass.reads) {
                ProcessAccess(r.handle, r.access, false, pos, dstQueue, resources, compiledPasses);
            }
            for (auto& w : pass.writes) {
                ProcessAccess(w.handle, w.access, true, pos, dstQueue, resources, compiledPasses);
            }
        }
    }

}  // namespace miki::rg
