/** @file FrameManager.cpp
 *  @brief Timeline-first frame pacing — tier-adaptive GPU/CPU synchronization.
 *
 *  Architecture:
 *    - Ring of N frame slots (default 2, max 3).
 *    - Each slot tracks a monotonic timeline value.
 *    - BeginFrame: CPU waits until the oldest slot's timeline value is reached.
 *    - EndFrame:   Submits command buffers, signals next timeline value, presents.
 *
 *  Tier adaptation:
 *    T1 (Vulkan 1.4 / D3D12): Timeline semaphore — single object per queue.
 *      CPU waits via WaitSemaphore(timeline, oldValue). GPU signals via SubmitDesc.
 *    T2 (Vulkan Compat):       Binary semaphore + VkFence per frame slot.
 *      CPU waits via WaitFence. GPU signals fence in SubmitDesc.
 *    T3 (WebGPU):              Implicit sync — wgpuQueueSubmit is synchronous enough.
 *      No explicit CPU wait; Present blocks if needed.
 *    T4 (OpenGL):              Implicit sync — glfwSwapBuffers blocks.
 *      No explicit wait.
 *
 *  See: specs/03-sync.md §4, specs/01-window-manager.md §5–§6.
 */

#include "miki/frame/FrameManager.h"

#include <algorithm>
#include <array>
#include <cassert>

#include "miki/frame/DeferredDestructor.h"
#include "miki/rhi/GpuCapabilityProfile.h"
#include "miki/rhi/RenderSurface.h"
#include "miki/rhi/backend/AllBackends.h"

namespace miki::frame {

    // =========================================================================
    // Per-frame slot — tracks one in-flight frame's sync state
    // =========================================================================

    struct FrameSlot {
        uint64_t timelineValue = 0;       // Timeline value this slot was submitted at
        rhi::FenceHandle fence;           // T2 only: per-slot binary fence
        rhi::SemaphoreHandle imageAvail;  // T1/T2: binary sem for swapchain acquire
        rhi::SemaphoreHandle renderDone;  // T1/T2: binary sem for present wait
    };

    // =========================================================================
    // FrameManager::Impl
    // =========================================================================

    struct FrameManager::Impl {
        rhi::DeviceHandle device;
        rhi::RenderSurface* surface = nullptr;  // nullptr = offscreen mode
        rhi::CapabilityTier tier = rhi::CapabilityTier::Tier4_OpenGL;

        // Frame ring
        uint32_t framesInFlight = kDefaultFramesInFlight;
        uint32_t frameIndex = 0;   // [0, framesInFlight) — current slot
        uint64_t frameNumber = 0;  // Monotonic, never wraps
        std::array<FrameSlot, kMaxFramesInFlight> slots{};

        // Timeline semaphore (T1 only — single object, shared across all slots)
        rhi::SemaphoreHandle graphicsTimeline;
        uint64_t currentTimelineValue = 0;  // Last signaled value

        // Cross-queue sync points
        TimelineSyncPoint computeSync;
        TimelineSyncPoint transferSync;

        // Resource lifecycle hooks (optional, null = disabled)
        resource::StagingRing* stagingRing = nullptr;
        resource::ReadbackRing* readbackRing = nullptr;
        DeferredDestructor* deferredDestructor = nullptr;

        // Offscreen dimensions (only used in offscreen mode)
        uint32_t offscreenWidth = 0;
        uint32_t offscreenHeight = 0;

        // ── Helpers ──────────────────────────────────────────────

        [[nodiscard]] auto IsTimeline() const noexcept -> bool {
            return tier == rhi::CapabilityTier::Tier1_Vulkan || tier == rhi::CapabilityTier::Tier1_D3D12;
        }

        [[nodiscard]] auto IsFenceBased() const noexcept -> bool { return tier == rhi::CapabilityTier::Tier2_Compat; }

        [[nodiscard]] auto IsImplicitSync() const noexcept -> bool {
            return tier == rhi::CapabilityTier::Tier3_WebGPU || tier == rhi::CapabilityTier::Tier4_OpenGL;
        }

