// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// ReadbackRing unit tests — specs/03-sync.md §7.2
// Tests verify multi-chunk readback ring allocation, GPU→CPU copy enqueue,
// frame lifecycle, ticket readiness, and memory management.
// BDD style: GIVEN / WHEN / THEN.

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>

#include "RhiTestFixture.h"
#include "miki/resource/ReadbackRing.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/RhiEnums.h"
#include "miki/rhi/RhiTypes.h"
#include "miki/rhi/backend/AllBackends.h"

using namespace miki;
using namespace miki::resource;
using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// Test fixture — parameterized across all backends
// ============================================================================

class ReadbackRingTest : public RhiTest {
   protected:
    [[nodiscard]] auto MakeRing(uint64_t chunkSize = uint64_t{64} << 10, uint32_t maxChunks = 16)
        -> core::Result<ReadbackRing> {
        return ReadbackRing::Create(Dev(), {.chunkSize = chunkSize, .maxChunks = maxChunks});
    }

    [[nodiscard]] auto CreateSrcBuffer(uint64_t size = 4096) -> BufferHandle {
        BufferDesc desc{};
        desc.size = size;
        desc.usage = BufferUsage::TransferSrc | BufferUsage::Uniform;
        desc.memory = MemoryLocation::GpuOnly;
        auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
        return result.value_or(BufferHandle{});
    }
};

// ============================================================================
// §7.2.1 RB-LC — Lifecycle
// ============================================================================

// RB-LC-01: GIVEN invalid device WHEN Create THEN error
TEST_P(ReadbackRingTest, LC01_InvalidDevice) {
    DeviceHandle invalid{};
    auto result = ReadbackRing::Create(invalid);
    EXPECT_FALSE(result.has_value());
}

// RB-LC-02: GIVEN valid device WHEN Create(default) THEN 1 active chunk, metrics correct
TEST_P(ReadbackRingTest, LC02_DefaultCreate) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value()) << "ReadbackRing::Create failed";
    auto& ring = *result;

    EXPECT_EQ(ring.GetActiveChunkCount(), 1u);
    EXPECT_EQ(ring.GetFreeChunkCount(), 0u);
    EXPECT_EQ(ring.GetTotalChunkCount(), 1u);
    EXPECT_GT(ring.Capacity(), 0u);
    EXPECT_EQ(ring.GetBytesReadbackThisFrame(), 0u);
    EXPECT_EQ(ring.GetPendingCopyCount(), 0u);
}

// RB-LC-03: GIVEN chunkSize==0 WHEN Create THEN error
TEST_P(ReadbackRingTest, LC03_ZeroChunkSize) {
    auto result = ReadbackRing::Create(Dev(), {.chunkSize = 0, .maxChunks = 16});
    EXPECT_FALSE(result.has_value());
}

// RB-LC-04: GIVEN maxChunks==0 WHEN Create THEN error
TEST_P(ReadbackRingTest, LC04_ZeroMaxChunks) {
    auto result = ReadbackRing::Create(Dev(), {.chunkSize = 4096, .maxChunks = 0});
    EXPECT_FALSE(result.has_value());
}

// RB-LC-05: GIVEN valid ring WHEN move-constructed THEN dest works
TEST_P(ReadbackRingTest, LC05_MoveConstruct) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());

    auto ring2 = std::move(*result);
    EXPECT_GT(ring2.Capacity(), 0u);
    EXPECT_EQ(ring2.GetActiveChunkCount(), 1u);
}

// RB-LC-06: GIVEN valid ring WHEN destructor after enqueue THEN no crash
TEST_P(ReadbackRingTest, LC06_DestructorClean) {
    {
        auto result = MakeRing();
        ASSERT_TRUE(result.has_value());

        auto src = CreateSrcBuffer();
        if (src.IsValid()) {
            auto ticket = result->EnqueueBufferReadback(src, 0, 256);
            (void)ticket;
            Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(src); });
        }
    }
    SUCCEED();
}

