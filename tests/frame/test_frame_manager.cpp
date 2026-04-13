// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// FrameManager unit tests — specs/03-sync.md §17
// Tests verify FrameManager behavior across all real backends using RhiTestFixture.
// Uses CreateOffscreen for headless CI (no window required).
// BDD style: GIVEN / WHEN / THEN (see §17.2–§17.14).

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "RhiTestFixture.h"
#include "miki/frame/DeferredDestructor.h"
#include "miki/frame/FrameContext.h"
#include "miki/frame/FrameManager.h"
#include "miki/frame/SyncScheduler.h"
#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Sync.h"
#include "miki/rhi/backend/AllCommandBuffers.h"

using namespace miki;
using namespace miki::frame;
using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// Test fixture for FrameManager — parameterized across all backends
// ============================================================================

class FrameManagerTest : public RhiTest {
   protected:
    void SetUp() override {
        RhiTest::SetUp();
        if (testing::Test::IsSkipped()) {
            return;
        }

        // FrameManager requires sync primitives; Mock backend doesn't support them
        if (GetParam() == BackendType::Mock) {
            GTEST_SKIP() << "FrameManager requires real sync primitives; Mock backend skipped";
        }
    }

    // Helper: create offscreen FrameManager with device-owned SyncScheduler
    [[nodiscard]] auto MakeOffscreen(uint32_t framesInFlight = 2) -> core::Result<FrameManager> {
        auto result = FrameManager::CreateOffscreen(Dev(), 1920, 1080, framesInFlight);
        if (result.has_value()) {
            result->SetSyncScheduler(&Dev().GetSyncScheduler());
        }
        return result;
    }

    // Helper: EndFrame with a single command buffer (no convenience overload).
    static auto EndFrameSingle(FrameManager& fm, rhi::CommandBufferHandle cmd) -> core::Result<void> {
        FrameManager::SubmitBatch batch{.commandBuffers = std::span(&cmd, 1)};
        return fm.EndFrame(std::span<const FrameManager::SubmitBatch>{&batch, 1});
    }

    // Helper: run N complete frames (BeginFrame + EndFrame with a real cmd buffer)
    void RunFrames(FrameManager& fm, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            auto ctx = fm.BeginFrame();
            ASSERT_TRUE(ctx.has_value()) << "BeginFrame failed at frame " << i;

            auto acq = fm.AcquireCommandList(QueueType::Graphics);
            ASSERT_TRUE(acq.has_value()) << "AcquireCommandList failed at frame " << i;

            acq->listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
            acq->listHandle.Dispatch([](auto& cmd) { cmd.End(); });

            auto endResult = EndFrameSingle(fm, acq->bufferHandle);
            ASSERT_TRUE(endResult.has_value()) << "EndFrame failed at frame " << i;
        }
    }
};

// ============================================================================
// §17.2 FM-LC — Lifecycle Tests
// ============================================================================

// FM-LC-01: GIVEN invalid DeviceHandle WHEN Create() THEN InvalidArgument
TEST_P(FrameManagerTest, LC01_InvalidDeviceReturnsError) {
    DeviceHandle invalid{};
    auto result = FrameManager::CreateOffscreen(invalid, 1920, 1080, 2);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), core::ErrorCode::InvalidArgument);
}

// FM-LC-02: GIVEN valid device WHEN CreateOffscreen() THEN correct initial state
TEST_P(FrameManagerTest, LC02_CreateOffscreenInitialState) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    EXPECT_EQ(fm.FramesInFlight(), 2u);
    EXPECT_EQ(fm.FrameIndex(), 0u);
    EXPECT_EQ(fm.FrameNumber(), 0u);
    EXPECT_FALSE(fm.IsWindowed());
}

// FM-LC-03: GIVEN valid device WHEN CreateOffscreen THEN IsWindowed == false
TEST_P(FrameManagerTest, LC03_OffscreenIsNotWindowed) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->IsWindowed());
    EXPECT_EQ(result->GetSurface(), nullptr);
}

// FM-LC-04: GIVEN framesInFlight == 0 WHEN Create() THEN clamped to 1
TEST_P(FrameManagerTest, LC04_FramesInFlightClampedMin) {
    auto result = MakeOffscreen(0);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->FramesInFlight(), 1u);
}

// FM-LC-05: GIVEN framesInFlight == 5 WHEN Create() THEN clamped to kMaxFramesInFlight
TEST_P(FrameManagerTest, LC05_FramesInFlightClampedMax) {
    auto result = MakeOffscreen(5);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->FramesInFlight(), FrameManager::kMaxFramesInFlight);
}

// FM-LC-06: GIVEN valid FM WHEN move-constructed THEN source is empty, dest is usable
TEST_P(FrameManagerTest, LC06_MoveConstructor) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());

    FrameManager b = std::move(*result);
    EXPECT_EQ(b.FramesInFlight(), 2u);

    // Source is moved-from
    EXPECT_EQ(result->FrameIndex(), 0u);
    EXPECT_EQ(result->FrameNumber(), 0u);
    EXPECT_EQ(result->FramesInFlight(), 0u);
}

// FM-LC-07: GIVEN valid FM WHEN destructor runs THEN no crash (WaitAll + cleanup)
TEST_P(FrameManagerTest, LC07_DestructorAfterFrames) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    RunFrames(*result, 3);
    // Destructor runs here — should call WaitAll() + DestroySyncObjects() cleanly
}

// ============================================================================
// §17.3 FM-FL — Frame Lifecycle
// ============================================================================

// FM-FL-01: GIVEN offscreen FM WHEN first BeginFrame THEN frameIndex==0, frameNumber==1
TEST_P(FrameManagerTest, FL01_FirstBeginFrame) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());
    EXPECT_EQ(ctx->frameIndex, 0u);
    EXPECT_EQ(ctx->frameNumber, 1u);
    EXPECT_EQ(ctx->width, 1920u);
    EXPECT_EQ(ctx->height, 1080u);
    EXPECT_FALSE(ctx->swapchainImage.IsValid());  // Offscreen: no swapchain

    // Cleanup: EndFrame to avoid dangling frame
    auto acq = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    acq->listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
    acq->listHandle.Dispatch([](auto& cmd) { cmd.End(); });
    EXPECT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
}

// FM-FL-02: GIVEN FM after BeginFrame WHEN EndFrame THEN frameIndex advances
TEST_P(FrameManagerTest, FL02_EndFrameAdvancesIndex) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    RunFrames(fm, 1);
    EXPECT_EQ(fm.FrameIndex(), 1u);
    EXPECT_EQ(fm.FrameNumber(), 1u);
}

// FM-FL-03: GIVEN FM with 2 FIF, after 2 frames WHEN BeginFrame for frame 3 THEN wraps to index 0
TEST_P(FrameManagerTest, FL03_FrameRingWraps) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    RunFrames(fm, 2);
    EXPECT_EQ(fm.FrameIndex(), 0u);  // 2 % 2 == 0

    // Frame 3: should wait on slot 0's previous work then return index 0
    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());
    EXPECT_EQ(ctx->frameIndex, 0u);
    EXPECT_EQ(ctx->frameNumber, 3u);

    // Cleanup
    auto acq = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    acq->listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
    acq->listHandle.Dispatch([](auto& cmd) { cmd.End(); });
    EXPECT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
}

// FM-FL-07: GIVEN FM WHEN EndFrame(singleCmd) overload THEN succeeds
TEST_P(FrameManagerTest, FL07_SingleCmdEndFrame) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());

    auto acq = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    acq->listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
    acq->listHandle.Dispatch([](auto& cmd) { cmd.End(); });

    // Single-cmd overload
    auto endResult = EndFrameSingle(fm, acq->bufferHandle);
    EXPECT_TRUE(endResult.has_value());
}

// ============================================================================
// §17.4 FM-TL — Timeline Semaphore (T1)
// ============================================================================

// FM-TL-02: GIVEN T1 FM, 3 frames WHEN CurrentTimelineValue THEN returns 3
TEST_P(FrameManagerTest, TL02_TimelineValueTracksFrames) {
    if (!IsTier1()) {
        GTEST_SKIP() << "Timeline tests require Tier1";
    }

    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    EXPECT_EQ(fm.CurrentTimelineValue(), 0u);
    RunFrames(fm, 3);
    EXPECT_EQ(fm.CurrentTimelineValue(), 3u);
}

// FM-TL-03: GIVEN T1 FM after 5 frames WHEN GetGraphicsSyncPoint THEN returns {sem, 5}
TEST_P(FrameManagerTest, TL03_GetGraphicsSyncPoint) {
    if (!IsTier1()) {
        GTEST_SKIP() << "Timeline tests require Tier1";
    }

    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    RunFrames(fm, 5);
    auto sp = fm.GetGraphicsSyncPoint();
    EXPECT_TRUE(sp.semaphore.IsValid());
    EXPECT_EQ(sp.value, 5u);
}

// ============================================================================
// §17.6 FM-IS — Implicit Sync (T3/T4)
// ============================================================================

// FM-IS-01: GIVEN T3/T4 FM WHEN CreateOffscreen THEN succeeds
TEST_P(FrameManagerTest, IS01_ImplicitSyncCreation) {
    auto tier = Caps().tier;
    if (tier != CapabilityTier::Tier3_WebGPU && tier != CapabilityTier::Tier4_OpenGL) {
        GTEST_SKIP() << "Implicit sync test only for T3/T4";
    }

    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->FramesInFlight(), 2u);
}

// ============================================================================
// §17.7 FM-TX — Transfer Dispatch
// ============================================================================

// FM-TX-08: GIVEN FM with no rings WHEN EndFrame THEN no crash
TEST_P(FrameManagerTest, TX08_NoRingsNoCrash) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    // No staging/readback ring set — should work fine
    RunFrames(fm, 3);
}

// FM-TX-03 partial: GIVEN FM WHEN FlushTransfers with no rings THEN no crash
TEST_P(FrameManagerTest, TX03_FlushTransfersNoRingsNoop) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());

    // FlushTransfers with no rings — no-op, no crash
    fm.FlushTransfers();

    auto acq = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    acq->listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
    acq->listHandle.Dispatch([](auto& cmd) { cmd.End(); });
    EXPECT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
}

// FM-TX-04: GIVEN FM WHEN FlushTransfers called twice THEN no crash (idempotent)
TEST_P(FrameManagerTest, TX04_FlushTransfersIdempotent) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());

    fm.FlushTransfers();
    fm.FlushTransfers();  // Second call — should be no-op

    auto acq = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    acq->listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
    acq->listHandle.Dispatch([](auto& cmd) { cmd.End(); });
    EXPECT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
}

