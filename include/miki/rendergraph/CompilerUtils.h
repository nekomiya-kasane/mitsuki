/** @file CompilerUtils.h
 *  @brief Shared utilities for render graph compiler stages.
 *
 *  Stateless helper functions used across multiple compiler stages:
 *    - BuildCombinedAccesses (Stage 7a, 7b)
 *
 *  Namespace: miki::rg
 */
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "miki/rendergraph/RenderGraphTypes.h"

namespace miki::rg {

    /// @brief Build per-resource combined access mask (OR of all read/write accesses).
    /// Used by Stage 7a (aliasing slot computation) and Stage 7b (aliasing barrier injection).
    /// @param order Topological pass order.
    /// @param passes All pass nodes.
    /// @param resourceCount Total number of resources.
    /// @return Vector indexed by resource index, each element is the OR of all accesses.
    [[nodiscard]] inline auto BuildCombinedAccesses(
        std::span<const uint32_t> order, std::span<const RGPassNode> passes, size_t resourceCount
    ) -> std::vector<ResourceAccess> {
        std::vector<ResourceAccess> result(resourceCount, ResourceAccess::None);
        for (auto passIdx : order) {
            auto& pass = passes[passIdx];
            for (auto& r : pass.reads) {
                result[r.handle.GetIndex()] = result[r.handle.GetIndex()] | r.access;
            }
            for (auto& w : pass.writes) {
                result[w.handle.GetIndex()] = result[w.handle.GetIndex()] | w.access;
            }
        }
        return result;
    }

}  // namespace miki::rg
