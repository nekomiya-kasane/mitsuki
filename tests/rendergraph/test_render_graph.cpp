/** @file test_render_graph.cpp
 *  @brief Unit tests for RenderGraph core: Builder, PassBuilder, Compiler.
 *
 *  Pure CPU tests — no GPU device required.
 */

#include <gtest/gtest.h>
#include <unordered_set>

#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rendergraph/RenderGraphCompiler.h"
#include "miki/rendergraph/RenderGraphExecutor.h"
#include "miki/rendergraph/RenderPassContext.h"
#include "miki/rhi/adaptation/AdaptationQuery.h"

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
    auto tex
        = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 800, .height = 600, .debugName = "Color"});

    auto pass = builder.AddGraphicsPass(
        "GBufferFill", [&](PassBuilder& pb) { pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {}
    );

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

    auto pass
        = builder.AddComputePass("CullPass", [&](PassBuilder& pb) { pb.ReadBuffer(buf); }, [](RenderPassContext&) {});

    EXPECT_TRUE(pass.IsValid());
    auto& p = builder.GetPasses()[0];
    EXPECT_EQ(p.queue, RGQueueType::Graphics);  // compute on graphics queue
    EXPECT_EQ(p.reads.size(), 1u);
}

TEST(RenderGraphBuilder, AddAsyncComputePass) {
    RenderGraphBuilder builder;
    auto pass = builder.AddAsyncComputePass("AsyncCull", [](PassBuilder&) {}, [](RenderPassContext&) {});

    auto& p = builder.GetPasses()[0];
    EXPECT_EQ(p.queue, RGQueueType::AsyncCompute);
}

TEST(RenderGraphBuilder, AddTransferPass) {
    RenderGraphBuilder builder;
    auto pass = builder.AddTransferPass("Upload", [](PassBuilder&) {}, [](RenderPassContext&) {});

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
    builder.AddGraphicsPass(
        "Writer", [&](PassBuilder& pb) { writtenHandle = pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {}
    );

    EXPECT_TRUE(writtenHandle.IsValid());
    EXPECT_EQ(writtenHandle.GetIndex(), tex.GetIndex());
    EXPECT_EQ(writtenHandle.GetVersion(), 1);  // version bumped
}

TEST(PassBuilder, ReadDoesNotBumpVersion) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.debugName = "Tex"});

    builder.AddGraphicsPass("Reader", [&](PassBuilder& pb) { pb.ReadTexture(tex); }, [](RenderPassContext&) {});

    auto& p = builder.GetPasses()[0];
    EXPECT_EQ(p.reads.size(), 1u);
    EXPECT_EQ(p.reads[0].handle.GetVersion(), 0);  // original version
}

TEST(PassBuilder, ReadWriteCreatesNewVersionAndRead) {
    RenderGraphBuilder builder;
    auto buf = builder.CreateBuffer({.size = 64, .debugName = "Buf"});

    RGResourceHandle newHandle;
    builder.AddComputePass(
        "RW", [&](PassBuilder& pb) { newHandle = pb.ReadWriteBuffer(buf); }, [](RenderPassContext&) {}
    );

    auto& p = builder.GetPasses()[0];
    EXPECT_EQ(p.reads.size(), 1u);
    EXPECT_EQ(p.writes.size(), 1u);
    EXPECT_EQ(newHandle.GetVersion(), 1);
}

TEST(PassBuilder, MultipleWritesBumpVersionSequentially) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.debugName = "Tex"});

    RGResourceHandle v1, v2;
    builder.AddGraphicsPass(
        "Pass1", [&](PassBuilder& pb) { v1 = pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Pass2", [&](PassBuilder& pb) { v2 = pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {}
    );

    EXPECT_EQ(v1.GetVersion(), 1);
    EXPECT_EQ(v2.GetVersion(), 2);
}

TEST(PassBuilder, SetSideEffects) {
    RenderGraphBuilder builder;
    builder.AddGraphicsPass("SE", [](PassBuilder& pb) { pb.SetSideEffects(); }, [](RenderPassContext&) {});

    EXPECT_TRUE(builder.GetPasses()[0].hasSideEffects);
}

TEST(PassBuilder, SetOrderHint) {
    RenderGraphBuilder builder;
    builder.AddGraphicsPass("Ordered", [](PassBuilder& pb) { pb.SetOrderHint(-100); }, [](RenderPassContext&) {});

    EXPECT_EQ(builder.GetPasses()[0].orderHint, -100);
}

// =============================================================================
// RenderGraphBuilder — conditional execution
// =============================================================================

TEST(RenderGraphBuilder, EnableIf) {
    RenderGraphBuilder builder;
    auto pass = builder.AddGraphicsPass(
        "Conditional", [](PassBuilder& pb) { pb.SetSideEffects(); }, [](RenderPassContext&) {}
    );

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

    builder.AddGraphicsPass(
        "SinglePass",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

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
    builder.AddGraphicsPass("Dead", [&](PassBuilder& pb) { pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {});

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
    builder.AddGraphicsPass(
        "Producer", [&](PassBuilder& pb) { written = pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "Consumer",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);  // reads base resource
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

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
        .format = Format::D32_FLOAT,
        .width = 800,
        .height = 600,
        .debugName = "Depth",
    });
    auto color = builder.CreateTexture({
        .format = Format::RGBA16_FLOAT,
        .width = 800,
        .height = 600,
        .debugName = "Color",
    });
    auto backbuffer = builder.ImportTexture(TextureHandle{1}, "Backbuffer");

    // Pass 0: depth prepass
    RGResourceHandle depthWritten;
    builder.AddGraphicsPass(
        "DepthPrepass", [&](PassBuilder& pb) { depthWritten = pb.WriteDepthStencil(depth); }, [](RenderPassContext&) {}
    );

    // Pass 1: gbuffer fill (reads depth, writes color)
    RGResourceHandle colorWritten;
    builder.AddGraphicsPass(
        "GBufferFill",
        [&](PassBuilder& pb) {
            pb.ReadDepth(depth);
            colorWritten = pb.WriteColorAttachment(color);
        },
        [](RenderPassContext&) {}
    );

    // Pass 2: present (reads color, side effects)
    builder.AddGraphicsPass(
        "Present",
        [&](PassBuilder& pb) {
            pb.ReadTexture(color);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

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

    auto aoPass = builder.AddGraphicsPass(
        "GTAO",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

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

    auto aoPass = builder.AddGraphicsPass(
        "GTAO",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

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
        .format = Format::RGBA16_FLOAT,
        .width = 256,
        .height = 256,
        .debugName = "RT",
    });

    builder.AddGraphicsPass(
        "Writer",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "Reader",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();

    // Disable merging to isolate Stage 6 barrier verification
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
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
    builder.AddGraphicsPass(
        "WriteA",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(texA);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    // Pass 1: write B (gap between pass 0 and pass 2)
    builder.AddGraphicsPass(
        "WriteB",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(texB);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    // Pass 2: read A (split barrier should be used: release at pass 0, acquire at pass 2)
    builder.AddGraphicsPass(
        "ReadA",
        [&](PassBuilder& pb) {
            pb.ReadTexture(texA);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

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
    builder.AddAsyncComputePass(
        "AsyncCull",
        [&](PassBuilder& pb) {
            pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    // Graphics pass reads the buffer
    builder.AddGraphicsPass(
        "Render",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(buf);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

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

    builder.AddGraphicsPass(
        "P0",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(texA);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "P1",
        [&](PassBuilder& pb) {
            pb.ReadTexture(texA);
            pb.WriteColorAttachment(texB);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "P2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(texB);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

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
        builder.AddGraphicsPass(
            "P",
            [&](PassBuilder& pb) {
                pb.WriteColorAttachment(tex);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
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
        builder.AddGraphicsPass(
            "P",
            [&](PassBuilder& pb) {
                pb.WriteColorAttachment(tex);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
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

    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 1920, .height = 1080, .debugName = "Depth"});
    auto gbufferA = builder.CreateTexture(
        {.format = Format::RGBA16_FLOAT, .width = 1920, .height = 1080, .debugName = "GBufferA"}
    );
    auto gbufferB = builder.CreateTexture(
        {.format = Format::RGBA8_UNORM, .width = 1920, .height = 1080, .debugName = "GBufferB"}
    );
    auto hdrColor
        = builder.CreateTexture({.format = Format::RGBA16_FLOAT, .width = 1920, .height = 1080, .debugName = "HDR"});
    auto ldrColor
        = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 1920, .height = 1080, .debugName = "LDR"});

    // 1. Depth prepass
    builder.AddGraphicsPass(
        "DepthPrepass", [&](PassBuilder& pb) { pb.WriteDepthStencil(depth); }, [](RenderPassContext&) {}
    );

    // 2. GBuffer fill
    builder.AddGraphicsPass(
        "GBufferFill",
        [&](PassBuilder& pb) {
            pb.ReadDepth(depth);
            pb.WriteColorAttachment(gbufferA, 0);
            pb.WriteColorAttachment(gbufferB, 1);
        },
        [](RenderPassContext&) {}
    );

    // 3. Deferred lighting
    builder.AddComputePass(
        "DeferredLighting",
        [&](PassBuilder& pb) {
            pb.ReadTexture(gbufferA);
            pb.ReadTexture(gbufferB);
            pb.ReadTexture(depth);
            pb.WriteTexture(hdrColor);
        },
        [](RenderPassContext&) {}
    );

    // 4. Tone mapping
    builder.AddComputePass(
        "ToneMap",
        [&](PassBuilder& pb) {
            pb.ReadTexture(hdrColor);
            pb.WriteTexture(ldrColor);
        },
        [](RenderPassContext&) {}
    );

    // 5. Present (side effect)
    builder.AddGraphicsPass(
        "Final",
        [&](PassBuilder& pb) {
            pb.ReadTexture(ldrColor);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 5u);

    // Verify topological ordering
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

// =============================================================================
// B-18: Compile-time access validation — IsAccessValidForPassType
// =============================================================================

TEST(AccessValidation, GraphicsPassAllowsAllAccesses) {
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ShaderReadOnly, RGPassFlags::Graphics));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ColorAttachWrite, RGPassFlags::Graphics));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::DepthStencilWrite, RGPassFlags::Graphics));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::DepthReadOnly, RGPassFlags::Graphics));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::TransferSrc, RGPassFlags::Graphics));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::InputAttachment, RGPassFlags::Graphics));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ShaderWrite, RGPassFlags::Graphics));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ShadingRateRead, RGPassFlags::Graphics));
}

TEST(AccessValidation, ComputePassAllowsShaderAccesses) {
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ShaderReadOnly, RGPassFlags::Compute));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ShaderWrite, RGPassFlags::Compute));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::TransferSrc, RGPassFlags::Compute));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::TransferDst, RGPassFlags::Compute));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::IndirectBuffer, RGPassFlags::Compute));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::AccelStructRead, RGPassFlags::Compute));
}

TEST(AccessValidation, ComputePassRejectsGraphicsOnlyAccesses) {
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::ColorAttachWrite, RGPassFlags::Compute));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::DepthStencilWrite, RGPassFlags::Compute));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::DepthReadOnly, RGPassFlags::Compute));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::InputAttachment, RGPassFlags::Compute));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::ShadingRateRead, RGPassFlags::Compute));
}

TEST(AccessValidation, AsyncComputeRejectsGraphicsAccesses) {
    auto flags = RGPassFlags::Compute | RGPassFlags::AsyncCompute;
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ShaderReadOnly, flags));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ShaderWrite, flags));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::ColorAttachWrite, flags));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::DepthStencilWrite, flags));
}

TEST(AccessValidation, TransferPassOnlyAllowsTransferAccesses) {
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::TransferSrc, RGPassFlags::Transfer));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::TransferDst, RGPassFlags::Transfer));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::ShaderReadOnly, RGPassFlags::Transfer));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::ShaderWrite, RGPassFlags::Transfer));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::ColorAttachWrite, RGPassFlags::Transfer));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::DepthStencilWrite, RGPassFlags::Transfer));
}

TEST(AccessValidation, PresentPassOnlyAllowsPresentSrc) {
    auto flags = RGPassFlags::Present | RGPassFlags::SideEffects | RGPassFlags::NeverCull;
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::PresentSrc, flags));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::ShaderReadOnly, flags));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::ColorAttachWrite, flags));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::TransferSrc, flags));
}

TEST(AccessValidation, NoneAccessAlwaysValid) {
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::None, RGPassFlags::Graphics));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::None, RGPassFlags::Compute));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::None, RGPassFlags::Transfer));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::None, RGPassFlags::Present));
}

TEST(AccessValidation, ConstexprEvaluable) {
    // Verify the function is actually constexpr-evaluable
    static_assert(IsAccessValidForPassType(ResourceAccess::ShaderReadOnly, RGPassFlags::Graphics));
    static_assert(!IsAccessValidForPassType(ResourceAccess::ColorAttachWrite, RGPassFlags::Compute));
    static_assert(!IsAccessValidForPassType(ResourceAccess::ShaderReadOnly, RGPassFlags::Transfer));
    static_assert(IsAccessValidForPassType(ResourceAccess::TransferSrc, RGPassFlags::Transfer));
    static_assert(IsAccessValidForPassType(ResourceAccess::PresentSrc, RGPassFlags::Present));
    static_assert(!IsAccessValidForPassType(ResourceAccess::ShaderWrite, RGPassFlags::Present));
}

// =============================================================================
// B-17: InlineFunction unit tests
// =============================================================================

using miki::core::InlineFunction;

TEST(InlineFunction, DefaultConstructedIsEmpty) {
    InlineFunction<void()> fn;
    EXPECT_FALSE(static_cast<bool>(fn));
    EXPECT_TRUE(fn == nullptr);
}

TEST(InlineFunction, NullptrConstructedIsEmpty) {
    InlineFunction<void()> fn(nullptr);
    EXPECT_FALSE(static_cast<bool>(fn));
}

TEST(InlineFunction, StorePlainFunction) {
    static int called = 0;
    called = 0;
    InlineFunction<void()> fn([]() { called++; });
    EXPECT_TRUE(static_cast<bool>(fn));
    fn();
    EXPECT_EQ(called, 1);
}

