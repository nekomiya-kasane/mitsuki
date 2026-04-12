/** @file RenderGraphAdvanced.cpp
 *  @brief Implementation of advanced render graph features (Phase L-6..L-12).
 *  Namespace: miki::rg
 */

#include "miki/rendergraph/RenderGraphAdvanced.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>
#include <limits>
#include <numeric>

namespace miki::rg {

    // =========================================================================
    // L-6: AsyncComputeDiscovery
    // =========================================================================

    AsyncComputeDiscovery::AsyncComputeDiscovery(const AsyncDiscoveryConfig& config) : config_(config) {}

    auto AsyncComputeDiscovery::Analyze(
        std::span<const RGPassNode> passes, std::span<const DependencyEdge> edges, uint32_t passCount
    ) const -> AsyncDiscoveryResult {
        AsyncDiscoveryResult result;
        result.passInfo.resize(passCount);

        // Build adjacency lists
        std::vector<std::vector<uint32_t>> predecessors(passCount);
        std::vector<std::vector<uint32_t>> successors(passCount);
        for (const auto& edge : edges) {
            if (edge.srcPass < passCount && edge.dstPass < passCount) {
                successors[edge.srcPass].push_back(edge.dstPass);
                predecessors[edge.dstPass].push_back(edge.srcPass);
            }
        }

        // Initialize pass info
        for (uint32_t i = 0; i < passCount; ++i) {
            result.passInfo[i].passIndex = i;
            result.passInfo[i].flags = passes[i].flags;
            result.passInfo[i].currentQueue = passes[i].queueHint;
            result.passInfo[i].estimatedGpuTimeUs = passes[i].estimatedGpuTimeUs;
        }

        // Forward pass: compute earliest start time (ASAP)
        // Topological order — process passes with all predecessors already computed
        std::vector<uint32_t> inDegree(passCount, 0);
        for (uint32_t i = 0; i < passCount; ++i) {
            inDegree[i] = static_cast<uint32_t>(predecessors[i].size());
        }

        std::vector<uint32_t> topoOrder;
        topoOrder.reserve(passCount);
        std::vector<uint32_t> queue;
        for (uint32_t i = 0; i < passCount; ++i) {
            if (inDegree[i] == 0) {
                queue.push_back(i);
            }
        }

        while (!queue.empty()) {
            uint32_t current = queue.back();
            queue.pop_back();
            topoOrder.push_back(current);
            for (uint32_t succ : successors[current]) {
                if (--inDegree[succ] == 0) {
                    queue.push_back(succ);
                }
            }
        }

        // Forward pass: ASAP = max(predecessor.ASAP + predecessor.duration)
        for (uint32_t idx : topoOrder) {
            float asap = 0.0f;
            for (uint32_t pred : predecessors[idx]) {
                float predEnd = result.passInfo[pred].earliestStart + result.passInfo[pred].estimatedGpuTimeUs;
                asap = std::max(asap, predEnd);
            }
            result.passInfo[idx].earliestStart = asap;
        }

        // Compute critical path length
        float maxEnd = 0.0f;
        for (uint32_t i = 0; i < passCount; ++i) {
            float end = result.passInfo[i].earliestStart + result.passInfo[i].estimatedGpuTimeUs;
            maxEnd = std::max(maxEnd, end);
        }
        result.criticalPathLengthUs = maxEnd;

        // Backward pass: ALAP = min(successor.ALAP) - duration
        for (uint32_t i = 0; i < passCount; ++i) {
            result.passInfo[i].latestStart = maxEnd;  // Initialize to max
        }
        for (auto it = topoOrder.rbegin(); it != topoOrder.rend(); ++it) {
            uint32_t idx = *it;
            float alap = maxEnd - result.passInfo[idx].estimatedGpuTimeUs;
            for (uint32_t succ : successors[idx]) {
                alap = std::min(alap, result.passInfo[succ].latestStart - result.passInfo[idx].estimatedGpuTimeUs);
            }
            result.passInfo[idx].latestStart = alap;
        }

        // Compute slack and critical path membership
        for (uint32_t i = 0; i < passCount; ++i) {
            auto& pi = result.passInfo[i];
            pi.slack = pi.latestStart - pi.earliestStart;
            pi.onCriticalPath = (pi.slack < 1e-3f);  // Near-zero slack = critical
        }

        // Identify async compute promotion candidates
        for (uint32_t i = 0; i < passCount; ++i) {
            const auto& pi = result.passInfo[i];
            const auto& pass = passes[i];

            // Only consider compute passes not already on async queue
            bool isCompute = (pass.flags & RGPassFlags::Compute) != RGPassFlags::None
                             && (pass.flags & RGPassFlags::Graphics) == RGPassFlags::None;
            if (!isCompute) {
                continue;
            }
            if (pass.queueHint == RGQueueType::AsyncCompute && config_.respectUserQueueHints) {
                continue;
            }
            if (pi.onCriticalPath) {
                continue;
            }
            if (pi.estimatedGpuTimeUs < config_.minPassDurationUs) {
                continue;
            }

            // Count cross-queue sync cost
            uint32_t newSyncEdges = 0;
            for (uint32_t pred : predecessors[i]) {
                if (passes[pred].queueHint == RGQueueType::Graphics) {
                    newSyncEdges++;
                }
            }
            for (uint32_t succ : successors[i]) {
                if (passes[succ].queueHint == RGQueueType::Graphics) {
                    newSyncEdges++;
                }
            }
            float syncCost = static_cast<float>(newSyncEdges) * config_.syncOverheadUs;

            // Overlap benefit = min(slack, passDuration)
            float overlapBenefit = std::min(pi.slack, pi.estimatedGpuTimeUs);

            bool profitable
                = overlapBenefit > syncCost && (overlapBenefit / pi.estimatedGpuTimeUs) >= config_.minOverlapRatio;

            result.candidates.push_back(
                AsyncPromotionCandidate{
                    .passIndex = i,
                    .estimatedGpuTimeUs = pi.estimatedGpuTimeUs,
                    .slack = pi.slack,
                    .estimatedOverlapBenefit = overlapBenefit,
                    .estimatedSyncCost = syncCost,
                    .profitable = profitable,
                }
            );
        }

        // Sort by benefit descending, limit promotions
        std::sort(result.candidates.begin(), result.candidates.end(), [](const auto& a, const auto& b) {
            return a.estimatedOverlapBenefit > b.estimatedOverlapBenefit;
        });

        float totalSavings = 0.0f;
        uint32_t promoted = 0;
        for (auto& c : result.candidates) {
            if (!c.profitable) {
                continue;
            }
            if (promoted >= config_.maxPromotionsPerFrame) {
                break;
            }
            totalSavings += c.estimatedOverlapBenefit - c.estimatedSyncCost;
            promoted++;
        }
        result.estimatedSavingsUs = totalSavings;
        result.estimatedFrameTimeUs = maxEnd - totalSavings;
        result.promotedCount = promoted;

        return result;
    }

