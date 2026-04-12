// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// AsyncTaskManager unit tests — specs/03-sync.md §5.6
// Tests verify async compute task submission, polling, batched submit,
// and shutdown across Tier1 backends (Vulkan, D3D12).
// BDD style: GIVEN / WHEN / THEN.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "RhiTestFixture.h"
#include "miki/frame/AsyncTaskManager.h"
#include "miki/frame/ComputeQueueLevel.h"
#include "miki/frame/SyncScheduler.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/Sync.h"
#include "miki/rhi/backend/AllBackends.h"
#include "miki/rhi/backend/AllCommandBuffers.h"

using namespace miki;
using namespace miki::frame;
using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// Test fixture — Tier1 only (requires real async compute + timeline semaphores)
// ============================================================================

class AsyncTaskManagerTest : public RhiTest {
   protected:
    void SetUp() override {
        RhiTest::SetUp();
        if (testing::Test::IsSkipped()) {
            return;
        }

        // ATM requires timeline semaphores and async compute
        if (GetParam() == BackendType::Mock) {
            GTEST_SKIP() << "AsyncTaskManager requires real sync primitives";
        }

        auto caps = Caps();
        if (!caps.hasTimelineSemaphore) {
            GTEST_SKIP() << "Backend lacks timeline semaphores";
        }

        // Init SyncScheduler from device timelines
        auto timelines = Dev().Dispatch([](const auto& dev) { return dev.GetQueueTimelines(); });
        scheduler_.Init(timelines);
    }

    [[nodiscard]] auto MakeATM() -> core::Result<AsyncTaskManager> {
        auto level = DetectComputeQueueLevel(Caps());
        return AsyncTaskManager::Create(Dev(), scheduler_, level);
    }

    [[nodiscard]] auto RecordEmptyComputeCmd() -> CommandBufferHandle {
        CommandPoolDesc poolDesc{.queue = QueueType::Compute, .transient = true};
        auto poolResult = Dev().Dispatch([&](auto& dev) { return dev.CreateCommandPool(poolDesc); });
        if (!poolResult.has_value()) {
            return {};
        }
        computePool_ = *poolResult;

        auto acq = Dev().Dispatch([&](auto& dev) { return dev.AllocateFromPool(computePool_); });
        if (!acq.has_value()) {
            return {};
        }
        acq->listHandle.Dispatch([](auto& cmd) {
            cmd.Begin();
            cmd.End();
        });
        return acq->bufferHandle;
    }

    [[nodiscard]] auto RecordEmptyGraphicsCmd() -> CommandBufferHandle {
        CommandPoolDesc poolDesc{.queue = QueueType::Graphics, .transient = true};
        auto poolResult = Dev().Dispatch([&](auto& dev) { return dev.CreateCommandPool(poolDesc); });
        if (!poolResult.has_value()) {
            return {};
        }
        graphicsPool_ = *poolResult;

        auto acq = Dev().Dispatch([&](auto& dev) { return dev.AllocateFromPool(graphicsPool_); });
        if (!acq.has_value()) {
            return {};
        }
        acq->listHandle.Dispatch([](auto& cmd) {
            cmd.Begin();
            cmd.End();
        });
        return acq->bufferHandle;
    }

    CommandPoolHandle computePool_{};
    CommandPoolHandle graphicsPool_{};

    SyncScheduler scheduler_;
};

// ============================================================================
// §5.6.1 ATM-LC — Lifecycle
// ============================================================================

// ATM-LC-01: GIVEN invalid device WHEN Create THEN error
TEST_P(AsyncTaskManagerTest, LC01_InvalidDevice) {
    SyncScheduler sched;
    DeviceHandle invalid{};
    auto result = AsyncTaskManager::Create(invalid, sched);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), core::ErrorCode::InvalidArgument);
}

// ATM-LC-02: GIVEN valid device + scheduler WHEN Create THEN ActiveTaskCount==0
TEST_P(AsyncTaskManagerTest, LC02_CreateEmpty) {
    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ActiveTaskCount(), 0u);
}

// ATM-LC-03: GIVEN ATM WHEN move-constructed THEN dest works
TEST_P(AsyncTaskManagerTest, LC03_MoveConstruct) {
    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());

    auto atm2 = std::move(*result);
    EXPECT_EQ(atm2.ActiveTaskCount(), 0u);
}

// ATM-LC-04: GIVEN ATM with tasks WHEN destructor runs THEN Shutdown called (no leak)
TEST_P(AsyncTaskManagerTest, LC04_DestructorShutdown) {
    {
        auto result = MakeATM();
        ASSERT_TRUE(result.has_value());

        auto caps = Caps();
        if (!caps.hasAsyncCompute) {
            GTEST_SKIP() << "No async compute queue";
        }

        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "Compute cmd acquisition failed";
        }

        auto handle = result->Submit(cmd);
        (void)handle;
    }
    // If no crash/hang, destructor called Shutdown correctly
    SUCCEED();
}

// ============================================================================
// §5.6.2 ATM-SUBMIT — Submit single task
// ============================================================================

// ATM-SUBMIT-01: GIVEN ATM WHEN Submit(emptyCmd) THEN returns valid handle, ActiveTaskCount==1
TEST_P(AsyncTaskManagerTest, Submit01_SingleTask) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    auto cmd = RecordEmptyComputeCmd();
    if (!cmd.IsValid()) {
        GTEST_SKIP() << "Compute cmd acquisition failed";
    }

    auto handle = atm.Submit(cmd);
    ASSERT_TRUE(handle.has_value());
    EXPECT_TRUE(handle->IsValid());
    EXPECT_EQ(handle->id, 1u) << "First submitted task must have ID=1";
    EXPECT_EQ(atm.ActiveTaskCount(), 1u);

    atm.Shutdown();
}

// ATM-SUBMIT-02: GIVEN submitted task WHEN GetCompletionPoint THEN valid sync point
TEST_P(AsyncTaskManagerTest, Submit02_CompletionPoint) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    auto cmd = RecordEmptyComputeCmd();
    if (!cmd.IsValid()) {
        GTEST_SKIP() << "Compute cmd acquisition failed";
    }

    auto handle = atm.Submit(cmd);
    ASSERT_TRUE(handle.has_value());

    auto point = atm.GetCompletionPoint(*handle);
    EXPECT_TRUE(point.semaphore.IsValid());
    EXPECT_GT(point.value, 0u);
    // Completion point semaphore must match the AsyncCompute queue's timeline semaphore
    auto asyncComputeSem = scheduler_.GetSemaphore(QueueType::AsyncCompute);
    EXPECT_EQ(point.semaphore.IsValid(), asyncComputeSem.IsValid());
    EXPECT_EQ(point.semaphore.value, asyncComputeSem.value) << "Semaphore handle must be the AsyncCompute timeline";
    // signal value == 1 (first allocation on AsyncCompute queue after Init)
    EXPECT_EQ(point.value, 1u) << "First signal on AsyncCompute queue must be value 1";

    atm.Shutdown();
}

// ATM-SUBMIT-03: GIVEN submitted task WHEN WaitForCompletion THEN IsComplete returns true
TEST_P(AsyncTaskManagerTest, Submit03_WaitThenComplete) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    auto cmd = RecordEmptyComputeCmd();
    if (!cmd.IsValid()) {
        GTEST_SKIP() << "Compute cmd acquisition failed";
    }

    auto handle = atm.Submit(cmd);
    ASSERT_TRUE(handle.has_value());

    auto waitResult = atm.WaitForCompletion(*handle);
    ASSERT_TRUE(waitResult.has_value());
    EXPECT_TRUE(atm.IsComplete(*handle));

    // After wait, GPU semaphore must have reached the signaled value
    auto point = atm.GetCompletionPoint(*handle);
    uint64_t gpuValue = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(point.semaphore); });
    EXPECT_GE(gpuValue, point.value) << "GPU must reach signaled value after WaitForCompletion";

    atm.Shutdown();
}

// ATM-SUBMIT-04: GIVEN unknown handle WHEN IsComplete THEN true (pruned = done)
TEST_P(AsyncTaskManagerTest, Submit04_UnknownHandleComplete) {
    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());

    AsyncTaskHandle fake{.id = 999};
    EXPECT_TRUE(result->IsComplete(fake));
}

// ATM-SUBMIT-05: GIVEN unknown handle WHEN GetCompletionPoint THEN empty sync point
TEST_P(AsyncTaskManagerTest, Submit05_UnknownHandleEmptyPoint) {
    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());

    AsyncTaskHandle fake{.id = 999};
    auto point = result->GetCompletionPoint(fake);
    EXPECT_EQ(point.value, 0u);
    EXPECT_FALSE(point.semaphore.IsValid()) << "Unknown handle must return invalid semaphore";
}

// ============================================================================
// §5.6.3 ATM-BATCH — SubmitBatched
// ============================================================================

