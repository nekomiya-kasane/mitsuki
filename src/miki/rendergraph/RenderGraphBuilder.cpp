/** @file RenderGraphBuilder.cpp
 *  @brief Implementation of RenderGraphBuilder — declarative graph construction.
 */

#include "miki/rendergraph/RenderGraphBuilder.h"

#include <cassert>
#include <utility>

namespace miki::rg {

    // =========================================================================
    // Pass declaration
    // =========================================================================

    auto RenderGraphBuilder::AddGraphicsPass(const char* name, PassSetupFn setup, PassExecuteFn execute) -> RGPassHandle {
        return AddPass(name, RGPassFlags::Graphics, RGQueueType::Graphics, std::move(setup), std::move(execute));
    }

    auto RenderGraphBuilder::AddComputePass(const char* name, PassSetupFn setup, PassExecuteFn execute) -> RGPassHandle {
        return AddPass(name, RGPassFlags::Compute, RGQueueType::Graphics, std::move(setup), std::move(execute));
    }

    auto RenderGraphBuilder::AddAsyncComputePass(const char* name, PassSetupFn setup, PassExecuteFn execute) -> RGPassHandle {
        return AddPass(name, RGPassFlags::Compute | RGPassFlags::AsyncCompute, RGQueueType::AsyncCompute, std::move(setup), std::move(execute));
    }

    auto RenderGraphBuilder::AddTransferPass(const char* name, PassSetupFn setup, PassExecuteFn execute) -> RGPassHandle {
        return AddPass(name, RGPassFlags::Transfer, RGQueueType::Transfer, std::move(setup), std::move(execute));
    }

    void RenderGraphBuilder::AddPresentPass(const char* name, RGResourceHandle backbuffer) {
        auto handle = AddPass(
            name, RGPassFlags::Present | RGPassFlags::SideEffects | RGPassFlags::NeverCull,
            RGQueueType::Graphics,
            [backbuffer](PassBuilder& pb) {
                pb.ReadTexture(backbuffer, ResourceAccess::PresentSrc);
                pb.SetSideEffects();
            },
            [](RenderPassContext&) { /* Present is handled by the executor */ }
        );
        (void)handle;
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

        // Move passes, adjusting resource handle indices
        for (auto& pass : subgraph.passes_) {
            for (auto& r : pass.reads) {
                r.handle = RGResourceHandle::Create(
                    static_cast<uint16_t>(r.handle.GetIndex() + resourceOffset),
                    r.handle.GetVersion());
            }
            for (auto& w : pass.writes) {
                w.handle = RGResourceHandle::Create(
                    static_cast<uint16_t>(w.handle.GetIndex() + resourceOffset),
                    w.handle.GetVersion());
            }
            passes_.push_back(std::move(pass));
        }

        (void)passOffset;  // passOffset available for future edge remapping
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

    auto RenderGraphBuilder::AddPass(const char* name, RGPassFlags flags, RGQueueType queue,
                                     PassSetupFn setup, PassExecuteFn execute) -> RGPassHandle {
        assert(!built_ && "Cannot modify graph after Build()");

        uint32_t passIndex = static_cast<uint32_t>(passes_.size());
        auto& pass = passes_.emplace_back();
        pass.name = name;
        pass.flags = flags;
        pass.queue = queue;
        pass.executeFn = std::move(execute);

        // Run setup lambda to record resource dependencies
        if (setup) {
            PassBuilder pb(*this, passIndex);
            setup(pb);
        }

        return RGPassHandle{passIndex};
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
