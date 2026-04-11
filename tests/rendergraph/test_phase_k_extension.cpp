/** @file test_phase_k_extension.cpp
 *  @brief Comprehensive tests for Phase K: Plugin Extension System + PSO Miss Handling.
 *
 *  Tests are organized as system behavior specifications, not mere validation.
 *  Every assertion checks precise values, not just has_value / non-null.
 *
 *  Sections:
 *    1. ExtensionCapability bitfield algebra
 *    2. IRenderGraphExtension lifecycle contracts
 *    3. ExtensionRegistry full API coverage
 *    4. PsoMissHandler — all policies × all statuses
 *    5. Transitive DCE propagation (linear, diamond, fan-out, deep chain)
 *    6. FormatStatus string formatting
 *    7. Adversarial / edge-case / stress scenarios
 *
 *  Pure CPU — no GPU device required.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rendergraph/RenderGraphExtension.h"
#include "miki/rendergraph/RenderGraphTypes.h"

using namespace miki::rg;
using namespace miki::rhi;

// =============================================================================
// Test helpers — mock extensions with observable lifecycle
// =============================================================================

struct LifecycleLog {
    std::vector<std::string> events;
    void Record(std::string_view event) { events.emplace_back(event); }
    [[nodiscard]] auto Count(std::string_view event) const -> int {
        return static_cast<int>(std::ranges::count(events, event));
    }
    void Clear() { events.clear(); }
};

class MockExtension : public IRenderGraphExtension {
   public:
    std::string name;
    int32_t priority;
    ExtensionCapability requiredCaps;
    bool activeThisFrame = true;
    int buildPassesCalls = 0;
    LifecycleLog* log = nullptr;

    explicit MockExtension(
        std::string iName, int32_t iPriority = 1000, ExtensionCapability iCaps = ExtensionCapability::None,
        LifecycleLog* iLog = nullptr
    )
        : name(std::move(iName)), priority(iPriority), requiredCaps(iCaps), log(iLog) {}

    void BuildPasses(RenderGraphBuilder& builder, const ExtensionBuildContext&) override {
        buildPassesCalls++;
        if (log) {
            log->Record(name + "::BuildPasses");
        }
        // Add one dummy pass per invocation so passesInjected can be tracked
        (void)builder.AddGraphicsPass(
            name.c_str(), [](PassBuilder& pb) { pb.SetSideEffects(); }, [](RenderPassContext&) {}
        );
    }
    void OnRegistered() override {
        if (log) {
            log->Record(name + "::OnRegistered");
        }
    }
    void OnUnregistered() override {
        if (log) {
            log->Record(name + "::OnUnregistered");
        }
    }
    void Shutdown() override {
        if (log) {
            log->Record(name + "::Shutdown");
        }
    }
    [[nodiscard]] auto GetName() const noexcept -> const char* override { return name.c_str(); }
    [[nodiscard]] auto GetPriority() const noexcept -> int32_t override { return priority; }
    [[nodiscard]] auto GetRequiredCapabilities() const noexcept -> ExtensionCapability override { return requiredCaps; }
    [[nodiscard]] auto IsActiveThisFrame(const ExtensionBuildContext&) const -> bool override {
        return activeThisFrame;
    }
};

class ConditionalExtension : public MockExtension {
   public:
    uint64_t activeOnEvenFrames = true;
    using MockExtension::MockExtension;

    [[nodiscard]] auto IsActiveThisFrame(const ExtensionBuildContext& ctx) const -> bool override {
        return (ctx.frameNumber % 2) == 0;
    }
};

// Helper: create a PipelineHandle with a specific raw value
static auto MakePipeline(uint64_t val) -> PipelineHandle {
    return PipelineHandle{val};
}

// Helper: build a CompiledRenderGraph with given pass count and edges
struct GraphBuilder {
    CompiledRenderGraph graph;

    auto AddPass(uint32_t idx, RGQueueType queue = RGQueueType::Graphics) -> GraphBuilder& {
        graph.passes.push_back({.passIndex = idx, .queue = queue});
        return *this;
    }

    // Edge: srcPass writes resourceIndex, dstPass reads it
    auto AddEdge(uint32_t src, uint32_t dst, uint16_t resource) -> GraphBuilder& {
        graph.edges.push_back({.srcPass = src, .dstPass = dst, .resourceIndex = resource});
        return *this;
    }

    [[nodiscard]] auto Build() -> CompiledRenderGraph { return std::move(graph); }
};

// =============================================================================
// 1. ExtensionCapability bitfield algebra
// =============================================================================

TEST(ExtensionCapability, NoneIsZero) {
    EXPECT_EQ(static_cast<uint32_t>(ExtensionCapability::None), 0u);
}

TEST(ExtensionCapability, OrCombines) {
    auto combined = ExtensionCapability::AsyncCompute | ExtensionCapability::MeshShader;
    EXPECT_EQ(static_cast<uint32_t>(combined), 0b11u);
}

TEST(ExtensionCapability, AndMasks) {
    auto combined = ExtensionCapability::AsyncCompute | ExtensionCapability::MeshShader;
    auto masked = combined & ExtensionCapability::AsyncCompute;
    EXPECT_EQ(masked, ExtensionCapability::AsyncCompute);
}

TEST(ExtensionCapability, OrEqualsAccumulates) {
    auto caps = ExtensionCapability::None;
    caps |= ExtensionCapability::RayTracing;
    caps |= ExtensionCapability::WorkGraphs;
    EXPECT_EQ(static_cast<uint32_t>(caps), (1u << 2) | (1u << 3));
}

TEST(ExtensionCapability, HasCapabilitySingleFlag) {
    auto set = ExtensionCapability::AsyncCompute | ExtensionCapability::RayTracing;
    EXPECT_TRUE(HasCapability(set, ExtensionCapability::AsyncCompute));
    EXPECT_TRUE(HasCapability(set, ExtensionCapability::RayTracing));
    EXPECT_FALSE(HasCapability(set, ExtensionCapability::MeshShader));
}

TEST(ExtensionCapability, HasCapabilityMultiFlag) {
    auto set = ExtensionCapability::AsyncCompute | ExtensionCapability::RayTracing | ExtensionCapability::MeshShader;
    auto required = ExtensionCapability::AsyncCompute | ExtensionCapability::RayTracing;
    EXPECT_TRUE(HasCapability(set, required));

    auto missing = ExtensionCapability::AsyncCompute | ExtensionCapability::WorkGraphs;
    EXPECT_FALSE(HasCapability(set, missing));
}

TEST(ExtensionCapability, HasCapabilityNoneAlwaysTrue) {
    EXPECT_TRUE(HasCapability(ExtensionCapability::None, ExtensionCapability::None));
    EXPECT_TRUE(HasCapability(ExtensionCapability::AsyncCompute, ExtensionCapability::None));
}

TEST(ExtensionCapability, AllFlagsDistinct) {
    std::array caps = {
        ExtensionCapability::AsyncCompute, ExtensionCapability::MeshShader,    ExtensionCapability::RayTracing,
        ExtensionCapability::WorkGraphs,   ExtensionCapability::Int64Atomics,  ExtensionCapability::Multiview,
        ExtensionCapability::VariableRate, ExtensionCapability::SparseBinding,
    };
    for (size_t i = 0; i < caps.size(); ++i) {
        for (size_t j = i + 1; j < caps.size(); ++j) {
            EXPECT_NE(caps[i], caps[j]) << "Flags at index " << i << " and " << j << " are not distinct";
            EXPECT_EQ(caps[i] & caps[j], ExtensionCapability::None);
        }
    }
}

// =============================================================================
// 2. IRenderGraphExtension lifecycle contracts
// =============================================================================

TEST(IRenderGraphExtension, DefaultPriorityIs1000) {
    MockExtension ext("Test");
    EXPECT_EQ(ext.GetPriority(), 1000);
}

TEST(IRenderGraphExtension, DefaultCapabilitiesIsNone) {
    MockExtension ext("Test");
    EXPECT_EQ(ext.GetRequiredCapabilities(), ExtensionCapability::None);
}

TEST(IRenderGraphExtension, DefaultDependenciesIsEmpty) {
    MockExtension ext("Test");
    EXPECT_TRUE(ext.GetDependencies().empty());
}

TEST(IRenderGraphExtension, DefaultIsActiveThisFrameIsTrue) {
    MockExtension ext("Test");
    ExtensionBuildContext ctx{};
    EXPECT_TRUE(ext.IsActiveThisFrame(ctx));
}

TEST(IRenderGraphExtension, LifecycleRegisterCallsOnRegistered) {
    LifecycleLog log;
    ExtensionRegistry registry;
    registry.RegisterExtension(std::make_unique<MockExtension>("A", 1000, ExtensionCapability::None, &log));
    EXPECT_EQ(log.Count("A::OnRegistered"), 1);
    EXPECT_EQ(log.Count("A::OnUnregistered"), 0);
    EXPECT_EQ(log.Count("A::Shutdown"), 0);
}

TEST(IRenderGraphExtension, LifecycleUnregisterCallsOnUnregistered) {
    LifecycleLog log;
    ExtensionRegistry registry;
    registry.RegisterExtension(std::make_unique<MockExtension>("A", 1000, ExtensionCapability::None, &log));
    log.Clear();
    EXPECT_TRUE(registry.UnregisterExtension("A"));
    EXPECT_EQ(log.Count("A::OnUnregistered"), 1);
}

TEST(IRenderGraphExtension, LifecycleDestructorCallsShutdownAll) {
    LifecycleLog log;
    {
        ExtensionRegistry registry;
        registry.RegisterExtension(std::make_unique<MockExtension>("A", 1000, ExtensionCapability::None, &log));
        registry.RegisterExtension(std::make_unique<MockExtension>("B", 500, ExtensionCapability::None, &log));
    }  // registry destroyed here
    EXPECT_EQ(log.Count("A::Shutdown"), 1);
    EXPECT_EQ(log.Count("B::Shutdown"), 1);
}

TEST(IRenderGraphExtension, LifecycleReplaceCallsUnregisterOnOldAndRegisterOnNew) {
    LifecycleLog log;
    ExtensionRegistry registry;
    registry.RegisterExtension(std::make_unique<MockExtension>("A", 1000, ExtensionCapability::None, &log));
    log.Clear();
    // Register again with same name — should replace
    auto replaced
        = registry.RegisterExtension(std::make_unique<MockExtension>("A", 500, ExtensionCapability::None, &log));
    EXPECT_FALSE(replaced);                        // false = replaced existing
    EXPECT_EQ(log.Count("A::OnUnregistered"), 1);  // old one
    EXPECT_EQ(log.Count("A::OnRegistered"), 1);    // new one
}

// =============================================================================
// 3. ExtensionRegistry — full API coverage
// =============================================================================

class ExtensionRegistryTest : public ::testing::Test {
   protected:
    ExtensionRegistry registry;
    LifecycleLog log;
    ExtensionBuildContext ctx{};
};

TEST_F(ExtensionRegistryTest, EmptyRegistryState) {
    EXPECT_EQ(registry.GetExtensionCount(), 0u);
    EXPECT_TRUE(registry.GetExtensionNames().empty());
    EXPECT_FALSE(registry.IsRegistered("anything"));
    EXPECT_FALSE(registry.IsEnabled("anything"));
    EXPECT_EQ(registry.FindExtension("anything"), nullptr);
}

TEST_F(ExtensionRegistryTest, RegisterSingle) {
    bool isNew = registry.RegisterExtension(std::make_unique<MockExtension>("Foo", 500));
    EXPECT_TRUE(isNew);
    EXPECT_EQ(registry.GetExtensionCount(), 1u);
    EXPECT_TRUE(registry.IsRegistered("Foo"));
    EXPECT_TRUE(registry.IsEnabled("Foo"));
    auto* ext = registry.FindExtension("Foo");
    ASSERT_NE(ext, nullptr);
    EXPECT_STREQ(ext->GetName(), "Foo");
    EXPECT_EQ(ext->GetPriority(), 500);
}

TEST_F(ExtensionRegistryTest, RegisterMultiple) {
    registry.RegisterExtension(std::make_unique<MockExtension>("A", 100));
    registry.RegisterExtension(std::make_unique<MockExtension>("B", 200));
    registry.RegisterExtension(std::make_unique<MockExtension>("C", 300));
    EXPECT_EQ(registry.GetExtensionCount(), 3u);
    auto names = registry.GetExtensionNames();
    EXPECT_EQ(names.size(), 3u);
}

TEST_F(ExtensionRegistryTest, RegisterReplacesExisting) {
    registry.RegisterExtension(std::make_unique<MockExtension>("X", 100, ExtensionCapability::None, &log));
    EXPECT_EQ(registry.FindExtension("X")->GetPriority(), 100);

    bool isNew = registry.RegisterExtension(std::make_unique<MockExtension>("X", 999, ExtensionCapability::None, &log));
    EXPECT_FALSE(isNew);
    EXPECT_EQ(registry.GetExtensionCount(), 1u);  // still 1
    EXPECT_EQ(registry.FindExtension("X")->GetPriority(), 999);
}

TEST_F(ExtensionRegistryTest, UnregisterExisting) {
    registry.RegisterExtension(std::make_unique<MockExtension>("A"));
    EXPECT_TRUE(registry.UnregisterExtension("A"));
    EXPECT_EQ(registry.GetExtensionCount(), 0u);
    EXPECT_FALSE(registry.IsRegistered("A"));
}

TEST_F(ExtensionRegistryTest, UnregisterNonexistent) {
    EXPECT_FALSE(registry.UnregisterExtension("ghost"));
}

TEST_F(ExtensionRegistryTest, UnregisterMiddleOfThree) {
    registry.RegisterExtension(std::make_unique<MockExtension>("A", 100));
    registry.RegisterExtension(std::make_unique<MockExtension>("B", 200));
    registry.RegisterExtension(std::make_unique<MockExtension>("C", 300));
    EXPECT_TRUE(registry.UnregisterExtension("B"));
    EXPECT_EQ(registry.GetExtensionCount(), 2u);
    EXPECT_TRUE(registry.IsRegistered("A"));
    EXPECT_FALSE(registry.IsRegistered("B"));
    EXPECT_TRUE(registry.IsRegistered("C"));
}

TEST_F(ExtensionRegistryTest, SetEnabledToggle) {
    registry.RegisterExtension(std::make_unique<MockExtension>("A"));
    EXPECT_TRUE(registry.IsEnabled("A"));

    EXPECT_TRUE(registry.SetEnabled("A", false));
    EXPECT_FALSE(registry.IsEnabled("A"));
    EXPECT_TRUE(registry.IsRegistered("A"));  // still registered

    EXPECT_TRUE(registry.SetEnabled("A", true));
    EXPECT_TRUE(registry.IsEnabled("A"));
}

TEST_F(ExtensionRegistryTest, SetEnabledNonexistent) {
    EXPECT_FALSE(registry.SetEnabled("ghost", false));
}

TEST_F(ExtensionRegistryTest, FindExtensionNonexistent) {
    EXPECT_EQ(registry.FindExtension("nope"), nullptr);
}

TEST_F(ExtensionRegistryTest, GetExtensionNamesOrderedByPriority) {
    registry.RegisterExtension(std::make_unique<MockExtension>("C", 300));
    registry.RegisterExtension(std::make_unique<MockExtension>("A", 100));
    registry.RegisterExtension(std::make_unique<MockExtension>("B", 200));

    // Invoke once to trigger sort
    RenderGraphBuilder builder;
    registry.InvokeExtensions(builder, ctx);

    auto names = registry.GetExtensionNames();
    ASSERT_EQ(names.size(), 3u);
    EXPECT_STREQ(names[0], "A");
    EXPECT_STREQ(names[1], "B");
    EXPECT_STREQ(names[2], "C");
}

TEST_F(ExtensionRegistryTest, PriorityOrderingInvocation) {
    registry.RegisterExtension(std::make_unique<MockExtension>("Late", 2000, ExtensionCapability::None, &log));
    registry.RegisterExtension(std::make_unique<MockExtension>("Early", 100, ExtensionCapability::None, &log));
    registry.RegisterExtension(std::make_unique<MockExtension>("Mid", 1000, ExtensionCapability::None, &log));

    RenderGraphBuilder builder;
    registry.InvokeExtensions(builder, ctx);

    // Verify invocation order from log
    ASSERT_GE(log.events.size(), 3u);
    std::vector<std::string> buildEvents;
    for (const auto& e : log.events) {
        if (e.find("::BuildPasses") != std::string::npos) {
            buildEvents.push_back(e);
        }
    }
    ASSERT_EQ(buildEvents.size(), 3u);
    EXPECT_EQ(buildEvents[0], "Early::BuildPasses");
    EXPECT_EQ(buildEvents[1], "Mid::BuildPasses");
    EXPECT_EQ(buildEvents[2], "Late::BuildPasses");
}

TEST_F(ExtensionRegistryTest, StableSortPreservesRegistrationOrder) {
    // Extensions with same priority should maintain registration order
    registry.RegisterExtension(std::make_unique<MockExtension>("First", 500, ExtensionCapability::None, &log));
    registry.RegisterExtension(std::make_unique<MockExtension>("Second", 500, ExtensionCapability::None, &log));
    registry.RegisterExtension(std::make_unique<MockExtension>("Third", 500, ExtensionCapability::None, &log));

    RenderGraphBuilder builder;
    log.Clear();
    registry.InvokeExtensions(builder, ctx);

    std::vector<std::string> buildEvents;
    for (const auto& e : log.events) {
        if (e.find("::BuildPasses") != std::string::npos) {
            buildEvents.push_back(e);
        }
    }
    ASSERT_EQ(buildEvents.size(), 3u);
    EXPECT_EQ(buildEvents[0], "First::BuildPasses");
    EXPECT_EQ(buildEvents[1], "Second::BuildPasses");
    EXPECT_EQ(buildEvents[2], "Third::BuildPasses");
}

TEST_F(ExtensionRegistryTest, DisabledExtensionNotInvoked) {
    registry.RegisterExtension(std::make_unique<MockExtension>("A", 100, ExtensionCapability::None, &log));
    registry.SetEnabled("A", false);

    RenderGraphBuilder builder;
    log.Clear();
    auto invoked = registry.InvokeExtensions(builder, ctx);

    EXPECT_EQ(invoked, 0u);
    EXPECT_EQ(log.Count("A::BuildPasses"), 0);

    const auto& stats = registry.GetLastInvocationStats();
    EXPECT_EQ(stats.totalRegistered, 1u);
    EXPECT_EQ(stats.totalEnabled, 0u);
    EXPECT_EQ(stats.totalCapable, 0u);
    EXPECT_EQ(stats.totalActive, 0u);
    EXPECT_EQ(stats.totalInvoked, 0u);
    EXPECT_EQ(stats.passesInjected, 0u);
}

TEST_F(ExtensionRegistryTest, CapabilityFilteringBlocksIncapable) {
    registry.SetAvailableCapabilities(ExtensionCapability::AsyncCompute);  // only async compute
    registry.RegisterExtension(std::make_unique<MockExtension>("NeedRT", 100, ExtensionCapability::RayTracing, &log));
    registry.RegisterExtension(std::make_unique<MockExtension>("NeedAC", 200, ExtensionCapability::AsyncCompute, &log));
    registry.RegisterExtension(std::make_unique<MockExtension>("NoReqs", 300, ExtensionCapability::None, &log));

    RenderGraphBuilder builder;
    log.Clear();
    auto invoked = registry.InvokeExtensions(builder, ctx);

    EXPECT_EQ(invoked, 2u);  // NeedAC + NoReqs
    EXPECT_EQ(log.Count("NeedRT::BuildPasses"), 0);
    EXPECT_EQ(log.Count("NeedAC::BuildPasses"), 1);
    EXPECT_EQ(log.Count("NoReqs::BuildPasses"), 1);

    const auto& stats = registry.GetLastInvocationStats();
    EXPECT_EQ(stats.totalRegistered, 3u);
    EXPECT_EQ(stats.totalEnabled, 3u);
    EXPECT_EQ(stats.totalCapable, 2u);
    EXPECT_EQ(stats.totalActive, 2u);
    EXPECT_EQ(stats.totalInvoked, 2u);
}

TEST_F(ExtensionRegistryTest, CapabilityFilteringMultiFlagRequirement) {
    registry.SetAvailableCapabilities(ExtensionCapability::AsyncCompute | ExtensionCapability::MeshShader);
    auto combined = ExtensionCapability::AsyncCompute | ExtensionCapability::MeshShader;
    registry.RegisterExtension(std::make_unique<MockExtension>("Both", 100, combined, &log));

    RenderGraphBuilder builder;
    log.Clear();
    auto invoked = registry.InvokeExtensions(builder, ctx);
    EXPECT_EQ(invoked, 1u);
    EXPECT_EQ(log.Count("Both::BuildPasses"), 1);
}

TEST_F(ExtensionRegistryTest, CapabilityFilteringPartialMultiFlagFails) {
    registry.SetAvailableCapabilities(ExtensionCapability::AsyncCompute);  // only one of two
    auto combined = ExtensionCapability::AsyncCompute | ExtensionCapability::MeshShader;
    registry.RegisterExtension(std::make_unique<MockExtension>("Both", 100, combined, &log));

    RenderGraphBuilder builder;
    log.Clear();
    auto invoked = registry.InvokeExtensions(builder, ctx);
    EXPECT_EQ(invoked, 0u);
    EXPECT_EQ(log.Count("Both::BuildPasses"), 0);
}

TEST_F(ExtensionRegistryTest, IsActiveThisFrameFiltering) {
    auto ext = std::make_unique<MockExtension>("Inactive", 100, ExtensionCapability::None, &log);
    ext->activeThisFrame = false;
    registry.RegisterExtension(std::move(ext));

    RenderGraphBuilder builder;
    log.Clear();
    auto invoked = registry.InvokeExtensions(builder, ctx);
    EXPECT_EQ(invoked, 0u);

    const auto& stats = registry.GetLastInvocationStats();
    EXPECT_EQ(stats.totalEnabled, 1u);
    EXPECT_EQ(stats.totalCapable, 1u);
    EXPECT_EQ(stats.totalActive, 0u);
}

TEST_F(ExtensionRegistryTest, ConditionalActivationByFrameNumber) {
    auto ext = std::make_unique<ConditionalExtension>("EvenOnly", 100, ExtensionCapability::None, &log);
    registry.RegisterExtension(std::move(ext));

    // Frame 0 (even) — active
    ExtensionBuildContext ctx0{.frameNumber = 0};
    RenderGraphBuilder b0;
    log.Clear();
    EXPECT_EQ(registry.InvokeExtensions(b0, ctx0), 1u);
    EXPECT_EQ(log.Count("EvenOnly::BuildPasses"), 1);

    // Frame 1 (odd) — inactive
    ExtensionBuildContext ctx1{.frameNumber = 1};
    RenderGraphBuilder b1;
    log.Clear();
    EXPECT_EQ(registry.InvokeExtensions(b1, ctx1), 0u);
    EXPECT_EQ(log.Count("EvenOnly::BuildPasses"), 0);

    // Frame 2 (even) — active again
    ExtensionBuildContext ctx2{.frameNumber = 2};
    RenderGraphBuilder b2;
    log.Clear();
    EXPECT_EQ(registry.InvokeExtensions(b2, ctx2), 1u);
}

TEST_F(ExtensionRegistryTest, InvocationStatsPassesInjectedCount) {
    registry.RegisterExtension(std::make_unique<MockExtension>("A", 100));
    registry.RegisterExtension(std::make_unique<MockExtension>("B", 200));

    RenderGraphBuilder builder;
    registry.InvokeExtensions(builder, ctx);

    const auto& stats = registry.GetLastInvocationStats();
    EXPECT_EQ(stats.totalInvoked, 2u);
    EXPECT_EQ(stats.passesInjected, 2u);  // each MockExtension adds exactly 1 pass
    EXPECT_EQ(builder.GetPassCount(), 2u);
}

TEST_F(ExtensionRegistryTest, InvokeEmptyRegistry) {
    RenderGraphBuilder builder;
    auto invoked = registry.InvokeExtensions(builder, ctx);
    EXPECT_EQ(invoked, 0u);
    EXPECT_EQ(builder.GetPassCount(), 0u);

    const auto& stats = registry.GetLastInvocationStats();
    EXPECT_EQ(stats.totalRegistered, 0u);
    EXPECT_EQ(stats.totalEnabled, 0u);
    EXPECT_EQ(stats.totalInvoked, 0u);
    EXPECT_EQ(stats.passesInjected, 0u);
}

TEST_F(ExtensionRegistryTest, ShutdownAllClearsAndCallsShutdown) {
    registry.RegisterExtension(std::make_unique<MockExtension>("A", 100, ExtensionCapability::None, &log));
    registry.RegisterExtension(std::make_unique<MockExtension>("B", 200, ExtensionCapability::None, &log));
    log.Clear();

    registry.ShutdownAll();

    EXPECT_EQ(registry.GetExtensionCount(), 0u);
    EXPECT_EQ(log.Count("A::Shutdown"), 1);
    EXPECT_EQ(log.Count("B::Shutdown"), 1);
}

TEST_F(ExtensionRegistryTest, DoubleShutdownIsSafe) {
    registry.RegisterExtension(std::make_unique<MockExtension>("A", 100, ExtensionCapability::None, &log));
    registry.ShutdownAll();
    log.Clear();
    // Second shutdown on empty registry — should not crash
    registry.ShutdownAll();
    EXPECT_EQ(log.events.size(), 0u);
}

TEST_F(ExtensionRegistryTest, ReplaceClearsManualDisable) {
    registry.RegisterExtension(std::make_unique<MockExtension>("A", 100));
    registry.SetEnabled("A", false);
    EXPECT_FALSE(registry.IsEnabled("A"));

    // Replace — should re-enable
    registry.RegisterExtension(std::make_unique<MockExtension>("A", 200));
    EXPECT_TRUE(registry.IsEnabled("A"));
}

TEST_F(ExtensionRegistryTest, NegativePriorityAllowed) {
    registry.RegisterExtension(std::make_unique<MockExtension>("Neg", -100, ExtensionCapability::None, &log));
    registry.RegisterExtension(std::make_unique<MockExtension>("Pos", 100, ExtensionCapability::None, &log));

    RenderGraphBuilder builder;
    log.Clear();
    registry.InvokeExtensions(builder, ctx);

    std::vector<std::string> buildEvents;
    for (const auto& e : log.events) {
        if (e.find("::BuildPasses") != std::string::npos) {
            buildEvents.push_back(e);
        }
    }
    ASSERT_EQ(buildEvents.size(), 2u);
    EXPECT_EQ(buildEvents[0], "Neg::BuildPasses");
    EXPECT_EQ(buildEvents[1], "Pos::BuildPasses");
}

// =============================================================================
// 4. PsoMissHandler — all policies × all statuses
// =============================================================================

class PsoMissHandlerTest : public ::testing::Test {
   protected:
    PsoMissHandler handler;
    PipelineHandle psoA = MakePipeline(1);
    PipelineHandle psoB = MakePipeline(2);
    PipelineHandle fallbackPso = MakePipeline(100);
};

TEST_F(PsoMissHandlerTest, EmptyGraph) {
    CompiledRenderGraph graph;
    auto results = handler.ResolveAll(graph, {});
    EXPECT_TRUE(results.empty());
    EXPECT_EQ(handler.GetSkippedPassCount(), 0u);
    EXPECT_EQ(handler.GetFallbackPassCount(), 0u);
    EXPECT_EQ(handler.GetReadyPassCount(), 0u);

    const auto& stats = handler.GetStats();
    EXPECT_EQ(stats.totalPasses, 0u);
}

TEST_F(PsoMissHandlerTest, NoReadinessQueryTreatsAllAsReady) {
    auto graph = GraphBuilder{}.AddPass(0).AddPass(1).Build();
    std::vector<PassPsoConfig> configs = {
        {.primaryPso = psoA, .missPolicy = PsoMissPolicy::Skip},
        {.primaryPso = psoB, .missPolicy = PsoMissPolicy::Skip},
    };

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_FALSE(results[0].isSkipped);
    EXPECT_TRUE(results[0].isPrimary);
    EXPECT_EQ(results[0].activePso, psoA);
    EXPECT_FALSE(results[1].isSkipped);
    EXPECT_TRUE(results[1].isPrimary);
    EXPECT_EQ(results[1].activePso, psoB);

    EXPECT_EQ(handler.GetReadyPassCount(), 2u);
    EXPECT_EQ(handler.GetSkippedPassCount(), 0u);
}

TEST_F(PsoMissHandlerTest, NoPsoCfgTreatsAsReady) {
    auto graph = GraphBuilder{}.AddPass(0).AddPass(1).Build();
    // No configs at all
    auto results = handler.ResolveAll(graph, {});
    ASSERT_EQ(results.size(), 2u);
    EXPECT_FALSE(results[0].isSkipped);
    EXPECT_FALSE(results[1].isSkipped);
    EXPECT_EQ(handler.GetReadyPassCount(), 2u);
}

TEST_F(PsoMissHandlerTest, InvalidPrimaryPsoTreatsAsReady) {
    auto graph = GraphBuilder{}.AddPass(0).Build();
    std::vector<PassPsoConfig> configs = {{.primaryPso = PipelineHandle{}, .missPolicy = PsoMissPolicy::Fallback}};

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].isSkipped);
    EXPECT_TRUE(results[0].isPrimary);
    EXPECT_EQ(handler.GetReadyPassCount(), 1u);
}

TEST_F(PsoMissHandlerTest, ReadyStatus_SkipPolicy) {
    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Ready; });
    auto graph = GraphBuilder{}.AddPass(0).Build();
    std::vector<PassPsoConfig> configs = {{.primaryPso = psoA, .missPolicy = PsoMissPolicy::Skip}};

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].isSkipped);
    EXPECT_TRUE(results[0].isPrimary);
    EXPECT_EQ(results[0].activePso, psoA);
    EXPECT_EQ(results[0].appliedPolicy, PsoMissPolicy::Skip);
}

TEST_F(PsoMissHandlerTest, ReadyStatus_FallbackPolicy) {
    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Ready; });
    auto graph = GraphBuilder{}.AddPass(0).Build();
    std::vector<PassPsoConfig> configs
        = {{.primaryPso = psoA, .fallbackPso = fallbackPso, .missPolicy = PsoMissPolicy::Fallback}};

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].isSkipped);
    EXPECT_TRUE(results[0].isPrimary);
    EXPECT_EQ(results[0].activePso, psoA);  // primary used since ready
}

TEST_F(PsoMissHandlerTest, PendingStatus_SkipPolicy) {
    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Pending; });
    auto graph = GraphBuilder{}.AddPass(0).Build();
    std::vector<PassPsoConfig> configs = {{.primaryPso = psoA, .missPolicy = PsoMissPolicy::Skip}};

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].isSkipped);
    EXPECT_EQ(results[0].appliedPolicy, PsoMissPolicy::Skip);

    EXPECT_EQ(handler.GetSkippedPassCount(), 1u);
    EXPECT_EQ(handler.GetStats().skippedPasses, 1u);
}

TEST_F(PsoMissHandlerTest, PendingStatus_FallbackPolicy_WithFallbackPso) {
    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Pending; });
    auto graph = GraphBuilder{}.AddPass(0).Build();
    std::vector<PassPsoConfig> configs
        = {{.primaryPso = psoA, .fallbackPso = fallbackPso, .missPolicy = PsoMissPolicy::Fallback}};

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].isSkipped);
    EXPECT_FALSE(results[0].isPrimary);
    EXPECT_EQ(results[0].activePso, fallbackPso);
    EXPECT_EQ(results[0].appliedPolicy, PsoMissPolicy::Fallback);

    EXPECT_EQ(handler.GetFallbackPassCount(), 1u);
    EXPECT_EQ(handler.GetStats().fallbackPasses, 1u);
}

TEST_F(PsoMissHandlerTest, PendingStatus_FallbackPolicy_NoFallbackPso) {
    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Pending; });
    auto graph = GraphBuilder{}.AddPass(0).Build();
    std::vector<PassPsoConfig> configs
        = {{.primaryPso = psoA, .fallbackPso = PipelineHandle{}, .missPolicy = PsoMissPolicy::Fallback}};

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].isSkipped);  // degrades to skip
    EXPECT_EQ(results[0].appliedPolicy, PsoMissPolicy::Skip);

    EXPECT_EQ(handler.GetSkippedPassCount(), 1u);
}

TEST_F(PsoMissHandlerTest, PendingStatus_StallPolicy) {
    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Pending; });
    auto graph = GraphBuilder{}.AddPass(0).Build();
    std::vector<PassPsoConfig> configs = {{.primaryPso = psoA, .missPolicy = PsoMissPolicy::Stall}};

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].isSkipped);
    EXPECT_TRUE(results[0].isPrimary);
    EXPECT_EQ(results[0].activePso, psoA);
    EXPECT_EQ(results[0].appliedPolicy, PsoMissPolicy::Stall);

    EXPECT_EQ(handler.GetStats().stalledPasses, 1u);
    EXPECT_EQ(handler.GetSkippedPassCount(), 0u);
}

TEST_F(PsoMissHandlerTest, FailedStatus_WithFallback) {
    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Failed; });
    auto graph = GraphBuilder{}.AddPass(0).Build();
    std::vector<PassPsoConfig> configs
        = {{.primaryPso = psoA, .fallbackPso = fallbackPso, .missPolicy = PsoMissPolicy::Skip}};

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].isSkipped);
    EXPECT_FALSE(results[0].isPrimary);
    EXPECT_EQ(results[0].activePso, fallbackPso);
    EXPECT_EQ(results[0].appliedPolicy, PsoMissPolicy::Fallback);

    const auto& stats = handler.GetStats();
    EXPECT_EQ(stats.failedPasses, 1u);
    EXPECT_EQ(stats.fallbackPasses, 1u);
}

TEST_F(PsoMissHandlerTest, FailedStatus_NoFallback) {
    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Failed; });
    auto graph = GraphBuilder{}.AddPass(0).Build();
    std::vector<PassPsoConfig> configs = {{.primaryPso = psoA, .missPolicy = PsoMissPolicy::Fallback}};

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].isSkipped);

    const auto& stats = handler.GetStats();
    EXPECT_EQ(stats.failedPasses, 1u);
    EXPECT_EQ(stats.skippedPasses, 1u);
}

TEST_F(PsoMissHandlerTest, MixedStatuses) {
    // 3 passes: Ready, Pending+Skip, Pending+Fallback
    handler.SetReadinessQuery([this](PipelineHandle h) -> PsoCompileStatus {
        if (h == psoA) {
            return PsoCompileStatus::Ready;
        }
        return PsoCompileStatus::Pending;
    });
    auto graph = GraphBuilder{}.AddPass(0).AddPass(1).AddPass(2).Build();
    std::vector<PassPsoConfig> configs = {
        {.primaryPso = psoA, .missPolicy = PsoMissPolicy::Skip},
        {.primaryPso = psoB, .missPolicy = PsoMissPolicy::Skip},
        {.primaryPso = psoB, .fallbackPso = fallbackPso, .missPolicy = PsoMissPolicy::Fallback},
    };

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 3u);
    EXPECT_FALSE(results[0].isSkipped);
    EXPECT_EQ(results[0].activePso, psoA);
    EXPECT_TRUE(results[1].isSkipped);
    EXPECT_FALSE(results[2].isSkipped);
    EXPECT_EQ(results[2].activePso, fallbackPso);

    EXPECT_EQ(handler.GetReadyPassCount(), 1u);
    EXPECT_EQ(handler.GetSkippedPassCount(), 1u);
    EXPECT_EQ(handler.GetFallbackPassCount(), 1u);
}

TEST_F(PsoMissHandlerTest, FewerConfigsThanPasses) {
    auto graph = GraphBuilder{}.AddPass(0).AddPass(1).AddPass(2).Build();
    // Only 1 config for 3 passes
    std::vector<PassPsoConfig> configs = {{.primaryPso = psoA, .missPolicy = PsoMissPolicy::Skip}};

    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Ready; });
    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 3u);
    EXPECT_FALSE(results[0].isSkipped);
    EXPECT_EQ(results[0].activePso, psoA);
    // Passes 1 and 2 have no config — treated as ready (no PSO needed)
    EXPECT_FALSE(results[1].isSkipped);
    EXPECT_FALSE(results[2].isSkipped);
    EXPECT_EQ(handler.GetReadyPassCount(), 3u);
}

// =============================================================================
// 5. Transitive DCE propagation
// =============================================================================

TEST_F(PsoMissHandlerTest, TransitiveDCE_LinearChain) {
    // P0 -> R0 -> P1 -> R1 -> P2
    // If P0 is skipped, P1 and P2 should be transitively skipped
    auto graph = GraphBuilder{}
                     .AddPass(0)
                     .AddPass(1)
                     .AddPass(2)
                     .AddEdge(0, 1, 0)  // P0 writes R0, P1 reads R0
                     .AddEdge(1, 2, 1)  // P1 writes R1, P2 reads R1
                     .Build();

    handler.SetReadinessQuery([this](PipelineHandle h) -> PsoCompileStatus {
        if (h == psoA) {
            return PsoCompileStatus::Pending;
        }
        return PsoCompileStatus::Ready;
    });
    std::vector<PassPsoConfig> configs = {
        {.primaryPso = psoA, .missPolicy = PsoMissPolicy::Skip},  // P0: pending, skip
        {.primaryPso = psoB, .missPolicy = PsoMissPolicy::Skip},  // P1: ready
        {.primaryPso = psoB, .missPolicy = PsoMissPolicy::Skip},  // P2: ready
    };

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 3u);
    EXPECT_TRUE(results[0].isSkipped);  // direct skip
    EXPECT_TRUE(results[1].isSkipped);  // transitive: only input R0 not produced
    EXPECT_TRUE(results[2].isSkipped);  // transitive: only input R1 not produced

    const auto& stats = handler.GetStats();
    EXPECT_EQ(stats.skippedPasses, 1u);    // P0 is direct
    EXPECT_EQ(stats.transitiveSkips, 2u);  // P1 and P2
    EXPECT_EQ(handler.GetSkippedPassCount(), 3u);
}

TEST_F(PsoMissHandlerTest, TransitiveDCE_FanOut) {
    // P0 -> R0 -> P1
    //     -> R1 -> P2
    // P0 skipped => R0, R1 not produced => P1, P2 both skipped
    auto graph = GraphBuilder{}.AddPass(0).AddPass(1).AddPass(2).AddEdge(0, 1, 0).AddEdge(0, 2, 1).Build();

    handler.SetReadinessQuery([this](PipelineHandle h) -> PsoCompileStatus {
        if (h == psoA) {
            return PsoCompileStatus::Pending;
        }
        return PsoCompileStatus::Ready;
    });
    std::vector<PassPsoConfig> configs = {
        {.primaryPso = psoA, .missPolicy = PsoMissPolicy::Skip},
        {.primaryPso = psoB, .missPolicy = PsoMissPolicy::Skip},
        {.primaryPso = psoB, .missPolicy = PsoMissPolicy::Skip},
    };

    auto results = handler.ResolveAll(graph, configs);
    EXPECT_TRUE(results[0].isSkipped);
    EXPECT_TRUE(results[1].isSkipped);
    EXPECT_TRUE(results[2].isSkipped);
    EXPECT_EQ(handler.GetStats().transitiveSkips, 2u);
}

TEST_F(PsoMissHandlerTest, TransitiveDCE_DiamondPartialSkip) {
    //     P0 (ready)
    //    /          \
    //   R0          R1
    //   |            |
    //  P1(skip)    P2(ready)
    //    \          /
    //     R2      R3
    //      \      /
    //       P3(ready)
    //
    // P1 is skipped => R2 not produced.
    // P3 reads both R2 (not produced) and R3 (produced by P2).
    // P3 should NOT be transitively skipped — it has at least one produced input.
    auto psoC = MakePipeline(3);
    auto psoD = MakePipeline(4);
    auto graph = GraphBuilder{}
                     .AddPass(0)
                     .AddPass(1)
                     .AddPass(2)
                     .AddPass(3)
                     .AddEdge(0, 1, 0)  // P0->R0->P1
                     .AddEdge(0, 2, 1)  // P0->R1->P2
                     .AddEdge(1, 3, 2)  // P1->R2->P3
                     .AddEdge(2, 3, 3)  // P2->R3->P3
                     .Build();

    handler.SetReadinessQuery([this, &psoC, &psoD](PipelineHandle h) -> PsoCompileStatus {
        if (h == psoB) {
            return PsoCompileStatus::Pending;  // P1's PSO pending
        }
        return PsoCompileStatus::Ready;
    });
    std::vector<PassPsoConfig> configs = {
        {.primaryPso = psoA, .missPolicy = PsoMissPolicy::Skip},  // P0 ready
        {.primaryPso = psoB, .missPolicy = PsoMissPolicy::Skip},  // P1 pending => skip
        {.primaryPso = psoC, .missPolicy = PsoMissPolicy::Skip},  // P2 ready
        {.primaryPso = psoD, .missPolicy = PsoMissPolicy::Skip},  // P3 ready
    };

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 4u);
    EXPECT_FALSE(results[0].isSkipped);
    EXPECT_TRUE(results[1].isSkipped);  // direct skip
    EXPECT_FALSE(results[2].isSkipped);
    EXPECT_FALSE(results[3].isSkipped);  // NOT transitively skipped — R3 from P2 is produced

    EXPECT_EQ(handler.GetStats().transitiveSkips, 0u);
}

TEST_F(PsoMissHandlerTest, TransitiveDCE_DiamondFullSkip) {
    // Same diamond but BOTH P1 and P2 are skipped => P3 also transitively skipped
    // P3's own PSO is ready, but all its inputs are from skipped passes.
    auto psoC = MakePipeline(3);
    auto psoD = MakePipeline(4);
    auto graph = GraphBuilder{}
                     .AddPass(0)
                     .AddPass(1)
                     .AddPass(2)
                     .AddPass(3)
                     .AddEdge(0, 1, 0)
                     .AddEdge(0, 2, 1)
                     .AddEdge(1, 3, 2)
                     .AddEdge(2, 3, 3)
                     .Build();

    // psoA = ready (P0), psoB/psoC = pending (P1/P2), psoD = ready (P3)
    handler.SetReadinessQuery([this, &psoD](PipelineHandle h) -> PsoCompileStatus {
        if (h == psoA || h == psoD) {
            return PsoCompileStatus::Ready;
        }
        return PsoCompileStatus::Pending;
    });
    std::vector<PassPsoConfig> configs = {
        {.primaryPso = psoA, .missPolicy = PsoMissPolicy::Skip},  // P0: ready
        {.primaryPso = psoB, .missPolicy = PsoMissPolicy::Skip},  // P1: pending => direct skip
        {.primaryPso = psoC, .missPolicy = PsoMissPolicy::Skip},  // P2: pending => direct skip
        {.primaryPso = psoD, .missPolicy = PsoMissPolicy::Skip},  // P3: ready but inputs missing
    };

    auto results = handler.ResolveAll(graph, configs);
    EXPECT_FALSE(results[0].isSkipped);
    EXPECT_TRUE(results[1].isSkipped);                  // direct skip
    EXPECT_TRUE(results[2].isSkipped);                  // direct skip
    EXPECT_TRUE(results[3].isSkipped);                  // transitively skipped (R2 + R3 both not produced)
    EXPECT_EQ(handler.GetStats().skippedPasses, 2u);    // P1, P2
    EXPECT_EQ(handler.GetStats().transitiveSkips, 1u);  // P3
}

TEST_F(PsoMissHandlerTest, TransitiveDCE_PropagatesThroughFallbackPass) {
    // P0(skip) -> R0 -> P1(fallback PSO) -> R1 -> P2(ready)
    // P0 is skipped => R0 not produced.
    // P1 uses a fallback PSO (not skipped by PSO resolution),
    // but P1's only input R0 is not produced => P1 is transitively skipped.
    // Then P2's only input R1 (from P1) is not produced => P2 also transitively skipped.
    // This is correct: fallback PSO doesn't save a pass whose inputs are missing.
    auto psoC = MakePipeline(3);
    auto graph = GraphBuilder{}.AddPass(0).AddPass(1).AddPass(2).AddEdge(0, 1, 0).AddEdge(1, 2, 1).Build();

    handler.SetReadinessQuery([this](PipelineHandle h) -> PsoCompileStatus {
        if (h == psoA) {
            return PsoCompileStatus::Pending;  // P0
        }
        if (h == psoB) {
            return PsoCompileStatus::Pending;  // P1 primary pending
        }
        return PsoCompileStatus::Ready;  // P2
    });
    std::vector<PassPsoConfig> configs = {
        {.primaryPso = psoA, .missPolicy = PsoMissPolicy::Skip},
        {.primaryPso = psoB, .fallbackPso = fallbackPso, .missPolicy = PsoMissPolicy::Fallback},
        {.primaryPso = psoC, .missPolicy = PsoMissPolicy::Skip},
    };

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 3u);
    EXPECT_TRUE(results[0].isSkipped);  // P0: direct skip (pending + skip policy)
    EXPECT_TRUE(results[1].isSkipped);  // P1: transitive skip (input R0 not produced)
    EXPECT_TRUE(results[2].isSkipped);  // P2: transitive skip (input R1 not produced)

    EXPECT_EQ(handler.GetStats().skippedPasses, 1u);    // P0 only
    EXPECT_EQ(handler.GetStats().transitiveSkips, 2u);  // P1 + P2
    EXPECT_EQ(handler.GetStats().fallbackPasses, 1u);   // P1 resolved to fallback before DCE
}

TEST_F(PsoMissHandlerTest, TransitiveDCE_FallbackPassWithMixedInputsSurvives) {
    // P0(skip) -> R0 -> P2(fallback)
    // P1(ready) -> R1 -> P2(fallback)
    // P2 has mixed inputs: R0 (not produced) + R1 (produced)
    // => P2 should NOT be transitively skipped (at least one input is available)
    auto psoC = MakePipeline(3);
    auto graph = GraphBuilder{}.AddPass(0).AddPass(1).AddPass(2).AddEdge(0, 2, 0).AddEdge(1, 2, 1).Build();

    handler.SetReadinessQuery([this, &psoC](PipelineHandle h) -> PsoCompileStatus {
        if (h == psoA) {
            return PsoCompileStatus::Pending;  // P0
        }
        if (h == psoC) {
            return PsoCompileStatus::Pending;  // P2 primary
        }
        return PsoCompileStatus::Ready;  // P1
    });
    std::vector<PassPsoConfig> configs = {
        {.primaryPso = psoA, .missPolicy = PsoMissPolicy::Skip},
        {.primaryPso = psoB, .missPolicy = PsoMissPolicy::Skip},
        {.primaryPso = psoC, .fallbackPso = fallbackPso, .missPolicy = PsoMissPolicy::Fallback},
    };

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 3u);
    EXPECT_TRUE(results[0].isSkipped);   // P0: direct skip
    EXPECT_FALSE(results[1].isSkipped);  // P1: ready
    EXPECT_FALSE(results[2].isSkipped);  // P2: fallback, but R1 from P1 is produced
    EXPECT_EQ(results[2].activePso, fallbackPso);
    EXPECT_FALSE(results[2].isPrimary);
}

TEST_F(PsoMissHandlerTest, TransitiveDCE_NoInputsNeverSkipped) {
    // A pass with no incoming edges (source node) should never be transitively skipped
    auto graph = GraphBuilder{}
                     .AddPass(0)  // no edges at all
                     .Build();

    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Ready; });
    std::vector<PassPsoConfig> configs = {{.primaryPso = psoA, .missPolicy = PsoMissPolicy::Skip}};

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].isSkipped);
}

TEST_F(PsoMissHandlerTest, TransitiveDCE_DeepChain10) {
    // P0 -> P1 -> P2 -> ... -> P9
    // P0 skipped => all 9 downstream transitively skipped
    constexpr uint32_t kChainLength = 10;
    GraphBuilder gb;
    for (uint32_t i = 0; i < kChainLength; ++i) {
        gb.AddPass(i);
    }
    for (uint32_t i = 0; i + 1 < kChainLength; ++i) {
        gb.AddEdge(i, i + 1, static_cast<uint16_t>(i));
    }
    auto graph = gb.Build();

    handler.SetReadinessQuery([this](PipelineHandle h) -> PsoCompileStatus {
        return h == psoA ? PsoCompileStatus::Pending : PsoCompileStatus::Ready;
    });

    std::vector<PassPsoConfig> configs;
    configs.push_back({.primaryPso = psoA, .missPolicy = PsoMissPolicy::Skip});  // P0 pending
    for (uint32_t i = 1; i < kChainLength; ++i) {
        configs.push_back({.primaryPso = psoB, .missPolicy = PsoMissPolicy::Skip});
    }

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), kChainLength);
    for (uint32_t i = 0; i < kChainLength; ++i) {
        EXPECT_TRUE(results[i].isSkipped) << "Pass " << i << " should be skipped";
    }
    EXPECT_EQ(handler.GetStats().skippedPasses, 1u);
    EXPECT_EQ(handler.GetStats().transitiveSkips, kChainLength - 1);
}

// =============================================================================
// 6. FormatStatus string formatting
// =============================================================================

TEST(PsoMissHandlerFormat, EmptyGraph) {
    PsoMissHandler handler;
    EXPECT_EQ(handler.FormatStatus(), "PSO: no passes");
}

TEST(PsoMissHandlerFormat, AllReady) {
    PsoMissHandler handler;
    auto graph = GraphBuilder{}.AddPass(0).AddPass(1).AddPass(2).Build();
    handler.ResolveAll(graph, {});  // no configs = all ready
    EXPECT_EQ(handler.FormatStatus(), "PSO: 3/3 ready");
}

TEST(PsoMissHandlerFormat, WithFallback) {
    PsoMissHandler handler;
    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Pending; });
    auto graph = GraphBuilder{}.AddPass(0).AddPass(1).Build();
    std::vector<PassPsoConfig> configs = {
        {.primaryPso = MakePipeline(1), .fallbackPso = MakePipeline(100), .missPolicy = PsoMissPolicy::Fallback},
        {.primaryPso = MakePipeline(2), .fallbackPso = MakePipeline(101), .missPolicy = PsoMissPolicy::Fallback},
    };
    handler.ResolveAll(graph, configs);
    auto status = handler.FormatStatus();
    EXPECT_NE(status.find("2 fallback"), std::string::npos) << "Got: " << status;
    EXPECT_NE(status.find("PSO: 0/2 ready"), std::string::npos) << "Got: " << status;
}

TEST(PsoMissHandlerFormat, WithSkippedAndTransitive) {
    PsoMissHandler handler;
    handler.SetReadinessQuery([](PipelineHandle h) -> PsoCompileStatus {
        return h.value == 1 ? PsoCompileStatus::Pending : PsoCompileStatus::Ready;
    });
    auto graph = GraphBuilder{}.AddPass(0).AddPass(1).AddEdge(0, 1, 0).Build();
    std::vector<PassPsoConfig> configs = {
        {.primaryPso = MakePipeline(1), .missPolicy = PsoMissPolicy::Skip},
        {.primaryPso = MakePipeline(2), .missPolicy = PsoMissPolicy::Skip},
    };
    handler.ResolveAll(graph, configs);
    auto status = handler.FormatStatus();
    EXPECT_NE(status.find("skipped"), std::string::npos) << "Got: " << status;
    EXPECT_NE(status.find("transitive"), std::string::npos) << "Got: " << status;
}

TEST(PsoMissHandlerFormat, WithFailed) {
    PsoMissHandler handler;
    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Failed; });
    auto graph = GraphBuilder{}.AddPass(0).Build();
    std::vector<PassPsoConfig> configs = {{.primaryPso = MakePipeline(1), .missPolicy = PsoMissPolicy::Skip}};
    handler.ResolveAll(graph, configs);
    auto status = handler.FormatStatus();
    EXPECT_NE(status.find("FAILED"), std::string::npos) << "Got: " << status;
}

// =============================================================================
// 7. Adversarial / edge-case / stress scenarios
// =============================================================================

TEST(ExtensionRegistry_Stress, MassRegisterUnregister) {
    ExtensionRegistry registry;
    constexpr int kCount = 100;

    for (int i = 0; i < kCount; ++i) {
        auto name = "Ext_" + std::to_string(i);
        registry.RegisterExtension(std::make_unique<MockExtension>(name, i));
    }
    EXPECT_EQ(registry.GetExtensionCount(), kCount);

    // Verify priority ordering
    RenderGraphBuilder builder;
    ExtensionBuildContext ctx{};
    registry.InvokeExtensions(builder, ctx);
    auto names = registry.GetExtensionNames();
    for (int i = 0; i + 1 < kCount; ++i) {
        auto* extA = registry.FindExtension(names[i]);
        auto* extB = registry.FindExtension(names[i + 1]);
        ASSERT_NE(extA, nullptr);
        ASSERT_NE(extB, nullptr);
        EXPECT_LE(extA->GetPriority(), extB->GetPriority())
            << "At index " << i << ": " << names[i] << " (pri=" << extA->GetPriority()
            << ") should be <= " << names[i + 1] << " (pri=" << extB->GetPriority() << ")";
    }

    // Unregister all even-numbered
    for (int i = 0; i < kCount; i += 2) {
        EXPECT_TRUE(registry.UnregisterExtension("Ext_" + std::to_string(i)));
    }
    EXPECT_EQ(registry.GetExtensionCount(), kCount / 2);
}

TEST(ExtensionRegistry_Stress, RapidReplacement) {
    LifecycleLog log;
    ExtensionRegistry registry;

    // Replace the same extension 50 times
    for (int i = 0; i < 50; ++i) {
        registry.RegisterExtension(std::make_unique<MockExtension>("Same", i, ExtensionCapability::None, &log));
    }

    EXPECT_EQ(registry.GetExtensionCount(), 1u);
    EXPECT_EQ(registry.FindExtension("Same")->GetPriority(), 49);  // last one
    // Should have 50 OnRegistered, 49 OnUnregistered (first register has no predecessor)
    EXPECT_EQ(log.Count("Same::OnRegistered"), 50);
    EXPECT_EQ(log.Count("Same::OnUnregistered"), 49);
}

TEST(ExtensionRegistry_Stress, AllExtensionsDisabled) {
    ExtensionRegistry registry;
    for (int i = 0; i < 10; ++i) {
        auto name = "Ext_" + std::to_string(i);
        registry.RegisterExtension(std::make_unique<MockExtension>(name, i * 100));
        registry.SetEnabled(name, false);
    }

    RenderGraphBuilder builder;
    ExtensionBuildContext ctx{};
    auto invoked = registry.InvokeExtensions(builder, ctx);
    EXPECT_EQ(invoked, 0u);
    EXPECT_EQ(builder.GetPassCount(), 0u);

    const auto& stats = registry.GetLastInvocationStats();
    EXPECT_EQ(stats.totalRegistered, 10u);
    EXPECT_EQ(stats.totalEnabled, 0u);
}

TEST(ExtensionRegistry_Stress, AllExtensionsInactiveThisFrame) {
    ExtensionRegistry registry;
    for (int i = 0; i < 10; ++i) {
        auto ext = std::make_unique<MockExtension>("Ext_" + std::to_string(i), i * 100);
        ext->activeThisFrame = false;
        registry.RegisterExtension(std::move(ext));
    }

    RenderGraphBuilder builder;
    ExtensionBuildContext ctx{};
    auto invoked = registry.InvokeExtensions(builder, ctx);
    EXPECT_EQ(invoked, 0u);

    const auto& stats = registry.GetLastInvocationStats();
    EXPECT_EQ(stats.totalRegistered, 10u);
    EXPECT_EQ(stats.totalEnabled, 10u);
    EXPECT_EQ(stats.totalCapable, 10u);
    EXPECT_EQ(stats.totalActive, 0u);
}

TEST(ExtensionRegistry_Stress, MixedCapabilityFilteringLargeSet) {
    ExtensionRegistry registry;
    registry.SetAvailableCapabilities(ExtensionCapability::AsyncCompute | ExtensionCapability::MeshShader);

    int expectedInvoked = 0;
    for (int i = 0; i < 20; ++i) {
        ExtensionCapability cap;
        if (i % 3 == 0) {
            cap = ExtensionCapability::None;  // always capable
            expectedInvoked++;
        } else if (i % 3 == 1) {
            cap = ExtensionCapability::AsyncCompute;  // available
            expectedInvoked++;
        } else {
            cap = ExtensionCapability::RayTracing;  // NOT available
        }
        registry.RegisterExtension(std::make_unique<MockExtension>("Ext_" + std::to_string(i), i * 10, cap));
    }

    RenderGraphBuilder builder;
    ExtensionBuildContext ctx{};
    auto invoked = registry.InvokeExtensions(builder, ctx);
    EXPECT_EQ(static_cast<int>(invoked), expectedInvoked);
}

TEST(PsoMissHandler_Stress, LargeGraphAllReady) {
    PsoMissHandler handler;
    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Ready; });

    constexpr uint32_t kNumPasses = 200;
    GraphBuilder gb;
    for (uint32_t i = 0; i < kNumPasses; ++i) {
        gb.AddPass(i);
    }
    // Linear chain
    for (uint32_t i = 0; i + 1 < kNumPasses; ++i) {
        gb.AddEdge(i, i + 1, static_cast<uint16_t>(i));
    }
    auto graph = gb.Build();

    std::vector<PassPsoConfig> configs;
    configs.reserve(kNumPasses);
    for (uint32_t i = 0; i < kNumPasses; ++i) {
        configs.push_back({.primaryPso = MakePipeline(i + 1), .missPolicy = PsoMissPolicy::Skip});
    }

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), kNumPasses);
    for (uint32_t i = 0; i < kNumPasses; ++i) {
        EXPECT_FALSE(results[i].isSkipped) << "Pass " << i << " should not be skipped";
        EXPECT_TRUE(results[i].isPrimary);
    }
    EXPECT_EQ(handler.GetStats().readyPasses, kNumPasses);
    EXPECT_EQ(handler.GetStats().transitiveSkips, 0u);
}

TEST(PsoMissHandler_Stress, LargeGraphMidChainSkip) {
    // 50-pass linear chain, pass 25 is pending => 25 downstream passes transitively skipped
    PsoMissHandler handler;
    constexpr uint32_t kNumPasses = 50;
    constexpr uint32_t kSkipAt = 25;

    auto skipPso = MakePipeline(9999);
    handler.SetReadinessQuery([&skipPso](PipelineHandle h) -> PsoCompileStatus {
        return h == skipPso ? PsoCompileStatus::Pending : PsoCompileStatus::Ready;
    });

    GraphBuilder gb;
    for (uint32_t i = 0; i < kNumPasses; ++i) {
        gb.AddPass(i);
    }
    for (uint32_t i = 0; i + 1 < kNumPasses; ++i) {
        gb.AddEdge(i, i + 1, static_cast<uint16_t>(i));
    }
    auto graph = gb.Build();

    std::vector<PassPsoConfig> configs;
    for (uint32_t i = 0; i < kNumPasses; ++i) {
        auto pso = (i == kSkipAt) ? skipPso : MakePipeline(i + 1);
        configs.push_back({.primaryPso = pso, .missPolicy = PsoMissPolicy::Skip});
    }

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), kNumPasses);

    for (uint32_t i = 0; i < kSkipAt; ++i) {
        EXPECT_FALSE(results[i].isSkipped) << "Pass " << i << " before skip point should run";
    }
    EXPECT_TRUE(results[kSkipAt].isSkipped);  // direct skip
    for (uint32_t i = kSkipAt + 1; i < kNumPasses; ++i) {
        EXPECT_TRUE(results[i].isSkipped) << "Pass " << i << " after skip point should be transitively skipped";
    }

    EXPECT_EQ(handler.GetStats().skippedPasses, 1u);
    EXPECT_EQ(handler.GetStats().transitiveSkips, kNumPasses - kSkipAt - 1);
}

TEST(PsoMissHandler_Stress, ComplexDAG_MultipleSources_MultipleSinks) {
    //  S0(skip) --> R0 --> M0 --> R4 --> K0
    //  S1(ready)--> R1 --> M0
    //  S0       --> R2 --> M1 --> R5 --> K1
    //  S1       --> R3 --> M1
    //
    // M0 has two inputs: R0 (not produced) and R1 (produced)
    //   => M0 NOT transitively skipped (at least one input is produced)
    // M1 has two inputs: R2 (not produced) and R3 (produced)
    //   => M1 NOT transitively skipped
    // K0 and K1 have single input from M0/M1 respectively => should be fine
    auto skipPso = MakePipeline(999);
    auto readyPso = MakePipeline(1);

    PsoMissHandler handler;
    handler.SetReadinessQuery([&](PipelineHandle h) -> PsoCompileStatus {
        return h == skipPso ? PsoCompileStatus::Pending : PsoCompileStatus::Ready;
    });

    auto graph = GraphBuilder{}
                     .AddPass(0)
                     .AddPass(1)
                     .AddPass(2)
                     .AddPass(3)
                     .AddPass(4)
                     .AddPass(5)        // S0,S1,M0,M1,K0,K1
                     .AddEdge(0, 2, 0)  // S0->R0->M0
                     .AddEdge(1, 2, 1)  // S1->R1->M0
                     .AddEdge(0, 3, 2)  // S0->R2->M1
                     .AddEdge(1, 3, 3)  // S1->R3->M1
                     .AddEdge(2, 4, 4)  // M0->R4->K0
                     .AddEdge(3, 5, 5)  // M1->R5->K1
                     .Build();

    std::vector<PassPsoConfig> configs = {
        {.primaryPso = skipPso, .missPolicy = PsoMissPolicy::Skip},   // S0: skip
        {.primaryPso = readyPso, .missPolicy = PsoMissPolicy::Skip},  // S1: ready
        {.primaryPso = readyPso, .missPolicy = PsoMissPolicy::Skip},  // M0: ready (mixed inputs)
        {.primaryPso = readyPso, .missPolicy = PsoMissPolicy::Skip},  // M1: ready (mixed inputs)
        {.primaryPso = readyPso, .missPolicy = PsoMissPolicy::Skip},  // K0: ready
        {.primaryPso = readyPso, .missPolicy = PsoMissPolicy::Skip},  // K1: ready
    };

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 6u);
    EXPECT_TRUE(results[0].isSkipped);   // S0 direct skip
    EXPECT_FALSE(results[1].isSkipped);  // S1 ready
    EXPECT_FALSE(results[2].isSkipped);  // M0: has R1 from S1 (produced)
    EXPECT_FALSE(results[3].isSkipped);  // M1: has R3 from S1 (produced)
    EXPECT_FALSE(results[4].isSkipped);  // K0: R4 from M0 (produced)
    EXPECT_FALSE(results[5].isSkipped);  // K1: R5 from M1 (produced)

    EXPECT_EQ(handler.GetStats().transitiveSkips, 0u);
}

TEST(PsoMissHandler_Stress, ResolveAllResetsStatsBetweenCalls) {
    PsoMissHandler handler;
    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Pending; });

    auto graph1 = GraphBuilder{}.AddPass(0).Build();
    std::vector<PassPsoConfig> configs1 = {{.primaryPso = MakePipeline(1), .missPolicy = PsoMissPolicy::Skip}};
    handler.ResolveAll(graph1, configs1);
    EXPECT_EQ(handler.GetStats().skippedPasses, 1u);

    // Second call should reset
    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Ready; });
    auto graph2 = GraphBuilder{}.AddPass(0).AddPass(1).AddPass(2).Build();
    std::vector<PassPsoConfig> configs2 = {
        {.primaryPso = MakePipeline(1), .missPolicy = PsoMissPolicy::Skip},
        {.primaryPso = MakePipeline(2), .missPolicy = PsoMissPolicy::Skip},
        {.primaryPso = MakePipeline(3), .missPolicy = PsoMissPolicy::Skip},
    };
    handler.ResolveAll(graph2, configs2);
    EXPECT_EQ(handler.GetStats().skippedPasses, 0u);
    EXPECT_EQ(handler.GetStats().readyPasses, 3u);
    EXPECT_EQ(handler.GetStats().totalPasses, 3u);
}

TEST(PsoMissHandler_Stress, PerPassReadinessQuery) {
    // Each pass has a distinct PSO with a distinct status
    PsoMissHandler handler;
    auto ready = MakePipeline(1);
    auto pending = MakePipeline(2);
    auto failed = MakePipeline(3);

    handler.SetReadinessQuery([&](PipelineHandle h) -> PsoCompileStatus {
        if (h == ready) {
            return PsoCompileStatus::Ready;
        }
        if (h == pending) {
            return PsoCompileStatus::Pending;
        }
        return PsoCompileStatus::Failed;
    });

    auto graph = GraphBuilder{}.AddPass(0).AddPass(1).AddPass(2).Build();
    auto fb = MakePipeline(100);
    std::vector<PassPsoConfig> configs = {
        {.primaryPso = ready, .missPolicy = PsoMissPolicy::Skip},
        {.primaryPso = pending, .missPolicy = PsoMissPolicy::Stall},
        {.primaryPso = failed, .fallbackPso = fb, .missPolicy = PsoMissPolicy::Fallback},
    };

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 3u);

    // Pass 0: ready
    EXPECT_FALSE(results[0].isSkipped);
    EXPECT_TRUE(results[0].isPrimary);
    EXPECT_EQ(results[0].activePso, ready);

    // Pass 1: pending + stall
    EXPECT_FALSE(results[1].isSkipped);
    EXPECT_TRUE(results[1].isPrimary);
    EXPECT_EQ(results[1].appliedPolicy, PsoMissPolicy::Stall);

    // Pass 2: failed + fallback
    EXPECT_FALSE(results[2].isSkipped);
    EXPECT_FALSE(results[2].isPrimary);
    EXPECT_EQ(results[2].activePso, fb);
    EXPECT_EQ(results[2].appliedPolicy, PsoMissPolicy::Fallback);

    const auto& stats = handler.GetStats();
    EXPECT_EQ(stats.readyPasses, 1u);
    EXPECT_EQ(stats.stalledPasses, 1u);
    EXPECT_EQ(stats.failedPasses, 1u);
    EXPECT_EQ(stats.fallbackPasses, 1u);
}

TEST(ExtensionRegistry_EdgeCase, UnregisterDuringIteration) {
    // Ensure that after InvokeExtensions, unregistering is safe
    LifecycleLog log;
    ExtensionRegistry registry;
    registry.RegisterExtension(std::make_unique<MockExtension>("A", 100, ExtensionCapability::None, &log));
    registry.RegisterExtension(std::make_unique<MockExtension>("B", 200, ExtensionCapability::None, &log));

    RenderGraphBuilder builder;
    ExtensionBuildContext ctx{};
    registry.InvokeExtensions(builder, ctx);

    // Unregister after invocation — should be safe
    EXPECT_TRUE(registry.UnregisterExtension("A"));
    EXPECT_EQ(registry.GetExtensionCount(), 1u);

    // Invoke again — only B should run
    RenderGraphBuilder builder2;
    log.Clear();
    auto invoked = registry.InvokeExtensions(builder2, ctx);
    EXPECT_EQ(invoked, 1u);
    EXPECT_EQ(log.Count("B::BuildPasses"), 1);
    EXPECT_EQ(log.Count("A::BuildPasses"), 0);
}

TEST(ExtensionRegistry_EdgeCase, RegisterAfterShutdownAll) {
    LifecycleLog log;
    ExtensionRegistry registry;
    registry.RegisterExtension(std::make_unique<MockExtension>("A", 100, ExtensionCapability::None, &log));
    registry.ShutdownAll();
    EXPECT_EQ(registry.GetExtensionCount(), 0u);

    // Re-register after shutdown
    log.Clear();
    registry.RegisterExtension(std::make_unique<MockExtension>("B", 200, ExtensionCapability::None, &log));
    EXPECT_EQ(registry.GetExtensionCount(), 1u);
    EXPECT_EQ(log.Count("B::OnRegistered"), 1);

    RenderGraphBuilder builder;
    ExtensionBuildContext ctx{};
    auto invoked = registry.InvokeExtensions(builder, ctx);
    EXPECT_EQ(invoked, 1u);
}

TEST(ExtensionRegistry_EdgeCase, MoveConstruction) {
    LifecycleLog log;
    ExtensionRegistry reg1;
    reg1.RegisterExtension(std::make_unique<MockExtension>("A", 100, ExtensionCapability::None, &log));

    ExtensionRegistry reg2 = std::move(reg1);
    EXPECT_EQ(reg2.GetExtensionCount(), 1u);
    EXPECT_TRUE(reg2.IsRegistered("A"));

    // reg2 destruction should call Shutdown
    log.Clear();
    reg2.ShutdownAll();
    EXPECT_EQ(log.Count("A::Shutdown"), 1);
}

TEST(PsoMissHandler_EdgeCase, GraphWithEdgesButNoPasses) {
    // Degenerate: edges reference non-existent passes
    CompiledRenderGraph graph;
    graph.edges.push_back({.srcPass = 0, .dstPass = 1, .resourceIndex = 0});
    // 0 passes

    PsoMissHandler handler;
    auto results = handler.ResolveAll(graph, {});
    EXPECT_TRUE(results.empty());
}

TEST(PsoMissHandler_EdgeCase, SelfLoop) {
    // Edge where src == dst (degenerate, shouldn't happen but must not crash)
    auto graph = GraphBuilder{}
                     .AddPass(0)
                     .AddEdge(0, 0, 0)  // self-loop
                     .Build();

    PsoMissHandler handler;
    handler.SetReadinessQuery([](PipelineHandle) { return PsoCompileStatus::Ready; });
    std::vector<PassPsoConfig> configs = {{.primaryPso = MakePipeline(1), .missPolicy = PsoMissPolicy::Skip}};

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_FALSE(results[0].isSkipped);
}

TEST(PsoMissHandler_EdgeCase, DisconnectedComponents) {
    // Two separate chains: P0->P1 and P2->P3
    // P0 skipped => P1 transitively skipped, but P2 and P3 unaffected
    PsoMissHandler handler;
    auto skip = MakePipeline(999);
    auto ready = MakePipeline(1);
    handler.SetReadinessQuery([&](PipelineHandle h) -> PsoCompileStatus {
        return h == skip ? PsoCompileStatus::Pending : PsoCompileStatus::Ready;
    });

    auto graph = GraphBuilder{}
                     .AddPass(0)
                     .AddPass(1)
                     .AddPass(2)
                     .AddPass(3)
                     .AddEdge(0, 1, 0)  // component 1
                     .AddEdge(2, 3, 1)  // component 2
                     .Build();

    std::vector<PassPsoConfig> configs = {
        {.primaryPso = skip, .missPolicy = PsoMissPolicy::Skip},   // P0: skip
        {.primaryPso = ready, .missPolicy = PsoMissPolicy::Skip},  // P1: ready
        {.primaryPso = ready, .missPolicy = PsoMissPolicy::Skip},  // P2: ready
        {.primaryPso = ready, .missPolicy = PsoMissPolicy::Skip},  // P3: ready
    };

    auto results = handler.ResolveAll(graph, configs);
    ASSERT_EQ(results.size(), 4u);
    EXPECT_TRUE(results[0].isSkipped);   // P0: direct
    EXPECT_TRUE(results[1].isSkipped);   // P1: transitive
    EXPECT_FALSE(results[2].isSkipped);  // P2: separate component
    EXPECT_FALSE(results[3].isSkipped);  // P3: separate component
}

TEST(ExtensionBuildContext, DefaultValues) {
    ExtensionBuildContext ctx{};
    EXPECT_EQ(ctx.frameContext, nullptr);
    EXPECT_EQ(ctx.availableCapabilities, ExtensionCapability::None);
    EXPECT_EQ(ctx.activeExtensionCount, 0u);
    EXPECT_EQ(ctx.frameNumber, 0u);
    EXPECT_FLOAT_EQ(ctx.deltaTimeSeconds, 0.0f);
}

TEST(PsoQueryResult, DefaultValues) {
    PsoQueryResult result{};
    EXPECT_EQ(result.status, PsoCompileStatus::Pending);
    EXPECT_FALSE(result.handle.IsValid());
}

TEST(PsoResolution, DefaultValues) {
    PsoResolution res{};
    EXPECT_FALSE(res.activePso.IsValid());
    EXPECT_TRUE(res.isPrimary);
    EXPECT_FALSE(res.isSkipped);
    EXPECT_EQ(res.appliedPolicy, PsoMissPolicy::Skip);
}

TEST(PassPsoConfig, DefaultValues) {
    PassPsoConfig cfg{};
    EXPECT_FALSE(cfg.primaryPso.IsValid());
    EXPECT_FALSE(cfg.fallbackPso.IsValid());
    EXPECT_EQ(cfg.missPolicy, PsoMissPolicy::Skip);
}
