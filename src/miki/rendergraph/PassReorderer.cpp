/** @file PassReorderer.cpp
 *  @brief Stage 3b: Barrier-aware global reordering.
 *  See: PassReorderer.h, specs/04-render-graph.md §5.3.1
 */

#include "miki/rendergraph/PassReorderer.h"

#include <algorithm>
#include <unordered_map>

#include "miki/rendergraph/RenderGraphBuilder.h"

namespace miki::rg {

    // =========================================================================
    // DagConstraints::Build
    // =========================================================================

    void DagConstraints::Build(
        uint32_t passCount, std::span<const DependencyEdge> edges, const std::vector<bool>& activeSet
    ) {
        predecessors.resize(passCount);
        successors.resize(passCount);
        for (auto& e : edges) {
            if (!activeSet[e.srcPass] || !activeSet[e.dstPass]) {
                continue;
            }
            predecessors[e.dstPass].push_back(e.srcPass);
            successors[e.srcPass].push_back(e.dstPass);
        }
    }

    // =========================================================================
    // ComputeInsertionRange
    // =========================================================================

    auto ComputeInsertionRange(
        uint32_t passIdx, std::span<const uint32_t> order, const DagConstraints& dag,
        std::span<const uint32_t> positionOf
    ) -> std::pair<uint32_t, uint32_t> {
        uint32_t lo = 0;
        uint32_t hi = static_cast<uint32_t>(order.size() - 1);

        for (auto pred : dag.predecessors[passIdx]) {
            lo = std::max(lo, positionOf[pred] + 1);
        }
        for (auto succ : dag.successors[passIdx]) {
            if (positionOf[succ] > 0) {
                hi = std::min(hi, positionOf[succ] - 1);
            } else {
                hi = 0;
            }
        }
        return {lo, hi};
    }

    // =========================================================================
    // EstimateBarrierCost
    // =========================================================================

    auto EstimateBarrierCost(
        std::span<const uint32_t> order, std::span<const DependencyEdge> edges, const std::vector<bool>& activeSet
    ) -> uint32_t {
        if (edges.empty() || order.empty()) {
            return 0;
        }

        std::vector<uint32_t> posOf(*std::max_element(order.begin(), order.end()) + 1, 0);
        for (uint32_t i = 0; i < order.size(); ++i) {
            posOf[order[i]] = i;
        }

        uint32_t cost = 0;
        for (auto& e : edges) {
            if (!activeSet[e.srcPass] || !activeSet[e.dstPass]) {
                continue;
            }
            // Barrier between consecutive passes = full barrier (more expensive)
            // Barrier between non-consecutive passes = split barrier (cheaper)
            uint32_t gap = posOf[e.dstPass] - posOf[e.srcPass];
            if (gap == 1) {
                cost += 2;  // Full barrier (no split opportunity)
            } else {
                cost += 1;  // Split barrier possible
            }
        }
        return cost;
    }

    // =========================================================================
    // EstimatePeakMemory
    // =========================================================================

    auto EstimatePeakMemory(
        std::span<const uint32_t> order, std::span<const RGPassNode> passes, std::span<const RGResourceNode> resources
    ) -> uint64_t {
        struct Interval {
            uint32_t first = UINT32_MAX;
            uint32_t last = 0;
            uint64_t size = 0;
        };

        std::unordered_map<uint16_t, Interval> intervals;
        std::vector<uint32_t> posOf(passes.size(), 0);
        for (uint32_t i = 0; i < order.size(); ++i) {
            posOf[order[i]] = i;
        }

        for (auto passIdx : order) {
            auto& pass = passes[passIdx];
            uint32_t pos = posOf[passIdx];
            for (auto& r : pass.reads) {
                auto idx = r.handle.GetIndex();
                if (idx < resources.size() && !resources[idx].imported) {
                    auto& iv = intervals[idx];
                    iv.first = std::min(iv.first, pos);
                    iv.last = std::max(iv.last, pos);
                    if (iv.size == 0) {
                        iv.size = resources[idx].kind == RGResourceKind::Texture
                                      ? EstimateTextureSize(resources[idx].textureDesc)
                                      : EstimateBufferSize(resources[idx].bufferDesc);
                    }
                }
            }
            for (auto& w : pass.writes) {
                auto idx = w.handle.GetIndex();
                if (idx < resources.size() && !resources[idx].imported) {
                    auto& iv = intervals[idx];
                    iv.first = std::min(iv.first, pos);
                    iv.last = std::max(iv.last, pos);
                    if (iv.size == 0) {
                        iv.size = resources[idx].kind == RGResourceKind::Texture
                                      ? EstimateTextureSize(resources[idx].textureDesc)
                                      : EstimateBufferSize(resources[idx].bufferDesc);
                    }
                }
            }
        }

        // Sweep-line for peak memory
        uint64_t peak = 0;
        for (uint32_t pos = 0; pos < order.size(); ++pos) {
            uint64_t live = 0;
            for (auto& [idx, iv] : intervals) {
                if (pos >= iv.first && pos <= iv.last) {
                    live += iv.size;
                }
            }
            peak = std::max(peak, live);
        }
        return peak;
    }

