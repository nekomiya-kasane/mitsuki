// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// SyncScheduler unit tests — specs/03-sync.md §13
// Pure CPU logic tests — no GPU device required.
// BDD style: GIVEN / WHEN / THEN.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "miki/frame/SyncScheduler.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/Sync.h"

using namespace miki;
using namespace miki::frame;
using namespace miki::rhi;

// ============================================================================
// Helper: create fake semaphore handles for testing
// ============================================================================

static auto MakeFakeSemaphore(uint64_t id) -> SemaphoreHandle {
    SemaphoreHandle h;
    h.value = id;
    return h;
}

static auto MakeTimelines() -> QueueTimelines {
    return {
        .graphics = MakeFakeSemaphore(100),
        .compute = MakeFakeSemaphore(200),
        .transfer = MakeFakeSemaphore(300),
    };
}

// ============================================================================
// §13.1 SS-INIT — Initialization
// ============================================================================

// SS-INIT-01: GIVEN default SyncScheduler WHEN Init(timelines) THEN semaphores are stored correctly
TEST(SyncSchedulerTest, Init01_SemaphoresStored) {
    SyncScheduler sched;
    auto tl = MakeTimelines();
    sched.Init(tl);

    EXPECT_EQ(sched.GetSemaphore(QueueType::Graphics).value, 100u);
    EXPECT_EQ(sched.GetSemaphore(QueueType::Compute).value, 200u);
    EXPECT_EQ(sched.GetSemaphore(QueueType::Transfer).value, 300u);
}

// SS-INIT-02: GIVEN Init'd scheduler WHEN GetCurrentValue THEN all queues start at 0
TEST(SyncSchedulerTest, Init02_CurrentValuesZero) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), 0u);
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Compute), 0u);
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Transfer), 0u);
}

// SS-INIT-03: GIVEN Init'd scheduler WHEN GetPendingWaits THEN empty for all queues
TEST(SyncSchedulerTest, Init03_NoPendingWaits) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    EXPECT_TRUE(sched.GetPendingWaits(QueueType::Graphics).empty());
    EXPECT_TRUE(sched.GetPendingWaits(QueueType::Compute).empty());
    EXPECT_TRUE(sched.GetPendingWaits(QueueType::Transfer).empty());
}

// ============================================================================
// §13.2 SS-ALLOC — AllocateSignal
// ============================================================================

// SS-ALLOC-01: GIVEN Init'd scheduler WHEN AllocateSignal(Graphics) x3 THEN returns 1,2,3 (monotonic)
TEST(SyncSchedulerTest, Alloc01_Monotonic) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    EXPECT_EQ(sched.AllocateSignal(QueueType::Graphics), 1u);
    EXPECT_EQ(sched.AllocateSignal(QueueType::Graphics), 2u);
    EXPECT_EQ(sched.AllocateSignal(QueueType::Graphics), 3u);
}

// SS-ALLOC-02: GIVEN Init'd scheduler WHEN AllocateSignal on different queues THEN independent counters
TEST(SyncSchedulerTest, Alloc02_IndependentQueues) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    EXPECT_EQ(sched.AllocateSignal(QueueType::Graphics), 1u);
    EXPECT_EQ(sched.AllocateSignal(QueueType::Compute), 1u);
    EXPECT_EQ(sched.AllocateSignal(QueueType::Transfer), 1u);

    EXPECT_EQ(sched.AllocateSignal(QueueType::Graphics), 2u);
    EXPECT_EQ(sched.AllocateSignal(QueueType::Compute), 2u);
}

// SS-ALLOC-03: GIVEN AllocateSignal(Graphics) returned 5 WHEN GetSignalValue(Graphics) THEN returns 5
TEST(SyncSchedulerTest, Alloc03_GetSignalValue) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    for (int i = 0; i < 5; ++i) {
        sched.AllocateSignal(QueueType::Graphics);
    }
    EXPECT_EQ(sched.GetSignalValue(QueueType::Graphics), 5u);
}

// ============================================================================
// §13.3 SS-DEP — AddDependency / GetPendingWaits
// ============================================================================

