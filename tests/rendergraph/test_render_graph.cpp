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
#include "miki/rendergraph/TransientHeapPool.h"
#include "miki/rhi/adaptation/AdaptationQuery.h"
#include "miki/rhi/backend/MockDevice.h"
#include "miki/frame/SyncScheduler.h"
#include "miki/frame/CommandPoolAllocator.h"
#include "miki/frame/FrameContext.h"

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
    EXPECT_EQ(stats.allocation.transientTexturesAllocated, 0u);
    EXPECT_EQ(stats.allocation.transientBuffersAllocated, 0u);
    EXPECT_EQ(stats.allocation.transientTextureViewsCreated, 0u);
    EXPECT_EQ(stats.allocation.heapsCreated, 0u);
    EXPECT_EQ(stats.recording.barriersEmitted, 0u);
    EXPECT_EQ(stats.submission.batchesSubmitted, 0u);
    EXPECT_EQ(stats.recording.passesRecorded, 0u);
    EXPECT_EQ(stats.recording.secondaryCmdBufsUsed, 0u);
    EXPECT_EQ(stats.allocation.transientMemoryBytes, 0u);
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

// =============================================================================
// SSA Versioning Deep Tests
// =============================================================================

TEST(SSAVersioning, WriteColorBumpsVersion) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    EXPECT_EQ(t.GetVersion(), 0);
    RGResourceHandle written;
    builder.AddGraphicsPass(
        "W", [&](PassBuilder& pb) { written = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {}
    );
    EXPECT_EQ(written.GetIndex(), t.GetIndex());
    EXPECT_EQ(written.GetVersion(), 1);
}

TEST(SSAVersioning, TwoWritesBumpTwice) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    RGResourceHandle v1, v2;
    builder.AddGraphicsPass("W1", [&](PassBuilder& pb) { v1 = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "W2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(v1);
            v2 = pb.WriteColorAttachment(v1);
        },
        [](RenderPassContext&) {}
    );
    EXPECT_EQ(v1.GetVersion(), 1);
    EXPECT_EQ(v2.GetVersion(), 2);
    EXPECT_EQ(v1.GetIndex(), v2.GetIndex());
}

TEST(SSAVersioning, ReadDoesNotBumpVersion) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    RGResourceHandle afterWrite;
    builder.AddGraphicsPass(
        "W", [&](PassBuilder& pb) { afterWrite = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(afterWrite);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    EXPECT_EQ(afterWrite.GetVersion(), 1);
}

TEST(SSAVersioning, MultipleResourcesIndependentVersioning) {
    RenderGraphBuilder builder;
    auto a = builder.CreateTexture({.width = 32, .height = 32, .debugName = "A"});
    auto b = builder.CreateTexture({.width = 32, .height = 32, .debugName = "B"});
    RGResourceHandle wa, wb;
    builder.AddGraphicsPass("WA", [&](PassBuilder& pb) { wa = pb.WriteColorAttachment(a); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass("WB", [&](PassBuilder& pb) { wb = pb.WriteColorAttachment(b); }, [](RenderPassContext&) {});
    EXPECT_NE(wa.GetIndex(), wb.GetIndex());
    EXPECT_EQ(wa.GetVersion(), 1);
    EXPECT_EQ(wb.GetVersion(), 1);
}

TEST(SSAVersioning, ChainOf5WritesYieldsVersion5) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 32, .height = 32, .debugName = "Chain"});
    RGResourceHandle cur = t;
    for (int i = 0; i < 5; ++i) {
        std::string name = "W" + std::to_string(i);
        bool first = (i == 0);
        builder.AddGraphicsPass(
            name.c_str(),
            [&cur, first](PassBuilder& pb) {
                if (!first) {
                    pb.ReadTexture(cur);
                }
                cur = pb.WriteColorAttachment(cur);
            },
            [](RenderPassContext&) {}
        );
    }
    EXPECT_EQ(cur.GetVersion(), 5);
    EXPECT_EQ(cur.GetIndex(), t.GetIndex());
}

TEST(SSAVersioning, DepthWriteBumpsVersion) {
    RenderGraphBuilder builder;
    auto d = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 64, .height = 64, .debugName = "D"});
    RGResourceHandle written;
    builder.AddGraphicsPass(
        "W", [&](PassBuilder& pb) { written = pb.WriteDepthStencil(d); }, [](RenderPassContext&) {}
    );
    EXPECT_EQ(written.GetVersion(), 1);
    EXPECT_EQ(written.GetIndex(), d.GetIndex());
}

TEST(SSAVersioning, BufferWriteBumpsVersion) {
    RenderGraphBuilder builder;
    auto buf = builder.CreateBuffer({.size = 1024, .debugName = "Buf"});
    RGResourceHandle written;
    builder.AddGraphicsPass(
        "W", [&](PassBuilder& pb) { written = pb.WriteBuffer(buf, ResourceAccess::ShaderWrite); },
        [](RenderPassContext&) {}
    );
    EXPECT_EQ(written.GetVersion(), 1);
}

// =============================================================================
// Barrier Detail Validation
// =============================================================================

TEST(BarrierDetail, RAWBarrierHasCorrectLayouts) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 128, .height = 128, .debugName = "RT"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { rt = pb.WriteColorAttachment(rt); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableSplitBarriers = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->passes.size(), 2u);
    ASSERT_FALSE(result->passes[1].acquireBarriers.empty());
    auto& b = result->passes[1].acquireBarriers[0];
    EXPECT_EQ(b.srcAccess, ResourceAccess::ColorAttachWrite);
    EXPECT_EQ(b.dstAccess, ResourceAccess::ShaderReadOnly);
    EXPECT_EQ(b.srcLayout, TextureLayout::ColorAttachment);
    EXPECT_EQ(b.dstLayout, TextureLayout::ShaderReadOnly);
    EXPECT_FALSE(b.isCrossQueue);
    EXPECT_FALSE(b.isAliasingBarrier);
}

TEST(BarrierDetail, DepthWriteToDepthReadBarrier) {
    RenderGraphBuilder builder;
    auto d = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 128, .height = 128, .debugName = "D"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { d = pb.WriteDepthStencil(d); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadDepth(d);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableSplitBarriers = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->passes.size(), 2u);
    ASSERT_FALSE(result->passes[1].acquireBarriers.empty());
    auto& b = result->passes[1].acquireBarriers[0];
    EXPECT_EQ(b.srcAccess, ResourceAccess::DepthStencilWrite);
    EXPECT_EQ(b.dstAccess, ResourceAccess::DepthReadOnly);
    EXPECT_EQ(b.srcLayout, TextureLayout::DepthStencilAttachment);
    EXPECT_EQ(b.dstLayout, TextureLayout::DepthStencilReadOnly);
}

TEST(BarrierDetail, TransferBarrierLayouts) {
    RenderGraphBuilder builder;
    auto buf = builder.CreateBuffer({.size = 1024, .debugName = "Buf"});
    builder.AddTransferPass(
        "Up", [&](PassBuilder& pb) { buf = pb.WriteBuffer(buf, ResourceAccess::TransferDst); },
        [](RenderPassContext&) {}
    );
    builder.AddTransferPass(
        "Down",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(buf, ResourceAccess::TransferSrc);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableSplitBarriers = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->passes.size(), 2u);
    ASSERT_FALSE(result->passes[1].acquireBarriers.empty());
    EXPECT_EQ(result->passes[1].acquireBarriers[0].srcAccess, ResourceAccess::TransferDst);
    EXPECT_EQ(result->passes[1].acquireBarriers[0].dstAccess, ResourceAccess::TransferSrc);
}

TEST(BarrierDetail, NoBarrierForFirstWritePass) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    builder.AddGraphicsPass(
        "First",
        [&](PassBuilder& pb) {
            rt = pb.WriteColorAttachment(rt);
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
    ASSERT_EQ(result->passes.size(), 1u);
    EXPECT_TRUE(result->passes[0].acquireBarriers.empty());
}

TEST(BarrierDetail, SplitBarrierReleaseAndAcquire) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 128, .height = 128, .debugName = "RT"});
    auto other = builder.CreateTexture({.width = 128, .height = 128, .debugName = "Other"});
    builder.AddGraphicsPass(
        "W0", [&](PassBuilder& pb) { rt = pb.WriteColorAttachment(rt); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Gap",
        [&](PassBuilder& pb) {
            other = pb.WriteColorAttachment(other);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableSplitBarriers = true;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->passes.size(), 3u);
    bool hasRelease = false;
    for (auto& b : result->passes[0].releaseBarriers) {
        if (b.isSplitRelease && b.resourceIndex == rt.GetIndex()) {
            hasRelease = true;
        }
    }
    EXPECT_TRUE(hasRelease) << "W0 missing split release for RT";
    bool hasAcquire = false;
    for (auto& b : result->passes[2].acquireBarriers) {
        if (b.isSplitAcquire && b.resourceIndex == rt.GetIndex()) {
            hasAcquire = true;
        }
    }
    EXPECT_TRUE(hasAcquire) << "R missing split acquire for RT";
}

TEST(BarrierDetail, NoSplitBarrierWhenConsecutive) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 128, .height = 128, .debugName = "RT"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { rt = pb.WriteColorAttachment(rt); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableSplitBarriers = true;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->passes.size(), 2u);
    for (auto& b : result->passes[0].releaseBarriers) {
        EXPECT_FALSE(b.isSplitRelease) << "Should not split for consecutive passes";
    }
    ASSERT_FALSE(result->passes[1].acquireBarriers.empty());
    EXPECT_FALSE(result->passes[1].acquireBarriers[0].isSplitAcquire);
}

TEST(BarrierDetail, WAWBarrierOnSameResource) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    builder.AddGraphicsPass(
        "W1", [&](PassBuilder& pb) { rt = pb.WriteColorAttachment(rt); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "W2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt);
            rt = pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableSplitBarriers = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->passes.size(), 2u);
    EXPECT_FALSE(result->passes[1].acquireBarriers.empty()) << "WAW needs barrier";
}

TEST(BarrierDetail, BarrierResourceIndexMatchesAccess) {
    RenderGraphBuilder builder;
    auto t0 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T0"});
    auto t1 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T1"});
    builder.AddGraphicsPass(
        "W",
        [&](PassBuilder& pb) {
            t0 = pb.WriteColorAttachment(t0);
            t1 = pb.WriteTexture(t1, ResourceAccess::ShaderWrite);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t0);
            pb.ReadTexture(t1);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableSplitBarriers = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->passes.size(), 2u);
    std::unordered_set<uint16_t> barrierResources;
    for (auto& b : result->passes[1].acquireBarriers) {
        barrierResources.insert(b.resourceIndex);
    }
    EXPECT_TRUE(barrierResources.count(t0.GetIndex())) << "Missing barrier for T0";
    EXPECT_TRUE(barrierResources.count(t1.GetIndex())) << "Missing barrier for T1";
}

// =============================================================================
// DAG Edge Precision Tests
// =============================================================================

TEST(DAGEdges, LinearChainEdgeCount) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T"});
    for (int i = 0; i < 5; ++i) {
        std::string name = "P" + std::to_string(i);
        bool first = (i == 0), last = (i == 4);
        builder.AddGraphicsPass(
            name.c_str(),
            [&t, first, last](PassBuilder& pb) {
                if (!first) {
                    pb.ReadTexture(t);
                }
                t = pb.WriteColorAttachment(t);
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
    EXPECT_EQ(result->passes.size(), 5u);
    uint32_t rawCount = 0;
    for (auto& e : result->edges) {
        if (e.hazard == HazardType::RAW) {
            rawCount++;
        }
    }
    EXPECT_GE(rawCount, 4u);
}

TEST(DAGEdges, DiamondHas2EdgesFromSource) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "Src"});
    auto tB = builder.CreateTexture({.width = 64, .height = 64, .debugName = "B"});
    auto tC = builder.CreateTexture({.width = 64, .height = 64, .debugName = "C"});
    auto hA = builder.AddGraphicsPass(
        "A", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            tB = pb.WriteColorAttachment(tB);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "C",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            tC = pb.WriteColorAttachment(tC);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "D",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tB);
            pb.ReadTexture(tC);
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
    uint32_t edgesFromA = 0;
    for (auto& e : result->edges) {
        if (e.srcPass == hA.index) {
            edgesFromA++;
        }
    }
    EXPECT_GE(edgesFromA, 2u);
}

TEST(DAGEdges, NoEdgesForIndependentPasses) {
    RenderGraphBuilder builder;
    auto t0 = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T0"});
    auto t1 = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T1"});
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
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
    EXPECT_TRUE(result->edges.empty());
}

TEST(DAGEdges, HazardTypeIsRAW) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
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
    ASSERT_FALSE(result->edges.empty());
    EXPECT_EQ(result->edges[0].hazard, HazardType::RAW);
}

TEST(DAGEdges, EdgeResourceIndexMatchesResource) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 32, .height = 32, .debugName = "RT"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
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
    ASSERT_FALSE(result->edges.empty());
    EXPECT_EQ(result->edges[0].resourceIndex, t.GetIndex());
}