        void WaitForSlot(uint32_t slotIdx) {
            auto& slot = slots[slotIdx];
            if (slot.timelineValue == 0) {
                return;  // Never submitted
            }

            if (IsTimeline()) {
                // T1: CPU wait on timeline semaphore for this slot's value
                device.Dispatch([&](auto& dev) {
                    dev.WaitSemaphore(graphicsTimeline, slot.timelineValue, UINT64_MAX);
                });
            } else if (IsFenceBased()) {
                // T2: CPU wait on per-slot fence
                if (slot.fence.IsValid()) {
                    device.Dispatch([&](auto& dev) {
                        dev.WaitFence(slot.fence, UINT64_MAX);
                        dev.ResetFence(slot.fence);
                    });
                }
            }
            // T3/T4: implicit sync, no CPU wait needed
        }

        void CreateSyncObjects() {
            if (IsTimeline()) {
                // T1: Reuse the device-global graphics timeline semaphore (specs/03-sync.md §8.2)
                auto timelines = device.Dispatch([](const auto& dev) { return dev.GetQueueTimelines(); });
                graphicsTimeline = timelines.graphics;
            }

            for (uint32_t i = 0; i < framesInFlight; ++i) {
                if (IsFenceBased()) {
                    // T2: Per-slot fence (signaled initially so first WaitFence doesn't block)
                    auto fenceResult = device.Dispatch([&](auto& dev) { return dev.CreateFence(true); });
                    if (fenceResult) {
                        slots[i].fence = *fenceResult;
                    }
                }

                if (!IsImplicitSync()) {
                    // T1/T2: Binary semaphores for swapchain acquire/present
                    rhi::SemaphoreDesc binDesc{.type = rhi::SemaphoreType::Binary, .initialValue = 0};
                    auto availResult = device.Dispatch([&](auto& dev) { return dev.CreateSemaphore(binDesc); });
                    if (availResult) {
                        slots[i].imageAvail = *availResult;
                    }

                    auto doneResult = device.Dispatch([&](auto& dev) { return dev.CreateSemaphore(binDesc); });
                    if (doneResult) {
                        slots[i].renderDone = *doneResult;
                    }
                }
            }
        }

