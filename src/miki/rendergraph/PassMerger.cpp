/** @file PassMerger.cpp
 *  @brief Stage 8: Render pass merging (subpass consolidation).
 *  See: PassMerger.h, specs/04-render-graph.md §5.7
 */

#include "miki/rendergraph/PassMerger.h"

#include <algorithm>

#include "miki/rendergraph/RenderGraphBuilder.h"

namespace miki::rg {

    // Merge profitability limits (§5.7.4)
    inline constexpr uint32_t kMaxSubpassesPerGroup = 8;
    inline constexpr uint32_t kMaxAttachmentsPerGroup = 8;

    // =========================================================================
    // Merge condition evaluators
    // =========================================================================

    auto PassMerger::GetPassRenderArea(const RGPassNode& pass, std::span<const RGResourceNode> resources)
        -> std::pair<uint32_t, uint32_t> {
        // Primary: from write attachments
        for (auto& w : pass.writes) {
            auto idx = w.handle.GetIndex();
            if (idx < resources.size() && resources[idx].kind == RGResourceKind::Texture) {
                bool isAttachment = (w.access & (ResourceAccess::ColorAttachWrite | ResourceAccess::DepthStencilWrite))
                                    != ResourceAccess::None;
                if (isAttachment) {
                    return {resources[idx].textureDesc.width, resources[idx].textureDesc.height};
                }
            }
        }
        // Fallback: from read textures (input attachment / shader read of RT)
        for (auto& r : pass.reads) {
            auto idx = r.handle.GetIndex();
            if (idx < resources.size() && resources[idx].kind == RGResourceKind::Texture) {
                return {resources[idx].textureDesc.width, resources[idx].textureDesc.height};
            }
        }
        return {0, 0};
    }

    auto PassMerger::HasMergeBlockingAccess(const RGPassNode& pass) -> bool {
        for (auto& r : pass.reads) {
            if ((r.access & ResourceAccess::PresentSrc) != ResourceAccess::None) {
                return true;
            }
        }
        for (auto& w : pass.writes) {
            if ((w.access & ResourceAccess::PresentSrc) != ResourceAccess::None) {
                return true;
            }
        }
        return false;
    }

    auto PassMerger::CollectAttachmentIndices(const RGPassNode& pass) -> std::vector<uint16_t> {
        std::vector<uint16_t> result;
        for (auto& w : pass.writes) {
            if ((w.access & (ResourceAccess::ColorAttachWrite | ResourceAccess::DepthStencilWrite))
                != ResourceAccess::None) {
                result.push_back(w.handle.GetIndex());
            }
        }
        return result;
    }

    auto PassMerger::HasSharedAttachment(
        const RGPassNode& prev, const RGPassNode& next, std::span<const RGResourceNode> resources
    ) -> bool {
        auto prevAttachments = CollectAttachmentIndices(prev);
        // Check if next reads any of prev's attachment outputs
        for (auto& r : next.reads) {
            auto idx = r.handle.GetIndex();
            for (auto att : prevAttachments) {
                if (idx == att) {
                    return true;
                }
            }
        }
        // Check shared depth attachment: both write same depth resource
        for (auto& pw : prev.writes) {
            if ((pw.access & ResourceAccess::DepthStencilWrite) == ResourceAccess::None) {
                continue;
            }
            for (auto& nw : next.writes) {
                if ((nw.access & ResourceAccess::DepthStencilWrite) == ResourceAccess::None) {
                    continue;
                }
                if (pw.handle.GetIndex() == nw.handle.GetIndex()) {
                    return true;
                }
            }
        }
        return false;
    }

    auto PassMerger::HasCrossQueueBetween(std::span<const CrossQueueSyncPoint> syncPoints, uint32_t posA, uint32_t posB)
        -> bool {
        for (auto& sp : syncPoints) {
            if (sp.srcPassIndex > posA && sp.srcPassIndex < posB) {
                return true;
            }
            if (sp.dstPassIndex > posA && sp.dstPassIndex < posB) {
                return true;
            }
        }
        return false;
    }

