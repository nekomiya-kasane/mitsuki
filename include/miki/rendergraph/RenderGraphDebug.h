/** @file RenderGraphDebug.h
 *  @brief Debugging & Profiling tools for the Render Graph (Phase J, §11-12).
 *
 *  J-1: Graphviz DOT export (ExportGraphviz)
 *  J-2: Per-pass GPU timestamps (PassTimingReport)
 *  J-3: Barrier audit log (BarrierAuditEntry, BarrierAuditLog)
 *  J-4: Debug region colors per queue type (GetPassDebugColor)
 *  J-5: Graph diff report (GraphDiffReport)
 *  J-6: RenderGraphValidator (debug-build validation)
 *
 *  Namespace: miki::rg
 */
#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/Handle.h"

#include <nlohmann/json_fwd.hpp>

namespace miki::rhi {
    class CommandListHandle;
}

namespace miki::rg {

    // =========================================================================
    // J-1: Graphviz DOT export (§12.1)
    // =========================================================================

    struct GraphvizOptions {
        bool showCulledPasses = true;       ///< Gray dashed nodes for culled passes
        bool showResourceNodes = true;      ///< Resource nodes as rounded rectangles
        bool showBarrierCounts = true;      ///< Barrier count in pass node labels
        bool showAliasing = true;           ///< Color-code aliased resource groups
        bool showCrossQueueSync = true;     ///< Bold red arrows for cross-queue edges
        bool showTimingEstimates = false;   ///< Show estimatedGpuTimeUs in labels
        bool clusterByQueue = true;         ///< Group passes into subgraphs by queue
        bool showHistoryResources = true;   ///< Show history resource tracking edges
        bool showExternalResources = true;  ///< Distinguish imported resources visually
        const char* graphTitle = "RenderGraph";
    };

    /// @brief Export compiled render graph as Graphviz DOT format.
    /// Nodes colored by queue: green=Graphics, blue=Compute, orange=Transfer, purple=AsyncCompute.
    /// Edges: solid=RAW, dashed=WAR, dotted=WAW. Cross-queue edges: bold red with timeline values.
    /// Culled passes: gray fill, dashed border. Aliased resources: shared color annotation.
    void ExportGraphviz(
        const CompiledRenderGraph& graph, const RenderGraphBuilder& builder, std::ostream& out,
        const GraphvizOptions& options = {}
    );

    /// @brief Export to DOT string (convenience wrapper).
    [[nodiscard]] auto ExportGraphvizString(
        const CompiledRenderGraph& graph, const RenderGraphBuilder& builder, const GraphvizOptions& options = {}
    ) -> std::string;

    // =========================================================================
    // J-1b: Mermaid flowchart export (§12.1)
    // =========================================================================

    struct MermaidOptions {
        bool showBarrierCounts = true;     ///< Show acquire/release barrier count in node label
        bool showCrossQueueSync = true;    ///< Show cross-queue sync edges with timeline values
        bool clusterByBatch = true;        ///< Group passes into subgraphs by CommandBatch
        bool showResourceEdges = true;     ///< Show resource read/write edges
        bool showAliasing = true;          ///< Annotate aliased resources
        const char* direction = "TD";      ///< TD (top-down) or LR (left-right)
    };

    /// @brief Export compiled render graph as Mermaid flowchart.
    void ExportMermaid(
        const CompiledRenderGraph& graph, const RenderGraphBuilder& builder, std::ostream& out,
        const MermaidOptions& options = {}
    );

    /// @brief Export to Mermaid string (convenience wrapper).
    [[nodiscard]] auto ExportMermaidString(
        const CompiledRenderGraph& graph, const RenderGraphBuilder& builder, const MermaidOptions& options = {}
    ) -> std::string;

    // =========================================================================
    // J-2: Per-pass GPU timestamps (§12.2)
    // =========================================================================

    struct PassTimingEntry {
        const char* name = nullptr;
        RGQueueType queue = RGQueueType::Graphics;
        float gpuTimeMs = 0.0f;      ///< From timestamp queries (2-frame readback latency)
        float cpuRecordMs = 0.0f;    ///< Wall-clock recording time for this pass
        uint32_t barrierCount = 0;   ///< Acquire + release barriers for this pass
        uint32_t drawCallCount = 0;  ///< Draw calls emitted (populated by executor)
        uint32_t dispatchCount = 0;  ///< Dispatch calls emitted (populated by executor)
    };

    struct PassTimingReport {
        std::vector<PassTimingEntry> passes;
        float totalFrameGpuMs = 0.0f;
        float totalFrameCpuMs = 0.0f;
        float asyncComputeOverlapMs = 0.0f;  ///< Time saved by async compute overlap
        float aliasingMemorySavedMB = 0.0f;  ///< VRAM saved by transient aliasing
        uint64_t frameIndex = 0;

        /// @brief Populate barrierCount for each pass from a compiled graph.
        void PopulateBarrierCounts(const CompiledRenderGraph& graph);