    auto AsyncComputeDiscovery::ApplyPromotions(std::span<RGPassNode> passes, const AsyncDiscoveryResult& result) const
        -> uint32_t {
        uint32_t applied = 0;
        for (const auto& candidate : result.candidates) {
            if (!candidate.profitable) {
                continue;
            }
            if (applied >= config_.maxPromotionsPerFrame) {
                break;
            }
            if (candidate.passIndex < passes.size()) {
                auto& pass = passes[candidate.passIndex];
                pass.queueHint = RGQueueType::AsyncCompute;
                pass.flags = pass.flags | RGPassFlags::AsyncEligible;
                applied++;
            }
        }
        return applied;
    }

    auto AsyncComputeDiscovery::FormatStatus(const AsyncDiscoveryResult& result) const -> std::string {
        return std::format(
            "AsyncComputeDiscovery: critical_path={:.1f}us, candidates={}, promoted={}, savings={:.1f}us, "
            "estimated_frame={:.1f}us",
            result.criticalPathLengthUs, result.candidates.size(), result.promotedCount, result.estimatedSavingsUs,
            result.estimatedFrameTimeUs
        );
    }

    // =========================================================================
    // L-10: GpuBreadcrumbTracker
    // =========================================================================

    GpuBreadcrumbTracker::GpuBreadcrumbTracker(const BreadcrumbConfig& config) : config_(config) {
        // Ensure maxEntries is power of 2
        if (config_.maxEntries > 0) {
            uint32_t v = config_.maxEntries;
            v--;
            v |= v >> 1;
            v |= v >> 2;
            v |= v >> 4;
            v |= v >> 8;
            v |= v >> 16;
            v++;
            config_.maxEntries = v;
        }
    }

    GpuBreadcrumbTracker::~GpuBreadcrumbTracker() = default;
    GpuBreadcrumbTracker::GpuBreadcrumbTracker(GpuBreadcrumbTracker&&) noexcept = default;
    auto GpuBreadcrumbTracker::operator=(GpuBreadcrumbTracker&&) noexcept -> GpuBreadcrumbTracker& = default;

    auto GpuBreadcrumbTracker::Initialize(rhi::BufferHandle readbackBuffer) -> bool {
        if (config_.mode == BreadcrumbMode::Disabled) {
            return true;
        }
        buffer_ = readbackBuffer;
        return buffer_.IsValid();
    }

    void GpuBreadcrumbTracker::BeginFrame(uint64_t frameIndex) {
        if (config_.mode == BreadcrumbMode::Disabled) {
            return;
        }
        currentFrame_ = frameIndex;
        writeOffset_ = 0;
        stats_.framesTracked++;
    }

