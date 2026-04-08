/** @file WebGPUDescriptors.cpp
 *  @brief WebGPU / Dawn (Tier 3) backend — descriptor layout, pipeline layout, descriptor set (bind group).
 */

#include "miki/rhi/backend/WebGPUDevice.h"
#include "miki/debug/StructuredLogger.h"

#include <dawn/webgpu.h>

namespace miki::rhi {

    // =========================================================================
    // Helpers
    // =========================================================================

    static auto ToWGPUShaderStageFlags(ShaderStage stages) -> WGPUShaderStage {
        WGPUShaderStage flags = 0;
        if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(ShaderStage::Vertex)) {
            flags |= WGPUShaderStage_Vertex;
        }
        if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(ShaderStage::Fragment)) {
            flags |= WGPUShaderStage_Fragment;
        }
        if (static_cast<uint32_t>(stages) & static_cast<uint32_t>(ShaderStage::Compute)) {
            flags |= WGPUShaderStage_Compute;
        }
        return flags;
    }

    // =========================================================================
    // Descriptor Layout → WGPUBindGroupLayout
    // =========================================================================

    auto WebGPUDevice::CreateDescriptorLayoutImpl(const DescriptorLayoutDesc& desc)
        -> RhiResult<DescriptorLayoutHandle> {
        auto [handle, data] = descriptorLayouts_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        std::vector<WGPUBindGroupLayoutEntry> entries;
        entries.reserve(desc.bindings.size());

        for (const auto& b : desc.bindings) {
            // User bindings in set 0 are shifted +1 to reserve binding 0 for push constant UBO
            // This shift is applied at pipeline layout creation and bind group binding time
            WGPUBindGroupLayoutEntry entry{};
            entry.binding = b.binding;
            entry.visibility = ToWGPUShaderStageFlags(b.stages);

            switch (b.type) {
                case BindingType::UniformBuffer:
                    entry.buffer.type = WGPUBufferBindingType_Uniform;
                    entry.buffer.hasDynamicOffset = false;
                    entry.buffer.minBindingSize = 0;
                    break;
                case BindingType::StorageBuffer:
                    entry.buffer.type = WGPUBufferBindingType_Storage;
                    entry.buffer.hasDynamicOffset = false;
                    entry.buffer.minBindingSize = 0;
                    break;
                case BindingType::SampledTexture:
                    entry.texture.sampleType = WGPUTextureSampleType_Float;
                    entry.texture.viewDimension = WGPUTextureViewDimension_2D;
                    entry.texture.multisampled = false;
                    break;
                case BindingType::StorageTexture:
                    entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
                    entry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
                    entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
                    break;
                case BindingType::Sampler: entry.sampler.type = WGPUSamplerBindingType_Filtering; break;
                case BindingType::CombinedTextureSampler:
                    // WebGPU doesn't have combined texture+sampler; split into two entries
                    // For now, map to texture binding (sampler must be bound separately)
                    entry.texture.sampleType = WGPUTextureSampleType_Float;
                    entry.texture.viewDimension = WGPUTextureViewDimension_2D;
                    entry.texture.multisampled = false;
                    break;
                default:
                    MIKI_LOG_WARN(
                        ::miki::debug::LogCategory::Rhi, "WebGPU: unsupported binding type {}", static_cast<int>(b.type)
                    );
                    break;
            }

            entries.push_back(entry);

            // Cache binding info
            data->bindings.push_back({
                .binding = b.binding,
                .type = b.type,
                .count = b.count,
                .stages = b.stages,
            });
        }

        WGPUBindGroupLayoutDescriptor layoutDesc{};
        layoutDesc.entryCount = static_cast<uint32_t>(entries.size());
        layoutDesc.entries = entries.data();

        data->bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device_, &layoutDesc);
        if (!data->bindGroupLayout) {
            descriptorLayouts_.Free(handle);
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        return handle;
    }

    void WebGPUDevice::DestroyDescriptorLayoutImpl(DescriptorLayoutHandle h) {
        auto* data = descriptorLayouts_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->bindGroupLayout) {
            wgpuBindGroupLayoutRelease(data->bindGroupLayout);
        }
        descriptorLayouts_.Free(h);
    }

    // =========================================================================
    // Pipeline Layout → WGPUPipelineLayout
    // =========================================================================

    auto WebGPUDevice::CreatePipelineLayoutImpl(const PipelineLayoutDesc& desc) -> RhiResult<PipelineLayoutHandle> {
        auto [handle, data] = pipelineLayouts_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        // Build bind group layout array: group(kPushConstantGroupIndex) = push constant BGL, then user BGLs
        std::vector<WGPUBindGroupLayout> bgls;
        bgls.push_back(pushConstantBindGroupLayout_);  // reserved for push constants

        for (auto setLayout : desc.setLayouts) {
            auto* layoutData = descriptorLayouts_.Lookup(setLayout);
            if (layoutData && layoutData->bindGroupLayout) {
                bgls.push_back(layoutData->bindGroupLayout);
            }
        }

        WGPUPipelineLayoutDescriptor plDesc{};
        plDesc.bindGroupLayoutCount = static_cast<uint32_t>(bgls.size());
        plDesc.bindGroupLayouts = bgls.data();

        data->layout = wgpuDeviceCreatePipelineLayout(device_, &plDesc);
        if (!data->layout) {
            pipelineLayouts_.Free(handle);
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        // Store push constant size from desc
        data->pushConstantSize = 0;
        for (const auto& pc : desc.pushConstants) {
            uint32_t end = pc.offset + pc.size;
            if (end > data->pushConstantSize) {
                data->pushConstantSize = end;
            }
        }

        data->setLayouts.assign(desc.setLayouts.begin(), desc.setLayouts.end());

        return handle;
    }

    void WebGPUDevice::DestroyPipelineLayoutImpl(PipelineLayoutHandle h) {
        auto* data = pipelineLayouts_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->layout) {
            wgpuPipelineLayoutRelease(data->layout);
        }
        pipelineLayouts_.Free(h);
    }

    // =========================================================================
    // Descriptor Set → WGPUBindGroup
    // =========================================================================

    auto WebGPUDevice::CreateDescriptorSetImpl(const DescriptorSetDesc& desc) -> RhiResult<DescriptorSetHandle> {
        auto [handle, data] = descriptorSets_.Allocate();
        if (!handle.IsValid()) {
            return std::unexpected(RhiError::TooManyObjects);
        }

        auto* layoutData = descriptorLayouts_.Lookup(desc.layout);
        if (!layoutData || !layoutData->bindGroupLayout) {
            descriptorSets_.Free(handle);
            return std::unexpected(RhiError::InvalidHandle);
        }

        data->layout = desc.layout;

        // Build bind group entries from writes
        std::vector<WGPUBindGroupEntry> entries;
        entries.reserve(desc.writes.size());

        for (const auto& write : desc.writes) {
            WGPUBindGroupEntry entry{};
            entry.binding = write.binding;

            // Determine type from layout bindings
            BindingType type{};
            for (const auto& b : layoutData->bindings) {
                if (b.binding == write.binding) {
                    type = b.type;
                    break;
                }
            }

            std::visit(
                [&](auto&& res) {
                    using T = std::decay_t<decltype(res)>;
                    if constexpr (std::is_same_v<T, BufferBinding>) {
                        auto* bufData = buffers_.Lookup(res.buffer);
                        if (bufData) {
                            entry.buffer = bufData->buffer;
                            entry.offset = res.offset;
                            entry.size = (res.range > 0) ? res.range : (bufData->size - res.offset);
                        }
                        data->resources.push_back({
                            .binding = write.binding,
                            .type = type,
                            .buffer = bufData ? bufData->buffer : nullptr,
                            .offset = res.offset,
                            .size = entry.size,
                        });
                    } else if constexpr (std::is_same_v<T, TextureBinding>) {
                        auto* viewData = textureViews_.Lookup(res.view);
                        if (viewData) {
                            entry.textureView = viewData->view;
                        }
                        auto* samplerData = samplers_.Lookup(res.sampler);
                        if (samplerData) {
                            entry.sampler = samplerData->sampler;
                        }
                        data->resources.push_back({
                            .binding = write.binding,
                            .type = type,
                            .textureView = viewData ? viewData->view : nullptr,
                            .sampler = samplerData ? samplerData->sampler : nullptr,
                        });
                    } else if constexpr (std::is_same_v<T, SamplerHandle>) {
                        auto* samplerData = samplers_.Lookup(res);
                        if (samplerData) {
                            entry.sampler = samplerData->sampler;
                        }
                        data->resources.push_back({
                            .binding = write.binding,
                            .type = type,
                            .sampler = samplerData ? samplerData->sampler : nullptr,
                        });
                    } else if constexpr (std::is_same_v<T, AccelStructHandle>) {
                        MIKI_LOG_WARN(
                            ::miki::debug::LogCategory::Rhi, "WebGPU T3: acceleration structure binding not supported"
                        );
                    }
                },
                write.resource
            );

            entries.push_back(entry);
        }

        WGPUBindGroupDescriptor bgDesc{};
        bgDesc.layout = layoutData->bindGroupLayout;
        bgDesc.entryCount = static_cast<uint32_t>(entries.size());
        bgDesc.entries = entries.data();

        data->bindGroup = wgpuDeviceCreateBindGroup(device_, &bgDesc);
        if (!data->bindGroup) {
            descriptorSets_.Free(handle);
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        return handle;
    }

    void WebGPUDevice::UpdateDescriptorSetImpl(DescriptorSetHandle h, std::span<const DescriptorWrite> writes) {
        auto* data = descriptorSets_.Lookup(h);
        if (!data) {
            return;
        }

        // WebGPU bind groups are immutable — must recreate
        if (data->bindGroup) {
            wgpuBindGroupRelease(data->bindGroup);
            data->bindGroup = nullptr;
        }
        data->resources.clear();

        auto* layoutData = descriptorLayouts_.Lookup(data->layout);
        if (!layoutData || !layoutData->bindGroupLayout) {
            return;
        }

        std::vector<WGPUBindGroupEntry> entries;
        entries.reserve(writes.size());

        for (const auto& write : writes) {
            WGPUBindGroupEntry entry{};
            entry.binding = write.binding;

            BindingType type{};
            for (const auto& b : layoutData->bindings) {
                if (b.binding == write.binding) {
                    type = b.type;
                    break;
                }
            }

            std::visit(
                [&](auto&& res) {
                    using T = std::decay_t<decltype(res)>;
                    if constexpr (std::is_same_v<T, BufferBinding>) {
                        auto* bufData = buffers_.Lookup(res.buffer);
                        if (bufData) {
                            entry.buffer = bufData->buffer;
                            entry.offset = res.offset;
                            entry.size = (res.range > 0) ? res.range : (bufData->size - res.offset);
                        }
                        data->resources.push_back({
                            .binding = write.binding,
                            .type = type,
                            .buffer = bufData ? bufData->buffer : nullptr,
                            .offset = res.offset,
                            .size = entry.size,
                        });
                    } else if constexpr (std::is_same_v<T, TextureBinding>) {
                        auto* viewData = textureViews_.Lookup(res.view);
                        if (viewData) {
                            entry.textureView = viewData->view;
                        }
                        auto* samplerData = samplers_.Lookup(res.sampler);
                        if (samplerData) {
                            entry.sampler = samplerData->sampler;
                        }
                        data->resources.push_back({
                            .binding = write.binding,
                            .type = type,
                            .textureView = viewData ? viewData->view : nullptr,
                            .sampler = samplerData ? samplerData->sampler : nullptr,
                        });
                    } else if constexpr (std::is_same_v<T, SamplerHandle>) {
                        auto* samplerData = samplers_.Lookup(res);
                        if (samplerData) {
                            entry.sampler = samplerData->sampler;
                        }
                        data->resources.push_back({
                            .binding = write.binding,
                            .type = type,
                            .sampler = samplerData ? samplerData->sampler : nullptr,
                        });
                    } else if constexpr (std::is_same_v<T, AccelStructHandle>) {
                        // Not supported on T3
                    }
                },
                write.resource
            );

            entries.push_back(entry);
        }

        WGPUBindGroupDescriptor bgDesc{};
        bgDesc.layout = layoutData->bindGroupLayout;
        bgDesc.entryCount = static_cast<uint32_t>(entries.size());
        bgDesc.entries = entries.data();

        data->bindGroup = wgpuDeviceCreateBindGroup(device_, &bgDesc);
    }

    void WebGPUDevice::DestroyDescriptorSetImpl(DescriptorSetHandle h) {
        auto* data = descriptorSets_.Lookup(h);
        if (!data) {
            return;
        }
        if (data->bindGroup) {
            wgpuBindGroupRelease(data->bindGroup);
        }
        descriptorSets_.Free(h);
    }

}  // namespace miki::rhi
