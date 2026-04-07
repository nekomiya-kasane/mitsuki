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
#include "miki/rendergraph/RenderGraphBuilder.h"
#include "miki/rendergraph/RenderGraphTypes.h"

namespace miki::rg {

    /// @brief Compiles a RenderGraphBuilder into an executable CompiledRenderGraph.
    class RenderGraphCompiler {
       public:
        struct Options {
            SchedulerStrategy strategy = SchedulerStrategy::Balanced;
            bool enableSplitBarriers = true;      ///< Use split barriers when backend supports
            bool enableAsyncCompute = true;       ///< Allow async compute queue scheduling
            bool enableTransientAliasing = true;  ///< Enable transient resource memory aliasing
            bool enableRenderPassMerging = true;  ///< Merge consecutive graphics passes into subpasses
            bool enableBarrierReordering = true;  ///< Enable barrier-aware global reordering pass
        };

        RenderGraphCompiler() noexcept = default;
        explicit RenderGraphCompiler(const Options& options) noexcept : options_(options) {}

        /// @brief Compile a built graph into an execution plan.
        /// @param builder The finalized graph builder (must have called Build()).
        /// @return CompiledRenderGraph on success, ErrorCode on failure.
        [[nodiscard]] auto Compile(RenderGraphBuilder& builder) -> core::Result<CompiledRenderGraph>;

        /// @brief Check if a previously compiled graph can be reused (cache hit).
        /// @param prev Previous frame's compiled graph.
        /// @param builder Current frame's graph builder.
        /// @return true if structural hash matches (skip recompilation).
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

        // Stage 6: Synthesize barriers (split barrier model)
        void SynthesizeBarriers(
            const RenderGraphBuilder& builder, const std::vector<uint32_t>& order,
            const std::vector<DependencyEdge>& edges, const std::vector<RGQueueType>& queueAssignments,
            std::vector<CompiledPassInfo>& compiledPasses
        );

        // Compute structural hash for caching
        auto ComputeStructuralHash(const RenderGraphBuilder& builder, const std::vector<bool>& activeSet)
            -> CompiledRenderGraph::StructuralHash;

        Options options_;
    };

}  // namespace miki::rg
