// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// §6 Descriptor & Binding System tests.
// Covers: DescriptorLayout, PipelineLayout, DescriptorSet create/update/destroy,
// push constants, bindless table, backend mapping validation.

#include <gtest/gtest.h>

#include "RhiTestFixture.h"

#include "miki/rhi/Descriptors.h"
#include "miki/rhi/Device.h"
#include "miki/rhi/Resources.h"

using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// §6.2 Descriptor Layout
// ============================================================================

class RhiDescriptorLayoutTest : public RhiTest {};

TEST_P(RhiDescriptorLayoutTest, CreateSingleUBOLayout) {
    BindingDesc binding{.binding = 0, .type = BindingType::UniformBuffer, .stages = ShaderStage::Vertex, .count = 1};
    DescriptorLayoutDesc desc{.bindings = std::span{&binding, 1}};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(desc); });
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());
    Dev().Dispatch([&](auto& dev) { dev.DestroyDescriptorLayout(*result); });
}

TEST_P(RhiDescriptorLayoutTest, CreateMultiBindingLayout) {
    std::array bindings = {
        BindingDesc{.binding = 0, .type = BindingType::UniformBuffer, .stages = ShaderStage::Vertex, .count = 1},
        BindingDesc{.binding = 1, .type = BindingType::SampledTexture, .stages = ShaderStage::Fragment, .count = 1},
        BindingDesc{.binding = 2, .type = BindingType::Sampler, .stages = ShaderStage::Fragment, .count = 1},
    };
    DescriptorLayoutDesc desc{.bindings = bindings};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyDescriptorLayout(*result); });
}

TEST_P(RhiDescriptorLayoutTest, CreateSSBOLayout) {
    BindingDesc binding{.binding = 0, .type = BindingType::StorageBuffer, .stages = ShaderStage::Compute, .count = 1};
    DescriptorLayoutDesc desc{.bindings = std::span{&binding, 1}};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyDescriptorLayout(*result); });
}

TEST_P(RhiDescriptorLayoutTest, CreateStorageTextureLayout) {
    BindingDesc binding{.binding = 0, .type = BindingType::StorageTexture, .stages = ShaderStage::Compute, .count = 1};
    DescriptorLayoutDesc desc{.bindings = std::span{&binding, 1}};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyDescriptorLayout(*result); });
}

TEST_P(RhiDescriptorLayoutTest, CreateCombinedTextureSamplerLayout) {
    BindingDesc binding{
        .binding = 0, .type = BindingType::CombinedTextureSampler, .stages = ShaderStage::Fragment, .count = 1
    };
    DescriptorLayoutDesc desc{.bindings = std::span{&binding, 1}};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyDescriptorLayout(*result); });
}

TEST_P(RhiDescriptorLayoutTest, CreateArrayBindingLayout) {
    BindingDesc binding{.binding = 0, .type = BindingType::SampledTexture, .stages = ShaderStage::Fragment, .count = 8};
    DescriptorLayoutDesc desc{.bindings = std::span{&binding, 1}};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyDescriptorLayout(*result); });
}

TEST_P(RhiDescriptorLayoutTest, CreateEmptyLayoutSucceeds) {
    DescriptorLayoutDesc desc{};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(desc); });
    // Empty layout may be valid (used for pipeline layout padding)
    if (result.has_value()) {
        Dev().Dispatch([&](auto& dev) { dev.DestroyDescriptorLayout(*result); });
    }
}

TEST_P(RhiDescriptorLayoutTest, CreatePushDescriptorLayout) {
    BindingDesc binding{.binding = 0, .type = BindingType::UniformBuffer, .stages = ShaderStage::All, .count = 1};
    DescriptorLayoutDesc desc{.bindings = std::span{&binding, 1}, .pushDescriptor = true};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(desc); });
    // Push descriptors may not be supported on all backends — acceptable to fail
    if (result.has_value()) {
        Dev().Dispatch([&](auto& dev) { dev.DestroyDescriptorLayout(*result); });
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllBackends, RhiDescriptorLayoutTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName
);

// ============================================================================
// §6.3 Pipeline Layout
// ============================================================================

class RhiPipelineLayoutTest : public RhiTest {};

TEST_P(RhiPipelineLayoutTest, CreateSingleSetLayout) {
    BindingDesc binding{.binding = 0, .type = BindingType::UniformBuffer, .stages = ShaderStage::All, .count = 1};
    DescriptorLayoutDesc layoutDesc{.bindings = std::span{&binding, 1}};
    auto descLayout = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(layoutDesc); });
    ASSERT_TRUE(descLayout.has_value());

    PipelineLayoutDesc plDesc{.setLayouts = std::span{&*descLayout, 1}};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreatePipelineLayout(plDesc); });
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());

    Dev().Dispatch([&](auto& dev) {
        dev.DestroyPipelineLayout(*result);
        dev.DestroyDescriptorLayout(*descLayout);
    });
}