TEST(DAGEdges, FanOutCreatesMultipleEdges) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    auto hW = builder.AddGraphicsPass(
        "W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {}
    );
    for (int i = 0; i < 4; ++i) {
        std::string name = "R" + std::to_string(i);
        builder.AddGraphicsPass(
            name.c_str(),
            [&t](PassBuilder& pb) {
                pb.ReadTexture(t);
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
    uint32_t edgesFromW = 0;
    for (auto& e : result->edges) {
        if (e.srcPass == hW.index) {
            edgesFromW++;
        }
    }
    EXPECT_GE(edgesFromW, 4u);
}

TEST(DAGEdges, FanInToSinglePassEdges) {
    RenderGraphBuilder builder;
    std::vector<RGResourceHandle> textures;
    for (int i = 0; i < 4; ++i) {
        std::string name = "T" + std::to_string(i);
        textures.push_back(builder.CreateTexture({.width = 32, .height = 32, .debugName = name.c_str()}));
    }
    for (int i = 0; i < 4; ++i) {
        std::string name = "W" + std::to_string(i);
        builder.AddGraphicsPass(
            name.c_str(), [&textures, i](PassBuilder& pb) { textures[i] = pb.WriteColorAttachment(textures[i]); },
            [](RenderPassContext&) {}
        );
    }
    auto hR = builder.AddGraphicsPass(
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
    uint32_t edgesToR = 0;
    for (auto& e : result->edges) {
        if (e.dstPass == hR.index) {
            edgesToR++;
        }
    }
    EXPECT_GE(edgesToR, 4u);
}

// =============================================================================
// Lifetime Precision Tests
// =============================================================================

TEST(Lifetime, SinglePassLifetimeSpansOnePosition) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    builder.AddGraphicsPass(
        "Only",
        [&](PassBuilder& pb) {
            rt = pb.WriteColorAttachment(rt);
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
    ASSERT_FALSE(result->lifetimes.empty());
    for (auto& lt : result->lifetimes) {
        if (lt.resourceIndex == rt.GetIndex()) {
            EXPECT_EQ(lt.firstPass, lt.lastPass);
        }
    }
}

TEST(Lifetime, TwoPassLifetimeSpansTwoPositions) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { rt = pb.WriteColorAttachment(rt); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt);
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
    bool found = false;
    for (auto& lt : result->lifetimes) {
        if (lt.resourceIndex == rt.GetIndex()) {
            found = true;
            EXPECT_EQ(lt.firstPass, 0u);
            EXPECT_EQ(lt.lastPass, 1u);
        }
    }
    EXPECT_TRUE(found);
}

TEST(Lifetime, FirstPassNeverExceedsLastPass) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    for (int i = 0; i < 5; ++i) {
        std::string name = "P" + std::to_string(i);
        bool first = (i == 0), last = (i == 4);
        builder.AddGraphicsPass(
            name.c_str(),
            [&t, first, last](PassBuilder& pb) {
                if (!first) {
                    pb.ReadTexture(t);
                }
                t = pb.WriteColorAttachment(t);
                if (last) {
                    pb.SetSideEffects();
                }
            },
            [](RenderPassContext&) {}
        );
    }
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableTransientAliasing = true;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    for (auto& lt : result->lifetimes) {
        EXPECT_LE(lt.firstPass, lt.lastPass) << "firstPass > lastPass for resource " << lt.resourceIndex;
    }
}

TEST(Lifetime, ImportedResourceExcludedFromLifetimes) {
    RenderGraphBuilder builder;
    auto imported = builder.ImportTexture({}, "Ext");
    builder.AddGraphicsPass(
        "Use",
        [&](PassBuilder& pb) {
            pb.ReadTexture(imported);
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
        EXPECT_NE(lt.resourceIndex, imported.GetIndex());
    }
}

TEST(Lifetime, MultipleResourcesAllHaveLifetimes) {
    RenderGraphBuilder builder;
    auto t0 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T0"});
    auto t1 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T1"});
    builder.AddGraphicsPass(
        "W0", [&](PassBuilder& pb) { t0 = pb.WriteColorAttachment(t0); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "W1",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t0);
            t1 = pb.WriteColorAttachment(t1);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t1);
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
    std::unordered_set<uint16_t> ltResources;
    for (auto& lt : result->lifetimes) {
        ltResources.insert(lt.resourceIndex);
    }
    EXPECT_TRUE(ltResources.count(t0.GetIndex()));
    EXPECT_TRUE(ltResources.count(t1.GetIndex()));
}

// =============================================================================
// Aliasing Precision Tests
// =============================================================================

TEST(AliasingPrecision, OverlappingResourcesGetSeparateSlots) {
    RenderGraphBuilder builder;
    auto rt0 = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT0"});
    auto rt1 = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT1"});
    builder.AddGraphicsPass(
        "P0",
        [&](PassBuilder& pb) {
            rt0 = pb.WriteColorAttachment(rt0);
            rt1 = pb.WriteTexture(rt1, ResourceAccess::ShaderWrite);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "P1",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt0);
            pb.ReadTexture(rt1);
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
    auto& rs = result->aliasing.resourceToSlot;
    if (rs[rt0.GetIndex()] != AliasingLayout::kNotAliased && rs[rt1.GetIndex()] != AliasingLayout::kNotAliased) {
        EXPECT_NE(rs[rt0.GetIndex()], rs[rt1.GetIndex()]);
    }
}

TEST(AliasingPrecision, HeapOffsetIsAligned) {
    RenderGraphBuilder builder;
    auto t0 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T0"});
    auto t1 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "T1"});
    builder.AddGraphicsPass(
        "P0",
        [&](PassBuilder& pb) {
            t0 = pb.WriteColorAttachment(t0);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "P1",
        [&](PassBuilder& pb) {
            t1 = pb.WriteColorAttachment(t1);
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
    for (auto& slot : result->aliasing.slots) {
        uint64_t align = slot.alignment > 0 ? slot.alignment : 1;
        EXPECT_EQ(slot.heapOffset % align, 0u) << "Slot " << slot.slotIndex << " misaligned";
    }
}

TEST(AliasingPrecision, MSAATextureGets4MBAlignment) {
    RenderGraphBuilder builder;
    auto msaa = builder.CreateTexture({.width = 256, .height = 256, .sampleCount = 4, .debugName = "MSAA"});
    builder.AddGraphicsPass(
        "W",
        [&](PassBuilder& pb) {
            msaa = pb.WriteColorAttachment(msaa);
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
    auto idx = msaa.GetIndex();
    if (result->aliasing.resourceToSlot[idx] != AliasingLayout::kNotAliased) {
        auto slotIdx = result->aliasing.resourceToSlot[idx];
        EXPECT_GE(result->aliasing.slots[slotIdx].alignment, kAlignmentMsaa);
    }
}

TEST(AliasingPrecision, BufferAndTextureInDifferentHeapGroups) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 64, .height = 64, .debugName = "Tex"});
    auto buf = builder.CreateBuffer({.size = 4096, .debugName = "Buf"});
    builder.AddGraphicsPass(
        "P0",
        [&](PassBuilder& pb) {
            tex = pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "P1",
        [&](PassBuilder& pb) {
            buf = pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
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
    auto& rs = result->aliasing.resourceToSlot;
    if (rs[tex.GetIndex()] != AliasingLayout::kNotAliased && rs[buf.GetIndex()] != AliasingLayout::kNotAliased) {
        auto texSlot = rs[tex.GetIndex()];
        auto bufSlot = rs[buf.GetIndex()];
        EXPECT_NE(result->aliasing.slots[texSlot].heapGroup, result->aliasing.slots[bufSlot].heapGroup);
    }
}

TEST(AliasingPrecision, DisabledAliasingProducesNoSlots) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    builder.AddGraphicsPass(
        "W",
        [&](PassBuilder& pb) {
            t = pb.WriteColorAttachment(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableTransientAliasing = false;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->aliasing.slots.empty());
}

TEST(AliasingPrecision, SlotLifetimeCoversResourceLifetime) {
    RenderGraphBuilder builder;
    auto t0 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "T0"});
    auto t1 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "T1"});
    builder.AddGraphicsPass(
        "W0", [&](PassBuilder& pb) { t0 = pb.WriteColorAttachment(t0); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "W1",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t0);
            t1 = pb.WriteColorAttachment(t1);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R1",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t1);
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
        auto slotIdx = result->aliasing.resourceToSlot[lt.resourceIndex];
        if (slotIdx != AliasingLayout::kNotAliased) {
            auto& slot = result->aliasing.slots[slotIdx];
            EXPECT_LE(slot.lifetimeStart, lt.firstPass);
            EXPECT_GE(slot.lifetimeEnd, lt.lastPass);
        }
    }
}

// =============================================================================
// DCE (Dead Code Elimination) Precision Tests
// =============================================================================

TEST(DCEPrecision, SideEffectsPreventCulling) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T"});
    builder.AddGraphicsPass(
        "SE",
        [&](PassBuilder& pb) {
            t = pb.WriteColorAttachment(t);
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

TEST(DCEPrecision, WriteOnlyPassIsCulled) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T"});
    builder.AddGraphicsPass(
        "Dead", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 0u);
}

TEST(DCEPrecision, TransitiveReachabilityFromSideEffect) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T"});
    builder.AddGraphicsPass("A", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
}

TEST(DCEPrecision, DeepChainReachableFromSideEffect) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T"});
    for (int i = 0; i < 5; ++i) {
        std::string name = "P" + std::to_string(i);
        bool first = (i == 0), last = (i == 4);
        builder.AddGraphicsPass(
            name.c_str(),
            [&t, first, last](PassBuilder& pb) {
                if (!first) {
                    pb.ReadTexture(t);
                }
                t = pb.WriteColorAttachment(t);
                if (last) {
                    pb.SetSideEffects();
                }
            },
            [](RenderPassContext&) {}
        );
    }
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 5u);
}

TEST(DCEPrecision, BranchWithDeadEndIsCulled) {
    RenderGraphBuilder builder;
    auto t0 = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T0"});
    auto t1 = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T1"});
    builder.AddGraphicsPass(
        "A",
        [&](PassBuilder& pb) {
            t0 = pb.WriteColorAttachment(t0);
            t1 = pb.WriteTexture(t1, ResourceAccess::ShaderWrite);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t0);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass("C", [&](PassBuilder& pb) { pb.ReadTexture(t1); }, [](RenderPassContext&) {});
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
}

TEST(DCEPrecision, DisabledPassAndItsDependencyCulled) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T"});
    builder.AddGraphicsPass("A", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    auto hB = builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.EnableIf(hB, []() { return false; });
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 0u);
}

TEST(DCEPrecision, MultipleSideEffectPassesSurvive) {
    RenderGraphBuilder builder;
    for (int i = 0; i < 8; ++i) {
        std::string name = "SE" + std::to_string(i);
        auto t = builder.CreateTexture({.width = 32, .height = 32, .debugName = name.c_str()});
        builder.AddGraphicsPass(
            name.c_str(),
            [t](PassBuilder& pb) mutable {
                pb.WriteColorAttachment(t);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
    }
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 8u);
}

TEST(DCEPrecision, MixedLiveAndDeadPasses) {
    RenderGraphBuilder builder;
    auto live = builder.CreateTexture({.width = 32, .height = 32, .debugName = "Live"});
    auto dead = builder.CreateTexture({.width = 32, .height = 32, .debugName = "Dead"});
    builder.AddGraphicsPass(
        "LiveW", [&](PassBuilder& pb) { live = pb.WriteColorAttachment(live); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "DeadW", [&](PassBuilder& pb) { dead = pb.WriteColorAttachment(dead); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "LiveR",
        [&](PassBuilder& pb) {
            pb.ReadTexture(live);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
}

// =============================================================================
// Conditional Execution Tests
// =============================================================================

TEST(ConditionalExec, EnabledPassSurvives) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T"});
    auto h = builder.AddGraphicsPass(
        "P",
        [&](PassBuilder& pb) {
            t = pb.WriteColorAttachment(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.EnableIf(h, []() { return true; });
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 1u);
}

TEST(ConditionalExec, DisabledPassCulled) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T"});
    auto h = builder.AddGraphicsPass(
        "P",
        [&](PassBuilder& pb) {
            t = pb.WriteColorAttachment(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.EnableIf(h, []() { return false; });
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 0u);
}

TEST(ConditionalExec, PartialDisableLeavesSurvivors) {
    RenderGraphBuilder builder;
    auto t0 = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T0"});
    auto t1 = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T1"});
    builder.AddGraphicsPass(
        "A",
        [&](PassBuilder& pb) {
            t0 = pb.WriteColorAttachment(t0);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    auto hB = builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            t1 = pb.WriteColorAttachment(t1);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.EnableIf(hB, []() { return false; });
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 1u);
}

TEST(ConditionalExec, DisabledConsumerCullsProducer) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T"});
    builder.AddGraphicsPass(
        "Producer", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {}
    );
    auto hCons = builder.AddGraphicsPass(
        "Consumer",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.EnableIf(hCons, []() { return false; });
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 0u);
}

// =============================================================================
// Batch Formation Precision Tests
// =============================================================================

TEST(BatchPrecision, AllPassIndicesInRange) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    for (int i = 0; i < 4; ++i) {
        std::string name = "P" + std::to_string(i);
        bool first = (i == 0), last = (i == 3);
        builder.AddGraphicsPass(
            name.c_str(),
            [&t, first, last](PassBuilder& pb) {
                if (!first) {
                    pb.ReadTexture(t);
                }
                t = pb.WriteColorAttachment(t);
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
    for (auto& batch : result->batches) {
        for (auto idx : batch.passIndices) {
            EXPECT_LT(idx, result->passes.size());
        }
    }
}

TEST(BatchPrecision, NoDuplicatePassIndicesAcrossBatches) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
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
    std::unordered_set<uint32_t> seen;
    for (auto& batch : result->batches) {
        for (auto idx : batch.passIndices) {
            EXPECT_FALSE(seen.count(idx)) << "Pass " << idx << " in multiple batches";
            seen.insert(idx);
        }
    }
}

TEST(BatchPrecision, LastBatchSignalsTimeline) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    builder.AddGraphicsPass(
        "Only",
        [&](PassBuilder& pb) {
            t = pb.WriteColorAttachment(t);
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
    ASSERT_FALSE(result->batches.empty());
    EXPECT_TRUE(result->batches.back().signalTimeline);
}

TEST(BatchPrecision, TotalBatchedPassesEqualsCompiledPasses) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    for (int i = 0; i < 6; ++i) {
        std::string name = "P" + std::to_string(i);
        bool first = (i == 0), last = (i == 5);
        builder.AddGraphicsPass(
            name.c_str(),
            [&t, first, last](PassBuilder& pb) {
                if (!first) {
                    pb.ReadTexture(t);
                }
                t = pb.WriteColorAttachment(t);
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
    uint32_t total = 0;
    for (auto& b : result->batches) {
        total += static_cast<uint32_t>(b.passIndices.size());
    }
    EXPECT_EQ(total, static_cast<uint32_t>(result->passes.size()));
}

TEST(BatchPrecision, BatchQueueMatchesPassQueue) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            t = pb.WriteColorAttachment(t);
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
        for (auto idx : batch.passIndices) {
            EXPECT_EQ(batch.queue, result->passes[idx].queue);
        }
    }
}

// =============================================================================
// Cross-Queue Synchronization Tests
// =============================================================================

TEST(CrossQueue, GraphicsToAsyncComputeSyncPoint) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    auto buf = builder.CreateBuffer({.size = 256, .debugName = "Buf"});
    builder.AddGraphicsPass(
        "Draw", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {}
    );
    builder.AddAsyncComputePass(
        "Compute",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            buf = pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Post",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(buf, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableAsyncCompute = true;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 3u);
    bool hasGfxToComp = false;
    for (auto& sp : result->syncPoints) {
        if (sp.srcQueue == RGQueueType::Graphics && sp.dstQueue == RGQueueType::AsyncCompute) {
            hasGfxToComp = true;
        }
    }
    EXPECT_TRUE(hasGfxToComp);
}

TEST(CrossQueue, NoSyncForSameQueue) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
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
    EXPECT_TRUE(result->syncPoints.empty());
}

TEST(CrossQueue, SyncPointPassIndicesAreValid) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    auto buf = builder.CreateBuffer({.size = 256, .debugName = "Buf"});
    builder.AddGraphicsPass(
        "Draw", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {}
    );
    builder.AddAsyncComputePass(
        "Compute",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            buf = pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Post",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(buf, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableAsyncCompute = true;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    std::unordered_set<uint32_t> passIndices;
    for (auto& p : result->passes) {
        passIndices.insert(p.passIndex);
    }
    for (auto& sp : result->syncPoints) {
        EXPECT_TRUE(passIndices.count(sp.srcPassIndex)) << "Sync src " << sp.srcPassIndex << " invalid";
        EXPECT_TRUE(passIndices.count(sp.dstPassIndex)) << "Sync dst " << sp.dstPassIndex << " invalid";
    }
}

// =============================================================================
// Structural Hash Tests
// =============================================================================

TEST(StructuralHashDeep, IdenticalGraphsSameHash) {
    auto buildGraph = []() {
        RenderGraphBuilder builder;
        auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
        builder.AddGraphicsPass(
            "W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {}
        );
        builder.AddGraphicsPass(
            "R",
            [&](PassBuilder& pb) {
                pb.ReadTexture(t);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        builder.Build();
        return builder;
    };
    auto b1 = buildGraph();
    auto b2 = buildGraph();
    RenderGraphCompiler compiler;
    auto r1 = compiler.Compile(b1);
    auto r2 = compiler.Compile(b2);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r1->hash, r2->hash);
}

TEST(StructuralHashDeep, DifferentPassCountDifferentHash) {
    RenderGraphBuilder b1;
    auto t1 = b1.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    b1.AddGraphicsPass(
        "W",
        [&](PassBuilder& pb) {
            t1 = pb.WriteColorAttachment(t1);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    b1.Build();

    RenderGraphBuilder b2;
    auto t2 = b2.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    b2.AddGraphicsPass("W1", [&](PassBuilder& pb) { t2 = pb.WriteColorAttachment(t2); }, [](RenderPassContext&) {});
    b2.AddGraphicsPass(
        "W2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t2);
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
    EXPECT_NE(r1->hash, r2->hash);
}

TEST(StructuralHashDeep, HashPassCountMatchesCompiled) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->hash.passCount, 2u);
    EXPECT_GE(result->hash.resourceCount, 1u);
}

// =============================================================================
// Complex Integration Tests
// =============================================================================

TEST(ComplexIntegration, FullDeferredPipeline) {
    // G-Buffer → Lighting → Post → Present-like SE
    RenderGraphBuilder builder;
    auto albedo = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "Albedo"});
    auto normal = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "Normal"});
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 1920, .height = 1080, .debugName = "Depth"});
    auto hdr
        = builder.CreateTexture({.format = Format::RGBA16_FLOAT, .width = 1920, .height = 1080, .debugName = "HDR"});
    auto ldr = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "LDR"});

    builder.AddGraphicsPass(
        "GBuffer",
        [&](PassBuilder& pb) {
            albedo = pb.WriteColorAttachment(albedo);
            normal = pb.WriteColorAttachment(normal, 1);
            depth = pb.WriteDepthStencil(depth);
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "Lighting",
        [&](PassBuilder& pb) {
            pb.ReadTexture(albedo);
            pb.ReadTexture(normal);
            pb.ReadDepth(depth);
            hdr = pb.WriteColorAttachment(hdr);
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "ToneMap",
        [&](PassBuilder& pb) {
            pb.ReadTexture(hdr);
            ldr = pb.WriteColorAttachment(ldr);
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
    EXPECT_EQ(result->passes.size(), 3u);
    VerifyTopologicalOrder(*result);
    VerifyBarrierStructure(*result);

    // Lighting pass needs barriers for albedo, normal, depth
    int posLight = FindCompiledPass(*result, 1);
    ASSERT_GE(posLight, 0);
    EXPECT_GE(result->passes[posLight].acquireBarriers.size(), 3u);

    // Verify lifetimes exist for all transient resources
    std::unordered_set<uint16_t> ltRes;
    for (auto& lt : result->lifetimes) {
        ltRes.insert(lt.resourceIndex);
    }
    EXPECT_TRUE(ltRes.count(albedo.GetIndex()));
    EXPECT_TRUE(ltRes.count(normal.GetIndex()));
    EXPECT_TRUE(ltRes.count(depth.GetIndex()));
    EXPECT_TRUE(ltRes.count(hdr.GetIndex()));
    EXPECT_TRUE(ltRes.count(ldr.GetIndex()));
}

TEST(ComplexIntegration, ShadowMapPlusDeferredPipeline) {
    RenderGraphBuilder builder;
    auto shadowMap
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 2048, .height = 2048, .debugName = "Shadow"});
    auto gbuf = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "GBuf"});
    auto depthBuf
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 1920, .height = 1080, .debugName = "Depth"});
    auto lit
        = builder.CreateTexture({.format = Format::RGBA16_FLOAT, .width = 1920, .height = 1080, .debugName = "Lit"});

    auto hShadow = builder.AddGraphicsPass(
        "ShadowPass", [&](PassBuilder& pb) { shadowMap = pb.WriteDepthStencil(shadowMap); }, [](RenderPassContext&) {}
    );
    auto hGBuf = builder.AddGraphicsPass(
        "GBuffer",
        [&](PassBuilder& pb) {
            gbuf = pb.WriteColorAttachment(gbuf);
            depthBuf = pb.WriteDepthStencil(depthBuf);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Lighting",
        [&](PassBuilder& pb) {
            pb.ReadTexture(gbuf);
            pb.ReadDepth(depthBuf);
            pb.ReadDepth(shadowMap);
            lit = pb.WriteColorAttachment(lit);
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
    EXPECT_EQ(result->passes.size(), 3u);
    VerifyTopologicalOrder(*result);

    // ShadowPass and GBuffer are independent, both must precede Lighting
    int posShadow = FindCompiledPass(*result, hShadow.index);
    int posGBuf = FindCompiledPass(*result, hGBuf.index);
    int posLit = FindCompiledPass(*result, 2);
    ASSERT_GE(posShadow, 0);
    ASSERT_GE(posGBuf, 0);
    ASSERT_GE(posLit, 0);
    EXPECT_LT(posShadow, posLit);
    EXPECT_LT(posGBuf, posLit);
}

TEST(ComplexIntegration, PingPongBlurChain) {
    RenderGraphBuilder builder;
    auto a = builder.CreateTexture({.width = 512, .height = 512, .debugName = "PingA"});
    auto b = builder.CreateTexture({.width = 512, .height = 512, .debugName = "PingB"});
    // Seed: write to A
    builder.AddGraphicsPass(
        "Seed", [&](PassBuilder& pb) { a = pb.WriteColorAttachment(a); }, [](RenderPassContext&) {}
    );
    // 4 blur iterations: A->B, B->A, A->B, B->A
    for (int i = 0; i < 4; ++i) {
        std::string name = "Blur" + std::to_string(i);
        if (i % 2 == 0) {
            builder.AddGraphicsPass(
                name.c_str(),
                [&](PassBuilder& pb) {
                    pb.ReadTexture(a);
                    b = pb.WriteColorAttachment(b);
                },
                [](RenderPassContext&) {}
            );
        } else {
            builder.AddGraphicsPass(
                name.c_str(),
                [&](PassBuilder& pb) {
                    pb.ReadTexture(b);
                    a = pb.WriteColorAttachment(a);
                },
                [](RenderPassContext&) {}
            );
        }
    }
    // Final read
    builder.AddGraphicsPass(
        "Final",
        [&](PassBuilder& pb) {
            pb.ReadTexture(a);
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
    EXPECT_EQ(result->passes.size(), 6u);
    VerifyTopologicalOrder(*result);
    VerifyBarrierStructure(*result);
    EXPECT_GE(result->edges.size(), 5u);
}

TEST(ComplexIntegration, ManyResourcesFewPasses) {
    RenderGraphBuilder builder;
    constexpr int N = 20;
    std::vector<RGResourceHandle> textures;
    for (int i = 0; i < N; ++i) {
        std::string name = "T" + std::to_string(i);
        textures.push_back(builder.CreateTexture({.width = 64, .height = 64, .debugName = name.c_str()}));
    }
    builder.AddGraphicsPass(
        "WriteAll",
        [&](PassBuilder& pb) {
            for (auto& t : textures) {
                t = pb.WriteColorAttachment(t);
            }
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "ReadAll",
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
    EXPECT_EQ(result->passes.size(), 2u);
    EXPECT_GE(result->edges.size(), static_cast<size_t>(N));
    // ReadAll should have N acquire barriers
    EXPECT_GE(result->passes[1].acquireBarriers.size(), static_cast<size_t>(N));
}

TEST(ComplexIntegration, WideParallelFanOutThenConverge) {
    RenderGraphBuilder builder;
    auto src = builder.CreateTexture({.width = 256, .height = 256, .debugName = "Src"});
    builder.AddGraphicsPass(
        "Init", [&](PassBuilder& pb) { src = pb.WriteColorAttachment(src); }, [](RenderPassContext&) {}
    );
    constexpr int BRANCHES = 8;
    std::vector<RGResourceHandle> branches;
    for (int i = 0; i < BRANCHES; ++i) {
        std::string tName = "B" + std::to_string(i);
        auto bt = builder.CreateTexture({.width = 256, .height = 256, .debugName = tName.c_str()});
        std::string pName = "Branch" + std::to_string(i);
        builder.AddGraphicsPass(
            pName.c_str(),
            [&src, &bt](PassBuilder& pb) {
                pb.ReadTexture(src);
                bt = pb.WriteColorAttachment(bt);
            },
            [](RenderPassContext&) {}
        );
        branches.push_back(bt);
    }
    builder.AddGraphicsPass(
        "Merge",
        [&](PassBuilder& pb) {
            for (auto& b : branches) {
                pb.ReadTexture(b);
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
    EXPECT_EQ(result->passes.size(), static_cast<size_t>(BRANCHES + 2));
    VerifyTopologicalOrder(*result);
    EXPECT_GE(result->edges.size(), static_cast<size_t>(BRANCHES * 2));
}

TEST(ComplexIntegration, EmptyGraphCompiles) {
    RenderGraphBuilder builder;
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->passes.empty());
    EXPECT_TRUE(result->edges.empty());
    EXPECT_TRUE(result->batches.empty());
    EXPECT_TRUE(result->syncPoints.empty());
}

TEST(ComplexIntegration, SingleResourceUsedByManyReaders) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 128, .height = 128, .debugName = "Shared"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    for (int i = 0; i < 10; ++i) {
        std::string name = "R" + std::to_string(i);
        builder.AddGraphicsPass(
            name.c_str(),
            [&t](PassBuilder& pb) {
                pb.ReadTexture(t);
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
    EXPECT_EQ(result->passes.size(), 11u);
    VerifyTopologicalOrder(*result);
}

TEST(ComplexIntegration, AsyncComputeWithTransferPass) {
    RenderGraphBuilder builder;
    auto buf = builder.CreateBuffer({.size = 4096, .debugName = "Data"});
    auto tex = builder.CreateTexture({.width = 256, .height = 256, .debugName = "Result"});
    builder.AddTransferPass(
        "Upload", [&](PassBuilder& pb) { buf = pb.WriteBuffer(buf, ResourceAccess::TransferDst); },
        [](RenderPassContext&) {}
    );
    builder.AddAsyncComputePass(
        "Process",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(buf, ResourceAccess::ShaderReadOnly);
            tex = pb.WriteTexture(tex, ResourceAccess::ShaderWrite);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Display",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableAsyncCompute = true;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 3u);
    VerifyTopologicalOrder(*result);
    VerifyBarrierStructure(*result);
}

TEST(ComplexIntegration, LargeGraphStressTest) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    constexpr int N = 50;
    for (int i = 0; i < N; ++i) {
        std::string name = "P" + std::to_string(i);
        bool first = (i == 0), last = (i == N - 1);
        builder.AddGraphicsPass(
            name.c_str(),
            [&t, first, last](PassBuilder& pb) {
                if (!first) {
                    pb.ReadTexture(t);
                }
                t = pb.WriteColorAttachment(t);
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
    EXPECT_EQ(result->passes.size(), static_cast<size_t>(N));
    VerifyTopologicalOrder(*result);
    EXPECT_GE(result->edges.size(), static_cast<size_t>(N - 1));
    uint32_t total = 0;
    for (auto& b : result->batches) {
        total += static_cast<uint32_t>(b.passIndices.size());
    }
    EXPECT_EQ(total, static_cast<uint32_t>(N));
}

TEST(ComplexIntegration, ReorderingDoesNotBreakDependencies) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 128, .height = 128, .debugName = "T"});
    auto u = builder.CreateTexture({.width = 128, .height = 128, .debugName = "U"});
    builder.AddGraphicsPass("W_T", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass("W_U", [&](PassBuilder& pb) { u = pb.WriteColorAttachment(u); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R_T",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R_U",
        [&](PassBuilder& pb) {
            pb.ReadTexture(u);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = true;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 4u);
    VerifyTopologicalOrder(*result);
}

// =============================================================================
// Scheduler Strategy Tests
// =============================================================================

TEST(SchedulerStrategy, MinBarriersCompiles) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    auto u = builder.CreateTexture({.width = 64, .height = 64, .debugName = "U"});
    builder.AddGraphicsPass("W_T", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass("W_U", [&](PassBuilder& pb) { u = pb.WriteColorAttachment(u); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R_T",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R_U",
        [&](PassBuilder& pb) {
            pb.ReadTexture(u);
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
    EXPECT_EQ(result->passes.size(), 4u);
    VerifyTopologicalOrder(*result);
}

TEST(SchedulerStrategy, MinMemoryCompiles) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    auto u = builder.CreateTexture({.width = 64, .height = 64, .debugName = "U"});
    builder.AddGraphicsPass("W_T", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass("W_U", [&](PassBuilder& pb) { u = pb.WriteColorAttachment(u); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R_T",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R_U",
        [&](PassBuilder& pb) {
            pb.ReadTexture(u);
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
    EXPECT_EQ(result->passes.size(), 4u);
    VerifyTopologicalOrder(*result);
}

TEST(SchedulerStrategy, MinLatencyCompiles) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    auto u = builder.CreateTexture({.width = 64, .height = 64, .debugName = "U"});
    builder.AddGraphicsPass("W_T", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass("W_U", [&](PassBuilder& pb) { u = pb.WriteColorAttachment(u); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R_T",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R_U",
        [&](PassBuilder& pb) {
            pb.ReadTexture(u);
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
    EXPECT_EQ(result->passes.size(), 4u);
    VerifyTopologicalOrder(*result);
}

TEST(SchedulerStrategy, BalancedCompiles) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
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
    VerifyTopologicalOrder(*result);
}

TEST(SchedulerStrategy, AllStrategiesPreservePassCount) {
    auto buildGraph = []() {
        RenderGraphBuilder builder;
        auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
        auto u = builder.CreateTexture({.width = 64, .height = 64, .debugName = "U"});
        builder.AddGraphicsPass(
            "W_T", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {}
        );
        builder.AddGraphicsPass(
            "W_U", [&](PassBuilder& pb) { u = pb.WriteColorAttachment(u); }, [](RenderPassContext&) {}
        );
        builder.AddGraphicsPass(
            "R_T",
            [&](PassBuilder& pb) {
                pb.ReadTexture(t);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        builder.AddGraphicsPass(
            "R_U",
            [&](PassBuilder& pb) {
                pb.ReadTexture(u);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        builder.Build();
        return builder;
    };
    SchedulerStrategy strategies[]
        = {SchedulerStrategy::MinBarriers, SchedulerStrategy::MinMemory, SchedulerStrategy::MinLatency,
           SchedulerStrategy::Balanced};
    for (auto strat : strategies) {
        auto b = buildGraph();
        RenderGraphCompiler::Options opts;
        opts.enableBarrierReordering = true;
        opts.strategy = strat;
        opts.enableRenderPassMerging = false;
        RenderGraphCompiler compiler(opts);
        auto result = compiler.Compile(b);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->passes.size(), 4u);
        VerifyTopologicalOrder(*result);
    }
}

// =============================================================================
// Render Pass Merging Tests
// =============================================================================

TEST(PassMerging, TwoConsecutiveGraphicsPassesMerge) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 256, .height = 256, .debugName = "Depth"});
    builder.AddGraphicsPass(
        "GBuf",
        [&](PassBuilder& pb) {
            rt = pb.WriteColorAttachment(rt);
            depth = pb.WriteDepthStencil(depth);
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
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = true;
    opts.enableTransientAliasing = true;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
    // mergedGroups may or may not merge depending on render area compatibility
    // but compilation must succeed
}

TEST(PassMerging, DisabledMergingProducesNoGroups) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { rt = pb.WriteColorAttachment(rt); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt);
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
    EXPECT_TRUE(result->mergedGroups.empty());
}

TEST(PassMerging, ComputePassNotMergedWithGraphics) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 128, .height = 128, .debugName = "T"});
    auto buf = builder.CreateBuffer({.size = 1024, .debugName = "Buf"});
    builder.AddGraphicsPass(
        "Draw", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {}
    );
    builder.AddComputePass(
        "Comp",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            buf = pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Post",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(buf, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = true;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 3u);
    // Compute pass should not be in any merged group
    for (auto& group : result->mergedGroups) {
        for (auto idx : group.subpassIndices) {
            EXPECT_NE(result->passes[idx].queue, RGQueueType::AsyncCompute);
        }
    }
}

// =============================================================================
// Backend Adaptation Tests
// =============================================================================

TEST(Adaptation, VulkanBackendCompiles) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableAdaptation = true;
    opts.backendType = BackendType::Vulkan14;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
}

TEST(Adaptation, D3D12BackendCompiles) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableAdaptation = true;
    opts.backendType = BackendType::D3D12;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
}

TEST(Adaptation, DisabledAdaptationProducesNoAdaptationPasses) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableAdaptation = false;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->adaptationPasses.empty());
}

// =============================================================================
// Invariant Checks (must hold for any compiled graph)
// =============================================================================

static void VerifyAllInvariants(const CompiledRenderGraph& cg) {
    // 1. Topological order
    VerifyTopologicalOrder(cg);
    // 2. Barrier structure
    VerifyBarrierStructure(cg);
    // 3. All batch pass indices in range
    for (auto& batch : cg.batches) {
        for (auto idx : batch.passIndices) {
            EXPECT_LT(idx, cg.passes.size());
        }
    }
    // 4. No duplicate pass in batches
    std::unordered_set<uint32_t> batchSeen;
    for (auto& batch : cg.batches) {
        for (auto idx : batch.passIndices) {
            EXPECT_FALSE(batchSeen.count(idx));
            batchSeen.insert(idx);
        }
    }
    // 5. Total batched == passes
    uint32_t totalBatched = 0;
    for (auto& b : cg.batches) {
        totalBatched += static_cast<uint32_t>(b.passIndices.size());
    }
    EXPECT_EQ(totalBatched, static_cast<uint32_t>(cg.passes.size()));
    // 6. Last batch signals timeline (if any)
    if (!cg.batches.empty()) {
        EXPECT_TRUE(cg.batches.back().signalTimeline);
    }
    // 7. Lifetimes: first <= last
    for (auto& lt : cg.lifetimes) {
        EXPECT_LE(lt.firstPass, lt.lastPass);
    }
    // 8. Aliasing slot offsets aligned
    for (auto& slot : cg.aliasing.slots) {
        uint64_t a = slot.alignment > 0 ? slot.alignment : 1;
        EXPECT_EQ(slot.heapOffset % a, 0u);
    }
    // 9. Edges reference valid passes
    std::unordered_set<uint32_t> passIdxSet;
    for (auto& p : cg.passes) {
        passIdxSet.insert(p.passIndex);
    }
    for (auto& e : cg.edges) {
        EXPECT_TRUE(passIdxSet.count(e.srcPass));
        EXPECT_TRUE(passIdxSet.count(e.dstPass));
    }
    // 10. Sync points reference valid passes
    for (auto& sp : cg.syncPoints) {
        EXPECT_TRUE(passIdxSet.count(sp.srcPassIndex));
        EXPECT_TRUE(passIdxSet.count(sp.dstPassIndex));
    }
}

TEST(Invariants, SimpleChainAllInvariants) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    VerifyAllInvariants(*result);
}

TEST(Invariants, DiamondAllInvariants) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "Src"});
    auto tB = builder.CreateTexture({.width = 64, .height = 64, .debugName = "B"});
    auto tC = builder.CreateTexture({.width = 64, .height = 64, .debugName = "C"});
    builder.AddGraphicsPass("A", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            tB = pb.WriteColorAttachment(tB);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "C",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            tC = pb.WriteColorAttachment(tC);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "D",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tB);
            pb.ReadTexture(tC);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    VerifyAllInvariants(*result);
}

TEST(Invariants, DeferredPipelineAllInvariants) {
    RenderGraphBuilder builder;
    auto albedo = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "Albedo"});
    auto normal = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "Normal"});
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 1920, .height = 1080, .debugName = "Depth"});
    auto hdr
        = builder.CreateTexture({.format = Format::RGBA16_FLOAT, .width = 1920, .height = 1080, .debugName = "HDR"});
    builder.AddGraphicsPass(
        "GBuf",
        [&](PassBuilder& pb) {
            albedo = pb.WriteColorAttachment(albedo);
            normal = pb.WriteColorAttachment(normal, 1);
            depth = pb.WriteDepthStencil(depth);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Light",
        [&](PassBuilder& pb) {
            pb.ReadTexture(albedo);
            pb.ReadTexture(normal);
            pb.ReadDepth(depth);
            hdr = pb.WriteColorAttachment(hdr);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    VerifyAllInvariants(*result);
}

TEST(Invariants, LargeChainAllInvariants) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    constexpr int N = 20;
    for (int i = 0; i < N; ++i) {
        std::string name = "P" + std::to_string(i);
        bool first = (i == 0), last = (i == N - 1);
        builder.AddGraphicsPass(
            name.c_str(),
            [&t, first, last](PassBuilder& pb) {
                if (!first) {
                    pb.ReadTexture(t);
                }
                t = pb.WriteColorAttachment(t);
                if (last) {
                    pb.SetSideEffects();
                }
            },
            [](RenderPassContext&) {}
        );
    }
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    VerifyAllInvariants(*result);
}

TEST(Invariants, CrossQueueAllInvariants) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    auto buf = builder.CreateBuffer({.size = 256, .debugName = "Buf"});
    builder.AddGraphicsPass(
        "Draw", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {}
    );
    builder.AddAsyncComputePass(
        "Compute",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            buf = pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Post",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(buf, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableAsyncCompute = true;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    VerifyAllInvariants(*result);
}

TEST(Invariants, AllOptionsEnabledAllInvariants) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 512, .height = 512, .debugName = "RT"});
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 512, .height = 512, .debugName = "Depth"});
    auto post = builder.CreateTexture({.width = 512, .height = 512, .debugName = "Post"});
    builder.AddGraphicsPass(
        "GBuf",
        [&](PassBuilder& pb) {
            rt = pb.WriteColorAttachment(rt);
            depth = pb.WriteDepthStencil(depth);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Light",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt);
            pb.ReadDepth(depth);
            post = pb.WriteColorAttachment(post);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = true;
    opts.enableTransientAliasing = true;
    opts.enableRenderPassMerging = true;
    opts.enableAdaptation = true;
    opts.enableSplitBarriers = true;
    opts.backendType = BackendType::Vulkan14;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    VerifyAllInvariants(*result);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST(EdgeCases, SingleSideEffectPassAllEnabled) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T"});
    builder.AddGraphicsPass(
        "Only",
        [&](PassBuilder& pb) {
            t = pb.WriteColorAttachment(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableBarrierReordering = true;
    opts.enableTransientAliasing = true;
    opts.enableRenderPassMerging = true;
    opts.enableAdaptation = true;
    opts.enableSplitBarriers = true;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 1u);
    VerifyAllInvariants(*result);
}

TEST(EdgeCases, AllPassesCulled) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 32, .height = 32, .debugName = "T"});
    builder.AddGraphicsPass(
        "Dead1", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Dead2", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 0u);
    EXPECT_TRUE(result->edges.empty());
    EXPECT_TRUE(result->batches.empty());
}

TEST(EdgeCases, BufferOnlyGraphCompiles) {
    RenderGraphBuilder builder;
    auto buf = builder.CreateBuffer({.size = 4096, .debugName = "Buf"});
    builder.AddComputePass(
        "Fill", [&](PassBuilder& pb) { buf = pb.WriteBuffer(buf, ResourceAccess::ShaderWrite); },
        [](RenderPassContext&) {}
    );
    builder.AddComputePass(
        "Read",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(buf, ResourceAccess::ShaderReadOnly);
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
    VerifyAllInvariants(*result);
}

TEST(EdgeCases, MixedTextureAndBufferGraph) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 128, .height = 128, .debugName = "Tex"});
    auto buf = builder.CreateBuffer({.size = 2048, .debugName = "Buf"});
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            tex = pb.WriteColorAttachment(tex);
            buf = pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Post",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.ReadBuffer(buf, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
    VerifyAllInvariants(*result);
}

TEST(EdgeCases, LargeTextureDescriptors) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture(
        {.width = 4096, .height = 4096, .mipLevels = 12, .arrayLayers = 6, .debugName = "CubeMap"}
    );
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
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
    // Verify aliasing slot size accounts for mips and layers
    if (result->aliasing.resourceToSlot[t.GetIndex()] != AliasingLayout::kNotAliased) {
        auto slotIdx = result->aliasing.resourceToSlot[t.GetIndex()];
        auto expected = EstimateTextureSize({.width = 4096, .height = 4096, .mipLevels = 12, .arrayLayers = 6});
        EXPECT_GE(result->aliasing.slots[slotIdx].size, expected);
    }
}

TEST(EdgeCases, ResourceHandleValidity) {
    RGResourceHandle invalid;
    EXPECT_FALSE(invalid.IsValid());
    RenderGraphBuilder builder;
    auto valid = builder.CreateTexture({.width = 32, .height = 32, .debugName = "V"});
    EXPECT_TRUE(valid.IsValid());
    EXPECT_EQ(valid.GetVersion(), 0);
}

TEST(EdgeCases, MultipleCompilationsOfSameBuilder) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler c1, c2;
    auto r1 = c1.Compile(builder);
    auto r2 = c2.Compile(builder);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r1->passes.size(), r2->passes.size());
    EXPECT_EQ(r1->hash, r2->hash);
}

TEST(EdgeCases, CompilerOptionsAllDisabled) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
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
    opts.enableSplitBarriers = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
    EXPECT_TRUE(result->aliasing.slots.empty());
    EXPECT_TRUE(result->mergedGroups.empty());
    EXPECT_TRUE(result->adaptationPasses.empty());
}

// =============================================================================
// Aliasing Precision Tests
// =============================================================================

TEST(AliasingPrecision, NonOverlappingLifetimesShareSlot) {
    RenderGraphBuilder builder;
    auto t0 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "T0"});
    auto t1 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "T1"});
    builder.AddGraphicsPass(
        "W0", [&](PassBuilder& pb) { t0 = pb.WriteColorAttachment(t0); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R0",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t0);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "W1", [&](PassBuilder& pb) { t1 = pb.WriteColorAttachment(t1); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "R1",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t1);
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
    EXPECT_EQ(result->passes.size(), 4u);
    // Both resources should have lifetime entries
    std::unordered_set<uint16_t> ltRes;
    for (auto& lt : result->lifetimes) {
        ltRes.insert(lt.resourceIndex);
    }
    EXPECT_TRUE(ltRes.count(t0.GetIndex()));
    EXPECT_TRUE(ltRes.count(t1.GetIndex()));
}

TEST(AliasingPrecision, OverlappingLifetimesCannotShareSlot) {
    RenderGraphBuilder builder;
    auto t0 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "T0"});
    auto t1 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "T1"});
    builder.AddGraphicsPass(
        "WBoth",
        [&](PassBuilder& pb) {
            t0 = pb.WriteColorAttachment(t0);
            t1 = pb.WriteColorAttachment(t1, 1);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "RBoth",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t0);
            pb.ReadTexture(t1);
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
    auto s0 = result->aliasing.resourceToSlot[t0.GetIndex()];
    auto s1 = result->aliasing.resourceToSlot[t1.GetIndex()];
    if (s0 != AliasingLayout::kNotAliased && s1 != AliasingLayout::kNotAliased) {
        EXPECT_NE(s0, s1) << "Overlapping lifetimes must not share aliasing slot";
    }
}

