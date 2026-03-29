// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// §9 Synchronization Primitives tests.
// Covers: Fence create/wait/reset/status, Semaphore (binary + timeline),
// timeline CPU signal/wait/getValue, barrier descriptor validation.

#include <gtest/gtest.h>

#include "RhiTestFixture.h"

#include "miki/rhi/Device.h"
#include "miki/rhi/Sync.h"

using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// §9.1 Fence (CPU-GPU)
// ============================================================================

class RhiFenceTest : public RhiTest {};

TEST_P(RhiFenceTest, CreateUnsignaledFence) {
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateFence(false); });
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());
    Dev().Dispatch([&](auto& dev) { dev.DestroyFence(*result); });
}

TEST_P(RhiFenceTest, CreateSignaledFence) {
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateFence(true); });
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());

    // Signaled fence: GetFenceStatus should return true
    auto status = Dev().Dispatch([&](auto& dev) { return dev.GetFenceStatus(*result); });
    EXPECT_TRUE(status);

    Dev().Dispatch([&](auto& dev) { dev.DestroyFence(*result); });
}

TEST_P(RhiFenceTest, UnsignaledFenceStatusIsFalse) {
    auto fence = Dev().Dispatch([&](auto& dev) { return dev.CreateFence(false); });
    ASSERT_TRUE(fence.has_value());

    auto status = Dev().Dispatch([&](auto& dev) { return dev.GetFenceStatus(*fence); });
    EXPECT_FALSE(status);

    Dev().Dispatch([&](auto& dev) { dev.DestroyFence(*fence); });
}

TEST_P(RhiFenceTest, ResetSignaledFence) {
    auto fence = Dev().Dispatch([&](auto& dev) { return dev.CreateFence(true); });
    ASSERT_TRUE(fence.has_value());

    Dev().Dispatch([&](auto& dev) { dev.ResetFence(*fence); });
    auto status = Dev().Dispatch([&](auto& dev) { return dev.GetFenceStatus(*fence); });
    EXPECT_FALSE(status);

    Dev().Dispatch([&](auto& dev) { dev.DestroyFence(*fence); });
}

TEST_P(RhiFenceTest, WaitSignaledFenceReturnsImmediately) {
    auto fence = Dev().Dispatch([&](auto& dev) { return dev.CreateFence(true); });
    ASSERT_TRUE(fence.has_value());

    // Wait with timeout=0 on signaled fence should return immediately
    Dev().Dispatch([&](auto& dev) { dev.WaitFence(*fence, 0); });

    Dev().Dispatch([&](auto& dev) { dev.DestroyFence(*fence); });
}

TEST_P(RhiFenceTest, WaitUnsignaledFenceTimesOut) {
    auto fence = Dev().Dispatch([&](auto& dev) { return dev.CreateFence(false); });
    ASSERT_TRUE(fence.has_value());

    // Wait with timeout=0 on unsignaled fence should return immediately (timeout)
    Dev().Dispatch([&](auto& dev) { dev.WaitFence(*fence, 0); });

    Dev().Dispatch([&](auto& dev) { dev.DestroyFence(*fence); });
}

TEST_P(RhiFenceTest, Create10Fences) {
    std::vector<FenceHandle> fences;
    for (int i = 0; i < 10; ++i) {
        auto f = Dev().Dispatch([&](auto& dev) { return dev.CreateFence(false); });
        ASSERT_TRUE(f.has_value()) << "Failed at fence " << i;
        fences.push_back(*f);
    }
    for (auto& f : fences) {
        Dev().Dispatch([&](auto& dev) { dev.DestroyFence(f); });
    }
}

TEST_P(RhiFenceTest, DestroyInvalidFenceSilent) {
    FenceHandle invalid{};
    Dev().Dispatch([&](auto& dev) { dev.DestroyFence(invalid); });
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiFenceTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);

// ============================================================================
// §9.2 Semaphore (GPU-GPU)
// ============================================================================

class RhiSemaphoreTest : public RhiTest {};

TEST_P(RhiSemaphoreTest, CreateBinarySemaphore) {
    SemaphoreDesc desc{.type = SemaphoreType::Binary};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateSemaphore(desc); });
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());
    Dev().Dispatch([&](auto& dev) { dev.DestroySemaphore(*result); });
}

TEST_P(RhiSemaphoreTest, CreateTimelineSemaphore) {
    RequireFeature(DeviceFeature::TimelineSemaphore);

    SemaphoreDesc desc{.type = SemaphoreType::Timeline, .initialValue = 0};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateSemaphore(desc); });
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());
    Dev().Dispatch([&](auto& dev) { dev.DestroySemaphore(*result); });
}