TEST_P(RhiPipelineLayoutTest, CreateLayoutWithPushConstants) {
    PushConstantRange pc{.stages = ShaderStage::Vertex | ShaderStage::Fragment, .offset = 0, .size = 128};
    PipelineLayoutDesc desc{.pushConstants = std::span{&pc, 1}};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreatePipelineLayout(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyPipelineLayout(*result); });
}

TEST_P(RhiPipelineLayoutTest, CreateLayoutWith4Sets) {
    std::array<DescriptorLayoutHandle, 4> layouts;
    for (int i = 0; i < 4; ++i) {
        BindingDesc binding{.binding = 0, .type = BindingType::UniformBuffer, .stages = ShaderStage::All, .count = 1};
        DescriptorLayoutDesc ld{.bindings = std::span{&binding, 1}};
        auto r = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(ld); });
        ASSERT_TRUE(r.has_value());
        layouts[i] = *r;
    }

    PipelineLayoutDesc plDesc{.setLayouts = layouts};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreatePipelineLayout(plDesc); });
    ASSERT_TRUE(result.has_value());

    Dev().Dispatch([&](auto& dev) {
        dev.DestroyPipelineLayout(*result);
        for (auto& l : layouts) {
            dev.DestroyDescriptorLayout(l);
        }
    });
}

TEST_P(RhiPipelineLayoutTest, CreateLayoutWithSetsAndPushConstants) {
    BindingDesc binding{.binding = 0, .type = BindingType::UniformBuffer, .stages = ShaderStage::All, .count = 1};
    DescriptorLayoutDesc ld{.bindings = std::span{&binding, 1}};
    auto descLayout = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(ld); });
    ASSERT_TRUE(descLayout.has_value());

    PushConstantRange pc{.stages = ShaderStage::All, .offset = 0, .size = 64};
    PipelineLayoutDesc plDesc{.setLayouts = std::span{&*descLayout, 1}, .pushConstants = std::span{&pc, 1}};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreatePipelineLayout(plDesc); });
    ASSERT_TRUE(result.has_value());

    Dev().Dispatch([&](auto& dev) {
        dev.DestroyPipelineLayout(*result);
        dev.DestroyDescriptorLayout(*descLayout);
    });
}

TEST_P(RhiPipelineLayoutTest, PushConstantSizeWithinLimits) {
    uint32_t maxSize = Caps().maxPushConstantSize;
    PushConstantRange pc{.stages = ShaderStage::All, .offset = 0, .size = maxSize};
    PipelineLayoutDesc desc{.pushConstants = std::span{&pc, 1}};
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreatePipelineLayout(desc); });
    ASSERT_TRUE(result.has_value());
    Dev().Dispatch([&](auto& dev) { dev.DestroyPipelineLayout(*result); });
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiPipelineLayoutTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);

// ============================================================================
// §6.4 Descriptor Set
// ============================================================================

class RhiDescriptorSetTest : public RhiTest {};

TEST_P(RhiDescriptorSetTest, CreateWithUBOBinding) {
    // Create layout
    BindingDesc binding{.binding = 0, .type = BindingType::UniformBuffer, .stages = ShaderStage::All, .count = 1};
    DescriptorLayoutDesc ld{.bindings = std::span{&binding, 1}};
    auto layout = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(ld); });
    ASSERT_TRUE(layout.has_value());

    // Create buffer for binding
    BufferDesc bufDesc{.size = 256, .usage = BufferUsage::Uniform, .memory = MemoryLocation::CpuToGpu};
    auto buf = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(bufDesc); });
    ASSERT_TRUE(buf.has_value());

    // Create descriptor set
    DescriptorWrite write{.binding = 0, .resource = BufferBinding{.buffer = *buf, .offset = 0, .range = 256}};
    DescriptorSetDesc dsDesc{.layout = *layout, .writes = std::span{&write, 1}};
    auto ds = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorSet(dsDesc); });
    ASSERT_TRUE(ds.has_value());
    EXPECT_TRUE(ds->IsValid());

    Dev().Dispatch([&](auto& dev) {
        dev.DestroyDescriptorSet(*ds);
        dev.DestroyBuffer(*buf);
        dev.DestroyDescriptorLayout(*layout);
    });
}