TEST(InlineFunction, StoreCapturingLambda) {
    int value = 0;
    InlineFunction<void()> fn([&value]() { value = 42; });
    fn();
    EXPECT_EQ(value, 42);
}

TEST(InlineFunction, ReturnValue) {
    InlineFunction<int(int, int)> fn([](int a, int b) { return a + b; });
    EXPECT_EQ(fn(3, 4), 7);
}

TEST(InlineFunction, MoveConstruct) {
    int value = 0;
    InlineFunction<void()> fn([&value]() { value = 99; });
    InlineFunction<void()> fn2(std::move(fn));
    EXPECT_FALSE(static_cast<bool>(fn));  // NOLINT: testing moved-from state
    EXPECT_TRUE(static_cast<bool>(fn2));
    fn2();
    EXPECT_EQ(value, 99);
}

TEST(InlineFunction, MoveAssign) {
    int a = 0, b = 0;
    InlineFunction<void()> fn1([&a]() { a = 1; });
    InlineFunction<void()> fn2([&b]() { b = 2; });
    fn2 = std::move(fn1);
    EXPECT_FALSE(static_cast<bool>(fn1));  // NOLINT
    fn2();
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 0);  // fn2's original callable was destroyed
}

TEST(InlineFunction, AssignNullptr) {
    int value = 0;
    InlineFunction<void()> fn([&value]() { value = 1; });
    EXPECT_TRUE(static_cast<bool>(fn));
    fn = nullptr;
    EXPECT_FALSE(static_cast<bool>(fn));
}

TEST(InlineFunction, MultipleCaptures) {
    int x = 10;
    float y = 20.0f;
    const char* z = "hello";
    InlineFunction<int()> fn([x, y, z]() { return static_cast<int>(x + y + (z ? 1 : 0)); });
    EXPECT_EQ(fn(), 31);
}

TEST(InlineFunction, MoveOnlyCapture) {
    auto ptr = std::make_unique<int>(42);
    InlineFunction<int(), 64> fn([p = std::move(ptr)]() { return *p; });
    EXPECT_EQ(fn(), 42);
}

TEST(InlineFunction, UsedAsPassSetupFn) {
    // Verify the actual render graph usage pattern compiles and works
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "T"});
    bool setupCalled = false;
    builder.AddGraphicsPass(
        "TestPass",
        [&setupCalled, tex](PassBuilder& pb) {
            setupCalled = true;
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    EXPECT_TRUE(setupCalled);
    EXPECT_EQ(builder.GetPassCount(), 1u);
}

TEST(InlineFunction, UsedAsConditionFn) {
    bool flag = true;
    ConditionFn cond([&flag]() -> bool { return flag; });
    EXPECT_TRUE(cond());
    flag = false;
    EXPECT_FALSE(cond());
}

TEST(InlineFunction, SizeGuarantee) {
    // Verify that InlineFunction with Cap=64 is exactly vtable_ptr + 64 bytes storage
    // (plus possible alignment padding)
    constexpr size_t expected = sizeof(void*) + 64;
    EXPECT_LE(sizeof(InlineFunction<void(), 64>), expected + alignof(std::max_align_t));
}

// =============================================================================
// B-16: LinearAllocator unit tests
// =============================================================================

using miki::core::LinearAllocator;

TEST(LinearAllocator, BasicAllocateAndReset) {
    LinearAllocator alloc(1024);
    EXPECT_EQ(alloc.GetCapacity(), 1024u);
    EXPECT_EQ(alloc.GetUsedBytes(), 0u);

    auto* p = alloc.Allocate(64);
    EXPECT_NE(p, nullptr);
    EXPECT_GE(alloc.GetUsedBytes(), 64u);

    alloc.Reset();
    EXPECT_EQ(alloc.GetUsedBytes(), 0u);
}

TEST(LinearAllocator, AllocateArrayReturnsSpan) {
    LinearAllocator alloc(4096);
    auto span = alloc.AllocateArray<uint32_t>(10);
    EXPECT_EQ(span.size(), 10u);
    for (size_t i = 0; i < span.size(); ++i) {
        span[i] = static_cast<uint32_t>(i);
    }
    EXPECT_EQ(span[5], 5u);
}

TEST(LinearAllocator, CopyToArena) {
    LinearAllocator alloc(4096);
    std::vector<int> src = {1, 2, 3, 4, 5};
    auto span = alloc.CopyToArena(std::span<const int>(src));
    EXPECT_EQ(span.size(), 5u);
    for (size_t i = 0; i < span.size(); ++i) {
        EXPECT_EQ(span[i], src[i]);
    }
    // Modifying span doesn't affect src (it's a copy)
    span[0] = 99;
    EXPECT_EQ(src[0], 1);
}

TEST(LinearAllocator, ExhaustedReturnsNull) {
    LinearAllocator alloc(32);
    auto* p1 = alloc.Allocate(16);
    EXPECT_NE(p1, nullptr);
    auto* p2 = alloc.Allocate(16);
    EXPECT_NE(p2, nullptr);
    auto* p3 = alloc.Allocate(16);  // Over capacity
    EXPECT_EQ(p3, nullptr);
}

TEST(LinearAllocator, EmptyArrayReturnsEmptySpan) {
    LinearAllocator alloc(1024);
    auto span = alloc.AllocateArray<int>(0);
    EXPECT_TRUE(span.empty());
}

TEST(LinearAllocator, MoveConstruct) {
    LinearAllocator alloc(1024);
    alloc.Allocate(128);
    EXPECT_GE(alloc.GetUsedBytes(), 128u);

    LinearAllocator alloc2(std::move(alloc));
    EXPECT_GE(alloc2.GetUsedBytes(), 128u);
    EXPECT_EQ(alloc2.GetCapacity(), 1024u);
}

// =============================================================================
// B-16: Arena-backed pass storage integration tests
// =============================================================================

TEST(ArenaPassStorage, PassReadsWritesAreSpans) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "T"});
    auto buf = builder.CreateBuffer({.size = 256, .debugName = "B"});

    RGResourceHandle writtenTex;
    builder.AddGraphicsPass(
        "P",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(buf, ResourceAccess::ShaderReadOnly);
            pb.ReadTexture(tex, ResourceAccess::ShaderReadOnly);
            writtenTex = pb.WriteColorAttachment(tex);
        },
        [](RenderPassContext&) {}
    );

    auto& passes = builder.GetPasses();
    EXPECT_EQ(passes.size(), 1u);
    EXPECT_EQ(passes[0].reads.size(), 2u);
    EXPECT_EQ(passes[0].writes.size(), 1u);
    EXPECT_EQ(passes[0].reads[0].handle, buf);  // first read is the buffer
    EXPECT_EQ(passes[0].reads[1].handle, tex);  // second read is the texture
    EXPECT_EQ(passes[0].writes[0].handle, writtenTex);
}

