/** @file test_render_graph.cpp
 *  @brief Unit tests for RenderGraph core: Builder, PassBuilder, Compiler.
 *
 *  Pure CPU tests — no GPU device required.
 */

#include <gtest/gtest.h>

#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rendergraph/RenderGraphCompiler.h"
#include "miki/rendergraph/RenderPassContext.h"

using namespace miki::rg;
using namespace miki::rhi;

// =============================================================================
// RGResourceHandle tests
// =============================================================================

TEST(RGResourceHandle, DefaultIsInvalid) {
    RGResourceHandle h;
    EXPECT_FALSE(h.IsValid());
    EXPECT_EQ(h.packed, RGResourceHandle::kInvalid);
}

TEST(RGResourceHandle, CreateAndQuery) {
    auto h = RGResourceHandle::Create(42, 3);
    EXPECT_TRUE(h.IsValid());
    EXPECT_EQ(h.GetIndex(), 42);
    EXPECT_EQ(h.GetVersion(), 3);
}

TEST(RGResourceHandle, NextVersion) {
    auto h = RGResourceHandle::Create(10, 0);
    auto h2 = h.NextVersion();
    EXPECT_EQ(h2.GetIndex(), 10);
    EXPECT_EQ(h2.GetVersion(), 1);
}

TEST(RGResourceHandle, Equality) {
    auto a = RGResourceHandle::Create(5, 1);
    auto b = RGResourceHandle::Create(5, 1);
    auto c = RGResourceHandle::Create(5, 2);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// =============================================================================
// RGPassHandle tests
// =============================================================================

TEST(RGPassHandle, DefaultIsInvalid) {
    RGPassHandle h;
    EXPECT_FALSE(h.IsValid());
}

TEST(RGPassHandle, ValidIndex) {
    RGPassHandle h{0};
    EXPECT_TRUE(h.IsValid());
}

// =============================================================================
// ResourceAccess tests
// =============================================================================

TEST(ResourceAccess, IsWriteAccess) {
    EXPECT_TRUE(IsWriteAccess(ResourceAccess::ShaderWrite));
    EXPECT_TRUE(IsWriteAccess(ResourceAccess::ColorAttachWrite));
    EXPECT_TRUE(IsWriteAccess(ResourceAccess::DepthStencilWrite));
    EXPECT_TRUE(IsWriteAccess(ResourceAccess::TransferDst));
    EXPECT_FALSE(IsWriteAccess(ResourceAccess::ShaderReadOnly));
    EXPECT_FALSE(IsWriteAccess(ResourceAccess::DepthReadOnly));
    EXPECT_FALSE(IsWriteAccess(ResourceAccess::None));
}

TEST(ResourceAccess, IsReadAccess) {
    EXPECT_TRUE(IsReadAccess(ResourceAccess::ShaderReadOnly));
    EXPECT_TRUE(IsReadAccess(ResourceAccess::DepthReadOnly));
    EXPECT_TRUE(IsReadAccess(ResourceAccess::IndirectBuffer));
    EXPECT_TRUE(IsReadAccess(ResourceAccess::TransferSrc));
    EXPECT_FALSE(IsReadAccess(ResourceAccess::ShaderWrite));
    EXPECT_FALSE(IsReadAccess(ResourceAccess::None));
}

// =============================================================================
// ResolveBarrier tests
// =============================================================================

TEST(BarrierMapping, ResolveShaderReadOnly) {
    auto b = ResolveBarrier(ResourceAccess::ShaderReadOnly);
    EXPECT_NE(static_cast<uint32_t>(b.stage), 0u);
    EXPECT_NE(static_cast<uint32_t>(b.access), 0u);
    EXPECT_EQ(b.layout, TextureLayout::ShaderReadOnly);
}

TEST(BarrierMapping, ResolveColorAttachWrite) {
    auto b = ResolveBarrier(ResourceAccess::ColorAttachWrite);
    EXPECT_EQ(b.stage, PipelineStage::ColorAttachmentOutput);
    EXPECT_EQ(b.access, AccessFlags::ColorAttachmentWrite);
    EXPECT_EQ(b.layout, TextureLayout::ColorAttachment);
}

TEST(BarrierMapping, ResolveDepthStencilWrite) {
    auto b = ResolveBarrier(ResourceAccess::DepthStencilWrite);
    EXPECT_EQ(b.layout, TextureLayout::DepthStencilAttachment);
}

TEST(BarrierMapping, ResolvePresentSrc) {
    auto b = ResolveBarrier(ResourceAccess::PresentSrc);
    EXPECT_EQ(b.layout, TextureLayout::Present);
}

TEST(BarrierMapping, ResolveCombined) {
    auto b = ResolveBarrierCombined(ResourceAccess::ShaderReadOnly | ResourceAccess::DepthReadOnly);
    EXPECT_NE(static_cast<uint32_t>(b.stage), 0u);
    // Should include both fragment shader and early/late fragment test stages
    auto stageRaw = static_cast<uint32_t>(b.stage);
    EXPECT_NE(stageRaw & static_cast<uint32_t>(PipelineStage::FragmentShader), 0u);
    EXPECT_NE(stageRaw & static_cast<uint32_t>(PipelineStage::EarlyFragmentTests), 0u);
}

// =============================================================================
// RenderGraphBuilder — resource declaration
// =============================================================================

TEST(RenderGraphBuilder, CreateTexture) {
    RenderGraphBuilder builder;
    auto h = builder.CreateTexture({
        .format = Format::RGBA16_FLOAT,
        .width = 1920,
        .height = 1080,
        .debugName = "GBufferA",
    });
    EXPECT_TRUE(h.IsValid());
    EXPECT_EQ(h.GetIndex(), 0);
    EXPECT_EQ(h.GetVersion(), 0);
    EXPECT_EQ(builder.GetResourceCount(), 1u);
}

TEST(RenderGraphBuilder, CreateBuffer) {
    RenderGraphBuilder builder;
    auto h = builder.CreateBuffer({.size = 1024, .debugName = "IndirectArgs"});
    EXPECT_TRUE(h.IsValid());
    EXPECT_EQ(builder.GetResourceCount(), 1u);
}

TEST(RenderGraphBuilder, ImportTexture) {
    RenderGraphBuilder builder;
    TextureHandle ext{42};
    auto h = builder.ImportTexture(ext, "ExternalTex");
    EXPECT_TRUE(h.IsValid());
    auto& res = builder.GetResources()[h.GetIndex()];
    EXPECT_TRUE(res.imported);
    EXPECT_EQ(res.importedTexture.value, 42u);
}

TEST(RenderGraphBuilder, ImportBuffer) {
    RenderGraphBuilder builder;
    BufferHandle ext{99};
    auto h = builder.ImportBuffer(ext, "ExternalBuf");
    EXPECT_TRUE(h.IsValid());
    auto& res = builder.GetResources()[h.GetIndex()];
    EXPECT_TRUE(res.imported);
    EXPECT_EQ(res.importedBuffer.value, 99u);
}

TEST(RenderGraphBuilder, MultipleResources) {
    RenderGraphBuilder builder;
    auto t1 = builder.CreateTexture({.debugName = "T1"});
    auto t2 = builder.CreateTexture({.debugName = "T2"});
    auto b1 = builder.CreateBuffer({.size = 512, .debugName = "B1"});
    EXPECT_EQ(builder.GetResourceCount(), 3u);
    EXPECT_NE(t1.GetIndex(), t2.GetIndex());
    EXPECT_NE(t2.GetIndex(), b1.GetIndex());
}

// =============================================================================
// RenderGraphBuilder — pass declaration
// =============================================================================

TEST(RenderGraphBuilder, AddGraphicsPass) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 800, .height = 600, .debugName = "Color"});

    auto pass = builder.AddGraphicsPass("GBufferFill",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
        },
        [](RenderPassContext&) {});

    EXPECT_TRUE(pass.IsValid());
    EXPECT_EQ(pass.index, 0u);
    EXPECT_EQ(builder.GetPassCount(), 1u);

    auto& p = builder.GetPasses()[0];
    EXPECT_STREQ(p.name, "GBufferFill");
    EXPECT_EQ(p.queue, RGQueueType::Graphics);
    EXPECT_EQ(p.writes.size(), 1u);
}

