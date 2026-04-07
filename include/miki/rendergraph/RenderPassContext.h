/** @file RenderPassContext.h
 *  @brief Pass execution interface with resource resolution and CRTP dispatch.
 *
 *  RenderPassContext is passed to pass execute lambdas. It provides:
 *  - Resource handle resolution (graph-local -> physical RHI handle)
 *  - Pre-configured rendering attachments for the current pass
 *  - Frame-scoped linear allocator for transient CPU data
 *  - CRTP zero-overhead command dispatch (one Dispatch() per pass)
 *
 *  Thread-safety: each recording thread receives its own RenderPassContext
 *  clone with a thread-local commandList. The physical handle lookup tables
 *  are immutable shared read-only spans -- no synchronization needed.
 *
 *  Namespace: miki::rg
 */
#pragma once

#include "miki/core/LinearAllocator.h"
#include "miki/rendergraph/RenderGraphTypes.h"
#include "miki/rhi/CommandBuffer.h"
#include "miki/rhi/Handle.h"
#include "miki/rhi/Pipeline.h"

namespace miki::resource {
    class ReadbackRing;
}

namespace miki::rg {

    /// @brief Execution-time context passed to each pass's execute lambda.
    struct RenderPassContext {
        rhi::CommandListHandle commandList;     ///< Type-erased recordable command list
        rhi::CommandBufferHandle bufferHandle;  ///< Opaque handle for submit/destroy
        uint32_t passIndex = 0;
        const char* passName = nullptr;

        // Physical handle lookup tables (read-only, populated by executor)
        std::span<const rhi::TextureHandle> physicalTextures = {};
        std::span<const rhi::BufferHandle> physicalBuffers = {};
        std::span<const rhi::TextureViewHandle> physicalTextureViews = {};

        // Pre-configured rendering attachments for this pass (populated by executor)
        std::span<const rhi::RenderingAttachment> colorAttachments = {};
        const rhi::RenderingAttachment* depthAttachment = nullptr;

        // Frame-scoped linear allocator for transient CPU data
        core::LinearAllocator* frameAllocator = nullptr;

        // Optional ReadbackRing for GPU->CPU readback (E-10/E-11, nullptr if not set)
        resource::ReadbackRing* readbackRing = nullptr;

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

        /// @brief Resolve a graph-local texture handle to its default texture view.
        [[nodiscard]] auto GetTextureView(RGResourceHandle res) const noexcept -> rhi::TextureViewHandle {
            auto idx = res.GetIndex();
            if (idx < physicalTextureViews.size()) {
                return physicalTextureViews[idx];
            }
            return {};
        }

        /// @brief Get a pre-configured color attachment by slot index.
        [[nodiscard]] auto GetColorAttachment(uint32_t index) const noexcept -> rhi::RenderingAttachment {
            if (index < colorAttachments.size()) {
                return colorAttachments[index];
            }
            return {};
        }

        /// @brief Get the pre-configured depth attachment (nullptr if none).
        [[nodiscard]] auto GetDepthAttachment() const noexcept -> const rhi::RenderingAttachment* {
            return depthAttachment;
        }

        /// @brief Get the frame-scoped linear allocator for transient CPU data.
        [[nodiscard]] auto GetFrameAllocator() const noexcept -> core::LinearAllocator& {
            assert(frameAllocator != nullptr && "RenderPassContext::GetFrameAllocator called with null allocator");
            return *frameAllocator;
        }

        /// @brief CRTP zero-overhead command dispatch.
        /// Calls Dispatch() once per pass to obtain the concrete backend command list,
        /// then all subsequent draw/dispatch/barrier calls within the lambda are direct
        /// function calls -- no vtable, no type-erasure overhead.
        /// Cost: 1 indirect jump (~2ns) per pass, 0ns per draw call within.
        template <typename Fn>
        auto DispatchCommands(Fn&& fn) -> void {
            commandList.Dispatch([&]<typename Impl>(rhi::CommandBufferBase<Impl>& cmd) { fn(cmd); });
        }

        /// @brief Const overload of DispatchCommands for read-only command queries.
        template <typename Fn>
        auto DispatchCommands(Fn&& fn) const -> void {
            commandList.Dispatch([&]<typename Impl>(const rhi::CommandBufferBase<Impl>& cmd) { fn(cmd); });
        }
    };

}  // namespace miki::rg
