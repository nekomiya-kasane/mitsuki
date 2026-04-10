/** @file RenderGraphCompiler.h
 *  @brief Transforms a declarative graph description into an optimized execution plan.
 *
 *  The compiler runs a 10-stage pipeline:
 *    1. Condition evaluation & pass culling
 *    2. Dependency analysis & DAG construction
 *    3. Topological sort (modified Kahn's with priority heuristics)
 *    4. Queue assignment
 *    5. Cross-queue synchronization synthesis
 *    6. Barrier synthesis (split barrier model)
 *    7. Transient resource aliasing (parallel with 6)
 *    8. Render pass merging
 *    9. Backend adaptation pass injection
 *   10. Command batch formation
 *
 *  Namespace: miki::rg
 */
#pragma once

#include "miki/core/Result.h"
#include "miki/rendergraph/AsyncComputeScheduler.h"
#include "miki/rendergraph/BarrierSynthesizer.h"
#include "miki/rendergraph/PassMerger.h"
#include "miki/rendergraph/PassReorderer.h"
#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/GpuCapabilityProfile.h"

namespace miki::rg {

    /// @brief Compiles a RenderGraphBuilder into an executable CompiledRenderGraph.
    class RenderGraphCompiler {
       public:
        struct Options {
            SchedulerStrategy strategy = SchedulerStrategy::Balanced;
            rhi::BackendType backendType = rhi::BackendType::Mock;    ///< Current backend for adaptation queries
            const rhi::GpuCapabilityProfile* capabilities = nullptr;  ///< GPU caps for merge profitability (nullable)
            bool enableSplitBarriers = true;                          ///< Use split barriers when backend supports
            bool enableAsyncCompute = true;                           ///< Allow async compute queue scheduling
            bool enableTransientAliasing = true;                      ///< Enable transient resource memory aliasing
            bool enableRenderPassMerging = true;   ///< Merge consecutive graphics passes into subpasses
            bool enableAdaptation = true;          ///< Enable backend adaptation pass injection
            bool enableBarrierReordering = true;   ///< Enable barrier-aware global reordering pass
            bool enableDeadlockPrevention = true;  ///< Enable cross-queue deadlock detection and demotion (§7.5)
            AsyncComputeScheduler* asyncScheduler = nullptr;  ///< Adaptive async scheduler (nullable, §7.2)
        };

        RenderGraphCompiler() noexcept = default;
        explicit RenderGraphCompiler(const Options& options) noexcept : options_(options) {}

        /// @brief Compile a built graph into an execution plan (full recompile).
        /// @param builder The finalized graph builder (must have called Build()).
        /// @return CompiledRenderGraph on success, ErrorCode on failure.
        [[nodiscard]] auto Compile(RenderGraphBuilder& builder) -> core::Result<CompiledRenderGraph>;

        /// @brief Incremental recompile: topology unchanged, only descriptors changed.
        /// Re-runs Stages 7-10 (aliasing, merging, adaptation, batching) only.
        /// Falls back to full Compile() if topology has changed.
        /// @param builder The finalized graph builder for the current frame.
        /// @param prev Previous frame's compiled graph (modified in-place, then returned).
        [[nodiscard]] auto CompileIncremental(RenderGraphBuilder& builder, CompiledRenderGraph& prev)
            -> core::Result<CompiledRenderGraph>;

        /// @brief Classify cache hit: FullHit / DescriptorOnlyChange / Miss.
        /// @param prev Previous frame's compiled graph.
        /// @param builder Current frame's graph builder.
        /// @param activeSet Condition evaluation results for current frame.
        [[nodiscard]] auto ClassifyCache(
            const CompiledRenderGraph& prev, const RenderGraphBuilder& builder, const std::vector<bool>& activeSet
        ) const -> CacheHitResult;

        /// @brief Quick check: is the previous graph fully reusable?
        /// Evaluates conditions internally. For finer control, use ClassifyCache.
        [[nodiscard]] auto IsCacheHit(const CompiledRenderGraph& prev, const RenderGraphBuilder& builder) const -> bool;

       private:
        // Stage 1: Evaluate conditions and cull dead passes
        void EvaluateConditions(RenderGraphBuilder& builder, std::vector<bool>& activeSet);

