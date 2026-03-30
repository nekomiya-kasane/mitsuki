// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// RHI Integration tests — complex call sequences exercising multiple subsystems.
// Covers: full resource lifecycle, descriptor binding chain,
// submit with sync, multi-queue timeline semaphore operations,
// deferred destruction patterns, stress tests for handle pool.
//
// All command buffer lifecycle tests use the pool-level API:
//   CreateCommandPool -> AllocateFromPool -> FreeFromPool -> DestroyCommandPool
// The old 1:1 CreateCommandBuffer/DestroyCommandBuffer API has been removed.

#include <gtest/gtest.h>

#include <vector>

#include "RhiTestFixture.h"

#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Descriptors.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Pipeline.h"
#include "miki/rhi/Resources.h"
#include "miki/rhi/Sync.h"

using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// Integration: Full Resource Lifecycle (create -> destroy)
// ============================================================================

class RhiIntegrationTest : public RhiTest {};

TEST_P(RhiIntegrationTest, BufferStagingLifecycle) {
    // Create staging buffer (CPU-visible) + GPU buffer
    BufferDesc stagingDesc{.size = 1024, .usage = BufferUsage::TransferSrc, .memory = MemoryLocation::CpuToGpu};
    auto staging = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(stagingDesc); });
    ASSERT_TRUE(staging.has_value());

    BufferDesc gpuDesc{
        .size = 1024, .usage = BufferUsage::Vertex | BufferUsage::TransferDst, .memory = MemoryLocation::GpuOnly
    };
    auto gpu = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(gpuDesc); });
    ASSERT_TRUE(gpu.has_value());

    // Map staging buffer, write data
    auto mapped = Dev().Dispatch([&](auto& dev) { return dev.MapBuffer(*staging); });
    if (mapped.has_value()) {
        std::memset(*mapped, 0xAB, 1024);
        Dev().Dispatch([&](auto& dev) { dev.UnmapBuffer(*staging); });
    }

    // Create command pool + allocate from pool for transfer
    CommandPoolDesc poolDesc{.queue = QueueType::Transfer, .transient = false};
    auto pool = Dev().Dispatch([&](auto& dev) { return dev.CreateCommandPool(poolDesc); });
    ASSERT_TRUE(pool.has_value());
    auto cb = Dev().Dispatch([&](auto& dev) { return dev.AllocateFromPool(*pool, false); });
    ASSERT_TRUE(cb.has_value());

    auto fence = Dev().Dispatch([&](auto& dev) { return dev.CreateFence(false); });
    ASSERT_TRUE(fence.has_value());

    // Submit empty (command recording not available through DeviceBase) with fence
    SubmitDesc submitDesc{.signalFence = *fence};
    Dev().Dispatch([&](auto& dev) { dev.Submit(QueueType::Transfer, submitDesc); });
    Dev().Dispatch([&](auto& dev) { dev.WaitFence(*fence, UINT64_MAX); });

    // Cleanup
    Dev().Dispatch([&](auto& dev) {
        dev.DestroyFence(*fence);
        dev.DestroyCommandPool(*pool);
        dev.DestroyBuffer(*staging);
        dev.DestroyBuffer(*gpu);
    });
}

TEST_P(RhiIntegrationTest, TextureWithViewLifecycle) {
    constexpr uint32_t kWidth = 64, kHeight = 64;

    TextureDesc texDesc{
        .dimension = TextureDimension::Tex2D,
        .format = Format::RGBA8_UNORM,
        .width = kWidth,
        .height = kHeight,
        .usage = TextureUsage::Sampled | TextureUsage::TransferDst,
    };
    auto tex = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(texDesc); });
    ASSERT_TRUE(tex.has_value());

    TextureViewDesc viewDesc{.texture = *tex};
    auto view = Dev().Dispatch([&](auto& dev) { return dev.CreateTextureView(viewDesc); });
    ASSERT_TRUE(view.has_value());

    // Destroy view before texture (correct order)
    Dev().Dispatch([&](auto& dev) {
        dev.DestroyTextureView(*view);
        dev.DestroyTexture(*tex);
    });
}

// ============================================================================
// Integration: Descriptor Binding Chain
// ============================================================================