// ============================================================================
// §17.8 FM-SS — Split Submit (EndFrame)
// ============================================================================

// FM-SS-04: GIVEN FM WHEN EndFrame({}) THEN delegates to EndFrame({})
TEST_P(FrameManagerTest, SS04_EmptyBatchesDelegatesToEndFrame) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());

    auto endResult = fm.EndFrame(std::span<const FrameManager::SubmitBatch>{});
    EXPECT_TRUE(endResult.has_value());
}

// FM-SS-01 / FM-SS-02: GIVEN T1 FM WHEN EndFrame with 2 batches THEN both submitted
TEST_P(FrameManagerTest, SS01_SplitSubmitTwoBatches) {
    if (!IsTier1()) {
        GTEST_SKIP() << "Split submit timeline test requires Tier1";
    }

    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());
    uint64_t baseTimeline = fm.CurrentTimelineValue();

    // Create 2 command buffers for 2 batches
    auto acq1 = fm.AcquireCommandList(QueueType::Graphics);
    auto acq2 = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq1.has_value());
    ASSERT_TRUE(acq2.has_value());

    acq1->listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
    acq1->listHandle.Dispatch([](auto& cmd) { cmd.End(); });
    acq2->listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
    acq2->listHandle.Dispatch([](auto& cmd) { cmd.End(); });

    std::array<CommandBufferHandle, 1> cmds1 = {acq1->bufferHandle};
    std::array<CommandBufferHandle, 1> cmds2 = {acq2->bufferHandle};

    std::array<FrameManager::SubmitBatch, 2> batches = {{
        {.commandBuffers = cmds1, .signalPartialTimeline = true},
        {.commandBuffers = cmds2, .signalPartialTimeline = true},
    }};

    auto endResult = fm.EndFrame(batches);
    EXPECT_TRUE(endResult.has_value());

    // Timeline should advance by 2 (one per batch)
    EXPECT_EQ(fm.CurrentTimelineValue(), baseTimeline + 2);
}

// ============================================================================
// §17.9 FM-XQ — Cross-Queue Sync Points
// ============================================================================

// FM-XQ-01: GIVEN T1 FM with compute sync point WHEN EndFrame THEN waits on it
TEST_P(FrameManagerTest, XQ01_ComputeSyncPointConsumed) {
    if (!IsTier1()) {
        GTEST_SKIP() << "Cross-queue sync requires Tier1";
    }

    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    // Get timeline semaphores from device
    auto timelines = Dev().Dispatch([](const auto& dev) { return dev.GetQueueTimelines(); });

    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());

    // Simulate compute completion at value 42
    if (timelines.compute.IsValid()) {
        Dev().Dispatch([&](auto& dev) { dev.SignalSemaphore(timelines.compute, 42); });
        fm.AddComputeSyncPoint({.semaphore = timelines.compute, .value = 42}, PipelineStage::ComputeShader);
    }

    auto acq = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    acq->listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
    acq->listHandle.Dispatch([](auto& cmd) { cmd.End(); });

    // EndFrame should internally wait on compute sync
    auto endResult = EndFrameSingle(fm, acq->bufferHandle);
    EXPECT_TRUE(endResult.has_value());
}

// FM-XQ-04: GIVEN T1 FM, no sync points WHEN EndFrame THEN succeeds
TEST_P(FrameManagerTest, XQ04_NoSyncPointsStillWorks) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    RunFrames(*result, 1);
}

// ============================================================================
// §17.10 FM-RL — Resource Lifecycle Hooks
// ============================================================================

// FM-RL-03: GIVEN FM with no hooks WHEN cycle THEN no crash
TEST_P(FrameManagerTest, RL03_NoHooksNoCrash) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    RunFrames(*result, 5);
}

// FM-RL-01: GIVEN FM with DeferredDestructor WHEN BeginFrame THEN DrainBin called
TEST_P(FrameManagerTest, RL01_DeferredDestructorIntegration) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto dd = DeferredDestructor::Create(Dev(), 2);
    fm.SetDeferredDestructor(&dd);

    // Run a few frames — DD should be drained at BeginFrame without crash
    RunFrames(fm, 4);

    fm.SetDeferredDestructor(nullptr);
}

// ============================================================================
// §17.11 FM-RS — Resize / Reconfigure
// ============================================================================

// FM-RS-02: GIVEN offscreen FM WHEN Resize(0,0) THEN no-op success
TEST_P(FrameManagerTest, RS02_ResizeZeroNoOp) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());

    auto r = result->Resize(0, 0);
    // Offscreen mode: just updates dimensions
    EXPECT_TRUE(r.has_value());
}

// FM-RS-03: GIVEN offscreen FM WHEN Resize(3840, 2160) THEN dimensions updated
TEST_P(FrameManagerTest, RS03_OffscreenResize) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto r = fm.Resize(3840, 2160);
    EXPECT_TRUE(r.has_value());

    // Verify new dimensions in next frame
    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());
    EXPECT_EQ(ctx->width, 3840u);
    EXPECT_EQ(ctx->height, 2160u);

    auto acq = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    acq->listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
    acq->listHandle.Dispatch([](auto& cmd) { cmd.End(); });
    EXPECT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
}

// FM-RS-04: GIVEN offscreen FM WHEN Reconfigure THEN InvalidState
TEST_P(FrameManagerTest, RS04_OffscreenReconfigureFails) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());

    RenderSurfaceConfig cfg{};
    auto r = result->Reconfigure(cfg);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), core::ErrorCode::InvalidState);
}

// ============================================================================
// §17.12 FM-QR — Queries
// ============================================================================

// FM-QR-01: GIVEN FM after 5 frames WHEN FrameNumber THEN returns 5
TEST_P(FrameManagerTest, QR01_FrameNumberAfterFrames) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    RunFrames(*result, 5);
    EXPECT_EQ(result->FrameNumber(), 5u);
}

// FM-QR-02: GIVEN FM with 2 FIF after 3 frames WHEN FrameIndex THEN returns 1
TEST_P(FrameManagerTest, QR02_FrameIndexModulo) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    RunFrames(*result, 3);
    EXPECT_EQ(result->FrameIndex(), 1u);  // 3 % 2 == 1
}

// FM-QR-03: GIVEN T1 FM after 10 frames WHEN IsFrameComplete THEN correct
TEST_P(FrameManagerTest, QR03_IsFrameComplete) {
    if (!IsTier1()) {
        GTEST_SKIP() << "IsFrameComplete timeline test requires Tier1";
    }

    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    RunFrames(fm, 10);
    // After WaitAll in destructor or explicit wait, GPU should be at timeline 10
    fm.WaitAll();
    EXPECT_TRUE(fm.IsFrameComplete(10));
}

// FM-QR-04: GIVEN moved-from FM WHEN queries THEN all return 0
TEST_P(FrameManagerTest, QR04_MovedFromQueries) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());

    FrameManager b = std::move(*result);
    EXPECT_EQ(result->FrameIndex(), 0u);
    EXPECT_EQ(result->FrameNumber(), 0u);
    EXPECT_EQ(result->FramesInFlight(), 0u);
    EXPECT_EQ(result->CurrentTimelineValue(), 0u);
    EXPECT_FALSE(result->IsWindowed());
}

// FM-QR-05: GIVEN FM never submitted WHEN CurrentTimelineValue THEN 0
TEST_P(FrameManagerTest, QR05_InitialTimelineValueZero) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->CurrentTimelineValue(), 0u);
}

// ============================================================================
// §17.13 FM-WA — WaitAll
// ============================================================================

// FM-WA-01: GIVEN FM after frames WHEN WaitAll THEN no hang
TEST_P(FrameManagerTest, WA01_WaitAllAfterFrames) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    RunFrames(*result, 5);
    result->WaitAll();  // Should not hang
}

// FM-WA-04: GIVEN moved-from FM WHEN WaitAll THEN no crash
TEST_P(FrameManagerTest, WA04_WaitAllMovedFrom) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());

    FrameManager b = std::move(*result);
    result->WaitAll();  // No-op, no crash
}

// ============================================================================
// §17.14 FM-MF — Multi-Frame Stress / Integration
// ============================================================================

// FM-MF-01: GIVEN T1 offscreen FM with 2 FIF WHEN 100 frames THEN correct state
TEST_P(FrameManagerTest, MF01_100FrameStress) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    RunFrames(fm, 100);
    EXPECT_EQ(fm.FrameNumber(), 100u);
    EXPECT_EQ(fm.FrameIndex(), 0u);  // 100 % 2 == 0

    if (IsTier1()) {
        EXPECT_EQ(fm.CurrentTimelineValue(), 100u);
    }
}

// FM-MF-02: GIVEN FM with 3 FIF WHEN 10 frames THEN ring wraps correctly
TEST_P(FrameManagerTest, MF02_ThreeFrameRing) {
    auto result = MakeOffscreen(3);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    for (uint32_t i = 0; i < 10; ++i) {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        EXPECT_EQ(ctx->frameIndex, i % 3) << "Wrong frameIndex at frame " << (i + 1);

        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
        acq->listHandle.Dispatch([](auto& cmd) { cmd.End(); });
        EXPECT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
    }

    EXPECT_EQ(fm.FrameNumber(), 10u);
    EXPECT_EQ(fm.FrameIndex(), 1u);  // 10 % 3 == 1
}

// FM-MF-03 partial: GIVEN FM WHEN FlushTransfers alternating with EndFrame THEN no crash
TEST_P(FrameManagerTest, MF03_AlternatingFlushTransfers) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    for (uint32_t i = 0; i < 10; ++i) {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());

        if (i % 2 == 1) {
            fm.FlushTransfers();  // Odd frames: eager path
        }

        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
        acq->listHandle.Dispatch([](auto& cmd) { cmd.End(); });
        EXPECT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
    }

    EXPECT_EQ(fm.FrameNumber(), 10u);
}

// FM-MF-04: GIVEN T1 FM WHEN AddComputeSyncPoint every frame THEN no crash
TEST_P(FrameManagerTest, MF04_ComputeSyncEveryFrame) {
    if (!IsTier1()) {
        GTEST_SKIP() << "Compute sync test requires Tier1";
    }

    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto timelines = Dev().Dispatch([](const auto& dev) { return dev.GetQueueTimelines(); });
    if (!timelines.compute.IsValid()) {
        GTEST_SKIP() << "No compute timeline available";
    }

    for (uint32_t i = 0; i < 10; ++i) {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());

        uint64_t computeVal = i + 1;
        Dev().Dispatch([&](auto& dev) { dev.SignalSemaphore(timelines.compute, computeVal); });
        fm.AddComputeSyncPoint({.semaphore = timelines.compute, .value = computeVal}, PipelineStage::ComputeShader);

        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
        acq->listHandle.Dispatch([](auto& cmd) { cmd.End(); });
        EXPECT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
    }
}