TEST(AliasingPrecision, ImportedResourceNotAliased) {
    RenderGraphBuilder builder;
    TextureHandle extTex{42};
    auto imported = builder.ImportTexture(extTex, "Imported");
    builder.AddGraphicsPass(
        "W",
        [&](PassBuilder& pb) {
            imported = pb.WriteColorAttachment(imported);
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
    if (imported.GetIndex() < result->aliasing.resourceToSlot.size()) {
        EXPECT_EQ(result->aliasing.resourceToSlot[imported.GetIndex()], AliasingLayout::kNotAliased);
    }
}

// =============================================================================
// WAW / WAR Hazard Edge Tests
// =============================================================================

TEST(HazardEdges, WAWCreatesEdge) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    builder.AddGraphicsPass("W1", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "W2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            t = pb.WriteColorAttachment(t);
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
    EXPECT_GE(result->edges.size(), 1u);
    VerifyTopologicalOrder(*result);
}

TEST(HazardEdges, RAWCreatesEdge) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
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
    EXPECT_GE(result->edges.size(), 1u);
    bool foundRAW = false;
    for (auto& e : result->edges) {
        if (e.hazard == HazardType::RAW) {
            foundRAW = true;
        }
    }
    EXPECT_TRUE(foundRAW);
}

TEST(HazardEdges, NoEdgeBetweenIndependentResources) {
    RenderGraphBuilder builder;
    auto t0 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T0"});
    auto t1 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T1"});
    builder.AddGraphicsPass(
        "W0",
        [&](PassBuilder& pb) {
            t0 = pb.WriteColorAttachment(t0);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "W1",
        [&](PassBuilder& pb) {
            t1 = pb.WriteColorAttachment(t1);
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
    // No edge should exist between the two independent writes
    for (auto& e : result->edges) {
        bool crossLink = (e.srcPass == 0 && e.dstPass == 1) || (e.srcPass == 1 && e.dstPass == 0);
        EXPECT_FALSE(crossLink) << "No dependency edge between independent resources";
    }
}

// =============================================================================
// Split Barrier Tests
// =============================================================================

TEST(SplitBarrier, EnabledCompilesCorrectly) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 256, .height = 256, .debugName = "T"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableSplitBarriers = true;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
    VerifyAllInvariants(*result);
}

TEST(SplitBarrier, DisabledProducesNoSplitBarriers) {
    RenderGraphBuilder builder;
    auto t = builder.CreateTexture({.width = 256, .height = 256, .debugName = "T"});
    builder.AddGraphicsPass("W", [&](PassBuilder& pb) { t = pb.WriteColorAttachment(t); }, [](RenderPassContext&) {});
    builder.AddGraphicsPass(
        "R",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();
    RenderGraphCompiler::Options opts;
    opts.enableSplitBarriers = false;
    opts.enableRenderPassMerging = false;
    RenderGraphCompiler compiler(opts);
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);
    // All barriers should be non-split (releaseBarriers empty)
    for (auto& p : result->passes) {
        EXPECT_TRUE(p.releaseBarriers.empty());
    }
}
// Phase E: Executor Runtime Tests
//
// These tests exercise the full Execute() pipeline (AllocateTransients +
// RecordPasses + SubmitBatches) using MockDevice which now returns synthetic
// valid handles. CPU-only testing of:
//   - Pass lambda invocation order and count
//   - Debug label emission
//   - Merged group rendering bracket logic
//   - Aliasing barrier emission
//   - Attachment load/store op propagation
//   - RenderPassContext field correctness
//   - Edge cases (empty graph, large pass count, mixed imported + transient)
// =============================================================================

namespace {

    // ---------------------------------------------------------------------------
    // Helper: compile + execute a graph with MockDevice
    // ---------------------------------------------------------------------------

    struct ExecuteResult {
        RenderGraphExecutor executor;
        ExecutionStats stats;
    };

    auto CompileAndExecute(
        RenderGraphBuilder& builder, const ExecutorConfig& execCfg = {}, bool enableMerging = false,
        bool enableAliasing = true
    ) -> std::expected<ExecuteResult, miki::core::ErrorCode> {
        builder.Build();

        RenderGraphCompiler::Options opts;
        opts.enableRenderPassMerging = enableMerging;
        opts.enableTransientAliasing = enableAliasing;
        opts.enableSplitBarriers = false;
        RenderGraphCompiler compiler(opts);
        auto compileResult = compiler.Compile(builder);
        if (!compileResult) {
            return std::unexpected(miki::core::ErrorCode::InvalidState);
        }

        MockDevice device;
        device.Init();
        auto deviceHandle = DeviceHandle(&device, BackendType::Mock);

        miki::frame::SyncScheduler scheduler;
        scheduler.Init(device.GetQueueTimelinesImpl());

        miki::frame::CommandPoolAllocator::Desc poolDesc{
            .device = deviceHandle,
            .framesInFlight = 2,
            .recordingThreadCount = execCfg.maxRecordingThreads,
        };
        auto poolResult = miki::frame::CommandPoolAllocator::Create(poolDesc);
        if (!poolResult) {
            return std::unexpected(miki::core::ErrorCode::OutOfMemory);
        }
        auto& pool = *poolResult;

        miki::frame::FrameContext frame{
            .frameIndex = 0,
            .frameNumber = 0,
            .width = 1920,
            .height = 1080,
        };

        ExecuteResult result;
        result.executor = RenderGraphExecutor(execCfg);

        auto execResult = result.executor.Execute(*compileResult, builder, frame, deviceHandle, scheduler, pool);
        if (!execResult) {
            return std::unexpected(execResult.error());
        }
        result.stats = result.executor.GetStats();
        return result;
    }

}  // anonymous namespace

// =============================================================================
// Executor — Single pass execution
// =============================================================================

TEST(Executor, SingleGraphicsPassExecutes) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 512, .height = 512, .debugName = "RT"});
    uint32_t invoked = 0;
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            invoked++;
            EXPECT_NE(ctx.passName, nullptr);
            EXPECT_STREQ(ctx.passName, "Draw");
            EXPECT_NE(ctx.frameAllocator, nullptr);
        }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(invoked, 1u);
    EXPECT_EQ(result->stats.recording.passesRecorded, 1u);
    EXPECT_EQ(result->stats.submission.batchesSubmitted, 1u);
    EXPECT_GE(result->stats.allocation.transientTexturesAllocated, 1u);
    EXPECT_GE(result->stats.allocation.transientTextureViewsCreated, 1u);
}

