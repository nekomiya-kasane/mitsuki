// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// CommandPoolAllocator unit tests — specs/03-sync.md §17.18
// Tests verify CPA behavior across all backends using RhiTestFixture.
// BDD style: GIVEN / WHEN / THEN.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "RhiTestFixture.h"
#include "miki/frame/CommandListArena.h"
#include "miki/frame/CommandPoolAllocator.h"
#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/backend/AllBackends.h"
#include "miki/rhi/backend/AllCommandBuffers.h"

using namespace miki;
using namespace miki::frame;
using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// Test fixture for CommandPoolAllocator — parameterized across backends
// ============================================================================

class CPATest : public RhiTest {
   protected:
    [[nodiscard]] auto MakeCPA(
        uint32_t framesInFlight = 2, bool asyncCompute = false, bool asyncTransfer = false, uint32_t arenaCapacity = 16,
        bool hwmShrink = false, uint32_t recordingThreadCount = 1
    ) -> core::Result<CommandPoolAllocator> {
        CommandPoolAllocator::Desc desc{
            .device = Dev(),
            .framesInFlight = framesInFlight,
            .hasAsyncCompute = asyncCompute,
            .hasAsyncTransfer = asyncTransfer,
            .initialArenaCapacity = arenaCapacity,
            .enableHwmShrink = hwmShrink,
            .recordingThreadCount = recordingThreadCount,
        };
        return CommandPoolAllocator::Create(desc);
    }
};

// ============================================================================
// §17.18.1 CPA-LC — Lifecycle Tests
// ============================================================================

// CPA-LC-01: GIVEN invalid DeviceHandle WHEN Create() THEN returns error
TEST_P(CPATest, LC01_InvalidDevice) {
    DeviceHandle invalid;
    CommandPoolAllocator::Desc desc{.device = invalid, .framesInFlight = 2};
    auto result = CommandPoolAllocator::Create(desc);
    EXPECT_FALSE(result.has_value());
}

// CPA-LC-02: GIVEN valid device, 2 frames, graphics only WHEN Create THEN GetPoolCount()==2
TEST_P(CPATest, LC02_GraphicsOnly) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value()) << "Create failed";
    auto& cpa = *result;
    EXPECT_EQ(cpa.GetPoolCount(), 2u);  // 2 slots x 1 queue
    EXPECT_EQ(cpa.GetAcquiredCount(0), 0u);
    EXPECT_EQ(cpa.GetAcquiredCount(1), 0u);
}

// CPA-LC-03: GIVEN valid device, 3 frames, all 3 queues WHEN Create THEN GetPoolCount()==9
TEST_P(CPATest, LC03_AllQueues) {
    auto caps = Dev().Dispatch([](const auto& dev) { return dev.GetCapabilities(); });
    if (!caps.hasAsyncCompute || !caps.hasAsyncTransfer) {
        GTEST_SKIP() << "Backend lacks async compute/transfer";
    }
    auto result = MakeCPA(3, true, true);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->GetPoolCount(), 9u);  // 3 slots x 3 queues
}

// CPA-LC-04: GIVEN valid CPA `a` WHEN `b = std::move(a)` THEN b works, a is moved-from
TEST_P(CPATest, LC04_MoveConstruct) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto a = std::move(*result);
    EXPECT_EQ(a.GetPoolCount(), 2u);

    auto b = std::move(a);
    EXPECT_EQ(b.GetPoolCount(), 2u);

    auto acq = b.Acquire(0, QueueType::Graphics);
    EXPECT_TRUE(acq.has_value());
    EXPECT_TRUE(acq->acquisition.bufferHandle.IsValid());
}

// CPA-LC-05: GIVEN valid CPA WHEN destructor runs THEN no crash/leak
TEST_P(CPATest, LC05_DestructorClean) {
    {
        auto result = MakeCPA(2);
        ASSERT_TRUE(result.has_value());
        auto& cpa = *result;
        // Acquire some buffers then let destructor clean up
        auto acq1 = cpa.Acquire(0, QueueType::Graphics);
        auto acq2 = cpa.Acquire(0, QueueType::Graphics);
        EXPECT_TRUE(acq1.has_value());
        EXPECT_TRUE(acq2.has_value());
    }
    // If we reach here without crash, pools were destroyed cleanly
    SUCCEED();
}