    auto PassMerger::HasAliasingConflict(const RGPassNode& prev, const RGPassNode& next, const AliasingLayout& aliasing)
        -> bool {
        if (aliasing.resourceToSlot.empty()) {
            return false;
        }
        auto prevAtts = CollectAttachmentIndices(prev);
        auto nextAtts = CollectAttachmentIndices(next);
        for (auto a : prevAtts) {
            if (a >= aliasing.resourceToSlot.size()) {
                continue;
            }
            auto slotA = aliasing.resourceToSlot[a];
            if (slotA == AliasingLayout::kNotAliased) {
                continue;
            }
            for (auto b : nextAtts) {
                if (b >= aliasing.resourceToSlot.size()) {
                    continue;
                }
                if (a != b && aliasing.resourceToSlot[b] == slotA) {
                    return true;
                }
            }
        }
        return false;
    }

    auto PassMerger::HasUavAndColorWrite(const RGPassNode& pass) -> bool {
        bool hasColor = false;
        bool hasUav = false;
        for (auto& w : pass.writes) {
            if ((w.access & ResourceAccess::ColorAttachWrite) != ResourceAccess::None) {
                hasColor = true;
            }
            if ((w.access & ResourceAccess::ShaderWrite) != ResourceAccess::None) {
                hasUav = true;
            }
        }
        return hasColor && hasUav;
    }

    // =========================================================================
    // CanMerge — evaluate all 6 conditions + profitability
    // =========================================================================

    auto PassMerger::CanMerge(
        const RGPassNode& prevPass, const RGPassNode& candidatePass, std::span<const RGResourceNode> resources,
        const MergedRenderPassGroup& currentGroup, const AliasingLayout& aliasing,
        std::span<const CrossQueueSyncPoint> syncPoints, uint32_t prevPos, uint32_t candidatePos,
        const CompiledPassInfo& candidateCpi
    ) const -> bool {
        // Condition 1: must be graphics (checked by caller)

        // Condition 2: same render area
        auto [curW, curH] = GetPassRenderArea(candidatePass, resources);
        if (curW != currentGroup.renderAreaWidth || curH != currentGroup.renderAreaHeight) {
            return false;
        }

        // Condition 3: shared attachment (input attachment read or shared depth)
        if (!HasSharedAttachment(prevPass, candidatePass, resources)) {
            return false;
        }

        // Condition 4: no cross-queue consumer between them
        if (HasCrossQueueBetween(syncPoints, prevPos, candidatePos)) {
            return false;
        }

        // Condition 5: no aliasing conflict
        if (HasAliasingConflict(prevPass, candidatePass, aliasing)) {
            return false;
        }

        // Condition 6: no host read/write/present
        if (HasMergeBlockingAccess(candidatePass) || HasMergeBlockingAccess(prevPass)) {
            return false;
        }

        // Profitability: subpass count limit
        if (currentGroup.subpassIndices.size() >= kMaxSubpassesPerGroup) {
            return false;
        }

        // Profitability: attachment count limit
        bool isTileBased = config_.capabilities && config_.capabilities->isTileBasedGpu;
        uint32_t maxAttachments
            = config_.capabilities ? config_.capabilities->maxColorAttachments : kMaxAttachmentsPerGroup;
        auto newAtts = CollectAttachmentIndices(candidatePass);
        uint32_t uniqueAttCount = static_cast<uint32_t>(currentGroup.sharedAttachments.size());
        for (auto a : newAtts) {
            bool found = false;
            for (auto existing : currentGroup.sharedAttachments) {
                if (existing == a) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                ++uniqueAttCount;
            }
        }
        if (uniqueAttCount > maxAttachments) {
            return false;
        }

        // Profitability: UAV + color write (some drivers flush tiles)
        if (!isTileBased && HasUavAndColorWrite(candidatePass)) {
            return false;
        }

        return true;
    }

    // =========================================================================
    // MergeIntoGroup — convert barriers to subpass dependencies
    // =========================================================================

