/** @file test_phase_i_caching.cpp
 *  @brief Phase I (Graph Caching & Incremental Recompile) comprehensive tests.
 *
 *  Tests define system behavior and boundary conditions for:
 *    I-1: StructuralHash (topology vs descriptor split)
 *    I-2: CacheHitResult classification (FullHit / DescriptorOnlyChange / Miss)
 *    I-3: PSO readiness tracking (bitmask, CheckPsoStaleness)
 *    I-4: Incremental recompile (CompileIncremental)
 *    I-5: External resource patching (CollectExternalResources, PatchExternalResources)
 *    I-6: Multi-graph composition (FrameOrchestrator)
 *
 *  Pure CPU tests — no GPU device required.
 */

#include <gtest/gtest.h>

#include "miki/rendergraph/FrameOrchestrator.h"
#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rendergraph/RenderGraphCompiler.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/Handle.h"

using namespace miki::rg;
using namespace miki::rhi;

// =============================================================================
// Helpers
// =============================================================================

namespace {

    constexpr uint32_t kTestWidth = 128;
    constexpr uint32_t kTestHeight = 128;

    auto MakeTextureHandle(uint32_t idx) -> TextureHandle {
        return TextureHandle::Pack(1, idx, 0, 0);
    }
    auto MakeBufferHandle(uint32_t idx) -> BufferHandle {
        return BufferHandle::Pack(1, idx, 0, 0);
    }
    auto MakePipelineHandle(uint32_t idx, uint16_t gen = 1) -> PipelineHandle {
        return PipelineHandle::Pack(gen, idx, 0, 0);
    }