TEST(ArenaPassStorage, MultiplePassesShareArena) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.debugName = "T"});

    for (int i = 0; i < 50; ++i) {
        std::string name = "Pass" + std::to_string(i);
        builder.AddGraphicsPass(
            name.c_str(),
            [&](PassBuilder& pb) {
                pb.ReadTexture(tex, ResourceAccess::ShaderReadOnly);
                pb.WriteColorAttachment(tex);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
    }

    EXPECT_EQ(builder.GetPassCount(), 50u);
    for (uint32_t i = 0; i < 50u; ++i) {
        EXPECT_EQ(builder.GetPasses()[i].reads.size(), 1u);
        EXPECT_EQ(builder.GetPasses()[i].writes.size(), 1u);
    }
}

TEST(ArenaPassStorage, PassWithNoAccessesHasEmptySpans) {
    RenderGraphBuilder builder;
    builder.AddGraphicsPass("Empty", [](PassBuilder& pb) { pb.SetSideEffects(); }, [](RenderPassContext&) {});

    auto& pass = builder.GetPasses()[0];
    EXPECT_TRUE(pass.reads.empty());
    EXPECT_TRUE(pass.writes.empty());
}

// =============================================================================
// D-1: Resource size/alignment estimation
// =============================================================================

using miki::rg::AliasingLayout;
using miki::rg::ClassifyHeapGroup;
using miki::rg::EstimateBufferAlignment;
using miki::rg::EstimateBufferSize;
using miki::rg::EstimateTextureAlignment;
using miki::rg::EstimateTextureSize;
using miki::rg::HeapGroupType;
using miki::rg::kAlignmentBuffer;
using miki::rg::kAlignmentMsaa;
using miki::rg::kAlignmentTexture;
using miki::rg::RGResourceKind;

TEST(ResourceEstimation, TextureSize_RGBA8_256x256) {
    RGTextureDesc desc{.format = Format::RGBA8_UNORM, .width = 256, .height = 256};
    EXPECT_EQ(EstimateTextureSize(desc), 256u * 256u * 4u);
}

TEST(ResourceEstimation, TextureSize_WithMips) {
    RGTextureDesc desc{.format = Format::RGBA8_UNORM, .width = 256, .height = 256, .mipLevels = 2};
    // mip0 = 256*256*4 = 262144, mip1 = 128*128*4 = 65536
    EXPECT_EQ(EstimateTextureSize(desc), 262144u + 65536u);
}

TEST(ResourceEstimation, TextureSize_MSAA) {
    RGTextureDesc desc{.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .sampleCount = 4};
    EXPECT_EQ(EstimateTextureSize(desc), 64u * 64u * 4u * 4u);
}

TEST(ResourceEstimation, BufferSize) {
    RGBufferDesc desc{.size = 4096};
    EXPECT_EQ(EstimateBufferSize(desc), 4096u);
}

TEST(ResourceEstimation, TextureAlignment_Normal) {
    RGTextureDesc desc{.format = Format::RGBA8_UNORM, .width = 64, .height = 64};
    EXPECT_EQ(EstimateTextureAlignment(desc), kAlignmentTexture);
}

TEST(ResourceEstimation, TextureAlignment_MSAA) {
    RGTextureDesc desc{.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .sampleCount = 4};
    EXPECT_EQ(EstimateTextureAlignment(desc), kAlignmentMsaa);
}

TEST(ResourceEstimation, BufferAlignment) {
    RGBufferDesc desc{.size = 1024};
    EXPECT_EQ(EstimateBufferAlignment(desc), kAlignmentBuffer);
}

// =============================================================================
// D-2: Heap group classification
// =============================================================================

TEST(HeapGroupClassification, ColorAttachIsRtDs) {
    EXPECT_EQ(ClassifyHeapGroup(RGResourceKind::Texture, ResourceAccess::ColorAttachWrite), HeapGroupType::RtDs);
}

TEST(HeapGroupClassification, DepthStencilIsRtDs) {
    EXPECT_EQ(ClassifyHeapGroup(RGResourceKind::Texture, ResourceAccess::DepthStencilWrite), HeapGroupType::RtDs);
}

TEST(HeapGroupClassification, ShaderReadOnlyIsNonRtDs) {
    EXPECT_EQ(ClassifyHeapGroup(RGResourceKind::Texture, ResourceAccess::ShaderReadOnly), HeapGroupType::NonRtDs);
}

TEST(HeapGroupClassification, BufferIsBuffer) {
    EXPECT_EQ(ClassifyHeapGroup(RGResourceKind::Buffer, ResourceAccess::ShaderReadOnly), HeapGroupType::Buffer);
    EXPECT_EQ(ClassifyHeapGroup(RGResourceKind::Buffer, ResourceAccess::ShaderWrite), HeapGroupType::Buffer);
}

// D-1..4: Stage 7 integration — aliasing with non-overlapping lifetimes
// =============================================================================

TEST(TransientAliasing, NonOverlappingResourcesShareSlot) {
    // Chain: WriteA → ReadA_WriteB → ReadB
    // This forces topo order to be sequential, giving A lifetime=[0,1], B lifetime=[1,2].
    // They overlap at pass 1 so they can't share. Instead, use a handoff via a third resource.
    //
    // Better approach: WriteA → ReadA → WriteB → ReadB with explicit dependency chain.
    // Use a "bridge" resource to force ReadA before WriteB in the topo sort.
    RenderGraphBuilder builder;
    auto texA = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 128, .height = 128, .debugName = "A"});
    auto texB = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 128, .height = 128, .debugName = "B"});
    auto bridge = builder.CreateBuffer({.size = 16, .debugName = "Bridge"});

    // Pass 0: write A
    RGResourceHandle writtenA;
    builder.AddGraphicsPass(
        "WriteA",
        [&](PassBuilder& pb) {
            writtenA = pb.WriteColorAttachment(texA);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    // Pass 1: read A, write bridge (creates dependency edge)
    RGResourceHandle writtenBridge;
    builder.AddGraphicsPass(
        "ReadA",
        [&](PassBuilder& pb) {
            pb.ReadTexture(writtenA, ResourceAccess::ShaderReadOnly);
            writtenBridge = pb.WriteBuffer(bridge, ResourceAccess::ShaderWrite);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    // Pass 2: read bridge (forces after ReadA), write B
    RGResourceHandle writtenB;
    builder.AddGraphicsPass(
        "WriteB",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(writtenBridge, ResourceAccess::ShaderReadOnly);
            writtenB = pb.WriteColorAttachment(texB);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    // Pass 3: read B
    builder.AddGraphicsPass(
        "ReadB",
        [&](PassBuilder& pb) {
            pb.ReadTexture(writtenB, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // A lifetime=[0,1], B lifetime=[2,3] — strictly non-overlapping, same heap group, same size
    auto& aliasing = result->aliasing;
    EXPECT_NE(aliasing.resourceToSlot[texA.GetIndex()], AliasingLayout::kNotAliased);
    EXPECT_NE(aliasing.resourceToSlot[texB.GetIndex()], AliasingLayout::kNotAliased);
    EXPECT_EQ(aliasing.resourceToSlot[texA.GetIndex()], aliasing.resourceToSlot[texB.GetIndex()]);
}

TEST(TransientAliasing, OverlappingResourcesGetSeparateSlots) {
    RenderGraphBuilder builder;
    auto texA = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 128, .height = 128, .debugName = "A"});
    auto texB = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 128, .height = 128, .debugName = "B"});

    RGResourceHandle writtenA, writtenB;
    builder.AddGraphicsPass(
        "WriteBoth",
        [&](PassBuilder& pb) {
            writtenA = pb.WriteColorAttachment(texA);
            writtenB = pb.WriteColorAttachment(texB);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "ReadBoth",
        [&](PassBuilder& pb) {
            pb.ReadTexture(writtenA, ResourceAccess::ShaderReadOnly);
            pb.ReadTexture(writtenB, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    auto& aliasing = result->aliasing;
    EXPECT_NE(aliasing.resourceToSlot[texA.GetIndex()], AliasingLayout::kNotAliased);
    EXPECT_NE(aliasing.resourceToSlot[texB.GetIndex()], AliasingLayout::kNotAliased);
    EXPECT_NE(aliasing.resourceToSlot[texA.GetIndex()], aliasing.resourceToSlot[texB.GetIndex()]);
}

TEST(TransientAliasing, ImportedResourcesAreNotAliased) {
    RenderGraphBuilder builder;
    TextureHandle extTex{42};
    auto imported = builder.ImportTexture(extTex, "Imp");

    builder.AddGraphicsPass(
        "Use",
        [&](PassBuilder& pb) {
            pb.ReadTexture(imported, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // Imported resources must not appear in aliasing
    EXPECT_EQ(result->aliasing.resourceToSlot[imported.GetIndex()], AliasingLayout::kNotAliased);
}

TEST(TransientAliasing, AliasingBarriersAtHandoff) {
    RenderGraphBuilder builder;
    auto texA = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 128, .height = 128, .debugName = "A"});
    auto texB = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 128, .height = 128, .debugName = "B"});

    builder.AddGraphicsPass(
        "WriteA",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(texA);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "ReadA",
        [&](PassBuilder& pb) {
            pb.ReadTexture(texA, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "WriteB",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(texB);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "ReadB",
        [&](PassBuilder& pb) {
            pb.ReadTexture(texB, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // If A and B share a slot, there should be an aliasing barrier when B takes over
    auto& aliasing = result->aliasing;
    if (aliasing.resourceToSlot[texA.GetIndex()] == aliasing.resourceToSlot[texB.GetIndex()]) {
        EXPECT_FALSE(aliasing.aliasingBarriers.empty());
        bool foundAliasingBarrierForB = false;
        for (auto& b : aliasing.aliasingBarriers) {
            if (b.resourceIndex == texB.GetIndex() && b.isAliasingBarrier) {
                foundAliasingBarrierForB = true;
                EXPECT_EQ(b.srcAccess, ResourceAccess::None);
                EXPECT_EQ(b.srcLayout, TextureLayout::Undefined);
            }
        }
        EXPECT_TRUE(foundAliasingBarrierForB);
    }
}

TEST(TransientAliasing, HeapGroupSizesAreNonZero) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "T"});
    auto buf = builder.CreateBuffer({.size = 256, .debugName = "B"});

    builder.AddGraphicsPass(
        "Use",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.ReadBuffer(buf, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    auto& aliasing = result->aliasing;
    // RT/DS heap should have size for the texture
    EXPECT_GT(aliasing.heapGroupSizes[static_cast<size_t>(HeapGroupType::RtDs)], 0u);
    // Buffer heap should have size for the buffer
    EXPECT_GT(aliasing.heapGroupSizes[static_cast<size_t>(HeapGroupType::Buffer)], 0u);
}

TEST(TransientAliasing, AlignmentAwareOffsetPacking) {
    RenderGraphBuilder builder;
    // Create several textures to force multiple slots
    auto t1 = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 128, .height = 128, .debugName = "T1"});
    auto t2 = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "T2"});

    // Use both simultaneously (overlapping → separate slots → packed in same heap)
    RGResourceHandle w1, w2;
    builder.AddGraphicsPass(
        "WriteBoth",
        [&](PassBuilder& pb) {
            w1 = pb.WriteColorAttachment(t1);
            w2 = pb.WriteColorAttachment(t2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "ReadBoth",
        [&](PassBuilder& pb) {
            pb.ReadTexture(w1, ResourceAccess::ShaderReadOnly);
            pb.ReadTexture(w2, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // Verify slots have aligned offsets
    for (auto& slot : result->aliasing.slots) {
        if (slot.alignment > 0) {
            EXPECT_EQ(slot.heapOffset % slot.alignment, 0u) << "Slot " << slot.slotIndex << " offset not aligned";
        }
    }
}

TEST(TransientAliasing, DisabledByOption) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "T"});

    builder.AddGraphicsPass(
        "Write",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableTransientAliasing = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // Aliasing should be empty when disabled
    EXPECT_TRUE(result->aliasing.slots.empty());
    EXPECT_TRUE(result->aliasing.aliasingBarriers.empty());
}

TEST(TransientAliasing, BufferAndTextureInDifferentHeapGroups) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "T"});
    auto buf = builder.CreateBuffer({.size = 1024, .debugName = "B"});

    // Sequential non-overlapping usage
    builder.AddGraphicsPass(
        "WriteTex",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "ReadTex",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddComputePass(
        "WriteBuf",
        [&](PassBuilder& pb) {
            pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    auto& aliasing = result->aliasing;
    uint32_t texSlot = aliasing.resourceToSlot[tex.GetIndex()];
    uint32_t bufSlot = aliasing.resourceToSlot[buf.GetIndex()];
    EXPECT_NE(texSlot, AliasingLayout::kNotAliased);
    EXPECT_NE(bufSlot, AliasingLayout::kNotAliased);
    // Different heap groups → different slots even if non-overlapping
    EXPECT_NE(aliasing.slots[texSlot].heapGroup, aliasing.slots[bufSlot].heapGroup);
}

// =============================================================================
// Stage 8: Render Pass Merging tests
// =============================================================================

TEST(RenderPassMerging, ConsecutiveGraphicsPassesSharingDepthAreMerged) {
    RenderGraphBuilder builder;
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 128, .height = 128, .debugName = "Depth"});

    RGResourceHandle writtenDepth;
    builder.AddGraphicsPass(
        "DepthPrePass",
        [&](PassBuilder& pb) {
            writtenDepth = pb.WriteDepthStencil(depth);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "GBufferFill",
        [&](PassBuilder& pb) {
            pb.WriteDepthStencil(writtenDepth);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // Two consecutive graphics passes sharing depth → merged into 1 group
    EXPECT_EQ(result->mergedGroups.size(), 1u);
    if (!result->mergedGroups.empty()) {
        EXPECT_EQ(result->mergedGroups[0].subpassIndices.size(), 2u);
        EXPECT_EQ(result->mergedGroups[0].renderAreaWidth, 128u);
        EXPECT_EQ(result->mergedGroups[0].renderAreaHeight, 128u);
    }
}

TEST(RenderPassMerging, InputAttachmentReadTriggersMerge) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 256, .height = 256, .debugName = "RT"});

    RGResourceHandle writtenRT;
    builder.AddGraphicsPass(
        "GBufferFill",
        [&](PassBuilder& pb) {
            writtenRT = pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "Resolve",
        [&](PassBuilder& pb) {
            pb.ReadTexture(writtenRT, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // Pass reads prev's color attachment → shared attachment → merge
    EXPECT_EQ(result->mergedGroups.size(), 1u);
    if (!result->mergedGroups.empty()) {
        EXPECT_EQ(result->mergedGroups[0].subpassIndices.size(), 2u);
    }
}

TEST(RenderPassMerging, DifferentRenderAreaPreventsmerge) {
    RenderGraphBuilder builder;
    auto rtA = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 128, .height = 128, .debugName = "A"});
    auto rtB = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 256, .height = 256, .debugName = "B"});

    RGResourceHandle writtenA;
    builder.AddGraphicsPass(
        "Pass128",
        [&](PassBuilder& pb) {
            writtenA = pb.WriteColorAttachment(rtA);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "Pass256",
        [&](PassBuilder& pb) {
            pb.ReadTexture(writtenA, ResourceAccess::ShaderReadOnly);
            pb.WriteColorAttachment(rtB);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // Different render areas → not merged
    EXPECT_EQ(result->mergedGroups.size(), 0u);
}

TEST(RenderPassMerging, ComputePassBreaksMergeChain) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 128, .height = 128, .debugName = "RT"});
    auto buf = builder.CreateBuffer({.size = 256, .debugName = "Buf"});

    RGResourceHandle writtenRT;
    builder.AddGraphicsPass(
        "Graphics1",
        [&](PassBuilder& pb) {
            writtenRT = pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    RGResourceHandle writtenBuf;
    builder.AddComputePass(
        "Compute",
        [&](PassBuilder& pb) {
            pb.ReadTexture(writtenRT, ResourceAccess::ShaderReadOnly);
            writtenBuf = pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "Graphics2",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(writtenBuf, ResourceAccess::ShaderReadOnly);
            pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // Compute pass in between → no merging
    EXPECT_EQ(result->mergedGroups.size(), 0u);
}

TEST(RenderPassMerging, NoSharedAttachmentPreventsmerge) {
    RenderGraphBuilder builder;
    auto rtA = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 128, .height = 128, .debugName = "A"});
    auto rtB = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 128, .height = 128, .debugName = "B"});

    builder.AddGraphicsPass(
        "WriteA",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rtA);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "WriteB",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rtB);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // No shared attachment between passes → not merged
    EXPECT_EQ(result->mergedGroups.size(), 0u);
}

TEST(RenderPassMerging, SubpassDependenciesCreated) {
    RenderGraphBuilder builder;
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 128, .height = 128, .debugName = "Depth"});

    RGResourceHandle writtenDepth;
    builder.AddGraphicsPass(
        "Pass0",
        [&](PassBuilder& pb) {
            writtenDepth = pb.WriteDepthStencil(depth);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "Pass1",
        [&](PassBuilder& pb) {
            pb.WriteDepthStencil(writtenDepth);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    ASSERT_EQ(result->mergedGroups.size(), 1u);
    auto& group = result->mergedGroups[0];
    // Should have at least one subpass dependency (barrier converted)
    EXPECT_GE(group.dependencies.size(), 1u);
    if (!group.dependencies.empty()) {
        EXPECT_EQ(group.dependencies[0].srcSubpass, 0u);
        EXPECT_EQ(group.dependencies[0].dstSubpass, 1u);
        EXPECT_TRUE(group.dependencies[0].byRegion);
    }
}

TEST(RenderPassMerging, MergedBarriersRemovedFromPasses) {
    RenderGraphBuilder builder;
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 128, .height = 128, .debugName = "Depth"});

    RGResourceHandle writtenDepth;
    builder.AddGraphicsPass(
        "Pass0",
        [&](PassBuilder& pb) {
            writtenDepth = pb.WriteDepthStencil(depth);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "Pass1",
        [&](PassBuilder& pb) {
            pb.WriteDepthStencil(writtenDepth);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    ASSERT_EQ(result->mergedGroups.size(), 1u);
    auto& group = result->mergedGroups[0];
    ASSERT_GE(group.subpassIndices.size(), 2u);

    // The second subpass should have its inter-pass acquire barriers removed
    // (converted to subpass dependencies). Only aliasing/cross-queue barriers remain.
    auto& secondPass = result->passes[group.subpassIndices[1]];
    for (auto& b : secondPass.acquireBarriers) {
        EXPECT_TRUE(b.isAliasingBarrier || b.isCrossQueue);
    }
}

TEST(RenderPassMerging, DisabledByOption) {
    RenderGraphBuilder builder;
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 128, .height = 128, .debugName = "Depth"});

    RGResourceHandle writtenDepth;
    builder.AddGraphicsPass(
        "Pass0",
        [&](PassBuilder& pb) {
            writtenDepth = pb.WriteDepthStencil(depth);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "Pass1",
        [&](PassBuilder& pb) {
            pb.WriteDepthStencil(writtenDepth);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->mergedGroups.size(), 0u);
}

TEST(RenderPassMerging, ThreePassChainMergedIntoOneGroup) {
    RenderGraphBuilder builder;
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 128, .height = 128, .debugName = "Depth"});

    RGResourceHandle d0;
    builder.AddGraphicsPass(
        "Pass0",
        [&](PassBuilder& pb) {
            d0 = pb.WriteDepthStencil(depth);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    RGResourceHandle d1;
    builder.AddGraphicsPass(
        "Pass1",
        [&](PassBuilder& pb) {
            d1 = pb.WriteDepthStencil(d0);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "Pass2",
        [&](PassBuilder& pb) {
            pb.WriteDepthStencil(d1);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // Transitivity: 3 consecutive passes sharing depth → 1 merged group with 3 subpasses
    EXPECT_EQ(result->mergedGroups.size(), 1u);
    if (!result->mergedGroups.empty()) {
        EXPECT_EQ(result->mergedGroups[0].subpassIndices.size(), 3u);
    }
}

// =============================================================================
// Stage 9: Backend Adaptation Pass Injection tests
// =============================================================================

TEST(AdaptationPassInjection, MockBackendInjectsNothing) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "Tex"});

    builder.AddGraphicsPass(
        "Blit",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    // Mock backend → no adaptation
    RenderGraphCompiler::Options opts;
    opts.backendType = BackendType::Mock;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->adaptationPasses.size(), 0u);
}

TEST(AdaptationPassInjection, VulkanNativeNoAdaptation) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "Tex"});

    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.backendType = BackendType::Vulkan14;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // Vulkan supports everything natively → no adaptation passes
    EXPECT_EQ(result->adaptationPasses.size(), 0u);
}

TEST(AdaptationPassInjection, DisabledByOption) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "Tex"});

    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.backendType = BackendType::WebGPU;
    opts.enableAdaptation = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->adaptationPasses.size(), 0u);
}

TEST(AdaptationPassInjection, WebGPUBlitTriggersShaderEmulation) {
    RenderGraphBuilder builder;
    auto src = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "Src"});
    auto dst = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "Dst"});

    RGResourceHandle writtenSrc;
    builder.AddGraphicsPass(
        "Produce",
        [&](PassBuilder& pb) {
            writtenSrc = pb.WriteColorAttachment(src);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddTransferPass(
        "Blit",
        [&](PassBuilder& pb) {
            pb.ReadTexture(writtenSrc, ResourceAccess::TransferSrc);
            pb.WriteTexture(dst, ResourceAccess::TransferDst);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.backendType = BackendType::WebGPU;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // WebGPU CmdBlitTexture → ShaderEmulation (has GPU overhead) → adaptation pass injected
    EXPECT_GE(result->adaptationPasses.size(), 1u);
    if (!result->adaptationPasses.empty()) {
        auto& ap = result->adaptationPasses[0];
        EXPECT_EQ(ap.feature, adaptation::Feature::CmdBlitTexture);
        EXPECT_EQ(ap.strategy, adaptation::Strategy::ShaderEmulation);
        EXPECT_NE(ap.description, nullptr);
    }
}

TEST(AdaptationPassInjection, D3D12BlitTriggersShaderEmulation) {
    RenderGraphBuilder builder;
    auto src = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "Src"});
    auto dst = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 64, .height = 64, .debugName = "Dst"});

    RGResourceHandle writtenSrc;
    builder.AddGraphicsPass(
        "Produce",
        [&](PassBuilder& pb) {
            writtenSrc = pb.WriteColorAttachment(src);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.AddTransferPass(
        "Blit",
        [&](PassBuilder& pb) {
            pb.ReadTexture(writtenSrc, ResourceAccess::TransferSrc);
            pb.WriteTexture(dst, ResourceAccess::TransferDst);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.backendType = BackendType::D3D12;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // D3D12 CmdBlitTexture → ShaderEmulation
    EXPECT_GE(result->adaptationPasses.size(), 1u);
    if (!result->adaptationPasses.empty()) {
        bool foundBlit = false;
        for (auto& ap : result->adaptationPasses) {
            if (ap.feature == adaptation::Feature::CmdBlitTexture) {
                EXPECT_EQ(ap.strategy, adaptation::Strategy::ShaderEmulation);
                foundBlit = true;
            }
        }
        EXPECT_TRUE(foundBlit);
    }
}

TEST(AdaptationPassInjection, WebGPUBufferFillTriggersShaderEmulation) {
    RenderGraphBuilder builder;
    auto buf = builder.CreateBuffer({.size = 1024, .debugName = "Buf"});

    builder.AddTransferPass(
        "Fill",
        [&](PassBuilder& pb) {
            pb.WriteBuffer(buf, ResourceAccess::TransferDst);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.backendType = BackendType::WebGPU;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // WebGPU CmdFillBufferNonZero → ShaderEmulation
    EXPECT_GE(result->adaptationPasses.size(), 1u);
    if (!result->adaptationPasses.empty()) {
        bool foundFill = false;
        for (auto& ap : result->adaptationPasses) {
            if (ap.feature == adaptation::Feature::CmdFillBufferNonZero) {
                EXPECT_EQ(ap.strategy, adaptation::Strategy::ShaderEmulation);
                foundFill = true;
            }
        }
        EXPECT_TRUE(foundFill);
    }
}

// =============================================================================
// Stage 3b: Barrier-Aware Global Reordering Tests (D-11)
// =============================================================================

TEST(BarrierAwareReordering, PreservesTopologicalOrder) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
    builder.AddGraphicsPass("A", [&](PassBuilder& pb) { pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = true;
    opts.strategy = SchedulerStrategy::MinBarriers;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
}

TEST(BarrierAwareReordering, DisabledByDefault) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 128, .height = 128, .debugName = "RT"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = false;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
}

TEST(BarrierAwareReordering, IndependentPassesStayValid) {
    RenderGraphBuilder builder;
    auto tA = builder.CreateTexture({.width = 256, .height = 256, .debugName = "A"});
    auto tB = builder.CreateTexture({.width = 256, .height = 256, .debugName = "B"});
    builder.AddGraphicsPass(
        "WA",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tA);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "WB",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tB);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = true;
    opts.strategy = SchedulerStrategy::MinBarriers;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
}

TEST(BarrierAwareReordering, BalancedStrategyProducesValidResult) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 512, .height = 512, .debugName = "RT"});
    builder.AddGraphicsPass("Draw", [&](PassBuilder& pb) { pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "Post",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = true;
    opts.strategy = SchedulerStrategy::Balanced;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
    EXPECT_FALSE(result->batches.empty());
}

TEST(BarrierAwareReordering, MinMemoryStrategyCompiles) {
    RenderGraphBuilder builder;
    auto t1 = builder.CreateTexture({.width = 1024, .height = 1024, .debugName = "T1"});
    auto t2 = builder.CreateTexture({.width = 1024, .height = 1024, .debugName = "T2"});
    builder.AddGraphicsPass(
        "P1",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(t1);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "P2",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(t2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = true;
    opts.strategy = SchedulerStrategy::MinMemory;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
}

TEST(BarrierAwareReordering, MinLatencyStrategyCompiles) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
    builder.AddGraphicsPass("Draw", [&](PassBuilder& pb) { pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "Post",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = true;
    opts.strategy = SchedulerStrategy::MinLatency;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
}

TEST(BarrierAwareReordering, FourPassDiamondDAG) {
    // Diamond: A -> B, A -> C, B -> D, C -> D
    RenderGraphBuilder builder;
    auto tAB = builder.CreateTexture({.width = 256, .height = 256, .debugName = "AB"});
    auto tAC = builder.CreateTexture({.width = 256, .height = 256, .debugName = "AC"});
    auto tBD = builder.CreateTexture({.width = 256, .height = 256, .debugName = "BD"});
    builder.AddGraphicsPass(
        "A",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tAB);
            pb.WriteColorAttachment(tAC, 1);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tAB);
            pb.WriteColorAttachment(tBD);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass("C", [&](PassBuilder& pb) { pb.ReadTexture(tAC); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "D",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tBD);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = true;
    opts.strategy = SchedulerStrategy::MinBarriers;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    // C may be culled (no side effects, no consumer) — expect 3 or 4 passes
    EXPECT_GE(result->passes.size(), 3u);
}

TEST(BarrierAwareReordering, SinglePassNoReorder) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    builder.AddGraphicsPass(
        "Only",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = true;
    opts.strategy = SchedulerStrategy::MinBarriers;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 1u);
}

// =============================================================================
// Stage 10: Command Batch Formation Tests (D-9)
// =============================================================================

TEST(CommandBatchFormation, SingleQueueSingleBatch) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
    builder.AddGraphicsPass("Draw", [&](PassBuilder& pb) { pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "Post",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->batches.size(), 1u);
    EXPECT_EQ(result->batches[0].queue, RGQueueType::Graphics);
    EXPECT_EQ(result->batches[0].passIndices.size(), 2u);
    EXPECT_TRUE(result->batches[0].signalTimeline);
    EXPECT_TRUE(result->batches[0].waits.empty());
}

TEST(CommandBatchFormation, EmptyGraphNoBatches) {
    RenderGraphBuilder builder;
    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->batches.empty());
}

TEST(CommandBatchFormation, ThreePassChainOneBatch) {
    RenderGraphBuilder builder;
    auto t1 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "T1"});
    auto t2 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "T2"});
    builder.AddGraphicsPass("P1", [&](PassBuilder& pb) { pb.WriteColorAttachment(t1); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "P2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t1);
            pb.WriteColorAttachment(t2);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "P3",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->batches.size(), 1u);
    EXPECT_EQ(result->batches[0].passIndices.size(), 3u);
}

TEST(CommandBatchFormation, LastBatchAlwaysSignals) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->batches.empty());
    EXPECT_TRUE(result->batches.back().signalTimeline);
}

