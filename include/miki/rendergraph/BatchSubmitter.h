/** @file BatchSubmitter.h
 *  @brief Phase 3 of render graph execution: queue submission with timeline sync.
 *
 *  Takes recorded command buffers from PassRecorder and submits them to GPU queues
 *  with correct cross-queue semaphore synchronization via SyncScheduler.
 *
 *  Stateless: all necessary data is passed through function parameters.
 *
 *  Namespace: miki::rg
 */
#pragma once

#include <span>

#include "miki/core/Result.h"
#include "miki/frame/SyncScheduler.h"
#include "miki/rendergraph/PassRecorder.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/Device.h"

namespace miki::rg {

    // =========================================================================
    // Submission statistics
    // =========================================================================

    struct SubmissionStats {
        uint32_t batchesSubmitted = 0;
    };

    // =========================================================================
    // SubmitBatches — free function (stateless)
    // =========================================================================

    /// @brief Submit recorded batches to GPU queues with timeline semaphore synchronization.
    /// @param graph The compiled render graph (for batch metadata and sync points).
    /// @param recordings The recorded command buffers from PassRecorder.
    /// @param device The RHI device handle.
    /// @param scheduler The sync scheduler for semaphore management.
    /// @param outStats Optional output statistics.
    [[nodiscard]] auto SubmitBatches(
        const CompiledRenderGraph& graph, std::span<const BatchRecording> recordings, rhi::DeviceHandle device,
        frame::SyncScheduler& scheduler, SubmissionStats* outStats = nullptr
    ) -> core::Result<void>;

}  // namespace miki::rg