TEST(RenderGraphBuilder, AddComputePass) {
    RenderGraphBuilder builder;
    auto buf = builder.CreateBuffer({.size = 1024, .debugName = "Data"});

    auto pass = builder.AddComputePass("CullPass",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(buf);
        },
        [](RenderPassContext&) {});

    EXPECT_TRUE(pass.IsValid());
    auto& p = builder.GetPasses()[0];
    EXPECT_EQ(p.queue, RGQueueType::Graphics);  // compute on graphics queue
    EXPECT_EQ(p.reads.size(), 1u);
}

TEST(RenderGraphBuilder, AddAsyncComputePass) {
    RenderGraphBuilder builder;
    auto pass = builder.AddAsyncComputePass("AsyncCull",
        [](PassBuilder&) {},
        [](RenderPassContext&) {});

    auto& p = builder.GetPasses()[0];
    EXPECT_EQ(p.queue, RGQueueType::AsyncCompute);
}

TEST(RenderGraphBuilder, AddTransferPass) {
    RenderGraphBuilder builder;
    auto pass = builder.AddTransferPass("Upload",
        [](PassBuilder&) {},
        [](RenderPassContext&) {});

    auto& p = builder.GetPasses()[0];
    EXPECT_EQ(p.queue, RGQueueType::Transfer);
}

