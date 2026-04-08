/** @file FrameOrchestrator.cpp
 *  @brief Multi-window frame orchestration with shared sync infrastructure.
 *  See: FrameOrchestrator.h, specs/03-sync.md §2.1
 */

#include "miki/frame/FrameOrchestrator.h"

#include <cassert>

#include "miki/debug/StructuredLogger.h"
#include "miki/frame/AsyncTaskManager.h"
#include "miki/frame/ComputeQueueLevel.h"
#include "miki/frame/DeferredDestructor.h"
#include "miki/frame/SyncScheduler.h"
#include "miki/rhi/backend/AllBackends.h"

namespace miki::frame {

    struct FrameOrchestrator::Impl {
        rhi::DeviceHandle device;
        ComputeQueueLevel computeLevel = ComputeQueueLevel::D_GraphicsOnly;
        SyncScheduler syncScheduler;
        AsyncTaskManager asyncTaskManager;
        DeferredDestructor deferredDestructor;

        Impl(
            SyncScheduler&& sched, AsyncTaskManager&& atm, DeferredDestructor&& dd, rhi::DeviceHandle dev,
            ComputeQueueLevel level
        )
            : device(dev)
            , computeLevel(level)
            , syncScheduler(std::move(sched))
            , asyncTaskManager(std::move(atm))
            , deferredDestructor(std::move(dd)) {}
    };

    FrameOrchestrator::~FrameOrchestrator() {
        if (impl_) {
            Shutdown();
        }
    }

    FrameOrchestrator::FrameOrchestrator(FrameOrchestrator&&) noexcept = default;
    auto FrameOrchestrator::operator=(FrameOrchestrator&&) noexcept -> FrameOrchestrator& = default;
    FrameOrchestrator::FrameOrchestrator(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    auto FrameOrchestrator::Create(rhi::DeviceHandle iDevice, uint32_t iFramesInFlight)
        -> core::Result<FrameOrchestrator> {
        if (!iDevice.IsValid()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        // Initialize SyncScheduler from device's global timeline semaphores
        SyncScheduler scheduler;
        auto timelines = iDevice.Dispatch([](const auto& dev) { return dev.GetQueueTimelines(); });
        scheduler.Init(timelines);

        // Detect compute queue level
        auto caps = iDevice.Dispatch([](const auto& dev) -> const rhi::GpuCapabilityProfile& {
            return dev.GetCapabilities();
        });
        auto computeLevel = DetectComputeQueueLevel(caps);
        MIKI_LOG_INFO(debug::LogCategory::Render, "[Frame] ComputeQueueLevel: {}", ComputeQueueLevelName(computeLevel));

        // Create AsyncTaskManager with level awareness
        auto atmResult = AsyncTaskManager::Create(iDevice, scheduler, computeLevel);
        if (!atmResult) {
            return std::unexpected(core::ErrorCode::InvalidState);
        }

        // Create DeferredDestructor
        auto dd = DeferredDestructor::Create(iDevice, iFramesInFlight);

        auto impl
            = std::make_unique<Impl>(std::move(scheduler), std::move(*atmResult), std::move(dd), iDevice, computeLevel);
        return FrameOrchestrator(std::move(impl));
    }

    auto FrameOrchestrator::GetSyncScheduler() noexcept -> SyncScheduler& {
        assert(impl_ && "FrameOrchestrator used after move");
        return impl_->syncScheduler;
    }

    auto FrameOrchestrator::GetSyncScheduler() const noexcept -> const SyncScheduler& {
        assert(impl_ && "FrameOrchestrator used after move");
        return impl_->syncScheduler;
    }

    auto FrameOrchestrator::GetAsyncTaskManager() noexcept -> AsyncTaskManager& {
        assert(impl_ && "FrameOrchestrator used after move");
        return impl_->asyncTaskManager;
    }

    auto FrameOrchestrator::GetAsyncTaskManager() const noexcept -> const AsyncTaskManager& {
        assert(impl_ && "FrameOrchestrator used after move");
        return impl_->asyncTaskManager;
    }

    auto FrameOrchestrator::GetDeferredDestructor() noexcept -> DeferredDestructor& {
        assert(impl_ && "FrameOrchestrator used after move");
        return impl_->deferredDestructor;
    }

    auto FrameOrchestrator::GetDeferredDestructor() const noexcept -> const DeferredDestructor& {
        assert(impl_ && "FrameOrchestrator used after move");
        return impl_->deferredDestructor;
    }

    auto FrameOrchestrator::GetDevice() const noexcept -> rhi::DeviceHandle {
        return impl_ ? impl_->device : rhi::DeviceHandle{};
    }

    auto FrameOrchestrator::GetComputeQueueLevel() const noexcept -> ComputeQueueLevel {
        return impl_ ? impl_->computeLevel : ComputeQueueLevel::D_GraphicsOnly;
    }

    auto FrameOrchestrator::Shutdown() -> void {
        if (!impl_) {
            return;
        }
        impl_->asyncTaskManager.Shutdown();
        impl_->device.Dispatch([](auto& dev) { dev.WaitIdle(); });
        impl_->deferredDestructor.DrainAll();
    }

}  // namespace miki::frame