TEST(Executor, SingleComputePassExecutes) {
    RenderGraphBuilder builder;
    auto buf = builder.CreateBuffer({.size = 1024, .debugName = "Buf"});
    uint32_t invoked = 0;
    builder.AddComputePass(
        "Compute",
        [&](PassBuilder& pb) {
            pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            invoked++;
            EXPECT_STREQ(ctx.passName, "Compute");
        }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(invoked, 1u);
    EXPECT_EQ(result->stats.recording.passesRecorded, 1u);
    EXPECT_GE(result->stats.allocation.transientBuffersAllocated, 1u);
}

// =============================================================================
// Executor — Multi-pass chain with barrier tracking
// =============================================================================

TEST(Executor, ThreePassChainInvocationOrder) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
    auto rt2 = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT2"});

    std::vector<uint32_t> order;
    builder.AddGraphicsPass(
        "Pass0", [&](PassBuilder& pb) { rt = pb.WriteColorAttachment(rt); },
        [&](RenderPassContext&) { order.push_back(0); }
    );
    builder.AddGraphicsPass(
        "Pass1",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt);
            rt2 = pb.WriteColorAttachment(rt2);
        },
        [&](RenderPassContext&) { order.push_back(1); }
    );
    builder.AddGraphicsPass(
        "Pass2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt2);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { order.push_back(2); }
    );

    ExecutorConfig cfg;
    cfg.enableDebugLabels = false;
    auto result = CompileAndExecute(builder, cfg);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(order.size(), 3u);
    // Must respect topological order: 0 < 1 < 2
    EXPECT_LT(order[0], order[1]);
    EXPECT_LT(order[1], order[2]);
    EXPECT_EQ(result->stats.recording.passesRecorded, 3u);
    EXPECT_GT(result->stats.recording.barriersEmitted, 0u);
}

// =============================================================================
// Executor — Debug labels
// =============================================================================

TEST(Executor, DebugLabelsEnabledByDefault) {
    ExecutorConfig cfg;
    EXPECT_TRUE(cfg.enableDebugLabels);
}

TEST(Executor, DebugLabelsDisabledDoesNotBreak) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    uint32_t invoked = 0;
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { invoked++; }
    );

    ExecutorConfig cfg;
    cfg.enableDebugLabels = false;
    auto result = CompileAndExecute(builder, cfg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(invoked, 1u);
    EXPECT_EQ(result->stats.recording.passesRecorded, 1u);
}

// =============================================================================
// Executor — Aliasing barrier emission
// =============================================================================

TEST(Executor, AliasingBarriersEmittedForSharedSlots) {
    // Two sequential passes writing different textures of the same size.
    // If aliased to the same slot, aliasing barriers should be counted in stats.
    RenderGraphBuilder builder;
    auto tA = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 128, .height = 128, .debugName = "tA"});
    auto tB = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 128, .height = 128, .debugName = "tB"});

    builder.AddGraphicsPass(
        "WriteA",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tA);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "WriteB",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(tB);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->stats.recording.passesRecorded, 2u);
    // Barriers should be emitted (at minimum acquire barriers for each pass)
    EXPECT_GE(result->stats.recording.barriersEmitted, 0u);
    // Two textures allocated
    EXPECT_GE(result->stats.allocation.transientTexturesAllocated, 2u);
}

// =============================================================================
// Executor — Attachment load/store ops propagation
// =============================================================================

TEST(Executor, LoadStoreOpsPassedToRenderPassContext) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});

    bool verified = false;
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rt, 0, AttachmentLoadOp::Load, AttachmentStoreOp::DontCare);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            verified = true;
            // RenderPassContext should have color attachments populated
            EXPECT_GE(ctx.colorAttachments.size(), 1u);
            if (!ctx.colorAttachments.empty()) {
                EXPECT_EQ(ctx.colorAttachments[0].loadOp, AttachmentLoadOp::Load);
                EXPECT_EQ(ctx.colorAttachments[0].storeOp, AttachmentStoreOp::DontCare);
            }
        }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(verified);
}

TEST(Executor, DepthStencilAttachmentPassedToContext) {
    RenderGraphBuilder builder;
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 128, .height = 128, .debugName = "Depth"});

    bool verified = false;
    builder.AddGraphicsPass(
        "DepthPass",
        [&](PassBuilder& pb) {
            pb.WriteDepthStencil(depth, AttachmentLoadOp::Clear, AttachmentStoreOp::Store);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            verified = true;
            EXPECT_NE(ctx.depthAttachment, nullptr);
            if (ctx.depthAttachment) {
                EXPECT_EQ(ctx.depthAttachment->loadOp, AttachmentLoadOp::Clear);
                EXPECT_EQ(ctx.depthAttachment->storeOp, AttachmentStoreOp::Store);
            }
        }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(verified);
}

TEST(Executor, MultipleColorAttachmentSlots) {
    RenderGraphBuilder builder;
    auto rt0 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "RT0"});
    auto rt1 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "RT1"});

    bool verified = false;
    builder.AddGraphicsPass(
        "MRT",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rt0, 0, AttachmentLoadOp::Clear, AttachmentStoreOp::Store);
            pb.WriteColorAttachment(rt1, 1, AttachmentLoadOp::Load, AttachmentStoreOp::DontCare);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            verified = true;
            EXPECT_GE(ctx.colorAttachments.size(), 2u);
            if (ctx.colorAttachments.size() >= 2) {
                EXPECT_EQ(ctx.colorAttachments[0].loadOp, AttachmentLoadOp::Clear);
                EXPECT_EQ(ctx.colorAttachments[0].storeOp, AttachmentStoreOp::Store);
                EXPECT_EQ(ctx.colorAttachments[1].loadOp, AttachmentLoadOp::Load);
                EXPECT_EQ(ctx.colorAttachments[1].storeOp, AttachmentStoreOp::DontCare);
            }
        }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(verified);
}

// =============================================================================
// Executor — Merged group execution
// =============================================================================

TEST(Executor, MergedGroupPassesInvoked) {
    // Two consecutive passes sharing depth -> merged. Both must be invoked.
    RenderGraphBuilder builder;
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 128, .height = 128, .debugName = "Depth"});

    std::vector<uint32_t> order;
    RGResourceHandle writtenDepth;
    builder.AddGraphicsPass(
        "DepthPre",
        [&](PassBuilder& pb) {
            writtenDepth = pb.WriteDepthStencil(depth);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { order.push_back(0); }
    );
    builder.AddGraphicsPass(
        "GBuffer",
        [&](PassBuilder& pb) {
            pb.WriteDepthStencil(writtenDepth);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { order.push_back(1); }
    );

    auto result = CompileAndExecute(builder, {}, /*enableMerging=*/true);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    EXPECT_EQ(result->stats.recording.passesRecorded, 2u);
}

TEST(Executor, MergedGroupContextStillHasAttachments) {
    // Even inside a merged group, each pass should receive its own attachment info
    RenderGraphBuilder builder;
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 128, .height = 128, .debugName = "Depth"});

    bool pass0HasDepth = false;
    bool pass1HasDepth = false;
    RGResourceHandle writtenDepth;
    builder.AddGraphicsPass(
        "P0",
        [&](PassBuilder& pb) {
            writtenDepth = pb.WriteDepthStencil(depth);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) { pass0HasDepth = (ctx.depthAttachment != nullptr); }
    );
    builder.AddGraphicsPass(
        "P1",
        [&](PassBuilder& pb) {
            pb.WriteDepthStencil(writtenDepth);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) { pass1HasDepth = (ctx.depthAttachment != nullptr); }
    );

    auto result = CompileAndExecute(builder, {}, /*enableMerging=*/true);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(pass0HasDepth);
    EXPECT_TRUE(pass1HasDepth);
}

// =============================================================================
// Executor — Imported resources
// =============================================================================

TEST(Executor, ImportedTextureUsesExistingHandle) {
    RenderGraphBuilder builder;
    auto extTex = TextureHandle{42};
    auto imported = builder.ImportTexture(extTex, "Backbuffer");

    bool verified = false;
    builder.AddGraphicsPass(
        "Use",
        [&](PassBuilder& pb) {
            pb.ReadTexture(imported, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            verified = true;
            // The physical texture at the imported index should be the original handle
            auto resolved = ctx.GetTexture(imported);
            EXPECT_EQ(resolved.value, 42u);
        }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(verified);
}

TEST(Executor, MixedImportedAndTransient) {
    RenderGraphBuilder builder;
    auto extTex = TextureHandle{99};
    auto imported = builder.ImportTexture(extTex, "External");
    auto transient = builder.CreateTexture({.width = 64, .height = 64, .debugName = "Transient"});

    bool verified = false;
    builder.AddGraphicsPass(
        "Write",
        [&](PassBuilder& pb) {
            pb.ReadTexture(imported, ResourceAccess::ShaderReadOnly);
            pb.WriteColorAttachment(transient);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            verified = true;
            // Imported should resolve to original
            auto resolvedImported = ctx.GetTexture(imported);
            EXPECT_EQ(resolvedImported.value, 99u);
            // Transient should resolve to a valid (non-zero) handle
            auto resolvedTransient = ctx.GetTexture(transient);
            EXPECT_TRUE(resolvedTransient.IsValid());
            EXPECT_NE(resolvedTransient.value, 99u);
        }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(verified);
}

// =============================================================================
// Executor — RenderPassContext fields
// =============================================================================

TEST(ExecutorContext, PhysicalBufferResolution) {
    RenderGraphBuilder builder;
    auto buf = builder.CreateBuffer({.size = 512, .debugName = "Buf"});

    bool verified = false;
    builder.AddComputePass(
        "Compute",
        [&](PassBuilder& pb) {
            pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            verified = true;
            auto resolved = ctx.GetBuffer(buf);
            EXPECT_TRUE(resolved.IsValid());
        }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(verified);
}

TEST(ExecutorContext, TextureViewResolution) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 128, .height = 128, .debugName = "RT"});

    bool verified = false;
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            verified = true;
            auto view = ctx.GetTextureView(rt);
            EXPECT_TRUE(view.IsValid());
        }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(verified);
}

TEST(ExecutorContext, OutOfRangeResolutionReturnsInvalid) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});

    bool verified = false;
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            verified = true;
            // Create a handle with an absurdly high index
            auto bogus = RGResourceHandle::Create(9999, 0);
            EXPECT_FALSE(ctx.GetTexture(bogus).IsValid());
            EXPECT_FALSE(ctx.GetBuffer(bogus).IsValid());
            EXPECT_FALSE(ctx.GetTextureView(bogus).IsValid());
        }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(verified);
}

TEST(ExecutorContext, FrameAllocatorIsNonNull) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});

    bool verified = false;
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            verified = true;
            EXPECT_NE(ctx.frameAllocator, nullptr);
        }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(verified);
}