    void PassMerger::MergeIntoGroup(
        MergedRenderPassGroup& group, uint32_t prevPos, uint32_t pos, const RGPassNode& pass,
        std::vector<CompiledPassInfo>& compiledPasses
    ) {
        uint32_t srcSubpass = static_cast<uint32_t>(group.subpassIndices.size()) - 1;
        uint32_t dstSubpass = static_cast<uint32_t>(group.subpassIndices.size());

        auto& cpi = compiledPasses[pos];

        // Convert inter-pass barriers to subpass dependencies
        for (auto& barrier : cpi.acquireBarriers) {
            if (barrier.isAliasingBarrier || barrier.isCrossQueue) {
                continue;
            }
            SubpassDependency dep;
            dep.srcSubpass = srcSubpass;
            dep.dstSubpass = dstSubpass;
            dep.srcAccess = barrier.srcAccess;
            dep.dstAccess = barrier.dstAccess;
            dep.srcLayout = barrier.srcLayout;
            dep.dstLayout = barrier.dstLayout;
            dep.byRegion = true;
            group.dependencies.push_back(dep);
        }

        // Remove converted barriers from the pass (subpass dependency replaces them)
        std::erase_if(cpi.acquireBarriers, [](const BarrierCommand& b) {
            return !b.isAliasingBarrier && !b.isCrossQueue;
        });
        // Also remove matching release barriers from previous pass
        std::erase_if(compiledPasses[prevPos].releaseBarriers, [](const BarrierCommand& b) {
            return !b.isAliasingBarrier && !b.isCrossQueue;
        });

        group.subpassIndices.push_back(pos);

        // Track new attachments
        auto newAtts = CollectAttachmentIndices(pass);
        for (auto a : newAtts) {
            bool found = false;
            for (auto existing : group.sharedAttachments) {
                if (existing == a) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                group.sharedAttachments.push_back(a);
            }
        }
    }

    // =========================================================================
    // Merge — main entry point
    // =========================================================================

    void PassMerger::Merge(
        const RenderGraphBuilder& builder, std::span<const uint32_t> order, const AliasingLayout& aliasing,
        std::span<const CrossQueueSyncPoint> syncPoints, std::vector<CompiledPassInfo>& compiledPasses,
        std::vector<MergedRenderPassGroup>& mergedGroups
    ) {
        if (compiledPasses.empty()) {
            return;
        }

        auto& passes = builder.GetPasses();
        auto& resources = builder.GetResources();

        MergedRenderPassGroup currentGroup;

        auto flushGroup = [&]() {
            if (currentGroup.subpassIndices.size() > 1) {
                mergedGroups.push_back(std::move(currentGroup));
            }
            currentGroup = {};
        };

        auto startGroup = [&](uint32_t pos) {
            currentGroup = {};
            currentGroup.subpassIndices.push_back(pos);
            auto passIdx = compiledPasses[pos].passIndex;
            auto [w, h] = GetPassRenderArea(passes[passIdx], resources);
            currentGroup.renderAreaWidth = w;
            currentGroup.renderAreaHeight = h;
            auto atts = CollectAttachmentIndices(passes[passIdx]);
            for (auto a : atts) {
                currentGroup.sharedAttachments.push_back(a);
            }
        };

        // Begin with first graphics pass
        if (compiledPasses[0].queue == RGQueueType::Graphics
            && (passes[compiledPasses[0].passIndex].flags & RGPassFlags::Graphics) != RGPassFlags::None) {
            startGroup(0);
        }

        for (uint32_t pos = 1; pos < compiledPasses.size(); ++pos) {
            auto& cpi = compiledPasses[pos];
            auto& pass = passes[cpi.passIndex];

            // Condition 1: must be graphics
            bool isGraphics
                = (cpi.queue == RGQueueType::Graphics) && ((pass.flags & RGPassFlags::Graphics) != RGPassFlags::None);
            if (!isGraphics) {
                flushGroup();
                continue;
            }

            // If current group is empty, start a new one
            if (currentGroup.subpassIndices.empty()) {
                startGroup(pos);
                continue;
            }

            uint32_t prevPos = currentGroup.subpassIndices.back();
            auto& prevPass = passes[compiledPasses[prevPos].passIndex];

            if (CanMerge(prevPass, pass, resources, currentGroup, aliasing, syncPoints, prevPos, pos, cpi)) {
                MergeIntoGroup(currentGroup, prevPos, pos, pass, compiledPasses);
            } else {
                flushGroup();
                startGroup(pos);
            }
        }

        flushGroup();
    }

}  // namespace miki::rg