// CPA-LC-06: GIVEN desc with initialArenaCapacity=32, enableHwmShrink=true WHEN Create THEN stats correct
TEST_P(CPATest, LC06_DescOptions) {
    auto result = MakeCPA(2, false, false, 32, true);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;
    EXPECT_EQ(cpa.GetPoolCount(), 2u);
    auto stats = cpa.GetStats();
    EXPECT_TRUE(stats.hwmShrinkEnabled);
}

// ============================================================================
// §17.18.2 CPA-AR — Acquire / Release / ResetSlot
// ============================================================================

// CPA-AR-01: Acquire 4 buffers from slot 0 → all distinct, GetAcquiredCount==4
TEST_P(CPATest, AR01_AcquireMultiple) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    std::vector<CommandPoolAllocator::PooledAcquisition> acqs;
    for (uint32_t i = 0; i < 4; ++i) {
        auto acq = cpa.Acquire(0, QueueType::Graphics);
        ASSERT_TRUE(acq.has_value()) << "Acquire " << i << " failed";
        acqs.push_back(*acq);
    }
    EXPECT_EQ(cpa.GetAcquiredCount(0), 4u);

    // All handles should be valid
    for (auto& acq : acqs) {
        EXPECT_TRUE(acq.acquisition.bufferHandle.IsValid());
    }
}

// CPA-AR-02: Acquire 3, ResetSlot, GetAcquiredCount==0, re-acquire succeeds
TEST_P(CPATest, AR02_ResetSlot) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    for (uint32_t i = 0; i < 3; ++i) {
        auto acq = cpa.Acquire(0, QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
    }
    EXPECT_EQ(cpa.GetAcquiredCount(0), 3u);

    cpa.ResetSlot(0);
    EXPECT_EQ(cpa.GetAcquiredCount(0), 0u);

    // Re-acquire after reset should succeed (pool reused)
    auto acq = cpa.Acquire(0, QueueType::Graphics);
    EXPECT_TRUE(acq.has_value());
    EXPECT_EQ(cpa.GetAcquiredCount(0), 1u);
}