// SS-DEP-01: GIVEN scheduler WHEN AddDependency(Graphics waits Compute@3) THEN GetPendingWaits(Graphics) has 1 entry
TEST(SyncSchedulerTest, Dep01_SingleDependency) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    sched.AddDependency(QueueType::Graphics, QueueType::Compute, 3, PipelineStage::ComputeShader);

    auto waits = sched.GetPendingWaits(QueueType::Graphics);
    ASSERT_EQ(waits.size(), 1u);
    EXPECT_EQ(waits[0].semaphore.value, 200u);  // Compute semaphore
    EXPECT_EQ(waits[0].value, 3u);
    EXPECT_EQ(waits[0].stageMask, PipelineStage::ComputeShader);
}

// SS-DEP-02: GIVEN scheduler WHEN AddDependency twice for same waiter THEN GetPendingWaits has 2 entries
TEST(SyncSchedulerTest, Dep02_MultipleDependencies) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    sched.AddDependency(QueueType::Graphics, QueueType::Compute, 1, PipelineStage::ComputeShader);
    sched.AddDependency(QueueType::Graphics, QueueType::Transfer, 2, PipelineStage::Transfer);

    auto waits = sched.GetPendingWaits(QueueType::Graphics);
    ASSERT_EQ(waits.size(), 2u);
    EXPECT_EQ(waits[0].semaphore.value, 200u);  // Compute
    EXPECT_EQ(waits[1].semaphore.value, 300u);  // Transfer
}

// SS-DEP-03: GIVEN dependency on Graphics WHEN check Compute pending THEN Compute is empty
TEST(SyncSchedulerTest, Dep03_DependencyIsolation) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    sched.AddDependency(QueueType::Graphics, QueueType::Compute, 1, PipelineStage::AllCommands);

    EXPECT_EQ(sched.GetPendingWaits(QueueType::Graphics).size(), 1u);
    EXPECT_TRUE(sched.GetPendingWaits(QueueType::Compute).empty());
    EXPECT_TRUE(sched.GetPendingWaits(QueueType::Transfer).empty());
}

// ============================================================================
// §13.4 SS-COMMIT — CommitSubmit
// ============================================================================

// SS-COMMIT-01: GIVEN AllocateSignal(G)=3, CommitSubmit(G) WHEN GetCurrentValue(G) THEN 3
TEST(SyncSchedulerTest, Commit01_UpdatesCurrentValue) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    sched.AllocateSignal(QueueType::Graphics);
    sched.AllocateSignal(QueueType::Graphics);
    sched.AllocateSignal(QueueType::Graphics);
    sched.CommitSubmit(QueueType::Graphics);

    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), 3u);
}

// SS-COMMIT-02: GIVEN pending waits WHEN CommitSubmit THEN waits are cleared
TEST(SyncSchedulerTest, Commit02_ClearsPendingWaits) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    sched.AddDependency(QueueType::Graphics, QueueType::Compute, 1, PipelineStage::AllCommands);
    EXPECT_EQ(sched.GetPendingWaits(QueueType::Graphics).size(), 1u);

    sched.AllocateSignal(QueueType::Graphics);
    sched.CommitSubmit(QueueType::Graphics);

    EXPECT_TRUE(sched.GetPendingWaits(QueueType::Graphics).empty());
}

// SS-COMMIT-03: GIVEN CommitSubmit(G) WHEN CommitSubmit does not affect Compute THEN Compute unchanged
TEST(SyncSchedulerTest, Commit03_QueueIsolation) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    sched.AllocateSignal(QueueType::Graphics);
    sched.AllocateSignal(QueueType::Compute);
    sched.AddDependency(QueueType::Compute, QueueType::Graphics, 1, PipelineStage::AllCommands);

    sched.CommitSubmit(QueueType::Graphics);

    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), 1u);
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Compute), 0u);  // Not committed yet
    EXPECT_EQ(sched.GetPendingWaits(QueueType::Compute).size(), 1u);  // Still pending
}

// ============================================================================
// §13.5 SS-RESET — Reset
// ============================================================================