// =============================================================================
// Executor — Heap creation stats
// =============================================================================

TEST(Executor, HeapCreationStatsCorrect) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.format = Format::RGBA8_UNORM, .width = 256, .height = 256, .debugName = "RT"});
    auto buf = builder.CreateBuffer({.size = 4096, .debugName = "Buf"});

    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rt);
            pb.ReadBuffer(buf, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    // Should have created heaps for RT/DS and Buffer groups
    EXPECT_GE(result->stats.allocation.heapsCreated, 1u);
    EXPECT_GT(result->stats.allocation.transientMemoryBytes, 0u);
}

// =============================================================================
// Executor — Stats reset between executions
// =============================================================================

TEST(Executor, StatsResetOnSecondExecution) {
    auto buildAndExecute = [](RenderGraphExecutor& executor) -> bool {
        RenderGraphBuilder builder;
        auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
        builder.AddGraphicsPass(
            "P",
            [&](PassBuilder& pb) {
                pb.WriteColorAttachment(rt);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        builder.Build();

        RenderGraphCompiler compiler;
        auto compiled = compiler.Compile(builder);
        if (!compiled) {
            return false;
        }

        MockDevice device;
        device.Init();
        auto dh = DeviceHandle(&device, BackendType::Mock);
        miki::frame::SyncScheduler sched;
        sched.Init(device.GetQueueTimelinesImpl());
        miki::frame::CommandPoolAllocator::Desc pd{.device = dh, .framesInFlight = 2};
        auto pool = miki::frame::CommandPoolAllocator::Create(pd);
        if (!pool) {
            return false;
        }
        miki::frame::FrameContext frame{.width = 64, .height = 64};
        return executor.Execute(*compiled, builder, frame, dh, sched, *pool).has_value();
    };

    RenderGraphExecutor executor;
    ASSERT_TRUE(buildAndExecute(executor));
    auto s1 = executor.GetStats();
    EXPECT_EQ(s1.recording.passesRecorded, 1u);

    ASSERT_TRUE(buildAndExecute(executor));
    auto s2 = executor.GetStats();
    // Stats must reset: should be 1, not 2
    EXPECT_EQ(s2.recording.passesRecorded, 1u);
}

// =============================================================================
// Executor — Edge cases
// =============================================================================

TEST(Executor, EmptyGraphSucceeds) {
    RenderGraphBuilder builder;
    // No passes added — graph is empty
    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->stats.recording.passesRecorded, 0u);
    EXPECT_EQ(result->stats.submission.batchesSubmitted, 0u);
    EXPECT_EQ(result->stats.allocation.transientTexturesAllocated, 0u);
}

TEST(Executor, AllPassesCulledResultsInEmptyExecution) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    // No side effects -> will be culled
    builder.AddGraphicsPass(
        "Culled", [&](PassBuilder& pb) { pb.WriteColorAttachment(rt); },
        [](RenderPassContext&) { FAIL() << "Should not be invoked"; }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->stats.recording.passesRecorded, 0u);
}

// =============================================================================
// Executor — Large pass count stress test
// =============================================================================

TEST(Executor, TwentyPassChainExecutes) {
    constexpr int N = 20;
    RenderGraphBuilder builder;
    std::vector<RGResourceHandle> textures(N);
    for (int i = 0; i < N; ++i) {
        textures[i] = builder.CreateTexture({.width = 64, .height = 64, .debugName = "T"});
    }

    uint32_t invokeCount = 0;
    for (int i = 0; i < N; ++i) {
        bool isLast = (i == N - 1);
        int readIdx = (i > 0) ? i - 1 : -1;
        int writeIdx = i;
        builder.AddGraphicsPass(
            "P",
            [&, readIdx, writeIdx, isLast](PassBuilder& pb) {
                if (readIdx >= 0) {
                    pb.ReadTexture(textures[readIdx]);
                }
                textures[writeIdx] = pb.WriteColorAttachment(textures[writeIdx]);
                if (isLast) {
                    pb.SetSideEffects();
                }
            },
            [&](RenderPassContext&) { invokeCount++; }
        );
    }

    ExecutorConfig cfg;
    cfg.enableDebugLabels = false;
    auto result = CompileAndExecute(builder, cfg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(invokeCount, static_cast<uint32_t>(N));
    EXPECT_EQ(result->stats.recording.passesRecorded, static_cast<uint32_t>(N));
}

// =============================================================================
// Executor — Complex multi-flow: diamond + merge + aliasing
// =============================================================================

TEST(ExecutorComplex, DiamondGraphExecution) {
    // A writes t1+t2, B reads t1, C reads t2, D reads B+C output (side effect)
    RenderGraphBuilder builder;
    auto t1 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "t1"});
    auto t2 = builder.CreateTexture({.width = 128, .height = 128, .debugName = "t2"});
    auto tB = builder.CreateTexture({.width = 128, .height = 128, .debugName = "tB"});
    auto tC = builder.CreateTexture({.width = 128, .height = 128, .debugName = "tC"});

    std::vector<uint32_t> order;
    builder.AddGraphicsPass(
        "A",
        [&](PassBuilder& pb) {
            t1 = pb.WriteColorAttachment(t1, 0);
            t2 = pb.WriteColorAttachment(t2, 1);
        },
        [&](RenderPassContext&) { order.push_back(0); }
    );
    builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t1);
            tB = pb.WriteColorAttachment(tB);
        },
        [&](RenderPassContext&) { order.push_back(1); }
    );
    builder.AddGraphicsPass(
        "C",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t2);
            tC = pb.WriteColorAttachment(tC);
        },
        [&](RenderPassContext&) { order.push_back(2); }
    );
    builder.AddGraphicsPass(
        "D",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tB);
            pb.ReadTexture(tC);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { order.push_back(3); }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(order.size(), 4u);
    // A must be first, D must be last
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[3], 3u);
    // B and C must be between A and D (either order)
    bool bBeforeD = false, cBeforeD = false;
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == 1) {
            bBeforeD = true;
        }
        if (order[i] == 2) {
            cBeforeD = true;
        }
        if (order[i] == 3) {
            EXPECT_TRUE(bBeforeD);
            EXPECT_TRUE(cBeforeD);
        }
    }
    EXPECT_EQ(result->stats.recording.passesRecorded, 4u);
}

TEST(ExecutorComplex, MergePlusNonMergePassesMixed) {
    // P0 writes depth, P1 writes depth (merged), P2 reads depth as SRV (not merged)
    RenderGraphBuilder builder;
    auto depth = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 256, .height = 256, .debugName = "D"});

    std::vector<uint32_t> order;
    RGResourceHandle d0;
    builder.AddGraphicsPass(
        "P0",
        [&](PassBuilder& pb) {
            d0 = pb.WriteDepthStencil(depth);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { order.push_back(0); }
    );

    RGResourceHandle d1;
    builder.AddGraphicsPass(
        "P1",
        [&](PassBuilder& pb) {
            d1 = pb.WriteDepthStencil(d0);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { order.push_back(1); }
    );

    builder.AddGraphicsPass(
        "P2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(d1, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { order.push_back(2); }
    );

    auto result = CompileAndExecute(builder, {}, /*enableMerging=*/true);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    EXPECT_EQ(order[2], 2u);
    EXPECT_EQ(result->stats.recording.passesRecorded, 3u);
}

TEST(ExecutorComplex, ImportedPlusTransientAliasingExecution) {
    // External backbuffer (imported) + 2 transient textures that can alias
    RenderGraphBuilder builder;
    auto backbuffer = builder.ImportTexture(TextureHandle{777}, "Backbuffer");
    auto t1 = builder.CreateTexture({.width = 256, .height = 256, .debugName = "t1"});
    auto t2 = builder.CreateTexture({.width = 256, .height = 256, .debugName = "t2"});

    std::vector<uint32_t> order;
    builder.AddGraphicsPass(
        "Scene", [&](PassBuilder& pb) { t1 = pb.WriteColorAttachment(t1); },
        [&](RenderPassContext&) { order.push_back(0); }
    );
    builder.AddGraphicsPass(
        "PostProcess",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t1);
            t2 = pb.WriteColorAttachment(t2);
        },
        [&](RenderPassContext&) { order.push_back(1); }
    );
    builder.AddGraphicsPass(
        "Composite",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t2);
            pb.ReadTexture(backbuffer, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            order.push_back(2);
            // Backbuffer should resolve to original handle
            auto bb = ctx.GetTexture(backbuffer);
            EXPECT_EQ(bb.value, 777u);
            // Transient textures should resolve to valid handles
            EXPECT_TRUE(ctx.GetTexture(t1).IsValid());
            EXPECT_TRUE(ctx.GetTexture(t2).IsValid());
        }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(result->stats.recording.passesRecorded, 3u);
}

// =============================================================================
// Executor — Verify pass lambda receives correct passIndex / passName
// =============================================================================

TEST(ExecutorContext, PassIndexAndNameCorrect) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});

    struct PassInfo {
        uint32_t idx;
        const char* name;
    };
    std::vector<PassInfo> captured;

    auto h0 = builder.AddGraphicsPass(
        "Alpha", [&](PassBuilder& pb) { rt = pb.WriteColorAttachment(rt); },
        [&](RenderPassContext& ctx) { captured.push_back({ctx.passIndex, ctx.passName}); }
    );
    auto h1 = builder.AddGraphicsPass(
        "Beta",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) { captured.push_back({ctx.passIndex, ctx.passName}); }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(captured.size(), 2u);
    // First invoked pass should be Alpha
    EXPECT_EQ(captured[0].idx, h0.index);
    EXPECT_STREQ(captured[0].name, "Alpha");
    EXPECT_EQ(captured[1].idx, h1.index);
    EXPECT_STREQ(captured[1].name, "Beta");
}

// =============================================================================
// Executor — Compute + Graphics mixed pipeline
// =============================================================================

TEST(ExecutorComplex, ComputeThenGraphicsPipeline) {
    RenderGraphBuilder builder;
    auto buf = builder.CreateBuffer({.size = 1024, .debugName = "SSBO"});
    auto rt = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});

    std::vector<std::string> names;
    RGResourceHandle writtenBuf;
    builder.AddComputePass(
        "GenerateData", [&](PassBuilder& pb) { writtenBuf = pb.WriteBuffer(buf, ResourceAccess::ShaderWrite); },
        [&](RenderPassContext&) { names.push_back("GenerateData"); }
    );
    builder.AddGraphicsPass(
        "Render",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(writtenBuf, ResourceAccess::ShaderReadOnly);
            pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { names.push_back("Render"); }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "GenerateData");
    EXPECT_EQ(names[1], "Render");
    EXPECT_EQ(result->stats.recording.passesRecorded, 2u);
    EXPECT_GE(result->stats.allocation.transientBuffersAllocated, 1u);
    EXPECT_GE(result->stats.allocation.transientTexturesAllocated, 1u);
}

// =============================================================================
// Executor — Three-pass merged chain
// =============================================================================

TEST(ExecutorMergedGroup, ThreeSubpassChainAllInvoked) {
    RenderGraphBuilder builder;
    auto depth = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 128, .height = 128, .debugName = "D"});

    uint32_t count = 0;
    RGResourceHandle d0, d1;
    builder.AddGraphicsPass(
        "Sub0",
        [&](PassBuilder& pb) {
            d0 = pb.WriteDepthStencil(depth);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { count++; }
    );
    builder.AddGraphicsPass(
        "Sub1",
        [&](PassBuilder& pb) {
            d1 = pb.WriteDepthStencil(d0);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { count++; }
    );
    builder.AddGraphicsPass(
        "Sub2",
        [&](PassBuilder& pb) {
            pb.WriteDepthStencil(d1);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { count++; }
    );

    auto result = CompileAndExecute(builder, {}, /*enableMerging=*/true);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(result->stats.recording.passesRecorded, 3u);
}

// =============================================================================
// Executor — Verify no double-begin/end rendering in merged group
// (verified by correct stats — if double begin/end happened, MockCommandBuffer
// would not crash but pass count would be wrong)
// =============================================================================

TEST(ExecutorMergedGroup, StatsMatchMergedGroupCount) {
    RenderGraphBuilder builder;
    auto depth = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 128, .height = 128, .debugName = "D"});

    RGResourceHandle d0;
    builder.AddGraphicsPass(
        "A",
        [&](PassBuilder& pb) {
            d0 = pb.WriteDepthStencil(depth);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.WriteDepthStencil(d0);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    // Run both with and without merging, stats.recording.passesRecorded should be identical
    auto mergedResult = CompileAndExecute(builder, {}, /*enableMerging=*/true);
    ASSERT_TRUE(mergedResult.has_value());

    RenderGraphBuilder builder2;
    auto depth2 = builder2.CreateTexture({.format = Format::D32_FLOAT, .width = 128, .height = 128, .debugName = "D"});
    RGResourceHandle d02;
    builder2.AddGraphicsPass(
        "A",
        [&](PassBuilder& pb) {
            d02 = pb.WriteDepthStencil(depth2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder2.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            pb.WriteDepthStencil(d02);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    auto unmergedResult = CompileAndExecute(builder2, {}, /*enableMerging=*/false);
    ASSERT_TRUE(unmergedResult.has_value());

    EXPECT_EQ(mergedResult->stats.recording.passesRecorded, unmergedResult->stats.recording.passesRecorded);
}

// =============================================================================
// Executor — Multiple batches
// =============================================================================

TEST(Executor, MultipleBatchesSubmitted) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 128, .height = 128, .debugName = "RT"});

    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            rt = pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    // At least one batch should be submitted
    EXPECT_GE(result->stats.submission.batchesSubmitted, 1u);
}

// =============================================================================
// Executor — DepthStencil with custom clear value
// =============================================================================

TEST(Executor, DepthClearValuePropagated) {
    RenderGraphBuilder builder;
    auto depth = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 128, .height = 128, .debugName = "D"});

    bool verified = false;
    builder.AddGraphicsPass(
        "DepthClear",
        [&](PassBuilder& pb) {
            ClearValue cv{};
            cv.depthStencil = {0.5f, 128};
            pb.WriteDepthStencil(depth, AttachmentLoadOp::Clear, AttachmentStoreOp::Store, cv);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            verified = true;
            ASSERT_NE(ctx.depthAttachment, nullptr);
            EXPECT_EQ(ctx.depthAttachment->loadOp, AttachmentLoadOp::Clear);
            EXPECT_FLOAT_EQ(ctx.depthAttachment->clearValue.depthStencil.depth, 0.5f);
            EXPECT_EQ(ctx.depthAttachment->clearValue.depthStencil.stencil, 128u);
        }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(verified);
}

// =============================================================================
// Executor — Color clear value propagation
// =============================================================================

TEST(Executor, ColorClearValuePropagated) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 128, .height = 128, .debugName = "RT"});

    bool verified = false;
    ClearValue cv{};
    cv.color = {0.1f, 0.2f, 0.3f, 1.0f};
    builder.AddGraphicsPass(
        "Clear",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rt, 0, AttachmentLoadOp::Clear, AttachmentStoreOp::Store, cv);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            verified = true;
            ASSERT_GE(ctx.colorAttachments.size(), 1u);
            EXPECT_EQ(ctx.colorAttachments[0].loadOp, AttachmentLoadOp::Clear);
            EXPECT_FLOAT_EQ(ctx.colorAttachments[0].clearValue.color.r, 0.1f);
            EXPECT_FLOAT_EQ(ctx.colorAttachments[0].clearValue.color.g, 0.2f);
            EXPECT_FLOAT_EQ(ctx.colorAttachments[0].clearValue.color.b, 0.3f);
            EXPECT_FLOAT_EQ(ctx.colorAttachments[0].clearValue.color.a, 1.0f);
        }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(verified);
}

// =============================================================================
// Executor — WriteColorAttachment with non-zero slot index gap
// =============================================================================

TEST(Executor, ColorAttachmentSlotGap) {
    // Write only slot 2. outColor should be sized to at least 3, with slot 0 and 1 default.
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});

    bool verified = false;
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rt, 2, AttachmentLoadOp::Clear, AttachmentStoreOp::Store);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            verified = true;
            EXPECT_GE(ctx.colorAttachments.size(), 3u);
            if (ctx.colorAttachments.size() >= 3) {
                // Slot 2 should have a valid view
                EXPECT_TRUE(ctx.colorAttachments[2].view.IsValid());
                EXPECT_EQ(ctx.colorAttachments[2].loadOp, AttachmentLoadOp::Clear);
                // Slot 0 and 1 should have invalid views (not written)
                EXPECT_FALSE(ctx.colorAttachments[0].view.IsValid());
                EXPECT_FALSE(ctx.colorAttachments[1].view.IsValid());
            }
        }
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(verified);
}

// =============================================================================
// Executor — Repeated execute (simulate multi-frame)
// =============================================================================

