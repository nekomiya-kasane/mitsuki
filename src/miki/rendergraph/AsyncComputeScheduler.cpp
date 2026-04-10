/** @file AsyncComputeScheduler.cpp
 *  @brief Adaptive async compute scheduling with EMA benefit estimator.
 *  See: AsyncComputeScheduler.h, specs/04-render-graph.md §7.2, §7.4, §7.5
 */

#include "miki/rendergraph/AsyncComputeScheduler.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>
#include <queue>
#include <unordered_set>

namespace miki::rg {

    // =========================================================================
    // Construction
    // =========================================================================

    AsyncComputeScheduler::AsyncComputeScheduler(const AsyncComputeSchedulerConfig& config) noexcept
        : config_(config) {}

    // =========================================================================
    // ShouldRunAsync — per-pass scheduling decision
    // =========================================================================

    auto AsyncComputeScheduler::ShouldRunAsync(
        uint32_t passIndex, RGPassFlags flags, float estimatedGpuTimeUs
    ) const noexcept -> bool {
        // Level D: no compute queue available — always graphics
        if (queueLevel_ == ComputeQueueLevel::D_GraphicsOnly) {
            return false;
        }

        // Must be flagged as async-eligible
        bool hasAsyncFlag = (static_cast<uint8_t>(flags) & static_cast<uint8_t>(RGPassFlags::AsyncCompute)) != 0;
        if (!hasAsyncFlag) {
            return false;
        }

        // Must be compute-capable (not graphics-only)
        bool isCompute = (static_cast<uint8_t>(flags) & static_cast<uint8_t>(RGPassFlags::Compute)) != 0;
        if (!isCompute) {
            return false;
        }

        // Look up EMA estimator for this pass
        auto it = estimates_.find(passIndex);
        if (it == estimates_.end()) {
            // No history: create cold-start entry, use static threshold
            PassAsyncEstimate& est = estimates_[passIndex];
            est.isWarmingUp = true;
            est.frameCount = 0;

            // Cold-start decision: if estimated time exceeds static threshold, try async
            float threshold = config_.staticThresholdUs + config_.crossQueueSyncCostUs;
            return estimatedGpuTimeUs >= threshold;
        }

        const PassAsyncEstimate& est = it->second;

        // Warm-up phase: use static threshold
        if (est.isWarmingUp || est.frameCount < config_.warmUpFrames) {
            float threshold = config_.staticThresholdUs + config_.crossQueueSyncCostUs;
            return estimatedGpuTimeUs >= threshold;
        }

        // Adaptive phase: use EMA benefit
        float threshold = config_.adaptiveThresholdUs;

        // Hysteresis: if recently beneficial, keep async
        if (est.framesSinceBenefit < config_.hysteresisFrames && est.framesOnAsync > 0) {
            return true;
        }

        // EMA benefit must exceed threshold (accounting for sync cost)
        return est.emaBenefitUs > threshold;
    }

    // =========================================================================
    // UpdateFeedback — incorporate GPU timing data from previous frame
    // =========================================================================

    void AsyncComputeScheduler::UpdateFeedback(std::span<const PassTimingFeedback> feedback) {
        for (const auto& fb : feedback) {
            auto& est = estimates_[fb.passIndex];

            // Compute benefit: time saved by async overlap
            // Benefit = max(0, overlappedGraphicsTime - syncCost)
            float benefit = std::max(0.0f, fb.overlappedGraphicsTimeUs - config_.crossQueueSyncCostUs);

            // EMA update: emaBenefitUs = alpha * benefit + (1 - alpha) * emaBenefitUs
            if (est.frameCount == 0) {
                // First sample: initialize directly
                est.emaBenefitUs = benefit;
            } else {
                est.emaBenefitUs = config_.emaAlpha * benefit + (1.0f - config_.emaAlpha) * est.emaBenefitUs;
            }

            // Track hysteresis
            if (est.emaBenefitUs > config_.adaptiveThresholdUs) {
                est.framesSinceBenefit = 0;
            } else {
                ++est.framesSinceBenefit;
            }

            // Warm-up transition
            est.frameCount++;
            if (est.isWarmingUp && est.frameCount >= config_.warmUpFrames) {
                est.isWarmingUp = false;
            }
        }
    }