TEST_P(RhiIntegrationTest, DescriptorBindingFullChain) {
    // Create UBO
    BufferDesc uboDesc{.size = 256, .usage = BufferUsage::Uniform, .memory = MemoryLocation::CpuToGpu};
    auto ubo = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(uboDesc); });
    ASSERT_TRUE(ubo.has_value());

    // Create descriptor layout
    BindingDesc binding{.binding = 0, .type = BindingType::UniformBuffer, .stages = ShaderStage::All, .count = 1};
    DescriptorLayoutDesc ld{.bindings = std::span{&binding, 1}};
    auto layout = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(ld); });
    ASSERT_TRUE(layout.has_value());

    // Create descriptor set with initial write
    DescriptorWrite write{.binding = 0, .resource = BufferBinding{.buffer = *ubo, .offset = 0, .range = 256}};
    DescriptorSetDesc dsDesc{.layout = *layout, .writes = std::span{&write, 1}};
    auto ds = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorSet(dsDesc); });
    ASSERT_TRUE(ds.has_value());

    // Update descriptor set with different offset
    DescriptorWrite update{.binding = 0, .resource = BufferBinding{.buffer = *ubo, .offset = 0, .range = 128}};
    Dev().Dispatch([&](auto& dev) { dev.UpdateDescriptorSet(*ds, std::span{&update, 1}); });

    // Create pipeline layout referencing the descriptor layout
    PipelineLayoutDesc plDesc{.setLayouts = std::span{&*layout, 1}};
    auto pipelineLayout = Dev().Dispatch([&](auto& dev) { return dev.CreatePipelineLayout(plDesc); });
    ASSERT_TRUE(pipelineLayout.has_value());

    // Cleanup in reverse dependency order
    Dev().Dispatch([&](auto& dev) {
        dev.DestroyPipelineLayout(*pipelineLayout);
        dev.DestroyDescriptorSet(*ds);
        dev.DestroyDescriptorLayout(*layout);
        dev.DestroyBuffer(*ubo);
    });
}

// ============================================================================
// Integration: Timeline Semaphore Multi-Queue Sync
// ============================================================================

TEST_P(RhiIntegrationTest, TimelineSemaphoreTransferToGraphics) {
    RequireFeature(DeviceFeature::TimelineSemaphore);

    // Create timeline semaphore
    SemaphoreDesc semDesc{.type = SemaphoreType::Timeline, .initialValue = 0};
    auto sem = Dev().Dispatch([&](auto& dev) { return dev.CreateSemaphore(semDesc); });
    ASSERT_TRUE(sem.has_value());

    // Create resources
    BufferDesc srcDesc{.size = 256, .usage = BufferUsage::TransferSrc, .memory = MemoryLocation::CpuToGpu};
    BufferDesc dstDesc{
        .size = 256, .usage = BufferUsage::TransferDst | BufferUsage::Uniform, .memory = MemoryLocation::GpuOnly
    };
    auto src = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(srcDesc); });
    auto dst = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(dstDesc); });
    ASSERT_TRUE(src.has_value() && dst.has_value());

    // Transfer command pool + allocation
    CommandPoolDesc transferPoolDesc{.queue = QueueType::Transfer, .transient = false};
    auto transferPool = Dev().Dispatch([&](auto& dev) { return dev.CreateCommandPool(transferPoolDesc); });
    ASSERT_TRUE(transferPool.has_value());
    auto transferCb = Dev().Dispatch([&](auto& dev) { return dev.AllocateFromPool(*transferPool, false); });
    ASSERT_TRUE(transferCb.has_value());

    // Submit transfer with signal semaphore value=1
    SemaphoreSubmitInfo signalInfo{.semaphore = *sem, .value = 1, .stageMask = PipelineStage::Transfer};
    SubmitDesc transferSubmit{.signalSemaphores = std::span{&signalInfo, 1}};
    Dev().Dispatch([&](auto& dev) { dev.Submit(QueueType::Transfer, transferSubmit); });

    // Graphics command pool + allocation (waits on semaphore value=1)
    CommandPoolDesc gfxPoolDesc{.queue = QueueType::Graphics, .transient = false};
    auto gfxPool = Dev().Dispatch([&](auto& dev) { return dev.CreateCommandPool(gfxPoolDesc); });
    ASSERT_TRUE(gfxPool.has_value());
    auto gfxCb = Dev().Dispatch([&](auto& dev) { return dev.AllocateFromPool(*gfxPool, false); });
    ASSERT_TRUE(gfxCb.has_value());

    // Submit graphics with wait semaphore value=1, signal value=2
    SemaphoreSubmitInfo waitInfo{.semaphore = *sem, .value = 1, .stageMask = PipelineStage::VertexShader};
    SemaphoreSubmitInfo signalInfo2{.semaphore = *sem, .value = 2, .stageMask = PipelineStage::BottomOfPipe};
    auto fence = Dev().Dispatch([&](auto& dev) { return dev.CreateFence(false); });
    ASSERT_TRUE(fence.has_value());

    SubmitDesc gfxSubmit{
        .waitSemaphores = std::span{&waitInfo, 1},
        .signalSemaphores = std::span{&signalInfo2, 1},
        .signalFence = *fence,
    };
    Dev().Dispatch([&](auto& dev) { dev.Submit(QueueType::Graphics, gfxSubmit); });
    Dev().Dispatch([&](auto& dev) { dev.WaitFence(*fence, UINT64_MAX); });

    // Verify timeline reached value 2
    auto val = Dev().Dispatch([&](auto& dev) { return dev.GetSemaphoreValue(*sem); });
    EXPECT_GE(val, 2u);

    // Cleanup
    Dev().Dispatch([&](auto& dev) {
        dev.DestroyFence(*fence);
        dev.DestroyCommandPool(*transferPool);
        dev.DestroyCommandPool(*gfxPool);
        dev.DestroyBuffer(*src);
        dev.DestroyBuffer(*dst);
        dev.DestroySemaphore(*sem);
    });
}

// ============================================================================
// Integration: Fence-Based Submit Ordering
// ============================================================================