// ATM-BATCH-01: GIVEN ATM WHEN SubmitBatched(empty) THEN error
TEST_P(AsyncTaskManagerTest, Batch01_EmptyBatchError) {
    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());

    auto handle = result->SubmitBatched({});
    ASSERT_FALSE(handle.has_value());
    EXPECT_EQ(handle.error(), core::ErrorCode::InvalidArgument) << "Empty batch must return InvalidArgument";
}

// ATM-BATCH-02: GIVEN ATM WHEN SubmitBatched(2 cmds) THEN valid handle, ActiveTaskCount==1
TEST_P(AsyncTaskManagerTest, Batch02_TwoBatches) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    auto cmd1 = RecordEmptyComputeCmd();
    auto cmd2 = RecordEmptyComputeCmd();
    if (!cmd1.IsValid() || !cmd2.IsValid()) {
        GTEST_SKIP() << "Compute cmd acquisition failed";
    }

    CommandBufferHandle cmds[] = {cmd1, cmd2};
    auto handle = atm.SubmitBatched(cmds);
    ASSERT_TRUE(handle.has_value());
    EXPECT_TRUE(handle->IsValid());
    EXPECT_EQ(atm.ActiveTaskCount(), 1u);

    // Completion point must reflect the LAST batch's signal, not the first
    auto point = atm.GetCompletionPoint(*handle);
    EXPECT_TRUE(point.semaphore.IsValid());
    EXPECT_GE(point.value, 2u) << "2-batch submit: last signal value must be >= 2";

    atm.Shutdown();
}

// ATM-BATCH-03: GIVEN batched task WHEN WaitForCompletion THEN complete, GPU reaches final value
TEST_P(AsyncTaskManagerTest, Batch03_WaitBatched) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    auto cmd1 = RecordEmptyComputeCmd();
    auto cmd2 = RecordEmptyComputeCmd();
    if (!cmd1.IsValid() || !cmd2.IsValid()) {
        GTEST_SKIP() << "Compute cmd acquisition failed";
    }

    CommandBufferHandle cmds[] = {cmd1, cmd2};
    auto handle = atm.SubmitBatched(cmds);
    ASSERT_TRUE(handle.has_value());

    auto point = atm.GetCompletionPoint(*handle);
    auto waitResult = atm.WaitForCompletion(*handle);
    ASSERT_TRUE(waitResult.has_value());
    EXPECT_TRUE(atm.IsComplete(*handle));

    // GPU must have reached the last batch's signal value
    uint64_t gpuValue = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(point.semaphore); });
    EXPECT_GE(gpuValue, point.value) << "GPU must reach last batch signal after wait";

    atm.Shutdown();
}

// ============================================================================
// §5.6.4 ATM-SHUTDOWN — Shutdown
// ============================================================================

// ATM-SHUTDOWN-01: GIVEN ATM with 3 tasks WHEN Shutdown THEN ActiveTaskCount==0
TEST_P(AsyncTaskManagerTest, Shutdown01_ClearsAll) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    for (int i = 0; i < 3; ++i) {
        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "Compute cmd acquisition failed at " << i;
        }
        auto h = atm.Submit(cmd);
        ASSERT_TRUE(h.has_value());
    }
    EXPECT_EQ(atm.ActiveTaskCount(), 3u);

    atm.Shutdown();
    EXPECT_EQ(atm.ActiveTaskCount(), 0u);
}

// ATM-SHUTDOWN-02: GIVEN already-shutdown ATM WHEN Shutdown again THEN no crash
TEST_P(AsyncTaskManagerTest, Shutdown02_DoubleShutdown) {
    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());

    result->Shutdown();
    result->Shutdown();  // Should be safe
    SUCCEED();
}

// ============================================================================
// §5.6.5 ATM-ROUTE — Level-aware routing correctness
// ============================================================================

// ATM-ROUTE-01: GIVEN Level D ATM WHEN Submit THEN timeline advances on Graphics semaphore
TEST_P(AsyncTaskManagerTest, Route01_LevelDUsesGraphics) {
    auto result = AsyncTaskManager::Create(Dev(), scheduler_, ComputeQueueLevel::D_GraphicsOnly);
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    // Level D routes to Graphics queue, so we need a Graphics-compatible cmd buffer
    auto cmd = RecordEmptyGraphicsCmd();
    if (!cmd.IsValid()) {
        GTEST_SKIP() << "Graphics cmd acquisition failed";
    }

    auto graphicsBefore = scheduler_.GetCurrentValue(QueueType::Graphics);
    auto computeBefore = scheduler_.GetCurrentValue(QueueType::Compute);

    auto handle = atm.Submit(cmd);
    ASSERT_TRUE(handle.has_value());

    // Level D routes to Graphics queue — after submit + commit, Graphics currentValue advances
    auto graphicsAfter = scheduler_.GetCurrentValue(QueueType::Graphics);
    auto computeAfter = scheduler_.GetCurrentValue(QueueType::Compute);
    EXPECT_GT(graphicsAfter, graphicsBefore) << "Graphics timeline must advance for Level D";
    EXPECT_EQ(computeAfter, computeBefore) << "Compute timeline must NOT advance for Level D";

    atm.Shutdown();
}

// ATM-ROUTE-02: GIVEN Level A/B/C ATM WHEN Submit THEN timeline advances on AsyncCompute semaphore
TEST_P(AsyncTaskManagerTest, Route02_LevelABCUsesCompute) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto level = DetectComputeQueueLevel(caps);
    ASSERT_NE(level, ComputeQueueLevel::D_GraphicsOnly);

    auto result = AsyncTaskManager::Create(Dev(), scheduler_, level);
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    auto cmd = RecordEmptyComputeCmd();
    if (!cmd.IsValid()) {
        GTEST_SKIP() << "Compute cmd acquisition failed";
    }

    auto graphicsBefore = scheduler_.GetCurrentValue(QueueType::Graphics);
    auto asyncComputeBefore = scheduler_.GetCurrentValue(QueueType::AsyncCompute);

    auto handle = atm.Submit(cmd);
    ASSERT_TRUE(handle.has_value());

    auto graphicsAfter = scheduler_.GetCurrentValue(QueueType::Graphics);
    auto asyncComputeAfter = scheduler_.GetCurrentValue(QueueType::AsyncCompute);
    EXPECT_EQ(graphicsAfter, graphicsBefore) << "Graphics timeline must NOT advance for Level A/B/C";
    EXPECT_GT(asyncComputeAfter, asyncComputeBefore) << "AsyncCompute timeline must advance for Level A/B/C";

    atm.Shutdown();
}

// ATM-ROUTE-03: GIVEN Level C ATM WHEN Submit THEN completion point uses Compute semaphore
TEST_P(AsyncTaskManagerTest, Route03_CompletionPointSemaphoreMatchesQueue) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    auto cmd = RecordEmptyComputeCmd();
    if (!cmd.IsValid()) {
        GTEST_SKIP() << "Compute cmd acquisition failed";
    }

    auto handle = atm.Submit(cmd);
    ASSERT_TRUE(handle.has_value());

    auto point = atm.GetCompletionPoint(*handle);
    auto asyncComputeSem = scheduler_.GetSemaphore(QueueType::AsyncCompute);
    EXPECT_TRUE(point.semaphore.IsValid());
    EXPECT_EQ(point.semaphore.value, asyncComputeSem.value) << "Completion semaphore must be AsyncCompute timeline";
    EXPECT_GT(point.value, 0u);

    atm.Shutdown();
}

// ATM-ROUTE-04: GIVEN Level D ATM WHEN Submit THEN completion point uses Graphics semaphore
TEST_P(AsyncTaskManagerTest, Route04_LevelDCompletionPointUsesGraphics) {
    auto result = AsyncTaskManager::Create(Dev(), scheduler_, ComputeQueueLevel::D_GraphicsOnly);
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    // Level D routes to Graphics queue
    auto cmd = RecordEmptyGraphicsCmd();
    if (!cmd.IsValid()) {
        GTEST_SKIP() << "Graphics cmd acquisition failed";
    }

    auto handle = atm.Submit(cmd);
    ASSERT_TRUE(handle.has_value());

    auto point = atm.GetCompletionPoint(*handle);
    auto graphicsSem = scheduler_.GetSemaphore(QueueType::Graphics);
    EXPECT_TRUE(point.semaphore.IsValid());
    EXPECT_EQ(point.semaphore.value, graphicsSem.value) << "Level D completion semaphore must be Graphics timeline";
    // Must NOT be the Compute semaphore
    auto computeSem = scheduler_.GetSemaphore(QueueType::Compute);
    if (computeSem.IsValid() && graphicsSem.value != computeSem.value) {
        EXPECT_NE(point.semaphore.value, computeSem.value) << "Level D must not use Compute semaphore";
    }
    EXPECT_GT(point.value, 0u);

    atm.Shutdown();
}

// ============================================================================
// §5.6.6 ATM-TIMELINE — Timeline value monotonicity and correctness
// ============================================================================