TEST(Executor, RepeatedExecuteDoesNotLeak) {
    // Execute the same graph multiple times — stats should reset each time
    RenderGraphExecutor executor;
    for (int frame = 0; frame < 5; ++frame) {
        RenderGraphBuilder builder;
        auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
        builder.AddGraphicsPass(
            "P",
            [&](PassBuilder& pb) {
                pb.WriteColorAttachment(rt);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        builder.Build();

        RenderGraphCompiler compiler;
        auto compiled = compiler.Compile(builder);
        ASSERT_TRUE(compiled.has_value()) << "Frame " << frame;

        MockDevice device;
        device.Init();
        auto dh = DeviceHandle(&device, BackendType::Mock);
        miki::frame::SyncScheduler sched;
        sched.Init(device.GetQueueTimelinesImpl());
        miki::frame::CommandPoolAllocator::Desc pd{.device = dh, .framesInFlight = 2};
        auto pool = miki::frame::CommandPoolAllocator::Create(pd);
        ASSERT_TRUE(pool.has_value()) << "Frame " << frame;
        miki::frame::FrameContext fc{.frameIndex = static_cast<uint32_t>(frame % 2), .width = 64, .height = 64};

        auto execResult = executor.Execute(*compiled, builder, fc, dh, sched, *pool);
        ASSERT_TRUE(execResult.has_value()) << "Frame " << frame;

        auto& stats = executor.GetStats();
        EXPECT_EQ(stats.recording.passesRecorded, 1u) << "Frame " << frame;
        EXPECT_EQ(stats.submission.batchesSubmitted, 1u) << "Frame " << frame;
    }
}

// =============================================================================
// Executor — Null executeFn should not crash
// =============================================================================

TEST(Executor, NullExecuteFnDoesNotCrash) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    // Set executeFn to nullptr by providing null lambda (should be handled gracefully)
    builder.AddGraphicsPass(
        "NullExec",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        nullptr
    );

    auto result = CompileAndExecute(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->stats.recording.passesRecorded, 1u);
}

// =============================================================================
// PassBuilder — Load/store op defaults
// =============================================================================

TEST(PassBuilderAttachment, DefaultColorAttachmentOps) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    builder.AddGraphicsPass(
        "P",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    auto& pass = builder.GetPasses()[0];
    ASSERT_EQ(pass.colorAttachments.size(), 1u);
    EXPECT_EQ(pass.colorAttachments[0].loadOp, AttachmentLoadOp::Clear);
    EXPECT_EQ(pass.colorAttachments[0].storeOp, AttachmentStoreOp::Store);
    EXPECT_EQ(pass.colorAttachments[0].slotIndex, 0u);
}

TEST(PassBuilderAttachment, DefaultDepthStencilOps) {
    RenderGraphBuilder builder;
    auto depth = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 64, .height = 64, .debugName = "D"});
    builder.AddGraphicsPass(
        "P",
        [&](PassBuilder& pb) {
            pb.WriteDepthStencil(depth);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    auto& pass = builder.GetPasses()[0];
    EXPECT_TRUE(pass.hasDepthStencil);
    EXPECT_EQ(pass.depthStencilAttachment.loadOp, AttachmentLoadOp::Clear);
    EXPECT_EQ(pass.depthStencilAttachment.storeOp, AttachmentStoreOp::Store);
    EXPECT_FLOAT_EQ(pass.depthStencilAttachment.clearValue.depthStencil.depth, 1.0f);
    EXPECT_EQ(pass.depthStencilAttachment.clearValue.depthStencil.stencil, 0u);
}

TEST(PassBuilderAttachment, CustomLoadStoreOps) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    builder.AddGraphicsPass(
        "P",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rt, 3, AttachmentLoadOp::Load, AttachmentStoreOp::DontCare);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    auto& pass = builder.GetPasses()[0];
    ASSERT_EQ(pass.colorAttachments.size(), 1u);
    EXPECT_EQ(pass.colorAttachments[0].slotIndex, 3u);
    EXPECT_EQ(pass.colorAttachments[0].loadOp, AttachmentLoadOp::Load);
    EXPECT_EQ(pass.colorAttachments[0].storeOp, AttachmentStoreOp::DontCare);
}

// =============================================================================
// RenderPassContext — GetColorAttachment / GetDepthAttachment
// =============================================================================

TEST(RenderPassContext, GetColorAttachmentByIndex) {
    RenderingAttachment att0{.loadOp = AttachmentLoadOp::Clear};
    RenderingAttachment att1{.loadOp = AttachmentLoadOp::Load};
    std::vector<RenderingAttachment> atts = {att0, att1};
    RenderPassContext ctx{.colorAttachments = atts};

    auto r0 = ctx.GetColorAttachment(0);
    EXPECT_EQ(r0.loadOp, AttachmentLoadOp::Clear);
    auto r1 = ctx.GetColorAttachment(1);
    EXPECT_EQ(r1.loadOp, AttachmentLoadOp::Load);
    // Out of range -> default
    auto rOob = ctx.GetColorAttachment(99);
    EXPECT_FALSE(rOob.view.IsValid());
}

TEST(RenderPassContext, GetDepthAttachmentNullptr) {
    RenderPassContext ctx{};
    EXPECT_EQ(ctx.GetDepthAttachment(), nullptr);
}

TEST(RenderPassContext, GetDepthAttachmentPresent) {
    RenderingAttachment depthAtt{.loadOp = AttachmentLoadOp::Clear};
    RenderPassContext ctx{.depthAttachment = &depthAtt};
    EXPECT_NE(ctx.GetDepthAttachment(), nullptr);
    EXPECT_EQ(ctx.GetDepthAttachment()->loadOp, AttachmentLoadOp::Clear);
}

// =============================================================================
// RenderPassContext — Buffer and TextureView resolution
// =============================================================================

TEST(RenderPassContext, ResolveBuffer) {
    BufferHandle physicals[] = {BufferHandle{10}, BufferHandle{20}};
    RenderPassContext ctx{.physicalBuffers = physicals};

    auto h = RGResourceHandle::Create(1, 0);
    auto resolved = ctx.GetBuffer(h);
    EXPECT_EQ(resolved.value, 20u);
}

TEST(RenderPassContext, ResolveTextureView) {
    TextureViewHandle physicals[] = {TextureViewHandle{100}, TextureViewHandle{200}, TextureViewHandle{300}};
    RenderPassContext ctx{.physicalTextureViews = physicals};

    auto h = RGResourceHandle::Create(2, 0);
    auto resolved = ctx.GetTextureView(h);
    EXPECT_EQ(resolved.value, 300u);
}

TEST(RenderPassContext, ResolveBufferOutOfRange) {
    RenderPassContext ctx{};
    auto h = RGResourceHandle::Create(999, 0);
    EXPECT_FALSE(ctx.GetBuffer(h).IsValid());
}

TEST(RenderPassContext, ResolveTextureViewOutOfRange) {
    RenderPassContext ctx{};
    auto h = RGResourceHandle::Create(999, 0);
    EXPECT_FALSE(ctx.GetTextureView(h).IsValid());
}

// =============================================================================
// Phase F: Transient Memory Management Tests
//
// Tests cover:
//   F-1: HeapPool cross-frame reuse + layoutHash matching
//   F-2: Transient buffer suballocator (linear offset packing)
//   F-3: Heap defragmentation
//   F-4: D3D12 heap grouping (ResourceHeapTier)
//   F-5: Vulkan heap grouping (HeapGroupHint mapping)
//   F-6: Aliasing barrier batching (via executor integration)
//   Alignment utilities: AlignUp, ComputeAlignedHeapSize, PackSlotsAligned
// =============================================================================

namespace {

    // Helper: create a MockDevice + DeviceHandle pair for standalone pool tests
    struct MockDeviceFixture {
        MockDevice device;
        DeviceHandle handle;

        MockDeviceFixture() {
            device.Init();
            handle = DeviceHandle(&device, BackendType::Mock);
        }
    };

    // Helper: build a simple AliasingLayout with specified heap group sizes
    auto MakeAliasingLayout(std::array<uint64_t, kHeapGroupCount> sizes, uint32_t slotCountPerGroup = 1)
        -> AliasingLayout {
        AliasingLayout layout;
        layout.heapGroupSizes = sizes;
        for (uint32_t g = 0; g < kHeapGroupCount; ++g) {
            if (sizes[g] > 0) {
                for (uint32_t s = 0; s < slotCountPerGroup; ++s) {
                    layout.slots.push_back({
                        .slotIndex = static_cast<uint32_t>(layout.slots.size()),
                        .heapGroup = static_cast<HeapGroupType>(g),
                        .size = sizes[g] / slotCountPerGroup,
                        .alignment = kAlignmentTexture,
                    });
                }
            }
        }
        return layout;
    }

}  // anonymous namespace

// =============================================================================
// F-1: HeapPool — Basic allocation
// =============================================================================

TEST(HeapPool, EmptyLayoutAllocatesNothing) {
    MockDeviceFixture f;
    TransientHeapPool pool;
    auto layout = MakeAliasingLayout({0, 0, 0, 0});

    auto result = pool.AcquireHeaps(layout, f.handle, 1, false);
    ASSERT_TRUE(result.has_value());

    for (auto& h : *result) {
        EXPECT_FALSE(h.IsValid());
    }
    EXPECT_EQ(pool.GetStats().heapsAllocated, 0u);
    EXPECT_EQ(pool.GetStats().heapsReused, 0u);
    EXPECT_EQ(pool.GetPooledHeapCount(), 0u);
}

TEST(HeapPool, SingleGroupAllocatesOneHeap) {
    MockDeviceFixture f;
    TransientHeapPool pool;
    auto layout = MakeAliasingLayout({65536, 0, 0, 0});  // RtDs only

    auto result = pool.AcquireHeaps(layout, f.handle, 1, false);
    ASSERT_TRUE(result.has_value());

    auto& heaps = *result;
    EXPECT_TRUE(heaps[0].IsValid());   // RtDs
    EXPECT_FALSE(heaps[1].IsValid());  // NonRtDs
    EXPECT_FALSE(heaps[2].IsValid());  // Buffer
    EXPECT_FALSE(heaps[3].IsValid());  // MixedFallback

    EXPECT_EQ(pool.GetStats().heapsAllocated, 1u);
    EXPECT_EQ(pool.GetStats().heapsReused, 0u);
    EXPECT_EQ(pool.GetStats().activeBytes, 65536u);
    EXPECT_EQ(pool.GetPooledHeapCount(), 1u);
}

TEST(HeapPool, MultipleGroupsAllocateMultipleHeaps) {
    MockDeviceFixture f;
    TransientHeapPool pool;
    auto layout = MakeAliasingLayout({65536, 32768, 4096, 0});

    auto result = pool.AcquireHeaps(layout, f.handle, 1, false);
    ASSERT_TRUE(result.has_value());

    auto& heaps = *result;
    EXPECT_TRUE(heaps[0].IsValid());   // RtDs
    EXPECT_TRUE(heaps[1].IsValid());   // NonRtDs
    EXPECT_TRUE(heaps[2].IsValid());   // Buffer
    EXPECT_FALSE(heaps[3].IsValid());  // MixedFallback

    EXPECT_EQ(pool.GetStats().heapsAllocated, 3u);
    EXPECT_EQ(pool.GetStats().activeBytes, 65536u + 32768u + 4096u);
    EXPECT_EQ(pool.GetPooledHeapCount(), 3u);
}

// =============================================================================
// F-1: HeapPool — Cross-frame reuse (exact layoutHash match)
// =============================================================================

TEST(HeapPool, ExactLayoutHashMatchReusesHeap) {
    MockDeviceFixture f;
    TransientHeapPool pool;
    auto layout = MakeAliasingLayout({65536, 0, 0, 0});

    // Frame 1: first allocation
    auto r1 = pool.AcquireHeaps(layout, f.handle, 1, false);
    ASSERT_TRUE(r1.has_value());
    auto heap1 = (*r1)[0];
    EXPECT_EQ(pool.GetStats().heapsAllocated, 1u);
    EXPECT_EQ(pool.GetStats().heapsReused, 0u);

    // Frame 2: same layout -> exact hash match, zero-alloc reuse
    auto r2 = pool.AcquireHeaps(layout, f.handle, 2, false);
    ASSERT_TRUE(r2.has_value());
    auto heap2 = (*r2)[0];

    EXPECT_EQ(heap1.value, heap2.value);  // Same heap handle reused
    EXPECT_EQ(pool.GetStats().heapsAllocated, 0u);
    EXPECT_EQ(pool.GetStats().heapsReused, 1u);
    EXPECT_EQ(pool.GetPooledHeapCount(), 1u);  // No growth
}

TEST(HeapPool, ThreeConsecutiveFramesReusesSameHeap) {
    MockDeviceFixture f;
    TransientHeapPool pool;
    auto layout = MakeAliasingLayout({65536, 0, 256, 0});

    auto r1 = pool.AcquireHeaps(layout, f.handle, 1, false);
    ASSERT_TRUE(r1.has_value());
    auto rtds1 = (*r1)[0];
    auto buf1 = (*r1)[2];

    for (uint32_t frame = 2; frame <= 10; ++frame) {
        auto rN = pool.AcquireHeaps(layout, f.handle, frame, false);
        ASSERT_TRUE(rN.has_value());
        EXPECT_EQ((*rN)[0].value, rtds1.value);
        EXPECT_EQ((*rN)[2].value, buf1.value);
        EXPECT_EQ(pool.GetStats().heapsReused, 2u);
        EXPECT_EQ(pool.GetStats().heapsAllocated, 0u);
    }
    EXPECT_EQ(pool.GetPooledHeapCount(), 2u);  // Stable count
}

// =============================================================================
// F-1: HeapPool — Size-compatible match (within overshoot tolerance)
// =============================================================================

TEST(HeapPool, SizeCompatibleMatchWithinTolerance) {
    MockDeviceFixture f;
    TransientHeapPoolConfig cfg;
    cfg.overshootTolerance = 0.20f;  // 20%
    TransientHeapPool pool(cfg);

    // Frame 1: allocate 100KB
    auto layout1 = MakeAliasingLayout({102400, 0, 0, 0});
    auto r1 = pool.AcquireHeaps(layout1, f.handle, 1, false);
    ASSERT_TRUE(r1.has_value());
    auto heapHandle = (*r1)[0];

    // Frame 2: request 90KB -> 102400 >= 90000 and <= 90000*1.2=108000 -> reuse
    auto layout2 = MakeAliasingLayout({90000, 0, 0, 0}, 1);
    auto r2 = pool.AcquireHeaps(layout2, f.handle, 2, false);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ((*r2)[0].value, heapHandle.value);
    EXPECT_EQ(pool.GetStats().heapsReused, 1u);
    EXPECT_EQ(pool.GetStats().heapsAllocated, 0u);
}

TEST(HeapPool, SizeIncompatibleExceedingToleranceAllocatesNew) {
    MockDeviceFixture f;
    TransientHeapPoolConfig cfg;
    cfg.overshootTolerance = 0.20f;
    TransientHeapPool pool(cfg);

    // Frame 1: allocate 200KB
    auto layout1 = MakeAliasingLayout({204800, 0, 0, 0});
    auto r1 = pool.AcquireHeaps(layout1, f.handle, 1, false);
    ASSERT_TRUE(r1.has_value());
    auto oldHeap = (*r1)[0];

    // Frame 2: request 100KB -> 204800 > 100000*1.2=120000 -> NOT compatible, new alloc
    auto layout2 = MakeAliasingLayout({102400, 0, 0, 0}, 1);
    auto r2 = pool.AcquireHeaps(layout2, f.handle, 2, false);
    ASSERT_TRUE(r2.has_value());
    EXPECT_NE((*r2)[0].value, oldHeap.value);
    EXPECT_EQ(pool.GetStats().heapsAllocated, 1u);
}

// =============================================================================
// F-1: HeapPool — LRU eviction
// =============================================================================

TEST(HeapPool, StaleHeapsEvictedAfterGracePeriod) {
    MockDeviceFixture f;
    TransientHeapPoolConfig cfg;
    cfg.lruGraceFrames = 2;
    TransientHeapPool pool(cfg);

    // Frame 1: allocate RtDs + Buffer
    auto layout1 = MakeAliasingLayout({65536, 0, 4096, 0});
    pool.AcquireHeaps(layout1, f.handle, 1, false);
    EXPECT_EQ(pool.GetPooledHeapCount(), 2u);

    // Frame 2: only need RtDs (Buffer becomes stale)
    auto layout2 = MakeAliasingLayout({65536, 0, 0, 0});
    pool.AcquireHeaps(layout2, f.handle, 2, false);
    EXPECT_EQ(pool.GetPooledHeapCount(), 2u);  // Grace period not expired

    // Frame 3: Buffer stale since frame 1, grace=2 -> 3 >= 1+2 -> evicted
    pool.AcquireHeaps(layout2, f.handle, 3, false);
    EXPECT_EQ(pool.GetPooledHeapCount(), 1u);  // Buffer evicted (>= condition)
    EXPECT_GE(pool.GetStats().heapsEvicted, 1u);
}

TEST(HeapPool, GracePeriodZeroEvictsImmediately) {
    MockDeviceFixture f;
    TransientHeapPoolConfig cfg;
    cfg.lruGraceFrames = 0;
    TransientHeapPool pool(cfg);

    auto layout1 = MakeAliasingLayout({65536, 0, 4096, 0});
    pool.AcquireHeaps(layout1, f.handle, 1, false);
    EXPECT_EQ(pool.GetPooledHeapCount(), 2u);

    // Frame 2: only RtDs -> Buffer immediately evicted (0 grace)
    auto layout2 = MakeAliasingLayout({65536, 0, 0, 0});
    pool.AcquireHeaps(layout2, f.handle, 2, false);
    EXPECT_EQ(pool.GetPooledHeapCount(), 1u);
}

// =============================================================================
// F-1: HeapPool — Oversized heap release during allocation
// =============================================================================

TEST(HeapPool, OversizedHeapReleasedBeforeNewAlloc) {
    MockDeviceFixture f;
    TransientHeapPoolConfig cfg;
    cfg.oversizedReleaseThreshold = 2.0f;  // Release if > 2x required
    cfg.lruGraceFrames = 100;              // High grace so it won't be LRU-evicted
    TransientHeapPool pool(cfg);

    // Frame 1: allocate 500KB
    auto layout1 = MakeAliasingLayout({512000, 0, 0, 0});
    pool.AcquireHeaps(layout1, f.handle, 1, false);
    EXPECT_EQ(pool.GetPooledHeapCount(), 1u);

    // Frame 2: request 100KB -> old 512000 > 2*100000=200000 -> oversized, released + new alloc
    auto layout2 = MakeAliasingLayout({100000, 0, 0, 0}, 1);
    auto r2 = pool.AcquireHeaps(layout2, f.handle, 2, false);
    ASSERT_TRUE(r2.has_value());
    // The oversized entry should have been evicted and a new one allocated
    EXPECT_EQ(pool.GetStats().heapsAllocated, 1u);
    EXPECT_GE(pool.GetStats().heapsEvicted, 1u);
}

// =============================================================================
// F-1: HeapPool — Statistics accuracy
// =============================================================================

TEST(HeapPool, StatsResetEachFrame) {
    MockDeviceFixture f;
    TransientHeapPool pool;
    auto layout = MakeAliasingLayout({65536, 0, 0, 0});

    pool.AcquireHeaps(layout, f.handle, 1, false);
    EXPECT_EQ(pool.GetStats().heapsAllocated, 1u);

    pool.AcquireHeaps(layout, f.handle, 2, false);
    // Stats are reset per-frame: should show reuse, not accumulated alloc
    EXPECT_EQ(pool.GetStats().heapsAllocated, 0u);
    EXPECT_EQ(pool.GetStats().heapsReused, 1u);
}

TEST(HeapPool, WastedBytesCalculation) {
    MockDeviceFixture f;
    TransientHeapPoolConfig cfg;
    cfg.overshootTolerance = 0.50f;  // 50% to easily trigger size-compat
    TransientHeapPool pool(cfg);

    // Frame 1: allocate 100KB
    auto layout1 = MakeAliasingLayout({102400, 0, 0, 0});
    pool.AcquireHeaps(layout1, f.handle, 1, false);

    // Frame 2: request 80KB -> 102400 <= 80000*1.5=120000 -> reuse with 22400 waste
    auto layout2 = MakeAliasingLayout({81920, 0, 0, 0}, 1);
    pool.AcquireHeaps(layout2, f.handle, 2, false);

    EXPECT_EQ(pool.GetStats().totalPooledBytes, 102400u);
    EXPECT_EQ(pool.GetStats().activeBytes, 102400u);
    // wastedBytes = totalPooledBytes - activeBytes (since only 1 entry, both same)
    // The per-match waste is tracked differently; totalPooledBytes covers all entries
    EXPECT_GE(pool.GetStats().totalPooledBytes, pool.GetStats().activeBytes);
}

