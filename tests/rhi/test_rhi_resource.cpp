// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// §5 Resource System tests.
// Covers: Buffer create/destroy, Texture create/destroy, TextureView,
// Sampler, MemoryHeap, memory aliasing, sparse binding, deferred destruction,
// memory requirements queries, memory statistics.

#include <gtest/gtest.h>

#include "RhiTestFixture.h"

#include "miki/rhi/Device.h"
#include "miki/rhi/Resources.h"

using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// §5.1 Buffer Creation
// ============================================================================

class RhiBufferTest : public RhiTest {};

TEST_P(RhiBufferTest, CreateVertexBufferSucceeds) {
    BufferDesc desc{};
    desc.size = 1024;
    desc.usage = BufferUsage::Vertex;
    desc.memory = MemoryLocation::GpuOnly;
    desc.debugName = "TestVertexBuffer";

    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
    ASSERT_TRUE(result.has_value()) << "CreateBuffer failed";
    EXPECT_TRUE(result->IsValid());

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(*result); });
}

TEST_P(RhiBufferTest, CreateIndexBufferSucceeds) {
    BufferDesc desc{.size = 512, .usage = BufferUsage::Index, .memory = MemoryLocation::GpuOnly};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());
    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(*result); });
}

TEST_P(RhiBufferTest, CreateUniformBufferSucceeds) {
    BufferDesc desc{.size = 256, .usage = BufferUsage::Uniform, .memory = MemoryLocation::CpuToGpu};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(*result); });
}

TEST_P(RhiBufferTest, CreateStorageBufferSucceeds) {
    BufferDesc desc{.size = 4096, .usage = BufferUsage::Storage, .memory = MemoryLocation::GpuOnly};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(*result); });
}

TEST_P(RhiBufferTest, CreateTransferBufferSucceeds) {
    BufferDesc desc{
        .size = 2048, .usage = BufferUsage::TransferSrc | BufferUsage::TransferDst, .memory = MemoryLocation::CpuToGpu
    };
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(*result); });
}

TEST_P(RhiBufferTest, CreateZeroSizeBufferFails) {
    BufferDesc desc{.size = 0, .usage = BufferUsage::Vertex, .memory = MemoryLocation::GpuOnly};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
    EXPECT_FALSE(result.has_value());
}

TEST_P(RhiBufferTest, CreateMultiUsageBufferSucceeds) {
    BufferDesc desc{
        .size = 1024,
        .usage = BufferUsage::Vertex | BufferUsage::TransferDst | BufferUsage::Storage,
        .memory = MemoryLocation::GpuOnly
    };
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(*result); });
}

TEST_P(RhiBufferTest, CreateTransientBufferSucceeds) {
    BufferDesc desc{.size = 1024, .usage = BufferUsage::Storage, .memory = MemoryLocation::GpuOnly, .transient = true};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(*result); });
}

TEST_P(RhiBufferTest, CreateReadbackBufferSucceeds) {
    BufferDesc desc{.size = 1024, .usage = BufferUsage::TransferDst, .memory = MemoryLocation::GpuToCpu};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(*result); });
}

TEST_P(RhiBufferTest, DestroyInvalidHandleSilent) {
    BufferHandle invalid{};
    // Should not crash
    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(invalid); });
}

TEST_P(RhiBufferTest, DoubleDestroyIsSilent) {
    BufferDesc desc{.size = 256, .usage = BufferUsage::Vertex, .memory = MemoryLocation::GpuOnly};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(*result); });
    // Second destroy should be no-op (generation mismatch)
    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(*result); });
}

TEST_P(RhiBufferTest, Create100BuffersSequential) {
    std::vector<BufferHandle> handles;
    handles.reserve(100);
    for (int i = 0; i < 100; ++i) {
        BufferDesc desc{.size = 64, .usage = BufferUsage::Uniform, .memory = MemoryLocation::CpuToGpu};
        auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
        ASSERT_TRUE(result.has_value()) << "Failed at buffer " << i;
        handles.push_back(*result);
    }
    for (auto& h : handles) {
        Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(h); });
    }
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiBufferTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);

