/** @file PassBuilder.h
 *  @brief Per-pass resource binding API with SSA versioning.
 *
 *  PassBuilder is the interface passed to pass setup lambdas. It records
 *  read/write dependencies on virtual resource handles, automatically
 *  incrementing SSA versions on writes.
 *
 *  Namespace: miki::rg
 */
#pragma once

#include "miki/rendergraph/RenderGraphTypes.h"

namespace miki::rg {

    class RenderGraphBuilder;

    /// @brief Per-pass resource binding interface.
    /// Constructed by RenderGraphBuilder for each AddPass call.
    /// Records resource accesses and maintains SSA version tracking.
    class PassBuilder {
       public:
        PassBuilder(RenderGraphBuilder& builder, uint32_t passIndex) noexcept
            : builder_(builder), passIndex_(passIndex) {}

        // -- Texture reads --

        /// @brief Declare a read dependency on a texture (SRV / sampled).
        /// Does NOT create a new version — reads are non-destructive.
        void ReadTexture(RGResourceHandle handle, ResourceAccess access = ResourceAccess::ShaderReadOnly);

        /// @brief Declare a read dependency on a depth buffer (read-only depth test).
        void ReadDepth(RGResourceHandle handle);

        /// @brief Declare a read dependency on an input attachment.
        void ReadInputAttachment(RGResourceHandle handle);

        // -- Buffer reads --

        /// @brief Declare a read dependency on a buffer.
        void ReadBuffer(RGResourceHandle handle, ResourceAccess access = ResourceAccess::ShaderReadOnly);

        // -- Texture writes --

        /// @brief Declare a write dependency on a texture.
        /// Returns a new SSA-versioned handle representing the written state.
        [[nodiscard]] auto WriteTexture(RGResourceHandle handle, ResourceAccess access = ResourceAccess::ShaderWrite)
            -> RGResourceHandle;

        /// @brief Declare a color attachment write with explicit load/store ops.
        [[nodiscard]] auto WriteColorAttachment(
            RGResourceHandle handle, uint32_t index = 0, rhi::AttachmentLoadOp loadOp = rhi::AttachmentLoadOp::Clear,
            rhi::AttachmentStoreOp storeOp = rhi::AttachmentStoreOp::Store, rhi::ClearValue clearValue = {}
        ) -> RGResourceHandle;

        /// @brief Declare a depth/stencil attachment write with explicit load/store ops.
        [[nodiscard]] auto WriteDepthStencil(
            RGResourceHandle handle, rhi::AttachmentLoadOp loadOp = rhi::AttachmentLoadOp::Clear,
            rhi::AttachmentStoreOp storeOp = rhi::AttachmentStoreOp::Store, rhi::ClearValue clearValue = {{}, {1.0f, 0}}
        ) -> RGResourceHandle;

        // -- Buffer writes --

        /// @brief Declare a write dependency on a buffer.
        /// Returns a new SSA-versioned handle.
        [[nodiscard]] auto WriteBuffer(RGResourceHandle handle, ResourceAccess access = ResourceAccess::ShaderWrite)
            -> RGResourceHandle;

        // -- Read-Write (aliased read + write) --

        /// @brief Declare a read-write dependency on a texture (UAV / storage image).
        /// Returns a new SSA-versioned handle.
        [[nodiscard]] auto ReadWriteTexture(
            RGResourceHandle handle, ResourceAccess readAccess = ResourceAccess::ShaderReadOnly,
            ResourceAccess writeAccess = ResourceAccess::ShaderWrite
        ) -> RGResourceHandle;

        /// @brief Declare a read-write dependency on a buffer (SSBO / UAV).
        [[nodiscard]] auto ReadWriteBuffer(
            RGResourceHandle handle, ResourceAccess readAccess = ResourceAccess::ShaderReadOnly,
            ResourceAccess writeAccess = ResourceAccess::ShaderWrite
        ) -> RGResourceHandle;

        // -- Per-pass resource creation --

        /// @brief Create a transient texture scoped to this pass.
        [[nodiscard]] auto CreateTexture(const RGTextureDesc& desc) -> RGResourceHandle;

        /// @brief Create a transient buffer scoped to this pass.
        [[nodiscard]] auto CreateBuffer(const RGBufferDesc& desc) -> RGResourceHandle;

        // -- History resources (cross-frame temporal) --

        /// @brief Read the previous frame's version of a texture.
        [[nodiscard]] auto ReadHistoryTexture(RGResourceHandle handle, const char* historyName = nullptr)
            -> RGResourceHandle;

        /// @brief Read the previous frame's version of a buffer.
        [[nodiscard]] auto ReadHistoryBuffer(RGResourceHandle handle, const char* historyName = nullptr)
            -> RGResourceHandle;

        // -- Async task integration --

        /// @brief Declare a dependency on an async task (timeline semaphore wait).
        void WaitForAsyncTask(uint64_t taskHandle);

        // -- Pass metadata --

        /// @brief Mark this pass as having side effects (never cull).
        void SetSideEffects();

        /// @brief Set an ordering hint for topological sort tiebreaking.
        void SetOrderHint(int32_t hint);

        // -- Subresource-level access --

        /// @brief Read a specific mip level of a texture.
        void ReadTextureMip(
            RGResourceHandle handle, uint32_t mipLevel, ResourceAccess access = ResourceAccess::ShaderReadOnly
        );

        /// @brief Write a specific mip level of a texture. Returns new version.
        [[nodiscard]] auto WriteTextureMip(
            RGResourceHandle handle, uint32_t mipLevel, ResourceAccess access = ResourceAccess::ShaderWrite
        ) -> RGResourceHandle;

       private:
        void RecordRead(
            RGResourceHandle handle, ResourceAccess access, uint32_t mip = kAllMips, uint32_t layer = kAllLayers
        );
        auto RecordWrite(
            RGResourceHandle handle, ResourceAccess access, uint32_t mip = kAllMips, uint32_t layer = kAllLayers
        ) -> RGResourceHandle;

        RenderGraphBuilder& builder_;
        uint32_t passIndex_;
    };

}  // namespace miki::rg
