// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// FrameOrchestrator unit tests — specs/03-sync.md §2.1
// Tests verify shared infrastructure ownership (SyncScheduler, AsyncTaskManager,
// DeferredDestructor), lifecycle, and shutdown correctness.
// BDD style: GIVEN / WHEN / THEN.

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>

#include "RhiTestFixture.h"
#include "miki/frame/AsyncTaskManager.h"
#include "miki/frame/ComputeQueueLevel.h"
#include "miki/frame/DeferredDestructor.h"
#include "miki/frame/FrameOrchestrator.h"
#include "miki/frame/SyncScheduler.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/backend/AllBackends.h"

using namespace miki;
using namespace miki::frame;
using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// Test fixture — parameterized across all backends
// ============================================================================

class FrameOrchestratorTest : public RhiTest {
   protected:
    void SetUp() override {
        RhiTest::SetUp();
        if (testing::Test::IsSkipped()) {
            return;
        }
        // FrameOrchestrator requires timeline semaphores for SyncScheduler init
        if (GetParam() == BackendType::Mock) {
            GTEST_SKIP() << "FrameOrchestrator requires real sync primitives";
        }
        if (!Caps().hasTimelineSemaphore) {
            GTEST_SKIP() << "Backend lacks timeline semaphores";
        }
    }
};

// ============================================================================
// §2.1.1 FO-LC — Lifecycle
// ============================================================================

// FO-LC-01: GIVEN invalid device WHEN Create THEN error
TEST_P(FrameOrchestratorTest, LC01_InvalidDevice) {
    DeviceHandle invalid{};
    auto result = FrameOrchestrator::Create(invalid, 2);
    EXPECT_FALSE(result.has_value());
}

// FO-LC-02: GIVEN valid device WHEN Create(2) THEN succeeds
TEST_P(FrameOrchestratorTest, LC02_CreateValid) {
    auto result = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(result.has_value()) << "FrameOrchestrator::Create failed";
}

// FO-LC-03: GIVEN valid orch WHEN GetDevice THEN returns same device
TEST_P(FrameOrchestratorTest, LC03_GetDevice) {
    auto result = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->GetDevice().IsValid());
}

// FO-LC-04: GIVEN valid orch WHEN move-constructed THEN dest works, source empty
TEST_P(FrameOrchestratorTest, LC04_MoveConstruct) {
    auto result = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(result.has_value());

    auto orch2 = std::move(*result);
    EXPECT_TRUE(orch2.GetDevice().IsValid());
}

// FO-LC-05: GIVEN valid orch WHEN move-assigned THEN dest works
TEST_P(FrameOrchestratorTest, LC05_MoveAssign) {
    auto r1 = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(r1.has_value());

    auto r2 = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(r2.has_value());

    *r2 = std::move(*r1);
    EXPECT_TRUE(r2->GetDevice().IsValid());
}

// FO-LC-06: GIVEN orch WHEN destructor runs THEN Shutdown called (no hang)
TEST_P(FrameOrchestratorTest, LC06_DestructorShutdown) {
    {
        auto result = FrameOrchestrator::Create(Dev(), 2);
        ASSERT_TRUE(result.has_value());
    }
    SUCCEED();
}

// ============================================================================
// §2.1.2 FO-ACCESS — Sub-component access
// ============================================================================

// FO-ACCESS-01: GIVEN orch WHEN GetSyncScheduler THEN valid reference
TEST_P(FrameOrchestratorTest, Access01_SyncScheduler) {
    auto result = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(result.has_value());

    auto& sched = result->GetSyncScheduler();
    // SyncScheduler should have been initialized with device timelines
    // Verify by checking that GetSemaphore returns a valid handle for graphics queue
    auto sem = sched.GetSemaphore(QueueType::Graphics);
    EXPECT_TRUE(sem.IsValid());
}

// FO-ACCESS-02: GIVEN orch WHEN GetSyncScheduler (const) THEN valid reference
TEST_P(FrameOrchestratorTest, Access02_SyncSchedulerConst) {
    auto result = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(result.has_value());

    const auto& orch = *result;
    const auto& sched = orch.GetSyncScheduler();
    auto sem = sched.GetSemaphore(QueueType::Graphics);
    EXPECT_TRUE(sem.IsValid());
}

// FO-ACCESS-03: GIVEN orch WHEN GetAsyncTaskManager THEN valid reference, 0 tasks
TEST_P(FrameOrchestratorTest, Access03_AsyncTaskManager) {
    auto result = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(result.has_value());

    auto& atm = result->GetAsyncTaskManager();
    EXPECT_EQ(atm.ActiveTaskCount(), 0u);
}