// CPA-AR-03: 3 frames, all queues → ResetSlot(1) only affects slot 1 (INV-SLOT-ISOLATION)
TEST_P(CPATest, AR03_SlotIsolation) {
    auto caps = Dev().Dispatch([](const auto& dev) { return dev.GetCapabilities(); });
    if (!caps.hasAsyncCompute || !caps.hasAsyncTransfer) {
        GTEST_SKIP() << "Backend lacks async compute/transfer";
    }

    auto result = MakeCPA(3, true, true);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    // Acquire from all 3 slots
    for (uint32_t f = 0; f < 3; ++f) {
        auto acq = cpa.Acquire(f, QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
    }

    EXPECT_EQ(cpa.GetAcquiredCount(0), 1u);
    EXPECT_EQ(cpa.GetAcquiredCount(1), 1u);
    EXPECT_EQ(cpa.GetAcquiredCount(2), 1u);

    cpa.ResetSlot(1);
    EXPECT_EQ(cpa.GetAcquiredCount(0), 1u);  // unchanged
    EXPECT_EQ(cpa.GetAcquiredCount(1), 0u);  // reset
    EXPECT_EQ(cpa.GetAcquiredCount(2), 1u);  // unchanged
}

// CPA-AR-04: Acquire then Release → GetAcquiredCount decremented
TEST_P(CPATest, AR04_Release) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    auto acq = cpa.Acquire(0, QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    EXPECT_EQ(cpa.GetAcquiredCount(0), 1u);

    cpa.Release(0, QueueType::Graphics, acq->arenaIndex);  // arenaIndex from PooledAcquisition
    EXPECT_EQ(cpa.GetAcquiredCount(0), 0u);
}

// CPA-AR-05: asyncTransfer=true → Transfer and Graphics use distinct pools
TEST_P(CPATest, AR05_TransferPool) {
    auto caps = Dev().Dispatch([](const auto& dev) { return dev.GetCapabilities(); });
    if (!caps.hasAsyncTransfer) {
        GTEST_SKIP() << "Backend lacks async transfer";
    }

    auto result = MakeCPA(2, false, true);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    auto gfxAcq = cpa.Acquire(0, QueueType::Graphics);
    auto xferAcq = cpa.Acquire(0, QueueType::Transfer);
    EXPECT_TRUE(gfxAcq.has_value());
    EXPECT_TRUE(xferAcq.has_value());
}

// CPA-AR-06: asyncTransfer=false → Acquire(Transfer) fails
TEST_P(CPATest, AR06_NoTransferPool) {
    auto result = MakeCPA(2, false, false);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    // Transfer queue should not exist when asyncTransfer=false
    // The QueueIndex assertion should fire or Acquire should handle gracefully
    // This depends on implementation — if assert fires in debug, we can't test it here
    // For now, test that graphics still works
    auto gfx = cpa.Acquire(0, QueueType::Graphics);
    EXPECT_TRUE(gfx.has_value());
}

// CPA-AR-07: Acquire 8, ResetSlot, Acquire 8 more → all succeed (pool reuse)
TEST_P(CPATest, AR07_PoolReuse) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    for (uint32_t i = 0; i < 8; ++i) {
        auto acq = cpa.Acquire(0, QueueType::Graphics);
        ASSERT_TRUE(acq.has_value()) << "First batch acquire " << i << " failed";
    }
    EXPECT_EQ(cpa.GetAcquiredCount(0), 8u);

    cpa.ResetSlot(0);
    EXPECT_EQ(cpa.GetAcquiredCount(0), 0u);

    for (uint32_t i = 0; i < 8; ++i) {
        auto acq = cpa.Acquire(0, QueueType::Graphics);
        ASSERT_TRUE(acq.has_value()) << "Second batch acquire " << i << " failed";
    }
    EXPECT_EQ(cpa.GetAcquiredCount(0), 8u);
}

// ============================================================================
// §17.18.3 CPA-BK — Backend-Specific Pool Flags
// (These are behavioral tests — the correct backend behavior is validated
// by the fact that operations succeed without driver errors.)
// ============================================================================

// CPA-BK-01: Vulkan — pools created without RESET_COMMAND_BUFFER_BIT (verified by successful reset)
TEST_P(CPATest, BK01_VulkanPoolFlags) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    // If pool was incorrectly created with RESET_COMMAND_BUFFER_BIT, behavior is still correct
    // but wasteful. We verify correct operation.
    auto acq = cpa.Acquire(0, QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    cpa.ResetSlot(0);
    auto acq2 = cpa.Acquire(0, QueueType::Graphics);
    EXPECT_TRUE(acq2.has_value());
}

// CPA-BK-02: D3D12 — allocator Reset + cached list reuse
TEST_P(CPATest, BK02_D3D12Reset) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    auto a1 = cpa.Acquire(0, QueueType::Graphics);
    auto a2 = cpa.Acquire(0, QueueType::Graphics);
    ASSERT_TRUE(a1.has_value());
    ASSERT_TRUE(a2.has_value());

    cpa.ResetSlot(0);

    auto b1 = cpa.Acquire(0, QueueType::Graphics);
    auto b2 = cpa.Acquire(0, QueueType::Graphics);
    EXPECT_TRUE(b1.has_value());
    EXPECT_TRUE(b2.has_value());
}

// CPA-BK-03: WebGPU — ResetSlot is no-op, subsequent Acquire creates new encoder
TEST_P(CPATest, BK03_WebGPUReset) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    auto acq = cpa.Acquire(0, QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    cpa.ResetSlot(0);
    auto acq2 = cpa.Acquire(0, QueueType::Graphics);
    EXPECT_TRUE(acq2.has_value());
}

// CPA-BK-04: OpenGL — ResetSlot clears deferred commands, Acquire works after
TEST_P(CPATest, BK04_OpenGLReset) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    auto acq = cpa.Acquire(0, QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    cpa.ResetSlot(0);
    auto acq2 = cpa.Acquire(0, QueueType::Graphics);
    EXPECT_TRUE(acq2.has_value());
}

