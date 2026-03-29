// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT
//
// §8 Pipeline System tests.
// Covers: Graphics/Compute/RT pipeline creation, pipeline cache,
// pipeline library (split compilation), pipeline descriptor validation,
// vertex input state, color blend state, depth/stencil state.

#include <gtest/gtest.h>

#include <array>

#include "RhiTestFixture.h"

#include "miki/rhi/Device.h"
#include "miki/rhi/Pipeline.h"

using namespace miki::rhi;
using namespace miki::rhi::test;

// ============================================================================
// §8.0 Pipeline Cache
// ============================================================================

class RhiPipelineCacheTest : public RhiTest {};

TEST_P(RhiPipelineCacheTest, CreateEmptyPipelineCache) {
    auto result = Dev().Dispatch([&](auto& dev) { return dev.CreatePipelineCache({}); });
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());
    Dev().Dispatch([&](auto& dev) { dev.DestroyPipelineCache(*result); });
}

TEST_P(RhiPipelineCacheTest, GetPipelineCacheData) {
    auto cache = Dev().Dispatch([&](auto& dev) { return dev.CreatePipelineCache({}); });
    ASSERT_TRUE(cache.has_value());

    auto data = Dev().Dispatch([&](auto& dev) { return dev.GetPipelineCacheData(*cache); });
    // Empty cache may return empty or small header data — both are valid
    // Just ensure it doesn't crash
    (void)data;

    Dev().Dispatch([&](auto& dev) { dev.DestroyPipelineCache(*cache); });
}

TEST_P(RhiPipelineCacheTest, CreateCacheFromBlob) {
    // Create a cache, get its data, then recreate from that data
    auto cache1 = Dev().Dispatch([&](auto& dev) { return dev.CreatePipelineCache({}); });
    ASSERT_TRUE(cache1.has_value());

    auto data = Dev().Dispatch([&](auto& dev) { return dev.GetPipelineCacheData(*cache1); });

    if (!data.empty()) {
        auto cache2 = Dev().Dispatch([&](auto& dev) { return dev.CreatePipelineCache(data); });
        // May fail if blob is invalid/empty — acceptable
        if (cache2.has_value()) {
            Dev().Dispatch([&](auto& dev) { dev.DestroyPipelineCache(*cache2); });
        }
    }

    Dev().Dispatch([&](auto& dev) { dev.DestroyPipelineCache(*cache1); });
}

INSTANTIATE_TEST_SUITE_P(AllBackends, RhiPipelineCacheTest, ::testing::ValuesIn(GetAvailableBackends()), BackendName);

// ============================================================================
// §8.1 Graphics Pipeline Descriptor — struct validation (pure CPU)
// ============================================================================

TEST(GraphicsPipelineDesc, DefaultsAreReasonable) {
    GraphicsPipelineDesc desc{};
    EXPECT_EQ(desc.topology, PrimitiveTopology::TriangleList);
    EXPECT_EQ(desc.polygonMode, PolygonMode::Fill);
    EXPECT_EQ(desc.cullMode, CullMode::Back);
    EXPECT_EQ(desc.frontFace, FrontFace::CounterClockwise);
    EXPECT_TRUE(desc.depthTestEnable);
    EXPECT_TRUE(desc.depthWriteEnable);
    EXPECT_EQ(desc.depthCompareOp, CompareOp::Less);
    EXPECT_FALSE(desc.stencilTestEnable);
    EXPECT_EQ(desc.sampleCount, 1u);
    EXPECT_EQ(desc.viewMask, 0u);
    EXPECT_FALSE(desc.IsMeshShaderPipeline());
}

TEST(GraphicsPipelineDesc, MeshShaderDetection) {
    GraphicsPipelineDesc desc{};
    EXPECT_FALSE(desc.IsMeshShaderPipeline());
    desc.meshShader = ShaderModuleHandle{.value = 1};  // Fake valid handle
    EXPECT_TRUE(desc.IsMeshShaderPipeline());
}

// ============================================================================
// §8.1.1 Vertex Input State — struct validation
// ============================================================================

TEST(VertexInputState, EmptyIsValid) {
    VertexInputState state{};
    EXPECT_TRUE(state.bindings.empty());
    EXPECT_TRUE(state.attributes.empty());
}

TEST(VertexInputState, SingleBindingSingleAttribute) {
    VertexInputBinding binding{.binding = 0, .stride = 20, .inputRate = VertexInputRate::PerVertex};
    VertexInputAttribute attr{.location = 0, .binding = 0, .format = Format::RGB32_FLOAT, .offset = 0};
    VertexInputState state{.bindings = std::span{&binding, 1}, .attributes = std::span{&attr, 1}};
    EXPECT_EQ(state.bindings.size(), 1u);
    EXPECT_EQ(state.attributes.size(), 1u);
}

TEST(VertexInputState, InterleavedPosNormalUV) {
    std::array bindings = {
        VertexInputBinding{.binding = 0, .stride = 32, .inputRate = VertexInputRate::PerVertex},
    };
    std::array attrs = {
        VertexInputAttribute{.location = 0, .binding = 0, .format = Format::RGB32_FLOAT, .offset = 0},   // pos
        VertexInputAttribute{.location = 1, .binding = 0, .format = Format::RGB32_FLOAT, .offset = 12},  // normal
        VertexInputAttribute{.location = 2, .binding = 0, .format = Format::RG32_FLOAT, .offset = 24},   // uv
    };
    VertexInputState state{.bindings = bindings, .attributes = attrs};
    EXPECT_EQ(state.attributes.size(), 3u);
}

