/** @file RenderGraphCompiler.cpp
 *  @brief Implementation of RenderGraphCompiler — transforms graph description into execution plan.
 *
 *  Implements stages 1-10 of the compilation pipeline:
 *    1. Condition evaluation & pass culling (+ dead code elimination)
 *    2. Dependency analysis & DAG construction
 *    3. Topological sort (modified Kahn's with barrier-aware reordering)
 *    4. Queue assignment
 *    5. Cross-queue synchronization synthesis
 *    6. Barrier synthesis (split barrier model)
 *    7. Transient resource aliasing (memory scheduling)
 *    8. Render pass merging (subpass consolidation)
 *    9. Backend adaptation pass injection
 *   10. Command batch formation
 */

#include "miki/rendergraph/RenderGraphCompiler.h"

#include "miki/rendergraph/AsyncComputeScheduler.h"
#include "miki/rendergraph/CompilerUtils.h"

#include <algorithm>
#include <cassert>
#include <future>
#include <queue>
#include <unordered_map>
#include <unordered_set>

#include "miki/core/Hash.h"
#include "miki/rhi/adaptation/AdaptationQuery.h"

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
            } else if (pass.queue == RGQueueType::AsyncCompute && options_.asyncScheduler != nullptr) {
                // Adaptive scheduling: consult the EMA-based scheduler (§7.2.1)
                bool shouldAsync = options_.asyncScheduler->ShouldRunAsync(passIdx, pass.flags);
                queueAssignments[passIdx] = shouldAsync ? RGQueueType::AsyncCompute : RGQueueType::Graphics;
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
    // Stage 7a: Compute resource lifetime intervals
    // =========================================================================

    void RenderGraphCompiler::ComputeLifetimes(
        const RenderGraphBuilder& builder, const std::vector<uint32_t>& order, const std::vector<bool>& activeSet,
        std::vector<CompiledRenderGraph::ResourceLifetime>& lifetimes
    ) {
        auto& resources = builder.GetResources();
        auto& passes = builder.GetPasses();

        // Build per-resource first/last pass positions in O(passes * accesses)
        struct Interval {
            uint32_t first = std::numeric_limits<uint32_t>::max();
            uint32_t last = 0;
        };
        std::vector<Interval> intervals(resources.size());

        for (uint32_t pos = 0; pos < order.size(); ++pos) {
            auto passIdx = order[pos];
            auto& pass = passes[passIdx];
            for (auto& r : pass.reads) {
                auto idx = r.handle.GetIndex();
                intervals[idx].first = std::min(intervals[idx].first, pos);
                intervals[idx].last = std::max(intervals[idx].last, pos);
            }
            for (auto& w : pass.writes) {
                auto idx = w.handle.GetIndex();
                intervals[idx].first = std::min(intervals[idx].first, pos);
                intervals[idx].last = std::max(intervals[idx].last, pos);
            }
        }

        lifetimes.clear();
        lifetimes.reserve(resources.size());
        for (uint16_t resIdx = 0; resIdx < resources.size(); ++resIdx) {
            if (resources[resIdx].imported || resources[resIdx].lifetimeExtended) {
                continue;
            }
            if (intervals[resIdx].first == std::numeric_limits<uint32_t>::max()) {
                continue;
            }
            lifetimes.push_back(
                CompiledRenderGraph::ResourceLifetime{
                    .resourceIndex = resIdx,
                    .firstPass = intervals[resIdx].first,
                    .lastPass = intervals[resIdx].last,
                }
            );
        }
    }

    // =========================================================================
    // Stage 7b-e: Transient resource aliasing (interval graph coloring +
    //             alignment-aware offset packing + aliasing barrier injection)
    // =========================================================================

    void RenderGraphCompiler::TransientResourceAliasing(
        const RenderGraphBuilder& builder, const std::vector<uint32_t>& order,
        const std::vector<CompiledRenderGraph::ResourceLifetime>& lifetimes, AliasingLayout& aliasing,
        std::vector<CompiledPassInfo>& compiledPasses
    ) {
        ComputeAliasingSlots(builder, order, lifetimes, aliasing);
        InjectAliasingBarriers(builder, order, lifetimes, aliasing, compiledPasses);
    }

    // ---- Stage 7a: Pure slot computation (parallelizable with Stage 6) ----

    void RenderGraphCompiler::ComputeAliasingSlots(
        const RenderGraphBuilder& builder, const std::vector<uint32_t>& order,
        const std::vector<CompiledRenderGraph::ResourceLifetime>& lifetimes, AliasingLayout& aliasing
    ) {
        auto& resources = builder.GetResources();
        auto& passes = builder.GetPasses();

        aliasing.resourceToSlot.assign(resources.size(), AliasingLayout::kNotAliased);
        aliasing.heapGroupSizes.fill(0);

        // ---- Gather per-resource metadata ----
        struct ResourceMeta {
            uint16_t resourceIndex = 0;
            uint64_t estimatedSize = 0;
            uint64_t alignment = 0;
            HeapGroupType heapGroup = HeapGroupType::RtDs;
            uint32_t firstPass = 0;
            uint32_t lastPass = 0;
            ResourceAccess combinedAccess = ResourceAccess::None;
        };

        auto combinedAccesses = BuildCombinedAccesses(order, passes, resources.size());

        std::vector<ResourceMeta> metas;
        metas.reserve(lifetimes.size());
        for (auto& lt : lifetimes) {
            auto& res = resources[lt.resourceIndex];
            ResourceMeta meta{
                .resourceIndex = lt.resourceIndex,
                .firstPass = lt.firstPass,
                .lastPass = lt.lastPass,
                .combinedAccess = combinedAccesses[lt.resourceIndex],
            };
            if (res.kind == RGResourceKind::Texture) {
                meta.estimatedSize = EstimateTextureSize(res.textureDesc);
                meta.alignment = EstimateTextureAlignment(res.textureDesc);
                meta.heapGroup = ClassifyHeapGroup(RGResourceKind::Texture, meta.combinedAccess);
            } else {
                meta.estimatedSize = EstimateBufferSize(res.bufferDesc);
                meta.alignment = EstimateBufferAlignment(res.bufferDesc);
                meta.heapGroup = HeapGroupType::Buffer;
            }
            metas.push_back(meta);
        }

        // ---- Sort by size descending (first-fit decreasing) ----
        std::sort(metas.begin(), metas.end(), [](const ResourceMeta& a, const ResourceMeta& b) {
            return a.estimatedSize > b.estimatedSize;
        });

        // ---- Interval graph coloring ----
        // For each resource, find an existing slot in the same heap group with non-overlapping lifetime.
        // 10% overshoot tolerance on slot size per spec §5.6.
        constexpr double kOvershootTolerance = 1.1;

        for (auto& meta : metas) {
            bool assigned = false;
            for (auto& slot : aliasing.slots) {
                if (slot.heapGroup != meta.heapGroup) {
                    continue;
                }
                // Check lifetime overlap
                if (meta.firstPass <= slot.lifetimeEnd && meta.lastPass >= slot.lifetimeStart) {
                    continue;
                }
                // Check size compatibility (slot can hold resource within overshoot)
                bool slotTooSmall = (slot.size < meta.estimatedSize)
                                    && (static_cast<double>(meta.estimatedSize)
                                        > static_cast<double>(slot.size) * kOvershootTolerance);
                if (slotTooSmall) {
                    continue;
                }

                // Assign to this slot
                aliasing.resourceToSlot[meta.resourceIndex] = slot.slotIndex;
                slot.lifetimeStart = std::min(slot.lifetimeStart, meta.firstPass);
                slot.lifetimeEnd = std::max(slot.lifetimeEnd, meta.lastPass);
                slot.size = std::max(slot.size, meta.estimatedSize);
                slot.alignment = std::max(slot.alignment, meta.alignment);
                assigned = true;
                break;
            }

            if (!assigned) {
                uint32_t slotIdx = static_cast<uint32_t>(aliasing.slots.size());
                aliasing.slots.push_back(
                    AliasingSlot{
                        .slotIndex = slotIdx,
                        .heapGroup = meta.heapGroup,
                        .size = meta.estimatedSize,
                        .alignment = meta.alignment,
                        .lifetimeStart = meta.firstPass,
                        .lifetimeEnd = meta.lastPass,
                    }
                );
                aliasing.resourceToSlot[meta.resourceIndex] = slotIdx;
            }
        }

        // ---- Alignment-aware offset packing per heap group ----
        // Group slots by heap group, then pack offsets with alignment.
        for (size_t g = 0; g < kHeapGroupCount; ++g) {
            auto group = static_cast<HeapGroupType>(g);
            uint64_t offset = 0;

            // Sort slots within group by size descending for tightest packing
            // (MSAA 4MB-aligned first to avoid fragmenting 64KB-aligned resources)
            std::vector<uint32_t> groupSlotIndices;
            for (auto& slot : aliasing.slots) {
                if (slot.heapGroup == group) {
                    groupSlotIndices.push_back(slot.slotIndex);
                }
            }
            std::sort(groupSlotIndices.begin(), groupSlotIndices.end(), [&](uint32_t a, uint32_t b) {
                return aliasing.slots[a].alignment > aliasing.slots[b].alignment;
            });

            for (auto slotIdx : groupSlotIndices) {
                auto& slot = aliasing.slots[slotIdx];
                uint64_t align = slot.alignment > 0 ? slot.alignment : 1;
                uint64_t alignedOffset = (offset + align - 1) & ~(align - 1);
                slot.heapOffset = alignedOffset;
                offset = alignedOffset + slot.size;
            }

            aliasing.heapGroupSizes[g] = offset;
        }
    }

    // ---- Stage 7b: Aliasing barrier injection (must run after Stage 6) ----

    void RenderGraphCompiler::InjectAliasingBarriers(
        const RenderGraphBuilder& builder, const std::vector<uint32_t>& order,
        const std::vector<CompiledRenderGraph::ResourceLifetime>& lifetimes, AliasingLayout& aliasing,
        std::vector<CompiledPassInfo>& compiledPasses
    ) {
        auto& resources = builder.GetResources();
        auto& passes = builder.GetPasses();

        auto combinedAccesses = BuildCombinedAccesses(order, passes, resources.size());

        // Build per-resource first/last pass from lifetimes, filtered to aliased resources
        struct AliasedResource {
            uint16_t resourceIndex = 0;
            uint32_t firstPass = 0;
            uint32_t lastPass = 0;
            ResourceAccess combinedAccess = ResourceAccess::None;
        };
        std::vector<AliasedResource> aliased;
        aliased.reserve(lifetimes.size());
        for (auto& lt : lifetimes) {
            if (aliasing.resourceToSlot[lt.resourceIndex] != AliasingLayout::kNotAliased) {
                aliased.push_back({
                    .resourceIndex = lt.resourceIndex,
                    .firstPass = lt.firstPass,
                    .lastPass = lt.lastPass,
                    .combinedAccess = combinedAccesses[lt.resourceIndex],
                });
            }
        }
        std::sort(aliased.begin(), aliased.end(), [](const AliasedResource& a, const AliasedResource& b) {
            return a.firstPass < b.firstPass;
        });

        // Map from topo order position -> compiled pass index
        std::unordered_map<uint32_t, uint32_t> passIdxToCompiledIdx;
        for (uint32_t ci = 0; ci < compiledPasses.size(); ++ci) {
            passIdxToCompiledIdx[order[ci]] = ci;
        }

        // Track slot ownership for handoff detection
        struct SlotOwnership {
            uint16_t currentResource = std::numeric_limits<uint16_t>::max();
            uint32_t lastPassEnd = 0;
        };
        std::vector<SlotOwnership> slotOwners(aliasing.slots.size());

        for (auto& ar : aliased) {
            uint32_t slotIdx = aliasing.resourceToSlot[ar.resourceIndex];
            auto& owner = slotOwners[slotIdx];

            // Only emit aliasing barrier at ownership HANDOFF — when a slot that was previously
            // used by a different resource is now being reused. The very first resource in a slot
            // does NOT need an aliasing barrier because Stage 6 already handles the initial
            // UNDEFINED -> first-layout transition via normal barrier synthesis.
            bool needsAliasingBarrier
                = (owner.currentResource != std::numeric_limits<uint16_t>::max()
                   && owner.currentResource != ar.resourceIndex);

            if (needsAliasingBarrier) {
                auto& res = resources[ar.resourceIndex];
                auto dstBarrier = ResolveBarrier(ar.combinedAccess);

                BarrierCommand barrier{
                    .resourceIndex = ar.resourceIndex,
                    .srcAccess = ResourceAccess::None,
                    .dstAccess = ar.combinedAccess,
                    .srcLayout = rhi::TextureLayout::Undefined,
                    .dstLayout
                    = (res.kind == RGResourceKind::Texture) ? dstBarrier.layout : rhi::TextureLayout::Undefined,
                    .isAliasingBarrier = true,
                };

                aliasing.aliasingBarriers.push_back(barrier);
                aliasing.aliasingBarrierPassPos.push_back(ar.firstPass);

                // Inject into compiled pass's acquire barriers (§5.6.4: co-locate with acquire barriers)
                uint32_t passIdx = order[ar.firstPass];
                auto it = passIdxToCompiledIdx.find(passIdx);
                if (it != passIdxToCompiledIdx.end()) {
                    // Insert aliasing barriers at the beginning of acquire barriers
                    auto& acq = compiledPasses[it->second].acquireBarriers;
                    acq.insert(acq.begin(), barrier);
                }
            }

            owner.currentResource = ar.resourceIndex;
            owner.lastPassEnd = ar.lastPass;
        }
    }

    // =========================================================================
    // Stage 9: Backend adaptation pass injection — §5.1 Stage 9
    // =========================================================================

    namespace {

        // Infer which adaptation::Feature a resource access pattern requires.
        // Returns a list of features that may need adaptation for the given pass.
        auto InferAdaptationFeatures(const RGPassNode& pass, const std::vector<RGResourceNode>& resources)
            -> std::vector<rhi::adaptation::Feature> {
            std::vector<rhi::adaptation::Feature> features;
            for (auto& w : pass.writes) {
                auto idx = w.handle.GetIndex();
                if (idx >= resources.size()) {
                    continue;
                }
                auto& res = resources[idx];
                // Blit/copy on texture
                if (res.kind == RGResourceKind::Texture
                    && (w.access & ResourceAccess::TransferDst) != ResourceAccess::None) {
                    features.push_back(rhi::adaptation::Feature::CmdBlitTexture);
                }
                // Buffer fill (TransferDst on buffer)
                if (res.kind == RGResourceKind::Buffer
                    && (w.access & ResourceAccess::TransferDst) != ResourceAccess::None) {
                    features.push_back(rhi::adaptation::Feature::CmdFillBufferNonZero);
                }
            }
            for (auto& r : pass.reads) {
                auto idx = r.handle.GetIndex();
                if (idx >= resources.size()) {
                    continue;
                }
                // Texture clear outside render pass
                if (resources[idx].kind == RGResourceKind::Texture
                    && (r.access & ResourceAccess::TransferSrc) != ResourceAccess::None) {
                    features.push_back(rhi::adaptation::Feature::CmdClearTexture);
                }
            }
            return features;
        }

    }  // anonymous namespace

    void RenderGraphCompiler::InjectAdaptationPasses(
        const RenderGraphBuilder& builder, const std::vector<uint32_t>& order,
        std::vector<CompiledPassInfo>& compiledPasses, std::vector<AdaptationPassInfo>& adaptationPasses
    ) {
        auto backend = options_.backendType;
        if (backend == rhi::BackendType::Mock) {
            return;  // Mock: no adaptation needed
        }

        auto& passes = builder.GetPasses();
        auto& resources = builder.GetResources();

        for (uint32_t pos = 0; pos < compiledPasses.size(); ++pos) {
            auto& cpi = compiledPasses[pos];
            auto& pass = passes[cpi.passIndex];

            auto features = InferAdaptationFeatures(pass, resources);
            for (auto feature : features) {
                auto strategy = rhi::adaptation::QueryStrategy(backend, feature);
                if (strategy == rhi::adaptation::Strategy::Native
                    || strategy == rhi::adaptation::Strategy::Unsupported) {
                    continue;
                }
                // Only inject auxiliary passes for strategies with hidden GPU work
                if (!rhi::adaptation::HasGpuOverhead(strategy)) {
                    continue;
                }

                auto* rule = rhi::adaptation::QueryRule(backend, feature);
                AdaptationPassInfo info;
                info.originalPassIndex = cpi.passIndex;
                info.insertBeforePosition = pos;
                info.queue = (strategy == rhi::adaptation::Strategy::StagingCopy) ? RGQueueType::Transfer : cpi.queue;
                info.feature = feature;
                info.strategy = strategy;
                info.description = rule ? rule->description : nullptr;
                adaptationPasses.push_back(info);
            }
        }
    }

    // =========================================================================
    // Stage 10: Command batch formation — §5.1 Stage 10
    // =========================================================================

    void RenderGraphCompiler::FormCommandBatches(
        const std::vector<uint32_t>& order, const std::vector<CompiledPassInfo>& compiledPasses,
        const std::vector<CrossQueueSyncPoint>& syncPoints, const std::vector<MergedRenderPassGroup>& mergedGroups,
        std::vector<CommandBatch>& batches
    ) {
        if (compiledPasses.empty()) {
            return;
        }

        // Build lookup: compiled pass position -> merged group index (UINT32_MAX if not merged)
        std::vector<uint32_t> passToMergedGroup(compiledPasses.size(), UINT32_MAX);
        for (uint32_t gi = 0; gi < mergedGroups.size(); ++gi) {
            for (auto pos : mergedGroups[gi].subpassIndices) {
                if (pos < passToMergedGroup.size()) {
                    passToMergedGroup[pos] = gi;
                }
            }
        }

        // Build lookup: compiled pass position -> sync points where this pass is the dst (requires wait)
        std::unordered_map<uint32_t, std::vector<const CrossQueueSyncPoint*>> dstSyncMap;
        // Build lookup: compiled pass position -> sync points where this pass is the src (requires signal)
        std::unordered_set<uint32_t> srcSyncPositions;
        for (auto& sp : syncPoints) {
            // Map pass indices to compiled pass positions
            for (uint32_t pos = 0; pos < compiledPasses.size(); ++pos) {
                if (compiledPasses[pos].passIndex == sp.dstPassIndex) {
                    dstSyncMap[pos].push_back(&sp);
                }
                if (compiledPasses[pos].passIndex == sp.srcPassIndex) {
                    srcSyncPositions.insert(pos);
                }
            }
        }

        // Linear scan: group consecutive passes on the same queue into batches.
        // Split batch when: (1) queue changes, (2) dst sync point (must wait before starting),
        // (3) would split a merged render pass group.
        CommandBatch current;
        current.queue = compiledPasses[0].queue;
        current.passIndices.push_back(0);

        auto flushBatch = [&]() {
            if (!current.passIndices.empty()) {
                // Mark signalTimeline if this batch contains any cross-queue sync source
                for (auto pos : current.passIndices) {
                    if (srcSyncPositions.contains(pos)) {
                        current.signalTimeline = true;
                        break;
                    }
                }
                batches.push_back(std::move(current));
            }
            current = {};
        };

        for (uint32_t pos = 1; pos < compiledPasses.size(); ++pos) {
            auto& cpi = compiledPasses[pos];

            // Check if this pass is the start of a cross-queue wait → new batch
            bool needsNewBatch = false;

            // Rule 1: queue change
            if (cpi.queue != current.queue) {
                needsNewBatch = true;
            }

            // Rule 2: this pass is a cross-queue sync destination (must wait)
            if (dstSyncMap.contains(pos)) {
                needsNewBatch = true;
            }

            // Rule 3: would split a merged render pass group
            // If previous pass and this pass are in the same merged group, do NOT split
            if (needsNewBatch && !current.passIndices.empty()) {
                uint32_t prevPos = current.passIndices.back();
                if (passToMergedGroup[prevPos] != UINT32_MAX && passToMergedGroup[prevPos] == passToMergedGroup[pos]) {
                    needsNewBatch = false;  // Keep in same batch — merged group is atomic
                }
            }

            if (needsNewBatch) {
                flushBatch();
                current.queue = cpi.queue;

                // Collect cross-queue waits for this batch
                if (dstSyncMap.contains(pos)) {
                    for (auto* sp : dstSyncMap[pos]) {
                        current.waits.push_back({.srcQueue = sp->srcQueue, .timelineValue = sp->timelineValue});
                    }
                }
            }

            current.passIndices.push_back(pos);
        }

        flushBatch();

        // Final batch always signals timeline (for frame completion)
        if (!batches.empty()) {
            batches.back().signalTimeline = true;
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

        // Stage 3b: Barrier-aware global reordering (optional)
        if (options_.enableBarrierReordering) {
            PassReorderer reorderer(options_.strategy);
            reorderer.Reorder(builder, result.edges, activeSet, order);
        }

        // Stage 4: Queue assignment
        std::vector<RGQueueType> queueAssignments;
        AssignQueues(builder, order, queueAssignments);

        // Stage 5: Cross-queue sync synthesis
        SynthesizeCrossQueueSync(result.edges, queueAssignments, result.syncPoints);

        // Stage 5b: Deadlock prevention (§7.5) — detect cross-queue cycles and demote
        if (options_.enableDeadlockPrevention && options_.enableAsyncCompute) {
            auto dlResult = AsyncComputeScheduler::DetectAndPreventDeadlocks(
                result.syncPoints, queueAssignments, builder.GetPasses()
            );
            if (dlResult.hasCycle) {
                // Re-synthesize sync points after demotion changed queue assignments
                result.syncPoints.clear();
                SynthesizeCrossQueueSync(result.edges, queueAssignments, result.syncPoints);
            }
        }

        // Stage 6 || Stage 7a: Run barrier synthesis and aliasing slot computation in parallel.
        // Stage 6 writes result.passes (barriers), Stage 7a writes result.lifetimes + result.aliasing (slots).
        // No shared mutable state between them — safe to parallelize.
        auto aliasingFuture = std::async(std::launch::async, [&] {
            ComputeLifetimes(builder, order, activeSet, result.lifetimes);
            if (options_.enableTransientAliasing) {
                ComputeAliasingSlots(builder, order, result.lifetimes, result.aliasing);
            }
        });

        BarrierSynthesizer barrierSynth({
            .backendType = options_.backendType,
            .enableSplitBarriers = options_.enableSplitBarriers,
        });
        barrierSynth.Synthesize(builder, order, queueAssignments, result.passes);

        aliasingFuture.get();  // Wait for Stage 7a to complete

        // Stage 7b: Inject aliasing barriers into compiled passes (must run after Stage 6)
        if (options_.enableTransientAliasing) {
            InjectAliasingBarriers(builder, order, result.lifetimes, result.aliasing, result.passes);
        }

        // Stage 8: Render pass merging
        if (options_.enableRenderPassMerging) {
            PassMerger merger({.capabilities = options_.capabilities});
            merger.Merge(builder, order, result.aliasing, result.syncPoints, result.passes, result.mergedGroups);
        }

        // Stage 9: Backend adaptation pass injection
        if (options_.enableAdaptation) {
            InjectAdaptationPasses(builder, order, result.passes, result.adaptationPasses);
        }

        // Stage 10: Command batch formation
        FormCommandBatches(order, result.passes, result.syncPoints, result.mergedGroups, result.batches);

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