// ============================================================================
// §17.15 SS — SyncScheduler Tests (pure CPU, no GPU required)
// ============================================================================

// Helper: build fake QueueTimelines with synthetic semaphore handles
static auto MakeFakeTimelines() -> QueueTimelines {
    QueueTimelines t;
    t.graphics = SemaphoreHandle::Pack(1, 100, 0, 0);
    t.compute = SemaphoreHandle::Pack(1, 101, 0, 0);
    t.transfer = SemaphoreHandle::Pack(1, 102, 0, 0);
    return t;
}

// SS-01: GIVEN freshly Init'd SyncScheduler WHEN AllocateSignal THEN returns 1, 2, ...
TEST(SyncSchedulerTest, SS01_AllocateSignalMonotonic) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());

    EXPECT_EQ(sched.AllocateSignal(QueueType::Graphics), 1u);
    EXPECT_EQ(sched.AllocateSignal(QueueType::Graphics), 2u);
    EXPECT_EQ(sched.AllocateSignal(QueueType::Compute), 1u);
}

// SS-02: GIVEN SyncScheduler WHEN AddDependency THEN GetPendingWaits has entry
TEST(SyncSchedulerTest, SS02_AddDependencyCreatesWait) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());

    sched.AddDependency(QueueType::Compute, QueueType::Graphics, 5, PipelineStage::ComputeShader);
    auto waits = sched.GetPendingWaits(QueueType::Compute);
    ASSERT_EQ(waits.size(), 1u);
    EXPECT_EQ(waits[0].value, 5u);
    EXPECT_EQ(waits[0].stageMask, PipelineStage::ComputeShader);
}

// SS-03: GIVEN SyncScheduler with pending waits WHEN CommitSubmit THEN cleared
TEST(SyncSchedulerTest, SS03_CommitSubmitClearsWaits) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());

    sched.AddDependency(QueueType::Compute, QueueType::Graphics, 5, PipelineStage::ComputeShader);
    EXPECT_EQ(sched.GetPendingWaits(QueueType::Compute).size(), 1u);

    sched.CommitSubmit(QueueType::Compute);
    EXPECT_EQ(sched.GetPendingWaits(QueueType::Compute).size(), 0u);
}

// SS-06: GIVEN SyncScheduler after Reset WHEN GetCurrentValue THEN 0
TEST(SyncSchedulerTest, SS06_ResetClearsState) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());

    (void)sched.AllocateSignal(QueueType::Graphics);
    (void)sched.AllocateSignal(QueueType::Graphics);
    sched.Reset();
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), 0u);
}

// SS-07: GIVEN SyncScheduler WHEN two AddDependency on same queue THEN 2 pending waits
TEST(SyncSchedulerTest, SS07_MultipleDependencies) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());

    sched.AddDependency(QueueType::Graphics, QueueType::Transfer, 3, PipelineStage::Transfer);
    sched.AddDependency(QueueType::Graphics, QueueType::Compute, 7, PipelineStage::FragmentShader);
    auto waits = sched.GetPendingWaits(QueueType::Graphics);
    EXPECT_EQ(waits.size(), 2u);
}

// SS-10: GIVEN 3-queue chain T->C->G WHEN dependencies added THEN correct pending waits
TEST(SyncSchedulerTest, SS10_ThreeQueueChain) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());

    sched.AddDependency(QueueType::Compute, QueueType::Transfer, 1, PipelineStage::ComputeShader);
    sched.AddDependency(QueueType::Graphics, QueueType::Compute, 1, PipelineStage::VertexInput);

    EXPECT_EQ(sched.GetPendingWaits(QueueType::Compute).size(), 1u);
    EXPECT_EQ(sched.GetPendingWaits(QueueType::Graphics).size(), 1u);
    EXPECT_EQ(sched.GetPendingWaits(QueueType::Transfer).size(), 0u);
}

// ============================================================================
// §17.16 DD — DeferredDestructor Tests (uses MockDevice — no real GPU needed)
// ============================================================================

// Helper: create a MockDevice-backed DeviceHandle
static auto MakeMockDeviceHandle() -> std::pair<std::unique_ptr<MockDevice>, DeviceHandle> {
    auto mock = std::make_unique<MockDevice>();
    auto r = mock->Init();
    (void)r;
    auto h = DeviceHandle(mock.get(), BackendType::Mock);
    return {std::move(mock), h};
}

// DD-01: GIVEN DD WHEN Destroy(buffer) THEN PendingCount == 1
TEST(DeferredDestructorTest, DD01_DestroyEnqueues) {
    auto [mock, dev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(dev, 2);

    // Use a synthetic handle — MockDevice::DestroyBuffer is a no-op
    auto fakeBuffer = BufferHandle::Pack(1, 1, 0, 0);
    dd.Destroy(fakeBuffer);
    EXPECT_EQ(dd.PendingCount(), 1u);

    dd.DrainAll();
    EXPECT_EQ(dd.PendingCount(), 0u);
}

// DD-03: GIVEN DD WHEN Destroy in bin 0 then SetCurrentBin(1) then Destroy THEN isolated
TEST(DeferredDestructorTest, DD03_BinIsolation) {
    auto [mock, dev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(dev, 2);

    auto buf1 = BufferHandle::Pack(1, 10, 0, 0);
    auto buf2 = BufferHandle::Pack(1, 11, 0, 0);

    dd.Destroy(buf1);  // Goes to bin 0
    dd.SetCurrentBin(1);
    dd.Destroy(buf2);  // Goes to bin 1

    EXPECT_EQ(dd.PendingCount(), 2u);

    dd.DrainBin(0);
    EXPECT_EQ(dd.PendingCount(), 1u);  // Only buf1 drained

    dd.DrainBin(1);
    EXPECT_EQ(dd.PendingCount(), 0u);  // buf2 drained
}

// DD-04: GIVEN DD with resources WHEN DrainAll THEN all destroyed
TEST(DeferredDestructorTest, DD04_DrainAll) {
    auto [mock, dev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(dev, 2);

    for (uint32_t i = 0; i < 5; ++i) {
        dd.Destroy(BufferHandle::Pack(1, i + 20, 0, 0));
    }

    EXPECT_EQ(dd.PendingCount(), 5u);
    dd.DrainAll();
    EXPECT_EQ(dd.PendingCount(), 0u);
}

// DD-05: GIVEN DD with resources in bin 0 WHEN DrainBin(1) THEN nothing drained
TEST(DeferredDestructorTest, DD05_DrainWrongBinNoEffect) {
    auto [mock, dev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(dev, 2);

    dd.Destroy(BufferHandle::Pack(1, 30, 0, 0));
    EXPECT_EQ(dd.PendingCount(), 1u);

    dd.DrainBin(1);                    // Wrong bin
    EXPECT_EQ(dd.PendingCount(), 1u);  // Still there

    dd.DrainAll();  // Cleanup
}

// DD-07: GIVEN binCount == 0 WHEN Create THEN clamped to 1
TEST(DeferredDestructorTest, DD07_BinCountClampedMin) {
    auto [mock, dev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(dev, 0);
    // Should not crash — bin count clamped to 1
    dd.Destroy(BufferHandle::Pack(1, 40, 0, 0));
    EXPECT_EQ(dd.PendingCount(), 1u);
    dd.DrainAll();
}

// DD-08: GIVEN binCount == 5 WHEN Create THEN clamped to kMaxBins(3)
TEST(DeferredDestructorTest, DD08_BinCountClampedMax) {
    auto [mock, dev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(dev, 5);
    // Should not crash — bin count clamped to 3
    dd.SetCurrentBin(2);  // Max valid bin
    dd.Destroy(BufferHandle::Pack(1, 50, 0, 0));
    EXPECT_EQ(dd.PendingCount(), 1u);
    dd.DrainAll();
}

// DD-06: GIVEN DD WHEN Destroy for multiple handle types THEN all dispatched correctly
TEST(DeferredDestructorTest, DD06_MultipleHandleTypes) {
    auto [mock, dev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(dev, 2);

    dd.Destroy(BufferHandle::Pack(1, 1, 0, 0));
    dd.Destroy(TextureHandle::Pack(1, 2, 0, 0));
    dd.Destroy(SamplerHandle::Pack(1, 3, 0, 0));
    dd.Destroy(PipelineHandle::Pack(1, 4, 0, 0));
    dd.Destroy(FenceHandle::Pack(1, 5, 0, 0));
    dd.Destroy(SemaphoreHandle::Pack(1, 6, 0, 0));

    EXPECT_EQ(dd.PendingCount(), 6u);
    dd.DrainAll();
    EXPECT_EQ(dd.PendingCount(), 0u);
}

// ============================================================================
// §18 Composite Tests — Behavior combinations across subsystems
// ============================================================================

// ── §18.2 CT-SS — SyncScheduler Multi-Frame Pipeline (pure CPU) ─────────

TEST(CompositeSSTest, CTSS01_MultiFramePipeline) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());
    for (uint32_t f = 0; f < 5; ++f) {
        auto tV = sched.AllocateSignal(QueueType::Transfer);
        sched.AddDependency(QueueType::Compute, QueueType::Transfer, tV, PipelineStage::ComputeShader);
        auto cV = sched.AllocateSignal(QueueType::Compute);
        sched.AddDependency(QueueType::Graphics, QueueType::Compute, cV, PipelineStage::VertexInput);
        (void)sched.AllocateSignal(QueueType::Graphics);
        EXPECT_FALSE(sched.DetectDeadlock()) << "frame " << f;
        sched.CommitSubmit(QueueType::Transfer);
        sched.CommitSubmit(QueueType::Compute);
        sched.CommitSubmit(QueueType::Graphics);
    }
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), 5u);
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Compute), 5u);
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Transfer), 5u);
    EXPECT_TRUE(sched.GetPendingWaits(QueueType::Graphics).empty());
}

TEST(CompositeSSTest, CTSS02_ResetAfterPipeline) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());
    for (uint32_t f = 0; f < 5; ++f) {
        (void)sched.AllocateSignal(QueueType::Graphics);
        sched.CommitSubmit(QueueType::Graphics);
    }
    sched.Reset();
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), 0u);
    EXPECT_EQ(sched.AllocateSignal(QueueType::Graphics), 1u);
}

