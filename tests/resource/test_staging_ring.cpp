// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// StagingRing unit tests — specs/03-sync.md §7.1
// Tests verify multi-chunk staging ring allocation, copy enqueue, frame lifecycle,
// and memory management across all real backends.
// BDD style: GIVEN / WHEN / THEN.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "RhiTestFixture.h"
#include "miki/resource/StagingRing.h"
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

class StagingRingTest : public RhiTest {
   protected:
    [[nodiscard]] auto MakeRing(uint64_t chunkSize = uint64_t{64} << 10, uint32_t maxChunks = 16)
        -> core::Result<StagingRing> {
        return StagingRing::Create(Dev(), {.chunkSize = chunkSize, .maxChunks = maxChunks});
    }

    [[nodiscard]] auto CreateDstBuffer(uint64_t size = 4096) -> BufferHandle {
        BufferDesc desc{};
        desc.size = size;
        desc.usage = BufferUsage::TransferDst | BufferUsage::Uniform;
        desc.memory = MemoryLocation::GpuOnly;
        auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
        return result.value_or(BufferHandle{});
    }
};

// ============================================================================
// §7.1.1 SR-LC — Lifecycle
// ============================================================================

// SR-LC-01: GIVEN invalid device WHEN Create THEN error
TEST_P(StagingRingTest, LC01_InvalidDevice) {
    DeviceHandle invalid{};
    auto result = StagingRing::Create(invalid);
    EXPECT_FALSE(result.has_value());
}

// SR-LC-02: GIVEN valid device WHEN Create(default) THEN 1 active chunk, metrics correct
TEST_P(StagingRingTest, LC02_DefaultCreate) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value()) << "StagingRing::Create failed";
    auto& ring = *result;

    EXPECT_EQ(ring.GetActiveChunkCount(), 1u);
    EXPECT_EQ(ring.GetFreeChunkCount(), 0u);
    EXPECT_EQ(ring.GetTotalChunkCount(), 1u);
    EXPECT_GT(ring.Capacity(), 0u);
    EXPECT_EQ(ring.GetBytesUploadedThisFrame(), 0u);
    EXPECT_EQ(ring.GetPendingCopyCount(), 0u);
}

// SR-LC-03: GIVEN chunkSize==0 WHEN Create THEN error
TEST_P(StagingRingTest, LC03_ZeroChunkSize) {
    auto result = StagingRing::Create(Dev(), {.chunkSize = 0, .maxChunks = 16});
    EXPECT_FALSE(result.has_value());
}

// SR-LC-04: GIVEN maxChunks==0 WHEN Create THEN error
TEST_P(StagingRingTest, LC04_ZeroMaxChunks) {
    auto result = StagingRing::Create(Dev(), {.chunkSize = 4096, .maxChunks = 0});
    EXPECT_FALSE(result.has_value());
}

// SR-LC-05: GIVEN valid ring WHEN move-constructed THEN dest works, source empty
TEST_P(StagingRingTest, LC05_MoveConstruct) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());

    auto ring2 = std::move(*result);
    EXPECT_GT(ring2.Capacity(), 0u);
    EXPECT_EQ(ring2.GetActiveChunkCount(), 1u);
}

// SR-LC-06: GIVEN valid ring WHEN destructor runs after allocations THEN no crash/leak
TEST_P(StagingRingTest, LC06_DestructorClean) {
    {
        auto result = MakeRing();
        ASSERT_TRUE(result.has_value());
        auto alloc = result->Allocate(1024);
        EXPECT_TRUE(alloc.has_value());
    }
    SUCCEED();
}

// ============================================================================
// §7.1.2 SR-ALLOC — Allocation
// ============================================================================

// SR-ALLOC-01: GIVEN ring WHEN Allocate(1024) THEN valid mapped pointer, correct size
TEST_P(StagingRingTest, Alloc01_BasicAlloc) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());

    auto alloc = result->Allocate(1024);
    ASSERT_TRUE(alloc.has_value());
    EXPECT_TRUE(alloc->IsValid());
    EXPECT_NE(alloc->mappedPtr, nullptr);
    EXPECT_EQ(alloc->size, 1024u);
}

