/** @file BarrierEmitter.cpp
 *  @brief Stateless barrier translation utilities for the render graph executor.
 *  See: BarrierEmitter.h
 */

#include "miki/rendergraph/BarrierEmitter.h"

#include "miki/rhi/backend/AllCommandBuffers.h"

#include <cassert>

namespace miki::rg {

    // =========================================================================
    // PrecomputeCombinedAccess — O(P+R) replacement for O(P*R) per-resource scan
    // =========================================================================

    auto PrecomputeCombinedAccess(std::span<const RGPassNode> passes, uint16_t resourceCount)
        -> std::vector<ResourceAccess> {
        auto combined = std::vector<ResourceAccess>(resourceCount, ResourceAccess::None);
        for (auto& pass : passes) {
            for (auto& r : pass.reads) {
                auto idx = r.handle.GetIndex();
                if (idx < resourceCount) {
                    combined[idx] = combined[idx] | r.access;
                }
            }
            for (auto& w : pass.writes) {
                auto idx = w.handle.GetIndex();
                if (idx < resourceCount) {
                    combined[idx] = combined[idx] | w.access;
                }
            }
        }
        return combined;
    }

    // =========================================================================
    // EmitBarriers — translate BarrierCommand to RHI barriers
    // =========================================================================

    void EmitBarriers(
        rhi::CommandListHandle& cmd, std::span<const BarrierCommand> barriers,
        std::span<const RGResourceNode> resources, std::span<const rhi::TextureHandle> physicalTextures,
        std::span<const rhi::BufferHandle> physicalBuffers
    ) {
        for (auto& bc : barriers) {
            if (bc.resourceIndex >= resources.size()) {
                continue;
            }

            auto srcMapping = ResolveBarrierCombined(bc.srcAccess);
            auto dstMapping = ResolveBarrierCombined(bc.dstAccess);

            auto srcLayout = bc.srcLayout != rhi::TextureLayout::Undefined ? bc.srcLayout : srcMapping.layout;
            auto dstLayout = bc.dstLayout != rhi::TextureLayout::Undefined ? bc.dstLayout : dstMapping.layout;
            auto srcStage
                = srcMapping.stage != rhi::PipelineStage::None ? srcMapping.stage : rhi::PipelineStage::TopOfPipe;
            auto dstStage
                = dstMapping.stage != rhi::PipelineStage::None ? dstMapping.stage : rhi::PipelineStage::BottomOfPipe;

            auto srcQueue = bc.isCrossQueue ? ToRhiQueueType(bc.srcQueue) : rhi::QueueType::Graphics;
            auto dstQueue = bc.isCrossQueue ? ToRhiQueueType(bc.dstQueue) : rhi::QueueType::Graphics;

            if (resources[bc.resourceIndex].kind == RGResourceKind::Texture) {
                auto texHandle = bc.resourceIndex < physicalTextures.size() ? physicalTextures[bc.resourceIndex]
                                                                            : rhi::TextureHandle{};
                if (!texHandle.IsValid()) {
                    continue;
                }

                rhi::TextureSubresourceRange subresource{};
                if (bc.mipLevel != kAllMips) {
                    subresource.baseMipLevel = bc.mipLevel;
                    subresource.mipLevelCount = 1;
                }
                if (bc.arrayLayer != kAllLayers) {
                    subresource.baseArrayLayer = bc.arrayLayer;
                    subresource.arrayLayerCount = 1;
                }

                rhi::TextureBarrierDesc desc{
                    .srcStage = srcStage,
                    .dstStage = dstStage,
                    .srcAccess = srcMapping.access,
                    .dstAccess = dstMapping.access,
                    .oldLayout = srcLayout,
                    .newLayout = dstLayout,
                    .subresource = subresource,
                    .srcQueue = srcQueue,
                    .dstQueue = dstQueue,
                };
                cmd.Dispatch([&](auto& c) { c.CmdTextureBarrier(texHandle, desc); });
            } else {
                auto bufHandle = bc.resourceIndex < physicalBuffers.size() ? physicalBuffers[bc.resourceIndex]
                                                                           : rhi::BufferHandle{};
                if (!bufHandle.IsValid()) {
                    continue;
                }

                rhi::BufferBarrierDesc desc{
                    .srcStage = srcStage,
                    .dstStage = dstStage,
                    .srcAccess = srcMapping.access,
                    .dstAccess = dstMapping.access,
                    .srcQueue = srcQueue,
                    .dstQueue = dstQueue,
                };
                cmd.Dispatch([&](auto& c) { c.CmdBufferBarrier(bufHandle, desc); });
            }
        }
    }

    // =========================================================================
    // EmitAliasingBarriers — aliasing barriers for a specific topo position
    // =========================================================================

    void EmitAliasingBarriers(
        rhi::CommandListHandle& cmd, uint32_t topoPosition, const AliasingLayout& aliasing,
        std::span<const rhi::TextureHandle> physicalTextures, std::span<const rhi::BufferHandle> physicalBuffers,
        std::span<const RGResourceNode> resources
    ) {
        for (size_t i = 0; i < aliasing.aliasingBarriers.size(); ++i) {
            if (i >= aliasing.aliasingBarrierPassPos.size()) {
                break;
            }
            if (aliasing.aliasingBarrierPassPos[i] != topoPosition) {
                continue;
            }

            auto& bc = aliasing.aliasingBarriers[i];
            if (bc.resourceIndex >= resources.size()) {
                continue;
            }

            auto dstMapping = ResolveBarrierCombined(bc.dstAccess);
            auto dstLayout = bc.dstLayout != rhi::TextureLayout::Undefined ? bc.dstLayout : dstMapping.layout;
            auto dstStage
                = dstMapping.stage != rhi::PipelineStage::None ? dstMapping.stage : rhi::PipelineStage::AllCommands;

            if (resources[bc.resourceIndex].kind == RGResourceKind::Texture) {
                auto texHandle = bc.resourceIndex < physicalTextures.size() ? physicalTextures[bc.resourceIndex]
                                                                            : rhi::TextureHandle{};
                if (!texHandle.IsValid()) {
                    continue;
                }

                rhi::TextureBarrierDesc desc{
                    .srcStage = rhi::PipelineStage::None,
                    .dstStage = dstStage,
                    .srcAccess = rhi::AccessFlags::None,
                    .dstAccess = dstMapping.access,
                    .oldLayout = rhi::TextureLayout::Undefined,
                    .newLayout = dstLayout,
                    .subresource = {},
                };
                cmd.Dispatch([&](auto& c) { c.CmdTextureBarrier(texHandle, desc); });
            } else {
                auto bufHandle = bc.resourceIndex < physicalBuffers.size() ? physicalBuffers[bc.resourceIndex]
                                                                           : rhi::BufferHandle{};
                if (!bufHandle.IsValid()) {
                    continue;
                }

                rhi::BufferBarrierDesc desc{
                    .srcStage = rhi::PipelineStage::None,
                    .dstStage = dstStage,
                    .srcAccess = rhi::AccessFlags::None,
                    .dstAccess = dstMapping.access,
                };
                cmd.Dispatch([&](auto& c) { c.CmdBufferBarrier(bufHandle, desc); });
            }
        }
    }

}  // namespace miki::rg