// =============================================================================
// PassBuilder — SSA versioning
// =============================================================================

TEST(PassBuilder, WriteCreatesNewVersion) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.debugName = "Tex"});
    EXPECT_EQ(tex.GetVersion(), 0);

    RGResourceHandle writtenHandle;
    builder.AddGraphicsPass("Writer",
        [&](PassBuilder& pb) {
            writtenHandle = pb.WriteColorAttachment(tex);
        },
        [](RenderPassContext&) {});

    EXPECT_TRUE(writtenHandle.IsValid());
    EXPECT_EQ(writtenHandle.GetIndex(), tex.GetIndex());
    EXPECT_EQ(writtenHandle.GetVersion(), 1);  // version bumped
}

TEST(PassBuilder, ReadDoesNotBumpVersion) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.debugName = "Tex"});

    builder.AddGraphicsPass("Reader",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
        },
        [](RenderPassContext&) {});

    auto& p = builder.GetPasses()[0];
    EXPECT_EQ(p.reads.size(), 1u);
    EXPECT_EQ(p.reads[0].handle.GetVersion(), 0);  // original version
}

TEST(PassBuilder, ReadWriteCreatesNewVersionAndRead) {
    RenderGraphBuilder builder;
    auto buf = builder.CreateBuffer({.size = 64, .debugName = "Buf"});

    RGResourceHandle newHandle;
    builder.AddComputePass("RW",
        [&](PassBuilder& pb) {
            newHandle = pb.ReadWriteBuffer(buf);
        },
        [](RenderPassContext&) {});

    auto& p = builder.GetPasses()[0];
    EXPECT_EQ(p.reads.size(), 1u);
    EXPECT_EQ(p.writes.size(), 1u);
    EXPECT_EQ(newHandle.GetVersion(), 1);
}

TEST(PassBuilder, MultipleWritesBumpVersionSequentially) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.debugName = "Tex"});

    RGResourceHandle v1, v2;
    builder.AddGraphicsPass("Pass1",
        [&](PassBuilder& pb) { v1 = pb.WriteColorAttachment(tex); },
        [](RenderPassContext&) {});
    builder.AddGraphicsPass("Pass2",
        [&](PassBuilder& pb) { v2 = pb.WriteColorAttachment(tex); },
        [](RenderPassContext&) {});

    EXPECT_EQ(v1.GetVersion(), 1);
    EXPECT_EQ(v2.GetVersion(), 2);
}

TEST(PassBuilder, SetSideEffects) {
    RenderGraphBuilder builder;
    builder.AddGraphicsPass("SE",
        [](PassBuilder& pb) { pb.SetSideEffects(); },
        [](RenderPassContext&) {});

    EXPECT_TRUE(builder.GetPasses()[0].hasSideEffects);
}