        void DestroySyncObjects() {
            for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
                auto& slot = slots[i];
                if (slot.fence.IsValid()) {
                    device.Dispatch([&](auto& dev) { dev.DestroyFence(slot.fence); });
                    slot.fence = {};
                }
                if (slot.imageAvail.IsValid()) {
                    device.Dispatch([&](auto& dev) { dev.DestroySemaphore(slot.imageAvail); });
                    slot.imageAvail = {};
                }
                if (slot.renderDone.IsValid()) {
                    device.Dispatch([&](auto& dev) { dev.DestroySemaphore(slot.renderDone); });
                    slot.renderDone = {};
                }
            }
            // graphicsTimeline is device-global — do NOT destroy it here.
            // It is owned by VulkanDevice and destroyed in ~VulkanDevice.
            graphicsTimeline = {};
        }
    };

    // =========================================================================
    // Lifecycle
    // =========================================================================

    FrameManager::~FrameManager() {
        if (impl_) {
            // Wait for all in-flight work before destroying sync objects
            WaitAll();
            impl_->DestroySyncObjects();
        }
    }

    FrameManager::FrameManager(FrameManager&&) noexcept = default;
    auto FrameManager::operator=(FrameManager&&) noexcept -> FrameManager& = default;
    FrameManager::FrameManager(std::unique_ptr<Impl> iImpl) : impl_(std::move(iImpl)) {}

    auto FrameManager::Create(rhi::DeviceHandle iDevice, rhi::RenderSurface& iSurface, uint32_t iFramesInFlight)
        -> core::Result<FrameManager> {
        if (!iDevice.IsValid()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        auto impl = std::make_unique<Impl>();
        impl->device = iDevice;
        impl->surface = &iSurface;
        impl->framesInFlight = std::clamp(iFramesInFlight, 1u, kMaxFramesInFlight);
        impl->tier = iDevice.Dispatch([](const auto& dev) { return dev.GetCapabilities().tier; });

        impl->CreateSyncObjects();

        // Wire sync info into RenderSurface for AcquireNextImage/Present
        // (T3/T4: these stay invalid, which is fine — RenderSurface handles it)

        return FrameManager(std::move(impl));
    }

    auto FrameManager::CreateOffscreen(
        rhi::DeviceHandle iDevice, uint32_t iWidth, uint32_t iHeight, uint32_t iFramesInFlight
    ) -> core::Result<FrameManager> {
        if (!iDevice.IsValid()) {
            return std::unexpected(core::ErrorCode::InvalidArgument);
        }

        auto impl = std::make_unique<Impl>();
        impl->device = iDevice;
        impl->surface = nullptr;  // Offscreen
        impl->framesInFlight = std::clamp(iFramesInFlight, 1u, kMaxFramesInFlight);
        impl->tier = iDevice.Dispatch([](const auto& dev) { return dev.GetCapabilities().tier; });
        impl->offscreenWidth = iWidth;
        impl->offscreenHeight = iHeight;

        impl->CreateSyncObjects();

        return FrameManager(std::move(impl));
    }

    // =========================================================================
    // Frame lifecycle
    // =========================================================================

    auto FrameManager::BeginFrame() -> core::Result<FrameContext> {
        assert(impl_ && "FrameManager used after move");

        // 1. CPU wait: ensure this slot's previous GPU work is complete
        impl_->WaitForSlot(impl_->frameIndex);

        // 1b. Drain deferred destructions for this slot (GPU is done with it)
        if (impl_->deferredDestructor) {
            impl_->deferredDestructor->DrainBin(impl_->frameIndex);
            impl_->deferredDestructor->SetCurrentBin(impl_->frameIndex);
        }

        // 1c. Reclaim staging/readback ring chunks from completed frames (specs/03-sync.md §6.4)
        // TODO(G5/G6): Uncomment when StagingRing/ReadbackRing are implemented.
        // if (impl_->stagingRing) {
        //     impl_->stagingRing->ReclaimCompleted(impl_->frameNumber - impl_->framesInFlight);
        // }
        // if (impl_->readbackRing) {
        //     impl_->readbackRing->ReclaimCompleted(impl_->frameNumber - impl_->framesInFlight);
        // }

        // 2. Advance frame number
        impl_->frameNumber++;

        // 3. Compute the timeline value this frame will signal upon completion
        uint64_t targetTimeline = 0;
        if (impl_->IsTimeline()) {
            targetTimeline = impl_->currentTimelineValue + 1;
        }

        // 4. Acquire swapchain image (windowed mode only)
        rhi::TextureHandle swapchainImage;
        uint32_t width = impl_->offscreenWidth;
        uint32_t height = impl_->offscreenHeight;

        if (impl_->surface) {
            auto extent = impl_->surface->GetExtent();
            width = extent.width;
            height = extent.height;

            if (width == 0 || height == 0) {
                // Minimized — skip this frame
                return std::unexpected(core::ErrorCode::InvalidState);
            }

            // Inject this slot's sync primitives into RenderSurface before acquire
            auto& slot = impl_->slots[impl_->frameIndex];
            impl_->surface->SetSubmitSyncInfo({
                .imageAvailable = slot.imageAvail,
                .renderFinished = slot.renderDone,
                .inFlightFence = slot.fence,
            });

            // Acquire: T1/T2 signal binary semaphore, T3/T4 implicit
            auto acquireResult = impl_->surface->AcquireNextImage();
            if (!acquireResult) {
                return std::unexpected(core::ErrorCode::SwapchainOutOfDate);
            }

            swapchainImage = impl_->surface->GetCurrentTexture();
        }

        // 5. Build FrameContext
        FrameContext ctx{
            .frameIndex = impl_->frameIndex,
            .frameNumber = impl_->frameNumber,
            .swapchainImage = swapchainImage,
            .width = width,
            .height = height,
            .graphicsTimelineTarget = targetTimeline,
            .transferWaitValue = impl_->transferSync.value,
            .computeWaitValue = impl_->computeSync.value,
        };

        return ctx;
    }

    auto FrameManager::EndFrame(std::span<const rhi::CommandBufferHandle> iCmdBuffers) -> core::Result<void> {
        assert(impl_ && "FrameManager used after move");

        auto& slot = impl_->slots[impl_->frameIndex];

        // 1. Build SubmitDesc with sync primitives
        std::vector<rhi::SemaphoreSubmitInfo> waits;
        std::vector<rhi::SemaphoreSubmitInfo> signals;

        if (impl_->IsTimeline()) {
            // T1: Signal timeline with next value
            uint64_t nextValue = impl_->currentTimelineValue + 1;

            // Wait on swapchain image availability (binary semaphore from AcquireNextImage)
            if (impl_->surface && slot.imageAvail.IsValid()) {
                waits.push_back({
                    .semaphore = slot.imageAvail,
                    .value = 0,
                    .stageMask = rhi::PipelineStage::ColorAttachmentOutput,
                });
            }

            // Wait on cross-queue sync points if present
            if (impl_->transferSync.semaphore.IsValid() && impl_->transferSync.value > 0) {
                waits.push_back({
                    .semaphore = impl_->transferSync.semaphore,
                    .value = impl_->transferSync.value,
                    .stageMask = rhi::PipelineStage::Transfer,
                });
            }
            if (impl_->computeSync.semaphore.IsValid() && impl_->computeSync.value > 0) {
                waits.push_back({
                    .semaphore = impl_->computeSync.semaphore,
                    .value = impl_->computeSync.value,
                    .stageMask = rhi::PipelineStage::ComputeShader,
                });
            }

            // Signal timeline + render-done binary (for Present)
            signals.push_back({
                .semaphore = impl_->graphicsTimeline,
                .value = nextValue,
                .stageMask = rhi::PipelineStage::AllCommands,
            });
            if (impl_->surface && slot.renderDone.IsValid()) {
                signals.push_back({
                    .semaphore = slot.renderDone,
                    .value = 0,
                    .stageMask = rhi::PipelineStage::AllCommands,
                });
            }

            slot.timelineValue = nextValue;
            impl_->currentTimelineValue = nextValue;

        } else if (impl_->IsFenceBased()) {
            // T2: Binary semaphores + fence
            if (impl_->surface && slot.imageAvail.IsValid()) {
                waits.push_back({
                    .semaphore = slot.imageAvail,
                    .value = 0,
                    .stageMask = rhi::PipelineStage::ColorAttachmentOutput,
                });
            }
            if (impl_->surface && slot.renderDone.IsValid()) {
                signals.push_back({
                    .semaphore = slot.renderDone,
                    .value = 0,
                    .stageMask = rhi::PipelineStage::AllCommands,
                });
            }
            slot.timelineValue = impl_->frameNumber;  // Track for WaitAll ordering
        }

        // 2. Submit to graphics queue
        rhi::SubmitDesc submitDesc{
            .commandBuffers = iCmdBuffers,
            .waitSemaphores = waits,
            .signalSemaphores = signals,
            .signalFence = impl_->IsFenceBased() ? slot.fence : rhi::FenceHandle{},
        };
        impl_->device.Dispatch([&](auto& dev) { dev.Submit(rhi::QueueType::Graphics, submitDesc); });

        // 4. Present (windowed mode only)
        if (impl_->surface) {
            auto presentResult = impl_->surface->Present();
            if (!presentResult) {
                return std::unexpected(core::ErrorCode::SwapchainOutOfDate);
            }
        }

        // 5. Reset cross-queue sync points for next frame
        impl_->transferSync.value = 0;
        impl_->computeSync.value = 0;

        // 6. Advance frame ring
        impl_->frameIndex = (impl_->frameIndex + 1) % impl_->framesInFlight;

        return {};
    }

    auto FrameManager::EndFrame(rhi::CommandBufferHandle iCmd) -> core::Result<void> {
        std::array cmds = {iCmd};
        return EndFrame(std::span<const rhi::CommandBufferHandle>{cmds});
    }

    auto FrameManager::EndFrameSplit(std::span<const SubmitBatch> iBatches) -> core::Result<void> {
        assert(impl_ && "FrameManager used after move");
        if (iBatches.empty()) {
            return EndFrame(std::span<const rhi::CommandBufferHandle>{});
        }

        auto& slot = impl_->slots[impl_->frameIndex];
        uint64_t lastTimelineValue = impl_->currentTimelineValue;

        for (size_t i = 0; i < iBatches.size(); ++i) {
            bool isFirst = (i == 0);
            bool isLast = (i == iBatches.size() - 1);
            auto& batch = iBatches[i];

            std::vector<rhi::SemaphoreSubmitInfo> waits;
            std::vector<rhi::SemaphoreSubmitInfo> signals;

            if (impl_->IsTimeline()) {
                // First batch: wait on swapchain acquire + cross-queue sync points
                if (isFirst) {
                    if (impl_->surface && slot.imageAvail.IsValid()) {
                        waits.push_back(
                            {.semaphore = slot.imageAvail,
                             .value = 0,
                             .stageMask = rhi::PipelineStage::ColorAttachmentOutput}
                        );
                    }
                    if (impl_->transferSync.semaphore.IsValid() && impl_->transferSync.value > 0) {
                        waits.push_back(
                            {.semaphore = impl_->transferSync.semaphore,
                             .value = impl_->transferSync.value,
                             .stageMask = rhi::PipelineStage::Transfer}
                        );
                    }
                }

                // Non-last batches that wait on compute: inject compute wait
                if (isLast && impl_->computeSync.semaphore.IsValid() && impl_->computeSync.value > 0) {
                    waits.push_back(
                        {.semaphore = impl_->computeSync.semaphore,
                         .value = impl_->computeSync.value,
                         .stageMask = rhi::PipelineStage::ComputeShader}
                    );
                }

                // Signal partial timeline if requested (or always on last)
                if (batch.signalPartialTimeline || isLast) {
                    uint64_t nextValue = ++lastTimelineValue;
                    signals.push_back(
                        {.semaphore = impl_->graphicsTimeline,
                         .value = nextValue,
                         .stageMask = rhi::PipelineStage::AllCommands}
                    );
                }

                // Last batch: also signal renderDone binary for present
                if (isLast && impl_->surface && slot.renderDone.IsValid()) {
                    signals.push_back(
                        {.semaphore = slot.renderDone, .value = 0, .stageMask = rhi::PipelineStage::AllCommands}
                    );
                }
            } else if (impl_->IsFenceBased()) {
                // T2: all batches merged into first+last (single submit effectively)
                if (isFirst && impl_->surface && slot.imageAvail.IsValid()) {
                    waits.push_back(
                        {.semaphore = slot.imageAvail,
                         .value = 0,
                         .stageMask = rhi::PipelineStage::ColorAttachmentOutput}
                    );
                }
                if (isLast && impl_->surface && slot.renderDone.IsValid()) {
                    signals.push_back(
                        {.semaphore = slot.renderDone, .value = 0, .stageMask = rhi::PipelineStage::AllCommands}
                    );
                }
            }

            rhi::SubmitDesc submitDesc{
                .commandBuffers = batch.commandBuffers,
                .waitSemaphores = waits,
                .signalSemaphores = signals,
                .signalFence = (isLast && impl_->IsFenceBased()) ? slot.fence : rhi::FenceHandle{},
            };
            impl_->device.Dispatch([&](auto& dev) { dev.Submit(rhi::QueueType::Graphics, submitDesc); });
        }

        // Update slot tracking
        slot.timelineValue = lastTimelineValue;
        impl_->currentTimelineValue = lastTimelineValue;
        if (impl_->IsFenceBased()) {
            slot.timelineValue = impl_->frameNumber;
        }

        // Present
        if (impl_->surface) {
            auto presentResult = impl_->surface->Present();
            if (!presentResult) {
                return std::unexpected(core::ErrorCode::SwapchainOutOfDate);
            }
        }

        impl_->transferSync.value = 0;
        impl_->computeSync.value = 0;
        impl_->frameIndex = (impl_->frameIndex + 1) % impl_->framesInFlight;
        return {};
    }

    // =========================================================================
    // Async compute / transfer integration
    // =========================================================================

    auto FrameManager::GetGraphicsSyncPoint() const noexcept -> TimelineSyncPoint {
        assert(impl_ && "FrameManager used after move");
        if (impl_->IsTimeline()) {
            return {.semaphore = impl_->graphicsTimeline, .value = impl_->currentTimelineValue};
        }
        return {};
    }

    auto FrameManager::SetComputeSyncPoint(TimelineSyncPoint iPoint) noexcept -> void {
        assert(impl_ && "FrameManager used after move");
        impl_->computeSync = iPoint;
    }

    auto FrameManager::SetTransferSyncPoint(TimelineSyncPoint iPoint) noexcept -> void {
        assert(impl_ && "FrameManager used after move");
        impl_->transferSync = iPoint;
    }

    // =========================================================================
    // Resource lifecycle hooks
    // =========================================================================

    auto FrameManager::SetStagingRing(resource::StagingRing* iRing) noexcept -> void {
        assert(impl_ && "FrameManager used after move");
        impl_->stagingRing = iRing;
    }

    auto FrameManager::SetReadbackRing(resource::ReadbackRing* iRing) noexcept -> void {
        assert(impl_ && "FrameManager used after move");
        impl_->readbackRing = iRing;
    }

    auto FrameManager::SetDeferredDestructor(DeferredDestructor* iDestructor) noexcept -> void {
        assert(impl_ && "FrameManager used after move");
        impl_->deferredDestructor = iDestructor;
    }

    // =========================================================================
    // Resize / reconfigure
    // =========================================================================

    auto FrameManager::Resize(uint32_t iWidth, uint32_t iHeight) -> core::Result<void> {
        assert(impl_ && "FrameManager used after move");

        if (!impl_->surface) {
            // Offscreen: just update dimensions
            impl_->offscreenWidth = iWidth;
            impl_->offscreenHeight = iHeight;
            return {};
        }

        if (iWidth == 0 || iHeight == 0) {
            return {};  // Minimized — no-op, will skip frames in BeginFrame
        }

        // Wait for all in-flight frames before recreating swapchain
        WaitAll();

        return impl_->surface->Resize(iWidth, iHeight);
    }

    auto FrameManager::Reconfigure(const rhi::RenderSurfaceConfig& iConfig) -> core::Result<void> {
        assert(impl_ && "FrameManager used after move");

        if (!impl_->surface) {
            return std::unexpected(core::ErrorCode::InvalidState);
        }

        // Wait for all in-flight frames before recreating swapchain
        WaitAll();

        return impl_->surface->Reconfigure(iConfig);
    }

    // =========================================================================
    // Queries
    // =========================================================================

    auto FrameManager::FrameIndex() const noexcept -> uint32_t {
        return impl_ ? impl_->frameIndex : 0;
    }

    auto FrameManager::FrameNumber() const noexcept -> uint64_t {
        return impl_ ? impl_->frameNumber : 0;
    }

    auto FrameManager::FramesInFlight() const noexcept -> uint32_t {
        return impl_ ? impl_->framesInFlight : 0;
    }

    auto FrameManager::IsWindowed() const noexcept -> bool {
        return impl_ && impl_->surface != nullptr;
    }

    auto FrameManager::GetSurface() const noexcept -> rhi::RenderSurface* {
        return impl_ ? impl_->surface : nullptr;
    }

    auto FrameManager::CurrentTimelineValue() const noexcept -> uint64_t {
        return impl_ ? impl_->currentTimelineValue : 0;
    }

    auto FrameManager::IsFrameComplete(uint64_t iFrameNumber) const noexcept -> bool {
        if (!impl_) {
            return true;
        }

        if (impl_->IsTimeline() && impl_->graphicsTimeline.IsValid()) {
            uint64_t gpuValue
                = impl_->device.Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(impl_->graphicsTimeline); });
            return gpuValue >= iFrameNumber;
        }

        // T2/T3/T4: conservative — assume complete if frameNumber is older than
        // oldest in-flight. This is correct because WaitForSlot ensures completion.
        return iFrameNumber <= impl_->frameNumber - impl_->framesInFlight;
    }

    // =========================================================================
    // WaitAll — drain all in-flight frames (per-surface, NOT device WaitIdle)
    // =========================================================================

    auto FrameManager::WaitAll() -> void {
        if (!impl_) {
            return;
        }

        if (impl_->IsTimeline() && impl_->graphicsTimeline.IsValid() && impl_->currentTimelineValue > 0) {
            // T1: Single wait on the highest submitted timeline value
            impl_->device.Dispatch([&](auto& dev) {
                dev.WaitSemaphore(impl_->graphicsTimeline, impl_->currentTimelineValue, UINT64_MAX);
            });
        } else if (impl_->IsFenceBased()) {
            // T2: Wait on all slot fences
            for (uint32_t i = 0; i < impl_->framesInFlight; ++i) {
                impl_->WaitForSlot(i);
            }
        } else if (impl_->IsImplicitSync()) {
            // T3/T4: A device tick / processEvents ensures GPU idle.
            // For correctness, do a lightweight device-level wait.
            // This is only called during surface detach / resize — not per-frame.
            impl_->device.Dispatch([](auto& dev) { dev.WaitIdle(); });
        }
    }

}  // namespace miki::frame