// ATM-TIMELINE-01: GIVEN ATM WHEN Submit x3 THEN each completion value > previous
TEST_P(AsyncTaskManagerTest, Timeline01_MonotonicValues) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    uint64_t prevValue = 0;
    for (int i = 0; i < 3; ++i) {
        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "Compute cmd acquisition failed at " << i;
        }
        auto handle = atm.Submit(cmd);
        ASSERT_TRUE(handle.has_value()) << "Submit failed at iteration " << i;

        auto point = atm.GetCompletionPoint(*handle);
        EXPECT_GT(point.value, prevValue) << "Timeline value must be strictly monotonic at iteration " << i;
        prevValue = point.value;
    }

    atm.Shutdown();
}

// ATM-TIMELINE-02: GIVEN ATM WHEN Submit + Wait THEN GPU semaphore reaches signaled value
TEST_P(AsyncTaskManagerTest, Timeline02_GpuReachesSignaledValue) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    auto cmd = RecordEmptyComputeCmd();
    if (!cmd.IsValid()) {
        GTEST_SKIP() << "Compute cmd acquisition failed";
    }

    auto handle = atm.Submit(cmd);
    ASSERT_TRUE(handle.has_value());

    auto point = atm.GetCompletionPoint(*handle);
    auto waitResult = atm.WaitForCompletion(*handle);
    ASSERT_TRUE(waitResult.has_value());

    // After wait, GPU semaphore value must be >= signaled value
    uint64_t gpuValue = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(point.semaphore); });
    EXPECT_GE(gpuValue, point.value);

    atm.Shutdown();
}

// ============================================================================
// §5.6.7 ATM-MULTI — Concurrent multi-task lifecycle
// ============================================================================

// ATM-MULTI-01: GIVEN ATM WHEN Submit x5 THEN ActiveTaskCount==5, each handle unique
TEST_P(AsyncTaskManagerTest, Multi01_FiveTasksTracked) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    constexpr int kTaskCount = 5;
    std::vector<AsyncTaskHandle> handles;
    for (int i = 0; i < kTaskCount; ++i) {
        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "Compute cmd acquisition failed at " << i;
        }
        auto h = atm.Submit(cmd);
        ASSERT_TRUE(h.has_value()) << "Submit failed at " << i;
        EXPECT_TRUE(h->IsValid());
        handles.push_back(*h);
    }
    EXPECT_EQ(atm.ActiveTaskCount(), static_cast<uint32_t>(kTaskCount));

    // Handle IDs must be unique AND sequential (nextTaskId increments from 1)
    for (size_t i = 0; i < handles.size(); ++i) {
        EXPECT_EQ(handles[i].id, i + 1) << "Handle ID must be sequential at index " << i;
    }

    // Each task's completion point must have a unique, monotonically increasing signal value
    uint64_t prevSignal = 0;
    for (size_t i = 0; i < handles.size(); ++i) {
        auto pt = atm.GetCompletionPoint(handles[i]);
        EXPECT_TRUE(pt.semaphore.IsValid());
        EXPECT_GT(pt.value, prevSignal) << "Signal values must be strictly monotonic at index " << i;
        prevSignal = pt.value;
    }

    atm.Shutdown();
}

// ATM-MULTI-02: GIVEN 5 submitted tasks WHEN WaitForCompletion on each THEN all complete
TEST_P(AsyncTaskManagerTest, Multi02_WaitAllComplete) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    std::vector<AsyncTaskHandle> handles;
    for (int i = 0; i < 5; ++i) {
        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "Compute cmd acquisition failed at " << i;
        }
        auto h = atm.Submit(cmd);
        ASSERT_TRUE(h.has_value());
        handles.push_back(*h);
    }

    // Collect completion points before waiting
    std::vector<TimelineSyncPoint> points;
    for (auto& h : handles) {
        points.push_back(atm.GetCompletionPoint(h));
    }

    // Wait for all in reverse order (tests non-FIFO waiting)
    for (auto it = handles.rbegin(); it != handles.rend(); ++it) {
        auto waitResult = atm.WaitForCompletion(*it);
        ASSERT_TRUE(waitResult.has_value());
    }

    // All should now be complete, and GPU values must cover all signals
    for (size_t i = 0; i < handles.size(); ++i) {
        EXPECT_TRUE(atm.IsComplete(handles[i])) << "Task " << i << " must be complete after wait";
        uint64_t gpuVal = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(points[i].semaphore); });
        EXPECT_GE(gpuVal, points[i].value) << "GPU value must reach signal for task " << i;
    }

    atm.Shutdown();
}

// ATM-MULTI-03: GIVEN mixed submitted + completed tasks WHEN Shutdown THEN clean exit
TEST_P(AsyncTaskManagerTest, Multi03_ShutdownWithPartialCompletion) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    // Submit 3 tasks
    std::vector<AsyncTaskHandle> handles;
    for (int i = 0; i < 3; ++i) {
        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "Compute cmd acquisition failed";
        }
        auto h = atm.Submit(cmd);
        ASSERT_TRUE(h.has_value());
        handles.push_back(*h);
    }

    // Collect all completion points
    std::vector<TimelineSyncPoint> points;
    for (auto& h : handles) {
        auto pt = atm.GetCompletionPoint(h);
        EXPECT_TRUE(pt.semaphore.IsValid());
        EXPECT_GT(pt.value, 0u);
        points.push_back(pt);
    }

    // Wait only for the first task
    auto waitResult = atm.WaitForCompletion(handles[0]);
    ASSERT_TRUE(waitResult.has_value());
    EXPECT_TRUE(atm.IsComplete(handles[0]));

    // GPU must have reached at least the first task's signal
    uint64_t gpuVal = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(points[0].semaphore); });
    EXPECT_GE(gpuVal, points[0].value) << "GPU must reach first task's signal after wait";

    // Tasks 1 and 2 are still in activeTasks (not pruned)
    EXPECT_EQ(atm.ActiveTaskCount(), 3u) << "All 3 tasks still tracked before Shutdown";

    // Shutdown should handle the remaining tasks gracefully
    atm.Shutdown();
    EXPECT_EQ(atm.ActiveTaskCount(), 0u);
}

// ============================================================================
// §5.6.8 ATM-EDGE — Edge cases and error paths
// ============================================================================

// ATM-EDGE-01: GIVEN zero-id handle WHEN IsComplete THEN true (zero-id is never tracked)
TEST_P(AsyncTaskManagerTest, Edge01_ZeroIdHandleIsComplete) {
    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());

    AsyncTaskHandle zeroHandle{.id = 0};
    EXPECT_TRUE(result->IsComplete(zeroHandle));
    EXPECT_FALSE(zeroHandle.IsValid());
}

// ATM-EDGE-02: GIVEN zero-id handle WHEN GetCompletionPoint THEN empty sync point
TEST_P(AsyncTaskManagerTest, Edge02_ZeroIdCompletionPoint) {
    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());

    AsyncTaskHandle zeroHandle{.id = 0};
    auto point = result->GetCompletionPoint(zeroHandle);
    EXPECT_EQ(point.value, 0u);
    EXPECT_FALSE(point.semaphore.IsValid()) << "Zero-id handle must return invalid semaphore";
}

// ATM-EDGE-03: GIVEN zero-id handle WHEN WaitForCompletion THEN succeeds immediately
TEST_P(AsyncTaskManagerTest, Edge03_ZeroIdWaitSucceeds) {
    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());

    AsyncTaskHandle zeroHandle{.id = 0};
    auto waitResult = result->WaitForCompletion(zeroHandle);
    EXPECT_TRUE(waitResult.has_value());
}

// ATM-EDGE-04: GIVEN ATM WHEN double-wait same handle THEN second wait succeeds
TEST_P(AsyncTaskManagerTest, Edge04_DoubleWaitSameHandle) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    auto cmd = RecordEmptyComputeCmd();
    if (!cmd.IsValid()) {
        GTEST_SKIP() << "Compute cmd acquisition failed";
    }

    auto handle = atm.Submit(cmd);
    ASSERT_TRUE(handle.has_value());

    auto point = atm.GetCompletionPoint(*handle);

    auto wait1 = atm.WaitForCompletion(*handle);
    ASSERT_TRUE(wait1.has_value());
    EXPECT_TRUE(atm.IsComplete(*handle)) << "Must be complete after first wait";

    // Verify GPU value after first wait
    uint64_t gpuVal1 = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(point.semaphore); });
    EXPECT_GE(gpuVal1, point.value);

    // Second wait on already-completed task must succeed immediately
    auto wait2 = atm.WaitForCompletion(*handle);
    ASSERT_TRUE(wait2.has_value());
    EXPECT_TRUE(atm.IsComplete(*handle)) << "Must still be complete after second wait";

    // GPU value must not regress
    uint64_t gpuVal2 = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(point.semaphore); });
    EXPECT_GE(gpuVal2, gpuVal1) << "GPU semaphore value must not regress";

    atm.Shutdown();
}