TEST(CompositeSSTest, CTSS03_DiamondDependency) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());
    auto tV = sched.AllocateSignal(QueueType::Transfer);
    sched.AddDependency(QueueType::Compute, QueueType::Transfer, tV, PipelineStage::ComputeShader);
    sched.AddDependency(QueueType::Graphics, QueueType::Transfer, tV, PipelineStage::Transfer);
    auto cV = sched.AllocateSignal(QueueType::Compute);
    sched.AddDependency(QueueType::Graphics, QueueType::Compute, cV, PipelineStage::FragmentShader);
    EXPECT_EQ(sched.GetPendingWaits(QueueType::Graphics).size(), 2u);
    EXPECT_EQ(sched.GetPendingWaits(QueueType::Compute).size(), 1u);
    EXPECT_FALSE(sched.DetectDeadlock());
}

TEST(CompositeSSTest, CTSS04_CycleDetectionAndBreaking) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());
    // Build valid chain T→C→G
    auto tV = sched.AllocateSignal(QueueType::Transfer);
    sched.AddDependency(QueueType::Compute, QueueType::Transfer, tV, PipelineStage::ComputeShader);
    auto cV = sched.AllocateSignal(QueueType::Compute);
    sched.AddDependency(QueueType::Graphics, QueueType::Compute, cV, PipelineStage::VertexInput);
    EXPECT_FALSE(sched.DetectDeadlock());
    // Close the cycle: G→T
    auto gV = sched.AllocateSignal(QueueType::Graphics);
    sched.AddDependency(QueueType::Transfer, QueueType::Graphics, gV, PipelineStage::Transfer);
    EXPECT_TRUE(sched.DetectDeadlock());
    // Break cycle by committing Transfer (advances currentValue past waited value tV)
    sched.CommitSubmit(QueueType::Transfer);
    // Now Transfer's currentValue >= tV, so T→C edge is resolved
    // But G→T still has gV which is > Transfer's currentValue... Let's commit all
    sched.CommitSubmit(QueueType::Compute);
    sched.CommitSubmit(QueueType::Graphics);
    // After commit, pending waits are cleared → no cycle
    EXPECT_FALSE(sched.DetectDeadlock());
}

TEST(CompositeSSTest, CTSS05_AllocateWithoutCommit) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());
    for (uint32_t i = 0; i < 10; ++i) {
        (void)sched.AllocateSignal(QueueType::Graphics);
    }
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), 0u);
    EXPECT_EQ(sched.GetSignalValue(QueueType::Graphics), 10u);
    sched.CommitSubmit(QueueType::Graphics);
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), 10u);
}

// ── §18.9 CT-DG — Diagnostics Under Complex State (pure CPU) ────────────

TEST(CompositeDGTest, CTDG01_DOTExportAfterCommit) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());
    for (uint32_t i = 0; i < 3; ++i) {
        auto tV = sched.AllocateSignal(QueueType::Transfer);
        sched.AddDependency(QueueType::Compute, QueueType::Transfer, tV, PipelineStage::ComputeShader);
        (void)sched.AllocateSignal(QueueType::Compute);
        (void)sched.AllocateSignal(QueueType::Graphics);
        sched.CommitSubmit(QueueType::Transfer);
        sched.CommitSubmit(QueueType::Compute);
        sched.CommitSubmit(QueueType::Graphics);
    }
    std::string dot;
    sched.ExportWaitGraphDOT(dot);
    EXPECT_NE(dot.find("digraph"), std::string::npos);
    sched.AddDependency(QueueType::Graphics, QueueType::Compute, 99, PipelineStage::FragmentShader);
    sched.ExportWaitGraphDOT(dot);
    EXPECT_NE(dot.find("C"), std::string::npos);
    EXPECT_NE(dot.find("G"), std::string::npos);
}

TEST(CompositeDGTest, CTDG02_JSONWithDeadlock) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());
    sched.AddDependency(QueueType::Graphics, QueueType::Compute, 1, PipelineStage::FragmentShader);
    sched.AddDependency(QueueType::Compute, QueueType::Graphics, 1, PipelineStage::ComputeShader);
    EXPECT_TRUE(sched.DetectDeadlock());
    std::string json;
    sched.ExportWaitGraphJSON(json);
    EXPECT_NE(json.find("\"deadlock\":true"), std::string::npos);
    EXPECT_NE(json.find("\"queues\""), std::string::npos);
}

TEST(CompositeDGTest, CTDG03_DumpWaitGraphNoCrash) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());
    sched.AddDependency(QueueType::Graphics, QueueType::Transfer, 1, PipelineStage::Transfer);
    sched.AddDependency(QueueType::Graphics, QueueType::Compute, 2, PipelineStage::ComputeShader);
    sched.AddDependency(QueueType::Compute, QueueType::Transfer, 3, PipelineStage::ComputeShader);
    sched.AddDependency(QueueType::Transfer, QueueType::Graphics, 4, PipelineStage::Transfer);
    sched.AddDependency(QueueType::Compute, QueueType::Graphics, 5, PipelineStage::FragmentShader);
    FILE* f = std::tmpfile();
    if (f) {
        sched.DumpWaitGraph(f);
        std::fclose(f);
    }
    SUCCEED();
}

// ── §18.11 CT-SSDD — SyncScheduler + DeferredDestructor (pure CPU) ──────

TEST(CompositeSSDDTest, CTSSDD01_FrameLoopSimulation) {
    auto [mock, dev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(dev, 2);
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());
    for (uint32_t i = 1; i <= 10; ++i) {
        (void)sched.AllocateSignal(QueueType::Graphics);
        dd.SetCurrentBin(i % 2);
        dd.Destroy(BufferHandle::Pack(1, i, 0, 0));
        sched.CommitSubmit(QueueType::Graphics);
        dd.DrainBin((i + 1) % 2);
    }
    EXPECT_EQ(dd.PendingCount(), 1u);
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), 10u);
    EXPECT_FALSE(sched.DetectDeadlock());
    dd.DrainAll();
    EXPECT_EQ(dd.PendingCount(), 0u);
}

TEST(CompositeSSDDTest, CTSSDD02_ThreeBinDrainPattern) {
    auto [mock, dev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(dev, 3);
    dd.SetCurrentBin(0);
    dd.Destroy(BufferHandle::Pack(1, 1, 0, 0));
    dd.DrainBin(2);
    dd.SetCurrentBin(1);
    dd.Destroy(TextureHandle::Pack(1, 2, 0, 0));
    dd.DrainBin(0);
    dd.SetCurrentBin(2);
    dd.Destroy(SamplerHandle::Pack(1, 3, 0, 0));
    dd.DrainBin(1);
    EXPECT_EQ(dd.PendingCount(), 1u);
    dd.DrainAll();
    EXPECT_EQ(dd.PendingCount(), 0u);
}

// ── §18.12 CT-EDGE — Edge Cases (pure CPU subset) ───────────────────────

TEST(CompositeEdgeTest, CTEDGE03_SingleBinDrainAndDestroy) {
    auto [mock, dev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(dev, 1);
    for (uint32_t i = 1; i <= 10; ++i) {
        dd.Destroy(BufferHandle::Pack(1, i, 0, 0));
    }
    EXPECT_EQ(dd.PendingCount(), 10u);
    dd.DrainBin(0);
    EXPECT_EQ(dd.PendingCount(), 0u);
    for (uint32_t i = 11; i <= 15; ++i) {
        dd.Destroy(BufferHandle::Pack(1, i, 0, 0));
    }
    EXPECT_EQ(dd.PendingCount(), 5u);
    dd.DrainAll();
    EXPECT_EQ(dd.PendingCount(), 0u);
}

TEST(CompositeEdgeTest, CTEDGE04_SelfDependencyDeadlock) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());
    sched.AddDependency(QueueType::Graphics, QueueType::Graphics, 1, PipelineStage::AllCommands);
    EXPECT_TRUE(sched.DetectDeadlock());
}

TEST(CompositeEdgeTest, CTEDGE06_DestroyInvalidHandle) {
    auto [mock, dev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(dev, 2);
    dd.Destroy(BufferHandle{});
    EXPECT_EQ(dd.PendingCount(), 0u);
    dd.Destroy(TextureHandle{});
    EXPECT_EQ(dd.PendingCount(), 0u);
    dd.DrainAll();
    EXPECT_EQ(dd.PendingCount(), 0u);
}

TEST(CompositeEdgeTest, CTEDGE08_CommitWithoutAllocate) {
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());
    sched.CommitSubmit(QueueType::Graphics);
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), 0u);
}

// ============================================================================
// §18 Composite Tests — GPU backend parameterized (appended to FrameManagerTest)
// ============================================================================

// ── §18.1 CT-DD — FrameManager + DeferredDestructor ─────────────────────

TEST_P(FrameManagerTest, CTDD01_BinDrainAcrossFrames) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto [mock, mockDev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(mockDev, 2);
    fm.SetDeferredDestructor(&dd);

    // Frame 1
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        dd.Destroy(BufferHandle::Pack(1, 1, 0, 0));
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    EXPECT_EQ(dd.PendingCount(), 1u);

    // Frame 2
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        dd.Destroy(TextureHandle::Pack(1, 2, 0, 0));
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    EXPECT_EQ(dd.PendingCount(), 2u);

    // Frame 3: reuses slot 0 → drains bin 0
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        EXPECT_EQ(dd.PendingCount(), 1u);
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }

    fm.SetDeferredDestructor(nullptr);
    dd.DrainAll();
}

TEST_P(FrameManagerTest, CTDD02_ThreeFrameBinCycling) {
    auto result = MakeOffscreen(3);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto [mock, mockDev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(mockDev, 3);
    fm.SetDeferredDestructor(&dd);
    for (uint32_t i = 0; i < 6; ++i) {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        dd.Destroy(BufferHandle::Pack(1, i + 1, 0, 0));
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    EXPECT_LE(dd.PendingCount(), 3u);
    fm.SetDeferredDestructor(nullptr);
    dd.DrainAll();
}

TEST_P(FrameManagerTest, CTDD04_DestroyInvalidHandleFiltered) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto [mock, mockDev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(mockDev, 2);
    fm.SetDeferredDestructor(&dd);
    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());
    dd.Destroy(BufferHandle{});
    EXPECT_EQ(dd.PendingCount(), 0u);
    dd.Destroy(BufferHandle::Pack(1, 1, 0, 0));
    EXPECT_EQ(dd.PendingCount(), 1u);
    auto acq = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
    acq->listHandle.Dispatch([](auto& c) { c.End(); });
    (void)EndFrameSingle(fm, acq->bufferHandle);
    fm.SetDeferredDestructor(nullptr);
    dd.DrainAll();
}

TEST_P(FrameManagerTest, CTDD05_MovePreservesDDHooks) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto [mock, mockDev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(mockDev, 2);
    result->SetDeferredDestructor(&dd);
    RunFrames(*result, 1);
    auto moved = std::move(*result);
    EXPECT_EQ(result->FrameNumber(), 0u);
    EXPECT_EQ(moved.FrameNumber(), 1u);
    dd.Destroy(BufferHandle::Pack(1, 1, 0, 0));
    RunFrames(moved, 1);
    EXPECT_EQ(moved.FrameNumber(), 2u);
    moved.SetDeferredDestructor(nullptr);
    dd.DrainAll();
}

// ── §18.3 CT-XQ — Cross-Queue Sync + Frame Lifecycle ────────────────────

TEST_P(FrameManagerTest, CTXQ01_ComputeSyncConsumedAndReset) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto fakeSem = SemaphoreHandle::Pack(1, 200, 0, 0);
    // Frame 1: set compute sync
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        fm.AddComputeSyncPoint({.semaphore = fakeSem, .value = 10}, PipelineStage::ComputeShader);
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    // Frame 2: no sync
    RunFrames(fm, 1);
    EXPECT_EQ(fm.FrameNumber(), 2u);
    EXPECT_EQ(fm.CurrentTimelineValue(), 2u);
}