TEST(CommandBatchFormation, AllPassesCoveredByBatches) {
    RenderGraphBuilder builder;
    auto t1 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "T1"});
    auto t2 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "T2"});
    builder.AddGraphicsPass("A", [&](PassBuilder& pb) { pb.WriteColorAttachment(t1); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t1);
            pb.WriteColorAttachment(t2);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "C",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    uint32_t totalInBatches = 0;
    for (auto& batch : result->batches) {
        totalInBatches += static_cast<uint32_t>(batch.passIndices.size());
    }
    EXPECT_EQ(totalInBatches, static_cast<uint32_t>(result->passes.size()));
}

TEST(CommandBatchFormation, BatchPassOrderIsAscending) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
    builder.AddGraphicsPass("P1", [&](PassBuilder& pb) { pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "P2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    for (auto& batch : result->batches) {
        for (size_t i = 1; i < batch.passIndices.size(); ++i) {
            EXPECT_GT(batch.passIndices[i], batch.passIndices[i - 1]);
        }
    }
}

TEST(CommandBatchFormation, SinglePassBatch) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    builder.AddGraphicsPass(
        "Only",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->batches.size(), 1u);
    EXPECT_EQ(result->batches[0].passIndices.size(), 1u);
}

TEST(CommandBatchFormation, BatchQueueMatchesPassQueue) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    for (auto& batch : result->batches) {
        for (auto pos : batch.passIndices) {
            EXPECT_EQ(result->passes[pos].queue, batch.queue);
        }
    }
}

// =============================================================================
// D-10: Parallel Compilation Correctness (Stage 6 || 7)
// =============================================================================

TEST(ParallelCompilation, DeterministicOutput) {
    // Compile same graph twice, results must be identical
    auto buildGraph = []() {
        RenderGraphBuilder builder;
        auto rt = builder.CreateTexture({.width = 512, .height = 512, .debugName = "RT"});
        auto depth
            = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 512, .height = 512, .debugName = "Depth"});
        builder.AddGraphicsPass(
            "GBuffer",
            [&](PassBuilder& pb) {
                pb.WriteColorAttachment(rt);
                pb.WriteDepthStencil(depth);
            },
            [](RenderPassContext&) {}
        );
        builder.AddGraphicsPass(
            "Lighting",
            [&](PassBuilder& pb) {
                pb.ReadTexture(rt);
                pb.ReadTexture(depth);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        builder.Build();
        return builder;
    };

    RenderGraphCompiler::Options opts;
    opts.enableTransientAliasing = true;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);

    auto b1 = buildGraph();
    auto r1 = compiler.Compile(b1);
    ASSERT_TRUE(r1.has_value());

    auto b2 = buildGraph();
    auto r2 = compiler.Compile(b2);
    ASSERT_TRUE(r2.has_value());

    // Same number of passes, batches, sync points
    EXPECT_EQ(r1->passes.size(), r2->passes.size());
    EXPECT_EQ(r1->batches.size(), r2->batches.size());
    EXPECT_EQ(r1->syncPoints.size(), r2->syncPoints.size());
    EXPECT_EQ(r1->lifetimes.size(), r2->lifetimes.size());

    // Barrier counts match
    for (size_t i = 0; i < r1->passes.size() && i < r2->passes.size(); ++i) {
        EXPECT_EQ(r1->passes[i].acquireBarriers.size(), r2->passes[i].acquireBarriers.size());
        EXPECT_EQ(r1->passes[i].releaseBarriers.size(), r2->passes[i].releaseBarriers.size());
    }
}

