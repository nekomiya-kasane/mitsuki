/** @file FrameManager.cpp
 *  @brief Unified-timeline frame pacing — all backends, single code path.
 *
 *  Architecture:
 *    - Ring of N frame slots (default 2, max 3).
 *    - Each slot tracks a monotonic timeline value.
 *    - BeginFrame: CPU waits until the oldest slot's timeline value is reached.
 *    - EndFrame:   Submits command batches, signals next timeline value, presents.
 *
 *  All backends use device-global timeline semaphores:
 *    T1 (Vulkan 1.4 / D3D12): Native timeline semaphore.
 *    T2 (Vulkan Compat):       Emulated via VK_KHR_timeline_semaphore.
 *    T3/T4 (WebGPU / OpenGL):  Emulated via CPU-side counter + backend sync.
 *  This eliminates all tier-specific branching in frame management.
 *
 *  Transfer dispatch (two entry points, mutually exclusive per frame):
 *
 *  FlushTransfers() — EAGER path (recommended, call after CPU memcpy)
 *  │  Only works when hasAsyncTransfer (dedicated DMA queue).
 *  │  Sets transfersFlushed = true; EndFrame skips re-dispatch.
 *  │  Transfer runs in parallel with cmd recording (~2ms overlap).
 *  │
 *  EndFrame — LAZY path (fallback if FlushTransfers not called)
 *  │
 *  ├─ hasAsyncTransfer && !transfersFlushed && pending copies?
 *  │   └─ SubmitTransferCopies() (same as FlushTransfers)
 *  │
 *  ├─ !hasAsyncTransfer && !transfersFlushed && pending copies?
 *  │   └─ AcquireCommandList(Graphics)
 *  │       ├─ RecordTransfersOnGraphics (FlushFrame + record)
 *  │       └─ Prepend to user's cmd batch in same submit
 *  │
 *  └─ transfersFlushed || no pending copies?
 *      └─ FlushFrame only (lifecycle bookkeeping)
 *
 *  See: specs/03-sync.md §4, specs/01-window-manager.md §5–§6.
 */

#include "miki/frame/FrameManager.h"

#include <algorithm>
#include <array>
#include <cassert>

#include "miki/debug/StructuredLogger.h"

#include "miki/core/EnumStrings.h"
#include "miki/frame/CommandPoolAllocator.h"
#include "miki/frame/DeferredDestructor.h"
#include "miki/resource/ReadbackRing.h"
#include "miki/resource/StagingRing.h"
#include "miki/rhi/GpuCapabilityProfile.h"
#include "miki/rhi/RenderSurface.h"
#include "miki/rhi/backend/AllBackends.h"
#include "miki/rhi/backend/AllCommandBuffers.h"

namespace miki::frame {

    // =========================================================================
    // Per-frame slot — tracks one in-flight frame's sync state
    // =========================================================================

    static constexpr uint32_t kMaxSwapchainImages = 8;

    struct FrameSlot {
        uint64_t timelineValue = 0;       // Timeline value this slot was submitted at
        rhi::SemaphoreHandle imageAvail;  // Binary sem for swapchain acquire
        // NOTE: renderDone is NOT per-slot — it is per-swapchain-image.
        // See Impl::imageRenderDone[] and specs/03-sync.md present semaphore lifetime.
    };

    // =========================================================================
    // FrameManager::Impl
    // =========================================================================

    struct FrameManager::Impl {
        rhi::DeviceHandle device;
        rhi::RenderSurface* surface = nullptr;  // nullptr = offscreen mode

        // Frame ring
        uint32_t framesInFlight = kDefaultFramesInFlight;
        uint32_t frameIndex = 0;   // [0, framesInFlight) — current slot
        uint64_t frameNumber = 0;  // Monotonic, never wraps
        std::array<FrameSlot, kMaxFramesInFlight> slots{};

        // Timeline semaphore (device-global, shared across all slots, all backends)
        rhi::SemaphoreHandle graphicsTimeline;
        uint64_t currentTimelineValue = 0;  // Last signaled value

        // Cross-queue sync points
        TimelineSyncPoint transferSync;
        std::vector<rhi::SemaphoreSubmitInfo> computeSyncPoints;  // Additive multi-compute waits (§4.4.4)

        // SyncScheduler integration (specs/03-sync.md §4.4)
        SyncScheduler* syncScheduler = nullptr;
        uint64_t lastPartialTimelineValue = 0;  // First partial signal from last EndFrame

        // Transfer queue integration (specs/03-sync.md §7.3)
        bool hasAsyncTransfer = false;
        rhi::SemaphoreHandle transferTimeline;  // Device-global transfer timeline (T1 only)
        uint64_t currentTransferTimelineValue = 0;
        bool transfersFlushed = false;  // Set by FlushTransfers(), reset in BeginFrame

        // Resource lifecycle hooks (optional, null = disabled)
        resource::StagingRing* stagingRing = nullptr;
        resource::ReadbackRing* readbackRing = nullptr;
        DeferredDestructor* deferredDestructor = nullptr;