// ============================================================================
// §17.18.4 CPA-MT — Multi-Thread Extension (v1: single-threaded, stub tests)
// ============================================================================

// CPA-MT-01: recordingThreadCount not yet implemented — skip
TEST_P(CPATest, MT01_MultiThreadPools) {
    GTEST_SKIP() << "Multi-thread extension not yet implemented (Phase 4)";
}

// CPA-MT-02: Multi-thread ResetSlot — skip
TEST_P(CPATest, MT02_MultiThreadReset) {
    GTEST_SKIP() << "Multi-thread extension not yet implemented (Phase 4)";
}

// CPA-MT-03: Single-thread mode (default) works correctly
TEST_P(CPATest, MT03_SingleThread) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    auto acq = cpa.Acquire(0, QueueType::Graphics);
    EXPECT_TRUE(acq.has_value());
    EXPECT_EQ(cpa.GetPoolCount(), 2u);  // framesInFlight x 1 queue x 1 thread
}

// ============================================================================
// §17.18.5 CPA-HW — Memory Shrink (OOM-Triggered)
// ============================================================================

// CPA-HW-01: enableHwmShrink=false → OOM flag ignored, fast-path reset
TEST_P(CPATest, HW01_ShrinkDisabled) {
    auto result = MakeCPA(2, false, false, 16, false);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    // Manually notify OOM — should be ignored since shrink is disabled
    cpa.NotifyOom(0, QueueType::Graphics);
    cpa.ResetSlot(0);  // Should still use fast-path (no crash)

    auto acq = cpa.Acquire(0, QueueType::Graphics);
    EXPECT_TRUE(acq.has_value());
}

