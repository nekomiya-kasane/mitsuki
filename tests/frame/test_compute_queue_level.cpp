// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// ComputeQueueLevel unit tests -- specs/03-sync.md SS5.8.2
// Pure CPU logic tests -- no GPU device required.
// BDD style: GIVEN / WHEN / THEN.

#include <gtest/gtest.h>

#include "miki/frame/ComputeQueueLevel.h"
#include "miki/rhi/GpuCapabilityProfile.h"

using namespace miki;
using namespace miki::frame;
using namespace miki::rhi;

// ============================================================================
// Helper: build a GpuCapabilityProfile with compute-related fields
// ============================================================================

static auto MakeCaps(uint32_t iComputeFamilyCount, bool iHasGlobalPriority, bool iHasAsyncCompute)
    -> GpuCapabilityProfile {
    GpuCapabilityProfile caps{};
    caps.computeQueueFamilyCount = iComputeFamilyCount;
    caps.hasGlobalPriority = iHasGlobalPriority;
    caps.hasAsyncCompute = iHasAsyncCompute;
    return caps;
}

// ============================================================================
// SS5.8.2.1 CQL-DET -- Detection logic
// ============================================================================

// CQL-DET-01: GIVEN 2+ compute families + global priority WHEN detect THEN Level A
TEST(ComputeQueueLevelTest, Det01_DualQueuePriority) {
    auto caps = MakeCaps(2, true, true);
    EXPECT_EQ(DetectComputeQueueLevel(caps), ComputeQueueLevel::A_DualQueuePriority);
}

// CQL-DET-02: GIVEN 1 compute family + global priority + async compute WHEN detect THEN Level B
TEST(ComputeQueueLevelTest, Det02_SingleQueuePriority) {
    auto caps = MakeCaps(1, true, true);
    EXPECT_EQ(DetectComputeQueueLevel(caps), ComputeQueueLevel::B_SingleQueuePriority);
}

// CQL-DET-03: GIVEN 1 compute family + no priority + async compute WHEN detect THEN Level C
TEST(ComputeQueueLevelTest, Det03_SingleQueueBatch) {
    auto caps = MakeCaps(1, false, true);
    EXPECT_EQ(DetectComputeQueueLevel(caps), ComputeQueueLevel::C_SingleQueueBatch);
}

// CQL-DET-04: GIVEN no compute family + no async WHEN detect THEN Level D
TEST(ComputeQueueLevelTest, Det04_GraphicsOnly) {
    auto caps = MakeCaps(0, false, false);
    EXPECT_EQ(DetectComputeQueueLevel(caps), ComputeQueueLevel::D_GraphicsOnly);
}

// CQL-DET-05: GIVEN 3 compute families + no priority + async compute WHEN detect THEN Level C
// (multiple families but no priority -> cannot isolate by priority, falls to C)
TEST(ComputeQueueLevelTest, Det05_MultiFamilyNoPriority) {
    auto caps = MakeCaps(3, false, true);
    EXPECT_EQ(DetectComputeQueueLevel(caps), ComputeQueueLevel::C_SingleQueueBatch);
}

// CQL-DET-06: GIVEN 0 compute families + global priority + no async WHEN detect THEN Level D
// (priority available but no compute queue -> D)
TEST(ComputeQueueLevelTest, Det06_PriorityButNoCompute) {
    auto caps = MakeCaps(0, true, false);
    EXPECT_EQ(DetectComputeQueueLevel(caps), ComputeQueueLevel::D_GraphicsOnly);
}

// CQL-DET-07: GIVEN 2+ families + priority but hasAsyncCompute=false WHEN detect THEN Level D
// (edge case: families exist but hasAsyncCompute not set -> D)
TEST(ComputeQueueLevelTest, Det07_FamiliesButNoAsyncFlag) {
    auto caps = MakeCaps(2, true, false);
    EXPECT_EQ(DetectComputeQueueLevel(caps), ComputeQueueLevel::D_GraphicsOnly);
}

// CQL-DET-08: GIVEN 5 compute families + priority + async WHEN detect THEN Level A
// (many families -> still A)
TEST(ComputeQueueLevelTest, Det08_ManyFamilies) {
    auto caps = MakeCaps(5, true, true);
    EXPECT_EQ(DetectComputeQueueLevel(caps), ComputeQueueLevel::A_DualQueuePriority);
}

// ============================================================================
// SS5.8.2.2 CQL-NAME -- Human-readable names
// ============================================================================

// CQL-NAME-01: GIVEN all 4 levels WHEN ComputeQueueLevelName THEN non-null, non-"Unknown"
TEST(ComputeQueueLevelTest, Name01_AllLevelsNamed) {
    const ComputeQueueLevel levels[] = {
        ComputeQueueLevel::A_DualQueuePriority,
        ComputeQueueLevel::B_SingleQueuePriority,
        ComputeQueueLevel::C_SingleQueueBatch,
        ComputeQueueLevel::D_GraphicsOnly,
    };
    for (auto lvl : levels) {
        const char* name = ComputeQueueLevelName(lvl);
        ASSERT_NE(name, nullptr);
        EXPECT_STRNE(name, "Unknown") << "Level " << static_cast<int>(lvl) << " has unknown name";
    }
}

// ============================================================================
// SS5.8.2.3 CQL-QFOT -- QFOT helper barrier descriptors
// ============================================================================

// CQL-QFOT-01: GIVEN MakeQFOTRelease(Compute, Graphics, ShaderWrite) THEN correct fields
TEST(ComputeQueueLevelTest, QFOT01_ReleaseBarrier) {
    auto barrier = MakeQFOTRelease(QueueType::Compute, QueueType::Graphics, AccessFlags::ShaderWrite);
    EXPECT_EQ(barrier.srcQueue, QueueType::Compute);
    EXPECT_EQ(barrier.dstQueue, QueueType::Graphics);
    EXPECT_EQ(barrier.srcAccess, AccessFlags::ShaderWrite);
    EXPECT_EQ(barrier.dstAccess, AccessFlags::None);
}

// CQL-QFOT-02: GIVEN MakeQFOTAcquire(Compute, Graphics, ShaderRead) THEN correct fields
TEST(ComputeQueueLevelTest, QFOT02_AcquireBarrier) {
    auto barrier = MakeQFOTAcquire(QueueType::Compute, QueueType::Graphics, AccessFlags::ShaderRead);
    EXPECT_EQ(barrier.srcQueue, QueueType::Compute);
    EXPECT_EQ(barrier.dstQueue, QueueType::Graphics);
    EXPECT_EQ(barrier.srcAccess, AccessFlags::None);
    EXPECT_EQ(barrier.dstAccess, AccessFlags::ShaderRead);
}

// CQL-QFOT-03: GIVEN same src/dst queue WHEN MakeQFOTRelease THEN srcQueue == dstQueue (no-op QFOT)
TEST(ComputeQueueLevelTest, QFOT03_SameQueueNoOp) {
    auto barrier = MakeQFOTRelease(QueueType::Graphics, QueueType::Graphics, AccessFlags::ShaderWrite);
    EXPECT_EQ(barrier.srcQueue, QueueType::Graphics);
    EXPECT_EQ(barrier.dstQueue, QueueType::Graphics);
}
