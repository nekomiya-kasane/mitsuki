/** @file FrameManager.cpp
 *  @brief Timeline-first frame pacing — tier-adaptive GPU/CPU synchronization.
 *
 *  Architecture:
 *    - Ring of N frame slots (default 2, max 3).
 *    - Each slot tracks a monotonic timeline value.
 *    - BeginFrame: CPU waits until the oldest slot's timeline value is reached.
 *    - EndFrame:   Submits command buffers, signals next timeline value, presents.
 *
 *  Transfer dispatch (two entry points, mutually exclusive per frame):
 *
 *  FlushTransfers() — EAGER path (recommended, call after CPU memcpy)
 *  │  Only works on T1 + hasAsyncTransfer (dedicated DMA queue).
 *  │  Sets transfersFlushed = true; EndFrame skips re-dispatch.
 *  │  Transfer runs in parallel with cmd recording (~2ms overlap).
 *  │
 *  EndFrame / EndFrameSplit — LAZY path (fallback if FlushTransfers not called)
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
        rhi::FenceHandle fence;           // T2 only: per-slot binary fence
        rhi::SemaphoreHandle imageAvail;  // T1/T2: binary sem for swapchain acquire
        // NOTE: renderDone is NOT per-slot — it is per-swapchain-image.
        // See Impl::imageRenderDone[] and specs/03-sync.md present semaphore lifetime.
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

        // ── Helpers ──────────────────────────────────────────────

        [[nodiscard]] auto IsTimeline() const noexcept -> bool {
            return tier == rhi::CapabilityTier::Tier1_Vulkan || tier == rhi::CapabilityTier::Tier1_D3D12;
        }

        [[nodiscard]] auto IsFenceBased() const noexcept -> bool { return tier == rhi::CapabilityTier::Tier2_Compat; }

        [[nodiscard]] auto IsImplicitSync() const noexcept -> bool {
            return tier == rhi::CapabilityTier::Tier3_WebGPU || tier == rhi::CapabilityTier::Tier4_OpenGL;
        }

        // Pure wait — blocks until the GPU work submitted on this slot is complete.
        // Does NOT reset fences. This is safe to call from both BeginFrame and WaitAll.
        // On an already-signaled fence (e.g. after WaitAll), vkWaitForFences returns immediately.
        void WaitForSlot(uint32_t slotIdx) {
            auto& slot = slots[slotIdx];

            if (IsTimeline()) {
                // T1: CPU wait on timeline semaphore for this slot's value
                if (slot.timelineValue > 0) {
                    device.Dispatch([&](auto& dev) {
                        dev.WaitSemaphore(graphicsTimeline, slot.timelineValue, UINT64_MAX);
                    });
                }
            } else if (IsFenceBased()) {
                // T2: CPU wait on per-slot fence (does NOT reset — see ResetSlotFence)
                if (slot.fence.IsValid() && slot.timelineValue > 0) {
                    device.Dispatch([&](auto& dev) { dev.WaitFence(slot.fence, UINT64_MAX); });
                }
            }
            // T3/T4: implicit sync, no CPU wait needed
        }

        // Reset the slot's fence so it can be reused by the next vkQueueSubmit.
        // Must be called AFTER WaitForSlot confirms GPU completion.
        // Separated from WaitForSlot so that WaitAll (drain-only) doesn't reset fences
        // that have no subsequent submit — which would cause the next BeginFrame to
        // deadlock on an unsignaled fence.
        void ResetSlotFence(uint32_t slotIdx) {
            if (!IsFenceBased()) {
                return;
            }
            auto& slot = slots[slotIdx];
            // Reset is needed even on first frame (fence created signaled) because
            // vkQueueSubmit2 requires an unsignaled fence (VUID-vkQueueSubmit2-fence-04895).
            if (slot.fence.IsValid()) {
                device.Dispatch([&](auto& dev) { dev.ResetFence(slot.fence); });
            }
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
            if (IsTimeline()) {
                // T1: Reuse the device-global timeline semaphores (specs/03-sync.md §8.2)
                auto timelines = device.Dispatch([](const auto& dev) { return dev.GetQueueTimelines(); });
                graphicsTimeline = timelines.graphics;
                if (hasAsyncTransfer) {
                    transferTimeline = timelines.transfer;
                }
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
                    // T1/T2: Binary semaphore for swapchain acquire (per-slot is correct)
                    rhi::SemaphoreDesc binDesc{.type = rhi::SemaphoreType::Binary, .initialValue = 0};
                    auto availResult = device.Dispatch([&](auto& dev) { return dev.CreateSemaphore(binDesc); });
                    if (availResult) {
                        slots[i].imageAvail = *availResult;
                    }
                }
            }

            // Per-swapchain-image renderDone semaphores.
            // Present holds renderDone asynchronously until the image is re-acquired,
            // so we index by image (not slot) to avoid reuse conflicts.
            if (!IsImplicitSync() && surface) {
                swapchainImageCount = surface->GetSwapchainImageCount();
                rhi::SemaphoreDesc binDesc{.type = rhi::SemaphoreType::Binary, .initialValue = 0};
                for (uint32_t i = 0; i < swapchainImageCount; ++i) {
                    auto doneResult = device.Dispatch([&](auto& dev) { return dev.CreateSemaphore(binDesc); });
                    if (doneResult) {
                        imageRenderDone[i] = *doneResult;
                    }
                }
            }
        }

        // Rebuild per-image renderDone semaphores after swapchain recreation.
        // Caller must ensure all GPU work is complete (WaitAll) before calling.
        void RecreatePresentSemaphores() {
            if (IsImplicitSync() || !surface) {
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
            rhi::SemaphoreDesc binDesc{.type = rhi::SemaphoreType::Binary, .initialValue = 0};
            for (uint32_t i = 0; i < swapchainImageCount; ++i) {
                auto doneResult = device.Dispatch([&](auto& dev) { return dev.CreateSemaphore(binDesc); });
                if (doneResult) {
                    imageRenderDone[i] = *doneResult;
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
        impl->tier = caps.tier;
        impl->hasAsyncTransfer = caps.hasAsyncTransfer && caps.hasTimelineSemaphore;

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
        impl->tier = caps.tier;
        impl->hasAsyncTransfer = caps.hasAsyncTransfer && caps.hasTimelineSemaphore;
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

        // 0. Reset per-frame transfer state
        impl_->transfersFlushed = false;

        // 1. CPU wait: ensure this slot's previous GPU work is complete
        impl_->WaitForSlot(impl_->frameIndex);

        // 1.0a. Reset fence for reuse by the next submit (T2 only; T1/T3/T4 no-op).
        // Must happen after wait confirms GPU completion, before any new submit on this slot.
        impl_->ResetSlotFence(impl_->frameIndex);

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

            // Inject slot-based sync primitives before acquire.
            // renderFinished is set AFTER acquire because it is image-indexed (not slot-indexed).
            auto& slot = impl_->slots[impl_->frameIndex];
            impl_->surface->SetSubmitSyncInfo({
                .imageAvailable = slot.imageAvail,
                .renderFinished = {},  // Deferred — set below after image index is known
                .inFlightFence = slot.fence,
            });

            // Acquire: T1/T2 signal binary semaphore, T3/T4 implicit
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
                    .inFlightFence = slot.fence,
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
            .computeWaitValue = impl_->computeSync.value,
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

    auto FrameManager::EndFrame(std::span<const rhi::CommandBufferHandle> iCmdBuffers) -> core::Result<void> {
        assert(impl_ && "FrameManager used after move");

        auto& slot = impl_->slots[impl_->frameIndex];
        uint64_t fenceValue = impl_->IsTimeline() ? (impl_->currentTimelineValue + 1) : impl_->frameNumber;

        // ── 0. Transfer copy dispatch ────────────────────────────────
        // If FlushTransfers() was already called this frame, async transfers are already in-flight and transferSync is
        // set. We only need to handle: (a) async path not yet flushed, (b) fallback graphics path, (c) no copies.
        rhi::CommandListAcquisition transferAcq{};  // Only used in fallback path
        std::vector<rhi::CommandBufferHandle> mergedCmds;

        if (impl_->hasAsyncTransfer && !impl_->transfersFlushed && impl_->HasPendingTransfers()) {
            // T1 + dedicated transfer queue → separate submit with timeline signal
            impl_->SubmitTransferCopies(fenceValue);
            impl_->transfersFlushed = true;
        } else if (!impl_->transfersFlushed && impl_->HasPendingTransfers()) {
            // Fallback: record copies on a graphics cmd prepended to the user's batch (via CPA §19)
            auto acqResult = impl_->commandPoolAllocator.Acquire(impl_->frameIndex, rhi::QueueType::Graphics);
            if (acqResult) {
                transferAcq = acqResult->acquisition;
                transferAcq.listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
                impl_->RecordTransfersOnGraphics(transferAcq.listHandle, fenceValue);
                transferAcq.listHandle.Dispatch([](auto& cmd) { cmd.End(); });

                // Prepend transfer cmd before user's rendering commands
                mergedCmds.reserve(1 + iCmdBuffers.size());
                mergedCmds.push_back(transferAcq.bufferHandle);
                mergedCmds.insert(mergedCmds.end(), iCmdBuffers.begin(), iCmdBuffers.end());
            } else {
                // Acquisition failed — flush rings anyway (data not lost, copies deferred)
                if (impl_->stagingRing) {
                    impl_->stagingRing->FlushFrame(fenceValue);
                }
                if (impl_->readbackRing) {
                    impl_->readbackRing->FlushFrame(fenceValue);
                }
            }
        } else if (!impl_->transfersFlushed) {
            // No pending copies and not yet flushed — flush ring lifecycle bookkeeping only
            if (impl_->stagingRing) {
                impl_->stagingRing->FlushFrame(fenceValue);
            }
            if (impl_->readbackRing) {
                impl_->readbackRing->FlushFrame(fenceValue);
            }
        }
        // else: transfersFlushed == true → SubmitTransferCopies already called FlushFrame

        auto finalCmds = mergedCmds.empty() ? iCmdBuffers : std::span<const rhi::CommandBufferHandle>{mergedCmds};

        // ── 1. Build SubmitDesc with sync primitives ─────────────────
        std::vector<rhi::SemaphoreSubmitInfo> waits;
        std::vector<rhi::SemaphoreSubmitInfo> signals;

        if (impl_->IsTimeline()) {
            uint64_t nextValue = impl_->currentTimelineValue + 1;

            if (impl_->surface && slot.imageAvail.IsValid()) {
                waits.push_back({
                    .semaphore = slot.imageAvail,
                    .value = 0,
                    .stageMask = rhi::PipelineStage::ColorAttachmentOutput,
                });
            }

            // Wait on cross-queue sync points (transfer queue and/or async compute)
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

            signals.push_back({
                .semaphore = impl_->graphicsTimeline,
                .value = nextValue,
                .stageMask = rhi::PipelineStage::AllCommands,
            });
            if (impl_->surface) {
                uint32_t imgIdx = impl_->surface->GetCurrentImageIndex();
                if (imgIdx < impl_->swapchainImageCount && impl_->imageRenderDone[imgIdx].IsValid()) {
                    signals.push_back({
                        .semaphore = impl_->imageRenderDone[imgIdx],
                        .value = 0,
                        .stageMask = rhi::PipelineStage::AllCommands,
                    });
                }
            }

            slot.timelineValue = nextValue;
            impl_->currentTimelineValue = nextValue;

        } else if (impl_->IsFenceBased()) {
            if (impl_->surface && slot.imageAvail.IsValid()) {
                waits.push_back({
                    .semaphore = slot.imageAvail,
                    .value = 0,
                    .stageMask = rhi::PipelineStage::ColorAttachmentOutput,
                });
            }
            if (impl_->surface) {
                uint32_t imgIdx = impl_->surface->GetCurrentImageIndex();
                if (imgIdx < impl_->swapchainImageCount && impl_->imageRenderDone[imgIdx].IsValid()) {
                    signals.push_back({
                        .semaphore = impl_->imageRenderDone[imgIdx],
                        .value = 0,
                        .stageMask = rhi::PipelineStage::AllCommands,
                    });
                }
            }
            slot.timelineValue = impl_->frameNumber;
        }

        // ── 2. Submit to graphics queue ──────────────────────────────
        rhi::SubmitDesc submitDesc{
            .commandBuffers = finalCmds,
            .waitSemaphores = waits,
            .signalSemaphores = signals,
            .signalFence = impl_->IsFenceBased() ? slot.fence : rhi::FenceHandle{},
        };
        impl_->device.Dispatch([&](auto& dev) { dev.Submit(rhi::QueueType::Graphics, submitDesc); });

        // ── 3. Transfer cmd not individually released — CPA ResetSlot handles bulk reclamation (§19)

        // ── 4. Present ───────────────────────────────────────────────
        if (impl_->surface) {
            auto presentResult = impl_->surface->Present();
            if (!presentResult) {
                return std::unexpected(core::ErrorCode::SwapchainOutOfDate);
            }
        }

        // ── 5. Reset cross-queue sync points for next frame ──────────
        impl_->transferSync.value = 0;
        impl_->computeSync.value = 0;

        // ── 6. Advance frame ring ────────────────────────────────────
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
        uint64_t fenceValue = impl_->IsTimeline() ? (lastTimelineValue + 1) : impl_->frameNumber;

        // ── 0. Transfer copy dispatch (before first graphics batch) ──
        // Same logic as EndFrame: respect transfersFlushed from FlushTransfers().
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

                // Prepend to first batch
                auto& firstBatch = iBatches[0];
                firstBatchMerged.reserve(1 + firstBatch.commandBuffers.size());
                firstBatchMerged.push_back(transferAcq.bufferHandle);
                firstBatchMerged.insert(
                    firstBatchMerged.end(), firstBatch.commandBuffers.begin(), firstBatch.commandBuffers.end()
                );
            } else {
                if (impl_->stagingRing) {
                    impl_->stagingRing->FlushFrame(fenceValue);
                }
                if (impl_->readbackRing) {
                    impl_->readbackRing->FlushFrame(fenceValue);
                }
            }
        } else if (!impl_->transfersFlushed) {
            // No pending copies and not yet flushed — flush ring lifecycle bookkeeping only
            if (impl_->stagingRing) {
                impl_->stagingRing->FlushFrame(fenceValue);
            }
            if (impl_->readbackRing) {
                impl_->readbackRing->FlushFrame(fenceValue);
            }
        }
        // else: transfersFlushed == true → SubmitTransferCopies already called FlushFrame

        // ── Submit batches ───────────────────────────────────────────
        for (size_t i = 0; i < iBatches.size(); ++i) {
            bool isFirst = (i == 0);
            bool isLast = (i == iBatches.size() - 1);
            auto& batch = iBatches[i];

            // Use merged first batch if transfer cmd was prepended
            auto batchCmds = (isFirst && !firstBatchMerged.empty())
                                 ? std::span<const rhi::CommandBufferHandle>{firstBatchMerged}
                                 : batch.commandBuffers;

            std::vector<rhi::SemaphoreSubmitInfo> waits;
            std::vector<rhi::SemaphoreSubmitInfo> signals;

            if (impl_->IsTimeline()) {
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

                if (isLast && impl_->computeSync.semaphore.IsValid() && impl_->computeSync.value > 0) {
                    waits.push_back(
                        {.semaphore = impl_->computeSync.semaphore,
                         .value = impl_->computeSync.value,
                         .stageMask = rhi::PipelineStage::ComputeShader}
                    );
                }

                if (batch.signalPartialTimeline || isLast) {
                    uint64_t nextValue = ++lastTimelineValue;
                    signals.push_back(
                        {.semaphore = impl_->graphicsTimeline,
                         .value = nextValue,
                         .stageMask = rhi::PipelineStage::AllCommands}
                    );
                }

                if (isLast && impl_->surface) {
                    uint32_t imgIdx = impl_->surface->GetCurrentImageIndex();
                    if (imgIdx < impl_->swapchainImageCount && impl_->imageRenderDone[imgIdx].IsValid()) {
                        signals.push_back(
                            {.semaphore = impl_->imageRenderDone[imgIdx],
                             .value = 0,
                             .stageMask = rhi::PipelineStage::AllCommands}
                        );
                    }
                }
            } else if (impl_->IsFenceBased()) {
                if (isFirst && impl_->surface && slot.imageAvail.IsValid()) {
                    waits.push_back(
                        {.semaphore = slot.imageAvail,
                         .value = 0,
                         .stageMask = rhi::PipelineStage::ColorAttachmentOutput}
                    );
                }
                if (isLast && impl_->surface) {
                    uint32_t imgIdx = impl_->surface->GetCurrentImageIndex();
                    if (imgIdx < impl_->swapchainImageCount && impl_->imageRenderDone[imgIdx].IsValid()) {
                        signals.push_back(
                            {.semaphore = impl_->imageRenderDone[imgIdx],
                             .value = 0,
                             .stageMask = rhi::PipelineStage::AllCommands}
                        );
                    }
                }
            }

            rhi::SubmitDesc submitDesc{
                .commandBuffers = batchCmds,
                .waitSemaphores = waits,
                .signalSemaphores = signals,
                .signalFence = (isLast && impl_->IsFenceBased()) ? slot.fence : rhi::FenceHandle{},
            };
            impl_->device.Dispatch([&](auto& dev) { dev.Submit(rhi::QueueType::Graphics, submitDesc); });
        }

        // ── Cleanup — transfer cmd not individually released, CPA ResetSlot handles it (§19)

        slot.timelineValue = lastTimelineValue;
        impl_->currentTimelineValue = lastTimelineValue;
        if (impl_->IsFenceBased()) {
            slot.timelineValue = impl_->frameNumber;
        }

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

    auto FrameManager::FlushTransfers() -> void {
        assert(impl_ && "FrameManager used after move");
        if (impl_->transfersFlushed) {
            return;  // Already flushed this frame
        }
        if (!impl_->hasAsyncTransfer) {
            return;  // Fallback path must wait for EndFrame (needs graphics cmd)
        }
        if (!impl_->HasPendingTransfers()) {
            return;  // Nothing to flush
        }

        uint64_t fenceValue = impl_->IsTimeline() ? (impl_->currentTimelineValue + 1) : impl_->frameNumber;
        impl_->SubmitTransferCopies(fenceValue);
        impl_->transfersFlushed = true;
    }

    auto FrameManager::DrainPendingTransfers() -> uint32_t {
        assert(impl_ && "FrameManager used after move");
        if (!impl_->HasPendingTransfers()) {
            return 0;
        }

        uint64_t fenceValue = impl_->IsTimeline() ? (impl_->currentTimelineValue + 1) : impl_->frameNumber;

        if (impl_->hasAsyncTransfer) {
            // T1: submit on dedicated transfer queue
            auto count = impl_->SubmitTransferCopies(fenceValue);
            impl_->transfersFlushed = true;
            return count;
        }

        // Fallback: submit on graphics queue (T2/T3/T4)
        auto acqResult = impl_->commandPoolAllocator.Acquire(impl_->frameIndex, rhi::QueueType::Graphics);
        if (!acqResult) {
            return 0;
        }

        auto& acq = acqResult->acquisition;
        // RecordTransfersOnGraphics handles FlushFrame + RecordTransfers internally
        acq.listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
        uint32_t count = impl_->RecordTransfersOnGraphics(acq.listHandle, fenceValue);
        acq.listHandle.Dispatch([](auto& cmd) { cmd.End(); });

        // Submit with timeline signal (T1) or fence (T2) or plain submit (T3/T4)
        if (impl_->IsTimeline()) {
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
        } else {
            rhi::SubmitDesc desc{
                .commandBuffers = std::span(&acq.bufferHandle, 1),
                .waitSemaphores = {},
                .signalSemaphores = {},
                .signalFence = {},
            };
            impl_->device.Dispatch([&](auto& dev) { dev.Submit(rhi::QueueType::Graphics, desc); });
        }

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
            // Offscreen: just update dimensions
            impl_->offscreenWidth = iWidth;
            impl_->offscreenHeight = iHeight;
            return {};
        }

        if (iWidth == 0 || iHeight == 0) {
            return {};  // Minimized — no-op, will skip frames in BeginFrame
        }

        // Same-size short-circuit: skip expensive WaitAll + swapchain recreation when the surface is already at the
        // requested dimensions (e.g. live resize callback already handled this size during Win32 modal drag).
        auto currentExtent = impl_->surface->GetExtent();
        if (currentExtent.width == iWidth && currentExtent.height == iHeight) {
            return {};
        }

        // Wait for all in-flight frames before recreating swapchain
        WaitAll();

        auto result = impl_->surface->Resize(iWidth, iHeight);
        if (result) {
            // Swapchain image count may have changed — rebuild present semaphores
            impl_->RecreatePresentSemaphores();
        }
        return result;
    }

    auto FrameManager::Reconfigure(const rhi::RenderSurfaceConfig& iConfig) -> core::Result<void> {
        assert(impl_ && "FrameManager used after move");

        if (!impl_->surface) {
            return std::unexpected(core::ErrorCode::InvalidState);
        }

        // Wait for all in-flight frames before recreating swapchain
        WaitAll();

        auto result = impl_->surface->Reconfigure(iConfig);
        if (result) {
            // Swapchain image count may have changed — rebuild present semaphores
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
            // T2: Pure wait on all slot fences — no reset.
            // Fences remain signaled; the next BeginFrame's WaitForSlot returns immediately,
            // then ResetSlotFence prepares the fence for reuse.
            for (uint32_t i = 0; i < impl_->framesInFlight; ++i) {
                impl_->WaitForSlot(i);
            }
        } else if (impl_->IsImplicitSync()) {
            // T3/T4: No per-slot sync objects. A device-level wait ensures GPU idle.
            // Only called during resize / detach — not per-frame.
            impl_->device.Dispatch([](auto& dev) { dev.WaitIdle(); });
        }
    }

}  // namespace miki::frame