        // Per-swapchain-image renderDone semaphores (specs/03-sync.md present semaphore lifetime).
        // Indexed by swapchain image index (NOT frame slot index) because vkQueuePresentKHR
        // holds the semaphore asynchronously until the image is re-acquired. Slot-indexed
        // renderDone causes reuse conflicts when consecutive frames target different images.
        std::array<rhi::SemaphoreHandle, kMaxSwapchainImages> imageRenderDone{};
        uint32_t swapchainImageCount = 0;

        // Offscreen dimensions (only used in offscreen mode)
        uint32_t offscreenWidth = 0;
        uint32_t offscreenHeight = 0;

        // Command pool allocator (§19)
        CommandPoolAllocator commandPoolAllocator;

        // ── Helpers ───────────────────────────────────────────────────

        // Unified wait — blocks until the GPU work submitted on this slot is complete.
        // All backends use timeline semaphore (native or emulated).
        void WaitForSlot(uint32_t slotIdx) {
            auto& slot = slots[slotIdx];
            if (graphicsTimeline.IsValid() && slot.timelineValue > 0) {
                MIKI_LOG_DEBUG(
                    ::miki::debug::LogCategory::Rhi,
                    "[Sync] WaitForSlot: slot=[{}] timelineValue=[{}] sem=[0x{:x}]",
                    slotIdx, slot.timelineValue, graphicsTimeline.value
                );
                device.Dispatch([&](auto& dev) {
                    dev.WaitSemaphore(graphicsTimeline, slot.timelineValue, UINT64_MAX);
                });
            } else {
                MIKI_LOG_DEBUG(
                    ::miki::debug::LogCategory::Rhi,
                    "[Sync] WaitForSlot: slot=[{}] SKIP (timelineValue=[{}])",
                    slotIdx, slot.timelineValue
                );
            }
        }

        [[nodiscard]] auto AllocateGraphicsSignal() -> uint64_t {
            assert(syncScheduler && "SyncScheduler is required");
            auto v = syncScheduler->AllocateSignal(rhi::QueueType::Graphics);
            MIKI_LOG_DEBUG(
                ::miki::debug::LogCategory::Rhi,
                "[Sync] AllocateGraphicsSignal: value=[{}] (prev=[{}])",
                v, currentTimelineValue
            );
            currentTimelineValue = v;
            return v;
        }

        [[nodiscard]] auto AllocateTransferSignal() -> uint64_t {
            assert(syncScheduler && "SyncScheduler is required");
            auto v = syncScheduler->AllocateSignal(rhi::QueueType::Transfer);
            currentTransferTimelineValue = v;
            return v;
        }

        void CommitGraphicsSubmit() {
            assert(syncScheduler && "SyncScheduler is required");
            syncScheduler->CommitSubmit(rhi::QueueType::Graphics);
        }

        void CommitTransferSubmit() {
            assert(syncScheduler && "SyncScheduler is required");
            syncScheduler->CommitSubmit(rhi::QueueType::Transfer);
        }

        [[nodiscard]] auto HasPendingTransfers() const -> bool {
            uint32_t count = 0;
            if (stagingRing) {
                count += stagingRing->GetPendingCopyCount();
            }
            if (readbackRing) {
                count += readbackRing->GetPendingCopyCount();
            }
            return count > 0;
        }

        /// Flush rings + record + submit copies on the dedicated transfer queue.
        /// Sets transferSync so graphics queue waits before vertex/index reads.
        /// Returns number of copy commands recorded.
        auto SubmitTransferCopies(uint64_t iFenceValue) -> uint32_t {
            // 1. Flush staging ring (FlushMappedRange for non-coherent)
            if (stagingRing) {
                stagingRing->FlushFrame(iFenceValue);
            }
            if (readbackRing) {
                readbackRing->FlushFrame(iFenceValue);
            }

            if (!HasPendingTransfers()) {
                return 0;
            }

            // 2. Acquire transfer command list via CPA (§19)
            auto acqResult = commandPoolAllocator.Acquire(frameIndex, rhi::QueueType::Transfer);
            if (!acqResult) {
                return 0;
            }

            auto& acq = acqResult->acquisition;
            uint32_t count = 0;

            // 3. Begin + record copies
            acq.listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
            if (stagingRing) {
                count += stagingRing->RecordTransfers(acq.listHandle);
            }
            if (readbackRing) {
                count += readbackRing->RecordTransfers(acq.listHandle);
            }
            acq.listHandle.Dispatch([](auto& cmd) { cmd.End(); });

            // 4. Submit to transfer queue, signal transfer timeline
            uint64_t nextTransferValue = ++currentTransferTimelineValue;
            MIKI_LOG_DEBUG(
                ::miki::debug::LogCategory::Rhi,
                "[Sync] SubmitTransferCopies: signal transferTimeline=[{}] sem=[0x{:x}]",
                nextTransferValue, transferTimeline.value
            );
            std::array<rhi::SemaphoreSubmitInfo, 1> transferSignals = {{
                {.semaphore = transferTimeline, .value = nextTransferValue, .stageMask = rhi::PipelineStage::Transfer},
            }};
            rhi::SubmitDesc transferSubmit{
                .commandBuffers = std::span(&acq.bufferHandle, 1),
                .waitSemaphores = {},
                .signalSemaphores = transferSignals,
                .signalFence = {},
            };
            device.Dispatch([&](auto& dev) { dev.Submit(rhi::QueueType::Transfer, transferSubmit); });

            // 5. Set transferSync so EndFrame's graphics submit waits on this
            transferSync = {.semaphore = transferTimeline, .value = nextTransferValue};

            // 6. Transfer cmd is not individually released — CPA ResetSlot handles bulk reclamation
            // at the next BeginFrame for this slot after GPU completion.

            return count;
        }

