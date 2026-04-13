/** @file RenderGraphDebug.cpp
 *  @brief Phase J: Debugging & Profiling implementation.
 */

#include "miki/rendergraph/RenderGraphDebug.h"

#include "miki/core/EnumStrings.h"
#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/backend/AllCommandBuffers.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <format>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace miki::rg {

    // =========================================================================
    // J-1: Graphviz DOT export (§12.1)
    // =========================================================================

    namespace {

        auto QueueColorHex(RGQueueType queue) -> const char* {
            switch (queue) {
                case RGQueueType::Graphics: return "#33cc33";      // Green
                case RGQueueType::AsyncCompute: return "#9933cc";  // Purple
                case RGQueueType::Transfer: return "#ff9933";      // Orange
                default: return "#33cc33";
            }
        }

        auto QueueFontColor(RGQueueType queue) -> const char* {
            switch (queue) {
                case RGQueueType::AsyncCompute: return "#ffffff";
                default: return "#000000";
            }
        }

        auto HazardEdgeStyle(HazardType h) -> const char* {
            switch (h) {
                case HazardType::RAW: return "solid";
                case HazardType::WAR: return "dashed";
                case HazardType::WAW: return "dotted";
                default: return "solid";
            }
        }

        // Generate a distinct hue for aliasing group coloring
        auto AliasingGroupColor(uint32_t groupIndex) -> std::string {
            // Golden ratio HSV hue distribution for perceptual distinctness
            constexpr float kGoldenAngle = 137.508f;
            float hue = std::fmod(static_cast<float>(groupIndex) * kGoldenAngle, 360.0f);
            // Convert HSV (hue, 0.3, 0.95) to hex RGB
            float h = hue / 60.0f;
            float c = 0.95f * 0.3f;
            float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
            float m = 0.95f - c;
            float r = 0, g = 0, b = 0;
            if (h < 1) {
                r = c;
                g = x;
            } else if (h < 2) {
                r = x;
                g = c;
            } else if (h < 3) {
                g = c;
                b = x;
            } else if (h < 4) {
                g = x;
                b = c;
            } else if (h < 5) {
                r = x;
                b = c;
            } else {
                r = c;
                b = x;
            }
            auto toHex = [](float v) { return static_cast<int>((v) * 255.0f); };
            return std::format("#{:02x}{:02x}{:02x}", toHex(r + m), toHex(g + m), toHex(b + m));
        }

    }  // namespace

    void ExportGraphviz(
        const CompiledRenderGraph& graph, const RenderGraphBuilder& builder, std::ostream& out,
        const GraphvizOptions& options
    ) {
        auto& passes = builder.GetPasses();
        auto& resources = builder.GetResources();

        // Build active pass set for quick lookup
        std::unordered_set<uint32_t> activePassIndices;
        for (auto& cp : graph.passes) {
            activePassIndices.insert(cp.passIndex);
        }

        // Build aliasing slot -> resource mapping from resourceToSlot
        std::unordered_map<uint16_t, uint32_t> resourceToAliasingSlot;
        if (options.showAliasing) {
            for (uint32_t ri = 0; ri < graph.aliasing.resourceToSlot.size(); ++ri) {
                auto slot = graph.aliasing.resourceToSlot[ri];
                if (slot != AliasingLayout::kNotAliased) {
                    resourceToAliasingSlot[static_cast<uint16_t>(ri)] = slot;
                }
            }
        }

        // Build per-pass barrier counts
        std::unordered_map<uint32_t, uint32_t> passBarrierCount;
        for (auto& cp : graph.passes) {
            passBarrierCount[cp.passIndex]
                = static_cast<uint32_t>(cp.acquireBarriers.size() + cp.releaseBarriers.size());
        }

        out << "digraph " << (options.graphTitle ? options.graphTitle : "RenderGraph") << " {\n";
        out << "  rankdir=LR;\n";
        out << "  node [shape=box, style=filled, fontname=\"Consolas\"];\n";
        out << "  edge [fontname=\"Consolas\", fontsize=9];\n\n";

        // --- Queue clusters ---
        if (options.clusterByQueue) {
            std::unordered_map<RGQueueType, std::vector<uint32_t>> queuePasses;
            for (auto& cp : graph.passes) {
                queuePasses[cp.queue].push_back(cp.passIndex);
            }
            uint32_t clusterIdx = 0;
            for (auto& [queue, passIndices] : queuePasses) {
                out << "  subgraph cluster_" << clusterIdx++ << " {\n";
                out << "    label=\"" << ToString(queue) << "\";\n";
                out << "    style=dashed; color=\"" << QueueColorHex(queue) << "\";\n";
                for (auto pi : passIndices) {
                    out << "    P" << pi << ";\n";
                }
                out << "  }\n\n";
            }
        }

        // --- Pass nodes ---
        for (uint32_t i = 0; i < passes.size(); ++i) {
            auto& pass = passes[i];
            bool active = activePassIndices.contains(i);

            if (!active && !options.showCulledPasses) {
                continue;
            }

            // Find queue from compiled pass
            RGQueueType queue = RGQueueType::Graphics;
            for (auto& cp : graph.passes) {
                if (cp.passIndex == i) {
                    queue = cp.queue;
                    break;
                }
            }

            std::string label = pass.name ? pass.name : "(unnamed)";
            if (options.showBarrierCounts && passBarrierCount.contains(i)) {
                label += std::format("\\nbarriers: {}", passBarrierCount[i]);
            }
            if (options.showTimingEstimates && pass.estimatedGpuTimeUs > 0) {
                label += std::format("\\n~{:.0f}us", pass.estimatedGpuTimeUs);
            }

            if (active) {
                out << "  P" << i << " [label=\"" << label << "\", fillcolor=\"" << QueueColorHex(queue)
                    << "\", fontcolor=\"" << QueueFontColor(queue) << "\"];\n";
            } else {
                out << "  P" << i << " [label=\"" << label << " (culled)\", fillcolor=\"#cccccc\","
                    << " style=\"filled,dashed\", fontcolor=\"#666666\"];\n";
            }
        }
        out << "\n";

        // --- Resource nodes ---
        if (options.showResourceNodes) {
            for (uint16_t i = 0; i < static_cast<uint16_t>(resources.size()); ++i) {
                auto& res = resources[i];
                std::string label = res.name ? res.name : std::format("R{}", i);
                std::string shape = "ellipse";
                std::string color = "#e0e0e0";

                if (res.imported) {
                    shape = "doubleoctagon";
                    color = options.showExternalResources ? "#ffffcc" : "#e0e0e0";
                    if (res.name
                        && (std::string_view(res.name) == "Backbuffer" || std::string_view(res.name) == "backbuffer")) {
                        color = "#ffcccc";
                    }
                } else {
                    shape = "ellipse";
                }

                if (options.showAliasing && resourceToAliasingSlot.contains(i)) {
                    color = AliasingGroupColor(resourceToAliasingSlot[i]);
                }

                if (res.kind == RGResourceKind::Buffer) {
                    label += " [buf]";
                }

                out << "  R" << i << " [label=\"" << label << "\", shape=" << shape << ", fillcolor=\"" << color
                    << "\", style=filled];\n";
            }
            out << "\n";
        }

        // --- Dependency edges ---
        for (auto& edge : graph.edges) {
            bool crossQueue = false;
            RGQueueType srcQ = RGQueueType::Graphics, dstQ = RGQueueType::Graphics;
            for (auto& cp : graph.passes) {
                if (cp.passIndex == edge.srcPass) {
                    srcQ = cp.queue;
                }
                if (cp.passIndex == edge.dstPass) {
                    dstQ = cp.queue;
                }
            }
            crossQueue = (srcQ != dstQ);

            std::string style = HazardEdgeStyle(edge.hazard);
            std::string color = crossQueue ? "red" : "black";
            std::string penwidth = crossQueue ? "2.0" : "1.0";

            std::string label = ToString(edge.hazard);
            if (edge.resourceIndex < resources.size() && resources[edge.resourceIndex].name) {
                label += std::format("\\n{}", resources[edge.resourceIndex].name);
            }

            out << "  P" << edge.srcPass << " -> P" << edge.dstPass << " [label=\"" << label << "\", style=" << style
                << ", color=" << color << ", penwidth=" << penwidth;
            if (crossQueue && options.showCrossQueueSync) {
                out << ", arrowhead=normal, arrowtail=dot, dir=both";
            }
            out << "];\n";
        }
        out << "\n";

        // --- Resource edges (pass -> resource, resource -> pass) ---
        if (options.showResourceNodes) {
            for (auto& cp : graph.passes) {
                auto& pass = passes[cp.passIndex];
                for (auto& acc : pass.reads) {
                    out << "  R" << acc.handle.GetIndex() << " -> P" << cp.passIndex
                        << " [color=\"#0066cc\", style=dashed, arrowsize=0.6];\n";
                }
                for (auto& acc : pass.writes) {
                    out << "  P" << cp.passIndex << " -> R" << acc.handle.GetIndex()
                        << " [color=\"#cc0000\", arrowsize=0.6];\n";
                }
            }
        }

        // --- Cross-queue sync points ---
        if (options.showCrossQueueSync) {
            for (auto& sp : graph.syncPoints) {
                out << "  // Sync: " << ToString(sp.srcQueue) << " -> " << ToString(sp.dstQueue)
                    << " timeline=" << sp.timelineValue << "\n";
            }
        }

        // --- History resources ---
        if (options.showHistoryResources) {
            for (auto& hr : graph.historyResources) {
                if (hr.resourceIndex < resources.size()) {
                    out << "  R" << hr.resourceIndex << " [peripheries=2];\n";
                }
            }
        }

        out << "}\n";
    }

    auto ExportGraphvizString(
        const CompiledRenderGraph& graph, const RenderGraphBuilder& builder, const GraphvizOptions& options
    ) -> std::string {
        std::ostringstream ss;
        ExportGraphviz(graph, builder, ss, options);
        return ss.str();
    }

    // =========================================================================
    // J-1b: Mermaid flowchart export
    // =========================================================================

    namespace {

        auto QueueMermaidStyle(RGQueueType queue) -> const char* {
            switch (queue) {
                case RGQueueType::Graphics: return "fill:#33cc33,color:#000";
                case RGQueueType::AsyncCompute: return "fill:#9933cc,color:#fff";
                case RGQueueType::Transfer: return "fill:#ff9933,color:#000";
                default: return "fill:#33cc33,color:#000";
            }
        }

    }  // anonymous namespace

    void ExportMermaid(
        const CompiledRenderGraph& graph, const RenderGraphBuilder& builder, std::ostream& out,
        const MermaidOptions& options
    ) {
        auto& passes = builder.GetPasses();
        auto& resources = builder.GetResources();

        out << "flowchart " << options.direction << "\n";

        // --- Batch subgraphs & pass nodes ---
        if (options.clusterByBatch && !graph.batches.empty()) {
            for (size_t bi = 0; bi < graph.batches.size(); ++bi) {
                auto& batch = graph.batches[bi];
                out << "  subgraph Batch" << bi << "[\"Batch " << bi << " — " << ToString(batch.queue) << "\"]\n";
                out << "    direction " << options.direction << "\n";
                for (auto pi : batch.passIndices) {
                    auto& cp = graph.passes[pi];
                    auto& pass = passes[cp.passIndex];
                    auto name = pass.name ? pass.name : "(null)";
                    out << "    P" << cp.passIndex << "[\"" << name;
                    if (options.showBarrierCounts) {
                        out << "\\nacq=" << cp.acquireBarriers.size() << " rel=" << cp.releaseBarriers.size();
                    }
                    out << "\"]\n";
                    out << "    style P" << cp.passIndex << " " << QueueMermaidStyle(cp.queue) << "\n";
                }
                out << "  end\n";
            }
        } else {
            for (auto& cp : graph.passes) {
                auto& pass = passes[cp.passIndex];
                auto name = pass.name ? pass.name : "(null)";
                out << "  P" << cp.passIndex << "[\"" << name;
                if (options.showBarrierCounts) {
                    out << "\\nacq=" << cp.acquireBarriers.size() << " rel=" << cp.releaseBarriers.size();
                }
                out << "\"]\n";
                out << "  style P" << cp.passIndex << " " << QueueMermaidStyle(cp.queue) << "\n";
            }
        }

        // --- Dependency edges ---
        for (auto& edge : graph.edges) {
            auto label = ToString(edge.hazard);
            bool crossQueue = false;
            if (edge.srcPass < graph.passes.size() && edge.dstPass < graph.passes.size()) {
                crossQueue = (graph.passes[edge.srcPass].queue != graph.passes[edge.dstPass].queue);
            }
            auto srcIdx = (edge.srcPass < graph.passes.size()) ? graph.passes[edge.srcPass].passIndex : edge.srcPass;
            auto dstIdx = (edge.dstPass < graph.passes.size()) ? graph.passes[edge.dstPass].passIndex : edge.dstPass;
            if (crossQueue) {
                out << "  P" << srcIdx << " ==>|\"" << label << " xQ\"| P" << dstIdx << "\n";
            } else {
                out << "  P" << srcIdx << " -->|\"" << label << "\"| P" << dstIdx << "\n";
            }
        }

        // --- Cross-queue sync points ---
        if (options.showCrossQueueSync) {
            for (auto& sp : graph.syncPoints) {
                auto srcIdx = (sp.srcPassIndex < graph.passes.size())
                                  ? graph.passes[sp.srcPassIndex].passIndex
                                  : sp.srcPassIndex;
                auto dstIdx = (sp.dstPassIndex < graph.passes.size())
                                  ? graph.passes[sp.dstPassIndex].passIndex
                                  : sp.dstPassIndex;
                out << "  P" << srcIdx << " -.->|\"timeline=" << sp.timelineValue << "\"| P" << dstIdx << "\n";
            }
        }

        // --- Resource nodes ---
        if (options.showResourceEdges) {
            for (auto& cp : graph.passes) {
                auto& pass = passes[cp.passIndex];
                for (auto& r : pass.reads) {
                    auto ri = r.handle.GetIndex();
                    if (ri < resources.size()) {
                        auto& res = resources[ri];
                        out << "  R" << ri << "([\"" << (res.name ? res.name : "?") << "\"])\n";
                        out << "  R" << ri << " -.-> P" << cp.passIndex << "\n";
                    }
                }
                for (auto& w : pass.writes) {
                    auto ri = w.handle.GetIndex();
                    if (ri < resources.size()) {
                        auto& res = resources[ri];
                        out << "  R" << ri << "([\"" << (res.name ? res.name : "?") << "\"])\n";
                        out << "  P" << cp.passIndex << " -.-> R" << ri << "\n";
                    }
                }
            }
        }

        // --- Aliasing annotations ---
        if (options.showAliasing) {
            for (size_t si = 0; si < graph.aliasing.resourceToSlot.size(); ++si) {
                auto slotIdx = graph.aliasing.resourceToSlot[si];
                if (slotIdx != AliasingLayout::kNotAliased && slotIdx < graph.aliasing.slots.size()) {
                    auto& slot = graph.aliasing.slots[slotIdx];
                    auto rName = (si < resources.size() && resources[si].name) ? resources[si].name : "?";
                    out << "  %% R" << si << " (" << rName << ") -> aliasing slot " << slotIdx
                        << " heap=" << ToString(slot.heapGroup) << " size=" << slot.size << "\n";
                }
            }
        }
    }

    auto ExportMermaidString(
        const CompiledRenderGraph& graph, const RenderGraphBuilder& builder, const MermaidOptions& options
    ) -> std::string {
        std::ostringstream ss;
        ExportMermaid(graph, builder, ss, options);
        return ss.str();
    }

    // =========================================================================
    // J-2: PassTimingReport (§12.2)
    // =========================================================================

    void PassTimingReport::PopulateBarrierCounts(const CompiledRenderGraph& graph) {
        passes.resize(graph.passes.size());
        for (size_t i = 0; i < graph.passes.size(); ++i) {
            auto& cp = graph.passes[i];
            passes[i].barrierCount = static_cast<uint32_t>(cp.acquireBarriers.size() + cp.releaseBarriers.size());
        }
    }

    void PassTimingReport::ComputeTotals() {
        totalFrameGpuMs = 0.0f;
        totalFrameCpuMs = 0.0f;
        for (auto& p : passes) {
            totalFrameGpuMs += p.gpuTimeMs;
            totalFrameCpuMs += p.cpuRecordMs;
        }
    }

    auto PassTimingReport::FormatTable() const -> std::string {
        std::string result;
        result += std::format(
            "{:<30} {:>8} {:>8} {:>8} {:>6} {:>6} {:>8}\n", "Pass", "Queue", "GPU(ms)", "CPU(ms)", "Barr", "Draw",
            "Disp"
        );
        result += std::string(86, '-') + "\n";

        for (auto& p : passes) {
            result += std::format(
                "{:<30} {:>8} {:>8.3f} {:>8.3f} {:>6} {:>6} {:>8}\n", p.name ? p.name : "(null)", ToString(p.queue),
                p.gpuTimeMs, p.cpuRecordMs, p.barrierCount, p.drawCallCount, p.dispatchCount
            );
        }

        result += std::string(86, '-') + "\n";
        result += std::format(
            "Total GPU: {:.3f}ms  CPU: {:.3f}ms  Async overlap: {:.3f}ms  "
            "Aliasing saved: {:.2f}MB\n",
            totalFrameGpuMs, totalFrameCpuMs, asyncComputeOverlapMs, aliasingMemorySavedMB
        );
        return result;
    }

    // =========================================================================
    // J-3: Barrier audit log (§12.3)
    // =========================================================================

    void BarrierAuditLog::PopulateFromCompiled(const CompiledRenderGraph& graph, const RenderGraphBuilder& builder) {
        entries.clear();
        auto& resources = builder.GetResources();
        auto& passes = builder.GetPasses();

        for (auto& cp : graph.passes) {
            const char* passName = (cp.passIndex < passes.size()) ? passes[cp.passIndex].name : nullptr;

            auto addBarrier = [&](const BarrierCommand& bc) {
                const char* resName
                    = (bc.resourceIndex < resources.size()) ? resources[bc.resourceIndex].name : nullptr;
                entries.push_back({
                    .passIndex = cp.passIndex,
                    .passName = passName,
                    .resourceName = resName,
                    .resourceIndex = bc.resourceIndex,
                    .srcAccess = bc.srcAccess,
                    .dstAccess = bc.dstAccess,
                    .srcLayout = bc.srcLayout,
                    .dstLayout = bc.dstLayout,
                    .isSplitRelease = bc.isSplitRelease,
                    .isSplitAcquire = bc.isSplitAcquire,
                    .isCrossQueue = bc.isCrossQueue,
                    .isAliasingBarrier = bc.isAliasingBarrier,
                    .isFenceBarrier = bc.isFenceBarrier,
                    .srcQueue = bc.srcQueue,
                    .dstQueue = bc.dstQueue,
                });
            };

            for (auto& b : cp.acquireBarriers) {
                addBarrier(b);
            }
            for (auto& b : cp.releaseBarriers) {
                addBarrier(b);
            }
        }
    }

    auto BarrierAuditLog::CountCrossQueue() const -> uint32_t {
        uint32_t count = 0;
        for (auto& e : entries) {
            if (e.isCrossQueue) {
                count++;
            }
        }
        return count;
    }

    auto BarrierAuditLog::CountSplitBarriers() const -> uint32_t {
        uint32_t count = 0;
        for (auto& e : entries) {
            if (e.isSplitRelease || e.isSplitAcquire) {
                count++;
            }
        }
        return count;
    }

    auto BarrierAuditLog::CountAliasingBarriers() const -> uint32_t {
        uint32_t count = 0;
        for (auto& e : entries) {
            if (e.isAliasingBarrier) {
                count++;
            }
        }
        return count;
    }

    auto BarrierAuditLog::FormatSummary() const -> std::string {
        return std::format(
            "Barriers: {} total, {} cross-queue, {} split, {} aliasing (frame {})", CountTotal(), CountCrossQueue(),
            CountSplitBarriers(), CountAliasingBarriers(), frameIndex
        );
    }

    auto BarrierAuditLog::ToJson() const -> nlohmann::json {
        using json = nlohmann::json;
        json arr = json::array();
        for (auto& e : entries) {
            arr.push_back({
                {"passIndex", e.passIndex},
                {"pass", e.passName ? e.passName : ""},
                {"resource", e.resourceName ? e.resourceName : ""},
                {"resourceIndex", e.resourceIndex},
                {"srcAccess", static_cast<uint32_t>(e.srcAccess)},
                {"dstAccess", static_cast<uint32_t>(e.dstAccess)},
                {"srcLayout", ToString(e.srcLayout)},
                {"dstLayout", ToString(e.dstLayout)},
                {"splitRelease", e.isSplitRelease},
                {"splitAcquire", e.isSplitAcquire},
                {"crossQueue", e.isCrossQueue},
                {"aliasing", e.isAliasingBarrier},
                {"fence", e.isFenceBarrier},
                {"srcQueue", ToString(e.srcQueue)},
                {"dstQueue", ToString(e.dstQueue)},
            });
        }
        return {
            {"frameIndex", frameIndex},
            {"totalCount", CountTotal()},
            {"crossQueueCount", CountCrossQueue()},
            {"splitCount", CountSplitBarriers()},
            {"aliasingCount", CountAliasingBarriers()},
            {"entries", arr},
        };
    }

    auto BarrierAuditLog::ToJsonString(bool pretty) const -> std::string {
        return ToJson().dump(pretty ? 2 : -1);
    }

    // =========================================================================
    // J-5: Graph diff report (§12.5)
    // =========================================================================

    auto GraphDiffReport::Generate(
        const RenderGraphBuilder& oldBuilder, const std::vector<bool>& oldActiveSet,
        const RenderGraphBuilder& newBuilder, const std::vector<bool>& newActiveSet, uint64_t frame,
        float recompileTimeUs, bool incremental
    ) -> GraphDiffReport {
        GraphDiffReport report;
        report.frameIndex = frame;
        report.recompilationTimeUs = recompileTimeUs;
        report.wasIncrementalRecompile = incremental;

        auto& oldPasses = oldBuilder.GetPasses();
        auto& newPasses = newBuilder.GetPasses();
        auto& oldResources = oldBuilder.GetResources();
        auto& newResources = newBuilder.GetResources();

        // Build name -> index maps for old passes
        std::unordered_map<std::string_view, uint32_t> oldPassMap;
        for (uint32_t i = 0; i < oldPasses.size(); ++i) {
            if (oldPasses[i].name && (i < oldActiveSet.size() && oldActiveSet[i])) {
                oldPassMap[oldPasses[i].name] = i;
            }
        }

        // Detect added and modified passes
        for (uint32_t i = 0; i < newPasses.size(); ++i) {
            bool newActive = (i < newActiveSet.size()) ? newActiveSet[i] : true;
            if (!newPasses[i].name) {
                continue;
            }

            auto it = oldPassMap.find(newPasses[i].name);
            if (it == oldPassMap.end()) {
                if (newActive) {
                    report.passDiffs.push_back({
                        .action = DiffAction::Added,
                        .passName = newPasses[i].name,
                        .reason = "new pass or condition became true",
                    });
                }
            } else {
                bool oldActive = (it->second < oldActiveSet.size()) ? oldActiveSet[it->second] : true;
                if (!oldActive && newActive) {
                    report.passDiffs.push_back({
                        .action = DiffAction::Added,
                        .passName = newPasses[i].name,
                        .reason = "condition became true",
                    });
                } else if (oldActive && !newActive) {
                    report.passDiffs.push_back({
                        .action = DiffAction::Removed,
                        .passName = newPasses[i].name,
                        .reason = "condition became false",
                    });
                }
                oldPassMap.erase(it);
            }
        }

        // Remaining old passes were removed
        for (auto& [name, idx] : oldPassMap) {
            report.passDiffs.push_back({
                .action = DiffAction::Removed,
                .passName = oldPasses[idx].name,
                .reason = "pass removed from graph",
            });
        }

        // Resource diffs — compare by name
        std::unordered_map<std::string_view, uint32_t> oldResMap;
        for (uint32_t i = 0; i < oldResources.size(); ++i) {
            if (oldResources[i].name) {
                oldResMap[oldResources[i].name] = i;
            }
        }

        for (uint32_t i = 0; i < newResources.size(); ++i) {
            if (!newResources[i].name) {
                continue;
            }
            auto it = oldResMap.find(newResources[i].name);
            if (it == oldResMap.end()) {
                report.resourceDiffs.push_back({
                    .action = DiffAction::Added,
                    .resourceName = newResources[i].name,
                    .detail = "new resource",
                });
            } else {
                // Check for descriptor changes (texture size, buffer size, format)
                auto& oldRes = oldResources[it->second];
                auto& newRes = newResources[i];
                if (oldRes.kind == RGResourceKind::Texture && newRes.kind == RGResourceKind::Texture) {
                    if (oldRes.textureDesc.width != newRes.textureDesc.width
                        || oldRes.textureDesc.height != newRes.textureDesc.height
                        || oldRes.textureDesc.format != newRes.textureDesc.format) {
                        report.resourceDiffs.push_back({
                            .action = DiffAction::Modified,
                            .resourceName = newRes.name,
                            .detail = std::format(
                                "{}x{} -> {}x{}", oldRes.textureDesc.width, oldRes.textureDesc.height,
                                newRes.textureDesc.width, newRes.textureDesc.height
                            ),
                        });
                    }
                } else if (oldRes.kind == RGResourceKind::Buffer && newRes.kind == RGResourceKind::Buffer) {
                    if (oldRes.bufferDesc.size != newRes.bufferDesc.size) {
                        report.resourceDiffs.push_back({
                            .action = DiffAction::Modified,
                            .resourceName = newRes.name,
                            .detail = std::format("size {} -> {}", oldRes.bufferDesc.size, newRes.bufferDesc.size),
                        });
                    }
                }
                oldResMap.erase(it);
            }
        }

        for (auto& [name, idx] : oldResMap) {
            report.resourceDiffs.push_back({
                .action = DiffAction::Removed,
                .resourceName = oldResources[idx].name,
                .detail = "resource removed",
            });
        }

        return report;
    }

    auto GraphDiffReport::ToJson() const -> nlohmann::json {
        using json = nlohmann::json;
        auto actionStr = [](DiffAction a) -> const char* {
            switch (a) {
                case DiffAction::Added: return "added";
                case DiffAction::Removed: return "removed";
                case DiffAction::Modified: return "modified";
                default: return "unknown";
            }
        };

        json passDiffsArr = json::array();
        for (auto& d : passDiffs) {
            passDiffsArr.push_back({
                {"action", actionStr(d.action)},
                {"pass", d.passName ? d.passName : ""},
                {"reason", d.reason},
            });
        }

        json resDiffsArr = json::array();
        for (auto& d : resourceDiffs) {
            resDiffsArr.push_back({
                {"action", actionStr(d.action)},
                {"resource", d.resourceName ? d.resourceName : ""},
                {"detail", d.detail},
            });
        }

        return {
            {"frameIndex", frameIndex},
            {"prevTopologyHash", std::format("0x{:016x}", prevTopologyHash)},
            {"newTopologyHash", std::format("0x{:016x}", newTopologyHash)},
            {"prevDescHash", std::format("0x{:016x}", prevDescHash)},
            {"newDescHash", std::format("0x{:016x}", newDescHash)},
            {"recompilationTimeUs", recompilationTimeUs},
            {"wasIncrementalRecompile", wasIncrementalRecompile},
            {"passDiffs", passDiffsArr},
            {"resourceDiffs", resDiffsArr},
        };
    }

    auto GraphDiffReport::ToJsonString(bool pretty) const -> std::string {
        return ToJson().dump(pretty ? 2 : -1);
    }

    auto GraphDiffReport::Summary() const -> std::string {
        std::string s = std::format("[RenderGraph] Structure changed at frame {}:\n", frameIndex);
        for (auto& d : passDiffs) {
            char prefix = ' ';
            switch (d.action) {
                case DiffAction::Added: prefix = '+'; break;
                case DiffAction::Removed: prefix = '-'; break;
                case DiffAction::Modified: prefix = '~'; break;
            }
            s += std::format("  {} {}: {}\n", prefix, d.passName ? d.passName : "(null)", d.reason);
        }
        for (auto& d : resourceDiffs) {
            char prefix = ' ';
            switch (d.action) {
                case DiffAction::Added: prefix = '+'; break;
                case DiffAction::Removed: prefix = '-'; break;
                case DiffAction::Modified: prefix = '~'; break;
            }
            s += std::format(
                "  {} resource \"{}\": {}\n", prefix, d.resourceName ? d.resourceName : "(null)", d.detail
            );
        }
        s += std::format(
            "  Recompilation: {} ({:.0f}us)\n", wasIncrementalRecompile ? "incremental" : "full", recompilationTimeUs
        );
        return s;
    }

    // =========================================================================
    // J-6: RenderGraphValidator (§11.2)
    // =========================================================================

    auto RenderGraphValidator::CheckUninitializedReads(
        const CompiledRenderGraph& graph, const RenderGraphBuilder& builder
    ) -> std::vector<ValidationWarning> {
        std::vector<ValidationWarning> warnings;
        auto& passes = builder.GetPasses();
        auto& resources = builder.GetResources();

        // Track which resources have been written in topological order
        std::unordered_set<uint16_t> writtenResources;

        // Mark imported resources as already "written" (they come from outside)
        for (uint16_t i = 0; i < static_cast<uint16_t>(resources.size()); ++i) {
            if (resources[i].imported) {
                writtenResources.insert(i);
            }
        }

        for (auto& cp : graph.passes) {
            if (cp.passIndex >= passes.size()) {
                continue;
            }
            auto& pass = passes[cp.passIndex];

            // Check reads: every read resource should have been written
            for (auto& acc : pass.reads) {
                auto ri = acc.handle.GetIndex();
                if (!writtenResources.contains(ri) && ri < resources.size() && !resources[ri].imported) {
                    warnings.push_back({
                        .category = ValidationWarning::Category::UninitializedRead,
                        .passIndex = cp.passIndex,
                        .passName = pass.name,
                        .resourceIndex = ri,
                        .resourceName = (ri < resources.size()) ? resources[ri].name : nullptr,
                        .detail = "resource read before any write in current frame",
                    });
                }
            }

            // Record writes
            for (auto& acc : pass.writes) {
                writtenResources.insert(acc.handle.GetIndex());
            }
        }

        return warnings;
    }

    auto RenderGraphValidator::ValidateAliasing(const CompiledRenderGraph& graph) -> std::vector<ValidationWarning> {
        std::vector<ValidationWarning> warnings;

        // Group resources by slot
        std::unordered_map<uint32_t, std::vector<uint16_t>> slotResources;
        for (uint32_t ri = 0; ri < graph.aliasing.resourceToSlot.size(); ++ri) {
            auto slot = graph.aliasing.resourceToSlot[ri];
            if (slot != AliasingLayout::kNotAliased) {
                slotResources[slot].push_back(static_cast<uint16_t>(ri));
            }
        }

        // Build resource -> lifetime lookup
        std::unordered_map<uint16_t, std::pair<uint32_t, uint32_t>> lifetimeMap;
        for (auto& lt : graph.lifetimes) {
            lifetimeMap[lt.resourceIndex] = {lt.firstPass, lt.lastPass};
        }

        for (auto& [slotIdx, resIndices] : slotResources) {
            for (size_t i = 0; i < resIndices.size(); ++i) {
                for (size_t j = i + 1; j < resIndices.size(); ++j) {
                    auto aIt = lifetimeMap.find(resIndices[i]);
                    auto bIt = lifetimeMap.find(resIndices[j]);
                    if (aIt == lifetimeMap.end() || bIt == lifetimeMap.end()) {
                        continue;
                    }
                    auto [aFirst, aLast] = aIt->second;
                    auto [bFirst, bLast] = bIt->second;
                    if (aFirst <= bLast && bFirst <= aLast) {
                        warnings.push_back({
                            .category = ValidationWarning::Category::AliasingOverlap,
                            .resourceIndex = resIndices[i],
                            .detail = std::format(
                                "resources {} and {} overlap in aliasing slot {}: [{},{}] vs [{},{}]", resIndices[i],
                                resIndices[j], slotIdx, aFirst, aLast, bFirst, bLast
                            ),
                        });
                    }
                }
            }
        }

        return warnings;
    }

    auto RenderGraphValidator::AuditBarriers(
        const BarrierAuditLog& emitted, const CompiledRenderGraph& graph, const RenderGraphBuilder& builder
    ) -> std::vector<ValidationWarning> {
        std::vector<ValidationWarning> warnings;
        auto& resources = builder.GetResources();

        // Build set of emitted barriers keyed by (passIndex, resourceIndex, srcAccess, dstAccess)
        struct BarrierKey {
            uint32_t passIndex;
            uint16_t resourceIndex;
            auto operator==(const BarrierKey&) const -> bool = default;
        };
        struct BarrierKeyHash {
            auto operator()(const BarrierKey& k) const -> size_t {
                return std::hash<uint64_t>{}(static_cast<uint64_t>(k.passIndex) << 16 | k.resourceIndex);
            }
        };

        std::unordered_set<BarrierKey, BarrierKeyHash> emittedSet;
        for (auto& e : emitted.entries) {
            emittedSet.insert({e.passIndex, e.resourceIndex});
        }

        // For each edge in the graph, there should be at least one barrier
        for (auto& edge : graph.edges) {
            bool found = emittedSet.contains({edge.dstPass, edge.resourceIndex})
                         || emittedSet.contains({edge.srcPass, edge.resourceIndex});
            if (!found && edge.hazard != HazardType::WAR) {
                // WAR edges may only need execution barrier (no memory barrier)
                // but RAW and WAW definitely need barriers
                const char* resName
                    = (edge.resourceIndex < resources.size()) ? resources[edge.resourceIndex].name : nullptr;
                warnings.push_back({
                    .category = ValidationWarning::Category::MissingBarrier,
                    .passIndex = edge.dstPass,
                    .resourceIndex = edge.resourceIndex,
                    .resourceName = resName,
                    .detail = std::format(
                        "missing barrier for {} edge P{} -> P{} on resource {}", ToString(edge.hazard), edge.srcPass,
                        edge.dstPass, resName ? resName : std::to_string(edge.resourceIndex)
                    ),
                });
            }
        }

        return warnings;
    }

    auto RenderGraphValidator::ValidateCrossQueueSync(
        const CompiledRenderGraph& graph, const RenderGraphBuilder& builder
    ) -> std::vector<ValidationWarning> {
        std::vector<ValidationWarning> warnings;
        auto& passes = builder.GetPasses();

        // Build pass -> queue mapping from compiled passes
        std::unordered_map<uint32_t, RGQueueType> passQueues;
        for (auto& cp : graph.passes) {
            passQueues[cp.passIndex] = cp.queue;
        }

        // Build set of synced queue pairs from sync points
        auto packQueuePair = [](RGQueueType src, RGQueueType dst) -> uint64_t {
            return (static_cast<uint64_t>(src) << 32) | static_cast<uint64_t>(dst);
        };
        std::unordered_set<uint64_t> syncedQueuePairs;
        for (auto& sp : graph.syncPoints) {
            syncedQueuePairs.insert(packQueuePair(sp.srcQueue, sp.dstQueue));
        }

        // Check every cross-queue edge has corresponding sync
        for (auto& edge : graph.edges) {
            auto srcIt = passQueues.find(edge.srcPass);
            auto dstIt = passQueues.find(edge.dstPass);
            if (srcIt == passQueues.end() || dstIt == passQueues.end()) {
                continue;
            }
            if (srcIt->second == dstIt->second) {
                continue;
            }

            // Cross-queue edge found — verify there's a sync point for this queue pair
            bool hasSyncPoint = syncedQueuePairs.contains(packQueuePair(srcIt->second, dstIt->second));
            if (!hasSyncPoint) {
                const char* passName = (edge.dstPass < passes.size()) ? passes[edge.dstPass].name : nullptr;
                warnings.push_back({
                    .category = ValidationWarning::Category::CrossQueueRace,
                    .passIndex = edge.dstPass,
                    .passName = passName,
                    .resourceIndex = edge.resourceIndex,
                    .detail = std::format(
                        "cross-queue edge P{} ({}) -> P{} ({}) without sync point", edge.srcPass,
                        ToString(srcIt->second), edge.dstPass, ToString(dstIt->second)
                    ),
                });
            }
        }

        return warnings;
    }

    auto RenderGraphValidator::ValidateAll(
        const CompiledRenderGraph& graph, const RenderGraphBuilder& builder, const BarrierAuditLog* auditLog
    ) -> ValidationReport {
        ValidationReport report;

        auto uninitWarnings = CheckUninitializedReads(graph, builder);
        auto aliasingWarnings = ValidateAliasing(graph);
        auto crossQueueWarnings = ValidateCrossQueueSync(graph, builder);

        report.warnings.insert(report.warnings.end(), uninitWarnings.begin(), uninitWarnings.end());
        report.warnings.insert(report.warnings.end(), aliasingWarnings.begin(), aliasingWarnings.end());
        report.warnings.insert(report.warnings.end(), crossQueueWarnings.begin(), crossQueueWarnings.end());

        if (auditLog) {
            auto barrierWarnings = AuditBarriers(*auditLog, graph, builder);
            report.warnings.insert(report.warnings.end(), barrierWarnings.begin(), barrierWarnings.end());
        }

        // Categorize
        for (auto& w : report.warnings) {
            // Fatal categories: UninitializedRead, AliasingOverlap, MissingBarrier, CrossQueueRace
            // Non-fatal: RedundantBarrier, StaleHistory, TimelineViolation
            switch (w.category) {
                case ValidationWarning::Category::UninitializedRead:
                case ValidationWarning::Category::AliasingOverlap:
                case ValidationWarning::Category::MissingBarrier:
                case ValidationWarning::Category::CrossQueueRace:
                case ValidationWarning::Category::TimelineViolation: report.errorCount++; break;
                case ValidationWarning::Category::RedundantBarrier:
                case ValidationWarning::Category::StaleHistory: report.warningCount++; break;
            }
        }

        return report;
    }

    auto ValidationReport::FormatReport() const -> std::string {
        auto catStr = [](ValidationWarning::Category c) -> const char* {
            switch (c) {
                case ValidationWarning::Category::UninitializedRead: return "UNINIT_READ";
                case ValidationWarning::Category::AliasingOverlap: return "ALIAS_OVERLAP";
                case ValidationWarning::Category::MissingBarrier: return "MISSING_BARRIER";
                case ValidationWarning::Category::RedundantBarrier: return "REDUNDANT_BARRIER";
                case ValidationWarning::Category::TimelineViolation: return "TIMELINE_VIOLATION";
                case ValidationWarning::Category::CrossQueueRace: return "CROSS_QUEUE_RACE";
                case ValidationWarning::Category::StaleHistory: return "STALE_HISTORY";
                default: return "UNKNOWN";
            }
        };

        std::string result;
        result += std::format("RenderGraphValidator: {} errors, {} warnings\n", errorCount, warningCount);
        for (auto& w : warnings) {
            result += std::format(
                "  [{}] pass={} resource={}: {}\n", catStr(w.category), w.passName ? w.passName : "(null)",
                w.resourceName ? w.resourceName : "(null)", w.detail
            );
        }
        return result;
    }

    // =========================================================================
    // Phase E integration: per-pass debug regions (§12.4)
    // =========================================================================

    void BeginPassDebugRegion(rhi::CommandListHandle& cmd, const char* passName, RGQueueType queue) {
        cmd.Dispatch([&](auto& c) { c.CmdBeginDebugLabel(passName, GetPassDebugColor(queue)); });
    }

    void EndPassDebugRegion(rhi::CommandListHandle& cmd) {
        cmd.Dispatch([&](auto& c) { c.CmdEndDebugLabel(); });
    }

    void InsertPassDebugMarker(rhi::CommandListHandle& cmd, const char* label, RGQueueType queue) {
        cmd.Dispatch([&](auto& c) { c.CmdInsertDebugLabel(label, GetPassDebugColor(queue)); });
    }

    // =========================================================================
    // Phase E integration: InitTimingReport
    // =========================================================================

    void InitTimingReport(
        PassTimingReport& report, const CompiledRenderGraph& graph, const RenderGraphBuilder& builder
    ) {
        auto& passes = builder.GetPasses();
        report.passes.resize(graph.passes.size());
        for (size_t i = 0; i < graph.passes.size(); ++i) {
            auto& cp = graph.passes[i];
            auto& entry = report.passes[i];
            entry.name = (cp.passIndex < passes.size()) ? passes[cp.passIndex].name : nullptr;
            entry.queue = cp.queue;
            entry.barrierCount = static_cast<uint32_t>(cp.acquireBarriers.size() + cp.releaseBarriers.size());
            entry.gpuTimeMs = 0.0f;
            entry.cpuRecordMs = 0.0f;
            entry.drawCallCount = 0;
            entry.dispatchCount = 0;
        }
    }

}  // namespace miki::rg