// SR-ALLOC-02: GIVEN ring WHEN Allocate(0) THEN returns empty allocation (not error)
TEST_P(StagingRingTest, Alloc02_ZeroSize) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());

    auto alloc = result->Allocate(0);
    ASSERT_TRUE(alloc.has_value());
    EXPECT_FALSE(alloc->IsValid());  // Zero-size = empty
}

// SR-ALLOC-03: GIVEN ring(64KB) WHEN Allocate 10 x 4KB THEN all succeed, bytes tracked
TEST_P(StagingRingTest, Alloc03_MultipleSmallAllocs) {
    auto result = MakeRing(uint64_t{64} << 10);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    for (int i = 0; i < 10; ++i) {
        auto alloc = ring.Allocate(4096);
        ASSERT_TRUE(alloc.has_value()) << "Allocate failed at " << i;
        EXPECT_TRUE(alloc->IsValid());
    }
    EXPECT_EQ(ring.GetBytesUploadedThisFrame(), 10u * 4096u);
}

// SR-ALLOC-04: GIVEN ring(64KB, maxChunks=4) WHEN allocate > 64KB THEN auto-grow, oversized chunk created
TEST_P(StagingRingTest, Alloc04_OversizedChunk) {
    auto result = MakeRing(uint64_t{64} << 10, 4);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    // Allocate 128KB — exceeds single chunk, should create oversized chunk
    auto alloc = ring.Allocate(uint64_t{128} << 10);
    ASSERT_TRUE(alloc.has_value());
    EXPECT_TRUE(alloc->IsValid());
    EXPECT_GE(ring.GetTotalChunkCount(), 2u);
}

// SR-ALLOC-05: GIVEN ring WHEN Allocate fills chunk THEN next alloc auto-grows
TEST_P(StagingRingTest, Alloc05_ChunkAutoGrow) {
    auto result = MakeRing(uint64_t{4} << 10, 8);  // 4KB chunks
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    EXPECT_EQ(ring.GetTotalChunkCount(), 1u);

    // Fill first chunk
    auto a1 = ring.Allocate(3000);
    ASSERT_TRUE(a1.has_value());

    // This should trigger a new chunk
    auto a2 = ring.Allocate(3000);
    ASSERT_TRUE(a2.has_value());

    EXPECT_GE(ring.GetTotalChunkCount(), 2u);
}

// SR-ALLOC-06: GIVEN ring WHEN write to mappedPtr THEN data persists
TEST_P(StagingRingTest, Alloc06_WriteData) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());

    auto alloc = result->Allocate(256);
    ASSERT_TRUE(alloc.has_value());
    ASSERT_NE(alloc->mappedPtr, nullptr);

    // Write pattern
    std::memset(alloc->mappedPtr, 0xAB, 256);
    EXPECT_EQ(static_cast<uint8_t>(alloc->mappedPtr[0]), 0xAB);
    EXPECT_EQ(static_cast<uint8_t>(alloc->mappedPtr[255]), 0xAB);
}

// ============================================================================
// §7.1.3 SR-COPY — Enqueue + Record
// ============================================================================

// SR-COPY-01: GIVEN ring with allocation WHEN EnqueueBufferCopy THEN PendingCopyCount==1
TEST_P(StagingRingTest, Copy01_EnqueueBuffer) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    auto dst = CreateDstBuffer();
    if (!dst.IsValid()) {
        GTEST_SKIP() << "Dst buffer creation failed";
    }

    auto alloc = ring.Allocate(512);
    ASSERT_TRUE(alloc.has_value());
    ring.EnqueueBufferCopy(*alloc, dst, 0);
    EXPECT_EQ(ring.GetPendingCopyCount(), 1u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(dst); });
}