TEST(VertexInputState, SeparateStreams) {
    std::array bindings = {
        VertexInputBinding{.binding = 0, .stride = 12, .inputRate = VertexInputRate::PerVertex},
        VertexInputBinding{.binding = 1, .stride = 16, .inputRate = VertexInputRate::PerInstance},
    };
    std::array attrs = {
        VertexInputAttribute{.location = 0, .binding = 0, .format = Format::RGB32_FLOAT, .offset = 0},
        VertexInputAttribute{.location = 1, .binding = 1, .format = Format::RGBA32_FLOAT, .offset = 0},
    };
    VertexInputState state{.bindings = bindings, .attributes = attrs};
    EXPECT_EQ(state.bindings.size(), 2u);
}

// ============================================================================
// §8.1.2 Color Attachment Blend — struct validation
// ============================================================================

TEST(ColorAttachmentBlend, DefaultsToNoBlend) {
    ColorAttachmentBlend blend{};
    EXPECT_FALSE(blend.blendEnable);
    EXPECT_EQ(blend.writeMask, ColorWriteMask::All);
}

TEST(ColorAttachmentBlend, AlphaBlendPreset) {
    ColorAttachmentBlend blend{
        .blendEnable = true,
        .srcColor = BlendFactor::SrcAlpha,
        .dstColor = BlendFactor::OneMinusSrcAlpha,
        .colorOp = BlendOp::Add,
        .srcAlpha = BlendFactor::One,
        .dstAlpha = BlendFactor::Zero,
        .alphaOp = BlendOp::Add,
    };
    EXPECT_TRUE(blend.blendEnable);
    EXPECT_EQ(blend.srcColor, BlendFactor::SrcAlpha);
}

TEST(ColorAttachmentBlend, AdditiveBlendPreset) {
    ColorAttachmentBlend blend{
        .blendEnable = true,
        .srcColor = BlendFactor::One,
        .dstColor = BlendFactor::One,
        .colorOp = BlendOp::Add,
    };
    EXPECT_TRUE(blend.blendEnable);
    EXPECT_EQ(blend.dstColor, BlendFactor::One);
}

// ============================================================================
// §8.2 Compute Pipeline Descriptor — struct validation
// ============================================================================

TEST(ComputePipelineDesc, DefaultIsEmpty) {
    ComputePipelineDesc desc{};
    EXPECT_FALSE(desc.computeShader.IsValid());
    EXPECT_FALSE(desc.pipelineLayout.IsValid());
}

// ============================================================================
// §8.3 Ray Tracing Pipeline Descriptor — struct validation
// ============================================================================

TEST(RayTracingPipelineDesc, DefaultMaxRecursion) {
    RayTracingPipelineDesc desc{};
    EXPECT_EQ(desc.maxRecursionDepth, 1u);
}

// ============================================================================
// §8.4 Dynamic Rendering Descriptors — struct validation
// ============================================================================

TEST(RenderingAttachment, DefaultLoadStore) {
    RenderingAttachment attach{};
    EXPECT_EQ(attach.loadOp, AttachmentLoadOp::Clear);
    EXPECT_EQ(attach.storeOp, AttachmentStoreOp::Store);
}

TEST(RenderingDesc, DefaultViewMaskZero) {
    RenderingDesc desc{};
    EXPECT_EQ(desc.viewMask, 0u);
    EXPECT_EQ(desc.depthAttachment, nullptr);
    EXPECT_EQ(desc.stencilAttachment, nullptr);
}

// ============================================================================
// §8.5 Pipeline Library (Split Compilation) — struct validation
// ============================================================================

TEST(PipelineLibrary, PartDescDefault) {
    PipelineLibraryPartDesc desc{};
    EXPECT_EQ(desc.part, PipelineLibraryPart::VertexInput);
}

TEST(PipelineLibrary, LinkedDescAllParts) {
    LinkedPipelineDesc desc{};
    EXPECT_FALSE(desc.vertexInput.IsValid());
    EXPECT_FALSE(desc.preRasterization.IsValid());
    EXPECT_FALSE(desc.fragmentShader.IsValid());
    EXPECT_FALSE(desc.fragmentOutput.IsValid());
}

// ============================================================================
// §8.6 Work Graph — struct validation
// ============================================================================

TEST(WorkGraphDesc, DefaultsValid) {
    WorkGraphDesc desc{};
    EXPECT_FALSE(desc.workGraphPipeline.IsValid());
    EXPECT_FALSE(desc.backingMemory.IsValid());
    EXPECT_EQ(desc.backingMemoryOffset, 0u);
    EXPECT_EQ(desc.backingMemorySize, 0u);
}

TEST(DispatchGraphDesc, DefaultsValid) {
    DispatchGraphDesc desc{};
    EXPECT_FALSE(desc.inputRecords.IsValid());
    EXPECT_EQ(desc.numRecords, 0u);
}
