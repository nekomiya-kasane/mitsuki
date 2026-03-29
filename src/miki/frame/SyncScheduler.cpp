/** @file SyncScheduler.cpp
 *  @brief Cross-queue timeline semaphore dependency resolution.
 *  See: SyncScheduler.h, specs/03-sync.md §13
 */

#include "miki/frame/SyncScheduler.h"

#include <cassert>

namespace miki::frame {

    void SyncScheduler::Init(const rhi::QueueTimelines& timelines) {
        queues_[0].semaphore = timelines.graphics;
        queues_[1].semaphore = timelines.compute;
        queues_[2].semaphore = timelines.transfer;
        for (auto& q : queues_) {
            q.nextValue = 1;
            q.currentValue = 0;
            q.pendingWaits.clear();
        }
    }

    auto SyncScheduler::AllocateSignal(rhi::QueueType queue) -> uint64_t {
        auto& state = queues_[QueueIndex(queue)];
        return state.nextValue++;
    }

    void SyncScheduler::AddDependency(rhi::QueueType waitQueue, rhi::QueueType signalQueue, uint64_t signalValue,
                                      rhi::PipelineStage waitStage) {
        auto& waiter = queues_[QueueIndex(waitQueue)];
        auto signalSem = queues_[QueueIndex(signalQueue)].semaphore;
        waiter.pendingWaits.push_back({.semaphore = signalSem, .value = signalValue, .stageMask = waitStage});
    }

    auto SyncScheduler::GetPendingWaits(rhi::QueueType queue) const -> std::span<const SyncWaitEntry> {
        return queues_[QueueIndex(queue)].pendingWaits;
    }

    auto SyncScheduler::GetSignalValue(rhi::QueueType queue) const -> uint64_t {
        auto& state = queues_[QueueIndex(queue)];
        assert(state.nextValue > 1 && "AllocateSignal not called before GetSignalValue");
        return state.nextValue - 1;  // Last allocated value
    }

    auto SyncScheduler::GetSemaphore(rhi::QueueType queue) const -> rhi::SemaphoreHandle {
        return queues_[QueueIndex(queue)].semaphore;
    }

    auto SyncScheduler::GetCurrentValue(rhi::QueueType queue) const -> uint64_t {
        return queues_[QueueIndex(queue)].currentValue;
    }

    void SyncScheduler::CommitSubmit(rhi::QueueType queue) {
        auto& state = queues_[QueueIndex(queue)];
        state.currentValue = state.nextValue - 1;  // Mark last allocated as committed
        state.pendingWaits.clear();
    }

    void SyncScheduler::Reset() {
        for (auto& q : queues_) {
            q.nextValue = 1;
            q.currentValue = 0;
            q.pendingWaits.clear();
        }
    }

}  // namespace miki::frame