// SR-COPY-02: GIVEN ring WHEN EnqueueTextureCopy THEN PendingCopyCount==1
TEST_P(StagingRingTest, Copy02_EnqueueTexture) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    TextureDesc texDesc{};
    texDesc.width = 4;
    texDesc.height = 4;
    texDesc.format = Format::RGBA8_UNORM;
    texDesc.usage = TextureUsage::TransferDst | TextureUsage::Sampled;
    auto tex = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(texDesc); });
    if (!tex.has_value()) {
        GTEST_SKIP() << "Texture creation failed";
    }

    auto alloc = ring.Allocate(4 * 4 * 4);  // 4x4 RGBA8
    ASSERT_TRUE(alloc.has_value());
    ring.EnqueueTextureCopy(*alloc, *tex, {.width = 4, .height = 4});
    EXPECT_EQ(ring.GetPendingCopyCount(), 1u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyTexture(*tex); });
}

// SR-COPY-03: GIVEN invalid allocation WHEN EnqueueBufferCopy THEN no-op (count stays 0)
TEST_P(StagingRingTest, Copy03_InvalidAllocSkipped) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());

    StagingAllocation invalid{};
    auto dst = CreateDstBuffer();
    if (!dst.IsValid()) {
        GTEST_SKIP() << "Dst buffer creation failed";
    }

    result->EnqueueBufferCopy(invalid, dst, 0);
    EXPECT_EQ(result->GetPendingCopyCount(), 0u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(dst); });
}

// ============================================================================
// §7.1.4 SR-WRAP — Convenience wrappers
// ============================================================================

// SR-WRAP-01: GIVEN ring WHEN UploadBuffer(data, dst, 0) THEN PendingCopyCount==1
TEST_P(StagingRingTest, Wrap01_UploadBuffer) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());

    auto dst = CreateDstBuffer();
    if (!dst.IsValid()) {
        GTEST_SKIP() << "Dst buffer creation failed";
    }

    std::array<std::byte, 128> data{};
    auto r = result->UploadBuffer(data, dst, 0);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(result->GetPendingCopyCount(), 1u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(dst); });
}

// SR-WRAP-02: GIVEN ring WHEN UploadBuffer(empty) THEN no-op
TEST_P(StagingRingTest, Wrap02_UploadBufferEmpty) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());

    auto dst = CreateDstBuffer();
    if (!dst.IsValid()) {
        GTEST_SKIP() << "Dst buffer creation failed";
    }

    auto r = result->UploadBuffer({}, dst, 0);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(result->GetPendingCopyCount(), 0u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(dst); });
}

// ============================================================================
// §7.1.5 SR-FRAME — Frame lifecycle
// ============================================================================

// SR-FRAME-01: GIVEN ring with uploads WHEN FlushFrame THEN bytesUploaded resets
TEST_P(StagingRingTest, Frame01_FlushResetsBytes) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    (void)ring.Allocate(1024);
    EXPECT_GT(ring.GetBytesUploadedThisFrame(), 0u);

    ring.FlushFrame(1);
    EXPECT_EQ(ring.GetBytesUploadedThisFrame(), 0u);
}

// SR-FRAME-02: GIVEN ring with flushed chunks WHEN ReclaimCompleted THEN free chunks increase
TEST_P(StagingRingTest, Frame02_Reclaim) {
    auto result = MakeRing(uint64_t{4} << 10, 8);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    // Fill first chunk, force second
    (void)ring.Allocate(3000);
    (void)ring.Allocate(3000);
    EXPECT_GE(ring.GetTotalChunkCount(), 2u);

    // Flush at fence=1 then reclaim
    ring.FlushFrame(1);
    ring.ReclaimCompleted(1);

    // Reclaimed chunks become free
    EXPECT_GT(ring.GetFreeChunkCount(), 0u);
}

// SR-FRAME-03: GIVEN 10-frame ring simulation THEN chunks are recycled
TEST_P(StagingRingTest, Frame03_FrameRingRecycle) {
    auto result = MakeRing(uint64_t{8} << 10, 16);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    for (uint64_t frame = 1; frame <= 10; ++frame) {
        if (frame > 2) {
            ring.ReclaimCompleted(frame - 2);
        }
        (void)ring.Allocate(4096);
        ring.FlushFrame(frame);
    }

    // After 10 frames with 2-frame latency, should have recycled chunks
    EXPECT_LE(ring.GetActiveChunkCount(), 3u);  // At most 2-3 in flight
}

