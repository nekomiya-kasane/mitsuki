/** @file VulkanDescriptors.cpp
 *  @brief Vulkan 1.4 backend — DescriptorLayout, PipelineLayout, DescriptorSet.
 *
 *  Uses traditional VkDescriptorSet path (arch decision #3).
 *  Auto-growing descriptor pool: when active pool is exhausted, retire it
 *  and allocate a new one. All pools destroyed at device teardown.
 */

#include "miki/rhi/backend/VulkanDevice.h"

#include <array>
#include <cassert>

namespace miki::rhi {

    // =========================================================================
    // Conversion helpers
    // =========================================================================

    namespace {
        auto ToVkDescriptorType(BindingType type) -> VkDescriptorType {
            switch (type) {
                case BindingType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                case BindingType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                case BindingType::SampledTexture: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                case BindingType::StorageTexture: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                case BindingType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
                case BindingType::CombinedTextureSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                case BindingType::AccelerationStructure: return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                case BindingType::BindlessTextures: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                case BindingType::BindlessBuffers: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            }
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        }

        auto ToVkShaderStageFlags(ShaderStage stages) -> VkShaderStageFlags {
            VkShaderStageFlags flags = 0;
            auto has = [stages](ShaderStage bit) {
                return (static_cast<uint32_t>(stages) & static_cast<uint32_t>(bit)) != 0;
            };
            if (has(ShaderStage::Vertex)) {
                flags |= VK_SHADER_STAGE_VERTEX_BIT;
            }
            if (has(ShaderStage::Fragment)) {
                flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
            }
            if (has(ShaderStage::Compute)) {
                flags |= VK_SHADER_STAGE_COMPUTE_BIT;
            }
            if (has(ShaderStage::Task)) {
                flags |= VK_SHADER_STAGE_TASK_BIT_EXT;
            }
            if (has(ShaderStage::Mesh)) {
                flags |= VK_SHADER_STAGE_MESH_BIT_EXT;
            }
            if (has(ShaderStage::RayGen)) {
                flags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
            }
            if (has(ShaderStage::AnyHit)) {
                flags |= VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
            }
            if (has(ShaderStage::ClosestHit)) {
                flags |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
            }
            if (has(ShaderStage::Miss)) {
                flags |= VK_SHADER_STAGE_MISS_BIT_KHR;
            }
            if (has(ShaderStage::Intersection)) {
                flags |= VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
            }
            if (has(ShaderStage::Callable)) {
                flags |= VK_SHADER_STAGE_CALLABLE_BIT_KHR;
            }
            if (stages == ShaderStage::All) {
                flags = VK_SHADER_STAGE_ALL;
            }
            return flags;
        }
    }  // namespace

    // =========================================================================
    // Descriptor pool management (auto-growing)
    // =========================================================================

