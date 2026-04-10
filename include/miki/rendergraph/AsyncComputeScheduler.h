/** @file AsyncComputeScheduler.h
 *  @brief Adaptive async compute scheduling with occupancy-aware feedback.
 *
 *  Implements spec §7.2.1 Adaptive Async Threshold:
 *    - Per-pass EMA benefit estimator (alpha = 0.1)
 *    - Cold-start warm-up (first N frames use static threshold)
 *    - Hysteresis (pass stays async for M frames after benefit drops)
 *    - ComputeQueueLevel detection (A-D degradation)
 *    - QFOT strategy selection (Vulkan EXCLUSIVE images, CONCURRENT buffers)
 *    - Pipelined compute detection (AMD RDNA guidance: small dispatches on graphics queue)
 *    - Vendor-aware tuning (NVIDIA separate ACE vs AMD shared CU scheduling)
 *    - Transfer queue staging policy for DMA + GPU decompress chains
 *
 *  Thread model: single-threaded (main thread only, called by compiler/executor).
 *
 *  See: specs/04-render-graph.md §7.2, §7.4, §7.5, §7.6
 *  Namespace: miki::rg
 */
#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "miki/core/Hash.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/GpuCapabilityProfile.h"

namespace miki::rg {

    // =========================================================================
    // ComputeQueueLevel — degradation tiers (specs/03-sync.md §5.8.2)
    // =========================================================================

    enum class ComputeQueueLevel : uint8_t {
        A_DualQueuePriority,    ///< 2 queues + priority isolation
        B_SingleQueuePriority,  ///< 1 queue + global priority HIGH
        C_SingleQueueBatch,     ///< 1 queue + batch splitting only
        D_GraphicsOnly,         ///< No compute queue (T2/T3/T4)
    };

    /// @brief Detect compute queue level from GPU capabilities.
    [[nodiscard]] constexpr auto DetectComputeQueueLevel(const rhi::GpuCapabilityProfile& caps) noexcept
        -> ComputeQueueLevel {
        if (caps.computeQueueFamilyCount >= 2 && caps.hasGlobalPriority) {
            return ComputeQueueLevel::A_DualQueuePriority;
        }
        if (caps.hasAsyncCompute && caps.hasGlobalPriority) {
            return ComputeQueueLevel::B_SingleQueuePriority;
        }
        if (caps.hasAsyncCompute) {
            return ComputeQueueLevel::C_SingleQueueBatch;
        }
        return ComputeQueueLevel::D_GraphicsOnly;
    }

    // =========================================================================
    // GPU vendor classification (affects async compute scheduling heuristics)
    // =========================================================================

    enum class GpuVendor : uint8_t {
        Unknown,
        Nvidia,  ///< Separate async compute engine (hardware scheduler)
        Amd,     ///< Shared CUs via ACE, bandwidth-bound passes benefit most
        Intel,   ///< Limited async compute, integrated GPU considerations
        Apple,   ///< Tile-based, no true async compute
        Other,
    };

    /// @brief Classify GPU vendor from vendor ID (PCI vendor codes).
    [[nodiscard]] constexpr auto ClassifyGpuVendor(uint32_t vendorId) noexcept -> GpuVendor {
        switch (vendorId) {
            case 0x10DE: return GpuVendor::Nvidia;
            case 0x1002: return GpuVendor::Amd;
            case 0x8086: return GpuVendor::Intel;
            case 0x106B: return GpuVendor::Apple;
            default: return GpuVendor::Unknown;
        }
    }

    // =========================================================================
    // QFOT (Queue Family Ownership Transfer) strategy
    // =========================================================================

    /// @brief Per-resource QFOT strategy based on resource kind and backend.
    enum class QfotStrategy : uint8_t {
        None,        ///< No QFOT needed (D3D12, WebGPU, OpenGL, or same queue family)
        Concurrent,  ///< VK_SHARING_MODE_CONCURRENT (buffers — negligible perf diff)
        Exclusive,   ///< VK_SHARING_MODE_EXCLUSIVE + QFOT barriers (images — optimal layout transitions)
    };

    /// @brief Determine QFOT strategy for a resource crossing queue boundaries.
    [[nodiscard]] constexpr auto DetermineQfotStrategy(RGResourceKind kind, rhi::BackendType backend) noexcept
        -> QfotStrategy {
        if (backend != rhi::BackendType::Vulkan14 && backend != rhi::BackendType::VulkanCompat) {
            return QfotStrategy::None;
        }
        return (kind == RGResourceKind::Buffer) ? QfotStrategy::Concurrent : QfotStrategy::Exclusive;
    }