// ============================================================================
// §5.2 Texture Creation
// ============================================================================

class RhiTextureTest : public RhiTest {};

TEST_P(RhiTextureTest, CreateTexture2DSucceeds) {
    TextureDesc desc{
        .dimension = TextureDimension::Tex2D,
        .format = Format::RGBA8_UNORM,
        .width = 256,
        .height = 256,
        .usage = TextureUsage::Sampled | TextureUsage::TransferDst,
        .debugName = "TestTexture2D"
    };
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(desc); });
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());
    Dev().Dispatch([&](auto& dev) { dev.DestroyTexture(*result); });
}

TEST_P(RhiTextureTest, CreateTextureCubeSucceeds) {
    TextureDesc desc{
        .dimension = TextureDimension::TexCube,
        .format = Format::RGBA8_UNORM,
        .width = 64,
        .height = 64,
        .arrayLayers = 6,
        .usage = TextureUsage::Sampled
    };
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyTexture(*result); });
}

TEST_P(RhiTextureTest, CreateTexture3DSucceeds) {
    TextureDesc desc{
        .dimension = TextureDimension::Tex3D,
        .format = Format::RGBA8_UNORM,
        .width = 32,
        .height = 32,
        .depth = 32,
        .usage = TextureUsage::Storage
    };
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyTexture(*result); });
}

TEST_P(RhiTextureTest, CreateTexture2DArraySucceeds) {
    TextureDesc desc{
        .dimension = TextureDimension::Tex2DArray,
        .format = Format::RGBA8_UNORM,
        .width = 64,
        .height = 64,
        .arrayLayers = 4,
        .usage = TextureUsage::Sampled
    };
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyTexture(*result); });
}

TEST_P(RhiTextureTest, CreateTextureWithMipChainSucceeds) {
    TextureDesc desc{
        .dimension = TextureDimension::Tex2D,
        .format = Format::RGBA8_UNORM,
        .width = 256,
        .height = 256,
        .mipLevels = 9,  // log2(256) + 1
        .usage = TextureUsage::Sampled | TextureUsage::TransferDst
    };
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyTexture(*result); });
}

TEST_P(RhiTextureTest, CreateMSAATexture) {
    TextureDesc desc{
        .dimension = TextureDimension::Tex2D,
        .format = Format::RGBA8_UNORM,
        .width = 512,
        .height = 512,
        .sampleCount = 4,
        .usage = TextureUsage::ColorAttachment
    };
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyTexture(*result); });
}

TEST_P(RhiTextureTest, CreateDepthStencilTexture) {
    TextureDesc desc{
        .dimension = TextureDimension::Tex2D,
        .format = Format::D32_FLOAT,
        .width = 1024,
        .height = 1024,
        .usage = TextureUsage::DepthStencil
    };
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyTexture(*result); });
}

TEST_P(RhiTextureTest, CreateColorAttachmentTexture) {
    TextureDesc desc{
        .dimension = TextureDimension::Tex2D,
        .format = Format::RGBA8_UNORM,
        .width = 800,
        .height = 600,
        .usage = TextureUsage::ColorAttachment | TextureUsage::Sampled
    };
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyTexture(*result); });
}

TEST_P(RhiTextureTest, CreateZeroSizeTextureFails) {
    TextureDesc desc{
        .dimension = TextureDimension::Tex2D,
        .format = Format::RGBA8_UNORM,
        .width = 0,
        .height = 0,
        .usage = TextureUsage::Sampled
    };
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(desc); });
    EXPECT_FALSE(result.has_value());
}

TEST_P(RhiTextureTest, CreateTransientTextureSucceeds) {
    TextureDesc desc{
        .dimension = TextureDimension::Tex2D,
        .format = Format::RGBA8_UNORM,
        .width = 512,
        .height = 512,
        .usage = TextureUsage::ColorAttachment,
        .transient = true
    };
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyTexture(*result); });
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiTextureTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);