    auto VulkanDevice::AllocateDescriptorPool() -> VkDescriptorPool {
        // Each pool supports a generous mix of descriptor types.
        // When exhausted, we retire it and create a new one.
        constexpr uint32_t kPoolSize = 1024;
        constexpr uint32_t kPoolSizeCount = 8u;
        std::array<VkDescriptorPoolSize, kPoolSizeCount> poolSizes = {{
            {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = kPoolSize},
            {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = kPoolSize},
            {.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = kPoolSize},
            {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = kPoolSize / 4},
            {.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = kPoolSize / 2},
            {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = kPoolSize},
            {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, .descriptorCount = kPoolSize / 4},
            {.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, .descriptorCount = kPoolSize / 8},
        }};
        uint32_t poolSizeCount = capabilities_.hasAccelerationStructure ? kPoolSizeCount : kPoolSizeCount - 1u;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags
            = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.maxSets = kPoolSize;
        poolInfo.poolSizeCount = poolSizeCount;
        poolInfo.pPoolSizes = poolSizes.data();

        VkDescriptorPool pool = VK_NULL_HANDLE;
        vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool);
        return pool;
    }

    // =========================================================================
    // DescriptorLayout
    // =========================================================================

    auto VulkanDevice::CreateDescriptorLayoutImpl(const DescriptorLayoutDesc& desc)
        -> RhiResult<DescriptorLayoutHandle> {
        std::vector<VkDescriptorSetLayoutBinding> vkBindings;
        std::vector<VkDescriptorBindingFlags> bindingFlags;
        vkBindings.reserve(desc.bindings.size());
        bindingFlags.reserve(desc.bindings.size());

        for (auto& b : desc.bindings) {
            VkDescriptorSetLayoutBinding vkb{};
            vkb.binding = b.binding;
            vkb.descriptorType = ToVkDescriptorType(b.type);
            vkb.descriptorCount = (b.count == 0) ? 65536 : b.count;  // 0 = runtime-sized (bindless)
            vkb.stageFlags = ToVkShaderStageFlags(b.stages);
            vkb.pImmutableSamplers = nullptr;
            vkBindings.push_back(vkb);

            VkDescriptorBindingFlags flags = 0;
            if (b.count == 0) {
                flags = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
                        | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            } else {
                flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            }
            bindingFlags.push_back(flags);
        }

        VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
        flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        flagsInfo.pBindingFlags = bindingFlags.data();

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.pNext = &flagsInfo;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
        layoutInfo.pBindings = vkBindings.data();

        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        VkResult r = vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &layout);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = descriptorLayouts_.Allocate();
        if (!data) {
            vkDestroyDescriptorSetLayout(device_, layout, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->layout = layout;
        data->bindings.reserve(desc.bindings.size());
        for (auto& b : desc.bindings) {
            data->bindings.push_back({.binding = b.binding, .type = b.type});
        }
        return handle;
    }

    void VulkanDevice::DestroyDescriptorLayoutImpl(DescriptorLayoutHandle h) {
        auto* data = descriptorLayouts_.Lookup(h);
        if (!data) {
            return;
        }
        vkDestroyDescriptorSetLayout(device_, data->layout, nullptr);
        descriptorLayouts_.Free(h);
    }

    // =========================================================================
    // PipelineLayout
    // =========================================================================

    auto VulkanDevice::CreatePipelineLayoutImpl(const PipelineLayoutDesc& desc) -> RhiResult<PipelineLayoutHandle> {
        std::vector<VkDescriptorSetLayout> vkLayouts;
        vkLayouts.reserve(desc.setLayouts.size());
        for (auto& layoutHandle : desc.setLayouts) {
            auto* layoutData = descriptorLayouts_.Lookup(layoutHandle);
            if (!layoutData) {
                return std::unexpected(RhiError::InvalidHandle);
            }
            vkLayouts.push_back(layoutData->layout);
        }

        std::vector<VkPushConstantRange> vkPushConstants;
        vkPushConstants.reserve(desc.pushConstants.size());
        for (auto& pc : desc.pushConstants) {
            VkPushConstantRange range{};
            range.stageFlags = ToVkShaderStageFlags(pc.stages);
            range.offset = pc.offset;
            range.size = pc.size;
            vkPushConstants.push_back(range);
        }

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(vkLayouts.size());
        layoutInfo.pSetLayouts = vkLayouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(vkPushConstants.size());
        layoutInfo.pPushConstantRanges = vkPushConstants.data();

        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkResult r = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = pipelineLayouts_.Allocate();
        if (!data) {
            vkDestroyPipelineLayout(device_, layout, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->layout = layout;
        return handle;
    }

    void VulkanDevice::DestroyPipelineLayoutImpl(PipelineLayoutHandle h) {
        auto* data = pipelineLayouts_.Lookup(h);
        if (!data) {
            return;
        }
        vkDestroyPipelineLayout(device_, data->layout, nullptr);
        pipelineLayouts_.Free(h);
    }

    // =========================================================================
    // DescriptorSet
    // =========================================================================

    auto VulkanDevice::CreateDescriptorSetImpl(const DescriptorSetDesc& desc) -> RhiResult<DescriptorSetHandle> {
        auto* layoutData = descriptorLayouts_.Lookup(desc.layout);
        if (!layoutData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        // Ensure we have an active pool
        if (!activeDescriptorPool_) {
            activeDescriptorPool_ = AllocateDescriptorPool();
            if (!activeDescriptorPool_) {
                return std::unexpected(RhiError::OutOfDeviceMemory);
            }
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = activeDescriptorPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layoutData->layout;

        VkDescriptorSet set = VK_NULL_HANDLE;
        VkResult r = vkAllocateDescriptorSets(device_, &allocInfo, &set);

        if (r == VK_ERROR_OUT_OF_POOL_MEMORY || r == VK_ERROR_FRAGMENTED_POOL) {
            // Retire current pool, allocate new one, retry
            retiredDescriptorPools_.push_back(activeDescriptorPool_);
            activeDescriptorPool_ = AllocateDescriptorPool();
            if (!activeDescriptorPool_) {
                return std::unexpected(RhiError::OutOfDeviceMemory);
            }

            allocInfo.descriptorPool = activeDescriptorPool_;
            r = vkAllocateDescriptorSets(device_, &allocInfo, &set);
        }

        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = descriptorSets_.Allocate();
        if (!data) {
            vkFreeDescriptorSets(device_, activeDescriptorPool_, 1, &set);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->set = set;
        data->sourcePool = activeDescriptorPool_;
        data->layoutHandle = desc.layout;

        // Apply initial writes
        if (!desc.writes.empty()) {
            UpdateDescriptorSetImpl(handle, desc.writes);
        }

        return handle;
    }

    void VulkanDevice::UpdateDescriptorSetImpl(DescriptorSetHandle h, std::span<const DescriptorWrite> writes) {
        auto* setData = descriptorSets_.Lookup(h);
        if (!setData) {
            return;
        }

        auto* layoutData = descriptorLayouts_.Lookup(setData->layoutHandle);
        if (!layoutData) {
            return;
        }

        // Resolve BindingType from the layout's cached binding metadata.
        auto resolveBindingType = [&](uint32_t bindingIndex) -> BindingType {
            for (auto& info : layoutData->bindings) {
                if (info.binding == bindingIndex) {
                    return info.type;
                }
            }
            return BindingType::UniformBuffer;
        };

        std::vector<VkWriteDescriptorSet> vkWrites;
        // Pre-allocate backing storage for descriptor info structs
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        std::vector<VkDescriptorImageInfo> imageInfos;
        std::vector<VkWriteDescriptorSetAccelerationStructureKHR> accelInfos;
        std::vector<VkAccelerationStructureKHR> accelHandles;

        bufferInfos.reserve(writes.size());
        imageInfos.reserve(writes.size());
        vkWrites.reserve(writes.size());

        for (auto& write : writes) {
            VkWriteDescriptorSet vkWrite{};
            vkWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            vkWrite.dstSet = setData->set;
            vkWrite.dstBinding = write.binding;
            vkWrite.dstArrayElement = write.arrayElement;
            vkWrite.descriptorCount = 1;

            BindingType bindingType = resolveBindingType(write.binding);

            // clang-format off
            // Descriptor type is resolved from the DescriptorLayout's cached BindingType, not guessed from the resource variant.
            // ┌──────────────────────┬───────────────────────┬──────────────────────────────────────────┬──────────────────┐
            // │ Resource variant     │ BindingType            │ VkDescriptorType                         │ Info struct      │
            // ├──────────────────────┼───────────────────────┼──────────────────────────────────────────┼──────────────────┤
            // │ BufferBinding        │ UniformBuffer          │ UNIFORM_BUFFER                           │ pBufferInfo      │
            // │ BufferBinding        │ StorageBuffer          │ STORAGE_BUFFER                           │ pBufferInfo      │
            // ├──────────────────────┼───────────────────────┼──────────────────────────────────────────┼──────────────────┤
            // │ TextureBinding       │ CombinedTextureSampler │ COMBINED_IMAGE_SAMPLER                   │ pImageInfo       │
            // │ TextureBinding       │ SampledTexture         │ SAMPLED_IMAGE                            │ pImageInfo       │
            // │ TextureBinding       │ StorageTexture         │ STORAGE_IMAGE                            │ pImageInfo       │
            // │ TextureBinding       │ Sampler                │ SAMPLER                                  │ pImageInfo       │
            // ├──────────────────────┼───────────────────────┼──────────────────────────────────────────┼──────────────────┤
            // │ SamplerHandle        │ Sampler                │ SAMPLER                                  │ pImageInfo       │
            // ├──────────────────────┼───────────────────────┼──────────────────────────────────────────┼──────────────────┤
            // │ AccelStructHandle    │ AccelerationStructure  │ ACCELERATION_STRUCTURE_KHR               │ pNext            │
            // └──────────────────────┴───────────────────────┴──────────────────────────────────────────┴──────────────────┘
            // clang-format on
            if (auto* bufBinding = std::get_if<BufferBinding>(&write.resource)) {
                auto* bufData = buffers_.Lookup(bufBinding->buffer);
                if (!bufData) {
                    continue;
                }

                bufferInfos.push_back(
                    {bufData->buffer, bufBinding->offset, bufBinding->range == 0 ? VK_WHOLE_SIZE : bufBinding->range}
                );
                vkWrite.descriptorType = ToVkDescriptorType(bindingType);
                vkWrite.pBufferInfo = &bufferInfos.back();
            } else if (auto* texBinding = std::get_if<TextureBinding>(&write.resource)) {
                // Validate sampler/view presence against layout-declared BindingType
                // ┌────────────────────────┬──────────┬─────────┐
                // │ BindingType            │ view     │ sampler │
                // ├────────────────────────┼──────────┼─────────┤
                // │ CombinedTextureSampler │ required │ required│
                // │ SampledTexture         │ required │ ignored │
                // │ StorageTexture         │ required │ ignored │
                // │ Sampler                │ ignored  │ required│
                // └────────────────────────┴──────────┴─────────┘
                bool needsView = bindingType == BindingType::CombinedTextureSampler
                                 || bindingType == BindingType::SampledTexture
                                 || bindingType == BindingType::StorageTexture;
                bool needsSampler
                    = bindingType == BindingType::CombinedTextureSampler || bindingType == BindingType::Sampler;
                assert(
                    (!needsView || texBinding->view.IsValid())
                    && "TextureBinding: view required for this BindingType but not provided"
                );
                assert(
                    (!needsSampler || texBinding->sampler.IsValid())
                    && "TextureBinding: sampler required for this BindingType but not provided"
                );

                // 
                VkDescriptorImageInfo imgInfo{};
                if (texBinding->view.IsValid()) {
                    auto* viewData = textureViews_.Lookup(texBinding->view);
                    if (viewData) {
                        imgInfo.imageView = viewData->view;
                    }
                }
                if (texBinding->sampler.IsValid()) {
                    auto* sampData = samplers_.Lookup(texBinding->sampler);
                    if (sampData) {
                        imgInfo.sampler = sampData->sampler;
                    }
                }
                imgInfo.imageLayout = (bindingType == BindingType::StorageTexture)
                                          ? VK_IMAGE_LAYOUT_GENERAL
                                          : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfos.push_back(imgInfo);
                vkWrite.descriptorType = ToVkDescriptorType(bindingType);
                vkWrite.pImageInfo = &imageInfos.back();
            } else if (auto* sampHandle = std::get_if<SamplerHandle>(&write.resource)) {
                auto* sampData = samplers_.Lookup(*sampHandle);
                if (!sampData) {
                    continue;
                }

                VkDescriptorImageInfo imgInfo{};
                imgInfo.sampler = sampData->sampler;
                imageInfos.push_back(imgInfo);
                vkWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                vkWrite.pImageInfo = &imageInfos.back();
            } else if (auto* accelHandle = std::get_if<AccelStructHandle>(&write.resource)) {
                auto* accelData = accelStructs_.Lookup(*accelHandle);
                if (!accelData) {
                    continue;
                }

                accelHandles.push_back(accelData->accelStruct);
                VkWriteDescriptorSetAccelerationStructureKHR accelWrite{};
                accelWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                accelWrite.accelerationStructureCount = 1;
                accelWrite.pAccelerationStructures = &accelHandles.back();
                accelInfos.push_back(accelWrite);
                vkWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                vkWrite.pNext = &accelInfos.back();
            }

            vkWrites.push_back(vkWrite);
        }

        if (!vkWrites.empty()) {
            vkUpdateDescriptorSets(device_, static_cast<uint32_t>(vkWrites.size()), vkWrites.data(), 0, nullptr);
        }
    }

    void VulkanDevice::DestroyDescriptorSetImpl(DescriptorSetHandle h) {
        auto* data = descriptorSets_.Lookup(h);
        if (!data) {
            return;
        }
        vkFreeDescriptorSets(device_, data->sourcePool, 1, &data->set);
        descriptorSets_.Free(h);
    }

}  // namespace miki::rhi