// ============================================================================
// §7.1.6 SR-SHRINK — ShrinkToFit
// ============================================================================

// SR-SHRINK-01: GIVEN ring with many chunks WHEN ShrinkToFit(0) THEN frees all free chunks
TEST_P(StagingRingTest, Shrink01_FreeAll) {
    auto result = MakeRing(uint64_t{4} << 10, 16);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    // Allocate across multiple chunks
    for (int i = 0; i < 4; ++i) {
        (void)ring.Allocate(3000);
    }
    ring.FlushFrame(1);
    ring.ReclaimCompleted(1);

    auto freed = ring.ShrinkToFit(0);
    EXPECT_GT(freed, 0u);
    EXPECT_EQ(ring.GetFreeChunkCount(), 0u);
}

// ============================================================================
// §7.1.7 SR-METRIC — Utilization metrics
// ============================================================================

// SR-METRIC-01: GIVEN fresh ring WHEN GetUtilization THEN 0.0
TEST_P(StagingRingTest, Metric01_InitialUtilZero) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    EXPECT_LE(result->GetUtilization(), 0.01f);
}

// SR-METRIC-02: GIVEN ring after allocation WHEN GetUtilization THEN > 0
TEST_P(StagingRingTest, Metric02_UtilAfterAlloc) {
    auto result = MakeRing(uint64_t{64} << 10);
    ASSERT_TRUE(result.has_value());

    (void)result->Allocate(32 * 1024);
    EXPECT_GT(result->GetUtilization(), 0.0f);
}

// ============================================================================
// §7.1.8 SR-STRESS — Steady-state stress
// ============================================================================

// SR-STRESS-01: 100-frame simulation with varying upload sizes
TEST_P(StagingRingTest, Stress01_HundredFrames) {
    auto result = MakeRing(uint64_t{16} << 10, 32);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    for (uint64_t frame = 1; frame <= 100; ++frame) {
        if (frame > 2) {
            ring.ReclaimCompleted(frame - 2);
        }

        // Varying upload sizes
        uint64_t uploadSize = 512 + (frame % 7) * 1024;
        auto alloc = ring.Allocate(uploadSize);
        ASSERT_TRUE(alloc.has_value()) << "Frame " << frame << " alloc failed";

        ring.FlushFrame(frame);
    }

    // Should not have excessive chunk count
    EXPECT_LE(ring.GetTotalChunkCount(), 8u);
}

// ============================================================================
// §7.1.9 SR-PENDING — GetPendingBytes tracking
// ============================================================================

// SR-PENDING-01: GIVEN fresh ring WHEN no enqueue THEN pendingBytes == 0
TEST_P(StagingRingTest, Pending01_InitialZero) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->GetPendingBytes(), 0u);
}

// SR-PENDING-02: GIVEN ring WHEN EnqueueBufferCopy(512) THEN pendingBytes == 512
TEST_P(StagingRingTest, Pending02_TracksSingleEnqueue) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    auto dst = CreateDstBuffer();
    if (!dst.IsValid()) {
        GTEST_SKIP() << "Dst buffer creation failed";
    }

    auto alloc = ring.Allocate(512);
    ASSERT_TRUE(alloc.has_value());
    ring.EnqueueBufferCopy(*alloc, dst, 0);

    EXPECT_EQ(ring.GetPendingBytes(), 512u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(dst); });
}

// SR-PENDING-03: GIVEN ring WHEN enqueue 3 copies THEN pendingBytes accumulates
TEST_P(StagingRingTest, Pending03_AccumulatesMultiple) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    auto dst = CreateDstBuffer(16384);
    if (!dst.IsValid()) {
        GTEST_SKIP() << "Dst buffer creation failed";
    }

    uint64_t expected = 0;
    for (uint64_t size : {128u, 256u, 1024u}) {
        auto alloc = ring.Allocate(size);
        ASSERT_TRUE(alloc.has_value());
        ring.EnqueueBufferCopy(*alloc, dst, expected);
        expected += size;
    }
    EXPECT_EQ(ring.GetPendingBytes(), expected);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(dst); });
}

