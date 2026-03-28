/** @file VulkanAccelStruct.cpp
 *  @brief Vulkan 1.4 backend — BLAS/TLAS build sizes, creation, destruction.
 *
 *  Requires VK_KHR_acceleration_structure. Methods check capability at runtime
 *  and return FeatureNotSupported if RT hardware is unavailable.
 */

#include "miki/rhi/backend/VulkanDevice.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>

namespace miki::rhi {

    auto VulkanDevice::GetBLASBuildSizesImpl(const BLASDesc& desc) -> AccelStructBuildSizes {
        if (!capabilities_.hasAccelerationStructure || !vkGetAccelerationStructureBuildSizesKHR) {
            return {};
        }

        std::vector<VkAccelerationStructureGeometryKHR> geometries;
        std::vector<uint32_t> maxPrimitiveCounts;
        geometries.reserve(desc.geometries.size());
        maxPrimitiveCounts.reserve(desc.geometries.size());

        for (auto& geom : desc.geometries) {
            VkAccelerationStructureGeometryKHR vkGeom{};
            vkGeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            vkGeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            vkGeom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            vkGeom.geometry.triangles.vertexStride = geom.vertexStride;
            vkGeom.geometry.triangles.maxVertex = geom.vertexCount;
            vkGeom.geometry.triangles.indexType = (geom.indexType == IndexType::Uint16)
                ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
            geometries.push_back(vkGeom);
            maxPrimitiveCounts.push_back(geom.triangleCount);
        }

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = 0;
        if (static_cast<uint8_t>(desc.flags) & static_cast<uint8_t>(AccelStructBuildFlags::PreferFastTrace)) {
            buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        }
        if (static_cast<uint8_t>(desc.flags) & static_cast<uint8_t>(AccelStructBuildFlags::PreferFastBuild)) {
            buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
        }
        if (static_cast<uint8_t>(desc.flags) & static_cast<uint8_t>(AccelStructBuildFlags::AllowUpdate)) {
            buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        }
        buildInfo.geometryCount = static_cast<uint32_t>(geometries.size());
        buildInfo.pGeometries = geometries.data();

        VkAccelerationStructureBuildSizesInfoKHR sizesInfo{};
        sizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(
            device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo, maxPrimitiveCounts.data(), &sizesInfo
        );

        return {
            sizesInfo.accelerationStructureSize,
            sizesInfo.buildScratchSize,
            sizesInfo.updateScratchSize,
        };
    }

    auto VulkanDevice::GetTLASBuildSizesImpl(const TLASDesc& desc) -> AccelStructBuildSizes {
        if (!capabilities_.hasAccelerationStructure || !vkGetAccelerationStructureBuildSizesKHR) {
            return {};
        }

        VkAccelerationStructureGeometryKHR geom{};
        geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geom.geometry.instances.arrayOfPointers = VK_FALSE;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags = 0;
        if (static_cast<uint8_t>(desc.flags) & static_cast<uint8_t>(AccelStructBuildFlags::PreferFastTrace)) {
            buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        }
        if (static_cast<uint8_t>(desc.flags) & static_cast<uint8_t>(AccelStructBuildFlags::AllowUpdate)) {
            buildInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        }
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geom;

        uint32_t primitiveCount = desc.instanceCount;
        VkAccelerationStructureBuildSizesInfoKHR sizesInfo{};
        sizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(
            device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildInfo, &primitiveCount, &sizesInfo
        );

        return {
            sizesInfo.accelerationStructureSize,
            sizesInfo.buildScratchSize,
            sizesInfo.updateScratchSize,
        };
    }

    auto VulkanDevice::CreateBLASImpl(const BLASDesc& desc) -> RhiResult<AccelStructHandle> {
        if (!capabilities_.hasAccelerationStructure || !vkCreateAccelerationStructureKHR) {
            return std::unexpected(RhiError::FeatureNotSupported);
        }

        auto sizes = GetBLASBuildSizesImpl(desc);
        if (sizes.accelerationStructureSize == 0) {
            return std::unexpected(RhiError::InvalidParameter);
        }

        // Create backing buffer
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = sizes.accelerationStructureSize;
        bufInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkBuffer backingBuffer = VK_NULL_HANDLE;
        VmaAllocation backingAlloc = nullptr;
        VkResult r = vmaCreateBuffer(allocator_, &bufInfo, &allocInfo, &backingBuffer, &backingAlloc, nullptr);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = backingBuffer;
        createInfo.size = sizes.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

        VkAccelerationStructureKHR accelStruct = VK_NULL_HANDLE;
        r = vkCreateAccelerationStructureKHR(device_, &createInfo, nullptr, &accelStruct);
        if (r != VK_SUCCESS) {
            vmaDestroyBuffer(allocator_, backingBuffer, backingAlloc);
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = accelStructs_.Allocate();
        if (!data) {
            vkDestroyAccelerationStructureKHR(device_, accelStruct, nullptr);
            vmaDestroyBuffer(allocator_, backingBuffer, backingAlloc);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->accelStruct = accelStruct;
        data->backingBuffer = backingBuffer;
        data->backingAllocation = backingAlloc;
        return handle;
    }

    auto VulkanDevice::CreateTLASImpl(const TLASDesc& desc) -> RhiResult<AccelStructHandle> {
        if (!capabilities_.hasAccelerationStructure || !vkCreateAccelerationStructureKHR) {
            return std::unexpected(RhiError::FeatureNotSupported);
        }

        auto sizes = GetTLASBuildSizesImpl(desc);
        if (sizes.accelerationStructureSize == 0) {
            return std::unexpected(RhiError::InvalidParameter);
        }

        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = sizes.accelerationStructureSize;
        bufInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkBuffer backingBuffer = VK_NULL_HANDLE;
        VmaAllocation backingAlloc = nullptr;
        VkResult r = vmaCreateBuffer(allocator_, &bufInfo, &allocInfo, &backingBuffer, &backingAlloc, nullptr);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = backingBuffer;
        createInfo.size = sizes.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

        VkAccelerationStructureKHR accelStruct = VK_NULL_HANDLE;
        r = vkCreateAccelerationStructureKHR(device_, &createInfo, nullptr, &accelStruct);
        if (r != VK_SUCCESS) {
            vmaDestroyBuffer(allocator_, backingBuffer, backingAlloc);
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = accelStructs_.Allocate();
        if (!data) {
            vkDestroyAccelerationStructureKHR(device_, accelStruct, nullptr);
            vmaDestroyBuffer(allocator_, backingBuffer, backingAlloc);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->accelStruct = accelStruct;
        data->backingBuffer = backingBuffer;
        data->backingAllocation = backingAlloc;
        return handle;
    }

    void VulkanDevice::DestroyAccelStructImpl(AccelStructHandle h) {
        auto* data = accelStructs_.Lookup(h);
        if (!data) return;
        if (vkDestroyAccelerationStructureKHR) {
            vkDestroyAccelerationStructureKHR(device_, data->accelStruct, nullptr);
        }
        vmaDestroyBuffer(allocator_, data->backingBuffer, data->backingAllocation);
        accelStructs_.Free(h);
    }

}  // namespace miki::rhi
