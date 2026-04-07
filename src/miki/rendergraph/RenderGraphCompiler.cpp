/** @file RenderGraphCompiler.cpp
 *  @brief Implementation of RenderGraphCompiler — transforms graph description into execution plan.
 *
 *  Implements stages 1-6 of the compilation pipeline:
 *    1. Condition evaluation & pass culling (+ dead code elimination)
 *    2. Dependency analysis & DAG construction
 *    3. Topological sort (modified Kahn's with barrier-aware reordering)
 *    4. Queue assignment
 *    5. Cross-queue synchronization synthesis
 *    6. Barrier synthesis (split barrier model)
 */

#include "miki/rendergraph/RenderGraphCompiler.h"

#include <algorithm>
#include <cassert>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "miki/core/Hash.h"

namespace miki::rg {

    // =========================================================================
    // Stage 1: Condition evaluation & pass culling
    // =========================================================================

    void RenderGraphCompiler::EvaluateConditions(RenderGraphBuilder& builder, std::vector<bool>& activeSet) {
        auto& passes = builder.GetPasses();
        activeSet.resize(passes.size(), true);

        // Evaluate static conditions
        for (uint32_t i = 0; i < passes.size(); ++i) {
            if (passes[i].conditionFn) {
                passes[i].enabled = passes[i].conditionFn();
                activeSet[i] = passes[i].enabled;
            }
        }

        // Dead code elimination: reverse walk from side-effect passes.
        // A pass is live if:
        //   1. It has side effects (present, readback, etc.), OR
        //   2. At least one live pass reads a resource that this pass writes.

        // Build writer map: resource index -> set of passes that write it
        std::unordered_map<uint16_t, std::vector<uint32_t>> writerMap;
        for (uint32_t i = 0; i < passes.size(); ++i) {
            if (!activeSet[i]) {
                continue;
            }
            for (auto& w : passes[i].writes) {
                writerMap[w.handle.GetIndex()].push_back(i);
            }
        }

        // Build reader-to-writer edges: for each reader, which writers does it depend on?
        // We use this to propagate liveness backwards.
        std::vector<std::vector<uint32_t>> reverseEdges(passes.size());
        for (uint32_t i = 0; i < passes.size(); ++i) {
            if (!activeSet[i]) {
                continue;
            }
            for (auto& r : passes[i].reads) {
                auto it = writerMap.find(r.handle.GetIndex());
                if (it != writerMap.end()) {
                    for (auto writerPass : it->second) {
                        if (writerPass != i) {
                            reverseEdges[i].push_back(writerPass);
                        }
                    }
                }
            }
        }

        // BFS from side-effect passes to mark reachable passes as live
        std::vector<bool> live(passes.size(), false);
        std::queue<uint32_t> worklist;

        for (uint32_t i = 0; i < passes.size(); ++i) {
            if (activeSet[i] && passes[i].hasSideEffects) {
                live[i] = true;
                worklist.push(i);
            }
        }

        while (!worklist.empty()) {
            auto current = worklist.front();
            worklist.pop();
            for (auto dep : reverseEdges[current]) {
                if (!live[dep] && activeSet[dep]) {
                    live[dep] = true;
                    worklist.push(dep);
                }
            }
        }

        // Apply DCE: cull passes that are not live
        for (uint32_t i = 0; i < passes.size(); ++i) {
            if (activeSet[i] && !live[i]) {
                activeSet[i] = false;
            }
        }
    }

    // =========================================================================
    // Stage 2: Dependency analysis & DAG construction
    // =========================================================================