// ATM-EDGE-05: GIVEN ATM WHEN Shutdown then Submit THEN Submit fails or is no-op
TEST_P(AsyncTaskManagerTest, Edge05_SubmitAfterShutdown) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    auto cmd = RecordEmptyComputeCmd();
    if (!cmd.IsValid()) {
        GTEST_SKIP() << "Compute cmd acquisition failed";
    }

    atm.Shutdown();
    // After shutdown, submit should still work (ATM state is cleared but device still valid)
    auto handle = atm.Submit(cmd);
    // If it succeeds, it must still be usable; if it fails, that's also acceptable
    if (handle.has_value()) {
        EXPECT_TRUE(handle->IsValid());
        atm.Shutdown();
    }
}

// ATM-EDGE-06: GIVEN ATM created with Level D WHEN hasAsyncCompute=true backend THEN still routes to Graphics
TEST_P(AsyncTaskManagerTest, Edge06_ForcedLevelDOverride) {
    // Even on a capable device, if we explicitly say Level D, it must respect that
    auto result = AsyncTaskManager::Create(Dev(), scheduler_, ComputeQueueLevel::D_GraphicsOnly);
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    // Level D routes to Graphics queue
    auto cmd = RecordEmptyGraphicsCmd();
    if (!cmd.IsValid()) {
        GTEST_SKIP() << "Graphics cmd acquisition failed";
    }

    auto graphicsBefore = scheduler_.GetCurrentValue(QueueType::Graphics);
    auto handle = atm.Submit(cmd);
    ASSERT_TRUE(handle.has_value());
    auto graphicsAfter = scheduler_.GetCurrentValue(QueueType::Graphics);

    EXPECT_GT(graphicsAfter, graphicsBefore) << "Level D must route to Graphics even on capable hardware";

    atm.Shutdown();
}

// ============================================================================
// §5.6.9 ATM-BATCHADV — Advanced batch-split tests
// ============================================================================

// ATM-BATCHADV-01: GIVEN 4-batch submit WHEN complete THEN last batch signal >= each earlier signal
TEST_P(AsyncTaskManagerTest, BatchAdv01_FourBatchMonotonic) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    constexpr int kBatchCount = 4;
    std::vector<CommandBufferHandle> cmds;
    for (int i = 0; i < kBatchCount; ++i) {
        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "Compute cmd acquisition failed at " << i;
        }
        cmds.push_back(cmd);
    }

    auto handle = atm.SubmitBatched(cmds);
    ASSERT_TRUE(handle.has_value());

    // The batched handle's completion point must reflect the LAST batch's signal
    auto point = atm.GetCompletionPoint(*handle);
    EXPECT_TRUE(point.semaphore.IsValid());
    // With 4 batches, the completion value must be >= 4 (4 AllocateSignals on this queue)
    EXPECT_GE(point.value, static_cast<uint64_t>(kBatchCount)) << "4-batch completion value must be >= batch count";

    auto waitResult = atm.WaitForCompletion(*handle);
    ASSERT_TRUE(waitResult.has_value());
    EXPECT_TRUE(atm.IsComplete(*handle));

    // GPU semaphore must have reached the last batch's signal
    uint64_t gpuVal = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(point.semaphore); });
    EXPECT_GE(gpuVal, point.value) << "GPU must reach last batch signal after wait";

    atm.Shutdown();
}

// ATM-BATCHADV-02: GIVEN single-element batch WHEN SubmitBatched THEN equivalent to Submit
TEST_P(AsyncTaskManagerTest, BatchAdv02_SingleBatchEquivalent) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    auto cmd = RecordEmptyComputeCmd();
    if (!cmd.IsValid()) {
        GTEST_SKIP() << "Compute cmd acquisition failed";
    }

    CommandBufferHandle batch[] = {cmd};
    auto handle = atm.SubmitBatched(batch);
    ASSERT_TRUE(handle.has_value());
    EXPECT_EQ(atm.ActiveTaskCount(), 1u);

    auto point = atm.GetCompletionPoint(*handle);
    EXPECT_GT(point.value, 0u);
    EXPECT_TRUE(point.semaphore.IsValid());

    atm.Shutdown();
}

// ATM-BATCHADV-03: GIVEN Submit + SubmitBatched interleaved WHEN complete THEN independent tasks
TEST_P(AsyncTaskManagerTest, BatchAdv03_InterleavedSubmitAndBatch) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    // Submit single
    auto cmd1 = RecordEmptyComputeCmd();
    if (!cmd1.IsValid()) {
        GTEST_SKIP() << "Compute cmd acquisition failed";
    }
    auto h1 = atm.Submit(cmd1);
    ASSERT_TRUE(h1.has_value());

    // Submit batched (2)
    auto cmd2 = RecordEmptyComputeCmd();
    auto cmd3 = RecordEmptyComputeCmd();
    if (!cmd2.IsValid() || !cmd3.IsValid()) {
        GTEST_SKIP() << "Compute cmd acquisition failed";
    }
    CommandBufferHandle batch[] = {cmd2, cmd3};
    auto h2 = atm.SubmitBatched(batch);
    ASSERT_TRUE(h2.has_value());

    // Submit single again
    auto cmd4 = RecordEmptyComputeCmd();
    if (!cmd4.IsValid()) {
        GTEST_SKIP() << "Compute cmd acquisition failed";
    }
    auto h3 = atm.Submit(cmd4);
    ASSERT_TRUE(h3.has_value());

    EXPECT_EQ(atm.ActiveTaskCount(), 3u);

    // Each handle has independent completion points on the same semaphore
    auto p1 = atm.GetCompletionPoint(*h1);
    auto p2 = atm.GetCompletionPoint(*h2);
    auto p3 = atm.GetCompletionPoint(*h3);

    // All on same queue's semaphore
    EXPECT_EQ(p1.semaphore.value, p2.semaphore.value);
    EXPECT_EQ(p2.semaphore.value, p3.semaphore.value);

    // Strictly monotonic: Submit(1cmd) < SubmitBatched(2cmd last) < Submit(1cmd)
    EXPECT_LT(p1.value, p2.value) << "Batched completion must exceed first single submit";
    EXPECT_LT(p2.value, p3.value) << "Third submit must exceed batched completion";
    // SubmitBatched(2 cmds) allocates 2 signals; the gap between p1 and p2 must be >= 2
    EXPECT_GE(p2.value - p1.value, 2u) << "2-batch gap must be >= 2";
    // Single submit allocates 1 signal
    EXPECT_EQ(p3.value - p2.value, 1u) << "Single submit after batch must advance by exactly 1";

    // Wait all and verify GPU values
    atm.WaitForCompletion(*h3);  // Last task implies all previous are complete
    uint64_t gpuVal = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(p3.semaphore); });
    EXPECT_GE(gpuVal, p3.value) << "GPU must reach final signal";

    atm.Shutdown();
}

// ATM-BATCHADV-04: GIVEN Level D ATM WHEN SubmitBatched THEN routes to Graphics queue
TEST_P(AsyncTaskManagerTest, BatchAdv04_LevelDBatchRoutesToGraphics) {
    auto result = AsyncTaskManager::Create(Dev(), scheduler_, ComputeQueueLevel::D_GraphicsOnly);
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    // Level D routes to Graphics queue
    auto cmd1 = RecordEmptyGraphicsCmd();
    auto cmd2 = RecordEmptyGraphicsCmd();
    if (!cmd1.IsValid() || !cmd2.IsValid()) {
        GTEST_SKIP() << "Graphics cmd acquisition failed";
    }

    auto graphicsBefore = scheduler_.GetCurrentValue(QueueType::Graphics);
    auto computeBefore = scheduler_.GetCurrentValue(QueueType::Compute);

    CommandBufferHandle batch[] = {cmd1, cmd2};
    auto handle = atm.SubmitBatched(batch);
    ASSERT_TRUE(handle.has_value());

    auto graphicsAfter = scheduler_.GetCurrentValue(QueueType::Graphics);
    auto computeAfter = scheduler_.GetCurrentValue(QueueType::Compute);

    // 2-batch on Graphics queue: timeline should advance by 2
    EXPECT_GE(graphicsAfter - graphicsBefore, 2u) << "Graphics timeline must advance by batch count";
    EXPECT_EQ(computeAfter, computeBefore) << "Compute timeline must NOT advance for Level D";

    atm.Shutdown();
}

// ============================================================================
// §5.6.10 ATM-COMPLEX — Complex temporal / state-machine tests
// ============================================================================

