// Copyright (c) 2024-2026 mitsuki. All rights reserved.
// SPDX-License-Identifier: MIT

#include "miki/rendergraph/RenderGraphSerializer.h"

#include "miki/core/EnumStrings.h"
#include "miki/rendergraph/RenderGraphExecutor.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <format>
#include <fstream>

namespace miki::rg {

    using json = nlohmann::ordered_json;

    // =========================================================================
    // Helpers
    // =========================================================================

    namespace {

        auto GetISOTimestamp() -> std::string {
            auto now = std::chrono::system_clock::now();
            return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(now));
        }

        auto ResourceAccessToJson(ResourceAccess access) -> json {
            json arr = json::array();
            const char* names[16]{};
            auto count = ResourceAccessToStrings(access, names, 16);
            for (size_t i = 0; i < count; ++i) {
                arr.push_back(names[i]);
            }
            return arr;
        }

        auto PassFlagsToJson(RGPassFlags flags) -> json {
            json arr = json::array();
            const char* names[8]{};
            auto count = PassFlagsToStrings(flags, names, 8);
            for (size_t i = 0; i < count; ++i) {
                arr.push_back(names[i]);
            }
            return arr;
        }

        // =====================================================================
        // Sub-object serializers (struct → json)
        // =====================================================================

        auto ToJson(const RGResourceAccess& ra) -> json {
            return {
                {"handle", ra.handle.packed},        {"resourceIndex", ra.handle.GetIndex()},
                {"version", ra.handle.GetVersion()}, {"access", ResourceAccessToJson(ra.access)},
                {"mipLevel", ra.mipLevel},           {"arrayLayer", ra.arrayLayer},
            };
        }

        auto ToJson(const RGTextureDesc& td) -> json {
            json j = {
                {"dimension", rhi::ToString(td.dimension)},
                {"format", rhi::ToString(td.format)},
                {"width", td.width},
                {"height", td.height},
                {"depth", td.depth},
                {"mipLevels", td.mipLevels},
                {"arrayLayers", td.arrayLayers},
                {"sampleCount", td.sampleCount},
            };
            if (td.debugName) {
                j["debugName"] = td.debugName;
            }
            return j;
        }

        auto ToJson(const RGBufferDesc& bd) -> json {
            json j = {{"size", bd.size}};
            if (bd.debugName) {
                j["debugName"] = bd.debugName;
            }
            return j;
        }

        auto ToJson(const RGPassNode& pass, uint32_t index) -> json {
            json reads = json::array();
            for (auto& r : pass.reads) {
                reads.push_back(ToJson(r));
            }
            json writes = json::array();
            for (auto& w : pass.writes) {
                writes.push_back(ToJson(w));
            }
            return {
                {"index", index},
                {"name", pass.name ? pass.name : ""},
                {"flags", PassFlagsToJson(pass.flags)},
                {"queue", ToString(pass.queue)},
                {"orderHint", pass.orderHint},
                {"hasSideEffects", pass.hasSideEffects},
                {"enabled", pass.enabled},
                {"reads", std::move(reads)},
                {"writes", std::move(writes)},
            };
        }

        auto ToJson(const RGResourceNode& res, uint32_t index) -> json {
            json j = {
                {"index", index},
                {"kind", ToString(res.kind)},
                {"imported", res.imported},
                {"lifetimeExtended", res.lifetimeExtended},
                {"currentVersion", res.currentVersion},
                {"name", res.name ? res.name : ""},
            };
            if (res.kind == RGResourceKind::Texture) {
                j["textureDesc"] = ToJson(res.textureDesc);
            } else {
                j["bufferDesc"] = ToJson(res.bufferDesc);
            }
            return j;
        }

        auto ToJson(const BarrierCommand& bc) -> json {
            json j = {
                {"resourceIndex", bc.resourceIndex},
                {"srcAccess", ResourceAccessToJson(bc.srcAccess)},
                {"dstAccess", ResourceAccessToJson(bc.dstAccess)},
                {"srcLayout", rhi::ToString(bc.srcLayout)},
                {"dstLayout", rhi::ToString(bc.dstLayout)},
                {"mipLevel", bc.mipLevel},
                {"arrayLayer", bc.arrayLayer},
                {"isSplitRelease", bc.isSplitRelease},
                {"isSplitAcquire", bc.isSplitAcquire},
                {"isCrossQueue", bc.isCrossQueue},
                {"isAliasingBarrier", bc.isAliasingBarrier},
            };
            if (bc.isCrossQueue) {
                j["srcQueue"] = ToString(bc.srcQueue);
                j["dstQueue"] = ToString(bc.dstQueue);
            }
            return j;
        }