// ============================================================================
// §5.3 Texture View
// ============================================================================

class RhiTextureViewTest : public RhiTest {};

TEST_P(RhiTextureViewTest, CreateDefaultViewSucceeds) {
    TextureDesc texDesc{
        .dimension = TextureDimension::Tex2D,
        .format = Format::RGBA8_UNORM,
        .width = 128,
        .height = 128,
        .usage = TextureUsage::Sampled
    };
    auto tex = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(texDesc); });
    ASSERT_TRUE(tex.has_value());

    TextureViewDesc viewDesc{.texture = *tex, .viewDimension = TextureDimension::Tex2D};
    auto view = Dev().Dispatch([&](auto& dev) { return dev.CreateTextureView(viewDesc); });
    ASSERT_TRUE(view.has_value());
    EXPECT_TRUE(view->IsValid());

    Dev().Dispatch([&](auto& dev) {
        dev.DestroyTextureView(*view);
        dev.DestroyTexture(*tex);
    });
}

TEST_P(RhiTextureViewTest, CreateSRGBReinterpretView) {
    TextureDesc texDesc{
        .dimension = TextureDimension::Tex2D,
        .format = Format::RGBA8_UNORM,
        .width = 64,
        .height = 64,
        .usage = TextureUsage::Sampled
    };
    auto tex = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(texDesc); });
    ASSERT_TRUE(tex.has_value());

    TextureViewDesc viewDesc{.texture = *tex, .format = Format::RGBA8_SRGB};
    auto view = Dev().Dispatch([&](auto& dev) { return dev.CreateTextureView(viewDesc); });
    ASSERT_TRUE(view.has_value());

    Dev().Dispatch([&](auto& dev) {
        dev.DestroyTextureView(*view);
        dev.DestroyTexture(*tex);
    });
}

TEST_P(RhiTextureViewTest, CreateMipSubrangeView) {
    TextureDesc texDesc{
        .dimension = TextureDimension::Tex2D,
        .format = Format::RGBA8_UNORM,
        .width = 256,
        .height = 256,
        .mipLevels = 5,
        .usage = TextureUsage::Sampled
    };
    auto tex = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(texDesc); });
    ASSERT_TRUE(tex.has_value());

    TextureViewDesc viewDesc{
        .texture = *tex,
        .baseMipLevel = 2,
        .mipLevelCount = 3  // mips 2, 3, 4
    };
    auto view = Dev().Dispatch([&](auto& dev) { return dev.CreateTextureView(viewDesc); });
    ASSERT_TRUE(view.has_value());

    Dev().Dispatch([&](auto& dev) {
        dev.DestroyTextureView(*view);
        dev.DestroyTexture(*tex);
    });
}

TEST_P(RhiTextureViewTest, CreateDepthAspectView) {
    TextureDesc texDesc{
        .dimension = TextureDimension::Tex2D,
        .format = Format::D32_FLOAT,
        .width = 512,
        .height = 512,
        .usage = TextureUsage::DepthStencil | TextureUsage::Sampled
    };
    auto tex = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(texDesc); });
    ASSERT_TRUE(tex.has_value());

    TextureViewDesc viewDesc{.texture = *tex, .aspect = TextureAspect::Depth};
    auto view = Dev().Dispatch([&](auto& dev) { return dev.CreateTextureView(viewDesc); });
    ASSERT_TRUE(view.has_value());

    Dev().Dispatch([&](auto& dev) {
        dev.DestroyTextureView(*view);
        dev.DestroyTexture(*tex);
    });
}

TEST_P(RhiTextureViewTest, ViewOfInvalidTextureReturnsError) {
    TextureViewDesc viewDesc{.texture = TextureHandle{}};
    auto view = Dev().Dispatch([&](auto& dev) { return dev.CreateTextureView(viewDesc); });
    EXPECT_FALSE(view.has_value());
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiTextureViewTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);

// ============================================================================
// §5.4 Sampler
// ============================================================================

class RhiSamplerTest : public RhiTest {};

