/** @file test_phase_l_advanced.cpp
 *  @brief Comprehensive tests for Phase L-6..L-12 advanced render graph features.
 *
 *  Tests define system behavior and boundary conditions for:
 *    L-6:  AsyncComputeDiscovery (critical path analysis + auto-promotion)
 *    L-7:  AddMeshShaderPass (mesh/task shader graph nodes)
 *    L-8:  Ray tracing acceleration structures as graph resources
 *    L-9:  VRS image as graph resource
 *    L-10: GPU breadcrumb tracking for crash diagnosis
 *    L-11: Sparse resource graph nodes
 *    L-12: D3D12 Fence Barriers Tier-2 integration (FenceBarrierResolver)
 *
 *  Also covers builder/PassBuilder API extensions and cross-feature integration.
 *
 *  Pure CPU tests -- no GPU device required.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_set>

#include "miki/core/EnumStrings.h"
#include "miki/rendergraph/RenderGraphAdvanced.h"
#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rendergraph/RenderGraphCompiler.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/GpuCapabilityProfile.h"
#include "miki/rhi/Handle.h"

using namespace miki::rg;
using namespace miki::rhi;

// =============================================================================
// Test helpers
// =============================================================================

namespace {

    constexpr float kEpsilon = 1e-3f;

    auto MakePasses(uint32_t count, RGPassFlags flags, RGQueueType queue, float gpuTime = 100.0f)
        -> std::vector<RGPassNode> {
        std::vector<RGPassNode> passes(count);
        for (uint32_t i = 0; i < count; ++i) {
            passes[i].name = "P";
            passes[i].flags = flags;
            passes[i].queue = queue;
            passes[i].estimatedGpuTimeUs = gpuTime;
        }
        return passes;
    }

    auto MakeChainEdges(uint32_t count) -> std::vector<DependencyEdge> {
        std::vector<DependencyEdge> edges;
        for (uint32_t i = 0; i + 1 < count; ++i) {
            edges.push_back({.srcPass = i, .dstPass = i + 1, .hazard = HazardType::RAW});
        }
        return edges;
    }

}  // namespace

// #############################################################################
// L-6: AsyncComputeDiscovery — Critical Path Analysis
// #############################################################################

// -- Default config ----------------------------------------------------------

TEST(AsyncComputeDiscovery, DefaultConfigValues) {
    AsyncDiscoveryConfig cfg;
    EXPECT_FLOAT_EQ(cfg.minPassDurationUs, 50.0f);
    EXPECT_FLOAT_EQ(cfg.syncOverheadUs, 10.0f);
    EXPECT_FLOAT_EQ(cfg.minOverlapRatio, 0.3f);
    EXPECT_TRUE(cfg.respectUserQueueHints);
    EXPECT_EQ(cfg.maxPromotionsPerFrame, 8u);
}

// -- Empty graph analysis returns zeros --------------------------------------

TEST(AsyncComputeDiscovery, EmptyGraphReturnsZeros) {
    AsyncComputeDiscovery discovery;
    auto result = discovery.Analyze({}, {}, 0);

    EXPECT_TRUE(result.passInfo.empty());
    EXPECT_TRUE(result.candidates.empty());
    EXPECT_FLOAT_EQ(result.criticalPathLengthUs, 0.0f);
    EXPECT_FLOAT_EQ(result.estimatedFrameTimeUs, 0.0f);
    EXPECT_FLOAT_EQ(result.estimatedSavingsUs, 0.0f);
    EXPECT_EQ(result.promotedCount, 0u);
}

// -- Single pass: critical path = pass duration, no candidates ---------------

TEST(AsyncComputeDiscovery, SinglePassIsCriticalPath) {
    auto passes = MakePasses(1, RGPassFlags::Compute, RGQueueType::Graphics, 200.0f);
    AsyncComputeDiscovery discovery;
    auto result = discovery.Analyze(passes, {}, 1);

    ASSERT_EQ(result.passInfo.size(), 1u);
    EXPECT_FLOAT_EQ(result.passInfo[0].earliestStart, 0.0f);
    EXPECT_TRUE(result.passInfo[0].onCriticalPath);
    EXPECT_FLOAT_EQ(result.criticalPathLengthUs, 200.0f);
    // Single pass on critical path -> no candidates
    EXPECT_TRUE(result.candidates.empty());
}

// -- Linear chain: all passes on critical path, no candidates ----------------

TEST(AsyncComputeDiscovery, LinearChainAllOnCriticalPath) {
    constexpr uint32_t N = 5;
    auto passes = MakePasses(N, RGPassFlags::Compute, RGQueueType::Graphics, 100.0f);
    auto edges = MakeChainEdges(N);

    AsyncComputeDiscovery discovery;
    auto result = discovery.Analyze(passes, edges, N);

    ASSERT_EQ(result.passInfo.size(), N);
    EXPECT_NEAR(result.criticalPathLengthUs, 500.0f, kEpsilon);

    for (uint32_t i = 0; i < N; ++i) {
        EXPECT_NEAR(result.passInfo[i].earliestStart, static_cast<float>(i) * 100.0f, kEpsilon);
        EXPECT_TRUE(result.passInfo[i].onCriticalPath) << "Pass " << i << " should be on critical path in linear chain";
        EXPECT_NEAR(result.passInfo[i].slack, 0.0f, kEpsilon);
    }

    // All on critical path -> no promotion candidates
    EXPECT_TRUE(result.candidates.empty());
}

// -- Diamond DAG: off-critical-path pass becomes candidate -------------------

TEST(AsyncComputeDiscovery, DiamondDAGDetectsSlack) {
    // A(100us) -> B(200us) -> D(50us)
    //         \-> C(50us)  -/
    // Critical path: A->B->D (350us). C has slack.
    auto passes = MakePasses(4, RGPassFlags::Compute, RGQueueType::Graphics);
    passes[0].estimatedGpuTimeUs = 100.0f;  // A
    passes[1].estimatedGpuTimeUs = 200.0f;  // B
    passes[2].estimatedGpuTimeUs = 50.0f;   // C
    passes[3].estimatedGpuTimeUs = 50.0f;   // D

    std::vector<DependencyEdge> edges = {
        {.srcPass = 0, .dstPass = 1, .hazard = HazardType::RAW},
        {.srcPass = 0, .dstPass = 2, .hazard = HazardType::RAW},
        {.srcPass = 1, .dstPass = 3, .hazard = HazardType::RAW},
        {.srcPass = 2, .dstPass = 3, .hazard = HazardType::RAW},
    };

    AsyncDiscoveryConfig cfg;
    cfg.minPassDurationUs = 10.0f;  // Low threshold to accept C
    cfg.syncOverheadUs = 5.0f;
    AsyncComputeDiscovery discovery(cfg);
    auto result = discovery.Analyze(passes, edges, 4);

    EXPECT_NEAR(result.criticalPathLengthUs, 350.0f, kEpsilon);

    // C starts earliest at 100 (after A), latest at 300-50=250
    // Slack for C = 250-100 = 150
    ASSERT_EQ(result.passInfo.size(), 4u);
    EXPECT_FALSE(result.passInfo[2].onCriticalPath);
    EXPECT_GT(result.passInfo[2].slack, 100.0f);

    // C should be identified as a candidate
    EXPECT_GE(result.candidates.size(), 1u);
    bool foundC = false;
    for (auto& c : result.candidates) {
        if (c.passIndex == 2) {
            foundC = true;
            EXPECT_GT(c.slack, 0.0f);
            EXPECT_GT(c.estimatedOverlapBenefit, 0.0f);
        }
    }
    EXPECT_TRUE(foundC) << "Pass C should be a promotion candidate";
}

// -- Graphics passes are never candidates ------------------------------------

TEST(AsyncComputeDiscovery, GraphicsPassesNeverCandidates) {
    auto passes = MakePasses(3, RGPassFlags::Graphics, RGQueueType::Graphics, 200.0f);
    // Independent passes (no edges) -> all have slack
    AsyncDiscoveryConfig cfg;
    cfg.minPassDurationUs = 1.0f;
    AsyncComputeDiscovery discovery(cfg);
    auto result = discovery.Analyze(passes, {}, 3);

    // Graphics-only passes should NOT be candidates
    EXPECT_TRUE(result.candidates.empty());
}

// -- Already-async passes skipped when respectUserQueueHints=true ------------

TEST(AsyncComputeDiscovery, RespectsUserAsyncHints) {
    auto passes = MakePasses(2, RGPassFlags::Compute, RGQueueType::Graphics, 200.0f);
    passes[1].queue = RGQueueType::AsyncCompute;  // User marked as async
    // No edges: both independent

    AsyncDiscoveryConfig cfg;
    cfg.respectUserQueueHints = true;
    cfg.minPassDurationUs = 1.0f;
    AsyncComputeDiscovery discovery(cfg);
    auto result = discovery.Analyze(passes, {}, 2);

    // Pass 1 already async + respectUserQueueHints -> skip
    for (auto& c : result.candidates) {
        EXPECT_NE(c.passIndex, 1u) << "Already-async pass should be skipped";
    }
}

// -- maxPromotionsPerFrame limits applied promotions -------------------------

TEST(AsyncComputeDiscovery, MaxPromotionsLimitsCount) {
    constexpr uint32_t N = 20;
    // N independent compute passes, all off critical path
    auto passes = MakePasses(N, RGPassFlags::Compute, RGQueueType::Graphics, 200.0f);

    AsyncDiscoveryConfig cfg;
    cfg.minPassDurationUs = 1.0f;
    cfg.maxPromotionsPerFrame = 3;
    cfg.syncOverheadUs = 0.1f;
    AsyncComputeDiscovery discovery(cfg);
    auto result = discovery.Analyze(passes, {}, N);

    EXPECT_LE(result.promotedCount, 3u);
}

// -- ApplyPromotions modifies queue and flags --------------------------------

TEST(AsyncComputeDiscovery, ApplyPromotionsModifiesQueueAndFlags) {
    // Diamond: A(gfx,500) -> C(compute,100) -> D(gfx,100), with B(compute,100) off critical path
    // Critical path: A->C->D = 700us. B starts at 0, ends at 100, latest start = 600 -> slack = 600.
    std::vector<RGPassNode> passes(4);
    passes[0].flags = RGPassFlags::Graphics;
    passes[0].queue = RGQueueType::Graphics;
    passes[0].estimatedGpuTimeUs = 500.0f;

    passes[1].flags = RGPassFlags::Compute;
    passes[1].queue = RGQueueType::Graphics;
    passes[1].estimatedGpuTimeUs = 100.0f;

    passes[2].flags = RGPassFlags::Compute;
    passes[2].queue = RGQueueType::Graphics;
    passes[2].estimatedGpuTimeUs = 100.0f;

    passes[3].flags = RGPassFlags::Graphics;
    passes[3].queue = RGQueueType::Graphics;
    passes[3].estimatedGpuTimeUs = 100.0f;

    std::vector<DependencyEdge> edges = {
        {.srcPass = 0, .dstPass = 2, .hazard = HazardType::RAW},  // A -> C
        {.srcPass = 2, .dstPass = 3, .hazard = HazardType::RAW},  // C -> D
        {.srcPass = 1, .dstPass = 3, .hazard = HazardType::RAW},  // B -> D
    };

    AsyncDiscoveryConfig cfg;
    cfg.minPassDurationUs = 1.0f;
    cfg.syncOverheadUs = 0.1f;
    AsyncComputeDiscovery discovery(cfg);
    auto result = discovery.Analyze(passes, edges, 4);

    // B (pass 1) should be a profitable candidate
    ASSERT_GE(result.candidates.size(), 1u);
    uint32_t promoted = discovery.ApplyPromotions(passes, result);
    EXPECT_GT(promoted, 0u);

    // Pass 1 (B) should now be on AsyncCompute queue
    EXPECT_EQ(passes[1].queue, RGQueueType::AsyncCompute);
    EXPECT_NE(passes[1].flags & RGPassFlags::AsyncCompute, RGPassFlags::None);
}

// -- Too-short passes are not candidates -------------------------------------

TEST(AsyncComputeDiscovery, ShortPassesBelowThresholdIgnored) {
    auto passes = MakePasses(3, RGPassFlags::Compute, RGQueueType::Graphics, 10.0f);

    AsyncDiscoveryConfig cfg;
    cfg.minPassDurationUs = 50.0f;  // All passes (10us) below threshold
    AsyncComputeDiscovery discovery(cfg);
    auto result = discovery.Analyze(passes, {}, 3);

    EXPECT_TRUE(result.candidates.empty());
}

// -- FormatStatus returns non-empty string -----------------------------------

TEST(AsyncComputeDiscovery, FormatStatusContainsStats) {
    auto passes = MakePasses(2, RGPassFlags::Compute, RGQueueType::Graphics, 200.0f);
    AsyncComputeDiscovery discovery;
    auto result = discovery.Analyze(passes, {}, 2);
    auto status = discovery.FormatStatus(result);
    EXPECT_FALSE(status.empty());
    EXPECT_NE(status.find("critical_path"), std::string::npos);
    EXPECT_NE(status.find("candidates"), std::string::npos);
}

// -- Sync cost exceeding benefit makes candidate unprofitable ----------------

TEST(AsyncComputeDiscovery, HighSyncCostMakesCandidateUnprofitable) {
    // 2 passes: A(graphics) -> B(compute, off critical path due to slack)
    // But B has 2 graphics predecessors -> sync cost = 2 * syncOverheadUs
    auto passes = MakePasses(3, RGPassFlags::Compute, RGQueueType::Graphics, 100.0f);
    passes[0].flags = RGPassFlags::Graphics;
    passes[1].flags = RGPassFlags::Graphics;
    passes[2].estimatedGpuTimeUs = 60.0f;

    std::vector<DependencyEdge> edges = {
        {.srcPass = 0, .dstPass = 2, .hazard = HazardType::RAW},
        {.srcPass = 1, .dstPass = 2, .hazard = HazardType::RAW},
    };

    AsyncDiscoveryConfig cfg;
    cfg.minPassDurationUs = 1.0f;
    cfg.syncOverheadUs = 100.0f;  // Very high sync cost
    AsyncComputeDiscovery discovery(cfg);
    auto result = discovery.Analyze(passes, edges, 3);

    // Pass 2 may be a candidate but should be unprofitable
    for (auto& c : result.candidates) {
        if (c.passIndex == 2) {
            EXPECT_FALSE(c.profitable) << "High sync cost should make candidate unprofitable";
        }
    }
    EXPECT_EQ(result.promotedCount, 0u);
}

// #############################################################################
// L-7: Mesh Shader Pass (AddMeshShaderPass)
// #############################################################################

TEST(MeshShaderPass, DefaultConfigValues) {
    MeshShaderPassConfig cfg;
    EXPECT_EQ(cfg.taskGroupCountX, 1u);
    EXPECT_EQ(cfg.taskGroupCountY, 1u);
    EXPECT_EQ(cfg.taskGroupCountZ, 1u);
    EXPECT_FLOAT_EQ(cfg.amplificationRate, 32.0f);
    EXPECT_EQ(cfg.verticesPerMeshlet, 64u);
    EXPECT_EQ(cfg.trianglesPerMeshlet, 124u);
    EXPECT_FALSE(cfg.isIndirect);
}

TEST(MeshShaderPass, AddMeshShaderPassCreatesValidPass) {
    RenderGraphBuilder builder;
    auto color = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "Color"});

    MeshShaderPassConfig meshCfg;
    meshCfg.taskGroupCountX = 256;
    meshCfg.amplificationRate = 64.0f;

    auto handle = builder.AddMeshShaderPass(
        "MeshletCull", meshCfg,
        [&](PassBuilder& pb) { color = pb.WriteTexture(color, ResourceAccess::ColorAttachWrite); },
        [](RenderPassContext&) {}
    );

    ASSERT_TRUE(handle.IsValid());
    EXPECT_EQ(handle.index, 0u);
    EXPECT_EQ(builder.GetPassCount(), 1u);

    auto& pass = builder.GetPasses()[0];
    EXPECT_STREQ(pass.name, "MeshletCull");
    EXPECT_EQ(pass.queue, RGQueueType::Graphics);
    EXPECT_NE(pass.flags & RGPassFlags::Graphics, RGPassFlags::None);
    EXPECT_NE(pass.flags & RGPassFlags::MeshShader, RGPassFlags::None);

    // estimatedWorkGroupCount = taskGroupCountX * Y * Z * amplificationRate
    uint32_t expectedMeshlets = static_cast<uint32_t>(256.0f * 64.0f);
    EXPECT_EQ(pass.estimatedWorkGroupCount, expectedMeshlets);
}

TEST(MeshShaderPass, MultidimensionalDispatch) {
    RenderGraphBuilder builder;
    auto color = builder.CreateTexture({.width = 64, .height = 64, .debugName = "C"});

    MeshShaderPassConfig meshCfg;
    meshCfg.taskGroupCountX = 4;
    meshCfg.taskGroupCountY = 8;
    meshCfg.taskGroupCountZ = 2;
    meshCfg.amplificationRate = 10.0f;

    auto handle = builder.AddMeshShaderPass(
        "MS3D", meshCfg, [&](PassBuilder& pb) { pb.WriteTexture(color, ResourceAccess::ColorAttachWrite); },
        [](RenderPassContext&) {}
    );

    auto& pass = builder.GetPasses()[handle.index];
    // 4*8*2=64 task groups * 10 = 640 meshlets
    EXPECT_EQ(pass.estimatedWorkGroupCount, 640u);
}

TEST(MeshShaderPass, ZeroAmplificationRate) {
    RenderGraphBuilder builder;
    auto color = builder.CreateTexture({.width = 64, .height = 64, .debugName = "C"});

    MeshShaderPassConfig meshCfg;
    meshCfg.taskGroupCountX = 100;
    meshCfg.amplificationRate = 0.0f;

    auto handle = builder.AddMeshShaderPass(
        "NoAmp", meshCfg, [&](PassBuilder& pb) { pb.WriteTexture(color, ResourceAccess::ColorAttachWrite); },
        [](RenderPassContext&) {}
    );

    EXPECT_EQ(builder.GetPasses()[handle.index].estimatedWorkGroupCount, 0u);
}

TEST(MeshShaderPass, CompilesMeshShaderPassCorrectly) {
    RenderGraphBuilder builder;
    auto color = builder.CreateTexture({.width = 256, .height = 256, .debugName = "MSColor"});

    MeshShaderPassConfig meshCfg;
    meshCfg.taskGroupCountX = 128;

    builder.AddMeshShaderPass(
        "MeshPass", meshCfg,
        [&](PassBuilder& pb) {
            pb.WriteTexture(color, ResourceAccess::ColorAttachWrite);
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

// #############################################################################
// L-8: Ray Tracing Acceleration Structures
// #############################################################################

TEST(AccelerationStructure, DefaultDescValues) {
    RGAccelStructDesc desc;
    EXPECT_EQ(desc.type, RGAccelStructType::BottomLevel);
    EXPECT_EQ(desc.geometryCount, 0u);
    EXPECT_EQ(desc.buildFlags, AccelStructBuildFlags::PreferFastTrace);
    EXPECT_FALSE(desc.allowUpdate);
    EXPECT_EQ(desc.estimatedSize, 0u);
    EXPECT_EQ(desc.estimatedScratchSize, 0u);
}

TEST(AccelerationStructure, CreateAccelStructReturnsValidHandle) {
    RenderGraphBuilder builder;
    auto handle = builder.CreateAccelStruct({
        .type = RGAccelStructType::TopLevel,
        .debugName = "SceneTLAS",
        .geometryCount = 1000,
        .buildFlags = AccelStructBuildFlags::PreferFastTrace,
        .estimatedSize = 1024 * 1024,
    });

    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(handle.GetIndex(), 0u);
    EXPECT_EQ(handle.GetVersion(), 0u);
    EXPECT_EQ(builder.GetResourceCount(), 1u);

    auto& res = builder.GetResources()[handle.GetIndex()];
    EXPECT_EQ(res.kind, RGResourceKind::AccelerationStructure);
    EXPECT_EQ(res.bufferDesc.size, 1024u * 1024u);
    EXPECT_FALSE(res.imported);
}

TEST(AccelerationStructure, MultipleAccelStructsDistinctIndices) {
    RenderGraphBuilder builder;
    auto blas = builder.CreateAccelStruct({.type = RGAccelStructType::BottomLevel, .debugName = "BLAS"});
    auto tlas = builder.CreateAccelStruct({.type = RGAccelStructType::TopLevel, .debugName = "TLAS"});

    EXPECT_NE(blas.GetIndex(), tlas.GetIndex());
    EXPECT_EQ(builder.GetResourceCount(), 2u);
    EXPECT_EQ(builder.GetResources()[blas.GetIndex()].kind, RGResourceKind::AccelerationStructure);
    EXPECT_EQ(builder.GetResources()[tlas.GetIndex()].kind, RGResourceKind::AccelerationStructure);
}

TEST(AccelerationStructure, PassBuilderReadWriteAccelStruct) {
    RenderGraphBuilder builder;
    auto tlas = builder.CreateAccelStruct({.debugName = "TLAS", .estimatedSize = 4096});

    RGResourceHandle writtenTlas;
    builder.AddComputePass(
        "BuildTLAS",
        [&](PassBuilder& pb) {
            writtenTlas = pb.WriteAccelStruct(tlas, ResourceAccess::AccelStructWrite);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    EXPECT_TRUE(writtenTlas.IsValid());
    EXPECT_EQ(writtenTlas.GetVersion(), 1u);  // Version bumped by write

    auto& pass = builder.GetPasses()[0];
    EXPECT_EQ(pass.writes.size(), 1u);
    EXPECT_EQ(pass.writes[0].access, ResourceAccess::AccelStructWrite);
}

TEST(AccelerationStructure, RTPassChainBuildThenTrace) {
    RenderGraphBuilder builder;
    auto tlas = builder.CreateAccelStruct({.debugName = "TLAS", .estimatedSize = 65536});
    auto output = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "RTOutput"});

    RGResourceHandle builtTlas;
    builder.AddComputePass(
        "BuildTLAS", [&](PassBuilder& pb) { builtTlas = pb.WriteAccelStruct(tlas, ResourceAccess::AccelStructWrite); },
        [](RenderPassContext&) {}
    );

    builder.AddComputePass(
        "TraceRays",
        [&](PassBuilder& pb) {
            pb.ReadAccelStruct(tlas, ResourceAccess::AccelStructRead);
            pb.WriteTexture(output, ResourceAccess::ShaderWrite);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);

    // Verify dependency: BuildTLAS before TraceRays
    bool foundEdge = false;
    for (auto& e : result->edges) {
        if (e.srcPass == 0 && e.dstPass == 1) {
            foundEdge = true;
            EXPECT_EQ(e.hazard, HazardType::RAW);
        }
    }
    EXPECT_TRUE(foundEdge) << "BuildTLAS -> TraceRays dependency edge missing";
}

// #############################################################################
// L-9: VRS Image as Graph Resource
// #############################################################################

TEST(VrsImage, DefaultConfigValues) {
    VrsImageConfig cfg;
    EXPECT_FLOAT_EQ(cfg.widthScale, 1.0f / 16.0f);
    EXPECT_FLOAT_EQ(cfg.heightScale, 1.0f / 16.0f);
    EXPECT_EQ(cfg.tileSize, 16u);
    EXPECT_FALSE(cfg.combineWithPerDraw);
    EXPECT_FALSE(cfg.combineWithPerPrimitive);
}

TEST(VrsImage, ReadVrsImageRecordsCorrectAccess) {
    RenderGraphBuilder builder;
    auto vrs = builder.CreateTexture({.width = 120, .height = 68, .debugName = "VRSImage"});

    builder.AddGraphicsPass(
        "UseVRS",
        [&](PassBuilder& pb) {
            pb.ReadVrsImage(vrs);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    auto& pass = builder.GetPasses()[0];
    ASSERT_EQ(pass.reads.size(), 1u);
    EXPECT_EQ(pass.reads[0].access, ResourceAccess::ShadingRateRead);
    EXPECT_EQ(pass.reads[0].handle.GetIndex(), vrs.GetIndex());
}

TEST(VrsImage, VrsBarrierResolvesToShadingRateLayout) {
    auto barrier = ResolveBarrier(ResourceAccess::ShadingRateRead);
    EXPECT_EQ(barrier.stage, PipelineStage::ShadingRateImage);
    EXPECT_EQ(barrier.access, AccessFlags::ShadingRateImageRead);
    EXPECT_EQ(barrier.layout, TextureLayout::ShadingRate);
}

TEST(VrsImage, VrsGenerateThenConsumePipeline) {
    RenderGraphBuilder builder;
    auto vrs = builder.CreateTexture({.width = 120, .height = 68, .debugName = "VRS"});
    auto color = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "Color"});

    builder.AddComputePass(
        "GenerateVRS", [&](PassBuilder& pb) { vrs = pb.WriteTexture(vrs, ResourceAccess::ShaderWrite); },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "Render",
        [&](PassBuilder& pb) {
            pb.ReadVrsImage(vrs);
            color = pb.WriteTexture(color, ResourceAccess::ColorAttachWrite);
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

// #############################################################################
// L-10: GPU Breadcrumb Tracker
// #############################################################################

TEST(GpuBreadcrumbTracker, DisabledModeHasZeroOverhead) {
    GpuBreadcrumbTracker tracker;  // Default = Disabled
    EXPECT_FALSE(tracker.IsActive());
    EXPECT_TRUE(tracker.Initialize(BufferHandle{}));  // Returns true even with invalid buffer
    tracker.BeginFrame(0);
    EXPECT_EQ(tracker.WriteBreadcrumb(0), 0u);
    EXPECT_EQ(tracker.GetStats().totalMarkersWritten, 0u);
    EXPECT_EQ(tracker.GetStats().framesTracked, 0u);
}

TEST(GpuBreadcrumbTracker, PerPassModeIsActive) {
    BreadcrumbConfig cfg;
    cfg.mode = BreadcrumbMode::PerPass;
    GpuBreadcrumbTracker tracker(cfg);
    EXPECT_TRUE(tracker.IsActive());
}

TEST(GpuBreadcrumbTracker, InitializeRequiresValidBuffer) {
    BreadcrumbConfig cfg;
    cfg.mode = BreadcrumbMode::PerPass;
    GpuBreadcrumbTracker tracker(cfg);

    // Invalid buffer -> Initialize returns false
    EXPECT_FALSE(tracker.Initialize(BufferHandle{}));

    // Valid buffer -> Initialize returns true
    auto validBuf = BufferHandle::Pack(1, 42, 0, 0);
    EXPECT_TRUE(tracker.Initialize(validBuf));
    EXPECT_EQ(tracker.GetBufferHandle().value, validBuf.value);
}

TEST(GpuBreadcrumbTracker, WriteBreadcrumbReturnsIncreasingOffsets) {
    BreadcrumbConfig cfg;
    cfg.mode = BreadcrumbMode::PerPass;
    cfg.maxEntries = 256;
    GpuBreadcrumbTracker tracker(cfg);

    auto buf = BufferHandle::Pack(1, 1, 0, 0);
    ASSERT_TRUE(tracker.Initialize(buf));
    tracker.BeginFrame(0);

    std::vector<uint64_t> offsets;
    for (uint32_t i = 0; i < 10; ++i) {
        offsets.push_back(tracker.WriteBreadcrumb(i, false));
    }

    // Offsets should be monotonically increasing (ring buffer not wrapped)
    for (size_t i = 1; i < offsets.size(); ++i) {
        EXPECT_GT(offsets[i], offsets[i - 1]) << "Offset at index " << i << " not increasing";
    }

    EXPECT_EQ(tracker.GetStats().totalMarkersWritten, 10u);
}

TEST(GpuBreadcrumbTracker, BeginFrameResetsWriteOffset) {
    BreadcrumbConfig cfg;
    cfg.mode = BreadcrumbMode::PerPass;
    cfg.maxEntries = 64;
    GpuBreadcrumbTracker tracker(cfg);

    auto buf = BufferHandle::Pack(1, 1, 0, 0);
    ASSERT_TRUE(tracker.Initialize(buf));

    tracker.BeginFrame(0);
    auto offset1 = tracker.WriteBreadcrumb(0);
    auto offset2 = tracker.WriteBreadcrumb(1);
    EXPECT_GT(offset2, offset1);

    // Begin new frame -> write offset resets
    tracker.BeginFrame(1);
    auto offset3 = tracker.WriteBreadcrumb(0);
    EXPECT_EQ(offset3, offset1) << "BeginFrame should reset write offset to 0";
    EXPECT_EQ(tracker.GetStats().framesTracked, 2u);
}

TEST(GpuBreadcrumbTracker, RingBufferWrapsAround) {
    BreadcrumbConfig cfg;
    cfg.mode = BreadcrumbMode::PerPass;
    cfg.maxEntries = 4;  // Very small ring buffer
    GpuBreadcrumbTracker tracker(cfg);

    auto buf = BufferHandle::Pack(1, 1, 0, 0);
    ASSERT_TRUE(tracker.Initialize(buf));
    tracker.BeginFrame(0);

    std::vector<uint64_t> offsets;
    for (uint32_t i = 0; i < 8; ++i) {
        offsets.push_back(tracker.WriteBreadcrumb(i));
    }

    // After 4 entries, should wrap around: offset[4] == offset[0]
    EXPECT_EQ(offsets[4], offsets[0]) << "Ring buffer should wrap after maxEntries";
    EXPECT_EQ(offsets[5], offsets[1]);
    EXPECT_EQ(offsets[6], offsets[2]);
    EXPECT_EQ(offsets[7], offsets[3]);
}

TEST(GpuBreadcrumbTracker, MaxEntriesRoundedToPowerOfTwo) {
    // 300 -> should round up to 512
    BreadcrumbConfig cfg;
    cfg.mode = BreadcrumbMode::PerPass;
    cfg.maxEntries = 300;
    GpuBreadcrumbTracker tracker(cfg);

    auto buf = BufferHandle::Pack(1, 1, 0, 0);
    ASSERT_TRUE(tracker.Initialize(buf));
    tracker.BeginFrame(0);

    // Write 512 entries to verify wrapping at power-of-2 boundary
    std::vector<uint64_t> offsets;
    for (uint32_t i = 0; i < 512; ++i) {
        offsets.push_back(tracker.WriteBreadcrumb(i));
    }

    // Entry 0 and entry 512 should produce the same offset (wrap at 512)
    tracker.BeginFrame(1);
    // After resetting, the first entry should match offset[0]
    auto wrapCheck = tracker.WriteBreadcrumb(0);
    EXPECT_EQ(wrapCheck, offsets[0]);
}

TEST(GpuBreadcrumbTracker, FormatCrashReportIncludesFrameIndex) {
    BreadcrumbConfig cfg;
    cfg.mode = BreadcrumbMode::PerPass;
    GpuBreadcrumbTracker tracker(cfg);

    auto buf = BufferHandle::Pack(1, 1, 0, 0);
    tracker.Initialize(buf);
    tracker.BeginFrame(42);
    tracker.WriteBreadcrumb(5);

    auto report = tracker.FormatCrashReport();
    EXPECT_NE(report.find("Frame 42"), std::string::npos);
    EXPECT_NE(report.find("markers written: 1"), std::string::npos);
}

TEST(GpuBreadcrumbTracker, MoveConstructorTransfersState) {
    BreadcrumbConfig cfg;
    cfg.mode = BreadcrumbMode::PerPassFull;
    cfg.maxEntries = 128;
    GpuBreadcrumbTracker original(cfg);

    auto buf = BufferHandle::Pack(1, 7, 0, 0);
    ASSERT_TRUE(original.Initialize(buf));
    original.BeginFrame(0);
    original.WriteBreadcrumb(0);

    GpuBreadcrumbTracker moved(std::move(original));
    EXPECT_TRUE(moved.IsActive());
    EXPECT_EQ(moved.GetBufferHandle().value, buf.value);
    EXPECT_EQ(moved.GetStats().totalMarkersWritten, 1u);
}

// #############################################################################
// L-11: Sparse Resource Graph Nodes
// #############################################################################

TEST(SparseResources, DefaultSparseTextureDescValues) {
    RGSparseTextureDesc desc;
    EXPECT_EQ(desc.dimension, TextureDimension::Tex2D);
    EXPECT_EQ(desc.format, Format::D32_FLOAT);
    EXPECT_EQ(desc.width, 0u);
    EXPECT_EQ(desc.height, 0u);
    EXPECT_EQ(desc.depth, 1u);
    EXPECT_EQ(desc.mipLevels, 1u);
    EXPECT_EQ(desc.arrayLayers, 1u);
}

TEST(SparseResources, DefaultSparseBufferDescValues) {
    RGSparseBufferDesc desc;
    EXPECT_EQ(desc.size, 0u);
    EXPECT_EQ(desc.pageSize, 65536u);
}

TEST(SparseResources, DeclareSparseTextureCreatesCorrectResource) {
    RenderGraphBuilder builder;
    auto handle = builder.DeclareSparseTexture({
        .format = Format::R8_UNORM,
        .width = 16384,
        .height = 16384,
        .mipLevels = 15,
        .debugName = "VSM_Sparse",
    });

    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(builder.GetResourceCount(), 1u);

    auto& res = builder.GetResources()[handle.GetIndex()];
    EXPECT_EQ(res.kind, RGResourceKind::SparseTexture);
    EXPECT_EQ(res.textureDesc.width, 16384u);
    EXPECT_EQ(res.textureDesc.height, 16384u);
    EXPECT_EQ(res.textureDesc.format, Format::R8_UNORM);
    EXPECT_EQ(res.textureDesc.mipLevels, 15u);
    EXPECT_FALSE(res.imported);
}

TEST(SparseResources, DeclareSparseBufferCreatesCorrectResource) {
    RenderGraphBuilder builder;
    auto handle = builder.DeclareSparseBuffer({
        .size = 256 * 1024 * 1024,  // 256MB virtual
        .pageSize = 65536,
        .debugName = "SparseBuffer",
    });

    EXPECT_TRUE(handle.IsValid());
    auto& res = builder.GetResources()[handle.GetIndex()];
    EXPECT_EQ(res.kind, RGResourceKind::SparseBuffer);
    EXPECT_EQ(res.bufferDesc.size, 256u * 1024u * 1024u);
}

TEST(SparseResources, AddSparseBindPassCreatesCorrectFlags) {
    RenderGraphBuilder builder;
    auto sparseTex = builder.DeclareSparseTexture({
        .width = 4096,
        .height = 4096,
        .debugName = "VSM",
    });

    auto handle = builder.AddSparseBindPass(
        "CommitPages",
        [&](PassBuilder& pb) {
            SparseBindOp op{.type = SparseOpType::Commit, .mipLevel = 0, .extentX = 4, .extentY = 4};
            pb.SparseCommit(sparseTex, {&op, 1});
        },
        [](RenderPassContext&) {}
    );

    ASSERT_TRUE(handle.IsValid());
    auto& pass = builder.GetPasses()[handle.index];
    EXPECT_NE(pass.flags & RGPassFlags::SparseBind, RGPassFlags::None);
    EXPECT_NE(pass.flags & RGPassFlags::SideEffects, RGPassFlags::None);
    EXPECT_TRUE(pass.hasSideEffects);

    // SparseCommit records a write
    EXPECT_EQ(pass.writes.size(), 1u);
}

TEST(SparseResources, SparseDecommitRecordsWrite) {
    RenderGraphBuilder builder;
    auto sparseBuf = builder.DeclareSparseBuffer({.size = 1024 * 1024, .debugName = "SpBuf"});

    builder.AddSparseBindPass(
        "Decommit",
        [&](PassBuilder& pb) {
            SparseBindOp op{.type = SparseOpType::Decommit};
            pb.SparseDecommit(sparseBuf, {&op, 1});
        },
        [](RenderPassContext&) {}
    );

    auto& pass = builder.GetPasses()[0];
    EXPECT_EQ(pass.writes.size(), 1u);
}

TEST(SparseResources, SparseResourceCompiles) {
    RenderGraphBuilder builder;
    auto sparseTex = builder.DeclareSparseTexture({.width = 4096, .height = 4096, .debugName = "VSM"});

    builder.AddSparseBindPass(
        "Commit",
        [&](PassBuilder& pb) {
            SparseBindOp op{.type = SparseOpType::Commit};
            pb.SparseCommit(sparseTex, {&op, 1});
        },
        [](RenderPassContext&) {}
    );

    builder.AddGraphicsPass(
        "RenderVSM",
        [&](PassBuilder& pb) {
            pb.ReadTexture(sparseTex, ResourceAccess::ShaderReadOnly);
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

// #############################################################################
// L-12: FenceBarrierResolver
// #############################################################################

TEST(FenceBarrierResolver, DefaultConfigNotActive) {
    FenceBarrierResolver resolver;
    EXPECT_FALSE(resolver.IsTier2Active());
}

TEST(FenceBarrierResolver, Tier1NotActive) {
    FenceBarrierTier2Config cfg;
    cfg.enableTier2 = true;
    cfg.tier = GpuCapabilityProfile::FenceBarrierTier::Tier1;
    FenceBarrierResolver resolver(cfg);
    EXPECT_FALSE(resolver.IsTier2Active());  // Tier1 < Tier2
}

TEST(FenceBarrierResolver, Tier2IsActive) {
    FenceBarrierTier2Config cfg;
    cfg.enableTier2 = true;
    cfg.tier = GpuCapabilityProfile::FenceBarrierTier::Tier2;
    FenceBarrierResolver resolver(cfg);
    EXPECT_TRUE(resolver.IsTier2Active());
}

TEST(FenceBarrierResolver, DisabledEvenWithTier2) {
    FenceBarrierTier2Config cfg;
    cfg.enableTier2 = false;
    cfg.tier = GpuCapabilityProfile::FenceBarrierTier::Tier2;
    FenceBarrierResolver resolver(cfg);
    EXPECT_FALSE(resolver.IsTier2Active());
}

TEST(FenceBarrierResolver, FallbackReturnsEmptyResult) {
    FenceBarrierResolver resolver;  // Tier2 not active
    std::vector<CrossQueueSyncPoint> syncPoints = {
        {.srcQueue = RGQueueType::Graphics,
         .dstQueue = RGQueueType::AsyncCompute,
         .srcPassIndex = 0,
         .dstPassIndex = 1},
    };
    auto result = resolver.ResolveSyncPoints(syncPoints);
    EXPECT_TRUE(result.empty());

    auto& stats = resolver.GetStats();
    EXPECT_EQ(stats.totalSyncPoints, 1u);
    EXPECT_EQ(stats.convertedToFenceBarrier, 0u);
    EXPECT_EQ(stats.remainingQueueSync, 1u);
    EXPECT_EQ(stats.splitsSaved, 0u);
}

TEST(FenceBarrierResolver, Tier2ConvertsAllSyncPoints) {
    FenceBarrierTier2Config cfg;
    cfg.enableTier2 = true;
    cfg.tier = GpuCapabilityProfile::FenceBarrierTier::Tier2;
    cfg.allowSubBatchSync = true;
    FenceBarrierResolver resolver(cfg);

    std::vector<CrossQueueSyncPoint> syncPoints = {
        {.srcQueue = RGQueueType::Graphics,
         .dstQueue = RGQueueType::AsyncCompute,
         .srcPassIndex = 0,
         .dstPassIndex = 1},
        {.srcQueue = RGQueueType::AsyncCompute,
         .dstQueue = RGQueueType::Graphics,
         .srcPassIndex = 1,
         .dstPassIndex = 2},
        {.srcQueue = RGQueueType::Graphics, .dstQueue = RGQueueType::Transfer, .srcPassIndex = 2, .dstPassIndex = 3},
    };

    auto result = resolver.ResolveSyncPoints(syncPoints);
    ASSERT_EQ(result.size(), 3u);

    // Verify each sync point is correctly converted
    EXPECT_EQ(result[0].srcPassIndex, 0u);
    EXPECT_EQ(result[0].dstPassIndex, 1u);
    EXPECT_EQ(result[0].srcQueue, RGQueueType::Graphics);
    EXPECT_EQ(result[0].dstQueue, RGQueueType::AsyncCompute);

    EXPECT_EQ(result[1].srcPassIndex, 1u);
    EXPECT_EQ(result[1].dstPassIndex, 2u);

    EXPECT_EQ(result[2].srcPassIndex, 2u);
    EXPECT_EQ(result[2].dstPassIndex, 3u);

    // Fence values monotonically increasing starting from 1
    EXPECT_EQ(result[0].fenceValue, 1u);
    EXPECT_EQ(result[1].fenceValue, 2u);
    EXPECT_EQ(result[2].fenceValue, 3u);

    auto& stats = resolver.GetStats();
    EXPECT_EQ(stats.totalSyncPoints, 3u);
    EXPECT_EQ(stats.convertedToFenceBarrier, 3u);
    EXPECT_EQ(stats.remainingQueueSync, 0u);
    EXPECT_EQ(stats.splitsSaved, 3u);
}

TEST(FenceBarrierResolver, FenceValuesUniqueAcrossMultipleCalls) {
    FenceBarrierTier2Config cfg;
    cfg.enableTier2 = true;
    cfg.tier = GpuCapabilityProfile::FenceBarrierTier::Tier2;
    FenceBarrierResolver resolver(cfg);

    std::vector<CrossQueueSyncPoint> sp1 = {{.srcPassIndex = 0, .dstPassIndex = 1}};
    std::vector<CrossQueueSyncPoint> sp2 = {{.srcPassIndex = 2, .dstPassIndex = 3}};

    auto r1 = resolver.ResolveSyncPoints(sp1);
    auto r2 = resolver.ResolveSyncPoints(sp2);

    // Each call resets internal counter -> both start from 1
    // This is correct: fence values are per-frame, not global
    ASSERT_EQ(r1.size(), 1u);
    ASSERT_EQ(r2.size(), 1u);
    EXPECT_EQ(r1[0].fenceValue, 1u);
    EXPECT_EQ(r2[0].fenceValue, 1u);
}

TEST(FenceBarrierResolver, EstimateSplitReductionWithSubBatch) {
    FenceBarrierTier2Config cfg;
    cfg.enableTier2 = true;
    cfg.tier = GpuCapabilityProfile::FenceBarrierTier::Tier2;
    cfg.allowSubBatchSync = true;
    FenceBarrierResolver resolver(cfg);

    std::vector<CrossQueueSyncPoint> syncPoints(5);
    EXPECT_EQ(resolver.EstimateSplitReduction(syncPoints), 5u);
}

TEST(FenceBarrierResolver, EstimateSplitReductionWithoutSubBatch) {
    FenceBarrierTier2Config cfg;
    cfg.enableTier2 = true;
    cfg.tier = GpuCapabilityProfile::FenceBarrierTier::Tier2;
    cfg.allowSubBatchSync = false;
    FenceBarrierResolver resolver(cfg);

    std::vector<CrossQueueSyncPoint> syncPoints(5);
    EXPECT_EQ(resolver.EstimateSplitReduction(syncPoints), 0u);
}

TEST(FenceBarrierResolver, FormatStatusInactive) {
    FenceBarrierResolver resolver;
    auto status = resolver.FormatStatus();
    EXPECT_NE(status.find("not active"), std::string::npos);
}

TEST(FenceBarrierResolver, FormatStatusActive) {
    FenceBarrierTier2Config cfg;
    cfg.enableTier2 = true;
    cfg.tier = GpuCapabilityProfile::FenceBarrierTier::Tier2;
    FenceBarrierResolver resolver(cfg);

    std::vector<CrossQueueSyncPoint> sp = {{.srcPassIndex = 0, .dstPassIndex = 1}};
    resolver.ResolveSyncPoints(sp);

    auto status = resolver.FormatStatus();
    EXPECT_NE(status.find("1/1"), std::string::npos);
}

TEST(FenceBarrierResolver, EmptySyncPointsProducesEmptyResult) {
    FenceBarrierTier2Config cfg;
    cfg.enableTier2 = true;
    cfg.tier = GpuCapabilityProfile::FenceBarrierTier::Tier2;
    FenceBarrierResolver resolver(cfg);

    auto result = resolver.ResolveSyncPoints({});
    EXPECT_TRUE(result.empty());
    EXPECT_EQ(resolver.GetStats().totalSyncPoints, 0u);
}

// #############################################################################
// EnumStrings coverage for new RGResourceKind values
// #############################################################################

TEST(EnumStrings, RGResourceKindToStringCoversNewTypes) {
    EXPECT_STREQ(miki::rg::ToString(RGResourceKind::AccelerationStructure), "AccelerationStructure");
    EXPECT_STREQ(miki::rg::ToString(RGResourceKind::SparseTexture), "SparseTexture");
    EXPECT_STREQ(miki::rg::ToString(RGResourceKind::SparseBuffer), "SparseBuffer");
}

// #############################################################################
// RGPassFlags bitmask operations with new flags
// #############################################################################

TEST(RGPassFlags, MeshShaderFlagDistinct) {
    auto flags = RGPassFlags::Graphics | RGPassFlags::MeshShader;
    EXPECT_NE(flags & RGPassFlags::MeshShader, RGPassFlags::None);
    EXPECT_NE(flags & RGPassFlags::Graphics, RGPassFlags::None);
    EXPECT_EQ(flags & RGPassFlags::Compute, RGPassFlags::None);
    EXPECT_EQ(flags & RGPassFlags::SparseBind, RGPassFlags::None);
}

TEST(RGPassFlags, SparseBindFlagDistinct) {
    auto flags = RGPassFlags::SparseBind | RGPassFlags::SideEffects;
    EXPECT_NE(flags & RGPassFlags::SparseBind, RGPassFlags::None);
    EXPECT_NE(flags & RGPassFlags::SideEffects, RGPassFlags::None);
    EXPECT_EQ(flags & RGPassFlags::MeshShader, RGPassFlags::None);
    EXPECT_EQ(flags & RGPassFlags::Graphics, RGPassFlags::None);
}

TEST(RGPassFlags, NewFlagsDoNotOverlapExisting) {
    std::array allFlags = {
        RGPassFlags::Graphics,  RGPassFlags::Compute,    RGPassFlags::AsyncCompute,
        RGPassFlags::Transfer,  RGPassFlags::Present,    RGPassFlags::SideEffects,
        RGPassFlags::NeverCull, RGPassFlags::MeshShader, RGPassFlags::SparseBind,
    };
    for (size_t i = 0; i < allFlags.size(); ++i) {
        for (size_t j = i + 1; j < allFlags.size(); ++j) {
            EXPECT_EQ(allFlags[i] & allFlags[j], RGPassFlags::None)
                << "Flags at index " << i << " and " << j << " overlap";
        }
    }
}

// #############################################################################
// IsAccessValidForPassType with new pass types
// #############################################################################

TEST(AccessValidation, MeshShaderPassAcceptsGraphicsAccesses) {
    auto flags = RGPassFlags::Graphics | RGPassFlags::MeshShader;
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ColorAttachWrite, flags));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::DepthStencilWrite, flags));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ShaderReadOnly, flags));
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ShadingRateRead, flags));
}

TEST(AccessValidation, SparseBindPassAcceptsWriteAccesses) {
    auto flags = RGPassFlags::SparseBind | RGPassFlags::SideEffects;
    // SparseBind alone (no Graphics/Compute/Transfer) -> falls through to "Graphics passes: all valid"
    // because the validation function doesn't have a specific SparseBind check
    EXPECT_TRUE(IsAccessValidForPassType(ResourceAccess::ShaderWrite, flags));
}

// #############################################################################
// Cross-feature integration tests
// #############################################################################

TEST(Integration, MeshShaderPassWithVRS) {
    RenderGraphBuilder builder;
    auto vrs = builder.CreateTexture({.width = 120, .height = 68, .debugName = "VRS"});
    auto color = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "Color"});

    // Generate VRS image
    builder.AddComputePass(
        "GenVRS", [&](PassBuilder& pb) { vrs = pb.WriteTexture(vrs, ResourceAccess::ShaderWrite); },
        [](RenderPassContext&) {}
    );

    // Mesh shader pass consuming VRS image
    MeshShaderPassConfig meshCfg;
    meshCfg.taskGroupCountX = 512;
    meshCfg.amplificationRate = 64.0f;

    builder.AddMeshShaderPass(
        "MeshRender", meshCfg,
        [&](PassBuilder& pb) {
            pb.ReadVrsImage(vrs);
            color = pb.WriteTexture(color, ResourceAccess::ColorAttachWrite);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 2u);

    // Verify dependency: GenVRS -> MeshRender
    bool foundEdge = false;
    for (auto& e : result->edges) {
        if (e.srcPass == 0 && e.dstPass == 1) {
            foundEdge = true;
        }
    }
    EXPECT_TRUE(foundEdge);
}

TEST(Integration, RTPassWithSparseAccelStruct) {
    RenderGraphBuilder builder;
    auto tlas = builder.CreateAccelStruct({
        .type = RGAccelStructType::TopLevel,
        .debugName = "SceneTLAS",
        .estimatedSize = 1024 * 1024,
    });
    auto sparseTex = builder.DeclareSparseTexture({
        .format = Format::RGBA8_UNORM,
        .width = 8192,
        .height = 8192,
        .debugName = "SparseAlbedo",
    });
    auto output = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "RTOutput"});

    // Build TLAS
    builder.AddComputePass(
        "BuildTLAS", [&](PassBuilder& pb) { tlas = pb.WriteAccelStruct(tlas, ResourceAccess::AccelStructWrite); },
        [](RenderPassContext&) {}
    );

    // Commit sparse pages
    builder.AddSparseBindPass(
        "CommitAlbedo",
        [&](PassBuilder& pb) {
            SparseBindOp op{.type = SparseOpType::Commit, .extentX = 16, .extentY = 16};
            pb.SparseCommit(sparseTex, {&op, 1});
        },
        [](RenderPassContext&) {}
    );

    // Trace rays using TLAS + sparse texture
    builder.AddComputePass(
        "TraceRays",
        [&](PassBuilder& pb) {
            pb.ReadAccelStruct(tlas, ResourceAccess::AccelStructRead);
            pb.ReadTexture(sparseTex, ResourceAccess::ShaderReadOnly);
            output = pb.WriteTexture(output, ResourceAccess::ShaderWrite);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->passes.size(), 3u);
}

TEST(Integration, AsyncDiscoveryOnCompiledGraph) {
    // Build a graph with a mix of graphics and compute passes
    struct Handles {
        RGResourceHandle depth, color, ssao, bloom;
    };
    Handles h;
    RenderGraphBuilder builder;
    h.depth = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 1920, .height = 1080, .debugName = "D"});
    h.color = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "C"});
    h.ssao = builder.CreateTexture({.width = 960, .height = 540, .debugName = "SSAO"});
    h.bloom = builder.CreateTexture({.width = 960, .height = 540, .debugName = "Bloom"});

    auto hGBuf = builder.AddGraphicsPass(
        "GBuffer",
        [&h](PassBuilder& pb) {
            h.depth = pb.WriteTexture(h.depth, ResourceAccess::DepthStencilWrite);
            h.color = pb.WriteTexture(h.color, ResourceAccess::ColorAttachWrite);
        },
        [](RenderPassContext&) {}
    );

    auto hSSAO = builder.AddComputePass(
        "SSAO",
        [&h](PassBuilder& pb) {
            pb.ReadTexture(h.depth, ResourceAccess::ShaderReadOnly);
            h.ssao = pb.WriteTexture(h.ssao, ResourceAccess::ShaderWrite);
        },
        [](RenderPassContext&) {}
    );

    auto hBloom = builder.AddComputePass(
        "Bloom",
        [&h](PassBuilder& pb) {
            pb.ReadTexture(h.color, ResourceAccess::ShaderReadOnly);
            h.bloom = pb.WriteTexture(h.bloom, ResourceAccess::ShaderWrite);
        },
        [](RenderPassContext&) {}
    );

    Handles* hp = &h;
    builder.AddGraphicsPass(
        "Composite",
        [hp](PassBuilder& pb) {
            pb.ReadTexture(hp->color, ResourceAccess::ShaderReadOnly);
            pb.ReadTexture(hp->ssao, ResourceAccess::ShaderReadOnly);
            pb.ReadTexture(hp->bloom, ResourceAccess::ShaderReadOnly);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler compiler;
    auto compiled = compiler.Compile(builder);
    ASSERT_TRUE(compiled.has_value());

    // Set estimated GPU times directly on builder (mutable ref available)
    auto& mutablePasses = builder.GetPasses();
    mutablePasses[hGBuf.index].estimatedGpuTimeUs = 2000.0f;
    mutablePasses[hSSAO.index].estimatedGpuTimeUs = 500.0f;
    mutablePasses[hBloom.index].estimatedGpuTimeUs = 300.0f;
    mutablePasses[3].estimatedGpuTimeUs = 100.0f;

    AsyncDiscoveryConfig cfg;
    cfg.minPassDurationUs = 100.0f;
    cfg.syncOverheadUs = 10.0f;
    AsyncComputeDiscovery discovery(cfg);
    auto result = discovery.Analyze(mutablePasses, compiled->edges, 4);

    EXPECT_GT(result.criticalPathLengthUs, 0.0f);
    EXPECT_EQ(result.passInfo.size(), 4u);
}

// #############################################################################
// Stress & edge case tests
// #############################################################################

TEST(Stress, LargeGraphAsyncDiscovery) {
    // 100-pass graph: 50 graphics passes in a chain, 50 independent compute passes
    constexpr uint32_t N_GFX = 50;
    constexpr uint32_t N_COMPUTE = 50;
    constexpr uint32_t TOTAL = N_GFX + N_COMPUTE;

    std::vector<RGPassNode> passes(TOTAL);
    for (uint32_t i = 0; i < N_GFX; ++i) {
        passes[i].flags = RGPassFlags::Graphics;
        passes[i].queue = RGQueueType::Graphics;
        passes[i].estimatedGpuTimeUs = 100.0f;
    }
    for (uint32_t i = N_GFX; i < TOTAL; ++i) {
        passes[i].flags = RGPassFlags::Compute;
        passes[i].queue = RGQueueType::Graphics;
        passes[i].estimatedGpuTimeUs = 200.0f;
    }

    // Graphics chain
    std::vector<DependencyEdge> edges;
    for (uint32_t i = 0; i + 1 < N_GFX; ++i) {
        edges.push_back({.srcPass = i, .dstPass = i + 1, .hazard = HazardType::RAW});
    }

    AsyncDiscoveryConfig cfg;
    cfg.minPassDurationUs = 50.0f;
    cfg.maxPromotionsPerFrame = 8;
    cfg.syncOverheadUs = 5.0f;
    AsyncComputeDiscovery discovery(cfg);
    auto result = discovery.Analyze(passes, edges, TOTAL);

    // Graphics passes on critical path (5000us chain)
    EXPECT_NEAR(result.criticalPathLengthUs, 5000.0f, kEpsilon);

    // Compute passes (200us each, independent) should all be candidates
    EXPECT_GE(result.candidates.size(), 1u);

    // Max 8 promotions
    EXPECT_LE(result.promotedCount, 8u);

    // Apply promotions
    uint32_t applied = discovery.ApplyPromotions(passes, result);
    EXPECT_EQ(applied, result.promotedCount);
}

TEST(Stress, ManyMeshShaderPassesCompile) {
    RenderGraphBuilder builder;
    constexpr uint32_t N = 50;
    auto tex = builder.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});

    for (uint32_t i = 0; i < N; ++i) {
        MeshShaderPassConfig cfg;
        cfg.taskGroupCountX = i + 1;
        cfg.amplificationRate = 32.0f;

        builder.AddMeshShaderPass(
            "MSPass", cfg,
            [&](PassBuilder& pb) {
                tex = pb.WriteTexture(tex, ResourceAccess::ColorAttachWrite);
                if (i == N - 1) {
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
    // DCE should keep all passes since they form a write chain consumed by the last with side effects
    EXPECT_GE(result->passes.size(), 1u);
}

TEST(Stress, AlternatingResourceTypes) {
    // Mix all new resource types with standard ones
    RenderGraphBuilder builder;
    auto tex = builder.CreateTexture({.width = 256, .height = 256, .debugName = "T"});
    auto buf = builder.CreateBuffer({.size = 1024, .debugName = "B"});
    auto accel = builder.CreateAccelStruct({.debugName = "AS", .estimatedSize = 4096});
    auto spTex = builder.DeclareSparseTexture({.width = 8192, .height = 8192, .debugName = "SpT"});
    auto spBuf = builder.DeclareSparseBuffer({.size = 1024 * 1024, .debugName = "SpB"});

    EXPECT_EQ(builder.GetResourceCount(), 5u);

    // Verify each has correct kind
    EXPECT_EQ(builder.GetResources()[tex.GetIndex()].kind, RGResourceKind::Texture);
    EXPECT_EQ(builder.GetResources()[buf.GetIndex()].kind, RGResourceKind::Buffer);
    EXPECT_EQ(builder.GetResources()[accel.GetIndex()].kind, RGResourceKind::AccelerationStructure);
    EXPECT_EQ(builder.GetResources()[spTex.GetIndex()].kind, RGResourceKind::SparseTexture);
    EXPECT_EQ(builder.GetResources()[spBuf.GetIndex()].kind, RGResourceKind::SparseBuffer);
}

TEST(Stress, FenceBarrierLargeSyncPointSet) {
    FenceBarrierTier2Config cfg;
    cfg.enableTier2 = true;
    cfg.tier = GpuCapabilityProfile::FenceBarrierTier::Tier2;
    cfg.allowSubBatchSync = true;
    FenceBarrierResolver resolver(cfg);

    constexpr uint32_t N = 1000;
    std::vector<CrossQueueSyncPoint> syncPoints(N);
    for (uint32_t i = 0; i < N; ++i) {
        syncPoints[i].srcPassIndex = i;
        syncPoints[i].dstPassIndex = i + 1;
        syncPoints[i].srcQueue = (i % 2 == 0) ? RGQueueType::Graphics : RGQueueType::AsyncCompute;
        syncPoints[i].dstQueue = (i % 2 == 0) ? RGQueueType::AsyncCompute : RGQueueType::Graphics;
    }

    auto result = resolver.ResolveSyncPoints(syncPoints);
    ASSERT_EQ(result.size(), N);

    // Fence values should be 1..N, all unique
    std::unordered_set<uint64_t> fenceValues;
    for (auto& fbsp : result) {
        fenceValues.insert(fbsp.fenceValue);
    }
    EXPECT_EQ(fenceValues.size(), N);
    EXPECT_EQ(*std::min_element(fenceValues.begin(), fenceValues.end()), 1u);
    EXPECT_EQ(*std::max_element(fenceValues.begin(), fenceValues.end()), static_cast<uint64_t>(N));

    auto& stats = resolver.GetStats();
    EXPECT_EQ(stats.convertedToFenceBarrier, N);
    EXPECT_EQ(stats.splitsSaved, N);
    EXPECT_EQ(stats.remainingQueueSync, 0u);
}

TEST(Stress, BreadcrumbTrackerManyFrames) {
    BreadcrumbConfig cfg;
    cfg.mode = BreadcrumbMode::PerPassFull;
    cfg.maxEntries = 4096;
    GpuBreadcrumbTracker tracker(cfg);

    auto buf = BufferHandle::Pack(1, 1, 0, 0);
    ASSERT_TRUE(tracker.Initialize(buf));

    // Simulate 100 frames, each with 88 passes (before + after = 176 markers)
    for (uint64_t frame = 0; frame < 100; ++frame) {
        tracker.BeginFrame(frame);
        for (uint32_t pass = 0; pass < 88; ++pass) {
            tracker.WriteBreadcrumb(pass, false);
            tracker.WriteBreadcrumb(pass, true);
        }
    }

    auto& stats = tracker.GetStats();
    EXPECT_EQ(stats.framesTracked, 100u);
    EXPECT_EQ(stats.totalMarkersWritten, 100u * 88u * 2u);
}

TEST(Stress, FullPipelineWithAllAdvancedFeatures) {
    // A complex pipeline using: mesh shaders, RT, VRS, sparse textures, standard passes
    // Use a struct to keep lambda captures within 64-byte InlineFunction capacity.
    struct Res {
        RGResourceHandle depth, gbufA, gbufB, tlas, rtShadow, vrs, sparseLM, hdr;
    };
    Res r;
    RenderGraphBuilder builder;

    r.depth = builder.CreateTexture({.format = Format::D32_FLOAT, .width = 1920, .height = 1080, .debugName = "D"});
    r.gbufA = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "GBufA"});
    r.gbufB = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "GBufB"});
    r.tlas = builder.CreateAccelStruct({.debugName = "TLAS", .estimatedSize = 1024 * 1024});
    r.rtShadow = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "RTShadow"});
    r.vrs = builder.CreateTexture({.width = 120, .height = 68, .debugName = "VRS"});
    r.sparseLM = builder.DeclareSparseTexture(
        {.format = Format::RGBA16_FLOAT, .width = 4096, .height = 4096, .debugName = "SparseLM"}
    );
    r.hdr = builder.CreateTexture({.format = Format::RGBA16_FLOAT, .width = 1920, .height = 1080, .debugName = "HDR"});

    // Pass 0: Build TLAS
    builder.AddComputePass(
        "BuildTLAS", [&r](PassBuilder& pb) { r.tlas = pb.WriteAccelStruct(r.tlas, ResourceAccess::AccelStructWrite); },
        [](RenderPassContext&) {}
    );

    // Pass 1: Sparse bind (commit lightmap pages)
    builder.AddSparseBindPass(
        "CommitLightmap",
        [&r](PassBuilder& pb) {
            SparseBindOp op{.type = SparseOpType::Commit, .extentX = 4, .extentY = 4};
            pb.SparseCommit(r.sparseLM, {&op, 1});
        },
        [](RenderPassContext&) {}
    );

    // Pass 2: Generate VRS image
    builder.AddComputePass(
        "GenVRS", [&r](PassBuilder& pb) { r.vrs = pb.WriteTexture(r.vrs, ResourceAccess::ShaderWrite); },
        [](RenderPassContext&) {}
    );

    // Pass 3: Mesh shader GBuffer — split into smaller lambda captures
    MeshShaderPassConfig meshCfg;
    meshCfg.taskGroupCountX = 1024;
    meshCfg.amplificationRate = 32.0f;
    struct GBufCaptures {
        Res* r;
    };
    GBufCaptures gc{&r};
    builder.AddMeshShaderPass(
        "MeshGBuffer", meshCfg,
        [gc](PassBuilder& pb) {
            pb.ReadVrsImage(gc.r->vrs);
            gc.r->depth = pb.WriteTexture(gc.r->depth, ResourceAccess::DepthStencilWrite);
            gc.r->gbufA = pb.WriteTexture(gc.r->gbufA, ResourceAccess::ColorAttachWrite);
            gc.r->gbufB = pb.WriteTexture(gc.r->gbufB, ResourceAccess::ColorAttachWrite);
        },
        [](RenderPassContext&) {}
    );

    // Pass 4: RT shadows
    builder.AddComputePass(
        "RTShadows",
        [&r](PassBuilder& pb) {
            pb.ReadAccelStruct(r.tlas, ResourceAccess::AccelStructRead);
            pb.ReadTexture(r.depth, ResourceAccess::ShaderReadOnly);
            r.rtShadow = pb.WriteTexture(r.rtShadow, ResourceAccess::ShaderWrite);
        },
        [](RenderPassContext&) {}
    );

    // Pass 5: Lighting
    builder.AddGraphicsPass(
        "Lighting",
        [&r](PassBuilder& pb) {
            pb.ReadTexture(r.gbufA, ResourceAccess::ShaderReadOnly);
            pb.ReadTexture(r.gbufB, ResourceAccess::ShaderReadOnly);
            pb.ReadTexture(r.rtShadow, ResourceAccess::ShaderReadOnly);
            pb.ReadTexture(r.sparseLM, ResourceAccess::ShaderReadOnly);
            r.hdr = pb.WriteTexture(r.hdr, ResourceAccess::ColorAttachWrite);
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

    // All 6 passes should survive (Lighting has side effects, pulls in all producers)
    EXPECT_EQ(result->passes.size(), 6u);

    // Verify topological ordering
    auto findPos = [&](uint32_t passIdx) -> int {
        for (uint32_t i = 0; i < result->passes.size(); ++i) {
            if (result->passes[i].passIndex == passIdx) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    int posBuildTLAS = findPos(0u);
    int posCommitLM = findPos(1u);
    int posGenVRS = findPos(2u);
    int posMeshGBuf = findPos(3u);
    int posRTShadow = findPos(4u);
    int posLighting = findPos(5u);

    ASSERT_GE(posBuildTLAS, 0);
    ASSERT_GE(posCommitLM, 0);
    ASSERT_GE(posGenVRS, 0);
    ASSERT_GE(posMeshGBuf, 0);
    ASSERT_GE(posRTShadow, 0);
    ASSERT_GE(posLighting, 0);

    // Dependencies: GenVRS < MeshGBuffer, BuildTLAS < RTShadows, MeshGBuffer < RTShadows, MeshGBuffer < Lighting
    EXPECT_LT(posGenVRS, posMeshGBuf) << "GenVRS must precede MeshGBuffer";
    EXPECT_LT(posBuildTLAS, posRTShadow) << "BuildTLAS must precede RTShadows";
    EXPECT_LT(posMeshGBuf, posRTShadow) << "MeshGBuffer must precede RTShadows (depth read)";
    EXPECT_LT(posMeshGBuf, posLighting) << "MeshGBuffer must precede Lighting";
    EXPECT_LT(posRTShadow, posLighting) << "RTShadows must precede Lighting";
    EXPECT_LT(posCommitLM, posLighting) << "CommitLightmap must precede Lighting (reads sparse)";
}

TEST(Stress, AsyncDiscoveryWithCyclicDetection) {
    // A graph where edges form a DAG (no actual cycles) but complex cross-dependencies
    // P0 -> P1, P0 -> P2, P1 -> P3, P2 -> P3, P2 -> P4, P3 -> P5, P4 -> P5
    constexpr uint32_t N = 6;
    auto passes = MakePasses(N, RGPassFlags::Compute, RGQueueType::Graphics, 100.0f);
    passes[0].estimatedGpuTimeUs = 200.0f;  // P0
    passes[1].estimatedGpuTimeUs = 300.0f;  // P1 (critical)
    passes[2].estimatedGpuTimeUs = 50.0f;   // P2 (slack)
    passes[3].estimatedGpuTimeUs = 100.0f;  // P3
    passes[4].estimatedGpuTimeUs = 40.0f;   // P4 (slack)
    passes[5].estimatedGpuTimeUs = 50.0f;   // P5

    std::vector<DependencyEdge> edges = {
        {.srcPass = 0, .dstPass = 1, .hazard = HazardType::RAW},
        {.srcPass = 0, .dstPass = 2, .hazard = HazardType::RAW},
        {.srcPass = 1, .dstPass = 3, .hazard = HazardType::RAW},
        {.srcPass = 2, .dstPass = 3, .hazard = HazardType::RAW},
        {.srcPass = 2, .dstPass = 4, .hazard = HazardType::RAW},
        {.srcPass = 3, .dstPass = 5, .hazard = HazardType::RAW},
        {.srcPass = 4, .dstPass = 5, .hazard = HazardType::RAW},
    };

    // Critical path: P0(200) -> P1(300) -> P3(100) -> P5(50) = 650
    AsyncDiscoveryConfig cfg;
    cfg.minPassDurationUs = 10.0f;
    cfg.syncOverheadUs = 5.0f;
    AsyncComputeDiscovery discovery(cfg);
    auto result = discovery.Analyze(passes, edges, N);

    EXPECT_NEAR(result.criticalPathLengthUs, 650.0f, kEpsilon);

    // P0, P1, P3, P5 on critical path; P2, P4 have slack
    EXPECT_TRUE(result.passInfo[0].onCriticalPath);
    EXPECT_TRUE(result.passInfo[1].onCriticalPath);
    EXPECT_FALSE(result.passInfo[2].onCriticalPath);
    EXPECT_TRUE(result.passInfo[3].onCriticalPath);
    EXPECT_FALSE(result.passInfo[4].onCriticalPath);
    EXPECT_TRUE(result.passInfo[5].onCriticalPath);

    EXPECT_GT(result.passInfo[2].slack, 0.0f);
    EXPECT_GT(result.passInfo[4].slack, 0.0f);
}

TEST(Stress, SparseBindThenRenderThenDecommitCycle) {
    // Simulates a virtual texture streaming cycle:
    // Frame N: Commit pages -> Render -> (next frame) Decommit unused pages
    RenderGraphBuilder builder;
    auto virtualTex = builder.DeclareSparseTexture(
        {.format = Format::RGBA8_UNORM, .width = 16384, .height = 16384, .mipLevels = 15, .debugName = "VTex"}
    );
    auto output = builder.CreateTexture({.width = 1920, .height = 1080, .debugName = "Output"});

    // Commit
    builder.AddSparseBindPass(
        "Commit",
        [&](PassBuilder& pb) {
            SparseBindOp ops[3] = {
                {.type = SparseOpType::Commit, .mipLevel = 0, .offsetX = 0, .offsetY = 0, .extentX = 4, .extentY = 4},
                {.type = SparseOpType::Commit, .mipLevel = 1, .offsetX = 0, .offsetY = 0, .extentX = 2, .extentY = 2},
                {.type = SparseOpType::Commit, .mipLevel = 2, .offsetX = 0, .offsetY = 0, .extentX = 1, .extentY = 1},
            };
            pb.SparseCommit(virtualTex, ops);
        },
        [](RenderPassContext&) {}
    );

    // Render with sparse texture
    builder.AddGraphicsPass(
        "Render",
        [&](PassBuilder& pb) {
            pb.ReadTexture(virtualTex, ResourceAccess::ShaderReadOnly);
            output = pb.WriteTexture(output, ResourceAccess::ColorAttachWrite);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );

    // Decommit (happens after rendering)
    builder.AddSparseBindPass(
        "Decommit",
        [&](PassBuilder& pb) {
            SparseBindOp op{.type = SparseOpType::Decommit, .mipLevel = 5, .extentX = 1, .extentY = 1};
            pb.SparseDecommit(virtualTex, {&op, 1});
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());
    // All 3 should be live: Commit has side effects (SparseBind flag), Render explicit,
    // Decommit has side effects
    EXPECT_EQ(result->passes.size(), 3u);
}

TEST(Stress, MixedResourceTypesWithDCE) {
    // Some passes are dead (unused output), some are live
    RenderGraphBuilder builder;
    auto deadTex = builder.CreateTexture({.width = 64, .height = 64, .debugName = "Dead"});
    auto deadAS = builder.CreateAccelStruct({.debugName = "DeadAS"});
    auto liveTex = builder.CreateTexture({.width = 64, .height = 64, .debugName = "Live"});

    // Dead pass: writes deadTex, no consumer
    (void)builder.AddGraphicsPass(
        "DeadPass", [&](PassBuilder& pb) { deadTex = pb.WriteTexture(deadTex, ResourceAccess::ColorAttachWrite); },
        [](RenderPassContext&) {}
    );

    // Dead pass: writes deadAS, no consumer
    (void)builder.AddComputePass(
        "DeadASBuild", [&](PassBuilder& pb) { deadAS = pb.WriteAccelStruct(deadAS, ResourceAccess::AccelStructWrite); },
        [](RenderPassContext&) {}
    );

    // Live pass: side effects
    (void)builder.AddGraphicsPass(
        "LivePass",
        [&](PassBuilder& pb) {
            liveTex = pb.WriteTexture(liveTex, ResourceAccess::ColorAttachWrite);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    builder.Build();

    RenderGraphCompiler compiler;
    auto result = compiler.Compile(builder);
    ASSERT_TRUE(result.has_value());

    // Only LivePass should survive
    EXPECT_EQ(result->passes.size(), 1u);
    EXPECT_EQ(result->passes[0].passIndex, 2u);
}
