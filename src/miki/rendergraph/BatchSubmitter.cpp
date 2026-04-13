/** @file BatchSubmitter.cpp
 *  @brief Phase 3 of render graph execution: queue submission with timeline sync.
 *  See: BatchSubmitter.h
 */

#include "miki/rendergraph/BatchSubmitter.h"

#include "miki/debug/StructuredLogger.h"
#include "miki/core/EnumStrings.h"
#include "miki/rhi/backend/AllBackends.h"

#include <vector>

namespace miki::rg {

    auto SubmitBatches(
        const CompiledRenderGraph& graph, std::span<const BatchRecording> recordings, rhi::DeviceHandle device,
        frame::SyncScheduler& scheduler, SubmissionStats* outStats
    ) -> core::Result<void> {
        SubmissionStats stats{};

        for (size_t bi = 0; bi < graph.batches.size() && bi < recordings.size(); ++bi) {
            auto& batch = graph.batches[bi];
            auto& recording = recordings[bi];
            auto rhiQueue = ToRhiQueueType(batch.queue);

            // Build wait semaphores from cross-queue dependencies
            std::vector<rhi::SemaphoreSubmitInfo> waitSems;
            for (auto& wait : batch.waits) {
                auto srcRhiQueue = ToRhiQueueType(wait.srcQueue);
                auto sem = scheduler.GetSemaphore(srcRhiQueue);
                if (sem.IsValid() && wait.timelineValue > 0) {
                    waitSems.push_back({
                        .semaphore = sem,
                        .value = wait.timelineValue,
                        .stageMask = rhi::PipelineStage::AllCommands,
                    });
                }
            }

            // Add pending waits from scheduler
            auto pendingWaits = scheduler.GetPendingWaits(rhiQueue);
            for (auto& pw : pendingWaits) {
                waitSems.push_back({
                    .semaphore = pw.semaphore,
                    .value = pw.value,
                    .stageMask = pw.stageMask,
                });
            }

            // Build signal semaphores
            std::vector<rhi::SemaphoreSubmitInfo> signalSems;
            if (batch.signalTimeline) {
                auto signalValue = scheduler.AllocateSignal(rhiQueue);
                auto sem = scheduler.GetSemaphore(rhiQueue);
                if (sem.IsValid()) {
                    signalSems.push_back({
                        .semaphore = sem,
                        .value = signalValue,
                        .stageMask = rhi::PipelineStage::AllCommands,
                    });
                }
            }

            // Submit
            rhi::SubmitDesc submitDesc{
                .commandBuffers = recording.commandBuffers,
                .waitSemaphores = waitSems,
                .signalSemaphores = signalSems,
                .signalFence = {},
            };
            MIKI_LOG_DEBUG(
                ::miki::debug::LogCategory::Rhi,
                "[Sync] RG BatchSubmitter: batch [{}/{}] queue={} cmds=[{}] waits=[{}] signals=[{}]",
                bi + 1, graph.batches.size(), rhi::ToString(rhiQueue),
                recording.commandBuffers.size(), waitSems.size(), signalSems.size()
            );
            for (auto& w : waitSems) {
                MIKI_LOG_DEBUG(
                    ::miki::debug::LogCategory::Rhi,
                    "[Sync]   wait: sem=[0x{:x}] value=[{}]", w.semaphore.value, w.value
                );
            }
            for (auto& s : signalSems) {
                MIKI_LOG_DEBUG(
                    ::miki::debug::LogCategory::Rhi,
                    "[Sync]   signal: sem=[0x{:x}] value=[{}]", s.semaphore.value, s.value
                );
            }
            device.Dispatch([&](auto& dev) { dev.Submit(rhiQueue, submitDesc); });
            MIKI_LOG_DEBUG(
                ::miki::debug::LogCategory::Rhi,
                "[Sync] RG BatchSubmitter: CommitSubmit queue={}",
                rhi::ToString(rhiQueue)
            );
            scheduler.CommitSubmit(rhiQueue);

            stats.batchesSubmitted++;
        }

        if (outStats) {
            *outStats = stats;
        }
        return {};
    }

}  // namespace miki::rg
