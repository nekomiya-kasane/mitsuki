/** @file RenderPassContext.h
 *  @brief Pass execution interface with resource resolution and CRTP dispatch.
 *
 *  RenderPassContext is passed to pass execute lambdas. It provides:
 *  - Resource handle resolution (graph-local -> physical RHI handle)
 *  - Command list access
 *  - Frame allocator access
 *
 *  Namespace: miki::rg
 */
#pragma once

#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/Handle.h"

namespace miki::rg {

    /// @brief Execution-time context passed to each pass's execute lambda.
    struct RenderPassContext {
        rhi::CommandBufferHandle commandBuffer = {};
        uint32_t passIndex = 0;
        const char* passName = nullptr;

        // Physical handle lookup table (read-only, populated by executor)
        std::span<const rhi::TextureHandle> physicalTextures = {};
        std::span<const rhi::BufferHandle> physicalBuffers = {};

        /// @brief Resolve a graph-local texture handle to its physical RHI handle.
        [[nodiscard]] auto GetTexture(RGResourceHandle res) const noexcept -> rhi::TextureHandle {
            auto idx = res.GetIndex();
            if (idx < physicalTextures.size()) {
                return physicalTextures[idx];
            }
            return {};
        }

        /// @brief Resolve a graph-local buffer handle to its physical RHI handle.
        [[nodiscard]] auto GetBuffer(RGResourceHandle res) const noexcept -> rhi::BufferHandle {
            auto idx = res.GetIndex();
            if (idx < physicalBuffers.size()) {
                return physicalBuffers[idx];
            }
            return {};
        }
    };

}  // namespace miki::rg