    // =========================================================================
    // Async compute dispatch classification (AMD RDNA Performance Guide)
    // =========================================================================

    /// @brief Classify whether a compute dispatch should use pipelined compute (same queue)
    /// or async compute (separate queue). AMD guidance: small dispatches + large draws
    /// benefit more from pipelined compute; large dispatches benefit from async.
    enum class ComputeDispatchMode : uint8_t {
        Pipelined,  ///< Execute on graphics queue (small dispatch, overlaps with pixel shader)
        Async,      ///< Execute on async compute queue (large dispatch, overlaps with frontend)
    };

    // =========================================================================
    // Async Compute Scheduler Configuration
    // =========================================================================

    struct AsyncComputeSchedulerConfig {
        float emaAlpha = 0.1f;                     ///< EMA smoothing factor (0.0 = no update, 1.0 = instant)
        float staticThresholdUs = 200.0f;          ///< Cold-start static threshold (microseconds)
        float adaptiveThresholdUs = 50.0f;         ///< Post-warm-up adaptive threshold (lower, more aggressive)
        uint32_t warmUpFrames = 8;                 ///< Frames before switching from static to adaptive
        uint32_t hysteresisFrames = 4;             ///< Frames to keep async after benefit drops below threshold
        float crossQueueSyncCostUs = 75.0f;        ///< Estimated sync overhead per queue transition (microseconds)
        GpuVendor gpuVendor = GpuVendor::Unknown;  ///< Vendor for scheduling heuristics

        // Pipelined compute thresholds (AMD RDNA guidance)
        uint32_t pipelinedMaxWorkGroups = 64;  ///< Dispatches <= this use pipelined compute on AMD
        float pipelinedMaxGpuTimeUs = 100.0f;  ///< Max GPU time for pipelined compute path

        // Transfer queue scheduling
        float transferMinSizeBytes = 64.0f * 1024;  ///< Min transfer size to justify dedicated queue (64KB)
        bool enableTransferQueue = true;            ///< Allow dedicated transfer queue scheduling
    };

    // =========================================================================
    // Per-pass async benefit estimator state
    // =========================================================================

    struct PassAsyncEstimate {
        float emaBenefitUs = 0.0f;                                  ///< Exponential moving average of measured benefit
        float emaAsyncTimeUs = 0.0f;                                ///< EMA of actual async execution time
        float emaOverlapTimeUs = 0.0f;                              ///< EMA of overlapped graphics time
        uint32_t framesOnAsync = 0;                                 ///< Consecutive frames this pass ran on async queue
        uint32_t framesSinceBenefit = 0;                            ///< Frames since benefit exceeded threshold
        bool isWarmingUp = true;                                    ///< Still in cold-start phase
        uint32_t frameCount = 0;                                    ///< Total frames tracked
        ComputeDispatchMode lastMode = ComputeDispatchMode::Async;  ///< Last scheduling decision
    };

    // =========================================================================
    // GPU timestamp feedback (from previous frame)
    // =========================================================================

    struct PassTimingFeedback {
        uint32_t passIndex = 0;
        float asyncTimeUs = 0.0f;               ///< Actual GPU time when running async
        float overlappedGraphicsTimeUs = 0.0f;  ///< Graphics work that overlapped
        float totalFrameTimeUs = 0.0f;          ///< Frame time for context
        uint32_t dispatchGroupCount = 0;        ///< Total workgroups dispatched (for pipelined heuristic)
    };

    // =========================================================================
    // Cross-queue deadlock detection result
    // =========================================================================

    struct DeadlockDetectionResult {
        bool hasCycle = false;
        std::vector<uint32_t> demotedPasses;  ///< Pass indices demoted to graphics queue
    };

    // =========================================================================
    // Cross-queue scheduling statistics (for profiling / debug output)
    // =========================================================================

    struct CrossQueueSchedulingStats {
        uint32_t totalAsyncPasses = 0;         ///< Passes scheduled on async compute
        uint32_t totalTransferPasses = 0;      ///< Passes scheduled on transfer queue
        uint32_t pipelinedComputePasses = 0;   ///< Async-eligible passes kept on graphics (pipelined)
        uint32_t demotedToGraphics = 0;        ///< Passes demoted by deadlock prevention
        uint32_t crossQueueSyncPoints = 0;     ///< Total cross-queue sync points emitted
        uint32_t qfotExclusiveBarriers = 0;    ///< QFOT release/acquire pairs (Vulkan EXCLUSIVE)
        uint32_t qfotConcurrentBarriers = 0;   ///< Cross-queue barriers (CONCURRENT/D3D12)
        float estimatedSyncOverheadUs = 0.0f;  ///< Estimated total sync cost in microseconds
    };

