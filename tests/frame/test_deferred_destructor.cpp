// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// DeferredDestructor unit tests — specs/03-sync.md §4.3
// Tests verify bin management, type-erased handle queuing, and drain correctness.
// Requires real GPU device for actual Destroy dispatch.
// BDD style: GIVEN / WHEN / THEN.

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>

#include "RhiTestFixture.h"
#include "miki/frame/DeferredDestructor.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"
#include "miki/rhi/backend/AllBackends.h"

using namespace miki;
using namespace miki::frame;
using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// Test fixture — parameterized across all backends
// ============================================================================

class DeferredDestructorTest : public RhiTest {
   protected:
    auto MakeDD(uint32_t binCount = 2) -> DeferredDestructor { return DeferredDestructor::Create(Dev(), binCount); }

    auto CreateTestBuffer() -> BufferHandle {
        BufferDesc desc{};
        desc.size = 256;
        desc.usage = BufferUsage::Uniform;
        desc.memory = MemoryLocation::CpuToGpu;
        auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
        return result.value_or(BufferHandle{});
    }

    auto CreateTestTexture() -> TextureHandle {
        TextureDesc desc{};
        desc.width = 4;
        desc.height = 4;
        desc.format = Format::RGBA8_UNORM;
        desc.usage = TextureUsage::Sampled;
        auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(desc); });
        return result.value_or(TextureHandle{});
    }
};

// ============================================================================
// §4.3.1 DD-LC — Lifecycle
// ============================================================================

// DD-LC-01: GIVEN valid device WHEN Create(2) THEN PendingCount==0
TEST_P(DeferredDestructorTest, LC01_CreateEmpty) {
    auto dd = MakeDD(2);
    EXPECT_EQ(dd.PendingCount(), 0u);
}

// DD-LC-02: GIVEN binCount==0 WHEN Create THEN clamped to 1
TEST_P(DeferredDestructorTest, LC02_BinCountClampMin) {
    auto dd = DeferredDestructor::Create(Dev(), 0);
    // Should not crash; bin count internally clamped to 1
    EXPECT_EQ(dd.PendingCount(), 0u);
}

// DD-LC-03: GIVEN binCount==10 WHEN Create THEN clamped to kMaxBins
TEST_P(DeferredDestructorTest, LC03_BinCountClampMax) {
    auto dd = DeferredDestructor::Create(Dev(), 10);
    EXPECT_EQ(dd.PendingCount(), 0u);
}

// DD-LC-04: GIVEN DD with pending items WHEN move-constructed THEN dest has pending, source empty
TEST_P(DeferredDestructorTest, LC04_MoveConstruct) {
    auto dd = MakeDD(2);
    auto buf = CreateTestBuffer();
    if (!buf.IsValid()) {
        GTEST_SKIP() << "Buffer creation failed";
    }
    dd.Destroy(buf);
    EXPECT_EQ(dd.PendingCount(), 1u);

    auto dd2 = std::move(dd);
    EXPECT_EQ(dd2.PendingCount(), 1u);
    // dd2 destructor will drain and destroy the buffer
}

// DD-LC-05: GIVEN DD with pending items WHEN move-assigned THEN dest drains old + has new pending
TEST_P(DeferredDestructorTest, LC05_MoveAssign) {
    auto dd1 = MakeDD(2);
    auto dd2 = MakeDD(2);

    auto buf = CreateTestBuffer();
    if (!buf.IsValid()) {
        GTEST_SKIP() << "Buffer creation failed";
    }
    dd1.Destroy(buf);

    dd2 = std::move(dd1);
    EXPECT_EQ(dd2.PendingCount(), 1u);
}

// DD-LC-06: GIVEN DD with pending WHEN destructor runs THEN DrainAll called (no leak)
TEST_P(DeferredDestructorTest, LC06_DestructorDrains) {
    {
        auto dd = MakeDD(2);
        auto buf = CreateTestBuffer();
        if (!buf.IsValid()) {
            GTEST_SKIP() << "Buffer creation failed";
        }
        dd.Destroy(buf);
    }
    // If we reach here without crash/validation error, destructor drained correctly
    SUCCEED();
}

// ============================================================================
// §4.3.2 DD-ENQ — Enqueue (Destroy various handle types)
// ============================================================================

// DD-ENQ-01: GIVEN DD WHEN Destroy(BufferHandle) THEN PendingCount==1
TEST_P(DeferredDestructorTest, Enq01_Buffer) {
    auto dd = MakeDD(2);
    auto buf = CreateTestBuffer();
    if (!buf.IsValid()) {
        GTEST_SKIP() << "Buffer creation failed";
    }
    dd.Destroy(buf);
    EXPECT_EQ(dd.PendingCount(), 1u);
}

// DD-ENQ-02: GIVEN DD WHEN Destroy(TextureHandle) THEN PendingCount==1
TEST_P(DeferredDestructorTest, Enq02_Texture) {
    auto dd = MakeDD(2);
    auto tex = CreateTestTexture();
    if (!tex.IsValid()) {
        GTEST_SKIP() << "Texture creation failed";
    }
    dd.Destroy(tex);
    EXPECT_EQ(dd.PendingCount(), 1u);
}

// DD-ENQ-03: GIVEN DD WHEN Destroy(invalidHandle) THEN PendingCount stays 0 (invalid handles skipped)
TEST_P(DeferredDestructorTest, Enq03_InvalidHandleSkipped) {
    auto dd = MakeDD(2);
    dd.Destroy(BufferHandle{});
    dd.Destroy(TextureHandle{});
    EXPECT_EQ(dd.PendingCount(), 0u);
}

