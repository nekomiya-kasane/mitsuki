/** @file VulkanQuery.cpp
 *  @brief Vulkan 1.4 backend — QueryPool creation, results retrieval, timestamp period.
 */

#include "miki/rhi/backend/VulkanDevice.h"

#include "miki/rhi/backend/VulkanCommandBuffer.h"

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
        if (!data) {
            return;
        }
        vkDestroyQueryPool(device_, data->pool, nullptr);
        queryPools_.Free(h);
    }

    auto VulkanDevice::GetQueryResultsImpl(
        QueryPoolHandle h, uint32_t first, uint32_t count, std::span<uint64_t> results
    ) -> RhiResult<void> {
        auto* data = queryPools_.Lookup(h);
        if (!data) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        VkResult r = vkGetQueryPoolResults(
            device_, data->pool, first, count, results.size() * sizeof(uint64_t), results.data(), sizeof(uint64_t),
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
    // Command pool management (§19 — pool-level API)
    // =========================================================================

    auto VulkanDevice::CreateCommandPoolImpl(const CommandPoolDesc& desc) -> RhiResult<CommandPoolHandle> {
        uint32_t queueFamily = queueFamilies_.graphics;
        if (desc.queue == QueueType::Compute) {
            queueFamily = queueFamilies_.compute;
        } else if (desc.queue == QueueType::Transfer) {
            queueFamily = queueFamilies_.transfer;
        }

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = desc.transient ? VK_COMMAND_POOL_CREATE_TRANSIENT_BIT : 0;
        poolInfo.queueFamilyIndex = queueFamily;

        VkCommandPool vkPool = VK_NULL_HANDLE;
        VkResult r = vkCreateCommandPool(device_, &poolInfo, nullptr, &vkPool);
        if (r != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [handle, data] = commandPools_.Allocate();
        if (!data) {
            vkDestroyCommandPool(device_, vkPool, nullptr);
            return std::unexpected(RhiError::TooManyObjects);
        }
        data->pool = vkPool;
        data->queueFamilyIndex = queueFamily;
        data->queueType = desc.queue;
        return handle;
    }

    void VulkanDevice::DestroyCommandPoolImpl(CommandPoolHandle h) {
        auto* data = commandPools_.Lookup(h);
        if (!data) {
            return;
        }
        // Free cached HandlePool entries before destroying the native pool
        for (auto& entry : data->cachedBuffers) {
            if (entry.bufHandle.IsValid()) {
                commandBuffers_.Free(entry.bufHandle);
            }
        }
        data->cachedBuffers.clear();
        vkDestroyCommandPool(device_, data->pool, nullptr);
        commandPools_.Free(h);
    }

    void VulkanDevice::ResetCommandPoolImpl(CommandPoolHandle h, CommandPoolResetFlags flags) {
        auto* data = commandPools_.Lookup(h);
        if (!data) {
            return;
        }
        VkCommandPoolResetFlags vkFlags = 0;
        bool releaseResources
            = static_cast<uint8_t>(flags) & static_cast<uint8_t>(CommandPoolResetFlags::ReleaseResources);
        if (releaseResources) {
            vkFlags = VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT;
        }
        vkResetCommandPool(device_, data->pool, vkFlags);
        if (releaseResources) {
            // RELEASE_RESOURCES invalidates all VkCommandBuffers — must clear cache and free handles
            for (auto& entry : data->cachedBuffers) {
                if (entry.bufHandle.IsValid()) {
                    commandBuffers_.Free(entry.bufHandle);
                }
            }
            data->cachedBuffers.clear();
        }
        // Reset reuse cursor — cached VkCommandBuffers survive normal pool reset and are
        // implicitly returned to Initial state by vkResetCommandPool (spec §19).
        data->nextFreeIndex = 0;
    }

    auto VulkanDevice::AllocateFromPoolImpl(CommandPoolHandle pool, bool secondary)
        -> RhiResult<CommandListAcquisition> {
        auto* poolData = commandPools_.Lookup(pool);
        if (!poolData) {
            return std::unexpected(RhiError::InvalidHandle);
        }

        // Helper: populate HandlePool data and build acquisition result
        auto buildAcquisition = [&](CommandBufferHandle bufHandle, VkCommandBuffer vkCB,
                                    VulkanCommandBuffer* wrapper) -> CommandListAcquisition {
            auto* bufData = commandBuffers_.Lookup(bufHandle);
            bufData->pool = poolData->pool;
            bufData->buffer = vkCB;
            bufData->queueType = poolData->queueType;
            wrapper->Init(this, vkCB, poolData->pool, poolData->queueType);
            return {.bufferHandle = bufHandle, .listHandle = CommandListHandle(wrapper, tier_)};
        };

        // ----- Fast path: reuse cached entry (steady-state, zero-alloc) -----
        if (poolData->nextFreeIndex < poolData->cachedBuffers.size()) {
            auto& entry = poolData->cachedBuffers[poolData->nextFreeIndex++];
            return buildAcquisition(entry.bufHandle, entry.vkCB, entry.wrapper.get());
        }

        // ----- Cold path: allocate new VkCommandBuffer (warm-up only) -----
        VkCommandBufferAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = poolData->pool,
            .level = secondary ? VK_COMMAND_BUFFER_LEVEL_SECONDARY : VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer vkCB = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device_, &allocInfo, &vkCB) != VK_SUCCESS) {
            return std::unexpected(RhiError::OutOfDeviceMemory);
        }

        auto [bufHandle, bufData] = commandBuffers_.Allocate();
        if (!bufData) {
            vkFreeCommandBuffers(device_, poolData->pool, 1, &vkCB);
            return std::unexpected(RhiError::TooManyObjects);
        }

        auto wrapper = std::make_unique<VulkanCommandBuffer>();
        auto acq = buildAcquisition(bufHandle, vkCB, wrapper.get());

        poolData->cachedBuffers.push_back({.vkCB = vkCB, .bufHandle = bufHandle, .wrapper = std::move(wrapper)});
        poolData->nextFreeIndex++;
        return acq;
    }

    void VulkanDevice::FreeFromPoolImpl(CommandPoolHandle /*pool*/, const CommandListAcquisition& /*acq*/) {
        // No-op: pool-reset model (spec §19) reclaims all buffers via ResetCommandPool.
        // Individual free is unnecessary — cached entries persist until pool destruction.
    }

}  // namespace miki::rhi
