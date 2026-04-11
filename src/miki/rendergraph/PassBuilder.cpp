/** @file PassBuilder.cpp
 *  @brief Implementation of PassBuilder — per-pass resource binding with SSA versioning.
 */

#include "miki/rendergraph/PassBuilder.h"

#include <algorithm>
#include <cassert>

#include "miki/rendergraph/RenderGraphAdvanced.h"
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

    auto PassBuilder::WriteColorAttachment(
        RGResourceHandle handle, uint32_t index, rhi::AttachmentLoadOp loadOp, rhi::AttachmentStoreOp storeOp,
        rhi::ClearValue clearValue
    ) -> RGResourceHandle {
        auto newHandle = RecordWrite(handle, ResourceAccess::ColorAttachWrite);
        auto& pass = builder_.GetPasses()[passIndex_];
        pass.colorAttachments.push_back(
            RGAttachmentInfo{
                .handle = newHandle,
                .slotIndex = index,
                .loadOp = loadOp,
                .storeOp = storeOp,
                .clearValue = clearValue,
                .isDepthStencil = false,
            }
        );
        return newHandle;
    }

    auto PassBuilder::WriteDepthStencil(
        RGResourceHandle handle, rhi::AttachmentLoadOp loadOp, rhi::AttachmentStoreOp storeOp,
        rhi::ClearValue clearValue
    ) -> RGResourceHandle {
        auto newHandle = RecordWrite(handle, ResourceAccess::DepthStencilWrite);
        auto& pass = builder_.GetPasses()[passIndex_];
        pass.hasDepthStencil = true;
        pass.depthStencilAttachment = RGAttachmentInfo{
            .handle = newHandle,
            .slotIndex = 0,
            .loadOp = loadOp,
            .storeOp = storeOp,
            .clearValue = clearValue,
            .isDepthStencil = true,
        };
        return newHandle;
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

    auto PassBuilder::ReadHistoryTexture(RGResourceHandle handle, const char* historyName, StalenessPolicy policy)
        -> RGResourceHandle {
        assert(handle.IsValid());
        auto& resources = builder_.GetResources();
        auto idx = handle.GetIndex();
        assert(idx < resources.size());

        // Mark resource as lifetime-extended (excluded from aliasing)
        resources[idx].lifetimeExtended = true;
        resources[idx].historyConsumerCount++;
        if (historyName) {
            resources[idx].historyName = historyName;
        }
        resources[idx].defaultStalenessPolicy = policy;

        // Record history edge on the pass node
        auto& passes = builder_.GetPasses();
        assert(passIndex_ < passes.size());
        passes[passIndex_].historyReads.push_back(
            HistoryEdge{
                .handle = handle,
                .consumerPassIndex = passIndex_,
                .historyName = historyName,
            }
        );

        RecordRead(handle, ResourceAccess::ShaderReadOnly);
        return handle;
    }

    auto PassBuilder::ReadHistoryBuffer(RGResourceHandle handle, const char* historyName, StalenessPolicy policy)
        -> RGResourceHandle {
        assert(handle.IsValid());
        auto& resources = builder_.GetResources();
        auto idx = handle.GetIndex();
        assert(idx < resources.size());

        resources[idx].lifetimeExtended = true;
        resources[idx].historyConsumerCount++;
        if (historyName) {
            resources[idx].historyName = historyName;
        }
        resources[idx].defaultStalenessPolicy = policy;

        auto& passes = builder_.GetPasses();
        assert(passIndex_ < passes.size());
        passes[passIndex_].historyReads.push_back(
            HistoryEdge{
                .handle = handle,
                .consumerPassIndex = passIndex_,
                .historyName = historyName,
            }
        );

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

    void PassBuilder::SetEstimatedGpuTime(float microseconds) {
        builder_.GetPasses()[passIndex_].estimatedGpuTimeUs = microseconds;
    }

    void PassBuilder::SetEstimatedWorkGroupCount(uint32_t count) {
        builder_.GetPasses()[passIndex_].estimatedWorkGroupCount = count;
    }

    void PassBuilder::SetEstimatedTransferBytes(uint64_t bytes) {
        builder_.GetPasses()[passIndex_].estimatedTransferBytes = bytes;
    }

    // =========================================================================
    // GPGPU & Heterogeneous Compute hints (Phase L, §8)
    // =========================================================================

    void PassBuilder::SetCooperativeMatrixHint(uint32_t M, uint32_t N, uint32_t K, uint32_t batchCount) {
        // Cooperative matrix passes are large-dispatch compute workloads.
        // Hint the scheduler: if M*N*K is large, prefer async compute for overlap.
        auto& pass = builder_.GetPasses()[passIndex_];
        uint64_t totalFlops = static_cast<uint64_t>(M) * N * K * 2 * batchCount;
        // Use estimated FLOPs to set workgroup count hint (rough: 256 FLOPs per thread, 64 threads per WG)
        uint32_t estimatedWGs = static_cast<uint32_t>(std::min(totalFlops / (256 * 64), uint64_t{UINT32_MAX}));
        if (estimatedWGs > pass.estimatedWorkGroupCount) {
            pass.estimatedWorkGroupCount = estimatedWGs;
        }
    }

    void PassBuilder::SetDeviceAffinity(uint8_t /*affinityHint*/) {
        // Device affinity is recorded for diagnostics but not enforced in single-GPU mode.
        // Multi-GPU enforcement will be added in future when DeviceAffinityResolver
        // is integrated into the compiler.
        // RGPassNode does not currently store affinity — extend in future.
    }

    // =========================================================================
    // Ray tracing acceleration structure access (L-8, §16.4)
    // =========================================================================

    void PassBuilder::ReadAccelStruct(RGResourceHandle handle, ResourceAccess access) {
        assert(IsReadAccess(access) && "ReadAccelStruct requires a read access flag");
        RecordRead(handle, access);
    }

    auto PassBuilder::WriteAccelStruct(RGResourceHandle handle, ResourceAccess access) -> RGResourceHandle {
        assert(IsWriteAccess(access) && "WriteAccelStruct requires a write access flag");
        return RecordWrite(handle, access);
    }

    // =========================================================================
    // VRS image access (L-9, §16.5)
    // =========================================================================

    void PassBuilder::ReadVrsImage(RGResourceHandle handle) {
        RecordRead(handle, ResourceAccess::ShadingRateRead);
    }

    // =========================================================================
    // Sparse resource operations (L-11, §16.7)
    // =========================================================================

    void PassBuilder::SparseCommit(RGResourceHandle handle, std::span<const SparseBindOp> /*ops*/) {
        // Sparse commit is modeled as a write (binds physical pages).
        // The actual bind ops are forwarded to the executor at recording time.
        RecordWrite(handle, ResourceAccess::ShaderWrite);
    }

    void PassBuilder::SparseDecommit(RGResourceHandle handle, std::span<const SparseBindOp> /*ops*/) {
        // Sparse decommit is modeled as a write (unbinds physical pages).
        RecordWrite(handle, ResourceAccess::ShaderWrite);
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
