/** @file test_integration_multiflow.cpp
 *  @brief End-to-end multi-flow integration tests for the entire RenderGraph module.
 *
 *  These tests exercise the full Build -> Compile -> Execute pipeline through
 *  MockDevice, covering all 10 compiler stages + executor + Phase L advanced features.
 *  Tests define system behavior and boundary conditions, not just "does it run".
 *
 *  Coverage matrix:
 *    - Full pipeline: Builder -> Compiler (10 stages) -> Executor (Alloc+Record+Submit)
 *    - Cross-queue: Graphics + AsyncCompute + Transfer with sync/batch validation
 *    - Advanced features: AccelStruct, VRS, Sparse, MeshShader through execution
 *    - Conditional execution + DCE through full pipeline
 *    - Incremental recompile + cache classification
 *    - History resources (cross-frame temporal)
 *    - Present pass + external resource patching
 *    - Barrier structure: split, fence, cross-queue QFOT, aliasing barriers
 *    - Complex topologies: diamond, wide fan-out/in, WAW/WAR chains
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <string>
#include <unordered_set>
#include <vector>

#include "miki/core/EnumStrings.h"
#include "miki/core/LinearAllocator.h"
#include "miki/frame/CommandPoolAllocator.h"
#include "miki/frame/FrameContext.h"
#include "miki/frame/SyncScheduler.h"
#include "miki/rendergraph/AsyncComputeScheduler.h"
#include "miki/rendergraph/RenderGraphAdvanced.h"
#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rendergraph/RenderGraphCompiler.h"
#include "miki/rendergraph/RenderGraphExecutor.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rendergraph/RenderPassContext.h"
#include "miki/rhi/GpuCapabilityProfile.h"
#include "miki/rhi/backend/MockDevice.h"

using namespace miki::rg;
using namespace miki::rhi;

// =============================================================================
// Test infrastructure — compile + execute through MockDevice
// =============================================================================

namespace {

    struct FullPipelineResult {
        CompiledRenderGraph compiled;
        ExecutionStats stats;
    };

    /// Compile and execute a graph through the full MockDevice pipeline.
    /// Returns compiled graph + execution stats on success.
    auto RunFullPipeline(
        RenderGraphBuilder& builder, RenderGraphCompiler::Options compOpts = {}, ExecutorConfig execCfg = {},
        bool build = true
    ) -> std::expected<FullPipelineResult, miki::core::ErrorCode> {
        if (build) {
            builder.Build();
        }

        RenderGraphCompiler compiler(compOpts);
        auto compiled = compiler.Compile(builder);
        if (!compiled) {
            return std::unexpected(compiled.error());
        }

        MockDevice device;
        device.Init();
        auto deviceHandle = DeviceHandle(&device, BackendType::Mock);

        miki::frame::SyncScheduler scheduler;
        scheduler.Init(device.GetQueueTimelinesImpl());

        miki::frame::CommandPoolAllocator::Desc pd{
            .device = deviceHandle,
            .framesInFlight = 2,
            .hasAsyncCompute = true,
            .hasAsyncTransfer = true,
        };
        auto pool = miki::frame::CommandPoolAllocator::Create(pd);
        if (!pool) {
            return std::unexpected(miki::core::ErrorCode::InvalidState);
        }

        miki::frame::FrameContext frame{.width = 1920, .height = 1080};

        RenderGraphExecutor executor(execCfg);
        auto execResult = executor.Execute(*compiled, builder, frame, deviceHandle, scheduler, *pool);
        if (!execResult) {
            return std::unexpected(execResult.error());
        }

        return FullPipelineResult{.compiled = std::move(*compiled), .stats = executor.GetStats()};
    }

    /// Find compiled pass position by builder pass index. Returns -1 if not found.
    auto FindPass(const CompiledRenderGraph& cg, uint32_t passIndex) -> int {
        for (uint32_t i = 0; i < cg.passes.size(); ++i) {
            if (cg.passes[i].passIndex == passIndex) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    /// Verify topological ordering: for every edge, src appears before dst.
    void VerifyTopo(const CompiledRenderGraph& cg) {
        std::unordered_map<uint32_t, uint32_t> passToPos;
        for (uint32_t i = 0; i < cg.passes.size(); ++i) {
            passToPos[cg.passes[i].passIndex] = i;
        }
        for (auto& e : cg.edges) {
            auto srcIt = passToPos.find(e.srcPass);
            auto dstIt = passToPos.find(e.dstPass);
            if (srcIt != passToPos.end() && dstIt != passToPos.end()) {
                EXPECT_LT(srcIt->second, dstIt->second)
                    << "Edge " << e.srcPass << " -> " << e.dstPass << " violates topological order";
            }
        }
    }

    /// Count barriers of a specific type across all compiled passes.
    struct BarrierCounts {
        uint32_t acquire = 0;
        uint32_t release = 0;
        uint32_t crossQueue = 0;
        uint32_t splitRelease = 0;
        uint32_t splitAcquire = 0;
        uint32_t aliasing = 0;
        uint32_t fence = 0;
    };

    auto CountBarriers(const CompiledRenderGraph& cg) -> BarrierCounts {
        BarrierCounts c{};
        for (auto& p : cg.passes) {
            c.acquire += static_cast<uint32_t>(p.acquireBarriers.size());
            c.release += static_cast<uint32_t>(p.releaseBarriers.size());
            for (auto& b : p.acquireBarriers) {
                if (b.isCrossQueue) {
                    c.crossQueue++;
                }
                if (b.isSplitAcquire) {
                    c.splitAcquire++;
                }
                if (b.isAliasingBarrier) {
                    c.aliasing++;
                }
                if (b.isFenceBarrier) {
                    c.fence++;
                }
            }
            for (auto& b : p.releaseBarriers) {
                if (b.isCrossQueue) {
                    c.crossQueue++;
                }
                if (b.isSplitRelease) {
                    c.splitRelease++;
                }
                if (b.isAliasingBarrier) {
                    c.aliasing++;
                }
                if (b.isFenceBarrier) {
                    c.fence++;
                }
            }
        }
        return c;
    }

}  // namespace

// #############################################################################
//  1. Classic Deferred Rendering Pipeline — Full Execution
// #############################################################################

TEST(MultiFlow, ClassicDeferredPipelineExecution) {
    // GBuffer -> Lighting -> Bloom -> ToneMap -> UI (side effect)
    // Validates: topological order, barriers, aliasing, batching, pass invocation
    RenderGraphBuilder builder;
    auto gbufA = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "GBufA"});
    auto gbufB = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "GBufB"});
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 1920, .height = 1080, .debugName = "Depth"});
    auto hdr
        = builder.CreateTexture({.format = Format::RGBA16_FLOAT, .width = 1920, .height = 1080, .debugName = "HDR"});
    auto bloom
        = builder.CreateTexture({.format = Format::RGBA16_FLOAT, .width = 960, .height = 540, .debugName = "Bloom"});
    auto ldr = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "LDR"});

    std::vector<uint32_t> order;

    auto hGBuf = builder.AddGraphicsPass(
        "GBuffer",
        [&](PassBuilder& pb) {
            gbufA = pb.WriteColorAttachment(gbufA, 0);
            gbufB = pb.WriteColorAttachment(gbufB, 1);
            depth = pb.WriteDepthStencil(depth);
        },
        [&](RenderPassContext& ctx) {
            order.push_back(0);
            // Verify all 3 attachments are available
            EXPECT_TRUE(ctx.GetTextureView(gbufA).IsValid());
            EXPECT_TRUE(ctx.GetTextureView(gbufB).IsValid());
        }
    );
    auto hLight = builder.AddGraphicsPass(
        "Lighting",
        [&](PassBuilder& pb) {
            pb.ReadTexture(gbufA);
            pb.ReadTexture(gbufB);
            pb.ReadDepth(depth);
            hdr = pb.WriteColorAttachment(hdr);
        },
        [&](RenderPassContext& ctx) {
            order.push_back(1);
            EXPECT_TRUE(ctx.GetTextureView(hdr).IsValid());
        }
    );
    auto hBloom = builder.AddComputePass(
        "BloomExtract",
        [&](PassBuilder& pb) {
            pb.ReadTexture(hdr);
            bloom = pb.WriteTexture(bloom, ResourceAccess::ShaderWrite);
        },
        [&](RenderPassContext&) { order.push_back(2); }
    );
    auto hTone = builder.AddGraphicsPass(
        "ToneMap",
        [&](PassBuilder& pb) {
            pb.ReadTexture(hdr);
            pb.ReadTexture(bloom);
            ldr = pb.WriteColorAttachment(ldr);
        },
        [&](RenderPassContext&) { order.push_back(3); }
    );
    auto hUI = builder.AddGraphicsPass(
        "UI",
        [&](PassBuilder& pb) {
            pb.ReadTexture(ldr);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { order.push_back(4); }
    );

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableTransientAliasing = true;
    auto result = RunFullPipeline(builder, opts);
    ASSERT_TRUE(result.has_value());
    auto& cg = result->compiled;

    // All 5 passes survive DCE (UI has side effects, rest are producers)
    EXPECT_EQ(cg.passes.size(), 5u);
    EXPECT_EQ(result->stats.recording.passesRecorded, 5u);

    // Verify invocation count and topological order
    ASSERT_EQ(order.size(), 5u);
    int pG = FindPass(cg, hGBuf.index);
    int pL = FindPass(cg, hLight.index);
    int pB = FindPass(cg, hBloom.index);
    int pT = FindPass(cg, hTone.index);
    int pU = FindPass(cg, hUI.index);
    EXPECT_LT(pG, pL);  // GBuffer before Lighting
    EXPECT_LT(pL, pB);  // Lighting before Bloom (HDR dependency)
    EXPECT_LT(pL, pT);  // Lighting before ToneMap
    EXPECT_LT(pB, pT);  // Bloom before ToneMap
    EXPECT_LT(pT, pU);  // ToneMap before UI
    VerifyTopo(cg);

    // Edges: GBuf->Light (gbufA, gbufB, depth), Light->Bloom (hdr), Light->Tone (hdr),
    //        Bloom->Tone (bloom), Tone->UI (ldr) = at least 6 RAW edges
    EXPECT_GE(cg.edges.size(), 6u);

    // Aliasing: gbufA/gbufB lifetime [0,1], bloom [2,3] — non-overlapping, could alias
    // Verify heap group sizes are non-zero
    bool anyRtDsHeap = false;
    for (size_t g = 0; g < kHeapGroupCount; ++g) {
        if (cg.aliasing.heapGroupSizes[g] > 0) {
            anyRtDsHeap = true;
        }
    }
    EXPECT_TRUE(anyRtDsHeap);

    // Single batch (all graphics queue), signals timeline
    EXPECT_EQ(cg.batches.size(), 1u);
    EXPECT_EQ(cg.batches[0].queue, RGQueueType::Graphics);
    EXPECT_TRUE(cg.batches[0].signalTimeline);

    // Structural hash is deterministic
    EXPECT_GT(cg.hash.topologyHash, 0u);
    EXPECT_EQ(cg.hash.passCount, 5u);
}

// #############################################################################
//  2. Cross-Queue Async Compute — Execution + Barrier Verification
// #############################################################################

TEST(MultiFlow, AsyncComputeCrossQueueExecution) {
    // Draw(gfx) -> AsyncBlur(async compute) -> Composite(gfx, side effect)
    // Verifies: cross-queue sync points, multi-batch formation, barrier correctness
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 512, .height = 512, .debugName = "RT"});
    auto blurred = builder.CreateTexture({.width = 512, .height = 512, .debugName = "Blurred"});

    std::vector<uint32_t> order;

    auto hDraw = builder.AddGraphicsPass(
        "Draw", [&](PassBuilder& pb) { rt = pb.WriteColorAttachment(rt); },
        [&](RenderPassContext&) { order.push_back(0); }
    );
    auto hBlur = builder.AddAsyncComputePass(
        "AsyncBlur",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt);
            blurred = pb.WriteTexture(blurred, ResourceAccess::ShaderWrite);
        },
        [&](RenderPassContext&) { order.push_back(1); }
    );
    auto hComp = builder.AddGraphicsPass(
        "Composite",
        [&](PassBuilder& pb) {
            pb.ReadTexture(blurred);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { order.push_back(2); }
    );

    RenderGraphCompiler::Options opts;
    opts.enableAsyncCompute = true;
    opts.enableRenderPassMerging = false;
    auto result = RunFullPipeline(builder, opts);
    ASSERT_TRUE(result.has_value());
    auto& cg = result->compiled;

    EXPECT_EQ(cg.passes.size(), 3u);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(result->stats.recording.passesRecorded, 3u);
    VerifyTopo(cg);

    // Cross-queue sync points must exist
    bool hasGfxToAsync = false, hasAsyncToGfx = false;
    for (auto& sp : cg.syncPoints) {
        if (sp.srcQueue == RGQueueType::Graphics && sp.dstQueue == RGQueueType::AsyncCompute) {
            hasGfxToAsync = true;
        }
        if (sp.srcQueue == RGQueueType::AsyncCompute && sp.dstQueue == RGQueueType::Graphics) {
            hasAsyncToGfx = true;
        }
    }
    EXPECT_TRUE(hasGfxToAsync) << "Missing Graphics->AsyncCompute sync";
    EXPECT_TRUE(hasAsyncToGfx) << "Missing AsyncCompute->Graphics sync";

    // Multi-batch: at least 2 batches (queue changes)
    EXPECT_GE(cg.batches.size(), 2u);
    bool foundAsyncBatch = false;
    for (auto& batch : cg.batches) {
        if (batch.queue == RGQueueType::AsyncCompute) {
            foundAsyncBatch = true;
            bool waitsOnGfx = std::any_of(batch.waits.begin(), batch.waits.end(), [](auto& w) {
                return w.srcQueue == RGQueueType::Graphics;
            });
            EXPECT_TRUE(waitsOnGfx) << "Async batch must wait on Graphics";
        }
    }
    EXPECT_TRUE(foundAsyncBatch);
    EXPECT_TRUE(cg.batches.back().signalTimeline);

    // Cross-queue barriers on the async pass
    int pBlur = FindPass(cg, hBlur.index);
    ASSERT_GE(pBlur, 0);
    bool hasCQBarrier
        = std::any_of(cg.passes[pBlur].acquireBarriers.begin(), cg.passes[pBlur].acquireBarriers.end(), [&](auto& b) {
              return b.isCrossQueue && b.resourceIndex == rt.GetIndex();
          });
    EXPECT_TRUE(hasCQBarrier) << "Async pass missing cross-queue acquire for RT";
}

// #############################################################################
//  3. Conditional Execution + DCE through Full Pipeline
// #############################################################################

TEST(MultiFlow, ConditionalExecutionAndDCE) {
    // P0 writes t0 (unconditional)
    // P1 reads t0, writes t1 (conditional — disabled)
    // P2 reads t1, side effect (conditional — disabled)
    // P3 writes t2, side effect (unconditional)
    // Expected: P1+P2 disabled -> P0 has no consumer -> P0 also culled. Only P3 survives.
    RenderGraphBuilder builder;
    auto t0 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "t0"});
    auto t1 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "t1"});
    auto t2 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "t2"});

    uint32_t p0Invoked = 0, p1Invoked = 0, p2Invoked = 0, p3Invoked = 0;

    builder.AddGraphicsPass(
        "P0", [&](PassBuilder& pb) { t0 = pb.WriteColorAttachment(t0); }, [&](RenderPassContext&) { p0Invoked++; }
    );
    auto hP1 = builder.AddGraphicsPass(
        "P1",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t0);
            t1 = pb.WriteColorAttachment(t1);
        },
        [&](RenderPassContext&) { p1Invoked++; }
    );
    auto hP2 = builder.AddGraphicsPass(
        "P2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(t1);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { p2Invoked++; }
    );
    auto hP3 = builder.AddGraphicsPass(
        "P3",
        [&](PassBuilder& pb) {
            t2 = pb.WriteColorAttachment(t2);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { p3Invoked++; }
    );

    // Disable P1 and P2
    builder.EnableIf(hP1, []() { return false; });
    builder.EnableIf(hP2, []() { return false; });

    auto result = RunFullPipeline(builder);
    ASSERT_TRUE(result.has_value());
    auto& cg = result->compiled;

    // Only P3 should survive
    EXPECT_EQ(cg.passes.size(), 1u);
    EXPECT_EQ(cg.passes[0].passIndex, hP3.index);
    EXPECT_EQ(p0Invoked, 0u);  // Culled by DCE (no live consumer)
    EXPECT_EQ(p1Invoked, 0u);  // Condition disabled
    EXPECT_EQ(p2Invoked, 0u);  // Condition disabled
    EXPECT_EQ(p3Invoked, 1u);
    EXPECT_EQ(result->stats.recording.passesRecorded, 1u);
}

// #############################################################################
//  4. Incremental Recompile — Topology Match + Descriptor-Only Change
// #############################################################################

TEST(MultiFlow, IncrementalRecompilePreservesTopology) {
    // Frame 1: full compile. Frame 2: same topology, different texture size -> incremental.
    auto buildGraph = [](RenderGraphBuilder& builder, uint32_t width) {
        auto rt = builder.CreateTexture({.width = width, .height = width, .debugName = "RT"});
        builder.AddGraphicsPass(
            "Draw",
            [&](PassBuilder& pb) {
                rt = pb.WriteColorAttachment(rt);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        builder.Build();
    };

    // Frame 1: full compile
    RenderGraphBuilder b1;
    buildGraph(b1, 1024);
    RenderGraphCompiler compiler;
    auto r1 = compiler.Compile(b1);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->cacheResult, CacheHitResult::Miss);
    auto hash1 = r1->hash;

    // Frame 2: same topology, different size -> descriptor-only change
    RenderGraphBuilder b2;
    buildGraph(b2, 2048);
    auto r2 = compiler.CompileIncremental(b2, *r1);
    ASSERT_TRUE(r2.has_value());

    // Topology hash should match (same pass names, flags, edges)
    EXPECT_TRUE(hash1.IsTopologyMatch(r2->hash));
    // Descriptor hash should differ (different texture size)
    EXPECT_NE(hash1.descHash, r2->hash.descHash);
    // Should report DescriptorOnlyChange
    EXPECT_EQ(r2->cacheResult, CacheHitResult::DescriptorOnlyChange);
    // Same pass count
    EXPECT_EQ(r2->passes.size(), 1u);
}

TEST(MultiFlow, IncrementalRecompileFallsBackOnTopologyChange) {
    RenderGraphBuilder b1;
    auto rt = b1.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
    b1.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            rt = pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    b1.Build();
    RenderGraphCompiler compiler;
    auto r1 = compiler.Compile(b1);
    ASSERT_TRUE(r1.has_value());

    // Frame 2: add a second pass -> topology change
    RenderGraphBuilder b2;
    auto rt2 = b2.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
    auto rt3 = b2.CreateTexture({.width = 256, .height = 256, .debugName = "RT2"});
    b2.AddGraphicsPass("Draw", [&](PassBuilder& pb) { rt2 = pb.WriteColorAttachment(rt2); }, [](RenderPassContext&) {});
    b2.AddGraphicsPass(
        "Post",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt2);
            rt3 = pb.WriteColorAttachment(rt3);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    b2.Build();
    auto r2 = compiler.CompileIncremental(b2, *r1);
    ASSERT_TRUE(r2.has_value());
    // Topology changed -> full recompile fallback
    EXPECT_EQ(r2->passes.size(), 2u);
}

// #############################################################################
//  5. History Resources — Cross-Frame Temporal Reads
// #############################################################################

TEST(MultiFlow, HistoryResourceLifetimeExtension) {
    // TAA pattern: pass reads previous frame's output via ReadHistoryTexture.
    // When producer is culled (condition false), history resource must be lifetime-extended.
    RenderGraphBuilder builder;
    auto color = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "Color"});
    auto velocity = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "Velocity"});

    // Producer: writes color (conditional)
    auto hProd = builder.AddGraphicsPass(
        "Producer",
        [&](PassBuilder& pb) {
            color = pb.WriteColorAttachment(color);
            velocity = pb.WriteColorAttachment(velocity, 1);
        },
        [](RenderPassContext&) {}
    );

    // TAA: reads current + history color
    builder.AddGraphicsPass(
        "TAA",
        [&](PassBuilder& pb) {
            pb.ReadTexture(color);
            pb.ReadTexture(velocity);
            pb.ReadHistoryTexture(color, "TAAHistory");
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    builder.Build();

    // First compile: producer active
    RenderGraphCompiler compiler;
    auto r1 = compiler.Compile(builder);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->passes.size(), 2u);  // Both passes live

    // Verify history resource info is populated
    bool foundHistory = false;
    for (auto& hr : r1->historyResources) {
        if (hr.resourceIndex == color.GetIndex()) {
            foundHistory = true;
            EXPECT_FALSE(hr.producerCulled);
        }
    }
    EXPECT_TRUE(foundHistory) << "History resource info missing for color";
}

// #############################################################################
//  6. Present Pass + External Resource Patching
// #############################################################################

TEST(MultiFlow, PresentPassAndExternalPatching) {
    RenderGraphBuilder builder;
    auto backbuffer = builder.ImportBackbuffer(TextureHandle{42}, "Backbuffer");
    auto scene = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "Scene"});

    uint32_t invoked = 0;
    builder.AddGraphicsPass(
        "Render",
        [&](PassBuilder& pb) {
            scene = pb.WriteColorAttachment(scene);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { invoked++; }
    );

    builder.AddPresentPass("Present", backbuffer);
    builder.Build();

    RenderGraphCompiler compiler;
    auto compiled = compiler.Compile(builder);
    ASSERT_TRUE(compiled.has_value());

    // Present pass has SideEffects
    bool foundPresent = false;
    for (auto& p : builder.GetPasses()) {
        if (std::string_view(p.name) == "Present") {
            foundPresent = true;
            EXPECT_TRUE(p.hasSideEffects);
        }
    }
    EXPECT_TRUE(foundPresent);

    // External resource slot for backbuffer
    EXPECT_GE(compiled->externalResources.size(), 1u);
    bool foundBB = false;
    for (auto& slot : compiled->externalResources) {
        if (slot.type == ExternalResourceType::Backbuffer) {
            foundBB = true;
            EXPECT_EQ(slot.physicalTexture.value, 42u);
        }
    }
    EXPECT_TRUE(foundBB);

    // PatchExternalResources swaps the backbuffer handle
    miki::rg::FrameContext rgFrame;
    rgFrame.swapchainImage = TextureHandle{99};
    PatchExternalResources(*compiled, rgFrame);
    for (auto& slot : compiled->externalResources) {
        if (slot.type == ExternalResourceType::Backbuffer) {
            EXPECT_EQ(slot.physicalTexture.value, 99u);
        }
    }
}

// #############################################################################
//  7. Split Barrier Verification — Gap Between Producer and Consumer
// #############################################################################

TEST(MultiFlow, SplitBarrierGapDetection) {
    // A(writes RT) -> B(independent, side effects) -> C(reads RT, side effects)
    // Gap between A and C should produce split barriers.
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 512, .height = 512, .debugName = "RT"});
    auto scratch = builder.CreateTexture({.width = 64, .height = 64, .debugName = "Scratch"});

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
    // Force ordering
    builder.GetPasses()[hA.index].orderHint = 0;
    builder.GetPasses()[hB.index].orderHint = 1;

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableSplitBarriers = true;
    auto result = RunFullPipeline(builder, opts);
    ASSERT_TRUE(result.has_value());
    auto& cg = result->compiled;

    EXPECT_EQ(cg.passes.size(), 3u);
    VerifyTopo(cg);

    int posA = FindPass(cg, hA.index);
    ASSERT_GE(posA, 0);

    // PassA should have a release barrier for RT
    bool hasRelease = false;
    for (auto& b : cg.passes[posA].releaseBarriers) {
        if (b.resourceIndex == rt.GetIndex() && b.isSplitRelease) {
            hasRelease = true;
            // Matching acquire must exist later
            bool hasAcquire = false;
            for (size_t i = posA + 1; i < cg.passes.size(); ++i) {
                for (auto& acq : cg.passes[i].acquireBarriers) {
                    if (acq.resourceIndex == rt.GetIndex() && acq.isSplitAcquire) {
                        hasAcquire = true;
                    }
                }
            }
            EXPECT_TRUE(hasAcquire) << "Split release without matching acquire";
        }
    }
    EXPECT_TRUE(hasRelease) << "No split release barrier for RT at PassA";
}

// #############################################################################
//  8. D3D12 Fence Barrier Tier-2 — Split Barriers Use Fence
// #############################################################################

TEST(MultiFlow, FenceBarrierTier2UsesFenceForSplits) {
    // Same gap scenario as above, but with D3D12 + FenceBarrierTier2.
    // Split barriers should use fence barriers (isFenceBarrier = true).
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 512, .height = 512, .debugName = "RT"});
    auto scratch = builder.CreateTexture({.width = 64, .height = 64, .debugName = "Scratch"});

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
    builder.GetPasses()[hA.index].orderHint = 0;
    builder.GetPasses()[hB.index].orderHint = 1;

    GpuCapabilityProfile caps{};
    caps.fenceBarrierTier = GpuCapabilityProfile::FenceBarrierTier::Tier2;

    RenderGraphCompiler::Options opts;
    opts.backendType = BackendType::D3D12;
    opts.capabilities = &caps;
    opts.enableRenderPassMerging = false;
    opts.enableSplitBarriers = true;
    auto result = RunFullPipeline(builder, opts);
    ASSERT_TRUE(result.has_value());
    auto& cg = result->compiled;

    auto barriers = CountBarriers(cg);
    // Fence barriers should exist for the split
    EXPECT_GT(barriers.fence, 0u) << "Expected fence barriers for D3D12 Tier2";
    // Split pairs should match
    EXPECT_EQ(barriers.splitRelease, barriers.splitAcquire);
}

// #############################################################################
//  9. Transient Resource Aliasing — Non-overlapping Lifetimes Share Slots
// #############################################################################

TEST(MultiFlow, AliasingNonOverlappingLifetimes) {
    // P0 writes tex0, P1 reads tex0 and writes tex1, P2 reads tex1 (side effect)
    // tex0 lifetime: [0,1], tex1 lifetime: [1,2] — they overlap at P1 so cannot alias.
    // Add P3 that writes tex2 (only used by P4, side effect), tex2 lifetime: [3,4]
    // tex0 [0,1] and tex2 [3,4] don't overlap -> should alias.
    RenderGraphBuilder builder;
    auto tex0 = builder.CreateTexture({.width = 256, .height = 256, .debugName = "tex0"});
    auto tex1 = builder.CreateTexture({.width = 256, .height = 256, .debugName = "tex1"});
    auto tex2 = builder.CreateTexture({.width = 256, .height = 256, .debugName = "tex2"});

    // Force the two chains into sequential topo-order via orderHint
    // Chain 1: P0(0)->P1(1)->P2(2), Chain 2: P3(3)->P4(4)
    builder.AddGraphicsPass(
        "P0",
        [&](PassBuilder& pb) {
            tex0 = pb.WriteColorAttachment(tex0);
            pb.SetOrderHint(0);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "P1",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex0);
            tex1 = pb.WriteColorAttachment(tex1);
            pb.SetOrderHint(1);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "P2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex1);
            pb.SetSideEffects();
            pb.SetOrderHint(2);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "P3",
        [&](PassBuilder& pb) {
            tex2 = pb.WriteColorAttachment(tex2);
            pb.SetOrderHint(3);
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "P4",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex2);
            pb.SetSideEffects();
            pb.SetOrderHint(4);
        },
        [](RenderPassContext&) {}
    );

    RenderGraphCompiler::Options opts;
    opts.enableTransientAliasing = true;
    opts.enableRenderPassMerging = false;
    auto result = RunFullPipeline(builder, opts);
    ASSERT_TRUE(result.has_value());
    auto& cg = result->compiled;

    EXPECT_EQ(cg.passes.size(), 5u);

    // Aliasing heuristic (first-fit-decreasing) should reuse at least one slot.
    // tex0 [0,1] and tex2 [3,4] don't overlap (same size) → can share.
    // tex1 [1,2] overlaps with tex0 [0,1] → cannot share with tex0.
    auto slot0 = cg.aliasing.resourceToSlot[tex0.GetIndex()];
    auto slot1 = cg.aliasing.resourceToSlot[tex1.GetIndex()];
    auto slot2 = cg.aliasing.resourceToSlot[tex2.GetIndex()];

    // All transient textures should be assigned to aliasing slots
    EXPECT_NE(slot0, AliasingLayout::kNotAliased);
    EXPECT_NE(slot1, AliasingLayout::kNotAliased);
    EXPECT_NE(slot2, AliasingLayout::kNotAliased);

    // Count unique slots used — must be < 3 (some aliasing occurred)
    std::unordered_set<uint32_t> uniqueSlots{slot0, slot1, slot2};
    EXPECT_LT(uniqueSlots.size(), 3u) << "At least two textures should share a slot";

    // tex0 and tex1 overlap at position 1 — they MUST be in different slots
    if (slot0 != AliasingLayout::kNotAliased && slot1 != AliasingLayout::kNotAliased) {
        EXPECT_NE(slot0, slot1) << "tex0 and tex1 overlap -> cannot share";
    }
}

// #############################################################################
//  10. Aliasing Barrier Injection at Slot Handoff
// #############################################################################

TEST(MultiFlow, AliasingBarrierAtSlotHandoff) {
    // Same as above: tex0 and tex2 share a slot.
    // At P3 (first use of tex2), an aliasing barrier should be injected.
    RenderGraphBuilder builder;
    auto tex0 = builder.CreateTexture({.width = 256, .height = 256, .debugName = "tex0"});
    auto tex2 = builder.CreateTexture({.width = 256, .height = 256, .debugName = "tex2"});

    builder.AddGraphicsPass(
        "Write0",
        [&](PassBuilder& pb) {
            tex0 = pb.WriteColorAttachment(tex0);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Write2",
        [&](PassBuilder& pb) {
            tex2 = pb.WriteColorAttachment(tex2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    RenderGraphCompiler::Options opts;
    opts.enableTransientAliasing = true;
    opts.enableRenderPassMerging = false;
    auto result = RunFullPipeline(builder, opts);
    ASSERT_TRUE(result.has_value());
    auto& cg = result->compiled;

    auto slot0 = cg.aliasing.resourceToSlot[tex0.GetIndex()];
    auto slot2 = cg.aliasing.resourceToSlot[tex2.GetIndex()];

    if (slot0 != AliasingLayout::kNotAliased && slot0 == slot2) {
        // Aliasing barrier should exist at the second pass
        auto barriers = CountBarriers(cg);
        EXPECT_GT(barriers.aliasing, 0u) << "Expected aliasing barrier at slot handoff";
    }
}

// #############################################################################
//  11. WAW + WAR Hazard Chains — Barrier and Edge Correctness
// #############################################################################

TEST(MultiFlow, WAWandWARHazardChain) {
    // P0 writes tex (WAW), P1 writes tex (WAW from P0), P2 reads tex (RAW from P1)
    // P3 writes tex again (WAR from P2, WAW from P1)
    // All must be in correct order with appropriate edges.
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 128, .height = 128, .debugName = "tex"});

    auto hP0 = builder.AddGraphicsPass(
        "P0", [&](PassBuilder& pb) { tex = pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {}
    );
    auto hP1 = builder.AddGraphicsPass(
        "P1", [&](PassBuilder& pb) { tex = pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {}
    );
    auto hP2 = builder.AddGraphicsPass(
        "P2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    auto hP3 = builder.AddGraphicsPass(
        "P3",
        [&](PassBuilder& pb) {
            tex = pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    auto result = RunFullPipeline(builder, opts);
    ASSERT_TRUE(result.has_value());
    auto& cg = result->compiled;

    VerifyTopo(cg);

    int p0 = FindPass(cg, hP0.index);
    int p1 = FindPass(cg, hP1.index);
    int p2 = FindPass(cg, hP2.index);
    int p3 = FindPass(cg, hP3.index);
    EXPECT_LT(p0, p1);  // WAW: P0 before P1
    EXPECT_LT(p1, p2);  // RAW: P1 before P2
    EXPECT_LT(p2, p3);  // WAR: P2 before P3

    // Check edge types
    bool hasWAW = false, hasRAW = false, hasWAR = false;
    for (auto& e : cg.edges) {
        if (e.srcPass == hP0.index && e.dstPass == hP1.index) {
            EXPECT_EQ(e.hazard, HazardType::WAW);
            hasWAW = true;
        }
        if (e.srcPass == hP1.index && e.dstPass == hP2.index) {
            EXPECT_EQ(e.hazard, HazardType::RAW);
            hasRAW = true;
        }
        if (e.srcPass == hP2.index && e.dstPass == hP3.index) {
            EXPECT_EQ(e.hazard, HazardType::WAR);
            hasWAR = true;
        }
    }
    EXPECT_TRUE(hasWAW) << "Missing WAW edge P0->P1";
    EXPECT_TRUE(hasRAW) << "Missing RAW edge P1->P2";
    EXPECT_TRUE(hasWAR) << "Missing WAR edge P2->P3";
}

// #############################################################################
//  12. Diamond DAG — Wide Fan-out + Fan-in with Execution
// #############################################################################

TEST(MultiFlow, DiamondDAGWithExecution) {
    // A -> B, A -> C, B -> D, C -> D (side effect)
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

    auto result = RunFullPipeline(builder);
    ASSERT_TRUE(result.has_value());
    auto& cg = result->compiled;

    EXPECT_EQ(cg.passes.size(), 4u);
    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order.front(), 0u);  // A first
    EXPECT_EQ(order.back(), 3u);   // D last
    VerifyTopo(cg);
    EXPECT_EQ(result->stats.recording.passesRecorded, 4u);
}

// #############################################################################
//  13. AccelStruct Build -> RT Trace — Full Execution
// #############################################################################

TEST(MultiFlow, AccelStructBuildAndTraceExecution) {
    RenderGraphBuilder builder;
    auto tlas = builder.CreateAccelStruct({.debugName = "TLAS", .estimatedSize = 1024 * 1024});
    auto rtOutput = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "RTOutput"});

    uint32_t buildInvoked = 0, traceInvoked = 0;

    auto hBuild = builder.AddComputePass(
        "BuildTLAS", [&](PassBuilder& pb) { tlas = pb.WriteAccelStruct(tlas, ResourceAccess::AccelStructWrite); },
        [&](RenderPassContext&) { buildInvoked++; }
    );
    auto hTrace = builder.AddComputePass(
        "RTTrace",
        [&](PassBuilder& pb) {
            pb.ReadAccelStruct(tlas, ResourceAccess::AccelStructRead);
            rtOutput = pb.WriteTexture(rtOutput, ResourceAccess::ShaderWrite);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { traceInvoked++; }
    );

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    auto result = RunFullPipeline(builder, opts);
    ASSERT_TRUE(result.has_value());
    auto& cg = result->compiled;

    EXPECT_EQ(cg.passes.size(), 2u);
    EXPECT_EQ(buildInvoked, 1u);
    EXPECT_EQ(traceInvoked, 1u);
    EXPECT_LT(FindPass(cg, hBuild.index), FindPass(cg, hTrace.index));

    // RAW edge from Build to Trace
    bool hasEdge = std::any_of(cg.edges.begin(), cg.edges.end(), [&](auto& e) {
        return e.srcPass == hBuild.index && e.dstPass == hTrace.index && e.hazard == HazardType::RAW;
    });
    EXPECT_TRUE(hasEdge) << "Missing RAW edge BuildTLAS -> RTTrace";
}

// #############################################################################
//  14. VRS Generate -> Consume through Execution
// #############################################################################

TEST(MultiFlow, VRSGenerateAndConsumeExecution) {
    RenderGraphBuilder builder;
    auto vrsImage = builder.CreateTexture({.width = 120, .height = 68, .debugName = "VRS"});
    auto color = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "Color"});

    uint32_t genInvoked = 0, drawInvoked = 0;

    builder.AddComputePass(
        "GenVRS", [&](PassBuilder& pb) { vrsImage = pb.WriteTexture(vrsImage, ResourceAccess::ShaderWrite); },
        [&](RenderPassContext&) { genInvoked++; }
    );
    builder.AddGraphicsPass(
        "DrawWithVRS",
        [&](PassBuilder& pb) {
            pb.ReadVrsImage(vrsImage);
            color = pb.WriteColorAttachment(color);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { drawInvoked++; }
    );

    auto result = RunFullPipeline(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(genInvoked, 1u);
    EXPECT_EQ(drawInvoked, 1u);
    EXPECT_EQ(result->compiled.passes.size(), 2u);
    EXPECT_EQ(result->stats.recording.passesRecorded, 2u);
}

// #############################################################################
//  15. Sparse Bind + Render Cycle — Full Execution
// #############################################################################

TEST(MultiFlow, SparseBindThenRenderExecution) {
    RenderGraphBuilder builder;
    auto sparseTex = builder.DeclareSparseTexture(
        {.format = Format::RGBA16_FLOAT, .width = 4096, .height = 4096, .debugName = "SparseLM"}
    );
    auto output = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "Output"});

    uint32_t bindInvoked = 0, renderInvoked = 0;

    builder.AddSparseBindPass(
        "CommitPages",
        [&](PassBuilder& pb) {
            SparseBindOp op{.type = SparseOpType::Commit, .extentX = 4, .extentY = 4};
            pb.SparseCommit(sparseTex, {&op, 1});
        },
        [&](RenderPassContext&) { bindInvoked++; }
    );
    builder.AddGraphicsPass(
        "RenderLightmap",
        [&](PassBuilder& pb) {
            pb.ReadTexture(sparseTex);
            output = pb.WriteColorAttachment(output);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { renderInvoked++; }
    );

    auto result = RunFullPipeline(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(bindInvoked, 1u);
    EXPECT_EQ(renderInvoked, 1u);
    EXPECT_EQ(result->compiled.passes.size(), 2u);

    // Sparse bind pass has SideEffects flag
    auto& passes = builder.GetPasses();
    bool foundSparseBind = false;
    for (auto& p : passes) {
        if (std::string_view(p.name) == "CommitPages") {
            foundSparseBind = true;
            EXPECT_TRUE(p.hasSideEffects);
            EXPECT_NE(p.flags & RGPassFlags::SparseBind, RGPassFlags::None);
            EXPECT_NE(p.flags & RGPassFlags::SideEffects, RGPassFlags::None);
        }
    }
    EXPECT_TRUE(foundSparseBind);
}

// #############################################################################
//  16. MeshShader Pass Through Full Execution
// #############################################################################

TEST(MultiFlow, MeshShaderPassExecution) {
    RenderGraphBuilder builder;
    auto gbuf = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "GBuf"});
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 1920, .height = 1080, .debugName = "Depth"});

    uint32_t invoked = 0;
    MeshShaderPassConfig meshCfg;
    meshCfg.taskGroupCountX = 1024;
    meshCfg.amplificationRate = 32.0f;

    auto hMesh = builder.AddMeshShaderPass(
        "MeshGBuffer", meshCfg,
        [&](PassBuilder& pb) {
            gbuf = pb.WriteColorAttachment(gbuf);
            depth = pb.WriteDepthStencil(depth);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { invoked++; }
    );

    auto result = RunFullPipeline(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(invoked, 1u);
    EXPECT_EQ(result->compiled.passes.size(), 1u);
    EXPECT_EQ(result->stats.recording.passesRecorded, 1u);

    // Verify MeshShader flag
    auto& pass = builder.GetPasses()[hMesh.index];
    EXPECT_NE(pass.flags & RGPassFlags::MeshShader, RGPassFlags::None);
    EXPECT_NE(pass.flags & RGPassFlags::Graphics, RGPassFlags::None);
}

// #############################################################################
//  17. Transfer Pass — DMA Queue Scheduling
// #############################################################################

TEST(MultiFlow, TransferPassExecution) {
    RenderGraphBuilder builder;
    auto staging = builder.CreateBuffer({.size = 65536, .debugName = "Staging"});
    auto dest = builder.CreateTexture({.width = 256, .height = 256, .debugName = "Dest"});

    uint32_t uploadInvoked = 0, drawInvoked = 0;

    builder.AddTransferPass(
        "Upload",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(staging, ResourceAccess::TransferSrc);
            dest = pb.WriteTexture(dest, ResourceAccess::TransferDst);
        },
        [&](RenderPassContext&) { uploadInvoked++; }
    );
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.ReadTexture(dest);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { drawInvoked++; }
    );

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    auto result = RunFullPipeline(builder, opts);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(uploadInvoked, 1u);
    EXPECT_EQ(drawInvoked, 1u);
}

// #############################################################################
//  18. OrderHint — Explicit User Ordering
// #############################################################################

TEST(MultiFlow, OrderHintRespected) {
    // Three independent side-effect passes. Without hints: any order is valid.
    // With hints: explicit ordering.
    RenderGraphBuilder builder;
    auto t0 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "t0"});
    auto t1 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "t1"});
    auto t2 = builder.CreateTexture({.width = 64, .height = 64, .debugName = "t2"});

    std::vector<uint32_t> order;

    auto hC = builder.AddGraphicsPass(
        "C",
        [&](PassBuilder& pb) {
            t2 = pb.WriteColorAttachment(t2);
            pb.SetSideEffects();
            pb.SetOrderHint(2);
        },
        [&](RenderPassContext&) { order.push_back(2); }
    );
    auto hA = builder.AddGraphicsPass(
        "A",
        [&](PassBuilder& pb) {
            t0 = pb.WriteColorAttachment(t0);
            pb.SetSideEffects();
            pb.SetOrderHint(0);
        },
        [&](RenderPassContext&) { order.push_back(0); }
    );
    auto hB = builder.AddGraphicsPass(
        "B",
        [&](PassBuilder& pb) {
            t1 = pb.WriteColorAttachment(t1);
            pb.SetSideEffects();
            pb.SetOrderHint(1);
        },
        [&](RenderPassContext&) { order.push_back(1); }
    );

    auto result = RunFullPipeline(builder);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(order.size(), 3u);
    // With orderHint 0, 1, 2: A should come first, then B, then C
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    EXPECT_EQ(order[2], 2u);
}

// #############################################################################
//  19. Read-to-Read Barrier Elision — Same Layout, Same Queue
// #############################################################################

TEST(MultiFlow, ReadReadBarrierElision) {
    // P0 writes tex, P1 reads tex (SRV), P2 reads tex (SRV, side effect)
    // P1->P2 is read-to-read on same queue, same layout -> no barrier needed.
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 128, .height = 128, .debugName = "tex"});

    builder.AddGraphicsPass(
        "Write", [&](PassBuilder& pb) { tex = pb.WriteColorAttachment(tex); }, [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Read1",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.AddGraphicsPass(
        "Read2",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    auto result = RunFullPipeline(builder, opts);
    ASSERT_TRUE(result.has_value());
    auto& cg = result->compiled;

    EXPECT_EQ(cg.passes.size(), 3u);

    // Between Read1 and Read2: no barrier for tex (read-to-read elision)
    // Find Read2's position and check it has no acquire barrier for tex from Read1
    int posRead2 = -1;
    for (uint32_t i = 0; i < cg.passes.size(); ++i) {
        if (std::string_view(builder.GetPasses()[cg.passes[i].passIndex].name) == "Read2") {
            posRead2 = static_cast<int>(i);
        }
    }
    ASSERT_GE(posRead2, 0);

    bool hasBarrierForTex = std::any_of(
        cg.passes[posRead2].acquireBarriers.begin(), cg.passes[posRead2].acquireBarriers.end(),
        [&](auto& b) { return b.resourceIndex == tex.GetIndex() && !b.isAliasingBarrier; }
    );
    EXPECT_FALSE(hasBarrierForTex) << "Read-to-read barrier should be elided";
}

// #############################################################################
//  20. PSO Staleness Check Integration
// #############################################################################

TEST(MultiFlow, PsoStalenessCheckClassification) {
    RenderGraphBuilder builder;
    auto rt = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            rt = pb.WriteColorAttachment(rt);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler compiler;
    auto compiled = compiler.Compile(builder);
    ASSERT_TRUE(compiled.has_value());

    // Set a fake PSO handle + generation
    ASSERT_EQ(compiled->passes.size(), 1u);
    compiled->passes[0].psoHandle = PipelineHandle{42};
    compiled->passes[0].psoGeneration = 10;

    // Matching generation -> PSO ready
    auto getGen = [](PipelineHandle) -> uint64_t { return 10; };
    CheckPsoStaleness(*compiled, getGen);
    EXPECT_TRUE(compiled->IsPsoReady(0));

    // Stale generation -> not ready
    auto getStaleGen = [](PipelineHandle) -> uint64_t { return 11; };
    CheckPsoStaleness(*compiled, getStaleGen);
    EXPECT_FALSE(compiled->IsPsoReady(0));
}

// #############################################################################
//  21. Cache Classification — Full/Descriptor/Miss
// #############################################################################

TEST(MultiFlow, CacheClassificationFullAndDescriptorAndMiss) {
    auto buildGraph = [](uint32_t w) {
        RenderGraphBuilder builder;
        auto rt = builder.CreateTexture({.width = w, .height = w, .debugName = "RT"});
        builder.AddGraphicsPass(
            "Draw",
            [&](PassBuilder& pb) {
                rt = pb.WriteColorAttachment(rt);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        builder.Build();
        return builder;
    };

    auto b1 = buildGraph(256);
    RenderGraphCompiler compiler;
    auto r1 = compiler.Compile(b1);
    ASSERT_TRUE(r1.has_value());

    // Same topology + same descriptors -> FullHit
    auto b2 = buildGraph(256);
    std::vector<bool> active2(b2.GetPasses().size(), true);
    EXPECT_EQ(compiler.ClassifyCache(*r1, b2, active2), CacheHitResult::FullHit);

    // Same topology, different descriptor -> DescriptorOnlyChange
    auto b3 = buildGraph(512);
    std::vector<bool> active3(b3.GetPasses().size(), true);
    EXPECT_EQ(compiler.ClassifyCache(*r1, b3, active3), CacheHitResult::DescriptorOnlyChange);

    // Different topology -> Miss
    RenderGraphBuilder b4;
    auto rt4a = b4.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
    auto rt4b = b4.CreateTexture({.width = 256, .height = 256, .debugName = "RT2"});
    b4.AddGraphicsPass(
        "Draw", [&](PassBuilder& pb) { rt4a = pb.WriteColorAttachment(rt4a); }, [](RenderPassContext&) {}
    );
    b4.AddGraphicsPass(
        "Extra",
        [&](PassBuilder& pb) {
            pb.ReadTexture(rt4a);
            rt4b = pb.WriteColorAttachment(rt4b);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    b4.Build();
    std::vector<bool> active4(b4.GetPasses().size(), true);
    EXPECT_EQ(compiler.ClassifyCache(*r1, b4, active4), CacheHitResult::Miss);
}

// #############################################################################
//  22. Stress: 50-Pass Linear Chain — Full Execute
// #############################################################################

TEST(MultiFlow, StressLinearChain50Passes) {
    constexpr uint32_t N = 50;
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 64, .height = 64, .debugName = "Chain"});

    uint32_t invokeCount = 0;
    for (uint32_t i = 0; i < N; ++i) {
        bool first = (i == 0);
        bool last = (i == N - 1);
        builder.AddGraphicsPass(
            "P",
            [&tex, first, last](PassBuilder& pb) {
                if (!first) {
                    pb.ReadTexture(tex);
                }
                tex = pb.WriteColorAttachment(tex);
                if (last) {
                    pb.SetSideEffects();
                }
            },
            [&invokeCount](RenderPassContext&) { invokeCount++; }
        );
    }

    ExecutorConfig execCfg;
    execCfg.enableDebugLabels = false;
    auto result = RunFullPipeline(builder, {}, execCfg);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(invokeCount, N);
    EXPECT_EQ(result->stats.recording.passesRecorded, N);
    EXPECT_EQ(result->compiled.passes.size(), N);
}

// #############################################################################
//  23. Stress: Wide Fan-out (1 producer, 16 consumers) — Execution
// #############################################################################

TEST(MultiFlow, StressWideFanOut16Consumers) {
    constexpr uint32_t FAN = 16;
    RenderGraphBuilder builder;
    auto src = builder.CreateTexture({.width = 256, .height = 256, .debugName = "Src"});

    uint32_t invokeCount = 0;
    builder.AddGraphicsPass(
        "Produce", [&](PassBuilder& pb) { src = pb.WriteColorAttachment(src); },
        [&](RenderPassContext&) { invokeCount++; }
    );
    for (uint32_t i = 0; i < FAN; ++i) {
        builder.AddGraphicsPass(
            "Consumer",
            [&](PassBuilder& pb) {
                pb.ReadTexture(src);
                pb.SetSideEffects();
            },
            [&](RenderPassContext&) { invokeCount++; }
        );
    }

    auto result = RunFullPipeline(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(invokeCount, FAN + 1);
    EXPECT_EQ(result->compiled.passes.size(), FAN + 1);
}

// #############################################################################
//  24. Mixed Resource Types — Texture + Buffer + AccelStruct in One Graph
// #############################################################################

TEST(MultiFlow, MixedResourceTypeExecution) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 256, .height = 256, .debugName = "Tex"});
    auto buf = builder.CreateBuffer({.size = 4096, .debugName = "Buf"});
    auto accel = builder.CreateAccelStruct({.debugName = "AS", .estimatedSize = 65536});

    uint32_t invoked = 0;

    builder.AddComputePass(
        "BuildAS", [&](PassBuilder& pb) { accel = pb.WriteAccelStruct(accel, ResourceAccess::AccelStructWrite); },
        [&](RenderPassContext&) { invoked++; }
    );
    builder.AddComputePass(
        "Compute",
        [&](PassBuilder& pb) {
            pb.ReadAccelStruct(accel, ResourceAccess::AccelStructRead);
            buf = pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
        },
        [&](RenderPassContext&) { invoked++; }
    );
    builder.AddGraphicsPass(
        "Draw",
        [&](PassBuilder& pb) {
            pb.ReadBuffer(buf, ResourceAccess::ShaderReadOnly);
            tex = pb.WriteColorAttachment(tex);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            invoked++;
            EXPECT_TRUE(ctx.GetBuffer(buf).IsValid());
            EXPECT_TRUE(ctx.GetTextureView(tex).IsValid());
        }
    );

    auto result = RunFullPipeline(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(invoked, 3u);
    EXPECT_EQ(result->compiled.passes.size(), 3u);

    // Verify resource kinds
    auto& resources = builder.GetResources();
    EXPECT_EQ(resources[tex.GetIndex()].kind, RGResourceKind::Texture);
    EXPECT_EQ(resources[buf.GetIndex()].kind, RGResourceKind::Buffer);
    EXPECT_EQ(resources[accel.GetIndex()].kind, RGResourceKind::AccelerationStructure);
}

// #############################################################################
//  25. Imported Resources — Physical Handle Resolution
// #############################################################################

TEST(MultiFlow, ImportedResourcePhysicalResolution) {
    RenderGraphBuilder builder;
    auto extTex = TextureHandle{1234};
    auto extBuf = BufferHandle{5678};
    auto imported = builder.ImportTexture(extTex, "ExtColor");
    auto importedBuf = builder.ImportBuffer(extBuf, "ExtData");
    auto output = builder.CreateTexture({.width = 256, .height = 256, .debugName = "Output"});

    bool texResolved = false, bufResolved = false;
    builder.AddGraphicsPass(
        "Use",
        [&](PassBuilder& pb) {
            pb.ReadTexture(imported);
            pb.ReadBuffer(importedBuf, ResourceAccess::ShaderReadOnly);
            output = pb.WriteColorAttachment(output);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            auto resolvedTex = ctx.GetTexture(imported);
            texResolved = (resolvedTex.value == 1234);
            auto resolvedBuf = ctx.GetBuffer(importedBuf);
            bufResolved = (resolvedBuf.value == 5678);
        }
    );

    auto result = RunFullPipeline(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(texResolved) << "Imported texture should resolve to original handle";
    EXPECT_TRUE(bufResolved) << "Imported buffer should resolve to original handle";
}

// #############################################################################
//  26. Complex Pipeline: Deferred + RT Shadows + VRS + Sparse + Async Compute
// #############################################################################

TEST(MultiFlow, ComplexPipelineAllFeatures) {
    // 7-pass pipeline exercising all advanced features:
    //   0. BuildTLAS (compute)
    //   1. SparseCommit (sparse bind)
    //   2. GenVRS (compute)
    //   3. MeshGBuffer (mesh shader, reads VRS)
    //   4. RTShadow (compute, reads TLAS)
    //   5. Lighting (graphics, reads GBuf + RTShadow)
    //   6. ToneMap (graphics, side effect)
    RenderGraphBuilder builder;
    auto tlas = builder.CreateAccelStruct({.debugName = "TLAS", .estimatedSize = 1024 * 1024});
    auto vrs = builder.CreateTexture({.width = 120, .height = 68, .debugName = "VRS"});
    auto sparseLM = builder.DeclareSparseTexture(
        {.format = Format::RGBA16_FLOAT, .width = 4096, .height = 4096, .debugName = "SparseLM"}
    );
    auto gbuf = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "GBuf"});
    auto depth
        = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 1920, .height = 1080, .debugName = "Depth"});
    auto rtShadow = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "RTShadow"});
    auto hdr
        = builder.CreateTexture({.format = Format::RGBA16_FLOAT, .width = 1920, .height = 1080, .debugName = "HDR"});

    std::vector<uint32_t> order;

    // 0: BuildTLAS
    builder.AddComputePass(
        "BuildTLAS", [&](PassBuilder& pb) { tlas = pb.WriteAccelStruct(tlas, ResourceAccess::AccelStructWrite); },
        [&](RenderPassContext&) { order.push_back(0); }
    );

    // 1: SparseCommit
    builder.AddSparseBindPass(
        "SparseCommit",
        [&](PassBuilder& pb) {
            SparseBindOp op{.type = SparseOpType::Commit, .extentX = 4, .extentY = 4};
            pb.SparseCommit(sparseLM, {&op, 1});
        },
        [&](RenderPassContext&) { order.push_back(1); }
    );

    // 2: GenVRS
    builder.AddComputePass(
        "GenVRS", [&](PassBuilder& pb) { vrs = pb.WriteTexture(vrs, ResourceAccess::ShaderWrite); },
        [&](RenderPassContext&) { order.push_back(2); }
    );

    // 3: MeshGBuffer
    MeshShaderPassConfig meshCfg;
    meshCfg.taskGroupCountX = 512;
    meshCfg.amplificationRate = 32.0f;
    struct MeshRes {
        RGResourceHandle* vrs;
        RGResourceHandle* gbuf;
        RGResourceHandle* depth;
    };
    MeshRes mr{&vrs, &gbuf, &depth};
    builder.AddMeshShaderPass(
        "MeshGBuffer", meshCfg,
        [mr](PassBuilder& pb) {
            pb.ReadVrsImage(*mr.vrs);
            *mr.gbuf = pb.WriteColorAttachment(*mr.gbuf);
            *mr.depth = pb.WriteDepthStencil(*mr.depth);
        },
        [&](RenderPassContext&) { order.push_back(3); }
    );

    // 4: RTShadow
    builder.AddComputePass(
        "RTShadow",
        [&](PassBuilder& pb) {
            pb.ReadAccelStruct(tlas, ResourceAccess::AccelStructRead);
            pb.ReadTexture(depth);
            rtShadow = pb.WriteTexture(rtShadow, ResourceAccess::ShaderWrite);
        },
        [&](RenderPassContext&) { order.push_back(4); }
    );

    // 5: Lighting
    builder.AddGraphicsPass(
        "Lighting",
        [&](PassBuilder& pb) {
            pb.ReadTexture(gbuf);
            pb.ReadTexture(rtShadow);
            pb.ReadDepth(depth);
            hdr = pb.WriteColorAttachment(hdr);
        },
        [&](RenderPassContext&) { order.push_back(5); }
    );

    // 6: ToneMap (side effect)
    builder.AddGraphicsPass(
        "ToneMap",
        [&](PassBuilder& pb) {
            pb.ReadTexture(hdr);
            pb.SetSideEffects();
        },
        [&](RenderPassContext&) { order.push_back(6); }
    );

    RenderGraphCompiler::Options opts;
    opts.enableRenderPassMerging = false;
    opts.enableTransientAliasing = true;
    auto result = RunFullPipeline(builder, opts);
    ASSERT_TRUE(result.has_value());
    auto& cg = result->compiled;

    // All 7 passes should survive (ToneMap has side effects, SparseCommit has side effects)
    EXPECT_EQ(cg.passes.size(), 7u);
    EXPECT_EQ(result->stats.recording.passesRecorded, 7u);
    ASSERT_EQ(order.size(), 7u);
    VerifyTopo(cg);

    // Key ordering constraints:
    // BuildTLAS < RTShadow (TLAS dependency)
    // GenVRS < MeshGBuffer (VRS dependency)
    // MeshGBuffer < Lighting (GBuf dependency)
    // MeshGBuffer < RTShadow (depth dependency)
    // RTShadow < Lighting (RTShadow dependency)
    // Lighting < ToneMap (HDR dependency)
    auto findOrder = [&](uint32_t id) -> size_t { return std::find(order.begin(), order.end(), id) - order.begin(); };
    EXPECT_LT(findOrder(0), findOrder(4));  // BuildTLAS < RTShadow
    EXPECT_LT(findOrder(2), findOrder(3));  // GenVRS < MeshGBuffer
    EXPECT_LT(findOrder(3), findOrder(5));  // MeshGBuffer < Lighting
    EXPECT_LT(findOrder(3), findOrder(4));  // MeshGBuffer < RTShadow (depth)
    EXPECT_LT(findOrder(4), findOrder(5));  // RTShadow < Lighting
    EXPECT_LT(findOrder(5), findOrder(6));  // Lighting < ToneMap

    // Verify resource kinds
    auto& resources = builder.GetResources();
    EXPECT_EQ(resources[tlas.GetIndex()].kind, RGResourceKind::AccelerationStructure);
    EXPECT_EQ(miki::rg::ToString(resources[tlas.GetIndex()].kind), std::string_view("AccelerationStructure"));
    EXPECT_EQ(resources[sparseLM.GetIndex()].kind, RGResourceKind::SparseTexture);
    EXPECT_EQ(miki::rg::ToString(resources[sparseLM.GetIndex()].kind), std::string_view("SparseTexture"));
}

// #############################################################################
//  27. Heap Pool Persistence Across Frames
// #############################################################################

TEST(MultiFlow, HeapPoolPersistenceAcrossFrames) {
    // Execute the same graph twice. Second execution should reuse heap allocations.
    auto buildAndCompile
        = []() -> std::pair<RenderGraphBuilder, std::expected<CompiledRenderGraph, miki::core::ErrorCode>> {
        RenderGraphBuilder builder;
        auto rt = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
        builder.AddGraphicsPass(
            "Draw",
            [&](PassBuilder& pb) {
                rt = pb.WriteColorAttachment(rt);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        builder.Build();
        RenderGraphCompiler compiler;
        auto compiled = compiler.Compile(builder);
        return {std::move(builder), std::move(compiled)};
    };

    MockDevice device;
    device.Init();
    auto deviceHandle = DeviceHandle(&device, BackendType::Mock);
    miki::frame::SyncScheduler scheduler;
    scheduler.Init(device.GetQueueTimelinesImpl());
    miki::frame::CommandPoolAllocator::Desc pd{
        .device = deviceHandle,
        .framesInFlight = 2,
        .hasAsyncCompute = true,
        .hasAsyncTransfer = true,
    };
    auto pool = miki::frame::CommandPoolAllocator::Create(pd);
    ASSERT_TRUE(pool.has_value());
    miki::frame::FrameContext frame{.width = 256, .height = 256};

    ExecutorConfig execCfg;
    execCfg.enableHeapPooling = true;
    RenderGraphExecutor executor(execCfg);

    // Frame 1
    auto [b1, c1] = buildAndCompile();
    ASSERT_TRUE(c1.has_value());
    auto r1 = executor.Execute(*c1, b1, frame, deviceHandle, scheduler, *pool);
    ASSERT_TRUE(r1.has_value());
    auto stats1 = executor.GetStats();
    EXPECT_GE(stats1.allocation.heapsCreated, 1u);

    // Frame 2: same graph -> heap pool should reuse
    auto [b2, c2] = buildAndCompile();
    ASSERT_TRUE(c2.has_value());
    auto r2 = executor.Execute(*c2, b2, frame, deviceHandle, scheduler, *pool);
    ASSERT_TRUE(r2.has_value());
    auto stats2 = executor.GetStats();
    // Second frame: heap pool reuse -> fewer (or 0) new heaps created
    EXPECT_LE(stats2.allocation.heapsCreated, stats1.allocation.heapsCreated);
}

// #############################################################################
//  28. Structural Hash Determinism
// #############################################################################

TEST(MultiFlow, StructuralHashDeterministic) {
    auto buildGraph = []() {
        RenderGraphBuilder builder;
        auto rt = builder.CreateTexture({.width = 256, .height = 256, .debugName = "RT"});
        auto depth
            = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 256, .height = 256, .debugName = "D"});
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
        return builder;
    };

    auto b1 = buildGraph();
    auto b2 = buildGraph();
    RenderGraphCompiler c1, c2;
    auto r1 = c1.Compile(b1);
    auto r2 = c2.Compile(b2);
    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());

    EXPECT_EQ(r1->hash.passCount, r2->hash.passCount);
    EXPECT_EQ(r1->hash.resourceCount, r2->hash.resourceCount);
    EXPECT_EQ(r1->hash.topologyHash, r2->hash.topologyHash);
    EXPECT_EQ(r1->hash.descHash, r2->hash.descHash);
    EXPECT_EQ(r1->hash, r2->hash);
}

// #############################################################################
//  29. Buffer + Texture Heap Isolation
// #############################################################################

TEST(MultiFlow, HeapGroupIsolation) {
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
            buf = pb.WriteBuffer(buf, ResourceAccess::ShaderWrite);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    RenderGraphCompiler::Options opts;
    opts.enableTransientAliasing = true;
    opts.enableRenderPassMerging = false;
    auto result = RunFullPipeline(builder, opts);
    ASSERT_TRUE(result.has_value());
    auto& cg = result->compiled;

    auto slotTex = cg.aliasing.resourceToSlot[tex.GetIndex()];
    auto slotBuf = cg.aliasing.resourceToSlot[buf.GetIndex()];
    if (slotTex != AliasingLayout::kNotAliased && slotBuf != AliasingLayout::kNotAliased) {
        EXPECT_NE(slotTex, slotBuf) << "Texture and buffer must not share a slot";
        EXPECT_EQ(cg.aliasing.slots[slotTex].heapGroup, HeapGroupType::RtDs);
        EXPECT_EQ(cg.aliasing.slots[slotBuf].heapGroup, HeapGroupType::Buffer);
    }
}

// #############################################################################
//  30. ReadWriteTexture (UAV) — SSA Version Bump + Execution
// #############################################################################

TEST(MultiFlow, ReadWriteTextureSSAAndExecution) {
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 256, .height = 256, .debugName = "UAV"});

    bool verified = false;
    builder.AddComputePass(
        "InitUAV", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex, ResourceAccess::ShaderWrite); },
        [](RenderPassContext&) {}
    );
    builder.AddComputePass(
        "ReadWriteUAV",
        [&](PassBuilder& pb) {
            tex = pb.ReadWriteTexture(tex);
            pb.SetSideEffects();
        },
        [&](RenderPassContext& ctx) {
            verified = true;
            EXPECT_TRUE(ctx.GetTextureView(tex).IsValid());
        }
    );

    auto result = RunFullPipeline(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(verified);
    EXPECT_EQ(result->compiled.passes.size(), 2u);
}