        auto ToJson(const CompiledPassInfo& cp) -> json {
            json acquire = json::array();
            for (auto& b : cp.acquireBarriers) {
                acquire.push_back(ToJson(b));
            }
            json release = json::array();
            for (auto& b : cp.releaseBarriers) {
                release.push_back(ToJson(b));
            }
            return {
                {"passIndex", cp.passIndex},
                {"queue", ToString(cp.queue)},
                {"acquireBarriers", std::move(acquire)},
                {"releaseBarriers", std::move(release)},
            };
        }

        auto ToJson(const DependencyEdge& e) -> json {
            return {
                {"srcPass", e.srcPass},
                {"dstPass", e.dstPass},
                {"resourceIndex", e.resourceIndex},
                {"hazard", ToString(e.hazard)},
            };
        }

        auto ToJson(const CrossQueueSyncPoint& sp) -> json {
            return {
                {"srcQueue", ToString(sp.srcQueue)}, {"dstQueue", ToString(sp.dstQueue)},
                {"srcPassIndex", sp.srcPassIndex},   {"dstPassIndex", sp.dstPassIndex},
                {"timelineValue", sp.timelineValue},
            };
        }

        auto ToJson(const CompiledRenderGraph::ResourceLifetime& lt) -> json {
            return {
                {"resourceIndex", lt.resourceIndex},
                {"firstPass", lt.firstPass},
                {"lastPass", lt.lastPass},
            };
        }

        auto ToJson(const AliasingSlot& s) -> json {
            return {
                {"slotIndex", s.slotIndex},
                {"heapGroup", ToString(s.heapGroup)},
                {"size", s.size},
                {"alignment", s.alignment},
                {"heapOffset", s.heapOffset},
                {"lifetimeStart", s.lifetimeStart},
                {"lifetimeEnd", s.lifetimeEnd},
            };
        }

        auto ToJson(const AliasingLayout& al) -> json {
            json slots = json::array();
            for (auto& s : al.slots) {
                slots.push_back(ToJson(s));
            }
            json rts = json::array();
            for (auto v : al.resourceToSlot) {
                rts.push_back(v);
            }
            json hgs = json::array();
            for (auto sz : al.heapGroupSizes) {
                hgs.push_back(sz);
            }
            return {
                {"slots", std::move(slots)},
                {"resourceToSlot", std::move(rts)},
                {"heapGroupSizes", std::move(hgs)},
            };
        }

        auto ToJson(const SubpassDependency& sd) -> json {
            return {
                {"srcSubpass", sd.srcSubpass},
                {"dstSubpass", sd.dstSubpass},
                {"srcAccess", ResourceAccessToJson(sd.srcAccess)},
                {"dstAccess", ResourceAccessToJson(sd.dstAccess)},
                {"srcLayout", rhi::ToString(sd.srcLayout)},
                {"dstLayout", rhi::ToString(sd.dstLayout)},
                {"byRegion", sd.byRegion},
            };
        }

        auto ToJson(const MergedRenderPassGroup& mg) -> json {
            json deps = json::array();
            for (auto& d : mg.dependencies) {
                deps.push_back(ToJson(d));
            }
            return {
                {"subpassIndices", mg.subpassIndices},
                {"dependencies", std::move(deps)},
                {"sharedAttachments", std::vector<uint32_t>(mg.sharedAttachments.begin(), mg.sharedAttachments.end())},
                {"renderAreaWidth", mg.renderAreaWidth},
                {"renderAreaHeight", mg.renderAreaHeight},
            };
        }

        auto ToJson(const AdaptationPassInfo& ap) -> json {
            return {
                {"originalPassIndex", ap.originalPassIndex},
                {"insertBeforePosition", ap.insertBeforePosition},
                {"queue", ToString(ap.queue)},
                {"feature", rhi::adaptation::ToString(ap.feature)},
                {"strategy", rhi::adaptation::ToString(ap.strategy)},
                {"description", ap.description ? ap.description : ""},
            };
        }

        auto ToJson(const CommandBatch::WaitEntry& we) -> json {
            return {
                {"srcQueue", ToString(we.srcQueue)},
                {"timelineValue", we.timelineValue},
            };
        }

        auto ToJson(const CommandBatch& b) -> json {
            json waits = json::array();
            for (auto& w : b.waits) {
                waits.push_back(ToJson(w));
            }
            return {
                {"queue", ToString(b.queue)},
                {"passIndices", b.passIndices},
                {"signalTimeline", b.signalTimeline},
                {"waits", std::move(waits)},
            };
        }

        auto CompilerOptionsToJson(const RenderGraphCompiler::Options& opts) -> json {
            return {
                {"strategy", ToString(opts.strategy)},
                {"backendType", rhi::ToString(opts.backendType)},
                {"enableSplitBarriers", opts.enableSplitBarriers},
                {"enableAsyncCompute", opts.enableAsyncCompute},
                {"enableTransientAliasing", opts.enableTransientAliasing},
                {"enableRenderPassMerging", opts.enableRenderPassMerging},
                {"enableAdaptation", opts.enableAdaptation},
                {"enableBarrierReordering", opts.enableBarrierReordering},
            };
        }