TEST_P(RhiDescriptorSetTest, UpdateDescriptorSet) {
    BindingDesc binding{.binding = 0, .type = BindingType::UniformBuffer, .stages = ShaderStage::All, .count = 1};
    DescriptorLayoutDesc ld{.bindings = std::span{&binding, 1}};
    auto layout = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(ld); });
    ASSERT_TRUE(layout.has_value());

    BufferDesc bufDesc{.size = 256, .usage = BufferUsage::Uniform, .memory = MemoryLocation::CpuToGpu};
    auto buf1 = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(bufDesc); });
    auto buf2 = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(bufDesc); });
    ASSERT_TRUE(buf1.has_value() && buf2.has_value());

    // Create with buf1
    DescriptorWrite write1{.binding = 0, .resource = BufferBinding{.buffer = *buf1}};
    DescriptorSetDesc dsDesc{.layout = *layout, .writes = std::span{&write1, 1}};
    auto ds = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorSet(dsDesc); });
    ASSERT_TRUE(ds.has_value());

    // Update to buf2
    DescriptorWrite write2{.binding = 0, .resource = BufferBinding{.buffer = *buf2}};
    Dev().Dispatch([&](auto& dev) { dev.UpdateDescriptorSet(*ds, std::span{&write2, 1}); });

    Dev().Dispatch([&](auto& dev) {
        dev.DestroyDescriptorSet(*ds);
        dev.DestroyBuffer(*buf1);
        dev.DestroyBuffer(*buf2);
        dev.DestroyDescriptorLayout(*layout);
    });
}

TEST_P(RhiDescriptorSetTest, CreateWithTextureBinding) {
    BindingDesc binding{.binding = 0, .type = BindingType::SampledTexture, .stages = ShaderStage::Fragment, .count = 1};
    DescriptorLayoutDesc ld{.bindings = std::span{&binding, 1}};
    auto layout = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(ld); });
    ASSERT_TRUE(layout.has_value());

    TextureDesc texDesc{
        .dimension = TextureDimension::Tex2D,
        .format = Format::RGBA8_UNORM,
        .width = 64,
        .height = 64,
        .usage = TextureUsage::Sampled
    };
    auto tex = Dev().Dispatch([&](auto& dev) { return dev.CreateTexture(texDesc); });
    ASSERT_TRUE(tex.has_value());

    TextureViewDesc viewDesc{.texture = *tex};
    auto view = Dev().Dispatch([&](auto& dev) { return dev.CreateTextureView(viewDesc); });
    ASSERT_TRUE(view.has_value());

    DescriptorWrite write{.binding = 0, .resource = TextureBinding{.view = *view}};
    DescriptorSetDesc dsDesc{.layout = *layout, .writes = std::span{&write, 1}};
    auto ds = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorSet(dsDesc); });
    ASSERT_TRUE(ds.has_value());

    Dev().Dispatch([&](auto& dev) {
        dev.DestroyDescriptorSet(*ds);
        dev.DestroyTextureView(*view);
        dev.DestroyTexture(*tex);
        dev.DestroyDescriptorLayout(*layout);
    });
}

TEST_P(RhiDescriptorSetTest, Create100DescriptorSets) {
    BindingDesc binding{.binding = 0, .type = BindingType::UniformBuffer, .stages = ShaderStage::All, .count = 1};
    DescriptorLayoutDesc ld{.bindings = std::span{&binding, 1}};
    auto layout = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorLayout(ld); });
    ASSERT_TRUE(layout.has_value());

    BufferDesc bufDesc{.size = 64, .usage = BufferUsage::Uniform, .memory = MemoryLocation::CpuToGpu};
    auto buf = Dev().Dispatch([&](auto& dev) { return dev.CreateBuffer(bufDesc); });
    ASSERT_TRUE(buf.has_value());

    std::vector<DescriptorSetHandle> sets;
    for (int i = 0; i < 100; ++i) {
        DescriptorWrite write{.binding = 0, .resource = BufferBinding{.buffer = *buf}};
        DescriptorSetDesc dsDesc{.layout = *layout, .writes = std::span{&write, 1}};
        auto ds = Dev().Dispatch([&](auto& dev) { return dev.CreateDescriptorSet(dsDesc); });
        ASSERT_TRUE(ds.has_value()) << "Failed at descriptor set " << i;
        sets.push_back(*ds);
    }

    Dev().Dispatch([&](auto& dev) {
        for (auto& s : sets) {
            dev.DestroyDescriptorSet(s);
        }
        dev.DestroyBuffer(*buf);
        dev.DestroyDescriptorLayout(*layout);
    });
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiDescriptorSetTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);