        /// Fallback: record copies on the graphics queue command buffer directly.
        /// Used when no dedicated transfer queue exists (T2/T3/T4).
        auto RecordTransfersOnGraphics(rhi::CommandListHandle& iGraphicsCmd, uint64_t iFenceValue) -> uint32_t {
            if (stagingRing) {
                stagingRing->FlushFrame(iFenceValue);
            }
            if (readbackRing) {
                readbackRing->FlushFrame(iFenceValue);
            }

            uint32_t count = 0;
            if (stagingRing) {
                count += stagingRing->RecordTransfers(iGraphicsCmd);
            }
            if (readbackRing) {
                count += readbackRing->RecordTransfers(iGraphicsCmd);
            }
            return count;
        }

        void CreateSyncObjects() {
            // All backends now provide device-global timeline semaphores (native or emulated)
            auto timelines = device.Dispatch([](const auto& dev) { return dev.GetQueueTimelines(); });
            graphicsTimeline = timelines.graphics;
            if (hasAsyncTransfer) {
                transferTimeline = timelines.transfer;
            }

            // Binary semaphores for swapchain acquire (per-slot)
            static constexpr const char* kImageAvailNames[] = {
                "imageAvail[0]", "imageAvail[1]", "imageAvail[2]", "imageAvail[3]"
            };
            rhi::SemaphoreDesc binDesc{.type = rhi::SemaphoreType::Binary, .initialValue = 0};
            for (uint32_t i = 0; i < framesInFlight; ++i) {
                auto availResult = device.Dispatch([&](auto& dev) { return dev.CreateSemaphore(binDesc); });
                if (availResult) {
                    slots[i].imageAvail = *availResult;
                    auto name = (i < 4) ? kImageAvailNames[i] : "imageAvail[N]";
                    device.Dispatch([&](auto& dev) { dev.SetObjectDebugName(slots[i].imageAvail, name); });
                }
            }

            // Per-swapchain-image renderDone semaphores.
            // Present holds renderDone asynchronously until the image is re-acquired,
            // so we index by image (not slot) to avoid reuse conflicts.
            static constexpr const char* kRenderDoneNames[] = {
                "renderDone[0]", "renderDone[1]", "renderDone[2]",
                "renderDone[3]", "renderDone[4]", "renderDone[5]",
                "renderDone[6]", "renderDone[7]",
            };
            if (surface) {
                swapchainImageCount = surface->GetSwapchainImageCount();
                for (uint32_t i = 0; i < swapchainImageCount; ++i) {
                    auto doneResult = device.Dispatch([&](auto& dev) { return dev.CreateSemaphore(binDesc); });
                    if (doneResult) {
                        imageRenderDone[i] = *doneResult;
                        auto name = (i < 8) ? kRenderDoneNames[i] : "renderDone[N]";
                        device.Dispatch([&](auto& dev) { dev.SetObjectDebugName(imageRenderDone[i], name); });
                    }
                }
            }
        }

        // Rebuild per-image renderDone semaphores after swapchain recreation.
        // Caller must ensure all GPU work is complete (WaitAll) before calling.
        void RecreatePresentSemaphores() {
            if (!surface) {
                return;
            }
            // Destroy old
            for (uint32_t i = 0; i < kMaxSwapchainImages; ++i) {
                if (imageRenderDone[i].IsValid()) {
                    device.Dispatch([&](auto& dev) { dev.DestroySemaphore(imageRenderDone[i]); });
                    imageRenderDone[i] = {};
                }
            }
            // Create new for potentially different image count
            swapchainImageCount = surface->GetSwapchainImageCount();
            static constexpr const char* kRenderDoneNames[] = {
                "renderDone[0]", "renderDone[1]", "renderDone[2]",
                "renderDone[3]", "renderDone[4]", "renderDone[5]",
                "renderDone[6]", "renderDone[7]",
            };
            rhi::SemaphoreDesc binDesc{.type = rhi::SemaphoreType::Binary, .initialValue = 0};
            for (uint32_t i = 0; i < swapchainImageCount; ++i) {
                auto doneResult = device.Dispatch([&](auto& dev) { return dev.CreateSemaphore(binDesc); });
                if (doneResult) {
                    imageRenderDone[i] = *doneResult;
                    auto name = (i < 8) ? kRenderDoneNames[i] : "renderDone[N]";
                    device.Dispatch([&](auto& dev) { dev.SetObjectDebugName(imageRenderDone[i], name); });
                }
            }
        }