// ATM-COMPLEX-01: Submit→Wait→Submit→Wait cycle verifies scheduler state resets correctly
// between independent task lifecycles. Each cycle must produce fresh, monotonic timeline values.
TEST_P(AsyncTaskManagerTest, Complex01_SubmitWaitCycleSchedulerState) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    uint64_t prevCompletionValue = 0;
    constexpr int kCycles = 4;

    for (int cycle = 0; cycle < kCycles; ++cycle) {
        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "Compute cmd acquisition failed at cycle " << cycle;
        }

        auto handle = atm.Submit(cmd);
        ASSERT_TRUE(handle.has_value()) << "Submit failed at cycle " << cycle;
        EXPECT_EQ(handle->id, static_cast<uint64_t>(cycle + 1)) << "Handle ID must be cycle+1";

        auto point = atm.GetCompletionPoint(*handle);
        EXPECT_GT(point.value, prevCompletionValue)
            << "Completion value must exceed previous cycle's value at cycle " << cycle;

        // Verify scheduler currentValue advanced exactly by 1 per submit
        auto schedulerCurrent = scheduler_.GetCurrentValue(QueueType::AsyncCompute);
        EXPECT_EQ(schedulerCurrent, point.value)
            << "Scheduler currentValue must equal completion value at cycle " << cycle;

        auto waitResult = atm.WaitForCompletion(*handle);
        ASSERT_TRUE(waitResult.has_value()) << "Wait failed at cycle " << cycle;
        EXPECT_TRUE(atm.IsComplete(*handle));

        // GPU must have reached the value
        uint64_t gpuVal = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(point.semaphore); });
        EXPECT_GE(gpuVal, point.value) << "GPU must reach signal at cycle " << cycle;

        prevCompletionValue = point.value;
    }

    // After 4 cycles of Submit+Wait, all tasks should still be tracked (no auto-prune on wait)
    // ActiveTaskCount should be kCycles since WaitForCompletion doesn't remove from activeTasks
    EXPECT_EQ(atm.ActiveTaskCount(), static_cast<uint32_t>(kCycles));
    atm.Shutdown();
    EXPECT_EQ(atm.ActiveTaskCount(), 0u);
}

// ATM-COMPLEX-02: Cascading batch→single→batch pattern with precise signal arithmetic.
// Verifies that SubmitBatched(N) allocates exactly N signals, and single Submit allocates 1.
TEST_P(AsyncTaskManagerTest, Complex02_CascadingBatchSingleSignalArithmetic) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    auto baselineValue = scheduler_.GetCurrentValue(QueueType::AsyncCompute);

    // Phase 1: SubmitBatched(3 cmds) → allocates 3 signals
    std::vector<CommandBufferHandle> batch1;
    for (int i = 0; i < 3; ++i) {
        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "cmd failed";
        }
        batch1.push_back(cmd);
    }
    auto h1 = atm.SubmitBatched(batch1);
    ASSERT_TRUE(h1.has_value());
    auto p1 = atm.GetCompletionPoint(*h1);
    EXPECT_EQ(p1.value, baselineValue + 3) << "3-batch must advance by exactly 3";
    EXPECT_EQ(scheduler_.GetCurrentValue(QueueType::AsyncCompute), baselineValue + 3);

    // Phase 2: Submit(single) → allocates 1 signal
    auto cmd2 = RecordEmptyComputeCmd();
    if (!cmd2.IsValid()) {
        GTEST_SKIP() << "cmd failed";
    }
    auto h2 = atm.Submit(cmd2);
    ASSERT_TRUE(h2.has_value());
    auto p2 = atm.GetCompletionPoint(*h2);
    EXPECT_EQ(p2.value, baselineValue + 4) << "Single submit after 3-batch must be baseline+4";
    EXPECT_EQ(scheduler_.GetCurrentValue(QueueType::AsyncCompute), baselineValue + 4);

    // Phase 3: SubmitBatched(2 cmds) → allocates 2 signals
    std::vector<CommandBufferHandle> batch3;
    for (int i = 0; i < 2; ++i) {
        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "cmd failed";
        }
        batch3.push_back(cmd);
    }
    auto h3 = atm.SubmitBatched(batch3);
    ASSERT_TRUE(h3.has_value());
    auto p3 = atm.GetCompletionPoint(*h3);
    EXPECT_EQ(p3.value, baselineValue + 6) << "2-batch after single must be baseline+6";
    EXPECT_EQ(scheduler_.GetCurrentValue(QueueType::AsyncCompute), baselineValue + 6);

    // Total: 3 tasks tracked, 6 signals allocated
    EXPECT_EQ(atm.ActiveTaskCount(), 3u);

    // Wait for the last one — since timeline is monotonic, this implies all prior are done
    auto waitResult = atm.WaitForCompletion(*h3);
    ASSERT_TRUE(waitResult.has_value());

    uint64_t gpuVal = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(p3.semaphore); });
    EXPECT_GE(gpuVal, p3.value);
    // All earlier tasks must also be complete (monotonic timeline)
    EXPECT_TRUE(atm.IsComplete(*h1));
    EXPECT_TRUE(atm.IsComplete(*h2));

    atm.Shutdown();
}

// ATM-COMPLEX-03: Cross-queue dependency injection — Graphics queue waits on ATM's completion.
// Simulates the real-world pattern: ATM does async compute, Graphics waits for result.
TEST_P(AsyncTaskManagerTest, Complex03_CrossQueueDependencyInjection) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    // Submit async compute task
    auto cmd = RecordEmptyComputeCmd();
    if (!cmd.IsValid()) {
        GTEST_SKIP() << "Compute cmd acquisition failed";
    }

    auto handle = atm.Submit(cmd);
    ASSERT_TRUE(handle.has_value());

    auto completionPoint = atm.GetCompletionPoint(*handle);
    EXPECT_TRUE(completionPoint.semaphore.IsValid());
    EXPECT_GT(completionPoint.value, 0u);

    // Inject dependency: Graphics queue must wait for ATM's async compute task
    scheduler_.AddDependency(
        QueueType::Graphics, QueueType::AsyncCompute, completionPoint.value, PipelineStage::AllCommands
    );

    // Verify the dependency was recorded
    auto waits = scheduler_.GetPendingWaits(QueueType::Graphics);
    ASSERT_GE(waits.size(), 1u) << "Graphics queue must have at least 1 pending wait";

    // Find the wait entry matching our completion point
    bool found = false;
    for (auto& w : waits) {
        if (w.semaphore.value == completionPoint.semaphore.value && w.value == completionPoint.value) {
            found = true;
            EXPECT_EQ(w.stageMask, PipelineStage::AllCommands);
            break;
        }
    }
    EXPECT_TRUE(found) << "Must find the ATM completion point in Graphics pending waits";

    // Verify no deadlock in the wait graph (compute → nothing, graphics → compute)
    EXPECT_FALSE(scheduler_.DetectDeadlock()) << "No deadlock expected for unidirectional dependency";

    // Now simulate Graphics submit: allocate Graphics signal, commit
    auto graphicsSignal = scheduler_.AllocateSignal(QueueType::Graphics);
    EXPECT_GT(graphicsSignal, 0u);

    // After commit, pending waits should be cleared
    scheduler_.CommitSubmit(QueueType::Graphics);
    auto waitsAfter = scheduler_.GetPendingWaits(QueueType::Graphics);
    EXPECT_TRUE(waitsAfter.empty()) << "Pending waits must be cleared after CommitSubmit";

    atm.Shutdown();
}

// ATM-COMPLEX-04: Level D + Level C ATM instances on the same scheduler — verify isolation.
// Two ATM instances with different levels must route to different queues without interference.
TEST_P(AsyncTaskManagerTest, Complex04_DualATMQueueIsolation) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    // Create Level C (→Compute) and Level D (→Graphics) ATMs sharing the same scheduler
    auto resultC = AsyncTaskManager::Create(Dev(), scheduler_, ComputeQueueLevel::C_SingleQueueBatch);
    ASSERT_TRUE(resultC.has_value());
    auto& atmC = *resultC;

    auto resultD = AsyncTaskManager::Create(Dev(), scheduler_, ComputeQueueLevel::D_GraphicsOnly);
    ASSERT_TRUE(resultD.has_value());
    auto& atmD = *resultD;

    auto asyncComputeBefore = scheduler_.GetCurrentValue(QueueType::AsyncCompute);
    auto graphicsBefore = scheduler_.GetCurrentValue(QueueType::Graphics);

    // Submit to Level C ATM → should advance AsyncCompute timeline
    auto cmdC = RecordEmptyComputeCmd();
    if (!cmdC.IsValid()) {
        GTEST_SKIP() << "Compute cmd failed";
    }
    auto hC = atmC.Submit(cmdC);
    ASSERT_TRUE(hC.has_value());

    auto asyncComputeAfterC = scheduler_.GetCurrentValue(QueueType::AsyncCompute);
    auto graphicsAfterC = scheduler_.GetCurrentValue(QueueType::Graphics);
    EXPECT_EQ(asyncComputeAfterC, asyncComputeBefore + 1) << "Level C must advance AsyncCompute by exactly 1";
    EXPECT_EQ(graphicsAfterC, graphicsBefore) << "Level C must not touch Graphics";

    // Submit to Level D ATM → should advance Graphics timeline
    auto cmdD = RecordEmptyGraphicsCmd();
    if (!cmdD.IsValid()) {
        GTEST_SKIP() << "Graphics cmd failed";
    }
    auto hD = atmD.Submit(cmdD);
    ASSERT_TRUE(hD.has_value());

    auto asyncComputeAfterD = scheduler_.GetCurrentValue(QueueType::AsyncCompute);
    auto graphicsAfterD = scheduler_.GetCurrentValue(QueueType::Graphics);
    EXPECT_EQ(asyncComputeAfterD, asyncComputeBefore + 1) << "Level D must not touch AsyncCompute";
    EXPECT_EQ(graphicsAfterD, graphicsBefore + 1) << "Level D must advance Graphics by exactly 1";

    // Completion semaphores must be different
    auto ptC = atmC.GetCompletionPoint(*hC);
    auto ptD = atmD.GetCompletionPoint(*hD);
    EXPECT_NE(ptC.semaphore.value, ptD.semaphore.value) << "Level C and D must use different timeline semaphores";

    // Both must complete independently
    atmC.WaitForCompletion(*hC);
    atmD.WaitForCompletion(*hD);
    EXPECT_TRUE(atmC.IsComplete(*hC));
    EXPECT_TRUE(atmD.IsComplete(*hD));

    atmC.Shutdown();
    atmD.Shutdown();
}