// ============================================================================
// §7.2.2 RB-ENQ — Enqueue readbacks
// ============================================================================

// RB-ENQ-01: GIVEN ring WHEN EnqueueBufferReadback THEN valid ticket, PendingCopyCount==1
TEST_P(ReadbackRingTest, Enq01_BufferReadback) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    auto src = CreateSrcBuffer();
    if (!src.IsValid()) {
        GTEST_SKIP() << "Src buffer creation failed";
    }

    auto ticket = ring.EnqueueBufferReadback(src, 0, 512);
    ASSERT_TRUE(ticket.has_value());
    EXPECT_TRUE(ticket->IsValid());
    EXPECT_EQ(ticket->size, 512u);
    EXPECT_EQ(ring.GetPendingCopyCount(), 1u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(src); });
}

// RB-ENQ-02: GIVEN ring WHEN EnqueueTextureReadback THEN valid ticket, PendingCopyCount==1
TEST_P(ReadbackRingTest, Enq02_TextureReadback) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    TextureDesc texDesc{};
    texDesc.width = 4;
    texDesc.height = 4;
    texDesc.format = Format::RGBA8_UNORM;
    texDesc.usage = TextureUsage::TransferSrc | TextureUsage::Sampled;
    auto tex = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(texDesc); });
    if (!tex.has_value()) {
        GTEST_SKIP() << "Texture creation failed";
    }

    auto ticket = ring.EnqueueTextureReadback(*tex, {.width = 4, .height = 4}, 4 * 4 * 4);
    ASSERT_TRUE(ticket.has_value());
    EXPECT_TRUE(ticket->IsValid());
    EXPECT_EQ(ring.GetPendingCopyCount(), 1u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyTexture(*tex); });
}

// RB-ENQ-03: GIVEN ring WHEN 5 buffer readbacks THEN PendingCopyCount==5
TEST_P(ReadbackRingTest, Enq03_MultipleReadbacks) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    auto src = CreateSrcBuffer();
    if (!src.IsValid()) {
        GTEST_SKIP() << "Src buffer creation failed";
    }

    for (int i = 0; i < 5; ++i) {
        auto ticket = ring.EnqueueBufferReadback(src, 0, 256);
        ASSERT_TRUE(ticket.has_value()) << "Enqueue failed at " << i;
    }
    EXPECT_EQ(ring.GetPendingCopyCount(), 5u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(src); });
}

// ============================================================================
// §7.2.3 RB-TICKET — Ticket readiness
// ============================================================================

// RB-TICKET-01: GIVEN fresh ticket WHEN IsReady before flush THEN false
TEST_P(ReadbackRingTest, Ticket01_NotReadyBeforeFlush) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    auto src = CreateSrcBuffer();
    if (!src.IsValid()) {
        GTEST_SKIP() << "Src buffer creation failed";
    }

    auto ticket = ring.EnqueueBufferReadback(src, 0, 256);
    ASSERT_TRUE(ticket.has_value());

    // Before FlushFrame + ReclaimCompleted, ticket should not be ready
    EXPECT_FALSE(ring.IsReady(*ticket));

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(src); });
}

// RB-TICKET-02: GIVEN ticket after flush+reclaim WHEN IsReady THEN true, GetData returns span
TEST_P(ReadbackRingTest, Ticket02_ReadyAfterReclaim) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    auto src = CreateSrcBuffer();
    if (!src.IsValid()) {
        GTEST_SKIP() << "Src buffer creation failed";
    }

    auto ticket = ring.EnqueueBufferReadback(src, 0, 256);
    ASSERT_TRUE(ticket.has_value());

    ring.FlushFrame(1);
    ring.ReclaimCompleted(1);

    EXPECT_TRUE(ring.IsReady(*ticket));
    auto data = ring.GetData(*ticket);
    EXPECT_EQ(data.size(), 256u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(src); });
}

