/** @file test_phase_j_debug.cpp
 *  @brief Phase J (Debugging & Profiling) comprehensive tests.
 *
 *  Tests define system behavior and boundary conditions for:
 *    J-1: Graphviz DOT export (ExportGraphviz)
 *    J-2: Per-pass GPU timestamps (PassTimingReport, InitTimingReport)
 *    J-3: Barrier audit log (BarrierAuditLog)
 *    J-4: Debug region colors (GetPassDebugColor, PackDebugColorU32)
 *    J-5: Graph diff report (GraphDiffReport)
 *    J-6: RenderGraphValidator
 *    Phase E integration: InitTimingReport, ToJson with nlohmann/json
 *
 *  Pure CPU tests — no GPU device required.
 */

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <string_view>

#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rendergraph/RenderGraphCompiler.h"
#include "miki/rendergraph/RenderGraphDebug.h"
#include "miki/rendergraph/RenderGraphTypes.h"

using namespace miki::rg;
using namespace miki::rhi;
using json = nlohmann::json;

// =============================================================================
// Helpers
// =============================================================================

namespace {

    constexpr uint32_t kW = 64;
    constexpr uint32_t kH = 64;

    auto MakeTextureHandle(uint32_t idx) -> TextureHandle {
        return TextureHandle::Pack(1, idx, 0, 0);
    }

