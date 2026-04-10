/** @file AsyncComputeScheduler.h
 *  @brief Adaptive async compute scheduling with occupancy-aware feedback.
 *
 *  Implements spec §7.2.1 Adaptive Async Threshold:
 *    - Per-pass EMA benefit estimator (alpha = 0.1)
 *    - Cold-start warm-up (first N frames use static threshold)
 *    - Hysteresis (pass stays async for M frames after benefit drops)
 *    - ComputeQueueLevel detection (A-D degradation)
 *    - QFOT strategy selection (Vulkan EXCLUSIVE images, CONCURRENT buffers)
 *
 *  Thread model: single-threaded (main thread only, called by compiler/executor).
 *
 *  See: specs/04-render-graph.md §7.2, §7.4, §7.5
 *  Namespace: miki::rg
 */
#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <unordered_map>
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
        // D3D12: no QFOT needed — resources implicitly shared across queues
        // WebGPU/OpenGL: single queue, no QFOT
        // Only Vulkan backends (1.4 and Compat) need QFOT
        if (backend != rhi::BackendType::Vulkan14 && backend != rhi::BackendType::VulkanCompat) {
            return QfotStrategy::None;
        }
        // Vulkan: buffers CONCURRENT, images EXCLUSIVE (AMD/NVIDIA guidance)
        return (kind == RGResourceKind::Buffer) ? QfotStrategy::Concurrent : QfotStrategy::Exclusive;
    }

    // =========================================================================
    // Async Compute Scheduler Configuration
    // =========================================================================

    struct AsyncComputeSchedulerConfig {
        float emaAlpha = 0.1f;               ///< EMA smoothing factor (0.0 = no update, 1.0 = instant)
        float staticThresholdUs = 200.0f;    ///< Cold-start static threshold (microseconds)
        float adaptiveThresholdUs = 50.0f;   ///< Post-warm-up adaptive threshold (lower, more aggressive)
        uint32_t warmUpFrames = 8;           ///< Frames before switching from static to adaptive
        uint32_t hysteresisFrames = 4;       ///< Frames to keep async after benefit drops below threshold
        float crossQueueSyncCostUs = 75.0f;  ///< Estimated sync overhead per queue transition (microseconds)
    };

    // =========================================================================
    // Per-pass async benefit estimator state
    // =========================================================================

    struct PassAsyncEstimate {
        float emaBenefitUs = 0.0f;        ///< Exponential moving average of measured benefit
        uint32_t framesOnAsync = 0;       ///< Consecutive frames this pass ran on async queue
        uint32_t framesSinceBenefit = 0;  ///< Frames since benefit exceeded threshold (hysteresis counter)
        bool isWarmingUp = true;          ///< Still in cold-start phase
        uint32_t frameCount = 0;          ///< Total frames tracked
    };

    // =========================================================================
    // GPU timestamp feedback (from previous frame)
    // =========================================================================

    struct PassTimingFeedback {
        uint32_t passIndex = 0;
        float asyncTimeUs = 0.0f;               ///< Actual GPU time when running async
        float overlappedGraphicsTimeUs = 0.0f;  ///< Graphics work that overlapped
        float totalFrameTimeUs = 0.0f;          ///< Frame time for context
    };

    // =========================================================================
    // Cross-queue deadlock detection result
    // =========================================================================

    struct DeadlockDetectionResult {
        bool hasCycle = false;
        std::vector<uint32_t> demotedPasses;  ///< Pass indices demoted to graphics queue
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
        /// Takes into account EMA benefit, warm-up, hysteresis, and queue level.
        [[nodiscard]] auto ShouldRunAsync(
            uint32_t passIndex, RGPassFlags flags, float estimatedGpuTimeUs = 0.0f
        ) const noexcept -> bool;

        /// @brief Update EMA estimators with timing feedback from the previous frame.
        /// Call once per frame before compilation.
        void UpdateFeedback(std::span<const PassTimingFeedback> feedback);

        /// @brief Advance frame counter. Call at the start of each frame.
        void BeginFrame() noexcept;

        /// @brief Get the per-pass estimator state (for diagnostics/profiling).
        [[nodiscard]] auto GetEstimate(uint32_t passIndex) const noexcept -> const PassAsyncEstimate*;

        /// @brief Get configuration (for inspection).
        [[nodiscard]] auto GetConfig() const noexcept -> const AsyncComputeSchedulerConfig& { return config_; }

        /// @brief Get the current global frame count.
        [[nodiscard]] auto GetFrameCount() const noexcept -> uint32_t { return globalFrameCount_; }

        // =====================================================================
        // Cross-queue deadlock prevention (§7.5)
        // =====================================================================

        /// @brief Detect cross-queue dependency cycles and demote offending passes.
        /// Operates on the cross-queue sync points produced by compiler Stage 5.
        /// Returns the set of passes demoted to graphics queue.
        [[nodiscard]] static auto DetectAndPreventDeadlocks(
            std::span<const CrossQueueSyncPoint> syncPoints, std::vector<RGQueueType>& queueAssignments,
            std::span<const RGPassNode> passes
        ) -> DeadlockDetectionResult;

       private:
        AsyncComputeSchedulerConfig config_;
        ComputeQueueLevel queueLevel_ = ComputeQueueLevel::D_GraphicsOnly;
        uint32_t globalFrameCount_ = 0;

        // Per-pass EMA state, keyed by pass index
        mutable std::unordered_map<uint32_t, PassAsyncEstimate> estimates_;
    };

}  // namespace miki::rg