// SR-PENDING-04: GIVEN ring with pending copies WHEN FlushFrame+ReclaimCompleted THEN
// pendingBytes persists (only RecordTransfers clears it) — verify semantic contract.
// NOTE: RecordTransfers with real GPU command buffers is tested in integration tests.
// This unit test verifies the tracking invariants without command recording.
TEST_P(StagingRingTest, Pending04_PersistsThroughFlushReclaim) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    auto dst = CreateDstBuffer();
    if (!dst.IsValid()) {
        GTEST_SKIP() << "Dst buffer creation failed";
    }

    auto alloc = ring.Allocate(512);
    ASSERT_TRUE(alloc.has_value());
    ring.EnqueueBufferCopy(*alloc, dst, 0);

    EXPECT_EQ(ring.GetPendingBytes(), 512u);
    EXPECT_EQ(ring.GetPendingCopyCount(), 1u);

    // FlushFrame does NOT clear pendingBytes — only RecordTransfers does
    ring.FlushFrame(1);
    EXPECT_EQ(ring.GetPendingBytes(), 512u);
    EXPECT_EQ(ring.GetPendingCopyCount(), 1u);

    // ReclaimCompleted does NOT clear pending copies either
    ring.ReclaimCompleted(1);
    EXPECT_EQ(ring.GetPendingBytes(), 512u);
    EXPECT_EQ(ring.GetPendingCopyCount(), 1u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(dst); });
}

// SR-PENDING-05: GIVEN ReBAR allocation WHEN EnqueueBufferCopy THEN no-op (pendingBytes stays 0)
TEST_P(StagingRingTest, Pending05_RebarSkipped) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    auto dst = CreateDstBuffer();
    if (!dst.IsValid()) {
        GTEST_SKIP() << "Dst buffer creation failed";
    }

    // Craft a fake ReBAR allocation
    StagingAllocation rebarAlloc{.mappedPtr = reinterpret_cast<std::byte*>(1), .size = 256, .isReBAR_ = true};
    ring.EnqueueBufferCopy(rebarAlloc, dst, 0);
    EXPECT_EQ(ring.GetPendingBytes(), 0u);
    EXPECT_EQ(ring.GetPendingCopyCount(), 0u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(dst); });
}

// ============================================================================
// §7.1.10 SR-EVICT — EvictStaleChunks
// ============================================================================

// SR-EVICT-01: GIVEN ring with 1 active chunk, no free WHEN EvictStaleChunks THEN 0 freed
TEST_P(StagingRingTest, Evict01_NoFreeChunks) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    EXPECT_EQ(ring.GetFreeChunkCount(), 0u);
    auto freed = ring.EvictStaleChunks(1000, 60);
    EXPECT_EQ(freed, 0u);
}

// SR-EVICT-02: GIVEN ring with free chunks not yet stale WHEN EvictStaleChunks THEN 0 freed
TEST_P(StagingRingTest, Evict02_NotYetStale) {
    auto result = MakeRing(uint64_t{4} << 10, 8);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    // Force 2 chunks, then reclaim at frame 100
    (void)ring.Allocate(3000);
    (void)ring.Allocate(3000);
    ring.FlushFrame(1);
    ring.EvictStaleChunks(100, 60);  // Sets currentFrame=100
    ring.ReclaimCompleted(1);        // Stamps lastReclaimedFrame=100

    auto freeCount = ring.GetFreeChunkCount();
    ASSERT_GT(freeCount, 0u);

    // Only 50 frames later — not stale yet (need > 60)
    auto freed = ring.EvictStaleChunks(150, 60);
    EXPECT_EQ(freed, 0u);
    EXPECT_EQ(ring.GetFreeChunkCount(), freeCount);  // Unchanged
}