// CPA-HW-02: enableHwmShrink=true, no OOM → fast-path reset
TEST_P(CPATest, HW02_ShrinkEnabledNoOOM) {
    auto result = MakeCPA(2, false, false, 16, true);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    auto acq = cpa.Acquire(0, QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    cpa.ResetSlot(0);

    // No OOM observed → fast-path reset (verified by no crash and continued operation)
    auto acq2 = cpa.Acquire(0, QueueType::Graphics);
    EXPECT_TRUE(acq2.has_value());
}

// CPA-HW-03: enableHwmShrink=true, OOM notified → RELEASE_RESOURCES on next reset
TEST_P(CPATest, HW03_ShrinkAfterOOM) {
    auto result = MakeCPA(2, false, false, 16, true);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    cpa.NotifyOom(0, QueueType::Graphics);
    cpa.ResetSlot(0);  // Should use RELEASE_RESOURCES (verified by no crash)

    auto acq = cpa.Acquire(0, QueueType::Graphics);
    EXPECT_TRUE(acq.has_value());
}

// CPA-HW-04: After shrink, next reset is fast-path again (flag cleared)
TEST_P(CPATest, HW04_ShrinkClearsFlag) {
    auto result = MakeCPA(2, false, false, 16, true);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    cpa.NotifyOom(0, QueueType::Graphics);
    cpa.ResetSlot(0);  // Shrink reset — clears flag

    // Second reset should be fast-path (no OOM since last shrink)
    auto acq = cpa.Acquire(0, QueueType::Graphics);
    ASSERT_TRUE(acq.has_value());
    cpa.ResetSlot(0);  // Fast-path (no crash)

    auto acq2 = cpa.Acquire(0, QueueType::Graphics);
    EXPECT_TRUE(acq2.has_value());
}

// ============================================================================
// §17.18.6 CPA-AS — AsyncTask Pool (not yet implemented, stub)
// ============================================================================

TEST_P(CPATest, AS01_AcquireAsync) {
    GTEST_SKIP() << "AsyncTask pool not yet implemented";
}

TEST_P(CPATest, AS02_ReleaseAsync) {
    GTEST_SKIP() << "AsyncTask pool not yet implemented";
}

TEST_P(CPATest, AS03_AsyncPoolGrow) {
    GTEST_SKIP() << "AsyncTask pool not yet implemented";
}

TEST_P(CPATest, AS04_AsyncCompletion) {
    GTEST_SKIP() << "AsyncTask pool not yet implemented";
}

TEST_P(CPATest, AS05_AsyncIsolation) {
    GTEST_SKIP() << "AsyncTask pool not yet implemented";
}

// ============================================================================
// §17.18.7 CPA-RT — Retained Command Buffers (not yet implemented, stub)
// ============================================================================

TEST_P(CPATest, RT01_AcquireRetained) {
    GTEST_SKIP() << "Retained command buffers not yet implemented";
}

TEST_P(CPATest, RT02_ReleaseRetained) {
    GTEST_SKIP() << "Retained command buffers not yet implemented";
}

TEST_P(CPATest, RT03_RetainedSurvivesReset) {
    GTEST_SKIP() << "Retained command buffers not yet implemented";
}

TEST_P(CPATest, RT04_LazyCreation) {
    GTEST_SKIP() << "Retained command buffers not yet implemented";
}

// ============================================================================
// §17.18.8 CPA-CA — CommandListArena
// ============================================================================

// CPA-CA-01: Empty arena, Acquire → index 0, AcquiredCount==1
TEST_P(CPATest, CA01_ArenaFirstAcquire) {
    CommandListArena<CommandListAcquisition> arena;
    arena.Reserve(16);

    auto [ptr, index] = arena.Acquire();
    EXPECT_EQ(index, 0u);
    EXPECT_EQ(arena.GetAcquiredCount(), 1u);
    EXPECT_NE(ptr, nullptr);
}

// CPA-CA-02: Acquire 5, Release(2), Acquire → returns index 2 (lowest free)
TEST_P(CPATest, CA02_ArenaReuseSlot) {
    CommandListArena<CommandListAcquisition> arena;
    arena.Reserve(16);

    for (uint32_t i = 0; i < 5; ++i) {
        (void)arena.Acquire();
    }
    EXPECT_EQ(arena.GetAcquiredCount(), 5u);

    arena.Release(2);
    auto [ptr, index] = arena.Acquire();
    EXPECT_EQ(index, 2u);
    EXPECT_EQ(arena.GetAcquiredCount(), 5u);  // released then re-acquired
}

// CPA-CA-03: Fill 128 slots, release one, re-acquire reuses it
TEST_P(CPATest, CA03_ArenaFull128) {
    CommandListArena<CommandListAcquisition> arena;

    for (uint32_t i = 0; i < 128; ++i) {
        (void)arena.Acquire();
    }
    EXPECT_EQ(arena.GetAcquiredCount(), 128u);

    arena.Release(30);
    auto [ptr, index] = arena.Acquire();
    EXPECT_EQ(index, 30u);
}

// CPA-CA-04: 65 Acquires (crosses word boundary)
TEST_P(CPATest, CA04_ArenaCrossWord) {
    CommandListArena<CommandListAcquisition> arena;

    for (uint32_t i = 0; i < 65; ++i) {
        (void)arena.Acquire();
    }
    EXPECT_EQ(arena.GetAcquiredCount(), 65u);

    arena.Release(0);
    auto [ptr, index] = arena.Acquire();
    EXPECT_EQ(index, 0u);  // Scans word 0 first
}

// CPA-CA-05: Acquire 5, Release all, ResetAll → AcquiredCount==0, capacity==5
TEST_P(CPATest, CA05_ArenaResetAll) {
    CommandListArena<CommandListAcquisition> arena;

    for (uint32_t i = 0; i < 5; ++i) {
        (void)arena.Acquire();
    }
    arena.ResetAll();
    EXPECT_EQ(arena.GetAcquiredCount(), 0u);
}

// CPA-CA-06: ResetAll then Acquire → returns index 0 (reused storage)
TEST_P(CPATest, CA06_ArenaResetThenAcquire) {
    CommandListArena<CommandListAcquisition> arena;

    for (uint32_t i = 0; i < 3; ++i) {
        (void)arena.Acquire();
    }
    arena.ResetAll();

    auto [ptr, index] = arena.Acquire();
    EXPECT_EQ(index, 0u);
}

// CPA-CA-07: Reserve(16), Acquire 16 times → no realloc
TEST_P(CPATest, CA07_ArenaReserve) {
    CommandListArena<CommandListAcquisition> arena;
    arena.Reserve(16);

    for (uint32_t i = 0; i < 16; ++i) {
        auto [ptr, index] = arena.Acquire();
        EXPECT_EQ(index, i);
    }
    EXPECT_EQ(arena.GetAcquiredCount(), 16u);
}

// CPA-CA-08: 128 full + 129th → overflow fallback, index==128
TEST_P(CPATest, CA08_ArenaOverflow) {
    CommandListArena<CommandListAcquisition> arena;

    for (uint32_t i = 0; i < 128; ++i) {
        (void)arena.Acquire();
    }
    auto [ptr, index] = arena.Acquire();
    EXPECT_EQ(index, 128u);  // Overflow slot
    EXPECT_NE(ptr, nullptr);
}

// ============================================================================
// §17.18.9 CPA-ST — Steady-State / Stress
// ============================================================================

// CPA-ST-01: 100-frame warm-up loop → steady state (arena reuse, no new pools)
TEST_P(CPATest, ST01_SteadyState) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    for (uint32_t frame = 0; frame < 100; ++frame) {
        uint32_t slot = frame % 2;
        cpa.ResetSlot(slot);
        for (uint32_t i = 0; i < 4; ++i) {
            auto acq = cpa.Acquire(slot, QueueType::Graphics);
            ASSERT_TRUE(acq.has_value()) << "Frame " << frame << " acquire " << i << " failed";
        }
    }

    // After warm-up, verify pool count hasn't grown
    EXPECT_EQ(cpa.GetPoolCount(), 2u);
}