        // Stage 2: Build DAG from resource read/write declarations
        void BuildDAG(
            const RenderGraphBuilder& builder, const std::vector<bool>& activeSet, std::vector<DependencyEdge>& edges
        );

        // Stage 3: Topological sort with priority heuristics
        auto TopologicalSort(
            const RenderGraphBuilder& builder, const std::vector<DependencyEdge>& edges,
            const std::vector<bool>& activeSet
        ) -> core::Result<std::vector<uint32_t>>;

        // Stage 4: Assign passes to queues
        void AssignQueues(
            const RenderGraphBuilder& builder, std::vector<uint32_t>& order, std::vector<RGQueueType>& queueAssignments
        );

        // Stage 5: Synthesize cross-queue synchronization points
        void SynthesizeCrossQueueSync(
            const std::vector<DependencyEdge>& edges, const std::vector<RGQueueType>& queueAssignments,
            std::vector<CrossQueueSyncPoint>& syncPoints
        );

        // Stage 7: Transient resource aliasing (memory scheduling)
        void ComputeLifetimes(
            const RenderGraphBuilder& builder, const std::vector<uint32_t>& order, const std::vector<bool>& activeSet,
            std::vector<CompiledRenderGraph::ResourceLifetime>& lifetimes
        );

        void TransientResourceAliasing(
            const RenderGraphBuilder& builder, const std::vector<uint32_t>& order,
            const std::vector<CompiledRenderGraph::ResourceLifetime>& lifetimes, AliasingLayout& aliasing,
            std::vector<CompiledPassInfo>& compiledPasses
        );

        // Stage 7a: Compute aliasing slot assignments (pure computation, no pass mutation)
        void ComputeAliasingSlots(
            const RenderGraphBuilder& builder, const std::vector<uint32_t>& order,
            const std::vector<CompiledRenderGraph::ResourceLifetime>& lifetimes, AliasingLayout& aliasing
        );

        // Stage 7b: Inject aliasing barriers into compiled passes (must run after Stage 6)
        void InjectAliasingBarriers(
            const RenderGraphBuilder& builder, const std::vector<uint32_t>& order,
            const std::vector<CompiledRenderGraph::ResourceLifetime>& lifetimes, AliasingLayout& aliasing,
            std::vector<CompiledPassInfo>& compiledPasses
        );

        // Stage 9: Backend adaptation pass injection
        void InjectAdaptationPasses(
            const RenderGraphBuilder& builder, const std::vector<uint32_t>& order,
            std::vector<CompiledPassInfo>& compiledPasses, std::vector<AdaptationPassInfo>& adaptationPasses
        );

        // Stage 10: Command batch formation
        void FormCommandBatches(
            const std::vector<uint32_t>& order, const std::vector<CompiledPassInfo>& compiledPasses,
            const std::vector<CrossQueueSyncPoint>& syncPoints, const std::vector<MergedRenderPassGroup>& mergedGroups,
            std::vector<CommandBatch>& batches
        );

        // Stage 1b: History lifetime extension — prevent aliasing of history resources whose producer is culled
        void ProcessHistoryLifetimes(
            RenderGraphBuilder& builder, const std::vector<bool>& activeSet, uint64_t frameIndex,
            std::vector<HistoryResourceInfo>& historyResources
        );

        // Compute structural hash for caching (Phase I, §10.1)
        auto ComputeStructuralHash(const RenderGraphBuilder& builder, const std::vector<bool>& activeSet) const
            -> CompiledRenderGraph::StructuralHash;

        // Collect external (imported) resource slots (Phase I, §10.3)
        void CollectExternalResources(const RenderGraphBuilder& builder, std::vector<ExternalResourceSlot>& slots);

        // Re-run Stages 7-10 for incremental recompile (Phase I, §10.2)
        void RecompileDescriptorDependentStages(
            RenderGraphBuilder& builder, const std::vector<uint32_t>& order, const std::vector<bool>& activeSet,
            const std::vector<RGQueueType>& queueAssignments, CompiledRenderGraph& result
        );

        Options options_;
        uint64_t frameIndex_ = 0;  ///< Monotonic frame counter for history staleness

        // Cached topology data for incremental recompile (Phase I, §10.2)
        std::vector<uint32_t> cachedOrder_;
        std::vector<RGQueueType> cachedQueueAssignments_;
        std::vector<bool> cachedActiveSet_;
    };

}  // namespace miki::rg