TEST_P(RhiIntegrationTest, SequentialSubmitWithFences) {
    auto fence1 = Dev().Dispatch([&](auto& dev) { return dev.CreateFence(false); });
    auto fence2 = Dev().Dispatch([&](auto& dev) { return dev.CreateFence(false); });
    ASSERT_TRUE(fence1.has_value() && fence2.has_value());

    // Submit 1 with fence1
    SubmitDesc sub1{.signalFence = *fence1};
    Dev().Dispatch([&](auto& dev) { dev.Submit(QueueType::Graphics, sub1); });

    // Wait for first submit
    Dev().Dispatch([&](auto& dev) { dev.WaitFence(*fence1, UINT64_MAX); });
    auto status1 = Dev().Dispatch([&](auto& dev) { return dev.GetFenceStatus(*fence1); });
    EXPECT_TRUE(status1);

    // Submit 2 with fence2
    SubmitDesc sub2{.signalFence = *fence2};
    Dev().Dispatch([&](auto& dev) { dev.Submit(QueueType::Graphics, sub2); });
    Dev().Dispatch([&](auto& dev) { dev.WaitFence(*fence2, UINT64_MAX); });

    Dev().Dispatch([&](auto& dev) {
        dev.DestroyFence(*fence1);
        dev.DestroyFence(*fence2);
    });
}

// ============================================================================
// Integration: Stress — Rapid Create/Destroy Cycles
// ============================================================================

TEST_P(RhiIntegrationTest, RapidBufferCreateDestroy500) {
    for (int i = 0; i < 500; ++i) {
        BufferDesc desc{.size = 256, .usage = BufferUsage::Uniform, .memory = MemoryLocation::CpuToGpu};
        auto buf = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
        ASSERT_TRUE(buf.has_value()) << "Failed at iteration " << i;
        Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(*buf); });
    }
}

TEST_P(RhiIntegrationTest, MixedResourceCreateDestroy) {
    for (int i = 0; i < 50; ++i) {
        BufferDesc bufDesc{.size = 512, .usage = BufferUsage::Storage, .memory = MemoryLocation::GpuOnly};
        auto buf = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(bufDesc); });
        ASSERT_TRUE(buf.has_value());

        TextureDesc texDesc{
            .dimension = TextureDimension::Tex2D,
            .format = Format::RGBA8_UNORM,
            .width = 32,
            .height = 32,
            .usage = TextureUsage::Sampled,
        };
        auto tex = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(texDesc); });
        ASSERT_TRUE(tex.has_value());

        SamplerDesc sampDesc{};
        auto samp = Dev().Dispatch([&](auto& dev) { return dev.CreateSampler(sampDesc); });
        ASSERT_TRUE(samp.has_value());

        Dev().Dispatch([&](auto& dev) {
            dev.DestroySampler(*samp);
            dev.DestroyTexture(*tex);
            dev.DestroyBuffer(*buf);
        });
    }
}

TEST_P(RhiIntegrationTest, BulkPoolAllocateAndFree) {
    // Create one pool per queue type, allocate multiple from each
    for (auto qt : {QueueType::Graphics, QueueType::Compute, QueueType::Transfer}) {
        CommandPoolDesc poolDesc{.queue = qt, .transient = false};
        auto pool = Dev().Dispatch([&](auto& dev) { return dev.CreateCommandPool(poolDesc); });
        ASSERT_TRUE(pool.has_value());
        std::vector<CommandListAcquisition> acqs;
        for (int i = 0; i < 33; ++i) {
            auto acq = Dev().Dispatch([&](auto& dev) { return dev.AllocateFromPool(*pool, false); });
            ASSERT_TRUE(acq.has_value()) << "Failed at allocation " << i << " for QueueType " << static_cast<int>(qt);
            acqs.push_back(*acq);
        }
        for (auto& a : acqs) {
            Dev().Dispatch([&](auto& dev) { dev.FreeFromPool(*pool, a); });
        }
        Dev().Dispatch([&](auto& dev) { dev.DestroyCommandPool(*pool); });
    }
}

// ============================================================================
// Integration: WaitIdle
// ============================================================================

TEST_P(RhiIntegrationTest, WaitIdleAfterSubmit) {
    // Submit empty batches then WaitIdle
    SubmitDesc sub{};
    Dev().Dispatch([&](auto& dev) { dev.Submit(QueueType::Graphics, sub); });
    Dev().Dispatch([&](auto& dev) { dev.Submit(QueueType::Compute, sub); });

    // WaitIdle should drain all queues
    Dev().Dispatch([&](auto& dev) { dev.WaitIdle(); });
}

// ============================================================================
// Integration: Debug Name on Resource (via BufferDesc.debugName)
// ============================================================================

TEST_P(RhiIntegrationTest, CreateBufferWithDebugName) {
    BufferDesc desc{
        .size = 64, .usage = BufferUsage::Uniform, .memory = MemoryLocation::CpuToGpu, .debugName = "NamedBuffer"
    };
    auto buf = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
    ASSERT_TRUE(buf.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(*buf); });
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiIntegrationTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);