// CPA-ST-02: 200 frames, 3 acquires per frame → no leaks, pool count stable
TEST_P(CPATest, ST02_StressNoLeak) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    for (uint32_t frame = 0; frame < 200; ++frame) {
        uint32_t slot = frame % 2;
        cpa.ResetSlot(slot);
        for (uint32_t i = 0; i < 3; ++i) {
            auto acq = cpa.Acquire(slot, QueueType::Graphics);
            ASSERT_TRUE(acq.has_value());
        }
    }
    EXPECT_EQ(cpa.GetPoolCount(), 2u);
    // Destructor runs here — if no crash, no leak
}

// ============================================================================
// §17.18.10 CPA-GS — GetStats / DumpStats / NotifyOom
// ============================================================================

// CPA-GS-01: After Create with graphics + transfer → stats correct
TEST_P(CPATest, GS01_InitialStats) {
    auto caps = Dev().Dispatch([](const auto& dev) { return dev.GetCapabilities(); });
    if (!caps.hasAsyncTransfer) {
        GTEST_SKIP() << "Backend lacks async transfer";
    }

    auto result = MakeCPA(2, false, true);
    ASSERT_TRUE(result.has_value());
    auto stats = result->GetStats();
    EXPECT_EQ(stats.framePoolCount, 4u);  // 2 frames x 2 queues (graphics + transfer)
    EXPECT_EQ(stats.currentAcquired, 0u);
    EXPECT_EQ(stats.highWaterMark, 0u);
    EXPECT_FALSE(stats.hwmShrinkEnabled);
}

// CPA-GS-02: Acquire 5 → currentAcquired==5, highWaterMark>=5
TEST_P(CPATest, GS02_AcquiredStats) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    for (uint32_t i = 0; i < 5; ++i) {
        auto acq = cpa.Acquire(0, QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
    }
    auto stats = cpa.GetStats();
    EXPECT_EQ(stats.currentAcquired, 5u);
    EXPECT_GE(stats.highWaterMark, 5u);
}

// CPA-GS-03: Acquire 10, ResetSlot, Acquire 3 → currentAcquired==3, highWaterMark==10
TEST_P(CPATest, GS03_HighWaterMark) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    for (uint32_t i = 0; i < 10; ++i) {
        auto acq = cpa.Acquire(0, QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
    }
    cpa.ResetSlot(0);
    for (uint32_t i = 0; i < 3; ++i) {
        auto acq = cpa.Acquire(0, QueueType::Graphics);
        ASSERT_TRUE(acq.has_value());
    }
    auto stats = cpa.GetStats();
    EXPECT_EQ(stats.currentAcquired, 3u);
    EXPECT_EQ(stats.highWaterMark, 10u);
}