TEST(PassBuilder, SetOrderHint) {
    RenderGraphBuilder builder;
    builder.AddGraphicsPass("Ordered",
        [](PassBuilder& pb) { pb.SetOrderHint(-100); },
        [](RenderPassContext&) {});

    EXPECT_EQ(builder.GetPasses()[0].orderHint, -100);
}

// =============================================================================
// RenderGraphBuilder — conditional execution
// =============================================================================

TEST(RenderGraphBuilder, EnableIf) {
    RenderGraphBuilder builder;
    auto pass = builder.AddGraphicsPass("Conditional",
        [](PassBuilder& pb) { pb.SetSideEffects(); },
        [](RenderPassContext&) {});

    bool enabled = false;
    builder.EnableIf(pass, [&]() { return enabled; });

    EXPECT_TRUE(builder.GetPasses()[0].conditionFn != nullptr);
}

// =============================================================================
// RenderGraphBuilder — build finalization
// =============================================================================

TEST(RenderGraphBuilder, BuildSetsFlag) {
    RenderGraphBuilder builder;
    EXPECT_FALSE(builder.IsBuilt());
    builder.Build();
    EXPECT_TRUE(builder.IsBuilt());
}

// =============================================================================
// RenderGraphCompiler — basic compilation
// =============================================================================

TEST(RenderGraphCompiler, CompileEmptyGraph) {
    RenderGraphBuilder builder;
    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->passes.empty());
}

TEST(RenderGraphCompiler, CompileSinglePassWithSideEffects) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 100, .height = 100, .debugName = "Out"});

    builder.AddGraphicsPass("SinglePass",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 1u);
}

TEST(RenderGraphCompiler, DCECullsDeadPasses) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.debugName = "Transient"});

    // This pass has no side effects and its output is not consumed
    builder.AddGraphicsPass("Dead",
        [&](PassBuilder& pb) { pb.WriteColorAttachment(tex); },
        [](RenderPassContext&) {});

    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 0u);  // Dead pass culled
}

TEST(RenderGraphCompiler, DCEKeepsProducerOfLiveConsumer) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.debugName = "Shared"});

    RGResourceHandle written;
    builder.AddGraphicsPass("Producer",
        [&](PassBuilder& pb) { written = pb.WriteColorAttachment(tex); },
        [](RenderPassContext&) {});

    builder.AddGraphicsPass("Consumer",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);  // reads base resource
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);  // Both kept
}

// =============================================================================
// RenderGraphCompiler — topological sort
// =============================================================================

TEST(RenderGraphCompiler, TopologicalOrderRespectsDependencies) {
    RenderGraphBuilder builder;
    auto depth = builder.CreateTexture({
        .format = Format::D32_FLOAT, .width = 800, .height = 600, .debugName = "Depth",
    });
    auto color = builder.CreateTexture({
        .format = Format::RGBA16_FLOAT, .width = 800, .height = 600, .debugName = "Color",
    });
    auto backbuffer = builder.ImportTexture(TextureHandle{1}, "Backbuffer");

    // Pass 0: depth prepass
    RGResourceHandle depthWritten;
    builder.AddGraphicsPass("DepthPrepass",
        [&](PassBuilder& pb) { depthWritten = pb.WriteDepthStencil(depth); },
        [](RenderPassContext&) {});

    // Pass 1: gbuffer fill (reads depth, writes color)
    RGResourceHandle colorWritten;
    builder.AddGraphicsPass("GBufferFill",
        [&](PassBuilder& pb) {
            pb.ReadDepth(depth);
            colorWritten = pb.WriteColorAttachment(color);
        },
        [](RenderPassContext&) {});

    // Pass 2: present (reads color, side effects)
    builder.AddGraphicsPass("Present",
        [&](PassBuilder& pb) {
            pb.ReadTexture(color);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 3u);

    // Verify order: DepthPrepass < GBufferFill < Present
    auto findPos = [&](const char* name) -> uint32_t {
        auto& passes = builder.GetPasses();
        for (uint32_t i = 0; i < result->passes.size(); ++i) {
            if (std::string_view(passes[result->passes[i].passIndex].name) == name) {
                return i;
            }
        }
        return UINT32_MAX;
    };

    EXPECT_LT(findPos("DepthPrepass"), findPos("GBufferFill"));
    EXPECT_LT(findPos("GBufferFill"), findPos("Present"));
}

// =============================================================================
// RenderGraphCompiler — conditional execution with DCE
// =============================================================================

TEST(RenderGraphCompiler, ConditionalPassDisabled) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.debugName = "AO"});

    auto aoPass = builder.AddGraphicsPass("GTAO",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    builder.EnableIf(aoPass, []() { return false; });
    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 0u);  // Disabled by condition
}