TEST(ParallelCompilation, AliasingAndBarriersCoexist) {
    // Ensure both aliasing and barriers are correctly populated when both enabled
    RenderGraphBuilder builder;
    auto rt1 = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT1"});
    auto rt2 = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT2"});
    builder.AddGraphicsPass("Pass1", [&](PassBuilder& pb) { pb.WriteColorAttachment(rt1); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "Pass2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt1);
            pb.WriteColorAttachment(rt2);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Pass3",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableTransientAliasing = true;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 3u);
    // Lifetimes should be computed
    EXPECT_FALSE(result->lifetimes.empty());
    // Barriers should exist between passes
    bool hasBarriers = false;
    for (auto& p : result->passes) {
        if (!p.acquireBarriers.empty() || !p.releaseBarriers.empty()) {
            hasBarriers = true;
            break;
        }
    }
    EXPECT_TRUE(hasBarriers);
}

TEST(ParallelCompilation, FullPipelineAllStages) {
    // Exercise all stages together: reordering + aliasing + merging + adaptation + batches
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 512, .height = 512, .debugName = "RT"});
    builder.AddGraphicsPass("Draw", [&](PassBuilder& pb) { pb.WriteColorAttachment(rt); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "Post",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = true;
    opts.strategy = SchedulerStrategy::Balanced;
    opts.enableTransientAliasing = true;
    opts.enableRenderPassMerging = true;
    opts.enableAdaptation = true;
    opts.backendType = BackendType::Vulkan14;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
    EXPECT_FALSE(result->batches.empty());
    // All passes must be in batches
    uint32_t batchedCount = 0;
    for (auto& b : result->batches) {
        batchedCount += static_cast<uint32_t>(b.passIndices.size());
    }
    EXPECT_EQ(batchedCount, static_cast<uint32_t>(result->passes.size()));
}

// =============================================================================
// Deep Validation: Comprehensive Integration Tests
// =============================================================================

// Helper: find compiled pass index by original pass index
static auto FindCompiledPass(const CompiledRenderGraph& cg, uint32_t originalIdx) -> int {
    for (size_t i = 0; i < cg.passes.size(); ++i) {
        if (cg.passes[i].passIndex == originalIdx) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Helper: verify topological ordering — for every edge (src -> dst), src must appear before dst in compiled order
static void VerifyTopologicalOrder(const CompiledRenderGraph& cg) {
    // Build position map: original pass index -> compiled position
    std::unordered_map<uint32_t, uint32_t> posMap;
    for (uint32_t i = 0; i < cg.passes.size(); ++i) {
        posMap[cg.passes[i].passIndex] = i;
    }
    for (auto& edge : cg.edges) {
        auto srcIt = posMap.find(edge.srcPass);
        auto dstIt = posMap.find(edge.dstPass);
        if (srcIt != posMap.end() && dstIt != posMap.end()) {
            EXPECT_LT(srcIt->second, dstIt->second)
                << "Edge violation: pass " << edge.srcPass << " (pos " << srcIt->second << ") must precede pass "
                << edge.dstPass << " (pos " << dstIt->second << ")";
        }
    }
}

// Helper: verify every compiled pass has at least an acquire barrier for its first resource access
// (except the very first pass which may have UNDEFINED -> X)
static void VerifyBarrierStructure(const CompiledRenderGraph& cg) {
    for (auto& pass : cg.passes) {
        for (auto& barrier : pass.acquireBarriers) {
            // dstAccess must not be None (acquiring something)
            EXPECT_NE(barrier.dstAccess, ResourceAccess::None)
                << "Acquire barrier on resource " << barrier.resourceIndex << " has no dstAccess";
            // dstLayout must not be Undefined for textures (we are transitioning TO a usable layout)
            // (srcLayout CAN be Undefined for first use or aliasing)
        }
        for (auto& barrier : pass.releaseBarriers) {
            // srcAccess must not be None (releasing something we used)
            EXPECT_NE(barrier.srcAccess, ResourceAccess::None)
                << "Release barrier on resource " << barrier.resourceIndex << " has no srcAccess";
        }
    }
}

TEST(DeepValidation, ThreePassChainBarriersAndLifetimes) {
    // A -> B -> C chain: A writes RT1, B reads RT1 + writes RT2, C reads RT2
    // Expected: 2 RAW edges, barriers at B (RT1 transition) and C (RT2 transition)
    RenderGraphBuilder builder;
    auto rt1 = builder.CreateTexture({.width = 512, .height = 512, .debugName = "RT1"});
    auto rt2 = builder.CreateTexture({.width = 512, .height = 512, .debugName = "RT2"});

    auto hA = builder.AddGraphicsPass(
        "PassA", [&](PassBuilder& pb) { rt1 = pb.WriteColorAttachment(rt1); }, [](RenderPassContext&) {}
    );
    auto hB = builder.AddGraphicsPass(
        "PassB",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt1);
            rt2 = pb.WriteColorAttachment(rt2);
        },
        [](RenderPassContext&) {}
    );
    auto hC = builder.AddGraphicsPass(
        "PassC",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableTransientAliasing = true;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    auto& cg = *result;

    // --- Pass count & ordering ---
    EXPECT_EQ(cg.passes.size(), 3u);
    VerifyTopologicalOrder(cg);

    // A must precede B, B must precede C
    int posA = FindCompiledPass(cg, hA.index);
    int posB = FindCompiledPass(cg, hB.index);
    int posC = FindCompiledPass(cg, hC.index);
    ASSERT_GE(posA, 0);
    ASSERT_GE(posB, 0);
    ASSERT_GE(posC, 0);
    EXPECT_LT(posA, posB);
    EXPECT_LT(posB, posC);

    // --- DAG edges ---
    // Should have at least 2 RAW edges: A->B (RT1), B->C (RT2)
    EXPECT_GE(cg.edges.size(), 2u);
    bool foundAB = false, foundBC = false;
    for (auto& e : cg.edges) {
        if (e.srcPass == hA.index && e.dstPass == hB.index) {
            EXPECT_EQ(e.hazard, HazardType::RAW);
            foundAB = true;
        }
        if (e.srcPass == hB.index && e.dstPass == hC.index) {
            EXPECT_EQ(e.hazard, HazardType::RAW);
            foundBC = true;
        }
    }
    EXPECT_TRUE(foundAB) << "Missing edge A->B";
    EXPECT_TRUE(foundBC) << "Missing edge B->C";

    // --- Barrier validation ---
    VerifyBarrierStructure(cg);

    // PassB should have an acquire barrier for RT1 (ColorAttachWrite -> ShaderReadOnly)
    auto& passB = cg.passes[posB];
    bool hasRt1Acquire = false;
    for (auto& b : passB.acquireBarriers) {
        if (b.resourceIndex == rt1.GetIndex()) {
            hasRt1Acquire = true;
            EXPECT_NE(b.dstLayout, TextureLayout::Undefined);
        }
    }
    EXPECT_TRUE(hasRt1Acquire) << "PassB missing acquire barrier for RT1";

    // PassC should have an acquire barrier for RT2
    auto& passC = cg.passes[posC];
    bool hasRt2Acquire = false;
    for (auto& b : passC.acquireBarriers) {
        if (b.resourceIndex == rt2.GetIndex()) {
            hasRt2Acquire = true;
            EXPECT_NE(b.dstLayout, TextureLayout::Undefined);
        }
    }
    EXPECT_TRUE(hasRt2Acquire) << "PassC missing acquire barrier for RT2";

    // --- Lifetime validation ---
    // RT1 lifetime: first=posA, last=posB; RT2 lifetime: first=posB, last=posC
    for (auto& lt : cg.lifetimes) {
        if (lt.resourceIndex == rt1.GetIndex()) {
            EXPECT_EQ(lt.firstPass, static_cast<uint32_t>(posA));
            EXPECT_EQ(lt.lastPass, static_cast<uint32_t>(posB));
        }
        if (lt.resourceIndex == rt2.GetIndex()) {
            EXPECT_EQ(lt.firstPass, static_cast<uint32_t>(posB));
            EXPECT_EQ(lt.lastPass, static_cast<uint32_t>(posC));
        }
    }

    // --- Aliasing validation ---
    // RT1 [posA, posB] and RT2 [posB, posC] overlap at posB, so they should NOT share a slot
    if (cg.aliasing.resourceToSlot[rt1.GetIndex()] != AliasingLayout::kNotAliased
        && cg.aliasing.resourceToSlot[rt2.GetIndex()] != AliasingLayout::kNotAliased) {
        // If both are aliased, they must be in different slots (overlapping lifetimes)
        EXPECT_NE(cg.aliasing.resourceToSlot[rt1.GetIndex()], cg.aliasing.resourceToSlot[rt2.GetIndex()])
            << "RT1 and RT2 have overlapping lifetimes but share a slot";
    }

    // --- Batch validation ---
    EXPECT_FALSE(cg.batches.empty());
    EXPECT_TRUE(cg.batches.back().signalTimeline);
    // All 3 passes on graphics queue -> 1 batch
    EXPECT_EQ(cg.batches.size(), 1u);
    EXPECT_EQ(cg.batches[0].queue, RGQueueType::Graphics);
    EXPECT_EQ(cg.batches[0].passIndices.size(), 3u);
}

TEST(DeepValidation, DiamondDAGWithAliasingOpportunity) {
    // Diamond: A writes tAB + tAC; B reads tAB writes tBD; C reads tAC; D reads tBD
    // tAB lifetime [0,1], tAC lifetime [0,1or2], tBD lifetime [1,3]
    // If C is culled (no side effects), tAC may not appear in compiled output.
    // tAB and tBD do NOT overlap -> can share a slot if same size.
    RenderGraphBuilder builder;
    auto tAB = builder.CreateTexture({.width = 256, .height = 256, .debugName = "tAB"});
    auto tAC = builder.CreateTexture({.width = 256, .height = 256, .debugName = "tAC"});
    auto tBD = builder.CreateTexture({.width = 256, .height = 256, .debugName = "tBD"});

    auto hA = builder.AddGraphicsPass(
        "A",
        [&](PassBuilder& pb) {
            tAB = pb.WriteColorAttachment(tAB);
            tAC = pb.WriteColorAttachment(tAC, 1);
        },
        [](RenderPassContext&) {}
    );
    auto hB = builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tAB);
            tBD = pb.WriteColorAttachment(tBD);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "C",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tAC);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    auto hD = builder.AddGraphicsPass(
        "D",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tBD);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableTransientAliasing = true;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    auto& cg = *result;

    // --- Topological order ---
    VerifyTopologicalOrder(cg);

    // A must be first (all others depend on it)
    int posA = FindCompiledPass(cg, hA.index);
    int posB = FindCompiledPass(cg, hB.index);
    int posD = FindCompiledPass(cg, hD.index);
    ASSERT_GE(posA, 0);
    ASSERT_GE(posB, 0);
    ASSERT_GE(posD, 0);
    EXPECT_LT(posA, posB);
    EXPECT_LT(posB, posD);

    // --- DAG edges: at least A->B and B->D ---
    bool hasAB = false, hasBD = false;
    for (auto& e : cg.edges) {
        if (e.srcPass == hA.index && e.dstPass == hB.index) {
            hasAB = true;
        }
        if (e.srcPass == hB.index && e.dstPass == hD.index) {
            hasBD = true;
        }
    }
    EXPECT_TRUE(hasAB);
    EXPECT_TRUE(hasBD);

    // --- Barrier structure ---
    VerifyBarrierStructure(cg);

    // --- Aliasing: tAB and tBD have non-overlapping lifetimes (same size/format) ---
    // tAB: used in A(write) and B(read) -> lifetime ends at posB
    // tBD: used in B(write) and D(read) -> lifetime starts at posB
    // They may or may not share a slot depending on whether the aliaser considers
    // posB as overlapping (both accessed in same pass B). Let's just verify consistency:
    auto slotAB = cg.aliasing.resourceToSlot[tAB.GetIndex()];
    auto slotBD = cg.aliasing.resourceToSlot[tBD.GetIndex()];
    if (slotAB != AliasingLayout::kNotAliased && slotBD != AliasingLayout::kNotAliased && slotAB == slotBD) {
        // If they share a slot, verify an aliasing barrier exists at tBD's first use
        bool hasAliasingBarrier = false;
        for (auto& b : cg.aliasing.aliasingBarriers) {
            if (b.resourceIndex == tBD.GetIndex() && b.isAliasingBarrier) {
                hasAliasingBarrier = true;
                EXPECT_EQ(b.srcLayout, TextureLayout::Undefined);
            }
        }
        EXPECT_TRUE(hasAliasingBarrier) << "Shared slot but no aliasing barrier for tBD";
    }

    // --- Heap size consistency ---
    for (auto& slot : cg.aliasing.slots) {
        EXPECT_GT(slot.size, 0u) << "Slot " << slot.slotIndex << " has zero size";
        EXPECT_GT(slot.alignment, 0u) << "Slot " << slot.slotIndex << " has zero alignment";
    }
}

TEST(DeepValidation, NonOverlappingResourcesShareSlot) {
    // Three sequential passes, each writing a different texture of the same size.
    // Only the last pass has side effects. Lifetimes:
    // tA: [0,1], tB: [1,2], tC: [2,2]
    // tA and tC never overlap -> should share a slot.
    RenderGraphBuilder builder;
    auto tA = builder.CreateTexture({.width = 128, .height = 128, .debugName = "tA"});
    auto tB = builder.CreateTexture({.width = 128, .height = 128, .debugName = "tB"});
    auto tC = builder.CreateTexture({.width = 128, .height = 128, .debugName = "tC"});

    builder.AddGraphicsPass(
        "PA", [&](PassBuilder& pb) { tA = pb.WriteColorAttachment(tA); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "PB",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tA);
            tB = pb.WriteColorAttachment(tB);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "PC",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tB);
            tC = pb.WriteColorAttachment(tC);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableTransientAliasing = true;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    auto& cg = *result;

    EXPECT_EQ(cg.passes.size(), 3u);
    VerifyTopologicalOrder(cg);

    // tA lifetime ends at pos 1, tC starts at pos 2 -> non-overlapping
    auto slotA = cg.aliasing.resourceToSlot[tA.GetIndex()];
    auto slotC = cg.aliasing.resourceToSlot[tC.GetIndex()];
    if (slotA != AliasingLayout::kNotAliased && slotC != AliasingLayout::kNotAliased) {
        EXPECT_EQ(slotA, slotC) << "tA and tC have non-overlapping lifetimes but are not sharing a slot";

        // If sharing, there must be an aliasing barrier for tC (handoff from tA)
        bool foundAliasingBarrier = false;
        for (auto& b : cg.aliasing.aliasingBarriers) {
            if (b.resourceIndex == tC.GetIndex() && b.isAliasingBarrier) {
                foundAliasingBarrier = true;
                EXPECT_EQ(b.srcAccess, ResourceAccess::None);
                EXPECT_EQ(b.srcLayout, TextureLayout::Undefined);
            }
        }
        EXPECT_TRUE(foundAliasingBarrier) << "tC reuses tA's slot but no aliasing barrier emitted";
    }

    // Heap group size should be > 0 for RtDs
    EXPECT_GT(cg.aliasing.heapGroupSizes[static_cast<size_t>(HeapGroupType::RtDs)], 0u);
}

TEST(DeepValidation, CrossQueueSyncBatchesAndWaits) {
    // Graphics pass writes RT, async compute reads it, graphics reads compute output
    // This creates cross-queue sync points and multiple batches.
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
    auto compOut = builder.CreateTexture({.width = 256, .height = 256, .debugName = "CompOut"});

    auto hDraw = builder.AddGraphicsPass(
        "Draw", [&](PassBuilder& pb) { rt = pb.WriteColorAttachment(rt); }, [](RenderPassContext&) {}
    );
    auto hComp = builder.AddAsyncComputePass(
        "AsyncBlur",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt);
            compOut = pb.WriteTexture(compOut);
        },
        [](RenderPassContext&) {}
    );
    auto hPost = builder.AddGraphicsPass(
        "PostProcess",
        [&](PassBuilder& pb) {
            pb.ReadTexture(compOut);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableTransientAliasing = true;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    auto& cg = *result;

    EXPECT_EQ(cg.passes.size(), 3u);
    VerifyTopologicalOrder(cg);
    VerifyBarrierStructure(cg);

    // --- Queue assignment validation ---
    int posDraw = FindCompiledPass(cg, hDraw.index);
    int posComp = FindCompiledPass(cg, hComp.index);
    int posPost = FindCompiledPass(cg, hPost.index);
    ASSERT_GE(posDraw, 0);
    ASSERT_GE(posComp, 0);
    ASSERT_GE(posPost, 0);

    EXPECT_EQ(cg.passes[posDraw].queue, RGQueueType::Graphics);
    EXPECT_EQ(cg.passes[posComp].queue, RGQueueType::AsyncCompute);
    EXPECT_EQ(cg.passes[posPost].queue, RGQueueType::Graphics);

    // --- Cross-queue sync points ---
    // Should have sync points for Graphics->AsyncCompute and AsyncCompute->Graphics
    bool hasGfxToComp = false, hasCompToGfx = false;
    for (auto& sp : cg.syncPoints) {
        if (sp.srcQueue == RGQueueType::Graphics && sp.dstQueue == RGQueueType::AsyncCompute) {
            hasGfxToComp = true;
        }
        if (sp.srcQueue == RGQueueType::AsyncCompute && sp.dstQueue == RGQueueType::Graphics) {
            hasCompToGfx = true;
        }
    }
    EXPECT_TRUE(hasGfxToComp) << "Missing Graphics->AsyncCompute sync point";
    EXPECT_TRUE(hasCompToGfx) << "Missing AsyncCompute->Graphics sync point";

    // --- Batch validation ---
    // Should have multiple batches since queue changes:
    // Batch 0: Graphics (Draw), Batch 1: AsyncCompute (AsyncBlur), Batch 2: Graphics (PostProcess)
    EXPECT_GE(cg.batches.size(), 2u);

    // Find the AsyncCompute batch
    bool foundAsyncBatch = false;
    for (auto& batch : cg.batches) {
        if (batch.queue == RGQueueType::AsyncCompute) {
            foundAsyncBatch = true;
            // This batch must wait on graphics queue
            bool waitsOnGfx = false;
            for (auto& w : batch.waits) {
                if (w.srcQueue == RGQueueType::Graphics) {
                    waitsOnGfx = true;
                }
            }
            EXPECT_TRUE(waitsOnGfx) << "AsyncCompute batch should wait on Graphics";
        }
    }
    EXPECT_TRUE(foundAsyncBatch) << "No AsyncCompute batch found";

    // The last batch (which contains PostProcess) should signal timeline
    EXPECT_TRUE(cg.batches.back().signalTimeline);

    // --- Cross-queue barriers ---
    // The async compute pass should have a cross-queue acquire barrier for RT
    auto& compPass = cg.passes[posComp];
    bool hasCrossQueueBarrier = false;
    for (auto& b : compPass.acquireBarriers) {
        if (b.isCrossQueue && b.resourceIndex == rt.GetIndex()) {
            hasCrossQueueBarrier = true;
            EXPECT_EQ(b.srcQueue, RGQueueType::Graphics);
            EXPECT_EQ(b.dstQueue, RGQueueType::AsyncCompute);
        }
    }
    EXPECT_TRUE(hasCrossQueueBarrier) << "AsyncCompute pass missing cross-queue barrier for RT";
}

TEST(DeepValidation, ReorderingPreservesDependencies) {
    // Build a graph where reordering could potentially violate dependencies if buggy.
    // A(writes t1) -> B(reads t1, writes t2) -> D(reads t2, side effects)
    // C(writes t3, side effects) — independent of A/B/D
    // Reordering might move C around, but A<B<D must hold.
    RenderGraphBuilder builder;
    auto t1 = builder.CreateTexture({.width = 256, .height = 256, .debugName = "t1"});
    auto t2 = builder.CreateTexture({.width = 256, .height = 256, .debugName = "t2"});
    auto t3 = builder.CreateTexture({.width = 256, .height = 256, .debugName = "t3"});

    auto hA = builder.AddGraphicsPass(
        "A", [&](PassBuilder& pb) { t1 = pb.WriteColorAttachment(t1); }, [](RenderPassContext&) {}
    );
    auto hB = builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t1);
            t2 = pb.WriteColorAttachment(t2);
        },
        [](RenderPassContext&) {}
    );
    auto hC = builder.AddGraphicsPass(
        "C",
        [&](PassBuilder& pb) {
            t3 = pb.WriteColorAttachment(t3);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    auto hD = builder.AddGraphicsPass(
        "D",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    // Test with all 4 strategies
    for (auto strategy :
         {SchedulerStrategy::MinBarriers, SchedulerStrategy::MinMemory, SchedulerStrategy::MinLatency,
          SchedulerStrategy::Balanced}) {
        RenderGraphCompiler::Options opts;
        opts.enableBarrierReordering = true;
        opts.strategy = strategy;
        opts.enableRenderPassMerging = false;
        opts.enableTransientAliasing = true;
        RenderGraphCompiler compiler(opts);
        auto result = compiler.Compile(builder);
        ASSERT_TRUE(result.has_value()) << "Failed with strategy " << static_cast<int>(strategy);
        auto& cg = *result;

        // Topological invariant must hold regardless of strategy
        VerifyTopologicalOrder(cg);
        VerifyBarrierStructure(cg);

        int posA = FindCompiledPass(cg, hA.index);
        int posB = FindCompiledPass(cg, hB.index);
        int posD = FindCompiledPass(cg, hD.index);
        ASSERT_GE(posA, 0);
        ASSERT_GE(posB, 0);
        ASSERT_GE(posD, 0);
        EXPECT_LT(posA, posB) << "A must precede B (strategy " << static_cast<int>(strategy) << ")";
        EXPECT_LT(posB, posD) << "B must precede D (strategy " << static_cast<int>(strategy) << ")";

        // C can be anywhere (independent) but must exist
        int posC = FindCompiledPass(cg, hC.index);
        EXPECT_GE(posC, 0) << "C should not be culled (has side effects)";
    }
}

TEST(DeepValidation, SplitBarriersWhenGapExists) {
    // A writes RT, B is independent (side effects), C reads RT
    // There's a gap (B) between A and C, so the compiler should emit split barriers:
    // A: release half, C: acquire half
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 512, .height = 512, .debugName = "RT"});
    auto scratch = builder.CreateTexture({.width = 64, .height = 64, .debugName = "scratch"});

    auto hA = builder.AddGraphicsPass(
        "A", [&](PassBuilder& pb) { rt = pb.WriteColorAttachment(rt); }, [](RenderPassContext&) {}
    );
    auto hB = builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            scratch = pb.WriteColorAttachment(scratch);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "C",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    // Force ordering: A, B, C via hints
    builder.GetPasses()[hA.index].orderHint = 0;
    builder.GetPasses()[hB.index].orderHint = 1;
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    auto& cg = *result;

    EXPECT_EQ(cg.passes.size(), 3u);
    VerifyTopologicalOrder(cg);

    // Check for split barrier pattern on RT:
    // PassA should have a release barrier for RT
    int posA = FindCompiledPass(cg, hA.index);
    ASSERT_GE(posA, 0);

    bool hasRelease = false;
    for (auto& b : cg.passes[posA].releaseBarriers) {
        if (b.resourceIndex == rt.GetIndex()) {
            hasRelease = true;
            if (b.isSplitRelease) {
                // Great — split barrier detected. Verify there's a matching acquire.
                bool hasMatchingAcquire = false;
                for (size_t i = static_cast<size_t>(posA) + 1; i < cg.passes.size(); ++i) {
                    for (auto& acq : cg.passes[i].acquireBarriers) {
                        if (acq.resourceIndex == rt.GetIndex() && acq.isSplitAcquire) {
                            hasMatchingAcquire = true;
                        }
                    }
                }
                EXPECT_TRUE(hasMatchingAcquire) << "Split release without matching acquire for RT";
            }
        }
    }
    EXPECT_TRUE(hasRelease) << "PassA missing release barrier for RT";
}