// ATM-COMPLEX-05: Rapid fire submit + selective wait pattern.
// Submit 8 tasks, wait only for tasks 2, 5, 7 (out of order), verify partial completion state.
TEST_P(AsyncTaskManagerTest, Complex05_SelectiveWaitPartialCompletion) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    constexpr int kTaskCount = 8;
    std::vector<AsyncTaskHandle> handles;
    std::vector<TimelineSyncPoint> points;

    for (int i = 0; i < kTaskCount; ++i) {
        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "cmd failed at " << i;
        }
        auto h = atm.Submit(cmd);
        ASSERT_TRUE(h.has_value());
        handles.push_back(*h);
        points.push_back(atm.GetCompletionPoint(*h));
    }
    EXPECT_EQ(atm.ActiveTaskCount(), static_cast<uint32_t>(kTaskCount));

    // Verify all signal values are sequential from baseline
    for (int i = 1; i < kTaskCount; ++i) {
        EXPECT_EQ(points[i].value, points[i - 1].value + 1) << "Signal values must be consecutive at index " << i;
    }

    // Wait for task index 6 (7th task) — on a monotonic timeline, this means tasks 0..6 are complete
    auto wait7 = atm.WaitForCompletion(handles[6]);
    ASSERT_TRUE(wait7.has_value());

    // Tasks 0..6 must be complete (timeline monotonicity guarantee)
    for (int i = 0; i <= 6; ++i) {
        EXPECT_TRUE(atm.IsComplete(handles[i])) << "Task " << i << " must be complete after waiting for task 6";
    }

    // Task 7 might or might not be complete (GPU may have finished all empty cmds already)
    // Just verify it doesn't crash
    [[maybe_unused]] bool task7Done = atm.IsComplete(handles[7]);

    // Now wait specifically for task 1 (already complete) — must return immediately
    auto wait1 = atm.WaitForCompletion(handles[1]);
    ASSERT_TRUE(wait1.has_value());

    // GPU value must cover task 6's signal at minimum
    uint64_t gpuVal = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(points[6].semaphore); });
    EXPECT_GE(gpuVal, points[6].value);

    atm.Shutdown();
}

// ATM-COMPLEX-06: SubmitBatched with increasing batch sizes + scheduler state verification.
// Batch(1) → Batch(2) → Batch(3) → verify total signal count = 6, handle count = 3.
TEST_P(AsyncTaskManagerTest, Complex06_IncreasingBatchSizes) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    auto baseline = scheduler_.GetCurrentValue(QueueType::AsyncCompute);

    // Batch(1)
    auto cmd1 = RecordEmptyComputeCmd();
    if (!cmd1.IsValid()) {
        GTEST_SKIP() << "cmd failed";
    }
    CommandBufferHandle b1[] = {cmd1};
    auto h1 = atm.SubmitBatched(b1);
    ASSERT_TRUE(h1.has_value());
    auto p1 = atm.GetCompletionPoint(*h1);
    EXPECT_EQ(p1.value, baseline + 1) << "Batch(1) signal must be baseline+1";

    // Batch(2)
    std::vector<CommandBufferHandle> b2;
    for (int i = 0; i < 2; ++i) {
        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "cmd failed";
        }
        b2.push_back(cmd);
    }
    auto h2 = atm.SubmitBatched(b2);
    ASSERT_TRUE(h2.has_value());
    auto p2 = atm.GetCompletionPoint(*h2);
    EXPECT_EQ(p2.value, baseline + 3) << "Batch(2) signal must be baseline+3";

    // Batch(3)
    std::vector<CommandBufferHandle> b3;
    for (int i = 0; i < 3; ++i) {
        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "cmd failed";
        }
        b3.push_back(cmd);
    }
    auto h3 = atm.SubmitBatched(b3);
    ASSERT_TRUE(h3.has_value());
    auto p3 = atm.GetCompletionPoint(*h3);
    EXPECT_EQ(p3.value, baseline + 6) << "Batch(3) signal must be baseline+6";

    // Scheduler state: 6 total signals allocated
    EXPECT_EQ(scheduler_.GetCurrentValue(QueueType::AsyncCompute), baseline + 6);

    // 3 tracked tasks
    EXPECT_EQ(atm.ActiveTaskCount(), 3u);

    // Wait for last → all complete (monotonic timeline)
    atm.WaitForCompletion(*h3);
    EXPECT_TRUE(atm.IsComplete(*h1));
    EXPECT_TRUE(atm.IsComplete(*h2));
    EXPECT_TRUE(atm.IsComplete(*h3));

    uint64_t gpuVal = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(p3.semaphore); });
    EXPECT_GE(gpuVal, p3.value) << "GPU must reach final batch signal";

    atm.Shutdown();
}

// ============================================================================
// §5.6.11 ATM-DEADLOCK — Deadlock-prone multi-flow & stress tests
// ============================================================================

// ATM-DEADLOCK-01: Bidirectional cross-queue dependency cycle detection.
// Graphics waits on Compute, Compute waits on Graphics → must detect deadlock.
// This is a scheduler-level test: no actual GPU submit (would hang), only verify
// the DetectDeadlock() oracle catches the cycle before any submit occurs.
TEST_P(AsyncTaskManagerTest, Deadlock01_BidirectionalCycleDetected) {
    // DetectDeadlock() creates an active edge i→j only when:
    //   queue i has a pending wait on queue j's semaphore with value V,
    //   AND queues_[j].currentValue < V  (i.e., j hasn't committed up to V yet).
    //
    // Strategy: Allocate signals on both Compute and Graphics but do NOT commit them,
    // then add cross-waits referencing those uncommitted values. Both edges become active → cycle.

    // Step 1: Allocate Compute signal (no commit → Compute.currentValue stays at initial)
    auto computeVal = scheduler_.AllocateSignal(QueueType::Compute);
    // Compute.nextValue = initial+1, Compute.currentValue = initial (still 0 or 1)

    // Step 2: Allocate Graphics signal (no commit → Graphics.currentValue stays at initial)
    auto graphicsVal = scheduler_.AllocateSignal(QueueType::Graphics);

    // Before any dependencies, no cycle
    EXPECT_FALSE(scheduler_.DetectDeadlock());

    // Step 3: Graphics waits on Compute value computeVal (unsatisfied: currentValue < computeVal)
    scheduler_.AddDependency(QueueType::Graphics, QueueType::Compute, computeVal, PipelineStage::AllCommands);
    EXPECT_FALSE(scheduler_.DetectDeadlock()) << "Single unsatisfied edge: no cycle yet";

    // Step 4: Compute waits on Graphics value graphicsVal (unsatisfied: currentValue < graphicsVal)
    // This closes the cycle: G→C (G waits on C) + C→G (C waits on G)
    scheduler_.AddDependency(QueueType::Compute, QueueType::Graphics, graphicsVal, PipelineStage::AllCommands);

    bool deadlocked = scheduler_.DetectDeadlock();
    EXPECT_TRUE(deadlocked) << "Bidirectional unsatisfied dependencies must be detected as deadlock";

    // Verify wait-graph export doesn't crash under deadlock state
    std::string dot, json;
    scheduler_.ExportWaitGraphDOT(dot);
    scheduler_.ExportWaitGraphJSON(json);
    EXPECT_FALSE(dot.empty());
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("\"deadlock\":true"), std::string::npos) << "JSON export must report deadlock=true";

    // Resolve: commit Graphics → breaks the C→G edge (Graphics.currentValue reaches graphicsVal)
    scheduler_.CommitSubmit(QueueType::Graphics);
    // Now only G→C edge remains (still unsatisfied), but no cycle
    EXPECT_FALSE(scheduler_.DetectDeadlock()) << "Single edge remaining: no cycle";

    // Commit Compute to clean up
    scheduler_.CommitSubmit(QueueType::Compute);
    EXPECT_FALSE(scheduler_.DetectDeadlock()) << "All committed: no cycle";
}