    void RenderGraphCompiler::BuildDAG(
        const RenderGraphBuilder& builder, const std::vector<bool>& activeSet, std::vector<DependencyEdge>& edges
    ) {
        auto& passes = builder.GetPasses();

        // Build a map: resourceIndex -> last writer pass index
        // For SSA model, we track the latest version written per resource.
        struct WriteRecord {
            uint32_t passIndex = RGPassHandle::kInvalid;
            uint16_t version = 0;
            ResourceAccess access = ResourceAccess::None;
        };

        // resourceIndex -> latest write
        std::unordered_map<uint16_t, WriteRecord> lastWriter;
        // resourceIndex -> list of readers since last write (for WAR detection)
        std::unordered_map<uint16_t, std::vector<uint32_t>> readersAfterWrite;

        for (uint32_t i = 0; i < passes.size(); ++i) {
            if (!activeSet[i]) {
                continue;
            }

            // Process reads: create RAW edges from the last writer
            for (auto& r : passes[i].reads) {
                auto resIdx = r.handle.GetIndex();
                auto it = lastWriter.find(resIdx);
                if (it != lastWriter.end() && it->second.passIndex != i) {
                    edges.push_back(
                        DependencyEdge{
                            .srcPass = it->second.passIndex,
                            .dstPass = i,
                            .resourceIndex = resIdx,
                            .hazard = HazardType::RAW,
                        }
                    );
                }
                readersAfterWrite[resIdx].push_back(i);
            }

            // Process writes: create WAR edges from readers, WAW edges from previous writer
            for (auto& w : passes[i].writes) {
                auto resIdx = w.handle.GetIndex();

                // WAW: previous writer -> this writer
                auto writerIt = lastWriter.find(resIdx);
                if (writerIt != lastWriter.end() && writerIt->second.passIndex != i) {
                    edges.push_back(
                        DependencyEdge{
                            .srcPass = writerIt->second.passIndex,
                            .dstPass = i,
                            .resourceIndex = resIdx,
                            .hazard = HazardType::WAW,
                        }
                    );
                }

                // WAR: all readers since last write -> this writer
                auto readerIt = readersAfterWrite.find(resIdx);
                if (readerIt != readersAfterWrite.end()) {
                    for (auto readerPass : readerIt->second) {
                        if (readerPass != i) {
                            edges.push_back(
                                DependencyEdge{
                                    .srcPass = readerPass,
                                    .dstPass = i,
                                    .resourceIndex = resIdx,
                                    .hazard = HazardType::WAR,
                                }
                            );
                        }
                    }
                    readerIt->second.clear();  // Reset readers for new write epoch
                }

                // Update last writer
                lastWriter[resIdx] = WriteRecord{
                    .passIndex = i,
                    .version = w.handle.GetVersion(),
                    .access = w.access,
                };
            }
        }
    }

    // =========================================================================
    // Stage 3: Topological sort (modified Kahn's with priority heuristics)
    // =========================================================================

    auto RenderGraphCompiler::TopologicalSort(
        const RenderGraphBuilder& builder, const std::vector<DependencyEdge>& edges, const std::vector<bool>& activeSet
    ) -> core::Result<std::vector<uint32_t>> {
        auto& passes = builder.GetPasses();
        uint32_t passCount = static_cast<uint32_t>(passes.size());

        // Build adjacency and in-degree
        std::vector<std::vector<uint32_t>> adj(passCount);
        std::vector<uint32_t> inDegree(passCount, 0);

        for (auto& e : edges) {
            if (!activeSet[e.srcPass] || !activeSet[e.dstPass]) {
                continue;
            }
            adj[e.srcPass].push_back(e.dstPass);
            inDegree[e.dstPass]++;
        }

        // Priority queue: lower orderHint = higher priority.
        // Barrier-aware reordering: prefer passes that access the same resources as recently
        // scheduled passes to maximize split-barrier gaps.
        auto cmp = [&passes](uint32_t a, uint32_t b) {
            return passes[a].orderHint > passes[b].orderHint;  // min-heap on orderHint
        };
        std::priority_queue<uint32_t, std::vector<uint32_t>, decltype(cmp)> ready(cmp);

        for (uint32_t i = 0; i < passCount; ++i) {
            if (activeSet[i] && inDegree[i] == 0) {
                ready.push(i);
            }
        }

        std::vector<uint32_t> order;
        order.reserve(passCount);

        while (!ready.empty()) {
            auto current = ready.top();
            ready.pop();
            order.push_back(current);

            for (auto next : adj[current]) {
                if (--inDegree[next] == 0) {
                    ready.push(next);
                }
            }
        }

        // Check for cycles
        uint32_t expectedCount = 0;
        for (uint32_t i = 0; i < passCount; ++i) {
            if (activeSet[i]) {
                expectedCount++;
            }
        }

        if (order.size() != expectedCount) {
            return std::unexpected(core::ErrorCode::GraphCycleDetected);
        }

        return order;
    }

    // =========================================================================
    // Stage 4: Queue assignment
    // =========================================================================

    void RenderGraphCompiler::AssignQueues(
        const RenderGraphBuilder& builder, std::vector<uint32_t>& order, std::vector<RGQueueType>& queueAssignments
    ) {
        auto& passes = builder.GetPasses();
        queueAssignments.resize(passes.size(), RGQueueType::Graphics);

        for (auto passIdx : order) {
            auto& pass = passes[passIdx];
            if (!options_.enableAsyncCompute) {
                // Demote async compute to graphics queue
                queueAssignments[passIdx]
                    = (pass.queue == RGQueueType::Transfer) ? RGQueueType::Transfer : RGQueueType::Graphics;
            } else {
                queueAssignments[passIdx] = pass.queue;
            }
        }
    }