TEST(DeepValidation, StructuralHashDeterminism) {
    // Same graph built twice must produce identical structural hashes
    auto buildGraph = [](RenderGraphBuilder& builder) {
        auto rt = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
        auto depth
            = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 256, .height = 256, .debugName = "Depth"});
        builder.AddGraphicsPass(
            "GBuf",
            [&](PassBuilder& pb) {
                pb.WriteColorAttachment(rt);
                pb.WriteDepthStencil(depth);
            },
            [](RenderPassContext&) {}
        );
        builder.AddGraphicsPass(
            "Light",
            [&](PassBuilder& pb) {
                pb.ReadTexture(rt);
                pb.ReadDepth(depth);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        builder.Build();
    };

    RenderGraphBuilder b1, b2;
    buildGraph(b1);
    buildGraph(b2);

    RenderGraphCompiler compiler;
    auto r1 = compiler.Compile(b1);
    auto r2 = compiler.Compile(b2);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());

    EXPECT_EQ(r1->hash.passCount, r2->hash.passCount);
    EXPECT_EQ(r1->hash.resourceCount, r2->hash.resourceCount);
    EXPECT_EQ(r1->hash.edgeHash, r2->hash.edgeHash);
    EXPECT_EQ(r1->hash.descHash, r2->hash.descHash);
    EXPECT_EQ(r1->hash, r2->hash);
}