        void DestroySyncObjects() {
            for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
                auto& slot = slots[i];
                if (slot.imageAvail.IsValid()) {
                    device.Dispatch([&](auto& dev) { dev.DestroySemaphore(slot.imageAvail); });
                    slot.imageAvail = {};
                }
            }
            // Destroy per-image renderDone semaphores
            for (uint32_t i = 0; i < kMaxSwapchainImages; ++i) {
                if (imageRenderDone[i].IsValid()) {
                    device.Dispatch([&](auto& dev) { dev.DestroySemaphore(imageRenderDone[i]); });
                    imageRenderDone[i] = {};
                }
            }
            swapchainImageCount = 0;
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
        auto caps = iDevice.Dispatch([](const auto& dev) { return dev.GetCapabilities(); });
        impl->hasAsyncTransfer = caps.hasAsyncTransfer;

        impl->CreateSyncObjects();

        // Initialize command pool allocator (§19)
        {
            CommandPoolAllocator::Desc cpaDesc{
                .device = iDevice,
                .framesInFlight = impl->framesInFlight,
                .hasAsyncCompute = caps.hasAsyncCompute,
                .hasAsyncTransfer = impl->hasAsyncTransfer,
                .initialArenaCapacity = 16,
                .enableHwmShrink = false,
            };
            auto cpaResult = CommandPoolAllocator::Create(cpaDesc);
            if (cpaResult) {
                impl->commandPoolAllocator = std::move(*cpaResult);
            }
        }

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
        auto caps = iDevice.Dispatch([](const auto& dev) { return dev.GetCapabilities(); });
        impl->hasAsyncTransfer = caps.hasAsyncTransfer;
        impl->offscreenWidth = iWidth;
        impl->offscreenHeight = iHeight;

        impl->CreateSyncObjects();

        // Initialize command pool allocator (§19)
        {
            CommandPoolAllocator::Desc cpaDesc{
                .device = iDevice,
                .framesInFlight = impl->framesInFlight,
                .hasAsyncCompute = caps.hasAsyncCompute,
                .hasAsyncTransfer = impl->hasAsyncTransfer,
                .initialArenaCapacity = 16,
                .enableHwmShrink = false,
            };
            auto cpaResult = CommandPoolAllocator::Create(cpaDesc);
            if (cpaResult) {
                impl->commandPoolAllocator = std::move(*cpaResult);
            }
        }

        return FrameManager(std::move(impl));
    }

    // =========================================================================
    // Frame lifecycle
    // =========================================================================

    auto FrameManager::BeginFrame() -> core::Result<FrameContext> {
        assert(impl_ && "FrameManager used after move");

        MIKI_LOG_DEBUG(
            ::miki::debug::LogCategory::Rhi,
            "[Sync] BeginFrame: frameIdx=[{}] currentTimeline=[{}] peekNext=[{}]",
            impl_->frameIndex, impl_->currentTimelineValue,
            impl_->syncScheduler ? impl_->syncScheduler->PeekNextSignal(rhi::QueueType::Graphics) : 0
        );

        // 0. Reset per-frame transfer state
        impl_->transfersFlushed = false;

        // 1. CPU wait: ensure this slot's previous GPU work is complete
        impl_->WaitForSlot(impl_->frameIndex);

        // 1a. Reset command pools for this slot (§19 — after GPU completion confirmed)
        impl_->commandPoolAllocator.ResetSlot(impl_->frameIndex);

        // 1b. Drain deferred destructions for this slot (GPU is done with it)
        if (impl_->deferredDestructor) {
            impl_->deferredDestructor->DrainBin(impl_->frameIndex);
            impl_->deferredDestructor->SetCurrentBin(impl_->frameIndex);
        }

        // 1c. Reclaim staging/readback ring chunks from completed frames (specs/03-sync.md §6.4)
        {
            auto completedValue = impl_->slots[impl_->frameIndex].timelineValue;
            if (impl_->stagingRing && completedValue > 0) {
                impl_->stagingRing->ReclaimCompleted(completedValue);
            }
            if (impl_->readbackRing && completedValue > 0) {
                impl_->readbackRing->ReclaimCompleted(completedValue);
            }
        }

        // 1d. Evict stale free chunks (Filament gc() pattern — reclaim idle VRAM)
        if (impl_->stagingRing) {
            impl_->stagingRing->EvictStaleChunks(impl_->frameNumber);
        }
        if (impl_->readbackRing) {
            impl_->readbackRing->EvictStaleChunks(impl_->frameNumber);
        }

        // 2. Advance frame number
        impl_->frameNumber++;

        // 3. Compute the timeline value this frame will signal upon completion.
        assert(impl_->syncScheduler && "SyncScheduler must be bound before BeginFrame");
        uint64_t targetTimeline = impl_->syncScheduler->PeekNextSignal(rhi::QueueType::Graphics);

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

            // Inject slot-based sync primitives before acquire.
            // renderFinished is set AFTER acquire because it is image-indexed (not slot-indexed).
            auto& slot = impl_->slots[impl_->frameIndex];
            impl_->surface->SetSubmitSyncInfo({
                .imageAvailable = slot.imageAvail,
                .renderFinished = {},  // Deferred — set below after image index is known
                .inFlightFence = {},
            });

            // Acquire: signals binary semaphore (all backends)
            auto acquireResult = impl_->surface->AcquireNextImage();
            if (!acquireResult) {
                return std::unexpected(core::ErrorCode::SwapchainOutOfDate);
            }

            // Now that we know the image index, inject the image-indexed renderDone semaphore.
            // vkQueuePresentKHR holds renderDone asynchronously until the image is re-acquired, so indexing by image
            // (not slot) prevents semaphore reuse conflicts.
            uint32_t imgIdx = impl_->surface->GetCurrentImageIndex();
            if (imgIdx < impl_->swapchainImageCount) {
                impl_->surface->SetSubmitSyncInfo({
                    .imageAvailable = slot.imageAvail,
                    .renderFinished = impl_->imageRenderDone[imgIdx],
                    .inFlightFence = {},
                });
            }

            swapchainImage = impl_->surface->GetCurrentTexture();
        }