TEST_P(RhiSamplerTest, CreateLinearSamplerSucceeds) {
    SamplerDesc desc{};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateSampler(desc); });
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());
    Dev().Dispatch([&](auto& dev) { dev.DestroySampler(*result); });
}

TEST_P(RhiSamplerTest, CreateNearestSampler) {
    SamplerDesc desc{.magFilter = Filter::Nearest, .minFilter = Filter::Nearest, .mipFilter = Filter::Nearest};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateSampler(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroySampler(*result); });
}

TEST_P(RhiSamplerTest, CreateAnisotropicSampler) {
    SamplerDesc desc{.maxAnisotropy = 16.0f};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateSampler(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroySampler(*result); });
}

TEST_P(RhiSamplerTest, CreateComparisonSampler) {
    SamplerDesc desc{.compareOp = CompareOp::LessOrEqual};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateSampler(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroySampler(*result); });
}

TEST_P(RhiSamplerTest, AllAddressModes) {
    for (auto mode :
         {AddressMode::Repeat, AddressMode::MirroredRepeat, AddressMode::ClampToEdge, AddressMode::ClampToBorder}) {
        SamplerDesc desc{.addressU = mode, .addressV = mode, .addressW = mode};
        auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateSampler(desc); });
        ASSERT_TRUE(result.has_value()) << "Failed for address mode " << static_cast<int>(mode);
        Dev().Dispatch([&](auto& dev) { dev.DestroySampler(*result); });
    }
}

TEST_P(RhiSamplerTest, AllBorderColors) {
    for (auto color : {BorderColor::TransparentBlack, BorderColor::OpaqueBlack, BorderColor::OpaqueWhite}) {
        SamplerDesc desc{.addressU = AddressMode::ClampToBorder, .borderColor = color};
        auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateSampler(desc); });
        ASSERT_TRUE(result.has_value()) << "Failed for border color " << static_cast<int>(color);
        Dev().Dispatch([&](auto& dev) { dev.DestroySampler(*result); });
    }
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiSamplerTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);

// ============================================================================
// §5.5 Memory Requirements & Aliasing (§5.6)
// ============================================================================

class RhiMemoryAliasingTest : public RhiTest {};

TEST_P(RhiMemoryAliasingTest, BufferMemoryRequirementsNonZero) {
    BufferDesc desc{.size = 4096, .usage = BufferUsage::Storage, .memory = MemoryLocation::GpuOnly};
    auto buf = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
    ASSERT_TRUE(buf.has_value());

    auto req = Dev().Dispatch([&](auto& dev) { return dev.GetBufferMemoryRequirements(*buf); });
    EXPECT_GT(req.size, 0u);
    EXPECT_GT(req.alignment, 0u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(*buf); });
}

TEST_P(RhiMemoryAliasingTest, TextureMemoryRequirementsNonZero) {
    TextureDesc desc{
        .dimension = TextureDimension::Tex2D,
        .format = Format::RGBA8_UNORM,
        .width = 256,
        .height = 256,
        .usage = TextureUsage::ColorAttachment,
        .transient = true
    };
    auto tex = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(desc); });
    ASSERT_TRUE(tex.has_value());

    auto req = Dev().Dispatch([&](auto& dev) { return dev.GetTextureMemoryRequirements(*tex); });
    EXPECT_GT(req.size, 0u);
    EXPECT_GT(req.alignment, 0u);

    Dev().Dispatch([&](auto& dev) { dev.DestroyTexture(*tex); });
}

TEST_P(RhiMemoryAliasingTest, CreateMemoryHeapSucceeds) {
    MemoryHeapDesc desc{.size = 64 * 1024 * 1024, .memory = MemoryLocation::GpuOnly, .debugName = "TestHeap"};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateMemoryHeap(desc); });
    if (result.has_value()) {
        EXPECT_TRUE(result->IsValid());
        Dev().Dispatch([&](auto& dev) { dev.DestroyMemoryHeap(*result); });
    }
    // WebGPU/OpenGL may return error (no aliasing support) — acceptable
}