TEST_P(FrameManagerTest, CTXQ02_BothSyncPointsThenPartial) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto csem = SemaphoreHandle::Pack(1, 200, 0, 0);
    auto tsem = SemaphoreHandle::Pack(1, 201, 0, 0);
    // Frame 1: both
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        fm.AddComputeSyncPoint({.semaphore = csem, .value = 5}, PipelineStage::ComputeShader);
        fm.SetTransferSyncPoint({.semaphore = tsem, .value = 3});
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    // Frame 2: compute only
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        fm.AddComputeSyncPoint({.semaphore = csem, .value = 10}, PipelineStage::ComputeShader);
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    EXPECT_EQ(fm.FrameNumber(), 2u);
}

TEST_P(FrameManagerTest, CTXQ03_AlternatingComputeSync) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto csem = SemaphoreHandle::Pack(1, 200, 0, 0);
    for (uint32_t i = 1; i <= 10; ++i) {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        if (i % 2 == 1) {
            fm.AddComputeSyncPoint({.semaphore = csem, .value = i}, PipelineStage::ComputeShader);
        }
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    EXPECT_EQ(fm.FrameNumber(), 10u);
    EXPECT_EQ(fm.CurrentTimelineValue(), 10u);
}

// ── §18.4 CT-RS — Resize + Frame Lifecycle Continuity ───────────────────

TEST_P(FrameManagerTest, CTRS01_ResizePreservesCounters) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    RunFrames(fm, 5);
    EXPECT_TRUE(fm.Resize(3840, 2160).has_value());
    RunFrames(fm, 5);
    EXPECT_EQ(fm.FrameNumber(), 10u);
    EXPECT_EQ(fm.CurrentTimelineValue(), 10u);
    EXPECT_EQ(fm.FrameIndex(), 0u);
}

TEST_P(FrameManagerTest, CTRS02_ResizeWithDD) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto [mock, mockDev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(mockDev, 2);
    fm.SetDeferredDestructor(&dd);
    RunFrames(fm, 3);
    dd.Destroy(BufferHandle::Pack(1, 1, 0, 0));
    EXPECT_TRUE(fm.Resize(1280, 720).has_value());
    RunFrames(fm, 2);
    EXPECT_EQ(fm.FrameNumber(), 5u);
    fm.SetDeferredDestructor(nullptr);
    dd.DrainAll();
}

TEST_P(FrameManagerTest, CTRS03_ResizeZeroThenRestore) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    RunFrames(fm, 3);
    EXPECT_TRUE(fm.Resize(0, 0).has_value());
    EXPECT_TRUE(fm.Resize(1920, 1080).has_value());
    RunFrames(fm, 2);
    EXPECT_EQ(fm.FrameNumber(), 5u);
}

// ── §18.5 CT-WA — WaitAll + Resume ──────────────────────────────────────

TEST_P(FrameManagerTest, CTWA01_WaitAllThenResume) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    RunFrames(fm, 5);
    fm.WaitAll();
    RunFrames(fm, 5);
    EXPECT_EQ(fm.FrameNumber(), 10u);
    EXPECT_EQ(fm.CurrentTimelineValue(), 10u);
}

TEST_P(FrameManagerTest, CTWA02_DoubleWaitAll) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    RunFrames(fm, 3);
    fm.WaitAll();
    fm.WaitAll();
    RunFrames(fm, 1);
    EXPECT_EQ(fm.FrameNumber(), 4u);
}

TEST_P(FrameManagerTest, CTWA03_WaitAllDestroyThenBeginFrame) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto [mock, mockDev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(mockDev, 2);
    fm.SetDeferredDestructor(&dd);
    RunFrames(fm, 3);
    fm.WaitAll();
    dd.Destroy(BufferHandle::Pack(1, 99, 0, 0));
    EXPECT_EQ(dd.PendingCount(), 1u);
    RunFrames(fm, 1);
    EXPECT_LE(dd.PendingCount(), 1u);
    fm.SetDeferredDestructor(nullptr);
    dd.DrainAll();
}

// ── §18.6 CT-MV — Move Semantics + State Continuity ─────────────────────

TEST_P(FrameManagerTest, CTMV01_MovePreservesState) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    RunFrames(*result, 5);
    auto moved = std::move(*result);
    EXPECT_EQ(moved.FrameNumber(), 5u);
    EXPECT_EQ(moved.CurrentTimelineValue(), 5u);
    EXPECT_EQ(moved.FramesInFlight(), 2u);
    EXPECT_EQ(result->FrameNumber(), 0u);
    RunFrames(moved, 1);
    EXPECT_EQ(moved.FrameNumber(), 6u);
}

TEST_P(FrameManagerTest, CTMV02_MoveWithDD) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto [mock, mockDev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(mockDev, 2);
    result->SetDeferredDestructor(&dd);
    {
        auto ctx = result->BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        dd.Destroy(BufferHandle::Pack(1, 1, 0, 0));
        auto acq = result->AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(*result, acq->bufferHandle);
    }
    EXPECT_EQ(dd.PendingCount(), 1u);
    auto moved = std::move(*result);
    RunFrames(moved, 3);
    EXPECT_EQ(dd.PendingCount(), 0u);
    moved.SetDeferredDestructor(nullptr);
}

TEST_P(FrameManagerTest, CTMV03_WaitAllOnMovedInstances) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    RunFrames(*result, 3);
    auto moved = std::move(*result);
    result->WaitAll();  // no-op on moved-from
    moved.WaitAll();
    EXPECT_EQ(moved.CurrentTimelineValue(), 3u);
}

// ── §18.7 CT-SP — EndFrame + Cross-Queue ───────────────────────────

TEST_P(FrameManagerTest, CTSP02_SplitThenSingleTimelineContinuity) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    // Frame 1: split with 2 batches
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        auto acqA = fm.AcquireCommandList(QueueType::Graphics);
        auto acqB = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acqA && acqB);
        acqA->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acqA->listHandle.Dispatch([](auto& c) { c.End(); });
        acqB->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acqB->listHandle.Dispatch([](auto& c) { c.End(); });
        std::array<FrameManager::SubmitBatch, 2> batches
            = {FrameManager::SubmitBatch{std::span(&acqA->bufferHandle, 1), true},
               FrameManager::SubmitBatch{std::span(&acqB->bufferHandle, 1), true}};
        EXPECT_TRUE(fm.EndFrame(batches).has_value());
    }
    auto tlAfterSplit = fm.CurrentTimelineValue();
    // Frame 2: single EndFrame
    RunFrames(fm, 1);
    EXPECT_EQ(fm.FrameNumber(), 2u);
    EXPECT_GT(fm.CurrentTimelineValue(), tlAfterSplit);
}

TEST_P(FrameManagerTest, CTSP03_InterleaveEndFrameAndSplit) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    RunFrames(fm, 3);
    EXPECT_EQ(fm.CurrentTimelineValue(), 3u);
    // Frame 4: split
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        auto acqA = fm.AcquireCommandList(QueueType::Graphics);
        auto acqB = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acqA && acqB);
        acqA->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acqA->listHandle.Dispatch([](auto& c) { c.End(); });
        acqB->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acqB->listHandle.Dispatch([](auto& c) { c.End(); });
        std::array<FrameManager::SubmitBatch, 2> batches
            = {FrameManager::SubmitBatch{std::span(&acqA->bufferHandle, 1), true},
               FrameManager::SubmitBatch{std::span(&acqB->bufferHandle, 1), true}};
        EXPECT_TRUE(fm.EndFrame(batches).has_value());
    }
    // Frame 5: back to normal
    RunFrames(fm, 1);
    EXPECT_EQ(fm.FrameNumber(), 5u);
    EXPECT_GT(fm.CurrentTimelineValue(), 3u);
}

// ── §18.8 CT-BC — DD Bin Cycling Under Frame Ring ───────────────────────