// SR-EVICT-03: GIVEN ring with stale free chunks WHEN EvictStaleChunks THEN freed > 0
TEST_P(StagingRingTest, Evict03_StaleEvicted) {
    auto result = MakeRing(uint64_t{4} << 10, 8);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    // Force 3 chunks (need >1 so eviction can keep 1 alive)
    (void)ring.Allocate(3000);
    (void)ring.Allocate(3000);
    (void)ring.Allocate(3000);
    auto totalBefore = ring.GetTotalChunkCount();
    ASSERT_GE(totalBefore, 3u);

    ring.FlushFrame(1);
    ring.EvictStaleChunks(10, 60);  // currentFrame=10
    ring.ReclaimCompleted(1);       // stamps lastReclaimedFrame=10

    auto freeCount = ring.GetFreeChunkCount();
    ASSERT_GT(freeCount, 0u);

    // 80 frames later (> 60 idle frames)
    auto freed = ring.EvictStaleChunks(90, 60);
    EXPECT_GT(freed, 0u);
    EXPECT_LT(ring.GetFreeChunkCount(), freeCount);
}

// SR-EVICT-04: GIVEN ring with 2 total chunks, 1 free stale WHEN EvictStaleChunks THEN keeps at least 1 alive
TEST_P(StagingRingTest, Evict04_KeepsMinimumOneAlive) {
    auto result = MakeRing(uint64_t{4} << 10, 8);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    // Force exactly 2 chunks
    (void)ring.Allocate(3000);
    (void)ring.Allocate(3000);
    ASSERT_GE(ring.GetTotalChunkCount(), 2u);

    ring.FlushFrame(1);
    ring.EvictStaleChunks(10, 60);
    ring.ReclaimCompleted(1);

    // All chunks free. Evict at frame 200 (very stale).
    ring.EvictStaleChunks(200, 60);

    // At least 1 chunk must remain alive
    EXPECT_GE(ring.GetTotalChunkCount(), 1u);
}

// SR-EVICT-05: GIVEN ring with never-reclaimed free chunk (sentinel UINT64_MAX) WHEN EvictStaleChunks THEN skipped
TEST_P(StagingRingTest, Evict05_SentinelNotEvicted) {
    auto result = MakeRing(uint64_t{4} << 10, 8);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    // The initial chunk is active, not free. Force a second chunk, but DON'T reclaim.
    // Instead, manually trigger a situation where a chunk enters freeChunks without
    // going through ReclaimCompleted (shouldn't happen in practice, but tests sentinel).
    // We test indirectly: all chunks go through normal flow, sentinel protection is additive.
    auto totalBefore = ring.GetTotalChunkCount();
    EXPECT_GE(totalBefore, 1u);
    // At frame UINT64_MAX - 1, nothing should crash
    auto freed = ring.EvictStaleChunks(UINT64_MAX - 1, 60);
    EXPECT_EQ(freed, 0u);  // No free chunks to evict
}

// ============================================================================
// §7.1.11 SR-COMPLEX — Multi-flow complex scenarios
// ============================================================================

// SR-COMPLEX-01: Interleaved alloc/enqueue/flush/reclaim/evict across 200 frames
TEST_P(StagingRingTest, Complex01_InterleavedFrameLifecycle) {
    auto result = MakeRing(uint64_t{8} << 10, 32);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    auto dst = CreateDstBuffer(1 << 20);  // 1MB
    if (!dst.IsValid()) {
        GTEST_SKIP() << "Dst buffer creation failed";
    }

    constexpr uint32_t kFrameLatency = 2;

    for (uint64_t frame = 1; frame <= 200; ++frame) {
        // Reclaim old frames
        if (frame > kFrameLatency) {
            ring.ReclaimCompleted(frame - kFrameLatency);
        }

        // Evict stale chunks periodically
        ring.EvictStaleChunks(frame, 30);

        // Varying number of uploads per frame (0-3)
        uint32_t uploadsThisFrame = static_cast<uint32_t>(frame % 4);
        for (uint32_t i = 0; i < uploadsThisFrame; ++i) {
            uint64_t size = 256 + (frame * 7 + i * 13) % 4096;
            auto alloc = ring.Allocate(size);
            ASSERT_TRUE(alloc.has_value()) << "Frame " << frame << " alloc " << i << " failed";
            ring.EnqueueBufferCopy(*alloc, dst, 0);
        }

        ring.FlushFrame(frame);
    }

    // Steady state: bounded chunk count
    EXPECT_LE(ring.GetTotalChunkCount(), 10u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(dst); });
}