        /// @brief Compute totalFrameGpuMs from per-pass timings (simple sum, ignores overlap).
        void ComputeTotals();

        /// @brief Format a human-readable summary table (pass name, queue, gpu ms, cpu ms, barriers).
        [[nodiscard]] auto FormatTable() const -> std::string;
    };

    // =========================================================================
    // J-3: Barrier audit log (§12.3)
    // =========================================================================

    struct BarrierAuditEntry {
        uint32_t passIndex = 0;
        const char* passName = nullptr;
        const char* resourceName = nullptr;
        uint16_t resourceIndex = 0;
        ResourceAccess srcAccess = ResourceAccess::None;
        ResourceAccess dstAccess = ResourceAccess::None;
        rhi::TextureLayout srcLayout = rhi::TextureLayout::Undefined;
        rhi::TextureLayout dstLayout = rhi::TextureLayout::Undefined;
        bool isSplitRelease = false;
        bool isSplitAcquire = false;
        bool isCrossQueue = false;
        bool isAliasingBarrier = false;
        bool isFenceBarrier = false;
        RGQueueType srcQueue = RGQueueType::Graphics;
        RGQueueType dstQueue = RGQueueType::Graphics;
    };

    struct BarrierAuditLog {
        std::vector<BarrierAuditEntry> entries;
        uint64_t frameIndex = 0;

        /// @brief Populate the audit log from a compiled graph's barrier commands.
        void PopulateFromCompiled(const CompiledRenderGraph& graph, const RenderGraphBuilder& builder);

        /// @brief Count barriers by category.
        [[nodiscard]] auto CountTotal() const -> uint32_t { return static_cast<uint32_t>(entries.size()); }
        [[nodiscard]] auto CountCrossQueue() const -> uint32_t;
        [[nodiscard]] auto CountSplitBarriers() const -> uint32_t;
        [[nodiscard]] auto CountAliasingBarriers() const -> uint32_t;

        /// @brief Format human-readable summary.
        [[nodiscard]] auto FormatSummary() const -> std::string;

        /// @brief Export as JSON object (nlohmann/json).
        [[nodiscard]] auto ToJson() const -> nlohmann::json;

        /// @brief Export as JSON string (convenience).
        [[nodiscard]] auto ToJsonString(bool pretty = false) const -> std::string;
    };

    // =========================================================================
    // J-4: Debug region colors per queue type (§12.4)
    // =========================================================================

    /// @brief Get RGBA debug color for a pass based on its queue type.
    /// Graphics=green, Compute=blue, Transfer=orange, AsyncCompute=purple.
    /// Matches the color scheme in DebugMarker.h and spec §12.4.
    constexpr auto GetPassDebugColor(RGQueueType queue) noexcept -> const float* {
        static constexpr float kGraphics[4] = {0.2f, 0.8f, 0.2f, 1.0f};      // Green
        static constexpr float kAsyncCompute[4] = {0.6f, 0.2f, 0.8f, 1.0f};  // Purple (spec §12.4)
        static constexpr float kTransfer[4] = {1.0f, 0.6f, 0.2f, 1.0f};      // Orange
        switch (queue) {
            case RGQueueType::Graphics: return kGraphics;
            case RGQueueType::AsyncCompute: return kAsyncCompute;
            case RGQueueType::Transfer: return kTransfer;
            default: return kGraphics;
        }
    }

    /// @brief Pack RGBA floats [0,1] into a single uint32_t (0xRRGGBBAA) for PIX/Nsight.
    constexpr auto PackDebugColorU32(const float color[4]) noexcept -> uint32_t {
        auto r = static_cast<uint32_t>(color[0] * 255.0f);
        auto g = static_cast<uint32_t>(color[1] * 255.0f);
        auto b = static_cast<uint32_t>(color[2] * 255.0f);
        auto a = static_cast<uint32_t>(color[3] * 255.0f);
        return (r << 24) | (g << 16) | (b << 8) | a;
    }

    // =========================================================================
    // J-5: Graph diff report (§12.5)
    // =========================================================================

    enum class DiffAction : uint8_t {
        Added,
        Removed,
        Modified
    };

    struct PassDiffEntry {
        DiffAction action = DiffAction::Added;
        const char* passName = nullptr;
        std::string reason;  ///< e.g., "condition became true", "dead code: RTAO enabled"
    };

    struct ResourceDiffEntry {
        DiffAction action = DiffAction::Added;
        const char* resourceName = nullptr;
        std::string detail;  ///< e.g., "half-res -> full-res", "format changed RGBA8 -> RGBA16F"
    };

    struct GraphDiffReport {
        uint64_t frameIndex = 0;
        uint64_t prevTopologyHash = 0;
        uint64_t newTopologyHash = 0;
        uint64_t prevDescHash = 0;
        uint64_t newDescHash = 0;
        std::vector<PassDiffEntry> passDiffs;
        std::vector<ResourceDiffEntry> resourceDiffs;
        float recompilationTimeUs = 0.0f;
        bool wasIncrementalRecompile = false;

