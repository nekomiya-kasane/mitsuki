/** @file AsyncTaskManager.cpp
 *  @brief Long-running cross-frame async compute task management.
 *  See: AsyncTaskManager.h, specs/03-sync.md §5.6
 */

#include "miki/frame/AsyncTaskManager.h"

#include <algorithm>
#include <cassert>
#include <vector>

#include "miki/rhi/backend/AllBackends.h"

namespace miki::frame {

    struct AsyncTaskManager::Impl {
        rhi::DeviceHandle device;
        SyncScheduler* scheduler = nullptr;
        ComputeQueueLevel level = ComputeQueueLevel::D_GraphicsOnly;
        uint64_t nextTaskId = 1;

        struct TaskEntry {
            AsyncTaskHandle handle;
            TimelineSyncPoint completionPoint;
        };
        std::vector<TaskEntry> activeTasks;

        void PruneCompleted() {
            std::erase_if(activeTasks, [&](const TaskEntry& t) {
                uint64_t gpuValue
                    = device.Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(t.completionPoint.semaphore); });
                return gpuValue >= t.completionPoint.value;
            });
        }
    };

    AsyncTaskManager::~AsyncTaskManager() {
        if (impl_) {
            Shutdown();
        }
    }

    AsyncTaskManager::AsyncTaskManager(AsyncTaskManager&&) noexcept = default;
    auto AsyncTaskManager::operator=(AsyncTaskManager&&) noexcept -> AsyncTaskManager& = default;
    AsyncTaskManager::AsyncTaskManager(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    auto AsyncTaskManager::Create(rhi::DeviceHandle iDevice, SyncScheduler& iScheduler, ComputeQueueLevel iLevel)
        -> core::Result<AsyncTaskManager> {
        if (!iDevice.IsValid()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }
        auto impl = std::make_unique<Impl>();
        impl->device = iDevice;
        impl->scheduler = &iScheduler;
        impl->level = iLevel;
        return AsyncTaskManager(std::move(impl));
    }

    auto AsyncTaskManager::Submit(rhi::CommandBufferHandle iCmd, std::span<const rhi::SemaphoreSubmitInfo> iWaits)
        -> core::Result<AsyncTaskHandle> {
        assert(impl_ && "AsyncTaskManager used after move");

        // Level D: no compute queue, submit to graphics queue during idle
        // Level A/B/C: submit to AsyncCompute queue (Level A: separate VkQueue, else alias Compute)
        auto submitQueue = (impl_->level == ComputeQueueLevel::D_GraphicsOnly) ? rhi::QueueType::Graphics
                                                                               : rhi::QueueType::AsyncCompute;
        uint64_t signalValue = impl_->scheduler->AllocateSignal(submitQueue);
        auto computeSem = impl_->scheduler->GetSemaphore(submitQueue);

        // Build signal info
        rhi::SemaphoreSubmitInfo signalInfo{
            .semaphore = computeSem, .value = signalValue, .stageMask = rhi::PipelineStage::ComputeShader
        };

        rhi::SubmitDesc submitDesc{
            .commandBuffers = std::span<const rhi::CommandBufferHandle>(&iCmd, 1),
            .waitSemaphores = iWaits,
            .signalSemaphores = std::span<const rhi::SemaphoreSubmitInfo>(&signalInfo, 1),
            .signalFence = {},
        };
        impl_->device.Dispatch([&](auto& dev) { dev.Submit(submitQueue, submitDesc); });
        impl_->scheduler->CommitSubmit(submitQueue);

        // Track the task
        AsyncTaskHandle handle{.id = impl_->nextTaskId++};
        impl_->activeTasks.push_back(
            {.handle = handle, .completionPoint = {.semaphore = computeSem, .value = signalValue}}
        );

        return handle;
    }

    auto AsyncTaskManager::IsComplete(AsyncTaskHandle iTask) const noexcept -> bool {
        assert(impl_ && "AsyncTaskManager used after move");
        auto it = std::ranges::find_if(impl_->activeTasks, [&](const Impl::TaskEntry& t) {
            return t.handle.id == iTask.id;
        });
        if (it == impl_->activeTasks.end()) {
            return true;  // Unknown task = already completed and pruned
        }
        uint64_t gpuValue
            = impl_->device.Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(it->completionPoint.semaphore); });
        return gpuValue >= it->completionPoint.value;
    }

    auto AsyncTaskManager::GetCompletionPoint(AsyncTaskHandle iTask) const noexcept -> TimelineSyncPoint {
        assert(impl_ && "AsyncTaskManager used after move");
        auto it = std::ranges::find_if(impl_->activeTasks, [&](const Impl::TaskEntry& t) {
            return t.handle.id == iTask.id;
        });
        if (it == impl_->activeTasks.end()) {
            return {};
        }
        return it->completionPoint;
    }

    auto AsyncTaskManager::WaitForCompletion(AsyncTaskHandle iTask, uint64_t iTimeoutNs) -> core::Result<void> {
        assert(impl_ && "AsyncTaskManager used after move");
        auto it = std::ranges::find_if(impl_->activeTasks, [&](const Impl::TaskEntry& t) {
            return t.handle.id == iTask.id;
        });
        if (it == impl_->activeTasks.end()) {
            return {};  // Already completed
        }
        impl_->device.Dispatch([&](auto& dev) {
            dev.WaitSemaphore(it->completionPoint.semaphore, it->completionPoint.value, iTimeoutNs);
        });
        return {};
    }

    auto AsyncTaskManager::SubmitBatched(
        std::span<const rhi::CommandBufferHandle> iBatches, std::span<const rhi::SemaphoreSubmitInfo> iWaits
    ) -> core::Result<AsyncTaskHandle> {
        assert(impl_ && "AsyncTaskManager used after move");
        if (iBatches.empty()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        auto submitQueue = (impl_->level == ComputeQueueLevel::D_GraphicsOnly) ? rhi::QueueType::Graphics
                                                                               : rhi::QueueType::AsyncCompute;
        auto computeSem = impl_->scheduler->GetSemaphore(submitQueue);
        uint64_t lastSignalValue = 0;

        for (size_t i = 0; i < iBatches.size(); ++i) {
            uint64_t signalValue = impl_->scheduler->AllocateSignal(submitQueue);
            rhi::SemaphoreSubmitInfo signalInfo{
                .semaphore = computeSem, .value = signalValue, .stageMask = rhi::PipelineStage::ComputeShader
            };

            rhi::SubmitDesc submitDesc{
                .commandBuffers = std::span<const rhi::CommandBufferHandle>(&iBatches[i], 1),
                .waitSemaphores = (i == 0) ? iWaits : std::span<const rhi::SemaphoreSubmitInfo>{},
                .signalSemaphores = std::span<const rhi::SemaphoreSubmitInfo>(&signalInfo, 1),
                .signalFence = {},
            };
            impl_->device.Dispatch([&](auto& dev) { dev.Submit(submitQueue, submitDesc); });
            impl_->scheduler->CommitSubmit(submitQueue);
            lastSignalValue = signalValue;
        }

        AsyncTaskHandle handle{.id = impl_->nextTaskId++};
        impl_->activeTasks.push_back(
            {.handle = handle, .completionPoint = {.semaphore = computeSem, .value = lastSignalValue}}
        );
        return handle;
    }

    auto AsyncTaskManager::ActiveTaskCount() const noexcept -> uint32_t {
        return impl_ ? static_cast<uint32_t>(impl_->activeTasks.size()) : 0;
    }

    auto AsyncTaskManager::Shutdown() -> void {
        if (!impl_) {
            return;
        }
        // Wait for all active tasks
        for (auto& task : impl_->activeTasks) {
            impl_->device.Dispatch([&](auto& dev) {
                dev.WaitSemaphore(task.completionPoint.semaphore, task.completionPoint.value, UINT64_MAX);
            });
        }
        impl_->activeTasks.clear();
    }

}  // namespace miki::frame