TEST(RenderGraphCompiler, ConditionalPassEnabled) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.debugName = "AO"});

    auto aoPass = builder.AddGraphicsPass("GTAO",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    builder.EnableIf(aoPass, []() { return true; });
    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 1u);
}

// =============================================================================
// RenderGraphCompiler — barrier synthesis
// =============================================================================

TEST(RenderGraphCompiler, BarriersInsertedBetweenWriteAndRead) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({
        .format = Format::RGBA16_FLOAT, .width = 256, .height = 256, .debugName = "RT",
    });

    builder.AddGraphicsPass("Writer",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    builder.AddGraphicsPass("Reader",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);

    // The reader pass should have an acquire barrier for the texture
    bool foundBarrier = false;
    for (auto& compiled : result->passes) {
        for (auto& b : compiled.acquireBarriers) {
            if (b.resourceIndex == tex.GetIndex()) {
                foundBarrier = true;
                EXPECT_EQ(b.srcAccess, ResourceAccess::ColorAttachWrite);
                EXPECT_EQ(b.dstAccess, ResourceAccess::ShaderReadOnly);
                EXPECT_EQ(b.dstLayout, TextureLayout::ShaderReadOnly);
            }
        }
    }
    EXPECT_TRUE(foundBarrier);
}

TEST(RenderGraphCompiler, SplitBarriersWhenGapExists) {
    RenderGraphBuilder builder;
    auto texA = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "A"});
    auto texB = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "B"});

    // Pass 0: write A
    builder.AddGraphicsPass("WriteA",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(texA);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    // Pass 1: write B (gap between pass 0 and pass 2)
    builder.AddGraphicsPass("WriteB",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(texB);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    // Pass 2: read A (split barrier should be used: release at pass 0, acquire at pass 2)
    builder.AddGraphicsPass("ReadA",
        [&](PassBuilder& pb) {
            pb.ReadTexture(texA);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 3u);

    // Check for split barrier: release at WriteA, acquire at ReadA
    bool foundRelease = false;
    bool foundAcquire = false;
    for (auto& compiled : result->passes) {
        for (auto& b : compiled.releaseBarriers) {
            if (b.resourceIndex == texA.GetIndex() && b.isSplitRelease) {
                foundRelease = true;
            }
        }
        for (auto& b : compiled.acquireBarriers) {
            if (b.resourceIndex == texA.GetIndex() && b.isSplitAcquire) {
                foundAcquire = true;
            }
        }
    }
    EXPECT_TRUE(foundRelease);
    EXPECT_TRUE(foundAcquire);
}

// =============================================================================
// RenderGraphCompiler — cross-queue sync
// =============================================================================

TEST(RenderGraphCompiler, CrossQueueSyncPoints) {
    RenderGraphBuilder builder;
    auto buf = builder.CreateBuffer({.size = 1024, .debugName = "CulledData"});

    // Async compute pass writes the buffer
    builder.AddAsyncComputePass("AsyncCull",
        [&](PassBuilder& pb) {
            pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    // Graphics pass reads the buffer
    builder.AddGraphicsPass("Render",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(buf);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
    EXPECT_GE(result->syncPoints.size(), 1u);

    // Verify sync point direction: AsyncCompute -> Graphics
    bool foundSync = false;
    for (auto& sp : result->syncPoints) {
        if (sp.srcQueue == RGQueueType::AsyncCompute && sp.dstQueue == RGQueueType::Graphics) {
            foundSync = true;
        }
    }
    EXPECT_TRUE(foundSync);
}

// =============================================================================
// RenderGraphCompiler — resource lifetimes
// =============================================================================

TEST(RenderGraphCompiler, TransientResourceLifetimes) {
    RenderGraphBuilder builder;
    auto texA = builder.CreateTexture({.debugName = "A"});
    auto texB = builder.CreateTexture({.debugName = "B"});

    builder.AddGraphicsPass("P0",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(texA);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    builder.AddGraphicsPass("P1",
        [&](PassBuilder& pb) {
            pb.ReadTexture(texA);
            pb.WriteColorAttachment(texB);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    builder.AddGraphicsPass("P2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(texB);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // Both resources should have lifetimes
    EXPECT_GE(result->lifetimes.size(), 2u);

    // texA: used in P0 (write) and P1 (read) -> lifetime [0,1]
    // texB: used in P1 (write) and P2 (read) -> lifetime [1,2]
    for (auto& lt : result->lifetimes) {
        if (lt.resourceIndex == texA.GetIndex()) {
            EXPECT_EQ(lt.firstPass, 0u);
            EXPECT_EQ(lt.lastPass, 1u);
        }
        if (lt.resourceIndex == texB.GetIndex()) {
            EXPECT_EQ(lt.firstPass, 1u);
            EXPECT_EQ(lt.lastPass, 2u);
        }
    }
}

// =============================================================================
// RenderGraphCompiler — structural hash
// =============================================================================

TEST(RenderGraphCompiler, StructuralHashConsistent) {
    auto buildGraph = []() {
        RenderGraphBuilder builder;
        auto tex = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "T"});
        builder.AddGraphicsPass("P",
            [&](PassBuilder& pb) {
                pb.WriteColorAttachment(tex);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {});
        builder.Build();
        RenderGraphCompiler compiler;
        return compiler.Compile(builder);
    };

    auto r1 = buildGraph();
    auto r2 = buildGraph();
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r1->hash, r2->hash);
}

TEST(RenderGraphCompiler, StructuralHashDiffersWithDifferentFormat) {
    auto buildGraph = [](Format fmt) {
        RenderGraphBuilder builder;
        auto tex = builder.CreateTexture({.format = fmt, .width = 64, .height = 64, .debugName = "T"});
        builder.AddGraphicsPass("P",
            [&](PassBuilder& pb) {
                pb.WriteColorAttachment(tex);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {});
        builder.Build();
        RenderGraphCompiler compiler;
        return compiler.Compile(builder);
    };

    auto r1 = buildGraph(Format::RGBA8_UNORM);
    auto r2 = buildGraph(Format::RGBA16_FLOAT);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_NE(r1->hash, r2->hash);
}

// =============================================================================
// RenderPassContext — handle resolution
// =============================================================================

TEST(RenderPassContext, ResolveTexture) {
    TextureHandle physicals[] = {TextureHandle{10}, TextureHandle{20}, TextureHandle{30}};
    RenderPassContext ctx{
        .physicalTextures = physicals,
    };

    auto h = RGResourceHandle::Create(1, 0);
    auto resolved = ctx.GetTexture(h);
    EXPECT_EQ(resolved.value, 20u);
}

TEST(RenderPassContext, ResolveOutOfRange) {
    RenderPassContext ctx{};
    auto h = RGResourceHandle::Create(999, 0);
    auto resolved = ctx.GetTexture(h);
    EXPECT_FALSE(resolved.IsValid());
}

// =============================================================================
// RGTextureDesc — conversion to RHI desc
// =============================================================================

TEST(RGTextureDesc, ToRhiDesc) {
    RGTextureDesc desc{
        .format = Format::RGBA16_FLOAT,
        .width = 1920,
        .height = 1080,
        .debugName = "Test",
    };

    auto rhiDesc = desc.ToRhiDesc(TextureUsage::Sampled | TextureUsage::ColorAttachment);
    EXPECT_EQ(rhiDesc.format, Format::RGBA16_FLOAT);
    EXPECT_EQ(rhiDesc.width, 1920u);
    EXPECT_EQ(rhiDesc.height, 1080u);
    EXPECT_TRUE(rhiDesc.transient);
    EXPECT_EQ(rhiDesc.memory, MemoryLocation::GpuOnly);
}

// =============================================================================
// Present pass integration
// =============================================================================

TEST(RenderGraphBuilder, AddPresentPass) {
    RenderGraphBuilder builder;
    auto bb = builder.ImportTexture(TextureHandle{1}, "Backbuffer");
    builder.AddPresentPass("Present", bb);

    EXPECT_EQ(builder.GetPassCount(), 1u);
    auto& pass = builder.GetPasses()[0];
    EXPECT_TRUE(pass.hasSideEffects);
    EXPECT_EQ(pass.reads.size(), 1u);
}

// =============================================================================
// Complex pipeline: depth -> gbuffer -> lighting -> tonemap -> present
// =============================================================================

TEST(RenderGraphCompiler, FullPipeline5Passes) {
    RenderGraphBuilder builder;

    auto depth = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 1920, .height = 1080, .debugName = "Depth"});
    auto gbufferA = builder.CreateTexture({.format = Format::RGBA16_FLOAT, .width = 1920, .height = 1080, .debugName = "GBufferA"});
    auto gbufferB = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 1920, .height = 1080, .debugName = "GBufferB"});
    auto hdrColor = builder.CreateTexture({.format = Format::RGBA16_FLOAT, .width = 1920, .height = 1080, .debugName = "HDR"});
    auto ldrColor = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 1920, .height = 1080, .debugName = "LDR"});

    // 1. Depth prepass
    builder.AddGraphicsPass("DepthPrepass",
        [&](PassBuilder& pb) { pb.WriteDepthStencil(depth); },
        [](RenderPassContext&) {});

    // 2. GBuffer fill
    builder.AddGraphicsPass("GBufferFill",
        [&](PassBuilder& pb) {
            pb.ReadDepth(depth);
            pb.WriteColorAttachment(gbufferA, 0);
            pb.WriteColorAttachment(gbufferB, 1);
        },
        [](RenderPassContext&) {});

    // 3. Deferred lighting
    builder.AddComputePass("DeferredLighting",
        [&](PassBuilder& pb) {
            pb.ReadTexture(gbufferA);
            pb.ReadTexture(gbufferB);
            pb.ReadTexture(depth);
            pb.WriteTexture(hdrColor);
        },
        [](RenderPassContext&) {});

    // 4. Tone mapping
    builder.AddComputePass("ToneMap",
        [&](PassBuilder& pb) {
            pb.ReadTexture(hdrColor);
            pb.WriteTexture(ldrColor);
        },
        [](RenderPassContext&) {});

    // 5. Present (side effect)
    builder.AddGraphicsPass("Final",
        [&](PassBuilder& pb) {
            pb.ReadTexture(ldrColor);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {});

    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 5u);

    // Verify topological ordering
    auto findPos = [&](const char* name) -> uint32_t {
        auto& passes = builder.GetPasses();
        for (uint32_t i = 0; i < result->passes.size(); ++i) {
            if (std::string_view(passes[result->passes[i].passIndex].name) == name) return i;
        }
        return UINT32_MAX;
    };

    EXPECT_LT(findPos("DepthPrepass"), findPos("GBufferFill"));
    EXPECT_LT(findPos("GBufferFill"), findPos("DeferredLighting"));
    EXPECT_LT(findPos("DeferredLighting"), findPos("ToneMap"));
    EXPECT_LT(findPos("ToneMap"), findPos("Final"));

    // Verify barriers exist
    uint32_t totalBarriers = 0;
    for (auto& p : result->passes) {
        totalBarriers += static_cast<uint32_t>(p.acquireBarriers.size() + p.releaseBarriers.size());
    }
    EXPECT_GT(totalBarriers, 0u);

    // Verify resource lifetimes exist for transient resources
    EXPECT_GE(result->lifetimes.size(), 3u);  // At least depth, gbufferA, hdrColor
}