TEST_P(FrameManagerTest, CTBC01_TenFrameBinCycling) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto [mock, mockDev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(mockDev, 2);
    fm.SetDeferredDestructor(&dd);
    for (uint32_t i = 0; i < 10; ++i) {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        dd.Destroy(BufferHandle::Pack(1, i + 1, 0, 0));
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    EXPECT_LE(dd.PendingCount(), 2u);
    fm.WaitAll();
    fm.SetDeferredDestructor(nullptr);
    dd.DrainAll();
    EXPECT_EQ(dd.PendingCount(), 0u);
}

TEST_P(FrameManagerTest, CTBC02_ThreeFrameThreeBin) {
    auto result = MakeOffscreen(3);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto [mock, mockDev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(mockDev, 3);
    fm.SetDeferredDestructor(&dd);
    // Frames 1-3: enqueue 1 resource each
    for (uint32_t i = 0; i < 3; ++i) {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        dd.Destroy(BufferHandle::Pack(1, i + 1, 0, 0));
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    EXPECT_EQ(dd.PendingCount(), 3u);
    // Frame 4: drains bin 0
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        EXPECT_EQ(dd.PendingCount(), 2u);
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    // Frame 5: drains bin 1
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        EXPECT_EQ(dd.PendingCount(), 1u);
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    // Frame 6: drains bin 2
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        EXPECT_EQ(dd.PendingCount(), 0u);
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    fm.SetDeferredDestructor(nullptr);
}

TEST_P(FrameManagerTest, CTBC03_BulkDestroyPerFrame) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto [mock, mockDev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(mockDev, 2);
    fm.SetDeferredDestructor(&dd);
    // Frame 1: 100 destroys
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        for (uint32_t i = 1; i <= 100; ++i) {
            dd.Destroy(BufferHandle::Pack(1, i, 0, 0));
        }
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    EXPECT_EQ(dd.PendingCount(), 100u);
    // Frame 2: 100 more
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        for (uint32_t i = 101; i <= 200; ++i) {
            dd.Destroy(BufferHandle::Pack(1, i, 0, 0));
        }
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    EXPECT_EQ(dd.PendingCount(), 200u);
    // Frame 3: drains bin 0 (100 resources)
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        EXPECT_EQ(dd.PendingCount(), 100u);
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    fm.SetDeferredDestructor(nullptr);
    dd.DrainAll();
}

// ── §18.12 CT-EDGE — Edge Cases (GPU backend subset) ────────────────────

TEST_P(FrameManagerTest, CTEDGE01_EmptyEndFrame) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());
    FrameManager::SubmitBatch empty{.commandBuffers = {}};
    auto endResult = fm.EndFrame(std::span<const FrameManager::SubmitBatch>{&empty, 1});
    EXPECT_TRUE(endResult.has_value());
    EXPECT_EQ(fm.CurrentTimelineValue(), 1u);
    RunFrames(fm, 1);
    EXPECT_EQ(fm.FrameNumber(), 2u);
}

TEST_P(FrameManagerTest, CTEDGE02_EmptySplitBatch) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());
    std::array<FrameManager::SubmitBatch, 1> batches
        = {FrameManager::SubmitBatch{std::span<const CommandBufferHandle>{}, true}};
    auto endResult = fm.EndFrame(batches);
    EXPECT_TRUE(endResult.has_value());
    EXPECT_EQ(fm.CurrentTimelineValue(), 1u);
}

TEST_P(FrameManagerTest, CTEDGE05_SingleFrameInFlight) {
    auto result = MakeOffscreen(1);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    for (uint32_t i = 0; i < 10; ++i) {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        EXPECT_EQ(fm.FrameIndex(), 0u);
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
    }
    EXPECT_EQ(fm.FrameNumber(), 10u);
    EXPECT_EQ(fm.CurrentTimelineValue(), 10u);
    EXPECT_EQ(fm.FrameIndex(), 0u);
}

TEST_P(FrameManagerTest, CTEDGE07_SyncPointBeforeBeginFrame) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto csem = SemaphoreHandle::Pack(1, 200, 0, 0);
    // Set sync point BEFORE BeginFrame
    fm.AddComputeSyncPoint({.semaphore = csem, .value = 42}, PipelineStage::ComputeShader);
    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());
    auto acq = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
    acq->listHandle.Dispatch([](auto& c) { c.End(); });
    (void)EndFrameSingle(fm, acq->bufferHandle);
    EXPECT_EQ(fm.CurrentTimelineValue(), 1u);
    // Sync point should be consumed (reset to 0) — verify via next frame with no sync
    RunFrames(fm, 1);
    EXPECT_EQ(fm.FrameNumber(), 2u);
}

// ── §18.10 CT-E2E — Full Frame Loop Simulation (GPU backend) ────────────

TEST_P(FrameManagerTest, CTE2E01_FullLoopWithDDAndSS) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto [mock, mockDev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(mockDev, 2);
    fm.SetDeferredDestructor(&dd);
    SyncScheduler sched;
    sched.Init(MakeFakeTimelines());
    auto csem = SemaphoreHandle::Pack(1, 200, 0, 0);

    for (uint32_t i = 1; i <= 20; ++i) {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        (void)sched.AllocateSignal(QueueType::Graphics);
        if (i % 2 == 0) {
            fm.AddComputeSyncPoint({.semaphore = csem, .value = i}, PipelineStage::ComputeShader);
        }
        dd.Destroy(BufferHandle::Pack(1, i, 0, 0));
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        (void)EndFrameSingle(fm, acq->bufferHandle);
        sched.CommitSubmit(QueueType::Graphics);
    }
    EXPECT_EQ(fm.FrameNumber(), 20u);
    EXPECT_EQ(fm.CurrentTimelineValue(), 20u);
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), 20u);
    EXPECT_LE(dd.PendingCount(), 2u);
    EXPECT_FALSE(sched.DetectDeadlock());
    fm.SetDeferredDestructor(nullptr);
    dd.DrainAll();
}

TEST_P(FrameManagerTest, CTE2E02_AlternatingEndFrameAndSplit) {
    auto result = MakeOffscreen(3);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto [mock, mockDev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(mockDev, 3);
    fm.SetDeferredDestructor(&dd);
    uint64_t prevTl = 0;

    for (uint32_t i = 1; i <= 20; ++i) {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        dd.Destroy(BufferHandle::Pack(1, i, 0, 0));

        if (i % 2 == 0) {
            // Split submit
            auto acqA = fm.AcquireCommandList(QueueType::Graphics);
            auto acqB = fm.AcquireCommandList(QueueType::Graphics);
            ASSERT_TRUE(acqA && acqB);
            acqA->listHandle.Dispatch([](auto& c) { c.Begin(); });
            acqA->listHandle.Dispatch([](auto& c) { c.End(); });
            acqB->listHandle.Dispatch([](auto& c) { c.Begin(); });
            acqB->listHandle.Dispatch([](auto& c) { c.End(); });
            std::array<FrameManager::SubmitBatch, 2> batches
                = {FrameManager::SubmitBatch{std::span(&acqA->bufferHandle, 1), true},
                   FrameManager::SubmitBatch{std::span(&acqB->bufferHandle, 1), true}};
            EXPECT_TRUE(fm.EndFrame(batches).has_value());
        } else {
            auto acq = fm.AcquireCommandList(QueueType::Graphics);
            ASSERT_TRUE(acq.has_value());
            acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
            acq->listHandle.Dispatch([](auto& c) { c.End(); });
            (void)EndFrameSingle(fm, acq->bufferHandle);
        }
        EXPECT_GT(fm.CurrentTimelineValue(), prevTl) << "Timeline not monotonic at frame " << i;
        prevTl = fm.CurrentTimelineValue();
    }
    EXPECT_EQ(fm.FrameNumber(), 20u);
    fm.WaitAll();
    fm.SetDeferredDestructor(nullptr);
    dd.DrainAll();
    EXPECT_EQ(dd.PendingCount(), 0u);
}

TEST_P(FrameManagerTest, CTE2E03_FramesResizeMoveResume) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto [mock, mockDev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(mockDev, 2);
    result->SetDeferredDestructor(&dd);

    RunFrames(*result, 10);
    EXPECT_TRUE(result->Resize(3840, 2160).has_value());
    RunFrames(*result, 10);

    auto moved = std::move(*result);
    RunFrames(moved, 5);

    EXPECT_EQ(moved.FrameNumber(), 25u);
    EXPECT_EQ(moved.CurrentTimelineValue(), 25u);
    EXPECT_LE(dd.PendingCount(), 2u);
    EXPECT_EQ(result->FrameNumber(), 0u);

    moved.SetDeferredDestructor(nullptr);
    dd.DrainAll();
}

// ============================================================================
// §19 INV — Invariant / Property Tests (precise value assertions)
// ============================================================================

// INV01: GIVEN offscreen FM WHEN BeginFrame THEN ctx.graphicsTimelineTarget == currentTL + 1
TEST_P(FrameManagerTest, INV01_BeginFrameTimelineTarget) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    for (uint32_t i = 0; i < 5; ++i) {
        uint64_t tlBefore = fm.CurrentTimelineValue();
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        EXPECT_EQ(ctx->graphicsTimelineTarget, tlBefore + 1)
            << "graphicsTimelineTarget must be currentTL+1 at frame " << (i + 1);
        EXPECT_EQ(ctx->frameIndex, i % 2) << "frameIndex mismatch at frame " << (i + 1);
        EXPECT_EQ(ctx->frameNumber, static_cast<uint64_t>(i + 1)) << "frameNumber mismatch";
        EXPECT_EQ(ctx->width, 1920u);
        EXPECT_EQ(ctx->height, 1080u);
        EXPECT_FALSE(ctx->swapchainImage.IsValid());

        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
        acq->listHandle.Dispatch([](auto& cmd) { cmd.End(); });
        ASSERT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
    }
}

// INV02: GIVEN offscreen FM WHEN EndFrame(single cmd) per frame THEN timeline increments by exactly 1 each time
TEST_P(FrameManagerTest, INV02_TimelineIncrementsExactlyOnePerFrame) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    for (uint32_t i = 0; i < 10; ++i) {
        uint64_t tlBefore = fm.CurrentTimelineValue();
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& cmd) { cmd.Begin(); });
        acq->listHandle.Dispatch([](auto& cmd) { cmd.End(); });
        ASSERT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
        EXPECT_EQ(fm.CurrentTimelineValue(), tlBefore + 1)
            << "Timeline must increment by exactly 1 at frame " << (i + 1);
    }
}

// INV03: GIVEN offscreen FM WHEN FrameIndex() after N frames THEN == N % framesInFlight
TEST_P(FrameManagerTest, INV03_FrameIndexModuloInvariant) {
    for (uint32_t fif : {1u, 2u, 3u}) {
        auto result = MakeOffscreen(fif);
        ASSERT_TRUE(result.has_value());
        auto& fm = *result;
        for (uint32_t i = 0; i < 12; ++i) {
            auto ctx = fm.BeginFrame();
            ASSERT_TRUE(ctx.has_value());
            EXPECT_EQ(ctx->frameIndex, i % fif) << "fif=" << fif << " frame=" << (i + 1);
            auto acq = fm.AcquireCommandList(QueueType::Graphics);
            ASSERT_TRUE(acq.has_value());
            acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
            acq->listHandle.Dispatch([](auto& c) { c.End(); });
            ASSERT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
            EXPECT_EQ(fm.FrameIndex(), (i + 1) % fif) << "After EndFrame fif=" << fif << " i=" << i;
        }
    }
}

// ============================================================================
// §20 BND — Boundary Value Tests
// ============================================================================

