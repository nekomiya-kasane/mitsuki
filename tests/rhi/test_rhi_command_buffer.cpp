// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// §7 CommandBuffer System tests.
// Covers: CommandBuffer creation/destruction per queue type,
// CommandBufferDesc struct validation, bulk allocation stress.
//
// NOTE: Command recording tests (barriers, copy, draw, dispatch, dynamic rendering,
// debug labels, viewport/scissor, push constants) require a CommandListHandle or
// backend-specific access to the concrete CRTP command buffer. DeviceBase currently
// only exposes CreateCommandBuffer/DestroyCommandBuffer returning opaque
// CommandBufferHandle. Recording tests are deferred until a public
// GetCommandListHandle(CommandBufferHandle) API is added to DeviceBase.

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

TEST_P(RhiCommandBufferTest, CreateGraphicsCommandBuffer) {
    CommandBufferDesc desc{.type = QueueType::Graphics};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateCommandBuffer(desc); });
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());
    Dev().Dispatch([&](auto& dev) { dev.DestroyCommandBuffer(*result); });
}

TEST_P(RhiCommandBufferTest, CreateComputeCommandBuffer) {
    CommandBufferDesc desc{.type = QueueType::Compute};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateCommandBuffer(desc); });
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());
    Dev().Dispatch([&](auto& dev) { dev.DestroyCommandBuffer(*result); });
}

TEST_P(RhiCommandBufferTest, CreateTransferCommandBuffer) {
    CommandBufferDesc desc{.type = QueueType::Transfer};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateCommandBuffer(desc); });
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());
    Dev().Dispatch([&](auto& dev) { dev.DestroyCommandBuffer(*result); });
}

TEST_P(RhiCommandBufferTest, Create20CommandBuffers) {
    std::vector<CommandBufferHandle> cbs;
    for (int i = 0; i < 20; ++i) {
        CommandBufferDesc desc{.type = QueueType::Graphics};
        auto r = Dev().Dispatch([&](auto& dev) { return dev.CreateCommandBuffer(desc); });
        ASSERT_TRUE(r.has_value()) << "Failed at cb " << i;
        cbs.push_back(*r);
    }
    for (auto& cb : cbs) {
        Dev().Dispatch([&](auto& dev) { dev.DestroyCommandBuffer(cb); });
    }
}

TEST_P(RhiCommandBufferTest, DestroyInvalidCBSilent) {
    CommandBufferHandle invalid{};
    Dev().Dispatch([&](auto& dev) { dev.DestroyCommandBuffer(invalid); });
}

TEST_P(RhiCommandBufferTest, DoubleDestroySilent) {
    CommandBufferDesc desc{.type = QueueType::Graphics};
    auto cb = Dev().Dispatch([&](auto& dev) { return dev.CreateCommandBuffer(desc); });
    ASSERT_TRUE(cb.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyCommandBuffer(*cb); });
    Dev().Dispatch([&](auto& dev) { dev.DestroyCommandBuffer(*cb); });
}

TEST_P(RhiCommandBufferTest, CreateAllQueueTypes) {
    for (auto qt : {QueueType::Graphics, QueueType::Compute, QueueType::Transfer}) {
        CommandBufferDesc desc{.type = qt};
        auto r = Dev().Dispatch([&](auto& dev) { return dev.CreateCommandBuffer(desc); });
        ASSERT_TRUE(r.has_value()) << "Failed for QueueType " << static_cast<int>(qt);
        Dev().Dispatch([&](auto& dev) { dev.DestroyCommandBuffer(*r); });
    }
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