    // --- Simple 2-pass chain: PassA writes tex, PassB reads tex ---
    auto BuildSimpleChain() -> RenderGraphBuilder {
        RenderGraphBuilder b;
        auto tex = b.CreateTexture({.width = kW, .height = kH, .debugName = "Color"});
        (void)b.AddGraphicsPass(
            "PassA", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {}
        );
        (void)b.AddGraphicsPass(
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

    // --- 3-pass chain: Write -> Read/Write -> Read with buffer ---
    auto BuildThreePassChain() -> RenderGraphBuilder {
        RenderGraphBuilder b;
        auto tex = b.CreateTexture({.width = kW, .height = kH, .debugName = "GBuffer"});
        auto buf = b.CreateBuffer({.size = 4096, .debugName = "IndirectArgs"});
        (void)b.AddGraphicsPass(
            "GeometryPass",
            [&](PassBuilder& pb) {
                tex = pb.WriteTexture(tex);
                buf = pb.WriteBuffer(buf);
            },
            [](RenderPassContext&) {}
        );
        (void)b.AddGraphicsPass(
            "LightingPass",
            [&](PassBuilder& pb) {
                pb.ReadTexture(tex);
                tex = pb.WriteTexture(tex);
                pb.ReadBuffer(buf);
            },
            [](RenderPassContext&) {}
        );
        (void)b.AddGraphicsPass(
            "PostProcess",
            [&](PassBuilder& pb) {
                pb.ReadTexture(tex);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        b.Build();
        return b;
    }

    // --- Multi-queue chain: Graphics -> AsyncCompute -> Graphics ---
    auto BuildCrossQueueChain() -> RenderGraphBuilder {
        RenderGraphBuilder b;
        auto tex = b.CreateTexture({.width = kW, .height = kH, .debugName = "Shared"});
        (void)b.AddGraphicsPass(
            "Render", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {}
        );
        (void)b.AddAsyncComputePass(
            "AsyncBlur",
            [&](PassBuilder& pb) {
                pb.ReadTexture(tex);
                tex = pb.WriteTexture(tex);
            },
            [](RenderPassContext&) {}
        );
        (void)b.AddGraphicsPass(
            "Composite",
            [&](PassBuilder& pb) {
                pb.ReadTexture(tex);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        b.Build();
        return b;
    }

    // --- Chain with conditional pass ---
    auto BuildConditionalChain(bool enableOptional) -> RenderGraphBuilder {
        RenderGraphBuilder b;
        auto tex = b.CreateTexture({.width = kW, .height = kH, .debugName = "RT"});
        (void)b.AddGraphicsPass(
            "Base", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {}
        );
        auto optPass = b.AddGraphicsPass(
            "Optional",
            [&](PassBuilder& pb) {
                pb.ReadTexture(tex);
                tex = pb.WriteTexture(tex);
            },
            [](RenderPassContext&) {}
        );
        b.EnableIf(optPass, [enableOptional]() { return enableOptional; });
        (void)b.AddGraphicsPass(
            "Final",
            [&](PassBuilder& pb) {
                pb.ReadTexture(tex);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        b.Build();
        return b;
    }

    // --- Chain with imported backbuffer ---
    auto BuildImportedChain(TextureHandle bbHandle) -> RenderGraphBuilder {
        RenderGraphBuilder b;
        auto bb = b.ImportBackbuffer(bbHandle);
        auto tex = b.CreateTexture({.width = kW, .height = kH, .debugName = "Intermediate"});
        (void)b.AddGraphicsPass(
            "Draw", [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {}
        );
        (void)b.AddGraphicsPass(
            "Present",
            [&](PassBuilder& pb) {
                pb.ReadTexture(tex);
                (void)pb.WriteTexture(bb);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) {}
        );
        b.Build();
        return b;
    }

    auto CompileGraph(RenderGraphBuilder& b) -> CompiledRenderGraph {
        RenderGraphCompiler compiler;
        auto result = compiler.Compile(b);
        EXPECT_TRUE(result.has_value()) << "Compilation failed";
        return std::move(*result);
    }

}  // namespace

// =============================================================================
// J-1: Graphviz DOT export
// =============================================================================

class GraphvizExportTest : public ::testing::Test {
   protected:
    void SetUp() override {
        builder_ = BuildSimpleChain();
        graph_ = CompileGraph(builder_);
    }
    RenderGraphBuilder builder_;
    CompiledRenderGraph graph_;
};

TEST_F(GraphvizExportTest, ContainsDigraphHeader) {
    auto dot = ExportGraphvizString(graph_, builder_);
    EXPECT_NE(dot.find("digraph RenderGraph"), std::string::npos);
    EXPECT_NE(dot.find("rankdir=LR"), std::string::npos);
}

TEST_F(GraphvizExportTest, ContainsPassNodes) {
    auto dot = ExportGraphvizString(graph_, builder_);
    EXPECT_NE(dot.find("PassA"), std::string::npos);
    EXPECT_NE(dot.find("PassB"), std::string::npos);
}

TEST_F(GraphvizExportTest, ContainsResourceNodeWhenEnabled) {
    GraphvizOptions opts;
    opts.showResourceNodes = true;
    auto dot = ExportGraphvizString(graph_, builder_, opts);
    EXPECT_NE(dot.find("Color"), std::string::npos);
}

TEST_F(GraphvizExportTest, OmitsResourceNodeWhenDisabled) {
    GraphvizOptions opts;
    opts.showResourceNodes = false;
    auto dot = ExportGraphvizString(graph_, builder_, opts);
    // Resource node "R0" should not appear as a standalone node
    // (it may still appear in edge labels, but not as "R0 [")
    EXPECT_EQ(dot.find("R0 ["), std::string::npos);
}

TEST_F(GraphvizExportTest, ContainsDependencyEdges) {
    auto dot = ExportGraphvizString(graph_, builder_);
    // Should have at least one P->P dependency edge
    EXPECT_NE(dot.find("-> P"), std::string::npos);
}

TEST_F(GraphvizExportTest, ContainsQueueColorForGraphics) {
    auto dot = ExportGraphvizString(graph_, builder_);
    // Graphics queue color is green: #33cc33
    EXPECT_NE(dot.find("#33cc33"), std::string::npos);
}

TEST_F(GraphvizExportTest, ContainsClusterWhenEnabled) {
    GraphvizOptions opts;
    opts.clusterByQueue = true;
    auto dot = ExportGraphvizString(graph_, builder_, opts);
    EXPECT_NE(dot.find("subgraph cluster_"), std::string::npos);
}

TEST_F(GraphvizExportTest, NoClusterWhenDisabled) {
    GraphvizOptions opts;
    opts.clusterByQueue = false;
    auto dot = ExportGraphvizString(graph_, builder_, opts);
    EXPECT_EQ(dot.find("subgraph cluster_"), std::string::npos);
}

TEST_F(GraphvizExportTest, BarrierCountsInLabel) {
    GraphvizOptions opts;
    opts.showBarrierCounts = true;
    auto dot = ExportGraphvizString(graph_, builder_, opts);
    EXPECT_NE(dot.find("barriers:"), std::string::npos);
}

TEST_F(GraphvizExportTest, CustomTitleApplied) {
    GraphvizOptions opts;
    opts.graphTitle = "MyCustomGraph";
    auto dot = ExportGraphvizString(graph_, builder_, opts);
    EXPECT_NE(dot.find("digraph MyCustomGraph"), std::string::npos);
}

TEST_F(GraphvizExportTest, StreamOutputMatchesStringOutput) {
    std::ostringstream ss;
    ExportGraphviz(graph_, builder_, ss);
    auto fromStream = ss.str();
    auto fromString = ExportGraphvizString(graph_, builder_);
    EXPECT_EQ(fromStream, fromString);
}

TEST(GraphvizCrossQueue, ShowsCrossQueueEdges) {
    auto builder = BuildCrossQueueChain();
    auto graph = CompileGraph(builder);
    GraphvizOptions opts;
    opts.showCrossQueueSync = true;
    auto dot = ExportGraphvizString(graph, builder, opts);
    // Should have red color edges for cross-queue dependencies
    // or at least a sync comment
    bool hasCrossQueue = dot.find("red") != std::string::npos || dot.find("Sync:") != std::string::npos;
    EXPECT_TRUE(hasCrossQueue);
}

TEST(GraphvizImported, ShowsImportedResourceDistinct) {
    auto bb = MakeTextureHandle(42);
    auto builder = BuildImportedChain(bb);
    auto graph = CompileGraph(builder);
    GraphvizOptions opts;
    opts.showExternalResources = true;
    auto dot = ExportGraphvizString(graph, builder, opts);
    // Imported resources use doubleoctagon shape
    EXPECT_NE(dot.find("doubleoctagon"), std::string::npos);
}

TEST(GraphvizCulled, ShowsCulledPassesGrayDashed) {
    auto builder = BuildConditionalChain(false);
    auto graph = CompileGraph(builder);
    GraphvizOptions opts;
    opts.showCulledPasses = true;
    auto dot = ExportGraphvizString(graph, builder, opts);
    // Culled pass should be gray
    EXPECT_NE(dot.find("#cccccc"), std::string::npos);
    EXPECT_NE(dot.find("culled"), std::string::npos);
}

TEST(GraphvizThreePass, AllPassesPresent) {
    auto builder = BuildThreePassChain();
    auto graph = CompileGraph(builder);
    auto dot = ExportGraphvizString(graph, builder);
    EXPECT_NE(dot.find("GeometryPass"), std::string::npos);
    EXPECT_NE(dot.find("LightingPass"), std::string::npos);
    EXPECT_NE(dot.find("PostProcess"), std::string::npos);
}

TEST(GraphvizThreePass, BufferResourceLabeled) {
    auto builder = BuildThreePassChain();
    auto graph = CompileGraph(builder);
    GraphvizOptions opts;
    opts.showResourceNodes = true;
    auto dot = ExportGraphvizString(graph, builder, opts);
    EXPECT_NE(dot.find("IndirectArgs"), std::string::npos);
    EXPECT_NE(dot.find("[buf]"), std::string::npos);
}

// =============================================================================
// J-2: PassTimingReport
// =============================================================================

class PassTimingReportTest : public ::testing::Test {
   protected:
    void SetUp() override {
        builder_ = BuildThreePassChain();
        graph_ = CompileGraph(builder_);
    }
    RenderGraphBuilder builder_;
    CompiledRenderGraph graph_;
};

TEST_F(PassTimingReportTest, PopulateBarrierCountsMatchesCompiled) {
    PassTimingReport report;
    report.PopulateBarrierCounts(graph_);
    ASSERT_EQ(report.passes.size(), graph_.passes.size());
    for (size_t i = 0; i < graph_.passes.size(); ++i) {
        auto expected
            = static_cast<uint32_t>(graph_.passes[i].acquireBarriers.size() + graph_.passes[i].releaseBarriers.size());
        EXPECT_EQ(report.passes[i].barrierCount, expected) << "Barrier count mismatch at pass " << i;
    }
}

TEST_F(PassTimingReportTest, ComputeTotalsSumsCorrectly) {
    PassTimingReport report;
    report.passes.resize(3);
    report.passes[0].gpuTimeMs = 1.5f;
    report.passes[0].cpuRecordMs = 0.3f;
    report.passes[1].gpuTimeMs = 2.5f;
    report.passes[1].cpuRecordMs = 0.7f;
    report.passes[2].gpuTimeMs = 0.5f;
    report.passes[2].cpuRecordMs = 0.1f;
    report.ComputeTotals();
    EXPECT_FLOAT_EQ(report.totalFrameGpuMs, 4.5f);
    EXPECT_FLOAT_EQ(report.totalFrameCpuMs, 1.1f);
}

TEST_F(PassTimingReportTest, FormatTableContainsHeaders) {
    PassTimingReport report;
    report.PopulateBarrierCounts(graph_);
    auto table = report.FormatTable();
    EXPECT_NE(table.find("Pass"), std::string::npos);
    EXPECT_NE(table.find("Queue"), std::string::npos);
    EXPECT_NE(table.find("GPU(ms)"), std::string::npos);
    EXPECT_NE(table.find("CPU(ms)"), std::string::npos);
}

TEST_F(PassTimingReportTest, InitTimingReportSetsNamesAndQueues) {
    PassTimingReport report;
    InitTimingReport(report, graph_, builder_);
    ASSERT_EQ(report.passes.size(), graph_.passes.size());
    for (size_t i = 0; i < graph_.passes.size(); ++i) {
        EXPECT_NE(report.passes[i].name, nullptr) << "Pass " << i << " has null name";
        EXPECT_EQ(report.passes[i].queue, graph_.passes[i].queue);
        // Barrier counts should match PopulateBarrierCounts
        auto expected
            = static_cast<uint32_t>(graph_.passes[i].acquireBarriers.size() + graph_.passes[i].releaseBarriers.size());
        EXPECT_EQ(report.passes[i].barrierCount, expected);
        // GPU/CPU times should be zero-initialized
        EXPECT_FLOAT_EQ(report.passes[i].gpuTimeMs, 0.0f);
        EXPECT_FLOAT_EQ(report.passes[i].cpuRecordMs, 0.0f);
    }
}

TEST(PassTimingEmpty, EmptyGraphProducesEmptyReport) {
    RenderGraphBuilder b;
    b.Build();
    RenderGraphCompiler compiler;
    auto graph = compiler.Compile(b);
    ASSERT_TRUE(graph.has_value());
    PassTimingReport report;
    report.PopulateBarrierCounts(*graph);
    EXPECT_TRUE(report.passes.empty());
    report.ComputeTotals();
    EXPECT_FLOAT_EQ(report.totalFrameGpuMs, 0.0f);
}

// =============================================================================
// J-3: Barrier audit log
// =============================================================================

class BarrierAuditLogTest : public ::testing::Test {
   protected:
    void SetUp() override {
        builder_ = BuildThreePassChain();
        graph_ = CompileGraph(builder_);
        log_.PopulateFromCompiled(graph_, builder_);
    }
    RenderGraphBuilder builder_;
    CompiledRenderGraph graph_;
    BarrierAuditLog log_;
};

TEST_F(BarrierAuditLogTest, EntryCountMatchesCompiledBarriers) {
    uint32_t expected = 0;
    for (auto& cp : graph_.passes) {
        expected += static_cast<uint32_t>(cp.acquireBarriers.size() + cp.releaseBarriers.size());
    }
    EXPECT_EQ(log_.CountTotal(), expected);
}

TEST_F(BarrierAuditLogTest, EntryFieldsCorrect) {
    for (auto& e : log_.entries) {
        EXPECT_NE(e.passName, nullptr);
        // Resource names should exist for valid indices
        // (not all resources might have names, but our test graphs all do)
        if (e.resourceIndex < builder_.GetResources().size()) {
            EXPECT_NE(e.resourceName, nullptr);
        }
    }
}

TEST_F(BarrierAuditLogTest, CountCrossQueueConsistent) {
    uint32_t manual = 0;
    for (auto& e : log_.entries) {
        if (e.isCrossQueue) {
            manual++;
        }
    }
    EXPECT_EQ(log_.CountCrossQueue(), manual);
}

TEST_F(BarrierAuditLogTest, CountSplitConsistent) {
    uint32_t manual = 0;
    for (auto& e : log_.entries) {
        if (e.isSplitRelease || e.isSplitAcquire) {
            manual++;
        }
    }
    EXPECT_EQ(log_.CountSplitBarriers(), manual);
}

TEST_F(BarrierAuditLogTest, CountAliasingConsistent) {
    uint32_t manual = 0;
    for (auto& e : log_.entries) {
        if (e.isAliasingBarrier) {
            manual++;
        }
    }
    EXPECT_EQ(log_.CountAliasingBarriers(), manual);
}

TEST_F(BarrierAuditLogTest, FormatSummaryContainsCounts) {
    auto summary = log_.FormatSummary();
    EXPECT_NE(summary.find("Barriers:"), std::string::npos);
    EXPECT_NE(summary.find("total"), std::string::npos);
    EXPECT_NE(summary.find("cross-queue"), std::string::npos);
    EXPECT_NE(summary.find("split"), std::string::npos);
    EXPECT_NE(summary.find("aliasing"), std::string::npos);
}

TEST_F(BarrierAuditLogTest, ToJsonStructure) {
    auto j = log_.ToJson();
    EXPECT_TRUE(j.contains("frameIndex"));
    EXPECT_TRUE(j.contains("totalCount"));
    EXPECT_TRUE(j.contains("crossQueueCount"));
    EXPECT_TRUE(j.contains("splitCount"));
    EXPECT_TRUE(j.contains("aliasingCount"));
    EXPECT_TRUE(j.contains("entries"));
    EXPECT_TRUE(j["entries"].is_array());
    EXPECT_EQ(j["totalCount"].get<uint32_t>(), log_.CountTotal());
    EXPECT_EQ(j["crossQueueCount"].get<uint32_t>(), log_.CountCrossQueue());
}

TEST_F(BarrierAuditLogTest, ToJsonStringRoundTrip) {
    auto compact = log_.ToJsonString(false);
    auto pretty = log_.ToJsonString(true);
    // Pretty should be longer (has whitespace)
    EXPECT_GT(pretty.size(), compact.size());
    // Both should parse to same structure
    auto j1 = json::parse(compact);
    auto j2 = json::parse(pretty);
    EXPECT_EQ(j1["totalCount"], j2["totalCount"]);
    EXPECT_EQ(j1["entries"].size(), j2["entries"].size());
}

TEST_F(BarrierAuditLogTest, ToJsonEntryFields) {
    if (log_.entries.empty()) {
        GTEST_SKIP() << "No barriers in test graph";
    }
    auto j = log_.ToJson();
    auto& first = j["entries"][0];
    EXPECT_TRUE(first.contains("passIndex"));
    EXPECT_TRUE(first.contains("pass"));
    EXPECT_TRUE(first.contains("resource"));
    EXPECT_TRUE(first.contains("srcLayout"));
    EXPECT_TRUE(first.contains("dstLayout"));
    EXPECT_TRUE(first.contains("crossQueue"));
    EXPECT_TRUE(first.contains("aliasing"));
    EXPECT_TRUE(first.contains("fence"));
    EXPECT_TRUE(first.contains("srcQueue"));
    EXPECT_TRUE(first.contains("dstQueue"));
}

TEST(BarrierAuditEmpty, EmptyGraphProducesEmptyLog) {
    RenderGraphBuilder b;
    b.Build();
    auto graph = CompileGraph(b);
    BarrierAuditLog log;
    log.PopulateFromCompiled(graph, b);
    EXPECT_EQ(log.CountTotal(), 0u);
    auto j = log.ToJson();
    EXPECT_EQ(j["totalCount"].get<uint32_t>(), 0u);
    EXPECT_TRUE(j["entries"].empty());
}

// =============================================================================
// J-4: Debug region colors
// =============================================================================

TEST(DebugColors, GraphicsIsGreen) {
    auto c = GetPassDebugColor(RGQueueType::Graphics);
    EXPECT_GT(c[1], c[0]);        // Green > Red
    EXPECT_GT(c[1], c[2]);        // Green > Blue
    EXPECT_FLOAT_EQ(c[3], 1.0f);  // Alpha = 1
}

TEST(DebugColors, AsyncComputeIsPurple) {
    auto c = GetPassDebugColor(RGQueueType::AsyncCompute);
    EXPECT_GT(c[0], c[1]);  // Red > Green (purple has R and B)
    EXPECT_GT(c[2], c[1]);  // Blue > Green
    EXPECT_FLOAT_EQ(c[3], 1.0f);
}

TEST(DebugColors, TransferIsOrange) {
    auto c = GetPassDebugColor(RGQueueType::Transfer);
    EXPECT_GT(c[0], c[2]);  // Red > Blue (orange)
    EXPECT_GT(c[1], c[2]);  // Green > Blue (orange has some green)
    EXPECT_FLOAT_EQ(c[3], 1.0f);
}

TEST(DebugColors, PackDebugColorU32_FullWhite) {
    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    auto packed = PackDebugColorU32(white);
    EXPECT_EQ(packed, 0xFFFFFFFF);
}

TEST(DebugColors, PackDebugColorU32_FullBlack) {
    float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    auto packed = PackDebugColorU32(black);
    EXPECT_EQ(packed, 0x000000FF);
}

TEST(DebugColors, PackDebugColorU32_PureRed) {
    float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    auto packed = PackDebugColorU32(red);
    EXPECT_EQ(packed, 0xFF0000FF);
}

TEST(DebugColors, PackDebugColorU32_GraphicsColor) {
    auto c = GetPassDebugColor(RGQueueType::Graphics);
    auto packed = PackDebugColorU32(c);
    // Verify round-trip: each channel should be within tolerance
    uint8_t r = (packed >> 24) & 0xFF;
    uint8_t g = (packed >> 16) & 0xFF;
    uint8_t b = (packed >> 8) & 0xFF;
    uint8_t a = packed & 0xFF;
    EXPECT_NEAR(r / 255.0f, c[0], 0.01f);
    EXPECT_NEAR(g / 255.0f, c[1], 0.01f);
    EXPECT_NEAR(b / 255.0f, c[2], 0.01f);
    EXPECT_EQ(a, 255);
}

TEST(DebugColors, AllQueueTypesReturnValidPointers) {
    EXPECT_NE(GetPassDebugColor(RGQueueType::Graphics), nullptr);
    EXPECT_NE(GetPassDebugColor(RGQueueType::AsyncCompute), nullptr);
    EXPECT_NE(GetPassDebugColor(RGQueueType::Transfer), nullptr);
}

TEST(DebugColors, DistinctColorsPerQueue) {
    auto g = GetPassDebugColor(RGQueueType::Graphics);
    auto a = GetPassDebugColor(RGQueueType::AsyncCompute);
    auto t = GetPassDebugColor(RGQueueType::Transfer);
    // At least one channel must differ between each pair
    bool ga_differ = (g[0] != a[0]) || (g[1] != a[1]) || (g[2] != a[2]);
    bool gt_differ = (g[0] != t[0]) || (g[1] != t[1]) || (g[2] != t[2]);
    bool at_differ = (a[0] != t[0]) || (a[1] != t[1]) || (a[2] != t[2]);
    EXPECT_TRUE(ga_differ);
    EXPECT_TRUE(gt_differ);
    EXPECT_TRUE(at_differ);
}

// =============================================================================
// J-5: Graph diff report
// =============================================================================

class GraphDiffReportTest : public ::testing::Test {};

TEST_F(GraphDiffReportTest, IdenticalGraphsNoDiffs) {
    auto b1 = BuildSimpleChain();
    auto b2 = BuildSimpleChain();
    auto g1 = CompileGraph(b1);
    auto g2 = CompileGraph(b2);

    // Build active sets — all passes active
    std::vector<bool> active1(b1.GetPasses().size(), true);
    std::vector<bool> active2(b2.GetPasses().size(), true);

    auto diff = GraphDiffReport::Generate(b1, active1, b2, active2, 100, 5.0f, false);
    EXPECT_FALSE(diff.HasDiffs());
    EXPECT_EQ(diff.frameIndex, 100u);
    EXPECT_FLOAT_EQ(diff.recompilationTimeUs, 5.0f);
    EXPECT_FALSE(diff.wasIncrementalRecompile);
}

TEST_F(GraphDiffReportTest, AddedPassDetected) {
    auto b1 = BuildSimpleChain();
    auto b2 = BuildThreePassChain();
    std::vector<bool> active1(b1.GetPasses().size(), true);
    std::vector<bool> active2(b2.GetPasses().size(), true);

    auto diff = GraphDiffReport::Generate(b1, active1, b2, active2, 42, 10.0f, false);
    EXPECT_TRUE(diff.HasDiffs());
    // b2 has new passes not in b1
    bool foundAdded = std::ranges::any_of(diff.passDiffs, [](auto& d) { return d.action == DiffAction::Added; });
    EXPECT_TRUE(foundAdded);
}

TEST_F(GraphDiffReportTest, RemovedPassDetected) {
    auto b1 = BuildThreePassChain();
    auto b2 = BuildSimpleChain();
    std::vector<bool> active1(b1.GetPasses().size(), true);
    std::vector<bool> active2(b2.GetPasses().size(), true);

    auto diff = GraphDiffReport::Generate(b1, active1, b2, active2, 50, 8.0f, false);
    EXPECT_TRUE(diff.HasDiffs());
    bool foundRemoved = std::ranges::any_of(diff.passDiffs, [](auto& d) { return d.action == DiffAction::Removed; });
    EXPECT_TRUE(foundRemoved);
}

TEST_F(GraphDiffReportTest, ConditionChangeDetected) {
    auto b1 = BuildConditionalChain(true);
    auto b2 = BuildConditionalChain(false);
    auto g1 = CompileGraph(b1);
    auto g2 = CompileGraph(b2);

    // b1: all passes active. b2: "Optional" is inactive
    std::vector<bool> active1(b1.GetPasses().size(), true);
    std::vector<bool> active2(b2.GetPasses().size(), true);
    // Find the "Optional" pass and mark it inactive in b2's set
    for (size_t i = 0; i < b2.GetPasses().size(); ++i) {
        if (b2.GetPasses()[i].name && std::string_view(b2.GetPasses()[i].name) == "Optional") {
            active2[i] = false;
        }
    }

    auto diff = GraphDiffReport::Generate(b1, active1, b2, active2, 77, 3.0f, true);
    EXPECT_TRUE(diff.HasDiffs());
    EXPECT_TRUE(diff.wasIncrementalRecompile);
    // Should find "Optional" as removed
    bool foundOptionalRemoved = std::ranges::any_of(diff.passDiffs, [](auto& d) {
        return d.action == DiffAction::Removed && d.passName != nullptr && std::string_view(d.passName) == "Optional";
    });
    EXPECT_TRUE(foundOptionalRemoved);
}

TEST_F(GraphDiffReportTest, ResourceSizeChangeDetected) {
    RenderGraphBuilder b1;
    auto tex1 = b1.CreateTexture({.width = 64, .height = 64, .debugName = "RT"});
    (void)b1.AddGraphicsPass(
        "Fill",
        [&](PassBuilder& pb) {
            tex1 = pb.WriteTexture(tex1);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    b1.Build();

    RenderGraphBuilder b2;
    auto tex2 = b2.CreateTexture({.width = 128, .height = 128, .debugName = "RT"});
    (void)b2.AddGraphicsPass(
        "Fill",
        [&](PassBuilder& pb) {
            tex2 = pb.WriteTexture(tex2);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    b2.Build();

    std::vector<bool> a1(b1.GetPasses().size(), true);
    std::vector<bool> a2(b2.GetPasses().size(), true);
    auto diff = GraphDiffReport::Generate(b1, a1, b2, a2, 1, 0, false);
    bool foundModified
        = std::ranges::any_of(diff.resourceDiffs, [](auto& d) { return d.action == DiffAction::Modified; });
    EXPECT_TRUE(foundModified);
    // Check the detail mentions size change
    for (auto& d : diff.resourceDiffs) {
        if (d.action == DiffAction::Modified) {
            EXPECT_NE(d.detail.find("64x64"), std::string::npos);
            EXPECT_NE(d.detail.find("128x128"), std::string::npos);
        }
    }
}

TEST_F(GraphDiffReportTest, ToJsonStructure) {
    auto b1 = BuildSimpleChain();
    auto b2 = BuildThreePassChain();
    std::vector<bool> a1(b1.GetPasses().size(), true);
    std::vector<bool> a2(b2.GetPasses().size(), true);
    auto diff = GraphDiffReport::Generate(b1, a1, b2, a2, 99, 15.5f, false);

    auto j = diff.ToJson();
    EXPECT_EQ(j["frameIndex"].get<uint64_t>(), 99u);
    EXPECT_TRUE(j.contains("prevTopologyHash"));
    EXPECT_TRUE(j.contains("newTopologyHash"));
    EXPECT_TRUE(j.contains("recompilationTimeUs"));
    EXPECT_TRUE(j.contains("wasIncrementalRecompile"));
    EXPECT_TRUE(j["passDiffs"].is_array());
    EXPECT_TRUE(j["resourceDiffs"].is_array());
    EXPECT_NEAR(j["recompilationTimeUs"].get<float>(), 15.5f, 0.01f);
    EXPECT_FALSE(j["wasIncrementalRecompile"].get<bool>());
}

TEST_F(GraphDiffReportTest, ToJsonStringParseable) {
    auto b1 = BuildSimpleChain();
    auto b2 = BuildThreePassChain();
    std::vector<bool> a1(b1.GetPasses().size(), true);
    std::vector<bool> a2(b2.GetPasses().size(), true);
    auto diff = GraphDiffReport::Generate(b1, a1, b2, a2, 1, 0, false);
    auto str = diff.ToJsonString(true);
    EXPECT_NO_THROW({
        auto parsed = json::parse(str);
        EXPECT_TRUE(parsed.contains("frameIndex"));
    });
}

TEST_F(GraphDiffReportTest, SummaryContainsPrefix) {
    auto b1 = BuildSimpleChain();
    auto b2 = BuildThreePassChain();
    std::vector<bool> a1(b1.GetPasses().size(), true);
    std::vector<bool> a2(b2.GetPasses().size(), true);
    auto diff = GraphDiffReport::Generate(b1, a1, b2, a2, 123, 5.0f, false);
    auto summary = diff.Summary();
    EXPECT_NE(summary.find("[RenderGraph]"), std::string::npos);
    EXPECT_NE(summary.find("frame 123"), std::string::npos);
    EXPECT_NE(summary.find("full"), std::string::npos);
}

// =============================================================================
// J-6: RenderGraphValidator
// =============================================================================

class ValidatorTest : public ::testing::Test {
   protected:
    RenderGraphValidator validator_;
};

TEST_F(ValidatorTest, CleanGraphPassesValidation) {
    auto builder = BuildSimpleChain();
    auto graph = CompileGraph(builder);
    auto report = validator_.ValidateAll(graph, builder);
    EXPECT_TRUE(report.IsClean()) << report.FormatReport();
    EXPECT_EQ(report.errorCount, 0u);
}

TEST_F(ValidatorTest, ThreePassChainClean) {
    auto builder = BuildThreePassChain();
    auto graph = CompileGraph(builder);
    auto report = validator_.ValidateAll(graph, builder);
    EXPECT_TRUE(report.IsClean()) << report.FormatReport();
}

TEST_F(ValidatorTest, ImportedChainClean) {
    auto bb = MakeTextureHandle(7);
    auto builder = BuildImportedChain(bb);
    auto graph = CompileGraph(builder);
    auto report = validator_.ValidateAll(graph, builder);
    EXPECT_TRUE(report.IsClean()) << report.FormatReport();
}

TEST_F(ValidatorTest, UninitializedReadDetected) {
    // Manually construct a graph where a pass reads a transient resource
    // that was never written
    RenderGraphBuilder b;
    auto tex = b.CreateTexture({.width = kW, .height = kH, .debugName = "Unwritten"});
    (void)b.AddGraphicsPass(
        "Reader",
        [&](PassBuilder& pb) {
            pb.ReadTexture(tex);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    b.Build();
    auto graph = CompileGraph(b);

    auto warnings = validator_.CheckUninitializedReads(graph, b);
    ASSERT_FALSE(warnings.empty());
    EXPECT_EQ(warnings[0].category, ValidationWarning::Category::UninitializedRead);
    EXPECT_NE(warnings[0].detail.find("read before any write"), std::string::npos);
    EXPECT_STREQ(warnings[0].passName, "Reader");
}

TEST_F(ValidatorTest, ImportedReadNotFlaggedAsUninitialized) {
    auto bb = MakeTextureHandle(99);
    auto builder = BuildImportedChain(bb);
    auto graph = CompileGraph(builder);
    auto warnings = validator_.CheckUninitializedReads(graph, builder);
    // Imported resources should NOT trigger uninitialized read
    for (auto& w : warnings) {
        EXPECT_NE(w.category, ValidationWarning::Category::UninitializedRead)
            << "False positive on imported resource: " << w.detail;
    }
}

TEST_F(ValidatorTest, AliasingOverlapDetected) {
    // Construct a CompiledRenderGraph with overlapping lifetimes in same slot
    CompiledRenderGraph graph;
    graph.aliasing.resourceToSlot = {0, 0};  // Both resources in slot 0
    graph.lifetimes = {
        {.resourceIndex = 0, .firstPass = 0, .lastPass = 2},
        {.resourceIndex = 1, .firstPass = 1, .lastPass = 3},  // Overlaps with resource 0
    };

    auto warnings = validator_.ValidateAliasing(graph);
    ASSERT_FALSE(warnings.empty());
    EXPECT_EQ(warnings[0].category, ValidationWarning::Category::AliasingOverlap);
    EXPECT_NE(warnings[0].detail.find("overlap"), std::string::npos);
}

TEST_F(ValidatorTest, NoAliasingOverlapWhenNonOverlapping) {
    CompiledRenderGraph graph;
    graph.aliasing.resourceToSlot = {0, 0};  // Both in slot 0
    graph.lifetimes = {
        {.resourceIndex = 0, .firstPass = 0, .lastPass = 1},
        {.resourceIndex = 1, .firstPass = 2, .lastPass = 3},  // No overlap
    };

    auto warnings = validator_.ValidateAliasing(graph);
    EXPECT_TRUE(warnings.empty());
}

TEST_F(ValidatorTest, NoAliasingOverlapWhenDifferentSlots) {
    CompiledRenderGraph graph;
    graph.aliasing.resourceToSlot = {0, 1};  // Different slots
    graph.lifetimes = {
        {.resourceIndex = 0, .firstPass = 0, .lastPass = 2},
        {.resourceIndex = 1, .firstPass = 1, .lastPass = 3},  // Would overlap if same slot
    };

    auto warnings = validator_.ValidateAliasing(graph);
    EXPECT_TRUE(warnings.empty());
}

TEST_F(ValidatorTest, NoAliasingWarningWhenNotAliased) {
    CompiledRenderGraph graph;
    graph.aliasing.resourceToSlot = {AliasingLayout::kNotAliased, AliasingLayout::kNotAliased};
    graph.lifetimes = {
        {.resourceIndex = 0, .firstPass = 0, .lastPass = 2},
        {.resourceIndex = 1, .firstPass = 0, .lastPass = 2},
    };

    auto warnings = validator_.ValidateAliasing(graph);
    EXPECT_TRUE(warnings.empty());
}

TEST_F(ValidatorTest, MissingBarrierDetected) {
    auto builder = BuildSimpleChain();
    auto graph = CompileGraph(builder);
    // Create an audit log that's missing barriers
    BarrierAuditLog emptyLog;
    auto warnings = validator_.AuditBarriers(emptyLog, graph, builder);
    // If graph has RAW/WAW edges, there should be missing barrier warnings
    bool hasRawOrWaw = std::ranges::any_of(graph.edges, [](auto& e) {
        return e.hazard == HazardType::RAW || e.hazard == HazardType::WAW;
    });
    if (hasRawOrWaw) {
        EXPECT_FALSE(warnings.empty());
        bool foundMissing = std::ranges::any_of(warnings, [](auto& w) {
            return w.category == ValidationWarning::Category::MissingBarrier;
        });
        EXPECT_TRUE(foundMissing);
    }
}

TEST_F(ValidatorTest, CrossQueueSyncCheckDetectsRace) {
    // Build a graph with cross-queue edges but NO sync points
    CompiledRenderGraph graph;
    graph.passes = {
        {.passIndex = 0, .queue = RGQueueType::Graphics},
        {.passIndex = 1, .queue = RGQueueType::AsyncCompute},
    };
    graph.edges = {{.srcPass = 0, .dstPass = 1, .resourceIndex = 0, .hazard = HazardType::RAW}};
    // No sync points!
    graph.syncPoints.clear();

    RenderGraphBuilder b;
    (void)b.CreateTexture({.width = kW, .height = kH, .debugName = "Tex"});
    (void)b.AddGraphicsPass("P0", [](PassBuilder&) {}, [](RenderPassContext&) {});
    (void)b.AddAsyncComputePass("P1", [](PassBuilder&) {}, [](RenderPassContext&) {});
    b.Build();

    auto warnings = validator_.ValidateCrossQueueSync(graph, b);
    ASSERT_FALSE(warnings.empty());
    EXPECT_EQ(warnings[0].category, ValidationWarning::Category::CrossQueueRace);
}

TEST_F(ValidatorTest, CrossQueueSyncPassesWithSyncPoint) {
    CompiledRenderGraph graph;
    graph.passes = {
        {.passIndex = 0, .queue = RGQueueType::Graphics},
        {.passIndex = 1, .queue = RGQueueType::AsyncCompute},
    };
    graph.edges = {{.srcPass = 0, .dstPass = 1, .resourceIndex = 0, .hazard = HazardType::RAW}};
    graph.syncPoints = {{
        .srcQueue = RGQueueType::Graphics,
        .dstQueue = RGQueueType::AsyncCompute,
        .srcPassIndex = 0,
        .dstPassIndex = 1,
        .timelineValue = 1,
    }};

    RenderGraphBuilder b;
    (void)b.CreateTexture({.width = kW, .height = kH, .debugName = "Tex"});
    (void)b.AddGraphicsPass("P0", [](PassBuilder&) {}, [](RenderPassContext&) {});
    (void)b.AddAsyncComputePass("P1", [](PassBuilder&) {}, [](RenderPassContext&) {});
    b.Build();

    auto warnings = validator_.ValidateCrossQueueSync(graph, b);
    EXPECT_TRUE(warnings.empty());
}

TEST_F(ValidatorTest, ValidateAllCombinesResults) {
    auto builder = BuildSimpleChain();
    auto graph = CompileGraph(builder);
    BarrierAuditLog log;
    log.PopulateFromCompiled(graph, builder);
    auto report = validator_.ValidateAll(graph, builder, &log);
    // A well-formed graph should have no errors
    EXPECT_TRUE(report.IsClean()) << report.FormatReport();
}

TEST_F(ValidatorTest, FormatReportContainsCountsAndCategories) {
    ValidationReport report;
    report.warnings.push_back({
        .category = ValidationWarning::Category::UninitializedRead,
        .passIndex = 0,
        .passName = "TestPass",
        .resourceIndex = 0,
        .resourceName = "TestRes",
        .detail = "test warning",
    });
    report.errorCount = 1;
    auto text = report.FormatReport();
    EXPECT_NE(text.find("1 errors"), std::string::npos);
    EXPECT_NE(text.find("UNINIT_READ"), std::string::npos);
    EXPECT_NE(text.find("TestPass"), std::string::npos);
    EXPECT_NE(text.find("TestRes"), std::string::npos);
}

// =============================================================================
// Complex / stress / edge-case tests
// =============================================================================

TEST(StressTest, LargeGraphGraphvizExport) {
    RenderGraphBuilder b;
    constexpr int N = 50;
    RGResourceHandle tex = b.CreateTexture({.width = kW, .height = kH, .debugName = "Chain0"});
    for (int i = 0; i < N; ++i) {
        std::string name = "Pass_" + std::to_string(i);
        auto nameCopy = new char[name.size() + 1];
        std::memcpy(nameCopy, name.c_str(), name.size() + 1);
        if (i == 0) {
            // First pass: write only (no read of uninitialized resource)
            (void)b.AddGraphicsPass(
                nameCopy, [&](PassBuilder& pb) { tex = pb.WriteTexture(tex); }, [](RenderPassContext&) {}
            );
        } else if (i == N - 1) {
            (void)b.AddGraphicsPass(
                nameCopy,
                [&](PassBuilder& pb) {
                    pb.ReadTexture(tex);
                    pb.SetSideEffects();
                },
                [](RenderPassContext&) {}
            );
        } else {
            (void)b.AddGraphicsPass(
                nameCopy,
                [&](PassBuilder& pb) {
                    pb.ReadTexture(tex);
                    tex = pb.WriteTexture(tex);
                },
                [](RenderPassContext&) {}
            );
        }
    }
    b.Build();
    auto graph = CompileGraph(b);
    auto dot = ExportGraphvizString(graph, b);

    // All passes should appear
    EXPECT_NE(dot.find("Pass_0"), std::string::npos);
    EXPECT_NE(dot.find("Pass_49"), std::string::npos);
    // Should be valid DOT (starts with digraph, ends with })
    EXPECT_NE(dot.find("digraph"), std::string::npos);
    EXPECT_NE(dot.rfind("}"), std::string::npos);

    // Validate the large graph
    RenderGraphValidator validator;
    auto report = validator.ValidateAll(graph, b);
    EXPECT_TRUE(report.IsClean()) << report.FormatReport();
}

TEST(StressTest, ManyResourcesDiffReport) {
    RenderGraphBuilder b1;
    for (int i = 0; i < 20; ++i) {
        std::string name = "Res_" + std::to_string(i);
        auto nameCopy = new char[name.size() + 1];
        std::memcpy(nameCopy, name.c_str(), name.size() + 1);
        (void)b1.CreateTexture({.width = static_cast<uint32_t>(64 + i), .height = 64, .debugName = nameCopy});
    }
    (void)b1.AddGraphicsPass("UseAll", [](PassBuilder& pb) { pb.SetSideEffects(); }, [](RenderPassContext&) {});
    b1.Build();

    RenderGraphBuilder b2;
    for (int i = 0; i < 20; ++i) {
        std::string name = "Res_" + std::to_string(i);
        auto nameCopy = new char[name.size() + 1];
        std::memcpy(nameCopy, name.c_str(), name.size() + 1);
        // Change size for even-numbered resources
        uint32_t w = (i % 2 == 0) ? static_cast<uint32_t>(128 + i) : static_cast<uint32_t>(64 + i);
        (void)b2.CreateTexture({.width = w, .height = 64, .debugName = nameCopy});
    }
    (void)b2.AddGraphicsPass("UseAll", [](PassBuilder& pb) { pb.SetSideEffects(); }, [](RenderPassContext&) {});
    b2.Build();

    std::vector<bool> a1(b1.GetPasses().size(), true);
    std::vector<bool> a2(b2.GetPasses().size(), true);
    auto diff = GraphDiffReport::Generate(b1, a1, b2, a2, 500, 20.0f, false);
    EXPECT_TRUE(diff.HasDiffs());
    // Should detect 10 modified resources (even indices)
    uint32_t modCount = 0;
    for (auto& d : diff.resourceDiffs) {
        if (d.action == DiffAction::Modified) {
            modCount++;
        }
    }
    EXPECT_EQ(modCount, 10u);
}

TEST(StressTest, AliasingValidation_ThreeResourcesInSlot) {
    // 3 resources in same slot, first two non-overlapping, third overlaps with second
    RenderGraphValidator validator;
    CompiledRenderGraph graph;
    graph.aliasing.resourceToSlot = {0, 0, 0};
    graph.lifetimes = {
        {.resourceIndex = 0, .firstPass = 0, .lastPass = 1},
        {.resourceIndex = 1, .firstPass = 2, .lastPass = 4},
        {.resourceIndex = 2, .firstPass = 3, .lastPass = 5},  // Overlaps with resource 1
    };

    auto warnings = validator.ValidateAliasing(graph);
    // Should detect exactly 1 overlap: resource 1 and 2
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_EQ(warnings[0].category, ValidationWarning::Category::AliasingOverlap);
    EXPECT_NE(warnings[0].detail.find("overlap"), std::string::npos);
}

TEST(StressTest, AliasingValidation_AllOverlapping) {
    // 3 resources all overlap in same slot -> 3 warnings (0-1, 0-2, 1-2)
    RenderGraphValidator validator;
    CompiledRenderGraph graph;
    graph.aliasing.resourceToSlot = {0, 0, 0};
    graph.lifetimes = {
        {.resourceIndex = 0, .firstPass = 0, .lastPass = 5},
        {.resourceIndex = 1, .firstPass = 0, .lastPass = 5},
        {.resourceIndex = 2, .firstPass = 0, .lastPass = 5},
    };

    auto warnings = validator.ValidateAliasing(graph);
    EXPECT_EQ(warnings.size(), 3u);
}

TEST(StressTest, CrossQueueMultipleEdgesPartialSync) {
    // Two cross-queue edges, only one has sync -> one race warning
    RenderGraphValidator validator;
    CompiledRenderGraph graph;
    graph.passes = {
        {.passIndex = 0, .queue = RGQueueType::Graphics},
        {.passIndex = 1, .queue = RGQueueType::AsyncCompute},
        {.passIndex = 2, .queue = RGQueueType::Transfer},
    };
    graph.edges = {
        {.srcPass = 0, .dstPass = 1, .resourceIndex = 0, .hazard = HazardType::RAW},  // Graphics -> Async
        {.srcPass = 0, .dstPass = 2, .resourceIndex = 1, .hazard = HazardType::RAW},  // Graphics -> Transfer
    };
    // Only sync Graphics -> AsyncCompute
    graph.syncPoints = {{
        .srcQueue = RGQueueType::Graphics,
        .dstQueue = RGQueueType::AsyncCompute,
        .srcPassIndex = 0,
        .dstPassIndex = 1,
        .timelineValue = 1,
    }};

    RenderGraphBuilder b;
    (void)b.CreateTexture({.width = kW, .height = kH, .debugName = "T0"});
    (void)b.CreateTexture({.width = kW, .height = kH, .debugName = "T1"});
    (void)b.AddGraphicsPass("P0", [](PassBuilder&) {}, [](RenderPassContext&) {});
    (void)b.AddAsyncComputePass("P1", [](PassBuilder&) {}, [](RenderPassContext&) {});
    (void)b.AddTransferPass("P2", [](PassBuilder&) {}, [](RenderPassContext&) {});
    b.Build();

    auto warnings = validator.ValidateCrossQueueSync(graph, b);
    // Only Graphics -> Transfer edge should produce a warning
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_EQ(warnings[0].category, ValidationWarning::Category::CrossQueueRace);
    EXPECT_NE(warnings[0].detail.find("Transfer"), std::string::npos);
}

TEST(StressTest, FullPipelineValidation_CrossQueueChain) {
    auto builder = BuildCrossQueueChain();
    auto graph = CompileGraph(builder);

    // Full validation
    RenderGraphValidator validator;
    BarrierAuditLog log;
    log.PopulateFromCompiled(graph, builder);
    auto report = validator.ValidateAll(graph, builder, &log);

    // Timing
    PassTimingReport timing;
    InitTimingReport(timing, graph, builder);
    EXPECT_EQ(timing.passes.size(), graph.passes.size());

    // Graphviz
    auto dot = ExportGraphvizString(graph, builder);
    EXPECT_NE(dot.find("Render"), std::string::npos);
    EXPECT_NE(dot.find("AsyncBlur"), std::string::npos);
    EXPECT_NE(dot.find("Composite"), std::string::npos);

    // JSON audit
    auto j = log.ToJson();
    EXPECT_TRUE(j["entries"].is_array());

    // Print report for informational purposes
    if (!report.IsClean()) {
        std::cerr << report.FormatReport();
    }
}

TEST(StressTest, DiamondDAG_ValidateAll) {
    // Diamond: A writes R, B and C both read R, D reads from B and C
    RenderGraphBuilder b;
    auto r = b.CreateTexture({.width = kW, .height = kH, .debugName = "Diamond"});
    auto r2 = b.CreateTexture({.width = kW, .height = kH, .debugName = "BranchB"});
    auto r3 = b.CreateTexture({.width = kW, .height = kH, .debugName = "BranchC"});
    (void)b.AddGraphicsPass("PassA", [&](PassBuilder& pb) { r = pb.WriteTexture(r); }, [](RenderPassContext&) {});
    (void)b.AddGraphicsPass(
        "PassB",
        [&](PassBuilder& pb) {
            pb.ReadTexture(r);
            r2 = pb.WriteTexture(r2);
        },
        [](RenderPassContext&) {}
    );
    (void)b.AddGraphicsPass(
        "PassC",
        [&](PassBuilder& pb) {
            pb.ReadTexture(r);
            r3 = pb.WriteTexture(r3);
        },
        [](RenderPassContext&) {}
    );
    (void)b.AddGraphicsPass(
        "PassD",
        [&](PassBuilder& pb) {
            pb.ReadTexture(r2);
            pb.ReadTexture(r3);
            pb.SetSideEffects();
        },
        [](RenderPassContext&) {}
    );
    b.Build();
    auto graph = CompileGraph(b);

    // Validate: no uninitialized reads or aliasing issues expected
    RenderGraphValidator validator;
    auto uninitWarnings = validator.CheckUninitializedReads(graph, b);
    EXPECT_TRUE(uninitWarnings.empty()) << "Unexpected uninitialized read in diamond DAG";
    auto aliasingWarnings = validator.ValidateAliasing(graph);
    EXPECT_TRUE(aliasingWarnings.empty()) << "Unexpected aliasing overlap in diamond DAG";

    // DOT should contain all 4 passes
    auto dot = ExportGraphvizString(graph, b);
    EXPECT_NE(dot.find("PassA"), std::string::npos);
    EXPECT_NE(dot.find("PassB"), std::string::npos);
    EXPECT_NE(dot.find("PassC"), std::string::npos);
    EXPECT_NE(dot.find("PassD"), std::string::npos);

    // Barrier audit log should have entries
    BarrierAuditLog log;
    log.PopulateFromCompiled(graph, b);
    EXPECT_GT(log.CountTotal(), 0u);
}

TEST(StressTest, SinglePassGraph) {
    RenderGraphBuilder b;
    (void)b.AddGraphicsPass("Lonely", [](PassBuilder& pb) { pb.SetSideEffects(); }, [](RenderPassContext&) {});
    b.Build();
    auto graph = CompileGraph(b);

    // Graphviz
    auto dot = ExportGraphvizString(graph, b);
    EXPECT_NE(dot.find("Lonely"), std::string::npos);

    // Timing
    PassTimingReport timing;
    InitTimingReport(timing, graph, b);
    EXPECT_EQ(timing.passes.size(), 1u);
    EXPECT_STREQ(timing.passes[0].name, "Lonely");

    // Validation
    RenderGraphValidator validator;
    auto report = validator.ValidateAll(graph, b);
    EXPECT_TRUE(report.IsClean());

    // Barrier audit
    BarrierAuditLog log;
    log.PopulateFromCompiled(graph, b);
    EXPECT_EQ(log.CountTotal(), 0u);
}

TEST(EdgeCase, NullPassNameHandling) {
    // Create a manually constructed graph with null names
    CompiledRenderGraph graph;
    graph.passes = {{.passIndex = 0, .queue = RGQueueType::Graphics}};

    RenderGraphBuilder b;
    (void)b.AddGraphicsPass(nullptr, [](PassBuilder& pb) { pb.SetSideEffects(); }, [](RenderPassContext&) {});
    b.Build();

    // These should not crash
    auto dot = ExportGraphvizString(graph, b);
    EXPECT_FALSE(dot.empty());

    PassTimingReport timing;
    InitTimingReport(timing, graph, b);
    // Name might be null, should not crash

    BarrierAuditLog log;
    log.PopulateFromCompiled(graph, b);
    auto j = log.ToJson();
    EXPECT_TRUE(j.contains("entries"));
}

// =============================================================================
// Regression / integration tests combining multiple phases
// =============================================================================

TEST(Integration, CompileGraphvizValidateBarrierAuditCycle) {
    // Build -> Compile -> Graphviz -> Validate -> BarrierAudit -> Timing -> Diff
    auto b1 = BuildThreePassChain();
    auto g1 = CompileGraph(b1);

    // Graphviz export
    auto dot = ExportGraphvizString(g1, b1);
    EXPECT_FALSE(dot.empty());
    EXPECT_NE(dot.find("GeometryPass"), std::string::npos);

    // Validation
    RenderGraphValidator validator;
    BarrierAuditLog log;
    log.PopulateFromCompiled(g1, b1);
    auto report = validator.ValidateAll(g1, b1, &log);
    EXPECT_TRUE(report.IsClean()) << report.FormatReport();

    // Timing
    PassTimingReport timing;
    InitTimingReport(timing, g1, b1);
    EXPECT_EQ(timing.passes.size(), g1.passes.size());

    // Now build a different graph and diff
    auto b2 = BuildCrossQueueChain();
    auto g2 = CompileGraph(b2);
    std::vector<bool> a1(b1.GetPasses().size(), true);
    std::vector<bool> a2(b2.GetPasses().size(), true);
    auto diff = GraphDiffReport::Generate(b1, a1, b2, a2, 999, 50.0f, false);
    EXPECT_TRUE(diff.HasDiffs());
    auto diffJson = diff.ToJsonString(true);
    EXPECT_NO_THROW((void)json::parse(diffJson));
}