// ATM-DEADLOCK-02: Triple-queue circular dependency detection.
// Graphics → Compute → Transfer → Graphics must be detected.
TEST_P(AsyncTaskManagerTest, Deadlock02_TripleQueueCircularDependency) {
    // This test exercises the scheduler's DFS cycle detection on the full 3-node graph.
    // No GPU work needed — purely scheduler state verification.

    // Allocate signals on all 3 queues but don't commit (so values are "unsatisfied")
    auto gVal = scheduler_.AllocateSignal(QueueType::Graphics);
    auto cVal = scheduler_.AllocateSignal(QueueType::Compute);
    auto tVal = scheduler_.AllocateSignal(QueueType::Transfer);
    // Don't commit any — all signals are unsatisfied

    // No dependencies yet → no deadlock
    EXPECT_FALSE(scheduler_.DetectDeadlock());

    // Chain: Graphics waits on Compute
    scheduler_.AddDependency(QueueType::Graphics, QueueType::Compute, cVal, PipelineStage::AllCommands);
    EXPECT_FALSE(scheduler_.DetectDeadlock()) << "Single edge: no cycle";

    // Chain: Compute waits on Transfer
    scheduler_.AddDependency(QueueType::Compute, QueueType::Transfer, tVal, PipelineStage::AllCommands);
    EXPECT_FALSE(scheduler_.DetectDeadlock()) << "Two edges (linear chain): no cycle";

    // Close the cycle: Transfer waits on Graphics
    scheduler_.AddDependency(QueueType::Transfer, QueueType::Graphics, gVal, PipelineStage::AllCommands);
    EXPECT_TRUE(scheduler_.DetectDeadlock()) << "G→C→T→G must be detected as a cycle";

    // Verify DOT export contains all 3 queue names
    std::string dot;
    scheduler_.ExportWaitGraphDOT(dot);
    EXPECT_NE(dot.find("G"), std::string::npos);
    EXPECT_NE(dot.find("C"), std::string::npos);
    EXPECT_NE(dot.find("T"), std::string::npos);

    // Resolve by committing Transfer (breaks T→G edge since T's signal is now satisfied)
    scheduler_.CommitSubmit(QueueType::Transfer);
    // Transfer's pending waits (Transfer waits on Graphics) are cleared by CommitSubmit
    EXPECT_FALSE(scheduler_.DetectDeadlock()) << "Breaking one edge must resolve the cycle";

    // Clean up remaining
    scheduler_.CommitSubmit(QueueType::Graphics);
    scheduler_.CommitSubmit(QueueType::Compute);
}

// ATM-DEADLOCK-03: Multi-ATM interleaved submit-wait stress test.
// 3 ATM instances (Level C, Level C, Level D) each submit N tasks interleaved,
// then wait in scrambled order. Verifies no hang, correct completion, and
// scheduler state integrity under heavy mixed-queue traffic.
TEST_P(AsyncTaskManagerTest, Deadlock03_MultiATMInterleavedStress) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto resultA = AsyncTaskManager::Create(Dev(), scheduler_, ComputeQueueLevel::C_SingleQueueBatch);
    ASSERT_TRUE(resultA.has_value());
    auto& atmA = *resultA;

    auto resultB = AsyncTaskManager::Create(Dev(), scheduler_, ComputeQueueLevel::C_SingleQueueBatch);
    ASSERT_TRUE(resultB.has_value());
    auto& atmB = *resultB;

    auto resultD = AsyncTaskManager::Create(Dev(), scheduler_, ComputeQueueLevel::D_GraphicsOnly);
    ASSERT_TRUE(resultD.has_value());
    auto& atmD = *resultD;

    auto asyncComputeBaseline = scheduler_.GetCurrentValue(QueueType::AsyncCompute);
    auto graphicsBaseline = scheduler_.GetCurrentValue(QueueType::Graphics);

    constexpr int kRounds = 4;
    struct TaskRecord {
        AsyncTaskManager* atm;
        AsyncTaskHandle handle;
        TimelineSyncPoint point;
        QueueType expectedQueue;
    };
    std::vector<TaskRecord> allTasks;

    // Interleaved: A(compute), D(graphics), B(compute), D(graphics), ...
    for (int r = 0; r < kRounds; ++r) {
        // ATM-A submits to Compute
        auto cmdA = RecordEmptyComputeCmd();
        if (!cmdA.IsValid()) {
            GTEST_SKIP() << "cmd failed";
        }
        auto hA = atmA.Submit(cmdA);
        ASSERT_TRUE(hA.has_value());
        allTasks.push_back({&atmA, *hA, atmA.GetCompletionPoint(*hA), QueueType::AsyncCompute});

        // ATM-D submits to Graphics
        auto cmdD = RecordEmptyGraphicsCmd();
        if (!cmdD.IsValid()) {
            GTEST_SKIP() << "cmd failed";
        }
        auto hD = atmD.Submit(cmdD);
        ASSERT_TRUE(hD.has_value());
        allTasks.push_back({&atmD, *hD, atmD.GetCompletionPoint(*hD), QueueType::Graphics});

        // ATM-B submits to Compute
        auto cmdB = RecordEmptyComputeCmd();
        if (!cmdB.IsValid()) {
            GTEST_SKIP() << "cmd failed";
        }
        auto hB = atmB.Submit(cmdB);
        ASSERT_TRUE(hB.has_value());
        allTasks.push_back({&atmB, *hB, atmB.GetCompletionPoint(*hB), QueueType::AsyncCompute});
    }

    // Verify scheduler advanced correctly: 2*kRounds asyncCompute signals, kRounds graphics signals
    EXPECT_EQ(scheduler_.GetCurrentValue(QueueType::AsyncCompute), asyncComputeBaseline + 2 * kRounds)
        << "AsyncCompute timeline must advance by 2 per round (ATM-A + ATM-B)";
    EXPECT_EQ(scheduler_.GetCurrentValue(QueueType::Graphics), graphicsBaseline + kRounds)
        << "Graphics timeline must advance by 1 per round (ATM-D)";

    // Verify each task's completion point uses the correct semaphore
    auto asyncComputeSem = scheduler_.GetSemaphore(QueueType::AsyncCompute);
    auto graphicsSem = scheduler_.GetSemaphore(QueueType::Graphics);
    for (size_t i = 0; i < allTasks.size(); ++i) {
        auto& t = allTasks[i];
        EXPECT_TRUE(t.point.semaphore.IsValid()) << "Task " << i << " must have valid semaphore";
        if (t.expectedQueue == QueueType::AsyncCompute) {
            EXPECT_EQ(t.point.semaphore.value, asyncComputeSem.value) << "Task " << i << " must use AsyncCompute sem";
        } else {
            EXPECT_EQ(t.point.semaphore.value, graphicsSem.value) << "Task " << i << " must use Graphics sem";
        }
    }

    // No deadlock expected (no cross-queue dependencies added)
    EXPECT_FALSE(scheduler_.DetectDeadlock());

    // Wait in reverse order — this is the most deadlock-prone pattern for buggy implementations
    for (auto it = allTasks.rbegin(); it != allTasks.rend(); ++it) {
        auto waitResult = it->atm->WaitForCompletion(it->handle);
        ASSERT_TRUE(waitResult.has_value()) << "Wait must succeed (no hang)";
    }

    // All must be complete with correct GPU values
    for (size_t i = 0; i < allTasks.size(); ++i) {
        auto& t = allTasks[i];
        EXPECT_TRUE(t.atm->IsComplete(t.handle)) << "Task " << i << " must be complete";
        uint64_t gpuVal = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(t.point.semaphore); });
        EXPECT_GE(gpuVal, t.point.value) << "GPU must reach signal for task " << i;
    }

    atmA.Shutdown();
    atmB.Shutdown();
    atmD.Shutdown();
}

// ATM-DEADLOCK-04: Massive sequential submit stress (32 tasks) + single wait on last.
// Exercises the timeline semaphore with a high signal value. On buggy implementations,
// large semaphore values can overflow or wrap. Also tests PruneCompleted performance.
TEST_P(AsyncTaskManagerTest, Deadlock04_MassiveSequentialSubmitStress) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    auto baseline = scheduler_.GetCurrentValue(QueueType::AsyncCompute);
    constexpr int kTaskCount = 32;

    std::vector<AsyncTaskHandle> handles;
    std::vector<TimelineSyncPoint> points;
    for (int i = 0; i < kTaskCount; ++i) {
        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "cmd failed at " << i;
        }
        auto h = atm.Submit(cmd);
        ASSERT_TRUE(h.has_value()) << "Submit failed at " << i;
        handles.push_back(*h);
        points.push_back(atm.GetCompletionPoint(*h));
    }

    // Verify state
    EXPECT_EQ(atm.ActiveTaskCount(), static_cast<uint32_t>(kTaskCount));
    EXPECT_EQ(scheduler_.GetCurrentValue(QueueType::AsyncCompute), baseline + kTaskCount);

    // All signal values must be strictly sequential
    for (int i = 0; i < kTaskCount; ++i) {
        EXPECT_EQ(points[i].value, baseline + static_cast<uint64_t>(i + 1))
            << "Signal value at index " << i << " must be baseline+" << (i + 1);
    }

    // Wait only for the LAST task — monotonic timeline guarantees all prior are done
    auto waitResult = atm.WaitForCompletion(handles.back());
    ASSERT_TRUE(waitResult.has_value());

    // GPU must have reached the last signal
    uint64_t gpuVal = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(points.back().semaphore); });
    EXPECT_GE(gpuVal, points.back().value) << "GPU must reach signal " << points.back().value;

    // All tasks must report complete
    for (int i = 0; i < kTaskCount; ++i) {
        EXPECT_TRUE(atm.IsComplete(handles[i])) << "Task " << i << " must be complete";
    }

    atm.Shutdown();
    EXPECT_EQ(atm.ActiveTaskCount(), 0u);
}

