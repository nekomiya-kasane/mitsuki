// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// §7 CommandBuffer System tests.
// Covers: CommandPool creation/destruction, pool-level allocation/free per queue type,
// CommandBufferDesc struct validation, bulk allocation stress.
//
// All command buffer lifecycle tests use the pool-level API:
//   CreateCommandPool -> AllocateFromPool -> FreeFromPool -> ResetCommandPool -> DestroyCommandPool
// The old 1:1 CreateCommandBuffer/DestroyCommandBuffer API has been removed.

#include <gtest/gtest.h>

#include "RhiTestFixture.h"

#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Sync.h"

using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// §7.0 CommandBufferDesc — pure CPU struct validation
// ============================================================================

TEST(CommandBufferDescStruct, DefaultsToGraphics) {
    CommandBufferDesc desc{};
    EXPECT_EQ(desc.type, QueueType::Graphics);
    EXPECT_FALSE(desc.secondary);
}

TEST(CommandBufferDescStruct, SecondaryFlag) {
    CommandBufferDesc desc{.type = QueueType::Compute, .secondary = true};
    EXPECT_EQ(desc.type, QueueType::Compute);
    EXPECT_TRUE(desc.secondary);
}

// ============================================================================
// §7.0.1 Barrier Desc — pure CPU struct validation
// ============================================================================

TEST(BarrierDescCB, BufferBarrierDefaultsValid) {
    BufferBarrierDesc desc{};
    EXPECT_EQ(desc.srcStage, PipelineStage::TopOfPipe);
    EXPECT_EQ(desc.dstStage, PipelineStage::BottomOfPipe);
    EXPECT_EQ(desc.srcAccess, AccessFlags::None);
    EXPECT_EQ(desc.dstAccess, AccessFlags::None);
    EXPECT_EQ(desc.offset, 0u);
    EXPECT_EQ(desc.size, kWholeSize);
}

TEST(BarrierDescCB, TextureBarrierDefaultsValid) {
    TextureBarrierDesc desc{};
    EXPECT_EQ(desc.oldLayout, TextureLayout::Undefined);
    EXPECT_EQ(desc.newLayout, TextureLayout::Undefined);
    EXPECT_EQ(desc.srcQueue, QueueType::Graphics);
    EXPECT_EQ(desc.dstQueue, QueueType::Graphics);
}

TEST(BarrierDescCB, BufferTextureCopyRegionDefaults) {
    BufferTextureCopyRegion region{};
    EXPECT_EQ(region.bufferOffset, 0u);
    EXPECT_EQ(region.bufferRowLength, 0u);
    EXPECT_EQ(region.bufferImageHeight, 0u);
    EXPECT_EQ(region.subresource.baseMipLevel, 0u);
    EXPECT_EQ(region.subresource.mipLevelCount, 1u);
    EXPECT_EQ(region.subresource.baseArrayLayer, 0u);
    EXPECT_EQ(region.subresource.arrayLayerCount, 1u);
}

// ============================================================================
// §7.1 CommandBuffer Lifecycle — Creation/Destruction
// ============================================================================

class RhiCommandBufferTest : public RhiTest {};

// Helper: create a command pool for a given queue type
static auto CreatePool(DeviceHandle& dev, QueueType qt) {
    CommandPoolDesc poolDesc{.queue = qt, .transient = false};
    return dev.Dispatch([&](auto& d) { return d.CreateCommandPool(poolDesc); });
}

TEST_P(RhiCommandBufferTest, PoolCreateAndDestroy) {
    auto pool = CreatePool(Dev(), QueueType::Graphics);
    ASSERT_TRUE(pool.has_value());
    EXPECT_TRUE(pool->IsValid());
    Dev().Dispatch([&](auto& dev) { dev.DestroyCommandPool(*pool); });
}

TEST_P(RhiCommandBufferTest, ResetPoolReclaims) {
    auto pool = CreatePool(Dev(), QueueType::Graphics);
    ASSERT_TRUE(pool.has_value());
    auto acq = Dev().Dispatch([&](auto& dev) { return dev.AllocateFromPool(*pool, false); });
    ASSERT_TRUE(acq.has_value());
    // ResetCommandPool reclaims all allocations — no individual Free needed
    Dev().Dispatch([&](auto& dev) { dev.ResetCommandPool(*pool); });
    // Can allocate again after reset
    auto acq2 = Dev().Dispatch([&](auto& dev) { return dev.AllocateFromPool(*pool, false); });
    ASSERT_TRUE(acq2.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyCommandPool(*pool); });
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiCommandBufferTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);

// ============================================================================
// §7.2 SubmitDesc — struct validation (pure CPU)
// ============================================================================

TEST(SubmitDescStruct, DefaultsValid) {
    SubmitDesc desc{};
    EXPECT_TRUE(desc.commandBuffers.empty());
    EXPECT_TRUE(desc.waitSemaphores.empty());
    EXPECT_TRUE(desc.signalSemaphores.empty());
    EXPECT_FALSE(desc.signalFence.IsValid());
}

TEST(SubmitDescStruct, SemaphoreSubmitInfoUseStageMask) {
    SemaphoreSubmitInfo info{};
    info.semaphore = SemaphoreHandle{.value = 1};
    info.value = 42;
    info.stageMask = PipelineStage::ComputeShader;
    EXPECT_EQ(info.stageMask, PipelineStage::ComputeShader);
    EXPECT_EQ(info.value, 42u);
}

// ============================================================================
// §7.3 Submit — empty submit (GPU sync point)
// ============================================================================

class RhiSubmitTest : public RhiTest {};

TEST_P(RhiSubmitTest, EmptySubmitSucceeds) {
    // Submit with no command buffers — valid as a GPU sync point
    SubmitDesc desc{};
    Dev().Dispatch([&](auto& dev) { dev.Submit(QueueType::Graphics, desc); });
}

TEST_P(RhiSubmitTest, SubmitWithFenceSignal) {
    auto fence = Dev().Dispatch([&](auto& dev) { return dev.CreateFence(false); });
    ASSERT_TRUE(fence.has_value());

    SubmitDesc desc{};
    desc.signalFence = *fence;
    Dev().Dispatch([&](auto& dev) { dev.Submit(QueueType::Graphics, desc); });
    Dev().Dispatch([&](auto& dev) { dev.WaitFence(*fence, UINT64_MAX); });

    auto status = Dev().Dispatch([&](auto& dev) { return dev.GetFenceStatus(*fence); });
    EXPECT_TRUE(status);

    Dev().Dispatch([&](auto& dev) { dev.DestroyFence(*fence); });
}

TEST_P(RhiSubmitTest, WaitIdleSucceeds) {
    Dev().Dispatch([&](auto& dev) { dev.WaitIdle(); });
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiSubmitTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);