// BND01: GIVEN FM WHEN EndFrame with 3 batches THEN timeline increments by exactly 3
TEST_P(FrameManagerTest, BND01_ThreeBatchTimelineIncrement) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());
    uint64_t tlBefore = fm.CurrentTimelineValue();

    auto acq1 = fm.AcquireCommandList(QueueType::Graphics);
    auto acq2 = fm.AcquireCommandList(QueueType::Graphics);
    auto acq3 = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq1 && acq2 && acq3);
    for (auto* a : {&acq1, &acq2, &acq3}) {
        (*a)->listHandle.Dispatch([](auto& c) { c.Begin(); });
        (*a)->listHandle.Dispatch([](auto& c) { c.End(); });
    }

    std::array<FrameManager::SubmitBatch, 3> batches = {{
        {.commandBuffers = std::span(&acq1->bufferHandle, 1), .signalPartialTimeline = true},
        {.commandBuffers = std::span(&acq2->bufferHandle, 1), .signalPartialTimeline = true},
        {.commandBuffers = std::span(&acq3->bufferHandle, 1), .signalPartialTimeline = true},
    }};

    auto endResult = fm.EndFrame(batches);
    EXPECT_TRUE(endResult.has_value());
    EXPECT_EQ(fm.CurrentTimelineValue(), tlBefore + 3);
}

// BND02: GIVEN FM WHEN AddComputeSyncPoint with invalid semaphore THEN ignored (no crash)
TEST_P(FrameManagerTest, BND02_InvalidSemaphoreComputeSyncIgnored) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());

    // Invalid semaphore handle
    fm.AddComputeSyncPoint({.semaphore = SemaphoreHandle{}, .value = 42}, PipelineStage::ComputeShader);
    // Value = 0 (should be ignored)
    auto fakeSem = SemaphoreHandle::Pack(1, 200, 0, 0);
    fm.AddComputeSyncPoint({.semaphore = fakeSem, .value = 0}, PipelineStage::ComputeShader);

    auto acq = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
    acq->listHandle.Dispatch([](auto& c) { c.End(); });
    ASSERT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
    EXPECT_EQ(fm.CurrentTimelineValue(), 1u);
}

// BND03: GIVEN FM WHEN DrainPendingTransfers with no rings THEN returns 0
TEST_P(FrameManagerTest, BND03_DrainPendingTransfersNoRings) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    uint32_t count = fm.DrainPendingTransfers();
    EXPECT_EQ(count, 0u);
}

// BND04: GIVEN FM WHEN IsFrameComplete(0) on fresh FM THEN true
TEST_P(FrameManagerTest, BND04_IsFrameCompleteZero) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    // Frame 0 was never submitted, GPU value >= 0 is trivially true
    EXPECT_TRUE(result->IsFrameComplete(0));
}

// BND05: GIVEN FM after 3 frames WHEN IsFrameComplete(999) THEN false
TEST_P(FrameManagerTest, BND05_IsFrameCompleteFuture) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    RunFrames(fm, 3);
    fm.WaitAll();
    EXPECT_FALSE(fm.IsFrameComplete(999));
}

// BND06: GIVEN moved-from FM WHEN IsFrameComplete(anything) THEN true
TEST_P(FrameManagerTest, BND06_IsFrameCompleteMovedFrom) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    RunFrames(*result, 2);
    auto moved = std::move(*result);
    // moved-from: impl_ is null → returns true
    EXPECT_TRUE(result->IsFrameComplete(0));
    EXPECT_TRUE(result->IsFrameComplete(100));
}

// BND07: GIVEN FM WHEN WaitAll on freshly created (no frames run) THEN no hang
TEST_P(FrameManagerTest, BND07_WaitAllFreshNoFrames) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    result->WaitAll();  // currentTimelineValue == 0 → no-op
    EXPECT_EQ(result->CurrentTimelineValue(), 0u);
}

// BND08: GIVEN FM with kMaxFramesInFlight WHEN full ring rotation THEN correct
TEST_P(FrameManagerTest, BND08_MaxFramesInFlightRing) {
    auto result = MakeOffscreen(FrameManager::kMaxFramesInFlight);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    EXPECT_EQ(fm.FramesInFlight(), FrameManager::kMaxFramesInFlight);

    constexpr uint32_t kRounds = 2;
    for (uint32_t i = 0; i < FrameManager::kMaxFramesInFlight * kRounds; ++i) {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        EXPECT_EQ(ctx->frameIndex, i % FrameManager::kMaxFramesInFlight);
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        ASSERT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
    }
    EXPECT_EQ(fm.FrameNumber(), static_cast<uint64_t>(FrameManager::kMaxFramesInFlight * kRounds));
    EXPECT_EQ(fm.CurrentTimelineValue(), static_cast<uint64_t>(FrameManager::kMaxFramesInFlight * kRounds));
}

// ============================================================================
// §21 STT — State Transition Tests
// ============================================================================

// STT01: GIVEN FM WHEN EndFrame(2 batches) THEN GetLastPartialTimelineValue == first batch value
TEST_P(FrameManagerTest, STT01_PartialTimelineAfterMultiBatch) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());
    uint64_t tlBefore = fm.CurrentTimelineValue();

    auto acq1 = fm.AcquireCommandList(QueueType::Graphics);
    auto acq2 = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq1 && acq2);
    acq1->listHandle.Dispatch([](auto& c) { c.Begin(); });
    acq1->listHandle.Dispatch([](auto& c) { c.End(); });
    acq2->listHandle.Dispatch([](auto& c) { c.Begin(); });
    acq2->listHandle.Dispatch([](auto& c) { c.End(); });

    std::array<FrameManager::SubmitBatch, 2> batches = {{
        {.commandBuffers = std::span(&acq1->bufferHandle, 1), .signalPartialTimeline = true},
        {.commandBuffers = std::span(&acq2->bufferHandle, 1), .signalPartialTimeline = true},
    }};
    ASSERT_TRUE(fm.EndFrame(batches).has_value());

    // First batch signals tlBefore+1, second signals tlBefore+2
    EXPECT_EQ(fm.GetLastPartialTimelineValue(), tlBefore + 1);
    EXPECT_EQ(fm.CurrentTimelineValue(), tlBefore + 2);

    // GetGraphicsSyncPoint should return the partial value (geometry-done)
    auto sp = fm.GetGraphicsSyncPoint();
    EXPECT_TRUE(sp.semaphore.IsValid());
    EXPECT_EQ(sp.value, tlBefore + 1);
}

// STT02: GIVEN FM WHEN EndFrame(1 batch) THEN GetLastPartialTimelineValue == 0
TEST_P(FrameManagerTest, STT02_NoPartialTimelineAfterSingleBatch) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    RunFrames(fm, 1);
    EXPECT_EQ(fm.GetLastPartialTimelineValue(), 0u);
    // GetGraphicsSyncPoint falls back to currentTimelineValue
    auto sp = fm.GetGraphicsSyncPoint();
    EXPECT_EQ(sp.value, fm.CurrentTimelineValue());
}

// STT03: GIVEN FM WHEN ClearComputeSyncPoints then EndFrame THEN no compute waits
TEST_P(FrameManagerTest, STT03_ClearComputeSyncPointsExplicit) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());

    auto fakeSem = SemaphoreHandle::Pack(1, 200, 0, 0);
    fm.AddComputeSyncPoint({.semaphore = fakeSem, .value = 10}, PipelineStage::ComputeShader);
    fm.AddComputeSyncPoint({.semaphore = fakeSem, .value = 20}, PipelineStage::ComputeShader);
    fm.ClearComputeSyncPoints();  // Explicit clear before EndFrame

    auto acq = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
    acq->listHandle.Dispatch([](auto& c) { c.End(); });
    ASSERT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
    EXPECT_EQ(fm.CurrentTimelineValue(), 1u);
}

// STT04: GIVEN FM WHEN EndFrame auto-clears compute sync THEN next frame has no residual
TEST_P(FrameManagerTest, STT04_EndFrameAutoClearsComputeSync) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto fakeSem = SemaphoreHandle::Pack(1, 200, 0, 0);

    // Frame 1: add sync point
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        fm.AddComputeSyncPoint({.semaphore = fakeSem, .value = 5}, PipelineStage::ComputeShader);
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        ASSERT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
    }
    EXPECT_EQ(fm.CurrentTimelineValue(), 1u);

    // Frame 2: no sync point added — should work fine (auto-cleared by EndFrame)
    {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        ASSERT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
    }
    EXPECT_EQ(fm.CurrentTimelineValue(), 2u);
    EXPECT_EQ(fm.FrameNumber(), 2u);
}

// STT05: GIVEN FM with device-owned SyncScheduler WHEN EndFrame THEN scheduler allocations increment
TEST_P(FrameManagerTest, STT05_SyncSchedulerIntegration) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;
    auto& sched = Dev().GetSyncScheduler();

    auto baseValue = sched.GetCurrentValue(QueueType::Graphics);
    RunFrames(fm, 5);

    // SyncScheduler should track timeline values
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), baseValue + 5u);
    EXPECT_EQ(fm.CurrentTimelineValue(), baseValue + 5u);

    // Continue with same scheduler — values keep incrementing
    RunFrames(fm, 3);
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), baseValue + 8u);
    EXPECT_EQ(fm.CurrentTimelineValue(), baseValue + 8u);
}

// STT06: GIVEN FM WHEN SetTransferSyncPoint THEN BeginFrame populates transferWaitValue
TEST_P(FrameManagerTest, STT06_TransferSyncPointInContext) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto fakeSem = SemaphoreHandle::Pack(1, 300, 0, 0);
    fm.SetTransferSyncPoint({.semaphore = fakeSem, .value = 7});

    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());
    EXPECT_EQ(ctx->transferWaitValue, 7u);

    auto acq = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
    acq->listHandle.Dispatch([](auto& c) { c.End(); });
    ASSERT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());

    // After EndFrame, transferSync.value is reset to 0
    auto ctx2 = fm.BeginFrame();
    ASSERT_TRUE(ctx2.has_value());
    EXPECT_EQ(ctx2->transferWaitValue, 0u);

    auto acq2 = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq2.has_value());
    acq2->listHandle.Dispatch([](auto& c) { c.Begin(); });
    acq2->listHandle.Dispatch([](auto& c) { c.End(); });
    ASSERT_TRUE(EndFrameSingle(fm, acq2->bufferHandle).has_value());
}

// ============================================================================
// §22 STR — Stress / Adversarial Tests
// ============================================================================

// STR01: GIVEN FM WHEN 200 frames THEN timeline monotonically increases, exact value
TEST_P(FrameManagerTest, STR01_200FrameMonotonicity) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    uint64_t prevTl = 0;
    for (uint32_t i = 0; i < 200; ++i) {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        EXPECT_EQ(ctx->frameNumber, static_cast<uint64_t>(i + 1));

        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        ASSERT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());

        uint64_t curTl = fm.CurrentTimelineValue();
        EXPECT_EQ(curTl, prevTl + 1) << "Non-monotonic at frame " << (i + 1);
        prevTl = curTl;
    }
    EXPECT_EQ(fm.FrameNumber(), 200u);
    EXPECT_EQ(fm.CurrentTimelineValue(), 200u);
    EXPECT_EQ(fm.FrameIndex(), 0u);  // 200 % 2 == 0
}