TEST(DeepValidation, BufferAndTextureAliasingInDifferentHeapGroups) {
    // Textures go to RtDs heap, buffers go to Buffer heap — they must NOT share slots.
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 128, .height = 128, .debugName = "Tex"});
    auto buf = builder.CreateBuffer({.size = 4096, .debugName = "Buf"});

    builder.AddGraphicsPass(
        "WriteTex", [&](PassBuilder& pb) { tex = pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "WriteBuf",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            buf = pb.WriteBuffer(buf);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableTransientAliasing = true;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    auto& cg = *result;

    EXPECT_EQ(cg.passes.size(), 2u);

    auto slotTex = cg.aliasing.resourceToSlot[tex.GetIndex()];
    auto slotBuf = cg.aliasing.resourceToSlot[buf.GetIndex()];
    if (slotTex != AliasingLayout::kNotAliased && slotBuf != AliasingLayout::kNotAliased) {
        // Different heap groups -> must be different slots
        EXPECT_NE(slotTex, slotBuf);
        auto& sTex = cg.aliasing.slots[slotTex];
        auto& sBuf = cg.aliasing.slots[slotBuf];
        EXPECT_EQ(sTex.heapGroup, HeapGroupType::RtDs);
        EXPECT_EQ(sBuf.heapGroup, HeapGroupType::Buffer);
    }

    // Buffer alignment should be 256B, texture alignment should be 64KB
    for (auto& slot : cg.aliasing.slots) {
        if (slot.heapGroup == HeapGroupType::Buffer) {
            EXPECT_EQ(slot.alignment, kAlignmentBuffer);
        } else if (slot.heapGroup == HeapGroupType::RtDs) {
            EXPECT_EQ(slot.alignment, kAlignmentTexture);
        }
    }
}

TEST(DeepValidation, FivePassPipelineFullValidation) {
    // GBuffer -> Lighting -> Bloom -> ToneMap -> UI
    // Tests the full 10-stage pipeline with all features enabled.
    RenderGraphBuilder builder;
    auto gbuf = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "GBuf"});
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 1920, .height = 1080, .debugName = "Depth"});
    auto hdr
        = builder.CreateTexture({.format = Format::RGBA16_FLOAT, .width = 1920, .height = 1080, .debugName = "HDR"});
    auto bloom
        = builder.CreateTexture({.format = Format::RGBA16_FLOAT, .width = 960, .height = 540, .debugName = "Bloom"});
    auto ldr = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "LDR"});

    auto hGBuf = builder.AddGraphicsPass(
        "GBuffer",
        [&](PassBuilder& pb) {
            gbuf = pb.WriteColorAttachment(gbuf);
            depth = pb.WriteDepthStencil(depth);
        },
        [](RenderPassContext&) {}
    );
    auto hLight = builder.AddGraphicsPass(
        "Lighting",
        [&](PassBuilder& pb) {
            pb.ReadTexture(gbuf);
            pb.ReadDepth(depth);
            hdr = pb.WriteColorAttachment(hdr);
        },
        [](RenderPassContext&) {}
    );
    auto hBloom = builder.AddGraphicsPass(
        "Bloom",
        [&](PassBuilder& pb) {
            pb.ReadTexture(hdr);
            bloom = pb.WriteColorAttachment(bloom);
        },
        [](RenderPassContext&) {}
    );
    auto hTone = builder.AddGraphicsPass(
        "ToneMap",
        [&](PassBuilder& pb) {
            pb.ReadTexture(hdr);
            pb.ReadTexture(bloom);
            ldr = pb.WriteColorAttachment(ldr);
        },
        [](RenderPassContext&) {}
    );
    auto hUI = builder.AddGraphicsPass(
        "UI",
        [&](PassBuilder& pb) {
            pb.ReadTexture(ldr);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = true;
    opts.strategy = SchedulerStrategy::Balanced;
    opts.enableTransientAliasing = true;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    auto& cg = *result;

    // --- All 5 passes present ---
    EXPECT_EQ(cg.passes.size(), 5u);
    VerifyTopologicalOrder(cg);
    VerifyBarrierStructure(cg);

    // --- Strict ordering ---
    int pGBuf = FindCompiledPass(cg, hGBuf.index);
    int pLight = FindCompiledPass(cg, hLight.index);
    int pBloom = FindCompiledPass(cg, hBloom.index);
    int pTone = FindCompiledPass(cg, hTone.index);
    int pUI = FindCompiledPass(cg, hUI.index);
    EXPECT_LT(pGBuf, pLight);
    EXPECT_LT(pLight, pBloom);  // Bloom reads HDR which Lighting writes
    EXPECT_LT(pLight, pTone);   // ToneMap reads HDR
    EXPECT_LT(pBloom, pTone);   // ToneMap reads Bloom
    EXPECT_LT(pTone, pUI);      // UI reads LDR which ToneMap writes

    // --- Edge count: at least 5 RAW edges ---
    // GBuf->Light (gbuf), GBuf->Light (depth), Light->Bloom (hdr),
    // Light->ToneMap (hdr), Bloom->ToneMap (bloom), ToneMap->UI (ldr)
    EXPECT_GE(cg.edges.size(), 5u);

    // --- Aliasing opportunities ---
    // gbuf lifetime [0, 1], bloom lifetime [2, 3], they don't overlap -> could alias
    // (but sizes differ: 1920x1080 vs 960x540, may not share due to size mismatch)
    // Verify all aliasing slots have valid heap offsets
    uint64_t maxOffset = 0;
    for (auto& slot : cg.aliasing.slots) {
        EXPECT_GE(slot.heapOffset + slot.size,
                  slot.heapOffset);  // No overflow
        maxOffset = std::max(maxOffset, slot.heapOffset + slot.size);
    }
    // Total allocated should match heap group size
    for (size_t g = 0; g < kHeapGroupCount; ++g) {
        if (cg.aliasing.heapGroupSizes[g] > 0) {
            uint64_t maxInGroup = 0;
            for (auto& slot : cg.aliasing.slots) {
                if (slot.heapGroup == static_cast<HeapGroupType>(g)) {
                    maxInGroup = std::max(maxInGroup, slot.heapOffset + slot.size);
                }
            }
            EXPECT_EQ(cg.aliasing.heapGroupSizes[g], maxInGroup) << "Heap group " << g << " size mismatch";
        }
    }

    // --- Batch: all on graphics -> 1 batch ---
    EXPECT_EQ(cg.batches.size(), 1u);
    EXPECT_EQ(cg.batches[0].passIndices.size(), 5u);
    EXPECT_TRUE(cg.batches[0].signalTimeline);
    EXPECT_TRUE(cg.batches[0].waits.empty());

    // --- Hash sanity ---
    EXPECT_EQ(cg.hash.passCount, 5u);
    EXPECT_GT(cg.hash.edgeHash, 0u);
}

TEST(DeepValidation, DCERemovesUnreachablePasses) {
    // Pass A writes t1, Pass B reads t1 and writes t2 (no side effects, no consumer for t2)
    // Pass C writes t3 (side effects)
    // B should be culled (t2 is unused), A should be culled (only consumer B is culled)
    RenderGraphBuilder builder;
    auto t1 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "t1"});
    auto t2 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "t2"});
    auto t3 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "t3"});

    builder.AddGraphicsPass("A", [&](PassBuilder& pb) { t1 = pb.WriteColorAttachment(t1); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t1);
            t2 = pb.WriteColorAttachment(t2);
        },
        [](RenderPassContext&) {}
    );
    auto hC = builder.AddGraphicsPass(
        "C",
        [&](PassBuilder& pb) {
            t3 = pb.WriteColorAttachment(t3);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    auto& cg = *result;

    // Only C should survive (A and B are dead code)
    EXPECT_EQ(cg.passes.size(), 1u);
    EXPECT_EQ(cg.passes[0].passIndex, hC.index);

    // No edges (only one pass)
    // Batches: 1 batch with 1 pass
    EXPECT_EQ(cg.batches.size(), 1u);
    EXPECT_EQ(cg.batches[0].passIndices.size(), 1u);
}

// =============================================================================
// ExecutorConfig unit tests
// =============================================================================

TEST(ExecutorConfig, DefaultValues) {
    ExecutorConfig cfg;
    EXPECT_EQ(cfg.maxRecordingThreads, 1u);
    EXPECT_FALSE(cfg.enableParallelRecording);
    EXPECT_FALSE(cfg.enableAsyncExecution);
    EXPECT_EQ(cfg.frameAllocatorCapacity, 256u * 1024u);
}

TEST(ExecutorConfig, CustomValues) {
    ExecutorConfig cfg{
        .maxRecordingThreads = 8,
        .enableParallelRecording = true,
        .enableAsyncExecution = true,
        .frameAllocatorCapacity = 1024 * 1024,
    };
    EXPECT_EQ(cfg.maxRecordingThreads, 8u);
    EXPECT_TRUE(cfg.enableParallelRecording);
    EXPECT_TRUE(cfg.enableAsyncExecution);
    EXPECT_EQ(cfg.frameAllocatorCapacity, 1024u * 1024u);
}

// =============================================================================
// ExecutionStats defaults
// =============================================================================

TEST(ExecutionStats, DefaultAllZero) {
    ExecutionStats stats{};
    EXPECT_EQ(stats.transientTexturesAllocated, 0u);
    EXPECT_EQ(stats.transientBuffersAllocated, 0u);
    EXPECT_EQ(stats.transientTextureViewsCreated, 0u);
    EXPECT_EQ(stats.heapsCreated, 0u);
    EXPECT_EQ(stats.barriersEmitted, 0u);
    EXPECT_EQ(stats.batchesSubmitted, 0u);
    EXPECT_EQ(stats.passesRecorded, 0u);
    EXPECT_EQ(stats.secondaryCmdBufsUsed, 0u);
    EXPECT_EQ(stats.transientMemoryBytes, 0u);
}

// =============================================================================
// Queue mapping -- ToRhiQueueType
// =============================================================================

TEST(QueueMapping, GraphicsToRhi) {
    EXPECT_EQ(ToRhiQueueType(RGQueueType::Graphics), QueueType::Graphics);
}

TEST(QueueMapping, AsyncComputeToRhi) {
    EXPECT_EQ(ToRhiQueueType(RGQueueType::AsyncCompute), QueueType::Compute);
}

TEST(QueueMapping, TransferToRhi) {
    EXPECT_EQ(ToRhiQueueType(RGQueueType::Transfer), QueueType::Transfer);
}

// =============================================================================
// RGResourceHandle -- edge cases
// =============================================================================

TEST(RGResourceHandle, MaxIndex) {
    auto h = RGResourceHandle::Create(0xFFFF, 0);
    EXPECT_TRUE(h.IsValid());
    EXPECT_EQ(h.GetIndex(), 0xFFFF);
    EXPECT_EQ(h.GetVersion(), 0);
}

TEST(RGResourceHandle, MaxVersion) {
    auto h = RGResourceHandle::Create(0, 0xFFFF);
    EXPECT_TRUE(h.IsValid());
    EXPECT_EQ(h.GetIndex(), 0);
    EXPECT_EQ(h.GetVersion(), 0xFFFF);
}

TEST(RGResourceHandle, MaxIndexAndVersion) {
    auto h = RGResourceHandle::Create(0xFFFF, 0xFFFF);
    // packed = 0xFFFFFFFF == kInvalid, so IsValid() returns false
    EXPECT_FALSE(h.IsValid());
    EXPECT_EQ(h.GetIndex(), 0xFFFF);
    EXPECT_EQ(h.GetVersion(), 0xFFFF);
}

TEST(RGResourceHandle, ZeroIndexIsValid) {
    auto h = RGResourceHandle::Create(0, 0);
    EXPECT_TRUE(h.IsValid());
    EXPECT_EQ(h.GetIndex(), 0);
    EXPECT_EQ(h.GetVersion(), 0);
}

TEST(RGResourceHandle, NextVersionOverflow) {
    auto h = RGResourceHandle::Create(5, 0xFFFE);
    auto h2 = h.NextVersion();
    EXPECT_EQ(h2.GetIndex(), 5);
    EXPECT_EQ(h2.GetVersion(), 0xFFFF);
}

TEST(RGResourceHandle, PackedBitLayout) {
    auto h = RGResourceHandle::Create(0x1234, 0xABCD);
    EXPECT_EQ(h.packed, 0xABCD1234u);
}

// =============================================================================
// RGPassHandle -- edge cases
// =============================================================================

TEST(RGPassHandle, ZeroIsValid) {
    RGPassHandle h{0};
    EXPECT_TRUE(h.IsValid());
    EXPECT_EQ(h.index, 0u);
}

TEST(RGPassHandle, LargeIndex) {
    RGPassHandle h{99999};
    EXPECT_TRUE(h.IsValid());
    EXPECT_EQ(h.index, 99999u);
}

TEST(RGPassHandle, Equality) {
    RGPassHandle a{5}, b{5}, c{6};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// =============================================================================
// Barrier mapping -- exhaustive single-access resolution
// =============================================================================

TEST(BarrierMapping, ResolveTransferSrc) {
    auto b = ResolveBarrier(ResourceAccess::TransferSrc);
    EXPECT_EQ(b.stage, PipelineStage::Transfer);
    EXPECT_EQ(b.access, AccessFlags::TransferRead);
    EXPECT_EQ(b.layout, TextureLayout::TransferSrc);
}

TEST(BarrierMapping, ResolveTransferDst) {
    auto b = ResolveBarrier(ResourceAccess::TransferDst);
    EXPECT_EQ(b.stage, PipelineStage::Transfer);
    EXPECT_EQ(b.access, AccessFlags::TransferWrite);
    EXPECT_EQ(b.layout, TextureLayout::TransferDst);
}

TEST(BarrierMapping, ResolveShaderWrite) {
    auto b = ResolveBarrier(ResourceAccess::ShaderWrite);
    EXPECT_EQ(b.layout, TextureLayout::General);
    EXPECT_NE(static_cast<uint32_t>(b.access), 0u);
}

TEST(BarrierMapping, ResolveIndirectBuffer) {
    auto b = ResolveBarrier(ResourceAccess::IndirectBuffer);
    EXPECT_EQ(b.stage, PipelineStage::DrawIndirect);
    EXPECT_EQ(b.access, AccessFlags::IndirectCommandRead);
    EXPECT_EQ(b.layout, TextureLayout::Undefined);
}

TEST(BarrierMapping, ResolveInputAttachment) {
    auto b = ResolveBarrier(ResourceAccess::InputAttachment);
    EXPECT_EQ(b.stage, PipelineStage::FragmentShader);
    EXPECT_EQ(b.access, AccessFlags::InputAttachmentRead);
}

TEST(BarrierMapping, ResolveAccelStructRead) {
    auto b = ResolveBarrier(ResourceAccess::AccelStructRead);
    EXPECT_NE(static_cast<uint32_t>(b.stage), 0u);
    EXPECT_EQ(b.access, AccessFlags::AccelStructRead);
}

TEST(BarrierMapping, ResolveShadingRateRead) {
    auto b = ResolveBarrier(ResourceAccess::ShadingRateRead);
    EXPECT_EQ(b.stage, PipelineStage::ShadingRateImage);
    EXPECT_EQ(b.layout, TextureLayout::ShadingRate);
}

TEST(BarrierMapping, ResolveAccelStructWrite) {
    auto b = ResolveBarrier(ResourceAccess::AccelStructWrite);
    EXPECT_EQ(b.stage, PipelineStage::AccelStructBuild);
    EXPECT_EQ(b.access, AccessFlags::AccelStructWrite);
}

TEST(BarrierMapping, ResolveDepthReadOnlyExact) {
    auto b = ResolveBarrier(ResourceAccess::DepthReadOnly);
    EXPECT_EQ(b.layout, TextureLayout::DepthStencilReadOnly);
    EXPECT_EQ(b.access, AccessFlags::DepthStencilRead);
}

TEST(BarrierMapping, ResolveNoneReturnsEmpty) {
    auto b = ResolveBarrier(ResourceAccess::None);
    EXPECT_EQ(static_cast<uint32_t>(b.stage), 0u);
    EXPECT_EQ(static_cast<uint32_t>(b.access), 0u);
}

// =============================================================================
// Resource estimation -- additional format/dimension tests
// =============================================================================

TEST(ResourceEstimation, TextureSize_R8_UNORM) {
    RGTextureDesc desc{.format = Format::R8_UNORM, .width = 512, .height = 512};
    EXPECT_EQ(EstimateTextureSize(desc), 512u * 512u * 1u);
}

TEST(ResourceEstimation, TextureSize_RGBA16_FLOAT) {
    RGTextureDesc desc{.format = Format::RGBA16_FLOAT, .width = 128, .height = 128};
    EXPECT_EQ(EstimateTextureSize(desc), 128u * 128u * 8u);
}

TEST(ResourceEstimation, TextureSize_D32_FLOAT) {
    RGTextureDesc desc{.format = Format::D32_FLOAT, .width = 1920, .height = 1080};
    EXPECT_EQ(EstimateTextureSize(desc), 1920u * 1080u * 4u);
}

TEST(ResourceEstimation, TextureSize_1x1) {
    RGTextureDesc desc{.format = Format::RGBA8_UNORM, .width = 1, .height = 1};
    EXPECT_EQ(EstimateTextureSize(desc), 4u);
}

TEST(ResourceEstimation, BufferSizeOne) {
    RGBufferDesc desc{.size = 1};
    EXPECT_EQ(EstimateBufferSize(desc), 1u);
}

// =============================================================================
// RenderGraphBuilder -- resource limit stress
// =============================================================================

TEST(RenderGraphBuilder, Create100Textures) {
    RenderGraphBuilder builder;
    for (int i = 0; i < 100; ++i) {
        auto h = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
        EXPECT_TRUE(h.IsValid());
        EXPECT_EQ(h.GetIndex(), static_cast<uint16_t>(i));
    }
    EXPECT_EQ(builder.GetResourceCount(), 100u);
}

TEST(RenderGraphBuilder, Create100Buffers) {
    RenderGraphBuilder builder;
    for (int i = 0; i < 100; ++i) {
        auto h = builder.CreateBuffer({.size = 256, .debugName = "B"});
        EXPECT_TRUE(h.IsValid());
    }
    EXPECT_EQ(builder.GetResourceCount(), 100u);
}

TEST(RenderGraphBuilder, MixedResourceTypes) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.debugName = "T"});
    auto b = builder.CreateBuffer({.size = 64, .debugName = "B"});
    auto i = builder.ImportTexture(TextureHandle{7}, "Imp");
    auto j = builder.ImportBuffer(BufferHandle{8}, "ImpBuf");
    EXPECT_EQ(builder.GetResourceCount(), 4u);
    EXPECT_NE(t.GetIndex(), b.GetIndex());
    EXPECT_NE(b.GetIndex(), i.GetIndex());
    EXPECT_NE(i.GetIndex(), j.GetIndex());
}

// =============================================================================
// Complex DAG topologies
// =============================================================================

