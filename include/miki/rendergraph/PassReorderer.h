/** @file PassReorderer.h
 *  @brief Stage 3b: Barrier-aware global reordering (optional second pass).
 *
 *  After topological sort, reorders passes within their valid DAG range to
 *  minimize a weighted objective function (barrier cost, peak memory, latency).
 *
 *  See: specs/04-render-graph.md §5.3.1
 *  Namespace: miki::rg
 */
#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "miki/rendergraph/RenderGraphTypes.h"

namespace miki::rg {

    class RenderGraphBuilder;

    // =========================================================================
    // DAG constraints — predecessor/successor lookup for insertion range
    // =========================================================================

    struct DagConstraints {
        std::vector<std::vector<uint32_t>> predecessors;  ///< passIndex -> predecessors
        std::vector<std::vector<uint32_t>> successors;    ///< passIndex -> successors

        void Build(uint32_t passCount, std::span<const DependencyEdge> edges, const std::vector<bool>& activeSet);
    };

    // =========================================================================
    // Cost estimators for reordering objective function
    // =========================================================================

    /// @brief Compute valid insertion range [lo, hi] for a pass within the order.
    [[nodiscard]] auto ComputeInsertionRange(
        uint32_t passIdx, std::span<const uint32_t> order, const DagConstraints& dag,
        std::span<const uint32_t> positionOf
    ) -> std::pair<uint32_t, uint32_t>;

    /// @brief Estimate total barrier cost of a given pass order.
    /// Adjacent-pass barriers cost 2 (full), non-adjacent cost 1 (split opportunity).
    [[nodiscard]] auto EstimateBarrierCost(
        std::span<const uint32_t> order, std::span<const DependencyEdge> edges, const std::vector<bool>& activeSet
    ) -> uint32_t;

    /// @brief Estimate peak transient memory from pass order (sweep-line).
    [[nodiscard]] auto EstimatePeakMemory(
        std::span<const uint32_t> order, std::span<const RGPassNode> passes, std::span<const RGResourceNode> resources
    ) -> uint64_t;

    // =========================================================================
    // PassReorderer — iterative local-search reordering
    // =========================================================================

    class PassReorderer {
       public:
        explicit PassReorderer(SchedulerStrategy strategy) noexcept : strategy_(strategy) {}

        /// @brief Reorder passes in-place to optimize the weighted objective.
        /// @param builder The graph builder (for pass/resource metadata).
        /// @param edges Dependency edges from Stage 2.
        /// @param activeSet Active pass bitmask from Stage 1.
        /// @param order [in/out] Topological order to reorder.
        void Reorder(
            const RenderGraphBuilder& builder, std::span<const DependencyEdge> edges,
            const std::vector<bool>& activeSet, std::vector<uint32_t>& order
        );

       private:
        SchedulerStrategy strategy_;
    };

}  // namespace miki::rg