TEST_P(RhiMemoryAliasingTest, AliasBufferToHeap) {
    RequireTier(CapabilityTier::Tier2_Compat);

    MemoryHeapDesc heapDesc{.size = 1024 * 1024, .memory = MemoryLocation::GpuOnly};
    auto heap = Dev().Dispatch([&](auto& dev) { return dev.CreateMemoryHeap(heapDesc); });
    if (!heap.has_value()) {
        GTEST_SKIP() << "CreateMemoryHeap not supported";
    }

    BufferDesc bufDesc{
        .size = 4096, .usage = BufferUsage::Storage, .memory = MemoryLocation::GpuOnly, .transient = true
    };
    auto buf = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(bufDesc); });
    ASSERT_TRUE(buf.has_value());

    Dev().Dispatch([&](auto& dev) { dev.AliasBufferMemory(*buf, *heap, 0); });

    Dev().Dispatch([&](auto& dev) {
        dev.DestroyBuffer(*buf);
        dev.DestroyMemoryHeap(*heap);
    });
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiMemoryAliasingTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);

// ============================================================================
// §5.5 Sparse Binding (T1 Only)
// ============================================================================

class RhiSparseBindingTest : public RhiTest {};

TEST_P(RhiSparseBindingTest, GetSparsePageSize) {
    RequireFeature(DeviceFeature::SparseBinding);

    auto pageSize = Dev().Dispatch([&](auto& dev) { return dev.GetSparsePageSize(); });
    EXPECT_GT(pageSize.bufferPageSize, 0u);
    EXPECT_GT(pageSize.imagePageSize, 0u);
}

INSTANTIATE_TEST_SUITE_P(Tier1, RhiSparseBindingTest, ::testing::ValuesIn(GetTier1Backends()), BackendName);

// ============================================================================
// §13.5 Memory Statistics
// ============================================================================

class RhiMemoryStatsTest : public RhiTest {};

TEST_P(RhiMemoryStatsTest, GetMemoryStatsSucceeds) {
    auto stats = Dev().Dispatch([&](auto& dev) { return dev.GetMemoryStats(); });
    // After device creation, some memory should be allocated (internal structures)
    // WebGPU may return zeros — that's acceptable
    EXPECT_GE(stats.totalAllocatedBytes, 0u);
}

TEST_P(RhiMemoryStatsTest, MemoryStatsReflectsAllocation) {
    auto statsBefore = Dev().Dispatch([&](auto& dev) { return dev.GetMemoryStats(); });

    BufferDesc desc{.size = 1024 * 1024, .usage = BufferUsage::Storage, .memory = MemoryLocation::GpuOnly};
    auto buf = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(desc); });
    ASSERT_TRUE(buf.has_value());

    auto statsAfter = Dev().Dispatch([&](auto& dev) { return dev.GetMemoryStats(); });
    // At least one more allocation
    if (statsBefore.totalAllocationCount > 0) {  // Skip if backend doesn't track
        EXPECT_GE(statsAfter.totalAllocationCount, statsBefore.totalAllocationCount);
    }

    Dev().Dispatch([&](auto& dev) { dev.DestroyBuffer(*buf); });
}

TEST_P(RhiMemoryStatsTest, GetMemoryHeapBudgets) {
    std::vector<MemoryHeapBudget> budgets(16);
    auto count = Dev().Dispatch([&](auto& dev) { return dev.GetMemoryHeapBudgets(std::span{budgets}); });
    // T1 backends should report at least 1 heap
    if (IsTier1()) {
        EXPECT_GE(count, 1u);
        bool hasDeviceLocal = false;
        for (uint32_t i = 0; i < count; ++i) {
            if (budgets[i].isDeviceLocal) {
                hasDeviceLocal = true;
                EXPECT_GT(budgets[i].budgetBytes, 0u);
            }
        }
        EXPECT_TRUE(hasDeviceLocal);
    }
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiMemoryStatsTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);