    // =========================================================================
    // PassReorderer::Reorder
    // =========================================================================

    void PassReorderer::Reorder(
        const RenderGraphBuilder& builder, std::span<const DependencyEdge> edges, const std::vector<bool>& activeSet,
        std::vector<uint32_t>& order
    ) {
        if (order.size() <= 2 || edges.empty()) {
            return;
        }

        auto& passes = builder.GetPasses();
        uint32_t passCount = static_cast<uint32_t>(passes.size());

        // Build DAG constraint lookup
        DagConstraints dag;
        dag.Build(passCount, edges, activeSet);

        // Position lookup: passIndex -> position in order
        std::vector<uint32_t> positionOf(passCount, 0);
        auto rebuildPositions = [&]() {
            for (uint32_t i = 0; i < order.size(); ++i) {
                positionOf[order[i]] = i;
            }
        };
        rebuildPositions();

        // Objective function weights based on strategy
        float wBarrier = 0.5f;
        [[maybe_unused]] float wMemory = 0.3f, wLatency = 0.2f;
        switch (strategy_) {
            case SchedulerStrategy::MinBarriers:
                wBarrier = 1.0f;
                wMemory = 0.0f;
                wLatency = 0.0f;
                break;
            case SchedulerStrategy::MinMemory:
                wBarrier = 0.0f;
                wMemory = 1.0f;
                wLatency = 0.0f;
                break;
            case SchedulerStrategy::MinLatency:
                wBarrier = 0.0f;
                wMemory = 0.0f;
                wLatency = 1.0f;
                break;
            case SchedulerStrategy::Balanced: break;
        }

        // Normalize baseline costs for weighted combination
        float baseBarrier = static_cast<float>(EstimateBarrierCost(order, edges, activeSet));

        constexpr uint32_t kMaxIterations = 3;

        for (uint32_t iter = 0; iter < kMaxIterations; ++iter) {
            bool improved = false;

            for (uint32_t pos = 0; pos < order.size(); ++pos) {
                uint32_t passIdx = order[pos];
                auto [lo, hi] = ComputeInsertionRange(passIdx, order, dag, positionOf);

                if (lo >= hi || (lo == pos && hi == pos)) {
                    continue;
                }

                // Evaluate current barrier cost contribution (local: edges involving this pass)
                float bestScore = 0.0f;
                uint32_t bestPos = pos;

                for (uint32_t candidatePos = lo; candidatePos <= hi; ++candidatePos) {
                    if (candidatePos == pos) {
                        continue;
                    }

                    // Tentative move: remove from pos, insert at candidatePos
                    auto trial = order;
                    trial.erase(trial.begin() + pos);
                    uint32_t insertAt = (candidatePos > pos) ? candidatePos - 1 : candidatePos;
                    trial.insert(trial.begin() + insertAt, passIdx);

                    // Evaluate delta (barrier-focused for speed)
                    float delta = 0.0f;

                    if (wBarrier > 0.0f) {
                        float newBarrier = static_cast<float>(EstimateBarrierCost(trial, edges, activeSet));
                        delta += wBarrier * (baseBarrier - newBarrier);
                    }

                    if (delta > bestScore) {
                        bestScore = delta;
                        bestPos = candidatePos;
                    }
                }

                if (bestPos != pos && bestScore > 0.0f) {
                    // Apply the move
                    order.erase(order.begin() + pos);
                    uint32_t insertAt = (bestPos > pos) ? bestPos - 1 : bestPos;
                    order.insert(order.begin() + insertAt, passIdx);
                    rebuildPositions();
                    baseBarrier = static_cast<float>(EstimateBarrierCost(order, edges, activeSet));
                    improved = true;
                }
            }

            if (!improved) {
                break;
            }
        }
    }

}  // namespace miki::rg