        // Get pre-created texture view from swapchain
        rhi::TextureViewHandle swapchainImageView;
        if (impl_->surface) {
            swapchainImageView = impl_->surface->GetCurrentTextureView();
        }

        // 5. Build FrameContext
        FrameContext ctx{
            .frameIndex = impl_->frameIndex,
            .frameNumber = impl_->frameNumber,
            .swapchainImage = swapchainImage,
            .swapchainImageView = swapchainImageView,
            .width = width,
            .height = height,
            .graphicsTimelineTarget = targetTimeline,
            .transferWaitValue = impl_->transferSync.value,
            .computeWaitValue = 0,
        };

        return ctx;
    }

    auto FrameManager::AcquireCommandList(rhi::QueueType iQueue, uint32_t iThreadIndex)
        -> core::Result<rhi::CommandListAcquisition> {
        assert(impl_ && "FrameManager used after move");
        auto pooled = impl_->commandPoolAllocator.Acquire(impl_->frameIndex, iQueue, iThreadIndex);
        if (!pooled) {
            return std::unexpected(pooled.error());
        }
        return pooled->acquisition;
    }

    auto FrameManager::EndFrame(std::span<const SubmitBatch> iBatches) -> core::Result<void> {
        assert(impl_ && "FrameManager used after move");

        auto& slot = impl_->slots[impl_->frameIndex];
        uint64_t fenceValue = impl_->currentTimelineValue + 1;
        impl_->lastPartialTimelineValue = 0;  // Reset partial tracking for this frame

        MIKI_LOG_DEBUG(
            ::miki::debug::LogCategory::Rhi,
            "[Sync] EndFrame BEGIN: frameIdx=[{}] batchCount=[{}] currentTimeline=[{}] fenceValue=[{}]",
            impl_->frameIndex, iBatches.size(), impl_->currentTimelineValue, fenceValue
        );

        // ── 0. Transfer copy dispatch ────────────────────────────────
        rhi::CommandListAcquisition transferAcq{};
        std::vector<rhi::CommandBufferHandle> firstBatchMerged;

        if (impl_->hasAsyncTransfer && !impl_->transfersFlushed && impl_->HasPendingTransfers()) {
            impl_->SubmitTransferCopies(fenceValue);
            impl_->transfersFlushed = true;
        } else if (!impl_->transfersFlushed && impl_->HasPendingTransfers()) {
            auto acqResult = impl_->commandPoolAllocator.Acquire(impl_->frameIndex, rhi::QueueType::Graphics);
            if (acqResult) {
                transferAcq = acqResult->acquisition;
                transferAcq.listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
                impl_->RecordTransfersOnGraphics(transferAcq.listHandle, fenceValue);
                transferAcq.listHandle.Dispatch([](auto& cmd) { cmd.End(); });

                // Prepend to first batch (or create a single-element batch if iBatches is empty)
                if (!iBatches.empty()) {
                    auto& firstBatch = iBatches[0];
                    firstBatchMerged.reserve(1 + firstBatch.commandBuffers.size());
                    firstBatchMerged.push_back(transferAcq.bufferHandle);
                    firstBatchMerged.insert(
                        firstBatchMerged.end(), firstBatch.commandBuffers.begin(), firstBatch.commandBuffers.end()
                    );
                } else {
                    firstBatchMerged.push_back(transferAcq.bufferHandle);
                }
            } else {
                if (impl_->stagingRing) {
                    impl_->stagingRing->FlushFrame(fenceValue);
                }
                if (impl_->readbackRing) {
                    impl_->readbackRing->FlushFrame(fenceValue);
                }
            }
        } else if (!impl_->transfersFlushed) {
            if (impl_->stagingRing) {
                impl_->stagingRing->FlushFrame(fenceValue);
            }
            if (impl_->readbackRing) {
                impl_->readbackRing->FlushFrame(fenceValue);
            }
        }

        // ── 1. Submit batches (unified timeline path) ────────────────
        size_t batchCount = iBatches.size();

        // Handle edge case: no user batches but we have a transfer cmd
        if (batchCount == 0 && !firstBatchMerged.empty()) {
            batchCount = 1;  // Synthetic batch from transfer merge
        }

        for (size_t i = 0; i < batchCount; ++i) {
            bool isFirst = (i == 0);
            bool isLast = (i == batchCount - 1);

            // Determine command buffers for this batch
            std::span<const rhi::CommandBufferHandle> batchCmds;
            bool batchSignalPartial = true;

            if (isFirst && !firstBatchMerged.empty()) {
                batchCmds = firstBatchMerged;
                batchSignalPartial = (i < iBatches.size()) ? iBatches[i].signalPartialTimeline : true;
            } else if (i < iBatches.size()) {
                batchCmds = iBatches[i].commandBuffers;
                batchSignalPartial = iBatches[i].signalPartialTimeline;
            }

            std::vector<rhi::SemaphoreSubmitInfo> waits;
            std::vector<rhi::SemaphoreSubmitInfo> signals;

            // First batch: wait on swapchain acquire + transfer completion
            if (isFirst) {
                if (impl_->surface && slot.imageAvail.IsValid()) {
                    waits.push_back({
                        .semaphore = slot.imageAvail,
                        .value = 0,
                        .stageMask = rhi::PipelineStage::ColorAttachmentOutput,
                    });
                }
                if (impl_->transferSync.semaphore.IsValid() && impl_->transferSync.value > 0) {
                    waits.push_back({
                        .semaphore = impl_->transferSync.semaphore,
                        .value = impl_->transferSync.value,
                        .stageMask = rhi::PipelineStage::Transfer,
                    });
                }
            }

            // Last batch: wait on accumulated compute sync points
            if (isLast) {
                waits.insert(waits.end(), impl_->computeSyncPoints.begin(), impl_->computeSyncPoints.end());
            }

            // Signal timeline for this batch
            if (batchSignalPartial || isLast) {
                uint64_t nextValue = impl_->AllocateGraphicsSignal();
                signals.push_back({
                    .semaphore = impl_->graphicsTimeline,
                    .value = nextValue,
                    .stageMask = rhi::PipelineStage::AllCommands,
                });
                // Track first partial signal for GetGraphicsSyncPoint (§4.4.3)
                if (!isLast && impl_->lastPartialTimelineValue == 0) {
                    impl_->lastPartialTimelineValue = nextValue;
                }
            }

            // Last batch: signal renderDone for present
            if (isLast && impl_->surface) {
                uint32_t imgIdx = impl_->surface->GetCurrentImageIndex();
                if (imgIdx < impl_->swapchainImageCount && impl_->imageRenderDone[imgIdx].IsValid()) {
                    signals.push_back({
                        .semaphore = impl_->imageRenderDone[imgIdx],
                        .value = 0,
                        .stageMask = rhi::PipelineStage::AllCommands,
                    });
                }
            }

            rhi::SubmitDesc submitDesc{
                .commandBuffers = batchCmds,
                .waitSemaphores = waits,
                .signalSemaphores = signals,
                .signalFence = {},
            };
            MIKI_LOG_DEBUG(
                ::miki::debug::LogCategory::Rhi,
                "[Sync] EndFrame batch [{}/{}]: cmds=[{}] waits=[{}] signals=[{}]",
                i + 1, batchCount, batchCmds.size(), waits.size(), signals.size()
            );
            for (auto& w : waits) {
                MIKI_LOG_DEBUG(
                    ::miki::debug::LogCategory::Rhi,
                    "[Sync]   wait: sem=[0x{:x}] value=[{}]", w.semaphore.value, w.value
                );
            }
            for (auto& s : signals) {
                MIKI_LOG_DEBUG(
                    ::miki::debug::LogCategory::Rhi,
                    "[Sync]   signal: sem=[0x{:x}] value=[{}]", s.semaphore.value, s.value
                );
            }
            impl_->device.Dispatch([&](auto& dev) { dev.Submit(rhi::QueueType::Graphics, submitDesc); });
        }

        // TODO (Nekomiya) review this, it is strange that we have to do a separate submit for the case of no batches.
        // Can we merge it with the first batch if it exists, or is there some other way to avoid this? Maybe we can
        // just always do this submit and merge the batch submits into it if they exist?
        //
        // ── 1b. Sync-only submit when no batches were issued ─────────
        // When all GPU work is submitted externally (e.g. RenderGraph's BatchSubmitter),
        // EndFrame receives empty iBatches. We must still:
        //   (a) advance the graphics timeline (BeginFrame waits on it), and
        //   (b) signal imageRenderDone so Present's binary semaphore wait is satisfied.
        // A zero-command-buffer vkQueueSubmit2 is valid per spec and costs ~0 GPU time.
        if (batchCount == 0 && impl_->surface) {
            std::vector<rhi::SemaphoreSubmitInfo> waits;
            std::vector<rhi::SemaphoreSubmitInfo> signals;

            // Wait on swapchain acquire
            if (slot.imageAvail.IsValid()) {
                waits.push_back({
                    .semaphore = slot.imageAvail,
                    .value = 0,
                    .stageMask = rhi::PipelineStage::ColorAttachmentOutput,
                });
            }

            // Wait on transfer completion if applicable
            if (impl_->transferSync.semaphore.IsValid() && impl_->transferSync.value > 0) {
                waits.push_back({
                    .semaphore = impl_->transferSync.semaphore,
                    .value = impl_->transferSync.value,
                    .stageMask = rhi::PipelineStage::Transfer,
                });
            }

            // Wait on accumulated compute sync points
            waits.insert(waits.end(), impl_->computeSyncPoints.begin(), impl_->computeSyncPoints.end());

            // Signal graphics timeline
            uint64_t nextValue = impl_->AllocateGraphicsSignal();
            signals.push_back({
                .semaphore = impl_->graphicsTimeline,
                .value = nextValue,
                .stageMask = rhi::PipelineStage::AllCommands,
            });

            // Signal renderDone for present
            uint32_t imgIdx = impl_->surface->GetCurrentImageIndex();
            if (imgIdx < impl_->swapchainImageCount && impl_->imageRenderDone[imgIdx].IsValid()) {
                signals.push_back({
                    .semaphore = impl_->imageRenderDone[imgIdx],
                    .value = 0,
                    .stageMask = rhi::PipelineStage::AllCommands,
                });
            }

            rhi::SubmitDesc syncSubmit{
                .commandBuffers = {},
                .waitSemaphores = waits,
                .signalSemaphores = signals,
                .signalFence = {},
            };
            MIKI_LOG_DEBUG(
                ::miki::debug::LogCategory::Rhi,
                "[Sync] EndFrame SYNC-ONLY submit: waits=[{}] signals=[{}]",
                waits.size(), signals.size()
            );
            for (auto& w : waits) {
                MIKI_LOG_DEBUG(
                    ::miki::debug::LogCategory::Rhi,
                    "[Sync]   wait: sem=[0x{:x}] value=[{}]", w.semaphore.value, w.value
                );
            }
            for (auto& s : signals) {
                MIKI_LOG_DEBUG(
                    ::miki::debug::LogCategory::Rhi,
                    "[Sync]   signal: sem=[0x{:x}] value=[{}]", s.semaphore.value, s.value
                );
            }
            impl_->device.Dispatch([&](auto& dev) { dev.Submit(rhi::QueueType::Graphics, syncSubmit); });
        }

        // ── 2. Update slot timeline value ────────────────────────────
        slot.timelineValue = impl_->currentTimelineValue;
        MIKI_LOG_DEBUG(
            ::miki::debug::LogCategory::Rhi,
            "[Sync] EndFrame: slot[{}].timelineValue = [{}]",
            impl_->frameIndex, slot.timelineValue
        );

        // ── 3. Present ───────────────────────────────────────────────
        if (impl_->surface) {
            MIKI_LOG_DEBUG(
                ::miki::debug::LogCategory::Rhi,
                "[Sync] EndFrame: Present (renderFinished sem=[0x{:x}])",
                impl_->imageRenderDone[impl_->surface->GetCurrentImageIndex()].value
            );
            auto presentResult = impl_->surface->Present();
            if (!presentResult) {
                return std::unexpected(core::ErrorCode::SwapchainOutOfDate);
            }
        }

        // ── 4. Commit to SyncScheduler + reset cross-queue sync points
        MIKI_LOG_DEBUG(
            ::miki::debug::LogCategory::Rhi,
            "[Sync] EndFrame: CommitGraphicsSubmit (currentTimeline=[{}])",
            impl_->currentTimelineValue
        );
        impl_->CommitGraphicsSubmit();
        impl_->transferSync.value = 0;
        impl_->computeSyncPoints.clear();

        // ── 5. Advance frame ring ────────────────────────────────────
        impl_->frameIndex = (impl_->frameIndex + 1) % impl_->framesInFlight;
        return {};
    }

    // =========================================================================
    // Async compute / transfer integration
    // =========================================================================

    auto FrameManager::GetGraphicsSyncPoint() const noexcept -> TimelineSyncPoint {
        assert(impl_ && "FrameManager used after move");
        // After EndFrame with partial signal: return geometry-done value (§4.4.3)
        uint64_t value
            = impl_->lastPartialTimelineValue > 0 ? impl_->lastPartialTimelineValue : impl_->currentTimelineValue;
        return {.semaphore = impl_->graphicsTimeline, .value = value};
    }

    auto FrameManager::AddComputeSyncPoint(TimelineSyncPoint iPoint, rhi::PipelineStage iStage) noexcept -> void {
        assert(impl_ && "FrameManager used after move");
        if (iPoint.semaphore.IsValid() && iPoint.value > 0) {
            impl_->computeSyncPoints.push_back(
                {.semaphore = iPoint.semaphore, .value = iPoint.value, .stageMask = iStage}
            );
        }
    }

    auto FrameManager::ClearComputeSyncPoints() noexcept -> void {
        assert(impl_ && "FrameManager used after move");
        impl_->computeSyncPoints.clear();
    }

    auto FrameManager::SetTransferSyncPoint(TimelineSyncPoint iPoint) noexcept -> void {
        assert(impl_ && "FrameManager used after move");
        impl_->transferSync = iPoint;
    }

    auto FrameManager::SetSyncScheduler(SyncScheduler* iScheduler) noexcept -> void {
        assert(impl_ && "FrameManager used after move");
        if (!iScheduler) {
            MIKI_LOG_ERROR(
                debug::LogCategory::Render,
                "[FrameManager] SetSyncScheduler called with null — SyncScheduler is mandatory"
            );
            assert(false && "SyncScheduler must not be null");
            return;
        }
        impl_->syncScheduler = iScheduler;
    }

    auto FrameManager::GetLastPartialTimelineValue() const noexcept -> uint64_t {
        return impl_ ? impl_->lastPartialTimelineValue : 0;
    }

    auto FrameManager::FlushTransfers() -> void {
        assert(impl_ && "FrameManager used after move");
        if (impl_->transfersFlushed) {
            return;
        }
        if (!impl_->hasAsyncTransfer) {
            return;
        }
        if (!impl_->HasPendingTransfers()) {
            return;
        }

        uint64_t fenceValue = impl_->AllocateTransferSignal();
        impl_->SubmitTransferCopies(fenceValue);
        impl_->CommitTransferSubmit();
        impl_->transfersFlushed = true;
    }

    auto FrameManager::DrainPendingTransfers() -> uint32_t {
        assert(impl_ && "FrameManager used after move");
        if (!impl_->HasPendingTransfers()) {
            return 0;
        }

        uint64_t fenceValue = impl_->currentTimelineValue + 1;

        if (impl_->hasAsyncTransfer) {
            auto count = impl_->SubmitTransferCopies(fenceValue);
            impl_->transfersFlushed = true;
            return count;
        }

        // Fallback: submit on graphics queue
        auto acqResult = impl_->commandPoolAllocator.Acquire(impl_->frameIndex, rhi::QueueType::Graphics);
        if (!acqResult) {
            return 0;
        }

        auto& acq = acqResult->acquisition;
        acq.listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
        uint32_t count = impl_->RecordTransfersOnGraphics(acq.listHandle, fenceValue);
        acq.listHandle.Dispatch([](auto& cmd) { cmd.End(); });

        uint64_t nextValue = ++impl_->currentTimelineValue;
        std::array<rhi::SemaphoreSubmitInfo, 1> signals = {{
            {.semaphore = impl_->graphicsTimeline, .value = nextValue, .stageMask = rhi::PipelineStage::Transfer},
        }};
        rhi::SubmitDesc desc{
            .commandBuffers = std::span(&acq.bufferHandle, 1),
            .waitSemaphores = {},
            .signalSemaphores = signals,
            .signalFence = {},
        };
        impl_->device.Dispatch([&](auto& dev) { dev.Submit(rhi::QueueType::Graphics, desc); });

        impl_->transfersFlushed = true;
        return count;
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
            impl_->offscreenWidth = iWidth;
            impl_->offscreenHeight = iHeight;
            return {};
        }

        if (iWidth == 0 || iHeight == 0) {
            return {};  // Minimized — no-op
        }

        auto currentExtent = impl_->surface->GetExtent();
        if (currentExtent.width == iWidth && currentExtent.height == iHeight) {
            return {};
        }

        WaitAll();
        auto result = impl_->surface->Resize(iWidth, iHeight);
        if (result) {
            impl_->RecreatePresentSemaphores();
        }
        return result;
    }

    auto FrameManager::Reconfigure(const rhi::RenderSurfaceConfig& iConfig) -> core::Result<void> {
        assert(impl_ && "FrameManager used after move");

        if (!impl_->surface) {
            return std::unexpected(core::ErrorCode::InvalidState);
        }

        WaitAll();
        auto result = impl_->surface->Reconfigure(iConfig);
        if (result) {
            impl_->RecreatePresentSemaphores();
        }
        return result;
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

        if (impl_->graphicsTimeline.IsValid()) {
            uint64_t gpuValue
                = impl_->device.Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(impl_->graphicsTimeline); });
            return gpuValue >= iFrameNumber;
        }

        return iFrameNumber <= impl_->frameNumber - impl_->framesInFlight;
    }

    // =========================================================================
    // WaitAll — drain all in-flight frames (per-surface, NOT device WaitIdle)
    // =========================================================================

    auto FrameManager::WaitAll() -> void {
        if (!impl_) {
            return;
        }

        if (impl_->graphicsTimeline.IsValid() && impl_->currentTimelineValue > 0) {
            // Single wait on the highest submitted timeline value (all backends)
            impl_->device.Dispatch([&](auto& dev) {
                dev.WaitSemaphore(impl_->graphicsTimeline, impl_->currentTimelineValue, UINT64_MAX);
            });
        }
    }

}  // namespace miki::frame