    // =========================================================================
    // Stage 5: Cross-queue synchronization synthesis
    // =========================================================================

    void RenderGraphCompiler::SynthesizeCrossQueueSync(
        const std::vector<DependencyEdge>& edges, const std::vector<RGQueueType>& queueAssignments,
        std::vector<CrossQueueSyncPoint>& syncPoints
    ) {
        // For each edge that crosses queue boundaries, emit a sync point.
        // Fan-in optimization: merge multiple sync points from the same src queue
        // converging on the same dst pass into a single sync point (max timeline value).

        struct SyncKey {
            RGQueueType srcQueue;
            uint32_t dstPass;
            auto operator==(const SyncKey&) const -> bool = default;
        };

        struct SyncKeyHash {
            auto operator()(const SyncKey& k) const -> size_t {
                return core::HashMultiple(static_cast<uint8_t>(k.srcQueue), k.dstPass);
            }
        };

        std::unordered_map<SyncKey, uint32_t, SyncKeyHash> mergedSyncs;

        for (auto& e : edges) {
            if (e.srcPass == RGPassHandle::kInvalid || e.dstPass == RGPassHandle::kInvalid) {
                continue;
            }
            if (e.srcPass >= queueAssignments.size() || e.dstPass >= queueAssignments.size()) {
                continue;
            }

            auto srcQueue = queueAssignments[e.srcPass];
            auto dstQueue = queueAssignments[e.dstPass];

            if (srcQueue != dstQueue) {
                SyncKey key{srcQueue, e.dstPass};
                auto it = mergedSyncs.find(key);
                if (it == mergedSyncs.end()) {
                    uint32_t syncIdx = static_cast<uint32_t>(syncPoints.size());
                    syncPoints.push_back(
                        CrossQueueSyncPoint{
                            .srcQueue = srcQueue,
                            .dstQueue = dstQueue,
                            .srcPassIndex = e.srcPass,
                            .dstPassIndex = e.dstPass,
                            .timelineValue = 0,  // Allocated at execution time
                        }
                    );
                    mergedSyncs[key] = syncIdx;
                } else {
                    // Fan-in: keep the latest src pass (highest index in topo order)
                    auto& existing = syncPoints[it->second];
                    if (e.srcPass > existing.srcPassIndex) {
                        existing.srcPassIndex = e.srcPass;
                    }
                }
            }
        }
    }

    // =========================================================================
    // Stage 6: Barrier synthesis (split barrier model)
    // =========================================================================

