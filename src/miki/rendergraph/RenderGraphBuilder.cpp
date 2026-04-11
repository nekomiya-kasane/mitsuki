/** @file RenderGraphBuilder.cpp
 *  @brief Implementation of RenderGraphBuilder — declarative graph construction.
 */

#include "miki/rendergraph/RenderGraphBuilder.h"

#include <cassert>
#include <utility>

#include "miki/rendergraph/RenderGraphAdvanced.h"
#include "miki/rendergraph/RenderGraphCompute.h"

namespace miki::rg {

    // =========================================================================
    // Constructor
    // =========================================================================

    RenderGraphBuilder::RenderGraphBuilder(size_t arenaCapacity) : arena_(arenaCapacity) {}

    // =========================================================================
    // Pass declaration
    // =========================================================================

    auto RenderGraphBuilder::AddGraphicsPass(const char* name, PassSetupFn setup, PassExecuteFn execute)
        -> RGPassHandle {
        return AddPass(name, RGPassFlags::Graphics, RGQueueType::Graphics, std::move(setup), std::move(execute));
    }

    auto RenderGraphBuilder::AddComputePass(const char* name, PassSetupFn setup, PassExecuteFn execute)
        -> RGPassHandle {
        return AddPass(name, RGPassFlags::Compute, RGQueueType::Graphics, std::move(setup), std::move(execute));
    }

    auto RenderGraphBuilder::AddAsyncComputePass(const char* name, PassSetupFn setup, PassExecuteFn execute)
        -> RGPassHandle {
        return AddPass(
            name, RGPassFlags::Compute | RGPassFlags::AsyncCompute, RGQueueType::AsyncCompute, std::move(setup),
            std::move(execute)
        );
    }

    auto RenderGraphBuilder::AddTransferPass(const char* name, PassSetupFn setup, PassExecuteFn execute)
        -> RGPassHandle {
        return AddPass(name, RGPassFlags::Transfer, RGQueueType::Transfer, std::move(setup), std::move(execute));
    }

    void RenderGraphBuilder::AddPresentPass(const char* name, RGResourceHandle backbuffer) {
        auto handle = AddPass(
            name, RGPassFlags::Present | RGPassFlags::SideEffects | RGPassFlags::NeverCull, RGQueueType::Graphics,
            [backbuffer](PassBuilder& pb) {
                pb.ReadTexture(backbuffer, ResourceAccess::PresentSrc);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) { /* Present is handled by the executor */ }
        );
        (void)handle;
    }

    auto RenderGraphBuilder::AddMeshShaderPass(
        const char* name, const MeshShaderPassConfig& config, PassSetupFn setup, PassExecuteFn execute
    ) -> RGPassHandle {
        auto handle = AddPass(
            name, RGPassFlags::Graphics | RGPassFlags::MeshShader, RGQueueType::Graphics, std::move(setup),
            std::move(execute)
        );
        // Store amplification rate as estimated workgroup count for the scheduler
        auto& pass = passes_[handle.index];
        uint32_t taskGroups = config.taskGroupCountX * config.taskGroupCountY * config.taskGroupCountZ;
        uint32_t estimatedMeshlets = static_cast<uint32_t>(static_cast<float>(taskGroups) * config.amplificationRate);
        pass.estimatedWorkGroupCount = estimatedMeshlets;
        return handle;
    }

    auto RenderGraphBuilder::AddSparseBindPass(const char* name, PassSetupFn setup, PassExecuteFn execute)
        -> RGPassHandle {
        // Sparse bind passes run on the graphics queue (sparse binding is a queue operation)
        // They have side effects (memory commitment changes are externally visible).
        return AddPass(
            name, RGPassFlags::SparseBind | RGPassFlags::SideEffects, RGQueueType::Graphics, std::move(setup),
            std::move(execute)
        );
    }

    // =========================================================================
    // Resource declaration
    // =========================================================================

    auto RenderGraphBuilder::CreateTexture(const RGTextureDesc& desc) -> RGResourceHandle {
        assert(!built_ && "Cannot modify graph after Build()");
        auto idx = AllocateResource(RGResourceKind::Texture, desc.debugName);
        auto& node = resources_[idx];
        node.textureDesc = desc;
        return RGResourceHandle::Create(idx, 0);
    }