TEST(ComplexDAG, WideParallelFanOut) {
    RenderGraphBuilder builder;
    auto src = builder.CreateTexture({.width = 256, .height = 256, .debugName = "Src"});
    builder.AddGraphicsPass(
        "Produce", [&](PassBuilder& pb) { src = pb.WriteColorAttachment(src); }, [](RenderPassContext&) {}
    );
    for (int i = 0; i < 4; ++i) {
        std::string name = "Consumer" + std::to_string(i);
        builder.AddGraphicsPass(
            name.c_str(),
            [&](PassBuilder& pb) {
                pb.ReadTexture(src);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
    }
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 5u);
    VerifyTopologicalOrder(*result);
}

TEST(ComplexDAG, FanInToSingleConsumer) {
    RenderGraphBuilder builder;
    std::array<RGResourceHandle, 4> textures;
    for (int i = 0; i < 4; ++i) {
        textures[i] = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    }
    for (int i = 0; i < 4; ++i) {
        std::string name = "Prod" + std::to_string(i);
        builder.AddGraphicsPass(
            name.c_str(), [&, i](PassBuilder& pb) { textures[i] = pb.WriteColorAttachment(textures[i]); },
            [](RenderPassContext&) {}
        );
    }
    builder.AddGraphicsPass(
        "Merge",
        [&](PassBuilder& pb) {
            for (auto& t : textures) {
                pb.ReadTexture(t);
            }
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 5u);
    VerifyTopologicalOrder(*result);
}

TEST(ComplexDAG, LinearChain10Passes) {
    RenderGraphBuilder builder;
    RGResourceHandle cur = builder.CreateTexture({.width = 128, .height = 128, .debugName = "Chain"});
    for (int i = 0; i < 10; ++i) {
        std::string name = "P" + std::to_string(i);
        bool first = (i == 0);
        bool last = (i == 9);
        builder.AddGraphicsPass(
            name.c_str(),
            [&cur, first, last](PassBuilder& pb) {
                if (!first) {
                    pb.ReadTexture(cur);
                }
                cur = pb.WriteColorAttachment(cur);
                if (last) {
                    pb.SetSideEffects();
                }
            },
            [](RenderPassContext&) {}
        );
    }
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 10u);
    VerifyTopologicalOrder(*result);
    EXPECT_EQ(result->batches.size(), 1u);
    EXPECT_EQ(result->batches[0].passIndices.size(), 10u);
}

TEST(ComplexDAG, TwoIndependentChains) {
    RenderGraphBuilder builder;
    RGResourceHandle tA = builder.CreateTexture({.width = 64, .height = 64, .debugName = "ChainA"});
    RGResourceHandle tB = builder.CreateTexture({.width = 64, .height = 64, .debugName = "ChainB"});
    for (int i = 0; i < 3; ++i) {
        std::string name = "A" + std::to_string(i);
        bool first = (i == 0);
        bool last = (i == 2);
        builder.AddGraphicsPass(
            name.c_str(),
            [&tA, first, last](PassBuilder& pb) {
                if (!first) {
                    pb.ReadTexture(tA);
                }
                tA = pb.WriteColorAttachment(tA);
                if (last) {
                    pb.SetSideEffects();
                }
            },
            [](RenderPassContext&) {}
        );
    }
    for (int i = 0; i < 3; ++i) {
        std::string name = "B" + std::to_string(i);
        bool first = (i == 0);
        bool last = (i == 2);
        builder.AddGraphicsPass(
            name.c_str(),
            [&tB, first, last](PassBuilder& pb) {
                if (!first) {
                    pb.ReadTexture(tB);
                }
                tB = pb.WriteColorAttachment(tB);
                if (last) {
                    pb.SetSideEffects();
                }
            },
            [](RenderPassContext&) {}
        );
    }
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 6u);
    VerifyTopologicalOrder(*result);
}

TEST(ComplexDAG, DiamondWithMerge) {
    // A -> B, A -> C, B -> D, C -> D (classic diamond)
    RenderGraphBuilder builder;
    auto tAB = builder.CreateTexture({.width = 128, .height = 128, .debugName = "AB"});
    auto tAC = builder.CreateTexture({.width = 128, .height = 128, .debugName = "AC"});
    auto hA = builder.AddGraphicsPass(
        "A",
        [&](PassBuilder& pb) {
            tAB = pb.WriteColorAttachment(tAB);
            tAC = pb.WriteColorAttachment(tAC, 1);
        },
        [](RenderPassContext&) {}
    );
    auto hB = builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tAB);
            tAB = pb.WriteColorAttachment(tAB);
        },
        [](RenderPassContext&) {}
    );
    auto hC = builder.AddGraphicsPass(
        "C",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tAC);
            tAC = pb.WriteColorAttachment(tAC);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "D",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tAB);
            pb.ReadTexture(tAC);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 4u);
    VerifyTopologicalOrder(*result);
    int posA = FindCompiledPass(*result, hA.index);
    int posB = FindCompiledPass(*result, hB.index);
    int posC = FindCompiledPass(*result, hC.index);
    EXPECT_LT(posA, posB);
    EXPECT_LT(posA, posC);
}

// =============================================================================
// Compiler option combinations
// =============================================================================

TEST(CompilerOptions, AllDisabled) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = false;
    opts.enableTransientAliasing = false;
    opts.enableRenderPassMerging = false;
    opts.enableAdaptation = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 1u);
    EXPECT_EQ(result->batches.size(), 1u);
}

TEST(CompilerOptions, AllEnabled) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 128, .height = 128, .debugName = "RT"});
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = true;
    opts.strategy = SchedulerStrategy::Balanced;
    opts.enableTransientAliasing = true;
    opts.enableRenderPassMerging = true;
    opts.enableAdaptation = true;
    opts.backendType = BackendType::Vulkan14;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 1u);
}

TEST(CompilerOptions, ReorderOnlyMinBarriers) {
    RenderGraphBuilder builder;
    auto t1 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T1"});
    auto t2 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T2"});
    builder.AddGraphicsPass(
        "W1", [&](PassBuilder& pb) { t1 = pb.WriteColorAttachment(t1); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "W2", [&](PassBuilder& pb) { t2 = pb.WriteColorAttachment(t2); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t1);
            pb.ReadTexture(t2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = true;
    opts.strategy = SchedulerStrategy::MinBarriers;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 3u);
    VerifyTopologicalOrder(*result);
}

TEST(CompilerOptions, AliasingOnlyNoMerging) {
    RenderGraphBuilder builder;
    auto t1 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "T1"});
    auto t2 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "T2"});
    builder.AddGraphicsPass(
        "W1", [&](PassBuilder& pb) { t1 = pb.WriteColorAttachment(t1); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R1W2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t1);
            t2 = pb.WriteColorAttachment(t2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableTransientAliasing = true;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
    EXPECT_FALSE(result->lifetimes.empty());
}

// =============================================================================
// Access validation -- additional pass type combinations
// =============================================================================

TEST(AccessValidation, ComputePassAllowsShaderReadWrite) {
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ShaderReadOnly, RGPassFlags::Compute));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ShaderWrite, RGPassFlags::Compute));
}

TEST(AccessValidation, ComputePassForbidsColorAttach) {
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::ColorAttachWrite, RGPassFlags::Compute));
}

TEST(AccessValidation, ComputePassForbidsDepthWrite) {
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::DepthStencilWrite, RGPassFlags::Compute));
}

TEST(AccessValidation, GraphicsPassAllowsEverything) {
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ShaderReadOnly, RGPassFlags::Graphics));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ColorAttachWrite, RGPassFlags::Graphics));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::DepthStencilWrite, RGPassFlags::Graphics));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::TransferSrc, RGPassFlags::Graphics));
}

TEST(AccessValidation, TransferPassAllowsOnlyTransfer) {
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::TransferSrc, RGPassFlags::Transfer));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::TransferDst, RGPassFlags::Transfer));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::ShaderReadOnly, RGPassFlags::Transfer));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::ColorAttachWrite, RGPassFlags::Transfer));
}

TEST(AccessValidation, PresentPassOnlyPresentSrc) {
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::PresentSrc, RGPassFlags::Present));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::ShaderReadOnly, RGPassFlags::Present));
    EXPECT_FALSE(IsAccessValidForPassType(ResourceAccess::ColorAttachWrite, RGPassFlags::Present));
}

TEST(AccessValidation, NoneAccessAlwaysValidAllTypes) {
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::None, RGPassFlags::Graphics));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::None, RGPassFlags::Compute));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::None, RGPassFlags::Transfer));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::None, RGPassFlags::Present));
}

// =============================================================================
// IsReadAccess / IsWriteAccess helper tests
// =============================================================================

TEST(ResourceAccessHelpers, ReadMaskIsRead) {
    EXPECT_TRUE(IsReadAccess(ResourceAccess::ShaderReadOnly));
    EXPECT_TRUE(IsReadAccess(ResourceAccess::DepthReadOnly));
    EXPECT_TRUE(IsReadAccess(ResourceAccess::TransferSrc));
    EXPECT_TRUE(IsReadAccess(ResourceAccess::InputAttachment));
    EXPECT_TRUE(IsReadAccess(ResourceAccess::PresentSrc));
}

TEST(ResourceAccessHelpers, WriteMaskIsWrite) {
    EXPECT_TRUE(IsWriteAccess(ResourceAccess::ShaderWrite));
    EXPECT_TRUE(IsWriteAccess(ResourceAccess::ColorAttachWrite));
    EXPECT_TRUE(IsWriteAccess(ResourceAccess::DepthStencilWrite));
    EXPECT_TRUE(IsWriteAccess(ResourceAccess::TransferDst));
}

TEST(ResourceAccessHelpers, ReadIsNotWrite) {
    EXPECT_FALSE(IsWriteAccess(ResourceAccess::ShaderReadOnly));
    EXPECT_FALSE(IsWriteAccess(ResourceAccess::DepthReadOnly));
    EXPECT_FALSE(IsWriteAccess(ResourceAccess::TransferSrc));
}

TEST(ResourceAccessHelpers, WriteIsNotRead) {
    EXPECT_FALSE(IsReadAccess(ResourceAccess::ShaderWrite));
    EXPECT_FALSE(IsReadAccess(ResourceAccess::ColorAttachWrite));
    EXPECT_FALSE(IsReadAccess(ResourceAccess::DepthStencilWrite));
}

TEST(ResourceAccessHelpers, NoneIsNeither) {
    EXPECT_FALSE(IsReadAccess(ResourceAccess::None));
    EXPECT_FALSE(IsWriteAccess(ResourceAccess::None));
}

// =============================================================================
// Structural hash -- additional scenarios
// =============================================================================

TEST(StructuralHash, DifferentGraphsProduceDifferentHashes) {
    RenderGraphBuilder b1;
    auto t1 = b1.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
    b1.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(t1);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    b1.Build();

    RenderGraphBuilder b2;
    auto t2a = b2.CreateTexture({.width = 256, .height = 256, .debugName = "RT1"});
    auto t2b = b2.CreateTexture({.width = 256, .height = 256, .debugName = "RT2"});
    b2.AddGraphicsPass(
        "Draw1", [&](PassBuilder& pb) { t2a = pb.WriteColorAttachment(t2a); }, [](RenderPassContext&) {}
    );
    b2.AddGraphicsPass(
        "Draw2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t2a);
            pb.WriteColorAttachment(t2b);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    b2.Build();

    RenderGraphCompiler compiler;
    auto r1 = compiler.Compile(b1);
    auto r2 = compiler.Compile(b2);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_NE(r1->hash.passCount, r2->hash.passCount);
}

TEST(StructuralHash, PassCountMatchesCompiled) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    builder.AddGraphicsPass(
        "A", [&](PassBuilder& pb) { tex = pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->hash.passCount, static_cast<uint32_t>(result->passes.size()));
}

// =============================================================================
// Deep validation -- additional integration scenarios
// =============================================================================

TEST(DeepValidation, AllPassesSideEffectsNoCulling) {
    // 5 independent passes, each writing its own texture + SetSideEffects.
    // None should be culled. This must NOT crash the compiler.
    RenderGraphBuilder builder;
    auto t0 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T0"});
    auto t1 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T1"});
    auto t2 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T2"});
    auto t3 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T3"});
    auto t4 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T4"});
    builder.AddGraphicsPass(
        "P0",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(t0);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "P1",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(t1);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "P2",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(t2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "P3",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(t3);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "P4",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(t4);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 5u);
}

TEST(DeepValidation, TransferPassCompiles) {
    RenderGraphBuilder builder;
    auto src = builder.CreateTexture({.width = 128, .height = 128, .debugName = "Src"});
    auto dst = builder.CreateTexture({.width = 128, .height = 128, .debugName = "Dst"});
    builder.AddGraphicsPass(
        "Produce", [&](PassBuilder& pb) { src = pb.WriteColorAttachment(src); }, [](RenderPassContext&) {}
    );
    builder.AddTransferPass(
        "Copy",
        [&](PassBuilder& pb) {
            pb.ReadTexture(src, ResourceAccess::TransferSrc);
            pb.WriteTexture(dst, ResourceAccess::TransferDst);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
    VerifyTopologicalOrder(*result);
    VerifyBarrierStructure(*result);
}

TEST(DeepValidation, ComputePassCompiles) {
    RenderGraphBuilder builder;
    auto buf = builder.CreateBuffer({.size = 1024, .debugName = "Buf"});
    builder.AddComputePass(
        "Dispatch",
        [&](PassBuilder& pb) {
            buf = pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 1u);
    EXPECT_EQ(result->passes[0].queue, RGQueueType::Graphics);
}

TEST(DeepValidation, AsyncComputeQueueAssignment) {
    RenderGraphBuilder builder;
    auto buf = builder.CreateBuffer({.size = 512, .debugName = "Buf"});
    builder.AddAsyncComputePass(
        "AsyncWork",
        [&](PassBuilder& pb) {
            buf = pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 1u);
    EXPECT_EQ(result->passes[0].queue, RGQueueType::AsyncCompute);
}

TEST(DeepValidation, LifetimesMonotonicallyOrdered) {
    RenderGraphBuilder builder;
    auto t1 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T1"});
    auto t2 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T2"});
    builder.AddGraphicsPass(
        "W1", [&](PassBuilder& pb) { t1 = pb.WriteColorAttachment(t1); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R1W2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t1);
            t2 = pb.WriteColorAttachment(t2);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableTransientAliasing = true;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    for (auto& lt : result->lifetimes) {
        EXPECT_LE(lt.firstPass, lt.lastPass) << "Resource " << lt.resourceIndex << " lifetime inverted";
    }
}

TEST(DeepValidation, BatchPassIndicesInRange) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
    builder.AddGraphicsPass(
        "Draw", [&](PassBuilder& pb) { tex = pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Post",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    for (auto& batch : result->batches) {
        for (auto idx : batch.passIndices) {
            EXPECT_LT(idx, static_cast<uint32_t>(result->passes.size()))
                << "Batch contains out-of-range pass index " << idx;
        }
    }
}

TEST(DeepValidation, EdgesReferenceValidPasses) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    builder.AddGraphicsPass(
        "W", [&](PassBuilder& pb) { tex = pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    std::unordered_set<uint32_t> passIndices;
    for (auto& p : result->passes) {
        passIndices.insert(p.passIndex);
    }
    for (auto& e : result->edges) {
        EXPECT_TRUE(passIndices.count(e.srcPass)) << "Edge src " << e.srcPass << " not in compiled passes";
        EXPECT_TRUE(passIndices.count(e.dstPass)) << "Edge dst " << e.dstPass << " not in compiled passes";
    }
}