    // Build a simple 2-pass chain: PassA writes tex, PassB reads tex
    auto BuildSimpleChain(uint32_t width = kTestWidth, uint32_t height = kTestHeight) -> RenderGraphBuilder {
        RenderGraphBuilder b;
        auto tex = b.CreateTexture({.width = width, .height = height, .debugName = "Tex"});
        b.AddGraphicsPass("PassA", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {});
        b.AddGraphicsPass(
            "PassB",
            [&](PassBuilder& pb) {
                pb.ReadTexture(tex);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        b.Build();
        return b;
    }

    // Build a 3-pass chain with an imported backbuffer
    auto BuildChainWithBackbuffer(TextureHandle backbufferHandle, uint32_t width = kTestWidth) -> RenderGraphBuilder {
        RenderGraphBuilder b;
        auto bb = b.ImportBackbuffer(backbufferHandle);
        auto tex = b.CreateTexture({.width = width, .height = width, .debugName = "Intermediate"});
        b.AddGraphicsPass("Render", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {});
        b.AddGraphicsPass(
            "Composite",
            [&](PassBuilder& pb) {
                pb.ReadTexture(tex);
                pb.WriteTexture(bb);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        b.AddPresentPass("Present", bb);
        b.Build();
        return b;
    }

    // Build a chain with imported texture + buffer
    auto BuildChainWithImports(TextureHandle texH, BufferHandle bufH) -> RenderGraphBuilder {
        RenderGraphBuilder b;
        auto importedTex = b.ImportTexture(texH, "SceneColor");
        auto importedBuf = b.ImportBuffer(bufH, "VertexData");
        auto transient = b.CreateTexture({.width = kTestWidth, .height = kTestHeight, .debugName = "Temp"});
        b.AddGraphicsPass(
            "UseImports",
            [&](PassBuilder& pb) {
                pb.ReadTexture(importedTex);
                pb.ReadBuffer(importedBuf);
                transient = pb.WriteTexture(transient);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        b.Build();
        return b;
    }

}  // namespace

// =============================================================================
// I-1: StructuralHash — topology vs descriptor split
// =============================================================================

TEST(StructuralHash, IdenticalGraphsProduceIdenticalHashes) {
    RenderGraphCompiler compiler;
    auto b1 = BuildSimpleChain();
    auto b2 = BuildSimpleChain();
    auto r1 = compiler.Compile(b1);
    auto r2 = compiler.Compile(b2);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());

    EXPECT_EQ(r1->hash.passCount, 2u);
    EXPECT_EQ(r2->hash.passCount, 2u);
    EXPECT_EQ(r1->hash.resourceCount, r2->hash.resourceCount);
    EXPECT_EQ(r1->hash.topologyHash, r2->hash.topologyHash);
    EXPECT_EQ(r1->hash.descHash, r2->hash.descHash);
    EXPECT_EQ(r1->hash, r2->hash);
}

TEST(StructuralHash, DifferentDescriptorsSameTopology) {
    // Same pass structure, different texture size -> topology matches, desc differs
    RenderGraphCompiler compiler;
    auto b1 = BuildSimpleChain(128, 128);
    auto b2 = BuildSimpleChain(256, 256);
    auto r1 = compiler.Compile(b1);
    auto r2 = compiler.Compile(b2);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());

    EXPECT_EQ(r1->hash.passCount, r2->hash.passCount);
    EXPECT_EQ(r1->hash.topologyHash, r2->hash.topologyHash);
    EXPECT_NE(r1->hash.descHash, r2->hash.descHash);
    EXPECT_NE(r1->hash, r2->hash);
    EXPECT_TRUE(r1->hash.IsTopologyMatch(r2->hash));
}

TEST(StructuralHash, DifferentPassCountIsTopologyMismatch) {
    RenderGraphCompiler compiler;
    auto b1 = BuildSimpleChain();

    // 3-pass graph
    RenderGraphBuilder b2;
    auto tex = b2.CreateTexture({.width = kTestWidth, .height = kTestHeight, .debugName = "Tex"});
    b2.AddGraphicsPass("A", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {});
    b2.AddGraphicsPass("B", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {});
    b2.AddGraphicsPass(
        "C",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    b2.Build();

    auto r1 = compiler.Compile(b1);
    auto r2 = compiler.Compile(b2);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());

    EXPECT_NE(r1->hash.passCount, r2->hash.passCount);
    EXPECT_FALSE(r1->hash.IsTopologyMatch(r2->hash));
}

TEST(StructuralHash, ConditionChangeAltersTopologyHash) {
    // Pass enabled vs disabled -> topologyHash must change
    RenderGraphCompiler c1, c2;
    auto makeGraph = [](bool enable) {
        RenderGraphBuilder b;
        auto tex = b.CreateTexture({.width = kTestWidth, .height = kTestHeight, .debugName = "T"});
        auto p
            = b.AddGraphicsPass("P", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {});
        b.AddGraphicsPass(
            "Q",
            [&](PassBuilder& pb) {
                pb.ReadTexture(tex);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        b.EnableIf(p, [enable]() { return enable; });
        b.Build();
        return b;
    };

    auto b1 = makeGraph(true);
    auto b2 = makeGraph(false);
    auto r1 = c1.Compile(b1);
    auto r2 = c2.Compile(b2);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());

    EXPECT_NE(r1->hash.topologyHash, r2->hash.topologyHash);
}

TEST(StructuralHash, DifferentPassNamesAreTopologyMismatch) {
    RenderGraphCompiler compiler;
    auto makeGraph = [](const char* nameA, const char* nameB) {
        RenderGraphBuilder b;
        auto tex = b.CreateTexture({.width = kTestWidth, .height = kTestHeight, .debugName = "T"});
        b.AddGraphicsPass(nameA, [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {});
        b.AddGraphicsPass(
            nameB,
            [&](PassBuilder& pb) {
                pb.ReadTexture(tex);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        b.Build();
        return b;
    };

    auto b1 = makeGraph("Alpha", "Beta");
    auto b2 = makeGraph("Gamma", "Delta");
    auto r1 = compiler.Compile(b1);
    auto r2 = compiler.Compile(b2);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());

    EXPECT_NE(r1->hash.topologyHash, r2->hash.topologyHash);
    EXPECT_FALSE(r1->hash.IsTopologyMatch(r2->hash));
}

TEST(StructuralHash, EmptyGraphHasZeroHash) {
    // Edge case: graph with only side-effect-less passes that all get culled
    RenderGraphCompiler compiler;
    RenderGraphBuilder b;
    auto tex = b.CreateTexture({.width = kTestWidth, .height = kTestHeight, .debugName = "T"});
    b.AddGraphicsPass("Dead", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {});
    // No consumer, no side effects -> should be culled
    b.Build();

    auto r = compiler.Compile(b);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->hash.passCount, 0u);
    EXPECT_EQ(r->passes.size(), 0u);
}

// =============================================================================
// I-2: CacheHitResult classification
// =============================================================================

TEST(CacheClassification, IdenticalGraphIsFullHit) {
    RenderGraphCompiler compiler;
    auto b1 = BuildSimpleChain();
    auto r1 = compiler.Compile(b1);
    ASSERT_TRUE(r1.has_value());

    auto b2 = BuildSimpleChain();
    EXPECT_TRUE(compiler.IsCacheHit(*r1, b2));
}

TEST(CacheClassification, DescriptorOnlyChangeDetected) {
    RenderGraphCompiler compiler;
    auto b1 = BuildSimpleChain(128, 128);
    auto r1 = compiler.Compile(b1);
    ASSERT_TRUE(r1.has_value());

    auto b2 = BuildSimpleChain(256, 256);
    std::vector<bool> activeSet(b2.GetPasses().size(), true);
    auto result = compiler.ClassifyCache(*r1, b2, activeSet);
    EXPECT_EQ(result, CacheHitResult::DescriptorOnlyChange);
}

TEST(CacheClassification, TopologyChangeIsMiss) {
    RenderGraphCompiler compiler;
    auto b1 = BuildSimpleChain();
    auto r1 = compiler.Compile(b1);
    ASSERT_TRUE(r1.has_value());

    // Build 3-pass graph (different topology)
    RenderGraphBuilder b2;
    auto tex = b2.CreateTexture({.width = kTestWidth, .height = kTestHeight, .debugName = "Tex"});
    b2.AddGraphicsPass("A", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {});
    b2.AddGraphicsPass("B", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {});
    b2.AddGraphicsPass(
        "C",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    b2.Build();

    std::vector<bool> activeSet(b2.GetPasses().size(), true);
    auto result = compiler.ClassifyCache(*r1, b2, activeSet);
    EXPECT_EQ(result, CacheHitResult::Miss);
}

TEST(CacheClassification, ConditionFlipIsMiss) {
    // Same graph structure, but one pass disabled -> topology changes -> Miss
    auto makeGraph = [](bool enable) {
        RenderGraphBuilder b;
        auto tex = b.CreateTexture({.width = kTestWidth, .height = kTestHeight, .debugName = "T"});
        auto p
            = b.AddGraphicsPass("P", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {});
        b.AddGraphicsPass(
            "Q",
            [&](PassBuilder& pb) {
                pb.ReadTexture(tex);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        b.EnableIf(p, [enable]() { return enable; });
        b.Build();
        return b;
    };

    RenderGraphCompiler compiler;
    auto b1 = makeGraph(true);
    auto r1 = compiler.Compile(b1);
    ASSERT_TRUE(r1.has_value());

    auto b2 = makeGraph(false);
    // IsCacheHit uses conservative all-active, so we use ClassifyCache with real activeSet
    std::vector<bool> activeSet(b2.GetPasses().size(), true);
    // Simulate condition evaluation: pass 0 disabled
    activeSet[0] = false;
    auto result = compiler.ClassifyCache(*r1, b2, activeSet);
    EXPECT_EQ(result, CacheHitResult::Miss);
}

// =============================================================================
// I-3: PSO readiness tracking
// =============================================================================

TEST(PsoReadiness, IsPsoReadyBoundaryBits) {
    CompiledRenderGraph graph;
    // Create 130 passes (> 2 words of 64 bits)
    constexpr uint32_t kNumPasses = 130;
    graph.passes.resize(kNumPasses);
    auto numWords = (kNumPasses + CompiledRenderGraph::kBitsPerWord - 1) / CompiledRenderGraph::kBitsPerWord;
    EXPECT_EQ(numWords, 3u);  // ceil(130/64) = 3
    graph.psoReadyMask.assign(numWords, 0);

    // Set bit 0, bit 63, bit 64, bit 129
    graph.psoReadyMask[0] |= (1ULL << 0);
    graph.psoReadyMask[0] |= (1ULL << 63);
    graph.psoReadyMask[1] |= (1ULL << 0);  // bit 64
    graph.psoReadyMask[2] |= (1ULL << 1);  // bit 129

    EXPECT_TRUE(graph.IsPsoReady(0));
    EXPECT_FALSE(graph.IsPsoReady(1));
    EXPECT_TRUE(graph.IsPsoReady(63));
    EXPECT_TRUE(graph.IsPsoReady(64));
    EXPECT_FALSE(graph.IsPsoReady(65));
    EXPECT_TRUE(graph.IsPsoReady(129));
    // Out of bounds -> false
    EXPECT_FALSE(graph.IsPsoReady(130));
    EXPECT_FALSE(graph.IsPsoReady(1000));
}

TEST(PsoReadiness, CheckPsoStalenessAllCurrent) {
    CompiledRenderGraph graph;
    graph.passes.resize(3);
    auto pso0 = MakePipelineHandle(10);
    auto pso1 = MakePipelineHandle(20);
    // Pass 2 has no PSO (compute pass without pipeline)
    graph.passes[0].psoHandle = pso0;
    graph.passes[0].psoGeneration = 5;
    graph.passes[1].psoHandle = pso1;
    graph.passes[1].psoGeneration = 7;
    graph.passes[2].psoHandle = {};  // invalid

    // getPsoGeneration returns matching generations
    auto getGen = [](PipelineHandle h) -> uint64_t {
        if (h.GetIndex() == 10) {
            return 5;
        }
        if (h.GetIndex() == 20) {
            return 7;
        }
        return 0;
    };
    CheckPsoStaleness(graph, getGen);

    EXPECT_EQ(graph.psoReadyMask.size(), 1u);  // ceil(3/64) = 1
    EXPECT_TRUE(graph.IsPsoReady(0));
    EXPECT_TRUE(graph.IsPsoReady(1));
    EXPECT_FALSE(graph.IsPsoReady(2));  // no PSO handle
}

TEST(PsoReadiness, CheckPsoStalenessStaleGeneration) {
    CompiledRenderGraph graph;
    graph.passes.resize(2);
    auto pso0 = MakePipelineHandle(10);
    auto pso1 = MakePipelineHandle(20);
    graph.passes[0].psoHandle = pso0;
    graph.passes[0].psoGeneration = 5;
    graph.passes[1].psoHandle = pso1;
    graph.passes[1].psoGeneration = 7;

    // Pipeline 20 was recompiled (generation advanced to 8)
    auto getGen = [](PipelineHandle h) -> uint64_t {
        if (h.GetIndex() == 10) {
            return 5;
        }
        if (h.GetIndex() == 20) {
            return 8;  // stale!
        }
        return 0;
    };
    CheckPsoStaleness(graph, getGen);

    EXPECT_TRUE(graph.IsPsoReady(0));
    EXPECT_FALSE(graph.IsPsoReady(1));  // generation mismatch
}

TEST(PsoReadiness, CheckPsoStalenessZeroPasses) {
    CompiledRenderGraph graph;
    // Empty graph
    auto getGen = [](PipelineHandle) -> uint64_t { return 0; };
    CheckPsoStaleness(graph, getGen);
    EXPECT_TRUE(graph.psoReadyMask.empty());
}

TEST(PsoReadiness, CheckPsoStalenessExactly64Passes) {
    // Boundary: exactly one full word
    CompiledRenderGraph graph;
    graph.passes.resize(64);
    for (uint32_t i = 0; i < 64; ++i) {
        graph.passes[i].psoHandle = MakePipelineHandle(i + 1);
        graph.passes[i].psoGeneration = 1;
    }
    auto getGen = [](PipelineHandle) -> uint64_t { return 1; };
    CheckPsoStaleness(graph, getGen);

    EXPECT_EQ(graph.psoReadyMask.size(), 1u);
    EXPECT_EQ(graph.psoReadyMask[0], UINT64_MAX);  // all 64 bits set
    for (uint32_t i = 0; i < 64; ++i) {
        EXPECT_TRUE(graph.IsPsoReady(i));
    }
}

TEST(PsoReadiness, CheckPsoStaleness65Passes) {
    // Boundary: 65 passes = 2 words, second word has 1 bit
    CompiledRenderGraph graph;
    constexpr uint32_t kN = 65;
    graph.passes.resize(kN);
    for (uint32_t i = 0; i < kN; ++i) {
        graph.passes[i].psoHandle = MakePipelineHandle(i + 1);
        graph.passes[i].psoGeneration = 1;
    }
    auto getGen = [](PipelineHandle) -> uint64_t { return 1; };
    CheckPsoStaleness(graph, getGen);

    EXPECT_EQ(graph.psoReadyMask.size(), 2u);
    EXPECT_EQ(graph.psoReadyMask[0], UINT64_MAX);
    EXPECT_EQ(graph.psoReadyMask[1], 1ULL);  // only bit 0 of second word
    EXPECT_TRUE(graph.IsPsoReady(64));
}

// =============================================================================
// I-4: Incremental recompile
// =============================================================================

TEST(IncrementalRecompile, DescriptorChangeReturnsDescriptorOnlyResult) {
    RenderGraphCompiler compiler;
    auto b1 = BuildSimpleChain(128, 128);
    auto r1 = compiler.Compile(b1);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->cacheResult, CacheHitResult::Miss);  // first compile is always Miss

    // Save hash before CompileIncremental mutates prev in-place
    auto oldDescHash = r1->hash.descHash;
    auto oldTopoHash = r1->hash.topologyHash;

    auto b2 = BuildSimpleChain(256, 256);
    auto r2 = compiler.CompileIncremental(b2, *r1);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->cacheResult, CacheHitResult::DescriptorOnlyChange);
    // Pass count preserved (topology unchanged)
    EXPECT_EQ(r2->hash.passCount, 2u);
    // descHash updated (different from original)
    EXPECT_NE(oldDescHash, r2->hash.descHash);
    // topologyHash preserved
    EXPECT_EQ(oldTopoHash, r2->hash.topologyHash);
}

TEST(IncrementalRecompile, TopologyChangeFallsBackToFullCompile) {
    RenderGraphCompiler compiler;
    auto b1 = BuildSimpleChain();
    auto r1 = compiler.Compile(b1);
    ASSERT_TRUE(r1.has_value());

    // 3-pass chain = different topology
    RenderGraphBuilder b2;
    auto tex = b2.CreateTexture({.width = kTestWidth, .height = kTestHeight, .debugName = "Tex"});
    b2.AddGraphicsPass("X", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {});
    b2.AddGraphicsPass("Y", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {});
    b2.AddGraphicsPass(
        "Z",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    b2.Build();

    auto r2 = compiler.CompileIncremental(b2, *r1);
    ASSERT_TRUE(r2.has_value());
    // Falls back to full compile -> Miss
    EXPECT_EQ(r2->cacheResult, CacheHitResult::Miss);
    EXPECT_EQ(r2->hash.passCount, 3u);
}

TEST(IncrementalRecompile, BatchesUpdatedAfterDescChange) {
    RenderGraphCompiler compiler;
    auto b1 = BuildSimpleChain(128, 128);
    auto r1 = compiler.Compile(b1);
    ASSERT_TRUE(r1.has_value());
    EXPECT_FALSE(r1->batches.empty());

    // Save batch info before CompileIncremental mutates prev
    auto batchPassCount = r1->batches[0].passIndices.size();

    auto b2 = BuildSimpleChain(512, 512);
    auto r2 = compiler.CompileIncremental(b2, *r1);
    ASSERT_TRUE(r2.has_value());
    // Batches must be regenerated (Stage 10)
    EXPECT_FALSE(r2->batches.empty());
    EXPECT_EQ(r2->batches[0].passIndices.size(), batchPassCount);
}

TEST(IncrementalRecompile, FrameIndexAdvances) {
    RenderGraphCompiler compiler;
    auto b1 = BuildSimpleChain();
    auto r1 = compiler.Compile(b1);
    ASSERT_TRUE(r1.has_value());
    uint64_t frame1 = r1->currentFrameIndex;

    auto b2 = BuildSimpleChain(256, 256);
    auto r2 = compiler.CompileIncremental(b2, *r1);
    ASSERT_TRUE(r2.has_value());
    EXPECT_GT(r2->currentFrameIndex, frame1);
}

// =============================================================================
// I-5: External resource collection and patching
// =============================================================================

TEST(ExternalResources, BackbufferCollectedAsExternalSlot) {
    RenderGraphCompiler compiler;
    auto bbHandle = MakeTextureHandle(42);
    auto b = BuildChainWithBackbuffer(bbHandle);
    auto r = compiler.Compile(b);
    ASSERT_TRUE(r.has_value());

    // Should have at least one external resource (the backbuffer)
    EXPECT_FALSE(r->externalResources.empty());
    bool foundBackbuffer = false;
    for (auto& slot : r->externalResources) {
        if (slot.type == ExternalResourceType::Backbuffer) {
            foundBackbuffer = true;
            EXPECT_EQ(slot.physicalTexture, bbHandle);
            EXPECT_NE(slot.debugName, nullptr);
        }
    }
    EXPECT_TRUE(foundBackbuffer);
}

TEST(ExternalResources, ImportedTextureAndBufferCollected) {
    RenderGraphCompiler compiler;
    auto texH = MakeTextureHandle(100);
    auto bufH = MakeBufferHandle(200);
    auto b = BuildChainWithImports(texH, bufH);
    auto r = compiler.Compile(b);
    ASSERT_TRUE(r.has_value());

    bool foundTex = false;
    bool foundBuf = false;
    for (auto& slot : r->externalResources) {
        if (slot.type == ExternalResourceType::ImportedTexture) {
            foundTex = true;
            EXPECT_EQ(slot.physicalTexture, texH);
        }
        if (slot.type == ExternalResourceType::ImportedBuffer) {
            foundBuf = true;
            EXPECT_EQ(slot.physicalBuffer, bufH);
        }
    }
    EXPECT_TRUE(foundTex);
    EXPECT_TRUE(foundBuf);
}

TEST(ExternalResources, PatchBackbufferUpdatesPhysicalHandle) {
    CompiledRenderGraph graph;
    graph.externalResources.push_back(
        {.resourceIndex = 0, .type = ExternalResourceType::Backbuffer, .physicalTexture = MakeTextureHandle(1)}
    );
    graph.externalResources.push_back(
        {.resourceIndex = 1, .type = ExternalResourceType::ImportedTexture, .physicalTexture = MakeTextureHandle(99)}
    );

    auto newSwapchain = MakeTextureHandle(777);
    FrameContext ctx{.swapchainImage = newSwapchain};
    PatchExternalResources(graph, ctx);

    // Backbuffer patched
    EXPECT_EQ(graph.externalResources[0].physicalTexture, newSwapchain);
    // ImportedTexture unchanged
    EXPECT_EQ(graph.externalResources[1].physicalTexture, MakeTextureHandle(99));
}

TEST(ExternalResources, PatchEmptyGraphNoOp) {
    CompiledRenderGraph graph;
    FrameContext ctx{.swapchainImage = MakeTextureHandle(1)};
    PatchExternalResources(graph, ctx);
    EXPECT_TRUE(graph.externalResources.empty());
}

TEST(ExternalResources, PatchMultipleBackbuffers) {
    // Edge case: two backbuffers (e.g., stereo rendering)
    CompiledRenderGraph graph;
    graph.externalResources.push_back(
        {.resourceIndex = 0, .type = ExternalResourceType::Backbuffer, .physicalTexture = MakeTextureHandle(1)}
    );
    graph.externalResources.push_back(
        {.resourceIndex = 1, .type = ExternalResourceType::Backbuffer, .physicalTexture = MakeTextureHandle(2)}
    );

    auto newSwap = MakeTextureHandle(555);
    PatchExternalResources(graph, {.swapchainImage = newSwap});

    // Both patched to same swapchain image
    EXPECT_EQ(graph.externalResources[0].physicalTexture, newSwap);
    EXPECT_EQ(graph.externalResources[1].physicalTexture, newSwap);
}

TEST(ExternalResources, TransientResourcesNotCollected) {
    RenderGraphCompiler compiler;
    RenderGraphBuilder b;
    auto tex = b.CreateTexture({.width = kTestWidth, .height = kTestHeight, .debugName = "Transient"});
    b.AddGraphicsPass(
        "P",
        [&](PassBuilder& pb) {
            tex = pb.WriteTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    b.Build();
    auto r = compiler.Compile(b);
    ASSERT_TRUE(r.has_value());
    // No imported resources -> no external slots
    EXPECT_TRUE(r->externalResources.empty());
}

// =============================================================================
// I-6: FrameOrchestrator — multi-graph composition
// =============================================================================

TEST(FrameOrchestrator, DefaultStateAllLayersActive) {
    FrameOrchestrator orch;
    EXPECT_EQ(orch.GetActiveLayerCount(), static_cast<uint32_t>(LayerId::Count));
}

TEST(FrameOrchestrator, DeactivateLayerReducesCount) {
    FrameOrchestrator orch;
    orch.SetLayerActive(LayerId::Preview, false);
    orch.SetLayerActive(LayerId::SVG, false);
    EXPECT_EQ(orch.GetActiveLayerCount(), static_cast<uint32_t>(LayerId::Count) - 2);
}

TEST(FrameOrchestrator, CompileAllWithSingleLayer) {
    FrameOrchestrator orch;
    // Deactivate all but Scene
    orch.SetLayerActive(LayerId::Preview, false);
    orch.SetLayerActive(LayerId::Overlay, false);
    orch.SetLayerActive(LayerId::Widgets, false);
    orch.SetLayerActive(LayerId::SVG, false);
    orch.SetLayerActive(LayerId::HUD, false);

    orch.BeginFrame({.swapchainImage = MakeTextureHandle(1)});
    orch.SetLayerBuilder(LayerId::Scene, BuildSimpleChain());

    EXPECT_TRUE(orch.CompileAll());

    auto* cg = orch.GetCompiledGraph(LayerId::Scene);
    ASSERT_NE(cg, nullptr);
    EXPECT_EQ(cg->hash.passCount, 2u);
    EXPECT_EQ(cg->cacheResult, CacheHitResult::Miss);  // first compile

    // Inactive layers return nullptr
    EXPECT_EQ(orch.GetCompiledGraph(LayerId::Preview), nullptr);
}

TEST(FrameOrchestrator, CacheHitOnSecondFrameIdenticalGraph) {
    FrameOrchestrator orch;
    // Only use Scene layer
    for (int i = 1; i < static_cast<int>(LayerId::Count); ++i) {
        orch.SetLayerActive(static_cast<LayerId>(i), false);
    }

    // Frame 1: full compile
    orch.BeginFrame({.swapchainImage = MakeTextureHandle(1)});
    orch.SetLayerBuilder(LayerId::Scene, BuildSimpleChain());
    EXPECT_TRUE(orch.CompileAll());
    EXPECT_EQ(orch.GetLayerCacheResult(LayerId::Scene), CacheHitResult::Miss);

    // Frame 2: identical graph -> FullHit
    orch.BeginFrame({.swapchainImage = MakeTextureHandle(2)});
    orch.SetLayerBuilder(LayerId::Scene, BuildSimpleChain());
    EXPECT_TRUE(orch.CompileAll());
    EXPECT_EQ(orch.GetLayerCacheResult(LayerId::Scene), CacheHitResult::FullHit);
}

TEST(FrameOrchestrator, IncrementalRecompileOnDescChange) {
    FrameOrchestrator orch;
    for (int i = 1; i < static_cast<int>(LayerId::Count); ++i) {
        orch.SetLayerActive(static_cast<LayerId>(i), false);
    }

    // Frame 1
    orch.BeginFrame({.swapchainImage = MakeTextureHandle(1)});
    orch.SetLayerBuilder(LayerId::Scene, BuildSimpleChain(128, 128));
    EXPECT_TRUE(orch.CompileAll());
    EXPECT_EQ(orch.GetLayerCacheResult(LayerId::Scene), CacheHitResult::Miss);

    // Frame 2: same topology, different descriptors
    orch.BeginFrame({.swapchainImage = MakeTextureHandle(2)});
    orch.SetLayerBuilder(LayerId::Scene, BuildSimpleChain(256, 256));
    EXPECT_TRUE(orch.CompileAll());
    EXPECT_EQ(orch.GetLayerCacheResult(LayerId::Scene), CacheHitResult::DescriptorOnlyChange);
}

TEST(FrameOrchestrator, MultipleLayersCompileIndependently) {
    FrameOrchestrator orch;
    for (int i = 2; i < static_cast<int>(LayerId::Count); ++i) {
        orch.SetLayerActive(static_cast<LayerId>(i), false);
    }

    orch.BeginFrame({.swapchainImage = MakeTextureHandle(1)});
    orch.SetLayerBuilder(LayerId::Scene, BuildSimpleChain(128, 128));
    orch.SetLayerBuilder(LayerId::Preview, BuildSimpleChain(64, 64));
    EXPECT_TRUE(orch.CompileAll());

    auto* scene = orch.GetCompiledGraph(LayerId::Scene);
    auto* preview = orch.GetCompiledGraph(LayerId::Preview);
    ASSERT_NE(scene, nullptr);
    ASSERT_NE(preview, nullptr);
    EXPECT_EQ(scene->hash.passCount, 2u);
    EXPECT_EQ(preview->hash.passCount, 2u);
    // Different descriptors -> different descHash
    EXPECT_NE(scene->hash.descHash, preview->hash.descHash);
}

TEST(FrameOrchestrator, LayerWithoutBuilderSkipped) {
    FrameOrchestrator orch;
    orch.BeginFrame({.swapchainImage = MakeTextureHandle(1)});
    // Don't set any builder
    EXPECT_TRUE(orch.CompileAll());  // should succeed (nothing to compile)
    EXPECT_EQ(orch.GetCompiledGraph(LayerId::Scene), nullptr);
}

TEST(FrameOrchestrator, InactiveLayerCacheResultIsMiss) {
    FrameOrchestrator orch;
    orch.SetLayerActive(LayerId::Scene, false);
    EXPECT_EQ(orch.GetLayerCacheResult(LayerId::Scene), CacheHitResult::Miss);
}

// =============================================================================
// Complex multi-flow stress tests
// =============================================================================

TEST(PhaseIStress, RapidCacheTransitions_Miss_FullHit_DescChange_Miss) {
    // Simulate 4-frame sequence exercising all cache states
    FrameOrchestrator orch;
    for (int i = 1; i < static_cast<int>(LayerId::Count); ++i) {
        orch.SetLayerActive(static_cast<LayerId>(i), false);
    }

    // Frame 1: first compile -> Miss
    orch.BeginFrame({.swapchainImage = MakeTextureHandle(1)});
    orch.SetLayerBuilder(LayerId::Scene, BuildSimpleChain(128, 128));
    EXPECT_TRUE(orch.CompileAll());
    EXPECT_EQ(orch.GetLayerCacheResult(LayerId::Scene), CacheHitResult::Miss);
    auto* cg1 = orch.GetCompiledGraph(LayerId::Scene);
    ASSERT_NE(cg1, nullptr);
    auto hash1 = cg1->hash;

    // Frame 2: identical -> FullHit
    orch.BeginFrame({.swapchainImage = MakeTextureHandle(2)});
    orch.SetLayerBuilder(LayerId::Scene, BuildSimpleChain(128, 128));
    EXPECT_TRUE(orch.CompileAll());
    EXPECT_EQ(orch.GetLayerCacheResult(LayerId::Scene), CacheHitResult::FullHit);

    // Frame 3: descriptor change -> DescriptorOnlyChange
    orch.BeginFrame({.swapchainImage = MakeTextureHandle(3)});
    orch.SetLayerBuilder(LayerId::Scene, BuildSimpleChain(256, 256));
    EXPECT_TRUE(orch.CompileAll());
    EXPECT_EQ(orch.GetLayerCacheResult(LayerId::Scene), CacheHitResult::DescriptorOnlyChange);
    auto* cg3 = orch.GetCompiledGraph(LayerId::Scene);
    ASSERT_NE(cg3, nullptr);
    EXPECT_EQ(cg3->hash.topologyHash, hash1.topologyHash);
    EXPECT_NE(cg3->hash.descHash, hash1.descHash);

    // Frame 4: topology change -> Miss
    orch.BeginFrame({.swapchainImage = MakeTextureHandle(4)});
    RenderGraphBuilder b4;
    auto tex = b4.CreateTexture({.width = kTestWidth, .height = kTestHeight, .debugName = "T"});
    b4.AddGraphicsPass(
        "Only",
        [&](PassBuilder& pb) {
            tex = pb.WriteTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    b4.Build();
    orch.SetLayerBuilder(LayerId::Scene, std::move(b4));
    EXPECT_TRUE(orch.CompileAll());
    EXPECT_EQ(orch.GetLayerCacheResult(LayerId::Scene), CacheHitResult::Miss);
    EXPECT_EQ(orch.GetCompiledGraph(LayerId::Scene)->hash.passCount, 1u);
}

TEST(PhaseIStress, PsoStalenessWithPartialInvalidation) {
    // 88 passes (typical scene pass count), varying PSO validity patterns
    constexpr uint32_t kNumPasses = 88;
    CompiledRenderGraph graph;
    graph.passes.resize(kNumPasses);

    // Assign PSOs to even-indexed passes only
    for (uint32_t i = 0; i < kNumPasses; ++i) {
        if (i % 2 == 0) {
            graph.passes[i].psoHandle = MakePipelineHandle(i + 1);
            graph.passes[i].psoGeneration = 10;
        }
    }

    // Hot-reload: invalidate every 4th PSO (gen advances to 11)
    auto getGen = [](PipelineHandle h) -> uint64_t {
        uint32_t idx = h.GetIndex() - 1;
        if (idx % 4 == 0) {
            return 11;  // stale
        }
        return 10;  // current
    };
    CheckPsoStaleness(graph, getGen);

    uint32_t readyCount = 0;
    uint32_t staleCount = 0;
    uint32_t noPsoCount = 0;
    for (uint32_t i = 0; i < kNumPasses; ++i) {
        if (i % 2 != 0) {
            EXPECT_FALSE(graph.IsPsoReady(i)) << "Pass " << i << " has no PSO";
            noPsoCount++;
        } else if (i % 4 == 0) {
            EXPECT_FALSE(graph.IsPsoReady(i)) << "Pass " << i << " should be stale";
            staleCount++;
        } else {
            EXPECT_TRUE(graph.IsPsoReady(i)) << "Pass " << i << " should be ready";
            readyCount++;
        }
    }
    EXPECT_EQ(noPsoCount, 44u);  // odd indices
    EXPECT_EQ(staleCount, 22u);  // 0, 4, 8, ... 84 -> 22 values
    EXPECT_EQ(readyCount, 22u);  // 2, 6, 10, ... 86 -> 22 values
    EXPECT_EQ(noPsoCount + staleCount + readyCount, kNumPasses);
}

TEST(PhaseIStress, ExternalResourcePatchingWithManySlots) {
    // Simulate heavy import scenario: 50 imported textures + 10 imported buffers + 1 backbuffer
    CompiledRenderGraph graph;
    graph.externalResources.push_back(
        {.resourceIndex = 0, .type = ExternalResourceType::Backbuffer, .physicalTexture = MakeTextureHandle(1)}
    );
    for (uint16_t i = 1; i <= 50; ++i) {
        graph.externalResources.push_back(
            {.resourceIndex = i,
             .type = ExternalResourceType::ImportedTexture,
             .physicalTexture = MakeTextureHandle(100 + i)}
        );
    }
    for (uint16_t i = 51; i <= 60; ++i) {
        graph.externalResources.push_back(
            {.resourceIndex = i,
             .type = ExternalResourceType::ImportedBuffer,
             .physicalBuffer = MakeBufferHandle(200 + i)}
        );
    }

    auto newSwap = MakeTextureHandle(999);
    PatchExternalResources(graph, {.swapchainImage = newSwap});

    // Only backbuffer patched
    EXPECT_EQ(graph.externalResources[0].physicalTexture, newSwap);
    // Imported textures unchanged
    for (size_t i = 1; i <= 50; ++i) {
        EXPECT_EQ(graph.externalResources[i].physicalTexture, MakeTextureHandle(100 + static_cast<uint32_t>(i)));
    }
    // Imported buffers unchanged
    for (size_t i = 51; i <= 60; ++i) {
        auto idx = i - 51 + 51;
        EXPECT_EQ(graph.externalResources[idx].physicalBuffer, MakeBufferHandle(200 + static_cast<uint32_t>(idx)));
    }
}

TEST(PhaseIStress, IncrementalRecompilePreservesEdgesAndSyncPoints) {
    // Verify that incremental recompile preserves topology-dependent data
    RenderGraphCompiler compiler;
    auto bbH = MakeTextureHandle(1);
    auto b1 = BuildChainWithBackbuffer(bbH, 128);
    auto r1 = compiler.Compile(b1);
    ASSERT_TRUE(r1.has_value());

    auto edgeCount1 = r1->edges.size();
    auto syncCount1 = r1->syncPoints.size();

    // Incremental: same topology, different descriptor
    auto b2 = BuildChainWithBackbuffer(bbH, 256);
    auto r2 = compiler.CompileIncremental(b2, *r1);
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(r2->cacheResult, CacheHitResult::DescriptorOnlyChange);
    // Edges and sync points preserved from first compile
    EXPECT_EQ(r2->edges.size(), edgeCount1);
    EXPECT_EQ(r2->syncPoints.size(), syncCount1);
}

TEST(PhaseIStress, MultiLayerMixedCacheStates) {
    // Scene: FullHit, Preview: DescChange, Overlay: Miss simultaneously
    FrameOrchestrator orch;
    for (int i = 3; i < static_cast<int>(LayerId::Count); ++i) {
        orch.SetLayerActive(static_cast<LayerId>(i), false);
    }

    // Frame 1: compile all three
    orch.BeginFrame({.swapchainImage = MakeTextureHandle(1)});
    orch.SetLayerBuilder(LayerId::Scene, BuildSimpleChain(128, 128));
    orch.SetLayerBuilder(LayerId::Preview, BuildSimpleChain(64, 64));
    orch.SetLayerBuilder(LayerId::Overlay, BuildSimpleChain(32, 32));
    EXPECT_TRUE(orch.CompileAll());

    // Frame 2:
    //   Scene: same -> FullHit
    //   Preview: same topology, different size -> DescriptorOnlyChange
    //   Overlay: different topology -> Miss
    orch.BeginFrame({.swapchainImage = MakeTextureHandle(2)});
    orch.SetLayerBuilder(LayerId::Scene, BuildSimpleChain(128, 128));
    orch.SetLayerBuilder(LayerId::Preview, BuildSimpleChain(256, 256));

    // Build a different topology for Overlay (3 passes)
    RenderGraphBuilder overlayB;
    auto ot = overlayB.CreateTexture({.width = 32, .height = 32, .debugName = "OT"});
    overlayB.AddGraphicsPass("OA", [&](PassBuilder& pb) { ot = pb.WriteTexture(ot); }, [](RenderPassContext&) {});
    overlayB.AddGraphicsPass("OB", [&](PassBuilder& pb) { ot = pb.WriteTexture(ot); }, [](RenderPassContext&) {});
    overlayB.AddGraphicsPass(
        "OC",
        [&](PassBuilder& pb) {
            pb.ReadTexture(ot);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    overlayB.Build();
    orch.SetLayerBuilder(LayerId::Overlay, std::move(overlayB));
    EXPECT_TRUE(orch.CompileAll());

    EXPECT_EQ(orch.GetLayerCacheResult(LayerId::Scene), CacheHitResult::FullHit);
    EXPECT_EQ(orch.GetLayerCacheResult(LayerId::Preview), CacheHitResult::DescriptorOnlyChange);
    EXPECT_EQ(orch.GetLayerCacheResult(LayerId::Overlay), CacheHitResult::Miss);

    // Verify Overlay was fully recompiled with 3 passes
    auto* overlayG = orch.GetCompiledGraph(LayerId::Overlay);
    ASSERT_NE(overlayG, nullptr);
    EXPECT_EQ(overlayG->hash.passCount, 3u);
}

TEST(PhaseIStress, SwapchainRecreationAcrossFrames) {
    // Simulate swapchain recreation: backbuffer handle changes each frame
    RenderGraphCompiler compiler;

    auto bb1 = MakeTextureHandle(1);
    auto b1 = BuildChainWithBackbuffer(bb1);
    auto r1 = compiler.Compile(b1);
    ASSERT_TRUE(r1.has_value());

    // Patch with new swapchain
    auto bb2 = MakeTextureHandle(2);
    PatchExternalResources(*r1, {.swapchainImage = bb2});

    bool foundBB = false;
    for (auto& slot : r1->externalResources) {
        if (slot.type == ExternalResourceType::Backbuffer) {
            EXPECT_EQ(slot.physicalTexture, bb2);
            foundBB = true;
        }
    }
    EXPECT_TRUE(foundBB);

    // Patch again (window resize)
    auto bb3 = MakeTextureHandle(3);
    PatchExternalResources(*r1, {.swapchainImage = bb3});
    for (auto& slot : r1->externalResources) {
        if (slot.type == ExternalResourceType::Backbuffer) {
            EXPECT_EQ(slot.physicalTexture, bb3);
        }
    }
}

TEST(PhaseIStress, OrchestratorReactivateLayerAfterDeactivation) {
    FrameOrchestrator orch;
    for (int i = 1; i < static_cast<int>(LayerId::Count); ++i) {
        orch.SetLayerActive(static_cast<LayerId>(i), false);
    }

    // Frame 1: compile Scene
    orch.BeginFrame({.swapchainImage = MakeTextureHandle(1)});
    orch.SetLayerBuilder(LayerId::Scene, BuildSimpleChain());
    EXPECT_TRUE(orch.CompileAll());
    EXPECT_NE(orch.GetCompiledGraph(LayerId::Scene), nullptr);

    // Frame 2: deactivate Scene
    orch.SetLayerActive(LayerId::Scene, false);
    EXPECT_EQ(orch.GetActiveLayerCount(), 0u);
    orch.BeginFrame({.swapchainImage = MakeTextureHandle(2)});
    EXPECT_TRUE(orch.CompileAll());
    // Scene still has compiled graph from frame 1
    EXPECT_NE(orch.GetCompiledGraph(LayerId::Scene), nullptr);

    // Frame 3: reactivate Scene with same graph -> FullHit
    orch.SetLayerActive(LayerId::Scene, true);
    orch.BeginFrame({.swapchainImage = MakeTextureHandle(3)});
    orch.SetLayerBuilder(LayerId::Scene, BuildSimpleChain());
    EXPECT_TRUE(orch.CompileAll());
    EXPECT_EQ(orch.GetLayerCacheResult(LayerId::Scene), CacheHitResult::FullHit);
}

TEST(PhaseIStress, HighPassCountHashStability) {
    // Verify hash determinism for a large graph (50 passes)
    auto buildLargeGraph = []() {
        RenderGraphBuilder b;
        auto tex = b.CreateTexture({.width = kTestWidth, .height = kTestHeight, .debugName = "T"});
        for (int i = 0; i < 49; ++i) {
            std::string name = "Pass_" + std::to_string(i);
            // Names are arena-allocated by builder, so the name pointers are stable
            b.AddGraphicsPass(
                name.c_str(), [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {}
            );
        }
        b.AddGraphicsPass(
            "Final",
            [&](PassBuilder& pb) {
                pb.ReadTexture(tex);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        b.Build();
        return b;
    };

    RenderGraphCompiler c1, c2;
    auto b1 = buildLargeGraph();
    auto b2 = buildLargeGraph();
    auto r1 = c1.Compile(b1);
    auto r2 = c2.Compile(b2);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());

    EXPECT_EQ(r1->hash.passCount, 50u);
    EXPECT_EQ(r2->hash.passCount, 50u);
    EXPECT_EQ(r1->hash.topologyHash, r2->hash.topologyHash);
    EXPECT_EQ(r1->hash.descHash, r2->hash.descHash);
    EXPECT_EQ(r1->hash, r2->hash);
}