// DD-ENQ-04: GIVEN DD with bin0 active WHEN Destroy 3 buffers THEN PendingCount==3
TEST_P(DeferredDestructorTest, Enq04_MultipleBuffers) {
    auto dd = MakeDD(2);
    for (int i = 0; i < 3; ++i) {
        auto buf = CreateTestBuffer();
        if (!buf.IsValid()) {
            GTEST_SKIP() << "Buffer creation failed at " << i;
        }
        dd.Destroy(buf);
    }
    EXPECT_EQ(dd.PendingCount(), 3u);
}

// ============================================================================
// §4.3.3 DD-BIN — Bin Management
// ============================================================================

// DD-BIN-01: GIVEN DD(bins=2) WHEN SetCurrentBin(0), Destroy, SetCurrentBin(1), Destroy
//            THEN PendingCount==2 (one per bin)
TEST_P(DeferredDestructorTest, Bin01_SeparateBins) {
    auto dd = MakeDD(2);

    auto buf0 = CreateTestBuffer();
    auto buf1 = CreateTestBuffer();
    if (!buf0.IsValid() || !buf1.IsValid()) {
        GTEST_SKIP() << "Buffer creation failed";
    }

    dd.SetCurrentBin(0);
    dd.Destroy(buf0);

    dd.SetCurrentBin(1);
    dd.Destroy(buf1);

    EXPECT_EQ(dd.PendingCount(), 2u);
}

// DD-BIN-02: GIVEN DD with items in bin0 and bin1 WHEN DrainBin(0) THEN only bin0 drained
TEST_P(DeferredDestructorTest, Bin02_DrainSingleBin) {
    auto dd = MakeDD(2);

    auto buf0 = CreateTestBuffer();
    auto buf1 = CreateTestBuffer();
    if (!buf0.IsValid() || !buf1.IsValid()) {
        GTEST_SKIP() << "Buffer creation failed";
    }

    dd.SetCurrentBin(0);
    dd.Destroy(buf0);

    dd.SetCurrentBin(1);
    dd.Destroy(buf1);

    EXPECT_EQ(dd.PendingCount(), 2u);

    dd.DrainBin(0);
    EXPECT_EQ(dd.PendingCount(), 1u);  // Only bin1 remains
}

// DD-BIN-03: GIVEN DD with items in both bins WHEN DrainAll THEN PendingCount==0
TEST_P(DeferredDestructorTest, Bin03_DrainAll) {
    auto dd = MakeDD(2);

    auto buf0 = CreateTestBuffer();
    auto buf1 = CreateTestBuffer();
    if (!buf0.IsValid() || !buf1.IsValid()) {
        GTEST_SKIP() << "Buffer creation failed";
    }

    dd.SetCurrentBin(0);
    dd.Destroy(buf0);
    dd.SetCurrentBin(1);
    dd.Destroy(buf1);

    dd.DrainAll();
    EXPECT_EQ(dd.PendingCount(), 0u);
}

// DD-BIN-04: GIVEN empty bin WHEN DrainBin THEN no crash (no-op)
TEST_P(DeferredDestructorTest, Bin04_DrainEmptyBin) {
    auto dd = MakeDD(2);
    dd.DrainBin(0);
    dd.DrainBin(1);
    EXPECT_EQ(dd.PendingCount(), 0u);
}

// DD-BIN-05: GIVEN DD(bins=3) WHEN frame-ring simulation THEN correct isolation
TEST_P(DeferredDestructorTest, Bin05_FrameRingSimulation) {
    auto dd = DeferredDestructor::Create(Dev(), 3);

    // Simulate 6 frames with 3-bin ring
    for (uint32_t frame = 0; frame < 6; ++frame) {
        uint32_t slot = frame % 3;
        dd.DrainBin(slot);  // Drain old frame's bin (simulates BeginFrame)
        dd.SetCurrentBin(slot);

        auto buf = CreateTestBuffer();
        if (!buf.IsValid()) {
            GTEST_SKIP() << "Buffer creation failed at frame " << frame;
        }
        dd.Destroy(buf);
    }

    // After 6 frames: bins 0,1,2 each have 1 pending from frames 3,4,5
    EXPECT_EQ(dd.PendingCount(), 3u);
}

// ============================================================================
// §4.3.4 DD-STRESS — Steady-state stress
// ============================================================================

// DD-STRESS-01: 100 frames, 2 bins, 2 buffers per frame → all destroyed correctly
TEST_P(DeferredDestructorTest, Stress01_HundredFrames) {
    auto dd = MakeDD(2);

    for (uint32_t frame = 0; frame < 100; ++frame) {
        uint32_t slot = frame % 2;
        dd.DrainBin(slot);
        dd.SetCurrentBin(slot);

        for (int i = 0; i < 2; ++i) {
            auto buf = CreateTestBuffer();
            if (!buf.IsValid()) {
                GTEST_SKIP() << "Buffer creation failed";
            }
            dd.Destroy(buf);
        }
    }

    dd.DrainAll();
    EXPECT_EQ(dd.PendingCount(), 0u);
}

// ============================================================================
// Parameterized instantiation
// ============================================================================

INSTANTIATE_TEST_SUITE_P(AllBackends, DeferredDestructorTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);