// RB-TICKET-03: GIVEN invalid ticket WHEN IsReady THEN false
TEST_P(ReadbackRingTest, Ticket03_InvalidTicket) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());

    ReadbackTicket invalid{};
    EXPECT_FALSE(result->IsReady(invalid));
}

// ============================================================================
// §7.2.4 RB-FRAME — Frame lifecycle
// ============================================================================

// RB-FRAME-01: GIVEN ring with readbacks WHEN FlushFrame THEN bytesReadback resets
TEST_P(ReadbackRingTest, Frame01_FlushResetsBytes) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    auto src = CreateSrcBuffer();
    if (!src.IsValid()) {
        GTEST_SKIP() << "Src buffer creation failed";
    }

    (void)ring.EnqueueBufferReadback(src, 0, 1024);
    ring.FlushFrame(1);
    EXPECT_EQ(ring.GetBytesReadbackThisFrame(), 0u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(src); });
}

// RB-FRAME-02: GIVEN 10-frame simulation THEN chunks recycled, no excessive growth
TEST_P(ReadbackRingTest, Frame02_FrameRingRecycle) {
    auto result = MakeRing(uint64_t{8} << 10, 16);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    auto src = CreateSrcBuffer();
    if (!src.IsValid()) {
        GTEST_SKIP() << "Src buffer creation failed";
    }

    for (uint64_t frame = 1; frame <= 10; ++frame) {
        if (frame > 2) {
            ring.ReclaimCompleted(frame - 2);
        }
        (void)ring.EnqueueBufferReadback(src, 0, 2048);
        ring.FlushFrame(frame);
    }

    EXPECT_LE(ring.GetActiveChunkCount(), 3u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(src); });
}

// ============================================================================
// §7.2.5 RB-SHRINK — ShrinkToFit
// ============================================================================

// RB-SHRINK-01: GIVEN ring with free chunks WHEN ShrinkToFit(0) THEN frees all free chunks
TEST_P(ReadbackRingTest, Shrink01_FreeAll) {
    auto result = MakeRing(uint64_t{4} << 10, 16);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    auto src = CreateSrcBuffer();
    if (!src.IsValid()) {
        GTEST_SKIP() << "Src buffer creation failed";
    }

    // Force multiple chunks
    for (int i = 0; i < 4; ++i) {
        (void)ring.EnqueueBufferReadback(src, 0, 3000);
    }
    ring.FlushFrame(1);
    ring.ReclaimCompleted(1);

    auto freed = ring.ShrinkToFit(0);
    EXPECT_GT(freed, 0u);
    EXPECT_EQ(ring.GetFreeChunkCount(), 0u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(src); });
}

// ============================================================================
// §7.2.6 RB-STRESS — Steady-state stress
// ============================================================================

// RB-STRESS-01: 100-frame simulation
TEST_P(ReadbackRingTest, Stress01_HundredFrames) {
    auto result = MakeRing(uint64_t{16} << 10, 32);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    auto src = CreateSrcBuffer();
    if (!src.IsValid()) {
        GTEST_SKIP() << "Src buffer creation failed";
    }

    for (uint64_t frame = 1; frame <= 100; ++frame) {
        if (frame > 2) {
            ring.ReclaimCompleted(frame - 2);
        }

        uint64_t readbackSize = 256 + (frame % 5) * 512;
        auto ticket = ring.EnqueueBufferReadback(src, 0, readbackSize);
        ASSERT_TRUE(ticket.has_value()) << "Frame " << frame << " enqueue failed";

        ring.FlushFrame(frame);
    }

    EXPECT_LE(ring.GetTotalChunkCount(), 8u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(src); });
}

// ============================================================================
// Parameterized instantiation
// ============================================================================

INSTANTIATE_TEST_SUITE_P(RealBackends, ReadbackRingTest, ::testing::ValuesIn(GetRealBackends()), BackendName);