// SS-RESET-01: GIVEN scheduler with state WHEN Reset THEN all counters back to initial
TEST(SyncSchedulerTest, Reset01_ClearsAll) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    sched.AllocateSignal(QueueType::Graphics);
    sched.AllocateSignal(QueueType::Compute);
    sched.CommitSubmit(QueueType::Graphics);
    sched.AddDependency(QueueType::Transfer, QueueType::Graphics, 1, PipelineStage::AllCommands);

    sched.Reset();

    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), 0u);
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Compute), 0u);
    EXPECT_TRUE(sched.GetPendingWaits(QueueType::Transfer).empty());

    // AllocateSignal should restart from 1
    EXPECT_EQ(sched.AllocateSignal(QueueType::Graphics), 1u);
}

// ============================================================================
// §13.6 SS-DIAG — Diagnostic output
// ============================================================================

// SS-DIAG-01: GIVEN scheduler with state WHEN DumpWaitGraph THEN no crash
TEST(SyncSchedulerTest, Diag01_DumpNoCrash) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    sched.AllocateSignal(QueueType::Graphics);
    sched.AddDependency(QueueType::Compute, QueueType::Graphics, 1, PipelineStage::AllCommands);

    sched.DumpWaitGraph(stderr);
    SUCCEED();
}

// SS-DIAG-02: GIVEN scheduler WHEN ExportWaitGraphDOT THEN valid DOT string
TEST(SyncSchedulerTest, Diag02_DOTExport) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    sched.AllocateSignal(QueueType::Graphics);
    sched.AddDependency(QueueType::Compute, QueueType::Graphics, 1, PipelineStage::AllCommands);

    std::string dot;
    sched.ExportWaitGraphDOT(dot);
    EXPECT_FALSE(dot.empty());
    EXPECT_NE(dot.find("digraph"), std::string::npos);
    EXPECT_NE(dot.find("WaitGraph"), std::string::npos);
}

// SS-DIAG-03: GIVEN scheduler WHEN ExportWaitGraphJSON THEN valid JSON-like string
TEST(SyncSchedulerTest, Diag03_JSONExport) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    sched.AllocateSignal(QueueType::Graphics);

    std::string json;
    sched.ExportWaitGraphJSON(json);
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("\"queues\""), std::string::npos);
}

// ============================================================================
// §13.7 SS-DEADLOCK — Deadlock Detection
// ============================================================================

// SS-DEADLOCK-01: GIVEN no dependencies WHEN DetectDeadlock THEN false
TEST(SyncSchedulerTest, Deadlock01_NoCycle) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    EXPECT_FALSE(sched.DetectDeadlock());
}

// SS-DEADLOCK-02: GIVEN linear chain G->C->T WHEN DetectDeadlock THEN false
TEST(SyncSchedulerTest, Deadlock02_LinearChainNoCycle) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    sched.AllocateSignal(QueueType::Graphics);
    sched.AllocateSignal(QueueType::Compute);
    // Compute waits on Graphics@1 (Graphics hasn't committed yet → edge exists)
    sched.AddDependency(QueueType::Compute, QueueType::Graphics, 1, PipelineStage::AllCommands);
    // Transfer waits on Compute@1 (Compute hasn't committed yet → edge exists)
    sched.AddDependency(QueueType::Transfer, QueueType::Compute, 1, PipelineStage::AllCommands);

    EXPECT_FALSE(sched.DetectDeadlock());
}

// SS-DEADLOCK-03: GIVEN cycle G->C->G WHEN DetectDeadlock THEN true
TEST(SyncSchedulerTest, Deadlock03_TwoNodeCycle) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    sched.AllocateSignal(QueueType::Graphics);
    sched.AllocateSignal(QueueType::Compute);
    // Graphics waits on Compute@1
    sched.AddDependency(QueueType::Graphics, QueueType::Compute, 1, PipelineStage::AllCommands);
    // Compute waits on Graphics@1
    sched.AddDependency(QueueType::Compute, QueueType::Graphics, 1, PipelineStage::AllCommands);

    EXPECT_TRUE(sched.DetectDeadlock());
}

