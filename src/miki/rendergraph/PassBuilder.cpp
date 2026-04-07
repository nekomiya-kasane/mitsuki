/** @file PassBuilder.cpp
 *  @brief Implementation of PassBuilder — per-pass resource binding with SSA versioning.
 */

#include "miki/rendergraph/PassBuilder.h"

#include <cassert>

#include "miki/rendergraph/RenderGraphBuilder.h"

namespace miki::rg {

    // =========================================================================
    // Read operations (non-destructive — no version bump)
    // =========================================================================

    void PassBuilder::ReadTexture(RGResourceHandle handle, ResourceAccess access) {
        assert(IsReadAccess(access) && "ReadTexture requires a read access flag");
        RecordRead(handle, access);
    }

    void PassBuilder::ReadDepth(RGResourceHandle handle) {
        RecordRead(handle, ResourceAccess::DepthReadOnly);
    }

    void PassBuilder::ReadInputAttachment(RGResourceHandle handle) {
        RecordRead(handle, ResourceAccess::InputAttachment);
    }

    void PassBuilder::ReadBuffer(RGResourceHandle handle, ResourceAccess access) {
        assert(IsReadAccess(access) && "ReadBuffer requires a read access flag");
        RecordRead(handle, access);
    }

    // =========================================================================
    // Write operations (SSA version bump — returns new handle)
    // =========================================================================

    auto PassBuilder::WriteTexture(RGResourceHandle handle, ResourceAccess access) -> RGResourceHandle {
        assert(IsWriteAccess(access) && "WriteTexture requires a write access flag");
        return RecordWrite(handle, access);
    }

    auto PassBuilder::WriteColorAttachment(RGResourceHandle handle, uint32_t /*index*/) -> RGResourceHandle {
        return RecordWrite(handle, ResourceAccess::ColorAttachWrite);
    }

    auto PassBuilder::WriteDepthStencil(RGResourceHandle handle) -> RGResourceHandle {
        return RecordWrite(handle, ResourceAccess::DepthStencilWrite);
    }

    auto PassBuilder::WriteBuffer(RGResourceHandle handle, ResourceAccess access) -> RGResourceHandle {
        assert(IsWriteAccess(access) && "WriteBuffer requires a write access flag");
        return RecordWrite(handle, access);
    }

    // =========================================================================
    // Read-Write operations (read + write combined, single version bump)
    // =========================================================================

    auto PassBuilder::ReadWriteTexture(RGResourceHandle handle, ResourceAccess readAccess, ResourceAccess writeAccess)
        -> RGResourceHandle {
        RecordRead(handle, readAccess);
        return RecordWrite(handle, writeAccess);
    }

    auto PassBuilder::ReadWriteBuffer(RGResourceHandle handle, ResourceAccess readAccess, ResourceAccess writeAccess)
        -> RGResourceHandle {
        RecordRead(handle, readAccess);
        return RecordWrite(handle, writeAccess);
    }

    // =========================================================================
    // Per-pass resource creation (delegates to builder)
    // =========================================================================

    auto PassBuilder::CreateTexture(const RGTextureDesc& desc) -> RGResourceHandle {
        auto handle = builder_.CreateTexture(desc);
        return RecordWrite(handle, ResourceAccess::ShaderWrite);
    }

    auto PassBuilder::CreateBuffer(const RGBufferDesc& desc) -> RGResourceHandle {
        auto handle = builder_.CreateBuffer(desc);
        return RecordWrite(handle, ResourceAccess::ShaderWrite);
    }

    // =========================================================================
    // History resources (cross-frame temporal)
    // =========================================================================

    auto PassBuilder::ReadHistoryTexture(RGResourceHandle handle, const char* /*historyName*/) -> RGResourceHandle {
        assert(handle.IsValid());
        auto& resources = builder_.GetResources();
        auto idx = handle.GetIndex();
        assert(idx < resources.size());
        resources[idx].lifetimeExtended = true;
        RecordRead(handle, ResourceAccess::ShaderReadOnly);
        return handle;
    }

    auto PassBuilder::ReadHistoryBuffer(RGResourceHandle handle, const char* /*historyName*/) -> RGResourceHandle {
        assert(handle.IsValid());
        auto& resources = builder_.GetResources();
        auto idx = handle.GetIndex();
        assert(idx < resources.size());
        resources[idx].lifetimeExtended = true;
        RecordRead(handle, ResourceAccess::ShaderReadOnly);
        return handle;
    }

    // =========================================================================
    // Async task integration
    // =========================================================================

    void PassBuilder::WaitForAsyncTask(uint64_t /*taskHandle*/) {
        // TODO: Record async task dependency for timeline semaphore wait injection.
        // The compiler will translate this into a timeline semaphore wait at submission.
    }

    // =========================================================================
    // Pass metadata
    // =========================================================================

    void PassBuilder::SetSideEffects() {
        auto& pass = builder_.GetPasses()[passIndex_];
        pass.hasSideEffects = true;
        pass.flags = pass.flags | RGPassFlags::SideEffects;
    }

    void PassBuilder::SetOrderHint(int32_t hint) {
        builder_.GetPasses()[passIndex_].orderHint = hint;
    }

    // =========================================================================
    // Subresource-level access
    // =========================================================================

    void PassBuilder::ReadTextureMip(RGResourceHandle handle, uint32_t mipLevel, ResourceAccess access) {
        assert(IsReadAccess(access));
        RecordRead(handle, access, mipLevel, kAllLayers);
    }

    auto PassBuilder::WriteTextureMip(RGResourceHandle handle, uint32_t mipLevel, ResourceAccess access)
        -> RGResourceHandle {
        assert(IsWriteAccess(access));
        return RecordWrite(handle, access, mipLevel, kAllLayers);
    }

    // =========================================================================
    // Internal recording
    // =========================================================================

    void PassBuilder::RecordRead(RGResourceHandle handle, ResourceAccess access, uint32_t mip, uint32_t layer) {
        assert(handle.IsValid());
        auto& pass = builder_.GetPasses()[passIndex_];
        assert(IsAccessValidForPassType(access, pass.flags) && "ResourceAccess incompatible with pass type (B-18)");
        builder_.GetStagingReads().push_back(
            RGResourceAccess{
                .handle = handle,
                .access = access,
                .mipLevel = mip,
                .arrayLayer = layer,
            }
        );
    }

    auto PassBuilder::RecordWrite(RGResourceHandle handle, ResourceAccess access, uint32_t mip, uint32_t layer)
        -> RGResourceHandle {
        assert(handle.IsValid());
        auto newHandle = builder_.AllocateResourceVersion(handle.GetIndex());
        auto& pass = builder_.GetPasses()[passIndex_];
        assert(IsAccessValidForPassType(access, pass.flags) && "ResourceAccess incompatible with pass type (B-18)");
        builder_.GetStagingWrites().push_back(
            RGResourceAccess{
                .handle = newHandle,
                .access = access,
                .mipLevel = mip,
                .arrayLayer = layer,
            }
        );
        return newHandle;
    }

}  // namespace miki::rg