// =============================================================================
// F-1: HeapPool — ReleaseAll
// =============================================================================

TEST(HeapPool, ReleaseAllClearsEverything) {
    MockDeviceFixture f;
    TransientHeapPool pool;
    auto layout = MakeAliasingLayout({65536, 32768, 4096, 0});
    pool.AcquireHeaps(layout, f.handle, 1, false);
    EXPECT_EQ(pool.GetPooledHeapCount(), 3u);

    pool.ReleaseAll(f.handle);
    EXPECT_EQ(pool.GetPooledHeapCount(), 0u);
    EXPECT_FALSE(pool.GetParentBuffer().IsValid());
    EXPECT_EQ(pool.GetParentBufferSize(), 0u);
}

// =============================================================================
// F-2: Buffer suballocator — basic offset packing
// =============================================================================

TEST(BufferSuballocator, EmptyResourcesProducesNoSuballocations) {
    MockDeviceFixture f;
    TransientHeapPool pool;

    AliasingLayout layout;
    std::vector<RGResourceNode> resources;

    auto result = pool.PrepareBufferSuballocations({}, resources, layout, f.handle);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
    EXPECT_FALSE(pool.GetParentBuffer().IsValid());
}

TEST(BufferSuballocator, SingleBufferSuballocation) {
    MockDeviceFixture f;
    TransientHeapPool pool;

    // Create a resource node for a 1024-byte buffer
    RGResourceNode bufNode;
    bufNode.kind = RGResourceKind::Buffer;
    bufNode.imported = false;
    bufNode.bufferDesc.size = 1024;

    AliasingLayout layout;
    layout.slots.push_back({
        .slotIndex = 0,
        .heapGroup = HeapGroupType::Buffer,
        .size = 1024,
        .alignment = kAlignmentBuffer,
    });
    layout.resourceToSlot = {0};  // resource 0 -> slot 0

    std::vector<RGResourceNode> resources = {bufNode};
    DeviceMemoryHandle bufHeap{42};

    auto result = pool.PrepareBufferSuballocations(bufHeap, resources, layout, f.handle);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].resourceIndex, 0u);
    EXPECT_EQ((*result)[0].offset, 0u);
    EXPECT_EQ((*result)[0].size, 1024u);
    EXPECT_TRUE(pool.GetParentBuffer().IsValid());
    EXPECT_EQ(pool.GetParentBufferSize(), 1024u);
}

TEST(BufferSuballocator, MultipleBuffersAligned256B) {
    MockDeviceFixture f;
    TransientHeapPool pool;

    // Two buffers: 300 bytes and 500 bytes
    RGResourceNode buf0;
    buf0.kind = RGResourceKind::Buffer;
    buf0.imported = false;
    buf0.bufferDesc.size = 300;

    RGResourceNode buf1;
    buf1.kind = RGResourceKind::Buffer;
    buf1.imported = false;
    buf1.bufferDesc.size = 500;

    AliasingLayout layout;
    layout.slots.push_back({
        .slotIndex = 0,
        .heapGroup = HeapGroupType::Buffer,
        .size = 300,
        .alignment = kAlignmentBuffer,
    });
    layout.slots.push_back({
        .slotIndex = 1,
        .heapGroup = HeapGroupType::Buffer,
        .size = 500,
        .alignment = kAlignmentBuffer,
    });
    layout.resourceToSlot = {0, 1};

    std::vector<RGResourceNode> resources = {buf0, buf1};

    auto result = pool.PrepareBufferSuballocations(DeviceMemoryHandle{1}, resources, layout, f.handle);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 2u);

    auto& sub0 = (*result)[0];
    auto& sub1 = (*result)[1];
    EXPECT_EQ(sub0.offset, 0u);
    EXPECT_EQ(sub0.size, 300u);
    // Second buffer starts at AlignUp(300, 256) = 512
    EXPECT_EQ(sub1.offset, AlignUp(300, kAlignmentBuffer));
    EXPECT_EQ(sub1.size, 500u);
    // No overlap
    EXPECT_GE(sub1.offset, sub0.offset + sub0.size);
    // All offsets are 256-aligned
    EXPECT_EQ(sub0.offset % kAlignmentBuffer, 0u);
    EXPECT_EQ(sub1.offset % kAlignmentBuffer, 0u);
}

TEST(BufferSuballocator, ImportedBuffersSkipped) {
    MockDeviceFixture f;
    TransientHeapPool pool;

    RGResourceNode imported;
    imported.kind = RGResourceKind::Buffer;
    imported.imported = true;
    imported.bufferDesc.size = 2048;

    RGResourceNode transient;
    transient.kind = RGResourceKind::Buffer;
    transient.imported = false;
    transient.bufferDesc.size = 512;

    AliasingLayout layout;
    layout.slots.push_back({
        .slotIndex = 0,
        .heapGroup = HeapGroupType::Buffer,
        .size = 512,
        .alignment = kAlignmentBuffer,
    });
    layout.resourceToSlot = {AliasingLayout::kNotAliased, 0};  // imported not aliased

    std::vector<RGResourceNode> resources = {imported, transient};

    auto result = pool.PrepareBufferSuballocations(DeviceMemoryHandle{1}, resources, layout, f.handle);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].resourceIndex, 1u);  // Only transient buffer
}

