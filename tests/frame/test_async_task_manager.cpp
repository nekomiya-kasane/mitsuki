// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// AsyncTaskManager unit tests — specs/03-sync.md §5.6
// Tests verify async compute task submission, polling, batched submit,
// and shutdown across Tier1 backends (Vulkan, D3D12).
// BDD style: GIVEN / WHEN / THEN.

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>

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

    CommandPoolHandle computePool_{};

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
    EXPECT_FALSE(result.has_value());
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
    EXPECT_TRUE(waitResult.has_value());
    EXPECT_TRUE(atm.IsComplete(*handle));

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
}

// ============================================================================
// §5.6.3 ATM-BATCH — SubmitBatched
// ============================================================================

// ATM-BATCH-01: GIVEN ATM WHEN SubmitBatched(empty) THEN error
TEST_P(AsyncTaskManagerTest, Batch01_EmptyBatchError) {
    auto result = MakeATM();
    ASSERT_TRUE(result.has_value());

    auto handle = result->SubmitBatched({});
    EXPECT_FALSE(handle.has_value());
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

    atm.Shutdown();
}

// ATM-BATCH-03: GIVEN batched task WHEN WaitForCompletion THEN complete
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

    auto waitResult = atm.WaitForCompletion(*handle);
    EXPECT_TRUE(waitResult.has_value());
    EXPECT_TRUE(atm.IsComplete(*handle));

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
// Parameterized instantiation — all backends (will skip on Mock / non-T1)
// ============================================================================

INSTANTIATE_TEST_SUITE_P(AllBackends, AsyncTaskManagerTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);