        auto StructuralHashToJson(const CompiledRenderGraph::StructuralHash& h) -> json {
            return {
                {"passCount", h.passCount},
                {"resourceCount", h.resourceCount},
                {"edgeHash", std::format("0x{:016x}", h.edgeHash)},
                {"conditionHash", std::format("0x{:016x}", h.conditionHash)},
                {"descHash", std::format("0x{:016x}", h.descHash)},
            };
        }

        auto ExecutionStatsToJson(const ExecutionStats& s) -> json {
            return {
                {"transientTexturesAllocated", s.transientTexturesAllocated},
                {"transientBuffersAllocated", s.transientBuffersAllocated},
                {"transientTextureViewsCreated", s.transientTextureViewsCreated},
                {"heapsCreated", s.heapsCreated},
                {"barriersEmitted", s.barriersEmitted},
                {"batchesSubmitted", s.batchesSubmitted},
                {"passesRecorded", s.passesRecorded},
                {"secondaryCmdBufsUsed", s.secondaryCmdBufsUsed},
                {"transientMemoryBytes", s.transientMemoryBytes},
            };
        }

    }  // namespace

    // =========================================================================
    // Public API
    // =========================================================================

    auto SerializeToJSON(
        const RenderGraphBuilder& builder, const CompiledRenderGraph& compiled,
        const RenderGraphCompiler::Options& compilerOptions, const ExecutionStats* stats,
        const SerializerOptions& options
    ) -> std::string {
        auto& passes = builder.GetPasses();
        auto& resources = builder.GetResources();

        json root;
        root["version"] = 1;
        root["generator"] = "miki::rg";
        root["timestamp"] = GetISOTimestamp();
        root["frameNumber"] = options.frameNumber;

        // Passes
        root["passes"] = json::array();
        for (uint32_t i = 0; i < passes.size(); ++i) {
            root["passes"].push_back(ToJson(passes[i], i));
        }

        // Resources
        root["resources"] = json::array();
        for (uint32_t i = 0; i < resources.size(); ++i) {
            root["resources"].push_back(ToJson(resources[i], i));
        }

        // Compiler options + structural hash
        root["compilerOptions"] = CompilerOptionsToJson(compilerOptions);
        root["structuralHash"] = StructuralHashToJson(compiled.hash);

        // Edges
        root["edges"] = json::array();
        for (auto& e : compiled.edges) {
            root["edges"].push_back(ToJson(e));
        }

        // Topological order
        root["topologicalOrder"] = json::array();
        for (auto& cp : compiled.passes) {
            root["topologicalOrder"].push_back(cp.passIndex);
        }

        // Compiled passes
        root["compiledPasses"] = json::array();
        for (auto& cp : compiled.passes) {
            root["compiledPasses"].push_back(ToJson(cp));
        }

        // Sync points
        root["syncPoints"] = json::array();
        for (auto& sp : compiled.syncPoints) {
            root["syncPoints"].push_back(ToJson(sp));
        }

        // Lifetimes
        root["lifetimes"] = json::array();
        for (auto& lt : compiled.lifetimes) {
            root["lifetimes"].push_back(ToJson(lt));
        }

        // Aliasing
        root["aliasing"] = ToJson(compiled.aliasing);

        // Merged groups
        root["mergedGroups"] = json::array();
        for (auto& mg : compiled.mergedGroups) {
            root["mergedGroups"].push_back(ToJson(mg));
        }

        // Adaptation passes
        root["adaptationPasses"] = json::array();
        for (auto& ap : compiled.adaptationPasses) {
            root["adaptationPasses"].push_back(ToJson(ap));
        }

        // Batches
        root["batches"] = json::array();
        for (auto& b : compiled.batches) {
            root["batches"].push_back(ToJson(b));
        }

        // Execution stats (optional)
        if (options.includeExecutionStats && stats) {
            root["executionStats"] = ExecutionStatsToJson(*stats);
        }

        return root.dump(options.prettyPrint ? 2 : -1);
    }

    auto SerializeToFile(
        const RenderGraphBuilder& builder, const CompiledRenderGraph& compiled,
        const RenderGraphCompiler::Options& compilerOptions, const char* path, const ExecutionStats* stats,
        const SerializerOptions& options
    ) -> bool {
        auto jsonStr = SerializeToJSON(builder, compiled, compilerOptions, stats, options);
        std::ofstream ofs(path, std::ios::out | std::ios::trunc);
        if (!ofs) {
            return false;
        }
        ofs << jsonStr;
        return ofs.good();
    }

}  // namespace miki::rg