// ATM-DEADLOCK-05: Shutdown under load — submit N tasks, immediately Shutdown without any Wait.
// Verifies Shutdown's internal WaitSemaphore for each active task doesn't deadlock or timeout.
// This is the most common real-world deadlock vector: frame tear-down while async work is in-flight.
TEST_P(AsyncTaskManagerTest, Deadlock05_ShutdownUnderFullLoad) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    constexpr int kTaskCount = 16;
    std::vector<TimelineSyncPoint> points;

    for (int i = 0; i < kTaskCount; ++i) {
        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "cmd failed at " << i;
        }
        auto h = atm.Submit(cmd);
        ASSERT_TRUE(h.has_value());
        points.push_back(atm.GetCompletionPoint(*h));
    }
    EXPECT_EQ(atm.ActiveTaskCount(), static_cast<uint32_t>(kTaskCount));

    // Immediately shut down — must NOT hang. Shutdown internally does WaitSemaphore for each task.
    atm.Shutdown();
    EXPECT_EQ(atm.ActiveTaskCount(), 0u) << "All tasks must be cleared after Shutdown";

    // Verify GPU reached all signal values (Shutdown waited for them)
    for (int i = 0; i < kTaskCount; ++i) {
        uint64_t gpuVal = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(points[i].semaphore); });
        EXPECT_GE(gpuVal, points[i].value) << "GPU must have reached signal for task " << i << " after Shutdown";
    }
}

// ATM-DEADLOCK-06: Mixed batch + single submit with cross-queue dependency injection + shutdown.
// Real-world pattern: ATM submits batched compute, Graphics injects a dependency on the result,
// then more compute work is submitted, and finally everything is torn down. Verify no deadlock,
// correct wait-graph state at each step, and clean shutdown.
TEST_P(AsyncTaskManagerTest, Deadlock06_BatchCrossQueueThenShutdown) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    auto asyncComputeBaseline = scheduler_.GetCurrentValue(QueueType::AsyncCompute);

    // Phase 1: Batched compute work (3 sub-batches)
    std::vector<CommandBufferHandle> batch;
    for (int i = 0; i < 3; ++i) {
        auto cmd = RecordEmptyComputeCmd();
        if (!cmd.IsValid()) {
            GTEST_SKIP() << "cmd failed";
        }
        batch.push_back(cmd);
    }
    auto hBatch = atm.SubmitBatched(batch);
    ASSERT_TRUE(hBatch.has_value());
    auto ptBatch = atm.GetCompletionPoint(*hBatch);
    EXPECT_EQ(ptBatch.value, asyncComputeBaseline + 3);

    // Phase 2: Graphics injects dependency on the batch result
    scheduler_.AddDependency(
        QueueType::Graphics, QueueType::AsyncCompute, ptBatch.value, PipelineStage::FragmentShader
    );
    EXPECT_FALSE(scheduler_.DetectDeadlock()) << "Uni-directional G→C must not deadlock";

    // Verify the dependency entry
    auto waits = scheduler_.GetPendingWaits(QueueType::Graphics);
    ASSERT_EQ(waits.size(), 1u);
    EXPECT_EQ(waits[0].value, ptBatch.value);
    EXPECT_EQ(waits[0].stageMask, PipelineStage::FragmentShader);

    // Phase 3: Simulate Graphics submit (consumes the dependency)
    auto gSig = scheduler_.AllocateSignal(QueueType::Graphics);
    scheduler_.CommitSubmit(QueueType::Graphics);
    EXPECT_TRUE(scheduler_.GetPendingWaits(QueueType::Graphics).empty())
        << "Graphics waits must be cleared after commit";

    // Phase 4: More compute work after the dependency was consumed
    auto cmd2 = RecordEmptyComputeCmd();
    if (!cmd2.IsValid()) {
        GTEST_SKIP() << "cmd failed";
    }
    auto hSingle = atm.Submit(cmd2);
    ASSERT_TRUE(hSingle.has_value());
    auto ptSingle = atm.GetCompletionPoint(*hSingle);
    EXPECT_EQ(ptSingle.value, asyncComputeBaseline + 4) << "Single submit after 3-batch must be baseline+4";

    // Phase 5: Reverse-inject — now AsyncCompute waits on Graphics (common for readback fence)
    scheduler_.AddDependency(QueueType::AsyncCompute, QueueType::Graphics, gSig, PipelineStage::AllCommands);
    // gSig was committed → currentValue >= gSig → this edge should be "satisfied"
    // so no deadlock even though we have bidirectional edges (both are satisfied)
    EXPECT_FALSE(scheduler_.DetectDeadlock()) << "Bidirectional edges with all satisfied values must not deadlock";

    // Verify scheduler state
    EXPECT_EQ(scheduler_.GetCurrentValue(QueueType::AsyncCompute), asyncComputeBaseline + 4);
    EXPECT_EQ(atm.ActiveTaskCount(), 2u);

    // Phase 6: Shutdown under this complex state
    atm.Shutdown();
    EXPECT_EQ(atm.ActiveTaskCount(), 0u);

    // GPU must have completed all work
    uint64_t gpuVal = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(ptSingle.semaphore); });
    EXPECT_GE(gpuVal, ptSingle.value);
}

// ATM-DEADLOCK-07: Submit→Shutdown→Submit→Shutdown rapid cycle stress.
// Verifies that alternating submit-shutdown cycles don't leave stale state in the scheduler
// or accumulate leaked semaphore values.
TEST_P(AsyncTaskManagerTest, Deadlock07_RapidSubmitShutdownCycles) {
    auto caps = Caps();
    if (!caps.hasAsyncCompute) {
        GTEST_SKIP() << "No async compute queue";
    }

    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());
    auto& atm = *result;

    constexpr int kCycles = 8;
    uint64_t prevSignalValue = 0;

    for (int cycle = 0; cycle < kCycles; ++cycle) {
        // Submit a batch of (cycle%3 + 1) tasks
        int batchSize = (cycle % 3) + 1;
        std::vector<CommandBufferHandle> cmds;
        for (int i = 0; i < batchSize; ++i) {
            auto cmd = RecordEmptyComputeCmd();
            if (!cmd.IsValid()) {
                GTEST_SKIP() << "cmd failed at cycle " << cycle;
            }
            cmds.push_back(cmd);
        }

        AsyncTaskHandle lastHandle;
        if (batchSize == 1) {
            auto h = atm.Submit(cmds[0]);
            ASSERT_TRUE(h.has_value()) << "Submit failed at cycle " << cycle;
            lastHandle = *h;
        } else {
            auto h = atm.SubmitBatched(cmds);
            ASSERT_TRUE(h.has_value()) << "SubmitBatched failed at cycle " << cycle;
            lastHandle = *h;
        }

        auto pt = atm.GetCompletionPoint(lastHandle);
        EXPECT_GT(pt.value, prevSignalValue)
            << "Signal value must be strictly monotonic across shutdown cycles at cycle " << cycle;
        prevSignalValue = pt.value;

        // Verify task count before shutdown
        EXPECT_GE(atm.ActiveTaskCount(), 1u);

        // Immediate shutdown — must not hang
        atm.Shutdown();
        EXPECT_EQ(atm.ActiveTaskCount(), 0u) << "ActiveTaskCount must be 0 after Shutdown at cycle " << cycle;

        // Verify GPU reached the signal
        uint64_t gpuVal = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(pt.semaphore); });
        EXPECT_GE(gpuVal, pt.value) << "GPU must reach signal after Shutdown at cycle " << cycle;
    }

    // Scheduler's Compute timeline must have advanced by the sum of all batch sizes
    // Sizes: 1,2,3,1,2,3,1,2 = 15
    int expectedTotal = 0;
    for (int c = 0; c < kCycles; ++c) {
        expectedTotal += (c % 3) + 1;
    }
    EXPECT_EQ(scheduler_.GetCurrentValue(QueueType::AsyncCompute), static_cast<uint64_t>(expectedTotal));
}

// ============================================================================
// Parameterized instantiation — all backends (will skip on Mock / non-T1)
// ============================================================================

INSTANTIATE_TEST_SUITE_P(AllBackends, AsyncTaskManagerTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);