    // =========================================================================
    // AsyncComputeScheduler
    // =========================================================================

    class AsyncComputeScheduler {
       public:
        explicit AsyncComputeScheduler(const AsyncComputeSchedulerConfig& config = {}) noexcept;

        /// @brief Set the compute queue level detected from GPU capabilities.
        void SetComputeQueueLevel(ComputeQueueLevel level) noexcept { queueLevel_ = level; }

        /// @brief Get current compute queue level.
        [[nodiscard]] auto GetComputeQueueLevel() const noexcept -> ComputeQueueLevel { return queueLevel_; }

        /// @brief Evaluate whether a pass should run on async compute queue.
        /// Returns true if the pass should use async compute, false for graphics.
        /// Takes into account EMA benefit, warm-up, hysteresis, queue level, and
        /// vendor-specific pipelined compute heuristics.
        [[nodiscard]] auto ShouldRunAsync(
            uint32_t passIndex, RGPassFlags flags, float estimatedGpuTimeUs = 0.0f, uint32_t workGroupCount = 0
        ) const noexcept -> bool;

        /// @brief Classify dispatch mode: pipelined (graphics queue) vs async (separate queue).
        /// AMD RDNA guidance: small dispatches (<64 workgroups) prefer pipelined compute.
        [[nodiscard]] auto ClassifyDispatchMode(
            uint32_t passIndex, RGPassFlags flags, float estimatedGpuTimeUs, uint32_t workGroupCount = 0
        ) const noexcept -> ComputeDispatchMode;

        /// @brief Update EMA estimators with timing feedback from the previous frame.
        void UpdateFeedback(std::span<const PassTimingFeedback> feedback);

        /// @brief Advance frame counter. Call at the start of each frame.
        void BeginFrame() noexcept;

        /// @brief Get the per-pass estimator state (for diagnostics/profiling).
        [[nodiscard]] auto GetEstimate(uint32_t passIndex) const noexcept -> const PassAsyncEstimate*;

        /// @brief Get configuration (for inspection).
        [[nodiscard]] auto GetConfig() const noexcept -> const AsyncComputeSchedulerConfig& { return config_; }

        /// @brief Get the current global frame count.
        [[nodiscard]] auto GetFrameCount() const noexcept -> uint32_t { return globalFrameCount_; }

        /// @brief Get scheduling statistics from the last frame.
        [[nodiscard]] auto GetStats() const noexcept -> const CrossQueueSchedulingStats& { return stats_; }

        /// @brief Reset per-frame scheduling statistics. Called by BeginFrame().
        void ResetStats() noexcept { stats_ = {}; }

        /// @brief Ensure internal storage covers at least `passCount` passes.
        void Reserve(uint32_t passCount);

        // =====================================================================
        // Cross-queue deadlock prevention (§7.5)
        // =====================================================================

        /// @brief Detect cross-queue dependency cycles and demote offending passes.
        /// Uses Kahn's algorithm on the cross-queue subgraph with flat vectors.
        /// Prioritizes demoting lower-priority passes first.
        [[nodiscard]] static auto DetectAndPreventDeadlocks(
            std::span<const CrossQueueSyncPoint> syncPoints, std::vector<RGQueueType>& queueAssignments,
            std::span<const RGPassNode> passes
        ) -> DeadlockDetectionResult;

       private:
        /// @brief Ensure estimator exists for a given pass index.
        void EnsureEstimate(uint32_t passIndex) const;

        AsyncComputeSchedulerConfig config_;
        ComputeQueueLevel queueLevel_ = ComputeQueueLevel::D_GraphicsOnly;
        uint32_t globalFrameCount_ = 0;
        mutable CrossQueueSchedulingStats stats_;

        // Per-pass EMA state — flat vector indexed by pass index (O(1) lookup, cache-friendly)
        // Grows on demand via EnsureEstimate(). Typical size: 10-200 passes.
        mutable std::vector<PassAsyncEstimate> estimates_;
        mutable std::vector<bool> estimateValid_;  ///< Whether estimates_[i] has been initialized
    };

}  // namespace miki::rg
