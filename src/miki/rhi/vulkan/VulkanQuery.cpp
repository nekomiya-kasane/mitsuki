/** @file VulkanQuery.cpp
 *  @brief Vulkan 1.4 backend — QueryPool creation, results retrieval, timestamp period.
 */

#include "miki/rhi/backend/VulkanDevice.h"

namespace miki::rhi {

    namespace {
        auto ToVkQueryType(QueryType type) -> VkQueryType {
            switch (type) {
                case QueryType::Timestamp: return VK_QUERY_TYPE_TIMESTAMP;
                case QueryType::Occlusion: return VK_QUERY_TYPE_OCCLUSION;
                case QueryType::PipelineStatistics: return VK_QUERY_TYPE_PIPELINE_STATISTICS;
            }
            return VK_QUERY_TYPE_TIMESTAMP;
        }
    }  // namespace

    auto VulkanDevice::CreateQueryPoolImpl(const QueryPoolDesc& desc) -> RhiResult<QueryPoolHandle> {
        VkQueryPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        poolInfo.queryType = ToVkQueryType(desc.type);
        poolInfo.queryCount = desc.count;
        if (desc.type == QueryType::PipelineStatistics) {
            poolInfo.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT
                | VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT
                | VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT
                | VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT
                | VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
        }

        VkQueryPool pool = VK_NULL_HANDLE;
        VkResult r = vkCreateQueryPool(device_, &poolInfo, nullptr, &pool);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = queryPools_.Allocate();
        if (!data) {
            vkDestroyQueryPool(device_, pool, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->pool = pool;
        data->count = desc.count;
        data->type = desc.type;

        if (desc.debugName && vkSetDebugUtilsObjectNameEXT) {
            VkDebugUtilsObjectNameInfoEXT nameInfo{};
            nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            nameInfo.objectType = VK_OBJECT_TYPE_QUERY_POOL;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(pool);
            nameInfo.pObjectName = desc.debugName;
            vkSetDebugUtilsObjectNameEXT(device_, &nameInfo);
        }

        return handle;
    }

    void VulkanDevice::DestroyQueryPoolImpl(QueryPoolHandle h) {
        auto* data = queryPools_.Lookup(h);
        if (!data) return;
        vkDestroyQueryPool(device_, data->pool, nullptr);
        queryPools_.Free(h);
    }

    auto VulkanDevice::GetQueryResultsImpl(
        QueryPoolHandle h, uint32_t first, uint32_t count, std::span<uint64_t> results
    ) -> RhiResult<void> {
        auto* data = queryPools_.Lookup(h);
        if (!data) return std::unexpected(RhiError::InvalidHandle);

        VkResult r = vkGetQueryPoolResults(
            device_, data->pool, first, count,
            results.size() * sizeof(uint64_t), results.data(), sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
        );
        if (r != VK_SUCCESS && r != VK_NOT_READY) {
            return std::unexpected(RhiError::DeviceLost);
        }
        return {};
    }

    auto VulkanDevice::GetTimestampPeriodImpl() -> double {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &props);
        return static_cast<double>(props.limits.timestampPeriod);
    }

    // =========================================================================
    // Command buffer creation/destruction
    // =========================================================================

    auto VulkanDevice::CreateCommandBufferImpl(const CommandBufferDesc& desc) -> RhiResult<CommandBufferHandle> {
        uint32_t queueFamily = queueFamilies_.graphics;
        if (desc.type == QueueType::Compute) queueFamily = queueFamilies_.compute;
        if (desc.type == QueueType::Transfer) queueFamily = queueFamilies_.transfer;

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamily;

        VkCommandPool cmdPool = VK_NULL_HANDLE;
        VkResult r = vkCreateCommandPool(device_, &poolInfo, nullptr, &cmdPool);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = cmdPool;
        allocInfo.level = desc.secondary ? VK_COMMAND_BUFFER_LEVEL_SECONDARY : VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
        r = vkAllocateCommandBuffers(device_, &allocInfo, &cmdBuffer);
        if (r != VK_SUCCESS) {
            vkDestroyCommandPool(device_, cmdPool, nullptr);
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = commandBuffers_.Allocate();
        if (!data) {
            vkDestroyCommandPool(device_, cmdPool, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->pool = cmdPool;
        data->buffer = cmdBuffer;
        data->queueType = desc.type;
        return handle;
    }

    void VulkanDevice::DestroyCommandBufferImpl(CommandBufferHandle h) {
        auto* data = commandBuffers_.Lookup(h);
        if (!data) return;
        // Destroying the pool also frees all command buffers allocated from it
        vkDestroyCommandPool(device_, data->pool, nullptr);
        commandBuffers_.Free(h);
    }

}  // namespace miki::rhi
