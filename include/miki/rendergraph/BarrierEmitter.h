/** @file BarrierEmitter.h
 *  @brief Stateless barrier translation utilities for the render graph executor.
 *
 *  Translates BarrierCommand (graph-level) and aliasing barriers into concrete
 *  RHI barrier descriptors and records them into command lists.
 *
 *  All functions are free functions in miki::rg — no state, no allocations,
 *  trivially testable in isolation.
 *
 *  Namespace: miki::rg
 */
#pragma once

#include <span>

#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Handle.h"

namespace miki::rg {

    // =========================================================================
    // ResourceAccess → RHI usage flag inference
    // =========================================================================

    /// @brief Infer RHI TextureUsage flags from combined ResourceAccess across all pass accesses.
    [[nodiscard]] constexpr auto InferTextureUsage(ResourceAccess combined) noexcept -> rhi::TextureUsage {
        auto usage = rhi::TextureUsage{0};
        if ((combined & ResourceAccess::ShaderReadOnly) != ResourceAccess::None) {
            usage = usage | rhi::TextureUsage::Sampled;
        }
        if ((combined & ResourceAccess::ShaderWrite) != ResourceAccess::None) {
            usage = usage | rhi::TextureUsage::Storage;
        }
        if ((combined & ResourceAccess::ColorAttachWrite) != ResourceAccess::None) {
            usage = usage | rhi::TextureUsage::ColorAttachment;
        }
        if ((combined & (ResourceAccess::DepthStencilWrite | ResourceAccess::DepthReadOnly)) != ResourceAccess::None) {
            usage = usage | rhi::TextureUsage::DepthStencil;
        }
        if ((combined & ResourceAccess::TransferSrc) != ResourceAccess::None) {
            usage = usage | rhi::TextureUsage::TransferSrc;
        }
        if ((combined & ResourceAccess::TransferDst) != ResourceAccess::None) {
            usage = usage | rhi::TextureUsage::TransferDst;
        }
        if ((combined & ResourceAccess::InputAttachment) != ResourceAccess::None) {
            usage = usage | rhi::TextureUsage::InputAttachment;
        }
        if ((combined & ResourceAccess::ShadingRateRead) != ResourceAccess::None) {
            usage = usage | rhi::TextureUsage::ShadingRate;
        }
        if (static_cast<uint32_t>(usage) == 0) {
            usage = rhi::TextureUsage::Sampled;
        }
        return usage;
    }

    /// @brief Infer RHI BufferUsage flags from combined ResourceAccess.
    [[nodiscard]] constexpr auto InferBufferUsage(ResourceAccess combined) noexcept -> rhi::BufferUsage {
        auto usage = rhi::BufferUsage{0};
        if ((combined & ResourceAccess::ShaderReadOnly) != ResourceAccess::None) {
            usage = usage | rhi::BufferUsage::Storage;
        }
        if ((combined & ResourceAccess::ShaderWrite) != ResourceAccess::None) {
            usage = usage | rhi::BufferUsage::Storage;
        }
        if ((combined & ResourceAccess::IndirectBuffer) != ResourceAccess::None) {
            usage = usage | rhi::BufferUsage::Indirect;
        }
        if ((combined & ResourceAccess::TransferSrc) != ResourceAccess::None) {
            usage = usage | rhi::BufferUsage::TransferSrc;
        }
        if ((combined & ResourceAccess::TransferDst) != ResourceAccess::None) {
            usage = usage | rhi::BufferUsage::TransferDst;
        }
        if ((combined & ResourceAccess::AccelStructRead) != ResourceAccess::None) {
            usage = usage | rhi::BufferUsage::AccelStructInput;
        }
        if ((combined & ResourceAccess::AccelStructWrite) != ResourceAccess::None) {
            usage = usage | rhi::BufferUsage::AccelStructStorage;
        }
        if (static_cast<uint32_t>(usage) == 0) {
            usage = rhi::BufferUsage::Storage;
        }
        return usage;
    }

    // =========================================================================
    // Combined access pre-computation
    // =========================================================================

    /// @brief Pre-compute combined ResourceAccess for every resource across all passes. O(P+R).
    /// Returns a vector indexed by resource index.
    [[nodiscard]] auto PrecomputeCombinedAccess(std::span<const RGPassNode> passes, uint16_t resourceCount)
        -> std::vector<ResourceAccess>;

    // =========================================================================
    // Debug label colors
    // =========================================================================

    /// @brief Queue-type-based debug label colors for PIX/RenderDoc/NSight integration.
    [[nodiscard]] constexpr auto GetQueueDebugColor(RGQueueType queue) noexcept -> std::array<float, 4> {
        switch (queue) {
            case RGQueueType::Graphics: return {0.2f, 0.4f, 0.9f, 1.0f};
            case RGQueueType::AsyncCompute: return {0.2f, 0.8f, 0.3f, 1.0f};
            case RGQueueType::Transfer: return {0.9f, 0.6f, 0.1f, 1.0f};
            default: return {0.5f, 0.5f, 0.5f, 1.0f};
        }
    }

    // =========================================================================
    // Barrier emission
    // =========================================================================

    /// @brief Translate BarrierCommand sequence to RHI barriers and record into command list.
    void EmitBarriers(
        rhi::CommandListHandle& cmd, std::span<const BarrierCommand> barriers,
        std::span<const RGResourceNode> resources, std::span<const rhi::TextureHandle> physicalTextures,
        std::span<const rhi::BufferHandle> physicalBuffers
    );

    /// @brief Emit aliasing barriers for a specific topological position.
    void EmitAliasingBarriers(
        rhi::CommandListHandle& cmd, uint32_t topoPosition, const AliasingLayout& aliasing,
        std::span<const rhi::TextureHandle> physicalTextures, std::span<const rhi::BufferHandle> physicalBuffers,
        std::span<const RGResourceNode> resources
    );

}  // namespace miki::rg