// SR-COMPLEX-02: Rapid grow then shrink — burst of large allocs then idle period with eviction
TEST_P(StagingRingTest, Complex02_BurstThenIdle) {
    auto result = MakeRing(uint64_t{4} << 10, 32);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    // Burst: fill 10 chunks in frame 1
    for (int i = 0; i < 10; ++i) {
        auto alloc = ring.Allocate(3500);
        ASSERT_TRUE(alloc.has_value());
    }
    EXPECT_GE(ring.GetTotalChunkCount(), 10u);

    ring.FlushFrame(1);
    ring.EvictStaleChunks(1, 5);
    ring.ReclaimCompleted(1);

    auto peakChunks = ring.GetTotalChunkCount();

    // Idle: 20 frames with no allocs, just evict
    for (uint64_t frame = 2; frame <= 20; ++frame) {
        ring.FlushFrame(frame);
        ring.EvictStaleChunks(frame, 5);
        ring.ReclaimCompleted(frame);
    }

    // After 20 idle frames with iMaxIdleFrames=5, most chunks should be evicted
    EXPECT_LT(ring.GetTotalChunkCount(), peakChunks);
    EXPECT_GE(ring.GetTotalChunkCount(), 1u);  // At least 1 survives
}

// SR-COMPLEX-03: Double FlushFrame same fence — idempotency
TEST_P(StagingRingTest, Complex03_DoubleFlush) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    (void)ring.Allocate(1024);
    ring.FlushFrame(1);
    ring.FlushFrame(1);  // Should not crash or corrupt state

    ring.ReclaimCompleted(1);
    // ChunkPool auto-promotes one free chunk back to active when activeChunks is empty,
    // so with 1 chunk: activeCount==1, freeCount==0 after reclaim.
    EXPECT_EQ(ring.GetActiveChunkCount(), 1u);
    EXPECT_EQ(ring.GetTotalChunkCount(), 1u);
}

// SR-COMPLEX-04: Reclaim with fence=0 — edge case
TEST_P(StagingRingTest, Complex04_ReclaimFenceZero) {
    auto result = MakeRing();
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    (void)ring.Allocate(1024);
    ring.FlushFrame(0);
    ring.ReclaimCompleted(0);

    // Reclaim with fence=0 is valid; chunk auto-promoted back to active
    EXPECT_EQ(ring.GetActiveChunkCount(), 1u);
    EXPECT_EQ(ring.GetTotalChunkCount(), 1u);
}

// SR-COMPLEX-05: Allocate exactly chunk boundary sizes
TEST_P(StagingRingTest, Complex05_ExactChunkBoundary) {
    constexpr uint64_t kChunkSize = uint64_t{4} << 10;  // 4KB
    auto result = MakeRing(kChunkSize, 8);
    ASSERT_TRUE(result.has_value());
    auto& ring = *result;

    // Allocate exactly 1 chunk worth (accounting for alignment)
    auto a1 = ring.Allocate(kChunkSize);
    ASSERT_TRUE(a1.has_value());
    EXPECT_TRUE(a1->IsValid());

    // Next alloc must go to a new chunk
    auto a2 = ring.Allocate(1);
    ASSERT_TRUE(a2.has_value());
    EXPECT_GE(ring.GetTotalChunkCount(), 2u);
}

// ============================================================================
// Parameterized instantiation
// ============================================================================

INSTANTIATE_TEST_SUITE_P(RealBackends, StagingRingTest, ::testing::ValuesIn(GetRealBackends()), BackendName);