    // =========================================================================
    // BeginFrame
    // =========================================================================

    void AsyncComputeScheduler::BeginFrame() noexcept {
        ++globalFrameCount_;
    }

    // =========================================================================
    // GetEstimate
    // =========================================================================

    auto AsyncComputeScheduler::GetEstimate(uint32_t passIndex) const noexcept -> const PassAsyncEstimate* {
        auto it = estimates_.find(passIndex);
        return it != estimates_.end() ? &it->second : nullptr;
    }

    // =========================================================================
    // DetectAndPreventDeadlocks — §7.5 cross-queue DAG cycle detection
    //
    // Algorithm: Build a directed graph where nodes are queue types and edges
    // represent cross-queue dependencies. If a cycle exists (e.g. G->C->G),
    // demote the least-important async pass in the cycle back to graphics.
    //
    // Implementation: BFS/DFS on the 3-node queue graph. Since |V|=3, the
    // cycle detection is trivially O(1). For pass-level cycles, we use
    // topological sort on the cross-queue DAG.
    // =========================================================================

    auto AsyncComputeScheduler::DetectAndPreventDeadlocks(
        std::span<const CrossQueueSyncPoint> syncPoints, std::vector<RGQueueType>& queueAssignments,
        [[maybe_unused]] std::span<const RGPassNode> passes
    ) -> DeadlockDetectionResult {
        DeadlockDetectionResult result;

        if (syncPoints.empty()) {
            return result;
        }

        // Build per-pass cross-queue adjacency list
        // passA -> passB means passB waits for passA across queues
        constexpr uint32_t kMaxPasses = 4096;
        std::unordered_map<uint32_t, std::vector<uint32_t>> adj;
        std::unordered_map<uint32_t, uint32_t> inDegree;
        std::unordered_set<uint32_t> crossQueuePasses;

        for (const auto& sp : syncPoints) {
            if (sp.srcPassIndex < kMaxPasses && sp.dstPassIndex < kMaxPasses) {
                adj[sp.srcPassIndex].push_back(sp.dstPassIndex);
                inDegree[sp.dstPassIndex]++;
                if (inDegree.find(sp.srcPassIndex) == inDegree.end()) {
                    inDegree[sp.srcPassIndex] = 0;
                }
                crossQueuePasses.insert(sp.srcPassIndex);
                crossQueuePasses.insert(sp.dstPassIndex);
            }
        }

        // Kahn's algorithm for cycle detection on cross-queue subgraph
        std::queue<uint32_t> frontier;
        for (auto& [node, deg] : inDegree) {
            if (deg == 0) {
                frontier.push(node);
            }
        }

        uint32_t visited = 0;
        while (!frontier.empty()) {
            auto node = frontier.front();
            frontier.pop();
            ++visited;
            if (auto it = adj.find(node); it != adj.end()) {
                for (auto next : it->second) {
                    if (--inDegree[next] == 0) {
                        frontier.push(next);
                    }
                }
            }
        }

        // If visited != total cross-queue passes, we have a cycle
        if (visited < crossQueuePasses.size()) {
            result.hasCycle = true;

            // Find passes in the cycle (those with remaining inDegree > 0)
            // Demote async compute passes to graphics to break the cycle
            for (auto& [passIdx, deg] : inDegree) {
                if (deg > 0 && passIdx < queueAssignments.size()) {
                    if (queueAssignments[passIdx] == RGQueueType::AsyncCompute) {
                        queueAssignments[passIdx] = RGQueueType::Graphics;
                        result.demotedPasses.push_back(passIdx);
                    }
                }
            }

            // Re-check: if cycle still exists after demotion, demote transfer passes too
            // (extremely rare case, defensive only)
            if (result.demotedPasses.empty()) {
                for (auto& [passIdx, deg] : inDegree) {
                    if (deg > 0 && passIdx < queueAssignments.size()) {
                        if (queueAssignments[passIdx] == RGQueueType::Transfer) {
                            queueAssignments[passIdx] = RGQueueType::Graphics;
                            result.demotedPasses.push_back(passIdx);
                        }
                    }
                }
            }
        }

        return result;
    }

}  // namespace miki::rg
