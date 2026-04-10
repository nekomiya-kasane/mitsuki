/** @file PassMerger.h
 *  @brief Stage 8: Render pass merging (subpass consolidation).
 *
 *  Evaluates consecutive graphics passes for merge eligibility and profitability,
 *  producing MergedRenderPassGroup structures. Supports:
 *    - 6 merge conditions (same queue, same area, shared attachment, no cross-queue,
 *      no aliasing conflict, no present/host access)
 *    - Profitability limits (max subpasses, max attachments, UAV+color heuristic)
 *    - Subpass dependency synthesis from converted barriers
 *
 *  See: specs/04-render-graph.md §5.7
 *  Namespace: miki::rg
 */
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/GpuCapabilityProfile.h"

namespace miki::rg {

    class RenderGraphBuilder;

    // =========================================================================
    // PassMerger configuration
    // =========================================================================

    struct PassMergerConfig {
        const rhi::GpuCapabilityProfile* capabilities = nullptr;
        bool enableRenderPassMerging = true;
    };

    // =========================================================================
    // PassMerger — evaluates and applies render pass merging
    // =========================================================================

    class PassMerger {
       public:
        explicit PassMerger(const PassMergerConfig& config) noexcept : config_(config) {}

        /// @brief Run Stage 8: merge consecutive graphics passes into subpass groups.
        /// Modifies compiledPasses in-place (converts barriers to subpass dependencies).
        void Merge(
            const RenderGraphBuilder& builder, std::span<const uint32_t> order, const AliasingLayout& aliasing,
            std::span<const CrossQueueSyncPoint> syncPoints, std::vector<CompiledPassInfo>& compiledPasses,
            std::vector<MergedRenderPassGroup>& mergedGroups
        );

       private:
        // ---- Merge condition evaluators ----

        /// @brief Get render area dimensions for a pass from its write attachments.
        [[nodiscard]] static auto GetPassRenderArea(const RGPassNode& pass, std::span<const RGResourceNode> resources)
            -> std::pair<uint32_t, uint32_t>;

        /// @brief Check if pass has PresentSrc access that prevents merging.
        [[nodiscard]] static auto HasMergeBlockingAccess(const RGPassNode& pass) -> bool;

        /// @brief Collect resource indices that are RT/DS attachments.
        [[nodiscard]] static auto CollectAttachmentIndices(const RGPassNode& pass) -> std::vector<uint16_t>;

        /// @brief Check if next pass reads an attachment output of prev pass.
        [[nodiscard]] static auto HasSharedAttachment(
            const RGPassNode& prev, const RGPassNode& next, std::span<const RGResourceNode> resources
        ) -> bool;

        /// @brief Check if a cross-queue sync point sits between two topo-order positions.
        [[nodiscard]] static auto HasCrossQueueBetween(
            std::span<const CrossQueueSyncPoint> syncPoints, uint32_t posA, uint32_t posB
        ) -> bool;

        /// @brief Check aliasing conflict: resources in two passes map to same aliasing slot.
        [[nodiscard]] static auto HasAliasingConflict(
            const RGPassNode& prev, const RGPassNode& next, const AliasingLayout& aliasing
        ) -> bool;

        /// @brief Check if pass has UAV write + color output simultaneously.
        [[nodiscard]] static auto HasUavAndColorWrite(const RGPassNode& pass) -> bool;

        // ---- Merge application ----

        /// @brief Evaluate all 6 conditions + profitability for merging candidate into group.
        [[nodiscard]] auto CanMerge(
            const RGPassNode& prevPass, const RGPassNode& candidatePass, std::span<const RGResourceNode> resources,
            const MergedRenderPassGroup& currentGroup, const AliasingLayout& aliasing,
            std::span<const CrossQueueSyncPoint> syncPoints, uint32_t prevPos, uint32_t candidatePos,
            const CompiledPassInfo& candidateCpi
        ) const -> bool;

        /// @brief Apply merge: convert barriers to subpass dependencies, add to group.
        static void MergeIntoGroup(
            MergedRenderPassGroup& group, uint32_t prevPos, uint32_t pos, const RGPassNode& pass,
            std::vector<CompiledPassInfo>& compiledPasses
        );

        PassMergerConfig config_;
    };

}  // namespace miki::rg