TEST_P(RhiSemaphoreTest, TimelineInitialValueCorrect) {
    RequireFeature(DeviceFeature::TimelineSemaphore);

    SemaphoreDesc desc{.type = SemaphoreType::Timeline, .initialValue = 42};
    auto sem = Dev().Dispatch([&](auto& dev) { return dev.CreateSemaphore(desc); });
    ASSERT_TRUE(sem.has_value());

    auto val = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(*sem); });
    EXPECT_EQ(val, 42u);

    Dev().Dispatch([&](auto& dev) { dev.DestroySemaphore(*sem); });
}

TEST_P(RhiSemaphoreTest, TimelineCpuSignal) {
    RequireFeature(DeviceFeature::TimelineSemaphore);

    SemaphoreDesc desc{.type = SemaphoreType::Timeline, .initialValue = 0};
    auto sem = Dev().Dispatch([&](auto& dev) { return dev.CreateSemaphore(desc); });
    ASSERT_TRUE(sem.has_value());

    Dev().Dispatch([&](auto& dev) { dev.SignalSemaphore(*sem, 10); });
    auto val = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(*sem); });
    EXPECT_GE(val, 10u);

    Dev().Dispatch([&](auto& dev) { dev.DestroySemaphore(*sem); });
}

TEST_P(RhiSemaphoreTest, TimelineCpuWaitAlreadySignaled) {
    RequireFeature(DeviceFeature::TimelineSemaphore);

    SemaphoreDesc desc{.type = SemaphoreType::Timeline, .initialValue = 100};
    auto sem = Dev().Dispatch([&](auto& dev) { return dev.CreateSemaphore(desc); });
    ASSERT_TRUE(sem.has_value());

    // Waiting for value <= current should return immediately
    Dev().Dispatch([&](auto& dev) { dev.WaitSemaphore(*sem, 50, 0); });
    Dev().Dispatch([&](auto& dev) { dev.WaitSemaphore(*sem, 100, 0); });

    Dev().Dispatch([&](auto& dev) { dev.DestroySemaphore(*sem); });
}

TEST_P(RhiSemaphoreTest, TimelineMonotonicSignal) {
    RequireFeature(DeviceFeature::TimelineSemaphore);

    SemaphoreDesc desc{.type = SemaphoreType::Timeline, .initialValue = 0};
    auto sem = Dev().Dispatch([&](auto& dev) { return dev.CreateSemaphore(desc); });
    ASSERT_TRUE(sem.has_value());

    for (uint64_t v = 1; v <= 10; ++v) {
        Dev().Dispatch([&](auto& dev) { dev.SignalSemaphore(*sem, v); });
        auto val = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(*sem); });
        EXPECT_GE(val, v);
    }

    Dev().Dispatch([&](auto& dev) { dev.DestroySemaphore(*sem); });
}

TEST_P(RhiSemaphoreTest, Create10BinarySemaphores) {
    std::vector<SemaphoreHandle> sems;
    for (int i = 0; i < 10; ++i) {
        SemaphoreDesc desc{.type = SemaphoreType::Binary};
        auto s = Dev().Dispatch([&](auto& dev) { return dev.CreateSemaphore(desc); });
        ASSERT_TRUE(s.has_value()) << "Failed at semaphore " << i;
        sems.push_back(*s);
    }
    for (auto& s : sems) {
        Dev().Dispatch([&](auto& dev) { dev.DestroySemaphore(s); });
    }
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiSemaphoreTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);

// ============================================================================
// §9.3 Barrier Descriptors — struct validation (pure CPU)
// ============================================================================

TEST(BarrierDesc, BufferBarrierDefaultsValid) {
    BufferBarrierDesc desc{};
    EXPECT_EQ(desc.srcStage, PipelineStage::TopOfPipe);
    EXPECT_EQ(desc.dstStage, PipelineStage::BottomOfPipe);
    EXPECT_EQ(desc.srcAccess, AccessFlags::None);
    EXPECT_EQ(desc.dstAccess, AccessFlags::None);
    EXPECT_EQ(desc.offset, 0u);
    EXPECT_EQ(desc.size, kWholeSize);
}

TEST(BarrierDesc, TextureBarrierDefaultsValid) {
    TextureBarrierDesc desc{};
    EXPECT_EQ(desc.oldLayout, TextureLayout::Undefined);
    EXPECT_EQ(desc.newLayout, TextureLayout::Undefined);
    EXPECT_EQ(desc.srcQueue, QueueType::Graphics);
    EXPECT_EQ(desc.dstQueue, QueueType::Graphics);
}

TEST(BarrierDesc, PipelineBarrierDefaultsValid) {
    PipelineBarrierDesc desc{};
    EXPECT_EQ(desc.srcStage, PipelineStage::TopOfPipe);
    EXPECT_EQ(desc.dstStage, PipelineStage::BottomOfPipe);
}