    auto GpuBreadcrumbTracker::WriteBreadcrumb(uint32_t passIndex, bool isAfter) -> uint64_t {
        if (config_.mode == BreadcrumbMode::Disabled) {
            return 0;
        }

        (void)passIndex;
        (void)isAfter;                                              // Used when RHI buffer mapping is wired in
        uint32_t offset = writeOffset_ & (config_.maxEntries - 1);  // Ring buffer wrap
        writeOffset_++;
        stats_.totalMarkersWritten++;

        // Return the byte offset into the breadcrumb buffer where the GPU should write.
        // The executor will emit a CmdWriteBufferMarker or UAV write at this offset.
        return static_cast<uint64_t>(offset) * sizeof(GpuBreadcrumb);
    }

    auto GpuBreadcrumbTracker::ReadCrashData() const -> std::vector<GpuBreadcrumb> {
        // In a real implementation, this reads from the persistent-mapped buffer.
        // The mapping survives device removal on most implementations.
        // For now, return empty — actual readback requires RHI buffer mapping.
        return {};
    }

    auto GpuBreadcrumbTracker::GetLastGoodPass() const -> uint32_t {
        auto data = ReadCrashData();
        uint32_t lastGood = RGPassHandle::kInvalid;
        for (const auto& b : data) {
            if ((b.marker & 1) == 1) {  // isAfter = true => pass completed
                lastGood = b.passIndex;
            }
        }
        return lastGood;
    }

    auto GpuBreadcrumbTracker::GetSuspectedCrashPass() const -> uint32_t {
        auto data = ReadCrashData();
        uint32_t lastBefore = RGPassHandle::kInvalid;
        for (const auto& b : data) {
            if ((b.marker & 1) == 0) {  // isAfter = false => pass started but may not have finished
                lastBefore = b.passIndex;
            }
        }
        return lastBefore;
    }

    auto GpuBreadcrumbTracker::FormatCrashReport(std::span<const char* const> passNames) const -> std::string {
        uint32_t lastGood = GetLastGoodPass();
        uint32_t suspected = GetSuspectedCrashPass();

        std::string report = std::format("=== GPU Crash Report (Frame {}) ===\n", currentFrame_);
        if (lastGood != RGPassHandle::kInvalid) {
            report += std::format("Last completed pass: #{}", lastGood);
            if (lastGood < passNames.size() && passNames[lastGood]) {
                report += std::format(" ({})", passNames[lastGood]);
            }
            report += "\n";
        }
        if (suspected != RGPassHandle::kInvalid) {
            report += std::format("Suspected crash pass: #{}", suspected);
            if (suspected < passNames.size() && passNames[suspected]) {
                report += std::format(" ({})", passNames[suspected]);
            }
            report += "\n";
        }
        report += std::format("Total markers written: {}\n", stats_.totalMarkersWritten);
        return report;
    }

    // =========================================================================
    // L-12: FenceBarrierResolver
    // =========================================================================

    FenceBarrierResolver::FenceBarrierResolver(const FenceBarrierTier2Config& config) : config_(config) {}

    auto FenceBarrierResolver::ResolveSyncPoints(std::span<const CrossQueueSyncPoint> syncPoints) const
        -> std::vector<FenceBarrierSyncPoint> {
        stats_ = {};
        stats_.totalSyncPoints = static_cast<uint32_t>(syncPoints.size());

        std::vector<FenceBarrierSyncPoint> result;

        if (!IsTier2Active()) {
            // Fallback: no conversion, all sync points remain as queue-level fence ops
            stats_.remainingQueueSync = stats_.totalSyncPoints;
            return result;
        }

        result.reserve(syncPoints.size());
        uint64_t nextFenceValue = 1;

        for (const auto& sp : syncPoints) {
            FenceBarrierSyncPoint fbsp;
            fbsp.srcPassIndex = sp.srcPassIndex;
            fbsp.dstPassIndex = sp.dstPassIndex;
            fbsp.fenceValue = nextFenceValue++;
            fbsp.srcQueue = sp.srcQueue;
            fbsp.dstQueue = sp.dstQueue;
            result.push_back(fbsp);
            stats_.convertedToFenceBarrier++;

            // Each converted sync point potentially saves one command list split
            if (config_.allowSubBatchSync) {
                stats_.splitsSaved++;
            }
        }

        stats_.remainingQueueSync = stats_.totalSyncPoints - stats_.convertedToFenceBarrier;
        return result;
    }

    auto FenceBarrierResolver::EstimateSplitReduction(std::span<const CrossQueueSyncPoint> syncPoints) const
        -> uint32_t {
        if (!IsTier2Active() || !config_.allowSubBatchSync) {
            return 0;
        }
        return static_cast<uint32_t>(syncPoints.size());
    }

    auto FenceBarrierResolver::FormatStatus() const -> std::string {
        if (!IsTier2Active()) {
            return "FenceBarrierResolver: Tier-2 not active (using queue-level sync)";
        }
        return std::format(
            "FenceBarrierResolver: {}/{} sync points converted to fence barriers, {} splits saved",
            stats_.convertedToFenceBarrier, stats_.totalSyncPoints, stats_.splitsSaved
        );
    }

}  // namespace miki::rg