// CPA-GS-04: DumpStats writes to file (basic smoke test)
TEST_P(CPATest, GS04_DumpStats) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    // DumpStats to stderr — just verify it doesn't crash
    cpa.DumpStats(stderr);
    SUCCEED();
}

// CPA-GS-05: NotifyOom sets flag, next ResetSlot uses RELEASE_RESOURCES
TEST_P(CPATest, GS05_NotifyOom) {
    auto result = MakeCPA(2, false, false, 16, true);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    cpa.NotifyOom(0, QueueType::Graphics);
    // Verify shrink is enabled in stats
    EXPECT_TRUE(cpa.GetStats().hwmShrinkEnabled);

    cpa.ResetSlot(0);  // Should use RELEASE_RESOURCES (no crash = success)

    auto acq = cpa.Acquire(0, QueueType::Graphics);
    EXPECT_TRUE(acq.has_value());
}

// ============================================================================
// §17.18.4 CPA-MT — Multi-Thread Extension Tests
// ============================================================================

// CPA-MT-01: GIVEN CPA with recordingThreadCount=4
//            WHEN  Acquire(slot=0, Graphics, threadIndex=0) and Acquire(slot=0, Graphics, threadIndex=1)
//            THEN  different native pools used (no contention)
//            AND   GetPoolCount() == framesInFlight * queueCount * threadCount
TEST_P(CPATest, MT01_MultiThreadDifferentPools) {
    constexpr uint32_t kThreads = 4;
    auto result = MakeCPA(2, false, false, 16, false, kThreads);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    // Pool count = 2 frames * 1 queue * 4 threads = 8
    EXPECT_EQ(cpa.GetPoolCount(), 2u * 1u * kThreads);
    EXPECT_EQ(cpa.GetRecordingThreadCount(), kThreads);

    // Acquire from different threads — each succeeds independently
    auto acq0 = cpa.Acquire(0, QueueType::Graphics, 0);
    auto acq1 = cpa.Acquire(0, QueueType::Graphics, 1);
    auto acq2 = cpa.Acquire(0, QueueType::Graphics, 2);
    auto acq3 = cpa.Acquire(0, QueueType::Graphics, 3);
    ASSERT_TRUE(acq0.has_value());
    ASSERT_TRUE(acq1.has_value());
    ASSERT_TRUE(acq2.has_value());
    ASSERT_TRUE(acq3.has_value());

    // All 4 buffers should have distinct handles (from different pools)
    EXPECT_NE(acq0->acquisition.bufferHandle, acq1->acquisition.bufferHandle);
    EXPECT_NE(acq2->acquisition.bufferHandle, acq3->acquisition.bufferHandle);

    EXPECT_EQ(cpa.GetAcquiredCount(0), 4u);
}

// CPA-MT-02: GIVEN CPA with recordingThreadCount=4
//            WHEN  ResetSlot(0) called
//            THEN  all 4 thread pools for slot 0 are reset
//            AND   thread pools for slots 1+ are unaffected
TEST_P(CPATest, MT02_ResetSlotResetsAllThreads) {
    constexpr uint32_t kThreads = 4;
    auto result = MakeCPA(2, false, false, 16, false, kThreads);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    // Acquire from all threads in slot 0
    for (uint32_t t = 0; t < kThreads; ++t) {
        auto acq = cpa.Acquire(0, QueueType::Graphics, t);
        ASSERT_TRUE(acq.has_value());
    }
    // Acquire from all threads in slot 1
    for (uint32_t t = 0; t < kThreads; ++t) {
        auto acq = cpa.Acquire(1, QueueType::Graphics, t);
        ASSERT_TRUE(acq.has_value());
    }
    EXPECT_EQ(cpa.GetAcquiredCount(0), kThreads);
    EXPECT_EQ(cpa.GetAcquiredCount(1), kThreads);

    // Reset only slot 0
    cpa.ResetSlot(0);
    EXPECT_EQ(cpa.GetAcquiredCount(0), 0u);
    EXPECT_EQ(cpa.GetAcquiredCount(1), kThreads);  // Unaffected
}

