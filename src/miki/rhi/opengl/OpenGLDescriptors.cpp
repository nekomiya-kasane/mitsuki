/** @file OpenGLDescriptors.cpp
 *  @brief OpenGL 4.3+ (Tier 4) backend — DescriptorLayout, PipelineLayout,
 *         DescriptorSet (direct glBindBufferRange / glBindTextureUnit / glBindSampler).
 *
 *  OpenGL has no descriptor set concept. The RHI descriptor model maps to:
 *  - DescriptorLayout: metadata only (binding type/count info)
 *  - PipelineLayout: metadata only (push constant size + set layouts)
 *  - DescriptorSet: cached binding list, applied via direct GL bind calls
 *  - Push constants: emulated via 128B UBO at binding point 0 (§6.5.1)
 */

#include "miki/rhi/backend/OpenGLDevice.h"

namespace miki::rhi {

    // =========================================================================
    // DescriptorLayout (metadata only)
    // =========================================================================

    auto OpenGLDevice::CreateDescriptorLayoutImpl(const DescriptorLayoutDesc& desc)
        -> RhiResult<DescriptorLayoutHandle> {
        auto [handle, data] = descriptorLayouts_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->bindings.reserve(desc.bindings.size());
        for (auto& b : desc.bindings) {
            data->bindings.push_back({b.binding, b.type, (b.count == 0) ? 1u : b.count});
        }
        return handle;
    }

    void OpenGLDevice::DestroyDescriptorLayoutImpl(DescriptorLayoutHandle h) {
        auto* data = descriptorLayouts_.Lookup(h);
        if (!data) {
            return;
        }
        descriptorLayouts_.Free(h);
    }

    // =========================================================================
    // PipelineLayout (metadata + push constant size)
    // =========================================================================

    auto OpenGLDevice::CreatePipelineLayoutImpl(const PipelineLayoutDesc& desc) -> RhiResult<PipelineLayoutHandle> {
        auto [handle, data] = pipelineLayouts_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        data->setLayouts.assign(desc.setLayouts.begin(), desc.setLayouts.end());

        uint32_t totalPushSize = 0;
        for (auto& pc : desc.pushConstants) {
            totalPushSize = std::max(totalPushSize, pc.offset + pc.size);
        }
        data->pushConstantSize = totalPushSize;

        return handle;
    }

    void OpenGLDevice::DestroyPipelineLayoutImpl(PipelineLayoutHandle h) {
        auto* data = pipelineLayouts_.Lookup(h);
        if (!data) {
            return;
        }
        pipelineLayouts_.Free(h);
    }

    // =========================================================================
    // DescriptorSet (cached binding list for direct GL calls)
    // =========================================================================

    auto OpenGLDevice::CreateDescriptorSetImpl(const DescriptorSetDesc& desc) -> RhiResult<DescriptorSetHandle> {
        auto* layoutData = descriptorLayouts_.Lookup(desc.layout);
        if (!layoutData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        auto [handle, data] = descriptorSets_.Allocate();
        if (!data) {
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->layout = desc.layout;
        data->resources.resize(layoutData->bindings.size());

        // Initialize with layout binding info
        for (size_t i = 0; i < layoutData->bindings.size(); ++i) {
            data->resources[i].binding = layoutData->bindings[i].binding;
            data->resources[i].type = layoutData->bindings[i].type;
        }

        if (!desc.writes.empty()) {
            UpdateDescriptorSetImpl(handle, desc.writes);
        }

        return handle;
    }

    void OpenGLDevice::UpdateDescriptorSetImpl(DescriptorSetHandle h, std::span<const DescriptorWrite> writes) {
        auto* setData = descriptorSets_.Lookup(h);
        if (!setData) {
            return;
        }

        for (auto& write : writes) {
            // Find the resource slot by binding index
            for (auto& res : setData->resources) {
                if (res.binding == write.binding) {
                    if (auto* bufBinding = std::get_if<BufferBinding>(&write.resource)) {
                        auto* bufData = buffers_.Lookup(bufBinding->buffer);
                        if (bufData) {
                            res.buffer = bufData->buffer;
                            res.offset = bufBinding->offset;
                            res.range = (bufBinding->range == 0) ? bufData->size : bufBinding->range;
                        }
                    } else if (auto* texBinding = std::get_if<TextureBinding>(&write.resource)) {
                        if (texBinding->view.IsValid()) {
                            auto* viewData = textureViews_.Lookup(texBinding->view);
                            if (viewData) {
                                res.texture = viewData->viewTexture;
                                // Cache internalFormat for StorageTexture (glBindImageTexture needs it)
                                if (res.type == BindingType::StorageTexture) {
                                    auto* texData = textures_.Lookup(viewData->parentTexture);
                                    if (texData) {
                                        res.imageFormat = texData->internalFormat;
                                    }
                                }
                            }
                        }
                        if (texBinding->sampler.IsValid()) {
                            auto* sampData = samplers_.Lookup(texBinding->sampler);
                            if (sampData) {
                                res.sampler = sampData->sampler;
                            }
                        }
                    } else if (auto* sampHandle = std::get_if<SamplerHandle>(&write.resource)) {
                        auto* sampData = samplers_.Lookup(*sampHandle);
                        if (sampData) {
                            res.sampler = sampData->sampler;
                        }
                    }
                    break;
                }
            }
        }
    }

    void OpenGLDevice::DestroyDescriptorSetImpl(DescriptorSetHandle h) {
        auto* data = descriptorSets_.Lookup(h);
        if (!data) {
            return;
        }
        descriptorSets_.Free(h);
    }

}  // namespace miki::rhi