    void RenderGraphCompiler::SynthesizeBarriers(
        const RenderGraphBuilder& builder, const std::vector<uint32_t>& order,
        const std::vector<DependencyEdge>& /*edges*/, const std::vector<RGQueueType>& queueAssignments,
        std::vector<CompiledPassInfo>& compiledPasses
    ) {
        auto& passes = builder.GetPasses();
        auto& resources = builder.GetResources();

        // Build compiled pass info for each pass in topological order
        compiledPasses.clear();
        compiledPasses.reserve(order.size());

        // Map from passIndex -> position in topological order
        std::unordered_map<uint32_t, uint32_t> orderPosition;
        for (uint32_t pos = 0; pos < order.size(); ++pos) {
            orderPosition[order[pos]] = pos;
        }

        for (auto passIdx : order) {
            compiledPasses.push_back(
                CompiledPassInfo{
                    .passIndex = passIdx,
                    .queue = queueAssignments[passIdx],
                    .acquireBarriers = {},
                    .releaseBarriers = {},
                }
            );
        }

        // Track current resource state: last access, last layout, last queue
        struct ResourceState {
            ResourceAccess lastAccess = ResourceAccess::None;
            rhi::TextureLayout lastLayout = rhi::TextureLayout::Undefined;
            RGQueueType lastQueue = RGQueueType::Graphics;
            uint32_t lastPassOrderPos = std::numeric_limits<uint32_t>::max();
        };

        std::unordered_map<uint16_t, ResourceState> resourceStates;

        // Process passes in topological order
        for (uint32_t pos = 0; pos < order.size(); ++pos) {
            auto passIdx = order[pos];
            auto& pass = passes[passIdx];
            auto& compiled = compiledPasses[pos];

            auto processAccess = [&](RGResourceHandle handle, ResourceAccess access, bool isWrite) {
                auto resIdx = handle.GetIndex();
                auto& state = resourceStates[resIdx];
                bool isTexture = (resIdx < resources.size() && resources[resIdx].kind == RGResourceKind::Texture);

                auto dstBarrier = ResolveBarrier(access);
                bool needsBarrier = false;
                bool isCrossQueue = false;

                if (state.lastAccess != ResourceAccess::None) {
                    auto srcBarrier = ResolveBarrierCombined(state.lastAccess);

                    // Determine if barrier is needed:
                    // 1. Any write involved (RAW, WAW, WAR-with-layout-change)
                    // 2. Layout transition needed (texture only)
                    // 3. Cross-queue transition
                    bool srcHasWrite = IsWriteAccess(state.lastAccess);
                    bool dstHasWrite = isWrite;

                    needsBarrier = srcHasWrite || dstHasWrite;

                    if (isTexture && srcBarrier.layout != dstBarrier.layout
                        && dstBarrier.layout != rhi::TextureLayout::Undefined) {
                        needsBarrier = true;
                    }

                    isCrossQueue = (state.lastQueue != queueAssignments[passIdx]);
                    if (isCrossQueue) {
                        needsBarrier = true;
                    }

                    if (needsBarrier) {
                        BarrierCommand barrier{
                            .resourceIndex = resIdx,
                            .srcAccess = state.lastAccess,
                            .dstAccess = access,
                            .srcLayout = isTexture ? srcBarrier.layout : rhi::TextureLayout::Undefined,
                            .dstLayout = isTexture ? dstBarrier.layout : rhi::TextureLayout::Undefined,
                            .isCrossQueue = isCrossQueue,
                            .srcQueue = state.lastQueue,
                            .dstQueue = queueAssignments[passIdx],
                        };

                        if (options_.enableSplitBarriers && !isCrossQueue
                            && state.lastPassOrderPos != std::numeric_limits<uint32_t>::max()
                            && pos - state.lastPassOrderPos > 1) {
                            // Split barrier: release at previous pass, acquire at current
                            BarrierCommand release = barrier;
                            release.isSplitRelease = true;
                            release.isSplitAcquire = false;

                            BarrierCommand acquire = barrier;
                            acquire.isSplitRelease = false;
                            acquire.isSplitAcquire = true;

                            // Insert release after the last pass that touched this resource
                            compiledPasses[state.lastPassOrderPos].releaseBarriers.push_back(release);
                            compiled.acquireBarriers.push_back(acquire);
                        } else {
                            // Full barrier at current pass
                            compiled.acquireBarriers.push_back(barrier);
                        }
                    }
                }

                // Update state
                state.lastAccess = access;
                if (isTexture && dstBarrier.layout != rhi::TextureLayout::Undefined) {
                    state.lastLayout = dstBarrier.layout;
                }
                state.lastQueue = queueAssignments[passIdx];
                state.lastPassOrderPos = pos;
            };

            // Process reads first, then writes (reads see previous state)
            for (auto& r : pass.reads) {
                processAccess(r.handle, r.access, false);
            }
            for (auto& w : pass.writes) {
                processAccess(w.handle, w.access, true);
            }
        }
    }

    // =========================================================================
    // Structural hash computation
    // =========================================================================

    auto RenderGraphCompiler::ComputeStructuralHash(
        const RenderGraphBuilder& builder, const std::vector<bool>& activeSet
    ) -> CompiledRenderGraph::StructuralHash {
        auto& passes = builder.GetPasses();
        auto& resources = builder.GetResources();

        CompiledRenderGraph::StructuralHash hash{};
        hash.passCount = 0;
        hash.resourceCount = resources.size();

        uint64_t edgeSeed = core::kFnv1aOffset64;
        uint64_t condSeed = core::kFnv1aOffset64;
        uint64_t descSeed = core::kFnv1aOffset64;

        for (uint32_t i = 0; i < passes.size(); ++i) {
            if (!activeSet[i]) {
                continue;
            }
            hash.passCount++;

            auto& pass = passes[i];

            // Hash pass name and flags
            if (pass.name) {
                edgeSeed = core::HashCombine(edgeSeed, core::Fnv1a64(pass.name));
            }
            edgeSeed = core::HashCombine(edgeSeed, static_cast<uint64_t>(pass.flags));
            edgeSeed = core::HashCombine(edgeSeed, static_cast<uint64_t>(pass.queue));

            // Hash condition state
            condSeed = core::HashCombine(condSeed, pass.enabled ? 1ULL : 0ULL);

            // Hash resource accesses
            for (auto& r : pass.reads) {
                edgeSeed
                    = core::HashCombine(edgeSeed, core::HashMultiple(r.handle.packed, static_cast<uint32_t>(r.access)));
            }
            for (auto& w : pass.writes) {
                edgeSeed
                    = core::HashCombine(edgeSeed, core::HashMultiple(w.handle.packed, static_cast<uint32_t>(w.access)));
            }
        }

        // Hash resource descriptors
        for (auto& res : resources) {
            if (res.kind == RGResourceKind::Texture) {
                descSeed = core::HashCombine(
                    descSeed,
                    core::HashMultiple(
                        static_cast<uint32_t>(res.textureDesc.format), res.textureDesc.width, res.textureDesc.height,
                        res.textureDesc.depth, res.textureDesc.mipLevels, res.textureDesc.arrayLayers
                    )
                );
            } else {
                descSeed = core::HashCombine(descSeed, core::HashTrivial(res.bufferDesc.size));
            }
        }

        hash.edgeHash = core::MurmurFinalize64(edgeSeed);
        hash.conditionHash = core::MurmurFinalize64(condSeed);
        hash.descHash = core::MurmurFinalize64(descSeed);
        return hash;
    }