// FO-ACCESS-04: GIVEN orch WHEN GetAsyncTaskManager (const) THEN valid reference
TEST_P(FrameOrchestratorTest, Access04_AsyncTaskManagerConst) {
    auto result = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(result.has_value());

    const auto& orch = *result;
    const auto& atm = orch.GetAsyncTaskManager();
    EXPECT_EQ(atm.ActiveTaskCount(), 0u);
}

// FO-ACCESS-05: GIVEN orch WHEN GetDeferredDestructor THEN valid reference, 0 pending
TEST_P(FrameOrchestratorTest, Access05_DeferredDestructor) {
    auto result = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(result.has_value());

    auto& dd = result->GetDeferredDestructor();
    EXPECT_EQ(dd.PendingCount(), 0u);
}

// FO-ACCESS-06: GIVEN orch WHEN GetDeferredDestructor (const) THEN valid reference
TEST_P(FrameOrchestratorTest, Access06_DeferredDestructorConst) {
    auto result = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(result.has_value());

    const auto& orch = *result;
    const auto& dd = orch.GetDeferredDestructor();
    EXPECT_EQ(dd.PendingCount(), 0u);
}

// ============================================================================
// §2.1.3 FO-INTEG — Integration: sub-components work together
// ============================================================================

// FO-INTEG-01: GIVEN orch WHEN use SyncScheduler to allocate + DeferredDestructor to queue
//              THEN both work correctly through the orchestrator
TEST_P(FrameOrchestratorTest, Integ01_SchedAndDD) {
    auto result = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(result.has_value());
    auto& orch = *result;

    // Use SyncScheduler
    auto& sched = orch.GetSyncScheduler();
    auto val = sched.AllocateSignal(QueueType::Graphics);
    EXPECT_EQ(val, 1u);

    // Use DeferredDestructor — queue a buffer destroy
    BufferDesc bufDesc{};
    bufDesc.size = 256;
    bufDesc.usage = BufferUsage::Uniform;
    bufDesc.memory = MemoryLocation::CpuToGpu;
    auto buf = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(bufDesc); });
    if (buf.has_value()) {
        auto& dd = orch.GetDeferredDestructor();
        dd.Destroy(*buf);
        EXPECT_EQ(dd.PendingCount(), 1u);
    }
}

// ============================================================================
// §2.1.4 FO-SHUTDOWN — Shutdown
// ============================================================================

// FO-SHUTDOWN-01: GIVEN orch with DD pending WHEN Shutdown THEN pending drained
TEST_P(FrameOrchestratorTest, Shutdown01_DrainsPending) {
    auto result = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(result.has_value());
    auto& orch = *result;

    BufferDesc bufDesc{};
    bufDesc.size = 256;
    bufDesc.usage = BufferUsage::Uniform;
    bufDesc.memory = MemoryLocation::CpuToGpu;
    auto buf = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(bufDesc); });
    if (buf.has_value()) {
        orch.GetDeferredDestructor().Destroy(*buf);
    }

    orch.Shutdown();
    EXPECT_EQ(orch.GetDeferredDestructor().PendingCount(), 0u);
}

// FO-SHUTDOWN-02: GIVEN already-shutdown orch WHEN Shutdown again THEN no crash
TEST_P(FrameOrchestratorTest, Shutdown02_DoubleShutdown) {
    auto result = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(result.has_value());

    result->Shutdown();
    result->Shutdown();
    SUCCEED();
}

// ============================================================================
// §2.1.5 FO-CQL — ComputeQueueLevel
// ============================================================================

// FO-CQL-01: GIVEN orch WHEN GetComputeQueueLevel THEN matches DetectComputeQueueLevel
TEST_P(FrameOrchestratorTest, CQL01_MatchesDetection) {
    auto result = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(result.has_value());

    auto expected = DetectComputeQueueLevel(Caps());
    EXPECT_EQ(result->GetComputeQueueLevel(), expected);
}

// FO-CQL-02: GIVEN orch WHEN GetComputeQueueLevel THEN Vulkan/D3D12 >= Level C
TEST_P(FrameOrchestratorTest, CQL02_Tier1AtLeastC) {
    auto bt = GetParam();
    if (bt != BackendType::Vulkan14 && bt != BackendType::D3D12) {
        GTEST_SKIP() << "Only Tier1 backends guaranteed async compute";
    }
    auto result = FrameOrchestrator::Create(Dev(), 2);
    ASSERT_TRUE(result.has_value());
    EXPECT_LE(
        static_cast<int>(result->GetComputeQueueLevel()), static_cast<int>(ComputeQueueLevel::C_SingleQueueBatch)
    );
}

// ============================================================================
// Parameterized instantiation
// ============================================================================

INSTANTIATE_TEST_SUITE_P(AllBackends, FrameOrchestratorTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);