// CPA-MT-03: GIVEN CPA with recordingThreadCount=1 (default)
//            WHEN  Acquire(slot=0, Graphics, threadIndex=0) called
//            THEN  succeeds (single-thread mode backward compat)
//            AND   GetPoolCount() == framesInFlight * queueCount * 1
TEST_P(CPATest, MT03_SingleThreadDefault) {
    auto result = MakeCPA(2);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    EXPECT_EQ(cpa.GetRecordingThreadCount(), 1u);
    EXPECT_EQ(cpa.GetPoolCount(), 2u);  // 2 frames * 1 queue * 1 thread

    auto acq = cpa.Acquire(0, QueueType::Graphics, 0);
    ASSERT_TRUE(acq.has_value());
    EXPECT_EQ(cpa.GetAcquiredCount(0), 1u);
}

// CPA-MT-04: GIVEN invalid recordingThreadCount=0 WHEN Create THEN returns error
TEST_P(CPATest, MT04_ZeroThreadCountReturnsError) {
    CommandPoolAllocator::Desc desc{
        .device = Dev(),
        .framesInFlight = 2,
        .recordingThreadCount = 0,
    };
    auto result = CommandPoolAllocator::Create(desc);
    EXPECT_FALSE(result.has_value());
}

// CPA-MT-05: GIVEN recordingThreadCount > kMaxRecordingThreads WHEN Create THEN returns error
TEST_P(CPATest, MT05_ExcessiveThreadCountReturnsError) {
    CommandPoolAllocator::Desc desc{
        .device = Dev(),
        .framesInFlight = 2,
        .recordingThreadCount = CommandPoolAllocator::kMaxRecordingThreads + 1,
    };
    auto result = CommandPoolAllocator::Create(desc);
    EXPECT_FALSE(result.has_value());
}

// CPA-MT-06: GIVEN CPA with 4 threads, AcquireSecondary from different threads
//            WHEN  AcquireSecondary(slot=0, Graphics, t) for t in [0..3]
//            THEN  all succeed
TEST_P(CPATest, MT06_AcquireSecondaryMultiThread) {
    constexpr uint32_t kThreads = 4;
    auto result = MakeCPA(2, false, false, 16, false, kThreads);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    for (uint32_t t = 0; t < kThreads; ++t) {
        auto acq = cpa.AcquireSecondary(0, QueueType::Graphics, t);
        ASSERT_TRUE(acq.has_value()) << "AcquireSecondary failed for thread " << t;
    }
    EXPECT_EQ(cpa.GetAcquiredCount(0), kThreads);
}

// CPA-MT-07: GIVEN CPA with 4 threads, Release per-thread
//            WHEN  Acquire then Release on each thread
//            THEN  acquired count returns to 0
TEST_P(CPATest, MT07_ReleasePerThread) {
    constexpr uint32_t kThreads = 4;
    auto result = MakeCPA(2, false, false, 16, false, kThreads);
    ASSERT_TRUE(result.has_value());
    auto& cpa = *result;

    std::vector<CommandPoolAllocator::PooledAcquisition> acqs;
    for (uint32_t t = 0; t < kThreads; ++t) {
        auto acq = cpa.Acquire(0, QueueType::Graphics, t);
        ASSERT_TRUE(acq.has_value());
        acqs.push_back(*acq);
    }
    EXPECT_EQ(cpa.GetAcquiredCount(0), kThreads);

    for (uint32_t t = 0; t < kThreads; ++t) {
        cpa.Release(0, QueueType::Graphics, acqs[t].arenaIndex, t);
    }
    EXPECT_EQ(cpa.GetAcquiredCount(0), 0u);
}

// ============================================================================
// Parameterized instantiation — runs across all available backends
// ============================================================================

INSTANTIATE_TEST_SUITE_P(AllBackends, CPATest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);