    // =========================================================================
    // Main compilation entry point
    // =========================================================================

    auto RenderGraphCompiler::Compile(RenderGraphBuilder& builder) -> core::Result<CompiledRenderGraph> {
        assert(builder.IsBuilt() && "Builder must be finalized via Build() before compilation");

        CompiledRenderGraph result;

        // Stage 1: Condition evaluation & DCE
        std::vector<bool> activeSet;
        EvaluateConditions(builder, activeSet);

        // Compute structural hash for caching
        result.hash = ComputeStructuralHash(builder, activeSet);

        // Stage 2: DAG construction
        BuildDAG(builder, activeSet, result.edges);

        // Stage 3: Topological sort
        auto orderResult = TopologicalSort(builder, result.edges, activeSet);
        if (!orderResult) {
            return std::unexpected(orderResult.error());
        }
        auto& order = *orderResult;

        // Stage 4: Queue assignment
        std::vector<RGQueueType> queueAssignments;
        AssignQueues(builder, order, queueAssignments);

        // Stage 5: Cross-queue sync synthesis
        SynthesizeCrossQueueSync(result.edges, queueAssignments, result.syncPoints);

        // Stage 6: Barrier synthesis
        SynthesizeBarriers(builder, order, result.edges, queueAssignments, result.passes);

        // Compute resource lifetimes for aliasing (future Stage 7)
        auto& resources = builder.GetResources();
        result.lifetimes.reserve(resources.size());
        for (uint16_t resIdx = 0; resIdx < resources.size(); ++resIdx) {
            if (resources[resIdx].imported || resources[resIdx].lifetimeExtended) {
                continue;
            }

            CompiledRenderGraph::ResourceLifetime lifetime{.resourceIndex = resIdx};
            lifetime.firstPass = std::numeric_limits<uint32_t>::max();
            lifetime.lastPass = 0;

            for (uint32_t pos = 0; pos < order.size(); ++pos) {
                auto passIdx = order[pos];
                auto& pass = builder.GetPasses()[passIdx];
                for (auto& r : pass.reads) {
                    if (r.handle.GetIndex() == resIdx) {
                        lifetime.firstPass = std::min(lifetime.firstPass, pos);
                        lifetime.lastPass = std::max(lifetime.lastPass, pos);
                    }
                }
                for (auto& w : pass.writes) {
                    if (w.handle.GetIndex() == resIdx) {
                        lifetime.firstPass = std::min(lifetime.firstPass, pos);
                        lifetime.lastPass = std::max(lifetime.lastPass, pos);
                    }
                }
            }

            if (lifetime.firstPass != std::numeric_limits<uint32_t>::max()) {
                result.lifetimes.push_back(lifetime);
            }
        }

        return result;
    }

    // =========================================================================
    // Cache hit check
    // =========================================================================

    auto RenderGraphCompiler::IsCacheHit(const CompiledRenderGraph& prev, const RenderGraphBuilder& builder) const
        -> bool {
        // Quick check: if pass or resource counts differ, definitely not a hit
        if (prev.hash.passCount != 0 || prev.hash.resourceCount != 0) {
            // Compute current hash and compare
            std::vector<bool> dummyActive(builder.GetPasses().size(), true);
            // Note: we need to evaluate conditions to get accurate hash,
            // but for a quick check we compare counts first.
            if (prev.hash.resourceCount != builder.GetResourceCount()) {
                return false;
            }
        }
        // Full structural comparison would require recomputing the hash,
        // which is done by the caller. This is a fast reject path.
        return false;  // Conservative: always recompile. Cache is opt-in.
    }

}  // namespace miki::rg
