/** @file FrameOrchestrator.h
 *  @brief Multi-window frame orchestration with shared sync infrastructure.
 *
 *  FrameOrchestrator owns AsyncTaskManager and DeferredDestructor.
 *  SyncScheduler is device-owned (per-device singleton); FrameOrchestrator
 *  provides a convenience accessor that delegates to DeviceHandle::GetSyncScheduler().
 *  It does NOT own FrameManagers — SurfaceManager does.
 *
 *  Typical usage:
 *    auto orch = FrameOrchestrator::Create(device, framesInFlight);
 *    // SurfaceManager and FrameManagers use device.GetSyncScheduler() directly.
 *
 *  See: specs/03-sync.md §2.1
 *  Namespace: miki::frame
 */
#pragma once

#include <cstdint>
#include <memory>

#include "miki/core/Result.h"
#include "miki/frame/ComputeQueueLevel.h"
#include "miki/rhi/Device.h"

namespace miki::frame {

    class SyncScheduler;
    class AsyncTaskManager;
    class DeferredDestructor;

    class FrameOrchestrator {
       public:
        ~FrameOrchestrator();

        FrameOrchestrator(const FrameOrchestrator&) = delete;
        auto operator=(const FrameOrchestrator&) -> FrameOrchestrator& = delete;
        FrameOrchestrator(FrameOrchestrator&&) noexcept;
        auto operator=(FrameOrchestrator&&) noexcept -> FrameOrchestrator&;

        /// @brief Create a FrameOrchestrator bound to a device.
        /// Uses the device-owned SyncScheduler, creates AsyncTaskManager
        /// and DeferredDestructor.
        /// @param iDevice         Valid device handle.
        /// @param iFramesInFlight Number of frame-in-flight slots for DeferredDestructor bins.
        [[nodiscard]] static auto Create(rhi::DeviceHandle iDevice, uint32_t iFramesInFlight = 2)
            -> core::Result<FrameOrchestrator>;

        /// @brief Access the device-owned SyncScheduler (convenience, delegates to device).
        /// Used by FrameManager (for timeline allocation) and RenderGraph (for cross-queue deps).
        [[nodiscard]] auto GetSyncScheduler() noexcept -> SyncScheduler&;
        [[nodiscard]] auto GetSyncScheduler() const noexcept -> const SyncScheduler&;

        /// @brief Access the device-global AsyncTaskManager.
        /// Used for long-running compute tasks (BLAS rebuild, GDeflate decode).
        [[nodiscard]] auto GetAsyncTaskManager() noexcept -> AsyncTaskManager&;
        [[nodiscard]] auto GetAsyncTaskManager() const noexcept -> const AsyncTaskManager&;

        /// @brief Access the device-global DeferredDestructor.
        /// Used by all FrameManagers for deferred GPU resource destruction.
        [[nodiscard]] auto GetDeferredDestructor() noexcept -> DeferredDestructor&;
        [[nodiscard]] auto GetDeferredDestructor() const noexcept -> const DeferredDestructor&;

        /// @brief Get the device handle.
        [[nodiscard]] auto GetDevice() const noexcept -> rhi::DeviceHandle;

        /// @brief Get the detected compute queue isolation level.
        [[nodiscard]] auto GetComputeQueueLevel() const noexcept -> ComputeQueueLevel;

        /// @brief Shutdown: wait for all async tasks, drain all deferred destructions.
        auto Shutdown() -> void;

       private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        explicit FrameOrchestrator(std::unique_ptr<Impl> iImpl);
    };

}  // namespace miki::frame