    auto RenderGraphBuilder::CreateBuffer(const RGBufferDesc& desc) -> RGResourceHandle {
        assert(!built_ && "Cannot modify graph after Build()");
        auto idx = AllocateResource(RGResourceKind::Buffer, desc.debugName);
        auto& node = resources_[idx];
        node.bufferDesc = desc;
        return RGResourceHandle::Create(idx, 0);
    }

    auto RenderGraphBuilder::ImportTexture(rhi::TextureHandle texture, const char* name) -> RGResourceHandle {
        assert(!built_ && "Cannot modify graph after Build()");
        auto idx = AllocateResource(RGResourceKind::Texture, name);
        auto& node = resources_[idx];
        node.imported = true;
        node.importedTexture = texture;
        return RGResourceHandle::Create(idx, 0);
    }

    auto RenderGraphBuilder::ImportBuffer(rhi::BufferHandle buffer, const char* name) -> RGResourceHandle {
        assert(!built_ && "Cannot modify graph after Build()");
        auto idx = AllocateResource(RGResourceKind::Buffer, name);
        auto& node = resources_[idx];
        node.imported = true;
        node.importedBuffer = buffer;
        return RGResourceHandle::Create(idx, 0);
    }

    auto RenderGraphBuilder::ImportBackbuffer(rhi::TextureHandle backbuffer, const char* name) -> RGResourceHandle {
        return ImportTexture(backbuffer, name);
    }

    auto RenderGraphBuilder::CreateAccelStruct(const RGAccelStructDesc& desc) -> RGResourceHandle {
        assert(!built_ && "Cannot modify graph after Build()");
        auto idx = AllocateResource(RGResourceKind::AccelerationStructure, desc.debugName);
        // AccelerationStructure resources use bufferDesc.size to store estimated size
        auto& node = resources_[idx];
        node.bufferDesc.size = desc.estimatedSize;
        node.bufferDesc.debugName = desc.debugName;
        return RGResourceHandle::Create(idx, 0);
    }

    auto RenderGraphBuilder::DeclareSparseTexture(const RGSparseTextureDesc& desc) -> RGResourceHandle {
        assert(!built_ && "Cannot modify graph after Build()");
        auto idx = AllocateResource(RGResourceKind::SparseTexture, desc.debugName);
        auto& node = resources_[idx];
        // Store in textureDesc for dimension/format information
        node.textureDesc.width = desc.width;
        node.textureDesc.height = desc.height;
        node.textureDesc.depth = desc.depth;
        node.textureDesc.format = desc.format;
        node.textureDesc.mipLevels = desc.mipLevels;
        node.textureDesc.arrayLayers = desc.arrayLayers;
        node.textureDesc.dimension = desc.dimension;
        node.textureDesc.debugName = desc.debugName;
        return RGResourceHandle::Create(idx, 0);
    }

    auto RenderGraphBuilder::DeclareSparseBuffer(const RGSparseBufferDesc& desc) -> RGResourceHandle {
        assert(!built_ && "Cannot modify graph after Build()");
        auto idx = AllocateResource(RGResourceKind::SparseBuffer, desc.debugName);
        auto& node = resources_[idx];
        node.bufferDesc.size = desc.size;
        node.bufferDesc.debugName = desc.debugName;
        return RGResourceHandle::Create(idx, 0);
    }

    // =========================================================================
    // Conditional execution
    // =========================================================================

    void RenderGraphBuilder::EnableIf(RGPassHandle pass, ConditionFn condition) {
        assert(!built_ && "Cannot modify graph after Build()");
        assert(pass.IsValid() && pass.index < passes_.size());
        passes_[pass.index].conditionFn = std::move(condition);
    }

    // =========================================================================
    // Subgraph composition
    // =========================================================================