// STR02: GIVEN FM WHEN alternating 1-batch and 3-batch EndFrame THEN state consistent
TEST_P(FrameManagerTest, STR02_AlternatingBatchSizes) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    uint64_t prevTl = 0;
    for (uint32_t i = 0; i < 20; ++i) {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());

        if (i % 2 == 0) {
            // Single batch
            auto acq = fm.AcquireCommandList(QueueType::Graphics);
            ASSERT_TRUE(acq.has_value());
            acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
            acq->listHandle.Dispatch([](auto& c) { c.End(); });
            ASSERT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
            EXPECT_EQ(fm.CurrentTimelineValue(), prevTl + 1) << "Single batch frame " << i;
            EXPECT_EQ(fm.GetLastPartialTimelineValue(), 0u);
        } else {
            // Triple batch
            auto a1 = fm.AcquireCommandList(QueueType::Graphics);
            auto a2 = fm.AcquireCommandList(QueueType::Graphics);
            auto a3 = fm.AcquireCommandList(QueueType::Graphics);
            ASSERT_TRUE(a1 && a2 && a3);
            for (auto* a : {&a1, &a2, &a3}) {
                (*a)->listHandle.Dispatch([](auto& c) { c.Begin(); });
                (*a)->listHandle.Dispatch([](auto& c) { c.End(); });
            }
            std::array<FrameManager::SubmitBatch, 3> batches = {{
                {.commandBuffers = std::span(&a1->bufferHandle, 1), .signalPartialTimeline = true},
                {.commandBuffers = std::span(&a2->bufferHandle, 1), .signalPartialTimeline = true},
                {.commandBuffers = std::span(&a3->bufferHandle, 1), .signalPartialTimeline = true},
            }};
            ASSERT_TRUE(fm.EndFrame(batches).has_value());
            EXPECT_EQ(fm.CurrentTimelineValue(), prevTl + 3) << "Triple batch frame " << i;
            EXPECT_EQ(fm.GetLastPartialTimelineValue(), prevTl + 1);
        }
        prevTl = fm.CurrentTimelineValue();
    }
    EXPECT_EQ(fm.FrameNumber(), 20u);
}

// STR03: GIVEN FM WHEN 3 compute sync points in one frame THEN all consumed
TEST_P(FrameManagerTest, STR03_MultiComputeSyncPointsSingleFrame) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto ctx = fm.BeginFrame();
    ASSERT_TRUE(ctx.has_value());

    auto sem1 = SemaphoreHandle::Pack(1, 200, 0, 0);
    auto sem2 = SemaphoreHandle::Pack(1, 201, 0, 0);
    auto sem3 = SemaphoreHandle::Pack(1, 202, 0, 0);

    fm.AddComputeSyncPoint({.semaphore = sem1, .value = 1}, PipelineStage::ComputeShader);
    fm.AddComputeSyncPoint({.semaphore = sem2, .value = 2}, PipelineStage::ComputeShader);
    fm.AddComputeSyncPoint({.semaphore = sem3, .value = 3}, PipelineStage::FragmentShader);

    auto acq = fm.AcquireCommandList(QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
    acq->listHandle.Dispatch([](auto& c) { c.End(); });
    ASSERT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());

    // All consumed — next frame should have no residual
    {
        auto ctx2 = fm.BeginFrame();
        ASSERT_TRUE(ctx2.has_value());
        auto acq2 = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq2.has_value());
        acq2->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq2->listHandle.Dispatch([](auto& c) { c.End(); });
        ASSERT_TRUE(EndFrameSingle(fm, acq2->bufferHandle).has_value());
    }
    EXPECT_EQ(fm.CurrentTimelineValue(), 2u);
    EXPECT_EQ(fm.FrameNumber(), 2u);
}

// STR04: GIVEN FM WHEN rapid create → run frames → destroy → recreate THEN no leak
TEST_P(FrameManagerTest, STR04_RapidCreateDestroyRecreate) {
    for (uint32_t round = 0; round < 5; ++round) {
        auto result = MakeOffscreen(2);
        ASSERT_TRUE(result.has_value()) << "Create failed round " << round;
        auto& fm = *result;
        RunFrames(fm, 10);
        EXPECT_EQ(fm.FrameNumber(), 10u);
        // SyncScheduler is shared across rounds; timeline values accumulate
        EXPECT_EQ(fm.CurrentTimelineValue(), 10u * (round + 1));
        fm.WaitAll();
        // fm destructor runs here, destroying sync objects
    }
}

// STR05: GIVEN FM WHEN WaitAll mid-loop then resume THEN counters continue correctly
TEST_P(FrameManagerTest, STR05_WaitAllMidLoopResume) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    RunFrames(fm, 10);
    EXPECT_EQ(fm.CurrentTimelineValue(), 10u);

    fm.WaitAll();

    RunFrames(fm, 10);
    EXPECT_EQ(fm.CurrentTimelineValue(), 20u);
    EXPECT_EQ(fm.FrameNumber(), 20u);

    fm.WaitAll();
    fm.WaitAll();  // Double WaitAll — idempotent

    RunFrames(fm, 5);
    EXPECT_EQ(fm.CurrentTimelineValue(), 25u);
    EXPECT_EQ(fm.FrameNumber(), 25u);
}

// STR06: GIVEN FM WHEN move mid-loop with DD + SyncScheduler THEN moved-to works, moved-from inert
TEST_P(FrameManagerTest, STR06_MoveWithFullIntegration) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());

    auto [mock, mockDev] = MakeMockDeviceHandle();
    auto dd = DeferredDestructor::Create(mockDev, 2);
    result->SetDeferredDestructor(&dd);

    auto timelines = Dev().Dispatch([](const auto& dev) { return dev.GetQueueTimelines(); });
    SyncScheduler sched;
    sched.Init(timelines);
    result->SetSyncScheduler(&sched);

    // Run 5 frames
    for (uint32_t i = 0; i < 5; ++i) {
        auto ctx = result->BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        dd.Destroy(BufferHandle::Pack(1, i + 1, 0, 0));
        auto acq = result->AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        ASSERT_TRUE(EndFrameSingle(*result, acq->bufferHandle).has_value());
    }

    // Move
    auto moved = std::move(*result);
    EXPECT_EQ(result->FrameNumber(), 0u);
    EXPECT_EQ(result->CurrentTimelineValue(), 0u);
    EXPECT_EQ(result->FramesInFlight(), 0u);

    EXPECT_EQ(moved.FrameNumber(), 5u);
    EXPECT_EQ(moved.CurrentTimelineValue(), 5u);
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), 5u);

    // Continue on moved instance
    RunFrames(moved, 5);
    EXPECT_EQ(moved.FrameNumber(), 10u);
    EXPECT_EQ(moved.CurrentTimelineValue(), 10u);
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), 10u);

    moved.SetDeferredDestructor(nullptr);
    dd.DrainAll();
}

// STR07: GIVEN FM WHEN Resize between every frame THEN counters unaffected
TEST_P(FrameManagerTest, STR07_ResizeBetweenEveryFrame) {
    auto result = MakeOffscreen(2);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    for (uint32_t i = 0; i < 10; ++i) {
        uint32_t w = 800 + i * 100;
        uint32_t h = 600 + i * 50;
        ASSERT_TRUE(fm.Resize(w, h).has_value());

        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());
        EXPECT_EQ(ctx->width, w);
        EXPECT_EQ(ctx->height, h);
        EXPECT_EQ(ctx->frameNumber, static_cast<uint64_t>(i + 1));

        auto acq = fm.AcquireCommandList(QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
        acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
        acq->listHandle.Dispatch([](auto& c) { c.End(); });
        ASSERT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
    }
    EXPECT_EQ(fm.FrameNumber(), 10u);
    EXPECT_EQ(fm.CurrentTimelineValue(), 10u);
}

// STR08: GIVEN FM WHEN interleaved compute sync + multi-batch over many frames THEN no corruption
TEST_P(FrameManagerTest, STR08_InterleavedComputeAndMultiBatch) {
    auto result = MakeOffscreen(3);
    ASSERT_TRUE(result.has_value());
    auto& fm = *result;

    auto csem = SemaphoreHandle::Pack(1, 200, 0, 0);
    uint64_t prevTl = 0;

    for (uint32_t i = 0; i < 30; ++i) {
        auto ctx = fm.BeginFrame();
        ASSERT_TRUE(ctx.has_value());

        // Every 3rd frame: add a compute sync point
        if (i % 3 == 0) {
            fm.AddComputeSyncPoint({.semaphore = csem, .value = i + 1}, PipelineStage::ComputeShader);
        }

        uint64_t expectedDelta;
        if (i % 4 == 0) {
            // Single batch
            auto acq = fm.AcquireCommandList(QueueType::Graphics);
            ASSERT_TRUE(acq.has_value());
            acq->listHandle.Dispatch([](auto& c) { c.Begin(); });
            acq->listHandle.Dispatch([](auto& c) { c.End(); });
            ASSERT_TRUE(EndFrameSingle(fm, acq->bufferHandle).has_value());
            expectedDelta = 1;
        } else {
            // Two batches
            auto a1 = fm.AcquireCommandList(QueueType::Graphics);
            auto a2 = fm.AcquireCommandList(QueueType::Graphics);
            ASSERT_TRUE(a1 && a2);
            a1->listHandle.Dispatch([](auto& c) { c.Begin(); });
            a1->listHandle.Dispatch([](auto& c) { c.End(); });
            a2->listHandle.Dispatch([](auto& c) { c.Begin(); });
            a2->listHandle.Dispatch([](auto& c) { c.End(); });
            std::array<FrameManager::SubmitBatch, 2> batches = {{
                {.commandBuffers = std::span(&a1->bufferHandle, 1), .signalPartialTimeline = true},
                {.commandBuffers = std::span(&a2->bufferHandle, 1), .signalPartialTimeline = true},
            }};
            ASSERT_TRUE(fm.EndFrame(batches).has_value());
            expectedDelta = 2;
        }

        EXPECT_EQ(fm.CurrentTimelineValue(), prevTl + expectedDelta) << "Timeline delta wrong at frame " << (i + 1);
        prevTl = fm.CurrentTimelineValue();
    }
    EXPECT_EQ(fm.FrameNumber(), 30u);
    EXPECT_EQ(fm.FrameIndex(), 0u);  // 30 % 3 == 0
}

// ============================================================================
// Instantiation — FrameManager tests parameterized across all backends
// ============================================================================

INSTANTIATE_TEST_SUITE_P(AllBackends, FrameManagerTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);