// SS-DEADLOCK-04: GIVEN 3-node cycle G->C->T->G WHEN DetectDeadlock THEN true
TEST(SyncSchedulerTest, Deadlock04_ThreeNodeCycle) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    sched.AllocateSignal(QueueType::Graphics);
    sched.AllocateSignal(QueueType::Compute);
    sched.AllocateSignal(QueueType::Transfer);

    sched.AddDependency(QueueType::Compute, QueueType::Graphics, 1, PipelineStage::AllCommands);
    sched.AddDependency(QueueType::Transfer, QueueType::Compute, 1, PipelineStage::AllCommands);
    sched.AddDependency(QueueType::Graphics, QueueType::Transfer, 1, PipelineStage::AllCommands);

    EXPECT_TRUE(sched.DetectDeadlock());
}

// SS-DEADLOCK-05: GIVEN dependency on already-committed value WHEN DetectDeadlock THEN false (no edge)
TEST(SyncSchedulerTest, Deadlock05_CommittedValueNoEdge) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    sched.AllocateSignal(QueueType::Graphics);
    sched.CommitSubmit(QueueType::Graphics);  // Graphics currentValue = 1

    // Compute waits on Graphics@1 — but Graphics already committed 1, so no edge
    sched.AddDependency(QueueType::Compute, QueueType::Graphics, 1, PipelineStage::AllCommands);
    // Graphics waits on Compute@1
    sched.AllocateSignal(QueueType::Compute);
    sched.AddDependency(QueueType::Graphics, QueueType::Compute, 1, PipelineStage::AllCommands);

    EXPECT_FALSE(sched.DetectDeadlock());
}

// ============================================================================
// §13.8 SS-MOVE — Move Semantics
// ============================================================================

// SS-MOVE-01: GIVEN scheduler with state WHEN move-constructed THEN dest has state
TEST(SyncSchedulerTest, Move01_MoveConstruct) {
    SyncScheduler a;
    a.Init(MakeTimelines());
    a.AllocateSignal(QueueType::Graphics);

    SyncScheduler b = std::move(a);
    EXPECT_EQ(b.GetSemaphore(QueueType::Graphics).value, 100u);
    EXPECT_EQ(b.GetSignalValue(QueueType::Graphics), 1u);
}

// SS-MOVE-02: GIVEN scheduler WHEN move-assigned THEN dest has state
TEST(SyncSchedulerTest, Move02_MoveAssign) {
    SyncScheduler a;
    a.Init(MakeTimelines());
    a.AllocateSignal(QueueType::Graphics);
    a.AllocateSignal(QueueType::Graphics);

    SyncScheduler b;
    b = std::move(a);
    EXPECT_EQ(b.GetSignalValue(QueueType::Graphics), 2u);
}

// ============================================================================
// §13.9 SS-STRESS — Multi-submit scenario
// ============================================================================

// SS-STRESS-01: GIVEN 1000 AllocateSignal + CommitSubmit cycles THEN counters are correct
TEST(SyncSchedulerTest, Stress01_ThousandSubmits) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    for (uint64_t i = 1; i <= 1000; ++i) {
        auto v = sched.AllocateSignal(QueueType::Graphics);
        ASSERT_EQ(v, i);
        sched.CommitSubmit(QueueType::Graphics);
        ASSERT_EQ(sched.GetCurrentValue(QueueType::Graphics), i);
    }
}

// SS-STRESS-02: GIVEN interleaved multi-queue submits THEN each queue tracks independently
TEST(SyncSchedulerTest, Stress02_InterleavedQueues) {
    SyncScheduler sched;
    sched.Init(MakeTimelines());

    for (uint64_t i = 1; i <= 100; ++i) {
        sched.AllocateSignal(QueueType::Graphics);
        sched.CommitSubmit(QueueType::Graphics);

        if (i % 2 == 0) {
            sched.AllocateSignal(QueueType::Compute);
            sched.CommitSubmit(QueueType::Compute);
        }
        if (i % 5 == 0) {
            sched.AllocateSignal(QueueType::Transfer);
            sched.CommitSubmit(QueueType::Transfer);
        }
    }

    EXPECT_EQ(sched.GetCurrentValue(QueueType::Graphics), 100u);
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Compute), 50u);
    EXPECT_EQ(sched.GetCurrentValue(QueueType::Transfer), 20u);
}