    void RenderGraphBuilder::InsertSubgraph(RenderGraphBuilder&& subgraph) {
        assert(!built_ && "Cannot modify graph after Build()");
        assert(subgraph.built_ && "Subgraph must be built before insertion");

        uint16_t resourceOffset = static_cast<uint16_t>(resources_.size());
        uint32_t passOffset = static_cast<uint32_t>(passes_.size());

        // Move resources
        for (auto& res : subgraph.resources_) {
            resources_.push_back(std::move(res));
        }

        // Move passes, re-copying arena spans and adjusting resource handle indices
        for (auto& pass : subgraph.passes_) {
            // Re-copy reads/writes into this builder's arena (subgraph arena will be destroyed)
            if (!pass.reads.empty()) {
                auto newReads = arena_.CopyToArena(std::span<const RGResourceAccess>(pass.reads));
                for (auto& r : newReads) {
                    r.handle = RGResourceHandle::Create(
                        static_cast<uint16_t>(r.handle.GetIndex() + resourceOffset), r.handle.GetVersion()
                    );
                }
                pass.reads = newReads;
            }
            if (!pass.writes.empty()) {
                auto newWrites = arena_.CopyToArena(std::span<const RGResourceAccess>(pass.writes));
                for (auto& w : newWrites) {
                    w.handle = RGResourceHandle::Create(
                        static_cast<uint16_t>(w.handle.GetIndex() + resourceOffset), w.handle.GetVersion()
                    );
                }
                pass.writes = newWrites;
            }
            passes_.push_back(std::move(pass));
        }

        (void)passOffset;  // passOffset available for future edge remapping
    }

    void RenderGraphBuilder::InsertComputeSubgraph(ComputeSubgraphBuilder&& subgraph) {
        assert(!built_ && "Cannot modify graph after Build()");
        // Build the compute subgraph's internal builder, then insert as a regular subgraph.
        auto& inner = subgraph.TakeBuilder();
        inner.Build();
        InsertSubgraph(std::move(inner));
    }

    // =========================================================================
    // Build finalization
    // =========================================================================

    void RenderGraphBuilder::Build() {
        assert(!built_ && "Build() already called");
        built_ = true;
    }

    // =========================================================================
    // Internal helpers
    // =========================================================================

    auto RenderGraphBuilder::AllocateResourceVersion(uint16_t resourceIndex) -> RGResourceHandle {
        assert(resourceIndex < resources_.size());
        auto& node = resources_[resourceIndex];
        node.currentVersion++;
        return RGResourceHandle::Create(resourceIndex, node.currentVersion);
    }

    auto RenderGraphBuilder::AddPass(
        const char* name, RGPassFlags flags, RGQueueType queue, PassSetupFn setup, PassExecuteFn execute
    ) -> RGPassHandle {
        assert(!built_ && "Cannot modify graph after Build()");

        uint32_t passIndex = static_cast<uint32_t>(passes_.size());
        auto& pass = passes_.emplace_back();
        pass.name = name;
        pass.flags = flags;
        pass.queue = queue;
        pass.executeFn = std::move(execute);

        // Run setup lambda — accesses go into staging buffers
        if (setup) {
            stagingReads_.clear();
            stagingWrites_.clear();
            PassBuilder pb(*this, passIndex);
            setup(pb);
            CommitStagedAccesses(pass);
        }

        return RGPassHandle{passIndex};
    }

    void RenderGraphBuilder::CommitStagedAccesses(RGPassNode& pass) {
        if (!stagingReads_.empty()) {
            pass.reads = arena_.CopyToArena(std::span<const RGResourceAccess>(stagingReads_));
            assert(!pass.reads.empty() && "Arena exhausted during read commit");
        }
        if (!stagingWrites_.empty()) {
            pass.writes = arena_.CopyToArena(std::span<const RGResourceAccess>(stagingWrites_));
            assert(!pass.writes.empty() && "Arena exhausted during write commit");
        }
    }

    auto RenderGraphBuilder::AllocateResource(RGResourceKind kind, const char* name) -> uint16_t {
        assert(resources_.size() < RGResourceHandle::kIndexMask && "Resource index overflow");
        auto idx = static_cast<uint16_t>(resources_.size());
        auto& node = resources_.emplace_back();
        node.kind = kind;
        node.name = name;
        return idx;
    }

}  // namespace miki::rg