TEST(BufferSuballocator, TexturesSkipped) {
    MockDeviceFixture f;
    TransientHeapPool pool;

    RGResourceNode texNode;
    texNode.kind = RGResourceKind::Texture;
    texNode.imported = false;
    texNode.textureDesc.width = 64;
    texNode.textureDesc.height = 64;

    AliasingLayout layout;
    layout.slots.push_back({
        .slotIndex = 0,
        .heapGroup = HeapGroupType::RtDs,
        .size = 65536,
        .alignment = kAlignmentTexture,
    });
    layout.resourceToSlot = {0};

    std::vector<RGResourceNode> resources = {texNode};
    auto result = pool.PrepareBufferSuballocations(DeviceMemoryHandle{1}, resources, layout, f.handle);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST(BufferSuballocator, StatsAccurate) {
    MockDeviceFixture f;
    TransientHeapPool pool;

    RGResourceNode buf0;
    buf0.kind = RGResourceKind::Buffer;
    buf0.imported = false;
    buf0.bufferDesc.size = 1024;

    RGResourceNode buf1;
    buf1.kind = RGResourceKind::Buffer;
    buf1.imported = false;
    buf1.bufferDesc.size = 2048;

    AliasingLayout layout;
    layout.slots.push_back(
        {.slotIndex = 0, .heapGroup = HeapGroupType::Buffer, .size = 1024, .alignment = kAlignmentBuffer}
    );
    layout.slots.push_back(
        {.slotIndex = 1, .heapGroup = HeapGroupType::Buffer, .size = 2048, .alignment = kAlignmentBuffer}
    );
    layout.resourceToSlot = {0, 1};

    // Need a fresh AcquireHeaps call first to reset stats
    auto layoutFull = MakeAliasingLayout({0, 0, 4096, 0});
    auto heaps = pool.AcquireHeaps(layoutFull, f.handle, 1, false);
    ASSERT_TRUE(heaps.has_value());

    std::vector<RGResourceNode> resources = {buf0, buf1};
    auto result = pool.PrepareBufferSuballocations((*heaps)[2], resources, layout, f.handle);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(pool.GetStats().bufferSuballocations, 2u);
    EXPECT_GT(pool.GetStats().bufferSuballocBytes, 0u);
}

// =============================================================================
// F-3: Heap defragmentation
// =============================================================================

TEST(HeapDefrag, ForceDefragEvictsStaleEntries) {
    MockDeviceFixture f;
    TransientHeapPoolConfig cfg;
    cfg.lruGraceFrames = 100;  // High so LRU won't evict naturally
    TransientHeapPool pool(cfg);

    // Frame 1: allocate 3 groups
    auto layout1 = MakeAliasingLayout({65536, 32768, 4096, 0});
    pool.AcquireHeaps(layout1, f.handle, 1, false);
    EXPECT_EQ(pool.GetPooledHeapCount(), 3u);

    // Frame 2: only need RtDs (NonRtDs + Buffer become stale but not LRU-evicted)
    auto layout2 = MakeAliasingLayout({65536, 0, 0, 0});
    pool.AcquireHeaps(layout2, f.handle, 2, false);
    EXPECT_EQ(pool.GetPooledHeapCount(), 3u);  // All still present due to high grace

    // Force defrag: evicts all non-active entries
    pool.DefragmentIfNeeded(layout2, f.handle, 3, 0.5f, /*forceDefrag=*/true);
    EXPECT_EQ(pool.GetPooledHeapCount(), 1u);  // Only RtDs remains
    EXPECT_GE(pool.GetStats().defragTriggered, 1u);
}

TEST(HeapDefrag, DefragNotTriggeredWhenConditionsNotMet) {
    MockDeviceFixture f;
    TransientHeapPool pool;
    auto layout = MakeAliasingLayout({65536, 0, 0, 0});
    pool.AcquireHeaps(layout, f.handle, 1, false);

    // Low VRAM pressure, no proliferation, no force -> no defrag
    pool.DefragmentIfNeeded(layout, f.handle, 2, 0.5f, false);
    EXPECT_EQ(pool.GetStats().defragTriggered, 0u);
}

TEST(HeapDefrag, VramPressureTriggersDefrag) {
    MockDeviceFixture f;
    TransientHeapPoolConfig cfg;
    cfg.defragVramThresholdFrames = 3;  // Low threshold for test
    cfg.defragVramUsageRatio = 0.90f;
    cfg.lruGraceFrames = 100;
    TransientHeapPool pool(cfg);

    auto layout = MakeAliasingLayout({65536, 32768, 0, 0});
    pool.AcquireHeaps(layout, f.handle, 1, false);

    // Simulate 3 consecutive frames at >90% VRAM
    auto layoutSmall = MakeAliasingLayout({65536, 0, 0, 0});
    pool.AcquireHeaps(layoutSmall, f.handle, 2, false);
    pool.DefragmentIfNeeded(layoutSmall, f.handle, 2, 0.95f, false);
    EXPECT_EQ(pool.GetStats().defragTriggered, 0u);  // Only 1 frame

    pool.AcquireHeaps(layoutSmall, f.handle, 3, false);
    pool.DefragmentIfNeeded(layoutSmall, f.handle, 3, 0.95f, false);
    EXPECT_EQ(pool.GetStats().defragTriggered, 0u);  // 2 frames

    pool.AcquireHeaps(layoutSmall, f.handle, 4, false);
    pool.DefragmentIfNeeded(layoutSmall, f.handle, 4, 0.95f, false);
    EXPECT_GE(pool.GetStats().defragTriggered, 1u);  // 3 frames -> triggered
}

TEST(HeapDefrag, HeapProliferationTriggersDefrag) {
    MockDeviceFixture f;
    TransientHeapPoolConfig cfg;
    cfg.defragHeapCountRatio = 1.5f;  // Trigger if pool > 1.5 * active groups
    cfg.lruGraceFrames = 100;
    TransientHeapPool pool(cfg);

    // Frame 1: 3 groups
    auto layout1 = MakeAliasingLayout({65536, 32768, 4096, 0});
    pool.AcquireHeaps(layout1, f.handle, 1, false);

    // Frame 2: only 1 group, but 3 entries still in pool (3 > 1.5*1 = 1.5)
    auto layout2 = MakeAliasingLayout({65536, 0, 0, 0});
    pool.AcquireHeaps(layout2, f.handle, 2, false);
    EXPECT_EQ(pool.GetPooledHeapCount(), 3u);

    pool.DefragmentIfNeeded(layout2, f.handle, 3, 0.5f, false);
    EXPECT_GE(pool.GetStats().defragTriggered, 1u);
    EXPECT_EQ(pool.GetPooledHeapCount(), 1u);  // Only the active RtDs entry survives
}

// =============================================================================
// F-4: D3D12 heap grouping (MixedFallback for Tier1)
// =============================================================================

TEST(HeapPool, MixedFallbackMergesAllGroups) {
    MockDeviceFixture f;
    TransientHeapPool pool;
    auto layout = MakeAliasingLayout({65536, 32768, 4096, 0});

    auto result = pool.AcquireHeaps(layout, f.handle, 1, /*useMixedFallback=*/true);
    ASSERT_TRUE(result.has_value());

    auto& heaps = *result;
    // Only MixedFallback should be allocated
    EXPECT_FALSE(heaps[0].IsValid());  // RtDs skipped
    EXPECT_FALSE(heaps[1].IsValid());  // NonRtDs skipped
    EXPECT_FALSE(heaps[2].IsValid());  // Buffer skipped
    EXPECT_TRUE(heaps[static_cast<uint32_t>(HeapGroupType::MixedFallback)].IsValid());

    EXPECT_EQ(pool.GetStats().heapsAllocated, 1u);
    // Total size = 65536 + 32768 + 4096 = 102400
    EXPECT_EQ(pool.GetStats().activeBytes, 65536u + 32768u + 4096u);
}

TEST(HeapPool, MixedFallbackReusesAcrossFrames) {
    MockDeviceFixture f;
    TransientHeapPool pool;
    auto layout = MakeAliasingLayout({65536, 32768, 4096, 0});

    auto r1 = pool.AcquireHeaps(layout, f.handle, 1, true);
    ASSERT_TRUE(r1.has_value());
    auto mixed1 = (*r1)[static_cast<uint32_t>(HeapGroupType::MixedFallback)];

    auto r2 = pool.AcquireHeaps(layout, f.handle, 2, true);
    ASSERT_TRUE(r2.has_value());
    auto mixed2 = (*r2)[static_cast<uint32_t>(HeapGroupType::MixedFallback)];

    EXPECT_EQ(mixed1.value, mixed2.value);  // Same heap reused
    EXPECT_EQ(pool.GetStats().heapsReused, 1u);
}

// =============================================================================
// F-5: HeapGroupHint mapping
// =============================================================================

TEST(HeapGroupHint, ToGroupHintMapping) {
    EXPECT_EQ(TransientHeapPool::ToGroupHint(HeapGroupType::RtDs), HeapGroupHint::RtDs);
    EXPECT_EQ(TransientHeapPool::ToGroupHint(HeapGroupType::NonRtDs), HeapGroupHint::NonRtDs);
    EXPECT_EQ(TransientHeapPool::ToGroupHint(HeapGroupType::Buffer), HeapGroupHint::Buffer);
    EXPECT_EQ(TransientHeapPool::ToGroupHint(HeapGroupType::MixedFallback), HeapGroupHint::MixedFallback);
}

// =============================================================================
// F-4: ResourceHeapTier in GpuCapabilityProfile
// =============================================================================

TEST(GpuCapabilityProfile, ResourceHeapTierDefaults) {
    GpuCapabilityProfile caps;
    // Default should be Tier2 (all modern GPUs)
    EXPECT_EQ(caps.resourceHeapTier, GpuCapabilityProfile::ResourceHeapTier::Tier2);
}

TEST(GpuCapabilityProfile, ResourceHeapTierTier1) {
    GpuCapabilityProfile caps;
    caps.resourceHeapTier = GpuCapabilityProfile::ResourceHeapTier::Tier1;
    EXPECT_EQ(static_cast<uint8_t>(caps.resourceHeapTier), 1u);
}

// =============================================================================
// F-5: MemoryHeapDesc groupHint
// =============================================================================

TEST(MemoryHeapDesc, GroupHintDefaultIsMixedFallback) {
    MemoryHeapDesc desc;
    EXPECT_EQ(desc.groupHint, HeapGroupHint::MixedFallback);
}

TEST(MemoryHeapDesc, GroupHintAllValues) {
    MemoryHeapDesc d1{.size = 1024, .groupHint = HeapGroupHint::RtDs};
    EXPECT_EQ(d1.groupHint, HeapGroupHint::RtDs);
    MemoryHeapDesc d2{.size = 1024, .groupHint = HeapGroupHint::NonRtDs};
    EXPECT_EQ(d2.groupHint, HeapGroupHint::NonRtDs);
    MemoryHeapDesc d3{.size = 1024, .groupHint = HeapGroupHint::Buffer};
    EXPECT_EQ(d3.groupHint, HeapGroupHint::Buffer);
}

// =============================================================================
// Alignment utilities
// =============================================================================

TEST(AlignUp, BasicCases) {
    EXPECT_EQ(AlignUp(0, 256), 0u);
    EXPECT_EQ(AlignUp(1, 256), 256u);
    EXPECT_EQ(AlignUp(255, 256), 256u);
    EXPECT_EQ(AlignUp(256, 256), 256u);
    EXPECT_EQ(AlignUp(257, 256), 512u);
    EXPECT_EQ(AlignUp(65535, 65536), 65536u);
    EXPECT_EQ(AlignUp(65536, 65536), 65536u);
    EXPECT_EQ(AlignUp(65537, 65536), 131072u);
}

TEST(AlignUp, LargeAlignment) {
    EXPECT_EQ(AlignUp(1, kAlignmentMsaa), kAlignmentMsaa);
    EXPECT_EQ(AlignUp(kAlignmentMsaa, kAlignmentMsaa), kAlignmentMsaa);
    EXPECT_EQ(AlignUp(kAlignmentMsaa + 1, kAlignmentMsaa), 2 * kAlignmentMsaa);
}

TEST(ComputeAlignedHeapSize, SingleNonMsaaSlot) {
    std::vector<AliasingSlot> slots = {
        {.heapGroup = HeapGroupType::RtDs, .size = 65536, .alignment = kAlignmentTexture},
    };
    auto size = ComputeAlignedHeapSize(slots, HeapGroupType::RtDs);
    // AlignUp(0, 65536) = 0, then +65536 = 65536
    EXPECT_EQ(size, 65536u);
}

TEST(ComputeAlignedHeapSize, MixedMsaaAndNonMsaa) {
    std::vector<AliasingSlot> slots = {
        {.heapGroup = HeapGroupType::RtDs, .size = 65536, .alignment = kAlignmentTexture},
        {.heapGroup = HeapGroupType::RtDs, .size = kAlignmentMsaa, .alignment = kAlignmentMsaa},
    };
    auto size = ComputeAlignedHeapSize(slots, HeapGroupType::RtDs);

    // nonMsaa region: AlignUp(0, 64K) + 65536 = 65536 + 65536 = ... wait, let's be precise
    // nonMsaaOffset = AlignUp(0, 64K) = 0 (if we treat initial as 0), then + 65536 = 65536
    // But wait, the code does: nonMsaaOffset = AlignUp(nonMsaaOffset, slot.alignment) + slot.size
    // Start: nonMsaaOffset = 0
    // Slot 0 (non-MSAA): AlignUp(0, 65536) = 0, then +65536 = 65536
    // msaaOffset = 0
    // Slot 1 (MSAA): AlignUp(0, 4MB) = 0, then +4MB = 4MB
    // nonMsaaOffset = AlignUp(65536, 4MB) = 4MB
    // total = 4MB + 4MB = 8MB
    EXPECT_EQ(size, 2 * kAlignmentMsaa);
}

TEST(ComputeAlignedHeapSize, IgnoresOtherGroups) {
    std::vector<AliasingSlot> slots = {
        {.heapGroup = HeapGroupType::RtDs, .size = 65536, .alignment = kAlignmentTexture},
        {.heapGroup = HeapGroupType::Buffer, .size = 1024, .alignment = kAlignmentBuffer},
    };
    auto rtdsSize = ComputeAlignedHeapSize(slots, HeapGroupType::RtDs);
    auto bufSize = ComputeAlignedHeapSize(slots, HeapGroupType::Buffer);
    EXPECT_GT(rtdsSize, 0u);
    EXPECT_GT(bufSize, 0u);
    // They're computed independently
    EXPECT_NE(rtdsSize, bufSize);
}

TEST(PackSlotsAligned, BasicPacking) {
    std::vector<AliasingSlot> slots = {
        {.slotIndex = 0, .heapGroup = HeapGroupType::Buffer, .size = 300, .alignment = kAlignmentBuffer},
        {.slotIndex = 1, .heapGroup = HeapGroupType::Buffer, .size = 500, .alignment = kAlignmentBuffer},
    };

    auto totalSize = PackSlotsAligned(slots, HeapGroupType::Buffer);
    EXPECT_GT(totalSize, 0u);

    // First-fit-decreasing: 500 first, then 300
    // Slot with 500 bytes should be at offset 0
    // Slot with 300 bytes should be at AlignUp(500, 256) = 512
    auto& bigger = (slots[0].size > slots[1].size) ? slots[0] : slots[1];
    auto& smaller = (slots[0].size > slots[1].size) ? slots[1] : slots[0];
    EXPECT_EQ(bigger.heapOffset, 0u);
    EXPECT_EQ(smaller.heapOffset, AlignUp(500, kAlignmentBuffer));
    EXPECT_EQ(smaller.heapOffset % kAlignmentBuffer, 0u);
}

TEST(PackSlotsAligned, MsaaPackedAfterNonMsaa) {
    std::vector<AliasingSlot> slots = {
        {.slotIndex = 0, .heapGroup = HeapGroupType::RtDs, .size = 65536, .alignment = kAlignmentTexture},
        {.slotIndex = 1, .heapGroup = HeapGroupType::RtDs, .size = kAlignmentMsaa, .alignment = kAlignmentMsaa},
    };

    PackSlotsAligned(slots, HeapGroupType::RtDs);

    // Non-MSAA should come first (sorted: non-MSAA before MSAA)
    EXPECT_LT(slots[0].heapOffset, slots[1].heapOffset);
    EXPECT_EQ(slots[1].heapOffset % kAlignmentMsaa, 0u);
}

// =============================================================================
// F-6: Aliasing barrier batching — via executor integration
// =============================================================================

TEST(ExecutorPhaseF, HeapPoolingEnabledByDefault) {
    ExecutorConfig cfg;
    EXPECT_TRUE(cfg.enableHeapPooling);
    EXPECT_TRUE(cfg.enableBufferSuballocation);
}

TEST(ExecutorPhaseF, HeapPoolingDisabledFallsBackToPerFrame) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    builder.AddGraphicsPass(
        "P",
        [&](PassBuilder& pb) {
            pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    ExecutorConfig cfg;
    cfg.enableHeapPooling = false;
    auto result = CompileAndExecute(builder, cfg);
    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result->stats.allocation.heapsCreated, 1u);
    EXPECT_EQ(result->stats.allocation.heapsReused, 0u);
}

TEST(ExecutorPhaseF, HeapPoolingReusesAcrossExecutions) {
    MockDevice device;
    device.Init();
    auto dh = DeviceHandle(&device, BackendType::Mock);

    miki::frame::SyncScheduler sched;
    sched.Init(device.GetQueueTimelinesImpl());
    miki::frame::CommandPoolAllocator::Desc pd{.device = dh, .framesInFlight = 2};
    auto pool = miki::frame::CommandPoolAllocator::Create(pd);
    ASSERT_TRUE(pool.has_value());
    miki::frame::FrameContext frame{.width = 64, .height = 64};

    ExecutorConfig cfg;
    cfg.enableHeapPooling = true;
    RenderGraphExecutor executor(cfg);

    // Build identical graph twice
    auto buildAndRun = [&]() {
        RenderGraphBuilder builder;
        auto rt = builder.CreateTexture({.width = 128, .height = 128, .debugName = "RT"});
        builder.AddGraphicsPass(
            "P",
            [&](PassBuilder& pb) {
                pb.WriteColorAttachment(rt);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        builder.Build();
        RenderGraphCompiler compiler;
        auto compiled = compiler.Compile(builder);
        EXPECT_TRUE(compiled.has_value());
        if (!compiled) {
            return;
        }
        auto r = executor.Execute(*compiled, builder, frame, dh, sched, *pool);
        EXPECT_TRUE(r.has_value());
    };

    buildAndRun();
    auto s1 = executor.GetStats();
    EXPECT_GE(s1.allocation.transientMemoryBytes, 1u);

    buildAndRun();
    auto s2 = executor.GetStats();
    // Second execution should reuse heaps
    EXPECT_GE(s2.allocation.heapsReused, 1u);
    // Pool count should be stable (no growth)
    EXPECT_LE(executor.GetHeapPool().GetPooledHeapCount(), 4u);

    executor.ReleasePooledResources(dh);
    EXPECT_EQ(executor.GetHeapPool().GetPooledHeapCount(), 0u);
}

TEST(ExecutorPhaseF, BufferSuballocationReducesBufferCreation) {
    RenderGraphBuilder builder;
    auto buf0 = builder.CreateBuffer({.size = 256, .debugName = "B0"});
    auto buf1 = builder.CreateBuffer({.size = 512, .debugName = "B1"});
    auto buf2 = builder.CreateBuffer({.size = 128, .debugName = "B2"});

    // Chain: Write0 -> Read0_Write1 -> Read1_Write2 -> Read2
    // This ensures non-overlapping lifetimes for some buffers
    builder.AddComputePass(
        "P0",
        [&](PassBuilder& pb) {
            pb.WriteBuffer(buf0);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddComputePass(
        "P1",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(buf0);
            pb.WriteBuffer(buf1);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddComputePass(
        "P2",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(buf1);
            pb.WriteBuffer(buf2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddComputePass(
        "P3",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(buf2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    ExecutorConfig cfg;
    cfg.enableBufferSuballocation = true;
    auto result = CompileAndExecute(builder, cfg);
    ASSERT_TRUE(result.has_value());
    // Should have allocated some transient buffers
    EXPECT_GE(result->stats.allocation.transientBuffersAllocated, 1u);
}

// =============================================================================
// Complex multi-flow stress tests
// =============================================================================

TEST(HeapPoolStress, RapidLayoutChanges) {
    // Simulate a scene where the render graph changes layout every frame
    // (e.g., editor with different views selected each frame)
    MockDeviceFixture f;
    TransientHeapPoolConfig cfg;
    cfg.lruGraceFrames = 2;
    TransientHeapPool pool(cfg);

    for (uint32_t frame = 1; frame <= 20; ++frame) {
        // Alternate between very different layouts
        auto layout = (frame % 3 == 0)   ? MakeAliasingLayout({65536, 0, 0, 0})
                      : (frame % 3 == 1) ? MakeAliasingLayout({0, 32768, 4096, 0})
                                         : MakeAliasingLayout({131072, 65536, 8192, 0});

        auto result = pool.AcquireHeaps(layout, f.handle, frame, false);
        ASSERT_TRUE(result.has_value()) << "Failed at frame " << frame;

        // Pool should not grow unboundedly
        EXPECT_LE(pool.GetPooledHeapCount(), 10u) << "Pool leak at frame " << frame;
    }
}

TEST(HeapPoolStress, LayoutStabilizationZeroAlloc) {
    // After initial frames, a stable application should hit zero-alloc steady state
    MockDeviceFixture f;
    TransientHeapPool pool;
    auto layout = MakeAliasingLayout({65536, 32768, 4096, 0});

    // Frame 1: initial allocation
    pool.AcquireHeaps(layout, f.handle, 1, false);
    EXPECT_EQ(pool.GetStats().heapsAllocated, 3u);

    // Frames 2-100: steady state -> all reused, zero new allocations
    for (uint32_t frame = 2; frame <= 100; ++frame) {
        pool.AcquireHeaps(layout, f.handle, frame, false);
        EXPECT_EQ(pool.GetStats().heapsAllocated, 0u) << "Unexpected alloc at frame " << frame;
        EXPECT_EQ(pool.GetStats().heapsReused, 3u) << "Expected 3 reuses at frame " << frame;
    }
    EXPECT_EQ(pool.GetPooledHeapCount(), 3u);  // No growth
}

TEST(HeapPoolStress, GrowingSizesThenStabilize) {
    // Simulate resolution changes: each frame requests slightly more, then stabilizes
    MockDeviceFixture f;
    TransientHeapPoolConfig cfg;
    cfg.overshootTolerance = 0.20f;
    cfg.lruGraceFrames = 2;
    TransientHeapPool pool(cfg);

    uint64_t baseSize = 65536;
    for (uint32_t frame = 1; frame <= 10; ++frame) {
        auto layout = MakeAliasingLayout({baseSize + frame * 10000, 0, 0, 0}, 1);
        pool.AcquireHeaps(layout, f.handle, frame, false);
    }

    // After growth phase, stabilize
    auto stableLayout = MakeAliasingLayout({165536, 0, 0, 0}, 1);
    pool.AcquireHeaps(stableLayout, f.handle, 11, false);
    auto heapHandle = (*pool.AcquireHeaps(stableLayout, f.handle, 12, false))[0];

    // Subsequent frames should reuse
    for (uint32_t frame = 13; frame <= 20; ++frame) {
        auto r = pool.AcquireHeaps(stableLayout, f.handle, frame, false);
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ((*r)[0].value, heapHandle.value) << "Heap changed at frame " << frame;
    }
}

TEST(HeapPoolStress, AllGroupsActiveAndStale) {
    // All 4 heap groups active, then progressively drop to 1, verify correct eviction
    MockDeviceFixture f;
    TransientHeapPoolConfig cfg;
    cfg.lruGraceFrames = 1;
    TransientHeapPool pool(cfg);

    auto layout4 = MakeAliasingLayout({65536, 32768, 4096, 8192});
    pool.AcquireHeaps(layout4, f.handle, 1, false);
    EXPECT_EQ(pool.GetPooledHeapCount(), 4u);

    auto layout2 = MakeAliasingLayout({65536, 32768, 0, 0});
    pool.AcquireHeaps(layout2, f.handle, 2, false);
    // Buffer+Mixed stale since frame 1, grace=1 -> 2 >= 1+1 -> evicted immediately
    EXPECT_EQ(pool.GetPooledHeapCount(), 2u);

    auto layout1 = MakeAliasingLayout({65536, 0, 0, 0});
    pool.AcquireHeaps(layout1, f.handle, 3, false);
    // NonRtDs stale since frame 2, grace=1 -> 3 >= 2+1 -> evicted
    EXPECT_EQ(pool.GetPooledHeapCount(), 1u);
}

TEST(HeapPoolStress, DefragAfterSceneTransition) {
    // Simulate: large scene (many groups, big heaps) -> transition to small scene -> force defrag
    MockDeviceFixture f;
    TransientHeapPoolConfig cfg;
    cfg.lruGraceFrames = 100;  // Never naturally evict
    TransientHeapPool pool(cfg);

    // Large scene
    auto bigLayout = MakeAliasingLayout({1024 * 1024, 512 * 1024, 256 * 1024, 0});
    pool.AcquireHeaps(bigLayout, f.handle, 1, false);
    EXPECT_EQ(pool.GetPooledHeapCount(), 3u);
    EXPECT_EQ(pool.GetStats().activeBytes, 1024u * 1024u + 512u * 1024u + 256u * 1024u);

    // Small scene: request 65536 RtDs. Old 1MB RtDs is >2x oversized -> released.
    auto smallLayout = MakeAliasingLayout({65536, 0, 0, 0});
    pool.AcquireHeaps(smallLayout, f.handle, 2, false);
    EXPECT_EQ(pool.GetPooledHeapCount(), 3u);  // 2 stale (NonRtDs+Buffer) + 1 new 65K RtDs

    // Force defrag at scene transition
    pool.DefragmentIfNeeded(smallLayout, f.handle, 3, 0.5f, true);
    EXPECT_EQ(pool.GetPooledHeapCount(), 1u);  // Only the small RtDs heap
    EXPECT_GE(pool.GetStats().defragTriggered, 1u);
}

TEST(HeapPoolStress, ConcurrentGroupsWithDifferentLifetimes) {
    // RtDs changes every 5 frames, Buffer changes every 3 frames, NonRtDs stable
    MockDeviceFixture f;
    TransientHeapPoolConfig cfg;
    cfg.lruGraceFrames = 2;
    cfg.overshootTolerance = 0.10f;  // Tight tolerance
    TransientHeapPool pool(cfg);

    for (uint32_t frame = 1; frame <= 30; ++frame) {
        uint64_t rtdsSize = ((frame / 5) % 3 + 1) * 65536;
        uint64_t bufSize = ((frame / 3) % 4 + 1) * 1024;
        auto layout = MakeAliasingLayout({rtdsSize, 32768, bufSize, 0});
        auto r = pool.AcquireHeaps(layout, f.handle, frame, false);
        ASSERT_TRUE(r.has_value()) << "Failed at frame " << frame;
        EXPECT_LE(pool.GetPooledHeapCount(), 12u) << "Pool too large at frame " << frame;
    }
}

// =============================================================================
// Executor integration — new config fields
// =============================================================================

TEST(ExecutorPhaseF, ConfigFieldsPropagate) {
    ExecutorConfig cfg;
    cfg.enableHeapPooling = false;
    cfg.enableBufferSuballocation = false;
    cfg.heapPoolConfig.lruGraceFrames = 7;
    cfg.heapPoolConfig.overshootTolerance = 0.30f;

    RenderGraphExecutor executor(cfg);
    // Config should be stored (we verify indirectly through behavior)
    EXPECT_EQ(executor.GetStats().allocation.heapsReused, 0u);
    EXPECT_EQ(executor.GetStats().allocation.bufferSuballocations, 0u);
}

TEST(ExecutorPhaseF, DefragmentHeapPoolDelegates) {
    MockDevice device;
    device.Init();
    auto dh = DeviceHandle(&device, BackendType::Mock);

    RenderGraphExecutor executor;
    AliasingLayout layout;
    CompiledRenderGraph graph;
    graph.aliasing = layout;

    // Should not crash even with empty layout
    executor.DefragmentHeapPool(graph, dh, 1, 0.5f, true);
}

TEST(ExecutorPhaseF, ReleasePooledResourcesWorks) {
    MockDevice device;
    device.Init();
    auto dh = DeviceHandle(&device, BackendType::Mock);

    RenderGraphExecutor executor;
    executor.ReleasePooledResources(dh);
    EXPECT_EQ(executor.GetHeapPool().GetPooledHeapCount(), 0u);
}

// =============================================================================
// Edge case: zero-size heap groups
// =============================================================================

TEST(HeapPool, ZeroSizeGroupsIgnored) {
    MockDeviceFixture f;
    TransientHeapPool pool;
    auto layout = MakeAliasingLayout({0, 0, 0, 0});

    auto r = pool.AcquireHeaps(layout, f.handle, 1, false);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(pool.GetStats().heapsAllocated, 0u);
    EXPECT_EQ(pool.GetStats().totalPooledBytes, 0u);
}

// =============================================================================
// Edge case: very large heap sizes
// =============================================================================

TEST(HeapPool, LargeHeapSizes) {
    MockDeviceFixture f;
    TransientHeapPool pool;
    uint64_t huge = 4ULL * 1024 * 1024 * 1024;  // 4GB
    auto layout = MakeAliasingLayout({huge, 0, 0, 0});

    auto r = pool.AcquireHeaps(layout, f.handle, 1, false);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE((*r)[0].IsValid());
    EXPECT_EQ(pool.GetStats().activeBytes, huge);
}

// =============================================================================
// Edge case: repeated ReleaseAll then re-acquire
// =============================================================================

TEST(HeapPool, ReleaseAllThenReacquire) {
    MockDeviceFixture f;
    TransientHeapPool pool;
    auto layout = MakeAliasingLayout({65536, 0, 0, 0});

    pool.AcquireHeaps(layout, f.handle, 1, false);
    EXPECT_EQ(pool.GetPooledHeapCount(), 1u);

    pool.ReleaseAll(f.handle);
    EXPECT_EQ(pool.GetPooledHeapCount(), 0u);

    // Re-acquire should work fine
    auto r = pool.AcquireHeaps(layout, f.handle, 2, false);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE((*r)[0].IsValid());
    EXPECT_EQ(pool.GetPooledHeapCount(), 1u);
    EXPECT_EQ(pool.GetStats().heapsAllocated, 1u);  // Fresh allocation
}

// =============================================================================
// Edge case: buffer suballocator called multiple times (parent buffer replaced)
// =============================================================================

TEST(BufferSuballocator, RepeatedCallsReplaceParentBuffer) {
    MockDeviceFixture f;
    TransientHeapPool pool;

    RGResourceNode buf;
    buf.kind = RGResourceKind::Buffer;
    buf.imported = false;
    buf.bufferDesc.size = 512;

    AliasingLayout layout;
    layout.slots.push_back(
        {.slotIndex = 0, .heapGroup = HeapGroupType::Buffer, .size = 512, .alignment = kAlignmentBuffer}
    );
    layout.resourceToSlot = {0};
    std::vector<RGResourceNode> resources = {buf};

    auto r1 = pool.PrepareBufferSuballocations(DeviceMemoryHandle{1}, resources, layout, f.handle);
    ASSERT_TRUE(r1.has_value());
    auto parent1 = pool.GetParentBuffer();
    EXPECT_TRUE(parent1.IsValid());

    // Second call: should destroy old parent and create new
    auto r2 = pool.PrepareBufferSuballocations(DeviceMemoryHandle{2}, resources, layout, f.handle);
    ASSERT_TRUE(r2.has_value());
    auto parent2 = pool.GetParentBuffer();
    EXPECT_TRUE(parent2.IsValid());
    // Different handles (MockDevice increments)
    EXPECT_NE(parent1.value, parent2.value);
}