        /// @brief Generate diff report by comparing old and new builder states.
        static auto Generate(
            const RenderGraphBuilder& oldBuilder, const std::vector<bool>& oldActiveSet,
            const RenderGraphBuilder& newBuilder, const std::vector<bool>& newActiveSet, uint64_t frameIndex,
            float recompileTimeUs, bool incremental
        ) -> GraphDiffReport;

        /// @brief Serialize to JSON object (nlohmann/json).
        [[nodiscard]] auto ToJson() const -> nlohmann::json;

        /// @brief Serialize to JSON string (convenience).
        [[nodiscard]] auto ToJsonString(bool pretty = false) const -> std::string;

        /// @brief Human-readable summary line (for log output).
        [[nodiscard]] auto Summary() const -> std::string;

        /// @brief True if there are any diffs.
        [[nodiscard]] auto HasDiffs() const noexcept -> bool { return !passDiffs.empty() || !resourceDiffs.empty(); }
    };

    // =========================================================================
    // J-6: RenderGraphValidator (§11.2) — debug builds only
    // =========================================================================

    struct ValidationWarning {
        enum class Category : uint8_t {
            UninitializedRead,  ///< Resource read before any write in current frame
            AliasingOverlap,    ///< Overlapping lifetimes in same aliasing slot
            MissingBarrier,     ///< Required barrier not found in emitted set
            RedundantBarrier,   ///< Barrier present but not required
            TimelineViolation,  ///< Wait on future value, or double-signal
            CrossQueueRace,     ///< Cross-queue access without sync
            StaleHistory,       ///< History read with frame gap > policy threshold
        };

        Category category = Category::UninitializedRead;
        uint32_t passIndex = 0;
        const char* passName = nullptr;
        uint16_t resourceIndex = 0;
        const char* resourceName = nullptr;
        std::string detail;
    };

    struct ValidationReport {
        std::vector<ValidationWarning> warnings;
        uint32_t errorCount = 0;    ///< Subset of warnings that are fatal (Category < RedundantBarrier)
        uint32_t warningCount = 0;  ///< Non-fatal warnings (RedundantBarrier, StaleHistory)

        [[nodiscard]] auto IsClean() const noexcept -> bool { return errorCount == 0; }
        [[nodiscard]] auto FormatReport() const -> std::string;
    };

    class RenderGraphValidator {
       public:
        /// @brief Validate uninitialized resource reads (resource read before any write).
        [[nodiscard]] auto CheckUninitializedReads(const CompiledRenderGraph& graph, const RenderGraphBuilder& builder)
            -> std::vector<ValidationWarning>;

        /// @brief Validate aliasing correctness (no overlapping lifetimes in same slot).
        [[nodiscard]] auto ValidateAliasing(const CompiledRenderGraph& graph) -> std::vector<ValidationWarning>;

        /// @brief Audit barriers: compare emitted barriers against required barriers.
        /// @param emitted The actual barrier audit log (from BarrierAuditLog::PopulateFromCompiled).
        /// @param graph The compiled graph.
        /// @param builder The original builder (for resource names).
        /// @return Warnings for missing and redundant barriers.
        [[nodiscard]] auto AuditBarriers(
            const BarrierAuditLog& emitted, const CompiledRenderGraph& graph, const RenderGraphBuilder& builder
        ) -> std::vector<ValidationWarning>;

        /// @brief Validate cross-queue sync: every cross-queue edge has timeline semaphore.
        [[nodiscard]] auto ValidateCrossQueueSync(const CompiledRenderGraph& graph, const RenderGraphBuilder& builder)
            -> std::vector<ValidationWarning>;

        /// @brief Run all validations and produce a combined report.
        [[nodiscard]] auto ValidateAll(
            const CompiledRenderGraph& graph, const RenderGraphBuilder& builder,
            const BarrierAuditLog* auditLog = nullptr
        ) -> ValidationReport;
    };

    // =========================================================================
    // Phase E integration: per-pass debug region helpers (§12.4)
    // =========================================================================

    /// @brief Begin a GPU debug region for a render graph pass.
    /// Wraps CmdBeginDebugLabel with queue-type color from GetPassDebugColor.
    void BeginPassDebugRegion(rhi::CommandListHandle& cmd, const char* passName, RGQueueType queue);

    /// @brief End a GPU debug region for a render graph pass.
    void EndPassDebugRegion(rhi::CommandListHandle& cmd);

    /// @brief Insert a single-shot debug marker (e.g., "barrier batch", "aliasing discard").
    void InsertPassDebugMarker(rhi::CommandListHandle& cmd, const char* label, RGQueueType queue);

    // =========================================================================
    // Phase E integration: timing report population from execution stats
    // =========================================================================

    /// @brief Populate a PassTimingReport from a compiled graph, initializing pass names and queues.
    /// Call this once after compilation, then fill in gpuTimeMs/cpuRecordMs per-pass from executor.
    void InitTimingReport(
        PassTimingReport& report, const CompiledRenderGraph& graph, const RenderGraphBuilder& builder
    );

}  // namespace miki::rg
